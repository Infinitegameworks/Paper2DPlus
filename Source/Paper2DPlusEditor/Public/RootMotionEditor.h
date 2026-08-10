// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SLeafWidget.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "AnimationTimeline.h"
#include "ProfilePanelFocusSeat.h"
#include "ProfileToolPanelProvider.h"
#include "Containers/Ticker.h"
#include "Editor/EditorEngine.h"

class SVerticalBox;
class SHorizontalBox;
class SScrollBox;
class UPaperFlipbook;
class SRootMotionCanvas;
class FCharacterProfileEditorModel;

/**
 * Root Motion controller + central preview. Embedded hosts retain the legacy side stack; the Character
 * workspace asks this same controller for fresh Position, Path/Skins, and Batch contextual views.
 */
class SRootMotionEditor : public SCompoundWidget, public FEditorUndoClient, public IProfileToolPanelProvider
{
public:
	static const FName PositionPanelId;
	static const FName OnionSkinsPanelId;
	static const FName BatchPanelId;

	SLATE_BEGIN_ARGS(SRootMotionEditor)
		: _HostContract(FProfileToolPanelHostContract::Embedded())
	{}
		SLATE_ARGUMENT(TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset>, Asset)
		SLATE_ARGUMENT(TSharedPtr<FCharacterProfileEditorModel>, Model)
		SLATE_ARGUMENT(FProfileToolPanelHostContract, HostContract)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SRootMotionEditor();

	// FEditorUndoClient
	virtual void PostUndo(bool bSuccess) override;
	virtual void PostRedo(bool bSuccess) override;

	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual bool SupportsKeyboardFocus() const override { return true; }

	// IProfileToolPanelProvider
	virtual FProfileToolPanelHostContract GetHostContract() const override { return HostContract; }
	virtual void GetContextualPanels(TArray<FProfileToolPanelDescriptor>& OutPanels) const override;

	void HandleHostActivated();
	void HandleHostDeactivated();

	void StopPlayback();
	void RefreshAll();
	void RefreshFlipbookList();
	bool HasActiveTransaction() const { return ActiveTransaction.IsValid(); }
	int32 GetSelectedFlipbookIndexForTests() const { return SelectedFlipbookIndex; }
	int32 GetSelectedFrameIndexForTests() const { return SelectedFrameIndex; }
	const FProfileToolPanelHostContract& GetHostContractForTests() const { return HostContract; }
	int32 GetContextPanelBuildCountForTests(FName PanelId) const
	{
		return ContextPanelBuildCounts.FindRef(PanelId);
	}
	int32 GetContextPanelResolvedFrameForTests(FName PanelId) const
	{
		return ContextPanelResolvedFrames.FindRef(PanelId);
	}
	FVector2D GetCurrentFramePositionForTests() const { return GetCurrentFramePosition(); }
	void SetCurrentFrameXForTests(float NewX);
	void SetCurrentFrameYForTests(float NewY);
	void ResetCurrentFrameForTests() { ResetCurrentFrame(); }
	void NudgeCurrentFrameForTests(FVector2D Delta) { NudgeCurrentFrame(Delta); }
	void CommitPendingEditForTests() { FinishActiveEditGesture(); }
	void BeginCanvasDragForTests();
	void DragCanvasToForTests(FVector2D NewPosition);
	void EndCanvasDragForTests();
	void SetPathSkinStateForTests(bool bOnion, bool bForward, int32 Frames, float Opacity);
	bool IsCanvasShowingOnionForTests() const;
	bool IsCanvasShowingForwardOnionForTests() const;
	void ConfigureBatchForTests(
		int32 OperationIndex,
		int32 TargetIndex,
		FVector2D CustomValue,
		int32 RangeStart,
		int32 RangeEnd);
	void ApplyBatchForTests() { OnApplyMotionBatchOperation(); }
	int32 GetTransactionBeginCountForTests() const { return TransactionBeginCount; }
	int32 GetTransactionEndCountForTests() const { return TransactionEndCount; }
	bool IsHostActiveForTests() const { return bHostActive; }
	/** Cancels a pending seat timer and runs its production body now (NullRHI never paints). */
	void ApplyDeferredHostFocusForTests();
	bool HasPendingHostFocusSeatForTests() const { return HostFocusSeat.GetPendingTimer().IsValid(); }

private:
	TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset> Asset;
	FProfileToolPanelHostContract HostContract;
	bool bHostActive = false;
	FProfilePanelFocusSeat HostFocusSeat;
	EActiveTimerReturnType ApplyDeferredHostFocus(double CurrentTime, float DeltaTime);
	mutable TMap<FName, int32> ContextPanelBuildCounts;
	mutable TMap<FName, int32> ContextPanelResolvedFrames;

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
	EActiveTimerReturnType HandleNudgeDebounce(double CurrentTime, float DeltaTime);

	// Model
	TSharedPtr<FCharacterProfileEditorModel> Model;
	FDelegateHandle ModelFlipbookSelectionHandle;
	FDelegateHandle ModelFrameSelectionHandle;
	FDelegateHandle ModelGroupCollapseHandle;
	FDelegateHandle ModelSearchTextHandle;
	FDelegateHandle ModelAssetExternallyModifiedHandle;
	FDelegateHandle ModelAssetDataChangedHandle;

	// Transaction helpers
	TUniquePtr<FScopedTransaction> ActiveTransaction;
	FProfileAnimationIdentity ActiveEditAnimation;
	int32 ActiveEditFrameIndex = INDEX_NONE;
	bool bActiveTransactionChanged = false;
	bool bBroadcastingOwnDataChange = false;
	int32 TransactionBeginCount = 0;
	int32 TransactionEndCount = 0;
	void BeginTransaction(const FText& Description);
	void EndTransaction();
	void FinishActiveEditGesture(bool bReleaseCanvasCapture = false);
	bool CanMutateLiveSelection() const;
	bool DoesActiveEditTargetLiveSelection() const;
	void MarkActiveTransactionChanged();
	void NotifyMotionDataChanged();

	// Sub-widgets
	TSharedPtr<SVerticalBox> FlipbookListBox;
	TSharedPtr<SHorizontalBox> FrameStripBox;
	TSharedPtr<SRootMotionCanvas> MotionCanvas;
	TSharedPtr<SVerticalBox> PropertiesBox;

	// UI builders
	TSharedRef<SWidget> BuildCentralWorkspace();
	TSharedRef<SWidget> BuildEmbeddedContextStack();
	TSharedRef<SWidget> BuildToolbar();
	TSharedRef<SWidget> BuildFlipbookList();
	TSharedRef<SWidget> BuildFrameStrip();
	TSharedRef<SWidget> BuildPropertiesPanel();
	TSharedRef<SWidget> BuildPositionPanel();
	TSharedRef<SWidget> BuildOnionSkinsPanel();
	TSharedRef<SWidget> BuildBatchPanel();

	// Refresh
	void RefreshFrameStrip();
	void RefreshPropertiesPanel();

	// Flipbook selection (internal — driven by model delegate)
	void OnModelFlipbookSelected(int32 FlipbookIndex);
	void OnModelFrameSelected();

	// Frame selection
	void OnFrameClicked(int32 FrameIndex);

	// Root motion editing
	bool SetCurrentFramePosition(FVector2D NewPosition);
	void SetCurrentFrameX(float NewX);
	void SetCurrentFrameY(float NewY);
	void CommitCurrentFramePositionEdit();
	void NudgeCurrentFrame(FVector2D Delta);
	void ResetCurrentFrame();
	FVector2D GetCurrentFramePosition() const;

	// Batch operations (sentence-style dropdown)
	void OnApplyMotionBatchOperation();
	int32 MotionBatchSourceIndex = 0;
	int32 MotionBatchTargetIndex = 0;
	FVector2D MotionBatchCustomValue = FVector2D::ZeroVector;
	int32 MotionBatchRangeStart = 0;
	int32 MotionBatchRangeEnd = 0;
	TArray<TSharedPtr<FString>> MotionBatchOperationOptions;
	TArray<TSharedPtr<FString>> MotionBatchTargetOptions;
	void InitializeBatchOptions();

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
		SLATE_ATTRIBUTE(int32, PreviousFlipbookIndex)
		SLATE_ATTRIBUTE(int32, NextFlipbookIndex)
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
	bool IsShowingOnionSkinForTests() const { return ShowOnionSkin.Get(false); }
	bool IsShowingForwardOnionSkinForTests() const { return ShowForwardOnionSkin.Get(false); }

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
	TAttribute<int32> PreviousFlipbookIndex;
	TAttribute<int32> NextFlipbookIndex;

	// View state
	float UserZoom = 1.0f;
	FVector2D PanOffset = FVector2D::ZeroVector;
	/** OnPaint computes fit zoom once; every draw helper reuses it instead of rescanning the path. */
	mutable TOptional<float> PaintZoomOverride;

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
