// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "Paper2DPlusAppearanceProfileHarness.h"

#include "Paper2DPlusAppearanceRenderBackend.h"
#include "Paper2DPlusAppearanceRenderPolicy.h"
#include "Paper2DPlusAppearanceCompositeCache.h"
#include "Paper2DPlusAppearanceBudgetSubsystem.h"
#include "Paper2DPlusAppearanceStats.h"
#include "Paper2DPlusCharacterLayerAsset.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Paper2DPlusCharacterProfileComponent.h"
#include "Paper2DPlusFlipbookComponent.h"
#include "Paper2DPlusLayerRenderComponent.h"
#include "Paper2DPlusNetTypes.h"
#include "Paper2DPlusNetAppearanceMeasurement.h"
#include "PaperFlipbook.h"
#include "PaperSprite.h"
#include "PaperSpriteComponent.h"
#include "SpriteEditorOnlyTypes.h"
#include "Camera/CameraTypes.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "Dom/JsonObject.h"
#include "DynamicRHI.h"
#include "Engine/Engine.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "GameFramework/WorldSettings.h"
#include "GenericPlatform/GenericPlatformProperties.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformMisc.h"
#include "Materials/MaterialInterface.h"
#include "Misc/App.h"
#include "Misc/DateTime.h"
#include "Misc/Crc.h"
#include "Misc/EngineVersion.h"
#include "Misc/EngineVersionComparison.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "RenderingThread.h"
#include "RHI.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/MemoryWriter.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

namespace
{
	constexpr int32 ProfileCharacterCount = 200;
	constexpr int32 ProfileFarCount = 140;
	constexpr int32 ProfileNearCount = 50;
	constexpr int32 ProfileChangingCount = 10;
	constexpr int32 ProfileCompositeFrameCount = 12;
	constexpr int32 ProfileWarmSamples = 3;
	constexpr int32 ProfilePassesPerSample = 2;
	constexpr int32 ProfileCompositeSubmissionSamples = 5;

	struct FPaper2DPlusAppearanceProfileCharacter
	{
		TObjectPtr<AActor> Actor = nullptr;
		TObjectPtr<UPaper2DPlusFlipbookComponent> Flipbook = nullptr;
		TObjectPtr<UPaper2DPlusCharacterProfileComponent> Profile = nullptr;
		TObjectPtr<UPaper2DPlusLayerRenderComponent> Appearance = nullptr;
		FGuid ChangingPresetB;
	};

	struct FPaper2DPlusAppearanceProfileTimestampState
	{
		FRenderQueryRHIRef GpuStartQuery;
		FRenderQueryRHIRef GpuEndQuery;
		uint64 RenderStartCycles = 0;
		uint64 RenderEndCycles = 0;
		uint64 GpuStartMicroseconds = 0;
		uint64 GpuEndMicroseconds = 0;
		bool bGpuTimingSupported = false;
		bool bGpuTimingRead = false;
	};

	struct FPaper2DPlusAppearanceProfileSample
	{
		double GameThreadMilliseconds = 0.0;
		double RenderThreadMilliseconds = 0.0;
		double GpuMilliseconds = 0.0;
		bool bGpuTimingAvailable = false;
	};

	int32 ProfileUniqueKeyCount(EPaper2DPlusAppearanceProfileKeyDistribution Distribution)
	{
		switch (Distribution)
		{
		case EPaper2DPlusAppearanceProfileKeyDistribution::Shared: return 1;
		case EPaper2DPlusAppearanceProfileKeyDistribution::Repeated: return 20;
		case EPaper2DPlusAppearanceProfileKeyDistribution::AllUnique: return ProfileCharacterCount;
		default: return ProfileCharacterCount;
		}
	}

	int32 ProfileKeyIndex(
		EPaper2DPlusAppearanceProfileKeyDistribution Distribution,
		int32 ActorIndex)
	{
		switch (Distribution)
		{
		case EPaper2DPlusAppearanceProfileKeyDistribution::Shared: return 0;
		case EPaper2DPlusAppearanceProfileKeyDistribution::Repeated: return ActorIndex % 20;
		case EPaper2DPlusAppearanceProfileKeyDistribution::AllUnique: return ActorIndex;
		default: return ActorIndex;
		}
	}

	double ProfileMedian(TArray<double> Values)
	{
		if (Values.Num() == 0)
		{
			return 0.0;
		}
		Values.Sort();
		const int32 Middle = Values.Num() / 2;
		return (Values.Num() & 1) != 0
			? Values[Middle]
			: (Values[Middle - 1] + Values[Middle]) * 0.5;
	}

	double ProfileImprovementPercent(double Baseline, double Candidate)
	{
		return Baseline > 0.0 ? ((Baseline - Candidate) / Baseline) * 100.0 : 0.0;
	}

	void ProfileWaitForGpuOnly()
	{
		ENQUEUE_RENDER_COMMAND(Paper2DPlusAppearanceProfileGpuWait)(
			[](FRHICommandListImmediate& RHICmdList)
			{
#if UE_VERSION_OLDER_THAN(5, 5, 0)
				RHICmdList.BlockUntilGPUIdle();
#else
				RHICmdList.SubmitAndBlockUntilGPUIdle();
#endif
			});
		FlushRenderingCommands();
	}

	FPaper2DPlusRepAppearanceState ProfileMakeAppearanceSnapshot(int32 LayerCount)
	{
		FPaper2DPlusRepAppearanceState Snapshot;
		Snapshot.Sequence = 1;
		Snapshot.PayloadVersion = 1;
		Snapshot.Appearance.DeliveryMode = ECharacterLayerUsageMode::RuntimeCustomizable;
		for (int32 LayerIndex = 0; LayerIndex < LayerCount; ++LayerIndex)
		{
			Snapshot.Appearance.ActiveLayerIds.Add(FGuid(
				static_cast<uint32>(LayerIndex + 1), 0xA11EA11E, 0xB22EB22E, 0xC33EC33E));
		}
		return Snapshot;
	}

	int32 ProfileEstimateSerializedDescriptorBytes(const FPaper2DPlusRepAppearanceState& Snapshot)
	{
		TArray<uint8> Payload;
		FMemoryWriter Writer(Payload, /*bIsPersistent=*/true);
		FPaper2DPlusRepAppearanceState MutableSnapshot = Snapshot;
		FPaper2DPlusRepAppearanceState::StaticStruct()->SerializeItem(
			Writer, &MutableSnapshot, nullptr);
		return Payload.Num();
	}

	int32 ProfileCountRegisteredVisiblePrimitives(
		const AActor* Actor,
		const UPrimitiveComponent* ExcludedPrimitive = nullptr)
	{
		if (!Actor)
		{
			return 0;
		}
		TInlineComponentArray<UPrimitiveComponent*> Primitives;
		Actor->GetComponents(Primitives);
		int32 Count = 0;
		for (const UPrimitiveComponent* Primitive : Primitives)
		{
			Count += Primitive
				&& Primitive != ExcludedPrimitive
				&& Primitive->IsRegistered()
				&& Primitive->IsVisible() ? 1 : 0;
		}
		return Count;
	}

	struct FPaper2DPlusAppearanceNetworkActor
	{
		TObjectPtr<AActor> Actor = nullptr;
		TObjectPtr<UPaper2DPlusLayerRenderComponent> Appearance = nullptr;
	};

	bool ProfileCreateNetworkActor(
		UWorld* World,
		UPaper2DPlusCharacterLayerAsset* Asset,
		EPaper2DPlusNetContext NetContext,
		bool bDedicatedServer,
		FPaper2DPlusAppearanceNetworkActor& OutActor,
		FString& OutError)
	{
		OutActor = FPaper2DPlusAppearanceNetworkActor();
		if (!World || !World->HasBegunPlay())
		{
			OutError = TEXT("The network probe requires the begun transient production world.");
			return false;
		}
		AActor* Actor = World->SpawnActor<AActor>();
		if (!Actor)
		{
			OutError = TEXT("Failed to spawn a network appearance probe actor.");
			return false;
		}
		Actor->SetReplicates(true);
		USceneComponent* Root = NewObject<USceneComponent>(Actor, NAME_None, RF_Transient);
		Actor->AddInstanceComponent(Root);
		Actor->SetRootComponent(Root);
		Root->RegisterComponentWithWorld(World);

		UPaper2DPlusLayerRenderComponent* Appearance =
			NewObject<UPaper2DPlusLayerRenderComponent>(Actor, NAME_None, RF_Transient);
		Appearance->CharacterLayerAsset = Asset;
		Appearance->bEnableReplication = true;
		Appearance->SetNetContextOverrideForTests(NetContext);
		Appearance->SetDedicatedServerOverrideForTests(bDedicatedServer);
		Actor->AddInstanceComponent(Appearance);
		Appearance->RegisterComponentWithWorld(World);
		if (!Appearance->HasBegunPlay())
		{
			OutError = TEXT("The registered network appearance component did not execute BeginPlay.");
			Actor->Destroy();
			return false;
		}

		OutActor.Actor = Actor;
		OutActor.Appearance = Appearance;
		return true;
	}

	bool ProfileMeasureAuthorityPublicationContract(
		UWorld* World,
		FPaper2DPlusAppearanceProfileRun& OutRun,
		FString& OutError)
	{
		UPaper2DPlusCharacterLayerAsset* Asset = NewObject<UPaper2DPlusCharacterLayerAsset>(
			GetTransientPackage(), NAME_None, RF_Transient | RF_DuplicateTransient);
		Asset->UsageMode = ECharacterLayerUsageMode::RuntimeCustomizable;
		FCharacterLayerExclusiveGroup& BodyChoice = Asset->ExclusiveGroups.AddDefaulted_GetRef();
		BodyChoice.DisplayName = TEXT("Body Choice");
		FCharacterLayer& Body = Asset->Layers.AddDefaulted_GetRef();
		Body.LayerName = TEXT("body");
		Body.ExclusiveGroupId = BodyChoice.GroupId;
		FCharacterLayer& Armor = Asset->Layers.AddDefaulted_GetRef();
		Armor.LayerName = TEXT("armor");
		Armor.ExclusiveGroupId = BodyChoice.GroupId;
		FCharacterLayerAppearancePreset& DefaultPreset = Asset->AppearancePresets.AddDefaulted_GetRef();
		DefaultPreset.DisplayName = TEXT("Default");
		DefaultPreset.ActiveLayerIds = { Body.LayerId };
		Asset->DefaultAppearancePresetId = DefaultPreset.PresetId;
		FCharacterLayerAppearancePreset& ArmorPreset = Asset->AppearancePresets.AddDefaulted_GetRef();
		ArmorPreset.DisplayName = TEXT("Armor");
		ArmorPreset.ActiveLayerIds = { Armor.LayerId };

		TArray<FPaper2DPlusRepAppearanceState> PublishedSnapshots;
		PublishedSnapshots.Reserve(ProfileCharacterCount);
		TArray<FPaper2DPlusAppearanceNetworkActor> Authorities;
		Authorities.Reserve(ProfileCharacterCount);
		TArray<uint16> AuthorityPublishedSequences;
		AuthorityPublishedSequences.Reserve(ProfileCharacterCount);
		int32 ObservedPublishes = 0;
		bool bEveryDedicatedServerSkippedVisuals = true;
		for (int32 CharacterIndex = 0; CharacterIndex < ProfileCharacterCount; ++CharacterIndex)
		{
			FPaper2DPlusAppearanceNetworkActor AuthorityActor;
			if (!ProfileCreateNetworkActor(
				World,
				Asset,
				EPaper2DPlusNetContext::Authority,
				/*bDedicatedServer=*/true,
				AuthorityActor,
				OutError))
			{
				return false;
			}
			UPaper2DPlusLayerRenderComponent* Authority = AuthorityActor.Appearance;
			bEveryDedicatedServerSkippedVisuals &=
				Authority->WouldSkipLayerComponentsOnDedicatedServerForTests()
				&& Authority->GetLayerChildCountForTests() == 0
				&& ProfileCountRegisteredVisiblePrimitives(AuthorityActor.Actor) == 0;

			// BeginPlay publishes the default committed descriptor. Measure the one subsequent logical
			// mutation as an observed sequence delta instead of assuming one publish per setter call.
			const uint16 SequenceBeforeMutation = Authority->GetRepAppearanceForTests().Sequence;
			Authority->ApplyAppearancePreset(ArmorPreset.PresetId);
			const FPaper2DPlusRepAppearanceState& Published = Authority->GetRepAppearanceForTests();
			const uint16 ExpectedSequence = static_cast<uint16>(SequenceBeforeMutation + 1) == 0
				? 1 : static_cast<uint16>(SequenceBeforeMutation + 1);
			if (Published.Sequence != ExpectedSequence
				|| Published.Appearance.ActiveLayerIds != ArmorPreset.ActiveLayerIds)
			{
				OutError = FString::Printf(
					TEXT("Authority %d did not publish exactly one canonical appearance snapshot."),
					CharacterIndex);
				return false;
			}
			++ObservedPublishes;
			PublishedSnapshots.Add(Published);
			AuthorityPublishedSequences.Add(Published.Sequence);
			Authorities.Add(AuthorityActor);
		}

		OutRun.LogicalAuthorityAppearanceChanges = ProfileCharacterCount;
		OutRun.AuthoritySnapshotPublishes = ObservedPublishes;
		OutRun.SimulatedClientCounts = { 0, 1, 4, 32 };
		OutRun.AuthorityPublishesBySimulatedClientCount.Reset(
			OutRun.SimulatedClientCounts.Num());
		OutRun.ClientOnRepApplicationsBySimulatedClientCount.Reset(
			OutRun.SimulatedClientCounts.Num());
		for (int32 SimulatedClientCount : OutRun.SimulatedClientCounts)
		{
			int32 ObservedOnRepApplications = 0;
			for (int32 ClientIndex = 0; ClientIndex < SimulatedClientCount; ++ClientIndex)
			{
				for (const FPaper2DPlusRepAppearanceState& Snapshot : PublishedSnapshots)
				{
					FPaper2DPlusAppearanceNetworkActor ClientActor;
					if (!ProfileCreateNetworkActor(
						World,
						Asset,
						EPaper2DPlusNetContext::SimulatedProxy,
						/*bDedicatedServer=*/false,
						ClientActor,
						OutError))
					{
						return false;
					}
					const uint32 BroadcastsBefore =
						ClientActor.Appearance->Test_GetLayersChangedBroadcastCount();
					ClientActor.Appearance->GetRepAppearanceForTests() = Snapshot;
					ClientActor.Appearance->OnRep_AppearanceState();
					ObservedOnRepApplications +=
						ClientActor.Appearance->GetAppearanceDescriptor() == Snapshot.Appearance
						&& ClientActor.Appearance->Test_GetLayersChangedBroadcastCount()
							== BroadcastsBefore + 1 ? 1 : 0;
					ClientActor.Actor->Destroy();
				}
			}
			OutRun.ClientOnRepApplicationsBySimulatedClientCount.Add(ObservedOnRepApplications);

			int32 AuthoritiesStillPublishedExactlyOnce = 0;
			for (int32 AuthorityIndex = 0; AuthorityIndex < Authorities.Num(); ++AuthorityIndex)
			{
				AuthoritiesStillPublishedExactlyOnce +=
					Authorities[AuthorityIndex].Appearance->GetRepAppearanceForTests().Sequence
						== AuthorityPublishedSequences[AuthorityIndex] ? 1 : 0;
			}
			OutRun.AuthorityPublishesBySimulatedClientCount.Add(
				AuthoritiesStillPublishedExactlyOnce);
		}

		FPaper2DPlusAppearanceNetworkActor LateJoinActor;
		if (!ProfileCreateNetworkActor(
			World,
			Asset,
			EPaper2DPlusNetContext::SimulatedProxy,
			/*bDedicatedServer=*/false,
			LateJoinActor,
			OutError))
		{
			return false;
		}
		UPaper2DPlusLayerRenderComponent* LateJoinClient = LateJoinActor.Appearance;
		const uint32 LateJoinBroadcastsBefore = LateJoinClient->Test_GetLayersChangedBroadcastCount();
		LateJoinClient->GetRepAppearanceForTests() = PublishedSnapshots.Last();
		LateJoinClient->OnRep_AppearanceState();
		OutRun.bLateJoinAppliedLatestSnapshot =
			LateJoinClient->GetAppearanceDescriptor() == PublishedSnapshots.Last().Appearance
			&& LateJoinClient->IsLayerActive(Armor.LayerId)
			&& LateJoinClient->Test_GetLayersChangedBroadcastCount()
				== LateJoinBroadcastsBefore + 1;
		LateJoinActor.Actor->Destroy();
		OutRun.bDedicatedServerAllocatedNoVisualChildren = bEveryDedicatedServerSkippedVisuals;

		bool bEveryFanoutCountStayedFixed =
			OutRun.AuthorityPublishesBySimulatedClientCount.Num()
			== OutRun.SimulatedClientCounts.Num();
		for (int32 PublishCount : OutRun.AuthorityPublishesBySimulatedClientCount)
		{
			bEveryFanoutCountStayedFixed &= PublishCount == ProfileCharacterCount;
		}
		bool bEveryClientOnRepWasObserved =
			OutRun.ClientOnRepApplicationsBySimulatedClientCount.Num()
			== OutRun.SimulatedClientCounts.Num();
		for (int32 Index = 0; Index < OutRun.SimulatedClientCounts.Num()
			&& Index < OutRun.ClientOnRepApplicationsBySimulatedClientCount.Num(); ++Index)
		{
			bEveryClientOnRepWasObserved &=
				OutRun.ClientOnRepApplicationsBySimulatedClientCount[Index]
				== OutRun.SimulatedClientCounts[Index] * ProfileCharacterCount;
		}
		OutRun.bNetworkContractGatePassed =
			OutRun.LogicalAuthorityAppearanceChanges == ProfileCharacterCount
			&& OutRun.AuthoritySnapshotPublishes == ProfileCharacterCount
			&& bEveryFanoutCountStayedFixed
			&& bEveryClientOnRepWasObserved
			&& OutRun.bLateJoinAppliedLatestSnapshot
			&& OutRun.bDedicatedServerAllocatedNoVisualChildren;
		for (FPaper2DPlusAppearanceNetworkActor& Authority : Authorities)
		{
			if (Authority.Actor)
			{
				Authority.Actor->Destroy();
			}
		}
		if (!OutRun.bNetworkContractGatePassed)
		{
			OutError = TEXT("The 200-authority appearance publication contract did not pass.");
			return false;
		}
		return true;
	}

	struct FPaper2DPlusAppearanceProfileProductionMetrics
	{
		int32 ComponentCount = 0;
		int32 FarCount = 0;
		int32 NearCount = 0;
		int32 ChangingCount = 0;
		int32 PeakLiveHandoffsGranted = 0;
		int32 PeakBuildUnitsGranted = 0;
		int32 PeakQueueDepth = 0;
		int32 FinalQueueDepth = 0;
		int32 SchedulerIterations = 0;
		FPaper2DPlusAppearanceCacheStats Cache;
		FPaper2DPlusAppearanceExternalStats External;
		int64 InitializedResourceBytes = 0;
		bool bInitializedResourceBytesAvailable = false;
	};

	int32 ProfileExpectedProductionCompositeKeys(
		EPaper2DPlusAppearanceProfileKeyDistribution Distribution)
	{
		// Every actor first requests Far. The 60 near actors then request the exact-order Near recipe.
		// Shared/repeated appearances reuse those tier keys; all-unique appearances add 60 near keys.
		switch (Distribution)
		{
		case EPaper2DPlusAppearanceProfileKeyDistribution::Shared: return 2;
		case EPaper2DPlusAppearanceProfileKeyDistribution::Repeated: return 40;
		case EPaper2DPlusAppearanceProfileKeyDistribution::AllUnique: return 260;
		default: return 260;
		}
	}

	class FPaper2DPlusAppearanceProfileWorld
	{
	public:
		~FPaper2DPlusAppearanceProfileWorld()
		{
			Shutdown();
		}

		UWorld* GetWorld() const { return World; }

		bool Initialize(FString& OutError)
		{
			if (!GEngine || !FApp::CanEverRender())
			{
				OutError = TEXT("The appearance profile requires a render-capable engine session.");
				return false;
			}

			const FName WorldName = MakeUniqueObjectName(
				nullptr, UWorld::StaticClass(), TEXT("P2DPAppearanceProfileWorld"));
			FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
			World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
			if (!World)
			{
				OutError = TEXT("Failed to create the transient appearance profiling world.");
				return false;
			}
			World->AddToRoot();
			WorldContext.SetCurrentWorld(World);

			if (!CreateCapture(OutError)
				|| !CreateCompositeProofArt(OutError)
				|| !CreateProfileFixture(OutError)
				|| !CreateLayerFixtures(OutError))
			{
				return false;
			}

			FURL Url;
			World->InitializeActorsForPlay(Url);
			World->BeginPlay();
			// A production world normally reaches actor BeginPlay through
			// GameMode -> GameState -> WorldSettings. This isolated profiling world
			// deliberately has no GameInstance/GameMode, so complete that same final
			// dispatch explicitly after the world-subsystem BeginPlay path has run.
			if (!World->HasBegunPlay())
			{
				AWorldSettings* WorldSettings = World->GetWorldSettings();
				if (!WorldSettings)
				{
					OutError = TEXT("The transient appearance profiling world has no WorldSettings.");
					return false;
				}
				WorldSettings->NotifyBeginPlay();
			}
			if (!World->HasBegunPlay())
			{
				OutError = TEXT("The transient appearance profiling world did not complete BeginPlay.");
				return false;
			}
			BudgetSubsystem = World->GetSubsystem<UPaper2DPlusAppearanceBudgetSubsystem>();
			if (!BudgetSubsystem)
			{
				OutError = TEXT("The production appearance budget world subsystem was unavailable.");
				return false;
			}
			FPaper2DPlusAppearanceLocalView View;
			View.Location = FVector::ZeroVector;
			View.Forward = FVector::ForwardVector;
			View.HalfHorizontalFovDegrees = 89.0f;
			BudgetSubsystem->SetLocalViewsOverrideForTests(
				TArray<FPaper2DPlusAppearanceLocalView>{ View });

			FlushRenderingCommands();
			return true;
		}

		bool MeasureMode(
			int32 LayerCount,
			EPaper2DPlusAppearanceProfileKeyDistribution Distribution,
			bool bHybrid,
			const FPaper2DPlusAppearanceBudgetConfig& Config,
			FPaper2DPlusAppearanceProfileModeMetrics& OutMetrics,
			FPaper2DPlusAppearanceProfileProductionMetrics* OutProduction,
			FString& OutError)
		{
			if (!World || !Capture || !CaptureTarget || !BudgetSubsystem)
			{
				OutError = TEXT("The appearance profile world was not initialized.");
				return false;
			}
			UPaper2DPlusCharacterLayerAsset* LayerAsset = LayerFixtures.FindRef(LayerCount);
			if (!LayerAsset)
			{
				OutError = FString::Printf(TEXT("No production fixture exists for %d layers."), LayerCount);
				return false;
			}

			DestroyCharacters();
			FPaper2DPlusAppearanceCompositeCache& Cache = FPaper2DPlusAppearanceCompositeCache::Get();
			Cache.ResetForTests();
			Cache.SetLimitsForTests(MAX_int32, Config.MaxTransientCacheBytes);
			Cache.SetBuildWorkLimitsForTests(
				Config.MaxCompositeBuildUnitsPerFrame,
				Config.MaxCompositeWorkMillisecondsPerFrame);
			Paper2DPlusAppearanceStats::ResetForTests();
			BudgetSubsystem->SetConfigOverrideForTests(Config);
			PeakLiveHandoffsGranted = 0;
			PeakBuildUnitsGranted = 0;
			PeakQueueDepth = 0;
			SchedulerIterations = 0;
			SampleFrameCursor = 0;

			const double ColdStart = FPlatformTime::Seconds();
			if (!CreateCharacters(LayerAsset, LayerCount, Distribution, bHybrid, OutError))
			{
				return false;
			}
			if (bHybrid && !SettleHybrid(Distribution, LayerCount, OutError))
			{
				return false;
			}
			if (!DriveEquivalentChangingSet(bHybrid, LayerCount, OutError))
			{
				return false;
			}
			World->SendAllEndOfFrameUpdates();
			FlushRenderingCommands();
			OutMetrics.ColdActivationWallMilliseconds =
				(FPlatformTime::Seconds() - ColdStart) * 1000.0;
			OutMetrics.RegisteredPrimitiveCount = CountAppearancePrimitives();
			// Cold activation above exercises real descriptor mutation, recipe construction, admission, and the first
			// bounded submissions. Warm timing isolates the steady presentation/policy cost at the same 140/50/10
			// layout; the independent composite-submission and cache gates already measure construction explicitly.
			if (bHybrid)
			{
				Cache.SetBuildWorkLimitsForTests(/*InMaxUnitsPerFrame=*/0, /*InMaxMillisecondsPerFrame=*/0.0);
			}
			const auto RestoreCompositeWorkLimits = [&Cache, &Config, bHybrid]()
			{
				if (bHybrid)
				{
					Cache.SetBuildWorkLimitsForTests(
						Config.MaxCompositeBuildUnitsPerFrame,
						Config.MaxCompositeWorkMillisecondsPerFrame);
				}
			};

			FPaper2DPlusAppearanceProfileSample Warmup;
			if (!MeasureSample(LayerCount, bHybrid, /*PassCount=*/1, Warmup, OutError))
			{
				RestoreCompositeWorkLimits();
				return false;
			}

			TArray<double> GameThreadSamples;
			TArray<double> RenderThreadSamples;
			TArray<double> GpuSamples;
			bool bEveryGpuSampleAvailable = true;
			for (int32 SampleIndex = 0; SampleIndex < ProfileWarmSamples; ++SampleIndex)
			{
				FPaper2DPlusAppearanceProfileSample Sample;
				if (!MeasureSample(LayerCount, bHybrid, ProfilePassesPerSample, Sample, OutError))
				{
					RestoreCompositeWorkLimits();
					return false;
				}
				GameThreadSamples.Add(Sample.GameThreadMilliseconds);
				RenderThreadSamples.Add(Sample.RenderThreadMilliseconds);
				bEveryGpuSampleAvailable &= Sample.bGpuTimingAvailable;
				if (Sample.bGpuTimingAvailable)
				{
					GpuSamples.Add(Sample.GpuMilliseconds);
				}
			}

			OutMetrics.WarmGameThreadMilliseconds = ProfileMedian(MoveTemp(GameThreadSamples));
			OutMetrics.WarmRenderThreadMilliseconds = ProfileMedian(MoveTemp(RenderThreadSamples));
			OutMetrics.bGpuTimingAvailable = bEveryGpuSampleAvailable
				&& GpuSamples.Num() == ProfileWarmSamples;
			OutMetrics.WarmGpuMilliseconds = OutMetrics.bGpuTimingAvailable
				? ProfileMedian(MoveTemp(GpuSamples)) : 0.0;
			RestoreCompositeWorkLimits();
			if (bHybrid && !DiscardChangingPendingResources(OutError))
			{
				return false;
			}
			if (OutProduction)
			{
				CollectProductionMetrics(*OutProduction);
			}
			return true;
		}

		bool MeasureCompositeSubmission(
			int32 LayerCount,
			double& OutMillisecondsPerUnit,
			FString& OutError)
		{
			OutMillisecondsPerUnit = 0.0;
			if (!World || CompositeFrameSprites.Num() != ProfileCompositeFrameCount)
			{
				OutError = TEXT("The representative composite proof art was not initialized.");
				return false;
			}

			FPaper2DPlusAppearanceBuildRecipe Recipe;
			Recipe.LayerAsset = LayerFixtures.FindRef(LayerCount);
			Recipe.CharacterProfile = CharacterProfile;
			Recipe.Flipbook = Flipbook;
			Recipe.Appearance.DeliveryMode = ECharacterLayerUsageMode::RuntimeCustomizable;
			const FCharacterLayerAppearancePreset* DefaultPreset = Recipe.LayerAsset
				? Recipe.LayerAsset->GetAppearancePresetById(Recipe.LayerAsset->DefaultAppearancePresetId)
				: nullptr;
			if (!DefaultPreset)
			{
				OutError = TEXT("The representative Layer fixture has no Default Appearance.");
				return false;
			}
			Recipe.Appearance.ActiveLayerIds = DefaultPreset->ActiveLayerIds;
			Recipe.CanonicalAnimationName = FString::Printf(TEXT("Profile_%dLayers"), LayerCount);
			Recipe.PixelSize = FIntPoint(64, 64);
			Recipe.PivotPixels = FVector2D(32.0, 32.0);
			Recipe.PixelsPerUnrealUnit = 1.0f;
			FPaper2DPlusAppearanceFrameRecipe& Frame = Recipe.Frames.AddDefaulted_GetRef();
			for (int32 LayerIndex = 0; LayerIndex < DefaultPreset->ActiveLayerIds.Num(); ++LayerIndex)
			{
				const FCharacterLayer* Layer = Recipe.LayerAsset->FindLayerById(
					DefaultPreset->ActiveLayerIds[LayerIndex]);
				if (!Layer)
				{
					OutError = TEXT("The representative Default Appearance contains an unknown Layer ID.");
					return false;
				}
				const FString& LayerName = Layer->LayerName;
				Recipe.OrderedBaseLayerNames.Add(LayerName);
				FPaper2DPlusAppearanceLayerDraw& Draw = Frame.BaseDraws.AddDefaulted_GetRef();
				Draw.LayerName = LayerName;
				Draw.Sprite = CompositeFrameSprites[0];
				Draw.PaintOrder = LayerIndex;
			}
			FString AdmissionError;
			if (!FPaper2DPlusAppearanceRenderBackend::ValidateRecipe(Recipe, AdmissionError))
			{
				OutError = FString::Printf(
					TEXT("Representative %d-layer composite admission failed: %s"),
					LayerCount,
					*AdmissionError);
				return false;
			}

			TArray<double> Samples;
			TArray<TSharedPtr<FPaper2DPlusAppearanceBuildSubmission, ESPMode::ThreadSafe>> Submissions;
			Samples.Reserve(ProfileCompositeSubmissionSamples);
			Submissions.Reserve(ProfileCompositeSubmissionSamples);
			for (int32 SampleIndex = 0; SampleIndex < ProfileCompositeSubmissionSamples; ++SampleIndex)
			{
				UPaper2DPlusAppearanceCompositeResource* Resource =
					NewObject<UPaper2DPlusAppearanceCompositeResource>(
						GetTransientPackage(), NAME_None, RF_Transient | RF_DuplicateTransient);
				TSharedPtr<FPaper2DPlusAppearanceBuildSubmission, ESPMode::ThreadSafe> Submission;
				FString BuildError;
				const double Start = FPlatformTime::Seconds();
				const bool bSubmitted = FPaper2DPlusAppearanceRenderBackend::BuildFrame(
					World, Resource, Recipe, 0, Submission, BuildError);
				const double Milliseconds = (FPlatformTime::Seconds() - Start) * 1000.0;
				if (!bSubmitted || !Submission.IsValid())
				{
					OutError = FString::Printf(
						TEXT("Representative %d-layer composite submission failed: %s"),
						LayerCount,
						*BuildError);
					return false;
				}
				Samples.Add(Milliseconds);
				Submissions.Add(MoveTemp(Submission));
			}

			ProfileWaitForGpuOnly();
			for (const TSharedPtr<FPaper2DPlusAppearanceBuildSubmission, ESPMode::ThreadSafe>& Submission : Submissions)
			{
				if (!Submission->DidRenderSucceedAfterGpuIdleForTests())
				{
					OutError = FString::Printf(
						TEXT("Representative %d-layer composite did not complete successfully."), LayerCount);
					return false;
				}
			}
			Samples.Sort();
			OutMillisecondsPerUnit = Samples.Last();
			return OutMillisecondsPerUnit > 0.0;
		}

	private:
		TObjectPtr<UWorld> World = nullptr;
		TObjectPtr<AActor> CaptureOwner = nullptr;
		TObjectPtr<USceneCaptureComponent2D> Capture = nullptr;
		TObjectPtr<UTextureRenderTarget2D> CaptureTarget = nullptr;
		TObjectPtr<UTexture2D> CompositeTexture = nullptr;
		/** Twelve distinct resident sprite objects with identical texture/material/geometry. Distinct identity makes
		 *  every all-live key-frame delivery perform the same real SetSprite swap that authored animation would. */
		TArray<TObjectPtr<UPaperSprite>> CompositeFrameSprites;
		TObjectPtr<UPaperFlipbook> Flipbook = nullptr;
		TObjectPtr<UPaper2DPlusCharacterProfileAsset> CharacterProfile = nullptr;
		TMap<int32, TObjectPtr<UPaper2DPlusCharacterLayerAsset>> LayerFixtures;
		TObjectPtr<UPaper2DPlusAppearanceBudgetSubsystem> BudgetSubsystem = nullptr;
		TArray<FPaper2DPlusAppearanceProfileCharacter> Characters;
		int32 PeakLiveHandoffsGranted = 0;
		int32 PeakBuildUnitsGranted = 0;
		int32 PeakQueueDepth = 0;
		int32 SchedulerIterations = 0;
		int32 SampleFrameCursor = 0;

		bool CreateCapture(FString& OutError)
		{
			CaptureOwner = World->SpawnActor<AActor>();
			if (!CaptureOwner)
			{
				OutError = TEXT("Failed to spawn the profiling scene-capture owner.");
				return false;
			}

			USceneComponent* Root = NewObject<USceneComponent>(CaptureOwner, TEXT("ProfileCaptureRoot"));
			CaptureOwner->AddInstanceComponent(Root);
			CaptureOwner->SetRootComponent(Root);
			Root->SetWorldLocationAndRotation(
				FVector(5250.0, -15000.0, 0.0), FRotator(0.0, 90.0, 0.0));
			Root->RegisterComponentWithWorld(World);

			CaptureTarget = NewObject<UTextureRenderTarget2D>(
				World, TEXT("P2DPAppearanceProfileTarget"), RF_Transient | RF_DuplicateTransient);
			CaptureTarget->RenderTargetFormat = RTF_RGBA8;
			CaptureTarget->ClearColor = FLinearColor::Black;
			CaptureTarget->bAutoGenerateMips = false;
			CaptureTarget->InitAutoFormat(256, 256);
			CaptureTarget->UpdateResourceImmediate(/*bClearRenderTarget=*/true);

			Capture = NewObject<USceneCaptureComponent2D>(CaptureOwner, TEXT("ProfileCapture"));
			CaptureOwner->AddInstanceComponent(Capture);
			Capture->SetupAttachment(Root);
			Capture->TextureTarget = CaptureTarget;
			Capture->ProjectionType = ECameraProjectionMode::Orthographic;
			Capture->OrthoWidth = 12000.0f;
			Capture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
			Capture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_RenderScenePrimitives;
			Capture->bCaptureEveryFrame = false;
			Capture->bCaptureOnMovement = false;
			Capture->PostProcessBlendWeight = 0.0f;
			Capture->RegisterComponentWithWorld(World);
			return true;
		}

		bool CreateProfileFixture(FString& OutError)
		{
			CharacterProfile = NewObject<UPaper2DPlusCharacterProfileAsset>(
				GetTransientPackage(), NAME_None, RF_Transient | RF_DuplicateTransient);
			Flipbook = NewObject<UPaperFlipbook>(
				CharacterProfile, NAME_None, RF_Transient | RF_DuplicateTransient);
			if (!CharacterProfile || !Flipbook
				|| CompositeFrameSprites.Num() != ProfileCompositeFrameCount)
			{
				OutError = TEXT("Failed to create the resident Profile/Flipbook fixture.");
				return false;
			}
			FScopedFlipbookMutator Mutator(Flipbook);
			Mutator.FramesPerSecond = 12.0f;
			for (int32 FrameIndex = 0; FrameIndex < ProfileCompositeFrameCount; ++FrameIndex)
			{
				FPaperFlipbookKeyFrame KeyFrame;
				KeyFrame.FrameRun = 1;
				KeyFrame.Sprite = CompositeFrameSprites[FrameIndex];
				Mutator.KeyFrames.Add(KeyFrame);
			}
			FFlipbookProfileEntry Entry;
			Entry.Identity.FlipbookName = TEXT("ProfileIdle");
			Entry.Identity.Flipbook = Flipbook;
			Entry.CombatData.Frames.SetNum(ProfileCompositeFrameCount);
			CharacterProfile->Flipbooks.Add(MoveTemp(Entry));
			return true;
		}

		bool CreateLayerFixtures(FString& OutError)
		{
			for (int32 LayerCount : { 4, 8, 12 })
			{
				UPaper2DPlusCharacterLayerAsset* Asset =
					NewObject<UPaper2DPlusCharacterLayerAsset>(
						GetTransientPackage(), NAME_None, RF_Transient | RF_DuplicateTransient);
				if (!Asset)
				{
					OutError = TEXT("Failed to create a resident Character Layer fixture.");
					return false;
				}
				Asset->UsageMode = ECharacterLayerUsageMode::RuntimeCustomizable;
				Asset->BaseProfile = CharacterProfile;
				TArray<FGuid> AlwaysActiveLayerIds;
				for (int32 LayerIndex = 0; LayerIndex < LayerCount - 1; ++LayerIndex)
				{
					FCharacterLayer& Layer = Asset->Layers.AddDefaulted_GetRef();
					Layer.LayerName = FString::Printf(TEXT("Layer_%02d"), LayerIndex);
					Layer.RuntimeRenderChannel =
						LayerIndex < 2
							? ECharacterLayerRuntimeRenderChannel::IndependentLive
							: ECharacterLayerRuntimeRenderChannel::BaseComposite;
					FCharacterLayerAnimationMapping& Mapping =
						Layer.AnimationSprites.AddDefaulted_GetRef();
					Mapping.AnimationName = TEXT("ProfileIdle");
					for (int32 FrameIndex = 0; FrameIndex < ProfileCompositeFrameCount; ++FrameIndex)
					{
						Mapping.Sprites.Add(TSoftObjectPtr<UPaperSprite>(CompositeFrameSprites[FrameIndex].Get()));
					}
					AlwaysActiveLayerIds.Add(Layer.LayerId);
				}

				FGuid ChoiceIds[2];
				const TCHAR ChoiceSuffixes[] = { TEXT('A'), TEXT('B') };
				for (int32 ChoiceIndex = 0; ChoiceIndex < 2; ++ChoiceIndex)
				{
					FCharacterLayer& Layer = Asset->Layers.AddDefaulted_GetRef();
					Layer.LayerName = FString::Printf(
						TEXT("Layer_%02d_%c"), LayerCount - 1, ChoiceSuffixes[ChoiceIndex]);
					Layer.RuntimeRenderChannel = ECharacterLayerRuntimeRenderChannel::BaseComposite;
					FCharacterLayerAnimationMapping& Mapping = Layer.AnimationSprites.AddDefaulted_GetRef();
					Mapping.AnimationName = TEXT("ProfileIdle");
					for (int32 FrameIndex = 0; FrameIndex < ProfileCompositeFrameCount; ++FrameIndex)
					{
						Mapping.Sprites.Add(TSoftObjectPtr<UPaperSprite>(CompositeFrameSprites[FrameIndex].Get()));
					}
					ChoiceIds[ChoiceIndex] = Layer.LayerId;
				}

				FCharacterLayerAppearancePreset& DefaultPreset = Asset->AppearancePresets.AddDefaulted_GetRef();
				DefaultPreset.DisplayName = TEXT("Default");
				DefaultPreset.ActiveLayerIds = AlwaysActiveLayerIds;
				DefaultPreset.ActiveLayerIds.Add(ChoiceIds[0]);
				Asset->DefaultAppearancePresetId = DefaultPreset.PresetId;
				FCharacterLayerAppearancePreset& AlternatePreset = Asset->AppearancePresets.AddDefaulted_GetRef();
				AlternatePreset.DisplayName = TEXT("Alternate");
				AlternatePreset.ActiveLayerIds = AlwaysActiveLayerIds;
				AlternatePreset.ActiveLayerIds.Add(ChoiceIds[1]);
				LayerFixtures.Add(LayerCount, Asset);
			}
			return true;
		}

		bool CreateCompositeProofArt(FString& OutError)
		{
			UMaterialInterface* SpriteMaterial = LoadObject<UMaterialInterface>(
				nullptr, TEXT("/Paper2D/TranslucentUnlitSpriteMaterial.TranslucentUnlitSpriteMaterial"));
			if (!SpriteMaterial)
			{
				OutError = TEXT("The standard resident Paper2D translucent material was unavailable.");
				return false;
			}

			const FName TextureName = MakeUniqueObjectName(
				GetTransientPackage(), UTexture2D::StaticClass(), TEXT("P2DPAppearanceProfileTexture"));
			CompositeTexture = UTexture2D::CreateTransient(64, 64, PF_B8G8R8A8, TextureName);
			if (!CompositeTexture || !CompositeTexture->GetPlatformData()
				|| CompositeTexture->GetPlatformData()->Mips.Num() == 0)
			{
				OutError = TEXT("Failed to create the representative resident sprite texture.");
				return false;
			}
			CompositeTexture->SRGB = false;
			CompositeTexture->Filter = TF_Nearest;
			CompositeTexture->LODGroup = TEXTUREGROUP_Pixels2D;
			FTexture2DMipMap& Mip = CompositeTexture->GetPlatformData()->Mips[0];
			void* TextureData = Mip.BulkData.Lock(LOCK_READ_WRITE);
			TArray<FColor> Pixels;
			Pixels.Init(FColor(255, 255, 255, 128), 64 * 64);
			FMemory::Memcpy(TextureData, Pixels.GetData(), Pixels.Num() * sizeof(FColor));
			Mip.BulkData.Unlock();
			CompositeTexture->UpdateResource();

			FSpriteAssetInitParameters Init;
			Init.Texture = CompositeTexture;
			Init.Offset = FIntPoint::ZeroValue;
			Init.Dimension = FIntPoint(64, 64);
			Init.SetPixelsPerUnrealUnit(1.0f);
			Init.DefaultMaterialOverride = SpriteMaterial;

			CompositeFrameSprites.Reset(ProfileCompositeFrameCount);
			TSet<const UPaperSprite*> DistinctSprites;
			const FName ReferenceName = MakeUniqueObjectName(
				GetTransientPackage(),
				UPaperSprite::StaticClass(),
				TEXT("P2DPAppearanceProfileSprite_00"));
			UPaperSprite* ReferenceSprite = NewObject<UPaperSprite>(
				GetTransientPackage(), ReferenceName, RF_Transient | RF_DuplicateTransient);
			if (!ReferenceSprite)
			{
				OutError = TEXT("Failed to create the reference resident sprite frame.");
				return false;
			}
			ReferenceSprite->InitializeSprite(Init, /*bRebuildData=*/true);
			ReferenceSprite->AlternateMaterialSplitIndex = INDEX_NONE;
			if (ReferenceSprite->BakedRenderData.Num() == 0
				|| ReferenceSprite->BakedRenderData.Num() % 3 != 0)
			{
				OutError = TEXT("InitializeSprite did not generate reference cooked triangle data.");
				return false;
			}
			if (ReferenceSprite->GetSourceTexture() != CompositeTexture
				|| ReferenceSprite->GetBakedTexture() != CompositeTexture
				|| ReferenceSprite->GetDefaultMaterial() != SpriteMaterial
				|| ReferenceSprite->GetSourceUV() != FVector2D::ZeroVector
				|| ReferenceSprite->GetSourceSize() != FVector2D(64.0, 64.0)
				|| ReferenceSprite->GetPixelsPerUnrealUnit() != 1.0f)
			{
				OutError = TEXT("The reference sprite does not match the intended resident source/material contract.");
				return false;
			}
			CompositeFrameSprites.Add(ReferenceSprite);
			DistinctSprites.Add(ReferenceSprite);

			auto HasExactBakedGeometry = [](const UPaperSprite* A, const UPaperSprite* B)
			{
				return A && B
					&& A->BakedRenderData.Num() == B->BakedRenderData.Num()
					&& (A->BakedRenderData.Num() == 0
						|| FMemory::Memcmp(
							A->BakedRenderData.GetData(),
							B->BakedRenderData.GetData(),
							A->BakedRenderData.Num() * sizeof(FVector4)) == 0);
			};

			for (int32 FrameIndex = 1; FrameIndex < ProfileCompositeFrameCount; ++FrameIndex)
			{
				const FName SpriteName = MakeUniqueObjectName(
					GetTransientPackage(),
					UPaperSprite::StaticClass(),
					*FString::Printf(TEXT("P2DPAppearanceProfileSprite_%02d"), FrameIndex));
				// Duplicate the initialized reference instead of rebuilding eleven more sprites. RebuildRenderData may emit
				// semantically-equivalent vertex arrays that do not compare byte-identically; duplication pins the complete
				// source/pivot/material/baked contract while retaining the distinct UObject identity SetSprite observes.
				UPaperSprite* FrameSprite = DuplicateObject<UPaperSprite>(
					ReferenceSprite, GetTransientPackage(), SpriteName);
				if (!FrameSprite)
				{
					OutError = FString::Printf(
						TEXT("Failed to create representative resident sprite frame %d."), FrameIndex);
					return false;
				}
				FrameSprite->SetFlags(RF_Transient | RF_DuplicateTransient);
				FVector2D ReferenceCustomPivot;
				FVector2D FrameCustomPivot;
				const ESpritePivotMode::Type ReferencePivotMode =
					ReferenceSprite->GetPivotMode(ReferenceCustomPivot);
				const ESpritePivotMode::Type FramePivotMode = FrameSprite->GetPivotMode(FrameCustomPivot);
				if (FrameSprite->GetSourceTexture() != ReferenceSprite->GetSourceTexture()
					|| FrameSprite->GetBakedTexture() != ReferenceSprite->GetBakedTexture()
					|| FrameSprite->GetDefaultMaterial() != ReferenceSprite->GetDefaultMaterial()
					|| FrameSprite->GetAlternateMaterial() != ReferenceSprite->GetAlternateMaterial()
					|| FrameSprite->GetSourceUV() != ReferenceSprite->GetSourceUV()
					|| FrameSprite->GetSourceSize() != ReferenceSprite->GetSourceSize()
					|| FrameSprite->GetPixelsPerUnrealUnit() != ReferenceSprite->GetPixelsPerUnrealUnit()
					|| FramePivotMode != ReferencePivotMode
					|| FrameCustomPivot != ReferenceCustomPivot
					|| FrameSprite->GetPivotPosition() != ReferenceSprite->GetPivotPosition()
					|| FrameSprite->AlternateMaterialSplitIndex
						!= ReferenceSprite->AlternateMaterialSplitIndex
					|| !HasExactBakedGeometry(FrameSprite, ReferenceSprite))
				{
					OutError = FString::Printf(
						TEXT("Duplicated sprite frame %d does not exactly match the reference texture/material/source/pivot/baked contract."),
						FrameIndex);
					return false;
				}
				if (DistinctSprites.Contains(FrameSprite))
				{
					OutError = TEXT("The animated sprite fixture reused a frame object pointer.");
					return false;
				}
				DistinctSprites.Add(FrameSprite);
				CompositeFrameSprites.Add(FrameSprite);
			}
			if (DistinctSprites.Num() != ProfileCompositeFrameCount)
			{
				OutError = FString::Printf(
					TEXT("The animated sprite fixture created %d distinct frames; expected %d."),
					DistinctSprites.Num(),
					ProfileCompositeFrameCount);
				return false;
			}
			FlushRenderingCommands();
			return true;
		}

		bool CreateCharacters(
			UPaper2DPlusCharacterLayerAsset* Asset,
			int32 LayerCount,
			EPaper2DPlusAppearanceProfileKeyDistribution Distribution,
			bool bHybrid,
			FString& OutError)
		{
			const int32 UniqueKeyCount = ProfileUniqueKeyCount(Distribution);
			TArray<TObjectPtr<UPaper2DPlusCharacterLayerAsset>> KeyAssets;
			KeyAssets.Reserve(UniqueKeyCount);
			KeyAssets.Add(Asset);
			for (int32 KeyIndex = 1; KeyIndex < UniqueKeyCount; ++KeyIndex)
			{
				const FName DuplicateName = MakeUniqueObjectName(
					GetTransientPackage(),
					UPaper2DPlusCharacterLayerAsset::StaticClass(),
					TEXT("P2DPAppearanceProfileLayers"));
				UPaper2DPlusCharacterLayerAsset* KeyAsset = DuplicateObject<UPaper2DPlusCharacterLayerAsset>(
					Asset, GetTransientPackage(), DuplicateName);
				if (!KeyAsset)
				{
					OutError = FString::Printf(TEXT("Failed to duplicate appearance key asset %d."), KeyIndex);
					return false;
				}
				KeyAssets.Add(KeyAsset);
			}

			Characters.Reserve(ProfileCharacterCount);
			for (int32 ActorIndex = 0; ActorIndex < ProfileCharacterCount; ++ActorIndex)
			{
				const float Distance = ActorIndex < ProfileFarCount
					? 10000.0f : (ActorIndex < ProfileFarCount + ProfileNearCount ? 1000.0f : 500.0f);
				const float Height = (static_cast<float>(ActorIndex % 20) - 9.5f) * 80.0f;
				AActor* Actor = World->SpawnActor<AActor>();
				if (!Actor)
				{
					OutError = FString::Printf(TEXT("Failed to spawn production character %d."), ActorIndex);
					return false;
				}
				USceneComponent* Root = NewObject<USceneComponent>(Actor, NAME_None, RF_Transient);
				Actor->AddInstanceComponent(Root);
				Actor->SetRootComponent(Root);
				Root->SetWorldLocation(FVector(Distance, 0.0f, Height));
				Root->RegisterComponentWithWorld(World);

				UPaper2DPlusFlipbookComponent* Driver =
					NewObject<UPaper2DPlusFlipbookComponent>(Actor, NAME_None, RF_Transient);
				Actor->AddInstanceComponent(Driver);
				Driver->SetupAttachment(Root);
				Driver->SetFlipbook(Flipbook);
				Driver->SetVisibility(false, true);
				Driver->SetComponentTickEnabled(false);
				Driver->RegisterComponentWithWorld(World);

				UPaper2DPlusCharacterProfileComponent* ProfileComponent =
					NewObject<UPaper2DPlusCharacterProfileComponent>(Actor, NAME_None, RF_Transient);
				ProfileComponent->CharacterProfile = CharacterProfile;
				ProfileComponent->FlipbookComponent = Driver;
				Actor->AddInstanceComponent(ProfileComponent);
				ProfileComponent->RegisterComponentWithWorld(World);

				const int32 KeyIndex = ProfileKeyIndex(Distribution, ActorIndex);
				UPaper2DPlusCharacterLayerAsset* CharacterLayerAsset = KeyAssets[KeyIndex];
				if (!CharacterLayerAsset->AppearancePresets.IsValidIndex(1))
				{
					OutError = TEXT("A profiling Layer fixture lost its alternate Appearance Preset.");
					return false;
				}
				UPaper2DPlusLayerRenderComponent* Appearance =
					NewObject<UPaper2DPlusLayerRenderComponent>(Actor, NAME_None, RF_Transient);
				Appearance->CharacterLayerAsset = CharacterLayerAsset;
				Appearance->ProfileComponent = ProfileComponent;
				Appearance->bEnableHybridRuntimeRenderer = bHybrid;
				Actor->AddInstanceComponent(Appearance);
				Appearance->RegisterComponentWithWorld(World);
				Appearance->Test_HandleFrameChanged(0);
				if (!Appearance->HasBegunPlay())
				{
					OutError = FString::Printf(
						TEXT("Production appearance component %d did not execute BeginPlay."), ActorIndex);
					return false;
				}

				FPaper2DPlusAppearanceProfileCharacter& Character =
					Characters.AddDefaulted_GetRef();
				Character.Actor = Actor;
				Character.Flipbook = Driver;
				Character.Profile = ProfileComponent;
				Character.Appearance = Appearance;
				Character.ChangingPresetB = CharacterLayerAsset->AppearancePresets[1].PresetId;
			}
			return Characters.Num() == ProfileCharacterCount;
		}

		void RecordSchedulerStats()
		{
			const FPaper2DPlusAppearanceStatsSnapshot Stats = BudgetSubsystem->GetStatsSnapshot();
			PeakLiveHandoffsGranted = FMath::Max(PeakLiveHandoffsGranted, Stats.LiveHandoffsGranted);
			PeakBuildUnitsGranted = FMath::Max(
				PeakBuildUnitsGranted, Stats.CompositeBuildUnitsGranted);
			PeakQueueDepth = FMath::Max(PeakQueueDepth, Stats.CompositeQueueDepth);
		}

		void EvaluateScheduler()
		{
			++GFrameCounter;
			BudgetSubsystem->EvaluateNowForTests();
			++SchedulerIterations;
			RecordSchedulerStats();
		}

		int32 CountAppearancePrimitives() const
		{
			int32 Count = 0;
			for (const FPaper2DPlusAppearanceProfileCharacter& Character : Characters)
			{
				Count += ProfileCountRegisteredVisiblePrimitives(
					Character.Actor, Character.Flipbook);
			}
			return Count;
		}

		FString DescribeHybridFailure() const
		{
			const FPaper2DPlusAppearanceCompositeCache& Cache =
				FPaper2DPlusAppearanceCompositeCache::Get();
			const FPaper2DPlusAppearanceCacheStats CacheStats = Cache.GetStats();
			const FPaper2DPlusAppearanceScratchStats ScratchStats =
				FPaper2DPlusAppearanceRenderBackend::GetScratchStats();

			TSet<const UPaper2DPlusAppearanceCompositeResource*> UniquePendingResources;
			int32 PendingComponents = 0;
			int32 PreparedPendingComponents = 0;
			int32 InvalidPendingComponents = 0;
			int32 ComponentsWithErrors = 0;
			int32 SupportedComponents = 0;
			int32 AdmissibleComponents = 0;
			const UPaper2DPlusLayerRenderComponent* FirstPendingComponent = nullptr;
			const UPaper2DPlusLayerRenderComponent* FirstChangingComponent = nullptr;
			for (const FPaper2DPlusAppearanceProfileCharacter& Character : Characters)
			{
				const UPaper2DPlusLayerRenderComponent* Appearance = Character.Appearance;
				if (!Appearance)
				{
					continue;
				}
				SupportedComponents += Appearance->IsAppearanceCompositeSupportedForTests() ? 1 : 0;
				AdmissibleComponents += Appearance->CanAdmitAppearanceCompositeForTests() ? 1 : 0;
				ComponentsWithErrors += Appearance->GetAppearanceCompositeErrorForTests().IsEmpty() ? 0 : 1;
				const UPaper2DPlusAppearanceCompositeResource* Pending =
					Appearance->GetPendingCompositeResourceForTests();
				if (Pending)
				{
					++PendingComponents;
					UniquePendingResources.Add(Pending);
					PreparedPendingComponents += Pending->IsPrepared() ? 1 : 0;
					InvalidPendingComponents += Pending->IsInvalidated() ? 1 : 0;
					FirstPendingComponent = FirstPendingComponent ? FirstPendingComponent : Appearance;
				}
				if (!FirstChangingComponent && Appearance->IsAppearanceChangingForTests())
				{
					FirstChangingComponent = Appearance;
				}
			}

			auto DescribeComponent = [&Cache](const UPaper2DPlusLayerRenderComponent* Appearance)
			{
				if (!Appearance)
				{
					return FString(TEXT("none"));
				}
				const UPaper2DPlusAppearanceCompositeResource* Pending =
					Appearance->GetPendingCompositeResourceForTests();
				const FPaper2DPlusAppearanceCacheEntryDiagnostics Entry =
					Cache.GetEntryDiagnosticsForTests(Pending);
				const FPaper2DPlusAppearanceBudgetDecision& Decision =
					Appearance->GetLastAppearanceDecisionForTests();
				return FString::Printf(
					TEXT("pending=%d indexed=%d recipe=%d ready=%d/%d claimed=%d queued=%d submissions=%d "
						"next=%d invalid=%d prepared=%d tier=%s pendingTier=%s reason=%s grant=%d release=%d "
						"request=%llu error='%s'"),
					Pending ? 1 : 0,
					Entry.bIndexed ? 1 : 0,
					Entry.bHasPendingRecipe ? 1 : 0,
					Entry.ReadyFrames,
					Entry.FrameCount,
					Entry.ClaimedFrames,
					Entry.QueuedFrames,
					Entry.PendingSubmissions,
					Entry.NextUnbuiltFrame,
					Pending && Pending->IsInvalidated() ? 1 : 0,
					Pending && Pending->IsPrepared() ? 1 : 0,
					Paper2DPlusAppearanceBudget::LexToString(Decision.Tier),
					Paper2DPlusAppearanceBudget::LexToString(Appearance->GetPendingCompositeTierForTests()),
					Paper2DPlusAppearanceBudget::LexToString(Decision.PendingReason),
					Decision.GrantedCompositeBuildUnits,
					Decision.bReleaseVisuals ? 1 : 0,
					static_cast<unsigned long long>(Appearance->GetAppearanceRequestSequenceForTests()),
					*Appearance->GetAppearanceCompositeErrorForTests());
			};

			return FString::Printf(
				TEXT("cache{resident=%d pending=%d bytes=%llu admissions=%llu preparedHits=%llu sharedHits=%llu "
					"evictions=%llu invalidations=%llu rejected=%llu units=%d workMs=%.6f} "
					"backend{framesSubmitted=%llu scratchRequests=%llu} "
					"components{pending=%d uniquePending=%d preparedPending=%d invalidPending=%d errors=%d "
					"supported=%d admissible=%d} first{%s} changing{%s}"),
				CacheStats.ResidentEntries,
				CacheStats.PendingEntries,
				static_cast<unsigned long long>(CacheStats.CacheReservedBytes),
				static_cast<unsigned long long>(CacheStats.Admissions),
				static_cast<unsigned long long>(CacheStats.PreparedHits),
				static_cast<unsigned long long>(CacheStats.SharedPendingHits),
				static_cast<unsigned long long>(CacheStats.Evictions),
				static_cast<unsigned long long>(CacheStats.Invalidations),
				static_cast<unsigned long long>(CacheStats.RejectedAdmissions),
				CacheStats.BuildUnitsClaimedThisFrame,
				CacheStats.BuildWorkMillisecondsMeasuredThisFrame,
				static_cast<unsigned long long>(ScratchStats.FramesSubmitted),
				static_cast<unsigned long long>(ScratchStats.TransientScratchRequests),
				PendingComponents,
				UniquePendingResources.Num(),
				PreparedPendingComponents,
				InvalidPendingComponents,
				ComponentsWithErrors,
				SupportedComponents,
				AdmissibleComponents,
				*DescribeComponent(FirstPendingComponent),
				*DescribeComponent(FirstChangingComponent));
		}

		bool IsStableHybridLayout() const
		{
			if (Characters.Num() != ProfileCharacterCount)
			{
				return false;
			}
			for (int32 ActorIndex = 0; ActorIndex < Characters.Num(); ++ActorIndex)
			{
				const FPaper2DPlusAppearanceProfileCharacter& Character = Characters[ActorIndex];
				const bool bFar = ActorIndex < ProfileFarCount;
				const EPaper2DPlusAppearanceTier ExpectedTier = bFar
					? EPaper2DPlusAppearanceTier::FarComposite
					: EPaper2DPlusAppearanceTier::NearComposite;
				const int32 ExpectedPrimitives = bFar ? 1 : 3;
				if (Character.Appearance->GetAppearanceTier() != ExpectedTier
					|| ProfileCountRegisteredVisiblePrimitives(
						Character.Actor, Character.Flipbook) != ExpectedPrimitives
					|| !Character.Appearance->GetActiveCompositeResourceForTests())
				{
					return false;
				}
			}
			return true;
		}

		bool SettleHybrid(
			EPaper2DPlusAppearanceProfileKeyDistribution Distribution,
			int32 LayerCount,
			FString& OutError)
		{
			const int32 ExpectedKeys = ProfileExpectedProductionCompositeKeys(Distribution);
			const int32 MaximumIterations = ExpectedKeys * ProfileCompositeFrameCount * 4 + 512;
			for (int32 Iteration = 0; Iteration < MaximumIterations; ++Iteration)
			{
				EvaluateScheduler();
				if ((Iteration & 7) == 7)
				{
					// These are compressed logical scheduler frames inside one automation callback,
					// not engine-rendered frames. Give the proof a bounded hard GPU drain, then
					// substitute that explicit idle proof for frame-delayed generic D3D11 fences.
					// Runtime scheduler frames remain nonblocking and production-fence-polled.
					ProfileWaitForGpuOnly();
					FPaper2DPlusAppearanceCompositeCache::Get()
						.PollCompletedFramesAfterGpuIdleForTests();
				}
				if (IsStableHybridLayout())
				{
					return true;
				}
			}
			ProfileWaitForGpuOnly();
			FPaper2DPlusAppearanceCompositeCache::Get()
				.PollCompletedFramesAfterGpuIdleForTests();
			EvaluateScheduler();
			if (!IsStableHybridLayout())
			{
				const FPaper2DPlusAppearanceStatsSnapshot Stats = BudgetSubsystem->GetStatsSnapshot();
				OutError = FString::Printf(
					TEXT("Production hybrid %d/%s did not settle after %d scheduler frames "
						"(far=%d near=%d changing=%d queue=%d primitives=%d). %s"),
					LayerCount,
					FPaper2DPlusAppearanceProfileHarness::DistributionName(Distribution),
					SchedulerIterations,
					Stats.FarCompositeCount,
					Stats.NearCompositeCount,
					Stats.ChangingLiveCount,
					Stats.CompositeQueueDepth,
					CountAppearancePrimitives(),
					*DescribeHybridFailure());
				return false;
			}
			return true;
		}

		bool DriveEquivalentChangingSet(bool bHybrid, int32 LayerCount, FString& OutError)
		{
			for (int32 ActorIndex = ProfileFarCount + ProfileNearCount;
				ActorIndex < Characters.Num(); ++ActorIndex)
			{
				FPaper2DPlusAppearanceProfileCharacter& Character = Characters[ActorIndex];
				Character.Appearance->ApplyAppearancePreset(Character.ChangingPresetB);
			}
			if (bHybrid)
			{
				EvaluateScheduler();
			}

			const int32 ExpectedPrimitives = bHybrid
				? ProfileFarCount + (ProfileNearCount * 3) + (ProfileChangingCount * LayerCount)
				: ProfileCharacterCount * LayerCount;
			const int32 ActualPrimitives = CountAppearancePrimitives();
			if (ActualPrimitives != ExpectedPrimitives)
			{
				OutError = FString::Printf(
					TEXT("Production %s layout registered %d visible primitives; expected %d."),
					bHybrid ? TEXT("hybrid") : TEXT("all-live"),
					ActualPrimitives,
					ExpectedPrimitives);
				return false;
			}
			return true;
		}

		bool DiscardChangingPendingResources(FString& OutError)
		{
			// The B selection is descriptor-distinct but render-equivalent. Warm timing holds the exact live-handoff
			// layout while the cache work gate is paused; retire only that pending B work afterward so the settled A
			// Far/Near resources remain the report's residency authority.
			TSet<UPaper2DPlusAppearanceCompositeResource*> ChangingPendingResources;
			for (int32 ActorIndex = ProfileFarCount + ProfileNearCount;
				ActorIndex < Characters.Num(); ++ActorIndex)
			{
				UPaper2DPlusLayerRenderComponent* Appearance = Characters[ActorIndex].Appearance;
				UPaper2DPlusAppearanceCompositeResource* Pending = Appearance
					? Appearance->GetPendingCompositeResourceForTests() : nullptr;
				const UPaper2DPlusAppearanceCompositeResource* Active = Appearance
					? Appearance->GetActiveCompositeResourceForTests() : nullptr;
				if (!Pending || Pending == Active || Pending->IsPrepared() || Pending->IsInvalidated())
				{
					OutError = FString::Printf(
						TEXT("Changing actor %d did not retain a distinct pending cache-miss resource."),
						ActorIndex);
					return false;
				}
				ChangingPendingResources.Add(Pending);
			}
			for (UPaper2DPlusAppearanceCompositeResource* Pending : ChangingPendingResources)
			{
				FPaper2DPlusAppearanceCompositeCache::Get().InvalidateResource(Pending);
			}
			return true;
		}

		bool MeasureSample(
			int32 LayerCount,
			bool bHybrid,
			int32 PassCount,
			FPaper2DPlusAppearanceProfileSample& OutSample,
			FString& OutError)
		{
			if (PassCount <= 0)
			{
				OutError = TEXT("Appearance profile pass count must be positive.");
				return false;
			}

			TSharedRef<FPaper2DPlusAppearanceProfileTimestampState, ESPMode::ThreadSafe> Timing =
				MakeShared<FPaper2DPlusAppearanceProfileTimestampState, ESPMode::ThreadSafe>();
			ENQUEUE_RENDER_COMMAND(Paper2DPlusAppearanceProfileBegin)(
				[Timing](FRHICommandListImmediate& RHICmdList)
				{
					Timing->RenderStartCycles = FPlatformTime::Cycles64();
					Timing->bGpuTimingSupported = GSupportsTimestampRenderQueries && GDynamicRHI != nullptr;
					if (Timing->bGpuTimingSupported)
					{
						Timing->GpuStartQuery = GDynamicRHI->RHICreateRenderQuery(RQT_AbsoluteTime);
						Timing->GpuEndQuery = GDynamicRHI->RHICreateRenderQuery(RQT_AbsoluteTime);
						Timing->bGpuTimingSupported = Timing->GpuStartQuery.IsValid()
							&& Timing->GpuEndQuery.IsValid();
						if (Timing->bGpuTimingSupported)
						{
							RHICmdList.EndRenderQuery(Timing->GpuStartQuery);
						}
					}
				});

			double GameThreadSeconds = 0.0;
			for (int32 PassIndex = 0; PassIndex < PassCount; ++PassIndex)
			{
				const double GameThreadStart = FPlatformTime::Seconds();
				if (bHybrid)
				{
					EvaluateScheduler();
				}
				const int32 FrameIndex = SampleFrameCursor++ % ProfileCompositeFrameCount;
				for (FPaper2DPlusAppearanceProfileCharacter& Character : Characters)
				{
					Character.Appearance->Test_HandleFrameChanged(FrameIndex);
				}
				World->SendAllEndOfFrameUpdates();
				GameThreadSeconds += FPlatformTime::Seconds() - GameThreadStart;
				Capture->CaptureScene();
			}
			const int32 ExpectedPrimitives = bHybrid
				? ProfileFarCount + (ProfileNearCount * 3) + (ProfileChangingCount * LayerCount)
				: ProfileCharacterCount * LayerCount;
			const int32 ActualPrimitives = CountAppearancePrimitives();
			if (ActualPrimitives != ExpectedPrimitives)
			{
				OutError = FString::Printf(
					TEXT("Warmed %s layout registered %d visible primitives; expected %d."),
					bHybrid ? TEXT("hybrid") : TEXT("all-live"),
					ActualPrimitives,
					ExpectedPrimitives);
				return false;
			}

			ENQUEUE_RENDER_COMMAND(Paper2DPlusAppearanceProfileEnd)(
				[Timing](FRHICommandListImmediate& RHICmdList)
				{
					if (Timing->bGpuTimingSupported)
					{
						RHICmdList.EndRenderQuery(Timing->GpuEndQuery);
					}
					Timing->RenderEndCycles = FPlatformTime::Cycles64();
#if UE_VERSION_OLDER_THAN(5, 5, 0)
					RHICmdList.BlockUntilGPUIdle();
#else
					RHICmdList.SubmitAndBlockUntilGPUIdle();
#endif
					RHICmdList.ImmediateFlush(EImmediateFlushType::FlushRHIThread);
					if (Timing->bGpuTimingSupported)
					{
#if UE_VERSION_OLDER_THAN(5, 1, 0)
						const bool bReadStart = RHICmdList.GetRenderQueryResult(
							Timing->GpuStartQuery, Timing->GpuStartMicroseconds, true);
						const bool bReadEnd = RHICmdList.GetRenderQueryResult(
							Timing->GpuEndQuery, Timing->GpuEndMicroseconds, true);
#else
						const bool bReadStart = RHIGetRenderQueryResult(
							Timing->GpuStartQuery, Timing->GpuStartMicroseconds, true);
						const bool bReadEnd = RHIGetRenderQueryResult(
							Timing->GpuEndQuery, Timing->GpuEndMicroseconds, true);
#endif
						Timing->bGpuTimingRead = bReadStart && bReadEnd
							&& Timing->GpuEndMicroseconds >= Timing->GpuStartMicroseconds;
					}
					Timing->GpuStartQuery.SafeRelease();
					Timing->GpuEndQuery.SafeRelease();
				});
			FlushRenderingCommands();

			if (Timing->RenderEndCycles < Timing->RenderStartCycles)
			{
				OutError = TEXT("Render-thread timing markers completed out of order.");
				return false;
			}

			OutSample.GameThreadMilliseconds = GameThreadSeconds * 1000.0 / PassCount;
			OutSample.RenderThreadMilliseconds = FPlatformTime::ToSeconds64(
				Timing->RenderEndCycles - Timing->RenderStartCycles) * 1000.0 / PassCount;
			OutSample.bGpuTimingAvailable = Timing->bGpuTimingRead;
			OutSample.GpuMilliseconds = Timing->bGpuTimingRead
				? static_cast<double>(Timing->GpuEndMicroseconds - Timing->GpuStartMicroseconds)
					/ 1000.0 / PassCount
				: 0.0;
			return true;
		}

		void CollectProductionMetrics(FPaper2DPlusAppearanceProfileProductionMetrics& OutMetrics) const
		{
			OutMetrics = FPaper2DPlusAppearanceProfileProductionMetrics();
			OutMetrics.ComponentCount = Characters.Num();
			for (const FPaper2DPlusAppearanceProfileCharacter& Character : Characters)
			{
				switch (Character.Appearance->GetAppearanceTier())
				{
				case EPaper2DPlusAppearanceTier::FarComposite: ++OutMetrics.FarCount; break;
				case EPaper2DPlusAppearanceTier::NearComposite: ++OutMetrics.NearCount; break;
				case EPaper2DPlusAppearanceTier::ChangingLive: ++OutMetrics.ChangingCount; break;
				default: break;
				}
			}
			OutMetrics.PeakLiveHandoffsGranted = PeakLiveHandoffsGranted;
			OutMetrics.PeakBuildUnitsGranted = PeakBuildUnitsGranted;
			OutMetrics.PeakQueueDepth = PeakQueueDepth;
			OutMetrics.FinalQueueDepth = BudgetSubsystem->GetStatsSnapshot().CompositeQueueDepth;
			OutMetrics.SchedulerIterations = SchedulerIterations;
			OutMetrics.Cache = FPaper2DPlusAppearanceCompositeCache::Get().GetStats();
			OutMetrics.External = Paper2DPlusAppearanceStats::Snapshot();

			TSet<const UPaper2DPlusAppearanceCompositeResource*> UniqueResources;
			for (const FPaper2DPlusAppearanceProfileCharacter& Character : Characters)
			{
				if (const UPaper2DPlusAppearanceCompositeResource* Resource =
					Character.Appearance->GetActiveCompositeResourceForTests())
				{
					UniqueResources.Add(Resource);
				}
			}
			uint64 ResourceBytes = 0;
			for (const UPaper2DPlusAppearanceCompositeResource* Resource : UniqueResources)
			{
				for (int32 FrameIndex = 0; FrameIndex < Resource->GetFrameCount(); ++FrameIndex)
				{
					if (UTextureRenderTarget2D* Target = Resource->GetFrameTarget(FrameIndex))
					{
						ResourceBytes += static_cast<uint64>(
							Target->GetResourceSizeBytes(EResourceSizeMode::Exclusive));
					}
				}
			}
			OutMetrics.InitializedResourceBytes = static_cast<int64>(
				FMath::Min<uint64>(ResourceBytes, static_cast<uint64>(MAX_int64)));
			OutMetrics.bInitializedResourceBytesAvailable = ResourceBytes > 0;
		}

		void DestroyCharacters()
		{
			for (FPaper2DPlusAppearanceProfileCharacter& Character : Characters)
			{
				if (Character.Actor)
				{
					Character.Actor->Destroy();
				}
			}
			Characters.Reset();
			if (World)
			{
				World->SendAllEndOfFrameUpdates();
			}
			FlushRenderingCommands();
		}

		void Shutdown()
		{
			if (!World)
			{
				return;
			}
			DestroyCharacters();
			if (BudgetSubsystem)
			{
				BudgetSubsystem->SetLocalViewsOverrideForTests(TOptional<TArray<FPaper2DPlusAppearanceLocalView>>());
				BudgetSubsystem->SetConfigOverrideForTests(TOptional<FPaper2DPlusAppearanceBudgetConfig>());
			}
			if (Capture && Capture->IsRegistered())
			{
				Capture->UnregisterComponent();
			}
			if (CaptureTarget)
			{
				CaptureTarget->ReleaseResource();
			}
			FlushRenderingCommands();

			UWorld* LocalWorld = World;
			World = nullptr;
			if (LocalWorld->HasBegunPlay())
			{
#if UE_VERSION_OLDER_THAN(5, 5, 0)
				// UWorld::EndPlay became public in 5.5. Match the older GameEngine
				// teardown path so every begun actor still receives paired EndPlay.
				LocalWorld->BeginTearingDown();
				for (FActorIterator ActorIt(LocalWorld); ActorIt; ++ActorIt)
				{
					ActorIt->RouteEndPlay(EEndPlayReason::Quit);
				}
#else
				LocalWorld->EndPlay(EEndPlayReason::Quit);
#endif
			}
			LocalWorld->DestroyWorld(false);
			if (GEngine)
			{
				GEngine->DestroyWorldContext(LocalWorld);
			}
			LocalWorld->RemoveFromRoot();
			FlushRenderingCommands();
			FPaper2DPlusAppearanceCompositeCache::Get().ResetForTests();
			LayerFixtures.Reset();
			BudgetSubsystem = nullptr;
			CharacterProfile = nullptr;
			Flipbook = nullptr;
			CompositeFrameSprites.Reset();
			CompositeTexture = nullptr;
			Capture = nullptr;
			CaptureTarget = nullptr;
			CaptureOwner = nullptr;
		}
	};

	TSharedRef<FJsonObject> ProfileModeJson(const FPaper2DPlusAppearanceProfileModeMetrics& Mode)
	{
		TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetNumberField(TEXT("RegisteredPrimitiveCount"), Mode.RegisteredPrimitiveCount);
		Json->SetNumberField(TEXT("ColdActivationWallMilliseconds"), Mode.ColdActivationWallMilliseconds);
		Json->SetNumberField(TEXT("WarmAppearanceGameThreadMilliseconds"), Mode.WarmGameThreadMilliseconds);
		Json->SetNumberField(TEXT("WarmRenderThreadMilliseconds"), Mode.WarmRenderThreadMilliseconds);
		Json->SetNumberField(TEXT("WarmGpuMilliseconds"), Mode.WarmGpuMilliseconds);
		Json->SetBoolField(TEXT("GpuTimingAvailable"), Mode.bGpuTimingAvailable);
		return Json;
	}
}

const TCHAR* FPaper2DPlusAppearanceProfileHarness::DistributionName(
	EPaper2DPlusAppearanceProfileKeyDistribution Distribution)
{
	switch (Distribution)
	{
	case EPaper2DPlusAppearanceProfileKeyDistribution::Shared: return TEXT("Shared");
	case EPaper2DPlusAppearanceProfileKeyDistribution::Repeated: return TEXT("Repeated");
	case EPaper2DPlusAppearanceProfileKeyDistribution::AllUnique: return TEXT("AllUnique");
	default: return TEXT("Unknown");
	}
}

bool FPaper2DPlusAppearanceProfileHarness::RunReference200(
	const FPaper2DPlusAppearanceBudgetConfig& InConfig,
	FPaper2DPlusAppearanceProfileRun& OutRun,
	FString& OutError)
{
	FPaper2DPlusAppearanceBudgetConfig Config = InConfig;
	Config.Normalize();
	OutRun = FPaper2DPlusAppearanceProfileRun();
	OutRun.Config = Config;
	OutRun.EngineVersion = FEngineVersion::Current().ToString();
	OutRun.Platform = FPlatformProperties::PlatformName();
	OutRun.RhiName = GDynamicRHI ? GDynamicRHI->GetName() : TEXT("Unavailable");
	OutRun.GeneratedUtc = FDateTime::UtcNow().ToIso8601();
	FPaper2DPlusAppearanceProfileWorld ProfileWorld;
	if (!ProfileWorld.Initialize(OutError))
	{
		return false;
	}
	if (!ProfileMeasureAuthorityPublicationContract(ProfileWorld.GetWorld(), OutRun, OutError))
	{
		return false;
	}

	const int32 LayerCounts[] = { 4, 8, 12 };
	const EPaper2DPlusAppearanceProfileKeyDistribution Distributions[] = {
		EPaper2DPlusAppearanceProfileKeyDistribution::Shared,
		EPaper2DPlusAppearanceProfileKeyDistribution::Repeated,
		EPaper2DPlusAppearanceProfileKeyDistribution::AllUnique
	};
	TMap<int32, double> CompositeSubmissionMilliseconds;
	for (int32 LayerCount : LayerCounts)
	{
		double MeasuredMilliseconds = 0.0;
		if (!ProfileWorld.MeasureCompositeSubmission(LayerCount, MeasuredMilliseconds, OutError))
		{
			return false;
		}
		CompositeSubmissionMilliseconds.Add(LayerCount, MeasuredMilliseconds);
	}
	FPaper2DPlusAppearanceBuildRecipe ResidentRecipe;
	ResidentRecipe.PixelSize = FIntPoint(64, 64);
	ResidentRecipe.Frames.SetNum(ProfileCompositeFrameCount);
	const uint64 ReservationBytesPerKey = ResidentRecipe.EstimateCacheReservationBytes();

	for (int32 LayerCount : LayerCounts)
	{
		const FPaper2DPlusRepAppearanceState NetworkSnapshot =
			ProfileMakeAppearanceSnapshot(LayerCount);
		Paper2DPlusNetAppearanceMeasurement::FPayloadResult NetworkMeasurement;
		if (!Paper2DPlusNetAppearanceMeasurement::Measure(
			NetworkSnapshot, NetworkMeasurement, OutError))
		{
			return false;
		}
		for (EPaper2DPlusAppearanceProfileKeyDistribution Distribution : Distributions)
		{
			FPaper2DPlusAppearanceProfileFixtureMetrics& Fixture = OutRun.Fixtures.AddDefaulted_GetRef();
			Fixture.LayerCount = LayerCount;
			Fixture.KeyDistribution = Distribution;
			Fixture.UniqueAppearanceKeys = ProfileUniqueKeyCount(Distribution);
			Fixture.ExpectedProductionCompositeKeys =
				ProfileExpectedProductionCompositeKeys(Distribution);
			Fixture.ExpectedHybridPrimitiveCount =
				ProfileFarCount + (ProfileNearCount * 3) + (ProfileChangingCount * LayerCount);
			Fixture.AnimationFrameCount = ProfileCompositeFrameCount;
			Fixture.CompositeBuildUnitsPerFrame = Config.MaxCompositeBuildUnitsPerFrame;
			Fixture.CompositeWorkBudgetMillisecondsPerFrame =
				Config.MaxCompositeWorkMillisecondsPerFrame;
			Fixture.MeasuredCompositeSubmissionMillisecondsPerUnit =
				CompositeSubmissionMilliseconds.FindChecked(LayerCount);
			Fixture.MeasuredUnitsFitWithinMilliseconds =
				Fixture.MeasuredCompositeSubmissionMillisecondsPerUnit > 0.0
					? FMath::FloorToInt(
						Fixture.CompositeWorkBudgetMillisecondsPerFrame
						/ Fixture.MeasuredCompositeSubmissionMillisecondsPerUnit)
					: 0;
			Fixture.ExpectedCacheReservationBytes = static_cast<int64>(
				FMath::Min<uint64>(
					ReservationBytesPerKey
						* static_cast<uint64>(Fixture.ExpectedProductionCompositeKeys),
					static_cast<uint64>(MAX_int64)));
			Fixture.CacheByteBudget = static_cast<int64>(FMath::Min<uint64>(
				Config.MaxTransientCacheBytes, static_cast<uint64>(MAX_int64)));
			Fixture.SerializedDescriptorBytesEstimate =
				ProfileEstimateSerializedDescriptorBytes(NetworkSnapshot);
			Fixture.SerializedDescriptorBytesFor200ChangesEstimate =
				static_cast<int64>(Fixture.SerializedDescriptorBytesEstimate) * ProfileCharacterCount;
			Fixture.ReplicatedPropertyPayloadBitsPerChange = NetworkMeasurement.PayloadBits;
			Fixture.ReplicatedPropertyPayloadBytesPerChangeCeil =
				NetworkMeasurement.PayloadBytesCeil;
			Fixture.ReplicatedPropertyPayloadBitsFor200Changes =
				NetworkMeasurement.PayloadBits * static_cast<int64>(ProfileCharacterCount);
			Fixture.ReplicatedPropertyPayloadBytesFor200ChangesCeil =
				NetworkMeasurement.PayloadBytesCeil * static_cast<int64>(ProfileCharacterCount);
			Fixture.bReplicatedPropertyPayloadRoundTripMatched =
				NetworkMeasurement.bRoundTripMatched;
			Fixture.bReplicatedPropertyVerified =
				NetworkMeasurement.bReplicatedPropertyVerified;
			Fixture.bReplicatedStateExcludesTierCachePixelFields =
				NetworkMeasurement.bExcludesTierCachePixelState;
			OutRun.bNetworkContractGatePassed &=
				Fixture.ReplicatedPropertyPayloadBitsPerChange > 0
				&& Fixture.ReplicatedPropertyPayloadBytesPerChangeCeil > 0
				&& Fixture.ReplicatedPropertyPayloadBitsFor200Changes
					== Fixture.ReplicatedPropertyPayloadBitsPerChange * ProfileCharacterCount
				&& Fixture.ReplicatedPropertyPayloadBytesFor200ChangesCeil
					== Fixture.ReplicatedPropertyPayloadBytesPerChangeCeil * ProfileCharacterCount
				&& Fixture.bReplicatedPropertyPayloadRoundTripMatched
				&& Fixture.bReplicatedPropertyVerified
				&& Fixture.bReplicatedStateExcludesTierCachePixelFields;

			FPaper2DPlusAppearanceProfileProductionMetrics Production;
			if (!ProfileWorld.MeasureMode(
				LayerCount,
				Distribution,
				/*bHybrid=*/false,
				Config,
				Fixture.AllLive,
				nullptr,
				OutError)
				|| !ProfileWorld.MeasureMode(
					LayerCount,
					Distribution,
					/*bHybrid=*/true,
					Config,
					Fixture.Hybrid,
					&Production,
					OutError))
			{
				return false;
			}
			Fixture.ProductionComponentCount = Production.ComponentCount;
			Fixture.StableFarCompositeCount = Production.FarCount;
			Fixture.StableNearCompositeCount = Production.NearCount;
			Fixture.ChangingLiveCount = Production.ChangingCount;
			Fixture.PolicyLiveHandoffsGranted = Production.PeakLiveHandoffsGranted;
			Fixture.PolicyBuildUnitsGranted = Production.PeakBuildUnitsGranted;
			Fixture.PolicyQueueDepth = Production.PeakQueueDepth;
			Fixture.FinalChangingQueueDepth = Production.FinalQueueDepth;
			Fixture.CompositeQueueDepthCold = Production.PeakQueueDepth;
			Fixture.FramesToDrainColdQueue = Production.SchedulerIterations;
			Fixture.ResidentCacheEntries = Production.Cache.ResidentEntries;
			Fixture.CacheReservationBytes = static_cast<int64>(FMath::Min<uint64>(
				Production.Cache.CacheReservedBytes, static_cast<uint64>(MAX_int64)));
			Fixture.InitializedRenderTargetResourceBytes = Production.InitializedResourceBytes;
			Fixture.bInitializedRenderTargetResourceBytesAvailable =
				Production.bInitializedResourceBytesAvailable;
			Fixture.ColdCacheMisses = static_cast<int32>(FMath::Min<uint64>(
				Production.Cache.Admissions, static_cast<uint64>(MAX_int32)));
			Fixture.ColdSharedPendingHits = static_cast<int32>(FMath::Min<uint64>(
				Production.Cache.SharedPendingHits, static_cast<uint64>(MAX_int32)));
			Fixture.WarmCacheHits = static_cast<int32>(FMath::Min<uint64>(
				Production.Cache.PreparedHits, static_cast<uint64>(MAX_int32)));
			Fixture.SynchronousLoadViolations = Production.External.SynchronousLoadViolations;

			const int32 ExpectedAllLivePrimitives = ProfileCharacterCount * LayerCount;
			OutRun.bPrimitiveGatePassed &= Fixture.AllLive.RegisteredPrimitiveCount == ExpectedAllLivePrimitives;
			OutRun.bPrimitiveGatePassed &=
				Fixture.Hybrid.RegisteredPrimitiveCount == Fixture.ExpectedHybridPrimitiveCount;
			if (LayerCount == 8)
			{
				OutRun.bPrimitiveGatePassed &= Fixture.Hybrid.RegisteredPrimitiveCount <= 370;
			}
			if (LayerCount == 12)
			{
				OutRun.bPrimitiveGatePassed &= Fixture.Hybrid.RegisteredPrimitiveCount <= 410;
			}
			OutRun.bGameThreadGatePassed &=
				Fixture.Hybrid.WarmGameThreadMilliseconds < Fixture.AllLive.WarmGameThreadMilliseconds;
			OutRun.bRenderThreadGatePassed &=
				Fixture.Hybrid.WarmRenderThreadMilliseconds < Fixture.AllLive.WarmRenderThreadMilliseconds;
			OutRun.bCacheGatePassed &=
				Fixture.CacheReservationBytes == Fixture.ExpectedCacheReservationBytes
				&& Fixture.CacheReservationBytes <= Fixture.CacheByteBudget
				&& Fixture.bInitializedRenderTargetResourceBytesAvailable
				&& Fixture.InitializedRenderTargetResourceBytes > 0
				&& Fixture.InitializedRenderTargetResourceBytes <= Fixture.CacheByteBudget
				&& Fixture.ResidentCacheEntries == Fixture.ExpectedProductionCompositeKeys;
			OutRun.bCompositeWorkBudgetGatePassed &=
				Fixture.MeasuredUnitsFitWithinMilliseconds >= Config.MaxCompositeBuildUnitsPerFrame
				&& Fixture.PolicyBuildUnitsGranted > 0
				&& Fixture.PolicyBuildUnitsGranted <= Config.MaxCompositeBuildUnitsPerFrame;
			OutRun.bLoadGatePassed &= Fixture.SynchronousLoadViolations == 0;
			OutRun.bProductionPathGatePassed &=
				Fixture.ProductionComponentCount == ProfileCharacterCount
				&& Fixture.StableFarCompositeCount == ProfileFarCount
				&& Fixture.StableNearCompositeCount == ProfileNearCount
				&& Fixture.ChangingLiveCount == ProfileChangingCount
				&& Fixture.PolicyLiveHandoffsGranted == Config.MaxConcurrentLiveHandoffs
				&& Fixture.ResidentCacheEntries == Fixture.ExpectedProductionCompositeKeys
				&& Fixture.Hybrid.RegisteredPrimitiveCount == Fixture.ExpectedHybridPrimitiveCount;
		}
	}

	return true;
}

bool FPaper2DPlusAppearanceProfileHarness::WriteStableReport(
	FPaper2DPlusAppearanceProfileRun& Run,
	FString& OutError)
{
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("ReportSchemaVersion"), Run.ReportSchemaVersion);
	Root->SetStringField(TEXT("ReportKind"), TEXT("Paper2DPlusAppearanceCrowdComparison"));
	Root->SetStringField(TEXT("EngineVersion"), Run.EngineVersion);
	Root->SetStringField(TEXT("Platform"), Run.Platform);
	Root->SetStringField(TEXT("RHI"), Run.RhiName);
	Root->SetStringField(TEXT("GeneratedUtc"), Run.GeneratedUtc);
	Root->SetStringField(TEXT("TimingStatus"), TEXT("RenderCapableProductionPathMeasured"));
	Root->SetStringField(TEXT("CpuBrand"), FPlatformMisc::GetCPUBrand());

	TSharedRef<FJsonObject> Reference = MakeShared<FJsonObject>();
	Reference->SetNumberField(TEXT("VisibleCharacters"), ProfileCharacterCount);
	Reference->SetNumberField(TEXT("Far"), ProfileFarCount);
	Reference->SetNumberField(TEXT("Near"), ProfileNearCount);
	Reference->SetNumberField(TEXT("Changing"), ProfileChangingCount);
	Reference->SetNumberField(TEXT("CompositeAnimationFrames"), ProfileCompositeFrameCount);
	Reference->SetNumberField(TEXT("DistinctResidentSpriteFrames"), ProfileCompositeFrameCount);
	Reference->SetBoolField(TEXT("SpriteFramesRenderEquivalent"), true);
	Root->SetObjectField(TEXT("ReferenceDistribution"), Reference);

	TSharedRef<FJsonObject> Config = MakeShared<FJsonObject>();
	Config->SetNumberField(TEXT("MaximumConcurrentLiveHandoffs"), Run.Config.MaxConcurrentLiveHandoffs);
	Config->SetNumberField(TEXT("CompositeBuildUnitsPerFrame"), Run.Config.MaxCompositeBuildUnitsPerFrame);
	Config->SetNumberField(
		TEXT("CompositeWorkBudgetMillisecondsPerFrame"),
		Run.Config.MaxCompositeWorkMillisecondsPerFrame);
	Config->SetNumberField(TEXT("MaximumIndependentChannels"), Run.Config.MaxIndependentChannels);
	Config->SetNumberField(TEXT("TransientCacheBytes"), static_cast<double>(Run.Config.MaxTransientCacheBytes));
	Config->SetNumberField(TEXT("NearDistance"), Run.Config.NearDistance);
	Config->SetNumberField(TEXT("MaximumVisibleDistance"), Run.Config.MaximumVisibleDistance);
	Config->SetNumberField(TEXT("FrustumPaddingDegrees"), Run.Config.FrustumPaddingDegrees);
	Root->SetObjectField(TEXT("ProjectBudgets"), Config);

	double MeasuredMaximumAnyLayerMillisecondsPerUnit = 0.0;
	double MeasuredMaximumTwelveLayerMillisecondsPerUnit = 0.0;
	for (const FPaper2DPlusAppearanceProfileFixtureMetrics& Fixture : Run.Fixtures)
	{
		MeasuredMaximumAnyLayerMillisecondsPerUnit = FMath::Max(
			MeasuredMaximumAnyLayerMillisecondsPerUnit,
			Fixture.MeasuredCompositeSubmissionMillisecondsPerUnit);
		if (Fixture.LayerCount == 12)
		{
			MeasuredMaximumTwelveLayerMillisecondsPerUnit = FMath::Max(
				MeasuredMaximumTwelveLayerMillisecondsPerUnit,
				Fixture.MeasuredCompositeSubmissionMillisecondsPerUnit);
		}
	}
	const double MeasuredStrictestPairMilliseconds =
		MeasuredMaximumAnyLayerMillisecondsPerUnit
		* static_cast<double>(Run.Config.MaxCompositeBuildUnitsPerFrame);
	const double MeasuredTwelveLayerPairMilliseconds =
		MeasuredMaximumTwelveLayerMillisecondsPerUnit
		* static_cast<double>(Run.Config.MaxCompositeBuildUnitsPerFrame);
	const double ReferenceWorstMillisecondsPerUnit = 0.13620033860206604;
	const double ReferenceTwelveLayerMillisecondsPerUnit = 0.04839897155761719;
	const double ReferenceHeadroomMultiplier = 1.10;
	const double ReferenceRequiredBudgetMilliseconds =
		ReferenceWorstMillisecondsPerUnit
		* static_cast<double>(Run.Config.MaxCompositeBuildUnitsPerFrame)
		* ReferenceHeadroomMultiplier;
	TSharedRef<FJsonObject> Calibration = MakeShared<FJsonObject>();
	Calibration->SetStringField(
		TEXT("ReferenceCapture"),
		TEXT("UE 5.8, D3D11, AMD Ryzen 7 5800X, 2026-07-10"));
	Calibration->SetNumberField(
		TEXT("ReferenceTwelveLayerMaximumMillisecondsPerUnit"),
		ReferenceTwelveLayerMillisecondsPerUnit);
	Calibration->SetNumberField(
		TEXT("ReferenceStrictestObservedMillisecondsPerUnit"),
		ReferenceWorstMillisecondsPerUnit);
	Calibration->SetNumberField(TEXT("ReferenceHeadroomMultiplier"), ReferenceHeadroomMultiplier);
	Calibration->SetNumberField(
		TEXT("ReferenceRequiredBudgetMilliseconds"),
		ReferenceRequiredBudgetMilliseconds);
	Calibration->SetNumberField(
		TEXT("ConfiguredBudgetMilliseconds"),
		Run.Config.MaxCompositeWorkMillisecondsPerFrame);
	Calibration->SetNumberField(
		TEXT("MeasuredThisRunMaximumMillisecondsPerUnit"),
		MeasuredMaximumAnyLayerMillisecondsPerUnit);
	Calibration->SetNumberField(
		TEXT("MeasuredThisRunStrictestPairMilliseconds"),
		MeasuredStrictestPairMilliseconds);
	Calibration->SetNumberField(
		TEXT("MeasuredThisRunTwelveLayerMaximumMillisecondsPerUnit"),
		MeasuredMaximumTwelveLayerMillisecondsPerUnit);
	Calibration->SetNumberField(
		TEXT("MeasuredThisRunTwelveLayerPairMilliseconds"),
		MeasuredTwelveLayerPairMilliseconds);
	Calibration->SetNumberField(
		TEXT("MeasuredThisRunHeadroomOverStrictestPairPercent"),
		MeasuredStrictestPairMilliseconds > 0.0
			? ((Run.Config.MaxCompositeWorkMillisecondsPerFrame
				/ MeasuredStrictestPairMilliseconds) - 1.0) * 100.0
			: 0.0);
	Root->SetObjectField(TEXT("CompositeWorkCalibration"), Calibration);

	TSharedRef<FJsonObject> Method = MakeShared<FJsonObject>();
	Method->SetStringField(TEXT("World"),
		TEXT("Begun transient Game world; 200 actors each own the production Profile, Paper2DPlus Flipbook, and Layer Render components; the production budget world subsystem drives tier decisions and the production component creates/destroys its sprite/composite primitives. A 256x256 orthographic SceneCapture2D renders the resulting scene."));
	Method->SetStringField(TEXT("GameThread"),
		TEXT("Median of three samples; each averages two warmed production budget-subsystem evaluations (hybrid), FrameChanged deliveries across all 200 components, visibility/handoff work, and scene-update passes. All twelve animation frames are distinct resident UPaperSprite objects over one shared texture/material and byte-equivalent cooked geometry, so both all-live and hybrid paths perform real frame swaps while rendering equivalent art. One descriptor-distinct but render-equivalent Appearance Preset cache miss establishes the 140 Far / 50 Near / 10 Changing layout before timing. Composite construction is paused only inside the warm window and measured independently by the cold/cache/queue and CompositeWorkMilliseconds sections."));
	Method->SetStringField(TEXT("RenderThread"),
		TEXT("Render-thread cycle markers bracket the warmed component updates and matching scene captures."));
	Method->SetStringField(TEXT("GPU"),
		TEXT("RQT_AbsoluteTime timestamps bracket the same captures; unavailable is reported explicitly when unsupported."));
	Method->SetStringField(TEXT("Cache"),
		TEXT("The Layer Render components acquire, build, poll, and hand off through the production composite cache. Resident entries/reservations come from that cache; engine GetResourceSizeBytes(Exclusive) is deduplicated from the active production resources. Cold queue drain and the first bounded Changing submissions run before warm timing. The work gate is paused during warm presentation/policy sampling to keep the exact 10-live layout, then restored before reporting; pending Changing-only resources are retired so settled Far/Near residency remains authoritative. Cold-drain FlushRenderingCommands calls are test synchronization only and are outside warmed timings."));
	Method->SetStringField(TEXT("SynchronousLoads"),
		TEXT("The production FrameChanged path is exercised across every component and all twelve distinct resident sprite frames. SynchronousLoadViolations is read from the runtime instrumentation at the load-free GetLayerSpriteForFrame site; zero is a required gate, not a default report literal."));
	Method->SetStringField(TEXT("CompositeWorkMilliseconds"),
		TEXT("Maximum of five measured game-thread BuildFrame preparation/target/submission attempts for one 64x64 unit; the units/frame cap remains independent."));
	Method->SetStringField(TEXT("Network"),
		TEXT("Exact RepAppearance struct VALUE payload from FRepLayout::SerializePropertiesForStruct into FNetBitWriter; reflected leaves use the engine replicated-property NetSerialize path. Excludes property handles/change masks, actor/channel/bunch and packet headers, packet handlers, reliability/retransmission, relevancy, and per-connection fan-out; therefore it is not a packet capture or true on-wire byte count."));
	Root->SetObjectField(TEXT("MeasurementMethod"), Method);

	TSharedRef<FJsonObject> NetworkAuthority = MakeShared<FJsonObject>();
	NetworkAuthority->SetNumberField(
		TEXT("LogicalAuthorityAppearanceChanges"), Run.LogicalAuthorityAppearanceChanges);
	NetworkAuthority->SetNumberField(
		TEXT("AuthoritySnapshotPublishes"), Run.AuthoritySnapshotPublishes);
	NetworkAuthority->SetBoolField(
		TEXT("LateJoinAppliedLatestSnapshot"), Run.bLateJoinAppliedLatestSnapshot);
	NetworkAuthority->SetBoolField(
		TEXT("DedicatedServerAllocatedNoVisualChildren"),
		Run.bDedicatedServerAllocatedNoVisualChildren);
	NetworkAuthority->SetStringField(
		TEXT("FanoutMethod"),
		TEXT("Two hundred registered dedicated-authority components mutate after BeginPlay and publication is counted from sequence deltas. For each 0/1/4/32 fan-out, registered simulated-proxy components execute OnRep for every snapshot; successful descriptor+broadcast applications are counted. Authority sequences are re-read afterward to prove fan-out did not republish."));
	TArray<TSharedPtr<FJsonValue>> FanoutRows;
	const int32 FanoutRowCount = FMath::Min(
		Run.SimulatedClientCounts.Num(),
		Run.AuthorityPublishesBySimulatedClientCount.Num());
	FanoutRows.Reserve(FanoutRowCount);
	for (int32 Index = 0; Index < FanoutRowCount; ++Index)
	{
		TSharedRef<FJsonObject> Fanout = MakeShared<FJsonObject>();
		Fanout->SetNumberField(TEXT("SimulatedClientCount"), Run.SimulatedClientCounts[Index]);
		Fanout->SetNumberField(
			TEXT("AuthoritySnapshotPublishes"),
			Run.AuthorityPublishesBySimulatedClientCount[Index]);
		if (Run.ClientOnRepApplicationsBySimulatedClientCount.IsValidIndex(Index))
		{
			Fanout->SetNumberField(
				TEXT("ObservedClientOnRepApplications"),
				Run.ClientOnRepApplicationsBySimulatedClientCount[Index]);
		}
		FanoutRows.Add(MakeShared<FJsonValueObject>(Fanout));
	}
	NetworkAuthority->SetArrayField(TEXT("AuthorityPublishesByClientFanout"), FanoutRows);
	Root->SetObjectField(TEXT("NetworkAuthorityProbe"), NetworkAuthority);

	TArray<TSharedPtr<FJsonValue>> Rows;
	Rows.Reserve(Run.Fixtures.Num());
	for (const FPaper2DPlusAppearanceProfileFixtureMetrics& Fixture : Run.Fixtures)
	{
		TSharedRef<FJsonObject> Row = MakeShared<FJsonObject>();
		Row->SetNumberField(TEXT("VisibleCharacters"), Fixture.VisibleCharacters);
		Row->SetNumberField(TEXT("LayerCount"), Fixture.LayerCount);
		Row->SetStringField(TEXT("KeyDistribution"), DistributionName(Fixture.KeyDistribution));
		Row->SetNumberField(TEXT("UniqueAppearanceKeys"), Fixture.UniqueAppearanceKeys);
		Row->SetNumberField(
			TEXT("ExpectedProductionCompositeKeys"),
			Fixture.ExpectedProductionCompositeKeys);

		TSharedRef<FJsonObject> Policy = MakeShared<FJsonObject>();
		Policy->SetNumberField(TEXT("ExpectedHybridPrimitiveCount"), Fixture.ExpectedHybridPrimitiveCount);
		Policy->SetNumberField(TEXT("ProductionComponentCount"), Fixture.ProductionComponentCount);
		Policy->SetNumberField(TEXT("ObservedStableFarCount"), Fixture.StableFarCompositeCount);
		Policy->SetNumberField(TEXT("ObservedStableNearCount"), Fixture.StableNearCompositeCount);
		Policy->SetNumberField(TEXT("ObservedChangingLiveCount"), Fixture.ChangingLiveCount);
		Policy->SetNumberField(TEXT("PeakLiveHandoffsGranted"), Fixture.PolicyLiveHandoffsGranted);
		Policy->SetNumberField(TEXT("PeakBuildUnitsGranted"), Fixture.PolicyBuildUnitsGranted);
		Policy->SetNumberField(TEXT("PeakCompositeQueueDepth"), Fixture.PolicyQueueDepth);
		Policy->SetNumberField(TEXT("FinalEquivalentChangeQueueDepth"), Fixture.FinalChangingQueueDepth);
		Policy->SetNumberField(TEXT("SynchronousLoadViolations"), Fixture.SynchronousLoadViolations);
		Row->SetObjectField(TEXT("Policy"), Policy);

		TSharedRef<FJsonObject> ColdCache = MakeShared<FJsonObject>();
		ColdCache->SetNumberField(TEXT("AnimationFramesPerKey"), Fixture.AnimationFrameCount);
		ColdCache->SetNumberField(TEXT("ResidentEntries"), Fixture.ResidentCacheEntries);
		ColdCache->SetNumberField(TEXT("PeakActorsNeedingCompositeBuild"), Fixture.CompositeQueueDepthCold);
		ColdCache->SetNumberField(TEXT("BuildUnitsPerFrame"), Fixture.CompositeBuildUnitsPerFrame);
		ColdCache->SetNumberField(
			TEXT("CompositeWorkBudgetMillisecondsPerFrame"),
			Fixture.CompositeWorkBudgetMillisecondsPerFrame);
		ColdCache->SetNumberField(
			TEXT("MeasuredMaximumCompositeSubmissionMillisecondsPerUnit"),
			Fixture.MeasuredCompositeSubmissionMillisecondsPerUnit);
		ColdCache->SetNumberField(
			TEXT("MeasuredUnitsFitWithinMilliseconds"),
			Fixture.MeasuredUnitsFitWithinMilliseconds);
		ColdCache->SetNumberField(TEXT("ObservedSchedulerIterationsToDrainAndSample"), Fixture.FramesToDrainColdQueue);
		ColdCache->SetNumberField(TEXT("CacheReservationBytes"), Fixture.CacheReservationBytes);
		ColdCache->SetNumberField(
			TEXT("ExpectedCacheReservationBytes"), Fixture.ExpectedCacheReservationBytes);
		ColdCache->SetNumberField(
			TEXT("InitializedRenderTargetResourceBytes"),
			Fixture.InitializedRenderTargetResourceBytes);
		ColdCache->SetBoolField(
			TEXT("InitializedRenderTargetResourceBytesAvailable"),
			Fixture.bInitializedRenderTargetResourceBytesAvailable);
		ColdCache->SetNumberField(TEXT("CacheByteBudget"), Fixture.CacheByteBudget);
		ColdCache->SetNumberField(TEXT("ColdMisses"), Fixture.ColdCacheMisses);
		ColdCache->SetNumberField(TEXT("ColdSharedPendingHits"), Fixture.ColdSharedPendingHits);
		ColdCache->SetNumberField(TEXT("WarmPreparedHits"), Fixture.WarmCacheHits);
		Row->SetObjectField(TEXT("CacheColdWarm"), ColdCache);

		TSharedRef<FJsonObject> Network = MakeShared<FJsonObject>();
		Network->SetStringField(
			TEXT("MeasurementKind"),
			TEXT("ReplicatedPropertyValuePayloadNetSerialize"));
		Network->SetBoolField(TEXT("ReplicatedPropertyValuePayloadMeasured"), true);
		Network->SetNumberField(
			TEXT("PayloadBitsPerChange"),
			static_cast<double>(Fixture.ReplicatedPropertyPayloadBitsPerChange));
		Network->SetNumberField(
			TEXT("PayloadBytesPerChangeCeil"),
			static_cast<double>(Fixture.ReplicatedPropertyPayloadBytesPerChangeCeil));
		Network->SetNumberField(
			TEXT("PayloadBitsFor200Changes"),
			static_cast<double>(Fixture.ReplicatedPropertyPayloadBitsFor200Changes));
		Network->SetNumberField(
			TEXT("PayloadBytesFor200IndependentChangesCeil"),
			static_cast<double>(Fixture.ReplicatedPropertyPayloadBytesFor200ChangesCeil));
		Network->SetBoolField(
			TEXT("PayloadRoundTripMatched"),
			Fixture.bReplicatedPropertyPayloadRoundTripMatched);
		Network->SetBoolField(
			TEXT("ReplicatedPropertyVerified"),
			Fixture.bReplicatedPropertyVerified);
		Network->SetBoolField(TEXT("IncludesRepLayoutHandlesOrChangeMasks"), false);
		Network->SetBoolField(TEXT("IncludesActorChannelBunchOrPacketHeaders"), false);
		Network->SetBoolField(TEXT("PacketCaptureMeasured"), false);
		Network->SetBoolField(TEXT("WireBytesMeasured"), false);
		Network->SetBoolField(
			TEXT("ContainsTierCacheOrPixelState"),
			!Fixture.bReplicatedStateExcludesTierCachePixelFields);
		Network->SetBoolField(
			TEXT("TierCachePixelExclusionGatePassed"),
			Fixture.bReplicatedStateExcludesTierCachePixelFields);
		// Schema-3 compatibility aliases retain the prior persistent archive estimate. They are
		// explicitly not the measured payload and are not used by any gate.
		Network->SetNumberField(
			TEXT("SerializedDescriptorBytesPerChangeEstimate"),
			Fixture.SerializedDescriptorBytesEstimate);
		Network->SetNumberField(
			TEXT("SerializedDescriptorBytesFor200ChangesEstimate"),
			Fixture.SerializedDescriptorBytesFor200ChangesEstimate);
		Network->SetBoolField(TEXT("LegacyPersistentEstimateRetainedForSchema3"), true);
		Row->SetObjectField(TEXT("Network"), Network);

		Row->SetObjectField(TEXT("AllLive"), ProfileModeJson(Fixture.AllLive));
		Row->SetObjectField(TEXT("Hybrid"), ProfileModeJson(Fixture.Hybrid));

		TSharedRef<FJsonObject> Improvement = MakeShared<FJsonObject>();
		Improvement->SetNumberField(TEXT("GameThreadPercent"), ProfileImprovementPercent(
			Fixture.AllLive.WarmGameThreadMilliseconds, Fixture.Hybrid.WarmGameThreadMilliseconds));
		Improvement->SetNumberField(TEXT("RenderThreadPercent"), ProfileImprovementPercent(
			Fixture.AllLive.WarmRenderThreadMilliseconds, Fixture.Hybrid.WarmRenderThreadMilliseconds));
		Improvement->SetNumberField(TEXT("GpuPercent"),
			Fixture.AllLive.bGpuTimingAvailable && Fixture.Hybrid.bGpuTimingAvailable
				? ProfileImprovementPercent(
					Fixture.AllLive.WarmGpuMilliseconds, Fixture.Hybrid.WarmGpuMilliseconds)
				: 0.0);
		Row->SetObjectField(TEXT("ImprovementPercent"), Improvement);
		Rows.Add(MakeShared<FJsonValueObject>(Row));
	}
	Root->SetArrayField(TEXT("Fixtures"), Rows);

	TSharedRef<FJsonObject> Gates = MakeShared<FJsonObject>();
	Gates->SetBoolField(TEXT("PrimitiveCapsPassed"), Run.bPrimitiveGatePassed);
	Gates->SetBoolField(TEXT("WarmGameThreadImproved"), Run.bGameThreadGatePassed);
	Gates->SetBoolField(TEXT("WarmRenderThreadImproved"), Run.bRenderThreadGatePassed);
	Gates->SetBoolField(TEXT("CacheBudgetPassed"), Run.bCacheGatePassed);
	Gates->SetBoolField(TEXT("CompositeWorkMillisecondsBudgetPassed"),
		Run.bCompositeWorkBudgetGatePassed);
	Gates->SetBoolField(TEXT("SynchronousLoadGatePassed"), Run.bLoadGatePassed);
	Gates->SetBoolField(TEXT("NetworkAuthorityAndPayloadContractPassed"),
		Run.bNetworkContractGatePassed);
	Gates->SetBoolField(TEXT("ProductionComponentSubsystemCachePathPassed"),
		Run.bProductionPathGatePassed);
	Root->SetObjectField(TEXT("GateSummary"), Gates);

	FString Json;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
	if (!FJsonSerializer::Serialize(Root, Writer))
	{
		OutError = TEXT("Failed to serialize the appearance crowd profile report.");
		return false;
	}

	Run.ReportPath = FPaths::Combine(
		FPaths::ProjectSavedDir(),
		TEXT("Automation"),
		TEXT("Paper2DPlusAppearanceCrowd"),
		TEXT("render-profile-report.json"));
	const FString TemporaryPath = Run.ReportPath + TEXT(".tmp");
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	if (!PlatformFile.CreateDirectoryTree(*FPaths::GetPath(Run.ReportPath))
		|| !FFileHelper::SaveStringToFile(
			Json, *TemporaryPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		OutError = FString::Printf(TEXT("Failed to write temporary report '%s'."), *TemporaryPath);
		return false;
	}
	if (PlatformFile.FileExists(*Run.ReportPath) && !PlatformFile.DeleteFile(*Run.ReportPath))
	{
		OutError = FString::Printf(TEXT("Failed to replace prior report '%s'."), *Run.ReportPath);
		return false;
	}
	if (!PlatformFile.MoveFile(*Run.ReportPath, *TemporaryPath))
	{
		OutError = FString::Printf(TEXT("Failed to publish stable report '%s'."), *Run.ReportPath);
		return false;
	}
	return true;
}

#endif // WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
