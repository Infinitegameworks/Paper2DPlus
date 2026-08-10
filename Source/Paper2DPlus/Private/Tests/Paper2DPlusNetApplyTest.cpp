// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#if WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "Paper2DPlusNetTypes.h"
#include "Paper2DPlusNetGating.h"
#include "Paper2DPlusMoveTransition.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Paper2DPlusCharacterProfileComponent.h"
#include "Paper2DPlusNetTestTypes.h"
#include "Paper2DPlusTestFrameCueTypes.h"
#include "PaperFlipbook.h"
#include "PaperFlipbookComponent.h"
#include "PaperSprite.h"
#include "GameFramework/Actor.h"
#include "Net/UnrealNetwork.h"

/** TASK-57 U8 apply-mode / catch-up-warm / drift-clock / root-motion-authority / gate-latch tests
 *  (worldless, driven through the U1/U2/U8 test seams: SetNetContextOverrideForTests /
 *  SetServerTimeOverrideForTests / the U2 accessors + the apply-mode opt-in flags). These pin the
 *  proxy-behavior half split out of U2: authority-only root motion + sprite offset (proxies advance
 *  baselines without moving), ConsumeRootMotionDelta's zero-on-proxy contract, apply-mode flipbook
 *  reconstruction from the server anchor with no one-shot replay, the gate-latch (Begin/End symmetry
 *  across mid-range context flips), the locally-driven drift corrector + authority stale-detect
 *  re-anchor, RepublishAnimState's authority-only re-anchor, and the single-player invariance of all
 *  of it. Helpers are NetApply_-prefixed per the unity-build file-unique-name rule (the sibling
 *  Paper2DPlusNetAnimStateTest.cpp already owns NetAnim_*). */

namespace
{
	/** Server-clock base used by the rigs (any constant works — anchors are relative). */
	constexpr double NetApply_BaseServerTime = 1000.0;

	/** Build a flipbook with NumFrames key frames (FPS 10, FrameRun 1 — frame i lives at i*0.1s,
	 *  total duration NumFrames*0.1s). Mirrors NetAnim_MakeFlipbook. */
	UPaperFlipbook* NetApply_MakeFlipbook(UObject* Owner, int32 NumFrames)
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

	/** Add a flipbook entry named MoveName with a NumFrames-key flipbook; sizes the per-frame combat /
	 *  motion arrays so root-motion and sprite-offset authoring has somewhere to land. Returns the live
	 *  flipbook. Mirrors NetAnim_AddMove but ALSO grows MotionData.RootMotion (NetAnim_AddMove omits it). */
	UPaperFlipbook* NetApply_AddMove(UPaper2DPlusCharacterProfileAsset* Asset, const FString& MoveName, int32 NumFrames)
	{
		FFlipbookProfileEntry Anim;
		Anim.Identity.FlipbookName = MoveName;
		UPaperFlipbook* FB = NetApply_MakeFlipbook(Asset, NumFrames);
		Anim.Identity.Flipbook = FB;
		Anim.CombatData.Frames.SetNum(NumFrames);
		Anim.CombatData.FrameExtractionInfo.SetNum(NumFrames);
		Anim.MotionData.RootMotion.SetNum(NumFrames);
		Asset->Flipbooks.Add(Anim);
		return FB;
	}

	/** Author a non-trivial root-motion trajectory on the asset's last-added move: each frame i gets
	 *  a strictly-increasing X position so consecutive frames produce a non-zero per-frame delta. */
	void NetApply_AuthorRootMotion(UPaper2DPlusCharacterProfileAsset* Asset, int32 EntryIndex, float PerFramePixelsX)
	{
		FFlipbookProfileEntry& Entry = Asset->Flipbooks[EntryIndex];
		for (int32 i = 0; i < Entry.MotionData.RootMotion.Num(); ++i)
		{
			Entry.MotionData.RootMotion[i].Position = FVector2D(PerFramePixelsX * static_cast<float>(i + 1), 0.0f);
		}
	}

	/** Author a non-trivial sprite offset on a single frame so the sprite-offset apply site has work. */
	void NetApply_AuthorSpriteOffset(UPaper2DPlusCharacterProfileAsset* Asset, int32 EntryIndex, int32 FrameIndex, FIntPoint Offset)
	{
		FFlipbookProfileEntry& Entry = Asset->Flipbooks[EntryIndex];
		if (Entry.CombatData.FrameExtractionInfo.IsValidIndex(FrameIndex))
		{
			Entry.CombatData.FrameExtractionInfo[FrameIndex].SpriteOffset = Offset;
		}
	}

	/** Worldless rig: actor + stock flipbook component + profile component, wired. Mirrors NetAnim_MakeRig. */
	struct FNetApply_Rig
	{
		AActor* Owner = nullptr;
		UPaperFlipbookComponent* FBComp = nullptr;
		UPaper2DPlusCharacterProfileComponent* DataComp = nullptr;
	};

	FNetApply_Rig NetApply_MakeRig(bool bEnableReplication = true)
	{
		FNetApply_Rig Rig;
		Rig.Owner = NewObject<AActor>();
		Rig.FBComp = NewObject<UPaperFlipbookComponent>(Rig.Owner);
		Rig.DataComp = NewObject<UPaper2DPlusCharacterProfileComponent>(Rig.Owner);
		Rig.Owner->AddOwnedComponent(Rig.FBComp);
		Rig.Owner->AddOwnedComponent(Rig.DataComp);
		Rig.DataComp->FlipbookComponent = Rig.FBComp;
		// Worldless: never BeginPlays, so ResolveNetContext reads the live flag. Keeps the
		// archetype-mismatch diagnostic quiet on rigs that drive OnReps.
		Rig.DataComp->bEnableReplication = bEnableReplication;
		return Rig;
	}

	UPaper2DPlusNetAnimStateRecorder* NetApply_BindRecorder(FNetApply_Rig& Rig)
	{
		UPaper2DPlusNetAnimStateRecorder* Recorder = NewObject<UPaper2DPlusNetAnimStateRecorder>();
		Rig.DataComp->OnReplicatedAnimStateChanged.AddDynamic(Recorder, &UPaper2DPlusNetAnimStateRecorder::OnReplicatedAnimState);
		return Recorder;
	}
}

// Everything below drives the !UE_BUILD_SHIPPING test seams.
#if !UE_BUILD_SHIPPING

// ════════════════════════════════════════════════════════════════════════════
// A. ROOT MOTION (Paper2DPlus.Network.RootMotion.*)
// ════════════════════════════════════════════════════════════════════════════

// ─────────────────────────────────────────────────────────────────────────────
// ProxyAdvancesBaselineWithoutMoving: a simulated proxy running bAutoApplyRootMotion
// advances the per-frame baseline (the WORLDLESS-observable consequence of the
// authority gate — GetRootMotionDelta reads the advanced baseline, no world needed).
//
// The actual actor DISPLACEMENT distinction (proxy stays put / authority moves)
// requires a UWorld: ApplyRootMotionWorldDelta early-returns when !Owner->GetWorld()
// and AddActorWorldOffset needs a world, so a worldless NewObject<AActor>() rig can
// NEVER move regardless of context. That distinction is covered by the U7 PIE matrix;
// here we assert ONLY the gating + baseline, which IS worldless-observable.
//
// NOTE on frame timing: the rig's flipbook is FPS 10 / FrameRun 1, so key frame N
// spans [N*0.1, (N+1)*0.1). GetRootMotionDelta reads GetKeyFrameIndexAtTime(
// GetPlaybackPosition()) — landing EXACTLY on a frame boundary (e.g. 0.2) is an
// ambiguous </>= edge, so we seek to the MID of each frame (N*0.1 + 0.05) to read
// frame N unambiguously, matching the sibling gating test's convention.
// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusNetApplyProxyAdvancesBaselineWithoutMoving,
	"Paper2DPlus.Network.RootMotion.ProxyAdvancesBaselineWithoutMoving",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusNetApplyProxyAdvancesBaselineWithoutMoving::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	UPaperFlipbook* FB = NetApply_AddMove(Asset, TEXT("Lunge"), 4);
	NetApply_AuthorRootMotion(Asset, 0, /*PerFramePixelsX=*/16.0f);

	// Proxy: drives frames; the baseline advances (worldless-observable via GetRootMotionDelta).
	{
		FNetApply_Rig Rig = NetApply_MakeRig();
		Rig.DataComp->SetNetContextOverrideForTests(EPaper2DPlusNetContext::SimulatedProxy);
		Rig.DataComp->SetServerTimeOverrideForTests(NetApply_BaseServerTime);
		Rig.DataComp->bAutoApplyRootMotion = true;
		Rig.DataComp->CharacterProfile = Asset;

		Rig.FBComp->SetFlipbook(FB);
		Rig.DataComp->HandleFlipbookChanged(FB); // warm + frame-0 dispatch
		// Drive a couple frame steps — the baseline advances to frame 2's authored position.
		Rig.DataComp->HandleFrameChanged(1);
		Rig.DataComp->HandleFrameChanged(2);

		// actor displacement (proxy stays put) requires a world — covered by the U7 PIE matrix;
		// worldless asserts the gating + baseline only.

		// GetRootMotionDelta is the CURRENT frame's pos minus the advanced baseline. After driving to
		// frame 2 (the baseline last advanced to frame 2's position), the const peek at frame 2 is zero
		// — proof the baseline tracked the move (a meaningful advisory, not an accumulating lie).
		Rig.FBComp->SetPlaybackPosition(2 * 0.1f + 0.05f, false); // mid-frame 2
		const FVector PeekAtCurrent = Rig.DataComp->GetRootMotionDelta();
		TestTrue(TEXT("baseline advanced to the current frame (delta zero at the settled frame)"),
			PeekAtCurrent.IsNearlyZero());
		// Peeking the NEXT frame's delta is a real non-zero per-frame step (16px → world units),
		// confirming the trajectory is alive on the proxy.
		Rig.FBComp->SetPlaybackPosition(3 * 0.1f + 0.05f, false); // mid-frame 3
		const FVector PeekAtNext = Rig.DataComp->GetRootMotionDelta();
		TestFalse(TEXT("a per-frame advisory delta exists (baseline is per-frame, not a total)"),
			PeekAtNext.IsNearlyZero());
	}

	// Authority: the SAME script advances the baseline identically (the const peek is the same
	// worldless observable). The authority world-delta APPLICATION (actor actually moves) requires a
	// world — covered by the U7 PIE matrix; here we pin only that the gate did NOT zero the proxy's
	// advisory delta, i.e. authority's GetRootMotionDelta is also a live per-frame value.
	{
		FNetApply_Rig Rig = NetApply_MakeRig();
		Rig.DataComp->SetNetContextOverrideForTests(EPaper2DPlusNetContext::Authority);
		Rig.DataComp->SetServerTimeOverrideForTests(NetApply_BaseServerTime);
		Rig.DataComp->bAutoApplyRootMotion = true;
		Rig.DataComp->CharacterProfile = Asset;

		Rig.FBComp->SetFlipbook(FB);
		Rig.DataComp->HandleFlipbookChanged(FB);
		Rig.DataComp->HandleFrameChanged(1);
		Rig.DataComp->HandleFrameChanged(2);

		// authority owner displacement (world delta applied) requires a world — covered by the U7 PIE
		// matrix; worldless asserts the gating + baseline only.
		Rig.FBComp->SetPlaybackPosition(3 * 0.1f + 0.05f, false); // mid-frame 3
		const FVector AuthorityPeek = Rig.DataComp->GetRootMotionDelta();
		TestFalse(TEXT("authority's per-frame advisory delta is also live (gate did not zero it)"),
			AuthorityPeek.IsNearlyZero());
	}

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// ConsumeRootMotionDeltaZeroOnProxy: on a SimulatedProxy ConsumeRootMotionDelta
// returns ZeroVector AND advances the baseline (a second call returns zero again),
// warn-once on the "authority-only" warning; on Standalone it returns the real
// non-zero delta and advances normally.
// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusNetApplyConsumeRootMotionDeltaZeroOnProxy,
	"Paper2DPlus.Network.RootMotion.ConsumeRootMotionDeltaZeroOnProxy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusNetApplyConsumeRootMotionDeltaZeroOnProxy::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	UPaperFlipbook* FB = NetApply_AddMove(Asset, TEXT("Lunge"), 4);
	NetApply_AuthorRootMotion(Asset, 0, /*PerFramePixelsX=*/16.0f);

	// Proxy: zero delta, baseline still advances, one warning.
	{
		FNetApply_Rig Rig = NetApply_MakeRig();
		Rig.DataComp->SetNetContextOverrideForTests(EPaper2DPlusNetContext::SimulatedProxy);
		Rig.DataComp->bAutoApplyRootMotion = false; // manual-drive caller
		Rig.DataComp->CharacterProfile = Asset;
		Rig.FBComp->SetFlipbook(FB);
		Rig.DataComp->HandleFlipbookChanged(FB);

		AddExpectedError(TEXT("ConsumeRootMotionDelta returns zero on a network proxy"),
			EAutomationExpectedErrorFlags::Contains);

		// Sit on a frame with a non-trivial authored position so a Standalone caller WOULD see motion.
		Rig.FBComp->SetPlaybackPosition(0.2f, false); // frame 2, pos (48,0)

		const FVector FirstConsume = Rig.DataComp->ConsumeRootMotionDelta();
		TestTrue(TEXT("proxy ConsumeRootMotionDelta returns ZeroVector"), FirstConsume.IsNearlyZero());

		// Second consume on the SAME frame returns zero too — the baseline advanced to the current pos.
		const FVector SecondConsume = Rig.DataComp->ConsumeRootMotionDelta();
		TestTrue(TEXT("baseline advanced (second consume still zero, current frame)"),
			SecondConsume.IsNearlyZero());

		// Warn-once: a later consume on a fresh frame must NOT emit a second warning (the latch holds).
		// AddExpectedError without Occurrences asserts >=1; a second emission would FAIL the test as an
		// unexpected error, so simply driving another consume here proves the once-pin.
		Rig.FBComp->SetPlaybackPosition(0.3f, false); // frame 3
		Rig.DataComp->ConsumeRootMotionDelta();
	}

	// Standalone: the real non-zero delta, advancing the baseline.
	{
		FNetApply_Rig Rig = NetApply_MakeRig(/*bEnableReplication=*/false);
		Rig.DataComp->bAutoApplyRootMotion = false;
		Rig.DataComp->CharacterProfile = Asset;
		Rig.FBComp->SetFlipbook(FB);
		Rig.DataComp->HandleFlipbookChanged(FB);

		TestEqual(TEXT("standalone rig resolves Standalone"),
			(int32)Rig.DataComp->GetNetContext(), (int32)EPaper2DPlusNetContext::Standalone);

		Rig.FBComp->SetPlaybackPosition(0.2f, false); // frame 2 — non-zero authored pos vs frame-0 baseline
		const FVector RealDelta = Rig.DataComp->ConsumeRootMotionDelta();
		TestFalse(TEXT("standalone ConsumeRootMotionDelta returns a real non-zero delta"),
			RealDelta.IsNearlyZero());

		// Consumed: the baseline advanced, so an immediate repeat on the same frame returns zero.
		const FVector AfterConsume = Rig.DataComp->ConsumeRootMotionDelta();
		TestTrue(TEXT("standalone consume advanced the baseline"), AfterConsume.IsNearlyZero());
	}

	return true;
}

// SpriteOffsetGatedOnProxy: the per-frame SpriteOffset dispatch reaches the offset-
// bearing frame identically under both contexts (the offset apply rides the same
// HandleFrameChanged pass), which IS worldless-observable via a CosmeticOnly one-shot
// on that frame.
//
// NOTE (audit F5): the per-frame sprite offset is COSMETIC — it AddWorldOffsets the
// FLIPBOOK COMPONENT, not the actor root — so it is NO LONGER gated out on a proxy
// whose flipbook component is not the actor root (movement replication owns the
// ACTOR's motion, not a child component's local visual offset). The offset is gated
// out ONLY in the proxy-AND-root case. The applied-vs-skipped distinction (and the
// root exception) is pinned worldlessly by SpriteOffsetCosmeticOnNonRootProxy below;
// this test pins only that the dispatch reaches the offset frame under both contexts.
// The actor-displacement half is covered by the U7 PIE matrix.
// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusNetApplySpriteOffsetGatedOnProxy,
	"Paper2DPlus.Network.RootMotion.SpriteOffsetGatedOnProxy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusNetApplySpriteOffsetGatedOnProxy::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	UPaperFlipbook* FB = NetApply_AddMove(Asset, TEXT("Step"), 4);
	// A non-trivial sprite offset on frame 1 (none on the others) so a step onto frame 1 applies it.
	NetApply_AuthorSpriteOffset(Asset, 0, /*FrameIndex=*/1, FIntPoint(64, 0));
	// A CosmeticOnly Cue on the offset-bearing frame 1 broadcasts on BOTH a proxy and authority, so it
	// is the worldless-observable proof that the HandleFrameChanged pass (which also runs the gated
	// sprite-offset apply) reached frame 1 under each context.
	UPaper2DPlusTestMomentCue* Frame1Moment = NewObject<UPaper2DPlusTestMomentCue>(Asset);
	Frame1Moment->TriggerFrame = 1;
	Frame1Moment->NetPolicy = EPaper2DPlusFrameCueNetPolicy::CosmeticOnly;
	Asset->Flipbooks[0].FrameEventData.FrameCues.Add(Frame1Moment);

	// Proxy: the offset frame is reached (one-shot fires). The offset apply itself is cosmetic on this
	// non-root rig (audit F5) — its applied-vs-skipped behavior is pinned by the dedicated F5 test below;
	// here we assert only that the dispatch reached the offset frame.
	{
		FNetApply_Rig Rig = NetApply_MakeRig();
		Rig.DataComp->SetNetContextOverrideForTests(EPaper2DPlusNetContext::SimulatedProxy);
		Rig.DataComp->CharacterProfile = Asset;
		UPaper2DPlusFrameCueRecorder* Recorder = NewObject<UPaper2DPlusFrameCueRecorder>();
		Rig.DataComp->OnFrameCue.AddDynamic(Recorder, &UPaper2DPlusFrameCueRecorder::OnCue);

		Rig.FBComp->SetFlipbook(FB);
		Rig.DataComp->HandleFlipbookChanged(FB);
		Rig.DataComp->HandleFrameChanged(1); // the offset-bearing frame

		TestTrue(TEXT("proxy reached the offset-bearing frame (cue broadcast ran)"),
			Recorder->Cues.Contains(Frame1Moment));
	}

	// Authority: the offset frame is reached identically (one-shot fires); the AddWorldOffset applies.
	{
		FNetApply_Rig Rig = NetApply_MakeRig();
		Rig.DataComp->SetNetContextOverrideForTests(EPaper2DPlusNetContext::Authority);
		Rig.DataComp->CharacterProfile = Asset;
		UPaper2DPlusFrameCueRecorder* Recorder = NewObject<UPaper2DPlusFrameCueRecorder>();
		Rig.DataComp->OnFrameCue.AddDynamic(Recorder, &UPaper2DPlusFrameCueRecorder::OnCue);

		Rig.FBComp->SetFlipbook(FB);
		Rig.DataComp->HandleFlipbookChanged(FB);
		Rig.DataComp->HandleFrameChanged(1);

		// authority owner displacement (sprite offset applied) requires a world — covered by the U7 PIE
		// matrix; worldless asserts only that the dispatch reached the offset frame.
		TestTrue(TEXT("authority reached the offset-bearing frame (cue broadcast ran; offset applies)"),
			Recorder->Cues.Contains(Frame1Moment));
	}

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// SpriteOffsetCosmeticOnNonRootProxy (audit F5): the per-frame sprite offset is
// COSMETIC — it AddWorldOffsets the FLIPBOOK COMPONENT, not the actor root. On a
// SimulatedProxy whose flipbook component is NOT the actor root it IS applied (the
// component moves locally; movement replication still owns the ACTOR's motion). The
// offset is gated OUT only in the proxy-AND-root case (where it would displace the
// whole actor and fight movement replication). Authority/Standalone always apply it.
//
// Worldless-observable because AddWorldOffset on a CHILD (non-root) scene component
// updates that component's OWN relative transform without a world — it just never
// moves the actor root, which is why the old gated test (reading GetActorLocation)
// could not see it. The actor-level displacement half stays in the U7 PIE matrix.
// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusNetApplySpriteOffsetCosmeticOnNonRootProxy,
	"Paper2DPlus.Network.RootMotion.SpriteOffsetCosmeticOnNonRootProxy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusNetApplySpriteOffsetCosmeticOnNonRootProxy::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	UPaperFlipbook* FB = NetApply_AddMove(Asset, TEXT("Step"), 4);
	// A non-trivial sprite offset on frame 1 (none on frame 0) so stepping onto frame 1 produces a
	// non-zero AddWorldOffset while frame 0 leaves the component put.
	NetApply_AuthorSpriteOffset(Asset, 0, /*FrameIndex=*/1, FIntPoint(64, 0));

	// (a) Non-root proxy: the flipbook component is NOT the actor root (NetApply_MakeRig never
	// SetRootComponents it), so the cosmetic offset IS applied even on a SimulatedProxy.
	{
		FNetApply_Rig Rig = NetApply_MakeRig();
		Rig.DataComp->SetNetContextOverrideForTests(EPaper2DPlusNetContext::SimulatedProxy);
		Rig.DataComp->CharacterProfile = Asset;

		TestTrue(TEXT("rig flipbook component is NOT the actor root"),
			Rig.Owner->GetRootComponent() != Rig.FBComp);

		Rig.FBComp->SetFlipbook(FB);
		Rig.DataComp->HandleFlipbookChanged(FB); // frame 0 — no offset authored there
		TestTrue(TEXT("no offset at frame 0 (component unmoved)"),
			Rig.FBComp->GetRelativeLocation().IsNearlyZero());

		Rig.DataComp->HandleFrameChanged(1); // the offset-bearing frame
		TestFalse(TEXT("cosmetic sprite offset WAS applied on a non-root proxy (component moved)"),
			Rig.FBComp->GetRelativeLocation().IsNearlyZero());
	}

	// (b) Root proxy: make the flipbook component the actor root — now the offset would displace the
	// whole actor, so it obeys the authority root-motion gate and is SKIPPED on a SimulatedProxy.
	{
		FNetApply_Rig Rig = NetApply_MakeRig();
		Rig.Owner->SetRootComponent(Rig.FBComp); // flipbook component IS the actor root
		Rig.DataComp->SetNetContextOverrideForTests(EPaper2DPlusNetContext::SimulatedProxy);
		Rig.DataComp->CharacterProfile = Asset;

		TestTrue(TEXT("rig flipbook component IS the actor root"),
			Rig.Owner->GetRootComponent() == Rig.FBComp);

		Rig.FBComp->SetFlipbook(FB);
		Rig.DataComp->HandleFlipbookChanged(FB);
		Rig.DataComp->HandleFrameChanged(1); // the offset-bearing frame
		TestTrue(TEXT("sprite offset is gated OUT on a root-flipbook proxy (root unmoved)"),
			Rig.FBComp->GetRelativeLocation().IsNearlyZero());
	}

	// (c) Authority with a root flipbook: the gate passes, so the offset applies even when the flipbook
	// IS the actor root (authority owns motion) — confirms the gate is authority/Standalone-OR-non-root.
	{
		FNetApply_Rig Rig = NetApply_MakeRig();
		Rig.Owner->SetRootComponent(Rig.FBComp);
		Rig.DataComp->SetNetContextOverrideForTests(EPaper2DPlusNetContext::Authority);
		Rig.DataComp->CharacterProfile = Asset;

		Rig.FBComp->SetFlipbook(FB);
		Rig.DataComp->HandleFlipbookChanged(FB);
		Rig.DataComp->HandleFrameChanged(1);
		TestFalse(TEXT("authority applies the offset even on a root flipbook (gate passes)"),
			Rig.FBComp->GetRelativeLocation().IsNearlyZero());
	}

	return true;
}

// ════════════════════════════════════════════════════════════════════════════
// B. APPLY MODE (Paper2DPlus.Network.Apply.*)
// ════════════════════════════════════════════════════════════════════════════

// ─────────────────────────────────────────────────────────────────────────────
// PlaysFlipbookFromAnchor: apply-mode simulated proxy driving OnRep_AnimState with
// a valid, fresh-sequence, mid-move-anchored state SWITCHES the flipbook component
// to the move's flipbook AND seeks to the anchor-derived position — and the
// advisory ALSO broadcasts (commit → apply → broadcast).
// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusNetApplyPlaysFlipbookFromAnchor,
	"Paper2DPlus.Network.Apply.PlaysFlipbookFromAnchor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusNetApplyPlaysFlipbookFromAnchor::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	UPaperFlipbook* FB = NetApply_AddMove(Asset, TEXT("BMove"), 4); // 0.4s total

	FNetApply_Rig Rig = NetApply_MakeRig();
	Rig.DataComp->SetNetContextOverrideForTests(EPaper2DPlusNetContext::SimulatedProxy);
	Rig.DataComp->SetServerTimeOverrideForTests(NetApply_BaseServerTime);
	Rig.DataComp->bApplyReplicatedAnimStateOnSimulatedProxies = true;
	UPaper2DPlusNetAnimStateRecorder* Recorder = NetApply_BindRecorder(Rig);

	// The proxy has NOT yet been playing this move's flipbook (apply mode reconstructs it).
	TestNull(TEXT("flipbook starts unset before the apply"), Rig.FBComp->GetFlipbook());

	Rig.DataComp->CharacterProfile = Asset;
	Rig.DataComp->GetRepProfileSeqForTests() = 1;
	Rig.DataComp->OnRep_CharacterProfile(nullptr);

	// Mid-move late state: 0.15s into the 0.4s move.
	FPaper2DPlusRepAnimState& Rep = Rig.DataComp->GetRepAnimStateForTests();
	Rep.Sequence = 5;
	Rep.ProfileSeq = 1;
	Rep.bIsProfileMove = true;
	Rep.MoveName = FName(TEXT("BMove"));
	Rep.StartServerTime = NetApply_BaseServerTime - 0.15;
	Rep.PlayRate = 1.0f;
	Rig.DataComp->OnRep_AnimState(FPaper2DPlusRepAnimState());

	TestTrue(TEXT("apply mode SWITCHED the flipbook component to the move's flipbook"),
		Rig.FBComp->GetFlipbook() == FB);
	TestTrue(TEXT("apply mode seeked to the anchor-derived position (~0.15s)"),
		FMath::IsNearlyEqual(Rig.FBComp->GetPlaybackPosition(), 0.15f, 1.e-3f));

	// commit → apply → broadcast: the advisory ALSO fired, carrying the same payload.
	TestEqual(TEXT("the advisory ALSO broadcast (commit→apply→broadcast)"), Recorder->BroadcastCount, 1);
	TestTrue(TEXT("advisory resolved the move"), Recorder->LastMoveName == FName(TEXT("BMove")));
	TestTrue(TEXT("advisory carries the resolved flipbook"), Recorder->LastFlipbook.Get() == FB);
	TestTrue(TEXT("advisory carries the anchor-derived position"),
		FMath::IsNearlyEqual(Recorder->LastPlaybackPosition, 0.15f, 1.e-3f));
	TestEqual(TEXT("sequence committed"), (int32)Rig.DataComp->GetLastAppliedSequenceForTests(), 5);

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// NoOneShotReplayOnCatchUp: an apply-mode mid-move catch-up fires ZERO one-shots
// (the frame-0 one-shot never replays) and the ranged event spanning the target
// key frame fires Begin ONLY when its policy is catch-up-eligible (CosmeticOnly);
// the same ranged event marked AuthorityOnly does NOT Begin on the proxy.
// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusNetApplyFrameCueCatchUpTest,
	"Paper2DPlus.FrameCues.Network.CatchUpMomentSuppressionAndRangeSymmetry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusNetApplyFrameCueCatchUpTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	NetApply_AddMove(Asset, TEXT("BMove"), 4);

	UPaper2DPlusTestMomentCue* Moment = NewObject<UPaper2DPlusTestMomentCue>(Asset);
	Moment->TriggerFrame = 0;
	Moment->NetPolicy = EPaper2DPlusFrameCueNetPolicy::CosmeticOnly;
	Asset->Flipbooks[0].FrameEventData.FrameCues.Add(Moment);

	UPaper2DPlusTestRangeCue* Range = NewObject<UPaper2DPlusTestRangeCue>(Asset);
	Range->StartFrame = 1;
	Range->FrameCount = 2; // catch-up target frame 1 lies inside [1,2]
	Range->NetPolicy = EPaper2DPlusFrameCueNetPolicy::CosmeticOnly;
	Asset->Flipbooks[0].FrameEventData.FrameCues.Add(Range);

	FNetApply_Rig Rig = NetApply_MakeRig();
	Rig.DataComp->SetNetContextOverrideForTests(EPaper2DPlusNetContext::SimulatedProxy);
	Rig.DataComp->SetServerTimeOverrideForTests(NetApply_BaseServerTime);
	Rig.DataComp->bApplyReplicatedAnimStateOnSimulatedProxies = true;
	UPaper2DPlusFrameCueRecorder* Recorder = NewObject<UPaper2DPlusFrameCueRecorder>();
	Rig.DataComp->OnFrameCue.AddDynamic(Recorder, &UPaper2DPlusFrameCueRecorder::OnCue);

	Rig.DataComp->CharacterProfile = Asset;
	Rig.DataComp->GetRepProfileSeqForTests() = 1;
	Rig.DataComp->OnRep_CharacterProfile(nullptr);

	FPaper2DPlusRepAnimState& Rep = Rig.DataComp->GetRepAnimStateForTests();
	Rep.Sequence = 5;
	Rep.ProfileSeq = 1;
	Rep.bIsProfileMove = true;
	Rep.MoveName = FName(TEXT("BMove"));
	Rep.StartServerTime = NetApply_BaseServerTime - 0.15; // key frame 1
	Rep.PlayRate = 1.0f;
	Rig.DataComp->OnRep_AnimState(FPaper2DPlusRepAnimState());

	TestEqual(TEXT("catch-up emits only the eligible Range Begin"), Recorder->Contexts.Num(), 1);
	if (Recorder->Contexts.Num() == 1)
	{
		TestTrue(TEXT("catch-up notification belongs to the Cue State"), Recorder->Cues[0] == Range);
		TestEqual(TEXT("catch-up reconstructs Begin"),
			Recorder->Contexts[0].Phase, EPaper2DPlusFrameCuePhase::Begin);
		TestTrue(TEXT("Begin is marked as catch-up"), Recorder->Contexts[0].bIsCatchUp);
	}
	TestTrue(TEXT("Range is active after catch-up reconstruction"),
		Rig.DataComp->IsRangeCueActiveForTests(Range));

	Rig.DataComp->HandleFrameChanged(3); // leave [1,2]
	TestEqual(TEXT("catch-up Begin receives one paired End"), Recorder->Contexts.Num(), 2);
	if (Recorder->Contexts.Num() == 2)
	{
		TestTrue(TEXT("paired End belongs to the same Cue State"), Recorder->Cues[1] == Range);
		TestEqual(TEXT("paired lifecycle ends"),
			Recorder->Contexts[1].Phase, EPaper2DPlusFrameCuePhase::End);
		TestEqual(TEXT("natural post-catch-up End reason"),
			Recorder->Contexts[1].EndReason, EPaper2DPlusFrameCueEndReason::Completed);
	}
	TestFalse(TEXT("Range leaves active set after paired End"),
		Rig.DataComp->IsRangeCueActiveForTests(Range));
	TestFalse(TEXT("Cue was never replayed during catch-up"), Recorder->Cues.Contains(Moment));
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// AdviseOnlyProxyDoesNotPlay: with bApplyReplicatedAnimStateOnSimulatedProxies=false
// the component is NOT switched by the apply path — only the advisory broadcasts
// (gate+advise default: the game plays the flipbook).
// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusNetApplyAdviseOnlyProxyDoesNotPlay,
	"Paper2DPlus.Network.Apply.AdviseOnlyProxyDoesNotPlay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusNetApplyAdviseOnlyProxyDoesNotPlay::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	NetApply_AddMove(Asset, TEXT("BMove"), 4);

	FNetApply_Rig Rig = NetApply_MakeRig();
	Rig.DataComp->SetNetContextOverrideForTests(EPaper2DPlusNetContext::SimulatedProxy);
	Rig.DataComp->SetServerTimeOverrideForTests(NetApply_BaseServerTime);
	Rig.DataComp->bApplyReplicatedAnimStateOnSimulatedProxies = false; // advise-only (the default)
	UPaper2DPlusNetAnimStateRecorder* Recorder = NetApply_BindRecorder(Rig);

	Rig.DataComp->CharacterProfile = Asset;
	Rig.DataComp->GetRepProfileSeqForTests() = 1;
	Rig.DataComp->OnRep_CharacterProfile(nullptr);

	FPaper2DPlusRepAnimState& Rep = Rig.DataComp->GetRepAnimStateForTests();
	Rep.Sequence = 5;
	Rep.ProfileSeq = 1;
	Rep.bIsProfileMove = true;
	Rep.MoveName = FName(TEXT("BMove"));
	Rep.StartServerTime = NetApply_BaseServerTime - 0.15;
	Rep.PlayRate = 1.0f;
	Rig.DataComp->OnRep_AnimState(FPaper2DPlusRepAnimState());

	TestNull(TEXT("advise-only proxy did NOT switch the flipbook (the game plays it)"),
		Rig.FBComp->GetFlipbook());
	TestEqual(TEXT("the advisory still broadcast"), Recorder->BroadcastCount, 1);
	TestTrue(TEXT("advisory resolved the move so the game can play it"),
		Recorder->LastMoveName == FName(TEXT("BMove")));

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// SameSeqReanchorDoesNotRewarm (audit F6): an apply-mode SimulatedProxy mid-move with
// a CosmeticOnly ranged frame event spanning the current key frame. A new-seq apply
// warms the move (the ranged event Begins ONCE). A SAME-seq re-anchor (same Sequence,
// changed StartServerTime — the RepublishAnimState / U5 hit-stop clock re-anchor shape)
// that maps to the SAME key frame must SEEK ONLY: it must NOT re-run the full catch-up
// warm, so the ranged event does NOT re-Begin / re-End and MoveInstanceCounter does NOT
// bump. The advisory still re-broadcasts (proving the re-anchor branch took the seek-
// only path, not a trivially-skipped early-return). With the pre-F6 code this same-seq
// re-anchor re-warmed the whole move on every hit-stop unfreeze / republish.
// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusNetApplySameSeqReanchorDoesNotRewarm,
	"Paper2DPlus.Network.Apply.SameSeqReanchorDoesNotRewarm",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusNetApplySameSeqReanchorDoesNotRewarm::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	UPaperFlipbook* FB = NetApply_AddMove(Asset, TEXT("BMove"), 4); // 0.4s, FPS 10 / FrameRun 1

	// CosmeticOnly ranged event spanning frames [1,2] — catch-up-eligible, so the warm Begins it.
	UPaper2DPlusTestRangeCue* Range = NewObject<UPaper2DPlusTestRangeCue>(Asset);
	Range->StartFrame = 1;
	Range->FrameCount = 2; // frames 1,2
	Range->NetPolicy = EPaper2DPlusFrameCueNetPolicy::CosmeticOnly;
	Asset->Flipbooks[0].FrameEventData.FrameCues.Add(Range);

	FNetApply_Rig Rig = NetApply_MakeRig();
	Rig.DataComp->SetNetContextOverrideForTests(EPaper2DPlusNetContext::SimulatedProxy);
	Rig.DataComp->SetServerTimeOverrideForTests(NetApply_BaseServerTime);
	Rig.DataComp->bApplyReplicatedAnimStateOnSimulatedProxies = true;
	UPaper2DPlusNetAnimStateRecorder* Recorder = NetApply_BindRecorder(Rig);
	UPaper2DPlusFrameCueRecorder* CueRecorder = NewObject<UPaper2DPlusFrameCueRecorder>();
	Rig.DataComp->OnFrameCue.AddDynamic(CueRecorder, &UPaper2DPlusFrameCueRecorder::OnCue);

	Rig.DataComp->CharacterProfile = Asset;
	Rig.DataComp->GetRepProfileSeqForTests() = 1;
	Rig.DataComp->OnRep_CharacterProfile(nullptr);

	// New-seq apply anchored mid-move at ~0.15s (frame 1) — warms the proxy; the ranged event Begins.
	FPaper2DPlusRepAnimState& Rep = Rig.DataComp->GetRepAnimStateForTests();
	Rep.Sequence = 5;
	Rep.ProfileSeq = 1;
	Rep.bIsProfileMove = true;
	Rep.MoveName = FName(TEXT("BMove"));
	Rep.StartServerTime = NetApply_BaseServerTime - 0.15; // ≈ frame 1
	Rep.PlayRate = 1.0f;
	Rig.DataComp->OnRep_AnimState(FPaper2DPlusRepAnimState());

	TestTrue(TEXT("apply mode switched to the move's flipbook"), Rig.FBComp->GetFlipbook() == FB);
	TestTrue(TEXT("Cue State begins on the catch-up warm"), Rig.DataComp->IsRangeCueActiveForTests(Range));
	TestEqual(TEXT("advisory broadcast once on the new-seq apply"), Recorder->BroadcastCount, 1);

	const int32 MoveInstanceAfterApply = (int32)Rig.DataComp->GetMoveInstanceCounterForTests();
	const int32 CueNotificationCountAfterApply = CueRecorder->Contexts.Num();

	// SAME-seq re-anchor: keep Sequence 5, change StartServerTime so it maps to the SAME key frame 1
	// (0.12s is still inside frame 1's [0.1,0.2) span). The F195a re-anchor branch fires (StartServerTime
	// changed); the F6 fix must SEEK ONLY because the target key frame is unchanged.
	Rep.StartServerTime = NetApply_BaseServerTime - 0.12; // still frame 1, different anchor
	Rig.DataComp->OnRep_AnimState(FPaper2DPlusRepAnimState());

	TestTrue(TEXT("same-seq re-anchor keeps the Cue State active (no re-warm)"),
		Rig.DataComp->IsRangeCueActiveForTests(Range));
	TestEqual(TEXT("same-seq re-anchor emits no duplicate Range lifecycle notification"),
		CueRecorder->Contexts.Num(), CueNotificationCountAfterApply);
	TestEqual(TEXT("same-seq re-anchor did NOT bump MoveInstanceCounter (no OnFlipbookChanged)"),
		(int32)Rig.DataComp->GetMoveInstanceCounterForTests(), MoveInstanceAfterApply);
	// The re-anchor branch DID run (it re-broadcast the advisory) — proves the seek-only path executed
	// rather than the test passing trivially because the branch was never entered.
	TestEqual(TEXT("re-anchor still re-broadcast the advisory (seek-only path executed)"),
		Recorder->BroadcastCount, 2);
	// And the proxy actually seeked to the re-anchored position (~0.12s), still on frame 1.
	TestTrue(TEXT("proxy seeked to the re-anchored position (~0.12s)"),
		FMath::IsNearlyEqual(Rig.FBComp->GetPlaybackPosition(), 0.12f, 1.e-2f));

	return true;
}

// ════════════════════════════════════════════════════════════════════════════
// C. GATE LATCH (Paper2DPlus.Network.GateLatch.*)
// ════════════════════════════════════════════════════════════════════════════

// ─────────────────────────────────────────────────────────────────────────────
// LatchedInKeepsEndAfterFlip: a CosmeticOnly ranged event that Begins while its
// gate PASSES keeps receiving Tick/End even after the test context flips mid-range
// so the gate would now FAIL — Begin/End symmetry preserved (the latched-IN event
// keeps dispatching). The flip uses SetNetContextOverrideForTests mid-range.
//
// NOTE: a CosmeticOnly event passes on SimulatedProxy and FAILS on a DEDICATED
// server. Worldless rigs report dedicated=false (no world), so the gate cannot be
// failed mid-range for CosmeticOnly purely by context override. We instead use an
// OwnerOnly event: it PASSES on a locally-controlled context and FAILS on a
// SimulatedProxy. With a non-pawn owner the locality input is always false, so the
// pass side is forced via Authority (Authority+OwnerOnly+notLocallyControlled is a
// dedicated-style FAIL in the matrix... ). The tightest worldless lever that flips
// a SINGLE event's gate result mid-range without locality is Authority↔SimProxy on
// an AuthorityOnly event (PASS on Authority, FAIL on SimulatedProxy). See report.
// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusNetApplyLatchedInKeepsEndAfterFlip,
	"Paper2DPlus.Network.GateLatch.LatchedInKeepsEndAfterFlip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusNetApplyLatchedInKeepsEndAfterFlip::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	UPaperFlipbook* FB = NetApply_AddMove(Asset, TEXT("BMove"), 5);

	// AuthorityOnly ranged event spanning frames [1,3] (StartFrame 1, FrameCount 3 → frames 1,2,3).
	UPaper2DPlusTestRangeCue* Range = NewObject<UPaper2DPlusTestRangeCue>(Asset);
	Range->StartFrame = 1;
	Range->FrameCount = 3;
	Range->bEmitUpdates = true;
	Range->NetPolicy = EPaper2DPlusFrameCueNetPolicy::AuthorityOnly;
	Asset->Flipbooks[0].FrameEventData.FrameCues.Add(Range);

	FNetApply_Rig Rig = NetApply_MakeRig();
	// Begin under a PASSING gate: Authority passes AuthorityOnly.
	Rig.DataComp->SetNetContextOverrideForTests(EPaper2DPlusNetContext::Authority);
	Rig.DataComp->SetServerTimeOverrideForTests(NetApply_BaseServerTime);
	Rig.DataComp->CharacterProfile = Asset;
	UPaper2DPlusFrameCueRecorder* Recorder = NewObject<UPaper2DPlusFrameCueRecorder>();
	Rig.DataComp->OnFrameCue.AddDynamic(Recorder, &UPaper2DPlusFrameCueRecorder::OnCue);

	Rig.FBComp->SetFlipbook(FB);
	Rig.DataComp->HandleFlipbookChanged(FB); // frame 0 — outside the range, quiet
	Rig.DataComp->HandleFrameChanged(1);     // enter the range under the PASSING gate → Begin

	TestTrue(TEXT("Cue State began under the passing gate"), Rig.DataComp->IsRangeCueActiveForTests(Range));

	// Flip the gate to FAILING mid-range: SimulatedProxy fails AuthorityOnly.
	Rig.DataComp->SetNetContextOverrideForTests(EPaper2DPlusNetContext::SimulatedProxy);

	Rig.DataComp->HandleFrameChanged(2); // still in range — latched IN keeps dispatching Tick
	Rig.DataComp->HandleFrameChanged(3); // still in range — Tick
	Rig.DataComp->HandleFrameChanged(4); // LEAVE the range → End must still fire (symmetry)

	TestFalse(TEXT("Cue State ends despite the mid-range gate flip"), Rig.DataComp->IsRangeCueActiveForTests(Range));
	int32 BeginCount = 0;
	int32 UpdateCount = 0;
	int32 EndCount = 0;
	for (const FPaper2DPlusFrameCueContext& Context : Recorder->Contexts)
	{
		BeginCount += Context.Phase == EPaper2DPlusFrameCuePhase::Begin ? 1 : 0;
		UpdateCount += Context.Phase == EPaper2DPlusFrameCuePhase::Update ? 1 : 0;
		EndCount += Context.Phase == EPaper2DPlusFrameCuePhase::End ? 1 : 0;
	}
	TestEqual(TEXT("Begin emitted exactly once"), BeginCount, 1);
	// Range [1,3], bEmitUpdates: each in-range HandleFrameChanged (frames 1,2,3) emits EXACTLY one Update (the
	// behavior-first Cue dispatch is deterministic; latch-IN keeps emitting despite the mid-range gate flip) => 3.
	TestEqual(TEXT("updates continue through the gate flip (latched IN)"), UpdateCount, 3);
	TestEqual(TEXT("End emitted exactly once"), EndCount, 1);

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// LatchedOutNeverStarts: a ranged event whose gate FAILS at its Begin frame
// (AuthorityOnly on a SimulatedProxy) never dispatches Begin/Tick/End across its
// whole range — AND flipping the context to passing mid-range does NOT start it
// (latched OUT for the flipbook).
// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusNetApplyLatchedOutNeverStarts,
	"Paper2DPlus.Network.GateLatch.LatchedOutNeverStarts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusNetApplyLatchedOutNeverStarts::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	UPaperFlipbook* FB = NetApply_AddMove(Asset, TEXT("BMove"), 5);

	UPaper2DPlusTestRangeCue* Range = NewObject<UPaper2DPlusTestRangeCue>(Asset);
	Range->StartFrame = 1;
	Range->FrameCount = 3; // frames 1,2,3
	Range->bEmitUpdates = true;
	Range->NetPolicy = EPaper2DPlusFrameCueNetPolicy::AuthorityOnly;
	Asset->Flipbooks[0].FrameEventData.FrameCues.Add(Range);

	FNetApply_Rig Rig = NetApply_MakeRig();
	// FAIL the gate at the Begin frame: SimulatedProxy fails AuthorityOnly.
	Rig.DataComp->SetNetContextOverrideForTests(EPaper2DPlusNetContext::SimulatedProxy);
	Rig.DataComp->SetServerTimeOverrideForTests(NetApply_BaseServerTime);
	Rig.DataComp->CharacterProfile = Asset;
	UPaper2DPlusFrameCueRecorder* Recorder = NewObject<UPaper2DPlusFrameCueRecorder>();
	Rig.DataComp->OnFrameCue.AddDynamic(Recorder, &UPaper2DPlusFrameCueRecorder::OnCue);

	Rig.FBComp->SetFlipbook(FB);
	Rig.DataComp->HandleFlipbookChanged(FB);
	Rig.DataComp->HandleFrameChanged(1); // Begin frame under the FAILING gate → latched OUT

	TestFalse(TEXT("Cue State never began (gate failed at Begin edge)"), Rig.DataComp->IsRangeCueActiveForTests(Range));

	// Flip to passing mid-range: it must STILL not start (latched out for the flipbook).
	Rig.DataComp->SetNetContextOverrideForTests(EPaper2DPlusNetContext::Authority);
	Rig.DataComp->HandleFrameChanged(2);
	Rig.DataComp->HandleFrameChanged(3);
	Rig.DataComp->HandleFrameChanged(4); // leave range

	TestFalse(TEXT("Cue State stays inactive after flip-to-passing (latched OUT)"),
		Rig.DataComp->IsRangeCueActiveForTests(Range));
	TestEqual(TEXT("latched-out Cue State emits no lifecycle notifications"), Recorder->Contexts.Num(), 0);

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// StandaloneIdentity: in Standalone the latch is INERT — a ranged event fires
// Begin/Tick/End exactly as it would without any networking (every gate is
// constant-true, nothing is ever latched out).
// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusNetApplyGateLatchStandaloneIdentity,
	"Paper2DPlus.Network.GateLatch.StandaloneIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusNetApplyGateLatchStandaloneIdentity::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	UPaperFlipbook* FB = NetApply_AddMove(Asset, TEXT("BMove"), 5);

	// AuthorityOnly policy — the policy that WOULD be gated on a proxy. In Standalone it must fire
	// regardless (the C1 identity guarantee).
	UPaper2DPlusTestRangeCue* Range = NewObject<UPaper2DPlusTestRangeCue>(Asset);
	Range->StartFrame = 1;
	Range->FrameCount = 3; // frames 1,2,3
	Range->bEmitUpdates = true;
	Range->NetPolicy = EPaper2DPlusFrameCueNetPolicy::AuthorityOnly;
	Asset->Flipbooks[0].FrameEventData.FrameCues.Add(Range);

	FNetApply_Rig Rig = NetApply_MakeRig(/*bEnableReplication=*/false); // offline → Standalone
	Rig.DataComp->CharacterProfile = Asset;
	UPaper2DPlusFrameCueRecorder* Recorder = NewObject<UPaper2DPlusFrameCueRecorder>();
	Rig.DataComp->OnFrameCue.AddDynamic(Recorder, &UPaper2DPlusFrameCueRecorder::OnCue);

	TestEqual(TEXT("offline rig resolves Standalone"),
		(int32)Rig.DataComp->GetNetContext(), (int32)EPaper2DPlusNetContext::Standalone);

	Rig.FBComp->SetFlipbook(FB);
	Rig.DataComp->HandleFlipbookChanged(FB);
	Rig.DataComp->HandleFrameChanged(1); // Begin + Tick
	Rig.DataComp->HandleFrameChanged(2); // Tick
	Rig.DataComp->HandleFrameChanged(3); // Tick
	Rig.DataComp->HandleFrameChanged(4); // End

	int32 BeginCount = 0;
	int32 UpdateCount = 0;
	int32 EndCount = 0;
	for (const FPaper2DPlusFrameCueContext& Context : Recorder->Contexts)
	{
		BeginCount += Context.Phase == EPaper2DPlusFrameCuePhase::Begin ? 1 : 0;
		UpdateCount += Context.Phase == EPaper2DPlusFrameCuePhase::Update ? 1 : 0;
		EndCount += Context.Phase == EPaper2DPlusFrameCuePhase::End ? 1 : 0;
	}
	TestEqual(TEXT("Standalone: Begin emits for AuthorityOnly (latch inert)"), BeginCount, 1);
	// Range [1,3], bEmitUpdates: frames 1,2,3 driven in-range each emit EXACTLY one Update (deterministic) => 3.
	TestEqual(TEXT("Standalone: Updates emit across the range"), UpdateCount, 3);
	TestEqual(TEXT("Standalone: End emits on leaving the range"), EndCount, 1);

	return true;
}

// ════════════════════════════════════════════════════════════════════════════
// D. CLOCK / DRIFT (Paper2DPlus.Network.Clock.*)
// ════════════════════════════════════════════════════════════════════════════

// ─────────────────────────────────────────────────────────────────────────────
// ProxyDriftSnap: an apply-mode proxy playing a move snaps its local playback to
// the server-formula position when the SERVER-TIME override advances so the
// expected position diverges by MORE than NetPlaybackSnapToleranceSeconds; a
// within-tolerance drift does NOT snap.
// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusNetApplyProxyDriftSnap,
	"Paper2DPlus.Network.Clock.ProxyDriftSnap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusNetApplyProxyDriftSnap::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	UPaperFlipbook* FB = NetApply_AddMove(Asset, TEXT("BMove"), 8); // 0.8s, looping headroom

	FNetApply_Rig Rig = NetApply_MakeRig();
	Rig.DataComp->SetNetContextOverrideForTests(EPaper2DPlusNetContext::SimulatedProxy);
	Rig.DataComp->SetServerTimeOverrideForTests(NetApply_BaseServerTime);
	Rig.DataComp->bApplyReplicatedAnimStateOnSimulatedProxies = true;
	Rig.DataComp->NetPlaybackSnapToleranceSeconds = 0.1f;

	Rig.DataComp->CharacterProfile = Asset;
	Rig.DataComp->GetRepProfileSeqForTests() = 1;
	Rig.DataComp->OnRep_CharacterProfile(nullptr);

	// Apply a looping move anchored at "now" so the live flipbook matches the published move (the drift
	// corrector only runs while the live flipbook IS the published move). Looping avoids the non-loop
	// finished-clamp interfering with the diverged-expected computation.
	FPaper2DPlusRepAnimState& Rep = Rig.DataComp->GetRepAnimStateForTests();
	Rep.Sequence = 5;
	Rep.ProfileSeq = 1;
	Rep.bIsProfileMove = true;
	Rep.bLooping = true;
	Rep.MoveName = FName(TEXT("BMove"));
	Rep.StartServerTime = NetApply_BaseServerTime; // position 0 at apply time
	Rep.PlayRate = 1.0f;
	Rig.DataComp->OnRep_AnimState(FPaper2DPlusRepAnimState());
	TestTrue(TEXT("apply seeded position ~0"),
		FMath::IsNearlyEqual(Rig.FBComp->GetPlaybackPosition(), 0.0f, 1.e-3f));

	// Within-tolerance: advance the server clock by 0.05s (< 0.1 tolerance). A frame change must NOT
	// snap the local position (still ~0, set by the apply; the local clock didn't advance worldless).
	Rig.DataComp->SetServerTimeOverrideForTests(NetApply_BaseServerTime + 0.05);
	Rig.DataComp->HandleFrameChanged(0); // drives MaybeCorrectReplicatedPlaybackDrift (not applying)
	TestTrue(TEXT("within-tolerance drift does NOT snap"),
		FMath::IsNearlyEqual(Rig.FBComp->GetPlaybackPosition(), 0.0f, 1.e-3f));

	// Beyond-tolerance: advance the server clock by 0.5s. The expected server-formula position is now
	// ~0.5s, diverging from the local ~0 by > tolerance → the corrector snaps toward expected.
	Rig.DataComp->SetServerTimeOverrideForTests(NetApply_BaseServerTime + 0.5);
	Rig.DataComp->HandleFrameChanged(1);
	TestTrue(TEXT("beyond-tolerance drift SNAPS toward the server-formula position (~0.5s)"),
		FMath::IsNearlyEqual(Rig.FBComp->GetPlaybackPosition(), 0.5f, 1.e-2f));

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// AuthorityStaleDetectReanchors: on Authority, a game-side playback mutation
// (SetPlaybackPosition to a divergent position) made WITHOUT calling
// RepublishAnimState is auto-detected on the next HandleFrameChanged and the
// published anchor is re-anchored to truth — SAME Sequence (no move-instance bump).
// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusNetApplyAuthorityStaleDetectReanchors,
	"Paper2DPlus.Network.Clock.AuthorityStaleDetectReanchors",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusNetApplyAuthorityStaleDetectReanchors::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	UPaperFlipbook* FB = NetApply_AddMove(Asset, TEXT("BMove"), 8); // 0.8s

	FNetApply_Rig Rig = NetApply_MakeRig();
	Rig.DataComp->SetNetContextOverrideForTests(EPaper2DPlusNetContext::Authority);
	Rig.DataComp->SetServerTimeOverrideForTests(NetApply_BaseServerTime);
	Rig.DataComp->NetPlaybackSnapToleranceSeconds = 0.1f;
	Rig.DataComp->CharacterProfile = Asset;

	// Non-looping move BEFORE the publish (so RepAnimState.bLooping captures false). CRITICAL: the
	// authority stale-detect measures CIRCULAR drift for a LOOPING move (min(|E-A|, Length-|E-A|)), so
	// a 0.5 divergence on this 0.8s loop would shrink to min(0.5, 0.3) = 0.3 — BELOW the 0.4 authority
	// re-anchor band — and never re-anchor. A non-looping move uses the LINEAR distance, so the 0.5
	// jump below stays 0.5 > 0.4 and the stale-detect fires. (UPaperFlipbookComponent defaults to
	// looping=true, hence the explicit clear; the loop-straddle case has its own test below.)
	Rig.FBComp->SetLooping(false);

	// Publish a move at position 0.
	Rig.FBComp->SetFlipbook(FB);
	Rig.DataComp->HandleFlipbookChanged(FB);
	const FPaper2DPlusRepAnimState& Rep = Rig.DataComp->GetRepAnimStateForTests();
	const int32 SeqAfterPublish = (int32)Rep.Sequence;
	const double AnchorAfterPublish = Rep.StartServerTime;
	TestEqual(TEXT("warm-up published seq 1"), SeqAfterPublish, 1);
	TestFalse(TEXT("published move is non-looping (linear drift distance applies)"), (bool)Rep.bLooping);
	TestTrue(TEXT("initial anchor at ServerNow (position 0)"),
		FMath::IsNearlyEqual(AnchorAfterPublish, NetApply_BaseServerTime, 1.e-6));

	// Game-side mutation the plugin cannot observe: jump playback to 0.5s WITHOUT RepublishAnimState.
	// Server clock unchanged, so the model says position 0 but the actual is 0.5 → LINEAR divergence
	// 0.5 > the 0.4 authority band (non-looping → no circular shrinkage).
	Rig.FBComp->SetPlaybackPosition(0.5f, false);
	Rig.DataComp->HandleFrameChanged(5); // the stale-detect runs here (authority, not applying)

	TestEqual(TEXT("re-anchor keeps the SAME Sequence (no move-instance bump)"),
		(int32)Rep.Sequence, SeqAfterPublish);
	// New anchor = ServerNow - actualPos/PlayRate = 1000 - 0.5 = 999.5.
	TestTrue(TEXT("anchor re-anchored to the new actual position"),
		FMath::IsNearlyEqual(Rep.StartServerTime, NetApply_BaseServerTime - 0.5, 1.e-3));
	TestTrue(TEXT("anchor actually moved from the stale value"),
		!FMath::IsNearlyEqual(Rep.StartServerTime, AnchorAfterPublish, 1.e-3));

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// LoopWrapDoesNotSpuriouslyReanchor (review regression guard): on Authority, a
// LOOPING move whose engine playback clock and server-wall-clock fmod straddle the
// period boundary (engine just wrapped to ~0, fmod still near Length) must NOT
// re-anchor — the circular-distance comparison reads the straddle as a SMALL drift,
// not a full period. Without it, every loop wrap of every looping move churned the
// wire. Authority also stays below the coarse re-anchor band for normal small drift.
// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusNetApplyLoopWrapNoSpuriousReanchor,
	"Paper2DPlus.Network.Clock.LoopWrapDoesNotSpuriouslyReanchor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusNetApplyLoopWrapNoSpuriousReanchor::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	UPaperFlipbook* FB = NetApply_AddMove(Asset, TEXT("Loop"), 8); // 0.8s loop period

	FNetApply_Rig Rig = NetApply_MakeRig();
	Rig.DataComp->SetNetContextOverrideForTests(EPaper2DPlusNetContext::Authority);
	Rig.DataComp->SetServerTimeOverrideForTests(NetApply_BaseServerTime);
	Rig.DataComp->NetPlaybackSnapToleranceSeconds = 0.1f;
	Rig.DataComp->CharacterProfile = Asset;

	// Publish a LOOPING move (looping must be set BEFORE the publish so RepAnimState.bLooping is true).
	Rig.FBComp->SetFlipbook(FB);
	Rig.FBComp->SetLooping(true);
	Rig.DataComp->HandleFlipbookChanged(FB);
	const FPaper2DPlusRepAnimState& Rep = Rig.DataComp->GetRepAnimStateForTests();
	TestTrue(TEXT("published move is looping"), (bool)Rep.bLooping);
	const double AnchorBefore = Rep.StartServerTime;

	// Straddle the loop boundary: local engine position just AFTER the wrap (0.02), server-formula
	// position just BEFORE it (0.78) — raw |0.78-0.02|=0.76 (~ a full period), circular distance
	// min(0.76, 0.8-0.76)=0.04 (well under the band). anchor==base (published at position 0), so
	// Expected=fmod(now-base,0.8): now=base+0.78 => Expected=0.78.
	Rig.FBComp->SetPlaybackPosition(0.02f, false);
	Rig.DataComp->SetServerTimeOverrideForTests(NetApply_BaseServerTime + 0.78);
	Rig.DataComp->HandleFrameChanged(1); // runs the authority stale-detect

	TestTrue(TEXT("loop-wrap straddle does NOT re-anchor (circular distance, not raw |E-A|)"),
		FMath::IsNearlyEqual(Rep.StartServerTime, AnchorBefore, 1.e-4));
	TestEqual(TEXT("Sequence unchanged (no spurious republish)"), (int32)Rep.Sequence, 1);

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// RepublishAnimStateAuthorityOnly: on Authority, RepublishAnimState re-anchors to
// the current actual position keeping the SAME Sequence (no bump); on a
// non-authority context it warns once and changes nothing.
// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusNetApplyRepublishAnimStateAuthorityOnly,
	"Paper2DPlus.Network.Clock.RepublishAnimStateAuthorityOnly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusNetApplyRepublishAnimStateAuthorityOnly::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	UPaperFlipbook* FB = NetApply_AddMove(Asset, TEXT("BMove"), 8);

	// Authority: re-anchors, same Sequence.
	{
		FNetApply_Rig Rig = NetApply_MakeRig();
		Rig.DataComp->SetNetContextOverrideForTests(EPaper2DPlusNetContext::Authority);
		Rig.DataComp->SetServerTimeOverrideForTests(NetApply_BaseServerTime);
		Rig.DataComp->CharacterProfile = Asset;

		Rig.FBComp->SetFlipbook(FB);
		Rig.DataComp->HandleFlipbookChanged(FB);
		const FPaper2DPlusRepAnimState& Rep = Rig.DataComp->GetRepAnimStateForTests();
		const int32 SeqBefore = (int32)Rep.Sequence;
		TestEqual(TEXT("warm-up published seq 1"), SeqBefore, 1);

		// Game-side mutation, then the explicit re-anchor escape hatch.
		Rig.FBComp->SetPlaybackPosition(0.3f, false);
		Rig.DataComp->RepublishAnimState();

		TestEqual(TEXT("RepublishAnimState keeps the SAME Sequence"), (int32)Rep.Sequence, SeqBefore);
		TestTrue(TEXT("anchor re-anchored to the new actual position (1000 - 0.3)"),
			FMath::IsNearlyEqual(Rep.StartServerTime, NetApply_BaseServerTime - 0.3, 1.e-3));
	}

	// Non-authority: warn-once + no change.
	{
		FNetApply_Rig Rig = NetApply_MakeRig();
		Rig.DataComp->SetNetContextOverrideForTests(EPaper2DPlusNetContext::SimulatedProxy);
		Rig.DataComp->SetServerTimeOverrideForTests(NetApply_BaseServerTime);
		Rig.DataComp->CharacterProfile = Asset;
		Rig.FBComp->SetFlipbook(FB);

		AddExpectedError(TEXT("RepublishAnimState called on a network proxy"),
			EAutomationExpectedErrorFlags::Contains);

		const FPaper2DPlusRepAnimState& Rep = Rig.DataComp->GetRepAnimStateForTests();
		const int32 SeqBefore = (int32)Rep.Sequence; // 0 — a proxy never published
		Rig.DataComp->RepublishAnimState();
		TestEqual(TEXT("non-authority RepublishAnimState changed nothing (Sequence stays)"),
			(int32)Rep.Sequence, SeqBefore);

		// Warn-once: a second call stays silent (the latch holds — a second emission would FAIL as an
		// unexpected error since AddExpectedError without Occurrences asserts the message appears).
		Rig.DataComp->RepublishAnimState();
	}

	// Standalone (single-player): a CLEAN no-op — NEVER warns (review fix: Standalone != Authority but is
	// not a misuse; the most common Fab consumer calls this in single-player). No AddExpectedError is
	// registered, so ANY warning emitted here fails the test.
	{
		FNetApply_Rig Rig = NetApply_MakeRig(/*bEnableReplication=*/false);
		Rig.DataComp->CharacterProfile = Asset;
		Rig.FBComp->SetFlipbook(FB);

		const FPaper2DPlusRepAnimState& Rep = Rig.DataComp->GetRepAnimStateForTests();
		const int32 SeqBefore = (int32)Rep.Sequence; // 0 — nothing published in Standalone
		Rig.DataComp->RepublishAnimState();
		Rig.DataComp->RepublishAnimState(); // a second call also stays silent
		TestEqual(TEXT("Standalone RepublishAnimState is a clean no-op (Sequence unchanged)"),
			(int32)Rep.Sequence, SeqBefore);
	}

	return true;
}

// ════════════════════════════════════════════════════════════════════════════
// E. SINGLE-PLAYER INVARIANCE (Paper2DPlus.Network.Apply.SinglePlayerInvariance)
// ════════════════════════════════════════════════════════════════════════════

// ─────────────────────────────────────────────────────────────────────────────
// SinglePlayerInvariance: with bEnableReplication=false (Standalone, no overrides),
// the FULL U8 surface behaves byte-identically to pre-U8: one-shots fire, the
// ranged event's Begin/Tick/End fire normally, and GetRootMotionDelta/
// ConsumeRootMotionDelta return real (non-zero) deltas — none of the proxy gates
// engage. Every gate is identity in Standalone, and nothing ever publishes.
//
// The actor DISPLACEMENT (root motion + sprite offset physically move the owner)
// requires a world — ApplyRootMotionWorldDelta early-returns when !Owner->GetWorld()
// — so a worldless rig can never move, regardless of context. That displacement is
// covered by the U7 PIE matrix; worldless asserts the gating + baseline only.
// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusNetApplySinglePlayerInvariance,
	"Paper2DPlus.Network.Apply.SinglePlayerInvariance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusNetApplySinglePlayerInvariance::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	UPaperFlipbook* FB = NetApply_AddMove(Asset, TEXT("Lunge"), 5);
	NetApply_AuthorRootMotion(Asset, 0, /*PerFramePixelsX=*/16.0f);
	NetApply_AuthorSpriteOffset(Asset, 0, /*FrameIndex=*/2, FIntPoint(48, 0));

	// A frame-0 Moment + a Cue State spanning frames [1,3].
	UPaper2DPlusTestMomentCue* Frame0Moment = NewObject<UPaper2DPlusTestMomentCue>(Asset);
	Frame0Moment->TriggerFrame = 0;
	Asset->Flipbooks[0].FrameEventData.FrameCues.Add(Frame0Moment);
	UPaper2DPlusTestRangeCue* Range = NewObject<UPaper2DPlusTestRangeCue>(Asset);
	Range->StartFrame = 1;
	Range->FrameCount = 3; // frames 1,2,3
	Range->bEmitUpdates = true;
	// AuthorityOnly — the policy that WOULD gate on a proxy. In Standalone it must fire (identity).
	Range->NetPolicy = EPaper2DPlusFrameCueNetPolicy::AuthorityOnly;
	Asset->Flipbooks[0].FrameEventData.FrameCues.Add(Range);

	FNetApply_Rig Rig = NetApply_MakeRig(/*bEnableReplication=*/false); // no overrides: offline path
	Rig.DataComp->bAutoApplyRootMotion = true;
	Rig.DataComp->CharacterProfile = Asset;
	UPaper2DPlusFrameCueRecorder* Recorder = NewObject<UPaper2DPlusFrameCueRecorder>();
	Rig.DataComp->OnFrameCue.AddDynamic(Recorder, &UPaper2DPlusFrameCueRecorder::OnCue);

	TestEqual(TEXT("offline rig resolves Standalone"),
		(int32)Rig.DataComp->GetNetContext(), (int32)EPaper2DPlusNetContext::Standalone);

	Rig.FBComp->SetFlipbook(FB);
	Rig.DataComp->HandleFlipbookChanged(FB); // frame-0 broadcast + root-motion seed
	TestTrue(TEXT("frame-0 Moment broadcasts in single player"), Recorder->Cues.Contains(Frame0Moment));

	// Drive the move through the ranged span and the sprite-offset frame. The baseline advances to
	// frame 4's authored position by the end of this drive.
	Rig.DataComp->HandleFrameChanged(1); // ranged Begin + Tick
	Rig.DataComp->HandleFrameChanged(2); // ranged Tick + sprite offset frame
	Rig.DataComp->HandleFrameChanged(3); // ranged Tick
	Rig.DataComp->HandleFrameChanged(4); // ranged End

	int32 BeginCount = 0;
	int32 UpdateCount = 0;
	int32 EndCount = 0;
	for (int32 Index = 0; Index < Recorder->Contexts.Num(); ++Index)
	{
		if (Recorder->Cues[Index] != Range)
		{
			continue;
		}
		BeginCount += Recorder->Contexts[Index].Phase == EPaper2DPlusFrameCuePhase::Begin ? 1 : 0;
		UpdateCount += Recorder->Contexts[Index].Phase == EPaper2DPlusFrameCuePhase::Update ? 1 : 0;
		EndCount += Recorder->Contexts[Index].Phase == EPaper2DPlusFrameCuePhase::End ? 1 : 0;
	}
	TestEqual(TEXT("Range Begin emits normally (Standalone identity)"), BeginCount, 1);
	// Range [1,3], bEmitUpdates: frames 1,2,3 driven in-range each emit EXACTLY one Update (deterministic) => 3.
	TestEqual(TEXT("Range Updates emit across the span"), UpdateCount, 3);
	TestEqual(TEXT("Range End emits normally"), EndCount, 1);

	// single-player owner displacement (root motion + sprite offset physically applied) requires a
	// world — covered by the U7 PIE matrix; worldless asserts the gating + baseline only.

	// GetRootMotionDelta / ConsumeRootMotionDelta return real values (no proxy gate). The baseline sits
	// at frame 4's position after the drive; sync the playback to the MID of frame 3 (3*0.1 + 0.05 =
	// 0.35, FPS 10 / FrameRun 1) so the const peek reads frame 3 unambiguously (away from the frame-
	// boundary </>= edge) — frame 3's authored pos differs from the frame-4 baseline, so the per-frame
	// delta is genuinely non-zero, proving the Standalone path returns a real delta (no proxy zero-gate).
	Rig.FBComp->SetPlaybackPosition(3 * 0.1f + 0.05f, false); // mid-frame 3
	// GetRootMotionDelta is a const peek (does not advance the baseline).
	const FVector Peek = Rig.DataComp->GetRootMotionDelta();
	TestFalse(TEXT("GetRootMotionDelta returns a real per-frame delta in single player"),
		Peek.IsNearlyZero());
	const FVector RealConsume = Rig.DataComp->ConsumeRootMotionDelta();
	TestFalse(TEXT("ConsumeRootMotionDelta returns a real delta in single player"),
		RealConsume.IsNearlyZero());

	// Nothing ever publishes offline.
	TestEqual(TEXT("nothing publishes in single player (Sequence stays 0)"),
		(int32)Rig.DataComp->GetRepAnimStateForTests().Sequence, 0);

	TestEqual(TEXT("still Standalone after the scripted sequence"),
		(int32)Rig.DataComp->GetNetContext(), (int32)EPaper2DPlusNetContext::Standalone);

	return true;
}

#endif // !UE_BUILD_SHIPPING

#endif // WITH_EDITOR
