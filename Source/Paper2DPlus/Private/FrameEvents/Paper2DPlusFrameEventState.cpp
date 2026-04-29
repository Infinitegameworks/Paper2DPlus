// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "FrameEvents/Paper2DPlusFrameEventState.h"

void UPaper2DPlusFrameEventState::OnFrameEventBegin_Implementation(const FPaper2DPlusFrameEventContext& Context)
{
	// Default no-op. BP subclasses override this.
}

void UPaper2DPlusFrameEventState::OnFrameEventTick_Implementation(const FPaper2DPlusFrameEventContext& Context)
{
	// Default no-op. BP subclasses override this.
}

void UPaper2DPlusFrameEventState::OnFrameEventEnd_Implementation(const FPaper2DPlusFrameEventContext& Context)
{
	// Default no-op. BP subclasses override this.
}

bool UPaper2DPlusFrameEventState::DispatchFrame(
	const FPaper2DPlusFrameEventContext& Context,
	TSet<TObjectPtr<UPaper2DPlusFrameEventBase>>& ActiveRangedEvents)
{
	const bool bInRange = ContainsFrame(Context.CurrentFrame);
	const bool bWasActive = ActiveRangedEvents.Contains(this);
	bool bFired = false;

	if (bInRange && !bWasActive)
	{
		OnFrameEventBegin(Context);
		ActiveRangedEvents.Add(this);
		bFired = true;
	}
	if (bInRange)
	{
		OnFrameEventTick(Context);
		ActiveRangedEvents.Add(this);
		bFired = true;
	}
	if (!bInRange && bWasActive)
	{
		OnFrameEventEnd(Context);
		ActiveRangedEvents.Remove(this);
		bFired = true;
	}
	return bFired;
}

void UPaper2DPlusFrameEventState::DispatchForceEnd(const FPaper2DPlusFrameEventContext& Context)
{
	OnFrameEventEnd(Context);
}
