// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FrameEvents/Paper2DPlusFrameEvent.h"
#include "Paper2DPlusEffectProfileAsset.h"
#include "Paper2DPlusSpawnEffectFrameEvent.generated.h"

class UPaperFlipbook;

/** Hidden load-only payload for the retired Spawn Effect Frame Event. */
UCLASS(Blueprintable, Hidden, HideDropdown, DisplayName = "Legacy Spawn Effect Frame Event (Load Only)")
class PAPER2DPLUS_API UPaper2DPlusSpawnEffectFrameEvent : public UPaper2DPlusFrameEvent
{
	GENERATED_BODY()

public:
	/** Networked-correct default: visual effects are world-visible cosmetics — CosmeticOnly (TASK-57 U1). */
	UPaper2DPlusSpawnEffectFrameEvent();

	/** Optional reusable Effect Profile entry. When set, fields below act as per-event overrides. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Effect|Profile")
	TObjectPtr<UPaper2DPlusEffectProfileAsset> EffectProfile = nullptr;

	/** Entry name inside EffectProfile. Ignored when EffectProfile is unset. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Effect|Profile")
	FName EffectName;

	/** Override the profile entry's flipbook. Existing direct-only events work even when this is false. */
	UPROPERTY(EditAnywhere, Category = "Effect|Overrides")
	bool bOverrideEffectFlipbook = false;

	UPROPERTY(EditAnywhere, Category = "Effect|Overrides")
	bool bOverrideOffset = false;

	UPROPERTY(EditAnywhere, Category = "Effect|Overrides")
	bool bOverrideRotation = false;

	UPROPERTY(EditAnywhere, Category = "Effect|Overrides")
	bool bOverrideScale = false;

	UPROPERTY(EditAnywhere, Category = "Effect|Overrides")
	bool bOverrideFlipWithCharacter = false;

	UPROPERTY(EditAnywhere, Category = "Effect|Overrides")
	bool bOverrideTint = false;

	/** The flipbook to spawn as the visual effect. */
	UPROPERTY(EditAnywhere, Category = "Effect")
	TObjectPtr<UPaperFlipbook> EffectFlipbook = nullptr;

	/** Pixel offset from the character's origin. */
	UPROPERTY(EditAnywhere, Category = "Effect")
	FVector2D Offset = FVector2D::ZeroVector;

	/** Rotation in degrees. */
	UPROPERTY(EditAnywhere, Category = "Effect")
	float Rotation = 0.0f;

	/** Scale multiplier. */
	UPROPERTY(EditAnywhere, Category = "Effect")
	FVector2D Scale = FVector2D(1.0, 1.0);

	/** If true, the effect flips with the character's facing direction. */
	UPROPERTY(EditAnywhere, Category = "Effect")
	bool bFlipWithCharacter = true;

	/** Color tint applied to the spawned flipbook. */
	UPROPERTY(EditAnywhere, Category = "Effect")
	FLinearColor Tint = FLinearColor::White;

	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Migration|Legacy Frame Event")
	bool ResolveSpawnSettings(FPaper2DPlusEffectSpawnSettings& OutSettings) const;

	FPaper2DPlusEffectSpawnSettings GetDirectSpawnSettings() const;

};
