// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "Paper2DPlusCombatProfileComponent.h"

#include "GameFramework/Actor.h"

UPaper2DPlusCombatProfileComponent::UPaper2DPlusCombatProfileComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	CombatRandomStream.Initialize(RandomSeed);
}

void UPaper2DPlusCombatProfileComponent::SetCombatProfile(UPaper2DPlusCombatProfileAsset* NewCombatProfile)
{
	CombatProfile = NewCombatProfile;
}

void UPaper2DPlusCombatProfileComponent::ResetCombatRandomStream(int32 NewSeed)
{
	RandomSeed = NewSeed;
	CombatRandomStream.Initialize(RandomSeed);
}

void UPaper2DPlusCombatProfileComponent::ScoreAttackOptions(
	const FPaper2DPlusCombatRuntimeContext& Context,
	TArray<FPaper2DPlusCombatRankedOption>& OutRankedOptions,
	FName ScoringProfileName) const
{
	OutRankedOptions.Reset();
	if (CombatProfile)
	{
		CombatProfile->ScoreAttackOptions(Context, OutRankedOptions, ResolveScoringProfileName(ScoringProfileName));
	}
}

bool UPaper2DPlusCombatProfileComponent::GetCombatDecision(
	const FPaper2DPlusCombatRuntimeContext& Context,
	FPaper2DPlusCombatDecision& OutDecision,
	FName ScoringProfileName)
{
	OutDecision = FPaper2DPlusCombatDecision();
	if (!CombatProfile)
	{
		return false;
	}

	const bool bHasDecision = CombatProfile->GetCombatDecision(Context, OutDecision, ResolveScoringProfileName(ScoringProfileName));
	OnCombatDecisionUpdated.Broadcast(OutDecision);
	return bHasDecision;
}

bool UPaper2DPlusCombatProfileComponent::PickWeightedCombatAttack(
	const FPaper2DPlusCombatRuntimeContext& Context,
	FPaper2DPlusCombatDecision& OutDecision,
	FName ScoringProfileName)
{
	OutDecision = FPaper2DPlusCombatDecision();
	if (!CombatProfile)
	{
		return false;
	}

	const bool bPicked = CombatProfile->PickWeightedCombatAttack(Context, CombatRandomStream, OutDecision, ResolveScoringProfileName(ScoringProfileName));
	OnCombatDecisionUpdated.Broadcast(OutDecision);
	return bPicked;
}

FPaper2DPlusCombatRuntimeContext UPaper2DPlusCombatProfileComponent::MakeTargetContext(
	AActor* TargetActor,
	const FGameplayTagContainer& DesiredRoleTags,
	const FGameplayTagContainer& TargetStateTags,
	float SelfHealthPercent,
	float TargetHealthPercent) const
{
	FPaper2DPlusCombatRuntimeContext Context;
	Context.SelfHealthPercent = FMath::Clamp(SelfHealthPercent, 0.0f, 1.0f);
	Context.TargetHealthPercent = FMath::Clamp(TargetHealthPercent, 0.0f, 1.0f);
	Context.DesiredRoleTags = DesiredRoleTags;
	Context.TargetStateTags = TargetStateTags;

	const AActor* OwnerActor = GetOwner();
	if (OwnerActor && TargetActor)
	{
		const FVector Delta = TargetActor->GetActorLocation() - OwnerActor->GetActorLocation();
		Context.DistanceToTarget = bUseHorizontalDistanceOnly ? FMath::Abs(Delta.X) : Delta.Size();
	}

	return Context;
}

bool UPaper2DPlusCombatProfileComponent::GetCombatDecisionForTarget(
	AActor* TargetActor,
	FPaper2DPlusCombatDecision& OutDecision,
	const FGameplayTagContainer& DesiredRoleTags,
	const FGameplayTagContainer& TargetStateTags,
	float SelfHealthPercent,
	float TargetHealthPercent,
	FName ScoringProfileName)
{
	const FPaper2DPlusCombatRuntimeContext Context = MakeTargetContext(
		TargetActor,
		DesiredRoleTags,
		TargetStateTags,
		SelfHealthPercent,
		TargetHealthPercent);
	return GetCombatDecision(Context, OutDecision, ScoringProfileName);
}

FName UPaper2DPlusCombatProfileComponent::ResolveScoringProfileName(FName ScoringProfileName) const
{
	return ScoringProfileName.IsNone() ? DefaultScoringProfileName : ScoringProfileName;
}
