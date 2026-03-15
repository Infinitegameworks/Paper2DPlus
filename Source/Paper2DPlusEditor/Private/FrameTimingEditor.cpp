// Copyright 2026 Infinite Gameworks. All Rights Reserved.

// FrameTimingEditor.cpp - Main frame timing editor tab and frame duration list

#include "FrameTimingEditor.h"
#include "AnimationTimeline.h"
#include "PaperFlipbook.h"
#include "PaperSprite.h"
#include "Widgets/SLeafWidget.h"
#include "ScopedTransaction.h"
#include "Editor.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/SToolTip.h"
#include "EditorCanvasUtils.h"
#include "CharacterProfileAssetEditor.h"

#define LOCTEXT_NAMESPACE "FrameTimingEditor"

// ==========================================
// SFramePreviewCanvas — draws sprite with offset + pivot shift
// ==========================================

class SFramePreviewCanvas : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SFramePreviewCanvas) {}
		SLATE_ATTRIBUTE(UPaperFlipbook*, Flipbook)
		SLATE_ATTRIBUTE(int32, FrameIndex)
		SLATE_ATTRIBUTE(float, Zoom)
		SLATE_ARGUMENT(TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset>, Asset)
		SLATE_ATTRIBUTE(int32, FlipbookIndex)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		Flipbook = InArgs._Flipbook;
		FrameIndex = InArgs._FrameIndex;
		Zoom = InArgs._Zoom;
		Asset = InArgs._Asset;
		FlipbookIndex = InArgs._FlipbookIndex;
		SetClipping(EWidgetClipping::ClipToBounds);
	}

	virtual FVector2D ComputeDesiredSize(float) const override
	{
		FIntPoint Dims = GetLargestSpriteDims();
		float Z = Zoom.Get(3.0f);
		return FVector2D(Dims.X * Z, Dims.Y * Z);
	}

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override
	{
		const FVector2D GeomSize = AllottedGeometry.GetLocalSize();

		// Checkerboard background (includes its own dark fill)
		FEditorCanvasUtils::DrawCheckerboard(OutDrawElements, LayerId, AllottedGeometry);
		LayerId++;

		// Draw sprite
		UPaperFlipbook* FB = Flipbook.Get(nullptr);
		int32 Frame = FrameIndex.Get(0);
		if (!FB || Frame < 0 || Frame >= FB->GetNumKeyFrames())
		{
			return LayerId;
		}

		UPaperSprite* Sprite = FB->GetKeyFrameChecked(Frame).Sprite;
		if (!Sprite) return LayerId;

		UTexture2D* Texture = Sprite->GetBakedTexture();
		if (!Texture) Texture = Cast<UTexture2D>(Sprite->GetSourceTexture());
		if (!Texture) return LayerId;

		float Z = Zoom.Get(3.0f);
		FVector2D Center = GeomSize * 0.5f;

		// Get sprite dimensions
		FVector2D SpriteDims = Sprite->GetSourceSize();
		FVector2D DrawSize(SpriteDims.X * Z, SpriteDims.Y * Z);

		// Get offset from extraction info
		FIntPoint Offset = GetOffsetAtFrame(Frame);

		// Get pivot shift (same logic as SSpriteAlignmentCanvas)
		FVector2D PivotShift = GetPivotShift(Sprite);

		// Position: centered, then shifted by offset + pivot
		FVector2D DrawPos = Center - DrawSize * 0.5f;
		DrawPos.X += (Offset.X + PivotShift.X) * Z;
		DrawPos.Y += (Offset.Y + PivotShift.Y) * Z;

		// Build brush with UV sub-region
		FSlateBrush SpriteBrush;
		SpriteBrush.SetResourceObject(Texture);
		SpriteBrush.ImageSize = FVector2D(Texture->GetSizeX(), Texture->GetSizeY());
		SpriteBrush.DrawAs = ESlateBrushDrawType::Image;
		SpriteBrush.Tiling = ESlateBrushTileType::NoTile;

		FVector2D SourceUV = Sprite->GetSourceUV();
		FVector2D SourceSize = Sprite->GetSourceSize();
		FVector2D TexSize(Texture->GetSizeX(), Texture->GetSizeY());
		if (TexSize.X > 0 && TexSize.Y > 0)
		{
			SpriteBrush.SetUVRegion(FBox2D(
				FVector2D(SourceUV.X / TexSize.X, SourceUV.Y / TexSize.Y),
				FVector2D((SourceUV.X + SourceSize.X) / TexSize.X, (SourceUV.Y + SourceSize.Y) / TexSize.Y)));
		}

		FSlateDrawElement::MakeBox(OutDrawElements, LayerId,
			AllottedGeometry.ToPaintGeometry(
				FVector2f(DrawSize),
				FSlateLayoutTransform(FVector2D(DrawPos))),
			&SpriteBrush, ESlateDrawEffect::None,
			FLinearColor::White);

		return LayerId;
	}

private:
	TAttribute<UPaperFlipbook*> Flipbook;
	TAttribute<int32> FrameIndex;
	TAttribute<float> Zoom;
	TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset> Asset;
	TAttribute<int32> FlipbookIndex;

	FIntPoint GetOffsetAtFrame(int32 Frame) const
	{
		if (!Asset.IsValid()) return FIntPoint::ZeroValue;
		int32 FBIdx = FlipbookIndex.Get(0);
		if (!Asset->Flipbooks.IsValidIndex(FBIdx)) return FIntPoint::ZeroValue;
		const FFlipbookHitboxData& Anim = Asset->Flipbooks[FBIdx];
		if (!Anim.FrameExtractionInfo.IsValidIndex(Frame)) return FIntPoint::ZeroValue;
		return Anim.FrameExtractionInfo[Frame].SpriteOffset;
	}

	FVector2D GetPivotShift(UPaperSprite* Sprite) const
	{
		if (!Sprite) return FVector2D::ZeroVector;
		FVector2D SourceCenter = Sprite->GetSourceUV() + Sprite->GetSourceSize() * 0.5f;
		FVector2D PivotPos = Sprite->GetPivotPosition();
		return SourceCenter - PivotPos;
	}

	FIntPoint GetLargestSpriteDims() const
	{
		UPaperFlipbook* FB = Flipbook.Get(nullptr);
		if (!FB) return FIntPoint(128, 128);

		FIntPoint Largest(1, 1);
		for (int32 i = 0; i < FB->GetNumKeyFrames(); ++i)
		{
			if (UPaperSprite* S = FB->GetKeyFrameChecked(i).Sprite)
			{
				FVector2D Sz = S->GetSourceSize();
				Largest.X = FMath::Max(Largest.X, FMath::RoundToInt(Sz.X));
				Largest.Y = FMath::Max(Largest.Y, FMath::RoundToInt(Sz.Y));
			}
		}
		return Largest;
	}
};

// ==========================================
// SFrameDurationList Implementation
// ==========================================

void SFrameDurationList::Construct(const FArguments& InArgs)
{
	Flipbook = InArgs._Flipbook;
	SelectedFrameIndex = InArgs._SelectedFrameIndex;
	DisplayUnit = InArgs._DisplayUnit;
	FPS = InArgs._FPS;
	SelectedFrames = InArgs._SelectedFrames;

	ChildSlot
	[
		SNew(SVerticalBox)

		// Header
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4, 2)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.FillWidth(0.15f)
			.Padding(2, 0)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("FrameCol", "Frame"))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
			]

			+ SHorizontalBox::Slot()
			.FillWidth(0.45f)
			.Padding(2, 0)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("DurationCol", "Duration"))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
			]

			+ SHorizontalBox::Slot()
			.FillWidth(0.4f)
			.Padding(2, 0)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("TimeCol", "Time"))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
			]
		]

		// Frame rows
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			[
				SAssignNew(FrameListBox, SVerticalBox)
			]
		]
	];

	Refresh();
}

void SFrameDurationList::SetFlipbook(UPaperFlipbook* InFlipbook)
{
	Flipbook = InFlipbook;
	Refresh();
}

void SFrameDurationList::Refresh()
{
	if (!FrameListBox.IsValid()) return;

	FrameListBox->ClearChildren();

	UPaperFlipbook* FB = Flipbook.Get();
	if (!FB) return;

	FFlipbookTimingData Timing = FFlipbookTimingData::ReadFromFlipbook(FB);

	for (int32 i = 0; i < Timing.FrameDurations.Num(); i++)
	{
		BuildFrameRow(i, Timing.FrameDurations[i]);
	}
}

void SFrameDurationList::BuildFrameRow(int32 FrameIndex, int32 CurrentDuration)
{
	int32 CapturedIndex = FrameIndex;

	FrameListBox->AddSlot()
	.AutoHeight()
	.Padding(2, 1)
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.BorderBackgroundColor_Lambda([this, CapturedIndex]() {
			const bool bIsActive = (SelectedFrameIndex.Get(-1) == CapturedIndex);
			const bool bIsMultiSelected = SelectedFrames && SelectedFrames->Contains(CapturedIndex);
			if (bIsActive) return FLinearColor(0.20f, 0.60f, 0.30f, 1.0f);
			if (bIsMultiSelected) return FLinearColor(0.15f, 0.45f, 0.75f, 1.0f);
			return FLinearColor(0.03f, 0.03f, 0.03f, 1.0f);
		})
		.Padding(4, 2)
		.OnMouseButtonDown_Lambda([this, CapturedIndex](const FGeometry&, const FPointerEvent& MouseEvent) -> FReply
		{
			if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
			{
				if (SelectedFrames)
				{
					UPaperFlipbook* FB = Flipbook.Get();
					int32 FrameCount = FB ? FB->GetNumKeyFrames() : 0;
					FrameSelectionUtils::HandleFrameClick(*SelectedFrames, FrameSelectionAnchorIndex,
						CapturedIndex, MouseEvent, FrameCount);
				}
				OnFrameSelected.ExecuteIfBound(CapturedIndex);
				return FReply::Handled();
			}
			return FReply::Unhandled();
		})
		[
			SNew(SHorizontalBox)

			// Frame number
			+ SHorizontalBox::Slot()
			.FillWidth(0.15f)
			.VAlign(VAlign_Center)
			.Padding(2, 0)
			[
				SNew(STextBlock)
				.Text(FText::Format(LOCTEXT("FrameNum", "#{0}"), FText::AsNumber(CapturedIndex + 1)))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
				.ColorAndOpacity_Lambda([this, CapturedIndex]() {
					const bool bIsActive = (SelectedFrameIndex.Get(-1) == CapturedIndex);
					const bool bIsMultiSelected = SelectedFrames && SelectedFrames->Contains(CapturedIndex);
					return (bIsActive || bIsMultiSelected)
						? FSlateColor(FLinearColor::White)
						: FSlateColor(FLinearColor(0.7f, 0.7f, 0.7f));
				})
			]

			// Duration spinbox
			+ SHorizontalBox::Slot()
			.FillWidth(0.45f)
			.VAlign(VAlign_Center)
			.Padding(2, 0)
			[
				SNew(SBox)
				.WidthOverride(80)
				[
					SNew(SSpinBox<int32>)
					.MinValue(1)
					.MaxValue(999)
					.Value_Lambda([this, CapturedIndex]() -> int32 {
						UPaperFlipbook* FB = Flipbook.Get();
						if (!FB || CapturedIndex >= FB->GetNumKeyFrames()) return 1;
						return FMath::Max(FB->GetKeyFrameChecked(CapturedIndex).FrameRun, 1);
					})
					.OnValueChanged_Lambda([this, CapturedIndex](int32 NewValue) {
						OnFrameDurationChanged.ExecuteIfBound(CapturedIndex, NewValue);
					})
					.ToolTipText_Lambda([this, CapturedIndex]() {
						UPaperFlipbook* FB = Flipbook.Get();
						if (!FB) return FText::GetEmpty();
						float CurrentFPS = FPS.Get(12.0f);
						int32 Dur = (CapturedIndex < FB->GetNumKeyFrames()) ? FB->GetKeyFrameChecked(CapturedIndex).FrameRun : 1;
						float Ms = (CurrentFPS > 0.0f) ? (Dur / CurrentFPS) * 1000.0f : 0.0f;
						return FText::Format(LOCTEXT("DurTooltip", "{0} frame(s) = {1}ms at {2} FPS"),
							FText::AsNumber(Dur), FText::AsNumber(FMath::RoundToInt(Ms)), FText::AsNumber(FMath::RoundToInt(CurrentFPS)));
					})
				]
			]

			// Time display
			+ SHorizontalBox::Slot()
			.FillWidth(0.4f)
			.VAlign(VAlign_Center)
			.Padding(2, 0)
			[
				SNew(STextBlock)
				.Text_Lambda([this, CapturedIndex]() {
					UPaperFlipbook* FB = Flipbook.Get();
					if (!FB || CapturedIndex >= FB->GetNumKeyFrames()) return FText::GetEmpty();
					float CurrentFPS = FPS.Get(12.0f);
					if (CurrentFPS <= 0.0f) return FText::GetEmpty();
					int32 Dur = FB->GetKeyFrameChecked(CapturedIndex).FrameRun;
					ETimingDisplayUnit Unit = DisplayUnit.Get(ETimingDisplayUnit::Frames);
					if (Unit == ETimingDisplayUnit::Milliseconds)
					{
						float Ms = (Dur / CurrentFPS) * 1000.0f;
						return FText::Format(LOCTEXT("TimeMs", "{0}ms"), FText::AsNumber(FMath::RoundToInt(Ms)));
					}
					else
					{
						float Secs = Dur / CurrentFPS;
						return FText::Format(LOCTEXT("TimeSec", "{0}s"), FText::FromString(FString::Printf(TEXT("%.3f"), Secs)));
					}
				})
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
				.ColorAndOpacity(FLinearColor(0.6f, 0.6f, 0.6f))
			]
		]
	];
}

// ==========================================
// SFrameTimingEditor Implementation
// ==========================================

SFrameTimingEditor::~SFrameTimingEditor()
{
	StopPlayback();
	if (GEditor)
	{
		GEditor->UnregisterForUndo(this);
	}
}

void SFrameTimingEditor::Construct(const FArguments& InArgs)
{
	Asset = InArgs._Asset;
	CollapsedFlipbookGroups = InArgs._CollapsedFlipbookGroups;
	ParentSelectedFrames = InArgs._SelectedFrames;

	// Determine initial FPS from first available flipbook
	if (Asset.IsValid() && Asset->Flipbooks.Num() > 0)
	{
		UPaperFlipbook* FB = GetCurrentFlipbook();
		if (FB)
		{
			PlaybackFPS = FB->GetFramesPerSecond();
		}
	}

	ChildSlot
	[
		SNew(SVerticalBox)

		// Toolbar
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			BuildToolbar()
		]

		// Main content area
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		.Padding(4)
		[
			SNew(SVerticalBox)

			// Top area: Flipbook List | Preview (centered, larger) | Frame Duration List
			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			.Padding(0, 0, 0, 4)
			[
				SNew(SSplitter)
				.Orientation(Orient_Horizontal)

				// Left: Flipbook list
				+ SSplitter::Slot()
				.Value(0.2f)
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
					.Padding(4)
					[
						BuildFlipbookList()
					]
				]

				// Center: Preview (prominent)
				+ SSplitter::Slot()
				.Value(0.4f)
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
					.Padding(4)
					[
						BuildPreviewPanel()
					]
				]

				// Right: Frame duration list + batch tools
				+ SSplitter::Slot()
				.Value(0.4f)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.FillHeight(1.0f)
					[
						SNew(SBorder)
						.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
						.Padding(4)
						[
							SAssignNew(FrameDurationListWidget, SFrameDurationList)
							.Flipbook(GetCurrentFlipbook())
							.SelectedFrameIndex_Lambda([this]() { return SelectedFrameIndex; })
							.DisplayUnit_Lambda([this]() { return DisplayUnit; })
							.FPS_Lambda([this]() { return PlaybackFPS; })
							.SelectedFrames(ParentSelectedFrames)
						]
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0, 4, 0, 0)
					[
						SNew(SBorder)
						.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
						.Padding(6)
						[
							BuildBatchToolsPanel()
						]
					]
				]
			]

			// Timeline (bottom area)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
				.Padding(4)
				[
					SAssignNew(TimelineWidget, SAnimationTimeline)
					.Flipbook(GetCurrentFlipbook())
					.SelectedFrameIndex_Lambda([this]() { return SelectedFrameIndex; })
					.PlaybackPosition_Lambda([this]() { return PlaybackPosition; })
					.IsPlaying_Lambda([this]() { return bIsPlaying; })
					.DisplayUnit_Lambda([this]() { return DisplayUnit; })
				]
			]
		]
	];

	// Register for undo/redo notifications
	if (GEditor)
	{
		GEditor->RegisterForUndo(this);
	}

	// Wire up delegates
	if (TimelineWidget.IsValid())
	{
		TimelineWidget->OnFrameSelected.BindSP(this, &SFrameTimingEditor::OnFrameSelected);
		TimelineWidget->OnFrameDurationChanged.BindSP(this, &SFrameTimingEditor::OnFrameDurationChanged);
	}

	if (FrameDurationListWidget.IsValid())
	{
		FrameDurationListWidget->OnFrameSelected.BindSP(this, &SFrameTimingEditor::OnFrameSelected);
		FrameDurationListWidget->OnFrameDurationChanged.BindSP(this, &SFrameTimingEditor::OnFrameDurationChanged);
	}

	RefreshFlipbookList();
}

TSharedRef<SWidget> SFrameTimingEditor::BuildToolbar()
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(FMargin(8, 4))
		[
			SNew(SWrapBox)
			.UseAllottedSize(true)

			// === Flipbook Name ===
			+ SWrapBox::Slot()
			.Padding(0, 0, 8, 0)
			[
				SNew(STextBlock)
				.Text_Lambda([this]() {
					const FFlipbookHitboxData* Anim = GetCurrentFlipbookData();
					return Anim ? FText::FromString(Anim->FlipbookName) : LOCTEXT("NoFlipbook", "No Flipbook");
				})
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
			]

			// Separator
			+ SWrapBox::Slot()
			.Padding(8, 0)
			[
				SNew(SSeparator)
				.Orientation(Orient_Vertical)
			]

			// === FPS Control ===
			+ SWrapBox::Slot()
			.Padding(0, 0, 4, 0)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("FPSLabel", "FPS:"))
			]

			+ SWrapBox::Slot()
			.Padding(0, 0, 8, 0)
			[
				SNew(SBox)
				.WidthOverride(60)
				[
					SNew(SSpinBox<float>)
					.MinValue(1.0f)
					.MaxValue(120.0f)
					.Delta(1.0f)
					.Value_Lambda([this]() { return PlaybackFPS; })
					.OnValueChanged_Lambda([this](float NewValue) { OnFPSChanged(NewValue); })
					.ToolTipText(LOCTEXT("FPSTooltip", "Flipbook frames per second. Changes the FPS of the flipbook directly."))
				]
			]

			// Separator
			+ SWrapBox::Slot()
			.Padding(8, 0)
			[
				SNew(SSeparator)
				.Orientation(Orient_Vertical)
			]

			// === Playback Controls ===
			+ SWrapBox::Slot()
			.Padding(0, 0, 2, 0)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
				.ToolTipText(LOCTEXT("PlayPause", "Play/Pause (Space)"))
				.OnClicked_Lambda([this]() {
					TogglePlayback();
					return FReply::Handled();
				})
				[
					SNew(STextBlock)
					.Text_Lambda([this]() { return bIsPlaying ? LOCTEXT("Pause", "Pause") : LOCTEXT("Play", "Play"); })
				]
			]

			+ SWrapBox::Slot()
			.Padding(0, 0, 2, 0)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
				.ToolTipText(LOCTEXT("StopTooltip", "Stop and reset to beginning"))
				.OnClicked_Lambda([this]() {
					StopPlayback();
					PlaybackPosition = 0.0f;
					SelectedFrameIndex = 0;
					return FReply::Handled();
				})
				[
					SNew(STextBlock)
					.Text(LOCTEXT("Stop", "Stop"))
				]
			]

			// Separator
			+ SWrapBox::Slot()
			.Padding(8, 0)
			[
				SNew(SSeparator)
				.Orientation(Orient_Vertical)
			]

			// === Unit Toggle ===
			+ SWrapBox::Slot()
			.Padding(0, 0, 4, 0)
			[
				SNew(SCheckBox)
				.Style(FAppStyle::Get(), "ToggleButtonCheckbox")
				.IsChecked_Lambda([this]() { return DisplayUnit == ETimingDisplayUnit::Frames ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this](ECheckBoxState) { OnDisplayUnitChanged(ETimingDisplayUnit::Frames); })
				.Padding(FMargin(6, 2))
				[
					SNew(STextBlock)
					.Text(LOCTEXT("Frames", "Frames"))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
				]
			]

			+ SWrapBox::Slot()
			.Padding(0, 0, 8, 0)
			[
				SNew(SCheckBox)
				.Style(FAppStyle::Get(), "ToggleButtonCheckbox")
				.IsChecked_Lambda([this]() { return DisplayUnit == ETimingDisplayUnit::Milliseconds ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this](ECheckBoxState) { OnDisplayUnitChanged(ETimingDisplayUnit::Milliseconds); })
				.Padding(FMargin(6, 2))
				[
					SNew(STextBlock)
					.Text(LOCTEXT("Milliseconds", "ms"))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
				]
			]

			// Separator
			+ SWrapBox::Slot()
			.Padding(8, 0)
			[
				SNew(SSeparator)
				.Orientation(Orient_Vertical)
			]

			// === Stats ===
			+ SWrapBox::Slot()
			.Padding(8, 0, 0, 0)
			[
				SAssignNew(StatsText, STextBlock)
				.Text_Lambda([this]() {
					UPaperFlipbook* FB = GetCurrentFlipbook();
					if (!FB) return LOCTEXT("NoFlipbook", "No flipbook selected");
					FFlipbookTimingData Timing = FFlipbookTimingData::ReadFromFlipbook(FB);
					return FText::Format(LOCTEXT("Stats", "Total: {0}s | {1} frames | {2} ticks"),
						FText::FromString(FString::Printf(TEXT("%.2f"), Timing.TotalDurationSeconds)),
						FText::AsNumber(Timing.TotalFrames),
						FText::AsNumber(FMath::RoundToInt(Timing.TotalDurationSeconds * Timing.FPS)));
				})
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
				.ColorAndOpacity(FLinearColor(0.6f, 0.6f, 0.6f))
			]
		];
}

TSharedRef<SWidget> SFrameTimingEditor::BuildFlipbookList()
{
	return SNew(SVerticalBox)

		// Title
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0, 0, 0, 4)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("Flipbooks", "Flipbooks"))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
		]

		// Flipbook list
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			[
				SAssignNew(FlipbookListBox, SVerticalBox)
			]
		];
}

TSharedRef<SWidget> SFrameTimingEditor::BuildPreviewPanel()
{
	return SNew(SVerticalBox)

		// Title
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0, 0, 0, 4)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("Preview", "Flipbook Preview"))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
		]

		// Sprite preview canvas (draws with offset + pivot shift)
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Fill)
		.Padding(4)
		[
			SAssignNew(PreviewCanvas, SFramePreviewCanvas)
			.Flipbook_Lambda([this]() { return GetCurrentFlipbook(); })
			.FrameIndex_Lambda([this]() { return SelectedFrameIndex; })
			.Zoom_Lambda([this]() { return PreviewZoom; })
			.Asset(Asset)
			.FlipbookIndex_Lambda([this]() { return SelectedFlipbookIndex; })
		]

		// Additional preview info
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0, 2, 0, 0)
		[
			SAssignNew(PreviewBox, SVerticalBox)
		]

		// Frame info
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0, 4, 0, 0)
		[
			SNew(STextBlock)
			.Text_Lambda([this]() {
				UPaperFlipbook* FB = GetCurrentFlipbook();
				if (!FB || SelectedFrameIndex >= FB->GetNumKeyFrames())
				{
					return LOCTEXT("NoFrame", "No frame selected");
				}
				const FPaperFlipbookKeyFrame& KF = FB->GetKeyFrameChecked(SelectedFrameIndex);
				int32 Duration = FMath::Max(KF.FrameRun, 1);
				float Ms = (PlaybackFPS > 0.0f) ? (Duration / PlaybackFPS) * 1000.0f : 0.0f;
				return FText::Format(LOCTEXT("FrameInfo", "Frame {0} of {1} | Duration: {2}f ({3}ms @ {4} FPS)"),
					FText::AsNumber(SelectedFrameIndex + 1),
					FText::AsNumber(FB->GetNumKeyFrames()),
					FText::AsNumber(Duration),
					FText::AsNumber(FMath::RoundToInt(Ms)),
					FText::AsNumber(FMath::RoundToInt(PlaybackFPS)));
			})
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
			.ColorAndOpacity(FLinearColor(0.5f, 0.5f, 0.5f))
			.AutoWrapText(true)
		];
}

// ==========================================
// Refresh Functions
// ==========================================

void SFrameTimingEditor::RefreshAll()
{
	RefreshFlipbookList();
	RefreshFrameList();
	RefreshPreview();
}

void SFrameTimingEditor::RefreshFlipbookList()
{
	if (!FlipbookListBox.IsValid() || !Asset.IsValid()) return;

	FlipbookListBox->ClearChildren();

	// Build sorted index list for alphabetical ordering
	TArray<int32> SortedIndices;
	SortedIndices.SetNum(Asset->Flipbooks.Num());
	for (int32 j = 0; j < SortedIndices.Num(); j++) { SortedIndices[j] = j; }
	SortedIndices.Sort([this](int32 A, int32 B)
	{
		return Asset->Flipbooks[A].FlipbookName.Compare(Asset->Flipbooks[B].FlipbookName, ESearchCase::IgnoreCase) < 0;
	});

	// Item builder lambda
	auto BuildItem = [this](int32 CapturedIdx) -> TSharedRef<SWidget>
	{
		const FFlipbookHitboxData& Anim = Asset->Flipbooks[CapturedIdx];
		UPaperFlipbook* LoadedFlipbook = !Anim.Flipbook.IsNull() ? Anim.Flipbook.LoadSynchronous() : nullptr;
		const bool bHasFlipbook = LoadedFlipbook != nullptr;
		const FText SourceNameText = FText::FromString(bHasFlipbook ? Anim.Flipbook.GetAssetName() : TEXT("No Flipbook Assigned"));

		return SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "NoBorder")
			.ToolTip(
				SNew(SToolTip)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0, 0, 0, 6)
					[
						SNew(STextBlock)
						.Text(FText::FromString(Anim.FlipbookName))
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SBox)
						.WidthOverride(128)
						.HeightOverride(128)
						[
							bHasFlipbook
								? StaticCastSharedRef<SWidget>(SNew(SFlipbookThumbnail).Flipbook(LoadedFlipbook))
								: StaticCastSharedRef<SWidget>(
									SNew(SBorder)
									.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
									.HAlign(HAlign_Center)
									.VAlign(VAlign_Center)
									[
										SNew(STextBlock)
										.Text(LOCTEXT("NoFBPreviewTooltip", "No FB"))
										.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
										.ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f)))
									])
						]
					]
				])
			.OnClicked_Lambda([this, CapturedIdx]() {
				OnFlipbookSelected(CapturedIdx);
				return FReply::Handled();
			})
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
				.BorderBackgroundColor_Lambda([this, CapturedIdx]() {
					return (SelectedFlipbookIndex == CapturedIdx)
						? FLinearColor(0.15f, 0.35f, 0.55f, 1.0f)
						: FLinearColor(0.03f, 0.03f, 0.03f, 1.0f);
				})
				.Padding(8, 6)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0, 0, 8, 0)
					[
						SNew(SBox)
						.WidthOverride(44)
						.HeightOverride(44)
						[
							bHasFlipbook
								? StaticCastSharedRef<SWidget>(SNew(SFlipbookThumbnail).Flipbook(LoadedFlipbook))
								: StaticCastSharedRef<SWidget>(
									SNew(SBorder)
									.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
									.HAlign(HAlign_Center)
									.VAlign(VAlign_Center)
									[
										SNew(STextBlock)
										.Text(LOCTEXT("NoFBPreviewList", "No FB"))
										.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
										.ColorAndOpacity(FSlateColor(FLinearColor(0.55f, 0.55f, 0.55f)))
									])
						]
					]
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.VAlign(VAlign_Center)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(STextBlock)
							.Text(FText::FromString(Anim.FlipbookName))
							.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
							.ColorAndOpacity(bHasFlipbook ? FLinearColor::White : FLinearColor(0.5f, 0.5f, 0.5f))
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0, 2, 0, 0)
						[
							SNew(STextBlock)
							.Text_Lambda([this, CapturedIdx, SourceNameText]() -> FText {
								if (!Asset.IsValid() || !Asset->Flipbooks.IsValidIndex(CapturedIdx)) return FText::GetEmpty();
								UPaperFlipbook* FB = Asset->Flipbooks[CapturedIdx].Flipbook.LoadSynchronous();
								if (!FB) return SourceNameText;
								return FText::Format(LOCTEXT("FlipbookInfoWithSource", "{0} | {1} frames | {2} FPS"),
									SourceNameText,
									FText::AsNumber(FB->GetNumKeyFrames()),
									FText::AsNumber(FMath::RoundToInt(FB->GetFramesPerSecond())));
							})
							.Font(FCoreStyle::GetDefaultFontStyle("Regular", 7))
							.ColorAndOpacity(bHasFlipbook ? FLinearColor(0.4f, 0.8f, 0.4f) : FLinearColor(0.6f, 0.4f, 0.4f))
						]
					]
				]
			];
	};

	// Partition sorted indices by group
	TMap<FName, TArray<int32>> FlipbooksByGroup;
	for (int32 i : SortedIndices)
	{
		FlipbooksByGroup.FindOrAdd(Asset->Flipbooks[i].FlipbookGroup).Add(i);
	}

	// Collect group names: ungrouped first, then named groups alphabetically
	TArray<FName> GroupOrder;
	if (FlipbooksByGroup.Contains(NAME_None))
	{
		GroupOrder.Add(NAME_None);
	}
	TArray<FName> NamedGroups;
	for (const auto& Pair : FlipbooksByGroup)
	{
		if (Pair.Key != NAME_None) NamedGroups.Add(Pair.Key);
	}
	NamedGroups.Sort([](const FName& A, const FName& B) { return A.Compare(B) < 0; });
	GroupOrder.Append(NamedGroups);

	// If no groups exist (all ungrouped), render flat list
	if (GroupOrder.Num() <= 1 && GroupOrder.Contains(NAME_None))
	{
		for (int32 Idx : FlipbooksByGroup[NAME_None])
		{
			FlipbookListBox->AddSlot().AutoHeight().Padding(0, 1)[BuildItem(Idx)];
		}
		return;
	}

	// Render grouped list with collapsible headers
	for (FName GroupName : GroupOrder)
	{
		const TArray<int32>& GroupIndices = FlipbooksByGroup[GroupName];
		bool bCollapsed = CollapsedFlipbookGroups && CollapsedFlipbookGroups->Contains(GroupName);
		FString DisplayName = GroupName.IsNone() ? TEXT("Ungrouped") : GroupName.ToString();

		// Group header
		FlipbookListBox->AddSlot()
		.AutoHeight()
		.Padding(0, 4, 0, 0)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "NoBorder")
			.OnClicked_Lambda([this, GroupName]()
			{
				if (CollapsedFlipbookGroups)
				{
					if (CollapsedFlipbookGroups->Contains(GroupName))
					{
						CollapsedFlipbookGroups->Remove(GroupName);
					}
					else
					{
						CollapsedFlipbookGroups->Add(GroupName);
					}
					RefreshFlipbookList();
				}
				return FReply::Handled();
			})
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0, 0, 4, 0)
				[
					SNew(STextBlock)
					.Text(FText::FromString(bCollapsed ? TEXT("\x25B6") : TEXT("\x25BC")))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 7))
					.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)))
				]

				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(FText::Format(LOCTEXT("GroupHeaderFmt", "{0} ({1})"),
						FText::FromString(DisplayName), FText::AsNumber(GroupIndices.Num())))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
					.ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f)))
				]
			]
		];

		// Group items (if not collapsed)
		if (!bCollapsed)
		{
			for (int32 Idx : GroupIndices)
			{
				FlipbookListBox->AddSlot()
				.AutoHeight()
				.Padding(8, 1, 0, 0)
				[
					BuildItem(Idx)
				];
			}
		}
	}
}

void SFrameTimingEditor::RefreshFrameList()
{
	UPaperFlipbook* FB = GetCurrentFlipbook();

	if (TimelineWidget.IsValid())
	{
		TimelineWidget->SetFlipbook(FB);
	}
	if (FrameDurationListWidget.IsValid())
	{
		FrameDurationListWidget->SetFlipbook(FB);
	}

	RefreshPreview();
}

void SFrameTimingEditor::RefreshPreview()
{
	if (!PreviewBox.IsValid()) return;

	PreviewBox->ClearChildren();

	UPaperFlipbook* FB = GetCurrentFlipbook();
	if (!FB || SelectedFrameIndex >= FB->GetNumKeyFrames()) return;

	const FPaperFlipbookKeyFrame& KF = FB->GetKeyFrameChecked(SelectedFrameIndex);

	// Duration color indicator (FPS-aware)
	int32 Duration = FMath::Max(KF.FrameRun, 1);
	float CurrentFPS = PlaybackFPS;
	FLinearColor DurColor = SAnimationTimeline::GetFrameColor(Duration, CurrentFPS);
	float HoldMs = (CurrentFPS > 0.0f) ? ((Duration - 1) * 1000.0f / CurrentFPS) : 0.0f;

	PreviewBox->AddSlot()
	.AutoHeight()
	.Padding(4)
	[
		SNew(SHorizontalBox)

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(0, 0, 8, 0)
		[
			SNew(SBox)
			.WidthOverride(12)
			.HeightOverride(12)
			[
				SNew(SImage)
				.Image(FAppStyle::GetBrush("WhiteBrush"))
				.ColorAndOpacity(DurColor)
			]
		]

		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text_Lambda([Duration, CurrentFPS]() {
				int32 TotalMs = (CurrentFPS > 0.0f) ? FMath::RoundToInt(Duration * 1000.0f / CurrentFPS) : 0;
				if (Duration == 1) return FText::Format(LOCTEXT("StandardFmt", "Standard ({0}ms)"), TotalMs);
				float HoldMs = (CurrentFPS > 0.0f) ? ((Duration - 1) * 1000.0f / CurrentFPS) : 0.0f;
				if (HoldMs <= 100.0f) return FText::Format(LOCTEXT("SlightHoldFmt", "Slight Hold ({0} frames, {1}ms)"), Duration, TotalMs);
				if (HoldMs <= 250.0f) return FText::Format(LOCTEXT("MediumHoldFmt", "Medium Hold ({0} frames, {1}ms)"), Duration, TotalMs);
				return FText::Format(LOCTEXT("LongHoldFmt", "Long Hold ({0} frames, {1}ms)"), Duration, TotalMs);
			})
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
		]
	];

	// Sprite name
	if (KF.Sprite)
	{
		PreviewBox->AddSlot()
		.AutoHeight()
		.Padding(4, 2)
		[
			SNew(STextBlock)
			.Text(FText::FromString(KF.Sprite->GetName()))
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
			.ColorAndOpacity(FLinearColor(0.5f, 0.5f, 0.5f))
		];
	}
}

// ==========================================
// Event Handlers
// ==========================================

void SFrameTimingEditor::OnFlipbookSelected(int32 Index)
{
	if (!Asset.IsValid() || !Asset->Flipbooks.IsValidIndex(Index)) return;

	StopPlayback();
	SelectedFlipbookIndex = Index;
	SelectedFrameIndex = 0;
	PlaybackPosition = 0.0f;

	// Update FPS from new flipbook
	UPaperFlipbook* FB = GetCurrentFlipbook();
	if (FB)
	{
		PlaybackFPS = FB->GetFramesPerSecond();
	}

	RefreshFrameList();
	RefreshFlipbookList();
}

void SFrameTimingEditor::OnFrameSelected(int32 Index)
{
	int32 FrameCount = GetCurrentFrameCount();
	if (Index < 0 || Index >= FrameCount) return;

	SelectedFrameIndex = Index;
	RefreshPreview();
}

void SFrameTimingEditor::OnFrameDurationChanged(int32 FrameIndex, int32 NewDuration)
{
	UPaperFlipbook* FB = GetCurrentFlipbook();
	if (!FB || FrameIndex < 0 || FrameIndex >= FB->GetNumKeyFrames()) return;

	NewDuration = FMath::Clamp(NewDuration, 1, 999);

	// Check if actually changed
	if (FB->GetKeyFrameChecked(FrameIndex).FrameRun == NewDuration) return;

	BeginTransaction(LOCTEXT("SetFrameDuration", "Set Frame Duration"));
	FB->Modify();

	{
		FScopedFlipbookMutator Mutator(FB);
		if (Mutator.KeyFrames.IsValidIndex(FrameIndex))
		{
			Mutator.KeyFrames[FrameIndex].FrameRun = NewDuration;
		}
	}

	FB->MarkPackageDirty();
	EndTransaction();

	// Refresh widgets
	if (TimelineWidget.IsValid())
	{
		TimelineWidget->RefreshTimingData();
	}
	if (FrameDurationListWidget.IsValid())
	{
		FrameDurationListWidget->Refresh();
	}
	RefreshPreview();

	// Return focus to the editor so Ctrl+Z reaches our OnKeyDown instead of
	// being consumed by the spinbox's internal text undo
	FSlateApplication::Get().SetKeyboardFocus(SharedThis(this));

	OnTimingDataModified.ExecuteIfBound();
}

void SFrameTimingEditor::OnFPSChanged(float NewFPS)
{
	UPaperFlipbook* FB = GetCurrentFlipbook();
	if (!FB) return;

	NewFPS = FMath::Clamp(NewFPS, 1.0f, 120.0f);
	PlaybackFPS = NewFPS;

	BeginTransaction(LOCTEXT("SetFlipbookFPS", "Set Flipbook FPS"));
	FB->Modify();

	{
		FScopedFlipbookMutator Mutator(FB);
		Mutator.FramesPerSecond = NewFPS;
	}

	FB->MarkPackageDirty();
	EndTransaction();

	// Restart playback if playing (new FPS = different tick interval)
	if (bIsPlaying)
	{
		StopPlayback();
		StartPlayback();
	}

	if (TimelineWidget.IsValid())
	{
		TimelineWidget->RefreshTimingData();
	}
	if (FrameDurationListWidget.IsValid())
	{
		FrameDurationListWidget->Refresh();
	}
	RefreshPreview();

	OnTimingDataModified.ExecuteIfBound();
}

void SFrameTimingEditor::OnDisplayUnitChanged(ETimingDisplayUnit NewUnit)
{
	DisplayUnit = NewUnit;
}

void SFrameTimingEditor::SetSelectedFlipbook(int32 FlipbookIndex)
{
	OnFlipbookSelected(FlipbookIndex);
}

// ==========================================
// Batch Operations
// ==========================================

void SFrameTimingEditor::OnSetAllDurations(int32 Duration)
{
	UPaperFlipbook* FB = GetCurrentFlipbook();
	if (!FB || FB->GetNumKeyFrames() == 0) return;

	Duration = FMath::Clamp(Duration, 1, 999);

	BeginTransaction(LOCTEXT("SetAllDurations", "Set All Frame Durations"));
	FB->Modify();

	{
		FScopedFlipbookMutator Mutator(FB);
		for (FPaperFlipbookKeyFrame& KF : Mutator.KeyFrames)
		{
			KF.FrameRun = Duration;
		}
	}

	FB->MarkPackageDirty();
	EndTransaction();

	if (TimelineWidget.IsValid()) TimelineWidget->RefreshTimingData();
	if (FrameDurationListWidget.IsValid()) FrameDurationListWidget->Refresh();
	RefreshPreview();
	OnTimingDataModified.ExecuteIfBound();
}

void SFrameTimingEditor::OnResetAllToOne()
{
	OnSetAllDurations(1);
}

void SFrameTimingEditor::OnApplySelectedDurationToAll()
{
	UPaperFlipbook* FB = GetCurrentFlipbook();
	if (!FB || SelectedFrameIndex < 0 || SelectedFrameIndex >= FB->GetNumKeyFrames())
	{
		return;
	}

	const int32 SelectedDuration = FMath::Clamp(FB->GetKeyFrameChecked(SelectedFrameIndex).FrameRun, 1, 999);
	OnSetAllDurations(SelectedDuration);
}

void SFrameTimingEditor::OnApplySelectedDurationToRemaining()
{
	UPaperFlipbook* FB = GetCurrentFlipbook();
	if (!FB || SelectedFrameIndex < 0 || SelectedFrameIndex >= FB->GetNumKeyFrames())
	{
		return;
	}

	const int32 SelectedDuration = FMath::Clamp(FB->GetKeyFrameChecked(SelectedFrameIndex).FrameRun, 1, 999);

	BeginTransaction(LOCTEXT("SetRemainingDurations", "Set Remaining Frame Durations"));
	FB->Modify();

	{
		FScopedFlipbookMutator Mutator(FB);
		for (int32 FrameIndex = SelectedFrameIndex; FrameIndex < Mutator.KeyFrames.Num(); ++FrameIndex)
		{
			Mutator.KeyFrames[FrameIndex].FrameRun = SelectedDuration;
		}
	}

	FB->MarkPackageDirty();
	EndTransaction();

	if (TimelineWidget.IsValid())
	{
		TimelineWidget->RefreshTimingData();
	}
	if (FrameDurationListWidget.IsValid())
	{
		FrameDurationListWidget->Refresh();
	}
	RefreshPreview();

	OnTimingDataModified.ExecuteIfBound();
}

void SFrameTimingEditor::OnApplySelectedDurationToSelectedFrames()
{
	UPaperFlipbook* FB = GetCurrentFlipbook();
	if (!FB || !ParentSelectedFrames || ParentSelectedFrames->Num() == 0) return;
	if (SelectedFrameIndex < 0 || SelectedFrameIndex >= FB->GetNumKeyFrames()) return;

	const int32 SelectedDuration = FMath::Clamp(FB->GetKeyFrameChecked(SelectedFrameIndex).FrameRun, 1, 999);

	BeginTransaction(LOCTEXT("SetSelectedFramesDurations", "Set Selected Frames Durations"));
	FB->Modify();

	{
		FScopedFlipbookMutator Mutator(FB);
		for (int32 Idx : *ParentSelectedFrames)
		{
			if (Idx >= 0 && Idx < Mutator.KeyFrames.Num())
			{
				Mutator.KeyFrames[Idx].FrameRun = SelectedDuration;
			}
		}
	}

	FB->MarkPackageDirty();
	EndTransaction();

	if (TimelineWidget.IsValid())
	{
		TimelineWidget->RefreshTimingData();
	}
	if (FrameDurationListWidget.IsValid())
	{
		FrameDurationListWidget->Refresh();
	}
	RefreshPreview();

	OnTimingDataModified.ExecuteIfBound();
}

TSharedRef<SWidget> SFrameTimingEditor::BuildBatchToolsPanel()
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0, 0, 0, 4)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("BatchToolsHeader", "Batch Duration Tools"))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0, 2)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0, 0, 2, 0)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
				.ToolTipText(LOCTEXT("ResetAllTooltip", "Reset all frame durations to 1"))
				.HAlign(HAlign_Center)
				.OnClicked_Lambda([this]() { OnResetAllToOne(); return FReply::Handled(); })
				[
					SNew(STextBlock)
					.Text(LOCTEXT("ResetAll", "Reset All to 1"))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
				]
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(2, 0, 0, 0)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
				.ToolTipText(LOCTEXT("DistributeTooltip", "Make all frames equal duration (average)"))
				.HAlign(HAlign_Center)
				.OnClicked_Lambda([this]() { OnDistributeEvenly(); return FReply::Handled(); })
				[
					SNew(STextBlock)
					.Text(LOCTEXT("Distribute", "Distribute Evenly"))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
				]
			]
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0, 2)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0, 0, 2, 0)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
				.ToolTipText(LOCTEXT("CopyDurationToAllTooltip", "Copy the current frame's duration to every frame"))
				.HAlign(HAlign_Center)
				.IsEnabled_Lambda([this]() {
					UPaperFlipbook* FB = GetCurrentFlipbook();
					return FB && SelectedFrameIndex >= 0 && SelectedFrameIndex < FB->GetNumKeyFrames();
				})
				.OnClicked_Lambda([this]() { OnApplySelectedDurationToAll(); return FReply::Handled(); })
				[
					SNew(STextBlock)
					.Text(LOCTEXT("CopyToAll", "Copy to All"))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
				]
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(2, 0, 0, 0)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
				.ToolTipText(LOCTEXT("CopyDurationToRemainingTooltip", "Copy the current frame's duration to all remaining frames"))
				.HAlign(HAlign_Center)
				.IsEnabled_Lambda([this]() {
					UPaperFlipbook* FB = GetCurrentFlipbook();
					return FB && SelectedFrameIndex >= 0 && SelectedFrameIndex < FB->GetNumKeyFrames();
				})
				.OnClicked_Lambda([this]() { OnApplySelectedDurationToRemaining(); return FReply::Handled(); })
				[
					SNew(STextBlock)
					.Text(LOCTEXT("CopyToRemaining", "Copy to Remaining"))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
				]
			]
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0, 2)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
			.ToolTipText(LOCTEXT("CopyDurationToSelectedTooltip", "Copy the current frame's duration to all selected frames (Ctrl/Shift+Click in frame list)"))
			.HAlign(HAlign_Center)
			.IsEnabled_Lambda([this]() {
				return ParentSelectedFrames && ParentSelectedFrames->Num() > 0;
			})
			.OnClicked_Lambda([this]() { OnApplySelectedDurationToSelectedFrames(); return FReply::Handled(); })
			[
				SNew(STextBlock)
				.Text(LOCTEXT("CopyToSelected", "Copy to Selected Frames"))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
			]
		];
}

void SFrameTimingEditor::OnDistributeEvenly()
{
	UPaperFlipbook* FB = GetCurrentFlipbook();
	if (!FB || FB->GetNumKeyFrames() == 0) return;

	// Calculate the average duration (round to nearest integer, minimum 1)
	FFlipbookTimingData Timing = FFlipbookTimingData::ReadFromFlipbook(FB);
	int32 TotalTicks = 0;
	for (int32 Dur : Timing.FrameDurations)
	{
		TotalTicks += Dur;
	}
	int32 AvgDuration = FMath::Max(1, FMath::RoundToInt((float)TotalTicks / Timing.TotalFrames));

	OnSetAllDurations(AvgDuration);
}

// ==========================================
// Playback
// ==========================================

void SFrameTimingEditor::StartPlayback()
{
	if (bIsPlaying) return;

	bIsPlaying = true;

	// Use a fine-grained tick (60fps) for smooth cursor movement
	PlaybackTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateSP(this, &SFrameTimingEditor::OnPlaybackTick),
		1.0f / 60.0f
	);
}

void SFrameTimingEditor::StopPlayback()
{
	if (!bIsPlaying) return;

	bIsPlaying = false;
	if (PlaybackTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(PlaybackTickerHandle);
		PlaybackTickerHandle.Reset();
	}
}

void SFrameTimingEditor::TogglePlayback()
{
	if (bIsPlaying)
	{
		StopPlayback();
	}
	else
	{
		StartPlayback();
	}
}

bool SFrameTimingEditor::OnPlaybackTick(float DeltaTime)
{
	UPaperFlipbook* FB = GetCurrentFlipbook();
	if (!FB || FB->GetNumKeyFrames() == 0) return true;

	FFlipbookTimingData Timing = FFlipbookTimingData::ReadFromFlipbook(FB);
	if (Timing.TotalDurationSeconds <= 0.0f) return true;

	// Advance playback position
	PlaybackPosition += DeltaTime;
	if (PlaybackPosition >= Timing.TotalDurationSeconds)
	{
		PlaybackPosition = FMath::Fmod(PlaybackPosition, Timing.TotalDurationSeconds);
	}

	// Determine which frame we're on based on the current time
	float AccumulatedTime = 0.0f;
	int32 NewFrameIndex = 0;
	for (int32 i = 0; i < Timing.FrameDurations.Num(); i++)
	{
		float FrameDur = Timing.GetFrameDurationSeconds(i);
		if (PlaybackPosition < AccumulatedTime + FrameDur)
		{
			NewFrameIndex = i;
			break;
		}
		AccumulatedTime += FrameDur;
		NewFrameIndex = i;
	}

	if (NewFrameIndex != SelectedFrameIndex)
	{
		SelectedFrameIndex = NewFrameIndex;
	}

	return true; // Continue ticking
}

// ==========================================
// Undo Support
// ==========================================

void SFrameTimingEditor::PostUndo(bool bSuccess)
{
	if (bSuccess)
	{
		// Update cached FPS from flipbook in case it was reverted
		if (UPaperFlipbook* FB = GetCurrentFlipbook())
		{
			PlaybackFPS = FB->GetFramesPerSecond();
		}
		RefreshAll();
	}
}

void SFrameTimingEditor::PostRedo(bool bSuccess)
{
	if (bSuccess)
	{
		if (UPaperFlipbook* FB = GetCurrentFlipbook())
		{
			PlaybackFPS = FB->GetFramesPerSecond();
		}
		RefreshAll();
	}
}

void SFrameTimingEditor::BeginTransaction(const FText& Description)
{
	if (!ActiveTransaction.IsValid())
	{
		ActiveTransaction = MakeUnique<FScopedTransaction>(Description);
	}
}

void SFrameTimingEditor::EndTransaction()
{
	ActiveTransaction.Reset();
}

// ==========================================
// Keyboard Handling
// ==========================================

FReply SFrameTimingEditor::OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	float ZoomDelta = MouseEvent.GetWheelDelta() * 0.25f;
	PreviewZoom = FMath::Clamp(PreviewZoom + ZoomDelta, 1.0f, 10.0f);
	return FReply::Handled();
}

FReply SFrameTimingEditor::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	FKey Key = InKeyEvent.GetKey();

	// Undo/Redo
	if (InKeyEvent.IsControlDown() && !InKeyEvent.IsShiftDown() && Key == EKeys::Z)
	{
		if (GEditor) GEditor->UndoTransaction();
		return FReply::Handled();
	}
	if (InKeyEvent.IsControlDown() && (Key == EKeys::Y || (InKeyEvent.IsShiftDown() && Key == EKeys::Z)))
	{
		if (GEditor) GEditor->RedoTransaction();
		return FReply::Handled();
	}

	// Space - toggle playback (but not Ctrl+Space, which opens the content browser)
	if (Key == EKeys::SpaceBar && !InKeyEvent.IsControlDown())
	{
		TogglePlayback();
		return FReply::Handled();
	}

	// Left/Right - navigate frames
	if (Key == EKeys::Left && SelectedFrameIndex > 0)
	{
		OnFrameSelected(SelectedFrameIndex - 1);
		return FReply::Handled();
	}
	if (Key == EKeys::Right && SelectedFrameIndex < GetCurrentFrameCount() - 1)
	{
		OnFrameSelected(SelectedFrameIndex + 1);
		return FReply::Handled();
	}

	// Home/End
	if (Key == EKeys::Home)
	{
		OnFrameSelected(0);
		return FReply::Handled();
	}
	if (Key == EKeys::End)
	{
		int32 FrameCount = GetCurrentFrameCount();
		if (FrameCount > 0) OnFrameSelected(FrameCount - 1);
		return FReply::Handled();
	}

	// +/- or ]/[ - adjust duration
	if (Key == EKeys::RightBracket || Key == EKeys::Equals)
	{
		UPaperFlipbook* FB = GetCurrentFlipbook();
		if (FB && SelectedFrameIndex < FB->GetNumKeyFrames())
		{
			int32 CurDur = FB->GetKeyFrameChecked(SelectedFrameIndex).FrameRun;
			OnFrameDurationChanged(SelectedFrameIndex, FMath::Min(CurDur + 1, 999));
		}
		return FReply::Handled();
	}
	if (Key == EKeys::LeftBracket || Key == EKeys::Hyphen)
	{
		UPaperFlipbook* FB = GetCurrentFlipbook();
		if (FB && SelectedFrameIndex < FB->GetNumKeyFrames())
		{
			int32 CurDur = FB->GetKeyFrameChecked(SelectedFrameIndex).FrameRun;
			OnFrameDurationChanged(SelectedFrameIndex, FMath::Max(CurDur - 1, 1));
		}
		return FReply::Handled();
	}

	return FReply::Unhandled();
}

// ==========================================
// Helpers
// ==========================================

UPaperFlipbook* SFrameTimingEditor::GetCurrentFlipbook() const
{
	if (!Asset.IsValid() || !Asset->Flipbooks.IsValidIndex(SelectedFlipbookIndex))
	{
		return nullptr;
	}
	return Asset->Flipbooks[SelectedFlipbookIndex].Flipbook.LoadSynchronous();
}

const FFlipbookHitboxData* SFrameTimingEditor::GetCurrentFlipbookData() const
{
	if (!Asset.IsValid() || !Asset->Flipbooks.IsValidIndex(SelectedFlipbookIndex))
	{
		return nullptr;
	}
	return &Asset->Flipbooks[SelectedFlipbookIndex];
}

int32 SFrameTimingEditor::GetCurrentFrameCount() const
{
	UPaperFlipbook* FB = GetCurrentFlipbook();
	return FB ? FB->GetNumKeyFrames() : 0;
}

#undef LOCTEXT_NAMESPACE
