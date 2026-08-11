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
#include "Paper2DPlusTestTransitionTypes.h"
#include "PaperFlipbook.h"
#include "PaperFlipbookComponent.h"
#include "PaperSprite.h"
#include "GameFramework/Actor.h"
#include "Containers/Ticker.h"
#include "Net/UnrealNetwork.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "UObject/CoreNet.h"
#include "UObject/UnrealType.h"

/** TASK-57 U2 replicated-anim-state-core tests (worldless, driven through the U1/U2 test seams:
 *  SetNetContextOverrideForTests / SetServerTimeOverrideForTests / the U2 accessors). Pins the
 *  publish producer invariants (incl. the Sequence sentinel-skip at the 65,536 boundary), the
 *  OnRep_CharacterProfile prewritten-property stash-restore funnel, the RepProfileSeq pairing
 *  (incl. the spawn-order deadlock prevention), the OnRep_AnimState decision tree (stash/defer,
 *  un-wedge clears, same-seq outcome rules, commit-before-broadcast), the self-loop confirm
 *  republish, the SetCharacterProfile proxy gate, and the single-player invariance suite.
 *  Helpers are NetAnim_-prefixed per the unity-build file-unique-name rule. */

namespace
{
	/** Server-clock base used by the rigs (any constant works — anchors are relative). */
	constexpr double NetAnim_BaseServerTime = 1000.0;

	/** Build a flipbook with NumFrames key frames (FPS 10, FrameRun 1 — frame i lives at i*0.1s,
	 *  total duration NumFrames*0.1s). */
	UPaperFlipbook* NetAnim_MakeFlipbook(UObject* Owner, int32 NumFrames)
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
	UPaperFlipbook* NetAnim_AddMove(UPaper2DPlusCharacterProfileAsset* Asset, const FString& MoveName, int32 NumFrames)
	{
		FFlipbookProfileEntry Anim;
		Anim.Identity.FlipbookName = MoveName;
		UPaperFlipbook* FB = NetAnim_MakeFlipbook(Asset, NumFrames);
		Anim.Identity.Flipbook = FB;
		Anim.CombatData.Frames.SetNum(NumFrames);
		Anim.CombatData.FrameExtractionInfo.SetNum(NumFrames);
		Asset->Flipbooks.Add(Anim);
		return FB;
	}

	/** Worldless rig: actor + stock flipbook component + profile component, wired (NOT warmed —
	 *  each scenario sequences its own overrides/profile/flipbook arrival). */
	struct FNetAnim_Rig
	{
		AActor* Owner = nullptr;
		UPaperFlipbookComponent* FBComp = nullptr;
		UPaper2DPlusCharacterProfileComponent* DataComp = nullptr;
	};

	FNetAnim_Rig NetAnim_MakeRig(bool bEnableReplication = true)
	{
		FNetAnim_Rig Rig;
		Rig.Owner = NewObject<AActor>();
		Rig.FBComp = NewObject<UPaperFlipbookComponent>(Rig.Owner);
		Rig.DataComp = NewObject<UPaper2DPlusCharacterProfileComponent>(Rig.Owner);
		Rig.Owner->AddOwnedComponent(Rig.FBComp);
		Rig.Owner->AddOwnedComponent(Rig.DataComp);
		Rig.DataComp->FlipbookComponent = Rig.FBComp;
		// Set the opt-in directly (worldless — never BeginPlays, so ResolveNetContext reads the live
		// flag): keeps the archetype-mismatch diagnostic quiet on rigs that drive OnReps.
		Rig.DataComp->bEnableReplication = bEnableReplication;
		return Rig;
	}

	UPaper2DPlusNetAnimStateRecorder* NetAnim_BindRecorder(FNetAnim_Rig& Rig)
	{
		UPaper2DPlusNetAnimStateRecorder* Recorder = NewObject<UPaper2DPlusNetAnimStateRecorder>();
		Rig.DataComp->OnReplicatedAnimStateChanged.AddDynamic(Recorder, &UPaper2DPlusNetAnimStateRecorder::OnReplicatedAnimState);
		return Recorder;
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Struct field defaults (Sequence=0 = never-published semantics) + serialization
// round-trip via SerializeTaggedProperties over an FObjectAndNameAsStringProxyArchive
// wrapped memory archive (NOT a bare FBitWriter — plain bit archives are not
// FName-aware; the real net path supplies a package map). Covers AC2.
// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusNetAnimStateStructDefaultsAndRoundTrip,
	"Paper2DPlus.Network.AnimState.StructDefaultsAndSerializationRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusNetAnimStateStructDefaultsAndRoundTrip::RunTest(const FString& Parameters)
{
	// Defaults — the never-published wire shape.
	const FPaper2DPlusRepAnimState Defaults;
	TestEqual(TEXT("Sequence defaults 0 (never published)"), (int32)Defaults.Sequence, 0);
	TestEqual(TEXT("ProfileSeq defaults 0"), (int32)Defaults.ProfileSeq, 0);
	TestTrue(TEXT("MoveName defaults None"), Defaults.MoveName.IsNone());
	TestTrue(TEXT("StartServerTime defaults -1 (no anchor)"), FMath::IsNearlyEqual(Defaults.StartServerTime, -1.0, 1.e-9));
	TestTrue(TEXT("PlayRate defaults 1"), FMath::IsNearlyEqual(Defaults.PlayRate, 1.0f, 1.e-6f));
	TestFalse(TEXT("bLooping defaults false"), (bool)Defaults.bLooping);
	TestFalse(TEXT("bReverse defaults false"), (bool)Defaults.bReverse);
	TestFalse(TEXT("bFlipX defaults false"), (bool)Defaults.bFlipX);
	TestFalse(TEXT("bIsProfileMove defaults false"), (bool)Defaults.bIsProfileMove);
	TestEqual(TEXT("LastRequestId defaults 0 (reserved seam)"), (int32)Defaults.LastRequestId, 0);

	const FPaper2DPlusRepHitStop HitStopDefaults;
	TestEqual(TEXT("HitStopSeq defaults 0"), (int32)HitStopDefaults.HitStopSeq, 0);
	TestTrue(TEXT("HitStop Duration defaults 0 (no freeze)"), FMath::IsNearlyEqual(HitStopDefaults.DurationSeconds, 0.0f, 1.e-6f));
	TestTrue(TEXT("HitStop anchor defaults -1"), FMath::IsNearlyEqual(HitStopDefaults.StartServerTime, -1.0, 1.e-9));
	TestNull(TEXT("HitStop victim defaults null"), HitStopDefaults.Victim.Get());

	// Round-trip: every field populated distinctly.
	FPaper2DPlusRepAnimState Src;
	Src.Sequence = 7;
	Src.ProfileSeq = 3;
	Src.MoveName = FName(TEXT("Slash"));
	Src.StartServerTime = 123.5;
	Src.PlayRate = 2.0f;
	Src.bLooping = true;
	Src.bReverse = true;
	Src.bFlipX = true;
	Src.bIsProfileMove = true;
	Src.LastRequestId = 9;

	TArray<uint8> Bytes;
	{
		FMemoryWriter MemWriter(Bytes);
		FObjectAndNameAsStringProxyArchive Writer(MemWriter, /*bInLoadIfFindFails=*/false);
		FPaper2DPlusRepAnimState::StaticStruct()->SerializeTaggedProperties(
			Writer, reinterpret_cast<uint8*>(&Src), FPaper2DPlusRepAnimState::StaticStruct(), nullptr);
	}
	TestTrue(TEXT("serialized payload is non-empty"), Bytes.Num() > 0);

	FPaper2DPlusRepAnimState Dst;
	{
		FMemoryReader MemReader(Bytes);
		FObjectAndNameAsStringProxyArchive Reader(MemReader, /*bInLoadIfFindFails=*/false);
		FPaper2DPlusRepAnimState::StaticStruct()->SerializeTaggedProperties(
			Reader, reinterpret_cast<uint8*>(&Dst), FPaper2DPlusRepAnimState::StaticStruct(), nullptr);
	}

	TestEqual(TEXT("Sequence round-trips"), (int32)Dst.Sequence, 7);
	TestEqual(TEXT("ProfileSeq round-trips"), (int32)Dst.ProfileSeq, 3);
	TestTrue(TEXT("MoveName round-trips"), Dst.MoveName == FName(TEXT("Slash")));
	TestTrue(TEXT("StartServerTime round-trips"), FMath::IsNearlyEqual(Dst.StartServerTime, 123.5, 1.e-9));
	TestTrue(TEXT("PlayRate round-trips"), FMath::IsNearlyEqual(Dst.PlayRate, 2.0f, 1.e-6f));
	TestTrue(TEXT("bLooping round-trips"), (bool)Dst.bLooping);
	TestTrue(TEXT("bReverse round-trips"), (bool)Dst.bReverse);
	TestTrue(TEXT("bFlipX round-trips"), (bool)Dst.bFlipX);
	TestTrue(TEXT("bIsProfileMove round-trips"), (bool)Dst.bIsProfileMove);
	TestEqual(TEXT("LastRequestId round-trips"), (int32)Dst.LastRequestId, 9);

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Replication registration pins (reflection): CharacterProfile is RepNotify
// (OnRep_CharacterProfile) registered REPNOTIFY_Always; RepProfileSeq is plain
// Replicated (rides the same bunch); RepAnimState/RepHitStop carry their OnReps.
// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusNetAnimStateReplicationSpecDeclared,
	"Paper2DPlus.Network.AnimState.ReplicationSpecDeclared",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusNetAnimStateReplicationSpecDeclared::RunTest(const FString& Parameters)
{
	UClass* Cls = UPaper2DPlusCharacterProfileComponent::StaticClass();
	Cls->SetUpRuntimeReplicationData(); // RepIndex assignment — required before lifetime matching.

	TArray<FLifetimeProperty> Lifetime;
	GetDefault<UPaper2DPlusCharacterProfileComponent>()->GetLifetimeReplicatedProps(Lifetime);

	auto FindLifetimeFor = [&Lifetime](const FProperty* Prop) -> const FLifetimeProperty*
	{
		if (!Prop)
		{
			return nullptr;
		}
		for (const FLifetimeProperty& Entry : Lifetime)
		{
			if (Entry.RepIndex == Prop->RepIndex)
			{
				return &Entry;
			}
		}
		return nullptr;
	};

	// CharacterProfile: ReplicatedUsing=OnRep_CharacterProfile + REPNOTIFY_Always.
	{
		const FProperty* Prop = Cls->FindPropertyByName(TEXT("CharacterProfile"));
		TestNotNull(TEXT("CharacterProfile property exists"), Prop);
		if (Prop)
		{
			TestTrue(TEXT("CharacterProfile is replicated"), Prop->HasAnyPropertyFlags(CPF_Net));
			TestTrue(TEXT("CharacterProfile carries RepNotify"), Prop->HasAnyPropertyFlags(CPF_RepNotify));
			TestTrue(TEXT("CharacterProfile RepNotify func is OnRep_CharacterProfile"),
				Prop->RepNotifyFunc == FName(TEXT("OnRep_CharacterProfile")));
			const FLifetimeProperty* Entry = FindLifetimeFor(Prop);
			TestNotNull(TEXT("CharacterProfile registered in GetLifetimeReplicatedProps"), Entry);
			if (Entry)
			{
				TestEqual(TEXT("CharacterProfile registered REPNOTIFY_Always"),
					(int32)Entry->RepNotifyCondition, (int32)REPNOTIFY_Always);
				TestEqual(TEXT("CharacterProfile condition COND_None"), (int32)Entry->Condition, (int32)COND_None);
			}
		}
	}

	// RepProfileSeq: plain Replicated (no RepNotify) — the same-bunch pairing rider.
	{
		const FProperty* Prop = Cls->FindPropertyByName(TEXT("RepProfileSeq"));
		TestNotNull(TEXT("RepProfileSeq property exists"), Prop);
		if (Prop)
		{
			TestTrue(TEXT("RepProfileSeq is replicated"), Prop->HasAnyPropertyFlags(CPF_Net));
			TestFalse(TEXT("RepProfileSeq has NO RepNotify"), Prop->HasAnyPropertyFlags(CPF_RepNotify));
			TestNotNull(TEXT("RepProfileSeq registered"), FindLifetimeFor(Prop));
		}
	}

	// RepAnimState / RepHitStop: ReplicatedUsing OnReps.
	{
		const FProperty* Prop = Cls->FindPropertyByName(TEXT("RepAnimState"));
		TestNotNull(TEXT("RepAnimState property exists"), Prop);
		if (Prop)
		{
			TestTrue(TEXT("RepAnimState is replicated"), Prop->HasAnyPropertyFlags(CPF_Net));
			TestTrue(TEXT("RepAnimState RepNotify func is OnRep_AnimState"),
				Prop->RepNotifyFunc == FName(TEXT("OnRep_AnimState")));
			TestNotNull(TEXT("RepAnimState registered"), FindLifetimeFor(Prop));
		}
	}
	{
		const FProperty* Prop = Cls->FindPropertyByName(TEXT("RepHitStop"));
		TestNotNull(TEXT("RepHitStop property exists"), Prop);
		if (Prop)
		{
			TestTrue(TEXT("RepHitStop is replicated"), Prop->HasAnyPropertyFlags(CPF_Net));
			TestTrue(TEXT("RepHitStop RepNotify func is OnRep_HitStop"),
				Prop->RepNotifyFunc == FName(TEXT("OnRep_HitStop")));
			TestNotNull(TEXT("RepHitStop registered"), FindLifetimeFor(Prop));
		}
	}

	return true;
}

// Everything below drives the !UE_BUILD_SHIPPING test seams.
#if !UE_BUILD_SHIPPING

// ─────────────────────────────────────────────────────────────────────────────
// Publish pins: an Authority-context flipbook change populates the snapshot —
// move name, anchor math against the overridden server clock, flags incl.
// bFlipX, and the producer invariant Sequence == uint16(MoveInstanceCounter).
// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusNetAnimStatePublishPopulatesSnapshot,
	"Paper2DPlus.Network.AnimState.PublishPopulatesSnapshotOnAuthority",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusNetAnimStatePublishPopulatesSnapshot::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	UPaperFlipbook* FB = NetAnim_AddMove(Asset, TEXT("Slash"), 4); // 0.4s total

	FNetAnim_Rig Rig = NetAnim_MakeRig();
	Rig.DataComp->SetNetContextOverrideForTests(EPaper2DPlusNetContext::Authority);
	Rig.DataComp->SetServerTimeOverrideForTests(NetAnim_BaseServerTime);
	Rig.DataComp->CharacterProfile = Asset;

	Rig.FBComp->SetFlipbook(FB);
	Rig.FBComp->SetPlayRate(2.0f);
	Rig.FBComp->SetPlaybackPosition(0.25f, false);
	Rig.DataComp->HandleFlipbookChanged(FB);

	const FPaper2DPlusRepAnimState& Rep = Rig.DataComp->GetRepAnimStateForTests();
	TestEqual(TEXT("first publish carries Sequence 1 (authority starts at 1)"), (int32)Rep.Sequence, 1);
	TestEqual(TEXT("producer invariant: Sequence == uint16(MoveInstanceCounter)"),
		(int32)Rep.Sequence, (int32)(uint16)Rig.DataComp->GetMoveInstanceCounterForTests());
	TestEqual(TEXT("ProfileSeq stamps the current generation"),
		(int32)Rep.ProfileSeq, (int32)Rig.DataComp->GetProfileChangeCounterForTests());
	TestTrue(TEXT("MoveName is the authored profile name"), Rep.MoveName == FName(TEXT("Slash")));
	TestTrue(TEXT("bIsProfileMove for a profile move"), (bool)Rep.bIsProfileMove);
	TestTrue(TEXT("PlayRate snapshot"), FMath::IsNearlyEqual(Rep.PlayRate, 2.0f, 1.e-6f));
	TestEqual(TEXT("bLooping mirrors the component"), (bool)Rep.bLooping, Rig.FBComp->IsLooping());
	TestEqual(TEXT("bReverse mirrors the component"), (bool)Rep.bReverse, Rig.FBComp->IsReversing());
	TestFalse(TEXT("bFlipX false while facing right"), (bool)Rep.bFlipX);
	// Anchor: ServerNow - CurrentPos/PlayRate = 1000 - 0.25/2 = 999.875.
	TestTrue(TEXT("anchor math: ServerNow - CurrentPos/PlayRate"),
		FMath::IsNearlyEqual(Rep.StartServerTime, NetAnim_BaseServerTime - 0.125, 1.e-6));

	// Facing-left republish: bFlipX rides the struct (KTD-4).
	Rig.FBComp->SetRelativeScale3D(FVector(-1.0f, 1.0f, 1.0f));
	Rig.DataComp->HandleFlipbookChanged(FB);
	TestEqual(TEXT("second publish bumps Sequence"), (int32)Rep.Sequence, 2);
	TestEqual(TEXT("producer invariant holds on republish"),
		(int32)Rep.Sequence, (int32)(uint16)Rig.DataComp->GetMoveInstanceCounterForTests());
	TestTrue(TEXT("bFlipX true while facing left"), (bool)Rep.bFlipX);

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Publish pins, continued: a non-profile flipbook publishes an authoritative
// CLEAR (bIsProfileMove=false); a Standalone context publishes NOTHING but the
// MoveInstanceCounter/ProcessedHits bookkeeping advances unconditionally.
// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusNetAnimStatePublishClearAndStandaloneSkip,
	"Paper2DPlus.Network.AnimState.PublishClearAndStandaloneSkip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusNetAnimStatePublishClearAndStandaloneSkip::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	NetAnim_AddMove(Asset, TEXT("Slash"), 4);

	// Authority + a flipbook the profile has NO entry for => CLEAR.
	{
		FNetAnim_Rig Rig = NetAnim_MakeRig();
		Rig.DataComp->SetNetContextOverrideForTests(EPaper2DPlusNetContext::Authority);
		Rig.DataComp->SetServerTimeOverrideForTests(NetAnim_BaseServerTime);
		Rig.DataComp->CharacterProfile = Asset;

		UPaperFlipbook* Locomotion = NetAnim_MakeFlipbook(Rig.Owner, 3);
		Rig.FBComp->SetFlipbook(Locomotion);
		Rig.DataComp->HandleFlipbookChanged(Locomotion);

		const FPaper2DPlusRepAnimState& Rep = Rig.DataComp->GetRepAnimStateForTests();
		TestEqual(TEXT("clear publish carries a live Sequence"), (int32)Rep.Sequence, 1);
		TestFalse(TEXT("non-profile flipbook publishes bIsProfileMove=false"), (bool)Rep.bIsProfileMove);
		TestTrue(TEXT("clear publish carries MoveName None"), Rep.MoveName.IsNone());
	}

	// Standalone: publish skipped entirely; counter + dedup reset advance unconditionally.
	{
		UPaper2DPlusCharacterProfileAsset* Asset2 = NewObject<UPaper2DPlusCharacterProfileAsset>();
		UPaperFlipbook* FB2 = NetAnim_AddMove(Asset2, TEXT("Slash"), 4);
		FNetAnim_Rig Rig = NetAnim_MakeRig(); // no context override => worldless Standalone
		Rig.DataComp->CharacterProfile = Asset2;

		Rig.DataComp->GetProcessedHitsForTests().Add(
			FPaper2DPlusHitDedupKey(Rig.Owner, Rig.Owner, /*MoveInstance=*/1, /*HitWindowIndex=*/0));

		Rig.FBComp->SetFlipbook(FB2);
		Rig.DataComp->HandleFlipbookChanged(FB2);

		TestEqual(TEXT("Standalone never publishes (Sequence stays 0)"),
			(int32)Rig.DataComp->GetRepAnimStateForTests().Sequence, 0);
		TestEqual(TEXT("MoveInstanceCounter advances unconditionally"),
			(int32)Rig.DataComp->GetMoveInstanceCounterForTests(), 1);
		TestEqual(TEXT("ProcessedHits reset unconditionally"),
			Rig.DataComp->GetProcessedHitsForTests().Num(), 0);
	}

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// The Sequence sentinel-skip at the 65,536 boundary: when uint16(counter) lands
// on 0, the COUNTER itself is bumped before stamping (not just the wire value)
// so the producer invariant survives the wrap and 0 stays the never-published
// sentinel. The counter is driven to the boundary via the U2 test seam.
// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusNetAnimStateSentinelSkipAtBoundary,
	"Paper2DPlus.Network.AnimState.PublishSentinelSkipAtSequenceBoundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusNetAnimStateSentinelSkipAtBoundary::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	UPaperFlipbook* FB = NetAnim_AddMove(Asset, TEXT("Slash"), 4);

	FNetAnim_Rig Rig = NetAnim_MakeRig();
	Rig.DataComp->SetNetContextOverrideForTests(EPaper2DPlusNetContext::Authority);
	Rig.DataComp->SetServerTimeOverrideForTests(NetAnim_BaseServerTime);
	Rig.DataComp->CharacterProfile = Asset;
	Rig.FBComp->SetFlipbook(FB);

	// 65535 + the OnFlipbookChanged increment = 65536 => uint16 == 0 => skip bumps to 65537.
	Rig.DataComp->SetMoveInstanceCounterForTests(65535u);
	Rig.DataComp->HandleFlipbookChanged(FB);

	const FPaper2DPlusRepAnimState& Rep = Rig.DataComp->GetRepAnimStateForTests();
	TestEqual(TEXT("boundary publish skips the 0 sentinel (Sequence 1)"), (int32)Rep.Sequence, 1);
	TestEqual(TEXT("the COUNTER was bumped past the boundary (65537)"),
		(int32)Rig.DataComp->GetMoveInstanceCounterForTests(), 65537);
	TestEqual(TEXT("producer invariant holds across the wrap"),
		(int32)Rep.Sequence, (int32)(uint16)Rig.DataComp->GetMoveInstanceCounterForTests());

	// The next instance continues normally.
	Rig.DataComp->HandleFlipbookChanged(FB);
	TestEqual(TEXT("post-boundary publish continues at 2"), (int32)Rep.Sequence, 2);
	TestEqual(TEXT("counter continues at 65538"), (int32)Rig.DataComp->GetMoveInstanceCounterForTests(), 65538);

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Anchor fallbacks: PlayRate <= 0 publishes anchor -1 and never divides; a
// publish without a server clock (override unset + worldless) anchors -1.
// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusNetAnimStatePublishAnchorFallbacks,
	"Paper2DPlus.Network.AnimState.PublishAnchorFallbacks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusNetAnimStatePublishAnchorFallbacks::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	UPaperFlipbook* FB = NetAnim_AddMove(Asset, TEXT("Slash"), 4);

	FNetAnim_Rig Rig = NetAnim_MakeRig();
	Rig.DataComp->SetNetContextOverrideForTests(EPaper2DPlusNetContext::Authority);
	Rig.DataComp->SetServerTimeOverrideForTests(NetAnim_BaseServerTime);
	Rig.DataComp->CharacterProfile = Asset;
	Rig.FBComp->SetFlipbook(FB);

	// PlayRate 0 (paused) — unrepresentable in the anchor model: anchor -1, no division.
	Rig.FBComp->SetPlayRate(0.0f);
	Rig.DataComp->HandleFlipbookChanged(FB);
	const FPaper2DPlusRepAnimState& Rep = Rig.DataComp->GetRepAnimStateForTests();
	TestTrue(TEXT("PlayRate 0 publishes anchor -1"), FMath::IsNearlyEqual(Rep.StartServerTime, -1.0, 1.e-9));
	TestTrue(TEXT("PlayRate 0 rides the wire"), FMath::IsNearlyEqual(Rep.PlayRate, 0.0f, 1.e-6f));

	// No server clock (override cleared; worldless => no GameState): anchor -1.
	Rig.FBComp->SetPlayRate(1.0f);
	Rig.DataComp->SetServerTimeOverrideForTests(TOptional<double>());
	Rig.DataComp->HandleFlipbookChanged(FB);
	TestTrue(TEXT("clockless publish anchors -1"), FMath::IsNearlyEqual(Rep.StartServerTime, -1.0, 1.e-9));
	TestEqual(TEXT("clockless publish still carries a live Sequence"), (int32)Rep.Sequence, 2);

	// Reverse playback: the FORWARD anchor model cannot represent a position that decreases with
	// time — treated like PlayRate <= 0: anchor -1, never derived; bReverse still rides the wire
	// (full reverse-aware derivation lands with U8's clock work).
	Rig.DataComp->SetServerTimeOverrideForTests(NetAnim_BaseServerTime);
	Rig.FBComp->Reverse(); // bPlaying + bReversePlayback => IsReversing()
	Rig.DataComp->HandleFlipbookChanged(FB);
	TestTrue(TEXT("reversed publish anchors -1"), FMath::IsNearlyEqual(Rep.StartServerTime, -1.0, 1.e-9));
	TestTrue(TEXT("bReverse still rides the wire on the reversed publish"), (bool)Rep.bReverse);
	TestTrue(TEXT("PlayRate stays positive on the reversed publish (anchor -1 is the reverse signal)"),
		Rep.PlayRate > 0.0f);
	TestEqual(TEXT("reversed publish still carries a live Sequence"), (int32)Rep.Sequence, 3);

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// OnRep_CharacterProfile prewritten-property funnel (the no-op regression pin):
// the net driver pre-writes the property, so the OnRep must stash-restore to
// defeat the SetCharacterProfile identity early-out — caches warm against the
// NEW profile, the frame-0 ONE-SHOT is suppressed (minimal catch-up) while the
// frame-0 RANGED event keeps its normal bookkeeping, and the generation adopts
// from the paired RepProfileSeq.
// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusNetAnimStateOnRepProfileFunnel,
	"Paper2DPlus.Network.AnimState.OnRepProfilePrewrittenPropertyFunnel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusNetAnimStateOnRepProfileFunnel::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	UPaperFlipbook* FB = NetAnim_AddMove(Asset, TEXT("BMove"), 4);

	// Frame-0 one-shot (must be suppressed during the funnel) + frame-0 ranged (CosmeticOnly so it
	// dispatches on a simulated proxy and stays exempt from the LocalAlways guardrail).
	UPaper2DPlusTestMomentCue* Moment = NewObject<UPaper2DPlusTestMomentCue>(Asset);
	Moment->TriggerFrame = 0;
	Asset->Flipbooks[0].FrameEventData.FrameCues.Add(Moment);
	UPaper2DPlusTestRangeCue* Range = NewObject<UPaper2DPlusTestRangeCue>(Asset);
	Range->StartFrame = 0;
	Range->FrameCount = 2;
	Range->NetPolicy = EPaper2DPlusFrameCueNetPolicy::CosmeticOnly;
	Asset->Flipbooks[0].FrameEventData.FrameCues.Add(Range);

	FNetAnim_Rig Rig = NetAnim_MakeRig();
	Rig.DataComp->SetNetContextOverrideForTests(EPaper2DPlusNetContext::SimulatedProxy);
	Rig.DataComp->SetServerTimeOverrideForTests(NetAnim_BaseServerTime);
	UPaper2DPlusFrameCueRecorder* CueRecorder = NewObject<UPaper2DPlusFrameCueRecorder>();
	Rig.DataComp->OnFrameCue.AddDynamic(CueRecorder, &UPaper2DPlusFrameCueRecorder::OnCue);

	// The game already plays the move's flipbook (advisory mode: the game owns playback).
	Rig.FBComp->SetFlipbook(FB);

	// Simulate the net bunch: property pre-written, paired generation, then the RepNotify.
	Rig.DataComp->CharacterProfile = Asset;
	Rig.DataComp->GetRepProfileSeqForTests() = 1;
	Rig.DataComp->OnRep_CharacterProfile(nullptr);

	TestEqual(TEXT("generation adopted from the paired RepProfileSeq"),
		(int32)Rig.DataComp->GetProfileChangeCounterForTests(), 1);
	// Caches warmed against the NEW profile: the move resolves by its AUTHORED name (the flipbook
	// object name differs) — a naive re-call would have no-op'd on the identity early-out.
	TestEqual(TEXT("funnel warmed caches (move resolves by authored name)"),
		Rig.DataComp->GetCurrentMoveName(), FString(TEXT("BMove")));
	TestFalse(TEXT("frame-0 Moment suppressed during the replicated funnel"), CueRecorder->Cues.Contains(Moment));
	TestTrue(TEXT("frame-0 Cue State keeps its normal Begin"), Rig.DataComp->IsRangeCueActiveForTests(Range));

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Stash/defer + bunch-order pin: an anim state whose ProfileSeq is ahead of the
// local generation stashes (no broadcast) and re-applies after the paired
// profile OnRep adopts the generation.
// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusNetAnimStateStashDrainsAfterProfileOnRep,
	"Paper2DPlus.Network.AnimState.StashOnGenerationMismatchDrainsAfterProfileOnRep",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusNetAnimStateStashDrainsAfterProfileOnRep::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	UPaperFlipbook* FB = NetAnim_AddMove(Asset, TEXT("BMove"), 4);

	FNetAnim_Rig Rig = NetAnim_MakeRig();
	Rig.DataComp->SetNetContextOverrideForTests(EPaper2DPlusNetContext::SimulatedProxy);
	Rig.DataComp->SetServerTimeOverrideForTests(NetAnim_BaseServerTime);
	UPaper2DPlusNetAnimStateRecorder* Recorder = NetAnim_BindRecorder(Rig);

	// The anim state lands BEFORE its paired profile (split across frames / out-of-order arrival).
	FPaper2DPlusRepAnimState& Rep = Rig.DataComp->GetRepAnimStateForTests();
	Rep.Sequence = 5;
	Rep.ProfileSeq = 1;
	Rep.bIsProfileMove = true;
	Rep.MoveName = FName(TEXT("BMove"));
	Rep.StartServerTime = NetAnim_BaseServerTime - 0.1; // 0.1s into the move
	Rep.PlayRate = 1.0f;
	Rig.DataComp->OnRep_AnimState(FPaper2DPlusRepAnimState());

	TestEqual(TEXT("mismatched generation stashes (no broadcast)"), Recorder->BroadcastCount, 0);
	TestTrue(TEXT("pending stash armed"), Rig.DataComp->HasPendingRepAnimStateForTests());
	TestEqual(TEXT("nothing committed yet"), (int32)Rig.DataComp->GetLastAppliedSequenceForTests(), 0);

	// The paired profile bunch lands: adoption drains the stash.
	Rig.DataComp->CharacterProfile = Asset;
	Rig.DataComp->GetRepProfileSeqForTests() = 1;
	Rig.DataComp->OnRep_CharacterProfile(nullptr);

	TestEqual(TEXT("stash re-applied after the profile OnRep"), Recorder->BroadcastCount, 1);
	TestTrue(TEXT("advisory resolved the move"), Recorder->LastMoveName == FName(TEXT("BMove")));
	TestTrue(TEXT("advisory carries the resolved flipbook"), Recorder->LastFlipbook.Get() == FB);
	TestTrue(TEXT("anchor-derived position"), FMath::IsNearlyEqual(Recorder->LastPlaybackPosition, 0.1f, 1.e-3f));
	TestFalse(TEXT("mid-move not finished"), Recorder->bLastAlreadyFinished);
	TestEqual(TEXT("sequence committed"), (int32)Rig.DataComp->GetLastAppliedSequenceForTests(), 5);
	TestFalse(TEXT("stash consumed"), Rig.DataComp->HasPendingRepAnimStateForTests());

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// The generation is adopted ONLY from RepProfileSeq — NEVER from the anim
// struct (the adversarially-found spawn-order deadlock): a stashed state whose
// ProfileSeq is still ahead after a profile OnRep stays stashed.
// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusNetAnimStateGenerationAdoptionSource,
	"Paper2DPlus.Network.AnimState.GenerationNeverAdoptedFromAnimStruct",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusNetAnimStateGenerationAdoptionSource::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* AssetA = NewObject<UPaper2DPlusCharacterProfileAsset>();
	NetAnim_AddMove(AssetA, TEXT("AMove"), 4);
	UPaper2DPlusCharacterProfileAsset* AssetB = NewObject<UPaper2DPlusCharacterProfileAsset>();
	NetAnim_AddMove(AssetB, TEXT("AMove"), 4);

	FNetAnim_Rig Rig = NetAnim_MakeRig();
	Rig.DataComp->SetNetContextOverrideForTests(EPaper2DPlusNetContext::SimulatedProxy);
	Rig.DataComp->SetServerTimeOverrideForTests(NetAnim_BaseServerTime);
	UPaper2DPlusNetAnimStateRecorder* Recorder = NetAnim_BindRecorder(Rig);

	// Profile A, generation 1.
	Rig.DataComp->CharacterProfile = AssetA;
	Rig.DataComp->GetRepProfileSeqForTests() = 1;
	Rig.DataComp->OnRep_CharacterProfile(nullptr);

	// A state from generation 3 stashes.
	FPaper2DPlusRepAnimState& Rep = Rig.DataComp->GetRepAnimStateForTests();
	Rep.Sequence = 6;
	Rep.ProfileSeq = 3;
	Rep.bIsProfileMove = true;
	Rep.MoveName = FName(TEXT("AMove"));
	Rig.DataComp->OnRep_AnimState(FPaper2DPlusRepAnimState());
	TestTrue(TEXT("future-generation state stashes"), Rig.DataComp->HasPendingRepAnimStateForTests());

	// A profile OnRep for generation 2 arrives: adopt 2 (from RepProfileSeq), and the stash with
	// ProfileSeq 3 must RE-STASH — adopting 3 from the anim struct would be the deadlock bug.
	Rig.DataComp->CharacterProfile = AssetB;
	Rig.DataComp->GetRepProfileSeqForTests() = 2;
	Rig.DataComp->OnRep_CharacterProfile(AssetA);

	TestEqual(TEXT("generation adopted from RepProfileSeq (2), not the anim struct (3)"),
		(int32)Rig.DataComp->GetProfileChangeCounterForTests(), 2);
	TestTrue(TEXT("still-ahead state remains stashed"), Rig.DataComp->HasPendingRepAnimStateForTests());
	TestEqual(TEXT("no broadcast for the still-ahead state"), Recorder->BroadcastCount, 0);

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Send-suppressed profile OnRep (the review BLOCKER pin): the net driver
// send-suppresses delta-equal properties, so a coalesced A→B→A profile swap (or
// a late joiner whose archetype equals the server's current pointer) ships
// RepProfileSeq WITHOUT firing OnRep_CharacterProfile — REPNOTIFY_Always governs
// the receive, not the send. ApplyReplicatedAnimState must adopt the generation
// from the PAIRED RepProfileSeq (already value-applied within the bunch) when
// the locally-held profile pointer is non-null, instead of stashing forever.
// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusNetAnimStatePairedSeqAdoption,
	"Paper2DPlus.Network.AnimState.GenerationAdoptsFromPairedSeqWithoutProfileOnRep",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusNetAnimStatePairedSeqAdoption::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	UPaperFlipbook* FB = NetAnim_AddMove(Asset, TEXT("BMove"), 4);

	FNetAnim_Rig Rig = NetAnim_MakeRig();
	Rig.DataComp->SetNetContextOverrideForTests(EPaper2DPlusNetContext::SimulatedProxy);
	Rig.DataComp->SetServerTimeOverrideForTests(NetAnim_BaseServerTime);
	UPaper2DPlusNetAnimStateRecorder* Recorder = NetAnim_BindRecorder(Rig);

	// Profile held locally with the generation still at its initial 0 — the pointer never moved, so
	// the profile OnRep never fired.
	Rig.DataComp->CharacterProfile = Asset;
	TestEqual(TEXT("local generation starts at 0"),
		(int32)Rig.DataComp->GetProfileChangeCounterForTests(), 0);

	// The coalesced-swap bunch: RepProfileSeq and the anim state value-applied, NO profile OnRep.
	Rig.DataComp->GetRepProfileSeqForTests() = 2;
	FPaper2DPlusRepAnimState& Rep = Rig.DataComp->GetRepAnimStateForTests();
	Rep.Sequence = 3;
	Rep.ProfileSeq = 2;
	Rep.bIsProfileMove = true;
	Rep.MoveName = FName(TEXT("BMove"));
	Rep.StartServerTime = NetAnim_BaseServerTime - 0.1;
	Rep.PlayRate = 1.0f;
	Rig.DataComp->OnRep_AnimState(FPaper2DPlusRepAnimState());

	TestEqual(TEXT("advisory FIRED — paired-seq adoption unwedged the apply (no stash)"),
		Recorder->BroadcastCount, 1);
	TestFalse(TEXT("no stash"), Rig.DataComp->HasPendingRepAnimStateForTests());
	TestEqual(TEXT("generation adopted from the paired RepProfileSeq"),
		(int32)Rig.DataComp->GetProfileChangeCounterForTests(), 2);
	TestTrue(TEXT("advisory resolved the move"), Recorder->LastMoveName == FName(TEXT("BMove")));
	TestTrue(TEXT("advisory carries the resolved flipbook"), Recorder->LastFlipbook.Get() == FB);
	TestEqual(TEXT("sequence committed"), (int32)Rig.DataComp->GetLastAppliedSequenceForTests(), 3);

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// RepProfileSeq pairing deadlock pin: the authority bumps the generation BEFORE
// the profile-swap publish funnel runs, so a publish triggered by a late
// flipbook arrival pairs with the CURRENT RepProfileSeq — a client consuming
// the pair applies without wedging.
// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusNetAnimStatePairingDeadlockPin,
	"Paper2DPlus.Network.AnimState.ProfileSwapPublishPairsWithNewGeneration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusNetAnimStatePairingDeadlockPin::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	UPaperFlipbook* FB = NetAnim_AddMove(Asset, TEXT("Slash"), 4);

	// Server: profile set BEFORE any flipbook (the spawn-order case) — no publish yet.
	FNetAnim_Rig Server = NetAnim_MakeRig();
	Server.DataComp->SetNetContextOverrideForTests(EPaper2DPlusNetContext::Authority);
	Server.DataComp->SetServerTimeOverrideForTests(NetAnim_BaseServerTime);
	Server.DataComp->SetCharacterProfile(Asset);

	TestEqual(TEXT("authority profile set bumps RepProfileSeq"),
		(int32)Server.DataComp->GetRepProfileSeqForTests(), 1);
	TestEqual(TEXT("no flipbook yet => no publish"),
		(int32)Server.DataComp->GetRepAnimStateForTests().Sequence, 0);

	// Late flipbook arrival: the publish must pair with the NEW generation.
	Server.FBComp->SetFlipbook(FB);
	Server.DataComp->HandleFlipbookChanged(FB);
	const FPaper2DPlusRepAnimState& ServerRep = Server.DataComp->GetRepAnimStateForTests();
	TestTrue(TEXT("late-arrival publish carries a live Sequence"), ServerRep.Sequence != 0);
	TestEqual(TEXT("publish ProfileSeq pairs with the current RepProfileSeq (the wedge-prevention pin)"),
		(int32)ServerRep.ProfileSeq, (int32)Server.DataComp->GetRepProfileSeqForTests());

	// Client consuming the pair applies — no stash wedge.
	FNetAnim_Rig Client = NetAnim_MakeRig();
	Client.DataComp->SetNetContextOverrideForTests(EPaper2DPlusNetContext::SimulatedProxy);
	Client.DataComp->SetServerTimeOverrideForTests(NetAnim_BaseServerTime);
	UPaper2DPlusNetAnimStateRecorder* Recorder = NetAnim_BindRecorder(Client);

	Client.DataComp->CharacterProfile = Asset;
	Client.DataComp->GetRepProfileSeqForTests() = Server.DataComp->GetRepProfileSeqForTests();
	Client.DataComp->OnRep_CharacterProfile(nullptr);
	Client.DataComp->GetRepAnimStateForTests() = ServerRep;
	Client.DataComp->OnRep_AnimState(FPaper2DPlusRepAnimState());

	TestEqual(TEXT("client applied the paired state"), Recorder->BroadcastCount, 1);
	TestTrue(TEXT("client resolved the move"), Recorder->LastMoveName == FName(TEXT("Slash")));
	TestFalse(TEXT("no wedged stash"), Client.DataComp->HasPendingRepAnimStateForTests());

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// No-server-clock stash + drain through the BeginPlay-tail retry path (the
// FlushPendingRepAnimStateForTests seam drives the SAME private retry), and a
// later applied OnRep discards a stale stash.
// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusNetAnimStateClocklessStashDrains,
	"Paper2DPlus.Network.AnimState.StashWithoutServerClockDrainsAtBeginPlayTail",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusNetAnimStateClocklessStashDrains::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	NetAnim_AddMove(Asset, TEXT("BMove"), 4);

	FNetAnim_Rig Rig = NetAnim_MakeRig();
	Rig.DataComp->SetNetContextOverrideForTests(EPaper2DPlusNetContext::SimulatedProxy);
	// NO server-time override yet — worldless means no GameState either: clockless.
	UPaper2DPlusNetAnimStateRecorder* Recorder = NetAnim_BindRecorder(Rig);

	Rig.DataComp->CharacterProfile = Asset;
	Rig.DataComp->GetRepProfileSeqForTests() = 1;
	Rig.DataComp->OnRep_CharacterProfile(nullptr);

	FPaper2DPlusRepAnimState& Rep = Rig.DataComp->GetRepAnimStateForTests();
	Rep.Sequence = 4;
	Rep.ProfileSeq = 1;
	Rep.bIsProfileMove = true;
	Rep.MoveName = FName(TEXT("BMove"));
	Rep.StartServerTime = NetAnim_BaseServerTime - 0.3;
	Rep.PlayRate = 1.0f;
	Rig.DataComp->OnRep_AnimState(FPaper2DPlusRepAnimState());

	TestEqual(TEXT("clockless OnRep stashes (no broadcast)"), Recorder->BroadcastCount, 0);
	TestTrue(TEXT("pending stash armed"), Rig.DataComp->HasPendingRepAnimStateForTests());

	// The clock appears (GameState arrives in a real world) — the BeginPlay-tail retry drains it.
	Rig.DataComp->SetServerTimeOverrideForTests(NetAnim_BaseServerTime);
	Rig.DataComp->FlushPendingRepAnimStateForTests();

	TestEqual(TEXT("stash drained at the BeginPlay-tail retry"), Recorder->BroadcastCount, 1);
	TestTrue(TEXT("anchor-derived position after the drain"),
		FMath::IsNearlyEqual(Recorder->LastPlaybackPosition, 0.3f, 1.e-3f));
	TestEqual(TEXT("sequence committed"), (int32)Rig.DataComp->GetLastAppliedSequenceForTests(), 4);
	TestFalse(TEXT("stash consumed"), Rig.DataComp->HasPendingRepAnimStateForTests());

	// A stale stash is discarded by a LATER applied OnRep (latest wins).
	Rig.DataComp->SetServerTimeOverrideForTests(TOptional<double>());
	Rep.Sequence = 5;
	Rig.DataComp->OnRep_AnimState(FPaper2DPlusRepAnimState());
	TestTrue(TEXT("clockless seq 5 stashes"), Rig.DataComp->HasPendingRepAnimStateForTests());

	Rig.DataComp->SetServerTimeOverrideForTests(NetAnim_BaseServerTime);
	Rep.Sequence = 6;
	Rig.DataComp->OnRep_AnimState(FPaper2DPlusRepAnimState());
	TestEqual(TEXT("newer OnRep applied"), (int32)Rig.DataComp->GetLastAppliedSequenceForTests(), 6);
	TestFalse(TEXT("stale stash discarded by the newer apply"), Rig.DataComp->HasPendingRepAnimStateForTests());
	TestEqual(TEXT("exactly one more broadcast"), Recorder->BroadcastCount, 2);

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Resolve failure WITH a matching ProfileSeq (asset version skew) => warn +
// advisory CLEAR, NEVER a wedged stash (no future OnRep would drain it — the
// security un-wedge pin). The sequence commits so the channel stays live.
// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusNetAnimStateResolveFailureClears,
	"Paper2DPlus.Network.AnimState.ResolveFailureClearsNeverWedges",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusNetAnimStateResolveFailureClears::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	NetAnim_AddMove(Asset, TEXT("BMove"), 4);

	FNetAnim_Rig Rig = NetAnim_MakeRig();
	Rig.DataComp->SetNetContextOverrideForTests(EPaper2DPlusNetContext::SimulatedProxy);
	Rig.DataComp->SetServerTimeOverrideForTests(NetAnim_BaseServerTime);
	UPaper2DPlusNetAnimStateRecorder* Recorder = NetAnim_BindRecorder(Rig);

	Rig.DataComp->CharacterProfile = Asset;
	Rig.DataComp->GetRepProfileSeqForTests() = 1;
	Rig.DataComp->OnRep_CharacterProfile(nullptr);

	AddExpectedError(TEXT("does not resolve on profile"), EAutomationExpectedErrorFlags::Contains);

	FPaper2DPlusRepAnimState& Rep = Rig.DataComp->GetRepAnimStateForTests();
	Rep.Sequence = 9;
	Rep.ProfileSeq = 1; // MATCHING generation — this is skew, not a pairing gap.
	Rep.bIsProfileMove = true;
	Rep.MoveName = FName(TEXT("Ghost"));
	Rig.DataComp->OnRep_AnimState(FPaper2DPlusRepAnimState());

	TestEqual(TEXT("advisory CLEAR broadcast"), Recorder->BroadcastCount, 1);
	TestTrue(TEXT("clear carries null flipbook"), Recorder->bLastFlipbookWasNull);
	TestTrue(TEXT("clear carries MoveName None"), Recorder->LastMoveName.IsNone());
	TestFalse(TEXT("NEVER a wedged stash"), Rig.DataComp->HasPendingRepAnimStateForTests());
	TestEqual(TEXT("sequence committed so the channel stays live"),
		(int32)Rig.DataComp->GetLastAppliedSequenceForTests(), 9);

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// bIsProfileMove == false => authoritative CLEAR: commit, then broadcast the
// null-move advisory (the game returns to its own locomotion).
// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusNetAnimStateNonProfileClear,
	"Paper2DPlus.Network.AnimState.NonProfileMoveBroadcastsAuthoritativeClear",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusNetAnimStateNonProfileClear::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	NetAnim_AddMove(Asset, TEXT("BMove"), 4);

	FNetAnim_Rig Rig = NetAnim_MakeRig();
	Rig.DataComp->SetNetContextOverrideForTests(EPaper2DPlusNetContext::SimulatedProxy);
	Rig.DataComp->SetServerTimeOverrideForTests(NetAnim_BaseServerTime);
	UPaper2DPlusNetAnimStateRecorder* Recorder = NetAnim_BindRecorder(Rig);

	Rig.DataComp->CharacterProfile = Asset;
	Rig.DataComp->GetRepProfileSeqForTests() = 1;
	Rig.DataComp->OnRep_CharacterProfile(nullptr);

	FPaper2DPlusRepAnimState& Rep = Rig.DataComp->GetRepAnimStateForTests();
	Rep.Sequence = 4;
	Rep.ProfileSeq = 1;
	Rep.bIsProfileMove = false; // server playing locomotion
	Rep.MoveName = NAME_None;
	Rig.DataComp->OnRep_AnimState(FPaper2DPlusRepAnimState());

	TestEqual(TEXT("clear broadcast fired"), Recorder->BroadcastCount, 1);
	TestTrue(TEXT("clear carries null flipbook"), Recorder->bLastFlipbookWasNull);
	TestTrue(TEXT("clear carries MoveName None"), Recorder->LastMoveName.IsNone());
	TestTrue(TEXT("clear position is 0"), FMath::IsNearlyEqual(Recorder->LastPlaybackPosition, 0.0f, 1.e-6f));
	TestFalse(TEXT("clear never reports finished"), Recorder->bLastAlreadyFinished);
	TestEqual(TEXT("clear commits the sequence"), (int32)Rig.DataComp->GetLastAppliedSequenceForTests(), 4);

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Null-profile stash split (the review MAJOR pin): an authoritative CLEAR
// (bIsProfileMove=false) needs no profile and must PASS THROUGH to the clear
// path even while CharacterProfile is null — a server running
// SetCharacterProfile(nullptr) mid-flipbook publishes a CLEAR that previously
// stashed forever. A profile-MOVE state with a null profile still stashes
// (unmapped-GUID join safety — drained when the profile OnRep maps).
// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusNetAnimStateNullProfileClearSplit,
	"Paper2DPlus.Network.AnimState.NullProfileClearAppliesButProfileMoveStashes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusNetAnimStateNullProfileClearSplit::RunTest(const FString& Parameters)
{
	FNetAnim_Rig Rig = NetAnim_MakeRig();
	Rig.DataComp->SetNetContextOverrideForTests(EPaper2DPlusNetContext::SimulatedProxy);
	Rig.DataComp->SetServerTimeOverrideForTests(NetAnim_BaseServerTime);
	UPaper2DPlusNetAnimStateRecorder* Recorder = NetAnim_BindRecorder(Rig);

	// Profile null (never arrived / cleared), generation 0 on both sides.
	FPaper2DPlusRepAnimState& Rep = Rig.DataComp->GetRepAnimStateForTests();
	Rep.Sequence = 4;
	Rep.ProfileSeq = 0;
	Rep.bIsProfileMove = false; // authoritative CLEAR
	Rep.MoveName = NAME_None;
	Rig.DataComp->OnRep_AnimState(FPaper2DPlusRepAnimState());

	TestEqual(TEXT("null-profile CLEAR applies (no wedge)"), Recorder->BroadcastCount, 1);
	TestTrue(TEXT("clear carries null flipbook"), Recorder->bLastFlipbookWasNull);
	TestFalse(TEXT("no stash for the CLEAR"), Rig.DataComp->HasPendingRepAnimStateForTests());
	TestEqual(TEXT("clear committed the sequence"), (int32)Rig.DataComp->GetLastAppliedSequenceForTests(), 4);

	// A profile-MOVE state with the profile still null stashes (the other half of the split).
	Rep.Sequence = 5;
	Rep.bIsProfileMove = true;
	Rep.MoveName = FName(TEXT("Ghost"));
	Rig.DataComp->OnRep_AnimState(FPaper2DPlusRepAnimState());

	TestEqual(TEXT("profile-move state with null profile stashes (no broadcast)"), Recorder->BroadcastCount, 1);
	TestTrue(TEXT("stash armed for the unmapped profile"), Rig.DataComp->HasPendingRepAnimStateForTests());

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Late-join advisory pin (covers AC2, advisory half): a fresh component
// (LastApplied=0) receiving Sequence=7 mid-move broadcasts the advisory with
// the anchor-derived position and replays NO one-shots; a state whose anchor
// says the non-looping move already ended reports bAlreadyFinished.
// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusNetAnimStateLateJoinAdvisory,
	"Paper2DPlus.Network.AnimState.LateJoinAdvisoryPin",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusNetAnimStateLateJoinAdvisory::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	UPaperFlipbook* FB = NetAnim_AddMove(Asset, TEXT("BMove"), 4); // 0.4s

	// A mid-move one-shot that must NOT replay on the late joiner.
	UPaper2DPlusTestMomentCue* MidMoveMoment = NewObject<UPaper2DPlusTestMomentCue>(Asset);
	MidMoveMoment->TriggerFrame = 1;
	Asset->Flipbooks[0].FrameEventData.FrameCues.Add(MidMoveMoment);

	FNetAnim_Rig Rig = NetAnim_MakeRig();
	Rig.DataComp->SetNetContextOverrideForTests(EPaper2DPlusNetContext::SimulatedProxy);
	Rig.DataComp->SetServerTimeOverrideForTests(NetAnim_BaseServerTime);
	UPaper2DPlusNetAnimStateRecorder* Recorder = NetAnim_BindRecorder(Rig);
	UPaper2DPlusFrameCueRecorder* CueRecorder = NewObject<UPaper2DPlusFrameCueRecorder>();
	Rig.DataComp->OnFrameCue.AddDynamic(CueRecorder, &UPaper2DPlusFrameCueRecorder::OnCue);

	Rig.DataComp->CharacterProfile = Asset;
	Rig.DataComp->GetRepProfileSeqForTests() = 1;
	Rig.DataComp->OnRep_CharacterProfile(nullptr);

	// Mid-move late join: 0.15s into a 0.4s move.
	FPaper2DPlusRepAnimState& Rep = Rig.DataComp->GetRepAnimStateForTests();
	Rep.Sequence = 7;
	Rep.ProfileSeq = 1;
	Rep.bIsProfileMove = true;
	Rep.MoveName = FName(TEXT("BMove"));
	Rep.StartServerTime = NetAnim_BaseServerTime - 0.15;
	Rep.PlayRate = 1.0f;
	Rig.DataComp->OnRep_AnimState(FPaper2DPlusRepAnimState());

	TestEqual(TEXT("late join broadcasts the advisory"), Recorder->BroadcastCount, 1);
	TestTrue(TEXT("advisory resolved the move"), Recorder->LastMoveName == FName(TEXT("BMove")));
	TestTrue(TEXT("advisory carries the resolved flipbook"), Recorder->LastFlipbook.Get() == FB);
	TestTrue(TEXT("anchor-derived mid-move position"),
		FMath::IsNearlyEqual(Recorder->LastPlaybackPosition, 0.15f, 1.e-3f));
	TestFalse(TEXT("mid-move not finished"), Recorder->bLastAlreadyFinished);
	TestFalse(TEXT("no Moment replay on the late joiner"), CueRecorder->Cues.Contains(MidMoveMoment));
	TestEqual(TEXT("sequence committed"), (int32)Rig.DataComp->GetLastAppliedSequenceForTests(), 7);

	// Already-finished leg: the anchor says the non-looping move ran out long ago.
	Rep.Sequence = 8;
	Rep.StartServerTime = NetAnim_BaseServerTime - 10.0;
	Rig.DataComp->OnRep_AnimState(FPaper2DPlusRepAnimState());
	TestEqual(TEXT("finished-state advisory fired"), Recorder->BroadcastCount, 2);
	TestTrue(TEXT("finished state clamps to Length"),
		FMath::IsNearlyEqual(Recorder->LastPlaybackPosition, 0.4f, 1.e-3f));
	TestTrue(TEXT("finished state reports bAlreadyFinished"), Recorder->bLastAlreadyFinished);

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Bind-then-pull (the review MAJOR-b pin): RebroadcastReplicatedAnimState
// re-broadcasts the advisory for the last APPLIED state through the same single
// broadcast site — a late binder (BP Event BeginPlay binds after the initial
// bunch applied) binds first, then pulls. No-op before anything has applied.
// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusNetAnimStateRebroadcastPull,
	"Paper2DPlus.Network.AnimState.RebroadcastReplicatedAnimStatePullsLastApplied",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusNetAnimStateRebroadcastPull::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	UPaperFlipbook* FB = NetAnim_AddMove(Asset, TEXT("BMove"), 4); // 0.4s

	FNetAnim_Rig Rig = NetAnim_MakeRig();
	Rig.DataComp->SetNetContextOverrideForTests(EPaper2DPlusNetContext::SimulatedProxy);
	Rig.DataComp->SetServerTimeOverrideForTests(NetAnim_BaseServerTime);
	UPaper2DPlusNetAnimStateRecorder* Recorder = NetAnim_BindRecorder(Rig);

	// No-op while nothing has applied (LastAppliedSequence == 0).
	Rig.DataComp->RebroadcastReplicatedAnimState();
	TestEqual(TEXT("no-op before any applied state"), Recorder->BroadcastCount, 0);

	// Apply a state mid-move (0.15s into 0.4s).
	Rig.DataComp->CharacterProfile = Asset;
	Rig.DataComp->GetRepProfileSeqForTests() = 1;
	Rig.DataComp->OnRep_CharacterProfile(nullptr);

	FPaper2DPlusRepAnimState& Rep = Rig.DataComp->GetRepAnimStateForTests();
	Rep.Sequence = 7;
	Rep.ProfileSeq = 1;
	Rep.bIsProfileMove = true;
	Rep.MoveName = FName(TEXT("BMove"));
	Rep.StartServerTime = NetAnim_BaseServerTime - 0.15;
	Rep.PlayRate = 1.0f;
	Rig.DataComp->OnRep_AnimState(FPaper2DPlusRepAnimState());
	TestEqual(TEXT("apply broadcast the advisory"), Recorder->BroadcastCount, 1);

	// "Clear the recorder" — the late binder's fresh view.
	Recorder->BroadcastCount = 0;
	Recorder->LastMoveName = NAME_None;
	Recorder->LastFlipbook = nullptr;
	Recorder->bLastFlipbookWasNull = true;
	Recorder->LastPlaybackPosition = -1.0f;
	Recorder->bLastAlreadyFinished = true;

	// The pull: a second, identical advisory for the same applied state.
	Rig.DataComp->RebroadcastReplicatedAnimState();
	TestEqual(TEXT("pull re-broadcast exactly one advisory"), Recorder->BroadcastCount, 1);
	TestTrue(TEXT("re-broadcast resolved the same move"), Recorder->LastMoveName == FName(TEXT("BMove")));
	TestTrue(TEXT("re-broadcast carries the same flipbook"), Recorder->LastFlipbook.Get() == FB);
	TestTrue(TEXT("re-broadcast re-derives the anchor position (clock unchanged => identical)"),
		FMath::IsNearlyEqual(Recorder->LastPlaybackPosition, 0.15f, 1.e-3f));
	TestFalse(TEXT("re-broadcast not finished"), Recorder->bLastAlreadyFinished);
	TestEqual(TEXT("pull committed nothing new"), (int32)Rig.DataComp->GetLastAppliedSequenceForTests(), 7);

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// SetCharacterProfile proxy gate (KTD-22): networked non-authority contexts are
// warn-once rejected with no mutation; the OnRep funnel (bApplyingReplicatedState)
// bypasses and applies.
// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusNetAnimStateSetProfileProxyGate,
	"Paper2DPlus.Network.AnimState.SetCharacterProfileProxyGate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusNetAnimStateSetProfileProxyGate::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	NetAnim_AddMove(Asset, TEXT("BMove"), 4);

	FNetAnim_Rig Rig = NetAnim_MakeRig();
	Rig.DataComp->SetNetContextOverrideForTests(EPaper2DPlusNetContext::SimulatedProxy);

	AddExpectedError(TEXT("SetCharacterProfile called on a non-authority networked context"),
		EAutomationExpectedErrorFlags::Contains);

	Rig.DataComp->SetCharacterProfile(Asset);
	TestNull(TEXT("simulated proxy call rejected (no mutation)"), Rig.DataComp->CharacterProfile.Get());

	// Warn-once: a second rejected call (autonomous proxy this time) stays silent — same latch.
	Rig.DataComp->SetNetContextOverrideForTests(EPaper2DPlusNetContext::AutonomousProxy);
	Rig.DataComp->SetCharacterProfile(Asset);
	TestNull(TEXT("autonomous proxy call rejected (no mutation)"), Rig.DataComp->CharacterProfile.Get());

	// The OnRep funnel applies: pre-write + RepNotify (bApplyingReplicatedState bypass).
	Rig.DataComp->SetNetContextOverrideForTests(EPaper2DPlusNetContext::SimulatedProxy);
	Rig.DataComp->CharacterProfile = Asset;
	Rig.DataComp->GetRepProfileSeqForTests() = 1;
	Rig.DataComp->OnRep_CharacterProfile(nullptr);
	TestTrue(TEXT("OnRep funnel applies the profile"), Rig.DataComp->CharacterProfile.Get() == Asset);
	TestEqual(TEXT("OnRep funnel adopted the generation"),
		(int32)Rig.DataComp->GetProfileChangeCounterForTests(), 1);

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Archetype-mismatch diagnostic: replicated data arriving while the LOCAL
// bEnableReplication is false warns ONCE (shared latch across the OnReps).
// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusNetAnimStateArchetypeMismatchWarn,
	"Paper2DPlus.Network.AnimState.ArchetypeMismatchWarnsOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusNetAnimStateArchetypeMismatchWarn::RunTest(const FString& Parameters)
{
	FNetAnim_Rig Rig = NetAnim_MakeRig(/*bEnableReplication=*/false);

	AddExpectedError(TEXT("while bEnableReplication is false locally"), EAutomationExpectedErrorFlags::Contains);

	FPaper2DPlusRepAnimState& Rep = Rig.DataComp->GetRepAnimStateForTests();
	Rep.Sequence = 3;
	Rep.bIsProfileMove = true;
	Rep.MoveName = FName(TEXT("Ghost"));
	Rig.DataComp->OnRep_AnimState(FPaper2DPlusRepAnimState());

	// Second replicated arrival (different OnRep) shares the latch — no second warning.
	Rig.DataComp->OnRep_HitStop();

	// With no profile the state stashed (the data path still behaves; the warn is diagnostic only).
	TestTrue(TEXT("state stashed despite the mismatch (profile null)"),
		Rig.DataComp->HasPendingRepAnimStateForTests());

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Single-player invariance suite (C1): with everything compiled in and
// bEnableReplication=false (no overrides — the true offline path), the scripted
// single-player flows behave exactly as shipped — frame-0 one-shots fire,
// the generation never bumps, and NOTHING ever publishes.
// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusNetAnimStateSinglePlayerInvariance,
	"Paper2DPlus.Network.AnimState.SinglePlayerInvariance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusNetAnimStateSinglePlayerInvariance::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	UPaperFlipbook* FB_A = NetAnim_AddMove(Asset, TEXT("Jab"), 4);

	UPaper2DPlusTestMomentCue* Frame0Moment = NewObject<UPaper2DPlusTestMomentCue>(Asset);
	Frame0Moment->TriggerFrame = 0;
	Asset->Flipbooks[0].FrameEventData.FrameCues.Add(Frame0Moment);

	FNetAnim_Rig Rig = NetAnim_MakeRig(/*bEnableReplication=*/false); // no overrides: offline path
	UPaper2DPlusFrameCueRecorder* CueRecorder = NewObject<UPaper2DPlusFrameCueRecorder>();
	Rig.DataComp->OnFrameCue.AddDynamic(CueRecorder, &UPaper2DPlusFrameCueRecorder::OnCue);

	TestEqual(TEXT("offline rig resolves Standalone"),
		(int32)Rig.DataComp->GetNetContext(), (int32)EPaper2DPlusNetContext::Standalone);

	// Profile set works on every machine in single player AND fires frame-0 one-shots (no
	// replicated-apply suppression outside the OnRep funnel).
	Rig.FBComp->SetFlipbook(FB_A);
	Rig.DataComp->SetCharacterProfile(Asset);
	TestTrue(TEXT("single-player SetCharacterProfile applies"), Rig.DataComp->CharacterProfile.Get() == Asset);
	TestTrue(TEXT("frame-0 Moment broadcasts in single player"), CueRecorder->Cues.Contains(Frame0Moment));
	TestEqual(TEXT("frame-0 Moment broadcasts exactly once"), CueRecorder->Cues.Num(), 1);
	TestEqual(TEXT("no generation bump in Standalone"), (int32)Rig.DataComp->GetRepProfileSeqForTests(), 0);

	TestEqual(TEXT("nothing ever publishes offline"),
		(int32)Rig.DataComp->GetRepAnimStateForTests().Sequence, 0);

	TestEqual(TEXT("still Standalone after the scripted sequence"),
		(int32)Rig.DataComp->GetNetContext(), (int32)EPaper2DPlusNetContext::Standalone);

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Codex F195a — a SAME-Sequence republish that RE-ANCHORS the move (changed
// StartServerTime, e.g. RepublishAnimState after SetPlaybackPosition or the U5
// hit-stop clock re-anchor) must RE-BROADCAST the advisory so advise-only consumers
// (the default gate+advise mode) learn the new position; an outcome-only same-seq
// update (anchor unchanged) must NOT re-broadcast (the fast-path is preserved).
// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusNetAnimStateSameSeqReAnchorReBroadcasts,
	"Paper2DPlus.Network.AnimState.SameSeqReAnchorReBroadcasts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusNetAnimStateSameSeqReAnchorReBroadcasts::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	NetAnim_AddMove(Asset, TEXT("Jab"), 8); // 8 key frames @ 10fps => 0.8s total

	FNetAnim_Rig Rig = NetAnim_MakeRig();
	Rig.DataComp->CharacterProfile = Asset;                         // local profile resolves MoveName (advise mode)
	Rig.DataComp->SetNetContextOverrideForTests(EPaper2DPlusNetContext::SimulatedProxy); // advise-only (NOT apply mode)
	Rig.DataComp->SetServerTimeOverrideForTests(NetAnim_BaseServerTime);                 // now = 1000.0
	UPaper2DPlusNetAnimStateRecorder* Rec = NetAnim_BindRecorder(Rig);

	// New-seq move anchored at 999.7 => position 0.3 at now 1000.0.
	FPaper2DPlusRepAnimState& Wire = Rig.DataComp->GetRepAnimStateForTests();
	Wire.Sequence = 1; Wire.ProfileSeq = 0; Wire.bIsProfileMove = true; Wire.MoveName = FName(TEXT("Jab"));
	Wire.StartServerTime = NetAnim_BaseServerTime - 0.3; Wire.PlayRate = 1.0f;
	Rig.DataComp->OnRep_AnimState(FPaper2DPlusRepAnimState{});
	TestEqual(TEXT("new-seq move broadcast the advisory once"), Rec->BroadcastCount, 1);
	const float Pos1 = Rec->LastPlaybackPosition;
	TestTrue(TEXT("new-seq position ~0.3"), FMath::IsNearlyEqual(Pos1, 0.3f, 0.02f));

	// SAME-seq RE-ANCHOR: the authority re-anchored mid-move (StartServerTime jumps to 999.5 => position 0.5),
	// keeping the Sequence. The advisory MUST re-fire with the new position (F195a; old code only merged outcome).
	Wire.StartServerTime = NetAnim_BaseServerTime - 0.5;
	Rig.DataComp->OnRep_AnimState(FPaper2DPlusRepAnimState{});
	TestEqual(TEXT("same-seq re-anchor RE-broadcast the advisory"), Rec->BroadcastCount, 2);
	TestTrue(TEXT("re-anchored advisory carries the new position ~0.5"),
		FMath::IsNearlyEqual(Rec->LastPlaybackPosition, 0.5f, 0.02f));

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Codex F194a — a no-server-clock anim-state stash must RE-ARM a retry (the same
// self-chaining one-shot core ticker hit-stop uses), or it stays stuck until some
// unrelated replication happens. Drive the REAL core ticker to prove a ticker was
// registered (a manual flush would mask the missing-ticker bug).
// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusNetAnimStateNoClockStashReArms,
	"Paper2DPlus.Network.AnimState.NoClockStashReArms",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusNetAnimStateNoClockStashReArms::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	NetAnim_AddMove(Asset, TEXT("Jab"), 8);

	FNetAnim_Rig Rig = NetAnim_MakeRig();
	Rig.DataComp->CharacterProfile = Asset;
	Rig.DataComp->SetNetContextOverrideForTests(EPaper2DPlusNetContext::SimulatedProxy);
	// NO SetServerTimeOverrideForTests => GetServerTimeSecondsForNet is unset (the missing-clock case).
	UPaper2DPlusNetAnimStateRecorder* Rec = NetAnim_BindRecorder(Rig);

	FPaper2DPlusRepAnimState& Wire = Rig.DataComp->GetRepAnimStateForTests();
	Wire.Sequence = 1; Wire.ProfileSeq = 0; Wire.bIsProfileMove = true; Wire.MoveName = FName(TEXT("Jab"));
	Wire.StartServerTime = NetAnim_BaseServerTime - 0.3; Wire.PlayRate = 1.0f;
	Rig.DataComp->OnRep_AnimState(FPaper2DPlusRepAnimState{});

	TestTrue(TEXT("no-clock anim state stashed"), Rig.DataComp->HasPendingRepAnimStateForTests());
	TestEqual(TEXT("nothing applied while the clock is missing"), Rec->BroadcastCount, 0);

	// The clock arrives; the re-armed one-shot core ticker drains the stash on the next tick (F194a).
	// Without the fix no ticker is registered, so ticking the core ticker would NOT drain it.
	Rig.DataComp->SetServerTimeOverrideForTests(NetAnim_BaseServerTime);
	FTSTicker::GetCoreTicker().Tick(0.01f);

	TestFalse(TEXT("the re-armed ticker drained the stash once the clock arrived"),
		Rig.DataComp->HasPendingRepAnimStateForTests());
	TestEqual(TEXT("the deferred anim state applied (advisory fired once)"), Rec->BroadcastCount, 1);

	return true;
}

#endif // !UE_BUILD_SHIPPING

#endif // WITH_EDITOR
