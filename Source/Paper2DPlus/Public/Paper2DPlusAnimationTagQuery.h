// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Paper2DPlusComboChain.h"

class UPaper2DPlusCharacterProfileAsset;
struct FFlipbookProfileEntry;

/**
 * Pure, worldless effective-animation-tag resolution over the flat profile data (TASK-108 U5). RUNTIME
 * (like `Paper2DPlusComboChain`) so the Blueprint tag queries work in cooked builds — editor surfaces
 * (chips, filter bars) consume the same batch so provenance can't drift from query results.
 *
 * EFFECTIVE TAGS of an animation = its OWN authored `EditorMeta.AnimationTags`
 *   ∪ the OWN tags of every valid numbered ROOT whose exact-group confirm path reaches it (cycle-safe;
 *     traversal cannot cross groups or pass through another numbered root; a shared in-scope finisher
 *     reachable from multiple roots unions ALL their tags)
 *   ∪ the `TagMappings` group KEY of every exact group in which it is an unambiguous real member
 *     (so "search by group" works with zero animation-tag authoring while invalid membership fails closed).
 *
 * Computed ON DEMAND, once per call, for the whole asset (the per-call-batching KTD) — NO persistent
 * cache, NO PostLoad materialization. A full batch is O(V + R*(V+E)) in the worst case because each
 * valid root has an independently bounded traversal; fine at current asset sizes. Callers that also
 * need root/group projection can pass one shared analysis batch to avoid deriving it twice.
 */
namespace Paper2DPlusAnimationTagQuery
{
	/** Per-animation tag resolution, provenance-separated so editor chips can style own (solid) vs
	 *  chain-inherited (ghosted) vs group-implied (outlined) without re-deriving anything. */
	struct FAnimationTagSet
	{
		/** Canonical authored animation name retained by the first-wins batch row. */
		FString AnimationName;

		/** Authored on the entry's own `EditorMeta.AnimationTags`. */
		FGameplayTagContainer OwnTags;

		/** The OWN tags of every exact-group numbered root that reaches this animation (the animation
		 *  itself excluded — a root never "inherits" from itself). Union across all scoped roots. */
		FGameplayTagContainer ChainInheritedTags;

		/** The `TagMappings` key of every exact group where this is an unambiguous real member. */
		FGameplayTagContainer GroupImpliedTags;

		/** Own ∪ chain-inherited ∪ group-implied — what `bIncludeInherited=true` queries match on. */
		FGameplayTagContainer EffectiveTags;

		/** Display names of the scoped roots that reach this animation (excludes the animation itself).
		 *  Drives the validator's root-vs-member Context-conflict check and chip provenance tooltips. A
		 *  profile entry used as a root in multiple groups appears once because its authored tags agree. */
		TArray<FString> ReachingRootNames;
	};

	/**
	 * THE batch build: one pass over the whole asset producing lowered-flipbook-name -> tag set. The
	 * shared `AnalyzeAnimationGroups` batch resolves every exact group and derives every numbered root
	 * chain once. Duplicate/invalid roots and ambiguous group membership fail closed. Duplicate-named
	 * profile entries contribute only their FIRST own-tag row but can never resolve as roots.
	 * Empty/whitespace names and PaperZD-only mapping entries (no `FFlipbookProfileEntry` to carry tags)
	 * are skipped. Null asset -> empty map.
	 */
	PAPER2DPLUS_API TMap<FString, FAnimationTagSet> BuildAnimationTagMap(const UPaper2DPlusCharacterProfileAsset* Asset);

	/** Build from a caller-owned complete group-analysis batch. The batch must come from
	 *  Paper2DPlusComboChain::AnalyzeAnimationGroups for the same Asset. */
	PAPER2DPLUS_API TMap<FString, FAnimationTagSet> BuildAnimationTagMap(
		const UPaper2DPlusCharacterProfileAsset* Asset,
		const TArray<Paper2DPlusComboChain::FScopedAnimationGroupAnalysis>& GroupAnalyses);

	/** Order-independent equality over the authored exact tags only. Parent/child hierarchy and
	 *  subset matching are deliberately not consulted. */
	PAPER2DPLUS_API bool AreExactTagSetsEqual(
		const FGameplayTagContainer& A,
		const FGameplayTagContainer& B);

	/** Collect every live profile entry whose OWN authored AnimationTags exactly equal QueryTags.
	 *  The result preserves profile order solely for diagnostics; callers must reject multiple
	 *  matches rather than using order as a tiebreaker. Null assets and empty queries return empty. */
	PAPER2DPLUS_API TArray<const FFlipbookProfileEntry*> FindExactOwnTagMatches(
		const UPaper2DPlusCharacterProfileAsset* Asset,
		const FGameplayTagContainer& QueryTags);

	/** True only when every supplied duplicate match is contained by exactly one valid numbered-root
	 *  chain. This is the sole exception to standalone exact-tag uniqueness validation. */
	PAPER2DPLUS_API bool AreMatchesContainedByOneRootChain(
		const UPaper2DPlusCharacterProfileAsset* Asset,
		const TArray<const FFlipbookProfileEntry*>& Matches);
}
