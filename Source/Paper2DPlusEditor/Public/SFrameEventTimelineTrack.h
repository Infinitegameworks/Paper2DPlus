// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SFrameCueTimeline.h"
#include "Widgets/SLeafWidget.h"

class UPaper2DPlusCharacterProfileAsset;
class UPaper2DPlusCueBase;
class FFrameCueDataProvider;
class FMenuBuilder;
enum class EPaper2DPlusCueTriggerEdge : uint8;

// Delegates used by the timeline track
DECLARE_DELEGATE_OneParam(FOnTimelineFrameClicked, int32);
DECLARE_DELEGATE_OneParam(FOnTimelineEventSelected, int32);
DECLARE_DELEGATE_OneParam(FOnTimelineEventDragStarted, int32); // EventIndex; once after drag threshold
DECLARE_DELEGATE(FOnTimelineEventDragEnded); // Paired only with a fired OnEventDragStarted
DECLARE_DELEGATE_TwoParams(FOnTimelineEventFrameChanged, int32, int32); // EventIndex, NewFrame
DECLARE_DELEGATE_TwoParams(FOnTimelineEventDurationChanged, int32, int32); // EventIndex, NewFrameCount
DECLARE_DELEGATE_TwoParams(
	FOnTimelineEventTriggerEdgeChanged,
	int32,
	EPaper2DPlusCueTriggerEdge); // EventIndex, edge the diamond was dropped on
DECLARE_DELEGATE_OneParam(FOnTimelineEventDuplicated, int32); // EventIndex
DECLARE_DELEGATE_OneParam(FOnTimelineEventRemoved, int32); // EventIndex
DECLARE_DELEGATE_OneParam(FOnTimelineEventTrackChanged, int32); // EventIndex
DECLARE_DELEGATE_TwoParams(FOnTimelineAddCueRequested, int32, FGuid); // FrameIndex, TrackId
DECLARE_DELEGATE_TwoParams(
	FOnTimelineEdgeAutoscrollRequested,
	Paper2DPlusFrameCueTimeline::EDragAxis,
	FVector2D); // Locked axis, screen-space pointer

/**
 * Timeline track widget for Frame Cue placements -- renders cue bars on a key-frame grid,
 * supports drag-to-move, drag-to-resize, duplicate/delete context actions, and
 * hover/selection highlighting.
 */
class SFrameEventTimelineTrack : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SFrameEventTimelineTrack) {}
		SLATE_ATTRIBUTE(TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset>, Asset)
		/** Provider supplies editor-only named-track membership. Null preserves legacy Default-only mode. */
		SLATE_ARGUMENT(TSharedPtr<FFrameCueDataProvider>, DataProvider)
		/** Live provider-backed cue source. Optional for legacy standalone tests/hosts. */
		SLATE_ATTRIBUTE(const TArray<TObjectPtr<UPaper2DPlusCueBase>>*, Cues)
		/** Definitive key-frame count from the active provider. */
		SLATE_ATTRIBUTE(int32, CharacterFrameCount)
		SLATE_ATTRIBUTE(int32, SelectedFlipbookIndex)
		SLATE_ATTRIBUTE(int32, SelectedEventIndex)
		SLATE_ATTRIBUTE(int32, SelectedFrameIndex)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SFrameEventTimelineTrack() override;

	// Delegates
	FOnTimelineFrameClicked OnFrameClicked;
	FOnTimelineEventSelected OnEventSelected;
	FOnTimelineEventDragStarted OnEventDragStarted;
	FOnTimelineEventDragEnded OnEventDragEnded;
	FOnTimelineEventFrameChanged OnEventFrameChanged;
	FOnTimelineEventDurationChanged OnEventDurationChanged;
	FOnTimelineEventTriggerEdgeChanged OnEventTriggerEdgeChanged;
	FOnTimelineEventDuplicated OnEventDuplicated;
	FOnTimelineEventRemoved OnEventRemoved;
	FOnTimelineEventTrackChanged OnEventTrackChanged;
	FOnTimelineAddCueRequested OnAddCueRequested;
	FOnTimelineEdgeAutoscrollRequested OnEdgeAutoscrollRequested;

	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual void OnMouseLeave(const FPointerEvent& MouseEvent) override;
	virtual void OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent) override;
	virtual FCursorReply OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const override;

	/** Cancel an armed/active edit when the owning tool is hidden or replaced. If a transaction-start
	 *  delegate fired, its matching end delegate fires exactly once before capture is released. */
	void CancelActiveInteraction();
	bool HasActiveInteractionForTests() const
	{
		return bDragArmed || bDraggingEvent || bResizingEvent || bEditingEdge || bReassigningTrack;
	}
	/** Automation seam that enters the same post-threshold delegate path as a real move/resize drag. */
	void BeginActiveInteractionForTests(UPaper2DPlusCueBase* Cue, bool bResize);
	/** Automation seam for the trigger-edge diamond: same start pairing, then edge emits. */
	void BeginEdgeInteractionForTests(UPaper2DPlusCueBase* Cue);
	void EmitEdgeChangeForTests(EPaper2DPlusCueTriggerEdge NewEdge);

	/** THE 52px frame-column width (the FFrameStripCellUtils standard cell width). PUBLIC because the
	 *  Frame Events curve-track rows (CurveTrackRow.cpp) pin their bodies and paint their playheads
	 *  with the SAME constant — single-sourced so the columns can never drift apart. */
	static constexpr float ColumnWidth =
		Paper2DPlusFrameCueTimeline::FTimingGeometry::PixelsPerKeyFrame;
	FText GetAccessibleSummaryText() const;
	/** Accessibility projection for a specific row; the visual tour uses it to ratchet both timing forms. */
	FText GetAccessibleSummaryTextForCue(int32 CueIndex) const;
	TArray<Paper2DPlusFrameCueTimeline::FPackedTrack> GetPackedTracksForPresentation() const;
	float GetPackedTrackHeightForPresentation(
		const Paper2DPlusFrameCueTimeline::FPackedTrack& Track) const;

#if WITH_DEV_AUTOMATION_TESTS
	bool HasAddCueContextHandlerForTests() const { return OnAddCueRequested.IsBound(); }
	bool ResolveAddCueContextTargetForTests(
		FVector2D LocalPosition,
		int32& OutFrameIndex,
		FGuid& OutTrackId) const
	{
		return ResolveAddCueContextTarget(LocalPosition, OutFrameIndex, OutTrackId);
	}
#endif

private:
	TAttribute<TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset>> Asset;
	TSharedPtr<FFrameCueDataProvider> DataProvider;
	TAttribute<const TArray<TObjectPtr<UPaper2DPlusCueBase>>*> CueSource;
	TAttribute<int32> CharacterFrameCountAttribute;
	TAttribute<int32> SelectedFlipbookIndex;
	TAttribute<int32> SelectedEventIndex;
	TAttribute<int32> SelectedFrameIndex;

	// Layout constants
	static constexpr float CharacterTrackHeight = 24.0f;
	static constexpr float EventTrackHeight = 30.0f;
	static constexpr float TrackPadding = 2.0f;
	static constexpr float LabelWidth = 0.0f;
	static constexpr float ResizeHandleWidth = 6.0f;
	static constexpr float EmptyTrackHeight = EventTrackHeight + TrackPadding;

	// Drag state
	// Mouse-down only arms a gesture. The parent-owned transaction starts after Slate's configured
	// drag threshold is crossed, so selecting a cue never dirties the profile or creates an undo step.
	bool bDragArmed = false;
	bool bArmedForResize = false;
	/** Armed on the trigger-edge diamond: the horizontal drag flips the edge, never the frame. */
	bool bArmedForEdge = false;
	bool bDraggingEvent = false;
	bool bResizingEvent = false;
	bool bEditingEdge = false;
	bool bReassigningTrack = false;
	bool bHasPendingTrackTarget = false;
	FGuid PendingTrackId;
	int32 DragEventIndex = INDEX_NONE;
	TWeakObjectPtr<UPaper2DPlusCueBase> DragCueIdentity;
	int32 DragStartFrame = 0;
	int32 DragStartFrameCount = 1;
	float DragStartX = 0.f;
	FVector2D DragStartScreenPos = FVector2D::ZeroVector;

	// Hover state
	mutable TWeakObjectPtr<UPaper2DPlusCueBase> HoveredCueIdentity;
	mutable bool bHoveringResizeHandle = false;

	struct FCachedCueLabelMetrics
	{
		FString Label;
		float Width = 0.0f;
	};
	mutable TMap<TWeakObjectPtr<UPaper2DPlusCueBase>, FCachedCueLabelMetrics> CueLabelMetricsCache;
	FDelegateHandle TagColorsChangedHandle;

	// Helpers
	void HandleTagColorsChanged();
	FText GetHoveredCueToolTipText() const;
	UPaper2DPlusCharacterProfileAsset* ResolveAsset() const
	{
		return Asset.Get(TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset>()).Get();
	}
	const TArray<TObjectPtr<UPaper2DPlusCueBase>>* ResolveCues() const;
	int32 GetCharacterFrameCount() const;
	int32 GetTotalColumns() const;
	TArray<Paper2DPlusFrameCueTimeline::FPackedTrack> BuildPackedTracks() const;
	float GetPackedTrackHeight(
		const Paper2DPlusFrameCueTimeline::FPackedTrack& Track) const;
	float GetTotalEventsHeight() const;
	int32 FrameFromX(float LocalX) const;
	static void GetEventFrameRange(const UPaper2DPlusCueBase* Cue, int32& OutStart, int32& OutCount);
	int32 HitTestEventBar(const FGeometry& Geom, const FVector2D& LocalPos) const;
	bool HitTestResizeHandle(const FGeometry& Geom, const FVector2D& LocalPos, int32 EventIdx) const;
	/** Moment-only: the trigger-edge diamond's slop box, with priority over the plain bar. */
	bool HitTestEdgeDiamond(const FGeometry& Geom, const FVector2D& LocalPos, int32 EventIdx) const;
	FGuid TrackIdFromY(float LocalY) const;
	bool ResolveAddCueContextTarget(
		FVector2D LocalPosition,
		int32& OutFrameIndex,
		FGuid& OutTrackId) const;
	void ShowTrackContextMenu(
		const FPointerEvent& MouseEvent,
		int32 FrameIndex,
		FGuid TrackId);
	void ShowEventContextMenu(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent, int32 EventIdx);
	void BuildMoveToTrackMenu(FMenuBuilder& MenuBuilder, TWeakObjectPtr<UPaper2DPlusCueBase> Cue);
	void MoveContextCueToTrack(TWeakObjectPtr<UPaper2DPlusCueBase> Cue, FGuid TrackId);
	int32 ResolveCurrentEventIndex(TWeakObjectPtr<UPaper2DPlusCueBase> Cue) const;
	void ResetDragState();
	void DuplicateContextCue(TWeakObjectPtr<UPaper2DPlusCueBase> Cue);
	void RemoveContextCue(TWeakObjectPtr<UPaper2DPlusCueBase> Cue);
};
