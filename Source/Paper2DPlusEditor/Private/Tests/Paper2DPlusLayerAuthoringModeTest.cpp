// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#if WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "CharacterProfileEditorModel.h"
#include "Editor.h"
#include "FrameCueDataProvider.h"
#include "Paper2DPlusEditorTestFrameCueTypes.h"
#include "FrameEventEditor.h"
#include "HitboxDataProvider.h"
#include "LayerArtInspector.h"
#include "LayerCueInspector.h"
#include "Paper2DPlusCharacterLayerAsset.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "PaperFlipbook.h"
#include "ScopedTransaction.h"
#include "UObject/Package.h"

namespace Paper2DPlusLayerAuthoringModeTest
{
	struct FFixture
	{
		UPackage* Package = nullptr;
		UPaper2DPlusCharacterProfileAsset* Profile = nullptr;
		UPaper2DPlusCharacterLayerAsset* LayerAsset = nullptr;
		TSharedPtr<FCharacterProfileEditorModel> Model;
		TArray<UPaperFlipbook*> Flipbooks;
	};

	FHitboxData Box(EHitboxType Type, int32 Damage, int32 X = 0)
	{
		FHitboxData Result;
		Result.Type = Type;
		Result.Damage = Damage;
		Result.X = X;
		Result.Width = 4;
		Result.Height = 4;
		return Result;
	}

	FFixture MakeFixture(bool bAttached = true)
	{
		FFixture Fixture;
		Fixture.Package = CreatePackage(*FString::Printf(
			TEXT("/Temp/Paper2DPlusLayerAuthoring_%s"),
			*FGuid::NewGuid().ToString(EGuidFormats::Digits)));
		Fixture.Profile = NewObject<UPaper2DPlusCharacterProfileAsset>(
			Fixture.Package, TEXT("Profile"), RF_Transactional);
		for (int32 AnimationIndex = 0; AnimationIndex < 2; ++AnimationIndex)
		{
			UPaperFlipbook* Flipbook = NewObject<UPaperFlipbook>(
				Fixture.Package,
				*FString::Printf(TEXT("Flipbook_%d"), AnimationIndex),
				RF_Transactional);
			const int32 FrameCount = AnimationIndex == 0 ? 3 : 1;
			{
				FScopedFlipbookMutator Mutator(Flipbook);
				Mutator.KeyFrames.SetNum(FrameCount);
			}
			Fixture.Flipbooks.Add(Flipbook);
			FFlipbookProfileEntry& Entry = Fixture.Profile->Flipbooks.AddDefaulted_GetRef();
			Entry.Identity.FlipbookName = FString::Printf(TEXT("Animation_%d"), AnimationIndex);
			Entry.Identity.Flipbook = Flipbook;
			Entry.CombatData.Frames.SetNum(FrameCount);
		}
		Fixture.Profile->Flipbooks[0].CombatData.Frames[0].FrameName = TEXT("BaselineFrame");
		Fixture.Profile->Flipbooks[0].CombatData.Frames[0].bInvulnerable = true;
		Fixture.Profile->Flipbooks[0].CombatData.Frames[0].Hitboxes.Add(Box(EHitboxType::Attack, 1));
		Fixture.Profile->Flipbooks[0].CombatData.Frames[0].Hitboxes.Add(Box(EHitboxType::Hurtbox, 2));
		Fixture.Profile->Flipbooks[0].CombatData.Frames[0].Hitboxes.Add(Box(EHitboxType::Collision, 3));

		Fixture.LayerAsset = NewObject<UPaper2DPlusCharacterLayerAsset>(
			Fixture.Package, TEXT("Layers"), RF_Transactional);
		Fixture.LayerAsset->BaseProfile = Fixture.Profile;
		Fixture.LayerAsset->UsageMode = ECharacterLayerUsageMode::RuntimeCustomizable;
		Fixture.LayerAsset->BakeSetId = FGuid::NewGuid();
		FCharacterLayerAppearancePreset& DefaultAppearance =
			Fixture.LayerAsset->AppearancePresets.AddDefaulted_GetRef();
		DefaultAppearance.PresetId = FGuid::NewGuid();
		DefaultAppearance.DisplayName = TEXT("Default Appearance");
		for (int32 LayerIndex = 0; LayerIndex < 3; ++LayerIndex)
		{
			FCharacterLayer& Layer = Fixture.LayerAsset->Layers.AddDefaulted_GetRef();
			Layer.LayerId = FGuid::NewGuid();
			Layer.LayerName = FString::Printf(TEXT("Layer_%d"), LayerIndex);
			DefaultAppearance.ActiveLayerIds.Add(Layer.LayerId);
		}
		Fixture.LayerAsset->DefaultAppearancePresetId = DefaultAppearance.PresetId;
		if (bAttached)
		{
			Fixture.LayerAsset->BakeAttachmentState = ECharacterLayerBakeAttachmentState::Attached;
			Fixture.Profile->CaptureCharacterBaselineFromRuntime(
				Fixture.LayerAsset->BakeSetId, Fixture.LayerAsset->GetPathName(), true);
		}

		Fixture.Model = MakeShared<FCharacterProfileEditorModel>();
		Fixture.Model->InitializeFromAsset(Fixture.Profile);
		Fixture.Model->SetSecondaryWatchedObject(Fixture.LayerAsset);
		Fixture.Model->SetSelectedFlipbook(0);
		Fixture.Model->SetSelectedFrame(0);
		Fixture.Model->SetSelectedLayerById(Fixture.LayerAsset->Layers[1].LayerId);
		Fixture.Package->SetDirtyFlag(false);
		return Fixture;
	}

	FCharacterLayerAuthoredAnimationData& AddSource(
		FFixture& Fixture,
		int32 LayerIndex,
		int32 FrameCount = 3)
	{
		FCharacterLayerAuthoredAnimationData& Source =
			Fixture.LayerAsset->Layers[LayerIndex].AuthoredAnimations.AddDefaulted_GetRef();
		Source.Flipbook = Fixture.Profile->Flipbooks[0].Identity.Flipbook;
		Source.LegacyAnimationName = Fixture.Profile->Flipbooks[0].Identity.FlipbookName;
		Source.Frames.SetNum(FrameCount);
		return Source;
	}

	int32 CountType(const TArray<FHitboxData>& Boxes, EHitboxType Type)
	{
		int32 Count = 0;
		for (const FHitboxData& Box : Boxes)
		{
			if (Box.Type == Type)
			{
				++Count;
			}
		}
		return Count;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusLayerAuthoringSharedModeStateTest,
	"Paper2DPlus.LayerAuthoringModes.SharedSelectionFrameClamp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusLayerAuthoringSharedModeStateTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusLayerAuthoringModeTest;
	FFixture Fixture = MakeFixture();
	Fixture.Model->SetSelectedFrame(2);
	int32 FrameBroadcasts = 0;
	Fixture.Model->OnFrameSelectionChanged.AddLambda([&FrameBroadcasts]() { ++FrameBroadcasts; });
	// The clamp is the MODEL's, not any host widget's: SetSelectedFlipbook resolves identity and
	// then clamps the frame to the new animation's key-frame count. This used to be asserted through
	// the retired embedded workspace, which never participated in it — the widget was scenery.
	const FGuid SelectedLayerId = Fixture.Model->GetSelectedLayerId();
	Fixture.Model->SetSelectedFlipbook(1);
	TestEqual(TEXT("the shared model clamps once for the one-frame animation"),
		Fixture.Model->GetSelectedFrameIndex(), 0);
	TestEqual(TEXT("one animation change emits one frame clamp notification"), FrameBroadcasts, 1);
	TestEqual(TEXT("changing animation preserves stable LayerId selection"),
		Fixture.Model->GetSelectedLayerId(), SelectedLayerId);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusLayerAuthoringSourceTransactionTest,
	"Paper2DPlus.LayerAuthoringModes.FirstBoxAndCueCreateSelectedSourceWithOneUndo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusLayerAuthoringSourceTransactionTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusLayerAuthoringModeTest;
	if (!TestNotNull(TEXT("GEditor available"), GEditor)) return false;
	GEditor->ResetTransaction(NSLOCTEXT("LayerAuthoringModeTest", "Reset", "Reset Layer Authoring Test"));
	FFixture Fixture = MakeFixture();
	FLayerHitboxDataProvider Hitboxes(Fixture.LayerAsset, Fixture.Model);
	FLayerFrameCueDataProvider Cues(Fixture.LayerAsset, Fixture.Model);

	{
		FScopedTransaction Transaction(NSLOCTEXT("LayerAuthoringModeTest", "AddBox", "Add Layer Box"));
		Fixture.LayerAsset->Modify();
		Hitboxes.BeginEdit();
		FFrameHitboxData* Frame = Hitboxes.EnsureFrameMutable(0, 0);
		if (TestNotNull(TEXT("first box edit creates selected source"), Frame))
		{
			Frame->Hitboxes.Add(Box(EHitboxType::Attack, 50, 4));
		}
		Hitboxes.CommitEdit();
	}
	TestEqual(TEXT("only selected layer receives one source row"),
		Fixture.LayerAsset->Layers[1].AuthoredAnimations.Num(), 1);
	TestTrue(TEXT("other layers remain source-empty"),
		Fixture.LayerAsset->Layers[0].AuthoredAnimations.IsEmpty()
		&& Fixture.LayerAsset->Layers[2].AuthoredAnimations.IsEmpty());
	TestEqual(TEXT("compiled Profile output remains untouched"),
		Fixture.Profile->Flipbooks[0].CombatData.Frames[0].Hitboxes.Num(), 3);
	TestTrue(TEXT("one undo removes the first source row"), GEditor->UndoTransaction(true));
	Hitboxes.InvalidateCachedViews();
	TestTrue(TEXT("selected layer source is gone after one undo"),
		Fixture.LayerAsset->Layers[1].AuthoredAnimations.IsEmpty());
	TestTrue(TEXT("redo restores the box source"), GEditor->RedoTransaction());
	Hitboxes.InvalidateCachedViews();

	{
		FScopedTransaction Transaction(NSLOCTEXT("LayerAuthoringModeTest", "AddCue", "Add Layer Cue"));
		Fixture.LayerAsset->Modify();
		TArray<TObjectPtr<UPaper2DPlusCueBase>>* SourceCues = Cues.EnsureCuesMutable(0);
		if (TestNotNull(TEXT("Cue edit reuses selected source"), SourceCues))
		{
			UPaper2DPlusEditorTestMomentCue* Cue = NewObject<UPaper2DPlusEditorTestMomentCue>(
				Fixture.LayerAsset, NAME_None, RF_Transactional);
			Cue->TriggerFrame = 0;
			SourceCues->Add(Cue);
		}
	}
	TestEqual(TEXT("source owns one Cue"),
		Fixture.LayerAsset->Layers[1].AuthoredAnimations[0].FrameCues.Num(), 1);
	TestTrue(TEXT("one undo removes only the Cue operation"), GEditor->UndoTransaction(true));
	TestTrue(TEXT("source row and box survive Cue undo"),
		Fixture.LayerAsset->Layers[1].AuthoredAnimations.Num() == 1
		&& Fixture.LayerAsset->Layers[1].AuthoredAnimations[0].FrameCues.IsEmpty()
		&& Fixture.LayerAsset->Layers[1].AuthoredAnimations[0].Frames[0].AttackBoxes.Num() == 1);
	GEditor->ResetTransaction(NSLOCTEXT("LayerAuthoringModeTest", "End", "End Layer Authoring Test"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusLayerAuthoringCompositionTest,
	"Paper2DPlus.LayerAuthoringModes.LocalPlacementReplaceLowerAndReadOnlyGhosts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusLayerAuthoringCompositionTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusLayerAuthoringModeTest;
	FFixture Fixture = MakeFixture();
	FCharacterLayerAuthoredAnimationData& Lower = AddSource(Fixture, 0);
	Lower.Frames[0].AttackBoxes.Add(Box(EHitboxType::Attack, 10, 1));
	FCharacterLayerAuthoredAnimationData& Selected = AddSource(Fixture, 1);
	Selected.AttackMerge = EPaper2DPlusLayerSourceMerge::ReplaceLower;
	Selected.HurtMerge = EPaper2DPlusLayerSourceMerge::Add;
	Selected.Frames[0].HurtBoxes.Add(Box(EHitboxType::Hurtbox, 12, 2));
	UPaper2DPlusEditorTestMomentCue* Cue = NewObject<UPaper2DPlusEditorTestMomentCue>(
		Fixture.LayerAsset, NAME_None, RF_Transactional);
	Selected.FrameCues.Add(Cue);
	FLayerHitboxDataProvider Provider(Fixture.LayerAsset, Fixture.Model);

	TArray<FHitboxData> LowerGhosts;
	TArray<FHitboxData> FinalGhosts;
	Provider.GetGhostBoxes(0, 0, LowerGhosts);
	Provider.GetFinalGhostBoxes(0, 0, FinalGhosts);
	TestEqual(TEXT("Lower projection contains baseline and lower-layer attacks"),
		CountType(LowerGhosts, EHitboxType::Attack), 2);
	TestEqual(TEXT("empty Replace Lower source cannot silently disarm attacks"),
		CountType(FinalGhosts, EHitboxType::Attack), 2);

	Selected.Frames[0].AttackBoxes.Add(Box(EHitboxType::Attack, 20, 3));
	Provider.GetFinalGhostBoxes(0, 0, FinalGhosts);
	TestEqual(TEXT("authored Replace Lower leaves one selected attack"),
		CountType(FinalGhosts, EHitboxType::Attack), 1);
	const FHitboxData* FinalAttack = FinalGhosts.FindByPredicate(
		[](const FHitboxData& Candidate) { return Candidate.Type == EHitboxType::Attack; });
	TestTrue(TEXT("final attack is the selected source payload"), FinalAttack && FinalAttack->Damage == 20);

	const int32 LocalXBeforeMove = Selected.Frames[0].AttackBoxes[0].X;
	const int32 SocketCountBeforeMove = Selected.Frames[0].Sockets.Num();
	const UPaper2DPlusCueBase* CueBeforeMove = Selected.FrameCues[0];
	TestTrue(TEXT("Art placement changes only the placement field"),
		SLayerArtInspector::SetPlacement(
			*Fixture.LayerAsset, *Fixture.Model, ELayerArtOffsetScope::AllAnimations, FVector2D(8, -2)));
	TestFalse(TEXT("recommitting identical Art placement is a transaction-free no-op"),
		SLayerArtInspector::SetPlacement(
			*Fixture.LayerAsset, *Fixture.Model, ELayerArtOffsetScope::AllAnimations, FVector2D(8, -2)));
	TestEqual(TEXT("moving art preserves source-local box X"),
		Selected.Frames[0].AttackBoxes[0].X, LocalXBeforeMove);
	TestEqual(TEXT("moving art preserves source-local socket array"),
		Selected.Frames[0].Sockets.Num(), SocketCountBeforeMove);
	TestTrue(TEXT("moving art preserves Cue identity"), Selected.FrameCues[0] == CueBeforeMove);
	TestEqual(TEXT("display applies placement without rewriting source"),
		Provider.GetAuthoringDisplayOffsetPx(0, 0), FVector2D(8, -2));
	Provider.GetFinalGhostBoxes(0, 0, FinalGhosts);
	FinalAttack = FinalGhosts.FindByPredicate(
		[](const FHitboxData& Candidate) { return Candidate.Type == EHitboxType::Attack; });
	TestTrue(TEXT("compiled projection translates the local attack with art"),
		FinalAttack && FinalAttack->X == LocalXBeforeMove + 8);
	if (FinalAttack) const_cast<FHitboxData*>(FinalAttack)->Damage = 999;
	TestEqual(TEXT("editing a ghost copy cannot mutate selected source"),
		Selected.Frames[0].AttackBoxes[0].Damage, 20.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusLayerAuthoringBaselineRoutingTest,
	"Paper2DPlus.LayerAuthoringModes.AttachedProfileRoutesBaselineAndUnmanagedStaysRuntime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusLayerAuthoringBaselineRoutingTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusLayerAuthoringModeTest;
	FFixture Attached = MakeFixture(true);
	FPaper2DPlusCharacterBaselineAnimation* Baseline = Attached.Profile->FindCharacterBaselineMutable(
		Attached.Profile->Flipbooks[0].Identity.Flipbook.ToSoftObjectPath(),
		Attached.Profile->Flipbooks[0].Identity.FlipbookName);
	if (!TestNotNull(TEXT("attached Profile owns a Character Baseline"), Baseline)) return false;
	Attached.Profile->Flipbooks[0].CombatData.Frames[0].Hitboxes[0].Damage = 500;
	FProfileHitboxDataProvider Hitboxes(Attached.Model);
	FFrameHitboxData* SourceFrame = Hitboxes.GetFrameMutable(0, 0);
	if (!TestNotNull(TEXT("Profile Hitboxes resolve Baseline source"), SourceFrame)) return false;
	const FString FrameName = SourceFrame->FrameName;
	const bool bInvulnerable = SourceFrame->bInvulnerable;
	const int32 CollisionCount = CountType(SourceFrame->Hitboxes, EHitboxType::Collision);
	SourceFrame->Hitboxes.Add(Box(EHitboxType::Attack, 42));
	TestEqual(TEXT("Baseline edit preserves frame name"), SourceFrame->FrameName, FrameName);
	TestEqual(TEXT("Baseline edit preserves frame invulnerability"), SourceFrame->bInvulnerable, bInvulnerable);
	TestEqual(TEXT("Baseline edit preserves Collision compatibility boxes"),
		CountType(SourceFrame->Hitboxes, EHitboxType::Collision), CollisionCount);
	TestEqual(TEXT("compiled runtime frame remains managed read-only output"),
		Attached.Profile->Flipbooks[0].CombatData.Frames[0].Hitboxes.Num(), 3);
	TArray<FHitboxData> CompiledGhosts;
	Hitboxes.GetGhostBoxes(0, 0, CompiledGhosts);
	TestTrue(TEXT("compiled output is exposed as a distinct ghost projection"),
		!CompiledGhosts.IsEmpty() && CompiledGhosts[0].Damage == 500);

	FProfileFrameCueDataProvider Cues(Attached.Model);
	UPaper2DPlusEditorTestMomentCue* BaselineCue = NewObject<UPaper2DPlusEditorTestMomentCue>(
		Attached.Profile, NAME_None, RF_Transactional);
	Cues.GetCuesMutable(0)->Add(BaselineCue);
	TestEqual(TEXT("Character-wide Cue edit lands in Baseline"), Baseline->FrameCues.Num(), 1);
	TestTrue(TEXT("compiled runtime Cue output remains untouched"),
		Attached.Profile->Flipbooks[0].FrameEventData.FrameCues.IsEmpty());
	TestNotNull(TEXT("attached Profile exposes compiled Cue projection"), Cues.GetFinalCues(0));

	FFixture Unmanaged = MakeFixture(false);
	FProfileHitboxDataProvider UnmanagedProvider(Unmanaged.Model);
	FFrameHitboxData* RuntimeFrame = UnmanagedProvider.GetFrameMutable(0, 0);
	if (TestNotNull(TEXT("unmanaged Profile keeps direct runtime authoring"), RuntimeFrame))
	{
		RuntimeFrame->Hitboxes[0].Damage = 77;
	}
	TestEqual(TEXT("unmanaged path remains byte-compatible"),
		Unmanaged.Profile->Flipbooks[0].CombatData.Frames[0].Hitboxes[0].Damage, 77.f);
	return true;
}

#endif // WITH_EDITOR
