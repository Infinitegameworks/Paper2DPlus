// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "AnimationTimeline.h"
#include "Paper2DPlusCharacterDataAsset.h"
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
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Refresh the list contents */
	void Refresh();

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

	TSharedPtr<SVerticalBox> FrameListBox;

	void BuildFrameRow(int32 FrameIndex, int32 CurrentDuration);
};

/**
 * Main Frame Timing Editor widget.
 * Container that hosts the timeline, frame list, preview, and controls.
 * Integrates as a tab in the CharacterDataAssetEditor.
 */
class SFrameTimingEditor : public SCompoundWidget, public FEditorUndoClient
{
public:
	SLATE_BEGIN_ARGS(SFrameTimingEditor) {}
		SLATE_ARGUMENT(TWeakObjectPtr<UPaper2DPlusCharacterDataAsset>, Asset)
		SLATE_ARGUMENT(TSet<FName>*, CollapsedFlipbookGroups)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SFrameTimingEditor();

	// FEditorUndoClient
	virtual void PostUndo(bool bSuccess) override;
	virtual void PostRedo(bool bSuccess) override;

	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual bool SupportsKeyboardFocus() const override { return true; }

	// External control
	void SetSelectedFlipbook(int32 FlipbookIndex);
	void StopPlayback();

	/** Refresh all sub-widgets */
	void RefreshAll();

	// Delegates for parent editor notifications
	DECLARE_DELEGATE(FOnTimingDataModified);
	FOnTimingDataModified OnTimingDataModified;

private:
	TWeakObjectPtr<UPaper2DPlusCharacterDataAsset> Asset;
	TSet<FName>* CollapsedFlipbookGroups = nullptr;

	// Selection state
	int32 SelectedFlipbookIndex = 0;
	int32 SelectedFrameIndex = 0;

	// Display settings
	ETimingDisplayUnit DisplayUnit = ETimingDisplayUnit::Frames;

	// Playback state
	bool bIsPlaying = false;
	float PlaybackPosition = 0.0f; // Current playback time in seconds
	float PlaybackFPS = 12.0f;
	FTSTicker::FDelegateHandle PlaybackTickerHandle;

	// Sub-widgets
	TSharedPtr<SAnimationTimeline> TimelineWidget;
	TSharedPtr<SFrameDurationList> FrameDurationListWidget;
	TSharedPtr<SVerticalBox> FlipbookListBox;
	TSharedPtr<SVerticalBox> PreviewBox;
	TSharedPtr<STextBlock> StatsText;
	TSharedPtr<class SFramePreviewCanvas> PreviewCanvas;

	float PreviewZoom = 3.0f;

	// UI builders
	TSharedRef<SWidget> BuildToolbar();
	TSharedRef<SWidget> BuildFlipbookList();
	TSharedRef<SWidget> BuildPreviewPanel();

	// Refresh functions
	void RefreshFlipbookList();
	void RefreshFrameList();
	void RefreshPreview();
	void RefreshStats();

	// Event handlers
	void OnFlipbookSelected(int32 Index);
	void OnFrameSelected(int32 Index);
	void OnFrameDurationChanged(int32 FrameIndex, int32 NewDuration);
	void OnFPSChanged(float NewFPS);
	void OnDisplayUnitChanged(ETimingDisplayUnit NewUnit);

	// Batch operations
	void OnSetAllDurations(int32 Duration);
	void OnDistributeEvenly();
	void OnResetAllToOne();

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
	const FFlipbookHitboxData* GetCurrentFlipbookData() const;
	int32 GetCurrentFrameCount() const;
};
