// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"

class UPaper2DPlusCharacterProfileAsset;
class UPaper2DPlusFrameEventBase;

// Delegates used by the timeline track
DECLARE_DELEGATE_OneParam(FOnTimelineFrameClicked, int32);
DECLARE_DELEGATE_OneParam(FOnTimelineEventSelected, int32);
DECLARE_DELEGATE(FOnTimelineEventDragStarted);
DECLARE_DELEGATE(FOnTimelineEventDragEnded);
DECLARE_DELEGATE_TwoParams(FOnTimelineEventFrameChanged, int32, int32); // EventIndex, NewFrame
DECLARE_DELEGATE_TwoParams(FOnTimelineEventDurationChanged, int32, int32); // EventIndex, NewFrameCount
DECLARE_DELEGATE_OneParam(FOnTimelineEventRemoved, int32); // EventIndex

/**
 * Timeline track widget for frame events -- renders event bars on a frame grid,
 * supports drag-to-move, drag-to-resize, right-click context menu, and
 * hover/selection highlighting.
 */
class SFrameEventTimelineTrack : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SFrameEventTimelineTrack) {}
		SLATE_ARGUMENT(TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset>, Asset)
		SLATE_ATTRIBUTE(int32, SelectedFlipbookIndex)
		SLATE_ATTRIBUTE(int32, SelectedEventIndex)
		SLATE_ATTRIBUTE(int32, SelectedFrameIndex)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	// Delegates
	FOnTimelineFrameClicked OnFrameClicked;
	FOnTimelineEventSelected OnEventSelected;
	FOnTimelineEventDragStarted OnEventDragStarted;
	FOnTimelineEventDragEnded OnEventDragEnded;
	FOnTimelineEventFrameChanged OnEventFrameChanged;
	FOnTimelineEventDurationChanged OnEventDurationChanged;
	FOnTimelineEventRemoved OnEventRemoved;

	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void OnMouseLeave(const FPointerEvent& MouseEvent) override;
	virtual void OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent) override;
	virtual FCursorReply OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const override;

private:
	TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset> Asset;
	TAttribute<int32> SelectedFlipbookIndex;
	TAttribute<int32> SelectedEventIndex;
	TAttribute<int32> SelectedFrameIndex;

	// Layout constants
	static constexpr float ColumnWidth = 52.0f;
	static constexpr float CharacterTrackHeight = 24.0f;
	static constexpr float EventTrackHeight = 20.0f;
	static constexpr float EventTrackHeightWithSprites = 48.0f;
	static constexpr float TrackPadding = 2.0f;
	static constexpr float LabelWidth = 0.0f;
	static constexpr float LabelRowHeight = 16.0f;
	static constexpr float ResizeHandleWidth = 6.0f;

	// Drag state
	bool bDraggingEvent = false;
	bool bResizingEvent = false;
	int32 DragEventIndex = INDEX_NONE;
	int32 DragStartFrame = 0;
	int32 DragStartFrameCount = 1;
	float DragStartX = 0.f;

	// Hover state
	mutable int32 HoveredEventIndex = INDEX_NONE;
	mutable bool bHoveringResizeHandle = false;

	// Helpers
	int32 GetCharacterFrameCount() const;
	int32 GetTotalColumns() const;
	static bool EventHasFlipbook(const UPaper2DPlusFrameEventBase* Event);
	static float GetEventHeight(const UPaper2DPlusFrameEventBase* Event);
	float GetEventTrackY(const TArray<TObjectPtr<UPaper2DPlusFrameEventBase>>& Events, int32 EventIdx) const;
	float GetTotalEventsHeight(const TArray<TObjectPtr<UPaper2DPlusFrameEventBase>>& Events) const;
	int32 FrameFromX(const FGeometry& Geom, float LocalX) const;
	static void GetEventFrameRange(const UPaper2DPlusFrameEventBase* Event, int32& OutStart, int32& OutCount);
	int32 HitTestEventBar(const FGeometry& Geom, const FVector2D& LocalPos) const;
	bool HitTestResizeHandle(const FGeometry& Geom, const FVector2D& LocalPos, int32 EventIdx) const;
	void ShowEventContextMenu(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent, int32 EventIdx);
};