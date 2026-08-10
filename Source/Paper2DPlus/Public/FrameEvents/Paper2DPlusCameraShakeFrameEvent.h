// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FrameEvents/Paper2DPlusFrameEvent.h"
#include "Camera/CameraShakeBase.h"
#include "Paper2DPlusCameraShakeFrameEvent.generated.h"

/** Hidden load-only camera-shake payload retained for lossless Cue migration. */
UCLASS(Blueprintable, Hidden, HideDropdown, DisplayName = "Legacy Camera Shake Frame Event (Load Only)")
class PAPER2DPLUS_API UPaper2DPlusCameraShakeFrameEvent : public UPaper2DPlusFrameEvent
{
	GENERATED_BODY()

public:
	/** Networked-correct default: camera feedback is a cosmetic — CosmeticOnly (TASK-57 U1). */
	UPaper2DPlusCameraShakeFrameEvent();

	/** Camera shake class to play. */
	UPROPERTY(EditAnywhere, Category = "Camera Shake")
	TSubclassOf<UCameraShakeBase> ShakeClass;

	/** Scale multiplier for the shake (amplitude). */
	UPROPERTY(EditAnywhere, Category = "Camera Shake", meta = (ClampMin = "0.0"))
	float Scale = 1.0f;

	/** Distance from owning actor within which shake plays at full strength.
	 *  Only used when OuterRadius > 0. */
	UPROPERTY(EditAnywhere, Category = "Camera Shake", meta = (ClampMin = "0.0"))
	float InnerRadius = 0.0f;

	/** Beyond this distance the shake does not play. Set > 0 to enable
	 *  radius-falloff mode; otherwise shake plays unconditionally on P0. */
	UPROPERTY(EditAnywhere, Category = "Camera Shake", meta = (ClampMin = "0.0"))
	float OuterRadius = 0.0f;

	/** Falloff exponent between InnerRadius and OuterRadius (1.0 = linear). */
	UPROPERTY(EditAnywhere, Category = "Camera Shake", meta = (ClampMin = "0.0"))
	float Falloff = 1.0f;

};
