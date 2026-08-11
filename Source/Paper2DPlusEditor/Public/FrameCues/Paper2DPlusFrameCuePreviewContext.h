// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Paper2DPlusEffectProfileAsset.h"
#include "Paper2DPlusFrameCuePreviewContext.generated.h"

class UAudioComponent;
class USoundBase;
class UPaper2DPlusCueBase;
class FPaper2DPlusFrameCuePreviewHost;
class UWorld;

USTRUCT(BlueprintType)
struct PAPER2DPLUSEDITOR_API FPaper2DPlusPreviewResourceHandle
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Frame Cue Preview")
	int32 Value = INDEX_NONE;

	bool IsValid() const { return Value != INDEX_NONE; }
};

/** Canvas-readable projectile proxy owned by the preview context, never a gameplay actor. */
struct FPaper2DPlusPreviewProjectile
{
	int32 Handle = INDEX_NONE;
	FVector2D Position = FVector2D::ZeroVector;
	FVector2D Velocity = FVector2D::ZeroVector;
	TArray<FVector2D> Trajectory;
	float Age = 0.0f;
	float Lifetime = 1.5f;
	FLinearColor Color = FLinearColor(1.0f, 0.45f, 0.1f);
};

/** Canvas-readable effect proxy. It owns no actor/component and advances only while the editor host ticks. */
USTRUCT()
struct FPaper2DPlusPreviewEffect
{
	GENERATED_BODY()

	UPROPERTY()
	int32 Handle = INDEX_NONE;

	UPROPERTY()
	FPaper2DPlusEffectSpawnSettings Settings;

	UPROPERTY()
	float Age = 0.0f;

	UPROPERTY()
	float Lifetime = 0.0f;

	/** Exact placement that created this proxy. Used by the canvas authoring hit-test; never gameplay. */
	UPROPERTY(Transient)
	TWeakObjectPtr<UPaper2DPlusCueBase> SourceCue;
};

/** Canvas-readable primitive offered to creator adapters without exposing Slate or a preview world. */
struct FPaper2DPlusPreviewShape
{
	int32 Handle = INDEX_NONE;
	FVector2D Center = FVector2D::ZeroVector;
	FVector2D Size = FVector2D(12.0f, 12.0f);
	FLinearColor Color = FLinearColor::White;
	float Remaining = 0.0f;
};

/** Exact, testable ownership ledger for the opaque resources held by one preview context. */
USTRUCT(BlueprintType)
struct PAPER2DPLUSEDITOR_API FPaper2DPlusPreviewResourceLedger
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Frame Cue Preview") int32 Audio = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Frame Cue Preview") int32 Effects = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Frame Cue Preview") int32 Projectiles = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Frame Cue Preview") int32 Overlays = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Frame Cue Preview") int32 Shapes = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Frame Cue Preview") int32 CameraOffsets = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Frame Cue Preview") int32 Timers = 0;

	int32 Total() const { return Audio + Effects + Projectiles + Overlays + Shapes + CameraOffsets + Timers; }
	bool IsZero() const { return Total() == 0; }
};

/**
 * Safe editor-only operations available to creator-authored preview adapters. Every operation returns
 * an opaque handle and every resource is owned by this context, so reset/seek/tab teardown is complete.
 */
UCLASS(BlueprintType)
class PAPER2DPLUSEDITOR_API UPaper2DPlusFrameCuePreviewContext : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Paper2DPlus|Frame Cue Preview")
	FPaper2DPlusPreviewResourceHandle PlaySound(
		USoundBase* Sound,
		float VolumeMultiplier = 1.0f,
		float PitchMultiplier = 1.0f,
		float StartTime = 0.0f);

	UFUNCTION(BlueprintCallable, Category = "Paper2DPlus|Frame Cue Preview")
	FPaper2DPlusPreviewResourceHandle SpawnProjectileProxy(
		FVector2D SpawnOffset,
		FVector2D LaunchVelocity,
		float Lifetime = 1.5f,
		FLinearColor Color = FLinearColor(1.0f, 0.45f, 0.1f));

	UFUNCTION(BlueprintCallable, Category = "Paper2DPlus|Frame Cue Preview")
	FPaper2DPlusPreviewResourceHandle SpawnEffectProxy(
		const FPaper2DPlusEffectSpawnSettings& Settings,
		float Lifetime = 0.0f,
		UPaper2DPlusCueBase* SourceCue = nullptr);

	UFUNCTION(BlueprintCallable, Category = "Paper2DPlus|Frame Cue Preview")
	FPaper2DPlusPreviewResourceHandle ShowOverlay(FLinearColor Color, float Duration = 0.1f);

	UFUNCTION(BlueprintCallable, Category = "Paper2DPlus|Frame Cue Preview")
	FPaper2DPlusPreviewResourceHandle ShowShape(
		FVector2D Center,
		FVector2D Size,
		FLinearColor Color,
		float Duration = 0.1f);

	/** Safe approximation only: offsets the editor canvas; it never runs a UCameraShakeBase graph. */
	UFUNCTION(BlueprintCallable, Category = "Paper2DPlus|Frame Cue Preview")
	FPaper2DPlusPreviewResourceHandle ShowCameraShakeApproximation(float Scale, float Duration = 0.25f);

	/** Host-owned lifetime token for adapter work that needs cleanup accounting but no gameplay timer. */
	UFUNCTION(BlueprintCallable, Category = "Paper2DPlus|Frame Cue Preview")
	FPaper2DPlusPreviewResourceHandle CreateTimer(float Duration);

	UFUNCTION(BlueprintCallable, Category = "Paper2DPlus|Frame Cue Preview")
	void ReleaseResource(FPaper2DPlusPreviewResourceHandle Handle);

	void Tick(float DeltaSeconds);
	const TArray<FPaper2DPlusPreviewProjectile>& GetProjectileProxies() const { return Projectiles; }
	const TArray<FPaper2DPlusPreviewEffect>& GetEffectProxies() const { return Effects; }
	const TArray<FPaper2DPlusPreviewShape>& GetShapes() const { return Shapes; }
	bool GetOverlay(FLinearColor& OutColor) const;
	FVector2D GetCameraOffset() const;
	int32 GetOwnedResourceCount() const;
	FPaper2DPlusPreviewResourceLedger GetResourceLedger() const;

	/**
	 * Host-only ownership scope. Every handle created until the matching EndAdapterDispatch belongs to
	 * OwnerToken.
	 *
	 * NESTABLE: Begin returns the token it displaced and End takes it back, because adapter code can
	 * synchronously re-enter cue notification (an adapter that edits the registry or triggers a
	 * Blueprint compile does exactly that). Resetting to INDEX_NONE on the way out instead would leave
	 * every later allocation in the OUTER adapter unowned, so its own per-owner cleanup would skip
	 * them and its sounds and proxies would outlive its teardown. Prefer FScopedAdapterDispatch.
	 */
	int32 BeginAdapterDispatch(int32 OwnerToken);
	void EndAdapterDispatch(int32 PreviousOwnerToken);

	/** RAII form of the pair above, so a nested dispatch cannot forget to restore the enclosing owner. */
	struct FScopedAdapterDispatch
	{
		FScopedAdapterDispatch(UPaper2DPlusFrameCuePreviewContext& InContext, int32 OwnerToken)
			: Context(InContext)
			, PreviousOwnerToken(InContext.BeginAdapterDispatch(OwnerToken))
		{
		}

		~FScopedAdapterDispatch()
		{
			Context.EndAdapterDispatch(PreviousOwnerToken);
		}

		FScopedAdapterDispatch(const FScopedAdapterDispatch&) = delete;
		FScopedAdapterDispatch& operator=(const FScopedAdapterDispatch&) = delete;

	private:
		UPaper2DPlusFrameCuePreviewContext& Context;
		int32 PreviousOwnerToken = INDEX_NONE;
	};
	void ReleaseResourcesForOwner(int32 OwnerToken);

	FSimpleMulticastDelegate OnPreviewChanged;

private:
	friend class FPaper2DPlusFrameCuePreviewHost;

	/** Global teardown is deliberately host-only. Creator adapters may release their own opaque
	 *  handles, but cannot erase resources owned by adapters that ran before them. */
	void ResetAll();
	void SetPreviewWorld(UWorld* World) { PreviewWorld = World; }

	UPROPERTY(Transient)
	TMap<int32, TObjectPtr<UAudioComponent>> AudioByHandle;
	TWeakObjectPtr<UWorld> PreviewWorld;

	UPROPERTY(Transient)
	TArray<FPaper2DPlusPreviewEffect> Effects;

	TArray<FPaper2DPlusPreviewProjectile> Projectiles;
	TArray<FPaper2DPlusPreviewShape> Shapes;

	struct FOverlayResource
	{
		FLinearColor Color = FLinearColor::Transparent;
		float Remaining = 0.0f;
	};
	struct FCameraResource
	{
		float Scale = 0.0f;
		float Age = 0.0f;
		float Remaining = 0.0f;
	};

	TMap<int32, FOverlayResource> Overlays;
	TMap<int32, FCameraResource> CameraOffsets;
	TMap<int32, float> Timers;
	TMap<int32, int32> OwnerByHandle;
	int32 ActiveOwnerToken = INDEX_NONE;
	int32 NextHandle = 1;

	FPaper2DPlusPreviewResourceHandle AllocateHandle();
	void ReleaseResourceInternal(int32 Handle, bool bBroadcast);
};
