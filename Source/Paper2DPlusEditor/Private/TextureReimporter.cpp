// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "TextureReimporter.h"
#include "SpriteExtractionUtils.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Engine/Texture2D.h"
#include "PaperSprite.h"
#include "PaperFlipbook.h"
#include "ScopedTransaction.h"
#include "Misc/Paths.h"

/** FTextureReimporter — Re-detects sprites from a texture using stored detection params, checks uniform bounds stability, and updates sprites in-place. */

FTextureReimportResult FTextureReimporter::ReimportFromTexture(
	const FString& TextureFilePath,
	UPaper2DPlusCharacterProfileAsset* Profile,
	int32 FlipbookIndex,
	bool bForceSkipStabilityCheck,
	const TSet<int32>* OnlyApplyFrames)
{
	FTextureReimportResult Result;

	// ==========================================
	// Step 1: Validate inputs
	// ==========================================
	if (!Profile)
	{
		Result.Warnings.Add(TEXT("Null CharacterProfile asset."));
		return Result;
	}

	if (!Profile->Flipbooks.IsValidIndex(FlipbookIndex))
	{
		Result.Warnings.Add(FString::Printf(TEXT("Invalid FlipbookIndex %d (asset has %d entries)."),
			FlipbookIndex, Profile->Flipbooks.Num()));
		return Result;
	}

	FFlipbookProfileEntry& Entry = Profile->Flipbooks[FlipbookIndex];

	if (Entry.SourceTexture.IsNull())
	{
		Result.Warnings.Add(FString::Printf(TEXT("Flipbook '%s' has no SourceTexture set."),
			*Entry.Identity.FlipbookName));
		return Result;
	}

	// ==========================================
	// Step 2: Load the texture
	// ==========================================
	UTexture2D* Texture = Entry.SourceTexture.LoadSynchronous();
	if (!Texture)
	{
		Result.Warnings.Add(FString::Printf(TEXT("Failed to load SourceTexture for flipbook '%s'."),
			*Entry.Identity.FlipbookName));
		return Result;
	}

	// ==========================================
	// Step 3: Preflight — verify Source data readable
	// ==========================================
	const FIntPoint TexDims = FSpriteExtractionUtils::GetDetectionDimensions(Texture);
	if (TexDims.X <= 0 || TexDims.Y <= 0)
	{
		Result.Warnings.Add(FString::Printf(TEXT("Texture '%s' has zero detection dimensions (%dx%d). Source data may be unreadable."),
			*Texture->GetName(), TexDims.X, TexDims.Y));
		return Result;
	}

	TArray<FColor> PreflightPixels;
	int32 PreflightW = 0, PreflightH = 0;
	if (!FSpriteExtractionUtils::LoadTextureData(Texture, PreflightPixels, PreflightW, PreflightH))
	{
		Result.Warnings.Add(FString::Printf(TEXT("Failed to load texture data for '%s'. Source data may be missing or corrupted."),
			*Texture->GetName()));
		return Result;
	}

	// ==========================================
	// Step 4: Build detection params from stored FAlignmentMetadata
	// ==========================================
	const FAlignmentMetadata& AlignmentData = Entry.AlignmentData;

	FSpriteDetectionParams Params;
	if (AlignmentData.IsValid())
	{
		Params.AlphaThreshold = AlignmentData.AlphaThreshold;
		Params.MinSpriteSize = AlignmentData.MinSpriteSize;
		Params.IslandMergeDistance = AlignmentData.IslandMergeDistance;
	}
	else
	{
		// Fall back to profile-level defaults when no per-flipbook metadata exists
		Params.AlphaThreshold = Profile->DefaultAlphaThreshold;
		Params.MinSpriteSize = Profile->DefaultMinSpriteSize;
		// IslandMergeDistance keeps FSpriteDetectionParams default (2)
		Result.Warnings.Add(FString::Printf(TEXT("Flipbook '%s' has no stored AlignmentMetadata. Using profile defaults for re-detection."),
			*Entry.Identity.FlipbookName));
	}
	// bUse8DirectionalFloodFill is not stored on FAlignmentMetadata — default to true
	Params.bUse8DirectionalFloodFill = true;

	// ==========================================
	// Step 5: Re-detect sprites
	// ==========================================
	TArray<FDetectedSprite> DetectedSprites = FSpriteExtractionUtils::DetectSpriteBounds(Texture, Params);

	if (DetectedSprites.Num() == 0)
	{
		Result.Warnings.Add(FString::Printf(TEXT("Re-detection found zero sprites in texture '%s'."),
			*Texture->GetName()));
		return Result;
	}

	// ==========================================
	// Step 6: Compute uniform bounds (requires >= 2 sprites)
	// ==========================================
	if (DetectedSprites.Num() >= 2)
	{
		FSpriteExtractionUtils::ComputeUniformBounds(DetectedSprites, TexDims.X, TexDims.Y);
	}

	// ==========================================
	// Step 7: Stability check — compare midpoints
	// ==========================================
	UPaperFlipbook* Flipbook = Entry.Identity.Flipbook.LoadSynchronous();
	if (!Flipbook)
	{
		Result.Warnings.Add(FString::Printf(TEXT("Failed to load flipbook for '%s'."),
			*Entry.Identity.FlipbookName));
		return Result;
	}

	const int32 NumKeyFrames = Flipbook->GetNumKeyFrames();
	const int32 NumDetected = DetectedSprites.Num();

	// Frame count change check (step 9, done early for informational purposes)
	if (NumDetected != NumKeyFrames)
	{
		Result.Warnings.Add(FString::Printf(
			TEXT("Frame count changed for '%s': %d keyframes in flipbook, %d sprites detected. Updating matched frames by index."),
			*Entry.Identity.FlipbookName, NumKeyFrames, NumDetected));
	}

	// Compare midpoints of existing sprites against new detected bounds
	const int32 NumToCompare = FMath::Min(NumKeyFrames, NumDetected);
	bool bHasInstability = false;

	for (int32 K = 0; K < NumToCompare; K++)
	{
		const FPaperFlipbookKeyFrame& KF = Flipbook->GetKeyFrameChecked(K);
		UPaperSprite* Sprite = KF.Sprite;
		if (!Sprite) continue;

		// Current sprite midpoint (integer-rounded to match convergence pattern from ApplyCrossSheetAlignment)
		const FVector2D UVFloat = Sprite->GetSourceUV();
		const FVector2D DimFloat = Sprite->GetSourceSize();
		const FIntPoint CurrentUV(FMath::RoundToInt(UVFloat.X), FMath::RoundToInt(UVFloat.Y));
		const FIntPoint CurrentDim(FMath::RoundToInt(DimFloat.X), FMath::RoundToInt(DimFloat.Y));
		const FIntPoint OldMidpoint = CurrentUV + CurrentDim / 2;

		// New detected midpoint
		const FIntRect& NewBounds = DetectedSprites[K].Bounds;
		const FIntPoint NewMidpoint = NewBounds.Min + FIntPoint(NewBounds.Width(), NewBounds.Height()) / 2;

		const int32 DeltaX = FMath::Abs(OldMidpoint.X - NewMidpoint.X);
		const int32 DeltaY = FMath::Abs(OldMidpoint.Y - NewMidpoint.Y);

		if (DeltaX > 1 || DeltaY > 1)
		{
			bHasInstability = true;

			FReimportConflict Conflict;
			Conflict.Type = EReimportConflictType::UniformBoundsInstability;
			Conflict.OldName = FString::Printf(TEXT("Sprite %d (%s) midpoint (%d, %d)"),
				K, *Sprite->GetName(), OldMidpoint.X, OldMidpoint.Y);
			Conflict.NewName = FString::Printf(TEXT("Sprite %d midpoint (%d, %d)"),
				K, NewMidpoint.X, NewMidpoint.Y);
			Conflict.Description = FString::Printf(
				TEXT("Uniform bounds midpoint shifted by (%d, %d) px for sprite %d in '%s'. This may indicate a structural change in the sprite sheet."),
				DeltaX, DeltaY, K, *Entry.Identity.FlipbookName);
			// Carry the re-apply context so the conflict dialog's "Accept New" resolution
			// can re-run this reimport with the stability gate bypassed (U16).
			Conflict.SourceProfile = Profile;
			Conflict.SourceFlipbookIndex = FlipbookIndex;
			Conflict.SourceFrameIndex = K;
			Conflict.SourceTextureFilePath = TextureFilePath;

			Result.Conflicts.Add(MoveTemp(Conflict));
		}
	}

	// If any midpoint instability was detected, report conflicts but do NOT proceed with update,
	// UNLESS the caller (conflict dialog "Accept New") forces the update past the stability gate (U16).
	if (bHasInstability && !bForceSkipStabilityCheck)
	{
		Result.bSuccess = false;
		return Result;
	}

	// ==========================================
	// Step 8: In-place sprite update (wrapped in transaction for undo)
	// ==========================================
	{
		FScopedTransaction Transaction(FText::FromString(FString::Printf(
			TEXT("Reimport Texture: %s"), *Entry.Identity.FlipbookName)));

		Profile->Modify();

		const int32 NumToUpdate = FMath::Min(NumKeyFrames, NumDetected);
		for (int32 K = 0; K < NumToUpdate; K++)
		{
			// Forced re-apply may restrict to the frames the user accepted, leaving the rest
			// (resolved "Keep Current") untouched (U16).
			if (OnlyApplyFrames && !OnlyApplyFrames->Contains(K))
			{
				continue;
			}

			const FPaperFlipbookKeyFrame& KF = Flipbook->GetKeyFrameChecked(K);
			UPaperSprite* Sprite = KF.Sprite;
			if (!Sprite) continue;

			const FIntRect& NewBounds = DetectedSprites[K].Bounds;

			// Compute current source region (integer-rounded)
			const FVector2D UVFloat = Sprite->GetSourceUV();
			const FVector2D DimFloat = Sprite->GetSourceSize();
			const FIntPoint CurrentUV(FMath::RoundToInt(UVFloat.X), FMath::RoundToInt(UVFloat.Y));
			const FIntPoint CurrentDim(FMath::RoundToInt(DimFloat.X), FMath::RoundToInt(DimFloat.Y));
			const FIntRect CurrentRect(CurrentUV, CurrentUV + CurrentDim);

			// Convergence guard: skip if bounds are identical
			if (NewBounds == CurrentRect)
			{
				continue;
			}

			// ArtShiftDelta = how far art content moved within the sprite.
			// Same delta pattern as ApplyCrossSheetAlignment: old min - new min.
			const FIntPoint ArtShiftDelta = CurrentRect.Min - NewBounds.Min;

			Sprite->Modify();
			FSpriteExtractionUtils::UpdateSpriteSourceRegion(Sprite, Texture, NewBounds, ArtShiftDelta);

			// Update FrameExtractionInfo if available (mirrors ApplyCrossSheetAlignment pattern)
			if (Entry.CombatData.FrameExtractionInfo.IsValidIndex(K))
			{
				FSpriteExtractionInfo& Info = Entry.CombatData.FrameExtractionInfo[K];
				Info.SourceOffset = NewBounds.Min;
				Info.ExtractionTime = FDateTime::Now();
				if (ArtShiftDelta != FIntPoint::ZeroValue)
				{
					Info.SpriteOffset = FIntPoint::ZeroValue;
				}
			}

			// Remap hitboxes and sockets if art shifted (same pattern as ApplyCrossSheetAlignment)
			if (Entry.CombatData.Frames.IsValidIndex(K) && ArtShiftDelta != FIntPoint::ZeroValue)
			{
				FFrameHitboxData& FrameData = Entry.CombatData.Frames[K];
				for (FHitboxData& H : FrameData.Hitboxes)
				{
					H.X += ArtShiftDelta.X;
					H.Y += ArtShiftDelta.Y;
				}
				for (FSocketData& S : FrameData.Sockets)
				{
					S.X += ArtShiftDelta.X;
					S.Y += ArtShiftDelta.Y;
				}
			}

			Sprite->MarkPackageDirty();
			Result.SpritesUpdated++;
		}

		// ==========================================
		// Step 10: Mark profile dirty
		// ==========================================
		Profile->MarkPackageDirty();
	}

	Result.bSuccess = true;
	return Result;
}
