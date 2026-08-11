// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#if WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "Paper2DPlusNetTypes.h"
#include "Paper2DPlusMoveTransition.h"
#include "Paper2DPlusFrameCurve.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Paper2DPlusCharacterProfileComponent.h"
#include "Paper2DPlusHitboxSubsystem.h"
#include "Paper2DPlusTypes.h"
#include "PaperFlipbook.h"
#include "PaperFlipbookComponent.h"
#include "PaperSprite.h"
#include "GameFramework/Actor.h"

/** TASK-57 U4 server-owned-combat tests (worldless, driven through the U1/U2/U4 test seams:
 *  SetNetContextOverrideForTests / SetServerTimeOverrideForTests / SetOverlapQueryOverrideForTests /
 *  SetFrameSpanForTests / GetProcessedHitsForTests / GetRepAnimStateForTests /
 *  SetMoveInstanceCounterForTests). Pins: the proxy-context hit-adjudication gate (warn + false +
 *  registers nothing), the victim-membership contract (overlap for a DIFFERENT victim => false;
 *  matched victim => Out fields equal the matched overlap, never caller input), the once-per-
 *  (attacker,victim,move-instance,window) dedup (duplicate => bDuplicate; multi-hit via the "HitWindow"
 *  curve; two victims one window; ResetHitDedupWindow / move-change reopen), span-
 *  based frame validation (a 1-frame active window crossed inside one HandleFrameChanged(prev=3->new=6)
 *  validates; outside the span => false), the Standalone full path, and GetCurrentHitWindowIndex
 *  (absent => 0; span-aware steps).
 *  Helpers are NetCombat_-prefixed per the unity-build file-unique-name rule. */

namespace
{
	/** Build a flipbook with NumFrames key frames (FPS 10, FrameRun 1 — frame i lives at i*0.1s). */
	UPaperFlipbook* NetCombat_MakeFlipbook(UObject* Owner, int32 NumFrames)
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
	UPaperFlipbook* NetCombat_AddMove(UPaper2DPlusCharacterProfileAsset* Asset, const FString& MoveName, int32 NumFrames)
	{
		FFlipbookProfileEntry Anim;
		Anim.Identity.FlipbookName = MoveName;
		UPaperFlipbook* FB = NetCombat_MakeFlipbook(Asset, NumFrames);
		Anim.Identity.Flipbook = FB;
		Anim.CombatData.Frames.SetNum(NumFrames);
		Anim.CombatData.FrameExtractionInfo.SetNum(NumFrames);
		Asset->Flipbooks.Add(Anim);
		return FB;
	}

	/** Mark an Attack hitbox (Damage/Knockback authored) on a frame of the entry at EntryIndex. */
	void NetCombat_SetAttackFrame(UPaper2DPlusCharacterProfileAsset* Asset, int32 EntryIndex, int32 Frame, int32 Damage = 0, int32 Knockback = 0)
	{
		FHitboxData Box;
		Box.Type = EHitboxType::Attack;
		Box.Width = 16;
		Box.Height = 16;
		Box.Damage = Damage;
		Box.Knockback = Knockback;
		Asset->Flipbooks[EntryIndex].CombatData.Frames[Frame].Hitboxes.Add(Box);
	}

	/** Mark a Hurtbox (defender geometry) on a frame of the entry at EntryIndex (TASK-91). */
	void NetCombat_SetHurtboxFrame(UPaper2DPlusCharacterProfileAsset* Asset, int32 EntryIndex, int32 Frame)
	{
		FHitboxData Box;
		Box.Type = EHitboxType::Hurtbox;
		Box.Width = 16;
		Box.Height = 16;
		Asset->Flipbooks[EntryIndex].CombatData.Frames[Frame].Hitboxes.Add(Box);
	}

	/** Author a "HitWindow" Constant/step curve on the entry at EntryIndex. */
	void NetCombat_AddHitWindowCurve(UPaper2DPlusCharacterProfileAsset* Asset, int32 EntryIndex, const TArray<TPair<int32, float>>& Keys)
	{
		FPaper2DPlusFrameCurve& Curve = Asset->Flipbooks[EntryIndex].CurveData.Curves.Add(TEXT("HitWindow"));
		Curve.Mode = EPaper2DPlusCurveInterp::Constant;
		for (const TPair<int32, float>& K : Keys)
		{
			Curve.SetKeyValue(K.Key, K.Value);
		}
	}

	struct FNetCombat_Rig
	{
		AActor* Owner = nullptr;
		UPaperFlipbookComponent* FBComp = nullptr;
		UPaper2DPlusCharacterProfileComponent* DataComp = nullptr;
	};

	FNetCombat_Rig NetCombat_MakeRig(bool bEnableReplication = true)
	{
		FNetCombat_Rig Rig;
		Rig.Owner = NewObject<AActor>();
		Rig.FBComp = NewObject<UPaperFlipbookComponent>(Rig.Owner);
		Rig.DataComp = NewObject<UPaper2DPlusCharacterProfileComponent>(Rig.Owner);
		Rig.Owner->AddOwnedComponent(Rig.FBComp);
		Rig.Owner->AddOwnedComponent(Rig.DataComp);
		Rig.DataComp->FlipbookComponent = Rig.FBComp;
		Rig.DataComp->bEnableReplication = bEnableReplication;
		return Rig;
	}

	/** Install an overlap-query override that returns ONE result resolving to Victim with the given
	 *  authored damage/knockback (the server's data). bHit defaults to true (the subsystem's invariant);
	 *  pass false to exercise the validator's explicit bHit gate (a broadphase/miss candidate). */
	void NetCombat_InjectVictimOverlap(FNetCombat_Rig& Rig, AActor* Victim, int32 Damage, int32 Knockback, bool bHit = true)
	{
		Rig.DataComp->SetOverlapQueryOverrideForTests(
			[Victim, Damage, Knockback, bHit](TArray<FHitboxCollisionResult>& Out)
			{
				FHitboxCollisionResult R;
				R.bHit = bHit;
				R.DefenderActor = Victim;
				R.Damage = Damage;
				R.Knockback = Knockback;
				Out.Add(R);
			});
	}
}

// Everything below drives the !UE_BUILD_SHIPPING test seams.
#if !UE_BUILD_SHIPPING

// ─────────────────────────────────────────────────────────────────────────────
// Codex Hitbox.ServerOnly: a proxy-context ValidateAndRegisterHit warns + returns
// false + registers NOTHING; authority validates and registers.
// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusNetCombatProxyGate,
	"Paper2DPlus.Network.Combat.ProxyGateRejects",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusNetCombatProxyGate::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	UPaperFlipbook* Jab = NetCombat_AddMove(Asset, TEXT("Jab"), 8);
	NetCombat_SetAttackFrame(Asset, 0, 4, /*Damage=*/10, /*Knockback=*/5);

	AActor* Victim = NewObject<AActor>();

	// The warn-once gate fires once per COMPONENT (the shared bWarnedHitAdjudicationOnNonAuthority latch
	// is set by ValidateAndRegisterHit and silences the later RegisterHitOnce on the same rig). The loop
	// below builds TWO proxy rigs, so the gate fires EXACTLY twice. UE 5.7 AddExpectedError defaults to
	// Occurrences=1 = "expect exactly one match" (NOT "any count" — see
	// docs/solutions/ue-worldless-automation-test-patterns.md), so the count MUST be the real 2 or the
	// test fails on the unmatched extra.
	AddExpectedError(TEXT("non-authority context"), EAutomationExpectedErrorFlags::Contains, /*Occurrences=*/2);

	// AutonomousProxy + SimulatedProxy both reject and register nothing.
	for (EPaper2DPlusNetContext ProxyCtx : { EPaper2DPlusNetContext::AutonomousProxy, EPaper2DPlusNetContext::SimulatedProxy })
	{
		FNetCombat_Rig Rig = NetCombat_MakeRig();
		Rig.DataComp->CharacterProfile = Asset;
		Rig.FBComp->SetFlipbook(Jab);
		Rig.DataComp->SetNetContextOverrideForTests(ProxyCtx);
		Rig.DataComp->SetFrameSpanForTests(3, 4); // an attack frame is in-span
		NetCombat_InjectVictimOverlap(Rig, Victim, 10, 5);

		FPaper2DPlusHitValidationResult Out;
		const bool bValid = Rig.DataComp->ValidateAndRegisterHit(Victim, Out);
		TestFalse(TEXT("proxy ValidateAndRegisterHit returns false"), bValid);
		TestFalse(TEXT("proxy result not valid"), Out.bValidHit);
		TestEqual(TEXT("proxy registers nothing"), Rig.DataComp->GetProcessedHitsForTests().Num(), 0);

		const bool bLight = Rig.DataComp->RegisterHitOnce(Victim);
		TestFalse(TEXT("proxy RegisterHitOnce returns false"), bLight);
		TestEqual(TEXT("proxy RegisterHitOnce registers nothing"), Rig.DataComp->GetProcessedHitsForTests().Num(), 0);
	}

	// Authority validates.
	{
		FNetCombat_Rig Rig = NetCombat_MakeRig();
		Rig.DataComp->CharacterProfile = Asset;
		Rig.FBComp->SetFlipbook(Jab);
		Rig.DataComp->SetNetContextOverrideForTests(EPaper2DPlusNetContext::Authority);
		Rig.DataComp->SetServerTimeOverrideForTests(1000.0);
		Rig.DataComp->SetMoveInstanceCounterForTests(1);
		Rig.DataComp->SetFrameSpanForTests(3, 4);
		NetCombat_InjectVictimOverlap(Rig, Victim, 10, 5);

		FPaper2DPlusHitValidationResult Out;
		const bool bValid = Rig.DataComp->ValidateAndRegisterHit(Victim, Out);
		TestTrue(TEXT("authority ValidateAndRegisterHit validates"), bValid);
		TestTrue(TEXT("authority result valid"), Out.bValidHit);
		TestEqual(TEXT("authority registered one dedup entry"), Rig.DataComp->GetProcessedHitsForTests().Num(), 1);
	}

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Victim-membership: an overlap for a DIFFERENT victim => false (the bypass pin);
// a matched victim => Out fields equal the MATCHED overlap (server data), never
// the caller's input.
// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusNetCombatVictimMembership,
	"Paper2DPlus.Network.Combat.VictimMembership",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusNetCombatVictimMembership::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	UPaperFlipbook* Jab = NetCombat_AddMove(Asset, TEXT("Jab"), 8);
	NetCombat_SetAttackFrame(Asset, 0, 4, /*Damage=*/10, /*Knockback=*/5);

	AActor* RealVictim = NewObject<AActor>();
	AActor* OtherVictim = NewObject<AActor>();

	FNetCombat_Rig Rig = NetCombat_MakeRig();
	Rig.DataComp->CharacterProfile = Asset;
	Rig.FBComp->SetFlipbook(Jab);
	Rig.DataComp->SetNetContextOverrideForTests(EPaper2DPlusNetContext::Authority);
	Rig.DataComp->SetServerTimeOverrideForTests(1000.0);
	Rig.DataComp->SetMoveInstanceCounterForTests(1);
	Rig.DataComp->SetFrameSpanForTests(3, 4);

	// The server overlap exists, but for OtherVictim — querying for RealVictim must NOT validate.
	NetCombat_InjectVictimOverlap(Rig, OtherVictim, 10, 5);

	FPaper2DPlusHitValidationResult MissOut;
	const bool bMiss = Rig.DataComp->ValidateAndRegisterHit(RealVictim, MissOut);
	TestFalse(TEXT("overlap for a different victim does not validate the queried victim"), bMiss);
	TestFalse(TEXT("miss result is not valid"), MissOut.bValidHit);
	TestEqual(TEXT("a miss registers nothing"), Rig.DataComp->GetProcessedHitsForTests().Num(), 0);

	// Now the matched victim — Out fields come from the matched overlap (Damage 22/Knockback 7), NOT
	// from any caller input (the caller passes no damage; the result reads the SERVER's authored data).
	NetCombat_InjectVictimOverlap(Rig, RealVictim, 22, 7);
	FPaper2DPlusHitValidationResult HitOut;
	const bool bHit = Rig.DataComp->ValidateAndRegisterHit(RealVictim, HitOut);
	TestTrue(TEXT("matched victim validates"), bHit);
	TestTrue(TEXT("matched result valid"), HitOut.bValidHit);
	TestTrue(TEXT("Out.Victim is the matched overlap's actor"), HitOut.Victim == RealVictim);
	TestEqual(TEXT("Out.Damage from the matched overlap"), HitOut.Damage, 22.f);
	TestEqual(TEXT("Out.Knockback from the matched overlap"), HitOut.Knockback, 7.f);
	TestEqual(TEXT("Out.MoveName is the server move"), HitOut.MoveName, FString(TEXT("Jab")));
	TestEqual(TEXT("Out.FrameIndex is the in-span attack frame"), HitOut.FrameIndex, 4);
	TestEqual(TEXT("Out.MoveInstanceSeq is the attacker instance"), HitOut.MoveInstanceSeq, 1);

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Codex Combat.DuplicateHitGuard: same window+victim second call => bDuplicate;
// two victims in one window => both register; ResetHitDedupWindow / move change
// reopen the window (the removed transition driver's self-loop reopen is now
// subsumed by the flipbook-change reopen).
// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusNetCombatDuplicateGuard,
	"Paper2DPlus.Network.Combat.DuplicateHitGuard",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusNetCombatDuplicateGuard::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	UPaperFlipbook* Jab = NetCombat_AddMove(Asset, TEXT("Jab"), 8);
	UPaperFlipbook* Jab2 = NetCombat_AddMove(Asset, TEXT("Jab2"), 8);
	NetCombat_SetAttackFrame(Asset, 0, 4, 10, 5);
	NetCombat_SetAttackFrame(Asset, 1, 2, 12, 6);

	AActor* VictimA = NewObject<AActor>();
	AActor* VictimB = NewObject<AActor>();

	FNetCombat_Rig Rig = NetCombat_MakeRig();
	Rig.DataComp->CharacterProfile = Asset;
	Rig.FBComp->SetFlipbook(Jab);
	Rig.DataComp->SetNetContextOverrideForTests(EPaper2DPlusNetContext::Authority);
	Rig.DataComp->SetServerTimeOverrideForTests(1000.0);
	Rig.DataComp->SetMoveInstanceCounterForTests(1);
	Rig.DataComp->SetFrameSpanForTests(3, 4);
	NetCombat_InjectVictimOverlap(Rig, VictimA, 10, 5);

	// First hit on VictimA registers; second on the same (victim, window, instance) is a duplicate.
	FPaper2DPlusHitValidationResult First;
	TestTrue(TEXT("first hit on VictimA validates"), Rig.DataComp->ValidateAndRegisterHit(VictimA, First));
	TestFalse(TEXT("first hit is not a duplicate"), First.bDuplicate);

	FPaper2DPlusHitValidationResult Second;
	const bool bSecond = Rig.DataComp->ValidateAndRegisterHit(VictimA, Second);
	TestFalse(TEXT("duplicate hit returns false"), bSecond);
	TestTrue(TEXT("duplicate flagged"), Second.bDuplicate);
	TestEqual(TEXT("dedup set still has exactly one entry"), Rig.DataComp->GetProcessedHitsForTests().Num(), 1);

	// Two victims in one window: VictimB still registers (different victim key).
	NetCombat_InjectVictimOverlap(Rig, VictimB, 10, 5);
	FPaper2DPlusHitValidationResult BHit;
	TestTrue(TEXT("a second victim in the same window registers"), Rig.DataComp->ValidateAndRegisterHit(VictimB, BHit));
	TestEqual(TEXT("two victims => two dedup entries"), Rig.DataComp->GetProcessedHitsForTests().Num(), 2);

	// ResetHitDedupWindow reopens: VictimA can be hit again.
	Rig.DataComp->ResetHitDedupWindow();
	TestEqual(TEXT("ResetHitDedupWindow clears the ledger"), Rig.DataComp->GetProcessedHitsForTests().Num(), 0);
	NetCombat_InjectVictimOverlap(Rig, VictimA, 10, 5);
	FPaper2DPlusHitValidationResult AfterReset;
	TestTrue(TEXT("VictimA re-hittable after ResetHitDedupWindow"), Rig.DataComp->ValidateAndRegisterHit(VictimA, AfterReset));
	TestFalse(TEXT("re-hit after reset is not a duplicate"), AfterReset.bDuplicate);

	// Move change (flipbook swap) reopens: a fresh move instance => the dedup window is fresh.
	Rig.FBComp->SetFlipbook(Jab2);
	Rig.DataComp->HandleFlipbookChanged(Jab2); // bumps MoveInstanceCounter + resets ProcessedHits
	Rig.DataComp->SetFrameSpanForTests(1, 2);  // Jab2's attack frame
	NetCombat_InjectVictimOverlap(Rig, VictimA, 12, 6);
	FPaper2DPlusHitValidationResult AfterMove;
	TestTrue(TEXT("VictimA hittable on the new move"), Rig.DataComp->ValidateAndRegisterHit(VictimA, AfterMove));
	TestFalse(TEXT("new move hit is not a duplicate"), AfterMove.bDuplicate);
	TestEqual(TEXT("new move's hit reads the new move's authored damage"), AfterMove.Damage, 12.f);

	// Move-change reopen (the U2 pin extended to dedup): the removed transition driver formerly drove a
	// same-flipbook self-loop reopen, but the dedup window now reopens purely on a flipbook change
	// (OnFlipbookChanged bumps MoveInstanceCounter + resets ProcessedHits). Re-establish Jab, land a hit,
	// then swap moves and confirm the window reopened on the fresh instance.
	Rig.FBComp->SetFlipbook(Jab);
	Rig.DataComp->HandleFlipbookChanged(Jab);
	Rig.DataComp->SetFrameSpanForTests(3, 4);
	NetCombat_InjectVictimOverlap(Rig, VictimA, 10, 5);
	FPaper2DPlusHitValidationResult PreReopen;
	TestTrue(TEXT("Jab hit before the reopen"), Rig.DataComp->ValidateAndRegisterHit(VictimA, PreReopen));
	const uint32 InstanceBeforeReopen = Rig.DataComp->GetMoveInstanceCounterForTests();

	Rig.FBComp->SetFlipbook(Jab2);
	Rig.DataComp->HandleFlipbookChanged(Jab2); // bumps MoveInstanceCounter + resets ProcessedHits
	TestTrue(TEXT("move change bumped the move instance"),
		Rig.DataComp->GetMoveInstanceCounterForTests() > InstanceBeforeReopen);
	TestEqual(TEXT("move change reset the dedup ledger"), Rig.DataComp->GetProcessedHitsForTests().Num(), 0);
	Rig.DataComp->SetFrameSpanForTests(1, 2);  // Jab2's attack frame
	NetCombat_InjectVictimOverlap(Rig, VictimA, 12, 6);
	FPaper2DPlusHitValidationResult PostReopen;
	TestTrue(TEXT("VictimA re-hittable after the move-change reopen"), Rig.DataComp->ValidateAndRegisterHit(VictimA, PostReopen));
	TestFalse(TEXT("post-reopen hit is not a duplicate"), PostReopen.bDuplicate);

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Multi-hit via the "HitWindow" curve: windows 0/1/2 across frames each register
// one hit on the same victim within the same move instance.
// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusNetCombatMultiHitWindows,
	"Paper2DPlus.Network.Combat.MultiHitWindows",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusNetCombatMultiHitWindows::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	UPaperFlipbook* Multi = NetCombat_AddMove(Asset, TEXT("Multi"), 9);
	NetCombat_SetAttackFrame(Asset, 0, 2, 10, 5);
	NetCombat_SetAttackFrame(Asset, 0, 5, 10, 5);
	NetCombat_SetAttackFrame(Asset, 0, 8, 10, 5);
	// HitWindow: (2,0)(5,1)(8,2) Constant — frames 2..4 = window 0, 5..7 = window 1, 8+ = window 2.
	NetCombat_AddHitWindowCurve(Asset, 0, { TPair<int32, float>(2, 0.f), TPair<int32, float>(5, 1.f), TPair<int32, float>(8, 2.f) });

	AActor* Victim = NewObject<AActor>();

	FNetCombat_Rig Rig = NetCombat_MakeRig();
	Rig.DataComp->CharacterProfile = Asset;
	Rig.FBComp->SetFlipbook(Multi);
	Rig.DataComp->SetNetContextOverrideForTests(EPaper2DPlusNetContext::Authority);
	Rig.DataComp->SetServerTimeOverrideForTests(1000.0);
	Rig.DataComp->SetMoveInstanceCounterForTests(1);
	NetCombat_InjectVictimOverlap(Rig, Victim, 10, 5);

	// Window 0 (frame 2): one hit, no duplicate; an immediate re-call in the same window is a duplicate.
	Rig.DataComp->SetFrameSpanForTests(1, 2);
	FPaper2DPlusHitValidationResult W0;
	TestTrue(TEXT("window 0 hit registers"), Rig.DataComp->ValidateAndRegisterHit(Victim, W0));
	TestEqual(TEXT("window 0 index resolves to 0"), W0.HitWindowIndex, 0);
	FPaper2DPlusHitValidationResult W0Dup;
	TestFalse(TEXT("window 0 re-hit is a duplicate"), Rig.DataComp->ValidateAndRegisterHit(Victim, W0Dup));
	TestTrue(TEXT("window 0 dup flagged"), W0Dup.bDuplicate);

	// Window 1 (frame 5): the SAME victim registers again — a different window key.
	Rig.DataComp->SetFrameSpanForTests(4, 5);
	FPaper2DPlusHitValidationResult W1;
	TestTrue(TEXT("window 1 hit registers the same victim"), Rig.DataComp->ValidateAndRegisterHit(Victim, W1));
	TestEqual(TEXT("window 1 index resolves to 1"), W1.HitWindowIndex, 1);

	// Window 2 (frame 8): one more.
	Rig.DataComp->SetFrameSpanForTests(7, 8);
	FPaper2DPlusHitValidationResult W2;
	TestTrue(TEXT("window 2 hit registers"), Rig.DataComp->ValidateAndRegisterHit(Victim, W2));
	TestEqual(TEXT("window 2 index resolves to 2"), W2.HitWindowIndex, 2);

	TestEqual(TEXT("three windows => three dedup entries for one victim/instance"),
		Rig.DataComp->GetProcessedHitsForTests().Num(), 3);

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Span validation: a 1-frame active window crossed entirely inside one
// HandleFrameChanged(prev=3 -> new=6) validates when bRequireActiveAttackFrame=true;
// a span entirely outside the active frame => false.
// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusNetCombatSpanValidation,
	"Paper2DPlus.Network.Combat.SpanValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusNetCombatSpanValidation::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	UPaperFlipbook* Lunge = NetCombat_AddMove(Asset, TEXT("Lunge"), 9);
	// A single 1-frame active window on frame 5.
	NetCombat_SetAttackFrame(Asset, 0, 5, 10, 5);

	AActor* Victim = NewObject<AActor>();

	FNetCombat_Rig Rig = NetCombat_MakeRig();
	Rig.DataComp->CharacterProfile = Asset;
	Rig.FBComp->SetFlipbook(Lunge);
	Rig.DataComp->SetNetContextOverrideForTests(EPaper2DPlusNetContext::Authority);
	Rig.DataComp->SetServerTimeOverrideForTests(1000.0);
	Rig.DataComp->SetMoveInstanceCounterForTests(1);
	NetCombat_InjectVictimOverlap(Rig, Victim, 10, 5);

	// Warm the cache, then drive HandleFrameChanged(prev=3 -> new=6) so the recorded span is (3, 6] —
	// frame 5 (the active window) is crossed entirely INSIDE the tick. Seed PreviousFrameIndex=3 by
	// pumping frame 3 first (its own span is recorded but overwritten by the 6 call).
	Rig.DataComp->HandleFlipbookChanged(Lunge);
	Rig.DataComp->HandleFrameChanged(3);
	Rig.DataComp->HandleFrameChanged(6);

	FPaper2DPlusHitValidationResult InSpan;
	const bool bInSpan = Rig.DataComp->ValidateAndRegisterHit(Victim, InSpan);
	TestTrue(TEXT("a 1-frame window crossed inside one tick validates"), bInSpan);
	TestEqual(TEXT("validated at the in-span active frame 5"), InSpan.FrameIndex, 5);

	// A second move instance with a span entirely after the active frame (7,8] => no in-span attack
	// frame => false.
	Rig.DataComp->ResetHitDedupWindow();
	Rig.DataComp->SetFrameSpanForTests(7, 8);
	FPaper2DPlusHitValidationResult OutOfSpan;
	const bool bOut = Rig.DataComp->ValidateAndRegisterHit(Victim, OutOfSpan);
	TestFalse(TEXT("a span past the active frame does not validate"), bOut);

	// With bRequireActiveAttackFrame=false the span gate is bypassed (still victim-membership-gated).
	FPaper2DPlusHitValidationResult NoRequire;
	const bool bNoRequire = Rig.DataComp->ValidateAndRegisterHit(Victim, NoRequire, /*HitWindowIndex=*/-1, /*bRequireActiveAttackFrame=*/false);
	TestTrue(TEXT("bRequireActiveAttackFrame=false validates off the active frame"), bNoRequire);

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Standalone: the full validate/dedup path works with NO net context (the counter
// increments unconditionally — single-player hit dedup is a real feature).
// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusNetCombatStandalone,
	"Paper2DPlus.Network.Combat.StandaloneFullPath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusNetCombatStandalone::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	UPaperFlipbook* Jab = NetCombat_AddMove(Asset, TEXT("Jab"), 8);
	NetCombat_SetAttackFrame(Asset, 0, 4, 14, 8);

	AActor* Victim = NewObject<AActor>();

	// No context override AND replication off => worldless Standalone.
	FNetCombat_Rig Rig = NetCombat_MakeRig(/*bEnableReplication=*/false);
	Rig.DataComp->CharacterProfile = Asset;
	Rig.FBComp->SetFlipbook(Jab);
	Rig.DataComp->SetFrameSpanForTests(3, 4);
	NetCombat_InjectVictimOverlap(Rig, Victim, 14, 8);

	FPaper2DPlusHitValidationResult Out;
	const bool bValid = Rig.DataComp->ValidateAndRegisterHit(Victim, Out);
	TestTrue(TEXT("Standalone validates (no net context)"), bValid);
	TestEqual(TEXT("Standalone reads server damage"), Out.Damage, 14.f);
	TestEqual(TEXT("Standalone registered one dedup entry"), Rig.DataComp->GetProcessedHitsForTests().Num(), 1);

	FPaper2DPlusHitValidationResult Dup;
	TestFalse(TEXT("Standalone duplicate guarded"), Rig.DataComp->ValidateAndRegisterHit(Victim, Dup));
	TestTrue(TEXT("Standalone duplicate flagged"), Dup.bDuplicate);

	// RegisterHitOnce (the light path) also works in Standalone.
	AActor* Victim2 = NewObject<AActor>();
	TestTrue(TEXT("Standalone RegisterHitOnce first call true"), Rig.DataComp->RegisterHitOnce(Victim2));
	TestFalse(TEXT("Standalone RegisterHitOnce duplicate false"), Rig.DataComp->RegisterHitOnce(Victim2));

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// GetCurrentHitWindowIndex: absent curve => 0; span-aware mid-move steps resolve
// to the latest in-span attack frame's window value.
// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusNetCombatHitWindowIndex,
	"Paper2DPlus.Network.Combat.GetCurrentHitWindowIndex",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusNetCombatHitWindowIndex::RunTest(const FString& Parameters)
{
	// Absent curve => 0.
	{
		UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
		UPaperFlipbook* Plain = NetCombat_AddMove(Asset, TEXT("Plain"), 8);
		NetCombat_SetAttackFrame(Asset, 0, 4, 10, 5);

		FNetCombat_Rig Rig = NetCombat_MakeRig();
		Rig.DataComp->CharacterProfile = Asset;
		Rig.FBComp->SetFlipbook(Plain);
		Rig.DataComp->SetNetContextOverrideForTests(EPaper2DPlusNetContext::Authority);
		Rig.DataComp->SetFrameSpanForTests(3, 4);
		TestEqual(TEXT("absent HitWindow curve => index 0"), Rig.DataComp->GetCurrentHitWindowIndex(), 0);
	}

	// Span-aware steps: a window boundary crossed inside one tick resolves to the latest in-span attack
	// frame's value.
	{
		UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
		UPaperFlipbook* Multi = NetCombat_AddMove(Asset, TEXT("Multi"), 9);
		NetCombat_SetAttackFrame(Asset, 0, 2, 10, 5);
		NetCombat_SetAttackFrame(Asset, 0, 5, 10, 5);
		NetCombat_AddHitWindowCurve(Asset, 0, { TPair<int32, float>(2, 0.f), TPair<int32, float>(5, 1.f) });

		FNetCombat_Rig Rig = NetCombat_MakeRig();
		Rig.DataComp->CharacterProfile = Asset;
		Rig.FBComp->SetFlipbook(Multi);
		Rig.DataComp->SetNetContextOverrideForTests(EPaper2DPlusNetContext::Authority);

		Rig.DataComp->SetFrameSpanForTests(1, 2);
		TestEqual(TEXT("span ending on frame 2 => window 0"), Rig.DataComp->GetCurrentHitWindowIndex(), 0);

		// A multi-frame tick (3 -> 6) crosses frame 5 (window 1) — the latest in-span attack frame is 5.
		Rig.DataComp->SetFrameSpanForTests(3, 6);
		TestEqual(TEXT("span crossing frame 5 => window 1 (latest in-span attack frame)"),
			Rig.DataComp->GetCurrentHitWindowIndex(), 1);
	}

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Move-change live-frame contract: a flipbook swap re-anchors the active-attack-frame
// resolution to the NEW move's LIVE key frame (HandleFlipbookChanged pumps its own
// frame-0 dispatch, resetting the per-tick span) and reads the new move's authored
// damage, never a stale prior-move frame.
// (Coverage note: the former FIX-2 stale-span-reset-on-SELF-LOOP regression this test
// guarded is no longer reachable now that the transition driver was removed; a
// same-flipbook restart can no longer bump a move instance without pumping a frame-0
// span, so no stale span can linger. See the fix report for the lost coverage.)
// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusNetCombatStaleSpanReset,
	"Paper2DPlus.Network.Combat.StaleSpanResetOnMoveInstance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusNetCombatStaleSpanReset::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	// "Loop" carries an attack frame at its live frame 0.
	UPaperFlipbook* Loop = NetCombat_AddMove(Asset, TEXT("Loop"), 8);
	NetCombat_SetAttackFrame(Asset, 0, 0, 10, 5);
	// "MoveB" carries its only attack frame at 0 (its live frame after a swap).
	UPaperFlipbook* MoveB = NetCombat_AddMove(Asset, TEXT("MoveB"), 8);
	NetCombat_SetAttackFrame(Asset, 1, 0, 12, 6);

	AActor* Victim = NewObject<AActor>();

	FNetCombat_Rig Rig = NetCombat_MakeRig();
	Rig.DataComp->CharacterProfile = Asset;
	Rig.FBComp->SetFlipbook(Loop);
	Rig.DataComp->SetNetContextOverrideForTests(EPaper2DPlusNetContext::Authority);
	Rig.DataComp->SetServerTimeOverrideForTests(1000.0);
	Rig.DataComp->SetMoveInstanceCounterForTests(1);

	// Establish Loop as the current move; its frame-0 dispatch anchors the live span.
	Rig.DataComp->HandleFlipbookChanged(Loop);
	NetCombat_InjectVictimOverlap(Rig, Victim, 10, 5);
	FPaper2DPlusHitValidationResult OnLoop;
	TestTrue(TEXT("Loop hit validates"), Rig.DataComp->ValidateAndRegisterHit(Victim, OnLoop));
	TestEqual(TEXT("Loop resolves its live frame 0"), OnLoop.FrameIndex, 0);

	// Move-change live-frame contract: a flipbook swap re-anchors to the NEW move's OWN live frame 0
	// (HandleFlipbookChanged pumps its own frame-0 dispatch, resetting the per-tick span) and reads the
	// new move's authored damage, never a stale prior-move frame.
	Rig.FBComp->SetFlipbook(MoveB);
	Rig.DataComp->HandleFlipbookChanged(MoveB);
	NetCombat_InjectVictimOverlap(Rig, Victim, 12, 6);
	FPaper2DPlusHitValidationResult OnMoveB;
	TestTrue(TEXT("MoveB hit validates"), Rig.DataComp->ValidateAndRegisterHit(Victim, OnMoveB));
	TestEqual(TEXT("MoveB resolves its OWN live frame 0, not a stale Move-A frame"), OnMoveB.FrameIndex, 0);
	TestEqual(TEXT("MoveB reads MoveB's authored damage"), OnMoveB.Damage, 12.f);

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// FIX 3 regression: an overlap result with the CORRECT DefenderActor but bHit=false
// (a broadphase/miss candidate) must NOT validate — the victim-membership predicate
// gates on Result.bHit. (Old code ignored bHit and would have validated the miss.)
// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusNetCombatBHitGate,
	"Paper2DPlus.Network.Combat.OverlapBHitGate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusNetCombatBHitGate::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	UPaperFlipbook* Jab = NetCombat_AddMove(Asset, TEXT("Jab"), 8);
	NetCombat_SetAttackFrame(Asset, 0, 4, 10, 5);

	AActor* Victim = NewObject<AActor>();

	FNetCombat_Rig Rig = NetCombat_MakeRig();
	Rig.DataComp->CharacterProfile = Asset;
	Rig.FBComp->SetFlipbook(Jab);
	Rig.DataComp->SetNetContextOverrideForTests(EPaper2DPlusNetContext::Authority);
	Rig.DataComp->SetServerTimeOverrideForTests(1000.0);
	Rig.DataComp->SetMoveInstanceCounterForTests(1);
	Rig.DataComp->SetFrameSpanForTests(3, 4); // an attack frame is in-span (so only bHit gates the result)

	// Correct victim, but bHit=false — a non-confirmed candidate.
	NetCombat_InjectVictimOverlap(Rig, Victim, 10, 5, /*bHit=*/false);
	FPaper2DPlusHitValidationResult Miss;
	const bool bMiss = Rig.DataComp->ValidateAndRegisterHit(Victim, Miss);
	TestFalse(TEXT("bHit=false candidate does not validate"), bMiss);
	TestFalse(TEXT("bHit=false result not valid"), Miss.bValidHit);
	TestEqual(TEXT("bHit=false registers nothing"), Rig.DataComp->GetProcessedHitsForTests().Num(), 0);

	// Sanity: the SAME victim with bHit=true validates (the only difference is the flag).
	NetCombat_InjectVictimOverlap(Rig, Victim, 10, 5, /*bHit=*/true);
	FPaper2DPlusHitValidationResult Real;
	TestTrue(TEXT("bHit=true confirmed overlap validates"), Rig.DataComp->ValidateAndRegisterHit(Victim, Real));
	TestTrue(TEXT("bHit=true result valid"), Real.bValidHit);

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// TASK-91 (Codex F197a): span validation must sample the RESOLVED attack frame's
// boxes through the REAL subsystem sweep. A coarse tick that crossed a 1-frame
// attack window (frame 4) and settled on a non-attack frame (frame 6) previously
// swept the CURRENT frame's cached boxes — empty — and rejected the very hit the
// span check was built to accept. Pins: (1) the bug class (current-frame cached
// sweep finds nothing), (2) the no-divergence ratchet (the frame-parameterized
// build byte-matches the cached build for the same frame), (3) the real-subsystem
// span case (the explicit-boxes overload finds the defender's hurtbox overlap).
// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusNetCombatSpanFrameBoxSampling,
	"Paper2DPlus.Network.Combat.SpanValidationSamplesResolvedFrameBoxes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusNetCombatSpanFrameBoxSampling::RunTest(const FString& Parameters)
{
	// Attacker: 8-frame Jab whose ONLY attack box lives on frame 4 (10 dmg / 5 kb).
	UPaper2DPlusCharacterProfileAsset* AttackerAsset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	UPaperFlipbook* Jab = NetCombat_AddMove(AttackerAsset, TEXT("Jab"), 8);
	NetCombat_SetAttackFrame(AttackerAsset, 0, 4, /*Damage=*/10, /*Knockback=*/5);

	FNetCombat_Rig Attacker = NetCombat_MakeRig(/*bEnableReplication=*/false);
	Attacker.DataComp->CharacterProfile = AttackerAsset;
	Attacker.FBComp->SetFlipbook(Jab);
	Attacker.DataComp->HandleFlipbookChanged(Jab);

	// Defender: 1-frame Idle with a hurtbox on frame 0. Identical authored geometry and identity
	// transforms (worldless rigs sit at the origin), so frame 4's attack box overlaps it.
	UPaper2DPlusCharacterProfileAsset* DefenderAsset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	UPaperFlipbook* Idle = NetCombat_AddMove(DefenderAsset, TEXT("Idle"), 1);
	NetCombat_SetHurtboxFrame(DefenderAsset, 0, 0);

	FNetCombat_Rig Defender = NetCombat_MakeRig(/*bEnableReplication=*/false);
	Defender.DataComp->CharacterProfile = DefenderAsset;
	Defender.FBComp->SetFlipbook(Idle);
	Defender.DataComp->HandleFlipbookChanged(Idle);
	Defender.DataComp->HandleFrameChanged(0);

	// The REAL broadphase, worldless (the BroadphaseRebuildGating precedent) — the defender's
	// hurtboxes index through the same RegisterProfileComponent/BuildIndex path a world uses.
	UPaper2DPlusHitboxSubsystem* Subsystem = NewObject<UPaper2DPlusHitboxSubsystem>();
	Subsystem->RegisterProfileComponent(Defender.DataComp);

	// Reference: the CACHED build ON the attack frame (the pre-existing current-frame path).
	Attacker.DataComp->HandleFrameChanged(4);
	TArray<FWorldHitbox> CachedAtFrame4;
	TestTrue(TEXT("cached build on frame 4 resolves"), Attacker.DataComp->GetCachedWorldAttackBoxes(CachedAtFrame4));
	TestEqual(TEXT("frame 4 carries the one attack box"), CachedAtFrame4.Num(), 1);

	// Coarse tick lands on frame 6 — a NON-attack frame; the recorded span (3,6] crossed frame 4.
	Attacker.DataComp->HandleFrameChanged(6);
	Attacker.DataComp->SetFrameSpanForTests(3, 6);

	// (1) Bug-class pin: the current-frame cached sweep is EMPTY on frame 6 — this is exactly what
	// the pre-fix re-query swept, which rejected the in-span hit.
	TArray<FHitboxCollisionResult> CurrentFrameResults;
	TestFalse(TEXT("current-frame cached sweep finds nothing on the non-attack frame"),
		Subsystem->QueryAttackOverlaps(Attacker.DataComp, CurrentFrameResults));
	TestEqual(TEXT("no overlap from the current frame's empty boxes"), CurrentFrameResults.Num(), 0);

	// (2) No-divergence ratchet: the frame-parameterized build for frame 4, issued while the CURRENT
	// frame is 6, must match the cached build captured on frame 4 (same pivot/transform funnel).
	TArray<FWorldHitbox> BuiltForFrame4;
	TestTrue(TEXT("frame-parameterized build resolves frame 4 off the current frame"),
		Attacker.DataComp->BuildWorldAttackBoxesForFrameForTests(4, BuiltForFrame4));
	TestEqual(TEXT("one attack box built for frame 4"), BuiltForFrame4.Num(), 1);
	if (CachedAtFrame4.Num() == 1 && BuiltForFrame4.Num() == 1)
	{
		TestEqual(TEXT("built center matches cached"), BuiltForFrame4[0].Center, CachedAtFrame4[0].Center);
		TestEqual(TEXT("built extents match cached"), BuiltForFrame4[0].Extents, CachedAtFrame4[0].Extents);
		TestEqual(TEXT("built damage matches cached"), BuiltForFrame4[0].Damage, CachedAtFrame4[0].Damage);
		TestEqual(TEXT("built knockback matches cached"), BuiltForFrame4[0].Knockback, CachedAtFrame4[0].Knockback);
	}

	// A non-attack frame builds successfully with ZERO boxes (true + empty — distinct from failure).
	TArray<FWorldHitbox> BuiltForFrame5;
	TestTrue(TEXT("non-attack frame 5 builds"), Attacker.DataComp->BuildWorldAttackBoxesForFrameForTests(5, BuiltForFrame5));
	TestEqual(TEXT("non-attack frame 5 builds zero boxes"), BuiltForFrame5.Num(), 0);

	// (3) Real-subsystem span case: the explicit-boxes sweep with the RESOLVED frame's boxes finds
	// the defender — the fast-tick lag-comp hit validates through the real query path.
	TArray<FHitboxCollisionResult> SpanResults;
	TestTrue(TEXT("explicit-boxes sweep overlaps the defender"),
		Subsystem->QueryAttackOverlaps(Attacker.DataComp, BuiltForFrame4, SpanResults));
	TestEqual(TEXT("exactly one overlap"), SpanResults.Num(), 1);
	if (SpanResults.Num() == 1)
	{
		TestTrue(TEXT("overlap confirmed"), SpanResults[0].bHit);
		TestTrue(TEXT("overlap resolves to the defender actor"), SpanResults[0].DefenderActor == Defender.Owner);
		TestEqual(TEXT("authored damage rides the result"), SpanResults[0].Damage, 10.f);
		TestEqual(TEXT("authored knockback rides the result"), SpanResults[0].Knockback, 5.f);
	}

	return true;
}

#endif // !UE_BUILD_SHIPPING

#endif // WITH_EDITOR
