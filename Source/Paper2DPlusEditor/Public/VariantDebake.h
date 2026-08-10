// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * De-bake shared base (TASK-107) — the pure pixel core.
 *
 * Artists deliver N variant sheets of ONE animation with per-variant art baked in (bottle tints, potion
 * VFX) and no clean shared base. This recovers the base deterministically (no AI): per-pixel consensus
 * vote across the N sheets, optionally masked by per-variant VFX sheets (a sheet only votes where its
 * front VFX alpha is 0), algebraic alpha un-blend under semi-transparent VFX, and a fixed-reference-sheet
 * tie-break so the base never flickers across frames. Per variant it emits a residual overlay layer that,
 * composited front-VFX OVER (overlay OVER (base OVER back-VFX)), reconstructs the original sheet.
 *
 * Everything here is pure + deterministic + unit-tested on synthetic buffers (the segmentation
 * convention — this is its multi-variant sibling). The editor surface that reads UTexture2D sources,
 * previews the result, and materialises the outputs is the Bulk Sprite Extractor's de-bake groups
 * (SBulkSpriteExtractorWindow + FBulkDebakeUtils).
 */

/** One variant sheet plus its optional VFX contamination sheets. All buffers are row-major BGRA8. */
struct FDebakeSheetInput
{
	/** The baked variant sheet (Width x Height). All sheets in a run must share dimensions. */
	TArray<FColor> Pixels;
	int32 Width = 0;
	int32 Height = 0;

	/**
	 * Optional VFX sheets, laid out on the SAME cell size as the main sheet but usually with fewer
	 * cells (the VFX covers a sub-range of frames). Front renders in FRONT of the character, back
	 * renders BEHIND it. Empty array = none. Heights are implied (Num() / Width). Front and back
	 * share ONE start offset but may differ in cell count (the real CoolDown sandwich is back 11 /
	 * front 10 cells); samples past a sheet's own range are transparent.
	 */
	TArray<FColor> VfxFront;
	TArray<FColor> VfxBack;
	int32 VfxFrontWidth = 0;
	int32 VfxBackWidth = 0;
};

struct FDebakeSettings
{
	/** User-entered frame grid of the main sheets (manual by design — no autodetect reliance). */
	int32 Columns = 1;
	int32 Rows = 1;

	/** Tie-break owner: ambiguous pixels resolve from this sheet so the base stays frame-consistent. */
	int32 ReferenceSheetIndex = 0;

	/** Frame offset (in cells) of every VFX sheet within the mains; INDEX_NONE = auto-search per sheet. */
	int32 VfxOffsetOverride = INDEX_NONE;
};

struct FDebakeResult
{
	/** The recovered base sheet (same dimensions as the inputs). Unrecoverable pixels left transparent. */
	TArray<FColor> BasePixels;

	/** Per variant: sheet-sized residual overlay (transparent where the base already reconstructs it). */
	TArray<TArray<FColor>> Overlays;

	/** Per variant: number of non-transparent overlay pixels emitted. */
	TArray<int32> OverlayPixelCounts;

	/** Per variant: pixels where the full reconstruction still differs from the original (>8/channel). */
	TArray<int32> ResidualCounts;

	/** Per variant: pixels the variant LACKS but the base has (a normal layer cannot erase — reported only). */
	TArray<int32> EraseUnfixableCounts;

	/** Per variant: resolved VFX frame offset in cells (INDEX_NONE for sheets without VFX). */
	TArray<int32> VfxOffsets;

	/** Pixels covered by opaque front VFX in ALL sheets — left transparent in the base, reported here. */
	TArray<FIntPoint> UnrecoverablePixels;
};

class PAPER2DPLUSEDITOR_API FVariantDebake
{
public:
	/**
	 * Recover the shared base + per-variant overlays from N baked variant sheets.
	 * Hard errors (returns false + OutError, never silently mis-cuts): fewer than 2 sheets, mismatched
	 * sheet dimensions, dimensions not divisible by Columns/Rows, malformed VFX buffers (width not a
	 * multiple of the cell width, more VFX cells than main cells), reference index out of range.
	 * A sub-3-sheet vote is legal but weak — the CALLER warns, not us.
	 */
	static bool Run(const TArray<FDebakeSheetInput>& Sheets, const FDebakeSettings& Settings,
		FDebakeResult& Out, FText& OutError);

	/**
	 * Step-2 auto-align: the frame offset (in cells, 0-based) of Sheet's VFX within the main sheet that
	 * minimizes mismatched pixels of frontVfx OVER (ProvisionalBase OVER backVfx) vs the variant sheet.
	 * Searches every valid offset; first minimum wins (deterministic). Returns INDEX_NONE when the sheet
	 * has no VFX. Exposed for tests.
	 */
	static int32 FindBestVfxOffset(const FDebakeSheetInput& Sheet, const TArray<FColor>& ProvisionalBase,
		int32 CellW, int32 CellH, int32 Columns, int32 Rows);

	/** Standard straight-alpha "Front OVER Back" compositing with +0.5 rounding. Exposed for tests. */
	static FColor AlphaOver(const FColor& Front, const FColor& Back);

	/**
	 * Recover the color underneath a semi-transparent Vfx pixel, given the observed composited Main
	 * pixel. With f = VfxA/255, m = MainA/255: u = (m - f) / (1 - f); u <= 0.02 means nothing meaningful
	 * underneath (returns false, OutUnder = transparent). RGB solved from the OVER equation and clamped.
	 * Precondition: 0 < VfxA < 255 (returns false otherwise). Exposed for tests.
	 */
	static bool UnBlend(const FColor& Main, const FColor& Vfx, FColor& OutUnder);
};
