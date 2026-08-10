// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectPtr.h"

class UPaper2DPlusCharacterLayerAsset;
class UPaper2DPlusCueBase;
struct FFlipbookProfileEntry;
struct FFrameHitboxData;
struct FPaper2DPlusAppearanceDescriptor;

/**
 * Pure, worldless composition of committed generic Layer gameplay.
 *
 * Composes the base profile animation with the selected Layers that own authored gameplay for that
 * animation. The committed descriptor is delivered through NotifyAppearanceCombatDirty; previews never
 * reach this seam. Both network ends receive the same descriptor and asset data, so composition requires
 * no world, network branch, or render-component state.
 *
 * Runtime Customizable resolves the descriptor's final visible layers in paint order and adapts each layer's
 * cooked primary-workspace gameplay table. Merge semantics live in the schema-neutral
 * Paper2DPlusLayerGameplayCompose core. Fixed/Baked delivery fails closed
 * because its Profile arrays are already compiled output and composing the source again would double-apply it.
 */
namespace Paper2DPlusLayerCombat
{
	/** Generic Layer-ID compose. Selected Layers contribute in the asset's sole global order. */
	PAPER2DPLUS_API bool ComposeCombatFrames(
		const FFlipbookProfileEntry* Base,
		const UPaper2DPlusCharacterLayerAsset* LayerAsset,
		const FPaper2DPlusAppearanceDescriptor& CommittedAppearance,
		const FString& AnimationName,
		TArray<FFrameHitboxData>& OutFrames);

	/** Generic Layer-ID Cue compose. Preview descriptors never reach this committed seam. */
	PAPER2DPLUS_API bool ComposeFrameCues(
		const FFlipbookProfileEntry* Base,
		const UPaper2DPlusCharacterLayerAsset* LayerAsset,
		const FPaper2DPlusAppearanceDescriptor& CommittedAppearance,
		const FString& AnimationName,
		TArray<TObjectPtr<UPaper2DPlusCueBase>>& OutCues);

}
