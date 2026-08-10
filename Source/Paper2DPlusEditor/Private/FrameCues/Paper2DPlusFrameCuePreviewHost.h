// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AudioDeviceManager.h"
#include "Templates/Function.h"
#include "Templates/UniquePtr.h"
#include "UObject/StrongObjectPtr.h"

class AActor;
class FPreviewScene;
class UActorComponent;
class UPaper2DPlusCueBase;
class UPaper2DPlusCharacterProfileAsset;
class UPaper2DPlusCharacterProfileComponent;
class UPaper2DPlusFrameCuePreviewAdapter;
class UPaper2DPlusFrameCuePreviewContext;
class UPaperFlipbook;
class UPaperFlipbookComponent;
class UPaperSprite;
class UPaperSpriteComponent;
class USceneComponent;
class UWorld;
class UBlueprint;
struct FPaper2DPlusFrameCueContext;

/** One already-resolved, load-free Character Layer sprite for the world-backed preview subject. */
struct FPaper2DPlusPreviewLayerSprite
{
	UPaperSprite* Sprite = nullptr;
	FVector2D TotalOffsetPx = FVector2D::ZeroVector;
};

/**
 * Per-editor owner for the isolated Cue behavior world and optional adapter resources.
 *
 * The subject actor/component pair is deliberately inert: the editor's explicit scrub dispatcher is
 * the one lifecycle authority. Cue behavior still receives ordinary actor/component/world context,
 * while seek/stop/compile teardown can clean everything without touching the open level.
 */
class FPaper2DPlusFrameCuePreviewHost
{
public:
	/**
	 * Adapters the plugin seeds itself, ahead of the project registry. Zero: the plugin ships cue
	 * machinery, not a cue catalogue, so there is no built-in Cue Type left for a built-in adapter to
	 * preview. Project adapters therefore occupy slots 0 through PreviewAdapters.Num() - 1.
	 *
	 * Named rather than spelled 0 because it is the base of every adapter-slot calculation. Anything
	 * that needs the Nth project adapter's slot must say NativeAdapterCount + N.
	 */
	static constexpr int32 NativeAdapterCount = 0;

	/**
	 * The preview context's resource OWNER TOKEN for one adapter slot.
	 *
	 * Tokens are one-based: the context sits at INDEX_NONE between dispatches, and a one-based token
	 * keeps the first adapter's ledger entries visibly distinct from a default-constructed zero. This
	 * is the single place that offset exists; never rebuild a token from a literal.
	 */
	static constexpr int32 GetOwnerTokenForAdapterIndex(int32 AdapterIndex) { return AdapterIndex + 1; }

	FPaper2DPlusFrameCuePreviewHost();
	~FPaper2DPlusFrameCuePreviewHost();

	/** Synchronizes the inert preview subject with the editor's current animation/frame. */
	void SyncSubject(
		UPaper2DPlusCharacterProfileAsset* Profile,
		UPaperFlipbook* Flipbook,
		int32 FrameIndex,
		FName AnimationName = NAME_None);
	void SetSubjectVisible(bool bVisible);
	/** Synchronizes visible Character Layer art as collision-free world components. */
	void SyncLayerSprites(const TArray<FPaper2DPlusPreviewLayerSprite>& Layers);
	/** Projects optional adapter-owned effect sprites into the same preview world/camera. */
	void SyncAdapterVisuals();

	/**
	 * Fills a context from the supplied subject identity and remembers it for later forced Ends.
	 * The resulting snapshot carries the exact Profile plus this host's registered render component;
	 * its explicit editor evaluation mode is preserved.
	 */
	void PopulateContext(
		FPaper2DPlusFrameCueContext& CueContext,
		UPaper2DPlusCharacterProfileAsset* Profile,
		UPaperFlipbook* Flipbook,
		FName AnimationName);
	/** Fills a context from the last synchronized outgoing identity without consulting runtime caches. */
	void PopulateContext(FPaper2DPlusFrameCueContext& CueContext);

	void Notify(UPaper2DPlusCueBase* Cue, const FPaper2DPlusFrameCueContext& Context);
	void Tick(float DeltaSeconds);
	void Reset();
	/**
	 * Defers compile/registry teardown across the editor's complete lifecycle transaction, including
	 * Cue behavior. Calls may nest with adapter callbacks; the outer End flushes pending work.
	 */
	void BeginDispatchScope();
	void EndDispatchScope();
	UPaper2DPlusFrameCuePreviewContext* GetContext() const { return Context.Get(); }
	bool HasOwnedResources() const;
	int32 GetOwnedResourceCount() const;
	int32 GetAdapterCount() const { return Adapters.Num(); }
	bool CanPreview(const UPaper2DPlusCueBase* Cue) const;
	TArray<FString> GetAdapterClassPaths() const;
	const TArray<FString>& GetRegistryProblems() const { return RegistryProblems; }

	/** Preview-world/test seams. None of these ever return the editor's open level. */
	FPreviewScene* GetPreviewScene() const { return PreviewScene.Get(); }
	UWorld* GetPreviewWorld() const;
	/** Changes only when the isolated UWorld is replaced beneath the stable preview-scene owner. */
	uint32 GetPreviewWorldRevision() const { return PreviewWorldRevision; }
	/** Changes when destructive Cue behavior forces the inert preview subject to be reconstructed. */
	uint32 GetPreviewSubjectRevision() const { return PreviewSubjectRevision; }
	AActor* GetPreviewActor() const { return PreviewActor.Get(); }
	UPaperFlipbookComponent* GetFlipbookComponent() const { return FlipbookComponent.Get(); }
	FBox GetSubjectVisualBounds() const;
	UPaper2DPlusCharacterProfileComponent* GetProfileComponent() const
	{
		return ProfileComponent.Get();
	}
	/**
	 * Binds the live viewport client to this stable FPreviewScene owner. Routine reset republishes the
	 * same pointer after replacing its UWorld; final destruction publishes nullptr first.
	 */
	void SetPreviewSceneChanged(TFunction<void(FPreviewScene*)> InCallback)
	{
		PreviewSceneChanged = MoveTemp(InCallback);
		if (PreviewSceneChanged)
		{
			PreviewSceneChanged(PreviewScene.Get());
		}
	}

	/**
	 * Called immediately before Blueprint-precompile or registry-replacement teardown. The editor
	 * binds its paired-End funnel here; a Reset requested by that callback is deferred so cleanup
	 * still runs exactly once.
	 */
	void SetBeforeHardReset(TFunction<void()> InCallback)
	{
		BeforeHardReset = MoveTemp(InCallback);
	}

private:
	TStrongObjectPtr<UPaper2DPlusFrameCuePreviewContext> Context;
	TUniquePtr<FPreviewScene> PreviewScene;
	/** Dedicated device makes world-scoped fire-and-forget sound cleanup truly host-local. */
	FAudioDeviceHandle PreviewAudioDevice;
	TWeakObjectPtr<AActor> PreviewActor;
	TWeakObjectPtr<USceneComponent> PreviewRootComponent;
	TWeakObjectPtr<UPaperFlipbookComponent> FlipbookComponent;
	TWeakObjectPtr<UPaper2DPlusCharacterProfileComponent> ProfileComponent;
	TArray<TWeakObjectPtr<UPaperSpriteComponent>> LayerSpriteComponents;
	TMap<int32, TWeakObjectPtr<UPaperFlipbookComponent>> AdapterEffectComponents;
	TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset> SyncedProfile;
	TWeakObjectPtr<UPaperFlipbook> SyncedFlipbook;
	FName SyncedAnimationName;
	int32 SyncedFrameIndex = INDEX_NONE;
	TSet<TWeakObjectPtr<AActor>> SpawnedActors;
	TSet<TWeakObjectPtr<UActorComponent>> BaselineComponents;
	TSet<TWeakObjectPtr<UActorComponent>> TrackedWorldComponents;
	TArray<TStrongObjectPtr<UPaper2DPlusFrameCuePreviewAdapter>> Adapters;
	TArray<FString> RegistryProblems;
	FDelegateHandle ActorSpawnedHandle;
	FDelegateHandle PreviewAdaptersChangedHandle;
	FDelegateHandle BlueprintPreCompileHandle;
	FDelegateHandle BlueprintCompiledHandle;
	TFunction<void()> BeforeHardReset;
	TFunction<void(FPreviewScene*)> PreviewSceneChanged;
	uint32 PreviewWorldRevision = 0;
	uint32 PreviewSubjectRevision = 0;
	int32 DispatchScopeDepth = 0;
	bool bRegistryRefreshPending = false;
	bool bResetPending = false;
	bool bRebuildPending = false;
	bool bHardResetPending = false;
	bool bFlushingDeferredWork = false;
	bool bInvokingBeforeHardReset = false;
	bool bSpawningPreviewSubject = false;
	bool bSubjectStateDirty = true;
	bool bTrackedWorldResourcesDirty = false;
	/** True after Cue behavior/adapters could have mutated the current world generation. */
	bool bPreviewWorldMayBeDirty = false;
	bool bDestroyingPreviewHost = false;

	void InvokeBeforeHardReset();
	void CreatePreviewScene();
	void ConfigurePreviewWorld();
	void ReplacePreviewWorld();
	void CreatePreviewSubject();
	bool EnsurePreviewSubject();
	void DestroyPreviewScene();
	void RegisterBaselineComponent(UActorComponent* Component);
	void CaptureBaselineComponents();
	void RefreshTrackedWorldResources();
	void ForEachPreviewWorldComponent(
		TFunctionRef<void(UActorComponent*)> Visitor) const;
	void HandleActorSpawned(AActor* Actor);
	bool IsBlueprintRelevantToPreview(const UBlueprint* Blueprint) const;
	void ResetPreviewResourcesNow();
	bool IsBaselineComponent(const UActorComponent* Component) const;
	void AddAdapterClass(UClass* AdapterClass);
	void RebuildAdapters();
	void FlushDeferredWork();
	void HandlePreviewAdaptersChanged();
	void HandleBlueprintPreCompile(UBlueprint* Blueprint);
	void HandleBlueprintCompiled();
};
