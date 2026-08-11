// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PaperFlipbookComponent.h"
#include "Paper2DPlusFlipbookComponent.generated.h"

class UPaper2DPlusFlipbookComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPaper2DPlusFlipbookChanged, UPaperFlipbook*, NewFlipbook);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPaper2DPlusFrameChanged, int32, NewFrameIndex);
DECLARE_MULTICAST_DELEGATE_TwoParams(
	FOnPaper2DPlusFlipbookChangedNative,
	UPaper2DPlusFlipbookComponent*,
	UPaperFlipbook*);
DECLARE_MULTICAST_DELEGATE_TwoParams(
	FOnPaper2DPlusFrameChangedNative,
	UPaper2DPlusFlipbookComponent*,
	int32);
DECLARE_MULTICAST_DELEGATE_OneParam(
	FOnPaper2DPlusPlaybackStartedNative,
	UPaper2DPlusFlipbookComponent*);
DECLARE_MULTICAST_DELEGATE_OneParam(
	FOnPaper2DPlusPlaybackObservedNative,
	UPaper2DPlusFlipbookComponent*);
DECLARE_MULTICAST_DELEGATE_OneParam(
	FOnPaper2DPlusNaturalFinishDetectedNative,
	UPaper2DPlusFlipbookComponent*);
DECLARE_MULTICAST_DELEGATE_TwoParams(
	FOnPaper2DPlusPlaybackTerminalNative,
	UPaper2DPlusFlipbookComponent*,
	bool);
DECLARE_MULTICAST_DELEGATE_OneParam(
	FOnPaper2DPlusPlaybackSourceDestroyedNative,
	UPaper2DPlusFlipbookComponent*);

/**
 * Drop-in replacement for UPaperFlipbookComponent that fires:
 *  - OnFlipbookChanged when SetFlipbook changes the active flipbook
 *  - OnFrameChanged when the playing flipbook advances to a new key frame
 *
 * UPaper2DPlusCharacterProfileComponent auto-detects this subclass and uses
 * both delegates for zero-overhead event-driven root motion + effect spawning
 * instead of polling its own TickComponent.
 */
UCLASS(ClassGroup=(Paper2DPlus), meta=(BlueprintSpawnableComponent, DisplayName="Paper2DPlus Flipbook Component"))
class PAPER2DPLUS_API UPaper2DPlusFlipbookComponent : public UPaperFlipbookComponent
{
	GENERATED_BODY()

public:
	UPaper2DPlusFlipbookComponent();

	/** Fired when SetFlipbook changes the active flipbook. */
	UPROPERTY(BlueprintAssignable, Category = "Paper2DPlus")
	FOnPaper2DPlusFlipbookChanged OnFlipbookChanged;

	/**
	 * Fired when the playing flipbook advances to a new key-frame index.
	 * Only fires on valid->valid transitions during playback; the initial frame
	 * after SetFlipbook is dispatched via the OnFlipbookChanged path, not here.
	 */
	UPROPERTY(BlueprintAssignable, Category = "Paper2DPlus")
	FOnPaper2DPlusFrameChanged OnFrameChanged;

	/** Native-only sender-aware counterpart to OnFlipbookChanged. Fires first. */
	FOnPaper2DPlusFlipbookChangedNative OnFlipbookChangedNative;

	/** Native-only sender-aware counterpart to OnFrameChanged. Fires first. */
	FOnPaper2DPlusFrameChangedNative OnFrameChangedNative;

	/** Native-only: fires when an observed stopped component begins playing. */
	FOnPaper2DPlusPlaybackStartedNative OnPlaybackStartedNative;

	/**
	 * Native-only per-tick binding observation. This lets a bound Profile Component detect a legacy
	 * direct C++ reassignment of its public source even while the old custom source is quiescent.
	 */
	FOnPaper2DPlusPlaybackObservedNative OnPlaybackObservedNative;

	/**
	 * Native-only: fires from this component's first OnFinishedPlaying listener.
	 * This is an early natural-terminal claim; final-frame delivery and safe
	 * lifecycle draining still complete through OnPlaybackTerminalNative.
	 */
	FOnPaper2DPlusNaturalFinishDetectedNative OnNaturalFinishDetectedNative;

	/**
	 * Native-only: fires after final-frame notification for a natural completion,
	 * or when observed playback otherwise becomes stopped. bCompletedNaturally
	 * is true only when OnFinishedPlaying fired during that same Super tick.
	 */
	FOnPaper2DPlusPlaybackTerminalNative OnPlaybackTerminalNative;

	/** Native-only: fires as this playback source is removed so active Cue States can close safely. */
	FOnPaper2DPlusPlaybackSourceDestroyedNative OnPlaybackSourceDestroyedNative;

	virtual bool SetFlipbook(UPaperFlipbook* NewFlipbook) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;
	virtual void OnRegister() override;

private:
	/** First inherited finish listener; latches and announces natural completion during Super tick only. */
	UFUNCTION()
	void HandleFinishedPlaying();

	/**
	 * Re-arm the inherited OnFinishedPlaying listener if anything has removed it.
	 *
	 * OnFinishedPlaying is public API of the engine's UPaperFlipbookComponent and game code manages it
	 * routinely — the stock "Unbind All Events from On Finished Playing" node, or a C++ Clear() when
	 * re-arming a one-shot. Removing the project's own handler removed this component's too, and with
	 * it the only signal that separates a natural finish from an ordinary stop, so every in-flight Cue
	 * State ended PlaybackStopped instead of Completed.
	 *
	 * Called from OnRegister (never the constructor, so nothing is serialized into Blueprint component
	 * templates) and again before each Super::TickComponent, which is the last moment it can matter.
	 */
	void EnsureNaturalFinishListenerBound();

	/**
	 * Reconstruct "did Super finish this flipbook naturally" from state captured BEFORE Super ran.
	 *
	 * The listener above is the ground truth because it fires at the exact instant the engine decides
	 * playback ended — before any other listener can restart it. This is the backstop for the window
	 * where it is somehow absent: it reads only pre-Super values, so a listener restarting playback
	 * cannot hide the finish from it either.
	 */
	bool DidLikelyFinishNaturally(
		bool bWasPlayingBeforeSuper,
		float PositionBeforeSuper,
		float DeltaTime) const;

	/** Last key-frame index observed during TickComponent. Reset on SetFlipbook. */
	int32 PreviousCachedFrameIndex = INDEX_NONE;

	/** Playing state committed by the last completed observation. */
	bool bObservedPlaying = false;

	/** True only while this override is inside Super::TickComponent. */
	bool bIsRunningSuperTick = false;

	/** Set by HandleFinishedPlaying while Super advances a non-looping flipbook to its terminal time. */
	bool bFinishedNaturallyDuringSuperTick = false;

	/** Incremented for every successful SetFlipbook so reentrant source mutations invalidate an old tick. */
	uint64 PlaybackObservationEpoch = 0;
};
