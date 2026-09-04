// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AsepriteIncrementalWrite.h" // TASK-192: verdict/grid currency shared with the gate seam
#include "Paper2DPlusTypes.h"

class UTexture2D;
class UPaperSprite;
class UPaperFlipbook;
class UPaper2DPlusCharacterProfileAsset;
class UPaper2DPlusCharacterLayerAsset;
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

/** TASK-192 U7: one per-tag flipbook outcome — enough for the profile append to reconcile its row
 *  by soft path even when the flipbook itself was skipped and never loaded. */
struct FAseTagFlipbookOutcome
{
	FString TagName;
	FString FlipbookAssetName;   // sanitized "<prefix>_<Tag>" (or "<prefix>_All")
	FString FlipbookPackagePath; // long package name
	UPaperFlipbook* Flipbook = nullptr; // resident when written/created; null when skipped
	bool bSkipped = false;
};

/**
 * TASK-192: everything the incremental gates need inside ImportFile — the composited sheet, the
 * flat sprites, and the per-tag flipbooks — plus the OUT stamps the caller persists on success.
 * A null context is the legacy full-write behavior.
 */
struct FAsepriteIncrementalImportContext
{
	const struct FAsepriteSourceContext* SourceContext = nullptr;
	bool bForceFullReimport = false;

	// The stamped and current grid triples (declared in AsepriteIncrementalWrite.h, included above).
	FAseStampedGrid StampedGrid;
	FAseStampedGrid CurrentGrid;

	// OUT: stamps for the caller to persist after a successful import.
	FString NewCompositeContentHash;              // whole-composite (U6)
	TMap<FString, FString> NewTagStructureHashes; // per tag name; "__AllFrames__" for the tagless flipbook (U7)
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

	/** TASK-192 U7: per-tag flipbook outcomes when an incremental context drove the import
	 *  (Flipbooks above then holds only the WRITTEN ones). Empty on legacy full imports. */
	TArray<FAseTagFlipbookOutcome> TagFlipbookOutcomes;
};

/** Per-layer composited frame buffers, keyed by layer index in FAsepriteParsedData::Layers.
 *  A named alias because SLATE_ARGUMENT cannot take a type containing a comma. */
using FPerLayerBufferMap = TMap<int32, TArray<TArray<FColor>>>;

/** Phase buckets for FAsepriteImportCostReport timings (TASK-192 U2). */
enum class EAsepriteImportCostPhase : uint8
{
	Parse,        // ParseFile / ParseBuffer — includes flattening the composited Frames[].Pixels
	Composite,    // CompositePerLayer — the per-layer pixel buffers
	TextureBuild, // a sheet writer end to end: previous-build wait, mip/source fill, UpdateResource
	Sprites,      // sprite creation / InitializeSprite passes (flat + per-layer + normal attach)
	Flipbooks,    // flipbook creation and rewrite
	Profile,      // profile regenerate/append + hitbox delivery
	Count
};

/**
 * One import's measured cost (TASK-192 U2). Counters pair written/skipped per generated asset
 * class — skipped stays 0 until an incremental-narrowing unit lands, which is exactly the
 * pre-narrowing baseline the incremental tests pin. Phase seconds split the modal stall into the
 * terms that could dominate (is it the DDC encode or the composite?). Phases never overlap, but
 * they do not cover every instruction either, so their sum is <= TotalSeconds. Texture DDC builds
 * that continue asynchronously after the import returns are NOT included: this measures the
 * blocking stall the user feels, not the total background cost.
 */
struct FAsepriteImportCostReport
{
	int32 SheetsWritten = 0;
	int32 SheetsSkipped = 0;
	int32 SpritesWritten = 0;
	int32 SpritesSkipped = 0;
	int32 FlipbooksWritten = 0;
	int32 FlipbooksSkipped = 0;
	int32 ProfileEntriesWritten = 0;
	int32 ProfileEntriesSkipped = 0;
	/** Unique packages MarkPackageDirty'd while the scope was active (repeat marks dedupe; a
	 *  package already dirty before the scope still counts — the engine event fires every mark). */
	int32 PackagesDirtied = 0;

	/** One line per incremental write/skip decision ("<class> <package>: <verdict> — <reason>"),
	 *  recorded by FAsepriteIncrementalWrite so the audit report (U8) can name every decision. */
	TArray<FString> DecisionLines;

	double PhaseSeconds[static_cast<int32>(EAsepriteImportCostPhase::Count)] = { 0.0 };
	double TotalSeconds = 0.0;

	double GetPhaseSeconds(EAsepriteImportCostPhase Phase) const
	{
		return PhaseSeconds[static_cast<int32>(Phase)];
	}

	/** The one-line log form: every counter pair, the dirty count, and the phase split. */
	FString ToSummaryString() const;
};

/**
 * Installs a cost report for the duration of one import (TASK-192 U2), RAII. The OUTERMOST scope
 * owns the report: nested scopes (ImportAsLayeredAsset inside the watcher's scope) contribute to
 * the active report and own nothing, so every entry point can open one unconditionally. The owner
 * counts unique dirtied packages through UPackage::PackageMarkedDirtyEvent and logs the one-line
 * summary on destruction. Game-thread only, like the import itself.
 */
class PAPER2DPLUSEDITOR_API FAsepriteImportCostScope
{
public:
	FAsepriteImportCostScope();
	~FAsepriteImportCostScope();

	FAsepriteImportCostScope(const FAsepriteImportCostScope&) = delete;
	FAsepriteImportCostScope& operator=(const FAsepriteImportCostScope&) = delete;

	/** The report this scope observes: the owner's own report, or the active one for a nested scope. */
	const FAsepriteImportCostReport& GetReport() const;

	/** The active report, or null when no scope is open. Write sites accumulate through this. */
	static FAsepriteImportCostReport* GetActive();

private:
	FAsepriteImportCostReport Report;
	bool bOwner = false;
	double StartSeconds = 0.0;
	FDelegateHandle PackageMarkedDirtyHandle;
	TSet<FName> DirtiedPackages;
};

/** RAII phase timer: adds its lifetime to the active report's phase bucket. No-op with no scope. */
class PAPER2DPLUSEDITOR_API FAsepriteImportCostPhaseTimer
{
public:
	explicit FAsepriteImportCostPhaseTimer(EAsepriteImportCostPhase InPhase);
	~FAsepriteImportCostPhaseTimer();

	FAsepriteImportCostPhaseTimer(const FAsepriteImportCostPhaseTimer&) = delete;
	FAsepriteImportCostPhaseTimer& operator=(const FAsepriteImportCostPhaseTimer&) = delete;

private:
	EAsepriteImportCostPhase Phase;
	double StartSeconds = 0.0;
};

/** Write-site accumulators for the active cost report. Each is a no-op when no scope is open, so
 *  production sites call them unconditionally (TASK-192 U2). The bWritten=false halves are wired
 *  by the incremental-narrowing units; nothing passes false until a skip gate exists. */
namespace AsepriteImportCost
{
	PAPER2DPLUSEDITOR_API void AddSheets(int32 Count, bool bWritten);
	PAPER2DPLUSEDITOR_API void AddSprites(int32 Count, bool bWritten);
	PAPER2DPLUSEDITOR_API void AddFlipbooks(int32 Count, bool bWritten);
	PAPER2DPLUSEDITOR_API void AddProfileEntries(int32 Count, bool bWritten);
}

/**
 * TASK-192 U5: what the sprite pass should do for one layer's sprites. Null plan = the legacy full
 * rebuild (first import / non-incremental callers). FrameBuffers points at the layer's composited
 * buffers so tight boxes are derived from data already in hand (KTD6), never by RebuildData().
 */
struct FAseSpriteWritePlan
{
	FAseWriteDecision SheetDecision;
	bool bSheetObjectRecreated = false;
	FAseStampedGrid StampedGrid;
	FAseStampedGrid CurrentGrid;
	bool bForceFullReimport = false;
	const TArray<TArray<FColor>>* FrameBuffers = nullptr;
	/** Full object path of the owning sheet ("/Game/P/X_Sheet.X_Sheet") for the targeted load a
	 *  create-under-a-skipped-sheet needs. */
	FString SheetObjectPath;
};

/** TASK-192 U5: what the sprite pass actually did — consumed by the normal-map attach gate. */
struct FAseSpriteWriteOutcome
{
	/** One per frame; a resident object when this pass touched it, a bare soft path otherwise. */
	TArray<TSoftObjectPtr<class UPaperSprite>> SpriteRefs;
	/** Frames whose sprite took a FULL InitializeSprite (which resets AdditionalSourceTextures). */
	TSet<int32> FullyInitializedFrames;
	int32 SpritesWritten = 0;
	int32 SpritesSkipped = 0;
};

enum class EAsepriteImportMode : uint8
{
	LayerAssetNewProfile,
	LayerAssetExistingProfile,
	SeparateAssetsPerLayer,
};

/**
 * Everything one `.ase` import needs to know, independent of who collected it.
 *
 * This lives beside the importer that CONSUMES it, not beside any one authoring surface: the bulk
 * extractor's per-row editor, the live-reimport watcher and scripted callers all fill it in, and it
 * outlived the modal dialog it was originally declared in.
 */
struct FAsepriteLayerImportSettings
{
	/** Which layers are checked for import (key = layer index in FAsepriteParsedData::Layers). */
	TMap<int32, bool> LayerImportEnabled;

	/** Per-layer global-order overrides (key = layer index). */
	TMap<int32, int32> LayerOrder;

	/** Which animation tags are checked for import (key = tag index in FAsepriteParsedData::Tags —
	 *  the file's authored order; a missing key counts as enabled). All-on by default. An unticked
	 *  tag produces no flipbook/animation mapping; its frames still import as sprites. */
	TMap<int32, bool> TagImportEnabled;

	/** Legacy multi-file batch flag from the retired modal dialog. The bulk extractor chooses ONE
	 *  Character Profile and ONE Layer Profile for the whole batch instead, so nothing sets this. */
	bool bApplyToRemainingFiles = false;

	/** Keep the source .ase in the project: copy it to <Project>/SourceArt/ and track the copy, so the
	 *  artist can commit the .ase with the project and edits propagate to every synced machine.
	 *  (Outside Content/ on purpose — a loose .ase in Content trips Unreal's own auto-import prompt.)
	 *  No-op when the file already lives under the project. Default on. */
	bool bKeepSourceInProject = true;

	/** Organize generated assets into subfolders under the output path — Flipbooks/, Sheets/, and
	 *  Sprites/<prefix>/ — instead of dumping hundreds of assets flat into one folder. The Profile and
	 *  Layer asset stay at the output root. Default on. */
	bool bOrganizeIntoSubfolders = true;

	/** Derived from the caller's pickers: "Separate flipbooks only" → SeparateAssetsPerLayer; a
	 *  Character Profile picked → LayerAssetExistingProfile; else LayerAssetNewProfile. */
	EAsepriteImportMode ImportMode = EAsepriteImportMode::LayerAssetNewProfile;

	/** Character Profile picker: None = create/refresh "<prefix>_Profile"; set = deliver hitbox data
	 *  onto this existing profile without regenerating its animations. */
	TSoftObjectPtr<UPaper2DPlusCharacterProfileAsset> ExistingProfile;

	/** Layer Asset picker: None = create/additively-reimport "<prefix>_Layers" at the output path;
	 *  set = import this file's layers into the picked Character Layer asset instead. */
	TSoftObjectPtr<UPaper2DPlusCharacterLayerAsset> ExistingLayerAsset;

	/** Content browser output path (e.g., "/Game/Sprites"). */
	FString OutputPath;

	/** Prefix for asset names (e.g., the filename without extension). */
	FString AssetPrefix;

	/** True once the caller has confirmed the import (the bulk extractor's Extract All). */
	bool bUserConfirmed = false;

	/** TASK-192: bypass every incremental content gate and replay the full pipeline. The recovery
	 *  path (U8's Force Full Reimport action) sets this; it deliberately also works while the Live
	 *  .ase Auto-Reimport setting is off, because a manual action is explicit user intent. */
	bool bForceFullReimport = false;

	/** Original .ase/.aseprite file path on disk. */
	FString SourceFilePath;
};

/**
 * Handles importing Aseprite (.ase/.aseprite) files.
 * Parses the binary format and creates Paper2D assets.
 */

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
	 * ADDITIVELY deliver a standard-import result's flipbooks onto a POPULATED existing profile —
	 * the multi-file-character workflow (one profile accumulating animations across several .ase
	 * imports). For each created flipbook: an entry whose FlipbookName matches the prefix-stripped
	 * name (case-insensitive) is REFRESHED in place (flipbook ref + source texture; its authored
	 * combat/timing/tag data untouched), otherwise a new entry is APPENDED. Never wipes, never
	 * reorders, never touches entries from other files. Modify() first (the U1/R12 reconcile
	 * signal); package dirtying stays with the caller. Hitbox delivery is separate — callers run
	 * TransferHitboxDataToProfile with their chosen policy afterwards. Worldless-testable.
	 */
	static void AppendProfileEntriesFromImportResult(
		UPaper2DPlusCharacterProfileAsset* Profile,
		const FAsepriteImportResult& ProfileResult,
		const FString& AssetPrefix,
		int32* OutEntriesAdded = nullptr,
		int32* OutEntriesRefreshed = nullptr,
		int32* OutEntriesSkipped = nullptr);

	/**
	 * PURE: drop tags whose ORIGINAL index (position in the authored array, the order the import
	 * dialog displays) is in DisabledTagIndices. Shared by ImportFile and ImportAsLayeredAsset so
	 * the dialog's per-tag checkbox semantics cannot drift between the two flows. Empty set = no-op.
	 * Worldless-testable.
	 */
	/**
	 * PURE: seed OutSettings with the default per-file selection — every visual (non-group,
	 * non-hitbox) layer enabled in source order with a sequential LayerOrder, every animation tag
	 * enabled. The ONE shared default for the bulk `.ase` intake, the per-row editor, and the
	 * watcher's full-pipeline reimport, so the three cannot drift. Worldless-testable.
	 */
	static void InitDefaultSelection(const FAsepriteParsedData& InParsedData, FAsepriteLayerImportSettings& OutSettings);

	static void FilterTagsByDisabledIndices(TArray<FAsepriteTag>& Tags, const TSet<int32>& DisabledTagIndices);

	/**
	 * PURE: canonical stored form of a .ase source path. A path under the project directory is stored
	 * PROJECT-RELATIVE (forward slashes) so the asset resolves on every machine that syncs the project;
	 * anything else is stored as the normalized absolute path. Worldless-testable.
	 */
	static FString MakeStoredAsePath(const FString& AbsoluteFilePath);

	/**
	 * PURE: resolve a stored .ase source path (as written by MakeStoredAsePath, or a legacy absolute
	 * path) to a normalized absolute path. Relative input resolves against the PROJECT directory —
	 * never against the process working directory, which is what a raw ConvertRelativePathToFull on a
	 * relative stored path would do (it resolves against the engine binaries dir). Worldless-testable.
	 */
	static FString ResolveStoredAsePath(const FString& StoredPath);

	/**
	 * MD5 of a file's content as a hex string, or empty when the file is unreadable. The stamp stored
	 * on UPaper2DPlusCharacterLayerAsset::ImportedAseContentHash at import/auto-reimport time, and the
	 * comparison the watcher's startup reconcile uses to catch edits made while the editor was closed.
	 */
	static FString HashAseFileContent(const FString& AbsoluteFilePath);

	/**
	 * "Keep source in project": ensure the imported .ase lives inside the project by copying it to
	 * <Project>/SourceArt/ (flat — distinct source files are expected to have distinct names).
	 * Deliberately OUTSIDE Content/ — a loose .ase inside Content trips Unreal's own auto-import monitor,
	 * which prompts "new source file detected" right after the import. Returns the absolute path the
	 * caller should treat as the EFFECTIVE source: the copy on success, or InAbsoluteFilePath unchanged
	 * when the file is already under the project or the copy fails (logged; the import never fails over this).
	 */
	static FString CopySourceAseIntoProject(const FString& InAbsoluteFilePath, const FString& OutputPath);

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
	 * @param DisabledTagIndices Optional per-tag de-selection (dialog checkboxes). Indices are into the
	 *        FILE'S AUTHORED tag order — the order ParseFile returns and the import dialog displays.
	 *        Matching tags are dropped right after parse, so sprites still cover every frame but no
	 *        flipbook is created for a dropped tag and hitbox→keyframe alignment sees only enabled tags.
	 *        Null/empty = import every tag (byte-identical legacy behavior). Disabling ALL tags falls
	 *        into the no-tags path (one "<prefix>_All" flipbook).
	 * @param bOrganizeSubfolders When true, generated assets land in subfolders under OutputPath —
	 *        the sheet texture in Sheets/, per-frame sprites in Sprites/<prefix>/, flipbooks in
	 *        Flipbooks/ — instead of flat beside each other. False = legacy flat layout.
	 * @return Import result with created assets
	 */
	static FAsepriteImportResult ImportFile(
		const FString& FilePath,
		const FString& OutputPath,
		const FString& AssetPrefix,
		const TSet<int32>* DisabledTagIndices = nullptr,
		bool bOrganizeSubfolders = false,
		FAsepriteIncrementalImportContext* IncrementalContext = nullptr
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
	 * TASK-192 U8: re-run the full import for ONE tracked source of a Layer Profile, optionally
	 * forcing every incremental gate open. The recovery path for a wrong skip — it deliberately
	 * works regardless of the Live .ase Auto-Reimport setting, because a manual action is explicit
	 * user intent while the setting governs only the automatic reaction to file changes.
	 */
	static bool ForceReimportLayerAssetSource(
		UPaper2DPlusCharacterLayerAsset& LayerAsset,
		const struct FAsepriteSourceContext& SourceContext,
		bool bForceFullReimport,
		FAsepriteImportCostReport* OutReport = nullptr);

	/**
	 * The layer names SourceContext contributes that at least one OTHER recorded source of the Layer Profile
	 * also contributes (sorted; the record is each source's LayerContentHashes keys). A shared name is ONE
	 * row whose sheet holds every contributor's frames, so ForceReimportLayerAssetSource refuses a source
	 * with any (2026-09-04); OutOtherSourcePaths receives the sources that share them. Empty when the asset
	 * has one source.
	 */
	static TArray<FString> CollectLayersSharedWithOtherSources(
		const UPaper2DPlusCharacterLayerAsset& LayerAsset,
		const struct FAsepriteSourceContext& SourceContext,
		TArray<FString>* OutOtherSourcePaths = nullptr);

	/** TASK-192 U8: publish one reimport's decision audit — every write/skip with its reason — as a
	 *  walkable, NON-modal Message Log page (the watcher runs unattended; a prompt would hang it).
	 *  No-op in unattended runs, where the cost summary and per-skip log lines are the record. */
	static void PublishIncrementalAuditPage(
		const FString& AssetDisplayName,
		const FAsepriteImportCostReport& Report);

	/**
	 * Prompt for one or more `.ase`/`.aseprite` files and load them into the Bulk Sprite Extractor as
	 * source rows. This is the MULTI-FILE door: a Content Browser drop can only deliver one file,
	 * because AssetTools stops its per-file loop as soon as a factory reports a cancel.
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
	static FPerLayerBufferMap CompositePerLayer(const FAsepriteParsedData& Data);

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
		const FString& AssetName,
		bool* bOutCreatedTexture = nullptr
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
		const FString& AssetPrefix,
		const FAseSpriteWritePlan* WritePlan = nullptr,
		FAseSpriteWriteOutcome* OutOutcome = nullptr
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
		const FString& AssetName,
		bool* bOutCreatedTexture = nullptr);

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
		int32& OutSpritesTouched,
		bool bSkipAlreadyAttached = false);

private:
	/** Create a texture from flattened frame pixel data */
	static UTexture2D* CreateSpriteSheetTexture(
		const FAsepriteParsedData& Data,
		const FString& OutputPath,
		const FString& AssetName,
		bool* bOutCreatedTexture = nullptr
	);

	/** Create sprites from the sprite sheet */
	static TArray<UPaperSprite*> CreateSprites(
		UTexture2D* SpriteSheet,
		const FAsepriteParsedData& Data,
		const FString& OutputPath,
		const FString& AssetPrefix,
		const FAseSpriteWritePlan* WritePlan = nullptr,
		FAseSpriteWriteOutcome* OutOutcome = nullptr
	);

	/** Create flipbooks from tags */
	static TArray<UPaperFlipbook*> CreateFlipbooks(
		const TArray<TSoftObjectPtr<UPaperSprite>>& SpriteRefs,
		const FAsepriteParsedData& Data,
		const FString& OutputPath,
		const FString& AssetPrefix,
		FAsepriteIncrementalImportContext* IncrementalContext = nullptr,
		TArray<FAseTagFlipbookOutcome>* OutOutcomes = nullptr
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
