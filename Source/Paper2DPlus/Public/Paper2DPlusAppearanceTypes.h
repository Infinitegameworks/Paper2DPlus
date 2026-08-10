// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Paper2DPlusLayerUsageMode.h"
#include "Paper2DPlusAppearanceTypes.generated.h"

namespace Paper2DPlusAppearanceVersion
{
	/** Semantic version of the save/replication-independent appearance descriptor. */
	static constexpr int32 Current = 1;
}

/**
 * Serializable, Blueprint-storable committed appearance authority.
 *
 * This is appearance intent, not presentation. It never carries resolved visibility, pixels, textures,
 * primitive/tier state, cache keys, or UObject references. The same value feeds gameplay composition,
 * persistence, replication, and whichever client renderer is active.
 */
USTRUCT(BlueprintType)
struct PAPER2DPLUS_API FPaper2DPlusAppearanceDescriptor
{
	GENERATED_BODY()

	/** Descriptor semantics version. Independent from the replication envelope's PayloadVersion. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Paper2DPlus|Appearance")
	int32 SemanticVersion = Paper2DPlusAppearanceVersion::Current;

	/** Mutually exclusive delivery path selected by the owning Character Layer Asset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Paper2DPlus|Appearance")
	ECharacterLayerUsageMode DeliveryMode = ECharacterLayerUsageMode::RuntimeCustomizable;

	/** Complete selected Layer identity in the owning asset's global Layer order. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Paper2DPlus|Appearance")
	TArray<FGuid> ActiveLayerIds;

	bool operator==(const FPaper2DPlusAppearanceDescriptor& Other) const;
	bool operator!=(const FPaper2DPlusAppearanceDescriptor& Other) const { return !(*this == Other); }
};
