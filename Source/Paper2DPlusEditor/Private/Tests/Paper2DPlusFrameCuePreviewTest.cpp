// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "Paper2DPlusFrameCueProjectAdapterTestTypes.h"

bool UPaper2DPlusFrameCueRegistryReentryAdapterTest::bBroadcastOnPreview = false;
bool UPaper2DPlusFrameCueRegistryReentryAdapterTest::bBroadcastOnReset = false;
bool UPaper2DPlusFrameCueRegistryReentryAdapterTest::bInsideCallback = false;
TWeakObjectPtr<UPaper2DPlusFrameCuePreviewContext>
	UPaper2DPlusFrameCueRegistryReentryAdapterTest::TrackedContext;
int32 UPaper2DPlusFrameCueRegistryReentryAdapterTest::PreviewCallCount = 0;
int32 UPaper2DPlusFrameCueRegistryReentryAdapterTest::ResetCallCount = 0;

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "FrameCues/Paper2DPlusFrameCuePreviewHost.h"
#include "FrameCues/Paper2DPlusFrameCuePreviewContext.h"
#include "FrameCues/Paper2DPlusFrameCueEditorSettings.h"
#include "Paper2DPlusEditorTestFrameCueTypes.h"
#include "SFrameEventPreviewCanvas.h"
#include "Components/SceneComponent.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/Engine.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/ScopeExit.h"
#include "PaperFlipbook.h"
#include "PaperFlipbookComponent.h"
#include "Paper2DPlusBlueprintLibrary.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Paper2DPlusEffectProfileAsset.h"
#include "PaperSprite.h"
#include "PaperSpriteComponent.h"
#include "Sound/SoundWave.h"
#include "UObject/UnrealType.h"

namespace Paper2DPlusFrameCuePreviewTest
{
	/**
	 * Preview adapters the plugin itself installs before the project registry is read.
	 *
	 * It is zero: the adapter FRAMEWORK and its project registration seam survive, but the five
	 * bespoke native adapters died with the built-in Cue Types they simulated. Adapter owner tokens
	 * are one-based executable registry slots counted from this base, so every count and every
	 * derived owner token in this file reads from here instead of from a literal.
	 */
	constexpr int32 NativeAdapterCount = 0;

	/** A Moment placement carrying real art, which is what SpawnEffectProxy requires. */
	UPaper2DPlusEditorTestMomentCue* MakeEffectCue()
	{
		UPaper2DPlusEditorTestMomentCue* Cue =
			NewObject<UPaper2DPlusEditorTestMomentCue>(GetTransientPackage());
		Cue->EffectArt = NewObject<UPaperFlipbook>();
		Cue->Offset = FVector2D(12.0f, 4.0f);
		return Cue;
	}

	/** One profile whose single animation holds authored FrameRuns, the exact-timing import shape. */
	UPaper2DPlusCharacterProfileAsset* MakeRunLengthProfile(
		const TArray<int32>& FrameRuns,
		const float FramesPerSecond,
		UPaperFlipbook*& OutFlipbook)
	{
		UPaper2DPlusCharacterProfileAsset* Profile =
			NewObject<UPaper2DPlusCharacterProfileAsset>(
				GetTransientPackage(), NAME_None, RF_Transient);
		OutFlipbook = NewObject<UPaperFlipbook>(Profile);
		{
			FScopedFlipbookMutator Mutator(OutFlipbook);
			Mutator.FramesPerSecond = FramesPerSecond;
			for (const int32 FrameRun : FrameRuns)
			{
				FPaperFlipbookKeyFrame KeyFrame;
				KeyFrame.FrameRun = FrameRun;
				Mutator.KeyFrames.Add(KeyFrame);
			}
		}
		FFlipbookProfileEntry& Entry = Profile->Flipbooks.AddDefaulted_GetRef();
		Entry.Identity.FlipbookName = TEXT("RunLengthSeek");
		Entry.Identity.Flipbook = OutFlipbook;
		Entry.CombatData.Frames.SetNum(FrameRuns.Num());
		return Profile;
	}

	/** Installs exactly one project adapter for the duration of a test and restores the registry. */
	struct FScopedPreviewAdapterRegistry
	{
		explicit FScopedPreviewAdapterRegistry(UClass* AdapterClass)
		{
			UPaper2DPlusFrameCueEditorSettings* Settings =
				GetMutableDefault<UPaper2DPlusFrameCueEditorSettings>();
			SavedAdapters = Settings->PreviewAdapters;
			Settings->PreviewAdapters.Reset();
			if (AdapterClass)
			{
				Settings->PreviewAdapters.Add(
					TSoftClassPtr<UPaper2DPlusFrameCuePreviewAdapter>(AdapterClass));
			}
			Settings->NotifyPreviewAdaptersChanged();
		}

		~FScopedPreviewAdapterRegistry()
		{
			UPaper2DPlusFrameCueEditorSettings* Settings =
				GetMutableDefault<UPaper2DPlusFrameCueEditorSettings>();
			Settings->PreviewAdapters = SavedAdapters;
			Settings->NotifyPreviewAdaptersChanged();
		}

		TArray<TSoftClassPtr<UPaper2DPlusFrameCuePreviewAdapter>> SavedAdapters;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCuePreviewResourceLedgerTest,
	"Paper2DPlus.FrameCues.Editor.Preview.ResourceLedgerAndExpiry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCuePreviewResourceLedgerTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusFrameCuePreviewTest;

	// The registry has to be installed before the host is constructed: RebuildAdapters runs in the
	// host constructor and a later broadcast would reset the ledger this test is measuring.
	FScopedPreviewAdapterRegistry Registry(UPaper2DPlusFrameCueLedgerAdapterTest::StaticClass());
	FPaper2DPlusFrameCuePreviewHost Host;
	UPaper2DPlusFrameCuePreviewContext* Preview = Host.GetContext();
	if (!TestNotNull(TEXT("Host context"), Preview)) return false;
	TestEqual(TEXT("The plugin installs no native adapters ahead of the project registry"),
		Host.GetAdapterCount(), NativeAdapterCount + 1);

	FPaper2DPlusFrameCueContext Trigger = FPaper2DPlusFrameCueContext::MakePreview(1, 0, false);
	Trigger.Phase = EPaper2DPlusFrameCuePhase::Trigger;

	UPaper2DPlusEditorTestMomentCue* EffectCue = MakeEffectCue();
	TestTrue(TEXT("The registered project adapter claims its cue class"), Host.CanPreview(EffectCue));
	Host.Notify(EffectCue, Trigger);

	FPaper2DPlusPreviewResourceLedger Ledger = Preview->GetResourceLedger();
	TestEqual(TEXT("Adapter-owned effect proxy is ledgered"), Ledger.Effects, 1);
	TestEqual(TEXT("Adapter-owned overlay is ledgered"), Ledger.Overlays, 1);
	TestEqual(TEXT("Adapter-owned safe camera approximation is ledgered"), Ledger.CameraOffsets, 1);
	TestEqual(TEXT("Ledger total matches context total"), Ledger.Total(), Preview->GetOwnedResourceCount());
	TestEqual(TEXT("Effect proxy is canvas-readable"), Preview->GetEffectProxies().Num(), 1);
	TSharedPtr<SFrameEventPreviewCanvas> Canvas =
		SNew(SFrameEventPreviewCanvas)
		.PreviewHost(&Host);
	Canvas->Tick(FGeometry(), /*InCurrentTime=*/0.0, /*InDeltaTime=*/0.0f);
	TInlineComponentArray<UPaperFlipbookComponent*> RenderedFlipbooks;
	Host.GetPreviewActor()->GetComponents(RenderedFlipbooks);
	TestEqual(TEXT("Adapter effect is projected into the host world beside the subject"),
		RenderedFlipbooks.Num(), 2);
	TestEqual(TEXT("The rendered adapter projection is not double-counted as another resource"),
		Host.GetOwnedResourceCount(), Preview->GetOwnedResourceCount());
	if (Preview->GetEffectProxies().Num() == 1)
	{
		const float InitialAge = Preview->GetEffectProxies()[0].Age;
		Canvas->TickViewportForTests(0.01f);
		TestTrue(TEXT("Effect proxy advances on the live viewport clock"),
			Preview->GetEffectProxies()[0].Age > InitialAge);
	}

	FPaper2DPlusPreviewResourceHandle Shape;
	{
		UPaper2DPlusFrameCuePreviewContext::FScopedAdapterDispatch OwnerScope(*Preview, 77);
		Shape = Preview->ShowShape(
			FVector2D(3.0f, 4.0f), FVector2D(8.0f, 12.0f), FLinearColor::Green, 1.0f);
		Preview->CreateTimer(1.0f);
	}
	TestTrue(TEXT("Shape handle is valid"), Shape.IsValid());
	TestEqual(TEXT("Owner-scoped resources were added"), Preview->GetResourceLedger().Total(), Ledger.Total() + 2);
	Preview->ReleaseResourcesForOwner(77);
	TestEqual(TEXT("Owner reset removes only that adapter's resources"), Preview->GetResourceLedger().Total(), Ledger.Total());

	Canvas->TickViewportForTests(0.3f);
	Canvas->Tick(FGeometry(), /*InCurrentTime=*/0.31, /*InDeltaTime=*/0.0f);
	TestTrue(TEXT("All short-lived adapter resources expire"), Preview->GetResourceLedger().IsZero());
	TestTrue(TEXT("Expired camera approximation returns to zero"), Preview->GetCameraOffset().IsNearlyZero());
	RenderedFlipbooks.Reset();
	Host.GetPreviewActor()->GetComponents(RenderedFlipbooks);
	TestEqual(TEXT("Expired adapter effect removes its world projection immediately"),
		RenderedFlipbooks.Num(), 1);

	// Reset remains idempotent after natural expiry and is the final tab/seek/compile teardown gate.
	Host.Reset();
	Host.Reset();
	TestTrue(TEXT("Repeated reset leaves a zero ledger"), Preview->GetResourceLedger().IsZero());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCuePreviewAudioIsolationTest,
	"Paper2DPlus.FrameCues.Editor.Preview.WorldOwnedAudioIsHostIsolated",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCuePreviewAudioIsolationTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusFrameCuePreviewTest;

	// Every assertion below counts real world-owned audio components, which the engine
	// refuses to create while all audio is disabled (-nosound / audio-less CI hosts).
	if (GEngine == nullptr || !GEngine->UseSound())
	{
		AddInfo(TEXT(
			"Skipped: audio is disabled on this host (-nosound), so world-owned preview "
			"audio components cannot exist."));
		return true;
	}

	FScopedPreviewAdapterRegistry Registry(nullptr);
	FPaper2DPlusFrameCuePreviewHost FirstHost;
	FPaper2DPlusFrameCuePreviewHost SecondHost;
	USoundWave* Sound = NewObject<USoundWave>(GetTransientPackage());
	Sound->Duration = 1.0f;

	const FPaper2DPlusPreviewResourceHandle FirstHandle =
		FirstHost.GetContext()->PlaySound(Sound);
	const FPaper2DPlusPreviewResourceHandle SecondHandle =
		SecondHost.GetContext()->PlaySound(Sound);
	TestTrue(TEXT("Each host creates its own world-owned audio component"),
		FirstHandle.IsValid() && SecondHandle.IsValid());
	TestEqual(TEXT("First host owns exactly one sound"),
		FirstHost.GetContext()->GetResourceLedger().Audio, 1);
	TestEqual(TEXT("Second host owns exactly one sound"),
		SecondHost.GetContext()->GetResourceLedger().Audio, 1);

	FirstHost.Reset();
	TestEqual(TEXT("Reset clears only the first host's sound"),
		FirstHost.GetContext()->GetResourceLedger().Audio, 0);
	TestEqual(TEXT("Reset leaves the peer host's sound owned and active"),
		SecondHost.GetContext()->GetResourceLedger().Audio, 1);
	SecondHost.Reset();
	TestTrue(TEXT("Both hosts finish with zero audio resources"),
		FirstHost.GetContext()->GetResourceLedger().IsZero()
			&& SecondHost.GetContext()->GetResourceLedger().IsZero());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCuePreviewWorldRecoveryTest,
	"Paper2DPlus.FrameCues.Editor.Preview.WorldSubjectAndLayerRecovery",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCuePreviewWorldRecoveryTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusFrameCuePreviewTest;

	FScopedPreviewAdapterRegistry Registry(nullptr);
	FPaper2DPlusFrameCuePreviewHost Host;
	UWorld* World = Host.GetPreviewWorld();
	const TWeakObjectPtr<UWorld> RetiredWorld = World;
	if (!TestNotNull(TEXT("Isolated preview world"), World)
		|| !TestNotNull(TEXT("Initial preview actor"), Host.GetPreviewActor())
		|| !TestNotNull(TEXT("Initial flipbook component"), Host.GetFlipbookComponent()))
	{
		return false;
	}
	TestEqual(TEXT("Preview host never borrows the open editor level"),
		World->WorldType, EWorldType::EditorPreview);

	const TWeakObjectPtr<AActor> FirstSubject = Host.GetPreviewActor();
	World->DestroyActor(
		Host.GetPreviewActor(), /*bNetForce=*/false, /*bShouldModifyLevel=*/false);
	Host.SyncSubject(nullptr, nullptr, 0);
	TestTrue(TEXT("Destroy Actor behavior is followed by an immediate fresh subject"),
		Host.GetPreviewActor()
			&& Host.GetPreviewActor() != FirstSubject.Get()
			&& Host.GetFlipbookComponent()
			&& Host.GetFlipbookComponent()->IsRegistered());

	UPaperFlipbookComponent* DestroyedRenderComponent = Host.GetFlipbookComponent();
	DestroyedRenderComponent->DestroyComponent();
	Host.SyncSubject(nullptr, nullptr, 0);
	TestTrue(TEXT("Destroy Component behavior cannot poison the next preview"),
		Host.GetFlipbookComponent()
			&& Host.GetFlipbookComponent() != DestroyedRenderComponent
			&& Host.GetFlipbookComponent()->IsRegistered());

	AActor* MutableSubject = Host.GetPreviewActor();
	UPaperFlipbookComponent* MutableRender = Host.GetFlipbookComponent();
	MutableSubject->SetActorHiddenInGame(true);
	MutableSubject->SetActorEnableCollision(false);
	MutableRender->SetVisibility(false, false);
	MutableRender->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MutableRender->SetRelativeLocation(FVector(40.0f, 0.0f, 15.0f));
	AWorldSettings* PoisonedWorldSettings = World->GetWorldSettings();
	if (PoisonedWorldSettings)
	{
		PoisonedWorldSettings->TimeDilation = 0.1f;
	}
	UPaper2DPlusEditorTestMomentCue* MutatingCue =
		NewObject<UPaper2DPlusEditorTestMomentCue>(GetTransientPackage());
	FPaper2DPlusFrameCueContext MutatingContext =
		FPaper2DPlusFrameCueContext::MakePreview(0, 0, false);
	Host.PopulateContext(MutatingContext, nullptr, nullptr, NAME_None);
	Host.Notify(MutatingCue, MutatingContext);
	Host.Reset();
	TestTrue(TEXT("Reset replaces the poisoned EditorPreview world"),
		Host.GetPreviewWorld()
			&& Host.GetPreviewWorld() != World
			&& Host.GetPreviewWorld()->WorldType == EWorldType::EditorPreview);
	TestFalse(TEXT("Reset immediately invalidates the retired world object graph"),
		RetiredWorld.IsValid());
	if (AWorldSettings* FreshWorldSettings =
		Host.GetPreviewWorld()->GetWorldSettings())
	{
		TestTrue(TEXT("World replacement restores default time dilation"),
			FMath::IsNearlyEqual(FreshWorldSettings->TimeDilation, 1.0f));
	}
	TestTrue(TEXT("Reset restores the subject's visible collision-ready baseline"),
		Host.GetPreviewActor()
			&& !Host.GetPreviewActor()->IsHidden()
			&& Host.GetPreviewActor()->GetActorEnableCollision());
	TestTrue(TEXT("Reset restores the render component's transform, visibility, and collision"),
		Host.GetFlipbookComponent()
			&& Host.GetFlipbookComponent()->IsVisible()
			&& Host.GetFlipbookComponent()->GetCollisionEnabled()
				== ECollisionEnabled::QueryAndPhysics
			&& Host.GetFlipbookComponent()->GetRelativeTransform().Equals(
				FTransform::Identity));

	UPaperSprite* LayerSprite = NewObject<UPaperSprite>(GetTransientPackage());
	TArray<FPaper2DPlusPreviewLayerSprite> Layers;
	FPaper2DPlusPreviewLayerSprite& Layer = Layers.AddDefaulted_GetRef();
	Layer.Sprite = LayerSprite;
	Layer.TotalOffsetPx = FVector2D(8.0f, -4.0f);
	Host.SyncLayerSprites(Layers);
	TInlineComponentArray<UPaperSpriteComponent*> LayerComponents;
	Host.GetPreviewActor()->GetComponents(LayerComponents);
	TestEqual(TEXT("Character Layer art is a real component in the same world"),
		LayerComponents.Num(), 1);
	if (LayerComponents.Num() == 1)
	{
		TestTrue(TEXT("Layer component is visible and collision-free"),
			LayerComponents[0]->IsVisible()
				&& LayerComponents[0]->GetSprite() == LayerSprite
				&& LayerComponents[0]->GetCollisionEnabled()
					== ECollisionEnabled::NoCollision);
	}
	TestFalse(TEXT("Layer mode hides only the base flipbook"),
		Host.GetFlipbookComponent()->IsVisible());
	TestEqual(TEXT("Baseline Layer components are not reported as Cue output"),
		Host.GetOwnedResourceCount(), 0);

	const TArray<FPaper2DPlusPreviewLayerSprite> NoLayers;
	Host.SyncLayerSprites(NoLayers);
	TestTrue(TEXT("Leaving Layer mode restores the base flipbook"),
		Host.GetFlipbookComponent()->IsVisible());
	TestEqual(TEXT("World recovery test ends with no Cue-owned output"),
		Host.GetOwnedResourceCount(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCuePreviewContextAnchorParityTest,
	"Paper2DPlus.FrameCues.Editor.Preview.ContextAndProfileSocketParity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCuePreviewContextAnchorParityTest::RunTest(
	const FString& Parameters)
{
	using namespace Paper2DPlusFrameCuePreviewTest;

	FScopedPreviewAdapterRegistry Registry(nullptr);
	FPaper2DPlusFrameCuePreviewHost Host;
	UPaper2DPlusCharacterProfileAsset* Profile =
		NewObject<UPaper2DPlusCharacterProfileAsset>(
			GetTransientPackage(), NAME_None, RF_Transient);
	UPaperFlipbook* Flipbook = NewObject<UPaperFlipbook>(Profile);
	UTexture2D* Texture = UTexture2D::CreateTransient(64, 64, PF_B8G8R8A8);
	UPaperSprite* Sprite = NewObject<UPaperSprite>(Profile);
	if (!TestNotNull(TEXT("Preview parity Profile"), Profile)
		|| !TestNotNull(TEXT("Preview parity Flipbook"), Flipbook)
		|| !TestNotNull(TEXT("Preview parity texture"), Texture)
		|| !TestNotNull(TEXT("Preview parity sprite"), Sprite))
	{
		return false;
	}

	FSpriteAssetInitParameters InitParams;
	InitParams.Texture = Texture;
	InitParams.Offset = FIntPoint::ZeroValue;
	InitParams.Dimension = FIntPoint(64, 64);
	InitParams.SetPixelsPerUnrealUnit(1.0f);
	Sprite->InitializeSprite(InitParams);
	if (FBoolProperty* SnapProperty = FindFProperty<FBoolProperty>(
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
		FScopedFlipbookMutator Mutator(Flipbook);
		Mutator.FramesPerSecond = 10.0f;
		FPaperFlipbookKeyFrame KeyFrame;
		KeyFrame.Sprite = Sprite;
		KeyFrame.FrameRun = 1;
		Mutator.KeyFrames.Add(KeyFrame);
	}

	FFlipbookProfileEntry& Entry = Profile->Flipbooks.AddDefaulted_GetRef();
	Entry.Identity.FlipbookName = TEXT("PreviewAttack");
	Entry.Identity.Flipbook = Flipbook;
	Entry.CombatData.Frames.SetNum(1);
	FSocketData WeaponSocket;
	WeaponSocket.Name = TEXT("WeaponHand");
	WeaponSocket.X = 43;
	WeaponSocket.Y = 17;
	Entry.CombatData.Frames[0].Sockets.Add(WeaponSocket);

	Host.SyncSubject(Profile, Flipbook, 0, TEXT("PreviewAttack"));
	UPaperFlipbookComponent* PlaybackComponent = Host.GetFlipbookComponent();
	if (!TestNotNull(TEXT("Preview parity playback component"), PlaybackComponent))
	{
		return false;
	}
	const FTransform ComponentTransform(
		FRotator(0.0f, 180.0f, 0.0f),
		FVector(120.0f, 7.0f, 55.0f),
		FVector(2.0f, 1.5f, 3.0f));
	PlaybackComponent->SetWorldTransform(ComponentTransform);

	FPaper2DPlusFrameCueContext PreviewContext =
		FPaper2DPlusFrameCueContext::MakePreview(
			0,
			0,
			false,
			EPaper2DPlusFrameCueEvaluationMode::EditorScrubSeek);
	Host.PopulateContext(
		PreviewContext,
		Profile,
		Flipbook,
		TEXT("PreviewAttack"));
	TestTrue(TEXT("Preview context carries the exact synced Profile"),
		PreviewContext.CharacterProfile == Profile);
	TestTrue(TEXT("Preview context carries the registered live render component"),
		PreviewContext.PlaybackComponent == PlaybackComponent
			&& PreviewContext.PlaybackComponent->IsRegistered());
	TestEqual(TEXT("Preview context retains the explicit scrub mode"),
		PreviewContext.EvaluationMode,
		EPaper2DPlusFrameCueEvaluationMode::EditorScrubSeek);
	TestTrue(TEXT("Preview mode derives the legacy flags"),
		PreviewContext.bIsEditorPreview && !PreviewContext.bIsCatchUp);

	FTransform PreviewSocketTransform = FTransform::Identity;
	UPaperFlipbookComponent* PreviewAttachment = nullptr;
	const EPaper2DPlusFrameCueAnchorResult PreviewResult =
		UPaper2DPlusBlueprintLibrary::ResolveFrameCueAnchor(
			PreviewContext,
			EPaper2DPlusFrameCueAnchorKind::ProfileSocket,
			TEXT("weaponhand"),
			PreviewSocketTransform,
			PreviewAttachment);
	TestEqual(TEXT("Preview Profile Socket resolves"), PreviewResult,
		EPaper2DPlusFrameCueAnchorResult::Success);
	TestTrue(TEXT("Preview Profile Socket returns its render component"),
		PreviewAttachment == PlaybackComponent);
	const FVector2D PivotLocal =
		Sprite->GetPivotPosition() - Sprite->GetSourceUV();
	TestFalse(TEXT("Preview parity fixture exercises fractional pivot compensation"),
		FMath::IsNearlyZero(FMath::Frac(PivotLocal.X))
			&& FMath::IsNearlyZero(FMath::Frac(PivotLocal.Y)));
	TestTrue(TEXT("Preview Profile Socket preserves the mirrored component rotation"),
		PreviewSocketTransform.GetRotation().Equals(
			PlaybackComponent->GetComponentQuat(),
			KINDA_SMALL_NUMBER));
	TestTrue(TEXT("Preview Profile Socket preserves nonuniform component scale"),
		PreviewSocketTransform.GetScale3D().Equals(
			PlaybackComponent->GetComponentScale(),
			KINDA_SMALL_NUMBER));

	FPaper2DPlusFrameCueContext RuntimeEquivalent = PreviewContext;
	RuntimeEquivalent.SetEvaluationMode(
		EPaper2DPlusFrameCueEvaluationMode::RuntimePlayback);
	FTransform RuntimeSocketTransform = FTransform::Identity;
	UPaperFlipbookComponent* RuntimeAttachment = nullptr;
	const EPaper2DPlusFrameCueAnchorResult RuntimeResult =
		UPaper2DPlusBlueprintLibrary::ResolveFrameCueAnchor(
			RuntimeEquivalent,
			EPaper2DPlusFrameCueAnchorKind::ProfileSocket,
			TEXT("WeaponHand"),
			RuntimeSocketTransform,
			RuntimeAttachment);
	TestEqual(TEXT("Equivalent runtime Profile Socket resolves"), RuntimeResult,
		EPaper2DPlusFrameCueAnchorResult::Success);
	TestTrue(TEXT("Preview and runtime use byte-consistent socket geometry"),
		PreviewSocketTransform.Equals(RuntimeSocketTransform, 0.001f));
	TestTrue(TEXT("Preview and runtime return the same attachment source"),
		RuntimeAttachment == PreviewAttachment);

	FPaper2DPlusFrameCueContext MissingProfile = PreviewContext;
	MissingProfile.CharacterProfile = nullptr;
	FTransform FailedTransform(
		FRotator(1.0f, 2.0f, 3.0f),
		FVector(4.0f, 5.0f, 6.0f),
		FVector(2.0f));
	UPaperFlipbookComponent* FailedAttachment = PlaybackComponent;
	const EPaper2DPlusFrameCueAnchorResult MissingProfileResult =
		UPaper2DPlusBlueprintLibrary::ResolveFrameCueAnchor(
			MissingProfile,
			EPaper2DPlusFrameCueAnchorKind::ProfileSocket,
			TEXT("WeaponHand"),
			FailedTransform,
			FailedAttachment);
	TestEqual(TEXT("Missing preview Profile fails explicitly"), MissingProfileResult,
		EPaper2DPlusFrameCueAnchorResult::MissingProfile);
	TestTrue(TEXT("Missing preview Profile resets the transform"),
		FailedTransform.Equals(FTransform::Identity));
	TestNull(TEXT("Missing preview Profile clears the attachment source"),
		FailedAttachment);

	FPaper2DPlusFrameCueContext MissingSubject = PreviewContext;
	MissingSubject.PlaybackComponent = nullptr;
	FailedTransform = ComponentTransform;
	FailedAttachment = PlaybackComponent;
	const EPaper2DPlusFrameCueAnchorResult MissingSubjectResult =
		UPaper2DPlusBlueprintLibrary::ResolveFrameCueAnchor(
			MissingSubject,
			EPaper2DPlusFrameCueAnchorKind::RenderOrigin,
			FString(),
			FailedTransform,
			FailedAttachment);
	TestEqual(TEXT("Missing preview subject fails explicitly"), MissingSubjectResult,
		EPaper2DPlusFrameCueAnchorResult::InvalidPlaybackComponent);
	TestTrue(TEXT("Missing preview subject resets the transform"),
		FailedTransform.Equals(FTransform::Identity));
	TestNull(TEXT("Missing preview subject clears the attachment source"),
		FailedAttachment);

	FailedTransform = ComponentTransform;
	FailedAttachment = PlaybackComponent;
	const EPaper2DPlusFrameCueAnchorResult MissingSocketResult =
		UPaper2DPlusBlueprintLibrary::ResolveFrameCueAnchor(
			PreviewContext,
			EPaper2DPlusFrameCueAnchorKind::ProfileSocket,
			TEXT("MissingSocket"),
			FailedTransform,
			FailedAttachment);
	TestEqual(TEXT("Missing preview socket fails explicitly"), MissingSocketResult,
		EPaper2DPlusFrameCueAnchorResult::SocketNotFound);
	TestTrue(TEXT("Missing preview socket resets the transform"),
		FailedTransform.Equals(FTransform::Identity));
	TestNull(TEXT("Missing preview socket clears the attachment source"),
		FailedAttachment);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCuePreviewStatusTextTest,
	"Paper2DPlus.FrameCues.Editor.Preview.ReadableStatusText",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCuePreviewStatusTextTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusFrameCuePreviewTest;

	// Pin an empty project registry to prove the ordinary preview message is independent of adapters.
	FScopedPreviewAdapterRegistry Registry(nullptr);
	FPaper2DPlusFrameCuePreviewHost Host;
	UPaper2DPlusFrameCuePreviewContext* Context = Host.GetContext();
	if (!TestNotNull(TEXT("Host context"), Context)) return false;
	TestEqual(TEXT("An empty project registry leaves the host with no adapters at all"),
		Host.GetAdapterCount(), NativeAdapterCount);

	UPaper2DPlusEditorTestRangeCue* Unsupported =
		NewObject<UPaper2DPlusEditorTestRangeCue>(GetTransientPackage());
	TestFalse(TEXT("Host reports a cue without a registered adapter"), Host.CanPreview(Unsupported));
	const FString Unavailable = SFrameEventPreviewCanvas::GetPreviewStatusText(Unsupported, Context).ToString();
	TestTrue(TEXT("Ready status describes automatic isolated-world behavior"),
		Unavailable.Contains(TEXT("Automatic"))
		&& Unavailable.Contains(TEXT("isolated")));
	UPaper2DPlusCharacterProfileAsset* Profile =
		NewObject<UPaper2DPlusCharacterProfileAsset>(GetTransientPackage());
	Profile->Flipbooks.AddDefaulted();
	Profile->Flipbooks[0].FrameEventData.FrameCues.Add(Unsupported);
	TSharedPtr<SFrameEventPreviewCanvas> Canvas =
		SNew(SFrameEventPreviewCanvas)
		.Asset(TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset>(Profile))
		.FlipbookIndex(0)
		.SelectedEventIndex(0)
		.PreviewHost(&Host);
	TestTrue(TEXT("Frame Cue preview retains Unreal's standard viewport grid"),
		Canvas->IsStandardGridEnabledForTests());
	const FString DisabledVisuals = Canvas->GetDisabledDesignerCueVisualsForTests();
	TestTrue(FString::Printf(
			TEXT("Frame Cue preview keeps designer-spawned translucent and editor visuals ")
			TEXT("enabled (disabled: %s)"),
			DisabledVisuals.IsEmpty() ? TEXT("none") : *DisabledVisuals),
		Canvas->AreDesignerCueVisualsEnabledForTests());
	const FString AccessibleStatus = Canvas->GetAccessibleSummaryText().ToString();
	TestTrue(TEXT("Canvas accessibility text exposes the same automatic-preview contract"),
		AccessibleStatus.Contains(TEXT("Automatic"))
		&& AccessibleStatus.Contains(TEXT("isolated")));
	FPreviewScene* InitialScene = Host.GetPreviewScene();
	UWorld* InitialWorld = Host.GetPreviewWorld();
	TestTrue(TEXT("Viewport client begins attached to the host scene"),
		Canvas->GetViewportPreviewSceneForTests() == InitialScene);
	FPaper2DPlusFrameCueContext DirtyContext =
		FPaper2DPlusFrameCueContext::MakePreview(0, 0, false);
	Host.PopulateContext(DirtyContext, Profile, nullptr, NAME_None);
	Host.Notify(Unsupported, DirtyContext);
	TestEqual(TEXT("A behavior-free Cue does not count ownerless world infrastructure as output"),
		Host.GetOwnedResourceCount(), 0);
	Host.Reset();
	TestTrue(TEXT("Deterministic reset preserves the viewport scene owner"),
		Host.GetPreviewScene() == InitialScene);
	TestTrue(TEXT("Deterministic reset replaces only the isolated world"),
		Host.GetPreviewWorld() && Host.GetPreviewWorld() != InitialWorld);
	TestTrue(TEXT("Viewport client remains attached to the stable scene owner"),
		Canvas->GetViewportPreviewSceneForTests() == InitialScene);
	UWorld* CleanWorld = Host.GetPreviewWorld();
	Host.Reset();
	TestTrue(TEXT("A clean seek avoids rebuilding an already-pristine world"),
		Host.GetPreviewWorld() == CleanWorld);

	// Any host-owned resource flips the same cue from "no output" to "active", so create one directly
	// rather than through an adapter: the status text reads the ledger, never the adapter registry.
	{
		UPaper2DPlusFrameCuePreviewContext::FScopedAdapterDispatch OwnerScope(
			*Context, NativeAdapterCount + 1);
		Context->ShowShape(FVector2D::ZeroVector, FVector2D(6.0f, 6.0f), FLinearColor::Green, 5.0f);
	}
	const FString Active = SFrameEventPreviewCanvas::GetPreviewStatusText(Unsupported, Context).ToString();
	TestTrue(TEXT("Active status is textual, not color-only"), Active.Contains(TEXT("Preview active")));

	Host.Reset();
	TestTrue(TEXT("Status fixture teardown leaves zero resources"), Context->GetResourceLedger().IsZero());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCuePreviewRegistryReentryTest,
	"Paper2DPlus.FrameCues.Editor.Preview.RegistryRefreshDefersAcrossCreatorCallbacks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCuePreviewRegistryReentryTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusFrameCuePreviewTest;

	UPaper2DPlusFrameCueEditorSettings* Settings =
		GetMutableDefault<UPaper2DPlusFrameCueEditorSettings>();
	if (!TestNotNull(TEXT("Frame Cue editor settings"), Settings)) return false;

	const TArray<TSoftClassPtr<UPaper2DPlusFrameCuePreviewAdapter>> SavedAdapters =
		Settings->PreviewAdapters;
	ON_SCOPE_EXIT
	{
		UPaper2DPlusFrameCueRegistryReentryAdapterTest::ResetFixture();
		UPaper2DPlusFrameCueEditorSettings* MutableSettings =
			GetMutableDefault<UPaper2DPlusFrameCueEditorSettings>();
		MutableSettings->PreviewAdapters = SavedAdapters;
		MutableSettings->NotifyPreviewAdaptersChanged();
	};

	Settings->PreviewAdapters.Reset();
	Settings->PreviewAdapters.Add(TSoftClassPtr<UPaper2DPlusFrameCuePreviewAdapter>(
		UPaper2DPlusFrameCueRegistryReentryAdapterTest::StaticClass()));
	Settings->NotifyPreviewAdaptersChanged();
	UPaper2DPlusFrameCueRegistryReentryAdapterTest::ResetFixture();

	{
		FPaper2DPlusFrameCuePreviewHost Host;
		UPaper2DPlusFrameCuePreviewContext* Preview = Host.GetContext();
		if (!TestNotNull(TEXT("Re-entry host context"), Preview)) return false;
		UPaper2DPlusFrameCueRegistryReentryAdapterTest::TrackedContext = Preview;
		TestEqual(TEXT("Every native adapter slot plus the re-entry fixture"),
			Host.GetAdapterCount(), NativeAdapterCount + 1);

		UPaper2DPlusEditorTestRangeCue* Cue =
			NewObject<UPaper2DPlusEditorTestRangeCue>(GetTransientPackage());
		FPaper2DPlusFrameCueContext Trigger =
			FPaper2DPlusFrameCueContext::MakePreview(0, 0, false);
		Trigger.Phase = EPaper2DPlusFrameCuePhase::Trigger;

		UPaper2DPlusFrameCueRegistryReentryAdapterTest::bBroadcastOnPreview = true;
		Host.Notify(Cue, Trigger);
		TestEqual(TEXT("Creator Preview executes exactly once"),
			UPaper2DPlusFrameCueRegistryReentryAdapterTest::PreviewCallCount, 1);
		TestEqual(TEXT("Deferred registry refresh performs one reset"),
			UPaper2DPlusFrameCueRegistryReentryAdapterTest::ResetCallCount, 1);
		TestTrue(TEXT("Deferred refresh cleans the creator resource ledger"),
			Preview->GetResourceLedger().IsZero());
		TestEqual(TEXT("Deferred refresh restores the same adapter count"),
			Host.GetAdapterCount(), NativeAdapterCount + 1);

		UPaper2DPlusFrameCueRegistryReentryAdapterTest::bBroadcastOnPreview = false;
		UPaper2DPlusFrameCueRegistryReentryAdapterTest::bBroadcastOnReset = true;
		Host.Notify(Cue, Trigger);
		const int32 ResetCallsBeforeExplicitReset =
			UPaper2DPlusFrameCueRegistryReentryAdapterTest::ResetCallCount;
		Host.Reset();
		TestEqual(TEXT("A registry edit during Reset does not recurse or double-reset"),
			UPaper2DPlusFrameCueRegistryReentryAdapterTest::ResetCallCount,
			ResetCallsBeforeExplicitReset + 1);
		TestTrue(TEXT("Re-entrant Reset still leaves a zero ledger"),
			Preview->GetResourceLedger().IsZero());
		TestEqual(TEXT("Reset-time refresh rebuilds one stable registry"),
			Host.GetAdapterCount(), NativeAdapterCount + 1);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCueProjectAdapterRegistryLifecycleTest,
	"Paper2DPlus.FrameCues.Editor.Preview.ProjectAdapterRegistryLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCueProjectAdapterRegistryLifecycleTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusFrameCuePreviewTest;

	UPaper2DPlusFrameCueEditorSettings* Settings =
		GetMutableDefault<UPaper2DPlusFrameCueEditorSettings>();
	if (!TestNotNull(TEXT("Frame Cue editor settings CDO"), Settings))
	{
		return false;
	}

	// Mutate only the in-memory CDO. SaveConfig is deliberately forbidden here: the automation test
	// must leave the project's authored registry byte-for-byte untouched, including on early return.
	const TArray<TSoftClassPtr<UPaper2DPlusFrameCuePreviewAdapter>> SavedAdapters =
		Settings->PreviewAdapters;
	ON_SCOPE_EXIT
	{
		UPaper2DPlusFrameCueEditorSettings* MutableSettings =
			GetMutableDefault<UPaper2DPlusFrameCueEditorSettings>();
		MutableSettings->PreviewAdapters = SavedAdapters;
		MutableSettings->NotifyPreviewAdaptersChanged();
	};

	Settings->PreviewAdapters.Reset();
	Settings->PreviewAdapters.Add(
		TSoftClassPtr<UPaper2DPlusFrameCuePreviewAdapter>(
			UPaper2DPlusFrameCueProjectAdapterBTest::StaticClass()));
	// The abstract base is a loadable, correctly typed soft class, but it is not a valid executable
	// registry row. It avoids missing-asset load noise while exercising the skip-and-continue path.
	Settings->PreviewAdapters.Add(
		TSoftClassPtr<UPaper2DPlusFrameCuePreviewAdapter>(
			UPaper2DPlusFrameCuePreviewAdapter::StaticClass()));
	Settings->PreviewAdapters.Add(
		TSoftClassPtr<UPaper2DPlusFrameCuePreviewAdapter>(
			UPaper2DPlusFrameCueProjectAdapterATest::StaticClass()));

	TStrongObjectPtr<UPaper2DPlusFrameCuePreviewContext> RetainedContext;
	TArray<FString> InitialAdapterPaths;
	TStrongObjectPtr<UBlueprint> CompileFixture(NewObject<UBlueprint>(
		GetTransientPackage(), NAME_None, RF_Transient));
	if (!TestNotNull(TEXT("Blueprint compile fixture"), CompileFixture.Get())
		|| !TestNotNull(TEXT("Editor is available in EditorContext automation"), GEditor))
	{
		return false;
	}

	{
		FPaper2DPlusFrameCuePreviewHost Host;
		FPaper2DPlusFrameCuePreviewHost PeerHost;
		UPaper2DPlusFrameCuePreviewContext* Preview = Host.GetContext();
		if (!TestNotNull(TEXT("Project-adapter host context"), Preview))
		{
			return false;
		}
		RetainedContext = TStrongObjectPtr<UPaper2DPlusFrameCuePreviewContext>(Preview);

		InitialAdapterPaths = Host.GetAdapterClassPaths();
		TestTrue(TEXT("Every simultaneously open host starts from the same registry"),
			PeerHost.GetAdapterClassPaths() == InitialAdapterPaths);
		TestEqual(TEXT("Every native adapter slot plus the two valid project adapters are installed"),
			InitialAdapterPaths.Num(), NativeAdapterCount + 2);
		for (int32 Index = 1; Index < FMath::Min(NativeAdapterCount, InitialAdapterPaths.Num()); ++Index)
		{
			TestTrue(TEXT("Native slots remain stable class-path order"),
				InitialAdapterPaths[Index - 1] < InitialAdapterPaths[Index]);
		}
		if (InitialAdapterPaths.Num() == NativeAdapterCount + 2)
		{
			TestEqual(TEXT("Project adapter B preserves its first authored slot"),
				InitialAdapterPaths[NativeAdapterCount],
				UPaper2DPlusFrameCueProjectAdapterBTest::StaticClass()->GetPathName());
			TestEqual(TEXT("Invalid middle row does not block later project adapter A"),
				InitialAdapterPaths[NativeAdapterCount + 1],
				UPaper2DPlusFrameCueProjectAdapterATest::StaticClass()->GetPathName());
		}

		// One authored row is invalid. The count is a property of the authored registry, not of how
		// many adapters the plugin ships, so it stays 1 with an empty native set.
		const TArray<FString>& RegistryProblems = Host.GetRegistryProblems();
		TestEqual(TEXT("Invalid registry row produces exactly one problem"), RegistryProblems.Num(), 1);
		if (RegistryProblems.Num() == 1)
		{
			TestTrue(TEXT("Registry problem identifies the authored row"),
				RegistryProblems[0].Contains(TEXT("Adapter 2")));
			TestTrue(TEXT("Registry problem explains that the row is invalid"),
				RegistryProblems[0].Contains(TEXT("invalid")));
			TestTrue(TEXT("Registry problem names the offending adapter class"),
				RegistryProblems[0].Contains(
					UPaper2DPlusFrameCuePreviewAdapter::StaticClass()->GetPathName()));
		}

		UPaper2DPlusEditorTestRangeCue* Cue =
			NewObject<UPaper2DPlusEditorTestRangeCue>(GetTransientPackage());
		FPaper2DPlusFrameCueContext Trigger =
			FPaper2DPlusFrameCueContext::MakePreview(4, 2, false);
		Trigger.Phase = EPaper2DPlusFrameCuePhase::Trigger;
		Host.Notify(Cue, Trigger);

		FPaper2DPlusPreviewResourceLedger Ledger = Preview->GetResourceLedger();
		TestEqual(TEXT("Project adapter B executed its distinguishable timer preview"), Ledger.Timers, 1);
		TestEqual(TEXT("Project adapter A executed its distinguishable shape preview"), Ledger.Shapes, 1);
		TestEqual(TEXT("Only the two project-adapter resources were allocated"), Ledger.Total(), 2);

		// Adapter owner tokens are their one-based executable slots. Releasing B must not disturb A,
		// which proves project resources participate in the same owner-scoped ledger as native ones.
		Preview->ReleaseResourcesForOwner(NativeAdapterCount + 1);
		Ledger = Preview->GetResourceLedger();
		TestEqual(TEXT("Releasing project B removes only its timer"), Ledger.Timers, 0);
		TestEqual(TEXT("Project A shape survives project B owner cleanup"), Ledger.Shapes, 1);
		Preview->ReleaseResourcesForOwner(NativeAdapterCount + 2);
		TestTrue(TEXT("Releasing both project owners leaves a zero ledger"),
			Preview->GetResourceLedger().IsZero());

		Host.Notify(Cue, Trigger);
		TestEqual(TEXT("Project resources exist before pre-compile teardown"),
			Preview->GetResourceLedger().Total(), 2);
		// The classless transient fixture is unresolvable, and the scoped compile reset deliberately
		// treats unresolvable as relevant, so this teardown still runs under the narrowed contract.
		GEditor->OnBlueprintPreCompile().Broadcast(CompileFixture.Get());
		TestTrue(TEXT("Blueprint pre-compile resets every project-adapter resource"),
			Preview->GetResourceLedger().IsZero());

		GEditor->OnBlueprintCompiled().Broadcast();
		TestTrue(TEXT("Compile-time rebuild itself keeps the resource ledger empty"),
			Preview->GetResourceLedger().IsZero());
		TestTrue(TEXT("Compile-time rebuild restores the exact deterministic adapter order"),
			Host.GetAdapterClassPaths() == InitialAdapterPaths);
		TestEqual(TEXT("Compile-time rebuild preserves the invalid-row diagnostic"),
			Host.GetRegistryProblems().Num(), 1);

		Host.Notify(Cue, Trigger);
		TestEqual(TEXT("Both project adapters execute after compile-time reconstruction"),
			Preview->GetResourceLedger().Total(), 2);

		// A project-wide registry edit resets and rebuilds every host, not only the editor that made it.
		Settings->PreviewAdapters.Reset();
		Settings->NotifyPreviewAdaptersChanged();
		TestTrue(TEXT("Registry broadcast tears down resources before replacing adapters"),
			Preview->GetResourceLedger().IsZero());
		TestEqual(TEXT("First open host refreshes from the empty project registry"),
			Host.GetAdapterCount(), NativeAdapterCount);
		TestEqual(TEXT("Peer open host refreshes from the empty project registry"),
			PeerHost.GetAdapterCount(), NativeAdapterCount);
		TestTrue(TEXT("An empty project registry also empties the invalid-row diagnostics"),
			Host.GetRegistryProblems().Num() == 0);

		TestTrue(TEXT("Registration helper adds and broadcasts one creator adapter"),
			Settings->RegisterPreviewAdapter(
				TSoftClassPtr<UPaper2DPlusFrameCuePreviewAdapter>(
					UPaper2DPlusFrameCueProjectAdapterATest::StaticClass()),
				false));
		TestEqual(TEXT("Registration refreshes the first open host"),
			Host.GetAdapterCount(), NativeAdapterCount + 1);
		TestEqual(TEXT("Registration refreshes the peer open host"),
			PeerHost.GetAdapterCount(), NativeAdapterCount + 1);
		TestFalse(TEXT("Duplicate registration is ignored without duplicating host adapters"),
			Settings->RegisterPreviewAdapter(
				TSoftClassPtr<UPaper2DPlusFrameCuePreviewAdapter>(
					UPaper2DPlusFrameCueProjectAdapterATest::StaticClass()),
				false));
		TestEqual(TEXT("Duplicate registration leaves the first host stable"),
			Host.GetAdapterCount(), NativeAdapterCount + 1);
		TestEqual(TEXT("Duplicate registration leaves the peer host stable"),
			PeerHost.GetAdapterCount(), NativeAdapterCount + 1);

		Settings->PreviewAdapters.Reset();
		Settings->PreviewAdapters.Add(
			TSoftClassPtr<UPaper2DPlusFrameCuePreviewAdapter>(
				UPaper2DPlusFrameCueProjectAdapterBTest::StaticClass()));
		Settings->PreviewAdapters.Add(
			TSoftClassPtr<UPaper2DPlusFrameCuePreviewAdapter>(
				UPaper2DPlusFrameCuePreviewAdapter::StaticClass()));
		Settings->PreviewAdapters.Add(
			TSoftClassPtr<UPaper2DPlusFrameCuePreviewAdapter>(
				UPaper2DPlusFrameCueProjectAdapterATest::StaticClass()));
		Settings->NotifyPreviewAdaptersChanged();
		TestTrue(TEXT("Registry broadcast restores the first host's deterministic order"),
			Host.GetAdapterClassPaths() == InitialAdapterPaths);
		TestTrue(TEXT("Registry broadcast restores the peer host's deterministic order"),
			PeerHost.GetAdapterClassPaths() == InitialAdapterPaths);

		Host.Notify(Cue, Trigger);
		TestEqual(TEXT("Both adapters execute after project-wide registry reconstruction"),
			Preview->GetResourceLedger().Total(), 2);
		// Deliberately leave resources live. The host destructor must perform the final teardown.
	}

	if (TestTrue(TEXT("Host destruction releases all retained-context resources"),
		RetainedContext.IsValid() && RetainedContext->GetResourceLedger().IsZero()))
	{
		// Broadcasting after host destruction is a crash-safety assertion for delegate removal: the
		// retained context must stay untouched because the dead host is no longer subscribed.
		GEditor->OnBlueprintPreCompile().Broadcast(CompileFixture.Get());
		GEditor->OnBlueprintCompiled().Broadcast();
		Settings->NotifyPreviewAdaptersChanged();
		TestTrue(TEXT("Destroyed host no longer reacts to editor compile delegates"),
			RetainedContext->GetResourceLedger().IsZero());
	}

	Settings->PreviewAdapters = SavedAdapters;
	Settings->NotifyPreviewAdaptersChanged();
	TestTrue(TEXT("In-memory project adapter registry is restored after host destruction"),
		Settings->PreviewAdapters == SavedAdapters);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCuePreviewSubjectKeyFrameSeekTest,
	"Paper2DPlus.FrameCues.Editor.Preview.SubjectSeekMapsKeyFrameIndices",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCuePreviewSubjectKeyFrameSeekTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusFrameCuePreviewTest;

	FScopedPreviewAdapterRegistry Registry(nullptr);
	FPaper2DPlusFrameCuePreviewHost Host;
	// Uneven FrameRuns are the GcdExact Aseprite import's ordinary output, and they are exactly
	// where key-frame indices and timeline frames diverge: seeking key frame N as a timeline frame
	// renders an EARLIER frame than the strip and dispatch use. This pins the time-based seek.
	const TArray<int32> FrameRuns = { 1, 3, 2, 4, 1 };
	UPaperFlipbook* Flipbook = nullptr;
	UPaper2DPlusCharacterProfileAsset* Profile =
		MakeRunLengthProfile(FrameRuns, /*FramesPerSecond=*/10.0f, Flipbook);
	if (!TestNotNull(TEXT("Run-length profile"), Profile)
		|| !TestNotNull(TEXT("Run-length flipbook"), Flipbook))
	{
		return false;
	}

	for (int32 KeyFrameIndex = 0; KeyFrameIndex < FrameRuns.Num(); ++KeyFrameIndex)
	{
		Host.SyncSubject(Profile, Flipbook, KeyFrameIndex, TEXT("RunLengthSeek"));
		UPaperFlipbookComponent* RenderComponent = Host.GetFlipbookComponent();
		if (!TestNotNull(TEXT("Subject render component"), RenderComponent))
		{
			return false;
		}
		TestEqual(
			FString::Printf(
				TEXT("Subject playback position maps back to requested key frame %d"),
				KeyFrameIndex),
			Flipbook->GetKeyFrameIndexAtTime(RenderComponent->GetPlaybackPosition()),
			KeyFrameIndex);
	}

	// A zero-fps flipbook has no time axis to seek; the guard leaves the subject parked at zero
	// instead of dividing by the frame rate.
	UPaperFlipbook* ZeroFpsFlipbook = nullptr;
	UPaper2DPlusCharacterProfileAsset* ZeroFpsProfile =
		MakeRunLengthProfile({ 1, 2 }, /*FramesPerSecond=*/0.0f, ZeroFpsFlipbook);
	Host.SyncSubject(ZeroFpsProfile, ZeroFpsFlipbook, 1, TEXT("RunLengthSeek"));
	if (UPaperFlipbookComponent* RenderComponent = Host.GetFlipbookComponent())
	{
		TestTrue(TEXT("Zero-fps seek stays at a safe zero position"),
			FMath::IsNearlyZero(RenderComponent->GetPlaybackPosition()));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCuePreviewCompileResetScopeTest,
	"Paper2DPlus.FrameCues.Editor.Preview.CompileResetScopedToRelevantBlueprints",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCuePreviewCompileResetScopeTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusFrameCuePreviewTest;

	if (!TestNotNull(TEXT("Editor is available in EditorContext automation"), GEditor))
	{
		return false;
	}
	FScopedPreviewAdapterRegistry Registry(nullptr);
	FPaper2DPlusFrameCuePreviewHost Host;
	UPaper2DPlusFrameCuePreviewContext* Preview = Host.GetContext();
	if (!TestNotNull(TEXT("Scoped-reset host context"), Preview))
	{
		return false;
	}

	// One long-lived context resource makes both reset observables readable: a skipped reset keeps
	// the ledger entry and the world; a performed reset clears the ledger and replaces the world.
	const auto DirtyPreview = [&]()
	{
		UPaper2DPlusFrameCuePreviewContext::FScopedAdapterDispatch OwnerScope(
			*Preview, NativeAdapterCount + 1);
		Preview->ShowShape(
			FVector2D::ZeroVector, FVector2D(6.0f, 6.0f), FLinearColor::Green, 60.0f);
	};

	DirtyPreview();
	const uint32 RevisionBeforeUnrelated = Host.GetPreviewWorldRevision();
	UBlueprint* UnrelatedBlueprint =
		NewObject<UBlueprint>(GetTransientPackage(), NAME_None, RF_Transient);
	UnrelatedBlueprint->ParentClass = AActor::StaticClass();
	GEditor->OnBlueprintPreCompile().Broadcast(UnrelatedBlueprint);
	TestEqual(TEXT("An unrelated gameplay Blueprint compile keeps preview resources"),
		Preview->GetResourceLedger().Total(), 1);
	TestTrue(TEXT("An unrelated gameplay Blueprint compile keeps the isolated world"),
		Host.GetPreviewWorldRevision() == RevisionBeforeUnrelated);

	UBlueprint* CueBlueprint =
		NewObject<UBlueprint>(GetTransientPackage(), NAME_None, RF_Transient);
	CueBlueprint->ParentClass = UPaper2DPlusCueBase::StaticClass();
	GEditor->OnBlueprintPreCompile().Broadcast(CueBlueprint);
	TestTrue(TEXT("A Cue-derived Blueprint compile still resets preview resources"),
		Preview->GetResourceLedger().IsZero());
	TestTrue(TEXT("A Cue-derived Blueprint compile still replaces the isolated world"),
		Host.GetPreviewWorldRevision() > RevisionBeforeUnrelated);

	DirtyPreview();
	UBlueprint* AdapterBlueprint =
		NewObject<UBlueprint>(GetTransientPackage(), NAME_None, RF_Transient);
	AdapterBlueprint->ParentClass = UPaper2DPlusFrameCuePreviewAdapter::StaticClass();
	GEditor->OnBlueprintPreCompile().Broadcast(AdapterBlueprint);
	TestTrue(TEXT("A preview-adapter Blueprint compile still resets preview resources"),
		Preview->GetResourceLedger().IsZero());

	// Lineage alone says this Blueprint is unrelated, but its OLD generated class has a live
	// instance in the preview world (the subject's render component), so reinstancing it would
	// leave stale pointers behind a skipped reset.
	DirtyPreview();
	UBlueprint* InstancedClassBlueprint =
		NewObject<UBlueprint>(GetTransientPackage(), NAME_None, RF_Transient);
	InstancedClassBlueprint->ParentClass = AActor::StaticClass();
	InstancedClassBlueprint->GeneratedClass = UPaperFlipbookComponent::StaticClass();
	GEditor->OnBlueprintPreCompile().Broadcast(InstancedClassBlueprint);
	TestTrue(TEXT("A compile whose old class is live in the preview world still resets"),
		Preview->GetResourceLedger().IsZero());

	// A Blueprint with no resolvable classes could be anything, including a Cue Type; the scoped
	// reset deliberately treats unresolvable as relevant.
	DirtyPreview();
	UBlueprint* UnresolvableBlueprint =
		NewObject<UBlueprint>(GetTransientPackage(), NAME_None, RF_Transient);
	GEditor->OnBlueprintPreCompile().Broadcast(UnresolvableBlueprint);
	TestTrue(TEXT("An unresolvable Blueprint compile fails conservative and resets"),
		Preview->GetResourceLedger().IsZero());

	Host.Reset();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
