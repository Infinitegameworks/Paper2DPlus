// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "AnimationTimeline.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "ProfilePanelFocusSeat.h"
#include "ProfileToolPanelProvider.h"
#include "Containers/Ticker.h"
#include "Editor/EditorEngine.h"

class SAnimationTimeline;
class SVerticalBox;
class FCharacterProfileEditorModel;

namespace Paper2DPlusEditor::FrameTimingEditorUtils
{
	/** Convert an authored FrameRun into its integer millisecond display value. */
	PAPER2DPLUSEDITOR_API int32 FrameRunToMilliseconds(int32 FrameRun, float FPS);

	/** Convert a millisecond edit back to the canonical [1, 999] FrameRun range. */
	PAPER2DPLUSEDITOR_API int32 MillisecondsToFrameRun(int32 Milliseconds, float FPS);
}

/**
 * Frame Duration List widget - shows per-frame details with spinbox editing.
 * Displays each frame's thumbnail, index, and editable duration.
 */
class SFrameDurationList : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SFrameDurationList) {}
		SLATE_ARGUMENT(TWeakObjectPtr<UPaperFlipbook>, Flipbook)
		SLATE_ATTRIBUTE(int32, SelectedFrameIndex)
		SLATE_ATTRIBUTE(ETimingDisplayUnit, DisplayUnit)
		SLATE_ATTRIBUTE(float, FPS)
		SLATE_ARGUMENT(TSet<int32>*, SelectedFrames)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Full rebuild — use when frame count changes (flipbook switch, undo). */
	void Refresh();

	/** Repaint only — use when values/selection changed but frame count is the same. Lambdas handle the data. */
	void InvalidateDisplay();

	/** Set the flipbook to display */
	void SetFlipbook(UPaperFlipbook* InFlipbook);

	// Delegates
	DECLARE_DELEGATE_ThreeParams(
		FOnFrameDurationChanged,
		UPaperFlipbook* /*SourceFlipbook*/,
		int32 /*FrameIndex*/,
		int32 /*NewDuration*/);
	DECLARE_DELEGATE_OneParam(FOnFrameSelected, int32 /*FrameIndex*/);
	DECLARE_DELEGATE(FOnEditGesture);

	FOnFrameDurationChanged OnFrameDurationChanged;
	FOnFrameSelected OnFrameSelected;
	FOnEditGesture OnEditGestureStarted;
	FOnEditGesture OnEditGestureFinished;

private:
	TWeakObjectPtr<UPaperFlipbook> Flipbook;
	TAttribute<int32> SelectedFrameIndex;
	TAttribute<ETimingDisplayUnit> DisplayUnit;
	TAttribute<float> FPS;
	TSet<int32>* SelectedFrames = nullptr;
	int32 FrameSelectionAnchorIndex = INDEX_NONE;
	bool bDurationSliderMoving = false;

	TSharedPtr<SVerticalBox> FrameListBox;

	void BuildFrameRow(int32 FrameIndex, int32 CurrentDuration);
	void CommitDisplayedDuration(
		TWeakObjectPtr<UPaperFlipbook> SourceFlipbook,
		int32 FrameIndex,
		int32 DisplayedDuration);
};

/**
 * Main Frame Timing Editor widget.
 * Container that hosts the timeline, frame list, preview, and controls.
 * Integrates as a tab in the CharacterProfileAssetEditor.
 */
class SFrameTimingEditor : public SCompoundWidget, public FEditorUndoClient, public IProfileToolPanelProvider
{
public:
	static const FName TimingPanelId;
	static const FName SelectionPanelId;
	static const FName BatchPanelId;

	SLATE_BEGIN_ARGS(SFrameTimingEditor)
		: _HostContract(FProfileToolPanelHostContract::Embedded())
	{}
		SLATE_ARGUMENT(TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset>, Asset)
		SLATE_ARGUMENT(TSharedPtr<FCharacterProfileEditorModel>, Model)
		SLATE_ARGUMENT(FProfileToolPanelHostContract, HostContract)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SFrameTimingEditor();

	// FEditorUndoClient
	virtual void PostUndo(bool bSuccess) override;
	virtual void PostRedo(bool bSuccess) override;

	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FProfileToolPanelHostContract GetHostContract() const override { return HostContract; }
	virtual void GetContextualPanels(TArray<FProfileToolPanelDescriptor>& OutPanels) const override;

	void StopPlayback();
	void HandleHostActivated();
	void HandleHostDeactivated();

	void RefreshAll();
	void RefreshFlipbookList();
	bool HasActiveTransaction() const { return ActiveTransaction.IsValid(); }

	// Focused U7 acceptance seams. Every mutating seam calls the production command path.
	int32 GetSelectedFlipbookIndexForTests() const { return SelectedFlipbookIndex; }
	int32 GetSelectedFrameIndexForTests() const { return SelectedFrameIndex; }
	float GetFPSForTests() const { return PlaybackFPS; }
	int32 GetFrameRunForTests(int32 FrameIndex) const;
	int32 GetFrameMillisecondsForTests(int32 FrameIndex) const;
	float GetTotalDurationSecondsForTests() const;
	void SetFPSForTests(float NewFPS) { OnFPSChanged(NewFPS); }
	void BeginFPSGestureForTests() { BeginFPSGesture(); }
	void EndFPSGestureForTests() { EndContinuousTimingEdit(); }
	void SetFrameRunForTests(int32 FrameIndex, int32 NewDuration)
	{
		OnFrameDurationChanged(FrameIndex, NewDuration);
	}
	void BeginFrameDurationGestureForTests() { BeginFrameDurationGesture(); }
	void EndFrameDurationGestureForTests() { EndContinuousTimingEdit(); }
	void SetFrameMillisecondsForTests(int32 FrameIndex, int32 Milliseconds);
	void ApplySelectionDurationForTests(
		UPaperFlipbook* SourceFlipbook,
		int32 FrameIndex,
		int32 NewDuration)
	{
		OnSelectionFrameDurationChanged(SourceFlipbook, FrameIndex, NewDuration);
	}
	void SetDisplayUnitForTests(ETimingDisplayUnit NewUnit) { OnDisplayUnitChanged(NewUnit); }
	void ConfigureBatchForTests(
		int32 SourceIndex,
		int32 TargetIndex,
		int32 CustomValue,
		int32 RangeStart,
		int32 RangeEnd);
	void ApplyBatchForTests() { OnApplyBatchOperation(); }
	int32 GetTransactionBeginCountForTests() const { return TransactionBeginCount; }
	int32 GetTransactionEndCountForTests() const { return TransactionEndCount; }
	bool HasPendingRefreshForTests() const { return bNeedsRefresh; }
	bool IsHostActiveForTests() const { return bHostActive; }
	int32 GetContextPanelBuildCountForTests(FName PanelId) const
	{
		return ContextPanelBuildCounts.FindRef(PanelId);
	}
	int32 GetContextPanelResolvedFlipbookForTests(FName PanelId) const
	{
		return ContextPanelResolvedFlipbooks.FindRef(PanelId);
	}
	int32 GetContextPanelResolvedFrameForTests(FName PanelId) const
	{
		return ContextPanelResolvedFrames.FindRef(PanelId);
	}
	static bool IsShortcutProtectedWidgetTypeForTests(const FString& WidgetTypeName);
	/** Cancels a pending seat timer and runs its production body now (NullRHI never paints). */
	void ApplyDeferredHostFocusForTests();
	bool HasPendingHostFocusSeatForTests() const { return HostFocusSeat.GetPendingTimer().IsValid(); }

private:
	TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset> Asset;
	TSharedPtr<FCharacterProfileEditorModel> Model;
	FProfileToolPanelHostContract HostContract;
	bool bHostActive = false;
	FProfilePanelFocusSeat HostFocusSeat;
	EActiveTimerReturnType ApplyDeferredHostFocus(double CurrentTime, float DeltaTime);

	TSet<int32> CachedSelectedFrames;
	void SyncSelectedFramesFromModel();

	// Model delegate handles
	FDelegateHandle ModelFlipbookSelectionHandle;
	FDelegateHandle ModelFrameSelectionHandle;
	FDelegateHandle ModelGroupCollapseHandle;
	FDelegateHandle ModelSearchTextHandle;
	FDelegateHandle ModelAssetExternallyModifiedHandle;
	FDelegateHandle ModelAssetDataChangedHandle;
	FDelegateHandle ModelDirectionalPreviewHandle;

	// Selection state
	int32 SelectedFlipbookIndex = 0;
	int32 SelectedFrameIndex = 0;
	int32 FrameSelectionAnchorIndex = INDEX_NONE;
	bool bPropagatingFrameSelection = false;

	/** Set by PostUndo/PostRedo; cleared by RefreshAll(). Avoids rebuilding widgets while tab is hidden. */
	bool bNeedsRefresh = false;
	TWeakPtr<FActiveTimerHandle> DeferredRefreshTimer;

	// Display settings
	ETimingDisplayUnit DisplayUnit = ETimingDisplayUnit::Frames;

	// Playback state
	bool bIsPlaying = false;
	bool bResumeAfterDirectionalPreviewResolves = false;
	float PlaybackPosition = 0.0f; // Current playback time in seconds
	float PlaybackFPS = 12.0f;
	FFlipbookTimingData CachedPlaybackTiming; // Cached to avoid per-tick allocation
	FTSTicker::FDelegateHandle PlaybackTickerHandle;

	// Sub-widgets
	TSharedPtr<SAnimationTimeline> TimelineWidget;
	TWeakPtr<SFrameDurationList> FrameDurationListWidget;
	TSharedPtr<SVerticalBox> FlipbookListBox;
	FString FlipbookSearchFilter;
	TSharedPtr<SVerticalBox> PreviewBox;
	TSharedPtr<class SFramePreviewCanvas> PreviewCanvas;

	float PreviewZoom = 3.0f;

	// UI builders
	TSharedRef<SWidget> BuildToolbar();
	TSharedRef<SWidget> BuildFlipbookList();
	TSharedRef<SWidget> BuildPreviewPanel();
	TSharedRef<SWidget> BuildBatchToolsPanel();

	// Refresh functions
	void RefreshFrameList();
	void RefreshPreview();

	// Event handlers
	void OnFlipbookSelected(int32 Index);
	void OnFrameSelected(int32 Index);
	void OnFrameDurationChanged(int32 FrameIndex, int32 NewDuration);
	void OnFPSChanged(float NewFPS);
	void OnDisplayUnitChanged(ETimingDisplayUnit NewUnit);
	void BeginFrameDurationGesture();
	void BeginFPSGesture();
	void BeginContinuousTimingEdit(const FText& Description);
	void EndContinuousTimingEdit();

	// Model delegate handlers
	void OnModelFlipbookSelectionChanged(int32 NewIndex);
	void OnModelFrameSelectionChanged();
	void OnModelGroupCollapseChanged();
	void OnModelSearchTextChanged(const FString& NewText);
	void OnModelAssetExternallyModified();
	void OnModelAssetDataChanged();

	// Batch operations
	void OnApplyBatchOperation();
	int32 BatchSourceIndex = 0;
	int32 BatchTargetIndex = 0;
	int32 BatchCustomValue = 4;
	int32 BatchRangeStart = 0;
	int32 BatchRangeEnd = 0;
	TArray<TSharedPtr<FString>> BatchSourceOptions;
	TArray<TSharedPtr<FString>> BatchTargetOptions;
	mutable TMap<FName, int32> ContextPanelBuildCounts;
	mutable TMap<FName, int32> ContextPanelResolvedFlipbooks;
	mutable TMap<FName, int32> ContextPanelResolvedFrames;

	// Playback
	void StartPlayback();
	void TogglePlayback();
	bool OnPlaybackTick(float DeltaTime);

	// Undo support
	void BeginTransaction(const FText& Description);
	void EndTransaction();
	TUniquePtr<FScopedTransaction> ActiveTransaction;
	int32 TransactionBeginCount = 0;
	int32 TransactionEndCount = 0;
	bool bContinuousEditGesture = false;
	bool bContinuousEditChanged = false;
	bool bFPSSliderMoving = false;

	// Helpers
	TSharedRef<SWidget> BuildCentralWorkspace();
	TSharedRef<SWidget> BuildEmbeddedWorkspace();
	TSharedRef<SWidget> BuildTimelinePanel();
	TSharedRef<SWidget> BuildSelectionPanel();
	void OnSelectionFrameDurationChanged(
		UPaperFlipbook* SourceFlipbook,
		int32 FrameIndex,
		int32 NewDuration);
	void FinishActiveEditGesture(bool bReleaseTimelineCapture);
	void ScheduleDeferredRefresh();
	EActiveTimerReturnType HandleDeferredRefreshTimer(double CurrentTime, float DeltaTime);
	bool CanAcceptEdits() const;
	int32 GetLiveSelectedFlipbookIndex() const;
	int32 GetLiveSelectedFrameIndex() const;
	UPaperFlipbook* GetCurrentFlipbook() const;
	UPaperFlipbook* GetPreviewFlipbook() const;
	const FFlipbookProfileEntry* GetCurrentFlipbookData() const;
	int32 GetCurrentFrameCount() const;

	friend class FPaper2DPlusDirectionalCrossToolPreviewArtTest;
};
