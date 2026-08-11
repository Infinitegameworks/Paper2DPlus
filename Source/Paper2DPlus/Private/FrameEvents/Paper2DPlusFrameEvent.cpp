// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "FrameEvents/Paper2DPlusFrameEvent.h"

void UPaper2DPlusFrameEvent::OnReceiveFrameEvent_Implementation(const FPaper2DPlusFrameEventContext& Context)
{
	// Load-only no-op. Saved BP overrides remain deserializable but are never invoked.
}

bool UPaper2DPlusFrameEvent::RemapFrameAnchors(const TArray<int32>& OldToNew, int32 NumNewFrames)
{
	if (OldToNew.IsValidIndex(TriggerFrame))
	{
		const int32 NewIndex = OldToNew[TriggerFrame];
		if (NewIndex == INDEX_NONE)
		{
			return false; // anchor frame removed — caller stashes/drops this event
		}
		TriggerFrame = NewIndex;
		return true;
	}
	// Anchor outside the mapped range (e.g. already out of bounds): clamp into the new frame set.
	if (NumNewFrames > 0)
	{
		TriggerFrame = FMath::Clamp(TriggerFrame, 0, NumNewFrames - 1);
	}
	return true;
}
