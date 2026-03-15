// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "Paper2DPlusCharacterProfileAsset.h"
#include "Paper2DPlusSettings.h"
#include "PaperSprite.h"
#include "PaperFlipbook.h"
#include "JsonObjectConverter.h"
#include "Misc/FileHelper.h"

namespace
{
void NormalizeFrameSourceIndices(FFlipbookHitboxData& Anim)
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

	for (FSpriteExtractionInfo& Info : Anim.FrameExtractionInfo)
	{
		Info.bExcludedFromFlipbook = false;
		AssignUniqueSourceIndex(Info);
	}

	for (FExcludedFlipbookFrameData& Excluded : Anim.ExcludedFrames)
	{
		Excluded.ExtractionInfo.bExcludedFromFlipbook = true;
		AssignUniqueSourceIndex(Excluded.ExtractionInfo);
	}
}

int32 FindRestoreInsertIndex(const FFlipbookHitboxData& Anim, int32 SourceFrameIndex)
{
	int32 InsertIndex = 0;
	for (const FSpriteExtractionInfo& Info : Anim.FrameExtractionInfo)
	{
		if (Info.SourceFrameIndex != INDEX_NONE && Info.SourceFrameIndex < SourceFrameIndex)
		{
			++InsertIndex;
		}
	}
	return InsertIndex;
}

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

	FFlipbookHitboxData& Anim = Flipbooks[FlipbookIndex];
	if (Anim.Flipbook.IsNull()) return;

	UPaperFlipbook* FB = Anim.Flipbook.LoadSynchronous();
	if (!FB) return;

	int32 FlipbookFrameCount = FB->GetNumKeyFrames();
	if (FlipbookFrameCount <= 0) return;

	// Resize Frames array to match flipbook — grow to add empty frames, shrink to trim orphans
	if (Anim.Frames.Num() != FlipbookFrameCount)
	{
		Anim.Frames.SetNum(FlipbookFrameCount);
	}

	// Also sync FrameExtractionInfo if populated
	if (Anim.FrameExtractionInfo.Num() > 0 && Anim.FrameExtractionInfo.Num() != FlipbookFrameCount)
	{
		Anim.FrameExtractionInfo.SetNum(FlipbookFrameCount);
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

	// Initialize extraction info array if empty but we have frames
	for (FFlipbookHitboxData& Anim : Flipbooks)
	{
		if (Anim.FrameExtractionInfo.Num() == 0 && Anim.Frames.Num() > 0)
		{
			Anim.FrameExtractionInfo.SetNum(Anim.Frames.Num());
		}

		NormalizeFrameSourceIndices(Anim);
	}

	bFlipbookLookupCacheValid = false;
	bNameLookupCacheValid = false;
	bTagLookupCacheValid = false;
}

#if WITH_EDITOR
void UPaper2DPlusCharacterProfileAsset::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	bFlipbookLookupCacheValid = false;
	bNameLookupCacheValid = false;
	bTagLookupCacheValid = false;
}
#endif

void UPaper2DPlusCharacterProfileAsset::RebuildFlipbookLookupCache() const
{
	FlipbookToDataIndexCache.Empty();
	FlipbookToDataIndexCache.Reserve(Flipbooks.Num());

	for (int32 Index = 0; Index < Flipbooks.Num(); ++Index)
	{
		// Use LoadSynchronous so soft references are resolved into the cache
		if (UPaperFlipbook* Flipbook = Flipbooks[Index].Flipbook.LoadSynchronous())
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
		NameToFlipbookIndexCache.Add(Flipbooks[Index].FlipbookName.ToLower(), Index);
	}

	bNameLookupCacheValid = true;
}

const FFlipbookHitboxData* UPaper2DPlusCharacterProfileAsset::FindFlipbookData(const FString& FlipbookName) const
{
	if (!bNameLookupCacheValid || Flipbooks.Num() != NameToFlipbookIndexCache.Num())
	{
		RebuildNameLookupCache();
	}

	if (const int32* FoundIndex = NameToFlipbookIndexCache.Find(FlipbookName.ToLower()))
	{
		if (Flipbooks.IsValidIndex(*FoundIndex) &&
			Flipbooks[*FoundIndex].FlipbookName.Equals(FlipbookName, ESearchCase::IgnoreCase))
		{
			return &Flipbooks[*FoundIndex];
		}
	}

	// Cache miss — linear scan fallback
	for (int32 i = 0; i < Flipbooks.Num(); ++i)
	{
		if (Flipbooks[i].FlipbookName.Equals(FlipbookName, ESearchCase::IgnoreCase))
		{
			NameToFlipbookIndexCache.Add(FlipbookName.ToLower(), i);
			return &Flipbooks[i];
		}
	}
	return nullptr;
}

const FFlipbookHitboxData* UPaper2DPlusCharacterProfileAsset::FindFlipbookDataPtr(const FString& FlipbookName) const
{
	return FindFlipbookData(FlipbookName);
}

TArray<FString> UPaper2DPlusCharacterProfileAsset::GetFlipbookNames() const
{
	TArray<FString> Names;
	Names.Reserve(Flipbooks.Num());
	for (const FFlipbookHitboxData& Anim : Flipbooks)
	{
		Names.Add(Anim.FlipbookName);
	}
	return Names;
}

bool UPaper2DPlusCharacterProfileAsset::GetFlipbook(const FString& FlipbookName, FFlipbookHitboxData& OutFlipbook) const
{
	if (const FFlipbookHitboxData* Anim = FindFlipbookData(FlipbookName))
	{
		OutFlipbook = *Anim;
		return true;
	}
	return false;
}

bool UPaper2DPlusCharacterProfileAsset::GetFlipbookByIndex(int32 Index, FFlipbookHitboxData& OutFlipbook) const
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
	if (const FFlipbookHitboxData* Anim = FindFlipbookData(FlipbookName))
	{
		return Anim->Frames.Num();
	}
	return 0;
}

bool UPaper2DPlusCharacterProfileAsset::GetFrame(const FString& FlipbookName, int32 FrameIndex, FFrameHitboxData& OutFrame) const
{
	if (const FFlipbookHitboxData* Anim = FindFlipbookData(FlipbookName))
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
	if (const FFlipbookHitboxData* Anim = FindFlipbookData(FlipbookName))
	{
		if (const FFrameHitboxData* Frame = Anim->GetFrameByName(FrameName))
		{
			OutFrame = *Frame;
			return true;
		}
	}
	return false;
}

const FFlipbookHitboxData* UPaper2DPlusCharacterProfileAsset::FindByFlipbookPtr(UPaperFlipbook* Flipbook) const
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
		const FFlipbookHitboxData& Anim = Flipbooks[*FoundIndex];
		if (Anim.Flipbook.Get() == Flipbook)
		{
			return &Anim;
		}
	}

	// Cache miss — linear scan fallback, resolve soft references
	for (int32 i = 0; i < Flipbooks.Num(); ++i)
	{
		if (Flipbooks[i].Flipbook.LoadSynchronous() == Flipbook)
		{
			FlipbookToDataIndexCache.FindOrAdd(Flipbook) = i;
			return &Flipbooks[i];
		}
	}
	return nullptr;
}


bool UPaper2DPlusCharacterProfileAsset::FindByFlipbook(UPaperFlipbook* Flipbook, FFlipbookHitboxData& OutFlipbook) const
{
	if (const FFlipbookHitboxData* Anim = FindByFlipbookPtr(Flipbook))
	{
		OutFlipbook = *Anim;
		return true;
	}

	return false;
}

TArray<FHitboxData> UPaper2DPlusCharacterProfileAsset::GetHitboxes(const FString& FlipbookName, int32 FrameIndex) const
{
	if (const FFlipbookHitboxData* Anim = FindFlipbookData(FlipbookName))
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
	if (const FFlipbookHitboxData* Anim = FindFlipbookData(FlipbookName))
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
	if (const FFlipbookHitboxData* Anim = FindFlipbookData(FlipbookName))
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
	if (const FFlipbookHitboxData* Anim = FindFlipbookData(FlipbookName))
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



bool UPaper2DPlusCharacterProfileAsset::CopyFrameDataToRange(const FString& FlipbookName, int32 SourceFrameIndex, int32 RangeStart, int32 RangeEnd, bool bIncludeSockets)
{
	for (FFlipbookHitboxData& Anim : Flipbooks)
	{
		if (!Anim.FlipbookName.Equals(FlipbookName, ESearchCase::IgnoreCase))
		{
			continue;
		}

		if (!Anim.Frames.IsValidIndex(SourceFrameIndex))
		{
			return false;
		}

		const int32 Start = FMath::Clamp(FMath::Min(RangeStart, RangeEnd), 0, Anim.Frames.Num() - 1);
		const int32 End = FMath::Clamp(FMath::Max(RangeStart, RangeEnd), 0, Anim.Frames.Num() - 1);

		const FFrameHitboxData SourceCopy = Anim.Frames[SourceFrameIndex];
		UPaperFlipbook* Flipbook = Anim.Flipbook.LoadSynchronous();
		for (int32 Index = Start; Index <= End; ++Index)
		{
			if (Index == SourceFrameIndex)
			{
				continue;
			}

			Anim.Frames[Index].Hitboxes = SourceCopy.Hitboxes;
			ClampFrameHitboxesToSpriteBounds(Anim.Frames[Index], Flipbook, Index);
			if (bIncludeSockets)
			{
				Anim.Frames[Index].Sockets = SourceCopy.Sockets;
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

	FFlipbookHitboxData& Anim = Flipbooks[FlipbookIndex];
	UPaperFlipbook* Flipbook = Anim.Flipbook.LoadSynchronous();
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
	if (Anim.Frames.Num() != KeyFrameCount)
	{
		Anim.Frames.SetNum(KeyFrameCount);
	}
	if (Anim.FrameExtractionInfo.Num() != KeyFrameCount)
	{
		Anim.FrameExtractionInfo.SetNum(KeyFrameCount);
	}

	NormalizeFrameSourceIndices(Anim);

	FExcludedFlipbookFrameData ExcludedFrame;
	ExcludedFrame.FrameData = Anim.Frames.IsValidIndex(FrameIndex) ? Anim.Frames[FrameIndex] : FFrameHitboxData();
	ExcludedFrame.ExtractionInfo = Anim.FrameExtractionInfo.IsValidIndex(FrameIndex) ? Anim.FrameExtractionInfo[FrameIndex] : FSpriteExtractionInfo();
	ExcludedFrame.ExtractionInfo.bExcludedFromFlipbook = true;

	{
		FScopedFlipbookMutator Mutator(Flipbook);
		if (!Mutator.KeyFrames.IsValidIndex(FrameIndex))
		{
			return false;
		}

		ExcludedFrame.KeyFrame = Mutator.KeyFrames[FrameIndex];
		Mutator.KeyFrames.RemoveAt(FrameIndex);
	}

	if (Anim.Frames.IsValidIndex(FrameIndex))
	{
		Anim.Frames.RemoveAt(FrameIndex);
	}
	if (Anim.FrameExtractionInfo.IsValidIndex(FrameIndex))
	{
		Anim.FrameExtractionInfo.RemoveAt(FrameIndex);
	}

	Anim.ExcludedFrames.Add(MoveTemp(ExcludedFrame));
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

	FFlipbookHitboxData& Anim = Flipbooks[FlipbookIndex];
	if (!Anim.ExcludedFrames.IsValidIndex(ExcludedFrameIndex))
	{
		return false;
	}

	UPaperFlipbook* Flipbook = Anim.Flipbook.LoadSynchronous();
	if (!Flipbook)
	{
		return false;
	}

	// NOTE: Does NOT call Modify() — callers manage transactions via BeginTransaction/EndTransaction.

	const int32 KeyFrameCount = Flipbook->GetNumKeyFrames();
	if (Anim.Frames.Num() != KeyFrameCount)
	{
		Anim.Frames.SetNum(KeyFrameCount);
	}
	if (Anim.FrameExtractionInfo.Num() != KeyFrameCount)
	{
		Anim.FrameExtractionInfo.SetNum(KeyFrameCount);
	}

	NormalizeFrameSourceIndices(Anim);

	const FExcludedFlipbookFrameData ExcludedFrame = Anim.ExcludedFrames[ExcludedFrameIndex];
	const int32 SourceFrameIndex = ExcludedFrame.ExtractionInfo.SourceFrameIndex;
	int32 InsertIndex = FindRestoreInsertIndex(Anim, SourceFrameIndex);
	InsertIndex = FMath::Clamp(InsertIndex, 0, KeyFrameCount);

	{
		FScopedFlipbookMutator Mutator(Flipbook);
		InsertIndex = FMath::Clamp(InsertIndex, 0, Mutator.KeyFrames.Num());
		Mutator.KeyFrames.Insert(ExcludedFrame.KeyFrame, InsertIndex);
	}

	Anim.Frames.Insert(ExcludedFrame.FrameData, InsertIndex);
	FSpriteExtractionInfo RestoredInfo = ExcludedFrame.ExtractionInfo;
	RestoredInfo.bExcludedFromFlipbook = false;
	Anim.FrameExtractionInfo.Insert(RestoredInfo, InsertIndex);
	Anim.ExcludedFrames.RemoveAt(ExcludedFrameIndex);

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

	FFlipbookHitboxData& Anim = Flipbooks[FlipbookIndex];
	if (Anim.ExcludedFrames.Num() == 0)
	{
		return 0;
	}

	UPaperFlipbook* Flipbook = Anim.Flipbook.LoadSynchronous();
	if (!Flipbook)
	{
		return 0;
	}

	// Does NOT call Modify() — callers manage transactions (consistent with
	// ExcludeFlipbookFrame / RestoreExcludedFlipbookFrame).
	const int32 KeyFrameCount = Flipbook->GetNumKeyFrames();
	if (Anim.Frames.Num() != KeyFrameCount)
	{
		Anim.Frames.SetNum(KeyFrameCount);
	}
	if (Anim.FrameExtractionInfo.Num() != KeyFrameCount)
	{
		Anim.FrameExtractionInfo.SetNum(KeyFrameCount);
	}

	NormalizeFrameSourceIndices(Anim);

	// Sort excluded frames by SourceFrameIndex so we insert highest-index first
	// (inserting from the back avoids shifting earlier insertion points).
	Anim.ExcludedFrames.Sort([](const FExcludedFlipbookFrameData& A, const FExcludedFlipbookFrameData& B)
	{
		return A.ExtractionInfo.SourceFrameIndex > B.ExtractionInfo.SourceFrameIndex;
	});

	const int32 RestoredCount = Anim.ExcludedFrames.Num();

	{
		FScopedFlipbookMutator Mutator(Flipbook);

		// Insert all excluded frames back in one pass (highest SourceFrameIndex first).
		for (const FExcludedFlipbookFrameData& ExcludedFrame : Anim.ExcludedFrames)
		{
			const int32 SourceIdx = ExcludedFrame.ExtractionInfo.SourceFrameIndex;
			int32 InsertIndex = FindRestoreInsertIndex(Anim, SourceIdx);
			InsertIndex = FMath::Clamp(InsertIndex, 0, Anim.Frames.Num());

			Anim.Frames.Insert(ExcludedFrame.FrameData, InsertIndex);
			FSpriteExtractionInfo RestoredInfo = ExcludedFrame.ExtractionInfo;
			RestoredInfo.bExcludedFromFlipbook = false;
			Anim.FrameExtractionInfo.Insert(RestoredInfo, InsertIndex);

			const int32 KeyInsertIndex = FMath::Clamp(InsertIndex, 0, Mutator.KeyFrames.Num());
			Mutator.KeyFrames.Insert(ExcludedFrame.KeyFrame, KeyInsertIndex);
		}
	}

	Anim.ExcludedFrames.Empty();
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

	return Flipbooks[FlipbookIndex].ExcludedFrames.Num();
}

int32 UPaper2DPlusCharacterProfileAsset::MirrorHitboxesInRange(const FString& FlipbookName, int32 RangeStart, int32 RangeEnd, int32 PivotX)
{
	for (FFlipbookHitboxData& Anim : Flipbooks)
	{
		if (!Anim.FlipbookName.Equals(FlipbookName, ESearchCase::IgnoreCase))
		{
			continue;
		}

		if (Anim.Frames.Num() == 0)
		{
			return 0;
		}

		const int32 Start = FMath::Clamp(FMath::Min(RangeStart, RangeEnd), 0, Anim.Frames.Num() - 1);
		const int32 End = FMath::Clamp(FMath::Max(RangeStart, RangeEnd), 0, Anim.Frames.Num() - 1);

		int32 MirroredCount = 0;
		for (int32 FrameIndex = Start; FrameIndex <= End; ++FrameIndex)
		{
			for (FHitboxData& Hitbox : Anim.Frames[FrameIndex].Hitboxes)
			{
				const int32 Right = Hitbox.X + Hitbox.Width;
				Hitbox.X = (2 * PivotX) - Right;
				MirroredCount++;
			}
			for (FSocketData& Socket : Anim.Frames[FrameIndex].Sockets)
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
	for (FFlipbookHitboxData& Anim : Flipbooks)
	{
		if (!Anim.FlipbookName.Equals(FlipbookName, ESearchCase::IgnoreCase))
		{
			continue;
		}

		if (Anim.Frames.Num() == 0)
		{
			return 0;
		}

		if (Anim.FrameExtractionInfo.Num() < Anim.Frames.Num())
		{
			Anim.FrameExtractionInfo.SetNum(Anim.Frames.Num());
		}

		const int32 Start = FMath::Clamp(FMath::Min(RangeStart, RangeEnd), 0, Anim.Frames.Num() - 1);
		const int32 End = FMath::Clamp(FMath::Max(RangeStart, RangeEnd), 0, Anim.Frames.Num() - 1);

		int32 UpdatedCount = 0;
		for (int32 FrameIndex = Start; FrameIndex <= End; ++FrameIndex)
		{
			if (!Anim.FrameExtractionInfo.IsValidIndex(FrameIndex))
			{
				continue;
			}

			FSpriteExtractionInfo& Info = Anim.FrameExtractionInfo[FrameIndex];
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
	for (const FFlipbookHitboxData& Anim : Flipbooks)
	{
		if (Anim.FlipbookName.Equals(FlipbookName, ESearchCase::IgnoreCase))
		{
			if (Anim.Frames.Num() <= 0)
			{
				return 0;
			}
			return SetSpriteFlipInRange(FlipbookName, 0, Anim.Frames.Num() - 1, bInFlipX, bInFlipY);
		}
	}

	return 0;
}

int32 UPaper2DPlusCharacterProfileAsset::SetSpriteFlipForAllFlipbooks(bool bInFlipX, bool bInFlipY)
{
	int32 TotalUpdated = 0;
	for (const FFlipbookHitboxData& Anim : Flipbooks)
	{
		TotalUpdated += SetSpriteFlipForFlipbook(Anim.FlipbookName, bInFlipX, bInFlipY);
	}
	return TotalUpdated;
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

	// Serialize FlipbookGroups (lives on asset class, not on FFlipbookHitboxData)
	Payload.FlipbookGroups = FlipbookGroups;

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
	for (FFlipbookHitboxData& Anim : Flipbooks)
	{
		if (Anim.FlipbookGroup != NAME_None && !ValidGroupNames.Contains(Anim.FlipbookGroup))
		{
			Anim.FlipbookGroup = NAME_None;
		}
	}

	bFlipbookLookupCacheValid = false;
	bNameLookupCacheValid = false;
	bTagLookupCacheValid = false;
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
		const FFlipbookHitboxData& Anim = Flipbooks[FlipbookIndex];
		const FString AnimLabel = FString::Printf(TEXT("Flipbook[%d] '%s'"), FlipbookIndex, *Anim.FlipbookName);

		if (Anim.FlipbookName.TrimStartAndEnd().IsEmpty())
		{
			AddIssue(ECharacterProfileValidationSeverity::Warning, AnimLabel, TEXT("Flipbook name is empty."));
		}
		else
		{
			const FString Normalized = Anim.FlipbookName.ToLower();
			if (FlipbookNames.Contains(Normalized))
			{
				AddIssue(ECharacterProfileValidationSeverity::Error, AnimLabel, TEXT("Duplicate flipbook name detected."));
			}
			FlipbookNames.Add(Normalized);
		}

		const int32 FrameCount = Anim.Frames.Num();
		const int32 ExtractionCount = Anim.FrameExtractionInfo.Num();
		if (ExtractionCount > 0 && ExtractionCount != FrameCount)
		{
			AddIssue(ECharacterProfileValidationSeverity::Warning, AnimLabel,
				FString::Printf(TEXT("FrameExtractionInfo count (%d) does not match Frames count (%d)."), ExtractionCount, FrameCount));
		}

		// Only warn about missing SourceTexture if frames were actually extracted
		// (FrameExtractionInfo is auto-populated in PostLoad for all flipbooks)
		if (!Anim.SourceTexture.ToSoftObjectPath().IsValid())
		{
			bool bHasRealExtractionData = false;
			for (const FSpriteExtractionInfo& Info : Anim.FrameExtractionInfo)
			{
				if (Info.ExtractionTime.GetTicks() != 0)
				{
					bHasRealExtractionData = true;
					break;
				}
			}
			if (bHasRealExtractionData)
			{
				AddIssue(ECharacterProfileValidationSeverity::Warning, AnimLabel, TEXT("Extraction metadata exists but SourceTexture is missing."));
			}
		}

		if (!Anim.Flipbook.IsNull())
		{
			if (UPaperFlipbook* Flipbook = Anim.Flipbook.LoadSynchronous())
			{
				const int32 FlipbookFrameCount = Flipbook->GetNumKeyFrames();
				if (FlipbookFrameCount != FrameCount)
				{
					AddIssue(ECharacterProfileValidationSeverity::Warning, AnimLabel,
						FString::Printf(TEXT("Frames count (%d) differs from Flipbook keyframe count (%d)."), FrameCount, FlipbookFrameCount));
				}
			}
			else
			{
				AddIssue(ECharacterProfileValidationSeverity::Warning, AnimLabel, TEXT("Flipbook reference could not be loaded."));
			}
		}

		for (int32 FrameIndex = 0; FrameIndex < Anim.Frames.Num(); ++FrameIndex)
		{
			const FFrameHitboxData& Frame = Anim.Frames[FrameIndex];
			const FString FrameLabel = FString::Printf(TEXT("%s Frame[%d] '%s'"), *AnimLabel, FrameIndex, *Frame.FrameName);

			for (int32 HitboxIndex = 0; HitboxIndex < Frame.Hitboxes.Num(); ++HitboxIndex)
			{
				const FHitboxData& Hitbox = Frame.Hitboxes[HitboxIndex];
				if (Hitbox.Width <= 0 || Hitbox.Height <= 0)
				{
					AddIssue(ECharacterProfileValidationSeverity::Error, FrameLabel,
						FString::Printf(TEXT("Hitbox[%d] has invalid size %dx%d."), HitboxIndex, Hitbox.Width, Hitbox.Height));
				}
			}

			TSet<FString> SocketNames;
			for (int32 SocketIndex = 0; SocketIndex < Frame.Sockets.Num(); ++SocketIndex)
			{
				const FSocketData& Socket = Frame.Sockets[SocketIndex];
				if (Socket.Name.TrimStartAndEnd().IsEmpty())
				{
					AddIssue(ECharacterProfileValidationSeverity::Warning, FrameLabel,
						FString::Printf(TEXT("Socket[%d] has empty name."), SocketIndex));
					continue;
				}

				const FString NormalizedSocket = Socket.Name.ToLower();
				if (SocketNames.Contains(NormalizedSocket))
				{
					AddIssue(ECharacterProfileValidationSeverity::Warning, FrameLabel,
						FString::Printf(TEXT("Duplicate socket name '%s' on frame."), *Socket.Name));
				}
				SocketNames.Add(NormalizedSocket);
			}
		}
	}

	// Validate tag mappings
	for (const auto& Pair : TagMappings)
	{
		const FString TagLabel = FString::Printf(TEXT("Tag Mapping '%s'"), *Pair.Key.ToString());

		if (!Pair.Key.IsValid())
		{
			AddIssue(ECharacterProfileValidationSeverity::Warning, TagLabel, TEXT("Tag is empty or invalid."));
		}

		for (int32 i = 0; i < Pair.Value.FlipbookNames.Num(); ++i)
		{
			const FString& AnimName = Pair.Value.FlipbookNames[i];
			if (!AnimName.IsEmpty() && !FlipbookNames.Contains(AnimName.ToLower()))
			{
				AddIssue(ECharacterProfileValidationSeverity::Warning, TagLabel,
					FString::Printf(TEXT("Flipbook '%s' is not found in this asset."), *AnimName));
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
					TEXT("Required tag mapping is not mapped."));
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

	FFlipbookHitboxData& Anim = Flipbooks[FlipbookIndex];
	UPaperFlipbook* Flipbook = Anim.Flipbook.LoadSynchronous();
	if (!Flipbook)
	{
		return 0;
	}

	const int32 TargetCount = FMath::Max(0, Flipbook->GetNumKeyFrames());
	int32 RemovedCount = 0;

	if (Anim.Frames.Num() > TargetCount)
	{
		RemovedCount += (Anim.Frames.Num() - TargetCount);
		Anim.Frames.SetNum(TargetCount);
	}

	if (Anim.FrameExtractionInfo.Num() > TargetCount)
	{
		RemovedCount += (Anim.FrameExtractionInfo.Num() - TargetCount);
		Anim.FrameExtractionInfo.SetNum(TargetCount);
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

TArray<FFlipbookHitboxData> UPaper2DPlusCharacterProfileAsset::GetFlipbookDataForTag(FGameplayTag Group) const
{
	TArray<FFlipbookHitboxData> Result;
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
			if (Flipbooks.IsValidIndex(Index) && !Flipbooks[Index].Flipbook.IsNull())
			{
				if (UPaperFlipbook* FB = Flipbooks[Index].Flipbook.LoadSynchronous())
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
			if (Flipbooks.IsValidIndex(Index) && !Flipbooks[Index].Flipbook.IsNull())
			{
				if (UPaperFlipbook* FB = Flipbooks[Index].Flipbook.LoadSynchronous())
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

UObject* UPaper2DPlusCharacterProfileAsset::GetPaperZDSequenceForTag(FGameplayTag Group, int32 ComboIndex) const
{
	if (const FFlipbookTagMapping* Binding = TagMappings.Find(Group))
	{
		if (Binding->PaperZDSequences.IsValidIndex(ComboIndex))
		{
			return Binding->PaperZDSequences[ComboIndex].LoadSynchronous();
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
	float ComputeAttackRangeForAnimData(const FFlipbookHitboxData& AnimData)
	{
		float MaxRange = 0.0f;
		for (const FFrameHitboxData& Frame : AnimData.Frames)
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
	FBox2D ComputeAttackBoundsForAnimData(const FFlipbookHitboxData& AnimData)
	{
		FBox2D Bounds(ForceInit);
		bool bHasAny = false;

		for (const FFrameHitboxData& Frame : AnimData.Frames)
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
	for (const FFlipbookHitboxData& Anim : Flipbooks)
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
			if (const FFlipbookHitboxData* Anim = FindFlipbookDataPtr(AnimName))
			{
				MaxRange = FMath::Max(MaxRange, ComputeAttackRangeForAnimData(*Anim));
			}
		}
	}
	return MaxRange;
}

float UPaper2DPlusCharacterProfileAsset::GetAttackRangeForFlipbook(const FString& FlipbookName) const
{
	if (const FFlipbookHitboxData* Anim = FindFlipbookDataPtr(FlipbookName))
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
			if (const FFlipbookHitboxData* Anim = FindFlipbookDataPtr(AnimName))
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
	if (const FFlipbookHitboxData* Anim = FindFlipbookDataPtr(FlipbookName))
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
		Pair.Value.FlipbookNames.RemoveAll([&FlipbookName](const FString& Name)
		{
			return Name.Equals(FlipbookName, ESearchCase::IgnoreCase);
		});
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
	// Sort children alphabetically within each parent
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
	for (FFlipbookHitboxData& Anim : Flipbooks)
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
	for (FFlipbookHitboxData& Anim : Flipbooks)
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
#endif
