// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#if WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "AnimationMap/Paper2DPlusAnimationMap.h"
#include "AnimationMap/Paper2DPlusAnimationMapNode_Move.h"
#include "AnimationMap/Paper2DPlusAnimationMapNode_Transition.h"
#include "AnimationMap/Paper2DPlusAnimationMapSchema.h"
#include "AnimationMapCore.h"
#include "AnimationMapPanel.h"
#include "CharacterProfileEditorModel.h"
#include "EdGraphNode_Comment.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "SGraphNode.h"
#include "UObject/Package.h"

namespace
{
	void AnimationMapStability_AddMove(
		UPaper2DPlusCharacterProfileAsset* Asset,
		const FString& MoveName)
	{
		FFlipbookProfileEntry Entry;
		Entry.Identity.FlipbookName = MoveName;
		Asset->Flipbooks.Add(MoveTemp(Entry));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusAnimationMapPersistedCommentIdentityTest,
	"Paper2DPlus.AnimationMap.Stability.PersistedCommentIdentitySurvivesFinalize",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusAnimationMapPersistedCommentIdentityTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusAnimationMap* Graph = NewObject<UPaper2DPlusAnimationMap>();
	const FGuid PersistedId = FGuid::NewGuid();

	UEdGraphNode_Comment* Comment =
		UPaper2DPlusAnimationMap::SpawnNodeUntransactional<UEdGraphNode_Comment>(
			*Graph,
			[PersistedId](UEdGraphNode_Comment& Node)
			{
				// MaterializeCommentsForCurrentGroup restores the persisted handle before Finalize.
				Node.NodeGuid = PersistedId;
				Node.NodeComment = TEXT("Persisted comment");
			});

	TestEqual(TEXT("Finalize preserves the configured persisted comment identity"),
		Comment->NodeGuid, PersistedId);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusAnimationMapGroupScopedFirstLayoutTest,
	"Paper2DPlus.AnimationMap.Stability.FirstLayoutIgnoresOtherGroups",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusAnimationMapGroupScopedFirstLayoutTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	AnimationMapStability_AddMove(Asset, TEXT("Jab"));
	AnimationMapStability_AddMove(Asset, TEXT("Kick"));
	AnimationMapStability_AddMove(Asset, TEXT("DistantOtherGroup"));

	TSet<FString> CurrentGroup;
	CurrentGroup.Add(TEXT("jab"));
	CurrentGroup.Add(TEXT("kick"));

	TMap<FString, FVector2D> Occupied;
	Occupied.Add(TEXT("jab"), FVector2D::ZeroVector);
	Occupied.Add(TEXT("distantothergroup"), FVector2D(5000.0, 10000.0));

	const TMap<FString, FVector2D> Fresh = Paper2DPlusAnimationMap::FirstLayout(
		Asset, Occupied, &CurrentGroup);
	const FVector2D* KickPosition = Fresh.Find(TEXT("kick"));
	if (TestNotNull(TEXT("Position-less current-group move receives a layout position"), KickPosition))
	{
		TestTrue(TEXT("Layout uses only the current group's occupied bounds"),
			KickPosition->Equals(FVector2D(0.0, 160.0)));
	}
	TestFalse(TEXT("Filtered-out group member never receives a fresh position"),
		Fresh.Contains(TEXT("distantothergroup")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusAnimationMapMoveThenExternalEditReconcileTest,
	"Paper2DPlus.AnimationMap.Stability.MoveThenExternalEditReconcilesWithoutPositionDrift",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusAnimationMapMoveThenExternalEditReconcileTest::RunTest(
	const FString& Parameters)
{
	const FString PackageName = FString::Printf(
		TEXT("/Game/Paper2DPlusTests/AnimationMapStability_%s"),
		*FGuid::NewGuid().ToString(EGuidFormats::Digits));
	UPackage* Package = CreatePackage(*PackageName);
	UPaper2DPlusCharacterProfileAsset* Asset =
		NewObject<UPaper2DPlusCharacterProfileAsset>(
			Package, TEXT("Profile"), RF_Transactional);
	AnimationMapStability_AddMove(Asset, TEXT("Jab"));
	AnimationMapStability_AddMove(Asset, TEXT("Recover"));
#if WITH_EDITORONLY_DATA
	// The full Map intentionally omits disconnected, never-placed moves. Seed Jab as an ordinary
	// placed node so this fixture exercises move/reconcile stability without changing that contract.
	Asset->AnimationMapNodePositions.Add(TEXT("jab"), FVector2D(40.0, 40.0));
#endif
	Package->SetDirtyFlag(false);

	const TSharedPtr<FCharacterProfileEditorModel> Model =
		MakeShared<FCharacterProfileEditorModel>();
	Model->InitializeFromAsset(Asset);
	const TSharedPtr<SAnimationMapPanel> Panel =
		SNew(SAnimationMapPanel).Model(Model);
	Panel->OpenAnimationMapView(TEXT("full"));
	Panel->ReconcileNowForTests();

	UPaper2DPlusAnimationMapNode_Move* JabNode =
		Panel->FindProjectedMoveNodeForTests(TEXT("Jab"));
	if (!TestNotNull(TEXT("Initial projection contains the ordinary Jab move"), JabNode))
	{
		Package->SetDirtyFlag(false);
		return false;
	}

	const FVector2D CommittedPosition(420.0, 180.0);
	JabNode->NodePosX = FMath::RoundToInt32(CommittedPosition.X);
	JabNode->NodePosY = FMath::RoundToInt32(CommittedPosition.Y);
	Panel->CommitProjectedMovePositionForTests(JabNode);

	const FVector2D* StoredPosition =
		Asset->AnimationMapNodePositions.Find(TEXT("jab"));
	TestTrue(TEXT("Move commit writes the ordinary durable position"),
		StoredPosition && StoredPosition->Equals(CommittedPosition));
	TestTrue(TEXT("Move commit dirties the profile package"), Package->IsDirty());

	// Simulate a Details/property edit arriving after the gesture. This changes topology without
	// touching the durable position and exercises the same reconcile path used by the deferred
	// external-object notification.
	Asset->Modify();
	TestTrue(TEXT("External transition edit changes the authoritative rows"),
		Paper2DPlusAnimationMap::AppendTransitionRow(
			Asset, TEXT("Jab"), TEXT("Recover")));
	Panel->ReconcileNowForTests();

	UPaper2DPlusAnimationMapNode_Move* ReconciledJab =
		Panel->FindProjectedMoveNodeForTests(TEXT("Jab"));
	TestNotNull(TEXT("Reconcile preserves the ordinary move node"), ReconciledJab);
	if (ReconciledJab)
	{
		TestEqual(TEXT("Reconcile preserves committed X"),
			ReconciledJab->NodePosX, FMath::RoundToInt32(CommittedPosition.X));
		TestEqual(TEXT("Reconcile preserves committed Y"),
			ReconciledJab->NodePosY, FMath::RoundToInt32(CommittedPosition.Y));
	}
	TestEqual(TEXT("External edit projects exactly one transition"),
		Panel->GetProjectedTransitionNodeCountForAutomation(), 1);
	TestTrue(TEXT("Reconcile keeps the profile dirty after authored writes"),
		Package->IsDirty());

	Package->SetDirtyFlag(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusAnimationMapEdgeIdentityDiffTest,
	"Paper2DPlus.AnimationMap.Stability.EdgeDiffPreservesSurvivingIdentities",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusAnimationMapEdgeIdentityDiffTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusAnimationMap;

	auto MakeEdge = [](const TCHAR* From, const TCHAR* To)
	{
		FProjectedEdge Edge;
		Edge.FromMoveLower = From;
		Edge.TargetMoveLower = To;
		return Edge;
	};

	FGraphProjection Before;
	Before.Edges.Add(MakeEdge(TEXT("jab"), TEXT("hit")));
	Before.Edges.Add(MakeEdge(TEXT("hit"), TEXT("recover")));

	FGraphProjection Reordered = Before;
	Swap(Reordered.Edges[0], Reordered.Edges[1]);
	const FProjectionDiff ReorderDiff = DiffProjection(Before, Reordered);
	TestFalse(TEXT("Authored row reorder still requests row-handle restamping"), ReorderDiff.bEdgesEqual);
	TestEqual(TEXT("A reorder creates no transition pills"), ReorderDiff.EdgesToAdd.Num(), 0);
	TestEqual(TEXT("A reorder removes no transition pills"), ReorderDiff.EdgesToRemove.Num(), 0);

	FGraphProjection Replaced = Reordered;
	Replaced.Edges[0] = MakeEdge(TEXT("hit"), TEXT("idle"));
	const FProjectionDiff ReplaceDiff = DiffProjection(Before, Replaced);
	TestEqual(TEXT("Endpoint replacement creates exactly one identity"), ReplaceDiff.EdgesToAdd.Num(), 1);
	TestEqual(TEXT("Endpoint replacement removes exactly one identity"), ReplaceDiff.EdgesToRemove.Num(), 1);
	if (ReplaceDiff.EdgesToAdd.Num() == 1 && ReplaceDiff.EdgesToRemove.Num() == 1)
	{
		TestTrue(TEXT("The new endpoint is the only added identity"),
			EdgeValueTuplesEqual(ReplaceDiff.EdgesToAdd[0], MakeEdge(TEXT("hit"), TEXT("idle"))));
		TestTrue(TEXT("The old endpoint is the only removed identity"),
			EdgeValueTuplesEqual(ReplaceDiff.EdgesToRemove[0], MakeEdge(TEXT("hit"), TEXT("recover"))));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusAnimationMapTransitionRewireGripDrawingTest,
	"Paper2DPlus.AnimationMap.Stability.TransitionRewireGripDoesNotExposeSecondLink",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusAnimationMapTransitionRewireGripDrawingTest::RunTest(
	const FString& Parameters)
{
	// The rewire grip is an SGraphPin, and SGraphPin::Construct requires its pin to resolve a schema
	// through the owning graph. An orphan NewObject node has no graph, so GetSchema() returns null and
	// the widget's check() aborts the WHOLE automation run rather than failing this one test. Build the
	// node inside a real schema'd graph, exactly as SAnimationMapPanel does.
	UPaper2DPlusAnimationMap* Graph =
		NewObject<UPaper2DPlusAnimationMap>(GetTransientPackage(), NAME_None, RF_Transient);
	Graph->Schema = UPaper2DPlusAnimationMapSchema::StaticClass();

	// SpawnNodeUntransactional allocates the default pins through FGraphNodeCreator::Finalize().
	UPaper2DPlusAnimationMapNode_Transition* TransitionNode =
		UPaper2DPlusAnimationMap::SpawnNodeUntransactional<UPaper2DPlusAnimationMapNode_Transition>(
			*Graph, [](UPaper2DPlusAnimationMapNode_Transition&) {});

	const TSharedPtr<SGraphNode> TransitionWidget =
		TransitionNode->CreateVisualWidget();
	if (TestTrue(TEXT("Transition node creates its visual widget"),
		TransitionWidget.IsValid()))
	{
		TArray<TSharedRef<SWidget>> RegisteredPins;
		TransitionWidget->GetPins(RegisteredPins);
		TestEqual(
			TEXT("Rewire grip stays out of graph-panel pin geometry discovery"),
			RegisteredPins.Num(),
			0);
	}
	return true;
}

#endif // WITH_EDITOR
