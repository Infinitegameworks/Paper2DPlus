// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Paper2DPlusCombatProfileTypes.h"

class UPaper2DPlusCombatProfileAsset;

class PAPER2DPLUS_API FPaper2DPlusCombatScoring
{
public:
	static void ScoreAttackOptions(
		const UPaper2DPlusCombatProfileAsset* CombatProfile,
		const FPaper2DPlusCombatRuntimeContext& Context,
		TArray<FPaper2DPlusCombatRankedOption>& OutRankedOptions,
		FName ScoringProfileName = NAME_None);

	static bool BuildDecision(
		const UPaper2DPlusCombatProfileAsset* CombatProfile,
		const FPaper2DPlusCombatRuntimeContext& Context,
		FPaper2DPlusCombatDecision& OutDecision,
		FName ScoringProfileName = NAME_None);

	static bool PickWeightedAttack(
		const TArray<FPaper2DPlusCombatRankedOption>& RankedOptions,
		FRandomStream& RandomStream,
		FPaper2DPlusCombatRankedOption& OutPickedOption);
};
