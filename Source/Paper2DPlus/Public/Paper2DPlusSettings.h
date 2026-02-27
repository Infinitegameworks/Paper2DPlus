// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "GameplayTagContainer.h"
#include "Paper2DPlusSettings.generated.h"

/**
 * Mapping from a GameplayTag animation group to a human-readable description.
 * Uses TArray<FGroupDescriptionMapping> instead of TMap<FGameplayTag, FText>
 * to work around UE-230676 (TMap<FGameplayTag, FText> Config serialization bug).
 */
USTRUCT(BlueprintType)
struct PAPER2DPLUS_API FGroupDescriptionMapping
{
	GENERATED_BODY()

	UPROPERTY(config, EditAnywhere, Category = "Group Description", meta = (Categories = "Paper2DPlus.Animation"))
	FGameplayTag Group;

	UPROPERTY(config, EditAnywhere, Category = "Group Description")
	FText Description;
};

/**
 * Project-wide settings for Paper2DPlus.
 * Appears in Project Settings > Plugins > Paper2DPlus.
 */
UCLASS(config = Game, defaultconfig, meta = (DisplayName = "Paper2DPlus"))
class PAPER2DPLUS_API UPaper2DPlusSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** Animation groups that every character should map (used for validation warnings). */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Animation Groups",
		meta = (Categories = "Paper2DPlus.Animation"))
	TArray<FGameplayTag> RequiredAnimationGroups;

	/** Optional descriptions for each group, shown as tooltips in the editor. */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Animation Groups")
	TArray<FGroupDescriptionMapping> GroupDescriptions;

	/** Enable depth (Z axis) for hitboxes and 3D viewport. When enabled, hitbox collision checks consider depth and the 3D viewer shows depth positioning. */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Hitbox", meta = (DisplayName = "Enable 3D Depth"))
	bool bEnable3DDepth = false;

	virtual FName GetCategoryName() const override { return FName(TEXT("Plugins")); }
	virtual FName GetSectionName() const override { return FName(TEXT("Paper2DPlus")); }

	/** Find description for a group tag. Returns empty FText if not found. */
	FText GetDescriptionForGroup(const FGameplayTag& Tag) const;

	/** Singleton accessor via GetDefault<>(). */
	static const UPaper2DPlusSettings* Get();
};
