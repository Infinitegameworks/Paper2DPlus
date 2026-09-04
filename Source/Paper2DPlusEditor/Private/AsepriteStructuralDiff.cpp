// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "AsepriteStructuralDiff.h"

#include "AsepriteImporter.h"
#include "Misc/SecureHash.h"
#include "Paper2DPlusCharacterLayerAsset.h"

namespace
{
	// First occurrence wins — the pipeline's layer/tag matching rule. Matching is case-insensitive
	// for free: a TMap keyed by FString already hashes with Strihash and compares with Stricmp, so
	// no lower-cased copy is needed (and each one would be a wasted allocation per item).
	// Returns the deduped indices in input order plus the name -> index map.
	void AseDiff_DedupeByName(
		TFunctionRef<const FString&(int32)> NameAt, int32 Num,
		TArray<int32>& OutOrder, TMap<FString, int32>& OutByName)
	{
		OutOrder.Reset();
		OutByName.Reset();
		for (int32 Index = 0; Index < Num; ++Index)
		{
			const FString& Key = NameAt(Index);
			if (!OutByName.Contains(Key))
			{
				OutByName.Add(Key, Index);
				OutOrder.Add(Index);
			}
		}
	}
}

FAseDiffResult FAsepriteStructuralDiff::DiffItems(
	const TArray<FAseDiffOldItem>& OldItems,
	const TArray<FAseDiffNewItem>& NewItems)
{
	FAseDiffResult Result;

	TArray<int32> OldOrder;
	TMap<FString, int32> OldByName;
	AseDiff_DedupeByName([&OldItems](int32 i) -> const FString& { return OldItems[i].Name; },
		OldItems.Num(), OldOrder, OldByName);

	TArray<int32> NewOrder;
	TMap<FString, int32> NewByName;
	AseDiff_DedupeByName([&NewItems](int32 i) -> const FString& { return NewItems[i].Name; },
		NewItems.Num(), NewOrder, NewByName);

	// Split into matched / removed / added by case-insensitive name.
	TArray<int32> RemovedIdx;
	TArray<int32> AddedIdx;
	for (const int32 OldIndex : OldOrder)
	{
		if (NewByName.Contains(OldItems[OldIndex].Name))
		{
			Result.MatchedNames.Add(OldItems[OldIndex].Name);
		}
		else
		{
			RemovedIdx.Add(OldIndex);
		}
	}
	for (const int32 NewIndex : NewOrder)
	{
		if (!OldByName.Contains(NewItems[NewIndex].Name))
		{
			AddedIdx.Add(NewIndex);
		}
	}

	// Rename pairing: strict 1:1 by non-empty content hash. Shared-with-sibling items stay IN the
	// candidate buckets (their presence makes a same-hash pairing ambiguous — stricter is safer)
	// but can never themselves pair.
	TMap<FString, TArray<int32>> RemovedByHash;
	for (const int32 OldIndex : RemovedIdx)
	{
		if (!OldItems[OldIndex].ContentHash.IsEmpty())
		{
			RemovedByHash.FindOrAdd(OldItems[OldIndex].ContentHash).Add(OldIndex);
		}
	}
	TMap<FString, TArray<int32>> AddedByHash;
	for (const int32 NewIndex : AddedIdx)
	{
		if (!NewItems[NewIndex].ContentHash.IsEmpty())
		{
			AddedByHash.FindOrAdd(NewItems[NewIndex].ContentHash).Add(NewIndex);
		}
	}

	TSet<int32> PairedRemoved;
	TSet<int32> PairedAdded;
	for (const int32 OldIndex : RemovedIdx)
	{
		const FAseDiffOldItem& Old = OldItems[OldIndex];
		if (Old.ContentHash.IsEmpty() || Old.bSharedWithOtherSources)
		{
			continue;
		}
		const TArray<int32>* RemovedBucket = RemovedByHash.Find(Old.ContentHash);
		const TArray<int32>* AddedBucket = AddedByHash.Find(Old.ContentHash);
		if (RemovedBucket && AddedBucket && RemovedBucket->Num() == 1 && AddedBucket->Num() == 1)
		{
			const int32 NewIndex = (*AddedBucket)[0];
			if (NewItems[NewIndex].bCollidesWithExistingItem)
			{
				// The new name already belongs to something else on the asset (typically a sibling
				// source's layer or tag). Renaming onto it would collide on disk or silently merge
				// into that owner's data, so degrade to delete+add and report.
				continue;
			}
			Result.Renames.Add({ Old.Name, NewItems[NewIndex].Name });
			PairedRemoved.Add(OldIndex);
			PairedAdded.Add(NewIndex);
		}
	}

	for (const int32 OldIndex : RemovedIdx)
	{
		if (PairedRemoved.Contains(OldIndex))
		{
			continue;
		}
		const FAseDiffOldItem& Old = OldItems[OldIndex];
		FAseDiffRemoval Removal;
		Removal.Name = Old.Name;
		Removal.Disposition = Old.bSharedWithOtherSources
			? EAseDiffRemovalDisposition::KeepSharedWithOtherSource
			: (Old.bHasAuthoredData
				? EAseDiffRemovalDisposition::KeepAuthoredData
				: EAseDiffRemovalDisposition::RemoveClean);
		Result.Removals.Add(MoveTemp(Removal));
	}

	for (const int32 NewIndex : AddedIdx)
	{
		if (!PairedAdded.Contains(NewIndex))
		{
			Result.Added.Add(NewItems[NewIndex].Name);
		}
	}

	return Result;
}

bool FAsepriteStructuralDiff::MergeAnimationSprites(
	TArray<FCharacterLayerAnimationMapping>& Existing,
	const TArray<FCharacterLayerAnimationMapping>& Incoming,
	int32* OutRefreshed,
	int32* OutAdded)
{
	int32 Refreshed = 0;
	int32 Added = 0;
	bool bChanged = false;

	for (const FCharacterLayerAnimationMapping& In : Incoming)
	{
		FCharacterLayerAnimationMapping* Match = Existing.FindByPredicate(
			[&In](const FCharacterLayerAnimationMapping& E)
			{
				return E.AnimationName.Equals(In.AnimationName, ESearchCase::IgnoreCase);
			});

		if (!Match)
		{
			Existing.Add(In);
			++Added;
			bChanged = true;
			continue;
		}

		// True no-op refresh (same exact name, same sprite paths in order) must not count as a
		// change — R14's no-dirty no-op reimport depends on it.
		const bool bSameName = Match->AnimationName.Equals(In.AnimationName, ESearchCase::CaseSensitive);
		bool bSameSprites = Match->Sprites.Num() == In.Sprites.Num();
		if (bSameSprites)
		{
			for (int32 SpriteIndex = 0; SpriteIndex < In.Sprites.Num(); ++SpriteIndex)
			{
				if (Match->Sprites[SpriteIndex].ToSoftObjectPath() != In.Sprites[SpriteIndex].ToSoftObjectPath())
				{
					bSameSprites = false;
					break;
				}
			}
		}
#if WITH_EDITORONLY_DATA
		// An incoming canonical Flipbook binding must still be adopted even when the sprite list is
		// unchanged, or it would be silently dropped by the no-op short-circuit.
		const bool bSameFlipbook = In.Flipbook.IsNull()
			|| Match->Flipbook.ToSoftObjectPath() == In.Flipbook.ToSoftObjectPath();
#else
		const bool bSameFlipbook = true;
#endif
		if (bSameName && bSameSprites && bSameFlipbook)
		{
			continue;
		}

		Match->AnimationName = In.AnimationName;
		Match->Sprites = In.Sprites;
#if WITH_EDITORONLY_DATA
		// An authored Flipbook binding survives a refresh that carries none.
		if (!In.Flipbook.IsNull())
		{
			Match->Flipbook = In.Flipbook;
		}
#endif
		++Refreshed;
		bChanged = true;
	}

	if (OutRefreshed)
	{
		*OutRefreshed = Refreshed;
	}
	if (OutAdded)
	{
		*OutAdded = Added;
	}
	return bChanged;
}

int32 FAsepriteStructuralDiff::JoinNewLayersIntoPreset(
	const TArray<FCharacterLayer>& Layers,
	const TArray<FString>& NewLayerNames,
	FCharacterLayerAppearancePreset& Preset,
	TArray<FString>* OutRefusedLayerNames)
{
	int32 JoinedCount = 0;

	for (const FString& NewLayerName : NewLayerNames)
	{
		const FCharacterLayer* NewLayer = Layers.FindByPredicate([&NewLayerName](const FCharacterLayer& Layer)
		{
			return Layer.LayerName.Equals(NewLayerName, ESearchCase::IgnoreCase);
		});
		if (!NewLayer || Preset.ActiveLayerIds.Contains(NewLayer->LayerId))
		{
			continue;
		}

		if (NewLayer->ExclusiveGroupId.IsValid())
		{
			// Evaluate occupancy against the preset's LIVE contents so two new members of the same
			// group cannot both join within a single pass.
			const FGuid NewLayerGroupId = NewLayer->ExclusiveGroupId;
			const bool bGroupOccupied = Preset.ActiveLayerIds.ContainsByPredicate(
				[&Layers, &NewLayerGroupId](const FGuid& ActiveLayerId)
				{
					const FCharacterLayer* Active = Layers.FindByPredicate([&ActiveLayerId](const FCharacterLayer& Layer)
					{
						return Layer.LayerId == ActiveLayerId;
					});
					return Active && Active->ExclusiveGroupId == NewLayerGroupId;
				});
			if (bGroupOccupied)
			{
				if (OutRefusedLayerNames)
				{
					OutRefusedLayerNames->Add(NewLayer->LayerName);
				}
				continue;
			}
		}

		Preset.ActiveLayerIds.Add(NewLayer->LayerId);
		++JoinedCount;
	}

	return JoinedCount;
}

FString FAsepriteStructuralDiff::ComputeLayerBuffersHash(
	const TArray<TArray<FColor>>& FrameBuffers, int32 FrameWidth, int32 FrameHeight)
{
	FMD5 Md5;
	const int32 FrameCount = FrameBuffers.Num();
	Md5.Update(reinterpret_cast<const uint8*>(&FrameCount), sizeof(FrameCount));
	Md5.Update(reinterpret_cast<const uint8*>(&FrameWidth), sizeof(FrameWidth));
	Md5.Update(reinterpret_cast<const uint8*>(&FrameHeight), sizeof(FrameHeight));
	for (const TArray<FColor>& Frame : FrameBuffers)
	{
		const int32 PixelCount = Frame.Num();
		Md5.Update(reinterpret_cast<const uint8*>(&PixelCount), sizeof(PixelCount));
		if (PixelCount > 0)
		{
			Md5.Update(reinterpret_cast<const uint8*>(Frame.GetData()), PixelCount * sizeof(FColor));
		}
	}
	FMD5Hash Hash;
	Hash.Set(Md5);
	return LexToString(Hash);
}

FString FAsepriteStructuralDiff::ComputeTagFramesHash(const FAsepriteParsedData& Data, const FAsepriteTag& Tag)
{
	// Authored ascending range, clamped to the frames that actually exist. Durations are excluded
	// by design — pairing is pixel identity, and a retimed-but-identical tag must still pair.
	const int32 RangeFrom = FMath::Min(Tag.FromFrame, Tag.ToFrame);
	const int32 RangeTo = FMath::Max(Tag.FromFrame, Tag.ToFrame);

	TArray<int32> FrameIndices;
	for (int32 FrameIndex = RangeFrom; FrameIndex <= RangeTo; ++FrameIndex)
	{
		if (Data.Frames.IsValidIndex(FrameIndex))
		{
			FrameIndices.Add(FrameIndex);
		}
	}

	FMD5 Md5;
	const int32 FrameCount = FrameIndices.Num();
	Md5.Update(reinterpret_cast<const uint8*>(&FrameCount), sizeof(FrameCount));
	for (const int32 FrameIndex : FrameIndices)
	{
		const FAsepriteFrame& Frame = Data.Frames[FrameIndex];
		Md5.Update(reinterpret_cast<const uint8*>(&Frame.Width), sizeof(Frame.Width));
		Md5.Update(reinterpret_cast<const uint8*>(&Frame.Height), sizeof(Frame.Height));
		const int32 PixelCount = Frame.Pixels.Num();
		Md5.Update(reinterpret_cast<const uint8*>(&PixelCount), sizeof(PixelCount));
		if (PixelCount > 0)
		{
			Md5.Update(reinterpret_cast<const uint8*>(Frame.Pixels.GetData()), PixelCount * sizeof(FColor));
		}
	}
	FMD5Hash Hash;
	Hash.Set(Md5);
	return LexToString(Hash);
}

// ============================================
// FAseDiffApplyReport — non-modal reporting (R13)
// ============================================

FString FAseDiffApplyReport::ToSummaryText() const
{
	TArray<FString> Parts;
	const int32 RenameCount = LayerRenames.Num() + TagRenames.Num();
	if (RenameCount > 0)
	{
		Parts.Add(FString::Printf(TEXT("%d rename(s)"), RenameCount));
	}
	if (KeptItems.Num() > 0)
	{
		Parts.Add(FString::Printf(TEXT("%d kept"), KeptItems.Num()));
	}
	if (RemovedItems.Num() > 0)
	{
		Parts.Add(FString::Printf(TEXT("%d removed"), RemovedItems.Num()));
	}
	if (OrphanedAssets.Num() > 0)
	{
		Parts.Add(FString::Printf(TEXT("%d orphan(s)"), OrphanedAssets.Num()));
	}
	if (DegradedRenames.Num() > 0)
	{
		Parts.Add(FString::Printf(TEXT("%d refused rename(s)"), DegradedRenames.Num()));
	}
	if (PresetJoinRefusals.Num() > 0)
	{
		Parts.Add(FString::Printf(TEXT("%d preset join refusal(s)"), PresetJoinRefusals.Num()));
	}
	return FString::Join(Parts, TEXT(", "));
}

void FAseDiffApplyReport::AppendDetailLines(TArray<FString>& OutLines) const
{
	for (const FAseDiffRename& Rename : LayerRenames)
	{
		OutLines.Add(FString::Printf(TEXT("Renamed layer '%s' to '%s' (pixel-identical, unambiguous); its generated assets moved on disk and left redirectors."),
			*Rename.OldName, *Rename.NewName));
	}
	for (const FAseDiffRename& Rename : TagRenames)
	{
		OutLines.Add(FString::Printf(TEXT("Renamed animation '%s' to '%s'; authored gameplay, timing and placement data followed the name."),
			*Rename.OldName, *Rename.NewName));
	}
	OutLines.Append(DegradedRenames);
	OutLines.Append(KeptItems);
	OutLines.Append(RemovedItems);
	for (const FString& Orphan : OrphanedAssets)
	{
		OutLines.Add(FString::Printf(TEXT("Orphaned generated asset (left on disk, nothing was deleted): %s"), *Orphan));
	}
	OutLines.Append(PresetJoinRefusals);
}

// ============================================
// Authored-data detection (R10)
// ============================================

bool FAsepriteStructuralDiff::HasAuthoredLayerData(
	const FCharacterLayer& Layer,
	const TArray<FCharacterLayerAppearancePreset>& Presets)
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
	for (const FCharacterLayerAppearancePreset& Preset : Presets)
	{
		if (Preset.ActiveLayerIds.Contains(Layer.LayerId))
		{
			return true;
		}
	}
	return false;
}

bool FAsepriteStructuralDiff::HasAuthoredAnimationData(
	const TArray<FCharacterLayer>& Layers,
	const FString& AnimationName)
{
	for (const FCharacterLayer& Layer : Layers)
	{
		for (const FCharacterLayerAnimationOffset& Offset : Layer.AnimationOffsets)
		{
			if (Offset.AnimationName.Equals(AnimationName, ESearchCase::IgnoreCase))
			{
				return true;
			}
		}
#if WITH_EDITORONLY_DATA
		for (const FCharacterLayerAuthoredAnimationData& Authored : Layer.AuthoredAnimations)
		{
			if (Authored.LegacyAnimationName.Equals(AnimationName, ESearchCase::IgnoreCase))
			{
				return true;
			}
		}
#endif
	}
	return false;
}

// ============================================
// Animation re-key / removal (R9, R10)
// ============================================

EAseLayerRenamePlan FAsepriteStructuralDiff::PlanLayerRename(
	const TArray<FCharacterLayer>& Layers,
	const FString& OldName,
	const FString& NewName,
	int32* OutLayerIndex)
{
	if (OutLayerIndex)
	{
		*OutLayerIndex = INDEX_NONE;
	}

	const int32 ExistingIndex = Layers.IndexOfByPredicate([&OldName](const FCharacterLayer& Layer)
	{
		return Layer.LayerName.Equals(OldName, ESearchCase::IgnoreCase);
	});
	if (ExistingIndex == INDEX_NONE)
	{
		return EAseLayerRenamePlan::OldNameNotFound;
	}

	// Checked against the WHOLE array, not just "some other layer": if the creation pass has already
	// minted the new name, this is exactly the collision the ordering rule exists to prevent.
	if (Layers.ContainsByPredicate([&NewName](const FCharacterLayer& Layer)
	{
		return Layer.LayerName.Equals(NewName, ESearchCase::IgnoreCase);
	}))
	{
		return EAseLayerRenamePlan::NewNameAlreadyOwned;
	}

	if (OutLayerIndex)
	{
		*OutLayerIndex = ExistingIndex;
	}
	return EAseLayerRenamePlan::Applicable;
}

int32 FAsepriteStructuralDiff::RenameAnimationOnLayers(
	TArray<FCharacterLayer>& Layers,
	const FString& OldAnimationName,
	const FString& NewAnimationName,
	TArray<FString>* OutCollidedLayerNames)
{
	int32 RowsRenamed = 0;
	if (OldAnimationName.IsEmpty() || NewAnimationName.IsEmpty()
		|| OldAnimationName.Equals(NewAnimationName, ESearchCase::CaseSensitive))
	{
		return 0;
	}

	for (FCharacterLayer& Layer : Layers)
	{
		// A layer that already owns a row under the NEW name cannot take the rename: writing it
		// would leave two rows claiming one animation and silently merge their authored data.
		// Refuse per layer and report — the caller degrades the pairing to delete+add.
		const bool bCollides = Layer.AnimationSprites.ContainsByPredicate(
			[&NewAnimationName](const FCharacterLayerAnimationMapping& Mapping)
			{
				return Mapping.AnimationName.Equals(NewAnimationName, ESearchCase::IgnoreCase);
			})
			|| Layer.AnimationOffsets.ContainsByPredicate(
			[&NewAnimationName](const FCharacterLayerAnimationOffset& Offset)
			{
				return Offset.AnimationName.Equals(NewAnimationName, ESearchCase::IgnoreCase);
			});
		if (bCollides)
		{
			if (OutCollidedLayerNames)
			{
				OutCollidedLayerNames->AddUnique(Layer.LayerName);
			}
			continue;
		}

		for (FCharacterLayerAnimationMapping& Mapping : Layer.AnimationSprites)
		{
			if (Mapping.AnimationName.Equals(OldAnimationName, ESearchCase::IgnoreCase))
			{
				Mapping.AnimationName = NewAnimationName;
				++RowsRenamed;
			}
		}
		for (FCharacterLayerAnimationOffset& Offset : Layer.AnimationOffsets)
		{
			if (Offset.AnimationName.Equals(OldAnimationName, ESearchCase::IgnoreCase))
			{
				Offset.AnimationName = NewAnimationName;
				++RowsRenamed;
			}
		}
#if WITH_EDITORONLY_DATA
		for (FCharacterLayerAuthoredAnimationData& Authored : Layer.AuthoredAnimations)
		{
			if (Authored.LegacyAnimationName.Equals(OldAnimationName, ESearchCase::IgnoreCase))
			{
				Authored.LegacyAnimationName = NewAnimationName;
				++RowsRenamed;
			}
		}
#endif
	}
	return RowsRenamed;
}

int32 FAsepriteStructuralDiff::RemoveAnimationFromLayers(
	TArray<FCharacterLayer>& Layers,
	const FString& AnimationName)
{
	int32 RowsRemoved = 0;
	if (AnimationName.IsEmpty())
	{
		return 0;
	}
	for (FCharacterLayer& Layer : Layers)
	{
		RowsRemoved += Layer.AnimationSprites.RemoveAll(
			[&AnimationName](const FCharacterLayerAnimationMapping& Mapping)
			{
				return Mapping.AnimationName.Equals(AnimationName, ESearchCase::IgnoreCase);
			});
	}
	return RowsRemoved;
}

bool FAsepriteStructuralDiff::RemoveLayerByName(
	TArray<FCharacterLayer>& Layers,
	TArray<FCharacterLayerAppearancePreset>& Presets,
	const FString& LayerName,
	FGuid* OutRemovedLayerId)
{
	const int32 Index = Layers.IndexOfByPredicate([&LayerName](const FCharacterLayer& Layer)
	{
		return Layer.LayerName.Equals(LayerName, ESearchCase::IgnoreCase);
	});
	if (Index == INDEX_NONE)
	{
		return false;
	}

	const FGuid RemovedId = Layers[Index].LayerId;
	if (OutRemovedLayerId)
	{
		*OutRemovedLayerId = RemovedId;
	}
	Layers.RemoveAt(Index);

	// A preset must never keep a LayerId that no longer exists — that is exactly the dangling
	// reference the completeness rules reject.
	for (FCharacterLayerAppearancePreset& Preset : Presets)
	{
		Preset.ActiveLayerIds.Remove(RemovedId);
	}
	return true;
}
