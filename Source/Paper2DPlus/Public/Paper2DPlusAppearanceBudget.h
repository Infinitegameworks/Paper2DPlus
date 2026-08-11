// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Paper2DPlusAppearanceBudget.generated.h"

/** Client-local visual tier. This value is never serialized or replicated. */
UENUM(BlueprintType)
enum class EPaper2DPlusAppearanceTier : uint8
{
	/** Logical appearance only: stored, dedicated-server, no-view, or active off-screen state. */
	DescriptorOnly UMETA(DisplayName = "Descriptor Only"),
	/** One prepared base-composite primitive. */
	FarComposite UMETA(DisplayName = "Far Composite"),
	/** One prepared base composite plus at most two independently animated channels. */
	NearComposite UMETA(DisplayName = "Near Composite"),
	/** Temporary exact authored live layers while a new prepared representation is pending. */
	ChangingLive UMETA(DisplayName = "Changing (Live Layers)")
};

/** Why a visible appearance has not yet reached its requested representation. */
UENUM(BlueprintType)
enum class EPaper2DPlusAppearancePendingReason : uint8
{
	None,
	OffScreen,
	NoLocalView,
	DedicatedServer,
	WaitingForLiveSlot,
	WaitingForCompositeWork,
	CachePressure,
	UnsupportedComposite
};

/**
 * Hard client-local limits consumed by the pure policy and the world scheduler. Platform projects may tune
 * memory/work values without changing authoritative appearance data, replication, gameplay composition, or cache keys.
 */
struct PAPER2DPLUS_API FPaper2DPlusAppearanceBudgetConfig
{
	int32 MaxConcurrentLiveHandoffs = 10;
	int32 MaxCompositeBuildUnitsPerFrame = 2;
	/**
	 * Hard game-thread preparation/submission budget. A completed unit may consume the remainder; no later unit starts.
	 * The 0.300 ms reference default is calibrated from the U31 real-RHI 200-character fixture; see the crowd benchmark.
	 */
	double MaxCompositeWorkMillisecondsPerFrame = 0.300;
	int32 MaxIndependentChannels = 2;
	uint64 MaxTransientCacheBytes = 128ull * 1024ull * 1024ull;
	float NearDistance = 3000.0f;
	float MaximumVisibleDistance = 100000.0f;
	float FrustumPaddingDegrees = 8.0f;

	void Normalize();
};

/** One lightweight request. No texture, primitive, actor, or replicated payload is stored here. */
struct PAPER2DPLUS_API FPaper2DPlusAppearanceBudgetRequest
{
	/** Stable world-scheduler identity. Lower id wins otherwise-equal requests. */
	uint64 RegistrationId = 0;
	/** Latest-wins logical appearance sequence. A lower sequence can never replace a queued request. */
	uint64 RequestSequence = 0;

	bool bDedicatedServer = false;
	bool bHasLocalView = true;
	bool bVisibleToAnyLocalView = false;
	bool bAppearanceChanging = false;
	bool bNeedsLiveFallback = false;
	bool bHasPreviousValidComposite = false;
	bool bCompositeReadyForRequestedKey = false;
	bool bNeedsCompositeBuild = false;
	bool bCanAdmitComposite = true;
	bool bCompositeSupported = true;
	bool bCacheHit = false;

	float DistanceToClosestView = TNumericLimits<float>::Max();
	int32 BlueprintPriority = 0;
	int32 AuthoredLivePrimitiveCount = 1;
	int32 IndependentChannelCount = 0;
	FString CacheKeyLabel;
};

/** One client-local camera projection consumed by the pure visibility policy. */
struct PAPER2DPLUS_API FPaper2DPlusAppearanceLocalView
{
	FVector Location = FVector::ZeroVector;
	FVector Forward = FVector::ForwardVector;
	float HalfHorizontalFovDegrees = 45.0f;
};

/** Pure scheduling result for one registration. */
USTRUCT(BlueprintType)
struct PAPER2DPLUS_API FPaper2DPlusAppearanceBudgetDecision
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Paper2DPlus|Appearance")
	EPaper2DPlusAppearanceTier Tier = EPaper2DPlusAppearanceTier::DescriptorOnly;

	UPROPERTY(BlueprintReadOnly, Category = "Paper2DPlus|Appearance")
	EPaper2DPlusAppearancePendingReason PendingReason = EPaper2DPlusAppearancePendingReason::None;

	UPROPERTY(BlueprintReadOnly, Category = "Paper2DPlus|Appearance")
	int32 VisiblePrimitiveCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Paper2DPlus|Appearance")
	int32 GrantedCompositeBuildUnits = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Paper2DPlus|Appearance")
	bool bGrantedLiveHandoff = false;

	UPROPERTY(BlueprintReadOnly, Category = "Paper2DPlus|Appearance")
	bool bRetainPreviousComposite = false;

	UPROPERTY(BlueprintReadOnly, Category = "Paper2DPlus|Appearance")
	bool bReleaseVisuals = true;

	UPROPERTY(BlueprintReadOnly, Category = "Paper2DPlus|Appearance")
	bool bCacheHit = false;

	UPROPERTY(BlueprintReadOnly, Category = "Paper2DPlus|Appearance")
	FString CacheKeyLabel;

	uint64 RegistrationId = 0;
	uint64 RequestSequence = 0;
};

/** Aggregate, read-only diagnostics for the current client world. */
USTRUCT(BlueprintType)
struct PAPER2DPLUS_API FPaper2DPlusAppearanceStatsSnapshot
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Paper2DPlus|Appearance") int32 RegisteredCount = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Paper2DPlus|Appearance") int32 VisibleCount = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Paper2DPlus|Appearance") int32 DescriptorOnlyCount = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Paper2DPlus|Appearance") int32 FarCompositeCount = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Paper2DPlus|Appearance") int32 NearCompositeCount = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Paper2DPlus|Appearance") int32 ChangingLiveCount = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Paper2DPlus|Appearance") int32 VisiblePrimitiveCount = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Paper2DPlus|Appearance") int32 LiveHandoffsGranted = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Paper2DPlus|Appearance") int32 PendingCount = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Paper2DPlus|Appearance") int32 CompositeBuildUnitsGranted = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Paper2DPlus|Appearance") int32 CompositeQueueDepth = 0;
	/** Deterministic cache-admission reservation; not driver/RHI allocation. */
	UPROPERTY(BlueprintReadOnly, Category = "Paper2DPlus|Appearance") int64 CacheReservationBytes = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Paper2DPlus|Appearance") int32 ResidentCacheEntries = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Paper2DPlus|Appearance") int64 CacheHits = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Paper2DPlus|Appearance") int64 CacheMisses = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Paper2DPlus|Appearance") int64 CacheEvictions = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Paper2DPlus|Appearance") int64 SynchronousLoadViolations = 0;
	/** Persistent reflected serialization sampled at publish; not packet or wire bytes. */
	UPROPERTY(BlueprintReadOnly, Category = "Paper2DPlus|Appearance") int64 SerializedAppearanceDescriptorBytes = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Paper2DPlus|Appearance") double CompositeBuildMilliseconds = 0.0;
	/** Actual preparation/submission time reported by completed build attempts in the current game frame. */
	UPROPERTY(BlueprintReadOnly, Category = "Paper2DPlus|Appearance")
	double CompositeWorkMillisecondsMeasuredThisFrame = 0.0;
	/** Configured hard milliseconds grant for one game frame; units/frame remains an independent hard cap. */
	UPROPERTY(BlueprintReadOnly, Category = "Paper2DPlus|Appearance")
	double CompositeWorkMillisecondsGrantedPerFrame = 0.0;
	UPROPERTY(BlueprintReadOnly, Category = "Paper2DPlus|Appearance")
	int32 CompositeBuildUnitsClaimedThisFrame = 0;
};

/** A complete deterministic frame from the pure scheduler. Decisions are sorted by RegistrationId. */
struct PAPER2DPLUS_API FPaper2DPlusAppearanceBudgetFrame
{
	TArray<FPaper2DPlusAppearanceBudgetDecision> Decisions;
	FPaper2DPlusAppearanceStatsSnapshot Stats;

	const FPaper2DPlusAppearanceBudgetDecision* Find(uint64 RegistrationId) const;
};

/**
 * Pure latest-wins scheduler used by the world subsystem and worldless tests. Submit is O(1); EvaluateFrame performs
 * one deterministic priority sort and grants hard-capped live/build work. It owns no UObjects and never ticks actors.
 */
class PAPER2DPLUS_API FPaper2DPlusAppearanceBudgetScheduler
{
public:
	bool Submit(const FPaper2DPlusAppearanceBudgetRequest& Request);
	void Remove(uint64 RegistrationId);
	void Reset();
	int32 Num() const { return Requests.Num(); }
	FPaper2DPlusAppearanceBudgetFrame EvaluateFrame(const FPaper2DPlusAppearanceBudgetConfig& Config) const;

private:
	TMap<uint64, FPaper2DPlusAppearanceBudgetRequest> Requests;
};

namespace Paper2DPlusAppearanceBudget
{
	PAPER2DPLUS_API const TCHAR* LexToString(EPaper2DPlusAppearanceTier Tier);
	PAPER2DPLUS_API const TCHAR* LexToString(EPaper2DPlusAppearancePendingReason Reason);
	PAPER2DPLUS_API FPaper2DPlusAppearanceBudgetFrame Evaluate(
		const TArray<FPaper2DPlusAppearanceBudgetRequest>& Requests,
		const FPaper2DPlusAppearanceBudgetConfig& Config);
	/** Multi-view, no-primitive visibility projection shared by the world subsystem and deterministic tests. */
	PAPER2DPLUS_API bool ProjectLocalViews(
		const FVector& TargetLocation,
		bool bTargetHidden,
		const TArray<FPaper2DPlusAppearanceLocalView>& Views,
		const FPaper2DPlusAppearanceBudgetConfig& Config,
		float& OutClosestDistance);
}
