// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "Paper2DPlusAppearanceBudgetSubsystem.h"

#include "Paper2DPlusAppearanceCompositeCache.h"
#include "Paper2DPlusAppearanceStats.h"
#include "Paper2DPlusSettings.h"
#include "Components/ActorComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"

namespace
{
	void Paper2DPlusAppearanceBudget_GatherLocalViews(
		const UWorld* World,
		TArray<FPaper2DPlusAppearanceLocalView>& OutViews)
	{
		OutViews.Reset();
		if (!World)
		{
			return;
		}

		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			const APlayerController* Controller = It->Get();
			const APlayerCameraManager* Camera = Controller ? Controller->PlayerCameraManager : nullptr;
			if (!Controller || !Controller->IsLocalController() || !Camera)
			{
				continue;
			}

			FPaper2DPlusAppearanceLocalView& View = OutViews.AddDefaulted_GetRef();
			View.Location = Camera->GetCameraLocation();
			View.Forward = Camera->GetCameraRotation().Vector().GetSafeNormal();
			View.HalfHorizontalFovDegrees = FMath::Clamp(Camera->GetFOVAngle() * 0.5f, 1.0f, 89.0f);
		}
	}
}

uint64 UPaper2DPlusAppearanceBudgetSubsystem::RegisterAppearance(
	UActorComponent* Component,
	const FPaper2DPlusAppearanceBudgetDecisionDelegate& ApplyDecision)
{
	if (!IsValid(Component))
	{
		return 0;
	}

	if (FRegistration* Existing = Registrations.Find(Component))
	{
		Existing->ApplyDecision = ApplyDecision;
		return Existing->RegistrationId;
	}

	FRegistration Registration;
	Registration.Component = Component;
	const uint64 AssignedId = NextRegistrationId++;
	Registration.RegistrationId = AssignedId;
	if (NextRegistrationId == 0)
	{
		NextRegistrationId = 1;
	}
	Registration.Request.RegistrationId = Registration.RegistrationId;
	Registration.ApplyDecision = ApplyDecision;
	Registrations.Add(Component, MoveTemp(Registration));
	return AssignedId;
}

void UPaper2DPlusAppearanceBudgetSubsystem::UnregisterAppearance(UActorComponent* Component)
{
	if (!Component)
	{
		return;
	}
	if (const FRegistration* Registration = Registrations.Find(Component))
	{
		const uint64 RegistrationId = Registration->RegistrationId;
		LastFrame.Decisions.RemoveAll([RegistrationId](const FPaper2DPlusAppearanceBudgetDecision& Decision)
		{
			return Decision.RegistrationId == RegistrationId;
		});
	}
	Registrations.Remove(Component);
}

bool UPaper2DPlusAppearanceBudgetSubsystem::UpdateAppearanceRequest(
	UActorComponent* Component,
	const FPaper2DPlusAppearanceBudgetRequest& Request)
{
	FRegistration* Registration = Component ? Registrations.Find(Component) : nullptr;
	if (!Registration || Request.RequestSequence < Registration->Request.RequestSequence)
	{
		return false;
	}

	Registration->Request = Request;
	Registration->Request.RegistrationId = Registration->RegistrationId;
	Registration->Request.BlueprintPriority = Registration->PriorityOverride;
	return true;
}

void UPaper2DPlusAppearanceBudgetSubsystem::SetPriorityOverride(UActorComponent* Component, int32 Priority)
{
	if (FRegistration* Registration = Component ? Registrations.Find(Component) : nullptr)
	{
		Registration->PriorityOverride = Priority;
		Registration->Request.BlueprintPriority = Priority;
	}
}

const FPaper2DPlusAppearanceBudgetDecision* UPaper2DPlusAppearanceBudgetSubsystem::FindDecision(
	UActorComponent* Component) const
{
	const FRegistration* Registration = Component ? Registrations.Find(Component) : nullptr;
	return Registration ? LastFrame.Find(Registration->RegistrationId) : nullptr;
}

FPaper2DPlusAppearanceBudgetConfig UPaper2DPlusAppearanceBudgetSubsystem::BuildConfig() const
{
#if WITH_DEV_AUTOMATION_TESTS
	if (ConfigOverrideForTests.IsSet())
	{
		FPaper2DPlusAppearanceBudgetConfig Config = ConfigOverrideForTests.GetValue();
		Config.Normalize();
		return Config;
	}
#endif
	FPaper2DPlusAppearanceBudgetConfig Config;
	if (const UPaper2DPlusSettings* Settings = UPaper2DPlusSettings::Get())
	{
		Config.MaxConcurrentLiveHandoffs = Settings->AppearanceMaxConcurrentLiveHandoffs;
		Config.MaxCompositeBuildUnitsPerFrame = Settings->AppearanceCompositeBuildUnitsPerFrame;
		Config.MaxCompositeWorkMillisecondsPerFrame = Settings->AppearanceCompositeWorkBudgetMs;
		Config.MaxIndependentChannels = Settings->AppearanceMaxIndependentChannels;
		Config.MaxTransientCacheBytes = static_cast<uint64>(FMath::Max<int64>(0, Settings->AppearanceTransientCacheBytes));
		Config.NearDistance = Settings->AppearanceNearDistance;
		Config.MaximumVisibleDistance = Settings->AppearanceMaximumVisibleDistance;
		Config.FrustumPaddingDegrees = Settings->AppearanceFrustumPaddingDegrees;
	}
	Config.Normalize();
	return Config;
}

void UPaper2DPlusAppearanceBudgetSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	EvaluateAndApply();
}

TStatId UPaper2DPlusAppearanceBudgetSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UPaper2DPlusAppearanceBudgetSubsystem, STATGROUP_Tickables);
}

bool UPaper2DPlusAppearanceBudgetSubsystem::IsTickable() const
{
	return Registrations.Num() > 0 && !IsTemplate();
}

bool UPaper2DPlusAppearanceBudgetSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game
		|| WorldType == EWorldType::PIE
		|| WorldType == EWorldType::GamePreview;
}

void UPaper2DPlusAppearanceBudgetSubsystem::Deinitialize()
{
	Registrations.Reset();
	LastFrame = FPaper2DPlusAppearanceBudgetFrame();
#if WITH_DEV_AUTOMATION_TESTS
	LocalViewsOverrideForTests.Reset();
	ConfigOverrideForTests.Reset();
#endif
	Super::Deinitialize();
}

void UPaper2DPlusAppearanceBudgetSubsystem::EvaluateAndApply()
{
	// GPU work completes independently of the game thread. Poll once from the one world scheduler before
	// decisions are projected; this is the sole no-stall path that promotes submitted targets to FrameReady.
	FPaper2DPlusAppearanceCompositeCache::Get().PollCompletedFrames();

	const FPaper2DPlusAppearanceBudgetConfig Config = BuildConfig();
	TArray<FPaper2DPlusAppearanceLocalView> Views;
#if WITH_DEV_AUTOMATION_TESTS
	if (LocalViewsOverrideForTests.IsSet())
	{
		Views = LocalViewsOverrideForTests.GetValue();
	}
	else
#endif
	{
		Paper2DPlusAppearanceBudget_GatherLocalViews(GetWorld(), Views);
	}

	TArray<FPaper2DPlusAppearanceBudgetRequest> Requests;
	TArray<TWeakObjectPtr<UActorComponent>> InvalidComponents;
	Requests.Reserve(Registrations.Num());
	for (TPair<TWeakObjectPtr<UActorComponent>, FRegistration>& Pair : Registrations)
	{
		FRegistration& Registration = Pair.Value;
		UActorComponent* Component = Registration.Component.Get();
		if (!IsValid(Component))
		{
			InvalidComponents.Add(Pair.Key);
			continue;
		}

		FPaper2DPlusAppearanceBudgetRequest Request = Registration.Request;
		Request.RegistrationId = Registration.RegistrationId;
		Request.BlueprintPriority = Registration.PriorityOverride;
		Request.bDedicatedServer = GetWorld() && GetWorld()->GetNetMode() == NM_DedicatedServer;
		Request.bHasLocalView = Views.Num() > 0;
		const AActor* Owner = Component->GetOwner();
		Request.bVisibleToAnyLocalView = Paper2DPlusAppearanceBudget::ProjectLocalViews(
			Owner ? Owner->GetActorLocation() : FVector::ZeroVector,
			!IsValid(Owner) || Owner->IsHidden(),
			Views,
			Config,
			Request.DistanceToClosestView);
		Requests.Add(MoveTemp(Request));
	}
	for (const TWeakObjectPtr<UActorComponent>& Invalid : InvalidComponents)
	{
		Registrations.Remove(Invalid);
	}

	LastFrame = Paper2DPlusAppearanceBudget::Evaluate(Requests, Config);
	const FPaper2DPlusAppearanceExternalStats External = Paper2DPlusAppearanceStats::Snapshot();
	LastFrame.Stats.CacheReservationBytes = External.CacheReservationBytes;
	LastFrame.Stats.ResidentCacheEntries = External.ResidentCacheEntries;
	LastFrame.Stats.CacheHits = External.CacheHits;
	LastFrame.Stats.CacheMisses = External.CacheMisses;
	LastFrame.Stats.CacheEvictions = External.CacheEvictions;
	LastFrame.Stats.SynchronousLoadViolations = External.SynchronousLoadViolations;
	LastFrame.Stats.SerializedAppearanceDescriptorBytes = External.SerializedAppearanceDescriptorBytes;
	LastFrame.Stats.CompositeBuildMilliseconds = External.CompositeBuildMilliseconds;
	LastFrame.Stats.CompositeWorkMillisecondsMeasuredThisFrame =
		External.CompositeWorkMillisecondsMeasuredThisFrame;
	LastFrame.Stats.CompositeWorkMillisecondsGrantedPerFrame =
		External.CompositeWorkMillisecondsGrantedPerFrame;
	LastFrame.Stats.CompositeBuildUnitsClaimedThisFrame =
		External.CompositeBuildUnitsClaimedThisFrame;

	TMap<uint64, TWeakObjectPtr<UActorComponent>> RegistrationById;
	RegistrationById.Reserve(Registrations.Num());
	for (const TPair<TWeakObjectPtr<UActorComponent>, FRegistration>& Pair : Registrations)
	{
		RegistrationById.Add(Pair.Value.RegistrationId, Pair.Key);
	}
	for (const FPaper2DPlusAppearanceBudgetDecision& Decision : LastFrame.Decisions)
	{
		if (const TWeakObjectPtr<UActorComponent>* ComponentKey = RegistrationById.Find(Decision.RegistrationId))
		{
			if (FRegistration* Registration = Registrations.Find(*ComponentKey))
			{
				Registration->ApplyDecision.ExecuteIfBound(Decision);
			}
		}
	}
}

#if WITH_DEV_AUTOMATION_TESTS
void UPaper2DPlusAppearanceBudgetSubsystem::EvaluateNowForTests()
{
	EvaluateAndApply();
}
#endif
