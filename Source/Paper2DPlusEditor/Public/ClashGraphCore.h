// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Paper2DPlusClashTypes.h"

/**
 * TASK-77 U5 — the pure, Slate-free / UEdGraph-free projection + diff core for the clash node-graph editor
 * (the analogue of Paper2DPlusAnimationMap's AnimationMapCore). The flat FClashGraph.Edges is the single
 * source of truth; the editor graph is a transient WRITE-THROUGH projection rebuilt by the reconcile.
 *
 * Worldless-testable: ProjectGraph / DiffProjection / AppendEdge / RemoveEdgeChecked are all pure functions
 * over plain data. The graph is a PLAIN-WIRE projection (a clash edge is just {Winner, Loser} — no per-edge
 * metadata, so no edge-as-node, no snapshots, no rename detection: a changed tag is an add+remove).
 */
namespace Paper2DPlusClashGraph
{
	/** One projected tag node. A node exists because an edge mentions its tag OR a TagNodePositions key
	 *  places it (an "Add Category" node with zero edges). Identity = the full tag; display = its leaf. */
	struct FClashProjectedNode
	{
		FGameplayTag Tag;
		FVector2D Position = FVector2D::ZeroVector;
		bool bHasStoredPosition = false; // false => the panel auto-layouts it (no stored placement yet)
	};

	/** One projected directed edge (Winner beats Loser), carrying its index into FClashGraph.Edges. */
	struct FClashProjectedEdge
	{
		int32 EdgeIndex = INDEX_NONE;
		FGameplayTag Winner;
		FGameplayTag Loser;

		bool operator==(const FClashProjectedEdge& Other) const
		{
			return Winner == Other.Winner && Loser == Other.Loser;
		}
		bool operator!=(const FClashProjectedEdge& Other) const { return !(*this == Other); }
	};

	struct FClashProjection
	{
		TArray<FClashProjectedNode> Nodes;
		TArray<FClashProjectedEdge> Edges;

		const FClashProjectedNode* FindNode(const FGameplayTag& Tag) const
		{
			return Nodes.FindByPredicate([&Tag](const FClashProjectedNode& N) { return N.Tag == Tag; });
		}
	};

	/** The full-diff early-out result. IsEmpty() == nothing to reconcile (the echo path). */
	struct FClashProjectionDiff
	{
		TArray<FGameplayTag> NodesToAdd;     // in Current, not in Prev
		TArray<FGameplayTag> NodesToRemove;  // in Prev, not in Current
		bool bEdgesEqual = true;             // the ordered edge list is identical
		bool bAnyPositionDrift = false;      // caller-supplied (live node moved vs stored)

		bool IsEmpty() const
		{
			return NodesToAdd.Num() == 0 && NodesToRemove.Num() == 0 && bEdgesEqual && !bAnyPositionDrift;
		}
	};

	/** Project the flat graph to nodes + edges. Node set = {valid edge Winner/Loser tags, first-appearance}
	 *  UNION {valid Positions keys} — INVALID tags are skipped on BOTH sides (fail-closed, no blank nodes).
	 *  Edges are DEDUPED by (Winner, Loser) pair (first occurrence wins) so duplicate flat rows project to ONE
	 *  wire (else the reconcile would MakeLinkTo the same pins twice). Deterministic order: edge tags in
	 *  first-appearance order, then position-only tags in map order. */
	PAPER2DPLUSEDITOR_API FClashProjection ProjectGraph(const FClashGraph& Graph, const TMap<FGameplayTag, FVector2D>* Positions = nullptr);

	/** Diff two projections by node-tag identity + ordered edge equality. bAnyPositionDrift is caller-supplied. */
	PAPER2DPLUSEDITOR_API FClashProjectionDiff DiffProjection(const FClashProjection& Prev, const FClashProjection& Current, bool bAnyPositionDrift);

	/** Append a "Winner beats Loser" edge. De-dupes (returns false if the exact pair already exists) and
	 *  rejects invalid/self pairs. Returns true iff an edge was added. */
	PAPER2DPLUSEDITOR_API bool AppendEdge(FClashGraph& Graph, const FGameplayTag& Winner, const FGameplayTag& Loser);

	/** Remove the first edge matching (Winner, Loser) by tag equality (no snapshot — tags are identity).
	 *  Returns true iff an edge was removed. */
	PAPER2DPLUSEDITOR_API bool RemoveEdgeChecked(FClashGraph& Graph, const FGameplayTag& Winner, const FGameplayTag& Loser);
}
