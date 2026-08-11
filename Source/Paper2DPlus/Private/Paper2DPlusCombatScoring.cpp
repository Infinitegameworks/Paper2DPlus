// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "Paper2DPlusCombatScoring.h"

#include "Paper2DPlusCombatProfileAsset.h"

namespace
{
	float CombatScoring_Clamp01(float Value)
	{
		return FMath::Clamp(Value, 0.0f, 1.0f);
	}

	float CombatScoring_Linear(float Value, float MinValue, float MaxValue)
	{
		if (FMath::IsNearlyEqual(MinValue, MaxValue))
		{
			return Value >= MaxValue ? 1.0f : 0.0f;
		}

		if (MinValue > MaxValue)
		{
			Swap(MinValue, MaxValue);
		}

		return CombatScoring_Clamp01((Value - MinValue) / (MaxValue - MinValue));
	}

	float CombatScoring_RangeWindow(float Value, float MinValue, float MaxValue)
	{
		if (MinValue > MaxValue)
		{
			Swap(MinValue, MaxValue);
		}

		if (Value >= MinValue && Value <= MaxValue)
		{
			return 1.0f;
		}

		const float Width = FMath::Max(1.0f, MaxValue - MinValue);
		const float OutsideDistance = Value < MinValue ? MinValue - Value : Value - MaxValue;
		return CombatScoring_Clamp01(1.0f - (OutsideDistance / Width));
	}

	void CombatScoring_AddTerm(
		FPaper2DPlusCombatScoreBreakdown& Breakdown,
		FName TermName,
		float RawValue,
		float NormalizedValue,
		float Weight,
		EPaper2DPlusCombatScoreCombineMode CombineMode)
	{
		FPaper2DPlusCombatScoreTerm Term;
		Term.TermName = TermName;
		Term.RawValue = RawValue;
		Term.NormalizedValue = CombatScoring_Clamp01(NormalizedValue);
		Term.Weight = Weight;
		Term.CombineMode = CombineMode;
		Breakdown.Terms.Add(MoveTemp(Term));
	}

	void CombatScoring_ApplyTerm(float& Score, const FPaper2DPlusCombatScoreTerm& Term)
	{
		const float ClampedWeight = FMath::Max(0.0f, Term.Weight);
		if (Term.CombineMode == EPaper2DPlusCombatScoreCombineMode::Add)
		{
			Score += Term.NormalizedValue * ClampedWeight;
			return;
		}

		const float Factor = ClampedWeight <= KINDA_SMALL_NUMBER
			? 1.0f
			: FMath::Pow(CombatScoring_Clamp01(Term.NormalizedValue), ClampedWeight);
		Score *= Factor;
	}

	bool CombatScoring_TryResolveFloatVariable(
		const UPaper2DPlusCombatProfileAsset* Profile,
		const FPaper2DPlusCombatRuntimeContext& Context,
		const FPaper2DPlusCombatAttackOption* Option,
		const FPaper2DPlusCombatTagDefaults* Defaults,
		FGameplayTag VariableTag,
		float& OutValue)
	{
		if (!Profile)
		{
			return false;
		}

		if (Profile->TryGetFloatVariable(Context.RuntimeVariables, VariableTag, OutValue))
		{
			return true;
		}

		if (Option && Profile->TryGetFloatVariable(Option->Variables, VariableTag, OutValue))
		{
			return true;
		}

		if (Defaults && Profile->TryGetFloatVariable(Defaults->Variables, VariableTag, OutValue))
		{
			return true;
		}

		return Profile->TryGetFloatVariable(Profile->GlobalVariables, VariableTag, OutValue);
	}

	bool CombatScoring_TryResolveBoolVariable(
		const UPaper2DPlusCombatProfileAsset* Profile,
		const FPaper2DPlusCombatRuntimeContext& Context,
		const FPaper2DPlusCombatAttackOption* Option,
		const FPaper2DPlusCombatTagDefaults* Defaults,
		FGameplayTag VariableTag,
		bool& bOutValue)
	{
		if (!Profile)
		{
			return false;
		}

		if (Profile->TryGetBoolVariable(Context.RuntimeVariables, VariableTag, bOutValue))
		{
			return true;
		}

		if (Option && Profile->TryGetBoolVariable(Option->Variables, VariableTag, bOutValue))
		{
			return true;
		}

		if (Defaults && Profile->TryGetBoolVariable(Defaults->Variables, VariableTag, bOutValue))
		{
			return true;
		}

		return Profile->TryGetBoolVariable(Profile->GlobalVariables, VariableTag, bOutValue);
	}

	float CombatScoring_EvaluateConsideration(
		const UPaper2DPlusCombatProfileAsset* Profile,
		const FPaper2DPlusCombatRuntimeContext& Context,
		const FPaper2DPlusCombatAttackDerivedData& Attack,
		const FPaper2DPlusCombatAttackOption* Option,
		const FPaper2DPlusCombatTagDefaults* Defaults,
		const FPaper2DPlusCombatConsideration& Consideration,
		float& OutRawValue)
	{
		OutRawValue = 0.0f;

		switch (Consideration.Source)
		{
		case EPaper2DPlusCombatConsiderationSource::DistanceToTarget:
			OutRawValue = Context.DistanceToTarget;
			break;
		case EPaper2DPlusCombatConsiderationSource::SelfHealthPercent:
			OutRawValue = Context.SelfHealthPercent;
			break;
		case EPaper2DPlusCombatConsiderationSource::TargetHealthPercent:
			OutRawValue = Context.TargetHealthPercent;
			break;
		case EPaper2DPlusCombatConsiderationSource::DesiredRoleTags:
			if (Context.DesiredRoleTags.IsEmpty())
			{
				OutRawValue = 1.0f;
				return 1.0f;
			}
			OutRawValue = Attack.RoleTags.HasAny(Context.DesiredRoleTags) ? 1.0f : 0.0f;
			return OutRawValue;
		case EPaper2DPlusCombatConsiderationSource::TargetStateTags:
			if (Consideration.RequiredTags.IsEmpty())
			{
				OutRawValue = 1.0f;
				return 1.0f;
			}
			if (Consideration.Operation == EPaper2DPlusCombatConsiderationOp::TagAll)
			{
				OutRawValue = Context.TargetStateTags.HasAll(Consideration.RequiredTags) ? 1.0f : 0.0f;
			}
			else
			{
				OutRawValue = Context.TargetStateTags.HasAny(Consideration.RequiredTags) ? 1.0f : 0.0f;
			}
			return OutRawValue;
		case EPaper2DPlusCombatConsiderationSource::CustomBool:
		{
			bool bValue = false;
			if (!CombatScoring_TryResolveBoolVariable(Profile, Context, Option, Defaults, Consideration.VariableTag, bValue))
			{
				OutRawValue = 0.0f;
				return 0.0f;
			}
			OutRawValue = bValue ? 1.0f : 0.0f;
			return bValue == Consideration.bExpectedBool ? 1.0f : 0.0f;
		}
		case EPaper2DPlusCombatConsiderationSource::CustomFloat:
			if (!CombatScoring_TryResolveFloatVariable(Profile, Context, Option, Defaults, Consideration.VariableTag, OutRawValue))
			{
				return 0.0f;
			}
			break;
		default:
			return 0.0f;
		}

		switch (Consideration.Operation)
		{
		case EPaper2DPlusCombatConsiderationOp::Linear:
			return CombatScoring_Linear(OutRawValue, Consideration.MinValue, Consideration.MaxValue);
		case EPaper2DPlusCombatConsiderationOp::LinearInverse:
			return 1.0f - CombatScoring_Linear(OutRawValue, Consideration.MinValue, Consideration.MaxValue);
		case EPaper2DPlusCombatConsiderationOp::BoolEquals:
			return (OutRawValue >= 0.5f) == Consideration.bExpectedBool ? 1.0f : 0.0f;
		case EPaper2DPlusCombatConsiderationOp::TagAny:
		case EPaper2DPlusCombatConsiderationOp::TagAll:
			return OutRawValue >= 0.5f ? 1.0f : 0.0f;
		case EPaper2DPlusCombatConsiderationOp::RangeWindow:
		default:
			return CombatScoring_RangeWindow(OutRawValue, Consideration.MinValue, Consideration.MaxValue);
		}
	}

	void CombatScoring_ApplyConsideration(
		const UPaper2DPlusCombatProfileAsset* Profile,
		const FPaper2DPlusCombatRuntimeContext& Context,
		const FPaper2DPlusCombatAttackDerivedData& Attack,
		const FPaper2DPlusCombatAttackOption* Option,
		const FPaper2DPlusCombatTagDefaults* Defaults,
		const FPaper2DPlusCombatConsideration& Consideration,
		float& Score,
		FPaper2DPlusCombatScoreBreakdown& Breakdown)
	{
		float RawValue = 0.0f;
		const float Normalized = CombatScoring_EvaluateConsideration(Profile, Context, Attack, Option, Defaults, Consideration, RawValue);
		const FName TermName = Consideration.ConsiderationName.IsNone() ? Consideration.VariableTag.GetTagName() : Consideration.ConsiderationName;

		CombatScoring_AddTerm(
			Breakdown,
			TermName.IsNone() ? TEXT("Consideration") : TermName,
			RawValue,
			Normalized,
			Consideration.Weight,
			Consideration.CombineMode);
		CombatScoring_ApplyTerm(Score, Breakdown.Terms.Last());
	}
}

void FPaper2DPlusCombatScoring::ScoreAttackOptions(
	const UPaper2DPlusCombatProfileAsset* CombatProfile,
	const FPaper2DPlusCombatRuntimeContext& Context,
	TArray<FPaper2DPlusCombatRankedOption>& OutRankedOptions,
	FName ScoringProfileName)
{
	OutRankedOptions.Reset();
	if (!CombatProfile)
	{
		return;
	}

	TArray<FPaper2DPlusCombatAttackDerivedData> Catalog;
	CombatProfile->BuildAttackCatalog(Catalog);
	if (Catalog.Num() == 0)
	{
		return;
	}

	const FPaper2DPlusCombatScoringProfile* ScoringProfile = CombatProfile->FindScoringProfile(ScoringProfileName);

	for (const FPaper2DPlusCombatAttackDerivedData& Attack : Catalog)
	{
		const FPaper2DPlusCombatAttackOption* Option = CombatProfile->FindAttackOption(Attack.MoveName);
		const FPaper2DPlusCombatTagDefaults* Defaults = CombatProfile->FindTagDefaults(Attack.AttackTag);

		FPaper2DPlusCombatRankedOption Ranked;
		Ranked.Attack = Attack;
		Ranked.Breakdown.MoveName = Attack.MoveName;

		float Score = FMath::Max(0.0f, Attack.BaseWeight);
		CombatScoring_AddTerm(Ranked.Breakdown, TEXT("BaseWeight"), Attack.BaseWeight, Attack.BaseWeight > 0.0f ? 1.0f : 0.0f, 1.0f, EPaper2DPlusCombatScoreCombineMode::Add);

		const float DistanceFit = CombatScoring_RangeWindow(Context.DistanceToTarget, Attack.PreferredRangeLocal.X, Attack.PreferredRangeLocal.Y);
		CombatScoring_AddTerm(Ranked.Breakdown, TEXT("DistanceFit"), Context.DistanceToTarget, DistanceFit, 1.0f, EPaper2DPlusCombatScoreCombineMode::Multiply);
		CombatScoring_ApplyTerm(Score, Ranked.Breakdown.Terms.Last());

		if (!Context.DesiredRoleTags.IsEmpty())
		{
			const float RoleFit = Attack.RoleTags.HasAny(Context.DesiredRoleTags) ? 1.0f : 0.0f;
			CombatScoring_AddTerm(Ranked.Breakdown, TEXT("DesiredRoleFit"), RoleFit, RoleFit, 1.0f, EPaper2DPlusCombatScoreCombineMode::Multiply);
			CombatScoring_ApplyTerm(Score, Ranked.Breakdown.Terms.Last());
		}

		if (Context.RecentMoves.Contains(Attack.MoveName))
		{
			CombatScoring_AddTerm(Ranked.Breakdown, TEXT("RecentMovePenalty"), 1.0f, 0.5f, 1.0f, EPaper2DPlusCombatScoreCombineMode::Multiply);
			CombatScoring_ApplyTerm(Score, Ranked.Breakdown.Terms.Last());
		}

		if (ScoringProfile)
		{
			for (const FPaper2DPlusCombatConsideration& Consideration : ScoringProfile->GlobalConsiderations)
			{
				CombatScoring_ApplyConsideration(CombatProfile, Context, Attack, Option, Defaults, Consideration, Score, Ranked.Breakdown);
			}
		}

		if (Defaults)
		{
			for (const FPaper2DPlusCombatConsideration& Consideration : Defaults->Considerations)
			{
				CombatScoring_ApplyConsideration(CombatProfile, Context, Attack, Option, Defaults, Consideration, Score, Ranked.Breakdown);
			}
		}

		if (Option)
		{
			for (const FPaper2DPlusCombatConsideration& Consideration : Option->Considerations)
			{
				CombatScoring_ApplyConsideration(CombatProfile, Context, Attack, Option, Defaults, Consideration, Score, Ranked.Breakdown);
			}
		}

		Ranked.Score = FMath::Max(0.0f, Score);
		Ranked.Breakdown.FinalScore = Ranked.Score;
		OutRankedOptions.Add(MoveTemp(Ranked));
	}

	OutRankedOptions.Sort([](const FPaper2DPlusCombatRankedOption& Left, const FPaper2DPlusCombatRankedOption& Right)
	{
		if (FMath::IsNearlyEqual(Left.Score, Right.Score))
		{
			return Left.Attack.MoveName.LexicalLess(Right.Attack.MoveName);
		}
		return Left.Score > Right.Score;
	});
}

bool FPaper2DPlusCombatScoring::BuildDecision(
	const UPaper2DPlusCombatProfileAsset* CombatProfile,
	const FPaper2DPlusCombatRuntimeContext& Context,
	FPaper2DPlusCombatDecision& OutDecision,
	FName ScoringProfileName)
{
	OutDecision = FPaper2DPlusCombatDecision();
	OutDecision.CurrentDistance = Context.DistanceToTarget;

	TArray<FPaper2DPlusCombatRankedOption> RankedOptions;
	ScoreAttackOptions(CombatProfile, Context, RankedOptions, ScoringProfileName);
	OutDecision.RankedOptions = RankedOptions;

	if (RankedOptions.Num() == 0)
	{
		return false;
	}

	const FPaper2DPlusCombatRankedOption& Best = RankedOptions[0];
	const FPaper2DPlusCombatScoringProfile* ScoringProfile = CombatProfile ? CombatProfile->FindScoringProfile(ScoringProfileName) : nullptr;
	const float MinimumScore = ScoringProfile ? ScoringProfile->MinimumViableScore : 0.01f;

	OutDecision.bHasGoodAttack = Best.Score >= MinimumScore;
	OutDecision.BestMove = Best.Attack.MoveName;
	OutDecision.BestAttackTag = Best.Attack.AttackTag;
	OutDecision.DesiredRangeLocal = Best.Attack.PreferredRangeLocal;
	OutDecision.BestScore = Best.Score;
	OutDecision.BestBreakdown = Best.Breakdown;
	return OutDecision.bHasGoodAttack;
}

bool FPaper2DPlusCombatScoring::PickWeightedAttack(
	const TArray<FPaper2DPlusCombatRankedOption>& RankedOptions,
	FRandomStream& RandomStream,
	FPaper2DPlusCombatRankedOption& OutPickedOption)
{
	float TotalWeight = 0.0f;
	for (const FPaper2DPlusCombatRankedOption& Option : RankedOptions)
	{
		TotalWeight += FMath::Max(0.0f, Option.Score);
	}

	if (TotalWeight <= KINDA_SMALL_NUMBER)
	{
		return false;
	}

	const float Pick = RandomStream.FRandRange(0.0f, TotalWeight);
	float Running = 0.0f;
	for (const FPaper2DPlusCombatRankedOption& Option : RankedOptions)
	{
		Running += FMath::Max(0.0f, Option.Score);
		if (Pick <= Running)
		{
			OutPickedOption = Option;
			return true;
		}
	}

	OutPickedOption = RankedOptions.Last();
	return true;
}
