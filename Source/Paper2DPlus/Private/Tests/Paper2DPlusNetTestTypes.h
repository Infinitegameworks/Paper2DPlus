// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Paper2DPlusCharacterProfileComponent.h"
#include "PaperFlipbook.h"
#include "Paper2DPlusNetTestTypes.generated.h"

UCLASS()
class UPaper2DPlusNetAnimStateRecorder : public UObject
{
	GENERATED_BODY()
public:
	int32 BroadcastCount = 0;
	FName LastMoveName;
	TWeakObjectPtr<UPaperFlipbook> LastFlipbook;
	bool bLastFlipbookWasNull = true;
	float LastPlaybackPosition = -1.0f;
	bool bLastAlreadyFinished = false;

	UFUNCTION()
	void OnReplicatedAnimState(FName MoveName, UPaperFlipbook* Flipbook, float PlaybackPosition, FName ConfirmedLabel, bool bAlreadyFinished)
	{
		(void)ConfirmedLabel;
		++BroadcastCount;
		LastMoveName = MoveName;
		LastFlipbook = Flipbook;
		bLastFlipbookWasNull = Flipbook == nullptr;
		LastPlaybackPosition = PlaybackPosition;
		bLastAlreadyFinished = bAlreadyFinished;
	}
};

/** TASK-145: recorder for the automatic hit-detection broadcasts (hits, window edges, whiffs). */
UCLASS()
class UPaper2DPlusAutoHitRecorder : public UObject
{
	GENERATED_BODY()
public:
	int32 HitConnectedCount = 0;
	int32 HitReceivedCount = 0;
	int32 WindowBeginCount = 0;
	int32 WindowEndCount = 0;
	int32 WhiffCount = 0;
	FPaper2DPlusAutoHitResult LastHit;
	FString LastWindowMoveName;
	int32 LastWindowBeginFrame = INDEX_NONE;
	int32 LastWindowEndFrame = INDEX_NONE;
	FString LastWhiffMoveName;

	UFUNCTION()
	void OnHitConnected(const FPaper2DPlusAutoHitResult& Hit)
	{
		++HitConnectedCount;
		LastHit = Hit;
	}

	UFUNCTION()
	void OnHitReceived(const FPaper2DPlusAutoHitResult& Hit)
	{
		++HitReceivedCount;
		LastHit = Hit;
	}

	UFUNCTION()
	void OnWindowBegin(const FString& MoveName, int32 KeyFrame)
	{
		++WindowBeginCount;
		LastWindowMoveName = MoveName;
		LastWindowBeginFrame = KeyFrame;
	}

	UFUNCTION()
	void OnWindowEnd(const FString& MoveName, int32 KeyFrame)
	{
		++WindowEndCount;
		LastWindowMoveName = MoveName;
		LastWindowEndFrame = KeyFrame;
	}

	UFUNCTION()
	void OnWhiff(const FString& MoveName)
	{
		++WhiffCount;
		LastWhiffMoveName = MoveName;
	}
};

/** TASK-145: a receiver that kills the ATTACKER from inside its own hit broadcast — the die-on-impact
 *  projectile shape. Proves the detection pass stops touching its own state instead of continuing to
 *  iterate remaining overlaps on a dead component. */
UCLASS()
class UPaper2DPlusAutoHitSelfDestructRecorder : public UObject
{
	GENERATED_BODY()
public:
	int32 HitConnectedCount = 0;

	/** Marked garbage on the first hit, so TWeakObjectPtr::IsValid() goes false mid-pass. */
	UPROPERTY()
	TObjectPtr<UPaper2DPlusCharacterProfileComponent> AttackerToKill = nullptr;

	UFUNCTION()
	void OnHitConnected(const FPaper2DPlusAutoHitResult& Hit)
	{
		(void)Hit;
		++HitConnectedCount;
		if (AttackerToKill)
		{
			AttackerToKill->MarkAsGarbage();
			AttackerToKill = nullptr;
		}
	}
};
