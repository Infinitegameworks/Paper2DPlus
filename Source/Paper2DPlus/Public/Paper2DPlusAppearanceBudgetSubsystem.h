// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Paper2DPlusAppearanceBudget.h"
#include "Subsystems/WorldSubsystem.h"
#include "Paper2DPlusAppearanceBudgetSubsystem.generated.h"

class UActorComponent;

DECLARE_DELEGATE_OneParam(
	FPaper2DPlusAppearanceBudgetDecisionDelegate,
	const FPaper2DPlusAppearanceBudgetDecision&);

/**
 * One client-local scheduler per world. Components register lightweight request state and receive decisions from this
 * single tick; there is no per-character appearance tick and no hidden sentinel primitive. Local cameras are sampled
 * together, so split-screen promotion is deterministic and a no-view world conservatively remains descriptor-only.
 */
UCLASS()
class PAPER2DPLUS_API UPaper2DPlusAppearanceBudgetSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Register once during component startup. Re-registering replaces the callback but preserves stable identity. */
	uint64 RegisterAppearance(
		UActorComponent* Component,
		const FPaper2DPlusAppearanceBudgetDecisionDelegate& ApplyDecision);

	void UnregisterAppearance(UActorComponent* Component);

	/** Latest-wins logical/render request. RegistrationId and local-view fields are owned by this subsystem. */
	bool UpdateAppearanceRequest(
		UActorComponent* Component,
		const FPaper2DPlusAppearanceBudgetRequest& Request);

	/** Read-only local priority override. It affects presentation scheduling only and never replication/gameplay. */
	void SetPriorityOverride(UActorComponent* Component, int32 Priority);

	const FPaper2DPlusAppearanceBudgetDecision* FindDecision(UActorComponent* Component) const;

	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Appearance", meta = (DisplayName = "Get Appearance Budget Stats"))
	FPaper2DPlusAppearanceStatsSnapshot GetStatsSnapshot() const { return LastFrame.Stats; }

	/** Settings-derived config; exposed as a plain value for deterministic tests and diagnostics. */
	FPaper2DPlusAppearanceBudgetConfig BuildConfig() const;

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Deinitialize() override;

#if WITH_DEV_AUTOMATION_TESTS
	void EvaluateNowForTests();
	/** Deterministic real-world profiling seam. Decisions still run through this subsystem and the production
	 *  evaluator; only the external camera discovery and project-settings lookup are replaced. */
	void SetLocalViewsOverrideForTests(
		TOptional<TArray<FPaper2DPlusAppearanceLocalView>> InViews)
	{
		LocalViewsOverrideForTests = MoveTemp(InViews);
	}
	void SetConfigOverrideForTests(TOptional<FPaper2DPlusAppearanceBudgetConfig> InConfig)
	{
		ConfigOverrideForTests = MoveTemp(InConfig);
	}
#endif

private:
	struct FRegistration
	{
		TWeakObjectPtr<UActorComponent> Component;
		uint64 RegistrationId = 0;
		int32 PriorityOverride = 0;
		FPaper2DPlusAppearanceBudgetRequest Request;
		FPaper2DPlusAppearanceBudgetDecisionDelegate ApplyDecision;
	};

	TMap<TWeakObjectPtr<UActorComponent>, FRegistration> Registrations;
	FPaper2DPlusAppearanceBudgetFrame LastFrame;
	uint64 NextRegistrationId = 1;

#if WITH_DEV_AUTOMATION_TESTS
	TOptional<TArray<FPaper2DPlusAppearanceLocalView>> LocalViewsOverrideForTests;
	TOptional<FPaper2DPlusAppearanceBudgetConfig> ConfigOverrideForTests;
#endif

	void EvaluateAndApply();
};
