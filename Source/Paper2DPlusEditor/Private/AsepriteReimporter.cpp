// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "AsepriteReimporter.h"
#include "AsepriteImporter.h"
#include "Paper2DPlusCharacterLayerAsset.h"
#include "SpriteExtractionUtils.h"
#include "Engine/Texture2D.h"
#include "ScopedTransaction.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "PaperSprite.h"

#define LOCTEXT_NAMESPACE "AsepriteReimporter"

// ============================================
// Grid layout helpers
// ============================================

TArray<FColor> FAsepriteReimporter::BuildGridPixelData(
	const TArray<TArray<FColor>>& FrameBuffers,
	int32 FrameWidth, int32 FrameHeight,
	int32 Columns, int32 Rows,
	int32 SheetWidth, int32 SheetHeight)
{
	const int32 TotalPixels = SheetWidth * SheetHeight;
	TArray<FColor> Pixels;
	Pixels.SetNumZeroed(TotalPixels);

	for (int32 FrameIdx = 0; FrameIdx < FrameBuffers.Num(); FrameIdx++)
	{
		const TArray<FColor>& FrameBuffer = FrameBuffers[FrameIdx];

		const int32 Col = FrameIdx % Columns;
		const int32 Row = FrameIdx / Columns;
		const int32 OffsetX = Col * FrameWidth;
		const int32 OffsetY = Row * FrameHeight;

		for (int32 Y = 0; Y < FrameHeight; Y++)
		{
			for (int32 X = 0; X < FrameWidth; X++)
			{
				const int32 SrcIdx = Y * FrameWidth + X;
				if (SrcIdx >= FrameBuffer.Num())
				{
					continue;
				}
				Pixels[(OffsetY + Y) * SheetWidth + (OffsetX + X)] = FrameBuffer[SrcIdx];
			}
		}
	}

	return Pixels;
}

// ============================================
// HasManualEdits
// ============================================

bool FAsepriteReimporter::HasManualEdits(
	const FCharacterLayer& Layer,
	const UPaper2DPlusCharacterLayerAsset& LayerAsset,
	int32 LayerIndex,
	int32 TotalLayers)
{
#if WITH_EDITORONLY_DATA
	// Organization and layer-local gameplay are curated source and never disposable import art.
	if (Layer.GroupId.IsValid()
		|| !Layer.AuthoredAnimations.IsEmpty())
	{
		return true;
	}
#endif
	if (!Layer.DefaultOffsetPx.IsNearlyZero()
		|| !Layer.AnimationOffsets.IsEmpty()
		|| Layer.CompositionMode != ECharacterLayerCompositionMode::Layer
		|| Layer.ExclusiveGroupId.IsValid())
	{
		return true;
	}

	// Generic presets retain stable IDs even when import art disappears. Keeping the missing Layer as a
	// conflict makes the broken curated reference visible to validation instead of silently rewriting a preset.
	for (const FCharacterLayerAppearancePreset& Preset : LayerAsset.AppearancePresets)
	{
		if (Preset.ActiveLayerIds.Contains(Layer.LayerId))
		{
			return true;
		}
	}
	return false;
}

// ============================================
// ReimportFromAseFile
// ============================================

FAsepriteReimportResult FAsepriteReimporter::ReimportFromAseFile(
	const FString& AseFilePath,
	UPaper2DPlusCharacterLayerAsset* LayerAsset)
{
	FAsepriteReimportResult Result;

	if (!LayerAsset)
	{
		Result.Warnings.Add(TEXT("LayerAsset is null"));
		return Result;
	}

	if (AseFilePath.IsEmpty() || !FPaths::FileExists(AseFilePath))
	{
		Result.Warnings.Add(FString::Printf(TEXT("Source .ase file not found: %s"), *AseFilePath));
		return Result;
	}

	// --- Step 1: Parse the .ase file ---
	FAsepriteParsedData NewParsedData;
	FString ParseError;
	if (!FAsepriteImporter::ParseFile(AseFilePath, NewParsedData, ParseError))
	{
		Result.Warnings.Add(FString::Printf(TEXT("Failed to parse .ase file: %s"), *ParseError));
		return Result;
	}

	// --- Step 2: Composite per-layer ---
	TMap<int32, TArray<TArray<FColor>>> PerLayerBuffers = FAsepriteImporter::CompositePerLayer(NewParsedData);

	// --- Step 3: Build layer match lists ---

	// Build a map from full hierarchy path to new layer index for O(1) lookups
	TMap<FString, int32> NewPathToIndex;
	for (const FAsepriteLayerNode& Node : NewParsedData.LayerHierarchy)
	{
		if (Node.LayerIndex >= 0 && NewParsedData.Layers.IsValidIndex(Node.LayerIndex))
		{
			const FAsepriteLayer& Layer = NewParsedData.Layers[Node.LayerIndex];
			// Skip group layers and hitbox layers (same filter as CompositePerLayer)
			if (Layer.LayerType == 1) continue;
			bool bIsHitbox = false;
			for (const FAsepriteHitboxLayer& HL : NewParsedData.HitboxLayers)
			{
				if (HL.LayerIndex == Node.LayerIndex) { bIsHitbox = true; break; }
			}
			if (bIsHitbox) continue;

			NewPathToIndex.Add(Node.FullPath, Node.LayerIndex);
		}
	}

	// Track which new layers have been matched
	TSet<int32> MatchedNewIndices;

	struct FLayerMatch
	{
		int32 OldIndex;    // Index into LayerAsset->Layers
		int32 NewLayerIdx; // Index into NewParsedData.Layers
		FString Path;
	};

	TArray<FLayerMatch> Matched;
	TArray<int32> MissingOldIndices; // Indices into LayerAsset->Layers with no match

	// Match existing layers by full path
	for (int32 OldIdx = 0; OldIdx < LayerAsset->Layers.Num(); OldIdx++)
	{
		const FCharacterLayer& OldLayer = LayerAsset->Layers[OldIdx];
		const int32* FoundNewIdx = NewPathToIndex.Find(OldLayer.LayerName);

		if (FoundNewIdx)
		{
			FLayerMatch Match;
			Match.OldIndex = OldIdx;
			Match.NewLayerIdx = *FoundNewIdx;
			Match.Path = OldLayer.LayerName;
			Matched.Add(Match);
			MatchedNewIndices.Add(*FoundNewIdx);
		}
		else
		{
			MissingOldIndices.Add(OldIdx);
		}
	}

	// --- Group rename detection ---
	// If a group was renamed but children have the same leaf names under the new group,
	// treat as rename: update LayerName on matched children, don't generate conflict.
	if (MissingOldIndices.Num() > 0)
	{
		// Build leaf-name to new-path lookup for unmatched new layers
		TMultiMap<FString, int32> LeafToUnmatchedNew;
		for (const auto& Pair : NewPathToIndex)
		{
			if (!MatchedNewIndices.Contains(Pair.Value))
			{
				// Extract leaf name (after last /)
				FString LeafName;
				int32 SlashIdx;
				if (Pair.Key.FindLastChar(TEXT('/'), SlashIdx))
				{
					LeafName = Pair.Key.Mid(SlashIdx + 1);
				}
				else
				{
					LeafName = Pair.Key;
				}
				LeafToUnmatchedNew.Add(LeafName, Pair.Value);
			}
		}

		// Try to match missing old layers by leaf name
		TArray<int32> StillMissing;
		for (int32 OldIdx : MissingOldIndices)
		{
			const FCharacterLayer& OldLayer = LayerAsset->Layers[OldIdx];

			// Extract leaf name from old path
			FString OldLeaf;
			int32 SlashIdx;
			if (OldLayer.LayerName.FindLastChar(TEXT('/'), SlashIdx))
			{
				OldLeaf = OldLayer.LayerName.Mid(SlashIdx + 1);
			}
			else
			{
				OldLeaf = OldLayer.LayerName;
			}

			// Find candidates by leaf name
			TArray<int32> Candidates;
			LeafToUnmatchedNew.MultiFind(OldLeaf, Candidates);

			if (Candidates.Num() == 1 && !MatchedNewIndices.Contains(Candidates[0]))
			{
				// Unique leaf match — treat as group rename
				int32 NewIdx = Candidates[0];

				// Find the new full path
				FString NewPath;
				for (const auto& Pair : NewPathToIndex)
				{
					if (Pair.Value == NewIdx)
					{
						NewPath = Pair.Key;
						break;
					}
				}

				FLayerMatch Match;
				Match.OldIndex = OldIdx;
				Match.NewLayerIdx = NewIdx;
				Match.Path = NewPath; // Will update the layer name to new path
				Matched.Add(Match);
				MatchedNewIndices.Add(NewIdx);

				UE_LOG(LogTemp, Log, TEXT("AsepriteReimporter: Group rename detected — '%s' -> '%s'"),
					*OldLayer.LayerName, *NewPath);
			}
			else
			{
				StillMissing.Add(OldIdx);
			}
		}
		MissingOldIndices = MoveTemp(StillMissing);
	}

	// Collect new layer indices (not matched to any existing layer)
	TArray<int32> NewLayerIndices;
	for (const auto& Pair : NewPathToIndex)
	{
		if (!MatchedNewIndices.Contains(Pair.Value))
		{
			NewLayerIndices.Add(Pair.Value);
		}
	}

	// NOT a transaction (matches the importer's recipe in AsepriteImporter.cpp): this rebuilds the
	// texture Source bulk data in place (Source.Init/LockMip/Memcpy/ForceRebuildPlatformData below)
	// and creates new asset packages (CreatePerLayerSpriteSheetTexture/CreatePerLayerSprites) —
	// neither bulk data nor package creation is undoable, so an FScopedTransaction here makes undo
	// mismatch and orphans the created packages. We still SetFlags(RF_Transactional)+Modify()+
	// MarkPackageDirty (Step 7) so OnObjectModified fires for the editor reconcile.
	LayerAsset->SetFlags(RF_Transactional);
	LayerAsset->Modify();

	// --- Step 4: Process matched layers (in-place update) ---
	for (const FLayerMatch& Match : Matched)
	{
		FCharacterLayer& OldLayer = LayerAsset->Layers[Match.OldIndex];

		// Update layer name if it changed (group rename)
		if (OldLayer.LayerName != Match.Path)
		{
			UE_LOG(LogTemp, Log, TEXT("AsepriteReimporter: Updating layer path '%s' -> '%s'"),
				*OldLayer.LayerName, *Match.Path);
			OldLayer.LayerName = Match.Path;

		}

		// Check if pixel data changed: compare frame count and tag count as fast heuristic
		const TArray<TArray<FColor>>* NewFrameBuffers = PerLayerBuffers.Find(Match.NewLayerIdx);
		if (!NewFrameBuffers || NewFrameBuffers->Num() == 0)
		{
			continue;
		}

		const int32 NewFrameCount = NewFrameBuffers->Num();

		// Dimension-aware grid for the regenerated sheet — computed once and shared by the texture
		// rebuild and the sprite-region regeneration below so they cannot disagree (and matches what
		// CreatePerLayerSprites produces). If even the widest legal packing overflows the max texture
		// dimension, skip this layer's reimport entirely (leave its texture + mappings intact) rather
		// than overflow the byte math or wipe the layer's sprites.
		const FSpriteSheetGrid Grid = FSpriteSheetGrid::Compute(NewFrameCount, NewParsedData.Width, NewParsedData.Height);
		if (!Grid.bValid)
		{
			Result.Warnings.Add(FString::Printf(
				TEXT("Layer '%s': %d frames of %dx%d exceed the maximum texture dimension; skipped its reimport."),
				*OldLayer.LayerName, NewFrameCount, NewParsedData.Width, NewParsedData.Height));
			continue;
		}

		// Fast path: check if tag structure and frame count match the existing data
		bool bStructureChanged = false;

		// Count existing UNIQUE frames, not the sum of per-tag sprite counts. The per-layer
		// sheet holds exactly one sprite per frame (see CreatePerLayerSprites), but overlapping
		// or gapped Aseprite tags reference the same sprite from several animation mappings.
		// Summing Mapping.Sprites.Num() double-counts those shared frames and trips a
		// false-positive structure change on a normal reimport (U15).
		TSet<UPaperSprite*> UniqueOldSprites;
		UPaperSprite* FirstOldSprite = nullptr;
		for (const FCharacterLayerAnimationMapping& Mapping : OldLayer.AnimationSprites)
		{
			for (const TSoftObjectPtr<UPaperSprite>& SpritePtr : Mapping.Sprites)
			{
				if (UPaperSprite* OldSprite = SpritePtr.LoadSynchronous())
				{
					UniqueOldSprites.Add(OldSprite);
					if (!FirstOldSprite)
					{
						FirstOldSprite = OldSprite;
					}
				}
			}
		}
		const int32 OldUniqueFrameCount = UniqueOldSprites.Num();

		if (NewFrameCount != OldUniqueFrameCount)
		{
			bStructureChanged = true;
		}

		// Check tag count. CreatePerLayerSprites drops any tag whose clamped frame range is
		// empty, so the existing mapping count already excludes those. Count only the new tags
		// that would survive the same drop — otherwise a degenerate (empty-range) tag in the
		// .ase reports a false structure change (U15).
		const int32 OldTagCount = OldLayer.AnimationSprites.Num();
		int32 NewTagCount;
		if (NewParsedData.Tags.Num() > 0)
		{
			NewTagCount = 0;
			for (const FAsepriteTag& NewTag : NewParsedData.Tags)
			{
				const int32 From = FMath::Clamp(NewTag.FromFrame, 0, NewFrameCount - 1);
				const int32 To = FMath::Clamp(NewTag.ToFrame, 0, NewFrameCount - 1);
				if (From <= To)
				{
					NewTagCount++;
				}
			}
		}
		else
		{
			NewTagCount = 1; // No tags -> a single default animation mapping
		}
		if (OldTagCount != NewTagCount)
		{
			bStructureChanged = true;
		}

		// Detect a frame-size (cell dimension) change even when frame and tag counts are
		// unchanged. The sheet is regenerated at NewParsedData.Width x Height every reimport,
		// so if the cell size changed the existing sprite source regions point at the wrong
		// texels until rebuilt. Treat it as a structure change so the regions and mappings are
		// recomputed for the new grid (U14).
		if (!bStructureChanged && FirstOldSprite)
		{
			const FVector2D ExistingSize = FirstOldSprite->GetSourceSize();
			const int32 ExistingW = FMath::RoundToInt(ExistingSize.X);
			const int32 ExistingH = FMath::RoundToInt(ExistingSize.Y);
			if (ExistingW != NewParsedData.Width || ExistingH != NewParsedData.Height)
			{
				bStructureChanged = true;
			}
		}

		// Always update texture pixel data (we can't cheaply compare pixels, and the
		// artist saved the file — presumably something changed)
		{
			UTexture2D* Texture = OldLayer.SourceTexture.LoadSynchronous();
			if (Texture)
			{
				const int32 Columns = Grid.Columns;
				const int32 Rows = Grid.Rows;
				const int32 SheetWidth = Grid.SheetW;
				const int32 SheetHeight = Grid.SheetH;

				TArray<FColor> NewPixels = BuildGridPixelData(
					*NewFrameBuffers,
					NewParsedData.Width, NewParsedData.Height,
					Columns, Rows,
					SheetWidth, SheetHeight
				);

				// In-place texture update (Pattern A from PadTextureInPlace)
				Texture->SetFlags(RF_Transactional);
				Texture->Modify();
				Texture->Source.Init(SheetWidth, SheetHeight, 1, 1, TSF_BGRA8);
				{
					uint8* DestData = Texture->Source.LockMip(0);
					FMemory::Memcpy(DestData, NewPixels.GetData(), static_cast<int64>(SheetWidth) * SheetHeight * sizeof(FColor));
					Texture->Source.UnlockMip(0);
				}
				Texture->ForceRebuildPlatformData();
				Texture->ReleaseResource();
				Texture->UpdateResource();
				Texture->MarkPackageDirty();

				Result.TexturesUpdated++;

				// Update sprite source regions if grid layout changed
				if (bStructureChanged)
				{
					// Rebuild sprites: update existing, create new if frame count increased.
					// Sprites are repositioned on the NEW grid (Columns/NewParsedData), and the
					// animation mappings are fully regenerated by CreatePerLayerSprites below.
					int32 SpriteIdx = 0;
					for (FCharacterLayerAnimationMapping& Mapping : OldLayer.AnimationSprites)
					{
						for (int32 i = 0; i < Mapping.Sprites.Num(); i++)
						{
							UPaperSprite* Sprite = Mapping.Sprites[i].LoadSynchronous();
							if (Sprite && SpriteIdx < NewFrameCount)
							{
								const FIntRect NewBounds = Grid.GetCellRect(SpriteIdx);

								Sprite->SetFlags(RF_Transactional);
								Sprite->Modify();
								FSpriteExtractionUtils::UpdateSpriteSourceRegion(Sprite, Texture, NewBounds);
								Result.SpritesUpdated++;
							}
							SpriteIdx++;
						}
					}
				}
			}
		}

		// Update animation mappings if tag structure changed
		if (bStructureChanged)
		{
			// Determine the output path and prefix from the existing texture asset
			UTexture2D* ExistingTexture = OldLayer.SourceTexture.LoadSynchronous();
			if (ExistingTexture)
			{
				// Get the package path for new sprite creation
				FString TexturePackagePath = ExistingTexture->GetOutermost()->GetName();
				FString OutputPath = FPaths::GetPath(TexturePackagePath);

				// Build a sprite prefix from the layer name
				FString SanitizedLayerName = OldLayer.LayerName;
				SanitizedLayerName.ReplaceInline(TEXT(" "), TEXT("_"));
				SanitizedLayerName.ReplaceInline(TEXT("/"), TEXT("_"));

				// Try to infer asset prefix from existing texture name
				FString TextureName = ExistingTexture->GetName();
				FString SpritePrefix;
				if (TextureName.EndsWith(TEXT("_Sheet")))
				{
					SpritePrefix = TextureName.LeftChop(6); // Remove "_Sheet"
				}
				else
				{
					SpritePrefix = TextureName + TEXT("_") + SanitizedLayerName;
				}

				// Recreate per-layer sprites using the existing functions
				TArray<FCharacterLayerAnimationMapping> NewMappings =
					FAsepriteImporter::CreatePerLayerSprites(
						ExistingTexture, NewParsedData, OutputPath, SpritePrefix);

				// Preserve frame events (do NOT touch FrameEvents)
				// Guard against an empty regeneration silently wiping the layer's sprites.
				if (NewMappings.Num() > 0)
				{
					OldLayer.AnimationSprites = MoveTemp(NewMappings);
				}
			}
		}
		else
		{
			// Even if structure didn't change, check for tag name renames
			if (NewParsedData.Tags.Num() > 0)
			{
				for (int32 TagIdx = 0; TagIdx < NewParsedData.Tags.Num() && TagIdx < OldLayer.AnimationSprites.Num(); TagIdx++)
				{
					const FAsepriteTag& NewTag = NewParsedData.Tags[TagIdx];
					FCharacterLayerAnimationMapping& OldMapping = OldLayer.AnimationSprites[TagIdx];

					if (OldMapping.AnimationName != NewTag.Name)
					{
						UE_LOG(LogTemp, Log, TEXT("AsepriteReimporter: Tag renamed '%s' -> '%s'"),
							*OldMapping.AnimationName, *NewTag.Name);
						OldMapping.AnimationName = NewTag.Name;
					}
				}
			}
		}

		Result.LayersUpdated++;
	}

	// --- Step 5: Process new layers ---
	if (NewLayerIndices.Num() > 0)
	{
		// Determine output path from existing layers or asset
		FString OutputPath;
		FString AssetPrefix;

		if (LayerAsset->Layers.Num() > 0)
		{
			UTexture2D* FirstTexture = LayerAsset->Layers[0].SourceTexture.LoadSynchronous();
			if (FirstTexture)
			{
				FString TexturePackagePath = FirstTexture->GetOutermost()->GetName();
				OutputPath = FPaths::GetPath(TexturePackagePath);

				// Infer asset prefix from texture name
				FString TextureName = FirstTexture->GetName();
				int32 UnderscoreIdx;
				if (TextureName.FindChar(TEXT('_'), UnderscoreIdx))
				{
					AssetPrefix = TextureName.Left(UnderscoreIdx);
				}
				else
				{
					AssetPrefix = LayerAsset->DisplayName;
				}
			}
		}

		if (OutputPath.IsEmpty())
		{
			FString AssetPackagePath = LayerAsset->GetOutermost()->GetName();
			OutputPath = FPaths::GetPath(AssetPackagePath);
			AssetPrefix = LayerAsset->DisplayName;
		}

		for (int32 NewLayerIdx : NewLayerIndices)
		{
			if (!NewParsedData.Layers.IsValidIndex(NewLayerIdx))
			{
				continue;
			}

			const FAsepriteLayer& NewLayer = NewParsedData.Layers[NewLayerIdx];
			const TArray<TArray<FColor>>* LayerBuffers = PerLayerBuffers.Find(NewLayerIdx);
			if (!LayerBuffers || LayerBuffers->Num() == 0)
			{
				continue;
			}

			// Get full path from hierarchy
			FString LayerPath;
			for (const FAsepriteLayerNode& Node : NewParsedData.LayerHierarchy)
			{
				if (Node.LayerIndex == NewLayerIdx)
				{
					LayerPath = Node.FullPath;
					break;
				}
			}
			if (LayerPath.IsEmpty())
			{
				LayerPath = NewLayer.Name;
			}

			// Sanitize for asset naming
			FString SanitizedLayerName = NewLayer.Name;
			SanitizedLayerName.ReplaceInline(TEXT(" "), TEXT("_"));
			SanitizedLayerName.ReplaceInline(TEXT("/"), TEXT("_"));

			FString TextureName = AssetPrefix + TEXT("_") + SanitizedLayerName + TEXT("_Sheet");
			FString SpritePrefix = AssetPrefix + TEXT("_") + SanitizedLayerName;

			// Create per-layer sprite sheet texture
			UTexture2D* LayerTexture = FAsepriteImporter::CreatePerLayerSpriteSheetTexture(
				*LayerBuffers, NewParsedData.Width, NewParsedData.Height, OutputPath, TextureName);

			if (!LayerTexture)
			{
				Result.Warnings.Add(FString::Printf(TEXT("Failed to create texture for new layer '%s'"), *LayerPath));
				continue;
			}

			// Create per-layer sprites
			TArray<FCharacterLayerAnimationMapping> Mappings =
				FAsepriteImporter::CreatePerLayerSprites(
					LayerTexture, NewParsedData, OutputPath, SpritePrefix);

			// Build the new FCharacterLayer
			FCharacterLayer CharLayer;
			CharLayer.LayerName = LayerPath;
			CharLayer.SourceTexture = LayerTexture;
			CharLayer.AnimationSprites = MoveTemp(Mappings);

			LayerAsset->Layers.Add(MoveTemp(CharLayer));
			Result.LayersAdded++;

			UE_LOG(LogTemp, Log, TEXT("AsepriteReimporter: Added new layer '%s' at global order index %d"),
				*LayerPath, LayerAsset->Layers.Num() - 1);
		}
	}

	// --- Step 6: Process missing layers (conflict detection) ---
	// Reverse iterate so removal indices stay valid
	for (int32 i = MissingOldIndices.Num() - 1; i >= 0; i--)
	{
		int32 OldIdx = MissingOldIndices[i];
		if (!LayerAsset->Layers.IsValidIndex(OldIdx))
		{
			continue;
		}

		const FCharacterLayer& MissingLayer = LayerAsset->Layers[OldIdx];

		if (HasManualEdits(MissingLayer, *LayerAsset, OldIdx, LayerAsset->Layers.Num()))
		{
			// Generate conflict — do not auto-remove
			FReimportConflict Conflict;
			Conflict.Type = EReimportConflictType::LayerDeleted;
			Conflict.OldName = MissingLayer.LayerName;
			Conflict.Description = FString::Printf(
				TEXT("Layer '%s' was removed from the .ase file but has curated UE data (membership, include/group/placement/gameplay, legacy events, visibility, or order)."),
				*MissingLayer.LayerName);
			Result.Conflicts.Add(MoveTemp(Conflict));

			UE_LOG(LogTemp, Log, TEXT("AsepriteReimporter: Conflict — layer '%s' deleted in .ase but has manual edits"),
				*MissingLayer.LayerName);
		}
		else
		{
			// Remove silently
			UE_LOG(LogTemp, Log, TEXT("AsepriteReimporter: Removing unedited missing layer '%s'"),
				*MissingLayer.LayerName);
			LayerAsset->Layers.RemoveAt(OldIdx);
			Result.LayersRemoved++;
		}
	}

	// --- Step 7: Finalize ---
	LayerAsset->EnsureLayerAuthoringIdentity();
	LayerAsset->MarkPackageDirty();
	Result.bSuccess = true;

	UE_LOG(LogTemp, Log, TEXT("AsepriteReimporter: Reimport complete — %d updated, %d added, %d removed, %d conflicts, %d textures, %d sprites"),
		Result.LayersUpdated, Result.LayersAdded, Result.LayersRemoved,
		Result.Conflicts.Num(), Result.TexturesUpdated, Result.SpritesUpdated);

	return Result;
}

#undef LOCTEXT_NAMESPACE
