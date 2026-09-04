// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Paper2DPlusClashTypes.h"

struct FGameplayTag;

/**
 * The PURE, worldless clash core (TASK-77 U1) — the Paper2DPlusComboChain / FPaper2DPlusFrameData
 * precedent: free functions over plain data, NOT on the UWorldSubsystem (so every outcome is worldless-
 * testable and the verdict is deterministic + server-re-derivable per the authority contract).
 *
 * Resolution is HIERARCHY-AWARE and TOTAL by construction:
 *  - An edge (Winner, Loser) supports "X beats Y" iff X.MatchesTag(Winner) && Y.MatchesTag(Loser)
 *    (MatchesTag walks X/Y up to their parents, so a generic `Strike` edge applies to `Strike.Heavy`).
 *  - Specificity of a match = TagDepth(Winner) + TagDepth(Loser); the MOST-SPECIFIC supporting edge wins
 *    a direction (a `Strike.Heavy -> Throw` override beats a generic `Strike -> Throw`).
 *  - ResolveClash compares the best "A beats B" specificity vs the best "B beats A": strictly greater wins;
 *    EQUAL (a no-rule gap OR an authored conflict) falls to the project Default (Trade/Clash). This makes
 *    the verdict deterministic, permutation-invariant (max over a flat array), and ANTISYMMETRIC by
 *    construction (Resolve(A,B) and Resolve(B,A) read the same two best-specificities).
 *
 * The detect-and-warn ValidateClashGraph turns the residual ambiguity (equal-specificity edges pointing
 * BOTH ways for a pair — which silently resolves to Default, ignoring both) into an authoring Error, so the
 * flexible graph stays expressive while any SHIPPED graph is unambiguous (the F1 fix).
 *
 * SCOPE of the unambiguity guarantee (v1, "Beats"-only edges): the verdict is provably deterministic,
 * permutation-invariant and antisymmetric, and the validator flags every OPPOSITE-direction equal-
 * specificity conflict. It does NOT yet flag a SAME-direction max-tie (two distinct edges supporting the
 * same winner at equal best specificity) — harmless in v1 because both edges agree on the verdict, so which
 * one "wins" the max is irrelevant. WHEN a later change gives FClashEdge a richer outcome payload, a
 * same-direction max-tie with DIVERGENT payloads becomes order-dependent — extend the validator then to
 * flag those (have BestBeatSpecificity return the tied edge set and compare payloads).
 */
namespace Paper2DPlusClash
{
	/** Number of dot-separated components in a tag (Paper2DPlus.Clash.Category.Strike = 4; invalid = 0). */
	PAPER2DPLUS_API int32 TagDepth(const FGameplayTag& Tag);

	/** Best specificity of an edge supporting "X beats Y" (TagDepth(Winner)+TagDepth(Loser)); -1 if none. */
	PAPER2DPLUS_API int32 BestBeatSpecificity(const FGameplayTag& X, const FGameplayTag& Y, const FClashGraph& Graph);

	/** Resolve two overlapping categories. Untagged/empty (no edge match) -> the project Default. */
	PAPER2DPLUS_API EClashOutcome ResolveClash(const FGameplayTag& A, const FGameplayTag& B, const FClashGraph& Graph);

	/**
	 * The keep/suppress decision the broadphase makes for an attack box overlapping a DEFENDER's hurtbox
	 * (TASK-77 U3) — does the attack connect, or does the defender's DEFENSE class beat it?
	 *  - An untagged defender (no DefenseClass) ALWAYS connects (fast path) — this, plus the resolver
	 *    returning the project Default whenever either tag is invalid, makes the consult a provable no-op for
	 *    all existing/untagged content (the i-frame non-regression guarantee).
	 *  - Otherwise resolve(attacker, defenderDefense): the attack is SUPPRESSED only when the defense WINS
	 *    (BWins — Armor absorbs Strike, Parry beats Strike/Projectile, Invincible beats Strike) or the
	 *    unreachable-in-v1 Whiff. AWins (Throw breaks Armor), Trade and Clash all CONNECT (Clash mutual-negate
	 *    is an attack-vs-attack concept for U4, never the defender gate).
	 * Deterministic + authority-safe (pure over the graph), so the server re-derives the same verdict.
	 */
	PAPER2DPLUS_API bool AttackConnects(const FGameplayTag& AttackerCategory, const FGameplayTag& DefenderDefenseClass, const FClashGraph& Graph);

	/**
	 * TASK-77 U4: does THIS attacker's box land, given the attack-vs-attack outcome from ITS perspective
	 * (Outcome = ResolveClash(myCategory, otherCategory))? True for Trade (both land) and AWins (this side
	 * wins); false for BWins (this side loses), Clash (mutual negate) and Whiff (mutual ignore — unreachable
	 * in v1, kept for forward-compat). Because ResolveClash is antisymmetric, the two attackers' independent
	 * QueryAttackClashes queries are automatically mirror-consistent (A AWins ⇔ B BWins) with no shared cache.
	 */
	PAPER2DPLUS_API bool ClashOutcomeDealsDamage(EClashOutcome Outcome);

	/**
	 * Detect-and-warn validator (the F1 fix). Enumerates every UNORDERED concrete pair over ConcreteTags
	 * (pass the union of the graph's edge tags + any project-assigned hitbox categories) and flags:
	 *  - ERROR: both directions supported at EQUAL best-specificity -> ambiguous winner, silently resolves
	 *    to Default ignoring both edges. Names the pair so the editor can highlight it.
	 *  - WARNING: an exact self-loop edge (Winner == Loser) — a category can't meaningfully beat itself.
	 * Gaps (no rule either way) are NOT flagged: that is the normal "trade by default" majority. Returns
	 * true iff there are no Error-severity issues.
	 */
	PAPER2DPLUS_API bool ValidateClashGraph(const FClashGraph& Graph, const TArray<FGameplayTag>& ConcreteTags, TArray<FClashValidationIssue>& OutIssues);

	/** Convenience overload: validate over the concrete tags appearing in the graph's own edges. */
	PAPER2DPLUS_API bool ValidateClashGraph(const FClashGraph& Graph, TArray<FClashValidationIssue>& OutIssues);

	/** The genre-default RPS edge set seeded onto a freshly created clash-graph asset. */
	PAPER2DPLUS_API FClashGraph MakeDefaultClashGraph();
}
