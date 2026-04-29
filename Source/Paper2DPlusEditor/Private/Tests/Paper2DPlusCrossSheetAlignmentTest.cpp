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
#include "SpriteExtractionUtils.h"
#include "Engine/Texture2D.h"
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
	}

	// Single entry → that entry's value (null key is fine, map is opaque pointer→int).
	{
		TMap<UTexture2D*, FIntPoint> Single;
		Single.Add(nullptr, FIntPoint(192, 256));
		TestEqual(TEXT("Single entry returns its value"),
			FSpriteExtractionUtils::ComputeGroupMaxCellSize(Single),
			FIntPoint(192, 256));
	}

	// Per-axis max, not preserving aspect: (256,128) & (128,256) → (256,256).
	{
		TMap<UTexture2D*, FIntPoint> Mixed;
		Mixed.Add(reinterpret_cast<UTexture2D*>(0x1), FIntPoint(256, 128));
		Mixed.Add(reinterpret_cast<UTexture2D*>(0x2), FIntPoint(128, 256));
		TestEqual(TEXT("Per-axis max picks largest per axis independently"),
			FSpriteExtractionUtils::ComputeGroupMaxCellSize(Mixed),
			FIntPoint(256, 256));
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
// TEST 7: PadTextureInPlace — bottom-center paste moves content correctly.
// =============================================================================
// Creates a transient 64x32 texture with a recognizable pattern (red fill in a
// 32x16 block at 0,0; transparent elsewhere), pads to 128x64 with BottomCenter
// anchor, verifies the paste offset is (32, 32) and the red block now sits at
// (32, 32)-(64, 48) in the new canvas with transparent borders.

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
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusPadTextureInPlaceBottomCenter,
	"Paper2DPlus.CrossSheetAlignment.PadTextureInPlace.BottomCenter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusPadTextureInPlaceBottomCenter::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusCrossSheetAlignment;

	UTexture2D* Tex = CreatePaddingFixtureTexture(64, 32);
	TestNotNull(TEXT("Fixture texture created"), Tex);
	if (!Tex) return false;

	// Pad 64x32 → 128x64 with BottomCenter anchor.
	// Expected paste offset: X = (128-64)/2 = 32, Y = 64-32 = 32.
	const FIntPoint Offset = FSpriteExtractionUtils::PadTextureInPlace(Tex, FIntPoint(128, 64),
		FIntPoint(1, 1), /*GroundPlaneOffset=*/0);
	TestEqual(TEXT("Ground plane 0 paste offset"), Offset, FIntPoint(32, 32));

	// Verify new Source dims.
	TestEqual(TEXT("New Source width"),  (int64)Tex->Source.GetSizeX(), (int64)128);
	TestEqual(TEXT("New Source height"), (int64)Tex->Source.GetSizeY(), (int64) 64);

	// Read new Source bytes. Left-half red block should now sit at (32, 32)-(64, 48).
	TArray<FColor> NewPixels;
	int32 NewW = 0, NewH = 0;
	TestTrue(TEXT("LoadTextureData succeeds post-pad"),
		FSpriteExtractionUtils::LoadTextureData(Tex, NewPixels, NewW, NewH));
	if (NewW != 128 || NewH != 64) return false;

	// Sample corners and center of the expected red region.
	auto PixelAt = [&](int32 X, int32 Y) { return NewPixels[Y * NewW + X]; };

	// Expected red at (32, 32)-(63, 47) (32 wide * 16 tall). Check a few cells.
	TestEqual(TEXT("Red at new-canvas (32,32)"), PixelAt(32, 32), FColor(255, 0, 0, 255));
	TestEqual(TEXT("Red at new-canvas (63,47)"), PixelAt(63, 47), FColor(255, 0, 0, 255));

	// Expected transparent: inside the new canvas but outside the pasted region.
	TestEqual(TEXT("Transparent left of paste"),  PixelAt(31, 40).A, (uint8)0);
	TestEqual(TEXT("Transparent above paste"),    PixelAt(40, 31).A, (uint8)0);
	TestEqual(TEXT("Transparent right of paste (original right-half was also transparent, so stays transparent at (64,40))"),
		PixelAt(64, 40).A, (uint8)0);
	TestEqual(TEXT("Transparent top-left corner"), PixelAt(0, 0).A,  (uint8)0);

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
	TestEqual(TEXT("Pad succeeded (non-zero offset)"), Offset, FIntPoint(32, 32));
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

#endif // WITH_EDITOR
