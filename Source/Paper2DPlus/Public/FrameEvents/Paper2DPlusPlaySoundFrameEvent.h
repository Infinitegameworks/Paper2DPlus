// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FrameEvents/Paper2DPlusFrameEvent.h"
#include "Paper2DPlusPlaySoundFrameEvent.generated.h"

class USoundBase;

/**
 * First-class frame event that plays a USoundBase at the owning actor's
 * location. Use for footstep SFX, attack whoosh, impact cues, voice lines.
 *
 * If `bAttachToOwner` is true the sound follows the actor via
 * `UGameplayStatics::SpawnSoundAttached` — useful for moving characters.
 * Otherwise it spawns as a one-shot at the actor's current location.
 */
UCLASS(Blueprintable, DisplayName = "Play Sound Frame Event")
class PAPER2DPLUS_API UPaper2DPlusPlaySoundFrameEvent : public UPaper2DPlusFrameEvent
{
	GENERATED_BODY()

public:
	/** The sound asset to play. */
	UPROPERTY(EditAnywhere, Category = "Sound")
	TObjectPtr<USoundBase> Sound;

	/** Volume multiplier applied at spawn time. */
	UPROPERTY(EditAnywhere, Category = "Sound", meta = (ClampMin = "0.0"))
	float VolumeMultiplier = 1.0f;

	/** Pitch multiplier applied at spawn time. */
	UPROPERTY(EditAnywhere, Category = "Sound", meta = (ClampMin = "0.0"))
	float PitchMultiplier = 1.0f;

	/** Offset into the sound asset to begin playback (seconds). */
	UPROPERTY(EditAnywhere, Category = "Sound", meta = (ClampMin = "0.0"))
	float StartTime = 0.0f;

	/** If true, the sound is attached to the owning actor and moves with it.
	 *  Otherwise spawned one-shot at the actor's current location. */
	UPROPERTY(EditAnywhere, Category = "Sound")
	bool bAttachToOwner = false;

	virtual void OnReceiveFrameEvent_Implementation(const FPaper2DPlusFrameEventContext& Context) override;
};
