// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#if WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "Paper2DPlusCombatProfileAsset.h"
#include "Paper2DPlusCombatScoring.h"
#include "Paper2DPlusCombatTags.h"

namespace
{
	FHitboxData CombatScoringTest_Attack(int32 X, int32 W, int32 Damage)
	{
		FHitboxData Hitbox;
		Hitbox.Type = EHitboxType::Attack;
		Hitbox.X = X;
		Hitbox.Y = -5;
		Hitbox.Width = W;
		Hitbox.Height = 10;
		Hitbox.Damage = Damage;
		return Hitbox;
	}

	FGameplayTag CombatScoringTest_AttackTag()
	{
		return FGameplayTag::RequestGameplayTag(FName("PlayerStates.Attacking.GroundAttack"), false);
	}

	UPaper2DPlusCombatProfileAsset* CombatScoringTest_MakeProfile()
	{
		UPaper2DPlusCharacterProfileAsset* CharacterProfile = NewObject<UPaper2DPlusCharacterProfileAsset>();
		{
			FFlipbookProfileEntry Close;
			Close.Identity.FlipbookName = TEXT("CloseSlash");
			Close.CombatData.Frames.SetNum(1);
			Close.CombatData.Frames[0].Hitboxes.Add(CombatScoringTest_Attack(10, 20, 5));
			CharacterProfile->Flipbooks.Add(Close);
		}
		{
			FFlipbookProfileEntry Far;
			Far.Identity.FlipbookName = TEXT("FarThrust");
			Far.CombatData.Frames.SetNum(1);
			Far.CombatData.Frames[0].Hitboxes.Add(CombatScoringTest_Attack(80, 20, 8));
			CharacterProfile->Flipbooks.Add(Far);
		}

		const FGameplayTag AttackTag = CombatScoringTest_AttackTag();
		FFlipbookTagMapping& AttackMapping = CharacterProfile->TagMappings.FindOrAdd(AttackTag);
		AttackMapping.Entries.Add(FFlipbookTagMappingEntry(TEXT("CloseSlash")));
		AttackMapping.Entries.Add(FFlipbookTagMappingEntry(TEXT("FarThrust")));

		UPaper2DPlusCombatProfileAsset* CombatProfile = NewObject<UPaper2DPlusCombatProfileAsset>();
		CombatProfile->CharacterProfile = CharacterProfile;

		FPaper2DPlusCombatScoringProfile ScoringProfile;
		ScoringProfile.ProfileName = TEXT("Default");
		ScoringProfile.MinimumViableScore = 0.01f;
		CombatProfile->ScoringProfiles.Add(ScoringProfile);

		return CombatProfile;
	}

	UPaper2DPlusCombatProfileAsset* CombatScoringTest_MakeRootMotionProfile()
	{
		UPaper2DPlusCharacterProfileAsset* CharacterProfile = NewObject<UPaper2DPlusCharacterProfileAsset>();
		{
			FFlipbookProfileEntry Close;
			Close.Identity.FlipbookName = TEXT("CloseSlash");
			Close.CombatData.Frames.SetNum(1);
			Close.CombatData.Frames[0].Hitboxes.Add(CombatScoringTest_Attack(10, 20, 5));
			CharacterProfile->Flipbooks.Add(Close);
		}
		{
			FFlipbookProfileEntry Lunge;
			Lunge.Identity.FlipbookName = TEXT("RootMotionLunge");
			Lunge.CombatData.Frames.SetNum(2);
			Lunge.CombatData.Frames[1].Hitboxes.Add(CombatScoringTest_Attack(20, 20, 7));
			Lunge.MotionData.RootMotion.SetNum(2);
			Lunge.MotionData.RootMotion[0].Position = FVector2D::ZeroVector;
			Lunge.MotionData.RootMotion[1].Position = FVector2D(70.0f, 0.0f);
			CharacterProfile->Flipbooks.Add(Lunge);
		}

		const FGameplayTag AttackTag = CombatScoringTest_AttackTag();
		FFlipbookTagMapping& AttackMapping = CharacterProfile->TagMappings.FindOrAdd(AttackTag);
		AttackMapping.Entries.Add(FFlipbookTagMappingEntry(TEXT("CloseSlash")));
		AttackMapping.Entries.Add(FFlipbookTagMappingEntry(TEXT("RootMotionLunge")));

		UPaper2DPlusCombatProfileAsset* CombatProfile = NewObject<UPaper2DPlusCombatProfileAsset>();
		CombatProfile->CharacterProfile = CharacterProfile;

		FPaper2DPlusCombatScoringProfile ScoringProfile;
		ScoringProfile.ProfileName = TEXT("Default");
		ScoringProfile.MinimumViableScore = 0.01f;
		CombatProfile->ScoringProfiles.Add(ScoringProfile);

		return CombatProfile;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCombatScoringDistanceRanksMovesTest,
	"Paper2DPlus.CombatProfile.Scoring.DistanceRanksMoves",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCombatScoringDistanceRanksMovesTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusCombatProfileAsset* CombatProfile = CombatScoringTest_MakeProfile();

	FPaper2DPlusCombatRuntimeContext Context;
	Context.DistanceToTarget = 20.0f;

	TArray<FPaper2DPlusCombatRankedOption> Ranked;
	CombatProfile->ScoreAttackOptions(Context, Ranked);
	TestEqual(TEXT("Both attack moves score"), Ranked.Num(), 2);
	TestEqual(TEXT("Close move wins at close distance"), Ranked[0].Attack.MoveName, FName(TEXT("CloseSlash")));
	TestTrue(TEXT("Close score is above far score"), Ranked[0].Score > Ranked[1].Score);

	Context.DistanceToTarget = 90.0f;
	CombatProfile->ScoreAttackOptions(Context, Ranked);
	TestEqual(TEXT("Far move wins at far distance"), Ranked[0].Attack.MoveName, FName(TEXT("FarThrust")));

	FPaper2DPlusCombatDecision Decision;
	TestTrue(TEXT("Decision succeeds"), CombatProfile->GetCombatDecision(Context, Decision));
	TestTrue(TEXT("Decision has good attack"), Decision.bHasGoodAttack);
	TestEqual(TEXT("Decision best move"), Decision.BestMove, FName(TEXT("FarThrust")));
	TestTrue(TEXT("Decision includes breakdown terms"), Decision.BestBreakdown.Terms.Num() >= 2);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCombatScoringRootMotionDistanceTest,
	"Paper2DPlus.CombatProfile.Scoring.RootMotionExtendsDistance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCombatScoringRootMotionDistanceTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusCombatProfileAsset* CombatProfile = CombatScoringTest_MakeRootMotionProfile();

	TArray<FPaper2DPlusCombatAttackDerivedData> Catalog;
	CombatProfile->BuildAttackCatalog(Catalog);
	const FPaper2DPlusCombatAttackDerivedData* Lunge = Catalog.FindByPredicate([](const FPaper2DPlusCombatAttackDerivedData& Row)
	{
		return Row.MoveName == TEXT("RootMotionLunge");
	});
	TestNotNull(TEXT("Catalog includes root-motion lunge"), Lunge);
	if (!Lunge)
	{
		return false;
	}

	TestTrue(TEXT("Raw hitbox range stays close"), FMath::IsNearlyEqual(Lunge->HitboxForwardRangeLocal.Y, 40.0f));
	TestTrue(TEXT("Effective lunge range includes root motion"), FMath::IsNearlyEqual(Lunge->ForwardRangeLocal.X, 90.0f));
	TestTrue(TEXT("Effective lunge range max includes root motion"), FMath::IsNearlyEqual(Lunge->ForwardRangeLocal.Y, 110.0f));

	FPaper2DPlusCombatRuntimeContext Context;
	Context.DistanceToTarget = 95.0f;

	TArray<FPaper2DPlusCombatRankedOption> Ranked;
	CombatProfile->ScoreAttackOptions(Context, Ranked);
	TestEqual(TEXT("Both root-motion profile moves score"), Ranked.Num(), 2);
	TestEqual(TEXT("Lunge wins at its effective root-motion distance"), Ranked[0].Attack.MoveName, FName(TEXT("RootMotionLunge")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCombatScoringConsiderationsTest,
	"Paper2DPlus.CombatProfile.Scoring.CustomConsiderations",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCombatScoringConsiderationsTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusCombatProfileAsset* CombatProfile = CombatScoringTest_MakeProfile();

	FPaper2DPlusCombatVariableDefinition Aggression;
	Aggression.VariableTag = Paper2DPlusCombatTags::Var_Aggression;
	Aggression.Type = EPaper2DPlusCombatVariableType::Float;
	CombatProfile->VariableDefinitions.Add(Aggression);

	FPaper2DPlusCombatAttackOption FarOption;
	FarOption.MoveName = TEXT("FarThrust");
	FarOption.BaseWeight = 1.0f;
	FPaper2DPlusCombatConsideration NeedsAggression;
	NeedsAggression.ConsiderationName = TEXT("NeedsAggression");
	NeedsAggression.Source = EPaper2DPlusCombatConsiderationSource::CustomFloat;
	NeedsAggression.Operation = EPaper2DPlusCombatConsiderationOp::Linear;
	NeedsAggression.VariableTag = Paper2DPlusCombatTags::Var_Aggression;
	NeedsAggression.MinValue = 0.0f;
	NeedsAggression.MaxValue = 1.0f;
	FarOption.Considerations.Add(NeedsAggression);
	CombatProfile->AttackOptions.Add(FarOption);
	CombatProfile->RebuildVariableBags();

	FPaper2DPlusCombatAttackOption* MutableFar = CombatProfile->AttackOptions.FindByPredicate([](const FPaper2DPlusCombatAttackOption& Option)
	{
		return Option.MoveName == TEXT("FarThrust");
	});
	TestNotNull(TEXT("Mutable far option exists"), MutableFar);
	if (!MutableFar)
	{
		return false;
	}

	MutableFar->Variables.FindOrAdd(Paper2DPlusCombatTags::Var_Aggression).FloatValue = 0.0f;

	FPaper2DPlusCombatRuntimeContext Context;
	Context.DistanceToTarget = 90.0f;

	TArray<FPaper2DPlusCombatRankedOption> Ranked;
	CombatProfile->ScoreAttackOptions(Context, Ranked);
	TestEqual(TEXT("Close wins when far move custom consideration is zero"), Ranked[0].Attack.MoveName, FName(TEXT("CloseSlash")));

	MutableFar->Variables.FindOrAdd(Paper2DPlusCombatTags::Var_Aggression).FloatValue = 1.0f;
	CombatProfile->ScoreAttackOptions(Context, Ranked);
	TestEqual(TEXT("Far wins when custom consideration is high"), Ranked[0].Attack.MoveName, FName(TEXT("FarThrust")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCombatScoringWeightedPickTest,
	"Paper2DPlus.CombatProfile.Scoring.WeightedPickDeterministic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCombatScoringWeightedPickTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusCombatProfileAsset* CombatProfile = CombatScoringTest_MakeProfile();

	FPaper2DPlusCombatRuntimeContext Context;
	Context.DistanceToTarget = 90.0f;

	FRandomStream RandomA(42);
	FRandomStream RandomB(42);

	FPaper2DPlusCombatDecision DecisionA;
	FPaper2DPlusCombatDecision DecisionB;
	TestTrue(TEXT("Weighted pick A succeeds"), CombatProfile->PickWeightedCombatAttack(Context, RandomA, DecisionA));
	TestTrue(TEXT("Weighted pick B succeeds"), CombatProfile->PickWeightedCombatAttack(Context, RandomB, DecisionB));
	TestEqual(TEXT("Same seed picks same move"), DecisionA.BestMove, DecisionB.BestMove);
	TestTrue(TEXT("Weighted decision carries ranked options"), DecisionA.RankedOptions.Num() == 2);

	TArray<FPaper2DPlusCombatRankedOption> Empty;
	FPaper2DPlusCombatRankedOption Picked;
	FRandomStream RandomC(7);
	TestFalse(TEXT("Empty weighted pick fails"), FPaper2DPlusCombatScoring::PickWeightedAttack(Empty, RandomC, Picked));

	return true;
}

#endif // WITH_EDITOR
