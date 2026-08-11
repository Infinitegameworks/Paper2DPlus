// Copyright 2026 Infinite Gameworks. All Rights Reserved.

// FrameTimingEditor.cpp - Main frame timing editor tab and frame duration list

#include "FrameTimingEditor.h"
#include "CharacterProfileEditorModel.h"
#include "FlipbookListBuilder.h"
#include "EditorCanvasUtils.h"
#include "AnimationTimeline.h"
#include "ProfilePropertyRow.h"
#include "SlateShortcutUtils.h"
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
#include "Framework/Application/SlateApplication.h"
#include "CharacterProfileAssetEditor.h"

/** SFrameTimingEditor — Frame timing tab: per-frame duration editing with FPS-aware color coding and visual feedback. */

#define LOCTEXT_NAMESPACE "FrameTimingEditor"

namespace Paper2DPlusEditor::FrameTimingEditorUtils
{
int32 FrameRunToMilliseconds(int32 FrameRun, float FPS)
{
	const int32 SafeFrameRun = FMath::Clamp(FrameRun, 1, 999);
	return FPS > 0.0f
		? FMath::Max(1, FMath::RoundToInt((SafeFrameRun / FPS) * 1000.0f))
		: SafeFrameRun;
}

int32 MillisecondsToFrameRun(int32 Milliseconds, float FPS)
{
	if (FPS <= 0.0f)
	{
		return FMath::Clamp(Milliseconds, 1, 999);
	}
	return FMath::Clamp(
		FMath::RoundToInt((FMath::Max(1, Milliseconds) / 1000.0f) * FPS),
		1,
		999);
}
}

const FName SFrameTimingEditor::TimingPanelId(TEXT("Paper2DPlus.Timing.Timing"));
const FName SFrameTimingEditor::SelectionPanelId(TEXT("Paper2DPlus.Timing.Selection"));
const FName SFrameTimingEditor::BatchPanelId(TEXT("Paper2DPlus.Timing.Batch"));

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
	const int32 CapturedIndex = FrameIndex;
	const TWeakObjectPtr<UPaperFlipbook> CapturedFlipbook = Flipbook;

	FrameListBox->AddSlot()
	.AutoHeight()
	.Padding(2, 1)
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
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
				.MinValue_Lambda([this]()
				{
					return DisplayUnit.Get(ETimingDisplayUnit::Frames) == ETimingDisplayUnit::Milliseconds
						? Paper2DPlusEditor::FrameTimingEditorUtils::FrameRunToMilliseconds(1, FPS.Get(12.0f))
						: 1;
				})
				.MaxValue_Lambda([this]()
				{
					return DisplayUnit.Get(ETimingDisplayUnit::Frames) == ETimingDisplayUnit::Milliseconds
						? Paper2DPlusEditor::FrameTimingEditorUtils::FrameRunToMilliseconds(999, FPS.Get(12.0f))
						: 999;
				})
				.Value_Lambda([this, CapturedFlipbook, CapturedIndex]() -> int32 {
					UPaperFlipbook* FB = CapturedFlipbook.Get();
					if (!FB || CapturedIndex >= FB->GetNumKeyFrames()) return 1;
					const int32 FrameRun = FMath::Max(FB->GetKeyFrameChecked(CapturedIndex).FrameRun, 1);
					return DisplayUnit.Get(ETimingDisplayUnit::Frames) == ETimingDisplayUnit::Milliseconds
						? Paper2DPlusEditor::FrameTimingEditorUtils::FrameRunToMilliseconds(FrameRun, FPS.Get(12.0f))
						: FrameRun;
				})
				.OnBeginSliderMovement_Lambda([this]()
				{
					bDurationSliderMoving = true;
					OnEditGestureStarted.ExecuteIfBound();
				})
				.OnValueChanged_Lambda([this, CapturedFlipbook, CapturedIndex](int32 NewValue)
				{
					if (bDurationSliderMoving)
					{
						CommitDisplayedDuration(CapturedFlipbook, CapturedIndex, NewValue);
					}
				})
				.OnValueCommitted_Lambda(
					[this, CapturedFlipbook, CapturedIndex](int32 NewValue, ETextCommit::Type)
					{
						if (!bDurationSliderMoving)
						{
							CommitDisplayedDuration(CapturedFlipbook, CapturedIndex, NewValue);
						}
					})
				.OnEndSliderMovement_Lambda([this, CapturedFlipbook, CapturedIndex](int32 NewValue)
				{
					if (!bDurationSliderMoving) return;
					CommitDisplayedDuration(CapturedFlipbook, CapturedIndex, NewValue);
					bDurationSliderMoving = false;
					OnEditGestureFinished.ExecuteIfBound();
				})
				.ToolTipText_Lambda([this, CapturedFlipbook, CapturedIndex]() {
					UPaperFlipbook* FB = CapturedFlipbook.Get();
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

void SFrameDurationList::CommitDisplayedDuration(
	TWeakObjectPtr<UPaperFlipbook> SourceFlipbook,
	int32 FrameIndex,
	int32 DisplayedDuration)
{
	UPaperFlipbook* FlipbookPtr = SourceFlipbook.Get();
	if (!FlipbookPtr) return;
	const int32 FrameRun = DisplayUnit.Get(ETimingDisplayUnit::Frames) == ETimingDisplayUnit::Milliseconds
		? Paper2DPlusEditor::FrameTimingEditorUtils::MillisecondsToFrameRun(
			DisplayedDuration, FPS.Get(12.0f))
		: FMath::Clamp(DisplayedDuration, 1, 999);
	OnFrameDurationChanged.ExecuteIfBound(FlipbookPtr, FrameIndex, FrameRun);
}

// ==========================================
// SFrameTimingEditor Implementation
// ==========================================

SFrameTimingEditor::~SFrameTimingEditor()
{
	StopPlayback();
	FinishActiveEditGesture(/*bReleaseTimelineCapture=*/true);
	if (TSharedPtr<FActiveTimerHandle> Timer = DeferredRefreshTimer.Pin())
	{
		UnRegisterActiveTimer(Timer.ToSharedRef());
	}
	DeferredRefreshTimer.Reset();

	if (Model.IsValid())
	{
		Model->OnFlipbookSelectionChanged.Remove(ModelFlipbookSelectionHandle);
		Model->OnFrameSelectionChanged.Remove(ModelFrameSelectionHandle);
		Model->OnGroupCollapseChanged.Remove(ModelGroupCollapseHandle);
		Model->OnSearchTextChanged.Remove(ModelSearchTextHandle);
		Model->OnAssetExternallyModified.Remove(ModelAssetExternallyModifiedHandle);
		Model->OnAssetDataChanged.Remove(ModelAssetDataChangedHandle);
	}

	if (GEditor)
	{
		GEditor->UnregisterForUndo(this);
	}
}

void SFrameTimingEditor::SyncSelectedFramesFromModel()
{
	if (Model.IsValid())
	{
		CachedSelectedFrames = Model->GetSelectedFrames();
	}
}

void SFrameTimingEditor::Construct(const FArguments& InArgs)
{
	Asset = InArgs._Asset;
	Model = InArgs._Model;
	HostContract = InArgs._HostContract.IsValid()
		? InArgs._HostContract
		: FProfileToolPanelHostContract::Embedded();
	bHostActive = HostContract.OwnsEmbeddedNavigation();
	if (Model.IsValid())
	{
		Asset = Model->GetAsset();
		SelectedFlipbookIndex = Model->GetSelectedFlipbookIndex();
		SelectedFrameIndex = Model->GetSelectedFrameIndex();
	}

	SyncSelectedFramesFromModel();

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
		HostContract.UsesExternalNavigation()
			? BuildCentralWorkspace()
			: BuildEmbeddedWorkspace()
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
		TimelineWidget->OnEditGestureStarted.BindSP(
			this, &SFrameTimingEditor::BeginFrameDurationGesture);
		TimelineWidget->OnEditGestureFinished.BindSP(
			this, &SFrameTimingEditor::EndContinuousTimingEdit);
	}

	if (Model.IsValid())
	{
		ModelFlipbookSelectionHandle = Model->OnFlipbookSelectionChanged.AddSP(this, &SFrameTimingEditor::OnModelFlipbookSelectionChanged);
		ModelFrameSelectionHandle = Model->OnFrameSelectionChanged.AddSP(this, &SFrameTimingEditor::OnModelFrameSelectionChanged);
		ModelGroupCollapseHandle = Model->OnGroupCollapseChanged.AddSP(this, &SFrameTimingEditor::OnModelGroupCollapseChanged);
		ModelSearchTextHandle = Model->OnSearchTextChanged.AddSP(this, &SFrameTimingEditor::OnModelSearchTextChanged);
		ModelAssetExternallyModifiedHandle = Model->OnAssetExternallyModified.AddSP(this, &SFrameTimingEditor::OnModelAssetExternallyModified);
		ModelAssetDataChangedHandle = Model->OnAssetDataChanged.AddSP(this, &SFrameTimingEditor::OnModelAssetDataChanged);
	}
}

void SFrameTimingEditor::GetContextualPanels(
	TArray<FProfileToolPanelDescriptor>& OutPanels) const
{
	if (!HostContract.UsesExternalNavigation())
	{
		return;
	}

	const TWeakPtr<SFrameTimingEditor> WeakController =
		ConstCastSharedRef<SFrameTimingEditor>(SharedThis(this));
	auto AddPanel = [&OutPanels, WeakController](
		FName PanelId,
		const FText& Label,
		const FText& ToolTip,
		TFunction<TSharedRef<SWidget>(SFrameTimingEditor&)> Builder)
	{
		FProfileToolPanelDescriptor Descriptor;
		Descriptor.PanelId = PanelId;
		Descriptor.Label = Label;
		Descriptor.ToolTip = ToolTip;
		Descriptor.CapabilityId = PanelId;
		Descriptor.IsAvailable = [WeakController]() { return WeakController.IsValid(); };
		Descriptor.WidgetFactory = [WeakController, PanelId, Builder = MoveTemp(Builder)]() -> TSharedRef<SWidget>
		{
			const TSharedPtr<SFrameTimingEditor> Controller = WeakController.Pin();
			if (!Controller.IsValid())
			{
				return SNullWidget::NullWidget;
			}
			++Controller->ContextPanelBuildCounts.FindOrAdd(PanelId);
			Controller->ContextPanelResolvedFlipbooks.FindOrAdd(PanelId) =
				Controller->GetLiveSelectedFlipbookIndex();
			Controller->ContextPanelResolvedFrames.FindOrAdd(PanelId) =
				Controller->GetLiveSelectedFrameIndex();
			return Builder(*Controller);
		};
		OutPanels.Add(MoveTemp(Descriptor));
	};

	AddPanel(
		TimingPanelId,
		LOCTEXT("TimingContextPanel", "Timing"),
		LOCTEXT("TimingContextPanelTip", "Edit FPS, choose display units, and inspect total duration."),
		[](SFrameTimingEditor& Controller) { return Controller.BuildToolbar(); });
	AddPanel(
		SelectionPanelId,
		LOCTEXT("TimingSelectionContextPanel", "Selection"),
		LOCTEXT("TimingSelectionContextPanelTip", "Inspect and edit frame durations using the live timeline selection."),
		[](SFrameTimingEditor& Controller) { return Controller.BuildSelectionPanel(); });
	AddPanel(
		BatchPanelId,
		LOCTEXT("TimingBatchContextPanel", "Batch"),
		LOCTEXT("TimingBatchContextPanelTip", "Apply one duration to all, selected, remaining, or ranged frames."),
		[](SFrameTimingEditor& Controller) { return Controller.BuildBatchToolsPanel(); });
}

void SFrameTimingEditor::HandleHostActivated()
{
	// The deferred seat's SetKeyboardFocus re-enters SDockTab activation synchronously. Suppress
	// only that re-entry so it cannot repeat the full refresh while focus is being committed.
	if (HostFocusSeat.IsApplying())
	{
		return;
	}
	bHostActive = true;
	RefreshAll();
	if (HostFocusSeat.ShouldRequestSeat(*this))
	{
		// Seat keyboard focus one paint after activation so the first Space press starts playback
		// without a preparatory click — the same deferred seat the Frame Cues tool uses.
		HostFocusSeat.TrackTimer(RegisterActiveTimer(
			0.0f,
			FWidgetActiveTimerDelegate::CreateSP(
				this,
				&SFrameTimingEditor::ApplyDeferredHostFocus)));
	}
}

EActiveTimerReturnType SFrameTimingEditor::ApplyDeferredHostFocus(
	double /*CurrentTime*/,
	float /*DeltaTime*/)
{
	return HostFocusSeat.ApplySeat(SharedThis(this), bHostActive);
}

void SFrameTimingEditor::ApplyDeferredHostFocusForTests()
{
	if (const TSharedPtr<FActiveTimerHandle> Timer = HostFocusSeat.GetPendingTimer())
	{
		UnRegisterActiveTimer(Timer.ToSharedRef());
	}
	ApplyDeferredHostFocus(0.0, 0.0f);
}

void SFrameTimingEditor::HandleHostDeactivated()
{
	StopPlayback();
	FinishActiveEditGesture(/*bReleaseTimelineCapture=*/true);
	bHostActive = false;
}

TSharedRef<SWidget> SFrameTimingEditor::BuildCentralWorkspace()
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		.Padding(4, 4, 4, 2)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
			.Padding(4)
			[
				BuildPreviewPanel()
			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4, 2, 4, 4)
		[
			BuildTimelinePanel()
		];
}

TSharedRef<SWidget> SFrameTimingEditor::BuildEmbeddedWorkspace()
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		.Padding(4, 4, 4, 2)
		[
			SNew(SSplitter)
			.Orientation(Orient_Horizontal)
			+ SSplitter::Slot()
			.Value(0.70f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					BuildToolbar()
				]
				+ SVerticalBox::Slot().FillHeight(1.0f)
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
					.Padding(4)
					[
						BuildPreviewPanel()
					]
				]
			]
			+ SSplitter::Slot()
			.Value(0.30f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().FillHeight(1.0f)
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
					.Padding(4)
					[
						BuildSelectionPanel()
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
					.Padding(6)
					[
						BuildBatchToolsPanel()
					]
				]
			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4, 2, 4, 4)
		[
			BuildTimelinePanel()
		];
}

TSharedRef<SWidget> SFrameTimingEditor::BuildTimelinePanel()
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
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
				.SelectedFrames(&CachedSelectedFrames)
			]
		];
}

TSharedRef<SWidget> SFrameTimingEditor::BuildSelectionPanel()
{
	TSharedPtr<SFrameDurationList> SelectionPtr;
	TSharedRef<SFrameDurationList> Selection =
		SAssignNew(SelectionPtr, SFrameDurationList)
		.Flipbook(GetCurrentFlipbook())
		.SelectedFrameIndex_Lambda([this]() { return SelectedFrameIndex; })
		.DisplayUnit_Lambda([this]() { return DisplayUnit; })
		.FPS_Lambda([this]() { return PlaybackFPS; })
		.SelectedFrames(&CachedSelectedFrames);
	FrameDurationListWidget = SelectionPtr;
	Selection->OnFrameSelected.BindSP(this, &SFrameTimingEditor::OnFrameSelected);
	Selection->OnEditGestureStarted.BindSP(this, &SFrameTimingEditor::BeginFrameDurationGesture);
	Selection->OnEditGestureFinished.BindSP(this, &SFrameTimingEditor::EndContinuousTimingEdit);
	const TWeakPtr<SFrameTimingEditor> WeakController = SharedThis(this);
	Selection->OnFrameDurationChanged.BindLambda(
		[WeakController](UPaperFlipbook* SourceFlipbook, int32 FrameIndex, int32 NewDuration)
		{
			if (const TSharedPtr<SFrameTimingEditor> Controller = WeakController.Pin())
			{
				Controller->OnSelectionFrameDurationChanged(
					SourceFlipbook, FrameIndex, NewDuration);
			}
		});
	return Selection;
}

TSharedRef<SWidget> SFrameTimingEditor::BuildToolbar()
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
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
					.OnBeginSliderMovement_Lambda([this]()
					{
						BeginFPSGesture();
						bFPSSliderMoving = bContinuousEditGesture;
					})
					.OnValueChanged_Lambda([this](float NewValue)
					{
						if (bFPSSliderMoving) OnFPSChanged(NewValue);
					})
					.OnValueCommitted_Lambda([this](float NewValue, ETextCommit::Type)
					{
						if (!bFPSSliderMoving) OnFPSChanged(NewValue);
					})
					.OnEndSliderMovement_Lambda([this](float NewValue)
					{
						if (!bFPSSliderMoving) return;
						OnFPSChanged(NewValue);
						bFPSSliderMoving = false;
						EndContinuousTimingEdit();
					})
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
				SNew(STextBlock)
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
			.InitialText(FText::FromString(FlipbookSearchFilter))
			.OnTextChanged_Lambda([this](const FText& NewText) {
				FlipbookSearchFilter = NewText.ToString();
				RefreshFlipbookList();
				if (Model.IsValid())
				{
					Model->SetFlipbookGroupSearchText(FlipbookSearchFilter);
				}
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
	// Re-resolve the asset from the shared model so a Base-Profile swap in the Character Layer editor
	// retargets this tab to the current profile (mirrors SHitboxEditorPanel::RefreshAll). Harmless in
	// the Character Profile editor, where the asset never swaps.
	if (Model.IsValid())
	{
		Asset = Model->GetAsset();
		SelectedFlipbookIndex = Model->GetSelectedFlipbookIndex();
		SelectedFrameIndex = Model->GetSelectedFrameIndex();
		SyncSelectedFramesFromModel();
	}
	if (UPaperFlipbook* FB = GetCurrentFlipbook())
	{
		PlaybackFPS = FB->GetFramesPerSecond();
		if (FB->GetNumKeyFrames() > 0)
		{
			SelectedFrameIndex = FMath::Clamp(SelectedFrameIndex, 0, FB->GetNumKeyFrames() - 1);
		}
	}
	bNeedsRefresh = false;
	RefreshFrameList();
}

void SFrameTimingEditor::RefreshFlipbookList()
{
	if (!FlipbookListBox.IsValid() || !Asset.IsValid()) return;

	FlipbookListBox->ClearChildren();

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
									.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.DarkGroupBorder"))
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
				.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
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
									.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.DarkGroupBorder"))
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

	TFunction<bool(int32)> SearchFilter = nullptr;
	if (!FlipbookSearchFilter.IsEmpty())
	{
		SearchFilter = [this](int32 Idx) -> bool
		{
			return Asset->Flipbooks[Idx].Identity.FlipbookName.Contains(FlipbookSearchFilter, ESearchCase::IgnoreCase);
		};
	}

	FFlipbookListBuilder::Build(FlipbookListBox, Model, BuildItem, [this]() { RefreshFlipbookList(); }, SearchFilter);
}

void SFrameTimingEditor::RefreshFrameList()
{
	UPaperFlipbook* FB = GetCurrentFlipbook();

	if (TimelineWidget.IsValid())
	{
		TimelineWidget->SetFlipbook(FB);
	}
	if (const TSharedPtr<SFrameDurationList> Selection = FrameDurationListWidget.Pin())
	{
		Selection->SetFlipbook(FB);
	}

	RefreshPreview();
}

void SFrameTimingEditor::RefreshPreview()
{
	if (!PreviewBox.IsValid()) return;

	PreviewBox->ClearChildren();

	UPaperFlipbook* FB = GetCurrentFlipbook();
	if (!FB || SelectedFrameIndex < 0 || SelectedFrameIndex >= FB->GetNumKeyFrames()) return;

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
				.Image(FAppStyle::Get().GetBrush("WhiteBrush"))
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
	if (Model.IsValid())
	{
		Model->SetSelectedFlipbook(Index);
		return;
	}

	const bool bWasPlaying = bIsPlaying;
	StopPlayback();
	FinishActiveEditGesture(/*bReleaseTimelineCapture=*/true);
	SelectedFlipbookIndex = Index;
	SelectedFrameIndex = 0;
	PlaybackPosition = 0.0f;

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
	if (Model.IsValid())
	{
		bPropagatingFrameSelection = true;
		Model->SetSelectedFrame(Index);
		bPropagatingFrameSelection = false;
		return;
	}

	SelectedFrameIndex = Index;

	// Invalidate both widgets so multi-select highlights update
	if (const TSharedPtr<SFrameDurationList> Selection = FrameDurationListWidget.Pin())
	{
		Selection->InvalidateDisplay();
	}
	if (TimelineWidget.IsValid())
	{
		TimelineWidget->Invalidate(EInvalidateWidgetReason::Paint);
	}

	RefreshPreview();
}

void SFrameTimingEditor::OnFrameDurationChanged(int32 FrameIndex, int32 NewDuration)
{
	if (!CanAcceptEdits()) return;
	UPaperFlipbook* FB = GetCurrentFlipbook();
	if (!FB || FrameIndex < 0 || FrameIndex >= FB->GetNumKeyFrames()) return;

	NewDuration = FMath::Clamp(NewDuration, 1, 999);

	// Check if actually changed
	if (FB->GetKeyFrameChecked(FrameIndex).FrameRun == NewDuration) return;

	const bool bOwnsTransaction = !ActiveTransaction.IsValid();
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
	if (bOwnsTransaction)
	{
		EndTransaction();
	}

	// Invalidate cached timing data so playback picks up the change
	CachedPlaybackTiming = FFlipbookTimingData();

	// Refresh widgets — lambdas handle values, just repaint
	if (TimelineWidget.IsValid())
	{
		TimelineWidget->RefreshTimingData();
	}
	if (const TSharedPtr<SFrameDurationList> Selection = FrameDurationListWidget.Pin())
	{
		Selection->InvalidateDisplay();
	}
	RefreshPreview();

	if (bContinuousEditGesture)
	{
		bContinuousEditChanged = true;
	}
	else if (Model.IsValid())
	{
		Model->NotifyAssetDataChanged();
	}
}

void SFrameTimingEditor::OnSelectionFrameDurationChanged(
	UPaperFlipbook* SourceFlipbook,
	int32 FrameIndex,
	int32 NewDuration)
{
	if (!CanAcceptEdits() || !SourceFlipbook || SourceFlipbook != GetCurrentFlipbook())
	{
		return;
	}
	OnFrameDurationChanged(FrameIndex, NewDuration);
}

void SFrameTimingEditor::OnFPSChanged(float NewFPS)
{
	if (!CanAcceptEdits()) return;
	// Cache the FPS first so the edit sticks even when no flipbook is assigned
	// (the FPS spinbox's Value_Lambda reads back PlaybackFPS; without this the
	// typed value reverts on the next paint). The flipbook-mutating block below
	// still runs only when there is a flipbook to write to.
	NewFPS = FMath::Clamp(NewFPS, 1.0f, 120.0f);
	UPaperFlipbook* FB = GetCurrentFlipbook();
	if (!FB)
	{
		PlaybackFPS = NewFPS;
		return;
	}
	if (FMath::IsNearlyEqual(FB->GetFramesPerSecond(), NewFPS))
	{
		PlaybackFPS = FB->GetFramesPerSecond();
		return;
	}
	PlaybackFPS = NewFPS;

	const bool bOwnsTransaction = !ActiveTransaction.IsValid();
	BeginTransaction(LOCTEXT("SetFlipbookFPS", "Set Flipbook FPS"));
	FB->Modify();

	{
		FScopedFlipbookMutator Mutator(FB);
		Mutator.FramesPerSecond = NewFPS;
	}

	FB->MarkPackageDirty();
	if (bOwnsTransaction)
	{
		EndTransaction();
	}

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
	if (const TSharedPtr<SFrameDurationList> Selection = FrameDurationListWidget.Pin())
	{
		Selection->InvalidateDisplay();
	}
	RefreshPreview();

	if (bContinuousEditGesture)
	{
		bContinuousEditChanged = true;
	}
	else if (Model.IsValid())
	{
		Model->NotifyAssetDataChanged();
	}
}

void SFrameTimingEditor::OnDisplayUnitChanged(ETimingDisplayUnit NewUnit)
{
	DisplayUnit = NewUnit;
	if (const TSharedPtr<SFrameDurationList> Selection = FrameDurationListWidget.Pin())
	{
		Selection->InvalidateDisplay();
	}
	if (TimelineWidget.IsValid())
	{
		TimelineWidget->Invalidate(EInvalidateWidgetReason::Paint);
	}
}

void SFrameTimingEditor::BeginFrameDurationGesture()
{
	if (!CanAcceptEdits()) return;
	BeginContinuousTimingEdit(LOCTEXT("DragFrameDuration", "Set Frame Duration"));
}

void SFrameTimingEditor::BeginFPSGesture()
{
	if (!CanAcceptEdits()) return;
	BeginContinuousTimingEdit(LOCTEXT("DragFlipbookFPS", "Set Flipbook FPS"));
}

void SFrameTimingEditor::BeginContinuousTimingEdit(const FText& Description)
{
	if (bContinuousEditGesture) return;
	FinishActiveEditGesture(/*bReleaseTimelineCapture=*/false);
	BeginTransaction(Description);
	bContinuousEditGesture = ActiveTransaction.IsValid();
	bContinuousEditChanged = false;
}

void SFrameTimingEditor::EndContinuousTimingEdit()
{
	if (!bContinuousEditGesture) return;
	const bool bNotifyModel = bContinuousEditChanged;
	bContinuousEditGesture = false;
	bContinuousEditChanged = false;
	EndTransaction();
	if (bNotifyModel && Model.IsValid())
	{
		Model->NotifyAssetDataChanged();
	}
}

// ==========================================
// Model Delegate Handlers
// ==========================================

void SFrameTimingEditor::OnModelFlipbookSelectionChanged(int32 NewIndex)
{
	const bool bWasPlaying = bIsPlaying;
	StopPlayback();
	FinishActiveEditGesture(/*bReleaseTimelineCapture=*/true);
	SelectedFlipbookIndex = NewIndex;
	SelectedFrameIndex = GetLiveSelectedFrameIndex();
	SyncSelectedFramesFromModel();
	PlaybackPosition = 0.0f;
	if (!bHostActive && HostContract.UsesExternalNavigation())
	{
		bNeedsRefresh = true;
		return;
	}

	UPaperFlipbook* FB = GetCurrentFlipbook();
	if (FB)
	{
		PlaybackFPS = FB->GetFramesPerSecond();
	}

	RefreshFrameList();

	if (bWasPlaying)
	{
		StartPlayback();
	}
}

void SFrameTimingEditor::OnModelFrameSelectionChanged()
{
	FinishActiveEditGesture(/*bReleaseTimelineCapture=*/true);
	if (Model.IsValid())
	{
		SelectedFrameIndex = Model->GetSelectedFrameIndex();
	}
	if (!bPropagatingFrameSelection)
	{
		SyncSelectedFramesFromModel();
	}
	if (!bHostActive && HostContract.UsesExternalNavigation())
	{
		bNeedsRefresh = true;
		return;
	}

	if (const TSharedPtr<SFrameDurationList> Selection = FrameDurationListWidget.Pin())
	{
		Selection->InvalidateDisplay();
	}
	if (TimelineWidget.IsValid())
	{
		TimelineWidget->Invalidate(EInvalidateWidgetReason::Paint);
	}
	if (PreviewCanvas.IsValid())
	{
		PreviewCanvas->Invalidate(EInvalidateWidgetReason::Paint);
	}
	RefreshPreview();
}

void SFrameTimingEditor::OnModelGroupCollapseChanged()
{
}

void SFrameTimingEditor::OnModelSearchTextChanged(const FString& NewText)
{
	FlipbookSearchFilter = NewText;
}

void SFrameTimingEditor::OnModelAssetExternallyModified()
{
	if (HasActiveTransaction()) return;
	CachedSelectedFrames.Empty();
	FrameSelectionAnchorIndex = INDEX_NONE;
	bNeedsRefresh = true;
	ScheduleDeferredRefresh();
}

void SFrameTimingEditor::OnModelAssetDataChanged()
{
	if (bHostActive || HostContract.OwnsEmbeddedNavigation())
	{
		RefreshAll();
	}
	else
	{
		bNeedsRefresh = true;
	}
}

// ==========================================
// Batch Operations
// ==========================================

void SFrameTimingEditor::OnApplyBatchOperation()
{
	if (!CanAcceptEdits()) return;
	UPaperFlipbook* FB = GetCurrentFlipbook();
	if (!FB || FB->GetNumKeyFrames() == 0) return;
	const int32 LiveFrameIndex = GetLiveSelectedFrameIndex();

	// Determine source duration value
	int32 SourceDuration = 1;
	if (BatchSourceIndex == 0) // Current Frame's Duration
	{
		if (LiveFrameIndex < 0 || LiveFrameIndex >= FB->GetNumKeyFrames()) return;
		SourceDuration = FMath::Clamp(FB->GetKeyFrameChecked(LiveFrameIndex).FrameRun, 1, 999);
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
			if (CachedSelectedFrames.Num() > 0)
			{
				for (int32 Idx : CachedSelectedFrames)
				{
					if (Mutator.KeyFrames.IsValidIndex(Idx))
					{
						Mutator.KeyFrames[Idx].FrameRun = SourceDuration;
					}
				}
			}
			else if (Mutator.KeyFrames.IsValidIndex(LiveFrameIndex))
			{
				Mutator.KeyFrames[LiveFrameIndex].FrameRun = SourceDuration;
			}
		}
		else if (BatchTargetIndex == 2) // Remaining Frames
		{
			if (Mutator.KeyFrames.IsValidIndex(LiveFrameIndex))
			{
				for (int32 i = LiveFrameIndex; i < Mutator.KeyFrames.Num(); ++i)
				{
					Mutator.KeyFrames[i].FrameRun = SourceDuration;
				}
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
	if (const TSharedPtr<SFrameDurationList> Selection = FrameDurationListWidget.Pin()) Selection->InvalidateDisplay();
	RefreshPreview();
	if (Model.IsValid())
	{
		Model->NotifyAssetDataChanged();
	}
}

TSharedRef<SWidget> SFrameTimingEditor::BuildBatchToolsPanel()
{
	if (BatchSourceOptions.Num() == 0)
	{
		BatchSourceOptions.Add(MakeShared<FString>(TEXT("Current Frame's Duration")));
		BatchSourceOptions.Add(MakeShared<FString>(TEXT("Duration of 1")));
		BatchSourceOptions.Add(MakeShared<FString>(TEXT("Average Duration")));
		BatchSourceOptions.Add(MakeShared<FString>(TEXT("Custom Value")));
		BatchTargetOptions.Add(MakeShared<FString>(TEXT("All Frames")));
		BatchTargetOptions.Add(MakeShared<FString>(TEXT("Selected Frames")));
		BatchTargetOptions.Add(MakeShared<FString>(TEXT("Remaining Frames")));
		BatchTargetOptions.Add(MakeShared<FString>(TEXT("Custom Range")));
	}

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0, 0, 0, 4)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("BatchToolsHeader", "Batch Duration Tools"))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
		]

		// Sentence: "Set" [Source v] — shared details-style rows keep the sentence labels.
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			FProfilePropertyRowUtils::MakeRow(
				LOCTEXT("SetLabel", "Set"),
				FProfilePropertyRowUtils::MakeStringCombo(&BatchSourceOptions, &BatchSourceIndex))
		]

		// Custom value spinbox (visible only when "Custom Value" selected)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SBox)
			.Visibility_Lambda([this]() { return BatchSourceIndex == 3 ? EVisibility::Visible : EVisibility::Collapsed; })
			[
				FProfilePropertyRowUtils::MakeRow(
					LOCTEXT("ValueLabel", "Value"),
					SNew(SSpinBox<int32>)
					.MinValue(1)
					.MaxValue(999)
					.Value_Lambda([this]() { return BatchCustomValue; })
					.OnValueChanged_Lambda([this](int32 NewValue) { BatchCustomValue = NewValue; }))
			]
		]

		// "to" [Target v]
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			FProfilePropertyRowUtils::MakeRow(
				LOCTEXT("ToLabel", "to"),
				FProfilePropertyRowUtils::MakeStringCombo(&BatchTargetOptions, &BatchTargetIndex))
		]

		// Custom range spinboxes (visible when "Custom Range" selected)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SBox)
			.Visibility_Lambda([this]() { return BatchTargetIndex == 3 ? EVisibility::Visible : EVisibility::Collapsed; })
			[
				FProfilePropertyRowUtils::MakeRow(
					LOCTEXT("RangeLabel", "Range"),
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0, 0, 4, 0)
					[
						SNew(SSpinBox<int32>).MinValue(0)
						.ToolTipText(LOCTEXT("RangeFromTip", "First frame of the range"))
						.MaxValue_Lambda([this]() { return FMath::Max(0, GetCurrentFrameCount() - 1); })
						.Value_Lambda([this]() { return BatchRangeStart; })
						.OnValueChanged_Lambda([this](int32 V) { BatchRangeStart = V; })
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
					[
						SNew(STextBlock).Text(LOCTEXT("RangeDash", "to")).Font(FProfilePropertyRowUtils::GetPropertyFont())
					]
					+ SHorizontalBox::Slot().FillWidth(1.0f)
					[
						SNew(SSpinBox<int32>).MinValue(0)
						.ToolTipText(LOCTEXT("RangeToTip", "Last frame of the range"))
						.MaxValue_Lambda([this]() { return FMath::Max(0, GetCurrentFrameCount() - 1); })
						.Value_Lambda([this]() { return BatchRangeEnd; })
						.OnValueChanged_Lambda([this](int32 V) { BatchRangeEnd = V; })
					],
					FText::GetEmpty(), 0.0f, 0.0f)
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
				const int32 LiveFrameIndex = GetLiveSelectedFrameIndex();
				if (BatchSourceIndex == 0 && (LiveFrameIndex < 0 || LiveFrameIndex >= FB->GetNumKeyFrames())) return false;
				if (BatchTargetIndex == 1 && CachedSelectedFrames.Num() == 0
					&& (LiveFrameIndex < 0 || LiveFrameIndex >= FB->GetNumKeyFrames())) return false;
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
	UPaperFlipbook* FB = GetCurrentFlipbook();
	if (!FB || FB->GetNumKeyFrames() == 0) return;
	FinishActiveEditGesture(/*bReleaseTimelineCapture=*/true);

	// Cache timing data to avoid per-tick heap allocation
	CachedPlaybackTiming = FFlipbookTimingData::ReadFromFlipbook(FB);
	if (CachedPlaybackTiming.TotalDurationSeconds <= 0.0f) return;
	bIsPlaying = true;

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
	if (!FB || FB->GetNumKeyFrames() == 0)
	{
		bIsPlaying = false;
		PlaybackTickerHandle.Reset();
		return false;
	}

	// Lazy-init cache if invalidated (e.g. flipbook switch during playback)
	if (CachedPlaybackTiming.TotalDurationSeconds <= 0.0f)
	{
		CachedPlaybackTiming = FFlipbookTimingData::ReadFromFlipbook(FB);
	}
	const FFlipbookTimingData& Timing = CachedPlaybackTiming;
	if (Timing.TotalDurationSeconds <= 0.0f)
	{
		bIsPlaying = false;
		PlaybackTickerHandle.Reset();
		return false;
	}

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
		if (Model.IsValid())
		{
			Model->SetSelectedFrame(NewFrameIndex);
		}
		else
		{
			SelectedFrameIndex = NewFrameIndex;
			if (PreviewCanvas.IsValid())
			{
				PreviewCanvas->Invalidate(EInvalidateWidgetReason::Paint);
			}
			if (TimelineWidget.IsValid())
			{
				TimelineWidget->Invalidate(EInvalidateWidgetReason::Paint);
			}
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
		FinishActiveEditGesture(/*bReleaseTimelineCapture=*/true);
		// Update cached FPS from flipbook in case it was reverted
		if (UPaperFlipbook* FB = GetCurrentFlipbook())
		{
			PlaybackFPS = FB->GetFramesPerSecond();
		}
		bNeedsRefresh = true;
		if (bHostActive || HostContract.OwnsEmbeddedNavigation())
		{
			RefreshAll();
		}
	}
}

void SFrameTimingEditor::PostRedo(bool bSuccess)
{
	if (bSuccess)
	{
		FinishActiveEditGesture(/*bReleaseTimelineCapture=*/true);
		if (UPaperFlipbook* FB = GetCurrentFlipbook())
		{
			PlaybackFPS = FB->GetFramesPerSecond();
		}
		bNeedsRefresh = true;
		if (bHostActive || HostContract.OwnsEmbeddedNavigation())
		{
			RefreshAll();
		}
	}
}

void SFrameTimingEditor::BeginTransaction(const FText& Description)
{
	if (!ActiveTransaction.IsValid())
	{
		ActiveTransaction = MakeUnique<FScopedTransaction>(Description);
		++TransactionBeginCount;
	}
}

void SFrameTimingEditor::EndTransaction()
{
	if (ActiveTransaction.IsValid())
	{
		ActiveTransaction.Reset();
		++TransactionEndCount;
	}
}

void SFrameTimingEditor::FinishActiveEditGesture(bool bReleaseTimelineCapture)
{
	const bool bNotifyModel = bContinuousEditGesture && bContinuousEditChanged;
	// Clear ownership before releasing capture. Slate delivers OnMouseCaptureLost synchronously, and the
	// timeline's loss handler calls EndContinuousTimingEdit; leaving the flags live until afterwards would
	// close and notify the same gesture twice on host switches/destruction.
	bContinuousEditGesture = false;
	bContinuousEditChanged = false;
	bFPSSliderMoving = false;
	EndTransaction();
	if (bReleaseTimelineCapture
		&& FSlateApplication::IsInitialized()
		&& TimelineWidget.IsValid()
		&& TimelineWidget->HasMouseCapture())
	{
		FSlateApplication::Get().ReleaseAllPointerCapture();
	}
	if (bNotifyModel && Model.IsValid())
	{
		Model->NotifyAssetDataChanged();
	}
}

void SFrameTimingEditor::ScheduleDeferredRefresh()
{
	if (TSharedPtr<FActiveTimerHandle> Existing = DeferredRefreshTimer.Pin())
	{
		UnRegisterActiveTimer(Existing.ToSharedRef());
	}
	DeferredRefreshTimer = RegisterActiveTimer(
		0.0f,
		FWidgetActiveTimerDelegate::CreateSP(
			this, &SFrameTimingEditor::HandleDeferredRefreshTimer));
}

EActiveTimerReturnType SFrameTimingEditor::HandleDeferredRefreshTimer(double, float)
{
	DeferredRefreshTimer.Reset();
	if (bHostActive || HostContract.OwnsEmbeddedNavigation())
	{
		RefreshAll();
	}
	else
	{
		bNeedsRefresh = true;
	}
	return EActiveTimerReturnType::Stop;
}

bool SFrameTimingEditor::CanAcceptEdits() const
{
	return HostContract.OwnsEmbeddedNavigation() || bHostActive;
}

// ==========================================
// Keyboard Handling
// ==========================================

FReply SFrameTimingEditor::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	// Skip when a text widget (spinbox editor, search box, rename field) has focus —
	// otherwise bracket/arrow keys get stolen from the text cursor and Ctrl+Z
	// triggers a transaction undo instead of editing the text.
	if (Paper2DPlusEditor::SlateShortcutUtils::ShouldIgnoreShortcutForFocusedWidget())
	{
		return FReply::Unhandled();
	}

	FKey Key = InKeyEvent.GetKey();
	const int32 LiveFrameIndex = GetLiveSelectedFrameIndex();

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

	// Space - toggle queue playback if queue active, else single-flipbook playback
	if (Key == EKeys::SpaceBar)
	{
		if (Model.IsValid() && Model->IsQueueActive())
			Model->SetQueuePlaying(!Model->IsQueuePlaying());
		else
			TogglePlayback();
		return FReply::Handled();
	}

	// Up/Down: queue-aware flipbook navigation
	if (Key == EKeys::Up && Model.IsValid())
	{
		if (Model->StepQueue(-1) == INDEX_NONE)
		{
			int32 NewIdx = Model->GetVisualAdjacentFlipbookIndex(-1);
			if (NewIdx != INDEX_NONE) { Model->SetSelectedFlipbook(NewIdx); }
		}
		return FReply::Handled();
	}
	if (Key == EKeys::Down && Model.IsValid())
	{
		if (Model->StepQueue(1) == INDEX_NONE)
		{
			int32 NewIdx = Model->GetVisualAdjacentFlipbookIndex(1);
			if (NewIdx != INDEX_NONE) { Model->SetSelectedFlipbook(NewIdx); }
		}
		return FReply::Handled();
	}

	// Left/Right: queue-aware frame navigation
	if (Key == EKeys::Left)
	{
		if (LiveFrameIndex > 0)
		{
			OnFrameSelected(LiveFrameIndex - 1);
		}
		else if (Model.IsValid())
		{
			// No queue entry to step back to — wrap inside this flipbook rather than swallowing the
			// key, which parked the playhead on frame 0 with no way forward.
			if (Model->StepQueue(-1, /*bLandOnLastFrame=*/true) == INDEX_NONE)
			{
				int32 FC = GetCurrentFrameCount();
				if (FC > 1) { OnFrameSelected(FC - 1); }
			}
		}
		return FReply::Handled();
	}
	if (Key == EKeys::Right)
	{
		const int32 FrameCount = GetCurrentFrameCount();
		if (LiveFrameIndex < FrameCount - 1)
		{
			OnFrameSelected(LiveFrameIndex + 1);
		}
		else if (Model.IsValid())
		{
			if (Model->StepQueue(1) == INDEX_NONE)
			{
				if (FrameCount > 1) { OnFrameSelected(0); }
			}
		}
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
		if (FB && LiveFrameIndex >= 0 && LiveFrameIndex < FB->GetNumKeyFrames())
		{
			int32 CurDur = FB->GetKeyFrameChecked(LiveFrameIndex).FrameRun;
			OnFrameDurationChanged(LiveFrameIndex, FMath::Min(CurDur + 1, 999));
		}
		return FReply::Handled();
	}
	if (Key == EKeys::LeftBracket || Key == EKeys::Hyphen)
	{
		UPaperFlipbook* FB = GetCurrentFlipbook();
		if (FB && LiveFrameIndex >= 0 && LiveFrameIndex < FB->GetNumKeyFrames())
		{
			int32 CurDur = FB->GetKeyFrameChecked(LiveFrameIndex).FrameRun;
			OnFrameDurationChanged(LiveFrameIndex, FMath::Max(CurDur - 1, 1));
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
	const int32 LiveIndex = GetLiveSelectedFlipbookIndex();
	if (!Asset.IsValid() || !Asset->Flipbooks.IsValidIndex(LiveIndex))
	{
		return nullptr;
	}
	return Asset->Flipbooks[LiveIndex].Identity.Flipbook.LoadSynchronous();
}

const FFlipbookProfileEntry* SFrameTimingEditor::GetCurrentFlipbookData() const
{
	const int32 LiveIndex = GetLiveSelectedFlipbookIndex();
	if (!Asset.IsValid() || !Asset->Flipbooks.IsValidIndex(LiveIndex))
	{
		return nullptr;
	}
	return &Asset->Flipbooks[LiveIndex];
}

int32 SFrameTimingEditor::GetCurrentFrameCount() const
{
	UPaperFlipbook* FB = GetCurrentFlipbook();
	return FB ? FB->GetNumKeyFrames() : 0;
}

int32 SFrameTimingEditor::GetLiveSelectedFlipbookIndex() const
{
	return Model.IsValid() ? Model->GetSelectedFlipbookIndex() : SelectedFlipbookIndex;
}

int32 SFrameTimingEditor::GetLiveSelectedFrameIndex() const
{
	return Model.IsValid() ? Model->GetSelectedFrameIndex() : SelectedFrameIndex;
}

int32 SFrameTimingEditor::GetFrameRunForTests(int32 FrameIndex) const
{
	UPaperFlipbook* FB = GetCurrentFlipbook();
	return FB && FrameIndex >= 0 && FrameIndex < FB->GetNumKeyFrames()
		? FMath::Max(1, FB->GetKeyFrameChecked(FrameIndex).FrameRun)
		: 0;
}

int32 SFrameTimingEditor::GetFrameMillisecondsForTests(int32 FrameIndex) const
{
	UPaperFlipbook* FB = GetCurrentFlipbook();
	return FB && FrameIndex >= 0 && FrameIndex < FB->GetNumKeyFrames()
		? Paper2DPlusEditor::FrameTimingEditorUtils::FrameRunToMilliseconds(
			GetFrameRunForTests(FrameIndex), FB->GetFramesPerSecond())
		: 0;
}

float SFrameTimingEditor::GetTotalDurationSecondsForTests() const
{
	return FFlipbookTimingData::ReadFromFlipbook(GetCurrentFlipbook()).TotalDurationSeconds;
}

void SFrameTimingEditor::SetFrameMillisecondsForTests(
	int32 FrameIndex,
	int32 Milliseconds)
{
	UPaperFlipbook* FB = GetCurrentFlipbook();
	if (!FB) return;
	OnFrameDurationChanged(
		FrameIndex,
		Paper2DPlusEditor::FrameTimingEditorUtils::MillisecondsToFrameRun(
			Milliseconds, FB->GetFramesPerSecond()));
}

void SFrameTimingEditor::ConfigureBatchForTests(
	int32 SourceIndex,
	int32 TargetIndex,
	int32 CustomValue,
	int32 RangeStart,
	int32 RangeEnd)
{
	BatchSourceIndex = SourceIndex;
	BatchTargetIndex = TargetIndex;
	BatchCustomValue = CustomValue;
	BatchRangeStart = RangeStart;
	BatchRangeEnd = RangeEnd;
}

bool SFrameTimingEditor::IsShortcutProtectedWidgetTypeForTests(
	const FString& WidgetTypeName)
{
	return Paper2DPlusEditor::SlateShortcutUtils::IsInputWidgetTypeName(WidgetTypeName);
}

#undef LOCTEXT_NAMESPACE
