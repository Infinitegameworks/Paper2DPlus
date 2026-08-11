// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "FrameCues/Paper2DPlusFrameCue.h"
#include "FrameCues/Paper2DPlusFrameCueDispatch.h"
#include "Tests/Paper2DPlusTestFrameCueTypes.h"

namespace Paper2DPlusCueTriggerEdgeTest
{
	UPaper2DPlusTestMomentCue* TriggerEdge_MakeMoment(
		int32 TriggerFrame,
		EPaper2DPlusCueTriggerEdge Edge)
	{
		UPaper2DPlusTestMomentCue* Cue = NewObject<UPaper2DPlusTestMomentCue>();
		Cue->TriggerFrame = TriggerFrame;
		Cue->TriggerEdge = Edge;
		return Cue;
	}

	int32 TriggerEdge_CountTriggers(
		TArrayView<const TObjectPtr<UPaper2DPlusCueBase>> Cues,
		const UPaper2DPlusCueBase* Target,
		int32 PreviousFrame,
		int32 CurrentFrame,
		bool bWasLoopWrap)
	{
		const FPaper2DPlusFrameCueContext Context =
			FPaper2DPlusFrameCueContext::MakePreview(
				CurrentFrame, PreviousFrame, bWasLoopWrap);
		TSet<TObjectPtr<UPaper2DPlusCueBase>> ActiveRanges;
		int32 Fired = 0;
		Paper2DPlusFrameCues::DispatchFrameTransition(
			Cues,
			Context,
			ActiveRanges,
			[](UPaper2DPlusCueBase&) { return true; },
			[Target, &Fired](
				UPaper2DPlusCueBase& Cue,
				const FPaper2DPlusFrameCueContext& CueContext)
			{
				if (&Cue == Target
					&& CueContext.Phase == EPaper2DPlusFrameCuePhase::Trigger)
				{
					++Fired;
				}
			});
		return Fired;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusTrailingEdgePredicateTest,
	"Paper2DPlus.FrameCues.TriggerEdge.TrailingEdgePredicateMirrorsTheStartRule",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusTrailingEdgePredicateTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusFrameCues;

	// Forward: departed frames [Prev, Current) — the exact closed-open mirror of the start
	// rule's (Prev, Current], so every boundary in a span fires exactly once between the two.
	TestTrue(TEXT("forward departs the previous frame"),
		WasFrameTrailingEdgeCrossed(1, 1, 3, false));
	TestTrue(TEXT("forward departs the crossed middle frame"),
		WasFrameTrailingEdgeCrossed(2, 1, 3, false));
	TestFalse(TEXT("forward never departs the entered frame"),
		WasFrameTrailingEdgeCrossed(3, 1, 3, false));
	TestFalse(TEXT("forward never departs an earlier frame"),
		WasFrameTrailingEdgeCrossed(0, 1, 3, false));

	// The start rule's complement holds on the same span.
	TestFalse(TEXT("start rule does not fire the departed frame"),
		WasFrameTraversed(1, 1, 3, false));
	TestTrue(TEXT("start rule fires the entered frame"),
		WasFrameTraversed(3, 1, 3, false));

	// Loop wrap from P to N departs [P, last] then [0, N).
	TestTrue(TEXT("wrap departs the wrap-origin frame"),
		WasFrameTrailingEdgeCrossed(3, 3, 1, true));
	TestTrue(TEXT("wrap departs the frames after the origin"),
		WasFrameTrailingEdgeCrossed(5, 3, 1, true));
	TestTrue(TEXT("wrap departs the pre-target frames"),
		WasFrameTrailingEdgeCrossed(0, 3, 1, true));
	TestFalse(TEXT("wrap never departs the entered frame"),
		WasFrameTrailingEdgeCrossed(1, 3, 1, true));
	TestFalse(TEXT("wrap never departs an uncrossed middle frame"),
		WasFrameTrailingEdgeCrossed(2, 3, 1, true));

	// Unseeded, same-frame, and backward transitions cross no trailing boundary.
	TestFalse(TEXT("an unseeded seek departs nothing"),
		WasFrameTrailingEdgeCrossed(2, INDEX_NONE, 2, false));
	TestFalse(TEXT("a same-frame notification departs nothing"),
		WasFrameTrailingEdgeCrossed(2, 2, 2, false));
	TestFalse(TEXT("a backward seek departs nothing"),
		WasFrameTrailingEdgeCrossed(2, 3, 1, false));
	TestFalse(TEXT("a negative anchor never fires"),
		WasFrameTrailingEdgeCrossed(-1, 0, 3, false));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusTriggerEdgeDispatchTest,
	"Paper2DPlus.FrameCues.TriggerEdge.MomentDispatchSelectsTheAuthoredEdge",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusTriggerEdgeDispatchTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusCueTriggerEdgeTest;

	UPaper2DPlusTestMomentCue* StartCue =
		TriggerEdge_MakeMoment(2, EPaper2DPlusCueTriggerEdge::FrameStart);
	UPaper2DPlusTestMomentCue* EndCue =
		TriggerEdge_MakeMoment(2, EPaper2DPlusCueTriggerEdge::FrameEnd);
	const TArray<TObjectPtr<UPaper2DPlusCueBase>> Cues = { StartCue, EndCue };

	// Entering frame 2 fires only the start-anchored cue.
	TestEqual(TEXT("entering the frame fires the start edge"),
		TriggerEdge_CountTriggers(Cues, StartCue, 1, 2, false), 1);
	TestEqual(TEXT("entering the frame does not fire the end edge"),
		TriggerEdge_CountTriggers(Cues, EndCue, 1, 2, false), 0);

	// Leaving frame 2 fires only the end-anchored cue.
	TestEqual(TEXT("leaving the frame does not re-fire the start edge"),
		TriggerEdge_CountTriggers(Cues, StartCue, 2, 3, false), 0);
	TestEqual(TEXT("leaving the frame fires the end edge"),
		TriggerEdge_CountTriggers(Cues, EndCue, 2, 3, false), 1);

	// A loop wrap over the anchor fires both edges exactly once: the frame was entered by the
	// crossing and departed by the wrap.
	TestEqual(TEXT("a wrap across the anchor fires the start edge once"),
		TriggerEdge_CountTriggers(Cues, StartCue, 4, 0, true)
			+ TriggerEdge_CountTriggers(Cues, StartCue, 0, 4, false), 1);
	TestEqual(TEXT("a wrap past the anchor fires the end edge"),
		TriggerEdge_CountTriggers(Cues, EndCue, 2, 0, true), 1);

	// A fresh seek onto the frame fires the start edge only; a backward scrub fires neither.
	TestEqual(TEXT("a fresh seek fires the start edge"),
		TriggerEdge_CountTriggers(Cues, StartCue, INDEX_NONE, 2, false), 1);
	TestEqual(TEXT("a fresh seek does not fire the end edge"),
		TriggerEdge_CountTriggers(Cues, EndCue, INDEX_NONE, 2, false), 0);
	TestEqual(TEXT("a backward scrub fires no start edge"),
		TriggerEdge_CountTriggers(Cues, StartCue, 4, 1, false), 0);
	TestEqual(TEXT("a backward scrub fires no end edge"),
		TriggerEdge_CountTriggers(Cues, EndCue, 4, 1, false), 0);

	// The class default preserves every existing placement's behavior.
	TestEqual(TEXT("the default trigger edge is FrameStart"),
		static_cast<uint8>(GetDefault<UPaper2DPlusCue>()->TriggerEdge),
		static_cast<uint8>(EPaper2DPlusCueTriggerEdge::FrameStart));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
