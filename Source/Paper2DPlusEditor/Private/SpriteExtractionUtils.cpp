// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "SpriteExtractionUtils.h"
#include "Engine/Texture2D.h"
#include "PaperSprite.h"
#include "AssetRegistry/AssetRegistryModule.h"

bool FSpriteExtractionUtils::NeedsPaper2DSettings(const UTexture2D* Texture)
{
	if (!Texture) return false;

	return Texture->CompressionSettings != TC_EditorIcon
		|| Texture->Filter != TF_Nearest
		|| Texture->MipGenSettings != TMGS_NoMipmaps
		|| Texture->LODGroup != TEXTUREGROUP_Pixels2D;
}

void FSpriteExtractionUtils::ApplyPaper2DSettings(UTexture2D* Texture)
{
	if (!Texture) return;

	Texture->Modify();

	Texture->CompressionSettings = TC_EditorIcon;
	Texture->Filter = TF_Nearest;
	Texture->MipGenSettings = TMGS_NoMipmaps;
	Texture->LODGroup = TEXTUREGROUP_Pixels2D;
	Texture->NeverStream = true;
	Texture->SRGB = true;

	Texture->UpdateResource();
	Texture->PostEditChange();
	Texture->GetPackage()->MarkPackageDirty();
}

bool FSpriteExtractionUtils::ForceCPUAccess(UTexture2D* Texture)
{
	if (!Texture) return false;

	if (Texture->Source.GetNumMips() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("SpriteExtractionUtils: Texture '%s' has no source mips — reimport from source may be required"),
			*Texture->GetName());
		return false;
	}

	Texture->Modify();
	Texture->ForceRebuildPlatformData();
	Texture->UpdateResource();
	Texture->GetPackage()->MarkPackageDirty();

	return true;
}

bool FSpriteExtractionUtils::LoadTextureData(
	UTexture2D* Texture,
	TArray<FColor>& OutPixels,
	int32& OutWidth,
	int32& OutHeight)
{
	if (!Texture) return false;

	if (!Texture->GetPlatformData() || Texture->GetPlatformData()->Mips.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("SpriteExtractionUtils: Texture has no platform data or mips"));
		return false;
	}

	OutWidth = Texture->GetSizeX();
	OutHeight = Texture->GetSizeY();

	FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
	void* Data = Mip.BulkData.Lock(LOCK_READ_ONLY);

	if (!Data)
	{
		Mip.BulkData.Unlock();
		return false;
	}

	int32 PixelCount = OutWidth * OutHeight;
	OutPixels.SetNum(PixelCount);

	EPixelFormat Format = Texture->GetPixelFormat();
	if (Format == PF_B8G8R8A8)
	{
		FMemory::Memcpy(OutPixels.GetData(), Data, PixelCount * sizeof(FColor));
	}
	else if (Format == PF_R8G8B8A8)
	{
		const uint8* SrcData = static_cast<const uint8*>(Data);
		for (int32 i = 0; i < PixelCount; i++)
		{
			OutPixels[i].R = SrcData[i * 4 + 0];
			OutPixels[i].G = SrcData[i * 4 + 1];
			OutPixels[i].B = SrcData[i * 4 + 2];
			OutPixels[i].A = SrcData[i * 4 + 3];
		}
	}
	else
	{
		Mip.BulkData.Unlock();
		return false;
	}

	Mip.BulkData.Unlock();
	return true;
}

UPaperSprite* FSpriteExtractionUtils::CreateSpriteFromBounds(
	UTexture2D* SourceTexture,
	const FIntRect& Bounds,
	const FString& SpriteName,
	const FString& OutputPath)
{
	if (!SourceTexture) return nullptr;

	FString PackageName = OutputPath / SpriteName;

	UPackage* Package = CreatePackage(*PackageName);
	if (!Package) return nullptr;

	// Check for existing object to avoid fatal crash on name collision
	UPaperSprite* Sprite = FindObject<UPaperSprite>(Package, *SpriteName);
	if (!Sprite)
	{
		// Check if a different-class object exists with this name
		UObject* Existing = StaticFindObject(UObject::StaticClass(), Package, *SpriteName);
		if (Existing)
		{
			// Remove the conflicting object so NewObject can succeed
			Existing->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_ForceNoResetLoaders);
		}
		Sprite = NewObject<UPaperSprite>(Package, *SpriteName, RF_Public | RF_Standalone);
	}
	if (!Sprite) return nullptr;

	FSpriteAssetInitParameters InitParams;
	InitParams.Texture = SourceTexture;
	InitParams.Offset = FIntPoint(Bounds.Min.X, Bounds.Min.Y);
	InitParams.Dimension = FIntPoint(Bounds.Width(), Bounds.Height());
	InitParams.SetPixelsPerUnrealUnit(1.0f);
	Sprite->InitializeSprite(InitParams);

	Package->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(Sprite);

	return Sprite;
}
