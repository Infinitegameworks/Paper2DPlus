// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "CharacterLayerExactCompositor.h"

namespace Paper2DPlusCharacterLayerExactCompositorPrivate
{
	bool IsInsideTextureLimit(const FIntPoint& Size, int32 MaxTextureDimension)
	{
		return Size.X > 0 && Size.Y > 0
			&& Size.X <= MaxTextureDimension && Size.Y <= MaxTextureDimension;
	}

	FLinearColor DecodeSource(const FColor& Source, bool bSourcePixelsAreSRGB)
	{
		return bSourcePixelsAreSRGB
			? FLinearColor::FromSRGBColor(Source)
			: FLinearColor(
				Source.R / 255.0f,
				Source.G / 255.0f,
				Source.B / 255.0f,
				Source.A / 255.0f);
	}

	FLinearColor BlendOverLinear(
		const FColor& Source,
		const FLinearColor& Destination,
		EPaper2DPlusSpriteOpacityMode OpacityMode,
		float OpacityMaskClipValue,
		bool bSourcePixelsAreSRGB)
	{
		FLinearColor SourceLinear = DecodeSource(Source, bSourcePixelsAreSRGB);
		if (OpacityMode == EPaper2DPlusSpriteOpacityMode::Opaque)
		{
			SourceLinear.A = 1.0f;
			return SourceLinear;
		}
		if (OpacityMode == EPaper2DPlusSpriteOpacityMode::Masked)
		{
			if (SourceLinear.A < OpacityMaskClipValue)
			{
				return Destination;
			}
			SourceLinear.A = 1.0f;
			return SourceLinear;
		}
		if (SourceLinear.A <= 0.0f)
		{
			return Destination;
		}
		if (SourceLinear.A >= 1.0f)
		{
			SourceLinear.A = 1.0f;
			return SourceLinear;
		}

		const float SourceAlpha = SourceLinear.A;
		const float DestinationAlpha = Destination.A;
		const float OutputAlpha = SourceAlpha + DestinationAlpha * (1.0f - SourceAlpha);
		if (OutputAlpha <= 0.0f)
		{
			return FLinearColor::Transparent;
		}

		return FLinearColor(
			(SourceLinear.R * SourceAlpha
				+ Destination.R * DestinationAlpha * (1.0f - SourceAlpha)) / OutputAlpha,
			(SourceLinear.G * SourceAlpha
				+ Destination.G * DestinationAlpha * (1.0f - SourceAlpha)) / OutputAlpha,
			(SourceLinear.B * SourceAlpha
				+ Destination.B * DestinationAlpha * (1.0f - SourceAlpha)) / OutputAlpha,
			OutputAlpha);
	}
}

FColor CharacterLayerExactCompositor::BlendOver(
	const FColor& Source,
	const FColor& Destination,
	EPaper2DPlusSpriteOpacityMode OpacityMode,
	float OpacityMaskClipValue,
	bool bSourcePixelsAreSRGB)
{
	const FLinearColor DestinationLinear = FLinearColor::FromSRGBColor(Destination);
	return Paper2DPlusCharacterLayerExactCompositorPrivate::BlendOverLinear(
		Source,
		DestinationLinear,
		OpacityMode,
		OpacityMaskClipValue,
		bSourcePixelsAreSRGB).ToFColorSRGB();
}

bool CharacterLayerExactCompositor::Compose(
	const TArray<FCharacterLayerExactFrameInput>& InputFrames,
	int32 MaxTextureDimension,
	FCharacterLayerExactComposite& OutComposite)
{
	OutComposite = FCharacterLayerExactComposite();
	if (InputFrames.IsEmpty())
	{
		OutComposite.Errors.Add(TEXT("The animation has no key frames to compose."));
		return false;
	}
	if (MaxTextureDimension <= 0)
	{
		OutComposite.Errors.Add(TEXT("The maximum texture dimension must be positive."));
		return false;
	}

	int32 MinX = 0;
	int32 MinY = 0;
	int32 MaxX = 0;
	int32 MaxY = 0;
	bool bHasRegistrationGeometry = false;

	for (int32 FrameIndex = 0; FrameIndex < InputFrames.Num(); ++FrameIndex)
	{
		const FCharacterLayerExactFrameInput& Frame = InputFrames[FrameIndex];
		if (Frame.CanonicalSourceSize.X > 0 && Frame.CanonicalSourceSize.Y > 0)
		{
			MaxX = FMath::Max(MaxX, Frame.CanonicalSourceSize.X);
			MaxY = FMath::Max(MaxY, Frame.CanonicalSourceSize.Y);
			bHasRegistrationGeometry = true;
		}

		for (const FCharacterLayerExactImage& Image : Frame.Images)
		{
			if (!Image.IsValid())
			{
				OutComposite.Errors.Add(FString::Printf(
					TEXT("Frame %d source '%s' has invalid pixel dimensions."),
					FrameIndex,
					*Image.SourceLabel));
				continue;
			}
			if (Image.OpacityMode == EPaper2DPlusSpriteOpacityMode::Masked
				&& !FMath::IsFinite(Image.OpacityMaskClipValue))
			{
				OutComposite.Errors.Add(FString::Printf(
					TEXT("Frame %d source '%s' has a non-finite opacity-mask clip value."),
					FrameIndex,
					*Image.SourceLabel));
				continue;
			}
			if (!Paper2DPlusSpriteMaterialContract::CanRepresentFlattenedOpacity(
				Frame.OutputOpacityMode, Image.OpacityMode))
			{
				OutComposite.Errors.Add(FString::Printf(
					TEXT("Frame %d source '%s' uses %s opacity, which cannot be represented exactly by the canonical %s output material."),
					FrameIndex,
					*Image.SourceLabel,
					Paper2DPlusSpriteMaterialContract::LexToString(Image.OpacityMode),
					Paper2DPlusSpriteMaterialContract::LexToString(Frame.OutputOpacityMode)));
				continue;
			}
			const int64 ImageRight = static_cast<int64>(Image.TopLeftFromCanonical.X) + Image.Size.X;
			const int64 ImageBottom = static_cast<int64>(Image.TopLeftFromCanonical.Y) + Image.Size.Y;
			if (ImageRight > MAX_int32 || ImageRight < MIN_int32
				|| ImageBottom > MAX_int32 || ImageBottom < MIN_int32)
			{
				OutComposite.Errors.Add(FString::Printf(
					TEXT("Frame %d source '%s' placement is not representable."),
					FrameIndex,
					*Image.SourceLabel));
				continue;
			}

			MinX = FMath::Min(MinX, Image.TopLeftFromCanonical.X);
			MinY = FMath::Min(MinY, Image.TopLeftFromCanonical.Y);
			MaxX = FMath::Max(MaxX, static_cast<int32>(ImageRight));
			MaxY = FMath::Max(MaxY, static_cast<int32>(ImageBottom));
		}
	}

	if (!OutComposite.Errors.IsEmpty())
	{
		return false;
	}
	if (!bHasRegistrationGeometry)
	{
		OutComposite.Errors.Add(TEXT("No canonical registration geometry is available for this animation."));
		return false;
	}

	OutComposite.CanonicalUnion = FIntRect(MinX, MinY, MaxX, MaxY);
	const int64 CellWidth = static_cast<int64>(MaxX) - MinX;
	const int64 CellHeight = static_cast<int64>(MaxY) - MinY;
	if (CellWidth <= 0 || CellHeight <= 0 || CellWidth > MAX_int32 || CellHeight > MAX_int32)
	{
		OutComposite.Errors.Add(TEXT("The canonical union cell is empty or not representable."));
		return false;
	}
	OutComposite.CellSize = FIntPoint(static_cast<int32>(CellWidth), static_cast<int32>(CellHeight));

	const int32 MaxColumnsByWidth = MaxTextureDimension / OutComposite.CellSize.X;
	const int32 MaxRowsByHeight = MaxTextureDimension / OutComposite.CellSize.Y;
	if (MaxColumnsByWidth <= 0 || MaxRowsByHeight <= 0
		|| static_cast<int64>(MaxColumnsByWidth) * MaxRowsByHeight < InputFrames.Num())
	{
		OutComposite.Errors.Add(FString::Printf(
			TEXT("%d cells of %dx%d cannot fit within one %dpx texture."),
			InputFrames.Num(), OutComposite.CellSize.X, OutComposite.CellSize.Y, MaxTextureDimension));
		return false;
	}

	// Preserve the historical near-square frame grid whenever it fits. For strongly rectangular cells,
	// clamp it into the full set of valid column counts instead of rejecting a valid rectangular sheet.
	const int32 PreferredColumns = FMath::Max(
		1, FMath::CeilToInt(FMath::Sqrt(static_cast<double>(InputFrames.Num()))));
	const int32 MinimumColumns = FMath::DivideAndRoundUp(InputFrames.Num(), MaxRowsByHeight);
	const int32 MaximumColumns = FMath::Min(InputFrames.Num(), MaxColumnsByWidth);
	if (MinimumColumns > MaximumColumns)
	{
		OutComposite.Errors.Add(TEXT("No valid managed sprite-sheet grid satisfies the texture limit."));
		return false;
	}
	const int32 Columns = FMath::Clamp(PreferredColumns, MinimumColumns, MaximumColumns);
	const int32 Rows = FMath::DivideAndRoundUp(InputFrames.Num(), Columns);
	OutComposite.GridSize = FIntPoint(Columns, Rows);
	const int64 SheetWidth = static_cast<int64>(OutComposite.CellSize.X) * Columns;
	const int64 SheetHeight = static_cast<int64>(OutComposite.CellSize.Y) * Rows;
	if (SheetWidth > MAX_int32 || SheetHeight > MAX_int32)
	{
		OutComposite.Errors.Add(TEXT("Managed sprite sheet dimensions are not representable."));
		return false;
	}
	OutComposite.SheetSize = FIntPoint(static_cast<int32>(SheetWidth), static_cast<int32>(SheetHeight));

	if (!Paper2DPlusCharacterLayerExactCompositorPrivate::IsInsideTextureLimit(
		OutComposite.SheetSize, MaxTextureDimension))
	{
		OutComposite.Errors.Add(FString::Printf(
			TEXT("Managed sprite sheet %dx%d exceeds the %dpx texture limit."),
			OutComposite.SheetSize.X,
			OutComposite.SheetSize.Y,
			MaxTextureDimension));
		return false;
	}

	const int64 PixelCount = static_cast<int64>(OutComposite.SheetSize.X) * OutComposite.SheetSize.Y;
	if (PixelCount <= 0 || PixelCount > MAX_int32)
	{
		OutComposite.Errors.Add(TEXT("Managed sprite sheet pixel count is not representable."));
		return false;
	}

	OutComposite.SheetPixels.Init(FColor(0, 0, 0, 0), static_cast<int32>(PixelCount));
	OutComposite.Frames.SetNum(InputFrames.Num());

	for (int32 FrameIndex = 0; FrameIndex < InputFrames.Num(); ++FrameIndex)
	{
		const FCharacterLayerExactFrameInput& InputFrame = InputFrames[FrameIndex];
		FCharacterLayerExactFrameOutput& OutputFrame = OutComposite.Frames[FrameIndex];
		OutputFrame.CellIndex = FrameIndex;
		OutputFrame.bCanonicalKeyFrameHadSprite = InputFrame.bCanonicalKeyFrameHadSprite;
		OutputFrame.SheetOrigin = FIntPoint(
			(FrameIndex % Columns) * OutComposite.CellSize.X,
			(FrameIndex / Columns) * OutComposite.CellSize.Y);
		OutputFrame.ManagedPivotLocal = InputFrame.CanonicalPivotLocal - FVector2D(MinX, MinY);

		TArray<FLinearColor> LinearRow;
		LinearRow.SetNumUninitialized(OutComposite.CellSize.X);
		for (int32 LocalY = 0; LocalY < OutComposite.CellSize.Y; ++LocalY)
		{
			for (FLinearColor& Pixel : LinearRow)
			{
				Pixel = FLinearColor::Transparent;
			}

			for (const FCharacterLayerExactImage& Image : InputFrame.Images)
			{
				const FIntPoint LocalDestinationOrigin =
					Image.TopLeftFromCanonical - FIntPoint(MinX, MinY);
				const int32 SourceY = LocalY - LocalDestinationOrigin.Y;
				if (SourceY < 0 || SourceY >= Image.Size.Y)
				{
					continue;
				}

				for (int32 SourceX = 0; SourceX < Image.Size.X; ++SourceX)
				{
					const int32 LocalX = LocalDestinationOrigin.X + SourceX;
					const int32 SourcePixelIndex = SourceY * Image.Size.X + SourceX;
					if (!Image.IsCovered(SourcePixelIndex))
					{
						continue;
					}
					const FColor& SourcePixel = Image.Pixels[SourcePixelIndex];
					LinearRow[LocalX] =
						Paper2DPlusCharacterLayerExactCompositorPrivate::BlendOverLinear(
						SourcePixel,
						LinearRow[LocalX],
						Image.OpacityMode,
						Image.OpacityMaskClipValue,
						Image.bSourcePixelsAreSRGB);
				}
			}

			const int32 SheetY = OutputFrame.SheetOrigin.Y + LocalY;
			for (int32 LocalX = 0; LocalX < OutComposite.CellSize.X; ++LocalX)
			{
				const int32 SheetX = OutputFrame.SheetOrigin.X + LocalX;
				OutComposite.SheetPixels[SheetY * OutComposite.SheetSize.X + SheetX] =
					LinearRow[LocalX].ToFColorSRGB();
			}
		}

		const bool bRequireOpaqueCoverage =
			InputFrame.OutputOpacityMode == EPaper2DPlusSpriteOpacityMode::Opaque;
		bool bHasUncoveredPixel = false;
		for (int32 LocalY = 0;
			LocalY < OutComposite.CellSize.Y && (bRequireOpaqueCoverage || !OutputFrame.bHasVisiblePixels);
			++LocalY)
		{
			const int32 SheetY = OutputFrame.SheetOrigin.Y + LocalY;
			for (int32 LocalX = 0; LocalX < OutComposite.CellSize.X; ++LocalX)
			{
				const int32 SheetX = OutputFrame.SheetOrigin.X + LocalX;
				const uint8 Alpha = OutComposite.SheetPixels[
					SheetY * OutComposite.SheetSize.X + SheetX].A;
				OutputFrame.bHasVisiblePixels |= Alpha > 0;
				bHasUncoveredPixel |= bRequireOpaqueCoverage && Alpha < 255;
				if (!bRequireOpaqueCoverage && OutputFrame.bHasVisiblePixels)
				{
					break;
				}
			}
		}
		if (bRequireOpaqueCoverage && OutputFrame.bHasVisiblePixels && bHasUncoveredPixel)
		{
			OutComposite.Errors.Add(FString::Printf(
				TEXT("Frame %d has uncovered pixels that an Opaque canonical output material would render as a rectangle. Use Masked/Translucent output or keep this art live."),
				FrameIndex));
		}
	}

	if (!OutComposite.Errors.IsEmpty())
	{
		return false;
	}
	OutComposite.bSuccess = true;
	return true;
}
