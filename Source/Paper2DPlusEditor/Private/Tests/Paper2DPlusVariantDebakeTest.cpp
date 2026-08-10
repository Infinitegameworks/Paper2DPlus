// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#if WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "VariantDebake.h"

/**
 * Worldless tests for the de-bake shared base pure core (FVariantDebake, TASK-107). Tiny synthetic
 * multi-cell sheets exercise the consensus vote, the VFX contamination masks, the algebraic un-blend,
 * the reference-sheet tie-break (with a permuted-input determinism ratchet), overlay emission (incl.
 * the semi-transparent-over-nothing double-composite regression), the offset auto-search with a
 * back/front sandwich, and unrecoverable-pixel reporting.
 *
 * Helpers carry a FILE-UNIQUE PREFIX (VariantDebake_*) because unity builds concatenate test .cpp files
 * into one TU — generic anon-namespace helper names collide (see CLAUDE.md / PR #158).
 */
static TArray<FColor> VariantDebake_Solid(int32 W, int32 H, FColor C)
{
	TArray<FColor> P;
	P.Init(C, W * H);
	return P;
}

static FDebakeSheetInput VariantDebake_Sheet(TArray<FColor> Pixels, int32 W, int32 H)
{
	FDebakeSheetInput In;
	In.Pixels = MoveTemp(Pixels);
	In.Width = W;
	In.Height = H;
	return In;
}

static const FColor VariantDebake_BaseC(10, 20, 30, 255);
static const FColor VariantDebake_Clear(0, 0, 0, 0);

// ─── 1. Pure consensus vote recovers the base with 3 variants differing at distinct pixels ───

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusVariantDebakePureVote,
	"Paper2DPlus.VariantDebake.PureVote",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusVariantDebakePureVote::RunTest(const FString& Parameters)
{
	const int32 W = 8, H = 4; // 2 cells of 4x4
	const FColor Uniques[3] = { FColor(200, 0, 0, 255), FColor(0, 200, 0, 255), FColor(0, 0, 200, 255) };

	TArray<FDebakeSheetInput> Sheets;
	for (int32 i = 0; i < 3; ++i)
	{
		TArray<FColor> P = VariantDebake_Solid(W, H, VariantDebake_BaseC);
		P[i] = Uniques[i]; // each variant deviates at its own distinct pixel
		Sheets.Add(VariantDebake_Sheet(MoveTemp(P), W, H));
	}

	FDebakeSettings Settings;
	Settings.Columns = 2;
	Settings.Rows = 1;

	FDebakeResult Result;
	FText Error;
	if (!TestTrue(TEXT("Run succeeds"), FVariantDebake::Run(Sheets, Settings, Result, Error))) { return false; }

	for (int32 P = 0; P < W * H; ++P)
	{
		if (Result.BasePixels[P] != VariantDebake_BaseC)
		{
			AddError(FString::Printf(TEXT("Base pixel %d not recovered (got %s)"), P, *Result.BasePixels[P].ToString()));
			return false;
		}
	}

	// Each variant's overlay carries exactly its one deviating pixel, verbatim.
	for (int32 i = 0; i < 3; ++i)
	{
		TestEqual(TEXT("Overlay pixel count"), Result.OverlayPixelCounts[i], 1);
		TestEqual(TEXT("Overlay carries the variant's own value"), Result.Overlays[i][i], Uniques[i]);
		TestEqual(TEXT("No residual mismatch"), Result.ResidualCounts[i], 0);
		TestEqual(TEXT("No erase-unfixable pixels"), Result.EraseUnfixableCounts[i], 0);
		TestEqual(TEXT("No VFX = no offset"), Result.VfxOffsets[i], (int32)INDEX_NONE);
	}
	TestEqual(TEXT("Nothing unrecoverable"), Result.UnrecoverablePixels.Num(), 0);
	return true;
}

// ─── 2. Palette collision: masked vote recovers the base where the pure vote cannot ───

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusVariantDebakeMaskedVote,
	"Paper2DPlus.VariantDebake.MaskedVote",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusVariantDebakeMaskedVote::RunTest(const FString& Parameters)
{
	const int32 W = 8, H = 4; // 2 cells of 4x4
	const int32 CollisionP = 5;
	const FColor VfxC(0, 255, 255, 255); // two variants share this exact baked "VFX" cyan

	// Sheets 0+1 baked the identical VFX pixel; sheets 2+3 show the true base there.
	TArray<FDebakeSheetInput> Sheets;
	for (int32 i = 0; i < 4; ++i)
	{
		TArray<FColor> P = VariantDebake_Solid(W, H, VariantDebake_BaseC);
		if (i < 2) { P[CollisionP] = VfxC; }
		Sheets.Add(VariantDebake_Sheet(MoveTemp(P), W, H));
	}

	FDebakeSettings Settings;
	Settings.Columns = 2;
	Settings.Rows = 1;

	// Without masks the 2-2 tie resolves from the reference sheet (0) — the WRONG value here.
	{
		FDebakeResult Result;
		FText Error;
		if (!TestTrue(TEXT("Unmasked run succeeds"), FVariantDebake::Run(Sheets, Settings, Result, Error))) { return false; }
		TestEqual(TEXT("Pure vote is fooled by the palette collision"), Result.BasePixels[CollisionP], VfxC);
	}

	// With full-size front masks (opaque exactly at the collision pixel) only clean sheets vote.
	for (int32 i = 0; i < 2; ++i)
	{
		TArray<FColor> Vfx = VariantDebake_Solid(W, H, VariantDebake_Clear);
		Vfx[CollisionP] = VfxC;
		Sheets[i].VfxFront = MoveTemp(Vfx);
		Sheets[i].VfxFrontWidth = W;
	}
	{
		FDebakeResult Result;
		FText Error;
		if (!TestTrue(TEXT("Masked run succeeds"), FVariantDebake::Run(Sheets, Settings, Result, Error))) { return false; }
		TestEqual(TEXT("Masked vote recovers the base"), Result.BasePixels[CollisionP], VariantDebake_BaseC);
		TestEqual(TEXT("Full-size VFX resolves offset 0"), Result.VfxOffsets[0], 0);
		TestEqual(TEXT("Sheets without VFX stay INDEX_NONE"), Result.VfxOffsets[2], (int32)INDEX_NONE);
	}
	return true;
}

// ─── 3. Un-blend round-trip: composite a known base + VFX at a=0.4, recover the base (±1) ───

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusVariantDebakeUnBlendRoundTrip,
	"Paper2DPlus.VariantDebake.UnBlendRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusVariantDebakeUnBlendRoundTrip::RunTest(const FString& Parameters)
{
	const FColor Under(200, 100, 50, 255);
	const FColor Vfx(0, 255, 255, 102); // a = 0.4

	const FColor Main = FVariantDebake::AlphaOver(Vfx, Under);
	FColor Recovered;
	if (!TestTrue(TEXT("UnBlend succeeds on a semi-transparent composite"), FVariantDebake::UnBlend(Main, Vfx, Recovered))) { return false; }
	TestTrue(TEXT("R within 1"), FMath::Abs((int32)Recovered.R - (int32)Under.R) <= 1);
	TestTrue(TEXT("G within 1"), FMath::Abs((int32)Recovered.G - (int32)Under.G) <= 1);
	TestTrue(TEXT("B within 1"), FMath::Abs((int32)Recovered.B - (int32)Under.B) <= 1);
	TestTrue(TEXT("A within 1"), FMath::Abs((int32)Recovered.A - (int32)Under.A) <= 1);

	// Semi-transparent VFX over NOTHING: un-blend reports "nothing underneath".
	FColor None;
	TestFalse(TEXT("VFX over nothing has no underlying value"), FVariantDebake::UnBlend(Vfx, Vfx, None));
	TestEqual(TEXT("Nothing-underneath output is transparent"), None, VariantDebake_Clear);

	// Precondition: opaque or absent VFX cannot be un-blended.
	TestFalse(TEXT("Opaque VFX rejected"), FVariantDebake::UnBlend(Main, FColor(1, 2, 3, 255), None));
	TestFalse(TEXT("Absent VFX rejected"), FVariantDebake::UnBlend(Main, FColor(1, 2, 3, 0), None));
	return true;
}

// ─── 4. Tie-break determinism: reference sheet owns all-unique pixels; permuted input, same base ───

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusVariantDebakeTieBreakDeterminism,
	"Paper2DPlus.VariantDebake.TieBreakDeterminism",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusVariantDebakeTieBreakDeterminism::RunTest(const FString& Parameters)
{
	const int32 W = 8, H = 4;
	const int32 TieP = 9; // every variant unique here (the "bottle liquid" pixel)
	const FColor Uniques[3] = { FColor(200, 0, 0, 255), FColor(0, 200, 0, 255), FColor(0, 0, 200, 255) };

	auto MakeSheets = [&](const TArray<int32>& Order)
	{
		TArray<FDebakeSheetInput> Sheets;
		for (const int32 Src : Order)
		{
			TArray<FColor> P = VariantDebake_Solid(W, H, VariantDebake_BaseC);
			P[TieP] = Uniques[Src];
			Sheets.Add(VariantDebake_Sheet(MoveTemp(P), W, H));
		}
		return Sheets;
	};

	FDebakeSettings Settings;
	Settings.Columns = 2;
	Settings.Rows = 1;

	// Identity order, reference = sheet 0.
	FDebakeResult A;
	FText Error;
	Settings.ReferenceSheetIndex = 0;
	if (!TestTrue(TEXT("Run A succeeds"), FVariantDebake::Run(MakeSheets({ 0, 1, 2 }), Settings, A, Error))) { return false; }
	TestEqual(TEXT("Tie resolves to the reference sheet's value"), A.BasePixels[TieP], Uniques[0]);

	// Permuted order [2,0,1] with ReferenceSheetIndex tracking the SAME original sheet 0 (now index 1).
	FDebakeResult B;
	Settings.ReferenceSheetIndex = 1;
	if (!TestTrue(TEXT("Run B succeeds"), FVariantDebake::Run(MakeSheets({ 2, 0, 1 }), Settings, B, Error))) { return false; }
	TestEqual(TEXT("Determinism ratchet: permuted input yields the identical base"), B.BasePixels, A.BasePixels);
	return true;
}

// ─── 5. Overlay correctness: exact reconstruction at emitted pixels; semi-over-nothing emits NOTHING ───

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusVariantDebakeOverlayCorrectness,
	"Paper2DPlus.VariantDebake.OverlayCorrectness",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusVariantDebakeOverlayCorrectness::RunTest(const FString& Parameters)
{
	const int32 W = 8, H = 4;    // 2 cells of 4x4: cell 0 = solid base block, cell 1 = empty
	const int32 CellPx = 4 * 4;
	const FColor SemiVfx(255, 0, 0, 128);

	auto MakeMain = [&]() // base block in cell 0, nothing in cell 1
	{
		TArray<FColor> P = VariantDebake_Solid(W, H, VariantDebake_Clear);
		for (int32 Y = 0; Y < H; ++Y)
		{
			for (int32 X = 0; X < 4; ++X) { P[Y * W + X] = VariantDebake_BaseC; }
		}
		return P;
	};

	TArray<FDebakeSheetInput> Sheets;
	for (int32 i = 0; i < 3; ++i)
	{
		Sheets.Add(VariantDebake_Sheet(MakeMain(), W, H));
	}

	// Sheet 1 carries variant art in cell 0 (an opaque unique pixel — the "bottle tint").
	const int32 ArtP = 1 * W + 2;
	const FColor ArtC(250, 240, 2, 255);
	Sheets[1].Pixels[ArtP] = ArtC;

	// Sheet 0 carries a 1-cell semi-transparent front VFX that belongs over the EMPTY cell 1:
	// its main shows the VFX composited over nothing there.
	TArray<FColor> Vfx = VariantDebake_Solid(4, 4, VariantDebake_Clear);
	Vfx[2 * 4 + 2] = SemiVfx;
	Sheets[0].VfxFront = Vfx;
	Sheets[0].VfxFrontWidth = 4;
	const int32 VfxMainP = 2 * W + (4 + 2); // cell 1, in-cell (2,2)
	Sheets[0].Pixels[VfxMainP] = FVariantDebake::AlphaOver(SemiVfx, VariantDebake_Clear);

	FDebakeSettings Settings;
	Settings.Columns = 2;
	Settings.Rows = 1;

	FDebakeResult Result;
	FText Error;
	if (!TestTrue(TEXT("Run succeeds"), FVariantDebake::Run(Sheets, Settings, Result, Error))) { return false; }

	TestEqual(TEXT("VFX auto-aligned to the empty cell"), Result.VfxOffsets[0], 1);

	// The double-composite regression: semi-transparent VFX over NOTHING must emit NOTHING —
	// the base is transparent there and front OVER transparent already reconstructs the main.
	TestEqual(TEXT("Base stays transparent under the VFX"), Result.BasePixels[VfxMainP], VariantDebake_Clear);
	TestEqual(TEXT("Semi-over-nothing emits no overlay pixel"), Result.Overlays[0][VfxMainP], VariantDebake_Clear);
	TestEqual(TEXT("Sheet 0 overlay is empty"), Result.OverlayPixelCounts[0], 0);

	// Variant art: emitted verbatim, and front OVER (overlay OVER (base OVER back)) reconstructs
	// the variant exactly at every emitted pixel.
	TestEqual(TEXT("Variant art emitted"), Result.Overlays[1][ArtP], ArtC);
	for (int32 P = 0; P < W * H; ++P)
	{
		if (Result.Overlays[1][P].A > 0)
		{
			const FColor Recon = FVariantDebake::AlphaOver(Result.Overlays[1][P], Result.BasePixels[P]);
			TestEqual(TEXT("Reconstruction is exact at emitted pixels"), Recon, Sheets[1].Pixels[P]);
		}
	}
	for (int32 i = 0; i < 3; ++i)
	{
		TestEqual(TEXT("No residual mismatch"), Result.ResidualCounts[i], 0);
	}
	return true;
}

// ─── 6. Offset search finds a known shift; back/front sandwich composites correctly ───

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusVariantDebakeOffsetSearchSandwich,
	"Paper2DPlus.VariantDebake.OffsetSearchSandwich",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusVariantDebakeOffsetSearchSandwich::RunTest(const FString& Parameters)
{
	const int32 W = 16, H = 4; // 4 cells of 4x4
	const FColor FrontC(0, 255, 0, 255);
	const FColor BackC(255, 0, 255, 255);

	// Base: one opaque pixel at in-cell (0,0) of every cell, transparent elsewhere — so the back
	// VFX has base transparency to show through.
	auto MakeMain = [&]()
	{
		TArray<FColor> P = VariantDebake_Solid(W, H, VariantDebake_Clear);
		for (int32 Cell = 0; Cell < 4; ++Cell) { P[Cell * 4] = VariantDebake_BaseC; }
		return P;
	};

	TArray<FDebakeSheetInput> Sheets;
	for (int32 i = 0; i < 3; ++i)
	{
		Sheets.Add(VariantDebake_Sheet(MakeMain(), W, H));
	}

	// Sheet 0: a 2-cell back+front sandwich, truly placed at cell offset 2. Cell 0 of the VFX pair
	// holds front at in-cell (1,1) and back at in-cell (2,2); cell 1 is empty.
	TArray<FColor> Front = VariantDebake_Solid(8, 4, VariantDebake_Clear);
	Front[1 * 8 + 1] = FrontC;
	TArray<FColor> Back = VariantDebake_Solid(8, 4, VariantDebake_Clear);
	Back[2 * 8 + 2] = BackC;
	Sheets[0].VfxFront = Front;
	Sheets[0].VfxFrontWidth = 8;
	Sheets[0].VfxBack = Back;
	Sheets[0].VfxBackWidth = 8;

	// Bake sheet 0's main accordingly: front pixel lands at cell 2 in-cell (1,1); back pixel shows
	// through base transparency at cell 2 in-cell (2,2).
	Sheets[0].Pixels[1 * W + (8 + 1)] = FrontC;
	Sheets[0].Pixels[2 * W + (8 + 2)] = BackC;

	FDebakeSettings Settings;
	Settings.Columns = 4;
	Settings.Rows = 1;

	FDebakeResult Result;
	FText Error;
	if (!TestTrue(TEXT("Run succeeds"), FVariantDebake::Run(Sheets, Settings, Result, Error))) { return false; }

	TestEqual(TEXT("Offset search found the true shift"), Result.VfxOffsets[0], 2);

	// The recovered base is the clean base: back-through-transparency was discarded from the vote,
	// front-covered pixels were outvoted by the clean sheets.
	for (int32 P = 0; P < W * H; ++P)
	{
		// Expected: in-cell (0,0) of each cell = base, else transparent.
		const int32 X = P % W, Y = P / W;
		const FColor Want = (X % 4 == 0 && Y == 0) ? VariantDebake_BaseC : VariantDebake_Clear;
		if (Result.BasePixels[P] != Want)
		{
			AddError(FString::Printf(TEXT("Base pixel (%d,%d) wrong: got %s"), X, Y, *Result.BasePixels[P].ToString()));
			return false;
		}
	}

	// Sandwich reconstructs exactly: no overlay needed, no residual.
	TestEqual(TEXT("No overlay pixels for the sandwich sheet"), Result.OverlayPixelCounts[0], 0);
	TestEqual(TEXT("No residual for the sandwich sheet"), Result.ResidualCounts[0], 0);

	// Explicit override wins over the search.
	Settings.VfxOffsetOverride = 1;
	FDebakeResult Overridden;
	if (!TestTrue(TEXT("Override run succeeds"), FVariantDebake::Run(Sheets, Settings, Overridden, Error))) { return false; }
	TestEqual(TEXT("Override respected"), Overridden.VfxOffsets[0], 1);
	return true;
}

// ─── 7. Unrecoverable: a pixel opaquely VFX-covered in ALL sheets stays transparent + is reported ───

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusVariantDebakeUnrecoverable,
	"Paper2DPlus.VariantDebake.Unrecoverable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusVariantDebakeUnrecoverable::RunTest(const FString& Parameters)
{
	const int32 W = 8, H = 4;
	const int32 CoveredX = 1, CoveredY = 1;
	const int32 CoveredP = CoveredY * W + CoveredX;

	TArray<FDebakeSheetInput> Sheets;
	const FColor VfxColors[3] = { FColor(255, 0, 0, 255), FColor(0, 255, 0, 255), FColor(0, 0, 255, 255) };
	for (int32 i = 0; i < 3; ++i)
	{
		TArray<FColor> P = VariantDebake_Solid(W, H, VariantDebake_BaseC);
		P[CoveredP] = VfxColors[i]; // opaque VFX baked over the same pixel in every variant

		TArray<FColor> Vfx = VariantDebake_Solid(W, H, VariantDebake_Clear);
		Vfx[CoveredP] = VfxColors[i];

		FDebakeSheetInput In = VariantDebake_Sheet(MoveTemp(P), W, H);
		In.VfxFront = MoveTemp(Vfx);
		In.VfxFrontWidth = W;
		Sheets.Add(MoveTemp(In));
	}

	FDebakeSettings Settings;
	Settings.Columns = 2;
	Settings.Rows = 1;

	FDebakeResult Result;
	FText Error;
	if (!TestTrue(TEXT("Run succeeds"), FVariantDebake::Run(Sheets, Settings, Result, Error))) { return false; }

	TestEqual(TEXT("Unrecoverable pixel left transparent (never invented)"), Result.BasePixels[CoveredP], VariantDebake_Clear);
	if (!TestEqual(TEXT("Exactly one unrecoverable pixel reported"), Result.UnrecoverablePixels.Num(), 1)) { return false; }
	TestEqual(TEXT("Reported at the right location"), Result.UnrecoverablePixels[0], FIntPoint(CoveredX, CoveredY));

	// Opaque front hides the pixel either way: no overlay there, no residual.
	for (int32 i = 0; i < 3; ++i)
	{
		TestEqual(TEXT("No overlay under opaque VFX"), Result.Overlays[i][CoveredP], VariantDebake_Clear);
		TestEqual(TEXT("No residual"), Result.ResidualCounts[i], 0);
	}
	return true;
}

// ─── 8. Hard-error guards: never silently mis-cut ───

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusVariantDebakeHardErrors,
	"Paper2DPlus.VariantDebake.HardErrors",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusVariantDebakeHardErrors::RunTest(const FString& Parameters)
{
	FDebakeSettings Settings;
	Settings.Columns = 2;
	Settings.Rows = 1;
	FDebakeResult Result;
	FText Error;

	// Fewer than 2 sheets.
	{
		TArray<FDebakeSheetInput> One;
		One.Add(VariantDebake_Sheet(VariantDebake_Solid(8, 4, VariantDebake_BaseC), 8, 4));
		TestFalse(TEXT("Single sheet rejected"), FVariantDebake::Run(One, Settings, Result, Error));
		TestFalse(TEXT("Error text set"), Error.IsEmpty());
	}

	// Mismatched dimensions.
	{
		TArray<FDebakeSheetInput> Bad;
		Bad.Add(VariantDebake_Sheet(VariantDebake_Solid(8, 4, VariantDebake_BaseC), 8, 4));
		Bad.Add(VariantDebake_Sheet(VariantDebake_Solid(4, 4, VariantDebake_BaseC), 4, 4));
		TestFalse(TEXT("Mismatched sheet dimensions rejected"), FVariantDebake::Run(Bad, Settings, Result, Error));
	}

	// Grid that does not divide the sheet.
	{
		TArray<FDebakeSheetInput> Sheets;
		Sheets.Add(VariantDebake_Sheet(VariantDebake_Solid(8, 4, VariantDebake_BaseC), 8, 4));
		Sheets.Add(VariantDebake_Sheet(VariantDebake_Solid(8, 4, VariantDebake_BaseC), 8, 4));
		FDebakeSettings BadGrid = Settings;
		BadGrid.Columns = 3;
		TestFalse(TEXT("Non-divisible grid rejected"), FVariantDebake::Run(Sheets, BadGrid, Result, Error));
	}

	// VFX with more cells than the main sheet.
	{
		TArray<FDebakeSheetInput> Sheets;
		Sheets.Add(VariantDebake_Sheet(VariantDebake_Solid(8, 4, VariantDebake_BaseC), 8, 4));
		Sheets.Add(VariantDebake_Sheet(VariantDebake_Solid(8, 4, VariantDebake_BaseC), 8, 4));
		Sheets[0].VfxFront = VariantDebake_Solid(12, 4, VariantDebake_Clear); // 3 cells > 2 main cells
		Sheets[0].VfxFrontWidth = 12;
		TestFalse(TEXT("Oversized VFX rejected"), FVariantDebake::Run(Sheets, Settings, Result, Error));
	}

	// Reference index out of range.
	{
		TArray<FDebakeSheetInput> Sheets;
		Sheets.Add(VariantDebake_Sheet(VariantDebake_Solid(8, 4, VariantDebake_BaseC), 8, 4));
		Sheets.Add(VariantDebake_Sheet(VariantDebake_Solid(8, 4, VariantDebake_BaseC), 8, 4));
		FDebakeSettings BadRef = Settings;
		BadRef.ReferenceSheetIndex = 5;
		TestFalse(TEXT("Out-of-range reference rejected"), FVariantDebake::Run(Sheets, BadRef, Result, Error));
	}
	return true;
}

#endif // WITH_EDITOR
