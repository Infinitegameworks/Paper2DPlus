// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "SpriteExtractionUtils.h"
#include "Paper2DPlusEditorCompat.h" // PAPER2DPLUS_RENAME_TO_TRANSIENT_FLAGS (UE5.8 REN_ForceNoResetLoaders deprecation)
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
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "Modules/ModuleManager.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SpriteExtractionUtils"

/** FSpriteExtractionUtils — Shared sprite detection algorithms: island flood-fill, grid analysis, texture padding, cross-sheet alignment, and asset creation utilities. */

TSharedRef<SWidget> FSpriteExtractionUtils::MakeContentPathPicker(TAttribute<FString> CurrentPath, TFunction<void(const FString&)> OnPathPicked)
{
	// Shared cell holding a weak ref to the combo. Populated after the combo is built so the menu's
	// OnPathSelected lambda (which captures this cell by value) can close the combo on pick — without a
	// dangling capture of a function-local.
	TSharedRef<TWeakPtr<SComboButton>> WeakComboCell = MakeShared<TWeakPtr<SComboButton>>();

	// Editable text box: the typed-entry fallback. Its displayed text tracks CurrentPath (so a picker
	// selection updates it), and committing typed text writes through OnPathPicked too.
	TSharedRef<SEditableTextBox> TextBox = SNew(SEditableTextBox)
		.Text_Lambda([CurrentPath]() { return FText::FromString(CurrentPath.Get()); })
		.HintText(LOCTEXT("PathPickerHint", "/Game/…"))
		.ToolTipText(LOCTEXT("PathPickerTypeTip", "Type or paste a content-browser path, or use the Browse button to pick a folder."))
		.OnTextCommitted_Lambda([OnPathPicked](const FText& InText, ETextCommit::Type)
		{
			OnPathPicked(InText.ToString().TrimStartAndEnd());
		});

	TSharedRef<SComboButton> ComboButton = SNew(SComboButton)
		.ToolTipText(LOCTEXT("PathPickerBrowseTip", "Browse for a content-browser folder"))
		.OnGetMenuContent_Lambda([CurrentPath, OnPathPicked, WeakComboCell]() -> TSharedRef<SWidget>
		{
			FPathPickerConfig Config;
			Config.DefaultPath = CurrentPath.Get();
			Config.bAllowContextMenu = true;
			Config.bAddDefaultPath = true;
			Config.OnPathSelected = FOnPathSelected::CreateLambda([OnPathPicked, WeakComboCell](const FString& Path)
			{
				OnPathPicked(Path);
				if (TSharedPtr<SComboButton> Pinned = WeakComboCell->Pin())
				{
					Pinned->SetIsOpen(false);
				}
			});

			FContentBrowserModule& CBModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
			return SNew(SBox)
				.WidthOverride(320.f)
				.HeightOverride(400.f)
				[
					CBModule.Get().CreatePathPicker(Config)
				];
		})
		.ButtonContent()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("PathPickerBrowse", "Browse…"))
		];

	*WeakComboCell = ComboButton;

	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		.VAlign(VAlign_Center)
		[
			TextBox
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(4, 0, 0, 0)
		.VAlign(VAlign_Center)
		[
			ComboButton
		];
}

FSpriteSheetGrid FSpriteSheetGrid::Compute(int32 FrameCount, int32 InCellW, int32 InCellH, int32 MaxDimension)
{
	FSpriteSheetGrid Grid;
	Grid.CellW = InCellW;
	Grid.CellH = InCellH;
	if (FrameCount <= 0 || InCellW <= 0 || InCellH <= 0 || MaxDimension <= 0)
	{
		return Grid; // bValid stays false
	}

	// Legacy near-square preference: <=16 frames keep a single row, otherwise ceil(sqrt). This is the
	// shape every site used before; reproducing it keeps the layout byte-identical wherever it fit.
	const int32 PreferredCols = (FrameCount <= 16)
		? FrameCount
		: FMath::CeilToInt(FMath::Sqrt(static_cast<float>(FrameCount)));

	// Clamp the column count DOWN so the sheet width can never exceed MaxDimension. For every sheet
	// that already fit at the preferred width this is a no-op (PreferredCols <= MaxDimension/CellW),
	// so the result equals the old formula; it only diverges for the wide sheets the old code rejected.
	const int32 MaxCols = FMath::Max(1, MaxDimension / InCellW);
	int32 Cols = FMath::Clamp(PreferredCols, 1, MaxCols);
	int32 Rows = FMath::DivideAndRoundUp(FrameCount, Cols);

	// If the resulting sheet is too TALL, add columns to shrink the row count (more columns can only
	// reduce rows, and the width clamp above already guarantees any Cols <= MaxCols fits the width).
	// This only triggers for tall/narrow sheets the old downward-only clamp would have rejected even
	// though a wider grid fits, so every layout that already fit stays byte-identical.
	if (static_cast<int64>(Rows) * InCellH > MaxDimension)
	{
		const int32 MaxRows = FMath::Max(1, MaxDimension / InCellH);
		const int32 MinColsForHeight = FMath::DivideAndRoundUp(FrameCount, MaxRows);
		Cols = FMath::Clamp(FMath::Max(Cols, MinColsForHeight), 1, MaxCols);
		Rows = FMath::DivideAndRoundUp(FrameCount, Cols);
	}

	const int64 SheetW64 = static_cast<int64>(Cols) * InCellW;
	const int64 SheetH64 = static_cast<int64>(Rows) * InCellH;
	if (SheetW64 <= 0 || SheetH64 <= 0 || SheetW64 > MaxDimension || SheetH64 > MaxDimension)
	{
		return Grid; // bValid stays false: a single cell (or its forced rows even at max columns) overflows MaxDimension
	}

	Grid.Columns = Cols;
	Grid.Rows = Rows;
	Grid.SheetW = static_cast<int32>(SheetW64);
	Grid.SheetH = static_cast<int32>(SheetH64);
	Grid.bValid = true;
	return Grid;
}

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
	OutPixels.Reset();
	OutWidth = 0;
	OutHeight = 0;
	if (!Texture) return false;

	// Read from source data (editor-only, always uncompressed) instead of platform data
	// which may be compressed (DXT/BC), power-of-2 padded, or not CPU-resident.
	if (Texture->Source.GetNumMips() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("SpriteExtractionUtils: Texture '%s' has no source mips — reimport may be required"),
			*Texture->GetName());
		return false;
	}

	const int32 Width = Texture->Source.GetSizeX();
	const int32 Height = Texture->Source.GetSizeY();

	const int64 PixelCount64 = (int64)Width * Height;
	if (PixelCount64 <= 0 || PixelCount64 > MAX_int32) return false;
	const int32 PixelCount = (int32)PixelCount64;

	// Positive geometry can survive while the compressed source payload is empty. Probe the
	// payload without locking first: on UE 5.1-5.7 the failed lock logs an Error, turning this
	// recoverable validation result into a failed automation run.
	if (Texture->Source.GetSizeOnDisk() <= 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("SpriteExtractionUtils: Texture '%s' has no readable source payload — reimport may be required"),
			*Texture->GetName());
		return false;
	}

	uint8* Data = Texture->Source.LockMip(0);
	if (!Data)
	{
		UE_LOG(LogTemp, Warning, TEXT("SpriteExtractionUtils: Texture '%s' source mip 0 could not be read"),
			*Texture->GetName());
		return false;
	}

	TArray<FColor> Pixels;
	Pixels.SetNum(PixelCount);

	const ETextureSourceFormat Format = Texture->Source.GetFormat();
	if (Format == TSF_BGRA8)
	{
		FMemory::Memcpy(Pixels.GetData(), Data, PixelCount * sizeof(FColor));
	}
	else if (Format == TSF_G8)
	{
		// Grayscale — treat as opaque white with alpha = pixel value
		for (int32 i = 0; i < PixelCount; i++)
		{
			Pixels[i].R = 255;
			Pixels[i].G = 255;
			Pixels[i].B = 255;
			Pixels[i].A = Data[i];
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
	OutPixels = MoveTemp(Pixels);
	OutWidth = Width;
	OutHeight = Height;
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
			Existing->Rename(nullptr, GetTransientPackage(), PAPER2DPLUS_RENAME_TO_TRANSIENT_FLAGS);
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

UTexture2D* FSpriteExtractionUtils::GetOrCreateTextureForName(UPackage* Package, const FString& Name)
{
	if (!Package)
	{
		return nullptr;
	}

	// Reuse an existing same-class texture in place (the caller re-initializes its pixels).
	if (UTexture2D* Existing = FindObject<UTexture2D>(Package, *Name))
	{
		return Existing;
	}

	// Evict any different-class occupant (redirector / stray asset) of that name to the transient
	// package so the explicit-name NewObject below cannot collide inside StaticAllocateObject (U7).
	if (UObject* Occupant = StaticFindObject(UObject::StaticClass(), Package, *Name))
	{
		Occupant->Rename(nullptr, GetTransientPackage(), PAPER2DPLUS_RENAME_TO_TRANSIENT_FLAGS);
	}

	return NewObject<UTexture2D>(Package, *Name, RF_Public | RF_Standalone);
}

TArray<int32> FSpriteExtractionUtils::FilterValidSpriteIndices(const TArray<int32>& Indices, int32 Count)
{
	TArray<int32> Valid;
	Valid.Reserve(Indices.Num());
	for (int32 Idx : Indices)
	{
		if (Idx >= 0 && Idx < Count) // TArray::IsValidIndex semantics, decoupled from a live array
		{
			Valid.Add(Idx);
		}
	}
	return Valid;
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
			Existing->Rename(nullptr, GetTransientPackage(), PAPER2DPLUS_RENAME_TO_TRANSIENT_FLAGS);
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
	const FIntRect ScanBounds(
		FMath::Clamp(CellBounds.Min.X, 0, TexW),
		FMath::Clamp(CellBounds.Min.Y, 0, TexH),
		FMath::Clamp(CellBounds.Max.X, 0, TexW),
		FMath::Clamp(CellBounds.Max.Y, 0, TexH));
	if (ScanBounds.Width() <= 0 || ScanBounds.Height() <= 0)
	{
		return FIntRect(CellBounds.Min, CellBounds.Min);
	}

	int32 MinX = ScanBounds.Max.X, MinY = ScanBounds.Max.Y;
	int32 MaxX = ScanBounds.Min.X - 1, MaxY = ScanBounds.Min.Y - 1;

	for (int32 Y = ScanBounds.Min.Y; Y < ScanBounds.Max.Y; Y++)
	{
		for (int32 X = ScanBounds.Min.X; X < ScanBounds.Max.X; X++)
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

FSpriteMaxExtentResult FSpriteExtractionUtils::ComputeMaxExtentTargets(
	TArray<FSpriteMaxExtentFrame>& Frames)
{
	FSpriteMaxExtentResult Result;

	for (FSpriteMaxExtentFrame& Frame : Frames)
	{
		Frame.bEmpty = Frame.TightBounds.Width() <= 0 || Frame.TightBounds.Height() <= 0;
		Frame.bTargetFits = false;
		Frame.TargetBounds = FIntRect(Frame.Anchor, Frame.Anchor);
		if (Frame.bEmpty)
		{
			++Result.EmptyFrames;
			continue;
		}

		++Result.NonEmptyFrames;
		Result.MaxLeft = FMath::Max(Result.MaxLeft, Frame.Anchor.X - Frame.TightBounds.Min.X);
		Result.MaxRight = FMath::Max(Result.MaxRight, Frame.TightBounds.Max.X - Frame.Anchor.X);
		Result.MaxTop = FMath::Max(Result.MaxTop, Frame.Anchor.Y - Frame.TightBounds.Min.Y);
		Result.MaxBottom = FMath::Max(Result.MaxBottom, Frame.TightBounds.Max.Y - Frame.Anchor.Y);
	}

	if (!Result.IsValid())
	{
		return Result;
	}

	for (FSpriteMaxExtentFrame& Frame : Frames)
	{
		Frame.TargetBounds = FIntRect(
			Frame.Anchor.X - Result.MaxLeft,
			Frame.Anchor.Y - Result.MaxTop,
			Frame.Anchor.X + Result.MaxRight,
			Frame.Anchor.Y + Result.MaxBottom);
		Frame.bTargetFits = Frame.ContainerBounds.Min.X <= Frame.TargetBounds.Min.X
			&& Frame.ContainerBounds.Min.Y <= Frame.TargetBounds.Min.Y
			&& Frame.ContainerBounds.Max.X >= Frame.TargetBounds.Max.X
			&& Frame.ContainerBounds.Max.Y >= Frame.TargetBounds.Max.Y;
		if (!Frame.bTargetFits)
		{
			++Result.FramesOutsideContainers;
		}
	}

	return Result;
}

FIntPoint FSpriteExtractionUtils::ComputePackedContentOffset(
	const FIntRect& SourceRegion,
	const FIntRect& TightContent,
	const FIntRect& PackedRegion)
{
	if (TightContent.Width() <= 0 || TightContent.Height() <= 0)
	{
		return PackedRegion.Min;
	}
	return PackedRegion.Min + (TightContent.Min - SourceRegion.Min);
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
			Existing->Rename(nullptr, GetTransientPackage(), PAPER2DPLUS_RENAME_TO_TRANSIENT_FLAGS);
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

FIntPoint FSpriteExtractionUtils::ComputeUniformPreviewSize(const TArray<FDetectedSprite>& Sprites, int32 TexW, int32 TexH)
{
	if (Sprites.Num() < 2 || TexW <= 0 || TexH <= 0)
	{
		return FIntPoint::ZeroValue;
	}

	// Run the stability-critical algorithm on a COPY so the caller's sprites are untouched
	// (the preview must not mutate canvas selection state). The uniform size is Bounds[0]'s
	// size after ComputeUniformBounds applies the per-row stride + max-extent fit.
	TArray<FDetectedSprite> Copy = Sprites;
	ComputeUniformBounds(Copy, TexW, TexH);
	return Copy[0].GetSize();
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
	const float OldPixelsPerUnrealUnit = FMath::Max(Sprite->GetPixelsPerUnrealUnit(), 0.001f);

	FSpriteAssetInitParameters InitParams;
	InitParams.Texture = NewTexture;
	InitParams.Offset = FIntPoint(NewBounds.Min.X, NewBounds.Min.Y);
	InitParams.Dimension = FIntPoint(NewBounds.Width(), NewBounds.Height());
	InitParams.SetPixelsPerUnrealUnit(OldPixelsPerUnrealUnit);
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
// Frame-grid detection (the gutter rule)
// ==========================================

/*
 * Where the FRAMES are — additive beside island detection, which answers where the ART is.
 *
 * The definitive property of a uniform grid is a GUTTER: every internal frame boundary falls on an
 * empty column. That holds no matter how the art inside a frame is shaped, so it survives scattered
 * particle VFX that shatter into dozens of islands and defeat any spacing-based heuristic.
 *
 *   accept cell width cw  <=>  every boundary column k*cw is empty
 *                        AND   cells are blank only as a capped run at the END
 *
 * The second clause stops over-fragmentation: with a wide gutter, a too-small cell can land entirely
 * inside the gutter and satisfy the first clause alone. It is a capped TRAILING run rather than a
 * flat "every cell must hold something" because VFX sheets are routinely padded with blank frames so
 * their length matches the animation they accompany — rejecting those forces a coarser grid that
 * merges real frames together. Interior blanks still disqualify a grid.
 *
 * Search runs most-frames-first, so the finest grid that still satisfies both wins. Sheets whose art
 * touches the frame edge have no gutter to find; those fall back to the artist's filename label, then
 * to island pitch, and are reported with a lower-confidence Source so they can be eyeballed.
 *
 * Measured against 462 real sprite sheets across two character libraries: 0 sheets sliced through
 * artwork, 108/109 agreement with the artists' own folder labels.
 */
namespace Paper2DPlusFrameGrid
{
	// FDetectedFrameGrid::Source values. Diagnostic strings, but the first three are also the
	// confidence set, so they are named constants rather than scattered literals.
	static const TCHAR* SourceGutter       = TEXT("gutter");
	static const TCHAR* SourceHint         = TEXT("hint");
	static const TCHAR* SourceSibling      = TEXT("sibling");
	static const TCHAR* SourceGutterSparse = TEXT("gutter-sparse");
	static const TCHAR* SourceGutterBleed  = TEXT("gutter-bleed");
	static const TCHAR* SourceHintNoGutter = TEXT("hint-nogutter");
	static const TCHAR* SourcePitch        = TEXT("pitch");
	static const TCHAR* SourceSingle       = TEXT("single");
	static const TCHAR* SourceSingleCell   = TEXT("single-cell");
	static const TCHAR* SourceEmpty        = TEXT("empty");

	/** Every divisor of Size that is at least MinCell, ascending. Size itself is always included,
	 *  which is what keeps the single-frame sheet selectable. */
	static TArray<int32> Divisors(int32 Size, int32 MinCell)
	{
		TArray<int32> Out;
		if (Size <= 0) return Out;
		for (int32 D = FMath::Max(1, MinCell); D <= Size; ++D)
		{
			if (Size % D == 0) Out.Add(D);
		}
		return Out;
	}

	/** Inclusive [Start, End] runs of occupied entries along one axis. */
	static TArray<FIntPoint> Runs(const TArray<bool>& Occupancy)
	{
		TArray<FIntPoint> Out;
		int32 Start = INDEX_NONE;
		for (int32 i = 0; i < Occupancy.Num(); ++i)
		{
			if (Occupancy[i] && Start == INDEX_NONE)
			{
				Start = i;
			}
			else if (!Occupancy[i] && Start != INDEX_NONE)
			{
				Out.Add(FIntPoint(Start, i - 1));
				Start = INDEX_NONE;
			}
		}
		if (Start != INDEX_NONE) Out.Add(FIntPoint(Start, Occupancy.Num() - 1));
		return Out;
	}

	static bool AnyOccupied(const TArray<bool>& Occupancy)
	{
		for (bool bOccupied : Occupancy)
		{
			if (bOccupied) return true;
		}
		return false;
	}

	/** True when no run of content crosses a cell boundary. */
	static bool NoStraddle(const TArray<FIntPoint>& ContentRuns, int32 Cell)
	{
		if (Cell <= 0) return false;
		for (const FIntPoint& Run : ContentRuns)
		{
			if (Run.X / Cell != Run.Y / Cell) return false;
		}
		return true;
	}

	/** THE GUTTER RULE: every internal boundary entry is empty. */
	static bool GutterOk(const TArray<bool>& Occupancy, int32 Cell)
	{
		if (Cell <= 0) return false;
		for (int32 X = Cell; X < Occupancy.Num(); X += Cell)
		{
			if (Occupancy[X]) return false;
		}
		return true;
	}

	/** THE BLEED TIER: a boundary may hold a few stray pixels, on strong evidence.
	 *
	 *  Exact emptiness is too strict for TRIMMED art. A single pixel bleeding across one
	 *  boundary disqualifies the true grid and collapses detection to a far coarser one — a
	 *  2816x128 explosion sheet read 16 frames as 2 because ONE boundary held ONE pixel.
	 *
	 *  Measured across three shipped libraries the populations separate cleanly: a true grid's
	 *  worst boundary holds 0px (p99 = 0, max 6), a too-fine grid's holds 29px at the median and
	 *  up to a full column. But tolerance ALONE over-fires — it split single-frame static poses
	 *  whose art merely has thin vertical gaps. Genuine bleed also shows STRUCTURE: many
	 *  boundaries, nearly all of them perfect. Measured, genuine splits run 10-15 boundaries at
	 *  80-93% perfectly clean; the false positives had 2 boundaries at 50%. Both clauses are
	 *  required, and this tier is only ever consulted when NO clean grid exists (see ResolveAxis),
	 *  so a sheet that already resolves cleanly can never change. */
	static bool GutterBleedOk(const TArray<int32>& Counts, int32 Cell, int32 Height)
	{
		if (Cell <= 0 || Counts.Num() == 0) return false;
		const int32 Tolerance = FMath::Max(2, FMath::FloorToInt(0.03f * Height));
		int32 NumBoundaries = 0;
		int32 NumClean = 0;
		for (int32 X = Cell; X < Counts.Num(); X += Cell)
		{
			++NumBoundaries;
			if (Counts[X] > Tolerance) return false;
			if (Counts[X] == 0) ++NumClean;
		}
		return NumBoundaries >= 4 && NumClean * 4 >= NumBoundaries * 3;   // >= 75% perfect
	}

	/** Indices of whole cells that hold nothing. */
	static TArray<int32> EmptyCells(const TArray<bool>& Occupancy, int32 Cell)
	{
		TArray<int32> Out;
		if (Cell <= 0) return Out;
		const int32 Size = Occupancy.Num();
		const int32 NumCells = Size / Cell;
		for (int32 K = 0; K < NumCells; ++K)
		{
			bool bAny = false;
			const int32 End = FMath::Min((K + 1) * Cell, Size);
			for (int32 X = K * Cell; X < End && !bAny; ++X)
			{
				bAny = Occupancy[X];
			}
			if (!bAny) Out.Add(K);
		}
		return Out;
	}

	/** Cells may be blank only as a run at the END, and only a few.
	 *
	 *  VFX sheets are routinely padded with blank trailing frames so their length matches the
	 *  animation they accompany — rejecting those forces a coarser grid that merges real frames
	 *  together. But an unlimited trailing run would re-admit over-fragmentation (one sprite on the
	 *  left of an otherwise empty sheet), so the run is capped at a quarter of the sheet. */
	static bool PaddingOk(const TArray<bool>& Occupancy, int32 Cell)
	{
		if (Cell <= 0) return false;
		const int32 NumCells = Occupancy.Num() / Cell;
		const TArray<int32> Blanks = EmptyCells(Occupancy, Cell);
		if (Blanks.Num() == 0) return true;

		for (int32 i = 0; i < Blanks.Num(); ++i)
		{
			// Interior blanks are not padding — they mean the grid is too fine.
			if (Blanks[i] != NumCells - Blanks.Num() + i) return false;
		}
		return Blanks.Num() <= FMath::Max(1, NumCells / 4);
	}

	static double Median(TArray<double>& Values)
	{
		if (Values.Num() == 0) return 0.0;
		Values.Sort();
		const int32 Mid = Values.Num() / 2;
		return (Values.Num() % 2 == 1) ? Values[Mid] : 0.5 * (Values[Mid - 1] + Values[Mid]);
	}

	/** Resolve ONE axis to a cell size, reporting which rule decided it.
	 *
	 *  The gutter rule alone is ambiguous in two directions: a sprite with a gap down its middle also
	 *  admits half-width cells, and a sheet containing a blank frame fails the trailing-run clause at
	 *  its true pitch. Both are resolved by EXTERNAL evidence — the artist's label, or what this
	 *  sheet's siblings agreed on — so those get first refusal over the raw search. */
	static int32 ResolveAxis(const TArray<bool>& Occupancy, int32 Hint, int32 MinCell, int32 Prefer, FString& OutSource,
		const TArray<int32>* Counts = nullptr, int32 Height = 0)
	{
		const int32 Size = Occupancy.Num();
		const TArray<int32> Divs = Divisors(Size, MinCell);

		if (Divs.Num() == 0 || !AnyOccupied(Occupancy))
		{
			OutSource = SourceEmpty;
			return (Hint > 0 && Divs.Contains(Hint)) ? Hint : Size;
		}

		// A CLEAN gutter is the strong signal and always wins. The bleed tier is strictly LOWER —
		// consulted only when no clean grid exists — because applying tolerance first regressed 11
		// sheets across two libraries by admitting grids that clip a sword tip or a thin VFX trail.
		// Tiering guarantees a sheet that already resolves cleanly cannot change.
		TArray<int32> Gutter;    // inner divisors whose every boundary entry is empty
		TArray<int32> Strict;    // ... and whose blank cells are only a capped trailing run
		TArray<int32> Bleed;     // boundaries effectively empty (trimmed art), on strong evidence
		TArray<int32> BleedPad;  // ... and whose blank cells are only a capped trailing run
		for (int32 D : Divs)
		{
			if (D >= Size) continue;                 // internal boundaries only
			const bool bClean = GutterOk(Occupancy, D);
			if (bClean)
			{
				Gutter.Add(D);
				if (PaddingOk(Occupancy, D)) Strict.Add(D);
			}
			else if (Counts && GutterBleedOk(*Counts, D, Height))
			{
				Bleed.Add(D);
				if (PaddingOk(Occupancy, D)) BleedPad.Add(D);
			}
		}

		// 1. the artist's own label, when the pixels do not contradict it. Hint == Size is the
		//    single-frame sheet: no internal boundary to test, so it is vacuously consistent and
		//    must stay selectable.
		if (Hint > 0 && (Hint == Size || Gutter.Contains(Hint) || Bleed.Contains(Hint)))
		{
			OutSource = SourceHint;
			return Hint;
		}
		// 2. what this folder's other sheets settled on
		if (Prefer > 0 && (Prefer == Size || Gutter.Contains(Prefer) || Bleed.Contains(Prefer)))
		{
			OutSource = SourceSibling;
			return Prefer;
		}
		// 3. finest grid with a CLEAN gutter, allowing trailing blank frames. Divs is ascending, so
		//    the first survivor is the most-frames answer.
		if (Strict.Num() > 0)
		{
			OutSource = SourceGutter;
			return Strict[0];
		}
		// 4. same, tolerating a few bleed pixels on a boundary (trimmed art)
		if (BleedPad.Num() > 0)
		{
			OutSource = SourceGutterBleed;
			return BleedPad[0];
		}
		// 5. either, tolerating blanks anywhere (low confidence)
		if (Gutter.Num() > 0 || Bleed.Num() > 0)
		{
			OutSource = SourceGutterSparse;
			return Gutter.Num() > 0 ? Gutter[0] : Bleed[0];
		}

		// 5. no gutter anywhere (art touches the frame edge)
		const TArray<FIntPoint> ContentRuns = Runs(Occupancy);
		if (Hint > 0 && Divs.Contains(Hint) && NoStraddle(ContentRuns, Hint))
		{
			OutSource = SourceHintNoGutter;
			return Hint;
		}
		if (ContentRuns.Num() >= 2)
		{
			TArray<double> CentreGaps;
			CentreGaps.Reserve(ContentRuns.Num() - 1);
			for (int32 i = 1; i < ContentRuns.Num(); ++i)
			{
				const double Previous = 0.5 * (ContentRuns[i - 1].X + ContentRuns[i - 1].Y);
				const double Current  = 0.5 * (ContentRuns[i].X + ContentRuns[i].Y);
				CentreGaps.Add(Current - Previous);
			}
			const double Pitch = Median(CentreGaps);

			TArray<int32> ByPitch = Divs;
			ByPitch.Sort([Pitch](int32 A, int32 B)
			{
				// Nearest to the measured pitch, ties to the finer cell — a total order, so the
				// answer does not depend on sort stability.
				const double DistA = FMath::Abs((double)A - Pitch);
				const double DistB = FMath::Abs((double)B - Pitch);
				return (DistA != DistB) ? (DistA < DistB) : (A < B);
			});
			for (int32 Candidate : ByPitch)
			{
				if (NoStraddle(ContentRuns, Candidate))
				{
					OutSource = SourcePitch;
					return Candidate;
				}
			}
		}

		OutSource = SourceSingle;
		return Size;
	}
}

FDetectedFrameGrid FSpriteExtractionUtils::DetectFrameGridFromOccupancy(
	const TArray<bool>& ColumnOccupancy,
	const TArray<bool>& RowOccupancy,
	FIntPoint FilenameHint,
	int32 MinCell,
	int32 PreferCellWidth,
	const TArray<int32>* ColumnCounts)
{
	FDetectedFrameGrid Out;

	const int32 Width = ColumnOccupancy.Num();
	const int32 Height = RowOccupancy.Num();
	if (Width <= 0 || Height <= 0) return Out;

	const int32 EffectiveMinCell = FMath::Max(1, MinCell);

	FString XSource;
	// Counts are only meaningful when they describe THIS sheet's columns.
	const TArray<int32>* Counts =
		(ColumnCounts && ColumnCounts->Num() == Width) ? ColumnCounts : nullptr;
	const int32 CellW = Paper2DPlusFrameGrid::ResolveAxis(
		ColumnOccupancy, FilenameHint.X, EffectiveMinCell, PreferCellWidth, XSource, Counts, Height);

	// Rows: ONLY when the artist's label asks for them. A horizontal gap in artwork is not a row
	// break — a potion VFX with a 2px gap between its upper and lower swirl reads as a perfect
	// 2-row gutter and is not one. A wrong row split silently HALVES every frame, so the burden of
	// proof sits here; anything genuinely multi-row is one override away.
	int32 CellH = Height;
	const int32 HintH = FilenameHint.Y;
	if (HintH > 0 && HintH < Height && (Height % HintH) == 0
		&& Paper2DPlusFrameGrid::NoStraddle(Paper2DPlusFrameGrid::Runs(RowOccupancy), HintH))
	{
		CellH = HintH;
	}

	Out.Cell = FIntPoint(CellW, CellH);
	Out.Grid = FIntPoint(Width / CellW, Height / CellH);
	// Source reports the HORIZONTAL rule — the axis the search actually reasons about, and the one
	// the confidence verdict keys off. The row axis is hint-or-nothing by construction.
	Out.Source = XSource;
	Out.bConfident = (XSource == Paper2DPlusFrameGrid::SourceGutter
		|| XSource == Paper2DPlusFrameGrid::SourceGutterBleed
		|| XSource == Paper2DPlusFrameGrid::SourceHint
		|| XSource == Paper2DPlusFrameGrid::SourceSibling);
	Out.BlankFrames = Paper2DPlusFrameGrid::EmptyCells(ColumnOccupancy, CellW);

	// A blank COLUMN cell is only a blank FRAME on a single-row sheet; on a multi-row sheet the
	// column may still hold content in another row, so the trailing-padding count stays 0.
	const int32 NumCells = Out.Grid.X * Out.Grid.Y;
	if (Out.BlankFrames.Num() > 0 && Out.Grid.Y == 1)
	{
		bool bTrailingRun = true;
		for (int32 i = 0; i < Out.BlankFrames.Num() && bTrailingRun; ++i)
		{
			bTrailingRun = (Out.BlankFrames[i] == NumCells - Out.BlankFrames.Num() + i);
		}
		if (bTrailingRun) Out.TrailingBlanks = Out.BlankFrames.Num();
	}

	return Out;
}

FFrameGridCandidate FSpriteExtractionUtils::MakeFrameGridCandidate(
	UTexture2D* Texture,
	const FString& FolderKey,
	FIntPoint FilenameHint,
	int32 MinCell)
{
	FFrameGridCandidate Out;
	Out.FolderKey = FolderKey;
	Out.FilenameHint = FilenameHint;
	Out.MinCell = FMath::Max(1, MinCell);

	TArray<FColor> Pixels;
	int32 Width = 0, Height = 0;
	if (!LoadTextureData(Texture, Pixels, Width, Height) || Width <= 0 || Height <= 0)
	{
		// Result stays invalid (IsValid() == false); the caller decides what an unreadable sheet
		// means, and ApplyFolderConsensus skips it rather than letting it dilute the vote.
		return Out;
	}

	// One pass, both axes. Occupancy is alpha > 0 — deliberately NOT
	// FSpriteDetectionParams::AlphaThreshold: a boundary must be EMPTY, not merely faint. The COUNT
	// is kept alongside the boolean because the bleed tier needs magnitude, not just presence: on
	// trimmed art a boundary holding one stray pixel is still a boundary, while one holding thirty
	// is artwork, and the boolean cannot tell those apart.
	Out.ColumnOccupancy.SetNumZeroed(Width);
	Out.RowOccupancy.SetNumZeroed(Height);
	Out.ColumnCounts.SetNumZeroed(Width);
	for (int32 Y = 0; Y < Height; ++Y)
	{
		const int32 RowBase = Y * Width;
		for (int32 X = 0; X < Width; ++X)
		{
			if (Pixels[RowBase + X].A > 0)
			{
				Out.ColumnOccupancy[X] = true;
				Out.RowOccupancy[Y] = true;
				++Out.ColumnCounts[X];
			}
		}
	}

	Out.Result = DetectFrameGridFromOccupancy(
		Out.ColumnOccupancy, Out.RowOccupancy, Out.FilenameHint, Out.MinCell, /*PreferCellWidth=*/0,
		&Out.ColumnCounts);

	UE_LOG(LogTemp, Log, TEXT("DetectFrameGrid: %s — Sheet=%dx%d Hint=%dx%d → Cell=%dx%d Grid=%dx%d source=%s confident=%d blanks=%d trailing=%d"),
		Texture ? *Texture->GetName() : TEXT("<null>"), Width, Height, FilenameHint.X, FilenameHint.Y,
		Out.Result.Cell.X, Out.Result.Cell.Y, Out.Result.Grid.X, Out.Result.Grid.Y,
		*Out.Result.Source, Out.Result.bConfident ? 1 : 0,
		Out.Result.BlankFrames.Num(), Out.Result.TrailingBlanks);

	return Out;
}

FDetectedFrameGrid FSpriteExtractionUtils::DetectFrameGrid(
	UTexture2D* Texture,
	FIntPoint FilenameHint,
	int32 MinCell)
{
	return MakeFrameGridCandidate(Texture, FString(), FilenameHint, MinCell).Result;
}

void FSpriteExtractionUtils::ApplyFolderConsensus(TArray<FFrameGridCandidate>& InOutResults)
{
	// Consensus is per FRAME FORMAT (cell width AND height), not per width: a folder of 192x128
	// character sheets also holds little 384x64 VFX strips, and those are a different format that
	// must never inherit the character's frame width.
	struct FFormatTally
	{
		FIntPoint Format = FIntPoint::ZeroValue;
		int32 Count = 0;
	};

	TMap<FString, TArray<FFormatTally>> ByFolder;
	for (const FFrameGridCandidate& Candidate : InOutResults)
	{
		if (!Candidate.Result.IsValid()) continue;   // unreadable sheet: no vote, and no denominator
		TArray<FFormatTally>& Tallies = ByFolder.FindOrAdd(Candidate.FolderKey);
		if (FFormatTally* Existing = Tallies.FindByPredicate(
			[&Candidate](const FFormatTally& Tally) { return Tally.Format == Candidate.Result.Cell; }))
		{
			Existing->Count++;
		}
		else
		{
			FFormatTally Added;
			Added.Format = Candidate.Result.Cell;
			Added.Count = 1;
			Tallies.Add(Added);
		}
	}

	TMap<FString, FIntPoint> Dominant;
	for (const TPair<FString, TArray<FFormatTally>>& Folder : ByFolder)
	{
		int32 Total = 0;
		for (const FFormatTally& Tally : Folder.Value) Total += Tally.Count;

		const FFormatTally* Best = nullptr;
		for (const FFormatTally& Tally : Folder.Value)
		{
			// Ties break toward the LARGER format so the choice is deterministic regardless of the
			// order the batch was assembled in.
			const bool bBetter = !Best
				|| Tally.Count > Best->Count
				|| (Tally.Count == Best->Count
					&& (Tally.Format.X > Best->Format.X
						|| (Tally.Format.X == Best->Format.X && Tally.Format.Y > Best->Format.Y)));
			if (bBetter) Best = &Tally;
		}

		// A folder must actually agree before it may speak for its odd sheets: 3+ readable sheets
		// and a strict majority.
		if (Best && Total >= 3 && Best->Count * 2 > Total)
		{
			Dominant.Add(Folder.Key, Best->Format);
		}
	}

	// TWIN RULE. Same folder AND identical sheet dimensions is near-conclusive evidence of a shared
	// layout — far stronger than a folder-wide majority, and it fires where that majority cannot. A
	// VFX folder held two 2816x128 explosion sheets where only one kept a clean gutter; the folder
	// had no dominant format at all (six sheets, five formats), so consensus stayed silent and the
	// bleed-damaged twin was left mis-detected beside its obvious sibling. Twins are consulted
	// BEFORE the folder majority, and like it they only ever PREFER a width — the sheet's own pixels
	// still have to admit it.
	TMap<FString, int32> TwinPreferred;   // candidate key (folder + dims) -> agreed cell width
	{
		TMap<FString, TArray<const FFrameGridCandidate*>> ByDims;
		for (const FFrameGridCandidate& Candidate : InOutResults)
		{
			if (!Candidate.Result.IsValid()) continue;
			const FString Key = FString::Printf(TEXT("%s|%dx%d"), *Candidate.FolderKey,
				Candidate.ColumnOccupancy.Num(), Candidate.RowOccupancy.Num());
			ByDims.FindOrAdd(Key).Add(&Candidate);
		}
		for (const TPair<FString, TArray<const FFrameGridCandidate*>>& Group : ByDims)
		{
			if (Group.Value.Num() < 2) continue;
			// Only CONFIDENT twins get a vote, and they must all agree, or the group says nothing.
			int32 Agreed = 0;
			bool bConflict = false;
			for (const FFrameGridCandidate* Candidate : Group.Value)
			{
				if (!Candidate->Result.bConfident) continue;
				if (Agreed == 0) Agreed = Candidate->Result.Cell.X;
				else if (Agreed != Candidate->Result.Cell.X) { bConflict = true; break; }
			}
			if (!bConflict && Agreed > 0)
			{
				TwinPreferred.Add(Group.Key, Agreed);
			}
		}
	}

	for (FFrameGridCandidate& Candidate : InOutResults)
	{
		if (!Candidate.Result.IsValid()) continue;

		// Twins first: identical dimensions in the same folder outrank a folder-wide majority.
		const FString TwinKey = FString::Printf(TEXT("%s|%dx%d"), *Candidate.FolderKey,
			Candidate.ColumnOccupancy.Num(), Candidate.RowOccupancy.Num());
		if (const int32* TwinWidth = TwinPreferred.Find(TwinKey))
		{
			if (Candidate.Result.Cell.X != *TwinWidth)
			{
				const FDetectedFrameGrid Rescued = DetectFrameGridFromOccupancy(
					Candidate.ColumnOccupancy, Candidate.RowOccupancy,
					Candidate.FilenameHint, Candidate.MinCell, /*PreferCellWidth=*/*TwinWidth,
					&Candidate.ColumnCounts);
				if (Rescued.Cell.X == *TwinWidth)
				{
					Candidate.Result = Rescued;
				}
			}
		}

		const FIntPoint* Dom = Dominant.Find(Candidate.FolderKey);
		if (!Dom) continue;

		// Same format only: the sheet must be exactly one dominant cell tall AND have settled on that
		// same cell height itself. This is what stops a 64-tall VFX strip inheriting a 128-tall grid.
		const bool bSameFormat = (Candidate.RowOccupancy.Num() == Dom->Y) && (Candidate.Result.Cell.Y == Dom->Y);
		if (bSameFormat && Candidate.Result.Cell.X != Dom->X)
		{
			// Consensus may break a tie but must never impose a cut through artwork: re-run this
			// sheet's OWN pixels with the folder width merely PREFERRED, and adopt the answer only
			// if those pixels admit it.
			const FDetectedFrameGrid Rescued = DetectFrameGridFromOccupancy(
				Candidate.ColumnOccupancy, Candidate.RowOccupancy,
				Candidate.FilenameHint, Candidate.MinCell, /*PreferCellWidth=*/Dom->X,
				&Candidate.ColumnCounts);
			if (Rescued.Cell.X == Dom->X)
			{
				Candidate.Result = Rescued;
			}
		}

		// A sheet exactly one dominant cell wide is a single frame, and there is no gutter to find
		// because there is no internal boundary. That is a confident answer, not an unresolved one.
		if (Candidate.Result.Grid.X == 1 && Candidate.Result.Cell == *Dom
			&& (Candidate.Result.Source == Paper2DPlusFrameGrid::SourceSingle
				|| Candidate.Result.Source == Paper2DPlusFrameGrid::SourceEmpty))
		{
			Candidate.Result.Source = Paper2DPlusFrameGrid::SourceSingleCell;
			Candidate.Result.bConfident = true;
		}
	}
}

FIntPoint FSpriteExtractionUtils::ParseFrameSizeHint(const FString& NameOrPath)
{
	// "Foo_192x128", "Foo_192x128.png", "Chars/192X128/Sheet" — the LAST plausible match wins,
	// because artists append the size and a path may carry unrelated digits earlier.
	//
	// Deliberately conservative: this decides whether the string carries a hint AT ALL, while
	// DetectFrameGrid decides whether the pixels agree with it. A bogus pair harvested from an id
	// ("Idle2x3") would otherwise reach the row rule, where a wrong split silently halves every
	// frame — so both dimensions must be plausible frame sizes here.
	static const int32 MinPlausibleDimension = 8;
	static const int32 MaxDigits = 5;

	const TCHAR* Chars = *NameOrPath;
	const int32 Len = NameOrPath.Len();
	FIntPoint Best = FIntPoint::ZeroValue;

	for (int32 i = 0; i < Len; ++i)
	{
		if (Chars[i] != TEXT('x') && Chars[i] != TEXT('X')) continue;

		int32 LeftStart = i;
		while (LeftStart > 0 && FChar::IsDigit(Chars[LeftStart - 1])) --LeftStart;
		const int32 LeftLen = i - LeftStart;

		int32 RightEnd = i + 1;
		while (RightEnd < Len && FChar::IsDigit(Chars[RightEnd])) ++RightEnd;
		const int32 RightLen = RightEnd - (i + 1);

		if (LeftLen <= 0 || LeftLen > MaxDigits || RightLen <= 0 || RightLen > MaxDigits) continue;

		const int32 HintW = FCString::Atoi(*NameOrPath.Mid(LeftStart, LeftLen));
		const int32 HintH = FCString::Atoi(*NameOrPath.Mid(i + 1, RightLen));
		if (HintW >= MinPlausibleDimension && HintH >= MinPlausibleDimension
			&& HintW <= FSpriteSheetGrid::DefaultMaxDimension
			&& HintH <= FSpriteSheetGrid::DefaultMaxDimension)
		{
			Best = FIntPoint(HintW, HintH);
		}
	}

	return Best;
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

bool FSpriteExtractionUtils::AreCellSizesUniform(const TMap<UTexture2D*, FIntPoint>& PerTextureCellSize)
{
	if (PerTextureCellSize.Num() == 0)
	{
		return false;
	}
	const FIntPoint Max = ComputeGroupMaxCellSize(PerTextureCellSize);
	if (Max.X <= 0 || Max.Y <= 0)
	{
		return false;
	}
	for (const TPair<UTexture2D*, FIntPoint>& Entry : PerTextureCellSize)
	{
		if (Entry.Value.X <= 0 || Entry.Value.Y <= 0 || Entry.Value != Max)
		{
			return false;
		}
	}
	return true;
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
	int32 GroundPlaneOffset,
	TArray<FIntPoint>* OutPerCellPasteDeltas)
{
	if (OutPerCellPasteDeltas) OutPerCellPasteDeltas->Reset();
	if (!Texture || TargetCellDims.X <= 0 || TargetCellDims.Y <= 0) return FIntPoint::ZeroValue;
	if (Grid.X <= 0 || Grid.Y <= 0) return FIntPoint::ZeroValue;

	TArray<FColor> SrcPixels;
	int32 SrcW = 0, SrcH = 0;
	if (!LoadTextureData(Texture, SrcPixels, SrcW, SrcH)) return FIntPoint::ZeroValue;

	const int32 SrcCellW = SrcW / Grid.X;
	const int32 SrcCellH = SrcH / Grid.Y;

	UE_LOG(LogTemp, Log, TEXT("PadTextureInPlace: %s — SrcDims=%dx%d Grid=%dx%d SrcCell=%dx%d TargetCell=%dx%d"),
		*Texture->GetName(), SrcW, SrcH, Grid.X, Grid.Y, SrcCellW, SrcCellH, TargetCellDims.X, TargetCellDims.Y);

	if (GroundPlaneOffset != 0)
	{
		// The ground-plane anchor is RETIRED: it collapsed to "content bottom at
		// DstCellH - GroundPlaneOffset - 1" for every cell, which deleted each animation's vertical
		// motion. The parameter survives for source compatibility only and is never read.
		UE_LOG(LogTemp, Log, TEXT("PadTextureInPlace: %s — ignoring deprecated GroundPlaneOffset=%d (cells are midpoint-centred)"),
			*Texture->GetName(), GroundPlaneOffset);
	}

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

	// MIDPOINT-TO-MIDPOINT: the source cell's centre lands on the destination cell's centre, so every
	// cell of the sheet shares ONE offset and each frame keeps its own vertical motion. Anchoring the
	// Y axis on per-cell CONTENT (the retired ground-plane rule) collapsed every cell's content bottom
	// onto a single line and deleted that motion.
	//
	// No clip check is needed: SrcCell <= DstCell on both axes (the FMath::Max above), so
	// CellOffset >= 0 and CellOffset + SrcCell = DstCell/2 + ceil(SrcCell/2) <= DstCell — the whole
	// source cell provably lands inside its destination cell.
	const int32 CellOffsetX = DstCellW / 2 - SrcCellW / 2;
	const int32 CellOffsetY = DstCellH / 2 - SrcCellH / 2;

	// Per-cell placement. The offset is content-independent, but the PASTE DELTA still varies per
	// cell because it also carries the accumulated growth of the preceding columns/rows:
	//   DeltaX = Col * (DstCellW - SrcCellW) + CellOffsetX
	//   DeltaY = Row * (DstCellH - SrcCellH) + CellOffsetY
	// Row-major push order is a load-bearing contract — ApplyCrossSheetAlignment indexes the array
	// as Row * GridColumns + Column.
	struct FCellPlacement
	{
		int32 SrcX = 0;
		int32 SrcY = 0;
		int32 DstX = 0;
		int32 DstY = 0;
		FIntPoint PasteDelta = FIntPoint::ZeroValue;
	};
	TArray<FCellPlacement> Placements;
	Placements.Reserve(Grid.X * Grid.Y);

	for (int32 Row = 0; Row < Grid.Y; Row++)
	{
		for (int32 Col = 0; Col < Grid.X; Col++)
		{
			FCellPlacement P;
			P.SrcX = Col * SrcCellW;
			P.SrcY = Row * SrcCellH;
			P.DstX = Col * DstCellW + CellOffsetX;
			P.DstY = Row * DstCellH + CellOffsetY;
			P.PasteDelta = FIntPoint(P.DstX - P.SrcX, P.DstY - P.SrcY);
			Placements.Add(P);
		}
	}

	UE_LOG(LogTemp, Log, TEXT("PadTextureInPlace: %s — NewDims=%dx%d DstCell=%dx%d CellOffset=(%d,%d) shared by all %d cells"),
		*Texture->GetName(), NewW, NewH, DstCellW, DstCellH, CellOffsetX, CellOffsetY, Placements.Num());

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

	if (OutPerCellPasteDeltas)
	{
		OutPerCellPasteDeltas->Reserve(Placements.Num());
		for (const FCellPlacement& Placement : Placements)
		{
			OutPerCellPasteDeltas->Add(Placement.PasteDelta);
		}
	}
	return Placements.Num() > 0 ? Placements[0].PasteDelta : FIntPoint::ZeroValue;
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
	const TMap<UTexture2D*, FIntPoint>& PerTextureSourceCellSizes,
	const TMap<UTexture2D*, TArray<FIntPoint>>& PerTexturePasteOffsets,
	const TMap<UPaper2DPlusCharacterProfileAsset*, int32>& MaxEntriesByProfile)
{
	FCrossSheetAlignmentStats Stats;
	if (GroupMaxCellDims.X <= 0 || GroupMaxCellDims.Y <= 0) return Stats;

	// OutDestCellOrigin is the top-left of the sprite's own DESTINATION cell in the padded texture.
	// It is derived from the same grid the delta is indexed against, so it is exact by construction —
	// no re-derivation from the content rect, and therefore no integer-rounding parity dependence.
	auto ResolvePasteDelta = [&PerTextureSourceCellSizes, &PerTexturePasteOffsets, GroupMaxCellDims](
		UTexture2D* Texture,
		const FIntRect& OldSourceRect,
		FIntPoint& OutDelta,
		FIntPoint& OutDestCellOrigin) -> bool
	{
		OutDelta = FIntPoint::ZeroValue;
		OutDestCellOrigin = FIntPoint::ZeroValue;
		const TArray<FIntPoint>* Offsets = PerTexturePasteOffsets.Find(Texture);
		if (!Offsets)
		{
			return false;
		}

		const FIntPoint* SourceCell = PerTextureSourceCellSizes.Find(Texture);
		if (!SourceCell || SourceCell->X <= 0 || SourceCell->Y <= 0)
		{
			return false;
		}

		const FIntPoint TextureDims = GetDetectionDimensions(Texture);
		const int32 GridColumns = TextureDims.X / GroupMaxCellDims.X;
		const int32 GridRows = TextureDims.Y / GroupMaxCellDims.Y;
		if (GridColumns <= 0 || GridRows <= 0 || Offsets->Num() != GridColumns * GridRows)
		{
			return false;
		}

		if (OldSourceRect.Width() <= 0 || OldSourceRect.Height() <= 0)
		{
			return false;
		}
		const FIntPoint SourceCellPoint(
			OldSourceRect.Min.X + (OldSourceRect.Width() - 1) / 2,
			OldSourceRect.Min.Y + (OldSourceRect.Height() - 1) / 2);
		if (SourceCellPoint.X < 0 || SourceCellPoint.Y < 0)
		{
			return false;
		}
		const int32 Column = SourceCellPoint.X / SourceCell->X;
		const int32 Row = SourceCellPoint.Y / SourceCell->Y;
		const int32 CellIndex = Row * GridColumns + Column;
		if (Column < 0 || Column >= GridColumns || Row < 0 || Row >= GridRows
			|| !Offsets->IsValidIndex(CellIndex))
		{
			return false;
		}

		OutDelta = (*Offsets)[CellIndex];
		// Destination cells tile the padded texture on the SAME grid the delta was indexed against,
		// so Column/Row are already bounded by GridColumns/GridRows and the cell provably fits.
		OutDestCellOrigin = FIntPoint(Column * GroupMaxCellDims.X, Row * GroupMaxCellDims.Y);
		return true;
	};

	auto GetEntryCount = [&MaxEntriesByProfile](UPaper2DPlusCharacterProfileAsset* Profile) -> int32
	{
		const int32* Limit = MaxEntriesByProfile.Find(Profile);
		return Limit ? FMath::Min(Profile->Flipbooks.Num(), FMath::Max(0, *Limit)) : Profile->Flipbooks.Num();
	};

	// Capture source geometry before any shared sprite is mutated. The same sprite asset can appear in
	// more than one Profile; every Profile's metadata must migrate, but the sprite itself moves once.
	TMap<UPaperSprite*, FIntRect> OriginalSpriteRects;
	TMap<UPaperSprite*, FIntPoint> SpritePasteDeltas;
	/** Top-left of each resolved sprite's destination cell in the padded texture — the ONLY origin
	 *  Phase 2 expands against. Cached beside the delta so both come from one grid resolution. */
	TMap<UPaperSprite*, FIntPoint> SpriteDestCellOrigins;
	TSet<UPaperSprite*> UnresolvedSprites;

	// -------------------------------------------------------------------
	// Phase 1: pre-apply FrameExtractionInfo.SourceOffset fixup for every
	// sprite referencing a texture padded in the current run. The HitboxDelta
	// math in Phase 2 reads SourceOffset in the new coordinate space; without
	// this shift the first-pad delta would be wrong.
	// -------------------------------------------------------------------
	for (UPaper2DPlusCharacterProfileAsset* Profile : Profiles)
	{
		if (!Profile) continue;
		// SourceOffset writes below must be captured by the caller's surrounding transaction.
		Profile->Modify();
		const int32 EntryCount = GetEntryCount(Profile);
		for (int32 EntryIndex = 0; EntryIndex < EntryCount; ++EntryIndex)
		{
			FFlipbookProfileEntry& Entry = Profile->Flipbooks[EntryIndex];
			UPaperFlipbook* Flipbook = Entry.Identity.Flipbook.LoadSynchronous();
			if (!Flipbook) continue;
			const int32 NumKF = Flipbook->GetNumKeyFrames();
			for (int32 K = 0; K < NumKF; K++)
			{
				const FPaperFlipbookKeyFrame& KF = Flipbook->GetKeyFrameChecked(K);
				if (!KF.Sprite) continue;
				UTexture2D* Tex = Cast<UTexture2D>(KF.Sprite->GetSourceTexture());
				if (!Tex || !PerTextureSourceCellSizes.Contains(Tex)) continue;
				FIntRect OldSourceRect;
				FIntPoint Offset;
				if (const FIntRect* CachedRect = OriginalSpriteRects.Find(KF.Sprite))
				{
					OldSourceRect = *CachedRect;
					Offset = SpritePasteDeltas.FindRef(KF.Sprite);
				}
				else
				{
					const FVector2D SourceUV = KF.Sprite->GetSourceUV();
					const FVector2D SourceSize = KF.Sprite->GetSourceSize();
					const FIntPoint SourceMin(FMath::RoundToInt(SourceUV.X), FMath::RoundToInt(SourceUV.Y));
					OldSourceRect = FIntRect(
						SourceMin,
						SourceMin + FIntPoint(FMath::RoundToInt(SourceSize.X), FMath::RoundToInt(SourceSize.Y)));
					FIntPoint DestCellOrigin = FIntPoint::ZeroValue;
					if (!ResolvePasteDelta(Tex, OldSourceRect, Offset, DestCellOrigin))
					{
						UnresolvedSprites.Add(KF.Sprite);
					}
					else
					{
						OriginalSpriteRects.Add(KF.Sprite, OldSourceRect);
						SpritePasteDeltas.Add(KF.Sprite, Offset);
						SpriteDestCellOrigins.Add(KF.Sprite, DestCellOrigin);
					}
				}
				if (UnresolvedSprites.Contains(KF.Sprite))
				{
					UE_LOG(LogTemp, Warning,
						TEXT("ApplyCrossSheetAlignment: could not resolve %s frame %d to its original source cell; leaving it unchanged."),
						*KF.Sprite->GetName(), K);
					continue;
				}
				if (Offset == FIntPoint::ZeroValue) continue;
				if (Entry.CombatData.FrameExtractionInfo.IsValidIndex(K))
				{
					Entry.CombatData.FrameExtractionInfo[K].SourceOffset += Offset;
				}
			}
		}
	}

	// -------------------------------------------------------------------
	// Phase 2: per-sprite expand bounds + convergence guard + remap.
	// -------------------------------------------------------------------
	TSet<UPaperSprite*> UpdatedSprites;
	for (UPaper2DPlusCharacterProfileAsset* Profile : Profiles)
	{
		if (!Profile) continue;
		bool bProfileTouched = false;
		Profile->Modify();

		const int32 EntryCount = GetEntryCount(Profile);
		for (int32 EntryIndex = 0; EntryIndex < EntryCount; ++EntryIndex)
		{
			FFlipbookProfileEntry& Entry = Profile->Flipbooks[EntryIndex];
			UPaperFlipbook* Flipbook = Entry.Identity.Flipbook.LoadSynchronous();
			if (!Flipbook) continue;
			const int32 NumKF = Flipbook->GetNumKeyFrames();

			for (int32 K = 0; K < NumKF; K++)
			{
				const FPaperFlipbookKeyFrame& KF = Flipbook->GetKeyFrameChecked(K);
				UPaperSprite* Sprite = KF.Sprite;
				if (!Sprite) continue;
				UTexture2D* Tex = Cast<UTexture2D>(Sprite->GetSourceTexture());
				if (!Tex || !PerTextureSourceCellSizes.Contains(Tex)) continue;

				// Integer rounding matches UniformBoundsWindow.cpp:518 — float comparison
				// on GetSourceUV would produce phantom inequality and defeat R10 convergence.
				const FIntRect* OriginalRect = OriginalSpriteRects.Find(Sprite);
				const FIntPoint* ResolvedDelta = SpritePasteDeltas.Find(Sprite);
				const FIntPoint* DestCellOrigin = SpriteDestCellOrigins.Find(Sprite);
				if (!OriginalRect || !ResolvedDelta || !DestCellOrigin || UnresolvedSprites.Contains(Sprite))
				{
					Stats.SpritesSkipped++;
					continue;
				}
				const FIntRect CurrentRect = *OriginalRect;
				const FIntPoint TexturePasteDelta = *ResolvedDelta;
				const FIntRect CurrentRectInPaddedTexture(
					CurrentRect.Min + TexturePasteDelta,
					CurrentRect.Max + TexturePasteDelta);

				// THE DESTINATION CELL IS THE NEW REGION — never an anchor re-derived from the content
				// rect. Two things break the moment the origin is recovered from the rect instead:
				//   * PARITY. The pad places a cell at CellOffset = D/2 - S/2 (two independent floors)
				//     while an anchor recovers it as paddedMin - (D-S)/2 (one floor). Those disagree by
				//     1px whenever D is even and S is odd (e.g. 33 -> 40), so every cell except the one
				//     the texture clamp happens to rescue lands one pixel off its own cell — exactly the
				//     per-frame drift this alignment exists to remove.
				//   * TRIMMED RECTS. ApplyCrossSheetAlignment runs over every Profile that references the
				//     texture, including sprites the single-sheet extractor cut with island detection,
				//     whose rect is TIGHT rather than the full source cell. Centring such a rect anchors
				//     on the trimmed BOX CENTRE — the anchor measured to differ from the cell midpoint on
				//     96% of character sheets (up to 16px) — and re-centres every frame on its own
				//     content, deleting the frame-to-frame motion the midpoint pad just preserved.
				// Taking the cell straight from the resolved grid is exact for both: the region is the
				// cell, and HitboxDelta below carries the content's real position inside it.
				const FIntRect NewBounds(*DestCellOrigin, *DestCellOrigin + GroupMaxCellDims);

				// HitboxDelta = art-shift inside the sprite as bounds grow.
				// The current rect is first translated into the padded texture's coordinate space, then
				// expanded. This keeps later cells tied to their own placement instead of frame zero's.
				const FIntPoint HitboxDelta = CurrentRectInPaddedTexture.Min - NewBounds.Min;

				// Convergence guard (R10 / R16) — no writes when nothing actually changed.
				if (NewBounds == CurrentRect && HitboxDelta == FIntPoint::ZeroValue)
				{
					Stats.SpritesSkipped++;
					continue;
				}

				if (!UpdatedSprites.Contains(Sprite))
				{
					Sprite->Modify();
					UpdateSpriteSourceRegion(Sprite, Tex, NewBounds, HitboxDelta);
					Sprite->MarkPackageDirty();
					UpdatedSprites.Add(Sprite);
					Stats.SpritesUpdated++;
				}

				// Hitbox + socket remap — reuse UniformBoundsWindow.cpp:571-590 pattern.
				if (Entry.CombatData.Frames.IsValidIndex(K) && HitboxDelta != FIntPoint::ZeroValue)
				{
					FFrameHitboxData& FrameData = Entry.CombatData.Frames[K];
					for (FHitboxData& H : FrameData.Hitboxes) { H.X += HitboxDelta.X; H.Y += HitboxDelta.Y; }
					for (FSocketData& S : FrameData.Sockets) { S.X += HitboxDelta.X; S.Y += HitboxDelta.Y; }
					Stats.HitboxesRemapped += FrameData.Hitboxes.Num();
				}

				// Phase 1 already moved SourceOffset into the padded texture's coordinate space. Keep
				// both Profile-owned alignment fields intact; auto-pad must never erase authored nudges.
				if (Entry.CombatData.FrameExtractionInfo.IsValidIndex(K))
				{
					FSpriteExtractionInfo& Info = Entry.CombatData.FrameExtractionInfo[K];
					Info.ExtractionTime = FDateTime::Now();
					Info.CachedPivotLocal = Sprite->GetPivotPosition() - Sprite->GetSourceUV();
				}

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

#undef LOCTEXT_NAMESPACE
