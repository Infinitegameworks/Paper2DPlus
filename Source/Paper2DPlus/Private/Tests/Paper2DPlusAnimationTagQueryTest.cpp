// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#if WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "NativeGameplayTags.h"
#include "Paper2DPlusAnimationTagQuery.h"
#include "Paper2DPlusAnimationTags.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Paper2DPlusMoveTransition.h"
#include "PaperFlipbook.h"

/**
 * Native effective-tag coverage retained after the broad Blueprint query surface was removed.
 * Provenance is derived inside exact groups and their flagged chain starts; it never relies on
 * global structural openers or exposes a raw result-array selection API.
 */
UE_DEFINE_GAMEPLAY_TAG_STATIC(
	AnimTagQuery_ContextAirborneRising,
	"Paper2DPlus.Animation.Context.Airborne.Rising")
UE_DEFINE_GAMEPLAY_TAG_STATIC(
	AnimTagQuery_SharedSinkGroup,
	"Paper2DPlus.Test.AnimationTagQuery.Group.SharedSink")
UE_DEFINE_GAMEPLAY_TAG_STATIC(
	AnimTagQuery_SharedSinkRootOne,
	"Paper2DPlus.Test.AnimationTagQuery.Root.One")
UE_DEFINE_GAMEPLAY_TAG_STATIC(
	AnimTagQuery_SharedSinkRootTwo,
	"Paper2DPlus.Test.AnimationTagQuery.Root.Two")

namespace
{
	int32 AnimTagQuery_AddMove(UPaper2DPlusCharacterProfileAsset* Asset, const FString& MoveName)
	{
		FFlipbookProfileEntry Animation;
		Animation.Identity.FlipbookName = MoveName;
		Animation.Identity.Flipbook = NewObject<UPaperFlipbook>(Asset);
		return Asset->Flipbooks.Add(Animation);
	}

	void AnimTagQuery_Tag(
		UPaper2DPlusCharacterProfileAsset* Asset,
		const FString& MoveName,
		const FGameplayTag& AnimationTag)
	{
		const int32 Index = Asset->Flipbooks.IndexOfByPredicate(
			[&MoveName](const FFlipbookProfileEntry& Entry)
			{
				return Entry.Identity.FlipbookName.Equals(MoveName, ESearchCase::CaseSensitive);
			});
		if (Index != INDEX_NONE)
		{
			Asset->Flipbooks[Index].EditorMeta.AnimationTags.AddTag(AnimationTag);
		}
	}

	void AnimTagQuery_Link(
		UPaper2DPlusCharacterProfileAsset* Asset,
		const FString& FromName,
		const FString& TargetName)
	{
		for (FFlipbookProfileEntry& Animation : Asset->Flipbooks)
		{
			if (Animation.Identity.FlipbookName.Equals(FromName, ESearchCase::IgnoreCase))
			{
				Animation.TransitionData.Transitions.Add(FPaper2DPlusMoveTransition(TargetName));
				return;
			}
		}
	}

	void AnimTagQuery_AddToGroup(
		UPaper2DPlusCharacterProfileAsset* Asset,
		const FGameplayTag& GroupTag,
		const FString& MoveName,
		bool bIsChainStart = false)
	{
		FFlipbookTagMappingEntry Entry(MoveName);
		Entry.bIsChainStart = bIsChainStart;
		Asset->TagMappings.FindOrAdd(GroupTag).Entries.Add(Entry);
	}

	int32 AnimTagQuery_CountWarnings(
		const TArray<FCharacterProfileValidationIssue>& Issues,
		const TCHAR* Marker)
	{
		int32 Count = 0;
		for (const FCharacterProfileValidationIssue& Issue : Issues)
		{
			if (Issue.Severity == ECharacterProfileValidationSeverity::Warning
				&& Issue.Message.Contains(Marker))
			{
				++Count;
			}
		}
		return Count;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusAnimTagQueryProvenanceTest,
	"Paper2DPlus.AnimationTagQuery.GroupRootProvenance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusAnimTagQueryProvenanceTest::RunTest(const FString& Parameters)
{
	const FGameplayTag GroupTag = Paper2DPlusAnimationTags::Combat;
	if (!GroupTag.IsValid())
	{
		AddInfo(TEXT("Animation taxonomy is unavailable; skipping tag provenance coverage."));
		return true;
	}

	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	AnimTagQuery_AddMove(Asset, TEXT("Root"));
	AnimTagQuery_AddMove(Asset, TEXT("Member"));
	AnimTagQuery_AddMove(Asset, TEXT("Outside"));
	AnimTagQuery_Tag(Asset, TEXT("Root"), Paper2DPlusAnimationTags::Combat_Combo);
	AnimTagQuery_Tag(Asset, TEXT("Member"), Paper2DPlusAnimationTags::Combat_Heavy);
	AnimTagQuery_Tag(Asset, TEXT("Outside"), Paper2DPlusAnimationTags::Context_Airborne);
	AnimTagQuery_AddToGroup(Asset, GroupTag, TEXT("Root"), /*bIsChainStart=*/true);
	AnimTagQuery_AddToGroup(Asset, GroupTag, TEXT("Member"));
	AnimTagQuery_Link(Asset, TEXT("Root"), TEXT("Member"));
	AnimTagQuery_Link(Asset, TEXT("Root"), TEXT("Outside")); // not a member: cannot leak

	const TMap<FString, Paper2DPlusAnimationTagQuery::FAnimationTagSet> TagMap =
		Paper2DPlusAnimationTagQuery::BuildAnimationTagMap(Asset);
	const Paper2DPlusAnimationTagQuery::FAnimationTagSet* RootTags = TagMap.Find(TEXT("root"));
	const Paper2DPlusAnimationTagQuery::FAnimationTagSet* MemberTags = TagMap.Find(TEXT("member"));
	const Paper2DPlusAnimationTagQuery::FAnimationTagSet* OutsideTags = TagMap.Find(TEXT("outside"));
	if (!TestNotNull(TEXT("Root projection exists"), RootTags)
		|| !TestNotNull(TEXT("Member projection exists"), MemberTags)
		|| !TestNotNull(TEXT("Outside projection exists"), OutsideTags))
	{
		return false;
	}

	TestTrue(TEXT("Root keeps its own Combo tag"),
		RootTags->OwnTags.HasTagExact(Paper2DPlusAnimationTags::Combat_Combo));
	TestTrue(TEXT("Root gains the exact group key as group-implied provenance"),
		RootTags->GroupImpliedTags.HasTagExact(GroupTag));
	TestTrue(TEXT("A root never chain-inherits from itself"), RootTags->ChainInheritedTags.IsEmpty());

	TestTrue(TEXT("Member keeps its own Heavy tag"),
		MemberTags->OwnTags.HasTagExact(Paper2DPlusAnimationTags::Combat_Heavy));
	TestTrue(TEXT("Member inherits the selected root's own Combo tag"),
		MemberTags->ChainInheritedTags.HasTagExact(Paper2DPlusAnimationTags::Combat_Combo));
	TestTrue(TEXT("Member also carries exact group-implied provenance"),
		MemberTags->GroupImpliedTags.HasTagExact(GroupTag));
	TestTrue(TEXT("Effective tags union own, root-inherited, and group-implied sources"),
		MemberTags->EffectiveTags.HasTagExact(Paper2DPlusAnimationTags::Combat_Heavy)
		&& MemberTags->EffectiveTags.HasTagExact(Paper2DPlusAnimationTags::Combat_Combo)
		&& MemberTags->EffectiveTags.HasTagExact(GroupTag));
	TestEqual(TEXT("Member records the reaching numbered root"),
		MemberTags->ReachingRootNames, TArray<FString>({ TEXT("Root") }));

	TestTrue(TEXT("Outside keeps its own tag"),
		OutsideTags->OwnTags.HasTagExact(Paper2DPlusAnimationTags::Context_Airborne));
	TestTrue(TEXT("Transition to an animation outside the exact group cannot inherit root tags"),
		OutsideTags->ChainInheritedTags.IsEmpty());
	TestTrue(TEXT("Outside has no group-implied tag"), OutsideTags->GroupImpliedTags.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusAnimTagQueryAnimationNameTest,
	"Paper2DPlus.AnimationTagQuery.AnimationNamePreservesFirstCanonicalEntry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusAnimTagQueryAnimationNameTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	AnimTagQuery_AddMove(Asset, TEXT("CanonicalWalk"));
	AnimTagQuery_AddMove(Asset, TEXT("canonicalwalk"));

	const TMap<FString, Paper2DPlusAnimationTagQuery::FAnimationTagSet> TagMap =
		Paper2DPlusAnimationTagQuery::BuildAnimationTagMap(Asset);
	TestEqual(TEXT("Case-insensitive duplicate names still produce one projection"), TagMap.Num(), 1);
	const Paper2DPlusAnimationTagQuery::FAnimationTagSet* WalkTags = TagMap.Find(TEXT("canonicalwalk"));
	if (!TestNotNull(TEXT("Canonical Walk projection exists"), WalkTags))
	{
		return false;
	}

	TestEqual(
		TEXT("The first authored spelling is retained for display"),
		WalkTags->AnimationName,
		FString(TEXT("CanonicalWalk")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusAnimTagQuerySharedSinkTest,
	"Paper2DPlus.AnimationTagQuery.SharedSinkUnionsMultipleRootProvenance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusAnimTagQuerySharedSinkTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	AnimTagQuery_AddMove(Asset, TEXT("RootOne"));
	AnimTagQuery_AddMove(Asset, TEXT("RootTwo"));
	AnimTagQuery_AddMove(Asset, TEXT("SharedSink"));
	AnimTagQuery_Tag(Asset, TEXT("RootOne"), AnimTagQuery_SharedSinkRootOne);
	AnimTagQuery_Tag(Asset, TEXT("RootTwo"), AnimTagQuery_SharedSinkRootTwo);
	AnimTagQuery_AddToGroup(Asset, AnimTagQuery_SharedSinkGroup, TEXT("RootOne"), /*bIsChainStart=*/true);
	AnimTagQuery_AddToGroup(Asset, AnimTagQuery_SharedSinkGroup, TEXT("RootTwo"), /*bIsChainStart=*/true);
	AnimTagQuery_AddToGroup(Asset, AnimTagQuery_SharedSinkGroup, TEXT("SharedSink"));
	AnimTagQuery_Link(Asset, TEXT("RootOne"), TEXT("SharedSink"));
	AnimTagQuery_Link(Asset, TEXT("RootTwo"), TEXT("SharedSink"));

	const TMap<FString, Paper2DPlusAnimationTagQuery::FAnimationTagSet> TagMap =
		Paper2DPlusAnimationTagQuery::BuildAnimationTagMap(Asset);
	const Paper2DPlusAnimationTagQuery::FAnimationTagSet* SharedTags =
		TagMap.Find(TEXT("sharedsink"));
	if (!TestNotNull(TEXT("Shared sink projection exists"), SharedTags))
	{
		return false;
	}

	TestTrue(TEXT("Shared sink inherits Root 1 authored tags"),
		SharedTags->ChainInheritedTags.HasTagExact(AnimTagQuery_SharedSinkRootOne));
	TestTrue(TEXT("Shared sink also inherits Root 2 authored tags"),
		SharedTags->ChainInheritedTags.HasTagExact(AnimTagQuery_SharedSinkRootTwo));
	TestTrue(TEXT("Shared sink retains its exact group provenance"),
		SharedTags->GroupImpliedTags.HasTagExact(AnimTagQuery_SharedSinkGroup));
	TestEqual(TEXT("Shared sink records both reaching roots"),
		SharedTags->ReachingRootNames.Num(), 2);
	TestTrue(TEXT("Shared sink records RootOne"),
		SharedTags->ReachingRootNames.Contains(TEXT("RootOne")));
	TestTrue(TEXT("Shared sink records RootTwo"),
		SharedTags->ReachingRootNames.Contains(TEXT("RootTwo")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusAnimTagQueryRootBoundaryTest,
	"Paper2DPlus.AnimationTagQuery.ChainStartsAreGroupScopedAndCycleSafe",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusAnimTagQueryRootBoundaryTest::RunTest(const FString& Parameters)
{
	const FGameplayTag CombatGroup = Paper2DPlusAnimationTags::Combat;
	const FGameplayTag ContextGroup = Paper2DPlusAnimationTags::Context;
	if (!CombatGroup.IsValid() || !ContextGroup.IsValid())
	{
		AddInfo(TEXT("Animation taxonomy is unavailable; skipping root-boundary coverage."));
		return true;
	}

	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	for (const TCHAR* Name :
		{ TEXT("CombatRoot"), TEXT("CombatMid"), TEXT("BoundaryRoot"), TEXT("BoundaryTail"),
			TEXT("ContextRoot"), TEXT("ContextMid") })
	{
		AnimTagQuery_AddMove(Asset, Name);
	}
	AnimTagQuery_Tag(Asset, TEXT("CombatRoot"), Paper2DPlusAnimationTags::Combat_Heavy);
	AnimTagQuery_Tag(Asset, TEXT("BoundaryRoot"), Paper2DPlusAnimationTags::Context_Airborne);
	AnimTagQuery_Tag(Asset, TEXT("ContextRoot"), Paper2DPlusAnimationTags::Combat_Light);

	AnimTagQuery_AddToGroup(Asset, CombatGroup, TEXT("CombatRoot"), /*bIsChainStart=*/true);
	AnimTagQuery_AddToGroup(Asset, CombatGroup, TEXT("CombatMid"));
	AnimTagQuery_AddToGroup(Asset, CombatGroup, TEXT("BoundaryRoot"), /*bIsChainStart=*/true);
	AnimTagQuery_AddToGroup(Asset, CombatGroup, TEXT("BoundaryTail"));
	AnimTagQuery_AddToGroup(Asset, ContextGroup, TEXT("ContextRoot"), /*bIsChainStart=*/true); // chain start in another group
	AnimTagQuery_AddToGroup(Asset, ContextGroup, TEXT("ContextMid"));

	AnimTagQuery_Link(Asset, TEXT("CombatRoot"), TEXT("CombatMid"));
	AnimTagQuery_Link(Asset, TEXT("CombatMid"), TEXT("CombatRoot")); // cycle
	AnimTagQuery_Link(Asset, TEXT("CombatMid"), TEXT("BoundaryRoot")); // hard root boundary
	AnimTagQuery_Link(Asset, TEXT("BoundaryRoot"), TEXT("BoundaryTail"));
	AnimTagQuery_Link(Asset, TEXT("ContextRoot"), TEXT("ContextMid"));

	const TMap<FString, Paper2DPlusAnimationTagQuery::FAnimationTagSet> TagMap =
		Paper2DPlusAnimationTagQuery::BuildAnimationTagMap(Asset);
	const auto* CombatMid = TagMap.Find(TEXT("combatmid"));
	const auto* BoundaryRoot = TagMap.Find(TEXT("boundaryroot"));
	const auto* BoundaryTail = TagMap.Find(TEXT("boundarytail"));
	const auto* ContextMid = TagMap.Find(TEXT("contextmid"));
	if (!TestNotNull(TEXT("CombatMid projection exists"), CombatMid)
		|| !TestNotNull(TEXT("BoundaryRoot projection exists"), BoundaryRoot)
		|| !TestNotNull(TEXT("BoundaryTail projection exists"), BoundaryTail)
		|| !TestNotNull(TEXT("ContextMid projection exists"), ContextMid))
	{
		return false;
	}

	TestTrue(TEXT("Cycle-safe traversal still applies the first chain start's Heavy provenance to CombatMid"),
		CombatMid->ChainInheritedTags.HasTagExact(Paper2DPlusAnimationTags::Combat_Heavy));
	TestFalse(TEXT("One chain start cannot cross into another flagged chain start"),
		BoundaryRoot->ChainInheritedTags.HasTagExact(Paper2DPlusAnimationTags::Combat_Heavy));
	TestTrue(TEXT("The second chain start applies its own Airborne provenance to its tail"),
		BoundaryTail->ChainInheritedTags.HasTagExact(Paper2DPlusAnimationTags::Context_Airborne));
	TestTrue(TEXT("A chain start in another group independently applies Light provenance"),
		ContextMid->ChainInheritedTags.HasTagExact(Paper2DPlusAnimationTags::Combat_Light));
	TestFalse(TEXT("Chain starts across groups never leak Heavy provenance"),
		ContextMid->ChainInheritedTags.HasTagExact(Paper2DPlusAnimationTags::Combat_Heavy));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusAnimTagQueryValidatorTest,
	"Paper2DPlus.AnimationTagQuery.ValidatorUsesChainStartProvenance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusAnimTagQueryValidatorTest::RunTest(const FString& Parameters)
{
	const FGameplayTag GroupTag = Paper2DPlusAnimationTags::Combat;
	if (!GroupTag.IsValid())
	{
		AddInfo(TEXT("Animation taxonomy is unavailable; skipping validator provenance coverage."));
		return true;
	}

	// Two explicit sibling Context tags on one animation remain an authoring warning.
	{
		UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
		AnimTagQuery_AddMove(Asset, TEXT("TwoContexts"));
		AnimTagQuery_Tag(Asset, TEXT("TwoContexts"), Paper2DPlusAnimationTags::Context_Airborne);
		AnimTagQuery_Tag(Asset, TEXT("TwoContexts"), Paper2DPlusAnimationTags::Context_Crouching);
		TArray<FCharacterProfileValidationIssue> Issues;
		Asset->ValidateCharacterProfileAsset(Issues);
		TestEqual(TEXT("Two explicit Context siblings produce one exclusivity warning"),
			AnimTagQuery_CountWarnings(Issues, TEXT("Context is exclusive")), 1);
	}

	// A member that conflicts with its exact flagged chain start warns; the incoming edge alone is not enough.
	{
		UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
		AnimTagQuery_AddMove(Asset, TEXT("Root"));
		AnimTagQuery_AddMove(Asset, TEXT("Member"));
		AnimTagQuery_Tag(Asset, TEXT("Root"), Paper2DPlusAnimationTags::Context_Airborne);
		AnimTagQuery_Tag(Asset, TEXT("Member"), Paper2DPlusAnimationTags::Context_Crouching);
		AnimTagQuery_AddToGroup(Asset, GroupTag, TEXT("Root"), /*bIsChainStart=*/true);
		AnimTagQuery_AddToGroup(Asset, GroupTag, TEXT("Member"));
		AnimTagQuery_Link(Asset, TEXT("Root"), TEXT("Member"));
		TArray<FCharacterProfileValidationIssue> Issues;
		Asset->ValidateCharacterProfileAsset(Issues);
		TestEqual(TEXT("Chain-start/member Context conflict produces one warning"),
			AnimTagQuery_CountWarnings(Issues, TEXT("conflicts with reaching chain root")), 1);
	}

	// Parent/child specialization is compatible and must not be reported as two dimensions.
	{
		UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
		AnimTagQuery_AddMove(Asset, TEXT("Specialized"));
		AnimTagQuery_Tag(Asset, TEXT("Specialized"), Paper2DPlusAnimationTags::Context_Airborne);
		AnimTagQuery_Tag(Asset, TEXT("Specialized"), AnimTagQuery_ContextAirborneRising);
		TArray<FCharacterProfileValidationIssue> Issues;
		Asset->ValidateCharacterProfileAsset(Issues);
		TestEqual(TEXT("Parent/child Context specialization is not an exclusivity warning"),
			AnimTagQuery_CountWarnings(Issues, TEXT("Context is exclusive")), 0);
	}
	return true;
}

#endif // WITH_EDITOR
