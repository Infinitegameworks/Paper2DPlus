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

void UPaper2DPlusCharacterDataAsset::SyncFramesToFlipbook(int32 AnimationIndex)
{
	if (!Animations.IsValidIndex(AnimationIndex)) return;

	FAnimationHitboxData& Anim = Animations[AnimationIndex];
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
	for (int32 i = 0; i < Animations.Num(); ++i)
	{
		SyncFramesToFlipbook(i);
	}
}

void UPaper2DPlusCharacterDataAsset::PostLoad()
{
	Super::PostLoad();

	// Initialize extraction info array if empty but we have frames
	for (FAnimationHitboxData& Anim : Animations)
	{
		if (Anim.FrameExtractionInfo.Num() == 0 && Anim.Frames.Num() > 0)
		{
			Anim.FrameExtractionInfo.SetNum(Anim.Frames.Num());
		}
	}

	bFlipbookLookupCacheValid = false;
	bNameLookupCacheValid = false;
	bGroupLookupCacheValid = false;
}

#if WITH_EDITOR
void UPaper2DPlusCharacterDataAsset::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	bFlipbookLookupCacheValid = false;
	bNameLookupCacheValid = false;
	bGroupLookupCacheValid = false;
}
#endif

void UPaper2DPlusCharacterDataAsset::RebuildFlipbookLookupCache() const
{
	FlipbookToAnimationIndexCache.Empty();
	FlipbookToAnimationIndexCache.Reserve(Animations.Num());

	for (int32 Index = 0; Index < Animations.Num(); ++Index)
	{
		if (UPaperFlipbook* Flipbook = Animations[Index].Flipbook.Get())
		{
			FlipbookToAnimationIndexCache.FindOrAdd(Flipbook) = Index;
		}
	}

	CachedAnimationCount = Animations.Num();
	bFlipbookLookupCacheValid = true;
}

void UPaper2DPlusCharacterDataAsset::RebuildNameLookupCache() const
{
	NameToAnimationIndexCache.Empty();
	NameToAnimationIndexCache.Reserve(Animations.Num());

	for (int32 Index = 0; Index < Animations.Num(); ++Index)
	{
		NameToAnimationIndexCache.Add(Animations[Index].AnimationName.ToLower(), Index);
	}

	bNameLookupCacheValid = true;
}

const FAnimationHitboxData* UPaper2DPlusCharacterDataAsset::FindAnimation(const FString& AnimationName) const
{
	if (!bNameLookupCacheValid || Animations.Num() != NameToAnimationIndexCache.Num())
	{
		RebuildNameLookupCache();
	}

	if (const int32* FoundIndex = NameToAnimationIndexCache.Find(AnimationName.ToLower()))
	{
		if (Animations.IsValidIndex(*FoundIndex) &&
			Animations[*FoundIndex].AnimationName.Equals(AnimationName, ESearchCase::IgnoreCase))
		{
			return &Animations[*FoundIndex];
		}
	}

	// Cache miss — linear scan fallback
	for (int32 i = 0; i < Animations.Num(); ++i)
	{
		if (Animations[i].AnimationName.Equals(AnimationName, ESearchCase::IgnoreCase))
		{
			NameToAnimationIndexCache.Add(AnimationName.ToLower(), i);
			return &Animations[i];
		}
	}
	return nullptr;
}

const FAnimationHitboxData* UPaper2DPlusCharacterDataAsset::FindAnimationPtr(const FString& AnimationName) const
{
	return FindAnimation(AnimationName);
}

TArray<FString> UPaper2DPlusCharacterDataAsset::GetAnimationNames() const
{
	TArray<FString> Names;
	Names.Reserve(Animations.Num());
	for (const FAnimationHitboxData& Anim : Animations)
	{
		Names.Add(Anim.AnimationName);
	}
	return Names;
}

bool UPaper2DPlusCharacterDataAsset::GetAnimation(const FString& AnimationName, FAnimationHitboxData& OutAnimation) const
{
	if (const FAnimationHitboxData* Anim = FindAnimation(AnimationName))
	{
		OutAnimation = *Anim;
		return true;
	}
	return false;
}

bool UPaper2DPlusCharacterDataAsset::GetAnimationByIndex(int32 Index, FAnimationHitboxData& OutAnimation) const
{
	if (Animations.IsValidIndex(Index))
	{
		OutAnimation = Animations[Index];
		return true;
	}
	return false;
}

int32 UPaper2DPlusCharacterDataAsset::GetFrameCount(const FString& AnimationName) const
{
	if (const FAnimationHitboxData* Anim = FindAnimation(AnimationName))
	{
		return Anim->Frames.Num();
	}
	return 0;
}

bool UPaper2DPlusCharacterDataAsset::GetFrame(const FString& AnimationName, int32 FrameIndex, FFrameHitboxData& OutFrame) const
{
	if (const FAnimationHitboxData* Anim = FindAnimation(AnimationName))
	{
		if (const FFrameHitboxData* Frame = Anim->GetFrame(FrameIndex))
		{
			OutFrame = *Frame;
			return true;
		}
	}
	return false;
}

bool UPaper2DPlusCharacterDataAsset::GetFrameByName(const FString& AnimationName, const FString& FrameName, FFrameHitboxData& OutFrame) const
{
	if (const FAnimationHitboxData* Anim = FindAnimation(AnimationName))
	{
		if (const FFrameHitboxData* Frame = Anim->GetFrameByName(FrameName))
		{
			OutFrame = *Frame;
			return true;
		}
	}
	return false;
}

const FAnimationHitboxData* UPaper2DPlusCharacterDataAsset::FindAnimationByFlipbookPtr(UPaperFlipbook* Flipbook) const
{
	if (!Flipbook)
	{
		return nullptr;
	}

	if (!bFlipbookLookupCacheValid || Animations.Num() != CachedAnimationCount)
	{
		RebuildFlipbookLookupCache();
	}

	const int32* FoundIndex = FlipbookToAnimationIndexCache.Find(Flipbook);
	if (FoundIndex && Animations.IsValidIndex(*FoundIndex))
	{
		const FAnimationHitboxData& Anim = Animations[*FoundIndex];
		if (Anim.Flipbook.Get() == Flipbook)
		{
			return &Anim;
		}
	}

	// Cache miss — linear scan fallback for late-loaded flipbooks
	for (int32 i = 0; i < Animations.Num(); ++i)
	{
		if (Animations[i].Flipbook.Get() == Flipbook)
		{
			FlipbookToAnimationIndexCache.FindOrAdd(Flipbook) = i;
			return &Animations[i];
		}
	}
	return nullptr;
}


bool UPaper2DPlusCharacterDataAsset::FindAnimationByFlipbook(UPaperFlipbook* Flipbook, FAnimationHitboxData& OutAnimation) const
{
	if (const FAnimationHitboxData* Anim = FindAnimationByFlipbookPtr(Flipbook))
	{
		OutAnimation = *Anim;
		return true;
	}

	return false;
}

TArray<FHitboxData> UPaper2DPlusCharacterDataAsset::GetHitboxes(const FString& AnimationName, int32 FrameIndex) const
{
	if (const FAnimationHitboxData* Anim = FindAnimation(AnimationName))
	{
		if (const FFrameHitboxData* Frame = Anim->GetFrame(FrameIndex))
		{
			return Frame->Hitboxes;
		}
	}
	return TArray<FHitboxData>();
}

TArray<FHitboxData> UPaper2DPlusCharacterDataAsset::GetHitboxesByType(const FString& AnimationName, int32 FrameIndex, EHitboxType Type) const
{
	if (const FAnimationHitboxData* Anim = FindAnimation(AnimationName))
	{
		if (const FFrameHitboxData* Frame = Anim->GetFrame(FrameIndex))
		{
			return Frame->GetHitboxesByType(Type);
		}
	}
	return TArray<FHitboxData>();
}

TArray<FSocketData> UPaper2DPlusCharacterDataAsset::GetSockets(const FString& AnimationName, int32 FrameIndex) const
{
	if (const FAnimationHitboxData* Anim = FindAnimation(AnimationName))
	{
		if (const FFrameHitboxData* Frame = Anim->GetFrame(FrameIndex))
		{
			return Frame->Sockets;
		}
	}
	return TArray<FSocketData>();
}

bool UPaper2DPlusCharacterDataAsset::FindSocket(const FString& AnimationName, int32 FrameIndex, const FString& SocketName, FSocketData& OutSocket) const
{
	if (const FAnimationHitboxData* Anim = FindAnimation(AnimationName))
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

bool UPaper2DPlusCharacterDataAsset::HasAnimation(const FString& AnimationName) const
{
	return FindAnimation(AnimationName) != nullptr;
}



bool UPaper2DPlusCharacterDataAsset::CopyFrameDataToRange(const FString& AnimationName, int32 SourceFrameIndex, int32 RangeStart, int32 RangeEnd, bool bIncludeSockets)
{
	for (FAnimationHitboxData& Anim : Animations)
	{
		if (!Anim.AnimationName.Equals(AnimationName, ESearchCase::IgnoreCase))
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

int32 UPaper2DPlusCharacterDataAsset::MirrorHitboxesInRange(const FString& AnimationName, int32 RangeStart, int32 RangeEnd, int32 PivotX)
{
	for (FAnimationHitboxData& Anim : Animations)
	{
		if (!Anim.AnimationName.Equals(AnimationName, ESearchCase::IgnoreCase))
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


int32 UPaper2DPlusCharacterDataAsset::SetSpriteFlipInRange(const FString& AnimationName, int32 RangeStart, int32 RangeEnd, bool bInFlipX, bool bInFlipY)
{
	for (FAnimationHitboxData& Anim : Animations)
	{
		if (!Anim.AnimationName.Equals(AnimationName, ESearchCase::IgnoreCase))
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

int32 UPaper2DPlusCharacterDataAsset::SetSpriteFlipForAnimation(const FString& AnimationName, bool bInFlipX, bool bInFlipY)
{
	for (const FAnimationHitboxData& Anim : Animations)
	{
		if (Anim.AnimationName.Equals(AnimationName, ESearchCase::IgnoreCase))
		{
			if (Anim.Frames.Num() <= 0)
			{
				return 0;
			}
			return SetSpriteFlipInRange(AnimationName, 0, Anim.Frames.Num() - 1, bInFlipX, bInFlipY);
		}
	}

	return 0;
}

int32 UPaper2DPlusCharacterDataAsset::SetSpriteFlipForAllAnimations(bool bInFlipX, bool bInFlipY)
{
	int32 TotalUpdated = 0;
	for (const FAnimationHitboxData& Anim : Animations)
	{
		TotalUpdated += SetSpriteFlipForAnimation(Anim.AnimationName, bInFlipX, bInFlipY);
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
		// v1 -> v2: GroupBindings didn't exist. Initialize empty (already default).
		InOutPayload.SchemaVersion = 2;
	}

	if (InOutPayload.SchemaVersion == 2)
	{
		// v2 -> v3: Removed UniformDimensions, bUseUniformDimensions, UniformAnchor, SpriteDimensions.
		// Old JSON payloads may carry these keys — FJsonObjectConverter silently ignores unknown keys.
		InOutPayload.SchemaVersion = 3;
	}

	return InOutPayload.SchemaVersion == CharacterDataJsonSchemaVersion;
}

bool UPaper2DPlusCharacterDataAsset::ExportToJsonString(FString& OutJson) const
{
	FCharacterDataAssetSerializablePayload Payload;
	Payload.SchemaVersion = CharacterDataJsonSchemaVersion;
	Payload.DisplayName = DisplayName;
	Payload.Animations = Animations;
	Payload.DefaultAlphaThreshold = DefaultAlphaThreshold;
	Payload.DefaultPadding = DefaultPadding;
	Payload.DefaultMinSpriteSize = DefaultMinSpriteSize;

	// Serialize GroupBindings as array of key-value pairs (avoids TMap<FGameplayTag> JSON issues)
	Payload.GroupBindings.Reserve(GroupBindings.Num());
	for (const auto& Pair : GroupBindings)
	{
		FSerializableGroupBinding Entry;
		Entry.Tag = Pair.Key.ToString();
		Entry.Binding = Pair.Value;
		Payload.GroupBindings.Add(MoveTemp(Entry));
	}

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
	Animations = Payload.Animations;
	DefaultAlphaThreshold = Payload.DefaultAlphaThreshold;
	DefaultPadding = Payload.DefaultPadding;
	DefaultMinSpriteSize = Payload.DefaultMinSpriteSize;

	// Restore GroupBindings from serialized array
	GroupBindings.Empty();
	for (const FSerializableGroupBinding& Entry : Payload.GroupBindings)
	{
		FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(*Entry.Tag), false);
		if (Tag.IsValid())
		{
			GroupBindings.Add(Tag, Entry.Binding);
		}
	}

	bFlipbookLookupCacheValid = false;
	bNameLookupCacheValid = false;
	bGroupLookupCacheValid = false;
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

	TSet<FString> AnimationNames;
	for (int32 AnimIndex = 0; AnimIndex < Animations.Num(); ++AnimIndex)
	{
		const FAnimationHitboxData& Anim = Animations[AnimIndex];
		const FString AnimLabel = FString::Printf(TEXT("Animation[%d] '%s'"), AnimIndex, *Anim.AnimationName);

		if (Anim.AnimationName.TrimStartAndEnd().IsEmpty())
		{
			AddIssue(ECharacterDataValidationSeverity::Warning, AnimLabel, TEXT("Animation name is empty."));
		}
		else
		{
			const FString Normalized = Anim.AnimationName.ToLower();
			if (AnimationNames.Contains(Normalized))
			{
				AddIssue(ECharacterDataValidationSeverity::Error, AnimLabel, TEXT("Duplicate animation name detected."));
			}
			AnimationNames.Add(Normalized);
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

	// Validate group bindings
	for (const auto& Pair : GroupBindings)
	{
		const FString GroupLabel = FString::Printf(TEXT("Group '%s'"), *Pair.Key.ToString());

		if (!Pair.Key.IsValid())
		{
			AddIssue(ECharacterDataValidationSeverity::Warning, GroupLabel, TEXT("Group tag is empty or invalid."));
		}

		for (int32 i = 0; i < Pair.Value.AnimationNames.Num(); ++i)
		{
			const FString& AnimName = Pair.Value.AnimationNames[i];
			if (!AnimName.IsEmpty() && !AnimationNames.Contains(AnimName.ToLower()))
			{
				AddIssue(ECharacterDataValidationSeverity::Warning, GroupLabel,
					FString::Printf(TEXT("Animation '%s' is not found in this asset."), *AnimName));
			}
		}
	}

	// Check for unmapped required roles
	if (const UPaper2DPlusSettings* Settings = UPaper2DPlusSettings::Get())
	{
		for (const FGameplayTag& RequiredGroup : Settings->RequiredAnimationGroups)
		{
			if (RequiredGroup.IsValid() && !GroupBindings.Contains(RequiredGroup))
			{
				AddIssue(ECharacterDataValidationSeverity::Warning,
					FString::Printf(TEXT("Group '%s'"), *RequiredGroup.ToString()),
					TEXT("Required group is not mapped."));
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

int32 UPaper2DPlusCharacterDataAsset::TrimTrailingFrameData(int32 AnimationIndex)
{
	if (!Animations.IsValidIndex(AnimationIndex))
	{
		return 0;
	}

	FAnimationHitboxData& Anim = Animations[AnimationIndex];
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
	for (int32 Index = 0; Index < Animations.Num(); ++Index)
	{
		TotalRemoved += TrimTrailingFrameData(Index);
	}
	return TotalRemoved;
}

// ==========================================
// ANIMATION GROUP LOOKUPS
// ==========================================

TArray<FAnimationHitboxData> UPaper2DPlusCharacterDataAsset::GetAnimationsForGroup(FGameplayTag Group) const
{
	TArray<FAnimationHitboxData> Result;
	if (!bGroupLookupCacheValid)
	{
		RebuildGroupLookupCache();
	}

	if (const TArray<int32>* Indices = GroupToAnimationIndicesCache.Find(Group))
	{
		Result.Reserve(Indices->Num());
		for (int32 Index : *Indices)
		{
			if (Animations.IsValidIndex(Index))
			{
				Result.Add(Animations[Index]);
			}
		}
	}
	return Result;
}

TArray<UPaperFlipbook*> UPaper2DPlusCharacterDataAsset::GetFlipbooksForGroup(FGameplayTag Group) const
{
	TArray<UPaperFlipbook*> Result;
	if (!bGroupLookupCacheValid)
	{
		RebuildGroupLookupCache();
	}

	if (const TArray<int32>* Indices = GroupToAnimationIndicesCache.Find(Group))
	{
		Result.Reserve(Indices->Num());
		for (int32 Index : *Indices)
		{
			if (Animations.IsValidIndex(Index) && !Animations[Index].Flipbook.IsNull())
			{
				if (UPaperFlipbook* FB = Animations[Index].Flipbook.LoadSynchronous())
				{
					Result.Add(FB);
				}
			}
		}
	}
	return Result;
}

UPaperFlipbook* UPaper2DPlusCharacterDataAsset::GetFirstFlipbookForGroup(FGameplayTag Group) const
{
	if (!bGroupLookupCacheValid)
	{
		RebuildGroupLookupCache();
	}

	if (const TArray<int32>* Indices = GroupToAnimationIndicesCache.Find(Group))
	{
		for (int32 Index : *Indices)
		{
			if (Animations.IsValidIndex(Index) && !Animations[Index].Flipbook.IsNull())
			{
				if (UPaperFlipbook* FB = Animations[Index].Flipbook.LoadSynchronous())
				{
					return FB;
				}
			}
		}
	}
	return nullptr;
}

UPaperFlipbook* UPaper2DPlusCharacterDataAsset::GetRandomFlipbookForGroup(FGameplayTag Group) const
{
	TArray<UPaperFlipbook*> Flipbooks = GetFlipbooksForGroup(Group);
	if (Flipbooks.Num() == 0)
	{
		return nullptr;
	}
	return Flipbooks[FMath::RandRange(0, Flipbooks.Num() - 1)];
}

UObject* UPaper2DPlusCharacterDataAsset::GetPaperZDSequenceForGroup(FGameplayTag Group) const
{
	if (const FAnimationGroupBinding* Binding = GroupBindings.Find(Group))
	{
		return Binding->PaperZDSequence.LoadSynchronous();
	}
	return nullptr;
}

UObject* UPaper2DPlusCharacterDataAsset::GetGroupMetadata(FGameplayTag Group, FName Key) const
{
	if (const FAnimationGroupBinding* Binding = GroupBindings.Find(Group))
	{
		if (const TSoftObjectPtr<UObject>* SoftRef = Binding->Metadata.Find(Key))
		{
			return SoftRef->LoadSynchronous();
		}
	}
	return nullptr;
}

TArray<FName> UPaper2DPlusCharacterDataAsset::GetGroupMetadataKeys(FGameplayTag Group) const
{
	TArray<FName> Keys;
	if (const FAnimationGroupBinding* Binding = GroupBindings.Find(Group))
	{
		Binding->Metadata.GetKeys(Keys);
	}
	return Keys;
}

bool UPaper2DPlusCharacterDataAsset::HasGroupMetadata(FGameplayTag Group, FName Key) const
{
	if (const FAnimationGroupBinding* Binding = GroupBindings.Find(Group))
	{
		return Binding->Metadata.Contains(Key);
	}
	return false;
}

bool UPaper2DPlusCharacterDataAsset::GetGroupBinding(FGameplayTag Group, FAnimationGroupBinding& OutBinding) const
{
	if (const FAnimationGroupBinding* Binding = GroupBindings.Find(Group))
	{
		OutBinding = *Binding;
		return true;
	}
	return false;
}

bool UPaper2DPlusCharacterDataAsset::HasGroup(FGameplayTag Group) const
{
	return GroupBindings.Contains(Group);
}

TArray<FGameplayTag> UPaper2DPlusCharacterDataAsset::GetAllMappedGroups() const
{
	TArray<FGameplayTag> Tags;
	GroupBindings.GetKeys(Tags);
	return Tags;
}

int32 UPaper2DPlusCharacterDataAsset::GetAnimationCountForGroup(FGameplayTag Group) const
{
	if (const FAnimationGroupBinding* Binding = GroupBindings.Find(Group))
	{
		return Binding->AnimationNames.Num();
	}
	return 0;
}

// ==========================================
// ATTACK BOUNDS (AI HELPERS)
// ==========================================

namespace
{
	/** Compute max distance from origin to any edge of attack hitboxes across all frames. */
	float ComputeAttackRangeForAnimData(const FAnimationHitboxData& AnimData)
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
	FBox2D ComputeAttackBoundsForAnimData(const FAnimationHitboxData& AnimData)
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
	for (const FAnimationHitboxData& Anim : Animations)
	{
		MaxRange = FMath::Max(MaxRange, ComputeAttackRangeForAnimData(Anim));
	}
	return MaxRange;
}

float UPaper2DPlusCharacterDataAsset::GetAttackRangeForGroup(FGameplayTag Group) const
{
	float MaxRange = 0.0f;
	if (const FAnimationGroupBinding* Binding = GroupBindings.Find(Group))
	{
		for (const FString& AnimName : Binding->AnimationNames)
		{
			if (const FAnimationHitboxData* Anim = FindAnimationPtr(AnimName))
			{
				MaxRange = FMath::Max(MaxRange, ComputeAttackRangeForAnimData(*Anim));
			}
		}
	}
	return MaxRange;
}

float UPaper2DPlusCharacterDataAsset::GetAttackRangeForAnimation(const FString& AnimationName) const
{
	if (const FAnimationHitboxData* Anim = FindAnimationPtr(AnimationName))
	{
		return ComputeAttackRangeForAnimData(*Anim);
	}
	return 0.0f;
}

FBox2D UPaper2DPlusCharacterDataAsset::GetAttackBoundsForGroup(FGameplayTag Group) const
{
	FBox2D Bounds(ForceInit);
	bool bHasAny = false;

	if (const FAnimationGroupBinding* Binding = GroupBindings.Find(Group))
	{
		for (const FString& AnimName : Binding->AnimationNames)
		{
			if (const FAnimationHitboxData* Anim = FindAnimationPtr(AnimName))
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

FBox2D UPaper2DPlusCharacterDataAsset::GetAttackBoundsForAnimation(const FString& AnimationName) const
{
	if (const FAnimationHitboxData* Anim = FindAnimationPtr(AnimationName))
	{
		return ComputeAttackBoundsForAnimData(*Anim);
	}
	return FBox2D(ForceInit);
}

// ==========================================
// GROUP BINDING HELPERS
// ==========================================

void UPaper2DPlusCharacterDataAsset::RebuildGroupLookupCache() const
{
	GroupToAnimationIndicesCache.Empty();

	if (!bNameLookupCacheValid || Animations.Num() != NameToAnimationIndexCache.Num())
	{
		RebuildNameLookupCache();
	}

	for (const auto& Pair : GroupBindings)
	{
		TArray<int32> Indices;
		for (const FString& AnimName : Pair.Value.AnimationNames)
		{
			if (const int32* FoundIndex = NameToAnimationIndexCache.Find(AnimName.ToLower()))
			{
				if (Animations.IsValidIndex(*FoundIndex))
				{
					Indices.Add(*FoundIndex);
				}
			}
		}
		GroupToAnimationIndicesCache.Add(Pair.Key, MoveTemp(Indices));
	}

	bGroupLookupCacheValid = true;
}

void UPaper2DPlusCharacterDataAsset::UpdateGroupBindingAnimationName(const FString& OldName, const FString& NewName)
{
	for (auto& Pair : GroupBindings)
	{
		for (FString& AnimName : Pair.Value.AnimationNames)
		{
			if (AnimName.Equals(OldName, ESearchCase::IgnoreCase))
			{
				AnimName = NewName;
			}
		}
	}
	bGroupLookupCacheValid = false;
}

void UPaper2DPlusCharacterDataAsset::RemoveAnimationFromGroupBindings(const FString& AnimationName)
{
	for (auto& Pair : GroupBindings)
	{
		Pair.Value.AnimationNames.RemoveAll([&AnimationName](const FString& Name)
		{
			return Name.Equals(AnimationName, ESearchCase::IgnoreCase);
		});
	}
	bGroupLookupCacheValid = false;
}

