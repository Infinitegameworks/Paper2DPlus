// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "SFrameEventTimelineTrack.h"
#include "EditorCanvasUtils.h"
#include "FrameCueDataProvider.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "FrameCues/Paper2DPlusFrameCue.h"
#include "FrameCues/Paper2DPlusFrameCuePreviewBehavior.h"
#include "FrameCues/Paper2DPlusFrameCueTimelinePresentation.h"
#include "Paper2DPlusSettings.h"
#include "PaperFlipbook.h"
#include "Styling/AppStyle.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"

#define LOCTEXT_NAMESPACE "FrameEventTimelineTrack"

void SFrameEventTimelineTrack::Construct(const FArguments& InArgs)
{
	Asset = InArgs._Asset;
	DataProvider = InArgs._DataProvider;
	CueSource = InArgs._Cues;
	CharacterFrameCountAttribute = InArgs._CharacterFrameCount;
	SelectedFlipbookIndex = InArgs._SelectedFlipbookIndex;
	SelectedEventIndex = InArgs._SelectedEventIndex;
	SelectedFrameIndex = InArgs._SelectedFrameIndex;
	SetClipping(EWidgetClipping::ClipToBounds);
	SetToolTipText(TAttribute<FText>::CreateSP(
		this,
		&SFrameEventTimelineTrack::GetHoveredCueToolTipText));
#if WITH_ACCESSIBILITY
	SetAccessibleBehavior(
		EAccessibleBehavior::Custom,
		TAttribute<FText>::CreateSP(
			this,
			&SFrameEventTimelineTrack::GetAccessibleSummaryText));
#endif
	TagColorsChangedHandle = UPaper2DPlusSettings::OnTagColorsChanged().AddSP(
		this,
		&SFrameEventTimelineTrack::HandleTagColorsChanged);
}

SFrameEventTimelineTrack::~SFrameEventTimelineTrack()
{
	UPaper2DPlusSettings::OnTagColorsChanged().Remove(TagColorsChangedHandle);
}

void SFrameEventTimelineTrack::HandleTagColorsChanged()
{
	Invalidate(EInvalidateWidgetReason::Paint);
}

FText SFrameEventTimelineTrack::GetHoveredCueToolTipText() const
{
	const TArray<TObjectPtr<UPaper2DPlusCueBase>>* CurrentCues = ResolveCues();
	const int32 HoveredEventIndex = ResolveCurrentEventIndex(HoveredCueIdentity);
	const UPaper2DPlusCueBase* Cue =
		CurrentCues && CurrentCues->IsValidIndex(HoveredEventIndex)
		? (*CurrentCues)[HoveredEventIndex].Get()
		: nullptr;
	if (!Cue)
	{
		return FText::GetEmpty();
	}

	const Paper2DPlusFrameCuePreviewBehavior::FPlacementBadge Badge =
		Paper2DPlusFrameCuePreviewBehavior::GetPlacementBadge(Cue);
	return Paper2DPlusFrameCueTimelinePresentation::BuildToolTip(
		*Cue,
		Badge.bHasError ? Badge.ToolTip : FText::GetEmpty());
}

FVector2D SFrameEventTimelineTrack::ComputeDesiredSize(float LayoutScaleMultiplier) const
{
	const TArray<TObjectPtr<UPaper2DPlusCueBase>>* Events = ResolveCues();
	const int32 TotalColumns = GetTotalColumns();
	const float TotalWidth = LabelWidth + (TotalColumns > 0
		? TotalColumns * ColumnWidth
		: Paper2DPlusFrameCueTimeline::FTimingGeometry::MinimumNonTimingBodyWidth);
	const float TotalHeight = Events ? GetTotalEventsHeight() : EmptyTrackHeight;
	return FVector2D(TotalWidth, FMath::Max(TotalHeight, 1.0f));
}

int32 SFrameEventTimelineTrack::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
	int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const FSlateBrush* WhiteBrush = FAppStyle::Get().GetBrush("WhiteBrush");
	const TArray<TObjectPtr<UPaper2DPlusCueBase>>* ResolvedEvents = ResolveCues();
	if (!ResolvedEvents)
	{
		FSlateDrawElement::MakeBox(
			OutDrawElements,
			LayerId,
			MakePaintGeometry(AllottedGeometry, AllottedGeometry.GetLocalSize(), FSlateLayoutTransform()),
			WhiteBrush,
			ESlateDrawEffect::None,
			FLinearColor(0.08f, 0.08f, 0.08f, 0.55f));
		FSlateDrawElement::MakeText(
			OutDrawElements,
			LayerId + 1,
			MakePaintGeometry(
				AllottedGeometry,
				FVector2D(280.0f, EmptyTrackHeight),
				FSlateLayoutTransform(FVector2D(8.0f, 11.0f))),
			LOCTEXT("NoAnimationCueTrack", "Choose an animation to author Frame Cues."),
			FCoreStyle::GetDefaultFontStyle("Regular", 8),
			ESlateDrawEffect::None,
			FLinearColor(0.65f, 0.65f, 0.65f, 1.0f));
		return LayerId + 1;
	}

	const int32 CharFrames = GetCharacterFrameCount();
	const int32 TotalCols = GetTotalColumns();
	const int32 SelFrame = SelectedFrameIndex.Get(0);
	const int32 SelEvent = SelectedEventIndex.Get(INDEX_NONE);
	const TArray<TObjectPtr<UPaper2DPlusCueBase>>& Events = *ResolvedEvents;
	const int32 HoveredEventIndex = ResolveCurrentEventIndex(HoveredCueIdentity);
	if (HoveredCueIdentity.IsValid() && HoveredEventIndex == INDEX_NONE)
	{
		HoveredCueIdentity.Reset();
		bHoveringResizeHandle = false;
	}
	if (CueLabelMetricsCache.Num() > Events.Num() * 2 + 8)
	{
		CueLabelMetricsCache.Reset();
	}
	const FSlateFontInfo LabelFont = FCoreStyle::GetDefaultFontStyle("Regular", 7);
	const TSharedRef<FSlateFontMeasure> FontMeasure =
		FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	const FVector2D LocalCullMin = AllottedGeometry.AbsoluteToLocal(
		FVector2D(MyCullingRect.Left, MyCullingRect.Top));
	const FVector2D LocalCullMax = AllottedGeometry.AbsoluteToLocal(
		FVector2D(MyCullingRect.Right, MyCullingRect.Bottom));

	// -- Background for overflow area (past character end) --
	if (TotalCols > CharFrames)
	{
		float OverflowX = LabelWidth + CharFrames * ColumnWidth;
		float OverflowW = (TotalCols - CharFrames) * ColumnWidth;
		float TotalH = AllottedGeometry.GetLocalSize().Y;
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId,
			MakePaintGeometry(AllottedGeometry, FVector2D(OverflowW, TotalH), FSlateLayoutTransform(FVector2D(OverflowX, 0))),
			WhiteBrush, ESlateDrawEffect::None, FLinearColor(0.08f, 0.06f, 0.06f, 0.5f));
	}

	// -- Column grid lines --
	for (int32 Col = 0; Col <= TotalCols; Col++)
	{
		float X = LabelWidth + Col * ColumnWidth;
		FLinearColor LineColor = (Col == CharFrames)
			? FLinearColor(0.5f, 0.2f, 0.2f, 0.6f)
			: FLinearColor(0.2f, 0.2f, 0.2f, 0.3f);
		float Thickness = (Col == CharFrames) ? 2.0f : 1.0f;
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId,
			MakePaintGeometry(AllottedGeometry, FVector2D(Thickness, AllottedGeometry.GetLocalSize().Y), FSlateLayoutTransform(FVector2D(X, 0))),
			WhiteBrush, ESlateDrawEffect::None, LineColor);
	}

	// -- Compact named tracks with deterministic overlap sublanes --
	const TArray<Paper2DPlusFrameCueTimeline::FPackedTrack> PackedTracks = BuildPackedTracks();
	float TrackTop = 0.0f;
	for (int32 TrackIndex = 0; TrackIndex < PackedTracks.Num(); ++TrackIndex)
	{
		const Paper2DPlusFrameCueTimeline::FPackedTrack& PackedTrack = PackedTracks[TrackIndex];
		const bool bPendingReassignmentTarget = bReassigningTrack && bHasPendingTrackTarget
			&& PackedTrack.TrackId == PendingTrackId;
		const float SublaneHeight = EventTrackHeight;
		const float PackedTrackHeight = GetPackedTrackHeight(PackedTrack);
		const float TrackBottom = TrackTop + PackedTrackHeight;
		if (TrackBottom >= LocalCullMin.Y && TrackTop <= LocalCullMax.Y)
		{
			FSlateDrawElement::MakeBox(
				OutDrawElements,
				LayerId,
				MakePaintGeometry(
					AllottedGeometry,
					FVector2D(AllottedGeometry.GetLocalSize().X, PackedTrackHeight),
					FSlateLayoutTransform(FVector2D(0.0f, TrackTop))),
				WhiteBrush,
				ESlateDrawEffect::None,
				bPendingReassignmentTarget
					? FLinearColor(0.14f, 0.32f, 0.50f, 0.82f)
					: TrackIndex % 2 == 0
					? FLinearColor(0.075f, 0.075f, 0.075f, 0.55f)
					: FLinearColor(0.095f, 0.095f, 0.095f, 0.55f));
			FSlateDrawElement::MakeBox(
				OutDrawElements,
				LayerId + 1,
				MakePaintGeometry(
					AllottedGeometry,
					FVector2D(AllottedGeometry.GetLocalSize().X, 1.0f),
					FSlateLayoutTransform(FVector2D(0.0f, TrackBottom - 1.0f))),
				WhiteBrush,
				ESlateDrawEffect::None,
				FLinearColor(0.22f, 0.22f, 0.22f, 0.65f));
		}

		for (const Paper2DPlusFrameCueTimeline::FPackedPlacement& Packed : PackedTrack.Placements)
		{
			const int32 EvIdx = Packed.CueIndex;
			if (!Events.IsValidIndex(EvIdx) || !IsValid(Events[EvIdx]))
			{
				continue;
			}
			const UPaper2DPlusCueBase* Event = Events[EvIdx];
			const TWeakObjectPtr<UPaper2DPlusCueBase> EventIdentity = Events[EvIdx];
			const float BarY = TrackTop + Packed.Sublane * SublaneHeight + 2.0f;
			const float BarHeight = FMath::Max(4.0f, SublaneHeight - 4.0f);
			if (BarY + BarHeight < LocalCullMin.Y || BarY > LocalCullMax.Y)
			{
				continue;
			}

			const float BarX = Packed.StartFrame * ColumnWidth;
			const float BarW = FMath::Max(
				4.0f,
				(Packed.EndFrameInclusive - Packed.StartFrame + 1) * ColumnWidth);
			const FVector2D BarPos(BarX, BarY);
			const FVector2D BarSize(BarW, BarHeight);
			const bool bIsSelectedEvent = EvIdx == SelEvent;
			const bool bIsCurrentlyHovered = EvIdx == HoveredEventIndex;
			const Paper2DPlusFrameCuePreviewBehavior::FPlacementBadge Badge =
				Paper2DPlusFrameCuePreviewBehavior::GetPlacementBadge(Event);
			const Paper2DPlusFrameCueTimelinePresentation::FPlacementPresentation Presentation =
				Paper2DPlusFrameCueTimelinePresentation::ResolvePlacement(
					*Event,
					Badge.bHasError);
			const Paper2DPlusFrameCueTimelinePresentation::FPlacementGeometry PlacementGeometry =
				Paper2DPlusFrameCueTimelinePresentation::ResolveGeometry(*Event, BarSize);
			FLinearColor BarColor = Presentation.FillColor;
			BarColor.A = bIsCurrentlyHovered || bIsSelectedEvent ? 0.9f : 0.68f;
			if (bIsSelectedEvent)
			{
				BarColor = BarColor * 1.15f;
				BarColor.A = 1.0f;
			}
			constexpr int32 PlacementBaseLayer = 2;
			FSlateDrawElement::MakeBox(
				OutDrawElements,
				LayerId
					+ PlacementBaseLayer
					+ Paper2DPlusFrameCueTimelinePresentation::FillLayerOffset,
				MakePaintGeometry(
					AllottedGeometry,
					PlacementGeometry.FillSize,
					FSlateLayoutTransform(BarPos + PlacementGeometry.FillOffset)),
				WhiteBrush,
				ESlateDrawEffect::None,
				BarColor);

			if (PlacementGeometry.bDrawAnchorMark)
			{
				FLinearColor AnchorColor = Presentation.LabelColor;
				AnchorColor.A = 0.82f;
				FSlateDrawElement::MakeBox(
					OutDrawElements,
					LayerId
						+ PlacementBaseLayer
						+ Paper2DPlusFrameCueTimelinePresentation::ShapeLayerOffset,
					MakePaintGeometry(
						AllottedGeometry,
						PlacementGeometry.AnchorSize,
						FSlateLayoutTransform(BarPos + PlacementGeometry.AnchorOffset)),
					WhiteBrush,
					ESlateDrawEffect::None,
					AnchorColor);
			}

			if (PlacementGeometry.bDrawEdgeDiamond)
			{
				// The trigger-edge diamond: a square rotated 45 degrees about its own center,
				// sitting on the boundary the cue fires on. Same contrast rule as the labels.
				FLinearColor DiamondColor = Presentation.LabelColor;
				DiamondColor.A = 0.92f;
				const FVector2D DiamondCenterOffset =
					PlacementGeometry.EdgeDiamondSize * 0.5f;
				// FPaintGeometry/MakeRotatedBox rotation-point parameter is FVector2D pre-5.6 and
				// the deprecation-bridged FVector2f from 5.6 (the AnimationMap arrowhead guard).
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6)
				const TOptional<FVector2f> DiamondRotationPoint =
					FVector2f(DiamondCenterOffset);
#else
				const TOptional<FVector2D> DiamondRotationPoint = DiamondCenterOffset;
#endif
				FSlateDrawElement::MakeRotatedBox(
					OutDrawElements,
					LayerId
						+ PlacementBaseLayer
						+ Paper2DPlusFrameCueTimelinePresentation::ShapeLayerOffset,
					MakePaintGeometry(
						AllottedGeometry,
						PlacementGeometry.EdgeDiamondSize,
						FSlateLayoutTransform(
							BarPos + PlacementGeometry.EdgeDiamondOffset)),
					WhiteBrush,
					ESlateDrawEffect::None,
					PI * 0.25f,
					DiamondRotationPoint,
					FSlateDrawElement::RelativeToElement,
					DiamondColor);
			}

			if (Event->IsRangeCue() && (bIsSelectedEvent || bIsCurrentlyHovered))
			{
				FSlateDrawElement::MakeBox(
					OutDrawElements,
					LayerId
						+ PlacementBaseLayer
						+ Paper2DPlusFrameCueTimelinePresentation::LabelLayerOffset,
					MakePaintGeometry(
						AllottedGeometry,
						FVector2D(ResizeHandleWidth, BarSize.Y),
						FSlateLayoutTransform(FVector2D(
							BarPos.X + BarSize.X - ResizeHandleWidth,
							BarPos.Y))),
					WhiteBrush,
					ESlateDrawEffect::None,
					FLinearColor(1.0f, 1.0f, 1.0f, 0.42f));
			}

			const FString Label = Presentation.Label.ToString();
			FCachedCueLabelMetrics* CachedMetrics = CueLabelMetricsCache.Find(EventIdentity);
			if (!CachedMetrics || CachedMetrics->Label != Label)
			{
				FCachedCueLabelMetrics NewMetrics;
				NewMetrics.Label = Label;
				NewMetrics.Width = FontMeasure->Measure(Label, LabelFont).X;
				CachedMetrics = &CueLabelMetricsCache.Add(EventIdentity, MoveTemp(NewMetrics));
			}
			if (CachedMetrics->Width + 10.0f <= BarW)
			{
				FSlateDrawElement::MakeText(
					OutDrawElements,
					LayerId
						+ PlacementBaseLayer
						+ Paper2DPlusFrameCueTimelinePresentation::LabelLayerOffset,
					MakePaintGeometry(
						AllottedGeometry,
						FVector2D(CachedMetrics->Width, BarHeight),
						FSlateLayoutTransform(FVector2D(BarX + 5.0f, BarY + 4.0f))),
					Label,
					LabelFont,
					ESlateDrawEffect::None,
					Presentation.LabelColor);
			}

			// Behavior that failed during preview badges its own placement, so the designer can see
			// which cue on the timeline needs fixing without reading the log. Preview keeps running.
			if (Presentation.bShowErrorBadge)
			{
				constexpr float BadgeSize = 8.0f;
				constexpr float BadgeInset = 2.0f;
				FSlateDrawElement::MakeBox(
					OutDrawElements,
					LayerId
						+ PlacementBaseLayer
						+ Paper2DPlusFrameCueTimelinePresentation::ErrorBadgeLayerOffset,
					MakePaintGeometry(
						AllottedGeometry,
						FVector2D(BadgeSize, BadgeSize),
						FSlateLayoutTransform(FVector2D(
							BarPos.X + BarSize.X - BadgeSize - BadgeInset,
							BarPos.Y + BadgeInset))),
					WhiteBrush,
					ESlateDrawEffect::None,
					FLinearColor(1.0f, 0.28f, 0.22f, 1.0f));
			}

			if (bIsSelectedEvent)
			{
				const FLinearColor OutlineColor(1.0f, 1.0f, 1.0f, 0.92f);
				constexpr float Thickness = 1.5f;
				constexpr int32 SelectionLayerOffset =
					Paper2DPlusFrameCueTimelinePresentation::ErrorBadgeLayerOffset + 1;
				FSlateDrawElement::MakeBox(OutDrawElements, LayerId + PlacementBaseLayer + SelectionLayerOffset,
					MakePaintGeometry(AllottedGeometry, FVector2D(BarSize.X, Thickness), FSlateLayoutTransform(BarPos)),
					WhiteBrush, ESlateDrawEffect::None, OutlineColor);
				FSlateDrawElement::MakeBox(OutDrawElements, LayerId + PlacementBaseLayer + SelectionLayerOffset,
					MakePaintGeometry(AllottedGeometry, FVector2D(BarSize.X, Thickness),
						FSlateLayoutTransform(FVector2D(BarPos.X, BarPos.Y + BarSize.Y - Thickness))),
					WhiteBrush, ESlateDrawEffect::None, OutlineColor);
				FSlateDrawElement::MakeBox(OutDrawElements, LayerId + PlacementBaseLayer + SelectionLayerOffset,
					MakePaintGeometry(AllottedGeometry, FVector2D(Thickness, BarSize.Y), FSlateLayoutTransform(BarPos)),
					WhiteBrush, ESlateDrawEffect::None, OutlineColor);
				FSlateDrawElement::MakeBox(OutDrawElements, LayerId + PlacementBaseLayer + SelectionLayerOffset,
					MakePaintGeometry(AllottedGeometry, FVector2D(Thickness, BarSize.Y),
						FSlateLayoutTransform(FVector2D(BarPos.X + BarSize.X - Thickness, BarPos.Y))),
					WhiteBrush, ESlateDrawEffect::None, OutlineColor);
			}
		}
		TrackTop = TrackBottom;
	}
	if (Events.IsEmpty())
	{
		FSlateDrawElement::MakeBox(
			OutDrawElements,
			LayerId,
			MakePaintGeometry(
				AllottedGeometry,
				FVector2D(AllottedGeometry.GetLocalSize().X, EmptyTrackHeight),
				FSlateLayoutTransform()),
			WhiteBrush,
			ESlateDrawEffect::None,
			FLinearColor(0.08f, 0.08f, 0.08f, 0.55f));
		const FText EmptyMessage = CharFrames > 0
			? LOCTEXT("EmptyDefaultCueTrack", "Default track is empty — use Add Cue to begin.")
			: LOCTEXT(
				"ZeroFrameCueTrack",
				"This animation has no definitive key frames. Add frames before placing Cues.");
		FSlateDrawElement::MakeText(
			OutDrawElements,
			LayerId + 1,
			MakePaintGeometry(
				AllottedGeometry,
				FVector2D(280.0f, EmptyTrackHeight),
				FSlateLayoutTransform(FVector2D(8.0f, 11.0f))),
			EmptyMessage,
			FCoreStyle::GetDefaultFontStyle("Regular", 8),
			ESlateDrawEffect::None,
			FLinearColor(0.65f, 0.65f, 0.65f, 1.0f));
	}

	// -- Playhead (selected frame) --
	if (CharFrames > 0 && SelFrame >= 0 && SelFrame < CharFrames)
	{
		float PlayheadX = LabelWidth + SelFrame * ColumnWidth + ColumnWidth * 0.5f;
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 7,
			MakePaintGeometry(AllottedGeometry, FVector2D(2, AllottedGeometry.GetLocalSize().Y), FSlateLayoutTransform(FVector2D(PlayheadX - 1, 0))),
			WhiteBrush, ESlateDrawEffect::None, FLinearColor(0.9f, 0.9f, 0.9f, 0.6f));
	}

	// Keyboard users get a non-color-only, persistent focus boundary around the editable timeline.
	if (HasKeyboardFocus())
	{
		const FVector2D Size = AllottedGeometry.GetLocalSize();
		const FLinearColor FocusColor(0.95f, 0.95f, 0.95f, 0.95f);
		constexpr float FocusThickness = 2.0f;
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 8,
			MakePaintGeometry(AllottedGeometry, FVector2D(Size.X, FocusThickness), FSlateLayoutTransform()),
			WhiteBrush, ESlateDrawEffect::None, FocusColor);
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 8,
			MakePaintGeometry(AllottedGeometry, FVector2D(Size.X, FocusThickness),
				FSlateLayoutTransform(FVector2D(0.0f, FMath::Max(0.0f, Size.Y - FocusThickness)))),
			WhiteBrush, ESlateDrawEffect::None, FocusColor);
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 8,
			MakePaintGeometry(AllottedGeometry, FVector2D(FocusThickness, Size.Y), FSlateLayoutTransform()),
			WhiteBrush, ESlateDrawEffect::None, FocusColor);
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 8,
			MakePaintGeometry(AllottedGeometry, FVector2D(FocusThickness, Size.Y),
				FSlateLayoutTransform(FVector2D(FMath::Max(0.0f, Size.X - FocusThickness), 0.0f))),
			WhiteBrush, ESlateDrawEffect::None, FocusColor);
	}

	return LayerId + 8;
}

FReply SFrameEventTimelineTrack::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	FVector2D LocalPos = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());

	if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		int32 HitEvent = HitTestEventBar(MyGeometry, LocalPos);
		if (HitEvent != INDEX_NONE)
		{
			const TArray<TObjectPtr<UPaper2DPlusCueBase>>* CurrentCues = ResolveCues();
			if (!CurrentCues || !CurrentCues->IsValidIndex(HitEvent)
				|| !IsValid((*CurrentCues)[HitEvent]))
			{
				return FReply::Handled();
			}
			const TWeakObjectPtr<UPaper2DPlusCueBase> HitCueIdentity = (*CurrentCues)[HitEvent];
			OnEventSelected.ExecuteIfBound(HitEvent);
			const int32 CurrentEventIndex = ResolveCurrentEventIndex(HitCueIdentity);
			if (CurrentEventIndex != INDEX_NONE)
			{
				ShowEventContextMenu(MyGeometry, MouseEvent, CurrentEventIndex);
			}
			return FReply::Handled().SetUserFocus(SharedThis(this), EFocusCause::Mouse);
		}

		int32 ContextFrame = INDEX_NONE;
		FGuid ContextTrack;
		if (ResolveAddCueContextTarget(LocalPos, ContextFrame, ContextTrack))
		{
			ShowTrackContextMenu(MouseEvent, ContextFrame, ContextTrack);
			return FReply::Handled().SetUserFocus(SharedThis(this), EFocusCause::Mouse);
		}
		return FReply::Unhandled();
	}

	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton) return FReply::Unhandled();

	// Check if clicking on an event bar
	int32 HitEvent = HitTestEventBar(MyGeometry, LocalPos);
	if (HitEvent != INDEX_NONE)
	{
		const TArray<TObjectPtr<UPaper2DPlusCueBase>>* SourceCues = ResolveCues();
		if (!SourceCues || !SourceCues->IsValidIndex(HitEvent)
			|| !IsValid((*SourceCues)[HitEvent]))
		{
			return FReply::Handled();
		}
		const TWeakObjectPtr<UPaper2DPlusCueBase> HitCueIdentity = (*SourceCues)[HitEvent];
		// The trigger-edge diamond outranks the plain bar so grabbing the glyph never starts a
		// frame move; it is moment-only, so it can never contest the Range resize handle.
		const bool bOnEdgeDiamond = HitTestEdgeDiamond(MyGeometry, LocalPos, HitEvent);
		const bool bOnResizeHandle =
			!bOnEdgeDiamond && HitTestResizeHandle(MyGeometry, LocalPos, HitEvent);

		OnEventSelected.ExecuteIfBound(HitEvent);
		const int32 CurrentEventIndex = ResolveCurrentEventIndex(HitCueIdentity);
		if (CurrentEventIndex == INDEX_NONE)
		{
			return FReply::Handled();
		}
		SourceCues = ResolveCues();
		if (!SourceCues || !SourceCues->IsValidIndex(CurrentEventIndex)
			|| (*SourceCues)[CurrentEventIndex].Get() != HitCueIdentity.Get())
		{
			return FReply::Handled();
		}

		// Arm only. Crossing Slate's drag threshold below is the transaction boundary; a click that
		// merely selects this cue never calls Asset->Modify() through OnEventDragStarted.
		bDragArmed = true;
		bArmedForResize = bOnResizeHandle;
		bArmedForEdge = bOnEdgeDiamond;
		bDraggingEvent = false;
		bResizingEvent = false;
		bEditingEdge = false;
		bReassigningTrack = false;
		bHasPendingTrackTarget = false;
		PendingTrackId.Invalidate();
		DragEventIndex = CurrentEventIndex;
		DragCueIdentity = HitCueIdentity;
		DragStartX = LocalPos.X;
		DragStartScreenPos = MouseEvent.GetScreenSpacePosition();
		GetEventFrameRange((*SourceCues)[CurrentEventIndex], DragStartFrame, DragStartFrameCount);

		return FReply::Handled()
			.CaptureMouse(SharedThis(this))
			.SetUserFocus(SharedThis(this), EFocusCause::Mouse);
	}

	return FReply::Unhandled();
}

FReply SFrameEventTimelineTrack::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if ((bDragArmed || bDraggingEvent || bResizingEvent || bEditingEdge || bReassigningTrack)
		&& MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		const bool bStartedTransaction = bDraggingEvent || bResizingEvent || bEditingEdge;
		const bool bCommitTrackReassignment = bReassigningTrack && bHasPendingTrackTarget;
		const TWeakObjectPtr<UPaper2DPlusCueBase> Cue = DragCueIdentity;
		const FGuid TargetTrackId = PendingTrackId;
		ResetDragState();
		if (bStartedTransaction)
		{
			OnEventDragEnded.ExecuteIfBound();
		}
		if (bCommitTrackReassignment)
		{
			MoveContextCueToTrack(Cue, TargetTrackId);
		}
		return FReply::Handled().ReleaseMouseCapture();
	}
	return FReply::Unhandled();
}

FReply SFrameEventTimelineTrack::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	FVector2D LocalPos = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
	const bool bHasGesture =
		bDragArmed || bResizingEvent || bDraggingEvent || bEditingEdge || bReassigningTrack;
	if (bHasGesture && !HasMouseCapture())
	{
		const bool bStartedTransaction = bDraggingEvent || bResizingEvent || bEditingEdge;
		ResetDragState();
		if (bStartedTransaction)
		{
			OnEventDragEnded.ExecuteIfBound();
		}
		return FReply::Unhandled();
	}
	if (bHasGesture)
	{
		DragEventIndex = ResolveCurrentEventIndex(DragCueIdentity);
		if (DragEventIndex == INDEX_NONE)
		{
			const bool bStartedTransaction = bDraggingEvent || bResizingEvent || bEditingEdge;
			ResetDragState();
			if (bStartedTransaction)
			{
				OnEventDragEnded.ExecuteIfBound();
			}
			return FReply::Handled().ReleaseMouseCapture();
		}
	}

	if (bDragArmed)
	{
		const FVector2D ScreenDelta =
			MouseEvent.GetScreenSpacePosition() - DragStartScreenPos;
		const float DragTriggerDistance = FSlateApplication::IsInitialized()
			? FSlateApplication::Get().GetDragTriggerDistance()
			: 5.0f;
		const Paper2DPlusFrameCueTimeline::EDragAxis DragAxis =
			Paper2DPlusFrameCueTimeline::ResolveDragAxis(
				ScreenDelta, DragTriggerDistance, !bArmedForResize && !bArmedForEdge);
		if (DragAxis == Paper2DPlusFrameCueTimeline::EDragAxis::Pending)
		{
			return FReply::Handled();
		}
		const int32 CandidateFrameDelta = FMath::RoundToInt32(
			(LocalPos.X - DragStartX) / ColumnWidth);
		UPaper2DPlusCueBase* ArmedCue = DragCueIdentity.Get();
		if (DragAxis == Paper2DPlusFrameCueTimeline::EDragAxis::Vertical)
		{
			bDragArmed = false;
			bReassigningTrack = true;
			bHasPendingTrackTarget = true;
			PendingTrackId = TrackIdFromY(LocalPos.Y);
			Invalidate(EInvalidateWidgetReason::Paint);
			return FReply::Handled();
		}
		const int32 CharacterFrameCount = GetCharacterFrameCount();
		if (!IsValid(ArmedCue) || CharacterFrameCount <= 0)
		{
			return FReply::Handled();
		}
		if (bArmedForEdge)
		{
			const UPaper2DPlusCue* ArmedMoment = Cast<UPaper2DPlusCue>(ArmedCue);
			if (!ArmedMoment)
			{
				return FReply::Handled();
			}
			const float CellX = FMath::Clamp(
				ArmedMoment->TriggerFrame, 0, CharacterFrameCount - 1) * ColumnWidth;
			const EPaper2DPlusCueTriggerEdge CandidateEdge =
				(LocalPos.X - CellX) > ColumnWidth * 0.5f
					? EPaper2DPlusCueTriggerEdge::FrameEnd
					: EPaper2DPlusCueTriggerEdge::FrameStart;
			if (CandidateEdge == ArmedMoment->TriggerEdge)
			{
				return FReply::Handled();
			}
		}
		else if (bArmedForResize)
		{
			const int32 MaxCount = FMath::Max(
				1, CharacterFrameCount - FMath::Clamp(
					ArmedCue->GetPrimaryAnchorFrame(), 0, CharacterFrameCount - 1));
			const int32 CandidateCount = FMath::Clamp(
				DragStartFrameCount + CandidateFrameDelta, 1, MaxCount);
			if (CandidateCount == ArmedCue->GetCueFrameCount())
			{
				return FReply::Handled();
			}
		}
		else
		{
			const int32 Duration = ArmedCue->IsRangeCue() ? ArmedCue->GetCueFrameCount() : 1;
			const int32 CandidateFrame = FMath::Clamp(
				DragStartFrame + CandidateFrameDelta,
				0,
				FMath::Max(0, CharacterFrameCount - Duration));
			if (CandidateFrame == ArmedCue->GetPrimaryAnchorFrame())
			{
				return FReply::Handled();
			}
		}

		const bool bStartResize = bArmedForResize;
		const bool bStartEdge = bArmedForEdge;
		bDragArmed = false;
		bArmedForResize = false;
		bArmedForEdge = false;
		bResizingEvent = bStartResize;
		bEditingEdge = bStartEdge;
		bDraggingEvent = !bStartResize && !bStartEdge;
		OnEventDragStarted.ExecuteIfBound(DragEventIndex);
		// The creator-owned callback may synchronously deactivate this host. Cancellation resets the
		// gesture and emits the matching end callback, so do not resume this mouse move or pair it twice.
		if (!bDraggingEvent && !bResizingEvent && !bEditingEdge)
		{
			return FReply::Handled();
		}

		// Preview callbacks are creator-extensible and may synchronously mutate the source array. Resolve
		// the stable cue identity again before this same mouse move is allowed to write.
		DragEventIndex = ResolveCurrentEventIndex(DragCueIdentity);
		if (DragEventIndex == INDEX_NONE)
		{
			ResetDragState();
			OnEventDragEnded.ExecuteIfBound();
			return FReply::Handled().ReleaseMouseCapture();
		}
	}

	if (bReassigningTrack && DragEventIndex != INDEX_NONE)
	{
		OnEdgeAutoscrollRequested.ExecuteIfBound(
			Paper2DPlusFrameCueTimeline::EDragAxis::Vertical,
			MouseEvent.GetScreenSpacePosition());
		const FGuid NewTarget = TrackIdFromY(LocalPos.Y);
		if (!bHasPendingTrackTarget || NewTarget != PendingTrackId)
		{
			PendingTrackId = NewTarget;
			bHasPendingTrackTarget = true;
			Invalidate(EInvalidateWidgetReason::Paint);
		}
		return FReply::Handled();
	}

	if (bEditingEdge && DragEventIndex != INDEX_NONE)
	{
		// The whole gesture lives inside one 52px cell: the pointer's half of the cell IS the
		// candidate edge. No autoscroll — the diamond never leaves its frame.
		const TArray<TObjectPtr<UPaper2DPlusCueBase>>* CurrentCues = ResolveCues();
		const UPaper2DPlusCue* Moment =
			CurrentCues && CurrentCues->IsValidIndex(DragEventIndex)
				? Cast<UPaper2DPlusCue>((*CurrentCues)[DragEventIndex].Get())
				: nullptr;
		const int32 CharacterFrameCount = GetCharacterFrameCount();
		if (Moment && CharacterFrameCount > 0)
		{
			const float CellX = FMath::Clamp(
				Moment->TriggerFrame, 0, CharacterFrameCount - 1) * ColumnWidth;
			const EPaper2DPlusCueTriggerEdge CandidateEdge =
				(LocalPos.X - CellX) > ColumnWidth * 0.5f
					? EPaper2DPlusCueTriggerEdge::FrameEnd
					: EPaper2DPlusCueTriggerEdge::FrameStart;
			if (CandidateEdge != Moment->TriggerEdge)
			{
				OnEventTriggerEdgeChanged.ExecuteIfBound(DragEventIndex, CandidateEdge);
				Invalidate(EInvalidateWidgetReason::Paint);
			}
		}
		return FReply::Handled();
	}

	if (bResizingEvent && DragEventIndex != INDEX_NONE)
	{
		OnEdgeAutoscrollRequested.ExecuteIfBound(
			Paper2DPlusFrameCueTimeline::EDragAxis::Horizontal,
			MouseEvent.GetScreenSpacePosition());
		float DeltaX = LocalPos.X - DragStartX;
		int32 FrameDelta = FMath::RoundToInt32(DeltaX / ColumnWidth);
		int32 NewCount = FMath::Max(1, DragStartFrameCount + FrameDelta);
		const int32 CharacterFrameCount = GetCharacterFrameCount();
		if (CharacterFrameCount > 0)
		{
			if (const TArray<TObjectPtr<UPaper2DPlusCueBase>>* CurrentCues = ResolveCues())
			{
				if (CurrentCues->IsValidIndex(DragEventIndex) && (*CurrentCues)[DragEventIndex])
				{
					const int32 StartFrame = FMath::Clamp(
						(*CurrentCues)[DragEventIndex]->GetPrimaryAnchorFrame(), 0, CharacterFrameCount - 1);
					NewCount = FMath::Min(NewCount, FMath::Max(1, CharacterFrameCount - StartFrame));
				}
			}
		}

		if (const TArray<TObjectPtr<UPaper2DPlusCueBase>>* CurrentCues = ResolveCues())
		{
			if (CurrentCues->IsValidIndex(DragEventIndex) && (*CurrentCues)[DragEventIndex]
				&& (*CurrentCues)[DragEventIndex]->GetCueFrameCount() == NewCount)
			{
				return FReply::Handled();
			}
		}
		OnEventDurationChanged.ExecuteIfBound(DragEventIndex, NewCount);
		Invalidate(EInvalidateWidgetReason::Paint);
		return FReply::Handled();
	}

	if (bDraggingEvent && DragEventIndex != INDEX_NONE)
	{
		OnEdgeAutoscrollRequested.ExecuteIfBound(
			Paper2DPlusFrameCueTimeline::EDragAxis::Horizontal,
			MouseEvent.GetScreenSpacePosition());
		float DeltaX = LocalPos.X - DragStartX;
		int32 FrameDelta = FMath::RoundToInt32(DeltaX / ColumnWidth);
		int32 NewFrame = FMath::Max(0, DragStartFrame + FrameDelta);

		// Clamp to definitive character key-frame bounds. Moving a Range preserves its duration rather
		// than silently shrinking it at the right edge; resize is the only gesture that changes length.
		const int32 CharacterFrameCount = GetCharacterFrameCount();
		int32 MaxFrame = FMath::Max(0, CharacterFrameCount - 1);
		if (CharacterFrameCount > 0)
		{
			if (const TArray<TObjectPtr<UPaper2DPlusCueBase>>* CurrentCues = ResolveCues())
			{
				if (CurrentCues->IsValidIndex(DragEventIndex) && (*CurrentCues)[DragEventIndex]
					&& (*CurrentCues)[DragEventIndex]->IsRangeCue())
				{
					MaxFrame = FMath::Max(
						0, CharacterFrameCount - (*CurrentCues)[DragEventIndex]->GetCueFrameCount());
				}
			}
		}
		NewFrame = FMath::Min(NewFrame, MaxFrame);

		if (const TArray<TObjectPtr<UPaper2DPlusCueBase>>* CurrentCues = ResolveCues())
		{
			if (CurrentCues->IsValidIndex(DragEventIndex) && (*CurrentCues)[DragEventIndex]
				&& (*CurrentCues)[DragEventIndex]->GetPrimaryAnchorFrame() == NewFrame)
			{
				return FReply::Handled();
			}
		}
		OnEventFrameChanged.ExecuteIfBound(DragEventIndex, NewFrame);
		Invalidate(EInvalidateWidgetReason::Paint);
		return FReply::Handled();
	}

	// Hover tracking
	int32 NewHovered = HitTestEventBar(MyGeometry, LocalPos);
	TWeakObjectPtr<UPaper2DPlusCueBase> NewHoveredIdentity;
	bool bNewHoverResize = false;
	if (NewHovered != INDEX_NONE)
	{
		if (const TArray<TObjectPtr<UPaper2DPlusCueBase>>* CurrentCues = ResolveCues())
		{
			if (CurrentCues->IsValidIndex(NewHovered)) NewHoveredIdentity = (*CurrentCues)[NewHovered];
		}
		bNewHoverResize = HitTestResizeHandle(MyGeometry, LocalPos, NewHovered);
	}
	if (NewHoveredIdentity != HoveredCueIdentity || bNewHoverResize != bHoveringResizeHandle)
	{
		HoveredCueIdentity = NewHoveredIdentity;
		bHoveringResizeHandle = bNewHoverResize;
		Invalidate(EInvalidateWidgetReason::Paint);
	}

	return FReply::Unhandled();
}

FReply SFrameEventTimelineTrack::OnKeyDown(
	const FGeometry& MyGeometry,
	const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	const int32 SelectedIndex = SelectedEventIndex.Get(INDEX_NONE);
	const TArray<TObjectPtr<UPaper2DPlusCueBase>>* SourceCues = ResolveCues();
	if (!SourceCues)
	{
		return FReply::Unhandled();
	}
	const TArray<TObjectPtr<UPaper2DPlusCueBase>>& Cues = *SourceCues;
	if (!Cues.IsValidIndex(SelectedIndex) || !IsValid(Cues[SelectedIndex]))
	{
		return FReply::Unhandled();
	}
	const TWeakObjectPtr<UPaper2DPlusCueBase> CueIdentity = Cues[SelectedIndex];

	if (InKeyEvent.IsControlDown() && Key == EKeys::D && OnEventDuplicated.IsBound())
	{
		OnEventDuplicated.Execute(SelectedIndex);
		return FReply::Handled();
	}
	if (InKeyEvent.IsAltDown() && !InKeyEvent.IsControlDown() && !InKeyEvent.IsCommandDown()
		&& (Key == EKeys::Up || Key == EKeys::Down) && DataProvider.IsValid())
	{
		UPaper2DPlusCueBase* Cue = CueIdentity.Get();
		const FProfileScopedAnimationIdentity Scope = DataProvider->GetScopedAnimationIdentity(
			SelectedFlipbookIndex.Get(INDEX_NONE));
		if (!IsValid(Cue) || !Scope.IsValid())
		{
			return FReply::Handled();
		}
		TArray<FGuid> TrackIds;
		TrackIds.Add(FGuid());
		for (const FPaper2DPlusFrameCueTrackDefinition& Track : DataProvider->GetOptionalTracks(Scope))
		{
			TrackIds.Add(Track.TrackId);
		}
		const FGuid CurrentTrackId = DataProvider->ResolveCueTrackId(Scope, Cue);
		const int32 CurrentTrackIndex = FMath::Max(0, TrackIds.IndexOfByKey(CurrentTrackId));
		const int32 TargetTrackIndex = FMath::Clamp(
			CurrentTrackIndex + (Key == EKeys::Down ? 1 : -1),
			0,
			TrackIds.Num() - 1);
		if (TargetTrackIndex != CurrentTrackIndex)
		{
			MoveContextCueToTrack(CueIdentity, TrackIds[TargetTrackIndex]);
		}
		return FReply::Handled();
	}
	if (InKeyEvent.IsControlDown() || InKeyEvent.IsAltDown() || InKeyEvent.IsCommandDown())
	{
		return FReply::Unhandled();
	}
	if ((Key == EKeys::Delete || Key == EKeys::BackSpace) && OnEventRemoved.IsBound())
	{
		OnEventRemoved.Execute(SelectedIndex);
		return FReply::Handled();
	}
	if (Key != EKeys::Left && Key != EKeys::Right)
	{
		return FReply::Unhandled();
	}

	const int32 Delta = Key == EKeys::Right ? 1 : -1;
	UPaper2DPlusCueBase* Cue = CueIdentity.Get();
	const int32 CharacterFrameCount = GetCharacterFrameCount();
	if (!IsValid(Cue) || CharacterFrameCount <= 0
		|| !OnEventDragStarted.IsBound() || !OnEventDragEnded.IsBound())
	{
		return FReply::Unhandled();
	}

	const bool bResize = InKeyEvent.IsShiftDown() && Cue->IsRangeCue();
	if (bResize)
	{
		const int32 MaxCount = FMath::Max(
			1, CharacterFrameCount - FMath::Clamp(Cue->GetPrimaryAnchorFrame(), 0, CharacterFrameCount - 1));
		const int32 NewCount = FMath::Clamp(Cue->GetCueFrameCount() + Delta, 1, MaxCount);
		if (NewCount == Cue->GetCueFrameCount() || !OnEventDurationChanged.IsBound())
		{
			return FReply::Handled();
		}
	}
	else
	{
		const int32 Duration = Cue->IsRangeCue() ? Cue->GetCueFrameCount() : 1;
		const int32 MaxFrame = FMath::Max(0, CharacterFrameCount - Duration);
		const int32 NewFrame = FMath::Clamp(Cue->GetPrimaryAnchorFrame() + Delta, 0, MaxFrame);
		if (NewFrame == Cue->GetPrimaryAnchorFrame() || !OnEventFrameChanged.IsBound())
		{
			return FReply::Handled();
		}
	}

	// Parent owns the transaction and preview teardown. Re-resolve after that creator-extensible
	// callback before applying this one keyboard edit, then pair the transaction on every path.
	OnEventDragStarted.Execute(SelectedIndex);
	int32 CurrentIndex = ResolveCurrentEventIndex(CueIdentity);
	if (CurrentIndex == INDEX_NONE)
	{
		OnEventDragEnded.Execute();
		return FReply::Handled();
	}
	SourceCues = ResolveCues();
	if (!SourceCues)
	{
		OnEventDragEnded.Execute();
		return FReply::Handled();
	}
	const TArray<TObjectPtr<UPaper2DPlusCueBase>>& CurrentCues = *SourceCues;
	Cue = CurrentCues.IsValidIndex(CurrentIndex) ? CurrentCues[CurrentIndex].Get() : nullptr;
	if (!IsValid(Cue))
	{
		OnEventDragEnded.Execute();
		return FReply::Handled();
	}

	if (bResize)
	{
		const int32 MaxCount = FMath::Max(
			1, CharacterFrameCount - FMath::Clamp(Cue->GetPrimaryAnchorFrame(), 0, CharacterFrameCount - 1));
		const int32 NewCount = FMath::Clamp(Cue->GetCueFrameCount() + Delta, 1, MaxCount);
		if (NewCount != Cue->GetCueFrameCount())
		{
			OnEventDurationChanged.Execute(CurrentIndex, NewCount);
		}
	}
	else
	{
		const int32 Duration = Cue->IsRangeCue() ? Cue->GetCueFrameCount() : 1;
		const int32 MaxFrame = FMath::Max(0, CharacterFrameCount - Duration);
		const int32 NewFrame = FMath::Clamp(Cue->GetPrimaryAnchorFrame() + Delta, 0, MaxFrame);
		if (NewFrame != Cue->GetPrimaryAnchorFrame())
		{
			OnEventFrameChanged.Execute(CurrentIndex, NewFrame);
		}
	}
	OnEventDragEnded.Execute();
	Invalidate(EInvalidateWidgetReason::Paint);
	return FReply::Handled();
}

void SFrameEventTimelineTrack::OnMouseLeave(const FPointerEvent& MouseEvent)
{
	if (HoveredCueIdentity.IsValid() || bHoveringResizeHandle)
	{
		HoveredCueIdentity.Reset();
		bHoveringResizeHandle = false;
		Invalidate(EInvalidateWidgetReason::Paint);
	}
	SLeafWidget::OnMouseLeave(MouseEvent);
}

void SFrameEventTimelineTrack::OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent)
{
	const bool bStartedTransaction = bDraggingEvent || bResizingEvent || bEditingEdge;
	if (bDragArmed || bStartedTransaction || bReassigningTrack)
	{
		ResetDragState();
		if (bStartedTransaction)
		{
			OnEventDragEnded.ExecuteIfBound();
		}
	}
	SLeafWidget::OnMouseCaptureLost(CaptureLostEvent);
}

void SFrameEventTimelineTrack::CancelActiveInteraction()
{
	const bool bStartedTransaction = bDraggingEvent || bResizingEvent || bEditingEdge;
	const bool bHadInteraction = bDragArmed || bStartedTransaction || bReassigningTrack;
	if (!bHadInteraction)
	{
		return;
	}

	// Reset before releasing capture. ReleaseAllPointerCapture synchronously reaches
	// OnMouseCaptureLost on some Slate paths; the cleared flags make that callback a no-op so the
	// parent-owned transaction end delegate remains exactly-once.
	ResetDragState();
	if (HasMouseCapture() && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().ReleaseAllPointerCapture();
	}
	if (bStartedTransaction)
	{
		OnEventDragEnded.ExecuteIfBound();
	}
}

void SFrameEventTimelineTrack::BeginActiveInteractionForTests(
	UPaper2DPlusCueBase* Cue,
	bool bResize)
{
	CancelActiveInteraction();
	const TArray<TObjectPtr<UPaper2DPlusCueBase>>* CurrentCues = ResolveCues();
	const int32 CueIndex = CurrentCues
		? CurrentCues->IndexOfByPredicate(
			[Cue](const TObjectPtr<UPaper2DPlusCueBase>& Candidate)
			{
				return Candidate.Get() == Cue;
			})
		: INDEX_NONE;
	if (!IsValid(Cue) || CueIndex == INDEX_NONE)
	{
		return;
	}
	DragEventIndex = CueIndex;
	DragCueIdentity = Cue;
	bResizingEvent = bResize;
	bDraggingEvent = !bResize;
	OnEventDragStarted.ExecuteIfBound(CueIndex);
}

void SFrameEventTimelineTrack::BeginEdgeInteractionForTests(UPaper2DPlusCueBase* Cue)
{
	CancelActiveInteraction();
	const TArray<TObjectPtr<UPaper2DPlusCueBase>>* CurrentCues = ResolveCues();
	const int32 CueIndex = CurrentCues
		? CurrentCues->IndexOfByPredicate(
			[Cue](const TObjectPtr<UPaper2DPlusCueBase>& Candidate)
			{
				return Candidate.Get() == Cue;
			})
		: INDEX_NONE;
	if (!IsValid(Cue) || CueIndex == INDEX_NONE)
	{
		return;
	}
	DragEventIndex = CueIndex;
	DragCueIdentity = Cue;
	bEditingEdge = true;
	OnEventDragStarted.ExecuteIfBound(CueIndex);
}

void SFrameEventTimelineTrack::EmitEdgeChangeForTests(EPaper2DPlusCueTriggerEdge NewEdge)
{
	if (bEditingEdge && DragEventIndex != INDEX_NONE)
	{
		OnEventTriggerEdgeChanged.ExecuteIfBound(DragEventIndex, NewEdge);
	}
}

FCursorReply SFrameEventTimelineTrack::OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const
{
	if (bDragArmed)
	{
		return (bArmedForResize || bArmedForEdge)
			? FCursorReply::Cursor(EMouseCursor::ResizeLeftRight)
			: FCursorReply::Cursor(EMouseCursor::GrabHandClosed);
	}
	if (bResizingEvent || bEditingEdge) return FCursorReply::Cursor(EMouseCursor::ResizeLeftRight);
	if (bReassigningTrack) return FCursorReply::Cursor(EMouseCursor::GrabHandClosed);
	if (bDraggingEvent) return FCursorReply::Cursor(EMouseCursor::GrabHandClosed);
	if (ResolveCurrentEventIndex(HoveredCueIdentity) != INDEX_NONE)
	{
		if (bHoveringResizeHandle) return FCursorReply::Cursor(EMouseCursor::ResizeLeftRight);
		return FCursorReply::Cursor(EMouseCursor::GrabHand);
	}
	return FCursorReply::Unhandled();
}

void SFrameEventTimelineTrack::ResetDragState()
{
	const bool bHadPaintedTrackTarget = bReassigningTrack || bHasPendingTrackTarget;
	bDragArmed = false;
	bArmedForResize = false;
	bArmedForEdge = false;
	bDraggingEvent = false;
	bResizingEvent = false;
	bEditingEdge = false;
	bReassigningTrack = false;
	bHasPendingTrackTarget = false;
	PendingTrackId.Invalidate();
	DragEventIndex = INDEX_NONE;
	DragCueIdentity.Reset();
	DragStartFrame = 0;
	DragStartFrameCount = 1;
	DragStartX = 0.0f;
	DragStartScreenPos = FVector2D::ZeroVector;
	if (bHadPaintedTrackTarget)
	{
		Invalidate(EInvalidateWidgetReason::Paint);
	}
}

// --- Helpers ----------------------------------------------------------------

FText SFrameEventTimelineTrack::GetAccessibleSummaryText() const
{
	return GetAccessibleSummaryTextForCue(SelectedEventIndex.Get(INDEX_NONE));
}

FText SFrameEventTimelineTrack::GetAccessibleSummaryTextForCue(const int32 CueIndex) const
{
	const TArray<TObjectPtr<UPaper2DPlusCueBase>>* CurrentCues = ResolveCues();
	if (!CurrentCues)
	{
		return LOCTEXT(
			"TimelineAccessibleNoAnimation",
			"Frame Cue timeline. No animation selected.");
	}
	const UPaper2DPlusCueBase* Cue = CurrentCues->IsValidIndex(CueIndex)
		? (*CurrentCues)[CueIndex].Get()
		: nullptr;
	if (!Cue)
	{
		return LOCTEXT(
			"TimelineAccessibleNoCue",
			"Frame Cue timeline. Select a Cue row. Left and Right move it; Shift plus Left or Right resizes a Cue State; Alt plus Up or Down changes its track.");
	}
	// The preview-error badge is a colored marker on the bar, so the screen-reader summary has to carry
	// the same information in words without replacing the placement's timing identity.
	const Paper2DPlusFrameCuePreviewBehavior::FPlacementBadge Badge =
		Paper2DPlusFrameCuePreviewBehavior::GetPlacementBadge(Cue);
	return Paper2DPlusFrameCueTimelinePresentation::BuildAccessibleSummary(
		*Cue,
		Badge.bHasError ? Badge.ToolTip : FText::GetEmpty());
}

int32 SFrameEventTimelineTrack::GetCharacterFrameCount() const
{
	if (CharacterFrameCountAttribute.IsBound())
	{
		return FMath::Max(0, CharacterFrameCountAttribute.Get(0));
	}
	if (!IsValid(ResolveAsset())) return 0;
	int32 FBIdx = SelectedFlipbookIndex.Get(INDEX_NONE);
	if (!ResolveAsset()->Flipbooks.IsValidIndex(FBIdx)) return 0;
	UPaperFlipbook* FB = ResolveAsset()->Flipbooks[FBIdx].Identity.Flipbook.Get();
	return FB ? FB->GetNumKeyFrames() : 0;
}

int32 SFrameEventTimelineTrack::GetTotalColumns() const
{
	return GetCharacterFrameCount();
}

TArray<Paper2DPlusFrameCueTimeline::FPackedTrack>
SFrameEventTimelineTrack::BuildPackedTracks() const
{
	using namespace Paper2DPlusFrameCueTimeline;
	TArray<FPlacement> Placements;
	TArray<FGuid> OrderedOptionalTracks;
	const TArray<TObjectPtr<UPaper2DPlusCueBase>>* Cues = ResolveCues();
	const int32 FlipbookIndex = SelectedFlipbookIndex.Get(INDEX_NONE);
	const FProfileScopedAnimationIdentity Scope = DataProvider.IsValid()
		? DataProvider->GetScopedAnimationIdentity(FlipbookIndex)
		: FProfileScopedAnimationIdentity();
	if (DataProvider.IsValid() && Scope.IsValid())
	{
		for (const FPaper2DPlusFrameCueTrackDefinition& Track :
			DataProvider->GetOptionalTracks(Scope))
		{
			OrderedOptionalTracks.Add(Track.TrackId);
		}
	}
	if (Cues)
	{
		Placements.Reserve(Cues->Num());
		for (int32 CueIndex = 0; CueIndex < Cues->Num(); ++CueIndex)
		{
			const UPaper2DPlusCueBase* Cue = (*Cues)[CueIndex];
			if (!IsValid(Cue))
			{
				continue;
			}
			int32 StartFrame = 0;
			int32 FrameCount = 1;
			GetEventFrameRange(Cue, StartFrame, FrameCount);
			FPlacement& Placement = Placements.AddDefaulted_GetRef();
			Placement.CueIndex = CueIndex;
			Placement.TrackId = DataProvider.IsValid() && Scope.IsValid()
				? DataProvider->ResolveCueTrackId(Scope, Cue)
				: FGuid();
			Placement.StartFrame = StartFrame;
			Placement.FrameCount = FrameCount;
		}
	}
	return PackPlacements(Placements, OrderedOptionalTracks);
}

TArray<Paper2DPlusFrameCueTimeline::FPackedTrack>
SFrameEventTimelineTrack::GetPackedTracksForPresentation() const
{
	return BuildPackedTracks();
}

float SFrameEventTimelineTrack::GetPackedTrackHeightForPresentation(
	const Paper2DPlusFrameCueTimeline::FPackedTrack& Track) const
{
	return GetPackedTrackHeight(Track);
}

float SFrameEventTimelineTrack::GetPackedTrackHeight(
	const Paper2DPlusFrameCueTimeline::FPackedTrack& Track) const
{
	return FMath::Max(1, Track.SublaneCount) * EventTrackHeight
		+ TrackPadding;
}

float SFrameEventTimelineTrack::GetTotalEventsHeight() const
{
	float H = 0.0f;
	for (const Paper2DPlusFrameCueTimeline::FPackedTrack& Track : BuildPackedTracks())
	{
		H += GetPackedTrackHeight(Track);
	}
	return FMath::Max(H, EmptyTrackHeight);
}

int32 SFrameEventTimelineTrack::FrameFromX(const float LocalX) const
{
	const float ContentX = LocalX - LabelWidth;
	int32 FrameIndex = INDEX_NONE;
	return Paper2DPlusFrameCueTimeline::FTimingGeometry::TryResolveFrameAtX(
		GetCharacterFrameCount() > 0,
		GetCharacterFrameCount(),
		ContentX,
		FrameIndex)
		? FrameIndex
		: INDEX_NONE;
}

void SFrameEventTimelineTrack::GetEventFrameRange(const UPaper2DPlusCueBase* Event, int32& OutStart, int32& OutCount)
{
	if (const UPaper2DPlusCueState* Ranged = Cast<UPaper2DPlusCueState>(Event))
	{
		OutStart = Ranged->StartFrame;
		OutCount = FMath::Max(1, Ranged->FrameCount);
	}
	else if (const UPaper2DPlusCue* OneShot = Cast<UPaper2DPlusCue>(Event))
	{
		OutStart = OneShot->TriggerFrame;
		OutCount = 1;
	}
	else
	{
		OutStart = 0;
		OutCount = 1;
	}
}

int32 SFrameEventTimelineTrack::HitTestEventBar(const FGeometry& /*Geom*/, const FVector2D& LocalPos) const
{
	const TArray<TObjectPtr<UPaper2DPlusCueBase>>* CurrentCues = ResolveCues();
	if (!CurrentCues) return INDEX_NONE;
	const TArray<TObjectPtr<UPaper2DPlusCueBase>>& Events = *CurrentCues;

	float TrackTop = 0.0f;
	for (const Paper2DPlusFrameCueTimeline::FPackedTrack& Track : BuildPackedTracks())
	{
		const float SublaneHeight = EventTrackHeight;
		for (const Paper2DPlusFrameCueTimeline::FPackedPlacement& Placement : Track.Placements)
		{
			if (!Events.IsValidIndex(Placement.CueIndex) || !IsValid(Events[Placement.CueIndex]))
			{
				continue;
			}
			const float BarX = Placement.StartFrame * ColumnWidth;
			const float BarW = FMath::Max(
				4.0f,
				(Placement.EndFrameInclusive - Placement.StartFrame + 1) * ColumnWidth);
			const float BarY = TrackTop + Placement.Sublane * SublaneHeight + 2.0f;
			const float BarHeight = FMath::Max(4.0f, SublaneHeight - 4.0f);
			if (LocalPos.X >= BarX && LocalPos.X <= BarX + BarW
				&& LocalPos.Y >= BarY && LocalPos.Y <= BarY + BarHeight)
			{
				return Placement.CueIndex;
			}
		}
		TrackTop += GetPackedTrackHeight(Track);
	}

	return INDEX_NONE;
}

bool SFrameEventTimelineTrack::HitTestResizeHandle(const FGeometry& /*Geom*/, const FVector2D& LocalPos, int32 EventIdx) const
{
	const TArray<TObjectPtr<UPaper2DPlusCueBase>>* CurrentCues = ResolveCues();
	if (!CurrentCues) return false;
	const TArray<TObjectPtr<UPaper2DPlusCueBase>>& Events = *CurrentCues;
	if (!Events.IsValidIndex(EventIdx) || !Events[EventIdx]) return false;

	// Every Cue State has a resize handle, including a single-frame range.
	if (!Events[EventIdx]->IsRangeCue()) return false;
	int32 Start = 0;
	int32 Count = 1;
	GetEventFrameRange(Events[EventIdx], Start, Count);

	float BarX = Start * ColumnWidth;
	float BarW = (float)Count * ColumnWidth;
	float RightEdge = BarX + BarW;

	// Hit zone: ResizeHandleWidth pixels from the right edge
	return LocalPos.X >= (RightEdge - ResizeHandleWidth - 2.0f) && LocalPos.X <= RightEdge + 2.0f;
}

bool SFrameEventTimelineTrack::HitTestEdgeDiamond(
	const FGeometry& /*Geom*/,
	const FVector2D& LocalPos,
	int32 EventIdx) const
{
	const TArray<TObjectPtr<UPaper2DPlusCueBase>>* CurrentCues = ResolveCues();
	if (!CurrentCues) return false;
	const TArray<TObjectPtr<UPaper2DPlusCueBase>>& Events = *CurrentCues;
	if (!Events.IsValidIndex(EventIdx) || !Events[EventIdx]) return false;

	// Moment-only: the diamond is the trigger-edge handle, and only UPaper2DPlusCue carries one.
	const UPaper2DPlusCue* Moment = Cast<UPaper2DPlusCue>(Events[EventIdx].Get());
	if (!Moment) return false;

	int32 Start = 0;
	int32 Count = 1;
	GetEventFrameRange(Events[EventIdx], Start, Count);
	const float BarX = Start * ColumnWidth;
	// X-only zone like the resize handle: a comfortable grab band hugging whichever cell edge the
	// diamond currently sits on, with the same 2px slop.
	const float DiamondBand = 10.0f;
	if (Moment->TriggerEdge == EPaper2DPlusCueTriggerEdge::FrameEnd)
	{
		const float RightEdge = BarX + ColumnWidth;
		return LocalPos.X >= (RightEdge - DiamondBand - 2.0f)
			&& LocalPos.X <= RightEdge + 2.0f;
	}
	return LocalPos.X >= BarX - 2.0f
		&& LocalPos.X <= BarX + DiamondBand + 2.0f;
}

FGuid SFrameEventTimelineTrack::TrackIdFromY(const float LocalY) const
{
	const TArray<TObjectPtr<UPaper2DPlusCueBase>>* CurrentCues = ResolveCues();
	const TArray<Paper2DPlusFrameCueTimeline::FPackedTrack> Tracks = BuildPackedTracks();
	if (Tracks.IsEmpty())
	{
		return FGuid();
	}
	static const TArray<TObjectPtr<UPaper2DPlusCueBase>> EmptyCues;
	const TArray<TObjectPtr<UPaper2DPlusCueBase>>& Cues = CurrentCues
		? *CurrentCues
		: EmptyCues;
	float TrackTop = 0.0f;
	for (const Paper2DPlusFrameCueTimeline::FPackedTrack& Track : Tracks)
	{
		const float TrackBottom = TrackTop + GetPackedTrackHeight(Track);
		if (LocalY < TrackBottom)
		{
			return Track.TrackId;
		}
		TrackTop = TrackBottom;
	}
	return Tracks.Last().TrackId;
}

bool SFrameEventTimelineTrack::ResolveAddCueContextTarget(
	const FVector2D LocalPosition,
	int32& OutFrameIndex,
	FGuid& OutTrackId) const
{
	OutFrameIndex = INDEX_NONE;
	OutTrackId.Invalidate();
	OutFrameIndex = FrameFromX(LocalPosition.X);
	if (OutFrameIndex == INDEX_NONE)
	{
		return false;
	}
	OutTrackId = TrackIdFromY(LocalPosition.Y);
	return true;
}

void SFrameEventTimelineTrack::ShowTrackContextMenu(
	const FPointerEvent& MouseEvent,
	const int32 FrameIndex,
	const FGuid TrackId)
{
	const TWeakPtr<SFrameEventTimelineTrack> WeakTrack = SharedThis(this);
	FMenuBuilder MenuBuilder(true, nullptr);
	MenuBuilder.BeginSection("TrackCueActions", LOCTEXT("TrackCueActionsSection", "Cue Track"));
	MenuBuilder.AddMenuEntry(
		LOCTEXT("AddCueFromTrackContext", "Add Cue"),
		FText::Format(
			LOCTEXT(
				"AddCueFromTrackContextTip",
				"Place a reusable Cue Type at key frame {0} on this track."),
			FText::AsNumber(FrameIndex + 1)),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([WeakTrack, FrameIndex, TrackId]()
			{
				if (const TSharedPtr<SFrameEventTimelineTrack> Track = WeakTrack.Pin())
				{
					Track->OnAddCueRequested.ExecuteIfBound(FrameIndex, TrackId);
				}
			}),
			FCanExecuteAction::CreateLambda([WeakTrack]()
			{
				const TSharedPtr<SFrameEventTimelineTrack> Track = WeakTrack.Pin();
				return Track.IsValid() && Track->OnAddCueRequested.IsBound();
			})));
	MenuBuilder.EndSection();

	FWidgetPath WidgetPath;
	FSlateApplication::Get().GeneratePathToWidgetUnchecked(SharedThis(this), WidgetPath);
	FSlateApplication::Get().PushMenu(
		SharedThis(this),
		WidgetPath,
		MenuBuilder.MakeWidget(),
		MouseEvent.GetScreenSpacePosition(),
		FPopupTransitionEffect::ContextMenu);
}

void SFrameEventTimelineTrack::ShowEventContextMenu(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent, int32 EventIdx)
{
	const TArray<TObjectPtr<UPaper2DPlusCueBase>>* CurrentCues = ResolveCues();
	if (!CurrentCues) return;
	const TArray<TObjectPtr<UPaper2DPlusCueBase>>& Events = *CurrentCues;
	if (!Events.IsValidIndex(EventIdx) || !Events[EventIdx]) return;

	UPaper2DPlusCueBase* Event = Events[EventIdx];
	const TWeakObjectPtr<UPaper2DPlusCueBase> EventIdentity(Event);
	int32 Start = 0;
	int32 Count = 1;
	GetEventFrameRange(Event, Start, Count);

	FMenuBuilder MenuBuilder(true, nullptr);
	MenuBuilder.BeginSection("CueActions", LOCTEXT("CueActionsSection", "Frame Cue"));

	// Show event info
	FString EventName = Event->DebugName.IsNone()
		? Event->GetClass()->GetDisplayNameText().ToString()
		: Event->DebugName.ToString();
	MenuBuilder.AddMenuEntry(
		FText::FromString(EventName),
		FText::GetEmpty(),
		FSlateIcon(),
		FUIAction(),
		NAME_None,
		EUserInterfaceActionType::None);

	MenuBuilder.AddMenuSeparator();

	// Set Frame
	MenuBuilder.AddMenuEntry(
		FText::Format(LOCTEXT("SetFrame", "Frame: {0}"), FText::AsNumber(Start)),
		LOCTEXT("SetFrameTooltip", "Definitive key frame anchoring this cue. Drag the cue on the timeline to reposition it."),
		FSlateIcon(),
		FUIAction(),
		NAME_None,
		EUserInterfaceActionType::None);

	// Duration (only for Cue States)
	if (Event->IsRangeCue())
	{
		MenuBuilder.AddMenuEntry(
			FText::Format(LOCTEXT("SetDuration", "Duration: {0} frames"), FText::AsNumber(Count)),
			LOCTEXT("SetDurationTooltip", "Inclusive key-frame span for this Cue State. It emits Begin on entry, optional Update while active, and End on exit."),
			FSlateIcon(),
			FUIAction(),
			NAME_None,
			EUserInterfaceActionType::None);
	}

	MenuBuilder.AddMenuSeparator();
	MenuBuilder.AddSubMenu(
		LOCTEXT("MoveCueToTrack", "Move to Track"),
		LOCTEXT(
			"MoveCueToTrackTooltip",
			"Change only this Cue's organizational track. Timing, payload, and dispatch order stay unchanged. Alt+Up/Down also moves the selected Cue."),
		FNewMenuDelegate::CreateSP(
			this, &SFrameEventTimelineTrack::BuildMoveToTrackMenu, EventIdentity));

	MenuBuilder.AddMenuSeparator();

	MenuBuilder.AddMenuEntry(
		LOCTEXT("DuplicateCue", "Duplicate Cue"),
		LOCTEXT("DuplicateCueTooltip", "Create an independent placement with the same cue class, timing, and overrides (Ctrl+D)."),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateSP(
			this, &SFrameEventTimelineTrack::DuplicateContextCue, EventIdentity)));

	// Delete
	MenuBuilder.AddMenuEntry(
		LOCTEXT("DeleteCue", "Delete Cue"),
		LOCTEXT("DeleteCueTooltip", "Remove this cue placement"),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateSP(
			this, &SFrameEventTimelineTrack::RemoveContextCue, EventIdentity)));

	MenuBuilder.EndSection();

	FWidgetPath WidgetPath;
	FSlateApplication::Get().GeneratePathToWidgetUnchecked(SharedThis(this), WidgetPath);
	FSlateApplication::Get().PushMenu(
		SharedThis(this), WidgetPath, MenuBuilder.MakeWidget(),
		MouseEvent.GetScreenSpacePosition(), FPopupTransitionEffect::ContextMenu);
}

void SFrameEventTimelineTrack::BuildMoveToTrackMenu(
	FMenuBuilder& MenuBuilder,
	const TWeakObjectPtr<UPaper2DPlusCueBase> Cue)
{
	UPaper2DPlusCueBase* CueObject = Cue.Get();
	const int32 FlipbookIndex = SelectedFlipbookIndex.Get(INDEX_NONE);
	const FProfileScopedAnimationIdentity Scope = DataProvider.IsValid()
		? DataProvider->GetScopedAnimationIdentity(FlipbookIndex)
		: FProfileScopedAnimationIdentity();
	if (!DataProvider.IsValid() || !Scope.IsValid() || !IsValid(CueObject))
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("MoveCueTrackUnavailable", "Track targets unavailable"),
			FText::GetEmpty(),
			FSlateIcon(),
			FUIAction());
		return;
	}
	const TWeakPtr<SFrameEventTimelineTrack> WeakSelf = SharedThis(this);
	auto AddTarget = [&](const FText& Label, const FGuid TrackId)
	{
		MenuBuilder.AddMenuEntry(
			Label,
			FText::GetEmpty(),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([WeakSelf, Cue, TrackId]()
				{
					if (const TSharedPtr<SFrameEventTimelineTrack> Track = WeakSelf.Pin())
					{
						Track->MoveContextCueToTrack(Cue, TrackId);
					}
				}),
				FCanExecuteAction(),
				FIsActionChecked::CreateLambda([WeakSelf, Cue, TrackId]()
				{
					const TSharedPtr<SFrameEventTimelineTrack> Track = WeakSelf.Pin();
					UPaper2DPlusCueBase* CurrentCue = Cue.Get();
					if (!Track.IsValid() || !Track->DataProvider.IsValid() || !IsValid(CurrentCue))
					{
						return false;
					}
					const FProfileScopedAnimationIdentity CurrentScope =
						Track->DataProvider->GetScopedAnimationIdentity(
							Track->SelectedFlipbookIndex.Get(INDEX_NONE));
					return CurrentScope.IsValid()
						&& Track->DataProvider->ResolveCueTrackId(CurrentScope, CurrentCue) == TrackId;
				})),
			NAME_None,
			EUserInterfaceActionType::RadioButton);
	};
	AddTarget(LOCTEXT("MoveCueDefaultTrack", "Default"), FGuid());
	for (const FPaper2DPlusFrameCueTrackDefinition& Track : DataProvider->GetOptionalTracks(Scope))
	{
		AddTarget(FText::FromString(Track.DisplayName), Track.TrackId);
	}
}

void SFrameEventTimelineTrack::MoveContextCueToTrack(
	const TWeakObjectPtr<UPaper2DPlusCueBase> Cue,
	const FGuid TrackId)
{
	UPaper2DPlusCueBase* CueObject = Cue.Get();
	if (!DataProvider.IsValid() || !IsValid(CueObject))
	{
		return;
	}
	const FFrameCueStableIdentity CueIdentity = DataProvider->GetCueIdentity(
		SelectedFlipbookIndex.Get(INDEX_NONE), CueObject);
	if (!CueIdentity.IsValid()
		|| DataProvider->ResolveCueTrackId(CueIdentity.Scope, CueObject) == TrackId
		|| !DataProvider->AssignCueToTrack(CueIdentity, TrackId))
	{
		return;
	}
	const int32 CurrentIndex = DataProvider->ResolveCueIndex(CueIdentity);
	OnEventSelected.ExecuteIfBound(CurrentIndex);
	OnEventTrackChanged.ExecuteIfBound(CurrentIndex);
	Invalidate(EInvalidateWidgetReason::LayoutAndVolatility);
}

int32 SFrameEventTimelineTrack::ResolveCurrentEventIndex(
	TWeakObjectPtr<UPaper2DPlusCueBase> Cue) const
{
	UPaper2DPlusCueBase* CuePtr = Cue.Get();
	const TArray<TObjectPtr<UPaper2DPlusCueBase>>* CurrentCues = ResolveCues();
	if (!CurrentCues || !IsValid(CuePtr))
	{
		return INDEX_NONE;
	}
	return CurrentCues->IndexOfByPredicate(
		[CuePtr](const TObjectPtr<UPaper2DPlusCueBase>& Candidate)
		{
			return Candidate.Get() == CuePtr;
		});
}

const TArray<TObjectPtr<UPaper2DPlusCueBase>>* SFrameEventTimelineTrack::ResolveCues() const
{
	if (const TArray<TObjectPtr<UPaper2DPlusCueBase>>* ProviderCues =
		CueSource.Get(static_cast<const TArray<TObjectPtr<UPaper2DPlusCueBase>>*>(nullptr)))
	{
		return ProviderCues;
	}

	UPaper2DPlusCharacterProfileAsset* CurrentAsset = ResolveAsset();
	const int32 FlipbookIndex = SelectedFlipbookIndex.Get(INDEX_NONE);
	return IsValid(CurrentAsset) && CurrentAsset->Flipbooks.IsValidIndex(FlipbookIndex)
		? &CurrentAsset->Flipbooks[FlipbookIndex].FrameEventData.FrameCues
		: nullptr;
}

void SFrameEventTimelineTrack::DuplicateContextCue(TWeakObjectPtr<UPaper2DPlusCueBase> Cue)
{
	const int32 CurrentIndex = ResolveCurrentEventIndex(Cue);
	if (CurrentIndex != INDEX_NONE)
	{
		OnEventDuplicated.ExecuteIfBound(CurrentIndex);
	}
}

void SFrameEventTimelineTrack::RemoveContextCue(TWeakObjectPtr<UPaper2DPlusCueBase> Cue)
{
	const int32 CurrentIndex = ResolveCurrentEventIndex(Cue);
	if (CurrentIndex != INDEX_NONE)
	{
		OnEventRemoved.ExecuteIfBound(CurrentIndex);
	}
}

#undef LOCTEXT_NAMESPACE
