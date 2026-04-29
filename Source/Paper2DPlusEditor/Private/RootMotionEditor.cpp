// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "RootMotionEditor.h"
#include "EditorCanvasUtils.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Input/SSlider.h"
#include "PaperFlipbook.h"
#include "PaperSprite.h"
#include "Framework/Application/SlateApplication.h"
#include "ScopedTransaction.h"

/** SRootMotionEditor — Root motion tab: per-frame position authoring with canvas visualization and trajectory preview. */

#define LOCTEXT_NAMESPACE "RootMotionEditor"

namespace RootMotionEditorInternal
{
/** Convert a playback time (seconds) to a frame index using timing data. */
static int32 TimeToFrameIndex(const FFlipbookTimingData& Timing, double Time)
{
	if (Timing.TotalFrames <= 0 || Timing.TotalDurationSeconds <= 0.0f) return 0;
	for (int32 i = Timing.TotalFrames - 1; i >= 0; i--)
	{
		if (Time >= Timing.GetFrameStartTime(i))
		{
			return i;
		}
	}
	return 0;
}
}

// ==========================================
// SRootMotionEditor — CONSTRUCT / DESTROY
// ==========================================

void SRootMotionEditor::Construct(const FArguments& InArgs)
{
	Asset = InArgs._Asset;
	BuildFlipbookListFunc = InArgs._BuildFlipbookListFunc;

	if (GEditor) GEditor->RegisterForUndo(this);

	ChildSlot
	[
		// Main content: flipbook list | (toolbar + canvas) | properties
		SNew(SSplitter)
		.Orientation(Orient_Horizontal)

			// Left: Flipbook list
			+ SSplitter::Slot()
			.Value(0.18f)
			[
				SNew(SVerticalBox)

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(4, 4, 4, 4)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("FlipbooksHeader", "Flipbooks"))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
				]

				+ SVerticalBox::Slot()
				.FillHeight(1.0f)
				[
					SNew(SScrollBox)
					+ SScrollBox::Slot()
					[
						SAssignNew(FlipbookListBox, SVerticalBox)
					]
				]
			]

			// Center: Toolbar + Canvas
			+ SSplitter::Slot()
			.Value(0.64f)
			[
				SNew(SVerticalBox)

				// Toolbar (moved here so left flipbook list stays top-flush)
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0, 0, 0, 4)
				[
					BuildToolbar()
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(4, 2, 4, 0)
				[
					SNew(STextBlock)
					.Text_Lambda([this]() {
						if (!Asset.IsValid() || !Asset->Flipbooks.IsValidIndex(SelectedFlipbookIndex))
							return FText::FromString(TEXT("No Flipbook"));
						const FFlipbookProfileEntry& Entry = Asset->Flipbooks[SelectedFlipbookIndex];
						UPaperFlipbook* FB = Entry.Identity.Flipbook.LoadSynchronous();
						int32 FrameCount = FB ? FB->GetNumKeyFrames() : 0;
						return FText::Format(LOCTEXT("MotionFlipbookTitleFmt", "{0}  Frame {1}/{2}"),
							FText::FromString(Entry.Identity.FlipbookName),
							FText::AsNumber(SelectedFrameIndex + 1),
							FText::AsNumber(FrameCount));
					})
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
					.ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f)))
				]

				+ SVerticalBox::Slot()
				.FillHeight(1.0f)
				[
					SAssignNew(MotionCanvas, SRootMotionCanvas)
					.Asset(Asset.Get())
					.SelectedFlipbookIndex_Lambda([this]() { return SelectedFlipbookIndex; })
					.SelectedFrameIndex_Lambda([this]() { return SelectedFrameIndex; })
					.IsPlaying_Lambda([this]() { return bIsPlaying; })
					.PlaybackTime_Lambda([this]() { return PlaybackTime; })
					.ShowOnionSkin_Lambda([this]() { return bShowOnionSkin; })
					.ShowForwardOnionSkin_Lambda([this]() { return bShowForwardOnionSkin; })
					.OnionSkinFrames_Lambda([this]() { return OnionSkinFrames; })
					.OnionSkinOpacity_Lambda([this]() { return OnionSkinOpacity; })
				]

				// Frame strip
				+ SVerticalBox::Slot()
				.AutoHeight()
				.MaxHeight(80.0f)
				[
					SNew(SScrollBox)
					.Orientation(Orient_Horizontal)
					+ SScrollBox::Slot()
					[
						SAssignNew(FrameStripBox, SHorizontalBox)
					]
				]
			]

			// Right: Properties panel
			+ SSplitter::Slot()
			.Value(0.18f)
			[
				SNew(SScrollBox)
				+ SScrollBox::Slot()
				[
					SAssignNew(PropertiesBox, SVerticalBox)
				]
			]
	];

	// Wire canvas delegates
	MotionCanvas->OnDragStarted.BindLambda([this]()
	{
		BeginTransaction(LOCTEXT("MoveRootMotion", "Move Root Motion Position"));
		EnsureRootMotionArraySized();
	});
	MotionCanvas->OnDragEnded.BindLambda([this]()
	{
		EndTransaction();
		if (FrameStripBox.IsValid()) FrameStripBox->Invalidate(EInvalidateWidgetReason::Paint);
		if (PropertiesBox.IsValid()) PropertiesBox->Invalidate(EInvalidateWidgetReason::Paint);
	});
	MotionCanvas->OnPositionChanged.BindLambda([this](FVector2D NewPos)
	{
		FFlipbookProfileEntry* Data = GetSelectedFlipbookData();
		if (Data && Data->MotionData.RootMotion.IsValidIndex(SelectedFrameIndex))
		{
			Data->MotionData.RootMotion[SelectedFrameIndex].Position = NewPos;
		}
	});

	RefreshAll();
}

SRootMotionEditor::~SRootMotionEditor()
{
	StopPlayback();
	if (GEditor) GEditor->UnregisterForUndo(this);
}

// ==========================================
// UNDO / REDO
// ==========================================

void SRootMotionEditor::PostUndo(bool bSuccess) { StopPlayback(); bNeedsRefresh = true; }
void SRootMotionEditor::PostRedo(bool bSuccess) { StopPlayback(); bNeedsRefresh = true; }

// ==========================================
// TRANSACTION HELPERS
// ==========================================

void SRootMotionEditor::BeginTransaction(const FText& Description)
{
	ActiveTransaction = MakeUnique<FScopedTransaction>(Description);
	if (Asset.IsValid()) Asset->Modify();
}

void SRootMotionEditor::EndTransaction()
{
	ActiveTransaction.Reset();
}

// ==========================================
// TOOLBAR
// ==========================================

TSharedRef<SWidget> SRootMotionEditor::BuildToolbar()
{
	// Empty toolbar — skins mixer moved to properties panel
	return SNullWidget::NullWidget;
}

// ==========================================
// FLIPBOOK LIST
// ==========================================

TSharedRef<SWidget> SRootMotionEditor::BuildFlipbookList()
{
	return SNullWidget::NullWidget; // Rebuilt in RefreshFlipbookList
}

void SRootMotionEditor::RefreshFlipbookList()
{
	if (!FlipbookListBox.IsValid() || !Asset.IsValid()) return;
	FlipbookListBox->ClearChildren();

	if (BuildFlipbookListFunc)
	{
		BuildFlipbookListFunc(FlipbookListBox, [this](int32 i) -> TSharedRef<SWidget>
		{
			const FFlipbookProfileEntry& Anim = Asset->Flipbooks[i];
			const bool bIsSelected = (i == SelectedFlipbookIndex);
			UPaperFlipbook* LoadedFlipbook = !Anim.Identity.Flipbook.IsNull() ? Anim.Identity.Flipbook.LoadSynchronous() : nullptr;
			const bool bHasMotion = Anim.MotionData.HasRootMotion();

			return SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "NoBorder")
				.OnClicked_Lambda([this, i]() { SetSelectedFlipbook(i); return FReply::Handled(); })
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
					.BorderBackgroundColor(bIsSelected
						? FLinearColor(0.15f, 0.35f, 0.55f, 1.0f)
						: FLinearColor(0.03f, 0.03f, 0.03f, 1.0f))
					.Padding(FMargin(8, 6))
					[
						SNew(SHorizontalBox)

						// Thumbnail
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(0, 0, 8, 0)
						[
							SNew(SBox)
							.WidthOverride(44)
							.HeightOverride(44)
							[
								LoadedFlipbook
									? StaticCastSharedRef<SWidget>(SNew(SFlipbookThumbnail).Flipbook(LoadedFlipbook))
									: StaticCastSharedRef<SWidget>(SNew(SBorder)
										.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
										.HAlign(HAlign_Center).VAlign(VAlign_Center)
										[
											SNew(STextBlock)
											.Text(LOCTEXT("NoFBList", "No FB"))
											.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
											.ColorAndOpacity(FSlateColor(FLinearColor(0.55f, 0.55f, 0.55f)))
										])
							]
						]

						// Name + root motion badge
						+ SHorizontalBox::Slot()
						.FillWidth(1.0f)
						.VAlign(VAlign_Center)
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot()
							.AutoHeight()
							[
								SNew(STextBlock)
								.Text(FText::FromString(Anim.Identity.FlipbookName))
								.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
							]
							+ SVerticalBox::Slot()
							.AutoHeight()
							[
								SNew(STextBlock)
								.Visibility(bHasMotion ? EVisibility::Visible : EVisibility::Collapsed)
								.Text_Lambda([this, i]() -> FText {
									if (!Asset.IsValid() || !Asset->Flipbooks.IsValidIndex(i)) return FText::GetEmpty();
									const FFlipbookMotionData& Motion = Asset->Flipbooks[i].MotionData;
									if (!Motion.HasRootMotion()) return FText::GetEmpty();
									// Show total displacement (last frame with non-zero position)
									for (int32 f = Motion.RootMotion.Num() - 1; f >= 0; --f)
									{
										if (!Motion.RootMotion[f].Position.IsNearlyZero())
										{
											return FText::Format(LOCTEXT("RMPosBadge", "({0}, {1})"),
												FText::AsNumber(FMath::RoundToInt(Motion.RootMotion[f].Position.X)),
												FText::AsNumber(FMath::RoundToInt(Motion.RootMotion[f].Position.Y)));
										}
									}
									return FText::GetEmpty();
								})
								.Font(FCoreStyle::GetDefaultFontStyle("Regular", 7))
								.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.8f, 0.5f)))
							]
						]
					]
				];
		});
	}
}

// ==========================================
// FRAME STRIP
// ==========================================

TSharedRef<SWidget> SRootMotionEditor::BuildFrameStrip()
{
	return SNullWidget::NullWidget; // Rebuilt in RefreshFrameStrip
}

void SRootMotionEditor::RefreshFrameStrip()
{
	if (!FrameStripBox.IsValid()) return;
	FrameStripBox->ClearChildren();

	UPaperFlipbook* FB = GetSelectedFlipbook();
	if (!FB || FB->GetNumKeyFrames() == 0) return;

	for (int32 i = 0; i < FB->GetNumKeyFrames(); i++)
	{
		UPaperSprite* Sprite = FB->GetKeyFrameChecked(i).Sprite;

		FFrameStripCellArgs CellArgs;
		CellArgs.Sprite = Sprite;
		CellArgs.FrameIndex = i;
		CellArgs.IsSelected = [this, i]() { return i == SelectedFrameIndex; };
		CellArgs.OnMouseButtonDown = [this, i](const FPointerEvent& Event) -> FReply
		{
			if (Event.GetEffectingButton() == EKeys::LeftMouseButton)
			{
				OnFrameClicked(i);
				return FReply::Handled();
			}
			return FReply::Unhandled();
		};

		// Color bar = green if frame has authored root motion data
		CellArgs.bShowBottomBar = true;
		CellArgs.BottomBarColor = TAttribute<FLinearColor>::CreateLambda([this, i]()
		{
			FFlipbookProfileEntry* Data = GetSelectedFlipbookData();
			bool bHasMotion = Data && Data->MotionData.RootMotion.IsValidIndex(i) &&
				!Data->MotionData.RootMotion[i].Position.IsNearlyZero();
			return bHasMotion ? FLinearColor(0.3f, 0.8f, 0.3f) : FLinearColor(0.0f, 0.0f, 0.0f, 0.0f);
		});

		FrameStripBox->AddSlot()
		.AutoWidth()
		.Padding(1, 0)
		[
			FFrameStripCellUtils::Build(CellArgs)
		];
	}
}

// ==========================================
// PROPERTIES PANEL
// ==========================================

void SRootMotionEditor::RefreshPropertiesPanel()
{
	if (!PropertiesBox.IsValid()) return;
	PropertiesBox->ClearChildren();

	FFlipbookProfileEntry* Data = GetSelectedFlipbookData();
	if (!Data) return;

	FVector2D CurrentPos = GetCurrentFramePosition();

	// === Skins Mixer (horizontal rows) ===
	PropertiesBox->AddSlot()
	.AutoHeight()
	.Padding(8, 4, 8, 2)
	[
		SNew(STextBlock)
		.Text(LOCTEXT("SkinsHeader", "Skins"))
		.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
	];

	// Onion row: checkbox | label | slider | frames spinbox
	PropertiesBox->AddSlot()
	.AutoHeight()
	.Padding(8, 2)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
		[
			SNew(SCheckBox)
			.IsChecked_Lambda([this]() { return bShowOnionSkin ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
			.OnCheckStateChanged_Lambda([this](ECheckBoxState S) { bShowOnionSkin = (S == ECheckBoxState::Checked); })
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 6, 0)
		[
			SNew(STextBlock).Text(LOCTEXT("OnionLabel", "Onion"))
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
			.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 1.0f)))
		]
		+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center).Padding(0, 0, 4, 0)
		[
			SNew(SSlider)
			.MinValue(0.1f).MaxValue(0.8f)
			.Value_Lambda([this]() { return OnionSkinOpacity; })
			.OnValueChanged_Lambda([this](float V) { OnionSkinOpacity = V; })
			.IsEnabled_Lambda([this]() { return bShowOnionSkin; })
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(SBox).WidthOverride(36)
			[
				SNew(SSpinBox<int32>).MinValue(1).MaxValue(3)
				.Value_Lambda([this]() { return OnionSkinFrames; })
				.OnValueChanged_Lambda([this](int32 V) { OnionSkinFrames = V; })
				.IsEnabled_Lambda([this]() { return bShowOnionSkin; })
			]
		]
	];

	// Forward row: checkbox | label | slider | frames spinbox
	PropertiesBox->AddSlot()
	.AutoHeight()
	.Padding(8, 2, 8, 4)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
		[
			SNew(SCheckBox)
			.IsChecked_Lambda([this]() { return bShowForwardOnionSkin ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
			.OnCheckStateChanged_Lambda([this](ECheckBoxState S) { bShowForwardOnionSkin = (S == ECheckBoxState::Checked); })
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 6, 0)
		[
			SNew(STextBlock).Text(LOCTEXT("ForwardLabel", "Forward"))
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
			.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 1.0f, 0.5f)))
		]
		+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center).Padding(0, 0, 4, 0)
		[
			SNew(SSlider)
			.MinValue(0.1f).MaxValue(0.8f)
			.Value_Lambda([this]() { return OnionSkinOpacity; })
			.OnValueChanged_Lambda([this](float V) { OnionSkinOpacity = V; })
			.IsEnabled_Lambda([this]() { return bShowForwardOnionSkin; })
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(SBox).WidthOverride(36)
			[
				SNew(SSpinBox<int32>).MinValue(1).MaxValue(3)
				.Value_Lambda([this]() { return OnionSkinFrames; })
				.OnValueChanged_Lambda([this](int32 V) { OnionSkinFrames = V; })
				.IsEnabled_Lambda([this]() { return bShowForwardOnionSkin; })
			]
		]
	];

	// Separator
	PropertiesBox->AddSlot()
	.AutoHeight()
	.Padding(8, 2, 8, 4)
	[
		SNew(SSeparator)
	];

	// X spinbox
	PropertiesBox->AddSlot()
	.AutoHeight()
	.Padding(8, 4)
	[
		SNew(SHorizontalBox)

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0, 0, 8, 0)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("XLabel", "X"))
			.ToolTipText(LOCTEXT("RootMotionXTip", "Horizontal root motion position for this frame in pixels. The runtime computes per-frame deltas from successive positions."))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
			.ColorAndOpacity(FSlateColor(FLinearColor(0.8f, 0.3f, 0.3f)))
		]

		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		[
			SNew(SSpinBox<float>)
			.MinValue(-10000.0f)
			.MaxValue(10000.0f)
			.Delta(1.0f)
			.Value_Lambda([this]() { return GetCurrentFramePosition().X; })
			.OnValueChanged_Lambda([this](float NewX)
			{
				// Create transaction for keyboard entry (slider drag already has one)
				if (!ActiveTransaction.IsValid())
				{
					BeginTransaction(LOCTEXT("EditRootMotionX", "Edit Root Motion X"));
					EnsureRootMotionArraySized();
				}
				FVector2D Pos = GetCurrentFramePosition();
				Pos.X = NewX;
				SetCurrentFramePosition(Pos);
			})
			.OnValueCommitted_Lambda([this](float, ETextCommit::Type)
			{
				if (ActiveTransaction.IsValid())
				{
					EndTransaction();
					if (FrameStripBox.IsValid()) FrameStripBox->Invalidate(EInvalidateWidgetReason::Paint);
				}
			})
			.OnBeginSliderMovement_Lambda([this]()
			{
				BeginTransaction(LOCTEXT("EditRootMotionX", "Edit Root Motion X"));
				EnsureRootMotionArraySized();
			})
			.OnEndSliderMovement_Lambda([this](float)
			{
				EndTransaction();
				if (FrameStripBox.IsValid()) FrameStripBox->Invalidate(EInvalidateWidgetReason::Paint);
			})
		]
	];

	// Y spinbox
	PropertiesBox->AddSlot()
	.AutoHeight()
	.Padding(8, 4)
	[
		SNew(SHorizontalBox)

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0, 0, 8, 0)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("YLabel", "Y"))
			.ToolTipText(LOCTEXT("RootMotionYTip", "Vertical root motion position for this frame in pixels. Positive values move down (Paper2D convention)."))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
			.ColorAndOpacity(FSlateColor(FLinearColor(0.3f, 0.8f, 0.3f)))
		]

		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		[
			SNew(SSpinBox<float>)
			.MinValue(-10000.0f)
			.MaxValue(10000.0f)
			.Delta(1.0f)
			.Value_Lambda([this]() { return GetCurrentFramePosition().Y; })
			.OnValueChanged_Lambda([this](float NewY)
			{
				if (!ActiveTransaction.IsValid())
				{
					BeginTransaction(LOCTEXT("EditRootMotionY", "Edit Root Motion Y"));
					EnsureRootMotionArraySized();
				}
				FVector2D Pos = GetCurrentFramePosition();
				Pos.Y = NewY;
				SetCurrentFramePosition(Pos);
			})
			.OnValueCommitted_Lambda([this](float, ETextCommit::Type)
			{
				if (ActiveTransaction.IsValid())
				{
					EndTransaction();
					if (FrameStripBox.IsValid()) FrameStripBox->Invalidate(EInvalidateWidgetReason::Paint);
				}
			})
			.OnBeginSliderMovement_Lambda([this]()
			{
				BeginTransaction(LOCTEXT("EditRootMotionY", "Edit Root Motion Y"));
				EnsureRootMotionArraySized();
			})
			.OnEndSliderMovement_Lambda([this](float)
			{
				EndTransaction();
				if (FrameStripBox.IsValid()) FrameStripBox->Invalidate(EInvalidateWidgetReason::Paint);
			})
		]
	];

	// Separator before batch ops
	PropertiesBox->AddSlot()
	.AutoHeight()
	.Padding(8, 8, 8, 4)
	[
		SNew(SSeparator)
	];

	// === Batch Operations (sentence-style dropdown) ===
	{
		TSharedPtr<TArray<TSharedPtr<FString>>> SourceOptions = MakeShared<TArray<TSharedPtr<FString>>>();
		SourceOptions->Add(MakeShared<FString>(TEXT("Current Frame's Position")));
		SourceOptions->Add(MakeShared<FString>(TEXT("Reset (0, 0)")));
		SourceOptions->Add(MakeShared<FString>(TEXT("Custom Position")));
		SourceOptions->Add(MakeShared<FString>(TEXT("Interpolate")));

		TSharedPtr<TArray<TSharedPtr<FString>>> TargetOptions = MakeShared<TArray<TSharedPtr<FString>>>();
		TargetOptions->Add(MakeShared<FString>(TEXT("All Frames")));
		TargetOptions->Add(MakeShared<FString>(TEXT("Selected Frames")));
		TargetOptions->Add(MakeShared<FString>(TEXT("Remaining Frames")));
		TargetOptions->Add(MakeShared<FString>(TEXT("Custom Range")));

		auto MakeCombo = [](TSharedPtr<TArray<TSharedPtr<FString>>> Options, int32* SelectedIdx) -> TSharedRef<SWidget>
		{
			return SNew(SComboBox<TSharedPtr<FString>>)
				.OptionsSource(Options.Get())
				.OnSelectionChanged_Lambda([SelectedIdx, Options](TSharedPtr<FString> Item, ESelectInfo::Type)
				{
					if (Item.IsValid() && Options.IsValid())
					{
						*SelectedIdx = Options->IndexOfByKey(Item);
					}
				})
				.OnGenerateWidget_Lambda([](TSharedPtr<FString> Item) -> TSharedRef<SWidget>
				{
					return SNew(STextBlock).Text(FText::FromString(*Item)).Font(FCoreStyle::GetDefaultFontStyle("Regular", 8));
				})
				.InitiallySelectedItem((*Options)[*SelectedIdx])
				[
					SNew(STextBlock)
					.Text_Lambda([Options, SelectedIdx]() -> FText
					{
						return Options->IsValidIndex(*SelectedIdx) ? FText::FromString(*(*Options)[*SelectedIdx]) : FText();
					})
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
				];
		};

		PropertiesBox->AddSlot()
		.AutoHeight()
		.Padding(8, 0, 8, 4)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("BatchOpsHeader", "Batch Position Tools"))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
		];

		// "Set" [Source v]
		PropertiesBox->AddSlot()
		.AutoHeight()
		.Padding(8, 2)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
			[
				SNew(STextBlock).Text(LOCTEXT("SetLabel", "Set")).Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0, 0, 4, 0)
			[
				MakeCombo(SourceOptions, &MotionBatchSourceIndex)
			]
		];

		// Custom position X/Y spinboxes (visible when "Custom Position" selected)
		PropertiesBox->AddSlot()
		.AutoHeight()
		.Padding(20, 2, 8, 2)
		[
			SNew(SBox)
			.Visibility_Lambda([this]() { return MotionBatchSourceIndex == 2 ? EVisibility::Visible : EVisibility::Collapsed; })
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
				[
					SNew(STextBlock).Text(LOCTEXT("CustX", "X:")).Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
				]
				+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0, 0, 8, 0)
				[
					SNew(SSpinBox<float>)
					.MinValue(-9999.0f).MaxValue(9999.0f).Delta(1.0f)
					.Value_Lambda([this]() { return MotionBatchCustomValue.X; })
					.OnValueChanged_Lambda([this](float V) { MotionBatchCustomValue.X = V; })
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
				[
					SNew(STextBlock).Text(LOCTEXT("CustY", "Y:")).Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
				]
				+ SHorizontalBox::Slot().FillWidth(1.0f)
				[
					SNew(SSpinBox<float>)
					.MinValue(-9999.0f).MaxValue(9999.0f).Delta(1.0f)
					.Value_Lambda([this]() { return MotionBatchCustomValue.Y; })
					.OnValueChanged_Lambda([this](float V) { MotionBatchCustomValue.Y = V; })
				]
			]
		];

		// "to" [Target v]
		PropertiesBox->AddSlot()
		.AutoHeight()
		.Padding(8, 2)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
			[
				SNew(STextBlock).Text(LOCTEXT("ToLabel", "to")).Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0, 0, 4, 0)
			[
				MakeCombo(TargetOptions, &MotionBatchTargetIndex)
			]
		];

		// Custom range spinboxes (visible when "Custom Range" selected)
		PropertiesBox->AddSlot()
		.AutoHeight()
		.Padding(20, 2, 8, 2)
		[
			SNew(SBox)
			.Visibility_Lambda([this]() { return MotionBatchTargetIndex == 3 ? EVisibility::Visible : EVisibility::Collapsed; })
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
				[
					SNew(STextBlock).Text(LOCTEXT("BatchRangeFrom", "From:")).Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
				]
				+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0, 0, 8, 0)
				[
					SNew(SSpinBox<int32>).MinValue(0)
					.MaxValue_Lambda([this]() { return FMath::Max(0, GetFrameCount() - 1); })
					.Value_Lambda([this]() { return MotionBatchRangeStart; })
					.OnValueChanged_Lambda([this](int32 V) { MotionBatchRangeStart = V; })
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
				[
					SNew(STextBlock).Text(LOCTEXT("BatchRangeTo", "To:")).Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
				]
				+ SHorizontalBox::Slot().FillWidth(1.0f)
				[
					SNew(SSpinBox<int32>).MinValue(0)
					.MaxValue_Lambda([this]() { return FMath::Max(0, GetFrameCount() - 1); })
					.Value_Lambda([this]() { return MotionBatchRangeEnd; })
					.OnValueChanged_Lambda([this](int32 V) { MotionBatchRangeEnd = V; })
				]
			]
		];

		// Apply button
		PropertiesBox->AddSlot()
		.AutoHeight()
		.Padding(8, 4)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
			.HAlign(HAlign_Center)
			.ToolTipText_Lambda([this]() -> FText {
				if (MotionBatchSourceIndex == 3 && MotionBatchTargetIndex != 3)
					return LOCTEXT("ApplyBatchMotionTipLerp", "Interpolate requires Custom Range target");
				return LOCTEXT("ApplyBatchMotionTip", "Apply the selected batch position operation");
			})
			.IsEnabled_Lambda([this]() {
				FFlipbookProfileEntry* Data = GetSelectedFlipbookData();
				if (!Data) return false;
				if (MotionBatchSourceIndex == 0 && !Data->MotionData.RootMotion.IsValidIndex(SelectedFrameIndex)) return false;
				if (MotionBatchSourceIndex == 3 && MotionBatchTargetIndex != 3) return false; // Interpolate requires Custom Range
				return true;
			})
			.OnClicked_Lambda([this]() { OnApplyMotionBatchOperation(); return FReply::Handled(); })
			[
				SNew(STextBlock)
				.Text(LOCTEXT("ApplyBatch", "Apply"))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
			]
		];
	}

}

TSharedRef<SWidget> SRootMotionEditor::BuildPropertiesPanel()
{
	return SNullWidget::NullWidget; // Rebuilt in RefreshPropertiesPanel
}

// ==========================================
// FRAME SELECTION
// ==========================================

void SRootMotionEditor::OnFrameClicked(int32 FrameIndex)
{
	if (FrameIndex == SelectedFrameIndex) return;
	SelectedFrameIndex = FrameIndex;
	// Invalidate only — frame strip selection and properties use lambda bindings
	if (FrameStripBox.IsValid()) FrameStripBox->Invalidate(EInvalidateWidgetReason::Paint);
	if (PropertiesBox.IsValid()) PropertiesBox->Invalidate(EInvalidateWidgetReason::Paint);
	if (MotionCanvas.IsValid()) MotionCanvas->Invalidate(EInvalidateWidgetReason::Paint);
}

// ==========================================
// ROOT MOTION EDITING
// ==========================================

void SRootMotionEditor::EnsureRootMotionArraySized()
{
	FFlipbookProfileEntry* Data = GetSelectedFlipbookData();
	if (!Data) return;

	int32 FrameCount = GetFrameCount();
	if (FrameCount <= 0) return;

	if (Data->MotionData.RootMotion.Num() < FrameCount)
	{
		Data->MotionData.RootMotion.SetNum(FrameCount);
	}
}

void SRootMotionEditor::SetCurrentFramePosition(FVector2D NewPosition)
{
	FFlipbookProfileEntry* Data = GetSelectedFlipbookData();
	if (!Data || !Asset.IsValid()) return;

	EnsureRootMotionArraySized();

	if (Data->MotionData.RootMotion.IsValidIndex(SelectedFrameIndex))
	{
		Data->MotionData.RootMotion[SelectedFrameIndex].Position = NewPosition;
		if (MotionCanvas.IsValid()) MotionCanvas->Invalidate(EInvalidateWidgetReason::Paint);
	}
}

void SRootMotionEditor::OnApplyMotionBatchOperation()
{
	FFlipbookProfileEntry* Data = GetSelectedFlipbookData();
	if (!Data) return;

	EnsureRootMotionArraySized();
	const int32 FrameCount = Data->MotionData.RootMotion.Num();
	if (FrameCount == 0) return;

	// Determine source position
	FVector2D SourcePos = FVector2D::ZeroVector;
	if (MotionBatchSourceIndex == 0) // Current Frame's Position
	{
		if (!Data->MotionData.RootMotion.IsValidIndex(SelectedFrameIndex)) return;
		SourcePos = Data->MotionData.RootMotion[SelectedFrameIndex].Position;
	}
	else if (MotionBatchSourceIndex == 1) // Reset (0, 0)
	{
		SourcePos = FVector2D::ZeroVector;
	}
	else if (MotionBatchSourceIndex == 2) // Custom Position
	{
		SourcePos = MotionBatchCustomValue;
	}
	else if (MotionBatchSourceIndex == 3) // Interpolate
	{
		// Interpolate requires Custom Range target
		if (MotionBatchTargetIndex != 3) return;
		int32 First = FMath::Clamp(FMath::Min(MotionBatchRangeStart, MotionBatchRangeEnd), 0, FrameCount - 1);
		int32 Last = FMath::Clamp(FMath::Max(MotionBatchRangeStart, MotionBatchRangeEnd), 0, FrameCount - 1);
		if (First == Last || !Data->MotionData.RootMotion.IsValidIndex(First) || !Data->MotionData.RootMotion.IsValidIndex(Last)) return;

		const FVector2D StartPos = Data->MotionData.RootMotion[First].Position;
		const FVector2D EndPos = Data->MotionData.RootMotion[Last].Position;
		const int32 Range = Last - First;

		BeginTransaction(LOCTEXT("LerpRootMotion", "Interpolate Root Motion"));
		for (int32 i = First; i <= Last; i++)
		{
			float Alpha = (float)(i - First) / (float)Range;
			Data->MotionData.RootMotion[i].Position = FMath::Lerp(StartPos, EndPos, Alpha);
		}
		EndTransaction();

		if (FrameStripBox.IsValid()) FrameStripBox->Invalidate(EInvalidateWidgetReason::Paint);
		if (MotionCanvas.IsValid()) MotionCanvas->Invalidate(EInvalidateWidgetReason::Paint);
		if (PropertiesBox.IsValid()) PropertiesBox->Invalidate(EInvalidateWidgetReason::Paint);
		OnRootMotionDataModified.ExecuteIfBound();
		return; // Early return - don't fall through to the generic apply below
	}

	// Build target frame list
	TArray<int32> TargetFrames;
	if (MotionBatchTargetIndex == 0) // All Frames
	{
		for (int32 i = 0; i < FrameCount; i++) TargetFrames.Add(i);
	}
	else if (MotionBatchTargetIndex == 1) // Selected Frames (current frame only since root motion has no multi-select)
	{
		if (Data->MotionData.RootMotion.IsValidIndex(SelectedFrameIndex))
		{
			TargetFrames.Add(SelectedFrameIndex);
		}
	}
	else if (MotionBatchTargetIndex == 2) // Remaining Frames
	{
		for (int32 i = SelectedFrameIndex; i < FrameCount; i++) TargetFrames.Add(i);
	}
	else if (MotionBatchTargetIndex == 3) // Custom Range
	{
		int32 First = FMath::Clamp(FMath::Min(MotionBatchRangeStart, MotionBatchRangeEnd), 0, FrameCount - 1);
		int32 Last = FMath::Clamp(FMath::Max(MotionBatchRangeStart, MotionBatchRangeEnd), 0, FrameCount - 1);
		for (int32 i = First; i <= Last; i++) TargetFrames.Add(i);
	}

	if (TargetFrames.Num() == 0) return;

	BeginTransaction(LOCTEXT("BatchSetRootMotion", "Batch Set Root Motion"));
	for (int32 Idx : TargetFrames)
	{
		if (Data->MotionData.RootMotion.IsValidIndex(Idx))
		{
			Data->MotionData.RootMotion[Idx].Position = SourcePos;
		}
	}
	EndTransaction();

	if (FrameStripBox.IsValid()) FrameStripBox->Invalidate(EInvalidateWidgetReason::Paint);
	if (MotionCanvas.IsValid()) MotionCanvas->Invalidate(EInvalidateWidgetReason::Paint);
	if (PropertiesBox.IsValid()) PropertiesBox->Invalidate(EInvalidateWidgetReason::Paint);
	OnRootMotionDataModified.ExecuteIfBound();
}

void SRootMotionEditor::ResetCurrentFrame()
{
	FFlipbookProfileEntry* Data = GetSelectedFlipbookData();
	if (!Data) return;

	if (!Data->MotionData.RootMotion.IsValidIndex(SelectedFrameIndex)) return;
	if (Data->MotionData.RootMotion[SelectedFrameIndex].Position.IsNearlyZero()) return;

	BeginTransaction(LOCTEXT("ResetRootMotionFrame", "Reset Root Motion Frame"));
	Data->MotionData.RootMotion[SelectedFrameIndex].Position = FVector2D::ZeroVector;
	EndTransaction();

	if (FrameStripBox.IsValid()) FrameStripBox->Invalidate(EInvalidateWidgetReason::Paint);
	if (PropertiesBox.IsValid()) PropertiesBox->Invalidate(EInvalidateWidgetReason::Paint);
	if (MotionCanvas.IsValid()) MotionCanvas->Invalidate(EInvalidateWidgetReason::Paint);
}

FVector2D SRootMotionEditor::GetCurrentFramePosition() const
{
	FFlipbookProfileEntry* Data = GetSelectedFlipbookData();
	if (!Data || !Data->MotionData.RootMotion.IsValidIndex(SelectedFrameIndex)) return FVector2D::ZeroVector;
	return Data->MotionData.RootMotion[SelectedFrameIndex].Position;
}

// ==========================================
// PLAYBACK
// ==========================================

void SRootMotionEditor::TogglePlayback()
{
	if (bIsPlaying)
	{
		StopPlayback();
	}
	else
	{
		UPaperFlipbook* FB = GetSelectedFlipbook();
		if (!FB || FB->GetNumKeyFrames() == 0) return;

		bIsPlaying = true;

		CachedTiming = FFlipbookTimingData::ReadFromFlipbook(FB);
		PlaybackTime = CachedTiming.GetFrameStartTime(SelectedFrameIndex);

		PlaybackTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateSP(this, &SRootMotionEditor::OnPlaybackTick), 1.0f / 60.0f);
	}
}

void SRootMotionEditor::StopPlayback()
{
	if (bIsPlaying)
	{
		bIsPlaying = false;
		if (PlaybackTickerHandle.IsValid())
		{
			FTSTicker::GetCoreTicker().RemoveTicker(PlaybackTickerHandle);
			PlaybackTickerHandle.Reset();
		}
	}
}

bool SRootMotionEditor::OnPlaybackTick(float DeltaTime)
{
	if (!bIsPlaying) return false;

	const float ActualDelta = FApp::GetDeltaTime();
	PlaybackTime += ActualDelta;

	// Loop
	if (CachedTiming.TotalDurationSeconds > 0.0f && PlaybackTime >= CachedTiming.TotalDurationSeconds)
	{
		PlaybackTime = FMath::Fmod(PlaybackTime, CachedTiming.TotalDurationSeconds);
	}

	int32 NewFrame = GetFrameFromTime();
	if (NewFrame != SelectedFrameIndex)
	{
		SelectedFrameIndex = NewFrame;
		if (FrameStripBox.IsValid()) FrameStripBox->Invalidate(EInvalidateWidgetReason::Paint);
		if (PropertiesBox.IsValid()) PropertiesBox->Invalidate(EInvalidateWidgetReason::Paint);
	}

	Invalidate(EInvalidateWidgetReason::Paint);
	return true;
}

int32 SRootMotionEditor::GetFrameFromTime() const
{
	if (CachedTiming.TotalDurationSeconds <= 0.0f) return 0;
	return RootMotionEditorInternal::TimeToFrameIndex(CachedTiming, PlaybackTime);
}

// ==========================================
// KEYBOARD
// ==========================================

FReply SRootMotionEditor::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	// Guard: don't handle when text input is focused
	TSharedPtr<SWidget> Focused = FSlateApplication::Get().GetKeyboardFocusedWidget();
	if (Focused.IsValid())
	{
		FName Type = Focused->GetType();
		if (Type == TEXT("SEditableText") || Type == TEXT("SMultiLineEditableText"))
		{
			return FReply::Unhandled();
		}
	}

	const FKey Key = InKeyEvent.GetKey();

	// Guard Ctrl for shortcuts (Ctrl+S save, etc.)
	if (InKeyEvent.IsControlDown())
	{
		return FReply::Unhandled();
	}

	// Space: toggle playback
	if (Key == EKeys::SpaceBar)
	{
		TogglePlayback();
		return FReply::Handled();
	}

	// O: toggle backward onion skin
	if (Key == EKeys::O)
	{
		bShowOnionSkin = !bShowOnionSkin;
		if (MotionCanvas.IsValid()) MotionCanvas->Invalidate(EInvalidateWidgetReason::Paint);
		return FReply::Handled();
	}

	// F: toggle forward onion skin
	if (Key == EKeys::F)
	{
		bShowForwardOnionSkin = !bShowForwardOnionSkin;
		if (MotionCanvas.IsValid()) MotionCanvas->Invalidate(EInvalidateWidgetReason::Paint);
		return FReply::Handled();
	}

	// Left/Right: frame navigation
	if (Key == EKeys::Left && GetFrameCount() > 0)
	{
		int32 NewFrame = FMath::Max(0, SelectedFrameIndex - 1);
		OnFrameClicked(NewFrame);
		return FReply::Handled();
	}
	if (Key == EKeys::Right && GetFrameCount() > 0)
	{
		int32 NewFrame = FMath::Min(GetFrameCount() - 1, SelectedFrameIndex + 1);
		OnFrameClicked(NewFrame);
		return FReply::Handled();
	}

	// WASD nudge for pixel-precise positioning
	FVector2D NudgeDelta = FVector2D::ZeroVector;
	const float NudgeAmount = InKeyEvent.IsShiftDown() ? 16.0f : 1.0f;

	if (Key == EKeys::A) NudgeDelta.X = -NudgeAmount;
	else if (Key == EKeys::D) NudgeDelta.X = NudgeAmount;
	else if (Key == EKeys::W) NudgeDelta.Y = -NudgeAmount;
	else if (Key == EKeys::S) NudgeDelta.Y = NudgeAmount;

	if (!NudgeDelta.IsNearlyZero())
	{
		FFlipbookProfileEntry* Data = GetSelectedFlipbookData();
		if (!Data) return FReply::Handled();

		EnsureRootMotionArraySized();

		// Debounced transaction: begin if no active transaction
		if (!ActiveTransaction.IsValid())
		{
			BeginTransaction(LOCTEXT("NudgeRootMotion", "Nudge Root Motion"));
		}

		if (Data->MotionData.RootMotion.IsValidIndex(SelectedFrameIndex))
		{
			Data->MotionData.RootMotion[SelectedFrameIndex].Position += NudgeDelta;
		}

		// Debounce: reset timer
		if (TSharedPtr<FActiveTimerHandle> OldTimer = NudgeDebounceTimer.Pin())
		{
			UnRegisterActiveTimer(OldTimer.ToSharedRef());
		}
		NudgeDebounceTimer = RegisterActiveTimer(0.5f, FWidgetActiveTimerDelegate::CreateLambda(
			[this](double, float) -> EActiveTimerReturnType
			{
				CommitNudgeTransaction();
				return EActiveTimerReturnType::Stop;
			}));

		// Invalidate instead of rebuilding — spinbox values auto-update via Value_Lambda,
		// frame strip selection/motion dots auto-update via lambda bindings
		if (MotionCanvas.IsValid()) MotionCanvas->Invalidate(EInvalidateWidgetReason::Paint);
		if (FrameStripBox.IsValid()) FrameStripBox->Invalidate(EInvalidateWidgetReason::Paint);
		if (PropertiesBox.IsValid()) PropertiesBox->Invalidate(EInvalidateWidgetReason::Paint);
		return FReply::Handled();
	}

	return FReply::Unhandled();
}

void SRootMotionEditor::CommitNudgeTransaction()
{
	EndTransaction();
	// Invalidate frame strip (motion dots update via lambda) instead of full rebuild
	if (FrameStripBox.IsValid()) FrameStripBox->Invalidate(EInvalidateWidgetReason::Paint);
	OnRootMotionDataModified.ExecuteIfBound();
}

// ==========================================
// EXTERNAL CONTROL
// ==========================================

void SRootMotionEditor::SetSelectedFlipbook(int32 FlipbookIndex)
{
	if (SelectedFlipbookIndex == FlipbookIndex) return;
	StopPlayback();
	SelectedFlipbookIndex = FlipbookIndex;
	SelectedFrameIndex = 0;

	// Auto-detect lerp range from first/last non-zero frames
	LerpStartFrame = 0;
	LerpEndFrame = 0;
	FFlipbookProfileEntry* Data = GetSelectedFlipbookData();
	if (Data && Data->MotionData.HasRootMotion())
	{
		int32 First = INDEX_NONE, Last = INDEX_NONE;
		for (int32 i = 0; i < Data->MotionData.RootMotion.Num(); i++)
		{
			if (!Data->MotionData.RootMotion[i].Position.IsNearlyZero())
			{
				if (First == INDEX_NONE) First = i;
				Last = i;
			}
		}
		if (First != INDEX_NONE) { LerpStartFrame = First; LerpEndFrame = Last; }
		else { LerpEndFrame = FMath::Max(0, Data->MotionData.RootMotion.Num() - 1); }
	}

	OnFlipbookSelectedInList.ExecuteIfBound(FlipbookIndex);
	RefreshAll();
}

void SRootMotionEditor::RefreshAll()
{
	bNeedsRefresh = false;
	RefreshFlipbookList();
	RefreshFrameStrip();
	RefreshPropertiesPanel();
	if (MotionCanvas.IsValid()) MotionCanvas->Invalidate(EInvalidateWidgetReason::Paint);
}

// ==========================================
// HELPERS
// ==========================================

FFlipbookProfileEntry* SRootMotionEditor::GetSelectedFlipbookData() const
{
	if (!Asset.IsValid() || !Asset->Flipbooks.IsValidIndex(SelectedFlipbookIndex)) return nullptr;
	return &Asset->Flipbooks[SelectedFlipbookIndex];
}

UPaperFlipbook* SRootMotionEditor::GetSelectedFlipbook() const
{
	FFlipbookProfileEntry* Data = GetSelectedFlipbookData();
	if (!Data || Data->Identity.Flipbook.IsNull()) return nullptr;
	return Data->Identity.Flipbook.LoadSynchronous();
}

int32 SRootMotionEditor::GetFrameCount() const
{
	UPaperFlipbook* FB = GetSelectedFlipbook();
	return FB ? FB->GetNumKeyFrames() : 0;
}

// ==========================================
// SRootMotionCanvas — IMPLEMENTATION
// ==========================================

void SRootMotionCanvas::Construct(const FArguments& InArgs)
{
	Asset = InArgs._Asset;
	SelectedFlipbookIndex = InArgs._SelectedFlipbookIndex;
	SelectedFrameIndex = InArgs._SelectedFrameIndex;
	IsPlaying = InArgs._IsPlaying;
	PlaybackTime = InArgs._PlaybackTime;
	ShowOnionSkin = InArgs._ShowOnionSkin;
	ShowForwardOnionSkin = InArgs._ShowForwardOnionSkin;
	OnionSkinFrames = InArgs._OnionSkinFrames;
	OnionSkinOpacity = InArgs._OnionSkinOpacity;

	SetClipping(EWidgetClipping::ClipToBounds);
}

FVector2D SRootMotionCanvas::ComputeDesiredSize(float LayoutScaleMultiplier) const
{
	return FVector2D(400.f, 300.f);
}

// ==========================================
// COORDINATE CONVERSION
// ==========================================

float SRootMotionCanvas::GetEffectiveZoom(const FGeometry& Geom) const
{
	// Compute zoom based on motion path bounds + sprite size
	FVector2D LargestDims = GetLargestSpriteDims();
	FVector2D WidgetSize = Geom.GetLocalSize();

	// Get motion path bounds to include in zoom calculation
	FVector2D ContentSize = LargestDims;

	if (Asset.IsValid())
	{
		int32 FBIndex = SelectedFlipbookIndex.Get(INDEX_NONE);
		if (Asset->Flipbooks.IsValidIndex(FBIndex))
		{
			const FFlipbookProfileEntry& Anim = Asset->Flipbooks[FBIndex];
			if (Anim.MotionData.HasRootMotion())
			{
				FVector2D PathBounds = GetMotionPathBounds(Anim.MotionData.RootMotion);
				// Add sprite padding to path bounds
				ContentSize.X = FMath::Max(ContentSize.X, PathBounds.X + LargestDims.X);
				ContentSize.Y = FMath::Max(ContentSize.Y, PathBounds.Y + LargestDims.Y);
			}
		}
	}

	if (ContentSize.X <= 0 || ContentSize.Y <= 0) return UserZoom;

	float BaseScale = FMath::Min(WidgetSize.X / ContentSize.X, WidgetSize.Y / ContentSize.Y) * 0.8f;
	return BaseScale * UserZoom;
}

FVector2D SRootMotionCanvas::GetCanvasOrigin(const FGeometry& Geom) const
{
	return Geom.GetLocalSize() * 0.5f + PanOffset;
}

FVector2D SRootMotionCanvas::ScreenToCanvas(const FGeometry& Geom, const FVector2D& ScreenPos) const
{
	float Zoom = GetEffectiveZoom(Geom);
	if (Zoom <= 0.f) return FVector2D::ZeroVector;
	return (ScreenPos - GetCanvasOrigin(Geom)) / Zoom;
}

FVector2D SRootMotionCanvas::CanvasToScreen(const FGeometry& Geom, const FVector2D& CanvasPos) const
{
	return CanvasPos * GetEffectiveZoom(Geom) + GetCanvasOrigin(Geom);
}

int32 SRootMotionCanvas::SnapToGrid(int32 Value) const
{
	return FMath::RoundToInt((float)Value / GridSize) * GridSize;
}

// ==========================================
// ON PAINT
// ==========================================

int32 SRootMotionCanvas::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId,
	const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	// Checkerboard background
	FEditorCanvasUtils::DrawCheckerboard(OutDrawElements, LayerId, AllottedGeometry, 16.0f);
	LayerId++;

	if (!Asset.IsValid()) return LayerId;
	int32 FBIndex = SelectedFlipbookIndex.Get(INDEX_NONE);
	if (!Asset->Flipbooks.IsValidIndex(FBIndex)) return LayerId;

	const FFlipbookProfileEntry& Anim = Asset->Flipbooks[FBIndex];
	UPaperFlipbook* FB = Anim.Identity.Flipbook.IsNull() ? nullptr : Anim.Identity.Flipbook.LoadSynchronous();
	if (!FB || FB->GetNumKeyFrames() == 0) return LayerId;

	int32 FrameIdx = SelectedFrameIndex.Get(0);
	FrameIdx = FMath::Clamp(FrameIdx, 0, FB->GetNumKeyFrames() - 1);

	// Draw grid
	DrawGrid(AllottedGeometry, OutDrawElements, LayerId);
	LayerId++;

	// Draw ground line
	DrawGroundLine(AllottedGeometry, OutDrawElements, LayerId);
	LayerId++;

	// Draw motion path
	if (Anim.MotionData.HasRootMotion())
	{
		DrawMotionPath(AllottedGeometry, OutDrawElements, LayerId, Anim.MotionData.RootMotion, FrameIdx);
		LayerId++;
	}

	// Draw onion skins (behind current sprite)
	if (ShowOnionSkin.Get())
	{
		DrawOnionSkin(AllottedGeometry, OutDrawElements, LayerId, FB, Anim.MotionData.RootMotion, FrameIdx);
		LayerId++;
	}

	// Draw forward onion skin
	if (ShowForwardOnionSkin.Get())
	{
		DrawForwardOnionSkin(AllottedGeometry, OutDrawElements, LayerId, FB, Anim.MotionData.RootMotion, FrameIdx);
		LayerId++;
	}

	// Draw sprite at current frame's root motion offset
	FVector2D Offset = FVector2D::ZeroVector;
	if (Anim.MotionData.RootMotion.IsValidIndex(FrameIdx))
	{
		Offset = Anim.MotionData.RootMotion[FrameIdx].Position;
	}
	DrawSprite(AllottedGeometry, OutDrawElements, LayerId, FB, FrameIdx, Offset);
	LayerId++;

	// Draw origin marker (small crosshair at 0,0)
	{
		const FSlateBrush* WhiteBrush = FAppStyle::GetBrush("WhiteBrush");
		FVector2D OriginScreen = CanvasToScreen(AllottedGeometry, FVector2D::ZeroVector);
		FLinearColor OriginColor(0.8f, 0.8f, 0.2f, 0.6f);
		float CrossSize = 8.0f;

		// Horizontal
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId,
			MakePaintGeometry(AllottedGeometry, FVector2D(CrossSize * 2, 1), FSlateLayoutTransform(FVector2D(OriginScreen.X - CrossSize, OriginScreen.Y))),
			WhiteBrush, ESlateDrawEffect::None, OriginColor);
		// Vertical
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId,
			MakePaintGeometry(AllottedGeometry, FVector2D(1, CrossSize * 2), FSlateLayoutTransform(FVector2D(OriginScreen.X, OriginScreen.Y - CrossSize))),
			WhiteBrush, ESlateDrawEffect::None, OriginColor);
		LayerId++;
	}

	return LayerId;
}

// ==========================================
// DRAW HELPERS
// ==========================================

void SRootMotionCanvas::DrawGroundLine(const FGeometry& Geom, FSlateWindowElementList& OutDrawElements, int32 LayerId) const
{
	const FSlateBrush* WhiteBrush = FAppStyle::GetBrush("WhiteBrush");
	FVector2D Origin = GetCanvasOrigin(Geom);
	FVector2D WidgetSize = Geom.GetLocalSize();

	// Horizontal ground line at Y=0 (canvas origin)
	FSlateDrawElement::MakeBox(OutDrawElements, LayerId,
		MakePaintGeometry(Geom, FVector2D(WidgetSize.X, 1), FSlateLayoutTransform(FVector2D(0, Origin.Y))),
		WhiteBrush, ESlateDrawEffect::None, FLinearColor(0.3f, 0.5f, 0.3f, 0.4f));
}

void SRootMotionCanvas::DrawGrid(const FGeometry& Geom, FSlateWindowElementList& OutDrawElements, int32 LayerId) const
{
	const FSlateBrush* WhiteBrush = FAppStyle::GetBrush("WhiteBrush");
	float Zoom = GetEffectiveZoom(Geom);
	if (Zoom <= 0.01f) return;

	FVector2D Origin = GetCanvasOrigin(Geom);
	FVector2D WidgetSize = Geom.GetLocalSize();
	float GridScreenSize = GridSize * Zoom;

	// Skip grid if too dense
	if (GridScreenSize < 4.0f) return;

	FLinearColor GridColor(0.15f, 0.15f, 0.15f, 0.3f);

	// Vertical lines
	float StartX = FMath::Fmod(Origin.X, GridScreenSize);
	if (StartX < 0) StartX += GridScreenSize;
	for (float X = StartX; X < WidgetSize.X; X += GridScreenSize)
	{
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId,
			MakePaintGeometry(Geom, FVector2D(1, WidgetSize.Y), FSlateLayoutTransform(FVector2D(X, 0))),
			WhiteBrush, ESlateDrawEffect::None, GridColor);
	}

	// Horizontal lines
	float StartY = FMath::Fmod(Origin.Y, GridScreenSize);
	if (StartY < 0) StartY += GridScreenSize;
	for (float Y = StartY; Y < WidgetSize.Y; Y += GridScreenSize)
	{
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId,
			MakePaintGeometry(Geom, FVector2D(WidgetSize.X, 1), FSlateLayoutTransform(FVector2D(0, Y))),
			WhiteBrush, ESlateDrawEffect::None, GridColor);
	}
}

void SRootMotionCanvas::DrawMotionPath(const FGeometry& Geom, FSlateWindowElementList& OutDrawElements, int32 LayerId,
	const TArray<FRootMotionFrameData>& RootMotion, int32 CurrentFrame) const
{
	if (RootMotion.Num() < 2) return;

	const FSlateBrush* WhiteBrush = FAppStyle::GetBrush("WhiteBrush");

	// Build polyline
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 4
	TArray<FVector2D> PathPoints;
#else
	TArray<FVector2f> PathPoints;
#endif
	PathPoints.Reserve(RootMotion.Num());
	for (int32 i = 0; i < RootMotion.Num(); i++)
	{
		FVector2D ScreenPos = CanvasToScreen(Geom, RootMotion[i].Position);
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 4
		PathPoints.Add(FVector2D(ScreenPos.X, ScreenPos.Y));
#else
		PathPoints.Add(FVector2f(ScreenPos.X, ScreenPos.Y));
#endif
	}

	// Draw the path line
	FLinearColor PathColor(0.4f, 0.7f, 1.0f, 0.6f);
	FSlateDrawElement::MakeLines(OutDrawElements, LayerId,
		Geom.ToPaintGeometry(), PathPoints, ESlateDrawEffect::None, PathColor, true, 2.0f);

	// Draw dots at each frame position with frame index labels
	for (int32 i = 0; i < RootMotion.Num(); i++)
	{
		FVector2D ScreenPos = CanvasToScreen(Geom, RootMotion[i].Position);
		bool bIsCurrent = (i == CurrentFrame);
		float DotSize = bIsCurrent ? 10.0f : 6.0f;
		FLinearColor DotColor = bIsCurrent
			? FLinearColor(0.2f, 0.9f, 0.3f, 1.0f)  // Green for current
			: FLinearColor(0.5f, 0.5f, 0.5f, 0.7f);  // Gray for others

		FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 1,
			MakePaintGeometry(Geom, FVector2D(DotSize, DotSize),
				FSlateLayoutTransform(FVector2D(ScreenPos.X - DotSize * 0.5f, ScreenPos.Y - DotSize * 0.5f))),
			WhiteBrush, ESlateDrawEffect::None, DotColor);

		// Frame index label (above the dot)
		FSlateFontInfo LabelFont = FCoreStyle::GetDefaultFontStyle("Regular", 7);
		FSlateDrawElement::MakeText(OutDrawElements, LayerId + 2,
			Geom.ToPaintGeometry(FSlateLayoutTransform(FVector2D(ScreenPos.X - 4, ScreenPos.Y - DotSize - 12))),
			FString::FromInt(i),
			LabelFont,
			ESlateDrawEffect::None,
			bIsCurrent ? FLinearColor(0.2f, 0.9f, 0.3f, 1.0f) : FLinearColor(0.6f, 0.6f, 0.6f, 0.6f));
	}
}

void SRootMotionCanvas::DrawSprite(const FGeometry& Geom, FSlateWindowElementList& OutDrawElements, int32 LayerId,
	UPaperFlipbook* Flipbook, int32 FrameIndex, const FVector2D& Offset, const FLinearColor& Tint) const
{
	if (!Flipbook || Flipbook->GetNumKeyFrames() == 0) return;
	FrameIndex = FMath::Clamp(FrameIndex, 0, Flipbook->GetNumKeyFrames() - 1);

	const FPaperFlipbookKeyFrame& KeyFrame = Flipbook->GetKeyFrameChecked(FrameIndex);
	UPaperSprite* Sprite = KeyFrame.Sprite;
	if (!Sprite) return;

	UTexture2D* Texture = Sprite->GetBakedTexture();
	if (!Texture) return;

	const FVector2D SpriteSourcePos = FVector2D(Sprite->GetSourceUV());
	const FVector2D SpriteSourceSize = FVector2D(Sprite->GetSourceSize());

	float Zoom = GetEffectiveZoom(Geom);
	FVector2D DrawSize = SpriteSourceSize * Zoom;
	FVector2D DrawPos = CanvasToScreen(Geom, Offset) - DrawSize * 0.5f;

	// Apply per-frame sprite offset from extraction info + pivot shift (prevents sprite bouncing)
	FVector2D SpriteShift = FVector2D::ZeroVector;
	if (Asset.IsValid())
	{
		int32 FBIndex = SelectedFlipbookIndex.Get(INDEX_NONE);
		if (Asset->Flipbooks.IsValidIndex(FBIndex))
		{
			const FFlipbookProfileEntry& Anim = Asset->Flipbooks[FBIndex];
			if (Anim.CombatData.FrameExtractionInfo.IsValidIndex(FrameIndex))
			{
				SpriteShift = FVector2D(Anim.CombatData.FrameExtractionInfo[FrameIndex].SpriteOffset);
			}
		}
	}

	// Pivot shift: how much the sprite's custom pivot offsets it from center
	FVector2D SourceCenter = Sprite->GetSourceUV() + Sprite->GetSourceSize() * 0.5f;
	FVector2D PivotPos = Sprite->GetPivotPosition();
	FVector2D PivotShift = FVector2D(SourceCenter) - FVector2D(PivotPos);

	DrawPos.X += (SpriteShift.X + PivotShift.X) * Zoom;
	DrawPos.Y += (SpriteShift.Y + PivotShift.Y) * Zoom;

	FSlateBrush SpriteBrush;
	SpriteBrush.SetResourceObject(Texture);
	SpriteBrush.ImageSize = FVector2D(Texture->GetSizeX(), Texture->GetSizeY());
	SpriteBrush.SetUVRegion(FBox2D(
		SpriteSourcePos / SpriteBrush.ImageSize,
		(SpriteSourcePos + SpriteSourceSize) / SpriteBrush.ImageSize));

	FSlateDrawElement::MakeBox(OutDrawElements, LayerId,
		MakePaintGeometry(Geom, DrawSize, FSlateLayoutTransform(DrawPos)),
		&SpriteBrush, ESlateDrawEffect::None, Tint);
}

void SRootMotionCanvas::DrawOnionSkin(const FGeometry& Geom, FSlateWindowElementList& OutDrawElements, int32 LayerId,
	UPaperFlipbook* Flipbook, const TArray<FRootMotionFrameData>& RootMotion, int32 CurrentFrame) const
{
	int32 NumOnionFrames = OnionSkinFrames.Get();
	float BaseOpacity = OnionSkinOpacity.Get();

	int32 FramesDrawn = 0;

	// Draw backward onion skins (blue tint)
	for (int32 i = 1; i <= NumOnionFrames; i++)
	{
		int32 PrevFrame = CurrentFrame - i;
		if (PrevFrame < 0) break;

		// Use root motion position if available, otherwise draw at origin
		FVector2D Offset = FVector2D::ZeroVector;
		if (RootMotion.IsValidIndex(PrevFrame))
		{
			Offset = RootMotion[PrevFrame].Position;
		}

		float Opacity = BaseOpacity * (1.0f - (float)(FramesDrawn) / (float)NumOnionFrames);
		FLinearColor OnionColor(0.5f, 0.5f, 1.0f, Opacity);

		DrawSprite(Geom, OutDrawElements, LayerId, Flipbook, PrevFrame, Offset, OnionColor);
		FramesDrawn++;
	}
}

void SRootMotionCanvas::DrawForwardOnionSkin(const FGeometry& Geom, FSlateWindowElementList& OutDrawElements, int32 LayerId,
	UPaperFlipbook* Flipbook, const TArray<FRootMotionFrameData>& RootMotion, int32 CurrentFrame) const
{
	int32 NumOnionFrames = OnionSkinFrames.Get();
	float BaseOpacity = OnionSkinOpacity.Get();
	int32 TotalFrames = Flipbook->GetNumKeyFrames();

	int32 FramesDrawn = 0;

	// Draw forward onion skins (green tint)
	for (int32 i = 1; i <= NumOnionFrames; i++)
	{
		int32 NextFrame = CurrentFrame + i;
		if (NextFrame >= TotalFrames) break;

		// Use root motion position if available, otherwise draw at origin
		FVector2D Offset = FVector2D::ZeroVector;
		if (RootMotion.IsValidIndex(NextFrame))
		{
			Offset = RootMotion[NextFrame].Position;
		}

		float Opacity = BaseOpacity * (1.0f - (float)(FramesDrawn) / (float)NumOnionFrames);
		FLinearColor OnionColor(0.5f, 1.0f, 0.5f, Opacity);

		DrawSprite(Geom, OutDrawElements, LayerId, Flipbook, NextFrame, Offset, OnionColor);
		FramesDrawn++;
	}
}

// ==========================================
// MOUSE INPUT
// ==========================================

FReply SRootMotionCanvas::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		DragMode = EDragMode::Panning;
		PanStartPos = MouseEvent.GetScreenSpacePosition();
		PanStartOffset = PanOffset;
		return FReply::Handled().CaptureMouse(SharedThis(this));
	}

	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		if (!Asset.IsValid()) return FReply::Handled();
		int32 FBIndex = SelectedFlipbookIndex.Get(INDEX_NONE);
		if (!Asset->Flipbooks.IsValidIndex(FBIndex)) return FReply::Handled();

		// Enter pending drag — actual drag starts after dead zone
		DragMode = EDragMode::PendingDrag;
		DragStartScreenPos = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
		DragStartCanvasPos = ScreenToCanvas(MyGeometry, DragStartScreenPos);

		const FFlipbookProfileEntry& Anim = Asset->Flipbooks[FBIndex];
		int32 FrameIdx = SelectedFrameIndex.Get(0);
		DragStartPosition = (Anim.MotionData.RootMotion.IsValidIndex(FrameIdx))
			? Anim.MotionData.RootMotion[FrameIdx].Position : FVector2D::ZeroVector;

		return FReply::Handled().CaptureMouse(SharedThis(this));
	}

	return FReply::Unhandled();
}

FReply SRootMotionCanvas::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (DragMode == EDragMode::Panning && MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		DragMode = EDragMode::None;
		return FReply::Handled().ReleaseMouseCapture();
	}

	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		if (DragMode == EDragMode::PendingDrag)
		{
			// Click without drag — just focus the widget
			DragMode = EDragMode::None;
			return FReply::Handled().ReleaseMouseCapture();
		}
		if (DragMode == EDragMode::MovingSprite)
		{
			DragMode = EDragMode::None;
			OnDragEnded.ExecuteIfBound();
			return FReply::Handled().ReleaseMouseCapture();
		}
	}

	return FReply::Unhandled();
}

FReply SRootMotionCanvas::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	FVector2D LocalPos = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());

	if (DragMode == EDragMode::Panning)
	{
		FVector2D Delta = MouseEvent.GetScreenSpacePosition() - PanStartPos;
		PanOffset = PanStartOffset + Delta;
		Invalidate(EInvalidateWidgetReason::Paint);
		return FReply::Handled();
	}

	if (DragMode == EDragMode::PendingDrag)
	{
		// Check dead zone before starting actual drag
		FVector2D Delta = LocalPos - DragStartScreenPos;
		if (Delta.Size() >= DragDeadZone)
		{
			DragMode = EDragMode::MovingSprite;
			OnDragStarted.ExecuteIfBound();
		}
		return FReply::Handled();
	}

	if (DragMode == EDragMode::MovingSprite)
	{
		FVector2D CurrentCanvasPos = ScreenToCanvas(MyGeometry, LocalPos);
		FVector2D CanvasDelta = CurrentCanvasPos - DragStartCanvasPos;
		FVector2D NewPos = DragStartPosition + CanvasDelta;

		// Grid snap
		NewPos.X = SnapToGrid(FMath::RoundToInt(NewPos.X));
		NewPos.Y = SnapToGrid(FMath::RoundToInt(NewPos.Y));

		OnPositionChanged.ExecuteIfBound(NewPos);
		Invalidate(EInvalidateWidgetReason::Paint);
		return FReply::Handled();
	}

	return FReply::Unhandled();
}

FReply SRootMotionCanvas::OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	float Delta = MouseEvent.GetWheelDelta() * 0.1f;
	UserZoom = FMath::Clamp(UserZoom + Delta, 0.1f, 5.0f);
	Invalidate(EInvalidateWidgetReason::Paint);
	return FReply::Handled();
}

void SRootMotionCanvas::OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent)
{
	if (DragMode == EDragMode::MovingSprite)
	{
		DragMode = EDragMode::None;
		OnDragEnded.ExecuteIfBound();
	}
	else
	{
		DragMode = EDragMode::None;
	}
}

FCursorReply SRootMotionCanvas::OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const
{
	if (DragMode == EDragMode::Panning) return FCursorReply::Cursor(EMouseCursor::GrabHandClosed);
	if (DragMode == EDragMode::MovingSprite) return FCursorReply::Cursor(EMouseCursor::CardinalCross);
	return FCursorReply::Unhandled();
}

// ==========================================
// HELPERS
// ==========================================

FVector2D SRootMotionCanvas::GetLargestSpriteDims() const
{
	if (!Asset.IsValid()) return CachedLargestDims;
	int32 FBIndex = SelectedFlipbookIndex.Get(INDEX_NONE);
	if (!Asset->Flipbooks.IsValidIndex(FBIndex)) return CachedLargestDims;

	const FFlipbookProfileEntry& Anim = Asset->Flipbooks[FBIndex];
	UPaperFlipbook* FB = Anim.Identity.Flipbook.IsNull() ? nullptr : Anim.Identity.Flipbook.LoadSynchronous();
	if (!FB) return CachedLargestDims;

	// Cache check
	if (FBIndex == CachedLargestDimsFlipbookIndex && CachedLargestDimsFlipbook.IsValid() && CachedLargestDimsFlipbook.Get() == FB)
	{
		return CachedLargestDims;
	}

	FVector2D Largest(128, 128);
	for (int32 i = 0; i < FB->GetNumKeyFrames(); i++)
	{
		const FPaperFlipbookKeyFrame& KeyFrame = FB->GetKeyFrameChecked(i);
		if (KeyFrame.Sprite)
		{
			FVector2D Size = FVector2D(KeyFrame.Sprite->GetSourceSize());
			Largest.X = FMath::Max(Largest.X, Size.X);
			Largest.Y = FMath::Max(Largest.Y, Size.Y);
		}
	}

	CachedLargestDims = Largest;
	CachedLargestDimsFlipbookIndex = FBIndex;
	CachedLargestDimsFlipbook = FB;
	return Largest;
}

FVector2D SRootMotionCanvas::GetMotionPathBounds(const TArray<FRootMotionFrameData>& RootMotion) const
{
	if (RootMotion.Num() == 0) return FVector2D::ZeroVector;

	FVector2D Min(TNumericLimits<double>::Max());
	FVector2D Max(TNumericLimits<double>::Lowest());

	for (const FRootMotionFrameData& Frame : RootMotion)
	{
		Min.X = FMath::Min(Min.X, Frame.Position.X);
		Min.Y = FMath::Min(Min.Y, Frame.Position.Y);
		Max.X = FMath::Max(Max.X, Frame.Position.X);
		Max.Y = FMath::Max(Max.Y, Frame.Position.Y);
	}

	return Max - Min;
}

#undef LOCTEXT_NAMESPACE
