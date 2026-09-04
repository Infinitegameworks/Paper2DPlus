// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "CoreGlobals.h"              // GUndo — suppressed around the node-spawn funnel
#include "EdGraph/EdGraph.h"
#include "GameplayTagContainer.h"
#include "Templates/UnrealTemplate.h" // TGuardValue
#include "Paper2DPlusClashGraph.generated.h"

class UPaper2DPlusClashGraphNode_Tag;

/** Wire-create request (TASK-77 U5). Fired by the schema's TryCreateConnection with the resolved
 *  Winner (output-side tag) and Loser (input-side tag). The panel-bound handler appends the FClashEdge
 *  under a transaction and sets LastProjection so the deferred Modify echo keeps the just-made wire.
 *  Returns true when the gesture was accepted (an edge was appended or already existed). */
DECLARE_DELEGATE_RetVal_TwoParams(bool, FClashWireCreateDelegate, const FGameplayTag& /*Winner*/, const FGameplayTag& /*Loser*/);

/** Wire-remove request (TASK-77 U5). Fired by the schema's Break* hooks for a USER break (NOT the
 *  reconcile's raw teardown). The panel handler removes the matching FClashEdge + requests a reconcile. */
DECLARE_DELEGATE_TwoParams(FClashWireRemoveDelegate, const FGameplayTag& /*Winner*/, const FGameplayTag& /*Loser*/);

/** Node-move commit (TASK-77 U5). Fired by SClashGraphNode_Tag::MoveTo on bMarkDirty=true (inside the
 *  engine's move transaction). The panel writes the TagNodePositions entry + sets LastProjection. */
DECLARE_DELEGATE_OneParam(FClashNodeMoveDelegate, UPaper2DPlusClashGraphNode_Tag* /*MoveNode*/);

/**
 * The Clash node-graph's transient UEdGraph (TASK-77 U5) — a fully transient PLAIN-WIRE projection over the
 * asset's flat FClashGraph.Edges (the flat data stays the single source of truth; the graph is never
 * serialized). Cloned from UPaper2DPlusAnimationMap, simplified: a clash edge is just {Winner, Loser} with
 * no metadata, so edges are plain pin links (no edge-as-node) and the schema/panel route writes through the
 * graph-owned delegate seam (the schema runs on the CDO and cannot hold per-editor state).
 */
UCLASS()
class UPaper2DPlusClashGraph : public UEdGraph
{
	GENERATED_BODY()

public:
	/** Bound by the panel. Winner-output -> Loser-input wire = "Winner beats Loser". */
	FClashWireCreateDelegate OnWireCreateRequested;

	/** Bound by the panel. Fired by the schema Break* hooks for a USER break only. */
	FClashWireRemoveDelegate OnWireRemoveRequested;

	/** Bound by the panel. The node-move position-persist seam (the widget reaches the panel via its graph). */
	FClashNodeMoveDelegate OnNodeMoveCommitted;

	/** Set by the panel for the whole scope of a reconcile/rebuild. Schema hooks early-out while set so the
	 *  reconcile's own raw pin teardown can't re-enter write-through (the repo's suppress-or-coerce rule). */
	bool bRebuildInProgress = false;

	/**
	 * THE shared node-spawn funnel — ALL node creation (wire-create + reconcile) goes through here.
	 * LOAD-BEARING INVARIANT (verbatim from UPaper2DPlusAnimationMap): the whole spawn runs with the
	 * transaction buffer SUPPRESSED (TGuardValue<ITransaction*>(GUndo, nullptr)) and the node leaves
	 * non-transactional. Clearing the flag alone cannot work at any point: UEdGraph::CreateNode constructs
	 * the node RF_Transactional and StaticConstructObject_Internal records every transactional object into
	 * the open transaction AT CONSTRUCTION, pinless and marked garbage — so a spawn inside a wire drop's
	 * transaction had its pins trashed by undo and RESURRECTED AS A ZOMBIE on redo (the Animation Map's
	 * hover-after-undo crash, 2026-09-04).
	 */
	template <typename NodeType, typename ConfigureFnType>
	static NodeType* SpawnNodeUntransactional(UEdGraph& Graph, ConfigureFnType ConfigureBeforePins)
	{
		TGuardValue<ITransaction*> SuppressTransactionBuffer(GUndo, nullptr); // construction records otherwise
		FGraphNodeCreator<NodeType> Creator(Graph);
		NodeType* Node = Creator.CreateNode(/*bSelectNewNode=*/false);
		Node->ClearFlags(RF_Transactional); // later Modify() calls never record it either
		ConfigureBeforePins(*Node);
		Creator.Finalize();
		return Node;
	}
};
