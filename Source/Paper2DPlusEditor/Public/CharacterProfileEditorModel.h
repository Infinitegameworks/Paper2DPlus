// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/WeakObjectPtrTemplates.h"
#include "Containers/Ticker.h"

class UPaper2DPlusCharacterProfileAsset;
class UPaper2DPlusCharacterLayerAsset;
class UPaperFlipbook;
class FTabManager;
class SWindow;

// Delegate signatures
DECLARE_MULTICAST_DELEGATE_OneParam(FOnEditorModelFlipbookSelectionChanged, int32 /*NewIndex*/);
DECLARE_MULTICAST_DELEGATE(FOnEditorModelFrameSelectionChanged);
DECLARE_MULTICAST_DELEGATE(FOnEditorModelGroupCollapseChanged);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnEditorModelSearchTextChanged, const FString& /*NewText*/);
DECLARE_MULTICAST_DELEGATE(FOnEditorModelAssetExternallyModified);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnEditorModelCompletionFilterChanged, int32 /*NewMask*/);
DECLARE_MULTICAST_DELEGATE(FOnEditorModelAssetDataChanged);
DECLARE_MULTICAST_DELEGATE(FOnEditorModelQueueChanged);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnEditorModelQueuePlaybackStateChanged, bool /*bIsPlaying*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnEditorModelLayerSelectionChanged, int32 /*NewIndex*/);
DECLARE_MULTICAST_DELEGATE(FOnEditorModelLayerVisibilityChanged);
DECLARE_MULTICAST_DELEGATE(FOnEditorModelTransitionSelectionChanged);

/**
 * Shared model for the Character Profile Editor.
 * Owns all cross-panel state and mediates communication via multicast delegates.
 * Created by the toolkit, passed to the parent widget as a SLATE_ARGUMENT.
 */
class FCharacterProfileEditorModel : public TSharedFromThis<FCharacterProfileEditorModel>
{
public:
	FCharacterProfileEditorModel();
	~FCharacterProfileEditorModel();

	void InitializeFromAsset(UPaper2DPlusCharacterProfileAsset* InAsset);
	void SetTabManager(TWeakPtr<FTabManager> InTabManager);

	// --- Asset access ---
	UPaper2DPlusCharacterProfileAsset* GetAsset() const;

	// --- Secondary watched object ---
	// Some editor surfaces derive live state from one asset beside the primary Profile: the Layer
	// workspace watches its Layer asset, while the Expected Tags panel watches the authoritative
	// Catalog it resolved. Registering either routes external Modify()s of that object (or its
	// subobjects) through the SAME deferred OnAssetExternallyModified broadcast discipline the
	// Profile uses. Optional and weak; callers replace/clear only the object they installed.
	void SetSecondaryWatchedObject(UObject* InObject);
	UObject* GetSecondaryWatchedObject() const { return SecondaryWatchedObject.Get(); }

	// --- Flipbook selection ---
	void SetSelectedFlipbook(int32 NewIndex);
	int32 GetSelectedFlipbookIndex() const { return SelectedFlipbookIndex; }

	// --- Frame selection ---
	void SetSelectedFrame(int32 NewIndex);
	int32 GetSelectedFrameIndex() const { return SelectedFrameIndex; }

	void SetSelectedExcludedFrame(int32 NewIndex);
	int32 GetSelectedExcludedFrameIndex() const { return SelectedExcludedFrameIndex; }

	void HandleFrameClick(int32 ClickedIndex, bool bCtrl, bool bShift, int32 TotalFrameCount);
	const TSet<int32>& GetSelectedFrames() const { return SelectedFrames; }
	int32 GetFrameSelectionAnchorIndex() const { return FrameSelectionAnchorIndex; }
	void ClearFrameSelection();

	// --- Card multi-select ---
	const TSet<int32>& GetSelectedFlipbookCards() const { return SelectedFlipbookCards; }
	TSet<int32>& GetSelectedFlipbookCardsMutable() { return SelectedFlipbookCards; }
	int32 GetSelectionAnchorIndex() const { return SelectionAnchorIndex; }
	void SetSelectionAnchorIndex(int32 Index) { SelectionAnchorIndex = Index; }

	// --- Group collapse ---
	const TSet<FName>& GetCollapsedFlipbookGroups() const { return CollapsedFlipbookGroups; }
	TSet<FName>& GetCollapsedFlipbookGroupsMutable() { return CollapsedFlipbookGroups; }
	void ToggleGroupCollapse(FName GroupName);
	void NotifyGroupCollapseChanged();

	// --- Search ---
	const FString& GetFlipbookGroupSearchText() const { return FlipbookGroupSearchText; }
	void SetFlipbookGroupSearchText(const FString& NewText);

	const TSet<FName>& GetPreSearchCollapsedState() const { return PreSearchCollapsedState; }
	TSet<FName>& GetPreSearchCollapsedStateMutable() { return PreSearchCollapsedState; }

	// --- Completion filter ---
	int32 GetCompletionFilterMask() const { return CompletionFilterMask; }
	void SetCompletionFilterMask(int32 NewMask);

	// --- View mode ---
	bool IsFlipbookGroupGridView() const { return bFlipbookGroupGridView; }
	// Grid (cards) vs List (rows) for the flipbook browser. Driven by the Animations tab's top-level
	// Grid/List segments — the nested per-group sub-toggle was promoted to the tab header. Broadcasts so the
	// browser refreshes its render when the choice changes from outside the panel.
	void SetFlipbookGroupGridView(bool bGrid)
	{
		if (bFlipbookGroupGridView != bGrid)
		{
			bFlipbookGroupGridView = bGrid;
			OnFlipbookGroupViewModeChanged.Broadcast();
		}
	}

	// --- Pending scroll ---
	FName GetPendingScrollToGroup() const { return PendingScrollToGroup; }
	void SetPendingScrollToGroup(FName GroupName) { PendingScrollToGroup = GroupName; }

	// --- Flipbook identity ---
	FName GetSelectedFlipbookName() const { return SelectedFlipbookName; }
	TWeakObjectPtr<UPaperFlipbook> GetSelectedFlipbookObject() const { return SelectedFlipbookObject; }
	/** Stable soft path survives unloaded assets and array reorder; name remains the legacy fallback. */
	const FSoftObjectPath& GetSelectedFlipbookPath() const { return SelectedFlipbookPath; }

	// --- Navigation ---
	TArray<int32> GetVisualFlipbookOrder() const;
	int32 GetVisualAdjacentFlipbookIndex(int32 Direction) const;
	TArray<int32> GetSortedFlipbookIndices() const;

	// --- Playback queue ---
	// The queue cursor (PlaybackQueueIndex) and the flipbook selection are two views of one position.
	// Selection can change from anywhere (browser card, navigator, Animation Map node, undo), so the
	// cursor is RECONCILED against the selection rather than trusted on its own — see
	// FindQueuePositionForSelection. Panels must navigate the queue through StepQueue, never by
	// hand-rolling "read cursor, add direction, set cursor" (six divergent copies of that is what made
	// arrow navigation stick on one entry).
	const TArray<int32>& GetPlaybackQueue() const { return PlaybackQueue; }
	int32 GetPlaybackQueueIndex() const { return PlaybackQueueIndex; }
	void SetPlaybackQueueIndex(int32 NewIndex);
	bool IsQueueActive() const { return PlaybackQueue.Num() > 0; }
	bool IsQueuePlaying() const { return bIsQueuePlaying; }
	void SetQueuePlaying(bool bPlaying);

	void AddToQueue(int32 FlipbookIndex);
	void RemoveFromQueue(int32 QueueIndex);
	void ReorderQueueEntry(int32 FromIndex, int32 ToIndex);
	void ClearQueue();
	void PurgeInvalidQueueEntries();
	/** Rebase transient queue indices after the profile removes one Flipbooks entry, then select the
	 *  caller-chosen surviving neighbor. Call after the array mutation and before asset-data notify. */
	void HandleFlipbookRemoved(int32 RemovedFlipbookIndex, int32 PreferredSelectionIndex);

	/**
	 * Flipbook one step away in queue order, or INDEX_NONE when there is nothing to step to — the
	 * queue holds fewer than two entries, or the SELECTED flipbook is not in the queue at all (the
	 * caller has navigated off the playlist, so queue neighbors are meaningless). Used for onion skin
	 * and for the wrap decision at a frame boundary; it does not move the cursor.
	 */
	int32 GetQueueAdjacentFlipbookIndex(int32 Direction) const;

	/**
	 * Queue slot holding the currently selected flipbook, or INDEX_NONE when the selection is off the
	 * queue. Prefers the current cursor when it already points at a matching entry, so a flipbook
	 * queued more than once keeps the position the caller actually stepped to instead of snapping back
	 * to its first occurrence. Read-only.
	 */
	int32 FindQueuePositionForSelection() const;

	/**
	 * Reconcile the cursor onto the selected flipbook and return the reconciled position. Off-queue
	 * selections return INDEX_NONE and leave the cursor untouched (a stale cursor is inert — every
	 * consumer reconciles first). Never broadcasts: the queue list repaints from the selection change.
	 */
	int32 SyncQueueIndexToSelection();

	/**
	 * THE queue-navigation entry point for every panel. Steps the cursor by Direction from the
	 * reconciled position, WRAPPING at both ends, selects that entry's flipbook, and lands the frame
	 * cursor on the last frame (bLandOnLastFrame, i.e. stepping backwards) or frame 0.
	 *
	 * Returns the newly selected flipbook index, or INDEX_NONE when the step is not possible — empty
	 * or single-entry queue, off-queue selection, or an entry that no longer resolves. INDEX_NONE is
	 * the caller's cue to fall back to non-queue behavior (wrap within the flipbook, or visual order).
	 */
	int32 StepQueue(int32 Direction, bool bLandOnLastFrame = false);

	/** Key-frame count of the selected flipbook; 0 when nothing is selected or the asset is unloaded. */
	int32 GetSelectedFlipbookFrameCount() const;

	// --- Layer state (used by layer editor, ignored by profile editor) ---
	int32 GetSelectedLayerIndex() const { return SelectedLayerIndex; }
	const FGuid& GetSelectedLayerId() const { return SelectedLayerId; }
	void SetSelectedLayer(int32 NewIndex);
	/** Stable-id selection used by the Layer-first tree/canvas. A stale id remains explicit, never a neighbor. */
	void SetSelectedLayerById(const FGuid& NewLayerId);
	/** Re-resolve the cached index after reorder/reimport without changing the selected identity. */
	void ReconcileLayerSelectionIdentity();
	bool IsLayerVisible(const FString& LayerName) const;
	void SetLayerVisibility(const FString& LayerName, bool bVisible);
	const TMap<FString, bool>& GetLayerVisibilityOverrides() const { return LayerVisibilityOverrides; }

	// --- Transition (edge) selection (TASK-108 U3) ---
	// The model-level selected-transition channel behind the details pane's edge mode: a (From, To)
	// VALUE key (lowered, case-insensitive — the FEdgeReselectKey shape), never a node pointer or row
	// index. An Animation Map edge click sets BOTH SetSelectedFlipbook(FromIndex) (tool tabs keep
	// following the From move) AND this key — in that order, because EVERY SetSelectedFlipbook call
	// clears the key up front (BEFORE its same-index no-op guard, so re-asserting the CURRENT index
	// through any selection path still exits edge mode — Codex P2, PR #224). The key is
	// allowed to go stale (the row can vanish under it) — consumers re-resolve per render and fall back;
	// the mutator's no-op guard therefore compares the KEY ALONE, even when it is unresolvable (the
	// TASK-96 rule: a validity-gated guard re-broadcasts forever when a panel mirrors the same stale key
	// back during the broadcast). ClearSelectedTransition when already clear is the same no-op.
	const FString& GetSelectedTransitionFromLower() const { return SelectedTransitionFromLower; }
	const FString& GetSelectedTransitionToLower() const { return SelectedTransitionToLower; }
	bool HasSelectedTransition() const { return !SelectedTransitionFromLower.IsEmpty() && !SelectedTransitionToLower.IsEmpty(); }
	void SetSelectedTransition(const FString& InFromMove, const FString& InToMove);
	void ClearSelectedTransition();

	// --- Asset data change ---
	void NotifyAssetDataChanged();

	// --- Tab management ---
	void BringTabToFront(FName TabId);

	/**
	 * The hosting toolkit's Playback Queue tab id, registered at init. Both the Character Profile and
	 * Layer toolkits host the queue under their own id, so "add to queue" affordances (which live in
	 * other panels) ask the model to reveal it rather than hard-coding one toolkit's tab.
	 */
	void SetPlaybackQueueTabId(FName TabId) { PlaybackQueueTabId = TabId; }
	/** Bring the queue tab to front, if the host registered one. Safe to call from any panel. */
	void RevealPlaybackQueue()
	{
		if (!PlaybackQueueTabId.IsNone()) BringTabToFront(PlaybackQueueTabId);
	}

	// --- Frame Data window ---
	// The read-only frame-data table lives in a floating window since the Frame Data tab retired
	// (layout _v8). Open focuses the existing window when one is already up. The hosted panel holds
	// a strong ref to this model, so the OWNING TOOLKIT must call CloseFrameDataWindow() from its
	// destructor — without that the window would outlive the editor and keep the model alive.
	void OpenFrameDataWindow();
	void CloseFrameDataWindow();

	// --- Mutation guard ---
	void BeginModelMutation();
	void EndModelMutation();

	// --- Delegates ---
	FOnEditorModelFlipbookSelectionChanged OnFlipbookSelectionChanged;
	FOnEditorModelFrameSelectionChanged OnFrameSelectionChanged;
	FOnEditorModelGroupCollapseChanged OnGroupCollapseChanged;
	FOnEditorModelSearchTextChanged OnSearchTextChanged;
	FOnEditorModelAssetExternallyModified OnAssetExternallyModified;
	FOnEditorModelCompletionFilterChanged OnCompletionFilterChanged;
	FOnEditorModelAssetDataChanged OnAssetDataChanged;
	FOnEditorModelQueueChanged OnQueueChanged;
	FOnEditorModelQueuePlaybackStateChanged OnQueuePlaybackStateChanged;
	FOnEditorModelLayerSelectionChanged OnLayerSelectionChanged;
	FOnEditorModelLayerVisibilityChanged OnLayerVisibilityChanged;
	/** Fired when the selected-transition key changes (set, re-key, or clear). Key via the getters. */
	FOnEditorModelTransitionSelectionChanged OnTransitionSelectionChanged;
	/** Fired when IsFlipbookGroupGridView flips (the Animations tab's top-level Grid/List segments). */
	FSimpleMulticastDelegate OnFlipbookGroupViewModeChanged;
	// --- Static accessor ---
	static TWeakPtr<FCharacterProfileEditorModel> GActiveEditorModel;

private:
	void ResolveFlipbookIdentity();
	/** Re-resolve path first/name second after reorder/rename/delete without selecting a neighbor. */
	void ReconcileFlipbookSelectionIdentity();
	void OnObjectModified(UObject* Object);
	bool DeferredExternalModifiedNotify(float DeltaTime);

	void BroadcastOrDefer(uint16 FlagBit, TFunction<void()> Broadcast);

	TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset> Asset;
	// Optional second OnObjectModified match target (Layer asset or authoritative Catalog).
	// See SetSecondaryWatchedObject.
	TWeakObjectPtr<UObject> SecondaryWatchedObject;
	TWeakPtr<FTabManager> TabManagerWeak;
	// Host-registered id for the Playback Queue tab; None until a toolkit registers one.
	FName PlaybackQueueTabId;

	// Floating Frame Data window (weak — the window owns itself; see OpenFrameDataWindow)
	TWeakPtr<SWindow> FrameDataWindow;

	// Selection state
	int32 SelectedFlipbookIndex = 0;
	int32 SelectedFrameIndex = 0;
	int32 SelectedExcludedFrameIndex = INDEX_NONE;

	// Frame multi-select
	TSet<int32> SelectedFrames;
	int32 FrameSelectionAnchorIndex = INDEX_NONE;

	// Card multi-select
	TSet<int32> SelectedFlipbookCards;
	int32 SelectionAnchorIndex = INDEX_NONE;

	// Group state
	TSet<FName> CollapsedFlipbookGroups;
	FString FlipbookGroupSearchText;
	int32 CompletionFilterMask = 0;
	bool bFlipbookGroupGridView = true;
	TSet<FName> PreSearchCollapsedState;
	FName PendingScrollToGroup;

	// Flipbook identity tracking
	FName SelectedFlipbookName;
	TWeakObjectPtr<UPaperFlipbook> SelectedFlipbookObject;
	FSoftObjectPath SelectedFlipbookPath;

	// Layer state (used by layer editor, defaults inactive for profile editor)
	int32 SelectedLayerIndex = INDEX_NONE;
	FGuid SelectedLayerId;
	TMap<FString, bool> LayerVisibilityOverrides;

	// Transition (edge) selection — lowered (From, To) value key; both empty = no edge selected
	FString SelectedTransitionFromLower;
	FString SelectedTransitionToLower;

	// Playback queue
	TArray<int32> PlaybackQueue;
	int32 PlaybackQueueIndex = 0;
	bool bIsQueuePlaying = false;

	// Mutation guard
	int32 MutationDepth = 0;
	uint16 PendingDelegateFlags = 0;

	// Re-entrancy guard
	bool bIsBroadcasting = false;
	TArray<TFunction<void()>> DeferredBroadcasts;

	// OnObjectModified
	FDelegateHandle OnObjectModifiedHandle;
	bool bExternalModifyPending = false;
	FTSTicker::FDelegateHandle ExternalModifyTickerHandle;
};

/** RAII helper for batching model mutations. */
struct FScopedEditorModelMutation
{
	explicit FScopedEditorModelMutation(TSharedPtr<FCharacterProfileEditorModel> InModel)
		: Model(InModel)
	{
		if (Model.IsValid())
		{
			Model->BeginModelMutation();
		}
	}

	~FScopedEditorModelMutation()
	{
		if (Model.IsValid())
		{
			Model->EndModelMutation();
		}
	}

	FScopedEditorModelMutation(const FScopedEditorModelMutation&) = delete;
	FScopedEditorModelMutation& operator=(const FScopedEditorModelMutation&) = delete;

private:
	TSharedPtr<FCharacterProfileEditorModel> Model;
};
