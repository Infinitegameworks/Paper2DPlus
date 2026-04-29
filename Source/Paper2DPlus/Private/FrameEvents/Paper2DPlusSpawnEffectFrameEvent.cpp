// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "FrameEvents/Paper2DPlusSpawnEffectFrameEvent.h"
#include "Paper2DPlusCharacterProfileComponent.h"
#include "PaperFlipbook.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"

void UPaper2DPlusEffectFlipbookComponent::HandleFinishedPlaying()
{
	DestroyComponent();
}

void UPaper2DPlusSpawnEffectFrameEvent::OnReceiveFrameEvent_Implementation(
	const FPaper2DPlusFrameEventContext& Context)
{
	if (!EffectFlipbook)
	{
		UE_LOG(LogTemp, Warning, TEXT("SpawnEffectFrameEvent: EffectFlipbook is null on '%s'. Assign a flipbook in the event properties."), *GetName());
		return;
	}
	if (!IsValid(Context.OwningActor) || !Context.OwningActor->GetWorld())
	{
		return;
	}

	AActor* Owner = Context.OwningActor;

	// Spawn the self-destructing flipbook component for the effect
	UPaper2DPlusEffectFlipbookComponent* EffectComp = NewObject<UPaper2DPlusEffectFlipbookComponent>(Owner);
	EffectComp->SetFlipbook(EffectFlipbook);
	EffectComp->SetLooping(false);
	EffectComp->SetSpriteColor(Tint);

	// Calculate world offset from pixel space
	FVector WorldOffset(Offset.X, 0.0f, Offset.Y);
	if (bFlipWithCharacter && Context.ProfileComponent)
	{
		if (UPaperFlipbookComponent* CharFB = Context.ProfileComponent->GetResolvedFlipbookComponent())
		{
			const FVector CompScale = CharFB->GetComponentScale();
			if (CompScale.X < 0.0f)
			{
				WorldOffset.X = -WorldOffset.X;
			}
		}
	}

	EffectComp->SetRelativeLocation(WorldOffset);
	EffectComp->SetRelativeRotation(FRotator(0.0f, 0.0f, Rotation));
	EffectComp->SetRelativeScale3D(FVector(Scale.X, 1.0f, Scale.Y));
	EffectComp->RegisterComponent();
	// Attach to flipbook component (sprite-relative) rather than root,
	// so effects stay anchored correctly when the sprite is offset from root.
	USceneComponent* AttachTarget = Owner->GetRootComponent();
	if (Context.ProfileComponent)
	{
		if (UPaperFlipbookComponent* FBComp = Context.ProfileComponent->GetResolvedFlipbookComponent())
		{
			AttachTarget = FBComp;
		}
	}
	if (AttachTarget)
	{
		EffectComp->AttachToComponent(AttachTarget, FAttachmentTransformRules::KeepRelativeTransform);
	}
	EffectComp->OnFinishedPlaying.AddDynamic(EffectComp, &UPaper2DPlusEffectFlipbookComponent::HandleFinishedPlaying);

	EffectComp->PlayFromStart();
}
