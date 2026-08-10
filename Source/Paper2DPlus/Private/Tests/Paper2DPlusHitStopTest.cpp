// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#if WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "Containers/Ticker.h"
#include "Paper2DPlusFrameCurve.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Paper2DPlusCharacterProfileComponent.h"
#include "Paper2DPlusSettings.h"
#include "Paper2DPlusTestTransitionTypes.h"
#include "FrameEvents/Paper2DPlusFrameEvent.h"
#include "PaperFlipbook.h"
#include "PaperFlipbookComponent.h"
#include "PaperSprite.h"
#include "GameFramework/Actor.h"

/** TASK-76 PR4 hit-stop tests (worldless). TriggerHitStop freezes the owner + an optional other actor
 *  via per-actor CustomTimeDilation (the HitStopFrozenDilation sentinel, never exactly 0) and restores
 *  each actor's PRIOR dilation after the duration of REAL time — driven by a core ticker, pumped here
 *  with FTSTicker::GetCoreTicker().Tick(Delta) (the worldless pump; other registered core tickers
 *  receive the same small deltas, so deltas stay small and assertions stay targeted). Hit-stop is
 *  EXPLICIT — games call TriggerHitStop directly (no auto-curve read). Retriggers EXTEND without re-capturing
 *  frozen dilations (the permanent-freeze bug, pinned); priors live in a shared REFCOUNTED registry so
 *  overlapping hit-stops from DIFFERENT components (trades) restore the TRUE priors when the LAST
 *  freezer ends (pinned), and the restore is sentinel-guarded so external mid-freeze dilation writes
 *  are respected (pinned). CancelHitStop restores early, is a safe no-op when inactive, and restores
 *  even from inside an OnHitStopBegin handler (the End broadcast is suppressed, Verbose — pinned).
 *  Helpers are HitStop_-prefixed per the unity-build file-unique-name rule. */

namespace
{
	/** Build a flipbook with NumFrames key frames (FPS 10, FrameRun 1 — frame i lives at i*0.1s). */
	UPaperFlipbook* HitStop_MakeFlipbook(UObject* Owner, int32 NumFrames)
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
	UPaperFlipbook* HitStop_AddMove(UPaper2DPlusCharacterProfileAsset* Asset, const FString& MoveName, int32 NumFrames)
	{
		FFlipbookProfileEntry Anim;
		Anim.Identity.FlipbookName = MoveName;
		UPaperFlipbook* FB = HitStop_MakeFlipbook(Asset, NumFrames);
		Anim.Identity.Flipbook = FB;
		Anim.CombatData.Frames.SetNum(NumFrames);
		Anim.CombatData.FrameExtractionInfo.SetNum(NumFrames);
		Asset->Flipbooks.Add(Anim);
		return FB;
	}

	/** Author an aux curve under ANY literal name on Asset->Flipbooks[MoveIdx] (the PR2 helper
	 *  Transition_AddCancelCurve composes "Cancel_<Category>" names — this file-local generalization
	 *  writes the given name verbatim so "HitStop" curves can be authored). */
	void HitStop_AddCurve(
		UPaper2DPlusCharacterProfileAsset* Asset, int32 MoveIdx, FName CurveName,
		const TArray<TPair<int32, float>>& Keys,
		EPaper2DPlusCurveInterp Mode = EPaper2DPlusCurveInterp::Constant)
	{
		FPaper2DPlusFrameCurve& Curve = Asset->Flipbooks[MoveIdx].CurveData.Curves.FindOrAdd(CurveName);
		Curve.SetMode(Mode);
		for (const TPair<int32, float>& Key : Keys)
		{
			Curve.SetKeyValue(Key.Key, Key.Value);
		}
	}

	/** Worldless attacker rig: actor + stock flipbook component + profile component, wired and warmed. */
	struct FHitStop_Rig
	{
		AActor* Attacker = nullptr;
		UPaperFlipbookComponent* FBComp = nullptr;
		UPaper2DPlusCharacterProfileComponent* DataComp = nullptr;
	};

	FHitStop_Rig HitStop_MakeRig(UPaper2DPlusCharacterProfileAsset* Asset, UPaperFlipbook* StartFlipbook)
	{
		FHitStop_Rig Rig;
		Rig.Attacker = NewObject<AActor>();
		Rig.FBComp = NewObject<UPaperFlipbookComponent>(Rig.Attacker);
		Rig.DataComp = NewObject<UPaper2DPlusCharacterProfileComponent>(Rig.Attacker);
		Rig.Attacker->AddOwnedComponent(Rig.FBComp);
		Rig.Attacker->AddOwnedComponent(Rig.DataComp);
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

	/** The frozen dilation TriggerHitStop applies — THE component's freeze sentinel (small non-zero
	 *  so dt-dividing components can't NaN; the restore is guarded on this exact value). */
	constexpr float HitStop_FrozenDilation = UPaper2DPlusCharacterProfileComponent::HitStopFrozenDilation;
}

// TriggerHitStop freezes attacker + victim (capturing DIFFERENT priors), holds across a partial real-time
// pump, and restores each actor's exact prior on expiry with one Begin and one End broadcast.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusHitStopTriggerFreezesAndRestores,
	"Paper2DPlus.HitStop.TriggerFreezesAndRestores",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusHitStopTriggerFreezesAndRestores::RunTest(const FString& Parameters)
{
	FHitStop_Rig Rig = HitStop_MakeRig(nullptr, nullptr); // direct-trigger path needs no profile
	Rig.Attacker->CustomTimeDilation = 1.0f;
	AActor* Victim = NewObject<AActor>();
	Victim->CustomTimeDilation = 0.5f; // distinct prior — restore must hit EXACTLY this, not 1.0

	UPaper2DPlusHitStopRecorder* Recorder = NewObject<UPaper2DPlusHitStopRecorder>();
	Rig.DataComp->OnHitStopBegin.AddDynamic(Recorder, &UPaper2DPlusHitStopRecorder::OnHitStopBegin);
	Rig.DataComp->OnHitStopEnd.AddDynamic(Recorder, &UPaper2DPlusHitStopRecorder::OnHitStopEnd);

	Rig.DataComp->TriggerHitStop(Victim, 0.1f);

	TestEqual(TEXT("Attacker is frozen to the small non-zero dilation"),
		Rig.Attacker->CustomTimeDilation, HitStop_FrozenDilation, 1.e-6f);
	TestEqual(TEXT("Victim is frozen to the small non-zero dilation"),
		Victim->CustomTimeDilation, HitStop_FrozenDilation, 1.e-6f);
	TestTrue(TEXT("IsHitStopActive is true while frozen"), Rig.DataComp->IsHitStopActive());
	TestEqual(TEXT("Begin broadcast exactly once"), Recorder->BeginCount, 1);
	TestTrue(TEXT("Begin payload carries the victim"), Recorder->LastVictim.Get() == Victim);
	TestTrue(TEXT("Begin payload carries the duration"),
		FMath::IsNearlyEqual(Recorder->LastDurationSeconds, 0.1f, 1.e-3f));
	TestEqual(TEXT("No End broadcast while frozen"), Recorder->EndCount, 0);

	// Partial pump: 0.05 of 0.1 REAL seconds elapsed — still frozen.
	FTSTicker::GetCoreTicker().Tick(0.05f);
	TestEqual(TEXT("Attacker still frozen at 0.05s"),
		Rig.Attacker->CustomTimeDilation, HitStop_FrozenDilation, 1.e-6f);
	TestEqual(TEXT("Victim still frozen at 0.05s"),
		Victim->CustomTimeDilation, HitStop_FrozenDilation, 1.e-6f);
	TestTrue(TEXT("Still active at 0.05s"), Rig.DataComp->IsHitStopActive());

	// Expiry pump: 0.05 + 0.06 = 0.11 > 0.1 — restored EXACTLY to each actor's prior.
	FTSTicker::GetCoreTicker().Tick(0.06f);
	TestEqual(TEXT("Attacker restored EXACTLY to its prior dilation 1.0"),
		Rig.Attacker->CustomTimeDilation, 1.0f, 1.e-6f);
	TestEqual(TEXT("Victim restored EXACTLY to its prior dilation 0.5"),
		Victim->CustomTimeDilation, 0.5f, 1.e-6f);
	TestFalse(TEXT("Inactive after expiry"), Rig.DataComp->IsHitStopActive());
	TestEqual(TEXT("End broadcast exactly once"), Recorder->EndCount, 1);
	TestEqual(TEXT("Begin count unchanged by expiry"), Recorder->BeginCount, 1);

	return true;
}

// Re-triggering while active EXTENDS the freeze (still one Begin) WITHOUT re-capturing the frozen ~0
// dilation as the saved prior — expiry restores the ORIGINAL priors (pins the frozen-dilation-recapture
// permanent-freeze bug); a SECOND victim added mid-freeze freezes and restores with everyone.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusHitStopRetriggerExtendsNotRecaptures,
	"Paper2DPlus.HitStop.RetriggerExtendsNotRecaptures",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusHitStopRetriggerExtendsNotRecaptures::RunTest(const FString& Parameters)
{
	FHitStop_Rig Rig = HitStop_MakeRig(nullptr, nullptr);
	Rig.Attacker->CustomTimeDilation = 1.0f;
	AActor* Victim = NewObject<AActor>();
	Victim->CustomTimeDilation = 0.5f;
	AActor* SecondVictim = NewObject<AActor>();
	SecondVictim->CustomTimeDilation = 0.75f;

	UPaper2DPlusHitStopRecorder* Recorder = NewObject<UPaper2DPlusHitStopRecorder>();
	Rig.DataComp->OnHitStopBegin.AddDynamic(Recorder, &UPaper2DPlusHitStopRecorder::OnHitStopBegin);
	Rig.DataComp->OnHitStopEnd.AddDynamic(Recorder, &UPaper2DPlusHitStopRecorder::OnHitStopEnd);

	Rig.DataComp->TriggerHitStop(Victim, 0.1f);
	TestEqual(TEXT("First trigger broadcasts Begin once"), Recorder->BeginCount, 1);

	FTSTicker::GetCoreTicker().Tick(0.05f); // 0.05 of 0.1 elapsed

	// Re-trigger with the SAME victim: extend to a fresh 0.1, no second Begin, no prior re-capture.
	Rig.DataComp->TriggerHitStop(Victim, 0.1f);
	TestEqual(TEXT("Re-trigger does NOT re-broadcast Begin"), Recorder->BeginCount, 1);
	TestTrue(TEXT("Still active after the re-trigger"), Rig.DataComp->IsHitStopActive());

	// +0.06 → 0.11 elapsed since the FIRST trigger (> its 0.1): only the extension keeps this frozen.
	FTSTicker::GetCoreTicker().Tick(0.06f);
	TestTrue(TEXT("Extension holds the freeze past the original duration (+0.11 total)"),
		Rig.DataComp->IsHitStopActive());
	TestEqual(TEXT("Victim still frozen past the original duration"),
		Victim->CustomTimeDilation, HitStop_FrozenDilation, 1.e-6f);

	// A SECOND victim added mid-freeze gets frozen (and extends the remaining time again).
	Rig.DataComp->TriggerHitStop(SecondVictim, 0.1f);
	TestEqual(TEXT("Second victim freezes mid-freeze"),
		SecondVictim->CustomTimeDilation, HitStop_FrozenDilation, 1.e-6f);
	TestEqual(TEXT("Adding a victim does NOT re-broadcast Begin"), Recorder->BeginCount, 1);

	// Expire — everyone restores to their ORIGINAL prior (a recapture bug would restore ~0).
	FTSTicker::GetCoreTicker().Tick(0.11f);
	TestFalse(TEXT("Inactive after expiry"), Rig.DataComp->IsHitStopActive());
	TestEqual(TEXT("Attacker restored to its ORIGINAL prior 1.0"),
		Rig.Attacker->CustomTimeDilation, 1.0f, 1.e-6f);
	TestEqual(TEXT("Victim restored to its ORIGINAL prior 0.5 (not the recaptured frozen value)"),
		Victim->CustomTimeDilation, 0.5f, 1.e-6f);
	TestEqual(TEXT("Second victim restored to its ORIGINAL prior 0.75"),
		SecondVictim->CustomTimeDilation, 0.75f, 1.e-6f);
	TestEqual(TEXT("One End broadcast for the whole extended freeze"), Recorder->EndCount, 1);

	return true;
}

// CancelHitStop restores immediately mid-freeze (one End), is a safe no-op when already inactive, and
// fully retires the ticker — further real-time pumps change nothing.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusHitStopCancelRestoresEarly,
	"Paper2DPlus.HitStop.CancelRestoresEarly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusHitStopCancelRestoresEarly::RunTest(const FString& Parameters)
{
	FHitStop_Rig Rig = HitStop_MakeRig(nullptr, nullptr);
	Rig.Attacker->CustomTimeDilation = 1.0f;
	AActor* Victim = NewObject<AActor>();
	Victim->CustomTimeDilation = 0.5f;

	UPaper2DPlusHitStopRecorder* Recorder = NewObject<UPaper2DPlusHitStopRecorder>();
	Rig.DataComp->OnHitStopBegin.AddDynamic(Recorder, &UPaper2DPlusHitStopRecorder::OnHitStopBegin);
	Rig.DataComp->OnHitStopEnd.AddDynamic(Recorder, &UPaper2DPlusHitStopRecorder::OnHitStopEnd);

	Rig.DataComp->TriggerHitStop(Victim, 0.2f);
	FTSTicker::GetCoreTicker().Tick(0.05f); // mid-freeze (0.05 of 0.2)

	Rig.DataComp->CancelHitStop();
	TestEqual(TEXT("Cancel restores the attacker immediately"),
		Rig.Attacker->CustomTimeDilation, 1.0f, 1.e-6f);
	TestEqual(TEXT("Cancel restores the victim immediately"),
		Victim->CustomTimeDilation, 0.5f, 1.e-6f);
	TestFalse(TEXT("Inactive after the cancel"), Rig.DataComp->IsHitStopActive());
	TestEqual(TEXT("End broadcast exactly once"), Recorder->EndCount, 1);

	// Second cancel while inactive = safe no-op.
	Rig.DataComp->CancelHitStop();
	TestEqual(TEXT("Cancel-when-inactive is a no-op (End count unchanged)"), Recorder->EndCount, 1);

	// The ticker is retired: further pumps fire nothing and touch nothing.
	FTSTicker::GetCoreTicker().Tick(0.1f);
	FTSTicker::GetCoreTicker().Tick(0.1f);
	TestEqual(TEXT("Post-cancel pumps leave the attacker untouched"),
		Rig.Attacker->CustomTimeDilation, 1.0f, 1.e-6f);
	TestEqual(TEXT("Post-cancel pumps leave the victim untouched"),
		Victim->CustomTimeDilation, 0.5f, 1.e-6f);
	TestEqual(TEXT("Post-cancel pumps broadcast no further End"), Recorder->EndCount, 1);
	TestEqual(TEXT("Post-cancel pumps broadcast no further Begin"), Recorder->BeginCount, 1);
	TestFalse(TEXT("Still inactive after the pumps"), Rig.DataComp->IsHitStopActive());

	return true;
}

// A null OtherActor freezes the attacker ONLY (bystanders untouched) and restores it on expiry.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusHitStopNullVictimFreezesAttackerOnly,
	"Paper2DPlus.HitStop.NullVictimFreezesAttackerOnly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusHitStopNullVictimFreezesAttackerOnly::RunTest(const FString& Parameters)
{
	FHitStop_Rig Rig = HitStop_MakeRig(nullptr, nullptr); // direct-trigger path needs no profile
	Rig.Attacker->CustomTimeDilation = 1.0f;
	AActor* Bystander = NewObject<AActor>(); // never passed in — its dilation pins "attacker only"
	Bystander->CustomTimeDilation = 0.5f;

	UPaper2DPlusHitStopRecorder* Recorder = NewObject<UPaper2DPlusHitStopRecorder>();
	Rig.DataComp->OnHitStopBegin.AddDynamic(Recorder, &UPaper2DPlusHitStopRecorder::OnHitStopBegin);
	Rig.DataComp->OnHitStopEnd.AddDynamic(Recorder, &UPaper2DPlusHitStopRecorder::OnHitStopEnd);

	// Direct trigger with a null victim: attacker-only freeze.
	Rig.DataComp->TriggerHitStop(nullptr, 0.1f);
	TestEqual(TEXT("Null victim: attacker freezes"),
		Rig.Attacker->CustomTimeDilation, HitStop_FrozenDilation, 1.e-6f);
	TestEqual(TEXT("Null victim: the bystander is untouched"),
		Bystander->CustomTimeDilation, 0.5f, 1.e-6f);
	TestTrue(TEXT("Null victim: active"), Rig.DataComp->IsHitStopActive());
	TestEqual(TEXT("Null victim: Begin broadcast once"), Recorder->BeginCount, 1);
	TestFalse(TEXT("Null victim: Begin payload Victim is null"), Recorder->LastVictim.IsValid());

	FTSTicker::GetCoreTicker().Tick(0.11f); // expire
	TestEqual(TEXT("Null victim: attacker restored on expiry"),
		Rig.Attacker->CustomTimeDilation, 1.0f, 1.e-6f);
	TestFalse(TEXT("Null victim: inactive after expiry"), Rig.DataComp->IsHitStopActive());
	TestEqual(TEXT("Null victim: End broadcast once"), Recorder->EndCount, 1);

	return true;
}

// THE cross-component regression pin (the fighting-game TRADE): two actors, each with its OWN profile
// component, freeze EACH OTHER in the same game frame. The shared refcounted registry must (a) keep
// both actors frozen until the LAST overlapping hit-stop ends, and (b) restore the TRUE pre-freeze
// priors — never the freeze sentinel a second component would have captured as a "prior" (the
// permanent-freeze soft-lock both reviews flagged as the PR4 blocker).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusHitStopTradeRestoresBothActors,
	"Paper2DPlus.HitStop.TradeRestoresBothActors",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusHitStopTradeRestoresBothActors::RunTest(const FString& Parameters)
{
	// Two full rigs — distinct priors set BEFORE any freeze so a sentinel-as-prior recapture is visible.
	FHitStop_Rig RigA = HitStop_MakeRig(nullptr, nullptr);
	FHitStop_Rig RigB = HitStop_MakeRig(nullptr, nullptr);
	RigA.Attacker->CustomTimeDilation = 1.0f;
	RigB.Attacker->CustomTimeDilation = 0.75f;

	// The trade: both hits resolve in the same game frame, each component freezing BOTH actors.
	RigA.DataComp->TriggerHitStop(RigB.Attacker, 0.1f);
	RigB.DataComp->TriggerHitStop(RigA.Attacker, 0.15f);

	TestEqual(TEXT("Trade: actor A frozen to the sentinel"),
		RigA.Attacker->CustomTimeDilation, HitStop_FrozenDilation, 1.e-6f);
	TestEqual(TEXT("Trade: actor B frozen to the sentinel"),
		RigB.Attacker->CustomTimeDilation, HitStop_FrozenDilation, 1.e-6f);
	TestTrue(TEXT("Trade: component A active"), RigA.DataComp->IsHitStopActive());
	TestTrue(TEXT("Trade: component B active"), RigB.DataComp->IsHitStopActive());

	// 0.11 > A's 0.1: A's hit-stop expires first, but B's overlapping hit-stop still holds BOTH actors.
	FTSTicker::GetCoreTicker().Tick(0.11f);
	TestFalse(TEXT("Component A's hit-stop expired at 0.11s"), RigA.DataComp->IsHitStopActive());
	TestTrue(TEXT("Component B's hit-stop still runs at 0.11s"), RigB.DataComp->IsHitStopActive());
	TestEqual(TEXT("Actor A STILL frozen at 0.11s (refcount holds until the LAST overlapping hit-stop ends)"),
		RigA.Attacker->CustomTimeDilation, HitStop_FrozenDilation, 1.e-6f);
	TestEqual(TEXT("Actor B STILL frozen at 0.11s (refcount holds until the LAST overlapping hit-stop ends)"),
		RigB.Attacker->CustomTimeDilation, HitStop_FrozenDilation, 1.e-6f);

	// 0.11 + 0.06 = 0.17 > B's 0.15: the LAST freezer ends — both restore to their TRUE priors.
	FTSTicker::GetCoreTicker().Tick(0.06f);
	TestEqual(TEXT("Actor A restored to its TRUE prior 1.0 (no sentinel residue)"),
		RigA.Attacker->CustomTimeDilation, 1.0f, 1.e-6f);
	TestEqual(TEXT("Actor B restored to its TRUE prior 0.75 (no sentinel residue)"),
		RigB.Attacker->CustomTimeDilation, 0.75f, 1.e-6f);
	TestFalse(TEXT("Component A inactive after the trade resolves"), RigA.DataComp->IsHitStopActive());
	TestFalse(TEXT("Component B inactive after the trade resolves"), RigB.DataComp->IsHitStopActive());

	return true;
}

// External CustomTimeDilation writes DURING the freeze are respected: the final restore is guarded on
// the actor still carrying the exact freeze sentinel, so a mid-freeze game write (slow-mo on death, a
// cutscene ramp) survives the hit-stop expiry instead of being stomped back to the captured prior.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusHitStopExternalDilationWriteRespected,
	"Paper2DPlus.HitStop.ExternalDilationWriteRespected",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusHitStopExternalDilationWriteRespected::RunTest(const FString& Parameters)
{
	FHitStop_Rig Rig = HitStop_MakeRig(nullptr, nullptr);
	Rig.Attacker->CustomTimeDilation = 1.0f;
	AActor* Victim = NewObject<AActor>();
	Victim->CustomTimeDilation = 1.0f;

	Rig.DataComp->TriggerHitStop(Victim, 0.1f);
	TestEqual(TEXT("Victim frozen to the sentinel before the external write"),
		Victim->CustomTimeDilation, HitStop_FrozenDilation, 1.e-6f);

	// Mid-freeze external write: game code claims the dilation channel while the freeze is active.
	Victim->CustomTimeDilation = 0.3f;

	FTSTicker::GetCoreTicker().Tick(0.11f); // expire
	TestFalse(TEXT("Inactive after expiry"), Rig.DataComp->IsHitStopActive());
	TestEqual(TEXT("Victim KEEPS the external mid-freeze write 0.3 (sentinel-guarded restore)"),
		Victim->CustomTimeDilation, 0.3f, 1.e-6f);
	TestEqual(TEXT("Attacker restores normally to its prior 1.0"),
		Rig.Attacker->CustomTimeDilation, 1.0f, 1.e-6f);

	return true;
}

// CancelHitStop issued from INSIDE an OnHitStopBegin handler (the dying-attacker EndPlay rescue shape)
// restores immediately: by the time TriggerHitStop returns, every dilation is back to its prior, the
// component is inactive, and the ticker is retired. Only the OnHitStopEnd BROADCAST is suppressed
// (logged Verbose — no expected-message whitelist needed).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusHitStopCancelDuringBeginBroadcastRestores,
	"Paper2DPlus.HitStop.CancelDuringBeginBroadcastRestores",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusHitStopCancelDuringBeginBroadcastRestores::RunTest(const FString& Parameters)
{
	FHitStop_Rig Rig = HitStop_MakeRig(nullptr, nullptr);
	Rig.Attacker->CustomTimeDilation = 1.0f;
	AActor* Victim = NewObject<AActor>();
	Victim->CustomTimeDilation = 0.5f;

	UPaper2DPlusHitStopRecorder* Recorder = NewObject<UPaper2DPlusHitStopRecorder>();
	Recorder->CancelTarget = Rig.DataComp;
	Recorder->bCancelOnBegin = true; // the Begin handler cancels its own hit-stop once
	Rig.DataComp->OnHitStopBegin.AddDynamic(Recorder, &UPaper2DPlusHitStopRecorder::OnHitStopBegin);
	Rig.DataComp->OnHitStopEnd.AddDynamic(Recorder, &UPaper2DPlusHitStopRecorder::OnHitStopEnd);

	Rig.DataComp->TriggerHitStop(Victim, 0.2f);

	TestEqual(TEXT("Begin broadcast once (the cancelling handler observed the freeze)"),
		Recorder->BeginCount, 1);
	TestEqual(TEXT("Attacker restored by the time TriggerHitStop returns"),
		Rig.Attacker->CustomTimeDilation, 1.0f, 1.e-6f);
	TestEqual(TEXT("Victim restored by the time TriggerHitStop returns"),
		Victim->CustomTimeDilation, 0.5f, 1.e-6f);
	TestFalse(TEXT("Inactive after the in-Begin cancel"), Rig.DataComp->IsHitStopActive());
	TestEqual(TEXT("OnHitStopEnd broadcast suppressed (EndCount stays 0)"), Recorder->EndCount, 0);

	// The cancel retired the ticker: further real-time pumps fire nothing and touch nothing.
	FTSTicker::GetCoreTicker().Tick(0.25f);
	TestEqual(TEXT("Post-cancel pump leaves the attacker untouched"),
		Rig.Attacker->CustomTimeDilation, 1.0f, 1.e-6f);
	TestEqual(TEXT("Post-cancel pump leaves the victim untouched"),
		Victim->CustomTimeDilation, 0.5f, 1.e-6f);
	TestEqual(TEXT("Post-cancel pump fires no late End broadcast"), Recorder->EndCount, 0);
	TestEqual(TEXT("Post-cancel pump fires no second Begin broadcast"), Recorder->BeginCount, 1);

	return true;
}

// Validation (TASK-76 PR4): a HitStop curve peaking above 60 frames draws exactly one Info-severity
// issue ("frames" in the message — the typo'd-800-instead-of-8 multi-second-freeze sanity check);
// a normal peak (8) draws none. Info never flips the bool return.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusHitStopValidationLongHitStopInfo,
	"Paper2DPlus.HitStop.Validation.LongHitStopInfo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusHitStopValidationLongHitStopInfo::RunTest(const FString& Parameters)
{
	const auto CountLongHitStopInfos = [](const TArray<FCharacterProfileValidationIssue>& Issues)
	{
		int32 Count = 0;
		for (const FCharacterProfileValidationIssue& Issue : Issues)
		{
			if (Issue.Severity == ECharacterProfileValidationSeverity::Info
				&& Issue.Message.Contains(TEXT("frames")))
			{
				++Count;
			}
		}
		return Count;
	};

	// Peak 120 (2 seconds at the default 60 fps rate): exactly one Info mentioning "frames".
	UPaper2DPlusCharacterProfileAsset* LongAsset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	HitStop_AddMove(LongAsset, TEXT("Cinematic"), 4);
	HitStop_AddCurve(LongAsset, 0, TEXT("HitStop"),
		{ TPair<int32, float>(0, 0.f), TPair<int32, float>(2, 120.f) });

	TArray<FCharacterProfileValidationIssue> LongIssues;
	TestTrue(TEXT("Peak 120: validation still returns true (Info never flips the result)"),
		LongAsset->ValidateCharacterProfileAsset(LongIssues));
	const int32 LongInfoCount = CountLongHitStopInfos(LongIssues);
	TestEqual(TEXT("Peak 120: exactly one Info issue mentioning 'frames'"), LongInfoCount, 1);

	// Peak 8 (a normal fighting-game hit-stop): no such issue.
	UPaper2DPlusCharacterProfileAsset* ShortAsset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	HitStop_AddMove(ShortAsset, TEXT("Jab"), 4);
	HitStop_AddCurve(ShortAsset, 0, TEXT("HitStop"),
		{ TPair<int32, float>(0, 0.f), TPair<int32, float>(2, 8.f) });

	TArray<FCharacterProfileValidationIssue> ShortIssues;
	TestTrue(TEXT("Peak 8: validation returns true"),
		ShortAsset->ValidateCharacterProfileAsset(ShortIssues));
	const int32 ShortInfoCount = CountLongHitStopInfos(ShortIssues);
	TestEqual(TEXT("Peak 8: no long-HitStop Info issue"), ShortInfoCount, 0);

	return true;
}

// TASK-74.1 semantic validation: HitStop is authored as non-negative whole-frame counts.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusHitStopValidationSemanticValues,
	"Paper2DPlus.HitStop.Validation.SemanticValues",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusHitStopValidationSemanticValues::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	HitStop_AddMove(Asset, TEXT("BadHitStop"), 4);
	HitStop_AddCurve(Asset, 0, TEXT("HitStop"),
		{ TPair<int32, float>(0, -2.f), TPair<int32, float>(2, 3.5f) });

	TArray<FCharacterProfileValidationIssue> Issues;
	TestTrue(TEXT("Negative HitStop warning is advisory, not a blocking validation error"),
		Asset->ValidateCharacterProfileAsset(Issues));

	int32 NegativeWarningCount = 0;
	int32 FractionalInfoCount = 0;
	for (const FCharacterProfileValidationIssue& Issue : Issues)
	{
		if (Issue.Severity == ECharacterProfileValidationSeverity::Warning
			&& Issue.Message.Contains(TEXT("negative value")))
		{
			++NegativeWarningCount;
		}
		if (Issue.Severity == ECharacterProfileValidationSeverity::Info
			&& Issue.Message.Contains(TEXT("fractional")))
		{
			++FractionalInfoCount;
		}
	}

	TestEqual(TEXT("One negative HitStop warning"), NegativeWarningCount, 1);
	TestEqual(TEXT("One fractional HitStop info"), FractionalInfoCount, 1);

	return true;
}

#endif // WITH_EDITOR
