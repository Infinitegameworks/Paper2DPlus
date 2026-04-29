// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "FrameEvents/Paper2DPlusFrameEventBase.h"
#include "AnimationTimeline.h"
#include "Containers/Ticker.h"
#include "Editor/EditorEngine.h"
#include "IDetailsView.h"

class SVerticalBox;
class SHorizontalBox;
class SScrollBox;
class SFrameEventPreviewCanvas;
class SFrameEventTimelineTrack;
class UPaper2DPlusFrameEvent;
class UPaper2DPlusSpawnEffectFrameEvent;

/** Tracks an active effect preview spawned during editor playback. */
struct FEditorEffectPreview
{
	TWeakObjectPtr<UPaper2DPlusSpawnEffectFrameEvent> SourceEvent;
	double StartTime = 0.0;       // PlaybackTime when triggered
	float Duration = 0.0f;        // Effect flipbook total duration in seconds
};

/**
 * Frame Events editor tab. Standardized layout matching SRootMotionEditor:
 * Left: flipbook list with thumbnails + event count badges.
 * Center: sprite preview canvas (SFrameEventPreviewCanvas) + horizontal frame strip with event indicators.
 * Right: event list toolbar (Add/Remove), event list, and IDetailsView for selected event.
 */
class SFrameEventEditor : public SCompoundWidget, public FEditorUndoClient
{
public:
	SLATE_BEGIN_ARGS(SFrameEventEditor) {}
		SLATE_ARGUMENT(TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset>, Asset)
		SLATE_ARGUMENT(TFunction<void(TSharedPtr<SVerticalBox>, TFunction<TSharedRef<SWidget>(int32)>)>, BuildFlipbookListFunc)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SFrameEventEditor();

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
	DECLARE_DELEGATE_OneParam(FOnFlipbookSelectedInList, int32);
	FOnFlipbookSelectedInList OnFlipbookSelectedInList;

private:
	TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset> Asset;

	// Selection state
	int32 SelectedFlipbookIndex = INDEX_NONE;
	int32 SelectedFrameIndex = 0;
	int32 SelectedEventIndex = INDEX_NONE;

	// Playback state
	bool bIsPlaying = false;
	double PlaybackTime = 0.0;
	FFlipbookTimingData CachedTiming;
	FTSTicker::FDelegateHandle PlaybackTickerHandle;

	// Editor effect preview state
	TSharedPtr<TArray<FEditorEffectPreview>> ActiveEditorEffects;
	int32 PreviousPlaybackFrame = INDEX_NONE;

	bool bNeedsRefresh = false;

	// Flipbook list builder
	TFunction<void(TSharedPtr<SVerticalBox>, TFunction<TSharedRef<SWidget>(int32)>)> BuildFlipbookListFunc;

	// Transaction helpers
	TUniquePtr<FScopedTransaction> ActiveTransaction;
	void BeginTransaction(const FText& Description);
	void EndTransaction();

	// Sub-widgets
	TSharedPtr<SVerticalBox> FlipbookListBox;
	TSharedPtr<SHorizontalBox> FrameStripBox;
	TSharedPtr<SVerticalBox> EventListBox;
	TSharedPtr<IDetailsView> EventDetailsView;
	TSharedPtr<SFrameEventPreviewCanvas> PreviewCanvasWidget;
	TSharedPtr<SFrameEventTimelineTrack> TimelineTrack;

	// UI builders
	TSharedRef<SWidget> BuildToolbar();

	// Refresh
	void RefreshFrameStrip();
	void RefreshEventList();
	void RefreshDetailsPanel();

	// Event management
	void ShowAddEventPicker();
	void CreateNewEventBlueprint(UClass* ParentClass);
	void AddFrameEvent(UClass* EventClass);
	void RemoveEvent(int32 EventIndex);
	void OnEventSelected(int32 EventIndex);

	// Frame selection
	void OnFrameClicked(int32 FrameIndex);

	// Playback
	void TogglePlayback();
	bool OnPlaybackTick(float DeltaTime);
	int32 GetFrameFromTime() const;
	void DispatchEditorPreview(UPaper2DPlusFrameEvent* Event);

	// Helpers
	FFlipbookProfileEntry* GetSelectedFlipbookData() const;
	UPaperFlipbook* GetSelectedFlipbook() const;
	int32 GetFrameCount() const;
	TArray<TObjectPtr<UPaper2DPlusFrameEventBase>>* GetFrameEventsArray() const;
};
