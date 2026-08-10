// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "FrameCues/Paper2DPlusSpawnFlipbookCue.h"

#include "Paper2DPlusBlueprintLibrary.h"
#include "Paper2DPlusCharacterProfileComponent.h"
#include "PaperFlipbook.h"

UPaper2DPlusEffectFlipbookComponent::UPaper2DPlusEffectFlipbookComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetGenerateOverlapEvents(false);
	OnFinishedPlaying.AddUniqueDynamic(
		this,
		&UPaper2DPlusEffectFlipbookComponent::HandleEffectFinishedPlaying);
}

void UPaper2DPlusEffectFlipbookComponent::HandleEffectFinishedPlaying()
{
	DestroyComponent();
}

UPaper2DPlusSpawnFlipbookCue::UPaper2DPlusSpawnFlipbookCue()
{
	// Networked-correct default for a world-visible cosmetic: dispatched everywhere a player can
	// see it, skipped entirely on dedicated servers. Delta-serialized class default — frozen.
	NetPolicy = EPaper2DPlusFrameCueNetPolicy::CosmeticOnly;
}

FTransform UPaper2DPlusSpawnFlipbookCue::ComputeEffectWorldTransform(
	const FVector& AnchorWorldLocation,
	const float AbsAnchorScaleX,
	const float AbsAnchorScaleZ,
	const bool bFacingLeft,
	const FVector2D& InOffset,
	const float InRotationDegrees,
	const FVector2D& InScale,
	const bool bInFlipWithCharacter)
{
	const float ScaleX = FMath::Max(AbsAnchorScaleX, KINDA_SMALL_NUMBER);
	const float ScaleZ = FMath::Max(AbsAnchorScaleZ, KINDA_SMALL_NUMBER);
	const float MirrorSign = (bInFlipWithCharacter && bFacingLeft) ? -1.0f : 1.0f;

	const FVector Location = AnchorWorldLocation
		+ FVector(MirrorSign * InOffset.X * ScaleX, 0.0f, InOffset.Y * ScaleZ);
	// Paper2D sprites rotate in the X/Z plane, which is a rotation about the Y axis (pitch).
	const FRotator Rotator(MirrorSign * InRotationDegrees, 0.0f, 0.0f);
	// A negative X scale mirrors the art itself, matching the character's facing flip.
	const FVector Scale3D(
		MirrorSign * InScale.X * ScaleX,
		1.0f,
		InScale.Y * ScaleZ);
	return FTransform(Rotator, Location, Scale3D);
}

UPaper2DPlusEffectFlipbookComponent* UPaper2DPlusSpawnFlipbookCue::SpawnEffect(
	const FPaper2DPlusFrameCueContext& Context) const
{
	AActor* Owner = Context.OwningActor;
	if (!Owner || !Owner->GetWorld())
	{
		return nullptr;
	}

	FTransform AnchorTransform;
	UPaperFlipbookComponent* AttachmentComponent = nullptr;
	const EPaper2DPlusFrameCueAnchorResult AnchorResult =
		UPaper2DPlusBlueprintLibrary::ResolveFrameCueAnchor(
			Context,
			Anchor,
			ProfileSocketName,
			AnchorTransform,
			AttachmentComponent);
	if (AnchorResult != EPaper2DPlusFrameCueAnchorResult::Success || !AttachmentComponent)
	{
		return nullptr;
	}

	// Warming loads this before the animation's first frame; the synchronous fallback only pays
	// on a warm miss (for example a cue authored during PIE) rather than every trigger.
	UPaperFlipbook* Flipbook = EffectFlipbook.Get();
	if (!Flipbook)
	{
		Flipbook = EffectFlipbook.LoadSynchronous();
	}
	if (!Flipbook)
	{
		return nullptr;
	}

	const FVector ComponentScale = AttachmentComponent->GetComponentScale();
	const bool bFacingLeft = UPaper2DPlusCharacterProfileComponent::ResolveFacingLeft(
		ComponentScale,
		AttachmentComponent->GetComponentRotation().Yaw);
	const FTransform EffectTransform = ComputeEffectWorldTransform(
		AnchorTransform.GetLocation(),
		FMath::Abs(ComponentScale.X),
		FMath::Abs(ComponentScale.Z),
		bFacingLeft,
		Offset,
		Rotation,
		Scale,
		bFlipWithCharacter);

	UPaper2DPlusEffectFlipbookComponent* Effect =
		NewObject<UPaper2DPlusEffectFlipbookComponent>(
			Owner,
			UPaper2DPlusEffectFlipbookComponent::StaticClass(),
			NAME_None,
			RF_Transient);
	if (!Effect)
	{
		return nullptr;
	}

	Effect->SetFlipbook(Flipbook);
	// A looping flipbook never reports finished and would therefore never release itself; this
	// cue is one-shot by contract.
	Effect->SetLooping(false);
	Effect->SetPlayRate(PlayRate);
	Effect->SetSpriteColor(Tint);
	Effect->RegisterComponentWithWorld(Owner->GetWorld());
	if (bAttachToCharacter)
	{
		Effect->AttachToComponent(
			AttachmentComponent,
			FAttachmentTransformRules::KeepWorldTransform);
	}
	else
	{
		Effect->SetUsingAbsoluteLocation(true);
		Effect->SetUsingAbsoluteRotation(true);
		Effect->SetUsingAbsoluteScale(true);
	}
	Effect->SetWorldTransform(EffectTransform);
	Effect->PlayFromStart();
	return Effect;
}

void UPaper2DPlusSpawnFlipbookCue::OnCueTriggered_Implementation(
	const FPaper2DPlusFrameCueContext& Context)
{
	// CosmeticOnly gating already skips dedicated servers, and catch-up never replays a moment
	// cue, so the body spawns unconditionally; the spawned component owns its own lifetime. The
	// shared placement stores no reference to it, staying stateless by contract.
	SpawnEffect(Context);
}
