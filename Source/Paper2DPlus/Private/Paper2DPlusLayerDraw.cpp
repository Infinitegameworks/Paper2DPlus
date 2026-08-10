// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "Paper2DPlusLayerDraw.h"
#include "Paper2DPlusCharacterLayerAsset.h"
#include "Paper2DPlusCharacterProfileAsset.h"

namespace Paper2DPlusLayerDraw
{

FVector2D ResolveLayerOffsetPx(const FCharacterLayer& Layer, const FString& AnimationName)
{
	// Per-anim override wins (case-insensitive, first match — mirrors FindAnimationMapping's identity rule);
	// otherwise the layer-wide default. An override entry is authoritative even when its value is zero (an
	// authored "no nudge for THIS animation" must beat a non-zero default).
	if (!AnimationName.IsEmpty())
	{
		for (const FCharacterLayerAnimationOffset& Override : Layer.AnimationOffsets)
		{
			if (Override.AnimationName.Equals(AnimationName, ESearchCase::IgnoreCase))
			{
				return Override.OffsetPx;
			}
		}
	}
	return Layer.DefaultOffsetPx;
}

FVector2D ResolveTotalOffsetPx(
	const FFlipbookProfileEntry* Entry,
	int32 KeyFrame,
	const FCharacterLayer* Layer,
	const FString& AnimationName)
{
	FVector2D TotalPx = FVector2D::ZeroVector;

	// Base per-frame alignment: SpriteOffset + TrimOffset — the SAME pair the base component's runtime
	// recipe combines (Paper2DPlusCharacterProfileComponent, "Sprite offset application"). Null entry /
	// out-of-range frame => zero contribution (fails soft, never asserts).
	if (Entry && Entry->CombatData.FrameExtractionInfo.IsValidIndex(KeyFrame))
	{
		const FSpriteExtractionInfo& Info = Entry->CombatData.FrameExtractionInfo[KeyFrame];
		const FIntPoint Combined = Info.SpriteOffset + Info.TrimOffset;
		TotalPx.X += Combined.X;
		TotalPx.Y += Combined.Y;
	}

	// The layer's authored placement (U4 move tool). Null layer (the base pass) contributes zero.
	if (Layer)
	{
		TotalPx += ResolveLayerOffsetPx(*Layer, AnimationName);
	}

	return TotalPx;
}

FVector PixelOffsetToWorld(
	const FVector2D& OffsetPx,
	float PixelsPerUnit,
	const FVector& ComponentScale,
	bool bFacingLeft)
{
	const float PPU = FMath::Max(PixelsPerUnit, 0.001f);

	// X: magnitude from |scale| with the single facing sign flip — a yaw-flipped facing (positive scale)
	// mirrors the offset too, and a negative-scale flip isn't double-negated (audit F4, base recipe parity).
	float OffsetX = (OffsetPx.X / PPU) * FMath::Abs(ComponentScale.X);
	if (bFacingLeft)
	{
		OffsetX = -OffsetX;
	}

	// Z: screen-down -> world-up sign flip; scale deliberately NOT abs'd (byte-parity with the base recipe).
	return FVector(OffsetX, 0.0f, (-OffsetPx.Y / PPU) * ComponentScale.Z);
}

FVector2D PixelSizeToWorld(
	const FVector2D& SizePx,
	float PixelsPerUnit,
	const FVector& ComponentScale)
{
	const float PPU = FMath::Max(PixelsPerUnit, 0.001f);

	// MAGNITUDES, so every sign is dropped: no facing flip on X and no screen-down flip on Y. A
	// height is a height whichever way the character faces. Scale contributes as |X| and |Z| for the
	// same reason -- a mirrored character is not a negatively tall one.
	return FVector2D(
		FMath::Abs(SizePx.X / PPU) * FMath::Abs(ComponentScale.X),
		FMath::Abs(SizePx.Y / PPU) * FMath::Abs(ComponentScale.Z));
}

} // namespace Paper2DPlusLayerDraw
