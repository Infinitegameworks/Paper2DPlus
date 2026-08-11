// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UMaterialInterface;

/** The opacity behavior Paper2DPlus can reproduce without evaluating an arbitrary material graph. */
enum class EPaper2DPlusSpriteOpacityMode : uint8
{
	Opaque,
	Masked,
	Translucent
};

/**
 * Shared admission contract for the canonical editor bake and the opt-in runtime compositor.
 * A material that is not one of Paper2D's known unlit sprite materials is never assumed to be
 * pixel-equivalent merely because it happens to use a familiar blend mode.
 */
namespace Paper2DPlusSpriteMaterialContract
{
	/** True only for the stock Paper2D sprite materials whose color/opacity behavior is known. */
	PAPER2DPLUS_API bool IsKnownPaper2DMaterial(const UMaterialInterface* Material);

	/**
	 * Resolve a Surface material using only the Unlit shading model to the small opacity vocabulary
	 * supported by the compositors. Mixed Unlit/lit shading-model fields fail closed.
	 * Set bRequireKnownPaper2DMaterial=false only when a separate explicit contract proves custom color
	 * behavior (the runtime recolor path is the sole shipped caller).
	 */
	PAPER2DPLUS_API bool ResolveOpacityMode(
		const UMaterialInterface* Material,
		bool bRequireKnownPaper2DMaterial,
		EPaper2DPlusSpriteOpacityMode& OutMode,
		float& OutOpacityMaskClipValue,
		FString& OutError);

	/** Whether flattened Source opacity can be represented exactly when rendered through Output. */
	PAPER2DPLUS_API bool CanRepresentFlattenedOpacity(
		EPaper2DPlusSpriteOpacityMode Output,
		EPaper2DPlusSpriteOpacityMode Source);

	PAPER2DPLUS_API const TCHAR* LexToString(EPaper2DPlusSpriteOpacityMode Mode);
}
