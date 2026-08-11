// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#if WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "Paper2DPlusNetTypes.h"
#include "Paper2DPlusFrameCurve.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Paper2DPlusCharacterProfileComponent.h"
#include "Paper2DPlusTypes.h"
#include "Paper2DPlusNetTestTypes.h"
#include "PaperFlipbook.h"
#include "PaperFlipbookComponent.h"
#include "PaperSprite.h"
#include "GameFramework/Actor.h"

/** TASK-145 automatic hit detection tests (worldless, driven through the public
 *  HandleFlipbookChanged/HandleFrameChanged funnels + the U1/U4 test seams). Pins: arming derives
 *  from the entered frame's attack boxes (OnAttackWindowBegin/End edges, including the move-end and
 *  direct-toggle-off closes), hits dedup once per (victim, move instance, "HitWindow" window)
 *  through the shared ProcessedHits ledger, the payload carries the matched overlap's server data,
 *  the victim-side OnHitReceived mirrors the attacker broadcast, OnAttackWhiffed fires only when an
 *  armed move instance registered zero hits, proxies never arm, and the subsystem's external
 *  TickAutoHitDetection entry is dedup-safe.
 *  Helpers are AutoHit_-prefixed per the unity-build file-unique-name rule. */

namespace
{
	/** Build a flipbook with NumFrames key frames (FPS 10, FrameRun 1 — frame i lives at i*0.1s). */
	UPaperFlipbook* AutoHit_MakeFlipbook(UObject* Owner, int32 NumFrames)
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
	UPaperFlipbook* AutoHit_AddMove(UPaper2DPlusCharacterProfileAsset* Asset, const FString& MoveName, int32 NumFrames)
	{
		FFlipbookProfileEntry Anim;
		Anim.Identity.FlipbookName = MoveName;
		UPaperFlipbook* FB = AutoHit_MakeFlipbook(Asset, NumFrames);
		Anim.Identity.Flipbook = FB;
		Anim.CombatData.Frames.SetNum(NumFrames);
		Anim.CombatData.FrameExtractionInfo.SetNum(NumFrames);
		Asset->Flipbooks.Add(Anim);
		return FB;
	}

	/** Mark an Attack hitbox (Damage/Knockback authored) on a frame of the entry at EntryIndex. */
	void AutoHit_SetAttackFrame(UPaper2DPlusCharacterProfileAsset* Asset, int32 EntryIndex, int32 Frame, int32 Damage = 0, int32 Knockback = 0)
	{
		FHitboxData Box;
		Box.Type = EHitboxType::Attack;
		Box.Width = 16;
		Box.Height = 16;
		Box.Damage = Damage;
		Box.Knockback = Knockback;
		Asset->Flipbooks[EntryIndex].CombatData.Frames[Frame].Hitboxes.Add(Box);
	}

	/** Author a "HitWindow" Constant/step curve on the entry at EntryIndex. */
	void AutoHit_AddHitWindowCurve(UPaper2DPlusCharacterProfileAsset* Asset, int32 EntryIndex, const TArray<TPair<int32, float>>& Keys)
	{
		FPaper2DPlusFrameCurve& Curve = Asset->Flipbooks[EntryIndex].CurveData.Curves.Add(TEXT("HitWindow"));
		Curve.Mode = EPaper2DPlusCurveInterp::Constant;
		for (const TPair<int32, float>& K : Keys)
		{
			Curve.SetKeyValue(K.Key, K.Value);
		}
	}

	struct FAutoHit_Rig
	{
		AActor* Owner = nullptr;
		UPaperFlipbookComponent* FBComp = nullptr;
		UPaper2DPlusCharacterProfileComponent* DataComp = nullptr;
	};

	/** Replication OFF by default: the resolved context is Standalone (auto detection allowed). */
	FAutoHit_Rig AutoHit_MakeRig(bool bEnableReplication = false)
	{
		FAutoHit_Rig Rig;
		Rig.Owner = NewObject<AActor>();
		Rig.FBComp = NewObject<UPaperFlipbookComponent>(Rig.Owner);
		Rig.DataComp = NewObject<UPaper2DPlusCharacterProfileComponent>(Rig.Owner);
		Rig.Owner->AddOwnedComponent(Rig.FBComp);
		Rig.Owner->AddOwnedComponent(Rig.DataComp);
		Rig.DataComp->FlipbookComponent = Rig.FBComp;
		Rig.DataComp->bEnableReplication = bEnableReplication;
		return Rig;
	}

	/** Install an overlap-query override that returns ONE result resolving to Victim. */
	void AutoHit_InjectVictimOverlap(FAutoHit_Rig& Rig, AActor* Victim, int32 Damage, int32 Knockback)
	{
		Rig.DataComp->SetOverlapQueryOverrideForTests(
			[Victim, Damage, Knockback](TArray<FHitboxCollisionResult>& Out)
			{
				FHitboxCollisionResult R;
				R.bHit = true;
				R.DefenderActor = Victim;
				R.Damage = Damage;
				R.Knockback = Knockback;
				Out.Add(R);
			});
	}

	/** Bind every TASK-145 delegate of the attacker rig to one recorder. */
	UPaper2DPlusAutoHitRecorder* AutoHit_BindRecorder(FAutoHit_Rig& Rig)
	{
		UPaper2DPlusAutoHitRecorder* Recorder = NewObject<UPaper2DPlusAutoHitRecorder>();
		Rig.DataComp->OnHitConnected.AddDynamic(Recorder, &UPaper2DPlusAutoHitRecorder::OnHitConnected);
		Rig.DataComp->OnAttackWindowBegin.AddDynamic(Recorder, &UPaper2DPlusAutoHitRecorder::OnWindowBegin);
		Rig.DataComp->OnAttackWindowEnd.AddDynamic(Recorder, &UPaper2DPlusAutoHitRecorder::OnWindowEnd);
		Rig.DataComp->OnAttackWhiffed.AddDynamic(Recorder, &UPaper2DPlusAutoHitRecorder::OnWhiff);
		return Recorder;
	}
}

// Everything below drives the !UE_BUILD_SHIPPING test seams.
#if !UE_BUILD_SHIPPING

// ─────────────────────────────────────────────────────────────────────────────
// Arming edges + single hit: entering an attack frame arms (Begin), the pass
// registers ONE hit per victim per window across the whole armed span, the
// payload carries the matched overlap + move context, the victim's component
// broadcasts OnHitReceived, and the first non-attack frame closes (End).
// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusAutoHitArmingEdges,
	"Paper2DPlus.HitDetection.Auto.ArmingEdgesAndSingleHit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusAutoHitArmingEdges::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	UPaperFlipbook* Slash = AutoHit_AddMove(Asset, TEXT("Slash"), 4);
	AutoHit_SetAttackFrame(Asset, 0, 1, /*Damage=*/7, /*Knockback=*/3);
	AutoHit_SetAttackFrame(Asset, 0, 2, /*Damage=*/7, /*Knockback=*/3);

	FAutoHit_Rig Attacker = AutoHit_MakeRig();
	Attacker.DataComp->CharacterProfile = Asset;
	Attacker.DataComp->bAutoHitDetection = true;
	Attacker.FBComp->SetFlipbook(Slash);

	FAutoHit_Rig VictimRig = AutoHit_MakeRig();
	AutoHit_InjectVictimOverlap(Attacker, VictimRig.Owner, 7, 3);

	UPaper2DPlusAutoHitRecorder* Recorder = AutoHit_BindRecorder(Attacker);
	UPaper2DPlusAutoHitRecorder* VictimRecorder = NewObject<UPaper2DPlusAutoHitRecorder>();
	VictimRig.DataComp->OnHitReceived.AddDynamic(VictimRecorder, &UPaper2DPlusAutoHitRecorder::OnHitReceived);

	// Frame 0 (no attack boxes): the move warm dispatches frame 0 — nothing arms.
	Attacker.DataComp->HandleFlipbookChanged(Slash);
	TestEqual(TEXT("frame 0: no window yet"), Recorder->WindowBeginCount, 0);
	TestFalse(TEXT("frame 0: not armed"), Attacker.DataComp->IsAutoHitDetectionArmedForTests());

	// Frame 1 (attack): arms + first hit.
	Attacker.DataComp->HandleFrameChanged(1);
	TestEqual(TEXT("frame 1: window began once"), Recorder->WindowBeginCount, 1);
	TestEqual(TEXT("frame 1: window began on frame 1"), Recorder->LastWindowBeginFrame, 1);
	TestTrue(TEXT("frame 1: armed"), Attacker.DataComp->IsAutoHitDetectionArmedForTests());
	TestEqual(TEXT("frame 1: one hit connected"), Recorder->HitConnectedCount, 1);
	TestEqual(TEXT("frame 1: victim received the hit"), VictimRecorder->HitReceivedCount, 1);

	// Payload = the matched overlap + attacker move context.
	TestEqual(TEXT("payload: damage"), Recorder->LastHit.Damage, 7.f);
	TestEqual(TEXT("payload: knockback"), Recorder->LastHit.Knockback, 3.f);
	TestEqual(TEXT("payload: move name"), Recorder->LastHit.MoveName, FString(TEXT("Slash")));
	TestEqual(TEXT("payload: frame index"), Recorder->LastHit.FrameIndex, 1);
	TestEqual(TEXT("payload: window 0 (no curve)"), Recorder->LastHit.HitWindowIndex, 0);
	TestTrue(TEXT("payload: attacker actor"), Recorder->LastHit.Attacker == Attacker.Owner);
	TestTrue(TEXT("payload: attacker component"), Recorder->LastHit.AttackerComponent == Attacker.DataComp);
	TestTrue(TEXT("payload: victim actor"), Recorder->LastHit.Victim == VictimRig.Owner);
	TestTrue(TEXT("payload: victim component"), Recorder->LastHit.VictimComponent == VictimRig.DataComp);

	// Frame 2 (attack): still the same window instance — dedup, no second hit, no second Begin.
	Attacker.DataComp->HandleFrameChanged(2);
	TestEqual(TEXT("frame 2: still one Begin"), Recorder->WindowBeginCount, 1);
	TestEqual(TEXT("frame 2: dedup holds"), Recorder->HitConnectedCount, 1);
	TestEqual(TEXT("frame 2: victim still one"), VictimRecorder->HitReceivedCount, 1);

	// Frame 3 (no attack): disarms + End.
	Attacker.DataComp->HandleFrameChanged(3);
	TestEqual(TEXT("frame 3: window ended once"), Recorder->WindowEndCount, 1);
	TestEqual(TEXT("frame 3: window ended on frame 3"), Recorder->LastWindowEndFrame, 3);
	TestFalse(TEXT("frame 3: disarmed"), Attacker.DataComp->IsAutoHitDetectionArmedForTests());

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Multi-hit windows: a "HitWindow" step curve segments the armed span — each
// window registers its own once-per-victim hit through the shared ledger.
// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusAutoHitMultiWindow,
	"Paper2DPlus.HitDetection.Auto.MultiHitWindows",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusAutoHitMultiWindow::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	UPaperFlipbook* Flurry = AutoHit_AddMove(Asset, TEXT("Flurry"), 4);
	AutoHit_SetAttackFrame(Asset, 0, 1, 4, 1);
	AutoHit_SetAttackFrame(Asset, 0, 2, 4, 1);
	AutoHit_AddHitWindowCurve(Asset, 0, { {0, 0.f}, {2, 1.f} }); // frames 0-1 = window 0, 2+ = window 1

	FAutoHit_Rig Attacker = AutoHit_MakeRig();
	Attacker.DataComp->CharacterProfile = Asset;
	Attacker.DataComp->bAutoHitDetection = true;
	Attacker.FBComp->SetFlipbook(Flurry);

	AActor* Victim = NewObject<AActor>();
	AutoHit_InjectVictimOverlap(Attacker, Victim, 4, 1);
	UPaper2DPlusAutoHitRecorder* Recorder = AutoHit_BindRecorder(Attacker);

	Attacker.DataComp->HandleFlipbookChanged(Flurry);
	Attacker.DataComp->HandleFrameChanged(1);
	TestEqual(TEXT("window 0 hit"), Recorder->HitConnectedCount, 1);
	TestEqual(TEXT("window 0 index"), Recorder->LastHit.HitWindowIndex, 0);

	Attacker.DataComp->HandleFrameChanged(2);
	TestEqual(TEXT("window 1 re-hits the same victim"), Recorder->HitConnectedCount, 2);
	TestEqual(TEXT("window 1 index"), Recorder->LastHit.HitWindowIndex, 1);

	// One window Begin only — the curve segments dedup windows, not the armed span.
	TestEqual(TEXT("single armed span"), Recorder->WindowBeginCount, 1);

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Whiff: an armed move instance that registers zero hits whiffs at move end;
// a move that landed a hit does not; a mid-window move end closes the window
// before the whiff fires.
// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusAutoHitWhiff,
	"Paper2DPlus.HitDetection.Auto.WhiffOnMoveEnd",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusAutoHitWhiff::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	UPaperFlipbook* Slash = AutoHit_AddMove(Asset, TEXT("Slash"), 4);
	AutoHit_SetAttackFrame(Asset, 0, 1, 5, 0);
	AutoHit_SetAttackFrame(Asset, 0, 2, 5, 0);
	UPaperFlipbook* Idle = AutoHit_AddMove(Asset, TEXT("Idle"), 2);

	// Case A: the whole move plays out with NO overlaps (worldless: no override + no world = empty
	// query) — the move-end finalize whiffs.
	{
		FAutoHit_Rig Attacker = AutoHit_MakeRig();
		Attacker.DataComp->CharacterProfile = Asset;
		Attacker.DataComp->bAutoHitDetection = true;
		Attacker.FBComp->SetFlipbook(Slash);
		UPaper2DPlusAutoHitRecorder* Recorder = AutoHit_BindRecorder(Attacker);

		Attacker.DataComp->HandleFlipbookChanged(Slash);
		Attacker.DataComp->HandleFrameChanged(1);
		Attacker.DataComp->HandleFrameChanged(2);
		Attacker.DataComp->HandleFrameChanged(3);
		TestEqual(TEXT("A: window closed by frame 3"), Recorder->WindowEndCount, 1);
		TestEqual(TEXT("A: no whiff before move end"), Recorder->WhiffCount, 0);

		Attacker.FBComp->SetFlipbook(Idle);
		Attacker.DataComp->HandleFlipbookChanged(Idle);
		TestEqual(TEXT("A: whiffed at move end"), Recorder->WhiffCount, 1);
		TestEqual(TEXT("A: whiff names the attacking move"), Recorder->LastWhiffMoveName, FString(TEXT("Slash")));
		TestEqual(TEXT("A: no extra window End from finalize"), Recorder->WindowEndCount, 1);
	}

	// Case B: the move landed a hit — no whiff.
	{
		FAutoHit_Rig Attacker = AutoHit_MakeRig();
		Attacker.DataComp->CharacterProfile = Asset;
		Attacker.DataComp->bAutoHitDetection = true;
		Attacker.FBComp->SetFlipbook(Slash);
		AActor* Victim = NewObject<AActor>();
		AutoHit_InjectVictimOverlap(Attacker, Victim, 5, 0);
		UPaper2DPlusAutoHitRecorder* Recorder = AutoHit_BindRecorder(Attacker);

		Attacker.DataComp->HandleFlipbookChanged(Slash);
		Attacker.DataComp->HandleFrameChanged(1);
		Attacker.DataComp->HandleFrameChanged(3);
		Attacker.FBComp->SetFlipbook(Idle);
		Attacker.DataComp->HandleFlipbookChanged(Idle);
		TestEqual(TEXT("B: landed a hit"), Recorder->HitConnectedCount, 1);
		TestEqual(TEXT("B: no whiff"), Recorder->WhiffCount, 0);
	}

	// Case C: the move ends MID-window — finalize closes the window, then whiffs.
	{
		FAutoHit_Rig Attacker = AutoHit_MakeRig();
		Attacker.DataComp->CharacterProfile = Asset;
		Attacker.DataComp->bAutoHitDetection = true;
		Attacker.FBComp->SetFlipbook(Slash);
		UPaper2DPlusAutoHitRecorder* Recorder = AutoHit_BindRecorder(Attacker);

		Attacker.DataComp->HandleFlipbookChanged(Slash);
		Attacker.DataComp->HandleFrameChanged(1);
		TestTrue(TEXT("C: armed mid-move"), Attacker.DataComp->IsAutoHitDetectionArmedForTests());

		Attacker.FBComp->SetFlipbook(Idle);
		Attacker.DataComp->HandleFlipbookChanged(Idle);
		TestEqual(TEXT("C: window closed by the move end"), Recorder->WindowEndCount, 1);
		TestEqual(TEXT("C: whiffed"), Recorder->WhiffCount, 1);
		TestFalse(TEXT("C: disarmed"), Attacker.DataComp->IsAutoHitDetectionArmedForTests());
	}

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Authority gate + opt-in: proxies never arm (no events, no registrations);
// authority runs the full path; the toggle off is a true no-op.
// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusAutoHitAuthorityGate,
	"Paper2DPlus.HitDetection.Auto.AuthorityGateAndOptIn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusAutoHitAuthorityGate::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	UPaperFlipbook* Slash = AutoHit_AddMove(Asset, TEXT("Slash"), 4);
	AutoHit_SetAttackFrame(Asset, 0, 1, 9, 2);

	AActor* Victim = NewObject<AActor>();

	// Proxies never arm — silently (no warn spam on a per-frame path).
	for (EPaper2DPlusNetContext ProxyCtx : { EPaper2DPlusNetContext::AutonomousProxy, EPaper2DPlusNetContext::SimulatedProxy })
	{
		FAutoHit_Rig Attacker = AutoHit_MakeRig(/*bEnableReplication=*/true);
		Attacker.DataComp->CharacterProfile = Asset;
		Attacker.DataComp->bAutoHitDetection = true;
		Attacker.FBComp->SetFlipbook(Slash);
		Attacker.DataComp->SetNetContextOverrideForTests(ProxyCtx);
		AutoHit_InjectVictimOverlap(Attacker, Victim, 9, 2);
		UPaper2DPlusAutoHitRecorder* Recorder = AutoHit_BindRecorder(Attacker);

		Attacker.DataComp->HandleFlipbookChanged(Slash);
		Attacker.DataComp->HandleFrameChanged(1);
		TestEqual(TEXT("proxy: no window"), Recorder->WindowBeginCount, 0);
		TestEqual(TEXT("proxy: no hits"), Recorder->HitConnectedCount, 0);
		TestFalse(TEXT("proxy: not armed"), Attacker.DataComp->IsAutoHitDetectionArmedForTests());
		TestEqual(TEXT("proxy: registered nothing"), Attacker.DataComp->GetProcessedHitsForTests().Num(), 0);
	}

	// Authority runs the full path.
	{
		FAutoHit_Rig Attacker = AutoHit_MakeRig(/*bEnableReplication=*/true);
		Attacker.DataComp->CharacterProfile = Asset;
		Attacker.DataComp->bAutoHitDetection = true;
		Attacker.FBComp->SetFlipbook(Slash);
		Attacker.DataComp->SetNetContextOverrideForTests(EPaper2DPlusNetContext::Authority);
		AutoHit_InjectVictimOverlap(Attacker, Victim, 9, 2);
		UPaper2DPlusAutoHitRecorder* Recorder = AutoHit_BindRecorder(Attacker);

		Attacker.DataComp->HandleFlipbookChanged(Slash);
		Attacker.DataComp->HandleFrameChanged(1);
		TestEqual(TEXT("authority: window began"), Recorder->WindowBeginCount, 1);
		TestEqual(TEXT("authority: hit connected"), Recorder->HitConnectedCount, 1);
		TestEqual(TEXT("authority: registered the dedup entry"), Attacker.DataComp->GetProcessedHitsForTests().Num(), 1);
	}

	// Feature off: zero activity even on attack frames.
	{
		FAutoHit_Rig Attacker = AutoHit_MakeRig();
		Attacker.DataComp->CharacterProfile = Asset;
		Attacker.FBComp->SetFlipbook(Slash);
		AutoHit_InjectVictimOverlap(Attacker, Victim, 9, 2);
		UPaper2DPlusAutoHitRecorder* Recorder = AutoHit_BindRecorder(Attacker);

		Attacker.DataComp->HandleFlipbookChanged(Slash);
		Attacker.DataComp->HandleFrameChanged(1);
		TestEqual(TEXT("off: no window"), Recorder->WindowBeginCount, 0);
		TestEqual(TEXT("off: no hits"), Recorder->HitConnectedCount, 0);
		TestEqual(TEXT("off: registered nothing"), Attacker.DataComp->GetProcessedHitsForTests().Num(), 0);
	}

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// External tick entry (the subsystem's armed-set path): TickAutoHitDetection
// re-checks while armed, shares the dedup ledger, and the direct-write toggle
// off disarms + closes the window from the tick.
// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusAutoHitTickEntry,
	"Paper2DPlus.HitDetection.Auto.SubsystemTickEntry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusAutoHitTickEntry::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	UPaperFlipbook* Slash = AutoHit_AddMove(Asset, TEXT("Slash"), 4);
	AutoHit_SetAttackFrame(Asset, 0, 1, 6, 1);

	FAutoHit_Rig Attacker = AutoHit_MakeRig();
	Attacker.DataComp->CharacterProfile = Asset;
	Attacker.DataComp->bAutoHitDetection = true;
	Attacker.FBComp->SetFlipbook(Slash);

	AActor* Victim1 = NewObject<AActor>();
	AActor* Victim2 = NewObject<AActor>();
	AutoHit_InjectVictimOverlap(Attacker, Victim1, 6, 1);
	UPaper2DPlusAutoHitRecorder* Recorder = AutoHit_BindRecorder(Attacker);

	Attacker.DataComp->HandleFlipbookChanged(Slash);
	Attacker.DataComp->HandleFrameChanged(1);
	TestEqual(TEXT("frame entry hit victim1"), Recorder->HitConnectedCount, 1);

	// A held frame: victim2 moves into range between key frames — the tick entry connects.
	AutoHit_InjectVictimOverlap(Attacker, Victim2, 6, 1);
	Attacker.DataComp->TickAutoHitDetection();
	TestEqual(TEXT("tick entry hit victim2"), Recorder->HitConnectedCount, 2);
	TestTrue(TEXT("victim2 is the latest hit"), Recorder->LastHit.Victim == Victim2);

	// Repeat ticks are dedup-safe.
	Attacker.DataComp->TickAutoHitDetection();
	TestEqual(TEXT("repeat tick dedups"), Recorder->HitConnectedCount, 2);

	// Direct-write toggle off mid-window: the next tick disarms + closes.
	Attacker.DataComp->bAutoHitDetection = false;
	Attacker.DataComp->TickAutoHitDetection();
	TestFalse(TEXT("toggle off disarmed"), Attacker.DataComp->IsAutoHitDetectionArmedForTests());
	TestEqual(TEXT("toggle off closed the window"), Recorder->WindowEndCount, 1);

	// Disarmed: further ticks are no-ops.
	Attacker.DataComp->TickAutoHitDetection();
	TestEqual(TEXT("disarmed tick is a no-op"), Recorder->WindowEndCount, 1);

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Loop wrap reopens the hit ledger. MoveInstanceCounter only advances on a
// FLIPBOOK change, so a looping attack that never switches flipbooks would
// rebuild the identical dedup key every pass and silently stop connecting after
// the first swing. A wrap is a fresh hit lifecycle exactly like it is a fresh
// Cue State lifecycle. Whiff bookkeeping stays per-move-instance, not per-loop.
// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusAutoHitLoopWrapReopensLedger,
	"Paper2DPlus.HitDetection.Auto.LoopWrapReopensHitLedger",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusAutoHitLoopWrapReopensLedger::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	UPaperFlipbook* SpinLoop = AutoHit_AddMove(Asset, TEXT("SpinLoop"), 3);
	// Both moves are added BEFORE the component caches anything: the component holds a raw
	// FFlipbookCombatData* into Asset->Flipbooks, so growing that array mid-test would dangle it.
	UPaperFlipbook* Idle = AutoHit_AddMove(Asset, TEXT("Idle"), 2);
	AutoHit_SetAttackFrame(Asset, 0, 1, /*Damage=*/5, /*Knockback=*/2);

	FAutoHit_Rig Attacker = AutoHit_MakeRig();
	Attacker.DataComp->CharacterProfile = Asset;
	Attacker.DataComp->bAutoHitDetection = true;
	Attacker.FBComp->SetFlipbook(SpinLoop);

	AActor* Victim = NewObject<AActor>();
	AutoHit_InjectVictimOverlap(Attacker, Victim, 5, 2);
	UPaper2DPlusAutoHitRecorder* Recorder = AutoHit_BindRecorder(Attacker);

	// Pass 1: frame 1 arms and connects; frame 2 disarms.
	Attacker.DataComp->HandleFlipbookChanged(SpinLoop);
	Attacker.DataComp->HandleFrameChanged(1);
	TestEqual(TEXT("pass 1 connected"), Recorder->HitConnectedCount, 1);
	Attacker.DataComp->HandleFrameChanged(2);
	TestEqual(TEXT("pass 1 closed the window"), Recorder->WindowEndCount, 1);

	// The loop wrap itself (frame 2 -> 0) — same flipbook, so no move-instance change.
	Attacker.DataComp->HandleFrameChanged(0);
	TestEqual(TEXT("wrap alone connects nothing"), Recorder->HitConnectedCount, 1);
	TestEqual(TEXT("wrap does not whiff mid-move"), Recorder->WhiffCount, 0);

	// Pass 2: the SAME victim must connect again — this is the regression.
	Attacker.DataComp->HandleFrameChanged(1);
	TestEqual(TEXT("pass 2 re-connects after the loop wrap"), Recorder->HitConnectedCount, 2);
	TestEqual(TEXT("pass 2 re-armed"), Recorder->WindowBeginCount, 2);
	TestTrue(TEXT("pass 2 hit the same victim"), Recorder->LastHit.Victim == Victim);

	// Pass 3 proves it is not a one-shot reopen.
	Attacker.DataComp->HandleFrameChanged(2);
	Attacker.DataComp->HandleFrameChanged(0);
	Attacker.DataComp->HandleFrameChanged(1);
	TestEqual(TEXT("pass 3 re-connects"), Recorder->HitConnectedCount, 3);

	// The move landed hits, so ending it must NOT whiff.
	Attacker.DataComp->HandleFlipbookChanged(Idle);
	TestEqual(TEXT("a move that connected never whiffs"), Recorder->WhiffCount, 0);

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// A receiver that destroys the attacker from inside its own hit broadcast must
// not leave the pass iterating the remaining overlaps on a dead component.
// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusAutoHitReceiverDestroysAttacker,
	"Paper2DPlus.HitDetection.Auto.ReceiverDestroyingAttackerStopsThePass",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusAutoHitReceiverDestroysAttacker::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	UPaperFlipbook* Burst = AutoHit_AddMove(Asset, TEXT("Burst"), 3);
	AutoHit_SetAttackFrame(Asset, 0, 1, /*Damage=*/9, /*Knockback=*/4);

	FAutoHit_Rig Attacker = AutoHit_MakeRig();
	Attacker.DataComp->CharacterProfile = Asset;
	Attacker.DataComp->bAutoHitDetection = true;
	Attacker.FBComp->SetFlipbook(Burst);

	// TWO victims overlap in the same pass; the first receiver kills the attacker.
	AActor* VictimA = NewObject<AActor>();
	AActor* VictimB = NewObject<AActor>();
	Attacker.DataComp->SetOverlapQueryOverrideForTests(
		[VictimA, VictimB](TArray<FHitboxCollisionResult>& Out)
		{
			for (AActor* V : { VictimA, VictimB })
			{
				FHitboxCollisionResult R;
				R.bHit = true;
				R.DefenderActor = V;
				R.Damage = 9;
				R.Knockback = 4;
				Out.Add(R);
			}
		});

	UPaper2DPlusAutoHitSelfDestructRecorder* Killer = NewObject<UPaper2DPlusAutoHitSelfDestructRecorder>();
	Killer->AttackerToKill = Attacker.DataComp;
	Attacker.DataComp->OnHitConnected.AddDynamic(
		Killer, &UPaper2DPlusAutoHitSelfDestructRecorder::OnHitConnected);

	Attacker.DataComp->HandleFlipbookChanged(Burst);
	Attacker.DataComp->HandleFrameChanged(1);

	// Exactly one broadcast: the pass bailed instead of processing VictimB on a dead component.
	TestEqual(TEXT("pass stops after the attacker dies"), Killer->HitConnectedCount, 1);

	return true;
}

#endif // !UE_BUILD_SHIPPING

#endif // WITH_EDITOR
