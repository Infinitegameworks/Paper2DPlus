// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "FrameEvents/Paper2DPlusFrameEvent.h"

void UPaper2DPlusFrameEvent::OnReceiveFrameEvent_Implementation(const FPaper2DPlusFrameEventContext& Context)
{
	// Default no-op. BP subclasses override this.
}

bool UPaper2DPlusFrameEvent::DispatchFrame(
	const FPaper2DPlusFrameEventContext& Context,
	TSet<TObjectPtr<UPaper2DPlusFrameEventBase>>& ActiveRangedEvents)
{
	if (Context.PreviousFrame == Context.CurrentFrame) return false;

	// Check if TriggerFrame falls within the range of frames since last dispatch.
	// Handles frame skips (e.g., PreviousFrame=3, CurrentFrame=6 fires events on 4,5,6).
	bool bShouldFire = false;
	if (Context.bWasLoopWrap)
	{
		// Loop wrap: fire if TriggerFrame is after PreviousFrame OR at/before CurrentFrame
		bShouldFire = (TriggerFrame > Context.PreviousFrame) || (TriggerFrame <= Context.CurrentFrame);
	}
	else if (Context.PreviousFrame < Context.CurrentFrame)
	{
		bShouldFire = (TriggerFrame > Context.PreviousFrame && TriggerFrame <= Context.CurrentFrame);
	}
	else
	{
		bShouldFire = (TriggerFrame == Context.CurrentFrame);
	}

	if (bShouldFire)
	{
		OnReceiveFrameEvent(Context);
		return true;
	}
	return false;
}
