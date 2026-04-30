// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "AnimationTimeline.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Containers/Ticker.h"
#include "Editor/EditorEngine.h"

class SAnimationTimeline;
class SVerticalBox;
class STextBlock;

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
	DECLARE_DELEGATE_TwoParams(FOnFrameDurationChanged, int32 /*FrameIndex*/, int32 /*NewDuration*/);
	DECLARE_DELEGATE_OneParam(FOnFrameSelected, int32 /*FrameIndex*/);

	FOnFrameDurationChanged OnFrameDurationChanged;
	FOnFrameSelected OnFrameSelected;

private:
	TWeakObjectPtr<UPaperFlipbook> Flipbook;
	TAttribute<int32> SelectedFrameIndex;
	TAttribute<ETimingDisplayUnit> DisplayUnit;
	TAttribute<float> FPS;
	TSet<int32>* SelectedFrames = nullptr;
	int32 FrameSelectionAnchorIndex = INDEX_NONE;

	TSharedPtr<SVerticalBox> FrameListBox;

	void BuildFrameRow(int32 FrameIndex, int32 CurrentDuration);
};

/**
 * Main Frame Timing Editor widget.
 * Container that hosts the timeline, frame list, preview, and controls.
 * Integrates as a tab in the CharacterProfileAssetEditor.
 */
class SFrameTimingEditor : public SCompoundWidget, public FEditorUndoClient
{
public:
	SLATE_BEGIN_ARGS(SFrameTimingEditor) {}
		SLATE_ARGUMENT(TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset>, Asset)
		SLATE_ARGUMENT(TSet<FName>*, CollapsedFlipbookGroups)
		SLATE_ARGUMENT(TSet<int32>*, SelectedFrames)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SFrameTimingEditor();

	// FEditorUndoClient
	virtual void PostUndo(bool bSuccess) override;
	virtual void PostRedo(bool bSuccess) override;

	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual bool SupportsKeyboardFocus() const override { return true; }

	// External control
	void SetSelectedFlipbook(int32 FlipbookIndex);
	void StopPlayback();

	/** Refresh all sub-widgets */
	void RefreshAll();
	void RefreshFlipbookList();
	bool HasActiveTransaction() const { return ActiveTransaction.IsValid(); }

	// Delegates for parent editor notifications
	DECLARE_DELEGATE(FOnTimingDataModified);
	FOnTimingDataModified OnTimingDataModified;

	DECLARE_DELEGATE_OneParam(FOnFlipbookSelectedInList, int32);
	FOnFlipbookSelectedInList OnFlipbookSelectedInList;

private:
	TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset> Asset;
	TSet<FName>* CollapsedFlipbookGroups = nullptr;
	TSet<int32>* ParentSelectedFrames = nullptr;

	// Selection state
	int32 SelectedFlipbookIndex = 0;
	int32 SelectedFrameIndex = 0;
	int32 FrameSelectionAnchorIndex = INDEX_NONE;

	/** Set by PostUndo/PostRedo; cleared by RefreshAll(). Avoids rebuilding widgets while tab is hidden. */
	bool bNeedsRefresh = false;

	// Display settings
	ETimingDisplayUnit DisplayUnit = ETimingDisplayUnit::Frames;

	// Playback state
	bool bIsPlaying = false;
	float PlaybackPosition = 0.0f; // Current playback time in seconds
	float PlaybackFPS = 12.0f;
	FFlipbookTimingData CachedPlaybackTiming; // Cached to avoid per-tick allocation
	FTSTicker::FDelegateHandle PlaybackTickerHandle;

	// Sub-widgets
	TSharedPtr<SAnimationTimeline> TimelineWidget;
	TSharedPtr<SFrameDurationList> FrameDurationListWidget;
	TSharedPtr<SVerticalBox> FlipbookListBox;
	FString FlipbookSearchFilter;
	TSharedPtr<SVerticalBox> PreviewBox;
	TSharedPtr<STextBlock> StatsText;
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

	// Batch operations
	void OnApplyBatchOperation();
	int32 BatchSourceIndex = 0;
	int32 BatchTargetIndex = 0;
	int32 BatchCustomValue = 4;
	int32 BatchRangeStart = 0;
	int32 BatchRangeEnd = 0;

	// Playback
	void StartPlayback();
	void TogglePlayback();
	bool OnPlaybackTick(float DeltaTime);

	// Undo support
	void BeginTransaction(const FText& Description);
	void EndTransaction();
	TUniquePtr<FScopedTransaction> ActiveTransaction;

	// Helpers
	UPaperFlipbook* GetCurrentFlipbook() const;
	const FFlipbookProfileEntry* GetCurrentFlipbookData() const;
	int32 GetCurrentFrameCount() const;
};
