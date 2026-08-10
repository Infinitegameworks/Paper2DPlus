// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "FrameCues/Paper2DPlusFrameCue.h"
#include "FrameCues/Paper2DPlusFrameCueDispatch.h"
#include "GameFramework/Actor.h"
#include "Misc/ScopeExit.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Paper2DPlusCharacterProfileComponent.h"
#include "PaperFlipbook.h"
#include "PaperFlipbookComponent.h"
#include "Sound/SoundWave.h"
#include "Tests/Paper2DPlusTestFrameCueTypes.h"

/*
 * Runtime Cue behavior execution.
 *
 * Behavior rides the SAME notification seam the listener broadcast uses, so these tests drive the
 * production component funnels (HandleFrameChanged, the animation boundary, the teardown force-end,
 * the unequip sweep) and assert what the placement's own declared events observed.
 */
namespace Paper2DPlusCueBehaviorRuntimeTest
{
	struct FCueBehaviorRig
	{
		TObjectPtr<AActor> Actor = nullptr;
		TObjectPtr<UPaperFlipbook> Flipbook = nullptr;
		TObjectPtr<UPaper2DPlusCharacterProfileAsset> Profile = nullptr;
		TObjectPtr<UPaperFlipbookComponent> FlipbookComponent = nullptr;
		TObjectPtr<UPaper2DPlusCharacterProfileComponent> ProfileComponent = nullptr;
		TObjectPtr<UPaper2DPlusFrameCueRecorder> Recorder = nullptr;
	};

	/** Builds one actor/component pair. Passing an existing profile shares its placements verbatim. */
	FCueBehaviorRig CueBehavior_MakeRig(
		const TArray<UPaper2DPlusCueBase*>& Cues,
		UPaper2DPlusCharacterProfileAsset* SharedProfile = nullptr,
		UPaperFlipbook* SharedFlipbook = nullptr)
	{
		FCueBehaviorRig Rig;
		Rig.Actor = NewObject<AActor>();
		Rig.Flipbook = SharedFlipbook ? SharedFlipbook : NewObject<UPaperFlipbook>();

		if (SharedProfile)
		{
			Rig.Profile = SharedProfile;
		}
		else
		{
			Rig.Profile = NewObject<UPaper2DPlusCharacterProfileAsset>();
			FFlipbookProfileEntry Entry;
			Entry.Identity.FlipbookName = TEXT("Attack");
			Entry.Identity.Flipbook = Rig.Flipbook;
			for (UPaper2DPlusCueBase* Cue : Cues)
			{
				Entry.FrameEventData.FrameCues.Add(Cue);
			}
			Rig.Profile->Flipbooks.Add(MoveTemp(Entry));
		}

		Rig.FlipbookComponent = NewObject<UPaperFlipbookComponent>(Rig.Actor);
		Rig.ProfileComponent = NewObject<UPaper2DPlusCharacterProfileComponent>(Rig.Actor);
		Rig.Recorder = NewObject<UPaper2DPlusFrameCueRecorder>();
		Rig.Actor->AddOwnedComponent(Rig.FlipbookComponent);
		Rig.Actor->AddOwnedComponent(Rig.ProfileComponent);
		Rig.FlipbookComponent->SetFlipbook(Rig.Flipbook);
		Rig.ProfileComponent->CharacterProfile = Rig.Profile;
		Rig.ProfileComponent->FlipbookComponent = Rig.FlipbookComponent;
		Rig.ProfileComponent->OnFrameCue.AddDynamic(
			Rig.Recorder.Get(), &UPaper2DPlusFrameCueRecorder::OnCue);
		Rig.ProfileComponent->HandleFlipbookChanged(Rig.Flipbook);
		return Rig;
	}

	/** Returns the authored placement array the component dispatches from. */
	TArray<TObjectPtr<UPaper2DPlusCueBase>>& CueBehavior_Placements(
		UPaper2DPlusCharacterProfileAsset& Profile)
	{
		return Profile.Flipbooks[0].FrameEventData.FrameCues;
	}

	int32 CueBehavior_CountFor(const UPaper2DPlusCueBase* Cue, EPaper2DPlusFrameCuePhase Phase)
	{
		int32 Count = 0;
		for (const Paper2DPlusBehaviorTestLog::FRecord& Record : Paper2DPlusBehaviorTestLog::Records())
		{
			if (Record.Cue == Cue && Record.Phase == Phase)
			{
				++Count;
			}
		}
		return Count;
	}

	/** How many LISTENER broadcasts named this placement with this phase. */
	int32 CueBehavior_ListenerCountFor(
		const UPaper2DPlusFrameCueRecorder& Recorder,
		const UPaper2DPlusCueBase* Cue,
		EPaper2DPlusFrameCuePhase Phase)
	{
		int32 Count = 0;
		const int32 Num = FMath::Min(Recorder.Cues.Num(), Recorder.Contexts.Num());
		for (int32 Index = 0; Index < Num; ++Index)
		{
			if (Recorder.Cues[Index].Get() == Cue && Recorder.Contexts[Index].Phase == Phase)
			{
				++Count;
			}
		}
		return Count;
	}

	/**
	 * Simulates the Cue Type class dying under a live placement: the reinstancing shape a Content
	 * Browser delete or a recompile during play leaves behind. Scoped so the flag never outlives the
	 * scenario, whatever the test does in between.
	 */
	struct FCueBehaviorScopedOrphanedClass
	{
		explicit FCueBehaviorScopedOrphanedClass(UClass* InClass)
			: Class(InClass)
		{
			if (Class && !Class->HasAnyClassFlags(CLASS_NewerVersionExists))
			{
				Class->ClassFlags |= CLASS_NewerVersionExists;
				bApplied = true;
			}
		}

		~FCueBehaviorScopedOrphanedClass()
		{
			if (bApplied && Class)
			{
				Class->ClassFlags &= ~CLASS_NewerVersionExists;
			}
		}

		FCueBehaviorScopedOrphanedClass(const FCueBehaviorScopedOrphanedClass&) = delete;
		FCueBehaviorScopedOrphanedClass& operator=(const FCueBehaviorScopedOrphanedClass&) = delete;

		UClass* Class = nullptr;
		bool bApplied = false;
	};

	/** The end reason the last recorded End carried, or None when behavior recorded no End at all. */
	EPaper2DPlusFrameCueEndReason CueBehavior_LastEndReason()
	{
		EPaper2DPlusFrameCueEndReason Reason = EPaper2DPlusFrameCueEndReason::None;
		for (const Paper2DPlusBehaviorTestLog::FRecord& Record : Paper2DPlusBehaviorTestLog::Records())
		{
			if (Record.Phase == EPaper2DPlusFrameCuePhase::End)
			{
				Reason = Record.EndReason;
			}
		}
		return Reason;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCueBehaviorRuntimeLifecycleTest,
	"Paper2DPlus.FrameCues.Behavior.RuntimeLifecycleRunsBeforeListeners",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCueBehaviorRuntimeLifecycleTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusCueBehaviorRuntimeTest;
	Paper2DPlusBehaviorTestLog::Reset();

	UPaper2DPlusTestBehaviorMomentCue* Moment = NewObject<UPaper2DPlusTestBehaviorMomentCue>();
	Moment->TriggerFrame = 1;
	Moment->Payload = 11;
	UPaper2DPlusTestBehaviorRangeCue* Range = NewObject<UPaper2DPlusTestBehaviorRangeCue>();
	Range->StartFrame = 2;
	Range->FrameCount = 2;
	Range->bEmitUpdates = true;
	Range->Payload = 22;

	FCueBehaviorRig Rig = CueBehavior_MakeRig({Moment, Range});
	Rig.ProfileComponent->HandleFrameChanged(0);
	Rig.ProfileComponent->HandleFrameChanged(1);
	Rig.ProfileComponent->HandleFrameChanged(2);
	Rig.ProfileComponent->HandleFrameChanged(3);
	Rig.ProfileComponent->HandleFrameChanged(4);

	const TArray<Paper2DPlusBehaviorTestLog::FRecord>& Log = Paper2DPlusBehaviorTestLog::Records();
	TestEqual(TEXT("Behavior receives one notification per admitted lifecycle phase"),
		Log.Num(), Rig.Recorder->Contexts.Num());
	TestEqual(TEXT("Moment behavior runs exactly once"),
		CueBehavior_CountFor(Moment, EPaper2DPlusFrameCuePhase::Trigger), 1);
	TestEqual(TEXT("Range behavior begins exactly once"),
		CueBehavior_CountFor(Range, EPaper2DPlusFrameCuePhase::Begin), 1);
	TestEqual(TEXT("Range behavior ends exactly once"),
		CueBehavior_CountFor(Range, EPaper2DPlusFrameCuePhase::End), 1);
	TestTrue(TEXT("Range behavior receives its opted-in updates"),
		CueBehavior_CountFor(Range, EPaper2DPlusFrameCuePhase::Update) > 0);

	if (Log.Num() > 0)
	{
		TestEqual(TEXT("Behavior reads its own placement payload"), Log[0].Payload, 11);
		TestTrue(TEXT("Behavior reads the dispatching component from the context"),
			Log[0].ProfileComponent == Rig.ProfileComponent.Get());
		TestTrue(TEXT("Behavior reads the owning actor from the context"),
			Log[0].OwningActor == Rig.Actor.Get());
		TestTrue(TEXT("Behavior reads the exact Character Profile from the context"),
			Log[0].CharacterProfile == Rig.Profile.Get());
		TestTrue(TEXT("Behavior reads the timeline-driving component from the context"),
			Log[0].PlaybackComponent == Rig.FlipbookComponent.Get());
		TestTrue(TEXT("Behavior reads the exact flipbook from the context"),
			Log[0].Flipbook == Rig.Flipbook.Get());
		TestEqual(TEXT("Behavior reads the animation identity from the context"),
			Log[0].AnimationName, FName(TEXT("Attack")));
		TestEqual(TEXT("Game dispatch identifies runtime playback explicitly"),
			Log[0].EvaluationMode, EPaper2DPlusFrameCueEvaluationMode::RuntimePlayback);
		TestFalse(TEXT("Game dispatch does not claim editor preview"), Log[0].bIsEditorPreview);
	}

	// Behavior-before-listener: every listener call already saw its own notification's behavior record.
	for (int32 Index = 0; Index < Rig.Recorder->BehaviorLogSizesAtBroadcast.Num(); ++Index)
	{
		TestEqual(
			*FString::Printf(TEXT("Notification %d ran behavior before the listener broadcast"), Index),
			Rig.Recorder->BehaviorLogSizesAtBroadcast[Index], Index + 1);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCueBehaviorExplicitStopReportTest,
	"Paper2DPlus.FrameCues.Behavior.ExplicitStopReportIsIdempotent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCueBehaviorExplicitStopReportTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusCueBehaviorRuntimeTest;
	Paper2DPlusBehaviorTestLog::Reset();

	UPaper2DPlusTestBehaviorRangeCue* Range = NewObject<UPaper2DPlusTestBehaviorRangeCue>();
	Range->StartFrame = 0;
	Range->FrameCount = 5;

	FCueBehaviorRig Rig = CueBehavior_MakeRig({Range});
	Rig.ProfileComponent->HandleFrameChanged(0);
	const int64 Generation = Rig.ProfileComponent->GetFrameCuePlaybackGeneration();
	const bool bWasPlaying = Rig.FlipbookComponent->IsPlaying();

	TestTrue(TEXT("A live playback generation is exposed to external timeline owners"),
		Generation > 0);
	TestTrue(TEXT("The first expected-source stop report owns the terminal boundary"),
		Rig.ProfileComponent->ReportFrameCuePlaybackStopped(
			Rig.FlipbookComponent.Get(), Generation));
	TestFalse(TEXT("A repeated stop report for the same generation is rejected"),
		Rig.ProfileComponent->ReportFrameCuePlaybackStopped(
			Rig.FlipbookComponent.Get(), Generation));
	Rig.ProfileComponent->HandleFlipbookChanged(NewObject<UPaperFlipbook>());

	TestEqual(TEXT("The explicit report pairs exactly one End"),
		CueBehavior_CountFor(Range, EPaper2DPlusFrameCuePhase::End), 1);
	TestEqual(TEXT("The explicit report uses PlaybackStopped"),
		CueBehavior_LastEndReason(), EPaper2DPlusFrameCueEndReason::PlaybackStopped);
	TestFalse(TEXT("The report leaves no active range"),
		Rig.ProfileComponent->IsRangeCueActiveForTests(Range));
	TestEqual(TEXT("The report does not take ownership of stopping external playback"),
		Rig.FlipbookComponent->IsPlaying(), bWasPlaying);

	const TArray<Paper2DPlusBehaviorTestLog::FRecord>& Log =
		Paper2DPlusBehaviorTestLog::Records();
	if (Log.Num() > 0)
	{
		const Paper2DPlusBehaviorTestLog::FRecord& End = Log.Last();
		TestEqual(TEXT("The terminal behavior record is End"),
			End.Phase, EPaper2DPlusFrameCuePhase::End);
		TestTrue(TEXT("The terminal retains the outgoing Profile"),
			End.CharacterProfile == Rig.Profile.Get());
		TestTrue(TEXT("The terminal retains the outgoing playback component"),
			End.PlaybackComponent == Rig.FlipbookComponent.Get());
		TestTrue(TEXT("The terminal retains the outgoing flipbook"),
			End.Flipbook == Rig.Flipbook.Get());
		TestEqual(TEXT("The terminal retains the last usable source frame"),
			End.CurrentFrame, 0);
		TestEqual(TEXT("The terminal retains the outgoing evaluation mode"),
			End.EvaluationMode, EPaper2DPlusFrameCueEvaluationMode::RuntimePlayback);
	}

	const int32 EndListenerIndex = Rig.Recorder->Contexts.IndexOfByPredicate(
		[Range](const FPaper2DPlusFrameCueContext& Context)
		{
			return Context.Phase == EPaper2DPlusFrameCuePhase::End;
		});
	if (EndListenerIndex != INDEX_NONE)
	{
		TestEqual(TEXT("Terminal behavior runs before its listener"),
			Rig.Recorder->BehaviorLogSizesAtBroadcast[EndListenerIndex],
			Paper2DPlusBehaviorTestLog::Records().Num());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCueBehaviorForceEndPairingTest,
	"Paper2DPlus.FrameCues.Behavior.ForceEndBoundariesPairEveryBegin",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCueBehaviorForceEndPairingTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusCueBehaviorRuntimeTest;

	// 1. Animation change.
	{
		Paper2DPlusBehaviorTestLog::Reset();
		UPaper2DPlusTestBehaviorRangeCue* Range = NewObject<UPaper2DPlusTestBehaviorRangeCue>();
		Range->StartFrame = 0;
		Range->FrameCount = 5;
		FCueBehaviorRig Rig = CueBehavior_MakeRig({Range});
		Rig.ProfileComponent->HandleFrameChanged(0);
		TestEqual(TEXT("Animation-change fixture begins once"),
			CueBehavior_CountFor(Range, EPaper2DPlusFrameCuePhase::Begin), 1);

		Rig.ProfileComponent->HandleFlipbookChanged(NewObject<UPaperFlipbook>());
		TestEqual(TEXT("Animation change ends the behavior range exactly once"),
			CueBehavior_CountFor(Range, EPaper2DPlusFrameCuePhase::End), 1);
		TestEqual(TEXT("Animation change reports its end reason to behavior"),
			CueBehavior_LastEndReason(), EPaper2DPlusFrameCueEndReason::AnimationChanged);
		TestFalse(TEXT("No Begin is leaked after the animation change"),
			Rig.ProfileComponent->IsRangeCueActiveForTests(Range));
	}

	// 2. Component teardown (the EndPlay/ComponentDestroyed funnel).
	{
		Paper2DPlusBehaviorTestLog::Reset();
		UPaper2DPlusTestBehaviorRangeCue* Range = NewObject<UPaper2DPlusTestBehaviorRangeCue>();
		Range->StartFrame = 0;
		Range->FrameCount = 5;
		FCueBehaviorRig Rig = CueBehavior_MakeRig({Range});
		Rig.ProfileComponent->HandleFrameChanged(0);
		Rig.ProfileComponent->ForceEndActiveRangeCuesForTests(
			EPaper2DPlusFrameCueEndReason::ComponentDestroyed);

		TestEqual(TEXT("Teardown ends the behavior range exactly once"),
			CueBehavior_CountFor(Range, EPaper2DPlusFrameCuePhase::End), 1);
		TestEqual(TEXT("Teardown reports its end reason to behavior"),
			CueBehavior_LastEndReason(), EPaper2DPlusFrameCueEndReason::ComponentDestroyed);
		TestFalse(TEXT("No Begin is leaked after component teardown"),
			Rig.ProfileComponent->IsRangeCueActiveForTests(Range));
	}

	// 3. The unequip-immediate End-only sweep: the placement leaves the dispatch view while active.
	{
		Paper2DPlusBehaviorTestLog::Reset();
		UPaper2DPlusTestBehaviorRangeCue* Range = NewObject<UPaper2DPlusTestBehaviorRangeCue>();
		Range->StartFrame = 0;
		Range->FrameCount = 5;
		FCueBehaviorRig Rig = CueBehavior_MakeRig({Range});
		Rig.ProfileComponent->HandleFrameChanged(0);
		CueBehavior_Placements(*Rig.Profile).Reset();
		Rig.ProfileComponent->SweepStaleFrameCueRangesForTests();

		TestEqual(TEXT("The unequip sweep ends the behavior range exactly once"),
			CueBehavior_CountFor(Range, EPaper2DPlusFrameCuePhase::End), 1);
		TestEqual(TEXT("The unequip sweep reports source removal to behavior"),
			CueBehavior_LastEndReason(), EPaper2DPlusFrameCueEndReason::SourceRemoved);
		TestEqual(TEXT("The End-only sweep never re-begins the range"),
			CueBehavior_CountFor(Range, EPaper2DPlusFrameCuePhase::Begin), 1);
		TestFalse(TEXT("No Begin is leaked after the unequip sweep"),
			Rig.ProfileComponent->IsRangeCueActiveForTests(Range));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCueBehaviorClassOrphanForceEndTest,
	"Paper2DPlus.FrameCues.Behavior.ClassOrphanedRangeStillReceivesForceEnd",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCueBehaviorClassOrphanForceEndTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusCueBehaviorRuntimeTest;

	// Deleting or recompiling a Cue Type while one of its ranges is active leaves the placement alive
	// with an unresolvable class. The receiver already acted on the Begin, so every teardown boundary
	// must still deliver the paired End; only BEHAVIOR is suppressed, because it can no longer resolve.
	// This is the same state the frame-change stale sweep ends, so the two funnels must agree.

	// 1. Component teardown (EndPlay / ComponentDestroyed).
	{
		Paper2DPlusBehaviorTestLog::Reset();
		UPaper2DPlusTestBehaviorRangeCue* Range = NewObject<UPaper2DPlusTestBehaviorRangeCue>();
		Range->StartFrame = 0;
		Range->FrameCount = 5;
		FCueBehaviorRig Rig = CueBehavior_MakeRig({Range});
		Rig.ProfileComponent->HandleFrameChanged(0);
		TestEqual(TEXT("The orphan-teardown fixture begins once for its listener"),
			CueBehavior_ListenerCountFor(*Rig.Recorder, Range, EPaper2DPlusFrameCuePhase::Begin), 1);

		const int32 BehaviorCallsBeforeTeardown = Paper2DPlusBehaviorTestLog::Records().Num();
		{
			FCueBehaviorScopedOrphanedClass Orphan(Range->GetClass());
			Rig.ProfileComponent->ForceEndActiveRangeCuesForTests(
				EPaper2DPlusFrameCueEndReason::ComponentDestroyed);
		}

		TestEqual(TEXT("Teardown still pairs the End for a class-orphaned range"),
			CueBehavior_ListenerCountFor(*Rig.Recorder, Range, EPaper2DPlusFrameCuePhase::End), 1);
		const int32 EndIndex = Rig.Recorder->Contexts.Num() - 1;
		if (Rig.Recorder->Contexts.IsValidIndex(EndIndex))
		{
			TestEqual(TEXT("The orphan teardown End carries the teardown reason"),
				Rig.Recorder->Contexts[EndIndex].EndReason,
				EPaper2DPlusFrameCueEndReason::ComponentDestroyed);
		}
		TestEqual(TEXT("An unresolvable placement runs no behavior on the way out"),
			Paper2DPlusBehaviorTestLog::Records().Num(), BehaviorCallsBeforeTeardown);
		TestFalse(TEXT("Teardown clears the class-orphaned active range"),
			Rig.ProfileComponent->IsRangeCueActiveForTests(Range));
	}

	// 2. Animation change (HandleFlipbookChanged), the second force-end boundary on the same funnel.
	{
		Paper2DPlusBehaviorTestLog::Reset();
		UPaper2DPlusTestBehaviorRangeCue* Range = NewObject<UPaper2DPlusTestBehaviorRangeCue>();
		Range->StartFrame = 0;
		Range->FrameCount = 5;
		FCueBehaviorRig Rig = CueBehavior_MakeRig({Range});
		Rig.ProfileComponent->HandleFrameChanged(0);

		{
			FCueBehaviorScopedOrphanedClass Orphan(Range->GetClass());
			Rig.ProfileComponent->HandleFlipbookChanged(NewObject<UPaperFlipbook>());
		}

		TestEqual(TEXT("An animation change still pairs the End for a class-orphaned range"),
			CueBehavior_ListenerCountFor(*Rig.Recorder, Range, EPaper2DPlusFrameCuePhase::End), 1);
		TestFalse(TEXT("The animation change clears the class-orphaned active range"),
			Rig.ProfileComponent->IsRangeCueActiveForTests(Range));
	}

	// 3. The frame-change stale sweep — the funnel the force-end paths must stay consistent with.
	{
		Paper2DPlusBehaviorTestLog::Reset();
		UPaper2DPlusTestBehaviorRangeCue* Range = NewObject<UPaper2DPlusTestBehaviorRangeCue>();
		Range->StartFrame = 0;
		Range->FrameCount = 5;
		FCueBehaviorRig Rig = CueBehavior_MakeRig({Range});
		Rig.ProfileComponent->HandleFrameChanged(0);

		{
			FCueBehaviorScopedOrphanedClass Orphan(Range->GetClass());
			Rig.ProfileComponent->HandleFrameChanged(1);
		}

		TestEqual(TEXT("The stale sweep ends the class-orphaned range as source-removed"),
			CueBehavior_ListenerCountFor(*Rig.Recorder, Range, EPaper2DPlusFrameCuePhase::End), 1);
		const int32 EndIndex = Rig.Recorder->Contexts.Num() - 1;
		if (Rig.Recorder->Contexts.IsValidIndex(EndIndex))
		{
			TestEqual(TEXT("The stale-sweep End reports source removal"),
				Rig.Recorder->Contexts[EndIndex].EndReason,
				EPaper2DPlusFrameCueEndReason::SourceRemoved);
		}
		TestFalse(TEXT("The stale sweep clears the class-orphaned active range"),
			Rig.ProfileComponent->IsRangeCueActiveForTests(Range));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCueBehaviorCompressedTraversalTest,
	"Paper2DPlus.FrameCues.Behavior.CompressedTraversalDeliversWholeLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCueBehaviorCompressedTraversalTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusCueBehaviorRuntimeTest;
	Paper2DPlusBehaviorTestLog::Reset();

	UPaper2DPlusTestBehaviorRangeCue* Range = NewObject<UPaper2DPlusTestBehaviorRangeCue>();
	Range->StartFrame = 1;
	Range->FrameCount = 2;
	Range->bEmitUpdates = true;

	FCueBehaviorRig Rig = CueBehavior_MakeRig({Range});
	Rig.ProfileComponent->HandleFrameChanged(0);
	Rig.ProfileComponent->HandleFrameChanged(4); // one transition crosses the whole range

	const TArray<Paper2DPlusBehaviorTestLog::FRecord>& Log = Paper2DPlusBehaviorTestLog::Records();
	if (!TestEqual(TEXT("A compressed crossing delivers Begin, Update and End to behavior"),
		Log.Num(), 3))
	{
		return false;
	}
	TestEqual(TEXT("Compressed behavior starts with Begin"),
		Log[0].Phase, EPaper2DPlusFrameCuePhase::Begin);
	TestEqual(TEXT("Compressed behavior keeps its Update"),
		Log[1].Phase, EPaper2DPlusFrameCuePhase::Update);
	TestEqual(TEXT("Compressed behavior finishes with End"),
		Log[2].Phase, EPaper2DPlusFrameCuePhase::End);
	for (int32 Index = 0; Index < Log.Num(); ++Index)
	{
		TestTrue(
			*FString::Printf(TEXT("Compressed phase %d is flagged compressed for behavior"), Index),
			Log[Index].bIsCompressed);
	}
	TestFalse(TEXT("A fully crossed range never stays active"),
		Rig.ProfileComponent->IsRangeCueActiveForTests(Range));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCueBehaviorLoopSeamTest,
	"Paper2DPlus.FrameCues.Behavior.LoopSeamKeepsBehaviorContinuous",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCueBehaviorLoopSeamTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusCueBehaviorRuntimeTest;
	Paper2DPlusBehaviorTestLog::Reset();

	UPaper2DPlusTestBehaviorRangeCue* Range = NewObject<UPaper2DPlusTestBehaviorRangeCue>();
	Range->StartFrame = 0;
	Range->FrameCount = 5;
	Range->bEmitUpdates = true;

	FCueBehaviorRig Rig = CueBehavior_MakeRig({Range});
	Rig.ProfileComponent->HandleFrameChanged(0);
	Rig.ProfileComponent->HandleFrameChanged(3);
	Rig.ProfileComponent->HandleFrameChanged(1); // loop wrap back into the same range

	TestEqual(TEXT("A range spanning the loop seam begins once"),
		CueBehavior_CountFor(Range, EPaper2DPlusFrameCuePhase::Begin), 1);
	TestEqual(TEXT("A range spanning the loop seam never flickers an End"),
		CueBehavior_CountFor(Range, EPaper2DPlusFrameCuePhase::End), 0);
	TestEqual(TEXT("The loop seam keeps delivering updates to behavior"),
		CueBehavior_CountFor(Range, EPaper2DPlusFrameCuePhase::Update), 3);
	TestTrue(TEXT("The range stays active across the loop seam"),
		Rig.ProfileComponent->IsRangeCueActiveForTests(Range));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCueBehaviorMidDispatchMutationTest,
	"Paper2DPlus.FrameCues.Behavior.PlaybackMutationFromBehaviorDefers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCueBehaviorMidDispatchMutationTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusCueBehaviorRuntimeTest;
	Paper2DPlusBehaviorTestLog::Reset();

	UPaper2DPlusTestBehaviorRangeCue* Range = NewObject<UPaper2DPlusTestBehaviorRangeCue>();
	Range->StartFrame = 0;
	Range->FrameCount = 3;
	UPaper2DPlusTestBehaviorMomentCue* Mutator = NewObject<UPaper2DPlusTestBehaviorMomentCue>();
	Mutator->TriggerFrame = 1;

	FCueBehaviorRig Rig = CueBehavior_MakeRig({Range, Mutator});

	UPaperFlipbook* OtherFlipbook = NewObject<UPaperFlipbook>();
	FFlipbookProfileEntry OtherEntry;
	OtherEntry.Identity.FlipbookName = TEXT("Other");
	OtherEntry.Identity.Flipbook = OtherFlipbook;
	Rig.Profile->Flipbooks.Add(MoveTemp(OtherEntry));
	Rig.Profile->InvalidateFlipbookLookupCache();
	// Growing the animation array moved the entries, so re-warm the component's cached view of them.
	Rig.ProfileComponent->HandleFlipbookChanged(Rig.Flipbook);
	Mutator->MutateToFlipbook = OtherFlipbook;

	Rig.ProfileComponent->HandleFrameChanged(0);
	Rig.ProfileComponent->HandleFrameChanged(1);

	TestEqual(TEXT("Behavior that switches animation runs exactly once (no re-entry)"),
		CueBehavior_CountFor(Mutator, EPaper2DPlusFrameCuePhase::Trigger), 1);
	TestTrue(TEXT("The deferred switch is drained after the dispatch completes"),
		Rig.FlipbookComponent->GetFlipbook() == OtherFlipbook);
	TestEqual(TEXT("The interrupted range receives exactly one End"),
		CueBehavior_CountFor(Range, EPaper2DPlusFrameCuePhase::End), 1);
	TestEqual(TEXT("Behavior-driven playback mutation ends the range as an animation change"),
		CueBehavior_LastEndReason(), EPaper2DPlusFrameCueEndReason::AnimationChanged);
	TestFalse(TEXT("Behavior-driven mutation leaks no active range"),
		Rig.ProfileComponent->IsRangeCueActiveForTests(Range));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCueBehaviorReentrantTerminalOrderingTest,
	"Paper2DPlus.FrameCues.Behavior.ReentrantTerminalOrderingFirstBoundaryWins",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCueBehaviorReentrantTerminalOrderingTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusCueBehaviorRuntimeTest;

	const auto RunOrderingScenario =
		[this](
			const bool bStopBeforeFlipbookChange,
			const EPaper2DPlusFrameCueEndReason ExpectedEndReason,
			const TCHAR* Scenario)
		{
			Paper2DPlusBehaviorTestLog::Reset();

			UPaper2DPlusTestBehaviorRangeCue* Range =
				NewObject<UPaper2DPlusTestBehaviorRangeCue>();
			Range->StartFrame = 0;
			Range->FrameCount = 3;
			UPaper2DPlusTestBehaviorMomentCue* Mutator =
				NewObject<UPaper2DPlusTestBehaviorMomentCue>();
			Mutator->TriggerFrame = 1;

			FCueBehaviorRig Rig = CueBehavior_MakeRig({Range, Mutator});
			UPaperFlipbook* TargetFlipbook = NewObject<UPaperFlipbook>();
			FFlipbookProfileEntry TargetEntry;
			TargetEntry.Identity.FlipbookName = TEXT("Target");
			TargetEntry.Identity.Flipbook = TargetFlipbook;
			Rig.Profile->Flipbooks.Add(MoveTemp(TargetEntry));
			Rig.Profile->InvalidateFlipbookLookupCache();
			Rig.ProfileComponent->HandleFlipbookChanged(Rig.Flipbook);

			Mutator->MutateToFlipbook = TargetFlipbook;
			Mutator->bReportPlaybackStopBeforeMutation = bStopBeforeFlipbookChange;
			Mutator->bReportPlaybackStopAfterMutation = !bStopBeforeFlipbookChange;

			Rig.ProfileComponent->HandleFrameChanged(0);
			Rig.ProfileComponent->HandleFrameChanged(1);

			TestTrue(
				*FString::Printf(TEXT("%s commits the target flipbook after outer dispatch"), Scenario),
				Rig.FlipbookComponent->GetFlipbook() == TargetFlipbook);
			TestEqual(
				*FString::Printf(TEXT("%s emits exactly one terminal behavior"), Scenario),
				CueBehavior_CountFor(Range, EPaper2DPlusFrameCuePhase::End), 1);
			TestEqual(
				*FString::Printf(TEXT("%s preserves the first terminal reason"), Scenario),
				CueBehavior_LastEndReason(), ExpectedEndReason);
			TestFalse(
				*FString::Printf(TEXT("%s leaves no active range"), Scenario),
				Rig.ProfileComponent->IsRangeCueActiveForTests(Range));

			int32 MutatorListenerIndex = INDEX_NONE;
			int32 EndListenerIndex = INDEX_NONE;
			const int32 NumListeners =
				FMath::Min(Rig.Recorder->Cues.Num(), Rig.Recorder->Contexts.Num());
			for (int32 Index = 0; Index < NumListeners; ++Index)
			{
				const EPaper2DPlusFrameCuePhase Phase = Rig.Recorder->Contexts[Index].Phase;
				if (Rig.Recorder->Cues[Index].Get() == Mutator
					&& Phase == EPaper2DPlusFrameCuePhase::Trigger)
				{
					MutatorListenerIndex = Index;
				}
				if (Rig.Recorder->Cues[Index].Get() == Range
					&& Phase == EPaper2DPlusFrameCuePhase::End)
				{
					EndListenerIndex = Index;
				}
			}
			TestEqual(
				*FString::Printf(TEXT("%s broadcasts exactly one terminal listener"), Scenario),
				CueBehavior_ListenerCountFor(
					*Rig.Recorder,
					Range,
					EPaper2DPlusFrameCuePhase::End),
				1);
			TestTrue(
				*FString::Printf(TEXT("%s drains after the outer notification"), Scenario),
				MutatorListenerIndex != INDEX_NONE
					&& EndListenerIndex > MutatorListenerIndex);
			if (EndListenerIndex != INDEX_NONE)
			{
				TestEqual(
					*FString::Printf(TEXT("%s runs terminal behavior before its listener"), Scenario),
					Rig.Recorder->BehaviorLogSizesAtBroadcast[EndListenerIndex],
					Paper2DPlusBehaviorTestLog::Records().Num());
			}
		};

	RunOrderingScenario(
		/*bStopBeforeFlipbookChange=*/true,
		EPaper2DPlusFrameCueEndReason::PlaybackStopped,
		TEXT("Stop-report then flipbook-change"));
	RunOrderingScenario(
		/*bStopBeforeFlipbookChange=*/false,
		EPaper2DPlusFrameCueEndReason::AnimationChanged,
		TEXT("Flipbook-change then stop-report"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCueBehaviorTerminalDrainRejectsReentrantBeginTest,
	"Paper2DPlus.FrameCues.Behavior.TerminalDrainRejectsReentrantExternalBegin",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCueBehaviorTerminalDrainRejectsReentrantBeginTest::RunTest(
	const FString& Parameters)
{
	using namespace Paper2DPlusCueBehaviorRuntimeTest;
	Paper2DPlusBehaviorTestLog::Reset();

	UPaper2DPlusTestBehaviorRangeCue* Range =
		NewObject<UPaper2DPlusTestBehaviorRangeCue>();
	Range->StartFrame = 0;
	Range->FrameCount = 3;
	FCueBehaviorRig Rig = CueBehavior_MakeRig({Range});

	UPaper2DPlusFrameCueMutationReceiver* Receiver =
		NewObject<UPaper2DPlusFrameCueMutationReceiver>();
	Receiver->TriggerCue = Range;
	Receiver->TriggerPhase = EPaper2DPlusFrameCuePhase::End;
	Receiver->bBeginExternalPlayback = true;
	Rig.ProfileComponent->OnFrameCue.AddDynamic(
		Receiver, &UPaper2DPlusFrameCueMutationReceiver::OnCue);

	Rig.ProfileComponent->HandleFrameChanged(0);
	const int64 OutgoingGeneration = Rig.ProfileComponent->GetFrameCuePlaybackGeneration();
	TestTrue(TEXT("The outgoing generation is open"), OutgoingGeneration > 0);
	TestTrue(
		TEXT("The explicit terminal report is accepted"),
		Rig.ProfileComponent->ReportFrameCuePlaybackStopped(
			Rig.FlipbookComponent.Get(), OutgoingGeneration));

	TestEqual(TEXT("The terminal listener attempts one nested external Begin"),
		Receiver->MutationCount, 1);
	TestEqual(TEXT("A nested Begin is rejected until every outgoing End has drained"),
		Receiver->ExternalBeginResult, int64(0));
	TestEqual(TEXT("The nested Begin cannot advance the generation"),
		Rig.ProfileComponent->GetFrameCuePlaybackGeneration(), OutgoingGeneration);
	TestEqual(TEXT("The outgoing range receives exactly one End"),
		CueBehavior_CountFor(Range, EPaper2DPlusFrameCuePhase::End), 1);
	TestFalse(TEXT("The outgoing terminal leaves no active range"),
		Rig.ProfileComponent->IsRangeCueActiveForTests(Range));

	const int64 ReplacementGeneration =
		Rig.ProfileComponent->BeginExternalFrameCuePlayback(Rig.FlipbookComponent.Get());
	TestTrue(TEXT("A replacement generation can begin after the drain completes"),
		ReplacementGeneration > 0 && ReplacementGeneration != OutgoingGeneration);
	Rig.ProfileComponent->HandleFrameChanged(0);
	TestTrue(TEXT("The replacement generation can establish its own range lifecycle"),
		Rig.ProfileComponent->IsRangeCueActiveForTests(Range));
	TestTrue(
		TEXT("The replacement generation closes through its own token"),
		Rig.ProfileComponent->ReportFrameCuePlaybackStopped(
			Rig.FlipbookComponent.Get(), ReplacementGeneration));
	TestEqual(TEXT("The replacement emits its own single paired End"),
		CueBehavior_CountFor(Range, EPaper2DPlusFrameCuePhase::End), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCueBehaviorProfileMutationContextTest,
	"Paper2DPlus.FrameCues.Behavior.ProfileMutationRetainsOutgoingContext",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCueBehaviorProfileMutationContextTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusCueBehaviorRuntimeTest;
	Paper2DPlusBehaviorTestLog::Reset();

	UPaper2DPlusTestBehaviorRangeCue* Range = NewObject<UPaper2DPlusTestBehaviorRangeCue>();
	Range->StartFrame = 0;
	Range->FrameCount = 3;
	UPaper2DPlusTestBehaviorMomentCue* MutatorCue =
		NewObject<UPaper2DPlusTestBehaviorMomentCue>();
	MutatorCue->TriggerFrame = 1;

	FCueBehaviorRig Rig = CueBehavior_MakeRig({Range, MutatorCue});
	UPaper2DPlusCharacterProfileAsset* ProfileA = Rig.Profile.Get();
	UPaperFlipbook* SharedFlipbook = Rig.Flipbook.Get();
	UPaperFlipbookComponent* PlaybackComponent = Rig.FlipbookComponent.Get();

	UPaper2DPlusCharacterProfileAsset* ProfileB =
		NewObject<UPaper2DPlusCharacterProfileAsset>();
	FFlipbookProfileEntry DestinationEntry;
	DestinationEntry.Identity.FlipbookName = TEXT("Attack");
	DestinationEntry.Identity.Flipbook = SharedFlipbook;
	ProfileB->Flipbooks.Add(MoveTemp(DestinationEntry));

	UPaper2DPlusFrameCueMutationReceiver* Receiver =
		NewObject<UPaper2DPlusFrameCueMutationReceiver>();
	Receiver->TriggerCue = MutatorCue;
	Receiver->TargetProfile = ProfileB;
	Rig.ProfileComponent->OnFrameCue.AddDynamic(
		Receiver, &UPaper2DPlusFrameCueMutationReceiver::OnCue);

	Rig.ProfileComponent->HandleFrameChanged(0);
	Rig.ProfileComponent->HandleFrameChanged(1);

	TestEqual(TEXT("The listener requests one Profile mutation"), Receiver->MutationCount, 1);
	TestTrue(TEXT("Profile B commits after the outgoing terminal drains"),
		Rig.ProfileComponent->CharacterProfile == ProfileB);
	TestTrue(TEXT("The same destination flipbook remains committed"),
		Rig.FlipbookComponent->GetFlipbook() == SharedFlipbook);
	TestEqual(TEXT("The outgoing range emits exactly one End"),
		CueBehavior_CountFor(Range, EPaper2DPlusFrameCuePhase::End), 1);
	TestFalse(TEXT("Profile replacement leaves no old active range"),
		Rig.ProfileComponent->IsRangeCueActiveForTests(Range));

	const Paper2DPlusBehaviorTestLog::FRecord* End =
		Paper2DPlusBehaviorTestLog::Records().FindByPredicate(
			[Range](const Paper2DPlusBehaviorTestLog::FRecord& Record)
			{
				return Record.Cue == Range
					&& Record.Phase == EPaper2DPlusFrameCuePhase::End;
			});
	if (TestNotNull(TEXT("The outgoing behavior receives its paired End"), End))
	{
		TestEqual(TEXT("The outgoing End is an animation change"),
			End->EndReason, EPaper2DPlusFrameCueEndReason::AnimationChanged);
		TestTrue(TEXT("The outgoing End retains Profile A"),
			End->CharacterProfile == ProfileA);
		TestTrue(TEXT("The outgoing End retains the old playback component"),
			End->PlaybackComponent == PlaybackComponent);
		TestTrue(TEXT("The outgoing End retains the old flipbook"),
			End->Flipbook == SharedFlipbook);
		TestEqual(TEXT("The outgoing End retains the last usable source frame"),
			End->CurrentFrame, 1);
		TestEqual(TEXT("The outgoing End retains runtime evaluation mode"),
			End->EvaluationMode, EPaper2DPlusFrameCueEvaluationMode::RuntimePlayback);
	}

	const int32 EndListenerIndex = Rig.Recorder->Contexts.IndexOfByPredicate(
		[](const FPaper2DPlusFrameCueContext& Context)
		{
			return Context.Phase == EPaper2DPlusFrameCuePhase::End;
		});
	if (EndListenerIndex != INDEX_NONE)
	{
		const FPaper2DPlusFrameCueContext& ListenerEnd =
			Rig.Recorder->Contexts[EndListenerIndex];
		TestTrue(TEXT("The listener End also retains Profile A"),
			ListenerEnd.CharacterProfile == ProfileA);
		TestTrue(TEXT("The listener End also retains the old playback component"),
			ListenerEnd.PlaybackComponent == PlaybackComponent);
		TestTrue(TEXT("The listener End also retains the old flipbook"),
			ListenerEnd.Flipbook == SharedFlipbook);
		TestEqual(TEXT("The listener End also retains the last usable source frame"),
			ListenerEnd.CurrentFrame, 1);
		TestEqual(TEXT("The listener End also retains runtime evaluation mode"),
			ListenerEnd.EvaluationMode,
			EPaper2DPlusFrameCueEvaluationMode::RuntimePlayback);
		TestEqual(TEXT("Profile-mutation End behavior runs before its listener"),
			Rig.Recorder->BehaviorLogSizesAtBroadcast[EndListenerIndex],
			Paper2DPlusBehaviorTestLog::Records().Num());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCueBehaviorRecompileSweepTest,
	"Paper2DPlus.FrameCues.Behavior.ReplacedPlacementEndsThenRebegins",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCueBehaviorRecompileSweepTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusCueBehaviorRuntimeTest;
	Paper2DPlusBehaviorTestLog::Reset();

	// A Cue Type recompile reinstances placements: the active object leaves the authored array and a
	// fresh instance takes its slot. The stale sweep must pair the old Begin before the new one runs.
	UPaper2DPlusTestBehaviorRangeCue* Original = NewObject<UPaper2DPlusTestBehaviorRangeCue>();
	Original->StartFrame = 0;
	Original->FrameCount = 5;
	Original->Payload = 1;

	FCueBehaviorRig Rig = CueBehavior_MakeRig({Original});
	Rig.ProfileComponent->HandleFrameChanged(0);
	TestEqual(TEXT("The pre-recompile placement begins"),
		CueBehavior_CountFor(Original, EPaper2DPlusFrameCuePhase::Begin), 1);

	UPaper2DPlusTestBehaviorRangeCue* Reinstanced = NewObject<UPaper2DPlusTestBehaviorRangeCue>();
	Reinstanced->StartFrame = 0;
	Reinstanced->FrameCount = 5;
	Reinstanced->Payload = 2;
	CueBehavior_Placements(*Rig.Profile)[0] = Reinstanced;

	Rig.ProfileComponent->HandleFrameChanged(1);

	TestEqual(TEXT("The replaced placement receives its paired End"),
		CueBehavior_CountFor(Original, EPaper2DPlusFrameCuePhase::End), 1);
	TestEqual(TEXT("The reinstanced placement begins fresh"),
		CueBehavior_CountFor(Reinstanced, EPaper2DPlusFrameCuePhase::Begin), 1);
	for (const Paper2DPlusBehaviorTestLog::FRecord& Record : Paper2DPlusBehaviorTestLog::Records())
	{
		if (Record.Cue == Original && Record.Phase == EPaper2DPlusFrameCuePhase::End)
		{
			TestEqual(TEXT("The replaced placement ends as source-removed"),
				Record.EndReason, EPaper2DPlusFrameCueEndReason::SourceRemoved);
		}
	}
	TestFalse(TEXT("The replaced placement is no longer active"),
		Rig.ProfileComponent->IsRangeCueActiveForTests(Original));
	TestTrue(TEXT("The reinstanced placement is the active one"),
		Rig.ProfileComponent->IsRangeCueActiveForTests(Reinstanced));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCueBehaviorPerPlacementPayloadTest,
	"Paper2DPlus.FrameCues.Behavior.EachPlacementReadsItsOwnPayload",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCueBehaviorPerPlacementPayloadTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusCueBehaviorRuntimeTest;
	Paper2DPlusBehaviorTestLog::Reset();

	UPaper2DPlusTestBehaviorMomentCue* Quiet = NewObject<UPaper2DPlusTestBehaviorMomentCue>();
	Quiet->TriggerFrame = 0;
	Quiet->Payload = 3;
	UPaper2DPlusTestBehaviorMomentCue* Loud = NewObject<UPaper2DPlusTestBehaviorMomentCue>();
	Loud->TriggerFrame = 0;
	Loud->Payload = 9;

	FCueBehaviorRig Rig = CueBehavior_MakeRig({Quiet, Loud});
	Rig.ProfileComponent->HandleFrameChanged(0);

	const TArray<Paper2DPlusBehaviorTestLog::FRecord>& Log = Paper2DPlusBehaviorTestLog::Records();
	if (!TestEqual(TEXT("Both placements of the same Cue Type execute"), Log.Num(), 2))
	{
		return false;
	}
	TestEqual(TEXT("The first placement's behavior reads its own payload"), Log[0].Payload, 3);
	TestEqual(TEXT("The second placement's behavior reads its own payload"), Log[1].Payload, 9);
	TestTrue(TEXT("Each record names the placement that ran"),
		Log[0].Cue == Quiet && Log[1].Cue == Loud);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCueBehaviorSharedPlacementTest,
	"Paper2DPlus.FrameCues.Behavior.SharedPlacementServesEveryComponent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCueBehaviorSharedPlacementTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusCueBehaviorRuntimeTest;
	Paper2DPlusBehaviorTestLog::Reset();

	// The statelessness pin: one asset-owned placement, two actors. Each dispatch must be attributed
	// to its own component, and neither component's range lifecycle may disturb the other's.
	UPaper2DPlusTestBehaviorRangeCue* Shared = NewObject<UPaper2DPlusTestBehaviorRangeCue>();
	Shared->StartFrame = 0;
	Shared->FrameCount = 3;
	Shared->Payload = 5;

	FCueBehaviorRig First = CueBehavior_MakeRig({Shared});
	FCueBehaviorRig Second = CueBehavior_MakeRig({}, First.Profile.Get(), First.Flipbook.Get());

	First.ProfileComponent->HandleFrameChanged(0);
	TestTrue(TEXT("The first component holds the shared range active"),
		First.ProfileComponent->IsRangeCueActiveForTests(Shared));
	TestFalse(TEXT("The second component is unaffected by the first component's Begin"),
		Second.ProfileComponent->IsRangeCueActiveForTests(Shared));

	Second.ProfileComponent->HandleFrameChanged(0);
	TestTrue(TEXT("The second component begins the same placement independently"),
		Second.ProfileComponent->IsRangeCueActiveForTests(Shared));

	First.ProfileComponent->HandleFrameChanged(4);
	TestFalse(TEXT("The first component ends its own lifecycle"),
		First.ProfileComponent->IsRangeCueActiveForTests(Shared));
	TestTrue(TEXT("The second component keeps its lifecycle across the first component's End"),
		Second.ProfileComponent->IsRangeCueActiveForTests(Shared));

	int32 FirstBegins = 0;
	int32 SecondBegins = 0;
	for (const Paper2DPlusBehaviorTestLog::FRecord& Record : Paper2DPlusBehaviorTestLog::Records())
	{
		if (Record.Phase != EPaper2DPlusFrameCuePhase::Begin)
		{
			continue;
		}
		TestEqual(TEXT("Every shared-placement execution reads the same payload"), Record.Payload, 5);
		FirstBegins += (Record.ProfileComponent == First.ProfileComponent.Get()) ? 1 : 0;
		SecondBegins += (Record.ProfileComponent == Second.ProfileComponent.Get()) ? 1 : 0;
	}
	TestEqual(TEXT("The first component's behavior ran once"), FirstBegins, 1);
	TestEqual(TEXT("The second component's behavior ran once"), SecondBegins, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCueBehaviorOrphanPlacementTest,
	"Paper2DPlus.FrameCues.Behavior.OrphanedPlacementIsSkippedAndReported",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCueBehaviorOrphanPlacementTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusCueBehaviorRuntimeTest;
	Paper2DPlusBehaviorTestLog::Reset();

	UPaper2DPlusTestBehaviorMomentCue* Healthy = NewObject<UPaper2DPlusTestBehaviorMomentCue>();
	Healthy->TriggerFrame = 0;
	Healthy->Payload = 4;
	UPaper2DPlusTestMomentCue* Orphaned = NewObject<UPaper2DPlusTestMomentCue>();
	Orphaned->TriggerFrame = 0;

	FCueBehaviorRig Rig = CueBehavior_MakeRig({Healthy, Orphaned});
	{
		FScopedFlipbookMutator Mutator(Rig.Flipbook);
		Mutator.KeyFrames.AddDefaulted();
	}
	Rig.Profile->Flipbooks[0].CombatData.Frames.SetNum(1);
	// Deleting a Cue Type leaves its placements unresolvable; a null slot is the other shape.
	CueBehavior_Placements(*Rig.Profile).Add(nullptr);
	FCueBehaviorScopedOrphanedClass OrphanedClass(Orphaned->GetClass());

	Rig.ProfileComponent->HandleFrameChanged(0);

	TestEqual(TEXT("A resolvable placement still dispatches"),
		CueBehavior_CountFor(Healthy, EPaper2DPlusFrameCuePhase::Trigger), 1);
	TestEqual(TEXT("An orphaned placement runs no behavior"),
		Paper2DPlusBehaviorTestLog::Records().Num(), 1);
	TestEqual(TEXT("An orphaned placement reaches no listener either"),
		Rig.Recorder->Contexts.Num(), 1);

	// Re-derived per shape rather than counted in bulk. The two shapes are reported with different
	// messages on purpose: the live stale-class object is an attributable orphan, while the null slot
	// accurately says its origin is ambiguous (never assigned or deleted Cue Type). The placement index
	// in the issue context is the stable key: FrameCue[1] is the orphan, FrameCue[2] the null slot.
	TArray<FCharacterProfileValidationIssue> Issues;
	Rig.Profile->ValidateCharacterProfileAsset(Issues);
	auto ErrorsForSlot = [&Issues](const TCHAR* SlotLabel)
	{
		return Issues.FilterByPredicate([SlotLabel](const FCharacterProfileValidationIssue& Issue)
		{
			return Issue.Severity == ECharacterProfileValidationSeverity::Error
				&& Issue.Context.EndsWith(SlotLabel);
		});
	};
	const TArray<FCharacterProfileValidationIssue> DeletedTypeErrors = ErrorsForSlot(TEXT("FrameCue[1]"));
	const TArray<FCharacterProfileValidationIssue> NullSlotErrors = ErrorsForSlot(TEXT("FrameCue[2]"));
	TestEqual(TEXT("The unresolvable-Cue-Type placement raises exactly one error"),
		DeletedTypeErrors.Num(), 1);
	TestEqual(TEXT("The empty placement slot raises exactly one error"), NullSlotErrors.Num(), 1);
	TestEqual(TEXT("A healthy placement raises no error of its own"),
		ErrorsForSlot(TEXT("FrameCue[0]")).Num(), 0);
	if (DeletedTypeErrors.Num() == 1 && NullSlotErrors.Num() == 1)
	{
		TestTrue(TEXT("The live class-orphan uses the orphaned-placement diagnostic"),
			DeletedTypeErrors[0].Message.Contains(TEXT("orphaned Frame Cue placement")));
		TestTrue(TEXT("The null slot uses the empty-placement diagnostic"),
			NullSlotErrors[0].Message.Contains(TEXT("empty Frame Cue placement")));
		for (const FCharacterProfileValidationIssue& Issue : { DeletedTypeErrors[0], NullSlotErrors[0] })
		{
			TestTrue(TEXT("The orphan error names the owning Character Profile"),
				Issue.Message.Contains(Rig.Profile->GetName()));
			TestTrue(TEXT("The orphan error names the affected animation"),
				Issue.Message.Contains(TEXT("Attack")));
		}
		TestFalse(
			TEXT("The ambiguous null slot is not reported as a live class-orphan"),
			DeletedTypeErrors[0].Message.Equals(NullSlotErrors[0].Message));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCueBehaviorContextAwareSoundTest,
	"Paper2DPlus.FrameCues.Behavior.ContextAwareSoundRoutesByContext",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCueBehaviorContextAwareSoundTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusTestBehaviorMomentCue* Cue = NewObject<UPaper2DPlusTestBehaviorMomentCue>();
	USoundWave* Sound = NewObject<USoundWave>();

	FPaper2DPlusFrameCueContext PreviewContext = FPaper2DPlusFrameCueContext::MakePreview(1, 0, false);
	PreviewContext.Phase = EPaper2DPlusFrameCuePhase::Trigger;

	const UPaper2DPlusCueBase* RoutedCue = nullptr;
	const USoundBase* RoutedSound = nullptr;
	float RoutedVolume = 0.0f;

	// The handler is process-global, and in an editor process the editor module owns it. This test
	// swaps in its own, so it must put the real one back on EVERY exit path. Declared after the locals
	// the replacement captures, it therefore restores before they die — an early failure return can
	// never leave a dangling lambda bound, nor leave editor preview mute for the rest of the session.
	const Paper2DPlusFrameCueBehavior::FPaper2DPlusFrameCuePreviewSound InstalledHandler =
		Paper2DPlusFrameCueBehavior::GetPreviewSoundHandler();
	ON_SCOPE_EXIT
	{
		Paper2DPlusFrameCueBehavior::SetPreviewSoundHandler(InstalledHandler);
	};

	// The runtime module itself installs nothing: with the seam released, a preview request is simply
	// silent rather than failing, which is what a cooked build always sees.
	Paper2DPlusFrameCueBehavior::ClearPreviewSoundHandler();
	TestFalse(TEXT("The runtime module leaves the preview audio seam unbound"),
		Paper2DPlusFrameCueBehavior::IsPreviewSoundHandlerBound());
	TestFalse(TEXT("Preview with nothing bound is silent rather than failing"),
		Cue->PlayCueSound(PreviewContext, Sound));

	Paper2DPlusFrameCueBehavior::SetPreviewSoundHandler(
		Paper2DPlusFrameCueBehavior::FPaper2DPlusFrameCuePreviewSound::CreateLambda(
			[&RoutedCue, &RoutedSound, &RoutedVolume](
				const UPaper2DPlusCueBase& InCue,
				USoundBase* InSound,
				float InVolume,
				float InPitch)
			{
				RoutedCue = &InCue;
				RoutedSound = InSound;
				RoutedVolume = InVolume;
				return true;
			}));

	TestTrue(TEXT("A preview notification routes the cue's sound to the editor seam"),
		Cue->PlayCueSound(PreviewContext, Sound, 0.5f));
	TestTrue(TEXT("The preview seam receives the placement that asked for the sound"),
		RoutedCue == Cue);
	TestTrue(TEXT("The preview seam receives the requested sound"), RoutedSound == Sound);
	TestEqual(TEXT("The preview seam receives the requested volume"), RoutedVolume, 0.5f);

	RoutedCue = nullptr;
	FPaper2DPlusFrameCueContext GameContext;
	GameContext.Phase = EPaper2DPlusFrameCuePhase::Trigger;
	TestFalse(TEXT("A game notification with no world never reaches the preview seam"),
		Cue->PlayCueSound(GameContext, Sound));
	TestNull(TEXT("A game notification is not routed to the editor seam"), RoutedCue);
	TestFalse(TEXT("A null sound routes nowhere"), Cue->PlayCueSound(PreviewContext, nullptr));

	Paper2DPlusFrameCueBehavior::ClearPreviewSoundHandler();
	TestFalse(TEXT("The preview audio seam can be released again"),
		Paper2DPlusFrameCueBehavior::IsPreviewSoundHandlerBound());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
