// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#if WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "Paper2DPlusLayerRenderComponent.h"
#include "Paper2DPlusCharacterLayerAsset.h"
#include "Paper2DPlusNetTypes.h"
#include "PaperSpriteComponent.h"

/**
 * SEAT D â€” NET APPEARANCE LIFECYCLE on UPaper2DPlusLayerRenderComponent (the GENERIC appearance surface).
 * Ports the three OnRep lifecycle behaviours the deleted Paper2DPlusNetWardrobeTest.cpp used to cover, onto the
 * CURRENT descriptor surface (FPaper2DPlusRepAppearanceState / OnRep_AppearanceState / ApplyAppearancePreset /
 * PreviewAppearancePreset / IsLayerActive / GetAppearanceDescriptor) â€” the retired wardrobe surface
 * (SetSkin/SetOutfit/OnRep_WardrobeState/IsLayerVisible/FPaper2DPlusRepWardrobeState) no longer exists.
 *
 * Worldless. The rigs drive the !UE_BUILD_SHIPPING net seams the header documents: pre-write RepAppearance via
 * GetRepAppearanceForTests() (net-driver simulation) then drive OnRep_AppearanceState() (the same
 * 'pre-write then drive OnRep' pattern the 200-char harness uses), force the net context with
 * SetNetContextOverrideForTests, register live child components via Test_RegisterLayerComponent (which flips
 * bLayerComponentsReady like CreateLayerComponents), and observe applies through Test_GetLayersChangedBroadcastCount().
 *
 * Helpers are NetAppLife_-prefixed and live in an ANONYMOUS namespace per the unity-build file-unique-name rule
 * (no file-scope `using namespace`).
 */

namespace
{
	// One RuntimeCustomizable asset: an independent Body layer + a Head Exclusive Group {Hat|Hood}. Two presets:
	// Default = {Body, Hat}, Armor = {Body, Hood}. Auto-generated LayerId/GroupId/PresetId (the harness idiom).
	struct FNetAppLife_Asset
	{
		UPaper2DPlusCharacterLayerAsset* Asset = nullptr;
		FGuid BodyLayerId;
		FGuid HatLayerId;
		FGuid HoodLayerId;
		FGuid DefaultPresetId; // {Body, Hat}
		FGuid ArmorPresetId;   // {Body, Hood}
	};

	FNetAppLife_Asset NetAppLife_MakeAsset()
	{
		FNetAppLife_Asset Out;
		Out.Asset = NewObject<UPaper2DPlusCharacterLayerAsset>();
		Out.Asset->UsageMode = ECharacterLayerUsageMode::RuntimeCustomizable;

		FCharacterLayerExclusiveGroup& Head = Out.Asset->ExclusiveGroups.AddDefaulted_GetRef();
		Head.DisplayName = TEXT("Head");

		FCharacterLayer& Body = Out.Asset->Layers.AddDefaulted_GetRef();
		Body.LayerName = TEXT("body");
		Out.BodyLayerId = Body.LayerId;
		FCharacterLayer& Hat = Out.Asset->Layers.AddDefaulted_GetRef();
		Hat.LayerName = TEXT("hat");
		Hat.ExclusiveGroupId = Head.GroupId;
		Out.HatLayerId = Hat.LayerId;
		FCharacterLayer& Hood = Out.Asset->Layers.AddDefaulted_GetRef();
		Hood.LayerName = TEXT("hood");
		Hood.ExclusiveGroupId = Head.GroupId;
		Out.HoodLayerId = Hood.LayerId;

		FCharacterLayerAppearancePreset& Default = Out.Asset->AppearancePresets.AddDefaulted_GetRef();
		Default.DisplayName = TEXT("Default");
		Default.ActiveLayerIds = { Out.BodyLayerId, Out.HatLayerId };
		Out.DefaultPresetId = Default.PresetId;
		Out.Asset->DefaultAppearancePresetId = Default.PresetId;

		FCharacterLayerAppearancePreset& Armor = Out.Asset->AppearancePresets.AddDefaulted_GetRef();
		Armor.DisplayName = TEXT("Armor");
		Armor.ActiveLayerIds = { Out.BodyLayerId, Out.HoodLayerId };
		Out.ArmorPresetId = Armor.PresetId;

		return Out;
	}

	struct FNetAppLife_Rig
	{
		FNetAppLife_Asset Data;
		UPaper2DPlusLayerRenderComponent* Comp = nullptr;
	};

	FNetAppLife_Rig NetAppLife_MakeRig(
		EPaper2DPlusNetContext Ctx,
		bool bEnableRep = true,
		bool bRegisterComponents = true)
	{
		FNetAppLife_Rig Rig;
		Rig.Data = NetAppLife_MakeAsset();
		Rig.Comp = NewObject<UPaper2DPlusLayerRenderComponent>();
		Rig.Comp->CharacterLayerAsset = Rig.Data.Asset;
		Rig.Comp->bEnableReplication = bEnableRep;
		Rig.Comp->SetNetContextOverrideForTests(Ctx);
		if (bRegisterComponents)
		{
			// Register a live child per layer as if CreateLayerComponents ran â€” this also flips
			// bLayerComponentsReady=true so OnRep applies immediately instead of stashing.
			for (const FCharacterLayer& Layer : Rig.Data.Asset->Layers)
			{
				UPaperSpriteComponent* SC = NewObject<UPaperSpriteComponent>();
				Rig.Comp->Test_RegisterLayerComponent(Layer.LayerName, SC, /*bVisible=*/false);
			}
		}
		return Rig;
	}

	/** Build a supported (PayloadVersion 1) rep snapshot carrying a compatible committed descriptor for PresetId. */
	FPaper2DPlusRepAppearanceState NetAppLife_PresetSnapshot(const FNetAppLife_Asset& Data, const FGuid& PresetId, uint16 Sequence)
	{
		FPaper2DPlusRepAppearanceState Snap;
		Snap.Sequence = Sequence;
		Snap.PayloadVersion = 1;
		Snap.Appearance.DeliveryMode = ECharacterLayerUsageMode::RuntimeCustomizable;
		if (const FCharacterLayerAppearancePreset* Preset = Data.Asset->GetAppearancePresetById(PresetId))
		{
			Snap.Appearance.ActiveLayerIds = Preset->ActiveLayerIds;
		}
		return Snap;
	}
} // anonymous namespace

// Everything below drives the !UE_BUILD_SHIPPING net seams.
#if !UE_BUILD_SHIPPING

// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
// (a) PREVIEW DISCARD, NO INTERMEDIATE PASS: a replicated committed snapshot landing while a client-local
// preview is active discards the preview WITHOUT a restore recompute â€” exactly ONE apply/broadcast, not two
// (CancelPreviewNoApply adds no pass because the same OnRep apply sets committed visibility itself).
// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusNetAppearanceOnRepPreviewDiscardedNoIntermediatePass,
	"Paper2DPlus.Network.Appearance.OnRepPreviewDiscardedNoIntermediatePass",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusNetAppearanceOnRepPreviewDiscardedNoIntermediatePass::RunTest(const FString& Parameters)
{
	FNetAppLife_Rig Rig = NetAppLife_MakeRig(EPaper2DPlusNetContext::SimulatedProxy);

	// A local try-before-equip preview is active (the client is browsing the Armor preset â€” Hood).
	TestTrue(TEXT("preview started (Armor preset previewed)"),
		Rig.Comp->PreviewAppearancePreset(Rig.Data.ArmorPresetId));
	TestTrue(TEXT("a preview is active"), Rig.Comp->IsPreviewActive());

	// Baseline the broadcast counter AFTER the preview's own recompute so we measure only the OnRep apply.
	const uint32 BroadcastsBefore = Rig.Comp->Test_GetLayersChangedBroadcastCount();

	// A server appearance update lands: the Default preset (Body + Hat) at a fresh sequence.
	Rig.Comp->GetRepAppearanceForTests() =
		NetAppLife_PresetSnapshot(Rig.Data, Rig.Data.DefaultPresetId, /*Sequence=*/7);
	Rig.Comp->OnRep_AppearanceState();

	// The preview discard added NO restore recompute â€” only the apply's single RecomputeVisibility broadcast.
	TestEqual(TEXT("preview discard added no intermediate pass (exactly one apply broadcast)"),
		(int32)(Rig.Comp->Test_GetLayersChangedBroadcastCount() - BroadcastsBefore), 1);
	TestFalse(TEXT("preview is gone after the replicated apply"), Rig.Comp->IsPreviewActive());

	// The replicated committed descriptor wins, NOT the previewed Armor selection.
	TestEqual(TEXT("committed descriptor is the replicated Default preset"),
		Rig.Comp->GetAppearanceDescriptor().ActiveLayerIds,
		TArray<FGuid>({ Rig.Data.BodyLayerId, Rig.Data.HatLayerId }));
	TestTrue(TEXT("replicated Hat is active"), Rig.Comp->IsLayerActive(Rig.Data.HatLayerId));
	TestFalse(TEXT("previewed Hood is gone"), Rig.Comp->IsLayerActive(Rig.Data.HoodLayerId));
	return true;
}

// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
// (b) SNAPSHOT COMPARE, NO BUMP: a no-op republish / CommitPreview to the identical committed state must NOT
// bump Sequence (AppearanceSnapshotEqualsIgnoringSeq guard, LayerRenderComponent.cpp ~:2154) â€” else a no-op
// republish would stomp a client-local preview.
// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusNetAppearanceSnapshotCompareNoBump,
	"Paper2DPlus.Network.Appearance.SnapshotCompareNoBump",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusNetAppearanceSnapshotCompareNoBump::RunTest(const FString& Parameters)
{
	FNetAppLife_Rig Rig = NetAppLife_MakeRig(EPaper2DPlusNetContext::Authority);
	const FPaper2DPlusRepAppearanceState& Wire = Rig.Comp->GetRepAppearanceForTests();

	TestEqual(TEXT("never-published Sequence starts 0"), (int32)Wire.Sequence, 0);

	// Commit the Armor preset â€” the first real delta publishes and bumps Sequence to 1.
	TestTrue(TEXT("ApplyAppearancePreset(Armor) succeeds"),
		Rig.Comp->ApplyAppearancePreset(Rig.Data.ArmorPresetId));
	const uint16 Baseline = Wire.Sequence;
	TestEqual(TEXT("Armor commit bumped Sequence to 1"), (int32)Baseline, 1);

	// Preview the SAME committed selection, then CommitPreview => identical => no delta => NO bump.
	TestTrue(TEXT("preview the already-committed Armor preset"),
		Rig.Comp->PreviewAppearancePreset(Rig.Data.ArmorPresetId));
	Rig.Comp->CommitPreview();
	TestEqual(TEXT("CommitPreview to identical committed state does NOT bump Sequence"),
		(int32)Wire.Sequence, (int32)Baseline);

	// A DIFFERENT preview then a cancel restores the unchanged committed state => still NO bump.
	TestTrue(TEXT("preview a different (Default) preset"),
		Rig.Comp->PreviewAppearancePreset(Rig.Data.DefaultPresetId));
	Rig.Comp->CancelPreview();
	TestEqual(TEXT("CancelPreview does NOT bump Sequence"), (int32)Wire.Sequence, (int32)Baseline);

	// Re-committing the SAME Armor preset is a semantic no-op through the publish chokepoint => still NO bump.
	TestTrue(TEXT("re-apply the same committed Armor preset"),
		Rig.Comp->ApplyAppearancePreset(Rig.Data.ArmorPresetId));
	TestEqual(TEXT("a no-op re-commit does NOT bump Sequence"), (int32)Wire.Sequence, (int32)Baseline);
	return true;
}

// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
// (c) PRE-BEGINPLAY STASH-THEN-DRAIN: a snapshot arriving before the layer components are ready STASHES
// (PendingRepAppearance) and drains at the BeginPlay tail exactly ONCE; a same-sequence re-drive after the
// drain is an idempotent no-op (re-apply safe).
// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusNetAppearanceOnRepStashBeforeComponentsReady,
	"Paper2DPlus.Network.Appearance.OnRepStashBeforeComponentsReady",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusNetAppearanceOnRepStashBeforeComponentsReady::RunTest(const FString& Parameters)
{
	// No components registered => bLayerComponentsReady stays false (pre-BeginPlay).
	FNetAppLife_Rig Rig =
		NetAppLife_MakeRig(EPaper2DPlusNetContext::SimulatedProxy, /*bEnableRep=*/true, /*bRegisterComponents=*/false);

	const uint32 BroadcastsAtStart = Rig.Comp->Test_GetLayersChangedBroadcastCount();

	Rig.Comp->GetRepAppearanceForTests() =
		NetAppLife_PresetSnapshot(Rig.Data, Rig.Data.ArmorPresetId, /*Sequence=*/8);
	Rig.Comp->OnRep_AppearanceState();

	TestTrue(TEXT("snapshot stashed before components exist"), Rig.Comp->HasPendingRepAppearanceForTests());
	TestEqual(TEXT("no apply ran while stashed (no broadcast)"),
		(int32)(Rig.Comp->Test_GetLayersChangedBroadcastCount() - BroadcastsAtStart), 0);
	TestFalse(TEXT("stashed snapshot did not commit anything"),
		Rig.Comp->IsLayerActive(Rig.Data.HoodLayerId));

	// Components come up (CreateLayerComponents equivalent), then the BeginPlay-tail drain applies the stash.
	for (const FCharacterLayer& Layer : Rig.Data.Asset->Layers)
	{
		UPaperSpriteComponent* SC = NewObject<UPaperSpriteComponent>();
		Rig.Comp->Test_RegisterLayerComponent(Layer.LayerName, SC, /*bVisible=*/false);
	}
	const uint32 BroadcastsBeforeDrain = Rig.Comp->Test_GetLayersChangedBroadcastCount();
	Rig.Comp->DrainPendingRepAppearanceForTests();

	TestFalse(TEXT("stash drained"), Rig.Comp->HasPendingRepAppearanceForTests());
	TestEqual(TEXT("deferred apply ran exactly once on drain"),
		(int32)(Rig.Comp->Test_GetLayersChangedBroadcastCount() - BroadcastsBeforeDrain), 1);
	TestEqual(TEXT("deferred apply landed the Armor preset"),
		Rig.Comp->GetAppearanceDescriptor().ActiveLayerIds,
		TArray<FGuid>({ Rig.Data.BodyLayerId, Rig.Data.HoodLayerId }));
	TestTrue(TEXT("deferred apply: Hood active"), Rig.Comp->IsLayerActive(Rig.Data.HoodLayerId));

	// Draining again is a no-op (nothing stashed) â€” no extra broadcast.
	const uint32 BroadcastsAfterDrain = Rig.Comp->Test_GetLayersChangedBroadcastCount();
	Rig.Comp->DrainPendingRepAppearanceForTests();
	TestEqual(TEXT("a second drain with nothing stashed is a no-op"),
		(int32)(Rig.Comp->Test_GetLayersChangedBroadcastCount() - BroadcastsAfterDrain), 0);

	// Idempotent re-apply safe: re-driving OnRep with the SAME sequence is gated out (different=>apply), no re-apply.
	Rig.Comp->OnRep_AppearanceState();
	TestEqual(TEXT("same-sequence OnRep re-drive is idempotent (no re-apply broadcast)"),
		(int32)(Rig.Comp->Test_GetLayersChangedBroadcastCount() - BroadcastsAfterDrain), 0);
	TestTrue(TEXT("idempotent re-drive keeps Hood active"), Rig.Comp->IsLayerActive(Rig.Data.HoodLayerId));
	return true;
}

// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
// SEAT A CONTRACT â€” REJECTED-SUPPORTED-PAYLOAD APPLY: a supported (PayloadVersion 1) snapshot whose descriptor
// is INCOMPATIBLE with the asset (asset skew: an ActiveLayerId the asset lacks) fails to apply, yet OnRep still
// COMMITS LastAppliedAppearanceSequence and warns ONCE. Consequences pinned worldlessly:
//   1. the committed descriptor is UNCHANGED (skew never becomes committed);
//   2. the sequence WAS committed â€” a later VALID snapshot at the SAME rejected Sequence does NOT apply
//      ("different=>apply" gating), proving the skew's sequence latched (INDEPENDENT of the warn-once latch);
//   3. the warning fires EXACTLY once across a same-sequence re-drive (AddExpectedError Occurrences=1).
// NOTE (Seat A coordination): this pins the CONTRACT and FAILS against the pre-Seat-A tree (today OnRep commits
// the sequence only when ApplyReplicatedAppearance returns true, and no warn fires on a rejected-but-supported
// payload). The warning substring is matched on the stable token "appearance snapshot"; align Seat A's warn text.
// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusNetAppearanceOnRepSkewCommitsSequenceWarnsOnce,
	"Paper2DPlus.Network.Appearance.OnRepSkewCommitsSequenceWarnsOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusNetAppearanceOnRepSkewCommitsSequenceWarnsOnce::RunTest(const FString& Parameters)
{
	FNetAppLife_Rig Rig = NetAppLife_MakeRig(EPaper2DPlusNetContext::SimulatedProxy);

	// Establish a known-good committed baseline (the Default preset) so a rejected apply is observable as "unchanged".
	Rig.Comp->GetRepAppearanceForTests() =
		NetAppLife_PresetSnapshot(Rig.Data, Rig.Data.DefaultPresetId, /*Sequence=*/5);
	Rig.Comp->OnRep_AppearanceState();
	TestTrue(TEXT("baseline Default committed"), Rig.Comp->IsLayerActive(Rig.Data.HatLayerId));

	// A SUPPORTED-payload SKEW snapshot at a fresh Sequence 6: a Layer GUID the asset does not contain, so
	// IsCompatible/NormalizeLayerSelection reject the descriptor ("unknown Layer IDs are rejected").
	const FGuid SkewLayerId(0xDEADBEEFu, 0xDEADBEEFu, 0xDEADBEEFu, 0xDEADBEEFu);
	FPaper2DPlusRepAppearanceState Skew;
	Skew.Sequence = 6;
	Skew.PayloadVersion = 1; // supported â€” the rejection is asset-compatibility, not payload version
	Skew.Appearance.DeliveryMode = ECharacterLayerUsageMode::RuntimeCustomizable;
	Skew.Appearance.ActiveLayerIds = { Rig.Data.BodyLayerId, SkewLayerId };

	// The warn-once diagnostic fires EXACTLY once across two same-sequence drives (a second emission would FAIL
	// as an unexpected error). Stable substring â€” see followups for the token contract Seat A must satisfy.
	AddExpectedError(TEXT("appearance snapshot"), EAutomationExpectedErrorFlags::Contains, 1);

	const uint32 BroadcastsBeforeSkew = Rig.Comp->Test_GetLayersChangedBroadcastCount();
	Rig.Comp->GetRepAppearanceForTests() = Skew;
	Rig.Comp->OnRep_AppearanceState();

	// 1. The rejected skew never became committed â€” the baseline Default selection stands, unbroadcast.
	TestEqual(TEXT("rejected skew did not change the committed descriptor"),
		Rig.Comp->GetAppearanceDescriptor().ActiveLayerIds,
		TArray<FGuid>({ Rig.Data.BodyLayerId, Rig.Data.HatLayerId }));
	TestFalse(TEXT("skew Layer never became active"), Rig.Comp->IsLayerActive(SkewLayerId));
	TestEqual(TEXT("a rejected apply emits no visibility broadcast"),
		(int32)(Rig.Comp->Test_GetLayersChangedBroadcastCount() - BroadcastsBeforeSkew), 0);

	// Re-drive the SAME rejected Sequence 6 â€” the committed sequence gate short-circuits it, so no re-attempt and
	// NO second warning (the AddExpectedError Occurrences=1 above catches a re-warn if the sequence were not committed).
	Rig.Comp->OnRep_AppearanceState();

	// 2. Sequence WAS committed (independent of the warn latch): a VALID Armor snapshot at the SAME Sequence 6 is
	//    gated out ("different=>apply"), so it must NOT apply â€” only true if the skew's Sequence 6 latched.
	Rig.Comp->GetRepAppearanceForTests() =
		NetAppLife_PresetSnapshot(Rig.Data, Rig.Data.ArmorPresetId, /*Sequence=*/6);
	Rig.Comp->OnRep_AppearanceState();
	TestFalse(TEXT("a valid snapshot at the rejected sequence does NOT apply (skew sequence was committed)"),
		Rig.Comp->IsLayerActive(Rig.Data.HoodLayerId));
	TestEqual(TEXT("committed descriptor still the pre-skew baseline"),
		Rig.Comp->GetAppearanceDescriptor().ActiveLayerIds,
		TArray<FGuid>({ Rig.Data.BodyLayerId, Rig.Data.HatLayerId }));

	// A FRESH sequence (7) with the same valid Armor descriptor DOES apply â€” the pipeline recovers after a skew.
	Rig.Comp->GetRepAppearanceForTests() =
		NetAppLife_PresetSnapshot(Rig.Data, Rig.Data.ArmorPresetId, /*Sequence=*/7);
	Rig.Comp->OnRep_AppearanceState();
	TestTrue(TEXT("a fresh-sequence valid snapshot recovers and applies (Hood active)"),
		Rig.Comp->IsLayerActive(Rig.Data.HoodLayerId));
	return true;
}

#endif // !UE_BUILD_SHIPPING

#endif // WITH_EDITOR
