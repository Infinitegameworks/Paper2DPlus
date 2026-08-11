// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#if WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "Paper2DPlusNetTypes.h"
#include "Paper2DPlusNetGating.h"
#include "Paper2DPlusMoveTransition.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Paper2DPlusCharacterProfileComponent.h"
#include "Paper2DPlusTestFrameCueTypes.h"
#include "FrameCues/Paper2DPlusFrameCue.h"
#include "PaperFlipbook.h"
#include "PaperFlipbookComponent.h"
#include "PaperSprite.h"
#include "GameFramework/Actor.h"
#include "Serialization/ObjectReader.h"
#include "Serialization/ObjectWriter.h"

/** TASK-57 U1 net-foundation tests (worldless). Pins the pure gating seam (Paper2DPlusNetGating —
 *  the five statics), the built-in frame-event NetPolicy ctor defaults, the typed hit-dedup key, and
 *  the dispatch-predicate integration: a worldless rig with NO context override resolves Standalone
 *  through ResolveNetContext's offline path, so EVERY policy dispatches identically to the
 *  pre-networking `return true` — the C1 single-player identity pin for the predicate rewire.
 *  Plus the NEGATIVE direction (ADV-4): forced-SimulatedProxy rigs (the !UE_BUILD_SHIPPING test
 *  seam) prove the predicate actually gates, on both the HandleFrameChanged call site and the
 *  HandleFlipbookChanged frame-0 funnel. The matrix oracle is a pair of HARDCODED literal tables
 *  transcribed from the plan (ADV-5), never computed logic that could mirror an implementation bug.
 *  Helpers are NetGate_-prefixed per the unity-build file-unique-name rule. */

namespace
{
	/** One realistic gating-matrix column: a (context, dedicated, locally-controlled) combination
	 *  that can actually occur at runtime (bDedicatedServer only pairs with Authority; an autonomous
	 *  proxy is by definition locally controlled; a simulated proxy never is). */
	struct FNetGate_MatrixColumn
	{
		const TCHAR* Name;
		EPaper2DPlusNetContext Context;
		bool bDedicatedServer;
		bool bIsLocallyControlled;
	};

	/** The five columns of the plan's gating-matrix table, in the table's column order (the
	 *  OwnerOnly listen-server NOT-locally-controlled footnote cell is asserted separately). */
	const FNetGate_MatrixColumn NetGate_MatrixColumns[] =
	{
		{ TEXT("Standalone"),                            EPaper2DPlusNetContext::Standalone,      false, false },
		{ TEXT("Authority(listen, locally controlled)"), EPaper2DPlusNetContext::Authority,       false, true  },
		{ TEXT("Authority(dedicated)"),                  EPaper2DPlusNetContext::Authority,       true,  false },
		{ TEXT("AutonomousProxy"),                       EPaper2DPlusNetContext::AutonomousProxy, false, true  },
		{ TEXT("SimulatedProxy"),                        EPaper2DPlusNetContext::SimulatedProxy,  false, false },
	};

	const TCHAR* NetGate_PolicyName(EPaper2DPlusFrameCueNetPolicy Policy)
	{
		switch (Policy)
		{
		case EPaper2DPlusFrameCueNetPolicy::LocalAlways:   return TEXT("LocalAlways");
		case EPaper2DPlusFrameCueNetPolicy::AuthorityOnly: return TEXT("AuthorityOnly");
		case EPaper2DPlusFrameCueNetPolicy::CosmeticOnly:  return TEXT("CosmeticOnly");
		case EPaper2DPlusFrameCueNetPolicy::OwnerOnly:     return TEXT("OwnerOnly");
		}
		return TEXT("<unknown>");
	}

	/** Row order of the literal expectation tables below — MUST match the tables' first index. */
	const EPaper2DPlusFrameCueNetPolicy NetGate_AllPolicies[] =
	{
		EPaper2DPlusFrameCueNetPolicy::LocalAlways,
		EPaper2DPlusFrameCueNetPolicy::AuthorityOnly,
		EPaper2DPlusFrameCueNetPolicy::CosmeticOnly,
		EPaper2DPlusFrameCueNetPolicy::OwnerOnly,
	};

	/** HARDCODED literal transcription of the plan's "Frame-event gating matrix" table
	 *  (docs/plans/2026-06-11-001-feat-paper2dplus-network-replication-plan.md) — deliberately
	 *  NOT computed logic, so the oracle cannot mirror an implementation bug (ADV-5).
	 *
	 *  Base (live dispatch, bIsCatchUp=false):
	 *    Policy \ Column | Standalone | listen+local | dedicated | autonomous | simulated
	 *    LocalAlways     |     Y      |      Y       |     Y     |     Y      |     Y
	 *    AuthorityOnly   |     Y      |      Y       |     Y     |     N      |     N
	 *    CosmeticOnly    |     Y      |      Y       |     N     |     Y      |     Y
	 *    OwnerOnly       |     Y      |      Y       |     N     |     Y      |     N    */
	const bool NetGate_BaseExpected[4][5] =
	{
		// Standalone, listen+local, dedicated, autonomous, simulated
		{ true,  true,  true,  true,  true  }, // LocalAlways
		{ true,  true,  true,  false, false }, // AuthorityOnly
		{ true,  true,  false, true,  true  }, // CosmeticOnly
		{ true,  true,  false, true,  false }, // OwnerOnly
	};

	/** HARDCODED catch-up table per the plan matrix's footnote: one-shot suppression on catch-up
	 *  is the CALLER's job — this table covers what the PURE FUNCTION returns with bIsCatchUp=true
	 *  (the ranged-Begin rebuild): ranged Begin only for LocalAlways/CosmeticOnly (and OwnerOnly
	 *  on the owner); AuthorityOnly never; the Standalone column stays constant-true. */
	const bool NetGate_CatchUpExpected[4][5] =
	{
		// Standalone, listen+local, dedicated, autonomous, simulated
		{ true,  true,  true,  true,  true  }, // LocalAlways
		{ true,  false, false, false, false }, // AuthorityOnly
		{ true,  true,  false, true,  true  }, // CosmeticOnly
		{ true,  true,  false, true,  false }, // OwnerOnly
	};

	/** Build a flipbook with NumFrames key frames (FPS 10, FrameRun 1 — frame i lives at i*0.1s). */
	UPaperFlipbook* NetGate_MakeFlipbook(UObject* Owner, int32 NumFrames)
	{
		UPaperFlipbook* FB = NewObject<UPaperFlipbook>(Owner);
		FScopedFlipbookMutator Mutator(FB);
		Mutator.FramesPerSecond = 10.0f;
		Mutator.KeyFrames.Empty();
		for (int32 i = 0; i < NumFrames; ++i)
		{
			FPaperFlipbookKeyFrame KF;
			KF.FrameRun = 1;
			KF.Sprite = NewObject<UPaperSprite>(FB);
			Mutator.KeyFrames.Add(KF);
		}
		return FB;
	}

	/** Add a flipbook entry named MoveName with a NumFrames-key flipbook; returns the live flipbook. */
	UPaperFlipbook* NetGate_AddMove(UPaper2DPlusCharacterProfileAsset* Asset, const FString& MoveName, int32 NumFrames)
	{
		FFlipbookProfileEntry Anim;
		Anim.Identity.FlipbookName = MoveName;
		UPaperFlipbook* FB = NetGate_MakeFlipbook(Asset, NumFrames);
		Anim.Identity.Flipbook = FB;
		Anim.CombatData.Frames.SetNum(NumFrames);
		Anim.CombatData.FrameExtractionInfo.SetNum(NumFrames);
		Asset->Flipbooks.Add(Anim);
		return FB;
	}

	/** Worldless rig: actor + stock flipbook component + profile component, wired and warmed. */
	struct FNetGate_Rig
	{
		AActor* Owner = nullptr;
		UPaperFlipbookComponent* FBComp = nullptr;
		UPaper2DPlusCharacterProfileComponent* DataComp = nullptr;
	};

	FNetGate_Rig NetGate_MakeRig(UPaper2DPlusCharacterProfileAsset* Asset, UPaperFlipbook* StartFlipbook)
	{
		FNetGate_Rig Rig;
		Rig.Owner = NewObject<AActor>();
		Rig.FBComp = NewObject<UPaperFlipbookComponent>(Rig.Owner);
		Rig.DataComp = NewObject<UPaper2DPlusCharacterProfileComponent>(Rig.Owner);
		Rig.Owner->AddOwnedComponent(Rig.FBComp);
		Rig.Owner->AddOwnedComponent(Rig.DataComp);
		if (StartFlipbook)
		{
			Rig.FBComp->SetFlipbook(StartFlipbook);
		}
		Rig.DataComp->CharacterProfile = Asset;
		Rig.DataComp->FlipbookComponent = Rig.FBComp;
		if (StartFlipbook)
		{
			Rig.DataComp->HandleFlipbookChanged(StartFlipbook);
		}
		return Rig;
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// ShouldDispatchFrameCue — the full networked policy x context x dedicated x
// locality x catch-up matrix, asserted cell by cell against the plan's table.
// Includes the OwnerOnly listen-server locality split (listen+local fires,
// listen+remote does not) and the catch-up rule (AuthorityOnly never fires a
// catch-up ranged Begin; cosmetic-grade policies keep their base row).
// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusNetGatingDispatchMatrix,
	"Paper2DPlus.Network.Gating.DispatchMatrix",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusNetGatingDispatchMatrix::RunTest(const FString& Parameters)
{
	static_assert(UE_ARRAY_COUNT(NetGate_AllPolicies) == 4, "table row count must match policy list");
	static_assert(UE_ARRAY_COUNT(NetGate_MatrixColumns) == 5, "table column count must match column list");

	for (int32 PolicyIdx = 0; PolicyIdx < (int32)UE_ARRAY_COUNT(NetGate_AllPolicies); ++PolicyIdx)
	{
		const EPaper2DPlusFrameCueNetPolicy Policy = NetGate_AllPolicies[PolicyIdx];
		for (int32 ColIdx = 0; ColIdx < (int32)UE_ARRAY_COUNT(NetGate_MatrixColumns); ++ColIdx)
		{
			const FNetGate_MatrixColumn& Col = NetGate_MatrixColumns[ColIdx];
			for (bool bIsCatchUp : { false, true })
			{
				const bool bExpected = bIsCatchUp
					? NetGate_CatchUpExpected[PolicyIdx][ColIdx]
					: NetGate_BaseExpected[PolicyIdx][ColIdx];
				const bool bActual = Paper2DPlusNetGating::ShouldDispatchFrameCue(
					Policy, Col.Context, Col.bDedicatedServer, Col.bIsLocallyControlled, bIsCatchUp);
				TestEqual(
					FString::Printf(TEXT("%s @ %s%s"),
						NetGate_PolicyName(Policy), Col.Name, bIsCatchUp ? TEXT(" [catch-up]") : TEXT("")),
					bActual, bExpected);
			}
		}
	}

	// The OwnerOnly listen-server locality split, pinned explicitly (the matrix's "Y if locally
	// controlled" footnote cell — the cell the 4-value context enum cannot encode, and the reason
	// the function takes bIsLocallyControlled at all). NOT-locally-controlled listen is N in both
	// the base and catch-up tables.
	TestTrue(TEXT("OwnerOnly fires on a locally-controlled listen-server pawn"),
		Paper2DPlusNetGating::ShouldDispatchFrameCue(
			EPaper2DPlusFrameCueNetPolicy::OwnerOnly, EPaper2DPlusNetContext::Authority,
			/*bDedicatedServer=*/false, /*bIsLocallyControlled=*/true, /*bIsCatchUp=*/false));
	TestFalse(TEXT("OwnerOnly does NOT fire for a remote pawn on a listen server"),
		Paper2DPlusNetGating::ShouldDispatchFrameCue(
			EPaper2DPlusFrameCueNetPolicy::OwnerOnly, EPaper2DPlusNetContext::Authority,
			/*bDedicatedServer=*/false, /*bIsLocallyControlled=*/false, /*bIsCatchUp=*/false));
	TestFalse(TEXT("OwnerOnly does NOT catch-up for a remote pawn on a listen server"),
		Paper2DPlusNetGating::ShouldDispatchFrameCue(
			EPaper2DPlusFrameCueNetPolicy::OwnerOnly, EPaper2DPlusNetContext::Authority,
			/*bDedicatedServer=*/false, /*bIsLocallyControlled=*/false, /*bIsCatchUp=*/true));

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Standalone fires EVERYTHING regardless of every other argument — the C1
// single-player identity pin (the 312-test guarantee: the predicate rewire is
// observably identical to the old hardcoded `return true` while context
// resolution returns Standalone).
// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusNetGatingStandaloneIdentity,
	"Paper2DPlus.Network.Gating.StandaloneIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusNetGatingStandaloneIdentity::RunTest(const FString& Parameters)
{
	for (EPaper2DPlusFrameCueNetPolicy Policy : NetGate_AllPolicies)
	{
		for (bool bDedicated : { false, true })
		{
			for (bool bLocal : { false, true })
			{
				for (bool bCatchUp : { false, true })
				{
					TestTrue(
						FString::Printf(TEXT("Standalone always dispatches: %s ded=%d loc=%d catchup=%d"),
							NetGate_PolicyName(Policy), bDedicated ? 1 : 0, bLocal ? 1 : 0, bCatchUp ? 1 : 0),
						Paper2DPlusNetGating::ShouldDispatchFrameCue(
							Policy, EPaper2DPlusNetContext::Standalone, bDedicated, bLocal, bCatchUp));
				}
			}
		}
	}
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// ShouldApplyRootMotion — only Standalone and Authority apply world offsets
// (KTD-16; consumed by U8, pinned with the rest of the seam here).
// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusNetGatingRootMotionGate,
	"Paper2DPlus.Network.Gating.RootMotionGate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusNetGatingRootMotionGate::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("Standalone applies root motion"),
		Paper2DPlusNetGating::ShouldApplyRootMotion(EPaper2DPlusNetContext::Standalone));
	TestTrue(TEXT("Authority applies root motion"),
		Paper2DPlusNetGating::ShouldApplyRootMotion(EPaper2DPlusNetContext::Authority));
	TestFalse(TEXT("AutonomousProxy does NOT apply root motion"),
		Paper2DPlusNetGating::ShouldApplyRootMotion(EPaper2DPlusNetContext::AutonomousProxy));
	TestFalse(TEXT("SimulatedProxy does NOT apply root motion"),
		Paper2DPlusNetGating::ShouldApplyRootMotion(EPaper2DPlusNetContext::SimulatedProxy));
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// ShouldApplySequence — "different => apply", 0 = never-published sentinel,
// identity not ordering: wraparound (65535 -> 3) and big relevancy gaps apply.
// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusNetGatingSequenceApply,
	"Paper2DPlus.Network.Gating.SequenceApply",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusNetGatingSequenceApply::RunTest(const FString& Parameters)
{
	// Fresh channel: last-applied 0 (never applied), incoming 1 (authority starts at 1) => apply.
	TestTrue(TEXT("0 -> 1 applies"), Paper2DPlusNetGating::ShouldApplySequence(1, 0));

	// Equal => skip (the no-op republish / duplicate-bunch case).
	TestFalse(TEXT("equal sequences skip"), Paper2DPlusNetGating::ShouldApplySequence(5, 5));
	TestFalse(TEXT("equal at the wrap boundary skips"), Paper2DPlusNetGating::ShouldApplySequence(65535, 65535));

	// Wraparound: 65535 -> 3 must apply (identity, never ">").
	TestTrue(TEXT("wrap 65535 -> 3 applies"), Paper2DPlusNetGating::ShouldApplySequence(3, 65535));

	// Large relevancy gaps in either numeric direction apply.
	TestTrue(TEXT("big forward gap applies"), Paper2DPlusNetGating::ShouldApplySequence(1000, 5));
	TestTrue(TEXT("big backward gap applies"), Paper2DPlusNetGating::ShouldApplySequence(5, 1000));

	// Incoming 0 is the never-published sentinel — never applies, even against 0.
	TestFalse(TEXT("incoming 0 never applies (vs 0)"), Paper2DPlusNetGating::ShouldApplySequence(0, 0));
	TestFalse(TEXT("incoming 0 never applies (vs 7)"), Paper2DPlusNetGating::ShouldApplySequence(0, 7));

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// ComputeReplicatedPlaybackPosition — anchor-derived playback position:
// mid-move, non-loop end (+ finished flag), loop fmod, negative elapsed clamps
// to 0, PlayRate != 1 (incl. under loop), Length <= 0 degenerate.
// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusNetGatingReplicatedPlaybackPosition,
	"Paper2DPlus.Network.Gating.ReplicatedPlaybackPosition",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusNetGatingReplicatedPlaybackPosition::RunTest(const FString& Parameters)
{
	const float Tolerance = 1.e-3f;
	bool bFinished = true;

	// Mid-move, rate 1, non-loop: 0.5s after the anchor of a 2s move => 0.5, not finished.
	float Pos = Paper2DPlusNetGating::ComputeReplicatedPlaybackPosition(
		/*StartServerTime=*/100.0, /*ServerNow=*/100.5, /*PlayRate=*/1.0f, /*Length=*/2.0f, /*bLooping=*/false, bFinished);
	TestEqual(TEXT("mid-move position"), Pos, 0.5f, Tolerance);
	TestFalse(TEXT("mid-move not finished"), bFinished);

	// Non-loop exact end: clamps to Length and reports finished.
	Pos = Paper2DPlusNetGating::ComputeReplicatedPlaybackPosition(100.0, 102.0, 1.0f, 2.0f, false, bFinished);
	TestEqual(TEXT("non-loop exact end clamps to Length"), Pos, 2.0f, Tolerance);
	TestTrue(TEXT("non-loop exact end finished"), bFinished);

	// Non-loop well past the end: still Length + finished (the late-join "move already over" case).
	Pos = Paper2DPlusNetGating::ComputeReplicatedPlaybackPosition(100.0, 105.0, 1.0f, 2.0f, false, bFinished);
	TestEqual(TEXT("non-loop past end clamps to Length"), Pos, 2.0f, Tolerance);
	TestTrue(TEXT("non-loop past end finished"), bFinished);

	// Loop: fmod into [0, Length). 5.3s into a 2s loop => 1.3, never finished.
	Pos = Paper2DPlusNetGating::ComputeReplicatedPlaybackPosition(100.0, 105.3, 1.0f, 2.0f, true, bFinished);
	TestEqual(TEXT("loop fmod position"), Pos, 1.3f, Tolerance);
	TestFalse(TEXT("loop never finished"), bFinished);

	// Negative elapsed (clock skew / anchor from the future) clamps to the move start.
	Pos = Paper2DPlusNetGating::ComputeReplicatedPlaybackPosition(100.0, 99.0, 1.0f, 2.0f, false, bFinished);
	TestEqual(TEXT("negative elapsed clamps to 0"), Pos, 0.0f, Tolerance);
	TestFalse(TEXT("negative elapsed not finished"), bFinished);

	// PlayRate != 1, non-loop: 0.5 wall seconds at rate 2 => 1.0 playback seconds.
	Pos = Paper2DPlusNetGating::ComputeReplicatedPlaybackPosition(100.0, 100.5, 2.0f, 3.0f, false, bFinished);
	TestEqual(TEXT("PlayRate 2 scales elapsed"), Pos, 1.0f, Tolerance);
	TestFalse(TEXT("PlayRate 2 mid-move not finished"), bFinished);

	// PlayRate scaling under loop: 2.7 wall seconds at rate 2 = 5.4 playback => fmod(5.4, 2) = 1.4.
	Pos = Paper2DPlusNetGating::ComputeReplicatedPlaybackPosition(100.0, 102.7, 2.0f, 2.0f, true, bFinished);
	TestEqual(TEXT("PlayRate 2 under loop fmods scaled elapsed"), Pos, 1.4f, Tolerance);
	TestFalse(TEXT("PlayRate 2 loop not finished"), bFinished);

	// Length <= 0 degenerate: 0, finished when non-looping, not finished when looping.
	Pos = Paper2DPlusNetGating::ComputeReplicatedPlaybackPosition(100.0, 105.0, 1.0f, 0.0f, false, bFinished);
	TestEqual(TEXT("zero length returns 0"), Pos, 0.0f, Tolerance);
	TestTrue(TEXT("zero length non-loop reports finished"), bFinished);
	Pos = Paper2DPlusNetGating::ComputeReplicatedPlaybackPosition(100.0, 105.0, 1.0f, 0.0f, true, bFinished);
	TestEqual(TEXT("zero length looping returns 0"), Pos, 0.0f, Tolerance);
	TestFalse(TEXT("zero length looping not finished"), bFinished);

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// FPaper2DPlusHitDedupKey — equality and hash distinctness across all four
// axes (attacker / victim / move instance / hit window), plus TSet semantics
// (the consumer container in U4's ProcessedHits).
// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusNetGatingHitDedupKey,
	"Paper2DPlus.Network.Gating.HitDedupKey",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusNetGatingHitDedupKey::RunTest(const FString& Parameters)
{
	AActor* AttackerA = NewObject<AActor>();
	AActor* AttackerB = NewObject<AActor>();
	AActor* VictimA = NewObject<AActor>();
	AActor* VictimB = NewObject<AActor>();

	const FPaper2DPlusHitDedupKey Base(AttackerA, VictimA, /*MoveInstance=*/1, /*HitWindowIndex=*/0);
	const FPaper2DPlusHitDedupKey SameAsBase(AttackerA, VictimA, 1, 0);

	// Identity: same tuple compares equal and hashes equal (TSet correctness requirement).
	TestTrue(TEXT("identical tuples are equal"), Base == SameAsBase);
	TestTrue(TEXT("identical tuples hash equal"), GetTypeHash(Base) == GetTypeHash(SameAsBase));

	// Each axis alone distinguishes the key.
	const FPaper2DPlusHitDedupKey DiffAttacker(AttackerB, VictimA, 1, 0);
	const FPaper2DPlusHitDedupKey DiffVictim(AttackerA, VictimB, 1, 0);
	const FPaper2DPlusHitDedupKey DiffInstance(AttackerA, VictimA, 2, 0);
	const FPaper2DPlusHitDedupKey DiffWindow(AttackerA, VictimA, 1, 1);

	TestTrue(TEXT("attacker axis distinguishes"), Base != DiffAttacker);
	TestTrue(TEXT("victim axis distinguishes"), Base != DiffVictim);
	TestTrue(TEXT("move-instance axis distinguishes"), Base != DiffInstance);
	TestTrue(TEXT("hit-window axis distinguishes"), Base != DiffWindow);

	// TSet treats the five distinct keys as distinct, and a duplicate add does not grow the set —
	// exactly the once-per-(attacker,victim,instance,window) registration semantics U4 relies on.
	TSet<FPaper2DPlusHitDedupKey> Keys;
	Keys.Add(Base);
	Keys.Add(DiffAttacker);
	Keys.Add(DiffVictim);
	Keys.Add(DiffInstance);
	Keys.Add(DiffWindow);
	TestEqual(TEXT("five distinct keys occupy five set slots"), Keys.Num(), 5);

	Keys.Add(SameAsBase);
	TestEqual(TEXT("duplicate add does not grow the set"), Keys.Num(), 5);
	TestTrue(TEXT("set finds the duplicate by value"), Keys.Contains(SameAsBase));

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Frame Cue NetPolicy class defaults: the base class and a freshly constructed
// BP-style subclass default to LocalAlways (the pre-networking behavior),
// inert until per-component opt-in.
// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusNetPolicyFrameCueDefaults,
	"Paper2DPlus.Network.Policy.FrameCueClassDefaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusNetPolicyFrameCueDefaults::RunTest(const FString& Parameters)
{
	// The abstract base and author classes keep LocalAlways — the default every user subclass
	// inherits. (int32 casts: FAutomationTestBase has no enum-class TestEqual overload.)
	TestEqual(TEXT("base class defaults LocalAlways"),
		(int32)GetDefault<UPaper2DPlusCueBase>()->NetPolicy, (int32)EPaper2DPlusFrameCueNetPolicy::LocalAlways);
	TestEqual(TEXT("Moment author class defaults LocalAlways"),
		(int32)GetDefault<UPaper2DPlusCue>()->NetPolicy, (int32)EPaper2DPlusFrameCueNetPolicy::LocalAlways);
	TestEqual(TEXT("Range author class defaults LocalAlways"),
		(int32)GetDefault<UPaper2DPlusCueState>()->NetPolicy, (int32)EPaper2DPlusFrameCueNetPolicy::LocalAlways);

	// A freshly CONSTRUCTED BP-style subclass (not just the CDO) also lands on LocalAlways.
	UPaper2DPlusTestMomentCue* FreshMoment = NewObject<UPaper2DPlusTestMomentCue>();
	TestEqual(TEXT("fresh Moment subclass instance defaults LocalAlways"),
		(int32)FreshMoment->NetPolicy, (int32)EPaper2DPlusFrameCueNetPolicy::LocalAlways);
	UPaper2DPlusTestRangeCue* FreshRange = NewObject<UPaper2DPlusTestRangeCue>();
	TestEqual(TEXT("fresh Range subclass instance defaults LocalAlways"),
		(int32)FreshRange->NetPolicy, (int32)EPaper2DPlusFrameCueNetPolicy::LocalAlways);

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// WHY a Frame Cue class default cannot be edited in place.
//
// A Cue class's NetPolicy default is not a preference, it is a serialized
// contract. A placement that ACCEPTED its class default wrote NOTHING to the
// asset — delta serialization only records values that differ from the class
// default — so changing that default silently rewrites the effective policy of
// every already-saved placement of that class. Change one only behind a
// versioned PostLoad migration, and update the defaults test above in the same
// commit.
//
// This is a characterization test, not an aspiration: it reproduces the exact
// mechanism so the reason survives without depending on anyone's memory. It was
// written against the built-in Cue classes; those are gone, and the mechanism is
// a property of delta serialization rather than of any particular class, so it
// is pinned here on an ordinary Cue subclass instead.
// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusNetPolicyDeltaSerializationRebase,
	"Paper2DPlus.Network.Policy.NetPolicyDeltaSerializationRebasesClassDefaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusNetPolicyDeltaSerializationRebase::RunTest(const FString& Parameters)
{
	// 1. A placement that accepted its class default writes NO Net Policy at all. Reading it back over
	//    a different value therefore leaves that different value standing — which is precisely what a
	//    changed class default would do to every asset saved under the old one.
	{
		UPaper2DPlusTestMomentCue* const Saved = NewObject<UPaper2DPlusTestMomentCue>();
		Saved->CustomPayload = 25; // an authored value, so the archive is provably non-empty

		TArray<uint8> Bytes;
		FObjectWriter Writer(Saved, Bytes);

		UPaper2DPlusTestMomentCue* const Reloaded = NewObject<UPaper2DPlusTestMomentCue>();
		Reloaded->NetPolicy = EPaper2DPlusFrameCueNetPolicy::AuthorityOnly; // "the class default changed"
		FObjectReader Reader(Reloaded, Bytes);

		TestEqual(TEXT("The archive did carry the placement's authored payload"),
			Reloaded->CustomPayload, 25);
		TestEqual(
			TEXT("A default-valued Net Policy is absent from the archive and takes the class default"),
			(int32)Reloaded->NetPolicy, (int32)EPaper2DPlusFrameCueNetPolicy::AuthorityOnly);
	}

	// 2. An explicitly authored Net Policy differs from the class default, is therefore written, and
	//    survives a reload onto an object holding something else entirely. That is the shape of a
	//    placement that CHOSE its policy, and why such a placement is unaffected by a default flip.
	{
		UPaper2DPlusTestMomentCue* const Saved = NewObject<UPaper2DPlusTestMomentCue>();
		Saved->NetPolicy = EPaper2DPlusFrameCueNetPolicy::CosmeticOnly;

		TArray<uint8> Bytes;
		FObjectWriter Writer(Saved, Bytes);

		UPaper2DPlusTestMomentCue* const Reloaded = NewObject<UPaper2DPlusTestMomentCue>();
		Reloaded->NetPolicy = EPaper2DPlusFrameCueNetPolicy::AuthorityOnly;
		FObjectReader Reader(Reloaded, Bytes);

		TestEqual(TEXT("An authored Cosmetic Only placement reloads as Cosmetic Only"),
			(int32)Reloaded->NetPolicy, (int32)EPaper2DPlusFrameCueNetPolicy::CosmeticOnly);
	}
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Dispatch-predicate worldless integration pin (the C1 identity pin for the
// predicate rewire): a worldless rig with NO context override resolves
// Standalone through ResolveNetContext's offline path, so events carrying
// EVERY NetPolicy dispatch identically to today's behavior — Moments broadcast
// once on their trigger frame and Cue States Begin.
// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusNetPolicyDispatchPredicateStandalonePin,
	"Paper2DPlus.Network.Policy.DispatchPredicateStandalonePin",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusNetPolicyDispatchPredicateStandalonePin::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	UPaperFlipbook* FB = NetGate_AddMove(Asset, TEXT("Attack"), 3);

	// One Cue per policy, all anchored on frame 1 (never the HandleFlipbookChanged
	// frame-0 funnel — this test pins HandleFrameChanged's predicate call site).
	const EPaper2DPlusFrameCueNetPolicy CuePolicies[] =
	{
		EPaper2DPlusFrameCueNetPolicy::LocalAlways,
		EPaper2DPlusFrameCueNetPolicy::AuthorityOnly,
		EPaper2DPlusFrameCueNetPolicy::CosmeticOnly,
		EPaper2DPlusFrameCueNetPolicy::OwnerOnly,
	};
	TArray<UPaper2DPlusTestMomentCue*> Moments;
	for (EPaper2DPlusFrameCueNetPolicy Policy : CuePolicies)
	{
		UPaper2DPlusTestMomentCue* Cue = NewObject<UPaper2DPlusTestMomentCue>(Asset);
		Cue->TriggerFrame = 1;
		Cue->NetPolicy = Policy;
		Asset->Flipbooks[0].FrameEventData.FrameCues.Add(Cue);
		Moments.Add(Cue);
	}

	// Plus a ranged AuthorityOnly event spanning frames 1-2: the most-restrictive networked policy
	// must still Begin normally on the Standalone path (bIsCatchUp is false in live dispatch).
	UPaper2DPlusTestRangeCue* Range = NewObject<UPaper2DPlusTestRangeCue>(Asset);
	Range->StartFrame = 1;
	Range->FrameCount = 2;
	Range->NetPolicy = EPaper2DPlusFrameCueNetPolicy::AuthorityOnly;
	Asset->Flipbooks[0].FrameEventData.FrameCues.Add(Range);

	FNetGate_Rig Rig = NetGate_MakeRig(Asset, FB);

	// The worldless rig resolves Standalone with no override set — the offline path of THE seam.
	TestEqual(TEXT("worldless rig resolves Standalone"),
		(int32)Rig.DataComp->GetNetContext(), (int32)EPaper2DPlusNetContext::Standalone);

	UPaper2DPlusFrameCueRecorder* Recorder = NewObject<UPaper2DPlusFrameCueRecorder>();
	Rig.DataComp->OnFrameCue.AddDynamic(Recorder, &UPaper2DPlusFrameCueRecorder::OnCue);

	// Drive playback onto key frame 1 and dispatch.
	Rig.FBComp->SetPlaybackPosition(1 * 0.1f + 0.05f, false);
	Rig.DataComp->HandleFrameChanged(1);

	for (UPaper2DPlusTestMomentCue* Cue : Moments)
	{
		TestTrue(TEXT("each policy's Cue broadcasts via Standalone"), Recorder->Cues.Contains(Cue));
	}
	TestTrue(TEXT("AuthorityOnly Cue State begins via Standalone"), Rig.DataComp->IsRangeCueActiveForTests(Range));
	TestEqual(TEXT("OnFrameCue broadcasts all four Moments plus the Range Begin"), Recorder->Contexts.Num(), 5);

	return true;
}

// The two negative-direction predicate pins below drive the SetNetContextOverrideForTests seam,
// which is compiled out of Shipping — same guard here so a Test/Shipping config never breaks.
#if !UE_BUILD_SHIPPING

// ─────────────────────────────────────────────────────────────────────────────
// Negative-direction predicate pin (ADV-4): a forced SimulatedProxy context
// actually GATES at the dispatch-predicate call site — an AuthorityOnly
// one-shot stays silent while a LocalAlways one fires, the gated-out
// AuthorityOnly RANGED event is neither Begun nor force-ended (a policy skip
// keeps it in the snapshot for stale bookkeeping; it must not be swept), the
// OnFrameCue broadcast count matches what actually passed, and clearing
// the override (TOptional unset) restores the Standalone identity.
// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusNetPolicyDispatchPredicateProxyGating,
	"Paper2DPlus.Network.Policy.DispatchPredicateProxyGating",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusNetPolicyDispatchPredicateProxyGating::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	UPaperFlipbook* FB = NetGate_AddMove(Asset, TEXT("Attack"), 3);

	// Frame-1 one-shots: the gated policy and the always-local policy, side by side.
	UPaper2DPlusTestMomentCue* AuthorityMoment = NewObject<UPaper2DPlusTestMomentCue>(Asset);
	AuthorityMoment->TriggerFrame = 1;
	AuthorityMoment->NetPolicy = EPaper2DPlusFrameCueNetPolicy::AuthorityOnly;
	Asset->Flipbooks[0].FrameEventData.FrameCues.Add(AuthorityMoment);

	UPaper2DPlusTestMomentCue* LocalMoment = NewObject<UPaper2DPlusTestMomentCue>(Asset);
	LocalMoment->TriggerFrame = 1;
	LocalMoment->NetPolicy = EPaper2DPlusFrameCueNetPolicy::LocalAlways;
	Asset->Flipbooks[0].FrameEventData.FrameCues.Add(LocalMoment);

	// An AuthorityOnly RANGED event covering frames 1-2 — gated out at frame 1, it must simply
	// not Begin (and must NOT be force-ended; EndCount is the counter that pins that). The gate-latch
	// (TASK-57 U8) then keeps it LATCHED OUT for the rest of this flipbook: clearing the context
	// override mid-range can NEITHER start it nor re-evaluate it (a latched-out ranged event only
	// re-evaluates at a flipbook change or loop wrap). Identity-after-clear is verified instead by the
	// frame-2 AuthorityOnly ONE-SHOT below — one-shots are not latched and re-evaluate every frame.
	UPaper2DPlusTestRangeCue* Range = NewObject<UPaper2DPlusTestRangeCue>(Asset);
	Range->StartFrame = 1;
	Range->FrameCount = 2;
	Range->NetPolicy = EPaper2DPlusFrameCueNetPolicy::AuthorityOnly;
	Asset->Flipbooks[0].FrameEventData.FrameCues.Add(Range);

	// A frame-2 AuthorityOnly one-shot for the override-cleared resume leg.
	UPaper2DPlusTestMomentCue* Frame2AuthorityMoment = NewObject<UPaper2DPlusTestMomentCue>(Asset);
	Frame2AuthorityMoment->TriggerFrame = 2;
	Frame2AuthorityMoment->NetPolicy = EPaper2DPlusFrameCueNetPolicy::AuthorityOnly;
	Asset->Flipbooks[0].FrameEventData.FrameCues.Add(Frame2AuthorityMoment);

	FNetGate_Rig Rig = NetGate_MakeRig(Asset, FB);

	UPaper2DPlusFrameCueRecorder* Recorder = NewObject<UPaper2DPlusFrameCueRecorder>();
	Rig.DataComp->OnFrameCue.AddDynamic(Recorder, &UPaper2DPlusFrameCueRecorder::OnCue);

	// Force the SimulatedProxy context and drive frame 1.
	Rig.DataComp->SetNetContextOverrideForTests(EPaper2DPlusNetContext::SimulatedProxy);
	Rig.FBComp->SetPlaybackPosition(1 * 0.1f + 0.05f, false);
	Rig.DataComp->HandleFrameChanged(1);

	TestFalse(TEXT("AuthorityOnly Moment gated out on SimulatedProxy"), Recorder->Cues.Contains(AuthorityMoment));
	TestTrue(TEXT("LocalAlways Moment broadcasts on SimulatedProxy"), Recorder->Cues.Contains(LocalMoment));
	TestEqual(TEXT("OnFrameCue broadcasts only the cue that passed"), Recorder->Contexts.Num(), 1);

	// The gated-out ranged event was never Begun AND never (force-)ended — being policy-skipped
	// keeps it PRESENT in dispatch bookkeeping without triggering the stale force-end sweep.
	TestFalse(TEXT("gated Cue State never begins"), Rig.DataComp->IsRangeCueActiveForTests(Range));

	// Clear the override (TOptional unset): the worldless rig resolves Standalone again, restoring the
	// identity context. ONE-SHOTS re-evaluate every frame, so the frame-2 AuthorityOnly one-shot fires
	// — proof the gate is back to constant-true. The latched-OUT ranged event, however, stays out: the
	// gate-latch (TASK-57 U8) holds a ranged event that failed its Begin-edge gate out for the rest of
	// the flipbook — a mid-range context flip can neither start nor re-evaluate it (it only re-evaluates
	// at a flipbook change or loop wrap). So Begin/Tick must STAY 0 on frame 2 even though the context
	// now passes.
	Rig.DataComp->SetNetContextOverrideForTests(TOptional<EPaper2DPlusNetContext>());
	TestEqual(TEXT("override cleared resolves Standalone"),
		(int32)Rig.DataComp->GetNetContext(), (int32)EPaper2DPlusNetContext::Standalone);

	Rig.FBComp->SetPlaybackPosition(2 * 0.1f + 0.05f, false);
	Rig.DataComp->HandleFrameChanged(2);

	// Identity restored: the one-shot (not latched) fires on frame 2.
	TestTrue(TEXT("frame-2 AuthorityOnly Moment broadcasts after override clears"), Recorder->Cues.Contains(Frame2AuthorityMoment));
	// Latched OUT: the ranged event neither Begins nor Ticks on frame 2 despite the now-passing gate.
	TestFalse(TEXT("latched-out Cue State stays out on frame 2"), Rig.DataComp->IsRangeCueActiveForTests(Range));
	TestFalse(TEXT("frame-1 AuthorityOnly Moment does not retro-broadcast"), Recorder->Cues.Contains(AuthorityMoment));
	// Broadcast count tracks: frame 1 (LocalAlways one-shot) + frame 2 (the one AuthorityOnly one-shot)
	// = 2. The latched-out ranged event contributes nothing.
	TestEqual(TEXT("broadcast count tracks the frame-1 + single frame-2 cue"), Recorder->Contexts.Num(), 2);

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Frame-0 funnel gating pin (ADV-4): HandleFlipbookChanged routes frame-0
// dispatch through HandleFrameChanged — THE single gating seam — so a frame-0
// AuthorityOnly one-shot is gated on a SimulatedProxy while a frame-0
// LocalAlways one-shot still fires. Pins that the frame-0 path has no side
// door around the predicate.
// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusNetPolicyFrameZeroFunnelGated,
	"Paper2DPlus.Network.Policy.FrameZeroFunnelGated",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusNetPolicyFrameZeroFunnelGated::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	UPaperFlipbook* FB = NetGate_AddMove(Asset, TEXT("Attack"), 3);

	UPaper2DPlusTestMomentCue* AuthorityFrame0 = NewObject<UPaper2DPlusTestMomentCue>(Asset);
	AuthorityFrame0->TriggerFrame = 0;
	AuthorityFrame0->NetPolicy = EPaper2DPlusFrameCueNetPolicy::AuthorityOnly;
	Asset->Flipbooks[0].FrameEventData.FrameCues.Add(AuthorityFrame0);

	UPaper2DPlusTestMomentCue* LocalFrame0 = NewObject<UPaper2DPlusTestMomentCue>(Asset);
	LocalFrame0->TriggerFrame = 0;
	LocalFrame0->NetPolicy = EPaper2DPlusFrameCueNetPolicy::LocalAlways;
	Asset->Flipbooks[0].FrameEventData.FrameCues.Add(LocalFrame0);

	// Build the rig WITHOUT the helper's warm-up HandleFlipbookChanged (it would run the funnel
	// under Standalone before the override is set), then force SimulatedProxy and drive the
	// frame-0 funnel directly.
	FNetGate_Rig Rig = NetGate_MakeRig(Asset, /*StartFlipbook=*/nullptr);
	Rig.DataComp->SetNetContextOverrideForTests(EPaper2DPlusNetContext::SimulatedProxy);
	UPaper2DPlusFrameCueRecorder* Recorder = NewObject<UPaper2DPlusFrameCueRecorder>();
	Rig.DataComp->OnFrameCue.AddDynamic(Recorder, &UPaper2DPlusFrameCueRecorder::OnCue);

	Rig.FBComp->SetFlipbook(FB);
	Rig.DataComp->HandleFlipbookChanged(FB);

	TestFalse(TEXT("frame-0 AuthorityOnly Moment gated through the funnel"), Recorder->Cues.Contains(AuthorityFrame0));
	TestTrue(TEXT("frame-0 LocalAlways Moment broadcasts through the funnel"), Recorder->Cues.Contains(LocalFrame0));
	TestEqual(TEXT("exactly one frame-0 cue passes"), Recorder->Cues.Num(), 1);

	return true;
}

#endif // !UE_BUILD_SHIPPING

#endif // WITH_EDITOR
