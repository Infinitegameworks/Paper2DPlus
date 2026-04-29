// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "Paper2DPlusBlueprintLibrary.h"
#include "Paper2DPlusSettings.h"
#include "Paper2DPlusCharacterProfileComponent.h"
#include "Paper2DPlusModule.h"
#include "PaperFlipbook.h"
#include "AnimSequences/PaperZDAnimSequence.h"
#include "PaperFlipbookComponent.h"
#include "PaperSprite.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

/** UPaper2DPlusBlueprintLibrary — Static Blueprint functions: hitbox collision checks, world-space coordinate transforms, damage/knockback queries. */

namespace
{
	void GetScaledHitboxRect(const FHitboxData& Hitbox, bool bFlipX, float ScaleX, float ScaleY, float& OutX, float& OutY, float& OutW, float& OutH)
	{
		OutX = Hitbox.X * ScaleX;
		OutY = Hitbox.Y * ScaleY;
		OutW = Hitbox.Width * ScaleX;
		OutH = Hitbox.Height * ScaleY;

		if (bFlipX)
		{
			OutX = -(OutX + OutW);
		}
	}

	void GetScaledHitboxRect(const FHitboxData& Hitbox, bool bFlipX, float Scale, float& OutX, float& OutY, float& OutW, float& OutH)
	{
		GetScaledHitboxRect(Hitbox, bFlipX, Scale, Scale, OutX, OutY, OutW, OutH);
	}

	FWorldHitbox MakeWorldHitbox(const FHitboxData& Hitbox, const FVector& WorldPosition, bool bFlipX, float ScaleX, float ScaleY)
	{
		float X = 0.0f, Z = 0.0f, W = 0.0f, H = 0.0f;
		GetScaledHitboxRect(Hitbox, bFlipX, ScaleX, ScaleY, X, Z, W, H);

		FWorldHitbox Out;
		Out.Type = Hitbox.Type;
		Out.Center = FVector(WorldPosition.X + X + W * 0.5f, WorldPosition.Y, WorldPosition.Z + Z + H * 0.5f);
		Out.Extents = FVector(W * 0.5f, 2.0f, H * 0.5f);
		Out.Damage = Hitbox.Damage;
		Out.Knockback = Hitbox.Knockback;
		return Out;
	}

	FWorldHitbox MakeWorldHitbox(const FHitboxData& Hitbox, const FVector& WorldPosition, bool bFlipX, float Scale)
	{
		return MakeWorldHitbox(Hitbox, WorldPosition, bFlipX, Scale, Scale);
	}

	FWorldSocket MakeWorldSocket(const FSocketData& Socket, const FVector& WorldPosition, bool bFlipX, float ScaleX, float ScaleY)
	{
		float X = Socket.X * ScaleX;
		float Y = Socket.Y * ScaleY;
		if (bFlipX) X = -X;

		FWorldSocket Out;
		Out.Name = Socket.Name;
		Out.Location = FVector(WorldPosition.X + X, WorldPosition.Y, WorldPosition.Z + Y);
		return Out;
	}

	FWorldSocket MakeWorldSocket(const FSocketData& Socket, const FVector& WorldPosition, bool bFlipX, float Scale)
	{
		return MakeWorldSocket(Socket, WorldPosition, bFlipX, Scale, Scale);
	}

	// Zero-allocation check: does attacker have attacks AND defender have hurtboxes?
	bool HasAttackAndHurtBoxes(const FFrameHitboxData& AttackerFrame, const FFrameHitboxData& DefenderFrame)
	{
		return AttackerFrame.HasHitboxOfType(EHitboxType::Attack) && DefenderFrame.HasHitboxOfType(EHitboxType::Hurtbox);
	}

	FBox2D HitboxToWorldSpaceNonUniform(
		const FHitboxData& Hitbox,
		FVector2D WorldPosition,
		bool bFlipX,
		float ScaleX,
		float ScaleY)
	{
		float X = 0.0f;
		float Y = 0.0f;
		float W = 0.0f;
		float H = 0.0f;
		GetScaledHitboxRect(Hitbox, bFlipX, ScaleX, ScaleY, X, Y, W, H);

		return FBox2D(
			FVector2D(WorldPosition.X + X, WorldPosition.Y + Y),
			FVector2D(WorldPosition.X + X + W, WorldPosition.Y + Y + H));
	}

	bool CheckHitboxCollisionNonUniform(
		const FFrameHitboxData& AttackerFrame,
		FVector2D AttackerPosition,
		bool bAttackerFlipX,
		float AttackerScaleX,
		float AttackerScaleY,
		const FFrameHitboxData& DefenderFrame,
		FVector2D DefenderPosition,
		bool bDefenderFlipX,
		float DefenderScaleX,
		float DefenderScaleY,
		TArray<FHitboxCollisionResult>& OutResults)
	{
		OutResults.Empty();

		// Zero-allocation: iterate hitbox arrays directly instead of copying by type
		if (!HasAttackAndHurtBoxes(AttackerFrame, DefenderFrame)) return false;

		bool bAnyHit = false;
		const FVector AttackerPos3D(AttackerPosition.X, 0.0f, AttackerPosition.Y);
		const FVector DefenderPos3D(DefenderPosition.X, 0.0f, DefenderPosition.Y);

		for (const FHitboxData& Attack : AttackerFrame.Hitboxes)
		{
			if (Attack.Type != EHitboxType::Attack) continue;

			const FBox2D AttackWorld = HitboxToWorldSpaceNonUniform(
				Attack, AttackerPosition, bAttackerFlipX, AttackerScaleX, AttackerScaleY);

			for (const FHitboxData& Hurt : DefenderFrame.Hitboxes)
			{
				if (Hurt.Type != EHitboxType::Hurtbox) continue;

				const FBox2D HurtWorld = HitboxToWorldSpaceNonUniform(
					Hurt, DefenderPosition, bDefenderFlipX, DefenderScaleX, DefenderScaleY);

				if (!AttackWorld.Intersect(HurtWorld)) continue;

				FHitboxCollisionResult Result;
				Result.bHit = true;
				Result.AttackBox = MakeWorldHitbox(Attack, AttackerPos3D, bAttackerFlipX, AttackerScaleX, AttackerScaleY);
				Result.HurtBox = MakeWorldHitbox(Hurt, DefenderPos3D, bDefenderFlipX, DefenderScaleX, DefenderScaleY);
				Result.Damage = Attack.Damage;
				Result.Knockback = Attack.Knockback;

				const FBox2D Overlap(
					FVector2D(FMath::Max(AttackWorld.Min.X, HurtWorld.Min.X), FMath::Max(AttackWorld.Min.Y, HurtWorld.Min.Y)),
					FVector2D(FMath::Min(AttackWorld.Max.X, HurtWorld.Max.X), FMath::Min(AttackWorld.Max.Y, HurtWorld.Max.Y)));
				Result.HitLocation = Overlap.GetCenter();

				OutResults.Add(Result);
				bAnyHit = true;
			}
		}

		return bAnyHit;
	}

	bool QuickHitCheckFromFramesNonUniform(
		const FFrameHitboxData& AttackerFrame,
		FVector2D AttackerPosition,
		bool bAttackerFlipX,
		float AttackerScaleX,
		float AttackerScaleY,
		const FFrameHitboxData& DefenderFrame,
		FVector2D DefenderPosition,
		bool bDefenderFlipX,
		float DefenderScaleX,
		float DefenderScaleY)
	{
		// Zero-allocation: iterate directly, early-out on first hit
		if (!HasAttackAndHurtBoxes(AttackerFrame, DefenderFrame)) return false;

		for (const FHitboxData& Attack : AttackerFrame.Hitboxes)
		{
			if (Attack.Type != EHitboxType::Attack) continue;

			const FBox2D AttackWorld = HitboxToWorldSpaceNonUniform(
				Attack, AttackerPosition, bAttackerFlipX, AttackerScaleX, AttackerScaleY);

			for (const FHitboxData& Hurt : DefenderFrame.Hitboxes)
			{
				if (Hurt.Type != EHitboxType::Hurtbox) continue;

				const FBox2D HurtWorld = HitboxToWorldSpaceNonUniform(
					Hurt, DefenderPosition, bDefenderFlipX, DefenderScaleX, DefenderScaleY);

				if (AttackWorld.Intersect(HurtWorld)) return true;
			}
		}

		return false;
	}

	// Returns const pointer to frame data + resolved frame index. Zero copies.
	const FFrameHitboxData* ResolveFrameDataPtr(
		UPaper2DPlusCharacterProfileAsset* CharacterProfile,
		UPaperFlipbook* Flipbook,
		float PlaybackPosition,
		int32& OutFrameIndex)
	{
		OutFrameIndex = INDEX_NONE;
		if (!CharacterProfile || !Flipbook) return nullptr;

		const FFlipbookProfileEntry* AnimData = CharacterProfile->FindByFlipbookPtr(Flipbook);
		if (!AnimData) return nullptr;

		const int32 NumKeyFrames = Flipbook->GetNumKeyFrames();
		if (NumKeyFrames <= 0) return nullptr;

		const float TotalDuration = Flipbook->GetTotalDuration();
		if (TotalDuration <= 0.0f) return nullptr;

		float WrappedPosition = FMath::Fmod(PlaybackPosition, TotalDuration);
		if (WrappedPosition < 0.0f) WrappedPosition += TotalDuration;

		OutFrameIndex = FMath::Clamp(Flipbook->GetKeyFrameIndexAtTime(WrappedPosition), 0, NumKeyFrames - 1);
		if (!AnimData->CombatData.Frames.IsValidIndex(OutFrameIndex)) return nullptr;

		return &AnimData->CombatData.Frames[OutFrameIndex];
	}

	bool TryResolveFrameData(
		UPaper2DPlusCharacterProfileAsset* CharacterProfile,
		UPaperFlipbook* Flipbook,
		float PlaybackPosition,
		FFrameHitboxData& OutFrameData)
	{
		return UPaper2DPlusBlueprintLibrary::ResolveFrameFromPlayback(CharacterProfile, Flipbook, PlaybackPosition, OutFrameData);
	}

	bool TryGetCurrentSpritePivotLocal(UPaperFlipbook* Flipbook, float PlaybackPosition, FVector2D& OutPivotLocal)
	{
		OutPivotLocal = FVector2D::ZeroVector;
		if (!Flipbook)
		{
			return false;
		}

		const int32 NumKeyFrames = Flipbook->GetNumKeyFrames();
		if (NumKeyFrames <= 0)
		{
			return false;
		}

		const float TotalDuration = Flipbook->GetTotalDuration();
		if (TotalDuration <= 0.0f)
		{
			return false;
		}

		float WrappedPosition = FMath::Fmod(PlaybackPosition, TotalDuration);
		if (WrappedPosition < 0.0f)
		{
			WrappedPosition += TotalDuration;
		}

		const int32 FrameIndex = FMath::Clamp(Flipbook->GetKeyFrameIndexAtTime(WrappedPosition), 0, NumKeyFrames - 1);
		const FPaperFlipbookKeyFrame& KeyFrame = Flipbook->GetKeyFrameChecked(FrameIndex);
		if (!KeyFrame.Sprite)
		{
			return false;
		}

#if WITH_EDITOR
		// Convert from texture-space coordinates to sprite-local (top-left) coordinates.
		OutPivotLocal = KeyFrame.Sprite->GetPivotPosition() - KeyFrame.Sprite->GetSourceUV();
		return true;
#else
		return false;
#endif
	}

	void ConvertFrameDataFromTopLeftToPivotSpace(FFrameHitboxData& InOutFrameData, int32 PivotX, int32 PivotY)
	{
		for (FHitboxData& Hitbox : InOutFrameData.Hitboxes)
		{
			Hitbox.X -= PivotX;
			Hitbox.Y = PivotY - Hitbox.Y - Hitbox.Height;
		}

		for (FSocketData& Socket : InOutFrameData.Sockets)
		{
			Socket.X -= PivotX;
			Socket.Y = PivotY - Socket.Y;
		}
	}

	UWorld* GetWorldFromContext(UObject* WorldContext)
	{
		return WorldContext ? WorldContext->GetWorld() : nullptr;
	}

	// ==========================================
	// ACTOR RESOLUTION
	// ==========================================

	struct FActorHitboxContext
	{
		UPaper2DPlusCharacterProfileAsset* CharacterProfile = nullptr;
		FFrameHitboxData FrameData;
		FVector WorldPosition = FVector::ZeroVector;
		bool bFlipX = false;
		float ScaleX = 1.0f;
		float ScaleY = 1.0f;
	};

	static TSet<TWeakObjectPtr<AActor>> WarnedActors;
	static int32 ResolveCallCounter = 0;

	void CleanupStaleWarnings()
	{
		for (auto It = WarnedActors.CreateIterator(); It; ++It)
		{
			if (!It->IsValid())
			{
				It.RemoveCurrent();
			}
		}
	}

	// Game thread only — WarnedActors TSet is not thread-safe
	bool TryResolveActorContext(AActor* Actor, FActorHitboxContext& OutContext)
	{
		if (!IsValid(Actor)) return false;

		// Throttle stale warning cleanup — every 64 calls instead of every call
		if ((++ResolveCallCounter & 0x3F) == 0)
		{
			CleanupStaleWarnings();
		}

		UPaper2DPlusCharacterProfileComponent* DataComp = Actor->FindComponentByClass<UPaper2DPlusCharacterProfileComponent>();
		if (!DataComp)
		{
			if (!WarnedActors.Contains(Actor))
			{
				UE_LOG(LogPaper2DPlus, Warning,
					TEXT("TryResolveActorContext: Actor '%s' (Class: %s) has no Paper2DPlusCharacterProfileComponent"),
					*Actor->GetName(), *Actor->GetClass()->GetName());
				WarnedActors.Add(Actor);
			}
			return false;
		}

		OutContext.CharacterProfile = DataComp->CharacterProfile;
		if (!OutContext.CharacterProfile)
		{
			if (!WarnedActors.Contains(Actor))
			{
				UE_LOG(LogPaper2DPlus, Warning,
					TEXT("TryResolveActorContext: Actor '%s' (Addr: %p) CharacterProfileComponent has no CharacterProfile asset set"),
					*Actor->GetName(), Actor);
				WarnedActors.Add(Actor);
			}
			return false;
		}

		UPaperFlipbookComponent* FlipbookComp = DataComp->GetResolvedFlipbookComponent();
		if (!IsValid(FlipbookComp))
		{
			if (!WarnedActors.Contains(Actor))
			{
				UE_LOG(LogPaper2DPlus, Warning,
					TEXT("TryResolveActorContext: Actor '%s' has no FlipbookComponent"),
					*Actor->GetName());
				WarnedActors.Add(Actor);
			}
			return false;
		}

		UPaperFlipbook* Flipbook = FlipbookComp->GetFlipbook();
		if (!Flipbook)
		{
			if (!WarnedActors.Contains(Actor))
			{
				UE_LOG(LogPaper2DPlus, Warning,
					TEXT("TryResolveActorContext: Actor '%s' FlipbookComponent has no Flipbook set (PaperZD may not have assigned one yet)"),
					*Actor->GetName());
				WarnedActors.Add(Actor);
			}
			return false;
		}

		// Resolve frame data as const pointer — zero copy. Also returns frame index
		// so we reuse it for pivot lookup (avoids double GetKeyFrameIndexAtTime).
		const float PlaybackPos = FlipbookComp->GetPlaybackPosition();
		int32 ResolvedFrameIndex = INDEX_NONE;
		const FFrameHitboxData* FrameDataPtr = ResolveFrameDataPtr(
			OutContext.CharacterProfile, Flipbook, PlaybackPos, ResolvedFrameIndex);

		if (!FrameDataPtr)
		{
			if (!WarnedActors.Contains(Actor))
			{
				UE_LOG(LogPaper2DPlus, Warning,
					TEXT("TryResolveActorContext: Actor '%s' — FindByFlipbookPtr failed for flipbook '%s' in profile '%s'"),
					*Actor->GetName(), *Flipbook->GetName(), *OutContext.CharacterProfile->GetName());
				WarnedActors.Add(Actor);
			}
			return false;
		}

		// Copy frame data (needed because pivot conversion mutates it)
		OutContext.FrameData = *FrameDataPtr;

		// Use the flipbook component's world transform (includes actor + component local transform)
		OutContext.WorldPosition = FlipbookComp->GetComponentLocation();

		const FVector CompScale = FlipbookComp->GetComponentScale();
		const float Yaw = FMath::Abs(FlipbookComp->GetComponentRotation().Yaw);
		OutContext.bFlipX = (Yaw > 90.0f && Yaw < 270.0f) || CompScale.X < 0.0f;
		OutContext.ScaleX = FMath::Max(FMath::Abs(CompScale.X), KINDA_SMALL_NUMBER);
		OutContext.ScaleY = FMath::Max(FMath::Abs(CompScale.Z), KINDA_SMALL_NUMBER);

		// Reuse ResolvedFrameIndex for pivot lookup — avoids a second GetKeyFrameIndexAtTime call
		if (ResolvedFrameIndex >= 0 && ResolvedFrameIndex < Flipbook->GetNumKeyFrames())
		{
			if (UPaperSprite* Sprite = Flipbook->GetKeyFrameChecked(ResolvedFrameIndex).Sprite)
			{
#if WITH_EDITOR
				const FVector2D PivotLocal = Sprite->GetPivotPosition() - Sprite->GetSourceUV();
				const int32 PivotXInt = FMath::FloorToInt(PivotLocal.X);
				const int32 PivotYInt = FMath::FloorToInt(PivotLocal.Y);
				const float PivotXFrac = PivotLocal.X - static_cast<float>(PivotXInt);
				const float PivotYFrac = PivotLocal.Y - static_cast<float>(PivotYInt);

				ConvertFrameDataFromTopLeftToPivotSpace(OutContext.FrameData, PivotXInt, PivotYInt);

				OutContext.WorldPosition.X += (OutContext.bFlipX ? PivotXFrac : -PivotXFrac) * OutContext.ScaleX;
				OutContext.WorldPosition.Z += PivotYFrac * OutContext.ScaleY;
#endif
			}
		}

		return true;
	}
}

// ==========================================
// WORLD SPACE CONVERSION
// ==========================================

FBox2D UPaper2DPlusBlueprintLibrary::HitboxToWorldSpace(const FHitboxData& Hitbox, FVector2D WorldPosition, bool bFlipX, float Scale)
{
	float X = 0.0f;
	float Y = 0.0f;
	float W = 0.0f;
	float H = 0.0f;
	GetScaledHitboxRect(Hitbox, bFlipX, Scale, X, Y, W, H);

	return FBox2D(
		FVector2D(WorldPosition.X + X, WorldPosition.Y + Y),
		FVector2D(WorldPosition.X + X + W, WorldPosition.Y + Y + H)
	);
}

FBox2D UPaper2DPlusBlueprintLibrary::HitboxToWorldSpace3D(const FHitboxData& Hitbox, FVector WorldPosition, bool bFlipX, float Scale)
{
	return HitboxToWorldSpace(Hitbox, FVector2D(WorldPosition.X, WorldPosition.Z), bFlipX, Scale);
}

FVector2D UPaper2DPlusBlueprintLibrary::SocketToWorldSpace(const FSocketData& Socket, FVector2D WorldPosition, bool bFlipX, float ScaleX, float ScaleY)
{
	float X = Socket.X * ScaleX;
	float Y = Socket.Y * ScaleY;

	if (bFlipX)
	{
		X = -X;
	}

	return FVector2D(WorldPosition.X + X, WorldPosition.Y + Y);
}

FVector UPaper2DPlusBlueprintLibrary::SocketToWorldSpace3D(const FSocketData& Socket, FVector WorldPosition, bool bFlipX, float ScaleX, float ScaleY)
{
	FVector2D Pos2D = SocketToWorldSpace(Socket, FVector2D(WorldPosition.X, WorldPosition.Z), bFlipX, ScaleX, ScaleY);
	return FVector(Pos2D.X, WorldPosition.Y, Pos2D.Y);
}

bool UPaper2DPlusBlueprintLibrary::SetActorCharacterProfile(AActor* Actor, UPaper2DPlusCharacterProfileAsset* NewCharacterProfile)
{
	if (!IsValid(Actor))
	{
		UE_LOG(LogPaper2DPlus, Warning, TEXT("SetActorCharacterProfile: Actor is null or invalid"));
		return false;
	}

	UPaper2DPlusCharacterProfileComponent* DataComp = Actor->FindComponentByClass<UPaper2DPlusCharacterProfileComponent>();
	if (!DataComp)
	{
		UE_LOG(LogPaper2DPlus, Warning,
			TEXT("SetActorCharacterProfile: Actor '%s' (Class: %s) has no Paper2DPlusCharacterProfileComponent. "
			     "Add one in the Blueprint component list."),
			*Actor->GetName(), *Actor->GetClass()->GetName());
		return false;
	}

	DataComp->SetCharacterProfile(NewCharacterProfile);

	// Clear stale warning so future resolution failures are logged fresh
	WarnedActors.Remove(Actor);

	return true;
}

// ==========================================
// ACTOR-BASED COLLISION DETECTION
// ==========================================

bool UPaper2DPlusBlueprintLibrary::CheckAttackCollision(
	AActor* Attacker, AActor* Defender, TArray<FHitboxCollisionResult>& OutResults)
{
	OutResults.Empty();

	FActorHitboxContext AttackerCtx, DefenderCtx;
	if (!TryResolveActorContext(Attacker, AttackerCtx)) return false;
	if (!TryResolveActorContext(Defender, DefenderCtx)) return false;

	return CheckHitboxCollisionNonUniform(
		AttackerCtx.FrameData,
		FVector2D(AttackerCtx.WorldPosition.X, AttackerCtx.WorldPosition.Z),
		AttackerCtx.bFlipX, AttackerCtx.ScaleX, AttackerCtx.ScaleY,
		DefenderCtx.FrameData,
		FVector2D(DefenderCtx.WorldPosition.X, DefenderCtx.WorldPosition.Z),
		DefenderCtx.bFlipX, DefenderCtx.ScaleX, DefenderCtx.ScaleY,
		OutResults
	);
}

bool UPaper2DPlusBlueprintLibrary::QuickHitCheck(AActor* Attacker, AActor* Defender)
{
	FActorHitboxContext AttackerCtx, DefenderCtx;
	if (!TryResolveActorContext(Attacker, AttackerCtx)) return false;
	if (!TryResolveActorContext(Defender, DefenderCtx)) return false;

	return QuickHitCheckFromFramesNonUniform(
		AttackerCtx.FrameData,
		FVector2D(AttackerCtx.WorldPosition.X, AttackerCtx.WorldPosition.Z),
		AttackerCtx.bFlipX, AttackerCtx.ScaleX, AttackerCtx.ScaleY,
		DefenderCtx.FrameData,
		FVector2D(DefenderCtx.WorldPosition.X, DefenderCtx.WorldPosition.Z),
		DefenderCtx.bFlipX, DefenderCtx.ScaleX, DefenderCtx.ScaleY
	);
}

bool UPaper2DPlusBlueprintLibrary::GetHitboxFrame(AActor* Actor, FFrameHitboxData& OutFrameData)
{
	FActorHitboxContext Ctx;
	if (!TryResolveActorContext(Actor, Ctx)) return false;
	OutFrameData = Ctx.FrameData;
	return true;
}

// ==========================================
// ACTOR-BASED WORLD HITBOXES
// ==========================================

// ==========================================
// WORLD SPACE GETTERS
// ==========================================

bool UPaper2DPlusBlueprintLibrary::GetActorWorldHitboxes(AActor* Actor, TArray<FWorldHitbox>& OutHitboxes)
{
	OutHitboxes.Empty();
	FActorHitboxContext Ctx;
	if (!TryResolveActorContext(Actor, Ctx)) return false;

	for (const FHitboxData& Hitbox : Ctx.FrameData.Hitboxes)
	{
		OutHitboxes.Add(MakeWorldHitbox(Hitbox, Ctx.WorldPosition, Ctx.bFlipX, Ctx.ScaleX, Ctx.ScaleY));
	}
	return OutHitboxes.Num() > 0;
}

bool UPaper2DPlusBlueprintLibrary::GetActorWorldAttackBoxes(AActor* Actor, TArray<FWorldHitbox>& OutHitboxes)
{
	OutHitboxes.Empty();
	FActorHitboxContext Ctx;
	if (!TryResolveActorContext(Actor, Ctx)) return false;

	for (const FHitboxData& Hitbox : Ctx.FrameData.Hitboxes)
	{
		if (Hitbox.Type == EHitboxType::Attack)
		{
			OutHitboxes.Add(MakeWorldHitbox(Hitbox, Ctx.WorldPosition, Ctx.bFlipX, Ctx.ScaleX, Ctx.ScaleY));
		}
	}
	return OutHitboxes.Num() > 0;
}

bool UPaper2DPlusBlueprintLibrary::GetActorWorldHurtboxes(AActor* Actor, TArray<FWorldHitbox>& OutHitboxes)
{
	OutHitboxes.Empty();
	FActorHitboxContext Ctx;
	if (!TryResolveActorContext(Actor, Ctx)) return false;

	for (const FHitboxData& Hitbox : Ctx.FrameData.Hitboxes)
	{
		if (Hitbox.Type == EHitboxType::Hurtbox)
		{
			OutHitboxes.Add(MakeWorldHitbox(Hitbox, Ctx.WorldPosition, Ctx.bFlipX, Ctx.ScaleX, Ctx.ScaleY));
		}
	}
	return OutHitboxes.Num() > 0;
}

bool UPaper2DPlusBlueprintLibrary::GetActorWorldSockets(AActor* Actor, TArray<FWorldSocket>& OutSockets)
{
	OutSockets.Empty();
	FActorHitboxContext Ctx;
	if (!TryResolveActorContext(Actor, Ctx)) return false;

	for (const FSocketData& Socket : Ctx.FrameData.Sockets)
	{
		OutSockets.Add(MakeWorldSocket(Socket, Ctx.WorldPosition, Ctx.bFlipX, Ctx.ScaleX, Ctx.ScaleY));
	}
	return OutSockets.Num() > 0;
}

bool UPaper2DPlusBlueprintLibrary::GetActorWorldSocketByName(AActor* Actor, const FString& SocketName, FVector& OutLocation)
{
	OutLocation = FVector::ZeroVector;
	FActorHitboxContext Ctx;
	if (!TryResolveActorContext(Actor, Ctx)) return false;

	const FSocketData* Socket = Ctx.FrameData.FindSocket(SocketName);
	if (!Socket) return false;

	FWorldSocket WS = MakeWorldSocket(*Socket, Ctx.WorldPosition, Ctx.bFlipX, Ctx.ScaleX, Ctx.ScaleY);
	OutLocation = WS.Location;
	return true;
}

// ==========================================
// LOCAL SPACE GETTERS (pixel coordinates relative to sprite origin)
// ==========================================

bool UPaper2DPlusBlueprintLibrary::GetActorLocalHitboxes(AActor* Actor, TArray<FHitboxData>& OutHitboxes)
{
	OutHitboxes.Empty();
	FActorHitboxContext Ctx;
	if (!TryResolveActorContext(Actor, Ctx)) return false;

	OutHitboxes = Ctx.FrameData.Hitboxes;
	return OutHitboxes.Num() > 0;
}

bool UPaper2DPlusBlueprintLibrary::GetActorLocalAttackBoxes(AActor* Actor, TArray<FHitboxData>& OutHitboxes)
{
	OutHitboxes.Empty();
	FActorHitboxContext Ctx;
	if (!TryResolveActorContext(Actor, Ctx)) return false;

	for (const FHitboxData& Hitbox : Ctx.FrameData.Hitboxes)
	{
		if (Hitbox.Type == EHitboxType::Attack) OutHitboxes.Add(Hitbox);
	}
	return OutHitboxes.Num() > 0;
}

bool UPaper2DPlusBlueprintLibrary::GetActorLocalHurtboxes(AActor* Actor, TArray<FHitboxData>& OutHitboxes)
{
	OutHitboxes.Empty();
	FActorHitboxContext Ctx;
	if (!TryResolveActorContext(Actor, Ctx)) return false;

	for (const FHitboxData& Hitbox : Ctx.FrameData.Hitboxes)
	{
		if (Hitbox.Type == EHitboxType::Hurtbox) OutHitboxes.Add(Hitbox);
	}
	return OutHitboxes.Num() > 0;
}

bool UPaper2DPlusBlueprintLibrary::GetActorLocalSockets(AActor* Actor, TArray<FSocketData>& OutSockets)
{
	OutSockets.Empty();
	FActorHitboxContext Ctx;
	if (!TryResolveActorContext(Actor, Ctx)) return false;

	OutSockets = Ctx.FrameData.Sockets;
	return OutSockets.Num() > 0;
}

// ==========================================
// DEPRECATED OLD NAMES (delegate to World variants)
// ==========================================

bool UPaper2DPlusBlueprintLibrary::GetActorHitboxes(AActor* Actor, TArray<FWorldHitbox>& OutHitboxes) { return GetActorWorldHitboxes(Actor, OutHitboxes); }
bool UPaper2DPlusBlueprintLibrary::GetActorAttackBoxes(AActor* Actor, TArray<FWorldHitbox>& OutHitboxes) { return GetActorWorldAttackBoxes(Actor, OutHitboxes); }
bool UPaper2DPlusBlueprintLibrary::GetActorHurtboxes(AActor* Actor, TArray<FWorldHitbox>& OutHitboxes) { return GetActorWorldHurtboxes(Actor, OutHitboxes); }
bool UPaper2DPlusBlueprintLibrary::GetActorSockets(AActor* Actor, TArray<FWorldSocket>& OutSockets) { return GetActorWorldSockets(Actor, OutSockets); }
bool UPaper2DPlusBlueprintLibrary::GetActorSocketByName(AActor* Actor, const FString& SocketName, FVector& OutLocation) { return GetActorWorldSocketByName(Actor, SocketName, OutLocation); }

bool UPaper2DPlusBlueprintLibrary::GetActorCollisionBoxes(AActor* Actor, TArray<FWorldHitbox>& OutHitboxes)
{
	OutHitboxes.Empty();
	FActorHitboxContext Ctx;
	if (!TryResolveActorContext(Actor, Ctx)) return false;

	for (const FHitboxData& Hitbox : Ctx.FrameData.Hitboxes)
	{
		if (Hitbox.Type == EHitboxType::Collision)
		{
			OutHitboxes.Add(MakeWorldHitbox(Hitbox, Ctx.WorldPosition, Ctx.bFlipX, Ctx.ScaleX, Ctx.ScaleY));
		}
	}
	return OutHitboxes.Num() > 0;
}

// ==========================================
// ACTOR-BASED FRAME DATA HELPERS
// ==========================================

int32 UPaper2DPlusBlueprintLibrary::GetFrameDamage(AActor* Actor)
{
	FActorHitboxContext Ctx;
	if (!TryResolveActorContext(Actor, Ctx)) return 0;

	int32 TotalDamage = 0;
	for (const FHitboxData& Hitbox : Ctx.FrameData.Hitboxes)
	{
		if (Hitbox.Type == EHitboxType::Attack) TotalDamage += Hitbox.Damage;
	}
	return TotalDamage;
}

int32 UPaper2DPlusBlueprintLibrary::GetFrameKnockback(AActor* Actor)
{
	FActorHitboxContext Ctx;
	if (!TryResolveActorContext(Actor, Ctx)) return 0;

	int32 MaxKnockback = 0;
	for (const FHitboxData& Hitbox : Ctx.FrameData.Hitboxes)
	{
		if (Hitbox.Type == EHitboxType::Attack && Hitbox.Knockback > MaxKnockback)
			MaxKnockback = Hitbox.Knockback;
	}
	return MaxKnockback;
}

bool UPaper2DPlusBlueprintLibrary::FrameHasAttack(AActor* Actor)
{
	FActorHitboxContext Ctx;
	if (!TryResolveActorContext(Actor, Ctx)) return false;
	return Ctx.FrameData.HasHitboxOfType(EHitboxType::Attack);
}

bool UPaper2DPlusBlueprintLibrary::IsFrameInvulnerable(AActor* Actor)
{
	FActorHitboxContext Ctx;
	if (!TryResolveActorContext(Actor, Ctx)) return false;
	return Ctx.FrameData.bInvulnerable;
}

// ==========================================
// MAX ATTACK REACH
// ==========================================

float UPaper2DPlusBlueprintLibrary::GetMaxAttackReach(const FFlipbookProfileEntry& FlipbookData)
{
	float MaxDistSq = 0.0f;

	for (const FFrameHitboxData& Frame : FlipbookData.CombatData.Frames)
	{
		for (const FHitboxData& HB : Frame.Hitboxes)
		{
			if (HB.Type != EHitboxType::Attack) continue;

			// Check all 4 corners of the hitbox — distance from origin (0,0)
			const float X0 = static_cast<float>(HB.X);
			const float Y0 = static_cast<float>(HB.Y);
			const float X1 = static_cast<float>(HB.X + HB.Width);
			const float Y1 = static_cast<float>(HB.Y + HB.Height);

			MaxDistSq = FMath::Max(MaxDistSq, X0 * X0 + Y0 * Y0);
			MaxDistSq = FMath::Max(MaxDistSq, X1 * X1 + Y0 * Y0);
			MaxDistSq = FMath::Max(MaxDistSq, X0 * X0 + Y1 * Y1);
			MaxDistSq = FMath::Max(MaxDistSq, X1 * X1 + Y1 * Y1);
		}
	}

	return FMath::Sqrt(MaxDistSq);
}

float UPaper2DPlusBlueprintLibrary::GetActorMaxAttackReach(AActor* Actor, int32 FlipbookIndex)
{
	if (!IsValid(Actor)) return 0.0f;

	UPaper2DPlusCharacterProfileComponent* ProfileComp = Actor->FindComponentByClass<UPaper2DPlusCharacterProfileComponent>();
	if (!ProfileComp) return 0.0f;

	UPaper2DPlusCharacterProfileAsset* Profile = ProfileComp->CharacterProfile;
	if (!Profile) return 0.0f;

	if (FlipbookIndex == -1)
	{
		// Resolve current flipbook from the flipbook component
		UPaperFlipbookComponent* FlipbookComp = Actor->FindComponentByClass<UPaperFlipbookComponent>();
		if (!FlipbookComp || !FlipbookComp->GetFlipbook()) return 0.0f;

		const FFlipbookProfileEntry* Data = Profile->FindByFlipbookPtr(FlipbookComp->GetFlipbook());
		if (!Data) return 0.0f;

		return GetMaxAttackReach(*Data);
	}

	if (!Profile->Flipbooks.IsValidIndex(FlipbookIndex)) return 0.0f;

	return GetMaxAttackReach(Profile->Flipbooks[FlipbookIndex]);
}

// ==========================================
// ANIMATION PHASE QUERIES
// ==========================================

EAnimationPhase UPaper2DPlusBlueprintLibrary::GetActorCurrentPhase(AActor* Actor)
{
	if (!Actor) return EAnimationPhase::None;

	UPaper2DPlusCharacterProfileComponent* ProfileComp = Actor->FindComponentByClass<UPaper2DPlusCharacterProfileComponent>();
	if (!ProfileComp || !ProfileComp->CharacterProfile) return EAnimationPhase::None;

	// Find the currently playing flipbook
	UPaperFlipbookComponent* FlipbookComp = Actor->FindComponentByClass<UPaperFlipbookComponent>();
	if (!FlipbookComp || !FlipbookComp->GetFlipbook()) return EAnimationPhase::None;

	// Look up the flipbook name from the character profile
	const FFlipbookProfileEntry* AnimData = ProfileComp->CharacterProfile->FindByFlipbookPtr(FlipbookComp->GetFlipbook());
	if (!AnimData) return EAnimationPhase::None;

	return ProfileComp->CharacterProfile->GetPhaseForFlipbook(AnimData->Identity.FlipbookName);
}

FString UPaper2DPlusBlueprintLibrary::GetActorCurrentPhaseGroup(AActor* Actor)
{
	if (!Actor) return FString();

	UPaper2DPlusCharacterProfileComponent* ProfileComp = Actor->FindComponentByClass<UPaper2DPlusCharacterProfileComponent>();
	if (!ProfileComp || !ProfileComp->CharacterProfile) return FString();

	UPaperFlipbookComponent* FlipbookComp = Actor->FindComponentByClass<UPaperFlipbookComponent>();
	if (!FlipbookComp || !FlipbookComp->GetFlipbook()) return FString();

	const FFlipbookProfileEntry* AnimData = ProfileComp->CharacterProfile->FindByFlipbookPtr(FlipbookComp->GetFlipbook());
	if (!AnimData) return FString();

	return ProfileComp->CharacterProfile->GetPhaseGroupNameForFlipbook(AnimData->Identity.FlipbookName);
}

// ==========================================
// CUSTOM PHASE SLOTS
// ==========================================

bool UPaper2DPlusBlueprintLibrary::HasPhaseGroupCustomSlot(
	const UPaper2DPlusCharacterProfileAsset* Asset,
	const FString& GroupName,
	const FString& CustomSlotName)
{
	if (!Asset) return false;
	const FPhaseGroup* Group = Asset->FindPhaseGroup(GroupName);
	if (!Group) return false;
	const FCustomPhaseSlot* Slot = Group->FindCustomSlot(CustomSlotName);
	return Slot && !Slot->FlipbookName.IsEmpty();
}

UPaperFlipbook* UPaper2DPlusBlueprintLibrary::GetPhaseGroupCustomFlipbook(
	const UPaper2DPlusCharacterProfileAsset* Asset,
	const FString& GroupName,
	const FString& CustomSlotName)
{
	if (!Asset) return nullptr;
	const FPhaseGroup* Group = Asset->FindPhaseGroup(GroupName);
	if (!Group) return nullptr;
	const FCustomPhaseSlot* Slot = Group->FindCustomSlot(CustomSlotName);
	if (!Slot || Slot->FlipbookName.IsEmpty()) return nullptr;

	const FFlipbookProfileEntry* AnimData = Asset->FindFlipbookDataPtr(Slot->FlipbookName);
	if (!AnimData) return nullptr;
	return AnimData->Identity.Flipbook.LoadSynchronous();
}

UPaperZDAnimSequence* UPaper2DPlusBlueprintLibrary::GetPhaseGroupCustomSequence(
	const UPaper2DPlusCharacterProfileAsset* Asset,
	const FString& GroupName,
	const FString& CustomSlotName)
{
	if (!Asset) return nullptr;
	const FPhaseGroup* Group = Asset->FindPhaseGroup(GroupName);
	if (!Group) return nullptr;
	const FCustomPhaseSlot* Slot = Group->FindCustomSlot(CustomSlotName);
	return Slot ? Slot->Sequence.Get() : nullptr;
}

FString UPaper2DPlusBlueprintLibrary::GetActorCurrentCustomSlotName(AActor* Actor, const FString& GroupName)
{
	if (!Actor) return FString();

	UPaper2DPlusCharacterProfileComponent* ProfileComp = Actor->FindComponentByClass<UPaper2DPlusCharacterProfileComponent>();
	if (!ProfileComp || !ProfileComp->CharacterProfile) return FString();

	UPaperFlipbookComponent* FlipbookComp = Actor->FindComponentByClass<UPaperFlipbookComponent>();
	if (!FlipbookComp || !FlipbookComp->GetFlipbook()) return FString();

	const FFlipbookProfileEntry* AnimData = ProfileComp->CharacterProfile->FindByFlipbookPtr(FlipbookComp->GetFlipbook());
	if (!AnimData) return FString();

	const FPhaseGroup* Group = ProfileComp->CharacterProfile->FindPhaseGroup(GroupName);
	if (!Group) return FString();

	return Group->FindCustomSlotNameForFlipbook(AnimData->Identity.FlipbookName);
}

// ==========================================
// ROOT MOTION QUERIES
// ==========================================

FVector2D UPaper2DPlusBlueprintLibrary::GetRootMotionAtFrame(const UPaper2DPlusCharacterProfileAsset* Asset, const FString& FlipbookName, int32 FrameIndex)
{
	if (!Asset) return FVector2D::ZeroVector;

	const FFlipbookProfileEntry* Data = Asset->FindFlipbookDataPtr(FlipbookName);
	if (!Data || !Data->MotionData.RootMotion.IsValidIndex(FrameIndex)) return FVector2D::ZeroVector;

	return Data->MotionData.RootMotion[FrameIndex].Position;
}

FVector UPaper2DPlusBlueprintLibrary::GetActorRootMotionDelta(AActor* Actor)
{
	if (!Actor) return FVector::ZeroVector;

	UPaper2DPlusCharacterProfileComponent* ProfileComp = Actor->FindComponentByClass<UPaper2DPlusCharacterProfileComponent>();
	if (!ProfileComp) return FVector::ZeroVector;

	return ProfileComp->GetRootMotionDelta();
}

// ==========================================
// UTILITIES
// ==========================================

int32 UPaper2DPlusBlueprintLibrary::GetTotalDamage(const TArray<FHitboxCollisionResult>& Results)
{
	int32 Total = 0;
	for (const FHitboxCollisionResult& Result : Results) Total += Result.Damage;
	return Total;
}

int32 UPaper2DPlusBlueprintLibrary::GetMaxKnockback(const TArray<FHitboxCollisionResult>& Results)
{
	int32 Max = 0;
	for (const FHitboxCollisionResult& Result : Results)
		if (Result.Knockback > Max) Max = Result.Knockback;
	return Max;
}

// ==========================================
// TAG MAPPING VALIDATION
// ==========================================

TArray<FGameplayTag> UPaper2DPlusBlueprintLibrary::GetUnmappedRequiredTags(const UPaper2DPlusCharacterProfileAsset* Asset)
{
	TArray<FGameplayTag> Unmapped;
	if (!Asset)
	{
		return Unmapped;
	}

	const UPaper2DPlusSettings* Settings = UPaper2DPlusSettings::Get();
	if (!Settings)
	{
		return Unmapped;
	}

	for (const FGameplayTag& RequiredTag : Settings->RequiredTagMappings)
	{
		if (!RequiredTag.IsValid())
		{
			continue;
		}

		if (!Asset->HasTagMapping(RequiredTag))
		{
			Unmapped.Add(RequiredTag);
		}
		else
		{
			// Also check if the binding has at least one valid animation name
			if (Asset->GetFlipbookCountForTag(RequiredTag) == 0)
			{
				Unmapped.Add(RequiredTag);
			}
		}
	}

	return Unmapped;
}

// ==========================================
// FRAME RESOLUTION
// ==========================================

bool UPaper2DPlusBlueprintLibrary::ResolveFrameFromPlayback(
	UPaper2DPlusCharacterProfileAsset* CharacterProfile,
	UPaperFlipbook* Flipbook,
	float PlaybackPosition,
	FFrameHitboxData& OutFrameData)
{
	if (!CharacterProfile || !Flipbook) return false;

	const FFlipbookProfileEntry* AnimData = CharacterProfile->FindByFlipbookPtr(Flipbook);
	if (!AnimData) return false;

	const int32 NumKeyFrames = Flipbook->GetNumKeyFrames();
	if (NumKeyFrames <= 0) return false;

	const float TotalDuration = Flipbook->GetTotalDuration();
	if (TotalDuration <= 0.0f) return false;

	float WrappedPosition = FMath::Fmod(PlaybackPosition, TotalDuration);
	if (WrappedPosition < 0.0f) WrappedPosition += TotalDuration;

	const int32 FrameIndex = FMath::Clamp(Flipbook->GetKeyFrameIndexAtTime(WrappedPosition), 0, NumKeyFrames - 1);
	if (!AnimData->CombatData.Frames.IsValidIndex(FrameIndex)) return false;

	OutFrameData = AnimData->CombatData.Frames[FrameIndex];
	return true;
}

// ==========================================
// DEBUG VISUALIZATION
// ==========================================

FColor UPaper2DPlusBlueprintLibrary::GetDebugColorForType(EHitboxType Type)
{
	switch (Type)
	{
	case EHitboxType::Attack: return FColor::Red;
	case EHitboxType::Hurtbox: return FColor::Green;
	case EHitboxType::Collision: return FColor::Blue;
	default: return FColor::White;
	}
}

void UPaper2DPlusBlueprintLibrary::DrawDebugHitboxes(
	UObject* WorldContext,
	const FFrameHitboxData& FrameData,
	FVector WorldPosition,
	bool bFlipX,
	float ScaleX,
	float ScaleY,
	float Duration,
	float Thickness,
	bool bDrawSockets)
{
	UWorld* World = GetWorldFromContext(WorldContext);
	if (!World) return;

	for (const FHitboxData& Hitbox : FrameData.Hitboxes)
	{
		DrawDebugHitbox(WorldContext, Hitbox, WorldPosition, bFlipX, ScaleX, ScaleY, FLinearColor::White, true, Duration, Thickness);
	}

	if (bDrawSockets)
	{
		for (const FSocketData& Socket : FrameData.Sockets)
		{
			FVector SocketWorld = SocketToWorldSpace3D(Socket, WorldPosition, bFlipX, ScaleX, ScaleY);
			float CrossSizeX = 5.0f * ScaleX;
			float CrossSizeZ = 5.0f * ScaleY;
			DrawDebugLine(World, SocketWorld - FVector(CrossSizeX, 0, 0), SocketWorld + FVector(CrossSizeX, 0, 0), FColor::Yellow, false, Duration, 0, Thickness);
			DrawDebugLine(World, SocketWorld - FVector(0, 0, CrossSizeZ), SocketWorld + FVector(0, 0, CrossSizeZ), FColor::Yellow, false, Duration, 0, Thickness);
			DrawDebugPoint(World, SocketWorld, 8.0f, FColor::Yellow, false, Duration);
		}
	}
}

void UPaper2DPlusBlueprintLibrary::DrawActorDebugHitboxes(
	UObject* WorldContext,
	AActor* Actor,
	float Duration,
	float Thickness,
	bool bDrawSockets)
{
	FActorHitboxContext Ctx;
	if (!TryResolveActorContext(Actor, Ctx)) return;
	DrawDebugHitboxes(WorldContext, Ctx.FrameData, Ctx.WorldPosition, Ctx.bFlipX, Ctx.ScaleX, Ctx.ScaleY, Duration, Thickness, bDrawSockets);
}

void UPaper2DPlusBlueprintLibrary::DrawDebugHitbox(
	UObject* WorldContext,
	const FHitboxData& Hitbox,
	FVector WorldPosition,
	bool bFlipX,
	float ScaleX,
	float ScaleY,
	FLinearColor Color,
	bool bUseTypeColor,
	float Duration,
	float Thickness)
{
	UWorld* World = GetWorldFromContext(WorldContext);
	if (!World) return;

	FColor DrawColor = bUseTypeColor ? GetDebugColorForType(Hitbox.Type) : Color.ToFColor(true);

	float X = 0.0f;
	float Z = 0.0f;
	float W = 0.0f;
	float H = 0.0f;
	GetScaledHitboxRect(Hitbox, bFlipX, ScaleX, ScaleY, X, Z, W, H);

	FVector BoxCenter(WorldPosition.X + X + W * 0.5f, WorldPosition.Y, WorldPosition.Z + Z + H * 0.5f);
	FVector BoxExtent(W * 0.5f, 2.0f, H * 0.5f);

	DrawDebugBox(World, BoxCenter, BoxExtent, DrawColor, false, Duration, 0, Thickness);

	FVector Min(WorldPosition.X + X, WorldPosition.Y, WorldPosition.Z + Z);
	FVector Max(WorldPosition.X + X + W, WorldPosition.Y, WorldPosition.Z + Z + H);

	DrawDebugLine(World, FVector(Min.X, Min.Y, Min.Z), FVector(Max.X, Min.Y, Min.Z), DrawColor, false, Duration, 0, Thickness);
	DrawDebugLine(World, FVector(Max.X, Min.Y, Min.Z), FVector(Max.X, Min.Y, Max.Z), DrawColor, false, Duration, 0, Thickness);
	DrawDebugLine(World, FVector(Max.X, Min.Y, Max.Z), FVector(Min.X, Min.Y, Max.Z), DrawColor, false, Duration, 0, Thickness);
	DrawDebugLine(World, FVector(Min.X, Min.Y, Max.Z), FVector(Min.X, Min.Y, Min.Z), DrawColor, false, Duration, 0, Thickness);
}
