// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#if WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "Containers/Ticker.h"
#include "Paper2DPlusNetTypes.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Paper2DPlusCharacterProfileComponent.h"
#include "Paper2DPlusTestTransitionTypes.h" // UPaper2DPlusHitStopRecorder (shared via unity build)
#include "PaperFlipbook.h"
#include "PaperFlipbookComponent.h"
#include "PaperSprite.h"
#include "GameFramework/Actor.h"

/** TASK-57 U5 hit-stop-replication tests (worldless, driven through the U1/U2/U5 test seams:
 *  SetNetContextOverrideForTests / SetServerTimeOverrideForTests / GetRepHitStopForTests /
 *  GetRepAnimStateForTests / GetActualFrozenSecondsForTests / FlushPendingRepHitStopForTests, plus the
 *  FTSTicker::GetCoreTicker().Tick() real-time pump the PR4 hit-stop rig idioms established).
 *
 *  The contract (KTD-1 self-expiring snapshot, KTD-19 clock re-anchor, KTD-21 unconditional clamp,
 *  KTD-24 victim-side publish / null-victim toleration):
 *   - PUBLISH: a proxy TriggerHitStop warns + no-ops; authority freezes (the EXISTING refcounted
 *     registry, unchanged) AND publishes RepHitStop (HitStopSeq bumps on a NEW trigger, stays stable +
 *     Duration updates on an EXTEND); CancelHitStop / EndPlay / expiry publish Duration=0.
 *   - RECEIVE: OnRep_HitStop computes Remaining = clamp(Duration - (ServerNow - StartServerTime), 0,
 *     Duration) UNCONDITIONALLY (a garbage/travel clock can never freeze for an arbitrary span); a
 *     mid-freeze late join applies the CLAMPED remaining; an expired snapshot (Remaining<=0) is ignored;
 *     Duration=0 ends an active local freeze; a null Victim freezes the attacker only (no warn);
 *     no-server-clock defers one frame.
 *   - CLOCK RE-ANCHOR: freezing N real seconds advances RepAnimState.StartServerTime by N on unfreeze
 *     (same Sequence — never bumped); a proxy under a replicated freeze suppresses the U8 drift snap via
 *     IsHitStopActive.
 *   - REGRESSION: single-player hit-stop is byte-identical with replication off.
 *
 *  Helpers are NetHS_-prefixed per the unity-build file-unique-name rule. */

namespace
{
	/** Build a flipbook with NumFrames key frames (FPS 10, FrameRun 1 — frame i lives at i*0.1s,
	 *  total duration NumFrames*0.1s). */
	UPaperFlipbook* NetHS_MakeFlipbook(UObject* Owner, int32 NumFrames)
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
	UPaperFlipbook* NetHS_AddMove(UPaper2DPlusCharacterProfileAsset* Asset, const FString& MoveName, int32 NumFrames)
	{
		FFlipbookProfileEntry Anim;
		Anim.Identity.FlipbookName = MoveName;
		UPaperFlipbook* FB = NetHS_MakeFlipbook(Asset, NumFrames);
		Anim.Identity.Flipbook = FB;
		Anim.CombatData.Frames.SetNum(NumFrames);
		Anim.CombatData.FrameExtractionInfo.SetNum(NumFrames);
		Asset->Flipbooks.Add(Anim);
		return FB;
	}

	struct FNetHS_Rig
	{
		AActor* Owner = nullptr;
		UPaperFlipbookComponent* FBComp = nullptr;
		UPaper2DPlusCharacterProfileComponent* DataComp = nullptr;
	};

	FNetHS_Rig NetHS_MakeRig(bool bEnableReplication = true)
	{
		FNetHS_Rig Rig;
		Rig.Owner = NewObject<AActor>();
		Rig.FBComp = NewObject<UPaperFlipbookComponent>(Rig.Owner);
		Rig.DataComp = NewObject<UPaper2DPlusCharacterProfileComponent>(Rig.Owner);
		Rig.Owner->AddOwnedComponent(Rig.FBComp);
		Rig.Owner->AddOwnedComponent(Rig.DataComp);
		Rig.DataComp->FlipbookComponent = Rig.FBComp;
		// Worldless: never BeginPlays, so ResolveNetContext reads the live flag (the override wins).
		Rig.DataComp->bEnableReplication = bEnableReplication;
		Rig.Owner->CustomTimeDilation = 1.0f;
		return Rig;
	}

	/** The frozen dilation sentinel the local freeze applies (small non-zero so dt-dividing components
	 *  can't NaN; the restore is guarded on this exact value). */
	constexpr float NetHS_FrozenDilation = UPaper2DPlusCharacterProfileComponent::HitStopFrozenDilation;
}

// Everything below drives the !UE_BUILD_SHIPPING test seams.
#if !UE_BUILD_SHIPPING

// ─────────────────────────────────────────────────────────────────────────────
// PUBLISH: a non-authority (proxy) TriggerHitStop warns once + no-ops (no local
// freeze, no wire publish); an authority TriggerHitStop freezes (the EXISTING
// registry) AND publishes the snapshot.
// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusNetHitStopProxyTriggerNoOps,
	"Paper2DPlus.Network.HitStop.ProxyTriggerNoOps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusNetHitStopProxyTriggerNoOps::RunTest(const FString& Parameters)
{
	AActor* Victim = NewObject<AActor>();
	Victim->CustomTimeDilation = 1.0f;

	// The warn-once gate fires once per COMPONENT; the loop builds TWO proxy rigs => exactly two.
	AddExpectedError(TEXT("non-authority context"), EAutomationExpectedErrorFlags::Contains, /*Occurrences=*/2);

	for (EPaper2DPlusNetContext ProxyCtx : { EPaper2DPlusNetContext::AutonomousProxy, EPaper2DPlusNetContext::SimulatedProxy })
	{
		FNetHS_Rig Rig = NetHS_MakeRig();
		Rig.DataComp->SetNetContextOverrideForTests(ProxyCtx);

		Rig.DataComp->TriggerHitStop(Victim, 0.1f);

		TestFalse(TEXT("proxy TriggerHitStop does not activate a local freeze"), Rig.DataComp->IsHitStopActive());
		TestEqual(TEXT("proxy TriggerHitStop leaves the owner dilation untouched"),
			Rig.Owner->CustomTimeDilation, 1.0f, 1.e-6f);
		TestEqual(TEXT("proxy TriggerHitStop publishes nothing (HitStopSeq stays 0)"),
			(int32)Rig.DataComp->GetRepHitStopForTests().HitStopSeq, 0);
	}

	// Authority freezes + publishes.
	{
		FNetHS_Rig Rig = NetHS_MakeRig();
		Rig.DataComp->SetNetContextOverrideForTests(EPaper2DPlusNetContext::Authority);
		Rig.DataComp->SetServerTimeOverrideForTests(1000.0);

		Rig.DataComp->TriggerHitStop(Victim, 0.1f);

		TestTrue(TEXT("authority TriggerHitStop activates the local freeze"), Rig.DataComp->IsHitStopActive());
		TestEqual(TEXT("authority TriggerHitStop freezes the owner (registry sentinel)"),
			Rig.Owner->CustomTimeDilation, NetHS_FrozenDilation, 1.e-6f);

		const FPaper2DPlusRepHitStop& Snap = Rig.DataComp->GetRepHitStopForTests();
		TestTrue(TEXT("authority TriggerHitStop bumped HitStopSeq off the sentinel"), Snap.HitStopSeq != 0);
		TestTrue(TEXT("authority snapshot carries the duration"),
			FMath::IsNearlyEqual(Snap.DurationSeconds, 0.1f, 1.e-4f));
		TestTrue(TEXT("authority snapshot anchored at the server time"),
			FMath::IsNearlyEqual((float)Snap.StartServerTime, 1000.0f, 1.e-3f));
		TestTrue(TEXT("authority snapshot carries the victim"), Snap.Victim == Victim);

		Rig.DataComp->CancelHitStop(); // retire the ticker before the test exits
	}

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// PUBLISH: HitStopSeq bumps on a NEW trigger and stays STABLE on an EXTEND (a
// re-trigger while active) — the extend only updates Duration (latest-snapshot-wins).
// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusNetHitStopSeqBumpVsExtend,
	"Paper2DPlus.Network.HitStop.SeqBumpVsExtend",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusNetHitStopSeqBumpVsExtend::RunTest(const FString& Parameters)
{
	AActor* Victim = NewObject<AActor>();

	FNetHS_Rig Rig = NetHS_MakeRig();
	Rig.DataComp->SetNetContextOverrideForTests(EPaper2DPlusNetContext::Authority);
	Rig.DataComp->SetServerTimeOverrideForTests(1000.0);

	Rig.DataComp->TriggerHitStop(Victim, 0.1f);
	const uint16 SeqAfterNew = Rig.DataComp->GetRepHitStopForTests().HitStopSeq;
	const double AnchorAfterNew = Rig.DataComp->GetRepHitStopForTests().StartServerTime;
	TestTrue(TEXT("a NEW trigger bumps HitStopSeq"), SeqAfterNew != 0);

	// EXTEND (re-trigger while active, longer duration): seq STABLE, Duration updated, anchor unchanged.
	Rig.DataComp->TriggerHitStop(Victim, 0.2f);
	const FPaper2DPlusRepHitStop& AfterExtend = Rig.DataComp->GetRepHitStopForTests();
	TestEqual(TEXT("an EXTEND keeps HitStopSeq stable"), (int32)AfterExtend.HitStopSeq, (int32)SeqAfterNew);
	TestTrue(TEXT("an EXTEND updates Duration (latest-snapshot-wins)"),
		FMath::IsNearlyEqual(AfterExtend.DurationSeconds, 0.2f, 1.e-4f));
	TestTrue(TEXT("an EXTEND keeps the original anchor"),
		FMath::IsNearlyEqual((float)AfterExtend.StartServerTime, (float)AnchorAfterNew, 1.e-3f));

	// A second NEW trigger after the freeze ends bumps the seq again.
	Rig.DataComp->CancelHitStop();
	const uint16 SeqAfterClear = Rig.DataComp->GetRepHitStopForTests().HitStopSeq;
	TestTrue(TEXT("a CLEAR bumps the seq so the receiver detects the end"), SeqAfterClear != SeqAfterNew);
	TestTrue(TEXT("a CLEAR publishes Duration 0"),
		Rig.DataComp->GetRepHitStopForTests().DurationSeconds <= 0.f);

	Rig.DataComp->TriggerHitStop(Victim, 0.1f);
	TestTrue(TEXT("a second NEW trigger bumps the seq again"),
		Rig.DataComp->GetRepHitStopForTests().HitStopSeq != SeqAfterClear);

	Rig.DataComp->CancelHitStop();
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// RECEIVE: a mid-freeze late join applies the CLAMPED remaining — a snapshot whose
// freeze started at server time T with duration D, observed at T+partial, freezes
// for (D - partial), not the full D.
// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusNetHitStopLateJoinClampedRemaining,
	"Paper2DPlus.Network.HitStop.LateJoinClampedRemaining",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusNetHitStopLateJoinClampedRemaining::RunTest(const FString& Parameters)
{
	AActor* Victim = NewObject<AActor>();
	Victim->CustomTimeDilation = 1.0f;

	// A receiver (simulated proxy) joins 0.2s after a 0.5s freeze began at server time 1000 — so it
	// observes the freeze at 1000.2 and should freeze for the clamped remaining 0.3s.
	FNetHS_Rig Rig = NetHS_MakeRig();
	Rig.DataComp->SetNetContextOverrideForTests(EPaper2DPlusNetContext::SimulatedProxy);
	Rig.DataComp->SetServerTimeOverrideForTests(1000.2);

	FPaper2DPlusRepHitStop& Wire = Rig.DataComp->GetRepHitStopForTests();
	Wire.HitStopSeq = 1;
	Wire.DurationSeconds = 0.5f;
	Wire.StartServerTime = 1000.0;
	Wire.Victim = Victim;
	Rig.DataComp->OnRep_HitStop();

	TestTrue(TEXT("late joiner applies a freeze"), Rig.DataComp->IsHitStopActive());
	TestEqual(TEXT("late joiner freezes the owner"), Rig.Owner->CustomTimeDilation, NetHS_FrozenDilation, 1.e-6f);
	TestEqual(TEXT("late joiner freezes the victim"), Victim->CustomTimeDilation, NetHS_FrozenDilation, 1.e-6f);

	// The clamped remaining is 0.3s: a 0.25 pump keeps it frozen, a further 0.06 (=0.31) expires it.
	FTSTicker::GetCoreTicker().Tick(0.25f);
	TestTrue(TEXT("still frozen at 0.25 of the clamped 0.3 remaining"), Rig.DataComp->IsHitStopActive());
	FTSTicker::GetCoreTicker().Tick(0.06f);
	TestFalse(TEXT("freeze expired after the clamped remaining 0.3 (not the full 0.5)"),
		Rig.DataComp->IsHitStopActive());
	TestEqual(TEXT("owner restored after the clamped freeze"), Rig.Owner->CustomTimeDilation, 1.0f, 1.e-6f);
	TestEqual(TEXT("victim restored after the clamped freeze"), Victim->CustomTimeDilation, 1.0f, 1.e-6f);

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// RECEIVE: an EXPIRED snapshot (the freeze already elapsed before this receiver saw
// it — Remaining<=0) is IGNORED (no freeze applied); and Duration=0 ENDS an active
// local freeze.
// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusNetHitStopExpiredAndClearReceive,
	"Paper2DPlus.Network.HitStop.ExpiredIgnoredAndClearEnds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusNetHitStopExpiredAndClearReceive::RunTest(const FString& Parameters)
{
	AActor* Victim = NewObject<AActor>();
	Victim->CustomTimeDilation = 1.0f;

	FNetHS_Rig Rig = NetHS_MakeRig();
	Rig.DataComp->SetNetContextOverrideForTests(EPaper2DPlusNetContext::SimulatedProxy);

	// EXPIRED snapshot: a 0.2s freeze that began at 1000, observed at 1000.5 (0.5s late) => Remaining<=0.
	Rig.DataComp->SetServerTimeOverrideForTests(1000.5);
	FPaper2DPlusRepHitStop& Wire = Rig.DataComp->GetRepHitStopForTests();
	Wire.HitStopSeq = 1;
	Wire.DurationSeconds = 0.2f;
	Wire.StartServerTime = 1000.0;
	Wire.Victim = Victim;
	Rig.DataComp->OnRep_HitStop();

	TestFalse(TEXT("an expired snapshot applies no freeze"), Rig.DataComp->IsHitStopActive());
	TestEqual(TEXT("an expired snapshot leaves the owner untouched"),
		Rig.Owner->CustomTimeDilation, 1.0f, 1.e-6f);

	// Now apply a LIVE freeze, then a Duration=0 CLEAR ends it.
	Rig.DataComp->SetServerTimeOverrideForTests(1000.0);
	Wire.HitStopSeq = 2;
	Wire.DurationSeconds = 0.5f;
	Wire.StartServerTime = 1000.0;
	Wire.Victim = Victim;
	Rig.DataComp->OnRep_HitStop();
	TestTrue(TEXT("a live snapshot applies a freeze"), Rig.DataComp->IsHitStopActive());

	UPaper2DPlusHitStopRecorder* Recorder = NewObject<UPaper2DPlusHitStopRecorder>();
	Rig.DataComp->OnHitStopEnd.AddDynamic(Recorder, &UPaper2DPlusHitStopRecorder::OnHitStopEnd);

	Wire.HitStopSeq = 3;
	Wire.DurationSeconds = 0.f; // explicit clear
	Wire.StartServerTime = -1.0;
	Wire.Victim = nullptr;
	Rig.DataComp->OnRep_HitStop();

	TestFalse(TEXT("Duration=0 ends the active local freeze"), Rig.DataComp->IsHitStopActive());
	TestEqual(TEXT("clear restored the owner"), Rig.Owner->CustomTimeDilation, 1.0f, 1.e-6f);
	TestEqual(TEXT("clear restored the victim"), Victim->CustomTimeDilation, 1.0f, 1.e-6f);
	TestEqual(TEXT("clear broadcast OnHitStopEnd exactly once"), Recorder->EndCount, 1);

	// A clear with NO active freeze is a benign no-op (no broadcast, no dilation change).
	Wire.HitStopSeq = 4;
	Wire.DurationSeconds = 0.f;
	Rig.DataComp->OnRep_HitStop();
	TestEqual(TEXT("a clear with no active freeze fires no extra End"), Recorder->EndCount, 1);

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// RECEIVE: Remaining is clamped against a GARBAGE / travel-reset clock — the freeze
// can never exceed Duration even when (ServerNow - StartServerTime) is negative
// (a seamless-travel clock reset to before the freeze began). KTD-21.
// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusNetHitStopClampAgainstGarbageClock,
	"Paper2DPlus.Network.HitStop.ClampAgainstGarbageClock",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusNetHitStopClampAgainstGarbageClock::RunTest(const FString& Parameters)
{
	AActor* Victim = NewObject<AActor>();
	Victim->CustomTimeDilation = 1.0f;

	FNetHS_Rig Rig = NetHS_MakeRig();
	Rig.DataComp->SetNetContextOverrideForTests(EPaper2DPlusNetContext::SimulatedProxy);

	// A travel-reset clock: ServerNow (5.0) is BEFORE the freeze's StartServerTime (1000.0) — the raw
	// Elapsed is hugely negative, so Duration - Elapsed would be enormous. The unconditional clamp pins
	// Remaining to at most Duration (0.1) — the freeze can never run for the implied ~995 seconds.
	Rig.DataComp->SetServerTimeOverrideForTests(5.0);
	FPaper2DPlusRepHitStop& Wire = Rig.DataComp->GetRepHitStopForTests();
	Wire.HitStopSeq = 1;
	Wire.DurationSeconds = 0.1f;
	Wire.StartServerTime = 1000.0;
	Wire.Victim = Victim;
	Rig.DataComp->OnRep_HitStop();

	TestTrue(TEXT("a garbage-clock snapshot still applies a freeze (clamped)"), Rig.DataComp->IsHitStopActive());

	// The clamp pins the freeze to at most Duration (0.1): a 0.11 pump expires it — it never holds for
	// the ~995s the negative Elapsed would otherwise imply.
	FTSTicker::GetCoreTicker().Tick(0.11f);
	TestFalse(TEXT("the freeze never exceeds Duration even off a garbage clock"),
		Rig.DataComp->IsHitStopActive());
	TestEqual(TEXT("owner restored after the clamped garbage-clock freeze"),
		Rig.Owner->CustomTimeDilation, 1.0f, 1.e-6f);

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// RECEIVE: new-vs-extend is decided by the WIRE HitStopSeq, not local freeze state.
// A distinct seq (a genuinely NEW freeze whose prior clear coalesced away on the
// wire) that lands while a stale local freeze is still draining REPLACES the
// remaining (adopts the server's CURRENT, shorter freeze); a SAME seq EXTENDs and
// never shortens. (Adversarial finding: the receiver previously conflated the two
// via bHitStopActive, so a new shorter freeze inherited the stale longer remaining.)
// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusNetHitStopSeqDistinguishesNewFromExtend,
	"Paper2DPlus.Network.HitStop.SeqDistinguishesNewFromExtend",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusNetHitStopSeqDistinguishesNewFromExtend::RunTest(const FString& Parameters)
{
	AActor* Victim = NewObject<AActor>();
	Victim->CustomTimeDilation = 1.0f;

	// ── Part A: a DISTINCT seq REPLACES while a stale local freeze is still draining ──
	{
		FNetHS_Rig Rig = NetHS_MakeRig();
		Rig.DataComp->SetNetContextOverrideForTests(EPaper2DPlusNetContext::SimulatedProxy);
		Rig.DataComp->SetServerTimeOverrideForTests(1000.0);

		// Freeze #1: seq 1, 0.5s at server 1000 → clamped remaining 0.5.
		FPaper2DPlusRepHitStop& Wire = Rig.DataComp->GetRepHitStopForTests();
		Wire.HitStopSeq = 1; Wire.DurationSeconds = 0.5f; Wire.StartServerTime = 1000.0; Wire.Victim = Victim;
		Rig.DataComp->OnRep_HitStop();
		TestTrue(TEXT("freeze #1 applied"), Rig.DataComp->IsHitStopActive());

		// Drain 0.1s of REAL time — local remaining ~0.4, still frozen.
		FTSTicker::GetCoreTicker().Tick(0.1f);
		TestTrue(TEXT("still frozen mid freeze #1"), Rig.DataComp->IsHitStopActive());

		// A genuinely NEW freeze #2 (distinct seq 2), SHORTER (0.2s) at server 1000.1 → clamped 0.2.
		// The prior freeze's clear coalesced away; the proxy must ADOPT the new 0.2 span (REPLACE), not
		// keep Max(0.4, 0.2)=0.4.
		Rig.DataComp->SetServerTimeOverrideForTests(1000.1);
		Wire.HitStopSeq = 2; Wire.DurationSeconds = 0.2f; Wire.StartServerTime = 1000.1; Wire.Victim = Victim;
		Rig.DataComp->OnRep_HitStop();
		TestTrue(TEXT("still frozen right after the replace"), Rig.DataComp->IsHitStopActive());

		// 0.21s expires the REPLACED 0.2 remaining. Under the old Max-conflation bug the freeze would
		// still hold (0.4 - 0.21 = 0.19 left), so this assertion is what catches the regression.
		FTSTicker::GetCoreTicker().Tick(0.21f);
		TestFalse(TEXT("the distinct-seq freeze adopted the NEW 0.2 span (not the stale 0.4)"),
			Rig.DataComp->IsHitStopActive());
		TestEqual(TEXT("owner restored after the replaced freeze"), Rig.Owner->CustomTimeDilation, 1.0f, 1.e-6f);
	}

	// ── Part B: a SAME seq EXTENDs and NEVER shortens ──
	{
		FNetHS_Rig Rig = NetHS_MakeRig();
		Rig.DataComp->SetNetContextOverrideForTests(EPaper2DPlusNetContext::SimulatedProxy);
		Rig.DataComp->SetServerTimeOverrideForTests(1000.0);

		// Freeze: seq 5, 0.5s at server 1000 → remaining 0.5.
		FPaper2DPlusRepHitStop& Wire = Rig.DataComp->GetRepHitStopForTests();
		Wire.HitStopSeq = 5; Wire.DurationSeconds = 0.5f; Wire.StartServerTime = 1000.0; Wire.Victim = Victim;
		Rig.DataComp->OnRep_HitStop();
		FTSTicker::GetCoreTicker().Tick(0.1f); // local remaining ~0.4
		TestTrue(TEXT("frozen mid same-seq freeze"), Rig.DataComp->IsHitStopActive());

		// Same seq 5 re-published with a SHORTER clamped remaining: at server 1000.2, dur 0.5, anchor
		// 1000.0 → Remaining = 0.5 - 0.2 = 0.3 < the local 0.4. EXTEND must KEEP the longer 0.4 (never
		// shorten) — a same-seq re-snapshot is jitter/duplicate, not a new freeze.
		Rig.DataComp->SetServerTimeOverrideForTests(1000.2);
		Wire.HitStopSeq = 5; Wire.DurationSeconds = 0.5f; Wire.StartServerTime = 1000.0; Wire.Victim = Victim;
		Rig.DataComp->OnRep_HitStop();

		// 0.35s would expire a shortened 0.3 but NOT the kept 0.4 — still frozen proves never-shorten held.
		FTSTicker::GetCoreTicker().Tick(0.35f);
		TestTrue(TEXT("same-seq re-snapshot never shortened (kept the longer 0.4 remaining)"),
			Rig.DataComp->IsHitStopActive());
		FTSTicker::GetCoreTicker().Tick(0.1f); // 0.45 total > 0.4 → now expired
		TestFalse(TEXT("the extended freeze expires after the kept span"), Rig.DataComp->IsHitStopActive());
		Rig.DataComp->CancelHitStop();
	}

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// RECEIVE: a NULL victim freezes the attacker ONLY (no warn) — KTD-24's
// attacker-only-freeze toleration through the receive path.
// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusNetHitStopNullVictimReceive,
	"Paper2DPlus.Network.HitStop.NullVictimFreezesAttackerOnly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusNetHitStopNullVictimReceive::RunTest(const FString& Parameters)
{
	AActor* Bystander = NewObject<AActor>(); // never referenced by the snapshot
	Bystander->CustomTimeDilation = 0.5f;

	FNetHS_Rig Rig = NetHS_MakeRig();
	Rig.DataComp->SetNetContextOverrideForTests(EPaper2DPlusNetContext::SimulatedProxy);
	Rig.DataComp->SetServerTimeOverrideForTests(1000.0);

	UPaper2DPlusHitStopRecorder* Recorder = NewObject<UPaper2DPlusHitStopRecorder>();
	Rig.DataComp->OnHitStopBegin.AddDynamic(Recorder, &UPaper2DPlusHitStopRecorder::OnHitStopBegin);

	// No AddExpectedError: a null victim must NOT warn (the attacker-only freeze is legitimate).
	FPaper2DPlusRepHitStop& Wire = Rig.DataComp->GetRepHitStopForTests();
	Wire.HitStopSeq = 1;
	Wire.DurationSeconds = 0.1f;
	Wire.StartServerTime = 1000.0;
	Wire.Victim = nullptr; // attacker-only freeze
	Rig.DataComp->OnRep_HitStop();

	TestTrue(TEXT("null-victim snapshot freezes the attacker"), Rig.DataComp->IsHitStopActive());
	TestEqual(TEXT("null-victim snapshot freezes the owner"),
		Rig.Owner->CustomTimeDilation, NetHS_FrozenDilation, 1.e-6f);
	TestEqual(TEXT("null-victim snapshot leaves the bystander untouched"),
		Bystander->CustomTimeDilation, 0.5f, 1.e-6f);
	TestEqual(TEXT("null-victim Begin broadcast once"), Recorder->BeginCount, 1);
	TestFalse(TEXT("null-victim Begin payload Victim is null"), Recorder->LastVictim.IsValid());

	Rig.DataComp->CancelHitStop();
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// RECEIVE: no server clock yet (GameState unreplicated) => the snapshot DEFERS one
// frame; once the clock exists the deferred retry applies the clamped freeze.
// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusNetHitStopDefersWithoutServerClock,
	"Paper2DPlus.Network.HitStop.DefersWithoutServerClock",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusNetHitStopDefersWithoutServerClock::RunTest(const FString& Parameters)
{
	AActor* Victim = NewObject<AActor>();

	FNetHS_Rig Rig = NetHS_MakeRig();
	Rig.DataComp->SetNetContextOverrideForTests(EPaper2DPlusNetContext::SimulatedProxy);
	// No SetServerTimeOverrideForTests => GetServerTimeSecondsForNet is unset (worldless => no GameState).

	FPaper2DPlusRepHitStop& Wire = Rig.DataComp->GetRepHitStopForTests();
	Wire.HitStopSeq = 1;
	Wire.DurationSeconds = 0.1f;
	Wire.StartServerTime = 1000.0;
	Wire.Victim = Victim;
	Rig.DataComp->OnRep_HitStop();

	TestFalse(TEXT("no-clock snapshot does not freeze yet"), Rig.DataComp->IsHitStopActive());
	TestTrue(TEXT("no-clock snapshot stashed for retry"), Rig.DataComp->HasPendingRepHitStopForTests());

	// The clock arrives; the deferred retry applies the clamped freeze.
	Rig.DataComp->SetServerTimeOverrideForTests(1000.0);
	Rig.DataComp->FlushPendingRepHitStopForTests();
	TestFalse(TEXT("the deferred retry drained the stash"), Rig.DataComp->HasPendingRepHitStopForTests());
	TestTrue(TEXT("the deferred retry applies the freeze once the clock exists"), Rig.DataComp->IsHitStopActive());
	TestEqual(TEXT("the deferred retry freezes the owner"),
		Rig.Owner->CustomTimeDilation, NetHS_FrozenDilation, 1.e-6f);

	Rig.DataComp->CancelHitStop();
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// CLOCK RE-ANCHOR (KTD-19): the authority freezes N real seconds; on unfreeze
// RepAnimState.StartServerTime advances by exactly that span (same Sequence — NEVER
// bumped) so a client recomputing after the re-anchor lands within tolerance of the
// server's actual frame. The drift snap is suppressed during the freeze (IsHitStopActive).
// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusNetHitStopClockReAnchor,
	"Paper2DPlus.Network.HitStop.ClockReAnchorOnUnfreeze",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusNetHitStopClockReAnchor::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	UPaperFlipbook* Jab = NetHS_AddMove(Asset, TEXT("Jab"), 8);

	AActor* Victim = NewObject<AActor>();

	FNetHS_Rig Rig = NetHS_MakeRig();
	Rig.DataComp->CharacterProfile = Asset;
	Rig.FBComp->SetFlipbook(Jab);
	Rig.DataComp->SetNetContextOverrideForTests(EPaper2DPlusNetContext::Authority);
	Rig.DataComp->SetServerTimeOverrideForTests(1000.0);

	// Publish a baseline profile-move anim state (HandleFlipbookChanged publishes on authority).
	Rig.DataComp->HandleFlipbookChanged(Jab);
	const FPaper2DPlusRepAnimState& Anim = Rig.DataComp->GetRepAnimStateForTests();
	TestTrue(TEXT("baseline anim state is a profile move"), Anim.bIsProfileMove);
	TestTrue(TEXT("baseline anim state has a real anchor"), Anim.StartServerTime >= 0.0);
	const uint16 SeqBeforeFreeze = Anim.Sequence;
	const double AnchorBeforeFreeze = Anim.StartServerTime;

	// Freeze for 0.3 real seconds, pumping the real-time ticker.
	Rig.DataComp->TriggerHitStop(Victim, 0.3f);
	TestTrue(TEXT("authority frozen"), Rig.DataComp->IsHitStopActive());

	// Mid-freeze: a HandleFrameChanged would normally run the drift corrector — but IsHitStopActive
	// suppresses it, so the anchor is NOT re-snapped during the freeze (the move clock legitimately
	// pauses). Drive a frame change to exercise that suppression.
	Rig.DataComp->HandleFrameChanged(2);
	TestTrue(TEXT("anchor unchanged DURING the freeze (drift snap suppressed by IsHitStopActive)"),
		FMath::IsNearlyEqual((float)Rig.DataComp->GetRepAnimStateForTests().StartServerTime,
			(float)AnchorBeforeFreeze, 1.e-3f));

	// Pump 0.3s of REAL time (in two ticks) to expire the freeze.
	FTSTicker::GetCoreTicker().Tick(0.2f);
	FTSTicker::GetCoreTicker().Tick(0.12f); // total 0.32 > 0.3
	TestFalse(TEXT("freeze expired after ~0.3 real seconds"), Rig.DataComp->IsHitStopActive());

	const FPaper2DPlusRepAnimState& AfterUnfreeze = Rig.DataComp->GetRepAnimStateForTests();
	const float FrozenSpan = Rig.DataComp->GetActualFrozenSecondsForTests();
	TestTrue(TEXT("the actual frozen span is ~0.32 real seconds"),
		FMath::IsNearlyEqual(FrozenSpan, 0.32f, 0.02f));
	TestEqual(TEXT("the Sequence is NOT bumped by the re-anchor (same-Sequence republish)"),
		(int32)AfterUnfreeze.Sequence, (int32)SeqBeforeFreeze);
	TestTrue(TEXT("StartServerTime advanced by the actual frozen span (KTD-19 re-anchor)"),
		FMath::IsNearlyEqual((float)AfterUnfreeze.StartServerTime,
			(float)(AnchorBeforeFreeze + FrozenSpan), 1.e-3f));

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// CLOCK RE-ANCHOR — model integrity: a proxy that recomputes the playback position
// AFTER the re-anchor lands within tolerance of where the server's flipbook actually
// is. Modeled by recomputing the server formula position before vs after the re-anchor:
// the re-anchor exactly cancels the frozen span so the derived position is unchanged.
// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusNetHitStopReAnchorModelIntegrity,
	"Paper2DPlus.Network.HitStop.ReAnchorModelIntegrity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusNetHitStopReAnchorModelIntegrity::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	UPaperFlipbook* Jab = NetHS_AddMove(Asset, TEXT("Jab"), 8);

	AActor* Victim = NewObject<AActor>();

	FNetHS_Rig Rig = NetHS_MakeRig();
	Rig.DataComp->CharacterProfile = Asset;
	Rig.FBComp->SetFlipbook(Jab);
	Rig.DataComp->SetNetContextOverrideForTests(EPaper2DPlusNetContext::Authority);
	Rig.DataComp->SetServerTimeOverrideForTests(1000.0);

	// Server playback at position 0.2 (frame 2); publish the anchor from there.
	Rig.FBComp->SetPlaybackPosition(0.2f, /*bFireEvents=*/false);
	Rig.DataComp->HandleFlipbookChanged(Jab);
	const double AnchorBefore = Rig.DataComp->GetRepAnimStateForTests().StartServerTime;

	// The server's flipbook (the move clock) is frozen by CustomTimeDilation, so its playback position
	// does NOT advance during the freeze. A naive proxy clock (server wall time) DOES advance. The
	// re-anchor is what keeps the two in sync: derived = ServerNow - anchor. Sample the derived position
	// at the unfrozen wall clock (1000 + frozenSpan) using the PRE-anchor vs POST-anchor anchors.
	Rig.DataComp->TriggerHitStop(Victim, 0.3f);
	FTSTicker::GetCoreTicker().Tick(0.2f);
	FTSTicker::GetCoreTicker().Tick(0.12f);
	TestFalse(TEXT("freeze expired"), Rig.DataComp->IsHitStopActive());

	const float FrozenSpan = Rig.DataComp->GetActualFrozenSecondsForTests();
	const double AnchorAfter = Rig.DataComp->GetRepAnimStateForTests().StartServerTime;

	// Server wall clock at unfreeze = 1000 + FrozenSpan (PlayRate 1 model). Derived playback position:
	//   pre-anchor (the BUG): ServerNowAtUnfreeze - AnchorBefore = 0.2 + FrozenSpan  (rubber-banded ahead)
	//   post-anchor (correct): ServerNowAtUnfreeze - AnchorAfter = 0.2               (matches the frozen
	//                                                                                 server flipbook)
	const double ServerNowAtUnfreeze = 1000.0 + static_cast<double>(FrozenSpan);
	const double DerivedPre = ServerNowAtUnfreeze - AnchorBefore;
	const double DerivedPost = ServerNowAtUnfreeze - AnchorAfter;

	TestTrue(TEXT("WITHOUT the re-anchor the derived position rubber-bands ahead by the frozen span"),
		FMath::IsNearlyEqual((float)DerivedPre, 0.2f + FrozenSpan, 1.e-3f));
	TestTrue(TEXT("WITH the re-anchor the derived position matches the server's frozen flipbook (0.2)"),
		FMath::IsNearlyEqual((float)DerivedPost, 0.2f, 1.e-3f));

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// EndPlay-teardown during an active freeze publishes the CLEAR (Duration=0) —
// travel/death never strands a frozen client (KTD-21). EndPlay's FIRST act is
// CancelHitStop() -> EndHitStopInternal, which publishes Duration=0 on the bumped
// seq; we drive CancelHitStop directly because the engine's UActorComponent::EndPlay
// does check(bHasBegunPlay), which a worldless rig (never BeginPlays) can't satisfy.
// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusNetHitStopEndPlayPublishesClear,
	"Paper2DPlus.Network.HitStop.EndPlayPublishesClear",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusNetHitStopEndPlayPublishesClear::RunTest(const FString& Parameters)
{
	AActor* Victim = NewObject<AActor>();

	FNetHS_Rig Rig = NetHS_MakeRig();
	Rig.DataComp->SetNetContextOverrideForTests(EPaper2DPlusNetContext::Authority);
	Rig.DataComp->SetServerTimeOverrideForTests(1000.0);

	Rig.DataComp->TriggerHitStop(Victim, 0.5f);
	TestTrue(TEXT("authority frozen"), Rig.DataComp->IsHitStopActive());
	TestTrue(TEXT("a live freeze is on the wire"),
		Rig.DataComp->GetRepHitStopForTests().DurationSeconds > 0.f);
	const uint16 SeqWhileFrozen = Rig.DataComp->GetRepHitStopForTests().HitStopSeq;

	// EndPlay -> CancelHitStop -> EndHitStopInternal -> publish Duration=0 on a bumped seq. Drive the
	// CancelHitStop funnel directly (the engine EndPlay's check(bHasBegunPlay) can't run worldless).
	Rig.DataComp->CancelHitStop();

	const FPaper2DPlusRepHitStop& Cleared = Rig.DataComp->GetRepHitStopForTests();
	TestFalse(TEXT("EndPlay teardown ended the local freeze"), Rig.DataComp->IsHitStopActive());
	TestTrue(TEXT("EndPlay teardown published Duration 0 (no stranded frozen clients)"), Cleared.DurationSeconds <= 0.f);
	TestTrue(TEXT("EndPlay teardown clear bumped the seq so receivers detect the end"), Cleared.HitStopSeq != SeqWhileFrozen);
	TestEqual(TEXT("EndPlay teardown restored the owner dilation"), Rig.Owner->CustomTimeDilation, 1.0f, 1.e-6f);

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// VICTIM-SIDE PUBLISH (KTD-24): when the victim carries its OWN profile component
// with bReplicateHitStop, the authority ALSO publishes a Victim=self snapshot on the
// victim's component (and runs its own local freeze) — removing the cross-actor
// pointer relevancy dependency.
// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusNetHitStopVictimSidePublish,
	"Paper2DPlus.Network.HitStop.VictimSidePublish",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusNetHitStopVictimSidePublish::RunTest(const FString& Parameters)
{
	// Attacker rig + a victim rig (its OWN profile component, authority context, bReplicateHitStop on).
	FNetHS_Rig Attacker = NetHS_MakeRig();
	Attacker.DataComp->SetNetContextOverrideForTests(EPaper2DPlusNetContext::Authority);
	Attacker.DataComp->SetServerTimeOverrideForTests(1000.0);

	FNetHS_Rig VictimRig = NetHS_MakeRig();
	VictimRig.DataComp->SetNetContextOverrideForTests(EPaper2DPlusNetContext::Authority);
	VictimRig.DataComp->SetServerTimeOverrideForTests(1000.0);
	VictimRig.DataComp->bReplicateHitStop = true;

	Attacker.DataComp->TriggerHitStop(VictimRig.Owner, 0.2f);

	// The attacker's snapshot carries the victim actor.
	const FPaper2DPlusRepHitStop& AttackerSnap = Attacker.DataComp->GetRepHitStopForTests();
	TestTrue(TEXT("attacker snapshot published"), AttackerSnap.HitStopSeq != 0);
	TestTrue(TEXT("attacker snapshot carries the victim actor"), AttackerSnap.Victim == VictimRig.Owner);

	// The victim component ALSO published — Victim=self (its own owner) — and runs its own local freeze.
	const FPaper2DPlusRepHitStop& VictimSnap = VictimRig.DataComp->GetRepHitStopForTests();
	TestTrue(TEXT("victim component published its own snapshot"), VictimSnap.HitStopSeq != 0);
	TestTrue(TEXT("victim snapshot is Victim=self"), VictimSnap.Victim == VictimRig.Owner);
	TestTrue(TEXT("victim component runs its own local freeze"), VictimRig.DataComp->IsHitStopActive());
	TestEqual(TEXT("the victim actor is frozen (registry sentinel)"),
		VictimRig.Owner->CustomTimeDilation, NetHS_FrozenDilation, 1.e-6f);

	// Cleanup: end both freezes; the victim actor was refcounted twice (attacker + victim comp) and
	// restores only when both ends complete.
	Attacker.DataComp->CancelHitStop();
	TestEqual(TEXT("victim actor STILL frozen after only the attacker ends (refcount holds)"),
		VictimRig.Owner->CustomTimeDilation, NetHS_FrozenDilation, 1.e-6f);
	VictimRig.DataComp->CancelHitStop();
	TestEqual(TEXT("victim actor restored after the LAST freezer ends"),
		VictimRig.Owner->CustomTimeDilation, 1.0f, 1.e-6f);

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// REGRESSION: single-player hit-stop is byte-identical with replication OFF — no
// publish, the local freeze/restore behaves exactly as TASK-76 PR4.
// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusNetHitStopSinglePlayerByteIdentical,
	"Paper2DPlus.Network.HitStop.SinglePlayerByteIdentical",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusNetHitStopSinglePlayerByteIdentical::RunTest(const FString& Parameters)
{
	// Replication OFF, no net context override => worldless Standalone (the gate passes).
	FNetHS_Rig Rig = NetHS_MakeRig(/*bEnableReplication=*/false);
	Rig.Owner->CustomTimeDilation = 1.0f;
	AActor* Victim = NewObject<AActor>();
	Victim->CustomTimeDilation = 0.5f;

	UPaper2DPlusHitStopRecorder* Recorder = NewObject<UPaper2DPlusHitStopRecorder>();
	Rig.DataComp->OnHitStopBegin.AddDynamic(Recorder, &UPaper2DPlusHitStopRecorder::OnHitStopBegin);
	Rig.DataComp->OnHitStopEnd.AddDynamic(Recorder, &UPaper2DPlusHitStopRecorder::OnHitStopEnd);

	Rig.DataComp->TriggerHitStop(Victim, 0.1f);

	// Local freeze identical to PR4 (sentinel applied, Begin once, distinct priors captured).
	TestEqual(TEXT("Standalone freezes the attacker"), Rig.Owner->CustomTimeDilation, NetHS_FrozenDilation, 1.e-6f);
	TestEqual(TEXT("Standalone freezes the victim"), Victim->CustomTimeDilation, NetHS_FrozenDilation, 1.e-6f);
	TestEqual(TEXT("Standalone Begin once"), Recorder->BeginCount, 1);

	// NO publish in Standalone (HitStopSeq stays the never-published sentinel).
	TestEqual(TEXT("Standalone publishes nothing (HitStopSeq stays 0)"),
		(int32)Rig.DataComp->GetRepHitStopForTests().HitStopSeq, 0);
	TestTrue(TEXT("Standalone re-anchor amount tracks (but never publishes)"),
		Rig.DataComp->GetActualFrozenSecondsForTests() >= 0.f);

	// Expiry restores each actor's exact prior and broadcasts End once.
	FTSTicker::GetCoreTicker().Tick(0.11f);
	TestFalse(TEXT("Standalone inactive after expiry"), Rig.DataComp->IsHitStopActive());
	TestEqual(TEXT("Standalone restored the attacker to 1.0"), Rig.Owner->CustomTimeDilation, 1.0f, 1.e-6f);
	TestEqual(TEXT("Standalone restored the victim to its prior 0.5"), Victim->CustomTimeDilation, 0.5f, 1.e-6f);
	TestEqual(TEXT("Standalone End once"), Recorder->EndCount, 1);
	TestEqual(TEXT("Standalone STILL publishes nothing after expiry"),
		(int32)Rig.DataComp->GetRepHitStopForTests().HitStopSeq, 0);

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Codex F198b — when a distinct-seq snapshot REPLACES an active proxy freeze that
// targeted a DIFFERENT victim, the STALE victim must be released (the authority
// already cleared it). The old replace path only reset the remaining time, leaving
// the previous victim CustomTimeDilation-frozen until the new freeze ended.
// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusNetHitStopReplaceReleasesStaleVictim,
	"Paper2DPlus.Network.HitStop.ReplaceReleasesStaleVictim",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusNetHitStopReplaceReleasesStaleVictim::RunTest(const FString& Parameters)
{
	AActor* VictimA = NewObject<AActor>(); VictimA->CustomTimeDilation = 1.0f;
	AActor* VictimB = NewObject<AActor>(); VictimB->CustomTimeDilation = 1.0f;

	FNetHS_Rig Rig = NetHS_MakeRig();
	Rig.DataComp->SetNetContextOverrideForTests(EPaper2DPlusNetContext::SimulatedProxy);
	Rig.DataComp->SetServerTimeOverrideForTests(1000.0);

	// Freeze #1 (seq 1) targets victim A.
	FPaper2DPlusRepHitStop& Wire = Rig.DataComp->GetRepHitStopForTests();
	Wire.HitStopSeq = 1; Wire.DurationSeconds = 0.5f; Wire.StartServerTime = 1000.0; Wire.Victim = VictimA;
	Rig.DataComp->OnRep_HitStop();
	TestEqual(TEXT("victim A frozen by freeze #1"), VictimA->CustomTimeDilation, NetHS_FrozenDilation, 1.e-6f);

	FTSTicker::GetCoreTicker().Tick(0.1f); // local freeze still draining
	TestTrue(TEXT("still frozen mid freeze #1"), Rig.DataComp->IsHitStopActive());

	// A genuinely NEW freeze (distinct seq 2) targets a DIFFERENT victim B while #1 is still draining — the
	// REPLACE path. Victim A is no longer targeted and must be released; B must be frozen.
	Rig.DataComp->SetServerTimeOverrideForTests(1000.1);
	Wire.HitStopSeq = 2; Wire.DurationSeconds = 0.3f; Wire.StartServerTime = 1000.1; Wire.Victim = VictimB;
	Rig.DataComp->OnRep_HitStop();

	TestEqual(TEXT("victim B frozen by the replacing freeze"), VictimB->CustomTimeDilation, NetHS_FrozenDilation, 1.e-6f);
	TestEqual(TEXT("stale victim A released on replace (F198b)"), VictimA->CustomTimeDilation, 1.0f, 1.e-6f);

	Rig.DataComp->CancelHitStop();
	return true;
}

#endif // !UE_BUILD_SHIPPING

#endif // WITH_EDITOR
