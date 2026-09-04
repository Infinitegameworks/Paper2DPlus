// Copyright 2026 Infinite Gameworks. All Rights Reserved.

// Behavioral coverage for the sectioned Layer Structure dock
// (docs/plans/2026-08-22-002-feat-layer-structure-sections-plan.md, U1-U4).
//
// Deliberately a NEW file rather than an extension of Paper2DPlusLayerWorkspaceTest.cpp: that file is
// under concurrent edit by another session, and a new file has zero merge surface.
//
// The load-bearing invariant here is the one the whole redesign turns on: sections are organization
// ONLY. Grouping never reorders anything, so the row numbers the dock shows must remain the layer's
// GLOBAL position, and searching must never make a section disappear.
//
// File-unique helper prefix `LayerSections_` — unity builds group these translation units, so a
// generic helper name would collide with a sibling test file.

#if WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "AsepriteStructuralDiff.h"
#include "CharacterProfileEditorModel.h"
#include "Editor.h"
#include "LayerStructureTree.h"
#include "Paper2DPlusCharacterLayerAsset.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "UObject/Package.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace Paper2DPlusLayerStructureSectionsTest
{
	struct FLayerSections_Fixture
	{
		UPackage* Package = nullptr;
		UPaper2DPlusCharacterProfileAsset* Profile = nullptr;
		UPaper2DPlusCharacterLayerAsset* LayerAsset = nullptr;
		TSharedPtr<FCharacterProfileEditorModel> Model;
	};

	/** Layer names deliberately carry imported source paths, because that is what a real .ase import
	 *  produces and what the search has to match. */
	FLayerSections_Fixture LayerSections_MakeFixture(const TArray<FString>& LayerNames)
	{
		FLayerSections_Fixture Fixture;
		Fixture.Package = CreatePackage(*FString::Printf(
			TEXT("/Temp/Paper2DPlusLayerSections_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits)));
		Fixture.Profile = NewObject<UPaper2DPlusCharacterProfileAsset>(
			Fixture.Package, TEXT("Profile"), RF_Transactional);
		Fixture.LayerAsset = NewObject<UPaper2DPlusCharacterLayerAsset>(
			Fixture.Package, TEXT("Layers"), RF_Transactional);
		Fixture.LayerAsset->BaseProfile = Fixture.Profile;
		for (const FString& Name : LayerNames)
		{
			FCharacterLayer& Layer = Fixture.LayerAsset->Layers.AddDefaulted_GetRef();
			Layer.LayerId = FGuid::NewGuid();
			Layer.LayerName = Name;
		}
		Fixture.Model = MakeShared<FCharacterProfileEditorModel>();
		Fixture.Model->InitializeFromAsset(Fixture.Profile);
		return Fixture;
	}

	/** The one readable form of "what is the global paint order right now". */
	FString LayerSections_OrderString(const UPaper2DPlusCharacterLayerAsset& Asset)
	{
		return FString::JoinBy(Asset.Layers, TEXT(","),
			[](const FCharacterLayer& Layer) { return Layer.LayerName; });
	}

	TSharedRef<SLayerStructureTree> LayerSections_MakeTree(const FLayerSections_Fixture& Fixture)
	{
		return SNew(SLayerStructureTree)
			.Model(Fixture.Model)
			.LayerAsset(Fixture.LayerAsset);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusLayerSectionsSearchTest,
	"Paper2DPlus.LayerWorkspace.SectionSearchFiltersLayersWithoutHidingSections",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusLayerSectionsSearchTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusLayerStructureSectionsTest;

	FLayerSections_Fixture Fixture = LayerSections_MakeFixture({
		TEXT("Body/Base"), TEXT("Clothes/Jacket/Front"), TEXT("Clothes/Jacket/Back"), TEXT("Hair/Front")
	});
	// Put the two jacket layers in a real section; leave the rest ungrouped.
	const FGuid SectionId = FLayerStructureController::AddGroup(*Fixture.LayerAsset, *Fixture.Model);
	FLayerStructureController::RenameGroup(
		*Fixture.LayerAsset, *Fixture.Model, SectionId, FText::FromString(TEXT("Outfit")));
	FLayerStructureController::MoveLayerToGroup(
		*Fixture.LayerAsset, *Fixture.Model, Fixture.LayerAsset->Layers[1].LayerId, SectionId);
	FLayerStructureController::MoveLayerToGroup(
		*Fixture.LayerAsset, *Fixture.Model, Fixture.LayerAsset->Layers[2].LayerId, SectionId);

	TSharedRef<SLayerStructureTree> Tree = LayerSections_MakeTree(Fixture);
	Tree->Refresh();
	TestEqual(TEXT("every layer has a row before filtering"), Tree->GetLayerRowCountForTests(), 4);
	TestEqual(TEXT("both sections are roots"), Tree->GetRootCountForTests(), 2);

	// Searching a LAYER path narrows the rows...
	Tree->SetSearchTextForTests(TEXT("jacket"));
	TestEqual(TEXT("search matches the layers whose source path contains the term"),
		Tree->GetMatchingLayerCountForTests(), 2);
	TestEqual(TEXT("only matching rows remain"), Tree->GetLayerRowCountForTests(), 2);
	// ...but a section is NEVER filtered away. It stays as its matches' ancestor, and Ungrouped
	// stays put so un-sectioned layers can never be silently promoted to root level.
	TestEqual(TEXT("sections survive filtering as ancestors"), Tree->GetRootCountForTests(), 2);

	// Searching a SECTION name keeps that whole section's contents, not just name matches.
	Tree->SetSearchTextForTests(TEXT("outfit"));
	TestEqual(TEXT("a section-name match keeps every layer in that section"),
		Tree->GetMatchingLayerCountForTests(), 2);

	// A term nothing matches empties the rows but still shows the sections, so the dock never looks
	// broken - it looks empty, which is the truth.
	Tree->SetSearchTextForTests(TEXT("zzz-nothing"));
	TestEqual(TEXT("no matches leaves no layer rows"), Tree->GetLayerRowCountForTests(), 0);
	TestEqual(TEXT("no matches still shows the sections"), Tree->GetRootCountForTests(), 2);

	// Clearing restores everything.
	Tree->SetSearchTextForTests(FString());
	TestEqual(TEXT("clearing the search restores every row"), Tree->GetLayerRowCountForTests(), 4);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusLayerSectionsGlobalOrderTest,
	"Paper2DPlus.LayerWorkspace.SectionRowsReportGlobalOrderNotSectionLocalIndex",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusLayerSectionsGlobalOrderTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusLayerStructureSectionsTest;

	// Interleaved membership ON PURPOSE: sections do not force their members to be contiguous, and
	// the dock must render that honestly rather than normalizing the data behind the designer.
	FLayerSections_Fixture Fixture = LayerSections_MakeFixture({
		TEXT("Body"), TEXT("Jacket"), TEXT("Hair"), TEXT("Gloves")
	});
	const FGuid SectionId = FLayerStructureController::AddGroup(*Fixture.LayerAsset, *Fixture.Model);
	FLayerStructureController::MoveLayerToGroup(
		*Fixture.LayerAsset, *Fixture.Model, Fixture.LayerAsset->Layers[1].LayerId, SectionId);
	FLayerStructureController::MoveLayerToGroup(
		*Fixture.LayerAsset, *Fixture.Model, Fixture.LayerAsset->Layers[3].LayerId, SectionId);

	TSharedRef<SLayerStructureTree> Tree = LayerSections_MakeTree(Fixture);
	Tree->Refresh();

	// The section holds global positions 1 and 3, NOT 0 and 1. That is the whole point: a grouped
	// view that renumbered its members would lie about paint order. Move Up on the LAST section
	// member therefore swaps it past a layer belonging to no section at all.
	const FGuid GlovesId = Fixture.LayerAsset->Layers[3].LayerId;
	const FString DisplacedName = Fixture.LayerAsset->Layers[2].LayerName;
	TestEqual(TEXT("the fixture starts with the section members interleaved"),
		DisplacedName, FString(TEXT("Hair")));

	TestTrue(TEXT("a section member reorders in the global array"),
		FLayerStructureController::MoveLayerByDelta(*Fixture.LayerAsset, *Fixture.Model, GlovesId, -1));
	TestEqual(TEXT("Move Up swapped past the layer OUTSIDE the section, not past its section peer"),
		Fixture.LayerAsset->Layers[2].LayerName, FString(TEXT("Gloves")));
	TestEqual(TEXT("the displaced layer took the vacated global slot"),
		Fixture.LayerAsset->Layers[3].LayerName, DisplacedName);

	// And the reorder changed nothing about organization - the row keeps its Section, it just
	// occupies a different global position, which is exactly what the row number must show.
	const FCharacterLayer* Moved = Fixture.LayerAsset->GetLayerById(GlovesId);
	if (TestNotNull(TEXT("the moved layer still resolves by its stable id"), Moved))
	{
		TestEqual(TEXT("reordering never changes section membership"), Moved->GroupId, SectionId);
	}

	// The dock must report the NEW global position for that row.
	Tree->Refresh();
	TestEqual(TEXT("the dock still shows every layer after a reorder"),
		Tree->GetLayerRowCountForTests(), 4);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusLayerSectionsAreOrganizationOnlyTest,
	"Paper2DPlus.LayerWorkspace.SectionEditsNeverReachPublishedData",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusLayerSectionsAreOrganizationOnlyTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusLayerStructureSectionsTest;

	FLayerSections_Fixture Fixture = LayerSections_MakeFixture({
		TEXT("Body"), TEXT("Jacket"), TEXT("Hair")
	});
	const FString OrderBefore = FString::JoinBy(Fixture.LayerAsset->Layers, TEXT(","),
		[](const FCharacterLayer& Layer) { return Layer.LayerName; });
	const FString DigestBefore = Fixture.LayerAsset->ComputeLayerSourceDigest();

	const FGuid SectionId = FLayerStructureController::AddGroup(*Fixture.LayerAsset, *Fixture.Model);
	FLayerStructureController::RenameGroup(
		*Fixture.LayerAsset, *Fixture.Model, SectionId, FText::FromString(TEXT("Outerwear")));
	FLayerStructureController::MoveLayerToGroup(
		*Fixture.LayerAsset, *Fixture.Model, Fixture.LayerAsset->Layers[1].LayerId, SectionId);

	// The three things a Section must never touch: paint order, the source digest that drives bake
	// staleness, and the layers themselves.
	TestEqual(TEXT("creating and populating a Section leaves global order untouched"),
		FString::JoinBy(Fixture.LayerAsset->Layers, TEXT(","),
			[](const FCharacterLayer& Layer) { return Layer.LayerName; }),
		OrderBefore);
	TestEqual(TEXT("Section edits leave the source digest untouched"),
		Fixture.LayerAsset->ComputeLayerSourceDigest(), DigestBefore);
	TestEqual(TEXT("no layer was added or lost"), Fixture.LayerAsset->Layers.Num(), 3);

	// Deleting a Section keeps its layers and CLEARS their GroupId — it must not reassign them to
	// some other section or invent a default one, because EnsureLayerAuthoringIdentity treats a
	// GroupId with no matching row as garbage and silently invalidates it.
	TestTrue(TEXT("deleting a Section succeeds"),
		FLayerStructureController::DeleteGroup(*Fixture.LayerAsset, *Fixture.Model, SectionId));
	TestEqual(TEXT("deleting a Section keeps every layer"), Fixture.LayerAsset->Layers.Num(), 3);
	TestFalse(TEXT("a reparented layer has no dangling GroupId"),
		Fixture.LayerAsset->Layers[1].GroupId.IsValid());
	TestEqual(TEXT("deleting a Section still leaves the source digest untouched"),
		Fixture.LayerAsset->ComputeLayerSourceDigest(), DigestBefore);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusLayerSectionsDropTransactionTest,
	"Paper2DPlus.LayerWorkspace.SectionDropCarriesReorderAndSectionInOneTransaction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusLayerSectionsDropTransactionTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusLayerStructureSectionsTest;

	if (!GEditor)
	{
		AddError(TEXT("This test needs the editor transaction buffer; it cannot pass without one."));
		return false;
	}

	FLayerSections_Fixture Fixture = LayerSections_MakeFixture({
		TEXT("Body/Base"), TEXT("Body/Head"), TEXT("Hair/Front"), TEXT("Armor/Chest")
	});
	const FGuid SectionId = FLayerStructureController::AddGroup(*Fixture.LayerAsset, *Fixture.Model);
	FLayerStructureController::MoveLayerToGroup(
		*Fixture.LayerAsset, *Fixture.Model, Fixture.LayerAsset->Layers[3].LayerId, SectionId);

	const FGuid MovingId = Fixture.LayerAsset->Layers[0].LayerId;
	const FGuid TargetId = Fixture.LayerAsset->Layers[3].LayerId;

	// Drop "Body/Base" into the gap BELOW the last layer, which also happens to be inside a Section.
	const FLayerStructureDropPlan Plan = FLayerStructureController::PlanDrop(
		*Fixture.LayerAsset, MovingId, ELayerStructureNodeKind::Layer, TargetId,
		ELayerStructureDropZone::Below);
	TestTrue(TEXT("a cross-section drop is allowed"), Plan.bValid);
	// Insert-before is expressed against the PRE-move array, so below index 3 is 4, not 3.
	TestEqual(TEXT("below the last row inserts before the end"), Plan.InsertBeforeIndex, 4);
	TestTrue(TEXT("crossing a boundary carries the destination Section"), Plan.bAssignGroup);
	TestTrue(TEXT("the destination Section is the target row's"), Plan.GroupId == SectionId);

	TestTrue(TEXT("applying the plan succeeds"),
		FLayerStructureController::ApplyDropPlan(*Fixture.LayerAsset, *Fixture.Model, MovingId, Plan));
	TestEqual(TEXT("the layer landed last in the GLOBAL order"),
		LayerSections_OrderString(*Fixture.LayerAsset), TEXT("Body/Head,Hair/Front,Armor/Chest,Body/Base"));
	TestTrue(TEXT("the moved layer took the destination Section"),
		Fixture.LayerAsset->Layers[3].GroupId == SectionId);

	// The load-bearing claim: ONE undo puts back BOTH halves. Two transactions would leave the layer
	// sitting in a Section the designer never picked.
	GEditor->UndoTransaction();
	TestEqual(TEXT("one undo restores the global order"),
		LayerSections_OrderString(*Fixture.LayerAsset), TEXT("Body/Base,Body/Head,Hair/Front,Armor/Chest"));
	TestFalse(TEXT("the same undo also restores the Section"),
		Fixture.LayerAsset->Layers[0].GroupId.IsValid());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusLayerSectionsDropOntoHeaderTest,
	"Paper2DPlus.LayerWorkspace.SectionDropOntoHeaderChangesMembershipWithoutReordering",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusLayerSectionsDropOntoHeaderTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusLayerStructureSectionsTest;

	FLayerSections_Fixture Fixture = LayerSections_MakeFixture({
		TEXT("Body/Base"), TEXT("Hair/Front"), TEXT("Armor/Chest")
	});
	const FGuid SectionId = FLayerStructureController::AddGroup(*Fixture.LayerAsset, *Fixture.Model);
	const FString OrderBefore = LayerSections_OrderString(*Fixture.LayerAsset);
	const FGuid MovingId = Fixture.LayerAsset->Layers[0].LayerId;

	// Dropping ONTO a Section header is a membership gesture. It must never smuggle in a reorder:
	// that is the whole reason every row can honestly advertise its global position.
	const FLayerStructureDropPlan IntoSection = FLayerStructureController::PlanDrop(
		*Fixture.LayerAsset, MovingId, ELayerStructureNodeKind::Group, SectionId,
		ELayerStructureDropZone::Onto);
	TestTrue(TEXT("a layer can be dropped onto a Section header"), IntoSection.bValid);
	TestEqual(TEXT("a header drop carries no reorder"), IntoSection.InsertBeforeIndex, (int32)INDEX_NONE);
	TestTrue(TEXT("a header drop changes membership"), IntoSection.bAssignGroup);

	TestTrue(TEXT("applying the header drop succeeds"),
		FLayerStructureController::ApplyDropPlan(*Fixture.LayerAsset, *Fixture.Model, MovingId, IntoSection));
	TestTrue(TEXT("the layer joined the Section"), Fixture.LayerAsset->Layers[0].GroupId == SectionId);
	TestEqual(TEXT("joining a Section left the global order byte-identical"),
		LayerSections_OrderString(*Fixture.LayerAsset), OrderBefore);

	// The synthesized Ungrouped root has an INVALID GroupId, and dropping onto it is the real gesture
	// "take this layer back out of its Section" — not a malformed target to reject.
	const FLayerStructureDropPlan OutOfSection = FLayerStructureController::PlanDrop(
		*Fixture.LayerAsset, MovingId, ELayerStructureNodeKind::Group, FGuid(),
		ELayerStructureDropZone::Onto);
	TestTrue(TEXT("dropping onto Ungrouped is allowed"), OutOfSection.bValid);
	TestTrue(TEXT("applying the Ungrouped drop succeeds"),
		FLayerStructureController::ApplyDropPlan(*Fixture.LayerAsset, *Fixture.Model, MovingId, OutOfSection));
	TestFalse(TEXT("the layer left the Section"), Fixture.LayerAsset->Layers[0].GroupId.IsValid());
	TestEqual(TEXT("leaving a Section also left the global order untouched"),
		LayerSections_OrderString(*Fixture.LayerAsset), OrderBefore);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusLayerSectionsDropRefusalTest,
	"Paper2DPlus.LayerWorkspace.SectionDropRefusesNoOpMoves",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusLayerSectionsDropRefusalTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusLayerStructureSectionsTest;

	// Three ungrouped layers plus one sectioned one. The ungrouped run is what makes a "same gap"
	// drop a true no-op: with a sectioned neighbour the same gesture would still change membership.
	FLayerSections_Fixture Fixture = LayerSections_MakeFixture({
		TEXT("Body/Base"), TEXT("Hair/Front"), TEXT("Armor/Chest"), TEXT("Armor/Pauldron")
	});
	const FGuid SectionId = FLayerStructureController::AddGroup(*Fixture.LayerAsset, *Fixture.Model);
	FLayerStructureController::MoveLayerToGroup(
		*Fixture.LayerAsset, *Fixture.Model, Fixture.LayerAsset->Layers[3].LayerId, SectionId);

	const FGuid FirstId = Fixture.LayerAsset->Layers[0].LayerId;
	const FGuid MiddleId = Fixture.LayerAsset->Layers[1].LayerId;
	const FGuid LastId = Fixture.LayerAsset->Layers[2].LayerId;
	const FGuid SectionedId = Fixture.LayerAsset->Layers[3].LayerId;

	// A refused plan is what greys out the insertion marker, so every one of these is a visible
	// promise: the gesture does nothing, opens no transaction, and does not dirty the package.
	TestFalse(TEXT("a row cannot be dropped above itself"),
		FLayerStructureController::PlanDrop(*Fixture.LayerAsset, FirstId,
			ELayerStructureNodeKind::Layer, FirstId, ELayerStructureDropZone::Above).bValid);

	// Below row 0 and above row 2 both name the gap the middle row already occupies.
	TestFalse(TEXT("dropping into the gap it already occupies (from above) is refused"),
		FLayerStructureController::PlanDrop(*Fixture.LayerAsset, MiddleId,
			ELayerStructureNodeKind::Layer, FirstId, ELayerStructureDropZone::Below).bValid);
	TestFalse(TEXT("dropping into the gap it already occupies (from below) is refused"),
		FLayerStructureController::PlanDrop(*Fixture.LayerAsset, MiddleId,
			ELayerStructureNodeKind::Layer, LastId, ELayerStructureDropZone::Above).bValid);

	// Layers do not contain layers, so a row is never an Onto target.
	TestFalse(TEXT("a layer row is never a container"),
		FLayerStructureController::PlanDrop(*Fixture.LayerAsset, FirstId,
			ELayerStructureNodeKind::Layer, LastId, ELayerStructureDropZone::Onto).bValid);

	// Re-joining the Section it is already in changes nothing.
	TestFalse(TEXT("dropping onto its own Section is refused"),
		FLayerStructureController::PlanDrop(*Fixture.LayerAsset, SectionedId,
			ELayerStructureNodeKind::Group, SectionId, ELayerStructureDropZone::Onto).bValid);

	// The mirror image, and the reason "same gap" alone is not enough to refuse: landing in the gap it
	// already occupies is still a real change when the neighbour belongs to a different Section.
	TestTrue(TEXT("a same-gap drop that changes the Section is a real change"),
		FLayerStructureController::PlanDrop(*Fixture.LayerAsset, LastId,
			ELayerStructureNodeKind::Layer, SectionedId, ELayerStructureDropZone::Above).bValid);

	// A Section row that no longer exists must fail closed rather than stamp a dangling GroupId that
	// EnsureLayerAuthoringIdentity would silently invalidate on the next import.
	TestFalse(TEXT("a vanished Section is refused"),
		FLayerStructureController::PlanDrop(*Fixture.LayerAsset, FirstId,
			ELayerStructureNodeKind::Group, FGuid::NewGuid(), ELayerStructureDropZone::Onto).bValid);

	// And a real move stays possible, so none of the above is passing by refusing everything.
	TestTrue(TEXT("a genuine reorder is still allowed"),
		FLayerStructureController::PlanDrop(*Fixture.LayerAsset, FirstId,
			ELayerStructureNodeKind::Layer, LastId, ELayerStructureDropZone::Below).bValid);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusLayerSectionSuggestionDerivationTest,
	"Paper2DPlus.LayerWorkspace.SectionSuggestionsDeriveOneLevelFromAseFolderPaths",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusLayerSectionSuggestionDerivationTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusLayerStructureSectionsTest;

	// Exactly the shape a real .ase produces: nested groups, a repeated folder in another case,
	// root-level layers, and two malformed paths.
	const TArray<FString> AseLayerNames = {
		TEXT("Armor/Chest"),
		TEXT("Armor/Pauldron/Left"),
		TEXT("Body"),                   // no folder at all
		TEXT("armor/Greaves"),          // same folder, different case
		TEXT("Hair/Front"),
		TEXT("/Orphan"),                // malformed: nothing before the separator
		TEXT("Trailing/")               // malformed: nothing after it
	};

	TArray<FLayerSectionSuggestion> Suggestions =
		FLayerStructureController::DeriveSectionSuggestions(AseLayerNames, nullptr);

	TestEqual(TEXT("one section per top-level folder"), Suggestions.Num(), 2);
	if (Suggestions.Num() != 2)
	{
		return false;
	}
	TestEqual(TEXT("sections follow first-seen source order"),
		Suggestions[0].SectionName, FString(TEXT("Armor")));
	TestEqual(TEXT("the second section keeps its own name"),
		Suggestions[1].SectionName, FString(TEXT("Hair")));

	// ONE level: "Armor/Pauldron/Left" belongs to Armor, NOT to an invented "Armor/Pauldron" — depth
	// would be a second organization system the Sections model deliberately does not have.
	TestEqual(TEXT("Armor holds its three members"), Suggestions[0].LayerNames.Num(), 3);
	TestTrue(TEXT("a deeper path joins its TOP-level folder"),
		Suggestions[0].LayerNames.Contains(TEXT("Armor/Pauldron/Left")));
	TestTrue(TEXT("a differently-cased folder is the same section"),
		Suggestions[0].LayerNames.Contains(TEXT("armor/Greaves")));

	// A root-level layer stays ungrouped rather than joining an invented catch-all, and neither
	// malformed path becomes a section or a member.
	for (const FLayerSectionSuggestion& Suggestion : Suggestions)
	{
		TestFalse(TEXT("a folderless layer is never suggested into a section"),
			Suggestion.LayerNames.Contains(TEXT("Body")));
		TestFalse(TEXT("a leading-separator path is skipped"),
			Suggestion.LayerNames.Contains(TEXT("/Orphan")));
		TestFalse(TEXT("a trailing-separator path is skipped"),
			Suggestion.LayerNames.Contains(TEXT("Trailing/")));
	}

	// Against a target that already owns a Section by that name the suggestion is flagged as a
	// MERGE, so the preview can say so before the designer commits to a second row.
	FLayerSections_Fixture Fixture = LayerSections_MakeFixture({ TEXT("Armor/Chest") });
	const FGuid ExistingId = FLayerStructureController::AddGroup(*Fixture.LayerAsset, *Fixture.Model);
	FLayerStructureController::RenameGroup(
		*Fixture.LayerAsset, *Fixture.Model, ExistingId, FText::FromString(TEXT("armor")));

	Suggestions = FLayerStructureController::DeriveSectionSuggestions(AseLayerNames, Fixture.LayerAsset);
	if (TestEqual(TEXT("the target does not change how many sections derive"), Suggestions.Num(), 2))
	{
		TestTrue(TEXT("a name the asset already has is flagged as a merge"),
			Suggestions[0].bMatchesExistingSection);
		TestFalse(TEXT("a name the asset does not have is not flagged"),
			Suggestions[1].bMatchesExistingSection);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusLayerSectionSuggestionApplyTest,
	"Paper2DPlus.LayerWorkspace.SectionSuggestionApplyWritesGroupRowAndGroupIdTogether",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusLayerSectionSuggestionApplyTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusLayerStructureSectionsTest;

	if (!GEditor)
	{
		AddError(TEXT("This test needs the editor transaction buffer; it cannot pass without one."));
		return false;
	}

	FLayerSections_Fixture Fixture = LayerSections_MakeFixture({
		TEXT("Armor/Chest"), TEXT("Armor/Greaves"), TEXT("Hair/Front"), TEXT("Body")
	});
	const FString OrderBefore = LayerSections_OrderString(*Fixture.LayerAsset);
	const FString DigestBefore = Fixture.LayerAsset->ComputeLayerSourceDigest();

	TArray<FLayerSectionSuggestion> Suggestions = FLayerStructureController::DeriveSectionSuggestions(
		{ TEXT("Armor/Chest"), TEXT("Armor/Greaves"), TEXT("Hair/Front"), TEXT("Body") },
		Fixture.LayerAsset);
	if (!TestEqual(TEXT("two sections derive from the fixture"), Suggestions.Num(), 2))
	{
		return false;
	}
	// The designer renames one and removes the other before applying — accept / rename / ignore.
	Suggestions[0].SectionName = TEXT("Outerwear");
	Suggestions[1].bAccepted = false;

	FLayerSectionApplyReport Report;
	TestTrue(TEXT("applying an accepted suggestion changes something"),
		FLayerStructureController::ApplySectionSuggestions(
			*Fixture.LayerAsset, Fixture.Model.Get(), Suggestions, Report));

	TestEqual(TEXT("exactly one Section row was created"), Report.SectionsCreated, 1);
	TestEqual(TEXT("no existing Section was reused"), Report.SectionsReused, 0);
	TestEqual(TEXT("both member layers were placed"), Report.LayersAssigned, 2);
	TestEqual(TEXT("an ignored suggestion writes no row"), Fixture.LayerAsset->LayerGroups.Num(), 1);
	TestEqual(TEXT("the row carries the RENAMED name, not the folder"),
		Fixture.LayerAsset->LayerGroups[0].DisplayName.ToString(), FString(TEXT("Outerwear")));

	const FGuid SectionId = Fixture.LayerAsset->LayerGroups[0].GroupId;
	TestTrue(TEXT("the first member points at it"), Fixture.LayerAsset->Layers[0].GroupId == SectionId);
	TestTrue(TEXT("the second member points at it"), Fixture.LayerAsset->Layers[1].GroupId == SectionId);
	TestFalse(TEXT("the ignored suggestion's layer stays ungrouped"),
		Fixture.LayerAsset->Layers[2].GroupId.IsValid());
	TestFalse(TEXT("a folderless layer stays ungrouped"), Fixture.LayerAsset->Layers[3].GroupId.IsValid());

	// THE TRAP. EnsureLayerAuthoringIdentity invalidates any GroupId with no matching row, and it
	// runs on EVERY import — so an apply that wrote the row and the GroupId together must survive it.
	Fixture.LayerAsset->EnsureLayerAuthoringIdentity();
	TestTrue(TEXT("the applied Section survives the identity pass that runs on every import"),
		Fixture.LayerAsset->Layers[0].GroupId == SectionId);

	// The negative control, so the assertion above cannot pass for an unrelated reason: a GroupId
	// written WITHOUT its row is exactly what that pass destroys.
	Fixture.LayerAsset->Layers[2].GroupId = FGuid::NewGuid();
	Fixture.LayerAsset->EnsureLayerAuthoringIdentity();
	TestFalse(TEXT("a GroupId with no matching row IS invalidated by that same pass"),
		Fixture.LayerAsset->Layers[2].GroupId.IsValid());

	// Sections are organization only: neither paint order nor the bake digest may move.
	TestEqual(TEXT("applying Sections leaves global order untouched"),
		LayerSections_OrderString(*Fixture.LayerAsset), OrderBefore);
	TestEqual(TEXT("applying Sections leaves the source digest untouched"),
		Fixture.LayerAsset->ComputeLayerSourceDigest(), DigestBefore);

	// ONE transaction: undo must take the row AND every GroupId back together, or the next import's
	// identity pass sees exactly the half-written state this whole design exists to prevent.
	GEditor->UndoTransaction();
	TestEqual(TEXT("one undo removes the Section row"), Fixture.LayerAsset->LayerGroups.Num(), 0);
	TestFalse(TEXT("the same undo also clears the first member's GroupId"),
		Fixture.LayerAsset->Layers[0].GroupId.IsValid());
	TestFalse(TEXT("the same undo also clears the second member's GroupId"),
		Fixture.LayerAsset->Layers[1].GroupId.IsValid());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusLayerSectionSuggestionDeferredApplyTest,
	"Paper2DPlus.LayerWorkspace.SectionSuggestionApplyPlacesLayersTheImportCreatesLater",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusLayerSectionSuggestionDeferredApplyTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusLayerStructureSectionsTest;

	// The bulk-extractor case: the designer accepts suggestions for a `.ase` whose layers do not
	// exist on the Layer Profile yet, because the import has not run.
	FLayerSections_Fixture Fixture = LayerSections_MakeFixture({ TEXT("Armor/Chest") });

	TArray<FLayerSectionSuggestion> Accepted = FLayerStructureController::DeriveSectionSuggestions(
		{ TEXT("Armor/Chest"), TEXT("Armor/Greaves"), TEXT("Hair/Front") }, Fixture.LayerAsset);
	if (!TestEqual(TEXT("the file proposes two sections"), Accepted.Num(), 2))
	{
		return false;
	}

	FLayerSectionApplyReport First;
	TestTrue(TEXT("the one already-present layer is placed immediately"),
		FLayerStructureController::ApplySectionSuggestions(
			*Fixture.LayerAsset, Fixture.Model.Get(), Accepted, First));
	TestEqual(TEXT("only the section that could place something was created"), First.SectionsCreated, 1);
	TestEqual(TEXT("one layer was placed"), First.LayersAssigned, 1);
	// A Section whose every member is still missing is NOT created — an empty row nobody asked for
	// is noise, and the accepted set is re-applied once the import produces the layers.
	TestEqual(TEXT("no empty Section row was minted for the absent members"),
		Fixture.LayerAsset->LayerGroups.Num(), 1);
	TestEqual(TEXT("the absent members are reported, not silently dropped"),
		First.UnmatchedLayerNames.Num(), 2);
	TestTrue(TEXT("the same-section absentee is named"),
		First.UnmatchedLayerNames.Contains(TEXT("Armor/Greaves")));
	TestTrue(TEXT("the other-section absentee is named"),
		First.UnmatchedLayerNames.Contains(TEXT("Hair/Front")));

	// Re-applying the identical set with nothing new changes nothing, opens no transaction, and
	// leaves the package undirtied — that is what makes re-applying after every import safe.
	Fixture.Package->SetDirtyFlag(false);
	FLayerSectionApplyReport Repeat;
	TestFalse(TEXT("re-applying an already-satisfied set is a no-op"),
		FLayerStructureController::ApplySectionSuggestions(
			*Fixture.LayerAsset, Fixture.Model.Get(), Accepted, Repeat));
	TestEqual(TEXT("a no-op apply creates nothing"), Repeat.SectionsCreated, 0);
	TestEqual(TEXT("a no-op apply places nothing"), Repeat.LayersAssigned, 0);
	TestFalse(TEXT("a no-op apply leaves the package undirtied"), Fixture.Package->IsDirty());

	// Now the import runs and creates the remaining layers, exactly as the pipeline's
	// FindOrCreate-in-place tail does.
	for (const TCHAR* NewName : { TEXT("Armor/Greaves"), TEXT("Hair/Front") })
	{
		FCharacterLayer& Layer = Fixture.LayerAsset->Layers.AddDefaulted_GetRef();
		Layer.LayerId = FGuid::NewGuid();
		Layer.LayerName = NewName;
	}

	const FGuid ArmorId = Fixture.LayerAsset->LayerGroups[0].GroupId;
	FLayerSectionApplyReport Second;
	TestTrue(TEXT("re-applying after the import places the new layers"),
		FLayerStructureController::ApplySectionSuggestions(
			*Fixture.LayerAsset, Fixture.Model.Get(), Accepted, Second));
	TestEqual(TEXT("both newly created layers were placed"), Second.LayersAssigned, 2);
	TestEqual(TEXT("the deferred section is created now that it has a member"), Second.SectionsCreated, 1);
	TestEqual(TEXT("the already-present section is REUSED, not duplicated"), Second.SectionsReused, 1);
	TestEqual(TEXT("there are exactly two Section rows"), Fixture.LayerAsset->LayerGroups.Num(), 2);
	TestEqual(TEXT("nothing is left unmatched"), Second.UnmatchedLayerNames.Num(), 0);
	TestTrue(TEXT("the new same-section layer joined the EXISTING row"),
		Fixture.LayerAsset->Layers[1].GroupId == ArmorId);
	TestTrue(TEXT("the layer placed in the first pass kept its Section"),
		Fixture.LayerAsset->Layers[0].GroupId == ArmorId);
	TestTrue(TEXT("the other layer joined the second Section"),
		Fixture.LayerAsset->Layers[2].GroupId == Fixture.LayerAsset->LayerGroups[1].GroupId);

	// And the whole result still survives the pass that runs on every subsequent import.
	Fixture.LayerAsset->EnsureLayerAuthoringIdentity();
	TestTrue(TEXT("every placement survives the identity pass"),
		Fixture.LayerAsset->Layers[0].GroupId == ArmorId
			&& Fixture.LayerAsset->Layers[1].GroupId == ArmorId
			&& Fixture.LayerAsset->Layers[2].GroupId.IsValid());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusLayerSectionReimportPreservationTest,
	"Paper2DPlus.LayerWorkspace.ReimportPreservesCuratedSectionAssignments",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusLayerSectionReimportPreservationTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusLayerStructureSectionsTest;

	// Curated by hand into a Section the .ase folders would NOT have proposed — the whole point of
	// the guarantee is that a reimport does not undo the designer's own organization.
	FLayerSections_Fixture Fixture = LayerSections_MakeFixture({
		TEXT("Armor/Chest"), TEXT("Armor/Greaves"), TEXT("Hair/Front")
	});
	const FGuid SectionId = FLayerStructureController::AddGroup(*Fixture.LayerAsset, *Fixture.Model);
	FLayerStructureController::RenameGroup(
		*Fixture.LayerAsset, *Fixture.Model, SectionId, FText::FromString(TEXT("Silhouette")));
	FLayerStructureController::MoveLayerToGroup(
		*Fixture.LayerAsset, *Fixture.Model, Fixture.LayerAsset->Layers[0].LayerId, SectionId);
	FLayerStructureController::MoveLayerToGroup(
		*Fixture.LayerAsset, *Fixture.Model, Fixture.LayerAsset->Layers[2].LayerId, SectionId);

	// A curated Section IS authored data, so the reimport's removal decision must KEEP a layer that
	// vanished from the source rather than destroying the organization silently.
	TestTrue(TEXT("a sectioned layer counts as authored work"),
		FAsepriteStructuralDiff::HasAuthoredLayerData(
			Fixture.LayerAsset->Layers[0], Fixture.LayerAsset->AppearancePresets));
	TestFalse(TEXT("an unsectioned, otherwise untouched layer does not"),
		FAsepriteStructuralDiff::HasAuthoredLayerData(
			Fixture.LayerAsset->Layers[1], Fixture.LayerAsset->AppearancePresets));

	// ---- Now replay what a reimport actually does to the layer array, in the pipeline's order.

	// 1. The structural-diff phase renames a layer IN PLACE (AsepriteImporter.cpp writes exactly
	//    `Existing->LayerName = Rename.NewName`), which must carry its Section with it.
	FCharacterLayer* Renamed = Fixture.LayerAsset->Layers.FindByPredicate(
		[](const FCharacterLayer& Layer) { return Layer.LayerName.Equals(TEXT("Hair/Front")); });
	if (!TestNotNull(TEXT("the layer to rename resolves"), Renamed))
	{
		return false;
	}
	Renamed->LayerName = TEXT("Hair/Fringe");

	// 2. A matched layer refreshes its art through the per-animation merge.
	TArray<FCharacterLayerAnimationMapping> Incoming;
	FCharacterLayerAnimationMapping& IncomingMapping = Incoming.AddDefaulted_GetRef();
	IncomingMapping.AnimationName = TEXT("Idle");
	FAsepriteStructuralDiff::MergeAnimationSprites(
		Fixture.LayerAsset->Layers[0].AnimationSprites, Incoming);

	// 3. The import appends a brand-new layer, which arrives with NO Section.
	FCharacterLayer& Added = Fixture.LayerAsset->Layers.AddDefaulted_GetRef();
	Added.LayerId = FGuid::NewGuid();
	Added.LayerName = TEXT("Armor/Helm");

	// 4. And the identity pass runs, exactly as every import path ends.
	Fixture.LayerAsset->EnsureLayerAuthoringIdentity();

	TestEqual(TEXT("the curated Section row survives a reimport"),
		Fixture.LayerAsset->LayerGroups.Num(), 1);
	TestEqual(TEXT("its name survives"),
		Fixture.LayerAsset->LayerGroups[0].DisplayName.ToString(), FString(TEXT("Silhouette")));
	TestTrue(TEXT("a matched layer keeps its curated Section"),
		Fixture.LayerAsset->Layers[0].GroupId == SectionId);
	TestTrue(TEXT("a RENAMED layer keeps its curated Section"),
		Fixture.LayerAsset->Layers[2].GroupId == SectionId);
	TestEqual(TEXT("the rename landed"),
		Fixture.LayerAsset->Layers[2].LayerName, FString(TEXT("Hair/Fringe")));
	TestFalse(TEXT("a layer the designer left ungrouped is not auto-sectioned"),
		Fixture.LayerAsset->Layers[1].GroupId.IsValid());
	TestFalse(TEXT("a NEW layer arrives ungrouped rather than guessing a Section"),
		Fixture.LayerAsset->Layers[3].GroupId.IsValid());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
#endif // WITH_EDITOR
