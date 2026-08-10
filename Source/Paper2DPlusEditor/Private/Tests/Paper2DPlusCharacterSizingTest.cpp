// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#if WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "CharacterSizingFit.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Paper2DPlusLayerDraw.h"
#include "PaperFlipbook.h"
#include "PaperSprite.h"
#include "ProfileTools/SProfileCharacterSizingTool.h"
#include "UObject/Package.h"

namespace
{
	/** A profile with one measurable animation, for the gesture tests. */
	UPaper2DPlusCharacterProfileAsset* Sizing_MakeProfile()
	{
		UPaper2DPlusCharacterProfileAsset* Profile =
			NewObject<UPaper2DPlusCharacterProfileAsset>(GetTransientPackage());
		FFlipbookProfileEntry Entry;
		Entry.Identity.FlipbookName = TEXT("Idle");
		UPaperFlipbook* Flipbook = NewObject<UPaperFlipbook>(Profile);
		{
			FScopedFlipbookMutator Mutator(Flipbook);
			Mutator.FramesPerSecond = 10.0f;
			Mutator.KeyFrames.Empty();
			FPaperFlipbookKeyFrame KF;
			KF.FrameRun = 1;
			KF.Sprite = NewObject<UPaperSprite>(Flipbook);
			Mutator.KeyFrames.Add(KF);
		}
		Entry.Identity.Flipbook = Flipbook;
		Entry.CombatData.Frames.SetNum(1);
		Entry.CombatData.FrameExtractionInfo.SetNum(1);
		Profile->Flipbooks.Add(MoveTemp(Entry));
		return Profile;
	}
}

/*
 * TASK-156 U6 -- the pure Character Sizing fit computation and its pixel-to-world conversion.
 *
 * Worldless and hand-calculated. The fit is the piece that is provably right or wrong, so it is
 * pinned against arithmetic rather than against whatever the surface happens to draw.
 *
 * The conversion half exists because the sanctioned OFFSET conversion is wrong for magnitudes: it
 * negates X when facing left and computes Z as -OffsetPx.Y, so feeding it a pixel height returns a
 * NEGATIVE world height and a left-facing preview mirrors a measured width. Both failures are
 * asserted directly below, against the offset conversion, so the reason the sibling exists cannot be
 * lost to a later "simplification".
 *
 * Helpers are Sizing_-prefixed per the unity-build file-unique-name rule.
 */

// =============================================================================
// KTD6 -- the magnitude conversion, and why it is not the offset conversion.
// =============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCharacterSizingSizeConversion,
	"Paper2DPlus.CharacterSizing.PixelSizeConversionDropsEverySign",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCharacterSizingSizeConversion::RunTest(const FString& Parameters)
{
	// 64 px tall at 2 px/unit = 32 uu.
	{
		const FVector2D Size = Paper2DPlusLayerDraw::PixelSizeToWorld(
			FVector2D(32.0f, 64.0f), 2.0f, FVector::OneVector);
		TestEqual(TEXT("width converts by pixels-per-unit"), (float)Size.X, 16.0f);
		TestEqual(TEXT("height converts by pixels-per-unit"), (float)Size.Y, 32.0f);
	}

	// Scale multiplies both axes; X uses |scale.X| and Y uses |scale.Z|.
	{
		const FVector2D Size = Paper2DPlusLayerDraw::PixelSizeToWorld(
			FVector2D(10.0f, 20.0f), 1.0f, FVector(3.0f, 1.0f, 4.0f));
		TestEqual(TEXT("width scales by scale.X"), (float)Size.X, 30.0f);
		TestEqual(TEXT("height scales by scale.Z"), (float)Size.Y, 80.0f);
	}

	// THE POINT. A mirrored character is not negatively wide, and a height is never negative.
	{
		const FVector2D Mirrored = Paper2DPlusLayerDraw::PixelSizeToWorld(
			FVector2D(10.0f, 20.0f), 1.0f, FVector(-3.0f, 1.0f, -4.0f));
		TestEqual(TEXT("a negative scale still yields a positive width"), (float)Mirrored.X, 30.0f);
		TestEqual(TEXT("and a positive height"), (float)Mirrored.Y, 80.0f);

		// The offset conversion, given the same height, produces the wrong sign -- which is exactly
		// the bug this sibling exists to avoid.
		const FVector AsOffset = Paper2DPlusLayerDraw::PixelOffsetToWorld(
			FVector2D(0.0f, 20.0f), 1.0f, FVector::OneVector, /*bFacingLeft=*/false);
		TestTrue(TEXT("the OFFSET conversion returns a negative Z for a positive pixel height"),
			AsOffset.Z < 0.0f);

		const FVector MirroredOffset = Paper2DPlusLayerDraw::PixelOffsetToWorld(
			FVector2D(10.0f, 0.0f), 1.0f, FVector::OneVector, /*bFacingLeft=*/true);
		TestTrue(TEXT("and mirrors X when facing left, which would flip a measured width"),
			MirroredOffset.X < 0.0f);
	}

	// Division-by-zero guard matches the offset conversion's clamp.
	{
		const FVector2D Clamped = Paper2DPlusLayerDraw::PixelSizeToWorld(
			FVector2D(1.0f, 1.0f), 0.0f, FVector::OneVector);
		TestTrue(TEXT("zero pixels-per-unit clamps to a finite result"),
			FMath::IsFinite(Clamped.X) && FMath::IsFinite(Clamped.Y));
		TestEqual(TEXT("clamped exactly like the offset conversion"), (float)Clamped.Y, 1000.0f);
	}
	return true;
}

// =============================================================================
// The fit itself, hand-calculated.
// =============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCharacterSizingFit,
	"Paper2DPlus.CharacterSizing.FitMatchesHandCalculation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCharacterSizingFit::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusCharacterSizing;

	// A 64 px tall silhouette at 1 px/unit is 64 uu tall. Target 192 uu (a 96 half-height capsule)
	// therefore needs a uniform scale of exactly 3.
	FFitInput In;
	In.SilhouetteHeightPx = 64.0f;
	In.SilhouetteWidthPx = 32.0f;
	In.PivotToSilhouetteBottomPx = 0.0f; // pivot already at the feet
	In.PixelsPerUnit = 1.0f;
	In.TargetHeightUU = 192.0f;

	const FFitResult Fit = ComputeFit(In);
	TestTrue(TEXT("a well-formed input fits"), Fit.bValid);
	TestEqual(TEXT("uniform scale is target / unscaled height"), (float)Fit.Scale.X, 3.0f);
	TestEqual(TEXT("scale is uniform across X and Z"), (float)Fit.Scale.Z, 3.0f);
	TestEqual(TEXT("depth is left alone"), (float)Fit.Scale.Y, 1.0f);
	TestEqual(TEXT("the resulting height is the target"), Fit.ResultingHeightUU, 192.0f);

	// Pivot already at the feet, so the pivot goes exactly on the ground line: -192/2.
	TestEqual(TEXT("a feet-pivot lands on the ground line"), (float)Fit.Location.Z, -96.0f);
	TestEqual(TEXT("and is horizontally centred"), (float)Fit.Location.X, 0.0f);

	// A pivot 64 px ABOVE the feet (i.e. at the top of a 64 px silhouette) must be lifted by the
	// scaled equivalent -- 64 px * scale 3 = 192 uu above the ground line.
	In.PivotToSilhouetteBottomPx = 64.0f;
	const FFitResult Lifted = ComputeFit(In);
	TestTrue(TEXT("the lifted-pivot case still fits"), Lifted.bValid);
	TestEqual(TEXT("the pivot is raised by the SCALED pivot-to-feet distance"),
		(float)Lifted.Location.Z, -96.0f + 192.0f);
	TestEqual(TEXT("and the height is unchanged by where the pivot sits"),
		Lifted.ResultingHeightUU, 192.0f);

	// Pixels-per-unit participates: 64 px at 2 px/unit is 32 uu, so target 192 needs scale 6.
	In.PivotToSilhouetteBottomPx = 0.0f;
	In.PixelsPerUnit = 2.0f;
	const FFitResult Dense = ComputeFit(In);
	TestEqual(TEXT("a denser sprite needs a proportionally larger scale"), (float)Dense.Scale.X, 6.0f);
	TestEqual(TEXT("and still reaches the target height"), Dense.ResultingHeightUU, 192.0f);
	return true;
}

// =============================================================================
// Fail-closed: the unconfigured profile is the NORMAL case, not an edge case.
// =============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCharacterSizingFailsClosed,
	"Paper2DPlus.CharacterSizing.UnconfiguredFitFailsClosed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCharacterSizingFailsClosed::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusCharacterSizing;

	FFitInput Base;
	Base.SilhouetteHeightPx = 64.0f;
	Base.SilhouetteWidthPx = 32.0f;
	Base.PixelsPerUnit = 1.0f;
	Base.TargetHeightUU = 192.0f;

	// No capsule and no typed height -- a fresh profile.
	{
		FFitInput In = Base;
		In.TargetHeightUU = 0.0f;
		const FFitResult Fit = ComputeFit(In);
		TestFalse(TEXT("no target height refuses to fit"), Fit.bValid);
		TestFalse(TEXT("and says why"), Fit.Reason.IsEmpty());
		TestEqual(TEXT("leaving an identity scale rather than a degenerate one"),
			(float)Fit.Scale.X, 1.0f);
		TestTrue(TEXT("and a finite location"), FMath::IsFinite(Fit.Location.Z));
	}

	// An empty animation -- nothing to measure.
	{
		FFitInput In = Base;
		In.SilhouetteHeightPx = 0.0f;
		const FFitResult Fit = ComputeFit(In);
		TestFalse(TEXT("an empty silhouette refuses to fit"), Fit.bValid);
		TestFalse(TEXT("and says why"), Fit.Reason.IsEmpty());
	}

	// A sprite reporting no pixels-per-unit.
	{
		FFitInput In = Base;
		In.PixelsPerUnit = 0.0f;
		const FFitResult Fit = ComputeFit(In);
		TestFalse(TEXT("zero pixels-per-unit refuses to fit"), Fit.bValid);
		TestFalse(TEXT("and says why"), Fit.Reason.IsEmpty());
	}

	// Negative inputs must not sneak through as "valid but inverted".
	{
		FFitInput In = Base;
		In.SilhouetteHeightPx = -10.0f;
		TestFalse(TEXT("a negative silhouette refuses to fit"), ComputeFit(In).bValid);
	}
	return true;
}

// =============================================================================
// The live height readout, which is what R9 is actually about.
// =============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCharacterSizingHeightReadout,
	"Paper2DPlus.CharacterSizing.HeightReadoutTracksScale",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCharacterSizingHeightReadout::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusCharacterSizing;

	TestEqual(TEXT("64 px at 1 ppu, scale 1"), ComputeHeightUU(64.0f, 1.0f, 1.0f), 64.0f);
	TestEqual(TEXT("scale 3 triples it"), ComputeHeightUU(64.0f, 1.0f, 3.0f), 192.0f);
	TestEqual(TEXT("2 ppu halves it"), ComputeHeightUU(64.0f, 2.0f, 1.0f), 32.0f);

	// A mirrored character is not negatively tall.
	TestEqual(TEXT("a negative scale still reads as a positive height"),
		ComputeHeightUU(64.0f, 1.0f, -3.0f), 192.0f);

	// The readout agrees with the fit: fitting to a target then reading the height returns it.
	FFitInput In;
	In.SilhouetteHeightPx = 50.0f;
	In.SilhouetteWidthPx = 20.0f;
	In.PixelsPerUnit = 1.0f;
	In.TargetHeightUU = 175.0f;
	const FFitResult Fit = ComputeFit(In);
	TestTrue(TEXT("the fit succeeded"), Fit.bValid);
	TestEqual(TEXT("the readout agrees with the fit for the same inputs"),
		ComputeHeightUU(In.SilhouetteHeightPx, In.PixelsPerUnit, Fit.Scale.Z),
		175.0f);

	// Metres, the human half of the readout.
	TestEqual(TEXT("175 uu is 1.75 m"), UnrealUnitsToMetres(175.0f), 1.75f);

	TestEqual(TEXT("a degenerate readout is zero, not NaN"), ComputeHeightUU(0.0f, 1.0f, 1.0f), 0.0f);
	TestEqual(TEXT("zero ppu likewise"), ComputeHeightUU(64.0f, 0.0f, 1.0f), 0.0f);
	return true;
}

// =============================================================================
// The drag gesture contract (R7). Canvas owns capture and coordinates; the panel
// owns the transaction. What is pinned is the discipline, not the pixel math:
// a click writes nothing, a drag collapses to ONE undo entry, and every
// invalidating boundary settles.
// =============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCharacterSizingDragGesture,
	"Paper2DPlus.CharacterSizing.DragOpensOneTransactionAndOnlyWhenItMoves",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCharacterSizingDragGesture::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Profile = Sizing_MakeProfile();
	const TSharedRef<SProfileCharacterSizingTool> Tool =
		SNew(SProfileCharacterSizingTool).Profile(Profile);
	const TSharedPtr<SCharacterSizingCanvas> Canvas = Tool->GetCanvasForTests();
	if (!TestTrue(TEXT("the canvas was built"), Canvas.IsValid()))
	{
		return false;
	}

	// Seed the view state the canvas would normally derive from a paint: origin at (150,150),
	// 1 screen pixel per Unreal unit, and a 100x100 sprite rect centred on the origin.
	Canvas->SeedViewStateForTests(
		FVector2D(150.0f, 150.0f), 1.0f, FSlateRect(100.0f, 100.0f, 200.0f, 200.0f));

	const FVector StartLocation = Profile->RelativeLocation;

	// --- A click with no movement must not dirty the asset or open a transaction ---------------
	Canvas->BeginGestureForTests(ECharacterSizingDragMode::Move, FVector2D(150.0f, 150.0f));
	TestFalse(TEXT("arming a gesture opens no transaction"), Tool->HasOpenTransactionForTests());
	Canvas->DragToForTests(FVector2D(150.0f, 150.0f));
	TestFalse(TEXT("a zero-delta move writes nothing"), Tool->HasOpenTransactionForTests());
	TestFalse(TEXT("and the canvas records no write"), Canvas->HasOpenedWriteForTests());
	Canvas->SettleGesture();
	TestTrue(TEXT("the asset is untouched by a click"),
		Profile->RelativeLocation.Equals(StartLocation, 1e-4f));

	// --- Sub-threshold movement likewise opens nothing -------------------------------------------
	Canvas->BeginGestureForTests(ECharacterSizingDragMode::Move, FVector2D(150.0f, 150.0f));
	Canvas->DragToForTests(FVector2D(150.02f, 150.0f)); // 0.02 uu, below the 0.1 uu step
	TestFalse(TEXT("sub-threshold movement opens no transaction"),
		Tool->HasOpenTransactionForTests());
	Canvas->SettleGesture();

	// --- A real drag opens exactly one transaction and reuses it ---------------------------------
	Canvas->BeginGestureForTests(ECharacterSizingDragMode::Move, FVector2D(150.0f, 150.0f));
	Canvas->DragToForTests(FVector2D(170.0f, 150.0f));
	TestTrue(TEXT("the first real delta opens the transaction"), Tool->HasOpenTransactionForTests());
	TestEqual(TEXT("moving right 20 px at 1 px/uu moves +20 uu in X"),
		(float)Profile->RelativeLocation.X, (float)StartLocation.X + 20.0f);

	Canvas->DragToForTests(FVector2D(190.0f, 130.0f));
	TestTrue(TEXT("later deltas reuse the SAME transaction"), Tool->HasOpenTransactionForTests());
	TestEqual(TEXT("the delta is measured from the gesture start, not cumulatively"),
		(float)Profile->RelativeLocation.X, (float)StartLocation.X + 40.0f);
	TestEqual(TEXT("screen-up is world-up"),
		(float)Profile->RelativeLocation.Z, (float)StartLocation.Z + 20.0f);

	Canvas->SettleGesture();
	TestFalse(TEXT("settling closes the transaction"), Tool->HasOpenTransactionForTests());

	// Settlement is idempotent -- it is called from several boundaries and must tolerate that.
	Canvas->SettleGesture();
	Canvas->SettleGesture();
	TestFalse(TEXT("re-settling is harmless"), Tool->HasOpenTransactionForTests());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCharacterSizingDragScaleAndCancel,
	"Paper2DPlus.CharacterSizing.HandleDragScalesUniformlyAndEscRestores",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCharacterSizingDragScaleAndCancel::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Profile = Sizing_MakeProfile();
	Profile->RelativeScale3D = FVector(2.0f, 1.0f, 2.0f);
	const TSharedRef<SProfileCharacterSizingTool> Tool =
		SNew(SProfileCharacterSizingTool).Profile(Profile);
	const TSharedPtr<SCharacterSizingCanvas> Canvas = Tool->GetCanvasForTests();
	if (!TestTrue(TEXT("the canvas was built"), Canvas.IsValid()))
	{
		return false;
	}
	Canvas->SeedViewStateForTests(
		FVector2D(150.0f, 150.0f), 1.0f, FSlateRect(100.0f, 100.0f, 200.0f, 200.0f));

	const FVector StartScale = Profile->RelativeScale3D;
	const FVector StartLocation = Profile->RelativeLocation;

	// Grab the bottom-right corner: distance from centre (150,150) to (200,200) is ~70.7.
	Canvas->BeginGestureForTests(ECharacterSizingDragMode::Scale, FVector2D(200.0f, 200.0f));
	// Drag to twice that distance -> the uniform scale doubles.
	Canvas->DragToForTests(FVector2D(250.0f, 250.0f));
	TestTrue(TEXT("a handle drag opens the transaction"), Tool->HasOpenTransactionForTests());
	TestEqual(TEXT("dragging a corner to twice the radius doubles the scale"),
		(float)Profile->RelativeScale3D.Z, (float)StartScale.Z * 2.0f);
	TestEqual(TEXT("scale stays UNIFORM across X and Z"),
		(float)Profile->RelativeScale3D.X, (float)Profile->RelativeScale3D.Z);
	TestEqual(TEXT("depth is never scaled"), (float)Profile->RelativeScale3D.Y, 1.0f);
	TestTrue(TEXT("a scale drag does not move the character"),
		Profile->RelativeLocation.Equals(StartLocation, 1e-4f));

	// Esc restores the pre-drag transform and leaves no undo entry behind.
	Canvas->CancelGestureForTests();
	TestFalse(TEXT("cancelling closes the transaction"), Tool->HasOpenTransactionForTests());
	TestTrue(TEXT("Esc restores the pre-drag scale"),
		Profile->RelativeScale3D.Equals(StartScale, 1e-4f));
	TestTrue(TEXT("and the pre-drag location"),
		Profile->RelativeLocation.Equals(StartLocation, 1e-4f));

	// A scale drag can never collapse the character to nothing.
	Canvas->BeginGestureForTests(ECharacterSizingDragMode::Scale, FVector2D(200.0f, 200.0f));
	Canvas->DragToForTests(FVector2D(150.0f, 150.0f)); // straight onto the centre
	TestTrue(TEXT("dragging a handle onto the centre never produces a zero scale"),
		FMath::Abs(Profile->RelativeScale3D.Z) > 0.0f);
	Canvas->SettleGesture();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCharacterSizingGestureSettlesOnBoundaries,
	"Paper2DPlus.CharacterSizing.GestureSettlesOnEveryInvalidatingBoundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCharacterSizingGestureSettlesOnBoundaries::RunTest(const FString& Parameters)
{
	// Lost capture -- a modal dialog or focus steal takes the mouse without a button-up.
	{
		UPaper2DPlusCharacterProfileAsset* Profile = Sizing_MakeProfile();
		const TSharedRef<SProfileCharacterSizingTool> Tool =
			SNew(SProfileCharacterSizingTool).Profile(Profile);
		const TSharedPtr<SCharacterSizingCanvas> Canvas = Tool->GetCanvasForTests();
		Canvas->SeedViewStateForTests(
			FVector2D(150.0f, 150.0f), 1.0f, FSlateRect(100.0f, 100.0f, 200.0f, 200.0f));
		Canvas->BeginGestureForTests(ECharacterSizingDragMode::Move, FVector2D(150.0f, 150.0f));
		Canvas->DragToForTests(FVector2D(170.0f, 150.0f));
		TestTrue(TEXT("a transaction is open mid-drag"), Tool->HasOpenTransactionForTests());

		FCaptureLostEvent Lost;
		Canvas->OnMouseCaptureLost(Lost);
		TestFalse(TEXT("losing capture settles the gesture"), Tool->HasOpenTransactionForTests());
		TestEqual(TEXT("and the drag is not in progress"),
			(int32)Canvas->GetDragMode(), (int32)ECharacterSizingDragMode::None);
	}

	// Widget destruction -- what closing the window reduces to. A transaction must never outlive
	// the panel that owns it.
	{
		UPaper2DPlusCharacterProfileAsset* Profile = Sizing_MakeProfile();
		TSharedPtr<SProfileCharacterSizingTool> Tool =
			SNew(SProfileCharacterSizingTool).Profile(Profile);
		const TSharedPtr<SCharacterSizingCanvas> Canvas = Tool->GetCanvasForTests();
		Canvas->SeedViewStateForTests(
			FVector2D(150.0f, 150.0f), 1.0f, FSlateRect(100.0f, 100.0f, 200.0f, 200.0f));
		Canvas->BeginGestureForTests(ECharacterSizingDragMode::Move, FVector2D(150.0f, 150.0f));
		Canvas->DragToForTests(FVector2D(180.0f, 150.0f));
		TestTrue(TEXT("a transaction is open mid-drag"), Tool->HasOpenTransactionForTests());

		Tool.Reset(); // destroys panel and canvas
		TestTrue(TEXT("tearing the tool down mid-drag leaves no open transaction behind"), true);
	}
	return true;
}

#endif // WITH_EDITOR
