// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "Paper2DPlusFlipbookComponent.h"
#include "PaperFlipbook.h"

/** UPaper2DPlusFlipbookComponent — PaperFlipbookComponent subclass that fires OnFlipbookChanged and OnFrameChanged delegates. */

UPaper2DPlusFlipbookComponent::UPaper2DPlusFlipbookComponent()
{
	// Deliberately NOT bound here. A constructor binding lands on the CDO and is serialized into every
	// Blueprint component template, where designers see a bound event they never added and cannot tell
	// from their own. OnRegister is where this component acquires its own listener.
}

void UPaper2DPlusFlipbookComponent::OnRegister()
{
	Super::OnRegister();
	EnsureNaturalFinishListenerBound();
}

void UPaper2DPlusFlipbookComponent::EnsureNaturalFinishListenerBound()
{
	// AddUnique is a short scan of this delegate's own invocation list — nothing next to the tick it
	// guards — and it is the whole defence against game code that legitimately clears a delegate the
	// engine, not this plugin, published.
	OnFinishedPlaying.AddUniqueDynamic(this, &UPaper2DPlusFlipbookComponent::HandleFinishedPlaying);
}

bool UPaper2DPlusFlipbookComponent::DidLikelyFinishNaturally(
	bool bWasPlayingBeforeSuper,
	float PositionBeforeSuper,
	float DeltaTime) const
{
	// Every input is a PRE-Super value, so a listener that restarted playback inside Super cannot mask
	// the finish. Looping sources never terminate, and a source that was not playing cannot finish.
	if (!bWasPlayingBeforeSuper || IsLooping())
	{
		return false;
	}
	const float TimelineLength = GetFlipbookLength();
	if (TimelineLength <= 0.0f)
	{
		return false;
	}
	// Mirrors UPaperFlipbookComponent::TickFlipbook's own terminal test, including the reverse-playback
	// sign, so this cannot claim a finish on a direction the engine was not actually running.
	const float EffectiveDeltaTime = DeltaTime * (IsReversing() ? -GetPlayRate() : GetPlayRate());
	const float NewPosition = PositionBeforeSuper + EffectiveDeltaTime;
	return EffectiveDeltaTime > 0.0f
		? NewPosition > TimelineLength
		: NewPosition < 0.0f;
}

bool UPaper2DPlusFlipbookComponent::SetFlipbook(UPaperFlipbook* NewFlipbook)
{
	const bool bResult = Super::SetFlipbook(NewFlipbook);
	if (bResult)
	{
		// Seed PreviousCachedFrameIndex to match CachedFrameIndex so TickComponent
		// does NOT re-broadcast OnFrameChanged for frame 0 — the OnFlipbookChanged
		// path handles initial-frame dispatch, otherwise ranged event Tick fires twice.
		++PlaybackObservationEpoch;
		PreviousCachedFrameIndex = CachedFrameIndex;
		bObservedPlaying = IsPlaying();
		OnFlipbookChangedNative.Broadcast(this, NewFlipbook);
		OnFlipbookChanged.Broadcast(NewFlipbook);
	}
	return bResult;
}

void UPaper2DPlusFlipbookComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	const uint64 TickObservationEpoch = PlaybackObservationEpoch;
	UPaperFlipbook* const TickFlipbook = GetFlipbook();
	const bool bPreviouslyObservedPlaying = bObservedPlaying;
	const bool bPlayingAtTickStart = IsPlaying();
	const bool bStartedBeforeSuper = !bPreviouslyObservedPlaying && bPlayingAtTickStart;

	bFinishedNaturallyDuringSuperTick = false;
	if (OnPlaybackObservedNative.IsBound())
	{
		OnPlaybackObservedNative.Broadcast(this);
	}
	if (IsBeingDestroyed()
		|| PlaybackObservationEpoch != TickObservationEpoch
		|| GetFlipbook() != TickFlipbook)
	{
		return;
	}

	// Same-flipbook Stop()->Play() restarts can leave the cached key-frame index
	// unchanged. Announce a restart before Super so its consumer can open a new
	// playback generation before any frame delivery.
	if (bStartedBeforeSuper)
	{
		bObservedPlaying = true;
		OnPlaybackStartedNative.Broadcast(this);
		if (IsBeingDestroyed()
			|| PlaybackObservationEpoch != TickObservationEpoch
			|| GetFlipbook() != TickFlipbook)
		{
			return;
		}
	}

	// Heal a listener the game removed between ticks, BEFORE the tick that needs it. Ownership of
	// OnFinishedPlaying is shared with project code by construction, so the binding is re-asserted
	// rather than assumed.
	EnsureNaturalFinishListenerBound();

	const bool bPlayingBeforeSuper = IsPlaying();
	const float PositionBeforeSuper = GetPlaybackPosition();

	bIsRunningSuperTick = true;
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	bIsRunningSuperTick = false;

	// Backstop for the window where the listener was absent anyway (a Clear() from inside another
	// listener during this very Super, say). Reads only pre-Super state, so it agrees with the
	// listener rather than second-guessing it, and it can only ever ADD a finish the listener missed.
	if (!bFinishedNaturallyDuringSuperTick
		&& DidLikelyFinishNaturally(bPlayingBeforeSuper, PositionBeforeSuper, DeltaTime)
		&& !IsPlaying())
	{
		bFinishedNaturallyDuringSuperTick = true;
		OnNaturalFinishDetectedNative.Broadcast(this);
	}

	const auto DidTickSourceChange = [this, TickObservationEpoch, TickFlipbook]()
	{
		return IsBeingDestroyed()
			|| PlaybackObservationEpoch != TickObservationEpoch
			|| GetFlipbook() != TickFlipbook;
	};
	const auto AbortIfTickSourceChanged = [this, &DidTickSourceChange]()
	{
		if (!DidTickSourceChange())
		{
			return false;
		}

		if (bFinishedNaturallyDuringSuperTick)
		{
			OnPlaybackTerminalNative.Broadcast(this, true);
		}
		return true;
	};

	// SetFlipbook may run reentrantly during Super or the pre-Super started
	// notification. It seeds the new source's observation state, so this old tick
	// must not overwrite it or emit transitions labeled with the old identity.
	// A natural completion is the exception: it was claimed synchronously before
	// the mutation and this post-Super signal is the safe-drain boundary.
	if (AbortIfTickSourceChanged())
	{
		return;
	}

	// Super::TickComponent updates CachedFrameIndex via CalculateCurrentFrame().
	// We only fire on valid->valid transitions; the initial frame after SetFlipbook
	// is dispatched via the OnFlipbookChanged path, not here.
	if (CachedFrameIndex != PreviousCachedFrameIndex && CachedFrameIndex != INDEX_NONE)
	{
		PreviousCachedFrameIndex = CachedFrameIndex;
		OnFrameChangedNative.Broadcast(this, CachedFrameIndex);
		if (AbortIfTickSourceChanged())
		{
			return;
		}

		OnFrameChanged.Broadcast(CachedFrameIndex);
		if (AbortIfTickSourceChanged())
		{
			return;
		}
	}

	const bool bCurrentlyPlaying = IsPlaying();
	const bool bWasPlayingThisObservation = bPreviouslyObservedPlaying || bStartedBeforeSuper;
	if (bFinishedNaturallyDuringSuperTick)
	{
		// OnFinishedPlaying proves a transient terminal boundary even if a later
		// inherited listener restarted the same source before Super returned.
		// Drain Completed after the final frame, then expose any restart as a new
		// generation. A SetFlipbook from either listener invalidates the latter;
		// its flipbook-change notification owns the replacement session.
		bObservedPlaying = false;
		OnPlaybackTerminalNative.Broadcast(this, true);
		if (DidTickSourceChange())
		{
			return;
		}

		if (IsPlaying())
		{
			bObservedPlaying = true;
			OnPlaybackStartedNative.Broadcast(this);
		}
	}
	else if (bWasPlayingThisObservation && !bCurrentlyPlaying)
	{
		// Commit before broadcasting so a same-source Play() from a terminal
		// listener is observed as a fresh start on the next tick. SetFlipbook()
		// reentrancy supersedes this value by incrementing the epoch and reseeding.
		bObservedPlaying = false;
		OnPlaybackTerminalNative.Broadcast(this, false);
	}
	else if (!bWasPlayingThisObservation && bCurrentlyPlaying)
	{
		bObservedPlaying = true;
		OnPlaybackStartedNative.Broadcast(this);
	}
	else
	{
		bObservedPlaying = bCurrentlyPlaying;
	}
}

void UPaper2DPlusFlipbookComponent::OnComponentDestroyed(const bool bDestroyingHierarchy)
{
	Super::OnComponentDestroyed(bDestroyingHierarchy);
	OnPlaybackSourceDestroyedNative.Broadcast(this);
	OnPlaybackSourceDestroyedNative.Clear();
}

void UPaper2DPlusFlipbookComponent::HandleFinishedPlaying()
{
	if (!bIsRunningSuperTick)
	{
		return;
	}

	bFinishedNaturallyDuringSuperTick = true;
	OnNaturalFinishDetectedNative.Broadcast(this);
}
