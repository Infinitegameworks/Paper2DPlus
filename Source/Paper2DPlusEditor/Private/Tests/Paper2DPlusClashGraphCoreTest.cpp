// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#if WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "ClashGraphCore.h"
#include "Paper2DPlusClashTypes.h"
#include "GameplayTagContainer.h"

// TASK-77 U5 — worldless tests for the pure clash-graph projection/diff core. File-unique helper prefix
// `ClashGraph_` (the unity-build rule). The native Paper2DPlusHitboxTags symbols live in the RUNTIME module
// and are not dll-exported, so this EDITOR-module test resolves the seed tags by NAME (registered at module
// load) instead of linking the symbols.

namespace
{
	FGameplayTag ClashGraph_Tag(const TCHAR* Name)
	{
		return FGameplayTag::RequestGameplayTag(FName(Name), /*ErrorIfNotFound=*/false);
	}
	FClashEdge ClashGraph_Edge(const FGameplayTag& W, const FGameplayTag& L)
	{
		FClashEdge E; E.Winner = W; E.Loser = L; return E;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusClashGraphProject, "Paper2DPlus.ClashGraph.ProjectGraph",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPaper2DPlusClashGraphProject::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusClashGraph;
	const FGameplayTag Armor = ClashGraph_Tag(TEXT("Paper2DPlus.Clash.Category.Armor"));
	const FGameplayTag Strike = ClashGraph_Tag(TEXT("Paper2DPlus.Clash.Category.Strike"));
	const FGameplayTag Throw = ClashGraph_Tag(TEXT("Paper2DPlus.Clash.Category.Throw"));
	const FGameplayTag Parry = ClashGraph_Tag(TEXT("Paper2DPlus.Clash.Category.Parry"));
	TestTrue(TEXT("Seed tags resolve by name"), Armor.IsValid() && Strike.IsValid() && Throw.IsValid() && Parry.IsValid());

	FClashGraph Graph;
	Graph.Edges = {
		ClashGraph_Edge(Armor, Strike),    // node order: Armor, Strike
		ClashGraph_Edge(Throw, Armor),     // + Throw (Armor already seen)
	};

	// Nodes = edge tags in first-appearance order (no positions).
	{
		const FClashProjection P = ProjectGraph(Graph);
		TestEqual(TEXT("3 distinct edge tags -> 3 nodes"), P.Nodes.Num(), 3);
		TestTrue(TEXT("First node is Armor (first appearance)"), P.Nodes[0].Tag == Armor);
		TestTrue(TEXT("Second is Strike"), P.Nodes[1].Tag == Strike);
		TestTrue(TEXT("Third is Throw"), P.Nodes[2].Tag == Throw);
		TestEqual(TEXT("2 edges projected"), P.Edges.Num(), 2);
		TestEqual(TEXT("Edge 0 carries source index 0"), P.Edges[0].EdgeIndex, 0);
		TestTrue(TEXT("No node has a stored position"), !P.Nodes[0].bHasStoredPosition);
	}

	// A position-only tag (NOT on any edge) still projects as a node, with its stored position.
	{
		TMap<FGameplayTag, FVector2D> Positions;
		Positions.Add(Parry, FVector2D(100.f, 50.f)); // Parry has no edge
		Positions.Add(Armor, FVector2D(10.f, 20.f));  // Armor IS on an edge
		const FClashProjection P = ProjectGraph(Graph, &Positions);
		TestEqual(TEXT("Position-only tag adds a 4th node"), P.Nodes.Num(), 4);
		const FClashProjectedNode* ParryNode = P.FindNode(Parry);
		TestNotNull(TEXT("Parry node exists from the position key"), ParryNode);
		if (ParryNode)
		{
			TestTrue(TEXT("Parry node has its stored position"), ParryNode->bHasStoredPosition && ParryNode->Position.Equals(FVector2D(100.f, 50.f)));
		}
		const FClashProjectedNode* ArmorNode = P.FindNode(Armor);
		TestTrue(TEXT("Edge tag Armor picks up its stored position too"), ArmorNode && ArmorNode->bHasStoredPosition);
	}

	// Fail-closed: an invalid edge tag / position key is skipped (no blank node).
	{
		FClashGraph Bad;
		Bad.Edges = { ClashGraph_Edge(Strike, FGameplayTag()) }; // half-invalid edge
		TMap<FGameplayTag, FVector2D> Positions;
		Positions.Add(FGameplayTag(), FVector2D::ZeroVector); // invalid key
		const FClashProjection P = ProjectGraph(Bad, &Positions);
		TestEqual(TEXT("Only the one valid tag projects"), P.Nodes.Num(), 1);
		TestEqual(TEXT("The half-invalid edge does not project"), P.Edges.Num(), 0);
	}

	// Canonical projection: duplicate flat rows (authorable via the Details array — only the graph-drag
	// AppendEdge dedupes) project to ONE edge, so the reconcile can't MakeLinkTo the same pins twice.
	{
		FClashGraph Dup;
		Dup.Edges = {
			ClashGraph_Edge(Armor, Strike),
			ClashGraph_Edge(Armor, Strike), // exact duplicate of edge 0
			ClashGraph_Edge(Throw, Armor),
		};
		const FClashProjection P = ProjectGraph(Dup);
		TestEqual(TEXT("Duplicate (Armor->Strike) rows project to ONE edge"), P.Edges.Num(), 2);
		TestEqual(TEXT("The deduped edge keeps the FIRST occurrence's source index"), P.Edges[0].EdgeIndex, 0);
		TestEqual(TEXT("Nodes unaffected (3 distinct tags)"), P.Nodes.Num(), 3);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusClashGraphDiff, "Paper2DPlus.ClashGraph.DiffAndMutate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPaper2DPlusClashGraphDiff::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusClashGraph;
	const FGameplayTag Armor = ClashGraph_Tag(TEXT("Paper2DPlus.Clash.Category.Armor"));
	const FGameplayTag Strike = ClashGraph_Tag(TEXT("Paper2DPlus.Clash.Category.Strike"));
	const FGameplayTag Throw = ClashGraph_Tag(TEXT("Paper2DPlus.Clash.Category.Throw"));

	FClashGraph Graph;
	Graph.Edges = { ClashGraph_Edge(Armor, Strike) };
	const FClashProjection Before = ProjectGraph(Graph);

	// AppendEdge de-dupes + rejects invalid/self.
	TestTrue(TEXT("Append a new edge"), AppendEdge(Graph, Throw, Armor));
	TestFalse(TEXT("Re-append the same edge is a no-op"), AppendEdge(Graph, Throw, Armor));
	TestFalse(TEXT("Self-edge rejected"), AppendEdge(Graph, Strike, Strike));
	TestFalse(TEXT("Invalid edge rejected"), AppendEdge(Graph, FGameplayTag(), Strike));
	TestEqual(TEXT("Exactly one edge added"), Graph.Edges.Num(), 2);

	// Diff: a new node (Throw) appeared, edges changed.
	const FClashProjection After = ProjectGraph(Graph);
	{
		const FClashProjectionDiff D = DiffProjection(Before, After, /*bAnyPositionDrift=*/false);
		TestEqual(TEXT("One node added (Throw)"), D.NodesToAdd.Num(), 1);
		TestTrue(TEXT("The added node is Throw"), D.NodesToAdd.Num() == 1 && D.NodesToAdd[0] == Throw);
		TestEqual(TEXT("No nodes removed"), D.NodesToRemove.Num(), 0);
		TestFalse(TEXT("Edges are NOT equal"), D.bEdgesEqual);
		TestFalse(TEXT("Diff is not empty"), D.IsEmpty());
	}

	// A pure no-op diff (same projection, no drift) is empty.
	{
		const FClashProjectionDiff D = DiffProjection(After, ProjectGraph(Graph), /*bAnyPositionDrift=*/false);
		TestTrue(TEXT("Identical projection diff is empty"), D.IsEmpty());
	}
	// Position drift alone makes the diff non-empty.
	{
		const FClashProjectionDiff D = DiffProjection(After, ProjectGraph(Graph), /*bAnyPositionDrift=*/true);
		TestFalse(TEXT("Position drift makes the diff non-empty"), D.IsEmpty());
	}

	// RemoveEdgeChecked: first-match true, miss false.
	TestTrue(TEXT("Remove the appended edge"), RemoveEdgeChecked(Graph, Throw, Armor));
	TestFalse(TEXT("Removing it again misses"), RemoveEdgeChecked(Graph, Throw, Armor));
	TestEqual(TEXT("Back to one edge"), Graph.Edges.Num(), 1);

	return true;
}

#endif // WITH_EDITOR
