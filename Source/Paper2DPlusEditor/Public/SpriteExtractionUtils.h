// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Paper2DPlusTypes.h"
#include "Widgets/SWidget.h"

class UTexture2D;
class UPaperSprite;
class UPaper2DPlusCharacterProfileAsset;

/**
 * Represents a detected sprite region in the source texture.
 * Shared between SSpriteExtractorWindow and re-extract flows.
 */
struct FDetectedSprite
{
	FIntRect Bounds;			// Bounding rectangle in texture space
	FIntRect OriginalBounds;	// Original tight-fit bounds before uniform sizing
	bool bSelected = true;		// Is this sprite selected for extraction?
	int32 Index = 0;			// Detection order index

	FIntPoint GetSize() const { return FIntPoint(Bounds.Width(), Bounds.Height()); }
	FIntPoint GetOriginalSize() const { return FIntPoint(OriginalBounds.Width(), OriginalBounds.Height()); }
};

/** One frame participating in midpoint/max-extent normalization. ContainerBounds is the region the
 *  target must remain inside (normally a source grid cell or the sprite's current source region).
 *  Empty tight bounds do not influence the maxima, but still receive the final uniform target when
 *  another frame supplies content. */
struct PAPER2DPLUSEDITOR_API FSpriteMaxExtentFrame
{
	FIntRect ContainerBounds;
	FIntRect TightBounds;
	FIntPoint Anchor = FIntPoint::ZeroValue;
	FIntRect TargetBounds;
	bool bEmpty = false;
	bool bTargetFits = false;
};

/** Result of the shared cell-midpoint/max-extent planner. */
struct PAPER2DPLUSEDITOR_API FSpriteMaxExtentResult
{
	int32 MaxLeft = 0;
	int32 MaxRight = 0;
	int32 MaxTop = 0;
	int32 MaxBottom = 0;
	int32 NonEmptyFrames = 0;
	int32 EmptyFrames = 0;
	int32 FramesOutsideContainers = 0;

	FIntPoint GetUniformSize() const
	{
		return FIntPoint(MaxLeft + MaxRight, MaxTop + MaxBottom);
	}

	bool IsValid() const
	{
		const FIntPoint Size = GetUniformSize();
		return NonEmptyFrames > 0 && Size.X > 0 && Size.Y > 0;
	}
};

/**
 * Parameters for island-based sprite detection.
 */
struct FSpriteDetectionParams
{
	int32 AlphaThreshold = 1;
	int32 MinSpriteSize = 4;
	bool bUse8DirectionalFloodFill = true;
	int32 IslandMergeDistance = 2;
};

/**
 * Result of uniform frame-GRID detection (FSpriteExtractionUtils::DetectFrameGrid).
 *
 * Distinct from island detection: islands find where the ART is, a frame grid finds where the
 * ANIMATION FRAMES are. The definitive property of a uniform grid is a GUTTER — every internal
 * frame boundary falls on an empty column — which survives scattered particle VFX that shatter
 * into dozens of islands and defeat any spacing-based heuristic.
 */
struct PAPER2DPLUSEDITOR_API FDetectedFrameGrid
{
	/** Cell size in pixels (width x height). */
	FIntPoint Cell = FIntPoint::ZeroValue;

	/** Grid layout (columns x rows). */
	FIntPoint Grid = FIntPoint::ZeroValue;

	/** Which rule decided the horizontal axis: "gutter", "hint", "sibling", "gutter-sparse",
	 *  "hint-nogutter", "pitch", "single", "single-cell", or "empty". Purely diagnostic —
	 *  surface it so a low-confidence sheet can be eyeballed. */
	FString Source;

	/** Indices of empty cells along the HORIZONTAL axis (one entry per column cell, ascending).
	 *  On the usual single-row sheet these are frame indices; on a multi-row sheet a column may
	 *  still hold content in another row, so treat them as columns, not frames. */
	TArray<int32> BlankFrames;

	/** Length of the blank run at the END of a single-row sheet (VFX length padding), else 0.
	 *  Legitimate: VFX strips are padded so their length matches the animation they accompany. */
	int32 TrailingBlanks = 0;

	/** True when the horizontal axis was decided by a gutter, the artist's hint, folder consensus,
	 *  or a proven single cell — i.e. the answer does not need a human pass. */
	bool bConfident = false;

	bool IsValid() const { return Cell.X > 0 && Cell.Y > 0 && Grid.X > 0 && Grid.Y > 0; }
};

/**
 * One sheet participating in a batch frame-grid pass (FSpriteExtractionUtils::ApplyFolderConsensus).
 *
 * Carries the per-axis occupancy so consensus can re-decide a sheet WITHOUT re-reading its pixels
 * and without touching a UObject — the whole second pass stays pure and worldless-testable.
 */
struct PAPER2DPLUSEDITOR_API FFrameGridCandidate
{
	/** Grouping key for consensus — normally the sheet's subfolder path. Sheets with different
	 *  keys never influence each other. */
	FString FolderKey;

	/** Artist's size hint parsed from the filename/folder, or Zero when absent. */
	FIntPoint FilenameHint = FIntPoint::ZeroValue;

	/** Smallest cell size the search may consider. */
	int32 MinCell = 16;

	/** Per source COLUMN: does any row in that column hold content. Length == sheet width. */
	TArray<bool> ColumnOccupancy;

	/** Per source ROW: does any column in that row hold content. Length == sheet height. */
	TArray<bool> RowOccupancy;

	/** Per source COLUMN: HOW MANY rows hold content. Length == sheet width. Drives the bleed
	 *  tier, which needs occupancy MAGNITUDE (a boundary holding one stray pixel is still a
	 *  boundary; one holding thirty is artwork) rather than the boolean. */
	TArray<int32> ColumnCounts;

	/** First-pass result; ApplyFolderConsensus may replace it with a consensus-rescued one. */
	FDetectedFrameGrid Result;
};

/**
 * Deterministic sprite-sheet grid PACKING shared by every Aseprite import/reimport site, so the
 * generated sheet, the per-sprite source regions, and the reimport-regenerated regions all agree
 * byte-for-byte. Pure integer packing math — distinct from the stability-critical uniform-bounds
 * DETECTION in ComputeUniformBounds/InferGridDimensions; do NOT route it through those.
 *
 * Dimension-aware: prefers a near-square column count (matching the legacy "<=16 frames -> single
 * row, else ceil(sqrt)" shape wherever it fits) but clamps the column count DOWN so the sheet width
 * never exceeds MaxDimension. That fixes the old over-rejection — e.g. 16 wide frames forced a single
 * row that overflowed and aborted a sheet a 2D grid fits fine — while staying byte-identical for every
 * sheet that already fit. bValid is false only when even the widest legal packing still overflows
 * (a cell taller/wider than MaxDimension, or zero frames).
 */
struct PAPER2DPLUSEDITOR_API FSpriteSheetGrid
{
	/** Max 2D texture dimension on modern RHIs (D3D11/12, Vulkan, Metal). Shared overflow ceiling. */
	static constexpr int32 DefaultMaxDimension = 16384;

	int32 Columns = 0;
	int32 Rows = 0;
	int32 CellW = 0;
	int32 CellH = 0;
	int32 SheetW = 0;
	int32 SheetH = 0;
	bool  bValid = false;   // false => caller must abort (sheet would overflow MaxDimension)

	static FSpriteSheetGrid Compute(int32 FrameCount, int32 CellW, int32 CellH, int32 MaxDimension = DefaultMaxDimension);

	/** Pixel rect of frame Index, using the Index%Columns / Index/Columns ordering every site uses. */
	FIntRect GetCellRect(int32 Index) const
	{
		const int32 Col = Columns > 0 ? Index % Columns : 0;
		const int32 Row = Columns > 0 ? Index / Columns : 0;
		return FIntRect(Col * CellW, Row * CellH, (Col + 1) * CellW, (Row + 1) * CellH);
	}
};

/**
 * Metadata for a single texture's pre-pad snapshot. Held in memory for the
 * duration of a pad run; the actual pixel payload lives on disk as a raw BGRA8
 * sidecar under <ProjectSaved>/Paper2DPlus/PadBackups/<RunGuid>/<TextureGuid>.bin.
 *
 * Deliberately minimal — no JSON, no embedded header, no cross-session recovery
 * metadata. Orphan sidecar recovery (enumerating pending runs from prior sessions)
 * is deferred to the Overview-tab toolbar work in Unit 8.
 */
struct FPadSnapshotManifest
{
	/** Run-level GUID shared by every snapshot in the same pad batch. Also the directory name on disk. */
	FGuid RunGuid;

	/** Per-texture GUID. Also the sidecar filename stem on disk. */
	FGuid TextureGuid;

	/** Soft-ref to the texture — survives asset rename/move between save and restore. */
	TSoftObjectPtr<UTexture2D> Texture;

	/** Dimensions of the snapshot bytes. Restore fails if the on-disk payload size disagrees. */
	FIntPoint OriginalDims = FIntPoint::ZeroValue;

	/** CRC32 of the snapshot bytes, checked on restore. */
	uint32 BytesCrc32 = 0;

	/** When the snapshot was captured (UTC). */
	FDateTime Timestamp;

	/** Soft-ref to the profile that initiated this pad run. */
	TSoftObjectPtr<UPaper2DPlusCharacterProfileAsset> InitiatingProfile;

	/** Absolute path to the .bin sidecar on disk. */
	FString SidecarPath;

	bool IsValid() const
	{
		return TextureGuid.IsValid() && RunGuid.IsValid() &&
			OriginalDims.X > 0 && OriginalDims.Y > 0 &&
			!SidecarPath.IsEmpty();
	}
};

/** Stats returned from ApplyCrossSheetAlignment. */
struct FCrossSheetAlignmentStats
{
	int32 SpritesUpdated = 0;
	int32 SpritesSkipped = 0;   // convergence guard fired
	int32 HitboxesRemapped = 0;
	int32 ProfilesTouched = 0;
};

/**
 * Shared utilities for sprite extraction operations.
 * Used by SSpriteExtractorWindow for initial extraction and
 * SCharacterProfileAssetEditor for re-extraction.
 */
class UPackage;

class PAPER2DPLUSEDITOR_API FSpriteExtractionUtils
{
public:
	/** Replace spaces with underscores in asset/folder names. UE asset names must not contain spaces. */
	static void SanitizeAssetName(FString& Name) { Name.ReplaceInline(TEXT(" "), TEXT("_")); }

	/**
	 * Replace EVERY character the engine forbids in an object name or a long package name with '_'.
	 *
	 * SanitizeAssetName handles the space and nothing else, which is enough for most artist layer names
	 * and is why it survived so long. It is NOT enough in general: the engine also rejects & ! ~ @ # . ,
	 * quotes, brackets and more (INVALID_OBJECTNAME_CHARACTERS / INVALID_LONGPACKAGE_CHARACTERS). A name
	 * carrying one of those cannot become a package, so the generated asset is never created — and any
	 * reference recorded against the intended path silently resolves to nothing.
	 *
	 * Substitution is one character to one underscore, never collapsing runs, so a name that the space-only
	 * sanitizer already produced is returned byte-identical and no working asset is ever renamed.
	 */
	static void SanitizeAssetNameStrict(FString& Name)
	{
		auto ReplaceEachOf = [&Name](const TCHAR* Invalid)
		{
			for (const TCHAR* Cursor = Invalid; *Cursor != TEXT('\0'); ++Cursor)
			{
				const TCHAR Single[2] = { *Cursor, TEXT('\0') };
				Name.ReplaceInline(Single, TEXT("_"));
			}
		};
		ReplaceEachOf(INVALID_OBJECTNAME_CHARACTERS);
		ReplaceEachOf(INVALID_LONGPACKAGE_CHARACTERS);
	}

	/**
	 * Build a content-browser output-path control: an editable text box (typed-entry fallback — users can
	 * paste/type a path) PLUS an SComboButton whose menu content is the Content Browser path picker
	 * (FContentBrowserModule::CreatePathPicker). Picking a folder closes the combo and writes the new path
	 * through OnPathPicked; typing a path commits it the same way. The text box stays in sync with the live
	 * CurrentPath attribute so a picker selection updates the displayed text.
	 *
	 *  - CurrentPath: bindable label/text source (e.g. read the settings field it writes to).
	 *  - OnPathPicked: invoked with the newly-selected/typed content path (e.g. "/Game/Sprites").
	 *
	 * Editor-only (ContentBrowser module). The picker is interactive — it is not exercised by headless tests.
	 */
	static TSharedRef<SWidget> MakeContentPathPicker(TAttribute<FString> CurrentPath, TFunction<void(const FString&)> OnPathPicked);

	/** Check if texture needs Paper2D pixel-art settings (nearest filter, no compression, no mips) */
	static bool NeedsPaper2DSettings(const UTexture2D* Texture);

	/** Apply Paper2D pixel-art settings to texture. Calls Texture->Modify() for editor undo support. */
	static void ApplyPaper2DSettings(UTexture2D* Texture);

	/** Force CPU access on texture by rebuilding platform data. Returns false if source data is missing. */
	static bool ForceCPUAccess(UTexture2D* Texture);

	/** Load texture pixel data. Supports PF_B8G8R8A8 and PF_R8G8B8A8. */
	static bool LoadTextureData(
		UTexture2D* Texture,
		TArray<FColor>& OutPixels,
		int32& OutWidth,
		int32& OutHeight);

	/** Create a PaperSprite asset from texture bounds. Returns nullptr on failure. */
	static UPaperSprite* CreateSpriteFromBounds(
		UTexture2D* SourceTexture,
		const FIntRect& Bounds,
		const FString& SpriteName,
		const FString& OutputPath);

	/** Create a packed texture containing only the given regions from the source.
	 *  Regions are laid out in a single horizontal strip. Returns nullptr on failure. */
	static UTexture2D* CreatePackedTexture(
		UTexture2D* SourceTexture,
		const TArray<FIntRect>& Regions,
		const FString& TextureName,
		const FString& OutputPath);

	/** Find-reuse-or-create the named UTexture2D in Package. Reuses an existing same-class texture in
	 *  place; evicts any different-class occupant (redirector / stray asset) of that name to the transient
	 *  package before the explicit-name NewObject so it cannot collide inside StaticAllocateObject (U7).
	 *  Returns nullptr only when Package is null. */
	static UTexture2D* GetOrCreateTextureForName(UPackage* Package, const FString& Name);

	/** Filter a merge/selection index list against a live array of the given size, dropping any index
	 *  that is out of range (stale after a re-index/shrink). Order preserved. Callers treat a result of
	 *  fewer than 2 survivors as "nothing to merge" (U8). */
	static TArray<int32> FilterValidSpriteIndices(const TArray<int32>& Indices, int32 Count);

	/** Find the tight content bounding rect within a cell, scanning for non-transparent pixels.
	 *  Returns a zero-area rect if the cell is entirely empty. */
	static FIntRect FindTightContentBounds(
		const TArray<FColor>& Pixels, int32 TexW, int32 TexH,
		const FIntRect& CellBounds, int32 AlphaThreshold = 1);

	/** Compute one uniform target from maximum left/right/top/bottom content extents around each
	 *  frame's authoritative anchor. Empty frames are excluded from the maxima and then assigned the
	 *  same target size. A target is repairable only when it fits inside ContainerBounds. */
	static FSpriteMaxExtentResult ComputeMaxExtentTargets(TArray<FSpriteMaxExtentFrame>& Frames);

	/** Convert a tight content origin from SourceRegion coordinates into PackedRegion coordinates.
	 *  Empty content resolves to PackedRegion.Min so metadata always names the texture actually used
	 *  by the sprite rather than leaking an origin from the pre-pack source sheet. */
	static FIntPoint ComputePackedContentOffset(
		const FIntRect& SourceRegion,
		const FIntRect& TightContent,
		const FIntRect& PackedRegion);

	/** Create a tightly-packed texture from variable-sized regions (no centering pad).
	 *  Layout: horizontal strip, each region placed at its exact width.
	 *  OutPackedBounds[i] = the UV rect in the packed texture for Regions[i]. */
	static UTexture2D* CreateTrimmedPackedTexture(
		UTexture2D* SourceTexture,
		const TArray<FIntRect>& TightRegions,
		const FString& TextureName,
		const FString& OutputPath,
		TArray<FIntRect>& OutPackedBounds);

	// ==========================================
	// Detection & uniform bounds (shared algorithms)
	// ==========================================

	/** Detect sprite regions in a texture using island (flood-fill) detection.
	 *  Returns sorted left-to-right, with nearby islands merged. */
	static TArray<FDetectedSprite> DetectSpriteBounds(
		UTexture2D* Texture,
		const FSpriteDetectionParams& Params);

	// ------------------------------------------------------------------
	// Frame-grid detection (the gutter rule) — ADDITIVE beside island detection.
	// Island detection answers "where is the art"; this answers "where are the frames".
	// Nothing here touches DetectSpriteBounds / InferGridDimensions / FSpriteDetectionParams.
	// ------------------------------------------------------------------

	/** Detect the uniform frame grid of a sprite sheet.
	 *  A candidate cell width is valid iff every internal boundary column is empty (the GUTTER
	 *  rule) AND blank cells appear only as a capped trailing run; the search runs
	 *  most-frames-first so the finest valid grid wins. FilenameHint is the artist's
	 *  "Foo_192x128" label (Zero when absent) and WINS whenever the pixels do not contradict it,
	 *  including hint.X == sheet width (the single-frame sheet, which has no internal boundary
	 *  to test and must stay selectable). Rows split ONLY on an explicit hint — a horizontal gap
	 *  through a character is not a row break, and a wrong row split silently halves every frame.
	 *  Occupancy is alpha > 0. Returns an invalid result (IsValid() == false) when the texture
	 *  has no readable source data. */
	static FDetectedFrameGrid DetectFrameGrid(
		UTexture2D* Texture,
		FIntPoint FilenameHint,
		int32 MinCell = 16);

	/** Pure core of DetectFrameGrid: decide the grid from per-axis occupancy alone.
	 *  No texture, no UObject, no world — the single algorithm home, so DetectFrameGrid and
	 *  ApplyFolderConsensus can never drift apart.
	 *  PreferCellWidth is the folder's agreed width (0 = none); it is honoured only when this
	 *  sheet's OWN pixels admit it, so consensus can break a tie but never cut through artwork.
	 *  ColumnCounts is optional; supplying it enables the BLEED TIER for trimmed art, where a
	 *  stray pixel on one boundary would otherwise disqualify the true grid. That tier is only
	 *  consulted when no CLEAN grid exists, so passing counts can never change a sheet that
	 *  already resolves cleanly. */
	static FDetectedFrameGrid DetectFrameGridFromOccupancy(
		const TArray<bool>& ColumnOccupancy,
		const TArray<bool>& RowOccupancy,
		FIntPoint FilenameHint,
		int32 MinCell = 16,
		int32 PreferCellWidth = 0,
		const TArray<int32>* ColumnCounts = nullptr);

	/** Build one batch entry: read the texture once, cache its per-axis occupancy, and run the
	 *  first-pass detection. Feed the resulting array to ApplyFolderConsensus. */
	static FFrameGridCandidate MakeFrameGridCandidate(
		UTexture2D* Texture,
		const FString& FolderKey,
		FIntPoint FilenameHint,
		int32 MinCell = 16);

	/** Second pass over a batch: let a folder's agreed frame FORMAT rescue its odd sheets.
	 *  Consensus is per (cell width AND height) — a folder of 192x128 character sheets also holds
	 *  little 384x64 VFX strips, and those must never inherit the character frame width. A folder
	 *  needs 3+ sheets with a strict majority to have a dominant format at all, and a sheet adopts
	 *  it only when its own pixels admit that width. Mutates each entry's Result in place. */
	static void ApplyFolderConsensus(TArray<FFrameGridCandidate>& InOutResults);

	/** Parse a "192x128"-style frame-size hint out of an asset name, source filename, or folder
	 *  name (last match wins; 'x' or 'X'; extension/suffix tolerated). Returns FIntPoint::ZeroValue
	 *  when the string carries no plausible hint. */
	static FIntPoint ParseFrameSizeHint(const FString& NameOrPath);

	/** Compute uniform bounds using cell-midpoint + max-extent algorithm.
	 *  Mutates Bounds on each sprite in-place. Requires >= 2 sprites.
	 *  OriginalBounds must be set to tight-fit bounds before calling.
	 *  TexW/TexH MUST match the coordinate space of sprite OriginalBounds
	 *  (use GetDetectionDimensions, not Texture->GetSizeX). */
	static void ComputeUniformBounds(
		TArray<FDetectedSprite>& Sprites,
		int32 TexW,
		int32 TexH);

	/** Get texture dimensions matching LoadTextureData coordinate space.
	 *  Uses source data dimensions (original import size, no power-of-2 padding). */
	static FIntPoint GetDetectionDimensions(const UTexture2D* Texture);

	/** Pure preview helper: copy Sprites, run ComputeUniformBounds, return the resulting uniform
	 *  size (Bounds[0]'s size). The single source of truth for the extractor's "Extraction bounds:
	 *  W x H" preview, so it can never diverge from the real extraction path.
	 *  Returns FIntPoint::ZeroValue if fewer than 2 sprites or TexW/TexH <= 0.
	 *  NOTE: ComputeUniformBounds emits heavy per-row UE_LOG — call this only when the selection
	 *  set / texture changes, never inside a per-paint lambda. */
	static FIntPoint ComputeUniformPreviewSize(const TArray<FDetectedSprite>& Sprites, int32 TexW, int32 TexH);

	/** Expand a single sprite's OriginalBounds to UniformSize using anchor alignment.
	 *  Used for single-sprite case or canvas preview with anchor control. */
	static FIntRect ExpandBoundsToUniform(
		const FIntRect& OriginalBounds,
		FIntPoint UniformSize,
		ESpriteAnchor Anchor,
		int32 TexW,
		int32 TexH);

	/** Update an existing sprite's source region and texture without creating a new asset.
	 *  Calls InitializeSprite internally. Sprite->Modify() must be called by the caller.
	 *  ArtShiftDelta = how far the art moved within the sprite (ArtInNewSprite - ArtInOldSprite).
	 *  Used to remap Custom pivots from old to new texture space. Named pivot modes recompute
	 *  automatically; only Custom pivots need the delta. */
	static void UpdateSpriteSourceRegion(
		UPaperSprite* Sprite,
		UTexture2D* NewTexture,
		const FIntRect& NewBounds,
		FIntPoint ArtShiftDelta = FIntPoint::ZeroValue);

	/** Delete a texture asset. Silently succeeds if Texture is null.
	 *  Uses ForceDeleteObjects to remove without confirmation. */
	static void DeleteTextureAsset(UTexture2D* Texture);

	// ==========================================
	// Cross-sheet alignment (Unit 1b of 2026-04-19 plan)
	// ==========================================

	/** Infer the grid layout (Cols x Rows) for a texture given its detected sprites.
	 *  Duplicates the Y-overlap row grouping logic from ComputeUniformBounds locally so the
	 *  stability-critical algorithm is not modified. A drift-detection test ensures both
	 *  paths produce identical row/col counts on identical input.
	 *  Returns FIntPoint::ZeroValue if Sprites has fewer than 2 entries. */
	static FIntPoint InferGridDimensions(const TArray<FDetectedSprite>& Sprites);

	/** Take the per-axis maximum across all entries in a per-texture cell-size map.
	 *  Used by the cross-sheet auto-pad step to find the group's target dimensions. */
	static FIntPoint ComputeGroupMaxCellSize(const TMap<UTexture2D*, FIntPoint>& PerTextureCellSize);

	/** True only when the map is non-empty, all sizes are positive, and every texture already uses
	 *  the same per-axis group maximum. The extraction gate derives readiness from this live data so
	 *  stale Padded/Skipped status chips cannot bypass a currently-required pad. */
	static bool AreCellSizesUniform(const TMap<UTexture2D*, FIntPoint>& PerTextureCellSize);

	/** Find every CharacterProfile whose flipbook sprites reference the given texture.
	 *  Walks the asset registry (authoritative) and dereferences each profile's flipbook
	 *  soft-refs + their KeyFrames.Sprite.SourceTexture. Returns deduplicated list, empty on null input.
	 *  Used by the shared-texture guard in cross-sheet auto-pad. */
	static TArray<UPaper2DPlusCharacterProfileAsset*> FindProfilesReferencingTexture(UTexture2D* Texture);

	/** Capture a pre-pad snapshot of a texture's Source bytes to a sidecar manifest on disk.
	 *  SaveDir is the run-level directory (e.g. <ProjectSaved>/Paper2DPlus/PadBackups/<RunGuid>).
	 *  Returns an invalid manifest (IsValid() == false) on failure (null texture, Source empty,
	 *  CPU data unavailable, disk write failure). */
	static FPadSnapshotManifest SnapshotTexture(
		UTexture2D* Texture,
		const FGuid& RunGuid,
		const FString& SaveDir,
		TSoftObjectPtr<UPaper2DPlusCharacterProfileAsset> InitiatingProfile);

	/** Restore a texture's Source bytes from a snapshot manifest, undoing an in-place pad.
	 *  Validates GUID match, dimension match, and CRC32 integrity before any mutation.
	 *  Returns false (leaving the texture untouched) on any validation or I/O failure. */
	static bool RestoreTextureSnapshot(const FPadSnapshotManifest& Manifest);

	/** In-place pad a sprite sheet texture so every grid cell reaches TargetCellDims.
	 *  Splits the source into Grid.X * Grid.Y cells, pads each cell individually to
	 *  TargetCellDims, then reassembles into a new texture of size
	 *  (Grid.X * TargetCellDims.X) x (Grid.Y * TargetCellDims.Y).
	 *
	 *  THE ANCHOR IS THE CELL MIDPOINT — midpoint-to-midpoint, on BOTH axes:
	 *      CellOffsetX = DstCellW / 2 - SrcCellW / 2;
	 *      CellOffsetY = DstCellH / 2 - SrcCellH / 2;
	 *  Every cell of the sheet therefore receives the SAME offset. That is what preserves each
	 *  animation's vertical motion: a content-driven per-cell anchor (the retired ground-plane
	 *  rule) collapses to one fixed ground line for every cell and DELETES the motion — measured
	 *  across 462 real sheets, Aerial_DownAttack's 26px drop went to 0 and Roll's 9px rise to 0.
	 *  It also matches the pivot the bulk extractor's uniform trim bakes at
	 *  (TrimMaxExtLeft, TrimMaxExtTop) = the cell midpoint, so trim and pad agree by construction.
	 *
	 *  INVARIANT — NEVER anchor on a TRIMMED cell's BOX CENTRE, only on the midpoint. The two
	 *  coincide before a trim and diverge after it (measured: differ on 96% of character sheets,
	 *  by up to 16px), so box-centre anchoring silently misaligns every trimmed flipbook.
	 *
	 *  GroundPlaneOffset is DEPRECATED and IGNORED — retained only so existing call sites still
	 *  compile. Do not pass a meaningful value; there is no ground plane any more.
	 *
	 *  Follows the Source.Init + LockMip sequence (no FTexturePlatformData).
	 *  Returns the first cell's paste delta for compatibility. When OutPerCellPasteDeltas is supplied,
	 *  it receives one texture-space delta per row-major cell. The per-cell array is still required
	 *  because deltas accumulate the growth of PRECEDING cells —
	 *      DeltaX = Col * (DstCellW - SrcCellW) + CellOffsetX
	 *      DeltaY = Row * (DstCellH - SrcCellH) + CellOffsetY
	 *  — but it is now content-INDEPENDENT: two cells in the same column/row always agree.
	 *  Callers updating frame metadata must use the per-cell array.
	 *  Returns FIntPoint::ZeroValue on failure or if no padding is needed. */
	static FIntPoint PadTextureInPlace(
		UTexture2D* Texture,
		FIntPoint TargetCellDims,
		FIntPoint Grid,
		int32 GroundPlaneOffset = 0,
		TArray<FIntPoint>* OutPerCellPasteDeltas = nullptr);

	/** Apply cross-sheet alignment across every flipbook in the given profiles: set each sprite's
	 *  SourceUV/SourceDimension to its own DESTINATION CELL in the padded texture — resolved from the
	 *  same grid the paste delta is indexed against, never re-derived from the sprite's content rect,
	 *  so it is exact for odd cell sizes and for tight/trimmed rects alike — compute
	 *  HitboxDelta, short-circuit when NewBounds == current && HitboxDelta == 0 (R10 convergence
	 *  guard), otherwise UpdateSpriteSourceRegion + remap hitboxes/sockets + update extraction
	 *  info. Existing pivot modes and Profile-owned SpriteOffset/TrimOffset are preserved.
	 *  PerTextureSourceCellSizes: each padded texture's cell size before padding. Together with the
	 *  sprite's old source-region center this selects the authoritative source cell even when a
	 *  flipbook reorders, subsets, or has bounds expanded slightly across a cell edge.
	 *  PerTexturePasteOffsets: for every texture padded in the current run, the row-major per-cell
	 *  deltas that PadTextureInPlace returned. Pre-apply fixup adds the matching source-cell delta to
	 *  FrameExtractionInfo.SourceOffset so HitboxDelta math operates in the new coordinate space.
	 *  MaxEntriesByProfile optionally limits individual Profiles to rows that predate newly-created
	 *  sprites, which already live in the padded coordinate space. Profiles absent from the map migrate
	 *  every row.
	 *  Caller owns the enclosing FScopedTransaction. */
	static FCrossSheetAlignmentStats ApplyCrossSheetAlignment(
		const TArray<UPaper2DPlusCharacterProfileAsset*>& Profiles,
		FIntPoint GroupMaxCellDims,
		const TMap<UTexture2D*, FIntPoint>& PerTextureSourceCellSizes,
		const TMap<UTexture2D*, TArray<FIntPoint>>& PerTexturePasteOffsets,
		const TMap<UPaper2DPlusCharacterProfileAsset*, int32>& MaxEntriesByProfile);

private:
	static bool IsPixelOpaque(const TArray<FColor>& Pixels, int32 Width, int32 X, int32 Y, int32 AlphaThreshold);
	static void FloodFillMark(TArray<bool>& Visited, const TArray<FColor>& Pixels, int32 Width, int32 Height,
		int32 StartX, int32 StartY, FIntRect& OutBounds, bool bUse8Dir, int32 AlphaThreshold);
	static void MergeNearbyIslands(TArray<FDetectedSprite>& Sprites, int32 MergeDistance);
};
