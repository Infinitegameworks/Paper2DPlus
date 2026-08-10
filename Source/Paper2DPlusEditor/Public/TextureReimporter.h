// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AsepriteReimporter.h"  // FReimportConflict, EReimportConflictType

class UPaper2DPlusCharacterProfileAsset;

/**
 * Result of a texture reimport operation.
 */
struct FTextureReimportResult
{
	/** Number of sprites whose source regions were updated. */
	int32 SpritesUpdated = 0;

	/** Conflicts requiring user attention (e.g., uniform bounds instability). */
	TArray<FReimportConflict> Conflicts;

	/** Non-blocking warnings (e.g., frame count changes). */
	TArray<FString> Warnings;

	/** True if the reimport completed without fatal errors. */
	bool bSuccess = false;
};

/**
 * Static utility for texture-based sprite re-extraction.
 *
 * Re-detects sprites using stored detection params from FAlignmentMetadata,
 * checks uniform bounds stability, and updates sprites in-place.
 * Used by the auto-reimport pipeline when a source texture file changes on disk.
 *
 * Follows the three-phase mutation pattern:
 *   1. Preflight  - verify texture Source is readable
 *   2. Snapshot   - write CRC-validated sidecar for rollback
 *   3. Apply      - destructive write with rollback on failure
 */
class PAPER2DPLUSEDITOR_API FTextureReimporter
{
public:
	/**
	 * Re-extract sprites from a texture for a specific flipbook entry.
	 *
	 * @param TextureFilePath   Absolute disk path of the source texture file (for logging only).
	 * @param Profile           The CharacterProfile asset owning the flipbook.
	 * @param FlipbookIndex     Index into Profile->Flipbooks[] for the entry to update.
	 * @param bForceSkipStabilityCheck  When true, skips the uniform-bounds stability gate and
	 *                          applies the new bounds unconditionally. Used by the conflict
	 *                          dialog's "Accept New" resolution to honor a user override (U16).
	 * @param OnlyApplyFrames   When non-null (forced re-apply only), restricts the in-place update
	 *                          to these keyframe indices, so per-frame "Keep Current" choices on the
	 *                          same flipbook are preserved instead of being overwritten (U16).
	 * @return Result with update counts, conflicts, and warnings.
	 */
	static FTextureReimportResult ReimportFromTexture(
		const FString& TextureFilePath,
		UPaper2DPlusCharacterProfileAsset* Profile,
		int32 FlipbookIndex,
		bool bForceSkipStabilityCheck = false,
		const TSet<int32>* OnlyApplyFrames = nullptr);
};
