// Copyright 2026 Infinite Gameworks. All Rights Reserved.

// FrameTimingEditor.cpp - Main frame timing editor tab and frame duration list

#include "FrameTimingEditor.h"
#include "EditorCanvasUtils.h"
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
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/SToolTip.h"
#include "EditorCanvasUtils.h"
#include "CharacterProfileAssetEditor.h"

/** SFrameTimingEditor — Frame timing tab: per-frame duration editing with FPS-aware color coding and visual feedback. */

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

		// Get pivot shift (same logic as SSpriteEditorCanvas)
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
			MakePaintGeometry(AllottedGeometry,
				FVector2D(DrawSize),
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
		const FFlipbookProfileEntry& Anim = Asset->Flipbooks[FBIdx];
		if (!Anim.CombatData.FrameExtractionInfo.IsValidIndex(Frame)) return FIntPoint::ZeroValue;
		return Anim.CombatData.FrameExtractionInfo[Frame].SpriteOffset;
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
			.FillWidth(0.3f)
			.Padding(2, 0)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("FrameCol", "#"))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
			]

			+ SHorizontalBox::Slot()
			.FillWidth(0.7f)
			.Padding(2, 0)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("DurationCol", "Duration"))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
			]
		]

		// Frame rows (scrollable)
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

void SFrameDurationList::InvalidateDisplay()
{
	if (FrameListBox.IsValid())
	{
		FrameListBox->Invalidate(EInvalidateWidgetReason::Paint);
	}
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
		.Padding(FMargin(4, 2))
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
			.FillWidth(0.3f)
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
			.FillWidth(0.7f)
			.VAlign(VAlign_Center)
			.Padding(2, 0)
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

		// Main content area
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		.Padding(4)
		[
			SNew(SVerticalBox)

			// Top area: Flipbook List | (Toolbar + Preview) | Frame Duration List
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

				// Center: Toolbar + Preview (toolbar moved here so left flipbook list stays top-flush)
				+ SSplitter::Slot()
				.Value(0.63f)
				[
					SNew(SVerticalBox)

					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						BuildToolbar()
					]

					+ SVerticalBox::Slot()
					.FillHeight(1.0f)
					[
						SNew(SBorder)
						.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
						.Padding(4)
						[
							BuildPreviewPanel()
						]
					]
				]

				// Right: Frame duration list + batch tools
				+ SSplitter::Slot()
				.Value(0.17f)
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

			// Timeline (bottom area — includes sprite thumbnails in each frame block)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
				.Padding(4)
				[
					SNew(SScrollBox)
					.Orientation(Orient_Horizontal)
					+ SScrollBox::Slot()
					[
						SAssignNew(TimelineWidget, SAnimationTimeline)
						.Flipbook(GetCurrentFlipbook())
						.SelectedFrameIndex_Lambda([this]() { return SelectedFrameIndex; })
						.PlaybackPosition_Lambda([this]() { return PlaybackPosition; })
						.IsPlaying_Lambda([this]() { return bIsPlaying; })
						.DisplayUnit_Lambda([this]() { return DisplayUnit; })
						.SelectedFrames(ParentSelectedFrames)
					]
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

		// Search filter
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0, 0, 0, 4)
		[
			SNew(SSearchBox)
			.HintText(LOCTEXT("SearchFlipbooks", "Search..."))
			.OnTextChanged_Lambda([this](const FText& NewText) {
				FlipbookSearchFilter = NewText.ToString();
				RefreshFlipbookList();
			})
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
			.Text_Lambda([this]() {
				const FFlipbookProfileEntry* Anim = GetCurrentFlipbookData();
				UPaperFlipbook* FB = GetCurrentFlipbook();
				if (!Anim || !FB) return FText::FromString(TEXT("No Flipbook"));
				return FText::Format(LOCTEXT("FlipbookTitle", "{0}  Frame {1}/{2}"),
					FText::FromString(Anim->Identity.FlipbookName),
					FText::AsNumber(SelectedFrameIndex + 1),
					FText::AsNumber(FB->GetNumKeyFrames()));
			})
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
			.ColorAndOpacity(FLinearColor(0.6f, 0.6f, 0.6f))
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

		// Duration info (single line below canvas)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0, 2, 0, 0)
		[
			SAssignNew(PreviewBox, SVerticalBox)
		];
}

// ==========================================
// Refresh Functions
// ==========================================

void SFrameTimingEditor::RefreshAll()
{
	bNeedsRefresh = false;
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
		return Asset->Flipbooks[A].Identity.FlipbookName.Compare(Asset->Flipbooks[B].Identity.FlipbookName, ESearchCase::IgnoreCase) < 0;
	});

	// Item builder lambda
	auto BuildItem = [this](int32 CapturedIdx) -> TSharedRef<SWidget>
	{
		const FFlipbookProfileEntry& Anim = Asset->Flipbooks[CapturedIdx];
		UPaperFlipbook* LoadedFlipbook = !Anim.Identity.Flipbook.IsNull() ? Anim.Identity.Flipbook.LoadSynchronous() : nullptr;
		const bool bHasFlipbook = LoadedFlipbook != nullptr;
		const FText SourceNameText = FText::FromString(bHasFlipbook ? Anim.Identity.Flipbook.GetAssetName() : TEXT("No Flipbook Assigned"));

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
						.Text(FText::FromString(Anim.Identity.FlipbookName))
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
				.Padding(FMargin(8, 6))
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
							.Text(FText::FromString(Anim.Identity.FlipbookName))
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
								UPaperFlipbook* FB = Asset->Flipbooks[CapturedIdx].Identity.Flipbook.LoadSynchronous();
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

	// Filter helper — returns true if a flipbook entry passes the current search filter
	auto PassesFilter = [this](int32 Idx) -> bool
	{
		if (FlipbookSearchFilter.IsEmpty()) return true;
		return Asset->Flipbooks[Idx].Identity.FlipbookName.Contains(FlipbookSearchFilter, ESearchCase::IgnoreCase);
	};

	// If no groups exist (all ungrouped), render flat list
	if (GroupOrder.Num() <= 1 && GroupOrder.Contains(NAME_None))
	{
		for (int32 Idx : FlipbooksByGroup[NAME_None])
		{
			if (!PassesFilter(Idx)) continue;
			FlipbookListBox->AddSlot().AutoHeight().Padding(0, 1)[BuildItem(Idx)];
		}
		return;
	}

	// Render grouped list with collapsible headers
	for (FName GroupName : GroupOrder)
	{
		const TArray<int32>& GroupIndices = FlipbooksByGroup[GroupName];

		// Skip entire group if no items pass the filter
		bool bAnyVisible = false;
		for (int32 Idx : GroupIndices)
		{
			if (PassesFilter(Idx)) { bAnyVisible = true; break; }
		}
		if (!bAnyVisible) continue;

		bool bCollapsed = CollapsedFlipbookGroups && CollapsedFlipbookGroups->Contains(GroupName);
		FString DisplayName = GroupName.IsNone() ? TEXT("Ungrouped") : GroupName.ToString();

		// Count visible items for header display
		int32 VisibleCount = 0;
		for (int32 Idx : GroupIndices) { if (PassesFilter(Idx)) ++VisibleCount; }

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
						FText::FromString(DisplayName), FText::AsNumber(VisibleCount)))
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
				if (!PassesFilter(Idx)) continue;
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

	int32 Duration = FMath::Max(KF.FrameRun, 1);
	float CurrentFPS = PlaybackFPS;
	FLinearColor DurColor = SAnimationTimeline::GetFrameColor(Duration, CurrentFPS);
	int32 TotalMs = (CurrentFPS > 0.0f) ? FMath::RoundToInt(Duration * 1000.0f / CurrentFPS) : 0;

	// Hold label
	FString HoldLabel;
	if (Duration == 1) HoldLabel = TEXT("Standard");
	else
	{
		float HoldMs = (CurrentFPS > 0.0f) ? ((Duration - 1) * 1000.0f / CurrentFPS) : 0.0f;
		if (HoldMs <= 100.0f) HoldLabel = TEXT("Slight Hold");
		else if (HoldMs <= 250.0f) HoldLabel = TEXT("Medium Hold");
		else HoldLabel = TEXT("Long Hold");
	}

	PreviewBox->AddSlot()
	.AutoHeight()
	.Padding(4, 2)
	[
		SNew(SHorizontalBox)

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0, 0, 6, 0)
		[
			SNew(SBox)
			.WidthOverride(10)
			.HeightOverride(10)
			[
				SNew(SImage)
				.Image(FAppStyle::GetBrush("WhiteBrush"))
				.ColorAndOpacity(DurColor)
			]
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(FText::Format(LOCTEXT("HoldInfoFmt", "{0}  {1}f  {2}ms"),
				FText::FromString(HoldLabel),
				FText::AsNumber(Duration),
				FText::AsNumber(TotalMs)))
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
			.ColorAndOpacity(FLinearColor(0.6f, 0.6f, 0.6f))
		]
	];
}

// ==========================================
// Event Handlers
// ==========================================

void SFrameTimingEditor::OnFlipbookSelected(int32 Index)
{
	if (!Asset.IsValid() || !Asset->Flipbooks.IsValidIndex(Index)) return;

	const bool bWasPlaying = bIsPlaying;
	StopPlayback();
	SelectedFlipbookIndex = Index;

	// Sync selection back to parent so RefreshAll doesn't override with stale index
	OnFlipbookSelectedInList.ExecuteIfBound(Index);
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

	if (bWasPlaying)
	{
		StartPlayback();
	}
}

void SFrameTimingEditor::OnFrameSelected(int32 Index)
{
	int32 FrameCount = GetCurrentFrameCount();
	if (Index < 0 || Index >= FrameCount) return;

	SelectedFrameIndex = Index;

	// Invalidate both widgets so multi-select highlights update
	if (FrameDurationListWidget.IsValid())
	{
		FrameDurationListWidget->InvalidateDisplay();
	}
	if (TimelineWidget.IsValid())
	{
		TimelineWidget->Invalidate(EInvalidateWidgetReason::Paint);
	}

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

	// Invalidate cached timing data so playback picks up the change
	CachedPlaybackTiming = FFlipbookTimingData();

	// Refresh widgets — lambdas handle values, just repaint
	if (TimelineWidget.IsValid())
	{
		TimelineWidget->RefreshTimingData();
	}
	if (FrameDurationListWidget.IsValid())
	{
		FrameDurationListWidget->InvalidateDisplay();
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
		FrameDurationListWidget->InvalidateDisplay();
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

void SFrameTimingEditor::OnApplyBatchOperation()
{
	UPaperFlipbook* FB = GetCurrentFlipbook();
	if (!FB || FB->GetNumKeyFrames() == 0) return;

	// Determine source duration value
	int32 SourceDuration = 1;
	if (BatchSourceIndex == 0) // Current Frame's Duration
	{
		if (SelectedFrameIndex < 0 || SelectedFrameIndex >= FB->GetNumKeyFrames()) return;
		SourceDuration = FMath::Clamp(FB->GetKeyFrameChecked(SelectedFrameIndex).FrameRun, 1, 999);
	}
	else if (BatchSourceIndex == 1) // Duration of 1
	{
		SourceDuration = 1;
	}
	else if (BatchSourceIndex == 2) // Average Duration
	{
		FFlipbookTimingData Timing = FFlipbookTimingData::ReadFromFlipbook(FB);
		int32 TotalTicks = 0;
		for (int32 Dur : Timing.FrameDurations) TotalTicks += Dur;
		SourceDuration = FMath::Max(1, FMath::RoundToInt((float)TotalTicks / Timing.TotalFrames));
	}
	else if (BatchSourceIndex == 3) // Custom Value
	{
		SourceDuration = FMath::Clamp(BatchCustomValue, 1, 999);
	}

	BeginTransaction(LOCTEXT("BatchSetDuration", "Batch Set Frame Duration"));
	FB->Modify();

	{
		FScopedFlipbookMutator Mutator(FB);

		if (BatchTargetIndex == 0) // All Frames
		{
			for (FPaperFlipbookKeyFrame& KF : Mutator.KeyFrames)
			{
				KF.FrameRun = SourceDuration;
			}
		}
		else if (BatchTargetIndex == 1) // Selected Frames
		{
			if (ParentSelectedFrames && ParentSelectedFrames->Num() > 0)
			{
				for (int32 Idx : *ParentSelectedFrames)
				{
					if (Mutator.KeyFrames.IsValidIndex(Idx))
					{
						Mutator.KeyFrames[Idx].FrameRun = SourceDuration;
					}
				}
			}
			else if (Mutator.KeyFrames.IsValidIndex(SelectedFrameIndex))
			{
				Mutator.KeyFrames[SelectedFrameIndex].FrameRun = SourceDuration;
			}
		}
		else if (BatchTargetIndex == 2) // Remaining Frames
		{
			for (int32 i = SelectedFrameIndex; i < Mutator.KeyFrames.Num(); ++i)
			{
				Mutator.KeyFrames[i].FrameRun = SourceDuration;
			}
		}
		else if (BatchTargetIndex == 3) // Custom Range
		{
			int32 First = FMath::Clamp(FMath::Min(BatchRangeStart, BatchRangeEnd), 0, Mutator.KeyFrames.Num() - 1);
			int32 Last = FMath::Clamp(FMath::Max(BatchRangeStart, BatchRangeEnd), 0, Mutator.KeyFrames.Num() - 1);
			for (int32 i = First; i <= Last; ++i)
			{
				Mutator.KeyFrames[i].FrameRun = SourceDuration;
			}
		}
	}

	FB->MarkPackageDirty();
	EndTransaction();

	CachedPlaybackTiming = FFlipbookTimingData();
	if (TimelineWidget.IsValid()) TimelineWidget->RefreshTimingData();
	if (FrameDurationListWidget.IsValid()) FrameDurationListWidget->InvalidateDisplay();
	RefreshPreview();
	OnTimingDataModified.ExecuteIfBound();
}

TSharedRef<SWidget> SFrameTimingEditor::BuildBatchToolsPanel()
{
	// Source options
	TSharedPtr<TArray<TSharedPtr<FString>>> SourceOptions = MakeShared<TArray<TSharedPtr<FString>>>();
	SourceOptions->Add(MakeShared<FString>(TEXT("Current Frame's Duration")));
	SourceOptions->Add(MakeShared<FString>(TEXT("Duration of 1")));
	SourceOptions->Add(MakeShared<FString>(TEXT("Average Duration")));
	SourceOptions->Add(MakeShared<FString>(TEXT("Custom Value")));

	// Target options
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

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0, 0, 0, 4)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("BatchToolsHeader", "Batch Duration Tools"))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
		]

		// Sentence: "Set" [Source v]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0, 2)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0, 0, 4, 0)
			[
				SNew(STextBlock).Text(LOCTEXT("SetLabel", "Set")).Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.Padding(0, 0, 4, 0)
			[
				MakeCombo(SourceOptions, &BatchSourceIndex)
			]
		]

		// Custom value spinbox (visible only when "Custom Value" selected)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(16, 2, 0, 2)
		[
			SNew(SBox)
			.Visibility_Lambda([this]() { return BatchSourceIndex == 3 ? EVisibility::Visible : EVisibility::Collapsed; })
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0, 0, 4, 0)
				[
					SNew(STextBlock).Text(LOCTEXT("ValueLabel", "Value:")).Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					SNew(SSpinBox<int32>)
					.MinValue(1)
					.MaxValue(999)
					.Value_Lambda([this]() { return BatchCustomValue; })
					.OnValueChanged_Lambda([this](int32 NewValue) { BatchCustomValue = NewValue; })
				]
			]
		]

		// "to" [Target v]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0, 2)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0, 0, 4, 0)
			[
				SNew(STextBlock).Text(LOCTEXT("ToLabel", "to")).Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.Padding(0, 0, 4, 0)
			[
				MakeCombo(TargetOptions, &BatchTargetIndex)
			]
		]

		// Custom range spinboxes (visible when "Custom Range" selected)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(16, 2, 0, 2)
		[
			SNew(SBox)
			.Visibility_Lambda([this]() { return BatchTargetIndex == 3 ? EVisibility::Visible : EVisibility::Collapsed; })
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
				[
					SNew(STextBlock).Text(LOCTEXT("RangeFrom", "From:")).Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
				]
				+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0, 0, 8, 0)
				[
					SNew(SSpinBox<int32>).MinValue(0)
					.MaxValue_Lambda([this]() { return FMath::Max(0, GetCurrentFrameCount() - 1); })
					.Value_Lambda([this]() { return BatchRangeStart; })
					.OnValueChanged_Lambda([this](int32 V) { BatchRangeStart = V; })
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
				[
					SNew(STextBlock).Text(LOCTEXT("RangeTo", "To:")).Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
				]
				+ SHorizontalBox::Slot().FillWidth(1.0f)
				[
					SNew(SSpinBox<int32>).MinValue(0)
					.MaxValue_Lambda([this]() { return FMath::Max(0, GetCurrentFrameCount() - 1); })
					.Value_Lambda([this]() { return BatchRangeEnd; })
					.OnValueChanged_Lambda([this](int32 V) { BatchRangeEnd = V; })
				]
			]
		]

		// Apply button
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0, 4)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
			.HAlign(HAlign_Center)
			.ToolTipText(LOCTEXT("ApplyBatchTooltip", "Apply the selected batch duration operation"))
			.IsEnabled_Lambda([this]() {
				UPaperFlipbook* FB = GetCurrentFlipbook();
				if (!FB || FB->GetNumKeyFrames() == 0) return false;
				// "Current Frame's Duration" requires a valid frame selection
				if (BatchSourceIndex == 0 && (SelectedFrameIndex < 0 || SelectedFrameIndex >= FB->GetNumKeyFrames())) return false;
				// "Selected Frames" target requires multi-selection or at least a current frame
				if (BatchTargetIndex == 1 && (!ParentSelectedFrames || ParentSelectedFrames->Num() == 0)
					&& (SelectedFrameIndex < 0 || SelectedFrameIndex >= FB->GetNumKeyFrames())) return false;
				return true;
			})
			.OnClicked_Lambda([this]() { OnApplyBatchOperation(); return FReply::Handled(); })
			[
				SNew(STextBlock)
				.Text(LOCTEXT("Apply", "Apply"))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
			]
		];
}

// ==========================================
// Playback
// ==========================================

void SFrameTimingEditor::StartPlayback()
{
	if (bIsPlaying) return;

	bIsPlaying = true;

	// Cache timing data to avoid per-tick heap allocation
	UPaperFlipbook* FB = GetCurrentFlipbook();
	CachedPlaybackTiming = FB ? FFlipbookTimingData::ReadFromFlipbook(FB) : FFlipbookTimingData();

	// Seed playback position from current frame so playback resumes where the user is
	PlaybackPosition = CachedPlaybackTiming.GetFrameStartTime(SelectedFrameIndex);

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

	// Lazy-init cache if invalidated (e.g. flipbook switch during playback)
	if (CachedPlaybackTiming.TotalDurationSeconds <= 0.0f)
	{
		CachedPlaybackTiming = FFlipbookTimingData::ReadFromFlipbook(FB);
	}
	const FFlipbookTimingData& Timing = CachedPlaybackTiming;
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
		// Force repaint so preview canvas and timeline update immediately
		if (PreviewCanvas.IsValid())
		{
			PreviewCanvas->Invalidate(EInvalidateWidgetReason::Paint);
		}
		if (TimelineWidget.IsValid())
		{
			TimelineWidget->Invalidate(EInvalidateWidgetReason::Paint);
		}
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
		bNeedsRefresh = true;
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
		bNeedsRefresh = true;
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

FReply SFrameTimingEditor::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	// Skip when a text widget (spinbox editor, search box, rename field) has focus —
	// otherwise bracket/arrow keys get stolen from the text cursor and Ctrl+Z
	// triggers a transaction undo instead of editing the text.
	if (TSharedPtr<SWidget> FocusedWidget = FSlateApplication::Get().GetKeyboardFocusedWidget())
	{
		const FName WidgetType = FocusedWidget->GetType();
		if (WidgetType == TEXT("SEditableText") || WidgetType == TEXT("SMultiLineEditableText"))
		{
			return FReply::Unhandled();
		}
	}

	FKey Key = InKeyEvent.GetKey();

	// Undo/Redo — let the parent editor's global handler own this so PostUndo
	// fires once and refreshes the tab via the dirty-mask system.
	if (InKeyEvent.IsControlDown() && (Key == EKeys::Z || Key == EKeys::Y))
	{
		return FReply::Unhandled();
	}

	// Any other Ctrl+Key (Ctrl+S save, Ctrl+Home, etc.) should bubble up.
	if (InKeyEvent.IsControlDown())
	{
		return FReply::Unhandled();
	}

	// Space - toggle playback
	if (Key == EKeys::SpaceBar)
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
		if (FB && SelectedFrameIndex >= 0 && SelectedFrameIndex < FB->GetNumKeyFrames())
		{
			int32 CurDur = FB->GetKeyFrameChecked(SelectedFrameIndex).FrameRun;
			OnFrameDurationChanged(SelectedFrameIndex, FMath::Min(CurDur + 1, 999));
		}
		return FReply::Handled();
	}
	if (Key == EKeys::LeftBracket || Key == EKeys::Hyphen)
	{
		UPaperFlipbook* FB = GetCurrentFlipbook();
		if (FB && SelectedFrameIndex >= 0 && SelectedFrameIndex < FB->GetNumKeyFrames())
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
	return Asset->Flipbooks[SelectedFlipbookIndex].Identity.Flipbook.LoadSynchronous();
}

const FFlipbookProfileEntry* SFrameTimingEditor::GetCurrentFlipbookData() const
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
