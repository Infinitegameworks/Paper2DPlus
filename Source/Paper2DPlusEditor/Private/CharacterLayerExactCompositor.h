// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Paper2DPlusSpriteMaterialContract.h"

/** One readable source region in paint order, positioned relative to the canonical sprite's source top-left. */
struct FCharacterLayerExactImage
{
	TArray<FColor> Pixels;
	/** One byte per source texel, derived from the sprite's cooked render triangles. Empty means full coverage. */
	TArray<uint8> CoverageMask;
	FIntPoint Size = FIntPoint::ZeroValue;
	FIntPoint TopLeftFromCanonical = FIntPoint::ZeroValue;
	FString SourceLabel;
	EPaper2DPlusSpriteOpacityMode OpacityMode = EPaper2DPlusSpriteOpacityMode::Translucent;
	float OpacityMaskClipValue = 0.3333f;
	bool bSourcePixelsAreSRGB = true;

	bool IsValid() const
	{
		return Size.X > 0 && Size.Y > 0
			&& static_cast<int64>(Pixels.Num()) == static_cast<int64>(Size.X) * Size.Y
			&& (CoverageMask.IsEmpty() || CoverageMask.Num() == Pixels.Num());
	}

	bool IsCovered(int32 PixelIndex) const
	{
		return CoverageMask.IsEmpty() || (CoverageMask.IsValidIndex(PixelIndex) && CoverageMask[PixelIndex] != 0);
	}
};

/** All ordered layer art for one flipbook key frame plus its immutable registration geometry. */
struct FCharacterLayerExactFrameInput
{
	FIntPoint CanonicalSourceSize = FIntPoint::ZeroValue;
	FVector2D CanonicalPivotLocal = FVector2D::ZeroVector;
	bool bCanonicalKeyFrameHadSprite = false;
	EPaper2DPlusSpriteOpacityMode OutputOpacityMode = EPaper2DPlusSpriteOpacityMode::Translucent;
	TArray<FCharacterLayerExactImage> Images;
};

/** Exact managed-sprite recipe for one key frame. A transparent result intentionally has no sprite. */
struct FCharacterLayerExactFrameOutput
{
	bool bHasVisiblePixels = false;
	bool bCanonicalKeyFrameHadSprite = false;
	int32 CellIndex = INDEX_NONE;
	FIntPoint SheetOrigin = FIntPoint::ZeroValue;
	FVector2D ManagedPivotLocal = FVector2D::ZeroVector;
};

/** Deterministic one-texture grid plus per-key-frame sprite geometry. */
struct FCharacterLayerExactComposite
{
	bool bSuccess = false;
	FIntRect CanonicalUnion = FIntRect(0, 0, 0, 0);
	FIntPoint CellSize = FIntPoint::ZeroValue;
	FIntPoint GridSize = FIntPoint::ZeroValue;
	FIntPoint SheetSize = FIntPoint::ZeroValue;
	TArray<FColor> SheetPixels;
	TArray<FCharacterLayerExactFrameOutput> Frames;
	TArray<FString> Errors;
};

namespace CharacterLayerExactCompositor
{
	/**
	 * Composite one sRGB source texel over an sRGB destination. Translucent RGB is blended in linear
	 * light and encoded back to sRGB; Masked/Opaque sources retain their binary coverage semantics.
	 */
	FColor BlendOver(
		const FColor& Source,
		const FColor& Destination,
		EPaper2DPlusSpriteOpacityMode OpacityMode = EPaper2DPlusSpriteOpacityMode::Translucent,
		float OpacityMaskClipValue = 0.3333f,
		bool bSourcePixelsAreSRGB = true);

	/**
	 * Build one animation-wide canonical-local union cell and pack every key frame into a deterministic grid.
	 * Input image regions are never content-cropped: transparent padding and positive/negative placement remain
	 * part of the registered cell. The canonical origin is preserved by shifting each managed custom pivot by
	 * the same union minimum. Key-frame indexing is one cell per flipbook key frame; FrameRun duration is not
	 * expanded here.
	 */
	bool Compose(
		const TArray<FCharacterLayerExactFrameInput>& InputFrames,
		int32 MaxTextureDimension,
		FCharacterLayerExactComposite& OutComposite);
}
