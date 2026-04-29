// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "FrameEvents/Paper2DPlusCameraShakeFrameEvent.h"
#include "Kismet/GameplayStatics.h"
#include "Camera/CameraShakeBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"

void UPaper2DPlusCameraShakeFrameEvent::OnReceiveFrameEvent_Implementation(
	const FPaper2DPlusFrameEventContext& Context)
{
	if (!ShakeClass || !IsValid(Context.OwningActor) || !Context.OwningActor->GetWorld()) return;

	UWorld* World = Context.OwningActor->GetWorld();

	// Radius-falloff mode: only affects cameras within OuterRadius of the owning actor.
	if (OuterRadius > 0.0f)
	{
		UGameplayStatics::PlayWorldCameraShake(
			World,
			ShakeClass,
			Context.OwningActor->GetActorLocation(),
			InnerRadius,
			OuterRadius,
			Falloff,
			/*bOrientShakeTowardsEpicenter=*/false);
		return;
	}

	// Unconditional mode: trigger on player 0's camera manager.
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0))
	{
		PC->ClientStartCameraShake(ShakeClass, Scale);
	}
}
