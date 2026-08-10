// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ClashGraphCore.h"
#include "GameplayTagContainer.h"
#include "Widgets/SCompoundWidget.h"

class FActiveTimerHandle;
class FClashGraphAssetEditorToolkit;
class FScopedTransaction;
class FUICommandList;
class SComboButton;
class SGraphEditor;
class UPaper2DPlusClashGraph;
class UPaper2DPlusClashGraphAsset;
class UPaper2DPlusClashGraphNode_Tag;
struct FGraphAppearanceInfo;

/**
 * The clash node-graph editor surface (TASK-77 U5) — an SGraphEditor rendering the LIVE projection of the
 * clash asset's flat FClashGraph.Edges (the single source of truth) as category NODES + plain "beats" pin
 * WIRES (Winner output -> Loser input). Cloned from SAnimationMapPanel, STRIPPED to the reconcile core: a
 * clash edge is metadata-free, so there is no edge-as-node, no groups/comments/phases/drawing-policy/
 * drag-drop/model-sync — ~60% of the Animation Map surface drops away.
 *
 * Every authoring gesture funnels its DATA write through this panel's transaction template (the graph is
 * never an authority):
 *  - WIRE CREATE: the schema fires OnWireCreateRequested; the handler appends the edge AND makes the visual
 *    pin link directly (HIGH #1), then sets LastProjection so the deferred Modify echo diffs empty.
 *  - WIRE BREAK: the schema fires OnWireRemoveRequested for a USER break; the handler removes the row +
 *    RequestReconcile WITHOUT setting LastProjection (HIGH #2), so the rebuild tears the orphaned wire.
 *  - NODE MOVE: SClashGraphNode_Tag::MoveTo (bMarkDirty) persists the position to the asset's editor-only
 *    TagNodePositions map (with USER-MOVE STICKINESS so reconciles never shuffle a moved node).
 *  - DELETE: removes the selected categories' edges + position entries; the reconcile drops the nodes.
 *  - ADD CATEGORY: places a tag node with zero edges (a TagNodePositions key).
 *
 * The asset is resolved STRICTLY via the toolkit per use (never cached). The reconcile triggers off the
 * toolkit's OnEditedAssetChanged (Details edits, undo, AND the panel's own write echo) — diffs the flat
 * data against LastProjection, early-outs on the echo path, else rebuilds nodes + wires wholesale.
 */
class PAPER2DPLUSEDITOR_API SClashGraphPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SClashGraphPanel) {}
		SLATE_ARGUMENT(FClashGraphAssetEditorToolkit*, Toolkit)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SClashGraphPanel();

	/** Consulted by the reconcile gesture-liveness guard. */
	bool HasActiveTransaction() const { return ActiveTransaction.IsValid(); }

	/** Coalesced reconcile request — sets the pending flag and arms the poll-until-applied timer. */
	void RequestReconcile();

	/** Paper2DPlus.ClashGraphProbe payload: logs node/edge counts, per-node "name@x,y", per-edge
	 *  "edge <Winner> -> <Loser>" lines (the greppable ECABridge/headless verification seam). */
	void LogProbe() const;

private:
	// --- The toolkit (the asset is resolved via Toolkit->GetEditedAsset() PER USE — never cached) ---
	FClashGraphAssetEditorToolkit* Toolkit = nullptr;
	FDelegateHandle EditedAssetChangedHandle;

	/** The transient projection graph: NewObject into GetTransientPackage(), RF_Transient, never
	 *  transactional, rooted by the panel. */
	UPaper2DPlusClashGraph* GraphObj = nullptr;

	TSharedPtr<SGraphEditor> GraphEditorWidget;
	TSharedPtr<SComboButton> AddCategoryCombo;
	TSharedPtr<FUICommandList> GraphEditorCommands;

	// --- Transaction helpers ---
	TUniquePtr<FScopedTransaction> ActiveTransaction;
	void BeginTransaction(const FText& Description);
	void EndTransaction();
	bool bGraphWriteInProgress = false;

	// --- Reconcile engine state ---
	bool bReconcilePending = false;
	TWeakPtr<FActiveTimerHandle> ReconcileTimerHandle;

	/** Set in Construct when NO saved view was restored — the first reconcile that produces nodes frames them
	 *  once (ZoomToFit) so a fresh asset opens with its seeded graph in view, then clears the flag. */
	bool bPendingInitialFit = false;

	/** The previous projection — DiffProjection's baseline for the full-diff early-out. INVARIANT: any
	 *  scope that mutates graph+data in LOCKSTEP must set LastProjection before releasing
	 *  bGraphWriteInProgress (wire-create / node-move). Wire-REMOVE/DELETE deliberately do NOT (the rebuild
	 *  must run to clean up orphaned wires/nodes). */
	Paper2DPlusClashGraph::FClashProjection LastProjection;

	/** SESSION-TRANSIENT auto-layout cache (tag -> position): FirstLayout results land here, NEVER on the
	 *  asset (zero dirty on tab open). */
	TMap<FGameplayTag, FVector2D> SessionPositionCache;

	/** Tag -> the position ReapplyPositions LAST wrote. Lets the reconcile tell a legitimate STORED change
	 *  (undo / external edit / a committed drag — RE-ASSERT it) from a node whose live position drifted
	 *  because the USER moved it uncommitted (KEEP it — never snap back). The "nodes shuffle themselves" fix. */
	TMap<FGameplayTag, FVector2D> LastAppliedPositions;

	// --- Reconcile engine internals ---
	void HandleEditedAssetChanged();
	EActiveTimerReturnType OnReconcileTimer(double InCurrentTime, float InDeltaTime);
	bool IsGestureLive() const;
	void ReconcileNow();
	void ReapplyPositions(const TMap<FGameplayTag, UPaper2DPlusClashGraphNode_Tag*>& LiveNodesByTag,
		const TSet<FGameplayTag>& NewlyAdded);
	void CollectLiveNodes(TMap<FGameplayTag, UPaper2DPlusClashGraphNode_Tag*>& OutNodesByTag,
		TArray<UPaper2DPlusClashGraphNode_Tag*>& OutOrdered) const;
	TMap<FGameplayTag, FVector2D> FirstLayout(const TArray<FGameplayTag>& Tags,
		const TMap<FGameplayTag, FVector2D>& Occupied) const;

	// --- Graph-delegate handlers ---
	bool HandleWireCreateRequested(const FGameplayTag& Winner, const FGameplayTag& Loser);
	void HandleWireRemoveRequested(const FGameplayTag& Winner, const FGameplayTag& Loser);
	void HandleNodeMoveCommitted(UPaper2DPlusClashGraphNode_Tag* TagNode);

	// --- Delete command ---
	void DeleteSelectedNodes();
	bool CanDeleteSelectedNodes() const;
	void SelectAllNodes();
	bool CanSelectAllNodes() const { return true; }

	// --- Add Category ---
	TSharedRef<SWidget> BuildAddCategoryMenu();
	void AddCategoryNode(const FGameplayTag& InTag);

	// --- Helpers ---
	UPaper2DPlusClashGraphAsset* GetAsset() const;
	UPaper2DPlusClashGraphNode_Tag* FindLiveNode(const FGameplayTag& InTag) const;
	Paper2DPlusClashGraph::FClashProjection ProjectCurrent(const UPaper2DPlusClashGraphAsset* Asset) const;
	FGraphAppearanceInfo GetGraphAppearance() const;
	/** Returns true if a saved view (zoom + location) was found and applied. */
	bool RestoreViewFromConfig();
	void SaveViewToConfig() const;
};
