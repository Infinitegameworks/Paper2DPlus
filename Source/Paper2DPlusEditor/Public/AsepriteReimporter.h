// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtr.h"

class UPaper2DPlusCharacterLayerAsset;
class UPaper2DPlusCharacterProfileAsset;

/** Type of structural conflict detected during reimport */
enum class EReimportConflictType : uint8
{
	LayerDeleted,              // Layer removed from .ase but has manual edits in UE
	LayerRenamed,              // Name match failed, possible rename detected
	TagRenamed,                // Animation tag renamed
	UniformBoundsInstability,  // (used by texture reimporter)
};

/** How the user chose to resolve a conflict */
enum class EReimportConflictResolution : uint8
{
	Unresolved,
	AcceptRename,
	KeepBoth,
	RemoveOld,
	KeepOrphaned,
	AcceptNew,
	KeepCurrent,
};

/** A single conflict detected during reimport that requires user resolution */
struct FReimportConflict
{
	EReimportConflictType Type;
	FString OldName;
	FString NewName;
	FString Description; // Human-readable description of what edits would be lost
	EReimportConflictResolution Resolution = EReimportConflictResolution::Unresolved;

	// --- Texture-reimport (UniformBoundsInstability) re-apply context ---
	// Populated by FTextureReimporter when a bounds-instability conflict is generated so the
	// conflict dialog's "Accept New" resolution can re-run the reimport with the stability
	// check bypassed (U16). Left at defaults for the Aseprite layer-reimport conflict types.
	TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset> SourceProfile;
	int32 SourceFlipbookIndex = INDEX_NONE;
	int32 SourceFrameIndex = INDEX_NONE; // Keyframe this conflict is about; lets "Accept New" apply per-frame.
	FString SourceTextureFilePath;
};

/** Result of an Aseprite reimport operation */
struct FAsepriteReimportResult
{
	int32 LayersUpdated = 0;
	int32 LayersAdded = 0;
	int32 LayersRemoved = 0;
	int32 TexturesUpdated = 0;
	int32 SpritesUpdated = 0;
	TArray<FReimportConflict> Conflicts;
	TArray<FString> Warnings;
	bool bSuccess = false;
};

/**
 * Diff-based Aseprite reimport engine.
 * Re-parses the .ase file, matches layers against existing CharacterLayerAsset
 * entries, and updates textures/sprites in-place. New layers are added, missing
 * layers with manual edits generate conflicts for user resolution.
 *
 * All mutations are wrapped in FScopedTransaction for single-step undo.
 */
class PAPER2DPLUSEDITOR_API FAsepriteReimporter
{
public:
	/**
	 * Reimport a CharacterLayerAsset from its source .ase file.
	 * @param AseFilePath Absolute disk path to the .ase/.aseprite file
	 * @param LayerAsset  The asset to update in-place
	 * @return Result with update counts, conflicts, and warnings
	 */
	static FAsepriteReimportResult ReimportFromAseFile(
		const FString& AseFilePath,
		UPaper2DPlusCharacterLayerAsset* LayerAsset
	);

	/** True when removing the imported Layer would discard curated generic appearance or gameplay state. */
	static bool HasManualEdits(
		const struct FCharacterLayer& Layer,
		const UPaper2DPlusCharacterLayerAsset& LayerAsset,
		int32 LayerIndex,
		int32 TotalLayers);

private:

	/** Build per-layer sprite sheet pixel data in grid layout for in-place texture update */
	static TArray<FColor> BuildGridPixelData(
		const TArray<TArray<FColor>>& FrameBuffers,
		int32 FrameWidth, int32 FrameHeight,
		int32 Columns, int32 Rows,
		int32 SheetWidth, int32 SheetHeight
	);
};
