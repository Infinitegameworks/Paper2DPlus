// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "FrameCues/Paper2DPlusFrameCueTabPresentation.h"

#include "FrameCues/Paper2DPlusFrameCue.h"
#include "FrameCues/Paper2DPlusFrameCueTypeAuthoring.h"
#include "UObject/UnrealType.h"

#define LOCTEXT_NAMESPACE "Paper2DPlusFrameCueTabPresentation"

namespace Paper2DPlusFrameCueTabPresentation
{

bool IsMigrationProperty(const FProperty* Property)
{
#if WITH_EDITORONLY_DATA
	if (!Property)
	{
		return false;
	}
	// Resolved by identity, not by name: a designer Cue Type is free to author its own field called
	// something similar, and only the two runtime-owned provenance fields are migration bookkeeping.
	static const FProperty* MigratedFlag = FindFProperty<FProperty>(
		UPaper2DPlusCueBase::StaticClass(),
		GET_MEMBER_NAME_CHECKED(UPaper2DPlusCueBase, bMigratedFromFrameEvent));
	static const FProperty* AcknowledgedFlag = FindFProperty<FProperty>(
		UPaper2DPlusCueBase::StaticClass(),
		GET_MEMBER_NAME_CHECKED(UPaper2DPlusCueBase, bMigrationReceiverAcknowledged));
	return Property == MigratedFlag || Property == AcknowledgedFlag;
#else
	return false;
#endif
}

bool CarriesLegacyFrameEventData(const UPaper2DPlusCueBase* Placement)
{
#if WITH_EDITORONLY_DATA
	return IsValid(Placement) && Placement->bMigratedFromFrameEvent;
#else
	return false;
#endif
}

bool IsPlacementDetailsPropertyVisible(
	const FProperty* Property,
	const UPaper2DPlusCueBase* Placement)
{
	if (!FPaper2DPlusFrameCueTypeAuthoring::IsCueAuthoringPropertyVisible(Property))
	{
		return false;
	}
	// Migration-era chrome is only ever true of an asset that actually carries the retired provenance.
	// On everything else it is a permanently-empty category the designer has to scroll past.
	return !IsMigrationProperty(Property) || CarriesLegacyFrameEventData(Placement);
}

FText DescribeNetPolicy(const EPaper2DPlusFrameCueNetPolicy NetPolicy)
{
	switch (NetPolicy)
	{
	case EPaper2DPlusFrameCueNetPolicy::AuthorityOnly:
		return LOCTEXT(
			"NetPolicyAuthorityOnly",
			"Runs only where the game has authority — the server, or a standalone game.");
	case EPaper2DPlusFrameCueNetPolicy::CosmeticOnly:
		return LOCTEXT(
			"NetPolicyCosmeticOnly",
			"Runs everywhere except a dedicated server, so effects stay off the headless build.");
	case EPaper2DPlusFrameCueNetPolicy::OwnerOnly:
		return LOCTEXT(
			"NetPolicyOwnerOnly",
			"Runs only on the machine that locally controls this character.");
	case EPaper2DPlusFrameCueNetPolicy::LocalAlways:
	default:
		return LOCTEXT(
			"NetPolicyLocalAlways",
			"Runs on every machine that plays this animation, including a dedicated server.");
	}
}

FText DescribeEndReasons(const UPaper2DPlusCueBase* Placement)
{
	if (!IsValid(Placement) || !Placement->IsRangeCue())
	{
		return FText::GetEmpty();
	}
	return LOCTEXT(
		"RangeEndReasonSummary",
		"Every Begin is paired with an End; the End carries an End Reason saying whether the Cue State "
		"finished or was cut short by an animation change, a stop, or teardown.");
}

FText BuildPlacementDetailsSummary(const UPaper2DPlusCueBase* Placement)
{
	if (!IsValid(Placement))
	{
		// Says only what a null placement actually proves. The caller reads a weak pointer, so null here
		// usually means the placement went away — deleted, or reinstanced by a Cue Type recompile —
		// between that edit and the next selection reconcile. It is NOT evidence about run-time dispatch,
		// and the pane must not claim otherwise to a designer who still has a live, working Cue.
		return LOCTEXT(
			"CuePlacementDetailsUnresolved",
			"Cue placement. This selection is no longer resolved — select it again on the timeline.");
	}
	const FText Lead = Placement->IsRangeCue()
		? LOCTEXT(
			"RangeCuePlacementDetails",
			"Cue State placement. Drag it on the timeline to move or resize it; edit its payload here.")
		: LOCTEXT(
			"MomentCuePlacementDetails",
			"Cue placement. Drag it on the timeline to move it; edit its payload here.");
	const FText EndReasons = DescribeEndReasons(Placement);
	if (EndReasons.IsEmpty())
	{
		return FText::Format(
			LOCTEXT("CuePlacementDetailsFmt", "{0}\n{1}"),
			Lead,
			DescribeNetPolicy(Placement->NetPolicy));
	}
	return FText::Format(
		LOCTEXT("CuePlacementRangeDetailsFmt", "{0}\n{1}\n{2}"),
		Lead,
		DescribeNetPolicy(Placement->NetPolicy),
		EndReasons);
}

FText BuildTrackDetailsSummary(const bool bNamedTrack)
{
	return bNamedTrack
		? LOCTEXT(
			"NamedTrackDetails",
			"Named Cue track. Track organization never changes timing, payload, or runtime dispatch.")
		: LOCTEXT(
			"DefaultTrackDetails",
			"Default Cue track. This is also the fallback for placements without authored organization.");
}

FText BuildCurveDetailsSummary(const FName CurveName, const bool bEditable)
{
	return FText::Format(
		bEditable
			? LOCTEXT(
				"CurveDetailsEditable",
				"Curve: {0}. Edit its keys in the combined graph below the Cue tracks.")
			: LOCTEXT(
				"CurveDetailsReadOnly",
				"Curve: {0}. Profile-owned data is read-only in this Layer view."),
		FText::FromName(CurveName));
}

FText BuildEmptyDetailsSummary()
{
	return LOCTEXT("TimelineDetailsEmpty", "Select a Cue, track, or curve in the timeline.");
}

} // namespace Paper2DPlusFrameCueTabPresentation

#undef LOCTEXT_NAMESPACE
