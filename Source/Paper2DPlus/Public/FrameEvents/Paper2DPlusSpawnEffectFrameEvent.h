// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PaperFlipbookComponent.h"
#include "FrameEvents/Paper2DPlusFrameEvent.h"
#include "Paper2DPlusSpawnEffectFrameEvent.generated.h"

class UPaperFlipbook;

/**
 * Thin UPaperFlipbookComponent subclass that self-destroys when its flipbook finishes.
 * Needed because UActorComponent::DestroyComponent has a default bool arg so it can't
 * bind directly to FFlipbookFinishedPlaySignature (which takes zero params).
 */
UCLASS()
class PAPER2DPLUS_API UPaper2DPlusEffectFlipbookComponent : public UPaperFlipbookComponent
{
	GENERATED_BODY()

public:
	UFUNCTION()
	void HandleFinishedPlaying();
};

/**
 * Built-in one-shot frame event that spawns a visual effect flipbook.
 * Concrete — users can also subclass this in BP for custom spawn logic.
 * The native OnReceiveFrameEvent_Implementation spawns a UPaper2DPlusEffectFlipbookComponent
 * which self-destructs when playback completes (no stack-local FTimerHandle).
 */
UCLASS(Blueprintable, DisplayName = "Spawn Effect Frame Event")
class PAPER2DPLUS_API UPaper2DPlusSpawnEffectFrameEvent : public UPaper2DPlusFrameEvent
{
	GENERATED_BODY()

public:
	/** The flipbook to spawn as the visual effect. */
	UPROPERTY(EditAnywhere, Category = "Effect")
	TObjectPtr<UPaperFlipbook> EffectFlipbook;

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

	virtual bool HasSpatialPreview() const override { return true; }

	virtual void OnReceiveFrameEvent_Implementation(const FPaper2DPlusFrameEventContext& Context) override;
};
