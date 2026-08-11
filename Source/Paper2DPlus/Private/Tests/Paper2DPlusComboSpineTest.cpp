// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#if WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "NativeGameplayTags.h"
#include "Paper2DPlusAnimationMapLibrary.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Paper2DPlusComboChain.h"
#include "Paper2DPlusMoveTransition.h"
#include "PaperFlipbook.h"
#include "UObject/SoftObjectPath.h"

UE_DEFINE_GAMEPLAY_TAG_STATIC(
	ComboSpine_GroupAttack,
	"Paper2DPlus.Test.ComboSpine.Group.Attack")
UE_DEFINE_GAMEPLAY_TAG_STATIC(
	ComboSpine_GroupSpecial,
	"Paper2DPlus.Test.ComboSpine.Group.Special")
UE_DEFINE_GAMEPLAY_TAG_STATIC(
	ComboSpine_ExactOpener,
	"Paper2DPlus.Test.ComboSpine.Exact.Opener")
UE_DEFINE_GAMEPLAY_TAG_STATIC(
	ComboSpine_ChainIdentity,
	"Paper2DPlus.Test.ComboSpine.Chain.Identity")
UE_DEFINE_GAMEPLAY_TAG_STATIC(
	ComboSpine_ChainIdentityAlt,
	"Paper2DPlus.Test.ComboSpine.Chain.IdentityAlt")

namespace
{
	UPaperFlipbook* ComboSpine_AddMove(
		UPaper2DPlusCharacterProfileAsset* Profile,
		const FString& Name,
		const FGameplayTagContainer& AnimationTags = FGameplayTagContainer())
	{
		FFlipbookProfileEntry Entry;
		Entry.Identity.FlipbookName = Name;
		Entry.Identity.Flipbook = NewObject<UPaperFlipbook>(Profile);
		Entry.EditorMeta.AnimationTags = AnimationTags;
		Profile->Flipbooks.Add(Entry);
		return Entry.Identity.Flipbook.Get();
	}

	void ComboSpine_AddColdSoftMove(UPaper2DPlusCharacterProfileAsset* Profile, const FString& Name)
	{
		FFlipbookProfileEntry Entry;
		Entry.Identity.FlipbookName = Name;
		Entry.Identity.Flipbook = TSoftObjectPtr<UPaperFlipbook>(FSoftObjectPath(
			FString::Printf(TEXT("/Temp/ComboSpine_%s.ComboSpine_%s"), *Name, *Name)));
		Profile->Flipbooks.Add(MoveTemp(Entry));
	}

	void ComboSpine_AddGroupEntry(
		UPaper2DPlusCharacterProfileAsset* Profile,
		const FGameplayTag& GroupTag,
		const FString& Name,
		bool bIsChainStart = false,
		bool bIsChainEnd = false,
		const FGameplayTagContainer& ChainTags = FGameplayTagContainer())
	{
		FFlipbookTagMappingEntry Entry(Name);
		Entry.bIsChainStart = bIsChainStart;
		Entry.bIsChainEnd = bIsChainEnd;
		Entry.ChainTags = ChainTags;
		Profile->TagMappings.FindOrAdd(GroupTag).Entries.Add(Entry);
	}

	void ComboSpine_AddTransition(
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
	FPaper2DPlusComboSpineDerivationTest,
	"Paper2DPlus.ComboSpine.MainLineIsLongestDeterministicAndBounded",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusComboSpineDerivationTest::RunTest(const FString& Parameters)
{
	const FGameplayTag AttackGroup = ComboSpine_GroupAttack;
	const FGameplayTag SpecialGroup = ComboSpine_GroupSpecial;

	UPaper2DPlusCharacterProfileAsset* Profile = NewObject<UPaper2DPlusCharacterProfileAsset>();
	ComboSpine_AddMove(Profile, TEXT("Jab"));
	ComboSpine_AddMove(Profile, TEXT("Jab2"));
	ComboSpine_AddMove(Profile, TEXT("Finisher"));
	ComboSpine_AddMove(Profile, TEXT("OtherRoot"));
	ComboSpine_AddMove(Profile, TEXT("OtherGroupMove"));
	ComboSpine_AddGroupEntry(Profile, AttackGroup, TEXT("Jab"), /*bIsChainStart=*/true);
	ComboSpine_AddGroupEntry(Profile, AttackGroup, TEXT("Jab2"));
	ComboSpine_AddGroupEntry(Profile, AttackGroup, TEXT("Finisher"));
	ComboSpine_AddGroupEntry(Profile, AttackGroup, TEXT("OtherRoot"), /*bIsChainStart=*/true);
	ComboSpine_AddGroupEntry(Profile, SpecialGroup, TEXT("OtherGroupMove"), /*bIsChainStart=*/true);

	// The killer case: the shortcut edge (Jab -> Finisher) is authored FIRST, but the main line must
	// still run the long route through Jab2. Cycle, other-root, and cross-group edges are boundaries.
	ComboSpine_AddTransition(Profile, TEXT("Jab"), TEXT("Finisher"));   // shortcut, row 0
	ComboSpine_AddTransition(Profile, TEXT("Jab"), TEXT("Jab2"));       // main line, row 1
	ComboSpine_AddTransition(Profile, TEXT("Jab"), TEXT("OtherRoot"));  // root boundary
	ComboSpine_AddTransition(Profile, TEXT("Jab"), TEXT("OtherGroupMove")); // group boundary
	ComboSpine_AddTransition(Profile, TEXT("Jab2"), TEXT("Finisher"));
	ComboSpine_AddTransition(Profile, TEXT("Finisher"), TEXT("Jab"));   // cycle back to the root

	const TArray<FString> Spine =
		Paper2DPlusComboChain::DeriveComboSpine(Profile, AttackGroup, TEXT("Jab"));
	TestEqual(TEXT("The main line runs the LONGEST route, not the first-authored shortcut"),
		Spine, TArray<FString>({ TEXT("Jab"), TEXT("Jab2"), TEXT("Finisher") }));

	// Contrast pin: the DFS flatten (phase derivation) visits the shortcut target first — the spine
	// deliberately diverges from it, which is the whole reason it exists.
	const TArray<FString> Flatten =
		Paper2DPlusComboChain::DeriveComboChain(Profile, AttackGroup, TEXT("Jab"));
	TestEqual(TEXT("Sanity: the DFS flatten still visits the shortcut target first"),
		Flatten, TArray<FString>({ TEXT("Jab"), TEXT("Finisher"), TEXT("Jab2") }));

	// Equal-length continuations tie-break by authored row order.
	UPaper2DPlusCharacterProfileAsset* TieProfile = NewObject<UPaper2DPlusCharacterProfileAsset>();
	ComboSpine_AddMove(TieProfile, TEXT("Opener"));
	ComboSpine_AddMove(TieProfile, TEXT("BranchA"));
	ComboSpine_AddMove(TieProfile, TEXT("BranchB"));
	ComboSpine_AddGroupEntry(TieProfile, AttackGroup, TEXT("Opener"), /*bIsChainStart=*/true);
	ComboSpine_AddGroupEntry(TieProfile, AttackGroup, TEXT("BranchA"));
	ComboSpine_AddGroupEntry(TieProfile, AttackGroup, TEXT("BranchB"));
	ComboSpine_AddTransition(TieProfile, TEXT("Opener"), TEXT("BranchA"));
	ComboSpine_AddTransition(TieProfile, TEXT("Opener"), TEXT("BranchB"));
	TestEqual(TEXT("Equal-length continuations tie-break by authored row order"),
		Paper2DPlusComboChain::DeriveComboSpine(TieProfile, AttackGroup, TEXT("Opener")),
		TArray<FString>({ TEXT("Opener"), TEXT("BranchA") }));

	// A valid chain start with no in-scope transitions is a one-move line; invalid starts return empty.
	TestEqual(TEXT("The second chain start has no outgoing rows — a one-move line"),
		Paper2DPlusComboChain::DeriveComboSpine(Profile, AttackGroup, TEXT("OtherRoot")),
		TArray<FString>({ TEXT("OtherRoot") }));
	TestEqual(TEXT("An unflagged move name returns an empty spine"),
		Paper2DPlusComboChain::DeriveComboSpine(Profile, AttackGroup, TEXT("Jab2")).Num(), 0);
	TestEqual(TEXT("An empty move name returns an empty spine"),
		Paper2DPlusComboChain::DeriveComboSpine(Profile, AttackGroup, FString()).Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusComboSpineFindForMoveTest,
	"Paper2DPlus.ComboSpine.FindForMoveLocatesTheUniqueChainOrFailsClosed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusComboSpineFindForMoveTest::RunTest(const FString& Parameters)
{
	const FGameplayTag AttackGroup = ComboSpine_GroupAttack;
	const FGameplayTag SpecialGroup = ComboSpine_GroupSpecial;

	UPaper2DPlusCharacterProfileAsset* Profile = NewObject<UPaper2DPlusCharacterProfileAsset>();
	ComboSpine_AddMove(Profile, TEXT("Jab"));
	ComboSpine_AddMove(Profile, TEXT("Jab2"));
	ComboSpine_AddMove(Profile, TEXT("Alt"));
	ComboSpine_AddMove(Profile, TEXT("Lone"));
	ComboSpine_AddMove(Profile, TEXT("Shared"));
	ComboSpine_AddMove(Profile, TEXT("SpecialRoot"));
	ComboSpine_AddGroupEntry(Profile, AttackGroup, TEXT("Jab"), /*bIsChainStart=*/true);
	ComboSpine_AddGroupEntry(Profile, AttackGroup, TEXT("Jab2"));
	ComboSpine_AddGroupEntry(Profile, AttackGroup, TEXT("Alt"));
	ComboSpine_AddGroupEntry(Profile, AttackGroup, TEXT("Shared"));
	ComboSpine_AddGroupEntry(Profile, SpecialGroup, TEXT("SpecialRoot"), /*bIsChainStart=*/true);
	ComboSpine_AddGroupEntry(Profile, SpecialGroup, TEXT("Shared"));
	ComboSpine_AddTransition(Profile, TEXT("Jab"), TEXT("Jab2"));
	ComboSpine_AddTransition(Profile, TEXT("Jab"), TEXT("Alt")); // shorter — a side branch
	ComboSpine_AddTransition(Profile, TEXT("Jab2"), TEXT("Alt")); // makes the Jab2 route longest
	ComboSpine_AddTransition(Profile, TEXT("Jab"), TEXT("Shared"));
	ComboSpine_AddTransition(Profile, TEXT("SpecialRoot"), TEXT("Shared"));

	Paper2DPlusComboChain::FScopedAnimationRoot Root;
	TArray<FString> Spine;
	TestEqual(TEXT("The root itself locates its own chain"),
		(int32)Paper2DPlusComboChain::FindComboSpineForMove(Profile, TEXT("Jab"), Root, Spine),
		(int32)Paper2DPlusComboChain::EComboSpineFindResult::Found);
	TestEqual(TEXT("Spine runs the longest route"),
		Spine, TArray<FString>({ TEXT("Jab"), TEXT("Jab2"), TEXT("Alt") }));
	TestEqual(TEXT("The located root carries its group"), Root.GroupTag, AttackGroup);
	TestEqual(TEXT("The located root carries its opener move name"), Root.RootMove, FString(TEXT("Jab")));

	TestEqual(TEXT("A mid-chain member locates the same chain (case-insensitive)"),
		(int32)Paper2DPlusComboChain::FindComboSpineForMove(Profile, TEXT("jab2"), Root, Spine),
		(int32)Paper2DPlusComboChain::EComboSpineFindResult::Found);
	TestEqual(TEXT("Mid-chain lookup returns the full spine"), Spine.Num(), 3);

	TestEqual(TEXT("A move on no rooted chain reports NotChained"),
		(int32)Paper2DPlusComboChain::FindComboSpineForMove(Profile, TEXT("Lone"), Root, Spine),
		(int32)Paper2DPlusComboChain::EComboSpineFindResult::NotChained);
	TestEqual(TEXT("A move reachable from two roots reports AmbiguousChain"),
		(int32)Paper2DPlusComboChain::FindComboSpineForMove(Profile, TEXT("Shared"), Root, Spine),
		(int32)Paper2DPlusComboChain::EComboSpineFindResult::AmbiguousChain);
	TestEqual(TEXT("Ambiguity clears the spine"), Spine.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusComboChainBlueprintTest,
	"Paper2DPlus.ComboSpine.BlueprintStepNodesResolveOpenerPlusIndex",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusComboChainBlueprintTest::RunTest(const FString& Parameters)
{
	const FGameplayTag AttackGroup = ComboSpine_GroupAttack;
	const FGameplayTag SpecialGroup = ComboSpine_GroupSpecial;

	UPaper2DPlusCharacterProfileAsset* Profile = NewObject<UPaper2DPlusCharacterProfileAsset>();
	FGameplayTagContainer OpenerTags;
	OpenerTags.AddTag(ComboSpine_ExactOpener);
	UPaperFlipbook* Jab = ComboSpine_AddMove(Profile, TEXT("Jab"), OpenerTags);
	UPaperFlipbook* Jab2 = ComboSpine_AddMove(Profile, TEXT("Jab2"));
	UPaperFlipbook* Finisher = ComboSpine_AddMove(Profile, TEXT("Finisher"));
	ComboSpine_AddMove(Profile, TEXT("Alt"));
	UPaperFlipbook* Lone = ComboSpine_AddMove(Profile, TEXT("Lone"));
	ComboSpine_AddGroupEntry(Profile, AttackGroup, TEXT("Jab"), /*bIsChainStart=*/true);
	ComboSpine_AddGroupEntry(Profile, AttackGroup, TEXT("Jab2"));
	ComboSpine_AddGroupEntry(Profile, AttackGroup, TEXT("Finisher"));
	ComboSpine_AddGroupEntry(Profile, AttackGroup, TEXT("Alt"));
	ComboSpine_AddTransition(Profile, TEXT("Jab"), TEXT("Jab2"));
	ComboSpine_AddTransition(Profile, TEXT("Jab"), TEXT("Alt")); // side branch — never indexed
	ComboSpine_AddTransition(Profile, TEXT("Jab2"), TEXT("Finisher"));

	// The combo-counter walk: opener as the reference, counter as the index, one flipbook out.
	UPaperFlipbook* Step = nullptr;
	int32 ChainLength = 0;
	TestEqual(TEXT("Index 0 is the opener itself"),
		UPaper2DPlusAnimationMapLibrary::GetComboChainFlipbookAtIndex(
			Profile, Jab, 0, Step, ChainLength),
		EPaper2DPlusComboChainResult::Success);
	TestTrue(TEXT("Index 0 returns the opener flipbook"), Step == Jab);
	TestEqual(TEXT("Success reports the chain length"), ChainLength, 3);
	TestEqual(TEXT("Index 1 is the next main-line hit"),
		UPaper2DPlusAnimationMapLibrary::GetComboChainFlipbookAtIndex(
			Profile, Jab, 1, Step, ChainLength),
		EPaper2DPlusComboChainResult::Success);
	TestTrue(TEXT("Index 1 walks the LONG route, never the authored side branch"), Step == Jab2);
	TestEqual(TEXT("Index 2 is the finisher"),
		UPaper2DPlusAnimationMapLibrary::GetComboChainFlipbookAtIndex(
			Profile, Jab, 2, Step, ChainLength),
		EPaper2DPlusComboChainResult::Success);
	TestTrue(TEXT("Index 2 returns the finisher flipbook"), Step == Finisher);

	// The "combo finished" signal: walking past the end keeps the length so counters can reset.
	Step = Finisher;
	TestEqual(TEXT("Index past the end reports IndexOutOfRange"),
		UPaper2DPlusAnimationMapLibrary::GetComboChainFlipbookAtIndex(
			Profile, Jab, 3, Step, ChainLength),
		EPaper2DPlusComboChainResult::IndexOutOfRange);
	TestNull(TEXT("IndexOutOfRange clears the flipbook"), Step);
	TestEqual(TEXT("IndexOutOfRange still reports the chain length"), ChainLength, 3);

	// Only the flagged opener keys a chain; members and unchained moves fail explicitly.
	TestEqual(TEXT("A mid-chain member is not a chain key"),
		UPaper2DPlusAnimationMapLibrary::GetComboChainFlipbookAtIndex(
			Profile, Jab2, 0, Step, ChainLength),
		EPaper2DPlusComboChainResult::NotChainStart);
	TestEqual(TEXT("NotChainStart clears the length"), ChainLength, 0);
	TestEqual(TEXT("An unmapped-but-real move is not a chain key"),
		UPaper2DPlusAnimationMapLibrary::GetComboChainFlipbookAtIndex(
			Profile, Lone, 0, Step, ChainLength),
		EPaper2DPlusComboChainResult::NotChainStart);

	UPaperFlipbook* Foreign = NewObject<UPaperFlipbook>(Profile);
	TestEqual(TEXT("A flipbook with no map entry reports NotFound"),
		UPaper2DPlusAnimationMapLibrary::GetComboChainFlipbookAtIndex(
			Profile, Foreign, 0, Step, ChainLength),
		EPaper2DPlusComboChainResult::NotFound);
	TestEqual(TEXT("A null flipbook reports InvalidRequest"),
		UPaper2DPlusAnimationMapLibrary::GetComboChainFlipbookAtIndex(
			Profile, nullptr, 0, Step, ChainLength),
		EPaper2DPlusComboChainResult::InvalidRequest);
	TestEqual(TEXT("A null profile reports InvalidRequest"),
		UPaper2DPlusAnimationMapLibrary::GetComboChainFlipbookAtIndex(
			nullptr, Jab, 0, Step, ChainLength),
		EPaper2DPlusComboChainResult::InvalidRequest);
	TestEqual(TEXT("A negative index reports InvalidRequest"),
		UPaper2DPlusAnimationMapLibrary::GetComboChainFlipbookAtIndex(
			Profile, Jab, -1, Step, ChainLength),
		EPaper2DPlusComboChainResult::InvalidRequest);

	// The tag-container entry: the opener's exact tags + a counter, one step out.
	FGameplayTagContainer QueryTags;
	QueryTags.AddTag(ComboSpine_ExactOpener);
	TestEqual(TEXT("Exact tags + index resolve the same step"),
		UPaper2DPlusAnimationMapLibrary::GetComboChainFlipbookAtIndexByTags(
			Profile, QueryTags, 1, Step, ChainLength),
		EPaper2DPlusComboChainResult::Success);
	TestTrue(TEXT("Tags entry walks the same main line"), Step == Jab2);
	TestEqual(TEXT("Tags entry reports the same length"), ChainLength, 3);

	FGameplayTagContainer NoSuchTags;
	NoSuchTags.AddTag(ComboSpine_GroupAttack);
	TestEqual(TEXT("Unmatched exact tags report NotFound"),
		UPaper2DPlusAnimationMapLibrary::GetComboChainFlipbookAtIndexByTags(
			Profile, NoSuchTags, 0, Step, ChainLength),
		EPaper2DPlusComboChainResult::NotFound);
	TestEqual(TEXT("Empty tags report InvalidRequest"),
		UPaper2DPlusAnimationMapLibrary::GetComboChainFlipbookAtIndexByTags(
			Profile, FGameplayTagContainer(), 0, Step, ChainLength),
		EPaper2DPlusComboChainResult::InvalidRequest);

	ComboSpine_AddMove(Profile, TEXT("JabTwin"), OpenerTags);
	TestEqual(TEXT("Duplicate exact containers report AmbiguousInput"),
		UPaper2DPlusAnimationMapLibrary::GetComboChainFlipbookAtIndexByTags(
			Profile, QueryTags, 0, Step, ChainLength),
		EPaper2DPlusComboChainResult::AmbiguousInput);

	// An opener flagged in TWO exact groups has no unique chain.
	ComboSpine_AddGroupEntry(Profile, SpecialGroup, TEXT("Jab"), /*bIsChainStart=*/true);
	TestEqual(TEXT("An opener flagged in two groups reports AmbiguousChain"),
		UPaper2DPlusAnimationMapLibrary::GetComboChainFlipbookAtIndex(
			Profile, Jab, 0, Step, ChainLength),
		EPaper2DPlusComboChainResult::AmbiguousChain);

	// A flipbook shared by two entries has no unambiguous identity.
	FFlipbookProfileEntry DuplicateEntry;
	DuplicateEntry.Identity.FlipbookName = TEXT("Jab2Clone");
	DuplicateEntry.Identity.Flipbook = Jab2;
	Profile->Flipbooks.Add(DuplicateEntry);
	TestEqual(TEXT("A flipbook shared by multiple entries reports AmbiguousInput"),
		UPaper2DPlusAnimationMapLibrary::GetComboChainFlipbookAtIndex(
			Profile, Jab2, 0, Step, ChainLength),
		EPaper2DPlusComboChainResult::AmbiguousInput);

	// A cold soft step fails the load fail-closed.
	UPaper2DPlusCharacterProfileAsset* ColdProfile = NewObject<UPaper2DPlusCharacterProfileAsset>();
	UPaperFlipbook* ColdRoot = ComboSpine_AddMove(ColdProfile, TEXT("ColdRoot"));
	ComboSpine_AddColdSoftMove(ColdProfile, TEXT("ColdNext"));
	ComboSpine_AddGroupEntry(ColdProfile, AttackGroup, TEXT("ColdRoot"), /*bIsChainStart=*/true);
	ComboSpine_AddGroupEntry(ColdProfile, AttackGroup, TEXT("ColdNext"));
	ComboSpine_AddTransition(ColdProfile, TEXT("ColdRoot"), TEXT("ColdNext"));
	TestEqual(TEXT("An unloadable indexed step reports LoadFailed"),
		UPaper2DPlusAnimationMapLibrary::GetComboChainFlipbookAtIndex(
			ColdProfile, ColdRoot, 1, Step, ChainLength),
		EPaper2DPlusComboChainResult::LoadFailed);
	TestNull(TEXT("LoadFailed clears the flipbook"), Step);
	TestEqual(TEXT("LoadFailed clears the length"), ChainLength, 0);
	return true;
}

// ─── Chain End: the spine's end-preference rule bounds the countable line ───────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusComboSpineChainEndTest,
	"Paper2DPlus.ComboSpine.ChainEndBoundsTheCountableLine",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusComboSpineChainEndTest::RunTest(const FString& Parameters)
{
	const FGameplayTag AttackGroup = ComboSpine_GroupAttack;

	// (1) The definitive start→end line: Jab(start)→Jab2→Jab3(end)→Recovery. The end-terminated
	// 3-step path beats the longer 4-step non-end continuation, and traversal never walks past the
	// end — Recovery stays authored/wired but is never indexed or counted.
	UPaper2DPlusCharacterProfileAsset* Profile = NewObject<UPaper2DPlusCharacterProfileAsset>();
	UPaperFlipbook* Jab = ComboSpine_AddMove(Profile, TEXT("Jab"));
	UPaperFlipbook* Jab2 = ComboSpine_AddMove(Profile, TEXT("Jab2"));
	UPaperFlipbook* Jab3 = ComboSpine_AddMove(Profile, TEXT("Jab3"));
	UPaperFlipbook* Recovery = ComboSpine_AddMove(Profile, TEXT("Recovery"));
	ComboSpine_AddGroupEntry(Profile, AttackGroup, TEXT("Jab"), /*bIsChainStart=*/true);
	ComboSpine_AddGroupEntry(Profile, AttackGroup, TEXT("Jab2"));
	ComboSpine_AddGroupEntry(Profile, AttackGroup, TEXT("Jab3"), /*bIsChainStart=*/false, /*bIsChainEnd=*/true);
	ComboSpine_AddGroupEntry(Profile, AttackGroup, TEXT("Recovery"));
	ComboSpine_AddTransition(Profile, TEXT("Jab"), TEXT("Jab2"));
	ComboSpine_AddTransition(Profile, TEXT("Jab2"), TEXT("Jab3"));
	ComboSpine_AddTransition(Profile, TEXT("Jab3"), TEXT("Recovery")); // wired AFTER the end

	TestEqual(TEXT("The spine terminates at the flagged Chain End — trailing recovery excluded"),
		Paper2DPlusComboChain::DeriveComboSpine(Profile, AttackGroup, TEXT("Jab")),
		TArray<FString>({ TEXT("Jab"), TEXT("Jab2"), TEXT("Jab3") }));

	// The Blueprint walk agrees: indices 0..2 return the main line, 3 is the "combo finished"
	// signal with the COUNTABLE length, and the Recovery flipbook is never returned.
	UPaperFlipbook* Step = nullptr;
	int32 ChainLength = 0;
	const TArray<UPaperFlipbook*> ExpectedSteps = { Jab, Jab2, Jab3 };
	for (int32 Index = 0; Index < ExpectedSteps.Num(); ++Index)
	{
		TestEqual(*FString::Printf(TEXT("Index %d resolves on the countable line"), Index),
			UPaper2DPlusAnimationMapLibrary::GetComboChainFlipbookAtIndex(
				Profile, Jab, Index, Step, ChainLength),
			EPaper2DPlusComboChainResult::Success);
		TestTrue(*FString::Printf(TEXT("Index %d is the expected step"), Index),
			Step == ExpectedSteps[Index]);
		TestTrue(TEXT("No index ever returns the post-end Recovery flipbook"), Step != Recovery);
		TestEqual(TEXT("Countable length is 3 at every index"), ChainLength, 3);
	}
	TestEqual(TEXT("Index 3 reports IndexOutOfRange — the end bounds the walk"),
		UPaper2DPlusAnimationMapLibrary::GetComboChainFlipbookAtIndex(
			Profile, Jab, 3, Step, ChainLength),
		EPaper2DPlusComboChainResult::IndexOutOfRange);
	TestNull(TEXT("IndexOutOfRange clears the flipbook"), Step);
	TestEqual(TEXT("IndexOutOfRange keeps the countable length 3"), ChainLength, 3);

	// (3) End mid-branch: two branches, only the SHORTER reaches a flagged end — it wins anyway.
	UPaper2DPlusCharacterProfileAsset* BranchProfile = NewObject<UPaper2DPlusCharacterProfileAsset>();
	ComboSpine_AddMove(BranchProfile, TEXT("Opener"));
	ComboSpine_AddMove(BranchProfile, TEXT("ShortEnd"));
	ComboSpine_AddMove(BranchProfile, TEXT("LongA"));
	ComboSpine_AddMove(BranchProfile, TEXT("LongB"));
	ComboSpine_AddGroupEntry(BranchProfile, AttackGroup, TEXT("Opener"), /*bIsChainStart=*/true);
	// The long non-end branch is authored FIRST so row order cannot be what picks the end branch.
	ComboSpine_AddGroupEntry(BranchProfile, AttackGroup, TEXT("LongA"));
	ComboSpine_AddGroupEntry(BranchProfile, AttackGroup, TEXT("LongB"));
	ComboSpine_AddGroupEntry(BranchProfile, AttackGroup, TEXT("ShortEnd"), /*bIsChainStart=*/false, /*bIsChainEnd=*/true);
	ComboSpine_AddTransition(BranchProfile, TEXT("Opener"), TEXT("LongA"));   // 3-step non-end branch, row 0
	ComboSpine_AddTransition(BranchProfile, TEXT("Opener"), TEXT("ShortEnd")); // 2-step end branch, row 1
	ComboSpine_AddTransition(BranchProfile, TEXT("LongA"), TEXT("LongB"));
	TestEqual(TEXT("The end-terminated branch beats the longer non-end branch"),
		Paper2DPlusComboChain::DeriveComboSpine(BranchProfile, AttackGroup, TEXT("Opener")),
		TArray<FString>({ TEXT("Opener"), TEXT("ShortEnd") }));

	// (4) Start-is-also-end: a 1-step chain even with an outgoing continuation authored.
	UPaper2DPlusCharacterProfileAsset* SoloProfile = NewObject<UPaper2DPlusCharacterProfileAsset>();
	UPaperFlipbook* Solo = ComboSpine_AddMove(SoloProfile, TEXT("Solo"));
	ComboSpine_AddMove(SoloProfile, TEXT("Extra"));
	ComboSpine_AddGroupEntry(SoloProfile, AttackGroup, TEXT("Solo"), /*bIsChainStart=*/true, /*bIsChainEnd=*/true);
	ComboSpine_AddGroupEntry(SoloProfile, AttackGroup, TEXT("Extra"));
	ComboSpine_AddTransition(SoloProfile, TEXT("Solo"), TEXT("Extra"));
	TestEqual(TEXT("A start that is also the end is a 1-step chain"),
		Paper2DPlusComboChain::DeriveComboSpine(SoloProfile, AttackGroup, TEXT("Solo")),
		TArray<FString>({ TEXT("Solo") }));
	TestEqual(TEXT("Length node agrees on the 1-step chain"),
		UPaper2DPlusAnimationMapLibrary::GetComboChainLength(SoloProfile, Solo, ChainLength),
		EPaper2DPlusComboChainResult::Success);
	TestEqual(TEXT("Start-is-end countable length is 1"), ChainLength, 1);
	return true;
}

// ─── The direct chain-length nodes: happy paths + every failure clears the output ───────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusComboChainLengthNodesTest,
	"Paper2DPlus.ComboSpine.LengthNodesReportTheCountableLength",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusComboChainLengthNodesTest::RunTest(const FString& Parameters)
{
	const FGameplayTag AttackGroup = ComboSpine_GroupAttack;
	const FGameplayTag SpecialGroup = ComboSpine_GroupSpecial;

	UPaper2DPlusCharacterProfileAsset* Profile = NewObject<UPaper2DPlusCharacterProfileAsset>();
	FGameplayTagContainer OpenerTags;
	OpenerTags.AddTag(ComboSpine_ExactOpener);
	UPaperFlipbook* Jab = ComboSpine_AddMove(Profile, TEXT("Jab"), OpenerTags);
	UPaperFlipbook* Jab2 = ComboSpine_AddMove(Profile, TEXT("Jab2"));
	ComboSpine_AddMove(Profile, TEXT("Finisher"));
	ComboSpine_AddGroupEntry(Profile, AttackGroup, TEXT("Jab"), /*bIsChainStart=*/true);
	ComboSpine_AddGroupEntry(Profile, AttackGroup, TEXT("Jab2"));
	ComboSpine_AddGroupEntry(Profile, AttackGroup, TEXT("Finisher"));
	ComboSpine_AddTransition(Profile, TEXT("Jab"), TEXT("Jab2"));
	ComboSpine_AddTransition(Profile, TEXT("Jab2"), TEXT("Finisher"));

	// Happy path, flipbook-keyed.
	int32 ChainLength = 0;
	TestEqual(TEXT("Length by opener flipbook succeeds"),
		UPaper2DPlusAnimationMapLibrary::GetComboChainLength(Profile, Jab, ChainLength),
		EPaper2DPlusComboChainResult::Success);
	TestEqual(TEXT("Flipbook-keyed length is the countable line"), ChainLength, 3);

	// Happy path, tag-keyed (opener own-tags fallback — no chain container authored).
	FGameplayTagContainer QueryTags;
	QueryTags.AddTag(ComboSpine_ExactOpener);
	ChainLength = 0;
	TestEqual(TEXT("Length by exact tags succeeds"),
		UPaper2DPlusAnimationMapLibrary::GetComboChainLengthByTags(Profile, QueryTags, ChainLength),
		EPaper2DPlusComboChainResult::Success);
	TestEqual(TEXT("Tag-keyed length matches"), ChainLength, 3);

	// The cheap boolean branch: only a 2+-step opener "has a combo".
	TestTrue(TEXT("A multi-step opener has a combo"),
		UPaper2DPlusAnimationMapLibrary::HasComboChain(Profile, Jab));
	TestFalse(TEXT("A mid-chain member does not key a combo"),
		UPaper2DPlusAnimationMapLibrary::HasComboChain(Profile, Jab2));
	UPaperFlipbook* Single = ComboSpine_AddMove(Profile, TEXT("Single"));
	ComboSpine_AddGroupEntry(Profile, AttackGroup, TEXT("Single"));
	TestFalse(TEXT("An unflagged single animation has no combo"),
		UPaper2DPlusAnimationMapLibrary::HasComboChain(Profile, Single));
	UPaperFlipbook* LoneStart = ComboSpine_AddMove(Profile, TEXT("LoneStart"));
	ComboSpine_AddGroupEntry(Profile, SpecialGroup, TEXT("LoneStart"), /*bIsChainStart=*/true);
	TestFalse(TEXT("A one-move chain (start with no continuations) is not a combo"),
		UPaper2DPlusAnimationMapLibrary::HasComboChain(Profile, LoneStart));
	TestFalse(TEXT("Null profile has no combo"),
		UPaper2DPlusAnimationMapLibrary::HasComboChain(nullptr, Jab));
	TestFalse(TEXT("Null flipbook has no combo"),
		UPaper2DPlusAnimationMapLibrary::HasComboChain(Profile, nullptr));

	// Every failure CLEARS the output (seed a stale value first, each time).
	ChainLength = 99;
	TestEqual(TEXT("Null profile reports InvalidRequest"),
		UPaper2DPlusAnimationMapLibrary::GetComboChainLength(nullptr, Jab, ChainLength),
		EPaper2DPlusComboChainResult::InvalidRequest);
	TestEqual(TEXT("InvalidRequest clears the length"), ChainLength, 0);

	ChainLength = 99;
	TestEqual(TEXT("Null flipbook reports InvalidRequest"),
		UPaper2DPlusAnimationMapLibrary::GetComboChainLength(Profile, nullptr, ChainLength),
		EPaper2DPlusComboChainResult::InvalidRequest);
	TestEqual(TEXT("Null-flipbook InvalidRequest clears the length"), ChainLength, 0);

	ChainLength = 99;
	TestEqual(TEXT("Empty tags report InvalidRequest"),
		UPaper2DPlusAnimationMapLibrary::GetComboChainLengthByTags(
			Profile, FGameplayTagContainer(), ChainLength),
		EPaper2DPlusComboChainResult::InvalidRequest);
	TestEqual(TEXT("Empty-tags InvalidRequest clears the length"), ChainLength, 0);

	UPaperFlipbook* Foreign = NewObject<UPaperFlipbook>(Profile);
	ChainLength = 99;
	TestEqual(TEXT("A flipbook with no map entry reports NotFound"),
		UPaper2DPlusAnimationMapLibrary::GetComboChainLength(Profile, Foreign, ChainLength),
		EPaper2DPlusComboChainResult::NotFound);
	TestEqual(TEXT("NotFound clears the length"), ChainLength, 0);

	FGameplayTagContainer NoSuchTags;
	NoSuchTags.AddTag(ComboSpine_GroupAttack);
	ChainLength = 99;
	TestEqual(TEXT("Unmatched exact tags report NotFound"),
		UPaper2DPlusAnimationMapLibrary::GetComboChainLengthByTags(Profile, NoSuchTags, ChainLength),
		EPaper2DPlusComboChainResult::NotFound);
	TestEqual(TEXT("Tag NotFound clears the length"), ChainLength, 0);

	ChainLength = 99;
	TestEqual(TEXT("A mid-chain member is not a chain key"),
		UPaper2DPlusAnimationMapLibrary::GetComboChainLength(Profile, Jab2, ChainLength),
		EPaper2DPlusComboChainResult::NotChainStart);
	TestEqual(TEXT("NotChainStart clears the length"), ChainLength, 0);

	// An opener flagged in TWO groups fails as AmbiguousChain and clears the output.
	ComboSpine_AddGroupEntry(Profile, SpecialGroup, TEXT("Jab"), /*bIsChainStart=*/true);
	ChainLength = 99;
	TestEqual(TEXT("An opener flagged in two groups reports AmbiguousChain"),
		UPaper2DPlusAnimationMapLibrary::GetComboChainLength(Profile, Jab, ChainLength),
		EPaper2DPlusComboChainResult::AmbiguousChain);
	TestEqual(TEXT("AmbiguousChain clears the length"), ChainLength, 0);
	return true;
}

// ─── Chain Tags: the authored chain-identity container resolves ByTags with precedence ──────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusComboChainTagsLookupTest,
	"Paper2DPlus.ComboSpine.ChainTagsResolveByTagsWithPrecedence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusComboChainTagsLookupTest::RunTest(const FString& Parameters)
{
	const FGameplayTag AttackGroup = ComboSpine_GroupAttack;
	const FGameplayTag SpecialGroup = ComboSpine_GroupSpecial;

	FGameplayTagContainer ChainIdentity;
	ChainIdentity.AddTag(ComboSpine_ChainIdentity);
	FGameplayTagContainer OpenerOwnTags;
	OpenerOwnTags.AddTag(ComboSpine_ExactOpener);

	// The chain's identity container lives on the START entry and differs from every animation's own
	// tags — the chain is searchable independently of how its opener is tagged.
	UPaper2DPlusCharacterProfileAsset* Profile = NewObject<UPaper2DPlusCharacterProfileAsset>();
	UPaperFlipbook* Jab = ComboSpine_AddMove(Profile, TEXT("Jab"), OpenerOwnTags);
	UPaperFlipbook* Jab2 = ComboSpine_AddMove(Profile, TEXT("Jab2"));
	ComboSpine_AddGroupEntry(
		Profile, AttackGroup, TEXT("Jab"), /*bIsChainStart=*/true, /*bIsChainEnd=*/false, ChainIdentity);
	ComboSpine_AddGroupEntry(Profile, AttackGroup, TEXT("Jab2"));
	ComboSpine_AddTransition(Profile, TEXT("Jab"), TEXT("Jab2"));

	UPaperFlipbook* Step = nullptr;
	int32 ChainLength = 0;
	TestEqual(TEXT("The authored chain container resolves the chain"),
		UPaper2DPlusAnimationMapLibrary::GetComboChainFlipbookAtIndexByTags(
			Profile, ChainIdentity, 0, Step, ChainLength),
		EPaper2DPlusComboChainResult::Success);
	TestTrue(TEXT("Chain-container lookup keys the flagged opener"), Step == Jab);
	TestEqual(TEXT("Chain-container lookup reports the countable length"), ChainLength, 2);
	TestEqual(TEXT("The length node resolves by chain container too"),
		UPaper2DPlusAnimationMapLibrary::GetComboChainLengthByTags(Profile, ChainIdentity, ChainLength),
		EPaper2DPlusComboChainResult::Success);
	TestEqual(TEXT("Chain-container length matches"), ChainLength, 2);

	// The opener's own tags still fall back (no chain container equals THIS query).
	TestEqual(TEXT("Opener own-tags fallback still resolves"),
		UPaper2DPlusAnimationMapLibrary::GetComboChainFlipbookAtIndexByTags(
			Profile, OpenerOwnTags, 1, Step, ChainLength),
		EPaper2DPlusComboChainResult::Success);
	TestTrue(TEXT("Fallback walks the same chain"), Step == Jab2);

	// PRECEDENCE: an animation whose OWN tags equal the query loses to a chain container match.
	// Decoy carries {ChainIdentity} as its own AnimationTags but starts no chain; the container on
	// Jab's start entry wins, so the query keys Jab's chain — never the Decoy animation.
	UPaperFlipbook* Decoy = ComboSpine_AddMove(Profile, TEXT("Decoy"), ChainIdentity);
	TestEqual(TEXT("A colliding own-tags animation does not shadow the chain container"),
		UPaper2DPlusAnimationMapLibrary::GetComboChainFlipbookAtIndexByTags(
			Profile, ChainIdentity, 0, Step, ChainLength),
		EPaper2DPlusComboChainResult::Success);
	TestTrue(TEXT("The chain container wins the collision — opener, not Decoy"),
		Step == Jab && Step != Decoy);

	// Two Chain Starts authored with EQUAL containers fail closed as AmbiguousInput.
	UPaperFlipbook* AltRoot = ComboSpine_AddMove(Profile, TEXT("AltRoot"));
	ComboSpine_AddGroupEntry(
		Profile, SpecialGroup, TEXT("AltRoot"), /*bIsChainStart=*/true, /*bIsChainEnd=*/false, ChainIdentity);
	Step = AltRoot;
	ChainLength = 99;
	TestEqual(TEXT("Two chains with equal containers report AmbiguousInput"),
		UPaper2DPlusAnimationMapLibrary::GetComboChainFlipbookAtIndexByTags(
			Profile, ChainIdentity, 0, Step, ChainLength),
		EPaper2DPlusComboChainResult::AmbiguousInput);
	TestNull(TEXT("AmbiguousInput clears the flipbook"), Step);
	TestEqual(TEXT("AmbiguousInput clears the length"), ChainLength, 0);
	TestEqual(TEXT("The length node fails the same ambiguity closed"),
		UPaper2DPlusAnimationMapLibrary::GetComboChainLengthByTags(Profile, ChainIdentity, ChainLength),
		EPaper2DPlusComboChainResult::AmbiguousInput);

	// A DIFFERENT container on the second start keeps both chains independently addressable.
	FGameplayTagContainer AltIdentity;
	AltIdentity.AddTag(ComboSpine_ChainIdentityAlt);
	Profile->TagMappings[SpecialGroup].Entries[0].ChainTags = AltIdentity;
	TestEqual(TEXT("Distinct containers resolve their own chains"),
		UPaper2DPlusAnimationMapLibrary::GetComboChainFlipbookAtIndexByTags(
			Profile, AltIdentity, 0, Step, ChainLength),
		EPaper2DPlusComboChainResult::Success);
	TestTrue(TEXT("The alternate container keys the alternate opener"), Step == AltRoot);
	return true;
}

#endif // WITH_EDITOR
