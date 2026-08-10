// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Paper2DPlusAppearanceTypes.h"

class UPaper2DPlusCharacterLayerAsset;

/** Pure generic Layer appearance validation, normalization, and contribution queries. */
namespace Paper2DPlusAppearanceResolver
{
	PAPER2DPLUS_API bool UsesGenericAppearanceSchema(const UPaper2DPlusCharacterLayerAsset* LayerAsset);
	PAPER2DPLUS_API TArray<FString> ValidateGenericAppearanceSource(const UPaper2DPlusCharacterLayerAsset* LayerAsset);
	PAPER2DPLUS_API bool NormalizeLayerSelection(
		const UPaper2DPlusCharacterLayerAsset* LayerAsset,
		const TArray<FGuid>& RequestedLayerIds,
		TArray<FGuid>& OutNormalizedLayerIds,
		FString* OutReason = nullptr);
	PAPER2DPLUS_API bool ApplyLayerActivation(
		const UPaper2DPlusCharacterLayerAsset* LayerAsset,
		const TArray<FGuid>& CurrentLayerIds,
		const FGuid& LayerId,
		bool bActive,
		TArray<FGuid>& OutNormalizedLayerIds,
		FString* OutReason = nullptr);
	PAPER2DPLUS_API bool BuildPresetDescriptor(
		const UPaper2DPlusCharacterLayerAsset* LayerAsset,
		const FGuid& PresetId,
		FPaper2DPlusAppearanceDescriptor& OutDescriptor,
		FString* OutReason = nullptr);
	PAPER2DPLUS_API bool BuildDefaultDescriptor(
		const UPaper2DPlusCharacterLayerAsset* LayerAsset,
		FPaper2DPlusAppearanceDescriptor& OutDescriptor,
		FString* OutReason = nullptr);
	PAPER2DPLUS_API TArray<FGuid> ResolveContributingArtLayerIds(
		const UPaper2DPlusCharacterLayerAsset* LayerAsset,
		const FPaper2DPlusAppearanceDescriptor& Descriptor,
		const FString& AnimationName);
	PAPER2DPLUS_API TArray<FGuid> ResolveContributingGameplayLayerIds(
		const UPaper2DPlusCharacterLayerAsset* LayerAsset,
		const FPaper2DPlusAppearanceDescriptor& Descriptor,
		const FString& AnimationName);
	PAPER2DPLUS_API void Normalize(FPaper2DPlusAppearanceDescriptor& Descriptor);
	PAPER2DPLUS_API TArray<FString> ResolveVisibleLayers(
		const UPaper2DPlusCharacterLayerAsset* LayerAsset,
		const FPaper2DPlusAppearanceDescriptor& Descriptor,
		const FString& AnimationName);
	PAPER2DPLUS_API bool IsCompatible(
		const FPaper2DPlusAppearanceDescriptor& Descriptor,
		const UPaper2DPlusCharacterLayerAsset* LayerAsset,
		FString* OutReason = nullptr);
	PAPER2DPLUS_API bool AllowsRuntimeMutation(ECharacterLayerUsageMode DeliveryMode);
	PAPER2DPLUS_API bool UsesRuntimeLayerRenderer(ECharacterLayerUsageMode DeliveryMode);
	PAPER2DPLUS_API bool AllowsGameplayComposition(ECharacterLayerUsageMode DeliveryMode);
}
