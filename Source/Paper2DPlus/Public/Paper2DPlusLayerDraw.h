// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FCharacterLayer;
struct FFlipbookProfileEntry;

/**
 * Paper2DPlusLayerDraw — THE single offset-math home (layered-asset redesign U4, plan KTD "One
 * offset-math home"). Pure, worldless-testable statics — no component/world/Slate dependencies —
 * consumed by BOTH:
 *   - the runtime per-child recipe (UPaper2DPlusLayerRenderComponent::ApplyChildOffsetsForFrame:
 *     per-child LastApplied tracker, the CHILD's OWN GetComponentScale(), AddWorldOffset delta —
 *     never SetRelativeLocation; undo-on-flipbook-change), and
 *   - every editor composite surface (SLayerPreviewCanvas, SLayerCompositeThumbnail,
 *     FLayerThumbnailRenderer, the Overview/Hitbox frame-strip cells) — each shifts its draw by
 *     ResolveTotalOffsetPx * zoom through its existing paint transform.
 *
 * Pixel-space convention matches FSpriteExtractionInfo::SpriteOffset: +X right, +Y DOWN (screen).
 * The world conversion flips Y sign (screen-down -> world-up) per the STABILITY-CRITICAL recipe in
 * Plugins/Paper2DPlus/docs/solutions/ue-runtime-sprite-offset-architecture.md.
 */
namespace Paper2DPlusLayerDraw
{
	/**
	 * The layer's authored placement offset for an animation: the AnimationOffsets entry whose
	 * AnimationName matches (case-insensitive, first match wins) else DefaultOffsetPx.
	 */
	PAPER2DPLUS_API FVector2D ResolveLayerOffsetPx(const FCharacterLayer& Layer, const FString& AnimationName);

	/**
	 * The TOTAL pixel offset a composed sprite carries at a key frame:
	 *   base per-frame alignment (Entry->CombatData.FrameExtractionInfo[KeyFrame].SpriteOffset +
	 *   TrimOffset — the same pair the base component applies at runtime; null Entry or an
	 *   out-of-range KeyFrame contributes zero)
	 * + the layer's authored offset (ResolveLayerOffsetPx; null Layer contributes zero — the base
	 *   flipbook sprite itself passes null so the base pass gets alignment-only).
	 */
	PAPER2DPLUS_API FVector2D ResolveTotalOffsetPx(
		const FFlipbookProfileEntry* Entry,
		int32 KeyFrame,
		const FCharacterLayer* Layer,
		const FString& AnimationName);

	/**
	 * Pixel -> world conversion, mirroring the base component's recipe EXACTLY
	 * (Paper2DPlusCharacterProfileComponent.cpp sprite-offset block):
	 *   X = (OffsetPx.X / PPU) * |ComponentScale.X|, NEGATED when bFacingLeft (the single sign flip
	 *       — magnitude from |scale| so a yaw-flipped facing mirrors too, audit F4);
	 *   Y = 0 (depth untouched);
	 *   Z = (-OffsetPx.Y / PPU) * ComponentScale.Z (screen-down -> world-up sign flip; Z scale is
	 *       deliberately NOT abs'd — byte-parity with the base recipe).
	 * PixelsPerUnit is clamped >= 0.001 (division-by-zero guard, same clamp as the base).
	 */
	PAPER2DPLUS_API FVector PixelOffsetToWorld(
		const FVector2D& OffsetPx,
		float PixelsPerUnit,
		const FVector& ComponentScale,
		bool bFacingLeft);

	/**
	 * Pixel -> world for a SIZE, not an offset. Same home, deliberately different signs.
	 *
	 * PixelOffsetToWorld is correct for POSITIONS and wrong for MAGNITUDES: it negates X when facing
	 * left and computes Z as -OffsetPx.Y (the screen-down -> world-up flip). Feed it a pixel HEIGHT
	 * and it hands back a NEGATIVE world height; measure a left-facing preview and it mirrors the
	 * width. Character sizing, the height readout, and the extractor readout all convert extents
	 * rather than offsets, so they need absolute magnitude on both axes with no sign flips.
	 *
	 * This lives beside PixelOffsetToWorld on purpose. The rule that matters is "one home for
	 * pixel-to-world", not "one function": a correctly-signed sibling here is the fix, a private
	 * conversion in a widget somewhere is the thing that rule forbids.
	 *
	 * Returns (width, height) in world units. PixelsPerUnit is clamped >= 0.001, matching the offset
	 * conversion's division-by-zero guard, and the component scale contributes as |X| and |Z|.
	 */
	PAPER2DPLUS_API FVector2D PixelSizeToWorld(
		const FVector2D& SizePx,
		float PixelsPerUnit,
		const FVector& ComponentScale);
}
