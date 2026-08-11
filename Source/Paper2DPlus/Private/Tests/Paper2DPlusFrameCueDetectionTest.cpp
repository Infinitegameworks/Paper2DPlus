// Copyright 2026 Infinite Gameworks. All Rights Reserved.

// The detection seams this file drives are declared inside #if !UE_BUILD_SHIPPING on the component,
// so the whole file carries the same guard: BuildPlugin compiles a Shipping game configuration in
// which those members do not exist (the lesson the built-in behavior tests learned the hard way).
#include "Misc/Build.h"

#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "Misc/AutomationTest.h"

#include "Async/TaskGraphInterfaces.h"
#include "Engine/Engine.h"
#include "Engine/EngineBaseTypes.h"
#include "Engine/World.h"
#include "FrameCues/Paper2DPlusFrameCue.h"
#include "FrameCues/Paper2DPlusFrameCueDispatch.h"
#include "GameFramework/Actor.h"
#include "GameFramework/WorldSettings.h"
#include "Paper2DPlusAppearanceTypes.h"
#include "Paper2DPlusCharacterLayerAsset.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Paper2DPlusCharacterProfileComponent.h"
#include "Paper2DPlusFlipbookComponent.h"
#include "PaperFlipbook.h"
#include "PaperFlipbookComponent.h"
#include "Tests/Paper2DPlusTestFrameCueTypes.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

/*
 * The Frame Cue DETECTION layer, driven the way a game drives it.
 *
 * Every other Frame Cue rig in this suite calls HandleFlipbookChanged / HandleFrameChanged by hand,
 * which is exactly the layer a designer never touches — and exactly the layer the reported no-fire
 * lives in. These tests instead spawn an actor into a real game world and REGISTER real components,
 * so BeginPlay runs, the profile component's own UpdateTickState picks a real poll rate onto a real
 * tick function, and the sprite's playback timing is real.
 *
 * WHY THE FRAME LOOP IS OURS AND NOT UWorld::Tick. An earlier revision of this file advanced time
 * with World->Tick(LEVELTICK_All, dt). That is silently unusable from inside an automation test: the
 * test itself already runs inside the editor's frame, so the nested tick re-enters the global tick
 * task manager and component tick functions execute on the FIRST call only. Measured, not assumed —
 * across sixty World->Tick calls the profile component polled once and the sprite advanced once,
 * which made every dispatch assertion here unfalsifiable. So the frame loop below is explicit:
 *
 *   - both components tick through FActorComponentTickFunction::ExecuteTick, the engine's own entry
 *     point, so registration, validity, and time-dilation gating are the shipping ones;
 *   - the sprite advances playback every frame, exactly as its own TG_DuringPhysics tick does;
 *   - the detection watch runs on ITS OWN schedule, honouring the TickInterval
 *     the component chose, with the true elapsed gap as DeltaTime — an interval tick function runs
 *     once when first queued and is then rescheduled by its interval, which is the whole reason a
 *     50 ms watch can step over a 20 ms animation without ever seeing it;
 *   - the watch samples BEFORE the sprite advances, matching production ordering (the profile
 *     component ticks in TG_PrePhysics, the sprite in TG_DuringPhysics).
 *
 * What is asserted about the SCHEDULER — which rate was chosen, whether the tick function is enabled
 * at all — is read directly off PrimaryComponentTick, so the policy under test is observed rather
 * than emulated. What runs is the real component code with real deltas.
 *
 * This is a deliberate departure from docs/solutions/ue-worldless-automation-test-patterns.md, on
 * the same grounds the built-in behavior tests take a world: the claim here involves registration,
 * BeginPlay, and real playback timing, so there is nothing left to test once they are stubbed out.
 *
 * WHAT IS STILL MISSED, AND MISSED SILENTLY. Watching every frame removes the shape where a whole
 * animation falls between two samples; it does not make sampling lossless. Two shapes survive at
 * TickInterval == 0, and neither emits a diagnostic — both emitters require the animation to be
 * OBSERVED at least once, and an animation this component never sees cannot be reported by anything
 * it runs:
 *
 *   - an animation whose entire life is shorter than one RENDERED frame (a 5 ms animation at 60 fps).
 *     There is no sample to take, because there is no frame in which it is the current flipbook;
 *   - a same-frame SetFlipbook A -> B -> A. The poll compares against the flipbook it last saw, so B
 *     never appears to have been current at all and none of B's anchors are traversed.
 *
 * Both are cured only by pushing instead of sampling (UPaper2DPlusFlipbookComponent's OnFlipbookChanged
 * fires per CALL, not per frame), which is exactly what the coarse-poll diagnostic tells the designer
 * to do. They are stated here for the same reason the two known heuristic gaps are: reverse playback
 * (a backward key-frame index is read as a loop wrap — see the LoopWrap test below and the heuristic
 * itself in HandleFrameChanged) and a sample coarser than a whole loop pass (anchors are owed once per
 * observed span, not once per elapsed pass). Naming a gap is not the same as accepting silence about a
 * dispatch that could have been made — these four are the boundary of what the poll can honestly claim.
 */
namespace Paper2DPlusCueDetectionTest
{
	/** A transient game world that has actually begun play, so components reach BeginPlay. */
	struct FCueDetect_ScopedWorld
	{
		FCueDetect_ScopedWorld()
		{
			if (!GEngine)
			{
				return;
			}
			const FName WorldName =
				MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("P2DPCueDetectionWorld"));
			World = UWorld::CreateWorld(
				EWorldType::Game, /*bInformEngineOfWorld=*/false, WorldName, GetTransientPackage());
			if (!World)
			{
				return;
			}
			World->AddToRoot();
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);

			FURL Url;
			World->InitializeActorsForPlay(Url);
			World->BeginPlay();
			// This isolated world has no GameInstance/GameMode, so the final BeginPlay dispatch a
			// production world reaches through WorldSettings has to be completed explicitly (the same
			// step the appearance profiling harness performs).
			if (!World->HasBegunPlay())
			{
				if (AWorldSettings* WorldSettings = World->GetWorldSettings())
				{
					WorldSettings->NotifyBeginPlay();
				}
			}
		}

		~FCueDetect_ScopedWorld()
		{
			if (!World)
			{
				return;
			}
			UWorld* const Local = World;
			World = nullptr;
			Local->DestroyWorld(false);
			if (GEngine)
			{
				GEngine->DestroyWorldContext(Local);
			}
			Local->RemoveFromRoot();
		}

		FCueDetect_ScopedWorld(const FCueDetect_ScopedWorld&) = delete;
		FCueDetect_ScopedWorld& operator=(const FCueDetect_ScopedWorld&) = delete;

		bool IsUsable() const { return World != nullptr && World->HasBegunPlay(); }

		UWorld* World = nullptr;
	};

	/** Countable skip token — see Paper2DPlusTestSkip.h in the editor module for the rationale. */
	void CueDetect_MarkSkipped(FAutomationTestBase& Test, const FString& What, const FString& Why)
	{
		Test.AddInfo(FString::Printf(TEXT("[P2DP-SKIPPED] %s — %s"), *What, *Why));
	}

	/** A real flipbook: N single-run key frames at the requested rate, so playback timing is real. */
	UPaperFlipbook* CueDetect_MakeFlipbook(int32 FrameCount, float FramesPerSecond)
	{
		UPaperFlipbook* Flipbook = NewObject<UPaperFlipbook>(GetTransientPackage());
		FScopedFlipbookMutator Mutator(Flipbook);
		Mutator.FramesPerSecond = FramesPerSecond;
		for (int32 Index = 0; Index < FrameCount; ++Index)
		{
			FPaperFlipbookKeyFrame KeyFrame;
			KeyFrame.FrameRun = 1;
			Mutator.KeyFrames.Add(KeyFrame);
		}
		return Flipbook;
	}

	void CueDetect_AddAnimation(
		UPaper2DPlusCharacterProfileAsset& Profile,
		const TCHAR* Name,
		UPaperFlipbook* Flipbook,
		const TArray<UPaper2DPlusCueBase*>& Cues)
	{
		FFlipbookProfileEntry Entry;
		Entry.Identity.FlipbookName = Name;
		Entry.Identity.Flipbook = Flipbook;
		for (UPaper2DPlusCueBase* Cue : Cues)
		{
			Entry.FrameEventData.FrameCues.Add(Cue);
		}
		Profile.Flipbooks.Add(MoveTemp(Entry));
	}

	/**
	 * A RuntimeCustomizable wardrobe asset with one Layer that contributes gameplay to exactly one
	 * animation. Pass a null Cue for a Layer that adds no cues at all — that is the control that keeps
	 * "a digest is live" from being mistaken for "this actor can dispatch a Layer cue".
	 */
	UPaper2DPlusCharacterLayerAsset* CueDetect_MakeWardrobeLayerAsset(
		const TCHAR* GameplayAnimationName,
		UPaper2DPlusCueBase* Cue,
		FPaper2DPlusAppearanceDescriptor& OutDescriptor)
	{
		UPaper2DPlusCharacterLayerAsset* Asset = NewObject<UPaper2DPlusCharacterLayerAsset>();
		Asset->UsageMode = ECharacterLayerUsageMode::RuntimeCustomizable;

		FCharacterLayer Layer;
		Layer.LayerName = TEXT("Wardrobe");

		FCharacterLayerAuthoredAnimationData Gameplay;
		// Name binding rather than a soft flipbook path: these flipbooks live in the transient package,
		// and the composer's name fallback is the binding a migrated row actually uses.
		Gameplay.LegacyAnimationName = GameplayAnimationName;
		if (Cue)
		{
			Gameplay.FrameCues.Add(Cue);
		}
		Layer.CookedGameplayAnimations.Add(MoveTemp(Gameplay));
		Asset->Layers.Add(MoveTemp(Layer));

		OutDescriptor = FPaper2DPlusAppearanceDescriptor();
		OutDescriptor.DeliveryMode = ECharacterLayerUsageMode::RuntimeCustomizable;
		OutDescriptor.ActiveLayerIds = { Asset->Layers[0].LayerId };
		return Asset;
	}

	struct FCueDetect_Rig
	{
		TObjectPtr<AActor> Actor = nullptr;
		TObjectPtr<UPaperFlipbookComponent> FlipbookComponent = nullptr;
		TObjectPtr<UPaper2DPlusCharacterProfileComponent> ProfileComponent = nullptr;
		TObjectPtr<UPaper2DPlusCharacterProfileAsset> Profile = nullptr;
		TObjectPtr<UPaper2DPlusFrameCueRecorder> Recorder = nullptr;

		/** Time banked since the detection watch last sampled (the interval scheduler's state). */
		float PendingPollSeconds = 0.0f;

		/** An interval tick function runs once when first queued; only then does the interval apply. */
		bool bPollPrimed = false;
	};

	/**
	 * One character in the world, wired the way a project wires it.
	 *
	 * The components are REGISTERED, not merely constructed, so the engine routes BeginPlay and
	 * builds the real tick functions — the profile component's own UpdateTickState picks its poll
	 * rate onto one of them, which is the whole point of these tests.
	 */
	FCueDetect_Rig CueDetect_MakeRig(
		UWorld& World,
		UPaper2DPlusCharacterProfileAsset* Profile,
		UPaperFlipbook* StartFlipbook,
		bool bEventDrivenComponent,
		bool bLooping,
		bool bLegacySlowPollPolicy = false)
	{
		FCueDetect_Rig Rig;
		Rig.Profile = Profile;
		// AlwaysSpawn: these actors carry no collision and several rigs share one world, so the spawn
		// must never be refused for placement reasons.
		FActorSpawnParameters SpawnParameters;
		SpawnParameters.SpawnCollisionHandlingOverride =
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Rig.Actor = World.SpawnActor<AActor>(
			AActor::StaticClass(), FTransform::Identity, SpawnParameters);

		Rig.FlipbookComponent = bEventDrivenComponent
			? NewObject<UPaperFlipbookComponent>(
				Rig.Actor.Get(), UPaper2DPlusFlipbookComponent::StaticClass())
			: NewObject<UPaperFlipbookComponent>(Rig.Actor.Get());
		Rig.Actor->SetRootComponent(Rig.FlipbookComponent.Get());
		Rig.FlipbookComponent->SetLooping(bLooping);
		Rig.FlipbookComponent->SetFlipbook(StartFlipbook);
		Rig.FlipbookComponent->RegisterComponent();

		Rig.Recorder = NewObject<UPaper2DPlusFrameCueRecorder>(Rig.Actor.Get());
		Rig.ProfileComponent = NewObject<UPaper2DPlusCharacterProfileComponent>(Rig.Actor.Get());
		Rig.ProfileComponent->CharacterProfile = Profile;
		Rig.ProfileComponent->FlipbookComponent = Rig.FlipbookComponent;
		Rig.ProfileComponent->OnFrameCue.AddDynamic(
			Rig.Recorder.Get(), &UPaper2DPlusFrameCueRecorder::OnCue);
		if (bLegacySlowPollPolicy)
		{
			Rig.ProfileComponent->SetLegacySlowPollPolicyForTests(true);
		}
		Rig.ProfileComponent->RegisterComponent();
		return Rig;
	}

	/**
	 * One frame of the game loop for this rig (see the file header for why the loop is ours).
	 *
	 * Watch first, sprite second — production ordering. The watch is invoked only when its own
	 * TickInterval says a sample is due, and receives the real gap since its previous sample, so the
	 * chosen rate is what actually decides which animations this component ever gets to see.
	 */
	/** Run one component tick through the engine's own entry point, not through TickComponent. */
	void CueDetect_ExecuteComponentTick(FActorComponentTickFunction& TickFunction, float DeltaSeconds)
	{
		TickFunction.ExecuteTick(
			DeltaSeconds, LEVELTICK_All, ENamedThreads::GameThread, FGraphEventRef());
	}

	void CueDetect_AdvanceOneFrame(FCueDetect_Rig& Rig, float DeltaSeconds)
	{
		UPaper2DPlusCharacterProfileComponent* ProfileComponent = Rig.ProfileComponent.Get();
		if (ProfileComponent && ProfileComponent->PrimaryComponentTick.IsTickFunctionEnabled())
		{
			Rig.PendingPollSeconds += DeltaSeconds;
			const float Interval = ProfileComponent->PrimaryComponentTick.TickInterval;
			if (!Rig.bPollPrimed || Rig.PendingPollSeconds >= Interval)
			{
				Rig.bPollPrimed = true;
				const float PollDelta = Rig.PendingPollSeconds;
				Rig.PendingPollSeconds = 0.0f;
				CueDetect_ExecuteComponentTick(ProfileComponent->PrimaryComponentTick, PollDelta);
			}
		}

		if (UPaperFlipbookComponent* FlipbookComponent = Rig.FlipbookComponent.Get())
		{
			CueDetect_ExecuteComponentTick(FlipbookComponent->PrimaryComponentTick, DeltaSeconds);
		}
	}

	void CueDetect_AdvanceFrames(FCueDetect_Rig& Rig, int32 Frames, float DeltaSeconds)
	{
		for (int32 Frame = 0; Frame < Frames; ++Frame)
		{
			CueDetect_AdvanceOneFrame(Rig, DeltaSeconds);
		}
	}

	int32 CueDetect_CountPhase(
		const UPaper2DPlusFrameCueRecorder& Recorder,
		const UPaper2DPlusCueBase* Cue,
		EPaper2DPlusFrameCuePhase Phase)
	{
		int32 Count = 0;
		for (int32 Index = 0; Index < Recorder.Contexts.Num(); ++Index)
		{
			if (Recorder.Cues.IsValidIndex(Index)
				&& Recorder.Cues[Index].Get() == Cue
				&& Recorder.Contexts[Index].Phase == Phase)
			{
				++Count;
			}
		}
		return Count;
	}

	int32 CueDetect_FindPhaseIndex(
		const UPaper2DPlusFrameCueRecorder& Recorder,
		const UPaper2DPlusCueBase* Cue,
		EPaper2DPlusFrameCuePhase Phase,
		int32 CurrentFrame = INDEX_NONE)
	{
		for (int32 Index = 0; Index < Recorder.Contexts.Num(); ++Index)
		{
			if (Recorder.Cues.IsValidIndex(Index)
				&& Recorder.Cues[Index].Get() == Cue
				&& Recorder.Contexts[Index].Phase == Phase
				&& (CurrentFrame == INDEX_NONE
					|| Recorder.Contexts[Index].CurrentFrame == CurrentFrame))
			{
				return Index;
			}
		}
		return INDEX_NONE;
	}

	/**
	 * Everything a detection failure needs explained: which flipbook the sprite is on, where playback
	 * actually is, which key frame that resolves to, and whether the watch is running at all. These
	 * tests assert about a policy that lives between two components, so a bare count explains nothing.
	 */
	FString CueDetect_DescribePlayback(const FCueDetect_Rig& Rig)
	{
		UPaperFlipbookComponent* FlipbookComponent = Rig.FlipbookComponent.Get();
		const UPaperFlipbook* Flipbook = FlipbookComponent ? FlipbookComponent->GetFlipbook() : nullptr;
		const float Position = FlipbookComponent ? FlipbookComponent->GetPlaybackPosition() : -1.0f;
		return FString::Printf(
			TEXT("polls=%u flipbook=%s length=%.4f position=%.4f keyFrame=%d playing=%d tickEnabled=%d interval=%.4f"),
			Rig.ProfileComponent->GetDetectionPollCountForTests(),
			*GetNameSafe(Flipbook),
			Flipbook ? Flipbook->GetTotalDuration() : -1.0f,
			Position,
			Flipbook ? Flipbook->GetKeyFrameIndexAtTime(Position) : -2,
			FlipbookComponent ? (FlipbookComponent->IsPlaying() ? 1 : 0) : -1,
			Rig.ProfileComponent->PrimaryComponentTick.IsTickFunctionEnabled() ? 1 : 0,
			Rig.ProfileComponent->PrimaryComponentTick.TickInterval);
	}

	/**
	 * Two idle frames, the attack for exactly its own life, then idle again — at 10 ms per frame.
	 *
	 * A 20 Hz watch primed at frame 1 samples again only at 50 ms, so the 20 ms window in which the
	 * attack is the current animation contains no sample at all. That is not a lucky phase: it is the
	 * interval scheduler's definition, and it is what makes this repro deterministic.
	 */
	void CueDetect_PlayShortAnimationBetweenPolls(
		FCueDetect_Rig& Rig,
		UPaperFlipbook* Attack,
		UPaperFlipbook* Idle)
	{
		CueDetect_AdvanceFrames(Rig, 2, 0.010f);
		Rig.FlipbookComponent->SetFlipbook(Attack);
		CueDetect_AdvanceFrames(Rig, 2, 0.010f);
		Rig.FlipbookComponent->SetFlipbook(Idle);
		CueDetect_AdvanceFrames(Rig, 20, 0.010f);
	}

	/**
	 * The same coarse watch, but the attack is LEFT UP: two idle frames to prime the 20 Hz sampler,
	 * then the attack stays the current animation until a poll finally lands on it.
	 *
	 * This is the diagnostic's precondition rather than the defect's: an animation the component never
	 * observes even once cannot be reported by anything, so the reportable shape is the one that is
	 * seen LATE. The rate has to be genuinely coarse for that to mean anything — see each caller for
	 * how it gets there.
	 */
	void CueDetect_ObserveAnimationAfterCoarsePoll(FCueDetect_Rig& Rig, UPaperFlipbook* Attack)
	{
		CueDetect_AdvanceFrames(Rig, 2, 0.010f);
		Rig.FlipbookComponent->SetFlipbook(Attack);
		CueDetect_AdvanceFrames(Rig, 20, 0.010f);
	}
}

/**
 * THE DEFECT, kept runnable.
 *
 * Under the pre-fix policy the watch rate is chosen from what the CURRENT animation dispatches, so
 * an actor sitting on a cue-free idle polls at 20 Hz. A cue-carrying animation that begins and ends
 * inside one of those 50 ms windows is never observed — its cues are not late, they never happen.
 * This is the reported no-fire, and it stays a test rather than folklore.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCueDetectionLegacyPollMissesTest,
	"Paper2DPlus.FrameCues.Detection.LegacySlowPollMissesAShortAnimation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCueDetectionLegacyPollMissesTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusCueDetectionTest;

	FCueDetect_ScopedWorld ScopedWorld;
	if (!ScopedWorld.IsUsable())
	{
		CueDetect_MarkSkipped(*this, TEXT("Legacy slow-poll miss"),
			TEXT("a transient game world could not begin play in this session."));
		return true;
	}

	UPaperFlipbook* Idle = CueDetect_MakeFlipbook(4, 10.0f);
	UPaperFlipbook* Attack = CueDetect_MakeFlipbook(2, 100.0f);

	UPaper2DPlusTestMomentCue* AttackCue = NewObject<UPaper2DPlusTestMomentCue>();
	AttackCue->TriggerFrame = 0;

	UPaper2DPlusCharacterProfileAsset* Profile = NewObject<UPaper2DPlusCharacterProfileAsset>();
	CueDetect_AddAnimation(*Profile, TEXT("Idle"), Idle, {});
	CueDetect_AddAnimation(*Profile, TEXT("Attack"), Attack, {AttackCue});

	FCueDetect_Rig Rig = CueDetect_MakeRig(
		*ScopedWorld.World, Profile, Idle,
		/*bEventDrivenComponent=*/false, /*bLooping=*/true, /*bLegacySlowPollPolicy=*/true);

	TestEqual(
		TEXT("The legacy policy watches a cue-carrying profile at 20 Hz while idle"),
		Rig.ProfileComponent->PrimaryComponentTick.TickInterval,
		0.05f);

	CueDetect_PlayShortAnimationBetweenPolls(Rig, Attack, Idle);
	AddInfo(TEXT("after the short animation: ") + CueDetect_DescribePlayback(Rig));

	// The poll DID run — this is not a disabled tick, it is a tick that sampled too slowly.
	TestTrue(
		TEXT("The 20 Hz watch was running throughout"),
		Rig.ProfileComponent->GetDetectionPollCountForTests() > 0);
	TestEqual(
		TEXT("A 20 ms animation between two 50 ms samples dispatches nothing at all"),
		CueDetect_CountPhase(*Rig.Recorder, AttackCue, EPaper2DPlusFrameCuePhase::Trigger),
		0);

	return true;
}

/**
 * The fix: the watch rate follows what the PROFILE can dispatch, so the same animation is caught.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCueDetectionShortAnimationTest,
	"Paper2DPlus.FrameCues.Detection.StockPollDispatchesAShortAnimation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCueDetectionShortAnimationTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusCueDetectionTest;

	FCueDetect_ScopedWorld ScopedWorld;
	if (!ScopedWorld.IsUsable())
	{
		CueDetect_MarkSkipped(*this, TEXT("Short-animation detection"),
			TEXT("a transient game world could not begin play in this session."));
		return true;
	}

	UPaperFlipbook* Idle = CueDetect_MakeFlipbook(4, 10.0f);
	UPaperFlipbook* Attack = CueDetect_MakeFlipbook(2, 100.0f);

	UPaper2DPlusTestMomentCue* AttackCue = NewObject<UPaper2DPlusTestMomentCue>();
	AttackCue->TriggerFrame = 0;

	UPaper2DPlusCharacterProfileAsset* Profile = NewObject<UPaper2DPlusCharacterProfileAsset>();
	CueDetect_AddAnimation(*Profile, TEXT("Idle"), Idle, {});
	CueDetect_AddAnimation(*Profile, TEXT("Attack"), Attack, {AttackCue});

	FCueDetect_Rig Rig = CueDetect_MakeRig(
		*ScopedWorld.World, Profile, Idle, /*bEventDrivenComponent=*/false, /*bLooping=*/true);

	TestTrue(TEXT("Detection tick is enabled on the compatibility path"),
		Rig.ProfileComponent->PrimaryComponentTick.IsTickFunctionEnabled());
	TestEqual(
		TEXT("An idle actor whose PROFILE carries cues is watched every frame, not at 20 Hz"),
		Rig.ProfileComponent->PrimaryComponentTick.TickInterval,
		0.0f);

	CueDetect_PlayShortAnimationBetweenPolls(Rig, Attack, Idle);
	AddInfo(TEXT("after the short animation: ") + CueDetect_DescribePlayback(Rig));

	const int32 Triggers =
		CueDetect_CountPhase(*Rig.Recorder, AttackCue, EPaper2DPlusFrameCuePhase::Trigger);
	TestEqual(
		TEXT("The short animation is observed and dispatches its frame-0 cue exactly once"),
		Triggers,
		1);

	// A cue-free profile keeps the cheap watch the 20 Hz policy was designed for.
	UPaper2DPlusCharacterProfileAsset* QuietProfile =
		NewObject<UPaper2DPlusCharacterProfileAsset>();
	CueDetect_AddAnimation(*QuietProfile, TEXT("Idle"), Idle, {});
	FCueDetect_Rig QuietRig = CueDetect_MakeRig(
		*ScopedWorld.World, QuietProfile, Idle, /*bEventDrivenComponent=*/false, /*bLooping=*/true);
	TestEqual(
		TEXT("A profile with no Frame Cues anywhere still watches at 20 Hz"),
		QuietRig.ProfileComponent->PrimaryComponentTick.TickInterval,
		0.05f);

	return true;
}

/**
 * A wardrobe Layer's cues count BEFORE the animation carrying them has ever been seen.
 *
 * The nastiest form of the same defect, one level up. A cue-free base profile plus a Layer whose cue
 * operations target a short animation X leaves nothing to observe: the composed set is built per
 * CURRENT animation, so a latch that flips when a composed cue appears cannot flip until X plays —
 * and on the 20 Hz watch X is precisely the animation that plays entirely between two samples. The
 * latch would be one OBSERVATION out of date exactly as the old rate was one ANIMATION out of date,
 * and neither diagnostic can fire, because both need the animation observed once. So the rate is
 * taken from the Layer ASSET at digest-push time, which knows about X before X has ever played.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCueDetectionLayerCueOnUnseenAnimationTest,
	"Paper2DPlus.FrameCues.Detection.LayerCuesOnACueFreeProfileWatchEveryFrame",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCueDetectionLayerCueOnUnseenAnimationTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusCueDetectionTest;

	FCueDetect_ScopedWorld ScopedWorld;
	if (!ScopedWorld.IsUsable())
	{
		CueDetect_MarkSkipped(*this, TEXT("Layer cue on an unseen animation"),
			TEXT("a transient game world could not begin play in this session."));
		return true;
	}

	UPaperFlipbook* Idle = CueDetect_MakeFlipbook(4, 10.0f);
	UPaperFlipbook* Attack = CueDetect_MakeFlipbook(2, 100.0f);

	// The base profile carries NO cues on any animation — every cue in this test belongs to the Layer.
	UPaper2DPlusCharacterProfileAsset* Profile = NewObject<UPaper2DPlusCharacterProfileAsset>();
	CueDetect_AddAnimation(*Profile, TEXT("Idle"), Idle, {});
	CueDetect_AddAnimation(*Profile, TEXT("Attack"), Attack, {});

	FCueDetect_Rig Rig = CueDetect_MakeRig(
		*ScopedWorld.World, Profile, Idle, /*bEventDrivenComponent=*/false, /*bLooping=*/true);

	TestEqual(
		TEXT("A cue-free base profile with nothing equipped keeps the cheap 20 Hz watch"),
		Rig.ProfileComponent->PrimaryComponentTick.TickInterval,
		0.05f);

	// Equip a Layer whose only cue sits on Attack — which is NOT the current animation, and which this
	// actor has never played. Nothing has been composed, so the composed-cue latch is still false.
	UPaper2DPlusTestMomentCue* LayerCue = NewObject<UPaper2DPlusTestMomentCue>();
	LayerCue->TriggerFrame = 0;
	FPaper2DPlusAppearanceDescriptor Descriptor;
	UPaper2DPlusCharacterLayerAsset* LayerAsset =
		CueDetect_MakeWardrobeLayerAsset(TEXT("Attack"), LayerCue, Descriptor);
	Rig.ProfileComponent->NotifyAppearanceCombatDirty(Descriptor, LayerAsset);

	TestEqual(
		TEXT("Equipping a Layer that adds cues to an animation never yet played switches the actor to an every-frame watch"),
		Rig.ProfileComponent->PrimaryComponentTick.TickInterval,
		0.0f);

	// And the payoff: the 20 ms animation that the 20 Hz watch would have skipped entirely is
	// observed, composed, and dispatched.
	CueDetect_PlayShortAnimationBetweenPolls(Rig, Attack, Idle);
	AddInfo(TEXT("after the short animation: ") + CueDetect_DescribePlayback(Rig));

	TestEqual(
		TEXT("The Layer's cue on the short animation fires exactly once"),
		CueDetect_CountPhase(*Rig.Recorder, LayerCue, EPaper2DPlusFrameCuePhase::Trigger),
		1);
	TestEqual(
		TEXT("Neither detection diagnostic fires when the cue is actually delivered"),
		Rig.ProfileComponent->GetDetectionDiagnosticsForTests().Num(),
		0);

	// The control: a live digest is not by itself a reason to watch every frame. An equipped Layer
	// that contributes gameplay but no cues leaves the cheap watch exactly where it was.
	UPaper2DPlusCharacterProfileAsset* QuietProfile =
		NewObject<UPaper2DPlusCharacterProfileAsset>();
	CueDetect_AddAnimation(*QuietProfile, TEXT("Idle"), Idle, {});
	CueDetect_AddAnimation(*QuietProfile, TEXT("Attack"), Attack, {});
	FCueDetect_Rig QuietRig = CueDetect_MakeRig(
		*ScopedWorld.World, QuietProfile, Idle, /*bEventDrivenComponent=*/false, /*bLooping=*/true);

	FPaper2DPlusAppearanceDescriptor QuietDescriptor;
	UPaper2DPlusCharacterLayerAsset* QuietLayerAsset =
		CueDetect_MakeWardrobeLayerAsset(TEXT("Attack"), /*Cue=*/nullptr, QuietDescriptor);
	QuietRig.ProfileComponent->NotifyAppearanceCombatDirty(QuietDescriptor, QuietLayerAsset);

	TestEqual(
		TEXT("A Layer that adds no cues anywhere still watches at 20 Hz"),
		QuietRig.ProfileComponent->PrimaryComponentTick.TickInterval,
		0.05f);

	return true;
}

/**
 * The event-driven contract: binding the delegates means the profile component never ticks.
 *
 * This is the property the poll-rate fix must not spend. A project on UPaper2DPlusFlipbookComponent
 * pays zero tick cost while idle AND still gets every cue, because detection is pushed to it.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCueDetectionEventDrivenZeroTickTest,
	"Paper2DPlus.FrameCues.Detection.EventDrivenPathStaysZeroTickWhenIdle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCueDetectionEventDrivenZeroTickTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusCueDetectionTest;

	FCueDetect_ScopedWorld ScopedWorld;
	if (!ScopedWorld.IsUsable())
	{
		CueDetect_MarkSkipped(*this, TEXT("Event-driven zero-tick"),
			TEXT("a transient game world could not begin play in this session."));
		return true;
	}

	UPaperFlipbook* Idle = CueDetect_MakeFlipbook(4, 10.0f);
	UPaperFlipbook* Attack = CueDetect_MakeFlipbook(4, 10.0f);

	UPaper2DPlusTestMomentCue* AttackCue = NewObject<UPaper2DPlusTestMomentCue>();
	AttackCue->TriggerFrame = 0;

	UPaper2DPlusCharacterProfileAsset* Profile = NewObject<UPaper2DPlusCharacterProfileAsset>();
	CueDetect_AddAnimation(*Profile, TEXT("Idle"), Idle, {});
	CueDetect_AddAnimation(*Profile, TEXT("Attack"), Attack, {AttackCue});

	FCueDetect_Rig Rig = CueDetect_MakeRig(
		*ScopedWorld.World, Profile, Idle, /*bEventDrivenComponent=*/true, /*bLooping=*/true);

	TestFalse(
		TEXT("The event-driven path leaves the profile component's tick disabled while idle"),
		Rig.ProfileComponent->PrimaryComponentTick.IsTickFunctionEnabled());

	CueDetect_AdvanceFrames(Rig, 20, 0.016f);
	TestFalse(
		TEXT("Advancing time does not switch the event-driven path back on"),
		Rig.ProfileComponent->PrimaryComponentTick.IsTickFunctionEnabled());
	TestEqual(
		TEXT("No detection poll ever ran on the event-driven path"),
		static_cast<int32>(Rig.ProfileComponent->GetDetectionPollCountForTests()),
		0);

	// And the cue still arrives — the zero-tick contract is only worth keeping if it dispatches.
	Rig.FlipbookComponent->SetFlipbook(Attack);
	CueDetect_AdvanceFrames(Rig, 4, 0.016f);
	TestEqual(
		TEXT("The pushed flipbook change dispatches the attack's frame-0 cue"),
		CueDetect_CountPhase(*Rig.Recorder, AttackCue, EPaper2DPlusFrameCuePhase::Trigger),
		1);
	TestEqual(
		TEXT("Dispatching through the pushed path still costs no detection poll"),
		static_cast<int32>(Rig.ProfileComponent->GetDetectionPollCountForTests()),
		0);

	return true;
}

/**
 * Stop() is observable without a priming tick on both playback-source paths.
 *
 * The range begins during the profile component's registered BeginPlay warm at frame 0. Stopping
 * immediately afterward is the adversarial boundary: neither path gets an observation tick that can
 * quietly establish "was playing" first. One real game frame must still pair the Begin exactly once.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCueDetectionCustomAndStockDirectStopTest,
	"Paper2DPlus.FrameCues.Detection.CustomAndStockDirectStop",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCueDetectionCustomAndStockDirectStopTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusCueDetectionTest;

	FCueDetect_ScopedWorld ScopedWorld;
	if (!ScopedWorld.IsUsable())
	{
		CueDetect_MarkSkipped(*this, TEXT("Custom and stock direct stop"),
			TEXT("a transient game world could not begin play in this session."));
		return true;
	}

	for (const bool bEventDrivenComponent : {true, false})
	{
		Paper2DPlusBehaviorTestLog::Reset();
		const FString Path = bEventDrivenComponent ? TEXT("custom") : TEXT("stock");

		UPaperFlipbook* Attack = CueDetect_MakeFlipbook(8, 10.0f);
		UPaper2DPlusTestBehaviorRangeCue* Range =
			NewObject<UPaper2DPlusTestBehaviorRangeCue>();
		Range->StartFrame = 0;
		Range->FrameCount = 8;

		UPaper2DPlusCharacterProfileAsset* Profile =
			NewObject<UPaper2DPlusCharacterProfileAsset>();
		CueDetect_AddAnimation(*Profile, TEXT("Attack"), Attack, {Range});

		FCueDetect_Rig Rig = CueDetect_MakeRig(
			*ScopedWorld.World,
			Profile,
			Attack,
			bEventDrivenComponent,
			/*bLooping=*/false);

		TestEqual(
			*FString::Printf(TEXT("%s path begins the frame-0 range during BeginPlay"), *Path),
			CueDetect_CountPhase(*Rig.Recorder, Range, EPaper2DPlusFrameCuePhase::Begin),
			1);
		TestTrue(
			*FString::Printf(TEXT("%s path leaves the frame-0 range active before Stop"), *Path),
			Rig.ProfileComponent->IsRangeCueActiveForTests(Range));
		TestEqual(
			*FString::Printf(TEXT("%s path has not ended before Stop is observed"), *Path),
			CueDetect_CountPhase(*Rig.Recorder, Range, EPaper2DPlusFrameCuePhase::End),
			0);

		// Deliberately no CueDetect_AdvanceOneFrame before Stop: this pins direct-stop observation
		// against the state established by BeginPlay itself.
		Rig.FlipbookComponent->Stop();
		CueDetect_AdvanceOneFrame(Rig, 0.016f);
		AddInfo(FString::Printf(
			TEXT("%s path after direct stop: %s"), *Path, *CueDetect_DescribePlayback(Rig)));

		TestEqual(
			*FString::Printf(TEXT("%s path pairs the range on the first relevant game frame"), *Path),
			CueDetect_CountPhase(*Rig.Recorder, Range, EPaper2DPlusFrameCuePhase::End),
			1);
		TestFalse(
			*FString::Printf(TEXT("%s path leaves no active range after direct Stop"), *Path),
			Rig.ProfileComponent->IsRangeCueActiveForTests(Range));

		const int32 EndIndex = CueDetect_FindPhaseIndex(
			*Rig.Recorder, Range, EPaper2DPlusFrameCuePhase::End);
		TestTrue(
			*FString::Printf(TEXT("%s path records the direct-stop End context"), *Path),
			EndIndex != INDEX_NONE);
		if (Rig.Recorder->Contexts.IsValidIndex(EndIndex))
		{
			TestEqual(
				*FString::Printf(TEXT("%s path labels direct Stop as PlaybackStopped"), *Path),
				Rig.Recorder->Contexts[EndIndex].EndReason,
				EPaper2DPlusFrameCueEndReason::PlaybackStopped);
		}

		CueDetect_AdvanceOneFrame(Rig, 0.016f);
		TestEqual(
			*FString::Printf(TEXT("%s path does not duplicate End on a later frame"), *Path),
			CueDetect_CountPhase(*Rig.Recorder, Range, EPaper2DPlusFrameCuePhase::End),
			1);
		if (bEventDrivenComponent)
		{
			TestEqual(
				TEXT("The custom direct-stop path remains purely event-driven"),
				static_cast<int32>(Rig.ProfileComponent->GetDetectionPollCountForTests()),
				0);
		}
	}

	return true;
}

/**
 * Natural completion delivers the definitive final frame before it closes active ranges.
 *
 * Custom playback reports completion from its post-Super terminal signal; stock playback latches
 * OnFinishedPlaying and drains it on the next ordered poll. Both must expose the same observable
 * sequence: frame-1 Trigger/Update, then one Completed End carrying frame 1.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCueDetectionNaturalCompletionFollowsFinalFrameTest,
	"Paper2DPlus.FrameCues.Detection.NaturalCompletionFollowsFinalFrame",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCueDetectionNaturalCompletionFollowsFinalFrameTest::RunTest(
	const FString& Parameters)
{
	using namespace Paper2DPlusCueDetectionTest;

	FCueDetect_ScopedWorld ScopedWorld;
	if (!ScopedWorld.IsUsable())
	{
		CueDetect_MarkSkipped(*this, TEXT("Natural completion final-frame order"),
			TEXT("a transient game world could not begin play in this session."));
		return true;
	}

	for (const bool bEventDrivenComponent : {true, false})
	{
		Paper2DPlusBehaviorTestLog::Reset();
		const FString Path = bEventDrivenComponent ? TEXT("custom") : TEXT("stock");

		UPaperFlipbook* Attack = CueDetect_MakeFlipbook(2, 10.0f);
		UPaper2DPlusTestBehaviorRangeCue* Range =
			NewObject<UPaper2DPlusTestBehaviorRangeCue>();
		Range->StartFrame = 0;
		Range->FrameCount = 2;
		Range->bEmitUpdates = true;
		UPaper2DPlusTestBehaviorMomentCue* FinalMoment =
			NewObject<UPaper2DPlusTestBehaviorMomentCue>();
		FinalMoment->TriggerFrame = 1;

		UPaper2DPlusCharacterProfileAsset* Profile =
			NewObject<UPaper2DPlusCharacterProfileAsset>();
		CueDetect_AddAnimation(*Profile, TEXT("Attack"), Attack, {Range, FinalMoment});

		FCueDetect_Rig Rig = CueDetect_MakeRig(
			*ScopedWorld.World,
			Profile,
			Attack,
			bEventDrivenComponent,
			/*bLooping=*/false);

		// Frame 1 is reached on the first step and natural completion on the second. Stock playback
		// needs one final poll after OnFinishedPlaying to drain its latched Completed terminal.
		CueDetect_AdvanceOneFrame(Rig, 0.110f);
		CueDetect_AdvanceOneFrame(Rig, 0.110f);
		if (!bEventDrivenComponent)
		{
			CueDetect_AdvanceOneFrame(Rig, 0.010f);
		}
		AddInfo(FString::Printf(
			TEXT("%s path after natural completion: %s"),
			*Path,
			*CueDetect_DescribePlayback(Rig)));

		const int32 TriggerIndex = CueDetect_FindPhaseIndex(
			*Rig.Recorder, FinalMoment, EPaper2DPlusFrameCuePhase::Trigger, 1);
		const int32 UpdateIndex = CueDetect_FindPhaseIndex(
			*Rig.Recorder, Range, EPaper2DPlusFrameCuePhase::Update, 1);
		const int32 EndIndex = CueDetect_FindPhaseIndex(
			*Rig.Recorder, Range, EPaper2DPlusFrameCuePhase::End);

		TestTrue(
			*FString::Printf(TEXT("%s path dispatches the final-frame Trigger"), *Path),
			TriggerIndex != INDEX_NONE);
		TestEqual(
			*FString::Printf(TEXT("%s path dispatches the final-frame Trigger exactly once"), *Path),
			CueDetect_CountPhase(
				*Rig.Recorder, FinalMoment, EPaper2DPlusFrameCuePhase::Trigger),
			1);
		TestTrue(
			*FString::Printf(TEXT("%s path dispatches the final-frame range Update"), *Path),
			UpdateIndex != INDEX_NONE);
		TestEqual(
			*FString::Printf(TEXT("%s path Updates once on each in-range frame"), *Path),
			CueDetect_CountPhase(*Rig.Recorder, Range, EPaper2DPlusFrameCuePhase::Update),
			2);
		TestEqual(
			*FString::Printf(TEXT("%s path dispatches exactly one range End"), *Path),
			CueDetect_CountPhase(*Rig.Recorder, Range, EPaper2DPlusFrameCuePhase::End),
			1);
		TestTrue(
			*FString::Printf(TEXT("%s path places final-frame Trigger before End"), *Path),
			TriggerIndex != INDEX_NONE && EndIndex != INDEX_NONE && TriggerIndex < EndIndex);
		TestTrue(
			*FString::Printf(TEXT("%s path places final-frame Update before End"), *Path),
			UpdateIndex != INDEX_NONE && EndIndex != INDEX_NONE && UpdateIndex < EndIndex);
		TestEqual(
			*FString::Printf(TEXT("%s path makes the terminal End the last notification"), *Path),
			EndIndex,
			Rig.Recorder->Contexts.Num() - 1);

		if (Rig.Recorder->Contexts.IsValidIndex(EndIndex))
		{
			const FPaper2DPlusFrameCueContext& End = Rig.Recorder->Contexts[EndIndex];
			TestEqual(
				*FString::Printf(TEXT("%s path labels natural completion as Completed"), *Path),
				End.EndReason,
				EPaper2DPlusFrameCueEndReason::Completed);
			TestTrue(
				*FString::Printf(TEXT("%s path never relabels natural completion as PlaybackStopped"), *Path),
				End.EndReason != EPaper2DPlusFrameCueEndReason::PlaybackStopped);
			TestEqual(
				*FString::Printf(TEXT("%s path retains final frame 1 on End"), *Path),
				End.CurrentFrame,
				1);
		}

		for (int32 Index = 0; Index < Rig.Recorder->BehaviorLogSizesAtBroadcast.Num(); ++Index)
		{
			TestEqual(
				*FString::Printf(
					TEXT("%s notification %d observes behavior before its listener"),
					*Path,
					Index),
				Rig.Recorder->BehaviorLogSizesAtBroadcast[Index],
				Index + 1);
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCueDetectionStockFinishRestartTest,
	"Paper2DPlus.FrameCues.Detection.StockNaturalFinishSurvivesImmediateRestart",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCueDetectionStockFinishRestartTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusCueDetectionTest;

	FCueDetect_ScopedWorld ScopedWorld;
	if (!ScopedWorld.IsUsable())
	{
		CueDetect_MarkSkipped(*this, TEXT("Stock natural-finish restart"),
			TEXT("a transient game world could not begin play in this session."));
		return true;
	}

	UPaperFlipbook* Attack = CueDetect_MakeFlipbook(2, 10.0f);
	UPaper2DPlusTestBehaviorRangeCue* Range =
		NewObject<UPaper2DPlusTestBehaviorRangeCue>();
	Range->StartFrame = 0;
	Range->FrameCount = 2;
	UPaper2DPlusTestBehaviorMomentCue* FinalMoment =
		NewObject<UPaper2DPlusTestBehaviorMomentCue>();
	FinalMoment->TriggerFrame = 1;

	UPaper2DPlusCharacterProfileAsset* Profile =
		NewObject<UPaper2DPlusCharacterProfileAsset>();
	CueDetect_AddAnimation(*Profile, TEXT("Attack"), Attack, {Range, FinalMoment});
	FCueDetect_Rig Rig = CueDetect_MakeRig(
		*ScopedWorld.World,
		Profile,
		Attack,
		/*bEventDrivenComponent=*/false,
		/*bLooping=*/false);

	UPaper2DPlusPlaybackFinishMutationReceiver* Restart =
		NewObject<UPaper2DPlusPlaybackFinishMutationReceiver>(Rig.Actor.Get());
	Restart->PlaybackComponent = Rig.FlipbookComponent;
	Restart->bRestartFromStart = true;
	Rig.FlipbookComponent->OnFinishedPlaying.AddDynamic(
		Restart, &UPaper2DPlusPlaybackFinishMutationReceiver::OnFinishedPlaying);

	const int64 OutgoingGeneration = Rig.ProfileComponent->GetFrameCuePlaybackGeneration();
	CueDetect_AdvanceOneFrame(Rig, 0.110f);
	CueDetect_AdvanceOneFrame(Rig, 0.110f);

	TestEqual(TEXT("The later finish listener restarts playback exactly once"),
		Restart->InvocationCount, 1);
	TestEqual(TEXT("Natural completion closes the outgoing range before the restart"),
		CueDetect_CountPhase(*Rig.Recorder, Range, EPaper2DPlusFrameCuePhase::End), 1);
	const int32 EndIndex = CueDetect_FindPhaseIndex(
		*Rig.Recorder, Range, EPaper2DPlusFrameCuePhase::End);
	if (Rig.Recorder->Contexts.IsValidIndex(EndIndex))
	{
		TestEqual(TEXT("The outgoing terminal remains Completed"),
			Rig.Recorder->Contexts[EndIndex].EndReason,
			EPaper2DPlusFrameCueEndReason::Completed);
		TestEqual(TEXT("The outgoing terminal retains the final source frame"),
			Rig.Recorder->Contexts[EndIndex].CurrentFrame, 1);
	}
	TestEqual(TEXT("The final-frame moment fires before the restart"),
		CueDetect_CountPhase(*Rig.Recorder, FinalMoment, EPaper2DPlusFrameCuePhase::Trigger), 1);

	// The next stock observation sees that the later listener left the source playing and opens a
	// distinct same-flipbook generation instead of discarding the latched completion.
	CueDetect_AdvanceOneFrame(Rig, 0.010f);
	const int64 RestartGeneration = Rig.ProfileComponent->GetFrameCuePlaybackGeneration();
	TestTrue(TEXT("The immediate restart receives a new generation"),
		RestartGeneration > 0 && RestartGeneration != OutgoingGeneration);
	TestEqual(TEXT("The restarted generation force-evaluates frame 0"),
		CueDetect_CountPhase(*Rig.Recorder, Range, EPaper2DPlusFrameCuePhase::Begin), 2);
	TestTrue(TEXT("The restarted generation owns a fresh active range"),
		Rig.ProfileComponent->IsRangeCueActiveForTests(Range));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCueDetectionDestroyedSourceReplacementTest,
	"Paper2DPlus.FrameCues.Detection.DestroyedCustomSourceEndsBeforeSameFlipbookReplacement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCueDetectionDestroyedSourceReplacementTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusCueDetectionTest;

	FCueDetect_ScopedWorld ScopedWorld;
	if (!ScopedWorld.IsUsable())
	{
		CueDetect_MarkSkipped(*this, TEXT("Destroyed custom source replacement"),
			TEXT("a transient game world could not begin play in this session."));
		return true;
	}

	UPaperFlipbook* Attack = CueDetect_MakeFlipbook(8, 10.0f);
	UPaper2DPlusTestBehaviorRangeCue* Range =
		NewObject<UPaper2DPlusTestBehaviorRangeCue>();
	Range->StartFrame = 0;
	Range->FrameCount = 8;
	UPaper2DPlusCharacterProfileAsset* Profile =
		NewObject<UPaper2DPlusCharacterProfileAsset>();
	CueDetect_AddAnimation(*Profile, TEXT("Attack"), Attack, {Range});
	FCueDetect_Rig Rig = CueDetect_MakeRig(
		*ScopedWorld.World,
		Profile,
		Attack,
		/*bEventDrivenComponent=*/true,
		/*bLooping=*/false);

	const int64 OutgoingGeneration = Rig.ProfileComponent->GetFrameCuePlaybackGeneration();
	UPaper2DPlusFlipbookComponent* OldSource =
		CastChecked<UPaper2DPlusFlipbookComponent>(Rig.FlipbookComponent.Get());
	OldSource->DestroyComponent();

	TestEqual(TEXT("Destroying the source closes the outgoing range once"),
		CueDetect_CountPhase(*Rig.Recorder, Range, EPaper2DPlusFrameCuePhase::End), 1);
	const int32 EndIndex = CueDetect_FindPhaseIndex(
		*Rig.Recorder, Range, EPaper2DPlusFrameCuePhase::End);
	if (Rig.Recorder->Contexts.IsValidIndex(EndIndex))
	{
		TestEqual(TEXT("A destroyed source reports Source Removed"),
			Rig.Recorder->Contexts[EndIndex].EndReason,
			EPaper2DPlusFrameCueEndReason::SourceRemoved);
		TestTrue(TEXT("The destroyed-source End retains the outgoing component"),
			Rig.Recorder->Contexts[EndIndex].PlaybackComponent == OldSource);
	}
	TestFalse(TEXT("Destroying the source leaves no outgoing active range"),
		Rig.ProfileComponent->IsRangeCueActiveForTests(Range));

	UPaper2DPlusFlipbookComponent* Replacement =
		NewObject<UPaper2DPlusFlipbookComponent>(Rig.Actor.Get());
	Replacement->SetLooping(false);
	Replacement->SetFlipbook(Attack);
	Rig.Actor->SetRootComponent(Replacement);
	Replacement->RegisterComponent();
	Rig.ProfileComponent->SetFrameCuePlaybackSource(Replacement);

	const int64 ReplacementGeneration = Rig.ProfileComponent->GetFrameCuePlaybackGeneration();
	TestTrue(TEXT("The same-flipbook replacement receives a new generation"),
		ReplacementGeneration > 0 && ReplacementGeneration != OutgoingGeneration);
	TestEqual(TEXT("The replacement establishes a fresh range lifecycle"),
		CueDetect_CountPhase(*Rig.Recorder, Range, EPaper2DPlusFrameCuePhase::Begin), 2);
	TestTrue(TEXT("The replacement range is active"),
		Rig.ProfileComponent->IsRangeCueActiveForTests(Range));
	TestTrue(TEXT("The replacement context names the replacement component"),
		Rig.Recorder->Contexts.Last().PlaybackComponent == Replacement);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCueDetectionQuiescentDirectAssignmentTest,
	"Paper2DPlus.FrameCues.Detection.QuiescentCustomDirectAssignmentRepairsAtomically",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCueDetectionQuiescentDirectAssignmentTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusCueDetectionTest;

	FCueDetect_ScopedWorld ScopedWorld;
	if (!ScopedWorld.IsUsable())
	{
		CueDetect_MarkSkipped(*this, TEXT("Quiescent custom direct assignment"),
			TEXT("a transient game world could not begin play in this session."));
		return true;
	}

	UPaperFlipbook* Attack = CueDetect_MakeFlipbook(8, 10.0f);
	UPaper2DPlusTestBehaviorRangeCue* Range =
		NewObject<UPaper2DPlusTestBehaviorRangeCue>();
	Range->StartFrame = 0;
	Range->FrameCount = 8;
	UPaper2DPlusCharacterProfileAsset* Profile =
		NewObject<UPaper2DPlusCharacterProfileAsset>();
	CueDetect_AddAnimation(*Profile, TEXT("Attack"), Attack, {Range});
	FCueDetect_Rig Rig = CueDetect_MakeRig(
		*ScopedWorld.World,
		Profile,
		Attack,
		/*bEventDrivenComponent=*/true,
		/*bLooping=*/false);

	UPaper2DPlusFlipbookComponent* OldSource =
		CastChecked<UPaper2DPlusFlipbookComponent>(Rig.FlipbookComponent.Get());
	UPaper2DPlusFlipbookComponent* Replacement =
		NewObject<UPaper2DPlusFlipbookComponent>(Rig.Actor.Get());
	Replacement->SetLooping(false);
	Replacement->SetFlipbook(Attack);
	Replacement->RegisterComponent();
	const int64 OutgoingGeneration = Rig.ProfileComponent->GetFrameCuePlaybackGeneration();

	// Intentional legacy C++ write: Blueprint writes route through the setter. The old custom
	// source's sender-aware observation must detect this even though the Profile tick remains off.
	Rig.ProfileComponent->FlipbookComponent = Replacement;
	CueDetect_ExecuteComponentTick(OldSource->PrimaryComponentTick, 0.010f);

	const int64 ReplacementGeneration = Rig.ProfileComponent->GetFrameCuePlaybackGeneration();
	TestTrue(TEXT("The quiescent mismatch is repaired into a new generation"),
		ReplacementGeneration > 0 && ReplacementGeneration != OutgoingGeneration);
	TestEqual(TEXT("The repair closes the outgoing range once"),
		CueDetect_CountPhase(*Rig.Recorder, Range, EPaper2DPlusFrameCuePhase::End), 1);
	const int32 EndIndex = CueDetect_FindPhaseIndex(
		*Rig.Recorder, Range, EPaper2DPlusFrameCuePhase::End);
	if (Rig.Recorder->Contexts.IsValidIndex(EndIndex))
	{
		TestEqual(TEXT("The legacy repair uses Source Removed"),
			Rig.Recorder->Contexts[EndIndex].EndReason,
			EPaper2DPlusFrameCueEndReason::SourceRemoved);
	}
	TestEqual(TEXT("The repaired source establishes a fresh frame-0 Begin"),
		CueDetect_CountPhase(*Rig.Recorder, Range, EPaper2DPlusFrameCuePhase::Begin), 2);
	TestTrue(TEXT("The replacement owns the active range after repair"),
		Rig.ProfileComponent->IsRangeCueActiveForTests(Range));
	TestTrue(TEXT("The repaired public source remains the replacement"),
		Rig.ProfileComponent->GetResolvedFlipbookComponent() == Replacement);
	TestTrue(TEXT("The replacement Begin carries the replacement component"),
		Rig.Recorder->Contexts.Last().PlaybackComponent == Replacement);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCueDetectionRestartMutationBeforeSuperTest,
	"Paper2DPlus.FrameCues.Detection.CustomRestartMutationDoesNotAdvanceReplacement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCueDetectionRestartMutationBeforeSuperTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusCueDetectionTest;

	FCueDetect_ScopedWorld ScopedWorld;
	if (!ScopedWorld.IsUsable())
	{
		CueDetect_MarkSkipped(*this, TEXT("Custom restart mutation before Super"),
			TEXT("a transient game world could not begin play in this session."));
		return true;
	}

	UPaperFlipbook* Attack = CueDetect_MakeFlipbook(8, 10.0f);
	UPaperFlipbook* Replacement = CueDetect_MakeFlipbook(8, 10.0f);
	UPaper2DPlusTestBehaviorRangeCue* Range =
		NewObject<UPaper2DPlusTestBehaviorRangeCue>();
	Range->StartFrame = 0;
	Range->FrameCount = 8;
	UPaper2DPlusTestBehaviorMomentCue* Mutator =
		NewObject<UPaper2DPlusTestBehaviorMomentCue>();
	Mutator->TriggerFrame = 0;

	UPaper2DPlusCharacterProfileAsset* Profile =
		NewObject<UPaper2DPlusCharacterProfileAsset>();
	CueDetect_AddAnimation(*Profile, TEXT("Attack"), Attack, {Range, Mutator});
	CueDetect_AddAnimation(*Profile, TEXT("Replacement"), Replacement, {});
	FCueDetect_Rig Rig = CueDetect_MakeRig(
		*ScopedWorld.World,
		Profile,
		Attack,
		/*bEventDrivenComponent=*/true,
		/*bLooping=*/false);

	Rig.FlipbookComponent->Stop();
	CueDetect_AdvanceOneFrame(Rig, 0.010f);
	Mutator->MutateToFlipbook = Replacement;
	Rig.FlipbookComponent->PlayFromStart();
	CueDetect_ExecuteComponentTick(Rig.FlipbookComponent->PrimaryComponentTick, 0.110f);

	TestTrue(TEXT("The restart Cue commits the replacement flipbook"),
		Rig.FlipbookComponent->GetFlipbook() == Replacement);
	TestTrue(TEXT("The old tick does not advance the replacement after reentrant mutation"),
		FMath::IsNearlyZero(Rig.FlipbookComponent->GetPlaybackPosition()));
	TestEqual(TEXT("The frame-0 mutator executes once per Attack generation"),
		CueDetect_CountPhase(*Rig.Recorder, Mutator, EPaper2DPlusFrameCuePhase::Trigger), 2);
	TestEqual(TEXT("The stopped and then interrupted Attack ranges each receive one End"),
		CueDetect_CountPhase(*Rig.Recorder, Range, EPaper2DPlusFrameCuePhase::End), 2);
	TestFalse(TEXT("The cue-free replacement has no inherited active range"),
		Rig.ProfileComponent->IsRangeCueActiveForTests(Range));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCueDetectionDestroyedStockSourceReentrantReplacementTest,
	"Paper2DPlus.FrameCues.Detection.DestroyedStockSourceKeepsReentrantReplacement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCueDetectionDestroyedStockSourceReentrantReplacementTest::RunTest(
	const FString& Parameters)
{
	using namespace Paper2DPlusCueDetectionTest;

	FCueDetect_ScopedWorld ScopedWorld;
	if (!ScopedWorld.IsUsable())
	{
		CueDetect_MarkSkipped(*this, TEXT("Destroyed stock source reentrant replacement"),
			TEXT("a transient game world could not begin play in this session."));
		return true;
	}

	UPaperFlipbook* Attack = CueDetect_MakeFlipbook(8, 10.0f);
	UPaper2DPlusTestBehaviorRangeCue* Range =
		NewObject<UPaper2DPlusTestBehaviorRangeCue>();
	Range->StartFrame = 0;
	Range->FrameCount = 8;
	UPaper2DPlusCharacterProfileAsset* Profile =
		NewObject<UPaper2DPlusCharacterProfileAsset>();
	CueDetect_AddAnimation(*Profile, TEXT("Attack"), Attack, {Range});
	FCueDetect_Rig Rig = CueDetect_MakeRig(
		*ScopedWorld.World,
		Profile,
		Attack,
		/*bEventDrivenComponent=*/false,
		/*bLooping=*/false);

	UPaperFlipbookComponent* Replacement =
		NewObject<UPaperFlipbookComponent>(Rig.Actor.Get());
	Replacement->SetLooping(false);
	Replacement->SetFlipbook(Attack);
	Replacement->RegisterComponent();
	Rig.Actor->SetRootComponent(Replacement);

	UPaper2DPlusFrameCueMutationReceiver* Receiver =
		NewObject<UPaper2DPlusFrameCueMutationReceiver>(Rig.Actor.Get());
	Receiver->TriggerCue = Range;
	Receiver->TriggerPhase = EPaper2DPlusFrameCuePhase::End;
	Receiver->TargetPlaybackComponent = Replacement;
	Rig.ProfileComponent->OnFrameCue.AddDynamic(
		Receiver, &UPaper2DPlusFrameCueMutationReceiver::OnCue);

	UPaperFlipbookComponent* DestroyedSource = Rig.FlipbookComponent.Get();
	DestroyedSource->DestroyComponent();
	CueDetect_ExecuteComponentTick(Rig.ProfileComponent->PrimaryComponentTick, 0.010f);

	TestEqual(TEXT("The Source Removed End queues one replacement"),
		Receiver->MutationCount, 1);
	TestTrue(TEXT("Destroyed-source cleanup preserves the reentrant replacement"),
		Rig.ProfileComponent->GetResolvedFlipbookComponent() == Replacement);
	TestEqual(TEXT("The destroyed stock source closes once"),
		CueDetect_CountPhase(*Rig.Recorder, Range, EPaper2DPlusFrameCuePhase::End), 1);
	const int32 EndIndex = CueDetect_FindPhaseIndex(
		*Rig.Recorder, Range, EPaper2DPlusFrameCuePhase::End);
	if (Rig.Recorder->Contexts.IsValidIndex(EndIndex))
	{
		TestEqual(TEXT("The destroyed stock source reports Source Removed"),
			Rig.Recorder->Contexts[EndIndex].EndReason,
			EPaper2DPlusFrameCueEndReason::SourceRemoved);
	}
	TestEqual(TEXT("The replacement opens a fresh frame-0 lifecycle"),
		CueDetect_CountPhase(*Rig.Recorder, Range, EPaper2DPlusFrameCuePhase::Begin), 2);
	TestTrue(TEXT("The replacement range stays active"),
		Rig.ProfileComponent->IsRangeCueActiveForTests(Range));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCueDetectionPendingKillSourceProfileTeardownTest,
	"Paper2DPlus.FrameCues.Detection.ProfileTeardownEndsPendingKillStockSource",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCueDetectionPendingKillSourceProfileTeardownTest::RunTest(
	const FString& Parameters)
{
	using namespace Paper2DPlusCueDetectionTest;

	FCueDetect_ScopedWorld ScopedWorld;
	if (!ScopedWorld.IsUsable())
	{
		CueDetect_MarkSkipped(*this, TEXT("Pending-kill source Profile teardown"),
			TEXT("a transient game world could not begin play in this session."));
		return true;
	}

	UPaperFlipbook* Attack = CueDetect_MakeFlipbook(8, 10.0f);
	UPaper2DPlusTestBehaviorRangeCue* Range =
		NewObject<UPaper2DPlusTestBehaviorRangeCue>();
	Range->StartFrame = 0;
	Range->FrameCount = 8;
	UPaper2DPlusCharacterProfileAsset* Profile =
		NewObject<UPaper2DPlusCharacterProfileAsset>();
	CueDetect_AddAnimation(*Profile, TEXT("Attack"), Attack, {Range});
	FCueDetect_Rig Rig = CueDetect_MakeRig(
		*ScopedWorld.World,
		Profile,
		Attack,
		/*bEventDrivenComponent=*/false,
		/*bLooping=*/false);

	Rig.FlipbookComponent->DestroyComponent();
	Rig.ProfileComponent->DestroyComponent();

	TestEqual(TEXT("Profile teardown closes the pending-kill source lifecycle once"),
		CueDetect_CountPhase(*Rig.Recorder, Range, EPaper2DPlusFrameCuePhase::End), 1);
	const int32 EndIndex = CueDetect_FindPhaseIndex(
		*Rig.Recorder, Range, EPaper2DPlusFrameCuePhase::End);
	if (Rig.Recorder->Contexts.IsValidIndex(EndIndex))
	{
		TestEqual(TEXT("Profile teardown keeps the Component Destroyed reason"),
			Rig.Recorder->Contexts[EndIndex].EndReason,
			EPaper2DPlusFrameCueEndReason::ComponentDestroyed);
	}
	return true;
}

/**
 * A stale terminal report cannot close a same-flipbook restart.
 *
 * Stop and PlayFromStart preserve flipbook identity and frame 0, so only the playback generation can
 * distinguish the two sessions. The restart must force frame-0 evaluation even though the key-frame
 * index is unchanged, and only its own generation may close the second range lifecycle.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCueDetectionSameFlipbookRestartRejectsStaleStopTest,
	"Paper2DPlus.FrameCues.Detection.SameFlipbookRestartRejectsStaleStop",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCueDetectionSameFlipbookRestartRejectsStaleStopTest::RunTest(
	const FString& Parameters)
{
	using namespace Paper2DPlusCueDetectionTest;

	FCueDetect_ScopedWorld ScopedWorld;
	if (!ScopedWorld.IsUsable())
	{
		CueDetect_MarkSkipped(*this, TEXT("Same-flipbook restart generation"),
			TEXT("a transient game world could not begin play in this session."));
		return true;
	}

	for (const bool bEventDrivenComponent : {true, false})
	{
		Paper2DPlusBehaviorTestLog::Reset();
		const FString Path = bEventDrivenComponent ? TEXT("custom") : TEXT("stock");

		UPaperFlipbook* Attack = CueDetect_MakeFlipbook(8, 10.0f);
		UPaper2DPlusTestBehaviorRangeCue* Range =
			NewObject<UPaper2DPlusTestBehaviorRangeCue>();
		Range->StartFrame = 0;
		Range->FrameCount = 8;

		UPaper2DPlusCharacterProfileAsset* Profile =
			NewObject<UPaper2DPlusCharacterProfileAsset>();
		CueDetect_AddAnimation(*Profile, TEXT("Attack"), Attack, {Range});

		FCueDetect_Rig Rig = CueDetect_MakeRig(
			*ScopedWorld.World,
			Profile,
			Attack,
			bEventDrivenComponent,
			/*bLooping=*/false);
		const int64 GenerationOne = Rig.ProfileComponent->GetFrameCuePlaybackGeneration();
		TestTrue(
			*FString::Printf(TEXT("%s path exposes the first positive generation"), *Path),
			GenerationOne > 0);

		Rig.FlipbookComponent->Stop();
		CueDetect_AdvanceOneFrame(Rig, 0.016f);
		TestEqual(
			*FString::Printf(TEXT("%s path observes the first session stop exactly once"), *Path),
			CueDetect_CountPhase(*Rig.Recorder, Range, EPaper2DPlusFrameCuePhase::End),
			1);
		TestFalse(
			*FString::Printf(TEXT("%s path closes the first session's range"), *Path),
			Rig.ProfileComponent->IsRangeCueActiveForTests(Range));

		Rig.FlipbookComponent->PlayFromStart();
		CueDetect_AdvanceOneFrame(Rig, 0.010f);
		const int64 GenerationTwo = Rig.ProfileComponent->GetFrameCuePlaybackGeneration();
		AddInfo(FString::Printf(
			TEXT("%s path restarted generation %lld after %lld: %s"),
			*Path,
			static_cast<long long>(GenerationTwo),
			static_cast<long long>(GenerationOne),
			*CueDetect_DescribePlayback(Rig)));

		TestTrue(
			*FString::Printf(TEXT("%s path assigns the same-flipbook restart a new generation"), *Path),
			GenerationTwo > 0 && GenerationTwo != GenerationOne);
		TestEqual(
			*FString::Printf(TEXT("%s path force-evaluates frame 0 for the restarted generation"), *Path),
			CueDetect_CountPhase(*Rig.Recorder, Range, EPaper2DPlusFrameCuePhase::Begin),
			2);
		TestTrue(
			*FString::Printf(TEXT("%s path reactivates the frame-0 range after restart"), *Path),
			Rig.ProfileComponent->IsRangeCueActiveForTests(Range));

		TestFalse(
			*FString::Printf(TEXT("%s path rejects the stopped generation after restart"), *Path),
			Rig.ProfileComponent->ReportFrameCuePlaybackStopped(
				Rig.FlipbookComponent.Get(), GenerationOne));
		TestEqual(
			*FString::Printf(TEXT("%s stale report does not End the restarted range"), *Path),
			CueDetect_CountPhase(*Rig.Recorder, Range, EPaper2DPlusFrameCuePhase::End),
			1);
		TestTrue(
			*FString::Printf(TEXT("%s stale report leaves the restarted range active"), *Path),
			Rig.ProfileComponent->IsRangeCueActiveForTests(Range));

		TestTrue(
			*FString::Printf(TEXT("%s path accepts the restarted generation's stop report"), *Path),
			Rig.ProfileComponent->ReportFrameCuePlaybackStopped(
				Rig.FlipbookComponent.Get(), GenerationTwo));
		TestEqual(
			*FString::Printf(TEXT("%s current report Ends only the restarted lifecycle"), *Path),
			CueDetect_CountPhase(*Rig.Recorder, Range, EPaper2DPlusFrameCuePhase::End),
			2);
		TestFalse(
			*FString::Printf(TEXT("%s current report leaves no restarted range active"), *Path),
			Rig.ProfileComponent->IsRangeCueActiveForTests(Range));
	}

	return true;
}

/**
 * A manually driven source reopens dispatch after an observed stop.
 *
 * PaperZD — and any driver that positions the sprite by hand — calls Stop() once at init and then
 * moves playback with SetPlaybackPosition, so IsPlaying() never flips back to true. The observed
 * playing→stopped transition claims a PlaybackStopped terminal; without the frame-advance reopen in
 * HandleFrameChanged, that one claim would permanently silence every later cue on the component
 * (the shipped no-fire: cues preview in the editor and never fire in game). Frame movement on the
 * bound source IS playback, so the generation must reopen natively and both the moment anchor and
 * the range lifecycle must run again — with no Play() call, ever.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCueDetectionManualDriveReopensAfterObservedStopTest,
	"Paper2DPlus.FrameCues.Detection.ManualDriveReopensAfterObservedStop",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCueDetectionManualDriveReopensAfterObservedStopTest::RunTest(
	const FString& Parameters)
{
	using namespace Paper2DPlusCueDetectionTest;

	FCueDetect_ScopedWorld ScopedWorld;
	if (!ScopedWorld.IsUsable())
	{
		CueDetect_MarkSkipped(*this, TEXT("Manual-drive reopen after observed stop"),
			TEXT("a transient game world could not begin play in this session."));
		return true;
	}

	for (const bool bEventDrivenComponent : {true, false})
	{
		const FString Path = bEventDrivenComponent ? TEXT("custom") : TEXT("stock");
		UPaperFlipbook* Attack = CueDetect_MakeFlipbook(8, 10.0f);
		UPaper2DPlusTestBehaviorMomentCue* Moment =
			NewObject<UPaper2DPlusTestBehaviorMomentCue>();
		Moment->TriggerFrame = 3;
		UPaper2DPlusTestBehaviorRangeCue* Range =
			NewObject<UPaper2DPlusTestBehaviorRangeCue>();
		Range->StartFrame = 0;
		Range->FrameCount = 8;

		UPaper2DPlusCharacterProfileAsset* Profile =
			NewObject<UPaper2DPlusCharacterProfileAsset>();
		CueDetect_AddAnimation(*Profile, TEXT("Attack"), Attack, {Moment, Range});
		FCueDetect_Rig Rig = CueDetect_MakeRig(
			*ScopedWorld.World,
			Profile,
			Attack,
			bEventDrivenComponent,
			/*bLooping=*/false);

		CueDetect_AdvanceOneFrame(Rig, 0.016f);
		TestTrue(
			*FString::Printf(TEXT("%s path begins the frame-0 range while playing"), *Path),
			Rig.ProfileComponent->IsRangeCueActiveForTests(Range));

		Rig.FlipbookComponent->Stop();
		CueDetect_AdvanceOneFrame(Rig, 0.016f);
		TestEqual(
			*FString::Printf(TEXT("%s path observes the stop exactly once"), *Path),
			CueDetect_CountPhase(*Rig.Recorder, Range, EPaper2DPlusFrameCuePhase::End),
			1);
		TestFalse(
			*FString::Printf(TEXT("%s path closes the stopped session's range"), *Path),
			Rig.ProfileComponent->IsRangeCueActiveForTests(Range));
		const int64 StoppedGeneration =
			Rig.ProfileComponent->GetFrameCuePlaybackGeneration();
		const int32 TriggersBeforeDrive =
			CueDetect_CountPhase(*Rig.Recorder, Moment, EPaper2DPlusFrameCuePhase::Trigger);

		// The PaperZD shape: the sprite is never Played again; playback is position writes only.
		// Mid-frame times walk every key frame 0..7 exactly once at 10 fps.
		for (int32 Step = 0; Step < 8; ++Step)
		{
			Rig.FlipbookComponent->SetPlaybackPosition(
				0.05f + 0.1f * Step, /*bFireEvents=*/false);
			CueDetect_AdvanceOneFrame(Rig, 0.016f);
		}

		AddInfo(FString::Printf(
			TEXT("%s path drove generations %lld -> %lld: %s"),
			*Path,
			static_cast<long long>(StoppedGeneration),
			static_cast<long long>(Rig.ProfileComponent->GetFrameCuePlaybackGeneration()),
			*CueDetect_DescribePlayback(Rig)));
		TestTrue(
			*FString::Printf(TEXT("%s path reopens a native generation from frame movement"), *Path),
			Rig.ProfileComponent->GetFrameCuePlaybackGeneration() != StoppedGeneration);
		TestEqual(
			*FString::Printf(TEXT("%s path fires the moment anchor during the manual drive"), *Path),
			CueDetect_CountPhase(*Rig.Recorder, Moment, EPaper2DPlusFrameCuePhase::Trigger),
			TriggersBeforeDrive + 1);
		TestEqual(
			*FString::Printf(TEXT("%s path re-begins the range for the manual session"), *Path),
			CueDetect_CountPhase(*Rig.Recorder, Range, EPaper2DPlusFrameCuePhase::Begin),
			2);
		TestTrue(
			*FString::Printf(TEXT("%s path leaves the manual session's range active"), *Path),
			Rig.ProfileComponent->IsRangeCueActiveForTests(Range));
	}

	return true;
}

/**
 * An externally reported generation owns only that generation.
 *
 * After its terminal drains, ordinary playback on the same bound source must return to the native
 * custom callback or stock poll path. Otherwise one PaperZD-shaped handoff permanently suppresses
 * every later Frame Cue session on that component.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCueDetectionExternalGenerationHandsBackToNativePlaybackTest,
	"Paper2DPlus.FrameCues.Detection.ExternalGenerationHandsBackToNativePlayback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCueDetectionExternalGenerationHandsBackToNativePlaybackTest::RunTest(
	const FString& Parameters)
{
	using namespace Paper2DPlusCueDetectionTest;

	FCueDetect_ScopedWorld ScopedWorld;
	if (!ScopedWorld.IsUsable())
	{
		CueDetect_MarkSkipped(*this, TEXT("External-to-native playback handback"),
			TEXT("a transient game world could not begin play in this session."));
		return true;
	}

	for (const bool bEventDrivenComponent : {true, false})
	{
		const FString Path = bEventDrivenComponent ? TEXT("custom") : TEXT("stock");
		UPaperFlipbook* Attack = CueDetect_MakeFlipbook(8, 10.0f);
		UPaper2DPlusTestBehaviorRangeCue* Range =
			NewObject<UPaper2DPlusTestBehaviorRangeCue>();
		Range->StartFrame = 0;
		Range->FrameCount = 8;

		UPaper2DPlusCharacterProfileAsset* Profile =
			NewObject<UPaper2DPlusCharacterProfileAsset>();
		CueDetect_AddAnimation(*Profile, TEXT("Attack"), Attack, {Range});
		FCueDetect_Rig Rig = CueDetect_MakeRig(
			*ScopedWorld.World,
			Profile,
			Attack,
			bEventDrivenComponent,
			/*bLooping=*/false);

		Rig.FlipbookComponent->Stop();
		CueDetect_AdvanceOneFrame(Rig, 0.016f);
		Rig.FlipbookComponent->SetPlaybackPosition(0.0f, /*bFireEvents=*/false);

		const int64 ExternalGeneration =
			Rig.ProfileComponent->BeginExternalFrameCuePlayback(
				Rig.FlipbookComponent.Get());
		TestTrue(
			*FString::Printf(TEXT("%s path opens an external generation"), *Path),
			ExternalGeneration > 0);
		TestTrue(
			*FString::Printf(TEXT("%s external generation begins the frame-0 range"), *Path),
			Rig.ProfileComponent->IsRangeCueActiveForTests(Range));
		TestTrue(
			*FString::Printf(TEXT("%s path accepts the external completion"), *Path),
			Rig.ProfileComponent->ReportFrameCuePlaybackCompleted(
				Rig.FlipbookComponent.Get(),
				ExternalGeneration));

		const int32 BeginCountBeforeHandback = CueDetect_CountPhase(
			*Rig.Recorder,
			Range,
			EPaper2DPlusFrameCuePhase::Begin);
		Rig.FlipbookComponent->PlayFromStart();
		CueDetect_AdvanceOneFrame(Rig, 0.010f);
		const int64 NativeGeneration =
			Rig.ProfileComponent->GetFrameCuePlaybackGeneration();

		TestTrue(
			*FString::Printf(TEXT("%s path advances to a native handback generation"), *Path),
			NativeGeneration > 0 && NativeGeneration != ExternalGeneration);
		TestEqual(
			*FString::Printf(TEXT("%s path re-begins the range after native handback"), *Path),
			CueDetect_CountPhase(
				*Rig.Recorder,
				Range,
				EPaper2DPlusFrameCuePhase::Begin),
			BeginCountBeforeHandback + 1);
		TestTrue(
			*FString::Printf(TEXT("%s native handback owns the active range"), *Path),
			Rig.ProfileComponent->IsRangeCueActiveForTests(Range));

		Rig.FlipbookComponent->Stop();
		CueDetect_AdvanceOneFrame(Rig, 0.016f);
		TestFalse(
			*FString::Printf(TEXT("%s native handback still observes its terminal"), *Path),
			Rig.ProfileComponent->IsRangeCueActiveForTests(Range));

		Rig.FlipbookComponent->SetPlaybackPosition(0.0f, /*bFireEvents=*/false);
		const int64 ReentrantExternalGeneration =
			Rig.ProfileComponent->BeginExternalFrameCuePlayback(
				Rig.FlipbookComponent.Get());
		TestTrue(
			*FString::Printf(TEXT("%s path opens the reentrant external generation"), *Path),
			ReentrantExternalGeneration > 0);
		const int32 BeginCountBeforeReentrantHandback = CueDetect_CountPhase(
			*Rig.Recorder,
			Range,
			EPaper2DPlusFrameCuePhase::Begin);
		Range->bPlayFromStartOnEnd = true;
		TestTrue(
			*FString::Printf(
				TEXT("%s path accepts completion whose End restarts playback"),
				*Path),
			Rig.ProfileComponent->ReportFrameCuePlaybackCompleted(
				Rig.FlipbookComponent.Get(),
				ReentrantExternalGeneration));
		Range->bPlayFromStartOnEnd = false;
		const int64 ReentrantNativeGeneration =
			Rig.ProfileComponent->GetFrameCuePlaybackGeneration();

		TestTrue(
			*FString::Printf(
				TEXT("%s path replays the native start after external End returns"),
				*Path),
			ReentrantNativeGeneration > 0
				&& ReentrantNativeGeneration != ReentrantExternalGeneration);
		TestEqual(
			*FString::Printf(
				TEXT("%s reentrant handback begins the next range exactly once"),
				*Path),
			CueDetect_CountPhase(
				*Rig.Recorder,
				Range,
				EPaper2DPlusFrameCuePhase::Begin),
			BeginCountBeforeReentrantHandback + 1);
		TestTrue(
			*FString::Printf(TEXT("%s reentrant handback owns the active range"), *Path),
			Rig.ProfileComponent->IsRangeCueActiveForTests(Range));

		Rig.FlipbookComponent->Stop();
		CueDetect_AdvanceOneFrame(Rig, 0.016f);
		TestFalse(
			*FString::Printf(TEXT("%s reentrant native handback still terminates"), *Path),
			Rig.ProfileComponent->IsRangeCueActiveForTests(Range));
	}

	return true;
}

/**
 * A poll that skips several key frames at once still owes every anchor in the span.
 *
 * The span (previous, current] is what makes a coarse sample lossless INSIDE an animation; this
 * drives it with real playback so the claim is about the shipping path and not a hand-called
 * HandleFrameChanged sequence. The 100 ms step is far coarser than the animation's own 16.7 ms
 * frames and coarser than what remains of it after the first step, so the anchors are collected in
 * two bites — including the ones past the point where a non-looping animation stops. Every anchor,
 * exactly once, is the whole claim.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCueDetectionMultiFrameSkipTest,
	"Paper2DPlus.FrameCues.Detection.MultiFrameSkipWithinOnePollCatchesEveryAnchor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCueDetectionMultiFrameSkipTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusCueDetectionTest;

	FCueDetect_ScopedWorld ScopedWorld;
	if (!ScopedWorld.IsUsable())
	{
		CueDetect_MarkSkipped(*this, TEXT("Multi-frame skip"),
			TEXT("a transient game world could not begin play in this session."));
		return true;
	}

	// Eight 60 fps frames = 133 ms, sampled every 100 ms (and the animation stays longer than one
	// sample, so the too-coarse diagnostic deliberately does NOT fire here).
	UPaperFlipbook* Attack = CueDetect_MakeFlipbook(8, 60.0f);

	UPaper2DPlusCharacterProfileAsset* Profile = NewObject<UPaper2DPlusCharacterProfileAsset>();
	TArray<UPaper2DPlusCueBase*> Cues;
	TArray<UPaper2DPlusTestMomentCue*> Anchors;
	for (int32 Frame = 1; Frame <= 6; ++Frame)
	{
		UPaper2DPlusTestMomentCue* Cue = NewObject<UPaper2DPlusTestMomentCue>();
		Cue->TriggerFrame = Frame;
		Anchors.Add(Cue);
		Cues.Add(Cue);
	}
	CueDetect_AddAnimation(*Profile, TEXT("Attack"), Attack, Cues);

	FCueDetect_Rig Rig = CueDetect_MakeRig(
		*ScopedWorld.World, Profile, Attack, /*bEventDrivenComponent=*/false, /*bLooping=*/false);

	CueDetect_AdvanceFrames(Rig, 6, 0.100f);
	AddInfo(TEXT("after the skipped span: ") + CueDetect_DescribePlayback(Rig));

	for (int32 Index = 0; Index < Anchors.Num(); ++Index)
	{
		TestEqual(
			*FString::Printf(
				TEXT("Anchor on frame %d fires exactly once across the skipped span"), Index + 1),
			CueDetect_CountPhase(*Rig.Recorder, Anchors[Index], EPaper2DPlusFrameCuePhase::Trigger),
			1);
	}

	return true;
}

/**
 * The loop-wrap heuristic, observed at a real tick rate rather than asserted on a synthetic span.
 *
 * Forward playback is the documented assumption: a key-frame index that moves BACKWARD is read as a
 * loop wrap, which is why reverse playback stays out of scope (HandleFrameChanged states this at the
 * heuristic itself). This pins one anchor per pass — no double fire at the seam, no missed pass.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCueDetectionLoopWrapTest,
	"Paper2DPlus.FrameCues.Detection.LoopWrapHeuristicUnaffected",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCueDetectionLoopWrapTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusCueDetectionTest;

	FCueDetect_ScopedWorld ScopedWorld;
	if (!ScopedWorld.IsUsable())
	{
		CueDetect_MarkSkipped(*this, TEXT("Loop wrap"),
			TEXT("a transient game world could not begin play in this session."));
		return true;
	}

	// Four 10 fps frames = 400 ms per pass, sampled every 16 ms. 960 ms is two wraps: frame 0 is
	// entered at play start and again after each wrap; frame 2 is crossed once per completed pass.
	UPaperFlipbook* Loop = CueDetect_MakeFlipbook(4, 10.0f);

	UPaper2DPlusTestMomentCue* FirstFrameCue = NewObject<UPaper2DPlusTestMomentCue>();
	FirstFrameCue->TriggerFrame = 0;
	UPaper2DPlusTestMomentCue* MidFrameCue = NewObject<UPaper2DPlusTestMomentCue>();
	MidFrameCue->TriggerFrame = 2;

	UPaper2DPlusCharacterProfileAsset* Profile = NewObject<UPaper2DPlusCharacterProfileAsset>();
	CueDetect_AddAnimation(*Profile, TEXT("Loop"), Loop, {FirstFrameCue, MidFrameCue});

	FCueDetect_Rig Rig = CueDetect_MakeRig(
		*ScopedWorld.World, Profile, Loop, /*bEventDrivenComponent=*/false, /*bLooping=*/true);

	CueDetect_AdvanceFrames(Rig, 60, 0.016f);
	AddInfo(TEXT("after two wraps: ") + CueDetect_DescribePlayback(Rig));

	TestEqual(
		TEXT("The frame-0 anchor fires once per pass across two wraps"),
		CueDetect_CountPhase(*Rig.Recorder, FirstFrameCue, EPaper2DPlusFrameCuePhase::Trigger),
		3);
	TestEqual(
		TEXT("A mid-animation anchor fires once per completed pass"),
		CueDetect_CountPhase(*Rig.Recorder, MidFrameCue, EPaper2DPlusFrameCuePhase::Trigger),
		2);

	// The forward-playback assumption, stated as a test: without a wrap flag, a backward step is NOT
	// read as a traversal of everything in between.
	TestFalse(
		TEXT("Backward frame movement without a wrap traverses only the target frame"),
		Paper2DPlusFrameCues::WasFrameTraversed(2, /*Previous=*/3, /*Current=*/1, /*bWasLoopWrap=*/false));
	TestTrue(
		TEXT("A wrap traverses the frames on both sides of the seam"),
		Paper2DPlusFrameCues::WasFrameTraversed(3, /*Previous=*/2, /*Current=*/1, /*bWasLoopWrap=*/true));

	return true;
}

/**
 * When the sample really is too coarse for the animation, the component says so — by name.
 *
 * R8's floor: an animation with cues either dispatches, or the actor and component responsible are
 * named out loud, once, in the log AND on screen. A 40 ms animation under a 20 Hz watch is that case,
 * and the diagnostic has to be attributable enough to walk straight to the offending actor.
 *
 * WHY THE RIG PUTS THE ACTOR AT 20 Hz DELIBERATELY. The diagnostic judges the sampling RATE this
 * component asked for, not the DeltaTime one tick happened to receive — a shader-compile hitch hands
 * a 300 ms delta to an actor that is already watching every frame it can, and telling that designer
 * to change components would be a false alarm they cannot dismiss. So a coarse rate has to be real
 * here: the legacy-policy switch is how the test produces one deterministically, and in the field the
 * same state arrives from a rate answer that is stale or out of band (see ProfileCarriesAnyFrameCues,
 * where an in-place profile edit is exactly what this diagnostic backstops).
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCueDetectionCoarsePollDiagnosticTest,
	"Paper2DPlus.FrameCues.Detection.CoarsePollDiagnosticNamesActorAndComponent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCueDetectionCoarsePollDiagnosticTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusCueDetectionTest;

	FCueDetect_ScopedWorld ScopedWorld;
	if (!ScopedWorld.IsUsable())
	{
		CueDetect_MarkSkipped(*this, TEXT("Coarse-poll diagnostic"),
			TEXT("a transient game world could not begin play in this session."));
		return true;
	}

	AddExpectedError(
		TEXT("cues on animations this short can be missed entirely"),
		EAutomationExpectedErrorFlags::Contains);

	// A cue-free idle to sit on (so the watch stays at its fallback rate), and four 100 fps frames =
	// 40 ms of attack, which is shorter than the 50 ms period that eventually samples it.
	UPaperFlipbook* Idle = CueDetect_MakeFlipbook(4, 10.0f);
	UPaperFlipbook* Attack = CueDetect_MakeFlipbook(4, 100.0f);

	UPaper2DPlusTestMomentCue* AttackCue = NewObject<UPaper2DPlusTestMomentCue>();
	AttackCue->TriggerFrame = 0;

	UPaper2DPlusCharacterProfileAsset* Profile = NewObject<UPaper2DPlusCharacterProfileAsset>();
	CueDetect_AddAnimation(*Profile, TEXT("Idle"), Idle, {});
	CueDetect_AddAnimation(*Profile, TEXT("Attack"), Attack, {AttackCue});

	FCueDetect_Rig Rig = CueDetect_MakeRig(
		*ScopedWorld.World, Profile, Idle, /*bEventDrivenComponent=*/false, /*bLooping=*/false,
		/*bLegacySlowPollPolicy=*/true);
	TestTrue(
		TEXT("The rig really is sampling coarsely — the diagnostic must never fire without that"),
		Rig.ProfileComponent->PrimaryComponentTick.TickInterval > 0.0f);

	CueDetect_ObserveAnimationAfterCoarsePoll(Rig, Attack);
	AddInfo(TEXT("after the late observation: ") + CueDetect_DescribePlayback(Rig));

	const TArray<FString>& Diagnostics = Rig.ProfileComponent->GetDetectionDiagnosticsForTests();
	TestEqual(TEXT("The coarse sample is reported exactly once per actor"), Diagnostics.Num(), 1);
	if (Diagnostics.Num() >= 1)
	{
		TestTrue(TEXT("The diagnostic names the owning actor"),
			Diagnostics[0].Contains(Rig.Actor->GetName()));
		TestTrue(TEXT("The diagnostic names the sprite component being polled"),
			Diagnostics[0].Contains(Rig.FlipbookComponent->GetName()));
		TestTrue(TEXT("The diagnostic names the animation"), Diagnostics[0].Contains(TEXT("Attack")));
	}

	return true;
}

/**
 * A frame hitch is not a coarse poll, and an actor at maximum rate is never told to speed up.
 *
 * Every cue-carrying actor now watches every frame; a routine PIE hitch (shader compile, sync load)
 * still hands that tick a 100-500 ms DeltaTime. Judged on the delta, the diagnostic latches a
 * permanent on-screen instruction to change components on an actor that missed nothing and has no
 * faster rate available — a false alarm nobody can dismiss, which trains designers to ignore the real
 * one. That is the same silence R8 forbids, reached from the other side, so it is pinned as a test:
 * hitch-sized deltas, an animation far shorter than any of them, and not one word.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCueDetectionHitchIsNotCoarsePollTest,
	"Paper2DPlus.FrameCues.Detection.FrameHitchDoesNotFakeACoarsePollDiagnostic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCueDetectionHitchIsNotCoarsePollTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusCueDetectionTest;

	FCueDetect_ScopedWorld ScopedWorld;
	if (!ScopedWorld.IsUsable())
	{
		CueDetect_MarkSkipped(*this, TEXT("Frame hitch vs coarse poll"),
			TEXT("a transient game world could not begin play in this session."));
		return true;
	}

	// Four 100 fps frames = 40 ms, under 100 ms deltas — the exact numbers that made this fire before.
	UPaperFlipbook* Attack = CueDetect_MakeFlipbook(4, 100.0f);

	UPaper2DPlusTestMomentCue* AttackCue = NewObject<UPaper2DPlusTestMomentCue>();
	AttackCue->TriggerFrame = 0;

	UPaper2DPlusCharacterProfileAsset* Profile = NewObject<UPaper2DPlusCharacterProfileAsset>();
	CueDetect_AddAnimation(*Profile, TEXT("Attack"), Attack, {AttackCue});

	FCueDetect_Rig Rig = CueDetect_MakeRig(
		*ScopedWorld.World, Profile, Attack, /*bEventDrivenComponent=*/false, /*bLooping=*/true);

	// The policy under test, observed rather than assumed: a cue-carrying actor is already at the
	// finest rate the poll has, which is the whole reason the advice would be nonsense.
	TestEqual(
		TEXT("A cue-carrying actor watches every frame"),
		Rig.ProfileComponent->PrimaryComponentTick.TickInterval, 0.0f);

	CueDetect_AdvanceFrames(Rig, 6, 0.100f);
	AddInfo(TEXT("after six hitched frames: ") + CueDetect_DescribePlayback(Rig));

	TestEqual(
		TEXT("A hitch on an every-frame watch produces no coarse-poll diagnostic"),
		Rig.ProfileComponent->GetDetectionDiagnosticsForTests().Num(), 0);

	return true;
}

/**
 * The gap is judged against WALL-CLOCK life, not authored length.
 *
 * GetTotalDuration() is unscaled: a 400 ms animation at PlayRate 10 is over in 40 ms and steps between
 * two 20 Hz samples exactly like a 40 ms one — while the authored number (0.4 > 0.05) short-circuits
 * the check and says the sample was plenty fine. That is the dangerous direction, a false NEGATIVE:
 * the animation really was missable and nobody was told. The same animation at rate 1 is the control
 * that keeps the scaling honest rather than merely loud.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCueDetectionCoarsePollPlayRateTest,
	"Paper2DPlus.FrameCues.Detection.CoarsePollDiagnosticAccountsForPlayRate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCueDetectionCoarsePollPlayRateTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusCueDetectionTest;

	FCueDetect_ScopedWorld ScopedWorld;
	if (!ScopedWorld.IsUsable())
	{
		CueDetect_MarkSkipped(*this, TEXT("Coarse-poll play rate"),
			TEXT("a transient game world could not begin play in this session."));
		return true;
	}

	AddExpectedError(
		TEXT("cues on animations this short can be missed entirely"),
		EAutomationExpectedErrorFlags::Contains);

	// Four 10 fps frames = 400 ms authored. At rate 10 that is 40 ms of wall time, under the 50 ms
	// watch; at rate 1 it is 400 ms, comfortably longer than the watch and genuinely not reportable.
	UPaperFlipbook* Idle = CueDetect_MakeFlipbook(4, 10.0f);
	UPaperFlipbook* Attack = CueDetect_MakeFlipbook(4, 10.0f);

	UPaper2DPlusTestMomentCue* AttackCue = NewObject<UPaper2DPlusTestMomentCue>();
	AttackCue->TriggerFrame = 0;

	UPaper2DPlusCharacterProfileAsset* Profile = NewObject<UPaper2DPlusCharacterProfileAsset>();
	CueDetect_AddAnimation(*Profile, TEXT("Idle"), Idle, {});
	CueDetect_AddAnimation(*Profile, TEXT("Attack"), Attack, {AttackCue});

	// See the coarse-poll test above for why the rate is forced rather than hoped for.
	FCueDetect_Rig FastRig = CueDetect_MakeRig(
		*ScopedWorld.World, Profile, Idle, /*bEventDrivenComponent=*/false, /*bLooping=*/false,
		/*bLegacySlowPollPolicy=*/true);
	FastRig.FlipbookComponent->SetPlayRate(10.0f);
	CueDetect_ObserveAnimationAfterCoarsePoll(FastRig, Attack);
	AddInfo(TEXT("play rate 10: ") + CueDetect_DescribePlayback(FastRig));

	const TArray<FString>& FastDiagnostics =
		FastRig.ProfileComponent->GetDetectionDiagnosticsForTests();
	TestEqual(
		TEXT("An animation whose PLAYED length fits inside the sample is reported"),
		FastDiagnostics.Num(), 1);
	if (FastDiagnostics.Num() >= 1)
	{
		TestTrue(TEXT("The play-rate diagnostic names the owning actor"),
			FastDiagnostics[0].Contains(FastRig.Actor->GetName()));
		TestTrue(TEXT("The play-rate diagnostic names the animation"),
			FastDiagnostics[0].Contains(TEXT("Attack")));
	}

	FCueDetect_Rig ControlRig = CueDetect_MakeRig(
		*ScopedWorld.World, Profile, Idle, /*bEventDrivenComponent=*/false, /*bLooping=*/false,
		/*bLegacySlowPollPolicy=*/true);
	CueDetect_ObserveAnimationAfterCoarsePoll(ControlRig, Attack);
	AddInfo(TEXT("play rate 1: ") + CueDetect_DescribePlayback(ControlRig));

	TestEqual(
		TEXT("The same animation at rate 1 outlives the sample and is not reported"),
		ControlRig.ProfileComponent->GetDetectionDiagnosticsForTests().Num(), 0);

	return true;
}

/**
 * A placement dispatch cannot deliver is announced, not skipped in silence.
 *
 * This is the "placed but inert" shape: dispatch already skips a placement whose Cue Type is empty
 * or deleted, and it did so without a word — which is indistinguishable, from the designer's chair,
 * from the cue system being broken. The count is taken with the SAME predicate dispatch uses.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCueDetectionUndispatchablePlacementTest,
	"Paper2DPlus.FrameCues.Detection.UndispatchablePlacementsAreLoudNotSilent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCueDetectionUndispatchablePlacementTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusCueDetectionTest;

	FCueDetect_ScopedWorld ScopedWorld;
	if (!ScopedWorld.IsUsable())
	{
		CueDetect_MarkSkipped(*this, TEXT("Undispatchable placement diagnostic"),
			TEXT("a transient game world could not begin play in this session."));
		return true;
	}

	AddExpectedError(
		TEXT("dispatch cannot deliver"),
		EAutomationExpectedErrorFlags::Contains);

	UPaperFlipbook* Attack = CueDetect_MakeFlipbook(4, 10.0f);

	UPaper2DPlusTestMomentCue* HealthyCue = NewObject<UPaper2DPlusTestMomentCue>();
	HealthyCue->TriggerFrame = 0;

	UPaper2DPlusCharacterProfileAsset* Profile = NewObject<UPaper2DPlusCharacterProfileAsset>();
	// The empty slot stands in for a placement whose Cue Type asset is gone: dispatch skips both for
	// the same reason, through the same predicate.
	CueDetect_AddAnimation(*Profile, TEXT("Attack"), Attack, {nullptr, HealthyCue});

	FCueDetect_Rig Rig = CueDetect_MakeRig(
		*ScopedWorld.World, Profile, Attack, /*bEventDrivenComponent=*/false, /*bLooping=*/false);

	CueDetect_AdvanceFrames(Rig, 4, 0.016f);

	const TArray<FString>& Diagnostics = Rig.ProfileComponent->GetDetectionDiagnosticsForTests();
	TestEqual(TEXT("The undeliverable placement is reported exactly once per actor"),
		Diagnostics.Num(), 1);
	if (Diagnostics.Num() >= 1)
	{
		TestTrue(TEXT("The diagnostic names the owning actor"),
			Diagnostics[0].Contains(Rig.Actor->GetName()));
		TestTrue(TEXT("The diagnostic names the profile component"),
			Diagnostics[0].Contains(Rig.ProfileComponent->GetName()));
		TestTrue(TEXT("The diagnostic names the animation"), Diagnostics[0].Contains(TEXT("Attack")));
	}

	// The healthy sibling is unaffected — one broken placement must not silence the animation.
	TestEqual(
		TEXT("The deliverable placement on the same animation still fires"),
		CueDetect_CountPhase(*Rig.Recorder, HealthyCue, EPaper2DPlusFrameCuePhase::Trigger),
		1);

	return true;
}

#if WITH_EDITORONLY_DATA
/**
 * The preview skip flag is an EDITOR flag. It must never explain a no-fire in game.
 *
 * Listed as a candidate cause for the reported no-fire; this falsifies it at the detection layer.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCueDetectionPreviewSkipTest,
	"Paper2DPlus.FrameCues.Detection.PreviewSkipFlagDoesNotSuppressRuntime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCueDetectionPreviewSkipTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusCueDetectionTest;

	FCueDetect_ScopedWorld ScopedWorld;
	if (!ScopedWorld.IsUsable())
	{
		CueDetect_MarkSkipped(*this, TEXT("Preview skip flag"),
			TEXT("a transient game world could not begin play in this session."));
		return true;
	}

	UPaperFlipbook* Attack = CueDetect_MakeFlipbook(4, 10.0f);

	UPaper2DPlusTestMomentCue* SkippedInPreview = NewObject<UPaper2DPlusTestMomentCue>();
	SkippedInPreview->TriggerFrame = 0;
	SkippedInPreview->bSkipInEditorPreview = true;

	UPaper2DPlusCharacterProfileAsset* Profile = NewObject<UPaper2DPlusCharacterProfileAsset>();
	CueDetect_AddAnimation(*Profile, TEXT("Attack"), Attack, {SkippedInPreview});

	FCueDetect_Rig Rig = CueDetect_MakeRig(
		*ScopedWorld.World, Profile, Attack, /*bEventDrivenComponent=*/false, /*bLooping=*/false);

	CueDetect_AdvanceFrames(Rig, 4, 0.016f);

	TestEqual(
		TEXT("bSkipInEditorPreview does not suppress runtime dispatch"),
		CueDetect_CountPhase(*Rig.Recorder, SkippedInPreview, EPaper2DPlusFrameCuePhase::Trigger),
		1);

	return true;
}
#endif // WITH_EDITORONLY_DATA

/**
 * Game code owns OnFinishedPlaying too, and clearing it must not cost this component its natural-
 * completion signal.
 *
 * OnFinishedPlaying is the ENGINE's BlueprintAssignable delegate, not the plugin's: the stock
 * "Unbind All Events from On Finished Playing" node and a C++ Clear() when re-arming a one-shot are
 * both ordinary project code. Removing the project's own handler used to remove this component's
 * with it, and the natural finish was then reported as an ordinary stop — so an in-flight Cue State
 * ended PlaybackStopped instead of Completed and a cue on the final frame could be skipped.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCueDetectionClearedFinishDelegateTest,
	"Paper2DPlus.FrameCues.Detection.ClearedFinishDelegateStillReportsCompleted",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCueDetectionClearedFinishDelegateTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusCueDetectionTest;

	FCueDetect_ScopedWorld ScopedWorld;
	if (!ScopedWorld.IsUsable())
	{
		CueDetect_MarkSkipped(*this, TEXT("Cleared finish delegate"),
			TEXT("a transient game world could not begin play in this session."));
		return true;
	}

	// Both playback paths: the custom component reads the latch directly, the stock one drains it on
	// the next ordered poll. Neither may depend on the game leaving the delegate alone.
	for (const bool bEventDrivenComponent : {true, false})
	{
		Paper2DPlusBehaviorTestLog::Reset();
		const FString Path = bEventDrivenComponent ? TEXT("custom") : TEXT("stock");

		UPaperFlipbook* Attack = CueDetect_MakeFlipbook(2, 10.0f);
		UPaper2DPlusTestBehaviorRangeCue* Range =
			NewObject<UPaper2DPlusTestBehaviorRangeCue>();
		Range->StartFrame = 0;
		Range->FrameCount = 2;
		Range->bEmitUpdates = true;

		UPaper2DPlusCharacterProfileAsset* Profile =
			NewObject<UPaper2DPlusCharacterProfileAsset>();
		CueDetect_AddAnimation(*Profile, TEXT("Attack"), Attack, {Range});

		FCueDetect_Rig Rig = CueDetect_MakeRig(
			*ScopedWorld.World,
			Profile,
			Attack,
			bEventDrivenComponent,
			/*bLooping=*/false);

		// Exactly what the stock Blueprint unbind node does.
		Rig.FlipbookComponent->OnFinishedPlaying.Clear();

		CueDetect_AdvanceOneFrame(Rig, 0.110f);
		CueDetect_AdvanceOneFrame(Rig, 0.110f);
		if (!bEventDrivenComponent)
		{
			CueDetect_AdvanceOneFrame(Rig, 0.010f);
		}

		const int32 EndIndex = CueDetect_FindPhaseIndex(
			*Rig.Recorder, Range, EPaper2DPlusFrameCuePhase::End);
		if (!TestTrue(
			*FString::Printf(TEXT("%s path still ends the range after a cleared delegate"), *Path),
			EndIndex != INDEX_NONE))
		{
			continue;
		}
		const FPaper2DPlusFrameCueContext& End = Rig.Recorder->Contexts[EndIndex];
		TestEqual(
			*FString::Printf(TEXT("%s path still labels natural completion as Completed"), *Path),
			End.EndReason,
			EPaper2DPlusFrameCueEndReason::Completed);
		TestTrue(
			*FString::Printf(TEXT("%s path does not downgrade completion to PlaybackStopped"), *Path),
			End.EndReason != EPaper2DPlusFrameCueEndReason::PlaybackStopped);
	}

	return true;
}

#if WITH_EDITOR
/**
 * Authoring the FIRST cue into a profile a live actor is ALREADY pointing at must re-open the rate
 * question.
 *
 * The memo used to key on the profile pointer alone. Adding a cue in place changes neither the
 * pointer nor the element count, so the component kept answering "this profile carries no cues" and
 * kept the 20 Hz watch — reinstating, for the animation the designer is actively authoring, exactly
 * the silent miss the every-frame rate exists to prevent.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCueDetectionInPlaceAuthoringTest,
	"Paper2DPlus.FrameCues.Detection.InPlaceCueAuthoringRaisesTheWatchRate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCueDetectionInPlaceAuthoringTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusCueDetectionTest;

	FCueDetect_ScopedWorld ScopedWorld;
	if (!ScopedWorld.IsUsable())
	{
		CueDetect_MarkSkipped(*this, TEXT("In-place cue authoring"),
			TEXT("a transient game world could not begin play in this session."));
		return true;
	}

	UPaperFlipbook* Idle = CueDetect_MakeFlipbook(4, 10.0f);
	UPaperFlipbook* Attack = CueDetect_MakeFlipbook(2, 100.0f);

	// Starts genuinely cue-free, so the component is entitled to the cheap watch.
	UPaper2DPlusCharacterProfileAsset* Profile = NewObject<UPaper2DPlusCharacterProfileAsset>();
	CueDetect_AddAnimation(*Profile, TEXT("Idle"), Idle, {});
	CueDetect_AddAnimation(*Profile, TEXT("Attack"), Attack, {});

	FCueDetect_Rig Rig = CueDetect_MakeRig(
		*ScopedWorld.World, Profile, Idle,
		/*bEventDrivenComponent=*/false, /*bLooping=*/true, /*bLegacySlowPollPolicy=*/false);

	TestEqual(
		TEXT("A cue-free profile keeps the cheap 20 Hz watch"),
		Rig.ProfileComponent->PrimaryComponentTick.TickInterval,
		0.05f);
	const uint32 RevisionBefore = Profile->GetEditorContentRevision();

	// Author a cue the way the Frame Cues timeline does: open the edit through Modify(), then mutate
	// the placement array in place. The profile pointer and the animation count are both unchanged.
	UPaper2DPlusTestMomentCue* AuthoredCue = NewObject<UPaper2DPlusTestMomentCue>();
	AuthoredCue->TriggerFrame = 0;
	Profile->Modify();
	Profile->Flipbooks[1].FrameEventData.FrameCues.Add(AuthoredCue);

	TestTrue(
		TEXT("An in-place edit advances the asset's editor content revision"),
		Profile->GetEditorContentRevision() != RevisionBefore);

	// Re-trigger the move, which is what a designer does next. On the STOCK path nothing notifies the
	// profile component directly — it must poll once to observe the change, and HandleFlipbookChanged
	// owns the one UpdateTickState call from there.
	Rig.FlipbookComponent->SetFlipbook(Attack);
	CueDetect_AdvanceOneFrame(Rig, 0.060f);

	TestEqual(
		TEXT("The newly authored cue raises the watch to every frame"),
		Rig.ProfileComponent->PrimaryComponentTick.TickInterval,
		0.0f);

	// And the rate must still be earned: removing the cue again returns the profile to the cheap
	// watch, so the revision key cannot decay into "once fast, always fast".
	Profile->Modify();
	Profile->Flipbooks[1].FrameEventData.FrameCues.Reset();
	Rig.FlipbookComponent->SetFlipbook(Idle);
	CueDetect_AdvanceOneFrame(Rig, 0.060f);

	TestEqual(
		TEXT("Removing the last cue returns the component to the cheap watch"),
		Rig.ProfileComponent->PrimaryComponentTick.TickInterval,
		0.05f);

	return true;
}
#endif // WITH_EDITOR

#endif // WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
