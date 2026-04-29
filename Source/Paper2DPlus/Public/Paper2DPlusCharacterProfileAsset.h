// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Paper2DPlusTypes.h"
#include "FrameEvents/Paper2DPlusFrameEventBase.h"
#include "PaperFlipbook.h"
#include "Engine/Texture2D.h"
#include "GameplayTagContainer.h"
#include "Paper2DPlusCharacterProfileAsset.generated.h"

class UPaperZDAnimSequence;

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

	/** If true, this group is a phase group (Startup/Active/Recovery slots). Phase data lives in FPhaseGroup with matching GroupName. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flipbook Groups")
	bool bIsPhaseGroup = false;
};

/**
 * Internal payload for a frame that has been excluded from a live flipbook but not deleted.
 */
USTRUCT()
struct PAPER2DPLUS_API FExcludedFlipbookFrameData
{
	GENERATED_BODY()

	UPROPERTY()
	FFrameHitboxData FrameData;

	UPROPERTY()
	FSpriteExtractionInfo ExtractionInfo;

	UPROPERTY()
	FPaperFlipbookKeyFrame KeyFrame;

	UPROPERTY()
	FRootMotionFrameData RootMotionData;
};

/** Preserved alignment metadata from the bulk extraction trim pipeline.
 *  Stored per-flipbook so re-extraction can reconstruct the alignment without the padded texture. */
USTRUCT(BlueprintType)
struct PAPER2DPLUS_API FAlignmentMetadata
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Alignment")
	FIntPoint GridDims = FIntPoint::ZeroValue;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Alignment")
	FIntPoint OriginalCellSize = FIntPoint::ZeroValue;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Alignment")
	FIntPoint UniformCellSize = FIntPoint::ZeroValue;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Alignment")
	int32 GroundPlaneOffset = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Alignment")
	int32 NumSprites = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Alignment")
	int32 AlphaThreshold = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Alignment")
	int32 MinSpriteSize = 4;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Alignment")
	int32 IslandMergeDistance = 2;

	bool IsValid() const { return GridDims.X > 0 && GridDims.Y > 0 && UniformCellSize.X > 0 && UniformCellSize.Y > 0; }
};

// ==========================================
// Sub-structs for FFlipbookProfileEntry decomposition
// ==========================================

/** Pure identity: name, flipbook ref, PaperZD bridge. */
USTRUCT()
struct PAPER2DPLUS_API FFlipbookIdentity
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Flipbook")
	FString FlipbookName;

	UPROPERTY(EditAnywhere, Category = "Flipbook")
	TSoftObjectPtr<UPaperFlipbook> Flipbook;

	UPROPERTY(EditAnywhere, Category = "Flipbook")
	TObjectPtr<UPaperZDAnimSequence> PaperZDSequence;
};

/** Editor-only metadata that doesn't affect runtime behavior. */
USTRUCT()
struct PAPER2DPLUS_API FFlipbookEditorMetadata
{
	GENERATED_BODY()

	/**
	 * Per-flipbook completion tracking bitmask for editor workflow.
	 * Bit 0: Hitboxes, Bit 1: Sprite Alignment, Bit 2: Frame Timing,
	 * Bit 3: Phases, Bit 4: Effects, Bit 5: Root Motion, Bit 6: Tag Mappings
	 */
	UPROPERTY(EditAnywhere, Category = "Editor")
	int32 CompletionFlags = 0;
};

/** Per-frame hitbox/socket data + extraction metadata. */
USTRUCT()
struct PAPER2DPLUS_API FFlipbookCombatData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Flipbook")
	TArray<FFrameHitboxData> Frames;

	UPROPERTY()
	TArray<FExcludedFlipbookFrameData> ExcludedFrames;

	UPROPERTY(EditAnywhere, Category = "Sprite Source")
	TArray<FSpriteExtractionInfo> FrameExtractionInfo;
};

/** Per-frame root motion offsets. */
USTRUCT()
struct PAPER2DPLUS_API FFlipbookMotionData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Root Motion")
	TArray<FRootMotionFrameData> RootMotion;

	bool HasRootMotion() const { return RootMotion.Num() > 0; }
};

/** Frame events authored on the timeline. Instanced subobjects of the profile asset. */
USTRUCT()
struct PAPER2DPLUS_API FFlipbookFrameEventData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Instanced, Category = "Frame Events")
	TArray<TObjectPtr<UPaper2DPlusFrameEventBase>> FrameEvents;
};

// ==========================================
// FFlipbookProfileEntry — wrapper with sub-structs
// ==========================================

/**
 * Animation data with hitbox information and sprite extraction metadata.
 * Decomposed into sub-structs by concern: Identity, EditorMeta, CombatData,
 * MotionData, FrameEventData.
 */
USTRUCT(BlueprintType)
struct PAPER2DPLUS_API FFlipbookProfileEntry
{
	GENERATED_BODY()

	// ==========================================
	// Sub-struct members
	// ==========================================

	UPROPERTY(EditAnywhere, Category = "Flipbook")
	FFlipbookIdentity Identity;

	UPROPERTY(EditAnywhere, Category = "Editor")
	FFlipbookEditorMetadata EditorMeta;

	UPROPERTY(EditAnywhere, Category = "Flipbook")
	FFlipbookCombatData CombatData;

	UPROPERTY(EditAnywhere, Category = "Root Motion")
	FFlipbookMotionData MotionData;

	UPROPERTY(EditAnywhere, Category = "Frame Events")
	FFlipbookFrameEventData FrameEventData;

	// ==========================================
	// Fields that stay on the wrapper (not in sub-structs)
	// ==========================================

	/** Source texture this animation was extracted from */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sprite Source")
	TSoftObjectPtr<UTexture2D> SourceTexture;

	/** Output path where sprites for this animation are saved */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sprite Source")
	FString SpritesOutputPath;

	/** Grid alignment metadata from the bulk extraction trim pipeline. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Alignment")
	FAlignmentMetadata AlignmentData;

	/** Visual group assignment for editor organization. Empty = Ungrouped. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flipbook")
	FName FlipbookGroup;

	/** DEPRECATED — migrated to UPaper2DPlusSpawnEffectFrameEvent in FrameEventData.FrameEvents.
	 *  Kept for PostLoad deserialization of pre-migration assets. Do NOT reference in new code. */
	UPROPERTY()
	TArray<FFlipbookEffectData> Effects;

	// ==========================================
	// Deprecated legacy fields (for PostLoad migration of existing assets)
	// UE loads old serialized field names into these via DeprecatedProperty,
	// then PostLoad moves data to the new sub-struct locations.
	// ==========================================

	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Moved to Identity.FlipbookName"))
	FString FlipbookName_DEPRECATED;

	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Moved to Identity.Flipbook"))
	TSoftObjectPtr<UPaperFlipbook> Flipbook_DEPRECATED;

	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Moved to Identity.PaperZDSequence"))
	TObjectPtr<UObject> PaperZDSequence_DEPRECATED;

	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Moved to CombatData.Frames"))
	TArray<FFrameHitboxData> Frames_DEPRECATED;

	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Moved to CombatData.ExcludedFrames"))
	TArray<FExcludedFlipbookFrameData> ExcludedFrames_DEPRECATED;

	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Moved to CombatData.FrameExtractionInfo"))
	TArray<FSpriteExtractionInfo> FrameExtractionInfo_DEPRECATED;

	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Moved to MotionData.RootMotion"))
	TArray<FRootMotionFrameData> RootMotion_DEPRECATED;

	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Moved to EditorMeta.CompletionFlags"))
	int32 CompletionFlags_DEPRECATED = 0;

	// ==========================================
	// Forwarding helpers (convenience accessors through sub-structs)
	// ==========================================

	bool HasRootMotion() const { return MotionData.HasRootMotion(); }

	const FFrameHitboxData* GetFrame(int32 Index) const
	{
		if (CombatData.Frames.IsValidIndex(Index))
		{
			return &CombatData.Frames[Index];
		}
		return nullptr;
	}

	const FFrameHitboxData* GetFrameByName(const FString& FrameName) const
	{
		for (const FFrameHitboxData& Frame : CombatData.Frames)
		{
			if (Frame.FrameName.Equals(FrameName, ESearchCase::IgnoreCase))
			{
				return &Frame;
			}
		}
		return nullptr;
	}

	int32 GetFrameCount() const
	{
		return CombatData.Frames.Num();
	}

	bool HasExtractionInfo() const
	{
		return !SourceTexture.IsNull() && CombatData.FrameExtractionInfo.Num() > 0;
	}

	/** Populates cache-view pointers for the runtime component. */
	void GetCacheView(const FFlipbookCombatData*& OutCombat,
	                  const FFlipbookMotionData*& OutMotion,
	                  const FFlipbookFrameEventData*& OutEventData) const
	{
		OutCombat = &CombatData;
		OutMotion = &MotionData;
		OutEventData = &FrameEventData;
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

	/** PaperZD AnimSequences parallel to FlipbookNames (one per combo entry). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tag Mappings")
	TArray<TObjectPtr<UPaperZDAnimSequence>> PaperZDSequences;

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
enum class ECharacterProfileValidationSeverity : uint8
{
	Info	UMETA(DisplayName = "Info"),
	Warning UMETA(DisplayName = "Warning"),
	Error	UMETA(DisplayName = "Error")
};

/** Status of the last cross-sheet alignment run on a CharacterProfile. */
UENUM(BlueprintType)
enum class EAlignmentStatus : uint8
{
	/** No alignment has ever run (legacy profile or brand-new). */
	Never				UMETA(DisplayName = "Never"),
	/** A run is in progress (transient — should only be observed mid-operation). */
	InProgress			UMETA(DisplayName = "In Progress"),
	/** Last run committed successfully. */
	Completed			UMETA(DisplayName = "Completed"),
	/** Last run aborted and was fully rolled back via snapshot restore. */
	AbortedRolledBack	UMETA(DisplayName = "Aborted (Rolled Back)"),
	/** Last run aborted and the rollback itself failed partway — manual recovery required. */
	AbortedPartial		UMETA(DisplayName = "Aborted (Partial)")
};


/** Internal serializable payload for JSON import/export. */
USTRUCT()
struct PAPER2DPLUS_API FCharacterProfileAssetSerializablePayload
{
	GENERATED_BODY()

	UPROPERTY()
	int32 SchemaVersion = 1;

	UPROPERTY()
	FString DisplayName;

	UPROPERTY()
	TArray<FFlipbookProfileEntry> Flipbooks;

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

	/** Phase group definitions for attack/action sequences. */
	UPROPERTY()
	TArray<FPhaseGroup> PhaseGroups;
};

/** Validation issue generated by CharacterProfile asset validation. */
USTRUCT(BlueprintType)
struct PAPER2DPLUS_API FCharacterProfileValidationIssue
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Validation")
	ECharacterProfileValidationSeverity Severity = ECharacterProfileValidationSeverity::Info;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Validation")
	FString Context;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Validation")
	FString Message;
};

/**
 * Character Profile Asset containing all animation hitbox data.
 * Manages flipbooks, hitboxes, sockets, and extraction metadata across all character animations.
 */
UCLASS(BlueprintType, NotBlueprintable, meta=(DisplayName="Character Profile"))
class PAPER2DPLUS_API UPaper2DPlusCharacterProfileAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPaper2DPlusCharacterProfileAsset();

	// ==========================================
	// EXISTING PROPERTIES (for backward compatibility)
	// ==========================================

	/** Display name for this character profile (e.g., character name) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character Profile")
	FString DisplayName;

	/** Name of the flipbook whose first-frame sprite is used as the Content Browser
	 *  thumbnail. Empty = auto-pick the first flipbook with a resolvable sprite.
	 *  Set via right-click "Set as Thumbnail" in the editor's flipbook context menu. */
	UPROPERTY(EditAnywhere, Category = "Character Profile")
	FString ThumbnailFlipbookName;

	/** All animations with their hitbox data */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character Profile")
	TArray<FFlipbookProfileEntry> Flipbooks;

	/** Visual grouping definitions for the editor Overview tab. */
	UPROPERTY(EditAnywhere, Category = "Flipbook Groups")
	TArray<FFlipbookGroupInfo> FlipbookGroups;

	// ==========================================
	// ANIMATION PHASE GROUPS
	// ==========================================

	/** Phase groups defining attack/action sequences. Each group has up to 3 flipbook slots (Startup/Active/Recovery). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation Phases")
	TArray<FPhaseGroup> PhaseGroups;

	// ==========================================
	// FLIPBOOK TAG MAPPINGS
	// ==========================================

	/** PaperZD AnimSource — used to auto-resolve flipbooks to their AnimSequences. */
	UPROPERTY(EditAnywhere, Category = "Tag Mappings")
	TSoftObjectPtr<UObject> PaperZDAnimSource;

	/** Find the PaperZD AnimSequence that uses the given flipbook, via the AnimSource.
	 *  Searches the asset registry for PaperZDAnimSequence_Flipbook assets belonging to PaperZDAnimSource
	 *  whose primary flipbook matches. Returns nullptr if no match or no AnimSource set. */
	UFUNCTION(BlueprintPure, Category = "Character Profile|PaperZD")
	UPaperZDAnimSequence* FindPaperZDSequenceForFlipbook(UPaperFlipbook* Flipbook) const;

	/** Auto-populate PaperZDSequence on each FFlipbookProfileEntry from the AnimSource.
	 *  Skips entries that already have a sequence assigned. */
	UFUNCTION(BlueprintCallable, Category = "Character Profile|PaperZD")
	void AutoPopulatePaperZDSequences();

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
	// RELATIVE TRANSFORM
	// ==========================================

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Relative Transform")
	FVector RelativeLocation = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Relative Transform")
	FRotator RelativeRotation = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Relative Transform")
	FVector RelativeScale3D = FVector(1.0, 1.0, 1.0);

	UFUNCTION(BlueprintPure, Category = "Relative Transform")
	FTransform GetRelativeTransform() const
	{
		return FTransform(RelativeRotation.Quaternion(), RelativeLocation, RelativeScale3D);
	}

	// ==========================================
	// CROSS-SHEET ALIGNMENT STATE
	// ==========================================

	/** UTC time the last cross-sheet alignment run finished (any status). FDateTime(0) = never. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Alignment")
	FDateTime LastAlignmentCheckTimestamp;

	/** Result of the last alignment run. Updated only on run completion/abort. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Alignment")
	EAlignmentStatus AlignmentStatus = EAlignmentStatus::Never;

	// ==========================================
	// EXISTING LOOKUP FUNCTIONS
	// ==========================================

	/** Get all flipbook names */
	UFUNCTION(BlueprintPure, Category = "Character Profile")
	TArray<FString> GetFlipbookNames() const;

	/** Get flipbook data by name */
	UFUNCTION(BlueprintCallable, Category = "Character Profile")
	bool GetFlipbook(const FString& FlipbookName, FFlipbookProfileEntry& OutFlipbook) const;

	/** Get flipbook data by index */
	UFUNCTION(BlueprintCallable, Category = "Character Profile")
	bool GetFlipbookByIndex(int32 Index, FFlipbookProfileEntry& OutFlipbook) const;

	/** Get frame count for a flipbook */
	UFUNCTION(BlueprintPure, Category = "Character Profile")
	int32 GetFrameCount(const FString& FlipbookName) const;

	/** Get frame data by flipbook name and frame index */
	UFUNCTION(BlueprintCallable, Category = "Character Profile")
	bool GetFrame(const FString& FlipbookName, int32 FrameIndex, FFrameHitboxData& OutFrame) const;

	/** Get frame data by flipbook name and frame name */
	UFUNCTION(BlueprintCallable, Category = "Character Profile")
	bool GetFrameByName(const FString& FlipbookName, const FString& FrameName, FFrameHitboxData& OutFrame) const;

	/** Find flipbook data by Flipbook reference */
	UFUNCTION(BlueprintCallable, Category = "Character Profile")
	bool FindByFlipbook(UPaperFlipbook* Flipbook, FFlipbookProfileEntry& OutFlipbook) const;

	/** Fast lookup helper that avoids copying flipbook data. */
	const FFlipbookProfileEntry* FindByFlipbookPtr(UPaperFlipbook* Flipbook) const;

	/** Invalidate cached flipbook lookup — call after changing asset at runtime. */
	void InvalidateFlipbookLookupCache() { bFlipbookLookupCacheValid = false; }

	/** Get a const pointer to flipbook data by name (no copy). */
	const FFlipbookProfileEntry* FindFlipbookDataPtr(const FString& FlipbookName) const;

	// ==========================================
	// DIRECT HITBOX ACCESS
	// ==========================================

	/** Get all hitboxes for a specific frame */
	UFUNCTION(BlueprintPure, Category = "Character Profile")
	TArray<FHitboxData> GetHitboxes(const FString& FlipbookName, int32 FrameIndex) const;

	/** Get hitboxes of a specific type for a frame */
	UFUNCTION(BlueprintPure, Category = "Character Profile")
	TArray<FHitboxData> GetHitboxesByType(const FString& FlipbookName, int32 FrameIndex, EHitboxType Type) const;

	/** Get all sockets for a specific frame */
	UFUNCTION(BlueprintPure, Category = "Character Profile")
	TArray<FSocketData> GetSockets(const FString& FlipbookName, int32 FrameIndex) const;

	/** Find a specific socket by name */
	UFUNCTION(BlueprintCallable, Category = "Character Profile")
	bool FindSocket(const FString& FlipbookName, int32 FrameIndex, const FString& SocketName, FSocketData& OutSocket) const;

	// ==========================================
	// ASSET INFO
	// ==========================================

	/** Get total number of animations */
	UFUNCTION(BlueprintPure, Category = "Character Profile")
	int32 GetFlipbookCount() const { return Flipbooks.Num(); }

	/** Check if a specific flipbook exists */
	UFUNCTION(BlueprintPure, Category = "Character Profile")
	bool HasFlipbook(const FString& FlipbookName) const;

	// ==========================================
	// TAG MAPPING LOOKUPS
	// ==========================================

	/** Get all flipbook data for a tag, in array order (for combo progression). */
	UFUNCTION(BlueprintPure, Category = "Tag Mappings", meta = (GameplayTagFilter = "Paper2DPlus.Animation"))
	TArray<FFlipbookProfileEntry> GetFlipbookDataForTag(FGameplayTag Group) const;

	/** Get all loaded flipbooks for a tag. */
	UFUNCTION(BlueprintPure, Category = "Tag Mappings", meta = (GameplayTagFilter = "Paper2DPlus.Animation"))
	TArray<UPaperFlipbook*> GetFlipbooksForTag(FGameplayTag Group) const;

	/** Get the first flipbook for a tag, or nullptr if unmapped. */
	UFUNCTION(BlueprintPure, Category = "Tag Mappings", meta = (GameplayTagFilter = "Paper2DPlus.Animation"))
	UPaperFlipbook* GetFirstFlipbookForTag(FGameplayTag Group) const;

	/** Get a random flipbook for a tag (non-deterministic — not safe for networked use). */
	UFUNCTION(BlueprintPure, Category = "Tag Mappings", meta = (GameplayTagFilter = "Paper2DPlus.Animation"))
	UPaperFlipbook* GetRandomFlipbookForTag(FGameplayTag Group) const;

	/** Get the PaperZD AnimSequence for a tag at a specific combo index. Calls LoadSynchronous — cache result in hot paths. */
	UFUNCTION(BlueprintPure, Category = "Tag Mappings", meta = (GameplayTagFilter = "Paper2DPlus.Animation"))
	UPaperZDAnimSequence* GetPaperZDSequenceForTag(FGameplayTag Group, int32 ComboIndex = 0) const;

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

	// ==========================================
	// PHASE GROUP QUERIES
	// ==========================================

	/** Get which phase a flipbook is assigned to in any phase group. Returns None if not in any group. */
	UFUNCTION(BlueprintPure, Category = "Character Profile|Animation Phases")
	EAnimationPhase GetPhaseForFlipbook(const FString& FlipbookName) const;

	/** Get the name of the phase group containing this flipbook. Returns empty string if not in any group. */
	UFUNCTION(BlueprintPure, Category = "Character Profile|Animation Phases")
	FString GetPhaseGroupNameForFlipbook(const FString& FlipbookName) const;

	/** Get all phase group names. */
	UFUNCTION(BlueprintPure, Category = "Character Profile|Animation Phases")
	TArray<FString> GetPhaseGroupNames() const;

	/** Get the flipbook name assigned to a specific phase in a group. Returns empty if not assigned. */
	UFUNCTION(BlueprintPure, Category = "Character Profile|Animation Phases")
	FString GetFlipbookForPhaseInGroup(const FString& GroupName, EAnimationPhase Phase) const;

	/** Get the PaperZD sequence for a specific phase in a group. Returns nullptr if not set. */
	UFUNCTION(BlueprintPure, Category = "Character Profile|Animation Phases")
	UPaperZDAnimSequence* GetPaperZDSequenceForPhaseInGroup(const FString& GroupName, EAnimationPhase Phase) const;

	/** Get the flipbook and optional PaperZD sequence for a phase in a group. Returns true if the phase has a flipbook assigned. */
	UFUNCTION(BlueprintCallable, Category = "Character Profile|Animation Phases")
	bool GetPhaseData(const FString& GroupName, EAnimationPhase Phase, UPaperFlipbook*& OutFlipbook, UPaperZDAnimSequence*& OutPaperZDSequence) const;

	/** Find a phase group by name. Returns nullptr if not found. */
	const FPhaseGroup* FindPhaseGroup(const FString& GroupName) const;

	/** Find a mutable phase group by name. Returns nullptr if not found. */
	FPhaseGroup* FindPhaseGroupMutable(const FString& GroupName);

	/** Find the phase group containing a specific flipbook. Returns nullptr if not found. */
	const FPhaseGroup* FindPhaseGroupForFlipbook(const FString& FlipbookName) const;

	// ==========================================
	// VALIDATION
	// ==========================================

	/** Validate the character profile asset for common data issues.
	 *  @return true when no errors are found (warnings allowed). */
	UFUNCTION(BlueprintCallable, Category = "Validation")
	bool ValidateCharacterProfileAsset(TArray<FCharacterProfileValidationIssue>& OutIssues) const;

	/** Trim trailing frame/extraction metadata beyond the flipbook keyframe count for one animation.
	 *  @return Total entries removed from all arrays for this animation. */
	UFUNCTION(BlueprintCallable, Category = "Character Profile")
	int32 TrimTrailingFrameData(int32 FlipbookIndex);

	/** Trim trailing frame/extraction metadata for all animations.
	 *  @return Total entries removed across all animations. */
	UFUNCTION(BlueprintCallable, Category = "Character Profile")
	int32 TrimAllTrailingFrameData();

	/** Copy all hitboxes/sockets from SourceFrameIndex to an inclusive frame range in an animation.
	 *  Copied hitboxes are clamped to each destination frame's sprite bounds when sprite data is available.
	 *  @param bMerge When true, source hitboxes/sockets are appended to existing
	 *                ones on target frames. When false (default), existing hitboxes
	 *                and sockets are replaced.
	 *  @return true if operation succeeds. */
	UFUNCTION(BlueprintCallable, Category = "Character Profile|Batch")
	bool CopyFrameDataToRange(const FString& FlipbookName, int32 SourceFrameIndex, int32 RangeStart, int32 RangeEnd, bool bIncludeSockets = true, bool bMerge = false);

	/** Exclude a frame from the live flipbook keyframes without deleting its data.
	 *  Excluded frames can be restored later. */
	UFUNCTION(BlueprintCallable, Category = "Character Profile|Frames")
	bool ExcludeFlipbookFrame(int32 FlipbookIndex, int32 FrameIndex);

	/** Restore one excluded frame back into the live flipbook at its original relative order.
	 *  ExcludedFrameIndex is indexed into Flipbooks[FlipbookIndex].ExcludedFrames. */
	UFUNCTION(BlueprintCallable, Category = "Character Profile|Frames")
	bool RestoreExcludedFlipbookFrame(int32 FlipbookIndex, int32 ExcludedFrameIndex);

	/** Restore all excluded frames for one flipbook.
	 *  @return number of frames restored. */
	UFUNCTION(BlueprintCallable, Category = "Character Profile|Frames")
	int32 RestoreAllExcludedFlipbookFrames(int32 FlipbookIndex);

	/** Get excluded-frame count for a flipbook. */
	UFUNCTION(BlueprintPure, Category = "Character Profile|Frames")
	int32 GetExcludedFlipbookFrameCount(int32 FlipbookIndex) const;

	/** Mirror hitboxes horizontally in an inclusive frame range using PivotX.
	 *  @return number of hitboxes mirrored. */
	UFUNCTION(BlueprintCallable, Category = "Character Profile|Batch")
	int32 MirrorHitboxesInRange(const FString& FlipbookName, int32 RangeStart, int32 RangeEnd, int32 PivotX);

	/** Set sprite flip state in an inclusive frame range.
	 *  @return number of frames updated. */
	UFUNCTION(BlueprintCallable, Category = "Character Profile|Batch")
	int32 SetSpriteFlipInRange(const FString& FlipbookName, int32 RangeStart, int32 RangeEnd, bool bInFlipX, bool bInFlipY);

	/** Set sprite flip state across every frame of a flipbook.
	 *  @return number of frames updated. */
	UFUNCTION(BlueprintCallable, Category = "Character Profile|Batch")
	int32 SetSpriteFlipForFlipbook(const FString& FlipbookName, bool bInFlipX, bool bInFlipY);

	/** Set sprite flip state across all flipbooks in the asset.
	 *  @return number of frames updated. */
	UFUNCTION(BlueprintCallable, Category = "Character Profile|Batch")
	int32 SetSpriteFlipForAllFlipbooks(bool bInFlipX, bool bInFlipY);

	// ==========================================
	// HITBOX BOUNDS UTILITIES (static)
	// ==========================================

	/** Get pixel bounds of a sprite at a given flipbook frame index. Returns false if no valid sprite. */
	static bool GetFrameSpriteBounds(UPaperFlipbook* Flipbook, int32 FrameIndex, int32& OutWidth, int32& OutHeight);

	/** Clamp a hitbox to fit within the given pixel bounds. Returns true if any field was changed. */
	static bool ClampHitboxToBounds(FHitboxData& Hitbox, int32 BoundsWidth, int32 BoundsHeight);

	/** Clamp all hitboxes in a frame to the sprite bounds of the corresponding flipbook keyframe. */
	static void ClampFrameHitboxesToSpriteBounds(FFrameHitboxData& Frame, UPaperFlipbook* Flipbook, int32 FrameIndex);

	/** Legacy CharacterProfile JSON schema version used before explicit schema stamping. */
	static constexpr int32 CharacterProfileJsonLegacySchemaVersion = 0;

	/** Current CharacterProfile JSON schema version. */
	static constexpr int32 CharacterProfileJsonSchemaVersion = 7;

	/** Get current CharacterProfile JSON schema version. */
	UFUNCTION(BlueprintPure, Category = "Character Profile|Serialization")
	int32 GetCharacterProfileJsonSchemaVersion() const { return CharacterProfileJsonSchemaVersion; }

	/** Export CharacterProfile content to a deterministic JSON string. */
	UFUNCTION(BlueprintCallable, Category = "Character Profile|Serialization")
	bool ExportToJsonString(FString& OutJson) const;

	/** Import CharacterProfile content from JSON string. */
	UFUNCTION(BlueprintCallable, Category = "Character Profile|Serialization")
	bool ImportFromJsonString(const FString& JsonString);

	/** Export CharacterProfile to a JSON file. */
	UFUNCTION(BlueprintCallable, Category = "Character Profile|Serialization")
	bool ExportToJsonFile(const FString& FilePath) const;

	/** Import CharacterProfile from a JSON file. */
	UFUNCTION(BlueprintCallable, Category = "Character Profile|Serialization")
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

	/** Reparent a visual group under a new parent (NAME_None = root). Does NOT call Modify(). */
	void ReparentFlipbookGroup(FName GroupName, FName NewParent);
#endif

	/** Check if GroupName is a descendant of AncestorGroup (direct or transitive). */
	bool IsDescendantOfFlipbookGroup(FName GroupName, FName AncestorGroup) const;

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
	const FFlipbookProfileEntry* FindFlipbookData(const FString& FlipbookName) const;

private:
	/** Shared migration: legacy FFlipbookEffectData → UPaper2DPlusSpawnEffectFrameEvent.
	 *  Idempotent — entries with populated FrameEvents are skipped. Called from
	 *  both PostLoad and ImportFromJsonString so imports match the PostLoad path. */
	void MigrateEffectsToFrameEvents();

	/** Prune TagMapping FlipbookNames (and parallel PaperZDSequences) whose
	 *  flipbook is no longer present. Called after any operation that replaces
	 *  the Flipbooks array wholesale (e.g., JSON import). */
	void PruneOrphanedTagMappings();

	/** Migrate imported JSON payload to the current schema version when possible. */
	static bool MigrateSerializablePayloadToCurrentSchema(FCharacterProfileAssetSerializablePayload& InOutPayload);

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

	/** Whether TagToFlipbookIndicesCache is synchronized with TagMappings/Flipbooks. */
	mutable bool bTagLookupCacheValid = false;
};
