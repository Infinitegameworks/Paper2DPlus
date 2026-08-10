// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#if WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "NativeGameplayTags.h"
#include "Paper2DPlusAnimationMapLibrary.h"
#include "Paper2DPlusAnimationTagQuery.h"
#include "Paper2DPlusBlueprintLibrary.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Paper2DPlusComboChain.h"
#include "Paper2DPlusMoveTransition.h"
#include "Paper2DPlusPaperZDLibrary.h"
#include "PaperFlipbook.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/UnrealType.h"

UE_DEFINE_GAMEPLAY_TAG_STATIC(
	AnimationResolver_GroupParent,
	"Paper2DPlus.Test.AnimationMapResolver.Group")
UE_DEFINE_GAMEPLAY_TAG_STATIC(
	AnimationResolver_GroupAttack,
	"Paper2DPlus.Test.AnimationMapResolver.Group.Attack")
UE_DEFINE_GAMEPLAY_TAG_STATIC(
	AnimationResolver_GroupSpecial,
	"Paper2DPlus.Test.AnimationMapResolver.Group.Special")
UE_DEFINE_GAMEPLAY_TAG_STATIC(
	AnimationResolver_CriteriaRoot,
	"Paper2DPlus.Test.AnimationMapResolver.Criteria.Root")
UE_DEFINE_GAMEPLAY_TAG_STATIC(
	AnimationResolver_CriteriaSpecial,
	"Paper2DPlus.Test.AnimationMapResolver.Criteria.Special")
UE_DEFINE_GAMEPLAY_TAG_STATIC(
	AnimationResolver_PhaseRecovery,
	"Paper2DPlus.Test.AnimationMapResolver.Phase.Recovery")
UE_DEFINE_GAMEPLAY_TAG_STATIC(
	AnimationResolver_PhaseActive,
	"Paper2DPlus.Test.AnimationMapResolver.Phase.Active")
UE_DEFINE_GAMEPLAY_TAG_STATIC(
	AnimationResolver_ExactLocomotion,
	"Paper2DPlus.Test.AnimationMapResolver.Exact.Locomotion")
UE_DEFINE_GAMEPLAY_TAG_STATIC(
	AnimationResolver_ExactIdle,
	"Paper2DPlus.Test.AnimationMapResolver.Exact.Idle")
UE_DEFINE_GAMEPLAY_TAG_STATIC(
	AnimationResolver_ExactWalk,
	"Paper2DPlus.Test.AnimationMapResolver.Exact.Walk")
UE_DEFINE_GAMEPLAY_TAG_STATIC(
	AnimationResolver_ExactIdleSpecialized,
	"Paper2DPlus.Test.AnimationMapResolver.Exact.Idle.Specialized")

namespace
{
	UPaperFlipbook* AnimationResolver_AddMove(
		UPaper2DPlusCharacterProfileAsset* Profile,
		const FString& Name,
		const FGameplayTag& Phase = FGameplayTag(),
		const FGameplayTagContainer& AnimationTags = FGameplayTagContainer())
	{
		FFlipbookProfileEntry Entry;
		Entry.Identity.FlipbookName = Name;
		Entry.Identity.Flipbook = NewObject<UPaperFlipbook>(Profile);
		Entry.EditorMeta.PhaseTag = Phase;
		Entry.EditorMeta.AnimationTags = AnimationTags;
		Profile->Flipbooks.Add(Entry);
		return Entry.Identity.Flipbook.Get();
	}

	void AnimationResolver_AddColdSoftMove(
		UPaper2DPlusCharacterProfileAsset* Profile,
		const FString& Name,
		const FGameplayTag& Phase = FGameplayTag())
	{
		FFlipbookProfileEntry Entry;
		Entry.Identity.FlipbookName = Name;
		Entry.Identity.Flipbook = TSoftObjectPtr<UPaperFlipbook>(FSoftObjectPath(
			FString::Printf(TEXT("/Temp/TASK125_%s.TASK125_%s"), *Name, *Name)));
		Entry.EditorMeta.PhaseTag = Phase;
		Profile->Flipbooks.Add(MoveTemp(Entry));
	}

	void AnimationResolver_AddWrongClassSoftMove(
		UPaper2DPlusCharacterProfileAsset* Profile,
		const FString& Name,
		const FGameplayTag& Phase)
	{
		UPaper2DPlusCharacterProfileAsset* WrongClassObject = NewObject<UPaper2DPlusCharacterProfileAsset>(
			Profile,
			*FString::Printf(TEXT("%s_WrongClass"), *Name));
		FFlipbookProfileEntry Entry;
		Entry.Identity.FlipbookName = Name;
		Entry.Identity.Flipbook = TSoftObjectPtr<UPaperFlipbook>(
			FSoftObjectPath(WrongClassObject->GetPathName()));
		Entry.EditorMeta.PhaseTag = Phase;
		Profile->Flipbooks.Add(MoveTemp(Entry));
	}

	void AnimationResolver_AddGroupEntry(
		UPaper2DPlusCharacterProfileAsset* Profile,
		const FGameplayTag& GroupTag,
		const FString& Name,
		bool bIsChainStart = false)
	{
		FFlipbookTagMappingEntry Entry(Name);
		Entry.bIsChainStart = bIsChainStart;
		Profile->TagMappings.FindOrAdd(GroupTag).Entries.Add(Entry);
	}

	void AnimationResolver_AddTransition(
		UPaper2DPlusCharacterProfileAsset* Profile,
		const FString& From,
		const FString& To)
	{
		for (FFlipbookProfileEntry& Entry : Profile->Flipbooks)
		{
			if (Entry.Identity.FlipbookName.Equals(From, ESearchCase::IgnoreCase))
			{
				Entry.TransitionData.Transitions.Add(FPaper2DPlusMoveTransition(To));
				return;
			}
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusAnimationResolverDiscoveryTest,
	"Paper2DPlus.AnimationMapResolver.ComboOpenerDiscovery",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusAnimationResolverDiscoveryTest::RunTest(const FString& Parameters)
{
	const FGameplayTag AttackGroup = AnimationResolver_GroupAttack;
	const FGameplayTag SpecialGroup = AnimationResolver_GroupSpecial;
	const FGameplayTag ParentGroup = AnimationResolver_GroupParent;

	UPaper2DPlusCharacterProfileAsset* Profile = NewObject<UPaper2DPlusCharacterProfileAsset>();
	UPaperFlipbook* Attack = AnimationResolver_AddMove(Profile, TEXT("Attack"));
	UPaperFlipbook* AttackThird = AnimationResolver_AddMove(Profile, TEXT("AttackThird"));
	UPaperFlipbook* Special = AnimationResolver_AddMove(Profile, TEXT("Special"));
	AnimationResolver_AddGroupEntry(Profile, SpecialGroup, TEXT("Special"), /*bIsChainStart=*/true);
	AnimationResolver_AddGroupEntry(Profile, AttackGroup, TEXT("Attack"), /*bIsChainStart=*/true);
	AnimationResolver_AddGroupEntry(Profile, AttackGroup, TEXT("AttackThird"), /*bIsChainStart=*/true);

	TArray<UPaperFlipbook*> Openers = UPaper2DPlusAnimationMapLibrary::GetComboOpenerFlipbooks(Profile);
	TestEqual(TEXT("Every flagged chain start's opener is discovered across groups"), Openers.Num(), 3);
	TestTrue(TEXT("Openers are group-tag-sorted, then authored entry order"),
		Openers.Num() == 3
		&& Openers[0] == Attack && Openers[1] == AttackThird && Openers[2] == Special);

	// Ordinary (unflagged) members never synthesize an opener.
	AnimationResolver_AddMove(Profile, TEXT("AttackLate"));
	AnimationResolver_AddGroupEntry(Profile, AttackGroup, TEXT("AttackLate"));
	TestEqual(TEXT("Adding an ordinary group member never synthesizes another opener"),
		UPaper2DPlusAnimationMapLibrary::GetComboOpenerFlipbooks(Profile).Num(), 3);

	// A flagged entry whose name resolves no live flipbook (dangling) is excluded, not guessed.
	AnimationResolver_AddGroupEntry(Profile, AttackGroup, TEXT("GhostOpener"), /*bIsChainStart=*/true);
	TestEqual(TEXT("A dangling flagged entry is excluded from discovery"),
		UPaper2DPlusAnimationMapLibrary::GetComboOpenerFlipbooks(Profile).Num(), 3);

	// An unloadable flagged opener (cold soft reference) is skipped, never a null slot.
	AnimationResolver_AddColdSoftMove(Profile, TEXT("ColdOpener"));
	AnimationResolver_AddGroupEntry(Profile, AttackGroup, TEXT("ColdOpener"), /*bIsChainStart=*/true);
	Openers = UPaper2DPlusAnimationMapLibrary::GetComboOpenerFlipbooks(Profile);
	TestEqual(TEXT("An unloadable flagged opener is skipped"), Openers.Num(), 3);
	TestFalse(TEXT("Discovery never emits a null opener"), Openers.Contains(nullptr));

	// The same opener flipbook flagged in a second group dedupes (AddUnique) instead of repeating.
	AnimationResolver_AddGroupEntry(Profile, ParentGroup, TEXT("Attack"), /*bIsChainStart=*/true);
	Openers = UPaper2DPlusAnimationMapLibrary::GetComboOpenerFlipbooks(Profile);
	TestEqual(TEXT("A flipbook flagged in two groups is discovered once"), Openers.Num(), 3);
	TestTrue(TEXT("The deduped opener keeps its first (group-sorted) discovery position"),
		Openers.Num() == 3 && Openers[0] == Attack);

	// Flagged-but-unresolvable starts also fail the native exact-root analysis (fail closed).
	Paper2DPlusComboChain::FScopedAnimationGroupAnalysis InvalidRootAnalysis;
	TestFalse(TEXT("Native exact-root analysis rejects an unflagged member as a chain start"),
		Paper2DPlusComboChain::AnalyzeAnimationRoot(
			Profile, AttackGroup, TEXT("AttackLate"), InvalidRootAnalysis));
	TestEqual(TEXT("Invalid root analysis never returns an unrelated valid root"),
		InvalidRootAnalysis.Roots.Num(), 0);

	TestEqual(TEXT("A null profile returns an empty opener list"),
		UPaper2DPlusAnimationMapLibrary::GetComboOpenerFlipbooks(nullptr).Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusAnimationResolverExactTagsTest,
	"Paper2DPlus.AnimationMapResolver.FindAnimationByExactTags",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusAnimationResolverExactTagsTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Profile = NewObject<UPaper2DPlusCharacterProfileAsset>();
	FGameplayTagContainer IdleTags;
	IdleTags.AddTag(AnimationResolver_ExactLocomotion);
	IdleTags.AddTag(AnimationResolver_ExactIdle);
	FGameplayTagContainer WalkTags;
	WalkTags.AddTag(AnimationResolver_ExactLocomotion);
	WalkTags.AddTag(AnimationResolver_ExactWalk);

	UPaperFlipbook* Idle = AnimationResolver_AddMove(Profile, TEXT("Idle"), FGameplayTag(), IdleTags);
	AnimationResolver_AddMove(Profile, TEXT("Walk"), FGameplayTag(), WalkTags);
	AnimationResolver_AddGroupEntry(Profile, AnimationResolver_GroupAttack, TEXT("Idle"));

	UPaperFlipbook* Resolved = NewObject<UPaperFlipbook>(Profile);
	FGameplayTagContainer ReversedIdleTags;
	ReversedIdleTags.AddTag(AnimationResolver_ExactIdle);
	ReversedIdleTags.AddTag(AnimationResolver_ExactLocomotion);
	TestEqual(TEXT("Exact tag order does not affect lookup"),
		UPaper2DPlusAnimationMapLibrary::FindAnimationByExactTags(
			Profile, ReversedIdleTags, Resolved),
		EPaper2DPlusAnimationTagFindResult::Success);
	TestTrue(TEXT("An unrooted grouped Idle resolves directly"), Resolved == Idle);

	FGameplayTagContainer Subset;
	Subset.AddTag(AnimationResolver_ExactLocomotion);
	TestEqual(TEXT("A tag subset is not an exact-container match"),
		UPaper2DPlusAnimationMapLibrary::FindAnimationByExactTags(Profile, Subset, Resolved),
		EPaper2DPlusAnimationTagFindResult::NoMatch);
	TestNull(TEXT("No Match clears a seeded output"), Resolved);

	FGameplayTagContainer Specialized;
	Specialized.AddTag(AnimationResolver_ExactLocomotion);
	Specialized.AddTag(AnimationResolver_ExactIdleSpecialized);
	Resolved = Idle;
	TestEqual(TEXT("Hierarchical child matching is not used for exact lookup"),
		UPaper2DPlusAnimationMapLibrary::FindAnimationByExactTags(Profile, Specialized, Resolved),
		EPaper2DPlusAnimationTagFindResult::NoMatch);
	TestNull(TEXT("Hierarchical No Match clears output"), Resolved);

	Resolved = Idle;
	TestEqual(TEXT("An empty tag container is an invalid request"),
		UPaper2DPlusAnimationMapLibrary::FindAnimationByExactTags(
			Profile, FGameplayTagContainer(), Resolved),
		EPaper2DPlusAnimationTagFindResult::InvalidRequest);
	TestNull(TEXT("Invalid Request clears output"), Resolved);
	Resolved = Idle;
	TestEqual(TEXT("A null profile is an invalid request"),
		UPaper2DPlusAnimationMapLibrary::FindAnimationByExactTags(
			nullptr, IdleTags, Resolved),
		EPaper2DPlusAnimationTagFindResult::InvalidRequest);
	TestNull(TEXT("Null-profile request clears output"), Resolved);

	AnimationResolver_AddMove(Profile, TEXT("IdleAlternate"), FGameplayTag(), IdleTags);
	Resolved = Idle;
	TestEqual(TEXT("Duplicate exact containers fail explicitly"),
		UPaper2DPlusAnimationMapLibrary::FindAnimationByExactTags(Profile, IdleTags, Resolved),
		EPaper2DPlusAnimationTagFindResult::Ambiguous);
	TestNull(TEXT("Ambiguous lookup clears output"), Resolved);

	UPaper2DPlusCharacterProfileAsset* ChainProfile = NewObject<UPaper2DPlusCharacterProfileAsset>();
	UPaperFlipbook* ChainRoot = AnimationResolver_AddMove(
		ChainProfile, TEXT("ChainRoot"), FGameplayTag(), WalkTags);
	UPaperFlipbook* ChainIdle = AnimationResolver_AddMove(
		ChainProfile, TEXT("ChainIdle"), FGameplayTag(), IdleTags);
	AnimationResolver_AddGroupEntry(
		ChainProfile, AnimationResolver_GroupAttack, TEXT("ChainRoot"), /*bIsChainStart=*/true);
	AnimationResolver_AddGroupEntry(
		ChainProfile, AnimationResolver_GroupAttack, TEXT("ChainIdle"));
	AnimationResolver_AddTransition(ChainProfile, TEXT("ChainRoot"), TEXT("ChainIdle"));
	TestEqual(TEXT("A uniquely tagged chain member remains available through the primary lookup"),
		UPaper2DPlusAnimationMapLibrary::FindAnimationByExactTags(
			ChainProfile, IdleTags, Resolved),
		EPaper2DPlusAnimationTagFindResult::Success);
	TestTrue(TEXT("Exact tags resolve the chain member itself"), Resolved == ChainIdle);

	ChainProfile->Flipbooks[0].EditorMeta.AnimationTags = IdleTags;
	const TArray<const FFlipbookProfileEntry*> SameChainMatches =
		Paper2DPlusAnimationTagQuery::FindExactOwnTagMatches(ChainProfile, IdleTags);
	TestEqual(TEXT("Both exact duplicates are discovered"), SameChainMatches.Num(), 2);
	TestTrue(TEXT("Duplicates wholly inside one rooted chain are a legal validation exception"),
		Paper2DPlusAnimationTagQuery::AreMatchesContainedByOneRootChain(
			ChainProfile, SameChainMatches));
	Resolved = ChainRoot;
	TestEqual(TEXT("The legal chain exception remains ambiguous for tag-only playback"),
		UPaper2DPlusAnimationMapLibrary::FindAnimationByExactTags(
			ChainProfile, IdleTags, Resolved),
		EPaper2DPlusAnimationTagFindResult::Ambiguous);
	TestNull(TEXT("Chain ambiguity also clears output"), Resolved);

	AnimationResolver_AddMove(ChainProfile, TEXT("UnrelatedIdle"), FGameplayTag(), IdleTags);
	const TArray<const FFlipbookProfileEntry*> UnrelatedMatches =
		Paper2DPlusAnimationTagQuery::FindExactOwnTagMatches(ChainProfile, IdleTags);
	TestFalse(TEXT("An unrelated third duplicate invalidates the chain exception"),
		Paper2DPlusAnimationTagQuery::AreMatchesContainedByOneRootChain(
			ChainProfile, UnrelatedMatches));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusAnimationResolverTransitionTest,
	"Paper2DPlus.AnimationMapResolver.ResolvesOneScopedTransitionByPhaseAndCriteria",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusAnimationResolverTransitionTest::RunTest(const FString& Parameters)
{
	const FGameplayTag AttackGroup = AnimationResolver_GroupAttack;
	const FGameplayTag RootTag = AnimationResolver_CriteriaRoot;
	const FGameplayTag SpecialTag = AnimationResolver_CriteriaSpecial;
	const FGameplayTag Recovery = AnimationResolver_PhaseRecovery;

	UPaper2DPlusCharacterProfileAsset* Profile = NewObject<UPaper2DPlusCharacterProfileAsset>();
	FGameplayTagContainer RootTags;
	RootTags.AddTag(RootTag);
	UPaperFlipbook* Jab = AnimationResolver_AddMove(Profile, TEXT("Jab"), FGameplayTag(), RootTags);
	UPaperFlipbook* Recover = AnimationResolver_AddMove(Profile, TEXT("Recover"), Recovery);
	FGameplayTagContainer SpecialTags;
	SpecialTags.AddTag(SpecialTag);
	UPaperFlipbook* SpecialRecover = AnimationResolver_AddMove(Profile, TEXT("SpecialRecover"), Recovery, SpecialTags);
	UPaperFlipbook* Outside = AnimationResolver_AddMove(Profile, TEXT("Outside"), Recovery);

	AnimationResolver_AddGroupEntry(Profile, AttackGroup, TEXT("Jab"), /*bIsChainStart=*/true);
	AnimationResolver_AddGroupEntry(Profile, AttackGroup, TEXT("Recover"));
	AnimationResolver_AddGroupEntry(Profile, AttackGroup, TEXT("SpecialRecover"));
	AnimationResolver_AddTransition(Profile, TEXT("Jab"), TEXT("Recover"));
	AnimationResolver_AddTransition(Profile, TEXT("Jab"), TEXT("SpecialRecover"));
	AnimationResolver_AddTransition(Profile, TEXT("Jab"), TEXT("Missing"));
	AnimationResolver_AddTransition(Profile, TEXT("Jab"), TEXT("Outside"));
	AnimationResolver_AddTransition(Profile, TEXT("Jab"), FString());

	FPaper2DPlusAnimationSelectionCriteria EmptyCriteria;
	FPaper2DPlusAnimationTransitionInfo Info;
	TestTrue(TEXT("Transition info resolves from the flipbook alone"),
		UPaper2DPlusAnimationMapLibrary::GetAnimationTransitionInfo(Profile, Jab, EmptyCriteria, Info));
	TestTrue(TEXT("Valid direct targets count as transitions"), Info.bHasTransitions);
	TestTrue(TEXT("At least one valid target has a phase"), Info.bHasPhases);
	TestEqual(TEXT("Duplicate target phases collapse to one authored phase choice"), Info.AvailablePhases.Num(), 1);
	if (Info.AvailablePhases.Num() == 1)
	{
		TestEqual(TEXT("Recovery is the available phase"), Info.AvailablePhases[0], Recovery);
	}

	UPaperFlipbook* Resolved = NewObject<UPaperFlipbook>(Profile);
	TestEqual(TEXT("Three matching Recovery targets report ambiguity"),
		UPaper2DPlusAnimationMapLibrary::ResolveAnimationTransition(
			Profile, Jab, Recovery, EmptyCriteria, Resolved),
		EPaper2DPlusAnimationResolveResult::Ambiguous);
	TestNull(TEXT("Ambiguous resolution always clears the output"), Resolved);

	FPaper2DPlusAnimationSelectionCriteria RequireSpecial;
	RequireSpecial.RequiredAllTags.AddTag(AttackGroup);
	RequireSpecial.RequiredAllTags.AddTag(RootTag);
	RequireSpecial.RequiredAllTags.AddTag(SpecialTag);
	TestEqual(TEXT("Effective group + chain + own tags disambiguate the exact transition"),
		UPaper2DPlusAnimationMapLibrary::ResolveAnimationTransition(
			Profile, Jab, Recovery, RequireSpecial, Resolved),
		EPaper2DPlusAnimationResolveResult::Success);
	TestTrue(TEXT("Group + root + target criteria resolve SpecialRecover"), Resolved == SpecialRecover);

	FPaper2DPlusAnimationSelectionCriteria AnySpecial;
	AnySpecial.RequiredAnyTags.AddTag(SpecialTag);
	TestEqual(TEXT("Required-any criteria can select a dynamic branch"),
		UPaper2DPlusAnimationMapLibrary::ResolveAnimationTransition(
			Profile, Jab, Recovery, AnySpecial, Resolved),
		EPaper2DPlusAnimationResolveResult::Success);
	TestTrue(TEXT("Required-any Special resolves SpecialRecover"), Resolved == SpecialRecover);

	FPaper2DPlusAnimationSelectionCriteria GroupedNonSpecial;
	GroupedNonSpecial.RequiredAllTags.AddTag(AttackGroup);
	GroupedNonSpecial.ExcludedTags.AddTag(SpecialTag);
	TestEqual(TEXT("The group tag scopes criteria without a Group input"),
		UPaper2DPlusAnimationMapLibrary::ResolveAnimationTransition(
			Profile, Jab, Recovery, GroupedNonSpecial, Resolved),
		EPaper2DPlusAnimationResolveResult::Success);
	TestTrue(TEXT("Excluding Special within the group resolves Recover"), Resolved == Recover);

	FPaper2DPlusAnimationSelectionCriteria Ungrouped;
	Ungrouped.ExcludedTags.AddTag(AttackGroup);
	TestEqual(TEXT("An ungrouped direct target is in view (no root scoping)"),
		UPaper2DPlusAnimationMapLibrary::ResolveAnimationTransition(
			Profile, Jab, Recovery, Ungrouped, Resolved),
		EPaper2DPlusAnimationResolveResult::Success);
	TestTrue(TEXT("Excluding the group resolves the ungrouped Outside"), Resolved == Outside);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusAnimationResolverFailureContractTest,
	"Paper2DPlus.AnimationMapResolver.FailureResultsAreExplicitAndClearOutputs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusAnimationResolverFailureContractTest::RunTest(const FString& Parameters)
{
	const FGameplayTag AttackGroup = AnimationResolver_GroupAttack;
	const FGameplayTag Active = AnimationResolver_PhaseActive;
	const FGameplayTag Recovery = AnimationResolver_PhaseRecovery;

	UPaper2DPlusCharacterProfileAsset* Profile = NewObject<UPaper2DPlusCharacterProfileAsset>();
	UPaperFlipbook* RootFlipbook = AnimationResolver_AddMove(Profile, TEXT("Root"));
	UPaperFlipbook* Middle = AnimationResolver_AddMove(Profile, TEXT("Middle"), Active);
	UPaperFlipbook* Tail = AnimationResolver_AddMove(Profile, TEXT("Tail"), Recovery);
	UPaperFlipbook* OtherRoot = AnimationResolver_AddMove(Profile, TEXT("OtherRoot"), Recovery);
	AnimationResolver_AddGroupEntry(Profile, AttackGroup, TEXT("Root"), /*bIsChainStart=*/true);
	AnimationResolver_AddGroupEntry(Profile, AttackGroup, TEXT("Middle"));
	AnimationResolver_AddGroupEntry(Profile, AttackGroup, TEXT("Tail"));
	AnimationResolver_AddGroupEntry(Profile, AttackGroup, TEXT("OtherRoot"), /*bIsChainStart=*/true);
	AnimationResolver_AddTransition(Profile, TEXT("Root"), TEXT("Middle"));
	AnimationResolver_AddTransition(Profile, TEXT("Middle"), TEXT("Tail"));

	auto ExpectFailureAndCleared = [this, Tail](
		const TCHAR* Label,
		UPaper2DPlusCharacterProfileAsset* InProfile,
		UPaperFlipbook* Original,
		FGameplayTag Phase,
		const FPaper2DPlusAnimationSelectionCriteria& Criteria,
		EPaper2DPlusAnimationResolveResult Expected)
	{
		UPaperFlipbook* OutFlipbook = Tail;
		TestEqual(Label,
			UPaper2DPlusAnimationMapLibrary::ResolveAnimationTransition(
				InProfile, Original, Phase, Criteria, OutFlipbook),
			Expected);
		TestNull(*FString::Printf(TEXT("%s clears OutFlipbook"), Label), OutFlipbook);
	};

	const FPaper2DPlusAnimationSelectionCriteria EmptyCriteria;
	ExpectFailureAndCleared(
		TEXT("Null profile reports InvalidRequest"),
		nullptr, RootFlipbook, Active, EmptyCriteria,
		EPaper2DPlusAnimationResolveResult::InvalidRequest);
	ExpectFailureAndCleared(
		TEXT("Null original reports InvalidRequest"),
		Profile, nullptr, Active, EmptyCriteria,
		EPaper2DPlusAnimationResolveResult::InvalidRequest);
	ExpectFailureAndCleared(
		TEXT("Invalid phase reports InvalidRequest"),
		Profile, RootFlipbook, FGameplayTag(), EmptyCriteria,
		EPaper2DPlusAnimationResolveResult::InvalidRequest);

	UPaperFlipbook* Foreign = NewObject<UPaperFlipbook>(Profile);
	ExpectFailureAndCleared(
		TEXT("A flipbook with no map entry reports FlipbookNotInMap"),
		Profile, Foreign, Active, EmptyCriteria,
		EPaper2DPlusAnimationResolveResult::FlipbookNotInMap);
	ExpectFailureAndCleared(
		TEXT("A transitive phase target is not a direct transition"),
		Profile, RootFlipbook, Recovery, EmptyCriteria,
		EPaper2DPlusAnimationResolveResult::NoMatch);
	ExpectFailureAndCleared(
		TEXT("Another chain start resolves as an ordinary key; no outgoing rows is NoMatch"),
		Profile, OtherRoot, Recovery, EmptyCriteria,
		EPaper2DPlusAnimationResolveResult::NoMatch);

	FPaper2DPlusAnimationSelectionCriteria ImpossibleCriteria;
	ImpossibleCriteria.RequiredAllTags.AddTag(AnimationResolver_CriteriaSpecial);
	ExpectFailureAndCleared(
		TEXT("Unmatched criteria report NoMatch"),
		Profile, RootFlipbook, Active, ImpossibleCriteria,
		EPaper2DPlusAnimationResolveResult::NoMatch);

	FPaper2DPlusAnimationTransitionInfo SeededInfo;
	SeededInfo.bHasTransitions = true;
	SeededInfo.bHasPhases = true;
	SeededInfo.AvailablePhases.Add(Recovery);
	TestFalse(TEXT("A flipbook with no map entry rejects transition metadata"),
		UPaper2DPlusAnimationMapLibrary::GetAnimationTransitionInfo(
			Profile, Foreign, EmptyCriteria, SeededInfo));
	TestFalse(TEXT("Failed metadata lookup clears bHasTransitions"), SeededInfo.bHasTransitions);
	TestFalse(TEXT("Failed metadata lookup clears bHasPhases"), SeededInfo.bHasPhases);
	TestEqual(TEXT("Failed metadata lookup clears phases"), SeededInfo.AvailablePhases.Num(), 0);

	// A flipbook referenced by two entries has no unambiguous map identity — fails closed.
	FFlipbookProfileEntry DuplicateEntry;
	DuplicateEntry.Identity.FlipbookName = TEXT("MiddleClone");
	DuplicateEntry.Identity.Flipbook = Middle;
	Profile->Flipbooks.Add(DuplicateEntry);
	ExpectFailureAndCleared(
		TEXT("A flipbook shared by multiple entries reports AmbiguousFlipbook"),
		Profile, Middle, Recovery, EmptyCriteria,
		EPaper2DPlusAnimationResolveResult::AmbiguousFlipbook);
	TestFalse(TEXT("A shared flipbook also rejects transition metadata"),
		UPaper2DPlusAnimationMapLibrary::GetAnimationTransitionInfo(
			Profile, Middle, EmptyCriteria, SeededInfo));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusAnimationResolverSoftReferenceTest,
	"Paper2DPlus.AnimationMapResolver.SoftReferencesKeepTopologyResidencyIndependent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusAnimationResolverSoftReferenceTest::RunTest(const FString& Parameters)
{
	const FGameplayTag AttackGroup = AnimationResolver_GroupAttack;
	const FGameplayTag Recovery = AnimationResolver_PhaseRecovery;
	const FGameplayTag Active = AnimationResolver_PhaseActive;

	UPaper2DPlusCharacterProfileAsset* Profile = NewObject<UPaper2DPlusCharacterProfileAsset>();
	UPaperFlipbook* RootFlipbook = AnimationResolver_AddMove(Profile, TEXT("ResidentRoot"));
	AnimationResolver_AddColdSoftMove(Profile, TEXT("ColdRecover"), Recovery);
	AnimationResolver_AddWrongClassSoftMove(Profile, TEXT("UnloadableActive"), Active);
	AnimationResolver_AddGroupEntry(Profile, AttackGroup, TEXT("ResidentRoot"), /*bIsChainStart=*/true);
	AnimationResolver_AddGroupEntry(Profile, AttackGroup, TEXT("ColdRecover"));
	AnimationResolver_AddGroupEntry(Profile, AttackGroup, TEXT("UnloadableActive"));
	AnimationResolver_AddTransition(Profile, TEXT("ResidentRoot"), TEXT("ColdRecover"));
	AnimationResolver_AddTransition(Profile, TEXT("ResidentRoot"), TEXT("UnloadableActive"));

	const FFlipbookProfileEntry* ColdEntry = Profile->FindFlipbookDataPtr(TEXT("ColdRecover"));
	TestNotNull(TEXT("Cold target entry exists"), ColdEntry);
	TestFalse(TEXT("Cold target has an authored soft path"), ColdEntry->Identity.Flipbook.IsNull());
	TestNull(TEXT("Cold target has no resident hard object"), ColdEntry->Identity.Flipbook.Get());
	const FFlipbookProfileEntry* UnloadableEntry =
		Profile->FindFlipbookDataPtr(TEXT("UnloadableActive"));
	TestNotNull(TEXT("Unloadable target entry exists"), UnloadableEntry);
	TestFalse(TEXT("Unloadable target has an authored soft path"),
		UnloadableEntry->Identity.Flipbook.IsNull());
	TestNull(TEXT("Unloadable target cannot resolve as a typed flipbook"),
		UnloadableEntry->Identity.Flipbook.Get());

	const TArray<UPaperFlipbook*> Openers =
		UPaper2DPlusAnimationMapLibrary::GetComboOpenerFlipbooks(Profile);
	TestEqual(TEXT("The resident flagged opener still resolves with cold group members"),
		Openers.Num(), 1);
	TestTrue(TEXT("Resolved opener is the requested resident object"),
		Openers.Num() == 1 && Openers[0] == RootFlipbook);

	FPaper2DPlusAnimationSelectionCriteria Criteria;
	FPaper2DPlusAnimationTransitionInfo Info;
	TestTrue(TEXT("Transition metadata resolves without loading its target"),
		UPaper2DPlusAnimationMapLibrary::GetAnimationTransitionInfo(
			Profile, RootFlipbook, Criteria, Info));
	TestTrue(TEXT("Cold direct target contributes transition metadata"), Info.bHasTransitions);
	TestTrue(TEXT("Cold direct target contributes its authored phase"), Info.bHasPhases);
	TestNull(TEXT("Metadata discovery does not synchronously load target art"),
		ColdEntry->Identity.Flipbook.Get());

	UPaperFlipbook* Resolved = RootFlipbook;
	TestEqual(TEXT("A direct phase target that cannot resolve as a flipbook reports NoMatch"),
		UPaper2DPlusAnimationMapLibrary::ResolveAnimationTransition(
			Profile, RootFlipbook, Active, Criteria, Resolved),
		EPaper2DPlusAnimationResolveResult::NoMatch);
	TestNull(TEXT("An unloadable direct target clears a seeded output"), Resolved);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusAnimationResolverBoundaryTest,
	"Paper2DPlus.AnimationMapResolver.ChainStartBoundariesCyclesAndInvalidRowsFailClosed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusAnimationResolverBoundaryTest::RunTest(const FString& Parameters)
{
	const FGameplayTag AttackGroup = AnimationResolver_GroupAttack;
	const FGameplayTag SpecialGroup = AnimationResolver_GroupSpecial;
	const FGameplayTag Active = AnimationResolver_PhaseActive;

	UPaper2DPlusCharacterProfileAsset* Profile = NewObject<UPaper2DPlusCharacterProfileAsset>();
	UPaperFlipbook* RootOne = AnimationResolver_AddMove(Profile, TEXT("RootOne"));
	FGameplayTagContainer MiddleTags;
	MiddleTags.AddTag(AnimationResolver_CriteriaSpecial);
	UPaperFlipbook* Middle = AnimationResolver_AddMove(Profile, TEXT("Middle"), Active, MiddleTags);
	UPaperFlipbook* RootTwo = AnimationResolver_AddMove(Profile, TEXT("RootTwo"), Active);
	UPaperFlipbook* OtherGroupTarget = AnimationResolver_AddMove(Profile, TEXT("OtherGroupTarget"), Active);
	UPaperFlipbook* DraftOnly = AnimationResolver_AddMove(Profile, TEXT("DraftOnly"));
	AnimationResolver_AddGroupEntry(Profile, AttackGroup, TEXT("RootOne"), /*bIsChainStart=*/true);
	AnimationResolver_AddGroupEntry(Profile, AttackGroup, TEXT("Middle"));
	AnimationResolver_AddGroupEntry(Profile, AttackGroup, TEXT("RootTwo"), /*bIsChainStart=*/true);
	AnimationResolver_AddGroupEntry(Profile, AttackGroup, TEXT("DraftOnly"));
	AnimationResolver_AddGroupEntry(Profile, SpecialGroup, TEXT("OtherGroupTarget"), /*bIsChainStart=*/true);
	AnimationResolver_AddTransition(Profile, TEXT("RootOne"), TEXT("Middle"));
	AnimationResolver_AddTransition(Profile, TEXT("Middle"), TEXT("RootOne")); // cycle
	AnimationResolver_AddTransition(Profile, TEXT("Middle"), TEXT("Middle"));  // explicit self-loop
	AnimationResolver_AddTransition(Profile, TEXT("Middle"), TEXT("RootTwo")); // root boundary
	AnimationResolver_AddTransition(Profile, TEXT("Middle"), TEXT("OtherGroupTarget")); // group boundary
	AnimationResolver_AddTransition(Profile, TEXT("DraftOnly"), FString());
	AnimationResolver_AddTransition(Profile, TEXT("DraftOnly"), TEXT("Missing"));

	const TArray<UPaperFlipbook*> Openers =
		UPaper2DPlusAnimationMapLibrary::GetComboOpenerFlipbooks(Profile);
	TestEqual(TEXT("All three flagged chain starts are discovered"), Openers.Num(), 3);
	TestTrue(TEXT("Sanity: the first Attack opener is the authored chain-start object"),
		Openers.Num() == 3 && Openers[0] == RootOne);

	FPaper2DPlusAnimationSelectionCriteria Criteria;
	FPaper2DPlusAnimationTransitionInfo Info;
	TestTrue(TEXT("A different chain start is an ordinary flipbook key now"),
		UPaper2DPlusAnimationMapLibrary::GetAnimationTransitionInfo(Profile, RootTwo, Criteria, Info));
	TestFalse(TEXT("A root with no outgoing rows reports no transitions"), Info.bHasTransitions);

	UPaperFlipbook* Resolved = nullptr;
	TestEqual(TEXT("Root and group boundaries no longer hide direct targets; ties are explicit"),
		UPaper2DPlusAnimationMapLibrary::ResolveAnimationTransition(
			Profile, Middle, Active, Criteria, Resolved),
		EPaper2DPlusAnimationResolveResult::Ambiguous);
	TestNull(TEXT("Ambiguity clears the output"), Resolved);

	FPaper2DPlusAnimationSelectionCriteria SelfLoop;
	SelfLoop.RequiredAllTags.AddTag(AnimationResolver_CriteriaSpecial);
	TestEqual(TEXT("A cycle-safe self-loop can resolve back to the original via criteria"),
		UPaper2DPlusAnimationMapLibrary::ResolveAnimationTransition(
			Profile, Middle, Active, SelfLoop, Resolved),
		EPaper2DPlusAnimationResolveResult::Success);
	TestTrue(TEXT("The explicit self-loop resolves Middle"), Resolved == Middle);

	FPaper2DPlusAnimationSelectionCriteria CrossGroup;
	CrossGroup.ExcludedTags.AddTag(AttackGroup);
	TestEqual(TEXT("A cross-group direct target is selectable via criteria"),
		UPaper2DPlusAnimationMapLibrary::ResolveAnimationTransition(
			Profile, Middle, Active, CrossGroup, Resolved),
		EPaper2DPlusAnimationResolveResult::Success);
	TestTrue(TEXT("Excluding the home group resolves the other group's target"),
		Resolved == OtherGroupTarget);

	TestTrue(TEXT("Draft rows resolve their info from the flipbook alone"),
		UPaper2DPlusAnimationMapLibrary::GetAnimationTransitionInfo(Profile, DraftOnly, Criteria, Info));
	TestFalse(TEXT("Dangling and empty rows contribute no transitions"), Info.bHasTransitions);
	TestEqual(TEXT("A null original is an invalid request"),
		UPaper2DPlusAnimationMapLibrary::ResolveAnimationTransition(
			Profile, nullptr, Active, Criteria, Resolved),
		EPaper2DPlusAnimationResolveResult::InvalidRequest);
	TestNull(TEXT("Every failure clears the output"), Resolved);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusAnimationResolverBlueprintSurfaceTest,
	"Paper2DPlus.AnimationMapResolver.BlueprintSurfaceIsFocused",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusAnimationResolverBlueprintSurfaceTest::RunTest(const FString& Parameters)
{
	struct FLegacyFunctionGroup
	{
		UClass* OwnerClass;
		TArray<FName> FunctionNames;
	};

	const TArray<FLegacyFunctionGroup> LegacyGroups =
	{
		{
			UPaper2DPlusCharacterProfileAsset::StaticClass(),
			{
				TEXT("GetFlipbookDataForTag"), TEXT("GetFlipbooksForTag"),
				TEXT("GetFirstFlipbookForTag"), TEXT("GetRandomFlipbookForTag"),
				TEXT("GetTagMapping"), TEXT("HasTagMapping"), TEXT("GetAllMappedTags"),
				TEXT("GetFlipbookCountForTag"), TEXT("HasTransitions"),
				TEXT("GetTransitionTargets")
			}
		},
		{
			UPaper2DPlusBlueprintLibrary::StaticClass(),
			{
				TEXT("GetFlipbooksWithPhaseTag"), TEXT("GetComboChainFlipbooks"),
				TEXT("GetComboChainFlipbookAtIndex"), TEXT("GetComboChainLength"),
				TEXT("GetActiveComboFlipbooks"), TEXT("GetComboRootFlipbooks"),
				TEXT("IsComboRoot"), TEXT("GetFlipbooksWithAnimationTag"),
				TEXT("GetFlipbooksWithAllAnimationTags"),
				TEXT("GetFlipbooksWithAnyAnimationTags"),
				TEXT("GetChainRootsWithAnimationTag"), TEXT("GetAnimationTags")
			}
		},
		{
			UPaper2DPlusPaperZDLibrary::StaticClass(),
			{ TEXT("GetPaperZDSequenceForTag") }
		},
		{
			// The numbered-root Group/Root discovery route died with chain-start flags.
			UPaper2DPlusAnimationMapLibrary::StaticClass(),
			{
				TEXT("GetAnimationGroupTags"), TEXT("FindAnimationGroup"),
				TEXT("GetAnimationRootNumbers"), TEXT("FindAnimationRoot")
			}
		}
	};

	int32 LegacyFunctionCount = 0;
	for (const FLegacyFunctionGroup& Group : LegacyGroups)
	{
		for (const FName FunctionName : Group.FunctionNames)
		{
			++LegacyFunctionCount;
			TestNull(
				*FString::Printf(TEXT("Legacy Blueprint function %s is absent"), *FunctionName.ToString()),
				Group.OwnerClass->FindFunctionByName(FunctionName));
		}
	}
	TestEqual(TEXT("Reflection ratchet covers the complete deleted function set"), LegacyFunctionCount, 27);
	for (const FName RetainedPaperZDFunction :
		{ FName(TEXT("FindPaperZDSequenceForFlipbook")),
			FName(TEXT("GetCachedPaperZDSequenceForFlipbook")),
			FName(TEXT("GetActorCurrentPaperZDSequence")) })
	{
		TestNotNull(
			*FString::Printf(TEXT("Canonical PaperZD function %s remains exposed"),
				*RetainedPaperZDFunction.ToString()),
			UPaper2DPlusPaperZDLibrary::StaticClass()->FindFunctionByName(RetainedPaperZDFunction));
	}

	const TArray<FName> FocusedFunctions =
	{
		TEXT("FindAnimationByExactTags"),
		TEXT("GetAnimationTransitionInfo"), TEXT("ResolveAnimationTransition"),
		TEXT("GetComboChainFlipbookAtIndex"), TEXT("GetComboChainFlipbookAtIndexByTags"),
		TEXT("GetComboChainLength"), TEXT("GetComboChainLengthByTags"),
		TEXT("HasComboChain"),
		TEXT("GetComboOpenerFlipbooks")
	};
	const TSet<FName> ComboFunctions =
	{
		TEXT("GetComboChainFlipbookAtIndex"), TEXT("GetComboChainFlipbookAtIndexByTags"),
		TEXT("GetComboChainLength"), TEXT("GetComboChainLengthByTags"),
		TEXT("HasComboChain"), TEXT("GetComboOpenerFlipbooks")
	};
	for (const FName FunctionName : FocusedFunctions)
	{
		UFunction* Function =
			UPaper2DPlusAnimationMapLibrary::StaticClass()->FindFunctionByName(FunctionName);
		TestNotNull(
			*FString::Printf(TEXT("Focused Blueprint function %s is present"), *FunctionName.ToString()),
			Function);
		const FString ExpectedCategory = ComboFunctions.Contains(FunctionName)
			? TEXT("Paper2DPlus|Animation Map|Combo")
			: TEXT("Paper2DPlus|Animation Map");
		TestEqual(
			*FString::Printf(TEXT("Focused Blueprint function %s uses the expected action-menu category"),
				*FunctionName.ToString()),
			Function ? Function->GetMetaData(TEXT("Category")) : FString(),
			ExpectedCategory);
	}
	TSet<FName> DeclaredBlueprintFunctions;
	for (TFieldIterator<UFunction> FunctionIt(
		UPaper2DPlusAnimationMapLibrary::StaticClass(), EFieldIterationFlags::None);
		FunctionIt; ++FunctionIt)
	{
		if (FunctionIt->HasAnyFunctionFlags(FUNC_BlueprintCallable | FUNC_BlueprintPure))
		{
			DeclaredBlueprintFunctions.Add(FunctionIt->GetFName());
		}
	}
	TestEqual(TEXT("Focused library declares exactly the approved Blueprint functions"),
		DeclaredBlueprintFunctions.Num(), FocusedFunctions.Num());
	for (const FName FunctionName : FocusedFunctions)
	{
		TestTrue(
			*FString::Printf(TEXT("Declared Blueprint set contains only approved node %s"),
				*FunctionName.ToString()),
			DeclaredBlueprintFunctions.Contains(FunctionName));
	}
	UFunction* ExactTagsFunction = UPaper2DPlusAnimationMapLibrary::StaticClass()->FindFunctionByName(
		TEXT("FindAnimationByExactTags"));
	TestNotNull(TEXT("FindAnimationByExactTags reflection exists for naming checks"), ExactTagsFunction);
	if (ExactTagsFunction)
	{
		TestEqual(TEXT("Blueprint node uses the requested designer-facing name"),
			ExactTagsFunction->GetMetaData(TEXT("DisplayName")),
			FString(TEXT("Find Animation by Exact Tags")));
	}

	UFunction* ResolveFunction = UPaper2DPlusAnimationMapLibrary::StaticClass()->FindFunctionByName(
		TEXT("ResolveAnimationTransition"));
	TestNotNull(TEXT("ResolveAnimationTransition reflection exists for metadata checks"), ResolveFunction);
	if (ResolveFunction)
	{
		TestFalse(TEXT("Phase filter is not applied to every GameplayTag pin on the function"),
			ResolveFunction->HasMetaData(TEXT("GameplayTagFilter")));
		const FProperty* RequestedPhaseProperty = FindFProperty<FProperty>(
			ResolveFunction, TEXT("RequestedPhase"));
		TestNotNull(TEXT("RequestedPhase parameter exists"), RequestedPhaseProperty);
		TestEqual(TEXT("Only RequestedPhase carries the phase picker filter"),
			RequestedPhaseProperty
				? RequestedPhaseProperty->GetMetaData(TEXT("GameplayTagFilter"))
				: FString(),
			FString(TEXT("Paper2DPlus.Phase")));
	}

	const TArray<UScriptStruct*> RawMappingStructs =
	{
		FFlipbookTagMappingEntry::StaticStruct(),
		FFlipbookTagMapping::StaticStruct()
	};
	for (UScriptStruct* RawStruct : RawMappingStructs)
	{
		TestFalse(
			*FString::Printf(TEXT("Raw mapping struct %s is not BlueprintType"), *RawStruct->GetName()),
			RawStruct->GetBoolMetaData(TEXT("BlueprintType")));
		for (TFieldIterator<FProperty> PropertyIt(RawStruct); PropertyIt; ++PropertyIt)
		{
			TestFalse(
				*FString::Printf(TEXT("Raw mapping property %s.%s is not Blueprint-visible"),
					*RawStruct->GetName(), *PropertyIt->GetName()),
				PropertyIt->HasAnyPropertyFlags(CPF_BlueprintVisible));
		}
	}

	const FProperty* TagMappingsProperty = FindFProperty<FProperty>(
		UPaper2DPlusCharacterProfileAsset::StaticClass(),
		GET_MEMBER_NAME_CHECKED(UPaper2DPlusCharacterProfileAsset, TagMappings));
	TestNotNull(TEXT("TagMappings storage remains reflected for serialization"), TagMappingsProperty);
	TestFalse(TEXT("TagMappings storage is not exposed directly to Blueprints"),
		TagMappingsProperty && TagMappingsProperty->HasAnyPropertyFlags(CPF_BlueprintVisible));
	return true;
}

#endif // WITH_EDITOR
