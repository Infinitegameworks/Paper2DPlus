// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Paper2DPlusAnimationMapLibrary.generated.h"

class UPaper2DPlusCharacterProfileAsset;
class UPaperFlipbook;

/** Optional deterministic branch filter for transitions that share a phase. Tags are matched
 *  against each target's EFFECTIVE animation tags (own authored tags, tags inherited from every
 *  chain start whose chain reaches the target, and the tag of every exact group it belongs to). */
USTRUCT(BlueprintType)
struct PAPER2DPLUS_API FPaper2DPlusAnimationSelectionCriteria
{
	GENERATED_BODY()

	/** Every tag must match the target's effective tags. Empty imposes no ALL requirement. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation Map")
	FGameplayTagContainer RequiredAllTags;

	/** At least one tag must match when non-empty. Empty imposes no ANY requirement. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation Map")
	FGameplayTagContainer RequiredAnyTags;

	/** A target matching any excluded tag is rejected. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation Map")
	FGameplayTagContainer ExcludedTags;
};

/** Useful facts about the valid direct transitions authored on one flipbook. */
USTRUCT(BlueprintType)
struct PAPER2DPLUS_API FPaper2DPlusAnimationTransitionInfo
{
	GENERATED_BODY()

	/** True only when at least one direct target resolves on the profile and matches Criteria. */
	UPROPERTY(BlueprintReadOnly, Category = "Animation Map")
	bool bHasTransitions = false;

	/** True when at least one valid direct transition has an effective phase. */
	UPROPERTY(BlueprintReadOnly, Category = "Animation Map")
	bool bHasPhases = false;

	/** Effective transition phases, deduplicated in transition-row order. A row override wins;
	 *  an unset row inherits its target animation's PhaseTag for compatibility. */
	UPROPERTY(BlueprintReadOnly, Category = "Animation Map")
	TArray<FGameplayTag> AvailablePhases;
};

/** Deterministic result from resolving one phase-selected transition. */
UENUM(BlueprintType)
enum class EPaper2DPlusAnimationResolveResult : uint8
{
	Success UMETA(DisplayName = "Success"),
	InvalidRequest UMETA(DisplayName = "Invalid Request"),
	FlipbookNotInMap UMETA(DisplayName = "Flipbook Not In Map"),
	AmbiguousFlipbook UMETA(DisplayName = "Ambiguous Flipbook"),
	NoMatch UMETA(DisplayName = "No Match"),
	Ambiguous UMETA(DisplayName = "Ambiguous")
};

/** Deterministic result from finding one flipbook by its complete authored AnimationTags set. */
UENUM(BlueprintType)
enum class EPaper2DPlusAnimationTagFindResult : uint8
{
	Success UMETA(DisplayName = "Success"),
	InvalidRequest UMETA(DisplayName = "Invalid Request"),
	NoMatch UMETA(DisplayName = "No Match"),
	Ambiguous UMETA(DisplayName = "Ambiguous")
};

/** Deterministic result from resolving one combo-chain step by index. */
UENUM(BlueprintType)
enum class EPaper2DPlusComboChainResult : uint8
{
	Success UMETA(DisplayName = "Success"),
	InvalidRequest UMETA(DisplayName = "Invalid Request"),
	/** The flipbook has no map entry / no animation's own tags equal the query. */
	NotFound UMETA(DisplayName = "Not Found"),
	/** The flipbook is shared by multiple entries / multiple animations carry the exact tags. */
	AmbiguousInput UMETA(DisplayName = "Ambiguous Input"),
	/** The identified animation is not a flagged Chain Start — only openers key a chain. */
	NotChainStart UMETA(DisplayName = "Not Chain Start"),
	/** The opener is flagged in more than one exact group — no unique chain. */
	AmbiguousChain UMETA(DisplayName = "Ambiguous Chain"),
	/** The chain resolved but Index walked past its end — the "combo finished" signal.
	 *  OutChainLength stays valid so counter logic can wrap or reset. */
	IndexOutOfRange UMETA(DisplayName = "Index Out Of Range"),
	/** The indexed step's flipbook failed to load; outputs are cleared. */
	LoadFailed UMETA(DisplayName = "Load Failed")
};

/**
 * The focused Blueprint front door for Character Profile animation maps: exact own tags for ordinary
 * single-animation lookup, flipbook-keyed transition inspection/resolution (a flipbook is unique in
 * its map, so it is the sole key — no Group or Root required), and one-step-by-index combo-chain
 * resolution plus direct chain-length queries (the chain's flagged opener — or its authored chain
 * container — plus a game-owned counter in, one flipbook or the countable length out). Everything is
 * keyed by Profile + flipbook or tags — there are no Group/Root reference structs.
 */
UCLASS()
class PAPER2DPLUS_API UPaper2DPlusAnimationMapLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Find one flipbook whose OWN authored AnimationTags exactly equal ExactTags. Tag order is
	 * irrelevant, but parent/child hierarchy and subsets are not matches. Chain starts are not
	 * required and do not exclude an otherwise unique result. Multiple matches fail explicitly;
	 * output is always cleared on failure.
	 */
	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Animation Map",
		meta = (DisplayName = "Find Animation by Exact Tags", AutoCreateRefTerm = "ExactTags"))
	static EPaper2DPlusAnimationTagFindResult FindAnimationByExactTags(
		UPaper2DPlusCharacterProfileAsset* Profile,
		UPARAM(meta = (Categories = "Paper2DPlus.Animation")) const FGameplayTagContainer& ExactTags,
		UPaperFlipbook*& OutFlipbook);

	/** Dynamic discovery: every authored Chain Start's opener flipbook, in group-tag-sorted then
	 *  authored entry order. Unloadable openers are skipped. Feed a result into
	 *  Get Combo Chain Flipbook at Index to walk its steps. */
	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Animation Map|Combo")
	static TArray<UPaperFlipbook*> GetComboOpenerFlipbooks(UPaper2DPlusCharacterProfileAsset* Profile);

	/**
	 * Inspect the valid direct transitions authored on Flipbook. The flipbook alone is the key: it
	 * must resolve to exactly one animation-map entry on Profile (a flipbook referenced by multiple
	 * entries fails closed). No Group or Root is required, and every authored From -> To row is
	 * visible regardless of group membership or chain-start boundaries.
	 */
	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Animation Map",
		meta = (AutoCreateRefTerm = "Criteria"))
	static bool GetAnimationTransitionInfo(
		UPaper2DPlusCharacterProfileAsset* Profile,
		UPaperFlipbook* Flipbook,
		const FPaper2DPlusAnimationSelectionCriteria& Criteria,
		FPaper2DPlusAnimationTransitionInfo& OutInfo);

	/**
	 * Resolve one direct transition authored on Flipbook by its effective per-row phase and optional
	 * branch criteria. A row override wins; an unset row inherits its target animation's PhaseTag.
	 * The flipbook alone is the key (see GetAnimationTransitionInfo). Zero matches and ambiguity are
	 * explicit failures; authored row order is never used as a tiebreaker.
	 */
	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Animation Map",
		meta = (AutoCreateRefTerm = "Criteria"))
	static EPaper2DPlusAnimationResolveResult ResolveAnimationTransition(
		UPaper2DPlusCharacterProfileAsset* Profile,
		UPaperFlipbook* Flipbook,
		UPARAM(meta = (GameplayTagFilter = "Paper2DPlus.Phase")) FGameplayTag RequestedPhase,
		const FPaper2DPlusAnimationSelectionCriteria& Criteria,
		UPaperFlipbook*& OutFlipbook);

	/**
	 * ONE combo step by index: the chain is keyed by its flagged OPENER flipbook (index 0 = the
	 * opener itself), Index walks the auto-derived MAIN LINE (the longest authored continuation,
	 * authored-row-order tiebreak — deterministic, no manual numbering), and exactly one flipbook
	 * comes out. Built for game-owned combo counters: hold the opener as the reference, feed the
	 * counter in as Index, play the result. Index Out Of Range is the "combo finished" signal —
	 * OutChainLength stays valid there (and on Success) so counter logic can reset or wrap; every
	 * other failure clears both outputs. A non-opener flipbook fails as Not Chain Start; an opener
	 * flagged in two exact groups fails as Ambiguous Chain.
	 */
	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Animation Map|Combo",
		meta = (DisplayName = "Get Combo Chain Flipbook at Index"))
	static EPaper2DPlusComboChainResult GetComboChainFlipbookAtIndex(
		UPaper2DPlusCharacterProfileAsset* Profile,
		UPaperFlipbook* ChainStartFlipbook,
		int32 Index,
		UPaperFlipbook*& OutFlipbook,
		int32& OutChainLength);

	/**
	 * As Get Combo Chain Flipbook at Index, but the chain is found by tags: ExactTags first matches
	 * a Chain Start's own authored CHAIN TAGS container (the chain's identity — exact equality,
	 * order-independent), and only when no chain container matches does it fall back to an opener
	 * animation's own AnimationTags (the FindAnimationByExactTags semantics). Put in e.g.
	 * {Combat.Attacking} + a combo counter, get that step's flipbook out.
	 */
	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Animation Map|Combo",
		meta = (DisplayName = "Get Combo Chain Flipbook at Index by Exact Tags", AutoCreateRefTerm = "ExactTags"))
	static EPaper2DPlusComboChainResult GetComboChainFlipbookAtIndexByTags(
		UPaper2DPlusCharacterProfileAsset* Profile,
		UPARAM(meta = (Categories = "Paper2DPlus.Animation")) const FGameplayTagContainer& ExactTags,
		int32 Index,
		UPaperFlipbook*& OutFlipbook,
		int32& OutChainLength);

	/**
	 * The countable length of the chain keyed by its flagged OPENER flipbook — the definitive
	 * start → Chain End line (trailing recovery wired after the end never counts; no end flagged =
	 * the longest authored continuation). The direct "how long is this combo" query for game-owned
	 * combo counters. Failure clears the output.
	 */
	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Animation Map|Combo",
		meta = (DisplayName = "Get Combo Chain Length"))
	static EPaper2DPlusComboChainResult GetComboChainLength(
		UPaper2DPlusCharacterProfileAsset* Profile,
		UPaperFlipbook* ChainStartFlipbook,
		int32& OutChainLength);

	/**
	 * As Get Combo Chain Length, but the chain is found by tags (chain-container match first, opener
	 * own-tags fallback — the Get Combo Chain Flipbook at Index by Exact Tags semantics). Put in the
	 * chain's container, get its countable length out.
	 */
	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Animation Map|Combo",
		meta = (DisplayName = "Get Combo Chain Length by Exact Tags", AutoCreateRefTerm = "ExactTags"))
	static EPaper2DPlusComboChainResult GetComboChainLengthByTags(
		UPaper2DPlusCharacterProfileAsset* Profile,
		UPARAM(meta = (Categories = "Paper2DPlus.Animation")) const FGameplayTagContainer& ExactTags,
		int32& OutChainLength);

	/**
	 * The cheap "does this attack open a combo?" branch: true ONLY when Flipbook is a flagged Chain
	 * Start whose countable main line has 2+ steps. Single animations, unflagged moves, mid-chain
	 * members (the OPENER keys a chain everywhere in this API), one-move chains, and every failure
	 * mode return false — fail closed, no result enum to unpack.
	 */
	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Animation Map|Combo",
		meta = (DisplayName = "Has Combo Chain"))
	static bool HasComboChain(
		UPaper2DPlusCharacterProfileAsset* Profile,
		UPaperFlipbook* ChainStartFlipbook);
};
