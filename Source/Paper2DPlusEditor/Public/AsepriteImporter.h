// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Paper2DPlusTypes.h"

class UTexture2D;
class UPaperSprite;
class UPaperFlipbook;
class UPaper2DPlusCharacterProfileAsset;
struct FCharacterLayerAnimationMapping;
struct FCharacterLayer;

/** Parsed frame data from Aseprite file */
struct FAsepriteFrame
{
	int32 Duration = 100; // ms
	TArray<FColor> Pixels; // RGBA
	int32 Width = 0;
	int32 Height = 0;
};

/** Parsed tag (animation group) from Aseprite file */
struct FAsepriteTag
{
	FString Name;
	int32 FromFrame = 0;
	int32 ToFrame = 0;
	uint8 LoopDirection = 0; // 0=Forward, 1=Reverse, 2=PingPong
};

/** Parsed layer info */
struct FAsepriteLayer
{
	FString Name;
	bool bVisible = true;
	float Opacity = 1.0f;
	int32 LayerType = 0; // 0=Normal, 1=Group
	uint16 ChildLevel = 0;
};

/** Hitbox/socket data extracted from a named Aseprite layer */
struct FAsepriteHitboxLayer
{
	FString LayerName;
	EHitboxType HitboxType = EHitboxType::Attack; // Attack or Hurtbox (ignored for sockets)
	bool bIsSocket = false;
	FString SocketName;      // For socket layers only (parsed from "socket_Name")
	int32 LayerIndex = -1;   // Index in FAsepriteParsedData::Layers
};

/** Per-frame extracted hitbox/socket data from Aseprite layers */
struct FAsepriteExtractedFrameData
{
	TArray<FHitboxData> Hitboxes;
	TArray<FSocketData> Sockets;
};

/** Cel data stored during parsing for later compositing and hitbox extraction */
struct FAsepriteCelData
{
	int32 LayerIndex = 0;
	int32 X = 0;
	int32 Y = 0;
	uint8 Opacity = 255;
	uint16 CelType = 0;
	int32 CelWidth = 0;
	int32 CelHeight = 0;
	TArray<FColor> Pixels;
	TArray<uint8> IndexedPixels;
	TArray<FColor> PaletteSnapshot;
	uint16 LinkedFrame = 0; // For linked cels
};

/** Node in the reconstructed Aseprite layer hierarchy tree */
struct FAsepriteLayerNode
{
	int32 LayerIndex = -1;
	int32 ParentIndex = -1; // -1 = root level
	FString FullPath;       // e.g., "Armor/Chest"
	TArray<int32> ChildIndices; // Indices into the LayerHierarchy array
};

/** Complete parsed Aseprite file data */
struct FAsepriteParsedData
{
	int32 Width = 0;
	int32 Height = 0;
	int32 ColorDepth = 32;
	TArray<FAsepriteFrame> Frames;
	TArray<FAsepriteTag> Tags;
	TArray<FAsepriteLayer> Layers;
	TArray<FColor> Palette;
	bool bIsValid = false;

	/** Classified hitbox/socket layers detected by name convention */
	TArray<FAsepriteHitboxLayer> HitboxLayers;

	/** Per-frame hitbox/socket data extracted from hitbox layers. One entry per frame. */
	TArray<FAsepriteExtractedFrameData> ExtractedFrameData;

	/** Raw per-frame cel data preserved for per-layer compositing and preview */
	TArray<TArray<FAsepriteCelData>> AllFrameCels;

	/** Reconstructed layer hierarchy from ChildLevel + LayerType */
	TArray<FAsepriteLayerNode> LayerHierarchy;

};

/** Result of an Aseprite import operation */
struct FAsepriteImportResult
{
	bool bSuccess = false;
	FString ErrorMessage;
	UTexture2D* SpriteSheet = nullptr;
	TArray<UPaperSprite*> Sprites;
	TArray<UPaperFlipbook*> Flipbooks;

	/** Per-frame hitbox/socket data extracted from Aseprite hitbox layers */
	TArray<FAsepriteExtractedFrameData> FrameHitboxData;
};

/**
 * Handles importing Aseprite (.ase/.aseprite) files.
 * Parses the binary format and creates Paper2D assets.
 */
struct FAsepriteLayerImportSettings;

/**
 * How per-frame .ase durations are turned into a flipbook FPS + per-key-frame FrameRun.
 * GcdExact (default): FPS = 1000/gcd(durations), FrameRun[i] = durationMs[i]/gcd — reproduces the
 *   artist's per-frame timing EXACTLY (per-frame seconds == durationMs/1000); FPS may be non-round.
 * FixedFps: FPS = 1000/min(durationMs), FrameRun[i] = round(durationMs[i]/min) — round, engine-friendly
 *   FPS, but QUANTIZES any duration that isn't an integer multiple of the shortest frame (lossy).
 */
enum class EAsepriteTimingBase : uint8
{
	GcdExact,
	FixedFps
};

/**
 * How name-convention `.ase` hitboxes/sockets are written into a profile frame that ALREADY has data.
 * Merge    = APPEND the imported boxes/sockets to the frame's existing ones, skipping only boxes that are
 *            BYTE-IDENTICAL (exact type+geometry+combat scalars) to an existing one. A pure no-op re-import
 *            stays stable, but a re-imported NUDGED box is not byte-identical and so is appended as a
 *            near-duplicate — use Overwrite to REPLACE the frame's boxes. (No fuzzy tolerance by design.)
 * Overwrite= replace the frame's boxes/sockets wholesale with the imported set (used on first-create —
 *            there is nothing to lose).
 * Apply    = least-destructive / fill-empty: write ONLY on frames that currently have none of that kind
 *            (an empty Hitboxes array gets the imported boxes; an empty Sockets array gets the imported
 *            sockets), so existing author data is never touched.
 */
enum class EHitboxApplyPolicy : uint8
{
	Merge,
	Overwrite,
	Apply
};

class PAPER2DPLUSEDITOR_API FAsepriteImporter
{
public:
	/**
	 * Pure helper: turn a list of per-frame durations (ms) into a flipbook FPS + per-key-frame FrameRun
	 * (1:1 with DurationsMs). Single source of timing math for every importer flipbook builder.
	 * Zero/missing durations are treated as 100ms. Empty input yields DefaultFps + no runs.
	 */
	static void ComputeFrameTiming(const TArray<int32>& DurationsMs, EAsepriteTimingBase Base,
	                               float DefaultFps, float& OutFps, TArray<int32>& OutFrameRuns);

	/**
	 * Build the DISPLAY-order source-frame sequence for a tag, honoring LoopDirection (0=Forward, 1=Reverse,
	 * 2=PingPong). This is the single source of truth for "which source frame each emitted key-frame displays" —
	 * CreateFlipbooks uses it to emit a tag's key-frames, and TransferHitboxDataToProfile uses it to align imported
	 * hitboxes onto the key-frame that actually displays each source frame (so Reverse/PingPong land correctly).
	 * Forward [From..To]; Reverse [To..From]; PingPong [From..To] then [To-1..From+1]. From/To are swapped if
	 * From > To. Returns one entry per emitted key-frame.
	 */
	static TArray<int32> BuildTagFrameSequence(const FAsepriteTag& Tag);

	/**
	 * Deliver name-convention `.ase` hitboxes/sockets onto a CharacterProfile's flipbook entries, BY KEYFRAME
	 * INDEX, applying Policy. Pure data — no Slate/RHI — so it is fully worldless-testable.
	 *
	 * Tagged path (Tags.Num() > 0): each Tag is matched to its profile entry by the sanitized, prefix-stripped
	 * name (FAsepriteImporter's CreateFlipbooks naming, mirrored here) rather than by array index, and each
	 * matched entry is consumed once so two tags that sanitize to the same name map to distinct entries. For a
	 * matched entry, the DISPLAY-order sequence is computed via BuildTagFrameSequence(Tag); each emitted key-frame
	 * k receives FrameHitboxData[Seq[k]] (the source frame that key-frame actually displays — Reverse/PingPong
	 * correct), bounds-checked. CombatData.Frames is GROWN (never shrunk) to the entry flipbook's key-frame count.
	 * LAYOUT-DIVERGENCE GUARD: if the entry flipbook's key-frame count != Seq.Num() (e.g. a hand-edited/re-authored
	 * existing flipbook), the tag is skipped — *OutUndeliveredTags is incremented and a warning is logged. A tag
	 * with no matching flipbook increments *OutUndeliveredTags (when provided) and is skipped.
	 *
	 * No-tags path (Tags.Num() == 0): the single all-frames flipbook (named "<prefix>_All" → stripped "All", or —
	 * only when bAllowSoleEntryFallback — the sole entry when there is exactly one) receives FrameHitboxData[0..N]
	 * in keyframe order. New-profile callers pass bAllowSoleEntryFallback=true (the sole entry IS the freshly built
	 * _All flipbook); existing-profile callers pass false (require an exact "<prefix>_All" match, else count it
	 * undelivered rather than writing onto an arbitrary sole flipbook).
	 *
	 * @param OutUndeliveredTags Optional out-counter of tags that matched no flipbook (name divergence/layout).
	 * @param bAllowSoleEntryFallback No-tags path: accept the sole flipbook entry when there is no "<prefix>_All".
	 */
	static void TransferHitboxDataToProfile(
		UPaper2DPlusCharacterProfileAsset* Profile,
		const TArray<FAsepriteExtractedFrameData>& FrameHitboxData,
		const TArray<FAsepriteTag>& Tags,
		const FString& AssetPrefix,
		EHitboxApplyPolicy Policy,
		int32* OutUndeliveredTags = nullptr,
		bool bAllowSoleEntryFallback = true);

	/**
	 * Count the profile key-frames that ALREADY carry hitboxes or sockets and that TransferHitboxDataToProfile
	 * would write onto (a real conflict worth prompting on). Empty target frames are NOT conflicts. Same
	 * name-matching + BuildTagFrameSequence display-order mapping as TransferHitboxDataToProfile so the count
	 * reflects exactly what the transfer will touch. Does NOT early-return — counts every conflicting frame.
	 * Pure data, worldless-testable.
	 * @param bAllowSoleEntryFallback Mirror the transfer's no-tags sole-entry rule (see TransferHitboxDataToProfile).
	 */
	static int32 CountHitboxConflicts(
		const UPaper2DPlusCharacterProfileAsset* Profile,
		const TArray<FAsepriteExtractedFrameData>& FrameHitboxData,
		const TArray<FAsepriteTag>& Tags,
		const FString& AssetPrefix,
		bool bAllowSoleEntryFallback = true);

	/**
	 * Thin wrapper: true if CountHitboxConflicts(...) > 0. Same name-matching + keyframe mapping as
	 * TransferHitboxDataToProfile. Pure data, worldless-testable.
	 */
	static bool ProfileHasHitboxConflicts(
		const UPaper2DPlusCharacterProfileAsset* Profile,
		const TArray<FAsepriteExtractedFrameData>& FrameHitboxData,
		const TArray<FAsepriteTag>& Tags,
		const FString& AssetPrefix,
		bool bAllowSoleEntryFallback = true);

	/**
	 * Regenerate a profile's Flipbooks from a standard-import result — the "Create New profile" layered-import
	 * branch, INCLUDING re-imports onto an existing profile: Modify() → wipe → repopulate (prefix-stripped
	 * names) → name-convention hitbox transfer (Overwrite). The Modify() BEFORE the wipe is load-bearing
	 * (combo-graph plan U1/R12): it fires FCoreUObjectDelegates::OnObjectModified for the profile so open
	 * editor models reconcile — the caller's MarkPackageDirty() alone never fires it, which previously left
	 * every open panel rendering the pre-wipe data indefinitely. Importer-side disk mutation, NOT a
	 * transaction (the ImportFromJsonString recipe). Extracted seam so the headless suite can drive the
	 * regeneration directly — the full ImportAsLayeredAsset profile path needs ImportFile, whose dirty/
	 * registry calls are not /Temp-guarded and so not headless-safe. Package dirtying/registration stays
	 * with the caller.
	 */
	static void RegenerateProfileFromImportResult(
		UPaper2DPlusCharacterProfileAsset* Profile,
		const FAsepriteImportResult& ProfileResult,
		const TArray<FAsepriteTag>& Tags,
		const FString& AssetPrefix,
		bool bCreatedProfile);

	/**
	 * Parse an Aseprite file from disk.
	 * @param FilePath Path to the .ase/.aseprite file
	 * @param OutData Parsed file data
	 * @param OutError Error message if parsing fails
	 * @return True if parsing succeeded
	 */
	static bool ParseFile(const FString& FilePath, FAsepriteParsedData& OutData, FString& OutError);

	/**
	 * Parse Aseprite data from a byte buffer.
	 */
	static bool ParseBuffer(const TArray<uint8>& Buffer, FAsepriteParsedData& OutData, FString& OutError);

	/**
	 * Import an Aseprite file, creating sprites and flipbooks.
	 * @param FilePath Path to the .ase/.aseprite file
	 * @param OutputPath Content browser output path
	 * @param AssetPrefix Prefix for created asset names
	 * @return Import result with created assets
	 */
	static FAsepriteImportResult ImportFile(
		const FString& FilePath,
		const FString& OutputPath,
		const FString& AssetPrefix
	);

	/**
	 * Import an Aseprite file as a layered character asset.
	 * Creates per-layer textures/sprites, optionally a new profile, and
	 * a UPaper2DPlusCharacterLayerAsset with all layers populated.
	 * @param ParsedData Already-parsed Aseprite data
	 * @param PerLayerBuffers Pre-composited per-layer frame buffers
	 * @param Settings Import configuration from the dialog
	 * @return The created CharacterLayerAsset, or nullptr on failure/cancel
	 */
	static UObject* ImportAsLayeredAsset(
		FAsepriteParsedData& ParsedData,
		const TMap<int32, TArray<TArray<FColor>>>& PerLayerBuffers,
		const FAsepriteLayerImportSettings& Settings
	);

	/**
	 * Show an import dialog for Aseprite files.
	 */
	static void ShowImportDialog();

	/**
	 * Register context menu extensions for Aseprite files.
	 */
	static void RegisterMenus();
	static void UnregisterMenus();

	/**
	 * Composite each visual layer's cels independently into per-layer frame buffers.
	 * Skips hitbox layers and group layers (LayerType==1).
	 * @param Data Parsed Aseprite data with AllFrameCels populated
	 * @return Map of layer index to per-frame RGBA pixel buffers
	 */
	static TMap<int32, TArray<TArray<FColor>>> CompositePerLayer(const FAsepriteParsedData& Data);

	/**
	 * Create a sprite sheet texture from per-frame pixel buffers for a single layer.
	 * Uses the same grid layout logic as CreateSpriteSheetTexture.
	 * @param FrameBuffers Per-frame RGBA pixel buffers
	 * @param FrameWidth Width of each frame in pixels
	 * @param FrameHeight Height of each frame in pixels
	 * @param OutputPath Content browser output path
	 * @param AssetName Name for the created texture asset
	 * @return Created texture, or nullptr on failure
	 */
	static UTexture2D* CreatePerLayerSpriteSheetTexture(
		const TArray<TArray<FColor>>& FrameBuffers,
		int32 FrameWidth, int32 FrameHeight,
		const FString& OutputPath,
		const FString& AssetName
	);

	/**
	 * Create sprites from a per-layer sprite sheet texture, grouped by animation tags.
	 * @param SpriteSheet The per-layer sprite sheet texture
	 * @param Data Parsed Aseprite data (for tags and frame dimensions)
	 * @param OutputPath Content browser output path
	 * @param AssetPrefix Prefix for created sprite asset names
	 * @return Array of animation-to-sprites mappings
	 */
	static TArray<FCharacterLayerAnimationMapping> CreatePerLayerSprites(
		UTexture2D* SpriteSheet,
		const FAsepriteParsedData& Data,
		const FString& OutputPath,
		const FString& AssetPrefix
	);

	/**
	 * Create one UPaperSprite per frame from a packed sprite sheet using the shared FSpriteSheetGrid layout — the
	 * tag-free counterpart of CreatePerLayerSprites, for callers that have raw per-frame buffers (not a parsed `.ase`
	 * with tags), e.g. flat-character layer separation. The sheet must have been packed by
	 * CreatePerLayerSpriteSheetTexture with the SAME FrameCount/FrameW/FrameH so each sprite's source cell lines up.
	 * Returned array is FrameCount long (a slot may be null if its package/sprite couldn't be created).
	 */
	static TArray<UPaperSprite*> CreateSpritesFromSheet(
		UTexture2D* SpriteSheet, int32 FrameW, int32 FrameH, int32 FrameCount,
		const FString& OutputPath, const FString& AssetPrefix);

	/**
	 * PURE pairing helper (TASK-72), the testable seam (mirrors DeriveSlotsFromLayerNames). Given the ordered list of
	 * Aseprite layer names + the configured normal-map suffixes, return the base-layer-index -> normal-layer-index
	 * pairing. A normal layer is one whose name is "<baseName><suffix>" (CASE-INSENSITIVE) for some configured suffix
	 * AND for which a base art layer literally named "<baseName>" exists in the list. Suffixes are matched LONGEST-FIRST
	 * so "_normal" wins over "_n" when both are configured (e.g. "body_normal" pairs to "body", not "body_norma").
	 *
	 * @param LayerNames      One entry per source layer, indexed by source layer index (LayerNames[i] = layer i's name).
	 * @param NormalSuffixes  Configured suffixes (e.g. {"_normal","_n"}). Empty/whitespace suffixes are ignored.
	 * @param OutBaseToNormal base layer index -> its paired normal layer index. Only successful pairings are added.
	 * @param OutPairedNormalIndices The SET of layer indices that were consumed as a normal map (so callers can exclude
	 *        them from the visual layer set). A layer that matches a suffix but has NO base sibling is NOT added here —
	 *        it stays a normal visual layer, byte-identical to today.
	 *
	 * Byte-identical no-op guarantee: with no matching normal layers, OutBaseToNormal and OutPairedNormalIndices are
	 * both empty and the importer's existing path is untouched. A base layer is paired to AT MOST one normal layer
	 * (first match in ascending layer index); a normal layer pairs to AT MOST one base. Deterministic.
	 */
	static void DeriveNormalMapPairings(
		const TArray<FString>& LayerNames,
		const TArray<FString>& NormalSuffixes,
		TMap<int32, int32>& OutBaseToNormal,
		TSet<int32>& OutPairedNormalIndices);

	/**
	 * PURE (TASK-72): drop any (base -> normal) pairing whose BASE layer index is NOT in EnabledBaseIndices, and
	 * release that normal index out of PairedNormalIndices (so it imports as an ordinary visual layer instead of
	 * silently vanishing). Mirrors the import dialog's per-layer enablement: a normal map only folds into a base the
	 * user actually imports. Both containers are mutated in place. Worldless-testable seam.
	 */
	static void PruneNormalPairingsToEnabledBases(
		TMap<int32, int32>& BaseToNormal,
		TSet<int32>& PairedNormalIndices,
		const TSet<int32>& EnabledBaseIndices);

	/**
	 * TASK-72: composite a normal layer's per-frame buffers into a sheet (reusing CreatePerLayerSpriteSheetTexture so the
	 * grid/frame dims match the base sheet byte-for-byte) then re-tag it as a normal map: CompressionSettings =
	 * TC_Normalmap, SRGB = false, LODGroup = TEXTUREGROUP_WorldNormalMap, then UpdateResource() (deliberately NOT
	 * PostEditChange — see the .cpp rationale). Returns the retagged texture, or nullptr if the sheet couldn't be created.
	 * Public for worldless tests.
	 */
	static UTexture2D* CreateNormalMapSheetTexture(
		const TArray<TArray<FColor>>& FrameBuffers,
		int32 FrameWidth, int32 FrameHeight,
		const FString& OutputPath,
		const FString& AssetName);

	/**
	 * TASK-72: attach a generated normal-map texture to each non-null sprite in Sprites (AdditionalSourceTextures index
	 * 0 -> material AdditionalTexture0) and, when LitMaterial is non-null, assign it as the sprite's DefaultMaterial.
	 * Both members are protected on UPaperSprite, so this re-initialises each sprite via InitializeSprite(
	 * FSpriteAssetInitParameters) reconstructed from the sprite's own source region — which REPLACES the additional-
	 * texture list (so a reimport re-attach stays a single entry) and rebuilds render data internally (no explicit
	 * RebuildRenderData needed). Counts the sprites actually touched. Public for worldless tests.
	 */
	static void AttachNormalMapToSprites(
		const TArray<class UPaperSprite*>& Sprites,
		UTexture2D* NormalTexture,
		class UMaterialInterface* LitMaterial,
		int32& OutSpritesTouched);

private:
	/** Create a texture from flattened frame pixel data */
	static UTexture2D* CreateSpriteSheetTexture(
		const FAsepriteParsedData& Data,
		const FString& OutputPath,
		const FString& AssetName
	);

	/** Create sprites from the sprite sheet */
	static TArray<UPaperSprite*> CreateSprites(
		UTexture2D* SpriteSheet,
		const FAsepriteParsedData& Data,
		const FString& OutputPath,
		const FString& AssetPrefix
	);

	/** Create flipbooks from tags */
	static TArray<UPaperFlipbook*> CreateFlipbooks(
		const TArray<UPaperSprite*>& Sprites,
		const FAsepriteParsedData& Data,
		const FString& OutputPath,
		const FString& AssetPrefix
	);

	/** Flatten a cel onto a frame buffer with opacity */
	static void FlattenCel(
		TArray<FColor>& FrameBuffer,
		int32 FrameWidth, int32 FrameHeight,
		const TArray<FColor>& CelPixels,
		int32 CelWidth, int32 CelHeight,
		int32 CelX, int32 CelY,
		float Opacity
	);

	/** Decompress zlib data. ExpectedSize is the known uncompressed size (0 to auto-estimate). */
	static bool DecompressZlib(const uint8* CompressedData, int32 CompressedSize, TArray<uint8>& OutData, int32 ExpectedSize = 0);

	/** Convert indexed color pixels to RGBA using palette */
	static TArray<FColor> ConvertIndexedToRGBA(const uint8* IndexData, int32 PixelCount, const TArray<FColor>& Palette);

	/**
	 * Extract hitbox/socket data from classified hitbox layers.
	 * Populates Data.ExtractedFrameData with one entry per frame.
	 */
	static void ExtractHitboxData(FAsepriteParsedData& Data, const TArray<TArray<FAsepriteCelData>>& AllFrameCels);

	/**
	 * Resolve a cel to its pixel data, following linked cel references.
	 * Returns the cel with actual pixel data, or nullptr if unresolvable.
	 * OutPositionCel is set to the cel whose X/Y should be used for placement.
	 */
	static const FAsepriteCelData* ResolveCel(
		const TArray<TArray<FAsepriteCelData>>& AllFrameCels,
		int32 FrameIndex,
		int32 LayerIndex,
		const FAsepriteCelData** OutPositionCel = nullptr
	);

	/** Build layer hierarchy tree from ChildLevel values and propagate group visibility */
	static void BuildLayerHierarchy(FAsepriteParsedData& Data);
};
