// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "CharacterSizingFit.h"

#include "Paper2DPlusLayerDraw.h"

namespace Paper2DPlusCharacterSizing
{
	FFitResult ComputeFit(const FFitInput& In)
	{
		FFitResult Result;

		// Fail closed. A fresh profile with no capsule configured lands here, and it is the normal
		// case rather than an edge case -- so it must produce a message, not a divide by zero or a
		// zero-scale transform that silently makes the character vanish.
		if (In.SilhouetteHeightPx <= 0.0f)
		{
			Result.Reason = TEXT("The reference pose has no measurable content, so there is nothing to fit.");
			return Result;
		}
		if (In.PixelsPerUnit <= 0.0f)
		{
			Result.Reason = TEXT("The reference sprite reports no pixels-per-unit.");
			return Result;
		}
		if (In.TargetHeightUU <= 0.0f)
		{
			Result.Reason = TEXT("No target height. Set a character class with a capsule, or type a target height.");
			return Result;
		}

		// Height at scale 1, through the MAGNITUDE conversion (the offset conversion would return a
		// negative height here). Scale.Y is left at 1: it is depth for a 2D sprite, and scaling it
		// would be meaningless rather than merely harmless.
		const FVector2D UnscaledSize = Paper2DPlusLayerDraw::PixelSizeToWorld(
			FVector2D(In.SilhouetteWidthPx, In.SilhouetteHeightPx),
			In.PixelsPerUnit,
			FVector::OneVector);

		const float UnscaledHeight = UnscaledSize.Y;
		if (UnscaledHeight <= 0.0f)
		{
			Result.Reason = TEXT("The reference pose converts to a zero world height.");
			return Result;
		}

		// UNIFORM, so the character is never stretched to hit the number.
		const float UniformScale = In.TargetHeightUU / UnscaledHeight;
		Result.Scale = FVector(UniformScale, 1.0f, UniformScale);

		// Place the pivot so the silhouette's FEET land on the ground line rather than centring the
		// sprite on it. The ground line sits at -TargetHeight/2 relative to the capsule centre, which
		// is the actor origin the profile's relative transform is measured from.
		const float GroundZ = -0.5f * In.TargetHeightUU;
		const float PivotAboveFeetUU = (In.PivotToSilhouetteBottomPx / In.PixelsPerUnit) * UniformScale;
		Result.Location = FVector(0.0f, 0.0f, GroundZ + PivotAboveFeetUU);

		Result.ResultingHeightUU = UnscaledHeight * UniformScale;
		Result.bValid = true;
		return Result;
	}

	float ComputeHeightUU(float SilhouetteHeightPx, float PixelsPerUnit, float UniformScale)
	{
		if (SilhouetteHeightPx <= 0.0f || PixelsPerUnit <= 0.0f)
		{
			return 0.0f;
		}
		const FVector2D Size = Paper2DPlusLayerDraw::PixelSizeToWorld(
			FVector2D(0.0f, SilhouetteHeightPx),
			PixelsPerUnit,
			FVector(UniformScale, 1.0f, UniformScale));
		return Size.Y;
	}

	float UnrealUnitsToMetres(float UnrealUnits)
	{
		// Unreal's world unit is a centimetre.
		return UnrealUnits * 0.01f;
	}

	FString FormatPixelSizeReadout(FIntPoint SizePx, float PixelsPerUnit)
	{
		if (SizePx.X <= 0 || SizePx.Y <= 0)
		{
			return FString();
		}

		// Magnitude conversion, never the offset one -- a size is not a position.
		const FVector2D World = Paper2DPlusLayerDraw::PixelSizeToWorld(
			FVector2D((float)SizePx.X, (float)SizePx.Y),
			PixelsPerUnit,
			FVector::OneVector);

		// The pixels-per-unit is STATED, not assumed. See the header.
		const float ClampedPPU = FMath::Max(PixelsPerUnit, 0.001f);
		return FString::Printf(
			TEXT("%dx%d px  ·  %.4gx%.4g uu  @ %.4g px/uu"),
			SizePx.X, SizePx.Y, World.X, World.Y, ClampedPPU);
	}
}
