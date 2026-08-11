// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FrameEvents/Paper2DPlusFrameEvent.h"
#include "Paper2DPlusScreenFlashFrameEvent.generated.h"

/** Hidden load-only payload for the retired screen-flash Frame Event. */
UCLASS(Blueprintable, Hidden, HideDropdown, DisplayName = "Legacy Screen Flash Frame Event (Load Only)")
class PAPER2DPLUS_API UPaper2DPlusScreenFlashFrameEvent : public UPaper2DPlusFrameEvent
{
	GENERATED_BODY()

public:
	/** Networked-correct default: screen feedback is a cosmetic — CosmeticOnly (TASK-57 U1). */
	UPaper2DPlusScreenFlashFrameEvent();

	/** Color of the flash (alpha is typically used as intensity by handlers).
	 *  Named `FlashColor` rather than `Color` to avoid shadowing the base class
	 *  timeline-display `Color` UPROPERTY. */
	UPROPERTY(EditAnywhere, Category = "Screen Flash")
	FLinearColor FlashColor = FLinearColor(1.0f, 1.0f, 1.0f, 0.5f);

	/** Duration in seconds. Handler controls fade-out curve. */
	UPROPERTY(EditAnywhere, Category = "Screen Flash", meta = (ClampMin = "0.0"))
	float Duration = 0.1f;

};
