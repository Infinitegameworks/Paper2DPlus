// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "Paper2DPlusSpriteSourceUtils.h"

#include "Engine/Texture2D.h"
#include "PaperSprite.h"

bool Paper2DPlusSpriteSourceUtils::ReadSourceRegion(
	UPaperSprite* Sprite,
	TArray<FColor>& OutPixels,
	int32& OutWidth,
	int32& OutHeight)
{
	OutPixels.Reset();
	OutWidth = 0;
	OutHeight = 0;
	if (!Sprite)
	{
		return false;
	}

#if WITH_EDITOR
	UTexture2D* Texture = Cast<UTexture2D>(Sprite->GetSourceTexture());
	if (!Texture || Texture->Source.GetFormat() != TSF_BGRA8)
	{
		return false;
	}

	TArray64<uint8> Raw;
	if (!Texture->Source.GetMipData(Raw, 0))
	{
		return false;
	}

	const int32 TextureWidth = Texture->Source.GetSizeX();
	const int32 TextureHeight = Texture->Source.GetSizeY();
	if (Raw.Num() < static_cast<int64>(TextureWidth) * TextureHeight * 4)
	{
		return false;
	}

	const FVector2D SourceUV(Sprite->GetSourceUV());
	const FVector2D SourceSize(Sprite->GetSourceSize());
	const int32 X0 = FMath::Clamp(FMath::FloorToInt(SourceUV.X), 0, TextureWidth);
	const int32 Y0 = FMath::Clamp(FMath::FloorToInt(SourceUV.Y), 0, TextureHeight);
	const int32 Width = FMath::Clamp(FMath::FloorToInt(SourceSize.X), 0, TextureWidth - X0);
	const int32 Height = FMath::Clamp(FMath::FloorToInt(SourceSize.Y), 0, TextureHeight - Y0);
	if (Width <= 0 || Height <= 0)
	{
		return false;
	}

	OutPixels.SetNumUninitialized(Width * Height);
	for (int32 Y = 0; Y < Height; ++Y)
	{
		for (int32 X = 0; X < Width; ++X)
		{
			const int64 ByteIndex = (static_cast<int64>(Y0 + Y) * TextureWidth + X0 + X) * 4;
			OutPixels[Y * Width + X] = FColor(
				Raw[ByteIndex + 2], Raw[ByteIndex + 1], Raw[ByteIndex], Raw[ByteIndex + 3]);
		}
	}
	OutWidth = Width;
	OutHeight = Height;
	return true;
#else
	return false;
#endif
}
