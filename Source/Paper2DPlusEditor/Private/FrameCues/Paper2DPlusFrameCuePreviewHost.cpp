// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "FrameCues/Paper2DPlusFrameCuePreviewHost.h"
#include "FrameCues/Paper2DPlusFrameCueEditorSettings.h"
#include "FrameCues/Paper2DPlusFrameCuePreviewAdapter.h"
#include "FrameCues/Paper2DPlusFrameCuePreviewContext.h"
#include "FrameCues/Paper2DPlusFrameCue.h"
#include "AnimationTimeline.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Paper2DPlusCharacterProfileComponent.h"
#include "Paper2DPlusLayerDraw.h"
#include "PaperFlipbook.h"
#include "PaperFlipbookComponent.h"
#include "PaperSprite.h"
#include "PaperSpriteComponent.h"
#include "AudioDevice.h"
#include "AudioDeviceManager.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/LineBatchComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkyLightComponent.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/Engine.h"
#include "Engine/CollisionProfile.h"
#include "Engine/EngineTypes.h"
#include "Engine/World.h"
#include "EngineGlobals.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/ScopeExit.h"
#include "PreviewScene.h"
#include "UObject/Package.h"
#include "UObject/UObjectHash.h"

namespace
{
/**
 * FPreviewScene owns a suitable editor render scene, but its destructor forces a full-purge GC in
 * supported engine versions. Timeline seeks need a fresh UWorld for timers, latent work, physics and
 * WorldSettings without destroying the stable viewport scene owner (or turning scrubbing into a GC
 * storm), so this small subclass replaces only the protected world and leaves ordinary final
 * FPreviewScene destruction to editor close.
 *
 * Default lighting is managed here rather than by the base constructor. That keeps the base's private
 * component list empty between world generations and lets every light/line-batcher move with the
 * replacement world.
 */
class FPaper2DPlusFrameCuePreviewScene final : public FPreviewScene
{
public:
	FPaper2DPlusFrameCuePreviewScene()
		: FPreviewScene(MakeConstructionValues())
	{
		CreateManagedLighting();
	}

	void ReplaceWorldWithoutForcedGarbageCollection()
	{
		RemoveManagedLighting();
		ReleaseCurrentWorld();
		CreateWorld();
		CreateManagedLighting();
	}

	void ReleaseWorldWithoutForcedGarbageCollection()
	{
		RemoveManagedLighting();
		ReleaseCurrentWorld();
	}

	void GetManagedComponents(TArray<UActorComponent*>& OutComponents) const
	{
		OutComponents.Add(DirectionalLight);
		OutComponents.Add(SkyLight);
		OutComponents.Add(LineBatcher);
	}

private:
	static FPreviewScene::ConstructionValues MakeConstructionValues()
	{
		FPreviewScene::ConstructionValues Values;
		Values
			.SetCreateDefaultLighting(false)
			.AllowAudioPlayback(true)
			.SetForceMipsResident(false)
			.SetCreatePhysicsScene(true)
			.SetTransactional(false)
			.SetEditor(true)
			.ForceUseMovementComponentInNonGameWorld(true);
		return Values;
	}

	void CreateWorld()
	{
		check(!PreviewWorld);
		PreviewWorld = NewObject<UWorld>(
			GetTransientPackage(), NAME_None, RF_NoFlags);
		PreviewWorld->WorldType = EWorldType::EditorPreview;

		FWorldContext& WorldContext =
			GEngine->CreateNewWorldContext(PreviewWorld->WorldType);
		WorldContext.SetCurrentWorld(PreviewWorld);

		UWorld::InitializationValues Values;
		Values
			.AllowAudioPlayback(true)
			.CreatePhysicsScene(true)
			.RequiresHitProxies(true)
			.CreateNavigation(false)
			.CreateAISystem(false)
			.ShouldSimulatePhysics(false)
			.SetTransactional(false)
			.SetDefaultGameMode(nullptr)
			.ForceUseMovementComponentInNonGameWorld(true);
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 8
		Values.AllowLumenPrimitiveTrackingInPreviewWorld(false);
#endif
		PreviewWorld->InitializeNewWorld(Values);
		PreviewWorld->InitializeActorsForPlay(FURL());
	}

	void ReleaseCurrentWorld()
	{
		UWorld* OldWorld = PreviewWorld;
		PreviewWorld = nullptr;
		if (!OldWorld || !GEngine)
		{
			return;
		}
		// Keep the host's unique device attached through CleanupWorld so cleanup callbacks cannot fall
		// back to the main editor device. Never call GetAudioDevice after the detach below: it would
		// resolve that fallback and could mute unrelated worldless auditions.
		OldWorld->CleanupWorld();
		OldWorld->SetAudioDevice(FAudioDeviceHandle());
		GEngine->DestroyWorldContext(OldWorld);
		OldWorld->ReleasePhysicsScene();
		// Invalidate the retired object graph immediately so external weak references cannot observe
		// actors from an already-reset preview. Deliberately do not collect here: normal editor GC can
		// reclaim the garbage without turning timeline seeks into full-purge hitches.
		OldWorld->MarkObjectsPendingKill();
	}

	void CreateManagedLighting()
	{
		DirectionalLight = NewObject<UDirectionalLightComponent>(
			GetTransientPackage(), NAME_None, RF_Transient);
		DirectionalLight->Intensity = PI;
		DirectionalLight->LightColor = FColor::White;
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 8
		DirectionalLight->bTransmission = true;
#endif
		AddComponent(DirectionalLight, FTransform(FRotator(-40.0f, -67.5f, 0.0f)));

		SkyLight = NewObject<USkyLightComponent>(
			GetTransientPackage(), NAME_None, RF_Transient);
		SkyLight->bLowerHemisphereIsBlack = false;
		SkyLight->SourceType = ESkyLightSourceType::SLS_SpecifiedCubemap;
		SkyLight->Intensity = 1.0f;
		SkyLight->Mobility = EComponentMobility::Movable;
		AddComponent(SkyLight, FTransform::Identity);

		LineBatcher = NewObject<ULineBatchComponent>(
			GetTransientPackage(), NAME_None, RF_Transient);
		LineBatcher->bCalculateAccurateBounds = false;
		AddComponent(LineBatcher, FTransform::Identity);
	}

	void RemoveManagedLighting()
	{
		if (DirectionalLight)
		{
			RemoveComponent(DirectionalLight);
			DirectionalLight = nullptr;
		}
		if (SkyLight)
		{
			RemoveComponent(SkyLight);
			SkyLight = nullptr;
		}
		if (LineBatcher)
		{
			RemoveComponent(LineBatcher);
			LineBatcher = nullptr;
		}
	}
};
}

FPaper2DPlusFrameCuePreviewHost::FPaper2DPlusFrameCuePreviewHost()
	: Context(TStrongObjectPtr<UPaper2DPlusFrameCuePreviewContext>(
		NewObject<UPaper2DPlusFrameCuePreviewContext>(GetTransientPackage(), NAME_None, RF_Transient)))
{
	CreatePreviewScene();
	RebuildAdapters();
	// Subscribe once for the host lifetime. RebuildAdapters deliberately never binds delegates, so
	// registry refreshes and Blueprint compile reconstruction cannot accumulate callbacks.
	PreviewAdaptersChangedHandle =
		UPaper2DPlusFrameCueEditorSettings::OnPreviewAdaptersChanged().AddRaw(
			this, &FPaper2DPlusFrameCuePreviewHost::HandlePreviewAdaptersChanged);
	if (GEditor)
	{
		BlueprintPreCompileHandle = GEditor->OnBlueprintPreCompile().AddRaw(
			this, &FPaper2DPlusFrameCuePreviewHost::HandleBlueprintPreCompile);
		BlueprintCompiledHandle = GEditor->OnBlueprintCompiled().AddRaw(
			this, &FPaper2DPlusFrameCuePreviewHost::HandleBlueprintCompiled);
	}
}

void FPaper2DPlusFrameCuePreviewHost::RebuildAdapters()
{
	Adapters.Reset();
	RegistryProblems.Reset();
	// Adapter ORDER is the whole contract here, and it stays deterministic and independent of
	// source-file order: the plugin's own adapters would be seeded first, then the project registry in
	// authored order. The plugin seeds NONE of its own — it ships cue machinery, not a cue catalogue —
	// so slot 0 is the FIRST project adapter and precedence is exactly the order the project authored
	// in Project Settings. This assert is the marker for where seeding goes if that ever changes, so
	// that adding a plugin-owned adapter cannot silently renumber project slots and their ledger tokens.
	static_assert(
		NativeAdapterCount == 0,
		"NativeAdapterCount changed: seed the plugin's own adapters at this point, ahead of the project "
		"registry, and re-check every NativeAdapterCount + N slot calculation (resource ledger owner "
		"tokens included) before removing this assert.");

	const UPaper2DPlusFrameCueEditorSettings* Settings = GetDefault<UPaper2DPlusFrameCueEditorSettings>();
	for (int32 Index = 0; Index < Settings->PreviewAdapters.Num(); ++Index)
	{
		const TSoftClassPtr<UPaper2DPlusFrameCuePreviewAdapter>& AdapterClass = Settings->PreviewAdapters[Index];
		UClass* LoadedClass = AdapterClass.LoadSynchronous();
		if (!LoadedClass)
		{
			RegistryProblems.Add(FString::Printf(
				TEXT("Adapter %d could not load: %s"), Index + 1, *AdapterClass.ToString()));
			continue;
		}
		const int32 BeforeCount = Adapters.Num();
		AddAdapterClass(LoadedClass);
		if (Adapters.Num() == BeforeCount)
		{
			RegistryProblems.Add(FString::Printf(
				TEXT("Adapter %d is invalid or duplicated: %s"), Index + 1, *LoadedClass->GetPathName()));
		}
	}
}

FPaper2DPlusFrameCuePreviewHost::~FPaper2DPlusFrameCuePreviewHost()
{
	bDestroyingPreviewHost = true;
	if (PreviewAdaptersChangedHandle.IsValid())
	{
		UPaper2DPlusFrameCueEditorSettings::OnPreviewAdaptersChanged().Remove(
			PreviewAdaptersChangedHandle);
		PreviewAdaptersChangedHandle.Reset();
	}
	if (GEditor)
	{
		if (BlueprintPreCompileHandle.IsValid())
		{
			GEditor->OnBlueprintPreCompile().Remove(BlueprintPreCompileHandle);
			BlueprintPreCompileHandle.Reset();
		}
		if (BlueprintCompiledHandle.IsValid())
		{
			GEditor->OnBlueprintCompiled().Remove(BlueprintCompiledHandle);
			BlueprintCompiledHandle.Reset();
		}
	}
	Reset();
	DestroyPreviewScene();
}

void FPaper2DPlusFrameCuePreviewHost::CreatePreviewScene()
{
	if (PreviewScene)
	{
		return;
	}

	PreviewScene = MakeUnique<FPaper2DPlusFrameCuePreviewScene>();
	ConfigurePreviewWorld();
	if (PreviewSceneChanged)
	{
		PreviewSceneChanged(PreviewScene.Get());
	}
}

void FPaper2DPlusFrameCuePreviewHost::ConfigurePreviewWorld()
{
	UWorld* World = GetPreviewWorld();
	if (!World)
	{
		return;
	}
	++PreviewWorldRevision;
	if (Context)
	{
		Context->SetPreviewWorld(World);
	}
	if (GEngine)
	{
		if (FAudioDeviceManager* AudioDeviceManager = GEngine->GetAudioDeviceManager())
		{
			FAudioDeviceParams AudioParams =
				AudioDeviceManager->GetDefaultParamsForNewWorld();
			AudioParams.AssociatedWorld = World;
			AudioParams.Scope = EAudioDeviceScope::Unique;
			PreviewAudioDevice = AudioDeviceManager->RequestAudioDevice(AudioParams);
			if (PreviewAudioDevice.IsValid())
			{
				World->SetAudioDevice(PreviewAudioDevice);
			}
		}
	}
	World->bAllowAudioPlayback = true;
	if (AWorldSettings* WorldSettings = World->GetWorldSettings(true))
	{
		// Unattached one-shot effects commonly use an owning actor outside the tiny default bounds.
		WorldSettings->bEnableWorldBoundsChecks = false;
		WorldSettings->SetIsTemporarilyHiddenInEditor(false);
	}

	// Capture every component created by InitializeNewWorld/FPreviewScene before adding the host
	// subject. This includes ownerless engine infrastructure such as the physics-field component;
	// treating it as Cue output would keep an otherwise empty stopped preview artificially active.
	CaptureBaselineComponents();
	ActorSpawnedHandle = World->AddOnActorSpawnedHandler(
		FOnActorSpawned::FDelegate::CreateRaw(
			this, &FPaper2DPlusFrameCuePreviewHost::HandleActorSpawned));
	CreatePreviewSubject();
}

void FPaper2DPlusFrameCuePreviewHost::ReplacePreviewWorld()
{
	if (!PreviewScene)
	{
		CreatePreviewScene();
		return;
	}

	UWorld* OldWorld = GetPreviewWorld();
	if (OldWorld)
	{
		if (PreviewAudioDevice.IsValid())
		{
			if (FAudioDevice* AudioDevice = PreviewAudioDevice.GetAudioDevice())
			{
				AudioDevice->Flush(OldWorld, /*bClearActivatedReverb=*/false);
			}
		}
		if (ActorSpawnedHandle.IsValid())
		{
			OldWorld->RemoveOnActorSpawnedHandler(ActorSpawnedHandle);
			ActorSpawnedHandle.Reset();
		}
	}

	SpawnedActors.Reset();
	BaselineComponents.Reset();
	TrackedWorldComponents.Reset();
	ProfileComponent.Reset();
	FlipbookComponent.Reset();
	LayerSpriteComponents.Reset();
	AdapterEffectComponents.Reset();
	PreviewRootComponent.Reset();
	PreviewActor.Reset();
	PreviewAudioDevice.Reset();

	static_cast<FPaper2DPlusFrameCuePreviewScene*>(PreviewScene.Get())
		->ReplaceWorldWithoutForcedGarbageCollection();
	ConfigurePreviewWorld();
	if (PreviewSceneChanged)
	{
		// The owner pointer remains stable; publish it again so the viewport invalidates against the
		// new underlying render scene without discarding camera focus/pan state.
		PreviewSceneChanged(PreviewScene.Get());
	}
}

void FPaper2DPlusFrameCuePreviewHost::CreatePreviewSubject()
{
	UWorld* World = GetPreviewWorld();
	if (!World)
	{
		return;
	}

	bSpawningPreviewSubject = true;
	ON_SCOPE_EXIT { bSpawningPreviewSubject = false; };
	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Name = MakeUniqueObjectName(
		World, AActor::StaticClass(), TEXT("Paper2DPlusFrameCuePreviewSubject"));
	SpawnParameters.ObjectFlags |= RF_Transient;
	SpawnParameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AActor* Subject = World->SpawnActor<AActor>(SpawnParameters);
	if (!Subject)
	{
		return;
	}
	PreviewActor = Subject;
	++PreviewSubjectRevision;
	Subject->SetActorTickEnabled(false);
	Subject->SetActorHiddenInGame(false);
	Subject->SetIsTemporarilyHiddenInEditor(false);
	Subject->SetActorEnableCollision(true);

	USceneComponent* Root = NewObject<USceneComponent>(
		Subject, TEXT("PreviewRoot"), RF_Transient);
	Subject->AddInstanceComponent(Root);
	Subject->SetRootComponent(Root);
	Root->RegisterComponent();
	PreviewRootComponent = Root;
	RegisterBaselineComponent(Root);

	UPaperFlipbookComponent* RenderComponent = NewObject<UPaperFlipbookComponent>(
		Subject, TEXT("PreviewFlipbook"), RF_Transient);
	Subject->AddInstanceComponent(RenderComponent);
	RenderComponent->SetupAttachment(Root);
	// Match PaperZD's preview precedent: ordinary trace nodes see the authored flipbook collision
	// exactly as they would on a game actor.
	RenderComponent->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	RenderComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	RenderComponent->SetLooping(false);
	RenderComponent->SetAutoActivate(false);
	RenderComponent->RegisterComponent();
	RenderComponent->Stop();
	FlipbookComponent = RenderComponent;
	RegisterBaselineComponent(RenderComponent);

	UPaper2DPlusCharacterProfileComponent* SubjectProfile =
		NewObject<UPaper2DPlusCharacterProfileComponent>(
			Subject, TEXT("PreviewProfile"), RF_Transient);
	Subject->AddInstanceComponent(SubjectProfile);
	SubjectProfile->bRegisterWithHitboxSubsystem = false;
	SubjectProfile->bAutoHitDetection = false;
	SubjectProfile->bAutoApplyRootMotion = false;
	SubjectProfile->SetAutoActivate(false);
	SubjectProfile->RegisterComponent();
	SubjectProfile->SetComponentTickEnabled(false);
	SubjectProfile->FlipbookComponent = RenderComponent;
	ProfileComponent = SubjectProfile;
	RegisterBaselineComponent(SubjectProfile);
	bSubjectStateDirty = true;
}

bool FPaper2DPlusFrameCuePreviewHost::EnsurePreviewSubject()
{
	AActor* Subject = PreviewActor.Get();
	USceneComponent* Root = PreviewRootComponent.Get();
	UPaperFlipbookComponent* RenderComponent = FlipbookComponent.Get();
	UPaper2DPlusCharacterProfileComponent* SubjectProfile = ProfileComponent.Get();
	const bool bHealthy =
		IsValid(Subject)
		&& !Subject->IsActorBeingDestroyed()
		&& !Subject->IsPendingKillPending()
		&& IsValid(Root)
		&& IsValid(RenderComponent)
		&& IsValid(SubjectProfile)
		&& Subject->GetRootComponent() == Root
		&& RenderComponent->IsRegistered()
		&& SubjectProfile->IsRegistered();
	if (bHealthy)
	{
		return false;
	}

	TSet<UActorComponent*> OldSubjectComponents;
	OldSubjectComponents.Add(Root);
	OldSubjectComponents.Add(RenderComponent);
	OldSubjectComponents.Add(SubjectProfile);
	for (const TWeakObjectPtr<UPaperSpriteComponent>& WeakLayer : LayerSpriteComponents)
	{
		OldSubjectComponents.Add(WeakLayer.Get());
	}
	if (IsValid(Subject)
		&& !Subject->IsActorBeingDestroyed()
		&& !Subject->IsPendingKillPending())
	{
		if (UWorld* World = GetPreviewWorld())
		{
			World->DestroyActor(Subject, /*bNetForce=*/false, /*bShouldModifyLevel=*/false);
		}
	}
	for (auto It = BaselineComponents.CreateIterator(); It; ++It)
	{
		UActorComponent* Component = It->Get();
		if (!IsValid(Component)
			|| OldSubjectComponents.Contains(Component)
			|| (Subject && Component->GetOwner() == Subject))
		{
			It.RemoveCurrent();
		}
	}
	PreviewActor.Reset();
	PreviewRootComponent.Reset();
	FlipbookComponent.Reset();
	ProfileComponent.Reset();
	LayerSpriteComponents.Reset();
	AdapterEffectComponents.Reset();
	CreatePreviewSubject();
	bSubjectStateDirty = true;
	return IsValid(PreviewActor.Get());
}

void FPaper2DPlusFrameCuePreviewHost::DestroyPreviewScene()
{
	if (Context)
	{
		Context->SetPreviewWorld(nullptr);
	}
	if (PreviewSceneChanged)
	{
		// FEditorViewportClient stores this pointer rather than owning the scene. Detach it before
		// final FPreviewScene destruction; routine resets retain this owner and replace only its world.
		PreviewSceneChanged(nullptr);
	}
	if (UWorld* World = GetPreviewWorld())
	{
		if (PreviewAudioDevice.IsValid())
		{
			if (FAudioDevice* AudioDevice = PreviewAudioDevice.GetAudioDevice())
			{
				// This device belongs only to this host, so ordinary Blueprint world sounds can be
				// stopped without muting another Frame Cues tab or an unrelated editor audition.
				AudioDevice->Flush(World, /*bClearActivatedReverb=*/false);
			}
		}
		if (ActorSpawnedHandle.IsValid())
		{
			World->RemoveOnActorSpawnedHandler(ActorSpawnedHandle);
			ActorSpawnedHandle.Reset();
		}
	}

	if (PreviewScene)
	{
		static_cast<FPaper2DPlusFrameCuePreviewScene*>(PreviewScene.Get())
			->ReleaseWorldWithoutForcedGarbageCollection();
	}
	SpawnedActors.Reset();
	BaselineComponents.Reset();
	TrackedWorldComponents.Reset();
	ProfileComponent.Reset();
	FlipbookComponent.Reset();
	LayerSpriteComponents.Reset();
	AdapterEffectComponents.Reset();
	PreviewRootComponent.Reset();
	PreviewActor.Reset();
	PreviewScene.Reset();
	PreviewAudioDevice.Reset();
}

void FPaper2DPlusFrameCuePreviewHost::CaptureBaselineComponents()
{
	BaselineComponents.Reset();
	ForEachPreviewWorldComponent(
		[this](UActorComponent* Component)
		{
			RegisterBaselineComponent(Component);
		});
}

void FPaper2DPlusFrameCuePreviewHost::ForEachPreviewWorldComponent(
	TFunctionRef<void(UActorComponent*)> Visitor) const
{
	UWorld* World = GetPreviewWorld();
	if (!World)
	{
		return;
	}
	ForEachObjectWithOuter(
		World,
		[World, &Visitor](UObject* Object)
		{
			UActorComponent* Component = Cast<UActorComponent>(Object);
			if (IsValid(Component) && Component->GetWorld() == World)
			{
				Visitor(Component);
			}
		},
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 8)
		EGetObjectsFlags::IncludeNestedObjects);
#else
		/*bIncludeNestedObjects=*/true);
#endif

	if (PreviewScene)
	{
		TArray<UActorComponent*> ManagedComponents;
		static_cast<const FPaper2DPlusFrameCuePreviewScene*>(PreviewScene.Get())
			->GetManagedComponents(ManagedComponents);
		for (UActorComponent* Component : ManagedComponents)
		{
			if (IsValid(Component) && Component->GetWorld() == World)
			{
				Visitor(Component);
			}
		}
	}
}

void FPaper2DPlusFrameCuePreviewHost::RegisterBaselineComponent(UActorComponent* Component)
{
	if (IsValid(Component))
	{
		BaselineComponents.Add(Component);
	}
}

void FPaper2DPlusFrameCuePreviewHost::RefreshTrackedWorldResources()
{
	TrackedWorldComponents.Reset();
	ForEachPreviewWorldComponent(
		[this](UActorComponent* Component)
		{
			if (!IsBaselineComponent(Component)
				|| Component->IsA<ULineBatchComponent>())
			{
				TrackedWorldComponents.Add(Component);
			}
		});
}

void FPaper2DPlusFrameCuePreviewHost::HandleActorSpawned(AActor* Actor)
{
	if (bSpawningPreviewSubject
		|| !IsValid(Actor)
		|| Actor == PreviewActor.Get()
		|| Actor->IsA<AWorldSettings>())
	{
		return;
	}
	SpawnedActors.Add(Actor);
}

UWorld* FPaper2DPlusFrameCuePreviewHost::GetPreviewWorld() const
{
	return PreviewScene ? PreviewScene->GetWorld() : nullptr;
}

FBox FPaper2DPlusFrameCuePreviewHost::GetSubjectVisualBounds() const
{
	FBox Bounds(ForceInit);
	if (UPaperFlipbookComponent* RenderComponent = FlipbookComponent.Get())
	{
		if (RenderComponent->IsVisible())
		{
			RenderComponent->UpdateBounds();
			Bounds += RenderComponent->Bounds.GetBox();
		}
	}
	for (const TWeakObjectPtr<UPaperSpriteComponent>& WeakLayer : LayerSpriteComponents)
	{
		if (UPaperSpriteComponent* LayerComponent = WeakLayer.Get())
		{
			if (LayerComponent->IsVisible() && LayerComponent->GetSprite())
			{
				LayerComponent->UpdateBounds();
				Bounds += LayerComponent->Bounds.GetBox();
			}
		}
	}
	return Bounds;
}

void FPaper2DPlusFrameCuePreviewHost::SyncSubject(
	UPaper2DPlusCharacterProfileAsset* Profile,
	UPaperFlipbook* Flipbook,
	int32 FrameIndex,
	FName AnimationName)
{
	const TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset> PreviousProfile = SyncedProfile;
	const TWeakObjectPtr<UPaperFlipbook> PreviousFlipbook = SyncedFlipbook;
	const FName PreviousAnimationName = SyncedAnimationName;
	const int32 PreviousFrameIndex = SyncedFrameIndex;
	SyncedProfile = Profile;
	SyncedFlipbook = Flipbook;
	SyncedAnimationName = AnimationName;
	if (SyncedAnimationName.IsNone() && Profile && Flipbook)
	{
		SyncedAnimationName = FName(*Profile->GetFlipbookName(Flipbook));
	}
	if (FrameIndex != INDEX_NONE)
	{
		SyncedFrameIndex = FrameIndex;
	}
	const bool bSubjectRecreated = EnsurePreviewSubject();
	const bool bIdentityChanged =
		PreviousProfile != SyncedProfile
		|| PreviousFlipbook != SyncedFlipbook
		|| PreviousAnimationName != SyncedAnimationName;
	const bool bFrameChanged =
		FrameIndex != INDEX_NONE && PreviousFrameIndex != SyncedFrameIndex;
	if (!bSubjectRecreated && !bIdentityChanged && !bFrameChanged && !bSubjectStateDirty)
	{
		return;
	}

	if (UPaper2DPlusCharacterProfileComponent* SubjectProfile = ProfileComponent.Get())
	{
		// Direct assignment keeps this context carrier inert. SetCharacterProfile would enter the
		// runtime flipbook-change/Frame-Cue dispatch funnel and create a second preview authority.
		SubjectProfile->CharacterProfile = Profile;
		SubjectProfile->FlipbookComponent = FlipbookComponent.Get();
		SubjectProfile->SetComponentTickEnabled(false);
	}
	if (UPaperFlipbookComponent* RenderComponent = FlipbookComponent.Get())
	{
		if (RenderComponent->GetFlipbook() != Flipbook)
		{
			RenderComponent->SetFlipbook(Flipbook);
		}
		RenderComponent->Stop();
		if (Flipbook && Flipbook->GetNumKeyFrames() > 0 && FrameIndex != INDEX_NONE)
		{
			const int32 ClampedFrame =
				FMath::Clamp(FrameIndex, 0, Flipbook->GetNumKeyFrames() - 1);
			// ClampedFrame is a KEY-FRAME index — the editor's currency, and what indexes
			// GetKeyFrameChecked below — but SetPlaybackPositionInFrames takes TIMELINE frames
			// (time * fps). The two diverge as soon as any key frame holds a FrameRun above one,
			// which the exact Aseprite timing import produces routinely, so seek by time instead.
			// Landing mid-frame keeps float rounding from resolving the seek back onto the previous
			// key frame's boundary. GetFrameStartTime/GetFrameDurationSeconds already guard zero fps.
			const FFlipbookTimingData Timing =
				FFlipbookTimingData::ReadFromFlipbook(Flipbook);
			const float MidFrameTime = Timing.GetFrameStartTime(ClampedFrame)
				+ 0.5f * Timing.GetFrameDurationSeconds(ClampedFrame);
			RenderComponent->SetPlaybackPosition(MidFrameTime, /*bFireEvents=*/false);

			// World rendering already honors the Paper Sprite's baked pivot. Add only the Profile's
			// canonical extraction/trim alignment, through the same pixel-to-world conversion used by
			// runtime Layer children. Resolve the selected entry by animation identity first because a
			// Flipbook pointer may intentionally appear in more than one Profile row.
			const FFlipbookProfileEntry* SelectedEntry = nullptr;
			if (Profile)
			{
				for (const FFlipbookProfileEntry& Entry : Profile->Flipbooks)
				{
					if (FName(*Entry.Identity.FlipbookName) == SyncedAnimationName
						&& Entry.Identity.Flipbook.Get() == Flipbook)
					{
						SelectedEntry = &Entry;
						break;
					}
				}
				if (!SelectedEntry)
				{
					SelectedEntry = Profile->FindByFlipbookPtr(Flipbook);
				}
			}
			UPaperSprite* FrameSprite =
				Flipbook->GetKeyFrameChecked(ClampedFrame).Sprite;
			const float PixelsPerUnit =
				FrameSprite ? FrameSprite->GetPixelsPerUnrealUnit() : 1.0f;
			const FVector2D OffsetPx = Paper2DPlusLayerDraw::ResolveTotalOffsetPx(
				SelectedEntry,
				ClampedFrame,
				/*Layer=*/nullptr,
				SyncedAnimationName.ToString());
			RenderComponent->SetRelativeLocation(
				Paper2DPlusLayerDraw::PixelOffsetToWorld(
					OffsetPx,
					PixelsPerUnit,
					RenderComponent->GetRelativeScale3D(),
					/*bFacingLeft=*/false));
		}
		RenderComponent->UpdateBounds();
		RenderComponent->MarkRenderTransformDirty();
		RenderComponent->MarkRenderStateDirty();
	}
	bSubjectStateDirty = false;
}

void FPaper2DPlusFrameCuePreviewHost::SetSubjectVisible(bool bVisible)
{
	if (UPaperFlipbookComponent* RenderComponent = FlipbookComponent.Get())
	{
		// Layer art and Cue-spawned attachments are siblings/children with their own visibility. Hiding
		// only the baseline prevents Layer mode from suppressing ordinary attached effects.
		RenderComponent->SetVisibility(bVisible, false);
	}
}

void FPaper2DPlusFrameCuePreviewHost::SyncLayerSprites(
	const TArray<FPaper2DPlusPreviewLayerSprite>& Layers)
{
	EnsurePreviewSubject();
	AActor* Subject = PreviewActor.Get();
	USceneComponent* Root = PreviewRootComponent.Get();
	if (!Subject || !Root)
	{
		return;
	}

	while (LayerSpriteComponents.Num() < Layers.Num())
	{
		const int32 LayerIndex = LayerSpriteComponents.Num();
		const FName ComponentName = MakeUniqueObjectName(
			Subject,
			UPaperSpriteComponent::StaticClass(),
			FName(*FString::Printf(TEXT("PreviewLayer_%d"), LayerIndex)));
		UPaperSpriteComponent* Component = NewObject<UPaperSpriteComponent>(
			Subject, ComponentName, RF_Transient);
		Subject->AddInstanceComponent(Component);
		Component->SetupAttachment(Root);
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Component->SetGenerateOverlapEvents(false);
		Component->SetComponentTickEnabled(false);
		Component->RegisterComponent();
		LayerSpriteComponents.Add(Component);
		RegisterBaselineComponent(Component);
	}

	for (int32 LayerIndex = 0; LayerIndex < LayerSpriteComponents.Num(); ++LayerIndex)
	{
		UPaperSpriteComponent* Component = LayerSpriteComponents[LayerIndex].Get();
		if (!IsValid(Component))
		{
			for (int32 SuffixIndex = LayerIndex;
				SuffixIndex < LayerSpriteComponents.Num();
				++SuffixIndex)
			{
				if (UPaperSpriteComponent* SuffixComponent =
					LayerSpriteComponents[SuffixIndex].Get())
				{
					BaselineComponents.Remove(
						TWeakObjectPtr<UActorComponent>(SuffixComponent));
					SuffixComponent->DestroyComponent();
				}
			}
			LayerSpriteComponents.SetNum(LayerIndex);
			SyncLayerSprites(Layers);
			return;
		}
		if (!Layers.IsValidIndex(LayerIndex) || !Layers[LayerIndex].Sprite)
		{
			Component->SetSprite(nullptr);
			Component->SetVisibility(false, false);
			continue;
		}

		const FPaper2DPlusPreviewLayerSprite& Layer = Layers[LayerIndex];
		Component->SetSprite(Layer.Sprite);
		Component->SetRelativeScale3D(FVector::OneVector);
		FVector RelativeLocation = Paper2DPlusLayerDraw::PixelOffsetToWorld(
			Layer.TotalOffsetPx,
			Layer.Sprite->GetPixelsPerUnrealUnit(),
			Component->GetRelativeScale3D(),
			/*bFacingLeft=*/false);
		RelativeLocation.Y = LayerIndex * 0.1f;
		Component->SetRelativeLocation(RelativeLocation);
		Component->SetRelativeRotation(FRotator::ZeroRotator);
		Component->SetSpriteColor(FLinearColor::White);
		Component->EmptyOverrideMaterials();
		Component->SetTranslucentSortPriority(LayerIndex);
		Component->SetVisibility(true, false);
	}

	SetSubjectVisible(Layers.IsEmpty());
}

void FPaper2DPlusFrameCuePreviewHost::SyncAdapterVisuals()
{
	if (!Context)
	{
		return;
	}
	EnsurePreviewSubject();
	AActor* Subject = PreviewActor.Get();
	USceneComponent* Root = PreviewRootComponent.Get();
	USceneComponent* EffectParent = FlipbookComponent.IsValid()
		? static_cast<USceneComponent*>(FlipbookComponent.Get())
		: Root;
	UWorld* World = GetPreviewWorld();
	if (!Subject || !Root || !EffectParent || !World)
	{
		return;
	}

	TSet<int32> LiveEffectHandles;
	for (const FPaper2DPlusPreviewEffect& Effect : Context->GetEffectProxies())
	{
		UPaperFlipbook* EffectFlipbook = Effect.Settings.EffectFlipbook;
		if (!EffectFlipbook || Effect.Handle == INDEX_NONE)
		{
			continue;
		}
		LiveEffectHandles.Add(Effect.Handle);
		UPaperFlipbookComponent* Component =
			AdapterEffectComponents.FindRef(Effect.Handle).Get();
		if (!IsValid(Component))
		{
			const FName ComponentName = MakeUniqueObjectName(
				Subject,
				UPaperFlipbookComponent::StaticClass(),
				FName(*FString::Printf(TEXT("PreviewAdapterEffect_%d"), Effect.Handle)));
			Component = NewObject<UPaperFlipbookComponent>(
				Subject, ComponentName, RF_Transient);
			Subject->AddInstanceComponent(Component);
			Component->SetupAttachment(EffectParent);
			Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Component->SetGenerateOverlapEvents(false);
			Component->SetComponentTickEnabled(false);
			Component->SetLooping(false);
			Component->RegisterComponent();
			AdapterEffectComponents.Add(Effect.Handle, Component);
		}

		Component->SetFlipbook(EffectFlipbook);
		Component->Stop();
		const float Duration = EffectFlipbook->GetTotalDuration();
		Component->SetPlaybackPosition(
			Duration > 0.0f
				? FMath::Clamp(
					Effect.Age, 0.0f, FMath::Max(0.0f, Duration - KINDA_SMALL_NUMBER))
				: 0.0f,
			/*bFireEvents=*/false);
		Component->SetRelativeLocation(FVector(
			Effect.Settings.Offset.X,
			10.0f + Effect.Handle * 0.001f,
			Effect.Settings.Offset.Y));
		Component->SetRelativeRotation(FRotator(-Effect.Settings.Rotation, 0.0f, 0.0f));
		Component->SetRelativeScale3D(FVector(
			Effect.Settings.Scale.X,
			1.0f,
			Effect.Settings.Scale.Y));
		Component->SetSpriteColor(Effect.Settings.Tint);
		Component->SetTranslucentSortPriority(1000 + Effect.Handle);
		Component->SetVisibility(true, false);
	}

	TArray<int32> StaleEffectHandles;
	for (const TPair<int32, TWeakObjectPtr<UPaperFlipbookComponent>>& Pair
		: AdapterEffectComponents)
	{
		if (!LiveEffectHandles.Contains(Pair.Key))
		{
			StaleEffectHandles.Add(Pair.Key);
		}
	}
	for (const int32 Handle : StaleEffectHandles)
	{
		if (UPaperFlipbookComponent* Component =
			AdapterEffectComponents.FindRef(Handle).Get())
		{
			Component->DestroyComponent();
		}
		AdapterEffectComponents.Remove(Handle);
	}

}

void FPaper2DPlusFrameCuePreviewHost::PopulateContext(
	FPaper2DPlusFrameCueContext& CueContext,
	UPaper2DPlusCharacterProfileAsset* Profile,
	UPaperFlipbook* Flipbook,
	FName AnimationName)
{
	SyncSubject(Profile, Flipbook, CueContext.CurrentFrame, AnimationName);
	PopulateContext(CueContext);
}

void FPaper2DPlusFrameCuePreviewHost::PopulateContext(
	FPaper2DPlusFrameCueContext& CueContext)
{
	EnsurePreviewSubject();
	CueContext.OwningActor = PreviewActor.Get();
	CueContext.ProfileComponent = ProfileComponent.Get();
	CueContext.Flipbook = SyncedFlipbook.Get();
	CueContext.AnimationName = SyncedAnimationName;
	CueContext.CharacterProfile = SyncedProfile.Get();
	CueContext.PlaybackComponent = FlipbookComponent.Get();
	switch (CueContext.EvaluationMode)
	{
	case EPaper2DPlusFrameCueEvaluationMode::EditorPlayback:
	case EPaper2DPlusFrameCueEvaluationMode::EditorScrubSeek:
	case EPaper2DPlusFrameCueEvaluationMode::EditorSelectionPreview:
		CueContext.SetEvaluationMode(CueContext.EvaluationMode);
		break;
	default:
		// The host is an editor-only producer. Normalize legacy/default callers instead of publishing
		// a runtime mode whose compatibility booleans contradict the isolated preview world.
		CueContext.SetEvaluationMode(EPaper2DPlusFrameCueEvaluationMode::EditorPlayback);
		break;
	}
}

void FPaper2DPlusFrameCuePreviewHost::HandlePreviewAdaptersChanged()
{
	bRegistryRefreshPending = true;
	FlushDeferredWork();
}

/**
 * True unless the compiled Blueprint provably cannot touch this preview. Compiling any gameplay
 * Blueprint used to rebuild the isolated world for every open Frame Cues panel; the reset is now
 * skipped only when all three relevance channels come back clean — no Cue or preview-adapter
 * lineage, and no live instance of the Blueprint's old classes inside the preview world. Anything
 * unresolvable answers true so a wrong guess can only cost a redundant reset, never a stale class.
 */
bool FPaper2DPlusFrameCuePreviewHost::IsBlueprintRelevantToPreview(
	const UBlueprint* Blueprint) const
{
	if (!Blueprint)
	{
		// An anonymous compile could be anything, including a Cue Type.
		return true;
	}

	// At pre-compile time GeneratedClass is still the OLD class about to be reinstanced, so it and
	// the skeleton double as the instance-match keys below. ParentClass covers a first compile that
	// has produced no generated class yet.
	UClass* const GeneratedClass = Blueprint->GeneratedClass.Get();
	UClass* const SkeletonClass = Blueprint->SkeletonGeneratedClass.Get();
	UClass* const ParentClass = Blueprint->ParentClass.Get();
	if (!GeneratedClass && !SkeletonClass && !ParentClass)
	{
		return true;
	}

	// Cue classes execute behavior against this host's subject, and every registered adapter class
	// (project rows are TSoftClassPtr<UPaper2DPlusFrameCuePreviewAdapter>) derives from the adapter
	// base, so lineage against the two roots covers both registries without loading anything.
	const auto IsCueOrAdapterLineage = [](const UClass* Class)
	{
		return Class
			&& (Class->IsChildOf(UPaper2DPlusCueBase::StaticClass())
				|| Class->IsChildOf(UPaper2DPlusFrameCuePreviewAdapter::StaticClass()));
	};
	if (IsCueOrAdapterLineage(GeneratedClass)
		|| IsCueOrAdapterLineage(SkeletonClass)
		|| IsCueOrAdapterLineage(ParentClass))
	{
		return true;
	}

	// Reinstancing rewrites every object whose class derives from the compiled Blueprint's old
	// classes, so any such actor or component in the isolated world makes the compile relevant.
	// The direction matters: an instance whose class IsChildOf the compiled class is affected; the
	// compiled class deriving from an instance's class is not. ParentClass is deliberately excluded
	// here — matching instances against a native ancestor like AActor would defeat the narrowing.
	// The scan stays cheap because the preview scene holds a handful of objects at most.
	UWorld* const World = GetPreviewWorld();
	if (!World)
	{
		return true;
	}
	const auto InstanceMatchesCompiledClass =
		[GeneratedClass, SkeletonClass](const UObject* Object)
	{
		const UClass* const ObjectClass = Object ? Object->GetClass() : nullptr;
		return ObjectClass
			&& ((GeneratedClass && ObjectClass->IsChildOf(GeneratedClass))
				|| (SkeletonClass && ObjectClass->IsChildOf(SkeletonClass)));
	};
	for (TActorIterator<AActor> ActorIt(World); ActorIt; ++ActorIt)
	{
		if (InstanceMatchesCompiledClass(*ActorIt))
		{
			return true;
		}
	}
	bool bComponentMatches = false;
	ForEachPreviewWorldComponent(
		[&bComponentMatches, &InstanceMatchesCompiledClass](UActorComponent* Component)
		{
			bComponentMatches |= InstanceMatchesCompiledClass(Component);
		});
	return bComponentMatches;
}

void FPaper2DPlusFrameCuePreviewHost::HandleBlueprintPreCompile(UBlueprint* Blueprint)
{
	// Reset before generated classes are reinstanced, while creator adapter functions are still safe
	// to invoke. The reset is scoped, not unconditional: a Blueprint with no Cue/adapter lineage and
	// no live instance in the preview world is skipped, and anything unresolvable keeps the reset.
	if (!IsBlueprintRelevantToPreview(Blueprint))
	{
		return;
	}
	bHardResetPending = true;
	FlushDeferredWork();
}

void FPaper2DPlusFrameCuePreviewHost::InvokeBeforeHardReset()
{
	if (!BeforeHardReset || bInvokingBeforeHardReset)
	{
		return;
	}
	bInvokingBeforeHardReset = true;
	BeforeHardReset();
	bInvokingBeforeHardReset = false;
}

void FPaper2DPlusFrameCuePreviewHost::HandleBlueprintCompiled()
{
	// Generated adapter classes may have been reinstanced; reconstruct after the pre-compile reset.
	bRebuildPending = true;
	FlushDeferredWork();
}

void FPaper2DPlusFrameCuePreviewHost::BeginDispatchScope()
{
	++DispatchScopeDepth;
}

void FPaper2DPlusFrameCuePreviewHost::EndDispatchScope()
{
	DispatchScopeDepth = FMath::Max(0, DispatchScopeDepth - 1);
	if (DispatchScopeDepth == 0)
	{
		FlushDeferredWork();
		if (bTrackedWorldResourcesDirty)
		{
			RefreshTrackedWorldResources();
			bTrackedWorldResourcesDirty = false;
		}
	}
}

void FPaper2DPlusFrameCuePreviewHost::FlushDeferredWork()
{
	if (DispatchScopeDepth > 0 || bFlushingDeferredWork
		|| (!bRegistryRefreshPending
			&& !bResetPending
			&& !bRebuildPending
			&& !bHardResetPending))
	{
		return;
	}

	bFlushingDeferredWork = true;
	constexpr int32 MaxDrainPasses = 16;
	int32 DrainPass = 0;
	for (; DrainPass < MaxDrainPasses; ++DrainPass)
	{
		const bool bNeedHardReset = bHardResetPending || bRegistryRefreshPending;
		const bool bNeedReset = bNeedHardReset || bResetPending;
		const bool bNeedRebuild = bRegistryRefreshPending || bRebuildPending;
		bHardResetPending = false;
		bRegistryRefreshPending = false;
		bResetPending = false;
		bRebuildPending = false;

		if (bNeedHardReset)
		{
			InvokeBeforeHardReset();
		}
		if (bNeedReset)
		{
			ResetPreviewResourcesNow();
			if (bRegistryRefreshPending)
			{
				// A ReceiveReset implementation may update the registry. Teardown has already happened
				// in this pass, so only the latest adapter reconstruction remains. Preserve a separately
				// raised Blueprint hard reset; it still needs its own paired-End/reset pass.
				bRebuildPending = true;
				bRegistryRefreshPending = false;
				if (!bHardResetPending)
				{
					bResetPending = false;
				}
			}
		}
		if (bNeedRebuild)
		{
			RebuildAdapters();
		}

		// ReceiveReset, Cue End, registry delegates, and Blueprint compile delegates may synchronously
		// request more work. Drain every flag—including a hard reset raised during ReceiveReset—before
		// returning, so a stale request cannot detonate on the next unrelated preview.
		if (!bHardResetPending
			&& !bRegistryRefreshPending
			&& !bResetPending
			&& !bRebuildPending)
		{
			break;
		}
	}
	bFlushingDeferredWork = false;
	const bool bStabilized = DrainPass < MaxDrainPasses;
	ensureMsgf(
		bStabilized,
		TEXT("Frame Cue preview teardown did not stabilize after %d passes."),
		MaxDrainPasses);
	if (!bStabilized)
	{
		// Fail closed without arming a delayed teardown against the next unrelated preview.
		bHardResetPending = false;
		bRegistryRefreshPending = false;
		bResetPending = false;
		bRebuildPending = false;
	}
}

void FPaper2DPlusFrameCuePreviewHost::AddAdapterClass(UClass* AdapterClass)
{
	if (!AdapterClass
		|| AdapterClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated)
		|| !AdapterClass->IsChildOf(UPaper2DPlusFrameCuePreviewAdapter::StaticClass()))
	{
		return;
	}
	for (const TStrongObjectPtr<UPaper2DPlusFrameCuePreviewAdapter>& Existing : Adapters)
	{
		if (Existing && Existing->GetClass() == AdapterClass)
		{
			return;
		}
	}
	Adapters.Emplace(NewObject<UPaper2DPlusFrameCuePreviewAdapter>(
		GetTransientPackage(), AdapterClass, NAME_None, RF_Transient));
}

void FPaper2DPlusFrameCuePreviewHost::Notify(
	UPaper2DPlusCueBase* Cue,
	const FPaper2DPlusFrameCueContext& CueContext)
{
	// Notify is called after the Cue's own behavior and before optional adapters return. From this
	// point the isolated world may contain untracked timers, latent work, physics or settings changes,
	// even when no visible resource was created.
	bPreviewWorldMayBeDirty = true;
	bTrackedWorldResourcesDirty = true;
	// Project adapters can synchronously edit the registry or trigger a Blueprint compile. Iterate a
	// strong snapshot and defer host reconstruction until the outer callback batch has completed.
	const TArray<TStrongObjectPtr<UPaper2DPlusFrameCuePreviewAdapter>> AdapterSnapshot = Adapters;
	BeginDispatchScope();
	for (int32 AdapterIndex = 0; AdapterIndex < AdapterSnapshot.Num(); ++AdapterIndex)
	{
		const TStrongObjectPtr<UPaper2DPlusFrameCuePreviewAdapter>& Adapter = AdapterSnapshot[AdapterIndex];
		if (Adapter)
		{
			UPaper2DPlusFrameCuePreviewContext::FScopedAdapterDispatch OwnerScope(
				*Context,
				GetOwnerTokenForAdapterIndex(AdapterIndex));
			Adapter->DispatchPreview(Cue, CueContext, Context.Get());
		}
	}
	EndDispatchScope();
	if (EnsurePreviewSubject())
	{
		// Destroy Actor/Destroy Component is ordinary Cue behavior. Rebuild the inert subject
		// immediately after every sink has observed that dispatch, so the viewport never spends a
		// frame with no character and the next Cue receives a complete context.
		SyncSubject(
			SyncedProfile.Get(),
			SyncedFlipbook.Get(),
			SyncedFrameIndex,
			SyncedAnimationName);
	}
}

/**
 * True when SOME registered adapter claims this cue.
 *
 * With no plugin-owned adapters this is false until a project registers one, and that is the ordinary
 * state, not a fault: a Cue Type's own behavior still runs in preview through the shared dispatch, and
 * adapters only add advanced editor overlays on top. This is an adapter-diagnostics seam, never the
 * availability gate for ordinary preview.
 */
bool FPaper2DPlusFrameCuePreviewHost::CanPreview(const UPaper2DPlusCueBase* Cue) const
{
	if (!Cue) return false;
	for (const TStrongObjectPtr<UPaper2DPlusFrameCuePreviewAdapter>& Adapter : Adapters)
	{
		if (Adapter && Adapter->CanPreview(*Cue)) return true;
	}
	return false;
}

TArray<FString> FPaper2DPlusFrameCuePreviewHost::GetAdapterClassPaths() const
{
	TArray<FString> Result;
	Result.Reserve(Adapters.Num());
	for (const TStrongObjectPtr<UPaper2DPlusFrameCuePreviewAdapter>& Adapter : Adapters)
	{
		if (Adapter) Result.Add(Adapter->GetClass()->GetPathName());
	}
	return Result;
}

bool FPaper2DPlusFrameCuePreviewHost::HasOwnedResources() const
{
	// A synchronous behavior call is not itself a resource. Real context/world output keeps the
	// stopped-preview status ticker alive; a behavior-free Cue returns to idle immediately.
	return GetOwnedResourceCount() > 0;
}

int32 FPaper2DPlusFrameCuePreviewHost::GetOwnedResourceCount() const
{
	int32 ResourceCount = Context ? Context->GetOwnedResourceCount() : 0;
	UWorld* World = GetPreviewWorld();
	if (!World)
	{
		return ResourceCount;
	}

	TSet<const AActor*> LiveSpawnedActors;
	for (const TWeakObjectPtr<AActor>& WeakActor : SpawnedActors)
	{
		const AActor* Actor = WeakActor.Get();
		if (IsValid(Actor)
			&& !Actor->IsActorBeingDestroyed()
			&& !Actor->IsPendingKillPending())
		{
			LiveSpawnedActors.Add(Actor);
			++ResourceCount;
		}
	}

	for (const TWeakObjectPtr<UActorComponent>& WeakComponent : TrackedWorldComponents)
	{
		const UActorComponent* Component = WeakComponent.Get();
		if (!IsValid(Component)
			|| Component->GetWorld() != World
			|| !Component->IsRegistered())
		{
			continue;
		}
		const ULineBatchComponent* LineBatcher = Cast<ULineBatchComponent>(Component);
		if (LineBatcher
			&& (LineBatcher->BatchedLines.Num() > 0
				|| LineBatcher->BatchedPoints.Num() > 0
				|| LineBatcher->BatchedMeshes.Num() > 0))
		{
			++ResourceCount;
			continue;
		}
		if (IsBaselineComponent(Component)
			|| LineBatcher)
		{
			continue;
		}
		bool bIsAdapterProjection = false;
		for (const TPair<int32, TWeakObjectPtr<UPaperFlipbookComponent>>& Pair
			: AdapterEffectComponents)
		{
			if (Pair.Value.Get() == Component)
			{
				bIsAdapterProjection = true;
				break;
			}
		}
		if (bIsAdapterProjection)
		{
			// The adapter context handle already contributes one resource. Its world-rendered
			// flipbook is a projection of that handle, not a second independently owned resource.
			continue;
		}
		// A spawned actor is one preview resource regardless of its component count. Components
		// attached to the stable subject (or world-owned one-shots) remain independently countable.
		if (!LiveSpawnedActors.Contains(Component->GetOwner()))
		{
			++ResourceCount;
		}
	}
	return ResourceCount;
}

void FPaper2DPlusFrameCuePreviewHost::Tick(float DeltaSeconds)
{
	const float SafeDelta = FMath::Max(0.0f, DeltaSeconds);
	if (Context)
	{
		Context->Tick(SafeDelta);
	}
	if (!GIntraFrameDebuggingGameThread)
	{
		if (UWorld* World = GetPreviewWorld())
		{
			World->Tick(LEVELTICK_All, SafeDelta);
		}
	}
	for (auto It = SpawnedActors.CreateIterator(); It; ++It)
	{
		const AActor* Actor = It->Get();
		if (!IsValid(Actor)
			|| Actor->IsActorBeingDestroyed()
			|| Actor->IsPendingKillPending())
		{
			It.RemoveCurrent();
		}
	}
}

void FPaper2DPlusFrameCuePreviewHost::Reset()
{
	bResetPending = true;
	FlushDeferredWork();
}

void FPaper2DPlusFrameCuePreviewHost::ResetPreviewResourcesNow()
{
	if (DispatchScopeDepth > 0 || bInvokingBeforeHardReset)
	{
		bResetPending = true;
		return;
	}

	const bool bNeedsFreshWorld =
		bPreviewWorldMayBeDirty
		|| Adapters.Num() > 0
		|| SpawnedActors.Num() > 0
		|| TrackedWorldComponents.Num() > 0
		|| (Context && Context->GetOwnedResourceCount() > 0);
	const TArray<TStrongObjectPtr<UPaper2DPlusFrameCuePreviewAdapter>> AdapterSnapshot = Adapters;
	BeginDispatchScope();
	for (int32 AdapterIndex = 0; AdapterIndex < AdapterSnapshot.Num(); ++AdapterIndex)
	{
		const TStrongObjectPtr<UPaper2DPlusFrameCuePreviewAdapter>& Adapter = AdapterSnapshot[AdapterIndex];
		if (Adapter)
		{
			{
				UPaper2DPlusFrameCuePreviewContext::FScopedAdapterDispatch OwnerScope(
					*Context,
					GetOwnerTokenForAdapterIndex(AdapterIndex));
				Adapter->DispatchReset(Context.Get());
			}
			// Cleanup is host-enforced even when a creator adapter forgets to release its handles.
			Context->ReleaseResourcesForOwner(GetOwnerTokenForAdapterIndex(AdapterIndex));
		}
	}
	EndDispatchScope();
	if (Context)
	{
		Context->ResetAll();
	}
	bPreviewWorldMayBeDirty = false;
	bTrackedWorldResourcesDirty = false;
	// Replacing the EditorPreview UWorld is the reset contract. It clears physics, timers, latent
	// actions, WorldSettings mutations (time dilation/gravity), dynamically spawned actor classes,
	// and every component property without destroying FPreviewScene (whose engine destructor forces a
	// full-purge GC and would turn ordinary timeline scrubbing into a hitch).
	if (!bDestroyingPreviewHost && bNeedsFreshWorld)
	{
		ReplacePreviewWorld();
		SyncSubject(
			SyncedProfile.Get(),
			SyncedFlipbook.Get(),
			SyncedFrameIndex,
			SyncedAnimationName);
	}
}

bool FPaper2DPlusFrameCuePreviewHost::IsBaselineComponent(
	const UActorComponent* Component) const
{
	return Component && BaselineComponents.Contains(
		TWeakObjectPtr<UActorComponent>(const_cast<UActorComponent*>(Component)));
}
