// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FrameEvents/Paper2DPlusFrameEvent.h"
#include "GameFramework/Actor.h"
#include "Paper2DPlusSpawnProjectileFrameEvent.generated.h"

/**
 * First-class frame event that spawns a projectile actor with an initial
 * velocity. Use for arrows, thrown weapons, bullet attacks.
 *
 * `SpawnOffset` and `LaunchVelocity` are 2D (XZ plane in this project) and
 * flip horizontally with the character when `bFlipWithCharacter` is true —
 * so one authored event works for both facing directions.
 *
 * The velocity is applied to the projectile's root component (via
 * `SetPhysicsLinearVelocity` on a simulating primitive) and/or to a
 * `UProjectileMovementComponent` if one exists.
 */
UCLASS(Blueprintable, DisplayName = "Spawn Projectile Frame Event")
class PAPER2DPLUS_API UPaper2DPlusSpawnProjectileFrameEvent : public UPaper2DPlusFrameEvent
{
	GENERATED_BODY()

public:
	/** Actor class to spawn. */
	UPROPERTY(EditAnywhere, Category = "Projectile")
	TSubclassOf<AActor> ProjectileClass;

	/** Pixel offset from the owning actor's location at spawn. Flipped when
	 *  bFlipWithCharacter is true and the character is facing left. */
	UPROPERTY(EditAnywhere, Category = "Projectile")
	FVector2D SpawnOffset = FVector2D::ZeroVector;

	/** Initial velocity (world units/sec). X is horizontal, Y maps to world Z. */
	UPROPERTY(EditAnywhere, Category = "Projectile")
	FVector2D LaunchVelocity = FVector2D(500.0f, 0.0f);

	/** If true, SpawnOffset and LaunchVelocity flip horizontally when the
	 *  character's flipbook component has negative X scale. */
	UPROPERTY(EditAnywhere, Category = "Projectile")
	bool bFlipWithCharacter = true;

	virtual void OnReceiveFrameEvent_Implementation(const FPaper2DPlusFrameEventContext& Context) override;
};
