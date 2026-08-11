// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraph.h"
#include "GameplayTagContainer.h"
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
	 * LOAD-BEARING INVARIANT (verbatim from UPaper2DPlusAnimationMap): ClearFlags(RF_Transactional) runs
	 * IMMEDIATELY after Finalize() and BEFORE any MakeLinkTo — UEdGraph::CreateNode spawns nodes
	 * RF_Transactional and SaveToTransactionBuffer has NO transient-package exemption, so a still-
	 * transactional node recorded in an open connection transaction RESURRECTS AS A ZOMBIE after a reconcile
	 * destroys it (undo restores the recorded node into a graph that no longer owns it).
	 */
	template <typename NodeType, typename ConfigureFnType>
	static NodeType* SpawnNodeUntransactional(UEdGraph& Graph, ConfigureFnType ConfigureBeforePins)
	{
		FGraphNodeCreator<NodeType> Creator(Graph);
		NodeType* Node = Creator.CreateNode(/*bSelectNewNode=*/false);
		ConfigureBeforePins(*Node);
		Creator.Finalize();
		Node->ClearFlags(RF_Transactional); // before any MakeLinkTo — the zombie invariant.
		return Node;
	}
};
