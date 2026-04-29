// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FrameEvents/Paper2DPlusFrameEventBase.h"
#include "Paper2DPlusFrameEventState.generated.h"

/**
 * Ranged frame event. Fires Begin when entering the range, Tick per frame
 * inside, End when leaving.
 */
UCLASS(Blueprintable, Abstract)
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

	UFUNCTION(BlueprintNativeEvent, Category = "Paper2DPlus|Frame Event")
	void OnFrameEventBegin(const FPaper2DPlusFrameEventContext& Context);

	UFUNCTION(BlueprintNativeEvent, Category = "Paper2DPlus|Frame Event")
	void OnFrameEventTick(const FPaper2DPlusFrameEventContext& Context);

	UFUNCTION(BlueprintNativeEvent, Category = "Paper2DPlus|Frame Event")
	void OnFrameEventEnd(const FPaper2DPlusFrameEventContext& Context);

	virtual bool DispatchFrame(const FPaper2DPlusFrameEventContext& Context,
	                           TSet<TObjectPtr<UPaper2DPlusFrameEventBase>>& ActiveRangedEvents) override;

	virtual void DispatchForceEnd(const FPaper2DPlusFrameEventContext& Context) override;
};
