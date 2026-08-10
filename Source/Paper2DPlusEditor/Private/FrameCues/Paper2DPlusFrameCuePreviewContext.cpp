// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "FrameCues/Paper2DPlusFrameCuePreviewContext.h"
#include "FrameCues/Paper2DPlusFrameCue.h"
#include "Components/AudioComponent.h"
#include "Kismet/GameplayStatics.h"
#include "PaperFlipbook.h"
#include "Sound/SoundBase.h"

FPaper2DPlusPreviewResourceHandle UPaper2DPlusFrameCuePreviewContext::AllocateHandle()
{
	FPaper2DPlusPreviewResourceHandle Handle;
	Handle.Value = NextHandle++;
	OwnerByHandle.Add(Handle.Value, ActiveOwnerToken);
	return Handle;
}

int32 UPaper2DPlusFrameCuePreviewContext::BeginAdapterDispatch(int32 OwnerToken)
{
	const int32 PreviousOwnerToken = ActiveOwnerToken;
	ActiveOwnerToken = OwnerToken;
	return PreviousOwnerToken;
}

void UPaper2DPlusFrameCuePreviewContext::EndAdapterDispatch(int32 PreviousOwnerToken)
{
	// Restore, never reset. Adapter code can synchronously re-enter cue notification, and a reset would
	// leave everything the OUTER adapter allocated afterwards stamped INDEX_NONE — invisible to its own
	// ReleaseResourcesForOwner sweep, so its sounds keep playing past its teardown.
	ActiveOwnerToken = PreviousOwnerToken;
}

FPaper2DPlusPreviewResourceHandle UPaper2DPlusFrameCuePreviewContext::PlaySound(
	USoundBase* Sound,
	float VolumeMultiplier,
	float PitchMultiplier,
	float StartTime)
{
	FPaper2DPlusPreviewResourceHandle Handle;
	UWorld* World = PreviewWorld.Get();
	if (!Sound || !World)
	{
		return Handle;
	}

	// Use the host's unique preview-world device. Each tab can overlap Cue sounds naturally, and
	// seek/reset/close can stop only its own components without mutating GEditor's global audition
	// component or its flush policy.
	if (UAudioComponent* Audio = UGameplayStatics::SpawnSound2D(
		World,
		Sound,
		FMath::Max(0.0f, VolumeMultiplier),
		FMath::Max(0.0f, PitchMultiplier),
		FMath::Max(0.0f, StartTime),
		/*ConcurrencySettings=*/nullptr,
		/*bPersistAcrossLevelTransition=*/false,
		/*bAutoDestroy=*/false))
	{
		Handle = AllocateHandle();
		AudioByHandle.Add(Handle.Value, Audio);
		OnPreviewChanged.Broadcast();
	}
	return Handle;
}

FPaper2DPlusPreviewResourceHandle UPaper2DPlusFrameCuePreviewContext::SpawnProjectileProxy(
	FVector2D SpawnOffset,
	FVector2D LaunchVelocity,
	float Lifetime,
	FLinearColor Color)
{
	FPaper2DPlusPreviewResourceHandle Handle = AllocateHandle();

	FPaper2DPlusPreviewProjectile& Projectile = Projectiles.AddDefaulted_GetRef();
	Projectile.Handle = Handle.Value;
	Projectile.Position = SpawnOffset;
	Projectile.Velocity = LaunchVelocity;
	Projectile.Lifetime = FMath::Max(0.05f, Lifetime);
	Projectile.Color = Color;
	constexpr int32 NumTrajectorySteps = 24;
	for (int32 Step = 0; Step <= NumTrajectorySteps; ++Step)
	{
		const float Alpha = static_cast<float>(Step) / NumTrajectorySteps;
		Projectile.Trajectory.Add(SpawnOffset + LaunchVelocity * (Projectile.Lifetime * Alpha));
	}
	OnPreviewChanged.Broadcast();
	return Handle;
}

FPaper2DPlusPreviewResourceHandle UPaper2DPlusFrameCuePreviewContext::SpawnEffectProxy(
	const FPaper2DPlusEffectSpawnSettings& Settings,
	float Lifetime,
	UPaper2DPlusCueBase* SourceCue)
{
	if (!Settings.EffectFlipbook)
	{
		return FPaper2DPlusPreviewResourceHandle();
	}

	FPaper2DPlusPreviewResourceHandle Handle = AllocateHandle();
	FPaper2DPlusPreviewEffect& Effect = Effects.AddDefaulted_GetRef();
	Effect.Handle = Handle.Value;
	Effect.Settings = Settings;
	Effect.SourceCue = SourceCue;
	const float NaturalDuration = Settings.EffectFlipbook->GetTotalDuration();
	Effect.Lifetime = FMath::Max(0.05f, Lifetime > 0.0f ? Lifetime : NaturalDuration);
	OnPreviewChanged.Broadcast();
	return Handle;
}

FPaper2DPlusPreviewResourceHandle UPaper2DPlusFrameCuePreviewContext::ShowOverlay(
	FLinearColor Color,
	float Duration)
{
	FPaper2DPlusPreviewResourceHandle Handle = AllocateHandle();
	FOverlayResource& Overlay = Overlays.Add(Handle.Value);
	Overlay.Color = Color;
	Overlay.Remaining = FMath::Max(0.0f, Duration);
	OnPreviewChanged.Broadcast();
	return Handle;
}

FPaper2DPlusPreviewResourceHandle UPaper2DPlusFrameCuePreviewContext::ShowShape(
	FVector2D Center,
	FVector2D Size,
	FLinearColor Color,
	float Duration)
{
	FPaper2DPlusPreviewResourceHandle Handle = AllocateHandle();
	FPaper2DPlusPreviewShape& Shape = Shapes.AddDefaulted_GetRef();
	Shape.Handle = Handle.Value;
	Shape.Center = Center;
	Shape.Size = FVector2D(FMath::Max(1.0f, FMath::Abs(Size.X)), FMath::Max(1.0f, FMath::Abs(Size.Y)));
	Shape.Color = Color;
	Shape.Remaining = FMath::Max(0.0f, Duration);
	OnPreviewChanged.Broadcast();
	return Handle;
}

FPaper2DPlusPreviewResourceHandle UPaper2DPlusFrameCuePreviewContext::ShowCameraShakeApproximation(
	float Scale,
	float Duration)
{
	FPaper2DPlusPreviewResourceHandle Handle = AllocateHandle();
	FCameraResource& Camera = CameraOffsets.Add(Handle.Value);
	Camera.Scale = FMath::Max(0.0f, Scale);
	Camera.Remaining = FMath::Max(0.0f, Duration);
	OnPreviewChanged.Broadcast();
	return Handle;
}

FPaper2DPlusPreviewResourceHandle UPaper2DPlusFrameCuePreviewContext::CreateTimer(float Duration)
{
	FPaper2DPlusPreviewResourceHandle Handle = AllocateHandle();
	Timers.Add(Handle.Value, FMath::Max(0.0f, Duration));
	return Handle;
}

void UPaper2DPlusFrameCuePreviewContext::ReleaseResource(FPaper2DPlusPreviewResourceHandle Handle)
{
	if (!Handle.IsValid()) return;
	ReleaseResourceInternal(Handle.Value, true);
}

void UPaper2DPlusFrameCuePreviewContext::ReleaseResourceInternal(int32 Handle, bool bBroadcast)
{
	if (TObjectPtr<UAudioComponent>* Audio = AudioByHandle.Find(Handle))
	{
		if (IsValid(*Audio))
		{
			(*Audio)->Stop();
			(*Audio)->DestroyComponent();
		}
		AudioByHandle.Remove(Handle);
	}
	Projectiles.RemoveAll([Handle](const FPaper2DPlusPreviewProjectile& P) { return P.Handle == Handle; });
	Effects.RemoveAll([Handle](const FPaper2DPlusPreviewEffect& E) { return E.Handle == Handle; });
	Shapes.RemoveAll([Handle](const FPaper2DPlusPreviewShape& S) { return S.Handle == Handle; });
	Overlays.Remove(Handle);
	CameraOffsets.Remove(Handle);
	Timers.Remove(Handle);
	OwnerByHandle.Remove(Handle);
	if (bBroadcast) OnPreviewChanged.Broadcast();
}

void UPaper2DPlusFrameCuePreviewContext::ReleaseResourcesForOwner(int32 OwnerToken)
{
	TArray<int32> Handles;
	for (const TPair<int32, int32>& Pair : OwnerByHandle)
	{
		if (Pair.Value == OwnerToken) Handles.Add(Pair.Key);
	}
	for (const int32 Handle : Handles) ReleaseResourceInternal(Handle, false);
	if (Handles.Num() > 0) OnPreviewChanged.Broadcast();
}

void UPaper2DPlusFrameCuePreviewContext::ResetAll()
{
	TArray<int32> AudioHandles;
	AudioByHandle.GetKeys(AudioHandles);
	for (const int32 Handle : AudioHandles) ReleaseResourceInternal(Handle, false);
	AudioByHandle.Reset();
	Effects.Reset();
	Projectiles.Reset();
	Shapes.Reset();
	Overlays.Reset();
	CameraOffsets.Reset();
	Timers.Reset();
	OwnerByHandle.Reset();
	ActiveOwnerToken = INDEX_NONE;
	OnPreviewChanged.Broadcast();
}

void UPaper2DPlusFrameCuePreviewContext::Tick(float DeltaSeconds)
{
	const float SafeDelta = FMath::Max(0.0f, DeltaSeconds);
	for (FPaper2DPlusPreviewProjectile& Projectile : Projectiles)
	{
		Projectile.Age += SafeDelta;
		Projectile.Position += Projectile.Velocity * SafeDelta;
	}
	for (FPaper2DPlusPreviewEffect& Effect : Effects) Effect.Age += SafeDelta;
	for (FPaper2DPlusPreviewShape& Shape : Shapes) Shape.Remaining -= SafeDelta;
	for (TPair<int32, FOverlayResource>& Pair : Overlays) Pair.Value.Remaining -= SafeDelta;
	for (TPair<int32, FCameraResource>& Pair : CameraOffsets)
	{
		Pair.Value.Age += SafeDelta;
		Pair.Value.Remaining -= SafeDelta;
	}
	for (TPair<int32, float>& Pair : Timers) Pair.Value -= SafeDelta;

	TArray<int32> ExpiredHandles;
	for (const TPair<int32, TObjectPtr<UAudioComponent>>& Pair : AudioByHandle)
	{
		if (!Pair.Value || !Pair.Value->IsPlaying()) ExpiredHandles.Add(Pair.Key);
	}
	for (const FPaper2DPlusPreviewProjectile& P : Projectiles) if (P.Age >= P.Lifetime) ExpiredHandles.Add(P.Handle);
	for (const FPaper2DPlusPreviewEffect& E : Effects) if (E.Age >= E.Lifetime) ExpiredHandles.Add(E.Handle);
	for (const FPaper2DPlusPreviewShape& S : Shapes) if (S.Remaining <= 0.0f) ExpiredHandles.Add(S.Handle);
	for (const TPair<int32, FOverlayResource>& Pair : Overlays) if (Pair.Value.Remaining <= 0.0f) ExpiredHandles.Add(Pair.Key);
	for (const TPair<int32, FCameraResource>& Pair : CameraOffsets) if (Pair.Value.Remaining <= 0.0f) ExpiredHandles.Add(Pair.Key);
	for (const TPair<int32, float>& Pair : Timers) if (Pair.Value <= 0.0f) ExpiredHandles.Add(Pair.Key);
	for (const int32 Handle : ExpiredHandles) ReleaseResourceInternal(Handle, false);

	if (AudioByHandle.Num() > 0 || Projectiles.Num() > 0 || Effects.Num() > 0 || Shapes.Num() > 0 ||
		Overlays.Num() > 0 || CameraOffsets.Num() > 0 || ExpiredHandles.Num() > 0)
	{
		OnPreviewChanged.Broadcast();
	}
}

bool UPaper2DPlusFrameCuePreviewContext::GetOverlay(FLinearColor& OutColor) const
{
	OutColor = FLinearColor::Transparent;
	int32 NewestHandle = INDEX_NONE;
	for (const TPair<int32, FOverlayResource>& Pair : Overlays)
	{
		if (Pair.Key > NewestHandle)
		{
			NewestHandle = Pair.Key;
			OutColor = Pair.Value.Color;
		}
	}
	return NewestHandle != INDEX_NONE;
}

int32 UPaper2DPlusFrameCuePreviewContext::GetOwnedResourceCount() const
{
	return OwnerByHandle.Num();
}

FVector2D UPaper2DPlusFrameCuePreviewContext::GetCameraOffset() const
{
	FVector2D Result = FVector2D::ZeroVector;
	for (const TPair<int32, FCameraResource>& Pair : CameraOffsets)
	{
		const FCameraResource& Camera = Pair.Value;
		Result += FVector2D(
			FMath::Sin(Camera.Age * 37.0f),
			FMath::Cos(Camera.Age * 29.0f) * 0.6f) * Camera.Scale;
	}
	return Result;
}

FPaper2DPlusPreviewResourceLedger UPaper2DPlusFrameCuePreviewContext::GetResourceLedger() const
{
	FPaper2DPlusPreviewResourceLedger Ledger;
	Ledger.Audio = AudioByHandle.Num();
	Ledger.Effects = Effects.Num();
	Ledger.Projectiles = Projectiles.Num();
	Ledger.Overlays = Overlays.Num();
	Ledger.Shapes = Shapes.Num();
	Ledger.CameraOffsets = CameraOffsets.Num();
	Ledger.Timers = Timers.Num();
	return Ledger;
}
