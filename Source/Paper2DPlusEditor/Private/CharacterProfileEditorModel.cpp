// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "CharacterProfileEditorModel.h"
#include "FrameDataPanel.h"
#include "Paper2DPlusCharacterLayerAsset.h"
#include "Paper2DPlusAppearanceResolver.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "PaperFlipbook.h"
#include "Framework/Docking/TabManager.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWindow.h"

TWeakPtr<FCharacterProfileEditorModel> FCharacterProfileEditorModel::GActiveEditorModel;

// Pending delegate flag bits for coalesced broadcast after EndModelMutation
namespace EditorModelDelegateFlags
{
	constexpr uint16 FlipbookSelection    = 1 << 0;
	constexpr uint16 FrameSelection       = 1 << 1;
	constexpr uint16 GroupCollapse         = 1 << 2;
	constexpr uint16 SearchText           = 1 << 3;
	constexpr uint16 ExternalModified     = 1 << 4;
	// (1 << 5 was PhaseGroup — retired with the phase-groups feature, legacy-cleanup 2026-07.)
	constexpr uint16 CompletionFilter     = 1 << 6;
	constexpr uint16 AssetData            = 1 << 7;
	constexpr uint16 QueueChanged         = 1 << 8;
	constexpr uint16 QueuePlaybackState   = 1 << 9;
	constexpr uint16 LayerSelection       = 1 << 10;
	constexpr uint16 LayerVisibility      = 1 << 12;
	constexpr uint16 TransitionSelection  = 1 << 14;
}

FCharacterProfileEditorModel::FCharacterProfileEditorModel()
{
	OnObjectModifiedHandle = FCoreUObjectDelegates::OnObjectModified.AddRaw(this, &FCharacterProfileEditorModel::OnObjectModified);
}

FCharacterProfileEditorModel::~FCharacterProfileEditorModel()
{
	FCoreUObjectDelegates::OnObjectModified.Remove(OnObjectModifiedHandle);

	if (ExternalModifyTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(ExternalModifyTickerHandle);
		ExternalModifyTickerHandle.Reset();
	}

	if (GActiveEditorModel.Pin().Get() == this)
	{
		GActiveEditorModel.Reset();
	}
}

void FCharacterProfileEditorModel::InitializeFromAsset(UPaper2DPlusCharacterProfileAsset* InAsset)
{
	const bool bSameAsset = Asset.Get() == InAsset;
	const FSoftObjectPath PreviousPath = SelectedFlipbookPath;
	const FName PreviousName = SelectedFlipbookName;
	Asset = InAsset;
	if (!bSameAsset)
	{
		SelectedTransitionFromLower.Reset();
		SelectedTransitionToLower.Reset();
	}

	if (InAsset && InAsset->Flipbooks.Num() > 0)
	{
		// Ensure every flipbook's per-frame data arrays (CombatData.Frames in particular) are sized to match
		// the flipbook's key frames before any tab reads them. Profiles created by the layered Aseprite import
		// assign flipbooks WITHOUT sizing Frames, which would leave the Hitbox tab unable to author or display
		// hitboxes (GetCurrentFrameMutable returns null on an empty Frames array). This is an in-memory repair
		// of incomplete data — SetNum does not mark the package dirty, so opening an editor never dirties a
		// well-formed asset (the resize is a no-op there) and only grows arrays where rows were missing.
		// GROW-ONLY: a passive on-open repair must never SHRINK — a profile whose per-frame data is longer than
		// the flipbook (e.g. key frames removed externally) would otherwise be silently truncated and that loss
		// could be persisted by a later unrelated save. Exact (grow+shrink) sync stays at explicit assignment sites.
		InAsset->SyncAllFramesToFlipbooks(/*bGrowOnly=*/true);

		// A same-asset reinitialization (layout rebuild / external editor retarget) preserves the stable
		// selection. A genuinely different profile begins at its first canonical row. Missing identities
		// never fall through to a neighboring row.
		SelectedFlipbookIndex = 0;
		if (bSameAsset)
		{
			SelectedFlipbookPath = PreviousPath;
			SelectedFlipbookName = PreviousName;
			ReconcileFlipbookSelectionIdentity();
			if (SelectedFlipbookIndex == INDEX_NONE)
			{
				// The old item was deleted. Keep no selection; do not silently choose row zero.
				SelectedFlipbookCards.Reset();
				SelectionAnchorIndex = INDEX_NONE;
			}
		}
		if (SelectedFlipbookIndex != INDEX_NONE)
		{
			SelectedFlipbookCards.Reset();
			SelectedFlipbookCards.Add(SelectedFlipbookIndex);
			SelectionAnchorIndex = SelectedFlipbookIndex;
			ResolveFlipbookIdentity();
		}
	}
	else
	{
		// Null/empty profile is a real state in the Layer editor. Clear every identity atomically so a
		// later popup cannot act on a stale index from the previously attached profile.
		SelectedFlipbookIndex = INDEX_NONE;
		SelectedFlipbookCards.Reset();
		SelectionAnchorIndex = INDEX_NONE;
		SelectedFrameIndex = 0;
		SelectedExcludedFrameIndex = INDEX_NONE;
		SelectedFrames.Reset();
		FrameSelectionAnchorIndex = INDEX_NONE;
		ResolveFlipbookIdentity();
	}

	// Notify panels that the underlying asset changed. At editor init this fires before any panel has
	// subscribed (harmless); on a later re-init — e.g. the Character Layer editor's Base Profile picker
	// swapping the profile — it lets panels that cache the asset (SHitboxEditorPanel) re-resolve it.
	OnAssetDataChanged.Broadcast();
}

void FCharacterProfileEditorModel::SetTabManager(TWeakPtr<FTabManager> InTabManager)
{
	TabManagerWeak = InTabManager;
}

void FCharacterProfileEditorModel::SetSecondaryWatchedObject(UObject* InObject)
{
	SecondaryWatchedObject = InObject;
	ReconcileLayerSelectionIdentity();
}

UPaper2DPlusCharacterProfileAsset* FCharacterProfileEditorModel::GetAsset() const
{
	return Asset.Get();
}

// ==========================================
// Flipbook Selection
// ==========================================

void FCharacterProfileEditorModel::SetSelectedFlipbook(int32 NewIndex)
{
	UPaper2DPlusCharacterProfileAsset* AssetPtr = Asset.Get();
	if (!AssetPtr) return;

	// ANY flipbook-selection intent exits edge mode (TASK-108 U3; Codex P2, PR #224) — hoisted ABOVE
	// the same-index no-op guard so every SetSelectedFlipbook path (sidebar rows, queue playback,
	// arrow-key nav, tool-tab lists) clears an active edge selection even when it re-asserts the
	// CURRENT index. ClearSelectedTransition is a silent no-op when nothing is set and broadcasts the
	// TRANSITION channel exactly once when it clears — the FLIPBOOK channel below is untouched, so the
	// TASK-96 rule (same index never re-broadcasts the flipbook channel) stays intact. The Animation
	// Map's edge click stays correct because it orders SetSelectedFlipbook(From) FIRST, then
	// SetSelectedTransition(From, To) — this clear only ever wipes an OLD key, never one being set.
	ClearSelectedTransition();

	// No-op guard: re-selecting the SAME index is a no-op. For a valid index we additionally require the
	// resolved identity to be unchanged; for an INVALID (out-of-range) index there is nothing to
	// re-resolve, so the same index always no-ops. The invalid-index case is load-bearing: panels that
	// MIRROR the model selection back via SetSelectedFlipbook (e.g. an SListView selection-sync) would
	// otherwise re-broadcast forever on an out-of-range index (the identity check never matches because
	// there is no entry), hanging the editor's game thread in BroadcastOrDefer's drain loop.
	if (NewIndex == SelectedFlipbookIndex)
	{
		if (!AssetPtr->Flipbooks.IsValidIndex(NewIndex))
		{
			return;
		}
		const FFlipbookProfileEntry& Entry = AssetPtr->Flipbooks[NewIndex];
		UPaperFlipbook* FB = Entry.Identity.Flipbook.Get();
		FName Name = *Entry.Identity.FlipbookName;
		const FSoftObjectPath Path = Entry.Identity.Flipbook.ToSoftObjectPath();
		if (Name == SelectedFlipbookName && Path == SelectedFlipbookPath && FB == SelectedFlipbookObject.Get())
		{
			return;
		}
	}

	SelectedFlipbookIndex = NewIndex;

	// Resolve identity
	ResolveFlipbookIdentity();

	// Clamp frame index to new flipbook's frame count
	if (AssetPtr->Flipbooks.IsValidIndex(NewIndex))
	{
		UPaperFlipbook* FB = AssetPtr->Flipbooks[NewIndex].Identity.Flipbook.Get();
		int32 FrameCount = FB ? FB->GetNumKeyFrames() : 0;
		if (SelectedFrameIndex >= FrameCount)
		{
			SelectedFrameIndex = FMath::Max(0, FrameCount - 1);
		}
	}
	else
	{
		SelectedFrameIndex = 0;
	}

	// Clear frame multi-select
	SelectedFrames.Empty();
	FrameSelectionAnchorIndex = INDEX_NONE;

	// Update card selection
	SelectedFlipbookCards.Empty();
	if (AssetPtr->Flipbooks.IsValidIndex(NewIndex))
	{
		SelectedFlipbookCards.Add(NewIndex);
		SelectionAnchorIndex = NewIndex;
	}
	else
	{
		SelectionAnchorIndex = INDEX_NONE;
	}

	// Keep the queue cursor honest for every selection path, not just the queue's own navigation:
	// a browser card, the navigator, an Animation Map node or undo can all land on a queued
	// animation. Without this the cursor stays where it last was, so the next queue step is computed
	// from an unrelated slot (the "arrow keys stick on one entry" bug) and the queue highlight lies.
	// Silent by design — the queue list repaints from OnFlipbookSelectionChanged below.
	SyncQueueIndexToSelection();

	// Fire delegates in order: flipbook first, then frame (the transition clear, when an edge was
	// active, already broadcast its own channel from the hoisted ClearSelectedTransition above)
	BroadcastOrDefer(EditorModelDelegateFlags::FlipbookSelection, [this, NewIndex]()
	{
		OnFlipbookSelectionChanged.Broadcast(NewIndex);
	});
	BroadcastOrDefer(EditorModelDelegateFlags::FrameSelection, [this]()
	{
		OnFrameSelectionChanged.Broadcast();
	});
}

// ==========================================
// Frame Selection
// ==========================================

void FCharacterProfileEditorModel::SetSelectedFrame(int32 NewIndex)
{
	SelectedFrameIndex = NewIndex;
	BroadcastOrDefer(EditorModelDelegateFlags::FrameSelection, [this]()
	{
		OnFrameSelectionChanged.Broadcast();
	});
}

void FCharacterProfileEditorModel::SetSelectedExcludedFrame(int32 NewIndex)
{
	SelectedExcludedFrameIndex = NewIndex;
}

void FCharacterProfileEditorModel::HandleFrameClick(int32 ClickedIndex, bool bCtrl, bool bShift, int32 TotalFrameCount)
{
	if (bCtrl)
	{
		if (SelectedFrames.Contains(ClickedIndex))
		{
			SelectedFrames.Remove(ClickedIndex);
		}
		else
		{
			SelectedFrames.Add(ClickedIndex);
		}
		FrameSelectionAnchorIndex = ClickedIndex;
	}
	else if (bShift && FrameSelectionAnchorIndex != INDEX_NONE)
	{
		int32 Start = FMath::Min(FrameSelectionAnchorIndex, ClickedIndex);
		int32 End = FMath::Max(FrameSelectionAnchorIndex, ClickedIndex);
		for (int32 i = Start; i <= End; ++i)
		{
			if (i >= 0 && i < TotalFrameCount)
			{
				SelectedFrames.Add(i);
			}
		}
	}
	else
	{
		SelectedFrames.Empty();
		FrameSelectionAnchorIndex = ClickedIndex;
	}

	BroadcastOrDefer(EditorModelDelegateFlags::FrameSelection, [this]()
	{
		OnFrameSelectionChanged.Broadcast();
	});
}

void FCharacterProfileEditorModel::ClearFrameSelection()
{
	SelectedFrames.Empty();
	FrameSelectionAnchorIndex = INDEX_NONE;
}

// ==========================================
// Group Collapse
// ==========================================

void FCharacterProfileEditorModel::ToggleGroupCollapse(FName GroupName)
{
	if (CollapsedFlipbookGroups.Contains(GroupName))
	{
		CollapsedFlipbookGroups.Remove(GroupName);
	}
	else
	{
		CollapsedFlipbookGroups.Add(GroupName);
	}
	NotifyGroupCollapseChanged();
}

void FCharacterProfileEditorModel::NotifyGroupCollapseChanged()
{
	BroadcastOrDefer(EditorModelDelegateFlags::GroupCollapse, [this]()
	{
		OnGroupCollapseChanged.Broadcast();
	});
}

// ==========================================
// Search
// ==========================================

void FCharacterProfileEditorModel::SetFlipbookGroupSearchText(const FString& NewText)
{
	// No-op guard (audit F16): on no change, return BEFORE BroadcastOrDefer — same contract as
	// SetSelectedFlipbook. Closes the documented re-entrancy/refresh-churn hazard if a future search
	// widget ever mirrors the model's text back during a broadcast. SetText() doesn't re-fire OnTextChanged,
	// so this is harmless today and purely defensive.
	if (FlipbookGroupSearchText == NewText)
	{
		return;
	}
	FlipbookGroupSearchText = NewText;
	BroadcastOrDefer(EditorModelDelegateFlags::SearchText, [this, NewText]()
	{
		OnSearchTextChanged.Broadcast(NewText);
	});
}

// ==========================================
// Completion Filter
// ==========================================

void FCharacterProfileEditorModel::SetCompletionFilterMask(int32 NewMask)
{
	if (CompletionFilterMask == NewMask) return;
	CompletionFilterMask = NewMask;
	BroadcastOrDefer(EditorModelDelegateFlags::CompletionFilter, [this, NewMask]()
	{
		OnCompletionFilterChanged.Broadcast(NewMask);
	});
}

// ==========================================
// Navigation
// ==========================================

TArray<int32> FCharacterProfileEditorModel::GetVisualFlipbookOrder() const
{
	TArray<int32> Result;
	UPaper2DPlusCharacterProfileAsset* AssetPtr = Asset.Get();
	if (!AssetPtr) return Result;

	TArray<int32> SortedIndices = GetSortedFlipbookIndices();
	TMap<FName, TArray<int32>> FlipbooksByGroup;
	for (int32 i : SortedIndices)
	{
		FlipbooksByGroup.FindOrAdd(AssetPtr->Flipbooks[i].FlipbookGroup).Add(i);
	}

	if (FlipbooksByGroup.Num() <= 1 && FlipbooksByGroup.Contains(NAME_None))
	{
		return FlipbooksByGroup[NAME_None];
	}

	TMap<FName, TArray<const FFlipbookGroupInfo*>> Tree = AssetPtr->GetFlipbookGroupTree();

	TFunction<void(FName)> TraverseGroup = [&](FName GroupName)
	{
		if (const TArray<int32>* GroupIndices = FlipbooksByGroup.Find(GroupName))
		{
			Result.Append(*GroupIndices);
		}

		const TArray<const FFlipbookGroupInfo*>* ChildGroups = GroupName.IsNone() ? nullptr : Tree.Find(GroupName);
		if (ChildGroups)
		{
			for (const FFlipbookGroupInfo* ChildGroup : *ChildGroups)
			{
				TraverseGroup(ChildGroup->GroupName);
			}
		}
	};

	TraverseGroup(NAME_None);

	if (const TArray<const FFlipbookGroupInfo*>* RootGroups = Tree.Find(NAME_None))
	{
		for (const FFlipbookGroupInfo* GroupInfo : *RootGroups)
		{
			TraverseGroup(GroupInfo->GroupName);
		}
	}

	return Result;
}

int32 FCharacterProfileEditorModel::GetVisualAdjacentFlipbookIndex(int32 Direction) const
{
	TArray<int32> Order = GetVisualFlipbookOrder();
	if (Order.Num() == 0) return INDEX_NONE;

	int32 CurrentPos = Order.IndexOfByKey(SelectedFlipbookIndex);
	if (CurrentPos == INDEX_NONE) return INDEX_NONE;

	int32 TargetPos = CurrentPos + Direction;
	if (TargetPos < 0 || TargetPos >= Order.Num()) return INDEX_NONE;

	return Order[TargetPos];
}

TArray<int32> FCharacterProfileEditorModel::GetSortedFlipbookIndices() const
{
	TArray<int32> Indices;
	UPaper2DPlusCharacterProfileAsset* AssetPtr = Asset.Get();
	if (!AssetPtr) return Indices;

	for (int32 i = 0; i < AssetPtr->Flipbooks.Num(); ++i)
	{
		Indices.Add(i);
	}

	Indices.Sort([AssetPtr](int32 A, int32 B)
	{
		return AssetPtr->Flipbooks[A].Identity.FlipbookName.Compare(AssetPtr->Flipbooks[B].Identity.FlipbookName, ESearchCase::IgnoreCase) < 0;
	});

	return Indices;
}

// ==========================================
// Layer State
// ==========================================

void FCharacterProfileEditorModel::SetSelectedLayer(int32 NewIndex)
{
	const UPaper2DPlusCharacterLayerAsset* LayerAsset =
		Cast<UPaper2DPlusCharacterLayerAsset>(SecondaryWatchedObject.Get());
	const FGuid NewId = LayerAsset && LayerAsset->Layers.IsValidIndex(NewIndex)
		? LayerAsset->Layers[NewIndex].LayerId
		: FGuid();
	SetSelectedLayerById(NewId);
}

void FCharacterProfileEditorModel::SetSelectedLayerById(const FGuid& NewLayerId)
{
	const UPaper2DPlusCharacterLayerAsset* LayerAsset =
		Cast<UPaper2DPlusCharacterLayerAsset>(SecondaryWatchedObject.Get());
	int32 ResolvedIndex = INDEX_NONE;
	if (LayerAsset && NewLayerId.IsValid())
	{
		ResolvedIndex = LayerAsset->Layers.IndexOfByPredicate([&NewLayerId](const FCharacterLayer& Layer)
		{
			return Layer.LayerId == NewLayerId;
		});
	}
	if (SelectedLayerId == NewLayerId && SelectedLayerIndex == ResolvedIndex) return;
	SelectedLayerId = NewLayerId;
	SelectedLayerIndex = ResolvedIndex;
	BroadcastOrDefer(EditorModelDelegateFlags::LayerSelection, [this]()
	{
		OnLayerSelectionChanged.Broadcast(SelectedLayerIndex);
	});
}

void FCharacterProfileEditorModel::ReconcileLayerSelectionIdentity()
{
	const UPaper2DPlusCharacterLayerAsset* LayerAsset =
		Cast<UPaper2DPlusCharacterLayerAsset>(SecondaryWatchedObject.Get());
	int32 ResolvedIndex = INDEX_NONE;
	if (LayerAsset && SelectedLayerId.IsValid())
	{
		ResolvedIndex = LayerAsset->Layers.IndexOfByPredicate([this](const FCharacterLayer& Layer)
		{
			return Layer.LayerId == SelectedLayerId;
		});
	}
	if (SelectedLayerIndex == ResolvedIndex) return;
	SelectedLayerIndex = ResolvedIndex;
	BroadcastOrDefer(EditorModelDelegateFlags::LayerSelection, [this]()
	{
		OnLayerSelectionChanged.Broadcast(SelectedLayerIndex);
	});
}

void FCharacterProfileEditorModel::SetSelectedTransition(const FString& InFromMove, const FString& InToMove)
{
	// The key is the LOWERED (From, To) pair — the FEdgeReselectKey value shape (case-insensitive,
	// matching the data layer's name semantics).
	const FString FromLower = InFromMove.ToLower();
	const FString ToLower = InToMove.ToLower();

	// No-op guard on the KEY ALONE — deliberately not gated on whether the key resolves to a live row
	// (the TASK-96 invalid-key rule): the pane/panel mirror this key back during the broadcast, and a
	// stale/unresolvable same key must dead-end here or the drain loop re-broadcasts forever.
	if (FromLower.Equals(SelectedTransitionFromLower, ESearchCase::CaseSensitive)
		&& ToLower.Equals(SelectedTransitionToLower, ESearchCase::CaseSensitive))
	{
		return;
	}

	SelectedTransitionFromLower = FromLower;
	SelectedTransitionToLower = ToLower;
	BroadcastOrDefer(EditorModelDelegateFlags::TransitionSelection, [this]()
	{
		OnTransitionSelectionChanged.Broadcast();
	});
}

void FCharacterProfileEditorModel::ClearSelectedTransition()
{
	// Clearing when already clear is the same-key no-op (no re-broadcast) — SetSelectedTransition's
	// guard covers the empty/empty key like any other.
	SetSelectedTransition(FString(), FString());
}

bool FCharacterProfileEditorModel::IsLayerVisible(const FString& LayerName) const
{
	if (const bool* Override = LayerVisibilityOverrides.Find(LayerName))
	{
		return *Override;
	}
	if (const UPaper2DPlusCharacterLayerAsset* LayerAsset =
		Cast<UPaper2DPlusCharacterLayerAsset>(SecondaryWatchedObject.Get()))
	{
		if (const FCharacterLayer* Layer = LayerAsset->GetLayerByName(LayerName))
		{
			FPaper2DPlusAppearanceDescriptor DefaultAppearance;
			return Paper2DPlusAppearanceResolver::BuildDefaultDescriptor(LayerAsset, DefaultAppearance)
				&& DefaultAppearance.ActiveLayerIds.Contains(Layer->LayerId);
		}
	}
	return true;
}

void FCharacterProfileEditorModel::SetLayerVisibility(const FString& LayerName, bool bVisible)
{
	// No-op guard (audit F16): only when an explicit override already equals the new value — return
	// before BroadcastOrDefer (matches the SetSelectedFlipbook contract; defensive against a future
	// visibility widget mirroring state back during a broadcast).
	if (const bool* Existing = LayerVisibilityOverrides.Find(LayerName))
	{
		if (*Existing == bVisible)
		{
			return;
		}
	}
	LayerVisibilityOverrides.FindOrAdd(LayerName) = bVisible;
	BroadcastOrDefer(EditorModelDelegateFlags::LayerVisibility, [this]()
	{
		OnLayerVisibilityChanged.Broadcast();
	});
}

// ==========================================
// Playback Queue
// ==========================================

void FCharacterProfileEditorModel::SetPlaybackQueueIndex(int32 NewIndex)
{
	if (PlaybackQueue.IsValidIndex(NewIndex))
	{
		PlaybackQueueIndex = NewIndex;
	}
}

void FCharacterProfileEditorModel::SetQueuePlaying(bool bPlaying)
{
	if (bIsQueuePlaying == bPlaying) return;
	bIsQueuePlaying = bPlaying;
	BroadcastOrDefer(EditorModelDelegateFlags::QueuePlaybackState, [this, bPlaying]()
	{
		OnQueuePlaybackStateChanged.Broadcast(bPlaying);
	});
}

void FCharacterProfileEditorModel::AddToQueue(int32 FlipbookIndex)
{
	UPaper2DPlusCharacterProfileAsset* AssetPtr = Asset.Get();
	if (!AssetPtr || !AssetPtr->Flipbooks.IsValidIndex(FlipbookIndex)) return;

	PlaybackQueue.Add(FlipbookIndex);
	PlaybackQueueIndex = PlaybackQueue.Num() - 1;

	SetSelectedFlipbook(FlipbookIndex);

	BroadcastOrDefer(EditorModelDelegateFlags::QueueChanged, [this]()
	{
		OnQueueChanged.Broadcast();
	});
}

void FCharacterProfileEditorModel::RemoveFromQueue(int32 QueueIndex)
{
	if (!PlaybackQueue.IsValidIndex(QueueIndex)) return;

	PlaybackQueue.RemoveAt(QueueIndex);

	if (PlaybackQueue.Num() == 0)
	{
		PlaybackQueueIndex = 0;
		SetQueuePlaying(false);
	}
	else if (QueueIndex < PlaybackQueueIndex)
	{
		PlaybackQueueIndex--;
	}
	else if (PlaybackQueueIndex >= PlaybackQueue.Num())
	{
		PlaybackQueueIndex = PlaybackQueue.Num() - 1;
	}

	BroadcastOrDefer(EditorModelDelegateFlags::QueueChanged, [this]()
	{
		OnQueueChanged.Broadcast();
	});
}

void FCharacterProfileEditorModel::ReorderQueueEntry(int32 FromIndex, int32 ToIndex)
{
	if (!PlaybackQueue.IsValidIndex(FromIndex)) return;
	ToIndex = FMath::Clamp(ToIndex, 0, PlaybackQueue.Num());

	int32 Value = PlaybackQueue[FromIndex];
	PlaybackQueue.RemoveAt(FromIndex);

	int32 InsertAt = (ToIndex > FromIndex) ? ToIndex - 1 : ToIndex;
	InsertAt = FMath::Clamp(InsertAt, 0, PlaybackQueue.Num());
	PlaybackQueue.Insert(Value, InsertAt);

	if (PlaybackQueueIndex == FromIndex)
	{
		PlaybackQueueIndex = InsertAt;
	}
	else
	{
		if (FromIndex < PlaybackQueueIndex && InsertAt >= PlaybackQueueIndex) PlaybackQueueIndex--;
		else if (FromIndex > PlaybackQueueIndex && InsertAt <= PlaybackQueueIndex) PlaybackQueueIndex++;
	}

	BroadcastOrDefer(EditorModelDelegateFlags::QueueChanged, [this]()
	{
		OnQueueChanged.Broadcast();
	});
}

void FCharacterProfileEditorModel::ClearQueue()
{
	if (PlaybackQueue.Num() == 0) return;
	SetQueuePlaying(false);
	PlaybackQueue.Empty();
	PlaybackQueueIndex = 0;
	BroadcastOrDefer(EditorModelDelegateFlags::QueueChanged, [this]()
	{
		OnQueueChanged.Broadcast();
	});
}

void FCharacterProfileEditorModel::PurgeInvalidQueueEntries()
{
	UPaper2DPlusCharacterProfileAsset* AssetPtr = Asset.Get();
	if (!AssetPtr) return;

	bool bChanged = false;
	for (int32 i = PlaybackQueue.Num() - 1; i >= 0; --i)
	{
		if (!AssetPtr->Flipbooks.IsValidIndex(PlaybackQueue[i]))
		{
			if (i < PlaybackQueueIndex) PlaybackQueueIndex--;
			PlaybackQueue.RemoveAt(i);
			bChanged = true;
		}
	}

	if (PlaybackQueue.Num() == 0)
	{
		PlaybackQueueIndex = 0;
		SetQueuePlaying(false);
	}
	else
	{
		PlaybackQueueIndex = FMath::Clamp(PlaybackQueueIndex, 0, PlaybackQueue.Num() - 1);
	}

	if (bChanged)
	{
		BroadcastOrDefer(EditorModelDelegateFlags::QueueChanged, [this]()
		{
			OnQueueChanged.Broadcast();
		});
	}
}

void FCharacterProfileEditorModel::HandleFlipbookRemoved(
	int32 RemovedFlipbookIndex,
	int32 PreferredSelectionIndex)
{
	if (RemovedFlipbookIndex < 0)
	{
		return;
	}

	bool bQueueChanged = false;
	bool bRemovedActiveQueueEntry = false;
	for (int32 QueueIndex = PlaybackQueue.Num() - 1; QueueIndex >= 0; --QueueIndex)
	{
		int32& ProfileIndex = PlaybackQueue[QueueIndex];
		if (ProfileIndex == RemovedFlipbookIndex)
		{
			if (QueueIndex < PlaybackQueueIndex)
			{
				--PlaybackQueueIndex;
			}
			else if (QueueIndex == PlaybackQueueIndex)
			{
				bRemovedActiveQueueEntry = true;
			}
			PlaybackQueue.RemoveAt(QueueIndex);
			bQueueChanged = true;
		}
		else if (ProfileIndex > RemovedFlipbookIndex)
		{
			--ProfileIndex;
			bQueueChanged = true;
		}
	}

	if (PlaybackQueue.IsEmpty())
	{
		PlaybackQueueIndex = 0;
		SetQueuePlaying(false);
	}
	else
	{
		PlaybackQueueIndex = FMath::Clamp(PlaybackQueueIndex, 0, PlaybackQueue.Num() - 1);
		if (bRemovedActiveQueueEntry)
		{
			SetQueuePlaying(false);
		}
	}

	if (bQueueChanged)
	{
		BroadcastOrDefer(EditorModelDelegateFlags::QueueChanged, [this]()
		{
			OnQueueChanged.Broadcast();
		});
	}

	SetSelectedFlipbook(PreferredSelectionIndex);
}

int32 FCharacterProfileEditorModel::FindQueuePositionForSelection() const
{
	if (PlaybackQueue.Num() == 0 || SelectedFlipbookIndex == INDEX_NONE)
	{
		return INDEX_NONE;
	}

	// Prefer the slot the cursor already points at when it matches. A flipbook queued twice would
	// otherwise snap back to its first occurrence on every step, so the queue could never be walked
	// past that duplicate.
	if (PlaybackQueue.IsValidIndex(PlaybackQueueIndex) && PlaybackQueue[PlaybackQueueIndex] == SelectedFlipbookIndex)
	{
		return PlaybackQueueIndex;
	}

	return PlaybackQueue.IndexOfByKey(SelectedFlipbookIndex);
}

int32 FCharacterProfileEditorModel::SyncQueueIndexToSelection()
{
	const int32 Position = FindQueuePositionForSelection();
	if (Position != INDEX_NONE)
	{
		PlaybackQueueIndex = Position;
	}
	return Position;
}

int32 FCharacterProfileEditorModel::GetSelectedFlipbookFrameCount() const
{
	UPaper2DPlusCharacterProfileAsset* AssetPtr = Asset.Get();
	if (!AssetPtr || !AssetPtr->Flipbooks.IsValidIndex(SelectedFlipbookIndex))
	{
		return 0;
	}
	UPaperFlipbook* Flipbook = AssetPtr->Flipbooks[SelectedFlipbookIndex].Identity.Flipbook.Get();
	return Flipbook ? Flipbook->GetNumKeyFrames() : 0;
}

int32 FCharacterProfileEditorModel::GetQueueAdjacentFlipbookIndex(int32 Direction) const
{
	// Fewer than two entries has no "other" entry to step to; the caller falls back to wrapping
	// within the current flipbook rather than re-selecting the one it is already on.
	if (PlaybackQueue.Num() < 2 || Direction == 0) return INDEX_NONE;

	const int32 Position = FindQueuePositionForSelection();
	if (Position == INDEX_NONE) return INDEX_NONE;

	const int32 Num = PlaybackQueue.Num();
	const int32 TargetQueuePos = ((Position + Direction) % Num + Num) % Num;
	return PlaybackQueue.IsValidIndex(TargetQueuePos) ? PlaybackQueue[TargetQueuePos] : INDEX_NONE;
}

int32 FCharacterProfileEditorModel::StepQueue(int32 Direction, bool bLandOnLastFrame)
{
	if (PlaybackQueue.Num() < 2 || Direction == 0) return INDEX_NONE;

	const int32 Position = SyncQueueIndexToSelection();
	if (Position == INDEX_NONE) return INDEX_NONE;

	const int32 Num = PlaybackQueue.Num();
	const int32 TargetPosition = ((Position + Direction) % Num + Num) % Num;
	if (!PlaybackQueue.IsValidIndex(TargetPosition)) return INDEX_NONE;

	const int32 TargetFlipbookIndex = PlaybackQueue[TargetPosition];
	UPaper2DPlusCharacterProfileAsset* AssetPtr = Asset.Get();
	if (!AssetPtr || !AssetPtr->Flipbooks.IsValidIndex(TargetFlipbookIndex)) return INDEX_NONE;

	PlaybackQueueIndex = TargetPosition;
	SetSelectedFlipbook(TargetFlipbookIndex);
	// SetSelectedFlipbook reconciles the cursor, which lands a duplicated entry on its first
	// occurrence. Re-assert the slot we actually stepped to so the next step continues from here.
	PlaybackQueueIndex = TargetPosition;

	const int32 FrameCount = GetSelectedFlipbookFrameCount();
	SetSelectedFrame(bLandOnLastFrame ? FMath::Max(0, FrameCount - 1) : 0);

	return TargetFlipbookIndex;
}

// ==========================================
// Asset Data Changed
// ==========================================

void FCharacterProfileEditorModel::NotifyAssetDataChanged()
{
	if (UPaper2DPlusCharacterLayerAsset* LayerAsset =
		Cast<UPaper2DPlusCharacterLayerAsset>(SecondaryWatchedObject.Get()))
	{
		// Keep the packaged Runtime Customizable projection current with the primary layer-authoring
		// workspace. PreSave repeats this as the cook/save safety net.
		LayerAsset->RebuildCookedGameplayData();
	}
	ReconcileFlipbookSelectionIdentity();
	ReconcileLayerSelectionIdentity();
	PurgeInvalidQueueEntries();

	BroadcastOrDefer(EditorModelDelegateFlags::AssetData, [this]()
	{
		OnAssetDataChanged.Broadcast();
	});
}

// ==========================================
// Tab Management
// ==========================================

void FCharacterProfileEditorModel::BringTabToFront(FName TabId)
{
	TSharedPtr<FTabManager> TabManager = TabManagerWeak.Pin();
	if (TabManager.IsValid())
	{
		TabManager->TryInvokeTab(TabId);
	}
}

// ==========================================
// Frame Data window
// ==========================================

void FCharacterProfileEditorModel::OpenFrameDataWindow()
{
	if (!FSlateApplication::IsInitialized())
	{
		return;
	}

	if (TSharedPtr<SWindow> Existing = FrameDataWindow.Pin())
	{
		Existing->BringToFront();
		return;
	}

	UPaper2DPlusCharacterProfileAsset* CurrentAsset = GetAsset();
	if (!CurrentAsset)
	{
		return;
	}

	// Non-modal floating read-out. SFrameDataPanel mutates nothing and self-refreshes on this model's
	// delegates, so the window needs no transaction/undo wiring. The panel's strong model ref keeps
	// this model alive while the window is up — the owning toolkit's destructor closes it (see header).
	const TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(FText::Format(
			NSLOCTEXT("CharacterProfileEditorModel", "FrameDataWindowTitle", "Frame Data — {0}"),
			FText::FromName(CurrentAsset->GetFName())))
		.ClientSize(FVector2D(1180.0f, 480.0f))
		.SupportsMinimize(true)
		.SupportsMaximize(true)
		[
			SNew(SFrameDataPanel)
			.Model(SharedThis(this))
		];

	FrameDataWindow = Window;
	FSlateApplication::Get().AddWindow(Window);
}

void FCharacterProfileEditorModel::CloseFrameDataWindow()
{
	if (TSharedPtr<SWindow> Window = FrameDataWindow.Pin())
	{
		Window->RequestDestroyWindow();
	}
	FrameDataWindow.Reset();
}

// ==========================================
// Mutation Guard
// ==========================================

void FCharacterProfileEditorModel::BeginModelMutation()
{
	++MutationDepth;
}

void FCharacterProfileEditorModel::EndModelMutation()
{
	check(MutationDepth > 0);
	--MutationDepth;

	if (MutationDepth == 0 && PendingDelegateFlags != 0)
	{
		uint16 Flags = PendingDelegateFlags;
		PendingDelegateFlags = 0;

		// External reconciliation may itself change stable selections. Keep that work inside the
		// batch, merge the resulting bits into this one flush, and avoid an immediate selection
		// broadcast followed by the same channel again below.
		if (Flags & EditorModelDelegateFlags::ExternalModified)
		{
			++MutationDepth;
			ReconcileFlipbookSelectionIdentity();
			--MutationDepth;
			Flags |= PendingDelegateFlags;
			PendingDelegateFlags = 0;
		}

		if (Flags & EditorModelDelegateFlags::FlipbookSelection)
		{
			OnFlipbookSelectionChanged.Broadcast(SelectedFlipbookIndex);
		}
		if (Flags & EditorModelDelegateFlags::FrameSelection)
		{
			OnFrameSelectionChanged.Broadcast();
		}
		if (Flags & EditorModelDelegateFlags::GroupCollapse)
		{
			OnGroupCollapseChanged.Broadcast();
		}
		if (Flags & EditorModelDelegateFlags::SearchText)
		{
			OnSearchTextChanged.Broadcast(FlipbookGroupSearchText);
		}
		if (Flags & EditorModelDelegateFlags::CompletionFilter)
		{
			OnCompletionFilterChanged.Broadcast(CompletionFilterMask);
		}
		if (Flags & EditorModelDelegateFlags::AssetData)
		{
			OnAssetDataChanged.Broadcast();
		}
		if (Flags & EditorModelDelegateFlags::ExternalModified)
		{
			OnAssetExternallyModified.Broadcast();
		}
		if (Flags & EditorModelDelegateFlags::QueueChanged)
		{
			OnQueueChanged.Broadcast();
		}
		if (Flags & EditorModelDelegateFlags::QueuePlaybackState)
		{
			OnQueuePlaybackStateChanged.Broadcast(bIsQueuePlaying);
		}
		if (Flags & EditorModelDelegateFlags::LayerSelection)
		{
			OnLayerSelectionChanged.Broadcast(SelectedLayerIndex);
		}
		if (Flags & EditorModelDelegateFlags::LayerVisibility)
		{
			OnLayerVisibilityChanged.Broadcast();
		}
		if (Flags & EditorModelDelegateFlags::TransitionSelection)
		{
			OnTransitionSelectionChanged.Broadcast();
		}
	}
}

// ==========================================
// Broadcast / Re-entrancy
// ==========================================

void FCharacterProfileEditorModel::BroadcastOrDefer(uint16 FlagBit, TFunction<void()> Broadcast)
{
	if (MutationDepth > 0)
	{
		PendingDelegateFlags |= FlagBit;
		return;
	}

	if (bIsBroadcasting)
	{
		DeferredBroadcasts.Add(MoveTemp(Broadcast));
		return;
	}

	bIsBroadcasting = true;
	Broadcast();
	bIsBroadcasting = false;

	// Drain deferred broadcasts
	while (DeferredBroadcasts.Num() > 0)
	{
		TArray<TFunction<void()>> Pending = MoveTemp(DeferredBroadcasts);
		for (auto& Fn : Pending)
		{
			bIsBroadcasting = true;
			Fn();
			bIsBroadcasting = false;
		}
	}
}

// ==========================================
// Identity Resolution
// ==========================================

void FCharacterProfileEditorModel::ResolveFlipbookIdentity()
{
	UPaper2DPlusCharacterProfileAsset* AssetPtr = Asset.Get();
	if (AssetPtr && AssetPtr->Flipbooks.IsValidIndex(SelectedFlipbookIndex))
	{
		const FFlipbookProfileEntry& Entry = AssetPtr->Flipbooks[SelectedFlipbookIndex];
		SelectedFlipbookName = *Entry.Identity.FlipbookName;
		SelectedFlipbookObject = Entry.Identity.Flipbook.Get();
		SelectedFlipbookPath = Entry.Identity.Flipbook.ToSoftObjectPath();
	}
	else
	{
		SelectedFlipbookName = NAME_None;
		SelectedFlipbookObject = nullptr;
		SelectedFlipbookPath.Reset();
	}
}

void FCharacterProfileEditorModel::ReconcileFlipbookSelectionIdentity()
{
	UPaper2DPlusCharacterProfileAsset* AssetPtr = Asset.Get();
	if (!AssetPtr)
	{
		SelectedFlipbookIndex = INDEX_NONE;
		ResolveFlipbookIdentity();
		return;
	}

	int32 ResolvedIndex = INDEX_NONE;
	if (UPaperFlipbook* PreviouslySelectedObject = SelectedFlipbookObject.Get())
	{
		for (int32 Index = 0; Index < AssetPtr->Flipbooks.Num(); ++Index)
		{
			if (AssetPtr->Flipbooks[Index].Identity.Flipbook.Get() == PreviouslySelectedObject)
			{
				ResolvedIndex = Index;
				break;
			}
		}
	}
	if (ResolvedIndex == INDEX_NONE && !SelectedFlipbookPath.IsNull())
	{
		for (int32 Index = 0; Index < AssetPtr->Flipbooks.Num(); ++Index)
		{
			if (AssetPtr->Flipbooks[Index].Identity.Flipbook.ToSoftObjectPath() == SelectedFlipbookPath)
			{
				ResolvedIndex = Index;
				break;
			}
		}
	}
	if (ResolvedIndex == INDEX_NONE && !SelectedFlipbookName.IsNone())
	{
		for (int32 Index = 0; Index < AssetPtr->Flipbooks.Num(); ++Index)
		{
			if (AssetPtr->Flipbooks[Index].Identity.FlipbookName.Equals(
				SelectedFlipbookName.ToString(), ESearchCase::IgnoreCase))
			{
				ResolvedIndex = Index;
				break;
			}
		}
	}

	const bool bIndexChanged = SelectedFlipbookIndex != ResolvedIndex;
	SelectedFlipbookIndex = ResolvedIndex;
	if (ResolvedIndex == INDEX_NONE)
	{
		SelectedFlipbookCards.Reset();
		SelectionAnchorIndex = INDEX_NONE;
		SelectedFrameIndex = 0;
	}
	else
	{
		for (TSet<int32>::TIterator It = SelectedFlipbookCards.CreateIterator(); It; ++It)
		{
			if (!AssetPtr->Flipbooks.IsValidIndex(*It)) It.RemoveCurrent();
		}
		if (bIndexChanged)
		{
			SelectedFlipbookCards.Reset();
			SelectedFlipbookCards.Add(ResolvedIndex);
			SelectionAnchorIndex = ResolvedIndex;
		}
	}
	ResolveFlipbookIdentity();
	if (bIndexChanged)
	{
		BroadcastOrDefer(EditorModelDelegateFlags::FlipbookSelection, [this]()
		{
			OnFlipbookSelectionChanged.Broadcast(SelectedFlipbookIndex);
		});
	}
}

// ==========================================
// OnObjectModified
// ==========================================

void FCharacterProfileEditorModel::OnObjectModified(UObject* Object)
{
	if (!Object) return;

	// Primary = the Profile. Secondary = one optional host-owned companion watch (the Layer editor's
	// Layer asset or the Profile editor's authoritative Catalog). Every match routes through the SAME
	// coalesced pend-or-defer discipline.
	UPaper2DPlusCharacterProfileAsset* AssetPtr = Asset.Get();
	UObject* SecondaryPtr = SecondaryWatchedObject.Get();

	const bool bMatchesPrimary = AssetPtr && (Object == AssetPtr || Object->IsIn(AssetPtr));
	const bool bMatchesSecondary = !bMatchesPrimary && SecondaryPtr && (Object == SecondaryPtr || Object->IsIn(SecondaryPtr));
	if (!bMatchesPrimary && !bMatchesSecondary) return;

	if (MutationDepth > 0)
	{
		PendingDelegateFlags |= EditorModelDelegateFlags::ExternalModified;
		return;
	}

	if (bExternalModifyPending) return;
	bExternalModifyPending = true;

	ExternalModifyTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateRaw(this, &FCharacterProfileEditorModel::DeferredExternalModifiedNotify),
		0.0f);
}

bool FCharacterProfileEditorModel::DeferredExternalModifiedNotify(float /*DeltaTime*/)
{
	bExternalModifyPending = false;
	ExternalModifyTickerHandle.Reset();

	ReconcileFlipbookSelectionIdentity();
	ReconcileLayerSelectionIdentity();
	OnAssetExternallyModified.Broadcast();
	return false;
}
