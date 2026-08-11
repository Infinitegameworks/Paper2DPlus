// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CoreMinimal.h"
#include "Paper2DPlusAppearanceBudget.h"

enum class EPaper2DPlusAppearanceProfileKeyDistribution : uint8
{
	Shared,
	Repeated,
	AllUnique
};

struct FPaper2DPlusAppearanceProfileModeMetrics
{
	int32 RegisteredPrimitiveCount = 0;
	double ColdActivationWallMilliseconds = 0.0;
	double WarmGameThreadMilliseconds = 0.0;
	double WarmRenderThreadMilliseconds = 0.0;
	double WarmGpuMilliseconds = 0.0;
	bool bGpuTimingAvailable = false;
};

struct FPaper2DPlusAppearanceProfileFixtureMetrics
{
	int32 VisibleCharacters = 200;
	int32 LayerCount = 0;
	EPaper2DPlusAppearanceProfileKeyDistribution KeyDistribution =
		EPaper2DPlusAppearanceProfileKeyDistribution::Shared;
	int32 UniqueAppearanceKeys = 0;
	/** Actual far+near production cache keys (near has a distinct exact-order recipe). */
	int32 ExpectedProductionCompositeKeys = 0;
	int32 ExpectedHybridPrimitiveCount = 0;
	int32 ProductionComponentCount = 0;
	int32 StableFarCompositeCount = 0;
	int32 StableNearCompositeCount = 0;
	int32 ChangingLiveCount = 0;
	int32 PolicyLiveHandoffsGranted = 0;
	int32 PolicyBuildUnitsGranted = 0;
	int32 PolicyQueueDepth = 0;
	int32 FinalChangingQueueDepth = 0;
	int32 AnimationFrameCount = 0;
	int32 ResidentCacheEntries = 0;
	int32 CompositeQueueDepthCold = 0;
	int32 CompositeBuildUnitsPerFrame = 0;
	double CompositeWorkBudgetMillisecondsPerFrame = 0.0;
	double MeasuredCompositeSubmissionMillisecondsPerUnit = 0.0;
	int32 MeasuredUnitsFitWithinMilliseconds = 0;
	int32 FramesToDrainColdQueue = 0;
	/** Deterministic bytes reserved by cache admission; not a claim about driver/RHI allocation. */
	int64 CacheReservationBytes = 0;
	int64 ExpectedCacheReservationBytes = 0;
	/** Engine-reported resource bytes after real render-target initialization on a render-capable RHI. */
	int64 InitializedRenderTargetResourceBytes = 0;
	bool bInitializedRenderTargetResourceBytesAvailable = false;
	int64 CacheByteBudget = 0;
	int32 ColdCacheMisses = 0;
	int32 ColdSharedPendingHits = 0;
	int32 WarmCacheHits = 0;
	/** Retained schema-3 compatibility estimate; never used by the network measurement gate. */
	int32 SerializedDescriptorBytesEstimate = 0;
	int64 SerializedDescriptorBytesFor200ChangesEstimate = 0;
	/** Exact FRepLayout/FNetBitWriter value payload; excludes property/changelist and packet framing. */
	int64 ReplicatedPropertyPayloadBitsPerChange = 0;
	int64 ReplicatedPropertyPayloadBytesPerChangeCeil = 0;
	int64 ReplicatedPropertyPayloadBitsFor200Changes = 0;
	int64 ReplicatedPropertyPayloadBytesFor200ChangesCeil = 0;
	bool bReplicatedPropertyPayloadRoundTripMatched = false;
	bool bReplicatedPropertyVerified = false;
	bool bReplicatedStateExcludesTierCachePixelFields = false;
	int64 SynchronousLoadViolations = 0;
	FPaper2DPlusAppearanceProfileModeMetrics AllLive;
	FPaper2DPlusAppearanceProfileModeMetrics Hybrid;
};

struct FPaper2DPlusAppearanceProfileRun
{
	int32 ReportSchemaVersion = 3;
	FString EngineVersion;
	FString Platform;
	FString RhiName;
	FString GeneratedUtc;
	FString ReportPath;
	FPaper2DPlusAppearanceBudgetConfig Config;
	TArray<FPaper2DPlusAppearanceProfileFixtureMetrics> Fixtures;
	bool bRenderThreadGatePassed = true;
	bool bGameThreadGatePassed = true;
	bool bPrimitiveGatePassed = true;
	bool bCacheGatePassed = true;
	bool bCompositeWorkBudgetGatePassed = true;
	bool bLoadGatePassed = true;
	int32 LogicalAuthorityAppearanceChanges = 0;
	int32 AuthoritySnapshotPublishes = 0;
	TArray<int32> SimulatedClientCounts;
	TArray<int32> AuthorityPublishesBySimulatedClientCount;
	bool bLateJoinAppliedLatestSnapshot = false;
	bool bDedicatedServerAllocatedNoVisualChildren = false;
	TArray<int32> ClientOnRepApplicationsBySimulatedClientCount;
	bool bNetworkContractGatePassed = true;
	bool bProductionPathGatePassed = true;
};

/**
 * Render-capable, programmatically constructed 200-character comparison harness.
 *
 * The harness owns a transient Game world with exactly 200 character actors. It compares the authored all-live
 * baseline with the U31 140-far/50-near/10-changing tier projection across 4/8/12 layers and three cache-key
 * distributions. It writes a stable schema/path report under Saved/Automation; no package or content asset is made.
 */
class FPaper2DPlusAppearanceProfileHarness
{
public:
	static const TCHAR* DistributionName(EPaper2DPlusAppearanceProfileKeyDistribution Distribution);

	static bool RunReference200(
		const FPaper2DPlusAppearanceBudgetConfig& Config,
		FPaper2DPlusAppearanceProfileRun& OutRun,
		FString& OutError);

	static bool WriteStableReport(
		FPaper2DPlusAppearanceProfileRun& Run,
		FString& OutError);
};

#endif // WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
