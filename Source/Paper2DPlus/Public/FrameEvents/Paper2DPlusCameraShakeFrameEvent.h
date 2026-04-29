// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FrameEvents/Paper2DPlusFrameEvent.h"
#include "Camera/CameraShakeBase.h"
#include "Paper2DPlusCameraShakeFrameEvent.generated.h"

/**
 * First-class frame event that triggers a `UCameraShakeBase` on the local
 * player's camera. Use for impact feedback, landing thuds, parry flashes.
 *
 * If `OuterRadius > 0` the shake is radius-falloff-filtered via
 * `UGameplayStatics::PlayWorldCameraShake` so off-screen actions don't
 * shake the camera. Otherwise it plays unconditionally on player 0's
 * camera via `StartCameraShake`.
 */
UCLASS(Blueprintable, DisplayName = "Camera Shake Frame Event")
class PAPER2DPLUS_API UPaper2DPlusCameraShakeFrameEvent : public UPaper2DPlusFrameEvent
{
	GENERATED_BODY()

public:
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

	virtual void OnReceiveFrameEvent_Implementation(const FPaper2DPlusFrameEventContext& Context) override;
};
