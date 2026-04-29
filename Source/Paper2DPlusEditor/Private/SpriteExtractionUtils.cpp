// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "SpriteExtractionUtils.h"
#include "Engine/Texture2D.h"
#include "PaperSprite.h"
#include "PaperFlipbook.h"
#include "SpriteEditorOnlyTypes.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "ObjectTools.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/Crc.h"
#include "HAL/PlatformFileManager.h"

/** FSpriteExtractionUtils — Shared sprite detection algorithms: island flood-fill, grid analysis, texture padding, cross-sheet alignment, and asset creation utilities. */

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

	// Read from source data (editor-only, always uncompressed) instead of platform data
	// which may be compressed (DXT/BC), power-of-2 padded, or not CPU-resident.
	if (Texture->Source.GetNumMips() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("SpriteExtractionUtils: Texture '%s' has no source mips — reimport may be required"),
			*Texture->GetName());
		return false;
	}

	OutWidth = Texture->Source.GetSizeX();
	OutHeight = Texture->Source.GetSizeY();

	const int32 PixelCount = OutWidth * OutHeight;
	if (PixelCount <= 0) return false;

	uint8* Data = Texture->Source.LockMip(0);
	if (!Data)
	{
		Texture->Source.UnlockMip(0);
		return false;
	}

	OutPixels.SetNum(PixelCount);

	const ETextureSourceFormat Format = Texture->Source.GetFormat();
	if (Format == TSF_BGRA8)
	{
		FMemory::Memcpy(OutPixels.GetData(), Data, PixelCount * sizeof(FColor));
	}
	else if (Format == TSF_G8)
	{
		// Grayscale — treat as opaque white with alpha = pixel value
		for (int32 i = 0; i < PixelCount; i++)
		{
			OutPixels[i].R = 255;
			OutPixels[i].G = 255;
			OutPixels[i].B = 255;
			OutPixels[i].A = Data[i];
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("SpriteExtractionUtils: Unsupported source format %d for texture '%s'"),
			static_cast<int32>(Format), *Texture->GetName());
		Texture->Source.UnlockMip(0);
		return false;
	}

	Texture->Source.UnlockMip(0);
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

UTexture2D* FSpriteExtractionUtils::CreatePackedTexture(
	UTexture2D* SourceTexture,
	const TArray<FIntRect>& Regions,
	const FString& TextureName,
	const FString& OutputPath)
{
	if (!SourceTexture || Regions.Num() == 0) return nullptr;

	// Load source pixel data
	TArray<FColor> SrcPixels;
	int32 SrcW = 0, SrcH = 0;
	if (!LoadTextureData(SourceTexture, SrcPixels, SrcW, SrcH)) return nullptr;

	// Compute packed dimensions: single horizontal strip
	int32 RegionW = 0, RegionH = 0;
	for (const FIntRect& R : Regions)
	{
		RegionW = FMath::Max(RegionW, R.Width());
		RegionH = FMath::Max(RegionH, R.Height());
	}
	const int32 PackedW = RegionW * Regions.Num();
	const int32 PackedH = RegionH;

	// Allocate packed pixel buffer (transparent black)
	TArray<FColor> PackedPixels;
	PackedPixels.SetNumZeroed(PackedW * PackedH);

	// Copy each region into the strip
	for (int32 i = 0; i < Regions.Num(); i++)
	{
		const FIntRect& R = Regions[i];
		const int32 DstOffsetX = i * RegionW;
		// Center the region within its cell if it's smaller than RegionW x RegionH
		const int32 PadX = (RegionW - R.Width()) / 2;
		const int32 PadY = (RegionH - R.Height()) / 2;

		for (int32 Y = 0; Y < R.Height(); Y++)
		{
			const int32 SrcY = R.Min.Y + Y;
			if (SrcY < 0 || SrcY >= SrcH) continue;
			for (int32 X = 0; X < R.Width(); X++)
			{
				const int32 SrcX = R.Min.X + X;
				if (SrcX < 0 || SrcX >= SrcW) continue;
				const int32 DstX = DstOffsetX + PadX + X;
				const int32 DstY = PadY + Y;
				if (DstX >= 0 && DstX < PackedW && DstY >= 0 && DstY < PackedH)
				{
					PackedPixels[DstY * PackedW + DstX] = SrcPixels[SrcY * SrcW + SrcX];
				}
			}
		}
	}

	// Create the texture asset (handle name collision for re-extract)
	FString PackageName = OutputPath / TextureName;
	UPackage* Package = CreatePackage(*PackageName);
	if (!Package) return nullptr;

	// Remove existing object with same name to avoid NewObject collision
	UTexture2D* NewTexture = FindObject<UTexture2D>(Package, *TextureName);
	if (!NewTexture)
	{
		UObject* Existing = StaticFindObject(UObject::StaticClass(), Package, *TextureName);
		if (Existing)
		{
			Existing->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_ForceNoResetLoaders);
		}
		NewTexture = NewObject<UTexture2D>(Package, *TextureName, RF_Public | RF_Standalone);
	}
	if (!NewTexture) return nullptr;

	// Initialize the texture
	NewTexture->Source.Init(PackedW, PackedH, 1, 1, TSF_BGRA8);

	// Write pixel data
	{
		uint8* DestData = NewTexture->Source.LockMip(0);
		FMemory::Memcpy(DestData, PackedPixels.GetData(), PackedW * PackedH * sizeof(FColor));
		NewTexture->Source.UnlockMip(0);
	}

	// Apply Paper2D pixel-art settings
	NewTexture->CompressionSettings = TC_EditorIcon;
	NewTexture->Filter = TF_Nearest;
	NewTexture->MipGenSettings = TMGS_NoMipmaps;
	NewTexture->LODGroup = TEXTUREGROUP_Pixels2D;
	NewTexture->NeverStream = true;
	NewTexture->SRGB = SourceTexture->SRGB;

	NewTexture->UpdateResource();
	Package->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(NewTexture);

	return NewTexture;
}

FIntRect FSpriteExtractionUtils::FindTightContentBounds(
	const TArray<FColor>& Pixels, int32 TexW, int32 TexH,
	const FIntRect& CellBounds, int32 AlphaThreshold)
{
	int32 MinX = CellBounds.Max.X, MinY = CellBounds.Max.Y;
	int32 MaxX = CellBounds.Min.X - 1, MaxY = CellBounds.Min.Y - 1;

	for (int32 Y = CellBounds.Min.Y; Y < CellBounds.Max.Y; Y++)
	{
		for (int32 X = CellBounds.Min.X; X < CellBounds.Max.X; X++)
		{
			const int32 Idx = Y * TexW + X;
			if (Idx >= 0 && Idx < Pixels.Num() && Pixels[Idx].A >= AlphaThreshold)
			{
				MinX = FMath::Min(MinX, X);
				MinY = FMath::Min(MinY, Y);
				MaxX = FMath::Max(MaxX, X);
				MaxY = FMath::Max(MaxY, Y);
			}
		}
	}

	if (MaxX < MinX || MaxY < MinY)
	{
		return FIntRect(CellBounds.Min.X, CellBounds.Min.Y, CellBounds.Min.X, CellBounds.Min.Y);
	}
	return FIntRect(MinX, MinY, MaxX + 1, MaxY + 1);
}

UTexture2D* FSpriteExtractionUtils::CreateTrimmedPackedTexture(
	UTexture2D* SourceTexture,
	const TArray<FIntRect>& TightRegions,
	const FString& TextureName,
	const FString& OutputPath,
	TArray<FIntRect>& OutPackedBounds)
{
	if (!SourceTexture || TightRegions.Num() == 0) return nullptr;

	TArray<FColor> SrcPixels;
	int32 SrcW = 0, SrcH = 0;
	if (!LoadTextureData(SourceTexture, SrcPixels, SrcW, SrcH)) return nullptr;

	int32 MaxH = 0;
	int32 TotalW = 0;
	for (const FIntRect& R : TightRegions)
	{
		const int32 W = FMath::Max(R.Width(), 1);
		TotalW += W;
		MaxH = FMath::Max(MaxH, FMath::Max(R.Height(), 1));
	}
	if (TotalW == 0 || MaxH == 0) return nullptr;

	TArray<FColor> PackedPixels;
	PackedPixels.SetNumZeroed(TotalW * MaxH);

	OutPackedBounds.Empty(TightRegions.Num());
	int32 CurX = 0;
	for (int32 i = 0; i < TightRegions.Num(); i++)
	{
		const FIntRect& R = TightRegions[i];
		const int32 RW = FMath::Max(R.Width(), 1);
		const int32 RH = FMath::Max(R.Height(), 1);
		OutPackedBounds.Add(FIntRect(CurX, 0, CurX + RW, RH));

		for (int32 Y = 0; Y < R.Height(); Y++)
		{
			const int32 SrcY = R.Min.Y + Y;
			if (SrcY < 0 || SrcY >= SrcH) continue;
			for (int32 X = 0; X < R.Width(); X++)
			{
				const int32 SrcX = R.Min.X + X;
				if (SrcX < 0 || SrcX >= SrcW) continue;
				const int32 DstIdx = Y * TotalW + CurX + X;
				const int32 SrcIdx = SrcY * SrcW + SrcX;
				if (DstIdx >= 0 && DstIdx < PackedPixels.Num() && SrcIdx >= 0 && SrcIdx < SrcPixels.Num())
				{
					PackedPixels[DstIdx] = SrcPixels[SrcIdx];
				}
			}
		}
		CurX += RW;
	}

	FString PackageName = OutputPath / TextureName;
	UPackage* Package = CreatePackage(*PackageName);
	if (!Package) return nullptr;

	UTexture2D* NewTexture = FindObject<UTexture2D>(Package, *TextureName);
	if (!NewTexture)
	{
		UObject* Existing = StaticFindObject(UObject::StaticClass(), Package, *TextureName);
		if (Existing)
		{
			Existing->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_ForceNoResetLoaders);
		}
		NewTexture = NewObject<UTexture2D>(Package, *TextureName, RF_Public | RF_Standalone);
	}
	if (!NewTexture) return nullptr;

	NewTexture->Source.Init(TotalW, MaxH, 1, 1, TSF_BGRA8);
	{
		uint8* DestData = NewTexture->Source.LockMip(0);
		FMemory::Memcpy(DestData, PackedPixels.GetData(), TotalW * MaxH * sizeof(FColor));
		NewTexture->Source.UnlockMip(0);
	}

	UE_LOG(LogTemp, Log, TEXT("CreateTrimmedPackedTexture: %s — %d regions → %dx%d strip"),
		*TextureName, TightRegions.Num(), TotalW, MaxH);
	for (int32 i = 0; i < OutPackedBounds.Num(); i++)
	{
		const FIntRect& PB = OutPackedBounds[i];
		const FIntRect& TR = TightRegions[i];
		UE_LOG(LogTemp, Log, TEXT("  Region[%d]: SrcTight=(%d,%d)-(%d,%d) %dx%d → Packed=(%d,%d)-(%d,%d)"),
			i, TR.Min.X, TR.Min.Y, TR.Max.X, TR.Max.Y, TR.Width(), TR.Height(),
			PB.Min.X, PB.Min.Y, PB.Max.X, PB.Max.Y);
	}

	NewTexture->CompressionSettings = TC_EditorIcon;
	NewTexture->Filter = TF_Nearest;
	NewTexture->MipGenSettings = TMGS_NoMipmaps;
	NewTexture->LODGroup = TEXTUREGROUP_Pixels2D;
	NewTexture->NeverStream = true;
	NewTexture->SRGB = SourceTexture->SRGB;

	NewTexture->UpdateResource();
	Package->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(NewTexture);

	return NewTexture;
}

// ==========================================
// Detection & uniform bounds
// ==========================================

bool FSpriteExtractionUtils::IsPixelOpaque(const TArray<FColor>& Pixels, int32 Width, int32 X, int32 Y, int32 AlphaThreshold)
{
	int32 Index = Y * Width + X;
	if (Index < 0 || Index >= Pixels.Num()) return false;
	return Pixels[Index].A >= AlphaThreshold;
}

/*
 * Island detection via iterative flood fill.
 *
 * Starting from (StartX, StartY), expands outward marking all connected opaque
 * pixels as visited. A pixel is "opaque" if its alpha >= AlphaThreshold.
 *
 * 4-directional: checks N/S/E/W neighbors. Good for pixel art with clean edges.
 * 8-directional: also checks diagonals. Needed for sprites with diagonal limbs
 *   or angled swords that only touch at corner pixels.
 *
 * Uses an explicit stack (not recursion) to avoid stack overflow on large sprites.
 * OutBounds is expanded to encompass all visited pixels — the caller uses this as
 * the bounding rect for the detected sprite.
 */
void FSpriteExtractionUtils::FloodFillMark(TArray<bool>& Visited, const TArray<FColor>& Pixels,
	int32 Width, int32 Height, int32 StartX, int32 StartY, FIntRect& OutBounds, bool bUse8Dir, int32 AlphaThreshold)
{
	TArray<FIntPoint> Stack;
	Stack.Push(FIntPoint(StartX, StartY));

	static const int32 DX4[] = { -1, 1, 0, 0 };
	static const int32 DY4[] = { 0, 0, -1, 1 };
	static const int32 DX8[] = { -1, 1, 0, 0, -1, -1, 1, 1 };
	static const int32 DY8[] = { 0, 0, -1, 1, -1, 1, -1, 1 };

	const int32* DX = bUse8Dir ? DX8 : DX4;
	const int32* DY = bUse8Dir ? DY8 : DY4;
	const int32 NumDirections = bUse8Dir ? 8 : 4;

	while (Stack.Num() > 0)
	{
		FIntPoint Pos = Stack.Pop();
		int32 X = Pos.X;
		int32 Y = Pos.Y;

		if (X < 0 || X >= Width || Y < 0 || Y >= Height) continue;

		int32 Index = Y * Width + X;
		if (Visited[Index]) continue;
		if (!IsPixelOpaque(Pixels, Width, X, Y, AlphaThreshold)) continue;

		Visited[Index] = true;

		OutBounds.Min.X = FMath::Min(OutBounds.Min.X, X);
		OutBounds.Min.Y = FMath::Min(OutBounds.Min.Y, Y);
		OutBounds.Max.X = FMath::Max(OutBounds.Max.X, X + 1);
		OutBounds.Max.Y = FMath::Max(OutBounds.Max.Y, Y + 1);

		for (int32 i = 0; i < NumDirections; i++)
		{
			Stack.Push(FIntPoint(X + DX[i], Y + DY[i]));
		}
	}
}

/*
 * Merge pass: combines nearby islands whose bounding rects are within MergeDistance pixels.
 *
 * Each sprite's bounds are expanded by MergeDistance in all directions. If expanded bounds
 * overlap another sprite, the two are merged into one (union of both original bounds).
 * Repeats until no more merges occur. O(n^2) per pass but sprite counts are typically <100.
 *
 * This handles sprite sheets where limbs or weapons are separated by a few transparent
 * pixels but should be treated as one sprite.
 */
void FSpriteExtractionUtils::MergeNearbyIslands(TArray<FDetectedSprite>& Sprites, int32 MergeDistance)
{
	if (MergeDistance <= 0) return;

	const int32 PreMergeCount = Sprites.Num();
	int32 MergeOps = 0;

	bool bMerged = true;
	while (bMerged)
	{
		bMerged = false;
		for (int32 i = 0; i < Sprites.Num(); i++)
		{
			FIntRect ExpandedI = Sprites[i].OriginalBounds;
			ExpandedI.Min.X -= MergeDistance;
			ExpandedI.Min.Y -= MergeDistance;
			ExpandedI.Max.X += MergeDistance;
			ExpandedI.Max.Y += MergeDistance;

			for (int32 j = i + 1; j < Sprites.Num(); j++)
			{
				const FIntRect& BoundsJ = Sprites[j].OriginalBounds;

				bool bIntersects = !(ExpandedI.Max.X <= BoundsJ.Min.X ||
									 ExpandedI.Min.X >= BoundsJ.Max.X ||
									 ExpandedI.Max.Y <= BoundsJ.Min.Y ||
									 ExpandedI.Min.Y >= BoundsJ.Max.Y);

				if (bIntersects)
				{
					UE_LOG(LogTemp, Log, TEXT("SpriteExtractor: Merging island[%d] (%d,%d)-(%d,%d) with island[%d] (%d,%d)-(%d,%d) (dist=%d)"),
						i, Sprites[i].OriginalBounds.Min.X, Sprites[i].OriginalBounds.Min.Y,
						Sprites[i].OriginalBounds.Max.X, Sprites[i].OriginalBounds.Max.Y,
						j, BoundsJ.Min.X, BoundsJ.Min.Y, BoundsJ.Max.X, BoundsJ.Max.Y, MergeDistance);

					Sprites[i].OriginalBounds.Min.X = FMath::Min(Sprites[i].OriginalBounds.Min.X, BoundsJ.Min.X);
					Sprites[i].OriginalBounds.Min.Y = FMath::Min(Sprites[i].OriginalBounds.Min.Y, BoundsJ.Min.Y);
					Sprites[i].OriginalBounds.Max.X = FMath::Max(Sprites[i].OriginalBounds.Max.X, BoundsJ.Max.X);
					Sprites[i].OriginalBounds.Max.Y = FMath::Max(Sprites[i].OriginalBounds.Max.Y, BoundsJ.Max.Y);
					Sprites[i].Bounds = Sprites[i].OriginalBounds;

					UE_LOG(LogTemp, Log, TEXT("SpriteExtractor:   Result: (%d,%d)-(%d,%d) %dx%d"),
						Sprites[i].OriginalBounds.Min.X, Sprites[i].OriginalBounds.Min.Y,
						Sprites[i].OriginalBounds.Max.X, Sprites[i].OriginalBounds.Max.Y,
						Sprites[i].OriginalBounds.Width(), Sprites[i].OriginalBounds.Height());

					Sprites.RemoveAt(j);
					bMerged = true;
					MergeOps++;
					break;
				}
			}
			if (bMerged) break;
		}
	}

	UE_LOG(LogTemp, Log, TEXT("SpriteExtractor: Merge complete: %d → %d sprites (%d merge ops, dist=%d)"),
		PreMergeCount, Sprites.Num(), MergeOps, MergeDistance);

	for (int32 i = 0; i < Sprites.Num(); i++)
	{
		Sprites[i].Index = i;
	}
}

TArray<FDetectedSprite> FSpriteExtractionUtils::DetectSpriteBounds(
	UTexture2D* Texture,
	const FSpriteDetectionParams& Params)
{
	TArray<FDetectedSprite> Result;
	if (!Texture) return Result;

	TArray<FColor> Pixels;
	int32 Width, Height;
	if (!LoadTextureData(Texture, Pixels, Width, Height)) return Result;

	TArray<bool> Visited;
	Visited.SetNumZeroed(Width * Height);

	UE_LOG(LogTemp, Log, TEXT("SpriteExtractor: DetectSpriteBounds — Texture=%s %dx%d, MinSize=%d, Alpha=%d, MergeDist=%d, 8Dir=%d"),
		*Texture->GetName(), Width, Height, Params.MinSpriteSize, Params.AlphaThreshold,
		Params.IslandMergeDistance, Params.bUse8DirectionalFloodFill ? 1 : 0);

	int32 Index = 0;
	int32 FilteredCount = 0;
	for (int32 Y = 0; Y < Height; Y++)
	{
		for (int32 X = 0; X < Width; X++)
		{
			if (!Visited[Y * Width + X] && IsPixelOpaque(Pixels, Width, X, Y, Params.AlphaThreshold))
			{
				FIntRect Bounds(X, Y, X, Y);
				FloodFillMark(Visited, Pixels, Width, Height, X, Y, Bounds,
					Params.bUse8DirectionalFloodFill, Params.AlphaThreshold);

				if (Bounds.Width() >= Params.MinSpriteSize && Bounds.Height() >= Params.MinSpriteSize)
				{
					FDetectedSprite Sprite;
					Sprite.Bounds = Bounds;
					Sprite.OriginalBounds = Bounds;
					Sprite.bSelected = true;
					Sprite.Index = Index++;
					Result.Add(Sprite);
					UE_LOG(LogTemp, Verbose, TEXT("SpriteExtractor:   Island[%d] accepted: (%d,%d)-(%d,%d) %dx%d"),
						Sprite.Index, Bounds.Min.X, Bounds.Min.Y, Bounds.Max.X, Bounds.Max.Y,
						Bounds.Width(), Bounds.Height());
				}
				else
				{
					FilteredCount++;
					UE_LOG(LogTemp, Verbose, TEXT("SpriteExtractor:   Island REJECTED (too small): (%d,%d)-(%d,%d) %dx%d < min %d"),
						Bounds.Min.X, Bounds.Min.Y, Bounds.Max.X, Bounds.Max.Y,
						Bounds.Width(), Bounds.Height(), Params.MinSpriteSize);
				}
			}
		}
	}

	UE_LOG(LogTemp, Log, TEXT("SpriteExtractor: Pre-merge: %d islands accepted, %d filtered (MinSize=%d)"),
		Result.Num(), FilteredCount, Params.MinSpriteSize);

	MergeNearbyIslands(Result, Params.IslandMergeDistance);

	// Sort left-to-right
	Result.Sort([](const FDetectedSprite& A, const FDetectedSprite& B)
	{
		if (A.Bounds.Min.X != B.Bounds.Min.X) return A.Bounds.Min.X < B.Bounds.Min.X;
		return A.Bounds.Min.Y < B.Bounds.Min.Y;
	});

	for (int32 i = 0; i < Result.Num(); i++)
	{
		Result[i].Index = i;
	}

	UE_LOG(LogTemp, Log, TEXT("SpriteExtractor: Final result: %d sprites (sorted L→R)"), Result.Num());
	for (int32 i = 0; i < Result.Num(); i++)
	{
		const FIntRect& B = Result[i].OriginalBounds;
		UE_LOG(LogTemp, Log, TEXT("SpriteExtractor:   [%d] (%d,%d)-(%d,%d) %dx%d"),
			i, B.Min.X, B.Min.Y, B.Max.X, B.Max.Y, B.Width(), B.Height());
	}

	return Result;
}

FIntPoint FSpriteExtractionUtils::GetDetectionDimensions(const UTexture2D* Texture)
{
	if (Texture && Texture->Source.GetNumMips() > 0)
	{
		return FIntPoint(Texture->Source.GetSizeX(), Texture->Source.GetSizeY());
	}
	if (Texture)
	{
		return FIntPoint(Texture->GetSizeX(), Texture->GetSizeY());
	}
	return FIntPoint::ZeroValue;
}

void FSpriteExtractionUtils::ComputeUniformBounds(
	TArray<FDetectedSprite>& Sprites,
	int32 TexW,
	int32 TexH)
{
	if (Sprites.Num() < 2 || TexW <= 0 || TexH <= 0) return;

	// --- Row grouping via Y-range overlap ---
	// Two sprites are on the same row if their vertical ranges overlap.
	// This is robust against mixtures of tall and short sprites (e.g., a 35px
	// body and its 7px feet fragment both span Y=57-96 / Y=89-96 — they overlap).
	TArray<int32> SortedIdx;
	for (int32 i = 0; i < Sprites.Num(); i++) { SortedIdx.Add(i); }
	SortedIdx.Sort([&Sprites](int32 A, int32 B)
	{
		const int32 CYA = (Sprites[A].OriginalBounds.Min.Y + Sprites[A].OriginalBounds.Max.Y) / 2;
		const int32 CYB = (Sprites[B].OriginalBounds.Min.Y + Sprites[B].OriginalBounds.Max.Y) / 2;
		if (CYA != CYB) return CYA < CYB;
		return Sprites[A].OriginalBounds.Min.X < Sprites[B].OriginalBounds.Min.X;
	});

	TArray<TArray<int32>> Rows;
	TArray<int32> RowMinY, RowMaxY;
	Rows.AddDefaulted();
	RowMinY.Add(Sprites[SortedIdx[0]].OriginalBounds.Min.Y);
	RowMaxY.Add(Sprites[SortedIdx[0]].OriginalBounds.Max.Y);
	Rows.Last().Add(SortedIdx[0]);
	for (int32 i = 1; i < SortedIdx.Num(); i++)
	{
		const FIntRect& B = Sprites[SortedIdx[i]].OriginalBounds;
		const bool bOverlaps = !(B.Max.Y <= RowMinY.Last() || B.Min.Y >= RowMaxY.Last());
		if (bOverlaps)
		{
			RowMinY.Last() = FMath::Min(RowMinY.Last(), B.Min.Y);
			RowMaxY.Last() = FMath::Max(RowMaxY.Last(), B.Max.Y);
		}
		else
		{
			Rows.AddDefaulted();
			RowMinY.Add(B.Min.Y);
			RowMaxY.Add(B.Max.Y);
		}
		Rows.Last().Add(SortedIdx[i]);
	}

	for (TArray<int32>& Row : Rows)
	{
		Row.Sort([&Sprites](int32 A, int32 B)
		{ return Sprites[A].OriginalBounds.Min.X < Sprites[B].OriginalBounds.Min.X; });
	}

	// --- Cell strides from texture dimensions ---
	const int32 CellStrideY = TexH / Rows.Num();

	auto GetMidX = [TexW, &Rows](int32 RowIdx, int32 ColIdx) -> int32
	{
		const int32 RowStrideX = TexW / Rows[RowIdx].Num();
		return ColIdx * RowStrideX + RowStrideX / 2;
	};

	// --- Diagnostic logging ---
	UE_LOG(LogTemp, Log, TEXT("UniformBounds: TexW=%d TexH=%d, %d sprites, %d rows"), TexW, TexH, Sprites.Num(), Rows.Num());
	for (int32 RowIdx = 0; RowIdx < Rows.Num(); RowIdx++)
	{
		const int32 RowStrideX = TexW / Rows[RowIdx].Num();
		UE_LOG(LogTemp, Log, TEXT("  Row %d: %d sprites, strideX=%d, strideY=%d"), RowIdx, Rows[RowIdx].Num(), RowStrideX, CellStrideY);
		for (int32 ColIdx = 0; ColIdx < Rows[RowIdx].Num(); ColIdx++)
		{
			const FIntRect& B = Sprites[Rows[RowIdx][ColIdx]].OriginalBounds;
			const int32 MidX = GetMidX(RowIdx, ColIdx);
			const int32 MidY = RowIdx * CellStrideY + CellStrideY / 2;
			const int32 SpriteCX = (B.Min.X + B.Max.X) / 2;
			const int32 SpriteCY = (B.Min.Y + B.Max.Y) / 2;
			UE_LOG(LogTemp, Log, TEXT("    [%d,%d] bounds=(%d,%d)-(%d,%d) %dx%d  midpoint=(%d,%d)  spriteCenter=(%d,%d)  drift=(%d,%d)"),
				RowIdx, ColIdx, B.Min.X, B.Min.Y, B.Max.X, B.Max.Y, B.Width(), B.Height(),
				MidX, MidY, SpriteCX, SpriteCY, MidX - SpriteCX, MidY - SpriteCY);
		}
	}

	// --- Max extents from cell midpoints ---
	int32 MaxExtLeft = 0, MaxExtRight = 0, MaxExtTop = 0, MaxExtBottom = 0;
	for (int32 RowIdx = 0; RowIdx < Rows.Num(); RowIdx++)
	{
		for (int32 ColIdx = 0; ColIdx < Rows[RowIdx].Num(); ColIdx++)
		{
			const FIntRect& B = Sprites[Rows[RowIdx][ColIdx]].OriginalBounds;
			const int32 MidX = GetMidX(RowIdx, ColIdx);
			const int32 MidY = RowIdx * CellStrideY + CellStrideY / 2;

			MaxExtLeft   = FMath::Max(MaxExtLeft,   MidX - B.Min.X);
			MaxExtRight  = FMath::Max(MaxExtRight,  B.Max.X - MidX);
			MaxExtTop    = FMath::Max(MaxExtTop,    MidY - B.Min.Y);
			MaxExtBottom = FMath::Max(MaxExtBottom,  B.Max.Y - MidY);
		}
	}

	UE_LOG(LogTemp, Log, TEXT("  MaxExtents: L=%d R=%d T=%d B=%d → uniform=%dx%d"),
		MaxExtLeft, MaxExtRight, MaxExtTop, MaxExtBottom,
		MaxExtLeft + MaxExtRight, MaxExtTop + MaxExtBottom);

	// --- Apply uniform bounds ---
	for (int32 RowIdx = 0; RowIdx < Rows.Num(); RowIdx++)
	{
		for (int32 ColIdx = 0; ColIdx < Rows[RowIdx].Num(); ColIdx++)
		{
			FDetectedSprite& Sprite = Sprites[Rows[RowIdx][ColIdx]];
			const int32 MidX = GetMidX(RowIdx, ColIdx);
			const int32 MidY = RowIdx * CellStrideY + CellStrideY / 2;

			Sprite.Bounds = FIntRect(
				FMath::Max(0, MidX - MaxExtLeft),
				FMath::Max(0, MidY - MaxExtTop),
				FMath::Min(TexW, MidX + MaxExtRight),
				FMath::Min(TexH, MidY + MaxExtBottom));

			UE_LOG(LogTemp, Log, TEXT("UniformBounds: [%d,%d] original=(%d,%d)-(%d,%d) %dx%d → uniform=(%d,%d)-(%d,%d) %dx%d  mid=(%d,%d)"),
				RowIdx, ColIdx,
				Sprite.OriginalBounds.Min.X, Sprite.OriginalBounds.Min.Y,
				Sprite.OriginalBounds.Max.X, Sprite.OriginalBounds.Max.Y,
				Sprite.OriginalBounds.Width(), Sprite.OriginalBounds.Height(),
				Sprite.Bounds.Min.X, Sprite.Bounds.Min.Y,
				Sprite.Bounds.Max.X, Sprite.Bounds.Max.Y,
				Sprite.Bounds.Width(), Sprite.Bounds.Height(),
				MidX, MidY);
		}
	}
}

FIntRect FSpriteExtractionUtils::ExpandBoundsToUniform(
	const FIntRect& OriginalBounds,
	FIntPoint UniformSize,
	ESpriteAnchor Anchor,
	int32 TexW,
	int32 TexH)
{
	const int32 DeltaW = UniformSize.X - OriginalBounds.Width();
	const int32 DeltaH = UniformSize.Y - OriginalBounds.Height();

	int32 MinX, MinY, MaxX, MaxY;

	// Horizontal anchor
	switch (Anchor)
	{
	case ESpriteAnchor::TopLeft:
	case ESpriteAnchor::CenterLeft:
	case ESpriteAnchor::BottomLeft:
		MinX = OriginalBounds.Min.X;
		MaxX = MinX + UniformSize.X;
		break;
	case ESpriteAnchor::TopRight:
	case ESpriteAnchor::CenterRight:
	case ESpriteAnchor::BottomRight:
		MaxX = OriginalBounds.Max.X;
		MinX = MaxX - UniformSize.X;
		break;
	default:
		MinX = OriginalBounds.Min.X - DeltaW / 2;
		MaxX = MinX + UniformSize.X;
		break;
	}

	// Vertical anchor
	switch (Anchor)
	{
	case ESpriteAnchor::TopLeft:
	case ESpriteAnchor::TopCenter:
	case ESpriteAnchor::TopRight:
		MinY = OriginalBounds.Min.Y;
		MaxY = MinY + UniformSize.Y;
		break;
	case ESpriteAnchor::BottomLeft:
	case ESpriteAnchor::BottomCenter:
	case ESpriteAnchor::BottomRight:
		MaxY = OriginalBounds.Max.Y;
		MinY = MaxY - UniformSize.Y;
		break;
	default:
		MinY = OriginalBounds.Min.Y - DeltaH / 2;
		MaxY = MinY + UniformSize.Y;
		break;
	}

	// Shift back inside texture (preserve uniform size)
	if (MinX < 0)    { MaxX -= MinX;          MinX = 0; }
	if (MinY < 0)    { MaxY -= MinY;          MinY = 0; }
	if (MaxX > TexW) { MinX -= (MaxX - TexW); MaxX = TexW; }
	if (MaxY > TexH) { MinY -= (MaxY - TexH); MaxY = TexH; }

	MinX = FMath::Max(0, MinX);
	MinY = FMath::Max(0, MinY);
	MaxX = FMath::Min(TexW, MaxX);
	MaxY = FMath::Min(TexH, MaxY);

	return FIntRect(MinX, MinY, MaxX, MaxY);
}

void FSpriteExtractionUtils::UpdateSpriteSourceRegion(
	UPaperSprite* Sprite,
	UTexture2D* NewTexture,
	const FIntRect& NewBounds,
	FIntPoint ArtShiftDelta)
{
	if (!Sprite || !NewTexture) return;

	FVector2D OldCustomPivot;
	ESpritePivotMode::Type OldPivotMode = Sprite->GetPivotMode(OldCustomPivot);
	const FVector2D OldSourceUV = Sprite->GetSourceUV();

	FSpriteAssetInitParameters InitParams;
	InitParams.Texture = NewTexture;
	InitParams.Offset = FIntPoint(NewBounds.Min.X, NewBounds.Min.Y);
	InitParams.Dimension = FIntPoint(NewBounds.Width(), NewBounds.Height());
	InitParams.SetPixelsPerUnrealUnit(1.0f);
	Sprite->InitializeSprite(InitParams);

	// Restore pivot. Named modes (Center, BottomCenter, etc.) recompute correctly from
	// the new SourceUV + SourceDimension. Custom pivots are in absolute texture-space
	// coordinates, so we remap: old sprite-local + art shift → new texture space.
	FVector2D RestoredPivot = OldCustomPivot;
	if (OldPivotMode == ESpritePivotMode::Custom)
	{
		const FVector2D PivotInOldSprite = OldCustomPivot - OldSourceUV;
		const FVector2D NewSourceUV(NewBounds.Min.X, NewBounds.Min.Y);
		RestoredPivot = NewSourceUV + PivotInOldSprite + FVector2D(ArtShiftDelta.X, ArtShiftDelta.Y);
	}
	Sprite->SetPivotMode(OldPivotMode, RestoredPivot, true);

	// Force full editor rebuild to flush TSoftObjectPtr caches
	Sprite->PostEditChange();
}

void FSpriteExtractionUtils::DeleteTextureAsset(UTexture2D* Texture)
{
	if (!Texture) return;

	TArray<UObject*> ObjectsToDelete;
	ObjectsToDelete.Add(Texture);
	ObjectTools::ForceDeleteObjects(ObjectsToDelete, false);
}

// ==========================================
// Cross-sheet alignment helpers (Unit 1b)
// ==========================================

FIntPoint FSpriteExtractionUtils::ComputeGroupMaxCellSize(const TMap<UTexture2D*, FIntPoint>& PerTextureCellSize)
{
	FIntPoint Max = FIntPoint::ZeroValue;
	for (const TPair<UTexture2D*, FIntPoint>& Entry : PerTextureCellSize)
	{
		Max.X = FMath::Max(Max.X, Entry.Value.X);
		Max.Y = FMath::Max(Max.Y, Entry.Value.Y);
	}
	return Max;
}

FIntPoint FSpriteExtractionUtils::InferGridDimensions(const TArray<FDetectedSprite>& Sprites)
{
	// Row grouping via Y-range overlap. This is a local DUPLICATE of the algorithm in
	// ComputeUniformBounds (SpriteExtractionUtils.cpp:477-515); the stability-critical
	// function is not factored out to preserve the algorithm byte-for-byte per the
	// stability doc at docs/solutions/ue-uniform-bounds-detection-architecture.md.
	// A drift-detection test asserts both implementations produce the same Rows/ColsPerRow
	// on identical input.
	if (Sprites.Num() < 2) return FIntPoint::ZeroValue;

	TArray<int32> SortedIdx;
	for (int32 i = 0; i < Sprites.Num(); i++) { SortedIdx.Add(i); }
	SortedIdx.Sort([&Sprites](int32 A, int32 B)
	{
		const int32 CYA = (Sprites[A].OriginalBounds.Min.Y + Sprites[A].OriginalBounds.Max.Y) / 2;
		const int32 CYB = (Sprites[B].OriginalBounds.Min.Y + Sprites[B].OriginalBounds.Max.Y) / 2;
		if (CYA != CYB) return CYA < CYB;
		return Sprites[A].OriginalBounds.Min.X < Sprites[B].OriginalBounds.Min.X;
	});

	TArray<TArray<int32>> Rows;
	TArray<int32> RowMinY, RowMaxY;
	Rows.AddDefaulted();
	RowMinY.Add(Sprites[SortedIdx[0]].OriginalBounds.Min.Y);
	RowMaxY.Add(Sprites[SortedIdx[0]].OriginalBounds.Max.Y);
	Rows.Last().Add(SortedIdx[0]);
	for (int32 i = 1; i < SortedIdx.Num(); i++)
	{
		const FIntRect& B = Sprites[SortedIdx[i]].OriginalBounds;
		const bool bOverlaps = !(B.Max.Y <= RowMinY.Last() || B.Min.Y >= RowMaxY.Last());
		if (bOverlaps)
		{
			RowMinY.Last() = FMath::Min(RowMinY.Last(), B.Min.Y);
			RowMaxY.Last() = FMath::Max(RowMaxY.Last(), B.Max.Y);
		}
		else
		{
			Rows.AddDefaulted();
			RowMinY.Add(B.Min.Y);
			RowMaxY.Add(B.Max.Y);
		}
		Rows.Last().Add(SortedIdx[i]);
	}

	// Cols = max column count across rows (handles sparse trailing rows; columns in a row
	// whose bounds are smaller than the max are simply "missing" slots for a given row).
	int32 MaxCols = 0;
	for (const TArray<int32>& Row : Rows)
	{
		MaxCols = FMath::Max(MaxCols, Row.Num());
	}
	return FIntPoint(MaxCols, Rows.Num());
}

TArray<UPaper2DPlusCharacterProfileAsset*> FSpriteExtractionUtils::FindProfilesReferencingTexture(UTexture2D* Texture)
{
	TArray<UPaper2DPlusCharacterProfileAsset*> Out;
	if (!Texture) return Out;

	// Asset-registry scan is the authoritative path. TextureWatcherService is path-keyed
	// and has private accessors; the registry walk handles synthetic textures (no filesystem
	// path) and newly-created-in-session profiles that the watcher's map may not know about
	// until refresh. Cost is O(NumProfiles * AvgFlipbooks) but runs only during shared-texture
	// guard invocation (once per pad run).
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(
		TEXT("AssetRegistry"));
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	TArray<FAssetData> ProfileAssets;
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
	AssetRegistry.GetAssetsByClass(
		UPaper2DPlusCharacterProfileAsset::StaticClass()->GetFName(),
		ProfileAssets,
		/*bSearchSubClasses=*/true);
#else
	AssetRegistry.GetAssetsByClass(
		UPaper2DPlusCharacterProfileAsset::StaticClass()->GetClassPathName(),
		ProfileAssets,
		/*bSearchSubClasses=*/true);
#endif

	TSet<UPaper2DPlusCharacterProfileAsset*> Seen;

	for (const FAssetData& AssetData : ProfileAssets)
	{
		UPaper2DPlusCharacterProfileAsset* Profile = Cast<UPaper2DPlusCharacterProfileAsset>(
			AssetData.GetAsset());
		if (!Profile || Seen.Contains(Profile)) continue;

		bool bReferencesTexture = false;
		for (const FFlipbookProfileEntry& Entry : Profile->Flipbooks)
		{
			UPaperFlipbook* Flipbook = Entry.Identity.Flipbook.LoadSynchronous();
			if (!Flipbook) continue;

			for (int32 K = 0; K < Flipbook->GetNumKeyFrames(); K++)
			{
				const FPaperFlipbookKeyFrame& KF = Flipbook->GetKeyFrameChecked(K);
				if (!KF.Sprite) continue;
				if (Cast<UTexture2D>(KF.Sprite->GetSourceTexture()) == Texture)
				{
					bReferencesTexture = true;
					break;
				}
			}
			if (bReferencesTexture) break;
		}

		if (bReferencesTexture)
		{
			Seen.Add(Profile);
			Out.Add(Profile);
		}
	}

	return Out;
}

FIntPoint FSpriteExtractionUtils::PadTextureInPlace(
	UTexture2D* Texture,
	FIntPoint TargetCellDims,
	FIntPoint Grid,
	int32 GroundPlaneOffset)
{
	if (!Texture || TargetCellDims.X <= 0 || TargetCellDims.Y <= 0) return FIntPoint::ZeroValue;
	if (Grid.X <= 0 || Grid.Y <= 0) return FIntPoint::ZeroValue;

	TArray<FColor> SrcPixels;
	int32 SrcW = 0, SrcH = 0;
	if (!LoadTextureData(Texture, SrcPixels, SrcW, SrcH)) return FIntPoint::ZeroValue;

	const int32 SrcCellW = SrcW / Grid.X;
	const int32 SrcCellH = SrcH / Grid.Y;

	UE_LOG(LogTemp, Log, TEXT("PadTextureInPlace: %s — SrcDims=%dx%d Grid=%dx%d SrcCell=%dx%d TargetCell=%dx%d GroundPlane=%d"),
		*Texture->GetName(), SrcW, SrcH, Grid.X, Grid.Y, SrcCellW, SrcCellH, TargetCellDims.X, TargetCellDims.Y, GroundPlaneOffset);

	if (SrcCellW >= TargetCellDims.X && SrcCellH >= TargetCellDims.Y)
	{
		UE_LOG(LogTemp, Log, TEXT("PadTextureInPlace: %s — SKIPPED (SrcCell %dx%d >= TargetCell %dx%d)"),
			*Texture->GetName(), SrcCellW, SrcCellH, TargetCellDims.X, TargetCellDims.Y);
		return FIntPoint::ZeroValue;
	}

	const int32 DstCellW = FMath::Max(SrcCellW, TargetCellDims.X);
	const int32 DstCellH = FMath::Max(SrcCellH, TargetCellDims.Y);
	const int32 NewW = Grid.X * DstCellW;
	const int32 NewH = Grid.Y * DstCellH;

	const int32 CellOffsetX = DstCellW / 2 - SrcCellW / 2;

	// Per-cell placement: scan each source cell's content bottom to compute Y offset
	// that places content bottom at DstCellH - GroundPlaneOffset.
	struct FCellPlacement { int32 SrcX, SrcY, DstX, DstY; };
	TArray<FCellPlacement> Placements;
	Placements.Reserve(Grid.X * Grid.Y);

	for (int32 Row = 0; Row < Grid.Y; Row++)
	{
		for (int32 Col = 0; Col < Grid.X; Col++)
		{
			const int32 CellX0 = Col * SrcCellW;
			const int32 CellY0 = Row * SrcCellH;

			int32 SrcContentMaxY = -1;
			for (int32 Y = SrcCellH - 1; Y >= 0 && SrcContentMaxY < 0; Y--)
			{
				for (int32 X = 0; X < SrcCellW; X++)
				{
					if (SrcPixels[(CellY0 + Y) * SrcW + (CellX0 + X)].A > 0)
					{
						SrcContentMaxY = Y;
						break;
					}
				}
			}

			const int32 SrcBPad = (SrcContentMaxY >= 0) ? (SrcCellH - SrcContentMaxY - 1) : 0;
			const int32 CellOffsetY = DstCellH - GroundPlaneOffset - SrcCellH + SrcBPad;

			UE_LOG(LogTemp, Log, TEXT("  Cell[%d,%d] SrcBPad=%d CellOffset=(%d,%d) → DstBottomPad=%d (ground=%d)"),
				Col, Row, SrcBPad, CellOffsetX, CellOffsetY, GroundPlaneOffset, GroundPlaneOffset);

			FCellPlacement P;
			P.SrcX = CellX0;
			P.SrcY = CellY0;
			P.DstX = Col * DstCellW + CellOffsetX;
			P.DstY = Row * DstCellH + CellOffsetY;
			Placements.Add(P);
		}
	}

	UE_LOG(LogTemp, Log, TEXT("PadTextureInPlace: %s — NewDims=%dx%d DstCell=%dx%d GroundPlane=%d"),
		*Texture->GetName(), NewW, NewH, DstCellW, DstCellH, GroundPlaneOffset);

	TArray<FColor> NewPixels;
	NewPixels.SetNumZeroed(NewW * NewH);

	for (const FCellPlacement& P : Placements)
	{
		for (int32 Y = 0; Y < SrcCellH; Y++)
		{
			for (int32 X = 0; X < SrcCellW; X++)
			{
				const int32 DstX = P.DstX + X;
				const int32 DstY = P.DstY + Y;
				if (DstX >= 0 && DstX < NewW && DstY >= 0 && DstY < NewH)
				{
					NewPixels[DstY * NewW + DstX] = SrcPixels[(P.SrcY + Y) * SrcW + (P.SrcX + X)];
				}
			}
		}
	}

	Texture->SetFlags(RF_Transactional);
	Texture->Modify();
	Texture->Source.Init(NewW, NewH, 1, 1, TSF_BGRA8);
	{
		uint8* DestData = Texture->Source.LockMip(0);
		FMemory::Memcpy(DestData, NewPixels.GetData(), NewW * NewH * sizeof(FColor));
		Texture->Source.UnlockMip(0);
	}
	Texture->ForceRebuildPlatformData();
	Texture->ReleaseResource();
	Texture->UpdateResource();
	Texture->MarkPackageDirty();

	return FIntPoint(CellOffsetX, DstCellH - GroundPlaneOffset - SrcCellH);
}

FPadSnapshotManifest FSpriteExtractionUtils::SnapshotTexture(
	UTexture2D* Texture,
	const FGuid& RunGuid,
	const FString& SaveDir,
	TSoftObjectPtr<UPaper2DPlusCharacterProfileAsset> InitiatingProfile)
{
	FPadSnapshotManifest Manifest;
	if (!Texture || !RunGuid.IsValid() || SaveDir.IsEmpty()) return Manifest;

	TArray<FColor> Pixels;
	int32 W = 0, H = 0;
	if (!LoadTextureData(Texture, Pixels, W, H)) return Manifest;

	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	if (!PlatformFile.DirectoryExists(*SaveDir) && !PlatformFile.CreateDirectoryTree(*SaveDir))
	{
		return Manifest;
	}

	Manifest.RunGuid = RunGuid;
	Manifest.TextureGuid = FGuid::NewGuid();
	Manifest.Texture = TSoftObjectPtr<UTexture2D>(Texture);
	Manifest.OriginalDims = FIntPoint(W, H);
	Manifest.Timestamp = FDateTime::UtcNow();
	Manifest.InitiatingProfile = InitiatingProfile;
	Manifest.SidecarPath = FPaths::Combine(SaveDir, FString::Printf(TEXT("%s.bin"), *Manifest.TextureGuid.ToString()));

	// Serialize once, reuse buffer for CRC and disk write.
	TArray<uint8> Bytes;
	Bytes.SetNumUninitialized(Pixels.Num() * sizeof(FColor));
	FMemory::Memcpy(Bytes.GetData(), Pixels.GetData(), Bytes.Num());
	Manifest.BytesCrc32 = FCrc::MemCrc32(Bytes.GetData(), Bytes.Num());

	if (!FFileHelper::SaveArrayToFile(Bytes, *Manifest.SidecarPath)) return FPadSnapshotManifest();

	return Manifest;
}

FCrossSheetAlignmentStats FSpriteExtractionUtils::ApplyCrossSheetAlignment(
	const TArray<UPaper2DPlusCharacterProfileAsset*>& Profiles,
	FIntPoint GroupMaxCellDims,
	const TMap<UTexture2D*, FIntPoint>& PerTexturePasteOffsets)
{
	FCrossSheetAlignmentStats Stats;
	if (GroupMaxCellDims.X <= 0 || GroupMaxCellDims.Y <= 0) return Stats;

	// -------------------------------------------------------------------
	// Phase 1: pre-apply FrameExtractionInfo.SourceOffset fixup for every
	// sprite referencing a texture padded in the current run. The HitboxDelta
	// math in Phase 2 reads SourceOffset in the new coordinate space; without
	// this shift the first-pad delta would be wrong.
	// -------------------------------------------------------------------
	for (UPaper2DPlusCharacterProfileAsset* Profile : Profiles)
	{
		if (!Profile) continue;
		for (FFlipbookProfileEntry& Entry : Profile->Flipbooks)
		{
			UPaperFlipbook* Flipbook = Entry.Identity.Flipbook.LoadSynchronous();
			if (!Flipbook) continue;
			const int32 NumKF = Flipbook->GetNumKeyFrames();
			for (int32 K = 0; K < NumKF; K++)
			{
				const FPaperFlipbookKeyFrame& KF = Flipbook->GetKeyFrameChecked(K);
				if (!KF.Sprite) continue;
				UTexture2D* Tex = Cast<UTexture2D>(KF.Sprite->GetSourceTexture());
				if (!Tex) continue;
				const FIntPoint* Offset = PerTexturePasteOffsets.Find(Tex);
				if (!Offset || *Offset == FIntPoint::ZeroValue) continue;
				if (Entry.CombatData.FrameExtractionInfo.IsValidIndex(K))
				{
					Entry.CombatData.FrameExtractionInfo[K].SourceOffset += *Offset;
				}
			}
		}
	}

	// -------------------------------------------------------------------
	// Phase 2: per-sprite expand bounds + convergence guard + remap.
	// -------------------------------------------------------------------
	for (UPaper2DPlusCharacterProfileAsset* Profile : Profiles)
	{
		if (!Profile) continue;
		bool bProfileTouched = false;
		Profile->Modify();

		for (FFlipbookProfileEntry& Entry : Profile->Flipbooks)
		{
			UPaperFlipbook* Flipbook = Entry.Identity.Flipbook.LoadSynchronous();
			if (!Flipbook) continue;
			const int32 NumKF = Flipbook->GetNumKeyFrames();

			for (int32 K = 0; K < NumKF; K++)
			{
				const FPaperFlipbookKeyFrame& KF = Flipbook->GetKeyFrameChecked(K);
				UPaperSprite* Sprite = KF.Sprite;
				if (!Sprite) continue;
				UTexture2D* Tex = Cast<UTexture2D>(Sprite->GetSourceTexture());
				if (!Tex) continue;

				// Integer rounding matches UniformBoundsWindow.cpp:518 — float comparison
				// on GetSourceUV would produce phantom inequality and defeat R10 convergence.
				const FVector2D UVFloat = Sprite->GetSourceUV();
				const FVector2D DimFloat = Sprite->GetSourceSize();
				const FIntPoint CurrentUV(FMath::RoundToInt(UVFloat.X), FMath::RoundToInt(UVFloat.Y));
				const FIntPoint CurrentDim(FMath::RoundToInt(DimFloat.X), FMath::RoundToInt(DimFloat.Y));
				const FIntRect CurrentRect(CurrentUV, CurrentUV + CurrentDim);

				const FIntPoint TexDims = GetDetectionDimensions(Tex);
				const FIntRect NewBounds = ExpandBoundsToUniform(
					CurrentRect, GroupMaxCellDims, ESpriteAnchor::BottomCenter, TexDims.X, TexDims.Y);

				// HitboxDelta = art-shift inside the sprite as bounds grow.
				// BottomCenter expansion: art shifts right by (newW-oldW)/2 and down by (newH-oldH).
				const FIntPoint HitboxDelta = CurrentRect.Min - NewBounds.Min;

				// Convergence guard (R10 / R16) — no writes when nothing actually changed.
				if (NewBounds == CurrentRect && HitboxDelta == FIntPoint::ZeroValue)
				{
					Stats.SpritesSkipped++;
					continue;
				}

				Sprite->Modify();
				UpdateSpriteSourceRegion(Sprite, Tex, NewBounds, HitboxDelta);
				Sprite->SetPivotMode(ESpritePivotMode::Center_Center, FVector2D::ZeroVector, true);

				// Hitbox + socket remap — reuse UniformBoundsWindow.cpp:571-590 pattern.
				if (Entry.CombatData.Frames.IsValidIndex(K) && HitboxDelta != FIntPoint::ZeroValue)
				{
					FFrameHitboxData& FrameData = Entry.CombatData.Frames[K];
					for (FHitboxData& H : FrameData.Hitboxes) { H.X += HitboxDelta.X; H.Y += HitboxDelta.Y; }
					for (FSocketData& S : FrameData.Sockets) { S.X += HitboxDelta.X; S.Y += HitboxDelta.Y; }
					Stats.HitboxesRemapped += FrameData.Hitboxes.Num();
				}

				// FrameExtractionInfo — unconditional SourceOffset/timestamp update (mirrors
				// UniformBoundsWindow.cpp:599-603). SpriteOffset reset gated on non-zero delta
				// so airborne-frame offsets survive no-op runs.
				if (Entry.CombatData.FrameExtractionInfo.IsValidIndex(K))
				{
					FSpriteExtractionInfo& Info = Entry.CombatData.FrameExtractionInfo[K];
					Info.SourceOffset = NewBounds.Min;
					Info.ExtractionTime = FDateTime::Now();
					if (HitboxDelta != FIntPoint::ZeroValue)
					{
						Info.SpriteOffset = FIntPoint::ZeroValue;
						Info.bHasCustomAlignment = false;
					}
				}

				Sprite->MarkPackageDirty();
				Stats.SpritesUpdated++;
				bProfileTouched = true;
			}
		}

		if (bProfileTouched)
		{
			Profile->MarkPackageDirty();
			Stats.ProfilesTouched++;
		}
	}

	return Stats;
}

bool FSpriteExtractionUtils::RestoreTextureSnapshot(const FPadSnapshotManifest& Manifest)
{
	// Null-check the archive data per audit pattern 11 before any mutation.
	if (!Manifest.IsValid()) return false;

	UTexture2D* Texture = Manifest.Texture.LoadSynchronous();
	if (!Texture) return false;

	TArray<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *Manifest.SidecarPath)) return false;

	// Validate dims + CRC before mutation.
	const int64 ExpectedSize = static_cast<int64>(Manifest.OriginalDims.X) *
		static_cast<int64>(Manifest.OriginalDims.Y) * 4;
	if (Bytes.Num() != ExpectedSize) return false;
	if (FCrc::MemCrc32(Bytes.GetData(), Bytes.Num()) != Manifest.BytesCrc32) return false;

	// Apply to Source with the original dims. Same sequence as PadTextureInPlace.
	Texture->SetFlags(RF_Transactional);
	Texture->Modify();
	Texture->Source.Init(Manifest.OriginalDims.X, Manifest.OriginalDims.Y, 1, 1, TSF_BGRA8);
	{
		uint8* DestData = Texture->Source.LockMip(0);
		FMemory::Memcpy(DestData, Bytes.GetData(), Bytes.Num());
		Texture->Source.UnlockMip(0);
	}
	Texture->ForceRebuildPlatformData();
	Texture->ReleaseResource();
	Texture->UpdateResource();
	Texture->MarkPackageDirty();
	return true;
}
