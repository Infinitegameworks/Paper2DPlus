// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#if WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "AnimationMapCore.h"
#include "CharacterProfileEditorModel.h"
#include "Editor.h" // GEditor undo for the group-change transaction test
#include "GameplayTagContainer.h"
#include "Paper2DPlusAnimationTagQuery.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Paper2DPlusMoveTransition.h"
#include "Paper2DPlusSettings.h"
#include "ProfileDetailsPanel.h"
#include "ProfileToolPanelProvider.h"
#include "ScopedTransaction.h"
#include "PaperFlipbook.h"

/**
 * TASK-108 U6 surfacing tests (worldless): the browser search predicate's tag dimension (R7,
 * AnimationSearchMatches), the Map tag-filter match (R8, AnimationTagFilterMatches), the shared
 * chip/badge color precedence pin (R11, GetAnimationTagChipColor — registry exact > registry
 * ancestor > convention fallback > neutral; registry mutated on the CDO array DIRECTLY and restored,
 * never via SetTagColor/ClearTagColor, which flush DefaultGame.ini), and the "Change Group…" data
 * semantics (R16 — home-tag no-op guard + one-transaction move: source order preserved, appended
 * last, new key created, single undo restores both sides, effective tags re-batch).
 * Helpers are AnimTagSurf_-prefixed per the unity-build file-unique-name rule; tag locals avoid the
 * bare name `Tag` (C4458 shadow rule).
 */

namespace
{
	FGameplayTag AnimTagSurf_Tag(const TCHAR* TagName)
	{
		return FGameplayTag::RequestGameplayTag(FName(TagName), /*ErrorIfNotFound=*/false);
	}

	/** Add a valid named profile member backed by a transient flipbook. */
	int32 AnimTagSurf_AddMove(UPaper2DPlusCharacterProfileAsset* Asset, const FString& MoveName)
	{
		FFlipbookProfileEntry Anim;
		Anim.Identity.FlipbookName = MoveName;
		Anim.Identity.Flipbook = NewObject<UPaperFlipbook>(Asset);
		return Asset->Flipbooks.Add(Anim);
	}

	/** Add MoveName to the mapping group keyed InGroupTag (creates the mapping when absent). */
	void AnimTagSurf_AddToGroup(
		UPaper2DPlusCharacterProfileAsset* Asset,
		const FGameplayTag& InGroupTag,
		const FString& MoveName,
		bool bIsChainStart = false)
	{
		FFlipbookTagMappingEntry Entry(MoveName);
		Entry.bIsChainStart = bIsChainStart;
		Asset->TagMappings.FindOrAdd(InGroupTag).Entries.Add(Entry);
	}

	/** Ordered flipbook names of a mapping group (empty when the key is absent). */
	TArray<FString> AnimTagSurf_GroupNames(const UPaper2DPlusCharacterProfileAsset* Asset, const FGameplayTag& InGroupTag)
	{
		TArray<FString> Names;
		if (const FFlipbookTagMapping* Mapping = Asset->TagMappings.Find(InGroupTag))
		{
			for (const FFlipbookTagMappingEntry& Entry : Mapping->Entries)
			{
				Names.Add(Entry.FlipbookName);
			}
		}
		return Names;
	}

	/** Append a pure From→To transition row onto FromName's entry (U7 chain-order fixtures). */
	void AnimTagSurf_AddTransition(UPaper2DPlusCharacterProfileAsset* Asset, const FString& FromName, const FString& ToName)
	{
		for (FFlipbookProfileEntry& Anim : Asset->Flipbooks)
		{
			if (Anim.Identity.FlipbookName.Equals(FromName, ESearchCase::IgnoreCase))
			{
				Anim.TransitionData.Transitions.Add(FPaper2DPlusMoveTransition(ToName));
				return;
			}
		}
	}
}

// ─── R7: the search predicate matches names, tag leaves, and full tag paths ─────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusAnimTagSurfSearchPredicateTest,
	"Paper2DPlus.AnimationTagSurfacing.SearchPredicate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusAnimTagSurfSearchPredicateTest::RunTest(const FString& Parameters)
{
	namespace CG = Paper2DPlusAnimationMap;

	const FGameplayTag AirborneTag = AnimTagSurf_Tag(TEXT("Paper2DPlus.Animation.Context.Airborne"));
	const FGameplayTag CombatGroupTag = AnimTagSurf_Tag(TEXT("Paper2DPlus.Animation.Combat"));
	if (!TestTrue(TEXT("Native taxonomy tags registered"), AirborneTag.IsValid() && CombatGroupTag.IsValid()))
	{
		return false;
	}

	// SkyKick: own Context.Airborne tag + a member of the Combat mapping group -> effective tags carry
	// both the own tag and the group key (the batch is the SAME source the queries use).
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	const int32 SkyKick = AnimTagSurf_AddMove(Asset, TEXT("SkyKick"));
	Asset->Flipbooks[SkyKick].EditorMeta.AnimationTags.AddTag(AirborneTag);
	AnimTagSurf_AddToGroup(Asset, CombatGroupTag, TEXT("SkyKick"));

	const TMap<FString, Paper2DPlusAnimationTagQuery::FAnimationTagSet> TagMap =
		Paper2DPlusAnimationTagQuery::BuildAnimationTagMap(Asset);
	const Paper2DPlusAnimationTagQuery::FAnimationTagSet* TagSet = TagMap.Find(TEXT("skykick"));
	if (!TestNotNull(TEXT("Batch has SkyKick"), TagSet))
	{
		return false;
	}
	const FGameplayTagContainer& Effective = TagSet->EffectiveTags;

	// Name matching still works (the pre-existing dimension).
	TestTrue(TEXT("Name substring matches"), CG::AnimationSearchMatches(TEXT("SkyKick"), Effective, TEXT("kyki")));
	// Tag LEAF name (case-insensitive substring).
	TestTrue(TEXT("'airborne' matches the Context.Airborne LEAF"), CG::AnimationSearchMatches(TEXT("SkyKick"), Effective, TEXT("airborne")));
	TestTrue(TEXT("Leaf match is case-insensitive"), CG::AnimationSearchMatches(TEXT("SkyKick"), Effective, TEXT("AIRBORNE")));
	// Tag FULL path substring.
	TestTrue(TEXT("Full-path substring matches"), CG::AnimationSearchMatches(TEXT("SkyKick"), Effective, TEXT("animation.context")));
	// Group-implied effective tag matches too ("search by group" with zero authoring).
	TestTrue(TEXT("Group key leaf matches (group-implied effective tag)"), CG::AnimationSearchMatches(TEXT("SkyKick"), Effective, TEXT("combat")));
	// Negative + empty.
	TestFalse(TEXT("Unrelated query does not match"), CG::AnimationSearchMatches(TEXT("SkyKick"), Effective, TEXT("swimming")));
	TestTrue(TEXT("Empty query matches everything"), CG::AnimationSearchMatches(TEXT("SkyKick"), Effective, FString()));
	// No tags -> name-only behavior.
	TestFalse(TEXT("No tags + non-name query does not match"),
		CG::AnimationSearchMatches(TEXT("Idle"), FGameplayTagContainer(), TEXT("airborne")));

	return true;
}

// ─── R8: the Map tag-filter match (hierarchical, invalid filter = off) ───────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusAnimTagSurfFilterPredicateTest,
	"Paper2DPlus.AnimationTagSurfacing.FilterPredicate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusAnimTagSurfFilterPredicateTest::RunTest(const FString& Parameters)
{
	namespace CG = Paper2DPlusAnimationMap;

	const FGameplayTag CombatTag = AnimTagSurf_Tag(TEXT("Paper2DPlus.Animation.Combat"));
	const FGameplayTag HeavyTag = AnimTagSurf_Tag(TEXT("Paper2DPlus.Animation.Combat.Heavy"));
	const FGameplayTag ContextTag = AnimTagSurf_Tag(TEXT("Paper2DPlus.Animation.Context"));
	if (!TestTrue(TEXT("Native taxonomy tags registered"), CombatTag.IsValid() && HeavyTag.IsValid() && ContextTag.IsValid()))
	{
		return false;
	}

	FGameplayTagContainer HeavyOnly;
	HeavyOnly.AddTag(HeavyTag);
	FGameplayTagContainer CombatOnly;
	CombatOnly.AddTag(CombatTag);

	// Hierarchical DOWNWARD: a parent filter matches descendant container tags…
	TestTrue(TEXT("Filter Combat matches {Combat.Heavy}"), CG::AnimationTagFilterMatches(HeavyOnly, CombatTag));
	TestTrue(TEXT("Filter Combat.Heavy matches {Combat.Heavy} (exact)"), CG::AnimationTagFilterMatches(HeavyOnly, HeavyTag));
	// …but a CHILD filter does not match a container holding only the parent.
	TestFalse(TEXT("Filter Combat.Heavy does NOT match {Combat}"), CG::AnimationTagFilterMatches(CombatOnly, HeavyTag));
	// Cross-dimension miss.
	TestFalse(TEXT("Filter Context does NOT match {Combat.Heavy}"), CG::AnimationTagFilterMatches(HeavyOnly, ContextTag));
	// Filter off (invalid tag) matches everything, including an empty container.
	TestTrue(TEXT("Invalid filter matches a tagged container"), CG::AnimationTagFilterMatches(HeavyOnly, FGameplayTag()));
	TestTrue(TEXT("Invalid filter matches an EMPTY container"), CG::AnimationTagFilterMatches(FGameplayTagContainer(), FGameplayTag()));
	// Active filter vs empty container -> no match (the node dims).
	TestFalse(TEXT("Active filter does not match an empty container"), CG::AnimationTagFilterMatches(FGameplayTagContainer(), CombatTag));

	return true;
}

// ─── R11: the shared chip/badge color precedence pin ─────────────────────────────────────────────────
// Registry EXACT beats registry ANCESTOR beats code-level CONVENTION beats neutral. Registry rows are
// mutated on the settings CDO array DIRECTLY and restored via ON_SCOPE_EXIT — NEVER SetTagColor/
// ClearTagColor (they TryUpdateDefaultConfigFile-flush DefaultGame.ini: the config-pollution gotcha).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusAnimTagSurfColorPrecedenceTest,
	"Paper2DPlus.AnimationTagSurfacing.ColorPrecedence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusAnimTagSurfColorPrecedenceTest::RunTest(const FString& Parameters)
{
	namespace CG = Paper2DPlusAnimationMap;

	UPaper2DPlusSettings* Settings = GetMutableDefault<UPaper2DPlusSettings>();
	if (!TestNotNull(TEXT("Settings CDO exists"), Settings))
	{
		return false;
	}

	const FGameplayTag CombatParent = AnimTagSurf_Tag(TEXT("Paper2DPlus.Animation.Combat"));
	const FGameplayTag HeavyTag = AnimTagSurf_Tag(TEXT("Paper2DPlus.Animation.Combat.Heavy"));
	const FGameplayTag AirborneTag = AnimTagSurf_Tag(TEXT("Paper2DPlus.Animation.Context.Airborne"));
	const FGameplayTag PhaseParent = AnimTagSurf_Tag(TEXT("Paper2DPlus.Phase"));
	const FGameplayTag ActivePhase = AnimTagSurf_Tag(TEXT("Paper2DPlus.Phase.Active"));
	if (!TestTrue(TEXT("Native tags registered"),
		CombatParent.IsValid() && HeavyTag.IsValid() && AirborneTag.IsValid() && PhaseParent.IsValid() && ActivePhase.IsValid()))
	{
		return false;
	}

	// Snapshot + clear the registry; restore at scope exit by direct CDO-array assignment.
	const TArray<FPaper2DPlusTagColor> SavedRegistry = Settings->TagColors;
	Settings->TagColors.Reset();
	ON_SCOPE_EXIT
	{
		GetMutableDefault<UPaper2DPlusSettings>()->TagColors = SavedRegistry;
	};

	// 1. NO registry -> code-level CONVENTION fallback palettes.
	{
		const CG::FTagChipColor HeavyChip = CG::GetAnimationTagChipColor(HeavyTag);
		TestTrue(TEXT("No registry: Combat.Heavy gets the Combat convention (deep red)"),
			HeavyChip.Color.Equals(FLinearColor(0.68f, 0.22f, 0.20f)));

		const CG::FTagChipColor AirChip = CG::GetAnimationTagChipColor(AirborneTag);
		TestTrue(TEXT("No registry: Context.Airborne gets the Context convention (sky)"),
			AirChip.Color.Equals(FLinearColor(0.32f, 0.62f, 0.85f)));
		TestFalse(TEXT("Combat and Context conventions are distinct"), HeavyChip.Color.Equals(AirChip.Color));

		// Phase family: the chip helper reads GetPhaseTagBadge's SAME leaf table (extend-the-seam pin).
		const CG::FTagChipColor PhaseChip = CG::GetAnimationTagChipColor(ActivePhase);
		const CG::FPhaseTagBadge PhaseBadge = CG::GetPhaseTagBadge(ActivePhase);
		TestTrue(TEXT("No registry: Phase.Active chip == the badge convention (Active green)"),
			PhaseChip.Color.Equals(PhaseBadge.Color) && PhaseBadge.Color.Equals(FLinearColor(0.30f, 0.75f, 0.35f)));

		// Outside every family -> neutral.
		const FGameplayTag OutsideTag = AnimTagSurf_Tag(TEXT("Paper2DPlus.Combat.Var"));
		if (OutsideTag.IsValid())
		{
			TestTrue(TEXT("No registry + no family: neutral gray"),
				CG::GetAnimationTagChipColor(OutsideTag).Color.Equals(FLinearColor(0.45f, 0.45f, 0.50f)));
		}
	}

	const FLinearColor ParentBlue(0.f, 0.f, 1.f, 1.f);
	const FLinearColor ExactRed(1.f, 0.f, 0.f, 1.f);

	// 2. Registry entry on the PARENT -> the ANCESTOR color wins over the convention.
	Settings->TagColors.Reset();
	Settings->TagColors.Emplace(CombatParent, ParentBlue);
	TestTrue(TEXT("Ancestor registry entry beats the Combat convention"),
		CG::GetAnimationTagChipColor(HeavyTag).Color.Equals(ParentBlue));

	// 3. EXACT registry entry beats the ancestor entry.
	Settings->TagColors.Emplace(HeavyTag, ExactRed);
	TestTrue(TEXT("Exact registry entry beats the ancestor entry"),
		CG::GetAnimationTagChipColor(HeavyTag).Color.Equals(ExactRed));

	// 4. Badge/chip precedence is IDENTICAL: a registry color on the Phase PARENT overrides the
	// convention green for Active on BOTH surfaces.
	Settings->TagColors.Reset();
	Settings->TagColors.Emplace(PhaseParent, ParentBlue);
	TestTrue(TEXT("Phase parent registry entry overrides convention green on the badge"),
		CG::GetPhaseTagBadge(ActivePhase).Color.Equals(ParentBlue));
	TestTrue(TEXT("…and identically on the chip helper"),
		CG::GetAnimationTagChipColor(ActivePhase).Color.Equals(ParentBlue));

	return true;
}

// ─── R16: "Change Group…" data semantics — home guard, order, append-last, new key, one undo ─────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusAnimTagSurfGroupChangeTest,
	"Paper2DPlus.AnimationTagSurfacing.GroupChange",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusAnimTagSurfGroupChangeTest::RunTest(const FString& Parameters)
{
	namespace CG = Paper2DPlusAnimationMap;

	if (!TestNotNull(TEXT("GEditor available for transaction tests"), GEditor))
	{
		return false;
	}

	const FGameplayTag CombatTag = AnimTagSurf_Tag(TEXT("Paper2DPlus.Animation.Combat"));
	const FGameplayTag ContextTag = AnimTagSurf_Tag(TEXT("Paper2DPlus.Animation.Context"));
	const FGameplayTag GrabTag = AnimTagSurf_Tag(TEXT("Paper2DPlus.Animation.Combat.Grab"));
	if (!TestTrue(TEXT("Native taxonomy tags registered"), CombatTag.IsValid() && ContextTag.IsValid() && GrabTag.IsValid()))
	{
		return false;
	}

	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	Asset->SetFlags(RF_Transactional);
	AnimTagSurf_AddMove(Asset, TEXT("A"));
	AnimTagSurf_AddMove(Asset, TEXT("B"));
	AnimTagSurf_AddMove(Asset, TEXT("C"));
	AnimTagSurf_AddMove(Asset, TEXT("D"));
	AnimTagSurf_AddToGroup(Asset, CombatTag, TEXT("A"));
	AnimTagSurf_AddToGroup(Asset, CombatTag, TEXT("B"));
	AnimTagSurf_AddToGroup(Asset, CombatTag, TEXT("C"));
	AnimTagSurf_AddToGroup(Asset, ContextTag, TEXT("D"));
	const FVector2D BSourcePosition(220.0, 160.0);
	const FVector2D CSourcePosition(440.0, 160.0);
	Asset->AnimationMapNodePositions.Add(TEXT("b"), BSourcePosition);
	Asset->AnimationMapNodePositions.Add(TEXT("c"), CSourcePosition);

	// Home resolution (the panel's no-op guard dimension).
	TestTrue(TEXT("B's home group is Combat"), CG::FindHomeAnimationGroupTag(Asset, TEXT("B")) == CombatTag);
	TestTrue(TEXT("Home lookup is case-insensitive"), CG::FindHomeAnimationGroupTag(Asset, TEXT("b")) == CombatTag);
	TestFalse(TEXT("Unmapped animation has no home"), CG::FindHomeAnimationGroupTag(Asset, TEXT("Nope")).IsValid());
	// Moving to the CURRENT group is the guard's no-op: home == target, the panel never transacts.
	TestTrue(TEXT("No-op guard dimension: home == target for B->Combat"),
		CG::FindHomeAnimationGroupTag(Asset, TEXT("B")) == CombatTag);
	TestTrue(TEXT("Same-group no-op retains B's existing placement"),
		Asset->AnimationMapNodePositions.Contains(TEXT("b")));

	GEditor->ResetTransaction(NSLOCTEXT("Paper2DPlusAnimTagSurfTest", "ResetGroupChange", "GroupChange Reset"));

	// ONE transaction moving B: Combat -> Context (the panel funnel's exact data call).
	{
		FScopedTransaction Transaction(NSLOCTEXT("Paper2DPlusAnimTagSurfTest", "ChangeGroup", "Change Animation Group"));
		Asset->Modify();
		Asset->AssignFlipbookToTagMapping(ContextTag, TEXT("B"), nullptr);
		Asset->AnimationMapNodePositions.Remove(TEXT("b"));
	}

	// Source order preserved; appended LAST on the target (after D).
	{
		const TArray<FString> CombatNames = AnimTagSurf_GroupNames(Asset, CombatTag);
		TestEqual(TEXT("Combat keeps 2 entries"), CombatNames.Num(), 2);
		if (CombatNames.Num() == 2)
		{
			TestEqual(TEXT("Combat order preserved [0]=A"), CombatNames[0], FString(TEXT("A")));
			TestEqual(TEXT("Combat order preserved [1]=C"), CombatNames[1], FString(TEXT("C")));
		}
		const TArray<FString> ContextNames = AnimTagSurf_GroupNames(Asset, ContextTag);
		TestEqual(TEXT("Context gains B"), ContextNames.Num(), 2);
		if (ContextNames.Num() == 2)
		{
			TestEqual(TEXT("Existing entry undisturbed [0]=D"), ContextNames[0], FString(TEXT("D")));
			TestEqual(TEXT("B appended LAST"), ContextNames[1], FString(TEXT("B")));
		}
		// Effective tags re-batch reflects the move (group-implied key flips Combat -> Context).
		const TMap<FString, Paper2DPlusAnimationTagQuery::FAnimationTagSet> TagMap =
			Paper2DPlusAnimationTagQuery::BuildAnimationTagMap(Asset);
		const Paper2DPlusAnimationTagQuery::FAnimationTagSet* BTags = TagMap.Find(TEXT("b"));
		if (TestNotNull(TEXT("Batch has B"), BTags))
		{
			TestTrue(TEXT("B's group-implied tags carry Context"), BTags->GroupImpliedTags.HasTagExact(ContextTag));
			TestFalse(TEXT("B's group-implied tags no longer carry Combat"), BTags->GroupImpliedTags.HasTagExact(CombatTag));
		}
		TestFalse(TEXT("A real group move clears B's old-group placement"),
			Asset->AnimationMapNodePositions.Contains(TEXT("b")));
	}

	// ONE undo restores BOTH sides.
	TestTrue(TEXT("Undo succeeds"), GEditor->UndoTransaction(true));
	{
		const TArray<FString> CombatNames = AnimTagSurf_GroupNames(Asset, CombatTag);
		TestEqual(TEXT("Undo restores Combat to 3 entries"), CombatNames.Num(), 3);
		if (CombatNames.Num() == 3)
		{
			TestEqual(TEXT("Undo restores B at its original position"), CombatNames[1], FString(TEXT("B")));
		}
		const TArray<FString> ContextNames = AnimTagSurf_GroupNames(Asset, ContextTag);
		TestEqual(TEXT("Undo removes B from Context"), ContextNames.Num(), 1);
		const FVector2D* RestoredPosition = Asset->AnimationMapNodePositions.Find(TEXT("b"));
		if (TestNotNull(TEXT("Undo restores B's source-group placement"), RestoredPosition))
		{
			TestTrue(TEXT("Undo restores B's exact source-group coordinates"),
				RestoredPosition->Equals(BSourcePosition));
		}
	}

	// A multi-target funnel performs the same ordered writes inside ONE transaction. One undo must
	// restore every selected move and both mapping arrays together.
	{
		FScopedTransaction Transaction(NSLOCTEXT(
			"Paper2DPlusAnimTagSurfTest", "ChangeGroupsBatch", "Change Animation Groups"));
		Asset->Modify();
		Asset->AssignFlipbookToTagMapping(ContextTag, TEXT("B"), nullptr);
		Asset->AssignFlipbookToTagMapping(ContextTag, TEXT("C"), nullptr);
		Asset->AnimationMapNodePositions.Remove(TEXT("b"));
		Asset->AnimationMapNodePositions.Remove(TEXT("c"));
	}
	{
		const TArray<FString> ContextNames = AnimTagSurf_GroupNames(Asset, ContextTag);
		TestEqual(TEXT("Batch target holds D plus both selected moves"), ContextNames.Num(), 3);
		if (ContextNames.Num() == 3)
		{
			TestEqual(TEXT("Batch preserves the existing target member"), ContextNames[0], FString(TEXT("D")));
			TestEqual(TEXT("Batch appends selected move B first"), ContextNames[1], FString(TEXT("B")));
			TestEqual(TEXT("Batch appends selected move C second"), ContextNames[2], FString(TEXT("C")));
		}
		TestFalse(TEXT("Batch clears B's old-group placement"), Asset->AnimationMapNodePositions.Contains(TEXT("b")));
		TestFalse(TEXT("Batch clears C's old-group placement"), Asset->AnimationMapNodePositions.Contains(TEXT("c")));
	}
	TestTrue(TEXT("One undo restores the complete multi-group batch"), GEditor->UndoTransaction(true));
	TestEqual(TEXT("Batch undo restores all three Combat members"),
		AnimTagSurf_GroupNames(Asset, CombatTag).Num(), 3);
	TestEqual(TEXT("Batch undo restores Context to its original member"),
		AnimTagSurf_GroupNames(Asset, ContextTag).Num(), 1);
	TestTrue(TEXT("Batch undo restores B's placement"),
		Asset->AnimationMapNodePositions.FindRef(TEXT("b")).Equals(BSourcePosition));
	TestTrue(TEXT("Batch undo restores C's placement"),
		Asset->AnimationMapNodePositions.FindRef(TEXT("c")).Equals(CSourcePosition));

	// A NEW tag (no mapping key yet) creates the key and appends the entry.
	TestFalse(TEXT("Grab mapping absent before the move"), Asset->TagMappings.Contains(GrabTag));
	{
		FScopedTransaction Transaction(NSLOCTEXT("Paper2DPlusAnimTagSurfTest", "ChangeGroupNew", "Change Animation Group (new)"));
		Asset->Modify();
		Asset->AssignFlipbookToTagMapping(GrabTag, TEXT("C"), nullptr);
		Asset->AnimationMapNodePositions.Remove(TEXT("c"));
	}
	TestTrue(TEXT("New mapping key created"), Asset->TagMappings.Contains(GrabTag));
	{
		const TArray<FString> GrabNames = AnimTagSurf_GroupNames(Asset, GrabTag);
		TestEqual(TEXT("New group holds the moved entry"), GrabNames.Num(), 1);
		if (GrabNames.Num() == 1)
		{
			TestEqual(TEXT("Moved entry is C"), GrabNames[0], FString(TEXT("C")));
		}
		TestTrue(TEXT("C's home is now the new group"), CG::FindHomeAnimationGroupTag(Asset, TEXT("C")) == GrabTag);
		// Effective tags re-batch reflects the new-key move too (group-implied key flips Combat -> Grab).
		const TMap<FString, Paper2DPlusAnimationTagQuery::FAnimationTagSet> TagMap =
			Paper2DPlusAnimationTagQuery::BuildAnimationTagMap(Asset);
		const Paper2DPlusAnimationTagQuery::FAnimationTagSet* CTags = TagMap.Find(TEXT("c"));
		if (TestNotNull(TEXT("Batch has C"), CTags))
		{
			TestTrue(TEXT("C's group-implied tags carry the new Grab key"), CTags->GroupImpliedTags.HasTagExact(GrabTag));
			TestFalse(TEXT("C's group-implied tags no longer carry Combat"), CTags->GroupImpliedTags.HasTagExact(CombatTag));
		}
		TestFalse(TEXT("New-key group move clears C's previous placement"),
			Asset->AnimationMapNodePositions.Contains(TEXT("c")));
	}

	GEditor->ResetTransaction(NSLOCTEXT("Paper2DPlusAnimTagSurfTest", "EndGroupChange", "GroupChange End"));
	return true;
}

// ─── Bulk exact-container tags + transition-target replacement remain one-step undoable ───────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusAnimMapBulkTagsAndRewireDataTest,
	"Paper2DPlus.AnimationMap.BulkTagsAndTransitionRewireData",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusAnimMapBulkTagsAndRewireDataTest::RunTest(const FString& Parameters)
{
	namespace CG = Paper2DPlusAnimationMap;
	if (!TestNotNull(TEXT("GEditor available for bulk authoring transaction tests"), GEditor))
	{
		return false;
	}
	const FGameplayTag HeavyTag = AnimTagSurf_Tag(TEXT("Paper2DPlus.Animation.Combat.Heavy"));
	const FGameplayTag AirborneTag = AnimTagSurf_Tag(TEXT("Paper2DPlus.Animation.Context.Airborne"));
	if (!TestTrue(TEXT("Bulk tag fixtures are registered"), HeavyTag.IsValid() && AirborneTag.IsValid()))
	{
		return false;
	}

	GEditor->ResetTransaction(NSLOCTEXT(
		"Paper2DPlusAnimMapBulkTest", "Reset", "Animation Map Bulk Reset"));
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>(
		GetTransientPackage(), NAME_None, RF_Transactional);
	const int32 AIndex = AnimTagSurf_AddMove(Asset, TEXT("A"));
	const int32 BIndex = AnimTagSurf_AddMove(Asset, TEXT("B"));
	AnimTagSurf_AddMove(Asset, TEXT("C"));
	Asset->Flipbooks[AIndex].EditorMeta.AnimationTags.AddTag(HeavyTag);
	Asset->Flipbooks[BIndex].EditorMeta.AnimationTags.AddTag(AirborneTag);
	AnimTagSurf_AddTransition(Asset, TEXT("A"), TEXT("B"));

	FGameplayTagContainer HeavyOnly;
	HeavyOnly.AddTag(HeavyTag);
	{
		FScopedTransaction Transaction(NSLOCTEXT(
			"Paper2DPlusAnimMapBulkTest", "SetTags", "Set Animation Tag Containers"));
		Asset->Modify();
		// A is the same-value no-op target; B is the one real write in the shared transaction.
		if (!(Asset->Flipbooks[AIndex].EditorMeta.AnimationTags == HeavyOnly))
		{
			Asset->Flipbooks[AIndex].EditorMeta.AnimationTags = HeavyOnly;
		}
		if (!(Asset->Flipbooks[BIndex].EditorMeta.AnimationTags == HeavyOnly))
		{
			Asset->Flipbooks[BIndex].EditorMeta.AnimationTags = HeavyOnly;
		}
	}
	TestTrue(TEXT("Bulk exact replacement gives both targets the same complete container"),
		Asset->Flipbooks[AIndex].EditorMeta.AnimationTags == HeavyOnly
		&& Asset->Flipbooks[BIndex].EditorMeta.AnimationTags == HeavyOnly);
	TestTrue(TEXT("One undo restores the mixed containers before the bulk edit"),
		GEditor->UndoTransaction(true));
	TestTrue(TEXT("Bulk tag undo restores A's Heavy container"),
		Asset->Flipbooks[AIndex].EditorMeta.AnimationTags.HasTagExact(HeavyTag));
	TestTrue(TEXT("Bulk tag undo restores B's Airborne container"),
		Asset->Flipbooks[BIndex].EditorMeta.AnimationTags.HasTagExact(AirborneTag));

	{
		FScopedTransaction Transaction(NSLOCTEXT(
			"Paper2DPlusAnimMapBulkTest", "ClearTags", "Set Animation Tag Containers"));
		Asset->Modify();
		Asset->Flipbooks[AIndex].EditorMeta.AnimationTags.Reset();
		Asset->Flipbooks[BIndex].EditorMeta.AnimationTags.Reset();
	}
	TestTrue(TEXT("Explicit empty-container replacement clears every target"),
		Asset->Flipbooks[AIndex].EditorMeta.AnimationTags.IsEmpty()
		&& Asset->Flipbooks[BIndex].EditorMeta.AnimationTags.IsEmpty());
	TestTrue(TEXT("One undo restores the complete bulk clear"), GEditor->UndoTransaction(true));

	TestEqual(TEXT("Rewire fixture starts at A->B"),
		Asset->Flipbooks[AIndex].TransitionData.Transitions[0].TargetMove, FString(TEXT("B")));
	TestEqual(TEXT("A->C is not initially a duplicate"),
		CG::FindTransitionRowIndex(Asset, TEXT("A"), TEXT("C")), INDEX_NONE);
	{
		FScopedTransaction Transaction(NSLOCTEXT(
			"Paper2DPlusAnimMapBulkTest", "Rewire", "Rewire Transition Target"));
		Asset->Modify();
		Asset->Flipbooks[AIndex].TransitionData.Transitions[0].TargetMove = TEXT("C");
	}
	TestEqual(TEXT("Atomic target replacement stores A->C"),
		Asset->Flipbooks[AIndex].TransitionData.Transitions[0].TargetMove, FString(TEXT("C")));
	TestTrue(TEXT("One undo restores the original transition target"), GEditor->UndoTransaction(true));
	TestEqual(TEXT("Rewire undo restores A->B"),
		Asset->Flipbooks[AIndex].TransitionData.Transitions[0].TargetMove, FString(TEXT("B")));

	AnimTagSurf_AddTransition(Asset, TEXT("A"), TEXT("C"));
	TestTrue(TEXT("Duplicate-target preflight detects an existing A->C pair"),
		CG::FindTransitionRowIndex(Asset, TEXT("A"), TEXT("C")) != INDEX_NONE);
	TestEqual(TEXT("Duplicate refusal leaves the original A->B row intact"),
		Asset->Flipbooks[AIndex].TransitionData.Transitions[0].TargetMove, FString(TEXT("B")));

	GEditor->ResetTransaction(NSLOCTEXT(
		"Paper2DPlusAnimMapBulkTest", "End", "Animation Map Bulk End"));
	return true;
}

// ─── R10 (U7): the "By Tag Group" lens section derivation — pure BuildTagLensSections ────────────────
// Pins: sections per NON-EMPTY TagMappings key SORTED by tag name + the "Unmapped" fallback LAST
// (LOCKED name); chain clusters lead in root→finisher order, the rest alphabetical; dual-membership
// shows in BOTH sections; Unmapped omits mapped animations; dangling entries are skipped and a key
// with NO resolvable member emits NO section (legacy-cleanup 2026-07 — emptied mappings vanish);
// a cyclic chain never hangs and keeps DFS first-visit order; empty asset → NO sections.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusAnimTagSurfTagLensSectionsTest,
	"Paper2DPlus.AnimationTagSurfacing.TagLensSections",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusAnimTagSurfTagLensSectionsTest::RunTest(const FString& Parameters)
{
	namespace CG = Paper2DPlusAnimationMap;

	const FGameplayTag CombatTag = AnimTagSurf_Tag(TEXT("Paper2DPlus.Animation.Combat"));
	const FGameplayTag ContextTag = AnimTagSurf_Tag(TEXT("Paper2DPlus.Animation.Context"));
	const FGameplayTag GrabTag = AnimTagSurf_Tag(TEXT("Paper2DPlus.Animation.Combat.Grab"));
	if (!TestTrue(TEXT("Native taxonomy tags registered"), CombatTag.IsValid() && ContextTag.IsValid() && GrabTag.IsValid()))
	{
		return false;
	}

	// Empty asset -> NO sections (pinned: not even an empty Unmapped fallback).
	{
		UPaper2DPlusCharacterProfileAsset* EmptyAsset = NewObject<UPaper2DPlusCharacterProfileAsset>();
		TestEqual(TEXT("Empty asset yields zero sections"), CG::BuildTagLensSections(EmptyAsset).Num(), 0);
		TestEqual(TEXT("Null asset yields zero sections"), CG::BuildTagLensSections(nullptr).Num(), 0);
	}

	// Main fixture: a chain Jab→Cross→Slam (Combat Root 1 = Jab), non-chain Combat members authored
	// OUT of alphabetical order, Cross dual-homed in Context, two unmapped moves, a dangling entry,
	// and a mapping key whose only entry dangles (the empty-section pin).
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	for (const TCHAR* MoveName : { TEXT("Jab"), TEXT("Cross"), TEXT("Slam"), TEXT("Windup"),
		TEXT("Aaa"), TEXT("Dodge"), TEXT("Zeta"), TEXT("Alpha") })
	{
		AnimTagSurf_AddMove(Asset, MoveName);
	}
	AnimTagSurf_AddTransition(Asset, TEXT("Jab"), TEXT("Cross"));
	AnimTagSurf_AddTransition(Asset, TEXT("Cross"), TEXT("Slam"));
	// Combat authored in NON-chain, NON-alphabetical order — the lens must reorder.
	for (const TCHAR* MemberName : { TEXT("Slam"), TEXT("Windup"), TEXT("Jab"), TEXT("Aaa"), TEXT("Cross"), TEXT("Ghost") })
	{
		AnimTagSurf_AddToGroup(
			Asset, CombatTag, MemberName,
			FString(MemberName) == TEXT("Jab") ? 1 : 0); // "Ghost" dangles
	}
	AnimTagSurf_AddToGroup(Asset, ContextTag, TEXT("Dodge"));
	AnimTagSurf_AddToGroup(Asset, ContextTag, TEXT("Cross")); // dual membership with Combat
	AnimTagSurf_AddToGroup(Asset, GrabTag, TEXT("Ghost"));    // dangling-only key -> NO section (vanishes)

	const TArray<CG::FTagLensSection> Sections = CG::BuildTagLensSections(Asset);
	if (!TestEqual(TEXT("Two non-empty mapping-key sections + the Unmapped fallback (dangling-only key vanishes)"), Sections.Num(), 3))
	{
		return false;
	}

	// Section order: sorted tag names (Combat < Context; Combat.Grab resolved nothing → no section),
	// Unmapped LAST.
	TestTrue(TEXT("[0] = Combat"), Sections[0].GroupTag == CombatTag);
	TestEqual(TEXT("Combat title is taxonomy-relative"), Sections[0].Title, FString(TEXT("Combat")));
	TestTrue(TEXT("[1] = Context"), Sections[1].GroupTag == ContextTag);
	TestTrue(TEXT("[2] = the Unmapped fallback, LAST"), Sections[2].bUnmapped);
	TestEqual(TEXT("Fallback is named 'Unmapped' (LOCKED)"), Sections[2].Title, FString(TEXT("Unmapped")));
	TestFalse(TEXT("Fallback carries no group tag"), Sections[2].GroupTag.IsValid());

	// Combat: chain cluster root→finisher [Jab, Cross, Slam], then the rest alphabetical [Aaa, Windup];
	// the dangling "Ghost" is skipped.
	{
		const TArray<FString> Expected = { TEXT("Jab"), TEXT("Cross"), TEXT("Slam"), TEXT("Aaa"), TEXT("Windup") };
		TestEqual(TEXT("Combat member count (dangling skipped)"), Sections[0].MemberNames.Num(), Expected.Num());
		if (Sections[0].MemberNames.Num() == Expected.Num())
		{
			for (int32 Index = 0; Index < Expected.Num(); ++Index)
			{
				TestEqual(FString::Printf(TEXT("Combat[%d]"), Index), Sections[0].MemberNames[Index], Expected[Index]);
			}
		}
		// Parallel indices resolve to the same entries.
		if (Sections[0].MemberIndices.Num() == Expected.Num())
		{
			for (int32 Index = 0; Index < Expected.Num(); ++Index)
			{
				const int32 FlipbookIndex = Sections[0].MemberIndices[Index];
				TestTrue(TEXT("Member index valid"), Asset->Flipbooks.IsValidIndex(FlipbookIndex));
				if (Asset->Flipbooks.IsValidIndex(FlipbookIndex))
				{
					TestEqual(TEXT("Member index matches name"),
						Asset->Flipbooks[FlipbookIndex].Identity.FlipbookName, Sections[0].MemberNames[Index]);
				}
			}
		}
	}

	// Context: Cross appears HERE TOO (dual membership = correct duplication), chain part first.
	{
		const TArray<FString> Expected = { TEXT("Cross"), TEXT("Dodge") };
		TestEqual(TEXT("Context member count"), Sections[1].MemberNames.Num(), Expected.Num());
		if (Sections[1].MemberNames.Num() == Expected.Num())
		{
			TestEqual(TEXT("Context[0] = Cross (chain member leads)"), Sections[1].MemberNames[0], Expected[0]);
			TestEqual(TEXT("Context[1] = Dodge (alphabetical rest)"), Sections[1].MemberNames[1], Expected[1]);
		}
	}

	// Unmapped: alphabetical (no chain members), and OMITS every mapped animation.
	{
		const TArray<FString> Expected = { TEXT("Alpha"), TEXT("Zeta") };
		TestEqual(TEXT("Unmapped member count"), Sections[2].MemberNames.Num(), Expected.Num());
		if (Sections[2].MemberNames.Num() == Expected.Num())
		{
			TestEqual(TEXT("Unmapped[0] = Alpha"), Sections[2].MemberNames[0], Expected[0]);
			TestEqual(TEXT("Unmapped[1] = Zeta"), Sections[2].MemberNames[1], Expected[1]);
		}
		TestFalse(TEXT("Unmapped omits mapped animations"), Sections[2].MemberNames.Contains(TEXT("Jab")));
	}

	// Cyclic chain: R→A→B→A must terminate (cycle-cut) and keep DFS first-visit order [R, A, B].
	{
		UPaper2DPlusCharacterProfileAsset* CycleAsset = NewObject<UPaper2DPlusCharacterProfileAsset>();
		for (const TCHAR* MoveName : { TEXT("R"), TEXT("A"), TEXT("B") })
		{
			AnimTagSurf_AddMove(CycleAsset, MoveName);
			AnimTagSurf_AddToGroup(
				CycleAsset, CombatTag, MoveName,
				FString(MoveName) == TEXT("R") ? 1 : 0);
		}
		AnimTagSurf_AddTransition(CycleAsset, TEXT("R"), TEXT("A"));
		AnimTagSurf_AddTransition(CycleAsset, TEXT("A"), TEXT("B"));
		AnimTagSurf_AddTransition(CycleAsset, TEXT("B"), TEXT("A")); // the cycle

		const TArray<CG::FTagLensSection> CycleSections = CG::BuildTagLensSections(CycleAsset);
		if (TestEqual(TEXT("Cycle asset: one section, no Unmapped"), CycleSections.Num(), 1)
			&& TestEqual(TEXT("Cycle section member count"), CycleSections[0].MemberNames.Num(), 3))
		{
			TestEqual(TEXT("Cycle[0] = R (root)"), CycleSections[0].MemberNames[0], FString(TEXT("R")));
			TestEqual(TEXT("Cycle[1] = A (first visit preserved)"), CycleSections[0].MemberNames[1], FString(TEXT("A")));
			TestEqual(TEXT("Cycle[2] = B (finisher; revisit of A cut)"), CycleSections[0].MemberNames[2], FString(TEXT("B")));
		}
	}

	return true;
}

// U5: focused Tags authoring resolves a stable animation identity after reorder, emits one shared
// model refresh per real edit (the Map/chips observer seam), and no-ops same-value edits.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusAnimTagFocusedPanelMutationTest,
	"Paper2DPlus.AnimationTagSurfacing.FocusedPanelStableIdentityAndSingleRefresh",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusAnimTagFocusedPanelMutationTest::RunTest(const FString& Parameters)
{
	if (!TestNotNull(TEXT("GEditor available for transaction tests"), GEditor))
	{
		return false;
	}
	const FGameplayTag HeavyTag = AnimTagSurf_Tag(TEXT("Paper2DPlus.Animation.Combat.Heavy"));
	const FGameplayTag ActivePhase = AnimTagSurf_Tag(TEXT("Paper2DPlus.Phase.Active"));
	if (!TestTrue(TEXT("Animation and phase tags are registered"), HeavyTag.IsValid() && ActivePhase.IsValid()))
	{
		return false;
	}
	GEditor->ResetTransaction(NSLOCTEXT(
		"Paper2DPlusAnimTagSurfTest", "ResetFocusedTags", "Focused Tags Reset"));

	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>(
		GetTransientPackage(), NAME_None, RF_Transactional);
	const int32 SlashIndex = AnimTagSurf_AddMove(Asset, TEXT("Slash"));
	AnimTagSurf_AddMove(Asset, TEXT("Idle"));
	TSharedRef<FCharacterProfileEditorModel> Model = MakeShared<FCharacterProfileEditorModel>();
	Model->InitializeFromAsset(Asset);
	Model->SetSelectedFlipbook(SlashIndex);
	TSharedRef<SProfileDetailsPanel> TagsPanel = SNew(SProfileDetailsPanel)
		.Model(TSharedPtr<FCharacterProfileEditorModel>(Model))
		.PaneMode(EProfileDetailsPaneMode::Tags);

	TestTrue(TEXT("focused Tags panel constructs both safe tag-authoring surfaces"),
		TagsPanel->HasAnimationTagsAuthoringSurfaceForTests());
	const FProfileAnimationIdentity SlashIdentity =
		Paper2DPlusProfileToolProvider::MakeAnimationIdentity(Asset, SlashIndex);
	Asset->Flipbooks.Swap(0, 1); // delayed picker action must not keep the old array index
	Model->NotifyAssetDataChanged(); // the external reorder reconciles selection before the delayed pick

	int32 MapRefreshes = 0;
	const FDelegateHandle RefreshHandle = Model->OnAssetDataChanged.AddLambda([&MapRefreshes]()
	{
		++MapRefreshes;
	});
	const int32 InitialTagPanelRefreshes = TagsPanel->GetAnimationTagsRefreshCountForTests();

	FGameplayTagContainer NewAnimationTags;
	NewAnimationTags.AddTag(HeavyTag);
	TestTrue(TEXT("animation-tag edit resolves Slash after reorder"),
		TagsPanel->CommitAnimationTags(SlashIdentity, NewAnimationTags));
	const int32 LiveSlashIndex = Paper2DPlusProfileToolProvider::ResolveAnimationIndex(Asset, SlashIdentity);
	TestTrue(TEXT("Slash receives its own Heavy tag, not the row now at the stale index"),
		Asset->Flipbooks.IsValidIndex(LiveSlashIndex)
		&& Asset->Flipbooks[LiveSlashIndex].EditorMeta.AnimationTags.HasTagExact(HeavyTag));
	TestEqual(TEXT("one animation-tag edit emits one Map/model refresh"), MapRefreshes, 1);
	TestEqual(TEXT("focused chips rebuild exactly once for the animation-tag edit"),
		TagsPanel->GetAnimationTagsRefreshCountForTests(), InitialTagPanelRefreshes + 1);

	const TMap<FString, Paper2DPlusAnimationTagQuery::FAnimationTagSet> TagMap =
		Paper2DPlusAnimationTagQuery::BuildAnimationTagMap(Asset);
	const Paper2DPlusAnimationTagQuery::FAnimationTagSet* SlashTags = TagMap.Find(TEXT("slash"));
	TestTrue(TEXT("the shared query/map projection sees the authored Heavy tag immediately"),
		SlashTags && SlashTags->OwnTags.HasTagExact(HeavyTag));

	const int32 BeforePhaseRefreshes = TagsPanel->GetAnimationTagsRefreshCountForTests();
	TestTrue(TEXT("phase edit uses the same stable identity funnel"),
		TagsPanel->CommitPhaseTag(SlashIdentity, ActivePhase));
	TestTrue(TEXT("phase stored on live Slash row"),
		Asset->Flipbooks.IsValidIndex(LiveSlashIndex)
		&& Asset->Flipbooks[LiveSlashIndex].EditorMeta.PhaseTag == ActivePhase);
	TestEqual(TEXT("one phase edit emits one additional Map/model refresh"), MapRefreshes, 2);
	TestEqual(TEXT("phase picker/chips surface rebuilds exactly once"),
		TagsPanel->GetAnimationTagsRefreshCountForTests(), BeforePhaseRefreshes + 1);

	TestFalse(TEXT("same animation tags are a no-op"),
		TagsPanel->CommitAnimationTags(SlashIdentity, NewAnimationTags));
	TestFalse(TEXT("same phase tag is a no-op"),
		TagsPanel->CommitPhaseTag(SlashIdentity, ActivePhase));
	TestEqual(TEXT("no-op edits emit no extra refresh"), MapRefreshes, 2);

	Model->OnAssetDataChanged.Remove(RefreshHandle);
	GEditor->ResetTransaction(NSLOCTEXT(
		"Paper2DPlusAnimTagSurfTest", "EndFocusedTags", "Focused Tags End"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusAnimTagChainTagsContextPanelMutationTest,
	"Paper2DPlus.AnimationTagSurfacing.ChainTagsContextPanelMutationIsTransactional",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusAnimTagChainTagsContextPanelMutationTest::RunTest(const FString& Parameters)
{
	if (!TestNotNull(TEXT("GEditor available for Chain Tags transaction test"), GEditor))
	{
		return false;
	}
	GEditor->ResetTransaction(NSLOCTEXT(
		"Paper2DPlusAnimTagSurfTest", "ResetChainTags", "Chain Tags Reset"));
	const FGameplayTag RootGroup = AnimTagSurf_Tag(TEXT("Paper2DPlus.Animation.Combat"));
	const FGameplayTag HeavyTag = AnimTagSurf_Tag(TEXT("Paper2DPlus.Animation.Combat.Heavy"));
	if (!TestTrue(TEXT("Chain fixture tags are registered"),
		RootGroup.IsValid() && HeavyTag.IsValid()))
	{
		return false;
	}

	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>(
		GetTransientPackage(), NAME_None, RF_Transactional);
	const int32 RootAIndex = AnimTagSurf_AddMove(Asset, TEXT("RootA"));
	const int32 RootBIndex = AnimTagSurf_AddMove(Asset, TEXT("RootB"));
	AnimTagSurf_AddToGroup(Asset, RootGroup, TEXT("RootA"));
	AnimTagSurf_AddToGroup(Asset, RootGroup, TEXT("RootB"), /*bIsChainStart=*/true);

	TSharedRef<FCharacterProfileEditorModel> Model = MakeShared<FCharacterProfileEditorModel>();
	Model->InitializeFromAsset(Asset);
	Model->SetSelectedFlipbook(RootAIndex);
	TSharedRef<SProfileDetailsPanel> Panel = SNew(SProfileDetailsPanel)
		.Model(TSharedPtr<FCharacterProfileEditorModel>(Model))
		.PaneMode(EProfileDetailsPaneMode::Tags);
	TestFalse(TEXT("Chain Tags stays hidden for a uniquely mapped non-opener"),
		Panel->HasChainTagsAuthoringSurfaceForTests());
	TestFalse(TEXT("The selected-animation seam rejects a non-opener"),
		Panel->CommitSelectedChainTagsForTests(FGameplayTagContainer()));

	Model->SetSelectedFlipbook(RootBIndex);
	TestTrue(TEXT("Chain Tags appears for the uniquely mapped selected opener"),
		Panel->HasChainTagsAuthoringSurfaceForTests());

	int32 Refreshes = 0;
	const FDelegateHandle RefreshHandle = Model->OnAssetDataChanged.AddLambda([&Refreshes]()
	{
		++Refreshes;
	});
	FGameplayTagContainer NewChainTags;
	NewChainTags.AddTag(HeavyTag);
	TestFalse(TEXT("Writing the opener's same empty container is a no-op"),
		Panel->CommitSelectedChainTagsForTests(FGameplayTagContainer()));
	TestTrue(TEXT("The contextual Chain Tags funnel commits the selected opener"),
		Panel->CommitSelectedChainTagsForTests(NewChainTags));
	TestTrue(TEXT("The exact opener entry stores the chain identity"),
		Asset->TagMappings[RootGroup].Entries[1].ChainTags.HasTagExact(HeavyTag)
		&& Asset->TagMappings[RootGroup].Entries[1].ChainTags.Num() == 1);
	TestEqual(TEXT("One real Chain Tags edit emits one model refresh"), Refreshes, 1);
	TestFalse(TEXT("An exact-set-equal Chain Tags write is a no-op"),
		Panel->CommitSelectedChainTagsForTests(NewChainTags));
	TestEqual(TEXT("The no-op emits no additional model refresh"), Refreshes, 1);
	TestTrue(TEXT("The Chain Tags commit creates one undoable transaction"),
		GEditor->UndoTransaction(true));
	TestTrue(TEXT("Undo restores the prior empty chain container"),
		Asset->TagMappings[RootGroup].Entries[1].ChainTags.IsEmpty());

	FFlipbookTagMappingEntry AmbiguousEntry(TEXT("RootB"));
	AmbiguousEntry.bIsChainStart = true;
	Asset->TagMappings.FindOrAdd(HeavyTag).Entries.Add(AmbiguousEntry);
	Model->NotifyAssetDataChanged();
	TestFalse(TEXT("Chain Tags hides when the selected move belongs to multiple groups"),
		Panel->HasChainTagsAuthoringSurfaceForTests());
	TestFalse(TEXT("Ambiguous multi-group identity fails closed before mutation"),
		Panel->CommitSelectedChainTagsForTests(NewChainTags));
	TestTrue(TEXT("The refused ambiguous write leaves both entries untouched"),
		Asset->TagMappings[RootGroup].Entries[1].ChainTags.IsEmpty()
		&& Asset->TagMappings[HeavyTag].Entries[0].ChainTags.IsEmpty());

	Asset->TagMappings.Reset();
	FFlipbookTagMappingEntry InvalidGroupEntry(TEXT("RootB"));
	InvalidGroupEntry.bIsChainStart = true;
	Asset->TagMappings.FindOrAdd(FGameplayTag()).Entries.Add(InvalidGroupEntry);
	Model->NotifyAssetDataChanged();
	TestFalse(TEXT("Chain Tags hides when the selected opener belongs only to an invalid group key"),
		Panel->HasChainTagsAuthoringSurfaceForTests());
	TestFalse(TEXT("An invalid group identity fails closed before Chain Tags mutation"),
		Panel->CommitSelectedChainTagsForTests(NewChainTags));
	TestTrue(TEXT("The refused invalid-group write leaves the chain identity untouched"),
		Asset->TagMappings[FGameplayTag()].Entries[0].ChainTags.IsEmpty());

	Model->OnAssetDataChanged.Remove(RefreshHandle);
	GEditor->ResetTransaction(NSLOCTEXT(
		"Paper2DPlusAnimTagSurfTest", "EndChainTags", "Chain Tags End"));
	return true;
}

// ─── Combo main-line step badges: projection stamps the runtime spine; the diff sees renumbering ────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusAnimTagSurfComboSpineStampTest,
	"Paper2DPlus.AnimationTagSurfacing.ComboSpineProjectionStamp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusAnimTagSurfComboSpineStampTest::RunTest(const FString& Parameters)
{
	namespace CG = Paper2DPlusAnimationMap;

	const FGameplayTag CombatGroupTag = AnimTagSurf_Tag(TEXT("Paper2DPlus.Animation.Combat"));
	if (!TestTrue(TEXT("Native group tag registered"), CombatGroupTag.IsValid()))
	{
		return false;
	}

	// Jab (chain start) with an authored-first shortcut to Finisher; the main line runs through Jab2.
	// Alt is a reachable side branch — it must project NO step badge (INDEX_NONE).
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	AnimTagSurf_AddMove(Asset, TEXT("Jab"));
	AnimTagSurf_AddMove(Asset, TEXT("Jab2"));
	AnimTagSurf_AddMove(Asset, TEXT("Finisher"));
	AnimTagSurf_AddMove(Asset, TEXT("Alt"));
	AnimTagSurf_AddToGroup(Asset, CombatGroupTag, TEXT("Jab"), /*bIsChainStart=*/true);
	AnimTagSurf_AddToGroup(Asset, CombatGroupTag, TEXT("Jab2"));
	AnimTagSurf_AddToGroup(Asset, CombatGroupTag, TEXT("Finisher"));
	AnimTagSurf_AddToGroup(Asset, CombatGroupTag, TEXT("Alt"));
	AnimTagSurf_AddTransition(Asset, TEXT("Jab"), TEXT("Finisher")); // shortcut, row 0
	AnimTagSurf_AddTransition(Asset, TEXT("Jab"), TEXT("Jab2"));
	AnimTagSurf_AddTransition(Asset, TEXT("Jab"), TEXT("Alt"));
	AnimTagSurf_AddTransition(Asset, TEXT("Jab2"), TEXT("Finisher"));

	auto FindNode = [](const CG::FGraphProjection& Projection, const TCHAR* NameLower)
		-> const CG::FProjectedNode*
	{
		return Projection.Nodes.FindByPredicate([NameLower](const CG::FProjectedNode& Node)
		{
			return Node.MoveNameLower == NameLower;
		});
	};

	const CG::FGraphProjection Before = CG::ProjectGraph(Asset);
	const CG::FProjectedNode* JabNode = FindNode(Before, TEXT("jab"));
	const CG::FProjectedNode* Jab2Node = FindNode(Before, TEXT("jab2"));
	const CG::FProjectedNode* FinisherNode = FindNode(Before, TEXT("finisher"));
	const CG::FProjectedNode* AltNode = FindNode(Before, TEXT("alt"));
	if (!TestTrue(TEXT("All four nodes project"), JabNode && Jab2Node && FinisherNode && AltNode))
	{
		return false;
	}
	TestEqual(TEXT("The root stamps step 0"), JabNode->ComboSpineIndex, 0);
	TestEqual(TEXT("The main line runs through Jab2 (step 1), not the authored-first shortcut"),
		Jab2Node->ComboSpineIndex, 1);
	TestEqual(TEXT("Finisher is step 2"), FinisherNode->ComboSpineIndex, 2);
	TestEqual(TEXT("Spine length stamps on every main-line node"), FinisherNode->ComboSpineLength, 3);
	TestEqual(TEXT("A side-branch member gets no step"), AltNode->ComboSpineIndex, (int32)INDEX_NONE);

	// Removing the long route renumbers Finisher (the shortcut becomes the main line) — the diff must
	// surface that as ComboSpineChanges even though Finisher's own rows never changed.
	for (FFlipbookProfileEntry& Anim : Asset->Flipbooks)
	{
		if (Anim.Identity.FlipbookName == TEXT("Jab2"))
		{
			Anim.TransitionData.Transitions.Reset();
		}
	}
	const CG::FGraphProjection After = CG::ProjectGraph(Asset);
	const CG::FProjectedNode* FinisherAfter = FindNode(After, TEXT("finisher"));
	if (!TestNotNull(TEXT("Finisher still projects"), FinisherAfter))
	{
		return false;
	}
	TestEqual(TEXT("The shortcut is now the main line — Finisher renumbers to step 1"),
		FinisherAfter->ComboSpineIndex, 1);
	const CG::FProjectionDiff Diff = CG::DiffProjection(Before, After);
	TestTrue(TEXT("The diff surfaces Finisher's renumbering"),
		Diff.ComboSpineChanges.Contains(TEXT("finisher")));
	TestFalse(TEXT("A renumbering diff is not empty"), Diff.IsEmpty());
	return true;
}

// ─── Multi-edit targeting: clicked-selected batches in projection order; otherwise one ─────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusAnimMapMultiEditTargetingTest,
	"Paper2DPlus.AnimationMap.MultiEditTargeting",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusAnimMapMultiEditTargetingTest::RunTest(const FString& Parameters)
{
	namespace CG = Paper2DPlusAnimationMap;

	CG::FGraphProjection Projection;
	auto AddNode = [&Projection](const TCHAR* Lower, const TCHAR* Display, bool bStub = false)
	{
		CG::FProjectedNode Node;
		Node.MoveNameLower = Lower;
		Node.DisplayName = Display;
		Node.bIsStub = bStub;
		Projection.Nodes.Add(Node);
	};
	AddNode(TEXT("alpha"), TEXT("Alpha"));
	AddNode(TEXT("bravo"), TEXT("Bravo"));
	AddNode(TEXT("charlie"), TEXT("Charlie"));
	AddNode(TEXT("missing"), TEXT("Missing"), /*bStub=*/true);

	TSet<FString> Selected{ TEXT("bravo"), TEXT("alpha"), TEXT("missing") };
	const TArray<FString> SelectedTargets =
		CG::ResolveMultiEditTargetNames(TEXT("bravo"), Selected, Projection);
	TestEqual(TEXT("Clicked-selected returns two real targets"), SelectedTargets.Num(), 2);
	if (SelectedTargets.Num() == 2)
	{
		TestEqual(TEXT("Bulk targets follow projection/authored order [0]"),
			SelectedTargets[0], FString(TEXT("Alpha")));
		TestEqual(TEXT("Bulk targets follow projection/authored order [1]"),
			SelectedTargets[1], FString(TEXT("Bravo")));
	}

	const TArray<FString> UnselectedTarget =
		CG::ResolveMultiEditTargetNames(TEXT("charlie"), Selected, Projection);
	TestEqual(TEXT("Clicked-unselected stays single-target"), UnselectedTarget.Num(), 1);
	if (UnselectedTarget.Num() == 1)
	{
		TestEqual(TEXT("Clicked-unselected returns the invoked move"),
			UnselectedTarget[0], FString(TEXT("Charlie")));
	}
	TestEqual(TEXT("A stub cannot become a bulk-edit target"),
		CG::ResolveMultiEditTargetNames(TEXT("missing"), Selected, Projection).Num(), 0);
	TestEqual(TEXT("An unknown requested move produces no targets"),
		CG::ResolveMultiEditTargetNames(TEXT("unknown"), Selected, Projection).Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusAnimMapIndependentEdgePhaseCommitTest,
	"Paper2DPlus.AnimationMap.TransitionPhaseCommitIsPerEdge",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusAnimMapIndependentEdgePhaseCommitTest::RunTest(const FString& Parameters)
{
	if (!TestNotNull(TEXT("GEditor available for transition phase transaction"), GEditor))
	{
		return false;
	}
	const FGameplayTag ActiveTag = AnimTagSurf_Tag(TEXT("Paper2DPlus.Phase.Active"));
	const FGameplayTag RecoveryTag = AnimTagSurf_Tag(TEXT("Paper2DPlus.Phase.Recovery"));
	if (!TestTrue(TEXT("Native phase tags registered"), ActiveTag.IsValid() && RecoveryTag.IsValid()))
	{
		return false;
	}

	GEditor->ResetTransaction(NSLOCTEXT(
		"Paper2DPlusAnimTagSurfTest", "ResetIndependentEdgePhase", "Independent Edge Phase Reset"));
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>(
		GetTransientPackage(), NAME_None, RF_Transactional);
	AnimTagSurf_AddMove(Asset, TEXT("FromA"));
	AnimTagSurf_AddMove(Asset, TEXT("FromB"));
	const int32 TargetIndex = AnimTagSurf_AddMove(Asset, TEXT("SharedTarget"));
	Asset->Flipbooks[TargetIndex].EditorMeta.PhaseTag = ActiveTag;
	AnimTagSurf_AddTransition(Asset, TEXT("FromA"), TEXT("SharedTarget"));
	AnimTagSurf_AddTransition(Asset, TEXT("FromB"), TEXT("SharedTarget"));

	TSharedRef<FCharacterProfileEditorModel> Model = MakeShared<FCharacterProfileEditorModel>();
	Model->InitializeFromAsset(Asset);
	Model->SetSelectedFlipbook(0);
	Model->SetSelectedTransition(TEXT("FromA"), TEXT("SharedTarget"));
	TSharedRef<SProfileDetailsPanel> Panel = SNew(SProfileDetailsPanel)
		.Model(TSharedPtr<FCharacterProfileEditorModel>(Model))
		.PaneMode(EProfileDetailsPaneMode::Transitions);

	int32 Refreshes = 0;
	const FDelegateHandle RefreshHandle = Model->OnAssetDataChanged.AddLambda([&Refreshes]()
	{
		++Refreshes;
	});
	TestTrue(TEXT("Editing the selected transition commits"), Panel->CommitEdgePhaseTag(RecoveryTag));
	TestEqual(TEXT("Only the selected row receives the override"),
		Asset->Flipbooks[0].TransitionData.Transitions[0].PhaseTagOverride, RecoveryTag);
	TestFalse(TEXT("The other incoming transition remains inherited"),
		Asset->Flipbooks[1].TransitionData.Transitions[0].PhaseTagOverride.IsValid());
	TestEqual(TEXT("The shared target animation phase is untouched"),
		Asset->Flipbooks[TargetIndex].EditorMeta.PhaseTag, ActiveTag);
	TestEqual(TEXT("One row edit emits one model refresh"), Refreshes, 1);
	TestFalse(TEXT("Reapplying the same row override is a no-op"),
		Panel->CommitEdgePhaseTag(RecoveryTag));
	TestEqual(TEXT("The no-op emits no additional refresh"), Refreshes, 1);

	const Paper2DPlusAnimationMap::FGraphProjection Projection =
		Paper2DPlusAnimationMap::ProjectGraph(Asset);
	const Paper2DPlusAnimationMap::FProjectedEdge* FirstEdge = Projection.Edges.FindByPredicate(
		[](const Paper2DPlusAnimationMap::FProjectedEdge& Edge)
		{
			return Edge.FromMoveLower == TEXT("froma");
		});
	const Paper2DPlusAnimationMap::FProjectedEdge* SecondEdge = Projection.Edges.FindByPredicate(
		[](const Paper2DPlusAnimationMap::FProjectedEdge& Edge)
		{
			return Edge.FromMoveLower == TEXT("fromb");
		});
	TestTrue(TEXT("Projection preserves different effective phases on converging arrows"),
		FirstEdge && FirstEdge->TransitionPhaseTag == RecoveryTag
			&& SecondEdge && SecondEdge->TransitionPhaseTag == ActiveTag);

	Model->OnAssetDataChanged.Remove(RefreshHandle);
	TestTrue(TEXT("One undo clears only the selected transition override"),
		GEditor->UndoTransaction(true));
	TestFalse(TEXT("Undo restores the selected transition to inherited phase behavior"),
		Asset->Flipbooks[0].TransitionData.Transitions[0].PhaseTagOverride.IsValid());
	TestFalse(TEXT("Undo leaves the sibling incoming transition inherited"),
		Asset->Flipbooks[1].TransitionData.Transitions[0].PhaseTagOverride.IsValid());
	TestEqual(TEXT("Undo never changes the shared target animation phase"),
		Asset->Flipbooks[TargetIndex].EditorMeta.PhaseTag, ActiveTag);
	TestTrue(TEXT("One redo reapplies the selected transition override"),
		GEditor->RedoTransaction());
	TestEqual(TEXT("Redo restores the selected transition phase only"),
		Asset->Flipbooks[0].TransitionData.Transitions[0].PhaseTagOverride, RecoveryTag);
	TestFalse(TEXT("Redo still leaves the sibling incoming transition inherited"),
		Asset->Flipbooks[1].TransitionData.Transitions[0].PhaseTagOverride.IsValid());
	GEditor->ResetTransaction(NSLOCTEXT(
		"Paper2DPlusAnimTagSurfTest", "EndIndependentEdgePhase", "Independent Edge Phase End"));
	return true;
}

// ─── Smooth refresh: transition phase is edge display state, never topology ───────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusAnimMapPhaseOnlyDiffTest,
	"Paper2DPlus.AnimationMap.PhaseOnlyDiffIsFieldRefresh",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusAnimMapPhaseOnlyDiffTest::RunTest(const FString& Parameters)
{
	namespace CG = Paper2DPlusAnimationMap;

	const FGameplayTag ActiveTag = AnimTagSurf_Tag(TEXT("Paper2DPlus.Phase.Active"));
	const FGameplayTag HeavyTag = AnimTagSurf_Tag(TEXT("Paper2DPlus.Animation.Combat.Heavy"));
	if (!TestTrue(TEXT("Native phase and animation tags registered"), ActiveTag.IsValid() && HeavyTag.IsValid()))
	{
		return false;
	}

	CG::FGraphProjection Before;
	CG::FProjectedNode BeforeNode;
	BeforeNode.MoveNameLower = TEXT("hit");
	BeforeNode.DisplayName = TEXT("Hit");
	Before.Nodes.Add(BeforeNode);
	CG::FProjectedEdge BeforeEdge;
	BeforeEdge.FromMoveLower = TEXT("jab");
	BeforeEdge.TargetMoveLower = TEXT("hit");
	Before.Edges.Add(BeforeEdge);

	CG::FGraphProjection After = Before;
	After.Edges[0].TransitionPhaseTag = ActiveTag;

	const CG::FProjectionDiff PhaseDiff = CG::DiffProjection(Before, After);
	TestTrue(TEXT("Phase-only edit preserves edge topology identity"), PhaseDiff.bEdgesEqual);
	TestEqual(TEXT("A transition override does not restamp the target move phase"),
		PhaseDiff.PhaseTagChanges.Num(), 0);
	TestEqual(TEXT("Exactly one existing pill needs a field refresh"), PhaseDiff.EdgePhaseChanges.Num(), 1);
	TestFalse(TEXT("Phase-only display work keeps the diff non-empty"), PhaseDiff.IsEmpty());

	CG::FGraphProjection InheritedTargetAfter = After;
	InheritedTargetAfter.Nodes[0].PhaseTag = ActiveTag;
	const CG::FProjectionDiff TargetPhaseDiff = CG::DiffProjection(After, InheritedTargetAfter);
	TestTrue(TEXT("The animation's own phase still refreshes its surviving move field"),
		TargetPhaseDiff.PhaseTagChanges.Contains(TEXT("hit")));

	CG::FGraphProjection TagsAfter = Before;
	TagsAfter.Nodes[0].OwnAnimationTags.AddTag(HeavyTag);
	const CG::FProjectionDiff TagDiff = CG::DiffProjection(Before, TagsAfter);
	TestTrue(TEXT("Animation-tag edits preserve edge topology"), TagDiff.bEdgesEqual);
	TestTrue(TEXT("Animation-tag edits target the surviving card's chips child"),
		TagDiff.AnimationTagChanges.Contains(TEXT("hit")));
	TestEqual(TEXT("Animation-tag edits do not add move nodes"), TagDiff.MovesToAdd.Num(), 0);
	TestEqual(TEXT("Animation-tag edits do not remove move nodes"), TagDiff.MovesToRemove.Num(), 0);
	TestFalse(TEXT("Animation-tag child refresh keeps the diff non-empty"), TagDiff.IsEmpty());

	After.Edges[0].TargetMoveLower = TEXT("miss");
	const CG::FProjectionDiff TopologyDiff = CG::DiffProjection(Before, After);
	TestFalse(TEXT("Changing the endpoint is still topology"), TopologyDiff.bEdgesEqual);
	TestEqual(TEXT("Topology changes are not mislabeled as phase-only refreshes"),
		TopologyDiff.EdgePhaseChanges.Num(), 0);
	return true;
}

#endif // WITH_EDITOR
