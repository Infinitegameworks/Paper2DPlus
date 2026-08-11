// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "FrameCues/Paper2DPlusFrameCueDispatch.h"
#include "FrameCues/Paper2DPlusFrameCue.h"

namespace Paper2DPlusFrameCueDispatchInternal
{
	void Notify(
		UPaper2DPlusCueBase& Cue,
		const FPaper2DPlusFrameCueContext& BaseContext,
		EPaper2DPlusFrameCuePhase Phase,
		EPaper2DPlusFrameCueEndReason EndReason,
		bool bCompressed,
		TFunctionRef<void(UPaper2DPlusCueBase&, const FPaper2DPlusFrameCueContext&)> OnNotification)
	{
		FPaper2DPlusFrameCueContext Context = BaseContext;
		Context.Phase = Phase;
		Context.EndReason = EndReason;
		Context.bIsCompressed = bCompressed;
		OnNotification(Cue, Context);
	}

	void SortCuesByPath(TArray<TObjectPtr<UPaper2DPlusCueBase>>& Cues)
	{
		Cues.Sort([](const UPaper2DPlusCueBase& A, const UPaper2DPlusCueBase& B)
		{
			return A.GetPathName() < B.GetPathName();
		});
	}
}

bool Paper2DPlusFrameCues::WasFrameTraversed(
	int32 Frame,
	int32 PreviousFrame,
	int32 CurrentFrame,
	bool bWasLoopWrap)
{
	if (Frame < 0 || CurrentFrame == INDEX_NONE)
	{
		return false;
	}
	if (PreviousFrame == INDEX_NONE)
	{
		return Frame == CurrentFrame;
	}
	if (PreviousFrame == CurrentFrame)
	{
		return false;
	}
	if (bWasLoopWrap)
	{
		return Frame > PreviousFrame || Frame <= CurrentFrame;
	}
	if (PreviousFrame < CurrentFrame)
	{
		return Frame > PreviousFrame && Frame <= CurrentFrame;
	}
	// Reverse playback is not a plugin feature today. Preserve the legacy target-frame fallback.
	return Frame == CurrentFrame;
}

bool Paper2DPlusFrameCues::WasFrameTrailingEdgeCrossed(
	int32 Frame,
	int32 PreviousFrame,
	int32 CurrentFrame,
	bool bWasLoopWrap)
{
	if (Frame < 0 || CurrentFrame == INDEX_NONE)
	{
		return false;
	}
	if (PreviousFrame == INDEX_NONE)
	{
		// A fresh seek or a new animation ENTERED a frame; nothing has been departed yet.
		return false;
	}
	if (PreviousFrame == CurrentFrame)
	{
		return false;
	}
	if (bWasLoopWrap)
	{
		// Wrapping from Prev to Current departs [Prev, last] and then [0, Current).
		return Frame >= PreviousFrame || Frame < CurrentFrame;
	}
	if (PreviousFrame < CurrentFrame)
	{
		// The closed-open mirror of the start-edge rule: the departed frames, not the entered ones.
		return Frame >= PreviousFrame && Frame < CurrentFrame;
	}
	// A backward seek crosses no boundary forward; nothing completed.
	return false;
}

void Paper2DPlusFrameCues::ForceEndActiveRanges(
	TArrayView<const TObjectPtr<UPaper2DPlusCueBase>> AuthoredOrder,
	const FPaper2DPlusFrameCueContext& BaseContext,
	EPaper2DPlusFrameCueEndReason EndReason,
	TSet<TObjectPtr<UPaper2DPlusCueBase>>& ActiveRanges,
	TFunctionRef<void(UPaper2DPlusCueBase&, const FPaper2DPlusFrameCueContext&)> OnNotification)
{
	// Teardown pairs every Begin, so the gate here is liveness only — deliberately IsValid and NOT
	// IsPlacementResolvable. A range whose Cue Type class died while it was active is still an alive
	// object holding a receiver-owned effect, and it must receive the End that closes it; behavior is
	// already skipped for it because ExecuteCueBehavior self-gates on IsPlacementResolvable. This keeps
	// the force-end boundaries byte-consistent with DispatchFrameTransition's stale sweep, which ends
	// the same orphan state for the same reason. Liveness INCLUDES reachability: a cue the GC's
	// reachability pass has already marked passes IsValid but cannot receive ProcessEvent
	// (!IsUnreachable assert), so a mid-GC entry is dropped without its End — nothing can safely
	// receive a dying object anyway.
	TSet<TObjectPtr<UPaper2DPlusCueBase>> Ended;
	for (const TObjectPtr<UPaper2DPlusCueBase>& CuePtr : AuthoredOrder)
	{
		UPaper2DPlusCueBase* Cue = CuePtr.Get();
		if (IsValid(Cue) && !Cue->IsUnreachable() && ActiveRanges.Contains(Cue))
		{
			Paper2DPlusFrameCueDispatchInternal::Notify(
				*Cue, BaseContext, EPaper2DPlusFrameCuePhase::End, EndReason, false, OnNotification);
			Ended.Add(Cue);
		}
	}

	TArray<TObjectPtr<UPaper2DPlusCueBase>> Remaining;
	for (const TObjectPtr<UPaper2DPlusCueBase>& Cue : ActiveRanges)
	{
		if (IsValid(Cue) && !Cue->IsUnreachable() && !Ended.Contains(Cue))
		{
			Remaining.Add(Cue);
		}
	}
	Paper2DPlusFrameCueDispatchInternal::SortCuesByPath(Remaining);
	for (UPaper2DPlusCueBase* Cue : Remaining)
	{
		Paper2DPlusFrameCueDispatchInternal::Notify(
			*Cue, BaseContext, EPaper2DPlusFrameCuePhase::End, EndReason, false, OnNotification);
	}
	ActiveRanges.Empty();
}

void Paper2DPlusFrameCues::DispatchFrameTransition(
	TArrayView<const TObjectPtr<UPaper2DPlusCueBase>> Cues,
	const FPaper2DPlusFrameCueContext& BaseContext,
	TSet<TObjectPtr<UPaper2DPlusCueBase>>& ActiveRanges,
	TFunctionRef<bool(UPaper2DPlusCueBase&)> ShouldDispatch,
	TFunctionRef<void(UPaper2DPlusCueBase&, const FPaper2DPlusFrameCueContext&)> OnNotification)
{
	// A placement whose Cue Type was deleted is deliberately NOT present: it can neither run behavior
	// nor serve payload, so an active range that lost its class still receives its paired End through
	// the stale sweep below instead of being stranded.
	TSet<TObjectPtr<UPaper2DPlusCueBase>> Present;
	Present.Reserve(Cues.Num());
	for (const TObjectPtr<UPaper2DPlusCueBase>& Cue : Cues)
	{
		if (Paper2DPlusFrameCueBehavior::IsPlacementResolvable(Cue))
		{
			Present.Add(Cue);
		}
	}

	TSet<TObjectPtr<UPaper2DPlusCueBase>> ActiveBeforeLoop;
	if (BaseContext.bWasLoopWrap && ActiveRanges.Num() > 0)
	{
		ActiveBeforeLoop = ActiveRanges;
		for (const TObjectPtr<UPaper2DPlusCueBase>& Cue : Cues)
		{
			if (Paper2DPlusFrameCueBehavior::IsPlacementResolvable(Cue) && ActiveRanges.Contains(Cue))
			{
				// Loop-seam continuity: a range still containing the post-wrap frame stays active so the
				// main loop fires only Update (an authority-only tag cue must not remove/re-add its tag
				// every loop). Same in-range predicate the main loop bInRange uses. Ranges that do not
				// cover the post-wrap frame still take the LoopReset force-end below.
				if (Cue->ContainsFrame(BaseContext.CurrentFrame))
				{
					continue;
				}
				Paper2DPlusFrameCueDispatchInternal::Notify(
					*Cue,
					BaseContext,
					EPaper2DPlusFrameCuePhase::End,
					EPaper2DPlusFrameCueEndReason::LoopReset,
					false,
					OnNotification);
				ActiveRanges.Remove(Cue);
			}
		}
	}

	for (const TObjectPtr<UPaper2DPlusCueBase>& CuePtr : Cues)
	{
		UPaper2DPlusCueBase* Cue = CuePtr.Get();
		if (!Paper2DPlusFrameCueBehavior::IsPlacementResolvable(Cue))
		{
			continue;
		}

		const bool bWasActive = ActiveRanges.Contains(Cue);
		const bool bAllowed = bWasActive || ShouldDispatch(*Cue);
		if (!bAllowed)
		{
			continue;
		}

		if (!Cue->IsRangeCue())
		{
			// The placement's trigger edge selects which boundary predicate fires it. Only
			// UPaper2DPlusCue carries the edge; any other moment shape keeps the legacy start
			// rule. The end edge's natural-completion case never reaches this funnel — the
			// profile component fires it from its Completed terminal drain.
			const UPaper2DPlusCue* Moment = Cast<UPaper2DPlusCue>(Cue);
			const bool bFiresOnTrailingEdge = Moment
				&& Moment->TriggerEdge == EPaper2DPlusCueTriggerEdge::FrameEnd;
			const bool bBoundaryCrossed = bFiresOnTrailingEdge
				? WasFrameTrailingEdgeCrossed(
					Cue->GetPrimaryAnchorFrame(),
					BaseContext.PreviousFrame,
					BaseContext.CurrentFrame,
					BaseContext.bWasLoopWrap)
				: WasFrameTraversed(
					Cue->GetPrimaryAnchorFrame(),
					BaseContext.PreviousFrame,
					BaseContext.CurrentFrame,
					BaseContext.bWasLoopWrap);
			if (bBoundaryCrossed)
			{
				Paper2DPlusFrameCueDispatchInternal::Notify(
					*Cue,
					BaseContext,
					EPaper2DPlusFrameCuePhase::Trigger,
					EPaper2DPlusFrameCueEndReason::None,
					false,
					OnNotification);
			}
			continue;
		}

		const bool bInRange = Cue->ContainsFrame(BaseContext.CurrentFrame);
		bool bTraversedRange = false;
		for (int32 Offset = 0; Offset < Cue->GetCueFrameCount(); ++Offset)
		{
			if (WasFrameTraversed(
				Cue->GetPrimaryAnchorFrame() + Offset,
				BaseContext.PreviousFrame,
				BaseContext.CurrentFrame,
				BaseContext.bWasLoopWrap))
			{
				bTraversedRange = true;
				break;
			}
		}

		if (bInRange)
		{
			if (!bWasActive)
			{
				Paper2DPlusFrameCueDispatchInternal::Notify(
					*Cue, BaseContext, EPaper2DPlusFrameCuePhase::Begin,
					EPaper2DPlusFrameCueEndReason::None, false, OnNotification);
				ActiveRanges.Add(Cue);
			}
			if (Cue->ShouldEmitUpdates())
			{
				Paper2DPlusFrameCueDispatchInternal::Notify(
					*Cue, BaseContext, EPaper2DPlusFrameCuePhase::Update,
					EPaper2DPlusFrameCueEndReason::None, false, OnNotification);
			}
		}
		else if (bWasActive)
		{
			Paper2DPlusFrameCueDispatchInternal::Notify(
				*Cue, BaseContext, EPaper2DPlusFrameCuePhase::End,
				EPaper2DPlusFrameCueEndReason::Completed, false, OnNotification);
			ActiveRanges.Remove(Cue);
		}
		else if (bTraversedRange && !ActiveBeforeLoop.Contains(Cue))
		{
			Paper2DPlusFrameCueDispatchInternal::Notify(
				*Cue, BaseContext, EPaper2DPlusFrameCuePhase::Begin,
				EPaper2DPlusFrameCueEndReason::None, true, OnNotification);
			if (Cue->ShouldEmitUpdates())
			{
				Paper2DPlusFrameCueDispatchInternal::Notify(
					*Cue, BaseContext, EPaper2DPlusFrameCuePhase::Update,
					EPaper2DPlusFrameCueEndReason::None, true, OnNotification);
			}
			Paper2DPlusFrameCueDispatchInternal::Notify(
				*Cue, BaseContext, EPaper2DPlusFrameCuePhase::End,
				EPaper2DPlusFrameCueEndReason::Completed, true, OnNotification);
		}
	}

	TArray<TObjectPtr<UPaper2DPlusCueBase>> Stale;
	for (const TObjectPtr<UPaper2DPlusCueBase>& Active : ActiveRanges)
	{
		if (!Present.Contains(Active))
		{
			Stale.Add(Active);
		}
	}
	Paper2DPlusFrameCueDispatchInternal::SortCuesByPath(Stale);
	for (UPaper2DPlusCueBase* Cue : Stale)
	{
		if (IsValid(Cue) && !Cue->IsUnreachable())
		{
			Paper2DPlusFrameCueDispatchInternal::Notify(
				*Cue, BaseContext, EPaper2DPlusFrameCuePhase::End,
				EPaper2DPlusFrameCueEndReason::SourceRemoved, false, OnNotification);
		}
		ActiveRanges.Remove(Cue);
	}
}
