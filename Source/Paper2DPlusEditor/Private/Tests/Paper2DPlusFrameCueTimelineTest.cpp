// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "SFrameCueTimeline.h"
#include "SFrameEventTimelineTrack.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCueTimelinePackingTest,
	"Paper2DPlus.FrameCues.Editor.Timeline.DeterministicDensePacking",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCueTimelinePackingTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusFrameCueTimeline;
	const FGuid AudioTrack = FGuid::NewGuid();
	const FGuid ImpactTrack = FGuid::NewGuid();
	const FGuid UnknownTrack = FGuid::NewGuid();

	TArray<FPlacement> Placements;
	Placements.Add({ 0, AudioTrack, 1, 2 }); // inclusive [1,2]
	Placements.Add({ 1, AudioTrack, 2, 1 }); // overlaps the Range at frame 2
	Placements.Add({ 2, AudioTrack, 3, 1 }); // first lane becomes free
	Placements.Add({ 3, ImpactTrack, 4, 1 });
	Placements.Add({ 4, ImpactTrack, 4, 1 }); // same-frame Moment needs another lane
	Placements.Add({ 5, UnknownTrack, 7, 1 }); // corrupt/stale assignment projects to Default
	Placements.Add({ 6, FGuid(), 8, 0 }); // Default; invalid count still occupies one frame

	const TArray<FPackedTrack> Packed = PackPlacements(
		Placements,
		{ AudioTrack, FGuid(), AudioTrack, ImpactTrack });

	TestEqual(TEXT("Default plus two unique valid optional tracks"), Packed.Num(), 3);
	if (Packed.Num() != 3)
	{
		return false;
	}
	TestTrue(TEXT("Default is first and implicit"), Packed[0].IsDefault());
	TestEqual(TEXT("Audio order is retained"), Packed[1].TrackId, AudioTrack);
	TestEqual(TEXT("Impact order is retained"), Packed[2].TrackId, ImpactTrack);

	TestEqual(TEXT("unknown and explicit Default assignments share Default"),
		Packed[0].Placements.Num(), 2);
	TestEqual(TEXT("unknown track is projected without changing Cue identity"),
		Packed[0].Placements[0].CueIndex, 5);
	TestFalse(TEXT("projected membership is canonical Default"),
		Packed[0].Placements[0].TrackId.IsValid());
	TestEqual(TEXT("zero count occupies its definitive anchor only"),
		Packed[0].Placements[1].EndFrameInclusive, 8);

	TestEqual(TEXT("inclusive Range/Moment overlap uses two Audio lanes"),
		Packed[1].SublaneCount, 2);
	TestEqual(TEXT("Range takes first lane"), Packed[1].Placements[0].Sublane, 0);
	TestEqual(TEXT("intersecting Moment takes second lane"), Packed[1].Placements[1].Sublane, 1);
	TestEqual(TEXT("next non-overlapping Moment reuses first lane"), Packed[1].Placements[2].Sublane, 0);

	TestEqual(TEXT("same-frame Moments remain independently visible"),
		Packed[2].SublaneCount, 2);
	TestEqual(TEXT("authoritative first Cue wins deterministic first sublane"),
		Packed[2].Placements[0].CueIndex, 3);
	TestEqual(TEXT("authoritative second Cue uses deterministic second sublane"),
		Packed[2].Placements[1].CueIndex, 4);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCueTimelineEmptyPackingTest,
	"Paper2DPlus.FrameCues.Editor.Timeline.EmptyTracksRemainDiscoverable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCueTimelineEmptyPackingTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusFrameCueTimeline;
	const FGuid OptionalTrack = FGuid::NewGuid();
	const TArray<FPackedTrack> Packed = PackPlacements({}, { OptionalTrack });
	TestEqual(TEXT("empty layout still returns Default and authored optional track"), Packed.Num(), 2);
	if (Packed.Num() == 2)
	{
		TestEqual(TEXT("empty Default keeps one visible sublane"), Packed[0].SublaneCount, 1);
		TestEqual(TEXT("empty optional track keeps one visible sublane"), Packed[1].SublaneCount, 1);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCueTimelineContextGeometryTest,
	"Paper2DPlus.FrameCues.Editor.Timeline.NoAnimationZeroFrameAndTimedGeometry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCueTimelineContextGeometryTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusFrameCueTimeline;
	TestEqual(TEXT("unresolved context is distinct"),
		FTimingGeometry::ResolveContext(false, 10), ETimingContext::NoAnimation);
	TestEqual(TEXT("resolved zero-frame context is distinct"),
		FTimingGeometry::ResolveContext(true, 0), ETimingContext::ResolvedZeroFrames);
	TestEqual(TEXT("positive frame context is timed"),
		FTimingGeometry::ResolveContext(true, 3), ETimingContext::Timed);
	TestEqual(TEXT("timed body uses the shared key-frame width"),
		FTimingGeometry::GetBodyWidth(true, 3), 3.0f * FTimingGeometry::PixelsPerKeyFrame);
	TestEqual(TEXT("zero-frame body remains visibly discoverable"),
		FTimingGeometry::GetBodyWidth(true, 0), FTimingGeometry::MinimumNonTimingBodyWidth);

	int32 FrameIndex = 99;
	TestFalse(TEXT("zero-frame context exposes no fake frame hit target"),
		FTimingGeometry::TryResolveFrameAtX(true, 0, 0.0f, FrameIndex));
	TestEqual(TEXT("failed frame resolve clears output"), FrameIndex, INDEX_NONE);
	TestTrue(TEXT("timed cell center resolves its key frame"),
		FTimingGeometry::TryResolveFrameAtX(
			true, 3, FTimingGeometry::PixelsPerKeyFrame * 1.5f, FrameIndex));
	TestEqual(TEXT("second cell resolves frame one"), FrameIndex, 1);
	TestFalse(TEXT("right edge is outside the half-open timing body"),
		FTimingGeometry::TryResolveFrameAtX(
			true, 3, FTimingGeometry::PixelsPerKeyFrame * 3.0f, FrameIndex));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCueTimelineResizableGutterTest,
	"Paper2DPlus.FrameCues.Editor.Timeline.SharedLegendGutterIsBoundedAndResizable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCueTimelineResizableGutterTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusFrameCueTimeline;
	TestEqual(
		TEXT("requested gutter widths clamp at the narrow design limit"),
		FGutterGeometry::ClampRequestedWidth(-1000.0f),
		FGutterGeometry::MinimumWidth);
	TestEqual(
		TEXT("requested gutter widths clamp at the wide design limit"),
		FGutterGeometry::ClampRequestedWidth(1000.0f),
		FGutterGeometry::MaximumWidth);
	TestEqual(
		TEXT("the compact default remains inside the bounded range"),
		FGutterGeometry::ClampRequestedWidth(FGutterGeometry::DefaultWidth),
		FGutterGeometry::DefaultWidth);

	TArray<TObjectPtr<UPaper2DPlusCueBase>> EmptyCues;
	const TSharedPtr<SFrameCueTimeline> Timeline = SNew(SFrameCueTimeline)
		.Cues(&EmptyCues)
		.SelectedFlipbookIndex(INDEX_NONE)
		.SelectedCueIndex(INDEX_NONE)
		.SelectedFrameIndex(INDEX_NONE);
	TestTrue(
		TEXT("the production timing surface mounts one resizable splitter gutter"),
		Timeline->HasResizableGutterForTests());
	TestEqual(
		TEXT("the gutter opens at the compact default"),
		Timeline->GetRequestedGutterWidthForTests(),
		FGutterGeometry::DefaultWidth);
	TestTrue(
		TEXT("the real SizeToContent slot dispatches its configured resize delegate"),
		Timeline->ResizeGutterThroughSlotForTests(FGutterGeometry::MaximumWidth + 50.0f));
	TestEqual(
		TEXT("the live gutter state cannot grow past the alignment bound"),
		Timeline->GetRequestedGutterWidthForTests(),
		FGutterGeometry::MaximumWidth);
	TestTrue(
		TEXT("the same production slot remains resizable toward the narrow bound"),
		Timeline->ResizeGutterThroughSlotForTests(FGutterGeometry::MinimumWidth - 50.0f));
	TestEqual(
		TEXT("the live gutter state cannot collapse the legend"),
		Timeline->GetRequestedGutterWidthForTests(),
		FGutterGeometry::MinimumWidth);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCueTimelineEmptyWidgetGeometryTest,
	"Paper2DPlus.FrameCues.Editor.Timeline.EmptyLaneDoesNotCollapse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCueTimelineEmptyWidgetGeometryTest::RunTest(const FString& Parameters)
{
	TArray<TObjectPtr<UPaper2DPlusCueBase>> EmptyCues;
	const TSharedRef<SFrameEventTimelineTrack> Track =
		SNew(SFrameEventTimelineTrack)
		.Cues(&EmptyCues)
		.CharacterFrameCount(0)
		.SelectedEventIndex(INDEX_NONE)
		.SelectedFrameIndex(INDEX_NONE);
	const FVector2D Desired = Track->ComputeDesiredSize(1.0f);
	TestTrue(TEXT("empty Cue lane retains visible height"), Desired.Y >= 32.0f);
	TestTrue(TEXT("zero-frame Cue lane retains explanatory body width"),
		Desired.X >= Paper2DPlusFrameCueTimeline::FTimingGeometry::MinimumNonTimingBodyWidth);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCueTimelineDragAxisTest,
	"Paper2DPlus.FrameCues.Editor.Timeline.DominantAxisTrackReassignment",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCueTimelineDragAxisTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusFrameCueTimeline;
	TestEqual(TEXT("sub-threshold motion remains armed"),
		ResolveDragAxis(FVector2D(2.0f, 2.0f), 5.0f, true), EDragAxis::Pending);
	TestEqual(TEXT("horizontal dominant motion changes timing"),
		ResolveDragAxis(FVector2D(9.0f, 4.0f), 5.0f, true), EDragAxis::Horizontal);
	TestEqual(TEXT("vertical dominant motion changes membership"),
		ResolveDragAxis(FVector2D(4.0f, 9.0f), 5.0f, true), EDragAxis::Vertical);
	TestEqual(TEXT("equal diagonal resolves deterministically to timing"),
		ResolveDragAxis(FVector2D(8.0f, 8.0f), 5.0f, true), EDragAxis::Horizontal);
	TestEqual(TEXT("Range resize handles can never switch tracks"),
		ResolveDragAxis(FVector2D(2.0f, 10.0f), 5.0f, false), EDragAxis::Horizontal);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCueTimelineScrollMathTest,
	"Paper2DPlus.FrameCues.Editor.Timeline.AxisSafeAutoscrollAndReveal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCueTimelineScrollMathTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusFrameCueTimeline;
	TestEqual(TEXT("center does not autoscroll"),
		ComputeEdgeAutoscrollDelta(100.0f, 200.0f), 0.0f);
	TestTrue(TEXT("left edge scrolls backward"),
		ComputeEdgeAutoscrollDelta(8.0f, 200.0f) < 0.0f);
	TestTrue(TEXT("right edge scrolls forward"),
		ComputeEdgeAutoscrollDelta(192.0f, 200.0f) > 0.0f);
	TestEqual(TEXT("outside pointer remains bounded"),
		ComputeEdgeAutoscrollDelta(500.0f, 200.0f), 24.0f);
	TestEqual(TEXT("already visible range does not jump"),
		ResolveRevealScrollOffset(100.0f, 200.0f, 140.0f, 220.0f), 100.0f);
	TestEqual(TEXT("left reveal scrolls just enough with margin"),
		ResolveRevealScrollOffset(100.0f, 200.0f, 50.0f, 75.0f), 34.0f);
	TestEqual(TEXT("right reveal scrolls just enough with margin"),
		ResolveRevealScrollOffset(100.0f, 200.0f, 290.0f, 340.0f), 156.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCueTimelinePrimarySelectionTest,
	"Paper2DPlus.FrameCues.Editor.Timeline.PrimarySelectionDoesNotRetargetAddCue",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCueTimelinePrimarySelectionTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusFrameCueTimeline;
	const FGuid ActiveTrack = FGuid::NewGuid();
	FPrimarySelection Selection;
	Selection.SelectTrack(ActiveTrack);
	TestEqual(TEXT("track selection stores its organizational identity"),
		Selection.TrackId, ActiveTrack);
	Selection.SelectCurve(FName(TEXT("Speed")));
	FProfileScopedAnimationIdentity SpeedScope;
	SpeedScope.Animation = FProfileAnimationIdentity(
		FSoftObjectPath(), TEXT("Attack"));
	SpeedScope.LayerScope = FProfileLayerScopeIdentity::Profile();
	Selection.Scope = SpeedScope;
	TestEqual(TEXT("curve becomes primary"), Selection.Kind, EPrimarySelectionKind::Curve);
	TestEqual(TEXT("curve identity is stored"), Selection.CurveName, FName(TEXT("Speed")));
	TestFalse(TEXT("curve selection carries no replacement Add Cue target"),
		Selection.TrackId.IsValid());
	TestTrue(TEXT("primary selection retains its exact animation scope"),
		Selection.IsInScope(SpeedScope));
	FProfileScopedAnimationIdentity OtherScope = SpeedScope;
	OtherScope.Animation = FProfileAnimationIdentity(
		FSoftObjectPath(), TEXT("Idle"));
	TestFalse(TEXT("same-named data in another animation cannot retain primary selection"),
		Selection.IsInScope(OtherScope));

	TSet<FGuid> Tracks;
	Tracks.Add(ActiveTrack);
	TSet<TWeakObjectPtr<UPaper2DPlusCueBase>> Cues;
	TSet<FName> Curves;
	Curves.Add(FName(TEXT("Speed")));
	Curves.Add(FName(TEXT("Height")));
	TestFalse(TEXT("authored curve remains selected"),
		Selection.Reconcile(true, Tracks, Cues, &Curves));
	Curves.Remove(FName(TEXT("Speed")));
	TestTrue(TEXT("deleted curve clears primary selection"),
		Selection.Reconcile(true, Tracks, Cues, &Curves));
	TestEqual(TEXT("stale curve reconciles to None"),
		Selection.Kind, EPrimarySelectionKind::None);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
