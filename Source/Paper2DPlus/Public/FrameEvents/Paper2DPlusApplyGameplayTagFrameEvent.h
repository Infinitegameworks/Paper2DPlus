// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "FrameEvents/Paper2DPlusFrameEventState.h"
#include "Paper2DPlusApplyGameplayTagFrameEvent.generated.h"

/** Hidden load-only payload for the retired gameplay-tag Frame Event. */
UCLASS(Blueprintable, Hidden, HideDropdown, DisplayName = "Legacy Apply Gameplay Tag Frame Event (Load Only)")
class PAPER2DPLUS_API UPaper2DPlusApplyGameplayTagFrameEvent : public UPaper2DPlusFrameEventState
{
	GENERATED_BODY()

public:
	/** Networked-correct default: gameplay tags mutate gameplay state — AuthorityOnly (TASK-57 U1). */
	UPaper2DPlusApplyGameplayTagFrameEvent();

	/** Tags to apply on range begin and remove on range end. */
	UPROPERTY(EditAnywhere, Category = "Gameplay Tags")
	FGameplayTagContainer Tags;

};
