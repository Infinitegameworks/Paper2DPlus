// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "Paper2DPlusAppearanceResolver.h"

#include "Paper2DPlusCharacterLayerAsset.h"

namespace
{
	bool Fail(FString* OutReason, const FString& Reason)
	{
		if (OutReason) *OutReason = Reason;
		return false;
	}

	bool GameplayMatchesAnimation(const FCharacterLayerAuthoredAnimationData& Gameplay, const FString& AnimationName)
	{
		if (!Gameplay.LegacyAnimationName.IsEmpty())
		{
			return Gameplay.LegacyAnimationName.Equals(AnimationName, ESearchCase::IgnoreCase);
		}
		return !Gameplay.Flipbook.IsNull()
			&& Gameplay.Flipbook.ToSoftObjectPath().GetAssetName().Equals(AnimationName, ESearchCase::IgnoreCase);
	}
}

bool Paper2DPlusAppearanceResolver::UsesGenericAppearanceSchema(
	const UPaper2DPlusCharacterLayerAsset* LayerAsset)
{
	return LayerAsset != nullptr;
}

TArray<FString> Paper2DPlusAppearanceResolver::ValidateGenericAppearanceSource(
	const UPaper2DPlusCharacterLayerAsset* LayerAsset)
{
	TArray<FString> Issues;
	if (!LayerAsset)
	{
		Issues.Add(TEXT("A Character Layer Asset is required."));
		return Issues;
	}

	TSet<FGuid> LayerIds;
	for (int32 Index = 0; Index < LayerAsset->Layers.Num(); ++Index)
	{
		const FCharacterLayer& Layer = LayerAsset->Layers[Index];
		if (!Layer.LayerId.IsValid())
		{
			Issues.Add(FString::Printf(TEXT("Layer[%d] '%s' has an invalid Layer ID."), Index, *Layer.LayerName));
		}
		else if (LayerIds.Contains(Layer.LayerId))
		{
			Issues.Add(FString::Printf(TEXT("Layer[%d] '%s' duplicates Layer ID %s."),
				Index, *Layer.LayerName, *Layer.LayerId.ToString()));
		}
		LayerIds.Add(Layer.LayerId);
	}

	TSet<FGuid> GroupIds;
	for (int32 Index = 0; Index < LayerAsset->ExclusiveGroups.Num(); ++Index)
	{
		const FCharacterLayerExclusiveGroup& Group = LayerAsset->ExclusiveGroups[Index];
		if (!Group.GroupId.IsValid())
		{
			Issues.Add(FString::Printf(TEXT("Exclusive Group[%d] '%s' has an invalid ID."), Index, *Group.DisplayName));
		}
		else if (GroupIds.Contains(Group.GroupId))
		{
			Issues.Add(FString::Printf(TEXT("Exclusive Group[%d] '%s' duplicates ID %s."),
				Index, *Group.DisplayName, *Group.GroupId.ToString()));
		}
		GroupIds.Add(Group.GroupId);
	}
	for (const FCharacterLayer& Layer : LayerAsset->Layers)
	{
		if (Layer.ExclusiveGroupId.IsValid() && !GroupIds.Contains(Layer.ExclusiveGroupId))
		{
			Issues.Add(FString::Printf(TEXT("Layer '%s' references missing Exclusive Group %s."),
				*Layer.LayerName, *Layer.ExclusiveGroupId.ToString()));
		}
	}

	TSet<FGuid> PresetIds;
	for (int32 Index = 0; Index < LayerAsset->AppearancePresets.Num(); ++Index)
	{
		const FCharacterLayerAppearancePreset& Preset = LayerAsset->AppearancePresets[Index];
		if (!Preset.PresetId.IsValid())
		{
			Issues.Add(FString::Printf(TEXT("Appearance Preset[%d] '%s' has an invalid ID."), Index, *Preset.DisplayName));
		}
		else if (PresetIds.Contains(Preset.PresetId))
		{
			Issues.Add(FString::Printf(TEXT("Appearance Preset[%d] '%s' duplicates ID %s."),
				Index, *Preset.DisplayName, *Preset.PresetId.ToString()));
		}
		PresetIds.Add(Preset.PresetId);
		TArray<FGuid> Normalized;
		FString Reason;
		if (!NormalizeLayerSelection(LayerAsset, Preset.ActiveLayerIds, Normalized, &Reason)
			|| Normalized != Preset.ActiveLayerIds)
		{
			Issues.Add(FString::Printf(TEXT("Appearance Preset '%s' is invalid: %s"),
				*Preset.DisplayName, Reason.IsEmpty() ? TEXT("Layer IDs are not in global Layer order.") : *Reason));
		}
	}
	if (!LayerAsset->DefaultAppearancePresetId.IsValid())
	{
		Issues.Add(TEXT("A valid Default Appearance preset is required."));
	}
	else if (!PresetIds.Contains(LayerAsset->DefaultAppearancePresetId))
	{
		Issues.Add(FString::Printf(TEXT("Default Appearance references missing preset %s."),
			*LayerAsset->DefaultAppearancePresetId.ToString()));
	}
	Issues.Sort();
	return Issues;
}

bool Paper2DPlusAppearanceResolver::NormalizeLayerSelection(
	const UPaper2DPlusCharacterLayerAsset* LayerAsset,
	const TArray<FGuid>& RequestedLayerIds,
	TArray<FGuid>& OutNormalizedLayerIds,
	FString* OutReason)
{
	OutNormalizedLayerIds.Reset();
	if (!LayerAsset) return Fail(OutReason, TEXT("A Character Layer Asset is required."));
	TSet<FGuid> Requested;
	for (const FGuid& LayerId : RequestedLayerIds)
	{
		if (!LayerId.IsValid()) return Fail(OutReason, TEXT("Appearance contains an invalid Layer ID."));
		if (Requested.Contains(LayerId)) return Fail(OutReason, TEXT("Appearance contains a duplicate Layer ID."));
		if (!LayerAsset->FindLayerById(LayerId)) return Fail(OutReason, TEXT("Appearance references an unknown Layer ID."));
		Requested.Add(LayerId);
	}
	TSet<FGuid> ActiveGroups;
	for (const FCharacterLayer& Layer : LayerAsset->Layers)
	{
		if (!Requested.Contains(Layer.LayerId)) continue;
		if (Layer.ExclusiveGroupId.IsValid())
		{
			if (!LayerAsset->GetExclusiveGroupById(Layer.ExclusiveGroupId))
				return Fail(OutReason, TEXT("Appearance references a missing Exclusive Group."));
			if (ActiveGroups.Contains(Layer.ExclusiveGroupId))
				return Fail(OutReason, TEXT("Appearance activates more than one Layer in an Exclusive Group."));
			ActiveGroups.Add(Layer.ExclusiveGroupId);
		}
		OutNormalizedLayerIds.Add(Layer.LayerId);
	}
	if (OutReason) OutReason->Reset();
	return true;
}

bool Paper2DPlusAppearanceResolver::ApplyLayerActivation(
	const UPaper2DPlusCharacterLayerAsset* LayerAsset,
	const TArray<FGuid>& CurrentLayerIds,
	const FGuid& LayerId,
	bool bActive,
	TArray<FGuid>& OutNormalizedLayerIds,
	FString* OutReason)
{
	TArray<FGuid> Working;
	if (!NormalizeLayerSelection(LayerAsset, CurrentLayerIds, Working, OutReason)) return false;
	const FCharacterLayer* Target = LayerAsset ? LayerAsset->FindLayerById(LayerId) : nullptr;
	if (!Target) return Fail(OutReason, TEXT("A known Layer ID is required."));
	Working.Remove(LayerId);
	if (bActive)
	{
		if (Target->ExclusiveGroupId.IsValid())
		{
			Working.RemoveAll([LayerAsset, Target](const FGuid& ActiveId)
			{
				const FCharacterLayer* Active = LayerAsset->FindLayerById(ActiveId);
				return Active && Active->ExclusiveGroupId == Target->ExclusiveGroupId;
			});
		}
		Working.Add(LayerId);
	}
	return NormalizeLayerSelection(LayerAsset, Working, OutNormalizedLayerIds, OutReason);
}

bool Paper2DPlusAppearanceResolver::BuildPresetDescriptor(
	const UPaper2DPlusCharacterLayerAsset* LayerAsset,
	const FGuid& PresetId,
	FPaper2DPlusAppearanceDescriptor& OutDescriptor,
	FString* OutReason)
{
	const FCharacterLayerAppearancePreset* Preset =
		LayerAsset ? LayerAsset->GetAppearancePresetById(PresetId) : nullptr;
	if (!Preset) return Fail(OutReason, TEXT("A known Appearance Preset ID is required."));
	TArray<FGuid> Normalized;
	if (!NormalizeLayerSelection(LayerAsset, Preset->ActiveLayerIds, Normalized, OutReason)) return false;
	OutDescriptor = FPaper2DPlusAppearanceDescriptor();
	OutDescriptor.DeliveryMode = LayerAsset->UsageMode;
	OutDescriptor.ActiveLayerIds = MoveTemp(Normalized);
	return true;
}

bool Paper2DPlusAppearanceResolver::BuildDefaultDescriptor(
	const UPaper2DPlusCharacterLayerAsset* LayerAsset,
	FPaper2DPlusAppearanceDescriptor& OutDescriptor,
	FString* OutReason)
{
	return LayerAsset && LayerAsset->DefaultAppearancePresetId.IsValid()
		? BuildPresetDescriptor(LayerAsset, LayerAsset->DefaultAppearancePresetId, OutDescriptor, OutReason)
		: Fail(OutReason, TEXT("The Character Layer Asset has no valid Default Appearance."));
}

TArray<FGuid> Paper2DPlusAppearanceResolver::ResolveContributingArtLayerIds(
	const UPaper2DPlusCharacterLayerAsset* LayerAsset,
	const FPaper2DPlusAppearanceDescriptor& Descriptor,
	const FString& AnimationName)
{
	TArray<FGuid> Selected;
	if (!IsCompatible(Descriptor, LayerAsset)
		|| !NormalizeLayerSelection(LayerAsset, Descriptor.ActiveLayerIds, Selected)) return {};
	Selected.RemoveAll([LayerAsset, &AnimationName](const FGuid& Id)
	{
		const FCharacterLayer* Layer = LayerAsset->FindLayerById(Id);
		return !Layer || !Layer->FindAnimationMapping(AnimationName);
	});
	return Selected;
}

TArray<FGuid> Paper2DPlusAppearanceResolver::ResolveContributingGameplayLayerIds(
	const UPaper2DPlusCharacterLayerAsset* LayerAsset,
	const FPaper2DPlusAppearanceDescriptor& Descriptor,
	const FString& AnimationName)
{
	TArray<FGuid> Selected;
	if (!IsCompatible(Descriptor, LayerAsset)
		|| !NormalizeLayerSelection(LayerAsset, Descriptor.ActiveLayerIds, Selected)) return {};
	Selected.RemoveAll([LayerAsset, &AnimationName](const FGuid& Id)
	{
		const FCharacterLayer* Layer = LayerAsset->FindLayerById(Id);
		return !Layer || !Layer->CookedGameplayAnimations.ContainsByPredicate(
			[&AnimationName](const FCharacterLayerAuthoredAnimationData& Data)
			{ return GameplayMatchesAnimation(Data, AnimationName); });
	});
	return Selected;
}

void Paper2DPlusAppearanceResolver::Normalize(FPaper2DPlusAppearanceDescriptor& Descriptor)
{
	if (Descriptor.SemanticVersion <= Paper2DPlusAppearanceVersion::Current)
		Descriptor.SemanticVersion = Paper2DPlusAppearanceVersion::Current;
}

TArray<FString> Paper2DPlusAppearanceResolver::ResolveVisibleLayers(
	const UPaper2DPlusCharacterLayerAsset* LayerAsset,
	const FPaper2DPlusAppearanceDescriptor& Descriptor,
	const FString& AnimationName)
{
	TArray<FString> Result;
	for (const FGuid& Id : ResolveContributingArtLayerIds(LayerAsset, Descriptor, AnimationName))
	{
		if (const FCharacterLayer* Layer = LayerAsset->FindLayerById(Id)) Result.Add(Layer->LayerName);
	}
	return Result;
}

bool Paper2DPlusAppearanceResolver::IsCompatible(
	const FPaper2DPlusAppearanceDescriptor& Descriptor,
	const UPaper2DPlusCharacterLayerAsset* LayerAsset,
	FString* OutReason)
{
	if (!LayerAsset) return Fail(OutReason, TEXT("A Character Layer Asset is required."));
	if (Descriptor.SemanticVersion != Paper2DPlusAppearanceVersion::Current)
		return Fail(OutReason, TEXT("Appearance semantic version is not current."));
	if (Descriptor.DeliveryMode != LayerAsset->UsageMode)
		return Fail(OutReason, TEXT("Appearance delivery mode does not match the Layer Asset."));
	TArray<FGuid> Normalized;
	if (!NormalizeLayerSelection(LayerAsset, Descriptor.ActiveLayerIds, Normalized, OutReason)) return false;
	if (Normalized != Descriptor.ActiveLayerIds)
		return Fail(OutReason, TEXT("Appearance Layer IDs are not in global Layer order."));
	if (OutReason) OutReason->Reset();
	return true;
}

bool Paper2DPlusAppearanceResolver::AllowsRuntimeMutation(ECharacterLayerUsageMode DeliveryMode)
{
	return DeliveryMode == ECharacterLayerUsageMode::RuntimeCustomizable;
}

bool Paper2DPlusAppearanceResolver::UsesRuntimeLayerRenderer(ECharacterLayerUsageMode DeliveryMode)
{
	return DeliveryMode == ECharacterLayerUsageMode::RuntimeCustomizable;
}

bool Paper2DPlusAppearanceResolver::AllowsGameplayComposition(ECharacterLayerUsageMode DeliveryMode)
{
	return DeliveryMode == ECharacterLayerUsageMode::RuntimeCustomizable;
}
