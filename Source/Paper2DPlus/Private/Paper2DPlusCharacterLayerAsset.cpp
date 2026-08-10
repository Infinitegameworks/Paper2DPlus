// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "Paper2DPlusCharacterLayerAsset.h"
#include "Paper2DPlusModule.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Paper2DPlusAppearanceResolver.h"
#include "Paper2DPlusAppearanceRenderPolicy.h"
#include "Paper2DPlusSettings.h"
#include "PaperFlipbook.h"

namespace
{
	template <typename TRow>
	bool BindAnimationRow(TRow& Row, const UPaper2DPlusCharacterProfileAsset* Profile, FString& LegacyName)
	{
		if (!Profile) return false;
		const FSoftObjectPath ExistingPath = Row.Flipbook.ToSoftObjectPath();
		const FFlipbookProfileEntry* Match = nullptr;
		for (const FFlipbookProfileEntry& Entry : Profile->Flipbooks)
		{
			const bool bPathMatch = !ExistingPath.IsNull()
				&& Entry.Identity.Flipbook.ToSoftObjectPath() == ExistingPath;
			const bool bNameMatch = ExistingPath.IsNull()
				&& Entry.Identity.FlipbookName.Equals(LegacyName, ESearchCase::IgnoreCase);
			if (!bPathMatch && !bNameMatch) continue;
			if (Match) return false;
			Match = &Entry;
		}
		if (!Match) return false;
		const bool bChanged = Row.Flipbook.ToSoftObjectPath() != Match->Identity.Flipbook.ToSoftObjectPath()
			|| LegacyName != Match->Identity.FlipbookName;
		Row.Flipbook = Match->Identity.Flipbook;
		LegacyName = Match->Identity.FlipbookName;
		return bChanged;
	}
}

UPaper2DPlusCharacterLayerAsset::UPaper2DPlusCharacterLayerAsset()
{
}

FPrimaryAssetId UPaper2DPlusCharacterLayerAsset::GetPrimaryAssetId() const
{
	return FPrimaryAssetId(TEXT("CharacterLayerAsset"), GetFName());
}

// ==========================================
// FCharacterLayer
// ==========================================

UPaperSprite* FCharacterLayer::GetSpriteForFrame(const FString& AnimName, int32 FrameIndex, bool bAllowSyncLoad) const
{
	const FCharacterLayerAnimationMapping* Mapping = FindAnimationMapping(AnimName);
	if (!Mapping) return nullptr;
	if (!Mapping->Sprites.IsValidIndex(FrameIndex)) return nullptr;
	// Audit F4: the per-frame playback path passes bAllowSyncLoad=false -> .Get() (already-warmed, no
	// game-thread stall). Editor/warmup callers keep the LoadSynchronous resolve.
	return bAllowSyncLoad ? Mapping->Sprites[FrameIndex].LoadSynchronous() : Mapping->Sprites[FrameIndex].Get();
}

void FCharacterLayer::WarmAnimationSprites(const FString& AnimName, TArray<TObjectPtr<UPaperSprite>>& OutWarmedSprites) const
{
	// Audit F4: resolve every soft sprite of this animation ONCE (flipbook-change path) so the per-frame
	// reads are load-free. The loaded pointers are APPENDED to OutWarmedSprites so the caller can root them —
	// a TSoftObjectPtr does NOT keep its target resident, so without a held hard ref a GC between this warm
	// and the load-free .Get() per-frame read would collect the not-yet-shown sprites (F4 re-review).
	if (const FCharacterLayerAnimationMapping* Mapping = FindAnimationMapping(AnimName))
	{
		for (const TSoftObjectPtr<UPaperSprite>& Soft : Mapping->Sprites)
		{
			if (!Soft.IsNull())
			{
				if (UPaperSprite* Loaded = Soft.LoadSynchronous())
				{
					OutWarmedSprites.Add(Loaded);
				}
			}
		}
	}
}

const FCharacterLayerAnimationMapping* FCharacterLayer::FindAnimationMapping(const FString& AnimName) const
{
	for (const FCharacterLayerAnimationMapping& Mapping : AnimationSprites)
	{
		if (Mapping.AnimationName.Equals(AnimName, ESearchCase::IgnoreCase))
		{
			return &Mapping;
		}
	}
	return nullptr;
}

// ==========================================
// UObject lifecycle + schema migration (TASK-62)
// ==========================================

void UPaper2DPlusCharacterLayerAsset::PostInitProperties()
{
	Super::PostInitProperties();

	// Generic-only assets always use the current schema.
	if (!HasAnyFlags(RF_ClassDefaultObject | RF_NeedLoad | RF_WasLoaded))
	{
		LayerSchemaVersion = CurrentLayerSchemaVersion;
#if WITH_EDITORONLY_DATA
		if (!BakeSetId.IsValid())
		{
			BakeSetId = FGuid::NewGuid();
		}
#endif
	}
}

void UPaper2DPlusCharacterLayerAsset::PostLoad()
{
	Super::PostLoad();

	// Warn once on pre-v5 assets whose wearable-era fields (Parts/Outfits/Variants/DefaultOutfitName/â€¦)
	// were removed without _DEPRECATED shells â€” their data is silently discarded on save (TASK-129,
	// deliberate). CDO never reaches PostLoad (PostInitProperties stamps it instead), so HasAnyFlags
	// checks here are unnecessary; the UPROPERTY default is 5, so only genuinely loaded older assets
	// will have LayerSchemaVersion < CurrentLayerSchemaVersion at this point.
	if (LayerSchemaVersion < CurrentLayerSchemaVersion)
	{
		UE_LOG(LogPaper2DPlus, Warning,
			TEXT("'%s': pre-v%u Layer schema (loaded v%u) â€” wearable-era Part/Outfit/Variant data is not "
				 "migrated and will be discarded on save. Re-author appearance presets in the Layer Workspace."),
			*GetPathName(), CurrentLayerSchemaVersion, LayerSchemaVersion);
	}

	LayerSchemaVersion = CurrentLayerSchemaVersion;

#if WITH_EDITOR
	// CookedGameplayAnimations is a runtime projection only. Rebuild it on editor load so
	// editor-only Cue track organization can never survive through a stale serialized copy.
	RebuildCookedGameplayData();
#endif
}

void UPaper2DPlusCharacterLayerAsset::PostDuplicate(EDuplicateMode::Type DuplicateMode)
{
	Super::PostDuplicate(DuplicateMode);

#if WITH_EDITORONLY_DATA
	// A copy is a new authoring source, never a second writer for the original's generated objects.
	BakeSetId = FGuid::NewGuid();
	BakeAttachmentState = ECharacterLayerBakeAttachmentState::Unclaimed;
	AnimationRegistration.Reset();
	BakeManifest = FCharacterLayerBakeManifest();
	LastBakeOperation = FCharacterLayerBakeOperationRecord();
	EnsureLayerAuthoringIdentity();
	RebuildCookedGameplayData();
#endif
}

void UPaper2DPlusCharacterLayerAsset::PostRename(UObject* OldOuter, const FName OldName)
{
	Super::PostRename(OldOuter, OldName);
#if WITH_EDITORONLY_DATA
	if (BakeAttachmentState == ECharacterLayerBakeAttachmentState::Attached && BakeSetId.IsValid())
	{
		if (UPaper2DPlusCharacterProfileAsset* Profile = BaseProfile.Get())
		{
			// The token is authority. Only its verified owner may refresh the diagnostic source-path hint.
			if (Profile->LayerBakeOwnerToken == BakeSetId && Profile->LayerBakeOwnerPathHint != GetPathName())
			{
				Profile->LayerBakeOwnerPathHint = GetPathName();
				Profile->MarkPackageDirty();
			}
		}
	}
#endif
}

#if WITH_EDITOR
void UPaper2DPlusCharacterLayerAsset::RebuildCookedGameplayData()
{
	const bool bCookLayerGameplay = UsageMode == ECharacterLayerUsageMode::RuntimeCustomizable;
	for (FCharacterLayer& Layer : Layers)
	{
		if (bCookLayerGameplay)
		{
			Layer.CookedGameplayAnimations = Layer.AuthoredAnimations;
#if WITH_EDITORONLY_DATA
			for (FCharacterLayerAuthoredAnimationData& Cooked : Layer.CookedGameplayAnimations)
			{
				Cooked.CueTrackLayout = FPaper2DPlusFrameCueTrackLayout();
			}
#endif
		}
		else
		{
			Layer.CookedGameplayAnimations.Reset();
		}
	}
}

void UPaper2DPlusCharacterLayerAsset::PreSave(FObjectPreSaveContext SaveContext)
{
	EnsureLayerAuthoringIdentity();
	RebuildCookedGameplayData();
	Super::PreSave(SaveContext);
}

bool UPaper2DPlusCharacterLayerAsset::Modify(bool bAlwaysMarkDirty)
{
	// See GetEditorContentRevision: authored Layer edits reach the asset through a transaction's
	// Modify(), including the Frame Cue timeline writes that add the first cue to a Layer animation.
	++EditorContentRevision;
	return Super::Modify(bAlwaysMarkDirty);
}

void UPaper2DPlusCharacterLayerAsset::PostEditUndo()
{
	// Undo restores bytes without calling Modify(), so advance the revision here as well.
	++EditorContentRevision;
	Super::PostEditUndo();
	RebuildCookedGameplayData();
}

void UPaper2DPlusCharacterLayerAsset::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	++EditorContentRevision;
	RebuildCookedGameplayData();
}
#endif

#if WITH_EDITOR
bool UPaper2DPlusCharacterLayerAsset::EnsureLayerAuthoringIdentity()
{
	bool bChanged = false;
#if WITH_EDITORONLY_DATA
	if (!BakeSetId.IsValid())
	{
		BakeSetId = FGuid::NewGuid();
		bChanged = true;
	}

	TSet<FGuid> UsedGroupIds;
	for (FCharacterLayerGroupInfo& Group : LayerGroups)
	{
		if (!Group.GroupId.IsValid() || UsedGroupIds.Contains(Group.GroupId))
		{
			Group.GroupId = FGuid::NewGuid();
			bChanged = true;
		}
		UsedGroupIds.Add(Group.GroupId);
	}

	TSet<FGuid> UsedLayerIds;
	const UPaper2DPlusCharacterProfileAsset* LoadedProfile = BaseProfile.Get();
	for (FCharacterLayer& Layer : Layers)
	{
		if (!Layer.LayerId.IsValid() || UsedLayerIds.Contains(Layer.LayerId))
		{
			Layer.LayerId = FGuid::NewGuid();
			bChanged = true;
		}
		UsedLayerIds.Add(Layer.LayerId);

		if (Layer.GroupId.IsValid() && !UsedGroupIds.Contains(Layer.GroupId))
		{
			Layer.GroupId.Invalidate();
			bChanged = true;
		}

		for (FCharacterLayerAnimationMapping& Mapping : Layer.AnimationSprites)
		{
			bChanged |= BindAnimationRow(Mapping, LoadedProfile, Mapping.AnimationName);
		}
		for (FCharacterLayerAnimationOffset& Offset : Layer.AnimationOffsets)
		{
			bChanged |= BindAnimationRow(Offset, LoadedProfile, Offset.AnimationName);
		}
		for (FCharacterLayerAuthoredAnimationData& Animation : Layer.AuthoredAnimations)
		{
			bChanged |= BindAnimationRow(Animation, LoadedProfile, Animation.LegacyAnimationName);
			FPaper2DPlusFrameCueReplacementMap CueReplacements;
			for (TObjectPtr<UPaper2DPlusCueBase>& Cue : Animation.FrameCues)
			{
				if (Cue && Cue->GetOuter() != this)
				{
					UPaper2DPlusCueBase* Previous = Cue.Get();
					Cue = DuplicateObject<UPaper2DPlusCueBase>(Previous, this);
					if (Cue)
					{
						CueReplacements.Add(Previous, Cue.Get());
					}
					bChanged = true;
				}
			}
			if (!CueReplacements.IsEmpty())
			{
				Animation.CueTrackLayout.RemapCueReferences(CueReplacements);
			}
			TSet<const UPaper2DPlusCueBase*> CueDomain;
			for (const UPaper2DPlusCueBase* Cue : Animation.FrameCues)
			{
				if (Cue) CueDomain.Add(Cue);
			}
			const int32 AssignmentCountBefore = Animation.CueTrackLayout.CueTrackIds.Num();
			Animation.CueTrackLayout.RetainCueAssignments(CueDomain);
			bChanged |= Animation.CueTrackLayout.CueTrackIds.Num() != AssignmentCountBefore;
		}
	}
#endif
	return bChanged;
}

FString UPaper2DPlusCharacterLayerAsset::ComputeLayerSourceDigest() const
{
	return Paper2DPlusLayerBake::ComputeSourceDigest(*this);
}

#endif // WITH_EDITOR

void UPaper2DPlusCharacterLayerAsset::SortVisibleLayersForEffectivePaintOrder(
	TArray<FString>& InOutVisibleLayers) const
{
	TMap<FString, int32> LayerIndexByKey;
	for (int32 LayerIndex = 0; LayerIndex < Layers.Num(); ++LayerIndex)
	{
		LayerIndexByKey.FindOrAdd(Layers[LayerIndex].LayerName.ToLower(), LayerIndex);
	}
	InOutVisibleLayers.StableSort([&LayerIndexByKey](const FString& A, const FString& B)
	{
		const int32* IndexA = LayerIndexByKey.Find(A.ToLower());
		const int32* IndexB = LayerIndexByKey.Find(B.ToLower());
		if (!IndexA || !IndexB) return IndexA != nullptr && IndexB == nullptr;
		return *IndexA < *IndexB;
	});
}

UPaperSprite* UPaper2DPlusCharacterLayerAsset::GetLayerSpriteForFrame(const FString& LayerName, const FString& AnimationName, int32 FrameIndex, bool bAllowSyncLoad) const
{
	const FCharacterLayer* Layer = GetLayerByName(LayerName);
	if (!Layer) return nullptr;
	return Layer->GetSpriteForFrame(AnimationName, FrameIndex, bAllowSyncLoad);
}

void UPaper2DPlusCharacterLayerAsset::WarmLayerSprites(const FString& AnimationName, TArray<TObjectPtr<UPaperSprite>>& OutWarmedSprites) const
{
	// Audit F4: warm EVERY layer's sprites for this animation in one pass (called on flipbook change), so
	// the subsequent per-frame UpdateLayerSprites reads are load-free. Loaded sprites are appended to
	// OutWarmedSprites for the caller to root (see WarmAnimationSprites' note on GC + soft refs).
	for (const FCharacterLayer& Layer : Layers)
	{
		Layer.WarmAnimationSprites(AnimationName, OutWarmedSprites);
	}
}

TArray<FString> UPaper2DPlusCharacterLayerAsset::GetAllLayerNames() const
{
	TArray<FString> Names;
	for (const FCharacterLayer& Layer : Layers)
	{
		Names.Add(Layer.LayerName);
	}
	return Names;
}

const FCharacterLayer* UPaper2DPlusCharacterLayerAsset::GetLayerByName(const FString& LayerName) const
{
	for (const FCharacterLayer& Layer : Layers)
	{
		if (Layer.LayerName.Equals(LayerName, ESearchCase::IgnoreCase))
		{
			return &Layer;
		}
	}
	return nullptr;
}

FCharacterLayer* UPaper2DPlusCharacterLayerAsset::GetLayerByNameMutable(const FString& LayerName)
{
	for (FCharacterLayer& Layer : Layers)
	{
		if (Layer.LayerName.Equals(LayerName, ESearchCase::IgnoreCase))
		{
			return &Layer;
		}
	}
	return nullptr;
}

TArray<FString> UPaper2DPlusCharacterLayerAsset::ResolveVisibleLayers(
	const FPaper2DPlusAppearanceDescriptor& Appearance,
	const FString& CurrentAnimationName) const
{
	return Paper2DPlusAppearanceResolver::ResolveVisibleLayers(this, Appearance, CurrentAnimationName);
}

#if WITH_EDITOR
const FCharacterLayer* UPaper2DPlusCharacterLayerAsset::GetLayerById(const FGuid& LayerId) const
{
#if WITH_EDITORONLY_DATA
	if (!LayerId.IsValid())
	{
		return nullptr;
	}
	for (const FCharacterLayer& Layer : Layers)
	{
		if (Layer.LayerId == LayerId)
		{
			return &Layer;
		}
	}
#endif
	return nullptr;
}

FCharacterLayer* UPaper2DPlusCharacterLayerAsset::GetLayerByIdMutable(const FGuid& LayerId)
{
#if WITH_EDITORONLY_DATA
	if (!LayerId.IsValid())
	{
		return nullptr;
	}
	for (FCharacterLayer& Layer : Layers)
	{
		if (Layer.LayerId == LayerId)
		{
			return &Layer;
		}
	}
#endif
	return nullptr;
}
#endif

TArray<FCharacterLayerValidationIssue> UPaper2DPlusCharacterLayerAsset::ValidateLayerAsset() const
{
	TArray<FCharacterLayerValidationIssue> Issues;
	for (const FString& Message : Paper2DPlusAppearanceResolver::ValidateGenericAppearanceSource(this))
	{
		FCharacterLayerValidationIssue& Issue = Issues.AddDefaulted_GetRef();
		Issue.Severity = ECharacterLayerValidationSeverity::Error;
		Issue.Message = Message;
	}
	return Issues;
}
