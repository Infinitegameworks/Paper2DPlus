// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "Paper2DPlusCharacterProfileAsset.h"
#include "Paper2DPlusSettings.h"
#include "Paper2DPlusAnimationTags.h"     // Context dimension root (TASK-108 U5 validator)
#include "Paper2DPlusAnimationTagQuery.h" // reaching-root batch (TASK-108 U5 validator)
#include "PaperSprite.h"
#include "PaperFlipbook.h"
#include "JsonObjectConverter.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
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

// Rewrite legacy JSON keys that have NO matching property to their current names BEFORE
// FJsonObjectConverter runs (it silently drops keys with no matching property).
//
// Only the "Animations" -> "Flipbooks" rename needs this: there is no "Animations" property and no
// "*_DEPRECATED" alias for it. EVERY OTHER renamed field is matched automatically, because UHT
// registers each *_DEPRECATED member under its BARE legacy name — e.g. FFlipbookProfileEntry's
// FlipbookName_DEPRECATED is registered as "FlipbookName", and FFlipbookTagMapping's
// FlipbookNames_DEPRECATED as "FlipbookNames" (verified in the generated reflection). The importer
// (FJsonObjectConverter, SkipFlags=0) does NOT skip deprecated properties and matches by authored
// (bare) name, so legacy keys like "FlipbookName"/"Frames"/"FlipbookNames"/"PaperZDSequences"
// populate those deprecated members directly; MigrateLoadedFlipbookSubStructs +
// MigrateTagMappingsToEntries then fold them forward. **Do NOT rewrite those keys to their
// "_DEPRECATED" forms** — the authored name is the bare form, so doing so would un-match them.
void ApplyLegacyJsonAliases(const TSharedRef<FJsonObject>& Root)
{
	if (!Root->HasField(TEXT("Animations")))
	{
		return;
	}

	// Both keys present (self-contradictory) — the current "Flipbooks" array wins. Warn rather than
	// silently discarding the legacy "Animations" data.
	if (Root->HasField(TEXT("Flipbooks")))
	{
		UE_LOG(LogTemp, Warning, TEXT("Paper2DPlus: JSON import has both legacy 'Animations' and current 'Flipbooks' arrays; ignoring 'Animations'."));
		return;
	}

	const TArray<TSharedPtr<FJsonValue>>* AnimArray = nullptr;
	if (Root->TryGetArrayField(TEXT("Animations"), AnimArray) && AnimArray)
	{
		Root->SetArrayField(TEXT("Flipbooks"), *AnimArray);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("Paper2DPlus: JSON import 'Animations' value is not an array; legacy animation data could not be migrated."));
	}
}

struct FSchemaSevenRootFlagCapture
{
	int32 Priority = 0;
	bool bValue = false;

	void Capture(const FString& FieldName, bool bInValue)
	{
		int32 CandidatePriority = 0;
		if (FieldName.Equals(TEXT("bIsComboRoot"), ESearchCase::CaseSensitive))
		{
			CandidatePriority = 4;
		}
		else if (FieldName.Equals(TEXT("IsComboRoot"), ESearchCase::CaseSensitive))
		{
			CandidatePriority = 3;
		}
		else if (FieldName.Equals(TEXT("bIsComboRoot"), ESearchCase::IgnoreCase))
		{
			CandidatePriority = 2;
		}
		else if (FieldName.Equals(TEXT("IsComboRoot"), ESearchCase::IgnoreCase))
		{
			CandidatePriority = 1;
		}

		if (CandidatePriority > Priority)
		{
			Priority = CandidatePriority;
			bValue = bInValue;
		}
	}

	bool IsTrue() const
	{
		return Priority > 0 && bValue;
	}
};

void CaptureSchemaSevenString(
	const FString& FieldName,
	const TCHAR* ExpectedFieldName,
	const FString& Value,
	int32& InOutPriority,
	FString& OutValue)
{
	const int32 CandidatePriority = FieldName.Equals(ExpectedFieldName, ESearchCase::CaseSensitive)
		? 2
		: (FieldName.Equals(ExpectedFieldName, ESearchCase::IgnoreCase) ? 1 : 0);
	if (CandidatePriority > InOutPriority)
	{
		InOutPriority = CandidatePriority;
		OutValue = Value;
	}
}

/**
 * Extract schema-7 root flags from the token stream before FJsonObject's case-insensitive key map
 * collapses case-only duplicates. Exact legacy spellings therefore win deterministically regardless
 * of authored field order. The current Flipbooks array wins over the Animations compatibility alias.
 */
void ExtractSchemaSevenLegacyRootNames(
	const FString& JsonString,
	TSet<FString>& OutRootNamesLower)
{
	enum class EAnimationArray : uint8
	{
		None,
		Flipbooks,
		Animations
	};

	OutRootNamesLower.Reset();
	TSet<FString> FlipbookRootNamesLower;
	TSet<FString> AnimationRootNamesLower;
	bool bSawFlipbooksArray = false;

	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
	EJsonNotation Notation = EJsonNotation::Error;
	int32 Depth = 0;
	int32 AnimationArrayDepth = INDEX_NONE;
	int32 FlipbookObjectDepth = INDEX_NONE;
	int32 IdentityObjectDepth = INDEX_NONE;
	int32 EditorMetaObjectDepth = INDEX_NONE;
	EAnimationArray ActiveArray = EAnimationArray::None;
	FSchemaSevenRootFlagCapture TopLevelRootFlag;
	FSchemaSevenRootFlagCapture EditorMetaRootFlag;
	FString IdentityFlipbookName;
	FString LegacyFlipbookName;
	int32 IdentityNamePriority = 0;
	int32 LegacyNamePriority = 0;

	auto ResetFlipbook = [&]()
	{
		TopLevelRootFlag = FSchemaSevenRootFlagCapture();
		EditorMetaRootFlag = FSchemaSevenRootFlagCapture();
		IdentityFlipbookName.Reset();
		LegacyFlipbookName.Reset();
		IdentityNamePriority = 0;
		LegacyNamePriority = 0;
	};

	auto FinishFlipbook = [&]()
	{
		if (!TopLevelRootFlag.IsTrue() && !EditorMetaRootFlag.IsTrue())
		{
			return;
		}

		FString FlipbookName = IdentityFlipbookName.IsEmpty()
			? LegacyFlipbookName
			: IdentityFlipbookName;
		FlipbookName.TrimStartAndEndInline();
		if (FlipbookName.IsEmpty())
		{
			return;
		}

		TSet<FString>& TargetSet = ActiveArray == EAnimationArray::Flipbooks
			? FlipbookRootNamesLower
			: AnimationRootNamesLower;
		TargetSet.Add(FlipbookName.ToLower());
	};

	while (Reader->ReadNext(Notation))
	{
		const FString& Identifier = Reader->GetIdentifier();
		switch (Notation)
		{
		case EJsonNotation::ObjectStart:
			if (AnimationArrayDepth != INDEX_NONE
				&& FlipbookObjectDepth == INDEX_NONE
				&& Depth == AnimationArrayDepth)
			{
				FlipbookObjectDepth = Depth + 1;
				ResetFlipbook();
			}
			else if (FlipbookObjectDepth != INDEX_NONE && Depth == FlipbookObjectDepth)
			{
				if (Identifier.Equals(TEXT("Identity"), ESearchCase::IgnoreCase))
				{
					IdentityObjectDepth = Depth + 1;
				}
				else if (Identifier.Equals(TEXT("EditorMeta"), ESearchCase::IgnoreCase))
				{
					EditorMetaObjectDepth = Depth + 1;
				}
			}
			++Depth;
			break;

		case EJsonNotation::ObjectEnd:
			if (Depth == IdentityObjectDepth)
			{
				IdentityObjectDepth = INDEX_NONE;
			}
			if (Depth == EditorMetaObjectDepth)
			{
				EditorMetaObjectDepth = INDEX_NONE;
			}
			if (Depth == FlipbookObjectDepth)
			{
				FinishFlipbook();
				FlipbookObjectDepth = INDEX_NONE;
			}
			--Depth;
			break;

		case EJsonNotation::ArrayStart:
			if (AnimationArrayDepth == INDEX_NONE
				&& FlipbookObjectDepth == INDEX_NONE
				&& Depth == 1)
			{
				if (Identifier.Equals(TEXT("Flipbooks"), ESearchCase::IgnoreCase))
				{
					ActiveArray = EAnimationArray::Flipbooks;
					AnimationArrayDepth = Depth + 1;
					bSawFlipbooksArray = true;
				}
				else if (Identifier.Equals(TEXT("Animations"), ESearchCase::IgnoreCase))
				{
					ActiveArray = EAnimationArray::Animations;
					AnimationArrayDepth = Depth + 1;
				}
			}
			++Depth;
			break;

		case EJsonNotation::ArrayEnd:
			if (Depth == AnimationArrayDepth)
			{
				AnimationArrayDepth = INDEX_NONE;
				ActiveArray = EAnimationArray::None;
			}
			--Depth;
			break;

		case EJsonNotation::String:
			if (FlipbookObjectDepth != INDEX_NONE)
			{
				if (Depth == IdentityObjectDepth)
				{
					CaptureSchemaSevenString(
						Identifier,
						TEXT("FlipbookName"),
						Reader->GetValueAsString(),
						IdentityNamePriority,
						IdentityFlipbookName);
				}
				else if (Depth == FlipbookObjectDepth)
				{
					CaptureSchemaSevenString(
						Identifier,
						TEXT("FlipbookName"),
						Reader->GetValueAsString(),
						LegacyNamePriority,
						LegacyFlipbookName);
				}
			}
			break;

		case EJsonNotation::Boolean:
			if (Depth == EditorMetaObjectDepth)
			{
				EditorMetaRootFlag.Capture(Identifier, Reader->GetValueAsBoolean());
			}
			else if (Depth == FlipbookObjectDepth)
			{
				TopLevelRootFlag.Capture(Identifier, Reader->GetValueAsBoolean());
			}
			break;

		case EJsonNotation::Error:
			UE_LOG(LogTemp, Warning,
				TEXT("Paper2DPlus: schema-7 root-flag token scan failed: %s"),
				*Reader->GetErrorMessage());
			return;

		default:
			break;
		}
	}

	OutRootNamesLower = bSawFlipbooksArray
		? MoveTemp(FlipbookRootNamesLower)
		: MoveTemp(AnimationRootNamesLower);
}

/**
 * Convert each schema-7 global root flag into a chain-start flag in every exact mapping that owns
 * that flipbook. (Numbered roots were retired for chain-start flags; the deterministic per-group
 * numbering this migration used to assign carried no meaning beyond identity, which the flag now
 * provides.)
 */
void MigrateSchemaSevenLegacyRootNumbers(
	FCharacterProfileAssetSerializablePayload& Payload,
	const TSet<FString>& LegacyRootNamesLower)
{
	if (LegacyRootNamesLower.IsEmpty())
	{
		return;
	}

	TSet<FString> ScopedLegacyRoots;
	int32 AssignedCount = 0;
	for (FSerializableTagMapping& GroupBinding : Payload.GroupBindings)
	{
		TSet<FString> AssignedNames;
		for (FFlipbookTagMappingEntry& Entry : GroupBinding.Binding.Entries)
		{
			const FString NameLower = Entry.FlipbookName.ToLower();
			if (!LegacyRootNamesLower.Contains(NameLower) || AssignedNames.Contains(NameLower))
			{
				continue;
			}

			AssignedNames.Add(NameLower);
			ScopedLegacyRoots.Add(NameLower);
			if (Entry.bIsChainStart)
			{
				continue;
			}
			Entry.bIsChainStart = true;
			++AssignedCount;
		}
	}

	for (const FString& LegacyRootName : LegacyRootNamesLower)
	{
		if (!ScopedLegacyRoots.Contains(LegacyRootName))
		{
			UE_LOG(LogTemp, Warning,
				TEXT("Paper2DPlus: schema-7 JSON root '%s' has no exact animation-group membership and cannot become a scoped Chain Start."),
				*LegacyRootName);
		}
	}
	UE_LOG(LogTemp, Log,
		TEXT("Paper2DPlus: created %d group-local Chain Start flag(s) from schema-7 global root flags."),
		AssignedCount);
}

UObject* FindTagMappingSequenceForFlipbook(
	const TMap<FGameplayTag, FFlipbookTagMapping>& TagMappings,
	const FString& FlipbookName,
	const FGameplayTag& TagToSkip,
	int32 EntryIndexToSkip)
{
	if (FlipbookName.IsEmpty())
	{
		return nullptr;
	}

	for (const TPair<FGameplayTag, FFlipbookTagMapping>& Pair : TagMappings)
	{
		const FFlipbookTagMapping& Mapping = Pair.Value;
		for (int32 Index = 0; Index < Mapping.Entries.Num(); ++Index)
		{
			if (Pair.Key == TagToSkip && Index == EntryIndexToSkip)
			{
				continue;
			}
			const FFlipbookTagMappingEntry& Entry = Mapping.Entries[Index];
			if (Entry.FlipbookName.Equals(FlipbookName, ESearchCase::IgnoreCase) && Entry.PaperZDSequence)
			{
				return Entry.PaperZDSequence.Get();
			}
		}
	}

	return nullptr;
}

#if WITH_EDITOR
bool ResolveTagBackedFlipbookGroup(const UPaper2DPlusCharacterProfileAsset* Asset, FName GroupName, FGameplayTag& OutTag)
{
	OutTag = FGameplayTag();
	if (GroupName.IsNone())
	{
		return false;
	}

	const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(GroupName, /*ErrorIfNotFound=*/false);
	if (!Tag.IsValid())
	{
		return false;
	}

	OutTag = Tag;
	return true;
}

bool EnsureFlipbookGroupForTagInternal(UPaper2DPlusCharacterProfileAsset* Asset, FGameplayTag Tag)
{
	if (!Asset || !Tag.IsValid())
	{
		return false;
	}

	const FName GroupName = Tag.GetTagName();
	if (Asset->HasFlipbookGroup(GroupName))
	{
		return false;
	}

	FFlipbookGroupInfo& NewGroup = Asset->FlipbookGroups.AddDefaulted_GetRef();
	NewGroup.GroupName = GroupName;
	return true;
}

bool SetFlipbookGroupToTagInternal(UPaper2DPlusCharacterProfileAsset* Asset, const FString& FlipbookName, FGameplayTag Tag)
{
	if (!Asset || FlipbookName.IsEmpty() || !Tag.IsValid())
	{
		return false;
	}

	EnsureFlipbookGroupForTagInternal(Asset, Tag);

	const FName GroupName = Tag.GetTagName();
	for (FFlipbookProfileEntry& Anim : Asset->Flipbooks)
	{
		if (Anim.Identity.FlipbookName.Equals(FlipbookName, ESearchCase::IgnoreCase))
		{
			if (Anim.FlipbookGroup == GroupName)
			{
				return false;
			}
			Anim.FlipbookGroup = GroupName;
			return true;
		}
	}

	return false;
}

bool ClearFlipbookGroupIfTagInternal(UPaper2DPlusCharacterProfileAsset* Asset, const FString& FlipbookName, FGameplayTag Tag)
{
	if (!Asset || FlipbookName.IsEmpty() || !Tag.IsValid())
	{
		return false;
	}

	const FName GroupName = Tag.GetTagName();
	for (FFlipbookProfileEntry& Anim : Asset->Flipbooks)
	{
		if (Anim.Identity.FlipbookName.Equals(FlipbookName, ESearchCase::IgnoreCase)
			&& Anim.FlipbookGroup == GroupName)
		{
			Anim.FlipbookGroup = NAME_None;
			return true;
		}
	}

	return false;
}

// Inverse of EnsureFlipbookGroupForTagInternal (legacy-cleanup 2026-07): when a tag mapping goes
// away, its auto-created tag-named visual group row must go with it — otherwise the empty group
// shell lingers in the browser's My Groups view. Straggler members (manual assignments the entry
// sweep missed) fall to Ungrouped/Unassigned; child groups reparent to root; the row is deleted.
bool RemoveFlipbookGroupForTagInternal(UPaper2DPlusCharacterProfileAsset* Asset, FGameplayTag Tag)
{
	if (!Asset || !Tag.IsValid())
	{
		return false;
	}

	const FName GroupName = Tag.GetTagName();
	int32 RowIndex = INDEX_NONE;
	for (int32 Index = 0; Index < Asset->FlipbookGroups.Num(); ++Index)
	{
		if (Asset->FlipbookGroups[Index].GroupName == GroupName)
		{
			RowIndex = Index;
			break;
		}
	}
	if (RowIndex == INDEX_NONE)
	{
		return false;
	}

	for (FFlipbookProfileEntry& Anim : Asset->Flipbooks)
	{
		if (Anim.FlipbookGroup == GroupName)
		{
			Anim.FlipbookGroup = NAME_None;
		}
	}
	for (FFlipbookGroupInfo& Group : Asset->FlipbookGroups)
	{
		if (Group.ParentGroup == GroupName)
		{
			Group.ParentGroup = NAME_None;
		}
	}
	Asset->FlipbookGroups.RemoveAt(RowIndex);
	return true;
}
#endif

}

FName Paper2DPlusCharacterProfileJson::GetTrackLayoutResetWarningCode()
{
	return TEXT("Paper2DPlus.CharacterProfile.Json.TrackLayoutReset");
}

PRAGMA_DISABLE_DEPRECATION_WARNINGS
int32 FFlipbookEffectData::GetEffectFrameCount() const
{
	return EffectFlipbook ? EffectFlipbook->GetNumKeyFrames() : 0;
}
PRAGMA_ENABLE_DEPRECATION_WARNINGS

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

void UPaper2DPlusCharacterProfileAsset::SyncFramesToFlipbook(int32 FlipbookIndex, bool bGrowOnly)
{
	if (!Flipbooks.IsValidIndex(FlipbookIndex)) return;

	FFlipbookProfileEntry& Anim = Flipbooks[FlipbookIndex];
	if (Anim.Identity.Flipbook.IsNull()) return;

	UPaperFlipbook* FB = Anim.Identity.Flipbook.LoadSynchronous();
	if (!FB) return;

	int32 FlipbookFrameCount = FB->GetNumKeyFrames();
	if (FlipbookFrameCount <= 0) return;

	// Resize an array to the flipbook frame count. Exact sync grows to add empty rows AND shrinks to trim
	// orphans; grow-only (the passive on-open repair) only adds missing rows, never truncating trailing
	// per-frame hitbox/extraction/motion data the user may still rely on.
	auto ResizeArray = [bGrowOnly, FlipbookFrameCount](auto& Array)
	{
		const bool bShouldResize = bGrowOnly ? (Array.Num() < FlipbookFrameCount)
		                                     : (Array.Num() != FlipbookFrameCount);
		if (bShouldResize)
		{
			Array.SetNum(FlipbookFrameCount);
		}
	};

	ResizeArray(Anim.CombatData.Frames);

	// Only sync FrameExtractionInfo / RootMotion when already populated (an empty array means "no data", not
	// "needs N empty rows") — matching the original behavior; grow-only still applies when they are populated.
	if (Anim.CombatData.FrameExtractionInfo.Num() > 0)
	{
		ResizeArray(Anim.CombatData.FrameExtractionInfo);
	}
	if (Anim.MotionData.RootMotion.Num() > 0)
	{
		ResizeArray(Anim.MotionData.RootMotion);
	}

	NormalizeFrameSourceIndices(Anim);
}

void UPaper2DPlusCharacterProfileAsset::SyncAllFramesToFlipbooks(bool bGrowOnly)
{
	for (int32 i = 0; i < Flipbooks.Num(); ++i)
	{
		SyncFramesToFlipbook(i, bGrowOnly);
	}
}

void UPaper2DPlusCharacterProfileAsset::PostInitProperties()
{
	Super::PostInitProperties();

	// Stamp the current root-motion schema on every genuine in-memory creation path (factory,
	// Duplicate, programmatic NewObject) so PostLoad's pre-v1 migration never re-flips already-correct
	// data. Guarded against:
	//   RF_ClassDefaultObject — the CDO default must stay 0 (delta-serialization baseline for legacy assets)
	//   RF_NeedLoad           — object is about to be serialized from disk; let PostLoad migrate it
	//   RF_WasLoaded          — object was loaded from disk; PostLoad owns its version
	if (!HasAnyFlags(RF_ClassDefaultObject | RF_NeedLoad | RF_WasLoaded))
	{
		RootMotionVersion = CurrentRootMotionVersion;
	}
}

void UPaper2DPlusCharacterProfileAsset::PostLoad()
{
	Super::PostLoad();

	// ─── Root motion Y-axis sign migration ───────────────────────────
	// Pre-v1 assets stored root motion Position.Y in pixel-space Y-down
	// without negation. The runtime now correctly negates Y (pixel Y-down
	// → world Z-up), so saved data must be flipped to match.
	if (RootMotionVersion < CurrentRootMotionVersion)
	{
		bool bMigrated = false;
		for (FFlipbookProfileEntry& Entry : Flipbooks)
		{
			for (FRootMotionFrameData& RM : Entry.MotionData.RootMotion)
			{
				if (!FMath::IsNearlyZero(RM.Position.Y))
				{
					RM.Position.Y = -RM.Position.Y;
					bMigrated = true;
				}
			}
		}
		RootMotionVersion = CurrentRootMotionVersion;
		if (bMigrated)
		{
			MarkPackageDirty();
			UE_LOG(LogTemp, Warning, TEXT("Paper2DPlus: Root motion Y values migrated for %s. Please re-save."), *GetName());
		}
	}

	// ─── Sub-struct migration (Phase 1b) — shared with JSON import ────
	MigrateLoadedFlipbookSubStructs();

	// (The legacy Effects / executable Frame Event → Cue conversions are GONE with the native cue
	//  classes they produced. Their legacy containers are left untouched and inert on load; nothing
	//  reads them at runtime, and re-authoring is a Frame Cues tab operation now.)

	// ─── Tag-mapping parallel-array → Entries migration (TASK-3) ──────
	MigrateTagMappingsToEntries();
	MigrateRootNumbersToChainStarts(); // legacy numbered roots → chain-start flags
	NormalizeTagMappingsToOneFlipbookHome();

	// ─── Move-transition purification: dedupe + drop deprecated values (TASK-108 U1) ───
	MigrateMoveTransitions();

	// ─── Legacy grouping cleanup (2026-07): retired phase groups + stale tag-backed group shells ───
	MigrateLegacyGrouping();

	// Clear retired CompletionFlags bits (Phases/Effects tabs were deleted) — keep only the
	// live task bits so the strip mask stays in sync with the editor completion meter.
	constexpr int32 RetiredCompletionBits = ~UPaper2DPlusCharacterProfileAsset::LiveTaskBits;
	for (FFlipbookProfileEntry& Anim : Flipbooks)
	{
		Anim.EditorMeta.CompletionFlags &= ~RetiredCompletionBits;
	}

	// ─── Auto-resolve SourceTexture and SpritesOutputPath from flipbook sprites ───
	// Audit F5/R4: this is AUTHORING-only work — it LoadSynchronous()es the flipbook just to read its
	// first sprite's source texture / package path (editor reimport/extraction metadata, unused at
	// runtime). Editor-only so a cooked/runtime PostLoad does no synchronous authoring load.
#if WITH_EDITOR
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
#endif // WITH_EDITOR

	bFlipbookLookupCacheValid = false;
	bNameLookupCacheValid = false;

	// Audit F5/R4: AutoPopulatePaperZDSequences scans the asset registry + LoadSynchronous()es soft refs
	// — authoring setup, EDITOR-ONLY. A cooked build must never run it on PostLoad (it is still
	// BlueprintCallable for an explicit editor/tooling rescan). The IsAsyncLoading defer remains so the
	// editor isn't doing the scan mid-streaming-load (the original deadlock guard).
#if WITH_EDITOR
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
#endif // WITH_EDITOR
}

void UPaper2DPlusCharacterProfileAsset::PostDuplicate(EDuplicateMode::Type DuplicateMode)
{
	Super::PostDuplicate(DuplicateMode);
#if WITH_EDITORONLY_DATA
	// The copied baseline remains useful source evidence, but the duplicate must not impersonate the original
	// Layer Asset's exclusive owner. A fresh Adopt and Bake All will establish a new token/hint atomically.
	LayerBakeOwnerToken.Invalidate();
	LayerBakeOwnerPathHint.Reset();
#endif
}

#if WITH_EDITOR
const FPaper2DPlusCharacterBaselineAnimation* UPaper2DPlusCharacterProfileAsset::FindCharacterBaseline(
	const FSoftObjectPath& FlipbookPath,
	const FString& LegacyName) const
{
#if WITH_EDITORONLY_DATA
	const FString WantedPath = FlipbookPath.ToString().ToLower();
	if (!WantedPath.IsEmpty())
	{
		return CharacterBaseline.FindByPredicate([&WantedPath](const FPaper2DPlusCharacterBaselineAnimation& Entry)
		{
			return Entry.Flipbook.ToSoftObjectPath().ToString().ToLower() == WantedPath;
		});
	}

	const FPaper2DPlusCharacterBaselineAnimation* Match = nullptr;
	for (const FPaper2DPlusCharacterBaselineAnimation& Entry : CharacterBaseline)
	{
		if (!Entry.LegacyAnimationName.Equals(LegacyName, ESearchCase::IgnoreCase))
		{
			continue;
		}
		if (Match)
		{
			return nullptr; // ambiguous legacy fallback fails closed
		}
		Match = &Entry;
	}
	return Match;
#else
	return nullptr;
#endif
}

FPaper2DPlusCharacterBaselineAnimation* UPaper2DPlusCharacterProfileAsset::FindCharacterBaselineMutable(
	const FSoftObjectPath& FlipbookPath,
	const FString& LegacyName)
{
#if WITH_EDITORONLY_DATA
	const FString WantedPath = FlipbookPath.ToString().ToLower();
	if (!WantedPath.IsEmpty())
	{
		return CharacterBaseline.FindByPredicate([&WantedPath](const FPaper2DPlusCharacterBaselineAnimation& Entry)
		{
			return Entry.Flipbook.ToSoftObjectPath().ToString().ToLower() == WantedPath;
		});
	}

	FPaper2DPlusCharacterBaselineAnimation* Match = nullptr;
	for (FPaper2DPlusCharacterBaselineAnimation& Entry : CharacterBaseline)
	{
		if (!Entry.LegacyAnimationName.Equals(LegacyName, ESearchCase::IgnoreCase))
		{
			continue;
		}
		if (Match)
		{
			return nullptr;
		}
		Match = &Entry;
	}
	return Match;
#else
	return nullptr;
#endif
}

bool UPaper2DPlusCharacterProfileAsset::CaptureCharacterBaselineFromRuntime(
	const FGuid& OwnerToken,
	const FString& OwnerPathHint,
	bool bReplaceExisting)
{
#if WITH_EDITORONLY_DATA
	if (!OwnerToken.IsValid() || (!CharacterBaseline.IsEmpty() && !bReplaceExisting))
	{
		return false;
	}

	TArray<FPaper2DPlusCharacterBaselineAnimation> Staged;
	Staged.Reserve(Flipbooks.Num());
	for (const FFlipbookProfileEntry& Source : Flipbooks)
	{
		FPaper2DPlusCharacterBaselineAnimation& Baseline = Staged.AddDefaulted_GetRef();
		Baseline.Flipbook = Source.Identity.Flipbook;
		Baseline.LegacyAnimationName = Source.Identity.FlipbookName;
		Baseline.Frames = Source.CombatData.Frames;

		Baseline.FrameCues.Reserve(Source.FrameEventData.FrameCues.Num());
		FPaper2DPlusFrameCueReplacementMap CueReplacements;
		for (const UPaper2DPlusCueBase* Cue : Source.FrameEventData.FrameCues)
		{
			UPaper2DPlusCueBase* Duplicate =
				Cue ? DuplicateObject<UPaper2DPlusCueBase>(Cue, this) : nullptr;
			Baseline.FrameCues.Add(Duplicate);
			if (Cue && Duplicate)
			{
				CueReplacements.Add(Cue, Duplicate);
			}
		}
		Baseline.CueTrackLayout = Source.FrameEventData.CueTrackLayout;
		Baseline.CueTrackLayout.RemapCueReferences(CueReplacements);
		TSet<const UPaper2DPlusCueBase*> BaselineCueDomain;
		for (const UPaper2DPlusCueBase* Cue : Baseline.FrameCues)
		{
			if (Cue) BaselineCueDomain.Add(Cue);
		}
		Baseline.CueTrackLayout.RetainCueAssignments(BaselineCueDomain);
		Baseline.LegacyFrameEvents.Reserve(Source.FrameEventData.FrameEvents.Num());
		for (const UPaper2DPlusFrameEventBase* Event : Source.FrameEventData.FrameEvents)
		{
			Baseline.LegacyFrameEvents.Add(Event ? DuplicateObject<UPaper2DPlusFrameEventBase>(Event, this) : nullptr);
		}
	}

	CharacterBaseline = MoveTemp(Staged);
	LayerBakeOwnerToken = OwnerToken;
	LayerBakeOwnerPathHint = OwnerPathHint;
	return true;
#else
	return false;
#endif
}
#endif // WITH_EDITOR

#if WITH_EDITOR
bool UPaper2DPlusCharacterProfileAsset::Modify(bool bAlwaysMarkDirty)
{
	// Every authored mutation announces itself here first (this project's panels all open a
	// transaction and call Modify() before touching data), so this is the one hook that sees a Frame
	// Cue added through a custom timeline as well as one added through the details panel.
	++EditorContentRevision;
	return Super::Modify(bAlwaysMarkDirty);
}

void UPaper2DPlusCharacterProfileAsset::PostEditUndo()
{
	// Undo restores bytes without routing through Modify(), so the revision has to advance here too or
	// undoing the removal of the last cue would leave a consumer memoized on the post-removal answer.
	++EditorContentRevision;
	bFlipbookLookupCacheValid = false;
	bNameLookupCacheValid = false;
	Super::PostEditUndo();
}

void UPaper2DPlusCharacterProfileAsset::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	++EditorContentRevision;
	bFlipbookLookupCacheValid = false;
	bNameLookupCacheValid = false;

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
	FlipbookPathToDataIndexCache.Empty();
	FlipbookPathToDataIndexCache.Reserve(Flipbooks.Num());
	ResidentFlipbookToDataIndexCache.Empty();
	ResidentFlipbookToDataIndexCache.Reserve(Flipbooks.Num());
	ExactAnimationNameToDataIndicesCache.Empty();
	ExactAnimationNameToDataIndicesCache.Reserve(Flipbooks.Num());

	for (int32 Index = 0; Index < Flipbooks.Num(); ++Index)
	{
		const FName AnimationName(*Flipbooks[Index].Identity.FlipbookName);
		if (!AnimationName.IsNone())
		{
			ExactAnimationNameToDataIndicesCache.FindOrAdd(AnimationName).Add(Index);
		}
		// Identity lookup must not turn a soft-reference inventory into a bulk preload. The caller
		// already owns the live UPaperFlipbook; its stable object path is enough to find the row.
		const TSoftObjectPtr<UPaperFlipbook>& FlipbookRef = Flipbooks[Index].Identity.Flipbook;
		const FSoftObjectPath FlipbookPath = FlipbookRef.ToSoftObjectPath();
		if (!FlipbookPath.IsNull())
		{
			FlipbookPathToDataIndexCache.FindOrAdd(FlipbookPath) = Index;
		}
		if (UPaperFlipbook* ResidentFlipbook = FlipbookRef.Get())
		{
			ResidentFlipbookToDataIndexCache.FindOrAdd(ResidentFlipbook) = Index;
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

bool UPaper2DPlusCharacterProfileAsset::GetFlipbookByIndex(int32 Index, FFlipbookProfileEntry& OutFlipbook) const
{
	if (Flipbooks.IsValidIndex(Index))
	{
		OutFlipbook = Flipbooks[Index];
		return true;
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

bool UPaper2DPlusCharacterProfileAsset::GetFramePivotLocal(UPaperFlipbook* Flipbook, int32 FrameIndex, FVector2D& OutPivotLocal) const
{
	OutPivotLocal = FVector2D::ZeroVector;
	if (!Flipbook || !Flipbook->IsValidKeyFrameIndex(FrameIndex))
	{
		return false;
	}

#if WITH_EDITOR
	// Editor: compute the pivot live (GetPivotPosition/GetSourceUV are editor-only on UPaperSprite).
	if (UPaperSprite* Sprite = Flipbook->GetKeyFrameChecked(FrameIndex).Sprite)
	{
		OutPivotLocal = Sprite->GetPivotPosition() - Sprite->GetSourceUV();
		return !OutPivotLocal.ContainsNaN();
	}
	return false;
#else
	// Packaged: those sprite APIs are stripped, so read the pivot baked into the profile at cook (TASK-48).
	// FrameIndex is a LIVE key-frame index; FrameExtractionInfo is index-parallel to the live key frames
	// (the same invariant CombatData.Frames[FrameIndex] already relies on — exclude removes from both
	// arrays in lockstep), and RepopulatePivotCache fills entry [i] from key-frame i.
	if (const FFlipbookProfileEntry* Entry = FindByFlipbookPtr(Flipbook))
	{
		return GetFramePivotLocalForEntry(*Entry, Flipbook, FrameIndex, OutPivotLocal);
	}
	return false;
#endif
}

bool UPaper2DPlusCharacterProfileAsset::GetFramePivotLocalForEntry(
	const FFlipbookProfileEntry& Entry,
	UPaperFlipbook* Flipbook,
	int32 FrameIndex,
	FVector2D& OutPivotLocal) const
{
	OutPivotLocal = FVector2D::ZeroVector;
	if (!Flipbook || !Flipbook->IsValidKeyFrameIndex(FrameIndex))
	{
		return false;
	}

#if WITH_EDITOR
	if (UPaperSprite* Sprite = Flipbook->GetKeyFrameChecked(FrameIndex).Sprite)
	{
		OutPivotLocal = Sprite->GetPivotPosition() - Sprite->GetSourceUV();
		return !OutPivotLocal.ContainsNaN();
	}
	return false;
#else
	if (!Entry.CombatData.FrameExtractionInfo.IsValidIndex(FrameIndex))
	{
		return false;
	}

	const FSpriteExtractionInfo& Info = Entry.CombatData.FrameExtractionInfo[FrameIndex];
	if (!Info.IsPivotCached())
	{
		return false;
	}

	OutPivotLocal = Info.CachedPivotLocal;
	return !OutPivotLocal.ContainsNaN();
#endif
}

#if WITH_EDITOR
void UPaper2DPlusCharacterProfileAsset::RepopulatePivotCache()
{
	for (FFlipbookProfileEntry& Entry : Flipbooks)
	{
		UPaperFlipbook* FB = Entry.Identity.Flipbook.Get();
		if (!FB)
		{
			FB = Entry.Identity.Flipbook.LoadSynchronous();
		}
		if (!FB)
		{
			continue;
		}

		// FrameExtractionInfo is index-parallel to the live key frames (see SyncFramesToFlipbook),
		// which is exactly the index the runtime pivot path looks up. Write only existing entries.
		const int32 Count = FMath::Min(FB->GetNumKeyFrames(), Entry.CombatData.FrameExtractionInfo.Num());
		for (int32 i = 0; i < Count; ++i)
		{
			if (UPaperSprite* Sprite = FB->GetKeyFrameChecked(i).Sprite)
			{
				Entry.CombatData.FrameExtractionInfo[i].CachedPivotLocal =
					Sprite->GetPivotPosition() - Sprite->GetSourceUV();
			}
		}
	}
}

void UPaper2DPlusCharacterProfileAsset::PreSave(FObjectPreSaveContext SaveContext)
{
	// Bake the live sprite pivots into the serialized per-frame cache so cooked/packaged builds get
	// pivot-correct runtime hitbox/socket conversion (the sprite pivot APIs are editor-only). Running it on
	// PreSave means every cook AND every manual save refreshes it, so existing assets migrate automatically.
	RepopulatePivotCache();
	Super::PreSave(SaveContext);
}
#endif

const FFlipbookProfileEntry* UPaper2DPlusCharacterProfileAsset::FindExactFlipbookData(
	FName AnimationName,
	UPaperFlipbook* Flipbook,
	bool& bOutAmbiguous) const
{
	bOutAmbiguous = false;
	if (AnimationName.IsNone() || !Flipbook)
	{
		return nullptr;
	}

	if (!bFlipbookLookupCacheValid || Flipbooks.Num() != CachedFlipbookCount)
	{
		RebuildFlipbookLookupCache();
	}

	const FSoftObjectPath FlipbookPath(Flipbook);
	const FFlipbookProfileEntry* Match = nullptr;
	const TArray<int32>* CandidateIndices =
		ExactAnimationNameToDataIndicesCache.Find(AnimationName);
	if (!CandidateIndices)
	{
		// Repair the one missed key rather than reporting "no such animation". A same-count mutation
		// (a rename above all else) can leave this cache stale with no count change to detect it, and
		// the caller — Frame Cue anchor resolution — cannot tell a stale cache from a deleted row. Both
		// sibling lookups already degrade to a scan here; without it this one fails closed and wrong.
		TArray<int32> RepairedIndices;
		for (int32 Index = 0; Index < Flipbooks.Num(); ++Index)
		{
			if (FName(*Flipbooks[Index].Identity.FlipbookName) == AnimationName)
			{
				RepairedIndices.Add(Index);
			}
		}
		if (RepairedIndices.Num() == 0)
		{
			return nullptr;
		}
		CandidateIndices = &ExactAnimationNameToDataIndicesCache.Add(
			AnimationName,
			MoveTemp(RepairedIndices));
	}

	for (const int32 CandidateIndex : *CandidateIndices)
	{
		if (!Flipbooks.IsValidIndex(CandidateIndex))
		{
			continue;
		}
		const FFlipbookProfileEntry& Entry = Flipbooks[CandidateIndex];
		const TSoftObjectPtr<UPaperFlipbook>& EntryRef = Entry.Identity.Flipbook;
		if (EntryRef.Get() != Flipbook
			&& (FlipbookPath.IsNull() || EntryRef.ToSoftObjectPath() != FlipbookPath))
		{
			continue;
		}
		if (Match)
		{
			bOutAmbiguous = true;
			return nullptr;
		}
		Match = &Entry;
	}

	return Match;
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

	const TWeakObjectPtr<UPaperFlipbook> ResidentKey(Flipbook);
	const int32* FoundIndex = ResidentFlipbookToDataIndexCache.Find(ResidentKey);
	if (FoundIndex && Flipbooks.IsValidIndex(*FoundIndex)
		&& Flipbooks[*FoundIndex].Identity.Flipbook.Get() == Flipbook)
	{
		return &Flipbooks[*FoundIndex];
	}

	const FSoftObjectPath FlipbookPath(Flipbook);
	FoundIndex = FlipbookPath.IsNull()
		? nullptr
		: FlipbookPathToDataIndexCache.Find(FlipbookPath);
	if (FoundIndex && Flipbooks.IsValidIndex(*FoundIndex))
	{
		const FFlipbookProfileEntry& Anim = Flipbooks[*FoundIndex];
		if (Anim.Identity.Flipbook.Get() == Flipbook
			|| Anim.Identity.Flipbook.ToSoftObjectPath() == FlipbookPath)
		{
			return &Anim;
		}
	}

	// Same-count reimports/reorders can leave an otherwise-valid cache pointing at old rows, and
	// redirected soft paths can resolve to a live object whose destination path differs from the
	// authored path. Repair only the missed identity without loading any other soft references.
	for (int32 Index = Flipbooks.Num() - 1; Index >= 0; --Index)
	{
		const TSoftObjectPtr<UPaperFlipbook>& EntryRef = Flipbooks[Index].Identity.Flipbook;
		const FSoftObjectPath EntryPath = EntryRef.ToSoftObjectPath();
		if (EntryRef.Get() != Flipbook && (FlipbookPath.IsNull() || EntryPath != FlipbookPath))
		{
			continue;
		}

		ResidentFlipbookToDataIndexCache.FindOrAdd(ResidentKey) = Index;
		if (!EntryPath.IsNull())
		{
			FlipbookPathToDataIndexCache.FindOrAdd(EntryPath) = Index;
		}
		return &Flipbooks[Index];
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

// ==========================================
// OBJECT-REFERENCE VARIANTS (UPaperFlipbook* in/out)
// ==========================================
// Object-keyed siblings of the name accessors above. Each resolves the entry via the same
// FindByFlipbookPtr lookup the runtime uses (GetActorCurveValue / GetCurrentMoveName convention),
// then runs the identical extraction as its name twin so results can never diverge. The attack-bounds
// pair (GetAttackRangeByFlipbook / GetAttackBoundsByFlipbook) lives next to its name twins further down
// because the Compute* helpers they share are file-static there.

UPaperFlipbook* UPaper2DPlusCharacterProfileAsset::GetFlipbookByName(const FString& FlipbookName) const
{
	if (const FFlipbookProfileEntry* Anim = FindFlipbookData(FlipbookName))
	{
		// Unlike the object-to-entry identity path, this function explicitly returns an object and
		// therefore resolves this one requested soft reference when it is not already resident.
		UPaperFlipbook* FB = Anim->Identity.Flipbook.Get();
		return FB ? FB : Anim->Identity.Flipbook.LoadSynchronous();
	}
	return nullptr;
}

FString UPaper2DPlusCharacterProfileAsset::GetFlipbookName(UPaperFlipbook* Flipbook) const
{
	const FFlipbookProfileEntry* Anim = FindByFlipbookPtr(Flipbook);
	return Anim ? Anim->Identity.FlipbookName : FString();
}

bool UPaper2DPlusCharacterProfileAsset::ContainsFlipbook(UPaperFlipbook* Flipbook) const
{
	return FindByFlipbookPtr(Flipbook) != nullptr;
}

int32 UPaper2DPlusCharacterProfileAsset::GetFrameCountByFlipbook(UPaperFlipbook* Flipbook) const
{
	if (const FFlipbookProfileEntry* Anim = FindByFlipbookPtr(Flipbook))
	{
		return Anim->CombatData.Frames.Num();
	}
	return 0;
}

bool UPaper2DPlusCharacterProfileAsset::GetFrameByFlipbook(UPaperFlipbook* Flipbook, int32 FrameIndex, FFrameHitboxData& OutFrame) const
{
	if (const FFlipbookProfileEntry* Anim = FindByFlipbookPtr(Flipbook))
	{
		if (const FFrameHitboxData* Frame = Anim->GetFrame(FrameIndex))
		{
			OutFrame = *Frame;
			return true;
		}
	}
	return false;
}

TArray<FHitboxData> UPaper2DPlusCharacterProfileAsset::GetHitboxesByFlipbook(UPaperFlipbook* Flipbook, int32 FrameIndex) const
{
	if (const FFlipbookProfileEntry* Anim = FindByFlipbookPtr(Flipbook))
	{
		if (const FFrameHitboxData* Frame = Anim->GetFrame(FrameIndex))
		{
			return Frame->Hitboxes;
		}
	}
	return TArray<FHitboxData>();
}

TArray<FHitboxData> UPaper2DPlusCharacterProfileAsset::GetHitboxesOfTypeByFlipbook(UPaperFlipbook* Flipbook, int32 FrameIndex, EHitboxType Type) const
{
	if (const FFlipbookProfileEntry* Anim = FindByFlipbookPtr(Flipbook))
	{
		if (const FFrameHitboxData* Frame = Anim->GetFrame(FrameIndex))
		{
			return Frame->GetHitboxesByType(Type);
		}
	}
	return TArray<FHitboxData>();
}

TArray<FSocketData> UPaper2DPlusCharacterProfileAsset::GetSocketsByFlipbook(UPaperFlipbook* Flipbook, int32 FrameIndex) const
{
	if (const FFlipbookProfileEntry* Anim = FindByFlipbookPtr(Flipbook))
	{
		if (const FFrameHitboxData* Frame = Anim->GetFrame(FrameIndex))
		{
			return Frame->Sockets;
		}
	}
	return TArray<FSocketData>();
}

bool UPaper2DPlusCharacterProfileAsset::FindSocketByFlipbook(UPaperFlipbook* Flipbook, int32 FrameIndex, const FString& SocketName, FSocketData& OutSocket) const
{
	if (const FFlipbookProfileEntry* Anim = FindByFlipbookPtr(Flipbook))
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

	// Remap frame-event anchors in lockstep with the removed key frame: events anchored on FrameIndex
	// are stashed onto the excluded frame (so restore can reattach them); all others shift down (TASK-61).
	{
		TArray<int32> OldToNew;
		OldToNew.SetNum(KeyFrameCount);
		for (int32 i = 0; i < KeyFrameCount; ++i)
		{
			OldToNew[i] = (i == FrameIndex) ? INDEX_NONE : (i > FrameIndex ? i - 1 : i);
		}
		RemapFrameEventAnchors(Anim, OldToNew, KeyFrameCount - 1, &ExcludedFrame.StashedFrameEvents);
		RemapFrameCueAnchors(Anim, OldToNew, KeyFrameCount - 1, &ExcludedFrame.StashedFrameCues);
		// Curve points remap in lockstep (TASK-74): a key on the excluded frame is stashed onto it.
		RemapFrameCurveAnchors(Anim, OldToNew, KeyFrameCount - 1, &ExcludedFrame.StashedCurvePoints);
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
	// Restore frame-event anchors (TASK-61): shift live events at/after the insertion up by one, then
	// reattach the stashed events to the restored frame's new key-frame index.
	{
		TArray<int32> OldToNew;
		OldToNew.SetNum(KeyFrameCount);
		for (int32 i = 0; i < KeyFrameCount; ++i)
		{
			OldToNew[i] = (i < InsertIndex) ? i : i + 1;
		}
		RemapFrameEventAnchors(Anim, OldToNew, KeyFrameCount + 1, nullptr);
		RemapFrameCueAnchors(Anim, OldToNew, KeyFrameCount + 1, nullptr);
		for (UPaper2DPlusFrameEventBase* Stashed : ExcludedFrame.StashedFrameEvents)
		{
			if (Stashed)
			{
				Stashed->SetPrimaryAnchorFrame(InsertIndex);
				Anim.FrameEventData.FrameEvents.Add(Stashed);
			}
		}
		for (UPaper2DPlusCueBase* Stashed : ExcludedFrame.StashedFrameCues)
		{
			if (Stashed)
			{
				Stashed->SetPrimaryAnchorFrame(InsertIndex);
				Anim.FrameEventData.FrameCues.Add(Stashed);
			}
		}
		// Curve points (TASK-74): shift live keys at/after the insertion up by one, then reattach each
		// stashed curve value onto the restored frame's key-frame index.
		RemapFrameCurveAnchors(Anim, OldToNew, KeyFrameCount + 1, nullptr);
		for (const TPair<FName, float>& StashedPoint : ExcludedFrame.StashedCurvePoints)
		{
			if (FPaper2DPlusFrameCurve* FrameCurve = Anim.CurveData.Curves.Find(StashedPoint.Key))
			{
				FrameCurve->SetKeyValue(InsertIndex, StashedPoint.Value);
			}
		}
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

			// Frame-event anchors (TASK-61): shift live events at/after InsertIndex up by one, then
			// reattach this excluded frame's stashed events at InsertIndex — done incrementally per
			// insertion so events stay correct as later (lower-index) frames push them up again.
			{
				const int32 PreInsertCount = Anim.CombatData.Frames.Num();
				TArray<int32> OldToNew;
				OldToNew.SetNum(PreInsertCount);
				for (int32 i = 0; i < PreInsertCount; ++i)
				{
					OldToNew[i] = (i < InsertIndex) ? i : i + 1;
				}
				RemapFrameEventAnchors(Anim, OldToNew, PreInsertCount + 1, nullptr);
				RemapFrameCueAnchors(Anim, OldToNew, PreInsertCount + 1, nullptr);
		for (UPaper2DPlusFrameEventBase* Stashed : ExcludedFrame.StashedFrameEvents)
				{
					if (Stashed)
					{
						Stashed->SetPrimaryAnchorFrame(InsertIndex);
				Anim.FrameEventData.FrameEvents.Add(Stashed);
					}
				}
				for (UPaper2DPlusCueBase* Stashed : ExcludedFrame.StashedFrameCues)
				{
					if (Stashed)
					{
						Stashed->SetPrimaryAnchorFrame(InsertIndex);
						Anim.FrameEventData.FrameCues.Add(Stashed);
					}
				}
				// Curve points (TASK-74): shift live keys up, then reattach this frame's stashed values.
				RemapFrameCurveAnchors(Anim, OldToNew, PreInsertCount + 1, nullptr);
				for (const TPair<FName, float>& StashedPoint : ExcludedFrame.StashedCurvePoints)
				{
					if (FPaper2DPlusFrameCurve* FrameCurve = Anim.CurveData.Curves.Find(StashedPoint.Key))
					{
						FrameCurve->SetKeyValue(InsertIndex, StashedPoint.Value);
					}
				}
			}

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

void UPaper2DPlusCharacterProfileAsset::RemapFrameEventAnchors(FFlipbookProfileEntry& Anim, const TArray<int32>& OldToNew, int32 NumNewFrames, TArray<TObjectPtr<UPaper2DPlusFrameEventBase>>* OutStashed)
{
	TArray<TObjectPtr<UPaper2DPlusFrameEventBase>>& Events = Anim.FrameEventData.FrameEvents;
	for (int32 i = Events.Num() - 1; i >= 0; --i)
	{
		UPaper2DPlusFrameEventBase* Event = Events[i];
		if (!Event)
		{
			continue;
		}
		if (Event->RemapFrameAnchors(OldToNew, NumNewFrames))
		{
			continue; // anchor remapped (or event has no frame anchor) — keep in place
		}
		// Primary anchor frame was removed. Policy: stash on the excluded frame so restore can
		// reattach (OutStashed provided); otherwise drop with a warning rather than leave it dangling.
		if (OutStashed)
		{
			OutStashed->Add(Event);
		}
		else
		{
			UE_LOG(LogTemp, Warning,
				TEXT("Paper2DPlus: legacy Frame Event '%s' was anchored on a removed key frame and has been dropped during compatibility remapping."),
				*Event->GetName());
		}
		Events.RemoveAt(i);
	}
}

void UPaper2DPlusCharacterProfileAsset::RemapFrameCueAnchors(
	FFlipbookProfileEntry& Anim,
	const TArray<int32>& OldToNew,
	int32 NumNewFrames,
	TArray<TObjectPtr<UPaper2DPlusCueBase>>* OutStashed)
{
	TArray<TObjectPtr<UPaper2DPlusCueBase>>& Cues = Anim.FrameEventData.FrameCues;
	for (int32 Index = Cues.Num() - 1; Index >= 0; --Index)
	{
		UPaper2DPlusCueBase* Cue = Cues[Index];
		if (!Cue || Cue->RemapFrameAnchors(OldToNew, NumNewFrames))
		{
			continue;
		}
		if (OutStashed)
		{
			OutStashed->Add(Cue);
		}
		else
		{
			// This is a permanent deletion rather than the exclusion stash path. Release the
			// editor-only object-key membership before the authoritative array reference goes away.
#if WITH_EDITOR
			Anim.FrameEventData.CueTrackLayout.RemoveCue(Cue);
#endif
			UE_LOG(LogTemp, Warning,
				TEXT("Paper2DPlus: frame cue '%s' was anchored on a removed key frame and has been dropped."),
				*Cue->GetName());
		}
		Cues.RemoveAt(Index);
	}
}

void UPaper2DPlusCharacterProfileAsset::RemapFrameCurveAnchors(FFlipbookProfileEntry& Anim, const TArray<int32>& OldToNew, int32 NumNewFrames, TMap<FName, float>* OutStashed)
{
	// Curve keys are pinned to key-frame indices (key Time == frame index). Rebuild each curve's key set
	// in the new index space: a surviving key moves to OldToNew[oldFrame]; a key whose frame was removed
	// (INDEX_NONE) is stashed (or dropped). Mirrors RemapFrameEventAnchors but per-key-per-curve.
	for (TPair<FName, FPaper2DPlusFrameCurve>& Pair : Anim.CurveData.Curves)
	{
		FPaper2DPlusFrameCurve& FrameCurve = Pair.Value;
		const TArray<FRichCurveKey> OldKeys = FrameCurve.Curve.GetCopyOfKeys();
		if (OldKeys.Num() == 0)
		{
			continue;
		}

		const ERichCurveInterpMode RichMode = FPaper2DPlusFrameCurve::ToRichCurveInterpMode(FrameCurve.Mode);

		// Clear and re-add so the rebuilt keys are sorted and any removed frames are gone.
		FrameCurve.Curve.Reset();
		for (const FRichCurveKey& OldKey : OldKeys)
		{
			const int32 OldFrame = FMath::RoundToInt(OldKey.Time);
			if (!OldToNew.IsValidIndex(OldFrame))
			{
				// Orphaned key (frame index outside the current key-frame range — e.g. stale data): DROP it rather
				// than clamp it onto a live frame, where it could silently overwrite a legitimate key (review finding).
				continue;
			}
			const int32 NewFrame = OldToNew[OldFrame];

			if (NewFrame == INDEX_NONE)
			{
				// Frame removed: stash this curve's value so a restore can reattach it.
				if (OutStashed)
				{
					OutStashed->Add(Pair.Key, OldKey.Value);
				}
				continue;
			}

			const int32 ClampedFrame = FMath::Clamp(NewFrame, 0, FMath::Max(0, NumNewFrames - 1));
			const FKeyHandle Handle = FrameCurve.Curve.UpdateOrAddKey(static_cast<float>(ClampedFrame), OldKey.Value);
			FrameCurve.Curve.SetKeyInterpMode(Handle, RichMode);
		}
	}
}

bool UPaper2DPlusCharacterProfileAsset::MoveFlipbookFrame(int32 FlipbookIndex, int32 FromIndex, int32 ToIndex)
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
	if (FromIndex < 0 || FromIndex >= KeyFrameCount || ToIndex < 0 || ToIndex >= KeyFrameCount)
	{
		return false;
	}
	if (FromIndex == ToIndex)
	{
		return false; // No-op — nothing to reorder.
	}

	// NOTE: Does NOT call Modify() — callers manage transactions via BeginTransaction/EndTransaction.

	// Align the parallel per-frame metadata with the live keyframe list before
	// permuting, so all four arrays move in lockstep (mirrors ExcludeFlipbookFrame).
	if (Anim.CombatData.Frames.Num() != KeyFrameCount)
	{
		Anim.CombatData.Frames.SetNum(KeyFrameCount);
	}
	if (Anim.CombatData.FrameExtractionInfo.Num() != KeyFrameCount)
	{
		Anim.CombatData.FrameExtractionInfo.SetNum(KeyFrameCount);
	}
	const bool bHasRootMotion = Anim.MotionData.RootMotion.Num() > 0;
	if (bHasRootMotion && Anim.MotionData.RootMotion.Num() != KeyFrameCount)
	{
		Anim.MotionData.RootMotion.SetNum(KeyFrameCount);
	}

	NormalizeFrameSourceIndices(Anim);

	// RemoveAt(From) + Insert(To): the moved element lands at index ToIndex. Both
	// indices are validated < KeyFrameCount above, so after the removal the array
	// has KeyFrameCount-1 elements and ToIndex is always a legal insertion point.
	auto MoveElement = [](auto& Array, int32 From, int32 To)
	{
		if (!Array.IsValidIndex(From))
		{
			return;
		}
		auto Element = Array[From];
		Array.RemoveAt(From);
		Array.Insert(MoveTemp(Element), FMath::Clamp(To, 0, Array.Num()));
	};

	{
		FScopedFlipbookMutator Mutator(Flipbook);
		MoveElement(Mutator.KeyFrames, FromIndex, ToIndex);
	}
	MoveElement(Anim.CombatData.Frames, FromIndex, ToIndex);
	MoveElement(Anim.CombatData.FrameExtractionInfo, FromIndex, ToIndex);
	if (bHasRootMotion)
	{
		MoveElement(Anim.MotionData.RootMotion, FromIndex, ToIndex);
	}

	// Carry frame-event anchors with their key frame across the reorder (TASK-61). No frame is
	// removed, so nothing is stashed; the moved frame's events follow it to ToIndex.
	{
		TArray<int32> OldToNew;
		OldToNew.SetNum(KeyFrameCount);
		for (int32 i = 0; i < KeyFrameCount; ++i)
		{
			if (i == FromIndex)                                                  { OldToNew[i] = ToIndex; }
			else if (FromIndex < ToIndex && i > FromIndex && i <= ToIndex)        { OldToNew[i] = i - 1; }
			else if (FromIndex > ToIndex && i >= ToIndex && i < FromIndex)        { OldToNew[i] = i + 1; }
			else                                                                 { OldToNew[i] = i; }
		}
		RemapFrameEventAnchors(Anim, OldToNew, KeyFrameCount, nullptr);
		RemapFrameCueAnchors(Anim, OldToNew, KeyFrameCount, nullptr);
		// Curve points follow their key frame across the reorder (TASK-74). No frame removed = no stash.
		RemapFrameCurveAnchors(Anim, OldToNew, KeyFrameCount, nullptr);
	}

	// Re-stamp SourceFrameIndex to the new positional order. The Sprite Editor frame
	// strip sorts and labels cells by SourceFrameIndex (not array position), so if the
	// moved frame kept its old SourceFrameIndex the strip would re-sort it back into its
	// original slot — the live keyframes (and playback) would move but the UI would not.
	// Resetting to INDEX_NONE makes NormalizeFrameSourceIndices assign strictly by
	// position; any excluded frames pack after the active ones.
	for (FSpriteExtractionInfo& Info : Anim.CombatData.FrameExtractionInfo)
	{
		Info.SourceFrameIndex = INDEX_NONE;
	}
	for (FExcludedFlipbookFrameData& Excluded : Anim.CombatData.ExcludedFrames)
	{
		Excluded.ExtractionInfo.SourceFrameIndex = INDEX_NONE;
	}
	NormalizeFrameSourceIndices(Anim);

	Flipbook->MarkPackageDirty();
	MarkPackageDirty();
	return true;
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

void UPaper2DPlusCharacterProfileAsset::MigrateLoadedFlipbookSubStructs()
{
	// Old assets/JSON stored these fields at the top level of FFlipbookProfileEntry. The
	// UPROPERTY(meta=(DeprecatedProperty)) members receive that data (binary loads strip the
	// "_DEPRECATED" suffix; JSON imports route legacy keys here via ApplyLegacyJsonAliases).
	// Move it into the sub-struct homes. Idempotent: only migrates when the destination is empty.
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
			Anim.Identity.PaperZDSequence = Anim.PaperZDSequence_DEPRECATED.Get();
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
}

void UPaper2DPlusCharacterProfileAsset::MigrateTagMappingsToEntries()
{
	for (auto& Pair : TagMappings)
	{
		FFlipbookTagMapping& Mapping = Pair.Value;

		// Idempotent: only fold the legacy parallel arrays into Entries when Entries is
		// still empty AND legacy data is present (re-save / re-import safe).
		if (Mapping.Entries.Num() == 0 && Mapping.FlipbookNames_DEPRECATED.Num() > 0)
		{
			const int32 Count = Mapping.FlipbookNames_DEPRECATED.Num();
			Mapping.Entries.Reserve(Count);
			for (int32 Index = 0; Index < Count; ++Index)
			{
				UObject* Sequence = Mapping.PaperZDSequences_DEPRECATED.IsValidIndex(Index)
					? ToRawPtr(Mapping.PaperZDSequences_DEPRECATED[Index])
					: nullptr;
				Mapping.Entries.Emplace(Mapping.FlipbookNames_DEPRECATED[Index], Sequence);
			}
		}

		// Always release the legacy arrays: once Entries is authoritative the parallel data
		// is dead weight (and would otherwise re-trigger the migration heuristic on re-entry).
		Mapping.FlipbookNames_DEPRECATED.Empty();
		Mapping.PaperZDSequences_DEPRECATED.Empty();
	}
}

int32 UPaper2DPlusCharacterProfileAsset::DedupeTransitionRows()
{
	// TASK-108 U1: one transition row per (owning flipbook, TargetMove case-insensitive) pair,
	// first-in-array wins (consistent with the historical first-match contract). Empty-target rows are
	// authoring-in-progress and exempt (two draft rows may coexist). Info logs only — never a Warning
	// (headless suites treat warnings as failures, and this is expected data hygiene).
	int32 RowsRemoved = 0;
	for (FFlipbookProfileEntry& Anim : Flipbooks)
	{
		TArray<FPaper2DPlusMoveTransition>& Rows = Anim.TransitionData.Transitions;
		TSet<FString> SeenTargetsLower;
		for (int32 RowIndex = 0; RowIndex < Rows.Num(); /* advanced below */)
		{
			const FString& Target = Rows[RowIndex].TargetMove;
			if (Target.IsEmpty())
			{
				++RowIndex;
				continue;
			}
			const FString TargetLower = Target.ToLower();
			if (SeenTargetsLower.Contains(TargetLower))
			{
				UE_LOG(LogTemp, Log,
					TEXT("Paper2DPlus: %s: dropped duplicate transition row %s -> %s (one edge per From->To pair; the first authored row wins, TASK-108)."),
					*GetName(), *Anim.Identity.FlipbookName, *Target);
				Rows.RemoveAt(RowIndex);
				++RowsRemoved;
				continue; // the same index now holds the next row
			}
			SeenTargetsLower.Add(TargetLower);
			++RowIndex;
		}
	}
	return RowsRemoved;
}

int32 UPaper2DPlusCharacterProfileAsset::MigrateMoveTransitions()
{
	// (a) Dedupe FIRST so first-in-array-wins is judged on the authored array, before any values drop.
	int32 Changes = DedupeTransitionRows();

	// (b) Drop the soft-deprecated per-row values loaded into the *_DEPRECATED members (binary loads
	// strip the "_DEPRECATED" suffix; legacy JSON keys land via the same bare-name registration).
	// Accepted data loss, decided with the user (TASK-108) — one Info log per dropped value so the
	// drop is visible exactly once (idempotent: cleared fields never re-log).
	for (FFlipbookProfileEntry& Anim : Flipbooks)
	{
		for (FPaper2DPlusMoveTransition& Row : Anim.TransitionData.Transitions)
		{
			if (!Row.Tag_DEPRECATED.IsNone())
			{
				UE_LOG(LogTemp, Log,
					TEXT("Paper2DPlus: %s: dropped transition name '%s' on %s -> %s (transitions are pure From->To arrows since TASK-108)."),
					*GetName(), *Row.Tag_DEPRECATED.ToString(), *Anim.Identity.FlipbookName, *Row.TargetMove);
				Row.Tag_DEPRECATED = NAME_None;
				++Changes;
			}
			if (!Row.CancelCategory_DEPRECATED.IsNone())
			{
				UE_LOG(LogTemp, Log,
					TEXT("Paper2DPlus: %s: dropped transition cancel category '%s' on %s -> %s (concept re-homed as dormant Combat Profile foundation data, TASK-108)."),
					*GetName(), *Row.CancelCategory_DEPRECATED.ToString(), *Anim.Identity.FlipbookName, *Row.TargetMove);
				Row.CancelCategory_DEPRECATED = NAME_None;
				++Changes;
			}
			if (Row.Condition_DEPRECATED != EPaper2DPlusTransitionCondition::Always)
			{
				UE_LOG(LogTemp, Log,
					TEXT("Paper2DPlus: %s: dropped transition condition '%s' on %s -> %s (concept re-homed as dormant Combat Profile foundation data, TASK-108). NOTE: this row now counts as a combo-chain edge — every non-empty-target row is a chain link, so derived chains/roots/phases can shift."),
					*GetName(),
					*StaticEnum<EPaper2DPlusTransitionCondition>()->GetNameStringByValue(static_cast<int64>(Row.Condition_DEPRECATED)),
					*Anim.Identity.FlipbookName, *Row.TargetMove);
				Row.Condition_DEPRECATED = EPaper2DPlusTransitionCondition::Always;
				++Changes;
			}
		}
	}
	return Changes;
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
		for (int32 Index = Mapping.Entries.Num() - 1; Index >= 0; --Index)
		{
			if (!IsLive(Mapping.Entries[Index].FlipbookName))
			{
				Mapping.Entries.RemoveAt(Index);
			}
		}
	}
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

	// v6 → v7: Phase ranges removed from flipbooks (historically replaced by asset-level phase
	// groups, themselves retired in the 2026-07 legacy cleanup — legacy "PhaseGroups" JSON keys
	// are simply dropped by struct conversion now). Old phase ranges are dropped.
	if (InOutPayload.SchemaVersion == 6)
	{
		InOutPayload.SchemaVersion = 7;
	}

	// v7 -> v8: Animation Map roots are stable positive Root Numbers on exact tag-mapping entries.
	// ImportFromJsonString translates any deleted global root flags before this schema stamp. Unflagged
	// rows remain explicitly unnumbered; topology is never used to invent identity.
	if (InOutPayload.SchemaVersion == 7)
	{
		InOutPayload.SchemaVersion = 8;
	}

	return InOutPayload.SchemaVersion == CharacterProfileJsonSchemaVersion;
}

bool UPaper2DPlusCharacterProfileAsset::ExportToJsonString(FString& OutJson) const
{
	FCharacterProfileAssetSerializablePayload Payload;
	Payload.SchemaVersion = CharacterProfileJsonSchemaVersion;
	Payload.DisplayName = DisplayName;
	Payload.Flipbooks = Flipbooks;
#if WITH_EDITORONLY_DATA
	for (FFlipbookProfileEntry& Entry : Payload.Flipbooks)
	{
		Entry.FrameEventData.CueTrackLayout = FPaper2DPlusFrameCueTrackLayout();
	}
#endif
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

	const bool bConverted = FJsonObjectConverter::UStructToJsonObjectString(
		Payload,
		OutJson,
		0,
		CPF_EditorOnly | CPF_Deprecated,
		0,
		nullptr,
		false // pretty-print disabled for deterministic compact output
	);

	return bConverted;
}

bool UPaper2DPlusCharacterProfileAsset::ImportFromJsonString(const FString& JsonString)
{
	TArray<FPaper2DPlusCharacterProfileJsonImportWarning> Warnings;
	if (!ImportFromJsonStringInternal(JsonString, &Warnings))
	{
		return false;
	}
	for (const FPaper2DPlusCharacterProfileJsonImportWarning& Warning : Warnings)
	{
		UE_LOG(LogTemp, Warning, TEXT("[%s] %s"), *Warning.Code.ToString(), *Warning.Message);
	}
	return true;
}

bool UPaper2DPlusCharacterProfileAsset::ImportFromJsonStringWithWarnings(
	const FString& JsonString,
	TArray<FPaper2DPlusCharacterProfileJsonImportWarning>& OutWarnings)
{
	OutWarnings.Reset();
	return ImportFromJsonStringInternal(JsonString, &OutWarnings);
}

// TASK-177: FJsonObjectConverter materializes Instanced sub-objects with the TRANSIENT package as
// Outer whenever the destination container is a raw USTRUCT (FCharacterProfileAssetSerializablePayload
// has no owning UObject to inherit), so every imported Cue placement — and every save-preserving
// legacy Frame Event shell — arrives outered to /Engine/Transient. Authoring creates placements as
// NewObject(Asset, ..., RF_Transactional) and FFrameCueTrackLayoutDiagnostics asserts
// GetOuter() == the profile asset (WrongCueOuter); a transient-outered instanced object also fails to
// save/cook once its transient outer is GC'd. Adopt each imported sub-object under the asset with the
// authoring flags. Rename with a null name auto-generates a unique name under the new Outer, so it
// can never collide with the replaced pre-import placements still outered to the asset.
static void Paper2DPlusProfileJson_AdoptImportedSubobject(UObject* Subobject, UObject* Asset)
{
	if (!Subobject)
	{
		return;
	}
	if (Subobject->GetOuter() != Asset)
	{
		Subobject->Rename(nullptr, Asset,
			REN_DontCreateRedirectors | REN_NonTransactional | REN_DoNotDirty);
	}
	Subobject->SetFlags(RF_Transactional);
}

static void Paper2DPlusProfileJson_AdoptImportedInstancedObjects(
	UPaper2DPlusCharacterProfileAsset& Asset)
{
	for (FFlipbookProfileEntry& Anim : Asset.Flipbooks)
	{
		for (const TObjectPtr<UPaper2DPlusCueBase>& Cue : Anim.FrameEventData.FrameCues)
		{
			Paper2DPlusProfileJson_AdoptImportedSubobject(Cue, &Asset);
		}
		for (const TObjectPtr<UPaper2DPlusFrameEventBase>& Event : Anim.FrameEventData.FrameEvents)
		{
			Paper2DPlusProfileJson_AdoptImportedSubobject(Event, &Asset);
		}
		for (FExcludedFlipbookFrameData& Excluded : Anim.CombatData.ExcludedFrames)
		{
			for (const TObjectPtr<UPaper2DPlusCueBase>& Cue : Excluded.StashedFrameCues)
			{
				Paper2DPlusProfileJson_AdoptImportedSubobject(Cue, &Asset);
			}
			for (const TObjectPtr<UPaper2DPlusFrameEventBase>& Event : Excluded.StashedFrameEvents)
			{
				Paper2DPlusProfileJson_AdoptImportedSubobject(Event, &Asset);
			}
		}
	}
}

bool UPaper2DPlusCharacterProfileAsset::ImportFromJsonStringInternal(
	const FString& JsonString,
	TArray<FPaper2DPlusCharacterProfileJsonImportWarning>* OutWarnings)
{
	// Parse to a JSON object first so legacy key aliases can be rewritten before struct conversion
	// (FJsonObjectConverter silently drops keys with no matching property — see ApplyLegacyJsonAliases).
	TSharedPtr<FJsonObject> Root;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		return false;
	}
	ApplyLegacyJsonAliases(Root.ToSharedRef());

	FCharacterProfileAssetSerializablePayload Payload;
	if (!FJsonObjectConverter::JsonObjectToUStruct<FCharacterProfileAssetSerializablePayload>(
		Root.ToSharedRef(), &Payload, 0, CPF_EditorOnly))
	{
		return false;
	}

	if (Payload.SchemaVersion > CharacterProfileJsonSchemaVersion)
	{
		return false;
	}
	if (Payload.SchemaVersion == 7)
	{
		TSet<FString> LegacyRootNamesLower;
		ExtractSchemaSevenLegacyRootNames(JsonString, LegacyRootNamesLower);
		MigrateSchemaSevenLegacyRootNumbers(Payload, LegacyRootNamesLower);
	}

	if (!MigrateSerializablePayloadToCurrentSchema(Payload))
	{
		return false;
	}

#if WITH_EDITORONLY_DATA
	// Track organization is intentionally outside the deterministic Character Profile JSON contract.
	// Even a hand-authored/imported editor-only field lands every imported placement on Default.
	for (FFlipbookProfileEntry& Entry : Payload.Flipbooks)
	{
		Entry.FrameEventData.CueTrackLayout = FPaper2DPlusFrameCueTrackLayout();
	}
#endif
	if (OutWarnings)
	{
		FPaper2DPlusCharacterProfileJsonImportWarning& Warning = OutWarnings->AddDefaulted_GetRef();
		Warning.Code = Paper2DPlusCharacterProfileJson::GetTrackLayoutResetWarningCode();
		Warning.Message = TEXT("Character Profile JSON carries gameplay/profile semantics only. Named timeline track names, order, and Cue membership are not imported; imported Cues use Default.");
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

	// Mirror PostLoad migrations so imports reach the same end state. MigrateLoadedFlipbookSubStructs
	// moves legacy flat entry fields (populated from bare legacy JSON keys via their *_DEPRECATED
	// members) into the Identity/CombatData/MotionData/EditorMeta sub-structs.
	MigrateLoadedFlipbookSubStructs();
	MigrateTagMappingsToEntries();
	MigrateRootNumbersToChainStarts(); // legacy numbered roots → chain-start flags
	MigrateMoveTransitions(); // TASK-108 U1: dedupe rows + drop legacy Tag/CancelCategory/Condition keys
	PruneOrphanedTagMappings();
	NormalizeTagMappingsToOneFlipbookHome();
	MigrateLegacyGrouping(); // legacy-cleanup 2026-07: drop phase-group rows + stale tag-backed group shells

	// TASK-177: re-outer every imported Instanced sub-object (Cue placements, stashed cues, legacy
	// Frame Event shells) from the transient package to this asset with RF_Transactional. Runs AFTER
	// the migrations above so sub-objects that arrived through legacy/deprecated fields (e.g. an
	// entry-level "ExcludedFrames" key folded forward by MigrateLoadedFlipbookSubStructs) are covered.
	Paper2DPlusProfileJson_AdoptImportedInstancedObjects(*this);

	bFlipbookLookupCacheValid = false;
	bNameLookupCacheValid = false;

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

bool UPaper2DPlusCharacterProfileAsset::ImportFromJsonFileWithWarnings(
	const FString& FilePath,
	TArray<FPaper2DPlusCharacterProfileJsonImportWarning>& OutWarnings)
{
	OutWarnings.Reset();
	if (FilePath.IsEmpty())
	{
		return false;
	}

	FString JsonString;
	if (!FFileHelper::LoadFileToString(JsonString, *FilePath))
	{
		return false;
	}

	return ImportFromJsonStringWithWarnings(JsonString, OutWarnings);
}


// ==========================================
// LEGACY GROUPING MIGRATION (legacy-cleanup 2026-07)
// ==========================================

void UPaper2DPlusCharacterProfileAsset::MigrateLegacyGrouping()
{
	TArray<FName> RemovedGroupNames;

	// 1) Phase groups are RETIRED: drop every visual group row flagged with the legacy phase-group
	//    bit (old assets/JSON land it in bIsPhaseGroup_DEPRECATED). The FPhaseGroup slot payload is
	//    dropped by tagged-property serialization — the property no longer exists.
	for (int32 Index = FlipbookGroups.Num() - 1; Index >= 0; --Index)
	{
		if (FlipbookGroups[Index].bIsPhaseGroup_DEPRECATED)
		{
			RemovedGroupNames.Add(FlipbookGroups[Index].GroupName);
			FlipbookGroups.RemoveAt(Index);
		}
	}

	// 2) Stale TAG-BACKED rows: a visual group auto-created for a TagMappings key whose mapping is
	//    now gone or empty (the pre-cleanup RemoveTagMapping deleted the key but left the row).
	//    A row is tag-backed exactly when its name resolves in the live tag tree; manual groups
	//    (arbitrary names) never resolve and are untouched.
	for (int32 Index = FlipbookGroups.Num() - 1; Index >= 0; --Index)
	{
		const FName GroupName = FlipbookGroups[Index].GroupName;
		if (GroupName.IsNone())
		{
			continue;
		}
		const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(GroupName, /*ErrorIfNotFound=*/false);
		if (!Tag.IsValid())
		{
			continue;
		}

		bool bHasLiveEntry = false;
		if (const FFlipbookTagMapping* Mapping = TagMappings.Find(Tag))
		{
			for (const FFlipbookTagMappingEntry& Entry : Mapping->Entries)
			{
				if (!Entry.FlipbookName.TrimStartAndEnd().IsEmpty())
				{
					bHasLiveEntry = true;
					break;
				}
			}
		}
		if (!bHasLiveEntry)
		{
			RemovedGroupNames.Add(GroupName);
			FlipbookGroups.RemoveAt(Index);
		}
	}

	if (RemovedGroupNames.Num() == 0)
	{
		return;
	}

	// Orphaned cards fall to Ungrouped/Unassigned; child groups reparent to root.
	int32 ReassignedCards = 0;
	for (FFlipbookProfileEntry& Anim : Flipbooks)
	{
		if (!Anim.FlipbookGroup.IsNone() && RemovedGroupNames.Contains(Anim.FlipbookGroup))
		{
			Anim.FlipbookGroup = NAME_None;
			++ReassignedCards;
		}
	}
	for (FFlipbookGroupInfo& Group : FlipbookGroups)
	{
		if (!Group.ParentGroup.IsNone() && RemovedGroupNames.Contains(Group.ParentGroup))
		{
			Group.ParentGroup = NAME_None;
		}
	}

	UE_LOG(LogTemp, Log,
		TEXT("Paper2DPlus: %s — removed %d legacy group row(s) (retired phase groups / stale tag-mapping groups); %d card(s) moved to Unassigned."),
		*GetName(), RemovedGroupNames.Num(), ReassignedCards);
}

// ==========================================
// PAPERZD AUTO-RESOLVE
// ==========================================

UObject* UPaper2DPlusCharacterProfileAsset::FindPaperZDSequenceForFlipbook(UPaperFlipbook* Flipbook) const
{
	if (!Flipbook || PaperZDAnimSource.IsNull()) return nullptr;

	UObject* AnimSource = PaperZDAnimSource.LoadSynchronous();
	if (!AnimSource) return nullptr;

	// Find the optional PaperZD sequence class by its fully qualified reflected path. Short-name lookups
	// emit a warning (and a full call stack) on current engines and can become ambiguous across modules.
	static const TCHAR* PaperZDSequenceClassPath = TEXT("/Script/PaperZD.PaperZDAnimSequence_Flipbook");
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 1)
	UClass* SeqClass = UClass::TryFindTypeSlow<UClass>(PaperZDSequenceClassPath);
#else
	UClass* SeqClass = FindObject<UClass>(nullptr, PaperZDSequenceClassPath);
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
			return Seq;
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

		// Frame Cues are the authoritative authoring/runtime model. Validate their complete placement
		// contract here (not only in the editor migration report) so Blueprint validation and cooked-data
		// checks cannot silently accept a malformed cue array.
		for (int32 CueIndex = 0; CueIndex < Anim.FrameEventData.FrameCues.Num(); ++CueIndex)
		{
			const UPaper2DPlusCueBase* Cue = Anim.FrameEventData.FrameCues[CueIndex];
			const FString CueLabel = FString::Printf(TEXT("%s FrameCue[%d]"), *AnimLabel, CueIndex);

			// An unresolvable placement carries no payload and is skipped by dispatch, so it gets one
			// attributable error naming the profile and animation instead of the generic structural
			// diagnostics its empty state would otherwise produce. IsPlacementResolvable fails on two
			// distinct shapes and they need distinct messages: a null slot, and a live object whose
			// class is gone or reinstanced.
			//
			// The null arm genuinely CANNOT tell "never assigned" from "Cue Type deleted": when a
			// class is absent from the binary the linker nulls the export outright, so the loaded
			// slot is byte-for-byte a never-assigned slot. Do not "fix" this by guessing one cause —
			// there is no surviving evidence here to distinguish them.
			if (!IsValid(Cue))
			{
				AddIssue(ECharacterProfileValidationSeverity::Error, CueLabel,
					FString::Printf(
						TEXT("Character Profile '%s' animation '%s' has an empty Frame Cue placement: the slot holds no Cue, so nothing fires. Either it was never assigned, or its Cue Type was deleted from the project — a deleted Cue Type's placements load as empty. Delete the placement in the Frame Cues tab, or assign a Cue Type."),
						*GetName(),
						*Anim.Identity.FlipbookName));
				continue;
			}
			if (!Paper2DPlusFrameCueBehavior::IsPlacementResolvable(Cue))
			{
				AddIssue(ECharacterProfileValidationSeverity::Error, CueLabel,
					FString::Printf(
						TEXT("Character Profile '%s' animation '%s' has an orphaned Frame Cue placement: its Cue Type class no longer resolves (the Cue Type was deleted, or is a stale reinstanced version), so the cue never fires. Delete the placement in the Frame Cues tab, or restore the Cue Type asset."),
						*GetName(),
						*Anim.Identity.FlipbookName));
				continue;
			}

			TArray<FPaper2DPlusFrameCueValidationIssue> CueIssues;
			Paper2DPlusFrameCueValidation::ValidateCue(Cue, FrameCount, CueIssues);
			for (const FPaper2DPlusFrameCueValidationIssue& CueIssue : CueIssues)
			{
				AddIssue(
					CueIssue.Severity == EPaper2DPlusFrameCueValidationSeverity::Error
						? ECharacterProfileValidationSeverity::Error
						: ECharacterProfileValidationSeverity::Warning,
					CueLabel,
					CueIssue.Message);
			}
		}
	}

	// (Phase-group validation is GONE with the feature — legacy-cleanup 2026-07.)

	// Validate Animation Groups and their retained TagMappings backing data.
	TMap<FString, TSet<FString>> MappingGroupsByFlipbook;
	TMap<FString, FString> MappingDisplayNameByLower;
	for (const TPair<FGameplayTag, FFlipbookTagMapping>& Pair : TagMappings)
	{
		const FString GroupName = Pair.Key.IsValid() ? Pair.Key.ToString() : FString(TEXT("<invalid>"));
		for (const FFlipbookTagMappingEntry& Entry : Pair.Value.Entries)
		{
			if (Entry.FlipbookName.TrimStartAndEnd().IsEmpty())
			{
				continue;
			}
			const FString NameLower = Entry.FlipbookName.ToLower();
			MappingGroupsByFlipbook.FindOrAdd(NameLower).Add(GroupName);
			FString& MappedDisplayName = MappingDisplayNameByLower.FindOrAdd(NameLower);
			if (MappedDisplayName.IsEmpty()
				|| Entry.FlipbookName.Compare(MappedDisplayName, ESearchCase::CaseSensitive) < 0)
			{
				MappedDisplayName = Entry.FlipbookName;
			}
		}
	}

	TArray<FString> MappedNamesLower;
	MappingGroupsByFlipbook.GetKeys(MappedNamesLower);
	MappedNamesLower.Sort();
	for (const FString& NameLower : MappedNamesLower)
	{
		const TSet<FString>& GroupSet = MappingGroupsByFlipbook.FindChecked(NameLower);
		if (GroupSet.Num() <= 1)
		{
			continue;
		}

		TArray<FString> GroupNames;
		GroupNames.Reserve(GroupSet.Num());
		for (const FString& GroupName : GroupSet)
		{
			GroupNames.Add(GroupName);
		}
		GroupNames.Sort();
		AddIssue(ECharacterProfileValidationSeverity::Error, TEXT("Animation Groups"),
			FString::Printf(TEXT("Flipbook '%s' is mapped to more than one exact group (%s). Each animation must have one group home."),
				*MappingDisplayNameByLower.FindChecked(NameLower),
				*FString::Join(GroupNames, TEXT(", "))));
	}

	for (const auto& Pair : TagMappings)
	{
		const FString TagLabel = FString::Printf(TEXT("Animation Group '%s'"), *Pair.Key.ToString());
		TMap<FString, int32> FirstEntryByName;

		if (!Pair.Key.IsValid())
		{
			AddIssue(ECharacterProfileValidationSeverity::Warning, TagLabel,
				TEXT("The group tag is empty or invalid. Open Animations > Map, then use Map > Recovery to move its entries to an unused registered tag or remove the empty group."));
		}

		for (int32 i = 0; i < Pair.Value.Entries.Num(); ++i)
		{
			const FFlipbookTagMappingEntry& MappingEntry = Pair.Value.Entries[i];
			const FString& AnimName = MappingEntry.FlipbookName;
			if (!AnimName.TrimStartAndEnd().IsEmpty())
			{
				const FString NameLower = AnimName.ToLower();
				if (const int32* FirstIndex = FirstEntryByName.Find(NameLower))
				{
					AddIssue(ECharacterProfileValidationSeverity::Error, TagLabel,
						FString::Printf(TEXT("Flipbook '%s' appears more than once in this exact group (entries %d and %d). Duplicate membership is ambiguous and is excluded from group/combo-chain resolution."),
							*AnimName, *FirstIndex + 1, i + 1));
				}
				else
				{
					FirstEntryByName.Add(NameLower, i);
				}
			}
			if (!AnimName.IsEmpty() && !FlipbookNames.Contains(AnimName.ToLower()))
			{
				AddIssue(ECharacterProfileValidationSeverity::Warning, TagLabel,
					FString::Printf(TEXT("Flipbook '%s' is not found in this asset. Remove it from the Animation Group or add the flipbook to the asset."), *AnimName));
			}

			if (!MappingEntry.bIsChainStart)
			{
				continue;
			}

			const FFlipbookProfileEntry* RootEntry = AnimName.IsEmpty() ? nullptr : FindFlipbookDataPtr(AnimName);
			if (!RootEntry || RootEntry->Identity.Flipbook.IsNull()
				|| RootEntry->Identity.Flipbook.LoadSynchronous() == nullptr)
			{
				AddIssue(ECharacterProfileValidationSeverity::Error, TagLabel,
					FString::Printf(TEXT("Chain Start '%s' must identify a live flipbook in this exact group; the entry cannot resolve."),
						*AnimName));
			}
		}
	}

	// Exact own Animation Tags are the primary single-animation lookup identity. A duplicate set is
	// ambiguous regardless of authored order. The sole authoring exception is when every duplicate is
	// contained by one valid chain-start chain; that chain has its own explicit combo route and
	// exact-tag lookup deliberately remains Ambiguous.
	{
		TSet<const FFlipbookProfileEntry*> ProcessedExactTagEntries;
		for (const FFlipbookProfileEntry& Anim : Flipbooks)
		{
			if (ProcessedExactTagEntries.Contains(&Anim)
				|| Anim.EditorMeta.AnimationTags.IsEmpty()
				|| Anim.Identity.FlipbookName.TrimStartAndEnd().IsEmpty()
				|| Anim.Identity.Flipbook.IsNull())
			{
				continue;
			}

			const TArray<const FFlipbookProfileEntry*> Matches =
				Paper2DPlusAnimationTagQuery::FindExactOwnTagMatches(
					this, Anim.EditorMeta.AnimationTags);
			for (const FFlipbookProfileEntry* Match : Matches)
			{
				ProcessedExactTagEntries.Add(Match);
			}
			if (Matches.Num() <= 1
				|| Paper2DPlusAnimationTagQuery::AreMatchesContainedByOneRootChain(
					this, Matches))
			{
				continue;
			}

			TArray<FString> MatchNames;
			for (const FFlipbookProfileEntry* Match : Matches)
			{
				MatchNames.Add(Match->Identity.FlipbookName);
			}
			MatchNames.Sort();

			TArray<FString> TagNames;
			for (const FGameplayTag& Tag : Anim.EditorMeta.AnimationTags)
			{
				TagNames.Add(Tag.ToString());
			}
			TagNames.Sort();

			AddIssue(ECharacterProfileValidationSeverity::Error, TEXT("Animation Tags"),
				FString::Printf(TEXT("The exact Animation Tags container {%s} is authored by multiple standalone animations (%s). Make the complete tag set unique, or keep every duplicate inside one valid numbered-root chain and use Group/Root lookup."),
					*FString::Join(TagNames, TEXT(", ")),
					*FString::Join(MatchNames, TEXT(", "))));
		}
	}

	// Validate the Context animation-tag dimension (TASK-108 U5). Context is the EXCLUSIVE dimension
	// (Combat is intentionally non-exclusive — a move can be both Heavy and Combo). A "sub-dimension"
	// is a DIRECT child of Paper2DPlus.Animation.Context (Airborne, Crouching, ...); parent/child tags
	// within ONE sub-dimension are SPECIALIZATION, not conflict (Context.Airborne + Context.Airborne.Rising
	// is fine — only tags on DIFFERENT branches directly under Context conflict). Two checks, both on
	// EXPLICIT (authored) tags only:
	//  (a) one animation carrying explicit Context tags from two different sub-dimensions;
	//  (b) a mid-chain animation's explicit Context sub-dimension conflicting with a reaching chain
	//      root's explicit Context sub-dimension.
	// Inherited-vs-inherited unions are silently ALLOWED — a shared finisher legitimately answers
	// multiple contexts.
	{
		const FGameplayTag ContextRoot = Paper2DPlusAnimationTags::Context;

		// The direct-child-of-Context ancestor of InTag (the sub-dimension branch); invalid when InTag
		// is not strictly below Context (a bare Context tag names no branch and never conflicts).
		auto ResolveContextBranch = [&ContextRoot](const FGameplayTag& InTag) -> FGameplayTag
		{
			if (!ContextRoot.IsValid() || !InTag.IsValid() || InTag == ContextRoot || !InTag.MatchesTag(ContextRoot))
			{
				return FGameplayTag();
			}
			FGameplayTag Current = InTag;
			while (Current.IsValid())
			{
				const FGameplayTag Parent = Current.RequestDirectParent();
				if (Parent == ContextRoot)
				{
					return Current;
				}
				Current = Parent;
			}
			return FGameplayTag();
		};

		// The distinct sub-dimension branches named by a container's explicit tags.
		auto CollectContextBranches = [&ResolveContextBranch](const FGameplayTagContainer& InTags) -> TArray<FGameplayTag>
		{
			TArray<FGameplayTag> Branches;
			for (const FGameplayTag& InTag : InTags)
			{
				const FGameplayTag Branch = ResolveContextBranch(InTag);
				if (Branch.IsValid())
				{
					Branches.AddUnique(Branch);
				}
			}
			return Branches;
		};

		auto BranchNames = [](const TArray<FGameplayTag>& Branches) -> FString
		{
			TArray<FString> Names;
			for (const FGameplayTag& Branch : Branches)
			{
				Names.Add(Branch.ToString());
			}
			return FString::Join(Names, TEXT(", "));
		};

		if (ContextRoot.IsValid())
		{
			// (a) Two explicit Context sub-dimensions on one animation — one warning per animation.
			for (int32 AnimIndex = 0; AnimIndex < Flipbooks.Num(); ++AnimIndex)
			{
				const FFlipbookProfileEntry& Anim = Flipbooks[AnimIndex];
				const TArray<FGameplayTag> Branches = CollectContextBranches(Anim.EditorMeta.AnimationTags);
				if (Branches.Num() > 1)
				{
					AddIssue(ECharacterProfileValidationSeverity::Warning,
						FString::Printf(TEXT("Flipbook[%d] '%s'"), AnimIndex, *Anim.Identity.FlipbookName),
						FString::Printf(TEXT("Animation carries Context tags from %d different sub-dimensions (%s). Context is exclusive — keep one Context sub-dimension per animation (parent/child specialization within one branch is fine). Fix it in the Animations tab."),
							Branches.Num(), *BranchNames(Branches)));
				}
			}

			// (b) Mid-chain explicit Context vs a reaching root's explicit Context — one warning per
			// conflicting (animation, root) pair. Reaching roots come from the effective-tag batch
			// (the same visited-set traversal the queries use).
			const TMap<FString, Paper2DPlusAnimationTagQuery::FAnimationTagSet> TagMap =
				Paper2DPlusAnimationTagQuery::BuildAnimationTagMap(this);
			for (int32 AnimIndex = 0; AnimIndex < Flipbooks.Num(); ++AnimIndex)
			{
				const FFlipbookProfileEntry& Anim = Flipbooks[AnimIndex];
				const TArray<FGameplayTag> MemberBranches = CollectContextBranches(Anim.EditorMeta.AnimationTags);
				if (MemberBranches.Num() == 0)
				{
					continue; // no explicit Context on the member — inherited unions are allowed
				}
				const Paper2DPlusAnimationTagQuery::FAnimationTagSet* Set = TagMap.Find(Anim.Identity.FlipbookName.ToLower());
				if (!Set)
				{
					continue;
				}
				for (const FString& RootName : Set->ReachingRootNames)
				{
					const Paper2DPlusAnimationTagQuery::FAnimationTagSet* RootSet = TagMap.Find(RootName.ToLower());
					if (!RootSet)
					{
						continue;
					}
					const TArray<FGameplayTag> RootBranches = CollectContextBranches(RootSet->OwnTags);
					bool bConflicts = false;
					for (const FGameplayTag& MemberBranch : MemberBranches)
					{
						for (const FGameplayTag& RootBranch : RootBranches)
						{
							if (MemberBranch != RootBranch)
							{
								bConflicts = true;
							}
						}
					}
					if (bConflicts)
					{
						AddIssue(ECharacterProfileValidationSeverity::Warning,
							FString::Printf(TEXT("Flipbook[%d] '%s'"), AnimIndex, *Anim.Identity.FlipbookName),
							FString::Printf(TEXT("Explicit Context sub-dimension (%s) conflicts with reaching chain root '%s' (%s) — the chain would inherit a second Context sub-dimension. Retag one of them, or leave the member's Context implicit."),
								*BranchNames(MemberBranches), *RootName, *BranchNames(RootBranches)));
					}
				}
			}
		}
	}

	// Validate move transitions (TASK-76): dangling target names + cancel-window gating hints (PR2).
	// Empty targets are authoring-in-progress rows and are not flagged.
	const UPaper2DPlusSettings* CurveSettings = UPaper2DPlusSettings::Get();
	auto ResolveValidationCurveMetadata =
		[CurveSettings](FName CurveName, FPaper2DPlusKnownCurve& OutMetadata) -> bool
	{
		bool bFound = false;
		if (CurveSettings)
		{
			for (const FPaper2DPlusKnownCurve& Known : CurveSettings->KnownCurves)
			{
				if (!Known.Name.IsNone()
					&& Known.Name.ToString().Equals(CurveName.ToString(), ESearchCase::IgnoreCase))
				{
					OutMetadata = Known;
					OutMetadata.Name = CurveName;
					bFound = true;
					break;
				}
			}
		}

		if (FFlipbookTransitionData::IsCancelCurveName(CurveName))
		{
			// Cancel_* is a reserved authoring convention, not a current runtime gate. Keep custom names
			// consistently step-shaped even when they are absent from KnownCurves, so future game-owned
			// readers do not inherit interpolated half-values.
			OutMetadata.Name = CurveName;
			OutMetadata.Semantic = EPaper2DPlusKnownCurveSemantic::StepWindow;
			OutMetadata.DefaultMode = EPaper2DPlusCurveInterp::Constant;
			OutMetadata.ValueMin = 0.f;
			OutMetadata.ValueMax = 1.f;
			OutMetadata.bWarnWhenOutsideValueRange = true;
			return true;
		}

		return bFound;
	};

	for (int32 AnimIndex = 0; AnimIndex < Flipbooks.Num(); ++AnimIndex)
	{
		const FFlipbookProfileEntry& Anim = Flipbooks[AnimIndex];
		const FString AnimLabel = FString::Printf(TEXT("Flipbook[%d] '%s'"), AnimIndex, *Anim.Identity.FlipbookName);
		for (const FPaper2DPlusMoveTransition& Transition : Anim.TransitionData.Transitions)
		{
			if (!Transition.TargetMove.IsEmpty() && !FlipbookNames.Contains(Transition.TargetMove.ToLower()))
			{
				AddIssue(ECharacterProfileValidationSeverity::Warning, AnimLabel,
					FString::Printf(TEXT("Transition targets flipbook '%s' which is not found in this asset. Fix it in the Animations tab's Details panel or add the flipbook."),
						*Transition.TargetMove));
			}
			// (The per-row cancel-category fail-closed check died with the CancelCategory field,
			// TASK-108 — cancel windows are dormant Combat Profile foundation data now.)
		}

		// Reserved Cancel_* authoring uses 0/1 step windows. Paper2DPlus no longer owns a transition
		// driver or runtime reader; this only keeps the authored data unambiguous for game-owned use.
		for (const TPair<FName, FPaper2DPlusFrameCurve>& CurvePair : Anim.CurveData.Curves)
		{
			if (CurvePair.Key.ToString().StartsWith(TEXT("Cancel_"), ESearchCase::IgnoreCase)
				&& CurvePair.Value.Mode != EPaper2DPlusCurveInterp::Constant)
			{
				AddIssue(ECharacterProfileValidationSeverity::Info, AnimLabel,
					FString::Printf(TEXT("Curve '%s' uses the reserved Cancel_* convention but is not step-interpolated. Paper2DPlus does not read cancel curves at runtime; set Constant mode to keep the authored 0/1 window unambiguous for game-owned logic."),
						*CurvePair.Key.ToString()));
			}
		}

		// Semantic value-range hints (TASK-74.1): fixed-band curves like Cancel_* and Armor should stay
		// in their authored domain even if legacy data or external edits bypassed the row snap/range.
		for (const TPair<FName, FPaper2DPlusFrameCurve>& CurvePair : Anim.CurveData.Curves)
		{
			FPaper2DPlusKnownCurve Metadata;
			if (!ResolveValidationCurveMetadata(CurvePair.Key, Metadata)
				|| !Metadata.bWarnWhenOutsideValueRange
				|| Metadata.ValueMax <= Metadata.ValueMin)
			{
				continue;
			}

			for (const FRichCurveKey& Key : CurvePair.Value.Curve.GetConstRefOfKeys())
			{
				if (Key.Value < Metadata.ValueMin || Key.Value > Metadata.ValueMax)
				{
					const int32 Frame = FMath::RoundToInt(Key.Time);
					AddIssue(ECharacterProfileValidationSeverity::Warning, AnimLabel,
						FString::Printf(TEXT("Curve '%s' has value %.2f at frame %d outside its expected %.2f..%.2f range. Adjust it in the Frame Cues curve tracks."),
							*CurvePair.Key.ToString(), Key.Value, Frame, Metadata.ValueMin, Metadata.ValueMax));
				}
			}
		}

		// Long hit-stop authoring sanity: Paper2DPlus does not automatically consume this curve, but a
		// game that chooses the documented frame-count convention could turn 800 instead of 8 into a
		// multi-second freeze. Values above 60 are legal and remain Info-only confirmation hints.
		if (const FPaper2DPlusFrameCurve* HitStopCurve = Anim.CurveData.Curves.Find(FName(TEXT("HitStop"))))
		{
			float PeakFrames = 0.f;
			for (const FRichCurveKey& Key : HitStopCurve->Curve.GetConstRefOfKeys())
			{
				PeakFrames = FMath::Max(PeakFrames, Key.Value);
				const int32 Frame = FMath::RoundToInt(Key.Time);
				if (Key.Value < 0.f)
				{
					AddIssue(ECharacterProfileValidationSeverity::Warning, AnimLabel,
						FString::Printf(TEXT("HitStop curve has a negative value %.2f at frame %d. HitStop values are non-negative freeze-frame counts; set it to 0 or a positive whole-frame count in the Frame Cues curve tracks."),
							Key.Value, Frame));
				}
				const float RoundedFrames = static_cast<float>(FMath::RoundToInt(Key.Value));
				if (!FMath::IsNearlyEqual(Key.Value, RoundedFrames, 0.01f))
				{
					AddIssue(ECharacterProfileValidationSeverity::Info, AnimLabel,
						FString::Printf(TEXT("HitStop curve value %.2f at frame %d is fractional. HitStop is authored as whole freeze-frame counts; the Frame Cues row snaps new edits to integers."),
							Key.Value, Frame));
				}
			}
			if (PeakFrames > 60.f)
			{
				const UPaper2DPlusSettings* Settings = UPaper2DPlusSettings::Get();
				const float FramesPerSecond = FMath::Max(1.f, Settings ? Settings->HitStopFramesPerSecond : 60.f);
				AddIssue(ECharacterProfileValidationSeverity::Info, AnimLabel,
					FString::Printf(TEXT("HitStop curve peaks at %.0f frames (~%.1fs at %.0f fps) - confirm this is intentional."),
						PeakFrames, PeakFrames / FramesPerSecond, FramesPerSecond));
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

float UPaper2DPlusCharacterProfileAsset::GetAttackRangeForTag(FGameplayTag Tag) const
{
	float MaxRange = 0.0f;
	if (const FFlipbookTagMapping* Binding = TagMappings.Find(Tag))
	{
		for (const FFlipbookTagMappingEntry& Entry : Binding->Entries)
		{
			if (const FFlipbookProfileEntry* Anim = FindFlipbookDataPtr(Entry.FlipbookName))
			{
				MaxRange = FMath::Max(MaxRange, ComputeAttackRangeForAnimData(*Anim));
			}
		}
	}
	return MaxRange;
}

FBox2D UPaper2DPlusCharacterProfileAsset::GetAttackBoundsForTag(FGameplayTag Tag) const
{
	FBox2D Bounds(ForceInit);
	bool bHasAny = false;

	if (const FFlipbookTagMapping* Binding = TagMappings.Find(Tag))
	{
		for (const FFlipbookTagMappingEntry& Entry : Binding->Entries)
		{
			if (const FFlipbookProfileEntry* Anim = FindFlipbookDataPtr(Entry.FlipbookName))
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

// Object-ref forms of the attack-bounds accessors (the OBJECT-REFERENCE VARIANTS family) — placed here
// so they can reach the file-static Compute* helpers above.
float UPaper2DPlusCharacterProfileAsset::GetAttackRangeByFlipbook(UPaperFlipbook* Flipbook) const
{
	if (const FFlipbookProfileEntry* Anim = FindByFlipbookPtr(Flipbook))
	{
		return ComputeAttackRangeForAnimData(*Anim);
	}
	return 0.0f;
}

FBox2D UPaper2DPlusCharacterProfileAsset::GetAttackBoundsByFlipbook(UPaperFlipbook* Flipbook) const
{
	if (const FFlipbookProfileEntry* Anim = FindByFlipbookPtr(Flipbook))
	{
		return ComputeAttackBoundsForAnimData(*Anim);
	}
	return FBox2D(ForceInit);
}

// ==========================================
// TAG MAPPING HELPERS
// ==========================================

void UPaper2DPlusCharacterProfileAsset::UpdateTagMappingFlipbookName(const FString& OldName, const FString& NewName)
{
	for (auto& Pair : TagMappings)
	{
		for (FFlipbookTagMappingEntry& Entry : Pair.Value.Entries)
		{
			if (Entry.FlipbookName.Equals(OldName, ESearchCase::IgnoreCase))
			{
				Entry.FlipbookName = NewName;
			}
		}
	}
	NormalizeTagMappingsToOneFlipbookHome();
}

bool UPaper2DPlusCharacterProfileAsset::AssignFlipbookToTagMapping(FGameplayTag Tag, const FString& FlipbookName, UObject* PaperZDSequence)
{
	if (FlipbookName.IsEmpty())
	{
		return false;
	}

	FFlipbookTagMapping& TargetMapping = TagMappings.FindOrAdd(Tag);
	int32 TargetIndex = INDEX_NONE;
	for (int32 Index = 0; Index < TargetMapping.Entries.Num(); ++Index)
	{
		if (TargetMapping.Entries[Index].FlipbookName.Equals(FlipbookName, ESearchCase::IgnoreCase))
		{
			TargetIndex = Index;
			break;
		}
	}

	UObject* SequenceToUse = PaperZDSequence;
	if (!SequenceToUse)
	{
		SequenceToUse = FindTagMappingSequenceForFlipbook(TagMappings, FlipbookName, Tag, TargetIndex);
	}
	if (!SequenceToUse)
	{
		if (const FFlipbookProfileEntry* Entry = FindFlipbookDataPtr(FlipbookName))
		{
			SequenceToUse = Entry->Identity.PaperZDSequence.Get();
		}
	}

	bool bChanged = false;
	if (TargetIndex == INDEX_NONE)
	{
		TargetIndex = TargetMapping.Entries.Emplace(FlipbookName, SequenceToUse);
		bChanged = true;
	}
	else
	{
		FFlipbookTagMappingEntry& TargetEntry = TargetMapping.Entries[TargetIndex];
		if (!TargetEntry.FlipbookName.Equals(FlipbookName, ESearchCase::CaseSensitive))
		{
			TargetEntry.FlipbookName = FlipbookName;
			bChanged = true;
		}
		if (!TargetEntry.PaperZDSequence && SequenceToUse)
		{
			TargetEntry.PaperZDSequence = SequenceToUse;
			bChanged = true;
		}
	}

	const int32 Removed = RemoveDuplicateFlipbookTagMappings(FlipbookName, Tag, TargetIndex);
#if WITH_EDITOR
	bChanged |= SetFlipbookGroupToTagInternal(this, FlipbookName, Tag);
#endif
	return bChanged || Removed > 0;
}

bool UPaper2DPlusCharacterProfileAsset::SetTagMappingEntryFlipbook(FGameplayTag Tag, int32 EntryIndex, const FString& FlipbookName, UObject* PaperZDSequence)
{
	if (FlipbookName.IsEmpty())
	{
		return false;
	}

	FFlipbookTagMapping* TargetMapping = TagMappings.Find(Tag);
	if (!TargetMapping || !TargetMapping->Entries.IsValidIndex(EntryIndex))
	{
		return false;
	}

	UObject* SequenceToUse = PaperZDSequence;
	if (!SequenceToUse)
	{
		SequenceToUse = FindTagMappingSequenceForFlipbook(TagMappings, FlipbookName, Tag, EntryIndex);
	}
	if (!SequenceToUse)
	{
		if (const FFlipbookProfileEntry* Entry = FindFlipbookDataPtr(FlipbookName))
		{
			SequenceToUse = Entry->Identity.PaperZDSequence.Get();
		}
	}

	FFlipbookTagMappingEntry& TargetEntry = TargetMapping->Entries[EntryIndex];
	const FString PreviousFlipbookName = TargetEntry.FlipbookName;
	bool bChanged = false;
	if (!TargetEntry.FlipbookName.Equals(FlipbookName, ESearchCase::CaseSensitive))
	{
		TargetEntry.FlipbookName = FlipbookName;
		bChanged = true;
	}
	if (TargetEntry.PaperZDSequence.Get() != SequenceToUse)
	{
		TargetEntry.PaperZDSequence = SequenceToUse;
		bChanged = true;
	}

	const int32 Removed = RemoveDuplicateFlipbookTagMappings(FlipbookName, Tag, EntryIndex);
#if WITH_EDITOR
	bool bPreviousStillMappedToTag = false;
	if (!PreviousFlipbookName.IsEmpty() && !PreviousFlipbookName.Equals(FlipbookName, ESearchCase::IgnoreCase))
	{
		for (const FFlipbookTagMappingEntry& Entry : TargetMapping->Entries)
		{
			if (Entry.FlipbookName.Equals(PreviousFlipbookName, ESearchCase::IgnoreCase))
			{
				bPreviousStillMappedToTag = true;
				break;
			}
		}
		if (!bPreviousStillMappedToTag)
		{
			bChanged |= ClearFlipbookGroupIfTagInternal(this, PreviousFlipbookName, Tag);
		}
	}
	bChanged |= SetFlipbookGroupToTagInternal(this, FlipbookName, Tag);
#endif
	return bChanged || Removed > 0;
}

bool UPaper2DPlusCharacterProfileAsset::SetTagMappingEntryChainStart(
	FGameplayTag Tag,
	int32 EntryIndex,
	bool bInIsChainStart)
{
	FFlipbookTagMapping* Mapping = TagMappings.Find(Tag);
	if (!Mapping || !Mapping->Entries.IsValidIndex(EntryIndex))
	{
		return false;
	}

	FFlipbookTagMappingEntry& Entry = Mapping->Entries[EntryIndex];
	if (Entry.bIsChainStart == bInIsChainStart)
	{
		return false;
	}
	Entry.bIsChainStart = bInIsChainStart;
	return true;
}

bool UPaper2DPlusCharacterProfileAsset::SetTagMappingEntryChainEnd(
	FGameplayTag Tag,
	int32 EntryIndex,
	bool bInIsChainEnd)
{
	FFlipbookTagMapping* Mapping = TagMappings.Find(Tag);
	if (!Mapping || !Mapping->Entries.IsValidIndex(EntryIndex))
	{
		return false;
	}

	FFlipbookTagMappingEntry& Entry = Mapping->Entries[EntryIndex];
	if (Entry.bIsChainEnd == bInIsChainEnd)
	{
		return false;
	}
	Entry.bIsChainEnd = bInIsChainEnd;
	return true;
}

bool UPaper2DPlusCharacterProfileAsset::SetTagMappingEntryChainTags(
	FGameplayTag Tag,
	int32 EntryIndex,
	const FGameplayTagContainer& InChainTags)
{
	FFlipbookTagMapping* Mapping = TagMappings.Find(Tag);
	if (!Mapping || !Mapping->Entries.IsValidIndex(EntryIndex))
	{
		return false;
	}

	FFlipbookTagMappingEntry& Entry = Mapping->Entries[EntryIndex];
	if (Paper2DPlusAnimationTagQuery::AreExactTagSetsEqual(Entry.ChainTags, InChainTags))
	{
		return false;
	}
	Entry.ChainTags = InChainTags;
	return true;
}

int32 UPaper2DPlusCharacterProfileAsset::MigrateRootNumbersToChainStarts()
{
	int32 FoldedCount = 0;
	for (TPair<FGameplayTag, FFlipbookTagMapping>& Pair : TagMappings)
	{
		for (FFlipbookTagMappingEntry& Entry : Pair.Value.Entries)
		{
			if (Entry.RootNumber_DEPRECATED > 0)
			{
				Entry.bIsChainStart = true;
				++FoldedCount;
				UE_LOG(LogTemp, Log,
					TEXT("Paper2DPlus: migrated legacy Root Number %d on '%s' in group '%s' to a Chain Start flag."),
					Entry.RootNumber_DEPRECATED, *Entry.FlipbookName, *Pair.Key.ToString());
			}
			Entry.RootNumber_DEPRECATED = 0;
		}
	}
	return FoldedCount;
}

bool UPaper2DPlusCharacterProfileAsset::RemoveFlipbookFromTagMapping(FGameplayTag Tag, const FString& FlipbookName)
{
	if (FlipbookName.IsEmpty())
	{
		return false;
	}

	FFlipbookTagMapping* Mapping = TagMappings.Find(Tag);
	if (!Mapping)
	{
		return false;
	}

	bool bChanged = false;
	for (int32 Index = Mapping->Entries.Num() - 1; Index >= 0; --Index)
	{
		if (Mapping->Entries[Index].FlipbookName.Equals(FlipbookName, ESearchCase::IgnoreCase))
		{
			Mapping->Entries.RemoveAt(Index);
			bChanged = true;
		}
	}

#if WITH_EDITOR
	bChanged |= ClearFlipbookGroupIfTagInternal(this, FlipbookName, Tag);
	if (Mapping->Entries.IsEmpty())
	{
		// The last entry is gone — the mapping key stays (the Details card remains for re-adding),
		// but the tag-named visual group row must go like the whole-key RemoveTagMapping path, or
		// an empty group shell lingers in My Groups until the next load migration.
		bChanged |= RemoveFlipbookGroupForTagInternal(this, Tag);
	}
#endif

	return bChanged;
}

bool UPaper2DPlusCharacterProfileAsset::RenameTagMapping(FGameplayTag OldTag, FGameplayTag NewTag)
{
	if (OldTag == NewTag || TagMappings.Contains(NewTag))
	{
		return false;
	}

	FFlipbookTagMapping* OldMapping = TagMappings.Find(OldTag);
	if (!OldMapping)
	{
		return false;
	}

	FFlipbookTagMapping Copy = *OldMapping;
	TagMappings.Remove(OldTag);
	TagMappings.Add(NewTag, Copy);

#if WITH_EDITOR
	if (NewTag.IsValid())
	{
		EnsureFlipbookGroupForTagInternal(this, NewTag);
	}
	for (const FFlipbookTagMappingEntry& Entry : Copy.Entries)
	{
		ClearFlipbookGroupIfTagInternal(this, Entry.FlipbookName, OldTag);
		SetFlipbookGroupToTagInternal(this, Entry.FlipbookName, NewTag);
	}
	// The old key no longer maps anything — drop its tag-named visual group row (members moved
	// to the new tag's row above; legacy-cleanup 2026-07).
	RemoveFlipbookGroupForTagInternal(this, OldTag);
#endif

	return true;
}

bool UPaper2DPlusCharacterProfileAsset::RemoveTagMapping(FGameplayTag Tag)
{
	FFlipbookTagMapping Existing;
	if (!TagMappings.RemoveAndCopyValue(Tag, Existing))
	{
		return false;
	}

#if WITH_EDITOR
	for (const FFlipbookTagMappingEntry& Entry : Existing.Entries)
	{
		ClearFlipbookGroupIfTagInternal(this, Entry.FlipbookName, Tag);
	}
	// The mapping is gone — take its auto-created tag-named visual group row with it so no
	// empty group shell survives in the browser (legacy-cleanup 2026-07).
	RemoveFlipbookGroupForTagInternal(this, Tag);
#endif

	return true;
}

int32 UPaper2DPlusCharacterProfileAsset::RemoveDuplicateFlipbookTagMappings(const FString& FlipbookName, FGameplayTag TagToKeep, int32 EntryIndexToKeep)
{
	if (FlipbookName.IsEmpty())
	{
		return 0;
	}

	int32 Removed = 0;
	for (TPair<FGameplayTag, FFlipbookTagMapping>& Pair : TagMappings)
	{
		FFlipbookTagMapping& Mapping = Pair.Value;
		for (int32 Index = Mapping.Entries.Num() - 1; Index >= 0; --Index)
		{
			const bool bKeepEntry = Pair.Key == TagToKeep && Index == EntryIndexToKeep;
			if (!bKeepEntry && Mapping.Entries[Index].FlipbookName.Equals(FlipbookName, ESearchCase::IgnoreCase))
			{
				Mapping.Entries.RemoveAt(Index);
				++Removed;
			}
		}
	}

	return Removed;
}

int32 UPaper2DPlusCharacterProfileAsset::NormalizeTagMappingsToOneFlipbookHome()
{
	TArray<FGameplayTag> Tags;
	TagMappings.GetKeys(Tags);
	Tags.Sort([](const FGameplayTag& A, const FGameplayTag& B)
	{
		return A.ToString() < B.ToString();
	});

	TSet<FString> SeenNames;
	TMap<FGameplayTag, TArray<int32>> IndicesToRemoveByTag;
	for (const FGameplayTag& Tag : Tags)
	{
		FFlipbookTagMapping* Mapping = TagMappings.Find(Tag);
		if (!Mapping)
		{
			continue;
		}

		for (int32 Index = 0; Index < Mapping->Entries.Num(); ++Index)
		{
			const FString& Name = Mapping->Entries[Index].FlipbookName;
			if (Name.IsEmpty())
			{
				continue;
			}

			const FString LowerName = Name.ToLower();
			if (SeenNames.Contains(LowerName))
			{
				IndicesToRemoveByTag.FindOrAdd(Tag).Add(Index);
			}
			else
			{
				SeenNames.Add(LowerName);
			}
		}
	}

	int32 Removed = 0;
	for (const TPair<FGameplayTag, TArray<int32>>& Pair : IndicesToRemoveByTag)
	{
		FFlipbookTagMapping* Mapping = TagMappings.Find(Pair.Key);
		if (!Mapping)
		{
			continue;
		}

		TArray<int32> Indices = Pair.Value;
		Indices.Sort([](int32 A, int32 B) { return A > B; });
		for (int32 Index : Indices)
		{
			if (Mapping->Entries.IsValidIndex(Index))
			{
				Mapping->Entries.RemoveAt(Index);
				++Removed;
			}
		}
	}

	return Removed;
}

void UPaper2DPlusCharacterProfileAsset::RemoveFlipbookFromTagMappings(const FString& FlipbookName)
{
	TArray<FGameplayTag> RemovedTags;
	for (auto& Pair : TagMappings)
	{
		FFlipbookTagMapping& Mapping = Pair.Value;
		for (int32 Index = Mapping.Entries.Num() - 1; Index >= 0; --Index)
		{
			if (Mapping.Entries[Index].FlipbookName.Equals(FlipbookName, ESearchCase::IgnoreCase))
			{
				Mapping.Entries.RemoveAt(Index);
				RemovedTags.AddUnique(Pair.Key);
			}
		}
	}
#if WITH_EDITOR
	for (const FGameplayTag& RemovedTag : RemovedTags)
	{
		ClearFlipbookGroupIfTagInternal(this, FlipbookName, RemovedTag);
	}
#endif
}

void UPaper2DPlusCharacterProfileAsset::UpdateTransitionFlipbookName(const FString& OldName, const FString& NewName)
{
	for (FFlipbookProfileEntry& Anim : Flipbooks)
	{
		for (FPaper2DPlusMoveTransition& Transition : Anim.TransitionData.Transitions)
		{
			if (Transition.TargetMove.Equals(OldName, ESearchCase::IgnoreCase))
			{
				Transition.TargetMove = NewName;
			}
		}
	}

	// TASK-108 U1: a rename can re-create a duplicate (From, Target) pair (e.g. A->B + A->C with C
	// renamed to B) — re-enforce the one-row-per-pair invariant immediately, first-in-array wins.
	DedupeTransitionRows();
}

bool UPaper2DPlusCharacterProfileAsset::RenameFlipbookAndPropagate(int32 FlipbookIndex, const FString& NewName)
{
	if (!Flipbooks.IsValidIndex(FlipbookIndex))
	{
		return false;
	}

	const FString TrimmedName = NewName.TrimStartAndEnd();
	if (TrimmedName.IsEmpty())
	{
		return false;
	}

	const FString OldName = Flipbooks[FlipbookIndex].Identity.FlipbookName;
	if (TrimmedName.Equals(OldName, ESearchCase::CaseSensitive))
	{
		return false; // nothing changed (incl. trim/no-op commit)
	}

	// Reject a collision with a *different* flipbook (case-insensitive). A pure case change of this
	// same entry is allowed because FlipbookIndex is excluded from the scan.
	for (int32 OtherIndex = 0; OtherIndex < Flipbooks.Num(); ++OtherIndex)
	{
		if (OtherIndex != FlipbookIndex &&
			Flipbooks[OtherIndex].Identity.FlipbookName.Equals(TrimmedName, ESearchCase::IgnoreCase))
		{
			return false;
		}
	}

	Flipbooks[FlipbookIndex].Identity.FlipbookName = TrimmedName;

	// Propagate to every by-name reference on this asset — but ONLY when there was a prior name to
	// match on. An empty OldName is a supported (validation-warned-only) state for an unnamed entry,
	// and the empty string is also the sentinel for an "unassigned" tag/phase slot and for an
	// auto-pick ThumbnailFlipbookName; propagating "" would falsely claim all of those.
	// NOTE: cross-asset by-name references — UPaper2DPlusCharacterLayerAsset's
	// FCharacterLayerAnimationMapping.AnimationName — live in a separate asset reachable only via a
	// one-way soft pointer, so they are intentionally NOT updated here.
	if (!OldName.IsEmpty())
	{
		UpdateTagMappingFlipbookName(OldName, TrimmedName);

		UpdateTransitionFlipbookName(OldName, TrimmedName);
		if (ThumbnailFlipbookName.Equals(OldName, ESearchCase::IgnoreCase))
		{
			ThumbnailFlipbookName = TrimmedName;
		}

#if WITH_EDITORONLY_DATA
		// Animation Map node placements are keyed by LOWERCASED flipbook name — move the entry to the
		// new key. Renamed-move-wins must hold on BOTH paths: with a stored position the Add
		// overwrites any stale entry at the new key (left by a deleted move — stale keys are kept on
		// purpose); WITHOUT one the stale entry is removed, or the renamed move would silently
		// inherit the dead move's placement. No Modify() here — per this function's contract the
		// caller owns the transaction.
		FVector2D NodePosition;
		if (AnimationMapNodePositions.RemoveAndCopyValue(OldName.ToLower(), NodePosition))
		{
			AnimationMapNodePositions.Add(TrimmedName.ToLower(), NodePosition);
		}
		else
		{
			AnimationMapNodePositions.Remove(TrimmedName.ToLower());
		}
#endif
	}

	// A rename leaves Flipbooks.Num() unchanged, so BOTH name-keyed caches keep a stale OldName key
	// that no count check can catch. RebuildFlipbookLookupCache owns ExactAnimationNameToDataIndicesCache
	// (the Frame Cue anchor's exact-name route), so invalidating only the lowercase name cache leaves
	// exact-name lookups permanently missing the renamed row.
	bNameLookupCacheValid = false;
	bFlipbookLookupCacheValid = false;

	return true;
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
bool UPaper2DPlusCharacterProfileAsset::IsTagBackedFlipbookGroup(FName GroupName, FGameplayTag& OutTag) const
{
	return ResolveTagBackedFlipbookGroup(this, GroupName, OutTag);
}

bool UPaper2DPlusCharacterProfileAsset::EnsureFlipbookGroupForTag(FGameplayTag Tag)
{
	return EnsureFlipbookGroupForTagInternal(this, Tag);
}

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
	for (int32 FlipbookIndex = 0; FlipbookIndex < Flipbooks.Num(); ++FlipbookIndex)
	{
		if (Flipbooks[FlipbookIndex].FlipbookGroup == Name)
		{
			MoveFlipbookToFlipbookGroup(FlipbookIndex, NAME_None);
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
	FGameplayTag OldTag;
	const bool bOldWasTagBacked = ResolveTagBackedFlipbookGroup(this, OldName, OldTag);

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

	FGameplayTag NewTag;
	const bool bNewIsTagBacked = ResolveTagBackedFlipbookGroup(this, NewName, NewTag);
	for (const FFlipbookProfileEntry& Anim : Flipbooks)
	{
		if (Anim.FlipbookGroup != NewName)
		{
			continue;
		}
		if (bOldWasTagBacked)
		{
			RemoveFlipbookFromTagMapping(OldTag, Anim.Identity.FlipbookName);
		}
		if (bNewIsTagBacked)
		{
			AssignFlipbookToTagMapping(NewTag, Anim.Identity.FlipbookName, Anim.Identity.PaperZDSequence.Get());
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
	if (!Flipbooks.IsValidIndex(FlipbookIndex))
	{
		return;
	}

	const FString FlipbookName = Flipbooks[FlipbookIndex].Identity.FlipbookName;
	UObject* PaperZDSequence = Flipbooks[FlipbookIndex].Identity.PaperZDSequence.Get();

	FGameplayTag TargetTag;
	if (ResolveTagBackedFlipbookGroup(this, GroupName, TargetTag))
	{
		EnsureFlipbookGroupForTagInternal(this, TargetTag);
		Flipbooks[FlipbookIndex].FlipbookGroup = GroupName;
		AssignFlipbookToTagMapping(TargetTag, FlipbookName, PaperZDSequence);
		return;
	}

	RemoveFlipbookFromTagMappings(FlipbookName);
	Flipbooks[FlipbookIndex].FlipbookGroup = GroupName;
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
