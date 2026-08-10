// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Containers/Ticker.h"
#include "AnimationTimeline.h"

class FCharacterProfileEditorModel;
class UPaper2DPlusCharacterProfileAsset;
class SVerticalBox;

/**
 * The playback queue: an ordered playlist of animations with transport controls, drag-to-add,
 * drag-to-reorder, and looping or ping-pong playback.
 *
 * Queue STATE lives on FCharacterProfileEditorModel — it is shared with every tool tab's arrow-key
 * navigation, so this panel is one view of it, not its owner. What this panel does own is the UI and
 * the playback ticker (which drives the model's flipbook/frame selection; the tool tabs then follow
 * through the ordinary selection delegates).
 *
 * It is a docked sibling of Completion and Related Profiles in the lower-right stack. It was
 * previously a collapsed-by-default expander inside the optional Navigator sidebar, which itself
 * ships as a closed tab — two disclosures deep for a surface designers use constantly.
 */
class SPlaybackQueuePanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SPlaybackQueuePanel) {}
		SLATE_ARGUMENT(TSharedPtr<FCharacterProfileEditorModel>, Model)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SPlaybackQueuePanel();

	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

	void RefreshQueueList();
	void StopQueuePlayback();

#if WITH_DEV_AUTOMATION_TESTS
	int32 GetQueueRowCountForTests() const;
#endif

private:
	TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset> Asset;
	TSharedPtr<FCharacterProfileEditorModel> Model;

	TSharedPtr<SVerticalBox> QueueListBox;

	// Playback state. Transient by design — the queue is a preview playlist, never authored data.
	FTSTicker::FDelegateHandle PlaybackTickerHandle;
	float PlaybackPosition = 0.0f;
	FFlipbookTimingData CachedPlaybackTiming;
	bool bPingPongPlayback = false;
	bool bPlaybackReversed = false;

	FDelegateHandle ModelFlipbookSelectionHandle;
	FDelegateHandle ModelAssetDataChangedHandle;
	FDelegateHandle ModelAssetExternallyModifiedHandle;
	FDelegateHandle ModelQueueChangedHandle;
	FDelegateHandle ModelQueuePlaybackStateHandle;

	void StartQueuePlayback();
	void ToggleQueuePlayback();
	bool OnQueuePlaybackTick(float DeltaTime);
	int32 FrameIndexFromPlaybackPosition(const FFlipbookTimingData& Timing, float Position) const;
	void ShowQueueEntryContextMenu(int32 QueueIndex);

	TSharedRef<SWidget> BuildTransportBar();
};
