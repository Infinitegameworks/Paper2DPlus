// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Paper2DPlusAppearanceRenderPolicy.generated.h"

class UTextureRenderTarget2D;
struct FPaper2DPlusAppearanceCompositeCache;
struct FPaper2DPlusAppearanceRenderBackend;

/** Exclusive client-local visual state for a Runtime Customizable appearance. */
enum class EPaper2DPlusAppearanceRenderState : uint8
{
	Disabled,
	LiveFallback,
	BuildingComposite,
	AwaitingKeyFrame,
	CompositeReady
};

/**
 * Pure, worldless hybrid-render policy. Gameplay and replication commit before this policy is entered.
 * A cache miss therefore remains visually exact through live children until a complete matching frame can
 * replace them at a key-frame boundary. There is never a partially-built or double-visible representation.
 */
struct PAPER2DPLUS_API FPaper2DPlusAppearanceRenderPolicy
{
	static constexpr int32 MaxIndependentChannels = 2;

	void Reset()
	{
		State = EPaper2DPlusAppearanceRenderState::Disabled;
		RemainingBuildUnits = 0;
		bLiveFallbackVisible = false;
		bCompositeVisible = false;
	}

	void BeginRequest(bool bCacheHit, int32 InBuildUnits)
	{
		RemainingBuildUnits = FMath::Max(0, InBuildUnits);
		bLiveFallbackVisible = true;
		bCompositeVisible = false;
		State = bCacheHit || RemainingBuildUnits == 0
			? EPaper2DPlusAppearanceRenderState::AwaitingKeyFrame
			: EPaper2DPlusAppearanceRenderState::BuildingComposite;
	}

	void CompleteBuildUnits(int32 CompletedUnits)
	{
		if (State != EPaper2DPlusAppearanceRenderState::BuildingComposite || CompletedUnits <= 0)
		{
			return;
		}
		RemainingBuildUnits = FMath::Max(0, RemainingBuildUnits - CompletedUnits);
		if (RemainingBuildUnits == 0)
		{
			State = EPaper2DPlusAppearanceRenderState::AwaitingKeyFrame;
		}
	}

	void MarkCompositePrepared()
	{
		if (State == EPaper2DPlusAppearanceRenderState::BuildingComposite)
		{
			RemainingBuildUnits = 0;
			State = EPaper2DPlusAppearanceRenderState::AwaitingKeyFrame;
		}
	}

	void FailToLiveFallback()
	{
		RemainingBuildUnits = 0;
		State = EPaper2DPlusAppearanceRenderState::LiveFallback;
		bLiveFallbackVisible = true;
		bCompositeVisible = false;
	}

	bool TryHandoffAtKeyFrame(bool bMatchingFrameReady)
	{
		if (State != EPaper2DPlusAppearanceRenderState::AwaitingKeyFrame || !bMatchingFrameReady)
		{
			return false;
		}
		State = EPaper2DPlusAppearanceRenderState::CompositeReady;
		bLiveFallbackVisible = false;
		bCompositeVisible = true;
		return true;
	}

	EPaper2DPlusAppearanceRenderState GetState() const { return State; }
	int32 GetRemainingBuildUnits() const { return RemainingBuildUnits; }
	bool IsLiveFallbackVisible() const { return bLiveFallbackVisible; }
	bool IsCompositeVisible() const { return bCompositeVisible; }
	bool NeedsBuildWork() const { return State == EPaper2DPlusAppearanceRenderState::BuildingComposite; }

private:
	EPaper2DPlusAppearanceRenderState State = EPaper2DPlusAppearanceRenderState::Disabled;
	int32 RemainingBuildUnits = 0;
	bool bLiveFallbackVisible = false;
	bool bCompositeVisible = false;
};

/**
 * One cacheable, transient prepared animation. The cache roots it while resident; render components hold a
 * UPROPERTY to the same object while consuming it. Consequently LRU eviction removes reuse eligibility without
 * collecting a frame that an on-screen character still owns. No instance is saved or placed in a content package.
 */
UCLASS(Transient)
class PAPER2DPLUS_API UPaper2DPlusAppearanceCompositeResource : public UObject
{
	GENERATED_BODY()

public:
	int32 GetFrameCount() const { return FrameTargets.Num(); }
	bool IsFrameReady(int32 FrameIndex) const
	{
		return FrameReady.IsValidIndex(FrameIndex) && FrameReady[FrameIndex] != 0
			&& FrameTargets.IsValidIndex(FrameIndex) && FrameTargets[FrameIndex] != nullptr;
	}
	bool IsPrepared() const
	{
		return FrameTargets.Num() > 0 && NumReadyFrames == FrameTargets.Num() && !bInvalidated;
	}
	bool IsInvalidated() const { return bInvalidated; }
	UTextureRenderTarget2D* GetFrameTarget(int32 FrameIndex) const
	{
		return FrameTargets.IsValidIndex(FrameIndex) ? FrameTargets[FrameIndex] : nullptr;
	}
	const FIntPoint& GetPixelSize() const { return PixelSize; }
	const FVector2D& GetPivotPixels() const { return PivotPixels; }
	float GetPixelsPerUnrealUnit() const { return PixelsPerUnrealUnit; }
	uint64 GetCacheReservationBytes() const { return CacheReservationBytes; }
	const FString& GetCacheIdentity() const { return CacheIdentity; }

private:
	friend FPaper2DPlusAppearanceCompositeCache;
	friend FPaper2DPlusAppearanceRenderBackend;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextureRenderTarget2D>> FrameTargets;

	UPROPERTY(Transient)
	TArray<uint8> FrameReady;

	FIntPoint PixelSize = FIntPoint::ZeroValue;
	FVector2D PivotPixels = FVector2D::ZeroVector;
	float PixelsPerUnrealUnit = 1.0f;
	uint64 CacheReservationBytes = 0;
	FString CacheIdentity;
	TSet<FSoftObjectPath> DependencyObjects;
	TSet<FName> DependencyPackages;
	int32 NumReadyFrames = 0;
	bool bInvalidated = false;
};
