// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Paper2DPlusBlueprintLibrary.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Paper2DPlusCharacterProfileComponent.h"
#include "Paper2DPlusTestFrameCueTypes.h"
#include "PaperFlipbook.h"
#include "PaperFlipbookComponent.h"
#if WITH_EDITOR
#include "Engine/Texture2D.h"
#include "PaperSprite.h"
#include "UObject/UnrealType.h"
#endif
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

namespace Paper2DPlusFrameCueRuntimeWorldTest
{
	/** Countable skip token -- see Paper2DPlusTestSkip.h in the editor module for the rationale. */
	void MarkSkipped(FAutomationTestBase& Test, const FString& What, const FString& Why)
	{
		Test.AddInfo(FString::Printf(TEXT("[P2DP-SKIPPED] %s -- %s"), *What, *Why));
	}

	/**
	 * A transient game world -- the only real-world rig in the Frame Cue suite.
	 *
	 * The other rigs deliberately drive notification funnels on unregistered components. This one
	 * spawns actors into an initialized world so registered components complete initialization.
	 * BeginPlay is deliberately never run: no dispatch path requires it, and requiring it here would
	 * hide a regression that only occurs before BeginPlay. Teardown mirrors the appearance harness.
	 */
	struct FRuntimeScopedWorld
	{
		FRuntimeScopedWorld()
		{
			if (!GEngine)
			{
				return;
			}

			const FName WorldName =
				MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("P2DPFrameCueRuntimeWorld"));
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
		}

		~FRuntimeScopedWorld()
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

		FRuntimeScopedWorld(const FRuntimeScopedWorld&) = delete;
		FRuntimeScopedWorld& operator=(const FRuntimeScopedWorld&) = delete;

		UWorld* World = nullptr;
	};

	/** One spawned character in a world, dispatching the supplied placements from one animation. */
	struct FWorldCharacter
	{
		TObjectPtr<AActor> Actor = nullptr;
		TObjectPtr<UPaperFlipbook> Flipbook = nullptr;
		TObjectPtr<UPaper2DPlusCharacterProfileAsset> Profile = nullptr;
		TObjectPtr<UPaperFlipbookComponent> FlipbookComponent = nullptr;
		TObjectPtr<UPaper2DPlusCharacterProfileComponent> ProfileComponent = nullptr;
		TObjectPtr<UPaper2DPlusFrameCueRecorder> Recorder = nullptr;
	};

	FWorldCharacter MakeWorldCharacter(
		UWorld& World,
		const TArray<UPaper2DPlusCueBase*>& Cues)
	{
		FWorldCharacter Character;
		Character.Actor = World.SpawnActor<AActor>();
		Character.Flipbook = NewObject<UPaperFlipbook>();
		Character.Profile = NewObject<UPaper2DPlusCharacterProfileAsset>();

		FFlipbookProfileEntry Entry;
		Entry.Identity.FlipbookName = TEXT("Attack");
		Entry.Identity.Flipbook = Character.Flipbook;
		for (UPaper2DPlusCueBase* Cue : Cues)
		{
			Entry.FrameEventData.FrameCues.Add(Cue);
		}
		Character.Profile->Flipbooks.Add(MoveTemp(Entry));

		// Unlike the worldless rigs, this sprite is registered on a spawned actor.
		Character.FlipbookComponent = NewObject<UPaperFlipbookComponent>(Character.Actor);
		Character.FlipbookComponent->SetFlipbook(Character.Flipbook);
		Character.Actor->SetRootComponent(Character.FlipbookComponent);
		Character.FlipbookComponent->RegisterComponent();

		// Deliberately not registered, and BeginPlay is never run. The suite drives the notification
		// funnels directly; the world's contribution is the spawned actor and registered sprite.
		Character.ProfileComponent =
			NewObject<UPaper2DPlusCharacterProfileComponent>(Character.Actor);
		Character.ProfileComponent->CharacterProfile = Character.Profile;
		Character.ProfileComponent->FlipbookComponent = Character.FlipbookComponent;
		Character.Recorder = NewObject<UPaper2DPlusFrameCueRecorder>();
		Character.ProfileComponent->OnFrameCue.AddDynamic(
			Character.Recorder.Get(), &UPaper2DPlusFrameCueRecorder::OnCue);
		Character.ProfileComponent->HandleFlipbookChanged(Character.Flipbook);
		return Character;
	}

	int32 CountActors(UWorld& World)
	{
		int32 Count = 0;
		for (TActorIterator<AActor> It(&World); It; ++It)
		{
			++Count;
		}
		return Count;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCueSpawnedWorldActorDispatchTest,
	"Paper2DPlus.FrameCues.Runtime.DispatchReachesASpawnedWorldActor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCueSpawnedWorldActorDispatchTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusFrameCueRuntimeWorldTest;
	Paper2DPlusBehaviorTestLog::Reset();

	// Every other Frame Cue rig is worldless. This proves the same contract on a spawned actor whose
	// sprite component completed registration, so a registration-only regression cannot hide.
	FRuntimeScopedWorld Scoped;
	if (!Scoped.World)
	{
		MarkSkipped(
			*this,
			TEXT("Frame Cue dispatch on a spawned world actor"),
			TEXT("no engine world could be created in this host"));
		return true;
	}

	UPaper2DPlusTestBehaviorMomentCue* const Cue =
		NewObject<UPaper2DPlusTestBehaviorMomentCue>();
	Cue->TriggerFrame = 0;
	Cue->Payload = 17;

	const int32 ActorsBefore = CountActors(*Scoped.World);
	FWorldCharacter Character = MakeWorldCharacter(*Scoped.World, {Cue});
	TestEqual(TEXT("The rig spawned exactly one actor into the world"),
		CountActors(*Scoped.World), ActorsBefore + 1);

	Character.ProfileComponent->HandleFrameChanged(0);

	TestEqual(TEXT("Behavior ran exactly once on the spawned actor"),
		Paper2DPlusBehaviorTestLog::Records().Num(), 1);
	if (Paper2DPlusBehaviorTestLog::Records().Num() == 1)
	{
		const Paper2DPlusBehaviorTestLog::FRecord& Record =
			Paper2DPlusBehaviorTestLog::Records()[0];
		TestTrue(TEXT("Behavior was attributed to the spawned actor's own profile component"),
			Record.ProfileComponent == Character.ProfileComponent.Get());
		TestTrue(TEXT("Cue self resolves the component's gameplay world during behavior"),
			Record.CueWorld == Scoped.World);
		TestEqual(TEXT("Behavior read the placement's authored payload"), Record.Payload, 17);
		TestFalse(TEXT("A real world is not an editor preview"), Record.bIsEditorPreview);
	}
	TestNull(TEXT("The shared placement is worldless again after gameplay dispatch"),
		Cue->GetWorld());
	TestEqual(TEXT("The listener broadcast reached the spawned actor once"),
		Character.Recorder->Contexts.Num(), 1);
	if (Character.Recorder->Contexts.Num() == 1)
	{
		TestTrue(TEXT("The broadcast context names the spawned actor"),
			Character.Recorder->Contexts[0].OwningActor == Character.Actor.Get());
		TestTrue(TEXT("The broadcast context names the registered sprite"),
			Character.ProfileComponent->GetResolvedFlipbookComponent()
				== Character.FlipbookComponent.Get());
	}

	// A second character sharing the asset-owned placement gets its own dispatch and takes nothing
	// from the first: the statelessness contract, now exercised in a real world.
	FWorldCharacter Second = MakeWorldCharacter(*Scoped.World, {});
	Second.Profile->Flipbooks[0].FrameEventData.FrameCues =
		Character.Profile->Flipbooks[0].FrameEventData.FrameCues;
	Second.ProfileComponent->HandleFlipbookChanged(Second.Flipbook);
	Second.ProfileComponent->HandleFrameChanged(0);
	TestEqual(TEXT("The shared placement runs again for the second spawned character"),
		Paper2DPlusBehaviorTestLog::Records().Num(), 2);
	if (Paper2DPlusBehaviorTestLog::Records().Num() == 2)
	{
		TestTrue(TEXT("Re-entrant use resolves the second component's world only for its callback"),
			Paper2DPlusBehaviorTestLog::Records()[1].CueWorld == Scoped.World);
	}
	TestEqual(TEXT("The first character gains nothing from the second character's dispatch"),
		Character.Recorder->Contexts.Num(), 1);
	TestEqual(TEXT("The second character's listener saw its own dispatch"),
		Second.Recorder->Contexts.Num(), 1);
	TestEqual(TEXT("Behavior left the shared placement's authored payload untouched"),
		Cue->Payload, 17);
	TestNull(TEXT("The shared placement stays worldless after the second dispatch"),
		Cue->GetWorld());
	return true;
}

#if WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCueSpatialAnchorTest,
	"Paper2DPlus.FrameCues.Context.SpatialAnchorSnapshot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCueSpatialAnchorTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusFrameCueRuntimeWorldTest;

	FRuntimeScopedWorld Scoped;
	if (!Scoped.World)
	{
		MarkSkipped(
			*this,
			TEXT("Frame Cue spatial anchor resolution"),
			TEXT("no engine world could be created in this host"));
		return true;
	}

	FWorldCharacter Character = MakeWorldCharacter(*Scoped.World, {});
	UTexture2D* const Texture = UTexture2D::CreateTransient(64, 64, PF_B8G8R8A8);
	UPaperSprite* const Sprite = NewObject<UPaperSprite>();
	if (!TestNotNull(TEXT("Anchor texture"), Texture)
		|| !TestNotNull(TEXT("Anchor sprite"), Sprite))
	{
		return false;
	}

	FSpriteAssetInitParameters InitParams;
	InitParams.Texture = Texture;
	InitParams.Offset = FIntPoint::ZeroValue;
	InitParams.Dimension = FIntPoint(64, 64);
	InitParams.SetPixelsPerUnrealUnit(1.0f);
	Sprite->InitializeSprite(InitParams);
	if (FBoolProperty* const SnapProperty = FindFProperty<FBoolProperty>(
		UPaperSprite::StaticClass(),
		TEXT("bSnapPivotToPixelGrid")))
	{
		SnapProperty->SetPropertyValue_InContainer(Sprite, false);
	}
	Sprite->SetPivotMode(
		ESpritePivotMode::Custom,
		FVector2D(31.25f, 29.75f),
		/*bSnapToPixelGrid=*/false);

	{
		FScopedFlipbookMutator Mutator(Character.Flipbook);
		Mutator.FramesPerSecond = 10.0f;
		Mutator.KeyFrames.Empty();
		FPaperFlipbookKeyFrame KeyFrame;
		KeyFrame.Sprite = Sprite;
		KeyFrame.FrameRun = 1;
		Mutator.KeyFrames.Add(KeyFrame);
	}

	FSocketData WeaponSocket;
	WeaponSocket.Name = TEXT("WeaponHand");
	WeaponSocket.X = 43;
	WeaponSocket.Y = 17;
	FFrameHitboxData Frame;
	Frame.Sockets.Add(WeaponSocket);
	Character.Profile->Flipbooks[0].CombatData.Frames.Add(Frame);

	const FTransform ComponentTransform(
		FRotator(0.0f, 180.0f, 0.0f),
		FVector(120.0f, 7.0f, 55.0f),
		FVector(2.0f, 1.5f, 3.0f));
	Character.FlipbookComponent->SetWorldTransform(ComponentTransform);

	FPaper2DPlusFrameCueContext Context;
	Context.OwningActor = Character.Actor;
	Context.ProfileComponent = Character.ProfileComponent;
	Context.Flipbook = Character.Flipbook;
	Context.AnimationName = TEXT("Attack");
	Context.CurrentFrame = 0;
	Context.CharacterProfile = Character.Profile;
	Context.PlaybackComponent = Character.FlipbookComponent;
	Context.SetEvaluationMode(EPaper2DPlusFrameCueEvaluationMode::RuntimePlayback);

	FTransform ResolvedTransform = FTransform::Identity;
	UPaperFlipbookComponent* AttachmentComponent = nullptr;
	EPaper2DPlusFrameCueAnchorResult Result =
		UPaper2DPlusBlueprintLibrary::ResolveFrameCueAnchor(
			Context,
			EPaper2DPlusFrameCueAnchorKind::RenderOrigin,
			FString(),
			ResolvedTransform,
			AttachmentComponent);
	TestEqual(TEXT("Render Origin resolves"), Result,
		EPaper2DPlusFrameCueAnchorResult::Success);
	TestTrue(TEXT("Render Origin is the exact live component transform"),
		ResolvedTransform.Equals(Character.FlipbookComponent->GetComponentTransform()));
	TestTrue(TEXT("Render Origin returns the live attachment component"),
		AttachmentComponent == Character.FlipbookComponent.Get());

	Result = UPaper2DPlusBlueprintLibrary::ResolveFrameCueAnchor(
		Context,
		EPaper2DPlusFrameCueAnchorKind::ProfileSocket,
		TEXT("weaponhand"),
		ResolvedTransform,
		AttachmentComponent);
	TestEqual(TEXT("Profile Socket lookup is case-insensitive"), Result,
		EPaper2DPlusFrameCueAnchorResult::Success);
	TestTrue(TEXT("Profile Socket preserves the component rotation"),
		ResolvedTransform.GetRotation().Equals(
			Character.FlipbookComponent->GetComponentQuat(),
			KINDA_SMALL_NUMBER));
	TestTrue(TEXT("Profile Socket preserves the component's signed scale"),
		ResolvedTransform.GetScale3D().Equals(
			Character.FlipbookComponent->GetComponentScale(),
			KINDA_SMALL_NUMBER));

	const FVector2D PivotLocal = Sprite->GetPivotPosition() - Sprite->GetSourceUV();
	TestFalse(TEXT("The fixture exercises fractional pivot compensation"),
		FMath::IsNearlyZero(FMath::Frac(PivotLocal.X))
			&& FMath::IsNearlyZero(FMath::Frac(PivotLocal.Y)));
	const int32 PivotX = FMath::FloorToInt(PivotLocal.X);
	const int32 PivotY = FMath::FloorToInt(PivotLocal.Y);
	const FVector2D PivotFraction(
		PivotLocal.X - static_cast<float>(PivotX),
		PivotLocal.Y - static_cast<float>(PivotY));
	FVector ExpectedSocketLocation = Character.FlipbookComponent->GetComponentLocation();
	ExpectedSocketLocation.X += PivotFraction.X * 2.0f;
	ExpectedSocketLocation.Z += PivotFraction.Y * 3.0f;
	ExpectedSocketLocation.X -= static_cast<float>(WeaponSocket.X - PivotX) * 2.0f;
	ExpectedSocketLocation.Z += static_cast<float>(PivotY - WeaponSocket.Y) * 3.0f;
	TestTrue(TEXT("Profile Socket uses canonical pivot, facing, and nonuniform scale"),
		ResolvedTransform.GetLocation().Equals(ExpectedSocketLocation, 0.01f));

	// Spatial resolution consumes the invocation snapshot, not mutable playback state.
	Character.FlipbookComponent->SetFlipbook(NewObject<UPaperFlipbook>());
	Result = UPaper2DPlusBlueprintLibrary::ResolveFrameCueAnchor(
		Context,
		EPaper2DPlusFrameCueAnchorKind::ProfileSocket,
		TEXT("WeaponHand"),
		ResolvedTransform,
		AttachmentComponent);
	TestEqual(TEXT("An outgoing snapshot ignores the component's newly assigned flipbook"), Result,
		EPaper2DPlusFrameCueAnchorResult::Success);

	Context.CurrentFrame = INDEX_NONE;
	Context.PreviousFrame = 0;
	Context.Phase = EPaper2DPlusFrameCuePhase::End;
	Result = UPaper2DPlusBlueprintLibrary::ResolveFrameCueAnchor(
		Context,
		EPaper2DPlusFrameCueAnchorKind::ProfileSocket,
		TEXT("WeaponHand"),
		ResolvedTransform,
		AttachmentComponent);
	TestEqual(TEXT("A forced End may resolve its last usable source frame"), Result,
		EPaper2DPlusFrameCueAnchorResult::Success);

	Context.Phase = EPaper2DPlusFrameCuePhase::Trigger;
	Result = UPaper2DPlusBlueprintLibrary::ResolveFrameCueAnchor(
		Context,
		EPaper2DPlusFrameCueAnchorKind::ProfileSocket,
		TEXT("WeaponHand"),
		ResolvedTransform,
		AttachmentComponent);
	TestEqual(TEXT("A non-End never hides an invalid current frame"), Result,
		EPaper2DPlusFrameCueAnchorResult::FrameOutOfRange);

	Context.CurrentFrame = 0;
	FSocketData DuplicateSocket;
	DuplicateSocket.Name = TEXT("weaponHAND");
	DuplicateSocket.X = 1;
	DuplicateSocket.Y = 2;
	Character.Profile->Flipbooks[0].CombatData.Frames[0].Sockets.Add(DuplicateSocket);
	ResolvedTransform = FTransform(
		FRotator(4.0f, 5.0f, 6.0f),
		FVector(1.0f, 2.0f, 3.0f),
		FVector(2.0f));
	AttachmentComponent = Character.FlipbookComponent;
	Result = UPaper2DPlusBlueprintLibrary::ResolveFrameCueAnchor(
		Context,
		EPaper2DPlusFrameCueAnchorKind::ProfileSocket,
		TEXT("WeaponHand"),
		ResolvedTransform,
		AttachmentComponent);
	TestEqual(TEXT("Duplicate-by-case socket names fail closed"), Result,
		EPaper2DPlusFrameCueAnchorResult::SocketAmbiguous);
	TestTrue(TEXT("A failed resolution resets the transform"),
		ResolvedTransform.Equals(FTransform::Identity));
	TestNull(TEXT("A failed resolution clears the attachment component"), AttachmentComponent);

	Character.Profile->Flipbooks[0].CombatData.Frames[0].Sockets.SetNum(1);
	const FFlipbookProfileEntry DuplicateEntry = Character.Profile->Flipbooks[0];
	Character.Profile->Flipbooks.Add(DuplicateEntry);
	Result = UPaper2DPlusBlueprintLibrary::ResolveFrameCueAnchor(
		Context,
		EPaper2DPlusFrameCueAnchorKind::ProfileSocket,
		TEXT("WeaponHand"),
		ResolvedTransform,
		AttachmentComponent);
	TestEqual(TEXT("Duplicate exact Profile identities fail closed"), Result,
		EPaper2DPlusFrameCueAnchorResult::ProfileRowAmbiguous);

	return true;
}
#endif // WITH_EDITOR

#endif // WITH_DEV_AUTOMATION_TESTS
