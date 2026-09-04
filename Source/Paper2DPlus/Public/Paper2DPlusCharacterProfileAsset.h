// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/EngineVersionComparison.h"
#include "Engine/DataAsset.h"
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 4)
#include "UObject/AssetRegistryTagsContext.h"
#endif
#include "Paper2DPlusAuthoringProgressTags.h"
#include "Paper2DPlusTypes.h"
#include "FrameEvents/Paper2DPlusFrameEventBase.h"
#include "FrameCues/Paper2DPlusFrameCue.h"
#include "Paper2DPlusFrameCurve.h"
#include "Paper2DPlusMoveTransition.h"
#include "PaperFlipbook.h"
#include "Engine/Texture2D.h"
#include "GameplayTagContainer.h"
#include "UObject/ObjectSaveContext.h"
#include "Paper2DPlusCharacterProfileAsset.generated.h"


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

	/** Legacy phase-group flag (feature removed 2026-07). Kept only so old assets/JSON deserialize;
	 *  PostLoad's MigrateLegacyGrouping drops flagged rows and reassigns their cards to Unassigned. */
	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Phase groups were removed. Use per-flipbook PhaseTag instead."))
	bool bIsPhaseGroup_DEPRECATED = false;
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

	/** Hidden, save-preserving compatibility bridge for custom executable Frame Events stashed on an
	 *  excluded frame. Do not mark CPF_Deprecated: Unreal would drop unsupported custom payloads on save. */
	UPROPERTY(Instanced)
	TArray<TObjectPtr<UPaper2DPlusFrameEventBase>> StashedFrameEvents;

	/** Cue placements that were anchored on this frame when it was excluded. */
	UPROPERTY(Instanced)
	TArray<TObjectPtr<UPaper2DPlusCueBase>> StashedFrameCues;

	/** Curve points (curveName -> value) that sat on this frame's key-frame index when it was excluded.
	 *  Stashed rather than dropped so a restore reattaches each value to the restored frame (TASK-74,
	 *  parallels StashedFrameCues). A curve with no key on the excluded frame contributes no entry. */
	UPROPERTY()
	TMap<FName, float> StashedCurvePoints;
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

	/**
	 * DEPRECATED and INERT. Padding now places every cell midpoint-to-midpoint, which is the same
	 * anchor the uniform trim bakes as the sprite pivot — so trim and pad agree by construction and
	 * each animation keeps its own vertical motion. `PadTextureInPlace` ignores this value and logs
	 * when a non-zero one reaches it.
	 *
	 * Retained, not drained: assets padded by earlier releases still carry a non-zero value here, and
	 * that number is the only surviving record of how they were laid out. Clearing it on load would
	 * destroy it on the next save for no benefit, since nothing reads it.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Alignment", meta = (DeprecatedProperty, DeprecationMessage = "Padding is midpoint-anchored; GroundPlaneOffset is inert and retained only as a record of pre-existing layouts."))
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
	TObjectPtr<UObject> PaperZDSequence = nullptr;
};

/**
 * Per-flipbook authoring metadata. Never drives playback, but it is NOT editor-only: PhaseTag and
 * AnimationTags are serialized into cooked builds and participate in scoped Animation Map resolution.
 */
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

	/** Optional descriptive phase tag for Animation Map organization. This does not drive playback. */
	UPROPERTY(EditAnywhere, Category = "Animation Map", meta = (GameplayTagFilter = "Paper2DPlus.Phase"))
	FGameplayTag PhaseTag;

	/**
	 * TASK-108 — category tags for this animation under the `Paper2DPlus.Animation` taxonomy
	 * (see Paper2DPlusAnimationTags.h for the dimension rule: hierarchy = specialization within
	 * one dimension, the container = combination ACROSS dimensions — e.g. an airborne heavy is
	 * {Combat.Heavy, Context.Airborne}, never a deep `Combat.Heavy.Airborne` tag). Descriptive
	 * selection data for the scoped Animation Map resolver; it never drives playback by itself.
	 * Serialized so cooked resolution can use it. Empty by default.
	 */
	UPROPERTY(EditAnywhere, Category = "Animation Map", meta = (Categories = "Paper2DPlus.Animation"))
	FGameplayTagContainer AnimationTags;
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

	/** Hit-priority default clash category for this move (TASK-77) — an Attack box on this move that leaves
	 *  its own ClashCategory empty inherits this, so designers can tag a whole move once instead of per box. */
	UPROPERTY(EditAnywhere, Category = "Combat", meta = (Categories = "Paper2DPlus.Clash.Category"))
	FGameplayTag DefaultClashCategory;
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

/** Frame Cue placements plus retained executable-event inventory. */
USTRUCT()
struct PAPER2DPLUS_API FFlipbookFrameEventData
{
	GENERATED_BODY()

	/** Hidden, save-preserving legacy inventory. These rows are never converted or dispatched.
	 *  Do not mark CPF_Deprecated until custom payloads no longer need to round-trip. */
	UPROPERTY(Instanced)
	TArray<TObjectPtr<UPaper2DPlusFrameEventBase>> FrameEvents;

	/** Authoritative Cue placements, including reflected payload and overridable behavior. */
	UPROPERTY(EditAnywhere, Instanced, Category = "Frame Cues")
	TArray<TObjectPtr<UPaper2DPlusCueBase>> FrameCues;

#if WITH_EDITORONLY_DATA
	/** Optional named-track organization. Default is implicit; runtime Cue order remains FrameCues order. */
	UPROPERTY()
	FPaper2DPlusFrameCueTrackLayout CueTrackLayout;
#endif
};

/** One occupied slot in a Directional Animation Set. SlotIndex is stable and never derived from array order. */
USTRUCT(BlueprintType)
struct PAPER2DPLUS_API FPaper2DPlusDirectionalAnimationSlot
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Directional Animation",
		meta = (ClampMin = "0", ClampMax = "15", UIMin = "0", UIMax = "15"))
	int32 SlotIndex = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Directional Animation")
	TSoftObjectPtr<UPaperFlipbook> Flipbook;

	/**
	 * Present this slot's art horizontally mirrored. The resolver returns the flag beside the
	 * flipbook; applying the flip (actor scale or sprite transform) stays project-owned. This is
	 * what lets a standard 8-way set ship five authored facings plus three mirrored reuses.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Directional Animation")
	bool bMirrorHorizontally = false;
};

/**
 * Optional, presence-aware directional art owned by one logical animation.
 *
 * bHasDirectionalSet distinguishes an absent legacy/base-only entry from an explicitly configured
 * empty set. DirectionCount and AngleOffsetDegrees are local values only while
 * bOverrideProfileSettings is true. Slots are sparse records keyed by stable indices 0..15; their
 * array position never defines direction identity.
 */
USTRUCT()
struct PAPER2DPLUS_API FPaper2DPlusDirectionalAnimationData
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category = "Directional Animation")
	bool bHasDirectionalSet = false;

	UPROPERTY(VisibleAnywhere, Category = "Directional Animation")
	bool bOverrideProfileSettings = false;

	UPROPERTY(VisibleAnywhere, Category = "Directional Animation",
		meta = (ClampMin = "3", ClampMax = "16", UIMin = "3", UIMax = "16"))
	int32 DirectionCount = 8;

	UPROPERTY(VisibleAnywhere, Category = "Directional Animation",
		meta = (ClampMin = "-45.0", ClampMax = "45.0", UIMin = "-45.0", UIMax = "45.0"))
	float AngleOffsetDegrees = 0.0f;

	UPROPERTY(VisibleAnywhere, Category = "Directional Animation")
	TArray<FPaper2DPlusDirectionalAnimationSlot> Slots;
};

/** Caller-specific projection for an untouched, wholly base-only duplicate reference. */
enum class PAPER2DPLUS_API EPaper2DPlusLogicalOwnerDuplicatePolicy : uint8
{
	/** Preserve the Profile pointer lookup's historical last-iteration winner. */
	PreserveBaseOnlyIterationWinner,

	/** Preserve callers such as Animation Map that historically failed closed on any duplicate row. */
	RequireUniqueOwner
};

/** Ordered, load-free faults reported by CheckDirectionalAnimationStructure. */
enum class PAPER2DPLUS_API EPaper2DPlusDirectionalStructureFault : uint8
{
	None,
	InvalidAnimationIndex,
	InvalidProfileDirectionCount,
	InvalidProfileAngleOffset,
	InvalidOverrideDirectionCount,
	InvalidOverrideAngleOffset,
	InconsistentSetPresence,
	InvalidSlotIndex,
	DuplicateSlotIndex,
	OccupiedInactiveSlot,
	MissingCanonicalBase
};

/** First structural fault for one logical animation. This result never requires loading a soft asset. */
struct PAPER2DPLUS_API FPaper2DPlusDirectionalStructureResult
{
	EPaper2DPlusDirectionalStructureFault Fault =
		EPaper2DPlusDirectionalStructureFault::None;
	int32 SlotIndex = INDEX_NONE;
	FString Field;
	FString Message;
};

// ==========================================
// FFlipbookProfileEntry — wrapper with sub-structs
// ==========================================

/**
 * Animation data with hitbox information and sprite extraction metadata.
 * Decomposed into sub-structs by concern: Identity, EditorMeta, CombatData,
 * MotionData, FrameEventData, CurveData, TransitionData, and DirectionalAnimationData.
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

	UPROPERTY(EditAnywhere, Category = "Frame Cues", meta = (DisplayName = "Frame Cues"))
	FFlipbookFrameEventData FrameEventData;

	/** Auxiliary per-frame float curves (TASK-74). Additive-optional sibling sub-struct — empty by
	 *  default so existing assets load byte-identically (no schema bump, no migration). */
	UPROPERTY(EditAnywhere, Category = "Curves")
	FFlipbookCurveData CurveData;

	/** Move→move transition/combo links (TASK-76). Additive-optional sibling sub-struct — empty by
	 *  default so existing assets load byte-identically (no schema bump, no migration). */
	UPROPERTY(EditAnywhere, Category = "Transitions")
	FFlipbookTransitionData TransitionData;

	/** Optional directional-art sibling. Presence remains distinct from active occupancy. */
	UPROPERTY(VisibleAnywhere, Category = "Directional Animation")
	FPaper2DPlusDirectionalAnimationData DirectionalAnimationData;

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

	/** Absolute disk path of the .ase file used to create this flipbook (separate-asset reimport tracking). Empty for non-Aseprite imports. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Import")
	FString SourceAseFilePath;

	/** Maps Aseprite layer names to their indices in the parsed layer hierarchy (separate-asset reimport matching). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Import")
	TMap<FString, int32> AseLayerNameToIndex;

	/** Visual group assignment for editor organization. Empty = Ungrouped. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flipbook")
	FName FlipbookGroup;

	/** DEPRECATED and inert — nothing reads this at runtime and no load bridge converts it any more.
	 *  Kept purely so legacy assets keep round-tripping their authored rows instead of losing them on
	 *  the next save; re-author them as Frame Cues in the Frame Cues tab. Do NOT reference in new code.
	 *  (UE strips the "_DEPRECATED" suffix on load, so legacy "Effects" data lands here.) */
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Inert legacy data. Author Frame Cues instead."))
	TArray<FFlipbookEffectData> Effects_DEPRECATED;
	PRAGMA_ENABLE_DEPRECATION_WARNINGS

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
	TObjectPtr<UObject> PaperZDSequence_DEPRECATED = nullptr;

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

	bool HasCurves() const { return CurveData.HasCurves(); }

	bool HasTransitions() const { return TransitionData.HasTransitions(); }

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
 * One member of an animation group: a flipbook name, stable Root Number, and optional PaperZD
 * AnimSequence. Replaces the former parallel FlipbookNames/PaperZDSequences arrays on
 * FFlipbookTagMapping (NS-1 / TASK-3) so a name and its sequence can never fall out of lockstep.
 */
USTRUCT()
struct PAPER2DPLUS_API FFlipbookTagMappingEntry
{
	GENERATED_BODY()

	/** Flipbook name referencing a Flipbooks[].Identity.FlipbookName entry. */
	UPROPERTY(EditAnywhere, Category = "Tag Mappings")
	FString FlipbookName;

	/**
	 * True when this entry STARTS a combo chain in this exact tag mapping (the Animation Map's
	 * "Chain Start" marker). Chains bound at every other flagged entry, so multiple flags in one
	 * group author multiple independent chains. Explicit authored identity — never inferred from
	 * structure (an opener with an incoming counter edge stays the start).
	 */
	UPROPERTY(EditAnywhere, Category = "Animation Map")
	bool bIsChainStart = false;

	/**
	 * True when this entry ENDS a combo chain's countable main line (the Animation Map's "Chain End"
	 * marker). The derived main line always prefers a path terminating at a flagged end and never
	 * walks past one, so trailing recovery/settle animations wired after the end are excluded from
	 * combo indexing and length. No end flagged = the longest authored continuation counts.
	 */
	UPROPERTY(EditAnywhere, Category = "Animation Map")
	bool bIsChainEnd = false;

	/**
	 * Optional identity container for the CHAIN this entry starts (meaningful only with
	 * bIsChainStart). The chain-lookup Blueprint nodes match this container by exact equality with
	 * PRECEDENCE over the opener animation's own AnimationTags, so a chain can be searched up
	 * independently of how its opener animation is tagged.
	 */
	UPROPERTY(EditAnywhere, Category = "Animation Map", meta = (Categories = "Paper2DPlus.Animation"))
	FGameplayTagContainer ChainTags;

	/** Deprecated numbered-root identity (pre chain-start flag). Legacy .uasset/JSON `RootNumber`
	 *  values deserialize here (UHT registers the bare name); `MigrateRootNumbersToChainStarts`
	 *  folds positives into bIsChainStart at load/import. Do NOT use in new code. */
	UPROPERTY(meta = (DeprecatedProperty))
	int32 RootNumber_DEPRECATED = 0;

	/** Optional PaperZD AnimSequence for this combo entry (UObject keeps PaperZD optional). */
	UPROPERTY(EditAnywhere, Category = "Tag Mappings")
	TObjectPtr<UObject> PaperZDSequence = nullptr;

	FFlipbookTagMappingEntry() = default;
	explicit FFlipbookTagMappingEntry(const FString& InFlipbookName, UObject* InPaperZDSequence = nullptr)
		: FlipbookName(InFlipbookName), PaperZDSequence(InPaperZDSequence) {}
};

/**
 * Mapping from a GameplayTag to one or more animation entries + metadata.
 * Tags reference existing animations by name (no data duplication).
 * Entry order is authoring/display order. bIsChainStart marks combo-chain openers.
 */
USTRUCT()
struct PAPER2DPLUS_API FFlipbookTagMapping
{
	GENERATED_BODY()

	/** Group members: flipbook name, chain-start flag, and optional PaperZD sequence. */
	UPROPERTY(EditAnywhere, Category = "Tag Mappings")
	TArray<FFlipbookTagMappingEntry> Entries;

	// ── Deprecated parallel arrays (TASK-3) ──────────────────────────────────
	// Pre-struct-ify assets serialized these two parallel arrays. UE strips the
	// "_DEPRECATED" suffix on load, so legacy data lands here; PostLoad's
	// MigrateTagMappingsToEntries() folds it into Entries. Do NOT use in new code.
	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Use Entries[].FlipbookName"))
	TArray<FString> FlipbookNames_DEPRECATED;

	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Use Entries[].PaperZDSequence"))
	TArray<TObjectPtr<UObject>> PaperZDSequences_DEPRECATED;
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


/**
 * Internal serializable payload for JSON import/export.
 *
 * Legacy-import aliases (handled by ImportFromJsonString → ApplyLegacyJsonAliases, then the shared
 * load migrations). Imports never silently drop data that merely moved or was renamed:
 *  - `"Animations"` (array)            → `"Flipbooks"`            — pre-v5.1 Animation→Flipbook rename.
 *  - flat entry keys `"FlipbookName"`/`"Frames"`/`"FrameExtractionInfo"`/`"ExcludedFrames"`/
 *    `"RootMotion"`/`"CompletionFlags"`/`"Flipbook"`/`"PaperZDSequence"` → the matching sub-struct
 *    (Identity/CombatData/MotionData/EditorMeta) via the entry's *_DEPRECATED members +
 *    MigrateLoadedFlipbookSubStructs (pre-sub-struct-decomposition entries).
 *  - tag-binding parallel `"FlipbookNames"`/`"PaperZDSequences"` → `Entries` via the binding's
 *    *_DEPRECATED members + MigrateTagMappingsToEntries (pre-struct-ify, TASK-3).
 * Aside from `"Animations"` these need no explicit JSON rewrite: UHT registers each *_DEPRECATED
 * member under its bare legacy name, so the importer matches the old keys directly. SchemaVersion
 * is read as-is; a value greater than CharacterProfileJsonSchemaVersion is rejected (future schema).
 */
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
	int32 DefaultDirectionalCount = 8;

	UPROPERTY()
	float DefaultDirectionalAngleOffset = 0.0f;

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

/** Animation Map editor: one comment box (Blueprint-graph-style) inside a group's graph. Editor-only
 *  authoring state — mirrors AnimationMapNodePositions (NOT part of FCharacterProfileAssetSerializablePayload,
 *  so JSON export/import never sees it; plain non-Transient so it serializes to the .uasset, rides undo, and
 *  duplicates with the asset). CommentId == the live UEdGraphNode_Comment's NodeGuid (the write-through handle). */
USTRUCT()
struct FPaper2DPlusAnimationMapComment
{
	GENERATED_BODY()

	UPROPERTY()
	FGuid CommentId;

	UPROPERTY()
	FString Text;

	UPROPERTY()
	FVector2D NodePos = FVector2D::ZeroVector;

	UPROPERTY()
	FVector2D NodeSize = FVector2D(400.0, 100.0);

	UPROPERTY()
	FLinearColor Color = FLinearColor::White;
};

/** Per-group list of Animation Map comments (a UPROPERTY TMap value cannot itself be a TArray). */
USTRUCT()
struct FPaper2DPlusAnimationMapCommentList
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FPaper2DPlusAnimationMapComment> Comments;
};

/**
 * Editor-only character-wide gameplay source for one canonical animation while a Layer bake set is attached.
 * Runtime Flipbooks[].CombatData/FrameEventData are managed output in that mode and never feed the next bake.
 */
USTRUCT()
struct PAPER2DPLUS_API FPaper2DPlusCharacterBaselineAnimation
{
	GENERATED_BODY()

	UPROPERTY()
	TSoftObjectPtr<UPaperFlipbook> Flipbook;

	UPROPERTY()
	FString LegacyAnimationName;

	/** Preserves character-wide frame flags, Collision compatibility data, hitboxes, and sockets. */
	UPROPERTY()
	TArray<FFrameHitboxData> Frames;

	/** Baseline Cue objects are distinct from compiled runtime Cue objects and remain owned by this Profile. */
	UPROPERTY(Instanced)
	TArray<TObjectPtr<UPaper2DPlusCueBase>> FrameCues;

#if WITH_EDITORONLY_DATA
	/** Editor-only organization copied with the baseline's distinct Cue objects. */
	UPROPERTY()
	FPaper2DPlusFrameCueTrackLayout CueTrackLayout;
#endif

	/** Save-preserving inventory for unresolved custom legacy event placements; never dispatched. */
	UPROPERTY(Instanced)
	TArray<TObjectPtr<UPaper2DPlusFrameEventBase>> LegacyFrameEvents;
};

/** One machine-readable advisory produced by Character Profile JSON import. */
struct PAPER2DPLUS_API FPaper2DPlusCharacterProfileJsonImportWarning
{
	FName Code;
	FString Message;
};

namespace Paper2DPlusCharacterProfileJson
{
	/** Stable warning identity for the semantics-only JSON boundary. */
	PAPER2DPLUS_API FName GetTrackLayoutResetWarningCode();

}

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

#if WITH_EDITOR
	/**
	 * Sum the ticked live completion criteria across every animation.
	 *
	 * The bits are manual designer ticks with no content-derived fallback, so this is the only way to
	 * report progress; a profile with no animations legitimately reports a Total of 0.
	 */
	void GetAuthoringProgress(int32& OutDone, int32& OutTotal) const
	{
		OutDone = 0;
		OutTotal = Flipbooks.Num() * LiveTaskCount;
		for (const FFlipbookProfileEntry& Animation : Flipbooks)
		{
			OutDone += FMath::CountBits(
				static_cast<uint32>(Animation.EditorMeta.CompletionFlags & LiveTaskBits));
		}
	}

	/** Designer-facing identity available to unloaded editor catalog cards without loading the profile. */
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 4)
	virtual void GetAssetRegistryTags(FAssetRegistryTagsContext Context) const override
	{
		Super::GetAssetRegistryTags(Context);
		Context.AddTag(FAssetRegistryTag(
			TEXT("Paper2DPlus.CharacterDisplayName"),
			DisplayName,
			FAssetRegistryTag::TT_Hidden));
		int32 ProgressDone = 0;
		int32 ProgressTotal = 0;
		GetAuthoringProgress(ProgressDone, ProgressTotal);
		Context.AddTag(FAssetRegistryTag(
			Paper2DPlusAuthoringProgress::DoneTag(),
			FString::FromInt(ProgressDone),
			FAssetRegistryTag::TT_Hidden));
		Context.AddTag(FAssetRegistryTag(
			Paper2DPlusAuthoringProgress::TotalTag(),
			FString::FromInt(ProgressTotal),
			FAssetRegistryTag::TT_Hidden));
	}
#else
	virtual void GetAssetRegistryTags(TArray<FAssetRegistryTag>& OutTags) const override
	{
		Super::GetAssetRegistryTags(OutTags);
		OutTags.Emplace(
			TEXT("Paper2DPlus.CharacterDisplayName"),
			DisplayName,
			FAssetRegistryTag::TT_Hidden);
		int32 ProgressDone = 0;
		int32 ProgressTotal = 0;
		GetAuthoringProgress(ProgressDone, ProgressTotal);
		OutTags.Emplace(
			Paper2DPlusAuthoringProgress::DoneTag(),
			FString::FromInt(ProgressDone),
			FAssetRegistryTag::TT_Hidden);
		OutTags.Emplace(
			Paper2DPlusAuthoringProgress::TotalTag(),
			FString::FromInt(ProgressTotal),
			FAssetRegistryTag::TT_Hidden);
	}
#endif
#endif

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

	/** Profile-wide direction topology inherited by directional sets without a local override. */
	UPROPERTY(VisibleAnywhere, Category = "Character Profile|Directional Animation",
		meta = (ClampMin = "3", ClampMax = "16", UIMin = "3", UIMax = "16"))
	int32 DefaultDirectionalCount = 8;

	/**
	 * Profile-wide angle offset in degrees, inherited with DefaultDirectionalCount. A positive
	 * value shifts the incoming clockwise facing forward before sector rounding, so on screen the
	 * authored slot layout rotates COUNTER-clockwise. This is exactly PaperZD 2.2.4's
	 * DirectionalAngleOffset semantics (GetDirectionIndexByAngle), kept sign-compatible on purpose
	 * so a project can mirror its PaperZD data-source configuration one-to-one.
	 */
	UPROPERTY(VisibleAnywhere, Category = "Character Profile|Directional Animation",
		meta = (ClampMin = "-45.0", ClampMax = "45.0", UIMin = "-45.0", UIMax = "45.0"))
	float DefaultDirectionalAngleOffset = 0.0f;

	/** All animations with their hitbox data */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character Profile")
	TArray<FFlipbookProfileEntry> Flipbooks;

	// ==========================================
	// DIRECTIONAL ANIMATION SCHEMA (C++ authoring/query seam)
	// ==========================================

	/** Validate the authored count/offset pair without loading any assets. */
	static bool AreDirectionalSettingsValid(int32 DirectionCount, float AngleOffsetDegrees);

	/**
	 * Return the first authored structural fault in a stable order without resolving or loading any
	 * soft reference. Cooked queries and native validation share this exact gate.
	 */
	bool CheckDirectionalAnimationStructure(
		int32 AnimationIndex,
		FPaper2DPlusDirectionalStructureResult& OutResult) const;

#if WITH_EDITOR
	/**
	 * Compare one resident directional variant with its canonical base timeline and normalized frame
	 * geometry. This never loads either asset; editor previews use it to fail closed before showing
	 * art whose shared gameplay data would be spatially or temporally misleading.
	 */
	bool CheckDirectionalAnimationVariantCompatibility(
		int32 AnimationIndex,
		const UPaperFlipbook* VariantFlipbook,
		FString& OutFailureReason) const;
#endif

	/**
	 * Convert a finite nonzero facing vector to its authored direction slot.
	 *
	 * Matches PaperZD 2.2.4's angular convention without depending on PaperZD: +Y is zero,
	 * clockwise is positive, offset is applied before half-sector rounding, and the result wraps by
	 * DirectionCount. Returns false and clears OutSlotIndex for invalid settings, zero, or non-finite
	 * vectors; vector magnitude otherwise has no bearing on the result.
	 */
	static bool ResolveDirectionalSlotIndex(
		const FVector2D& Direction,
		int32 DirectionCount,
		float AngleOffsetDegrees,
		int32& OutSlotIndex);

	/** Resolve Profile defaults or the entry's explicit local override. No soft reference is loaded. */
	bool GetEffectiveDirectionalSettings(
		int32 AnimationIndex,
		int32& OutDirectionCount,
		float& OutAngleOffsetDegrees) const;

	/** True only for explicit set presence; configured-empty therefore returns true. */
	bool HasDirectionalSet(int32 AnimationIndex) const;

	/** True only when at least one non-null slot is active under the effective direction count. */
	bool HasActiveDirectionalSlots(int32 AnimationIndex) const;

	/** Read one occupied stable slot without loading its soft flipbook. */
	bool GetDirectionalSlot(
		int32 AnimationIndex,
		int32 SlotIndex,
		TSoftObjectPtr<UPaperFlipbook>& OutFlipbook) const;

	/** Report whether a Profile count can be applied without deactivating inheriting assignments. */
	bool CanSetDirectionalDefaults(
		int32 DirectionCount,
		TArray<int32>& OutStrandedAnimationIndices) const;

	/** Report whether an override transition can be applied without deactivating this entry's assignments. */
	bool CanSetDirectionalOverride(
		int32 AnimationIndex,
		bool bOverrideProfileSettings,
		int32 DirectionCount,
		TArray<int32>& OutStrandedSlotIndices) const;

	/** Atomically update Profile defaults; invalid or stranding requests leave the Profile unchanged. */
	bool SetDirectionalDefaults(int32 DirectionCount, float AngleOffsetDegrees);

	/** Create explicit configured-empty presence for an entry with a canonical base flipbook. */
	bool EnableDirectionalSet(int32 AnimationIndex);

	/** Remove explicit presence only when every sparse slot is unoccupied. */
	bool RemoveDirectionalSet(int32 AnimationIndex);

	/** Enable/disable the local settings override without using zero values as inheritance sentinels. */
	bool SetDirectionalOverride(
		int32 AnimationIndex,
		bool bOverrideProfileSettings,
		int32 DirectionCount,
		float AngleOffsetDegrees);

	/** Assign one stable slot. First assignment enables an absent set only when a canonical base exists. */
	bool SetDirectionalSlot(
		int32 AnimationIndex,
		int32 SlotIndex,
		const TSoftObjectPtr<UPaperFlipbook>& Flipbook);

	/** Clear one stable slot while preserving configured presence; malformed absent-presence rows remain repairable. */
	bool ClearDirectionalSlot(int32 AnimationIndex, int32 SlotIndex);

	/** Read one occupied stable slot plus its mirror presentation flag. No soft reference is loaded. */
	bool GetDirectionalSlot(
		int32 AnimationIndex,
		int32 SlotIndex,
		TSoftObjectPtr<UPaperFlipbook>& OutFlipbook,
		bool& bOutMirrorHorizontally) const;

	/** Set the mirror presentation flag on an existing occupied slot; false when no record exists. */
	bool SetDirectionalSlotMirror(
		int32 AnimationIndex,
		int32 SlotIndex,
		bool bMirrorHorizontally);

#if WITH_EDITORONLY_DATA
	/** Exclusive bake owner token. The Layer Asset path is a diagnostic hint, never an object back-reference. */
	UPROPERTY(VisibleAnywhere, Category = "Character Profile|Layer Bake")
	FGuid LayerBakeOwnerToken;

	UPROPERTY(VisibleAnywhere, Category = "Character Profile|Layer Bake")
	FString LayerBakeOwnerPathHint;

	/** Character-owned source data used only while LayerBakeOwnerToken is valid. */
	UPROPERTY()
	TArray<FPaper2DPlusCharacterBaselineAnimation> CharacterBaseline;
#endif

	/** Visual grouping definitions for the editor Overview tab. */
	UPROPERTY(EditAnywhere, Category = "Flipbook Groups")
	TArray<FFlipbookGroupInfo> FlipbookGroups;

	// ==========================================
	// FLIPBOOK TAG MAPPINGS
	// ==========================================

	/** PaperZD AnimSource — used to auto-resolve flipbooks to their AnimSequences. */
	UPROPERTY(EditAnywhere, Category = "Tag Mappings")
	TSoftObjectPtr<UObject> PaperZDAnimSource;

	/** Find the PaperZD AnimSequence that uses the given flipbook, via the AnimSource.
	 *  Searches the asset registry for PaperZDAnimSequence_Flipbook assets belonging to PaperZDAnimSource
	 *  whose primary flipbook matches. Returns nullptr if no match or no AnimSource set.
	 *  C++-only; the Blueprint face is UPaper2DPlusPaperZDLibrary::FindPaperZDSequenceForFlipbook. */
	UObject* FindPaperZDSequenceForFlipbook(UPaperFlipbook* Flipbook) const;

	/** Auto-populate PaperZDSequence on each FFlipbookProfileEntry from the AnimSource.
	 *  Skips entries that already have a sequence assigned. */
	UFUNCTION(BlueprintCallable, Category = "Character Profile|PaperZD")
	void AutoPopulatePaperZDSequences();

	/** Tag-to-animation mappings. Each GameplayTag maps to one or more animation entries + metadata. */
	UPROPERTY(EditAnywhere, Category = "Tag Mappings",
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
	// COMBO GRAPH EDITOR STATE
	// ==========================================

#if WITH_EDITORONLY_DATA
	/** Animation Map editor: per-move node positions keyed by LOWERCASED flipbook name (the
	 *  NameToFlipbookIndexCache key convention). Editor-only data — stripped from cooked builds, no
	 *  runtime serialization impact. Deliberately NOT part of FCharacterProfileAssetSerializablePayload,
	 *  so JSON export/import never sees it (ExportToJsonString hand-copies exactly the payload fields).
	 *  Plain (non-Transient) so placements serialize to the .uasset (source-control shared), ride undo
	 *  transactions, and duplicate with the asset. Keys for removed moves are kept on purpose
	 *  (re-adding the move restores its spot); RenameFlipbookAndPropagate rewrites the key on rename. */
	UPROPERTY()
	TMap<FString, FVector2D> AnimationMapNodePositions;

#if WITH_EDITORONLY_DATA
	/** Character Sizing: the gameplay capsule to size against, read off the class DEFAULT OBJECT --
	 *  no spawned actor, no PIE. Editor-only authoring state, excluded from
	 *  FCharacterProfileAssetSerializablePayload exactly like AnimationMapNodePositions, so JSON
	 *  export/import never carries it. Soft so setting it loads no Blueprint. */
	UPROPERTY(EditAnywhere, Category = "Character Sizing")
	TSoftClassPtr<AActor> SizingCharacterClass;

	/** Fallback target height in Unreal units for projects that do not use ACharacter capsules. Used
	 *  only when SizingCharacterClass resolves no capsule. Zero means "not configured", which the
	 *  sizing tool reports as a prompt rather than fitting to a degenerate target. */
	UPROPERTY(EditAnywhere, Category = "Character Sizing")
	float SizingTargetHeight = 0.0f;

	/** Which animation the fit measures. Empty resolves to idle. Fitting against max extents across
	 *  EVERY animation is wrong -- one outstretched pose would shrink the idle -- so the reference
	 *  pose is a single deliberate choice and is shown on the surface. */
	UPROPERTY(EditAnywhere, Category = "Character Sizing")
	FString SizingReferenceAnimation;
#endif

	/** Animation Map editor: per-GROUP comment boxes, keyed by the group SCOPE KEY (the active group tag's
	 *  string, or "__unassigned__" for the unassigned bucket). Editor-only, NOT part of
	 *  FCharacterProfileAssetSerializablePayload (JSON-neutral by construction). Plain (non-Transient) so
	 *  comments serialize to the .uasset, ride undo, and duplicate with the asset. Comments for a deleted
	 *  group are kept on purpose (re-adding the group restores them). */
	UPROPERTY()
	TMap<FString, FPaper2DPlusAnimationMapCommentList> AnimationMapComments;

#endif

	// ==========================================
	// EXISTING LOOKUP FUNCTIONS
	// ==========================================

	/** Get all flipbook names */
	UFUNCTION(BlueprintPure, Category = "Character Profile")
	TArray<FString> GetFlipbookNames() const;

	/** Get flipbook data by index */
	UFUNCTION(BlueprintCallable, Category = "Character Profile")
	bool GetFlipbookByIndex(int32 Index, FFlipbookProfileEntry& OutFlipbook) const;

	/** Get frame data by flipbook name and frame name */
	UFUNCTION(BlueprintCallable, Category = "Character Profile")
	bool GetFrameByName(const FString& FlipbookName, const FString& FrameName, FFrameHitboxData& OutFrame) const;

	/** Find flipbook data by Flipbook reference */
	UFUNCTION(BlueprintCallable, Category = "Character Profile")
	bool FindByFlipbook(UPaperFlipbook* Flipbook, FFlipbookProfileEntry& OutFlipbook) const;

	/**
	 * Resolve a base or active directional variant to its one logical animation owner.
	 * Repeated references under one owner collapse. Cross-owner directional involvement fails closed;
	 * untouched base-only duplicates retain the historical last-iteration winner.
	 */
	const FFlipbookProfileEntry* ResolveLogicalAnimationOwner(
		UPaperFlipbook* Flipbook,
		bool& bOutAmbiguous,
		EPaper2DPlusLogicalOwnerDuplicatePolicy DuplicatePolicy =
			EPaper2DPlusLogicalOwnerDuplicatePolicy::PreserveBaseOnlyIterationWinner) const;

	/** Compatibility wrapper around ResolveLogicalAnimationOwner that returns null for ambiguity. */
	const FFlipbookProfileEntry* FindByFlipbookPtr(UPaperFlipbook* Flipbook) const;

	/**
	 * Resolve a Cue snapshot's base or variant flipbook to its canonical logical Profile row, then
	 * require that row's authored animation name to match the snapshot.
	 *
	 * No soft reference is loaded. Directional owner collisions fail closed before the name filter.
	 * For an untouched base-only duplicate, the complete name+flipbook identity preserves the exact
	 * lookup's historical unique/ambiguous projection.
	 */
	const FFlipbookProfileEntry* FindExactFlipbookData(
		FName AnimationName,
		UPaperFlipbook* Flipbook,
		bool& bOutAmbiguous) const;

	/** Resolve a key-frame's sprite pivot in sprite-local top-left space (GetPivotPosition()-GetSourceUV()).
	 *  EDITOR: computes it live from the sprite. NON-EDITOR (packaged): reads the serialized
	 *  FrameExtractionInfo[FrameIndex].CachedPivotLocal (baked at cook by PreSave). Returns false when no
	 *  pivot is available (caller then skips pivot adjustment = top-left fallback). Single source of truth
	 *  for runtime pivot resolution across all build configs — see TASK-48. */
	bool GetFramePivotLocal(UPaperFlipbook* Flipbook, int32 FrameIndex, FVector2D& OutPivotLocal) const;

	/**
	 * Exact-row form of GetFramePivotLocal used after a complete Cue identity has resolved.
	 * Packaged builds read the pivot cache from this same row and never re-resolve by flipbook alone.
	 */
	bool GetFramePivotLocalForEntry(
		const FFlipbookProfileEntry& Entry,
		UPaperFlipbook* Flipbook,
		int32 FrameIndex,
		FVector2D& OutPivotLocal) const;

	/** Invalidate cached flipbook lookup — call after changing asset at runtime. */
	void InvalidateFlipbookLookupCache() { bFlipbookLookupCacheValid = false; }

	/** Get a const pointer to flipbook data by name (no copy). */
	const FFlipbookProfileEntry* FindFlipbookDataPtr(const FString& FlipbookName) const;

	// ==========================================
	// ASSET INFO
	// ==========================================

	/** Get total number of animations */
	UFUNCTION(BlueprintPure, Category = "Character Profile")
	int32 GetFlipbookCount() const { return Flipbooks.Num(); }

	// ==========================================
	// OBJECT-REFERENCE VARIANTS (UPaperFlipbook* in/out)
	// ==========================================
	//
	// Every read accessor above keys off the authored animation NAME. The functions below accept a
	// canonical base or active directional variant as INPUT, resolve its logical owner once, and read
	// that owner's shared gameplay data. Object OUTPUT remains the canonical base identity.

	/** Resolve a flipbook name to its canonical base UPaperFlipbook (the entry's Identity.Flipbook,
	 *  loaded if needed). Null when the name is unknown or the entry has no base assigned. */
	UFUNCTION(BlueprintPure, Category = "Character Profile")
	UPaperFlipbook* GetFlipbookByName(const FString& FlipbookName) const;

	/** Resolve a base or active directional variant to its logical animation name on this asset.
	 *  Empty string when the object has no unambiguous owner. A variant therefore round-trips through
	 *  GetFlipbookByName to its canonical base rather than back to the directional art reference. */
	UFUNCTION(BlueprintPure, Category = "Character Profile")
	FString GetFlipbookName(UPaperFlipbook* Flipbook) const;

	/** Object-ref form of HasFlipbook: true when this reference is one of the asset's flipbooks. */
	UFUNCTION(BlueprintPure, Category = "Character Profile")
	bool ContainsFlipbook(UPaperFlipbook* Flipbook) const;

	/** Object-ref form of GetFrameCount. */
	UFUNCTION(BlueprintPure, Category = "Character Profile")
	int32 GetFrameCountByFlipbook(UPaperFlipbook* Flipbook) const;

	/** Object-ref form of GetFrame. */
	UFUNCTION(BlueprintCallable, Category = "Character Profile")
	bool GetFrameByFlipbook(UPaperFlipbook* Flipbook, int32 FrameIndex, FFrameHitboxData& OutFrame) const;

	/** Object-ref form of GetHitboxes. */
	UFUNCTION(BlueprintPure, Category = "Character Profile")
	TArray<FHitboxData> GetHitboxesByFlipbook(UPaperFlipbook* Flipbook, int32 FrameIndex) const;

	/** Object-ref form of GetHitboxesByType. */
	UFUNCTION(BlueprintPure, Category = "Character Profile")
	TArray<FHitboxData> GetHitboxesOfTypeByFlipbook(UPaperFlipbook* Flipbook, int32 FrameIndex, EHitboxType Type) const;

	/** Object-ref form of GetSockets. */
	UFUNCTION(BlueprintPure, Category = "Character Profile")
	TArray<FSocketData> GetSocketsByFlipbook(UPaperFlipbook* Flipbook, int32 FrameIndex) const;

	/** Object-ref form of FindSocket. */
	UFUNCTION(BlueprintCallable, Category = "Character Profile")
	bool FindSocketByFlipbook(UPaperFlipbook* Flipbook, int32 FrameIndex, const FString& SocketName, FSocketData& OutSocket) const;

	/** Object-ref form of GetAttackRangeForFlipbook. */
	UFUNCTION(BlueprintPure, Category = "Attack Bounds")
	float GetAttackRangeByFlipbook(UPaperFlipbook* Flipbook) const;

	/** Object-ref form of GetAttackBoundsForFlipbook. */
	UFUNCTION(BlueprintPure, Category = "Attack Bounds")
	FBox2D GetAttackBoundsByFlipbook(UPaperFlipbook* Flipbook) const;

	// ==========================================
	// ATTACK BOUNDS (AI HELPERS)
	// ==========================================

	/** Get the max attack range across ALL animations (distance from origin to furthest attack hitbox edge). */
	UFUNCTION(BlueprintPure, Category = "Attack Bounds")
	float GetMaxAttackRange() const;

	/** Get the max attack range for a specific tag mapping. */
	UFUNCTION(BlueprintPure, Category = "Attack Bounds", meta = (GameplayTagFilter = "Paper2DPlus.Animation"))
	float GetAttackRangeForTag(FGameplayTag Tag) const;

	/** Get the combined bounds (FBox2D) of all attack hitboxes across all frames of a tag. */
	UFUNCTION(BlueprintPure, Category = "Attack Bounds", meta = (GameplayTagFilter = "Paper2DPlus.Animation"))
	FBox2D GetAttackBoundsForTag(FGameplayTag Tag) const;

	/** Get the combined bounds (FBox2D) of all attack hitboxes across all frames of an animation. */
	FBox2D GetAttackBoundsForFlipbook(const FString& FlipbookName) const;

	// (The PHASE GROUP query surface is GONE — the phase-groups feature was removed entirely in
	// the 2026-07 legacy cleanup. Per-flipbook phase identity lives on EditorMeta.PhaseTag —
	// `Paper2DPlus.Phase.*` — and derived combo-chain position; see Paper2DPlusComboChain.)

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

	/** Reorder a live flipbook frame, moving it from FromIndex to ToIndex.
	 *  The live keyframes and all parallel per-frame metadata (Frames,
	 *  FrameExtractionInfo, RootMotion) are permuted in lockstep, and
	 *  SourceFrameIndex is re-stamped to the new positional order so the
	 *  Sprite Editor frame strip (which sorts/labels by SourceFrameIndex)
	 *  reflects the reorder. No-op (returns false) when FromIndex == ToIndex
	 *  or either index is out of range.
	 *
	 *  Excluded frames: the re-stamp packs any excluded frames after the active
	 *  ones, so reordering active frames moves the greyed excluded cells to the
	 *  end of the strip and a later restore re-inserts them after the active
	 *  frames rather than at their pre-reorder interleaved slot. This is
	 *  intentional — once active frames are deliberately permuted, the original
	 *  interleave position is no longer well-defined (and dense SourceFrameIndex
	 *  labels leave no gap to interleave into). No frame data is lost.
	 *
	 *  Frame events are NOT remapped here, consistent with ExcludeFlipbookFrame /
	 *  RestoreExcludedFlipbookFrame — event TriggerFrame/StartFrame values are
	 *  treated as user-managed (asset validation flags any left out of bounds). */
	UFUNCTION(BlueprintCallable, Category = "Character Profile|Frames")
	bool MoveFlipbookFrame(int32 FlipbookIndex, int32 FromIndex, int32 ToIndex);

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

	static constexpr int32 MinimumDirectionalCount = 3;
	static constexpr int32 MaximumDirectionalCount = 16;
	static constexpr float MinimumDirectionalAngleOffset = -45.0f;
	static constexpr float MaximumDirectionalAngleOffset = 45.0f;

	/** Shared tolerance for directional timeline and frame-geometry floating-point comparisons. */
	static constexpr float DirectionalCompatibilityFloatTolerance = 0.001f;

	/** Current CharacterProfile JSON schema version. */
	static constexpr int32 CharacterProfileJsonSchemaVersion = 9;

	/** CompletionFlags bits that still map to a live editor task (the others were retired with
	 *  their tabs). Bit 0: Hitboxes, 1: Alignment, 2: Timing, 5: Motion, 6: Tags.
	 *  PostLoad strips everything NOT in this mask; the completion meter counts/divides by it. */
	static constexpr int32 LiveTaskBits = (1 << 0) | (1 << 1) | (1 << 2) | (1 << 5) | (1 << 6);

	/** Number of live completion tasks (popcount of LiveTaskBits). Meter denominator. */
	static constexpr int32 LiveTaskCount = 5;

	/** Get current CharacterProfile JSON schema version. */
	UFUNCTION(BlueprintPure, Category = "Character Profile|Serialization")
	int32 GetCharacterProfileJsonSchemaVersion() const { return CharacterProfileJsonSchemaVersion; }

	/** Export CharacterProfile content to a deterministic JSON string. */
	UFUNCTION(BlueprintCallable, Category = "Character Profile|Serialization")
	bool ExportToJsonString(FString& OutJson) const;

	/** Import CharacterProfile content from JSON string. */
	UFUNCTION(BlueprintCallable, Category = "Character Profile|Serialization")
	bool ImportFromJsonString(const FString& JsonString);

	/** C++ import seam that returns structured advisories instead of logging them. */
	bool ImportFromJsonStringWithWarnings(
		const FString& JsonString,
		TArray<FPaper2DPlusCharacterProfileJsonImportWarning>& OutWarnings);

	/** Export CharacterProfile to a JSON file. */
	UFUNCTION(BlueprintCallable, Category = "Character Profile|Serialization")
	bool ExportToJsonFile(const FString& FilePath) const;

	/** Import CharacterProfile from a JSON file. */
	UFUNCTION(BlueprintCallable, Category = "Character Profile|Serialization")
	bool ImportFromJsonFile(const FString& FilePath);

	/** File counterpart to ImportFromJsonStringWithWarnings. */
	bool ImportFromJsonFileWithWarnings(
		const FString& FilePath,
		TArray<FPaper2DPlusCharacterProfileJsonImportWarning>& OutWarnings);

	/** Get asset primary ID for async loading */
	virtual FPrimaryAssetId GetPrimaryAssetId() const override;

	/** Sync hitbox Frames[] array to match the flipbook frame count for an animation.
	 *  Preserves existing data, appends empty FFrameHitboxData for new frames.
	 *  @param bGrowOnly  When true, arrays are only GROWN to the key-frame count, never shrunk — use this for
	 *                    a passive on-open repair so a profile whose per-frame data is LONGER than the flipbook
	 *                    (e.g. key frames removed externally) is not silently truncated. Default false = exact
	 *                    sync (grow AND shrink), the correct behavior at explicit flipbook-assignment sites. */
	void SyncFramesToFlipbook(int32 FlipbookIndex, bool bGrowOnly = false);

	/** Sync all animations' frame arrays to their flipbooks. See SyncFramesToFlipbook for bGrowOnly. */
	void SyncAllFramesToFlipbooks(bool bGrowOnly = false);

	/** Update animation name references in TagMappings when an animation is renamed. */
	void UpdateTagMappingFlipbookName(const FString& OldName, const FString& NewName);

	/** Add or move a flipbook into one tag mapping, removing it from every other mapping.
	 *  Does NOT call Modify() — caller must manage transactions. */
	bool AssignFlipbookToTagMapping(FGameplayTag Tag, const FString& FlipbookName, UObject* PaperZDSequence = nullptr);

	/** Set an existing tag-mapping entry and enforce the same one-tag-home invariant.
	 *  Does NOT call Modify() — caller must manage transactions. */
	bool SetTagMappingEntryFlipbook(FGameplayTag Tag, int32 EntryIndex, const FString& FlipbookName, UObject* PaperZDSequence = nullptr);

	/** Set or clear one entry's chain-start flag. False when nothing changes.
	 *  Does NOT call Modify() — caller must manage transactions. */
	bool SetTagMappingEntryChainStart(FGameplayTag Tag, int32 EntryIndex, bool bInIsChainStart);

	/** Set or clear one entry's chain-end flag. False when nothing changes.
	 *  Does NOT call Modify() — caller must manage transactions. */
	bool SetTagMappingEntryChainEnd(FGameplayTag Tag, int32 EntryIndex, bool bInIsChainEnd);

	/** Replace one entry's chain-identity container (exact-set compare, order-independent).
	 *  False when nothing changes. Does NOT call Modify() — caller must manage transactions. */
	bool SetTagMappingEntryChainTags(FGameplayTag Tag, int32 EntryIndex, const FGameplayTagContainer& InChainTags);

	/** Remove one flipbook from one tag mapping and clear the matching tag-backed group assignment.
	 *  Does NOT call Modify() — caller must manage transactions. */
	bool RemoveFlipbookFromTagMapping(FGameplayTag Tag, const FString& FlipbookName);

	/** Rename a tag mapping and move its overview group membership to the new tag name.
	 *  Does NOT call Modify() — caller must manage transactions. */
	bool RenameTagMapping(FGameplayTag OldTag, FGameplayTag NewTag);

	/** Remove a tag mapping and clear overview group membership that points at that tag.
	 *  Does NOT call Modify() — caller must manage transactions. */
	bool RemoveTagMapping(FGameplayTag Tag);

	/** Remove duplicate memberships for a flipbook, preserving exactly the requested entry.
	 *  Does NOT call Modify() — caller must manage transactions. */
	int32 RemoveDuplicateFlipbookTagMappings(const FString& FlipbookName, FGameplayTag TagToKeep, int32 EntryIndexToKeep);

	/** Normalize all tag mappings so a non-empty flipbook name is mapped under at most one tag.
	 *  Does NOT call Modify() — caller must manage transactions. */
	int32 NormalizeTagMappingsToOneFlipbookHome();

	/** Update flipbook-name references in move-transition targets (every entry's
	 *  TransitionData.Transitions) when an animation is renamed. Case-insensitive match, mirroring
	 *  UpdateTagMappingFlipbookName (TASK-76). The FROM side renames implicitly — transitions live
	 *  on the entry itself. */
	void UpdateTransitionFlipbookName(const FString& OldName, const FString& NewName);

	/** TASK-108 U1 shared migration: purify move transitions to pure From→To. (a) DedupeTransitionRows
	 *  first (first-in-array wins, judged on the authored array); (b) drop the soft-deprecated
	 *  Tag/CancelCategory/Condition values, one Info log per dropped value (a non-Always Condition logs
	 *  a chain-semantics note — that row now counts as a combo-chain edge). Idempotent: a migrated
	 *  asset re-runs to 0 (so re-save/reload emits no second log). Called from both PostLoad and
	 *  ImportFromJsonString (the shared-migration dual-call discipline). Does NOT call
	 *  Modify(). Returns rows removed + values dropped — tests pin idempotency on the count. */
	int32 MigrateMoveTransitions();

	/** The dedupe half alone: one row per (owning flipbook, TargetMove case-insensitive) pair,
	 *  first-in-array wins, one Info log per dropped row. Empty-target (authoring-in-progress) rows are
	 *  exempt. Re-run at the end of UpdateTransitionFlipbookName — a rename can re-create a duplicate
	 *  pair. Does NOT call Modify(). Returns the number of rows removed. */
	int32 DedupeTransitionRows();

	/** Rename the flipbook entry at FlipbookIndex and propagate the new name to every by-name
	 *  reference on this asset: TagMappings,
	 *  move-transition targets, ThumbnailFlipbookName, and (editor-only) the AnimationMapNodePositions
	 *  key. Invalidates the name/flipbook lookup caches. Returns false and changes
	 *  nothing when the index is invalid, the trimmed name is empty, the name is unchanged, or it
	 *  collides with another flipbook (case-insensitive). Does NOT call Modify() — the caller must
	 *  manage the transaction. */
	bool RenameFlipbookAndPropagate(int32 FlipbookIndex, const FString& NewName);

	/** Remove a flipbook name from all TagMappings entries. */
	void RemoveFlipbookFromTagMappings(const FString& FlipbookName);

	// ==========================================
	// FLIPBOOK GROUP HELPERS
	// ==========================================

#if WITH_EDITOR
	/** True when a visual group name resolves to a gameplay tag-backed animation-map lane. */
	bool IsTagBackedFlipbookGroup(FName GroupName, FGameplayTag& OutTag) const;

	/** Ensure an overview group named exactly like Tag exists. Does NOT call Modify(). */
	bool EnsureFlipbookGroupForTag(FGameplayTag Tag);

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

	/** Current root-motion schema version. Bump when RootMotion data layout/semantics change.
	 *  v1 = Position.Y stored in world Z-up convention (pixel Y-down sign-flipped). */
	static constexpr uint32 CurrentRootMotionVersion = 1;

	/** Versioned migration counter for root motion Y-axis sign fix (pixel Y-down → world Z-up).
	 *  CDO default stays 0 so genuinely pre-v1 assets migrate on load; PostInitProperties stamps
	 *  CurrentRootMotionVersion on every genuine in-memory creation path. */
	UPROPERTY()
	uint32 RootMotionVersion = 0;

	/** Stamp RootMotionVersion to current on creation / duplicate / programmatic NewObject, but NOT
	 *  on the CDO or on objects being loaded — so PostLoad can still migrate genuine legacy data. */
	virtual void PostInitProperties() override;

	/** Post-load hook for asset migration */
	virtual void PostLoad() override;

	/** A duplicated Profile cannot retain another Layer Asset's exclusive ownership claim. */
	virtual void PostDuplicate(EDuplicateMode::Type DuplicateMode) override;

#if WITH_EDITOR
	/** Explicit adoption helper. Captures live runtime source without changing any compiled output. */
	bool CaptureCharacterBaselineFromRuntime(const FGuid& OwnerToken, const FString& OwnerPathHint, bool bReplaceExisting = false);

	const FPaper2DPlusCharacterBaselineAnimation* FindCharacterBaseline(const FSoftObjectPath& FlipbookPath, const FString& LegacyName) const;
	FPaper2DPlusCharacterBaselineAnimation* FindCharacterBaselineMutable(const FSoftObjectPath& FlipbookPath, const FString& LegacyName);
#endif

	/** Legacy-cleanup 2026-07 migration (PostLoad + ImportFromJsonString): (1) drops visual group
	 *  rows flagged with the retired bIsPhaseGroup (phase groups were removed as a feature — their
	 *  FPhaseGroup slot payload is dropped on load) and (2) drops stale TAG-BACKED visual group rows
	 *  whose TagMappings key is gone or empty (the pre-cleanup RemoveTagMapping left the row behind).
	 *  Cards in any removed group fall to Ungrouped/Unassigned; child groups reparent to root.
	 *  Idempotent; does NOT call Modify() (passive on-load repair, like the frame-array grow sync). */
	void MigrateLegacyGrouping();

	/**
	 * Monotonic counter over in-place EDITOR mutations of this asset, so a consumer that memoizes a
	 * derived answer can tell "same asset, same content" from "same asset, edited since".
	 *
	 * Exists because pointer identity is not content identity while an asset is being authored: the
	 * Frame Cue detection tick rate memoizes "does this profile carry any cue" against the profile it
	 * scanned, and authoring the FIRST cue into an already-assigned profile changes neither the pointer
	 * nor the element count. The memo then keeps the slow poll and a short animation's cues never fire
	 * — during PIE, which is exactly when a designer is authoring them.
	 *
	 * Always 0 in cooked builds: nothing mutates an asset there, so the memo needs no revision at all
	 * and pays nothing for this.
	 */
	uint32 GetEditorContentRevision() const
	{
#if WITH_EDITORONLY_DATA
		return EditorContentRevision;
#else
		return 0;
#endif
	}

#if WITH_EDITOR
	/** Every authoring path funnels through Modify() per this project's transaction template, which
	 *  makes it the one hook that sees custom-panel array edits as well as details-panel edits. Bumping
	 *  here deliberately over-invalidates — a redundant re-scan is cheap and editor-only, whereas a
	 *  missed one costs dispatches. */
	virtual bool Modify(bool bAlwaysMarkDirty = true) override;
	virtual void PostEditUndo() override;
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;

	/** Bake each key-frame's live sprite pivot into FrameExtractionInfo[i].CachedPivotLocal so packaged
	 *  (non-editor) builds get pivot-correct hitbox/socket conversion. Runs on PreSave so every cook
	 *  carries a fresh cache — existing assets migrate automatically on the next cook/save (TASK-48). */
	virtual void PreSave(FObjectPreSaveContext SaveContext) override;
	void RepopulatePivotCache();
#endif

protected:
	/** Internal lookup - find flipbook data by name */
	const FFlipbookProfileEntry* FindFlipbookData(const FString& FlipbookName) const;

private:
	bool CheckDirectionalAnimationStructureInternal(
		int32 AnimationIndex,
		FPaper2DPlusDirectionalStructureResult& OutResult,
		bool bCheckProfileSettings) const;

	bool ImportFromJsonStringInternal(
		const FString& JsonString,
		TArray<FPaper2DPlusCharacterProfileJsonImportWarning>* OutWarnings);

	/** Shared migration: legacy parallel FFlipbookTagMapping arrays
	 *  (FlipbookNames_DEPRECATED + PaperZDSequences_DEPRECATED) → FFlipbookTagMappingEntry list.
	 *  Idempotent — mappings with a populated Entries list are skipped; the legacy arrays are
	 *  always emptied afterwards. Called from both PostLoad and ImportFromJsonString. */
	void MigrateTagMappingsToEntries();

	/** Shared migration: pre chain-start-flag numbered roots. Folds every positive legacy
	 *  RootNumber_DEPRECATED into bIsChainStart (the number carried no meaning beyond identity,
	 *  which the flag now provides) and zeroes the deprecated field. Idempotent; one Info log per
	 *  folded entry. Called from both PostLoad and ImportFromJsonString. Returns entries folded. */
	int32 MigrateRootNumbersToChainStarts();

	/** Deprecated-load-bridge anchor remap (TASK-61). Applies an old→new key-frame-index mapping
	 *  (OldToNew[OldIndex] = NewIndex, INDEX_NONE if removed) to every legacy event on Anim. Events
	 *  whose primary anchor frame was removed are moved into OutStashed when provided (ExcludeFlipbookFrame
	 *  stashes them on the excluded frame so restore can reattach); when OutStashed is null they are
	 *  dropped with a warning. Called from ExcludeFlipbookFrame / RestoreExcludedFlipbookFrame /
	 *  MoveFlipbookFrame (and the bulk restore) so all frame-index mutations remap events in lockstep. */
	void RemapFrameEventAnchors(FFlipbookProfileEntry& Anim, const TArray<int32>& OldToNew, int32 NumNewFrames,
	                            TArray<TObjectPtr<UPaper2DPlusFrameEventBase>>* OutStashed = nullptr);

	void RemapFrameCueAnchors(FFlipbookProfileEntry& Anim, const TArray<int32>& OldToNew, int32 NumNewFrames,
	                          TArray<TObjectPtr<UPaper2DPlusCueBase>>* OutStashed = nullptr);

	/** Shared frame-curve anchor remap (TASK-74), the curve-point parallel to RemapFrameEventAnchors.
	 *  Curve keys are pinned to key-frame indices (key Time = frame index), so any frame-index mutation
	 *  must remap them in the same pass. Applies an old->new key-frame-index mapping (OldToNew[OldIndex]
	 *  = NewIndex, INDEX_NONE if removed) to every key of every curve on Anim: surviving keys move to
	 *  their new index, keys on a removed frame are recorded in OutStashed (curveName -> value) when
	 *  provided (ExcludeFlipbookFrame stashes them on the excluded frame so restore can reattach), else
	 *  dropped. Keys are clamped in-bounds against NumNewFrames. Called from ExcludeFlipbookFrame /
	 *  RestoreExcludedFlipbookFrame / MoveFlipbookFrame (and the bulk restore) so curve points behave
	 *  exactly like frame-event anchors under exclude/restore/move. */
	void RemapFrameCurveAnchors(FFlipbookProfileEntry& Anim, const TArray<int32>& OldToNew, int32 NumNewFrames,
	                            TMap<FName, float>* OutStashed = nullptr);

	/** Shared migration: move legacy top-level FFlipbookProfileEntry fields loaded into the
	 *  *_DEPRECATED members into their sub-struct homes (Identity/CombatData/MotionData/EditorMeta),
	 *  then sync the per-frame parallel arrays to the frame count and normalize source-frame ordering.
	 *  Idempotent. Called from both PostLoad and ImportFromJsonString so a JSON import reaches the
	 *  same end state as a binary load. */
	void MigrateLoadedFlipbookSubStructs();

	/** Prune TagMapping FlipbookNames (and parallel PaperZDSequences) whose
	 *  flipbook is no longer present. Called after any operation that replaces
	 *  the Flipbooks array wholesale (e.g., JSON import). */
	void PruneOrphanedTagMappings();

	/** Migrate imported JSON payload to the current schema version when possible. */
	static bool MigrateSerializablePayloadToCurrentSchema(FCharacterProfileAssetSerializablePayload& InOutPayload);

	void RebuildFlipbookLookupCache() const;
	void RebuildNameLookupCache() const;

	struct FFlipbookOwnerCandidateCacheEntry
	{
		/** Unique logical owner rows, kept in authored iteration order. */
		TArray<int32> OwnerIndices;

		/** True when this key is referenced by at least one active directional slot. */
		bool bHasVariantReference = false;
	};

	/** Gather the shared owner view for one already-live base or active variant without loading assets. */
	bool GatherLogicalAnimationOwnerCandidates(
		UPaperFlipbook* Flipbook,
		FFlipbookOwnerCandidateCacheEntry& OutCandidates) const;

	/** True only for a cross-owner collision where a variant or direction-enabled candidate participates. */
	bool HasDirectionalLogicalOwnerCollision(
		const FFlipbookOwnerCandidateCacheEntry& Candidates) const;

	/** Load-free soft-path index retaining every logical owner candidate for a base or active variant. */
	mutable TMap<FSoftObjectPath, FFlipbookOwnerCandidateCacheEntry>
		FlipbookPathToOwnerCandidatesCache;

	/** Resident-object counterpart; weak keys never root animation assets. */
	mutable TMap<TWeakObjectPtr<UPaperFlipbook>, FFlipbookOwnerCandidateCacheEntry>
		ResidentFlipbookToOwnerCandidatesCache;

#if WITH_EDITORONLY_DATA
	/** Backing store for GetEditorContentRevision. Deliberately NOT a UPROPERTY: it describes an
	 *  editing session, not asset content, so it must never serialize, cook, or transact. */
	uint32 EditorContentRevision = 0;
#endif

	/** Whether the path and resident-object lookup indexes are synchronized with Flipbooks. */
	mutable bool bFlipbookLookupCacheValid = false;

	/** Number of Flipbooks entries when the flipbook cache was last built. */
	mutable int32 CachedFlipbookCount = 0;

	/** Cached map to accelerate name-to-flipbook data lookup. */
	mutable TMap<FString, int32> NameToFlipbookIndexCache;

	/** Whether NameToFlipbookIndexCache is synchronized with Flipbooks. */
	mutable bool bNameLookupCacheValid = false;

};
