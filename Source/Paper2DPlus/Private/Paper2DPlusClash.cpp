// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "Paper2DPlusClash.h"
#include "Paper2DPlusHitboxTags.h"
#include "GameplayTagContainer.h"

namespace Paper2DPlusClash
{
	int32 TagDepth(const FGameplayTag& Tag)
	{
		if (!Tag.IsValid())
		{
			return 0;
		}
		// Component count = dots + 1. Deterministic; no dependence on the tag registry's iteration order.
		const FString Str = Tag.ToString();
		int32 Depth = 1;
		for (const TCHAR Ch : Str)
		{
			if (Ch == TEXT('.'))
			{
				++Depth;
			}
		}
		return Depth;
	}

	int32 BestBeatSpecificity(const FGameplayTag& X, const FGameplayTag& Y, const FClashGraph& Graph)
	{
		// "X beats Y" is supported by an edge iff X is the Winner (or a descendant) AND Y is the Loser (or a
		// descendant). MatchesTag(Parent) is true when the receiver IS or DESCENDS FROM Parent, so a generic
		// `Strike` Winner edge supports a `Strike.Heavy` attacker. Iterate the FLAT array (no TMap/TSet order
		// dependence) and keep the MAX specificity — equal-specificity same-direction edges agree, so max is
		// permutation-invariant.
		int32 Best = -1;
		if (!X.IsValid() || !Y.IsValid())
		{
			return Best; // an untagged box supports no "beats" rule -> falls to the default in ResolveClash.
		}
		for (const FClashEdge& Edge : Graph.Edges)
		{
			if (Edge.Winner.IsValid() && Edge.Loser.IsValid()
				&& X.MatchesTag(Edge.Winner) && Y.MatchesTag(Edge.Loser))
			{
				Best = FMath::Max(Best, TagDepth(Edge.Winner) + TagDepth(Edge.Loser));
			}
		}
		return Best;
	}

	EClashOutcome ResolveClash(const FGameplayTag& A, const FGameplayTag& B, const FClashGraph& Graph)
	{
		const int32 ABeatsB = BestBeatSpecificity(A, B, Graph);
		const int32 BBeatsA = BestBeatSpecificity(B, A, Graph);

		if (ABeatsB > BBeatsA)
		{
			return EClashOutcome::AWins;
		}
		if (BBeatsA > ABeatsB)
		{
			return EClashOutcome::BWins;
		}
		// Equal — either a no-rule gap (-1/-1) or an authored conflict (equal-specificity both ways). Either
		// way fall to the project default: deterministic, symmetric, and the conflict is surfaced as an
		// authoring Error by ValidateClashGraph rather than silently picking a winner by iteration order.
		return (Graph.Default == EClashSameCategoryDefault::Clash) ? EClashOutcome::Clash : EClashOutcome::Trade;
	}

	bool AttackConnects(const FGameplayTag& AttackerCategory, const FGameplayTag& DefenderDefenseClass, const FClashGraph& Graph)
	{
		// Fast path + inherent non-regression gate: a defender with no DefenseClass always takes the hit (this
		// is EVERY existing frame). For a tagged defender, suppress ONLY when the defense wins the clash —
		// BWins (the defense out-prioritizes the attacker) or the v1-unreachable Whiff. BWins additionally
		// requires the attacker to be tagged AND an authored "defense beats attacker" edge, so an untagged
		// attacker vs a defended frame still connects (resolves to the project Default → not BWins).
		if (!DefenderDefenseClass.IsValid())
		{
			return true;
		}
		const EClashOutcome Outcome = ResolveClash(AttackerCategory, DefenderDefenseClass, Graph);
		return Outcome != EClashOutcome::BWins && Outcome != EClashOutcome::Whiff;
	}

	bool ClashOutcomeDealsDamage(EClashOutcome Outcome)
	{
		// This attacker's box lands iff it wins (AWins) or both land (Trade). It does NOT land on a loss
		// (BWins), a mutual negate (Clash), or a mutual ignore (Whiff — v1-unreachable, kept for forward-compat).
		return Outcome == EClashOutcome::Trade || Outcome == EClashOutcome::AWins;
	}

	bool ValidateClashGraph(const FClashGraph& Graph, const TArray<FGameplayTag>& ConcreteTags, TArray<FClashValidationIssue>& OutIssues)
	{
		// Reset the output before appending (Codex P2) — like the profile/effect validators. A caller that
		// reuses the array across runs (e.g. re-validating after a fix) must not see stale issues alongside a
		// true return. This is the single append site (the single-arg overload + the asset forward here).
		OutIssues.Reset();

		bool bHasError = false;

		const FGameplayTag Root = Paper2DPlusHitboxTags::Category;
		for (const FClashEdge& Edge : Graph.Edges)
		{
			// WARNING: exact self-loop edges (a category can't meaningfully beat itself).
			if (Edge.Winner.IsValid() && Edge.Winner == Edge.Loser)
			{
				FClashValidationIssue Issue;
				Issue.Severity = EClashValidationSeverity::Warning;
				Issue.CategoryA = Edge.Winner;
				Issue.CategoryB = Edge.Loser;
				Issue.Message = FString::Printf(TEXT("Self-loop edge: '%s' beats itself — this rule is inert. Remove it."), *Edge.Winner.ToString());
				OutIssues.Add(Issue);
			}

			// WARNING: the bare ROOT tag as an endpoint is a match-everything wildcard at a lower specificity
			// (depth 3) than any genre edge (depth 4) — it silently undercuts the whole precedence scale, and
			// the depth metric is only meaningful for tags UNDER the root. Flag a root/off-root endpoint (F2).
			auto WarnOffRoot = [&](const FGameplayTag& Tag)
			{
				if (Tag.IsValid() && (Tag == Root || !Tag.MatchesTag(Root)))
				{
					FClashValidationIssue Issue;
					Issue.Severity = EClashValidationSeverity::Warning;
					Issue.CategoryA = Tag;
					Issue.Message = FString::Printf(
						TEXT("Edge endpoint '%s' is the bare root (match-everything wildcard) or not under '%s' — use a specific category; precedence is only well-defined for tags under the root."),
						*Tag.ToString(), *Root.ToString());
					OutIssues.Add(Issue);
				}
			};
			WarnOffRoot(Edge.Winner);
			WarnOffRoot(Edge.Loser);
		}

		// ERROR: equal-specificity edges in BOTH directions for a concrete pair -> ambiguous winner. Resolves
		// to the default, silently ignoring both authored edges. Enumerate UNORDERED pairs (i < j) over a
		// DEDUPED tag set (a caller — e.g. U2 merging hitbox categories — may pass duplicates; dedupe so a
		// pair isn't flagged twice).
		TArray<FGameplayTag> Tags;
		Tags.Reserve(ConcreteTags.Num());
		for (const FGameplayTag& T : ConcreteTags)
		{
			if (T.IsValid())
			{
				Tags.AddUnique(T);
			}
		}
		for (int32 i = 0; i < Tags.Num(); ++i)
		{
			for (int32 j = i + 1; j < Tags.Num(); ++j)
			{
				const FGameplayTag& A = Tags[i];
				const FGameplayTag& B = Tags[j];
				const int32 ABeatsB = BestBeatSpecificity(A, B, Graph);
				const int32 BBeatsA = BestBeatSpecificity(B, A, Graph);
				if (ABeatsB >= 0 && BBeatsA >= 0 && ABeatsB == BBeatsA)
				{
					FClashValidationIssue Issue;
					Issue.Severity = EClashValidationSeverity::Error;
					Issue.CategoryA = A;
					Issue.CategoryB = B;
					Issue.Message = FString::Printf(
						TEXT("Ambiguous clash: an edge says '%s' beats '%s' and an edge says '%s' beats '%s' at equal specificity (%d). It resolves to the default, ignoring both. Author a more-specific override on one direction."),
						*A.ToString(), *B.ToString(), *B.ToString(), *A.ToString(), ABeatsB);
					OutIssues.Add(Issue);
					bHasError = true;
				}
			}
		}

		return !bHasError;
	}

	bool ValidateClashGraph(const FClashGraph& Graph, TArray<FClashValidationIssue>& OutIssues)
	{
		// Concrete tags = the (deduped) tags appearing on the graph's own edges, in first-seen order
		// (deterministic). U2 augments this with the project's hitbox-assigned categories via the other overload.
		TArray<FGameplayTag> ConcreteTags;
		for (const FClashEdge& Edge : Graph.Edges)
		{
			if (Edge.Winner.IsValid())
			{
				ConcreteTags.AddUnique(Edge.Winner);
			}
			if (Edge.Loser.IsValid())
			{
				ConcreteTags.AddUnique(Edge.Loser);
			}
		}
		return ValidateClashGraph(Graph, ConcreteTags, OutIssues);
	}

	FClashGraph MakeDefaultClashGraph()
	{
		using namespace Paper2DPlusHitboxTags;
		auto Edge = [](const FGameplayTag& W, const FGameplayTag& L)
		{
			FClashEdge E;
			E.Winner = W;
			E.Loser = L;
			return E;
		};

		FClashGraph Graph;
		Graph.Default = EClashSameCategoryDefault::Trade;
		// The seeded genre-default edges (designers override per-project). A sparse directed graph of
		// exceptions over "same category trades"; deliberately CONFLICT-FREE under the equal-specificity rule
		// (a DAG with no 2-cycle, so no pair has equal-specificity edges both ways). Aligns with the U3
		// defensive-property scope: the defender's Armor/Parry/Invincible beats/absorbs the attacker's
		// Strike/Projectile, and a Throw beats Armor/Parry. Strike-vs-Throw is PHASE-gated (startup) and is
		// left to U4 rather than seeded as an unconditional edge here.
		Graph.Edges = {
			Edge(Category_Throw,      Category_Armor),       // a throw beats armor
			Edge(Category_Throw,      Category_Parry),       // a throw beats a parry
			Edge(Category_Armor,      Category_Strike),      // armor absorbs strikes
			Edge(Category_Armor,      Category_Projectile),  // armor absorbs projectiles
			Edge(Category_Parry,      Category_Strike),      // a parry beats strikes
			Edge(Category_Parry,      Category_Projectile),  // a parry beats projectiles
			Edge(Category_Invincible, Category_Strike),      // an invincible reversal beats strikes
			Edge(Category_Invincible, Category_Projectile),  // ...and projectiles (parity with Armor/Parry)
			Edge(Category_Strike_Heavy, Category_Strike_Light), // a heavy strike beats a light strike
		};
		return Graph;
	}
}
