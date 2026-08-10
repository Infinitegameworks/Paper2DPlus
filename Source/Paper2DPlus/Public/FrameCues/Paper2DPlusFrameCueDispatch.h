// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FrameCues/Paper2DPlusFrameCue.h"
#include "Templates/Function.h"

/** Worldless Frame Cue lifecycle and traversal functions shared by runtime and editor preview. */
namespace Paper2DPlusFrameCues
{
	PAPER2DPLUS_API bool WasFrameTraversed(
		int32 Frame,
		int32 PreviousFrame,
		int32 CurrentFrame,
		bool bWasLoopWrap);

	/**
	 * True when Frame's TRAILING boundary was crossed by this transition: the frame was departed
	 * forward, or a loop wrapped past it. The exact mirror of WasFrameTraversed's entered-frame
	 * rule — forward fires [Prev, Current) where start-edge fires (Prev, Current] — so between the
	 * two predicates every boundary in a span fires exactly once. Deliberately false on an
	 * unseeded transition (nothing was departed yet) and on a backward seek (no boundary was
	 * crossed forward). Natural completion is NOT visible here: the profile component fires the
	 * final frame's trailing edge from its Completed terminal drain, where the frame provably
	 * finished.
	 */
	PAPER2DPLUS_API bool WasFrameTrailingEdgeCrossed(
		int32 Frame,
		int32 PreviousFrame,
		int32 CurrentFrame,
		bool bWasLoopWrap);

	PAPER2DPLUS_API void DispatchFrameTransition(
		TArrayView<const TObjectPtr<UPaper2DPlusCueBase>> Cues,
		const FPaper2DPlusFrameCueContext& BaseContext,
		TSet<TObjectPtr<UPaper2DPlusCueBase>>& ActiveRanges,
		TFunctionRef<bool(UPaper2DPlusCueBase&)> ShouldDispatch,
		TFunctionRef<void(UPaper2DPlusCueBase&, const FPaper2DPlusFrameCueContext&)> OnNotification);

	PAPER2DPLUS_API void ForceEndActiveRanges(
		TArrayView<const TObjectPtr<UPaper2DPlusCueBase>> AuthoredOrder,
		const FPaper2DPlusFrameCueContext& BaseContext,
		EPaper2DPlusFrameCueEndReason EndReason,
		TSet<TObjectPtr<UPaper2DPlusCueBase>>& ActiveRanges,
		TFunctionRef<void(UPaper2DPlusCueBase&, const FPaper2DPlusFrameCueContext&)> OnNotification);
}
