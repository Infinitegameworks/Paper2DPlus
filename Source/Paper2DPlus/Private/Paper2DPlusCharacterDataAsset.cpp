// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "Paper2DPlusCharacterDataAsset.h"
#include "Paper2DPlusSettings.h"
#include "PaperSprite.h"
#include "PaperFlipbook.h"
#include "JsonObjectConverter.h"
#include "Misc/FileHelper.h"


UPaper2DPlusCharacterDataAsset::UPaper2DPlusCharacterDataAsset()
{
	DisplayName = TEXT("New Character Data");
}

FPrimaryAssetId UPaper2DPlusCharacterDataAsset::GetPrimaryAssetId() const
{
	return FPrimaryAssetId(TEXT("CharacterData"), GetFName());
}

void UPaper2DPlusCharacterDataAsset::SyncFramesToFlipbook(int32 FlipbookIndex)
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
}

void UPaper2DPlusCharacterDataAsset::SyncAllFramesToFlipbooks()
{
	for (int32 i = 0; i < Flipbooks.Num(); ++i)
	{
		SyncFramesToFlipbook(i);
	}
}

void UPaper2DPlusCharacterDataAsset::PostLoad()
{
	Super::PostLoad();

	// Initialize extraction info array if empty but we have frames
	for (FFlipbookHitboxData& Anim : Flipbooks)
	{
		if (Anim.FrameExtractionInfo.Num() == 0 && Anim.Frames.Num() > 0)
		{
			Anim.FrameExtractionInfo.SetNum(Anim.Frames.Num());
		}
	}

	bFlipbookLookupCacheValid = false;
	bNameLookupCacheValid = false;
	bTagLookupCacheValid = false;
}

#if WITH_EDITOR
void UPaper2DPlusCharacterDataAsset::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	bFlipbookLookupCacheValid = false;
	bNameLookupCacheValid = false;
	bTagLookupCacheValid = false;
}
#endif

void UPaper2DPlusCharacterDataAsset::RebuildFlipbookLookupCache() const
{
	FlipbookToDataIndexCache.Empty();
	FlipbookToDataIndexCache.Reserve(Flipbooks.Num());

	for (int32 Index = 0; Index < Flipbooks.Num(); ++Index)
	{
		if (UPaperFlipbook* Flipbook = Flipbooks[Index].Flipbook.Get())
		{
			FlipbookToDataIndexCache.FindOrAdd(Flipbook) = Index;
		}
	}

	CachedFlipbookCount = Flipbooks.Num();
	bFlipbookLookupCacheValid = true;
}

void UPaper2DPlusCharacterDataAsset::RebuildNameLookupCache() const
{
	NameToFlipbookIndexCache.Empty();
	NameToFlipbookIndexCache.Reserve(Flipbooks.Num());

	for (int32 Index = 0; Index < Flipbooks.Num(); ++Index)
	{
		NameToFlipbookIndexCache.Add(Flipbooks[Index].FlipbookName.ToLower(), Index);
	}

	bNameLookupCacheValid = true;
}

const FFlipbookHitboxData* UPaper2DPlusCharacterDataAsset::FindFlipbookData(const FString& FlipbookName) const
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

const FFlipbookHitboxData* UPaper2DPlusCharacterDataAsset::FindFlipbookDataPtr(const FString& FlipbookName) const
{
	return FindFlipbookData(FlipbookName);
}

TArray<FString> UPaper2DPlusCharacterDataAsset::GetFlipbookNames() const
{
	TArray<FString> Names;
	Names.Reserve(Flipbooks.Num());
	for (const FFlipbookHitboxData& Anim : Flipbooks)
	{
		Names.Add(Anim.FlipbookName);
	}
	return Names;
}

bool UPaper2DPlusCharacterDataAsset::GetFlipbook(const FString& FlipbookName, FFlipbookHitboxData& OutFlipbook) const
{
	if (const FFlipbookHitboxData* Anim = FindFlipbookData(FlipbookName))
	{
		OutFlipbook = *Anim;
		return true;
	}
	return false;
}

bool UPaper2DPlusCharacterDataAsset::GetFlipbookByIndex(int32 Index, FFlipbookHitboxData& OutFlipbook) const
{
	if (Flipbooks.IsValidIndex(Index))
	{
		OutFlipbook = Flipbooks[Index];
		return true;
	}
	return false;
}

int32 UPaper2DPlusCharacterDataAsset::GetFrameCount(const FString& FlipbookName) const
{
	if (const FFlipbookHitboxData* Anim = FindFlipbookData(FlipbookName))
	{
		return Anim->Frames.Num();
	}
	return 0;
}

bool UPaper2DPlusCharacterDataAsset::GetFrame(const FString& FlipbookName, int32 FrameIndex, FFrameHitboxData& OutFrame) const
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

bool UPaper2DPlusCharacterDataAsset::GetFrameByName(const FString& FlipbookName, const FString& FrameName, FFrameHitboxData& OutFrame) const
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

const FFlipbookHitboxData* UPaper2DPlusCharacterDataAsset::FindByFlipbookPtr(UPaperFlipbook* Flipbook) const
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

	// Cache miss — linear scan fallback for late-loaded flipbooks
	for (int32 i = 0; i < Flipbooks.Num(); ++i)
	{
		if (Flipbooks[i].Flipbook.Get() == Flipbook)
		{
			FlipbookToDataIndexCache.FindOrAdd(Flipbook) = i;
			return &Flipbooks[i];
		}
	}
	return nullptr;
}


bool UPaper2DPlusCharacterDataAsset::FindByFlipbook(UPaperFlipbook* Flipbook, FFlipbookHitboxData& OutFlipbook) const
{
	if (const FFlipbookHitboxData* Anim = FindByFlipbookPtr(Flipbook))
	{
		OutFlipbook = *Anim;
		return true;
	}

	return false;
}

TArray<FHitboxData> UPaper2DPlusCharacterDataAsset::GetHitboxes(const FString& FlipbookName, int32 FrameIndex) const
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

TArray<FHitboxData> UPaper2DPlusCharacterDataAsset::GetHitboxesByType(const FString& FlipbookName, int32 FrameIndex, EHitboxType Type) const
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

TArray<FSocketData> UPaper2DPlusCharacterDataAsset::GetSockets(const FString& FlipbookName, int32 FrameIndex) const
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

bool UPaper2DPlusCharacterDataAsset::FindSocket(const FString& FlipbookName, int32 FrameIndex, const FString& SocketName, FSocketData& OutSocket) const
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

bool UPaper2DPlusCharacterDataAsset::HasFlipbook(const FString& FlipbookName) const
{
	return FindFlipbookData(FlipbookName) != nullptr;
}



bool UPaper2DPlusCharacterDataAsset::CopyFrameDataToRange(const FString& FlipbookName, int32 SourceFrameIndex, int32 RangeStart, int32 RangeEnd, bool bIncludeSockets)
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
		for (int32 Index = Start; Index <= End; ++Index)
		{
			if (Index == SourceFrameIndex)
			{
				continue;
			}

			Anim.Frames[Index].Hitboxes = SourceCopy.Hitboxes;
			if (bIncludeSockets)
			{
				Anim.Frames[Index].Sockets = SourceCopy.Sockets;
			}
		}

		return true;
	}

	return false;
}

int32 UPaper2DPlusCharacterDataAsset::MirrorHitboxesInRange(const FString& FlipbookName, int32 RangeStart, int32 RangeEnd, int32 PivotX)
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


int32 UPaper2DPlusCharacterDataAsset::SetSpriteFlipInRange(const FString& FlipbookName, int32 RangeStart, int32 RangeEnd, bool bInFlipX, bool bInFlipY)
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

int32 UPaper2DPlusCharacterDataAsset::SetSpriteFlipForFlipbook(const FString& FlipbookName, bool bInFlipX, bool bInFlipY)
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

int32 UPaper2DPlusCharacterDataAsset::SetSpriteFlipForAllFlipbooks(bool bInFlipX, bool bInFlipY)
{
	int32 TotalUpdated = 0;
	for (const FFlipbookHitboxData& Anim : Flipbooks)
	{
		TotalUpdated += SetSpriteFlipForFlipbook(Anim.FlipbookName, bInFlipX, bInFlipY);
	}
	return TotalUpdated;
}

bool UPaper2DPlusCharacterDataAsset::MigrateSerializablePayloadToCurrentSchema(FCharacterDataAssetSerializablePayload& InOutPayload)
{
	if (InOutPayload.SchemaVersion == CharacterDataJsonSchemaVersion)
	{
		return true;
	}

	if (InOutPayload.SchemaVersion == CharacterDataJsonLegacySchemaVersion)
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

	return InOutPayload.SchemaVersion == CharacterDataJsonSchemaVersion;
}

bool UPaper2DPlusCharacterDataAsset::ExportToJsonString(FString& OutJson) const
{
	FCharacterDataAssetSerializablePayload Payload;
	Payload.SchemaVersion = CharacterDataJsonSchemaVersion;
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

bool UPaper2DPlusCharacterDataAsset::ImportFromJsonString(const FString& JsonString)
{
	FCharacterDataAssetSerializablePayload Payload;
	if (!FJsonObjectConverter::JsonObjectStringToUStruct<FCharacterDataAssetSerializablePayload>(JsonString, &Payload, 0, 0))
	{
		return false;
	}

	if (Payload.SchemaVersion > CharacterDataJsonSchemaVersion)
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

bool UPaper2DPlusCharacterDataAsset::ExportToJsonFile(const FString& FilePath) const
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

bool UPaper2DPlusCharacterDataAsset::ImportFromJsonFile(const FString& FilePath)
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


bool UPaper2DPlusCharacterDataAsset::ValidateCharacterDataAsset(TArray<FCharacterDataValidationIssue>& OutIssues) const
{
	OutIssues.Reset();

	auto AddIssue = [&OutIssues](ECharacterDataValidationSeverity Severity, const FString& Context, const FString& Message)
	{
		FCharacterDataValidationIssue Issue;
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
			AddIssue(ECharacterDataValidationSeverity::Warning, AnimLabel, TEXT("Flipbook name is empty."));
		}
		else
		{
			const FString Normalized = Anim.FlipbookName.ToLower();
			if (FlipbookNames.Contains(Normalized))
			{
				AddIssue(ECharacterDataValidationSeverity::Error, AnimLabel, TEXT("Duplicate flipbook name detected."));
			}
			FlipbookNames.Add(Normalized);
		}

		const int32 FrameCount = Anim.Frames.Num();
		const int32 ExtractionCount = Anim.FrameExtractionInfo.Num();
		if (ExtractionCount > 0 && ExtractionCount != FrameCount)
		{
			AddIssue(ECharacterDataValidationSeverity::Warning, AnimLabel,
				FString::Printf(TEXT("FrameExtractionInfo count (%d) does not match Frames count (%d)."), ExtractionCount, FrameCount));
		}

		if (Anim.FrameExtractionInfo.Num() > 0 && !Anim.SourceTexture.ToSoftObjectPath().IsValid())
		{
			AddIssue(ECharacterDataValidationSeverity::Warning, AnimLabel, TEXT("Extraction metadata exists but SourceTexture is missing."));
		}

		if (!Anim.Flipbook.IsNull())
		{
			if (UPaperFlipbook* Flipbook = Anim.Flipbook.LoadSynchronous())
			{
				const int32 FlipbookFrameCount = Flipbook->GetNumKeyFrames();
				if (FlipbookFrameCount != FrameCount)
				{
					AddIssue(ECharacterDataValidationSeverity::Warning, AnimLabel,
						FString::Printf(TEXT("Frames count (%d) differs from Flipbook keyframe count (%d)."), FrameCount, FlipbookFrameCount));
				}
			}
			else
			{
				AddIssue(ECharacterDataValidationSeverity::Warning, AnimLabel, TEXT("Flipbook reference could not be loaded."));
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
					AddIssue(ECharacterDataValidationSeverity::Error, FrameLabel,
						FString::Printf(TEXT("Hitbox[%d] has invalid size %dx%d."), HitboxIndex, Hitbox.Width, Hitbox.Height));
				}
			}

			TSet<FString> SocketNames;
			for (int32 SocketIndex = 0; SocketIndex < Frame.Sockets.Num(); ++SocketIndex)
			{
				const FSocketData& Socket = Frame.Sockets[SocketIndex];
				if (Socket.Name.TrimStartAndEnd().IsEmpty())
				{
					AddIssue(ECharacterDataValidationSeverity::Warning, FrameLabel,
						FString::Printf(TEXT("Socket[%d] has empty name."), SocketIndex));
					continue;
				}

				const FString NormalizedSocket = Socket.Name.ToLower();
				if (SocketNames.Contains(NormalizedSocket))
				{
					AddIssue(ECharacterDataValidationSeverity::Warning, FrameLabel,
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
			AddIssue(ECharacterDataValidationSeverity::Warning, TagLabel, TEXT("Tag is empty or invalid."));
		}

		for (int32 i = 0; i < Pair.Value.FlipbookNames.Num(); ++i)
		{
			const FString& AnimName = Pair.Value.FlipbookNames[i];
			if (!AnimName.IsEmpty() && !FlipbookNames.Contains(AnimName.ToLower()))
			{
				AddIssue(ECharacterDataValidationSeverity::Warning, TagLabel,
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
				AddIssue(ECharacterDataValidationSeverity::Warning,
					FString::Printf(TEXT("Tag Mapping '%s'"), *RequiredTag.ToString()),
					TEXT("Required tag mapping is not mapped."));
			}
		}
	}

	for (const FCharacterDataValidationIssue& Issue : OutIssues)
	{
		if (Issue.Severity == ECharacterDataValidationSeverity::Error)
		{
			return false;
		}
	}
	return true;
}

int32 UPaper2DPlusCharacterDataAsset::TrimTrailingFrameData(int32 FlipbookIndex)
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

int32 UPaper2DPlusCharacterDataAsset::TrimAllTrailingFrameData()
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

TArray<FFlipbookHitboxData> UPaper2DPlusCharacterDataAsset::GetFlipbookDataForTag(FGameplayTag Group) const
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

TArray<UPaperFlipbook*> UPaper2DPlusCharacterDataAsset::GetFlipbooksForTag(FGameplayTag Group) const
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

UPaperFlipbook* UPaper2DPlusCharacterDataAsset::GetFirstFlipbookForTag(FGameplayTag Group) const
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

UPaperFlipbook* UPaper2DPlusCharacterDataAsset::GetRandomFlipbookForTag(FGameplayTag Group) const
{
	TArray<UPaperFlipbook*> TagFlipbooks = GetFlipbooksForTag(Group);
	if (TagFlipbooks.Num() == 0)
	{
		return nullptr;
	}
	return TagFlipbooks[FMath::RandRange(0, TagFlipbooks.Num() - 1)];
}

UObject* UPaper2DPlusCharacterDataAsset::GetPaperZDSequenceForTag(FGameplayTag Group) const
{
	if (const FFlipbookTagMapping* Binding = TagMappings.Find(Group))
	{
		return Binding->PaperZDSequence.LoadSynchronous();
	}
	return nullptr;
}

UObject* UPaper2DPlusCharacterDataAsset::GetTagMappingMetadata(FGameplayTag Group, FName Key) const
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

TArray<FName> UPaper2DPlusCharacterDataAsset::GetTagMappingMetadataKeys(FGameplayTag Group) const
{
	TArray<FName> Keys;
	if (const FFlipbookTagMapping* Binding = TagMappings.Find(Group))
	{
		Binding->Metadata.GetKeys(Keys);
	}
	return Keys;
}

bool UPaper2DPlusCharacterDataAsset::HasTagMappingMetadata(FGameplayTag Group, FName Key) const
{
	if (const FFlipbookTagMapping* Binding = TagMappings.Find(Group))
	{
		return Binding->Metadata.Contains(Key);
	}
	return false;
}

bool UPaper2DPlusCharacterDataAsset::GetTagMapping(FGameplayTag Group, FFlipbookTagMapping& OutBinding) const
{
	if (const FFlipbookTagMapping* Binding = TagMappings.Find(Group))
	{
		OutBinding = *Binding;
		return true;
	}
	return false;
}

bool UPaper2DPlusCharacterDataAsset::HasTagMapping(FGameplayTag Group) const
{
	return TagMappings.Contains(Group);
}

TArray<FGameplayTag> UPaper2DPlusCharacterDataAsset::GetAllMappedTags() const
{
	TArray<FGameplayTag> Tags;
	TagMappings.GetKeys(Tags);
	return Tags;
}

int32 UPaper2DPlusCharacterDataAsset::GetFlipbookCountForTag(FGameplayTag Group) const
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

float UPaper2DPlusCharacterDataAsset::GetMaxAttackRange() const
{
	float MaxRange = 0.0f;
	for (const FFlipbookHitboxData& Anim : Flipbooks)
	{
		MaxRange = FMath::Max(MaxRange, ComputeAttackRangeForAnimData(Anim));
	}
	return MaxRange;
}

float UPaper2DPlusCharacterDataAsset::GetAttackRangeForTag(FGameplayTag Group) const
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

float UPaper2DPlusCharacterDataAsset::GetAttackRangeForFlipbook(const FString& FlipbookName) const
{
	if (const FFlipbookHitboxData* Anim = FindFlipbookDataPtr(FlipbookName))
	{
		return ComputeAttackRangeForAnimData(*Anim);
	}
	return 0.0f;
}

FBox2D UPaper2DPlusCharacterDataAsset::GetAttackBoundsForTag(FGameplayTag Group) const
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

FBox2D UPaper2DPlusCharacterDataAsset::GetAttackBoundsForFlipbook(const FString& FlipbookName) const
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

void UPaper2DPlusCharacterDataAsset::RebuildTagLookupCache() const
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

void UPaper2DPlusCharacterDataAsset::UpdateTagMappingFlipbookName(const FString& OldName, const FString& NewName)
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

void UPaper2DPlusCharacterDataAsset::RemoveFlipbookFromTagMappings(const FString& FlipbookName)
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

bool UPaper2DPlusCharacterDataAsset::HasFlipbookGroup(FName Name) const
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

TMap<FName, TArray<const FFlipbookGroupInfo*>> UPaper2DPlusCharacterDataAsset::GetFlipbookGroupTree() const
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

TArray<int32> UPaper2DPlusCharacterDataAsset::GetFlipbookIndicesForFlipbookGroup(FName GroupName) const
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
FFlipbookGroupInfo& UPaper2DPlusCharacterDataAsset::AddFlipbookGroup(FName Name, FName Parent)
{
	FFlipbookGroupInfo& NewGroup = FlipbookGroups.AddDefaulted_GetRef();
	NewGroup.GroupName = Name;
	NewGroup.ParentGroup = Parent;
	return NewGroup;
}

void UPaper2DPlusCharacterDataAsset::RemoveFlipbookGroup(FName Name)
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

void UPaper2DPlusCharacterDataAsset::RenameFlipbookGroup(FName OldName, FName NewName)
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

void UPaper2DPlusCharacterDataAsset::SetFlipbookGroupColor(FName Name, FLinearColor Color)
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

void UPaper2DPlusCharacterDataAsset::MoveFlipbookToFlipbookGroup(int32 FlipbookIndex, FName GroupName)
{
	if (Flipbooks.IsValidIndex(FlipbookIndex))
	{
		Flipbooks[FlipbookIndex].FlipbookGroup = GroupName;
	}
}
#endif
