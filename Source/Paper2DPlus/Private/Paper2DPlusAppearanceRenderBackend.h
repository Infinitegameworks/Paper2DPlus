// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Paper2DPlusAppearanceTypes.h"
#include "UObject/StrongObjectPtr.h"

class UMaterialInterface;
class UMaterialInstanceDynamic;
class UPaper2DPlusCharacterLayerAsset;
class UPaper2DPlusCharacterProfileAsset;
class UPaperFlipbook;
class UPaperSprite;
class UStaticMesh;
class UStaticMeshComponent;
class UTexture2D;
class UTextureRenderTarget2D;
class UPaper2DPlusAppearanceCompositeResource;
struct FPaper2DPlusAppearanceCompositeCache;
struct FPaper2DPlusAppearanceRenderBackend;

/** One visible layer draw in one key frame, already resolved into canonical paint order. */
struct FPaper2DPlusAppearanceLayerDraw
{
	FString LayerName;
	TObjectPtr<UPaperSprite> Sprite = nullptr;
	TObjectPtr<UMaterialInterface> RecolorMaterial = nullptr;
	TObjectPtr<UTexture2D> PaletteLUT = nullptr;
	FVector2D TotalOffsetPx = FVector2D::ZeroVector;
	int32 PaletteRow = 0;
	float RecolorIntensity = 0.0f;
	int32 PaintOrder = 0;
	bool bRecolorEnabled = false;
	/** Explicit opt-in: the live material implements the documented luminance/LUT/unchanged-alpha contract. */
	bool bCompositeRecolorContractVerified = false;
};

struct FPaper2DPlusAppearanceFrameRecipe
{
	TArray<FPaper2DPlusAppearanceLayerDraw> BaseDraws;
};

/**
 * Immutable input to one prepared base-composite build. Independent channels are named in the key but omitted
 * from BaseDraws; they remain live and are capped by the policy at two. All soft references must be resolved at
 * request/animation boundaries before this recipe is admitted, so build/frame playback never synchronously loads.
 */
struct FPaper2DPlusAppearanceBuildRecipe
{
	TObjectPtr<UPaper2DPlusCharacterLayerAsset> LayerAsset = nullptr;
	TObjectPtr<UPaper2DPlusCharacterProfileAsset> CharacterProfile = nullptr;
	TObjectPtr<UPaperFlipbook> Flipbook = nullptr;
	FPaper2DPlusAppearanceDescriptor Appearance;
	FString CanonicalAnimationName;
	TArray<FString> OrderedBaseLayerNames;
	TArray<FString> IndependentChannelLayerNames;
	TArray<FPaper2DPlusAppearanceFrameRecipe> Frames;
	FIntPoint PixelSize = FIntPoint::ZeroValue;
	FVector2D PivotPixels = FVector2D::ZeroVector;
	float PixelsPerUnrealUnit = 1.0f;

	/** Deterministic cache-admission reservation, not measured RHI allocation. */
	uint64 EstimateCacheReservationBytes() const;

private:
	friend struct FPaper2DPlusAppearanceRenderBackend;

	/** Set only after every frame passes admission. The recipe is immutable after that boundary. */
	bool bWholeRecipeValidated = false;
};

/** Collision-safe cache key: hash accelerates lookup, canonical bytes remain the equality authority. */
struct FPaper2DPlusAppearanceCompositeKey
{
	TArray<uint8> CanonicalBytes;
	uint32 FastHash = 0;
	FString DebugIdentity;

	bool IsValid() const { return CanonicalBytes.Num() > 0; }
	bool operator==(const FPaper2DPlusAppearanceCompositeKey& Other) const
	{
		return CanonicalBytes == Other.CanonicalBytes;
	}

	friend uint32 GetTypeHash(const FPaper2DPlusAppearanceCompositeKey& Key)
	{
		return Key.FastHash;
	}
};

struct FPaper2DPlusAppearanceScratchStats
{
	/** Logical RDG-transient scratch requests; physical allocation/aliasing remains RDG-owned. */
	uint64 TransientScratchRequests = 0;
	uint64 FramesSubmitted = 0;
	uint64 LargestSubmissionScratchBytes = 0;
	uint64 PeakSubmittedScratchBytesPerGameFrame = 0;
	int32 PeakSubmittedScratchRequestsPerGameFrame = 0;
};

#if WITH_EDITOR
/** Instrumentation returned by the focused validation-scaling seam. */
struct FPaper2DPlusAppearanceValidationStats
{
	uint64 WholeRecipeValidationRuns = 0;
	uint64 FrameSafetyValidationRuns = 0;
};
#endif

/**
 * Strong, non-blocking ownership token for one submitted key frame. The output target remains rooted until the
 * cache observes completion. Render-thread failure is reported through the same token, so a queued command can
 * never be mistaken for a prepared frame.
 */
struct FPaper2DPlusAppearanceBuildSubmission
{
	explicit FPaper2DPlusAppearanceBuildSubmission(UTextureRenderTarget2D* InOutputTarget);

	bool IsComplete() const;
	bool DidRenderSucceed() const;
	UTextureRenderTarget2D* GetOutputTarget() const { return OutputTarget.Get(); }
#if WITH_DEV_AUTOMATION_TESTS
	/** Proof-only query. The caller must first block until the GPU is idle and flush render commands. */
	bool DidRenderSucceedAfterGpuIdleForTests() const;
#endif

private:
	friend FPaper2DPlusAppearanceCompositeCache;
	friend FPaper2DPlusAppearanceRenderBackend;
	bool IsRenderCommandComplete() const;
	bool DidRenderCommandSucceed() const;

	struct FCompletionState;
	TStrongObjectPtr<UTextureRenderTarget2D> OutputTarget;
	TSharedRef<FCompletionState, ESPMode::ThreadSafe> CompletionState;
};

/** Pure preparation/key surface. GPU execution is deliberately kept behind this proof-gated seam. */
struct FPaper2DPlusAppearanceRenderBackend
{
	static constexpr int32 MaxCompositeDimension = 2048;

	/** Exact deterministic identity: normalized descriptor + source/package revisions + geometry/material/recolor data. */
	static FPaper2DPlusAppearanceCompositeKey MakeCacheKey(const FPaper2DPlusAppearanceBuildRecipe& Recipe);

	/** One-time immutable admission proof. Fail closed when a recipe cannot satisfy the composite contract. */
	static bool ValidateRecipe(FPaper2DPlusAppearanceBuildRecipe& Recipe, FString& OutError);

	/** Collect object and package dependencies for edit-time invalidation and strong cache ownership. */
	static void CollectDependencies(
		const FPaper2DPlusAppearanceBuildRecipe& Recipe,
		TSet<FSoftObjectPath>& OutObjects,
		TSet<FName>& OutPackages);

	/** Manual GC bridge for the cache's non-UObject pending recipes. */
	static void AddReferencedObjects(FReferenceCollector& Collector, FPaper2DPlusAppearanceBuildRecipe& Recipe);

	/**
	 * Submit one admitted key frame to the GPU. The whole recipe is not rescanned; only this frame's source residency
	 * and safety are rechecked. The returned strong token must be retained and polled without flushing.
	 */
	static bool BuildFrame(
		UObject* WorldContextObject,
		UPaper2DPlusAppearanceCompositeResource* Resource,
		const FPaper2DPlusAppearanceBuildRecipe& Recipe,
		int32 FrameIndex,
		TSharedPtr<FPaper2DPlusAppearanceBuildSubmission, ESPMode::ThreadSafe>& OutSubmission,
		FString& OutError);

	/** Configure the one steady-state base primitive from already hard-referenced engine assets. Never loads. */
	static bool ConfigureCompositePrimitive(
		UStaticMeshComponent* Primitive,
		UStaticMesh* PlaneMesh,
		UMaterialInterface* DisplayMaterial,
		const UPaper2DPlusAppearanceCompositeResource* Resource,
		UMaterialInstanceDynamic*& OutMID,
		FString& OutError);

	/** Per-key-frame steady-state operation: one texture parameter write, no load/rebuild/readback. */
	static bool BindPreparedFrame(
		UStaticMeshComponent* Primitive,
		UMaterialInstanceDynamic* MID,
		const UPaper2DPlusAppearanceCompositeResource* Resource,
		int32 FrameIndex);

	static FPaper2DPlusAppearanceScratchStats GetScratchStats();
#if WITH_EDITOR
	static void ResetScratchForTests();
	static void ResetValidationStatsForTests();
	static FPaper2DPlusAppearanceValidationStats GetValidationStatsForTests();
	static bool ValidateAdmittedFrameForTests(
		const FPaper2DPlusAppearanceBuildRecipe& Recipe,
		int32 FrameIndex,
		FString& OutError);
#endif

private:
	static bool ValidateAdmittedFrame(
		const FPaper2DPlusAppearanceBuildRecipe& Recipe,
		int32 FrameIndex,
		FString& OutError);
};
