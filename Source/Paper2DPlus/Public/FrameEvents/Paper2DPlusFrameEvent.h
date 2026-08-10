// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FrameEvents/Paper2DPlusFrameEventBase.h"
#include "Paper2DPlusFrameEvent.generated.h"

/**
 * Hidden one-shot legacy load shell. OnReceiveFrameEvent remains only so saved custom Blueprint
 * graphs can load and be reported; the runtime never invokes it.
 */
UCLASS(Blueprintable, Abstract, Hidden, HideDropdown)
class PAPER2DPLUS_API UPaper2DPlusFrameEvent : public UPaper2DPlusFrameEventBase
{
	GENERATED_BODY()

public:
	/** The key-frame index at which this event fires. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Trigger")
	int32 TriggerFrame = 0;

	/** DEPRECATED saved-graph signature; never invoked by Paper2DPlus runtime or editor preview. */
	UFUNCTION(BlueprintNativeEvent, Category = "Paper2DPlus|Migration|Legacy Frame Event",
		meta = (DeprecatedFunction, DeprecationMessage = "Executable Frame Events are load-only. Create a Cue Type and implement OnCueTriggered or listen to OnFrameCue instead."))
	void OnReceiveFrameEvent(const FPaper2DPlusFrameEventContext& Context);

	virtual bool RemapFrameAnchors(const TArray<int32>& OldToNew, int32 NumNewFrames) override;
	virtual void SetPrimaryAnchorFrame(int32 NewFrame) override { TriggerFrame = NewFrame; }
};
