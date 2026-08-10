// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * The one authoring-progress asset-registry tag contract.
 *
 * Character Profile, Character Layer, Effect Profile, and Combat Profile each export a
 * done/total pair describing how much of their MANUAL editor completion checklist a designer has
 * ticked. The Character Catalog roster reads these tags straight from FAssetData so it can show real
 * progress for characters whose assets are not loaded.
 *
 * Both tags are hidden (TT_Hidden) editor-generated metadata: nothing in a cooked build reads them.
 * Because the checklist bits are manual ticks with no content-derived fallback, a Total of 0 or an
 * absent pair means "no progress data", never "complete" — see IsAuthoringProgressPairValid.
 */
namespace Paper2DPlusAuthoringProgress
{
	/** Ticked checklist criteria across the whole asset. Stringified int32. */
	inline const FName& DoneTag()
	{
		static const FName Tag(TEXT("Paper2DPlus.CompletionDone"));
		return Tag;
	}

	/** Total checklist criteria the asset offers. Stringified int32; 0 means nothing to author. */
	inline const FName& TotalTag()
	{
		static const FName Tag(TEXT("Paper2DPlus.CompletionTotal"));
		return Tag;
	}

	/**
	 * FAssetData::GetTagValue reports success for a tag that merely EXISTS and silently Atoi's a
	 * malformed value to 0, so presence is not validity. A pair only counts when the total is
	 * positive and the done count sits inside it.
	 */
	inline bool IsAuthoringProgressPairValid(int32 Done, int32 Total)
	{
		return Total > 0 && Done >= 0 && Done <= Total;
	}

	/**
	 * Criteria count of the shared Profile Completion checklist used by Character Layer, Effect
	 * Profile, and Combat Profile. The editor panel's own mask derives from these constants, so a
	 * criteria change cannot leave the exported Total disagreeing with the checkbox list.
	 */
	static constexpr int32 ProfileChecklistCriteriaCount = 5;
	static constexpr int32 ProfileChecklistCriteriaMask = (1 << ProfileChecklistCriteriaCount) - 1;

	/** Done/total for one asset carrying a single EditorCompletionFlags checklist. */
	inline void ComputeChecklistProgress(int32 CompletionFlags, int32& OutDone, int32& OutTotal)
	{
		OutDone = FMath::CountBits(
			static_cast<uint32>(CompletionFlags & ProfileChecklistCriteriaMask));
		OutTotal = ProfileChecklistCriteriaCount;
	}
}
