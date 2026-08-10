// Copyright 2026 Infinite Gameworks. All Rights Reserved.
//
// Test-only recorder for the TASK-76 PR4 hit-stop DYNAMIC multicast delegates (DYNAMIC multicasts
// need UFUNCTIONs). Used by Paper2DPlusHitStopTest.cpp / Paper2DPlusNetHitStopTest.cpp to observe the
// hit-stop freeze begin/end broadcasts (and the cancel-from-inside-Begin rescue) without a world.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Paper2DPlusCharacterProfileComponent.h"
#include "PaperFlipbook.h"
#include "PaperFlipbookComponent.h"
#include "FrameEvents/Paper2DPlusFrameEvent.h"
#include "Paper2DPlusTestTransitionTypes.generated.h"

/** Test-only recorder for the OnHitStopBegin/OnHitStopEnd DYNAMIC multicast delegates (TASK-76 PR4). */
UCLASS()
class UPaper2DPlusHitStopRecorder : public UObject
{
	GENERATED_BODY()

public:
	int32 BeginCount = 0;
	int32 EndCount = 0;
	TWeakObjectPtr<AActor> LastVictim;
	float LastDurationSeconds = 0.f;

	/** When bCancelOnBegin is set, OnHitStopBegin calls CancelTarget->CancelHitStop() ONCE — the
	 *  PR4 cancel-from-inside-Begin rescue: the freeze is undone immediately while the End
	 *  broadcast is suppressed (Verbose log, no expected-message whitelist needed). */
	TWeakObjectPtr<UPaper2DPlusCharacterProfileComponent> CancelTarget;
	bool bCancelOnBegin = false;

	UFUNCTION()
	void OnHitStopBegin(AActor* Victim, float DurationSeconds)
	{
		++BeginCount;
		LastVictim = Victim;
		LastDurationSeconds = DurationSeconds;

		if (bCancelOnBegin && CancelTarget.IsValid())
		{
			bCancelOnBegin = false; // once
			CancelTarget->CancelHitStop();
		}
	}

	UFUNCTION()
	void OnHitStopEnd()
	{
		++EndCount;
	}
};
