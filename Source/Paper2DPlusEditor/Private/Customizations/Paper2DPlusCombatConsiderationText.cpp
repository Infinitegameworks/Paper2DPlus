// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "Customizations/Paper2DPlusCombatConsiderationText.h"
#include "Paper2DPlusCombatProfileTypes.h"
#include "GameplayTagContainer.h"

#define LOCTEXT_NAMESPACE "CombatConsiderationText"

namespace Paper2DPlusCombatText
{
	static FText VariableName(const FPaper2DPlusCombatConsideration& C)
	{
		if (C.VariableTag.IsValid())
		{
			FString Name = C.VariableTag.GetTagName().ToString();
			Name.ReplaceInline(TEXT("Paper2DPlus.Combat.Var."), TEXT(""));
			return FText::Format(LOCTEXT("VarFmt", "variable '{0}'"), FText::FromString(Name));
		}
		return LOCTEXT("VarUnset", "variable (unset)");
	}

	static FText NumericNoun(const FPaper2DPlusCombatConsideration& C)
	{
		switch (C.Source)
		{
		case EPaper2DPlusCombatConsiderationSource::DistanceToTarget:    return LOCTEXT("SrcDistance", "distance to target");
		case EPaper2DPlusCombatConsiderationSource::SelfHealthPercent:   return LOCTEXT("SrcSelfHp", "my health %");
		case EPaper2DPlusCombatConsiderationSource::TargetHealthPercent: return LOCTEXT("SrcTgtHp", "target health %");
		case EPaper2DPlusCombatConsiderationSource::CustomFloat:         return VariableName(C);
		default:                                                          return LOCTEXT("SrcUnknown", "value");
		}
	}

	FText SummarizeConsideration(const FPaper2DPlusCombatConsideration& C)
	{
		auto Num = [](float V) { return FText::FromString(FString::SanitizeFloat(V, 0)); };
		const FText Effect = (C.CombineMode == EPaper2DPlusCombatScoreCombineMode::Multiply)
			? FText::Format(LOCTEXT("EffectMul", "multiply score (x{0})"), Num(C.Weight))
			: FText::Format(LOCTEXT("EffectAdd", "add to score (+{0})"), Num(C.Weight));

		// Source-specific phrasing — must match how CombatScoring_EvaluateConsideration actually reads
		// each source's operands (DesiredRoleTags uses neither RequiredTags nor Operation; TargetStateTags
		// uses RequiredTags with op only choosing All-vs-Any; CustomBool uses bExpectedBool).
		switch (C.Source)
		{
		case EPaper2DPlusCombatConsiderationSource::DesiredRoleTags:
			return FText::Format(LOCTEXT("SumRole", "Prefer attacks matching my desired role, {0}"), Effect);

		case EPaper2DPlusCombatConsiderationSource::TargetStateTags:
		{
			const bool bAll = (C.Operation == EPaper2DPlusCombatConsiderationOp::TagAll);
			const FText Tags = C.RequiredTags.IsEmpty()
				? LOCTEXT("AnyState", "(no tags set — always true)")
				: FText::FromString(C.RequiredTags.ToStringSimple());
			return FText::Format(
				bAll ? LOCTEXT("SumTgtAll", "When target has ALL of [{0}], {1}")
				     : LOCTEXT("SumTgtAny", "When target has any of [{0}], {1}"),
				Tags, Effect);
		}

		case EPaper2DPlusCombatConsiderationSource::CustomBool:
			return FText::Format(
				C.bExpectedBool ? LOCTEXT("SumBoolTrue", "When {0} is true, {1}")
				                : LOCTEXT("SumBoolFalse", "When {0} is false, {1}"),
				VariableName(C), Effect);

		default:
			break; // numeric sources + CustomFloat use the operation clause below
		}

		const FText Noun = NumericNoun(C);
		FText Clause;
		switch (C.Operation)
		{
		case EPaper2DPlusCombatConsiderationOp::RangeWindow:
			Clause = FText::Format(LOCTEXT("ClauseRange", "is within {0}-{1}"), Num(C.MinValue), Num(C.MaxValue)); break;
		case EPaper2DPlusCombatConsiderationOp::Linear:
			Clause = FText::Format(LOCTEXT("ClauseLinear", "rises from {0} to {1}"), Num(C.MinValue), Num(C.MaxValue)); break;
		case EPaper2DPlusCombatConsiderationOp::LinearInverse:
			Clause = FText::Format(LOCTEXT("ClauseLinearInv", "falls from {0} to {1}"), Num(C.MinValue), Num(C.MaxValue)); break;
		case EPaper2DPlusCombatConsiderationOp::BoolEquals:
			Clause = C.bExpectedBool ? LOCTEXT("ClauseBoolTrue", "is set") : LOCTEXT("ClauseBoolFalse", "is not set"); break;
		case EPaper2DPlusCombatConsiderationOp::TagAny:
		case EPaper2DPlusCombatConsiderationOp::TagAll:
			Clause = LOCTEXT("ClauseThreshold", "is set"); break; // generic tag ops just threshold a numeric value
		default: break;
		}
		return FText::Format(LOCTEXT("SummaryFmt", "When {0} {1}, {2}"), Noun, Clause, Effect);
	}
}

#undef LOCTEXT_NAMESPACE
