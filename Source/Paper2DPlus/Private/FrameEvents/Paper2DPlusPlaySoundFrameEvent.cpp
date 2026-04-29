// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "FrameEvents/Paper2DPlusPlaySoundFrameEvent.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "GameFramework/Actor.h"

void UPaper2DPlusPlaySoundFrameEvent::OnReceiveFrameEvent_Implementation(
	const FPaper2DPlusFrameEventContext& Context)
{
	if (!Sound || !IsValid(Context.OwningActor) || !Context.OwningActor->GetWorld()) return;

	if (bAttachToOwner && Context.OwningActor->GetRootComponent())
	{
		UGameplayStatics::SpawnSoundAttached(
			Sound,
			Context.OwningActor->GetRootComponent(),
			NAME_None,
			FVector::ZeroVector,
			EAttachLocation::KeepRelativeOffset,
			/*bStopWhenAttachedToDestroyed=*/true,
			VolumeMultiplier,
			PitchMultiplier,
			StartTime);
	}
	else
	{
		UGameplayStatics::PlaySoundAtLocation(
			Context.OwningActor,
			Sound,
			Context.OwningActor->GetActorLocation(),
			FRotator::ZeroRotator,
			VolumeMultiplier,
			PitchMultiplier,
			StartTime);
	}
}
