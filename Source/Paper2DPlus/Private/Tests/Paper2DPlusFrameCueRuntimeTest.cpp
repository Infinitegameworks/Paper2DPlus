// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "HAL/PlatformProperties.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Modules/ModuleManager.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Paper2DPlusAnimationTags.h"
#include "Paper2DPlusBlueprintLibrary.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Paper2DPlusCharacterProfileComponent.h"
#include "Paper2DPlusModule.h"
#include "FrameCues/Paper2DPlusFrameCueBlueprint.h"
#include "FrameCues/Paper2DPlusFrameCueDispatch.h"
#include "Paper2DPlusFrameCueListener.h"
#include "Paper2DPlusTestFrameCueTypes.h"
#include "PaperFlipbook.h"
#include "PaperFlipbookComponent.h"
#include "GameFramework/Actor.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UnrealType.h"

namespace Paper2DPlusFrameCueRuntimeTest
{
	struct FCookedRuntimeWorld
	{
		FCookedRuntimeWorld()
		{
			if (!GEngine)
			{
				return;
			}

			const FName WorldName = MakeUniqueObjectName(
				nullptr,
				UWorld::StaticClass(),
				TEXT("P2DPCueTypeCookedRuntimeWorld"));
			World = UWorld::CreateWorld(
				EWorldType::Game,
				/*bInformEngineOfWorld=*/false,
				WorldName,
				GetTransientPackage());
			if (!World)
			{
				return;
			}

			World->AddToRoot();
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			FURL Url;
			World->InitializeActorsForPlay(Url);
		}

		~FCookedRuntimeWorld()
		{
			if (!World)
			{
				return;
			}

			UWorld* const LocalWorld = World;
			World = nullptr;
			LocalWorld->DestroyWorld(false);
			if (GEngine)
			{
				GEngine->DestroyWorldContext(LocalWorld);
			}
			LocalWorld->RemoveFromRoot();
		}

		FCookedRuntimeWorld(const FCookedRuntimeWorld&) = delete;
		FCookedRuntimeWorld& operator=(const FCookedRuntimeWorld&) = delete;

		UWorld* World = nullptr;
	};

	struct FRig
	{
		TObjectPtr<AActor> Actor = nullptr;
		TObjectPtr<UPaperFlipbook> Flipbook = nullptr;
		TObjectPtr<UPaper2DPlusCharacterProfileAsset> Profile = nullptr;
		TObjectPtr<UPaperFlipbookComponent> FlipbookComponent = nullptr;
		TObjectPtr<UPaper2DPlusCharacterProfileComponent> ProfileComponent = nullptr;
		TObjectPtr<UPaper2DPlusFrameCueRecorder> Recorder = nullptr;
	};

	FRig MakeRig(
		const TArray<UPaper2DPlusCueBase*>& Cues,
		bool bCreateDispatchableFrameZero = false)
	{
		FRig Rig;
		Rig.Actor = NewObject<AActor>();
		Rig.Flipbook = NewObject<UPaperFlipbook>();
		if (bCreateDispatchableFrameZero)
		{
			FScopedFlipbookMutator Mutator(Rig.Flipbook);
			Mutator.FramesPerSecond = 10.0f;
			FPaperFlipbookKeyFrame KeyFrame;
			KeyFrame.FrameRun = 1;
			Mutator.KeyFrames.Add(KeyFrame);
		}
		Rig.Profile = NewObject<UPaper2DPlusCharacterProfileAsset>();

		FFlipbookProfileEntry Entry;
		Entry.Identity.FlipbookName = TEXT("Attack");
		Entry.Identity.Flipbook = Rig.Flipbook;
		for (UPaper2DPlusCueBase* Cue : Cues)
		{
			Entry.FrameEventData.FrameCues.Add(Cue);
		}
		Rig.Profile->Flipbooks.Add(MoveTemp(Entry));

		Rig.FlipbookComponent = NewObject<UPaperFlipbookComponent>(Rig.Actor);
		Rig.ProfileComponent = NewObject<UPaper2DPlusCharacterProfileComponent>(Rig.Actor);
		Rig.Recorder = NewObject<UPaper2DPlusFrameCueRecorder>();
		Rig.Actor->AddOwnedComponent(Rig.FlipbookComponent);
		Rig.Actor->AddOwnedComponent(Rig.ProfileComponent);
		Rig.FlipbookComponent->SetFlipbook(Rig.Flipbook);
		Rig.ProfileComponent->CharacterProfile = Rig.Profile;
		Rig.ProfileComponent->FlipbookComponent = Rig.FlipbookComponent;
		Rig.ProfileComponent->OnFrameCue.AddDynamic(
			Rig.Recorder.Get(), &UPaper2DPlusFrameCueRecorder::OnCue);
		Rig.ProfileComponent->HandleFlipbookChanged(Rig.Flipbook);
		return Rig;
	}

	void CheckCueOrder(
		FAutomationTestBase& Test,
		const TCHAR* Label,
		const TArray<UPaper2DPlusCueBase*>& Actual,
		const TArray<UPaper2DPlusCueBase*>& Expected)
	{
		Test.TestEqual(*FString::Printf(TEXT("%s count"), Label), Actual.Num(), Expected.Num());
		const int32 SharedCount = FMath::Min(Actual.Num(), Expected.Num());
		for (int32 Index = 0; Index < SharedCount; ++Index)
		{
			Test.TestTrue(
				*FString::Printf(TEXT("%s cue %d preserves authored identity/order"), Label, Index),
				Actual[Index] == Expected[Index]);
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCueRuntimeBroadcastTest,
	"Paper2DPlus.FrameCues.Runtime.BroadcastLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCueRuntimeBroadcastTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusTestMomentCue* Moment = NewObject<UPaper2DPlusTestMomentCue>();
	Moment->TriggerFrame = 1;
	UPaper2DPlusTestRangeCue* Range = NewObject<UPaper2DPlusTestRangeCue>();
	Range->StartFrame = 2;
	Range->FrameCount = 2;

	Paper2DPlusFrameCueRuntimeTest::FRig Rig =
		Paper2DPlusFrameCueRuntimeTest::MakeRig({Moment, Range});
	Rig.ProfileComponent->HandleFrameChanged(0);
	Rig.ProfileComponent->HandleFrameChanged(1);
	Rig.ProfileComponent->HandleFrameChanged(2);
	Rig.ProfileComponent->HandleFrameChanged(3);
	Rig.ProfileComponent->HandleFrameChanged(4);

	TestEqual(TEXT("Moment plus range Begin/End broadcast"), Rig.Recorder->Contexts.Num(), 3);
	if (Rig.Recorder->Contexts.Num() == 3)
	{
		TestEqual(TEXT("Moment phase"), Rig.Recorder->Contexts[0].Phase, EPaper2DPlusFrameCuePhase::Trigger);
		TestEqual(TEXT("Range begins"), Rig.Recorder->Contexts[1].Phase, EPaper2DPlusFrameCuePhase::Begin);
		TestEqual(TEXT("Range ends"), Rig.Recorder->Contexts[2].Phase, EPaper2DPlusFrameCuePhase::End);
		TestEqual(TEXT("Natural end reason"), Rig.Recorder->Contexts[2].EndReason, EPaper2DPlusFrameCueEndReason::Completed);
		TestEqual(TEXT("Animation identity is broadcast"), Rig.Recorder->Contexts[0].AnimationName, FName(TEXT("Attack")));
		TestTrue(TEXT("Runtime context is not preview"), !Rig.Recorder->Contexts[0].bIsEditorPreview);
	}
	TestTrue(TEXT("Range is removed from active set"), !Rig.ProfileComponent->IsRangeCueActiveForTests(Range));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCueExternalPlaybackLifecycleTest,
	"Paper2DPlus.FrameCues.Runtime.ExternalPlaybackLifecycleGenerationIsolation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCueExternalPlaybackLifecycleTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusTestRangeCue* Range = NewObject<UPaper2DPlusTestRangeCue>();
	Range->StartFrame = 1;
	Range->FrameCount = 3;

	Paper2DPlusFrameCueRuntimeTest::FRig Rig =
		Paper2DPlusFrameCueRuntimeTest::MakeRig({Range});
	{
		FScopedFlipbookMutator Mutator(Rig.Flipbook);
		Mutator.FramesPerSecond = 10.0f;
		Mutator.KeyFrames.Reset();
		for (int32 Frame = 0; Frame < 4; ++Frame)
		{
			FPaperFlipbookKeyFrame KeyFrame;
			KeyFrame.FrameRun = 1;
			Mutator.KeyFrames.Add(KeyFrame);
		}
	}

	// Model an externally owned (for example, PaperZD-shaped) timeline entirely through the generic
	// public seam: the test has no PaperZD include, module dependency, object, or type reference.
	Rig.FlipbookComponent->Stop();
	Rig.FlipbookComponent->SetPlaybackPosition(0.05f, /*bFireEvents=*/false);
	Rig.ProfileComponent->HandleFlipbookChanged(Rig.Flipbook);
	TestFalse(TEXT("The externally driven stock source starts stopped"), Rig.FlipbookComponent->IsPlaying());

	const int64 GenerationOne =
		Rig.ProfileComponent->BeginExternalFrameCuePlayback(Rig.FlipbookComponent);
	TestTrue(
		TEXT("A stopped external source receives a positive playback generation"),
		GenerationOne > 0);
	TestFalse(
		TEXT("Beginning external Frame Cue playback does not Play the caller-owned source"),
		Rig.FlipbookComponent->IsPlaying());

	for (int32 Frame = 1; Frame < 4; ++Frame)
	{
		Rig.FlipbookComponent->SetPlaybackPosition(
			Frame * 0.1f + 0.05f,
			/*bFireEvents=*/false);
		Rig.ProfileComponent->HandleFrameChanged(Frame);
	}
	const float FinalPlaybackPosition = Rig.FlipbookComponent->GetPlaybackPosition();
	TestTrue(
		TEXT("The Cue State is active on the externally driven final frame"),
		Rig.ProfileComponent->IsRangeCueActiveForTests(Range));
	TestTrue(
		TEXT("The first external completion report is accepted"),
		Rig.ProfileComponent->ReportFrameCuePlaybackCompleted(
			Rig.FlipbookComponent,
			GenerationOne));

	int32 CompletedEndCount = 0;
	for (const FPaper2DPlusFrameCueContext& Context : Rig.Recorder->Contexts)
	{
		if (Context.Phase == EPaper2DPlusFrameCuePhase::End
			&& Context.EndReason == EPaper2DPlusFrameCueEndReason::Completed)
		{
			++CompletedEndCount;
		}
	}
	TestEqual(TEXT("Generation one emits exactly one Completed End"), CompletedEndCount, 1);
	TestTrue(
		TEXT("Generation one completion removes the active Cue State"),
		!Rig.ProfileComponent->IsRangeCueActiveForTests(Range));
	if (!Rig.Recorder->Contexts.IsEmpty())
	{
		const FPaper2DPlusFrameCueContext& CompletedContext = Rig.Recorder->Contexts.Last();
		TestEqual(
			TEXT("External completion emits End"),
			CompletedContext.Phase,
			EPaper2DPlusFrameCuePhase::End);
		TestEqual(
			TEXT("External completion preserves the Completed reason"),
			CompletedContext.EndReason,
			EPaper2DPlusFrameCueEndReason::Completed);
		TestEqual(
			TEXT("External completion is ordinary runtime playback"),
			CompletedContext.EvaluationMode,
			EPaper2DPlusFrameCueEvaluationMode::RuntimePlayback);
		TestEqual(TEXT("External completion identifies the final frame"), CompletedContext.CurrentFrame, 3);
		TestTrue(TEXT("External completion carries the owning actor"), CompletedContext.OwningActor == Rig.Actor);
		TestTrue(
			TEXT("External completion carries the Profile component"),
			CompletedContext.ProfileComponent == Rig.ProfileComponent);
		TestTrue(
			TEXT("External completion carries the Character Profile"),
			CompletedContext.CharacterProfile == Rig.Profile);
		TestTrue(
			TEXT("External completion carries the caller-owned playback source"),
			CompletedContext.PlaybackComponent == Rig.FlipbookComponent);
		TestTrue(TEXT("External completion carries the active flipbook"), CompletedContext.Flipbook == Rig.Flipbook);
		TestEqual(
			TEXT("External completion carries the animation identity"),
			CompletedContext.AnimationName,
			FName(TEXT("Attack")));
		TestFalse(TEXT("External completion is not an editor preview"), CompletedContext.bIsEditorPreview);
		TestFalse(TEXT("External completion is not catch-up evaluation"), CompletedContext.bIsCatchUp);
	}
	TestFalse(
		TEXT("Completion does not Play the caller-owned source"),
		Rig.FlipbookComponent->IsPlaying());
	TestEqual(
		TEXT("Completion does not reposition the caller-owned source"),
		Rig.FlipbookComponent->GetPlaybackPosition(),
		FinalPlaybackPosition);

	const int32 NotificationCountAfterGenerationOne = Rig.Recorder->Contexts.Num();
	const int64 GenerationTwo =
		Rig.ProfileComponent->BeginExternalFrameCuePlayback(Rig.FlipbookComponent);
	TestTrue(TEXT("A same-flipbook restart receives another positive generation"), GenerationTwo > 0);
	TestTrue(TEXT("A same-flipbook restart receives a distinct generation"), GenerationTwo != GenerationOne);
	TestEqual(
		TEXT("A same-frame restart immediately re-evaluates and re-Begins the Cue State"),
		Rig.Recorder->Contexts.Num(),
		NotificationCountAfterGenerationOne + 1);
	if (Rig.Recorder->Contexts.Num() == NotificationCountAfterGenerationOne + 1)
	{
		const FPaper2DPlusFrameCueContext& RestartContext = Rig.Recorder->Contexts.Last();
		TestEqual(
			TEXT("The forced same-frame evaluation emits Begin"),
			RestartContext.Phase,
			EPaper2DPlusFrameCuePhase::Begin);
		TestEqual(TEXT("The forced evaluation remains on the final frame"), RestartContext.CurrentFrame, 3);
		TestEqual(
			TEXT("The forced evaluation resets its previous-frame edge"),
			RestartContext.PreviousFrame,
			INDEX_NONE);
		TestEqual(
			TEXT("The restarted generation remains ordinary runtime playback"),
			RestartContext.EvaluationMode,
			EPaper2DPlusFrameCueEvaluationMode::RuntimePlayback);
	}
	TestTrue(
		TEXT("The restarted generation owns an active Cue State"),
		Rig.ProfileComponent->IsRangeCueActiveForTests(Range));

	const int32 NotificationCountAfterGenerationTwoBegin = Rig.Recorder->Contexts.Num();
	TestFalse(
		TEXT("A delayed stop report for generation one is rejected"),
		Rig.ProfileComponent->ReportFrameCuePlaybackStopped(
			Rig.FlipbookComponent,
			GenerationOne));
	TestFalse(
		TEXT("A delayed completion report for generation one is rejected"),
		Rig.ProfileComponent->ReportFrameCuePlaybackCompleted(
			Rig.FlipbookComponent,
			GenerationOne));
	TestEqual(
		TEXT("Stale generation-one reports cannot notify generation two"),
		Rig.Recorder->Contexts.Num(),
		NotificationCountAfterGenerationTwoBegin);
	TestTrue(
		TEXT("Stale generation-one reports cannot end generation two"),
		Rig.ProfileComponent->IsRangeCueActiveForTests(Range));

	TestTrue(
		TEXT("The current generation-two stop report is accepted"),
		Rig.ProfileComponent->ReportFrameCuePlaybackStopped(
			Rig.FlipbookComponent,
			GenerationTwo));
	TestFalse(
		TEXT("A duplicate generation-two stop report is idempotently rejected"),
		Rig.ProfileComponent->ReportFrameCuePlaybackStopped(
			Rig.FlipbookComponent,
			GenerationTwo));
	TestEqual(
		TEXT("Generation two emits exactly one terminal notification"),
		Rig.Recorder->Contexts.Num(),
		NotificationCountAfterGenerationTwoBegin + 1);
	if (Rig.Recorder->Contexts.Num() == NotificationCountAfterGenerationTwoBegin + 1)
	{
		TestEqual(
			TEXT("Generation two terminates with End"),
			Rig.Recorder->Contexts.Last().Phase,
			EPaper2DPlusFrameCuePhase::End);
		TestEqual(
			TEXT("Generation two terminates as PlaybackStopped"),
			Rig.Recorder->Contexts.Last().EndReason,
			EPaper2DPlusFrameCueEndReason::PlaybackStopped);
	}
	TestTrue(
		TEXT("Generation two stop removes the active Cue State"),
		!Rig.ProfileComponent->IsRangeCueActiveForTests(Range));
	TestFalse(
		TEXT("Lifecycle reports leave the caller-owned stock source stopped"),
		Rig.FlipbookComponent->IsPlaying());
	TestEqual(
		TEXT("Lifecycle reports leave the caller-owned playback position untouched"),
		Rig.FlipbookComponent->GetPlaybackPosition(),
		FinalPlaybackPosition);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCueRuntimeEffectWarmTest,
	"Paper2DPlus.FrameCues.Runtime.CurrentAnimationEffectsWarmAndDeduplicate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCueRuntimeEffectWarmTest::RunTest(const FString& Parameters)
{
	UPaperFlipbook* EffectA = NewObject<UPaperFlipbook>();
	UPaperFlipbook* EffectB = NewObject<UPaperFlipbook>();
	// Keyed on the SOFT art field: that branch of CollectWarmableEffectArt is structural and compiles
	// in every configuration, so this pin means the same thing in a cooked build as it does here.
	UPaper2DPlusTestEffectArtCue* First = NewObject<UPaper2DPlusTestEffectArtCue>();
	UPaper2DPlusTestEffectArtCue* Duplicate = NewObject<UPaper2DPlusTestEffectArtCue>();
	UPaper2DPlusTestEffectArtCue* Second = NewObject<UPaper2DPlusTestEffectArtCue>();
	First->SoftArt = EffectA;
	Duplicate->SoftArt = EffectA;
	Second->SoftArt = EffectB;

	Paper2DPlusFrameCueRuntimeTest::FRig Rig =
		Paper2DPlusFrameCueRuntimeTest::MakeRig(
			{First, Duplicate, Second},
			/*bCreateDispatchableFrameZero=*/true);
	TestEqual(TEXT("The current animation retains each distinct effect once"),
		Rig.ProfileComponent->GetWarmedFrameCueEffectCountForTests(), 2);
	TestTrue(TEXT("Frame-zero dispatch observes the complete warm set"),
		!Rig.Recorder->WarmedEffectCountsAtDispatch.IsEmpty()
			&& Rig.Recorder->WarmedEffectCountsAtDispatch[0] == 2);
	const FSoftObjectPath EffectAPath(EffectA);
	const FSoftObjectPath EffectBPath(EffectB);
	Rig.Actor->AddToRoot();
	EffectA = nullptr;
	EffectB = nullptr;
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
	TestNotNull(TEXT("The current animation strongly retains its first soft effect"),
		EffectAPath.ResolveObject());
	TestNotNull(TEXT("The current animation strongly retains its second soft effect"),
		EffectBPath.ResolveObject());

	Rig.ProfileComponent->HandleFlipbookChanged(NewObject<UPaperFlipbook>());
	TestEqual(TEXT("Changing to an animation with no profile row releases warmed effects"),
		Rig.ProfileComponent->GetWarmedFrameCueEffectCountForTests(), 0);
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
	TestNull(TEXT("Released soft effects can leave memory after the animation changes"),
		EffectAPath.ResolveObject());
	TestNull(TEXT("Every released soft effect can leave memory after the animation changes"),
		EffectBPath.ResolveObject());
	Rig.Actor->RemoveFromRoot();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCueRuntimeAnimationChangeTest,
	"Paper2DPlus.FrameCues.Runtime.AnimationChangeEndsRange",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCueRuntimeAnimationChangeTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusTestRangeCue* Range = NewObject<UPaper2DPlusTestRangeCue>();
	Range->StartFrame = 0;
	Range->FrameCount = 5;
	Paper2DPlusFrameCueRuntimeTest::FRig Rig = Paper2DPlusFrameCueRuntimeTest::MakeRig({Range});
	Rig.ProfileComponent->HandleFrameChanged(0);
	TestTrue(TEXT("Range begins on frame zero"), Rig.ProfileComponent->IsRangeCueActiveForTests(Range));

	UPaperFlipbook* OtherFlipbook = NewObject<UPaperFlipbook>();
	Rig.ProfileComponent->HandleFlipbookChanged(OtherFlipbook);

	TestEqual(TEXT("Begin and forced End broadcast"), Rig.Recorder->Contexts.Num(), 2);
	if (Rig.Recorder->Contexts.Num() == 2)
	{
		TestEqual(TEXT("Forced phase is End"), Rig.Recorder->Contexts[1].Phase, EPaper2DPlusFrameCuePhase::End);
		TestEqual(TEXT("Animation change reason"), Rig.Recorder->Contexts[1].EndReason, EPaper2DPlusFrameCueEndReason::AnimationChanged);
	}
	TestTrue(TEXT("Range is inactive after animation change"), !Rig.ProfileComponent->IsRangeCueActiveForTests(Range));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCueRuntimeNetGateTest,
	"Paper2DPlus.FrameCues.Runtime.NetPolicyGate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCueRuntimeNetGateTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusTestMomentCue* Moment = NewObject<UPaper2DPlusTestMomentCue>();
	Moment->TriggerFrame = 0;
	Moment->NetPolicy = EPaper2DPlusFrameCueNetPolicy::AuthorityOnly;
	Paper2DPlusFrameCueRuntimeTest::FRig Rig = Paper2DPlusFrameCueRuntimeTest::MakeRig({Moment});

	Rig.ProfileComponent->SetNetContextOverrideForTests(EPaper2DPlusNetContext::SimulatedProxy);
	Rig.ProfileComponent->HandleFrameChanged(0);
	TestEqual(TEXT("Authority cue is suppressed on a simulated proxy"), Rig.Recorder->Contexts.Num(), 0);

	Rig.ProfileComponent->SetNetContextOverrideForTests(EPaper2DPlusNetContext::Authority);
	Rig.ProfileComponent->HandleFlipbookChanged(Rig.Flipbook);
	Rig.ProfileComponent->HandleFrameChanged(0);
	TestEqual(TEXT("Authority cue broadcasts on authority"), Rig.Recorder->Contexts.Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCueRuntimeBehaviorNetGateTest,
	"Paper2DPlus.FrameCues.Runtime.NetPolicyGateCoversBehavior",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCueRuntimeBehaviorNetGateTest::RunTest(const FString& Parameters)
{
	// Behavior rides the same admission gate as listeners, so a cosmetic cue must stay inert on a
	// dedicated server (nothing there can hear or see it) while a LocalAlways cue still runs.
	Paper2DPlusBehaviorTestLog::Reset();

	UPaper2DPlusTestBehaviorMomentCue* Cosmetic = NewObject<UPaper2DPlusTestBehaviorMomentCue>();
	Cosmetic->TriggerFrame = 0;
	Cosmetic->Payload = 1;
	Cosmetic->NetPolicy = EPaper2DPlusFrameCueNetPolicy::CosmeticOnly;
	UPaper2DPlusTestBehaviorMomentCue* Everywhere = NewObject<UPaper2DPlusTestBehaviorMomentCue>();
	Everywhere->TriggerFrame = 0;
	Everywhere->Payload = 2;
	Everywhere->NetPolicy = EPaper2DPlusFrameCueNetPolicy::LocalAlways;

	Paper2DPlusFrameCueRuntimeTest::FRig Rig =
		Paper2DPlusFrameCueRuntimeTest::MakeRig({Cosmetic, Everywhere});
	Rig.ProfileComponent->SetNetContextOverrideForTests(EPaper2DPlusNetContext::Authority);
	Rig.ProfileComponent->SetDedicatedServerOverrideForTests(true);
	Rig.ProfileComponent->HandleFrameChanged(0);

	const TArray<Paper2DPlusBehaviorTestLog::FRecord>& ServerLog =
		Paper2DPlusBehaviorTestLog::Records();
	TestEqual(TEXT("A dedicated server runs exactly one cue's behavior"), ServerLog.Num(), 1);
	if (ServerLog.Num() == 1)
	{
		TestEqual(TEXT("Only the LocalAlways cue's behavior ran on the dedicated server"),
			ServerLog[0].Payload, 2);
	}
	TestEqual(TEXT("The suppressed cosmetic cue reaches no listener either"),
		Rig.Recorder->Contexts.Num(), 1);

	// The same placements on a listen server (no dedicated-server suppression) run both behaviors.
	Paper2DPlusBehaviorTestLog::Reset();
	Rig.ProfileComponent->SetDedicatedServerOverrideForTests(false);
	Rig.ProfileComponent->HandleFlipbookChanged(Rig.Flipbook);
	Rig.ProfileComponent->HandleFrameChanged(0);
	TestEqual(TEXT("Off a dedicated server both behaviors run"),
		Paper2DPlusBehaviorTestLog::Records().Num(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCueListenerLifecycleTest,
	"Paper2DPlus.FrameCues.Runtime.ListenerFilterAndCancel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCueListenerLifecycleTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusTestMomentCue* Moment = NewObject<UPaper2DPlusTestMomentCue>();
	Moment->TriggerFrame = 1;
	Paper2DPlusFrameCueRuntimeTest::FRig Rig = Paper2DPlusFrameCueRuntimeTest::MakeRig({Moment});
	// The rig's general recorder is intentionally ignored here; this recorder only observes the
	// filtered async proxy output.
	UPaper2DPlusFrameCueRecorder* ListenerRecorder = NewObject<UPaper2DPlusFrameCueRecorder>();
	UPaper2DPlusFrameCueListener* Listener = UPaper2DPlusFrameCueListener::ListenForFrameCue(
		Rig.Actor,
		Rig.ProfileComponent,
		UPaper2DPlusTestMomentCue::StaticClass(),
		FGameplayTag(),
		false,
		false);
	Listener->Triggered.AddDynamic(ListenerRecorder, &UPaper2DPlusFrameCueRecorder::OnCue);
	Listener->Activate();

	Rig.ProfileComponent->HandleFrameChanged(0);
	Rig.ProfileComponent->HandleFrameChanged(1);
	TestEqual(TEXT("Matching listener receives the trigger"), ListenerRecorder->Contexts.Num(), 1);

	Listener->Cancel();
	Rig.ProfileComponent->HandleFlipbookChanged(Rig.Flipbook);
	Rig.ProfileComponent->HandleFrameChanged(0);
	Rig.ProfileComponent->HandleFrameChanged(1);
	TestEqual(TEXT("Cancelled listener receives no later trigger"), ListenerRecorder->Contexts.Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCueSkippedStepTest,
	"Paper2DPlus.FrameCues.Runtime.SkippedStepCompressedOrdering",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCueSkippedStepTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusTestRangeCue* Range = NewObject<UPaper2DPlusTestRangeCue>();
	Range->StartFrame = 1;
	Range->FrameCount = 2;
	Range->bEmitUpdates = true;
	UPaper2DPlusTestMomentCue* Moment = NewObject<UPaper2DPlusTestMomentCue>();
	Moment->TriggerFrame = 3;

	Paper2DPlusFrameCueRuntimeTest::FRig Rig =
		Paper2DPlusFrameCueRuntimeTest::MakeRig({Range, Moment});
	Rig.ProfileComponent->HandleFrameChanged(0);
	Rig.ProfileComponent->HandleFrameChanged(4); // crosses the whole range and the Moment anchor

	TestEqual(TEXT("compressed range lifecycle plus crossed Moment"), Rig.Recorder->Contexts.Num(), 4);
	if (Rig.Recorder->Contexts.Num() == 4)
	{
		TestTrue(TEXT("authored range emits first"), Rig.Recorder->Cues[0] == Range);
		TestTrue(TEXT("range Update remains adjacent"), Rig.Recorder->Cues[1] == Range);
		TestTrue(TEXT("range End remains adjacent"), Rig.Recorder->Cues[2] == Range);
		TestTrue(TEXT("authored Moment follows the range"), Rig.Recorder->Cues[3] == Moment);
		TestEqual(TEXT("compressed lifecycle starts with Begin"),
			Rig.Recorder->Contexts[0].Phase, EPaper2DPlusFrameCuePhase::Begin);
		TestEqual(TEXT("compressed lifecycle preserves Update"),
			Rig.Recorder->Contexts[1].Phase, EPaper2DPlusFrameCuePhase::Update);
		TestEqual(TEXT("compressed lifecycle ends with End"),
			Rig.Recorder->Contexts[2].Phase, EPaper2DPlusFrameCuePhase::End);
		TestEqual(TEXT("crossed Moment triggers"),
			Rig.Recorder->Contexts[3].Phase, EPaper2DPlusFrameCuePhase::Trigger);
		TestTrue(TEXT("Begin is marked compressed"), Rig.Recorder->Contexts[0].bIsCompressed);
		TestTrue(TEXT("Update is marked compressed"), Rig.Recorder->Contexts[1].bIsCompressed);
		TestTrue(TEXT("End is marked compressed"), Rig.Recorder->Contexts[2].bIsCompressed);
		TestFalse(TEXT("Moment crossing is not a compressed range phase"),
			Rig.Recorder->Contexts[3].bIsCompressed);
		TestEqual(TEXT("skipped transition records previous frame"),
			Rig.Recorder->Contexts[0].PreviousFrame, 0);
		TestEqual(TEXT("skipped transition records target frame"),
			Rig.Recorder->Contexts[0].CurrentFrame, 4);
	}
	TestFalse(TEXT("fully crossed range never remains active"),
		Rig.ProfileComponent->IsRangeCueActiveForTests(Range));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCueLoopWrapTest,
	"Paper2DPlus.FrameCues.Runtime.LoopWrapEndsActiveBeforeReentry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCueLoopWrapTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusTestRangeCue* Range = NewObject<UPaper2DPlusTestRangeCue>();
	Range->StartFrame = 3;
	Range->FrameCount = 3;
	UPaper2DPlusTestMomentCue* LoopStart = NewObject<UPaper2DPlusTestMomentCue>();
	LoopStart->TriggerFrame = 0;

	Paper2DPlusFrameCueRuntimeTest::FRig Rig =
		Paper2DPlusFrameCueRuntimeTest::MakeRig({Range, LoopStart});
	Rig.ProfileComponent->HandleFrameChanged(0); // initial loop-start Moment
	Rig.Recorder->Cues.Reset();
	Rig.Recorder->Contexts.Reset();
	Rig.ProfileComponent->HandleFrameChanged(3); // Range Begin
	Rig.ProfileComponent->HandleFrameChanged(0); // wrap: End old range, then trigger loop start

	TestEqual(TEXT("Begin, loop-reset End, and loop-start Trigger"), Rig.Recorder->Contexts.Num(), 3);
	if (Rig.Recorder->Contexts.Num() == 3)
	{
		TestEqual(TEXT("range begins before wrap"),
			Rig.Recorder->Contexts[0].Phase, EPaper2DPlusFrameCuePhase::Begin);
		TestEqual(TEXT("active range ends before wrap re-entry"),
			Rig.Recorder->Contexts[1].Phase, EPaper2DPlusFrameCuePhase::End);
		TestEqual(TEXT("loop reset is the forced-end reason"),
			Rig.Recorder->Contexts[1].EndReason, EPaper2DPlusFrameCueEndReason::LoopReset);
		TestTrue(TEXT("forced End carries loop-wrap context"),
			Rig.Recorder->Contexts[1].bWasLoopWrap);
		TestEqual(TEXT("loop-start Moment follows the End"),
			Rig.Recorder->Contexts[2].Phase, EPaper2DPlusFrameCuePhase::Trigger);
		TestTrue(TEXT("loop-start Trigger carries loop-wrap context"),
			Rig.Recorder->Contexts[2].bWasLoopWrap);
	}
	TestFalse(TEXT("pre-wrap range is not spuriously reactivated"),
		Rig.ProfileComponent->IsRangeCueActiveForTests(Range));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCueNestedSourceMutationTest,
	"Paper2DPlus.FrameCues.Runtime.NestedReceiverFlipbookAndProfileMutation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCueNestedSourceMutationTest::RunTest(const FString& Parameters)
{
	// A cue receiver switches flipbook while another Cue State is active. The switch queues until the
	// in-flight snapshot finishes, then ends the old range exactly once.
	{
		UPaper2DPlusTestRangeCue* Range = NewObject<UPaper2DPlusTestRangeCue>();
		Range->StartFrame = 0;
		Range->FrameCount = 3;
		UPaper2DPlusTestMomentCue* MutatorCue = NewObject<UPaper2DPlusTestMomentCue>();
		MutatorCue->TriggerFrame = 1;
		Paper2DPlusFrameCueRuntimeTest::FRig Rig =
			Paper2DPlusFrameCueRuntimeTest::MakeRig({Range, MutatorCue});

		UPaperFlipbook* OtherFlipbook = NewObject<UPaperFlipbook>();
		FFlipbookProfileEntry OtherEntry;
		OtherEntry.Identity.FlipbookName = TEXT("Other");
		OtherEntry.Identity.Flipbook = OtherFlipbook;
		Rig.Profile->Flipbooks.Add(MoveTemp(OtherEntry));
		Rig.Profile->InvalidateFlipbookLookupCache();

		UPaper2DPlusFrameCueMutationReceiver* Receiver =
			NewObject<UPaper2DPlusFrameCueMutationReceiver>();
		Receiver->TriggerCue = MutatorCue;
		Receiver->TargetFlipbook = OtherFlipbook;
		Rig.ProfileComponent->OnFrameCue.AddDynamic(
			Receiver, &UPaper2DPlusFrameCueMutationReceiver::OnCue);

		Rig.ProfileComponent->HandleFrameChanged(0);
		Rig.ProfileComponent->HandleFrameChanged(1);
		TestEqual(TEXT("flipbook receiver mutated once"), Receiver->MutationCount, 1);
		TestTrue(TEXT("queued flipbook switch drained after dispatch"),
			Rig.FlipbookComponent->GetFlipbook() == OtherFlipbook);
		TestFalse(TEXT("old range ended after queued flipbook mutation"),
			Rig.ProfileComponent->IsRangeCueActiveForTests(Range));
		TestEqual(TEXT("Begin, mutator Trigger, single End"), Rig.Recorder->Contexts.Num(), 3);
		if (Rig.Recorder->Contexts.Num() == 3)
		{
			TestEqual(TEXT("flipbook mutation End reason"),
				Rig.Recorder->Contexts[2].EndReason, EPaper2DPlusFrameCueEndReason::AnimationChanged);
		}
	}

	// A profile swap from the same cue receiver changes the profile immediately but defers the cache
	// rewarm/teardown until the current notification sequence is stable.
	{
		UPaper2DPlusTestRangeCue* Range = NewObject<UPaper2DPlusTestRangeCue>();
		Range->StartFrame = 0;
		Range->FrameCount = 3;
		UPaper2DPlusTestMomentCue* MutatorCue = NewObject<UPaper2DPlusTestMomentCue>();
		MutatorCue->TriggerFrame = 1;
		Paper2DPlusFrameCueRuntimeTest::FRig Rig =
			Paper2DPlusFrameCueRuntimeTest::MakeRig({Range, MutatorCue});

		UPaper2DPlusCharacterProfileAsset* Replacement =
			NewObject<UPaper2DPlusCharacterProfileAsset>();
		FFlipbookProfileEntry ReplacementEntry;
		ReplacementEntry.Identity.FlipbookName = TEXT("Attack");
		ReplacementEntry.Identity.Flipbook = Rig.Flipbook;
		Replacement->Flipbooks.Add(MoveTemp(ReplacementEntry));

		UPaper2DPlusFrameCueMutationReceiver* Receiver =
			NewObject<UPaper2DPlusFrameCueMutationReceiver>();
		Receiver->TriggerCue = MutatorCue;
		Receiver->TargetProfile = Replacement;
		Rig.ProfileComponent->OnFrameCue.AddDynamic(
			Receiver, &UPaper2DPlusFrameCueMutationReceiver::OnCue);

		Rig.ProfileComponent->HandleFrameChanged(0);
		Rig.ProfileComponent->HandleFrameChanged(1);
		TestEqual(TEXT("profile receiver mutated once"), Receiver->MutationCount, 1);
		TestTrue(TEXT("replacement profile committed"),
			Rig.ProfileComponent->CharacterProfile == Replacement);
		TestFalse(TEXT("old-profile range ended after deferred rewarm"),
			Rig.ProfileComponent->IsRangeCueActiveForTests(Range));
		TestEqual(TEXT("profile mutation yields one Begin/Trigger/End sequence"),
			Rig.Recorder->Contexts.Num(), 3);
		if (Rig.Recorder->Contexts.Num() == 3)
		{
			TestEqual(TEXT("profile mutation End reason"),
				Rig.Recorder->Contexts[2].EndReason, EPaper2DPlusFrameCueEndReason::AnimationChanged);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCueQueryFiltersTest,
	"Paper2DPlus.FrameCues.Runtime.QueryFiltersAndAuthoredOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCueQueryFiltersTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Profile = NewObject<UPaper2DPlusCharacterProfileAsset>();
	UPaperFlipbook* AttackFlipbook = NewObject<UPaperFlipbook>();
	UPaperFlipbook* IdleFlipbook = NewObject<UPaperFlipbook>();

	UPaper2DPlusTestMomentCue* HeavyMoment = NewObject<UPaper2DPlusTestMomentCue>(Profile);
	HeavyMoment->CueTag = Paper2DPlusAnimationTags::Combat_Heavy;
	HeavyMoment->TriggerFrame = 2;
	UPaper2DPlusTestRangeCue* CombatRange = NewObject<UPaper2DPlusTestRangeCue>(Profile);
	CombatRange->CueTag = Paper2DPlusAnimationTags::Combat;
	CombatRange->StartFrame = 1;
	CombatRange->FrameCount = 3;
	UPaper2DPlusTestMomentCue* LightMoment = NewObject<UPaper2DPlusTestMomentCue>(Profile);
	LightMoment->CueTag = Paper2DPlusAnimationTags::Combat_Light;
	LightMoment->TriggerFrame = 0;

	FFlipbookProfileEntry AttackEntry;
	AttackEntry.Identity.FlipbookName = TEXT("Attack");
	AttackEntry.Identity.Flipbook = AttackFlipbook;
	AttackEntry.FrameEventData.FrameCues.Add(HeavyMoment);
	AttackEntry.FrameEventData.FrameCues.Add(nullptr); // invalid authored row is safely omitted
	AttackEntry.FrameEventData.FrameCues.Add(CombatRange);
	Profile->Flipbooks.Add(MoveTemp(AttackEntry));

	FFlipbookProfileEntry IdleEntry;
	IdleEntry.Identity.FlipbookName = TEXT("Idle");
	IdleEntry.Identity.Flipbook = IdleFlipbook;
	IdleEntry.FrameEventData.FrameCues.Add(LightMoment);
	Profile->Flipbooks.Add(MoveTemp(IdleEntry));

	Paper2DPlusFrameCueRuntimeTest::CheckCueOrder(
		*this,
		TEXT("Derived class filter"),
		UPaper2DPlusBlueprintLibrary::GetFrameCuesByClass(
			Profile, UPaper2DPlusCue::StaticClass(), false),
		{HeavyMoment, LightMoment});
	Paper2DPlusFrameCueRuntimeTest::CheckCueOrder(
		*this,
		TEXT("Exact concrete class filter"),
		UPaper2DPlusBlueprintLibrary::GetFrameCuesByClass(
			Profile, UPaper2DPlusTestMomentCue::StaticClass(), true),
		{HeavyMoment, LightMoment});
	TestTrue(
		TEXT("Exact abstract/base class does not include derived placements"),
		UPaper2DPlusBlueprintLibrary::GetFrameCuesByClass(
			Profile, UPaper2DPlusCue::StaticClass(), true).IsEmpty());
	TestTrue(
		TEXT("Null class returns empty"),
		UPaper2DPlusBlueprintLibrary::GetFrameCuesByClass(
			Profile, TSubclassOf<UPaper2DPlusCueBase>(), false).IsEmpty());

	Paper2DPlusFrameCueRuntimeTest::CheckCueOrder(
		*this,
		TEXT("Hierarchical tag filter"),
		UPaper2DPlusBlueprintLibrary::GetFrameCuesByTag(
			Profile, Paper2DPlusAnimationTags::Combat, false),
		{HeavyMoment, CombatRange, LightMoment});
	Paper2DPlusFrameCueRuntimeTest::CheckCueOrder(
		*this,
		TEXT("Exact tag filter"),
		UPaper2DPlusBlueprintLibrary::GetFrameCuesByTag(
			Profile, Paper2DPlusAnimationTags::Combat, true),
		{CombatRange});
	TestTrue(
		TEXT("Invalid tag returns empty instead of matching all cues"),
		UPaper2DPlusBlueprintLibrary::GetFrameCuesByTag(Profile, FGameplayTag(), false).IsEmpty());
	TestTrue(
		TEXT("Null asset class query returns empty"),
		UPaper2DPlusBlueprintLibrary::GetFrameCuesByClass(
			nullptr, UPaper2DPlusCueBase::StaticClass(), false).IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCueQueryParityTest,
	"Paper2DPlus.FrameCues.Runtime.QueryAnimationFrameAndActiveRangeParity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCueQueryParityTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusTestRangeCue* RangeA = NewObject<UPaper2DPlusTestRangeCue>();
	RangeA->StartFrame = 0;
	RangeA->FrameCount = 3;
	UPaper2DPlusTestMomentCue* Moment = NewObject<UPaper2DPlusTestMomentCue>();
	Moment->TriggerFrame = 1;
	UPaper2DPlusTestRangeCue* RangeB = NewObject<UPaper2DPlusTestRangeCue>();
	RangeB->StartFrame = 1;
	RangeB->FrameCount = 2;

	Paper2DPlusFrameCueRuntimeTest::FRig Rig =
		Paper2DPlusFrameCueRuntimeTest::MakeRig({RangeA, Moment, RangeB});

	const TArray<UPaper2DPlusCueBase*> NameAnimation =
		UPaper2DPlusBlueprintLibrary::GetFrameCuesForAnimation(Rig.Profile, TEXT("attack"));
	const TArray<UPaper2DPlusCueBase*> ObjectAnimation =
		UPaper2DPlusBlueprintLibrary::GetFrameCuesForFlipbook(Rig.Profile, Rig.Flipbook);
	Paper2DPlusFrameCueRuntimeTest::CheckCueOrder(
		*this, TEXT("Name-keyed animation"), NameAnimation, {RangeA, Moment, RangeB});
	Paper2DPlusFrameCueRuntimeTest::CheckCueOrder(
		*this, TEXT("Animation name/flipbook parity"), ObjectAnimation, NameAnimation);

	const TArray<UPaper2DPlusCueBase*> NameAnchored =
		UPaper2DPlusBlueprintLibrary::GetFrameCuesAtKeyFrame(Rig.Profile, TEXT("Attack"), 1);
	const TArray<UPaper2DPlusCueBase*> ObjectAnchored =
		UPaper2DPlusBlueprintLibrary::GetFrameCuesAtKeyFrameByFlipbook(Rig.Profile, Rig.Flipbook, 1);
	Paper2DPlusFrameCueRuntimeTest::CheckCueOrder(
		*this, TEXT("Primary anchors at key frame"), NameAnchored, {Moment, RangeB});
	Paper2DPlusFrameCueRuntimeTest::CheckCueOrder(
		*this, TEXT("Key-frame name/flipbook parity"), ObjectAnchored, NameAnchored);

	const TArray<UPaper2DPlusCueBase*> NameContaining =
		UPaper2DPlusBlueprintLibrary::GetFrameCueRangesContainingKeyFrame(
			Rig.Profile, TEXT("Attack"), 1);
	const TArray<UPaper2DPlusCueBase*> ObjectContaining =
		UPaper2DPlusBlueprintLibrary::GetFrameCueRangesContainingKeyFrameByFlipbook(
			Rig.Profile, Rig.Flipbook, 1);
	Paper2DPlusFrameCueRuntimeTest::CheckCueOrder(
		*this, TEXT("Ranges containing key frame"), NameContaining, {RangeA, RangeB});
	Paper2DPlusFrameCueRuntimeTest::CheckCueOrder(
		*this, TEXT("Containing-range name/flipbook parity"), ObjectContaining, NameContaining);

	Rig.ProfileComponent->HandleFrameChanged(0);
	const TArray<UPaper2DPlusCueBase*> ComponentActive =
		Rig.ProfileComponent->GetActiveFrameCueRanges();
	const TArray<UPaper2DPlusCueBase*> ActorActive =
		UPaper2DPlusBlueprintLibrary::GetActorActiveFrameCueRanges(Rig.Actor);
	Paper2DPlusFrameCueRuntimeTest::CheckCueOrder(
		*this, TEXT("Component active ranges preserve authored order"), ComponentActive, {RangeA});
	Paper2DPlusFrameCueRuntimeTest::CheckCueOrder(
		*this, TEXT("Actor/component active-range parity"), ActorActive, ComponentActive);

	UPaperFlipbook* ForeignFlipbook = NewObject<UPaperFlipbook>();
	TestTrue(TEXT("Empty animation name returns empty"),
		UPaper2DPlusBlueprintLibrary::GetFrameCuesForAnimation(Rig.Profile, FString()).IsEmpty());
	TestTrue(TEXT("Unknown animation returns empty"),
		UPaper2DPlusBlueprintLibrary::GetFrameCuesForAnimation(Rig.Profile, TEXT("Missing")).IsEmpty());
	TestTrue(TEXT("Foreign flipbook returns empty"),
		UPaper2DPlusBlueprintLibrary::GetFrameCuesForFlipbook(Rig.Profile, ForeignFlipbook).IsEmpty());
	TestTrue(TEXT("Negative key frame returns empty"),
		UPaper2DPlusBlueprintLibrary::GetFrameCuesAtKeyFrame(Rig.Profile, TEXT("Attack"), -1).IsEmpty());
	TestTrue(TEXT("Null actor returns no active ranges"),
		UPaper2DPlusBlueprintLibrary::GetActorActiveFrameCueRanges(nullptr).IsEmpty());
	TestTrue(TEXT("Actor without a profile component returns no active ranges"),
		UPaper2DPlusBlueprintLibrary::GetActorActiveFrameCueRanges(NewObject<AActor>()).IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCueCookedRuntimeTest,
	"Paper2DPlus.FrameCues.CueType.CookedRuntime.LoadAndDispatch",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCueCookedRuntimeTest::RunTest(const FString& Parameters)
{
	if (!FParse::Param(FCommandLine::Get(), TEXT("Paper2DPlusCueTypeCookedRuntimeProof")))
	{
		AddError(TEXT(
			"Cooked Frame Cue Type proof requires "
			"-Paper2DPlusCueTypeCookedRuntimeProof in a cooked client."));
		return false;
	}

	static const TCHAR* FixturePackagePath =
		TEXT("/Game/Paper2DPlusAutomation/P2DPCueTypeCookedRuntimeFixture");
	static const TCHAR* GeneratedClassPath =
		TEXT("/Game/Paper2DPlusAutomation/P2DPCueTypeCookedRuntimeFixture.BP_PersistentCueType_C");
	static const TCHAR* RangeGeneratedClassPath =
		TEXT("/Game/Paper2DPlusAutomation/P2DPCueTypeCookedRuntimeFixture.BP_PersistentRangeCueType_C");
	static const TCHAR* ProfilePath =
		TEXT("/Game/Paper2DPlusAutomation/P2DPCueTypeCookedRuntimeFixture.PersistentCueProfile");
	static const TCHAR* EnvelopeClassPath =
		TEXT("/Script/Paper2DPlus.Paper2DPlusFrameCueBlueprint");

	TestFalse(TEXT("Cooked runtime proof excludes editor code"), WITH_EDITOR != 0);
	TestFalse(TEXT("Cooked runtime proof excludes editor-only data"), WITH_EDITORONLY_DATA != 0);
	TestTrue(TEXT("Target platform requires cooked data"), FPlatformProperties::RequiresCookedData());

	FModuleManager& ModuleManager = FModuleManager::Get();
	TestFalse(
		TEXT("Paper2DPlusEditor has no staged runtime module binary"),
		ModuleManager.ModuleExists(TEXT("Paper2DPlusEditor")));
	TestFalse(
		TEXT("Paper2DPlusEditor is not loaded"),
		ModuleManager.IsModuleLoaded(TEXT("Paper2DPlusEditor")));

	UClass* EnvelopeClass = FindObject<UClass>(nullptr, EnvelopeClassPath);
	TestNotNull(TEXT("Runtime Cue Type asset envelope class resolves"), EnvelopeClass);
	if (EnvelopeClass)
	{
		TestEqual(
			TEXT("Runtime Cue Type asset envelope resolves at the exact script path"),
			EnvelopeClass->GetPathName(),
			FString(EnvelopeClassPath));
		TestTrue(
			TEXT("Resolved asset envelope is the Paper2DPlus runtime class"),
			EnvelopeClass == UPaper2DPlusFrameCueBlueprint::StaticClass());
		TestNull(
			TEXT("DurableSchemaVersion reflection is stripped from cooked runtime"),
			FindFProperty<FProperty>(EnvelopeClass, TEXT("DurableSchemaVersion")));
		TestNull(
			TEXT("DurableSchemaFingerprint reflection is stripped from cooked runtime"),
			FindFProperty<FProperty>(EnvelopeClass, TEXT("DurableSchemaFingerprint")));
	}

	UClass* GeneratedCueClass = LoadObject<UClass>(nullptr, GeneratedClassPath);
	if (!TestNotNull(TEXT("Cooked generated Cue class loads by exact path"), GeneratedCueClass))
	{
		return false;
	}
	TestEqual(
		TEXT("Loaded generated Cue class keeps its deterministic path"),
		GeneratedCueClass->GetPathName(),
		FString(GeneratedClassPath));
	TestTrue(
		TEXT("Loaded generated Cue class derives from Cue"),
		GeneratedCueClass->IsChildOf(UPaper2DPlusCue::StaticClass()));
	TestTrue(
		TEXT("Cooked Cue class retains the runtime generated-class envelope"),
		GeneratedCueClass->GetClass()->IsChildOf(
			UPaper2DPlusFrameCueBlueprintGeneratedClass::StaticClass()));
	TestFalse(
		TEXT("Loaded generated Cue class is not a Cue State"),
		GeneratedCueClass->IsChildOf(UPaper2DPlusCueState::StaticClass()));

	UClass* GeneratedRangeCueClass = LoadObject<UClass>(nullptr, RangeGeneratedClassPath);
	if (!TestNotNull(TEXT("Cooked generated Cue State class loads by exact path"), GeneratedRangeCueClass))
	{
		return false;
	}
	TestEqual(
		TEXT("Loaded generated Cue State class keeps its deterministic path"),
		GeneratedRangeCueClass->GetPathName(),
		FString(RangeGeneratedClassPath));
	TestTrue(
		TEXT("Loaded generated Cue State class derives from Cue State"),
		GeneratedRangeCueClass->IsChildOf(UPaper2DPlusCueState::StaticClass()));
	TestTrue(
		TEXT("Cooked Range class retains the runtime generated-class envelope"),
		GeneratedRangeCueClass->GetClass()->IsChildOf(
			UPaper2DPlusFrameCueBlueprintGeneratedClass::StaticClass()));
	const UPaper2DPlusCueState* RangeClassDefaults = Cast<UPaper2DPlusCueState>(
		GeneratedRangeCueClass->GetDefaultObject());
	TestTrue(
		TEXT("Cooked Range class preserves its Update lifecycle default"),
		RangeClassDefaults && RangeClassDefaults->bEmitUpdates);

	UPackage* FixturePackage = GeneratedCueClass->GetOutermost();
	if (!TestNotNull(TEXT("Cooked generated Cue class has a package"), FixturePackage))
	{
		return false;
	}
	TestEqual(
		TEXT("Generated Cue class belongs to the deterministic fixture package"),
		FixturePackage->GetName(),
		FString(FixturePackagePath));
	TestTrue(
		TEXT("Generated Cue class package carries PKG_Cooked"),
		FixturePackage->HasAnyPackageFlags(PKG_Cooked));

	UPaper2DPlusCharacterProfileAsset* Profile =
		LoadObject<UPaper2DPlusCharacterProfileAsset>(nullptr, ProfilePath);
	if (!TestNotNull(TEXT("Cooked Character Profile loads by exact path"), Profile))
	{
		return false;
	}
	TestEqual(
		TEXT("Loaded Character Profile keeps its deterministic path"),
		Profile->GetPathName(),
		FString(ProfilePath));
	TestTrue(
		TEXT("Generated Cue class and Character Profile share the fixture package"),
		Profile->GetOutermost() == FixturePackage);
	TestTrue(
		TEXT("Generated Cue State class shares the fixture package"),
		GeneratedRangeCueClass->GetOutermost() == FixturePackage);
	TestTrue(
		TEXT("Character Profile package carries PKG_Cooked"),
		Profile->GetOutermost()->HasAnyPackageFlags(PKG_Cooked));
	TestNull(
		TEXT("Cooked package contains no Blueprint authoring object"),
		FindObject<UObject>(FixturePackage, TEXT("BP_PersistentCueType")));
	TestNull(
		TEXT("Cooked package contains no Range Blueprint authoring object"),
		FindObject<UObject>(FixturePackage, TEXT("BP_PersistentRangeCueType")));

	if (!TestEqual(TEXT("Cooked Character Profile has one animation entry"), Profile->Flipbooks.Num(), 1))
	{
		return false;
	}
	FFlipbookProfileEntry& Entry = Profile->Flipbooks[0];
	if (!TestEqual(
		TEXT("Cooked Character Profile has exactly two Cue placements"),
		Entry.FrameEventData.FrameCues.Num(),
		2))
	{
		return false;
	}

	UPaper2DPlusCueBase* Placement = Entry.FrameEventData.FrameCues[0];
	if (!TestNotNull(TEXT("Cooked Cue placement resolves"), Placement))
	{
		return false;
	}
	TestTrue(
		TEXT("Cooked placement uses the exact generated Cue class"),
		Placement->GetClass() == GeneratedCueClass);
	TestTrue(
		TEXT("Cooked placement remains owned by its Character Profile"),
		Placement->GetOuter() == Profile);
	TestEqual(TEXT("Cooked Cue anchor is frame 3"), Placement->GetPrimaryAnchorFrame(), 3);

	FIntProperty* PowerProperty =
		CastField<FIntProperty>(FindFProperty<FProperty>(GeneratedCueClass, TEXT("Power")));
	if (!TestNotNull(TEXT("Generated Cue class contains the integer Power payload"), PowerProperty))
	{
		return false;
	}
	const UPaper2DPlusCueBase* ClassDefaultCue =
		Cast<UPaper2DPlusCueBase>(GeneratedCueClass->GetDefaultObject());
	if (!TestNotNull(TEXT("Generated Cue class has a Cue class default object"), ClassDefaultCue))
	{
		return false;
	}
	TestEqual(
		TEXT("Cooked Cue class default preserves Power 12"),
		PowerProperty->GetPropertyValue_InContainer(ClassDefaultCue),
		12);
	TestEqual(
		TEXT("Cooked placement preserves its Power 37 override"),
		PowerProperty->GetPropertyValue_InContainer(Placement),
		37);

	UPaper2DPlusCueState* RangePlacement = Cast<UPaper2DPlusCueState>(
		Entry.FrameEventData.FrameCues[1]);
	if (!TestNotNull(TEXT("Cooked Cue State placement resolves"), RangePlacement))
	{
		return false;
	}
	TestTrue(
		TEXT("Cooked Range placement uses the exact generated Range class"),
		RangePlacement->GetClass() == GeneratedRangeCueClass);
	TestTrue(
		TEXT("Cooked Range placement remains owned by its Character Profile"),
		RangePlacement->GetOuter() == Profile);
	TestEqual(TEXT("Cooked Range start is frame 0"), RangePlacement->StartFrame, 0);
	TestEqual(TEXT("Cooked Range spans two frames"), RangePlacement->FrameCount, 2);
	TestTrue(TEXT("Cooked Range placement emits Updates"), RangePlacement->bEmitUpdates);
	FIntProperty* RangePowerProperty =
		CastField<FIntProperty>(FindFProperty<FProperty>(GeneratedRangeCueClass, TEXT("Power")));
	if (!TestNotNull(TEXT("Generated Range class contains the integer Power payload"),
		RangePowerProperty))
	{
		return false;
	}
	TestEqual(
		TEXT("Cooked Range placement preserves its Power 83 override"),
		RangePowerProperty->GetPropertyValue_InContainer(RangePlacement),
		83);

	// The retained fixture's cooked Profile and placements are the persistence authority. Build only
	// the live spatial/playback side transiently here so the packaged-client proof does not require an
	// editor asset or an editor module at runtime.
	UPaperFlipbook* RuntimeFlipbook = NewObject<UPaperFlipbook>();
	{
		FScopedFlipbookMutator Mutator(RuntimeFlipbook);
		Mutator.FramesPerSecond = 10.0f;
		Mutator.KeyFrames.Reset();
		for (int32 FrameIndex = 0; FrameIndex < 4; ++FrameIndex)
		{
			FPaperFlipbookKeyFrame KeyFrame;
			KeyFrame.FrameRun = 1;
			Mutator.KeyFrames.Add(KeyFrame);
		}
	}
	Entry.Identity.Flipbook = RuntimeFlipbook;
	Entry.CombatData.Frames.SetNum(4);
	Entry.CombatData.FrameExtractionInfo.SetNum(4);
	for (FSpriteExtractionInfo& ExtractionInfo : Entry.CombatData.FrameExtractionInfo)
	{
		ExtractionInfo.CachedPivotLocal = FVector2D(6.25f, 7.75f);
	}
	FSocketData CookedSocket;
	CookedSocket.Name = TEXT("CookedHand");
	CookedSocket.X = 11;
	CookedSocket.Y = 3;
	Entry.CombatData.Frames[0].Sockets.Add(CookedSocket);
	Profile->InvalidateFlipbookLookupCache();

	Paper2DPlusFrameCueRuntimeTest::FCookedRuntimeWorld RuntimeWorld;
	if (!TestNotNull(TEXT("Cooked proof creates a real runtime world"), RuntimeWorld.World))
	{
		return false;
	}
	AActor* const RuntimeActor = RuntimeWorld.World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("Cooked proof spawns a runtime owner"), RuntimeActor))
	{
		return false;
	}
	UPaperFlipbookComponent* const RuntimePlayback =
		NewObject<UPaperFlipbookComponent>(RuntimeActor);
	RuntimePlayback->SetFlipbook(RuntimeFlipbook);
	RuntimeActor->SetRootComponent(RuntimePlayback);
	RuntimePlayback->RegisterComponent();
	const FTransform RuntimeComponentTransform(
		FRotator(0.0f, 180.0f, 0.0f),
		FVector(120.0f, 7.0f, 55.0f),
		FVector(2.0f, 1.5f, 3.0f));
	RuntimePlayback->SetWorldTransform(RuntimeComponentTransform);

	UPaper2DPlusCharacterProfileComponent* const RuntimeProfileComponent =
		NewObject<UPaper2DPlusCharacterProfileComponent>(RuntimeActor);
	RuntimeActor->AddOwnedComponent(RuntimeProfileComponent);
	RuntimeProfileComponent->CharacterProfile = Profile;
	RuntimeProfileComponent->SetFrameCuePlaybackSource(RuntimePlayback);
	UPaper2DPlusFrameCueRecorder* const RuntimeRecorder =
		NewObject<UPaper2DPlusFrameCueRecorder>();
	RuntimeProfileComponent->OnFrameCue.AddDynamic(
		RuntimeRecorder,
		&UPaper2DPlusFrameCueRecorder::OnCue);
	RuntimeProfileComponent->HandleFlipbookChanged(RuntimeFlipbook);

	FPaper2DPlusFrameCueContext BaseContext;
	BaseContext.OwningActor = RuntimeActor;
	BaseContext.ProfileComponent = RuntimeProfileComponent;
	BaseContext.Flipbook = RuntimeFlipbook;
	BaseContext.AnimationName = FName(*Entry.Identity.FlipbookName);
	BaseContext.CharacterProfile = Profile;
	BaseContext.PlaybackComponent = RuntimePlayback;
	BaseContext.SetEvaluationMode(EPaper2DPlusFrameCueEvaluationMode::RuntimePlayback);
	TSet<TObjectPtr<UPaper2DPlusCueBase>> ActiveRanges;
	TArray<TObjectPtr<UPaper2DPlusCueBase>> DispatchedCues;
	TArray<FPaper2DPlusFrameCueContext> Notifications;
	const auto Dispatch = [&Entry, &BaseContext, &ActiveRanges, &DispatchedCues, &Notifications](
		int32 PreviousFrame,
		int32 CurrentFrame)
	{
		BaseContext.PreviousFrame = PreviousFrame;
		BaseContext.CurrentFrame = CurrentFrame;
		Paper2DPlusFrameCues::DispatchFrameTransition(
			Entry.FrameEventData.FrameCues,
			BaseContext,
			ActiveRanges,
			[](UPaper2DPlusCueBase&) { return true; },
			[&DispatchedCues, &Notifications](
				UPaper2DPlusCueBase& Cue,
				const FPaper2DPlusFrameCueContext& Notification)
			{
				DispatchedCues.Add(&Cue);
				Notifications.Add(Notification);
			});
	};

	Dispatch(-1, 0);
	TestEqual(TEXT("Cooked Range entry emits Begin plus its enabled Update"), Notifications.Num(), 2);
	if (Notifications.Num() == 2 && DispatchedCues.Num() == 2)
	{
		TestTrue(TEXT("Range Begin reports the cooked Range placement"),
			DispatchedCues[0] == RangePlacement);
		TestEqual(TEXT("Cooked Range dispatches Begin"),
			Notifications[0].Phase, EPaper2DPlusFrameCuePhase::Begin);
		TestTrue(TEXT("Entry-frame Range Update reports the cooked Range placement"),
			DispatchedCues[1] == RangePlacement);
		TestEqual(TEXT("Cooked Range dispatches its enabled entry-frame Update"),
			Notifications[1].Phase, EPaper2DPlusFrameCuePhase::Update);
	}
	if (!Notifications.IsEmpty())
	{
		const FPaper2DPlusFrameCueContext& CookedContext = Notifications[0];
		TestTrue(TEXT("Cooked callback carries the exact Character Profile"),
			CookedContext.CharacterProfile == Profile);
		TestTrue(TEXT("Cooked callback carries the live playback component"),
			CookedContext.PlaybackComponent == RuntimePlayback);
		TestEqual(TEXT("Cooked callback reports runtime playback evaluation"),
			CookedContext.EvaluationMode,
			EPaper2DPlusFrameCueEvaluationMode::RuntimePlayback);
		TestFalse(TEXT("Cooked callback is not editor preview"), CookedContext.bIsEditorPreview);
		TestFalse(TEXT("Cooked callback is not catch-up"), CookedContext.bIsCatchUp);

		FTransform AnchorTransform = FTransform::Identity;
		UPaperFlipbookComponent* AttachmentComponent = nullptr;
		EPaper2DPlusFrameCueAnchorResult AnchorResult =
			UPaper2DPlusBlueprintLibrary::ResolveFrameCueAnchor(
				CookedContext,
				EPaper2DPlusFrameCueAnchorKind::RenderOrigin,
				FString(),
				AnchorTransform,
				AttachmentComponent);
		TestEqual(TEXT("Cooked Render Origin resolves"), AnchorResult,
			EPaper2DPlusFrameCueAnchorResult::Success);
		TestTrue(TEXT("Cooked Render Origin is the live component transform"),
			AnchorTransform.Equals(RuntimePlayback->GetComponentTransform()));
		TestTrue(TEXT("Cooked Render Origin returns the live attachment source"),
			AttachmentComponent == RuntimePlayback);

		AnchorResult = UPaper2DPlusBlueprintLibrary::ResolveFrameCueAnchor(
			CookedContext,
			EPaper2DPlusFrameCueAnchorKind::ProfileSocket,
			TEXT("cookedhand"),
			AnchorTransform,
			AttachmentComponent);
		TestEqual(TEXT("Cooked Profile Socket resolves from serialized runtime geometry"),
			AnchorResult,
			EPaper2DPlusFrameCueAnchorResult::Success);
		TestTrue(TEXT("Cooked Profile Socket preserves component rotation"),
			AnchorTransform.GetRotation().Equals(
				RuntimePlayback->GetComponentQuat(),
				KINDA_SMALL_NUMBER));
		TestTrue(TEXT("Cooked Profile Socket preserves component scale"),
			AnchorTransform.GetScale3D().Equals(
				RuntimePlayback->GetComponentScale(),
				KINDA_SMALL_NUMBER));
		TestTrue(TEXT("Cooked Profile Socket returns the live attachment source"),
			AttachmentComponent == RuntimePlayback);

		AnchorResult = UPaper2DPlusBlueprintLibrary::ResolveFrameCueAnchor(
			CookedContext,
			EPaper2DPlusFrameCueAnchorKind::ProfileSocket,
			TEXT("MissingCookedSocket"),
			AnchorTransform,
			AttachmentComponent);
		TestEqual(TEXT("A missing cooked Profile Socket fails explicitly"),
			AnchorResult,
			EPaper2DPlusFrameCueAnchorResult::SocketNotFound);
		TestTrue(TEXT("A missing cooked socket resets its transform"),
			AnchorTransform.Equals(FTransform::Identity));
		TestNull(TEXT("A missing cooked socket clears its attachment source"),
			AttachmentComponent);
	}
	TestTrue(TEXT("Cooked Range is active after Begin"), ActiveRanges.Contains(RangePlacement));

	Notifications.Reset();
	DispatchedCues.Reset();
	Dispatch(0, 1);
	TestEqual(TEXT("Cooked Range interior emits one Update"), Notifications.Num(), 1);
	if (Notifications.Num() == 1 && DispatchedCues.Num() == 1)
	{
		TestTrue(TEXT("Range Update reports the cooked Range placement"),
			DispatchedCues[0] == RangePlacement);
		TestEqual(TEXT("Cooked Range dispatches Update"),
			Notifications[0].Phase, EPaper2DPlusFrameCuePhase::Update);
	}

	Notifications.Reset();
	DispatchedCues.Reset();
	Dispatch(1, 2);
	TestEqual(TEXT("Cooked Range exit emits one paired End"), Notifications.Num(), 1);
	if (Notifications.Num() == 1 && DispatchedCues.Num() == 1)
	{
		TestTrue(TEXT("Range End reports the cooked Range placement"),
			DispatchedCues[0] == RangePlacement);
		TestEqual(TEXT("Cooked Range dispatches End"),
			Notifications[0].Phase, EPaper2DPlusFrameCuePhase::End);
	}
	TestEqual(TEXT("Cooked Range is inactive after End"), ActiveRanges.Num(), 0);

	Notifications.Reset();
	DispatchedCues.Reset();
	Dispatch(2, 3);
	TestEqual(TEXT("Cooked Moment transition emits exactly one Trigger"), Notifications.Num(), 1);
	if (Notifications.Num() == 1 && DispatchedCues.Num() == 1)
	{
		TestTrue(TEXT("Moment Trigger reports the cooked Moment placement"),
			DispatchedCues[0] == Placement);
		TestEqual(TEXT("Cooked Cue dispatches Trigger"),
			Notifications[0].Phase, EPaper2DPlusFrameCuePhase::Trigger);
	}

	RuntimePlayback->Stop();
	RuntimePlayback->SetPlaybackPosition(0.05f, /*bFireEvents=*/false);
	RuntimeProfileComponent->HandleFlipbookChanged(RuntimeFlipbook);
	RuntimeRecorder->Contexts.Reset();
	const int64 CompletedGeneration =
		RuntimeProfileComponent->BeginExternalFrameCuePlayback(RuntimePlayback);
	TestTrue(TEXT("Cooked external playback opens a completion generation"),
		CompletedGeneration > 0);
	if (!RuntimeProfileComponent->IsRangeCueActiveForTests(RangePlacement))
	{
		RuntimeProfileComponent->HandleFrameChanged(0);
	}
	TestTrue(TEXT("Cooked Cue State is active before completion"),
		RuntimeProfileComponent->IsRangeCueActiveForTests(RangePlacement));
	TestTrue(TEXT("Cooked completion report is accepted"),
		RuntimeProfileComponent->ReportFrameCuePlaybackCompleted(
			RuntimePlayback,
			CompletedGeneration));
	TestFalse(TEXT("Cooked completion report is idempotent"),
		RuntimeProfileComponent->ReportFrameCuePlaybackCompleted(
			RuntimePlayback,
			CompletedGeneration));

	int32 CompletedEndCount = 0;
	for (const FPaper2DPlusFrameCueContext& Context : RuntimeRecorder->Contexts)
	{
		if (Context.Phase == EPaper2DPlusFrameCuePhase::End
			&& Context.EndReason == EPaper2DPlusFrameCueEndReason::Completed)
		{
			++CompletedEndCount;
		}
	}
	TestEqual(TEXT("Cooked completion emits exactly one Completed End"),
		CompletedEndCount, 1);
	TestFalse(TEXT("Cooked completion clears the active Cue State"),
		RuntimeProfileComponent->IsRangeCueActiveForTests(RangePlacement));
	if (!RuntimeRecorder->Contexts.IsEmpty())
	{
		const FPaper2DPlusFrameCueContext& CompletedContext =
			RuntimeRecorder->Contexts.Last();
		TestEqual(TEXT("Cooked completion listener receives End"),
			CompletedContext.Phase, EPaper2DPlusFrameCuePhase::End);
		TestEqual(TEXT("Cooked completion listener receives Completed"),
			CompletedContext.EndReason, EPaper2DPlusFrameCueEndReason::Completed);
		TestTrue(TEXT("Cooked completion preserves the direct Profile"),
			CompletedContext.CharacterProfile == Profile);
		TestTrue(TEXT("Cooked completion preserves the live playback source"),
			CompletedContext.PlaybackComponent == RuntimePlayback);
		TestEqual(TEXT("Cooked completion preserves runtime evaluation mode"),
			CompletedContext.EvaluationMode,
			EPaper2DPlusFrameCueEvaluationMode::RuntimePlayback);
	}

	RuntimePlayback->SetPlaybackPosition(0.05f, /*bFireEvents=*/false);
	RuntimeRecorder->Contexts.Reset();
	const int64 StoppedGeneration =
		RuntimeProfileComponent->BeginExternalFrameCuePlayback(RuntimePlayback);
	TestTrue(TEXT("Cooked external playback opens a distinct stop generation"),
		StoppedGeneration > 0 && StoppedGeneration != CompletedGeneration);
	if (!RuntimeProfileComponent->IsRangeCueActiveForTests(RangePlacement))
	{
		RuntimeProfileComponent->HandleFrameChanged(0);
	}
	TestTrue(TEXT("Cooked Cue State is active before explicit stop"),
		RuntimeProfileComponent->IsRangeCueActiveForTests(RangePlacement));
	TestTrue(TEXT("Cooked explicit stop report is accepted"),
		RuntimeProfileComponent->ReportFrameCuePlaybackStopped(
			RuntimePlayback,
			StoppedGeneration));
	TestFalse(TEXT("Cooked explicit stop report is idempotent"),
		RuntimeProfileComponent->ReportFrameCuePlaybackStopped(
			RuntimePlayback,
			StoppedGeneration));

	int32 PlaybackStoppedEndCount = 0;
	for (const FPaper2DPlusFrameCueContext& Context : RuntimeRecorder->Contexts)
	{
		if (Context.Phase == EPaper2DPlusFrameCuePhase::End
			&& Context.EndReason == EPaper2DPlusFrameCueEndReason::PlaybackStopped)
		{
			++PlaybackStoppedEndCount;
		}
	}
	TestEqual(TEXT("Cooked explicit stop emits exactly one PlaybackStopped End"),
		PlaybackStoppedEndCount, 1);
	TestFalse(TEXT("Cooked explicit stop clears the active Cue State"),
		RuntimeProfileComponent->IsRangeCueActiveForTests(RangePlacement));
	if (!RuntimeRecorder->Contexts.IsEmpty())
	{
		const FPaper2DPlusFrameCueContext& StoppedContext =
			RuntimeRecorder->Contexts.Last();
		TestEqual(TEXT("Cooked explicit stop listener receives End"),
			StoppedContext.Phase, EPaper2DPlusFrameCuePhase::End);
		TestEqual(TEXT("Cooked explicit stop listener receives PlaybackStopped"),
			StoppedContext.EndReason,
			EPaper2DPlusFrameCueEndReason::PlaybackStopped);
		TestTrue(TEXT("Cooked explicit stop preserves the direct Profile"),
			StoppedContext.CharacterProfile == Profile);
		TestTrue(TEXT("Cooked explicit stop preserves the live playback source"),
			StoppedContext.PlaybackComponent == RuntimePlayback);
		TestEqual(TEXT("Cooked explicit stop preserves runtime evaluation mode"),
			StoppedContext.EvaluationMode,
			EPaper2DPlusFrameCueEvaluationMode::RuntimePlayback);
	}

	if (!HasAnyErrors())
	{
		UE_LOG(LogPaper2DPlus, Display, TEXT("P2DP_CUE_COOKED_RUNTIME_PASS"));
	}
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCueWarmDeclarationTest,
	"Paper2DPlus.FrameCues.Runtime.WarmingFindsScalarSoftFlipbooksStructurally",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCueWarmDeclarationTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusFrameCueRuntimeTest;

	// The warm pass is structural and class-blind: scalar soft flipbooks are collected, while hard
	// object payloads remain ordinary already-resident data.
	UPaperFlipbook* const SoftArt = NewObject<UPaperFlipbook>();
	UPaperFlipbook* const HardArt = NewObject<UPaperFlipbook>();

	UPaper2DPlusTestEffectArtCue* const Designer = NewObject<UPaper2DPlusTestEffectArtCue>();
	Designer->TriggerFrame = 0;
	Designer->SoftArt = SoftArt;
	Designer->HardArt = HardArt;

	TArray<TSoftObjectPtr<UPaperFlipbook>> Declared;
	Designer->CollectWarmableEffectArt(Declared);
	TestTrue(TEXT("A designer Cue Type's scalar soft art is collected"),
		Declared.ContainsByPredicate([SoftArt](const TSoftObjectPtr<UPaperFlipbook>& Art)
		{
			return Art.Get() == SoftArt;
		}));
	TestFalse(TEXT("A hard flipbook payload is not added to the soft warm set"),
		Declared.ContainsByPredicate([HardArt](const TSoftObjectPtr<UPaperFlipbook>& Art)
		{
			return Art.Get() == HardArt;
		}));

	// End to end through the production warm seam.
	FRig Rig = MakeRig({Designer});
	TestTrue(TEXT("A designer Cue Type's soft art is warmed for the current animation"),
		Rig.ProfileComponent->IsFrameCueEffectWarmedForTests(SoftArt));
	TestFalse(TEXT("Hard payload art is not pulled into the soft warm set"),
		Rig.ProfileComponent->IsFrameCueEffectWarmedForTests(HardArt));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
