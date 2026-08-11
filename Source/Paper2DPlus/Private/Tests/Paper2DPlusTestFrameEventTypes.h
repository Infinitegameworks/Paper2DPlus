// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FrameEvents/Paper2DPlusFrameEvent.h"
#include "FrameEvents/Paper2DPlusFrameEventState.h"
#include "Paper2DPlusTestFrameEventTypes.generated.h"

UCLASS(HideDropdown)
class UPaper2DPlusTestFrameEvent : public UPaper2DPlusFrameEvent
{
	GENERATED_BODY()
public:
	int32 FireCount = 0;
	virtual void OnReceiveFrameEvent_Implementation(const FPaper2DPlusFrameEventContext& Context) override
	{
		++FireCount;
	}
};

UCLASS(HideDropdown)
class UPaper2DPlusTestFrameEventState : public UPaper2DPlusFrameEventState
{
	GENERATED_BODY()
public:
	int32 BeginCount = 0;
	int32 TickCount = 0;
	int32 EndCount = 0;

	virtual void OnFrameEventBegin_Implementation(const FPaper2DPlusFrameEventContext& Context) override { ++BeginCount; }
	virtual void OnFrameEventTick_Implementation(const FPaper2DPlusFrameEventContext& Context) override { ++TickCount; }
	virtual void OnFrameEventEnd_Implementation(const FPaper2DPlusFrameEventContext& Context) override { ++EndCount; }
};
