// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FrameEvents/Paper2DPlusFrameEvent.h"
#include "GameFramework/Actor.h"
#include "Paper2DPlusSpawnProjectileFrameEvent.generated.h"

/** Hidden load-only projectile payload retained for lossless Cue migration. */
UCLASS(Blueprintable, Hidden, HideDropdown, DisplayName = "Legacy Spawn Projectile Frame Event (Load Only)")
class PAPER2DPLUS_API UPaper2DPlusSpawnProjectileFrameEvent : public UPaper2DPlusFrameEvent
{
	GENERATED_BODY()

public:
	/** Networked-correct default: spawning an actor mutates gameplay state — AuthorityOnly (TASK-57 U1). */
	UPaper2DPlusSpawnProjectileFrameEvent();

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

};
