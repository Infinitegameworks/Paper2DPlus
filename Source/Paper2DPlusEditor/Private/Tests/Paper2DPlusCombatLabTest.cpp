// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "CombatProfileEditor/CombatLabCanvas.h"
#include "CombatProfileEditor/CombatLabPanel.h"
#include "CombatProfileEditor/CombatProfileEditorSession.h"
#include "CombatProfileEditor/CombatScorePlaygroundPanel.h"
#include "Editor.h"
#include "InputCoreTypes.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Paper2DPlusCombatProfileAsset.h"
#include "PaperFlipbook.h"
#include "PaperSprite.h"
#include "Engine/Texture2D.h"
#include "UObject/UnrealType.h"

namespace CombatLabTestPrivate
{
	UPaperSprite* MakePivotSprite(UObject* Outer, const FVector2D& Pivot)
	{
		UTexture2D* Texture = UTexture2D::CreateTransient(32, 32, PF_B8G8R8A8);
		if (!Texture) return nullptr;
		UPaperSprite* Sprite = NewObject<UPaperSprite>(Outer);
		FSpriteAssetInitParameters Init;
		Init.Texture = Texture;
		Init.Offset = FIntPoint::ZeroValue;
		Init.Dimension = FIntPoint(32, 32);
		Init.SetPixelsPerUnrealUnit(1.0f);
		Sprite->InitializeSprite(Init);
		if (FBoolProperty* SnapProperty = FindFProperty<FBoolProperty>(
			UPaperSprite::StaticClass(), TEXT("bSnapPivotToPixelGrid")))
		{
			SnapProperty->SetPropertyValue_InContainer(Sprite, false);
		}
		Sprite->SetPivotMode(ESpritePivotMode::Custom, Pivot, false);
		return Sprite;
	}

	FFlipbookProfileEntry MakeMove(UObject* Outer, const TCHAR* Name, bool bAttack)
	{
		FFlipbookProfileEntry Entry;
		Entry.Identity.FlipbookName = Name;
		Entry.Identity.Flipbook = NewObject<UPaperFlipbook>(Outer);
		{
			FScopedFlipbookMutator Mutator(Entry.Identity.Flipbook.Get());
			Mutator.FramesPerSecond = 10.0f;
			Mutator.KeyFrames.SetNum(2);
			for (FPaperFlipbookKeyFrame& KeyFrame : Mutator.KeyFrames)
			{
				KeyFrame.FrameRun = 1;
			}
		}
		Entry.CombatData.Frames.SetNum(2);
		FHitboxData Box;
		Box.Type = bAttack ? EHitboxType::Attack : EHitboxType::Hurtbox;
		Box.X = bAttack ? 0 : -10;
		Box.Y = 0;
		Box.Width = bAttack ? 40 : 20;
		Box.Height = 30;
		Box.Damage = bAttack ? 8 : 0;
		Entry.CombatData.Frames[0].Hitboxes.Add(Box);
		if (!bAttack)
		{
			// Keep Guard useful as the defender while also making it a legitimate
			// scored attack for bidirectional score/Lab selection coverage.
			FHitboxData DistantAttack;
			DistantAttack.Type = EHitboxType::Attack;
			DistantAttack.X = 100;
			DistantAttack.Width = 10;
			DistantAttack.Height = 10;
			DistantAttack.Damage = 1;
			Entry.CombatData.Frames[0].Hitboxes.Add(DistantAttack);
		}
		return Entry;
	}

	UPaper2DPlusCombatProfileAsset* MakeAsset()
	{
		UPaper2DPlusCombatProfileAsset* Combat = NewObject<UPaper2DPlusCombatProfileAsset>(
			GetTransientPackage(), NAME_None, RF_Transactional);
		Combat->CharacterProfile = NewObject<UPaper2DPlusCharacterProfileAsset>(
			Combat, NAME_None, RF_Transactional);
		Combat->CharacterProfile->Flipbooks.Add(MakeMove(Combat->CharacterProfile, TEXT("Jab"), true));
		Combat->CharacterProfile->Flipbooks.Add(MakeMove(Combat->CharacterProfile, TEXT("Guard"), false));
		for (const FFlipbookProfileEntry& Entry : Combat->CharacterProfile->Flipbooks)
		{
			FPaper2DPlusCombatAttackOption Option;
			Option.MoveName = FName(*Entry.Identity.FlipbookName);
			Option.MoveFlipbook = Entry.Identity.Flipbook;
			Option.bIncludeWhenNotTagged = true;
			Combat->AttackOptions.Add(Option);
		}
		Combat->RefreshAttackOptionMoveBindings();
		return Combat;
	}

	FGameplayTag GroundTag()
	{
		return FGameplayTag::RequestGameplayTag(
			FName(TEXT("PlayerStates.Attacking.GroundAttack")), false);
	}

	FGameplayTag AirTag()
	{
		return FGameplayTag::RequestGameplayTag(
			FName(TEXT("PlayerStates.Attacking.AirAttack")), false);
	}

	FGameplayTag AggressionTag()
	{
		return FGameplayTag::RequestGameplayTag(
			FName(TEXT("Paper2DPlus.Combat.Var.Aggression")), false);
	}

	FGameplayTag CanPunishTag()
	{
		return FGameplayTag::RequestGameplayTag(
			FName(TEXT("Paper2DPlus.Combat.Var.CanPunish")), false);
	}

	FPaper2DPlusCombatConsideration MakeConsideration(
		FName Name,
		EPaper2DPlusCombatConsiderationSource Source)
	{
		FPaper2DPlusCombatConsideration Result;
		Result.ConsiderationName = Name;
		Result.Source = Source;
		Result.Operation = EPaper2DPlusCombatConsiderationOp::Linear;
		Result.MinValue = 0.0f;
		Result.MaxValue = 1.0f;
		return Result;
	}

	void ConfigureFullContextScoring(UPaper2DPlusCombatProfileAsset* Combat)
	{
		check(Combat && Combat->AttackOptions.Num() == 2);
		Combat->AttackOptions[0].RoleTags.AddTag(GroundTag());
		Combat->AttackOptions[1].RoleTags.AddTag(AirTag());

		FPaper2DPlusCombatVariableDefinition Aggression;
		Aggression.VariableTag = AggressionTag();
		Aggression.Type = EPaper2DPlusCombatVariableType::Float;
		Combat->VariableDefinitions.Add(Aggression);
		FPaper2DPlusCombatVariableDefinition CanPunish;
		CanPunish.VariableTag = CanPunishTag();
		CanPunish.Type = EPaper2DPlusCombatVariableType::Bool;
		Combat->VariableDefinitions.Add(CanPunish);

		FPaper2DPlusCombatScoringProfile Profile;
		Profile.ProfileName = TEXT("Contextual");
		Profile.GlobalConsiderations.Add(MakeConsideration(
			TEXT("SelfHealth"), EPaper2DPlusCombatConsiderationSource::SelfHealthPercent));
		Profile.GlobalConsiderations.Add(MakeConsideration(
			TEXT("TargetHealth"), EPaper2DPlusCombatConsiderationSource::TargetHealthPercent));
		FPaper2DPlusCombatConsideration TargetState = MakeConsideration(
			TEXT("TargetState"), EPaper2DPlusCombatConsiderationSource::TargetStateTags);
		TargetState.Operation = EPaper2DPlusCombatConsiderationOp::TagAny;
		TargetState.RequiredTags.AddTag(AirTag());
		Profile.GlobalConsiderations.Add(TargetState);
		FPaper2DPlusCombatConsideration AggressionRule = MakeConsideration(
			TEXT("Aggression"), EPaper2DPlusCombatConsiderationSource::CustomFloat);
		AggressionRule.VariableTag = AggressionTag();
		Profile.GlobalConsiderations.Add(AggressionRule);
		FPaper2DPlusCombatConsideration PunishRule = MakeConsideration(
			TEXT("CanPunish"), EPaper2DPlusCombatConsiderationSource::CustomBool);
		PunishRule.Operation = EPaper2DPlusCombatConsiderationOp::BoolEquals;
		PunishRule.VariableTag = CanPunishTag();
		PunishRule.bExpectedBool = true;
		Profile.GlobalConsiderations.Add(PunishRule);
		Combat->ScoringProfiles.Add(Profile);
		Combat->RebuildVariableBags();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCombatLabFrameGeometryParityTest,
	"Paper2DPlus.Editor.CombatProfile.CombatLab.PivotFacingRootMotionAndLiveEditParity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCombatLabFrameGeometryParityTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusCombatProfileAsset* Combat = CombatLabTestPrivate::MakeAsset();
	UPaper2DPlusCharacterProfileAsset* Character = Combat->CharacterProfile;
	UPaperSprite* Sprite = CombatLabTestPrivate::MakePivotSprite(Character, FVector2D(16.5f, 31.25f));
	TestNotNull(TEXT("Fixture creates a fractional bottom-center sprite pivot"), Sprite);
	if (!Sprite) return false;

	for (FFlipbookProfileEntry& Entry : Character->Flipbooks)
	{
		FScopedFlipbookMutator Mutator(Entry.Identity.Flipbook.Get());
		for (FPaperFlipbookKeyFrame& KeyFrame : Mutator.KeyFrames)
		{
			KeyFrame.Sprite = Sprite;
		}
		Entry.MotionData.RootMotion.SetNum(2);
	}
	Character->Flipbooks[0].MotionData.RootMotion[0].Position = FVector2D(6.0f, -4.0f);
	Character->Flipbooks[1].MotionData.RootMotion[0].Position = FVector2D(8.0f, -2.0f);

	TSharedRef<FCombatProfileEditorSession> Session = MakeShared<FCombatProfileEditorSession>(Combat);
	TSharedRef<FCombatLabModel> Lab = MakeShared<FCombatLabModel>(Session);
	Lab->SetMove(0, TEXT("Jab"));
	Lab->SetMove(1, TEXT("Guard"));
	Lab->SetPosition(0, FVector2D::ZeroVector);
	Lab->SetPosition(1, FVector2D(100.0f, 0.0f));

	TestTrue(TEXT("Right-facing preview applies authored cumulative root motion"),
		Lab->GetParticipantPreviewPosition(0).Equals(FVector2D(6.0f, 4.0f)));
	TestTrue(TEXT("Left-facing preview mirrors root-motion X and converts texture Y to world Z"),
		Lab->GetParticipantPreviewPosition(1).Equals(FVector2D(92.0f, 2.0f)));

	const FCombatLabEvaluation& Initial = Lab->GetEvaluation();
	TestTrue(TEXT("Attacker emits a world box"), Initial.AttackerBoxes.Num() > 0);
	TestTrue(TEXT("Defender emits world boxes"), Initial.DefenderBoxes.Num() > 0);
	if (Initial.AttackerBoxes.Num() > 0)
	{
		// Pivot floor=(16,31), fraction=(.5,.25); Jab raw rect=(0,0,40,30).
		TestTrue(FString::Printf(
			TEXT("Attacker X matches the shared fractional-pivot transform (actual %.3f)"),
			Initial.AttackerBoxes[0].Center.X),
			FMath::IsNearlyEqual(Initial.AttackerBoxes[0].Center.X, 9.5f, 0.01f));
		TestTrue(FString::Printf(
			TEXT("Attacker Z matches the shared top-left-to-pivot transform (actual %.3f)"),
			Initial.AttackerBoxes[0].Center.Z),
			FMath::IsNearlyEqual(Initial.AttackerBoxes[0].Center.Z, 20.25f, 0.01f));
	}
	if (Initial.DefenderBoxes.Num() > 0)
	{
		// Guard hurt rect=(-10,0,20,30) is mirrored after pivot conversion around preview X=92.
		TestTrue(FString::Printf(
			TEXT("Defender X matches the shared facing-left transform (actual %.3f)"),
			Initial.DefenderBoxes[0].Center.X),
			FMath::IsNearlyEqual(Initial.DefenderBoxes[0].Center.X, 108.5f, 0.01f));
		TestTrue(FString::Printf(
			TEXT("Defender Z retains fractional pivot and root-motion height (actual %.3f)"),
			Initial.DefenderBoxes[0].Center.Z),
			FMath::IsNearlyEqual(Initial.DefenderBoxes[0].Center.Z, 18.25f, 0.01f));
	}

	const float CenterBeforeEdit = Initial.AttackerBoxes.Num() > 0
		? Initial.AttackerBoxes[0].Center.X
		: 0.0f;
	Character->Flipbooks[0].CombatData.Frames[0].Hitboxes[0].X += 7;
	Session->OnDataChanged().Broadcast();
	TestTrue(TEXT("Editing the selected move reevaluates geometry without a selection change"),
		Lab->GetEvaluation().AttackerBoxes.Num() > 0
		&& FMath::IsNearlyEqual(
			Lab->GetEvaluation().AttackerBoxes[0].Center.X,
			CenterBeforeEdit + 7.0f,
			0.01f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCombatLabCollisionTest,
	"Paper2DPlus.Editor.CombatProfile.CombatLab.WorldlessCollisionDistanceAndScrub",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCombatLabCollisionTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusCombatProfileAsset* Combat = CombatLabTestPrivate::MakeAsset();
	TSharedRef<FCombatProfileEditorSession> Session = MakeShared<FCombatProfileEditorSession>(Combat);
	TSharedRef<FCombatLabModel> Lab = MakeShared<FCombatLabModel>(Session);
	Lab->SetMove(0, TEXT("Jab"));
	Lab->SetMove(1, TEXT("Guard"));
	Lab->SetPosition(0, FVector2D(0.0f, 0.0f));
	Lab->SetPosition(1, FVector2D(20.0f, 0.0f));
	TestEqual(TEXT("Participant spacing becomes runtime context distance"),
		Session->GetPreviewContext().DistanceToTarget, 20.0f);
	TestTrue(TEXT("Existing 2D intersection helper reports the active attack/hurt overlap"),
		Lab->GetEvaluation().bAnyCollision);
	TestEqual(TEXT("One overlap is counted"), Lab->GetEvaluation().AttackHurtOverlapCount, 1);

	Lab->SetFrame(0, 1);
	TestFalse(TEXT("Independent attacker scrub selects the empty second frame"),
		Lab->GetEvaluation().bAnyCollision);
	Lab->SetFrame(0, 0);
	int32 ScoreRefreshCount = 0;
	const FDelegateHandle ScoreHandle = Session->OnScoreChanged().AddLambda(
		[&ScoreRefreshCount]() { ++ScoreRefreshCount; });
	Lab->SetPosition(1, FVector2D(200.0f, 0.0f));
	Session->OnScoreChanged().Remove(ScoreHandle);
	TestFalse(TEXT("Dragging outside range clears collision"), Lab->GetEvaluation().bAnyCollision);
	TestEqual(TEXT("Drag refreshes score context once to the new distance"),
		Session->GetPreviewContext().DistanceToTarget, 200.0f);
	TestEqual(TEXT("One drag mutation performs one score refresh"), ScoreRefreshCount, 1);

	TArray<FPaper2DPlusCombatRankedOption> Direct;
	Combat->ScoreAttackOptions(Session->GetPreviewContext(), Direct, Session->GetScoringProfileName());
	TestEqual(TEXT("Lab ranking count matches the runtime helper"), Session->GetRankedOptions().Num(), Direct.Num());
	if (Direct.Num() > 0 && Session->GetRankedOptions().Num() > 0)
	{
		TestEqual(TEXT("Lab top move matches runtime scoring"),
			Session->GetRankedOptions()[0].Attack.MoveName, Direct[0].Attack.MoveName);
		TestEqual(TEXT("Lab top score matches runtime scoring"),
			Session->GetRankedOptions()[0].Score, Direct[0].Score);
	}
	TSharedRef<SCombatLabPanel> Panel = SNew(SCombatLabPanel).Model(Lab);
	TestTrue(TEXT("Lab always states the advisory-only boundary"),
		Panel->GetStatusText().ToString().Contains(TEXT("executes no gameplay")));
	TestTrue(TEXT("Lab links the visual attacker to a compact score explanation"),
		Panel->GetScoreExplanationText().ToString().Contains(TEXT("Scored move Jab"))
		&& Panel->GetScoreExplanationText().ToString().Contains(TEXT("Score Playground")));

	TestTrue(TEXT("Score selection can drive the attacker preview move"), Session->SelectAttackByName(TEXT("Guard")));
	TestEqual(TEXT("Lab attacker follows shared score selection"), Lab->GetParticipant(0).MoveName, FName(TEXT("Guard")));
	Lab->SetMove(0, TEXT("Jab"));
	TestEqual(TEXT("Attacker move selection drives the shared scored move"),
		Session->GetSelectedAttack().FallbackKey, FString(TEXT("Jab")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCombatLabFullContextTest,
	"Paper2DPlus.Editor.CombatProfile.CombatLab.FullContextRuntimeParityAndBreakdown",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCombatLabFullContextTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusCombatProfileAsset* Combat = CombatLabTestPrivate::MakeAsset();
	CombatLabTestPrivate::ConfigureFullContextScoring(Combat);
	TSharedRef<FCombatProfileEditorSession> Session = MakeShared<FCombatProfileEditorSession>(Combat);
	Session->SetScoringProfileName(TEXT("Contextual"));
	TSharedRef<SCombatScorePlaygroundPanel> Playground =
		SNew(SCombatScorePlaygroundPanel).Session(Session);

	FPaper2DPlusCombatRuntimeContext Base;
	Base.DistanceToTarget = 0.0f;
	Base.SelfHealthPercent = 1.0f;
	Base.TargetHealthPercent = 1.0f;
	Base.DesiredRoleTags.AddTag(CombatLabTestPrivate::GroundTag());
	Base.TargetStateTags.AddTag(CombatLabTestPrivate::AirTag());
	Base.RuntimeVariables.FindOrAdd(CombatLabTestPrivate::AggressionTag()).FloatValue = 1.0f;
	Base.RuntimeVariables.FindOrAdd(CombatLabTestPrivate::CanPunishTag()).BoolValue = true;

	auto VerifyRuntimeParity = [this, Combat, &Session, &Playground](
		const TCHAR* Label,
		const FPaper2DPlusCombatRuntimeContext& Context,
		FName ExpectedProfile = TEXT("Contextual"))
	{
		Playground->SetContextForTests(Context);
		TArray<FPaper2DPlusCombatRankedOption> Direct;
		Combat->ScoreAttackOptions(Context, Direct, ExpectedProfile);
		TestEqual(FString::Printf(TEXT("%s ranked count"), Label), Playground->GetRankedCountForTests(), Direct.Num());
		for (const FPaper2DPlusCombatRankedOption& Expected : Direct)
		{
			const FPaper2DPlusCombatRankedOption* Actual = Playground->FindRankedForTests(Expected.Attack.MoveName);
			TestNotNull(FString::Printf(TEXT("%s contains %s"), Label, *Expected.Attack.MoveName.ToString()), Actual);
			if (!Actual) continue;
			TestEqual(FString::Printf(TEXT("%s %s score"), Label, *Expected.Attack.MoveName.ToString()), Actual->Score, Expected.Score);
			TestEqual(FString::Printf(TEXT("%s %s term count"), Label, *Expected.Attack.MoveName.ToString()),
				Actual->Breakdown.Terms.Num(), Expected.Breakdown.Terms.Num());
			for (int32 TermIndex = 0; TermIndex < Expected.Breakdown.Terms.Num(); ++TermIndex)
			{
				const FPaper2DPlusCombatScoreTerm& A = Actual->Breakdown.Terms[TermIndex];
				const FPaper2DPlusCombatScoreTerm& E = Expected.Breakdown.Terms[TermIndex];
				TestEqual(FString::Printf(TEXT("%s term %d name"), Label, TermIndex), A.TermName, E.TermName);
				TestEqual(FString::Printf(TEXT("%s term %d raw"), Label, TermIndex), A.RawValue, E.RawValue);
				TestEqual(FString::Printf(TEXT("%s term %d normalized"), Label, TermIndex), A.NormalizedValue, E.NormalizedValue);
				TestEqual(FString::Printf(TEXT("%s term %d weight"), Label, TermIndex), A.Weight, E.Weight);
				TestEqual(FString::Printf(TEXT("%s term %d combine"), Label, TermIndex), A.CombineMode, E.CombineMode);
			}
		}
		return Direct.Num() > 0 ? Direct[0] : FPaper2DPlusCombatRankedOption();
	};

	const FPaper2DPlusCombatRankedOption Baseline = VerifyRuntimeParity(TEXT("baseline"), Base);
	auto VerifyChangedField = [this, &VerifyRuntimeParity, &Baseline](
		const TCHAR* Label,
		const FPaper2DPlusCombatRuntimeContext& Context)
	{
		const FPaper2DPlusCombatRankedOption Changed = VerifyRuntimeParity(Label, Context);
		TestTrue(FString::Printf(TEXT("%s changes score or rank"), Label),
			Changed.Attack.MoveName != Baseline.Attack.MoveName || !FMath::IsNearlyEqual(Changed.Score, Baseline.Score));
	};

	FPaper2DPlusCombatRuntimeContext Changed = Base;
	Changed.DistanceToTarget = 100.0f;
	VerifyChangedField(TEXT("distance"), Changed);
	Changed = Base; Changed.SelfHealthPercent = 0.0f;
	VerifyChangedField(TEXT("self health"), Changed);
	Changed = Base; Changed.TargetHealthPercent = 0.0f;
	VerifyChangedField(TEXT("target health"), Changed);
	Changed = Base; Changed.DesiredRoleTags.Reset(); Changed.DesiredRoleTags.AddTag(CombatLabTestPrivate::AirTag());
	VerifyChangedField(TEXT("desired roles"), Changed);
	Changed = Base; Changed.TargetStateTags.Reset();
	VerifyChangedField(TEXT("target state"), Changed);
	Changed = Base; Changed.RecentMoves.Add(TEXT("Jab"));
	VerifyChangedField(TEXT("recent moves"), Changed);
	Changed = Base; Changed.RuntimeVariables.FindChecked(CombatLabTestPrivate::AggressionTag()).FloatValue = 0.0f;
	VerifyChangedField(TEXT("runtime float"), Changed);
	Changed = Base; Changed.RuntimeVariables.FindChecked(CombatLabTestPrivate::CanPunishTag()).BoolValue = false;
	VerifyChangedField(TEXT("runtime bool"), Changed);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCombatLabPresetTest,
	"Paper2DPlus.Editor.CombatProfile.CombatLab.ScenarioPresetRoundTripExcludesPlayback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCombatLabPresetTest::RunTest(const FString& Parameters)
{
	if (GEditor) GEditor->ResetTransaction(FText::FromString(TEXT("Reset Combat Lab transactions")));
	UPaper2DPlusCombatProfileAsset* Combat = CombatLabTestPrivate::MakeAsset();
	TSharedRef<FCombatProfileEditorSession> Session = MakeShared<FCombatProfileEditorSession>(Combat);
	TSharedRef<FCombatLabModel> Lab = MakeShared<FCombatLabModel>(Session);
	TSharedRef<SCombatScorePlaygroundPanel> Playground =
		SNew(SCombatScorePlaygroundPanel).Session(Session).LabModel(Lab);
	FPaper2DPlusCombatScoringProfile Alternate;
	Alternate.ProfileName = TEXT("Alternate");
	Combat->ScoringProfiles.Add(Alternate);
	Session->SetScoringProfileName(TEXT("Alternate"));

	FPaper2DPlusCombatRuntimeContext Context;
	Context.DistanceToTarget = 44.0f;
	Context.SelfHealthPercent = 0.35f;
	Context.TargetHealthPercent = 0.8f;
	Context.DesiredRoleTags.AddTag(CombatLabTestPrivate::GroundTag());
	Context.TargetStateTags.AddTag(CombatLabTestPrivate::AirTag());
	Context.RecentMoves.Add(TEXT("Guard"));
	Context.RuntimeVariables.FindOrAdd(CombatLabTestPrivate::AggressionTag()).FloatValue = 0.6f;
	Playground->SetContextForTests(Context);
	Lab->SetMove(0, TEXT("Jab"));
	Lab->SetMove(1, TEXT("Guard"));
	Lab->SetPosition(0, FVector2D(-12.0f, 3.0f));
	Lab->SetPosition(1, FVector2D(57.0f, -4.0f));
	Lab->SetSelectedParticipant(1);
	Lab->SetPlaying(0, true);
	Lab->SetFrame(0, 1);
	TestTrue(TEXT("Scenario saves transactionally"), Playground->SavePresetForTests(TEXT("CloseRange")));
	TestEqual(TEXT("One preset is stored"), Combat->ScenarioPresets.Num(), 1);
	TestEqual(TEXT("Preset stores the selected moves"), Combat->ScenarioPresets[0].AttackerMove, FName(TEXT("Jab")));
	TestEqual(TEXT("Preset stores selected participant"), Combat->ScenarioPresets[0].SelectedParticipant, 1);
	TestEqual(TEXT("Preset stores scoring profile needed to reproduce rank"),
		Combat->ScenarioPresets[0].ScoringProfileName, FName(TEXT("Alternate")));
	TestNull(TEXT("Preset schema contains no transient playback clock"),
		FPaper2DPlusCombatScenarioPreset::StaticStruct()->FindPropertyByName(TEXT("PlaybackSeconds")));

	Lab->SetMove(0, TEXT("Guard"));
	Lab->SetPosition(0, FVector2D(400.0f, 0.0f));
	Lab->SetSelectedParticipant(0);
	Session->SetScoringProfileName(NAME_None);
	Session->EditPreviewContext() = FPaper2DPlusCombatRuntimeContext();
	TestTrue(TEXT("Scenario reload succeeds"), Playground->LoadPresetForTests(TEXT("CloseRange")));
	TestEqual(TEXT("Attacker move restores"), Lab->GetParticipant(0).MoveName, FName(TEXT("Jab")));
	TestTrue(TEXT("Attacker position restores"), Lab->GetParticipant(0).Position.Equals(FVector2D(-12.0f, 3.0f)));
	TestTrue(TEXT("Defender position restores"), Lab->GetParticipant(1).Position.Equals(FVector2D(57.0f, -4.0f)));
	TestEqual(TEXT("Selected participant restores"), Lab->GetSelectedParticipant(), 1);
	TestEqual(TEXT("Scoring profile restores"), Session->GetScoringProfileName(), FName(TEXT("Alternate")));
	TestEqual(TEXT("Distance restores from the saved participant positions"),
		Session->GetPreviewContext().DistanceToTarget, 69.0f);
	TestEqual(TEXT("Self health restores"), Session->GetPreviewContext().SelfHealthPercent, 0.35f);
	TestEqual(TEXT("Target health restores"), Session->GetPreviewContext().TargetHealthPercent, 0.8f);
	TestTrue(TEXT("Desired roles restore"), Session->GetPreviewContext().DesiredRoleTags.HasTagExact(CombatLabTestPrivate::GroundTag()));
	TestTrue(TEXT("Target states restore"), Session->GetPreviewContext().TargetStateTags.HasTagExact(CombatLabTestPrivate::AirTag()));
	TestTrue(TEXT("Recent moves restore"), Session->GetPreviewContext().RecentMoves.Contains(TEXT("Guard")));
	TestEqual(TEXT("Runtime variables restore"),
		Session->GetPreviewContext().RuntimeVariables.FindChecked(CombatLabTestPrivate::AggressionTag()).FloatValue, 0.6f);
	TestFalse(TEXT("Playback remains transient and resets on load"), Lab->GetParticipant(0).bPlaying);
	TestEqual(TEXT("Frame scrub remains transient and resets on load"), Lab->GetParticipant(0).FrameIndex, 0);
	TestTrue(TEXT("One Undo removes the saved preset"), GEditor && GEditor->UndoTransaction(true));
	TestEqual(TEXT("Undo restores the pre-save preset array"), Combat->ScenarioPresets.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCombatLabPlaybackTest,
	"Paper2DPlus.Editor.CombatProfile.CombatLab.IndependentPlaybackUsesKeyFrameHitboxes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCombatLabPlaybackTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusCombatProfileAsset* Combat = CombatLabTestPrivate::MakeAsset();
	TSharedRef<FCombatProfileEditorSession> Session = MakeShared<FCombatProfileEditorSession>(Combat);
	TSharedRef<FCombatLabModel> Lab = MakeShared<FCombatLabModel>(Session);
	Lab->SetMove(0, TEXT("Jab"));
	Lab->SetMove(1, TEXT("Guard"));
	Lab->SetPosition(0, FVector2D::ZeroVector);
	Lab->SetPosition(1, FVector2D(20.0f, 0.0f));
	TestTrue(TEXT("Initial active frames overlap"), Lab->GetEvaluation().bAnyCollision);
	Lab->SetPlaying(0, true);
	Lab->Advance(0.11f);
	TestEqual(TEXT("Attacker advances independently"), Lab->GetParticipant(0).FrameIndex, 1);
	TestEqual(TEXT("Defender remains on its scrubbed frame"), Lab->GetParticipant(1).FrameIndex, 0);
	TestFalse(TEXT("Playback evaluates the attacker's empty second key frame"), Lab->GetEvaluation().bAnyCollision);
	Lab->SetPlaying(0, false);
	Lab->SetFrame(0, 0);
	Lab->SetPlaying(1, true);
	Lab->Advance(0.11f);
	TestEqual(TEXT("Defender advances independently"), Lab->GetParticipant(1).FrameIndex, 1);
	TestEqual(TEXT("Attacker remains on its scrubbed frame"), Lab->GetParticipant(0).FrameIndex, 0);
	TestFalse(TEXT("Playback evaluates the defender's empty second key frame"), Lab->GetEvaluation().bAnyCollision);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCombatLabCanvasSafetyTest,
	"Paper2DPlus.Editor.CombatProfile.CombatLab.CanvasPickAndSafeEmptyState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCombatLabCanvasSafetyTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusCombatProfileAsset* Combat = CombatLabTestPrivate::MakeAsset();
	TSharedRef<FCombatProfileEditorSession> Session = MakeShared<FCombatProfileEditorSession>(Combat);
	TSharedRef<FCombatLabModel> Lab = MakeShared<FCombatLabModel>(Session);
	Lab->SetPosition(0, FVector2D(0.0f, 0.0f));
	TSharedRef<SCombatLabCanvas> Canvas = SNew(SCombatLabCanvas).Model(Lab);
	TestTrue(TEXT("Canvas supports keyboard focus"), Canvas->SupportsKeyboardFocus());
	TestEqual(TEXT("Canvas hit testing resolves participant zero"),
		Canvas->PickParticipantForTests(FVector2D(320.0f, 252.0f), FVector2D(640.0f, 360.0f)), 0);
	TestEqual(TEXT("Far click selects no participant"),
		Canvas->PickParticipantForTests(FVector2D(10.0f, 10.0f), FVector2D(640.0f, 360.0f)), INDEX_NONE);
	Lab->SetSelectedParticipant(0);
	const FVector2D KeyboardStart = Lab->GetParticipant(0).Position;
	TestTrue(TEXT("Right arrow repositions the selected participant"),
		Canvas->OnKeyDown(
			FGeometry(),
			FKeyEvent(EKeys::Right, FModifierKeysState(), 0, false, 0, 0)).IsEventHandled());
	TestTrue(TEXT("Keyboard positioning uses the normal ten-unit step"),
		Lab->GetParticipant(0).Position.Equals(KeyboardStart + FVector2D(10.0f, 0.0f)));
	TestTrue(TEXT("Up arrow repositions vertically without changing selection"),
		Canvas->OnKeyDown(
			FGeometry(),
			FKeyEvent(EKeys::Up, FModifierKeysState(), 0, false, 0, 0)).IsEventHandled()
		&& Lab->GetParticipant(0).Position.Equals(KeyboardStart + FVector2D(10.0f, 10.0f))
		&& Lab->GetSelectedParticipant() == 0);

	UPaper2DPlusCombatProfileAsset* Empty = NewObject<UPaper2DPlusCombatProfileAsset>();
	TSharedRef<FCombatProfileEditorSession> EmptySession = MakeShared<FCombatProfileEditorSession>(Empty);
	TSharedRef<FCombatLabModel> EmptyLab = MakeShared<FCombatLabModel>(EmptySession);
	TSharedRef<SCombatScorePlaygroundPanel> EmptyPlayground =
		SNew(SCombatScorePlaygroundPanel).Session(EmptySession).LabModel(EmptyLab);
	TestFalse(TEXT("Null Character Profile yields a safe no-collision state"), EmptyLab->GetEvaluation().bAnyCollision);
	TestEqual(TEXT("Null Character Profile yields no visual boxes"),
		EmptyLab->GetEvaluation().AttackerBoxes.Num() + EmptyLab->GetEvaluation().DefenderBoxes.Num(), 0);
	TestEqual(TEXT("No viable attacks yields an empty ranked state"), EmptyPlayground->GetRankedCountForTests(), 0);
	const float DistanceBeforeInvalidLoad = EmptySession->GetPreviewContext().DistanceToTarget;
	TestFalse(TEXT("Invalid preset reference is rejected safely"), EmptyPlayground->LoadPresetForTests(TEXT("Missing")));
	TestEqual(TEXT("Invalid preset does not mutate context"),
		EmptySession->GetPreviewContext().DistanceToTarget, DistanceBeforeInvalidLoad);

	UPaper2DPlusCombatProfileAsset* Mismatch = CombatLabTestPrivate::MakeAsset();
	UPaperFlipbook* DefenderFlipbook = Mismatch->CharacterProfile->Flipbooks[1].Identity.Flipbook.Get();
	{
		FScopedFlipbookMutator Mutator(DefenderFlipbook);
		Mutator.KeyFrames.SetNum(1);
		Mutator.KeyFrames[0].FrameRun = 1;
	}
	TSharedRef<FCombatProfileEditorSession> MismatchSession = MakeShared<FCombatProfileEditorSession>(Mismatch);
	TSharedRef<FCombatLabModel> MismatchLab = MakeShared<FCombatLabModel>(MismatchSession);
	MismatchLab->SetMove(1, TEXT("Guard"));
	MismatchLab->SetFrame(1, 99);
	TestEqual(TEXT("Mismatched flipbook/profile frame counts clamp safely"), MismatchLab->GetParticipant(1).FrameIndex, 0);
	Mismatch->CharacterProfile->Flipbooks[0].Identity.Flipbook.Reset();
	MismatchLab->SetFrame(0, 1);
	MismatchLab->RefreshEvaluation();
	TestEqual(TEXT("Null flipbook falls back to profile frame count safely"), MismatchLab->GetParticipant(0).FrameIndex, 1);

	FPaper2DPlusCombatScenarioPreset InvalidReferences;
	InvalidReferences.PresetName = TEXT("InvalidReferences");
	InvalidReferences.AttackerMove = TEXT("MissingAttacker");
	InvalidReferences.DefenderMove = TEXT("MissingDefender");
	InvalidReferences.ScoringProfileName = TEXT("MissingScoringProfile");
	InvalidReferences.SelectedParticipant = 99;
	Mismatch->ScenarioPresets.Add(InvalidReferences);
	TSharedRef<SCombatScorePlaygroundPanel> MismatchPlayground =
		SNew(SCombatScorePlaygroundPanel).Session(MismatchSession).LabModel(MismatchLab);
	TestTrue(TEXT("An existing preset with stale internal references loads without crashing"),
		MismatchPlayground->LoadPresetForTests(TEXT("InvalidReferences")));
	TestFalse(TEXT("A stale attacker reference clears shared score selection"),
		MismatchSession->GetSelectedAttack().IsValid());
	TestTrue(TEXT("A stale attacker reference produces a safe empty participant"),
		MismatchLab->GetParticipant(0).MoveName.IsNone());
	TestEqual(TEXT("Out-of-range selected participant clamps safely"), MismatchLab->GetSelectedParticipant(), 1);
	TestEqual(TEXT("Missing scoring profile falls back to the Default picker entry"),
		MismatchSession->GetScoringProfileName(), FName(TEXT("Default")));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
