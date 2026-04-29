// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Paper2DPlusTypes.h"

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
class PAPER2DPLUSEDITOR_API FSpriteExtractionUtils
{
public:
	/** Replace spaces with underscores in asset/folder names. UE asset names must not contain spaces. */
	static void SanitizeAssetName(FString& Name) { Name.ReplaceInline(TEXT(" "), TEXT("_")); }

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

	/** Find the tight content bounding rect within a cell, scanning for non-transparent pixels.
	 *  Returns a zero-area rect if the cell is entirely empty. */
	static FIntRect FindTightContentBounds(
		const TArray<FColor>& Pixels, int32 TexW, int32 TexH,
		const FIntRect& CellBounds, int32 AlphaThreshold = 1);

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
	 *  GroundPlaneOffset: pixels of empty space below content in each destination cell
	 *  (from reference sheet analysis). Each source cell's content bottom is placed at
	 *  DstCellH - GroundPlaneOffset.
	 *  Follows the Source.Init + LockMip sequence (no FTexturePlatformData).
	 *  Returns the per-cell paste offset (how many pixels each cell's content shifted);
	 *  caller applies this shift to any sprite's SourceUV referencing this texture.
	 *  Returns FIntPoint::ZeroValue on failure or if no padding is needed. */
	static FIntPoint PadTextureInPlace(
		UTexture2D* Texture,
		FIntPoint TargetCellDims,
		FIntPoint Grid,
		int32 GroundPlaneOffset = 0);

	/** Apply cross-sheet alignment across every flipbook in the given profiles: expand each
	 *  sprite's SourceUV/SourceDimension to GroupMaxCellDims with BottomCenter anchor, compute
	 *  HitboxDelta, short-circuit when NewBounds == current && HitboxDelta == 0 (R10 convergence
	 *  guard), otherwise UpdateSpriteSourceRegion + remap hitboxes/sockets + update extraction
	 *  info. Forces pivot to Center_Center.
	 *  PerTexturePasteOffsets: for every texture padded in the current run, the offset that
	 *  PadTextureInPlace returned. Pre-apply fixup adds these to each affected sprite's
	 *  FrameExtractionInfo.SourceOffset so HitboxDelta math operates in the new coordinate space.
	 *  Caller owns the enclosing FScopedTransaction. */
	static FCrossSheetAlignmentStats ApplyCrossSheetAlignment(
		const TArray<UPaper2DPlusCharacterProfileAsset*>& Profiles,
		FIntPoint GroupMaxCellDims,
		const TMap<UTexture2D*, FIntPoint>& PerTexturePasteOffsets);

private:
	static bool IsPixelOpaque(const TArray<FColor>& Pixels, int32 Width, int32 X, int32 Y, int32 AlphaThreshold);
	static void FloodFillMark(TArray<bool>& Visited, const TArray<FColor>& Pixels, int32 Width, int32 Height,
		int32 StartX, int32 StartY, FIntRect& OutBounds, bool bUse8Dir, int32 AlphaThreshold);
	static void MergeNearbyIslands(TArray<FDetectedSprite>& Sprites, int32 MergeDistance);
};
