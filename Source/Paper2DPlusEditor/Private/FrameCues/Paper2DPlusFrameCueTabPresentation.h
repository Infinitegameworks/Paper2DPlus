// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

// Self-contained on 5.0-5.8 without the shared PCH: every type these declarations name is either
// included here (FText returned by value, FName taken by value) or forward-declared below. Nothing here
// names a UObject container type, so no ObjectPtr.h dependency is carried either.
#include "CoreMinimal.h"
#include "Internationalization/Text.h"
#include "UObject/NameTypes.h"

class FProperty;
class UPaper2DPlusCueBase;
enum class EPaper2DPlusFrameCueNetPolicy : uint8;

/**
 * Worldless presentation rules for the Frame Cues tab.
 *
 * The tab's default surface is pick a frame, add a Cue, edit its properties, preview. Everything that
 * is NOT that sequence — migration bookkeeping, network policy, range end reasons — belongs in the
 * details pane, and only when it has something to say. These are pure functions so both the Slate
 * panel and headless automation ask the same question and get the same answer; none of them loads,
 * mutates, or needs a world.
 *
 * They live outside FrameEventEditor.cpp deliberately: that file is far past the repo's split
 * threshold, so new tab UI logic lands here instead of growing it.
 */
namespace Paper2DPlusFrameCueTabPresentation
{
	/** True for the two placement fields that only exist to record a retired Frame Event import. */
	bool IsMigrationProperty(const FProperty* Property);

	/**
	 * True when this placement was imported from the retired executable Frame Event model.
	 *
	 * The migration gate is PER PLACEMENT on purpose: one legacy import in an animation must not put
	 * migration bookkeeping on the details pane of every clean sibling next to it.
	 */
	bool CarriesLegacyFrameEventData(const UPaper2DPlusCueBase* Placement);

	/**
	 * Property filter for the Cue details pane.
	 *
	 * Timing stays out because the timeline owns it. Migration bookkeeping — including the receiver
	 * acknowledgement checkbox — appears only for a placement that actually came from a Frame Event;
	 * pass a null placement (Cue Type defaults) and it is hidden, because a Cue Type is never itself
	 * migrated. Everything else, network policy included, stays visible and editable.
	 */
	bool IsPlacementDetailsPropertyVisible(
		const FProperty* Property,
		const UPaper2DPlusCueBase* Placement);

	/** One designer-facing sentence for where a cue's behavior and listeners run in a networked game. */
	FText DescribeNetPolicy(EPaper2DPlusFrameCueNetPolicy NetPolicy);

	/** Range-only sentence naming the End Reason channel, so the concept has a home off the timeline. */
	FText DescribeEndReasons(const UPaper2DPlusCueBase* Placement);

	/** Details-pane header for a selected placement: what it is, where it runs, how it ends. */
	FText BuildPlacementDetailsSummary(const UPaper2DPlusCueBase* Placement);

	/** Details-pane header for a selected Cue track. Tracks never change timing or dispatch. */
	FText BuildTrackDetailsSummary(bool bNamedTrack);

	/** Details-pane header for a selected curve. */
	FText BuildCurveDetailsSummary(FName CurveName, bool bEditable);

	/** Details-pane header when nothing is selected. */
	FText BuildEmptyDetailsSummary();
}
