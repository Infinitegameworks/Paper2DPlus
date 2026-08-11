// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Paper2DPlusCombatProfileAsset.h"
#include "Paper2DPlusCombatProfileComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPaper2DPlusCombatDecisionUpdated, const FPaper2DPlusCombatDecision&, Decision);

UCLASS(ClassGroup=(Paper2DPlus), meta=(BlueprintSpawnableComponent, DisplayName="Combat Profile Component"))
class PAPER2DPLUS_API UPaper2DPlusCombatProfileComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPaper2DPlusCombatProfileComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetCombatProfile, Category = "Paper2DPlus|Combat Profile")
	TObjectPtr<UPaper2DPlusCombatProfileAsset> CombatProfile = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paper2DPlus|Combat Profile")
	FName DefaultScoringProfileName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paper2DPlus|Combat Profile")
	bool bUseHorizontalDistanceOnly = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paper2DPlus|Combat Profile")
	int32 RandomSeed = 1337;

	UPROPERTY(BlueprintAssignable, Category = "Paper2DPlus|Combat Profile")
	FOnPaper2DPlusCombatDecisionUpdated OnCombatDecisionUpdated;

	UFUNCTION(BlueprintCallable, Category = "Paper2DPlus|Combat Profile")
	void SetCombatProfile(UPaper2DPlusCombatProfileAsset* NewCombatProfile);

	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Combat Profile")
	UPaper2DPlusCombatProfileAsset* GetCombatProfile() const { return CombatProfile; }

	UFUNCTION(BlueprintCallable, Category = "Paper2DPlus|Combat Profile")
	void ResetCombatRandomStream(int32 NewSeed);

	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Combat Profile")
	void ScoreAttackOptions(const FPaper2DPlusCombatRuntimeContext& Context, TArray<FPaper2DPlusCombatRankedOption>& OutRankedOptions, FName ScoringProfileName = NAME_None) const;

	UFUNCTION(BlueprintCallable, Category = "Paper2DPlus|Combat Profile")
	bool GetCombatDecision(const FPaper2DPlusCombatRuntimeContext& Context, FPaper2DPlusCombatDecision& OutDecision, FName ScoringProfileName = NAME_None);

	UFUNCTION(BlueprintCallable, Category = "Paper2DPlus|Combat Profile")
	bool PickWeightedCombatAttack(const FPaper2DPlusCombatRuntimeContext& Context, FPaper2DPlusCombatDecision& OutDecision, FName ScoringProfileName = NAME_None);

	UFUNCTION(BlueprintCallable, Category = "Paper2DPlus|Combat Profile", meta = (AutoCreateRefTerm = "DesiredRoleTags,TargetStateTags"))
	FPaper2DPlusCombatRuntimeContext MakeTargetContext(
		AActor* TargetActor,
		const FGameplayTagContainer& DesiredRoleTags,
		const FGameplayTagContainer& TargetStateTags,
		float SelfHealthPercent = 1.0f,
		float TargetHealthPercent = 1.0f) const;

	UFUNCTION(BlueprintCallable, Category = "Paper2DPlus|Combat Profile", meta = (AutoCreateRefTerm = "DesiredRoleTags,TargetStateTags"))
	bool GetCombatDecisionForTarget(
		AActor* TargetActor,
		FPaper2DPlusCombatDecision& OutDecision,
		const FGameplayTagContainer& DesiredRoleTags,
		const FGameplayTagContainer& TargetStateTags,
		float SelfHealthPercent = 1.0f,
		float TargetHealthPercent = 1.0f,
		FName ScoringProfileName = NAME_None);

private:
	FName ResolveScoringProfileName(FName ScoringProfileName) const;

	UPROPERTY(Transient)
	FRandomStream CombatRandomStream;
};
