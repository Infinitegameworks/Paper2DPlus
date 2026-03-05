// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "GameplayTagContainer.h"
#include "Paper2DPlusSettings.generated.h"

/**
 * Mapping from a GameplayTag to a human-readable description for tag mappings.
 * Uses TArray<FTagMappingDescription> instead of TMap<FGameplayTag, FText>
 * to work around UE-230676 (TMap<FGameplayTag, FText> Config serialization bug).
 */
USTRUCT(BlueprintType)
struct PAPER2DPLUS_API FTagMappingDescription
{
	GENERATED_BODY()

	UPROPERTY(config, EditAnywhere, Category = "Tag Mapping Description", meta = (Categories = "Paper2DPlus.Animation"))
	FGameplayTag Group;

	UPROPERTY(config, EditAnywhere, Category = "Tag Mapping Description")
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
	/** Tag mappings that every character should map (used for validation warnings). */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Tag Mappings",
		meta = (Categories = "Paper2DPlus.Animation"))
	TArray<FGameplayTag> RequiredTagMappings;

	/** Optional descriptions for each tag mapping, shown as tooltips in the editor. */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Tag Mappings")
	TArray<FTagMappingDescription> TagMappingDescriptions;

	/** Enable depth (Z axis) for hitboxes and 3D viewport. When enabled, hitbox collision checks consider depth and the 3D viewer shows depth positioning. */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Hitbox", meta = (DisplayName = "Enable 3D Depth"))
	bool bEnable3DDepth = false;

	virtual FName GetCategoryName() const override { return FName(TEXT("Plugins")); }
	virtual FName GetSectionName() const override { return FName(TEXT("Paper2DPlus")); }

	/** Find description for a tag mapping. Returns empty FText if not found. */
	FText GetDescriptionForTag(const FGameplayTag& Tag) const;

	/** Singleton accessor via GetDefault<>(). */
	static const UPaper2DPlusSettings* Get();
};
