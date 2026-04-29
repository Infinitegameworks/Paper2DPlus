// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "Paper2DPlusCharacterProfileAsset.h"
#include "Paper2DPlusSettings.h"
#include "FrameEvents/Paper2DPlusFrameEvent.h"
#include "FrameEvents/Paper2DPlusFrameEventState.h"
#include "FrameEvents/Paper2DPlusSpawnEffectFrameEvent.h"
#include "PaperSprite.h"
#include "PaperFlipbook.h"
#include "AnimSequences/PaperZDAnimSequence.h"
#include "JsonObjectConverter.h"
#include "Misc/FileHelper.h"
#include "Containers/Ticker.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/AssetData.h"
#include "Misc/PackageName.h"
#include "Runtime/Launch/Resources/Version.h"

/** UPaper2DPlusCharacterProfileAsset — Master character animation data asset: serialization, JSON import/export, caching, migration, and lookup functions. */

namespace
{
void NormalizeFrameSourceIndices(FFlipbookProfileEntry& Anim)
{
	TSet<int32> UsedSourceIndices;
	int32 NextAvailableSourceIndex = 0;

	auto AssignUniqueSourceIndex = [&UsedSourceIndices, &NextAvailableSourceIndex](FSpriteExtractionInfo& Info)
	{
		if (Info.SourceFrameIndex >= 0 && !UsedSourceIndices.Contains(Info.SourceFrameIndex))
		{
			UsedSourceIndices.Add(Info.SourceFrameIndex);
			NextAvailableSourceIndex = FMath::Max(NextAvailableSourceIndex, Info.SourceFrameIndex + 1);
			return;
		}

		while (UsedSourceIndices.Contains(NextAvailableSourceIndex))
		{
			++NextAvailableSourceIndex;
		}

		Info.SourceFrameIndex = NextAvailableSourceIndex;
		UsedSourceIndices.Add(NextAvailableSourceIndex);
		++NextAvailableSourceIndex;
	};

	for (FSpriteExtractionInfo& Info : Anim.CombatData.FrameExtractionInfo)
	{
		Info.bExcludedFromFlipbook = false;
		AssignUniqueSourceIndex(Info);
	}

	for (FExcludedFlipbookFrameData& Excluded : Anim.CombatData.ExcludedFrames)
	{
		Excluded.ExtractionInfo.bExcludedFromFlipbook = true;
		AssignUniqueSourceIndex(Excluded.ExtractionInfo);
	}
}

int32 FindRestoreInsertIndex(const FFlipbookProfileEntry& Anim, int32 SourceFrameIndex)
{
	int32 InsertIndex = 0;
	for (const FSpriteExtractionInfo& Info : Anim.CombatData.FrameExtractionInfo)
	{
		if (Info.SourceFrameIndex != INDEX_NONE && Info.SourceFrameIndex < SourceFrameIndex)
		{
			++InsertIndex;
		}
	}
	return InsertIndex;
}

}

int32 FFlipbookEffectData::GetEffectFrameCount() const
{
	return EffectFlipbook ? EffectFlipbook->GetNumKeyFrames() : 0;
}

bool UPaper2DPlusCharacterProfileAsset::GetFrameSpriteBounds(UPaperFlipbook* Flipbook, int32 FrameIndex, int32& OutWidth, int32& OutHeight)
{
	OutWidth = 0;
	OutHeight = 0;

	if (!Flipbook || FrameIndex < 0 || FrameIndex >= Flipbook->GetNumKeyFrames())
	{
		return false;
	}

	UPaperSprite* Sprite = Flipbook->GetKeyFrameChecked(FrameIndex).Sprite;
	if (!Sprite)
	{
		return false;
	}

#if WITH_EDITOR
	const FVector2D SourceSize = Sprite->GetSourceSize();
	OutWidth = FMath::Max(0, FMath::RoundToInt(SourceSize.X));
	OutHeight = FMath::Max(0, FMath::RoundToInt(SourceSize.Y));
	return OutWidth > 0 && OutHeight > 0;
#else
	return false;
#endif
}

bool UPaper2DPlusCharacterProfileAsset::ClampHitboxToBounds(FHitboxData& Hitbox, int32 BoundsWidth, int32 BoundsHeight)
{
	if (BoundsWidth <= 0 || BoundsHeight <= 0)
	{
		return false;
	}

	const FHitboxData Original = Hitbox;

	Hitbox.Width = FMath::Clamp(Hitbox.Width, 1, BoundsWidth);
	Hitbox.Height = FMath::Clamp(Hitbox.Height, 1, BoundsHeight);
	Hitbox.X = FMath::Clamp(Hitbox.X, 0, BoundsWidth - Hitbox.Width);
	Hitbox.Y = FMath::Clamp(Hitbox.Y, 0, BoundsHeight - Hitbox.Height);

	return Hitbox.X != Original.X
		|| Hitbox.Y != Original.Y
		|| Hitbox.Width != Original.Width
		|| Hitbox.Height != Original.Height;
}

void UPaper2DPlusCharacterProfileAsset::ClampFrameHitboxesToSpriteBounds(FFrameHitboxData& Frame, UPaperFlipbook* Flipbook, int32 FrameIndex)
{
	int32 BoundsWidth = 0;
	int32 BoundsHeight = 0;
	if (!GetFrameSpriteBounds(Flipbook, FrameIndex, BoundsWidth, BoundsHeight))
	{
		return;
	}

	for (FHitboxData& Hitbox : Frame.Hitboxes)
	{
		ClampHitboxToBounds(Hitbox, BoundsWidth, BoundsHeight);
	}
}

UPaper2DPlusCharacterProfileAsset::UPaper2DPlusCharacterProfileAsset()
{
	DisplayName = TEXT("New Character Profile");
}

FPrimaryAssetId UPaper2DPlusCharacterProfileAsset::GetPrimaryAssetId() const
{
	return FPrimaryAssetId(TEXT("CharacterProfile"), GetFName());
}

void UPaper2DPlusCharacterProfileAsset::SyncFramesToFlipbook(int32 FlipbookIndex)
{
	if (!Flipbooks.IsValidIndex(FlipbookIndex)) return;

	FFlipbookProfileEntry& Anim = Flipbooks[FlipbookIndex];
	if (Anim.Identity.Flipbook.IsNull()) return;

	UPaperFlipbook* FB = Anim.Identity.Flipbook.LoadSynchronous();
	if (!FB) return;

	int32 FlipbookFrameCount = FB->GetNumKeyFrames();
	if (FlipbookFrameCount <= 0) return;

	// Resize Frames array to match flipbook — grow to add empty frames, shrink to trim orphans
	if (Anim.CombatData.Frames.Num() != FlipbookFrameCount)
	{
		Anim.CombatData.Frames.SetNum(FlipbookFrameCount);
	}

	// Also sync FrameExtractionInfo if populated
	if (Anim.CombatData.FrameExtractionInfo.Num() > 0 && Anim.CombatData.FrameExtractionInfo.Num() != FlipbookFrameCount)
	{
		Anim.CombatData.FrameExtractionInfo.SetNum(FlipbookFrameCount);
	}

	// Sync RootMotion if populated
	if (Anim.MotionData.RootMotion.Num() > 0 && Anim.MotionData.RootMotion.Num() != FlipbookFrameCount)
	{
		Anim.MotionData.RootMotion.SetNum(FlipbookFrameCount);
	}

	NormalizeFrameSourceIndices(Anim);
}

void UPaper2DPlusCharacterProfileAsset::SyncAllFramesToFlipbooks()
{
	for (int32 i = 0; i < Flipbooks.Num(); ++i)
	{
		SyncFramesToFlipbook(i);
	}
}

void UPaper2DPlusCharacterProfileAsset::PostLoad()
{
	Super::PostLoad();

	// ─── Sub-struct migration (Phase 1b) ──────────────────────────────
	// Old assets have fields at the top level of FFlipbookProfileEntry.
	// The UPROPERTY(meta=(DeprecatedProperty)) fields load old data into
	// *_DEPRECATED members. Migrate them to the new sub-struct locations.
	for (FFlipbookProfileEntry& Anim : Flipbooks)
	{
		// Identity
		if (!Anim.FlipbookName_DEPRECATED.IsEmpty() && Anim.Identity.FlipbookName.IsEmpty())
		{
			Anim.Identity.FlipbookName = MoveTemp(Anim.FlipbookName_DEPRECATED);
		}
		if (!Anim.Flipbook_DEPRECATED.IsNull() && Anim.Identity.Flipbook.IsNull())
		{
			Anim.Identity.Flipbook = MoveTemp(Anim.Flipbook_DEPRECATED);
		}
		if (Anim.PaperZDSequence_DEPRECATED && !Anim.Identity.PaperZDSequence)
		{
			Anim.Identity.PaperZDSequence = Cast<UPaperZDAnimSequence>(Anim.PaperZDSequence_DEPRECATED.Get());
			Anim.PaperZDSequence_DEPRECATED = nullptr;
		}

		// CombatData
		if (Anim.Frames_DEPRECATED.Num() > 0 && Anim.CombatData.Frames.Num() == 0)
		{
			Anim.CombatData.Frames = MoveTemp(Anim.Frames_DEPRECATED);
		}
		if (Anim.ExcludedFrames_DEPRECATED.Num() > 0 && Anim.CombatData.ExcludedFrames.Num() == 0)
		{
			Anim.CombatData.ExcludedFrames = MoveTemp(Anim.ExcludedFrames_DEPRECATED);
		}
		if (Anim.FrameExtractionInfo_DEPRECATED.Num() > 0 && Anim.CombatData.FrameExtractionInfo.Num() == 0)
		{
			Anim.CombatData.FrameExtractionInfo = MoveTemp(Anim.FrameExtractionInfo_DEPRECATED);
		}

		// MotionData
		if (Anim.RootMotion_DEPRECATED.Num() > 0 && Anim.MotionData.RootMotion.Num() == 0)
		{
			Anim.MotionData.RootMotion = MoveTemp(Anim.RootMotion_DEPRECATED);
		}

		// EditorMeta
		if (Anim.CompletionFlags_DEPRECATED != 0 && Anim.EditorMeta.CompletionFlags == 0)
		{
			Anim.EditorMeta.CompletionFlags = Anim.CompletionFlags_DEPRECATED;
			Anim.CompletionFlags_DEPRECATED = 0;
		}
	}

	// Sync extraction info and root motion arrays to match Frames count
	for (FFlipbookProfileEntry& Anim : Flipbooks)
	{
		if (Anim.CombatData.Frames.Num() > 0)
		{
			if (Anim.CombatData.FrameExtractionInfo.Num() != Anim.CombatData.Frames.Num())
			{
				Anim.CombatData.FrameExtractionInfo.SetNum(Anim.CombatData.Frames.Num());
			}
			if (Anim.MotionData.RootMotion.Num() > 0 && Anim.MotionData.RootMotion.Num() != Anim.CombatData.Frames.Num())
			{
				Anim.MotionData.RootMotion.SetNum(Anim.CombatData.Frames.Num());
			}
		}

		NormalizeFrameSourceIndices(Anim);
	}

	// ─── Effects → FrameEvents migration (Phase 3) ───────────────────
	MigrateEffectsToFrameEvents();

	// Clear retired CompletionFlags bits 3 (Phases tab) and 4 (Effects tab) —
	// the corresponding tabs were deleted; the bits no longer have meaning.
	constexpr int32 RetiredCompletionBits = (1 << 3) | (1 << 4);
	for (FFlipbookProfileEntry& Anim : Flipbooks)
	{
		Anim.EditorMeta.CompletionFlags &= ~RetiredCompletionBits;
	}

	// ─── Auto-resolve SourceTexture and SpritesOutputPath from flipbook sprites ───
	for (FFlipbookProfileEntry& Entry : Flipbooks)
	{
		UPaperFlipbook* FB = Entry.Identity.Flipbook.IsNull() ? nullptr : Entry.Identity.Flipbook.LoadSynchronous();
		if (!FB || FB->GetNumKeyFrames() == 0) continue;

		const FPaperFlipbookKeyFrame& FirstFrame = FB->GetKeyFrameChecked(0);
		UPaperSprite* FirstSprite = FirstFrame.Sprite;
		if (!FirstSprite) continue;

		// Auto-resolve SourceTexture from the first sprite's texture
#if WITH_EDITORONLY_DATA
		if (Entry.SourceTexture.IsNull())
		{
			UTexture2D* SpriteTex = Cast<UTexture2D>(FirstSprite->GetSourceTexture());
			if (SpriteTex)
			{
				Entry.SourceTexture = SpriteTex;
			}
		}
#endif

		// Auto-resolve SpritesOutputPath from the first sprite's package path
		if (Entry.SpritesOutputPath.IsEmpty())
		{
			Entry.SpritesOutputPath = FPackageName::GetLongPackagePath(FirstSprite->GetPackage()->GetName());
		}
	}

	bFlipbookLookupCacheValid = false;
	bNameLookupCacheValid = false;
	bTagLookupCacheValid = false;

	// Skip the synchronous PaperZD scan when we're in the middle of a load —
	// AutoPopulatePaperZDSequences calls LoadSynchronous on soft refs and scans
	// the asset registry, which can deadlock during PostLoad under streaming load.
	// Defer to the next frame so the editor is idle when we run it.
	if (IsAsyncLoading())
	{
		TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset> WeakThis(this);
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
			[WeakThis](float) -> bool
			{
				if (UPaper2DPlusCharacterProfileAsset* Self = WeakThis.Get())
				{
					Self->AutoPopulatePaperZDSequences();
				}
				return false; // one-shot
			}));
	}
	else
	{
		AutoPopulatePaperZDSequences();
	}
}

#if WITH_EDITOR
void UPaper2DPlusCharacterProfileAsset::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	bFlipbookLookupCacheValid = false;
	bNameLookupCacheValid = false;
	bTagLookupCacheValid = false;

	// Re-populate PaperZD sequences when AnimSource changes
	if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UPaper2DPlusCharacterProfileAsset, PaperZDAnimSource))
	{
		// Clear existing auto-resolved sequences so they get re-resolved
		for (FFlipbookProfileEntry& Anim : Flipbooks)
		{
			Anim.Identity.PaperZDSequence = nullptr;
		}
		AutoPopulatePaperZDSequences();
	}
}
#endif

void UPaper2DPlusCharacterProfileAsset::RebuildFlipbookLookupCache() const
{
	FlipbookToDataIndexCache.Empty();
	FlipbookToDataIndexCache.Reserve(Flipbooks.Num());

	for (int32 Index = 0; Index < Flipbooks.Num(); ++Index)
	{
		// Use LoadSynchronous so soft references are resolved into the cache
		if (UPaperFlipbook* Flipbook = Flipbooks[Index].Identity.Flipbook.LoadSynchronous())
		{
			FlipbookToDataIndexCache.FindOrAdd(Flipbook) = Index;
		}
	}

	CachedFlipbookCount = Flipbooks.Num();
	bFlipbookLookupCacheValid = true;
}

void UPaper2DPlusCharacterProfileAsset::RebuildNameLookupCache() const
{
	NameToFlipbookIndexCache.Empty();
	NameToFlipbookIndexCache.Reserve(Flipbooks.Num());

	for (int32 Index = 0; Index < Flipbooks.Num(); ++Index)
	{
		NameToFlipbookIndexCache.Add(Flipbooks[Index].Identity.FlipbookName.ToLower(), Index);
	}

	bNameLookupCacheValid = true;
}

const FFlipbookProfileEntry* UPaper2DPlusCharacterProfileAsset::FindFlipbookData(const FString& FlipbookName) const
{
	if (!bNameLookupCacheValid || Flipbooks.Num() != NameToFlipbookIndexCache.Num())
	{
		RebuildNameLookupCache();
	}

	if (const int32* FoundIndex = NameToFlipbookIndexCache.Find(FlipbookName.ToLower()))
	{
		if (Flipbooks.IsValidIndex(*FoundIndex) &&
			Flipbooks[*FoundIndex].Identity.FlipbookName.Equals(FlipbookName, ESearchCase::IgnoreCase))
		{
			return &Flipbooks[*FoundIndex];
		}
	}

	// Cache miss — linear scan fallback
	for (int32 i = 0; i < Flipbooks.Num(); ++i)
	{
		if (Flipbooks[i].Identity.FlipbookName.Equals(FlipbookName, ESearchCase::IgnoreCase))
		{
			NameToFlipbookIndexCache.Add(FlipbookName.ToLower(), i);
			return &Flipbooks[i];
		}
	}
	return nullptr;
}

const FFlipbookProfileEntry* UPaper2DPlusCharacterProfileAsset::FindFlipbookDataPtr(const FString& FlipbookName) const
{
	return FindFlipbookData(FlipbookName);
}

TArray<FString> UPaper2DPlusCharacterProfileAsset::GetFlipbookNames() const
{
	TArray<FString> Names;
	Names.Reserve(Flipbooks.Num());
	for (const FFlipbookProfileEntry& Anim : Flipbooks)
	{
		Names.Add(Anim.Identity.FlipbookName);
	}
	return Names;
}

bool UPaper2DPlusCharacterProfileAsset::GetFlipbook(const FString& FlipbookName, FFlipbookProfileEntry& OutFlipbook) const
{
	if (const FFlipbookProfileEntry* Anim = FindFlipbookData(FlipbookName))
	{
		OutFlipbook = *Anim;
		return true;
	}
	return false;
}

bool UPaper2DPlusCharacterProfileAsset::GetFlipbookByIndex(int32 Index, FFlipbookProfileEntry& OutFlipbook) const
{
	if (Flipbooks.IsValidIndex(Index))
	{
		OutFlipbook = Flipbooks[Index];
		return true;
	}
	return false;
}

int32 UPaper2DPlusCharacterProfileAsset::GetFrameCount(const FString& FlipbookName) const
{
	if (const FFlipbookProfileEntry* Anim = FindFlipbookData(FlipbookName))
	{
		return Anim->CombatData.Frames.Num();
	}
	return 0;
}

bool UPaper2DPlusCharacterProfileAsset::GetFrame(const FString& FlipbookName, int32 FrameIndex, FFrameHitboxData& OutFrame) const
{
	if (const FFlipbookProfileEntry* Anim = FindFlipbookData(FlipbookName))
	{
		if (const FFrameHitboxData* Frame = Anim->GetFrame(FrameIndex))
		{
			OutFrame = *Frame;
			return true;
		}
	}
	return false;
}

bool UPaper2DPlusCharacterProfileAsset::GetFrameByName(const FString& FlipbookName, const FString& FrameName, FFrameHitboxData& OutFrame) const
{
	if (const FFlipbookProfileEntry* Anim = FindFlipbookData(FlipbookName))
	{
		if (const FFrameHitboxData* Frame = Anim->GetFrameByName(FrameName))
		{
			OutFrame = *Frame;
			return true;
		}
	}
	return false;
}

const FFlipbookProfileEntry* UPaper2DPlusCharacterProfileAsset::FindByFlipbookPtr(UPaperFlipbook* Flipbook) const
{
	if (!Flipbook)
	{
		return nullptr;
	}

	if (!bFlipbookLookupCacheValid || Flipbooks.Num() != CachedFlipbookCount)
	{
		RebuildFlipbookLookupCache();
	}

	const int32* FoundIndex = FlipbookToDataIndexCache.Find(Flipbook);
	if (FoundIndex && Flipbooks.IsValidIndex(*FoundIndex))
	{
		const FFlipbookProfileEntry& Anim = Flipbooks[*FoundIndex];
		if (Anim.Identity.Flipbook.Get() == Flipbook)
		{
			return &Anim;
		}
	}

	// Cache miss — linear scan fallback, resolve soft references.
	// Try .Get() first (already-loaded check) before LoadSynchronous() to avoid disk I/O.
	for (int32 i = 0; i < Flipbooks.Num(); ++i)
	{
		UPaperFlipbook* Loaded = Flipbooks[i].Identity.Flipbook.Get();
		if (!Loaded)
		{
			Loaded = Flipbooks[i].Identity.Flipbook.LoadSynchronous();
		}
		if (Loaded == Flipbook)
		{
			FlipbookToDataIndexCache.FindOrAdd(Flipbook) = i;
			return &Flipbooks[i];
		}
	}
	return nullptr;
}


bool UPaper2DPlusCharacterProfileAsset::FindByFlipbook(UPaperFlipbook* Flipbook, FFlipbookProfileEntry& OutFlipbook) const
{
	if (const FFlipbookProfileEntry* Anim = FindByFlipbookPtr(Flipbook))
	{
		OutFlipbook = *Anim;
		return true;
	}

	return false;
}

TArray<FHitboxData> UPaper2DPlusCharacterProfileAsset::GetHitboxes(const FString& FlipbookName, int32 FrameIndex) const
{
	if (const FFlipbookProfileEntry* Anim = FindFlipbookData(FlipbookName))
	{
		if (const FFrameHitboxData* Frame = Anim->GetFrame(FrameIndex))
		{
			return Frame->Hitboxes;
		}
	}
	return TArray<FHitboxData>();
}

TArray<FHitboxData> UPaper2DPlusCharacterProfileAsset::GetHitboxesByType(const FString& FlipbookName, int32 FrameIndex, EHitboxType Type) const
{
	if (const FFlipbookProfileEntry* Anim = FindFlipbookData(FlipbookName))
	{
		if (const FFrameHitboxData* Frame = Anim->GetFrame(FrameIndex))
		{
			return Frame->GetHitboxesByType(Type);
		}
	}
	return TArray<FHitboxData>();
}

TArray<FSocketData> UPaper2DPlusCharacterProfileAsset::GetSockets(const FString& FlipbookName, int32 FrameIndex) const
{
	if (const FFlipbookProfileEntry* Anim = FindFlipbookData(FlipbookName))
	{
		if (const FFrameHitboxData* Frame = Anim->GetFrame(FrameIndex))
		{
			return Frame->Sockets;
		}
	}
	return TArray<FSocketData>();
}

bool UPaper2DPlusCharacterProfileAsset::FindSocket(const FString& FlipbookName, int32 FrameIndex, const FString& SocketName, FSocketData& OutSocket) const
{
	if (const FFlipbookProfileEntry* Anim = FindFlipbookData(FlipbookName))
	{
		if (const FFrameHitboxData* Frame = Anim->GetFrame(FrameIndex))
		{
			if (const FSocketData* Socket = Frame->FindSocket(SocketName))
			{
				OutSocket = *Socket;
				return true;
			}
		}
	}
	return false;
}

bool UPaper2DPlusCharacterProfileAsset::HasFlipbook(const FString& FlipbookName) const
{
	return FindFlipbookData(FlipbookName) != nullptr;
}



bool UPaper2DPlusCharacterProfileAsset::CopyFrameDataToRange(const FString& FlipbookName, int32 SourceFrameIndex, int32 RangeStart, int32 RangeEnd, bool bIncludeSockets, bool bMerge)
{
	for (FFlipbookProfileEntry& Anim : Flipbooks)
	{
		if (!Anim.Identity.FlipbookName.Equals(FlipbookName, ESearchCase::IgnoreCase))
		{
			continue;
		}

		if (!Anim.CombatData.Frames.IsValidIndex(SourceFrameIndex))
		{
			return false;
		}

		const int32 Start = FMath::Clamp(FMath::Min(RangeStart, RangeEnd), 0, Anim.CombatData.Frames.Num() - 1);
		const int32 End = FMath::Clamp(FMath::Max(RangeStart, RangeEnd), 0, Anim.CombatData.Frames.Num() - 1);

		const FFrameHitboxData SourceCopy = Anim.CombatData.Frames[SourceFrameIndex];
		UPaperFlipbook* Flipbook = Anim.Identity.Flipbook.LoadSynchronous();
		for (int32 Index = Start; Index <= End; ++Index)
		{
			if (Index == SourceFrameIndex)
			{
				continue;
			}

			if (bMerge)
			{
				// Append source hitboxes to existing ones
				Anim.CombatData.Frames[Index].Hitboxes.Append(SourceCopy.Hitboxes);
			}
			else
			{
				// Replace: overwrite entire hitbox array
				Anim.CombatData.Frames[Index].Hitboxes = SourceCopy.Hitboxes;
			}
			ClampFrameHitboxesToSpriteBounds(Anim.CombatData.Frames[Index], Flipbook, Index);

			if (bIncludeSockets)
			{
				if (bMerge)
				{
					// Append source sockets, skipping ones already present by name
					for (const FSocketData& SourceSocket : SourceCopy.Sockets)
					{
						bool bAlreadyExists = false;
						for (const FSocketData& ExistingSocket : Anim.CombatData.Frames[Index].Sockets)
						{
							if (ExistingSocket.Name == SourceSocket.Name)
							{
								bAlreadyExists = true;
								break;
							}
						}
						if (!bAlreadyExists)
						{
							Anim.CombatData.Frames[Index].Sockets.Add(SourceSocket);
						}
					}
				}
				else
				{
					Anim.CombatData.Frames[Index].Sockets = SourceCopy.Sockets;
				}
			}
		}

		return true;
	}

	return false;
}

bool UPaper2DPlusCharacterProfileAsset::ExcludeFlipbookFrame(int32 FlipbookIndex, int32 FrameIndex)
{
	if (!Flipbooks.IsValidIndex(FlipbookIndex))
	{
		return false;
	}

	FFlipbookProfileEntry& Anim = Flipbooks[FlipbookIndex];
	UPaperFlipbook* Flipbook = Anim.Identity.Flipbook.LoadSynchronous();
	if (!Flipbook)
	{
		return false;
	}

	const int32 KeyFrameCount = Flipbook->GetNumKeyFrames();
	if (FrameIndex < 0 || FrameIndex >= KeyFrameCount)
	{
		return false;
	}

	// NOTE: Does NOT call Modify() — callers manage transactions via BeginTransaction/EndTransaction.

	// Ensure metadata arrays are aligned with the live keyframe list before excluding.
	if (Anim.CombatData.Frames.Num() != KeyFrameCount)
	{
		Anim.CombatData.Frames.SetNum(KeyFrameCount);
	}
	if (Anim.CombatData.FrameExtractionInfo.Num() != KeyFrameCount)
	{
		Anim.CombatData.FrameExtractionInfo.SetNum(KeyFrameCount);
	}
	if (Anim.MotionData.RootMotion.Num() > 0 && Anim.MotionData.RootMotion.Num() != KeyFrameCount)
	{
		Anim.MotionData.RootMotion.SetNum(KeyFrameCount);
	}

	NormalizeFrameSourceIndices(Anim);

	FExcludedFlipbookFrameData ExcludedFrame;
	ExcludedFrame.FrameData = Anim.CombatData.Frames.IsValidIndex(FrameIndex) ? Anim.CombatData.Frames[FrameIndex] : FFrameHitboxData();
	ExcludedFrame.ExtractionInfo = Anim.CombatData.FrameExtractionInfo.IsValidIndex(FrameIndex) ? Anim.CombatData.FrameExtractionInfo[FrameIndex] : FSpriteExtractionInfo();
	ExcludedFrame.ExtractionInfo.bExcludedFromFlipbook = true;
	ExcludedFrame.RootMotionData = Anim.MotionData.RootMotion.IsValidIndex(FrameIndex) ? Anim.MotionData.RootMotion[FrameIndex] : FRootMotionFrameData();

	{
		FScopedFlipbookMutator Mutator(Flipbook);
		if (!Mutator.KeyFrames.IsValidIndex(FrameIndex))
		{
			return false;
		}

		ExcludedFrame.KeyFrame = Mutator.KeyFrames[FrameIndex];
		Mutator.KeyFrames.RemoveAt(FrameIndex);
	}

	if (Anim.CombatData.Frames.IsValidIndex(FrameIndex))
	{
		Anim.CombatData.Frames.RemoveAt(FrameIndex);
	}
	if (Anim.CombatData.FrameExtractionInfo.IsValidIndex(FrameIndex))
	{
		Anim.CombatData.FrameExtractionInfo.RemoveAt(FrameIndex);
	}
	if (Anim.MotionData.RootMotion.IsValidIndex(FrameIndex))
	{
		Anim.MotionData.RootMotion.RemoveAt(FrameIndex);
	}

	Anim.CombatData.ExcludedFrames.Add(MoveTemp(ExcludedFrame));
	NormalizeFrameSourceIndices(Anim);

	Flipbook->MarkPackageDirty();
	MarkPackageDirty();
	return true;
}

bool UPaper2DPlusCharacterProfileAsset::RestoreExcludedFlipbookFrame(int32 FlipbookIndex, int32 ExcludedFrameIndex)
{
	if (!Flipbooks.IsValidIndex(FlipbookIndex))
	{
		return false;
	}

	FFlipbookProfileEntry& Anim = Flipbooks[FlipbookIndex];
	if (!Anim.CombatData.ExcludedFrames.IsValidIndex(ExcludedFrameIndex))
	{
		return false;
	}

	// If the excluded entry's Sprite was deleted from the project while the
	// excluded frame sat in storage, reinserting it would produce a broken
	// keyframe that renders blank. Bail so the user can delete the stale entry.
	if (!Anim.CombatData.ExcludedFrames[ExcludedFrameIndex].KeyFrame.Sprite)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("RestoreExcludedFlipbookFrame: excluded frame %d of flipbook %d has a null Sprite (source sprite deleted?); skipping restore."),
			ExcludedFrameIndex, FlipbookIndex);
		return false;
	}

	UPaperFlipbook* Flipbook = Anim.Identity.Flipbook.LoadSynchronous();
	if (!Flipbook)
	{
		return false;
	}

	// NOTE: Does NOT call Modify() — callers manage transactions via BeginTransaction/EndTransaction.

	const int32 KeyFrameCount = Flipbook->GetNumKeyFrames();
	if (Anim.CombatData.Frames.Num() != KeyFrameCount)
	{
		Anim.CombatData.Frames.SetNum(KeyFrameCount);
	}
	if (Anim.CombatData.FrameExtractionInfo.Num() != KeyFrameCount)
	{
		Anim.CombatData.FrameExtractionInfo.SetNum(KeyFrameCount);
	}
	if (Anim.MotionData.RootMotion.Num() > 0 && Anim.MotionData.RootMotion.Num() != KeyFrameCount)
	{
		Anim.MotionData.RootMotion.SetNum(KeyFrameCount);
	}

	NormalizeFrameSourceIndices(Anim);

	const FExcludedFlipbookFrameData ExcludedFrame = Anim.CombatData.ExcludedFrames[ExcludedFrameIndex];
	const int32 SourceFrameIndex = ExcludedFrame.ExtractionInfo.SourceFrameIndex;
	int32 InsertIndex = FindRestoreInsertIndex(Anim, SourceFrameIndex);
	InsertIndex = FMath::Clamp(InsertIndex, 0, KeyFrameCount);

	{
		FScopedFlipbookMutator Mutator(Flipbook);
		InsertIndex = FMath::Clamp(InsertIndex, 0, Mutator.KeyFrames.Num());
		Mutator.KeyFrames.Insert(ExcludedFrame.KeyFrame, InsertIndex);
	}

	Anim.CombatData.Frames.Insert(ExcludedFrame.FrameData, InsertIndex);
	FSpriteExtractionInfo RestoredInfo = ExcludedFrame.ExtractionInfo;
	RestoredInfo.bExcludedFromFlipbook = false;
	Anim.CombatData.FrameExtractionInfo.Insert(RestoredInfo, InsertIndex);
	if (Anim.MotionData.RootMotion.Num() > 0 || !ExcludedFrame.RootMotionData.Position.IsNearlyZero())
	{
		// If array was empty but excluded frame has data, initialize to pre-insert frame count (Frames already grew by 1 above)
		if (Anim.MotionData.RootMotion.Num() == 0)
		{
			Anim.MotionData.RootMotion.SetNum(Anim.CombatData.Frames.Num() - 1);
		}
		Anim.MotionData.RootMotion.Insert(ExcludedFrame.RootMotionData, FMath::Clamp(InsertIndex, 0, Anim.MotionData.RootMotion.Num()));
	}
	Anim.CombatData.ExcludedFrames.RemoveAt(ExcludedFrameIndex);

	NormalizeFrameSourceIndices(Anim);

	Flipbook->MarkPackageDirty();
	MarkPackageDirty();
	return true;
}

int32 UPaper2DPlusCharacterProfileAsset::RestoreAllExcludedFlipbookFrames(int32 FlipbookIndex)
{
	if (!Flipbooks.IsValidIndex(FlipbookIndex))
	{
		return 0;
	}

	FFlipbookProfileEntry& Anim = Flipbooks[FlipbookIndex];
	if (Anim.CombatData.ExcludedFrames.Num() == 0)
	{
		return 0;
	}

	UPaperFlipbook* Flipbook = Anim.Identity.Flipbook.LoadSynchronous();
	if (!Flipbook)
	{
		return 0;
	}

	// Does NOT call Modify() — callers manage transactions (consistent with
	// ExcludeFlipbookFrame / RestoreExcludedFlipbookFrame).
	const int32 KeyFrameCount = Flipbook->GetNumKeyFrames();
	if (Anim.CombatData.Frames.Num() != KeyFrameCount)
	{
		Anim.CombatData.Frames.SetNum(KeyFrameCount);
	}
	if (Anim.CombatData.FrameExtractionInfo.Num() != KeyFrameCount)
	{
		Anim.CombatData.FrameExtractionInfo.SetNum(KeyFrameCount);
	}
	if (Anim.MotionData.RootMotion.Num() > 0 && Anim.MotionData.RootMotion.Num() != KeyFrameCount)
	{
		Anim.MotionData.RootMotion.SetNum(KeyFrameCount);
	}

	NormalizeFrameSourceIndices(Anim);

	// Sort excluded frames by SourceFrameIndex so we insert highest-index first
	// (inserting from the back avoids shifting earlier insertion points).
	Anim.CombatData.ExcludedFrames.Sort([](const FExcludedFlipbookFrameData& A, const FExcludedFlipbookFrameData& B)
	{
		return A.ExtractionInfo.SourceFrameIndex > B.ExtractionInfo.SourceFrameIndex;
	});

	const int32 RestoredCount = Anim.CombatData.ExcludedFrames.Num();

	// Pre-determine if root motion handling is needed. Must sync the array to
	// Frames.Num() once BEFORE the loop — doing it inside the loop causes the
	// array to fall behind Frames by (N-1) after N insertions.
	bool bNeedsRootMotion = Anim.MotionData.RootMotion.Num() > 0;
	if (!bNeedsRootMotion)
	{
		for (const FExcludedFlipbookFrameData& EF : Anim.CombatData.ExcludedFrames)
		{
			if (!EF.RootMotionData.Position.IsNearlyZero())
			{
				bNeedsRootMotion = true;
				break;
			}
		}
	}
	if (bNeedsRootMotion && Anim.MotionData.RootMotion.Num() != Anim.CombatData.Frames.Num())
	{
		Anim.MotionData.RootMotion.SetNum(Anim.CombatData.Frames.Num());
	}

	{
		FScopedFlipbookMutator Mutator(Flipbook);

		// Insert all excluded frames back in one pass (highest SourceFrameIndex first).
		for (const FExcludedFlipbookFrameData& ExcludedFrame : Anim.CombatData.ExcludedFrames)
		{
			const int32 SourceIdx = ExcludedFrame.ExtractionInfo.SourceFrameIndex;
			int32 InsertIndex = FindRestoreInsertIndex(Anim, SourceIdx);
			InsertIndex = FMath::Clamp(InsertIndex, 0, Anim.CombatData.Frames.Num());

			Anim.CombatData.Frames.Insert(ExcludedFrame.FrameData, InsertIndex);
			FSpriteExtractionInfo RestoredInfo = ExcludedFrame.ExtractionInfo;
			RestoredInfo.bExcludedFromFlipbook = false;
			Anim.CombatData.FrameExtractionInfo.Insert(RestoredInfo, InsertIndex);
			if (bNeedsRootMotion)
			{
				Anim.MotionData.RootMotion.Insert(ExcludedFrame.RootMotionData, FMath::Clamp(InsertIndex, 0, Anim.MotionData.RootMotion.Num()));
			}

			const int32 KeyInsertIndex = FMath::Clamp(InsertIndex, 0, Mutator.KeyFrames.Num());
			Mutator.KeyFrames.Insert(ExcludedFrame.KeyFrame, KeyInsertIndex);
		}
	}

	Anim.CombatData.ExcludedFrames.Empty();
	NormalizeFrameSourceIndices(Anim);

	Flipbook->MarkPackageDirty();
	MarkPackageDirty();
	return RestoredCount;
}

int32 UPaper2DPlusCharacterProfileAsset::GetExcludedFlipbookFrameCount(int32 FlipbookIndex) const
{
	if (!Flipbooks.IsValidIndex(FlipbookIndex))
	{
		return 0;
	}

	return Flipbooks[FlipbookIndex].CombatData.ExcludedFrames.Num();
}

int32 UPaper2DPlusCharacterProfileAsset::MirrorHitboxesInRange(const FString& FlipbookName, int32 RangeStart, int32 RangeEnd, int32 PivotX)
{
	for (FFlipbookProfileEntry& Anim : Flipbooks)
	{
		if (!Anim.Identity.FlipbookName.Equals(FlipbookName, ESearchCase::IgnoreCase))
		{
			continue;
		}

		if (Anim.CombatData.Frames.Num() == 0)
		{
			return 0;
		}

		const int32 Start = FMath::Clamp(FMath::Min(RangeStart, RangeEnd), 0, Anim.CombatData.Frames.Num() - 1);
		const int32 End = FMath::Clamp(FMath::Max(RangeStart, RangeEnd), 0, Anim.CombatData.Frames.Num() - 1);

		int32 MirroredCount = 0;
		for (int32 FrameIndex = Start; FrameIndex <= End; ++FrameIndex)
		{
			for (FHitboxData& Hitbox : Anim.CombatData.Frames[FrameIndex].Hitboxes)
			{
				const int32 Right = Hitbox.X + Hitbox.Width;
				Hitbox.X = (2 * PivotX) - Right;
				MirroredCount++;
			}
			for (FSocketData& Socket : Anim.CombatData.Frames[FrameIndex].Sockets)
			{
				Socket.X = (2 * PivotX) - Socket.X;
			}
		}

		return MirroredCount;
	}

	return 0;
}


int32 UPaper2DPlusCharacterProfileAsset::SetSpriteFlipInRange(const FString& FlipbookName, int32 RangeStart, int32 RangeEnd, bool bInFlipX, bool bInFlipY)
{
	for (FFlipbookProfileEntry& Anim : Flipbooks)
	{
		if (!Anim.Identity.FlipbookName.Equals(FlipbookName, ESearchCase::IgnoreCase))
		{
			continue;
		}

		if (Anim.CombatData.Frames.Num() == 0)
		{
			return 0;
		}

		if (Anim.CombatData.FrameExtractionInfo.Num() < Anim.CombatData.Frames.Num())
		{
			Anim.CombatData.FrameExtractionInfo.SetNum(Anim.CombatData.Frames.Num());
		}

		const int32 Start = FMath::Clamp(FMath::Min(RangeStart, RangeEnd), 0, Anim.CombatData.Frames.Num() - 1);
		const int32 End = FMath::Clamp(FMath::Max(RangeStart, RangeEnd), 0, Anim.CombatData.Frames.Num() - 1);

		int32 UpdatedCount = 0;
		for (int32 FrameIndex = Start; FrameIndex <= End; ++FrameIndex)
		{
			if (!Anim.CombatData.FrameExtractionInfo.IsValidIndex(FrameIndex))
			{
				continue;
			}

			FSpriteExtractionInfo& Info = Anim.CombatData.FrameExtractionInfo[FrameIndex];
			Info.bFlipX = bInFlipX;
			Info.bFlipY = bInFlipY;
			UpdatedCount++;
		}

		return UpdatedCount;
	}

	return 0;
}

int32 UPaper2DPlusCharacterProfileAsset::SetSpriteFlipForFlipbook(const FString& FlipbookName, bool bInFlipX, bool bInFlipY)
{
	for (const FFlipbookProfileEntry& Anim : Flipbooks)
	{
		if (Anim.Identity.FlipbookName.Equals(FlipbookName, ESearchCase::IgnoreCase))
		{
			if (Anim.CombatData.Frames.Num() <= 0)
			{
				return 0;
			}
			return SetSpriteFlipInRange(FlipbookName, 0, Anim.CombatData.Frames.Num() - 1, bInFlipX, bInFlipY);
		}
	}

	return 0;
}

int32 UPaper2DPlusCharacterProfileAsset::SetSpriteFlipForAllFlipbooks(bool bInFlipX, bool bInFlipY)
{
	int32 TotalUpdated = 0;
	for (const FFlipbookProfileEntry& Anim : Flipbooks)
	{
		TotalUpdated += SetSpriteFlipForFlipbook(Anim.Identity.FlipbookName, bInFlipX, bInFlipY);
	}
	return TotalUpdated;
}

void UPaper2DPlusCharacterProfileAsset::MigrateEffectsToFrameEvents()
{
	// Convert legacy FFlipbookEffectData entries to UPaper2DPlusSpawnEffectFrameEvent
	// instances in the new FrameEventData array. Skip entries that already have
	// frame events (re-save safe).
	for (FFlipbookProfileEntry& Entry : Flipbooks)
	{
		if (Entry.FrameEventData.FrameEvents.Num() > 0 || Entry.Effects.Num() == 0) continue;
		for (const FFlipbookEffectData& OldEffect : Entry.Effects)
		{
			auto* NewEvent = NewObject<UPaper2DPlusSpawnEffectFrameEvent>(this, NAME_None, RF_Transactional);
			NewEvent->TriggerFrame = OldEffect.TriggerFrame;
			NewEvent->EffectFlipbook = OldEffect.EffectFlipbook;
			NewEvent->Offset = OldEffect.Offset;
			NewEvent->Rotation = OldEffect.Rotation;
			NewEvent->Scale = OldEffect.Scale;
			NewEvent->bFlipWithCharacter = OldEffect.bFlipWithCharacter;
			NewEvent->Tint = OldEffect.Color;
			Entry.FrameEventData.FrameEvents.Add(NewEvent);
		}
		Entry.Effects.Empty();
	}
}

void UPaper2DPlusCharacterProfileAsset::PruneOrphanedTagMappings()
{
	auto IsLive = [this](const FString& Name)
	{
		for (const FFlipbookProfileEntry& Anim : Flipbooks)
		{
			if (Anim.Identity.FlipbookName.Equals(Name, ESearchCase::IgnoreCase))
			{
				return true;
			}
		}
		return false;
	};

	for (auto& Pair : TagMappings)
	{
		FFlipbookTagMapping& Mapping = Pair.Value;
		for (int32 Index = Mapping.FlipbookNames.Num() - 1; Index >= 0; --Index)
		{
			if (!IsLive(Mapping.FlipbookNames[Index]))
			{
				Mapping.FlipbookNames.RemoveAt(Index);
				if (Mapping.PaperZDSequences.IsValidIndex(Index))
				{
					Mapping.PaperZDSequences.RemoveAt(Index);
				}
			}
		}
	}
	bTagLookupCacheValid = false;
}

bool UPaper2DPlusCharacterProfileAsset::MigrateSerializablePayloadToCurrentSchema(FCharacterProfileAssetSerializablePayload& InOutPayload)
{
	if (InOutPayload.SchemaVersion == CharacterProfileJsonSchemaVersion)
	{
		return true;
	}

	if (InOutPayload.SchemaVersion == CharacterProfileJsonLegacySchemaVersion)
	{
		// Legacy payloads predate explicit schema stamping. Treat as equivalent to schema v1.
		InOutPayload.SchemaVersion = 1;
	}

	if (InOutPayload.SchemaVersion == 1)
	{
		// v1 -> v2: TagMappings didn't exist. Initialize empty (already default).
		InOutPayload.SchemaVersion = 2;
	}

	if (InOutPayload.SchemaVersion == 2)
	{
		// v2 -> v3: Removed UniformDimensions, bUseUniformDimensions, UniformAnchor, SpriteDimensions.
		// Old JSON payloads may carry these keys — FJsonObjectConverter silently ignores unknown keys.
		InOutPayload.SchemaVersion = 3;
	}

	if (InOutPayload.SchemaVersion == 3)
	{
		// v3 -> v4: Added FlipbookGroups array and per-flipbook FlipbookGroup field.
		// Default empty — all flipbooks appear in Ungrouped. No data to migrate.
		InOutPayload.SchemaVersion = 4;
	}

	if (InOutPayload.SchemaVersion == 4)
	{
		// v4 -> v5: Added non-destructive excluded frame storage and source frame ordering metadata.
		// New fields default automatically; no explicit migration required.
		InOutPayload.SchemaVersion = 5;
	}

	if (InOutPayload.SchemaVersion == 5)
	{
		// v5 -> v6: Added animation phase ranges (Startup/Active/Recovery) on FFlipbookProfileEntry.
		// FAnimationPhaseRange fields default to INDEX_NONE. No explicit migration needed.
		InOutPayload.SchemaVersion = 6;
	}

	// v6 → v7: Phase ranges removed from flipbooks, replaced by PhaseGroups on asset.
	// No automatic migration — old phase ranges are dropped, groups start empty.
	if (InOutPayload.SchemaVersion == 6)
	{
		InOutPayload.SchemaVersion = 7;
	}

	return InOutPayload.SchemaVersion == CharacterProfileJsonSchemaVersion;
}

bool UPaper2DPlusCharacterProfileAsset::ExportToJsonString(FString& OutJson) const
{
	FCharacterProfileAssetSerializablePayload Payload;
	Payload.SchemaVersion = CharacterProfileJsonSchemaVersion;
	Payload.DisplayName = DisplayName;
	Payload.Flipbooks = Flipbooks;
	Payload.DefaultAlphaThreshold = DefaultAlphaThreshold;
	Payload.DefaultPadding = DefaultPadding;
	Payload.DefaultMinSpriteSize = DefaultMinSpriteSize;

	// Serialize TagMappings as array of key-value pairs (avoids TMap<FGameplayTag> JSON issues)
	Payload.GroupBindings.Reserve(TagMappings.Num());
	for (const auto& Pair : TagMappings)
	{
		FSerializableTagMapping Entry;
		Entry.Tag = Pair.Key.ToString();
		Entry.Binding = Pair.Value;
		Payload.GroupBindings.Add(MoveTemp(Entry));
	}

	// Serialize FlipbookGroups (lives on asset class, not on FFlipbookProfileEntry)
	Payload.FlipbookGroups = FlipbookGroups;

	// Serialize PhaseGroups
	Payload.PhaseGroups = PhaseGroups;

	const bool bConverted = FJsonObjectConverter::UStructToJsonObjectString(
		Payload,
		OutJson,
		0,
		0,
		0,
		nullptr,
		false // pretty-print disabled for deterministic compact output
	);

	return bConverted;
}

bool UPaper2DPlusCharacterProfileAsset::ImportFromJsonString(const FString& JsonString)
{
	FCharacterProfileAssetSerializablePayload Payload;
	if (!FJsonObjectConverter::JsonObjectStringToUStruct<FCharacterProfileAssetSerializablePayload>(JsonString, &Payload, 0, 0))
	{
		return false;
	}

	if (Payload.SchemaVersion > CharacterProfileJsonSchemaVersion)
	{
		return false;
	}

	if (!MigrateSerializablePayloadToCurrentSchema(Payload))
	{
		return false;
	}

	Modify();

	DisplayName = Payload.DisplayName;
	Flipbooks = Payload.Flipbooks;
	DefaultAlphaThreshold = Payload.DefaultAlphaThreshold;
	DefaultPadding = Payload.DefaultPadding;
	DefaultMinSpriteSize = Payload.DefaultMinSpriteSize;

	// Restore TagMappings from serialized array
	TagMappings.Empty();
	for (const FSerializableTagMapping& Entry : Payload.GroupBindings)
	{
		FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(*Entry.Tag), false);
		if (Tag.IsValid())
		{
			TagMappings.Add(Tag, Entry.Binding);
		}
	}

	// Restore FlipbookGroups
	FlipbookGroups = Payload.FlipbookGroups;

	// Referential integrity: orphaned FlipbookGroup values on flipbooks → set to NAME_None
	TSet<FName> ValidGroupNames;
	for (const FFlipbookGroupInfo& Group : FlipbookGroups)
	{
		ValidGroupNames.Add(Group.GroupName);
	}
	for (FFlipbookProfileEntry& Anim : Flipbooks)
	{
		if (Anim.FlipbookGroup != NAME_None && !ValidGroupNames.Contains(Anim.FlipbookGroup))
		{
			Anim.FlipbookGroup = NAME_None;
		}
	}

	// Restore PhaseGroups
	PhaseGroups = Payload.PhaseGroups;

	// Mirror PostLoad migrations so imports reach the same end state
	MigrateEffectsToFrameEvents();
	PruneOrphanedTagMappings();

	bFlipbookLookupCacheValid = false;
	bNameLookupCacheValid = false;
	bTagLookupCacheValid = false;

	MarkPackageDirty();
	return true;
}

bool UPaper2DPlusCharacterProfileAsset::ExportToJsonFile(const FString& FilePath) const
{
	if (FilePath.IsEmpty())
	{
		return false;
	}

	FString JsonString;
	if (!ExportToJsonString(JsonString))
	{
		return false;
	}

	return FFileHelper::SaveStringToFile(JsonString, *FilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

bool UPaper2DPlusCharacterProfileAsset::ImportFromJsonFile(const FString& FilePath)
{
	if (FilePath.IsEmpty())
	{
		return false;
	}

	FString JsonString;
	if (!FFileHelper::LoadFileToString(JsonString, *FilePath))
	{
		return false;
	}

	return ImportFromJsonString(JsonString);
}


// ==========================================
// PHASE GROUP QUERIES
// ==========================================

EAnimationPhase UPaper2DPlusCharacterProfileAsset::GetPhaseForFlipbook(const FString& FlipbookName) const
{
	for (const FPhaseGroup& Group : PhaseGroups)
	{
		EAnimationPhase Phase = Group.GetPhaseForFlipbook(FlipbookName);
		if (Phase != EAnimationPhase::None) return Phase;
	}
	return EAnimationPhase::None;
}

FString UPaper2DPlusCharacterProfileAsset::GetPhaseGroupNameForFlipbook(const FString& FlipbookName) const
{
	const FPhaseGroup* Group = FindPhaseGroupForFlipbook(FlipbookName);
	return Group ? Group->GroupName : FString();
}

TArray<FString> UPaper2DPlusCharacterProfileAsset::GetPhaseGroupNames() const
{
	TArray<FString> Names;
	Names.Reserve(PhaseGroups.Num());
	for (const FPhaseGroup& Group : PhaseGroups)
	{
		Names.Add(Group.GroupName);
	}
	Names.Sort();
	return Names;
}

FString UPaper2DPlusCharacterProfileAsset::GetFlipbookForPhaseInGroup(const FString& GroupName, EAnimationPhase Phase) const
{
	const FPhaseGroup* Group = FindPhaseGroup(GroupName);
	return Group ? Group->GetFlipbookForPhase(Phase) : FString();
}

UPaperZDAnimSequence* UPaper2DPlusCharacterProfileAsset::GetPaperZDSequenceForPhaseInGroup(const FString& GroupName, EAnimationPhase Phase) const
{
	const FPhaseGroup* Group = FindPhaseGroup(GroupName);
	return Group ? Group->GetSequenceForPhase(Phase) : nullptr;
}

bool UPaper2DPlusCharacterProfileAsset::GetPhaseData(const FString& GroupName, EAnimationPhase Phase, UPaperFlipbook*& OutFlipbook, UPaperZDAnimSequence*& OutPaperZDSequence) const
{
	OutFlipbook = nullptr;
	OutPaperZDSequence = nullptr;

	const FPhaseGroup* Group = FindPhaseGroup(GroupName);
	if (!Group) return false;

	const FString& FBName = Group->GetFlipbookForPhase(Phase);
	if (FBName.IsEmpty()) return false;

	const FFlipbookProfileEntry* Data = FindFlipbookDataPtr(FBName);
	if (Data)
	{
		OutFlipbook = Data->Identity.Flipbook.LoadSynchronous();
	}

	OutPaperZDSequence = Group->GetSequenceForPhase(Phase);
	return OutFlipbook != nullptr;
}

const FPhaseGroup* UPaper2DPlusCharacterProfileAsset::FindPhaseGroup(const FString& GroupName) const
{
	for (const FPhaseGroup& Group : PhaseGroups)
	{
		if (Group.GroupName == GroupName) return &Group;
	}
	return nullptr;
}

FPhaseGroup* UPaper2DPlusCharacterProfileAsset::FindPhaseGroupMutable(const FString& GroupName)
{
	for (FPhaseGroup& Group : PhaseGroups)
	{
		if (Group.GroupName == GroupName) return &Group;
	}
	return nullptr;
}

const FPhaseGroup* UPaper2DPlusCharacterProfileAsset::FindPhaseGroupForFlipbook(const FString& FlipbookName) const
{
	for (const FPhaseGroup& Group : PhaseGroups)
	{
		if (Group.GetPhaseForFlipbook(FlipbookName) != EAnimationPhase::None)
		{
			return &Group;
		}
	}
	return nullptr;
}

// ==========================================
// PAPERZD AUTO-RESOLVE
// ==========================================

UPaperZDAnimSequence* UPaper2DPlusCharacterProfileAsset::FindPaperZDSequenceForFlipbook(UPaperFlipbook* Flipbook) const
{
	if (!Flipbook || PaperZDAnimSource.IsNull()) return nullptr;

	UObject* AnimSource = PaperZDAnimSource.LoadSynchronous();
	if (!AnimSource) return nullptr;

	// Find PaperZDAnimSequence_Flipbook class via reflection
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 1)
	UClass* SeqClass = UClass::TryFindTypeSlow<UClass>(TEXT("PaperZDAnimSequence_Flipbook"));
#else
	UClass* SeqClass = FindObject<UClass>(ANY_PACKAGE, TEXT("PaperZDAnimSequence_Flipbook"));
#endif
	if (!SeqClass) return nullptr;

	// Query asset registry for all sequences belonging to this AnimSource
	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	FARFilter Filter;
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
	Filter.ClassNames.Add(SeqClass->GetFName());
#else
	Filter.ClassPaths.Add(SeqClass->GetClassPathName());
#endif
	Filter.bRecursiveClasses = true;

	TArray<FAssetData> SequenceAssets;
	AssetRegistry.GetAssets(Filter, SequenceAssets);

	const FString DesiredSourcePath = PaperZDAnimSource.ToSoftObjectPath().ToString();

	for (const FAssetData& AssetData : SequenceAssets)
	{
		// Filter by AnimSource tag (avoid loading every sequence)
		FAssetDataTagMapSharedView::FFindTagResult TagResult = AssetData.TagsAndValues.FindTag(FName("AnimSource"));
		if (!TagResult.IsSet()) continue;

		FString TagObjectPath = FPackageName::ExportTextPathToObjectPath(TagResult.GetValue());
		if (TagObjectPath != DesiredSourcePath) continue;

		// Load and check primary flipbook via reflection
		UObject* Seq = AssetData.GetAsset();
		if (!Seq) continue;

		// Access AnimData array via reflection: TArray<FPaperZDFlipbookAnimDataSource>
		FArrayProperty* AnimDataProp = FindFProperty<FArrayProperty>(Seq->GetClass(), TEXT("AnimData"));
		if (!AnimDataProp) continue;

		FScriptArrayHelper ArrayHelper(AnimDataProp, AnimDataProp->ContainerPtrToValuePtr<void>(Seq));
		if (ArrayHelper.Num() == 0) continue;

		// Get the Animation property (TObjectPtr<UPaperFlipbook>) from the first element
		FStructProperty* InnerStruct = CastField<FStructProperty>(AnimDataProp->Inner);
		if (!InnerStruct) continue;

		FObjectProperty* AnimProp = FindFProperty<FObjectProperty>(InnerStruct->Struct, TEXT("Animation"));
		if (!AnimProp) continue;

		UObject* PrimaryFlipbook = AnimProp->GetObjectPropertyValue(AnimProp->ContainerPtrToValuePtr<void>(ArrayHelper.GetRawPtr(0)));
		if (PrimaryFlipbook == Flipbook)
		{
			return Cast<UPaperZDAnimSequence>(Seq);
		}
	}

	return nullptr;
}

void UPaper2DPlusCharacterProfileAsset::AutoPopulatePaperZDSequences()
{
	if (PaperZDAnimSource.IsNull()) return;

	for (FFlipbookProfileEntry& Anim : Flipbooks)
	{
		// Skip if already assigned
		if (Anim.Identity.PaperZDSequence) continue;

		UPaperFlipbook* FB = Anim.Identity.Flipbook.LoadSynchronous();
		if (!FB) continue;

		Anim.Identity.PaperZDSequence = FindPaperZDSequenceForFlipbook(FB);
	}
}


bool UPaper2DPlusCharacterProfileAsset::ValidateCharacterProfileAsset(TArray<FCharacterProfileValidationIssue>& OutIssues) const
{
	OutIssues.Reset();

	auto AddIssue = [&OutIssues](ECharacterProfileValidationSeverity Severity, const FString& Context, const FString& Message)
	{
		FCharacterProfileValidationIssue Issue;
		Issue.Severity = Severity;
		Issue.Context = Context;
		Issue.Message = Message;
		OutIssues.Add(Issue);
	};

	TSet<FString> FlipbookNames;
	for (int32 FlipbookIndex = 0; FlipbookIndex < Flipbooks.Num(); ++FlipbookIndex)
	{
		const FFlipbookProfileEntry& Anim = Flipbooks[FlipbookIndex];
		const FString AnimLabel = FString::Printf(TEXT("Flipbook[%d] '%s'"), FlipbookIndex, *Anim.Identity.FlipbookName);

		if (Anim.Identity.FlipbookName.TrimStartAndEnd().IsEmpty())
		{
			AddIssue(ECharacterProfileValidationSeverity::Warning, AnimLabel, TEXT("Flipbook name is empty. Rename it in the Overview tab."));
		}
		else
		{
			const FString Normalized = Anim.Identity.FlipbookName.ToLower();
			if (FlipbookNames.Contains(Normalized))
			{
				AddIssue(ECharacterProfileValidationSeverity::Error, AnimLabel, TEXT("Duplicate flipbook name detected. Rename one of the duplicates in the Overview tab."));
			}
			FlipbookNames.Add(Normalized);
		}

		const int32 FrameCount = Anim.CombatData.Frames.Num();

		if (!Anim.Identity.Flipbook.IsNull())
		{
			if (UPaperFlipbook* Flipbook = Anim.Identity.Flipbook.LoadSynchronous())
			{
				const int32 FlipbookFrameCount = Flipbook->GetNumKeyFrames();
				if (FlipbookFrameCount != FrameCount)
				{
					AddIssue(ECharacterProfileValidationSeverity::Warning, AnimLabel,
						FString::Printf(TEXT("Frames count (%d) differs from Flipbook keyframe count (%d). Open in the editor and re-save to sync."), FrameCount, FlipbookFrameCount));
				}
			}
			else
			{
				AddIssue(ECharacterProfileValidationSeverity::Error, AnimLabel, TEXT("Flipbook reference could not be loaded — asset may be missing or corrupted. Re-assign the flipbook in the Overview tab."));
			}
		}

		if (Anim.MotionData.RootMotion.Num() > 0 && Anim.MotionData.RootMotion.Num() != FrameCount)
		{
			AddIssue(ECharacterProfileValidationSeverity::Warning, AnimLabel,
				FString::Printf(TEXT("RootMotion count (%d) does not match Frames count (%d). Open in the editor and re-save to sync."), Anim.MotionData.RootMotion.Num(), FrameCount));
		}

		for (int32 FrameIndex = 0; FrameIndex < Anim.CombatData.Frames.Num(); ++FrameIndex)
		{
			const FFrameHitboxData& Frame = Anim.CombatData.Frames[FrameIndex];
			const FString FrameLabel = FString::Printf(TEXT("%s Frame[%d] '%s'"), *AnimLabel, FrameIndex, *Frame.FrameName);

			for (int32 HitboxIndex = 0; HitboxIndex < Frame.Hitboxes.Num(); ++HitboxIndex)
			{
				const FHitboxData& Hitbox = Frame.Hitboxes[HitboxIndex];
				if (Hitbox.Width <= 0 || Hitbox.Height <= 0)
				{
					AddIssue(ECharacterProfileValidationSeverity::Error, FrameLabel,
						FString::Printf(TEXT("Hitbox[%d] has invalid size %dx%d. Resize or delete it in the Hitbox Editor tab."), HitboxIndex, Hitbox.Width, Hitbox.Height));
				}
			}

			TSet<FString> SocketNames;
			for (int32 SocketIndex = 0; SocketIndex < Frame.Sockets.Num(); ++SocketIndex)
			{
				const FSocketData& Socket = Frame.Sockets[SocketIndex];
				if (Socket.Name.TrimStartAndEnd().IsEmpty())
				{
					AddIssue(ECharacterProfileValidationSeverity::Warning, FrameLabel,
						FString::Printf(TEXT("Socket[%d] has empty name. Name it in the Hitbox Editor tab or delete it."), SocketIndex));
					continue;
				}

				const FString NormalizedSocket = Socket.Name.ToLower();
				if (SocketNames.Contains(NormalizedSocket))
				{
					AddIssue(ECharacterProfileValidationSeverity::Warning, FrameLabel,
						FString::Printf(TEXT("Duplicate socket name '%s' on frame. Rename one in the Hitbox Editor tab."), *Socket.Name));
				}
				SocketNames.Add(NormalizedSocket);
			}
		}

		// Validate frame events
		for (int32 EventIndex = 0; EventIndex < Anim.FrameEventData.FrameEvents.Num(); ++EventIndex)
		{
			const UPaper2DPlusFrameEventBase* Event = Anim.FrameEventData.FrameEvents[EventIndex];
			const FString EventLabel = FString::Printf(TEXT("%s FrameEvent[%d]"), *AnimLabel, EventIndex);

			if (!Event)
			{
				AddIssue(ECharacterProfileValidationSeverity::Error, EventLabel,
					TEXT("Frame event slot is null. Remove it or assign a class in the Frame Events tab."));
				continue;
			}

			const UPaper2DPlusFrameEvent* OneShot = Cast<UPaper2DPlusFrameEvent>(Event);
			const UPaper2DPlusFrameEventState* Ranged = Cast<UPaper2DPlusFrameEventState>(Event);

			if (OneShot && FrameCount > 0 && OneShot->TriggerFrame >= FrameCount)
			{
				AddIssue(ECharacterProfileValidationSeverity::Warning, EventLabel,
					FString::Printf(TEXT("TriggerFrame %d is out of bounds (max %d). Adjust it in the Frame Events tab."),
						OneShot->TriggerFrame, FrameCount - 1));
			}

			if (Ranged)
			{
				if (FrameCount > 0 && Ranged->StartFrame >= FrameCount)
				{
					AddIssue(ECharacterProfileValidationSeverity::Warning, EventLabel,
						FString::Printf(TEXT("StartFrame %d is out of bounds (max %d). Adjust it in the Frame Events tab."),
							Ranged->StartFrame, FrameCount - 1));
				}
				if (Ranged->FrameCount < 1)
				{
					AddIssue(ECharacterProfileValidationSeverity::Warning, EventLabel,
						TEXT("FrameCount < 1; the ranged event will never fire. Set FrameCount in the Frame Events tab."));
				}
			}

			if (const UPaper2DPlusSpawnEffectFrameEvent* Spawn = Cast<UPaper2DPlusSpawnEffectFrameEvent>(Event))
			{
				if (!Spawn->EffectFlipbook)
				{
					AddIssue(ECharacterProfileValidationSeverity::Warning, EventLabel,
						TEXT("Spawn-effect event has no EffectFlipbook assigned. Assign one in the Frame Events tab."));
				}
				if (FMath::IsNearlyZero(Spawn->Scale.X) || FMath::IsNearlyZero(Spawn->Scale.Y))
				{
					AddIssue(ECharacterProfileValidationSeverity::Warning, EventLabel,
						TEXT("Spawn-effect event has zero scale — the effect won't be visible."));
				}
			}
		}
	}

	// Validate tag mappings
	for (const auto& Pair : TagMappings)
	{
		const FString TagLabel = FString::Printf(TEXT("Tag Mapping '%s'"), *Pair.Key.ToString());

		if (!Pair.Key.IsValid())
		{
			AddIssue(ECharacterProfileValidationSeverity::Warning, TagLabel, TEXT("Tag is empty or invalid. Remove it in the Tag Mappings panel."));
		}

		for (int32 i = 0; i < Pair.Value.FlipbookNames.Num(); ++i)
		{
			const FString& AnimName = Pair.Value.FlipbookNames[i];
			if (!AnimName.IsEmpty() && !FlipbookNames.Contains(AnimName.ToLower()))
			{
				AddIssue(ECharacterProfileValidationSeverity::Warning, TagLabel,
					FString::Printf(TEXT("Flipbook '%s' is not found in this asset. Remove it from the tag or add the flipbook to the asset."), *AnimName));
			}
		}
	}

	// Check for unmapped required tag mappings
	if (const UPaper2DPlusSettings* Settings = UPaper2DPlusSettings::Get())
	{
		for (const FGameplayTag& RequiredTag : Settings->RequiredTagMappings)
		{
			if (RequiredTag.IsValid() && !TagMappings.Contains(RequiredTag))
			{
				AddIssue(ECharacterProfileValidationSeverity::Warning,
					FString::Printf(TEXT("Tag Mapping '%s'"), *RequiredTag.ToString()),
					TEXT("Required tag mapping is not mapped. Drag a flipbook onto this tag in the Tag Mappings panel."));
			}
		}
	}

	for (const FCharacterProfileValidationIssue& Issue : OutIssues)
	{
		if (Issue.Severity == ECharacterProfileValidationSeverity::Error)
		{
			return false;
		}
	}
	return true;
}

int32 UPaper2DPlusCharacterProfileAsset::TrimTrailingFrameData(int32 FlipbookIndex)
{
	if (!Flipbooks.IsValidIndex(FlipbookIndex))
	{
		return 0;
	}

	FFlipbookProfileEntry& Anim = Flipbooks[FlipbookIndex];
	UPaperFlipbook* Flipbook = Anim.Identity.Flipbook.LoadSynchronous();
	if (!Flipbook)
	{
		return 0;
	}

	const int32 TargetCount = FMath::Max(0, Flipbook->GetNumKeyFrames());
	int32 RemovedCount = 0;

	if (Anim.CombatData.Frames.Num() > TargetCount)
	{
		RemovedCount += (Anim.CombatData.Frames.Num() - TargetCount);
		Anim.CombatData.Frames.SetNum(TargetCount);
	}

	if (Anim.CombatData.FrameExtractionInfo.Num() > TargetCount)
	{
		RemovedCount += (Anim.CombatData.FrameExtractionInfo.Num() - TargetCount);
		Anim.CombatData.FrameExtractionInfo.SetNum(TargetCount);
	}

	if (Anim.MotionData.RootMotion.Num() > TargetCount)
	{
		RemovedCount += (Anim.MotionData.RootMotion.Num() - TargetCount);
		Anim.MotionData.RootMotion.SetNum(TargetCount);
	}

	if (RemovedCount > 0)
	{
		bFlipbookLookupCacheValid = false;
	}

	return RemovedCount;
}

int32 UPaper2DPlusCharacterProfileAsset::TrimAllTrailingFrameData()
{
	int32 TotalRemoved = 0;
	for (int32 Index = 0; Index < Flipbooks.Num(); ++Index)
	{
		TotalRemoved += TrimTrailingFrameData(Index);
	}
	return TotalRemoved;
}

// ==========================================
// TAG MAPPING LOOKUPS
// ==========================================

TArray<FFlipbookProfileEntry> UPaper2DPlusCharacterProfileAsset::GetFlipbookDataForTag(FGameplayTag Group) const
{
	TArray<FFlipbookProfileEntry> Result;
	if (!bTagLookupCacheValid)
	{
		RebuildTagLookupCache();
	}

	if (const TArray<int32>* Indices = TagToFlipbookIndicesCache.Find(Group))
	{
		Result.Reserve(Indices->Num());
		for (int32 Index : *Indices)
		{
			if (Flipbooks.IsValidIndex(Index))
			{
				Result.Add(Flipbooks[Index]);
			}
		}
	}
	return Result;
}

TArray<UPaperFlipbook*> UPaper2DPlusCharacterProfileAsset::GetFlipbooksForTag(FGameplayTag Group) const
{
	TArray<UPaperFlipbook*> Result;
	if (!bTagLookupCacheValid)
	{
		RebuildTagLookupCache();
	}

	if (const TArray<int32>* Indices = TagToFlipbookIndicesCache.Find(Group))
	{
		Result.Reserve(Indices->Num());
		for (int32 Index : *Indices)
		{
			if (Flipbooks.IsValidIndex(Index) && !Flipbooks[Index].Identity.Flipbook.IsNull())
			{
				if (UPaperFlipbook* FB = Flipbooks[Index].Identity.Flipbook.LoadSynchronous())
				{
					Result.Add(FB);
				}
			}
		}
	}
	return Result;
}

UPaperFlipbook* UPaper2DPlusCharacterProfileAsset::GetFirstFlipbookForTag(FGameplayTag Group) const
{
	if (!bTagLookupCacheValid)
	{
		RebuildTagLookupCache();
	}

	if (const TArray<int32>* Indices = TagToFlipbookIndicesCache.Find(Group))
	{
		for (int32 Index : *Indices)
		{
			if (Flipbooks.IsValidIndex(Index) && !Flipbooks[Index].Identity.Flipbook.IsNull())
			{
				if (UPaperFlipbook* FB = Flipbooks[Index].Identity.Flipbook.LoadSynchronous())
				{
					return FB;
				}
			}
		}
	}
	return nullptr;
}

UPaperFlipbook* UPaper2DPlusCharacterProfileAsset::GetRandomFlipbookForTag(FGameplayTag Group) const
{
	TArray<UPaperFlipbook*> TagFlipbooks = GetFlipbooksForTag(Group);
	if (TagFlipbooks.Num() == 0)
	{
		return nullptr;
	}
	return TagFlipbooks[FMath::RandRange(0, TagFlipbooks.Num() - 1)];
}

UPaperZDAnimSequence* UPaper2DPlusCharacterProfileAsset::GetPaperZDSequenceForTag(FGameplayTag Group, int32 ComboIndex) const
{
	if (const FFlipbookTagMapping* Binding = TagMappings.Find(Group))
	{
		if (Binding->PaperZDSequences.IsValidIndex(ComboIndex))
		{
			return Binding->PaperZDSequences[ComboIndex];
		}
	}
	return nullptr;
}

UObject* UPaper2DPlusCharacterProfileAsset::GetTagMappingMetadata(FGameplayTag Group, FName Key) const
{
	if (const FFlipbookTagMapping* Binding = TagMappings.Find(Group))
	{
		if (const TSoftObjectPtr<UObject>* SoftRef = Binding->Metadata.Find(Key))
		{
			return SoftRef->LoadSynchronous();
		}
	}
	return nullptr;
}

TArray<FName> UPaper2DPlusCharacterProfileAsset::GetTagMappingMetadataKeys(FGameplayTag Group) const
{
	TArray<FName> Keys;
	if (const FFlipbookTagMapping* Binding = TagMappings.Find(Group))
	{
		Binding->Metadata.GetKeys(Keys);
	}
	return Keys;
}

bool UPaper2DPlusCharacterProfileAsset::HasTagMappingMetadata(FGameplayTag Group, FName Key) const
{
	if (const FFlipbookTagMapping* Binding = TagMappings.Find(Group))
	{
		return Binding->Metadata.Contains(Key);
	}
	return false;
}

bool UPaper2DPlusCharacterProfileAsset::GetTagMapping(FGameplayTag Group, FFlipbookTagMapping& OutBinding) const
{
	if (const FFlipbookTagMapping* Binding = TagMappings.Find(Group))
	{
		OutBinding = *Binding;
		return true;
	}
	return false;
}

bool UPaper2DPlusCharacterProfileAsset::HasTagMapping(FGameplayTag Group) const
{
	return TagMappings.Contains(Group);
}

TArray<FGameplayTag> UPaper2DPlusCharacterProfileAsset::GetAllMappedTags() const
{
	TArray<FGameplayTag> Tags;
	TagMappings.GetKeys(Tags);
	return Tags;
}

int32 UPaper2DPlusCharacterProfileAsset::GetFlipbookCountForTag(FGameplayTag Group) const
{
	if (const FFlipbookTagMapping* Binding = TagMappings.Find(Group))
	{
		return Binding->FlipbookNames.Num();
	}
	return 0;
}

// ==========================================
// ATTACK BOUNDS (AI HELPERS)
// ==========================================

namespace
{
	/** Compute max distance from origin to any edge of attack hitboxes across all frames. */
	float ComputeAttackRangeForAnimData(const FFlipbookProfileEntry& AnimData)
	{
		float MaxRange = 0.0f;
		for (const FFrameHitboxData& Frame : AnimData.CombatData.Frames)
		{
			for (const FHitboxData& Hitbox : Frame.Hitboxes)
			{
				if (Hitbox.Type != EHitboxType::Attack) continue;

				// Max horizontal extent from origin (accounts for both sides)
				const float Left = FMath::Abs(static_cast<float>(Hitbox.X));
				const float Right = FMath::Abs(static_cast<float>(Hitbox.X + Hitbox.Width));
				const float HorizMax = FMath::Max(Left, Right);

				// Max vertical extent from origin
				const float Top = FMath::Abs(static_cast<float>(Hitbox.Y));
				const float Bottom = FMath::Abs(static_cast<float>(Hitbox.Y + Hitbox.Height));
				const float VertMax = FMath::Max(Top, Bottom);

				// Euclidean distance — the actual max reach
				const float Range = FMath::Sqrt(HorizMax * HorizMax + VertMax * VertMax);
				MaxRange = FMath::Max(MaxRange, Range);
			}
		}
		return MaxRange;
	}

	/** Compute combined FBox2D of all attack hitboxes across all frames. */
	FBox2D ComputeAttackBoundsForAnimData(const FFlipbookProfileEntry& AnimData)
	{
		FBox2D Bounds(ForceInit);
		bool bHasAny = false;

		for (const FFrameHitboxData& Frame : AnimData.CombatData.Frames)
		{
			for (const FHitboxData& Hitbox : Frame.Hitboxes)
			{
				if (Hitbox.Type != EHitboxType::Attack) continue;

				const FVector2D Min(static_cast<float>(Hitbox.X), static_cast<float>(Hitbox.Y));
				const FVector2D Max(static_cast<float>(Hitbox.X + Hitbox.Width), static_cast<float>(Hitbox.Y + Hitbox.Height));

				if (!bHasAny)
				{
					Bounds = FBox2D(Min, Max);
					bHasAny = true;
				}
				else
				{
					Bounds += FBox2D(Min, Max);
				}
			}
		}
		return Bounds;
	}
}

float UPaper2DPlusCharacterProfileAsset::GetMaxAttackRange() const
{
	float MaxRange = 0.0f;
	for (const FFlipbookProfileEntry& Anim : Flipbooks)
	{
		MaxRange = FMath::Max(MaxRange, ComputeAttackRangeForAnimData(Anim));
	}
	return MaxRange;
}

float UPaper2DPlusCharacterProfileAsset::GetAttackRangeForTag(FGameplayTag Group) const
{
	float MaxRange = 0.0f;
	if (const FFlipbookTagMapping* Binding = TagMappings.Find(Group))
	{
		for (const FString& AnimName : Binding->FlipbookNames)
		{
			if (const FFlipbookProfileEntry* Anim = FindFlipbookDataPtr(AnimName))
			{
				MaxRange = FMath::Max(MaxRange, ComputeAttackRangeForAnimData(*Anim));
			}
		}
	}
	return MaxRange;
}

float UPaper2DPlusCharacterProfileAsset::GetAttackRangeForFlipbook(const FString& FlipbookName) const
{
	if (const FFlipbookProfileEntry* Anim = FindFlipbookDataPtr(FlipbookName))
	{
		return ComputeAttackRangeForAnimData(*Anim);
	}
	return 0.0f;
}

FBox2D UPaper2DPlusCharacterProfileAsset::GetAttackBoundsForTag(FGameplayTag Group) const
{
	FBox2D Bounds(ForceInit);
	bool bHasAny = false;

	if (const FFlipbookTagMapping* Binding = TagMappings.Find(Group))
	{
		for (const FString& AnimName : Binding->FlipbookNames)
		{
			if (const FFlipbookProfileEntry* Anim = FindFlipbookDataPtr(AnimName))
			{
				FBox2D AnimBounds = ComputeAttackBoundsForAnimData(*Anim);
				if (AnimBounds.bIsValid)
				{
					if (!bHasAny)
					{
						Bounds = AnimBounds;
						bHasAny = true;
					}
					else
					{
						Bounds += AnimBounds;
					}
				}
			}
		}
	}
	return Bounds;
}

FBox2D UPaper2DPlusCharacterProfileAsset::GetAttackBoundsForFlipbook(const FString& FlipbookName) const
{
	if (const FFlipbookProfileEntry* Anim = FindFlipbookDataPtr(FlipbookName))
	{
		return ComputeAttackBoundsForAnimData(*Anim);
	}
	return FBox2D(ForceInit);
}

// ==========================================
// TAG MAPPING HELPERS
// ==========================================

void UPaper2DPlusCharacterProfileAsset::RebuildTagLookupCache() const
{
	TagToFlipbookIndicesCache.Empty();

	if (!bNameLookupCacheValid || Flipbooks.Num() != NameToFlipbookIndexCache.Num())
	{
		RebuildNameLookupCache();
	}

	for (const auto& Pair : TagMappings)
	{
		TArray<int32> Indices;
		for (const FString& AnimName : Pair.Value.FlipbookNames)
		{
			if (const int32* FoundIndex = NameToFlipbookIndexCache.Find(AnimName.ToLower()))
			{
				if (Flipbooks.IsValidIndex(*FoundIndex))
				{
					Indices.Add(*FoundIndex);
				}
			}
		}
		TagToFlipbookIndicesCache.Add(Pair.Key, MoveTemp(Indices));
	}

	bTagLookupCacheValid = true;
}

void UPaper2DPlusCharacterProfileAsset::UpdateTagMappingFlipbookName(const FString& OldName, const FString& NewName)
{
	for (auto& Pair : TagMappings)
	{
		for (FString& AnimName : Pair.Value.FlipbookNames)
		{
			if (AnimName.Equals(OldName, ESearchCase::IgnoreCase))
			{
				AnimName = NewName;
			}
		}
	}
	bTagLookupCacheValid = false;
}

void UPaper2DPlusCharacterProfileAsset::RemoveFlipbookFromTagMappings(const FString& FlipbookName)
{
	for (auto& Pair : TagMappings)
	{
		FFlipbookTagMapping& Mapping = Pair.Value;
		// FlipbookNames and PaperZDSequences are parallel arrays — must remove matching indices together.
		for (int32 Index = Mapping.FlipbookNames.Num() - 1; Index >= 0; --Index)
		{
			if (Mapping.FlipbookNames[Index].Equals(FlipbookName, ESearchCase::IgnoreCase))
			{
				Mapping.FlipbookNames.RemoveAt(Index);
				if (Mapping.PaperZDSequences.IsValidIndex(Index))
				{
					Mapping.PaperZDSequences.RemoveAt(Index);
				}
			}
		}
	}
	bTagLookupCacheValid = false;
}

// ==========================================
// FLIPBOOK GROUP HELPERS
// ==========================================

bool UPaper2DPlusCharacterProfileAsset::HasFlipbookGroup(FName Name) const
{
	for (const FFlipbookGroupInfo& Group : FlipbookGroups)
	{
		if (Group.GroupName == Name)
		{
			return true;
		}
	}
	return false;
}

TMap<FName, TArray<const FFlipbookGroupInfo*>> UPaper2DPlusCharacterProfileAsset::GetFlipbookGroupTree() const
{
	TMap<FName, TArray<const FFlipbookGroupInfo*>> Tree;
	for (const FFlipbookGroupInfo& Group : FlipbookGroups)
	{
		Tree.FindOrAdd(Group.ParentGroup).Add(&Group);
	}
	// Sort child groups alphabetically within each parent
	for (auto& Pair : Tree)
	{
		Pair.Value.Sort([](const FFlipbookGroupInfo& A, const FFlipbookGroupInfo& B)
		{
			return A.GroupName.LexicalLess(B.GroupName);
		});
	}
	return Tree;
}

TArray<int32> UPaper2DPlusCharacterProfileAsset::GetFlipbookIndicesForFlipbookGroup(FName GroupName) const
{
	TArray<int32> Result;
	for (int32 i = 0; i < Flipbooks.Num(); ++i)
	{
		if (Flipbooks[i].FlipbookGroup == GroupName)
		{
			Result.Add(i);
		}
	}
	return Result;
}

#if WITH_EDITOR
FFlipbookGroupInfo& UPaper2DPlusCharacterProfileAsset::AddFlipbookGroup(FName Name, FName Parent)
{
	FFlipbookGroupInfo& NewGroup = FlipbookGroups.AddDefaulted_GetRef();
	NewGroup.GroupName = Name;
	NewGroup.ParentGroup = Parent;
	return NewGroup;
}

void UPaper2DPlusCharacterProfileAsset::RemoveFlipbookGroup(FName Name)
{
	// Find the group being removed to know its parent
	FName GroupParent = NAME_None;
	for (const FFlipbookGroupInfo& Group : FlipbookGroups)
	{
		if (Group.GroupName == Name)
		{
			GroupParent = Group.ParentGroup;
			break;
		}
	}

	// Promote child sub-groups to the removed group's parent
	for (FFlipbookGroupInfo& Group : FlipbookGroups)
	{
		if (Group.ParentGroup == Name)
		{
			Group.ParentGroup = GroupParent;
		}
	}

	// Move flipbooks to Ungrouped
	for (FFlipbookProfileEntry& Anim : Flipbooks)
	{
		if (Anim.FlipbookGroup == Name)
		{
			Anim.FlipbookGroup = NAME_None;
		}
	}

	// Remove the group entry
	FlipbookGroups.RemoveAll([Name](const FFlipbookGroupInfo& Group)
	{
		return Group.GroupName == Name;
	});
}

void UPaper2DPlusCharacterProfileAsset::RenameFlipbookGroup(FName OldName, FName NewName)
{
	// Update the group definition
	for (FFlipbookGroupInfo& Group : FlipbookGroups)
	{
		if (Group.GroupName == OldName)
		{
			Group.GroupName = NewName;
		}
		// Update child group parent references
		if (Group.ParentGroup == OldName)
		{
			Group.ParentGroup = NewName;
		}
	}

	// Update flipbook group assignments
	for (FFlipbookProfileEntry& Anim : Flipbooks)
	{
		if (Anim.FlipbookGroup == OldName)
		{
			Anim.FlipbookGroup = NewName;
		}
	}
}

void UPaper2DPlusCharacterProfileAsset::SetFlipbookGroupColor(FName Name, FLinearColor Color)
{
	for (FFlipbookGroupInfo& Group : FlipbookGroups)
	{
		if (Group.GroupName == Name)
		{
			Group.Color = Color;
			return;
		}
	}
}

void UPaper2DPlusCharacterProfileAsset::MoveFlipbookToFlipbookGroup(int32 FlipbookIndex, FName GroupName)
{
	if (Flipbooks.IsValidIndex(FlipbookIndex))
	{
		Flipbooks[FlipbookIndex].FlipbookGroup = GroupName;
	}
}

void UPaper2DPlusCharacterProfileAsset::ReparentFlipbookGroup(FName GroupName, FName NewParent)
{
	for (FFlipbookGroupInfo& Group : FlipbookGroups)
	{
		if (Group.GroupName == GroupName)
		{
			Group.ParentGroup = NewParent;
			return;
		}
	}
}
#endif

bool UPaper2DPlusCharacterProfileAsset::IsDescendantOfFlipbookGroup(FName GroupName, FName AncestorGroup) const
{
	// Walk up the parent chain from GroupName, checking if we hit AncestorGroup
	FName Current = GroupName;
	TSet<FName> Visited;
	while (Current != NAME_None)
	{
		if (Visited.Contains(Current)) return false; // Cycle guard
		Visited.Add(Current);

		for (const FFlipbookGroupInfo& Group : FlipbookGroups)
		{
			if (Group.GroupName == Current)
			{
				if (Group.ParentGroup == AncestorGroup) return true;
				Current = Group.ParentGroup;
				break;
			}
		}
	}
	return false;
}
