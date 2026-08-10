// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#if WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "Paper2DPlusFrameCurve.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Paper2DPlusBlueprintLibrary.h"
#include "PaperFlipbook.h"
#include "PaperSprite.h"

/** TASK-74 auxiliary frame-curve tests (worldless). Covers the requirements-doc acceptance examples:
 *  AE1 (linear eval + missing-curve default), AE2 (constant/step), R4 (null asset / unknown move /
 *  unknown curve all return the default, no crash), and AE3 (exclude/restore preserves a point's value
 *  on its frame; excluding a DIFFERENT frame shifts the remaining points' anchors in lockstep).
 *  Helpers are FrameCurve_-prefixed per the unity-build file-unique-name rule. */

namespace
{
	/** Build a flipbook with NumFrames single-run key frames (frame index 0..N-1), owned by Owner. */
	UPaperFlipbook* FrameCurve_MakeFlipbook(UObject* Owner, int32 NumFrames)
	{
		UPaperFlipbook* FB = NewObject<UPaperFlipbook>(Owner);
		FScopedFlipbookMutator Mutator(FB);
		Mutator.FramesPerSecond = 10.0f;
		Mutator.KeyFrames.Empty();
		for (int32 i = 0; i < NumFrames; ++i)
		{
			FPaperFlipbookKeyFrame KF;
			KF.FrameRun = 1;
			KF.Sprite = NewObject<UPaperSprite>(FB);
			Mutator.KeyFrames.Add(KF);
		}
		return FB;
	}

	/** Add a flipbook entry named MoveName with a NumFrames-key flipbook; returns the live flipbook. */
	UPaperFlipbook* FrameCurve_AddMove(UPaper2DPlusCharacterProfileAsset* Asset, const FString& MoveName, int32 NumFrames)
	{
		FFlipbookProfileEntry Anim;
		Anim.Identity.FlipbookName = MoveName;
		UPaperFlipbook* FB = FrameCurve_MakeFlipbook(Asset, NumFrames);
		Anim.Identity.Flipbook = FB;
		Anim.CombatData.Frames.SetNum(NumFrames);
		Anim.CombatData.FrameExtractionInfo.SetNum(NumFrames);
		Asset->Flipbooks.Add(Anim);
		return FB;
	}
}

// AE1 — linear eval: 0@f0, 8@f6 -> 8 at frame 6, ~4 at frame 3; unauthored curve -> default.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCurveLinearEval,
	"Paper2DPlus.FrameCurve.Eval.Linear",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCurveLinearEval::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	FrameCurve_AddMove(Asset, TEXT("Attack"), 7);

	FPaper2DPlusFrameCurve& Curve = Asset->Flipbooks[0].CurveData.Curves.Add(TEXT("HitStop"));
	Curve.Mode = EPaper2DPlusCurveInterp::Linear;
	Curve.SetKeyValue(0, 0.f);
	Curve.SetKeyValue(6, 8.f);

	TestEqual(TEXT("Value at frame 6 is 8"),
		UPaper2DPlusBlueprintLibrary::GetMoveCurveValueAtFrame(Asset, TEXT("Attack"), TEXT("HitStop"), 6, 0.f), 8.f);
	TestEqual(TEXT("Value at frame 0 is 0"),
		UPaper2DPlusBlueprintLibrary::GetMoveCurveValueAtFrame(Asset, TEXT("Attack"), TEXT("HitStop"), 0, 0.f), 0.f);
	// Frame 3 is the midpoint of a 0->8 ramp across 6 frames => 4.
	TestEqual(TEXT("Value at frame 3 interpolates to ~4"),
		UPaper2DPlusBlueprintLibrary::GetMoveCurveValueAtFrame(Asset, TEXT("Attack"), TEXT("HitStop"), 3, 0.f), 4.f);

	// Unauthored curve name -> default.
	TestEqual(TEXT("Unauthored curve returns the supplied default"),
		UPaper2DPlusBlueprintLibrary::GetMoveCurveValueAtFrame(Asset, TEXT("Attack"), TEXT("Cancel"), 3, -1.f), -1.f);

	return true;
}

// AE2 — constant/step: 1@f2, 0@f5 -> 1 on frames 2..4, 0 on frame 5 (no ramp between).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCurveConstantEval,
	"Paper2DPlus.FrameCurve.Eval.Constant",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCurveConstantEval::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	FrameCurve_AddMove(Asset, TEXT("Charge"), 7);

	FPaper2DPlusFrameCurve& Curve = Asset->Flipbooks[0].CurveData.Curves.Add(TEXT("Armor"));
	Curve.Mode = EPaper2DPlusCurveInterp::Constant;
	Curve.SetKeyValue(2, 1.f);
	Curve.SetKeyValue(5, 0.f);

	TestEqual(TEXT("Frame 2 holds 1"),
		UPaper2DPlusBlueprintLibrary::GetMoveCurveValueAtFrame(Asset, TEXT("Charge"), TEXT("Armor"), 2, 0.f), 1.f);
	TestEqual(TEXT("Frame 3 still holds 1 (step, no ramp)"),
		UPaper2DPlusBlueprintLibrary::GetMoveCurveValueAtFrame(Asset, TEXT("Charge"), TEXT("Armor"), 3, 0.f), 1.f);
	TestEqual(TEXT("Frame 4 still holds 1"),
		UPaper2DPlusBlueprintLibrary::GetMoveCurveValueAtFrame(Asset, TEXT("Charge"), TEXT("Armor"), 4, 0.f), 1.f);
	TestEqual(TEXT("Frame 5 drops to 0"),
		UPaper2DPlusBlueprintLibrary::GetMoveCurveValueAtFrame(Asset, TEXT("Charge"), TEXT("Armor"), 5, 0.f), 0.f);

	return true;
}

// CROSS-VERSION — constant/step boundary must read the key's OWN value when sampled EXACTLY on a key,
// on every engine. UE <=5.2's FRichCurve EvalForTwoKeys constant branch returned the LEFT key's value at
// InTime == Key2.Time; UE 5.3+ fixed it to the key's own value. FPaper2DPlusFrameCurve::Eval evaluates
// constant curves manually so all of 5.0–5.8 agree (cancel/hit-stop/hit-window step curves depend on it).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCurveConstantBoundary,
	"Paper2DPlus.FrameCurve.Eval.ConstantBoundaryCrossVersion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCurveConstantBoundary::RunTest(const FString& Parameters)
{
	FPaper2DPlusFrameCurve Curve;
	Curve.Mode = EPaper2DPlusCurveInterp::Constant;
	Curve.SetKeyValue(2, 1.f);
	Curve.SetKeyValue(5, 0.f);

	// Before the first key -> first key's value.
	TestEqual(TEXT("Frame 0 (before first key) holds first key value 1"), Curve.Eval(0.f, -9.f), 1.f);
	// On/after the first key, before the second -> first key value (the step holds).
	TestEqual(TEXT("Frame 2 (on first key) is 1"), Curve.Eval(2.f, -9.f), 1.f);
	TestEqual(TEXT("Frame 4 (interior, just before step) is 1"), Curve.Eval(4.f, -9.f), 1.f);
	TestEqual(TEXT("Frame 4.999 (sub-frame before step) is 1"), Curve.Eval(4.999f, -9.f), 1.f);
	// EXACTLY on the second key -> that key's own value (the cross-version fix; UE<=5.2 returned 1 here).
	TestEqual(TEXT("Frame 5 (exactly on second key) drops to 0"), Curve.Eval(5.f, -9.f), 0.f);
	// After the last key -> last key value (post-infinity constant hold).
	TestEqual(TEXT("Frame 6 (after last key) holds 0"), Curve.Eval(6.f, -9.f), 0.f);

	// >= 0.5 gate (the cancel-window convention): closed before frame 5's step, then the key's own value.
	FPaper2DPlusFrameCurve Gate;
	Gate.Mode = EPaper2DPlusCurveInterp::Constant;
	Gate.SetKeyValue(0, 0.f); // start closed
	Gate.SetKeyValue(3, 1.f); // open at frame 3
	TestTrue(TEXT("Cancel gate OPEN exactly on its key frame 3 (>=0.5)"), Gate.Eval(3.f, 0.f) >= 0.5f);
	TestFalse(TEXT("Cancel gate CLOSED at frame 2 (<0.5)"), Gate.Eval(2.f, 0.f) >= 0.5f);
	return true;
}

// R4 — null asset / unknown move / unknown curve all return the default and never crash.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCurveMissingDefaults,
	"Paper2DPlus.FrameCurve.Eval.MissingReturnsDefault",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCurveMissingDefaults::RunTest(const FString& Parameters)
{
	// Null asset.
	TestEqual(TEXT("Null asset returns default"),
		UPaper2DPlusBlueprintLibrary::GetMoveCurveValueAtFrame(nullptr, TEXT("Attack"), TEXT("HitStop"), 3, 7.f), 7.f);

	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	FrameCurve_AddMove(Asset, TEXT("Attack"), 7);
	FPaper2DPlusFrameCurve& Curve = Asset->Flipbooks[0].CurveData.Curves.Add(TEXT("HitStop"));
	Curve.SetKeyValue(0, 5.f);

	// Unknown move.
	TestEqual(TEXT("Unknown move returns default"),
		UPaper2DPlusBlueprintLibrary::GetMoveCurveValueAtFrame(Asset, TEXT("DoesNotExist"), TEXT("HitStop"), 0, 9.f), 9.f);
	// Unknown curve on an existing move.
	TestEqual(TEXT("Unknown curve returns default"),
		UPaper2DPlusBlueprintLibrary::GetMoveCurveValueAtFrame(Asset, TEXT("Attack"), TEXT("Nope"), 0, 9.f), 9.f);

	// Curve type's own empty-curve guard returns the default.
	FPaper2DPlusFrameCurve Empty;
	TestEqual(TEXT("Empty curve Eval returns default"), Empty.Eval(3.f, 2.5f), 2.5f);

	return true;
}

// AE3a — excluding then restoring the frame a point sits on preserves the point's value on that frame.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCurveExcludeRestorePreserves,
	"Paper2DPlus.FrameCurve.Remap.ExcludeRestorePreserves",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCurveExcludeRestorePreserves::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	FrameCurve_AddMove(Asset, TEXT("Attack"), 7); // frames 0..6

	FPaper2DPlusFrameCurve& Curve = Asset->Flipbooks[0].CurveData.Curves.Add(TEXT("HitStop"));
	Curve.SetKeyValue(6, 8.f); // the point lives on frame 6

	// Exclude frame 6: the key on the removed frame is stashed, not in the live curve anymore.
	TestTrue(TEXT("Exclude frame 6 succeeds"), Asset->ExcludeFlipbookFrame(0, 6));
	TestEqual(TEXT("Live curve has no key after excluding its only frame"),
		Asset->Flipbooks[0].CurveData.Curves[TEXT("HitStop")].Curve.GetNumKeys(), 0);
	TestEqual(TEXT("One excluded frame stored"), Asset->GetExcludedFlipbookFrameCount(0), 1);
	TestTrue(TEXT("Stashed curve point recorded on the excluded frame"),
		Asset->Flipbooks[0].CombatData.ExcludedFrames[0].StashedCurvePoints.Contains(TEXT("HitStop")));

	// Restore: the value reattaches onto the restored frame (frame 6 again).
	TestTrue(TEXT("Restore succeeds"), Asset->RestoreExcludedFlipbookFrame(0, 0));
	const FPaper2DPlusFrameCurve& Restored = Asset->Flipbooks[0].CurveData.Curves[TEXT("HitStop")];
	TestEqual(TEXT("Restored curve has the key back"), Restored.Curve.GetNumKeys(), 1);
	TestEqual(TEXT("Restored value is preserved on frame 6"), Restored.Eval(6.f, 0.f), 8.f);

	return true;
}

// AE3b — excluding a DIFFERENT frame shifts remaining points' anchors in lockstep so each value stays
// on its original frame's data. Two points: 8@f6 and 3@f2. Excluding frame 1 shifts both down by one
// (point at f6 -> f5, point at f2 -> f1) while their values are unchanged.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCurveExcludeOtherShifts,
	"Paper2DPlus.FrameCurve.Remap.ExcludeOtherShiftsAnchors",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCurveExcludeOtherShifts::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	FrameCurve_AddMove(Asset, TEXT("Attack"), 7); // frames 0..6

	FPaper2DPlusFrameCurve& Curve = Asset->Flipbooks[0].CurveData.Curves.Add(TEXT("HitStop"));
	Curve.SetKeyValue(2, 3.f);
	Curve.SetKeyValue(6, 8.f);

	// Exclude frame 1 (no curve point on it). The two points at f2 and f6 should each shift down by one.
	TestTrue(TEXT("Exclude frame 1 succeeds"), Asset->ExcludeFlipbookFrame(0, 1));
	TestEqual(TEXT("No curve point was stashed (frame 1 had none)"),
		Asset->Flipbooks[0].CombatData.ExcludedFrames[0].StashedCurvePoints.Num(), 0);

	const FPaper2DPlusFrameCurve& Shifted = Asset->Flipbooks[0].CurveData.Curves[TEXT("HitStop")];
	TestEqual(TEXT("Still two keys after shift"), Shifted.Curve.GetNumKeys(), 2);
	// Point formerly at frame 2 now reads its value at frame 1; formerly frame 6 now at frame 5.
	TestEqual(TEXT("Value 3 now sits on frame 1"), Shifted.Eval(1.f, -1.f), 3.f);
	TestEqual(TEXT("Value 8 now sits on frame 5"), Shifted.Eval(5.f, -1.f), 8.f);

	return true;
}

// R1 — a populated curve survives the JSON export/import round-trip (values AND per-curve interp mode).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCurveJsonRoundTrip,
	"Paper2DPlus.FrameCurve.Json.RoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCurveJsonRoundTrip::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Src = NewObject<UPaper2DPlusCharacterProfileAsset>();
	// F18: keep this worldless test object (and its flipbook/sprite subobjects) GC-rooted across the JSON
	// export+import. Unrooted, UE 5.0's GC collects it mid-serialize -> use-after-free AV (5.1+ happens not
	// to; real assets live in packages so are always rooted). Proven by toggling rooting on 5.0.
	Src->AddToRoot();
	FrameCurve_AddMove(Src, TEXT("Attack"), 7);
	FPaper2DPlusFrameCurve& Curve = Src->Flipbooks[0].CurveData.Curves.Add(TEXT("Armor"));
	Curve.Mode = EPaper2DPlusCurveInterp::Constant; // distinct from the default so we can prove the mode survives
	Curve.SetKeyValue(2, 1.f);
	Curve.SetKeyValue(5, 0.f);

	FString Json;
	TestTrue(TEXT("Export to JSON succeeds"), Src->ExportToJsonString(Json));

	UPaper2DPlusCharacterProfileAsset* Dst = NewObject<UPaper2DPlusCharacterProfileAsset>();
	Dst->AddToRoot();
	TestTrue(TEXT("Import from JSON succeeds"), Dst->ImportFromJsonString(Json));
	Src->RemoveFromRoot();
	Dst->RemoveFromRoot();

	// Values survive.
	TestEqual(TEXT("Imported value at frame 2 is 1"),
		UPaper2DPlusBlueprintLibrary::GetMoveCurveValueAtFrame(Dst, TEXT("Attack"), TEXT("Armor"), 2, -1.f), 1.f);
	// Constant/step mode survives: frame 3 still reads 1 (a Linear flip would ramp toward 0).
	TestEqual(TEXT("Imported curve keeps Constant/step mode (frame 3 holds 1, not a ramp)"),
		UPaper2DPlusBlueprintLibrary::GetMoveCurveValueAtFrame(Dst, TEXT("Attack"), TEXT("Armor"), 3, -1.f), 1.f);

	return true;
}

// U1 (curve tracks PR B) — DeleteKeyAtFrame: removes exactly the key on the given frame, returns
// false when absent, leaves the other keys untouched. The single delete funnel that retires the
// editor's FKeyHandle scan (the TASK-84 5.1 break site).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCurveDeleteKeyAtFrame,
	"Paper2DPlus.FrameCurve.Write.DeleteKeyAtFrame",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCurveDeleteKeyAtFrame::RunTest(const FString& Parameters)
{
	FPaper2DPlusFrameCurve Curve;
	Curve.SetKeyValue(0, 1.f);
	Curve.SetKeyValue(3, 5.f);
	Curve.SetKeyValue(6, 9.f);

	TestTrue(TEXT("HasKeyAtFrame sees the key on frame 3"), Curve.HasKeyAtFrame(3));
	TestFalse(TEXT("HasKeyAtFrame is false for an empty frame"), Curve.HasKeyAtFrame(4));

	TestTrue(TEXT("Deleting the frame-3 key succeeds"), Curve.DeleteKeyAtFrame(3));
	TestEqual(TEXT("Two keys remain"), Curve.Curve.GetNumKeys(), 2);
	TestFalse(TEXT("Frame 3 no longer carries a key"), Curve.HasKeyAtFrame(3));
	TestEqual(TEXT("Frame 0 key untouched"), Curve.Eval(0.f, -1.f), 1.f);
	TestEqual(TEXT("Frame 6 key untouched"), Curve.Eval(6.f, -1.f), 9.f);

	TestFalse(TEXT("Deleting an absent frame returns false"), Curve.DeleteKeyAtFrame(3));
	TestEqual(TEXT("Failed delete mutates nothing"), Curve.Curve.GetNumKeys(), 2);

	return true;
}

// U1 (curve tracks PR B) — MoveKeyToFrame: value follows the move; absent source returns false;
// moving onto an OCCUPIED frame replaces the occupant (moved key wins); the moved key is re-stamped
// with the curve's Mode (the SetKeyValue write-funnel invariant).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCurveMoveKeyToFrame,
	"Paper2DPlus.FrameCurve.Write.MoveKeyToFrame",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCurveMoveKeyToFrame::RunTest(const FString& Parameters)
{
	// Basic move: value follows, source frame empties.
	FPaper2DPlusFrameCurve Curve;
	Curve.SetKeyValue(1, 5.f);
	TestTrue(TEXT("Move 1->4 succeeds"), Curve.MoveKeyToFrame(1, 4));
	TestEqual(TEXT("Still exactly one key"), Curve.Curve.GetNumKeys(), 1);
	TestFalse(TEXT("Frame 1 emptied"), Curve.HasKeyAtFrame(1));
	TestEqual(TEXT("Value 5 now sits on frame 4"), Curve.Eval(4.f, -1.f), 5.f);

	// Absent source: returns false, mutates nothing.
	TestFalse(TEXT("Moving from an empty frame returns false"), Curve.MoveKeyToFrame(2, 5));
	TestEqual(TEXT("Failed move mutates nothing"), Curve.Curve.GetNumKeys(), 1);

	// Same-frame move: successful position no-op.
	TestTrue(TEXT("Same-frame move is a successful no-op"), Curve.MoveKeyToFrame(4, 4));
	TestEqual(TEXT("No-op keeps the key"), Curve.Eval(4.f, -1.f), 5.f);

	// F12d — same-frame move STILL re-stamps Mode (the write lands through SetKeyValue even when the
	// position doesn't change, per the function's doc claim): a raw-added Linear key on a
	// Constant-mode curve picks up Constant from a same-frame move.
	FPaper2DPlusFrameCurve SameFrame;
	SameFrame.Mode = EPaper2DPlusCurveInterp::Constant;
	SameFrame.Curve.AddKey(2.f, 4.f); // deliberately bypasses SetKeyValue: key carries RCIM_Linear
	TestTrue(TEXT("Same-frame move of the raw key succeeds"), SameFrame.MoveKeyToFrame(2, 2));
	const TArray<FRichCurveKey> SameFrameKeys = SameFrame.Curve.GetCopyOfKeys();
	TestEqual(TEXT("Same-frame move keeps exactly one key"), SameFrameKeys.Num(), 1);
	if (SameFrameKeys.Num() == 1)
	{
		TestEqual(TEXT("Same-frame move kept the value"), SameFrameKeys[0].Value, 4.f);
		TestEqual(TEXT("Same-frame move re-stamped the curve's Constant mode"),
			static_cast<int32>(SameFrameKeys[0].InterpMode.GetValue()), static_cast<int32>(RCIM_Constant));
	}

	// Replace-on-collision: the moved key wins, key count shrinks by one.
	FPaper2DPlusFrameCurve Collide;
	Collide.SetKeyValue(2, 3.f);
	Collide.SetKeyValue(5, 8.f);
	TestTrue(TEXT("Move 2->5 onto an occupied frame succeeds"), Collide.MoveKeyToFrame(2, 5));
	TestEqual(TEXT("Collision deduped to one key"), Collide.Curve.GetNumKeys(), 1);
	TestEqual(TEXT("The MOVED key's value won the collision"), Collide.Eval(5.f, -1.f), 3.f);

	// Mode re-stamp: a raw-added key (Linear default interp) picks up the curve's Constant Mode on move.
	FPaper2DPlusFrameCurve Stamped;
	Stamped.Mode = EPaper2DPlusCurveInterp::Constant;
	Stamped.Curve.AddKey(1.f, 7.f); // deliberately bypasses SetKeyValue: key carries RCIM_Linear
	TestTrue(TEXT("Move of the raw key succeeds"), Stamped.MoveKeyToFrame(1, 3));
	const TArray<FRichCurveKey> StampedKeys = Stamped.Curve.GetCopyOfKeys();
	TestEqual(TEXT("One key after the stamped move"), StampedKeys.Num(), 1);
	if (StampedKeys.Num() == 1)
	{
		TestEqual(TEXT("Moved key was re-stamped with the curve's Constant mode"),
			static_cast<int32>(StampedKeys[0].InterpMode.GetValue()), static_cast<int32>(RCIM_Constant));
	}

	return true;
}

// R10 (move) — moving a key frame remaps a curve point in lockstep (the From<To permutation branch).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCurveMoveRemaps,
	"Paper2DPlus.FrameCurve.Remap.MoveShiftsPoint",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCurveMoveRemaps::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	FrameCurve_AddMove(Asset, TEXT("Attack"), 7); // frames 0..6

	FPaper2DPlusFrameCurve& Curve = Asset->Flipbooks[0].CurveData.Curves.Add(TEXT("HitStop"));
	Curve.SetKeyValue(1, 5.f); // point on frame 1

	// Move frame 1 -> 4: the point that lived on frame 1 follows to frame 4 with its value intact.
	TestTrue(TEXT("Move frame 1->4 succeeds"), Asset->MoveFlipbookFrame(0, 1, 4));
	const TArray<FRichCurveKey> Keys = Asset->Flipbooks[0].CurveData.Curves[TEXT("HitStop")].Curve.GetCopyOfKeys();
	TestEqual(TEXT("Still exactly one key after the move"), Keys.Num(), 1);
	if (Keys.Num() == 1)
	{
		TestEqual(TEXT("Key's frame anchor moved 1 -> 4"), FMath::RoundToInt(Keys[0].Time), 4);
		TestEqual(TEXT("Key's value is preserved"), Keys[0].Value, 5.f);
	}

	return true;
}

#endif // WITH_EDITOR
