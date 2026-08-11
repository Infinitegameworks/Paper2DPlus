// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "ClashGraphCore.h"

namespace Paper2DPlusClashGraph
{
	FClashProjection ProjectGraph(const FClashGraph& Graph, const TMap<FGameplayTag, FVector2D>* Positions)
	{
		FClashProjection Out;

		auto AddNode = [&Out, Positions](const FGameplayTag& Tag)
		{
			if (!Tag.IsValid() || Out.FindNode(Tag) != nullptr)
			{
				return; // fail-closed on invalid; dedupe
			}
			FClashProjectedNode Node;
			Node.Tag = Tag;
			if (Positions)
			{
				if (const FVector2D* Pos = Positions->Find(Tag))
				{
					Node.Position = *Pos;
					Node.bHasStoredPosition = true;
				}
			}
			Out.Nodes.Add(Node);
		};

		// Edge tags first (first-appearance order), then position-only tags (so an "Add Category" node with
		// no edges still projects). Both sides skip invalid tags.
		for (const FClashEdge& Edge : Graph.Edges)
		{
			AddNode(Edge.Winner);
			AddNode(Edge.Loser);
		}
		if (Positions)
		{
			for (const TPair<FGameplayTag, FVector2D>& Pair : *Positions)
			{
				AddNode(Pair.Key);
			}
		}

		// Edges: only those whose BOTH endpoints are valid (a half-invalid edge can't draw a wire), DEDUPED
		// by (Winner, Loser) pair (keeping the first occurrence's index). The flat data CAN hold duplicate
		// rows (the Details Edges array has no uniqueness — only the graph-drag AppendEdge dedupes), and a
		// duplicate projected edge would make the reconcile MakeLinkTo the same pins twice = one visible wire
		// backed by two pin links (a wire-data desync the identity-blind diff can't see). A canonical
		// projection guarantees one wire per logical relationship.
		for (int32 i = 0; i < Graph.Edges.Num(); ++i)
		{
			const FClashEdge& Edge = Graph.Edges[i];
			if (!Edge.Winner.IsValid() || !Edge.Loser.IsValid())
			{
				continue;
			}
			const bool bDuplicate = Out.Edges.ContainsByPredicate([&Edge](const FClashProjectedEdge& Existing)
			{
				return Existing.Winner == Edge.Winner && Existing.Loser == Edge.Loser;
			});
			if (!bDuplicate)
			{
				Out.Edges.Add(FClashProjectedEdge{ i, Edge.Winner, Edge.Loser });
			}
		}

		return Out;
	}

	FClashProjectionDiff DiffProjection(const FClashProjection& Prev, const FClashProjection& Current, bool bAnyPositionDrift)
	{
		FClashProjectionDiff Diff;
		Diff.bAnyPositionDrift = bAnyPositionDrift;

		for (const FClashProjectedNode& Node : Current.Nodes)
		{
			if (Prev.FindNode(Node.Tag) == nullptr)
			{
				Diff.NodesToAdd.Add(Node.Tag);
			}
		}
		for (const FClashProjectedNode& Node : Prev.Nodes)
		{
			if (Current.FindNode(Node.Tag) == nullptr)
			{
				Diff.NodesToRemove.Add(Node.Tag);
			}
		}

		// Edges are rebuilt wholesale in order, so compare the ordered list.
		Diff.bEdgesEqual = (Prev.Edges.Num() == Current.Edges.Num());
		if (Diff.bEdgesEqual)
		{
			for (int32 i = 0; i < Current.Edges.Num(); ++i)
			{
				if (Prev.Edges[i] != Current.Edges[i])
				{
					Diff.bEdgesEqual = false;
					break;
				}
			}
		}

		return Diff;
	}

	bool AppendEdge(FClashGraph& Graph, const FGameplayTag& Winner, const FGameplayTag& Loser)
	{
		if (!Winner.IsValid() || !Loser.IsValid() || Winner == Loser)
		{
			return false; // invalid or self-edge ("A beats A" is meaningless — schema disallows it too)
		}
		for (const FClashEdge& Edge : Graph.Edges)
		{
			if (Edge.Winner == Winner && Edge.Loser == Loser)
			{
				return false; // already present — de-dupe (no parallel duplicate edges)
			}
		}
		FClashEdge NewEdge;
		NewEdge.Winner = Winner;
		NewEdge.Loser = Loser;
		Graph.Edges.Add(NewEdge);
		return true;
	}

	bool RemoveEdgeChecked(FClashGraph& Graph, const FGameplayTag& Winner, const FGameplayTag& Loser)
	{
		for (int32 i = 0; i < Graph.Edges.Num(); ++i)
		{
			if (Graph.Edges[i].Winner == Winner && Graph.Edges[i].Loser == Loser)
			{
				Graph.Edges.RemoveAt(i);
				return true;
			}
		}
		return false;
	}
}
