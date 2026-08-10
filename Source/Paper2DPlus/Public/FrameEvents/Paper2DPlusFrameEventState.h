// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FrameEvents/Paper2DPlusFrameEventBase.h"
#include "Paper2DPlusFrameEventState.generated.h"

/**
 * Hidden ranged legacy load shell. Begin/Tick/End signatures remain only so saved custom Blueprint
 * graphs can load and be inventoried; the runtime never invokes them.
 */
UCLASS(Blueprintable, Abstract, Hidden, HideDropdown)
class PAPER2DPLUS_API UPaper2DPlusFrameEventState : public UPaper2DPlusFrameEventBase
{
	GENERATED_BODY()

public:
	/** First frame in the range (inclusive). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Trigger")
	int32 StartFrame = 0;

	/** Number of frames the range spans. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Trigger", meta = (ClampMin = "1"))
	int32 FrameCount = 1;

	/** Check if a frame index is within this event's range. */
	bool ContainsFrame(int32 Frame) const { return Frame >= StartFrame && Frame < StartFrame + FrameCount; }

	UFUNCTION(BlueprintNativeEvent, Category = "Paper2DPlus|Migration|Legacy Frame Event",
		meta = (DeprecatedFunction, DeprecationMessage = "Executable Frame Events are load-only. Create a Cue State Type and implement OnCueBegin or listen to OnFrameCue instead."))
	void OnFrameEventBegin(const FPaper2DPlusFrameEventContext& Context);

	UFUNCTION(BlueprintNativeEvent, Category = "Paper2DPlus|Migration|Legacy Frame Event",
		meta = (DeprecatedFunction, DeprecationMessage = "Executable Frame Events are load-only. Create a Cue State Type and implement OnCueUpdate or listen to OnFrameCue instead."))
	void OnFrameEventTick(const FPaper2DPlusFrameEventContext& Context);

	UFUNCTION(BlueprintNativeEvent, Category = "Paper2DPlus|Migration|Legacy Frame Event",
		meta = (DeprecatedFunction, DeprecationMessage = "Executable Frame Events are load-only. Create a Cue State Type and implement OnCueEnd or listen to OnFrameCue instead."))
	void OnFrameEventEnd(const FPaper2DPlusFrameEventContext& Context);

	virtual bool RemapFrameAnchors(const TArray<int32>& OldToNew, int32 NumNewFrames) override;
	virtual void SetPrimaryAnchorFrame(int32 NewFrame) override { StartFrame = NewFrame; }
};
