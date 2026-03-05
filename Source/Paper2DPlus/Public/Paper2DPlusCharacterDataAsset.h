// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Paper2DPlusTypes.h"
#include "PaperFlipbook.h"
#include "Engine/Texture2D.h"
#include "GameplayTagContainer.h"
#include "Paper2DPlusCharacterDataAsset.generated.h"

/**
 * Visual group definition for editor organization of flipbook animations.
 * Groups form a tree via ParentGroup references (NAME_None = root level).
 * Group names are globally unique (case-insensitive, FName semantics).
 */
USTRUCT(BlueprintType)
struct PAPER2DPLUS_API FFlipbookGroupInfo
{
	GENERATED_BODY()

	/** Unique group identifier. Case-insensitive (FName). Globally unique across all nesting levels. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flipbook Groups")
	FName GroupName;

	/** Parent group name. NAME_None = root level. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flipbook Groups")
	FName ParentGroup;

	/** Visual tint color for the group header. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flipbook Groups")
	FLinearColor Color = FLinearColor(0.3f, 0.5f, 0.8f, 1.0f);
};

/**
 * Animation data with hitbox information and sprite extraction metadata
 */
USTRUCT(BlueprintType)
struct PAPER2DPLUS_API FFlipbookHitboxData
{
	GENERATED_BODY()

	// ==========================================
	// EXISTING FIELDS (for backward compatibility)
	// ==========================================

	/** Name of the flipbook entry */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flipbook")
	FString FlipbookName;

	/** Optional reference to the Paper2D Flipbook for this flipbook entry */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flipbook")
	TSoftObjectPtr<UPaperFlipbook> Flipbook;

	/** All frames in this flipbook with hitbox data */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flipbook")
	TArray<FFrameHitboxData> Frames;

	/** Source texture this animation was extracted from */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sprite Source")
	TSoftObjectPtr<UTexture2D> SourceTexture;

	/** Per-frame extraction metadata */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sprite Source")
	TArray<FSpriteExtractionInfo> FrameExtractionInfo;

	/** Output path where sprites for this animation are saved */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sprite Source")
	FString SpritesOutputPath;

	/** Visual group assignment for editor organization. Empty = Ungrouped. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flipbook")
	FName FlipbookGroup;

	// ==========================================
	// EXISTING HELPER FUNCTIONS
	// ==========================================

	/** Get frame data by index */
	const FFrameHitboxData* GetFrame(int32 Index) const
	{
		if (Frames.IsValidIndex(Index))
		{
			return &Frames[Index];
		}
		return nullptr;
	}

	/** Get frame data by name */
	const FFrameHitboxData* GetFrameByName(const FString& FrameName) const
	{
		for (const FFrameHitboxData& Frame : Frames)
		{
			if (Frame.FrameName.Equals(FrameName, ESearchCase::IgnoreCase))
			{
				return &Frame;
			}
		}
		return nullptr;
	}

	/** Get total frame count */
	int32 GetFrameCount() const
	{
		return Frames.Num();
	}

	/** Check if this animation has extraction info available */
	bool HasExtractionInfo() const
	{
		return !SourceTexture.IsNull() && FrameExtractionInfo.Num() > 0;
	}
};

/**
 * Mapping from a GameplayTag to one or more animation entries + metadata.
 * Tags reference existing animations by name (no data duplication).
 * Array order is significant for combo systems (index 0 = first, etc.).
 */
USTRUCT(BlueprintType)
struct PAPER2DPLUS_API FFlipbookTagMapping
{
	GENERATED_BODY()

	/** Flipbook names referencing Flipbooks[].FlipbookName entries. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tag Mappings")
	TArray<FString> FlipbookNames;

	/** PaperZD AnimSequence for this tag mapping (soft ref — no hard dependency on PaperZD). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tag Mappings")
	TSoftObjectPtr<UObject> PaperZDSequence;

	/** Arbitrary metadata assets keyed by name (e.g., "SoundCue", "Montage"). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tag Mappings")
	TMap<FName, TSoftObjectPtr<UObject>> Metadata;
};

/** Serializable key-value pair for tag mappings in JSON export. */
USTRUCT()
struct PAPER2DPLUS_API FSerializableTagMapping
{
	GENERATED_BODY()

	UPROPERTY()
	FString Tag;

	UPROPERTY()
	FFlipbookTagMapping Binding;
};

UENUM(BlueprintType)
enum class ECharacterDataValidationSeverity : uint8
{
	Info	UMETA(DisplayName = "Info"),
	Warning UMETA(DisplayName = "Warning"),
	Error	UMETA(DisplayName = "Error")
};


/** Internal serializable payload for JSON import/export. */
USTRUCT()
struct PAPER2DPLUS_API FCharacterDataAssetSerializablePayload
{
	GENERATED_BODY()

	UPROPERTY()
	int32 SchemaVersion = 1;

	UPROPERTY()
	FString DisplayName;

	UPROPERTY()
	TArray<FFlipbookHitboxData> Flipbooks;

	UPROPERTY()
	int32 DefaultAlphaThreshold = 10;

	UPROPERTY()
	int32 DefaultPadding = 0;

	UPROPERTY()
	int32 DefaultMinSpriteSize = 4;

	/** Tag mappings serialized as array of key-value pairs (avoids TMap<FGameplayTag> JSON issues). */
	UPROPERTY()
	TArray<FSerializableTagMapping> GroupBindings;

	/** Visual grouping definitions for editor organization. */
	UPROPERTY()
	TArray<FFlipbookGroupInfo> FlipbookGroups;
};

/** Validation issue generated by CharacterData asset validation. */
USTRUCT(BlueprintType)
struct PAPER2DPLUS_API FCharacterDataValidationIssue
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Validation")
	ECharacterDataValidationSeverity Severity = ECharacterDataValidationSeverity::Info;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Validation")
	FString Context;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Validation")
	FString Message;
};

/**
 * Character Data Asset containing all animation hitbox data.
 * Manages flipbooks, hitboxes, sockets, and extraction metadata across all character animations.
 */
UCLASS(BlueprintType, NotBlueprintable)
class PAPER2DPLUS_API UPaper2DPlusCharacterDataAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPaper2DPlusCharacterDataAsset();

	// ==========================================
	// EXISTING PROPERTIES (for backward compatibility)
	// ==========================================

	/** Display name for this character data (e.g., character name) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character Data")
	FString DisplayName;

	/** All animations with their hitbox data */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character Data")
	TArray<FFlipbookHitboxData> Flipbooks;

	/** Visual grouping definitions for the editor Overview tab. */
	UPROPERTY(EditAnywhere, Category = "Flipbook Groups")
	TArray<FFlipbookGroupInfo> FlipbookGroups;

	// ==========================================
	// FLIPBOOK TAG MAPPINGS
	// ==========================================

	/** Tag-to-animation mappings. Each GameplayTag maps to one or more animation entries + metadata. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tag Mappings",
		meta = (Categories = "Paper2DPlus.Animation"))
	TMap<FGameplayTag, FFlipbookTagMapping> TagMappings;

	/** Alpha threshold to use for sprite extraction */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Extraction Settings")
	int32 DefaultAlphaThreshold = 10;

	/** Padding to apply around detected sprites */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Extraction Settings")
	int32 DefaultPadding = 0;

	/** Minimum sprite size filter */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Extraction Settings")
	int32 DefaultMinSpriteSize = 4;

	// ==========================================
	// EXISTING LOOKUP FUNCTIONS
	// ==========================================

	/** Get all flipbook names */
	UFUNCTION(BlueprintPure, Category = "Character Data")
	TArray<FString> GetFlipbookNames() const;

	/** Get flipbook data by name */
	UFUNCTION(BlueprintCallable, Category = "Character Data")
	bool GetFlipbook(const FString& FlipbookName, FFlipbookHitboxData& OutFlipbook) const;

	/** Get flipbook data by index */
	UFUNCTION(BlueprintCallable, Category = "Character Data")
	bool GetFlipbookByIndex(int32 Index, FFlipbookHitboxData& OutFlipbook) const;

	/** Get frame count for a flipbook */
	UFUNCTION(BlueprintPure, Category = "Character Data")
	int32 GetFrameCount(const FString& FlipbookName) const;

	/** Get frame data by flipbook name and frame index */
	UFUNCTION(BlueprintCallable, Category = "Character Data")
	bool GetFrame(const FString& FlipbookName, int32 FrameIndex, FFrameHitboxData& OutFrame) const;

	/** Get frame data by flipbook name and frame name */
	UFUNCTION(BlueprintCallable, Category = "Character Data")
	bool GetFrameByName(const FString& FlipbookName, const FString& FrameName, FFrameHitboxData& OutFrame) const;

	/** Find flipbook data by Flipbook reference */
	UFUNCTION(BlueprintCallable, Category = "Character Data")
	bool FindByFlipbook(UPaperFlipbook* Flipbook, FFlipbookHitboxData& OutFlipbook) const;

	/** Fast lookup helper that avoids copying flipbook data. */
	const FFlipbookHitboxData* FindByFlipbookPtr(UPaperFlipbook* Flipbook) const;

	/** Get a const pointer to flipbook data by name (no copy). */
	const FFlipbookHitboxData* FindFlipbookDataPtr(const FString& FlipbookName) const;

	// ==========================================
	// DIRECT HITBOX ACCESS
	// ==========================================

	/** Get all hitboxes for a specific frame */
	UFUNCTION(BlueprintPure, Category = "Character Data")
	TArray<FHitboxData> GetHitboxes(const FString& FlipbookName, int32 FrameIndex) const;

	/** Get hitboxes of a specific type for a frame */
	UFUNCTION(BlueprintPure, Category = "Character Data")
	TArray<FHitboxData> GetHitboxesByType(const FString& FlipbookName, int32 FrameIndex, EHitboxType Type) const;

	/** Get all sockets for a specific frame */
	UFUNCTION(BlueprintPure, Category = "Character Data")
	TArray<FSocketData> GetSockets(const FString& FlipbookName, int32 FrameIndex) const;

	/** Find a specific socket by name */
	UFUNCTION(BlueprintCallable, Category = "Character Data")
	bool FindSocket(const FString& FlipbookName, int32 FrameIndex, const FString& SocketName, FSocketData& OutSocket) const;

	// ==========================================
	// ASSET INFO
	// ==========================================

	/** Get total number of animations */
	UFUNCTION(BlueprintPure, Category = "Character Data")
	int32 GetFlipbookCount() const { return Flipbooks.Num(); }

	/** Check if a specific flipbook exists */
	UFUNCTION(BlueprintPure, Category = "Character Data")
	bool HasFlipbook(const FString& FlipbookName) const;

	// ==========================================
	// TAG MAPPING LOOKUPS
	// ==========================================

	/** Get all flipbook data for a tag, in array order (for combo progression). */
	UFUNCTION(BlueprintPure, Category = "Tag Mappings", meta = (GameplayTagFilter = "Paper2DPlus.Animation"))
	TArray<FFlipbookHitboxData> GetFlipbookDataForTag(FGameplayTag Group) const;

	/** Get all loaded flipbooks for a tag. */
	UFUNCTION(BlueprintPure, Category = "Tag Mappings", meta = (GameplayTagFilter = "Paper2DPlus.Animation"))
	TArray<UPaperFlipbook*> GetFlipbooksForTag(FGameplayTag Group) const;

	/** Get the first flipbook for a tag, or nullptr if unmapped. */
	UFUNCTION(BlueprintPure, Category = "Tag Mappings", meta = (GameplayTagFilter = "Paper2DPlus.Animation"))
	UPaperFlipbook* GetFirstFlipbookForTag(FGameplayTag Group) const;

	/** Get a random flipbook for a tag (non-deterministic — not safe for networked use). */
	UFUNCTION(BlueprintPure, Category = "Tag Mappings", meta = (GameplayTagFilter = "Paper2DPlus.Animation"))
	UPaperFlipbook* GetRandomFlipbookForTag(FGameplayTag Group) const;

	/** Get the PaperZD AnimSequence for a tag. Calls LoadSynchronous — cache result in hot paths. */
	UFUNCTION(BlueprintPure, Category = "Tag Mappings", meta = (GameplayTagFilter = "Paper2DPlus.Animation"))
	UObject* GetPaperZDSequenceForTag(FGameplayTag Group) const;

	/** Get a metadata asset for a tag by key. Calls LoadSynchronous — cache result in hot paths. */
	UFUNCTION(BlueprintPure, Category = "Tag Mappings", meta = (GameplayTagFilter = "Paper2DPlus.Animation"))
	UObject* GetTagMappingMetadata(FGameplayTag Group, FName Key) const;

	/** Get all metadata keys for a tag. */
	UFUNCTION(BlueprintPure, Category = "Tag Mappings", meta = (GameplayTagFilter = "Paper2DPlus.Animation"))
	TArray<FName> GetTagMappingMetadataKeys(FGameplayTag Group) const;

	/** Check if a tag has a metadata entry for the given key. */
	UFUNCTION(BlueprintPure, Category = "Tag Mappings", meta = (GameplayTagFilter = "Paper2DPlus.Animation"))
	bool HasTagMappingMetadata(FGameplayTag Group, FName Key) const;

	/** Get the full tag mapping struct (animations + metadata) for a tag. Returns false if unmapped. */
	UFUNCTION(BlueprintPure, Category = "Tag Mappings", meta = (GameplayTagFilter = "Paper2DPlus.Animation"))
	bool GetTagMapping(FGameplayTag Group, FFlipbookTagMapping& OutBinding) const;

	/** Check if this asset has a mapping for the given tag. */
	UFUNCTION(BlueprintPure, Category = "Tag Mappings", meta = (GameplayTagFilter = "Paper2DPlus.Animation"))
	bool HasTagMapping(FGameplayTag Group) const;

	/** Get all tags that have been mapped in this asset. */
	UFUNCTION(BlueprintPure, Category = "Tag Mappings")
	TArray<FGameplayTag> GetAllMappedTags() const;

	/** Get the number of flipbooks mapped to a tag (useful for combo systems). */
	UFUNCTION(BlueprintPure, Category = "Tag Mappings", meta = (GameplayTagFilter = "Paper2DPlus.Animation"))
	int32 GetFlipbookCountForTag(FGameplayTag Group) const;

	// ==========================================
	// ATTACK BOUNDS (AI HELPERS)
	// ==========================================

	/** Get the max attack range across ALL animations (distance from origin to furthest attack hitbox edge). */
	UFUNCTION(BlueprintPure, Category = "Attack Bounds")
	float GetMaxAttackRange() const;

	/** Get the max attack range for a specific tag mapping. */
	UFUNCTION(BlueprintPure, Category = "Attack Bounds", meta = (GameplayTagFilter = "Paper2DPlus.Animation"))
	float GetAttackRangeForTag(FGameplayTag Group) const;

	/** Get the max attack range for a specific animation by name. */
	UFUNCTION(BlueprintPure, Category = "Attack Bounds")
	float GetAttackRangeForFlipbook(const FString& FlipbookName) const;

	/** Get the combined bounds (FBox2D) of all attack hitboxes across all frames of a tag. */
	UFUNCTION(BlueprintPure, Category = "Attack Bounds", meta = (GameplayTagFilter = "Paper2DPlus.Animation"))
	FBox2D GetAttackBoundsForTag(FGameplayTag Group) const;

	/** Get the combined bounds (FBox2D) of all attack hitboxes across all frames of an animation. */
	UFUNCTION(BlueprintPure, Category = "Attack Bounds")
	FBox2D GetAttackBoundsForFlipbook(const FString& FlipbookName) const;

	/** Validate the character data asset for common data issues.
	 *  @return true when no errors are found (warnings allowed). */
	UFUNCTION(BlueprintCallable, Category = "Validation")
	bool ValidateCharacterDataAsset(TArray<FCharacterDataValidationIssue>& OutIssues) const;

	/** Trim trailing frame/extraction metadata beyond the flipbook keyframe count for one animation.
	 *  @return Total entries removed from all arrays for this animation. */
	UFUNCTION(BlueprintCallable, Category = "Character Data")
	int32 TrimTrailingFrameData(int32 FlipbookIndex);

	/** Trim trailing frame/extraction metadata for all animations.
	 *  @return Total entries removed across all animations. */
	UFUNCTION(BlueprintCallable, Category = "Character Data")
	int32 TrimAllTrailingFrameData();

	/** Copy all hitboxes/sockets from SourceFrameIndex to an inclusive frame range in an animation.
	 *  @return true if operation succeeds. */
	UFUNCTION(BlueprintCallable, Category = "Character Data|Batch")
	bool CopyFrameDataToRange(const FString& FlipbookName, int32 SourceFrameIndex, int32 RangeStart, int32 RangeEnd, bool bIncludeSockets = true);

	/** Mirror hitboxes horizontally in an inclusive frame range using PivotX.
	 *  @return number of hitboxes mirrored. */
	UFUNCTION(BlueprintCallable, Category = "Character Data|Batch")
	int32 MirrorHitboxesInRange(const FString& FlipbookName, int32 RangeStart, int32 RangeEnd, int32 PivotX);

	/** Set sprite flip state in an inclusive frame range.
	 *  @return number of frames updated. */
	UFUNCTION(BlueprintCallable, Category = "Character Data|Batch")
	int32 SetSpriteFlipInRange(const FString& FlipbookName, int32 RangeStart, int32 RangeEnd, bool bInFlipX, bool bInFlipY);

	/** Set sprite flip state across every frame of a flipbook.
	 *  @return number of frames updated. */
	UFUNCTION(BlueprintCallable, Category = "Character Data|Batch")
	int32 SetSpriteFlipForFlipbook(const FString& FlipbookName, bool bInFlipX, bool bInFlipY);

	/** Set sprite flip state across all flipbooks in the asset.
	 *  @return number of frames updated. */
	UFUNCTION(BlueprintCallable, Category = "Character Data|Batch")
	int32 SetSpriteFlipForAllFlipbooks(bool bInFlipX, bool bInFlipY);

	/** Legacy CharacterData JSON schema version used before explicit schema stamping. */
	static constexpr int32 CharacterDataJsonLegacySchemaVersion = 0;

	/** Current CharacterData JSON schema version. */
	static constexpr int32 CharacterDataJsonSchemaVersion = 4;

	/** Get current CharacterData JSON schema version. */
	UFUNCTION(BlueprintPure, Category = "Character Data|Serialization")
	int32 GetCharacterDataJsonSchemaVersion() const { return CharacterDataJsonSchemaVersion; }

	/** Export CharacterData content to a deterministic JSON string. */
	UFUNCTION(BlueprintCallable, Category = "Character Data|Serialization")
	bool ExportToJsonString(FString& OutJson) const;

	/** Import CharacterData content from JSON string. */
	UFUNCTION(BlueprintCallable, Category = "Character Data|Serialization")
	bool ImportFromJsonString(const FString& JsonString);

	/** Export CharacterData to a JSON file. */
	UFUNCTION(BlueprintCallable, Category = "Character Data|Serialization")
	bool ExportToJsonFile(const FString& FilePath) const;

	/** Import CharacterData from a JSON file. */
	UFUNCTION(BlueprintCallable, Category = "Character Data|Serialization")
	bool ImportFromJsonFile(const FString& FilePath);

	/** Get asset primary ID for async loading */
	virtual FPrimaryAssetId GetPrimaryAssetId() const override;

	/** Sync hitbox Frames[] array to match the flipbook frame count for an animation.
	 *  Preserves existing data, appends empty FFrameHitboxData for new frames. */
	void SyncFramesToFlipbook(int32 FlipbookIndex);

	/** Sync all animations' frame arrays to their flipbooks */
	void SyncAllFramesToFlipbooks();

	/** Update animation name references in TagMappings when an animation is renamed. */
	void UpdateTagMappingFlipbookName(const FString& OldName, const FString& NewName);

	/** Remove a flipbook name from all TagMappings entries. */
	void RemoveFlipbookFromTagMappings(const FString& FlipbookName);

	// ==========================================
	// FLIPBOOK GROUP HELPERS
	// ==========================================

#if WITH_EDITOR
	/** Add a new visual group. Does NOT call Modify() — caller must manage transactions. */
	FFlipbookGroupInfo& AddFlipbookGroup(FName Name, FName Parent = NAME_None);

	/** Remove a visual group. Moves animations to Ungrouped, promotes child sub-groups to parent level.
	 *  Does NOT call Modify() — caller must manage transactions. */
	void RemoveFlipbookGroup(FName Name);

	/** Rename a visual group. Cascades to animations and child groups.
	 *  Does NOT call Modify() — caller must manage transactions. */
	void RenameFlipbookGroup(FName OldName, FName NewName);

	/** Set the color of a visual group. Does NOT call Modify(). */
	void SetFlipbookGroupColor(FName Name, FLinearColor Color);

	/** Move an animation to a visual group (NAME_None = Ungrouped). Does NOT call Modify(). */
	void MoveFlipbookToFlipbookGroup(int32 FlipbookIndex, FName GroupName);
#endif

	/** Check if a visual group with the given name exists (globally unique, case-insensitive). */
	bool HasFlipbookGroup(FName Name) const;

	/** Get the group tree as a map of parent -> children pointers. */
	TMap<FName, TArray<const FFlipbookGroupInfo*>> GetFlipbookGroupTree() const;

	/** Get animation indices that belong to a given group. */
	TArray<int32> GetFlipbookIndicesForFlipbookGroup(FName GroupName) const;

	/** Post-load hook for asset migration */
	virtual void PostLoad() override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

protected:
	/** Internal lookup - find flipbook data by name */
	const FFlipbookHitboxData* FindFlipbookData(const FString& FlipbookName) const;

private:
	/** Migrate imported JSON payload to the current schema version when possible. */
	static bool MigrateSerializablePayloadToCurrentSchema(FCharacterDataAssetSerializablePayload& InOutPayload);

	void RebuildFlipbookLookupCache() const;
	void RebuildNameLookupCache() const;

	/** Cached map to accelerate flipbook -> animation lookup in hot paths. */
	mutable TMap<TObjectPtr<UPaperFlipbook>, int32> FlipbookToDataIndexCache;

	/** Whether FlipbookToDataIndexCache is synchronized with Flipbooks. */
	mutable bool bFlipbookLookupCacheValid = false;

	/** Number of Flipbooks entries when the flipbook cache was last built. */
	mutable int32 CachedFlipbookCount = 0;

	/** Cached map to accelerate name -> flipbook data lookup. */
	mutable TMap<FString, int32> NameToFlipbookIndexCache;

	/** Whether NameToFlipbookIndexCache is synchronized with Flipbooks. */
	mutable bool bNameLookupCacheValid = false;

	void RebuildTagLookupCache() const;

	/** Cached map: tag -> resolved flipbook indices (into Flipbooks array). */
	mutable TMap<FGameplayTag, TArray<int32>> TagToFlipbookIndicesCache;

	/** Whether TagToAnimationIndicesCache is synchronized with TagMappings/Animations. */
	mutable bool bTagLookupCacheValid = false;
};
