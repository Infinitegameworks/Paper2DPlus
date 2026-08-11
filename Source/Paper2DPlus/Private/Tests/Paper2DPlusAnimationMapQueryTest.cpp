// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#if WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "NativeGameplayTags.h"
#include "Paper2DPlusAnimationMapLibrary.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Paper2DPlusMoveTransition.h"
#include "Paper2DPlusTypes.h"
#include "PaperFlipbook.h"
#include "PaperSprite.h"

UE_DEFINE_GAMEPLAY_TAG_STATIC(
	AMQuery_GroupAttack,
	"Paper2DPlus.Test.AnimationMapQuery.Group.Attack")
UE_DEFINE_GAMEPLAY_TAG_STATIC(
	AMQuery_PhaseActive,
	"Paper2DPlus.Test.AnimationMapQuery.Phase.Active")
UE_DEFINE_GAMEPLAY_TAG_STATIC(
	AMQuery_PhaseRecovery,
	"Paper2DPlus.Test.AnimationMapQuery.Phase.Recovery")

/** Focused worldless coverage for the chain-start opener discovery -> transition-inspection pipeline. */
namespace
{
	UPaperFlipbook* AMQuery_MakeFlipbook(UObject* Owner, int32 NumFrames)
	{
		UPaperFlipbook* Flipbook = NewObject<UPaperFlipbook>(Owner);
		FScopedFlipbookMutator Mutator(Flipbook);
		Mutator.FramesPerSecond = 10.0f;
		Mutator.KeyFrames.Empty();
		for (int32 Index = 0; Index < NumFrames; ++Index)
		{
			FPaperFlipbookKeyFrame KeyFrame;
			KeyFrame.FrameRun = 1;
			KeyFrame.Sprite = NewObject<UPaperSprite>(Flipbook);
			Mutator.KeyFrames.Add(KeyFrame);
		}
		return Flipbook;
	}

	UPaperFlipbook* AMQuery_AddMove(
		UPaper2DPlusCharacterProfileAsset* Asset,
		const FString& MoveName,
		const FGameplayTag& Phase = FGameplayTag())
	{
		FFlipbookProfileEntry Animation;
		Animation.Identity.FlipbookName = MoveName;
		Animation.Identity.Flipbook = AMQuery_MakeFlipbook(Asset, 4);
		Animation.EditorMeta.PhaseTag = Phase;
		Animation.CombatData.Frames.SetNum(4);
		Animation.CombatData.FrameExtractionInfo.SetNum(4);
		Asset->Flipbooks.Add(Animation);
		return Animation.Identity.Flipbook.Get();
	}

	void AMQuery_AddGroupMember(
		UPaper2DPlusCharacterProfileAsset* Asset,
		const FGameplayTag& GroupTag,
		const FString& MoveName,
		bool bIsChainStart = false)
	{
		FFlipbookTagMappingEntry Entry(MoveName);
		Entry.bIsChainStart = bIsChainStart;
		Asset->TagMappings.FindOrAdd(GroupTag).Entries.Add(Entry);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusAMQueryTransitionMapTest,
	"Paper2DPlus.AnimationMapQuery.ComboOpenerTransitionInfo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusAMQueryTransitionMapTest::RunTest(const FString& Parameters)
{
	const FGameplayTag AttackGroup = AMQuery_GroupAttack;
	const FGameplayTag Active = AMQuery_PhaseActive;
	const FGameplayTag Recovery = AMQuery_PhaseRecovery;

	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	UPaperFlipbook* Jab = AMQuery_AddMove(Asset, TEXT("Jab"));
	UPaperFlipbook* Jab2 = AMQuery_AddMove(Asset, TEXT("Jab2"), Active);
	UPaperFlipbook* Launcher = AMQuery_AddMove(Asset, TEXT("Launcher"), Recovery);
	UPaperFlipbook* Foreign = AMQuery_MakeFlipbook(Asset, 2);

	AMQuery_AddGroupMember(Asset, AttackGroup, TEXT("Jab"), /*bIsChainStart=*/true);
	AMQuery_AddGroupMember(Asset, AttackGroup, TEXT("Jab2"));
	AMQuery_AddGroupMember(Asset, AttackGroup, TEXT("Launcher"));
	AMQuery_AddGroupMember(Asset, AttackGroup, TEXT("GhostMove")); // dangling member is ignored

	FFlipbookTransitionData& Transitions = Asset->Flipbooks[0].TransitionData;
	Transitions.Transitions.Add(FPaper2DPlusMoveTransition(TEXT("Jab2")));
	Transitions.Transitions.Add(FPaper2DPlusMoveTransition(TEXT("launcher")));
	Transitions.Transitions.Add(FPaper2DPlusMoveTransition(TEXT("GhostMove")));
	Transitions.Transitions.Add(FPaper2DPlusMoveTransition(FString()));
	Transitions.Transitions.Add(FPaper2DPlusMoveTransition(TEXT("JAB2"))); // duplicate target

	const TArray<UPaperFlipbook*> Openers =
		UPaper2DPlusAnimationMapLibrary::GetComboOpenerFlipbooks(Asset);
	TestEqual(TEXT("Exactly one flagged chain start is discovered (dangling members are ignored)"),
		Openers.Num(), 1);
	TestTrue(TEXT("Opener discovery returns the flagged opener flipbook directly"),
		Openers.Num() == 1 && Openers[0] == Jab);

	FPaper2DPlusAnimationSelectionCriteria Criteria;
	FPaper2DPlusAnimationTransitionInfo Info;
	TestTrue(TEXT("Flipbook-keyed Jab transition info resolves"),
		UPaper2DPlusAnimationMapLibrary::GetAnimationTransitionInfo(Asset, Jab, Criteria, Info));
	TestTrue(TEXT("At least one valid direct target exists"), Info.bHasTransitions);
	TestTrue(TEXT("Valid targets expose phases"), Info.bHasPhases);
	TestEqual(TEXT("Duplicate and invalid rows do not duplicate phase choices"), Info.AvailablePhases.Num(), 2);
	if (Info.AvailablePhases.Num() == 2)
	{
		TestEqual(TEXT("Phase order follows first valid transition rows"), Info.AvailablePhases[0], Active);
		TestEqual(TEXT("Second phase is Recovery"), Info.AvailablePhases[1], Recovery);
	}

	UPaperFlipbook* Resolved = nullptr;
	TestEqual(TEXT("Active selects the exact Jab2 transition"),
		UPaper2DPlusAnimationMapLibrary::ResolveAnimationTransition(
			Asset, Jab, Active, Criteria, Resolved),
		EPaper2DPlusAnimationResolveResult::Success);
	TestTrue(TEXT("Resolved Active target is Jab2"), Resolved == Jab2);
	TestEqual(TEXT("Recovery selects the case-insensitive Launcher transition"),
		UPaper2DPlusAnimationMapLibrary::ResolveAnimationTransition(
			Asset, Jab, Recovery, Criteria, Resolved),
		EPaper2DPlusAnimationResolveResult::Success);
	TestTrue(TEXT("Resolved Recovery target is Launcher"), Resolved == Launcher);

	TestFalse(TEXT("A flipbook with no map entry fails cleanly"),
		UPaper2DPlusAnimationMapLibrary::GetAnimationTransitionInfo(Asset, Foreign, Criteria, Info));
	TestFalse(TEXT("Null flipbook fails cleanly"),
		UPaper2DPlusAnimationMapLibrary::GetAnimationTransitionInfo(Asset, nullptr, Criteria, Info));
	TestTrue(TEXT("A target with no outgoing rows still has valid info"),
		UPaper2DPlusAnimationMapLibrary::GetAnimationTransitionInfo(Asset, Jab2, Criteria, Info));
	TestFalse(TEXT("A target with no outgoing rows reports no transitions"), Info.bHasTransitions);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusAMQueryIndependentIncomingPhaseTest,
	"Paper2DPlus.AnimationMapQuery.TransitionPhasesAreIndependentPerEdge",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusAMQueryIndependentIncomingPhaseTest::RunTest(const FString& Parameters)
{
	const FGameplayTag Active = AMQuery_PhaseActive;
	const FGameplayTag Recovery = AMQuery_PhaseRecovery;

	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	UPaperFlipbook* FromA = AMQuery_AddMove(Asset, TEXT("FromA"));
	UPaperFlipbook* FromB = AMQuery_AddMove(Asset, TEXT("FromB"));
	UPaperFlipbook* SharedTarget = AMQuery_AddMove(Asset, TEXT("SharedTarget"), Active);

	FPaper2DPlusMoveTransition OverrideRow(TEXT("SharedTarget"));
	OverrideRow.PhaseTagOverride = Recovery;
	Asset->Flipbooks[0].TransitionData.Transitions.Add(OverrideRow);
	Asset->Flipbooks[1].TransitionData.Transitions.Add(FPaper2DPlusMoveTransition(TEXT("SharedTarget")));

	FPaper2DPlusAnimationSelectionCriteria Criteria;
	FPaper2DPlusAnimationTransitionInfo Info;
	TestTrue(TEXT("The first source exposes its transition-owned override"),
		UPaper2DPlusAnimationMapLibrary::GetAnimationTransitionInfo(Asset, FromA, Criteria, Info));
	TestEqual(TEXT("The first source has one available phase"), Info.AvailablePhases.Num(), 1);
	if (Info.AvailablePhases.Num() == 1)
	{
		TestEqual(TEXT("The row override wins over the shared target phase"),
			Info.AvailablePhases[0], Recovery);
	}

	TestTrue(TEXT("The second source still resolves its inherited target phase"),
		UPaper2DPlusAnimationMapLibrary::GetAnimationTransitionInfo(Asset, FromB, Criteria, Info));
	TestEqual(TEXT("The second source has one available phase"), Info.AvailablePhases.Num(), 1);
	if (Info.AvailablePhases.Num() == 1)
	{
		TestEqual(TEXT("An unset row inherits the target animation phase for compatibility"),
			Info.AvailablePhases[0], Active);
	}

	UPaperFlipbook* Resolved = nullptr;
	TestEqual(TEXT("The override phase resolves only the first incoming transition"),
		UPaper2DPlusAnimationMapLibrary::ResolveAnimationTransition(
			Asset, FromA, Recovery, Criteria, Resolved),
		EPaper2DPlusAnimationResolveResult::Success);
	TestTrue(TEXT("The overridden edge still resolves the shared target"), Resolved == SharedTarget);
	TestEqual(TEXT("The inherited phase resolves the second incoming transition"),
		UPaper2DPlusAnimationMapLibrary::ResolveAnimationTransition(
			Asset, FromB, Active, Criteria, Resolved),
		EPaper2DPlusAnimationResolveResult::Success);
	TestTrue(TEXT("The inherited edge resolves the same shared target"), Resolved == SharedTarget);
	TestEqual(TEXT("Authoring the first edge never mutates the target animation phase"),
		Asset->Flipbooks[2].EditorMeta.PhaseTag, Active);
	return true;
}

#endif // WITH_EDITOR
