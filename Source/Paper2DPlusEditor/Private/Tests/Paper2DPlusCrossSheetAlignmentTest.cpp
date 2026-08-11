// Copyright 2026 Infinite Gameworks. All Rights Reserved.
//
// Characterization tests for FSpriteExtractionUtils::ComputeUniformBounds.
//
// The cell-midpoint + max-extent algorithm at SpriteExtractionUtils.cpp:466 was
// stabilized after 2+ hours of debugging (see docs/solutions/ue-uniform-bounds-
// detection-architecture.md). These tests pin its output on three representative
// fixtures so any future refactor that silently changes behavior fails loudly.
//
// Unit 1a from docs/plans/2026-04-19-001-feat-sprite-extractor-cross-sheet-
// alignment-plan.md. The plan originally slated this test for the Paper2DPlus
// runtime module; it lives here in Paper2DPlusEditor instead because
// FSpriteExtractionUtils is an Editor-module symbol and the runtime module
// cannot depend on the editor module.
//
// The tests operate on synthetic TArray<FDetectedSprite> inputs rather than
// texture pixel data. ComputeUniformBounds consumes pre-detected sprites, so
// bypassing DetectSpriteBounds isolates the algorithm under test.

#if WITH_EDITOR

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "SpriteExtractionUtils.h"
#include "SpriteEditorPanel.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Engine/Texture2D.h"
#include "PaperFlipbook.h"
#include "PaperSprite.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"
#include "HAL/PlatformFileManager.h"

/** Cross-sheet alignment test suite — Multi-texture sprite alignment and batch extraction verification. */

namespace Paper2DPlusCrossSheetAlignment
{
	/** Build a detected sprite with OriginalBounds at the given rect. Bounds starts identical. */
	static FDetectedSprite MakeSprite(int32 MinX, int32 MinY, int32 MaxX, int32 MaxY, int32 Index)
	{
		FDetectedSprite Sprite;
		Sprite.OriginalBounds = FIntRect(MinX, MinY, MaxX, MaxY);
		Sprite.Bounds = Sprite.OriginalBounds;
		Sprite.bSelected = true;
		Sprite.Index = Index;
		return Sprite;
	}

	/** Assert a sprite's Bounds equals the expected rect. Emits the index on failure for diagnostics. */
	static bool AssertBounds(
		FAutomationTestBase& Test,
		const FDetectedSprite& Sprite,
		int32 MinX, int32 MinY, int32 MaxX, int32 MaxY,
		const TCHAR* Label)
	{
		const FIntRect Expected(MinX, MinY, MaxX, MaxY);
		const FIntRect Actual = Sprite.Bounds;
		const bool bMatch = (Actual == Expected);
		if (!bMatch)
		{
			Test.AddError(FString::Printf(TEXT("%s: expected (%d,%d)-(%d,%d), got (%d,%d)-(%d,%d)"),
				Label,
				Expected.Min.X, Expected.Min.Y, Expected.Max.X, Expected.Max.Y,
				Actual.Min.X, Actual.Min.Y, Actual.Max.X, Actual.Max.Y));
		}
		return bMatch;
	}
}


// =============================================================================
// TEST 1: Clean 4x2 regular grid, content centered in cells.
// =============================================================================
// Every sprite is 32x32 centered within a 64x64 grid cell. The algorithm should
// produce Bounds == OriginalBounds because every sprite's drift from its cell
// midpoint is identical — max extents match the existing tight fit.
//
// Input texture dims: 256x128. Grid: 4 cols x 2 rows. Cell size: 64x64.
// Content: 32x32 centered in each cell.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusComputeUniformBoundsCleanGrid,
	"Paper2DPlus.CrossSheetAlignment.ComputeUniformBounds.CleanGrid",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusComputeUniformBoundsCleanGrid::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusCrossSheetAlignment;

	TArray<FDetectedSprite> Sprites;
	// Row 0: Y 16-48
	Sprites.Add(MakeSprite( 16, 16,  48, 48, 0));  // R0C0
	Sprites.Add(MakeSprite( 80, 16, 112, 48, 1));  // R0C1
	Sprites.Add(MakeSprite(144, 16, 176, 48, 2));  // R0C2
	Sprites.Add(MakeSprite(208, 16, 240, 48, 3));  // R0C3
	// Row 1: Y 80-112
	Sprites.Add(MakeSprite( 16, 80,  48, 112, 4)); // R1C0
	Sprites.Add(MakeSprite( 80, 80, 112, 112, 5)); // R1C1
	Sprites.Add(MakeSprite(144, 80, 176, 112, 6)); // R1C2
	Sprites.Add(MakeSprite(208, 80, 240, 112, 7)); // R1C3

	FSpriteExtractionUtils::ComputeUniformBounds(Sprites, 256, 128);

	TestEqual(TEXT("Sprite count preserved"), Sprites.Num(), 8);

	// Every sprite's Bounds should match its OriginalBounds (perfect symmetry — no expansion).
	AssertBounds(*this, Sprites[0],  16, 16,  48, 48, TEXT("R0C0 Bounds"));
	AssertBounds(*this, Sprites[1],  80, 16, 112, 48, TEXT("R0C1 Bounds"));
	AssertBounds(*this, Sprites[2], 144, 16, 176, 48, TEXT("R0C2 Bounds"));
	AssertBounds(*this, Sprites[3], 208, 16, 240, 48, TEXT("R0C3 Bounds"));
	AssertBounds(*this, Sprites[4],  16, 80,  48, 112, TEXT("R1C0 Bounds"));
	AssertBounds(*this, Sprites[5],  80, 80, 112, 112, TEXT("R1C1 Bounds"));
	AssertBounds(*this, Sprites[6], 144, 80, 176, 112, TEXT("R1C2 Bounds"));
	AssertBounds(*this, Sprites[7], 208, 80, 240, 112, TEXT("R1C3 Bounds"));

	return true;
}


// =============================================================================
// TEST 2: Mixed-height row — one sprite smaller than siblings.
// =============================================================================
// Same 4x2 grid, but R0C0's content is only 16x16 centered in its cell. All
// seven other sprites remain 32x32 centered. The max-extent pass across ALL
// sprites gives us 16px extent in each direction (from the larger siblings).
// R0C0's Bounds expands from (24,24)-(40,40) to (16,16)-(48,48), matching the
// group. Other sprites' Bounds == OriginalBounds.
//
// This is the core correctness case: small sprites get their Bounds widened to
// the group uniform size so rendering stays aligned across frames.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusComputeUniformBoundsMixedHeight,
	"Paper2DPlus.CrossSheetAlignment.ComputeUniformBounds.MixedHeightRow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusComputeUniformBoundsMixedHeight::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusCrossSheetAlignment;

	TArray<FDetectedSprite> Sprites;
	// Row 0: R0C0 is 16x16 (small), others 32x32
	Sprites.Add(MakeSprite( 24, 24,  40,  40, 0));  // R0C0 — 16x16 small sprite
	Sprites.Add(MakeSprite( 80, 16, 112,  48, 1));
	Sprites.Add(MakeSprite(144, 16, 176,  48, 2));
	Sprites.Add(MakeSprite(208, 16, 240,  48, 3));
	// Row 1: all 32x32
	Sprites.Add(MakeSprite( 16, 80,  48, 112, 4));
	Sprites.Add(MakeSprite( 80, 80, 112, 112, 5));
	Sprites.Add(MakeSprite(144, 80, 176, 112, 6));
	Sprites.Add(MakeSprite(208, 80, 240, 112, 7));

	FSpriteExtractionUtils::ComputeUniformBounds(Sprites, 256, 128);

	TestEqual(TEXT("Sprite count preserved"), Sprites.Num(), 8);

	// R0C0 expands to the group uniform size (32x32 centered on its cell midpoint 32,32).
	AssertBounds(*this, Sprites[0],  16, 16,  48, 48, TEXT("R0C0 expanded to group uniform"));

	// Other seven sprites already at uniform size — Bounds should equal OriginalBounds.
	AssertBounds(*this, Sprites[1],  80, 16, 112,  48, TEXT("R0C1 Bounds"));
	AssertBounds(*this, Sprites[2], 144, 16, 176,  48, TEXT("R0C2 Bounds"));
	AssertBounds(*this, Sprites[3], 208, 16, 240,  48, TEXT("R0C3 Bounds"));
	AssertBounds(*this, Sprites[4],  16, 80,  48, 112, TEXT("R1C0 Bounds"));
	AssertBounds(*this, Sprites[5],  80, 80, 112, 112, TEXT("R1C1 Bounds"));
	AssertBounds(*this, Sprites[6], 144, 80, 176, 112, TEXT("R1C2 Bounds"));
	AssertBounds(*this, Sprites[7], 208, 80, 240, 112, TEXT("R1C3 Bounds"));

	// R0C0's OriginalBounds must remain untouched — Bounds is the only mutated field.
	const FIntRect OriginalR0C0(24, 24, 40, 40);
	TestEqual(TEXT("R0C0 OriginalBounds preserved"), Sprites[0].OriginalBounds, OriginalR0C0);

	return true;
}


// =============================================================================
// TEST 3: Sparse row — fewer sprites in Row 1 than Row 0.
// =============================================================================
// Per-row stride is the subtle behavior this test pins. Row 0 has 4 sprites
// (stride = 256/4 = 64), Row 1 has 3 sprites (stride = 256/3 = 85 via integer
// truncation). Because Row 1's cell midpoints are further apart than Row 0's,
// the sprites in Row 1 drift further from their midpoints → MaxExtLeft grows
// from the Row 1 contributions → uniform width expands.
//
// Expected: uniform size becomes 84x32 (MaxExtLeft=68 from R1C2, MaxExtRight=16
// from Row 0 and R1C0, vertical extents stay 16 each). Bounds that would fall
// below 0 on X are clamped to 0.
//
// This test pins the per-row stride behavior called out in the stability doc
// as "X stride is per-row, not global". A regression that used a global stride
// (e.g., MaxPerRow) would produce different output here.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusComputeUniformBoundsSparseRow,
	"Paper2DPlus.CrossSheetAlignment.ComputeUniformBounds.SparseRow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusComputeUniformBoundsSparseRow::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusCrossSheetAlignment;

	TArray<FDetectedSprite> Sprites;
	// Row 0: full row, 4 sprites at 32x32
	Sprites.Add(MakeSprite( 16, 16,  48,  48, 0));  // R0C0
	Sprites.Add(MakeSprite( 80, 16, 112,  48, 1));  // R0C1
	Sprites.Add(MakeSprite(144, 16, 176,  48, 2));  // R0C2
	Sprites.Add(MakeSprite(208, 16, 240,  48, 3));  // R0C3
	// Row 1: only 3 sprites, occupying the leftmost 3 positions
	Sprites.Add(MakeSprite( 16, 80,  48, 112, 4));  // R1 position 0
	Sprites.Add(MakeSprite( 80, 80, 112, 112, 5));  // R1 position 1
	Sprites.Add(MakeSprite(144, 80, 176, 112, 6));  // R1 position 2

	FSpriteExtractionUtils::ComputeUniformBounds(Sprites, 256, 128);

	TestEqual(TEXT("Sprite count preserved"), Sprites.Num(), 7);

	// Uniform width = MaxExtLeft(68) + MaxExtRight(16) = 84.
	// Uniform height = MaxExtTop(16) + MaxExtBottom(16) = 32.
	// Row 0 midpoints (stride 64): X=32, 96, 160, 224. Y=32.
	// Row 1 midpoints (stride 85): X=42, 127, 212. Y=96.

	// Row 0 — each cell gets 68px left-extent, 16px right. Leftmost clamps to 0.
	AssertBounds(*this, Sprites[0],   0, 16,  48, 48, TEXT("R0C0 clamped left to 0"));   // 32-68 clamped
	AssertBounds(*this, Sprites[1],  28, 16, 112, 48, TEXT("R0C1 bounds"));              // 96-68, 96+16
	AssertBounds(*this, Sprites[2],  92, 16, 176, 48, TEXT("R0C2 bounds"));              // 160-68, 160+16
	AssertBounds(*this, Sprites[3], 156, 16, 240, 48, TEXT("R0C3 bounds"));              // 224-68, 224+16

	// Row 1 — midpoints further from art due to wider stride, bounds clamp to texture edges.
	AssertBounds(*this, Sprites[4],   0, 80,  58, 112, TEXT("R1P0 clamped left"));       // 42-68 clamped, 42+16
	AssertBounds(*this, Sprites[5],  59, 80, 143, 112, TEXT("R1P1 bounds"));             // 127-68, 127+16
	AssertBounds(*this, Sprites[6], 144, 80, 228, 112, TEXT("R1P2 bounds"));             // 212-68, 212+16

	return true;
}


// =============================================================================
// TEST 4: Degenerate inputs — empty / single-sprite / invalid tex dims.
// =============================================================================
// ComputeUniformBounds early-returns on these cases. Ensures no crash and that
// inputs are untouched.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusComputeUniformBoundsDegenerate,
	"Paper2DPlus.CrossSheetAlignment.ComputeUniformBounds.Degenerate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusComputeUniformBoundsDegenerate::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusCrossSheetAlignment;

	// Empty array — must not crash.
	{
		TArray<FDetectedSprite> Empty;
		FSpriteExtractionUtils::ComputeUniformBounds(Empty, 256, 128);
		TestEqual(TEXT("Empty input stays empty"), Empty.Num(), 0);
	}

	// Single sprite — algorithm requires >= 2, should be a no-op.
	{
		TArray<FDetectedSprite> Single;
		Single.Add(MakeSprite(16, 16, 48, 48, 0));
		const FIntRect Before = Single[0].Bounds;
		FSpriteExtractionUtils::ComputeUniformBounds(Single, 256, 128);
		TestEqual(TEXT("Single-sprite input: Bounds unchanged"), Single[0].Bounds, Before);
	}

	// Zero texture dimensions — early-return guard.
	{
		TArray<FDetectedSprite> TwoSprites;
		TwoSprites.Add(MakeSprite(16, 16, 48,  48, 0));
		TwoSprites.Add(MakeSprite(80, 16, 112, 48, 1));
		const FIntRect BeforeA = TwoSprites[0].Bounds;
		const FIntRect BeforeB = TwoSprites[1].Bounds;
		FSpriteExtractionUtils::ComputeUniformBounds(TwoSprites, 0, 128);
		TestEqual(TEXT("Zero TexW: Bounds[0] unchanged"), TwoSprites[0].Bounds, BeforeA);
		TestEqual(TEXT("Zero TexW: Bounds[1] unchanged"), TwoSprites[1].Bounds, BeforeB);

		FSpriteExtractionUtils::ComputeUniformBounds(TwoSprites, 256, 0);
		TestEqual(TEXT("Zero TexH: Bounds[0] unchanged"), TwoSprites[0].Bounds, BeforeA);
		TestEqual(TEXT("Zero TexH: Bounds[1] unchanged"), TwoSprites[1].Bounds, BeforeB);
	}

	return true;
}

// =============================================================================
// TEST 5: ComputeGroupMaxCellSize — per-axis max across a texture→cell map.
// =============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusComputeGroupMaxCellSize,
	"Paper2DPlus.CrossSheetAlignment.ComputeGroupMaxCellSize",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusComputeGroupMaxCellSize::RunTest(const FString& Parameters)
{
	// Empty map → zero.
	{
		TMap<UTexture2D*, FIntPoint> Empty;
		TestEqual(TEXT("Empty map returns zero"),
			FSpriteExtractionUtils::ComputeGroupMaxCellSize(Empty),
			FIntPoint::ZeroValue);
		TestFalse(TEXT("Empty map is not extraction-ready"),
			FSpriteExtractionUtils::AreCellSizesUniform(Empty));
	}

	// Single entry → that entry's value (null key is fine, map is opaque pointer→int).
	{
		TMap<UTexture2D*, FIntPoint> Single;
		Single.Add(nullptr, FIntPoint(192, 256));
		TestEqual(TEXT("Single entry returns its value"),
			FSpriteExtractionUtils::ComputeGroupMaxCellSize(Single),
			FIntPoint(192, 256));
		TestTrue(TEXT("Single positive cell size is uniform"),
			FSpriteExtractionUtils::AreCellSizesUniform(Single));
	}

	// Per-axis max, not preserving aspect: (256,128) & (128,256) → (256,256).
	{
		TMap<UTexture2D*, FIntPoint> Mixed;
		Mixed.Add(reinterpret_cast<UTexture2D*>(0x1), FIntPoint(256, 128));
		Mixed.Add(reinterpret_cast<UTexture2D*>(0x2), FIntPoint(128, 256));
		TestEqual(TEXT("Per-axis max picks largest per axis independently"),
			FSpriteExtractionUtils::ComputeGroupMaxCellSize(Mixed),
			FIntPoint(256, 256));
		TestFalse(TEXT("Mismatched live cell sizes keep extraction blocked"),
			FSpriteExtractionUtils::AreCellSizesUniform(Mixed));
	}

	{
		TMap<UTexture2D*, FIntPoint> Equal;
		Equal.Add(reinterpret_cast<UTexture2D*>(0x1), FIntPoint(128, 128));
		Equal.Add(reinterpret_cast<UTexture2D*>(0x2), FIntPoint(128, 128));
		TestTrue(TEXT("Equal current cell sizes are extraction-ready"),
			FSpriteExtractionUtils::AreCellSizesUniform(Equal));
	}

	return true;
}


// =============================================================================
// TEST 6: InferGridDimensions — drift-detection vs ComputeUniformBounds.
// =============================================================================
// InferGridDimensions duplicates the row-grouping logic from ComputeUniformBounds
// rather than factoring it out (the stability-critical algorithm stays byte-for-
// byte unchanged). To catch silent drift between the two implementations, we run
// both on the same inputs and compare the derived (Cols, Rows) values.
//
// ComputeUniformBounds doesn't expose row structure directly — we infer it from
// the uniform Bounds output: same number of distinct Y-spans = rows; max sprites
// per Y-span = cols.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusInferGridDimensionsMatchesUniformBounds,
	"Paper2DPlus.CrossSheetAlignment.InferGridDimensions.DriftCheck",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusInferGridDimensionsMatchesUniformBounds::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusCrossSheetAlignment;

	// Fixture 1: clean 4x2 grid.
	{
		TArray<FDetectedSprite> Sprites;
		Sprites.Add(MakeSprite( 16, 16,  48,  48, 0));
		Sprites.Add(MakeSprite( 80, 16, 112,  48, 1));
		Sprites.Add(MakeSprite(144, 16, 176,  48, 2));
		Sprites.Add(MakeSprite(208, 16, 240,  48, 3));
		Sprites.Add(MakeSprite( 16, 80,  48, 112, 4));
		Sprites.Add(MakeSprite( 80, 80, 112, 112, 5));
		Sprites.Add(MakeSprite(144, 80, 176, 112, 6));
		Sprites.Add(MakeSprite(208, 80, 240, 112, 7));

		const FIntPoint Grid = FSpriteExtractionUtils::InferGridDimensions(Sprites);
		TestEqual(TEXT("4x2 grid: InferGridDimensions"), Grid, FIntPoint(4, 2));
	}

	// Fixture 2: sparse row (4 in row 0, 3 in row 1). MaxCols = 4.
	{
		TArray<FDetectedSprite> Sprites;
		Sprites.Add(MakeSprite( 16, 16,  48,  48, 0));
		Sprites.Add(MakeSprite( 80, 16, 112,  48, 1));
		Sprites.Add(MakeSprite(144, 16, 176,  48, 2));
		Sprites.Add(MakeSprite(208, 16, 240,  48, 3));
		Sprites.Add(MakeSprite( 16, 80,  48, 112, 4));
		Sprites.Add(MakeSprite( 80, 80, 112, 112, 5));
		Sprites.Add(MakeSprite(144, 80, 176, 112, 6));

		const FIntPoint Grid = FSpriteExtractionUtils::InferGridDimensions(Sprites);
		TestEqual(TEXT("Sparse row: MaxCols from the full row"), Grid, FIntPoint(4, 2));
	}

	// Fixture 3: single row of 3.
	{
		TArray<FDetectedSprite> Sprites;
		Sprites.Add(MakeSprite( 16, 16,  48, 48, 0));
		Sprites.Add(MakeSprite( 80, 16, 112, 48, 1));
		Sprites.Add(MakeSprite(144, 16, 176, 48, 2));

		const FIntPoint Grid = FSpriteExtractionUtils::InferGridDimensions(Sprites);
		TestEqual(TEXT("Single row: 3x1"), Grid, FIntPoint(3, 1));
	}

	// Fixture 4: degenerate inputs.
	{
		TArray<FDetectedSprite> Empty;
		TestEqual(TEXT("Empty: zero"),
			FSpriteExtractionUtils::InferGridDimensions(Empty), FIntPoint::ZeroValue);

		TArray<FDetectedSprite> Single;
		Single.Add(MakeSprite(16, 16, 48, 48, 0));
		TestEqual(TEXT("Single sprite: zero (< 2 required)"),
			FSpriteExtractionUtils::InferGridDimensions(Single), FIntPoint::ZeroValue);
	}

	return true;
}


// =============================================================================
// TEST 7: PadTextureInPlace — MIDPOINT-CENTRED paste moves content correctly.
// =============================================================================
// Creates a transient 64x32 texture with a recognizable pattern (opaque red left
// half, transparent right half), pads it to 128x64, and verifies the source cell's
// midpoint landed on the destination cell's midpoint:
//     CellOffsetX = 128/2 - 64/2 = 32     CellOffsetY = 64/2 - 32/2 = 16
// so the red block sits at (32,16)-(64,48) with transparent margins ABOVE AND
// BELOW. The "transparent below the paste" probe is the anti-regression assertion:
// under the retired bottom/ground-plane anchor the block would sit at y 32..63,
// flush with the canvas bottom, and row 48 would be opaque.

namespace Paper2DPlusCrossSheetAlignment
{
	/** Create a transient UTexture2D with procedural BGRA8 Source data. Fills the left-half with a
	 *  recognizable opaque red block; right half transparent. */
	static UTexture2D* CreatePaddingFixtureTexture(int32 W, int32 H)
	{
		UTexture2D* Tex = NewObject<UTexture2D>(GetTransientPackage(), NAME_None, RF_Transient);
		Tex->Source.Init(W, H, 1, 1, TSF_BGRA8);

		// FColor is BGRA in memory. Fill left half red-opaque, right half transparent.
		TArray<FColor> Pixels;
		Pixels.SetNumZeroed(W * H);
		for (int32 Y = 0; Y < H; Y++)
		{
			for (int32 X = 0; X < W / 2; X++)
			{
				Pixels[Y * W + X] = FColor(255, 0, 0, 255);
			}
		}

		uint8* Dest = Tex->Source.LockMip(0);
		FMemory::Memcpy(Dest, Pixels.GetData(), Pixels.Num() * sizeof(FColor));
		Tex->Source.UnlockMip(0);

		Tex->CompressionSettings = TC_EditorIcon;
		Tex->Filter = TF_Nearest;
		Tex->MipGenSettings = TMGS_NoMipmaps;
		Tex->LODGroup = TEXTUREGROUP_Pixels2D;
		Tex->NeverStream = true;
		Tex->SRGB = false;
		Tex->UpdateResource();
		return Tex;
	}

	// -------------------------------------------------------------------------------------------
	// Shared fixture builders for the midpoint-pad and frame-grid-detection suites below.
	// Helper names are file-unique (unity build groups every test .cpp into one TU).
	// -------------------------------------------------------------------------------------------

	/** Build a transient BGRA8 texture, filling each supplied rect with opaque white and leaving the
	 *  rest fully transparent — so alpha alone describes the sheet's occupancy, which is exactly what
	 *  both FindTightContentBounds and DetectFrameGrid read. */
	static UTexture2D* PadGrid_MakeTexture(int32 W, int32 H, const TArray<FIntRect>& FilledRects)
	{
		UTexture2D* Tex = NewObject<UTexture2D>(GetTransientPackage(), NAME_None, RF_Transient);
		Tex->Source.Init(W, H, 1, 1, TSF_BGRA8);

		TArray<FColor> Pixels;
		Pixels.SetNumZeroed(W * H);
		for (const FIntRect& Rect : FilledRects)
		{
			const int32 MinY = FMath::Max(0, Rect.Min.Y);
			const int32 MaxY = FMath::Min(H, Rect.Max.Y);
			const int32 MinX = FMath::Max(0, Rect.Min.X);
			const int32 MaxX = FMath::Min(W, Rect.Max.X);
			for (int32 Y = MinY; Y < MaxY; ++Y)
			{
				for (int32 X = MinX; X < MaxX; ++X)
				{
					Pixels[Y * W + X] = FColor::White;
				}
			}
		}

		uint8* Dest = Tex->Source.LockMip(0);
		FMemory::Memcpy(Dest, Pixels.GetData(), Pixels.Num() * sizeof(FColor));
		Tex->Source.UnlockMip(0);

		Tex->CompressionSettings = TC_EditorIcon;
		Tex->Filter = TF_Nearest;
		Tex->MipGenSettings = TMGS_NoMipmaps;
		Tex->LODGroup = TEXTUREGROUP_Pixels2D;
		Tex->NeverStream = true;
		Tex->SRGB = false;
		Tex->UpdateResource();
		return Tex;
	}

	/** One filled rect per occupied cell of a Cols x 1 sheet: cell K's content is Inset translated by
	 *  (K * CellW, 0). Cells absent from OccupiedCells stay entirely transparent. */
	static TArray<FIntRect> PadGrid_MakeRowRects(int32 CellW, const TArray<int32>& OccupiedCells, const FIntRect& Inset)
	{
		TArray<FIntRect> Out;
		Out.Reserve(OccupiedCells.Num());
		for (int32 K : OccupiedCells)
		{
			const FIntPoint Shift(K * CellW, 0);
			Out.Add(FIntRect(Inset.Min + Shift, Inset.Max + Shift));
		}
		return Out;
	}

	/** Assert a detected frame grid's cell size + layout, reporting the deciding rule and the blank
	 *  bookkeeping on failure — those are what actually explain a wrong answer. */
	static void PadGrid_ExpectGrid(
		FAutomationTestBase& Test,
		const FDetectedFrameGrid& Detected,
		FIntPoint ExpectedCell,
		FIntPoint ExpectedLayout,
		const TCHAR* Label)
	{
		if (Detected.Cell != ExpectedCell || Detected.Grid != ExpectedLayout)
		{
			Test.AddError(FString::Printf(
				TEXT("%s: expected cell %dx%d grid %dx%d, got cell %dx%d grid %dx%d (source='%s' confident=%d blanks=%d trailing=%d)"),
				Label,
				ExpectedCell.X, ExpectedCell.Y, ExpectedLayout.X, ExpectedLayout.Y,
				Detected.Cell.X, Detected.Cell.Y, Detected.Grid.X, Detected.Grid.Y,
				*Detected.Source, Detected.bConfident ? 1 : 0,
				Detected.BlankFrames.Num(), Detected.TrailingBlanks));
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusPadTextureInPlaceMidpointCentre,
	"Paper2DPlus.CrossSheetAlignment.PadTextureInPlace.MidpointCentre",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusPadTextureInPlaceMidpointCentre::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusCrossSheetAlignment;

	UTexture2D* Tex = CreatePaddingFixtureTexture(64, 32);
	TestNotNull(TEXT("Fixture texture created"), Tex);
	if (!Tex) return false;

	// Pad 64x32 → 128x64, midpoint-to-midpoint on both axes.
	// Expected paste offset: X = 128/2 - 64/2 = 32, Y = 64/2 - 32/2 = 16.
	const FIntPoint Offset = FSpriteExtractionUtils::PadTextureInPlace(Tex, FIntPoint(128, 64),
		FIntPoint(1, 1), /*GroundPlaneOffset=*/0);
	TestEqual(TEXT("Midpoint-centred paste offset"), Offset, FIntPoint(32, 16));

	// Verify new Source dims.
	TestEqual(TEXT("New Source width"),  (int64)Tex->Source.GetSizeX(), (int64)128);
	TestEqual(TEXT("New Source height"), (int64)Tex->Source.GetSizeY(), (int64) 64);

	// Read new Source bytes. Left-half red block should now sit at (32, 16)-(64, 48).
	TArray<FColor> NewPixels;
	int32 NewW = 0, NewH = 0;
	TestTrue(TEXT("LoadTextureData succeeds post-pad"),
		FSpriteExtractionUtils::LoadTextureData(Tex, NewPixels, NewW, NewH));
	if (NewW != 128 || NewH != 64) return false;

	// The whole content rect in one assertion — the offset above only pins the paste origin, this
	// pins where the ART actually ended up.
	TestEqual(TEXT("Content rect is centred on the new canvas"),
		FSpriteExtractionUtils::FindTightContentBounds(NewPixels, NewW, NewH, FIntRect(0, 0, NewW, NewH)),
		FIntRect(32, 16, 64, 48));

	// Sample corners and center of the expected red region.
	auto PixelAt = [&](int32 X, int32 Y) { return NewPixels[Y * NewW + X]; };

	// Expected red at (32, 16)-(63, 47) (32 wide * 32 tall). Check a few cells.
	TestEqual(TEXT("Red at new-canvas (32,16) — content top-left"), PixelAt(32, 16), FColor(255, 0, 0, 255));
	TestEqual(TEXT("Red at new-canvas (32,32)"), PixelAt(32, 32), FColor(255, 0, 0, 255));
	TestEqual(TEXT("Red at new-canvas (63,47) — content bottom-right"), PixelAt(63, 47), FColor(255, 0, 0, 255));

	// Expected transparent: inside the new canvas but outside the pasted region.
	TestEqual(TEXT("Transparent left of paste"),  PixelAt(31, 40).A, (uint8)0);
	TestEqual(TEXT("Transparent above paste"),    PixelAt(40, 15).A, (uint8)0);
	// THE ANCHOR RATCHET: a bottom/ground-plane anchor would place the block at y 32..63, making row
	// 48 opaque and leaving no margin under it. Midpoint centring must leave 16 blank rows below.
	TestEqual(TEXT("Transparent BELOW paste (a bottom anchor would be opaque here)"),
		PixelAt(40, 48).A, (uint8)0);
	TestEqual(TEXT("Transparent at the canvas bottom edge"), PixelAt(40, 63).A, (uint8)0);
	TestEqual(TEXT("Transparent right of paste (original right-half was also transparent, so stays transparent at (64,40))"),
		PixelAt(64, 40).A, (uint8)0);
	TestEqual(TEXT("Transparent top-left corner"), PixelAt(0, 0).A,  (uint8)0);

	return true;
}

// =============================================================================
// TEST 7b: PadTextureInPlace — per-cell deltas under midpoint centring.
// =============================================================================
// The deltas are NOT all equal, and that is deliberate: the in-cell offset is
// shared, but each delta also carries the accumulated growth of the PRECEDING
// columns/rows —
//     DeltaX = Col * (DstCellW - SrcCellW) + CellOffsetX
//     DeltaY = Row * (DstCellH - SrcCellH) + CellOffsetY
// — so ApplyCrossSheetAlignment can index the row-major array per source cell.
// What IS uniform here is dy: both cells sit in row 0, so both share CellOffsetY.
//
// The pixel probes are the motion ratchet. Cell 0's content ends 8px lower than
// cell 1's in the source; under the RETIRED ground-plane anchor both would have
// been snapped onto one shared bottom line and that 8px would be gone.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusPadTextureInPlacePerCellDeltas,
	"Paper2DPlus.CrossSheetAlignment.PadTextureInPlace.PerCellDeltas",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusPadTextureInPlacePerCellDeltas::RunTest(const FString& Parameters)
{
	constexpr int32 Width = 64;
	constexpr int32 Height = 32;
	UTexture2D* Texture = NewObject<UTexture2D>(GetTransientPackage(), NAME_None, RF_Transient);
	Texture->Source.Init(Width, Height, 1, 1, TSF_BGRA8);
	TArray<FColor> Pixels;
	Pixels.SetNumZeroed(Width * Height);
	// Cell 0 reaches its source bottom (rows 16..31). Cell 1 ends eight pixels higher (rows 8..23).
	for (int32 Y = 16; Y < 32; ++Y)
	{
		for (int32 X = 8; X < 24; ++X) Pixels[Y * Width + X] = FColor::White;
	}
	for (int32 Y = 8; Y < 24; ++Y)
	{
		for (int32 X = 40; X < 56; ++X) Pixels[Y * Width + X] = FColor::White;
	}
	uint8* Dest = Texture->Source.LockMip(0);
	FMemory::Memcpy(Dest, Pixels.GetData(), Pixels.Num() * sizeof(FColor));
	Texture->Source.UnlockMip(0);
	Texture->CompressionSettings = TC_EditorIcon;
	Texture->MipGenSettings = TMGS_NoMipmaps;
	Texture->UpdateResource();

	TArray<FIntPoint> PerCellDeltas;
	// SrcCell 32x32 → DstCell 40x40, so CellOffset = (20-16, 20-16) = (4,4) for every cell.
	// GroundPlaneOffset is deprecated and ignored; a non-zero value must not change the result.
	FSpriteExtractionUtils::PadTextureInPlace(
		Texture, FIntPoint(40, 40), FIntPoint(2, 1), /*GroundPlaneOffset=*/4, &PerCellDeltas);
	TestEqual(TEXT("One delta per row-major cell"), PerCellDeltas.Num(), 2);
	if (PerCellDeltas.Num() != 2) return false;
	TestEqual(TEXT("First cell delta is the shared in-cell offset"), PerCellDeltas[0], FIntPoint(4, 4));
	TestEqual(TEXT("Second cell adds the preceding column's growth on X only"),
		PerCellDeltas[1], FIntPoint(12, 4));
	TestEqual(TEXT("Every cell of a row receives the SAME dy"), PerCellDeltas[1].Y, PerCellDeltas[0].Y);
	TestNotEqual(TEXT("dx still accumulates preceding-cell growth (not collapsible to one delta)"),
		PerCellDeltas[1].X, PerCellDeltas[0].X);
	TestEqual(TEXT("Padded width"), static_cast<int32>(Texture->Source.GetSizeX()), 80);
	TestEqual(TEXT("Padded height"), static_cast<int32>(Texture->Source.GetSizeY()), 40);

	TArray<FColor> PaddedPixels;
	int32 PaddedW = 0;
	int32 PaddedH = 0;
	TestTrue(TEXT("Padded pixels remain readable"),
		FSpriteExtractionUtils::LoadTextureData(Texture, PaddedPixels, PaddedW, PaddedH));
	if (PaddedPixels.Num() == 0 || PaddedW != 80 || PaddedH != 40) return false;

	// Cell 0 content moved by (4,4) → rows 20..35, cols 12..27.
	// Cell 1 content moved by (12,4) → rows 12..27, cols 52..67.
	TestEqual(TEXT("Cell zero content bottom row is 35"), PaddedPixels[35 * PaddedW + 12].A, static_cast<uint8>(255));
	TestEqual(TEXT("Cell zero content stops after row 35"), PaddedPixels[36 * PaddedW + 12].A, static_cast<uint8>(0));
	TestEqual(TEXT("Cell one content bottom row is 27 — eight rows HIGHER, not snapped to a shared ground line"),
		PaddedPixels[27 * PaddedW + 52].A, static_cast<uint8>(255));
	TestEqual(TEXT("Cell one content stops after row 27"), PaddedPixels[28 * PaddedW + 52].A, static_cast<uint8>(0));
	TestEqual(TEXT("Cell one is EMPTY where a shared ground line would have put it"),
		PaddedPixels[35 * PaddedW + 52].A, static_cast<uint8>(0));

	// The 8px source difference, preserved exactly.
	const FIntRect CellZeroTight = FSpriteExtractionUtils::FindTightContentBounds(
		PaddedPixels, PaddedW, PaddedH, FIntRect(0, 0, 40, 40));
	const FIntRect CellOneTight = FSpriteExtractionUtils::FindTightContentBounds(
		PaddedPixels, PaddedW, PaddedH, FIntRect(40, 0, 80, 40));
	TestEqual(TEXT("Cell zero tight bounds after pad"), CellZeroTight, FIntRect(12, 20, 28, 36));
	TestEqual(TEXT("Cell one tight bounds after pad"), CellOneTight, FIntRect(52, 12, 68, 28));
	TestEqual(TEXT("Frame-to-frame vertical difference survives the pad (source: 32 vs 24)"),
		CellZeroTight.Max.Y - CellOneTight.Max.Y, 8);
	return true;
}


// =============================================================================
// TEST 8: SnapshotTexture / RestoreTextureSnapshot — pixel round-trip.
// =============================================================================
// Snapshot a texture, mutate it via PadTextureInPlace, restore from snapshot.
// Verify the restored bytes are byte-identical to the original. Also verifies
// CRC integrity check fires on manifest tampering.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusSnapshotRestoreRoundTrip,
	"Paper2DPlus.CrossSheetAlignment.SnapshotRestore.RoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusSnapshotRestoreRoundTrip::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusCrossSheetAlignment;

	UTexture2D* Tex = CreatePaddingFixtureTexture(64, 32);
	if (!Tex) return false;

	// Grab pre-snapshot pixels for later comparison.
	TArray<FColor> OriginalPixels;
	int32 OrigW = 0, OrigH = 0;
	FSpriteExtractionUtils::LoadTextureData(Tex, OriginalPixels, OrigW, OrigH);

	// Stage snapshot under a unique temp dir to isolate from other runs.
	const FGuid RunGuid = FGuid::NewGuid();
	const FString SaveDir = FPaths::Combine(FPaths::ProjectSavedDir(),
		TEXT("Paper2DPlus"), TEXT("TestPadBackups"), RunGuid.ToString());

	FPadSnapshotManifest Manifest = FSpriteExtractionUtils::SnapshotTexture(
		Tex, RunGuid, SaveDir, TSoftObjectPtr<UPaper2DPlusCharacterProfileAsset>());

	TestTrue(TEXT("Snapshot manifest is valid"), Manifest.IsValid());
	TestTrue(TEXT("Snapshot sidecar exists"),
		FPlatformFileManager::Get().GetPlatformFile().FileExists(*Manifest.SidecarPath));

	// Mutate the texture — pad to double size.
	const FIntPoint Offset = FSpriteExtractionUtils::PadTextureInPlace(Tex, FIntPoint(128, 64),
		FIntPoint(1, 1), /*GroundPlaneOffset=*/0);
	TestEqual(TEXT("Pad succeeded (midpoint-centred offset)"), Offset, FIntPoint(32, 16));
	TestEqual(TEXT("Texture dims mutated"), (int64)Tex->Source.GetSizeX(), (int64)128);

	// Restore from snapshot.
	const bool bRestored = FSpriteExtractionUtils::RestoreTextureSnapshot(Manifest);
	TestTrue(TEXT("Restore reports success"), bRestored);
	TestEqual(TEXT("Texture dims restored"), (int64)Tex->Source.GetSizeX(), (int64)64);
	TestEqual(TEXT("Texture height restored"), (int64)Tex->Source.GetSizeY(), (int64)32);

	// Verify pixel-identical round-trip.
	TArray<FColor> RestoredPixels;
	int32 RW = 0, RH = 0;
	FSpriteExtractionUtils::LoadTextureData(Tex, RestoredPixels, RW, RH);
	TestEqual(TEXT("Restored pixel count"), RestoredPixels.Num(), OriginalPixels.Num());
	bool bBytesMatch = (RestoredPixels.Num() == OriginalPixels.Num());
	for (int32 i = 0; bBytesMatch && i < RestoredPixels.Num(); i++)
	{
		if (RestoredPixels[i] != OriginalPixels[i]) { bBytesMatch = false; }
	}
	TestTrue(TEXT("Pixel-identical round-trip"), bBytesMatch);

	// Tamper-detection: corrupt the CRC in the manifest struct and re-attempt restore.
	FPadSnapshotManifest TamperedManifest = Manifest;
	TamperedManifest.BytesCrc32 ^= 0xDEADBEEFu;
	TestFalse(TEXT("Tampered-CRC restore is rejected"),
		FSpriteExtractionUtils::RestoreTextureSnapshot(TamperedManifest));

	// Tamper-detection: corrupt the expected dims and re-attempt.
	FPadSnapshotManifest WrongDims = Manifest;
	WrongDims.OriginalDims = FIntPoint(999, 999);
	TestFalse(TEXT("Wrong-dims restore is rejected"),
		FSpriteExtractionUtils::RestoreTextureSnapshot(WrongDims));

	// Clean up sidecar file + run directory.
	IPlatformFile& PF = FPlatformFileManager::Get().GetPlatformFile();
	PF.DeleteFile(*Manifest.SidecarPath);
	PF.DeleteDirectoryRecursively(*SaveDir);

	return true;
}


// =============================================================================
// TEST 9: ComputeUniformPreviewSize matches ComputeUniformBounds (Bounds[0]).
// =============================================================================
// The extractor's "Extraction bounds: W x H" preview is driven by
// FSpriteExtractionUtils::ComputeUniformPreviewSize, which must agree byte-for-byte
// with the real extraction path (ComputeUniformBounds) — the whole point of the
// helper is that the preview can never diverge from extraction. Mixed-height,
// multi-row, uneven-column sprites on an NPOT texture exercise the per-row stride +
// max-extent fit. The expected size is derived by running ComputeUniformBounds on a
// copy and reading Bounds[0]'s size, so this stays correct if the algorithm evolves.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusComputeUniformPreviewSizeMatchesBounds,
	"Paper2DPlus.CrossSheetAlignment.ComputeUniformPreviewSize.MatchesBounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusComputeUniformPreviewSizeMatchesBounds::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusCrossSheetAlignment;

	// NPOT texture dims, deliberately not power-of-two.
	const int32 TexW = 300;
	const int32 TexH = 170;

	// Row 0: three columns, mixed heights (a tall body, a short fragment, a mid sprite).
	// Row 1: two columns (uneven vs row 0), also mixed heights. Center-Y ranges in row 0
	// all overlap each other and none overlap row 1, so this groups as 2 rows.
	TArray<FDetectedSprite> Sprites;
	Sprites.Add(MakeSprite( 20,  20,  60,  64, 0));  // R0C0 — 40x44 tall
	Sprites.Add(MakeSprite(120,  40, 150,  58, 1));  // R0C1 — 30x18 short
	Sprites.Add(MakeSprite(210,  24, 256,  60, 2));  // R0C2 — 46x36 mid
	Sprites.Add(MakeSprite( 30, 100,  78, 150, 3));  // R1C0 — 48x50 tall
	Sprites.Add(MakeSprite(170, 110, 205, 140, 4));  // R1C1 — 35x30 mid

	// Expected = run the real algorithm on a copy and read Bounds[0]'s size.
	TArray<FDetectedSprite> Expected = Sprites;
	FSpriteExtractionUtils::ComputeUniformBounds(Expected, TexW, TexH);
	const FIntPoint ExpectedSize = Expected[0].GetSize();

	const FIntPoint PreviewSize = FSpriteExtractionUtils::ComputeUniformPreviewSize(Sprites, TexW, TexH);
	TestEqual(TEXT("Preview size == ComputeUniformBounds Bounds[0] size"), PreviewSize, ExpectedSize);

	// The preview helper must NOT mutate the caller's sprites (it operates on a copy).
	TestEqual(TEXT("Input Sprites[0] Bounds untouched by preview"),
		Sprites[0].Bounds, FIntRect(20, 20, 60, 64));

	// Degenerate guards: < 2 sprites and non-positive dims return zero.
	{
		TArray<FDetectedSprite> One;
		One.Add(MakeSprite(20, 20, 60, 64, 0));
		TestEqual(TEXT("Single sprite returns zero"),
			FSpriteExtractionUtils::ComputeUniformPreviewSize(One, TexW, TexH), FIntPoint::ZeroValue);

		TestEqual(TEXT("Zero TexW returns zero"),
			FSpriteExtractionUtils::ComputeUniformPreviewSize(Sprites, 0, TexH), FIntPoint::ZeroValue);
		TestEqual(TEXT("Zero TexH returns zero"),
			FSpriteExtractionUtils::ComputeUniformPreviewSize(Sprites, TexW, 0), FIntPoint::ZeroValue);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusSpriteOffsetPersistentDataTest,
	"Paper2DPlus.CrossSheetAlignment.SpriteOffset.PersistentProfileDataDoesNotMutateSharedPivot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusSpriteOffsetPersistentDataTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Profile =
		NewObject<UPaper2DPlusCharacterProfileAsset>();
	FFlipbookProfileEntry Entry;
	Entry.Identity.FlipbookName = TEXT("Attack");
	Entry.CombatData.Frames.SetNum(1);
	Entry.CombatData.FrameExtractionInfo.SetNum(1);

	const FString PackageName = FString::Printf(
		TEXT("/Engine/Transient/Paper2DPlusSpritePersistent_%s"),
		*FGuid::NewGuid().ToString(EGuidFormats::Digits));
	UPackage* SpritePackage = CreatePackage(*PackageName);
	UPaperSprite* SharedSprite = NewObject<UPaperSprite>(
		SpritePackage, TEXT("SharedSprite"), RF_Public | RF_Standalone);
	UPaperFlipbook* Flipbook = NewObject<UPaperFlipbook>(Profile);
	{
		FScopedFlipbookMutator Mutator(Flipbook);
		FPaperFlipbookKeyFrame KeyFrame;
		KeyFrame.Sprite = SharedSprite;
		KeyFrame.FrameRun = 1;
		Mutator.KeyFrames.Add(KeyFrame);
	}
	Entry.Identity.Flipbook = Flipbook;
	Profile->Flipbooks.Add(MoveTemp(Entry));
	SpritePackage->SetDirtyFlag(false);
	const FVector2D PivotBefore = SharedSprite->GetPivotPosition();

	FSpriteExtractionInfo& ExtractionInfo =
		Profile->Flipbooks[0].CombatData.FrameExtractionInfo[0];
	FBoolProperty* LegacyAlignmentProperty = FindFProperty<FBoolProperty>(
		FSpriteExtractionInfo::StaticStruct(), TEXT("bHasCustomAlignment"));
	TestNotNull(TEXT("legacy alignment marker remains loadable under its original reflected name"),
		LegacyAlignmentProperty);
	if (LegacyAlignmentProperty)
	{
		TestTrue(TEXT("legacy alignment marker is explicitly deprecated"),
		LegacyAlignmentProperty->HasAnyPropertyFlags(CPF_Deprecated));
		TestFalse(TEXT("legacy alignment marker is no longer editable in property details"),
		LegacyAlignmentProperty->HasAnyPropertyFlags(CPF_Edit));
		LegacyAlignmentProperty->SetPropertyValue_InContainer(&ExtractionInfo, true);
	}

	ExtractionInfo.SpriteOffset = FIntPoint(6, -4);
	TestEqual(TEXT("frame-strip preparation resolves the persistent Profile data"),
		Paper2DPlusEditor::SpriteEditorPanelUtils::PrepareFrameStripFrameCount(Profile, 0), 1);
	TestEqual(TEXT("editor preparation never changes the shared sprite pivot"),
		SharedSprite->GetPivotPosition(), PivotBefore);
	TestFalse(TEXT("editor preparation never dirties the unrelated sprite package"),
		SpritePackage->IsDirty());
	TestEqual(TEXT("editor preparation preserves the authored Profile offset"),
		ExtractionInfo.SpriteOffset,
		FIntPoint(6, -4));

	SharedSprite->ClearFlags(RF_Standalone);
	SpritePackage->SetDirtyFlag(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusApplyCrossSheetPerCellPlacement,
	"Paper2DPlus.CrossSheetAlignment.Apply.PerCellPlacement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusApplyCrossSheetPerCellPlacement::RunTest(const FString& Parameters)
{
	UTexture2D* Texture = NewObject<UTexture2D>(GetTransientPackage(), NAME_None, RF_Transient);
	Texture->Source.Init(80, 40, 1, 1, TSF_BGRA8);

	UPaper2DPlusCharacterProfileAsset* Profile =
		NewObject<UPaper2DPlusCharacterProfileAsset>(GetTransientPackage());
	UPaperFlipbook* Flipbook = NewObject<UPaperFlipbook>(Profile);
	UPaperSprite* Sprites[2] = {};
	for (int32 FrameIndex = 0; FrameIndex < 2; ++FrameIndex)
	{
		Sprites[FrameIndex] = NewObject<UPaperSprite>(Profile);
		FSpriteAssetInitParameters Init;
		Init.Texture = Texture;
		Init.Offset = FIntPoint(FrameIndex * 32, 0);
		Init.Dimension = FIntPoint(32, 32);
		Sprites[FrameIndex]->InitializeSprite(Init);
		Sprites[FrameIndex]->SetPivotMode(
			ESpritePivotMode::Center_Center, FVector2D::ZeroVector, true);
	}
	{
		FScopedFlipbookMutator Mutator(Flipbook);
		// Deliberately reverse the source-cell order. Alignment must resolve from each sprite's old
		// SourceUV rather than assuming flipbook keyframe K is sheet cell K.
		for (int32 FrameIndex = 1; FrameIndex >= 0; --FrameIndex)
		{
			FPaperFlipbookKeyFrame Frame;
			Frame.Sprite = Sprites[FrameIndex];
			Frame.FrameRun = 1;
			Mutator.KeyFrames.Add(Frame);
		}
	}

	FFlipbookProfileEntry Entry;
	Entry.Identity.FlipbookName = TEXT("Existing");
	Entry.Identity.Flipbook = Flipbook;
	Entry.CombatData.Frames.SetNum(2);
	Entry.CombatData.FrameExtractionInfo.SetNum(2);
	Entry.CombatData.FrameExtractionInfo[0].SourceOffset = FIntPoint(40, 8);
	Entry.CombatData.FrameExtractionInfo[1].SourceOffset = FIntPoint(8, 16);
	Entry.CombatData.FrameExtractionInfo[0].SpriteOffset = FIntPoint(3, -2);
	Entry.CombatData.FrameExtractionInfo[1].TrimOffset = FIntPoint(-1, 5);
	FHitboxData Hitbox;
	Hitbox.X = 10;
	Hitbox.Y = 10;
	Entry.CombatData.Frames[0].Hitboxes.Add(Hitbox);
	Entry.CombatData.Frames[1].Hitboxes.Add(Hitbox);
	Profile->Flipbooks.Add(MoveTemp(Entry));
	UPaper2DPlusCharacterProfileAsset* SisterProfile =
		NewObject<UPaper2DPlusCharacterProfileAsset>(GetTransientPackage());
	SisterProfile->Flipbooks.Add(Profile->Flipbooks[0]);
	FFlipbookProfileEntry AlreadyPaddedEntry = Profile->Flipbooks[0];
	AlreadyPaddedEntry.Identity.FlipbookName = TEXT("CreatedDuringCommit");
	AlreadyPaddedEntry.CombatData.FrameExtractionInfo[0].SourceOffset = FIntPoint(0, 0);
	Profile->Flipbooks.Add(MoveTemp(AlreadyPaddedEntry));

	TMap<UTexture2D*, TArray<FIntPoint>> Placements;
	Placements.Add(Texture, { FIntPoint(4, 4), FIntPoint(12, 12) });
	TMap<UTexture2D*, FIntPoint> OriginalCellSizes;
	OriginalCellSizes.Add(Texture, FIntPoint(32, 32));
	TMap<UPaper2DPlusCharacterProfileAsset*, int32> EntryLimits;
	EntryLimits.Add(Profile, 1);
	const FCrossSheetAlignmentStats Stats = FSpriteExtractionUtils::ApplyCrossSheetAlignment(
		{ Profile, SisterProfile }, FIntPoint(40, 40), OriginalCellSizes, Placements, EntryLimits);

	TestEqual(TEXT("Shared sprite assets migrate once"), Stats.SpritesUpdated, 2);
	TestEqual(TEXT("Both Profiles migrate their own metadata"), Stats.ProfilesTouched, 2);
	TestEqual(TEXT("First sprite moves into padded cell zero"),
		FIntPoint(FMath::RoundToInt(Sprites[0]->GetSourceUV().X), FMath::RoundToInt(Sprites[0]->GetSourceUV().Y)),
		FIntPoint(0, 0));
	TestEqual(TEXT("Second sprite uses its own cell delta, not frame zero's"),
		FIntPoint(FMath::RoundToInt(Sprites[1]->GetSourceUV().X), FMath::RoundToInt(Sprites[1]->GetSourceUV().Y)),
		FIntPoint(40, 0));
	TestEqual(TEXT("First keyframe follows source cell one's placement"),
		Profile->Flipbooks[0].CombatData.FrameExtractionInfo[0].SourceOffset, FIntPoint(52, 20));
	TestEqual(TEXT("Second keyframe follows source cell zero's placement"),
		Profile->Flipbooks[0].CombatData.FrameExtractionInfo[1].SourceOffset, FIntPoint(12, 20));
	TestEqual(TEXT("First hitbox keeps source cell one's distinct vertical shift"),
		Profile->Flipbooks[0].CombatData.Frames[0].Hitboxes[0].Y, 22);
	TestEqual(TEXT("Second hitbox follows source cell zero's placement"),
		Profile->Flipbooks[0].CombatData.Frames[1].Hitboxes[0].X, 14);
	TestEqual(TEXT("Authored SpriteOffset survives auto-pad"),
		Profile->Flipbooks[0].CombatData.FrameExtractionInfo[0].SpriteOffset, FIntPoint(3, -2));
	TestEqual(TEXT("Authored TrimOffset survives auto-pad"),
		Profile->Flipbooks[0].CombatData.FrameExtractionInfo[1].TrimOffset, FIntPoint(-1, 5));
	TestEqual(TEXT("Rows created during commit never receive the placement twice"),
		Profile->Flipbooks[1].CombatData.FrameExtractionInfo[0].SourceOffset, FIntPoint(0, 0));
	TestEqual(TEXT("Sister Profile metadata follows shared source cell one"),
		SisterProfile->Flipbooks[0].CombatData.FrameExtractionInfo[0].SourceOffset, FIntPoint(52, 20));
	TestEqual(TEXT("Sister Profile metadata follows shared source cell zero"),
		SisterProfile->Flipbooks[0].CombatData.FrameExtractionInfo[1].SourceOffset, FIntPoint(12, 20));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusMaxExtentTargetsIgnoreBlankFrames,
	"Paper2DPlus.CrossSheetAlignment.MaxExtentTargets.BlankFrames",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusMaxExtentTargetsIgnoreBlankFrames::RunTest(const FString& Parameters)
{
	TArray<FSpriteMaxExtentFrame> Frames;
	FSpriteMaxExtentFrame& First = Frames.AddDefaulted_GetRef();
	First.ContainerBounds = FIntRect(0, 0, 64, 64);
	First.TightBounds = FIntRect(20, 16, 44, 56);
	First.Anchor = FIntPoint(32, 32);
	FSpriteMaxExtentFrame& Second = Frames.AddDefaulted_GetRef();
	Second.ContainerBounds = FIntRect(64, 0, 128, 64);
	Second.TightBounds = FIntRect(76, 12, 116, 60);
	Second.Anchor = FIntPoint(96, 32);
	FSpriteMaxExtentFrame& Blank = Frames.AddDefaulted_GetRef();
	Blank.ContainerBounds = FIntRect(128, 0, 192, 64);
	Blank.TightBounds = FIntRect(128, 0, 128, 0);
	Blank.Anchor = FIntPoint(160, 32);

	const FSpriteMaxExtentResult Result = FSpriteExtractionUtils::ComputeMaxExtentTargets(Frames);
	TestTrue(TEXT("Non-empty frames establish a valid result"), Result.IsValid());
	TestEqual(TEXT("Blank frame is counted"), Result.EmptyFrames, 1);
	TestEqual(TEXT("Blank frame does not inflate the 40x48 maximum"),
		Result.GetUniformSize(), FIntPoint(40, 48));
	TestEqual(TEXT("Blank frame still receives the uniform target"),
		Frames[2].TargetBounds, FIntRect(140, 12, 180, 60));
	TestTrue(TEXT("Blank target fits its source cell"), Frames[2].bTargetFits);
	TestEqual(TEXT("Packed metadata keeps the tight content origin in packed-texture coordinates"),
		FSpriteExtractionUtils::ComputePackedContentOffset(
			FIntRect(12, 12, 52, 60), FIntRect(20, 16, 44, 56), FIntRect(80, 0, 120, 48)),
		FIntPoint(88, 4));
	TestEqual(TEXT("Blank packed metadata resolves to the packed frame origin"),
		FSpriteExtractionUtils::ComputePackedContentOffset(
			FIntRect(140, 12, 180, 60), FIntRect(140, 12, 140, 12), FIntRect(120, 0, 160, 48)),
		FIntPoint(120, 0));
	return true;
}


// =============================================================================
// TEST 12: THE INVARIANT — trimmed + midpoint == untrimmed + centred.
// =============================================================================
// The two extraction paths must place identical art at identical positions:
//
//   Path A (untrimmed): PadTextureInPlace centres each source cell inside its
//                       destination cell, so the source cell MIDPOINT lands on
//                       the destination cell MIDPOINT.
//   Path B (trimmed):   the bulk extractor's uniform trim anchors every frame on
//                       that same cell midpoint (max directional extent from it),
//                       then bakes the pivot at (MaxExtLeft, MaxExtTop) — the
//                       packed-space point that IS the midpoint.
//
// Both therefore express content position as an offset from ONE shared anchor,
// and this test measures that offset on each path — Path A from the real padded
// pixels, Path B from the real trim planner — and requires them equal.
//
// The fixture's content is deliberately ASYMMETRIC about the cell midpoint on
// both axes, so the trimmed box's own CENTRE is a different point. The closing
// guard asserts that box-centre anchoring DISAGREES; without it the equality
// above could pass vacuously on symmetric art. Never anchor on the box centre —
// it differs on 96% of real character sheets, by up to 16px.
//
// One frame is blank: it must be excluded from the maxima (so it cannot inflate
// the uniform size) while still receiving the shared target.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusPadTrimMidpointAgreement,
	"Paper2DPlus.CrossSheetAlignment.PadTextureInPlace.TrimAndPadAgree",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusPadTrimMidpointAgreement::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusCrossSheetAlignment;

	constexpr int32 SrcCellW = 32;
	constexpr int32 SrcCellH = 32;
	constexpr int32 Cols = 3;
	constexpr int32 SrcW = Cols * SrcCellW;   // 96
	constexpr int32 SrcH = SrcCellH;          // 32
	constexpr int32 DstCell = 48;

	// Cell midpoints are (16,16), (48,16), (80,16).
	//   frame 0 → left 14 / right  4, top 12 / bottom 6
	//   frame 1 → left  8 / right 10, top  6 / bottom 8
	//   frame 2 → blank
	TArray<FIntRect> Content;
	Content.Add(FIntRect(2, 4, 20, 22));
	Content.Add(FIntRect(40, 10, 58, 24));
	UTexture2D* Tex = PadGrid_MakeTexture(SrcW, SrcH, Content);
	TestNotNull(TEXT("Fixture texture created"), Tex);
	if (!Tex) return false;

	// ---------------------------------------------------------------------
	// Path B: plan the uniform trim exactly the way the bulk extractor does.
	// ---------------------------------------------------------------------
	TArray<FColor> SrcPixels;
	int32 ReadW = 0, ReadH = 0;
	TestTrue(TEXT("Source pixels readable"),
		FSpriteExtractionUtils::LoadTextureData(Tex, SrcPixels, ReadW, ReadH));
	if (ReadW != SrcW || ReadH != SrcH) return false;

	TArray<FIntRect> SourceTights;
	TArray<FSpriteMaxExtentFrame> ExtentFrames;
	SourceTights.Reserve(Cols);
	ExtentFrames.Reserve(Cols);
	for (int32 K = 0; K < Cols; ++K)
	{
		const FIntRect Cell(K * SrcCellW, 0, (K + 1) * SrcCellW, SrcCellH);
		const FIntRect Tight = FSpriteExtractionUtils::FindTightContentBounds(SrcPixels, SrcW, SrcH, Cell);
		SourceTights.Add(Tight);

		FSpriteMaxExtentFrame& Frame = ExtentFrames.AddDefaulted_GetRef();
		Frame.ContainerBounds = Cell;
		Frame.TightBounds = Tight;
		Frame.Anchor = FIntPoint(Cell.Min.X + SrcCellW / 2, Cell.Min.Y + SrcCellH / 2);
	}

	const FSpriteMaxExtentResult Extents = FSpriteExtractionUtils::ComputeMaxExtentTargets(ExtentFrames);
	TestTrue(TEXT("Uniform extents are valid"), Extents.IsValid());
	TestEqual(TEXT("Blank frame excluded from the maxima"), Extents.EmptyFrames, 1);
	TestEqual(TEXT("Two frames contributed content"), Extents.NonEmptyFrames, 2);
	TestEqual(TEXT("Uniform size comes from the non-blank frames only"),
		Extents.GetUniformSize(), FIntPoint(24, 20));
	TestEqual(TEXT("Every uniform target fits inside its source cell"), Extents.FramesOutsideContainers, 0);
	TestEqual(TEXT("Blank frame still receives the shared uniform target"),
		ExtentFrames[2].TargetBounds, FIntRect(66, 4, 90, 24));

	// Without asymmetry the box-centre guard below proves nothing.
	TestNotEqual(TEXT("Fixture is asymmetric about the midpoint horizontally"), Extents.MaxLeft, Extents.MaxRight);
	TestNotEqual(TEXT("Fixture is asymmetric about the midpoint vertically"), Extents.MaxTop, Extents.MaxBottom);

	// ---------------------------------------------------------------------
	// Path A: pad the UNTRIMMED sheet and measure the real pixels.
	// ---------------------------------------------------------------------
	TArray<FIntPoint> PerCellDeltas;
	FSpriteExtractionUtils::PadTextureInPlace(
		Tex, FIntPoint(DstCell, DstCell), FIntPoint(Cols, 1), /*GroundPlaneOffset=*/0, &PerCellDeltas);
	TestEqual(TEXT("One paste delta per cell"), PerCellDeltas.Num(), Cols);

	TArray<FColor> PadPixels;
	int32 PadW = 0, PadH = 0;
	TestTrue(TEXT("Padded pixels readable"),
		FSpriteExtractionUtils::LoadTextureData(Tex, PadPixels, PadW, PadH));
	TestEqual(TEXT("Padded width"), PadW, Cols * DstCell);
	TestEqual(TEXT("Padded height"), PadH, DstCell);
	if (PadW != Cols * DstCell || PadH != DstCell) return false;

	// ---------------------------------------------------------------------
	// The two paths must agree, frame by frame.
	// ---------------------------------------------------------------------
	const FIntPoint MidpointPivot(Extents.MaxLeft, Extents.MaxTop);
	const FIntPoint BoxCentrePivot(Extents.GetUniformSize().X / 2, Extents.GetUniformSize().Y / 2);
	TestNotEqual(TEXT("The two candidate anchors are genuinely different points"),
		MidpointPivot, BoxCentrePivot);

	int32 ComparedFrames = 0;
	for (int32 K = 0; K < Cols; ++K)
	{
		if (ExtentFrames[K].bEmpty) continue;
		++ComparedFrames;

		// Trim path: content offset from the BAKED pivot inside the packed region.
		const FIntPoint ContentInPacked = SourceTights[K].Min - ExtentFrames[K].TargetBounds.Min;
		const FIntPoint TrimRelative = ContentInPacked - MidpointPivot;

		// Pad path: content offset from the DESTINATION cell midpoint, read off real pixels.
		const FIntRect DestCellRect(K * DstCell, 0, (K + 1) * DstCell, DstCell);
		const FIntRect PaddedTight =
			FSpriteExtractionUtils::FindTightContentBounds(PadPixels, PadW, PadH, DestCellRect);
		const FIntPoint DestMidpoint(DestCellRect.Min.X + DstCell / 2, DestCellRect.Min.Y + DstCell / 2);
		const FIntPoint PadRelative = PaddedTight.Min - DestMidpoint;

		TestEqual(*FString::Printf(TEXT("Frame %d: trimmed+midpoint lands exactly where untrimmed+centred does"), K),
			TrimRelative, PadRelative);
		TestEqual(*FString::Printf(TEXT("Frame %d: content size survives the pad"), K),
			FIntPoint(PaddedTight.Width(), PaddedTight.Height()),
			FIntPoint(SourceTights[K].Width(), SourceTights[K].Height()));

		// GUARD: the box-centre anchor MUST disagree, or the equality above is vacuous.
		const FIntPoint BoxRelative = ContentInPacked - BoxCentrePivot;
		TestNotEqual(*FString::Printf(TEXT("Frame %d: box-centre anchoring DISAGREES — never anchor there"), K),
			BoxRelative, PadRelative);
	}
	TestEqual(TEXT("Both non-blank frames were compared"), ComparedFrames, 2);

	return true;
}


// =============================================================================
// TEST 13: RIGIDITY — every cell of a padded sheet receives the SAME dy.
// =============================================================================
// This is what preserves an animation's vertical motion. The retired ground-plane
// anchor scanned each cell for its own content bottom and snapped it to a fixed
// line, which collapsed every frame's dy to whatever that frame needed —
// measured on real sheets, Aerial_DownAttack's 26px drop went to 0 and Roll's
// 9px rise to 0. Under midpoint centring the offset is content-INDEPENDENT.
//
// The fixture's four frames have deliberately different content heights and
// vertical positions (a jump arc), so a content-driven anchor would produce four
// different dy values.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusPadTextureInPlaceRigidVerticalOffset,
	"Paper2DPlus.CrossSheetAlignment.PadTextureInPlace.RigidVerticalOffset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusPadTextureInPlaceRigidVerticalOffset::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusCrossSheetAlignment;

	constexpr int32 SrcCellW = 32;
	constexpr int32 SrcCellH = 32;
	constexpr int32 Cols = 4;
	constexpr int32 SrcW = Cols * SrcCellW;   // 128
	constexpr int32 DstCellW = 48;
	constexpr int32 DstCellH = 64;

	// Frame k content: x [32k+8, 32k+24), y as listed. Heights differ too (10, 10, 10, 10 rows of
	// content but at four distinct vertical positions), so no shared bottom line exists.
	const int32 SourceTops[Cols] = { 20, 12, 4, 14 };
	const int32 SourceBottoms[Cols] = { 30, 22, 14, 24 };
	TArray<FIntRect> Content;
	for (int32 K = 0; K < Cols; ++K)
	{
		Content.Add(FIntRect(K * SrcCellW + 8, SourceTops[K], K * SrcCellW + 24, SourceBottoms[K]));
	}
	UTexture2D* Tex = PadGrid_MakeTexture(SrcW, SrcCellH, Content);
	TestNotNull(TEXT("Fixture texture created"), Tex);
	if (!Tex) return false;

	TArray<FIntPoint> Deltas;
	FSpriteExtractionUtils::PadTextureInPlace(
		Tex, FIntPoint(DstCellW, DstCellH), FIntPoint(Cols, 1), /*GroundPlaneOffset=*/0, &Deltas);
	TestEqual(TEXT("One delta per cell"), Deltas.Num(), Cols);
	if (Deltas.Num() != Cols) return false;

	// CellOffsetY = 64/2 - 32/2 = 16, shared by every cell of the single row.
	for (int32 K = 0; K < Cols; ++K)
	{
		TestEqual(*FString::Printf(TEXT("Cell %d receives the shared dy"), K), Deltas[K].Y, 16);
		TestEqual(*FString::Printf(TEXT("Cell %d dx carries its preceding columns' growth"), K),
			Deltas[K].X, K * (DstCellW - SrcCellW) + 8);
	}

	TArray<FColor> PadPixels;
	int32 PadW = 0, PadH = 0;
	TestTrue(TEXT("Padded pixels readable"),
		FSpriteExtractionUtils::LoadTextureData(Tex, PadPixels, PadW, PadH));
	TestEqual(TEXT("Padded width"), PadW, Cols * DstCellW);
	TestEqual(TEXT("Padded height"), PadH, DstCellH);
	if (PadW != Cols * DstCellW || PadH != DstCellH) return false;

	TArray<FIntRect> PaddedTights;
	PaddedTights.Reserve(Cols);
	for (int32 K = 0; K < Cols; ++K)
	{
		const FIntRect DestCellRect(K * DstCellW, 0, (K + 1) * DstCellW, DstCellH);
		PaddedTights.Add(FSpriteExtractionUtils::FindTightContentBounds(PadPixels, PadW, PadH, DestCellRect));
	}

	// Absolute placement: every frame moved down by exactly the shared dy.
	for (int32 K = 0; K < Cols; ++K)
	{
		TestEqual(*FString::Printf(TEXT("Frame %d moved down by the shared dy, nothing else"), K),
			PaddedTights[K].Min.Y, SourceTops[K] + 16);
	}

	// Relative placement: frame-to-frame vertical relationships are preserved EXACTLY.
	for (int32 K = 1; K < Cols; ++K)
	{
		TestEqual(*FString::Printf(TEXT("Frame %d keeps its vertical offset from frame 0"), K),
			PaddedTights[K].Min.Y - PaddedTights[0].Min.Y,
			SourceTops[K] - SourceTops[0]);
	}

	// Anti-ground-plane guard: content bottoms must NOT have collapsed onto one line.
	TestNotEqual(TEXT("Frame bottoms did not snap to a shared ground line"),
		PaddedTights[0].Max.Y, PaddedTights[2].Max.Y);
	TestEqual(TEXT("The 16px source spread between the lowest and highest frame survives"),
		PaddedTights[0].Max.Y - PaddedTights[2].Max.Y, SourceBottoms[0] - SourceBottoms[2]);

	// ---------------------------------------------------------------------
	// Multi-row: dy differs per ROW (it accumulates the rows above it), but the
	// IN-CELL offset is still one shared constant.
	// ---------------------------------------------------------------------
	{
		TArray<FIntRect> GridContent;
		GridContent.Add(FIntRect(4, 4, 20, 20));
		GridContent.Add(FIntRect(40, 8, 56, 28));
		GridContent.Add(FIntRect(6, 40, 26, 56));
		GridContent.Add(FIntRect(36, 34, 60, 60));
		UTexture2D* GridTex = PadGrid_MakeTexture(64, 64, GridContent);
		TestNotNull(TEXT("Multi-row fixture created"), GridTex);
		if (!GridTex) return false;

		TArray<FIntPoint> GridDeltas;
		FSpriteExtractionUtils::PadTextureInPlace(
			GridTex, FIntPoint(40, 40), FIntPoint(2, 2), /*GroundPlaneOffset=*/0, &GridDeltas);
		TestEqual(TEXT("One delta per row-major cell of the 2x2 grid"), GridDeltas.Num(), 4);
		if (GridDeltas.Num() != 4) return false;

		// SrcCell 32x32 → DstCell 40x40: growth 8 per axis, shared in-cell offset (4,4).
		for (int32 Index = 0; Index < 4; ++Index)
		{
			const int32 Row = Index / 2;
			const int32 Col = Index % 2;
			TestEqual(*FString::Printf(TEXT("Cell %d in-cell X offset is shared"), Index),
				GridDeltas[Index].X - Col * 8, 4);
			TestEqual(*FString::Printf(TEXT("Cell %d in-cell Y offset is shared"), Index),
				GridDeltas[Index].Y - Row * 8, 4);
		}
		TestEqual(TEXT("Row-major push order: (0,0)"), GridDeltas[0], FIntPoint(4, 4));
		TestEqual(TEXT("Row-major push order: (0,1)"), GridDeltas[1], FIntPoint(12, 4));
		TestEqual(TEXT("Row-major push order: (1,0)"), GridDeltas[2], FIntPoint(4, 12));
		TestEqual(TEXT("Row-major push order: (1,1)"), GridDeltas[3], FIntPoint(12, 12));
	}

	return true;
}


// =============================================================================
// TEST 14: ApplyCrossSheetAlignment — the Center anchor on a MULTI-ROW sheet.
// =============================================================================
// Center is the partner of PadTextureInPlace's midpoint pad anchor. Single-row
// sheets resolve identically under Center and BottomCenter (the texture clamp
// pins MinY to 0), so Apply.PerCellPlacement above cannot see the difference —
// this fixture is deliberately 1 column x 2 rows, where they diverge:
//
//   row-1 sprite, padded rect y 44..76, uniform 40
//     Center       → 40..80  = destination cell row 1 exactly
//     BottomCenter → 36..76  = straddles BOTH destination cells, and HitboxDelta
//                              is then computed against the wrong origin.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusApplyCrossSheetMultiRowAnchor,
	"Paper2DPlus.CrossSheetAlignment.Apply.MultiRowCentreAnchor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusApplyCrossSheetMultiRowAnchor::RunTest(const FString& Parameters)
{
	// Already-padded sheet: 1 column x 2 rows of 40x40 destination cells.
	UTexture2D* Texture = NewObject<UTexture2D>(GetTransientPackage(), NAME_None, RF_Transient);
	Texture->Source.Init(40, 80, 1, 1, TSF_BGRA8);

	UPaper2DPlusCharacterProfileAsset* Profile =
		NewObject<UPaper2DPlusCharacterProfileAsset>(GetTransientPackage());
	UPaperFlipbook* Flipbook = NewObject<UPaperFlipbook>(Profile);
	UPaperSprite* Sprites[2] = {};
	for (int32 FrameIndex = 0; FrameIndex < 2; ++FrameIndex)
	{
		Sprites[FrameIndex] = NewObject<UPaperSprite>(Profile);
		FSpriteAssetInitParameters Init;
		Init.Texture = Texture;
		// Pre-pad source rects: row 0 at y 0, row 1 at y 32 (source cells are 32x32).
		Init.Offset = FIntPoint(0, FrameIndex * 32);
		Init.Dimension = FIntPoint(32, 32);
		Sprites[FrameIndex]->InitializeSprite(Init);
		Sprites[FrameIndex]->SetPivotMode(
			ESpritePivotMode::Center_Center, FVector2D::ZeroVector, true);
	}
	{
		FScopedFlipbookMutator Mutator(Flipbook);
		for (int32 FrameIndex = 0; FrameIndex < 2; ++FrameIndex)
		{
			FPaperFlipbookKeyFrame Frame;
			Frame.Sprite = Sprites[FrameIndex];
			Frame.FrameRun = 1;
			Mutator.KeyFrames.Add(Frame);
		}
	}

	FFlipbookProfileEntry Entry;
	Entry.Identity.FlipbookName = TEXT("TwoRow");
	Entry.Identity.Flipbook = Flipbook;
	Entry.CombatData.Frames.SetNum(2);
	Entry.CombatData.FrameExtractionInfo.SetNum(2);
	FHitboxData Hitbox;
	Hitbox.X = 10;
	Hitbox.Y = 10;
	Entry.CombatData.Frames[0].Hitboxes.Add(Hitbox);
	Entry.CombatData.Frames[1].Hitboxes.Add(Hitbox);
	Profile->Flipbooks.Add(MoveTemp(Entry));

	// PadTextureInPlace(32x64 → target cell 40x40, grid 1x2) yields these row-major deltas:
	// CellOffset = (4,4); row 1 additionally carries the preceding row's 8px growth.
	TMap<UTexture2D*, TArray<FIntPoint>> Placements;
	Placements.Add(Texture, { FIntPoint(4, 4), FIntPoint(4, 12) });
	TMap<UTexture2D*, FIntPoint> OriginalCellSizes;
	OriginalCellSizes.Add(Texture, FIntPoint(32, 32));

	const FCrossSheetAlignmentStats Stats = FSpriteExtractionUtils::ApplyCrossSheetAlignment(
		{ Profile }, FIntPoint(40, 40), OriginalCellSizes, Placements,
		TMap<UPaper2DPlusCharacterProfileAsset*, int32>());

	TestEqual(TEXT("Both sprites migrate"), Stats.SpritesUpdated, 2);
	TestEqual(TEXT("No sprite was skipped as unresolvable"), Stats.SpritesSkipped, 0);

	TestEqual(TEXT("Row-0 sprite lands on destination cell row 0"),
		FIntPoint(FMath::RoundToInt(Sprites[0]->GetSourceUV().X), FMath::RoundToInt(Sprites[0]->GetSourceUV().Y)),
		FIntPoint(0, 0));
	// THE RATCHET: BottomCenter would resolve this to (0,36) — straddling both destination cells.
	TestEqual(TEXT("Row-1 sprite lands on destination cell row 1, not straddling two cells"),
		FIntPoint(FMath::RoundToInt(Sprites[1]->GetSourceUV().X), FMath::RoundToInt(Sprites[1]->GetSourceUV().Y)),
		FIntPoint(0, 40));
	TestEqual(TEXT("Row-1 sprite keeps the full uniform cell size"),
		FIntPoint(FMath::RoundToInt(Sprites[1]->GetSourceSize().X), FMath::RoundToInt(Sprites[1]->GetSourceSize().Y)),
		FIntPoint(40, 40));

	// HitboxDelta = paddedRect.Min - newBounds.Min = (4,4) for BOTH rows under Center.
	// BottomCenter would give row 1 a delta of (4,8) → hitbox Y 18 instead of 14.
	TestEqual(TEXT("Row-0 hitbox remapped by the in-cell offset"),
		FIntPoint(Profile->Flipbooks[0].CombatData.Frames[0].Hitboxes[0].X,
			Profile->Flipbooks[0].CombatData.Frames[0].Hitboxes[0].Y),
		FIntPoint(14, 14));
	TestEqual(TEXT("Row-1 hitbox remapped by the SAME in-cell offset, not a straddled origin"),
		FIntPoint(Profile->Flipbooks[0].CombatData.Frames[1].Hitboxes[0].X,
			Profile->Flipbooks[0].CombatData.Frames[1].Hitboxes[0].Y),
		FIntPoint(14, 14));

	// Phase 1 moved SourceOffset into the padded coordinate space using each row's own delta.
	TestEqual(TEXT("Row-0 SourceOffset shifted by its own paste delta"),
		Profile->Flipbooks[0].CombatData.FrameExtractionInfo[0].SourceOffset, FIntPoint(4, 4));
	TestEqual(TEXT("Row-1 SourceOffset shifted by its own paste delta"),
		Profile->Flipbooks[0].CombatData.FrameExtractionInfo[1].SourceOffset, FIntPoint(4, 12));

	return true;
}


// =============================================================================
// TEST 14b: ODD source cells — every cell lands on its OWN destination cell.
// =============================================================================
// Recovering the new region's origin from the sprite's content rect makes the
// result depend on integer PARITY: the pad places a cell at
//     CellOffset = D/2 - S/2            (two independent floors)
// while an anchor recovers it as
//     paddedMin - (D - S)/2             (one floor)
// and those disagree by exactly 1px when D is even and S is odd. With 33x33
// source cells padded to 40x40 the anchored origin is Col*40 + 1 — one pixel
// right of the cell — for every column except the last, which the texture clamp
// silently rescues. That produced a hitbox delta of 3 on frames 0-2 and 4 on
// frame 3 of the SAME flipbook. Resolving the destination cell from the grid
// removes the dependence entirely: every cell shares one delta.
//
// Every other fixture in this file uses even cell sizes, so nothing else can
// catch this.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusApplyCrossSheetOddCellPlacement,
	"Paper2DPlus.CrossSheetAlignment.Apply.OddSourceCellPlacement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusApplyCrossSheetOddCellPlacement::RunTest(const FString& Parameters)
{
	constexpr int32 SrcCell = 33;   // ODD — the whole point of this fixture
	constexpr int32 DstCell = 40;   // EVEN
	constexpr int32 Cols = 4;
	constexpr int32 Rows = 2;
	constexpr int32 Growth = DstCell - SrcCell;   // 7
	constexpr int32 InCellOffset = DstCell / 2 - SrcCell / 2;   // 20 - 16 = 4

	// Already-padded sheet: 4 columns x 2 rows of 40x40 destination cells.
	UTexture2D* Texture = NewObject<UTexture2D>(GetTransientPackage(), NAME_None, RF_Transient);
	Texture->Source.Init(Cols * DstCell, Rows * DstCell, 1, 1, TSF_BGRA8);

	UPaper2DPlusCharacterProfileAsset* Profile =
		NewObject<UPaper2DPlusCharacterProfileAsset>(GetTransientPackage());
	UPaperFlipbook* Flipbook = NewObject<UPaperFlipbook>(Profile);

	TArray<UPaperSprite*> Sprites;
	Sprites.Reserve(Cols * Rows);
	for (int32 Row = 0; Row < Rows; ++Row)
	{
		for (int32 Col = 0; Col < Cols; ++Col)
		{
			UPaperSprite* Sprite = NewObject<UPaperSprite>(Profile);
			FSpriteAssetInitParameters Init;
			Init.Texture = Texture;
			// Pre-pad source rect: the full ODD source cell.
			Init.Offset = FIntPoint(Col * SrcCell, Row * SrcCell);
			Init.Dimension = FIntPoint(SrcCell, SrcCell);
			Sprite->InitializeSprite(Init);
			Sprite->SetPivotMode(ESpritePivotMode::Center_Center, FVector2D::ZeroVector, true);
			Sprites.Add(Sprite);
		}
	}
	{
		FScopedFlipbookMutator Mutator(Flipbook);
		for (UPaperSprite* Sprite : Sprites)
		{
			FPaperFlipbookKeyFrame Frame;
			Frame.Sprite = Sprite;
			Frame.FrameRun = 1;
			Mutator.KeyFrames.Add(Frame);
		}
	}

	FFlipbookProfileEntry Entry;
	Entry.Identity.FlipbookName = TEXT("OddCells");
	Entry.Identity.Flipbook = Flipbook;
	Entry.CombatData.Frames.SetNum(Sprites.Num());
	Entry.CombatData.FrameExtractionInfo.SetNum(Sprites.Num());
	for (int32 Index = 0; Index < Sprites.Num(); ++Index)
	{
		FHitboxData Hitbox;
		Hitbox.X = 10;
		Hitbox.Y = 10;
		Entry.CombatData.Frames[Index].Hitboxes.Add(Hitbox);
	}
	Profile->Flipbooks.Add(MoveTemp(Entry));

	// Row-major per-cell paste deltas exactly as PadTextureInPlace produces them for 33 -> 40.
	TArray<FIntPoint> CellDeltas;
	for (int32 Row = 0; Row < Rows; ++Row)
	{
		for (int32 Col = 0; Col < Cols; ++Col)
		{
			CellDeltas.Add(FIntPoint(Col * Growth + InCellOffset, Row * Growth + InCellOffset));
		}
	}
	TMap<UTexture2D*, TArray<FIntPoint>> Placements;
	Placements.Add(Texture, CellDeltas);
	TMap<UTexture2D*, FIntPoint> OriginalCellSizes;
	OriginalCellSizes.Add(Texture, FIntPoint(SrcCell, SrcCell));

	const FCrossSheetAlignmentStats Stats = FSpriteExtractionUtils::ApplyCrossSheetAlignment(
		{ Profile }, FIntPoint(DstCell, DstCell), OriginalCellSizes, Placements,
		TMap<UPaper2DPlusCharacterProfileAsset*, int32>());

	TestEqual(TEXT("Every sprite migrates"), Stats.SpritesUpdated, Sprites.Num());
	TestEqual(TEXT("No sprite was skipped as unresolvable"), Stats.SpritesSkipped, 0);

	for (int32 Index = 0; Index < Sprites.Num(); ++Index)
	{
		const int32 Row = Index / Cols;
		const int32 Col = Index % Cols;

		// THE RATCHET: a content-derived anchor gives Col*40 + 1 on every column but the last.
		TestEqual(*FString::Printf(TEXT("Cell %d lands exactly on its destination cell origin"), Index),
			FIntPoint(FMath::RoundToInt(Sprites[Index]->GetSourceUV().X),
				FMath::RoundToInt(Sprites[Index]->GetSourceUV().Y)),
			FIntPoint(Col * DstCell, Row * DstCell));
		TestEqual(*FString::Printf(TEXT("Cell %d takes the full uniform cell size"), Index),
			FIntPoint(FMath::RoundToInt(Sprites[Index]->GetSourceSize().X),
				FMath::RoundToInt(Sprites[Index]->GetSourceSize().Y)),
			FIntPoint(DstCell, DstCell));

		// ONE shared HitboxDelta for the whole flipbook — that is what keeps frames aligned to
		// each other. The parity bug made the last column differ from every other frame.
		TestEqual(*FString::Printf(TEXT("Cell %d hitbox uses the ONE shared in-cell delta"), Index),
			FIntPoint(Profile->Flipbooks[0].CombatData.Frames[Index].Hitboxes[0].X,
				Profile->Flipbooks[0].CombatData.Frames[Index].Hitboxes[0].Y),
			FIntPoint(10 + InCellOffset, 10 + InCellOffset));
		TestEqual(*FString::Printf(TEXT("Cell %d SourceOffset shifted by its own paste delta"), Index),
			Profile->Flipbooks[0].CombatData.FrameExtractionInfo[Index].SourceOffset,
			CellDeltas[Index]);
	}

	return true;
}


// =============================================================================
// TEST 14c: a TRIMMED (tight) source rect keeps its position inside the cell.
// =============================================================================
// ApplyCrossSheetAlignment runs over every Profile referencing the texture, not
// only rows the bulk extractor created — including sprites the single-sheet
// extractor cut with island detection, whose rect is TIGHT rather than the full
// source cell. Anchoring such a rect by centring it re-centres every frame on
// its OWN content: two frames whose art sits at different heights inside their
// cells come out at the same in-region position, which deletes exactly the
// frame-to-frame motion the midpoint pad exists to preserve, and reproduces the
// box-centre pivot measured to differ on 96% of character sheets.
//
// Expanding to the destination CELL instead keeps each frame's content exactly
// where the pad put it, so the two frames' deltas differ by their real motion.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusApplyCrossSheetTrimmedRectPlacement,
	"Paper2DPlus.CrossSheetAlignment.Apply.TrimmedRectKeepsMotion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusApplyCrossSheetTrimmedRectPlacement::RunTest(const FString& Parameters)
{
	constexpr int32 SrcCell = 32;
	constexpr int32 DstCell = 40;

	// Already-padded sheet: 2 columns x 1 row of 40x40 destination cells.
	UTexture2D* Texture = NewObject<UTexture2D>(GetTransientPackage(), NAME_None, RF_Transient);
	Texture->Source.Init(2 * DstCell, DstCell, 1, 1, TSF_BGRA8);

	UPaper2DPlusCharacterProfileAsset* Profile =
		NewObject<UPaper2DPlusCharacterProfileAsset>(GetTransientPackage());
	UPaperFlipbook* Flipbook = NewObject<UPaperFlipbook>(Profile);

	// TIGHT pre-pad rects, deliberately at DIFFERENT heights inside their own source cells:
	//   cell 0 -> content 20x20 at y 2 ; cell 1 -> content 20x20 at y 10 (an 8px rise).
	const FIntRect TightRects[2] = { FIntRect(6, 2, 26, 22), FIntRect(38, 10, 58, 30) };
	UPaperSprite* Sprites[2] = {};
	for (int32 Index = 0; Index < 2; ++Index)
	{
		Sprites[Index] = NewObject<UPaperSprite>(Profile);
		FSpriteAssetInitParameters Init;
		Init.Texture = Texture;
		Init.Offset = TightRects[Index].Min;
		Init.Dimension = FIntPoint(TightRects[Index].Width(), TightRects[Index].Height());
		Sprites[Index]->InitializeSprite(Init);
		Sprites[Index]->SetPivotMode(ESpritePivotMode::Center_Center, FVector2D::ZeroVector, true);
	}
	{
		FScopedFlipbookMutator Mutator(Flipbook);
		for (int32 Index = 0; Index < 2; ++Index)
		{
			FPaperFlipbookKeyFrame Frame;
			Frame.Sprite = Sprites[Index];
			Frame.FrameRun = 1;
			Mutator.KeyFrames.Add(Frame);
		}
	}

	FFlipbookProfileEntry Entry;
	Entry.Identity.FlipbookName = TEXT("TrimmedRects");
	Entry.Identity.Flipbook = Flipbook;
	Entry.CombatData.Frames.SetNum(2);
	Entry.CombatData.FrameExtractionInfo.SetNum(2);
	for (int32 Index = 0; Index < 2; ++Index)
	{
		FHitboxData Hitbox;
		Hitbox.X = 0;
		Hitbox.Y = 0;
		Entry.CombatData.Frames[Index].Hitboxes.Add(Hitbox);
	}
	Profile->Flipbooks.Add(MoveTemp(Entry));

	// CellOffset = 40/2 - 32/2 = 4; column 1 also carries the preceding column's 8px growth.
	TMap<UTexture2D*, TArray<FIntPoint>> Placements;
	Placements.Add(Texture, { FIntPoint(4, 4), FIntPoint(12, 4) });
	TMap<UTexture2D*, FIntPoint> OriginalCellSizes;
	OriginalCellSizes.Add(Texture, FIntPoint(SrcCell, SrcCell));

	const FCrossSheetAlignmentStats Stats = FSpriteExtractionUtils::ApplyCrossSheetAlignment(
		{ Profile }, FIntPoint(DstCell, DstCell), OriginalCellSizes, Placements,
		TMap<UPaper2DPlusCharacterProfileAsset*, int32>());

	TestEqual(TEXT("Both trimmed sprites migrate"), Stats.SpritesUpdated, 2);
	TestEqual(TEXT("Neither trimmed sprite was skipped"), Stats.SpritesSkipped, 0);

	// Each region is its own destination cell — NOT a box centred on the tight content.
	TestEqual(TEXT("Trimmed cell 0 region is destination cell 0"),
		FIntPoint(FMath::RoundToInt(Sprites[0]->GetSourceUV().X),
			FMath::RoundToInt(Sprites[0]->GetSourceUV().Y)),
		FIntPoint(0, 0));
	TestEqual(TEXT("Trimmed cell 1 region is destination cell 1"),
		FIntPoint(FMath::RoundToInt(Sprites[1]->GetSourceUV().X),
			FMath::RoundToInt(Sprites[1]->GetSourceUV().Y)),
		FIntPoint(DstCell, 0));

	// The hitbox deltas ARE the in-cell content positions, so their difference is the real motion.
	// Centring on the content would make both frames' deltas identical (motion deleted).
	const int32 Delta0Y = Profile->Flipbooks[0].CombatData.Frames[0].Hitboxes[0].Y;
	const int32 Delta1Y = Profile->Flipbooks[0].CombatData.Frames[1].Hitboxes[0].Y;
	TestEqual(TEXT("Trimmed cell 0 keeps its own in-cell content height"), Delta0Y, 2 + 4);
	TestEqual(TEXT("Trimmed cell 1 keeps its own in-cell content height"), Delta1Y, 10 + 4);
	TestEqual(TEXT("THE RATCHET: the 8px rise between the two frames survives"),
		Delta1Y - Delta0Y, 8);

	return true;
}


// =============================================================================
// TESTS 15-20: FSpriteExtractionUtils::DetectFrameGrid — the gutter rule.
// =============================================================================
// A candidate cell width is valid iff every INTERNAL boundary column is empty
// (the gutter) AND blank cells appear only as a capped TRAILING run. The search
// runs most-frames-first, so the finest valid grid wins. Rows split ONLY on an
// explicit filename hint.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDetectFrameGridGutterFinest,
	"Paper2DPlus.CrossSheetAlignment.DetectFrameGrid.GutterFinestGrid",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDetectFrameGridGutterFinest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusCrossSheetAlignment;

	// 256x64: four 64-wide frames, content inset 8px so columns 64/128/192 are empty gutters.
	// 128 is ALSO a valid gutter width (column 128 is empty, both halves hold content), so this
	// fixture genuinely tests that the FINEST valid grid wins rather than the first one found.
	UTexture2D* Tex = PadGrid_MakeTexture(256, 64,
		PadGrid_MakeRowRects(64, { 0, 1, 2, 3 }, FIntRect(8, 8, 56, 56)));
	TestNotNull(TEXT("Fixture texture created"), Tex);
	if (!Tex) return false;

	const FDetectedFrameGrid Grid = FSpriteExtractionUtils::DetectFrameGrid(Tex, FIntPoint::ZeroValue);
	TestTrue(TEXT("Detection produced a valid grid"), Grid.IsValid());
	PadGrid_ExpectGrid(*this, Grid, FIntPoint(64, 64), FIntPoint(4, 1), TEXT("Four-frame gutter sheet"));
	TestEqual(TEXT("Decided by the gutter rule"), Grid.Source, FString(TEXT("gutter")));
	TestTrue(TEXT("A gutter answer is confident"), Grid.bConfident);
	TestEqual(TEXT("No blank frames"), Grid.BlankFrames.Num(), 0);
	TestEqual(TEXT("No trailing padding"), Grid.TrailingBlanks, 0);
	TestNotEqual(TEXT("The coarser 128-wide grid (also gutter-valid) did NOT win"), Grid.Cell.X, 128);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDetectFrameGridTrailingBlanks,
	"Paper2DPlus.CrossSheetAlignment.DetectFrameGrid.TrailingBlankFrames",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDetectFrameGridTrailingBlanks::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusCrossSheetAlignment;

	// The real bug this pins: a VFX strip padded with one trailing blank frame so its length matches
	// the animation it accompanies. Rejecting the blank forced the search to a coarser grid that
	// MERGED real frames in pairs (a 1280x96 sheet detected as 5 frames of 256 instead of 10 of 128).
	// Same shape at half scale: 640x48, ten 64-wide frames, the last one blank.
	UTexture2D* Tex = PadGrid_MakeTexture(640, 48,
		PadGrid_MakeRowRects(64, { 0, 1, 2, 3, 4, 5, 6, 7, 8 }, FIntRect(8, 8, 56, 40)));
	TestNotNull(TEXT("Fixture texture created"), Tex);
	if (!Tex) return false;

	const FDetectedFrameGrid Grid = FSpriteExtractionUtils::DetectFrameGrid(Tex, FIntPoint::ZeroValue);
	TestTrue(TEXT("Detection produced a valid grid"), Grid.IsValid());
	PadGrid_ExpectGrid(*this, Grid, FIntPoint(64, 48), FIntPoint(10, 1), TEXT("Trailing-blank VFX strip"));
	TestNotEqual(TEXT("Did NOT fall back to a coarser grid that merges real frames"), Grid.Cell.X, 128);
	TestEqual(TEXT("Decided by the gutter rule"), Grid.Source, FString(TEXT("gutter")));
	TestTrue(TEXT("Still a confident answer"), Grid.bConfident);
	TestEqual(TEXT("The trailing blank is KEPT as a frame"), Grid.Grid.X, 10);
	TestEqual(TEXT("One blank frame reported"), Grid.BlankFrames.Num(), 1);
	if (Grid.BlankFrames.Num() == 1)
	{
		TestEqual(TEXT("The blank is the LAST frame"), Grid.BlankFrames[0], 9);
	}
	TestEqual(TEXT("Reported as legitimate trailing padding"), Grid.TrailingBlanks, 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDetectFrameGridInteriorBlanks,
	"Paper2DPlus.CrossSheetAlignment.DetectFrameGrid.InteriorBlanksDisqualify",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDetectFrameGridInteriorBlanks::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusCrossSheetAlignment;

	// 256x64 with cells 0, 1 and 3 occupied — the blank sits in the MIDDLE. Width 64 has a perfect
	// gutter, but an interior blank means the grid is too fine (a real animation does not skip a
	// frame), so it is disqualified and the coarser 128 wins.
	UTexture2D* Tex = PadGrid_MakeTexture(256, 64,
		PadGrid_MakeRowRects(64, { 0, 1, 3 }, FIntRect(8, 8, 56, 56)));
	TestNotNull(TEXT("Fixture texture created"), Tex);
	if (!Tex) return false;

	const FDetectedFrameGrid Grid = FSpriteExtractionUtils::DetectFrameGrid(Tex, FIntPoint::ZeroValue);
	TestTrue(TEXT("Detection produced a valid grid"), Grid.IsValid());
	PadGrid_ExpectGrid(*this, Grid, FIntPoint(128, 64), FIntPoint(2, 1), TEXT("Interior-blank sheet"));
	TestNotEqual(TEXT("The gutter-valid 64-wide grid was disqualified by its INTERIOR blank"),
		Grid.Cell.X, 64);
	TestEqual(TEXT("Still decided by the gutter rule, one step coarser"), Grid.Source, FString(TEXT("gutter")));
	TestEqual(TEXT("No blank cells remain at the accepted width"), Grid.BlankFrames.Num(), 0);
	TestEqual(TEXT("An interior blank is never reported as trailing padding"), Grid.TrailingBlanks, 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDetectFrameGridTrailingBlankCap,
	"Paper2DPlus.CrossSheetAlignment.DetectFrameGrid.TrailingBlankCap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDetectFrameGridTrailingBlankCap::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusCrossSheetAlignment;

	// A / B on the SAME sheet size and the SAME gutter, differing only in how many trailing cells are
	// blank — so the cap, and nothing else, is what discriminates.

	// A: ONE sprite on the left of an otherwise empty 256-wide sheet. Width 64 has a clean gutter but
	//    would fragment it into 4 frames, 3 of them blank (cap = max(1, 4/4) = 1). Rejected.
	{
		UTexture2D* Tex = PadGrid_MakeTexture(256, 64,
			PadGrid_MakeRowRects(64, { 0 }, FIntRect(8, 8, 56, 56)));
		TestNotNull(TEXT("Sparse fixture created"), Tex);
		if (!Tex) return false;

		const FDetectedFrameGrid Grid = FSpriteExtractionUtils::DetectFrameGrid(Tex, FIntPoint::ZeroValue);
		PadGrid_ExpectGrid(*this, Grid, FIntPoint(128, 64), FIntPoint(2, 1), TEXT("Single sprite, mostly empty sheet"));
		TestNotEqual(TEXT("The cap stopped over-fragmentation into four cells"), Grid.Cell.X, 64);
		TestEqual(TEXT("One trailing blank survives at the accepted width"), Grid.TrailingBlanks, 1);
	}

	// B: same sheet, but cells 0-2 hold content so width 64 leaves only ONE trailing blank
	//    (1 <= cap of 1). Now the fine grid is accepted — proving A was rejected by the cap.
	{
		UTexture2D* Tex = PadGrid_MakeTexture(256, 64,
			PadGrid_MakeRowRects(64, { 0, 1, 2 }, FIntRect(8, 8, 56, 56)));
		TestNotNull(TEXT("Padded-animation fixture created"), Tex);
		if (!Tex) return false;

		const FDetectedFrameGrid Grid = FSpriteExtractionUtils::DetectFrameGrid(Tex, FIntPoint::ZeroValue);
		PadGrid_ExpectGrid(*this, Grid, FIntPoint(64, 64), FIntPoint(4, 1), TEXT("Three frames plus one trailing blank"));
		TestEqual(TEXT("Within the cap, the fine grid IS accepted"), Grid.Cell.X, 64);
		TestEqual(TEXT("The trailing blank is kept as a frame"), Grid.TrailingBlanks, 1);
		if (Grid.BlankFrames.Num() == 1)
		{
			TestEqual(TEXT("The blank is the last frame"), Grid.BlankFrames[0], 3);
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDetectFrameGridFilenameHint,
	"Paper2DPlus.CrossSheetAlignment.DetectFrameGrid.FilenameHintWins",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDetectFrameGridFilenameHint::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusCrossSheetAlignment;

	// The same four-frame gutter sheet the finest-grid test uses: raw search answers 64.
	UTexture2D* Tex = PadGrid_MakeTexture(256, 64,
		PadGrid_MakeRowRects(64, { 0, 1, 2, 3 }, FIntRect(8, 8, 56, 56)));
	TestNotNull(TEXT("Fixture texture created"), Tex);
	if (!Tex) return false;

	// Baseline: no hint → the finest gutter-valid grid.
	{
		const FDetectedFrameGrid Grid = FSpriteExtractionUtils::DetectFrameGrid(Tex, FIntPoint::ZeroValue);
		PadGrid_ExpectGrid(*this, Grid, FIntPoint(64, 64), FIntPoint(4, 1), TEXT("No hint"));
	}

	// The artist's label wins whenever the pixels do not contradict it — 128 is gutter-valid here.
	{
		const FDetectedFrameGrid Grid = FSpriteExtractionUtils::DetectFrameGrid(Tex, FIntPoint(128, 64));
		PadGrid_ExpectGrid(*this, Grid, FIntPoint(128, 64), FIntPoint(2, 1), TEXT("Hint 128x64"));
		TestEqual(TEXT("Decided by the hint, over the finer gutter answer"), Grid.Source, FString(TEXT("hint")));
		TestTrue(TEXT("A hint answer is confident"), Grid.bConfident);
	}

	// hint == sheet width is the SINGLE-FRAME case: there is no internal boundary to test, so it is
	// vacuously consistent and must stay selectable even though a gutter exists at 64.
	{
		const FDetectedFrameGrid Grid = FSpriteExtractionUtils::DetectFrameGrid(Tex, FIntPoint(256, 64));
		PadGrid_ExpectGrid(*this, Grid, FIntPoint(256, 64), FIntPoint(1, 1), TEXT("Hint == sheet width"));
		TestEqual(TEXT("Single frame"), Grid.Grid.X, 1);
		TestEqual(TEXT("Decided by the hint"), Grid.Source, FString(TEXT("hint")));
	}

	// A hint the PIXELS contradict is ignored: 32 would cut straight through the artwork.
	{
		const FDetectedFrameGrid Grid = FSpriteExtractionUtils::DetectFrameGrid(Tex, FIntPoint(32, 64));
		PadGrid_ExpectGrid(*this, Grid, FIntPoint(64, 64), FIntPoint(4, 1), TEXT("Contradicted hint 32x64"));
		TestEqual(TEXT("Falls back to the gutter rule rather than cutting through art"),
			Grid.Source, FString(TEXT("gutter")));
	}

	// ---------------------------------------------------------------------
	// Where the hint comes from. ParseFrameSizeHint is deliberately conservative:
	// both dimensions must be plausible frame sizes, because a bogus pair harvested
	// from an id ("Idle2x3") would otherwise reach the ROW rule, where a wrong split
	// silently halves every frame.
	// ---------------------------------------------------------------------
	TestEqual(TEXT("Asset-name hint"),
		FSpriteExtractionUtils::ParseFrameSizeHint(TEXT("/Game/Chars/Hero_192x128")), FIntPoint(192, 128));
	TestEqual(TEXT("Uppercase X and a file extension are tolerated"),
		FSpriteExtractionUtils::ParseFrameSizeHint(TEXT("Hero_192X128.png")), FIntPoint(192, 128));
	TestEqual(TEXT("Last match wins, so a per-sheet hint beats a folder-wide one"),
		FSpriteExtractionUtils::ParseFrameSizeHint(TEXT("/Game/Chars/192x128/Hero_64x64")), FIntPoint(64, 64));
	TestEqual(TEXT("An implausibly small pair is not a hint"),
		FSpriteExtractionUtils::ParseFrameSizeHint(TEXT("Idle2x3")), FIntPoint::ZeroValue);
	TestEqual(TEXT("An over-long digit run is not a hint"),
		FSpriteExtractionUtils::ParseFrameSizeHint(TEXT("Foo_123456x128")), FIntPoint::ZeroValue);
	TestEqual(TEXT("A name with no pair carries no hint"),
		FSpriteExtractionUtils::ParseFrameSizeHint(TEXT("AttackCombo")), FIntPoint::ZeroValue);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDetectFrameGridRowsRequireHint,
	"Paper2DPlus.CrossSheetAlignment.DetectFrameGrid.RowsRequireHint",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDetectFrameGridRowsRequireHint::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusCrossSheetAlignment;

	// 128x64: two 64-wide frames whose artwork has a HORIZONTAL GAP through it (rows 28-35 empty) —
	// exactly the shape that reads as a perfect 2-row gutter and is not one. A wrong row split
	// silently halves every frame, so the burden of proof sits on an explicit hint.
	TArray<FIntRect> Content;
	for (int32 K = 0; K < 2; ++K)
	{
		Content.Add(FIntRect(K * 64 + 8, 0, K * 64 + 56, 28));
		Content.Add(FIntRect(K * 64 + 8, 36, K * 64 + 56, 64));
	}
	UTexture2D* Tex = PadGrid_MakeTexture(128, 64, Content);
	TestNotNull(TEXT("Fixture texture created"), Tex);
	if (!Tex) return false;

	// No hint: the gap is NOT a row break.
	{
		const FDetectedFrameGrid Grid = FSpriteExtractionUtils::DetectFrameGrid(Tex, FIntPoint::ZeroValue);
		PadGrid_ExpectGrid(*this, Grid, FIntPoint(64, 64), FIntPoint(2, 1), TEXT("Horizontal gap, no hint"));
		TestEqual(TEXT("Rows do not split without an explicit hint"), Grid.Grid.Y, 1);
		TestEqual(TEXT("Cell height is the whole sheet"), Grid.Cell.Y, 64);
		TestEqual(TEXT("Horizontal axis still decided by the gutter"), Grid.Source, FString(TEXT("gutter")));
	}

	// With an explicit hint that the gap admits, rows DO split — the override is one hint away.
	{
		const FDetectedFrameGrid Grid = FSpriteExtractionUtils::DetectFrameGrid(Tex, FIntPoint(64, 32));
		PadGrid_ExpectGrid(*this, Grid, FIntPoint(64, 32), FIntPoint(2, 2), TEXT("Horizontal gap, hint 64x32"));
		TestEqual(TEXT("An explicit hint splits the rows"), Grid.Grid.Y, 2);
	}

	// Even an explicit hint is refused when the split would cut THROUGH artwork: 16-tall cells put a
	// boundary at row 16, inside the upper band.
	{
		const FDetectedFrameGrid Grid = FSpriteExtractionUtils::DetectFrameGrid(Tex, FIntPoint(64, 16));
		TestEqual(TEXT("A row hint that straddles content is refused"), Grid.Cell.Y, 64);
		TestEqual(TEXT("So the sheet stays single-row"), Grid.Grid.Y, 1);
	}

	return true;
}

#endif // WITH_EDITOR
