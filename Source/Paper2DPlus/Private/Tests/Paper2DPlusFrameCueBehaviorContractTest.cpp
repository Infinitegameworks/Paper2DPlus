// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "FrameCues/Paper2DPlusFrameCue.h"
#include "Tests/Paper2DPlusTestFrameCueTypes.h"
#include "UObject/UnrealType.h"

namespace Paper2DPlusCueBehaviorContractTest
{
	const UFunction* FindDeclaredEvent(const UClass* CueClass, const TCHAR* EventName)
	{
		return CueClass ? CueClass->FindFunctionByName(FName(EventName)) : nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCueBehaviorEventDeclarationTest,
	"Paper2DPlus.FrameCues.Behavior.DeclaredEvents",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCueBehaviorEventDeclarationTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusCueBehaviorContractTest;

	const UFunction* MomentTrigger = FindDeclaredEvent(
		UPaper2DPlusCue::StaticClass(), TEXT("OnCueTriggered"));
	if (!TestNotNull(TEXT("Cue declares On Cue Triggered"), MomentTrigger))
	{
		return false;
	}
	TestTrue(TEXT("On Cue Triggered is a Blueprint-implementable event"),
		MomentTrigger->HasAnyFunctionFlags(FUNC_BlueprintEvent));
	TestTrue(TEXT("On Cue Triggered is declared by the native Cue base"),
		MomentTrigger->GetOwnerClass() == UPaper2DPlusCue::StaticClass()
			&& MomentTrigger->GetOwnerClass()->HasAnyClassFlags(CLASS_Native));

	for (const TCHAR* RangeEventName : { TEXT("OnCueBegin"), TEXT("OnCueUpdate"), TEXT("OnCueEnd") })
	{
		const UFunction* RangeEvent = FindDeclaredEvent(
			UPaper2DPlusCueState::StaticClass(), RangeEventName);
		if (!TestNotNull(
			*FString::Printf(TEXT("Cue State declares %s"), RangeEventName), RangeEvent))
		{
			continue;
		}
		TestTrue(
			*FString::Printf(TEXT("%s is a Blueprint-implementable event"), RangeEventName),
			RangeEvent->HasAnyFunctionFlags(FUNC_BlueprintEvent));
	}

	// A Cue never gains the range lifecycle, and a Cue State never gains the moment one.
	TestNull(TEXT("Cue does not declare On Cue Begin"),
		FindDeclaredEvent(UPaper2DPlusCue::StaticClass(), TEXT("OnCueBegin")));
	TestNull(TEXT("Cue State does not declare On Cue Triggered"),
		FindDeclaredEvent(UPaper2DPlusCueState::StaticClass(), TEXT("OnCueTriggered")));

	TestEqual(TEXT("Moment behavior registry lists exactly its declared event"),
		Paper2DPlusFrameCueBehavior::GetMomentCueEventNames().Num(), 1);
	TestEqual(TEXT("Range behavior registry lists exactly its declared events"),
		Paper2DPlusFrameCueBehavior::GetRangeCueEventNames().Num(), 3);
	TestTrue(TEXT("Registry recognizes a declared event name"),
		Paper2DPlusFrameCueBehavior::IsDeclaredEventName(TEXT("OnCueEnd")));
	TestFalse(TEXT("Registry rejects an undeclared name"),
		Paper2DPlusFrameCueBehavior::IsDeclaredEventName(TEXT("SmuggledCustomEvent")));

	const TArray<FName> MomentEvents =
		Paper2DPlusFrameCueBehavior::GetDeclaredEventNamesForClass(
			UPaper2DPlusTestMomentCue::StaticClass());
	TestTrue(TEXT("A Moment subclass inherits the moment behavior surface"),
		MomentEvents.Num() == 1 && MomentEvents[0] == FName(TEXT("OnCueTriggered")));
	const TArray<FName> RangeSeeds =
		Paper2DPlusFrameCueBehavior::GetSeedEventNamesForClass(
			UPaper2DPlusTestRangeCue::StaticClass());
	TestTrue(TEXT("Range seeding covers the guaranteed Begin/End pair only"),
		RangeSeeds.Num() == 2
			&& RangeSeeds.Contains(FName(TEXT("OnCueBegin")))
			&& RangeSeeds.Contains(FName(TEXT("OnCueEnd"))));
	TestTrue(TEXT("Behavior registry returns nothing for a non-Cue class"),
		Paper2DPlusFrameCueBehavior::GetDeclaredEventNamesForClass(
			UObject::StaticClass()).Num() == 0);

#if WITH_EDITOR
	const UPaper2DPlusCue* CueCDO = GetDefault<UPaper2DPlusCue>();
	if (TestNotNull(TEXT("Cue class default object"), CueCDO))
	{
		TestTrue(TEXT("Cue Types advertise native GetWorld support to Blueprint world-context nodes"),
			CueCDO->ImplementsGetWorld());
		TestNull(TEXT("The Cue class default object never resolves a dispatch world"),
			CueCDO->GetWorld());
	}
#endif

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCueBehaviorDefaultImplementationTest,
	"Paper2DPlus.FrameCues.Behavior.EmptyNativeDefaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCueBehaviorDefaultImplementationTest::RunTest(const FString& Parameters)
{
	// A Cue Type that implements nothing must be inert: the native defaults run and do nothing, so
	// U2's dispatch seam can call them unconditionally without a per-placement capability check.
	UPaper2DPlusTestMomentCue* MomentCue = NewObject<UPaper2DPlusTestMomentCue>();
	UPaper2DPlusTestRangeCue* RangeCue = NewObject<UPaper2DPlusTestRangeCue>();
	if (!TestNotNull(TEXT("Moment fixture"), MomentCue)
		|| !TestNotNull(TEXT("Range fixture"), RangeCue))
	{
		return false;
	}

	FPaper2DPlusFrameCueContext Context = FPaper2DPlusFrameCueContext::MakePreview(2, 1, false);
	Context.Phase = EPaper2DPlusFrameCuePhase::Trigger;
	MomentCue->OnCueTriggered_Implementation(Context);
	Context.Phase = EPaper2DPlusFrameCuePhase::Begin;
	RangeCue->OnCueBegin_Implementation(Context);
	Context.Phase = EPaper2DPlusFrameCuePhase::Update;
	RangeCue->OnCueUpdate_Implementation(Context);
	Context.Phase = EPaper2DPlusFrameCuePhase::End;
	Context.EndReason = EPaper2DPlusFrameCueEndReason::Completed;
	RangeCue->OnCueEnd_Implementation(Context);

	TestEqual(TEXT("Empty behavior defaults leave the moment placement untouched"),
		MomentCue->TriggerFrame, 0);
	TestEqual(TEXT("Empty behavior defaults leave the range placement untouched"),
		RangeCue->StartFrame, 0);
	TestTrue(TEXT("Preview context still reports the editor-preview branch to behavior"),
		Context.bIsEditorPreview);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCueContextEvaluationModeContractTest,
	"Paper2DPlus.FrameCues.Context.EvaluationModeAndAppendedPins",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCueContextEvaluationModeContractTest::RunTest(const FString& Parameters)
{
	FPaper2DPlusFrameCueContext Context;
	TestEqual(TEXT("A default context remains runtime playback"),
		Context.EvaluationMode,
		EPaper2DPlusFrameCueEvaluationMode::RuntimePlayback);
	TestFalse(TEXT("Default runtime playback is not editor preview"), Context.bIsEditorPreview);
	TestFalse(TEXT("Default runtime playback is not catch-up"), Context.bIsCatchUp);

	Context.SetEvaluationMode(EPaper2DPlusFrameCueEvaluationMode::RuntimeCatchUp);
	TestFalse(TEXT("Runtime catch-up is not editor preview"), Context.bIsEditorPreview);
	TestTrue(TEXT("Runtime catch-up derives the legacy catch-up flag"), Context.bIsCatchUp);

	Context.SetEvaluationMode(EPaper2DPlusFrameCueEvaluationMode::EditorScrubSeek);
	TestTrue(TEXT("Editor scrub derives the legacy preview flag"), Context.bIsEditorPreview);
	TestFalse(TEXT("Editor scrub clears the legacy catch-up flag"), Context.bIsCatchUp);

	const FPaper2DPlusFrameCueContext Preview =
		FPaper2DPlusFrameCueContext::MakePreview(4, 3, false);
	TestEqual(TEXT("The compatibility preview factory defaults to editor playback"),
		Preview.EvaluationMode,
		EPaper2DPlusFrameCueEvaluationMode::EditorPlayback);
	TestTrue(TEXT("The compatibility preview factory derives preview"), Preview.bIsEditorPreview);
	TestFalse(TEXT("The compatibility preview factory clears catch-up"), Preview.bIsCatchUp);

	const FPaper2DPlusFrameCueContext Selection =
		FPaper2DPlusFrameCueContext::MakePreview(
			4,
			3,
			false,
			EPaper2DPlusFrameCueEvaluationMode::EditorSelectionPreview);
	TestEqual(TEXT("The preview factory preserves an explicit editor mode"),
		Selection.EvaluationMode,
		EPaper2DPlusFrameCueEvaluationMode::EditorSelectionPreview);

	TArray<FName> PropertyNames;
	for (TFieldIterator<FProperty> It(FPaper2DPlusFrameCueContext::StaticStruct()); It; ++It)
	{
		PropertyNames.Add(It->GetFName());
	}
	const TArray<FName> ExpectedPropertyNames = {
		TEXT("OwningActor"),
		TEXT("ProfileComponent"),
		TEXT("Flipbook"),
		TEXT("AnimationName"),
		TEXT("CurrentFrame"),
		TEXT("PreviousFrame"),
		TEXT("Phase"),
		TEXT("EndReason"),
		TEXT("bWasLoopWrap"),
		TEXT("bIsCompressed"),
		TEXT("bIsEditorPreview"),
		TEXT("NetContext"),
		TEXT("bIsCatchUp"),
		TEXT("CharacterProfile"),
		TEXT("PlaybackComponent"),
		TEXT("EvaluationMode")
	};
	TestEqual(TEXT("Context keeps every legacy pin and appends the three spatial fields"),
		PropertyNames,
		ExpectedPropertyNames);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCueBehaviorDirectExecutionTest,
	"Paper2DPlus.FrameCues.Behavior.ExecuteCueBehaviorMapsPhaseToDeclaredEvent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCueBehaviorDirectExecutionTest::RunTest(const FString& Parameters)
{
	// ExecuteCueBehavior is the single mapping from (Moment/Range, phase) to the declared behavior
	// event, shared by game dispatch and editor preview. Every OTHER behavior test drives it through
	// HandleFrameChanged, which cannot distinguish "the seam is right" from "the component's dispatch
	// happens to compensate". This is the one direct caller, so the seam keeps explicit coverage.
	//
	// It also pins the worldless direct-call case on purpose. Runtime and editor hosts normally supply
	// an actor/component, but the shared helper remains safe for pure lifecycle tests and callers that
	// deliberately have no world context.
	Paper2DPlusBehaviorTestLog::Reset();

	FPaper2DPlusFrameCueContext Preview = FPaper2DPlusFrameCueContext::MakePreview(0, -1, false);

	UPaper2DPlusTestBehaviorMomentCue* const Moment = NewObject<UPaper2DPlusTestBehaviorMomentCue>();
	Moment->Payload = 3;
	UPaper2DPlusTestBehaviorRangeCue* const Range = NewObject<UPaper2DPlusTestBehaviorRangeCue>();
	Range->Payload = 9;

	Preview.Phase = EPaper2DPlusFrameCuePhase::Trigger;
	Paper2DPlusFrameCueBehavior::ExecuteCueBehavior(*Moment, Preview);
	// A Cue State has no Trigger event, and a Cue has no range lifecycle: the mismatched pairs
	// must fall through silently rather than run the wrong event or assert.
	Paper2DPlusFrameCueBehavior::ExecuteCueBehavior(*Range, Preview);

	Preview.Phase = EPaper2DPlusFrameCuePhase::Begin;
	Paper2DPlusFrameCueBehavior::ExecuteCueBehavior(*Range, Preview);
	Paper2DPlusFrameCueBehavior::ExecuteCueBehavior(*Moment, Preview);

	Preview.Phase = EPaper2DPlusFrameCuePhase::Update;
	Paper2DPlusFrameCueBehavior::ExecuteCueBehavior(*Range, Preview);

	Preview.Phase = EPaper2DPlusFrameCuePhase::End;
	Preview.EndReason = EPaper2DPlusFrameCueEndReason::Completed;
	Paper2DPlusFrameCueBehavior::ExecuteCueBehavior(*Range, Preview);

	const TArray<Paper2DPlusBehaviorTestLog::FRecord>& Log = Paper2DPlusBehaviorTestLog::Records();
	TestEqual(TEXT("Only the four matching phase/lifecycle pairs ran"), Log.Num(), 4);
	if (Log.Num() == 4)
	{
		TestTrue(TEXT("Trigger reached the Moment placement"),
			Log[0].Cue == Moment && Log[0].Phase == EPaper2DPlusFrameCuePhase::Trigger);
		TestTrue(TEXT("Begin reached the Range placement"),
			Log[1].Cue == Range && Log[1].Phase == EPaper2DPlusFrameCuePhase::Begin);
		TestTrue(TEXT("Update reached the Range placement"),
			Log[2].Cue == Range && Log[2].Phase == EPaper2DPlusFrameCuePhase::Update);
		TestTrue(TEXT("End reached the Range placement with its reason"),
			Log[3].Cue == Range
				&& Log[3].Phase == EPaper2DPlusFrameCuePhase::End
				&& Log[3].EndReason == EPaper2DPlusFrameCueEndReason::Completed);
		TestTrue(TEXT("Every execution read the placement's own authored payload"),
			Log[0].Payload == 3 && Log[1].Payload == 9);
		TestTrue(TEXT("The preview context reaches behavior flagged as preview"),
			Log[0].bIsEditorPreview);
		TestNull(TEXT("A worldless direct context remains worldless inside behavior"),
			Log[0].CueWorld);
	}
	TestNull(TEXT("A shared Cue placement has no world outside behavior dispatch"),
		Moment->GetWorld());

	// An unresolvable placement is skipped rather than run: the same guard dispatch relies on.
	Paper2DPlusBehaviorTestLog::Reset();
	UPaper2DPlusTestBehaviorMomentCue* const Orphaned = NewObject<UPaper2DPlusTestBehaviorMomentCue>();
	Orphaned->MarkAsGarbage();
	Preview.Phase = EPaper2DPlusFrameCuePhase::Trigger;
	Paper2DPlusFrameCueBehavior::ExecuteCueBehavior(*Orphaned, Preview);
	TestEqual(TEXT("An unresolvable placement runs no behavior through the direct seam"),
		Paper2DPlusBehaviorTestLog::Records().Num(), 0);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
