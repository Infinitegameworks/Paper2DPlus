// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#if WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "CurveTrackPanel.h"
#include "Paper2DPlusMoveTransition.h"
#include "SFrameCueTimeline.h"

/**
 * Worldless tests for the curve-track pure logic (curve tracks under Frame Events, PR B):
 * the coerce funnel (round / clamp / dedupe / Mode re-stamp — brainstorm D4), the orphan detector
 * (bug E17), the shared name->color hash determinism, and the shared picker's semantic seeding.
 * Helpers are CurveTracks_-prefixed per the unity-build file-unique-name rule.
 */

namespace
{
	/** Add a key at (Time, Value) with an explicit interp mode, bypassing every write funnel —
	 *  simulates the engine SCurveEditor poking the RichCurve directly. */
	FKeyHandle CurveTracks_AddRawKey(FRichCurve& Curve, float Time, float Value,
		ERichCurveInterpMode Interp = RCIM_Linear)
	{
		const FKeyHandle Handle = Curve.AddKey(Time, Value);
		Curve.SetKeyInterpMode(Handle, Interp);
		return Handle;
	}

	/** Sorted (frame, value) snapshot of a curve for order-independent assertions. */
	TArray<TPair<int32, float>> CurveTracks_Snapshot(const FRichCurve& Curve)
	{
		TArray<TPair<int32, float>> Out;
		for (const FRichCurveKey& Key : Curve.GetConstRefOfKeys())
		{
			Out.Emplace(FMath::RoundToInt(Key.Time), Key.Value);
		}
		Out.Sort([](const TPair<int32, float>& A, const TPair<int32, float>& B) { return A.Key < B.Key; });
		return Out;
	}

	int32 CurveTracks_CountWidgetType(const TSharedRef<SWidget>& Widget, const FName WidgetType)
	{
		int32 Count = Widget->GetType() == WidgetType ? 1 : 0;
		if (FChildren* Children = Widget->GetChildren())
		{
			for (int32 ChildIndex = 0; ChildIndex < Children->Num(); ++ChildIndex)
			{
				Count += CurveTracks_CountWidgetType(
					Children->GetChildAt(ChildIndex),
					WidgetType);
			}
		}
		return Count;
	}
}

// D4 — rounding: fractional key times (the MMB FreeDrag leak) snap to the nearest integer frame,
// values untouched; an already-clean curve is a no-op (bChanged false, all counts zero).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCurveTracksCoerceRounds,
	"Paper2DPlus.CurveTracks.Coerce.RoundsToFrames",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCurveTracksCoerceRounds::RunTest(const FString& Parameters)
{
	FRichCurve Curve;
	CurveTracks_AddRawKey(Curve, 0.0f, 1.f);
	CurveTracks_AddRawKey(Curve, 2.4f, 5.f);  // rounds down to 2
	CurveTracks_AddRawKey(Curve, 5.6f, 9.f);  // rounds up to 6

	const Paper2DPlusCurveTracks::FCurveCoerceSummary Summary =
		Paper2DPlusCurveTracks::CoerceCurveKeysToFrames(Curve, /*FrameCount*/ 8, EPaper2DPlusCurveInterp::Linear);

	TestTrue(TEXT("Summary reports a change"), Summary.bChanged);
	TestEqual(TEXT("Two keys needed rounding"), Summary.NumRounded, 2);
	TestEqual(TEXT("Nothing deduped"), Summary.NumDeduped, 0);
	TestEqual(TEXT("Nothing clamped"), Summary.NumClamped, 0);

	const TArray<TPair<int32, float>> Keys = CurveTracks_Snapshot(Curve);
	TestEqual(TEXT("Three keys survive"), Keys.Num(), 3);
	if (Keys.Num() == 3)
	{
		TestEqual(TEXT("Key 0 stays on frame 0"), Keys[0].Key, 0);
		TestEqual(TEXT("2.4 snapped to frame 2"), Keys[1].Key, 2);
		TestEqual(TEXT("2.4's value preserved"), Keys[1].Value, 5.f);
		TestEqual(TEXT("5.6 snapped to frame 6"), Keys[2].Key, 6);
		TestEqual(TEXT("5.6's value preserved"), Keys[2].Value, 9.f);
	}

	// Idempotence: a second pass over the now-clean curve is a no-op.
	const Paper2DPlusCurveTracks::FCurveCoerceSummary Second =
		Paper2DPlusCurveTracks::CoerceCurveKeysToFrames(Curve, 8, EPaper2DPlusCurveInterp::Linear);
	TestFalse(TEXT("Second pass is a no-op"), Second.bChanged);
	TestEqual(TEXT("No-op rounds nothing"), Second.NumRounded, 0);

	return true;
}

// D4/F4 — clamping: only keys whose PRE-coerce time was FRACTIONAL (actively moved this gesture)
// clamp into [0, FrameCount-1]. Exactly-integral out-of-range keys are legacy orphans and survive —
// see the dedicated LegacyOrphansSurvive test below.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCurveTracksCoerceClamps,
	"Paper2DPlus.CurveTracks.Coerce.ClampsIntoRange",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCurveTracksCoerceClamps::RunTest(const FString& Parameters)
{
	FRichCurve Curve;
	CurveTracks_AddRawKey(Curve, -2.4f, 1.f);  // fractional below range: rounds -2, clamps to 0
	CurveTracks_AddRawKey(Curve, 3.0f, 5.f);   // in range, integral — untouched
	CurveTracks_AddRawKey(Curve, 11.3f, 9.f);  // fractional past range: rounds 11, clamps to 7 (FrameCount 8)

	const Paper2DPlusCurveTracks::FCurveCoerceSummary Summary =
		Paper2DPlusCurveTracks::CoerceCurveKeysToFrames(Curve, 8, EPaper2DPlusCurveInterp::Linear);

	TestTrue(TEXT("Summary reports a change"), Summary.bChanged);
	TestEqual(TEXT("Two fractional keys clamped"), Summary.NumClamped, 2);
	TestEqual(TEXT("Both clamped keys also counted as rounded"), Summary.NumRounded, 2);

	const TArray<TPair<int32, float>> Keys = CurveTracks_Snapshot(Curve);
	TestEqual(TEXT("Three keys survive"), Keys.Num(), 3);
	if (Keys.Num() == 3)
	{
		TestEqual(TEXT("-2.4 clamped onto frame 0"), Keys[0].Key, 0);
		TestEqual(TEXT("Frame 3 untouched"), Keys[1].Key, 3);
		TestEqual(TEXT("11.3 clamped onto frame 7"), Keys[2].Key, 7);
	}

	// FrameCount <= 0: no valid band — rounding still applies, clamping is skipped (the orphan
	// detector's caller surfaces the frames-unavailable state instead of the funnel piling
	// everything onto frame 0).
	FRichCurve NoBand;
	CurveTracks_AddRawKey(NoBand, 4.6f, 2.f);
	const Paper2DPlusCurveTracks::FCurveCoerceSummary NoBandSummary =
		Paper2DPlusCurveTracks::CoerceCurveKeysToFrames(NoBand, 0, EPaper2DPlusCurveInterp::Linear);
	TestEqual(TEXT("FrameCount 0 clamps nothing"), NoBandSummary.NumClamped, 0);
	TestEqual(TEXT("FrameCount 0 still rounds"), NoBandSummary.NumRounded, 1);
	TestEqual(TEXT("Key rounded onto frame 5"), CurveTracks_Snapshot(NoBand)[0].Key, 5);

	return true;
}

// F4 (CT-5) — legacy-orphan survival: exactly-integral out-of-range keys are NOT clamped, so an
// unrelated edit on a curve carrying legacy orphans (flipbook shrink/reimport) can no longer cascade
// clamp+dedupe and silently destroy the authored last-frame key. Keys at 9 and 11 (integral, past
// FrameCount 8) + an authored key at 7, plus an unrelated engine-added key near frame 1 -> coerce ->
// 9 and 11 SURVIVE as orphans, 7 untouched.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCurveTracksCoerceLegacyOrphansSurvive,
	"Paper2DPlus.CurveTracks.Coerce.LegacyOrphansSurvive",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCurveTracksCoerceLegacyOrphansSurvive::RunTest(const FString& Parameters)
{
	FRichCurve Curve;
	CurveTracks_AddRawKey(Curve, 9.0f, 2.f);   // legacy orphan (integral, past FrameCount 8)
	CurveTracks_AddRawKey(Curve, 11.0f, 4.f);  // legacy orphan
	CurveTracks_AddRawKey(Curve, 7.0f, 8.f);   // the authored last-frame key the old cascade destroyed
	CurveTracks_AddRawKey(Curve, 1.2f, 1.f);   // the unrelated edit this gesture — rounds onto frame 1

	const Paper2DPlusCurveTracks::FCurveCoerceSummary Summary =
		Paper2DPlusCurveTracks::CoerceCurveKeysToFrames(Curve, /*FrameCount*/ 8, EPaper2DPlusCurveInterp::Linear);

	TestEqual(TEXT("Only the unrelated fractional key rounded"), Summary.NumRounded, 1);
	TestEqual(TEXT("Nothing clamped — integral orphans are not 'actively moved'"), Summary.NumClamped, 0);
	TestEqual(TEXT("Nothing deduped — no clamp means no collision cascade"), Summary.NumDeduped, 0);

	const TArray<TPair<int32, float>> Keys = CurveTracks_Snapshot(Curve);
	TestEqual(TEXT("All four keys survive"), Keys.Num(), 4);
	if (Keys.Num() == 4)
	{
		TestEqual(TEXT("Unrelated key landed on frame 1"), Keys[0].Key, 1);
		TestEqual(TEXT("Authored frame-7 key untouched"), Keys[1].Key, 7);
		TestEqual(TEXT("Authored frame-7 value untouched"), Keys[1].Value, 8.f);
		TestEqual(TEXT("Legacy orphan 9 survives"), Keys[2].Key, 9);
		TestEqual(TEXT("Legacy orphan 11 survives"), Keys[3].Key, 11);
	}

	// The survivors are exactly what the orphan detector reports for the prune affordance.
	const TArray<int32> Orphans = Paper2DPlusCurveTracks::FindOrphanKeyFrames(Curve, 8);
	TestEqual(TEXT("Detector reports the two surviving orphans"), Orphans.Num(), 2);

	return true;
}

// D4 — collision dedupe: when a dragged (fractional-time) key lands on a stationary key's frame, the
// FURTHER-from-integer key (the dragged one) wins; an exact-integer tie keeps the EARLIER array entry
// (= the most recently re-inserted key, i.e. the dragged one — see the funnel's insert-before note).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCurveTracksCoerceDedupes,
	"Paper2DPlus.CurveTracks.Coerce.DedupeMovedKeyWins",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCurveTracksCoerceDedupes::RunTest(const FString& Parameters)
{
	// Stationary key exactly on frame 3 (value 1); dragged key at fractional 2.6 (value 7) rounds to 3.
	FRichCurve Curve;
	CurveTracks_AddRawKey(Curve, 3.0f, 1.f);
	CurveTracks_AddRawKey(Curve, 2.6f, 7.f);

	const Paper2DPlusCurveTracks::FCurveCoerceSummary Summary =
		Paper2DPlusCurveTracks::CoerceCurveKeysToFrames(Curve, 8, EPaper2DPlusCurveInterp::Linear);

	TestEqual(TEXT("One key deduped"), Summary.NumDeduped, 1);
	const TArray<TPair<int32, float>> Keys = CurveTracks_Snapshot(Curve);
	TestEqual(TEXT("One key survives the collision"), Keys.Num(), 1);
	if (Keys.Num() == 1)
	{
		TestEqual(TEXT("Survivor sits on frame 3"), Keys[0].Key, 3);
		TestEqual(TEXT("The DRAGGED (further-from-integer) key's value won"), Keys[0].Value, 7.f);
	}

	// Tie-break: two keys both exactly integral on the same frame -> the EARLIER array entry wins,
	// which is the most recently (re)inserted key: 5.7 FRichCurve::AddKey inserts an equal-time key
	// BEFORE existing ones (and SetKeyTime = DeleteKey+AddKey), so a snapped drag onto an occupied
	// frame keeps the DRAGGED key. Here the second AddKey(4.0) lands at index 0 and must win.
	FRichCurve Tie;
	CurveTracks_AddRawKey(Tie, 4.0f, 1.f);
	CurveTracks_AddRawKey(Tie, 4.0f, 9.f); // inserted BEFORE the first equal-time key -> must survive
	const Paper2DPlusCurveTracks::FCurveCoerceSummary TieSummary =
		Paper2DPlusCurveTracks::CoerceCurveKeysToFrames(Tie, 8, EPaper2DPlusCurveInterp::Linear);
	TestEqual(TEXT("Tie deduped one key"), TieSummary.NumDeduped, 1);
	const TArray<TPair<int32, float>> TieKeys = CurveTracks_Snapshot(Tie);
	TestEqual(TEXT("Tie leaves one key"), TieKeys.Num(), 1);
	if (TieKeys.Num() == 1)
	{
		TestEqual(TEXT("Tie-break kept the most recently inserted (dragged) key's value"), TieKeys[0].Value, 9.f);
	}

	return true;
}

// F12a — regression with the REAL gesture topology: an engine snap-drag moves a key via
// FRichCurve::SetKeyTime, which is a handle-preserving delete+re-insert (the re-inserted equal-time
// key lands BEFORE the stationary one). Moving a key onto an OCCUPIED integer frame this way must
// dedupe in favor of the MOVED key — pinning the earlier-iteration-order tie-break against the actual
// API the gesture uses, not just raw AddKey ordering.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCurveTracksCoerceGestureTopology,
	"Paper2DPlus.CurveTracks.Coerce.SetKeyTimeMoveOntoOccupiedFrame",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCurveTracksCoerceGestureTopology::RunTest(const FString& Parameters)
{
	FRichCurve Curve;
	CurveTracks_AddRawKey(Curve, 3.0f, 1.f);                                  // stationary occupant
	const FKeyHandle MovedHandle = CurveTracks_AddRawKey(Curve, 1.0f, 7.f);   // the key being dragged
	Curve.SetKeyTime(MovedHandle, 3.0f); // the snapped LMB drag landing on the occupied frame

	const Paper2DPlusCurveTracks::FCurveCoerceSummary Summary =
		Paper2DPlusCurveTracks::CoerceCurveKeysToFrames(Curve, /*FrameCount*/ 8, EPaper2DPlusCurveInterp::Linear);

	TestEqual(TEXT("Collision deduped one key"), Summary.NumDeduped, 1);
	const TArray<TPair<int32, float>> Keys = CurveTracks_Snapshot(Curve);
	TestEqual(TEXT("One key survives"), Keys.Num(), 1);
	if (Keys.Num() == 1)
	{
		TestEqual(TEXT("Survivor sits on frame 3"), Keys[0].Key, 3);
		TestEqual(TEXT("The MOVED key's value survived the SetKeyTime topology"), Keys[0].Value, 7.f);
	}

	return true;
}

// D4 — Mode re-stamp: per-key interp set by the engine's RMB menu is coerced back to the per-curve
// Mode (SetMode semantics), and a pure-restamp pass still reports bChanged.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCurveTracksCoerceRestampsMode,
	"Paper2DPlus.CurveTracks.Coerce.RestampsMode",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCurveTracksCoerceRestampsMode::RunTest(const FString& Parameters)
{
	FRichCurve Curve;
	CurveTracks_AddRawKey(Curve, 1.0f, 1.f, RCIM_Cubic);   // engine RMB menu set per-key Cubic
	CurveTracks_AddRawKey(Curve, 4.0f, 0.f, RCIM_Linear);

	const Paper2DPlusCurveTracks::FCurveCoerceSummary Summary =
		Paper2DPlusCurveTracks::CoerceCurveKeysToFrames(Curve, 8, EPaper2DPlusCurveInterp::Constant);

	TestTrue(TEXT("Restamp-only pass reports bChanged"), Summary.bChanged);
	TestEqual(TEXT("No keys moved"), Summary.NumRounded + Summary.NumClamped + Summary.NumDeduped, 0);
	for (const FRichCurveKey& Key : Curve.GetConstRefOfKeys())
	{
		TestEqual(TEXT("Every key re-stamped to the per-curve Constant mode"),
			static_cast<int32>(Key.InterpMode.GetValue()), static_cast<int32>(RCIM_Constant));
	}

	return true;
}

// E17 — orphan detector: rounded frames outside [0, FrameCount-1], sorted; empty when clean.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCurveTracksOrphanDetector,
	"Paper2DPlus.CurveTracks.Orphans.Detector",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCurveTracksOrphanDetector::RunTest(const FString& Parameters)
{
	FRichCurve Curve;
	CurveTracks_AddRawKey(Curve, 12.0f, 1.f);
	CurveTracks_AddRawKey(Curve, 2.0f, 5.f);
	CurveTracks_AddRawKey(Curve, 9.0f, 9.f);
	CurveTracks_AddRawKey(Curve, -1.0f, 3.f);

	const TArray<int32> Orphans = Paper2DPlusCurveTracks::FindOrphanKeyFrames(Curve, /*FrameCount*/ 8);
	TestEqual(TEXT("Three orphans found"), Orphans.Num(), 3);
	if (Orphans.Num() == 3)
	{
		TestEqual(TEXT("Sorted: -1 first"), Orphans[0], -1);
		TestEqual(TEXT("Then 9"), Orphans[1], 9);
		TestEqual(TEXT("Then 12"), Orphans[2], 12);
	}

	FRichCurve Clean;
	CurveTracks_AddRawKey(Clean, 2.0f, 5.f);
	TestEqual(TEXT("Clean curve has no orphans"), Paper2DPlusCurveTracks::FindOrphanKeyFrames(Clean, 8).Num(), 0);

	return true;
}

// F3 (CT-4/ADV-3) — FrameCount <= 0 means "orphan state UNKNOWN" (no flipbook resolved), NOT
// "everything is an orphan": the detector returns EMPTY so the prune affordance can never offer to
// delete every key; contextual Details surfaces a distinct frames-unavailable state instead.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCurveTracksOrphanFrameCountZero,
	"Paper2DPlus.CurveTracks.Orphans.FrameCountZeroIsUnknown",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCurveTracksOrphanFrameCountZero::RunTest(const FString& Parameters)
{
	FRichCurve Curve;
	CurveTracks_AddRawKey(Curve, 2.0f, 5.f);
	CurveTracks_AddRawKey(Curve, 9.0f, 1.f);

	TestEqual(TEXT("FrameCount 0 reports NO orphans (state unknown)"),
		Paper2DPlusCurveTracks::FindOrphanKeyFrames(Curve, 0).Num(), 0);
	TestEqual(TEXT("Negative FrameCount reports NO orphans too"),
		Paper2DPlusCurveTracks::FindOrphanKeyFrames(Curve, -3).Num(), 0);
	// Sanity: the same curve WITH a resolved band reports the real orphan.
	TestEqual(TEXT("FrameCount 8 reports the genuine orphan"),
		Paper2DPlusCurveTracks::FindOrphanKeyFrames(Curve, 8).Num(), 1);

	return true;
}

// U2 — shared seeding + color hash: known curves resolve semantic authoring metadata (Cancel_*
// fixed step windows, HitStop whole-frame counts); Paper2DPlusCurveTracks::NameToColor is THE single hash
// (the legacy Paper2DPlusCurveGraph forwarder died with the Curves tab, retired in curves PR C —
// the old parity assertion became the determinism/case-insensitivity checks below).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCurveTracksSharedPickerSeeds,
	"Paper2DPlus.CurveTracks.Picker.SeedsAndColorHash",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCurveTracksSharedPickerSeeds::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("Cancel_Normal is a cancel curve name"),
		FFlipbookTransitionData::IsCancelCurveName(TEXT("Cancel_Normal")));
	TestTrue(TEXT("cancel_special detection is case-insensitive"),
		FFlipbookTransitionData::IsCancelCurveName(TEXT("cancel_special")));
	TestFalse(TEXT("HitStop is not a cancel curve name"),
		FFlipbookTransitionData::IsCancelCurveName(TEXT("HitStop")));
	TestTrue(TEXT("MakeCancelCurveName round-trips through IsCancelCurveName"),
		FFlipbookTransitionData::IsCancelCurveName(FFlipbookTransitionData::MakeCancelCurveName(TEXT("Normal"))));

	TestEqual(TEXT("Cancel_* seeds Constant mode"),
		FPaper2DPlusCurvePickerUtils::MakeSeededCurve(TEXT("Cancel_Normal")).Mode, EPaper2DPlusCurveInterp::Constant);
	TestEqual(TEXT("HitStop seeds Constant mode because frame counts should hold"),
		FPaper2DPlusCurvePickerUtils::MakeSeededCurve(TEXT("HitStop")).Mode, EPaper2DPlusCurveInterp::Constant);
	TestEqual(TEXT("Unknown names seed Linear mode"),
		FPaper2DPlusCurvePickerUtils::MakeSeededCurve(TEXT("UnknownScalar")).Mode, EPaper2DPlusCurveInterp::Linear);

	const FPaper2DPlusKnownCurve CancelMetadata =
		FPaper2DPlusCurvePickerUtils::ResolveCurveMetadata(TEXT("Cancel_Custom"));
	TestEqual(TEXT("Cancel_* fallback metadata is a step window"),
		CancelMetadata.Semantic, EPaper2DPlusKnownCurveSemantic::StepWindow);
	TestTrue(TEXT("Cancel_* fallback has fixed 0..1 range"),
		CancelMetadata.bUseFixedValueRange && CancelMetadata.ValueMin == 0.f && CancelMetadata.ValueMax == 1.f);
	TestTrue(TEXT("Cancel_* fallback snaps output values"), CancelMetadata.bSnapOutputValues);

	const FPaper2DPlusKnownCurve HitStopMetadata =
		FPaper2DPlusCurvePickerUtils::ResolveCurveMetadata(TEXT("HitStop"));
	TestEqual(TEXT("HitStop metadata is frame-count semantic"),
		HitStopMetadata.Semantic, EPaper2DPlusKnownCurveSemantic::FrameCount);
	TestEqual(TEXT("HitStop output snap is one frame"), HitStopMetadata.OutputSnap, 1.f);
	TestTrue(TEXT("HitStop snaps output values"), HitStopMetadata.bSnapOutputValues);
	TestEqual(TEXT("HitStop units are frames"), HitStopMetadata.Units, FString(TEXT("frames")));

	const FPaper2DPlusKnownCurve ArmorMetadata =
		FPaper2DPlusCurvePickerUtils::ResolveCurveMetadata(TEXT("Armor"));
	TestEqual(TEXT("Armor metadata is boolean step semantic"),
		ArmorMetadata.Semantic, EPaper2DPlusKnownCurveSemantic::BooleanStep);
	TestTrue(TEXT("Armor uses fixed 0..1 range"),
		ArmorMetadata.bUseFixedValueRange && ArmorMetadata.ValueMin == 0.f && ArmorMetadata.ValueMax == 1.f);

	float SharedOutputSnap = 0.f;
	TArray<FName> SharedStepCurves;
	SharedStepCurves.Add(FName(TEXT("Cancel_Normal")));
	SharedStepCurves.Add(FName(TEXT("Cancel_Special")));
	SharedStepCurves.Add(FName(TEXT("Armor")));
	TestTrue(TEXT("Shared step/boolean lanes keep output snapping when snaps agree"),
		FPaper2DPlusCurvePickerUtils::ResolveSharedOutputSnap(SharedStepCurves, SharedOutputSnap));
	TestEqual(TEXT("Shared snap is one value step"), SharedOutputSnap, 1.f);

	TArray<FName> MixedCurves;
	MixedCurves.Add(FName(TEXT("HitStop")));
	MixedCurves.Add(FName(TEXT("Damage")));
	TestFalse(TEXT("Mixed continuous curves disable shared output snapping"),
		FPaper2DPlusCurvePickerUtils::ResolveSharedOutputSnap(MixedCurves, SharedOutputSnap));

	TArray<FName> NoCurves;
	TestFalse(TEXT("Empty shared curve list does not enable output snapping"),
		FPaper2DPlusCurvePickerUtils::ResolveSharedOutputSnap(NoCurves, SharedOutputSnap));

	// Color hash: deterministic, name-sensitive, case-insensitive (the coverage the retired
	// CurvesPanel graph test carried — now asserted directly against the single shared hash).
	const FName Probe(TEXT("HitStop"));
	TestTrue(TEXT("Same name -> same color"),
		Paper2DPlusCurveTracks::NameToColor(Probe).Equals(Paper2DPlusCurveTracks::NameToColor(Probe)));
	TestTrue(TEXT("Different names -> different colors"),
		!Paper2DPlusCurveTracks::NameToColor(Probe).Equals(Paper2DPlusCurveTracks::NameToColor(FName(TEXT("Armor")))));
	TestTrue(TEXT("Color is case-insensitive"),
		Paper2DPlusCurveTracks::NameToColor(FName(TEXT("hitstop"))).Equals(Paper2DPlusCurveTracks::NameToColor(Probe)));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCurvePresentationStateTest,
	"Paper2DPlus.CurveTracks.Presentation.VisibilitySoloSelectionAndRename",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCurvePresentationStateTest::RunTest(const FString& Parameters)
{
	const FName Armor(TEXT("Armor"));
	const FName Damage(TEXT("Damage"));
	const FName Guard(TEXT("Guard"));
	const FName HitStop(TEXT("HitStop"));
	TArray<FName> Authored;
	Authored.Add(Armor);
	Authored.Add(Damage);

	Paper2DPlusCurveTracks::FCurvePresentationState State;
	State.Reconcile(Authored);
	TestTrue(TEXT("new Armor curve starts visible"), State.IsVisible(Armor));
	TestTrue(TEXT("new Damage curve starts visible"), State.IsVisible(Damage));

	State.ToggleVisibility(Armor);
	State.Select(Armor);
	State.ToggleSolo(Damage);
	TestFalse(TEXT("solo hides a base-hidden non-solo curve"), State.IsVisible(Armor));
	TestTrue(TEXT("solo curve is visible"), State.IsVisible(Damage));
	TestTrue(TEXT("solo identity is explicit"), State.IsSoloed(Damage));
	TestEqual(TEXT("selection is independent from solo"), State.GetSelectedCurve(), Armor);

	State.ExitSoloForRetarget();
	TestFalse(TEXT("leaving solo restores Armor's hidden base state"), State.IsVisible(Armor));
	TestTrue(TEXT("leaving solo restores Damage's visible base state"), State.IsVisible(Damage));

	State.Rename(Armor, Guard);
	TestFalse(TEXT("rename removes the old presentation identity"), State.IsKnown(Armor));
	TestTrue(TEXT("rename keeps the hidden state on the new identity"), State.IsKnown(Guard) && !State.IsVisible(Guard));
	TestEqual(TEXT("rename remaps primary selection"), State.GetSelectedCurve(), Guard);

	State.Add(HitStop);
	TestTrue(TEXT("explicitly added curve starts visible"), State.IsVisible(HitStop));
	State.Remove(Guard);
	TestTrue(TEXT("removing selected curve clears primary selection"), State.GetSelectedCurve().IsNone());

	Authored.Reset();
	Authored.Add(Damage);
	Authored.Add(HitStop);
	Authored.Add(Armor);
	State.Reconcile(Authored);
	TestTrue(TEXT("a newly reconciled authored identity starts visible"), State.IsVisible(Armor));
	TestEqual(TEXT("visible projection preserves authored order"), State.GetVisibleCurves(Authored), Authored);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCurvePresentationScopeIsolationTest,
	"Paper2DPlus.CurveTracks.Presentation.ProfileAnimationScopeIsolation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCurvePresentationScopeIsolationTest::RunTest(const FString& Parameters)
{
	auto MakeAsset = [](const TCHAR* AssetName) -> UPaper2DPlusCharacterProfileAsset*
	{
		UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>(
			GetTransientPackage(),
			MakeUniqueObjectName(GetTransientPackage(), UPaper2DPlusCharacterProfileAsset::StaticClass(), FName(AssetName)),
			RF_Transactional);
		Asset->Flipbooks.AddDefaulted();
		Asset->Flipbooks[0].Identity.FlipbookName = TEXT("Attack");
		Asset->Flipbooks[0].CombatData.Frames.SetNum(5);
		Asset->Flipbooks[0].CurveData.Curves.Add(TEXT("Damage"));
		Asset->Flipbooks[0].CurveData.Curves.Add(TEXT("HitStop"));
		return Asset;
	};

	UPaper2DPlusCharacterProfileAsset* FirstAsset = MakeAsset(TEXT("CurveScopeFirst"));
	UPaper2DPlusCharacterProfileAsset* SecondAsset = MakeAsset(TEXT("CurveScopeSecond"));
	TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset> CurrentAsset = FirstAsset;

	TSharedPtr<SCurveTrackStack> Stack = SNew(SCurveTrackStack)
		.Asset_Lambda([&CurrentAsset]() { return CurrentAsset; })
		.SelectedFlipbookIndex(0)
		.SelectedFrameIndex(0);
	Stack->RebuildRowsImmediatelyForTests();
	Stack->ToggleCurveVisibility(TEXT("HitStop"));
	Stack->ToggleCurveSolo(TEXT("Damage"));
	Stack->SelectCurve(TEXT("HitStop"));
	Stack->RebuildRowsImmediatelyForTests();
	TestTrue(TEXT("first scope has Damage soloed"), Stack->IsCurveSoloed(TEXT("Damage")));
	TestFalse(TEXT("first scope keeps HitStop base-hidden"), Stack->IsCurveVisible(TEXT("HitStop")));

	CurrentAsset = SecondAsset;
	Stack->RefreshTracks(true);
	Stack->RebuildRowsImmediatelyForTests();
	TestTrue(TEXT("second Profile starts with Damage visible"), Stack->IsCurveVisible(TEXT("Damage")));
	TestTrue(TEXT("second Profile starts with HitStop visible"), Stack->IsCurveVisible(TEXT("HitStop")));
	TestFalse(TEXT("solo does not leak into the second Profile"), Stack->IsCurveSoloed(TEXT("Damage")));
	TestTrue(TEXT("selection does not leak into the second Profile"), Stack->GetSelectedCurve().IsNone());

	CurrentAsset = FirstAsset;
	Stack->RefreshTracks(true);
	Stack->RebuildRowsImmediatelyForTests();
	TestFalse(TEXT("retarget permanently exits the old scope's solo"), Stack->IsCurveSoloed(TEXT("Damage")));
	TestFalse(TEXT("base visibility survives a round-trip retarget"), Stack->IsCurveVisible(TEXT("HitStop")));
	TestEqual(TEXT("primary selection remains isolated/restorable per scope"), Stack->GetSelectedCurve(), FName(TEXT("HitStop")));
	Stack->HandleHostDeactivated();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCurveTrackRenameTest,
	"Paper2DPlus.CurveTracks.Authoring.RenameValidationAndUndo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCurveTrackRenameTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>(
		GetTransientPackage(),
		MakeUniqueObjectName(GetTransientPackage(), UPaper2DPlusCharacterProfileAsset::StaticClass(), TEXT("CurveRenameAsset")),
		RF_Transactional);
	Asset->Flipbooks.AddDefaulted();
	Asset->Flipbooks[0].Identity.FlipbookName = TEXT("Attack");
	Asset->Flipbooks[0].CombatData.Frames.SetNum(6);
	FPaper2DPlusFrameCurve& HitStop = Asset->Flipbooks[0].CurveData.Curves.Add(TEXT("HitStop"));
	HitStop.Mode = EPaper2DPlusCurveInterp::Constant;
	HitStop.SetKeyValue(2, 3.0f);
	HitStop.SetKeyValue(9, 4.0f);
	Asset->Flipbooks[0].CurveData.Curves.Add(TEXT("Damage"));
	Asset->Flipbooks[0].CurveData.Curves.Add(TEXT("Temporary"));

	TSharedPtr<SCurveTrackStack> Stack = SNew(SCurveTrackStack)
		.Asset(TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset>(Asset))
		.SelectedFlipbookIndex(0)
		.SelectedFrameIndex(0)
		.CanEditCurves(true);
	Stack->RebuildRowsImmediatelyForTests();
	TestTrue(TEXT("editable Damage row exposes a keyboard focus target"),
		Stack->HasCurveRowKeyboardTargetForTests(TEXT("Damage")));
	TestTrue(TEXT("editable HitStop row exposes a keyboard focus target"),
		Stack->HasCurveRowKeyboardTargetForTests(TEXT("HitStop")));
	const TSharedRef<SWidget> Legend = Stack->DetachLegendContentForExternalLayout();
	TestEqual(TEXT("the compact legend contains one color identity per curve"),
		CurveTracks_CountWidgetType(Legend, TEXT("SColorBlock")), 3);
	TestEqual(TEXT("the compact legend contains one name identity per curve"),
		CurveTracks_CountWidgetType(Legend, TEXT("STextBlock")), 3);
	TestEqual(TEXT("curve mutation buttons moved out of the compact legend"),
		CurveTracks_CountWidgetType(Legend, TEXT("SButton")), 0);
	TestEqual(TEXT("interpolation dropdowns moved out of the compact legend"),
		CurveTracks_CountWidgetType(Legend, TEXT("SComboButton")), 0);
	TestEqual(TEXT("rename fields moved out of the compact legend"),
		CurveTracks_CountWidgetType(Legend, TEXT("SEditableText")), 0);
	TestTrue(TEXT("Profile Details may mutate the selected curve through the stack funnel"),
		Stack->CanMutateCurve(TEXT("HitStop")));
	TestEqual(TEXT("Details reads the authored interpolation mode through the stack"),
		Stack->GetCurveMode(TEXT("HitStop")), EPaper2DPlusCurveInterp::Constant);
	TestEqual(TEXT("Details surfaces the authored orphan key"),
		Stack->GetCurveOrphanFrames(TEXT("HitStop")), TArray<int32>({ 9 }));
	TestTrue(TEXT("Details can poll orphan presence without allocating the frame list"),
		Stack->HasCurveOrphans(TEXT("HitStop")));
	Stack->PruneOrphanKeys(TEXT("HitStop"));
	TestTrue(TEXT("Details prune routes through the stack's safe mutation funnel"),
		Stack->GetCurveOrphanFrames(TEXT("HitStop")).IsEmpty());
	TestFalse(TEXT("orphan presence clears after pruning"),
		Stack->HasCurveOrphans(TEXT("HitStop")));
	Stack->SetCurveMode(TEXT("Damage"), EPaper2DPlusCurveInterp::Cubic);
	TestEqual(TEXT("Details interpolation routes through the stack's transaction funnel"),
		Stack->GetCurveMode(TEXT("Damage")), EPaper2DPlusCurveInterp::Cubic);
	Stack->RemoveCurve(TEXT("Temporary"));
	TestFalse(TEXT("Details removal routes through the stack's TMap-safe teardown"),
		Asset->Flipbooks[0].CurveData.Curves.Contains(TEXT("Temporary")));
	Stack->SelectCurve(TEXT("Damage"));
	TestTrue(TEXT("keyboard Down advances selection in stable curve-name order"),
		Stack->MoveCurveSelectionForTests(1));
	TestEqual(TEXT("keyboard Down selects HitStop"), Stack->GetSelectedCurve(), FName(TEXT("HitStop")));
	TestFalse(TEXT("keyboard navigation does not wrap beyond the last row"),
		Stack->MoveCurveSelectionForTests(1));
	TestEqual(TEXT("boundary navigation keeps the last curve selected"),
		Stack->GetSelectedCurve(), FName(TEXT("HitStop")));
	TestTrue(TEXT("keyboard Up returns to the previous curve"),
		Stack->MoveCurveSelectionForTests(-1));
	TestEqual(TEXT("keyboard Up selects Damage"), Stack->GetSelectedCurve(), FName(TEXT("Damage")));
	Stack->SelectCurve(TEXT("HitStop"));

	FText Error;
	TestFalse(TEXT("empty rename is rejected"), Stack->RenameCurve(TEXT("HitStop"), TEXT("  "), &Error));
	TestFalse(TEXT("unchanged rename is rejected"), Stack->RenameCurve(TEXT("HitStop"), TEXT("HitStop"), &Error));
	TestFalse(TEXT("case-only rename is rejected"), Stack->RenameCurve(TEXT("HitStop"), TEXT("hitstop"), &Error));
	TestFalse(TEXT("case-insensitive collision is rejected"), Stack->RenameCurve(TEXT("HitStop"), TEXT("damage"), &Error));
	TestTrue(TEXT("validation failures leave the source curve present"),
		Asset->Flipbooks[0].CurveData.Curves.Contains(TEXT("HitStop")));

	TestTrue(TEXT("trimmed unique rename succeeds"),
		Stack->RenameCurve(TEXT("HitStop"), TEXT("  ImpactStrength  "), &Error));
	TestFalse(TEXT("old map key is gone"), Asset->Flipbooks[0].CurveData.Curves.Contains(TEXT("HitStop")));
	const FPaper2DPlusFrameCurve* Renamed = Asset->Flipbooks[0].CurveData.Curves.Find(TEXT("ImpactStrength"));
	if (TestNotNull(TEXT("new map key exists"), Renamed))
	{
		TestEqual(TEXT("curve mode survives rename"), Renamed->Mode, EPaper2DPlusCurveInterp::Constant);
		TestEqual(TEXT("curve keys survive rename"), Renamed->Eval(2.0f, 0.0f), 3.0f);
	}
	TestEqual(TEXT("primary selection follows rename"), Stack->GetSelectedCurve(), FName(TEXT("ImpactStrength")));

	if (TestNotNull(TEXT("GEditor is available for transactional rename proof"), GEditor))
	{
		TestTrue(TEXT("one Undo restores the old curve identity"), GEditor->UndoTransaction(true));
		TestTrue(TEXT("Undo restored HitStop"), Asset->Flipbooks[0].CurveData.Curves.Contains(TEXT("HitStop")));
		TestFalse(TEXT("Undo removed ImpactStrength"), Asset->Flipbooks[0].CurveData.Curves.Contains(TEXT("ImpactStrength")));
		TestTrue(TEXT("one Redo reapplies the rename"), GEditor->RedoTransaction());
		TestTrue(TEXT("Redo restored ImpactStrength"), Asset->Flipbooks[0].CurveData.Curves.Contains(TEXT("ImpactStrength")));
	}
	Stack->HandleHostDeactivated();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCurveTrackReadOnlyAndZeroFrameTest,
	"Paper2DPlus.CurveTracks.LayerReadOnly.RealCurvesAndZeroFrameGeometry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCurveTrackReadOnlyAndZeroFrameTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>(
		GetTransientPackage(),
		MakeUniqueObjectName(GetTransientPackage(), UPaper2DPlusCharacterProfileAsset::StaticClass(), TEXT("ReadOnlyCurveAsset")),
		RF_Transactional);
	Asset->Flipbooks.AddDefaulted();
	Asset->Flipbooks[0].Identity.FlipbookName = TEXT("EmptyAttack");
	FPaper2DPlusFrameCurve& Curve = Asset->Flipbooks[0].CurveData.Curves.Add(TEXT("Damage"));
	Curve.SetKeyValue(0, 2.0f);

	TSharedPtr<SCurveTrackStack> Stack = SNew(SCurveTrackStack)
		.Asset(TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset>(Asset))
		.SelectedFlipbookIndex(0)
		.SelectedFrameIndex(0)
		.CanEditCurves(false);
	Stack->RebuildRowsImmediatelyForTests();
	TestTrue(TEXT("read-only Layer row keeps keyboard selection/navigation"),
		Stack->HasCurveRowKeyboardTargetForTests(TEXT("Damage")));
	TestEqual(TEXT("read-only Layer projection exposes the real authored curve name"),
		Stack->GetAuthoredCurveNamesForTests(), TArray<FName>({ FName(TEXT("Damage")) }));
	TestEqual(TEXT("read-only graph initially projects the real curve"),
		Stack->GetGraphCurveNamesForTests(), TArray<FName>({ FName(TEXT("Damage")) }));
	TestFalse(TEXT("zero-frame graph is noninteractive"), Stack->IsCurveGraphInteractiveForTests());
	TestEqual(TEXT("zero-frame graph uses the shared non-timing body width"),
		Stack->GetCurveBodyWidthForTests(),
		Paper2DPlusFrameCueTimeline::FTimingGeometry::MinimumNonTimingBodyWidth);

	FText RenameError;
	TestFalse(TEXT("read-only mode rejects asset rename"),
		Stack->RenameCurve(TEXT("Damage"), TEXT("DamageRenamed"), &RenameError));
	TestFalse(TEXT("Layer Details reports asset mutation controls as read-only"),
		Stack->CanMutateCurve(TEXT("Damage")));
	Stack->SetCurveMode(TEXT("Damage"), EPaper2DPlusCurveInterp::Cubic);
	Stack->PruneOrphanKeys(TEXT("Damage"));
	Stack->RemoveCurve(TEXT("Damage"));
	TestTrue(TEXT("read-only rejection leaves asset data byte-shape intact"),
		Asset->Flipbooks[0].CurveData.Curves.Contains(TEXT("Damage"))
		&& !Asset->Flipbooks[0].CurveData.Curves.Contains(TEXT("DamageRenamed"))
		&& Asset->Flipbooks[0].CurveData.Curves.FindChecked(TEXT("Damage")).Mode
			== EPaper2DPlusCurveInterp::Linear);
	Stack->ToggleCurveVisibility(TEXT("Damage"));
	TestFalse(TEXT("presentation visibility remains usable in read-only mode"), Stack->IsCurveVisible(TEXT("Damage")));
	Stack->ToggleCurveSolo(TEXT("Damage"));
	TestTrue(TEXT("presentation solo remains usable in read-only mode"), Stack->IsCurveSoloed(TEXT("Damage")));
	Stack->SelectCurve(TEXT("Damage"));
	TestEqual(TEXT("presentation selection remains usable in read-only mode"),
		Stack->GetSelectedCurve(), FName(TEXT("Damage")));

	Asset->Flipbooks[0].CombatData.Frames.SetNum(4);
	Stack->RefreshTracks(true);
	Stack->RebuildRowsImmediatelyForTests();
	TestEqual(TEXT("timed curve width stays aligned to the shared 52px frame geometry"),
		Stack->GetCurveBodyWidthForTests(),
		4.0f * Paper2DPlusFrameCueTimeline::FTimingGeometry::PixelsPerKeyFrame);
	TestFalse(TEXT("read-only timed graph still declines editing"), Stack->IsCurveGraphInteractiveForTests());
	Stack->HandleHostDeactivated();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCurveTrackRetargetTransactionOwnerTest,
	"Paper2DPlus.CurveTracks.Transactions.PendingCoerceKeepsSnapshotOwner",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCurveTrackRetargetTransactionOwnerTest::RunTest(const FString& Parameters)
{
	auto MakeAsset = [](const TCHAR* AssetName, float KeyTime, float KeyValue) -> UPaper2DPlusCharacterProfileAsset*
	{
		UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>(
			GetTransientPackage(),
			MakeUniqueObjectName(GetTransientPackage(), UPaper2DPlusCharacterProfileAsset::StaticClass(), FName(AssetName)),
			RF_Transactional);
		Asset->Flipbooks.AddDefaulted();
		Asset->Flipbooks[0].Identity.FlipbookName = TEXT("Attack");
		Asset->Flipbooks[0].CombatData.Frames.SetNum(5);
		FPaper2DPlusFrameCurve& Curve = Asset->Flipbooks[0].CurveData.Curves.Add(TEXT("HitStop"));
		CurveTracks_AddRawKey(Curve.Curve, KeyTime, KeyValue);
		return Asset;
	};

	UPaper2DPlusCharacterProfileAsset* FirstAsset = MakeAsset(TEXT("PendingCurveFirst"), 1.6f, 7.0f);
	UPaper2DPlusCharacterProfileAsset* SecondAsset = MakeAsset(TEXT("PendingCurveSecond"), 3.0f, 11.0f);
	TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset> CurrentAsset = FirstAsset;
	int32 Notifications = 0;
	TSharedPtr<SCurveTrackStack> Stack = SNew(SCurveTrackStack)
		.Asset_Lambda([&CurrentAsset]() { return CurrentAsset; })
		.SelectedFlipbookIndex(0)
		.SelectedFrameIndex(0)
		.CanEditCurves(true)
		.OnCurveListChanged(FSimpleDelegate::CreateLambda([&Notifications]() { ++Notifications; }));
	Stack->RebuildRowsImmediatelyForTests();
	TestTrue(TEXT("editable timed Profile mounts an interactive SCurveEditor owner"),
		Stack->IsCurveGraphInteractiveForTests());
	Stack->QueuePendingCoerceForTests(TEXT("HitStop"));

	// Retarget the live attributes without rebuilding: pending work must resolve exclusively through
	// the row-build adapter and enroll that adapter's original Profile in the transaction.
	CurrentAsset = SecondAsset;
	Stack->FlushPendingCoerceForTests();
	const TArray<TPair<int32, float>> FirstKeys = CurveTracks_Snapshot(
		FirstAsset->Flipbooks[0].CurveData.Curves.FindChecked(TEXT("HitStop")).Curve);
	const TArray<TPair<int32, float>> SecondKeys = CurveTracks_Snapshot(
		SecondAsset->Flipbooks[0].CurveData.Curves.FindChecked(TEXT("HitStop")).Curve);
	TestEqual(TEXT("pending key on the original Profile was rounded"), FirstKeys[0].Key, 2);
	TestEqual(TEXT("new Profile key was not touched"), SecondKeys[0].Key, 3);
	TestEqual(TEXT("transaction enrolled the original row-build Profile"),
		Stack->GetLastTransactionAssetForTests().Get(), FirstAsset);
	TestEqual(TEXT("old-scope coercion suppresses the new scope's model notification"), Notifications, 0);
	Stack->HandleHostDeactivated();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCurveTrackHostLifecycleTest,
	"Paper2DPlus.CurveTracks.HostDeactivationCancelsDeferredWorkWithoutLosingKeys",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCurveTrackHostLifecycleTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>(
		GetTransientPackage(), NAME_None, RF_Transactional);
	Asset->Flipbooks.AddDefaulted();
	Asset->Flipbooks[0].Identity.FlipbookName = TEXT("CurveLifecycle");
	Asset->Flipbooks[0].CombatData.Frames.SetNum(6);
	FPaper2DPlusFrameCurve& Curve =
		Asset->Flipbooks[0].CurveData.Curves.Add(TEXT("HitStop"));
	Curve.SetKeyValue(1, 0.25f);
	Curve.SetKeyValue(4, 0.75f);

	TSharedPtr<SCurveTrackStack> Stack = SNew(SCurveTrackStack)
		.Asset(TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset>(Asset))
		.SelectedFlipbookIndex(0)
		.SelectedFrameIndex(1);
	TestTrue(TEXT("initial structural attach owns one deferred rebuild timer"),
		Stack->HasActiveTimerForTests());
	Stack->HandleHostDeactivated();
	TestFalse(TEXT("host deactivation unregisters every curve-stack active timer"),
		Stack->HasActiveTimerForTests());
	TestFalse(TEXT("host deactivation leaves no pending coerce write"),
		Stack->HasPendingCoerceForTests());
	TestFalse(TEXT("host deactivation leaves no panel transaction"),
		Stack->HasActiveTransaction());

	const FPaper2DPlusFrameCurve* Preserved =
		Asset->Flipbooks[0].CurveData.Curves.Find(TEXT("HitStop"));
	if (TestNotNull(TEXT("authored curve survives host teardown"), Preserved))
	{
		TestEqual(TEXT("first authored key survives host teardown"), Preserved->Eval(1.0f, 0.0f), 0.25f);
		TestEqual(TEXT("second authored key survives host teardown"), Preserved->Eval(4.0f, 0.0f), 0.75f);
	}

	// Reopening/activation requests a fresh structural attachment. A second teardown must remain
	// idempotent and cancel that replacement timer too.
	Stack->RefreshTracks(/*bForceRebuild=*/ true);
	TestTrue(TEXT("reactivation can request a fresh attachment"), Stack->HasActiveTimerForTests());
	Stack->HandleHostDeactivated();
	Stack->HandleHostDeactivated();
	TestFalse(TEXT("repeated host deactivation remains timer-clean"),
		Stack->HasActiveTimerForTests());
	return true;
}

#endif // WITH_EDITOR
