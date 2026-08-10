// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Dom/JsonObject.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/EngineVersion.h"
#include "Misc/EngineVersionComparison.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Paper2DPlusAppearanceBudget.h"
#include "Paper2DPlusNetTypes.h"
#include "Paper2DPlusSettings.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Paper2DPlusAppearanceProfileHarness.h"

namespace
{
	// UE 5.8 / D3D11 / Ryzen 7 5800X calibration captured by Profile200 on 2026-07-10.
	// Two units at the strictest observed cost plus 10% = 0.299640745 ms, rounded up to 0.300 ms.
	constexpr double P2DP_Crowd_ReferenceTwelveLayerMillisecondsPerUnit = 0.04839897155761719;
	constexpr double P2DP_Crowd_ReferenceWorstMillisecondsPerUnit = 0.13620033860206604;
	constexpr double P2DP_Crowd_ReferenceHeadroomMultiplier = 1.10;
	constexpr double P2DP_Crowd_CalibratedWorkBudgetMilliseconds = 0.300;

	enum class EP2DP_CrowdKeyDistribution : uint8
	{
		Shared,
		Repeated,
		AllUnique
	};

	const TCHAR* P2DP_Crowd_DistributionName(EP2DP_CrowdKeyDistribution Distribution)
	{
		switch (Distribution)
		{
		case EP2DP_CrowdKeyDistribution::Shared: return TEXT("Shared");
		case EP2DP_CrowdKeyDistribution::Repeated: return TEXT("Repeated");
		case EP2DP_CrowdKeyDistribution::AllUnique: return TEXT("AllUnique");
		default: return TEXT("Unknown");
		}
	}

	int32 P2DP_Crowd_KeyIndex(EP2DP_CrowdKeyDistribution Distribution, int32 ActorIndex)
	{
		switch (Distribution)
		{
		case EP2DP_CrowdKeyDistribution::Shared: return 0;
		case EP2DP_CrowdKeyDistribution::Repeated: return ActorIndex % 20;
		case EP2DP_CrowdKeyDistribution::AllUnique: return ActorIndex;
		default: return ActorIndex;
		}
	}

	TArray<FPaper2DPlusAppearanceBudgetRequest> P2DP_Crowd_MakeFixture(
		int32 LayerCount,
		EP2DP_CrowdKeyDistribution Distribution)
	{
		TArray<FPaper2DPlusAppearanceBudgetRequest> Requests;
		Requests.Reserve(200);
		for (int32 Index = 0; Index < 200; ++Index)
		{
			FPaper2DPlusAppearanceBudgetRequest& Request = Requests.AddDefaulted_GetRef();
			Request.RegistrationId = static_cast<uint64>(Index + 1);
			Request.RequestSequence = 1;
			Request.bHasLocalView = true;
			Request.bVisibleToAnyLocalView = true;
			Request.bCanAdmitComposite = true;
			Request.bCompositeSupported = true;
			Request.bHasPreviousValidComposite = true;
			Request.CacheKeyLabel = FString::Printf(
				TEXT("CrowdKey-%d"),
				P2DP_Crowd_KeyIndex(Distribution, Index));

			if (Index < 140)
			{
				Request.DistanceToClosestView = 10000.0f;
				Request.bCompositeReadyForRequestedKey = true;
			}
			else if (Index < 190)
			{
				Request.DistanceToClosestView = 1000.0f;
				Request.bCompositeReadyForRequestedKey = true;
				Request.IndependentChannelCount = 2;
			}
			else
			{
				Request.DistanceToClosestView = 500.0f;
				Request.bAppearanceChanging = true;
				Request.bNeedsLiveFallback = true;
				Request.bCompositeReadyForRequestedKey = false;
				Request.bNeedsCompositeBuild = true;
				Request.AuthoredLivePrimitiveCount = LayerCount;
			}
		}
		return Requests;
	}

	bool P2DP_Crowd_WriteReport(
		const TArray<TSharedPtr<FJsonValue>>& FixtureRows,
		FString& OutPath)
	{
		TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetNumberField(TEXT("ReportSchemaVersion"), 2);
		Root->SetStringField(TEXT("ReportKind"), TEXT("Paper2DPlusAppearanceCrowdPolicyGate"));
		Root->SetStringField(TEXT("EngineVersion"), FEngineVersion::Current().ToString());
		Root->SetStringField(TEXT("TimingStatus"), TEXT("DeterministicPolicyOnly"));
		Root->SetStringField(TEXT("TimingNote"),
			TEXT("Run Paper2DPlus.AppearanceCrowd.Render.Profile200 without -nullrhi for measured GT/RT/GPU data."));
		Root->SetStringField(TEXT("RenderProfileArtifact"),
			TEXT("Saved/Automation/Paper2DPlusAppearanceCrowd/render-profile-report.json"));
		Root->SetArrayField(TEXT("Fixtures"), FixtureRows);

		FString Json;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
		if (!FJsonSerializer::Serialize(Root, Writer))
		{
			return false;
		}

		OutPath = FPaths::Combine(
			FPaths::ProjectSavedDir(),
			TEXT("Automation"),
			TEXT("Paper2DPlusAppearanceCrowd"),
			TEXT("reference-policy-report.json"));
		IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
		PlatformFile.CreateDirectoryTree(*FPaths::GetPath(OutPath));
		return FFileHelper::SaveStringToFile(Json, *OutPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusAppearanceCrowdPrimitiveGateTest,
	"Paper2DPlus.AppearanceCrowd.Reference200.PrimitiveAndWorkGates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusAppearanceCrowdPrimitiveGateTest::RunTest(const FString& Parameters)
{
	FPaper2DPlusAppearanceBudgetConfig Config;
	Config.MaxConcurrentLiveHandoffs = 10;
	Config.MaxCompositeBuildUnitsPerFrame = 2;
	Config.MaxCompositeWorkMillisecondsPerFrame = UPaper2DPlusSettings::Get()
		? UPaper2DPlusSettings::Get()->AppearanceCompositeWorkBudgetMs
		: P2DP_Crowd_CalibratedWorkBudgetMilliseconds;
	Config.MaxIndependentChannels = 2;

	// Hardware-calibrated timing constants (Ryzen 7 5800X reference numbers + the exact shipped 0.300 ms
	// budget) are NOT release authority here and would false-fail on any host that overrides
	// AppearanceCompositeWorkBudgetMs or when calibration drifts. The real measured perf gate is
	// scripts/profile-paper2dplus-appearance.ps1 (schema-3, TimingStatus=RenderCapableProductionPathMeasured).
	// Default runs log the budget-vs-calibration comparison; set P2DP_CROWD_TIMING_STRICT=1 to assert it.
	const double StrictestMeasuredPairWithHeadroom =
		P2DP_Crowd_ReferenceWorstMillisecondsPerUnit
		* static_cast<double>(Config.MaxCompositeBuildUnitsPerFrame)
		* P2DP_Crowd_ReferenceHeadroomMultiplier;
	const double TwelveLayerMeasuredPair =
		P2DP_Crowd_ReferenceTwelveLayerMillisecondsPerUnit
		* static_cast<double>(Config.MaxCompositeBuildUnitsPerFrame);
	const bool bTimingStrict =
		!FPlatformMisc::GetEnvironmentVariable(TEXT("P2DP_CROWD_TIMING_STRICT")).IsEmpty();
	if (bTimingStrict)
	{
		TestTrue(TEXT("shipped work budget is the documented 0.300 ms calibration"),
			FMath::IsNearlyEqual(
				Config.MaxCompositeWorkMillisecondsPerFrame,
				P2DP_Crowd_CalibratedWorkBudgetMilliseconds,
				1.e-6));
		TestTrue(TEXT("shipped work budget covers two strictest measured units plus ten-percent headroom"),
			Config.MaxCompositeWorkMillisecondsPerFrame >= StrictestMeasuredPairWithHeadroom);
		TestTrue(TEXT("shipped work budget separately covers the measured twelve-layer pair"),
			Config.MaxCompositeWorkMillisecondsPerFrame >= TwelveLayerMeasuredPair);
	}
	else
	{
		AddInfo(FString::Printf(
			TEXT("Work budget %.6f ms vs calibration: strictest pair+headroom %.6f ms, twelve-layer pair %.6f ms ")
			TEXT("(measured perf authority: scripts/profile-paper2dplus-appearance.ps1; set P2DP_CROWD_TIMING_STRICT=1 to assert)."),
			Config.MaxCompositeWorkMillisecondsPerFrame,
			StrictestMeasuredPairWithHeadroom,
			TwelveLayerMeasuredPair));
	}

	TArray<TSharedPtr<FJsonValue>> ReportRows;
	const int32 LayerCounts[] = { 4, 8, 12 };
	const EP2DP_CrowdKeyDistribution Distributions[] = {
		EP2DP_CrowdKeyDistribution::Shared,
		EP2DP_CrowdKeyDistribution::Repeated,
		EP2DP_CrowdKeyDistribution::AllUnique
	};

	for (int32 LayerCount : LayerCounts)
	{
		for (EP2DP_CrowdKeyDistribution Distribution : Distributions)
		{
			const TArray<FPaper2DPlusAppearanceBudgetRequest> Requests =
				P2DP_Crowd_MakeFixture(LayerCount, Distribution);
			const FPaper2DPlusAppearanceBudgetFrame Frame =
				Paper2DPlusAppearanceBudget::Evaluate(Requests, Config);
			const int32 ExpectedPrimitives = 140 + (50 * 3) + (10 * LayerCount);

			TestEqual(FString::Printf(TEXT("%d/%s has all 200 visible"), LayerCount,
				P2DP_Crowd_DistributionName(Distribution)), Frame.Stats.VisibleCount, 200);
			TestEqual(FString::Printf(TEXT("%d/%s primitive formula"), LayerCount,
				P2DP_Crowd_DistributionName(Distribution)), Frame.Stats.VisiblePrimitiveCount, ExpectedPrimitives);
			TestEqual(TEXT("far tier count"), Frame.Stats.FarCompositeCount, 140);
			TestEqual(TEXT("near tier count"), Frame.Stats.NearCompositeCount, 50);
			TestEqual(TEXT("changing tier count"), Frame.Stats.ChangingLiveCount, 10);
			TestEqual(TEXT("live cap is exact"), Frame.Stats.LiveHandoffsGranted, 10);
			TestEqual(TEXT("work cap is exact"), Frame.Stats.CompositeBuildUnitsGranted, 2);
			TestEqual(TEXT("all ten changing composites remain represented in the queue"),
				Frame.Stats.CompositeQueueDepth, 10);
			TestTrue(TEXT("hybrid primitives improve on matching all-live baseline"),
				Frame.Stats.VisiblePrimitiveCount < 200 * LayerCount);

			if (LayerCount == 8)
			{
				TestTrue(TEXT("8-layer acceptance gate <= 370"), Frame.Stats.VisiblePrimitiveCount <= 370);
			}
			if (LayerCount == 12)
			{
				TestTrue(TEXT("12-layer acceptance gate <= 410"), Frame.Stats.VisiblePrimitiveCount <= 410);
			}

			TSharedRef<FJsonObject> Row = MakeShared<FJsonObject>();
			Row->SetNumberField(TEXT("VisibleCharacters"), 200);
			Row->SetNumberField(TEXT("LayerCount"), LayerCount);
			Row->SetStringField(TEXT("KeyDistribution"), P2DP_Crowd_DistributionName(Distribution));
			Row->SetNumberField(TEXT("FarCount"), 140);
			Row->SetNumberField(TEXT("NearCount"), 50);
			Row->SetNumberField(TEXT("ChangingCount"), 10);
			Row->SetNumberField(TEXT("HybridPrimitiveCount"), Frame.Stats.VisiblePrimitiveCount);
			Row->SetNumberField(TEXT("AllLivePrimitiveCount"), 200 * LayerCount);
			Row->SetNumberField(TEXT("BuildUnitsGranted"), Frame.Stats.CompositeBuildUnitsGranted);
			Row->SetNumberField(TEXT("CompositeQueueDepth"), Frame.Stats.CompositeQueueDepth);
			Row->SetBoolField(TEXT("SynchronousLoadViolationsMeasured"), false);
			ReportRows.Add(MakeShared<FJsonValueObject>(Row));
		}
	}

	FString ReportPath;
	TestTrue(TEXT("versioned crowd policy report is written"), P2DP_Crowd_WriteReport(ReportRows, ReportPath));
	TestTrue(TEXT("report exists at the announced path"), FPaths::FileExists(ReportPath));
	AddInfo(FString::Printf(TEXT("Appearance crowd policy report: %s"), *ReportPath));
	return true;
}

#if WITH_EDITOR

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusAppearanceCrowdRenderProfileTest,
	"Paper2DPlus.AppearanceCrowd.Render.Profile200",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusAppearanceCrowdRenderProfileTest::RunTest(const FString& Parameters)
{
#if UE_VERSION_OLDER_THAN(5, 8, 0)
	AddWarning(TEXT(
		"The measured 200-character performance profile is calibrated and release-gated on UE 5.8. "
		"Cross-version suites retain the deterministic appearance policy coverage."));
	return true;
#else
	// The whole body must sit under #else: an early return above a compiled-anyway body is C4702
	// unreachable code, which the 5.5-5.7 installed-engine build env elevates to error.

	if (!FApp::CanEverRender())
	{
		// The [P2DP-SKIPPED] token is written literally here on purpose. The shared helper lives in
		// the EDITOR module's private test folder, which this RUNTIME test cannot include, and a
		// cross-module private include would be worse than repeating one string. The token itself is
		// the contract — see Paper2DPlusTestSkip.h for why skips must be countable.
		AddWarning(TEXT(
			"[P2DP-SKIPPED] Render-capable 200-character profile — non-rendering RHI. "
			"Run scripts/profile-paper2dplus-appearance.ps1 to generate the measured stable report."));
		return true;
	}

	FPaper2DPlusAppearanceBudgetConfig Config;
	Config.MaxConcurrentLiveHandoffs = 10;
	Config.MaxCompositeBuildUnitsPerFrame = 2;
	Config.MaxCompositeWorkMillisecondsPerFrame = UPaper2DPlusSettings::Get()
		? UPaper2DPlusSettings::Get()->AppearanceCompositeWorkBudgetMs
		: P2DP_Crowd_CalibratedWorkBudgetMilliseconds;
	Config.MaxIndependentChannels = 2;
	Config.MaxTransientCacheBytes = 128ull * 1024ull * 1024ull;
	Config.Normalize();

	FPaper2DPlusAppearanceProfileRun Run;
	FString Error;
	if (!TestTrue(
		TEXT("the programmatic 200-character render world completes"),
		FPaper2DPlusAppearanceProfileHarness::RunReference200(Config, Run, Error)))
	{
		AddError(Error);
		return false;
	}

	// Publish the report before evaluating performance gates, so a failed platform gate remains diagnosable.
	if (!TestTrue(
		TEXT("the versioned render comparison report publishes atomically"),
		FPaper2DPlusAppearanceProfileHarness::WriteStableReport(Run, Error)))
	{
		AddError(Error);
		return false;
	}

	TestEqual(TEXT("4/8/12 x shared/repeated/all-unique emits nine fixtures"), Run.Fixtures.Num(), 9);
	bool bAnyGpuTimingAvailable = false;
	for (const FPaper2DPlusAppearanceProfileFixtureMetrics& Fixture : Run.Fixtures)
	{
		const FString Label = FString::Printf(
			TEXT("%d/%s"),
			Fixture.LayerCount,
			FPaper2DPlusAppearanceProfileHarness::DistributionName(Fixture.KeyDistribution));
		TestEqual(
			*FString::Printf(TEXT("%s all-live primitive count"), *Label),
			Fixture.AllLive.RegisteredPrimitiveCount,
			200 * Fixture.LayerCount);
		TestEqual(
			*FString::Printf(TEXT("%s hybrid world matches policy count"), *Label),
			Fixture.Hybrid.RegisteredPrimitiveCount,
			Fixture.ExpectedHybridPrimitiveCount);
		TestTrue(
			*FString::Printf(TEXT("%s scheduler build grants stay within the hard cap"), *Label),
			Fixture.PolicyBuildUnitsGranted <= Config.MaxCompositeBuildUnitsPerFrame);
		TestTrue(
			*FString::Printf(TEXT("%s cache admission reservation matches its contract and byte cap"), *Label),
			Fixture.CacheReservationBytes == Fixture.ExpectedCacheReservationBytes
				&& Fixture.CacheReservationBytes <= Fixture.CacheByteBudget);
		TestTrue(
			*FString::Printf(TEXT("%s initialized render targets report bounded resource bytes"), *Label),
			Fixture.bInitializedRenderTargetResourceBytesAvailable
				&& Fixture.InitializedRenderTargetResourceBytes > 0
				&& Fixture.InitializedRenderTargetResourceBytes <= Fixture.CacheByteBudget);
		TestEqual(
			*FString::Printf(TEXT("%s production cache entry count matches far/near recipe keys"), *Label),
			Fixture.ResidentCacheEntries,
			Fixture.ExpectedProductionCompositeKeys);
		TestEqual(
			*FString::Printf(TEXT("%s uses 200 production Layer Render components"), *Label),
			Fixture.ProductionComponentCount,
			200);
		TestEqual(
			*FString::Printf(TEXT("%s observes 140 stable far composites"), *Label),
			Fixture.StableFarCompositeCount,
			140);
		TestEqual(
			*FString::Printf(TEXT("%s observes 50 stable near composites"), *Label),
			Fixture.StableNearCompositeCount,
			50);
		TestEqual(
			*FString::Printf(TEXT("%s observes ten production changing-live handoffs"), *Label),
			Fixture.ChangingLiveCount,
			10);
		TestTrue(
			*FString::Printf(TEXT("%s reports measured composite submission work"), *Label),
			Fixture.MeasuredCompositeSubmissionMillisecondsPerUnit > 0.0);
		TestTrue(
			*FString::Printf(TEXT("%s replicated-property payload is measured and internally consistent"), *Label),
			Fixture.ReplicatedPropertyPayloadBitsPerChange > 0
				&& Fixture.ReplicatedPropertyPayloadBytesPerChangeCeil > 0
				&& Fixture.ReplicatedPropertyPayloadBitsFor200Changes
					== Fixture.ReplicatedPropertyPayloadBitsPerChange * 200
				&& Fixture.ReplicatedPropertyPayloadBytesFor200ChangesCeil
					== Fixture.ReplicatedPropertyPayloadBytesPerChangeCeil * 200
				&& Fixture.bReplicatedPropertyPayloadRoundTripMatched
				&& Fixture.bReplicatedPropertyVerified
				&& Fixture.bReplicatedStateExcludesTierCachePixelFields);
		TestEqual(
			*FString::Printf(TEXT("%s warmed frame path loads synchronously zero times"), *Label),
			Fixture.SynchronousLoadViolations,
			static_cast<int64>(0));
		bAnyGpuTimingAvailable |= Fixture.AllLive.bGpuTimingAvailable
			&& Fixture.Hybrid.bGpuTimingAvailable;
	}

	TestTrue(TEXT("all reference primitive caps pass"), Run.bPrimitiveGatePassed);
	TestTrue(TEXT("warm appearance game-thread work improves over all-live in every fixture"),
		Run.bGameThreadGatePassed);
	TestTrue(TEXT("warm render-thread work improves over all-live in every fixture"),
		Run.bRenderThreadGatePassed);
	TestTrue(TEXT("cache reservations and initialized render-target resources fit the hard byte cap"),
		Run.bCacheGatePassed);
	TestTrue(TEXT("the measured milliseconds budget admits the configured deterministic unit cap"),
		Run.bCompositeWorkBudgetGatePassed);
	TestTrue(TEXT("all warmed frame paths remain load-free"), Run.bLoadGatePassed);
	TestTrue(TEXT("all fixtures exercise the production component/subsystem/cache/handoff path"),
		Run.bProductionPathGatePassed);
	TestEqual(TEXT("200 logical authority changes publish exactly 200 snapshots"),
		Run.AuthoritySnapshotPublishes, Run.LogicalAuthorityAppearanceChanges);
	TestEqual(TEXT("authority fan-out probe has one result per simulated client count"),
		Run.AuthorityPublishesBySimulatedClientCount.Num(), Run.SimulatedClientCounts.Num());
	for (int32 PublishCount : Run.AuthorityPublishesBySimulatedClientCount)
	{
		TestEqual(TEXT("simulated client fan-out never multiplies authority publishes"),
			PublishCount, 200);
	}
	TestEqual(TEXT("client fan-out probe has one observed OnRep result per client count"),
		Run.ClientOnRepApplicationsBySimulatedClientCount.Num(), Run.SimulatedClientCounts.Num());
	for (int32 Index = 0; Index < Run.SimulatedClientCounts.Num()
		&& Index < Run.ClientOnRepApplicationsBySimulatedClientCount.Num(); ++Index)
	{
		TestEqual(TEXT("registered proxy OnRep applications match snapshot fan-out"),
			Run.ClientOnRepApplicationsBySimulatedClientCount[Index],
			Run.SimulatedClientCounts[Index] * 200);
	}
	TestTrue(TEXT("late join applies the latest canonical snapshot"),
		Run.bLateJoinAppliedLatestSnapshot);
	TestTrue(TEXT("dedicated-server authorities own zero visual children"),
		Run.bDedicatedServerAllocatedNoVisualChildren);
	TestTrue(TEXT("network payload/authority contract gate passes"),
		Run.bNetworkContractGatePassed);
	if (!bAnyGpuTimingAvailable)
	{
		AddWarning(TEXT(
			"This RHI did not expose absolute timestamp queries; the report records GPU timing as unavailable "
			"instead of fabricating a value."));
	}

	UScriptStruct* AppearanceStruct = FPaper2DPlusRepAppearanceState::StaticStruct();
	TestNull(TEXT("replication envelope has no local Tier field"),
		AppearanceStruct->FindPropertyByName(TEXT("Tier")));
	TestNull(TEXT("replication envelope has no local CacheKey field"),
		AppearanceStruct->FindPropertyByName(TEXT("CacheKey")));
	TestNull(TEXT("replication envelope has no local QueueDepth field"),
		AppearanceStruct->FindPropertyByName(TEXT("QueueDepth")));
	TestNull(TEXT("replication envelope has no pixel resource field"),
		AppearanceStruct->FindPropertyByName(TEXT("CompositeTexture")));

	AddInfo(FString::Printf(TEXT("Appearance crowd render profile: %s"), *Run.ReportPath));
	return true;
#endif // UE_VERSION_OLDER_THAN(5, 8, 0)
}

#endif // WITH_EDITOR

#endif // WITH_DEV_AUTOMATION_TESTS
