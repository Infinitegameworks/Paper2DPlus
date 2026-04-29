// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FrameEvents/Paper2DPlusFrameEventBase.h"
#include "Paper2DPlusFrameEvent.generated.h"

/**
 * One-shot frame event. Fires OnReceiveFrameEvent when playback reaches TriggerFrame.
 * BP authors subclass this and override OnReceiveFrameEvent to implement custom behavior
 * (audio cue, camera shake, gameplay tag fire, projectile spawn, etc.).
 */
UCLASS(Blueprintable, Abstract)
class PAPER2DPLUS_API UPaper2DPlusFrameEvent : public UPaper2DPlusFrameEventBase
{
	GENERATED_BODY()

public:
	/** The key-frame index at which this event fires. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Trigger")
	int32 TriggerFrame = 0;

	/** Override in BP to implement custom one-shot behavior. */
	UFUNCTION(BlueprintNativeEvent, Category = "Paper2DPlus|Frame Event")
	void OnReceiveFrameEvent(const FPaper2DPlusFrameEventContext& Context);

	virtual bool DispatchFrame(const FPaper2DPlusFrameEventContext& Context,
	                           TSet<TObjectPtr<UPaper2DPlusFrameEventBase>>& ActiveRangedEvents) override;
};
