// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Paper2DPlusLayerUsageMode.generated.h"

/**
 * Selects exactly one delivery path for a Character Layer Asset.
 *
 * FixedBaked renders canonical output compiled by the editor. RuntimeCustomizable keeps generic
 * source Layers available for runtime selection. A renderer must never compose both paths at once.
 */
UENUM(BlueprintType)
enum class ECharacterLayerUsageMode : uint8
{
	FixedBaked UMETA(DisplayName = "Fixed / Baked"),
	RuntimeCustomizable UMETA(DisplayName = "Runtime Customizable")
};
