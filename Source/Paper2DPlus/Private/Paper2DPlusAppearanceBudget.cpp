// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "Paper2DPlusAppearanceBudget.h"

#include "Algo/Sort.h"

void FPaper2DPlusAppearanceBudgetConfig::Normalize()
{
	MaxConcurrentLiveHandoffs = FMath::Max(0, MaxConcurrentLiveHandoffs);
	MaxCompositeBuildUnitsPerFrame = FMath::Max(0, MaxCompositeBuildUnitsPerFrame);
	MaxCompositeWorkMillisecondsPerFrame = FMath::Max(0.0, MaxCompositeWorkMillisecondsPerFrame);
	MaxIndependentChannels = FMath::Clamp(MaxIndependentChannels, 0, 2);
	NearDistance = FMath::Max(0.0f, NearDistance);
	MaximumVisibleDistance = FMath::Max(NearDistance, MaximumVisibleDistance);
	FrustumPaddingDegrees = FMath::Clamp(FrustumPaddingDegrees, 0.0f, 45.0f);
}

const FPaper2DPlusAppearanceBudgetDecision* FPaper2DPlusAppearanceBudgetFrame::Find(uint64 RegistrationId) const
{
	return Decisions.FindByPredicate([RegistrationId](const FPaper2DPlusAppearanceBudgetDecision& Decision)
	{
		return Decision.RegistrationId == RegistrationId;
	});
}

bool FPaper2DPlusAppearanceBudgetScheduler::Submit(const FPaper2DPlusAppearanceBudgetRequest& Request)
{
	if (Request.RegistrationId == 0)
	{
		return false;
	}

	if (const FPaper2DPlusAppearanceBudgetRequest* Existing = Requests.Find(Request.RegistrationId))
	{
		if (Request.RequestSequence < Existing->RequestSequence)
		{
			return false;
		}
	}

	// Equal sequence is deliberately replaceable: view visibility/distance are client-local and may change every frame
	// without an authoritative appearance sequence bump. A lower logical sequence can never resurrect stale work.
	Requests.Add(Request.RegistrationId, Request);
	return true;
}

void FPaper2DPlusAppearanceBudgetScheduler::Remove(uint64 RegistrationId)
{
	Requests.Remove(RegistrationId);
}

void FPaper2DPlusAppearanceBudgetScheduler::Reset()
{
	Requests.Reset();
}

FPaper2DPlusAppearanceBudgetFrame FPaper2DPlusAppearanceBudgetScheduler::EvaluateFrame(
	const FPaper2DPlusAppearanceBudgetConfig& Config) const
{
	TArray<FPaper2DPlusAppearanceBudgetRequest> Values;
	Requests.GenerateValueArray(Values);
	return Paper2DPlusAppearanceBudget::Evaluate(Values, Config);
}

namespace
{
	bool Paper2DPlusAppearanceBudget_IsNear(
		const FPaper2DPlusAppearanceBudgetRequest& Request,
		const FPaper2DPlusAppearanceBudgetConfig& Config)
	{
		return Request.DistanceToClosestView <= Config.NearDistance;
	}

	bool Paper2DPlusAppearanceBudget_HigherPriority(
		const FPaper2DPlusAppearanceBudgetRequest& A,
		const FPaper2DPlusAppearanceBudgetRequest& B,
		const FPaper2DPlusAppearanceBudgetConfig& Config)
	{
		if (A.BlueprintPriority != B.BlueprintPriority)
		{
			return A.BlueprintPriority > B.BlueprintPriority;
		}
		const bool bANear = Paper2DPlusAppearanceBudget_IsNear(A, Config);
		const bool bBNear = Paper2DPlusAppearanceBudget_IsNear(B, Config);
		if (bANear != bBNear)
		{
			return bANear;
		}
		if (A.bAppearanceChanging != B.bAppearanceChanging)
		{
			return A.bAppearanceChanging;
		}
		if (!FMath::IsNearlyEqual(A.DistanceToClosestView, B.DistanceToClosestView))
		{
			return A.DistanceToClosestView < B.DistanceToClosestView;
		}
		return A.RegistrationId < B.RegistrationId;
	}

	void Paper2DPlusAppearanceBudget_CountDecision(
		const FPaper2DPlusAppearanceBudgetDecision& Decision,
		FPaper2DPlusAppearanceStatsSnapshot& Stats)
	{
		Stats.VisiblePrimitiveCount += Decision.VisiblePrimitiveCount;
		Stats.CompositeBuildUnitsGranted += Decision.GrantedCompositeBuildUnits;
		Stats.LiveHandoffsGranted += Decision.bGrantedLiveHandoff ? 1 : 0;
		Stats.PendingCount += Decision.PendingReason != EPaper2DPlusAppearancePendingReason::None ? 1 : 0;

		switch (Decision.Tier)
		{
		case EPaper2DPlusAppearanceTier::DescriptorOnly: ++Stats.DescriptorOnlyCount; break;
		case EPaper2DPlusAppearanceTier::FarComposite: ++Stats.FarCompositeCount; break;
		case EPaper2DPlusAppearanceTier::NearComposite: ++Stats.NearCompositeCount; break;
		case EPaper2DPlusAppearanceTier::ChangingLive: ++Stats.ChangingLiveCount; break;
		default: break;
		}
	}
}

FPaper2DPlusAppearanceBudgetFrame Paper2DPlusAppearanceBudget::Evaluate(
	const TArray<FPaper2DPlusAppearanceBudgetRequest>& Requests,
	const FPaper2DPlusAppearanceBudgetConfig& InConfig)
{
	FPaper2DPlusAppearanceBudgetConfig Config = InConfig;
	Config.Normalize();

	FPaper2DPlusAppearanceBudgetFrame Result;
	Result.Stats.RegisteredCount = Requests.Num();
	Result.Decisions.Reserve(Requests.Num());

	TArray<const FPaper2DPlusAppearanceBudgetRequest*> PriorityOrder;
	PriorityOrder.Reserve(Requests.Num());
	for (const FPaper2DPlusAppearanceBudgetRequest& Request : Requests)
	{
		PriorityOrder.Add(&Request);
	}
	PriorityOrder.Sort([&Config](const FPaper2DPlusAppearanceBudgetRequest& A, const FPaper2DPlusAppearanceBudgetRequest& B)
	{
		return Paper2DPlusAppearanceBudget_HigherPriority(A, B, Config);
	});

	int32 RemainingLiveSlots = Config.MaxConcurrentLiveHandoffs;
	int32 RemainingBuildUnits = Config.MaxCompositeBuildUnitsPerFrame;

	for (const FPaper2DPlusAppearanceBudgetRequest* RequestPtr : PriorityOrder)
	{
		check(RequestPtr);
		const FPaper2DPlusAppearanceBudgetRequest& Request = *RequestPtr;
		FPaper2DPlusAppearanceBudgetDecision Decision;
		Decision.RegistrationId = Request.RegistrationId;
		Decision.RequestSequence = Request.RequestSequence;
		Decision.bCacheHit = Request.bCacheHit;
		Decision.CacheKeyLabel = Request.CacheKeyLabel;

		if (Request.bDedicatedServer)
		{
			Decision.PendingReason = EPaper2DPlusAppearancePendingReason::DedicatedServer;
			Result.Decisions.Add(MoveTemp(Decision));
			continue;
		}
		if (!Request.bHasLocalView)
		{
			Decision.PendingReason = EPaper2DPlusAppearancePendingReason::NoLocalView;
			Result.Decisions.Add(MoveTemp(Decision));
			continue;
		}
		if (!Request.bVisibleToAnyLocalView || Request.DistanceToClosestView > Config.MaximumVisibleDistance)
		{
			Decision.PendingReason = EPaper2DPlusAppearancePendingReason::OffScreen;
			Result.Decisions.Add(MoveTemp(Decision));
			continue;
		}

		++Result.Stats.VisibleCount;
		Decision.bReleaseVisuals = false;
		const bool bNear = Paper2DPlusAppearanceBudget_IsNear(Request, Config);
		const int32 CompositePrimitives = 1 + (bNear
			? FMath::Clamp(Request.IndependentChannelCount, 0, Config.MaxIndependentChannels)
			: 0);

		if (!Request.bCompositeSupported)
		{
			Decision.PendingReason = EPaper2DPlusAppearancePendingReason::UnsupportedComposite;
		}
		else if (!Request.bCanAdmitComposite)
		{
			Decision.PendingReason = EPaper2DPlusAppearancePendingReason::CachePressure;
		}
		else if (Request.bNeedsCompositeBuild)
		{
			++Result.Stats.CompositeQueueDepth;
			if (RemainingBuildUnits > 0)
			{
				Decision.GrantedCompositeBuildUnits = 1;
				--RemainingBuildUnits;
			}
			else
			{
				Decision.PendingReason = EPaper2DPlusAppearancePendingReason::WaitingForCompositeWork;
			}
		}

		const bool bNeedsLive = Request.bNeedsLiveFallback && !Request.bCompositeReadyForRequestedKey;
		if (bNeedsLive && RemainingLiveSlots > 0)
		{
			Decision.Tier = EPaper2DPlusAppearanceTier::ChangingLive;
			Decision.VisiblePrimitiveCount = FMath::Max(1, Request.AuthoredLivePrimitiveCount);
			Decision.bGrantedLiveHandoff = true;
			--RemainingLiveSlots;
		}
		else if (bNeedsLive && Request.bHasPreviousValidComposite)
		{
			Decision.Tier = bNear
				? EPaper2DPlusAppearanceTier::NearComposite
				: EPaper2DPlusAppearanceTier::FarComposite;
			Decision.VisiblePrimitiveCount = CompositePrimitives;
			Decision.bRetainPreviousComposite = true;
			Decision.PendingReason = EPaper2DPlusAppearancePendingReason::WaitingForLiveSlot;
		}
		else if (bNeedsLive)
		{
			// No stale primitive or hidden sentinel is created. The descriptor/gameplay remain current and the
			// deterministic queue will promote this request when capacity becomes available.
			Decision.bReleaseVisuals = true;
			Decision.PendingReason = EPaper2DPlusAppearancePendingReason::WaitingForLiveSlot;
		}
		else if (Request.bCompositeReadyForRequestedKey || Request.bHasPreviousValidComposite)
		{
			Decision.Tier = bNear
				? EPaper2DPlusAppearanceTier::NearComposite
				: EPaper2DPlusAppearanceTier::FarComposite;
			Decision.VisiblePrimitiveCount = CompositePrimitives;
		}
		else
		{
			Decision.bReleaseVisuals = true;
		}

		Result.Decisions.Add(MoveTemp(Decision));
	}

	Result.Decisions.Sort([](const FPaper2DPlusAppearanceBudgetDecision& A, const FPaper2DPlusAppearanceBudgetDecision& B)
	{
		return A.RegistrationId < B.RegistrationId;
	});
	for (const FPaper2DPlusAppearanceBudgetDecision& Decision : Result.Decisions)
	{
		Paper2DPlusAppearanceBudget_CountDecision(Decision, Result.Stats);
	}
	return Result;
}

bool Paper2DPlusAppearanceBudget::ProjectLocalViews(
	const FVector& TargetLocation,
	bool bTargetHidden,
	const TArray<FPaper2DPlusAppearanceLocalView>& Views,
	const FPaper2DPlusAppearanceBudgetConfig& InConfig,
	float& OutClosestDistance)
{
	FPaper2DPlusAppearanceBudgetConfig Config = InConfig;
	Config.Normalize();
	OutClosestDistance = TNumericLimits<float>::Max();
	if (bTargetHidden || Views.Num() == 0)
	{
		return false;
	}

	bool bVisible = false;
	for (const FPaper2DPlusAppearanceLocalView& View : Views)
	{
		const FVector ToTarget = TargetLocation - View.Location;
		const float Distance = ToTarget.Size();
		OutClosestDistance = FMath::Min(OutClosestDistance, Distance);
		if (Distance > Config.MaximumVisibleDistance)
		{
			continue;
		}
		if (Distance <= KINDA_SMALL_NUMBER)
		{
			bVisible = true;
			continue;
		}

		const float PaddedHalfFov = FMath::Min(
			89.0f,
			FMath::Clamp(View.HalfHorizontalFovDegrees, 1.0f, 89.0f)
				+ Config.FrustumPaddingDegrees);
		const float MinimumDot = FMath::Cos(FMath::DegreesToRadians(PaddedHalfFov));
		FVector SafeForward = View.Forward.GetSafeNormal();
		if (SafeForward.IsNearlyZero())
		{
			SafeForward = FVector::ForwardVector;
		}
		bVisible |= FVector::DotProduct(SafeForward, ToTarget / Distance) >= MinimumDot;
	}
	return bVisible;
}

const TCHAR* Paper2DPlusAppearanceBudget::LexToString(EPaper2DPlusAppearanceTier Tier)
{
	switch (Tier)
	{
	case EPaper2DPlusAppearanceTier::DescriptorOnly: return TEXT("DescriptorOnly");
	case EPaper2DPlusAppearanceTier::FarComposite: return TEXT("FarComposite");
	case EPaper2DPlusAppearanceTier::NearComposite: return TEXT("NearComposite");
	case EPaper2DPlusAppearanceTier::ChangingLive: return TEXT("ChangingLive");
	default: return TEXT("Unknown");
	}
}

const TCHAR* Paper2DPlusAppearanceBudget::LexToString(EPaper2DPlusAppearancePendingReason Reason)
{
	switch (Reason)
	{
	case EPaper2DPlusAppearancePendingReason::None: return TEXT("None");
	case EPaper2DPlusAppearancePendingReason::OffScreen: return TEXT("OffScreen");
	case EPaper2DPlusAppearancePendingReason::NoLocalView: return TEXT("NoLocalView");
	case EPaper2DPlusAppearancePendingReason::DedicatedServer: return TEXT("DedicatedServer");
	case EPaper2DPlusAppearancePendingReason::WaitingForLiveSlot: return TEXT("WaitingForLiveSlot");
	case EPaper2DPlusAppearancePendingReason::WaitingForCompositeWork: return TEXT("WaitingForCompositeWork");
	case EPaper2DPlusAppearancePendingReason::CachePressure: return TEXT("CachePressure");
	case EPaper2DPlusAppearancePendingReason::UnsupportedComposite: return TEXT("UnsupportedComposite");
	default: return TEXT("Unknown");
	}
}
