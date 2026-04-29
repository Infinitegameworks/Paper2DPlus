// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SLeafWidget.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "AnimationTimeline.h"
#include "Containers/Ticker.h"
#include "Editor/EditorEngine.h"

class SVerticalBox;
class SHorizontalBox;
class SScrollBox;
class UPaperFlipbook;
class SRootMotionCanvas;

/**
 * Self-contained Root Motion Editor (Tab 6).
 * Left panel: flipbook list. Center: SRootMotionCanvas. Right: X/Y spinboxes. Bottom: frame strip.
 */
class SRootMotionEditor : public SCompoundWidget, public FEditorUndoClient
{
public:
	SLATE_BEGIN_ARGS(SRootMotionEditor) {}
		SLATE_ARGUMENT(TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset>, Asset)
		SLATE_ARGUMENT(TFunction<void(TSharedPtr<SVerticalBox>, TFunction<TSharedRef<SWidget>(int32)>)>, BuildFlipbookListFunc)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SRootMotionEditor();

	// FEditorUndoClient
	virtual void PostUndo(bool bSuccess) override;
	virtual void PostRedo(bool bSuccess) override;

	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual bool SupportsKeyboardFocus() const override { return true; }

	// External control
	void SetSelectedFlipbook(int32 FlipbookIndex);
	void StopPlayback();
	void RefreshAll();
	void RefreshFlipbookList();
	bool HasActiveTransaction() const { return ActiveTransaction.IsValid(); }

	// Delegates
	DECLARE_DELEGATE(FOnRootMotionDataModified);
	FOnRootMotionDataModified OnRootMotionDataModified;

	DECLARE_DELEGATE_OneParam(FOnFlipbookSelectedInList, int32);
	FOnFlipbookSelectedInList OnFlipbookSelectedInList;

private:
	TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset> Asset;

	// Selection state
	int32 SelectedFlipbookIndex = INDEX_NONE;
	int32 SelectedFrameIndex = 0;

	// Playback state
	bool bIsPlaying = false;
	double PlaybackTime = 0.0;
	FFlipbookTimingData CachedTiming;
	FTSTicker::FDelegateHandle PlaybackTickerHandle;

	// Linear interpolation range (user-overridable, defaults auto-detected)
	int32 LerpStartFrame = 0;
	int32 LerpEndFrame = 0;

	// Onion skin state
	bool bShowOnionSkin = false;
	bool bShowForwardOnionSkin = false;
	int32 OnionSkinFrames = 1;
	float OnionSkinOpacity = 0.4f;

	/** Set by PostUndo/PostRedo; cleared by RefreshAll(). Avoids rebuilding widgets while tab is hidden. */
	bool bNeedsRefresh = false;

	// Nudge debounce
	TWeakPtr<FActiveTimerHandle> NudgeDebounceTimer;
	void CommitNudgeTransaction();

	// Flipbook list builder (provided by parent editor for grouped list with thumbnails)
	TFunction<void(TSharedPtr<SVerticalBox>, TFunction<TSharedRef<SWidget>(int32)>)> BuildFlipbookListFunc;

	// Transaction helpers
	TUniquePtr<FScopedTransaction> ActiveTransaction;
	void BeginTransaction(const FText& Description);
	void EndTransaction();

	// Sub-widgets
	TSharedPtr<SVerticalBox> FlipbookListBox;
	TSharedPtr<SHorizontalBox> FrameStripBox;
	TSharedPtr<SRootMotionCanvas> MotionCanvas;
	TSharedPtr<SVerticalBox> PropertiesBox;

	// UI builders
	TSharedRef<SWidget> BuildToolbar();
	TSharedRef<SWidget> BuildFlipbookList();
	TSharedRef<SWidget> BuildFrameStrip();
	TSharedRef<SWidget> BuildPropertiesPanel();

	// Refresh
	void RefreshFrameStrip();
	void RefreshPropertiesPanel();

	// Frame selection
	void OnFrameClicked(int32 FrameIndex);

	// Root motion editing
	void SetCurrentFramePosition(FVector2D NewPosition);
	void ResetCurrentFrame();
	FVector2D GetCurrentFramePosition() const;

	// Batch operations (sentence-style dropdown)
	void OnApplyMotionBatchOperation();
	int32 MotionBatchSourceIndex = 0;
	int32 MotionBatchTargetIndex = 0;
	FVector2D MotionBatchCustomValue = FVector2D::ZeroVector;
	int32 MotionBatchRangeStart = 0;
	int32 MotionBatchRangeEnd = 0;

	// Playback
	bool OnPlaybackTick(float DeltaTime);
	void TogglePlayback();
	int32 GetFrameFromTime() const;

	// Helpers
	FFlipbookProfileEntry* GetSelectedFlipbookData() const;
	UPaperFlipbook* GetSelectedFlipbook() const;
	int32 GetFrameCount() const;

	/** Ensure RootMotion array is sized to match frame count. */
	void EnsureRootMotionArraySized();
};

/**
 * Canvas widget for rendering root motion visualization.
 * Shows sprite at its root motion offset, motion path polyline, ground line, and grid.
 * Supports drag-to-position and pan/zoom.
 */
class SRootMotionCanvas : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SRootMotionCanvas) {}
		SLATE_ARGUMENT(TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset>, Asset)
		SLATE_ATTRIBUTE(int32, SelectedFlipbookIndex)
		SLATE_ATTRIBUTE(int32, SelectedFrameIndex)
		SLATE_ATTRIBUTE(bool, IsPlaying)
		SLATE_ATTRIBUTE(double, PlaybackTime)
		SLATE_ATTRIBUTE(bool, ShowOnionSkin)
		SLATE_ATTRIBUTE(bool, ShowForwardOnionSkin)
		SLATE_ATTRIBUTE(int32, OnionSkinFrames)
		SLATE_ATTRIBUTE(float, OnionSkinOpacity)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

	// Mouse input
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent) override;
	virtual FCursorReply OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const override;

	// Delegates
	DECLARE_DELEGATE(FOnDragStarted);
	DECLARE_DELEGATE(FOnDragEnded);
	DECLARE_DELEGATE_OneParam(FOnPositionChanged, FVector2D); // NewPosition in pixels

	FOnDragStarted OnDragStarted;
	FOnDragEnded OnDragEnded;
	FOnPositionChanged OnPositionChanged;

private:
	TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset> Asset;
	TAttribute<int32> SelectedFlipbookIndex;
	TAttribute<int32> SelectedFrameIndex;
	TAttribute<bool> IsPlaying;
	TAttribute<double> PlaybackTime;
	TAttribute<bool> ShowOnionSkin;
	TAttribute<bool> ShowForwardOnionSkin;
	TAttribute<int32> OnionSkinFrames;
	TAttribute<float> OnionSkinOpacity;

	// View state
	float UserZoom = 1.0f;
	FVector2D PanOffset = FVector2D::ZeroVector;

	// Drag state
	enum class EDragMode : uint8 { None, PendingDrag, MovingSprite, Panning };
	EDragMode DragMode = EDragMode::None;
	FVector2D DragStartScreenPos; // Screen pos at mouse down (for dead zone check)
	FVector2D DragStartCanvasPos;
	FVector2D DragStartPosition; // Original RootMotion position at drag start
	static constexpr float DragDeadZone = 4.0f; // Pixels before drag starts

	// Pan state
	FVector2D PanStartPos;
	FVector2D PanStartOffset;

	// Grid
	static constexpr int32 GridSize = 16;

	// Coordinate conversion
	float GetEffectiveZoom(const FGeometry& Geom) const;
	FVector2D GetCanvasOrigin(const FGeometry& Geom) const;
	FVector2D ScreenToCanvas(const FGeometry& Geom, const FVector2D& ScreenPos) const;
	FVector2D CanvasToScreen(const FGeometry& Geom, const FVector2D& CanvasPos) const;
	int32 SnapToGrid(int32 Value) const;

	// Drawing helpers
	void DrawGroundLine(const FGeometry& Geom, FSlateWindowElementList& OutDrawElements, int32 LayerId) const;
	void DrawGrid(const FGeometry& Geom, FSlateWindowElementList& OutDrawElements, int32 LayerId) const;
	void DrawMotionPath(const FGeometry& Geom, FSlateWindowElementList& OutDrawElements, int32 LayerId,
		const TArray<FRootMotionFrameData>& RootMotion, int32 CurrentFrame) const;
	void DrawSprite(const FGeometry& Geom, FSlateWindowElementList& OutDrawElements, int32 LayerId,
		UPaperFlipbook* Flipbook, int32 FrameIndex, const FVector2D& Offset,
		const FLinearColor& Tint = FLinearColor::White) const;
	void DrawOnionSkin(const FGeometry& Geom, FSlateWindowElementList& OutDrawElements, int32 LayerId,
		UPaperFlipbook* Flipbook, const TArray<FRootMotionFrameData>& RootMotion, int32 CurrentFrame) const;
	void DrawForwardOnionSkin(const FGeometry& Geom, FSlateWindowElementList& OutDrawElements, int32 LayerId,
		UPaperFlipbook* Flipbook, const TArray<FRootMotionFrameData>& RootMotion, int32 CurrentFrame) const;

	// Helpers
	FVector2D GetLargestSpriteDims() const;
	FVector2D GetMotionPathBounds(const TArray<FRootMotionFrameData>& RootMotion) const;

	// Cache for GetLargestSpriteDims
	mutable FVector2D CachedLargestDims = FVector2D(128, 128);
	mutable int32 CachedLargestDimsFlipbookIndex = -1;
	mutable TWeakObjectPtr<UPaperFlipbook> CachedLargestDimsFlipbook;
};
