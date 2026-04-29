// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "FrameEvents/Paper2DPlusSpawnProjectileFrameEvent.h"
#include "Paper2DPlusCharacterProfileComponent.h"
#include "PaperFlipbookComponent.h"
#include "GameFramework/Actor.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"

void UPaper2DPlusSpawnProjectileFrameEvent::OnReceiveFrameEvent_Implementation(
	const FPaper2DPlusFrameEventContext& Context)
{
	if (!ProjectileClass || !IsValid(Context.OwningActor) || !Context.OwningActor->GetWorld()) return;

	// Determine facing-flip sign (1 = right, -1 = left)
	float FacingSign = 1.0f;
	if (bFlipWithCharacter && Context.ProfileComponent)
	{
		if (UPaperFlipbookComponent* CharFB = Context.ProfileComponent->GetResolvedFlipbookComponent())
		{
			if (CharFB->GetComponentScale().X < 0.0f)
			{
				FacingSign = -1.0f;
			}
		}
	}

	// Apply offset (2D → 3D: X lateral, Y maps to world Z)
	const FVector SpawnLocation = Context.OwningActor->GetActorLocation()
		+ FVector(SpawnOffset.X * FacingSign, 0.0f, SpawnOffset.Y);

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = Context.OwningActor;
	SpawnParams.Instigator = Context.OwningActor->GetInstigator();
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

	AActor* Projectile = Context.OwningActor->GetWorld()->SpawnActor<AActor>(
		ProjectileClass, SpawnLocation, FRotator::ZeroRotator, SpawnParams);
	if (!Projectile) return;

	const FVector WorldVelocity(LaunchVelocity.X * FacingSign, 0.0f, LaunchVelocity.Y);

	// Prefer a ProjectileMovementComponent when the projectile has one.
	if (UProjectileMovementComponent* ProjMove = Projectile->FindComponentByClass<UProjectileMovementComponent>())
	{
		ProjMove->Velocity = WorldVelocity;
		ProjMove->UpdateComponentVelocity();
		return;
	}

	// Fallback: set physics linear velocity on the root primitive if it simulates.
	if (UPrimitiveComponent* RootPrim = Cast<UPrimitiveComponent>(Projectile->GetRootComponent()))
	{
		if (RootPrim->IsSimulatingPhysics())
		{
			RootPrim->SetPhysicsLinearVelocity(WorldVelocity);
		}
	}
}
