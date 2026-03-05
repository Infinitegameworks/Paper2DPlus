// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"
#include "PaperFlipbook.h"

/**
 * Transient helper struct for reading/writing flipbook timing data.
 * Not serialized - used only within editor sessions.
 */
struct FFlipbookTimingData
{
	float FPS = 12.0f;
	TArray<int32> FrameDurations;
	int32 TotalFrames = 0;
	float TotalDurationSeconds = 0.0f;

	/** Read timing data from a flipbook */
	static FFlipbookTimingData ReadFromFlipbook(UPaperFlipbook* Flipbook);

	/** Recalculate derived fields */
	void Recalculate();

	/** Get the time offset (in seconds) at which a given frame starts */
	float GetFrameStartTime(int32 FrameIndex) const;

	/** Get the duration of a single frame tick in seconds */
	float GetTickDuration() const { return (FPS > 0.0f) ? (1.0f / FPS) : 0.0f; }

	/** Get duration of a specific frame in seconds */
	float GetFrameDurationSeconds(int32 FrameIndex) const;

	/** Convert frames to milliseconds at current FPS */
	float FramesToMilliseconds(int32 Frames) const;

	/** Convert milliseconds to frames at current FPS */
	int32 MillisecondsToFrames(float Milliseconds) const;
};

/** Display unit mode for the timeline */
enum class ETimingDisplayUnit : uint8
{
	Frames,
	Milliseconds
};

/**
 * Interactive visual timeline widget for animation frame durations.
 * Displays frames as colored blocks proportional to their duration.
 * Supports drag handles to resize, click to select, zoom, and playback cursor.
 */
class SAnimationTimeline : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SAnimationTimeline) {}
		SLATE_ARGUMENT(TWeakObjectPtr<UPaperFlipbook>, Flipbook)
		SLATE_ATTRIBUTE(int32, SelectedFrameIndex)
		SLATE_ATTRIBUTE(float, PlaybackPosition)
		SLATE_ATTRIBUTE(bool, IsPlaying)
		SLATE_ATTRIBUTE(ETimingDisplayUnit, DisplayUnit)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	// SWidget interface
	virtual FVector2D ComputeDesiredSize(float) const override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FCursorReply OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const override;
	virtual void OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent) override;

	/** Refresh cached timing data from the flipbook */
	void RefreshTimingData();

	/** Set the flipbook to display */
	void SetFlipbook(UPaperFlipbook* InFlipbook);

	// Delegates
	DECLARE_DELEGATE_TwoParams(FOnFrameDurationChanged, int32 /*FrameIndex*/, int32 /*NewDuration*/);
	DECLARE_DELEGATE_OneParam(FOnFrameSelected, int32 /*FrameIndex*/);
	DECLARE_DELEGATE_OneParam(FOnZoomChanged, float /*NewZoom*/);

	FOnFrameDurationChanged OnFrameDurationChanged;
	FOnFrameSelected OnFrameSelected;
	FOnZoomChanged OnZoomChanged;

	/** Get current zoom level */
	float GetZoom() const { return ZoomFactor; }
	void SetZoom(float NewZoom) { ZoomFactor = FMath::Clamp(NewZoom, MinZoom, MaxZoom); }

	/** Get cached timing data */
	const FFlipbookTimingData& GetTimingData() const { return CachedTiming; }

	/** Color coding for frame durations - FPS-aware (classifies by hold time, not raw frame count) */
	static FLinearColor GetFrameColor(int32 Duration, float FPS);

private:
	TWeakObjectPtr<UPaperFlipbook> Flipbook;
	TAttribute<int32> SelectedFrameIndex;
	TAttribute<float> PlaybackPosition;
	TAttribute<bool> IsPlaying;
	TAttribute<ETimingDisplayUnit> DisplayUnit;

	// Cached timing data
	FFlipbookTimingData CachedTiming;

	// Zoom
	float ZoomFactor = 1.0f;
	static constexpr float MinZoom = 0.25f;
	static constexpr float MaxZoom = 8.0f;
	static constexpr float BasePixelsPerFrame = 40.0f;

	// Scroll
	float ScrollOffset = 0.0f;

	// Ruler height
	static constexpr float RulerHeight = 20.0f;
	static constexpr float FrameBlockHeight = 50.0f;
	static constexpr float DragHandleWidth = 6.0f;

	// Drag state
	bool bIsDraggingHandle = false;
	int32 DragHandleFrameIndex = -1;
	float DragStartX = 0.0f;
	int32 DragStartDuration = 0;

	// Pan state
	bool bIsPanning = false;
	float PanStartX = 0.0f;
	float PanStartScrollOffset = 0.0f;

	// Coordinate helpers
	float GetFrameXPosition(int32 FrameIndex) const;
	float GetFrameWidth(int32 FrameIndex) const;
	float GetTotalTimelineWidth() const;
	int32 HitTestFrame(const FGeometry& Geom, const FVector2D& LocalPos) const;
	int32 HitTestDragHandle(const FGeometry& Geom, const FVector2D& LocalPos) const;

	// Drawing helpers
	void DrawRuler(const FGeometry& Geom, FSlateWindowElementList& OutDrawElements, int32 LayerId) const;
	void DrawFrameBlocks(const FGeometry& Geom, FSlateWindowElementList& OutDrawElements, int32 LayerId) const;
	void DrawPlaybackCursor(const FGeometry& Geom, FSlateWindowElementList& OutDrawElements, int32 LayerId) const;
	void DrawDragHandles(const FGeometry& Geom, FSlateWindowElementList& OutDrawElements, int32 LayerId) const;
};
