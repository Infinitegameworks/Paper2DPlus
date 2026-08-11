// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * The pure Character Sizing fit computation -- worldless, Slate-free, and the piece that is provably
 * right or wrong. The surface follows once this is pinned.
 *
 * Everything here is expressed against the REFERENCE POSE's measured silhouette, never against max
 * extents across every animation: one outstretched air-strike pose would shrink the idle, and the
 * idle is what the character actually looks like standing still.
 */
namespace Paper2DPlusCharacterSizing
{
	/** Everything the fit needs, already measured. No assets, no world, no Slate. */
	struct FFitInput
	{
		/** Measured content height of the reference pose, in source pixels. */
		float SilhouetteHeightPx = 0.0f;
		/** Measured content width, in source pixels. Carried for the readout, not the fit. */
		float SilhouetteWidthPx = 0.0f;
		/**
		 * Pixels from the sprite's pivot DOWN to the bottom of its silhouette (screen-down positive).
		 * This is what turns "how tall" into "where", so feet land on the ground line instead of the
		 * sprite being centred on it.
		 */
		float PivotToSilhouetteBottomPx = 0.0f;
		float PixelsPerUnit = 1.0f;
		/** Full target height in Unreal units -- capsule half-height doubled, or a typed height. */
		float TargetHeightUU = 0.0f;
	};

	struct FFitResult
	{
		bool bValid = false;
		/** Uniform, so the character is never stretched. */
		FVector Scale = FVector::OneVector;
		/** Relative location placing the pivot so the silhouette's feet sit on the ground line. */
		FVector Location = FVector::ZeroVector;
		/** The height the character ends up at, for the readout to confirm against the capsule. */
		float ResultingHeightUU = 0.0f;
		/** Why the fit was refused. Empty when valid. */
		FString Reason;
	};

	/**
	 * Computes a starting transform from the reference pose, its pixels-per-unit, and the capsule.
	 *
	 * Fails CLOSED rather than producing a degenerate transform: a zero or negative silhouette,
	 * pixels-per-unit, or target height returns bValid=false with a reason and an identity transform.
	 * The unconfigured-capsule case is the normal one on a fresh profile, so it must not divide by
	 * zero or silently fit to nothing.
	 */
	PAPER2DPLUSEDITOR_API FFitResult ComputeFit(const FFitInput& In);

	/**
	 * The character's height in Unreal units at a given uniform scale -- the live readout while
	 * dragging. Uses the magnitude conversion, never the offset one: a pixel height converted through
	 * the offset conversion comes back NEGATIVE.
	 */
	PAPER2DPLUSEDITOR_API float ComputeHeightUU(
		float SilhouetteHeightPx,
		float PixelsPerUnit,
		float UniformScale);

	/** Metres, for the second half of the readout. */
	PAPER2DPLUSEDITOR_API float UnrealUnitsToMetres(float UnrealUnits);

	/**
	 * The pixel-to-world readout, shared by the sizing tool and the sprite extractor so the
	 * relationship is stated the same way wherever a designer meets it: "64x64 px - 64x64 uu @ 1
	 * px/uu".
	 *
	 * It ALWAYS names the pixels-per-unit it used rather than assuming one. Extraction currently
	 * hard-codes pixels-per-unit to 1, so freshly extracted sprites really are one pixel to one
	 * unit -- but that is a property of this extractor, not of sprites in general, and a readout
	 * that silently assumed it would be quietly wrong for any sprite authored elsewhere.
	 *
	 * Purely descriptive: it reads dimensions and changes nothing.
	 */
	PAPER2DPLUSEDITOR_API FString FormatPixelSizeReadout(FIntPoint SizePx, float PixelsPerUnit);
}
