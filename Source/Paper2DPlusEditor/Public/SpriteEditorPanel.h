// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "ScopedTransaction.h"
#include "Containers/Ticker.h"
#include "AnimationTimeline.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "ProfileToolPanelProvider.h"

class UPaperSprite;
class UPaperFlipbook;
class FCharacterProfileEditorModel;
class SSpriteEditorCanvas;
class SHorizontalBox;

namespace Paper2DPlusEditor::SpriteEditorPanelUtils
{
	int32 PrepareFrameStripFrameCount(UPaper2DPlusCharacterProfileAsset* Asset, int32 FlipbookIndex);
}

namespace Paper2DPlusEditor::DirectionalPreviewPlayback
{
	/** Playback inputs shared by every Character Profile tool that owns a local preview ticker. */
	enum class EEvent : uint8
	{
		BeginResolving,
		BecameEmpty,
		BecameRenderable,
		BecameUnavailable,
		UserToggle,
		UserToggleWhilePaused
	};

	/** Desired caller-visible playback state after one directional-preview input. */
	struct FDecision
	{
		bool bShouldBePlaying = false;
		bool bResumeAfterDirectionalPreviewResolves = false;
	};

	/**
	 * Pure transition shared by Sprite, Frame Timing, Frame Cues, and Root Motion previews.
	 * Empty and Resolving pause without discarding an earlier play request. A user toggle while
	 * either preview state is paused explicitly cancels that queued automatic resume.
	 */
	PAPER2DPLUSEDITOR_API FDecision Resolve(
		bool bIsPlaying,
		bool bResumeAfterDirectionalPreviewResolves,
		EEvent Event);
}

/**
 * Independent panel for the Sprite Editor tab.
 * Owns all sprite editing, playback queue, onion skin, and offset tools.
 * Subscribes to FCharacterProfileEditorModel delegates for cross-panel sync.
 */
class SSpriteEditorPanel : public SCompoundWidget, public FEditorUndoClient, public IProfileToolPanelProvider
{
public:
	static const FName OffsetNudgePanelId;
	static const FName OnionSkinsPanelId;
	static const FName BatchPanelId;

	SLATE_BEGIN_ARGS(SSpriteEditorPanel)
		: _HostContract(FProfileToolPanelHostContract::Embedded())
	{}
		SLATE_ARGUMENT(TSharedPtr<FCharacterProfileEditorModel>, Model)
		SLATE_ARGUMENT(FProfileToolPanelHostContract, HostContract)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SSpriteEditorPanel();

	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnPreviewKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

	// IProfileToolPanelProvider — Character workspaces mount fresh contextual views over this controller.
	virtual FProfileToolPanelHostContract GetHostContract() const override { return HostContract; }
	virtual void GetContextualPanels(TArray<FProfileToolPanelDescriptor>& OutPanels) const override;

	/** Stop playback and close any canvas/nudge gesture exactly once before another tool becomes active. */
	void HandleHostDeactivated();

	// --- Public API for parent ---

	/** Refresh all sub-widgets. Called by parent on tab switch or dirty-tab mechanism. */
	void RefreshAll();

	/** Stop playback if running. Called by parent when switching away from this tab. */
	void StopPlayback();

	/** Whether this panel has an active transaction (used by parent to skip external-modify refresh). */
	bool HasActiveTransaction() const { return ActiveTransaction.IsValid(); }

	// Focused U23 acceptance seams. They invoke the production commands rather than shadow logic.
	const FProfileToolPanelHostContract& GetHostContractForTests() const { return HostContract; }
	int32 GetContextPanelBuildCountForTests(FName PanelId) const
	{
		return ContextPanelBuildCounts.FindRef(PanelId);
	}
	int32 GetContextPanelResolvedFrameForTests(FName PanelId) const
	{
		return ContextPanelResolvedFrames.FindRef(PanelId);
	}
	FIntPoint GetCurrentOffsetForTests() const;
	void SetOffsetXForTests(int32 Value) { OnOffsetXChanged(Value); }
	void SetOffsetYForTests(int32 Value) { OnOffsetYChanged(Value); }
	void NudgeOffsetForTests(int32 DeltaX, int32 DeltaY) { NudgeOffset(DeltaX, DeltaY); }
	void CommitPendingEditForTests();
	void CopyOffsetForTests() { OnCopyOffset(); }
	void PasteOffsetForTests() { OnPasteOffset(); }
	void ResetOffsetForTests() { OnResetOffset(); }
	void ConfigureBatchForTests(int32 ActionIndex, int32 TargetIndex, FIntPoint CustomValue);
	void ApplyBatchForTests() { OnApplyAlignmentBatchOperation(); }
	void SetOnionSkinsStateForTests(
		bool bInOnion,
		bool bInForwardOnion,
		int32 InOnionFrames,
		float InOnionOpacity,
		int32 InReferenceFlipbook,
		int32 InReferenceFrame,
		float InReferenceOpacity);
	bool IsCanvasShowingOnionForTests() const;
	bool IsCanvasShowingReferenceForTests() const;
	static bool IsShortcutProtectedWidgetTypeForTests(const FString& WidgetTypeName);
	int32 GetTransactionBeginCountForTests() const { return TransactionBeginCount; }
	int32 GetTransactionEndCountForTests() const { return TransactionEndCount; }
	bool HasNudgeDebounceForTests() const { return NudgeDebounceTimer.IsValid(); }

private:
	TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset> Asset;
	TSharedPtr<FCharacterProfileEditorModel> Model;
	FProfileToolPanelHostContract HostContract;

	// Delegate handles for model subscriptions
	FDelegateHandle ModelFlipbookSelectionHandle;
	FDelegateHandle ModelFrameSelectionHandle;
	FDelegateHandle ModelGroupCollapseHandle;
	FDelegateHandle ModelSearchTextHandle;
	FDelegateHandle ModelAssetDataChangedHandle;
	FDelegateHandle ModelAssetExternallyModifiedHandle;
	FDelegateHandle ModelDirectionalPreviewHandle;

	// --- Sprite Editor local state (moved from parent) ---

	// Zoom / reticle
	float SpriteEditorZoomLevel = 1.0f;
	bool bShowSpriteEditorReticle = false;
	FVector2D SpriteEditorReticlePos = FVector2D::ZeroVector;
	bool bSpriteEditorDragActive = false;
	TWeakPtr<FActiveTimerHandle> NudgeDebounceTimer;
	void CommitNudgeTransaction();
	EActiveTimerReturnType HandleNudgeDebounceTimer(double CurrentTime, float DeltaTime);

	// Flip state
	bool bSpriteFlipX = false;
	bool bSpriteFlipY = false;

	// Playback
	bool bIsPlaying = false;
	bool bResumeAfterDirectionalPreviewResolves = false;
	FTSTicker::FDelegateHandle PlaybackTickerHandle;
	float PlaybackPosition = 0.0f;
	FFlipbookTimingData CachedPlaybackTiming;

	// Queue delegate handle
	FDelegateHandle ModelQueuePlaybackStateHandle;

	// Onion skin
	bool bShowOnionSkin = false;
	bool bShowForwardOnionSkin = false;
	int32 OnionSkinFrames = 1;
	int32 ForwardOnionSkinFrames = 1;
	float OnionSkinOpacity = 0.4f;

	// Ping-pong playback
	bool bPingPongPlayback = false;
	bool bPlaybackReversed = false;

	// Reference sprite
	bool bShowReferenceSprite = false;
	int32 ReferenceFlipbookIndex = INDEX_NONE;
	int32 ReferenceFrameIndex = INDEX_NONE;
	float ReferenceSpriteOpacity = 0.4f;

	// Offset clipboard
	FIntPoint CopiedOffset = FIntPoint::ZeroValue;
	bool bHasCopiedOffset = false;

	// Alignment batch
	int32 AlignBatchActionIndex = 0;
	int32 AlignBatchTargetIndex = 0;
	FIntPoint AlignBatchCustomValue = FIntPoint::ZeroValue;
	int32 AlignBatchRangeStart = 0;
	int32 AlignBatchRangeEnd = 0;
	// Persistent sources: contextual close/reopen must never leave an SComboBox pointing at a temporary.
	TArray<TSharedPtr<FString>> PlaybackModeOptions;
	TArray<TSharedPtr<FString>> AlignActionOptions;
	TArray<TSharedPtr<FString>> AlignTargetOptions;

	// Widget references
	TSharedPtr<SSpriteEditorCanvas> SpriteEditorCanvas;
	TSharedPtr<SHorizontalBox> SpriteEditorFrameListBox;

	// Frame drag-reorder gesture state. Owned by the panel rather than the per-cell
	// SFrameDragDropWrapper because selecting a frame on mouse-down rebuilds the strip and
	// destroys the wrapper mid-gesture — the arm must outlive it. The rebuilt wrapper under
	// the cursor reads this state back via its OnDragArmFunc/OnDragDetectFunc callbacks.
	bool bFrameDragArmed = false;
	FVector2D FrameDragStartScreenPos = FVector2D::ZeroVector;
	int32 FrameDragSourceIndex = INDEX_NONE;

	// Active transaction for undo support
	TUniquePtr<FScopedTransaction> ActiveTransaction;
	int32 TransactionBeginCount = 0;
	int32 TransactionEndCount = 0;
	mutable TMap<FName, int32> ContextPanelBuildCounts;
	mutable TMap<FName, int32> ContextPanelResolvedFrames;

	// --- FEditorUndoClient ---
	virtual void PostUndo(bool bSuccess) override;
	virtual void PostRedo(bool bSuccess) override;

	// --- Transaction helpers ---
	void BeginTransaction(const FText& Description);
	void EndTransaction();
	void FinishActiveEditGesture(bool bReleaseCanvasCapture = false);
	void HandleModelFrameSelectionChanged();

	// --- UI Builders ---
	TSharedRef<SWidget> BuildCentralWorkspace();
	TSharedRef<SWidget> BuildEmbeddedContextStack();
	TSharedRef<SWidget> BuildSpriteEditorToolbar();
	TSharedRef<SWidget> BuildSpriteEditorFrameList();
	TSharedRef<SWidget> BuildSpriteEditorCanvasArea();
	TSharedRef<SWidget> BuildOffsetNudgePanel();
	TSharedRef<SWidget> BuildOnionSkinsPanel();
	TSharedRef<SWidget> BuildBatchPanel();
	TSharedRef<SWidget> BuildSpriteEditorRestoreExcludedMenu();

	// --- Refresh ---
	void RefreshSpriteEditorFrameList();
	void RefreshCurrentFrameFlipState();
	void RefreshAfterFrameExclusion(bool bDismissMenus = false);
	void InvalidateSpriteEditorCanvas();

	// --- Selection ---
	void OnFlipbookSelected(int32 Index);
	void OnFrameSelected(int32 Index);

	// --- Offset operations ---
	void NudgeOffset(int32 DeltaX, int32 DeltaY);
	void OnOffsetXChanged(int32 NewValue);
	void OnOffsetYChanged(int32 NewValue);
	void OnCopyOffset();
	void OnPasteOffset();
	void OnResetOffset();
	void OnApplyAlignmentBatchOperation();
	void OnSpriteEditorOffsetChanged(int32 DeltaX, int32 DeltaY);

	// --- Frame exclude/restore ---
	void OnExcludeCurrentSpriteEditorFrame();
	void OnRestoreExcludedSpriteEditorFrame(int32 ExcludedFrameIndex);
	void OnRestoreAllExcludedSpriteEditorFrames();
	bool CanExcludeCurrentSpriteEditorFrame() const;

	// --- Frame reorder (drag-drop in the frame strip) ---
	void OnReorderSpriteEditorFrame(int32 FromIndex, int32 ToIndex);

	// --- Playback ---
	void StartPlayback();
	void TogglePlayback();
	bool OnPlaybackTick(float DeltaTime);
	int32 FrameIndexFromPlaybackPosition(const FFlipbookTimingData& Timing, float Position) const;

	// --- Reference sprite ---
	void SetReferenceSprite(int32 FlipbookIndex, int32 FrameIndex);
	void ClearReferenceSprite();

	// --- Helpers ---
	const FFlipbookProfileEntry* GetCurrentFlipbookData() const;
	FFlipbookProfileEntry* GetCurrentFlipbookDataMutable();
	int32 GetCurrentFrameCount() const;
	UPaperFlipbook* GetPreviewFlipbook() const;
	UPaperSprite* GetCurrentSprite() const;

	friend class FPaper2DPlusDirectionalCrossToolPreviewArtTest;

	// Frame multi-select helpers
	void ClearFrameSelection();

};
