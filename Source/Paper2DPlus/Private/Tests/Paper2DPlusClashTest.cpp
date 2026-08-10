// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#if WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "Paper2DPlusClash.h"
#include "Paper2DPlusClashTypes.h"
#include "Paper2DPlusHitboxTags.h"
#include "Paper2DPlusTypes.h"
#include "GameplayTagContainer.h"
#include "Algo/Reverse.h"

// TASK-77 U1 — worldless tests for the pure clash resolver + the detect-and-warn conflict validator.
// File-unique helper prefix `Clash_` (the unity-build file-unique-name rule).

namespace
{
	FClashEdge Clash_Edge(const FGameplayTag& Winner, const FGameplayTag& Loser)
	{
		FClashEdge E;
		E.Winner = Winner;
		E.Loser = Loser;
		return E;
	}

	// A's mirror of an outcome: AWins<->BWins, the symmetric outcomes map to themselves.
	EClashOutcome Clash_Mirror(EClashOutcome O)
	{
		if (O == EClashOutcome::AWins) return EClashOutcome::BWins;
		if (O == EClashOutcome::BWins) return EClashOutcome::AWins;
		return O; // Trade / Clash / Whiff are symmetric
	}
}

// --- Outcomes: AWins / BWins / Trade-default / Clash-default ---------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusClashOutcomes, "Paper2DPlus.Clash.Outcomes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPaper2DPlusClashOutcomes::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusHitboxTags;
	FClashGraph Graph;
	Graph.Default = EClashSameCategoryDefault::Trade;
	Graph.Edges = { Clash_Edge(Category_Armor, Category_Strike) }; // armor beats strike

	TestEqual(TEXT("Armor beats Strike -> A wins"),
		Paper2DPlusClash::ResolveClash(Category_Armor, Category_Strike, Graph), EClashOutcome::AWins);
	TestEqual(TEXT("Strike loses to Armor -> B wins"),
		Paper2DPlusClash::ResolveClash(Category_Strike, Category_Armor, Graph), EClashOutcome::BWins);
	TestEqual(TEXT("No rule -> Trade default"),
		Paper2DPlusClash::ResolveClash(Category_Strike, Category_Throw, Graph), EClashOutcome::Trade);

	Graph.Default = EClashSameCategoryDefault::Clash;
	TestEqual(TEXT("No rule -> Clash default when project clashes"),
		Paper2DPlusClash::ResolveClash(Category_Strike, Category_Throw, Graph), EClashOutcome::Clash);

	return true;
}

// --- Untagged box -> default (never crashes, never beats) ------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusClashUntagged, "Paper2DPlus.Clash.Untagged",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPaper2DPlusClashUntagged::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusHitboxTags;
	FClashGraph Graph = Paper2DPlusClash::MakeDefaultClashGraph();
	const FGameplayTag Empty;
	TestEqual(TEXT("Untagged vs tagged -> default (Trade)"),
		Paper2DPlusClash::ResolveClash(Empty, Category_Strike, Graph), EClashOutcome::Trade);
	TestEqual(TEXT("Tagged vs untagged -> default (Trade)"),
		Paper2DPlusClash::ResolveClash(Category_Armor, Empty, Graph), EClashOutcome::Trade);
	TestEqual(TEXT("Untagged vs untagged -> default (Trade)"),
		Paper2DPlusClash::ResolveClash(Empty, Empty, Graph), EClashOutcome::Trade);
	return true;
}

// --- Hierarchy: a specific sub-tag override beats the generic parent rule --------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusClashHierarchy, "Paper2DPlus.Clash.HierarchyOverride",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPaper2DPlusClashHierarchy::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusHitboxTags;
	FClashGraph Graph;
	Graph.Default = EClashSameCategoryDefault::Trade;
	Graph.Edges = {
		Clash_Edge(Category_Parry, Category_Strike),       // generic: parry beats strike
		Clash_Edge(Category_Strike_Heavy, Category_Parry), // specific: a HEAVY strike beats a parry
	};

	// The generic strike still loses to parry.
	TestEqual(TEXT("Generic Strike loses to Parry"),
		Paper2DPlusClash::ResolveClash(Category_Strike, Category_Parry, Graph), EClashOutcome::BWins);
	// The specific heavy strike OVERRIDES the generic rule and beats the parry (depth 9 > 8).
	TestEqual(TEXT("Strike.Heavy overrides and beats Parry"),
		Paper2DPlusClash::ResolveClash(Category_Strike_Heavy, Category_Parry, Graph), EClashOutcome::AWins);
	// ...and the mirror.
	TestEqual(TEXT("Parry loses to Strike.Heavy"),
		Paper2DPlusClash::ResolveClash(Category_Parry, Category_Strike_Heavy, Graph), EClashOutcome::BWins);
	return true;
}

// --- Determinism: permutation-invariance + antisymmetry over the seeded graph ---------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusClashDeterminism, "Paper2DPlus.Clash.Determinism",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPaper2DPlusClashDeterminism::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusHitboxTags;
	FClashGraph Graph = Paper2DPlusClash::MakeDefaultClashGraph();

	const TArray<FGameplayTag> Tags = {
		Category_Strike, Category_Throw, Category_Projectile, Category_Armor,
		Category_Invincible, Category_Parry, Category_Strike_Heavy, Category_Strike_Light,
		Category_Projectile_Arrow, Category_Projectile_Magic
	};

	// Reversed-edge graph must produce identical verdicts (max over a flat array is order-independent).
	FClashGraph Reversed = Graph;
	Algo::Reverse(Reversed.Edges);

	for (const FGameplayTag& A : Tags)
	{
		for (const FGameplayTag& B : Tags)
		{
			const EClashOutcome O1 = Paper2DPlusClash::ResolveClash(A, B, Graph);
			const EClashOutcome O2 = Paper2DPlusClash::ResolveClash(A, B, Reversed);
			TestEqual(FString::Printf(TEXT("Permutation-invariant: %s vs %s"), *A.ToString(), *B.ToString()), O1, O2);

			// Antisymmetry: Resolve(A,B) is the mirror of Resolve(B,A).
			const EClashOutcome OBA = Paper2DPlusClash::ResolveClash(B, A, Graph);
			TestEqual(FString::Printf(TEXT("Antisymmetric: %s vs %s"), *A.ToString(), *B.ToString()), O1, Clash_Mirror(OBA));
		}
	}
	return true;
}

// --- The detect-and-warn validator (the F1 fix) ---------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusClashValidator, "Paper2DPlus.Clash.Validator",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPaper2DPlusClashValidator::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusHitboxTags;

	// The seeded genre-default graph is conflict-free.
	{
		FClashGraph Graph = Paper2DPlusClash::MakeDefaultClashGraph();
		TArray<FClashValidationIssue> Issues;
		const bool bOk = Paper2DPlusClash::ValidateClashGraph(Graph, Issues);
		TestTrue(TEXT("Seeded default graph validates clean"), bOk);
		int32 Errors = 0;
		for (const FClashValidationIssue& I : Issues) if (I.Severity == EClashValidationSeverity::Error) ++Errors;
		TestEqual(TEXT("Seeded default graph has no Error issues"), Errors, 0);
	}

	// An authored ambiguity (equal-specificity edges BOTH ways) is flagged Error.
	{
		FClashGraph Graph;
		Graph.Edges = {
			Clash_Edge(Category_Strike, Category_Projectile_Magic),   // Strike beats Projectile.Magic (4+5=9)
			Clash_Edge(Category_Projectile, Category_Strike_Heavy),   // Projectile beats Strike.Heavy (4+5=9)
		};
		TArray<FClashValidationIssue> Issues;
		const bool bOk = Paper2DPlusClash::ValidateClashGraph(Graph, Issues);
		TestFalse(TEXT("Ambiguous graph fails validation"), bOk);
		bool bFlaggedPair = false;
		for (const FClashValidationIssue& I : Issues)
		{
			if (I.Severity == EClashValidationSeverity::Error
				&& ((I.CategoryA == Category_Strike_Heavy && I.CategoryB == Category_Projectile_Magic)
					|| (I.CategoryA == Category_Projectile_Magic && I.CategoryB == Category_Strike_Heavy)))
			{
				bFlaggedPair = true;
			}
		}
		TestTrue(TEXT("The ambiguous Strike.Heavy vs Projectile.Magic pair is named"), bFlaggedPair);
	}

	// A more-specific override of one direction is NOT a conflict (different specificity).
	{
		FClashGraph Graph;
		Graph.Edges = {
			Clash_Edge(Category_Parry, Category_Strike),        // generic (4+4=8)
			Clash_Edge(Category_Strike_Heavy, Category_Parry),  // specific override (5+4=9)
		};
		TArray<FClashValidationIssue> Issues;
		TestTrue(TEXT("Specific override is unambiguous"), Paper2DPlusClash::ValidateClashGraph(Graph, Issues));
	}

	// An exact self-loop edge is a WARNING, not an Error (validation still passes).
	{
		FClashGraph Graph;
		Graph.Edges = { Clash_Edge(Category_Strike, Category_Strike) };
		TArray<FClashValidationIssue> Issues;
		const bool bOk = Paper2DPlusClash::ValidateClashGraph(Graph, Issues);
		TestTrue(TEXT("Self-loop-only graph still validates (Warning, not Error)"), bOk);
		bool bWarned = false;
		for (const FClashValidationIssue& I : Issues) if (I.Severity == EClashValidationSeverity::Warning) bWarned = true;
		TestTrue(TEXT("Self-loop edge warns"), bWarned);
	}

	// The bare ROOT tag as an endpoint is a Warning (match-everything wildcard footgun, F2).
	{
		FClashGraph Graph;
		Graph.Edges = { Clash_Edge(Category, Category_Strike) }; // root -> Strike
		TArray<FClashValidationIssue> Issues;
		Paper2DPlusClash::ValidateClashGraph(Graph, Issues);
		bool bWarnedRoot = false;
		for (const FClashValidationIssue& I : Issues) if (I.Severity == EClashValidationSeverity::Warning) bWarnedRoot = true;
		TestTrue(TEXT("Bare-root endpoint warns"), bWarnedRoot);
	}

	// A no-rule gap pair is NOT an Error.
	{
		FClashGraph Graph; // no edges at all
		TArray<FClashValidationIssue> Issues;
		TestTrue(TEXT("Empty graph validates clean (gaps are not errors)"), Paper2DPlusClash::ValidateClashGraph(Graph, Issues));
	}

	// The validator RESETS its output array (Codex P2) — a reused array doesn't carry stale issues.
	{
		FClashGraph Clean = Paper2DPlusClash::MakeDefaultClashGraph();
		TArray<FClashValidationIssue> Issues;
		Issues.AddDefaulted(); // pre-fill with a stale entry
		const bool bOk = Paper2DPlusClash::ValidateClashGraph(Clean, Issues);
		TestTrue(TEXT("Clean graph validates after a reused array"), bOk);
		TestEqual(TEXT("Stale issues are cleared on a clean run"), Issues.Num(), 0);
	}

	return true;
}

// --- Specificity max-reduction (the override contract) + the two-arg overload (the U2 seam) --------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusClashSpecificity, "Paper2DPlus.Clash.Specificity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPaper2DPlusClashSpecificity::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusHitboxTags;

	// Two SAME-direction edges supporting "Strike.Heavy beats Parry" at depths 8 and 9: BestBeatSpecificity
	// must return the MAX (9), permutation-invariantly — this pins the max-reduction the "specific override
	// beats generic" contract relies on (a first/last-match regression would return 8 in one edge order).
	{
		FClashGraph G;
		G.Edges = {
			Clash_Edge(Category_Strike, Category_Parry),        // depth 4+4 = 8
			Clash_Edge(Category_Strike_Heavy, Category_Parry),  // depth 5+4 = 9
		};
		TestEqual(TEXT("BestBeat picks the max (9)"),
			Paper2DPlusClash::BestBeatSpecificity(Category_Strike_Heavy, Category_Parry, G), 9);
		FClashGraph R = G;
		Algo::Reverse(R.Edges);
		TestEqual(TEXT("BestBeat max is permutation-invariant"),
			Paper2DPlusClash::BestBeatSpecificity(Category_Strike_Heavy, Category_Parry, R), 9);
		TestEqual(TEXT("Same-direction overlap resolves AWins (both orders)"),
			Paper2DPlusClash::ResolveClash(Category_Strike_Heavy, Category_Parry, R), EClashOutcome::AWins);
	}

	// The two-arg overload (the entry point U2 calls with hitbox categories): a conflict surfaces over the
	// supplied set, and DUPLICATE input tags don't double-report.
	{
		FClashGraph G;
		G.Edges = {
			Clash_Edge(Category_Strike, Category_Projectile_Magic),  // 4+5 = 9
			Clash_Edge(Category_Projectile, Category_Strike_Heavy),  // 4+5 = 9
		};
		TArray<FGameplayTag> Extra = { Category_Strike_Heavy, Category_Projectile_Magic, Category_Strike_Heavy /*dup*/ };
		TArray<FClashValidationIssue> Issues;
		const bool bOk = Paper2DPlusClash::ValidateClashGraph(G, Extra, Issues);
		TestFalse(TEXT("Two-arg overload surfaces the conflict"), bOk);
		int32 Errors = 0;
		for (const FClashValidationIssue& I : Issues) if (I.Severity == EClashValidationSeverity::Error) ++Errors;
		TestEqual(TEXT("Duplicate input tags don't double-report the pair"), Errors, 1);
	}

	return true;
}

// --- U2: per-hitbox clash category resolution (box tag else the move default) ---------------------------
// The makers (MakeCachedWorldHitbox / MakeWorldHitbox) carry FHitboxData::GetResolvedClashCategory into the
// world hitbox the broadphase reads; this pins that resolution rule worldlessly.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusClashHitboxCategory, "Paper2DPlus.Clash.HitboxCategory",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPaper2DPlusClashHitboxCategory::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusHitboxTags;

	FHitboxData Box;
	const FGameplayTag Empty;

	// A box with its own category keeps it, regardless of the move default.
	Box.ClashCategory = Category_Strike_Heavy;
	TestTrue(TEXT("Box tag wins over the move default"),
		Box.GetResolvedClashCategory(Category_Strike) == Category_Strike_Heavy);

	// A box with NO category inherits the move default.
	Box.ClashCategory = Empty;
	TestTrue(TEXT("Empty box inherits the move default"),
		Box.GetResolvedClashCategory(Category_Strike) == Category_Strike);

	// Both empty -> empty (no clash participation; the resolver then trades).
	TestFalse(TEXT("Empty box + empty default -> invalid (no participation)"),
		Box.GetResolvedClashCategory(Empty).IsValid());

	return true;
}

// --- U3: AttackConnects (the broadphase keep/suppress decision) + i-frame non-regression -----------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusClashAttackConnects, "Paper2DPlus.Clash.AttackConnects",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPaper2DPlusClashAttackConnects::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusHitboxTags;
	const FClashGraph G = Paper2DPlusClash::MakeDefaultClashGraph();
	auto Connects = [&](const FGameplayTag& A, const FGameplayTag& D) { return Paper2DPlusClash::AttackConnects(A, D, G); };

	// SUPPRESS — the defender's defense beats the attacker (BWins).
	TestFalse(TEXT("Armor absorbs Strike"), Connects(Category_Strike, Category_Armor));
	TestFalse(TEXT("Armor absorbs Projectile"), Connects(Category_Projectile, Category_Armor));
	TestFalse(TEXT("Parry beats Strike"), Connects(Category_Strike, Category_Parry));
	TestFalse(TEXT("Parry beats Projectile"), Connects(Category_Projectile, Category_Parry));
	TestFalse(TEXT("Invincible beats Strike"), Connects(Category_Strike, Category_Invincible));
	TestFalse(TEXT("Invincible beats Projectile"), Connects(Category_Projectile, Category_Invincible));

	// KEEP — the attacker beats the defense (AWins) or there's no rule (Trade/Clash).
	TestTrue(TEXT("Throw breaks Armor"), Connects(Category_Throw, Category_Armor));
	TestTrue(TEXT("Throw breaks Parry"), Connects(Category_Throw, Category_Parry));
	TestTrue(TEXT("Throw punishes Invincible (no Throw>Invincible edge -> Trade)"), Connects(Category_Throw, Category_Invincible));
	TestTrue(TEXT("Strike vs Strike-defense -> same-cat default -> connects"), Connects(Category_Strike, Category_Strike));

	// NON-REGRESSION / inherent gate — untagged content always connects, over BOTH defaults.
	FClashGraph ClashDefault = Paper2DPlusClash::MakeDefaultClashGraph();
	ClashDefault.Default = EClashSameCategoryDefault::Clash;
	const FGameplayTag Empty;
	const FClashGraph* const Graphs[] = { &G, &ClashDefault };
	for (const FClashGraph* Graph : Graphs)
	{
		TestTrue(TEXT("Untagged attacker + untagged defense connects"), Paper2DPlusClash::AttackConnects(Empty, Empty, *Graph));
		TestTrue(TEXT("Tagged attacker + untagged defense connects (fast path)"), Paper2DPlusClash::AttackConnects(Category_Strike, Empty, *Graph));
		// The critical pin: a tagged defender vs an UNTAGGED attacker still connects even under Default=Clash
		// (invalid attacker -> resolver returns the default; Clash maps to KEEP, not suppress).
		TestTrue(TEXT("Untagged attacker vs Armor defense connects (no silent drop under Default=Clash)"),
			Paper2DPlusClash::AttackConnects(Empty, Category_Armor, *Graph));
	}

	// Empty graph (the shipped DefaultClashGraph=None fallback) is a safe no-op — everything connects.
	TestTrue(TEXT("Empty graph: Strike vs Armor connects (no edges -> never BWins)"),
		Paper2DPlusClash::AttackConnects(Category_Strike, Category_Armor, FClashGraph()));

	// Hierarchy routes through AttackConnects: a generic strike is parried, a heavy strike overrides + lands.
	{
		FClashGraph H;
		H.Edges = {
			Clash_Edge(Category_Parry, Category_Strike),       // generic: parry beats strike
			Clash_Edge(Category_Strike_Heavy, Category_Parry),  // specific: heavy strike beats parry
		};
		TestFalse(TEXT("Generic Strike is parried"), Paper2DPlusClash::AttackConnects(Category_Strike, Category_Parry, H));
		TestTrue(TEXT("Strike.Heavy overrides and connects through Parry"), Paper2DPlusClash::AttackConnects(Category_Strike_Heavy, Category_Parry, H));
	}

	return true;
}

// --- U4: attack-vs-attack clash — ClashOutcomeDealsDamage + per-side antisymmetric consistency ----------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusClashAttackVsAttack, "Paper2DPlus.Clash.AttackVsAttack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPaper2DPlusClashAttackVsAttack::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusHitboxTags;

	// ClashOutcomeDealsDamage truth table (the bHit/connects mapping). Whiff is v1-unreachable but kept here
	// as a forward-compat case (it must read false).
	TestTrue(TEXT("Trade lands"), Paper2DPlusClash::ClashOutcomeDealsDamage(EClashOutcome::Trade));
	TestTrue(TEXT("AWins lands"), Paper2DPlusClash::ClashOutcomeDealsDamage(EClashOutcome::AWins));
	TestFalse(TEXT("BWins does not land"), Paper2DPlusClash::ClashOutcomeDealsDamage(EClashOutcome::BWins));
	TestFalse(TEXT("Clash does not land"), Paper2DPlusClash::ClashOutcomeDealsDamage(EClashOutcome::Clash));
	TestFalse(TEXT("Whiff does not land (forward-compat)"), Paper2DPlusClash::ClashOutcomeDealsDamage(EClashOutcome::Whiff));

	// Per-side consistency: each attacker resolves ITS OWN ResolveClash(my, other); the two are mirrors (no
	// shared cache needed). This is exactly what the subsystem's two QueryAttackClashes queries compute.
	auto Side = [](const FGameplayTag& My, const FGameplayTag& Other, const FClashGraph& G)
	{
		const EClashOutcome O = Paper2DPlusClash::ResolveClash(My, Other, G);
		return TPair<EClashOutcome, bool>(O, Paper2DPlusClash::ClashOutcomeDealsDamage(O));
	};

	// Priority clash: a heavy strike beats a light strike (seed graph). Heavy lands, light does not.
	{
		const FClashGraph G = Paper2DPlusClash::MakeDefaultClashGraph();
		const auto Heavy = Side(Category_Strike_Heavy, Category_Strike_Light, G);
		const auto Light = Side(Category_Strike_Light, Category_Strike_Heavy, G);
		TestEqual(TEXT("Heavy side wins"), Heavy.Key, EClashOutcome::AWins);
		TestEqual(TEXT("Light side is the mirror (loses)"), Light.Key, Clash_Mirror(Heavy.Key));
		TestTrue(TEXT("Heavy connects"), Heavy.Value);
		TestFalse(TEXT("Light does not connect"), Light.Value);
	}

	// No-rule pair -> Trade both ways -> both land.
	{
		const FClashGraph G = Paper2DPlusClash::MakeDefaultClashGraph();
		const auto A = Side(Category_Strike, Category_Throw, G);
		const auto B = Side(Category_Throw, Category_Strike, G);
		TestEqual(TEXT("No-rule A trades"), A.Key, EClashOutcome::Trade);
		TestEqual(TEXT("No-rule B trades"), B.Key, EClashOutcome::Trade);
		TestTrue(TEXT("Trade A lands"), A.Value);
		TestTrue(TEXT("Trade B lands"), B.Value);
	}

	// Same category under Default=Clash -> both Clash -> both negate.
	{
		FClashGraph G = Paper2DPlusClash::MakeDefaultClashGraph();
		G.Default = EClashSameCategoryDefault::Clash;
		const auto A = Side(Category_Strike, Category_Strike, G);
		const auto B = Side(Category_Strike, Category_Strike, G);
		TestEqual(TEXT("Same-cat clash A"), A.Key, EClashOutcome::Clash);
		TestEqual(TEXT("Same-cat clash B"), B.Key, EClashOutcome::Clash);
		TestFalse(TEXT("Clash A negates"), A.Value);
		TestFalse(TEXT("Clash B negates"), B.Value);
	}

	// A default-constructed clash result reads as a benign Trade-that-connects (additive non-regression).
	{
		FHitboxClashResult R;
		TestEqual(TEXT("Default clash result Outcome = Trade"), R.Outcome, EClashOutcome::Trade);
		TestTrue(TEXT("Default clash result connects"), R.bAttackConnects);
	}

	return true;
}

#endif // WITH_EDITOR
