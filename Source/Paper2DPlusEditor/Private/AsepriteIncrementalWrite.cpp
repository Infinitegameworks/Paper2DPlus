// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "AsepriteIncrementalWrite.h"

#include "AsepriteImporter.h" // FAsepriteImportCostScope — the decision audit rides the cost report
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Engine/Texture2D.h"
#include "Misc/SecureHash.h"
#include "Modules/ModuleManager.h"

namespace
{
	/** Record + log one decision so no caller can skip silently (R10). Skips for the big asset
	 *  classes log at Log level; everything lands in the audit list the U8 report publishes. */
	FAseWriteDecision AseIncremental_Finish(
		const TCHAR* AssetClass, const FString& AssetName, FAseWriteDecision Decision,
		const bool bLogSkipAtLogLevel = true)
	{
		const TCHAR* VerdictText = TEXT("write");
		switch (Decision.Verdict)
		{
		case EAseWriteVerdict::Create:             VerdictText = TEXT("create"); break;
		case EAseWriteVerdict::WriteGridChanged:   VerdictText = TEXT("write (grid changed)"); break;
		case EAseWriteVerdict::WriteInputsChanged: VerdictText = TEXT("write (inputs changed)"); break;
		case EAseWriteVerdict::WriteForced:        VerdictText = TEXT("write (forced)"); break;
		case EAseWriteVerdict::SkipPayload:        VerdictText = TEXT("skip"); break;
		default: break;
		}

		if (FAsepriteImportCostReport* Report = FAsepriteImportCostScope::GetActive())
		{
			Report->DecisionLines.Add(FString::Printf(
				TEXT("%s %s: %s — %s"), AssetClass, *AssetName, VerdictText, *Decision.Reason));
		}

		if (Decision.Verdict == EAseWriteVerdict::SkipPayload)
		{
			if (bLogSkipAtLogLevel)
			{
				UE_LOG(LogTemp, Log, TEXT("Aseprite incremental: skipping %s %s — %s"),
					AssetClass, *AssetName, *Decision.Reason);
			}
			else
			{
				UE_LOG(LogTemp, Verbose, TEXT("Aseprite incremental: skipping %s %s — %s"),
					AssetClass, *AssetName, *Decision.Reason);
			}
		}
		return Decision;
	}
}

bool FAsepriteIncrementalWrite::RegistryHasPackage(const FString& LongPackageName)
{
	if (LongPackageName.IsEmpty())
	{
		return false;
	}
	IAssetRegistry& AssetRegistry =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	TArray<FAssetData> Assets;
	AssetRegistry.GetAssetsByPackageName(FName(*LongPackageName), Assets);
	return Assets.Num() > 0;
}

FAseWriteDecision FAsepriteIncrementalWrite::ShouldWriteSheet(
	const FString& SheetPackageName,
	const FString& StoredContentHash,
	const FString& NewContentHash,
	const FAseStampedGrid& StampedGrid,
	const FAseStampedGrid& CurrentGrid,
	const bool bForceFullReimport)
{
	FAseWriteDecision Decision;
	if (bForceFullReimport)
	{
		Decision.Verdict = EAseWriteVerdict::WriteForced;
		Decision.Reason = TEXT("Force Full Reimport bypasses the content gates");
	}
	else if (!RegistryHasPackage(SheetPackageName))
	{
		Decision.Verdict = EAseWriteVerdict::Create;
		Decision.Reason = TEXT("no package in the Asset Registry");
	}
	else if (!StampedGrid.Matches(CurrentGrid))
	{
		Decision.Verdict = EAseWriteVerdict::WriteGridChanged;
		Decision.Reason = FString::Printf(TEXT("grid changed (stamped %s, now %s)"),
			StampedGrid.IsStamped() ? *StampedGrid.ToString() : TEXT("nothing"),
			*CurrentGrid.ToString());
	}
	else if (StoredContentHash.IsEmpty())
	{
		Decision.Verdict = EAseWriteVerdict::WriteInputsChanged;
		Decision.Reason = TEXT("no stored content stamp (imported before the incremental gate)");
	}
	else if (!StoredContentHash.Equals(NewContentHash, ESearchCase::IgnoreCase))
	{
		Decision.Verdict = EAseWriteVerdict::WriteInputsChanged;
		Decision.Reason = TEXT("composited pixels changed (content hash differs)");
	}
	else
	{
		Decision.Verdict = EAseWriteVerdict::SkipPayload;
		Decision.Reason = TEXT("pixels unchanged (content hash matches) and the package exists");
	}
	return AseIncremental_Finish(TEXT("sheet"), SheetPackageName, MoveTemp(Decision));
}

FAseWriteDecision FAsepriteIncrementalWrite::ShouldWriteFlipbook(
	const FString& FlipbookPackageName,
	const FString& StoredStructureHash,
	const FString& NewStructureHash,
	const bool bForceFullReimport)
{
	FAseWriteDecision Decision;
	if (bForceFullReimport)
	{
		Decision.Verdict = EAseWriteVerdict::WriteForced;
		Decision.Reason = TEXT("Force Full Reimport bypasses the content gates");
	}
	else if (!RegistryHasPackage(FlipbookPackageName))
	{
		Decision.Verdict = EAseWriteVerdict::Create;
		Decision.Reason = TEXT("no package in the Asset Registry");
	}
	else if (StoredStructureHash.IsEmpty())
	{
		Decision.Verdict = EAseWriteVerdict::WriteInputsChanged;
		Decision.Reason = TEXT("no stored structure stamp (imported before the incremental gate)");
	}
	else if (!StoredStructureHash.Equals(NewStructureHash, ESearchCase::IgnoreCase))
	{
		Decision.Verdict = EAseWriteVerdict::WriteInputsChanged;
		Decision.Reason = TEXT("tag structure changed (range, durations, or keyframe sprites)");
	}
	else
	{
		Decision.Verdict = EAseWriteVerdict::SkipPayload;
		Decision.Reason = TEXT("tag structure unchanged and the package exists");
	}
	return AseIncremental_Finish(TEXT("flipbook"), FlipbookPackageName, MoveTemp(Decision));
}

FAseWriteDecision FAsepriteIncrementalWrite::ShouldWriteSpritePayload(
	const FString& SpritePackageName,
	const FAseWriteDecision& OwningSheetDecision,
	const bool bSheetObjectRecreated,
	const FAseStampedGrid& StampedGrid,
	const FAseStampedGrid& CurrentGrid,
	const bool bForceFullReimport)
{
	FAseWriteDecision Decision;
	if (bForceFullReimport)
	{
		Decision.Verdict = EAseWriteVerdict::WriteForced;
		Decision.Reason = TEXT("Force Full Reimport bypasses the content gates");
	}
	else if (!RegistryHasPackage(SpritePackageName))
	{
		// Existence never rides the sheet verdict (KTD3): a sprite deleted under an unchanged
		// sheet must be re-created, or no reimport ever repairs it.
		Decision.Verdict = EAseWriteVerdict::Create;
		Decision.Reason = TEXT("no package in the Asset Registry");
	}
	else if (!StampedGrid.Matches(CurrentGrid))
	{
		Decision.Verdict = EAseWriteVerdict::WriteGridChanged;
		Decision.Reason = FString::Printf(TEXT("grid changed (stamped %s, now %s) — cell rects and UVs moved"),
			StampedGrid.IsStamped() ? *StampedGrid.ToString() : TEXT("nothing"),
			*CurrentGrid.ToString());
	}
	else if (bSheetObjectRecreated)
	{
		// R8: a re-created sheet is a NEW UTexture2D object; every sprite of the layer must be
		// re-pointed at it even when its bounds did not move.
		Decision.Verdict = EAseWriteVerdict::WriteInputsChanged;
		Decision.Reason = TEXT("the owning sheet object was re-created — the sprite must re-point at it");
	}
	else if (OwningSheetDecision.ShouldWrite())
	{
		Decision.Verdict = EAseWriteVerdict::WriteInputsChanged;
		Decision.Reason = TEXT("the owning sheet's pixels changed — derive and compare tight bounds");
	}
	else
	{
		Decision.Verdict = EAseWriteVerdict::SkipPayload;
		Decision.Reason = TEXT("the owning sheet was skipped — unchanged pixels cannot move a tight bound");
	}
	return AseIncremental_Finish(TEXT("sprite"), SpritePackageName, MoveTemp(Decision),
		/*bLogSkipAtLogLevel*/ false);
}

FAseWriteDecision FAsepriteIncrementalWrite::ShouldWriteSpriteForDerivedBounds(
	const FString& SpritePackageName,
	const FVector2D& StoredRenderCenter, const FVector2D& StoredRenderSize,
	const FVector2D& DerivedRenderCenter, const FVector2D& DerivedRenderSize,
	const FVector2D& StoredCollisionCenter, const FVector2D& StoredCollisionSize,
	const FVector2D& DerivedCollisionCenter, const FVector2D& DerivedCollisionSize)
{
	// Integer-derived values in floats: exact comparison is safe, but a hair of tolerance keeps a
	// serialized float that round-tripped through text from forcing a rewrite.
	const auto Same = [](const FVector2D& A, const FVector2D& B)
	{
		return A.Equals(B, 0.01f);
	};

	FAseWriteDecision Decision;
	if (!Same(StoredRenderCenter, DerivedRenderCenter) || !Same(StoredRenderSize, DerivedRenderSize))
	{
		Decision.Verdict = EAseWriteVerdict::WriteInputsChanged;
		Decision.Reason = FString::Printf(
			TEXT("render tight bounds moved (stored %s/%s, derived %s/%s)"),
			*StoredRenderCenter.ToString(), *StoredRenderSize.ToString(),
			*DerivedRenderCenter.ToString(), *DerivedRenderSize.ToString());
	}
	else if (!Same(StoredCollisionCenter, DerivedCollisionCenter)
		|| !Same(StoredCollisionSize, DerivedCollisionSize))
	{
		Decision.Verdict = EAseWriteVerdict::WriteInputsChanged;
		Decision.Reason = TEXT("collision tight bounds moved");
	}
	else
	{
		Decision.Verdict = EAseWriteVerdict::SkipPayload;
		Decision.Reason = TEXT("tight bounds unchanged (render and collision)");
	}
	return AseIncremental_Finish(TEXT("sprite"), SpritePackageName, MoveTemp(Decision),
		/*bLogSkipAtLogLevel*/ false);
}

void FAsepriteIncrementalWrite::DeriveTightBoxForCell(
	const TArray<FColor>& CellPixels, const int32 CellWidth, const int32 CellHeight,
	const FIntPoint& CellOffsetInSheet, const float AlphaThreshold,
	FVector2D& OutBoxCenter, FVector2D& OutBoxSize)
{
	// UPaperSprite::FindTextureBoundingBox reproduced over the buffer in hand: FBitmap thresholds
	// with clamp(int(Threshold*255), 0, 255) and treats a pixel as occupied when alpha is STRICTLY
	// greater; shrink order and the degenerate stops must match exactly, or a compare against the
	// engine's serialized result would report false movement.
	const int32 AlphaThresholdInt = FMath::Clamp<int32>(
		static_cast<int32>(AlphaThreshold * 255.0f), 0, 255);

	const auto IsOccupied = [&](const int32 X, const int32 Y)
	{
		const int32 Index = Y * CellWidth + X;
		return CellPixels.IsValidIndex(Index) && CellPixels[Index].A > AlphaThresholdInt;
	};
	const auto IsRowEmpty = [&](const int32 X0, const int32 X1, const int32 Y)
	{
		for (int32 X = X0; X <= X1; ++X)
		{
			if (IsOccupied(X, Y)) { return false; }
		}
		return true;
	};
	const auto IsColumnEmpty = [&](const int32 X, const int32 Y0, const int32 Y1)
	{
		for (int32 Y = Y0; Y <= Y1; ++Y)
		{
			if (IsOccupied(X, Y)) { return false; }
		}
		return true;
	};

	int32 TopBound = 0;
	int32 BottomBound = CellHeight - 1;
	int32 LeftBound = 0;
	int32 RightBound = CellWidth - 1;

	while ((TopBound < BottomBound) && IsRowEmpty(LeftBound, RightBound, TopBound))
	{
		++TopBound;
	}
	while ((BottomBound > TopBound) && IsRowEmpty(LeftBound, RightBound, BottomBound))
	{
		--BottomBound;
	}
	while ((LeftBound < RightBound) && IsColumnEmpty(LeftBound, TopBound, BottomBound))
	{
		++LeftBound;
	}
	while ((RightBound > LeftBound) && IsColumnEmpty(RightBound, TopBound, BottomBound))
	{
		--RightBound;
	}

	const FVector2D BoxSize(
		static_cast<float>(RightBound - LeftBound + 1),
		static_cast<float>(BottomBound - TopBound + 1));
	const FVector2D BoxPosition(
		static_cast<float>(CellOffsetInSheet.X + LeftBound),
		static_cast<float>(CellOffsetInSheet.Y + TopBound));

	OutBoxSize = BoxSize;
	OutBoxCenter = BoxPosition + BoxSize * 0.5f; // CreatePolygonFromBoundingBox recenters the same way
}

FString FAsepriteIncrementalWrite::ComputeTagStructureHash(
	const int32 FromFrame, const int32 ToFrame,
	const TArray<int32>& DurationsMs,
	const TArray<FString>& KeyframeSpriteNames)
{
	FMD5 Md5;
	Md5.Update(reinterpret_cast<const uint8*>(&FromFrame), sizeof(FromFrame));
	Md5.Update(reinterpret_cast<const uint8*>(&ToFrame), sizeof(ToFrame));

	const int32 DurationCount = DurationsMs.Num();
	Md5.Update(reinterpret_cast<const uint8*>(&DurationCount), sizeof(DurationCount));
	if (DurationCount > 0)
	{
		Md5.Update(reinterpret_cast<const uint8*>(DurationsMs.GetData()), DurationCount * sizeof(int32));
	}

	const int32 NameCount = KeyframeSpriteNames.Num();
	Md5.Update(reinterpret_cast<const uint8*>(&NameCount), sizeof(NameCount));
	for (const FString& Name : KeyframeSpriteNames)
	{
		const FTCHARToUTF8 Utf8(*Name);
		const int32 Length = Utf8.Length();
		Md5.Update(reinterpret_cast<const uint8*>(&Length), sizeof(Length));
		Md5.Update(reinterpret_cast<const uint8*>(Utf8.Get()), Length);
	}

	FMD5Hash Hash;
	Hash.Set(Md5);
	return LexToString(Hash);
}

bool FAsepriteIncrementalWrite::ReconcileSheetSettings(UTexture2D* Sheet, const bool bIsNormalMap)
{
	if (!Sheet)
	{
		return false;
	}

	const TextureMipGenSettings WantMipGen = TMGS_NoMipmaps;
	const TextureCompressionSettings WantCompression = bIsNormalMap ? TC_Normalmap : TC_EditorIcon;
	const TextureFilter WantFilter = TF_Nearest;
	const bool bWantSRGB = !bIsNormalMap;
	const TextureGroup WantGroup = bIsNormalMap ? TEXTUREGROUP_WorldNormalMap : TEXTUREGROUP_Pixels2D;

	bool bAnyDiffered = false;
	if (Sheet->MipGenSettings != WantMipGen)
	{
		Sheet->MipGenSettings = WantMipGen;
		bAnyDiffered = true;
	}
	if (Sheet->CompressionSettings != WantCompression)
	{
		Sheet->CompressionSettings = WantCompression;
		bAnyDiffered = true;
	}
	if (Sheet->Filter != WantFilter)
	{
		Sheet->Filter = WantFilter;
		bAnyDiffered = true;
	}
	if (!Sheet->NeverStream)
	{
		Sheet->NeverStream = true;
		bAnyDiffered = true;
	}
	if ((Sheet->SRGB != 0) != bWantSRGB)
	{
		Sheet->SRGB = bWantSRGB;
		bAnyDiffered = true;
	}
	if (Sheet->LODGroup != WantGroup)
	{
		Sheet->LODGroup = WantGroup;
		bAnyDiffered = true;
	}
	return bAnyDiffered;
}
