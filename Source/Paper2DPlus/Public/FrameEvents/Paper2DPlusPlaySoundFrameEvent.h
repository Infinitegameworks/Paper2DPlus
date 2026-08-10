// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FrameEvents/Paper2DPlusFrameEvent.h"
#include "Paper2DPlusPlaySoundFrameEvent.generated.h"

class USoundBase;

/** Hidden load-only sound payload retained for lossless Cue migration. */
UCLASS(Blueprintable, Hidden, HideDropdown, DisplayName = "Legacy Play Sound Frame Event (Load Only)")
class PAPER2DPLUS_API UPaper2DPlusPlaySoundFrameEvent : public UPaper2DPlusFrameEvent
{
	GENERATED_BODY()

public:
	/** Networked-correct default: audio is a world-audible cosmetic — CosmeticOnly (TASK-57 U1). */
	UPaper2DPlusPlaySoundFrameEvent();

	/** The sound asset to play. */
	UPROPERTY(EditAnywhere, Category = "Sound")
	TObjectPtr<USoundBase> Sound = nullptr;

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

};
