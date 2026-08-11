// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Paper2DPlusMoveTransition.h"

class UPaper2DPlusCharacterProfileAsset;
enum class EAnimationPhase : uint8;

/**
 * Pure, worldless chain-start / phase analysis over the flat FFlipbookTransitionData. Runtime so
 * cooked game code and the Animation Map editor share one interpretation of authored arrows. A chain
 * start ("root") is identified only by an exact TagMappings group entry carrying the explicit
 * bIsChainStart flag; there is no profile-global root or structural-opener fallback (an opener with
 * an incoming counter edge stays the start ONLY because it is flagged).
 *
 * The "confirm path" is every move->move transition whose target is non-empty. TASK-108 U1 removed the
 * per-row Condition (transitions are pure From→To arrows), so the historical OnBlock/OnWhiff exclusion
 * is gone — every arrow is a chain link (accepted KTD; derived chains/roots/phases on assets that used
 * conditions can shift, and the load migration logs each dropped non-Always condition).
 */
namespace Paper2DPlusComboChain
{
	/** Stable native identity for one valid chain start in one exact animation group. */
	struct FScopedAnimationRoot
	{
		FGameplayTag GroupTag;
		FString RootMove;

		/** The chain's own authored identity container (the start entry's ChainTags; may be empty).
		 *  Chain lookup matches this with precedence over the opener animation's own tags. */
		FGameplayTagContainer ChainTags;
	};

	/** One unambiguous real profile member of an exact mapping, in authored mapping order. */
	struct FScopedGroupMember
	{
		FString MoveName;
	};

	/** One valid chain start plus its already-derived, group-bounded chain. */
	struct FScopedAnimationRootAnalysis
	{
		FScopedAnimationRoot Root;
		TArray<FString> ChainMoves;
	};

	/** One valid exact group, its usable authored-order members, and its chain starts in authored
	 *  entry order. */
	struct FScopedAnimationGroupAnalysis
	{
		FGameplayTag GroupTag;
		TArray<FScopedGroupMember> Members;
		TArray<FScopedAnimationRootAnalysis> Roots;
	};

	/** True when Transition is a chain edge: a non-empty target. (TASK-108: the Always-or-OnHit
	 *  condition filter died with the per-row Condition field — every arrow is a chain link.) */
	PAPER2DPLUS_API bool IsConfirmTransition(const FPaper2DPlusMoveTransition& Transition);

	/** Resolve the usable members of one exact group. Duplicate mapping membership, duplicate profile
	 *  names, dangling names, and entries without a real flipbook are omitted. False means the group is
	 *  missing, invalid, or contains no usable members. */
	PAPER2DPLUS_API bool GetAnimationGroupMembers(
		const UPaper2DPlusCharacterProfileAsset* Asset,
		const FGameplayTag& GroupTag,
		TArray<FScopedGroupMember>& OutMembers);

	/** Analyze one exact group once. False means the group is missing or has no usable members. */
	PAPER2DPLUS_API bool AnalyzeAnimationGroup(
		const UPaper2DPlusCharacterProfileAsset* Asset,
		const FGameplayTag& GroupTag,
		FScopedAnimationGroupAnalysis& OutAnalysis);

	/** Resolve usable membership and valid chain starts without traversing transition chains. This is
	 *  the lightweight discovery path used by Blueprint Group/Root enumeration and board badges. */
	PAPER2DPLUS_API bool AnalyzeAnimationGroupTopology(
		const UPaper2DPlusCharacterProfileAsset* Asset,
		const FGameplayTag& GroupTag,
		FScopedAnimationGroupAnalysis& OutAnalysis);

	/** Resolve one exact chain start (by opener move name, case-insensitive) and derive only that
	 *  chain. OutAnalysis contains the group's usable members plus zero or one root; false means the
	 *  selected entry is missing, unflagged, dangling, or ambiguous. */
	PAPER2DPLUS_API bool AnalyzeAnimationRoot(
		const UPaper2DPlusCharacterProfileAsset* Asset,
		const FGameplayTag& GroupTag,
		const FString& RootMoveName,
		FScopedAnimationGroupAnalysis& OutAnalysis);

	/** Analyze all usable groups in deterministic tag order, reusing one profile-name index. */
	PAPER2DPLUS_API TArray<FScopedAnimationGroupAnalysis> AnalyzeAnimationGroups(
		const UPaper2DPlusCharacterProfileAsset* Asset);

	/** Analyze every usable group's membership and root identities without deriving chains. */
	PAPER2DPLUS_API TArray<FScopedAnimationGroupAnalysis> AnalyzeAnimationGroupTopologies(
		const UPaper2DPlusCharacterProfileAsset* Asset);

	/**
	 * Enumerate every valid chain start without flattening away its group identity. Results are
	 * sorted by exact group-tag string and then authored entry order. A chain start is omitted when
	 * its mapping membership is duplicated or its mapping name does not resolve to exactly one real
	 * flipbook profile entry.
	 */
	PAPER2DPLUS_API TArray<FScopedAnimationRoot> GetAnimationRoots(const UPaper2DPlusCharacterProfileAsset* Asset);

	/** Resolve exactly one flagged chain start (by opener move name, case-insensitive) inside one
	 *  exact group. Unflagged entries and invalid membership fail closed. */
	PAPER2DPLUS_API bool FindAnimationRoot(
		const UPaper2DPlusCharacterProfileAsset* Asset,
		const FGameplayTag& GroupTag,
		const FString& RootMoveName,
		FScopedAnimationRoot& OutRoot);

	/**
	 * Ordered chain for one exact (GroupTag, chain-start move), using deterministic first-visit
	 * pre-order DFS in authored transition-row order. Traversal can visit only valid members of that
	 * exact group and stops before every OTHER flagged chain start, so one chain can never absorb
	 * another or cross a group boundary. Cycles and shared sinks terminate at first visit. Returns
	 * authored display names with the start first, or empty when the group/start is missing,
	 * dangling, duplicated, or otherwise ambiguous. A valid start with no in-scope transitions
	 * returns only itself.
	 */
	PAPER2DPLUS_API TArray<FString> DeriveComboChain(
		const UPaper2DPlusCharacterProfileAsset* Asset,
		const FGameplayTag& GroupTag,
		const FString& RootMoveName);

	/** Phase from a position in an ordered chain: index 0 => Startup, last => Recovery, middle =>
	 *  Active. Chains shorter than 2 (a lone clip) => None for every position. Pure — no tag registry. */
	PAPER2DPLUS_API EAnimationPhase PhaseForChainPosition(int32 IndexInChain, int32 ChainLength);

	/**
	 * The MAIN combo line ("spine") for one exact (GroupTag, chain-start move): the definitive
	 * countable line from the flagged opener, restricted to valid non-start members of that exact
	 * group (another chain start or a group boundary is a hard stop; a cycle cannot revisit).
	 * Selection rule: a simple confirm path TERMINATING AT a flagged Chain End always beats any
	 * longer path that does not, and traversal never walks past an end — so trailing
	 * recovery/settle animations wired after the end are excluded from combo indexing and length.
	 * Among candidates of equal end-status, the LONGEST wins; remaining ties go to the EARLIER
	 * authored transition row — fully deterministic. No end flagged = plain longest. This is
	 * deliberately different from DeriveComboChain, whose DFS flatten is the REACHABLE SET in visit
	 * order (right for phase derivation, wrong for "combo step N"): a shared finisher with an
	 * authored shortcut edge still spines through the long route here.
	 *
	 * Returns authored display names with the start first; empty when the group/start is missing,
	 * dangling, duplicated, or otherwise ambiguous. A valid start with no in-scope transitions
	 * returns only itself. The backtracking search is expansion-capped (pathologically dense authored
	 * graphs degrade deterministically to the best line found before the cap).
	 */
	PAPER2DPLUS_API TArray<FString> DeriveComboSpine(
		const UPaper2DPlusCharacterProfileAsset* Asset,
		const FGameplayTag& GroupTag,
		const FString& RootMoveName);

	/** Result of locating the one rooted chain containing a move. */
	enum class EComboSpineFindResult : uint8
	{
		Found,
		NotChained,
		AmbiguousChain
	};

	/**
	 * Locate the unique valid chain start whose reachable chain contains MoveName (the move itself
	 * when it IS that root), then derive that root's spine. NotChained = no valid root anywhere on the
	 * profile reaches the move; AmbiguousChain = more than one does (fail closed — never a hidden
	 * first-wins pick). On Found, OutSpine may not contain MoveName: the move is then reachable from
	 * the root only via a side branch off the main line.
	 */
	PAPER2DPLUS_API EComboSpineFindResult FindComboSpineForMove(
		const UPaper2DPlusCharacterProfileAsset* Asset,
		const FString& MoveName,
		FScopedAnimationRoot& OutRoot,
		TArray<FString>& OutSpine);

}
