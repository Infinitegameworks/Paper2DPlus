// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#if WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "FrameCues/Paper2DPlusFrameCue.h"
#include "FrameCues/Paper2DPlusFrameCueDispatch.h"
#include "Tests/Paper2DPlusTestFrameCueTypes.h"

namespace Paper2DPlusFrameCueTestInternal
{
	struct FRecordedNotification
	{
		TObjectPtr<UPaper2DPlusCueBase> Cue = nullptr;
		EPaper2DPlusFrameCuePhase Phase = EPaper2DPlusFrameCuePhase::Trigger;
		EPaper2DPlusFrameCueEndReason EndReason = EPaper2DPlusFrameCueEndReason::None;
		bool bCompressed = false;
	};

	void Dispatch(
		const TArray<TObjectPtr<UPaper2DPlusCueBase>>& Cues,
		int32 PreviousFrame,
		int32 CurrentFrame,
		bool bLoopWrap,
		TSet<TObjectPtr<UPaper2DPlusCueBase>>& Active,
		TArray<FRecordedNotification>& Out)
	{
		FPaper2DPlusFrameCueContext Context;
		Context.PreviousFrame = PreviousFrame;
		Context.CurrentFrame = CurrentFrame;
		Context.bWasLoopWrap = bLoopWrap;
		Paper2DPlusFrameCues::DispatchFrameTransition(
			Cues,
			Context,
			Active,
			[](UPaper2DPlusCueBase&) { return true; },
			[&Out](UPaper2DPlusCueBase& Cue, const FPaper2DPlusFrameCueContext& Notification)
			{
				Out.Add({ &Cue, Notification.Phase, Notification.EndReason, Notification.bIsCompressed });
			});
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCueUnreachableNeverDispatchedTest,
	"Paper2DPlus.FrameCues.Dispatch.UnreachableCueIsNeverDispatched",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCueUnreachableNeverDispatchedTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusFrameCueTestInternal;

	// The GC's reachability pass marks objects long before they become pending-kill, and
	// ProcessEvent on a marked object asserts. Range ledgers and composed Layer views are
	// GC-invisible by design, so dispatch has to treat "marked unreachable" as not-live at every
	// funnel: transition dispatch, force-end teardown, and the resolvability predicate itself.
	UPaper2DPlusTestRangeCue* Range = NewObject<UPaper2DPlusTestRangeCue>();
	Range->StartFrame = 2;
	Range->FrameCount = 2;
	Range->bEmitUpdates = true;

	TArray<TObjectPtr<UPaper2DPlusCueBase>> Cues = { Range };
	TSet<TObjectPtr<UPaper2DPlusCueBase>> Active;
	TArray<FRecordedNotification> Notifications;
	Dispatch(Cues, 1, 2, false, Active, Notifications);
	TestEqual(TEXT("The live range begins normally"), Notifications.Num(), 2);
	TestEqual(TEXT("The live range is ledgered"), Active.Num(), 1);

	Range->SetInternalFlags(EInternalObjectFlags::Unreachable);
	if (!Range->IsUnreachable())
	{
		// UE 5.4+ incremental reachability owns the unreachable mark and ignores a manually set
		// flag, so the mid-GC window this guards against cannot be constructed from a test there.
		// UE 5.0-5.4 — the engines whose between-test GC pass produced the crash — honor the flag,
		// and the matrix runs this test on all of them.
		Range->ClearInternalFlags(EInternalObjectFlags::Unreachable);
		Active.Empty();
		AddInfo(TEXT(
			"Skipped: this engine's GC owns reachability marks, so the unreachable dispatch "
			"window is not constructible from a test here."));
		return true;
	}
	TestFalse(TEXT("An unreachable cue is not placement-resolvable"),
		Paper2DPlusFrameCueBehavior::IsPlacementResolvable(Range));

	Notifications.Reset();
	Paper2DPlusFrameCues::ForceEndActiveRanges(
		Cues,
		FPaper2DPlusFrameCueContext(),
		EPaper2DPlusFrameCueEndReason::Interrupted,
		Active,
		[&Notifications](UPaper2DPlusCueBase& Cue, const FPaper2DPlusFrameCueContext& Context)
		{
			Notifications.Add({ &Cue, Context.Phase, Context.EndReason, Context.bIsCompressed });
		});
	TestEqual(TEXT("Force-end never dispatches to an unreachable cue"), Notifications.Num(), 0);
	TestEqual(TEXT("Force-end still drops the unreachable ledger entry"), Active.Num(), 0);

	// Re-ledger it, then prove the transition funnel's stale sweep drops it silently too.
	Active.Add(Range);
	Notifications.Reset();
	Dispatch(Cues, 2, 3, false, Active, Notifications);
	TestEqual(TEXT("Transition dispatch never notifies an unreachable cue"), Notifications.Num(), 0);
	TestEqual(TEXT("The stale sweep drops the unreachable ledger entry"), Active.Num(), 0);

	Range->ClearInternalFlags(EInternalObjectFlags::Unreachable);
	TestTrue(TEXT("Clearing the mark restores resolvability"),
		Paper2DPlusFrameCueBehavior::IsPlacementResolvable(Range));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCueCompressedOrderingTest,
	"Paper2DPlus.FrameCues.Dispatch.CompressedOrdering",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCueCompressedOrderingTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusFrameCueTestInternal;
	UPaper2DPlusTestMomentCue* Moment = NewObject<UPaper2DPlusTestMomentCue>();
	Moment->TriggerFrame = 3;
	UPaper2DPlusTestRangeCue* Range = NewObject<UPaper2DPlusTestRangeCue>();
	Range->StartFrame = 4;
	Range->FrameCount = 2;
	Range->bEmitUpdates = true;

	TArray<TObjectPtr<UPaper2DPlusCueBase>> Cues = { Moment, Range };
	TSet<TObjectPtr<UPaper2DPlusCueBase>> Active;
	TArray<FRecordedNotification> Notifications;
	Dispatch(Cues, 2, 7, false, Active, Notifications);

	TestEqual(TEXT("Moment plus compressed range emits four notifications"), Notifications.Num(), 4);
	if (Notifications.Num() == 4)
	{
		TestEqual(TEXT("Moment first in authored order"), Notifications[0].Phase, EPaper2DPlusFrameCuePhase::Trigger);
		TestEqual(TEXT("Range Begin"), Notifications[1].Phase, EPaper2DPlusFrameCuePhase::Begin);
		TestEqual(TEXT("Range Update"), Notifications[2].Phase, EPaper2DPlusFrameCuePhase::Update);
		TestEqual(TEXT("Range End"), Notifications[3].Phase, EPaper2DPlusFrameCuePhase::End);
		TestTrue(TEXT("Compressed Begin marked"), Notifications[1].bCompressed);
		TestEqual(TEXT("Compressed End completes"), Notifications[3].EndReason, EPaper2DPlusFrameCueEndReason::Completed);
	}
	TestEqual(TEXT("Compressed range never remains active"), Active.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCueRangeLifecycleTest,
	"Paper2DPlus.FrameCues.Dispatch.RangeLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCueRangeLifecycleTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusFrameCueTestInternal;
	UPaper2DPlusTestRangeCue* Range = NewObject<UPaper2DPlusTestRangeCue>();
	Range->StartFrame = 2;
	Range->FrameCount = 2;
	Range->bEmitUpdates = true;
	TArray<TObjectPtr<UPaper2DPlusCueBase>> Cues = { Range };
	TSet<TObjectPtr<UPaper2DPlusCueBase>> Active;
	TArray<FRecordedNotification> Notifications;

	Dispatch(Cues, 1, 2, false, Active, Notifications);
	Dispatch(Cues, 2, 3, false, Active, Notifications);
	Dispatch(Cues, 3, 4, false, Active, Notifications);

	TestEqual(TEXT("Begin, two Updates, End"), Notifications.Num(), 4);
	if (Notifications.Num() == 4)
	{
		TestEqual(TEXT("Entry begins"), Notifications[0].Phase, EPaper2DPlusFrameCuePhase::Begin);
		TestEqual(TEXT("Entry updates"), Notifications[1].Phase, EPaper2DPlusFrameCuePhase::Update);
		TestEqual(TEXT("Second active frame updates"), Notifications[2].Phase, EPaper2DPlusFrameCuePhase::Update);
		TestEqual(TEXT("Exit ends"), Notifications[3].Phase, EPaper2DPlusFrameCuePhase::End);
		TestEqual(TEXT("Natural End reason"), Notifications[3].EndReason, EPaper2DPlusFrameCueEndReason::Completed);
	}
	TestEqual(TEXT("Range inactive after exit"), Active.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCueLoopAndStaleEndTest,
	"Paper2DPlus.FrameCues.Dispatch.LoopAndStaleEndReasons",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCueLoopAndStaleEndTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusFrameCueTestInternal;
	UPaper2DPlusTestRangeCue* Range = NewObject<UPaper2DPlusTestRangeCue>();
	Range->StartFrame = 0;
	Range->FrameCount = 3;
	TArray<TObjectPtr<UPaper2DPlusCueBase>> Cues = { Range };
	TSet<TObjectPtr<UPaper2DPlusCueBase>> Active;
	TArray<FRecordedNotification> Notifications;

	Dispatch(Cues, INDEX_NONE, 1, false, Active, Notifications);
	Dispatch(Cues, 2, 1, true, Active, Notifications);
	// Loop-seam continuity: a range still containing the post-wrap frame stays active across the
	// wrap — no End(LoopReset)/Begin pair (an authority-only tag cue must not remove/re-add its tag
	// every loop). The out-of-range-after-wrap LoopReset case is pinned in FrameCueRuntimeTest.
	TestEqual(TEXT("Seam-spanning range stays active across the wrap"), Notifications.Num(), 1);
	if (Notifications.Num() == 1)
	{
		TestEqual(TEXT("Initial Begin"), Notifications[0].Phase, EPaper2DPlusFrameCuePhase::Begin);
	}
	TestEqual(TEXT("Range still active after wrap"), Active.Num(), 1);

	TArray<TObjectPtr<UPaper2DPlusCueBase>> Empty;
	Dispatch(Empty, 1, 2, false, Active, Notifications);
	TestEqual(TEXT("Stale source emits one more End"), Notifications.Num(), 2);
	if (Notifications.Num() == 2)
	{
		TestEqual(TEXT("Stale End reason"), Notifications[1].EndReason, EPaper2DPlusFrameCueEndReason::SourceRemoved);
	}
	TestEqual(TEXT("Stale source removed from active set"), Active.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCueAnchorRemapTest,
	"Paper2DPlus.FrameCues.Authoring.AnchorRemap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCueAnchorRemapTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusTestMomentCue* Moment = NewObject<UPaper2DPlusTestMomentCue>();
	Moment->TriggerFrame = 2;
	UPaper2DPlusTestRangeCue* Range = NewObject<UPaper2DPlusTestRangeCue>();
	Range->StartFrame = 1;
	Range->FrameCount = 3;
	const TArray<int32> Mapping = { 2, 0, 3, 1 };

	TestTrue(TEXT("Moment remaps"), Moment->RemapFrameAnchors(Mapping, 4));
	TestEqual(TEXT("Moment target"), Moment->TriggerFrame, 3);
	TestTrue(TEXT("Range remaps"), Range->RemapFrameAnchors(Mapping, 4));
	TestEqual(TEXT("Range min target"), Range->StartFrame, 0);
	TestEqual(TEXT("Range spans surviving extent"), Range->FrameCount, 4);

	Moment->TriggerFrame = 2;
	const TArray<int32> Removed = { 0, 1, INDEX_NONE, 2 };
	TestFalse(TEXT("Removed primary anchor requests stash"), Moment->RemapFrameAnchors(Removed, 3));
	return true;
}

#endif // WITH_EDITOR
