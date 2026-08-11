// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "BulkDebakeUtils.h"

#include "Paper2DPlusEditorModule.h"
#include "SpriteExtractionUtils.h"
#include "VariantDebake.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Texture2D.h"
#include "Misc/EngineVersionComparison.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"

FString FBulkDebakeUtils::NormalizeName(const FString& In)
{
	FString S = In.ToLower();
	S.ReplaceInline(TEXT("vfx"), TEXT(""));
	S.ReplaceInline(TEXT("_"), TEXT(""));
	S.ReplaceInline(TEXT("-"), TEXT(""));
	S.ReplaceInline(TEXT(" "), TEXT(""));
	return S;
}

FString FBulkDebakeUtils::StripSandwichTokens(const FString& Norm)
{
	FString S = Norm;
	S.ReplaceInline(TEXT("back"), TEXT(""));
	S.ReplaceInline(TEXT("front"), TEXT(""));
	return S;
}

FString FBulkDebakeUtils::RawCommonPrefix(const TArray<FString>& MainNames)
{
	FString Prefix;
	bool bFirst = true;
	for (const FString& Name : MainNames)
	{
		if (bFirst)
		{
			Prefix = Name;
			bFirst = false;
			continue;
		}
		int32 Match = 0;
		while (Match < Prefix.Len() && Match < Name.Len() && Prefix[Match] == Name[Match])
		{
			Match++;
		}
		Prefix = Prefix.Left(Match);
	}
	return Prefix;
}

FString FBulkDebakeUtils::CommonPrefix(const TArray<FString>& MainNames)
{
	FString Prefix = RawCommonPrefix(MainNames);
	while (Prefix.Len() > 0 && (Prefix[Prefix.Len() - 1] == '_' || Prefix[Prefix.Len() - 1] == '-'))
	{
		Prefix = Prefix.Left(Prefix.Len() - 1);
	}
	if (Prefix.StartsWith(TEXT("T_")))
	{
		Prefix.RightChopInline(2);
	}
	return Prefix.IsEmpty() ? TEXT("Debaked") : Prefix;
}

FString FBulkDebakeUtils::VariantLabel(const FString& MainName, const FString& InRawCommonPrefix)
{
	FString Label = MainName;
	if (!InRawCommonPrefix.IsEmpty() && Label.StartsWith(InRawCommonPrefix) && Label.Len() > InRawCommonPrefix.Len())
	{
		Label.RightChopInline(InRawCommonPrefix.Len());
	}
	while (Label.Len() > 0 && (Label[0] == '_' || Label[0] == '-'))
	{
		Label.RightChopInline(1);
	}
	return Label.IsEmpty() ? MainName : Label;
}

void FBulkDebakeUtils::PrefillVfxAssignments(const TArray<FString>& MemberNames, const TArray<FVfxPrefillCandidate>& Candidates,
	TArray<FSoftObjectPath>& OutFront, TArray<FSoftObjectPath>& OutBack)
{
	OutFront.Init(FSoftObjectPath(), MemberNames.Num());
	OutBack.Init(FSoftObjectPath(), MemberNames.Num());
	for (int32 MemberIndex = 0; MemberIndex < MemberNames.Num(); ++MemberIndex)
	{
		const FString NormMain = NormalizeName(MemberNames[MemberIndex]);
		int32 BestFrontScore = 0, BestBackScore = 0;
		for (const FVfxPrefillCandidate& Candidate : Candidates)
		{
			if (Candidate.NameCore.IsEmpty())
			{
				continue;
			}
			const bool bCandidateFitsMember = NormMain.Contains(Candidate.NameCore);
			if (!bCandidateFitsMember && !Candidate.NameCore.Contains(NormMain))
			{
				continue;
			}
			// A core contained by the member is a stronger match than a candidate that merely
			// contains the member. Without this tier, "cooldown" steals a shorter "cool" member
			// from an exact "cool" candidate simply because its core is longer.
			const int32 Score = (bCandidateFitsMember ? 1 << 20 : 0) + Candidate.NameCore.Len();
			int32& BestScore = Candidate.bBack ? BestBackScore : BestFrontScore;
			if (Score > BestScore)
			{
				BestScore = Score;
				(Candidate.bBack ? OutBack[MemberIndex] : OutFront[MemberIndex]) = Candidate.Path;
			}
		}
	}
}

bool FBulkDebakeUtils::BuildCompositeBuffer(const TArray<FColor>& Base, const TArray<FColor>& Overlay, TArray<FColor>& Out)
{
	if (Base.Num() != Overlay.Num())
	{
		return false;
	}
	Out.SetNumUninitialized(Base.Num());
	for (int32 P = 0; P < Base.Num(); ++P)
	{
		Out[P] = Overlay[P].A > 0 ? FVariantDebake::AlphaOver(Overlay[P], Base[P]) : Base[P];
	}
	return true;
}

bool FBulkDebakeUtils::ValidateGridDims(int32 W, int32 H, int32 Columns, int32 Rows)
{
	return W > 0 && H > 0 && Columns >= 1 && Rows >= 1 && W % Columns == 0 && H % Rows == 0;
}

UTexture2D* FBulkDebakeUtils::CreateTransientPreviewTexture(int32 W, int32 H, const TArray<FColor>& Pixels)
{
	if (W <= 0 || H <= 0 || (int64)Pixels.Num() != (int64)W * H)
	{
		return nullptr;
	}

	const TConstArrayView64<uint8> ImageData(reinterpret_cast<const uint8*>(Pixels.GetData()), (int64)Pixels.Num() * 4);
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 4)
	UTexture2D* Texture = UTexture2D::CreateTransient(W, H, PF_B8G8R8A8, NAME_None, ImageData);
#else
	// UE <5.4 has no CreateTransient overload that takes initial pixel data; create empty, then upload into Mip 0.
	UTexture2D* Texture = UTexture2D::CreateTransient(W, H, PF_B8G8R8A8);
	if (Texture)
	{
		// GetPlatformData exists throughout UE 5.0-5.8; keeping one spelling avoids a
		// version-only direct-field access that diverges from the shared compatibility contract.
		FTexturePlatformData* PlatformData = Texture->GetPlatformData();
		if (!PlatformData || PlatformData->Mips.Num() == 0)
		{
			return nullptr;
		}
		void* Dest = PlatformData->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
		if (!Dest)
		{
			return nullptr;
		}
		FMemory::Memcpy(Dest, ImageData.GetData(), (SIZE_T)ImageData.Num());
		PlatformData->Mips[0].BulkData.Unlock();
	}
#endif
	if (Texture)
	{
		Texture->Filter = TF_Nearest;    // crisp pixels at high zoom
		Texture->SRGB = true;            // match the (sRGB) sources so colors display identically
		Texture->UpdateResource();
	}
	return Texture;
}

bool FBulkDebakeUtils::LoadPixels(UTexture2D* Texture, TArray<FColor>& OutPixels, int32& OutW, int32& OutH)
{
	return Texture && FSpriteExtractionUtils::LoadTextureData(Texture, OutPixels, OutW, OutH);
}

UTexture2D* FBulkDebakeUtils::WriteSheetTexture(const FString& OutputPath, const FString& InName, int32 W, int32 H, const TArray<FColor>& Pixels)
{
	// Fail-closed: the memcpy below reads exactly W*H*4 bytes from the buffer. An undersized buffer
	// (e.g. a future "leave empty overlays unallocated" optimisation in FVariantDebake::Run) must
	// refuse here instead of reading out of bounds.
	if (W <= 0 || H <= 0 || (int64)Pixels.Num() != (int64)W * H)
	{
		UE_LOG(LogPaper2DPlusEditor, Error,
			TEXT("WriteSheetTexture('%s'): pixel buffer size %d does not match %dx%d — refusing to write."),
			*InName, Pixels.Num(), W, H);
		return nullptr;
	}

	const bool bIsAutomationTemp = OutputPath.Equals(TEXT("/Temp")) || OutputPath.StartsWith(TEXT("/Temp/"));
	if (OutputPath.IsEmpty() || (!bIsAutomationTemp && !FPackageName::IsValidLongPackageName(OutputPath)))
	{
		UE_LOG(LogPaper2DPlusEditor, Error,
			TEXT("WriteSheetTexture('%s'): output path '%s' is not a valid long package path — write aborted."),
			*InName, *OutputPath);
		return nullptr;
	}

	FString Name = InName;
	FSpriteExtractionUtils::SanitizeAssetName(Name);
	const FString PackageName = OutputPath / Name;
	UPackage* Package = CreatePackage(*PackageName);
	if (!Package)
	{
		return nullptr;
	}
	// Load any on-disk occupant so a re-run in a fresh session REUSES the prior output instead of
	// silently shadowing it (FindObject alone misses unloaded assets).
	Package->FullyLoad();
	const bool bExisted = FindObject<UTexture2D>(Package, *Name) != nullptr;
	UTexture2D* Texture = FSpriteExtractionUtils::GetOrCreateTextureForName(Package, Name);
	if (!Texture)
	{
		return nullptr;
	}
	Texture->Source.Init(W, H, 1, 1, TSF_BGRA8);
	{
		uint8* Dest = Texture->Source.LockMip(0);
		if (!Dest)
		{
			UE_LOG(LogPaper2DPlusEditor, Error,
				TEXT("WriteSheetTexture('%s'): failed to lock the texture source mip — refusing to write."),
				*InName);
			return nullptr;
		}
		FMemory::Memcpy(Dest, Pixels.GetData(), (int64)W * H * sizeof(FColor));
		Texture->Source.UnlockMip(0);
	}
	Texture->CompressionSettings = TC_EditorIcon;
	Texture->Filter = TF_Nearest;
	Texture->MipGenSettings = TMGS_NoMipmaps;
	Texture->LODGroup = TEXTUREGROUP_Pixels2D;
	Texture->NeverStream = true;
	Texture->SRGB = true;
	Texture->UpdateResource();
	// /Temp packages skip dirty + registry: the SCC async git-status against /Temp fatally errors,
	// and headless tests write there (the AsepriteImporter convention).
	if (!PackageName.StartsWith(TEXT("/Temp")))
	{
		Package->MarkPackageDirty();
		if (!bExisted)
		{
			FAssetRegistryModule::AssetCreated(Texture);
		}
	}
	return Texture;
}
