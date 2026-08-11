// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UPaperSprite;

/** Dimension-preserving operations applied to every pixel in one flipbook frame. */
enum class EFlipbookPixelTransform : uint8
{
	FlipHorizontal,
	FlipVertical,
	Rotate180,
	ShiftLeft,
	ShiftRight,
	ShiftUp,
	ShiftDown,
	Clear
};

/**
 * FFlipbookPixelEdit — the pure, worldless pixel-editing core for the Flipbook Draw tool.
 *
 * No Slate, no UObjects beyond UPaperSprite. Everything here is unit-tested. The headline pieces:
 *  - ReadFrame  : sprite source sub-region -> editable FColor buffer (delegates to the proven reader).
 *  - WriteFrame : WINDOWED in-place write back into the sprite's [SourceUV, +SourceSize) source window
 *                 WITHOUT re-Init, so sibling frames packed on the same sheet are never disturbed.
 *  - StampDab / StampLine : brush stamping into a CPU FColor buffer (square brush, gap-free lines).
 *
 * FColor's in-memory byte order is B,G,R,A, which equals TSF_BGRA8 source byte order, so the read/write
 * paths memcpy directly (the round-trip is pinned by a test).
 */
struct PAPER2DPLUSEDITOR_API FFlipbookPixelEdit
{
	/** Read the sprite's source sub-region into a WxH FColor buffer. BGRA8 only; fail-closed otherwise. */
	static bool ReadFrame(UPaperSprite* Sprite, TArray<FColor>& OutPixels, int32& OutW, int32& OutH);

	/**
	 * Windowed in-place write: copies the WxH buffer into the sprite's source-texture window
	 * ([SourceUV, SourceUV+SourceSize)) WITHOUT calling Source.Init (which would reallocate/wipe the
	 * whole sheet). BGRA8 only. Runs UpdateResource() + MarkPackageDirty() once. Returns false on
	 * format / size / bounds mismatch (leaving the texture untouched).
	 *
	 * On success it also rebuilds the sprite's derived geometry (RebuildData) and dirties the SPRITE
	 * package. Tight / shrink-wrapped / diced geometry is computed FROM the source alpha and then
	 * serialized, and the engine never invalidates it when a texture changes — so a write that moves
	 * the art must re-derive it here or the frame renders and collides against its pre-edit bounds.
	 */
	static bool WriteFrame(UPaperSprite* Sprite, const TArray<FColor>& Pixels, int32 W, int32 H);

	/** An "inverted" rect used to accumulate a dirty region (Min=MAX, Max=MIN). */
	static FIntRect EmptyDirtyRect();
	/** True once the dirty rect has covered at least one pixel. */
	static bool IsDirtyValid(const FIntRect& R);
	/** Grow A to also cover B (component-wise min/max). */
	static void UnionDirty(FIntRect& A, const FIntRect& B);

	/**
	 * Stamp a square brush of side BrushSize centered at (CX,CY) with Color into Buf (clamped to WxH).
	 * Expands InOutDirty (half-open: Max is exclusive) to cover the touched pixels.
	 */
	static void StampDab(TArray<FColor>& Buf, int32 W, int32 H, int32 CX, int32 CY, int32 BrushSize,
		const FColor& Color, FIntRect& InOutDirty);

	/** Stamp dabs along the Bresenham line (X0,Y0)->(X1,Y1) so fast drags leave no gaps. */
	static void StampLine(TArray<FColor>& Buf, int32 W, int32 H, int32 X0, int32 Y0, int32 X1, int32 Y1,
		int32 BrushSize, const FColor& Color, FIntRect& InOutDirty);

	/** Like StampDab, but also stamps the X- and/or Y-mirrored position(s) within WxH (mirror drawing). */
	static void StampDabMirrored(TArray<FColor>& Buf, int32 W, int32 H, int32 CX, int32 CY, int32 BrushSize,
		const FColor& Color, bool bMirrorX, bool bMirrorY, FIntRect& InOutDirty);

	/** Like StampLine, but also stamps the X- and/or Y-mirrored line(s) within WxH (mirror drawing). */
	static void StampLineMirrored(TArray<FColor>& Buf, int32 W, int32 H, int32 X0, int32 Y0, int32 X1, int32 Y1,
		int32 BrushSize, const FColor& Color, bool bMirrorX, bool bMirrorY, FIntRect& InOutDirty);

	/** Stamp the four edges of the inclusive rectangle Start..End with the square brush. */
	static void StampRectangle(TArray<FColor>& Buf, int32 W, int32 H, const FIntPoint& Start, const FIntPoint& End,
		int32 BrushSize, const FColor& Color, FIntRect& InOutDirty);

	/** Like StampRectangle, with optional reflected copies for drawing-symmetry mode. */
	static void StampRectangleMirrored(TArray<FColor>& Buf, int32 W, int32 H, const FIntPoint& Start, const FIntPoint& End,
		int32 BrushSize, const FColor& Color, bool bMirrorX, bool bMirrorY, FIntRect& InOutDirty);

	/**
	 * 4-connected flood fill from (X,Y): replaces the contiguous run of the seed pixel's exact color with
	 * NewColor (tolerance 0), bounded to WxH. Expands InOutDirty. No-op if the seed already is NewColor.
	 */
	static void FloodFill(TArray<FColor>& Buf, int32 W, int32 H, int32 X, int32 Y, const FColor& NewColor,
		FIntRect& InOutDirty);

	/**
	 * Apply a whole-frame transform without changing W/H. Shifts discard pixels crossing an edge and
	 * reveal transparent pixels on the opposite edge. Returns true only when valid pixels changed.
	 */
	static bool TransformFrame(TArray<FColor>& Buf, int32 W, int32 H, EFlipbookPixelTransform Transform);
};
