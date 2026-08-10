// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Types/SlateEnums.h"

class FActiveTimerHandle;
class SWidget;

/**
 * Deferred keyboard-focus seat shared by the Frame Cues, Frame Timing, and Root Motion tools.
 *
 * Activating a tool tab must give its panel keyboard focus so the first Space press starts playback
 * without a preparatory click. A synchronous focus move re-enters SDockTab activation before Slate
 * commits its focus path, so the seat defers one paint through the panel's own RegisterActiveTimer
 * (a protected SWidget seam — the panel registers the timer, this helper decides and applies).
 *
 * The seat decision itself is the pure SlateShortcutUtils::ShouldSeatDeferredHostFocus predicate:
 * it re-seats over stale Space-swallowing descendants (a leftover focused button eats Space) while
 * yielding to Space-forwarding widgets and a live text caret.
 */
class PAPER2DPLUSEDITOR_API FProfilePanelFocusSeat
{
public:
	/** True while the deferred seat is committing focus. Docking activates the tab synchronously from
	 *  OnFocusChanging, so the owning panel's HandleHostActivated must early-return on this — that
	 *  re-entry must not repeat the full refresh while focus is being committed. */
	bool IsApplying() const { return bApplying; }

	/** Whether activation should register the one-paint deferred timer: Slate is up, no seat timer is
	 *  already pending, and the focus predicate says the panel still needs keyboard focus. */
	bool ShouldRequestSeat(const SWidget& Panel) const;

	/** Adopt the handle from the panel's RegisterActiveTimer so a second activation cannot stack timers. */
	void TrackTimer(const TSharedRef<FActiveTimerHandle>& Handle);

	/** Pending seat timer, if any. Panel test seams cancel it through their UnRegisterActiveTimer. */
	TSharedPtr<FActiveTimerHandle> GetPendingTimer() const;

	/** The timer body: re-evaluates the predicate at fire time (focus may have legitimately settled
	 *  during the deferred paint) and seats keyboard focus on the panel. Always drains the tracked
	 *  handle and stops the timer. */
	EActiveTimerReturnType ApplySeat(const TSharedRef<SWidget>& Panel, bool bHostActive);

private:
	/** Resolves the live focus facts and feeds the pure seat predicate. */
	bool ShouldSeatNow(const SWidget& Panel) const;

	TWeakPtr<FActiveTimerHandle> TimerHandle;
	bool bApplying = false;
};
