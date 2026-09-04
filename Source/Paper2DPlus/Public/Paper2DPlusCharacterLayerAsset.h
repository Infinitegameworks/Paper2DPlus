// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/EngineVersionComparison.h"
#include "Engine/DataAsset.h"
#include "PaperSprite.h"
#include "Paper2DPlusTypes.h"
#include "UObject/ObjectSaveContext.h"
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 4)
#include "UObject/AssetRegistryTagsContext.h"
#endif
#include "FrameCues/Paper2DPlusFrameCue.h"
#include "Paper2DPlusLayerBakeTypes.h"
#include "Paper2DPlusAppearanceTypes.h"
#include "Paper2DPlusAuthoringProgressTags.h"
#include "Paper2DPlusCharacterLayerAsset.generated.h"

class UPaper2DPlusCharacterProfileAsset;
class UTexture2D;

USTRUCT(BlueprintType)
struct PAPER2DPLUS_API FCharacterLayerAnimationMapping
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layer")
	FString AnimationName;

#if WITH_EDITORONLY_DATA
	/** Canonical animation identity for v4 authoring. AnimationName remains the legacy display fallback. */
	UPROPERTY(EditAnywhere, Category = "Layer")
	TSoftObjectPtr<UPaperFlipbook> Flipbook;
#endif

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layer")
	TArray<TSoftObjectPtr<UPaperSprite>> Sprites;
};

/** How a visible Layer composes with lower active Layers. */
UENUM(BlueprintType)
enum class ECharacterLayerCompositionMode : uint8
{
	/** Normal additive stack: this layer draws over lower layers without hiding them. */
	Layer UMETA(DisplayName = "Layer"),
	/** Replacement stack: this Layer hides lower active Layers. */
	Replace UMETA(DisplayName = "Replace")
};

/** Runtime Customizable visual channel. Base is frame-synchronous and prepared into the one shared composite. */
UENUM(BlueprintType)
enum class ECharacterLayerRuntimeRenderChannel : uint8
{
	/** Default/schema-v4 behavior: this layer becomes part of the one prepared base primitive. */
	BaseComposite UMETA(DisplayName = "Base Composite (Frame Synchronous)"),
	/** Keep this layer as one bounded live sprite channel (for example a weapon or independently moving cape). */
	IndependentLive UMETA(DisplayName = "Independent Live Channel")
};

/**
 * Per-animation placement override for a layer (schema v2, consumed by U4's placement tool). OffsetPx is an
 * authored pixel-space nudge applied on top of the base sprite's own alignment offsets. Fixed publishing and the
 * Runtime Customizable gameplay projection both consume the same Paper2DPlusLayerDraw transform.
 */
USTRUCT(BlueprintType)
struct PAPER2DPLUS_API FCharacterLayerAnimationOffset
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layer Offset")
	FString AnimationName;

#if WITH_EDITORONLY_DATA
	/** Canonical animation identity for v4 authoring. */
	UPROPERTY(EditAnywhere, Category = "Layer Offset")
	TSoftObjectPtr<UPaperFlipbook> Flipbook;
#endif

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layer Offset")
	FVector2D OffsetPx = FVector2D::ZeroVector;
};

/** Optional mutual-exclusion constraint for otherwise ordinary Layers. Zero active members is valid. */
USTRUCT(BlueprintType)
struct PAPER2DPLUS_API FCharacterLayerExclusiveGroup
{
	GENERATED_BODY()

	/** Stable identity referenced by member Layers. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Exclusive Group", meta = (IgnoreForMemberInitializationTest))
	FGuid GroupId = FGuid::NewGuid();

	/** Designer-facing label only; selection and persistence always use GroupId. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Exclusive Group")
	FString DisplayName;
};

/** Complete appearance snapshot. Paint and gameplay order always come from the asset's Layers array. */
USTRUCT(BlueprintType)
struct PAPER2DPLUS_API FCharacterLayerAppearancePreset
{
	GENERATED_BODY()

	/** Stable preset identity. The committed descriptor copies ActiveLayerIds and never stores this ID. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Appearance Preset", meta = (IgnoreForMemberInitializationTest))
	FGuid PresetId = FGuid::NewGuid();

	/** Designer-facing label only. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Appearance Preset")
	FString DisplayName;

	/** Complete selected Layer set. Applying this preset deactivates every Layer not present. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Appearance Preset")
	TArray<FGuid> ActiveLayerIds;
};

USTRUCT(BlueprintType)
struct PAPER2DPLUS_API FCharacterLayer
{
	GENERATED_BODY()

	/** Cooked stable selection identity. Display names never participate in committed appearance state. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Layer", meta = (IgnoreForMemberInitializationTest))
	FGuid LayerId = FGuid::NewGuid();

	/** Optional cooked Exclusive Group membership. Invalid means this Layer stacks independently. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layer|Appearance")
	FGuid ExclusiveGroupId;

#if WITH_EDITORONLY_DATA
	/** Organization only: changing GroupId never changes visual order or a source digest. */
	UPROPERTY(EditAnywhere, Category = "Layer")
	FGuid GroupId;
#endif

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layer")
	FString LayerName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layer")
	ECharacterLayerCompositionMode CompositionMode = ECharacterLayerCompositionMode::Layer;

	/**
	 * Cooked U30 authoring declaration. At most two layers on an asset may be IndependentLive; every other layer
	 * remains in the frame-synchronous base composite. The default preserves all schema-v4 and legacy assets.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layer|Runtime Rendering", AdvancedDisplay)
	ECharacterLayerRuntimeRenderChannel RuntimeRenderChannel = ECharacterLayerRuntimeRenderChannel::BaseComposite;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layer")
	TSoftObjectPtr<UTexture2D> SourceTexture;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layer")
	TArray<FCharacterLayerAnimationMapping> AnimationSprites;

	/** Default per-layer placement nudge in pixels (schema v2; U4 wires the runtime/editor consumers).
	 *  Zero = byte-identical legacy placement. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layer Offset")
	FVector2D DefaultOffsetPx = FVector2D::ZeroVector;

	/** Per-animation overrides of DefaultOffsetPx (schema v2; U4 consumer). Empty = use DefaultOffsetPx. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layer Offset")
	TArray<FCharacterLayerAnimationOffset> AnimationOffsets;

#if WITH_EDITORONLY_DATA
	/** Layer-local Attack/Hurt/socket/Cue source data. Runtime Profile arrays are compiled output, not input. */
	UPROPERTY(EditAnywhere, Category = "Layer Gameplay")
	TArray<FCharacterLayerAuthoredAnimationData> AuthoredAnimations;
#endif

	/**
	 * Cooked Runtime Customizable projection of AuthoredAnimations. Fixed/Baked compiles gameplay into the
	 * Character Profile instead; Legacy uses variant AnimationOverrides. Rebuilt at every editor mutation/save,
	 * then read directly in packaged games so designer-authored layer boxes, sockets, and Cues survive cooking.
	 */
	UPROPERTY()
	TArray<FCharacterLayerAuthoredAnimationData> CookedGameplayAnimations;

	/** Audit F4: bAllowSyncLoad=false makes this LOAD-FREE — the per-frame playback path passes false and
	 *  reads the already-loaded sprite (.Get(), null if not warmed) so it never stalls the game thread on a
	 *  soft-ref load. Warm with WarmAnimationSprites on the animation-change path (allowed to sync-load). */
	UPaperSprite* GetSpriteForFrame(const FString& AnimName, int32 FrameIndex, bool bAllowSyncLoad = true) const;
	/** Audit F4: synchronously resolve every soft sprite of AnimName ONCE (e.g. on flipbook change), so the
	 *  per-frame GetSpriteForFrame(..., bAllowSyncLoad=false) is a load-free .Get(). The caller MUST keep the
	 *  appended hard refs rooted (a TSoftObjectPtr does NOT prevent GC) — otherwise a GC between warm and the
	 *  load-free read would collect the not-yet-displayed sprites and the layer renders blank (F4 re-review). */
	void WarmAnimationSprites(const FString& AnimName, TArray<TObjectPtr<UPaperSprite>>& OutWarmedSprites) const;
	const FCharacterLayerAnimationMapping* FindAnimationMapping(const FString& AnimName) const;
};

UENUM(BlueprintType)
enum class ECharacterLayerValidationSeverity : uint8
{
	Info,
	Warning,
	Error
};

USTRUCT(BlueprintType)
struct PAPER2DPLUS_API FCharacterLayerValidationIssue
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Validation")
	ECharacterLayerValidationSeverity Severity = ECharacterLayerValidationSeverity::Info;

	UPROPERTY(BlueprintReadOnly, Category = "Validation")
	FString Message;

	UPROPERTY(BlueprintReadOnly, Category = "Validation")
	FString LayerName;
};

/**
 * Import bookkeeping for ONE contributing .ase source of a Layer Profile (TASK-189 multi-source
 * batches: several .ase files feed one asset). Editor-only data — never cooked. The legacy
 * single-source fields on the asset (SourceAseFilePath / ImportedAseContentHash / ImportAssetPrefix /
 * ImportDisabledTagNames) remain the reflected, Blueprint-visible MIRROR of element 0 of
 * ImportedAseSources; UpsertAseSourceContext + SyncLegacyAseSourceMirror keep them in lockstep.
 */
USTRUCT()
struct PAPER2DPLUS_API FAsepriteSourceContext
{
	GENERATED_BODY()

	/** Stored form of the source path (FAsepriteImporter::MakeStoredAsePath — project-relative
	 *  under the project, absolute otherwise). Resolve via ResolveStoredAsePath only. */
	UPROPERTY(VisibleAnywhere, Category = "Import")
	FString StoredSourcePath;

	/** Whole-file MD5 as of this source's last successful import (the watcher's echo/offline gate). */
	UPROPERTY(VisibleAnywhere, Category = "Import")
	FString ContentHash;

	/** Asset prefix this source's generated assets were named with. */
	UPROPERTY(VisibleAnywhere, Category = "Import")
	FString AssetPrefix;

	/** Tag NAMES de-selected at this source's import (names, stable across tag reordering). */
	UPROPERTY(VisibleAnywhere, Category = "Import")
	TArray<FString> DisabledTagNames;

	/**
	 * Per-layer content hashes (layer FULL hierarchy path -> MD5 of that layer's OWN composited
	 * pixels across all frames). The keys double as this source's contributed-layer record; the
	 * values feed the structural-diff rename pairing (unambiguous + pixel-identical only).
	 */
	UPROPERTY(VisibleAnywhere, Category = "Import")
	TMap<FString, FString> LayerContentHashes;

	/**
	 * Per-tag content hashes (tag name -> MD5 of the tag's composited full frames, durations
	 * excluded). The keys double as this source's contributed-animation record.
	 */
	UPROPERTY(VisibleAnywhere, Category = "Import")
	TMap<FString, FString> TagContentHashes;

	/**
	 * Whole-composite hash: every composited frame plus frame count and canvas dims — the composited
	 * sheet's incremental write gate. Empty (an import that predates the gate) always reads as
	 * "write", never as "skip".
	 */
	UPROPERTY(VisibleAnywhere, Category = "Import")
	FString CompositeContentHash;

	/**
	 * Paired normal-map hashes (BASE layer full path -> MD5 of the paired NORMAL layer's own
	 * composited pixels). The per-layer loop skips paired normals before the LayerContentHashes
	 * stamp, so without this map a `_Sheet_N` has nothing to compare against and would either
	 * always rebuild or silently ship stale.
	 */
	UPROPERTY(VisibleAnywhere, Category = "Import")
	TMap<FString, FString> NormalLayerContentHashes;

	/**
	 * Per-tag STRUCTURE hashes (tag name -> MD5 over authored range + per-frame durations +
	 * keyframe sprite names) — the flipbook write gate. Deliberately a SIBLING of TagContentHashes:
	 * that one hashes pixels and excludes durations for rename pairing, which is wrong in both
	 * directions for flipbooks.
	 */
	UPROPERTY(VisibleAnywhere, Category = "Import")
	TMap<FString, FString> TagStructureHashes;

	/** The stamped grid (frame count + canvas dims). A change moves every cell rect and UV, so
	 *  every incremental gate short-circuits to "write" when these disagree; 0 = unstamped = write. */
	UPROPERTY(VisibleAnywhere, Category = "Import")
	int32 StampedFrameCount = 0;

	UPROPERTY(VisibleAnywhere, Category = "Import")
	int32 StampedCanvasWidth = 0;

	UPROPERTY(VisibleAnywhere, Category = "Import")
	int32 StampedCanvasHeight = 0;
};

UCLASS(BlueprintType, NotBlueprintable, meta=(DisplayName="Character Layer Asset"))
class PAPER2DPLUS_API UPaper2DPlusCharacterLayerAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPaper2DPlusCharacterLayerAsset();

	virtual FPrimaryAssetId GetPrimaryAssetId() const override;

#if WITH_EDITOR
	/** Stable passive relationship metadata consumed by ProfileRelationshipService without loading. */
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 4)
	virtual void GetAssetRegistryTags(FAssetRegistryTagsContext Context) const override
	{
		Super::GetAssetRegistryTags(Context);
		Context.AddTag(FAssetRegistryTag(
			TEXT("Paper2DPlus.CharacterProfile"),
			BaseProfile.IsNull() ? FString(TEXT("None")) : BaseProfile.ToSoftObjectPath().ToString(),
			FAssetRegistryTag::TT_Hidden));
		// Source-.ase identity as registry tags so the TextureWatcherService can map every layer asset
		// to its source file WITHOUT loading it (load happens lazily, only when the file changes).
		// Multi-source assets emit newline-joined lists; legacy single-source assets emit the bare value.
		Context.AddTag(FAssetRegistryTag(
			TEXT("Paper2DPlus.SourceAseFile"), BuildAseSourceFileTagValue(), FAssetRegistryTag::TT_Hidden));
		Context.AddTag(FAssetRegistryTag(
			TEXT("Paper2DPlus.SourceAseHash"), BuildAseSourceHashTagValue(), FAssetRegistryTag::TT_Hidden));
		int32 ProgressDone = 0;
		int32 ProgressTotal = 0;
		Paper2DPlusAuthoringProgress::ComputeChecklistProgress(
			EditorCompletionFlags, ProgressDone, ProgressTotal);
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
			TEXT("Paper2DPlus.CharacterProfile"),
			BaseProfile.IsNull() ? FString(TEXT("None")) : BaseProfile.ToSoftObjectPath().ToString(),
			FAssetRegistryTag::TT_Hidden);
		// Source-.ase identity as registry tags so the TextureWatcherService can map every layer asset
		// to its source file WITHOUT loading it (load happens lazily, only when the file changes).
		// Multi-source assets emit newline-joined lists; legacy single-source assets emit the bare value.
		OutTags.Emplace(
			TEXT("Paper2DPlus.SourceAseFile"), BuildAseSourceFileTagValue(), FAssetRegistryTag::TT_Hidden);
		OutTags.Emplace(
			TEXT("Paper2DPlus.SourceAseHash"), BuildAseSourceHashTagValue(), FAssetRegistryTag::TT_Hidden);
		int32 ProgressDone = 0;
		int32 ProgressTotal = 0;
		Paper2DPlusAuthoringProgress::ComputeChecklistProgress(
			EditorCompletionFlags, ProgressDone, ProgressTotal);
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

	//~ Begin UObject interface
	virtual void PostInitProperties() override;
	virtual void PostLoad() override;
	virtual void PostDuplicate(EDuplicateMode::Type DuplicateMode) override;
	virtual void PostRename(UObject* OldOuter, const FName OldName) override;
#if WITH_EDITOR
	virtual void PreSave(FObjectPreSaveContext SaveContext) override;
	/** See GetEditorContentRevision — Modify() is the hook that catches custom-panel array edits. */
	virtual bool Modify(bool bAlwaysMarkDirty = true) override;
	virtual void PostEditUndo() override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;

	/** Refresh the cooked Runtime Customizable gameplay projection without saving the asset. */
	void RebuildCookedGameplayData();
#endif
	//~ End UObject interface

	/**
	 * Monotonic counter over in-place EDITOR mutations, so a consumer memoizing a derived answer per
	 * Layer asset can tell "same asset" from "same asset, edited since". The Frame Cue detection tick
	 * rate memoizes its per-Layer cue scan on FObjectKey alone, which cannot see a cue authored into an
	 * already-equipped Layer. Always 0 in cooked builds.
	 */
	uint32 GetEditorContentRevision() const
	{
#if WITH_EDITORONLY_DATA
		return EditorContentRevision;
#else
		return 0;
#endif
	}

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Character Layers")
	FString DisplayName;

#if WITH_EDITORONLY_DATA
	/** Manual editor workflow progress. Bits are owned by the shared Profile Completion panel. */
	UPROPERTY()
	int32 EditorCompletionFlags = 0;

	/** Backing store for GetEditorContentRevision. Deliberately NOT a UPROPERTY: it describes an
	 *  editing session, not asset content, so it must never serialize, cook, or transact. */
	uint32 EditorContentRevision = 0;
#endif

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Character Layers")
	TSoftObjectPtr<UPaper2DPlusCharacterProfileAsset> BaseProfile;

	/** Cooked delivery path. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Character Layers|Delivery")
	ECharacterLayerUsageMode UsageMode = ECharacterLayerUsageMode::RuntimeCustomizable;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Character Layers")
	TArray<FCharacterLayer> Layers;

	/** Optional mutual-exclusion constraints. They classify no Layer beyond the explicit membership ID. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Character Layers|Appearance")
	TArray<FCharacterLayerExclusiveGroup> ExclusiveGroups;

	/** Complete selection snapshots. Presets never contain paint/gameplay order. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Character Layers|Appearance")
	TArray<FCharacterLayerAppearancePreset> AppearancePresets;

	/** Required preset consumed by both Fixed/Baked publish and Runtime Customizable initialization. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Character Layers|Appearance")
	FGuid DefaultAppearancePresetId;

#if WITH_EDITORONLY_DATA
	/** Stable identity for this asset's exclusive canonical output set. Possession never implies attachment. */
	UPROPERTY(VisibleAnywhere, Category = "Character Layers|Bake")
	FGuid BakeSetId;

	UPROPERTY(VisibleAnywhere, Category = "Character Layers|Bake")
	ECharacterLayerBakeAttachmentState BakeAttachmentState = ECharacterLayerBakeAttachmentState::Unclaimed;

	/** One-level organization only. Layers point at a GroupId; array order remains visual/bake order. */
	UPROPERTY(EditAnywhere, Category = "Character Layers|Organization")
	TArray<FCharacterLayerGroupInfo> LayerGroups;

	/** Original canonical flipbook/sprite topology. Only explicit Rebase Registration may replace it. */
	UPROPERTY()
	TArray<FCharacterLayerAnimationRegistration> AnimationRegistration;

	/** Written last by the sole bake coordinator after outputs and ownership verify. */
	UPROPERTY()
	FCharacterLayerBakeManifest BakeManifest;

	/** Latest explicit publish command. Evidence only; the manifest remains the consistency contract. */
	UPROPERTY()
	FCharacterLayerBakeOperationRecord LastBakeOperation;
#endif

	/**
	 * Disk path of the .ase file used to create this asset. Empty for assets created before auto-reimport.
	 * Stored PROJECT-RELATIVE when the file lives under the project directory (so the asset resolves on any
	 * machine that syncs the project — the artist-commits-the-.ase workflow), absolute otherwise. Resolve
	 * through FAsepriteImporter::ResolveStoredAsePath, never by raw ConvertRelativePathToFull (which resolves
	 * relative paths against the engine binaries directory, not the project).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Import")
	FString SourceAseFilePath;

	/**
	 * MD5 of the .ase file content as of the last successful import/auto-reimport. Lets the watcher detect
	 * edits that landed while the editor was CLOSED (e.g. a git pull of the artist's commit): at startup it
	 * compares this against the file on disk and auto-reimports on mismatch. Empty = never stamped (assets
	 * imported before this field; they reconcile only on live change events until their next reimport).
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Import")
	FString ImportedAseContentHash;

	/**
	 * Import context captured at import time so the watcher's auto-reimport can re-run the FULL import
	 * pipeline (sheet texture, per-frame sprites, per-tag flipbooks, additive profile delivery, layer
	 * refresh) against the same assets when the .ase changes — an Aseprite save then reflects everywhere,
	 * not just in this asset's per-layer textures. Empty ImportAssetPrefix = context never captured
	 * (pre-2026-08 asset): the watcher falls back to the layer-only diff reimport.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Import")
	FString ImportOutputPath;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Import")
	FString ImportAssetPrefix;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Import")
	bool bImportOrganizeIntoSubfolders = false;

	/** Tag NAMES the user de-selected at import (names, not indices — stable across tag reordering). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Import")
	TArray<FString> ImportDisabledTagNames;

#if WITH_EDITORONLY_DATA
	/**
	 * All contributing .ase sources (TASK-189 multi-source batches). Element 0 is the PRIMARY
	 * source, mirrored into the legacy single-source fields above (kept for Blueprint/API
	 * compatibility and the watcher's tags-absent fallback). Legacy assets keep an EMPTY array
	 * until their next import stamps it — lazy materialization in UpsertAseSourceContext, no
	 * PostLoad migration.
	 */
	UPROPERTY(VisibleAnywhere, Category = "Import", AdvancedDisplay)
	TArray<FAsepriteSourceContext> ImportedAseSources;

	/** Case-insensitive lookup by stored path. Null when the path is not a recorded source. */
	const FAsepriteSourceContext* FindAseSourceContext(const FString& StoredPath) const;
	FAsepriteSourceContext* FindAseSourceContextMutable(const FString& StoredPath);

	/**
	 * Find-or-add the context entry for StoredPath. On a LEGACY asset (empty array, non-empty
	 * SourceAseFilePath) the legacy fields are first materialized into element 0 so the old
	 * source's record is never lost — which means callers must NOT overwrite SourceAseFilePath
	 * before calling this, or the materialization records the new path as the old source and the
	 * previous source's record is destroyed. Fill the returned entry, then call
	 * SyncLegacyAseSourceMirror() so the reflected single-source fields track element 0.
	 *
	 * The returned reference is invalidated by any later ImportedAseSources mutation (including
	 * another UpsertAseSourceContext) — never hold two of them at once.
	 */
	FAsepriteSourceContext& UpsertAseSourceContext(const FString& StoredPath);

	/** Copy element 0 of ImportedAseSources into the legacy single-source mirror fields. */
	void SyncLegacyAseSourceMirror();

	/** Joined multi-value bodies for the two source registry tags (legacy single value when the
	 *  array is empty). Empty per-source values are preserved so path/hash index parity holds. */
	FString BuildAseSourceFileTagValue() const;
	FString BuildAseSourceHashTagValue() const;
#endif

	/**
	 * Registry-tag multi-value delimiter (TASK-189) is a NEWLINE — the one separator that cannot
	 * appear in a Windows path and is vanishingly rare elsewhere. It is not assumed impossible:
	 * a newline is legal in a filename on Linux/macOS, so it is REJECTED at every stamp and
	 * emission site instead (a mis-split would bind a source to another source's hash).
	 *
	 * ParseAseSourceTagList splits WITHOUT culling empties, so the path list and the hash list keep
	 * index parity; no delimiter -> one element (the legacy single-source parse). NOTE: an empty
	 * input yields ZERO elements, so a single source whose hash was never stamped produces one path
	 * and zero hashes — consumers must normalize that one case rather than treating it as a desync.
	 */
	static void ParseAseSourceTagList(const FString& TagValue, TArray<FString>& OutValues);

	/**
	 * Explicit opt-in for Runtime Customizable recolor composition. Enable only when every assigned live
	 * RecolorBaseMaterial implements Paper2DPlus's documented luminance→PaletteLUT formula and leaves sprite alpha
	 * unchanged. False keeps any recolored appearance on exact live layers; arbitrary project materials are never
	 * approximated silently. Cooked, default false, and additive to schema v4.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Character Layers|Runtime Rendering", AdvancedDisplay)
	bool bRuntimeCompositeRecolorContractVerified = false;

	/** Current generic Layer asset schema. */
	UPROPERTY()
	uint32 LayerSchemaVersion = 5;

	static constexpr uint32 CurrentLayerSchemaVersion = 5;
	static constexpr uint32 GenericLayerSchemaVersion = CurrentLayerSchemaVersion;

	// --- Query API ---

	UFUNCTION(BlueprintCallable, Category = "Paper2D+|Layers")
	/** Audit F4: bAllowSyncLoad=false = load-free per-frame read (already-warmed sprite or null). Pair with
	 *  WarmLayerSprites on the animation-change path. */
	UPaperSprite* GetLayerSpriteForFrame(const FString& LayerName, const FString& AnimationName, int32 FrameIndex, bool bAllowSyncLoad = true) const;
	/** Audit F4: synchronously warm every layer's sprites for AnimationName ONCE (flipbook-change path). The
	 *  appended hard refs MUST be kept rooted by the caller (e.g. the render component's transient
	 *  WarmedLayerSprites UPROPERTY) so a GC can't collect them out from under the load-free per-frame reads. */
	void WarmLayerSprites(const FString& AnimationName, TArray<TObjectPtr<UPaperSprite>>& OutWarmedSprites) const;

	UFUNCTION(BlueprintCallable, Category = "Paper2D+|Layers")
	TArray<FString> GetAllLayerNames() const;

	UFUNCTION(BlueprintCallable, Category = "Paper2D+|Layers")
	int32 GetLayerCount() const { return Layers.Num(); }

	// --- Helpers ---

	const FCharacterLayer* GetLayerByName(const FString& LayerName) const;
	FCharacterLayer* GetLayerByNameMutable(const FString& LayerName);
	/** Cook-safe stable-ID lookup used by generic runtime appearance. */
	const FCharacterLayer* FindLayerById(const FGuid& LayerId) const
	{
		return LayerId.IsValid() ? Layers.FindByPredicate([&LayerId](const FCharacterLayer& Layer)
		{
			return Layer.LayerId == LayerId;
		}) : nullptr;
	}
	const FCharacterLayerExclusiveGroup* GetExclusiveGroupById(const FGuid& GroupId) const
	{
		return GroupId.IsValid() ? ExclusiveGroups.FindByPredicate([&GroupId](const FCharacterLayerExclusiveGroup& Group)
		{
			return Group.GroupId == GroupId;
		}) : nullptr;
	}
	const FCharacterLayerAppearancePreset* GetAppearancePresetById(const FGuid& PresetId) const
	{
		return PresetId.IsValid() ? AppearancePresets.FindByPredicate([&PresetId](const FCharacterLayerAppearancePreset& Preset)
		{
			return Preset.PresetId == PresetId;
		}) : nullptr;
	}
#if WITH_EDITOR
	const FCharacterLayer* GetLayerById(const FGuid& LayerId) const;
	FCharacterLayer* GetLayerByIdMutable(const FGuid& LayerId);

	/** Idempotently seeds v4 identities/inclusion and uniquely resolvable canonical animation references. */
	bool EnsureLayerAuthoringIdentity();

	/** Semantic source digest: includes ordered/included source payload and excludes groups/preview/editor layout. */
	FString ComputeLayerSourceDigest() const;

#endif

	/** Resolve the active generic Layer IDs for the current animation. */
	TArray<FString> ResolveVisibleLayers(
		const FPaper2DPlusAppearanceDescriptor& Appearance,
		const FString& CurrentAnimationName) const;

	/** Normalize an already-resolved set into the asset's sole global Layer order. */
	void SortVisibleLayersForEffectivePaintOrder(TArray<FString>& InOutVisibleLayers) const;

	// --- Validation ---

	/** Every native validation row: the appearance-source contract plus ValidateSpriteReferences(). */
	TArray<FCharacterLayerValidationIssue> ValidateLayerAsset() const;

	/**
	 * Every sprite reference recorded on every layer must resolve to an asset that exists - resident in
	 * memory or known to the Asset Registry - checked WITHOUT loading anything. One Error row per layer
	 * that references missing frames (LayerName set, the missing count and the first missing path in the
	 * message). The import funnels that record these references never checked them, so a layer whose
	 * sprites were never written (an engine-invalid character in its name; a per-source reimport of a row
	 * two sources share) rendered nothing with no error anywhere - only LogCoreRedirects noise from every
	 * registry touch of the dead path, which failed unrelated automation tests (2026-09-04).
	 * A reference the registry cannot answer yet (still scanning) is not counted: a scan in progress is
	 * not evidence of absence. An empty entry is an empty frame, not a reference.
	 */
	TArray<FCharacterLayerValidationIssue> ValidateSpriteReferences() const;

};
