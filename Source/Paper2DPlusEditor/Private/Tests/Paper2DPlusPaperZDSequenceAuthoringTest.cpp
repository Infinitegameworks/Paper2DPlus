// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#if WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "PaperZDSequenceAuthoring.h"
#include "Paper2DPlusCharacterProfileAsset.h"

#include "PaperFlipbook.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusPaperZDSequenceAuthoringTest,
	"Paper2DPlus.PaperZD.SequenceAuthoring.OptionalAvailabilityAndScopedPlanning",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusPaperZDSequenceAuthoringTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlus::PaperZDSequenceAuthoring;

	TestFalse(
		TEXT("an absent or disabled plugin hides optional PaperZD authoring"),
		EvaluateOptionalAvailability(
			false,
			UPaper2DPlusCharacterProfileAsset::StaticClass(),
			UPaperFlipbook::StaticClass()));
	TestFalse(
		TEXT("a missing reflected source class fails closed"),
		EvaluateOptionalAvailability(
			true,
			nullptr,
			UPaperFlipbook::StaticClass()));
	TestFalse(
		TEXT("a missing reflected sequence class fails closed"),
		EvaluateOptionalAvailability(
			true,
			UPaper2DPlusCharacterProfileAsset::StaticClass(),
			nullptr));
	TestFalse(
		TEXT("unrelated reflected classes fail the PaperZD schema gate"),
		EvaluateOptionalAvailability(
			true,
			UPaper2DPlusCharacterProfileAsset::StaticClass(),
			UPaperFlipbook::StaticClass()));
	UClass* ReflectedAnimationSourceClass = ResolveAnimationSourceClass();
	UClass* ReflectedSequenceClass = ResolveFlipbookSequenceClass();
	if (ReflectedAnimationSourceClass && ReflectedSequenceClass)
	{
		TestTrue(
			TEXT("installed PaperZD classes with the expected schema expose authoring"),
			EvaluateOptionalAvailability(
				true,
				ReflectedAnimationSourceClass,
				ReflectedSequenceClass));
	}
	TestEqual(
		TEXT("default names include the profile prefix once"),
		BuildDefaultSequenceName(TEXT("Hero"), TEXT("Attack"), true),
		FString(TEXT("Hero_Attack")));
	TestEqual(
		TEXT("an existing profile prefix is not duplicated"),
		BuildDefaultSequenceName(TEXT("Hero"), TEXT("hero_Attack"), true),
		FString(TEXT("hero_Attack")));
	TestEqual(
		TEXT("the prefix can be disabled"),
		BuildDefaultSequenceName(TEXT("Hero"), TEXT("Attack"), false),
		FString(TEXT("Attack")));
	UPaper2DPlusCharacterProfileAsset* Profile =
		NewObject<UPaper2DPlusCharacterProfileAsset>(
			GetTransientPackage(),
			TEXT("ScopedPaperZDProfile"),
			RF_Transactional);
	UPaperFlipbook* Idle =
		NewObject<UPaperFlipbook>(Profile, TEXT("Idle"));
	UPaperFlipbook* Attack =
		NewObject<UPaperFlipbook>(Profile, TEXT("Attack"));
	UPaperFlipbook* Run =
		NewObject<UPaperFlipbook>(Profile, TEXT("Run"));

	for (UPaperFlipbook* Flipbook : { Idle, Attack, Run })
	{
		FFlipbookProfileEntry& Entry = Profile->Flipbooks.AddDefaulted_GetRef();
		Entry.Identity.FlipbookName = Flipbook->GetName();
		Entry.Identity.Flipbook = Flipbook;
	}

	TArray<UPaperFlipbook*> Scope = { Attack };
	TMap<UPaperFlipbook*, UObject*> ResolvedSequences;
	TArray<TSharedPtr<FPendingSequence>> PendingSequences;
	GatherSequenceWork(
		*Profile,
		false,
		Scope,
		ResolvedSequences,
		PendingSequences);

	TestEqual(
		TEXT("a restricted Bulk scope plans only the successfully produced flipbook"),
		PendingSequences.Num(),
		1);
	if (PendingSequences.Num() == 1)
	{
		TestTrue(
			TEXT("the pending row retains the scoped flipbook identity"),
			PendingSequences[0]->Flipbook.Get() == Attack);
		TestEqual(
			TEXT("the pending row uses the final profile and flipbook names"),
			PendingSequences[0]->SequenceName,
			FString(TEXT("ScopedPaperZDProfile_Attack")));
	}

	Scope.Reset();
	GatherSequenceWork(
		*Profile,
		false,
		Scope,
		ResolvedSequences,
		PendingSequences);
	TestEqual(
		TEXT("an explicitly empty Bulk success scope plans no work"),
		PendingSequences.Num(),
		0);

	UObject* FirstSequence =
		NewObject<UPaperFlipbook>(Profile, TEXT("FirstSequence"));
	UObject* ConflictingSequence =
		NewObject<UPaperFlipbook>(Profile, TEXT("ConflictingSequence"));
	TestTrue(
		TEXT("the first assignment fills the missing profile reference"),
		AssignSequenceToMissingReferences(*Profile, Attack, FirstSequence));
	TestFalse(
		TEXT("a later assignment never overwrites an authored reference"),
		AssignSequenceToMissingReferences(*Profile, Attack, ConflictingSequence));
	TestTrue(
		TEXT("the original sequence remains authoritative"),
		Profile->Flipbooks[1].Identity.PaperZDSequence == FirstSequence);

	return true;
}

#endif // WITH_EDITOR
