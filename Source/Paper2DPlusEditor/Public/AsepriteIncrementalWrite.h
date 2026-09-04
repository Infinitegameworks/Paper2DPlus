// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UTexture2D;

/** Verdict for one generated asset's payload (TASK-192 U3). */
enum class EAseWriteVerdict : uint8
{
	/** The Asset Registry has no such package — write it and advertise it. */
	Create,
	/** The frame count or canvas dims moved every cell rect and UV — write regardless of stamps. */
	WriteGridChanged,
	/** The stamped inputs differ (or no stamp exists — absent stamps fail closed to write). */
	WriteInputsChanged,
	/** Force Full Reimport bypassed every content gate (recovery path). */
	WriteForced,
	/** The registry has the package and every stamped input matches — leave the payload alone. */
	SkipPayload
};

/** One gate decision: the verdict plus the reason string every log/audit line carries (R10). */
struct FAseWriteDecision
{
	EAseWriteVerdict Verdict = EAseWriteVerdict::Create;
	FString Reason;

	bool ShouldWrite() const { return Verdict != EAseWriteVerdict::SkipPayload; }
};

/** The stamped grid triple. Any disagreement — including an unstamped side — reads as changed. */
struct FAseStampedGrid
{
	int32 FrameCount = 0;
	int32 CanvasWidth = 0;
	int32 CanvasHeight = 0;

	bool IsStamped() const
	{
		return FrameCount > 0 && CanvasWidth > 0 && CanvasHeight > 0;
	}

	bool Matches(const FAseStampedGrid& Other) const
	{
		return IsStamped() && Other.IsStamped()
			&& FrameCount == Other.FrameCount
			&& CanvasWidth == Other.CanvasWidth
			&& CanvasHeight == Other.CanvasHeight;
	}

	FString ToString() const
	{
		return FString::Printf(TEXT("%d frames of %dx%d"), FrameCount, CanvasWidth, CanvasHeight);
	}
};

/**
 * TASK-192 U3: the ONE place that answers "should this generated asset's payload be written?", so
 * the narrowing units cannot disagree about the rules.
 *
 * - Existence comes from the Asset Registry, never from FindObject (KTD2): generated assets are not
 *   resident during a reimport, and a null FindObject means "not loaded", not "not there".
 * - Content comes from stamps carried by the Layer Profile's FAsepriteSourceContext (KTD1), which IS
 *   loaded. An absent or partial stamp always reads as "write", never as "skip".
 * - The grid short-circuits before any stamp comparison: a frame-count or canvas change moves every
 *   cell rect and UV, so nothing content-gated survives it.
 * - A forced import (U8) flows through the same seam and bypasses only the content gates, so every
 *   decision — forced included — still produces its audit line.
 *
 * Every decision is recorded into the active import cost report's audit list and skips are logged,
 * so no caller can skip silently (R10).
 */
class PAPER2DPLUSEDITOR_API FAsepriteIncrementalWrite
{
public:
	/** Registry-only existence for a long package name ("/Game/Path/Asset"). Never loads (KTD2). */
	static bool RegistryHasPackage(const FString& LongPackageName);

	/** The gate for any generated sheet texture (per-layer, paired normal, or composited). */
	static FAseWriteDecision ShouldWriteSheet(
		const FString& SheetPackageName,
		const FString& StoredContentHash,
		const FString& NewContentHash,
		const FAseStampedGrid& StampedGrid,
		const FAseStampedGrid& CurrentGrid,
		bool bForceFullReimport);

	/** The gate for a per-tag flipbook. Grid changes need no special case here: a range shift or a
	 *  frame insertion changes the structure hash of every affected tag by construction. */
	static FAseWriteDecision ShouldWriteFlipbook(
		const FString& FlipbookPackageName,
		const FString& StoredStructureHash,
		const FString& NewStructureHash,
		bool bForceFullReimport);

	/**
	 * First-stage sprite gate (KTD3): existence is per sprite and never rides a content verdict; the
	 * payload rides the owning sheet's verdict, because unchanged pixels cannot move a tight bound.
	 * A WriteInputsChanged result here means "candidate — load it and refine with
	 * ShouldWriteSpriteForDerivedBounds", not "unconditionally rewrite".
	 */
	static FAseWriteDecision ShouldWriteSpritePayload(
		const FString& SpritePackageName,
		const FAseWriteDecision& OwningSheetDecision,
		bool bSheetObjectRecreated,
		const FAseStampedGrid& StampedGrid,
		const FAseStampedGrid& CurrentGrid,
		bool bForceFullReimport);

	/**
	 * Second-stage sprite refinement for a pixel-changed layer: with the sprite loaded and the
	 * expected tight boxes derived from the buffer already in hand (KTD6 — never RebuildData() to
	 * measure), decide whether the serialized geometry actually moved. Both geometries must match
	 * to skip. Positions/sizes are the stored-shape form (center + size, texture space).
	 */
	static FAseWriteDecision ShouldWriteSpriteForDerivedBounds(
		const FString& SpritePackageName,
		const FVector2D& StoredRenderCenter, const FVector2D& StoredRenderSize,
		const FVector2D& DerivedRenderCenter, const FVector2D& DerivedRenderSize,
		const FVector2D& StoredCollisionCenter, const FVector2D& StoredCollisionSize,
		const FVector2D& DerivedCollisionCenter, const FVector2D& DerivedCollisionSize);

	/**
	 * The engine's FindTextureBoundingBox + CreatePolygonFromBoundingBox math reproduced over a cell
	 * buffer already in hand: occupied = alpha STRICTLY greater than clamp(int(Threshold*255),0,255);
	 * shrink top, then bottom, then left, then right, with the engine's exact degenerate stops (an
	 * all-empty cell converges on its bottom-right texel, size 1x1). Returns the stored-shape form —
	 * center and size in TEXTURE space, the cell's sheet offset applied.
	 */
	static void DeriveTightBoxForCell(
		const TArray<FColor>& CellPixels, int32 CellWidth, int32 CellHeight,
		const FIntPoint& CellOffsetInSheet, float AlphaThreshold,
		FVector2D& OutBoxCenter, FVector2D& OutBoxSize);

	/** The per-tag flipbook STRUCTURE hash: authored range + per-frame durations + keyframe sprite
	 *  names. The rename-pairing pixel hash is not an input and never will be. */
	static FString ComputeTagStructureHash(
		int32 FromFrame, int32 ToFrame,
		const TArray<int32>& DurationsMs,
		const TArray<FString>& KeyframeSpriteNames);

	/**
	 * KTD9 settings reconciliation for a RESIDENT sheet: compare-and-assign the pixel-art settings
	 * (or the normal-map set), returning true when anything differed. A true return means the DDC
	 * key changed too — the caller owns the follow-up UpdateResource and MarkPackageDirty. A
	 * skipped, non-resident sheet has nothing in memory to drift, so callers only reach this with
	 * an object in hand.
	 */
	static bool ReconcileSheetSettings(UTexture2D* Sheet, bool bIsNormalMap);
};
