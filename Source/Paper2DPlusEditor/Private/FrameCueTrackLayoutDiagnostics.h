// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FrameCues/Paper2DPlusFrameCue.h"

class UPaper2DPlusCharacterLayerAsset;
class UPaper2DPlusCharacterProfileAsset;

/**
 * Read-only diagnostics for the editor-only named-track sidecar.
 *
 * The analyzer deliberately accepts storage-neutral domains rather than a data provider. This keeps
 * validation useful for active/stashed Profile rows, Character baselines, and Layer authored rows
 * without manufacturing editor selection state or repairing an asset merely because it was opened.
 */
namespace Paper2DPlusFrameCueTrackLayoutDiagnostics
{
	enum class EIssueKind : uint8
	{
		InvalidTrackId,
		DuplicateTrackId,
		EmptyTrackName,
		NonCanonicalTrackName,
		DuplicateTrackName,
		NullMembershipCue,
		InvalidMembershipTrackId,
		UnknownMembershipTrackId,
		AssignmentOutsideDomain,
		CrossDomainMembership,
		DuplicateCueInDomain,
		CueInMultipleDomains,
		WrongCueOuter
	};

	struct FIssue
	{
		EIssueKind Kind = EIssueKind::AssignmentOutsideDomain;
		FName Code;
		FString DomainIdentity;
		FString ItemIdentity;
		FString Message;
	};

	/** One authoritative Cue-array domain plus its sidecar. Default membership is intentionally absent. */
	struct FDomain
	{
		FString Identity;
		const UObject* ExpectedCueOuter = nullptr;
		const FPaper2DPlusFrameCueTrackLayout* Layout = nullptr;
		TArray<const UPaper2DPlusCueBase*> ActiveCues;
		TArray<const UPaper2DPlusCueBase*> StashedCues;
	};

	FName GetIssueCode(EIssueKind Kind);

	/** Analyze immutable domains, including cross-domain references, without pruning or normalizing. */
	void AnalyzeDomains(const TArray<FDomain>& Domains, TArray<FIssue>& OutIssues);

	/** Gather active+excluded/stashed Profile rows and editor-only Character-baseline rows. */
	void AnalyzeCharacterProfile(
		const UPaper2DPlusCharacterProfileAsset& Asset,
		TArray<FIssue>& OutIssues);

	/** Gather only Layer AuthoredAnimations. CookedGameplayAnimations is never an authoring domain. */
	void AnalyzeCharacterLayer(
		const UPaper2DPlusCharacterLayerAsset& Asset,
		TArray<FIssue>& OutIssues);
}
