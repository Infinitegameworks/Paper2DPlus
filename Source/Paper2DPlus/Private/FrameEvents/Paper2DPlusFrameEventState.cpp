// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "FrameEvents/Paper2DPlusFrameEventState.h"

void UPaper2DPlusFrameEventState::OnFrameEventBegin_Implementation(const FPaper2DPlusFrameEventContext& Context)
{
	// Load-only no-op. Saved BP overrides remain deserializable but are never invoked.
}

void UPaper2DPlusFrameEventState::OnFrameEventTick_Implementation(const FPaper2DPlusFrameEventContext& Context)
{
	// Load-only no-op. Saved BP overrides remain deserializable but are never invoked.
}

void UPaper2DPlusFrameEventState::OnFrameEventEnd_Implementation(const FPaper2DPlusFrameEventContext& Context)
{
	// Load-only no-op. Saved BP overrides remain deserializable but are never invoked.
}

bool UPaper2DPlusFrameEventState::RemapFrameAnchors(const TArray<int32>& OldToNew, int32 NumNewFrames)
{
	const int32 OldStart = StartFrame;
	const int32 OldEnd = OldStart + FMath::Max(FrameCount, 1) - 1; // last in-range key frame (inclusive)

	if (OldToNew.IsValidIndex(OldStart))
	{
		const int32 NewStart = OldToNew[OldStart];
		if (NewStart == INDEX_NONE)
		{
			return false; // primary (start) anchor removed — caller stashes/drops this ranged event
		}

		// Span the new range across the FULL extent (min..max) of every surviving mapped frame in the old
		// span [OldStart, OldEnd], so an interior/end-frame exclude OR a reorder resizes correctly (the
		// previous version only tracked the start and clamp-kept FrameCount, silently mis-sizing interior
		// mutations — TASK-61 / WS2-2C). Min/max (not start+tail) is required because the reorder/Move
		// OldToNew is NOT monotonic across a span — a naive "end = last surviving" can land below the start
		// and collapse the span. A fully-surviving span therefore never shrinks; removed endpoints contract
		// inward. (A span whose surviving frames became non-contiguous is approximated by its bounding
		// range — an inherent start+count limitation.) NewStart is guaranteed valid (start survived).
		int32 NewMin = OldToNew[OldStart];
		int32 NewMax = NewMin;
		const int32 ClampedEnd = FMath::Min(OldEnd, OldToNew.Num() - 1);
		for (int32 OldFrame = OldStart; OldFrame <= ClampedEnd; ++OldFrame)
		{
			const int32 Mapped = OldToNew[OldFrame];
			if (Mapped != INDEX_NONE)
			{
				NewMin = FMath::Min(NewMin, Mapped);
				NewMax = FMath::Max(NewMax, Mapped);
			}
		}

		StartFrame = NewMin;
		FrameCount = FMath::Max(1, (NewMax - NewMin) + 1);
	}

	// Final clamp into the post-mutation frame set (also handles a start anchor outside the mapped range,
	// matching the one-shot remap fallback).
	if (NumNewFrames > 0)
	{
		StartFrame = FMath::Clamp(StartFrame, 0, NumNewFrames - 1);
		FrameCount = FMath::Clamp(FrameCount, 1, NumNewFrames - StartFrame);
	}
	return true;
}
