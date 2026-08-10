// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "Paper2DPlusBlueprintLibrary.h"
#include "Paper2DPlusSettings.h"
#include "Paper2DPlusClash.h"            // ResolveClash / AttackConnects (TASK-77 U3)
#include "Paper2DPlusClashGraphAsset.h"  // the active clash graph
#include "Paper2DPlusCharacterProfileComponent.h"
#include "Paper2DPlusFrameGeometry.h"
#include "Paper2DPlusHitboxSubsystem.h"
#include "Paper2DPlusModule.h"
#include "PaperFlipbook.h"
#include "PaperFlipbookComponent.h"
#include "PaperSprite.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

/** UPaper2DPlusBlueprintLibrary — Static Blueprint functions: hitbox collision checks, world-space coordinate transforms, damage/knockback queries. */

TSoftObjectPtr<UPaper2DPlusCharacterCatalogAsset> UPaper2DPlusBlueprintLibrary::GetDefaultCharacterCatalog()
{
	const UPaper2DPlusSettings* Settings = UPaper2DPlusSettings::Get();
	return Settings ? Settings->DefaultCharacterCatalog : TSoftObjectPtr<UPaper2DPlusCharacterCatalogAsset>();
}

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

	FWorldHitbox MakeWorldHitbox(const FHitboxData& Hitbox, const FVector& WorldPosition, bool bFlipX, float ScaleX, float ScaleY, const FGameplayTag& MoveDefaultClashCategory = FGameplayTag())
	{
		// TASK-77: carry the resolved clash category. This actor-vs-actor (non-broadphase) path works on bare
		// frames without the move's combat data, so the per-move default is empty here — the box's own tag is
		// authoritative; the broadphase path (MakeCachedWorldHitbox) supplies the move default.
		return Paper2DPlusFrameGeometry::MakeWorldHitbox(
			Hitbox,
			WorldPosition,
			bFlipX,
			ScaleX,
			ScaleY,
			MoveDefaultClashCategory);
	}

	FWorldHitbox MakeWorldHitbox(const FHitboxData& Hitbox, const FVector& WorldPosition, bool bFlipX, float Scale)
	{
		return MakeWorldHitbox(Hitbox, WorldPosition, bFlipX, Scale, Scale);
	}

	FWorldSocket MakeWorldSocket(const FSocketData& Socket, const FVector& WorldPosition, bool bFlipX, float ScaleX, float ScaleY)
	{
		return Paper2DPlusFrameGeometry::MakeWorldSocket(
			Socket,
			WorldPosition,
			bFlipX,
			ScaleX,
			ScaleY);
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
		AActor* DefenderActor,
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
				Result.DefenderActor = DefenderActor;
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
	// Base-only by contract: equip-aware = Paper2DPlusLayerCombat::ComposeCombatFrames (the component cached tier).
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

		if (DataComp->TryGetCachedHitboxContext(
			OutContext.FrameData,
			OutContext.WorldPosition,
			OutContext.bFlipX,
			OutContext.ScaleX,
			OutContext.ScaleY))
		{
			return true;
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

		// Apply the per-frame sprite pivot (top-left -> pivot space). Editor computes it live; packaged
		// reads the value baked at cook (TASK-48) — resolved uniformly by the asset across build configs.
		FVector2D PivotLocal;
		if (OutContext.CharacterProfile->GetFramePivotLocal(Flipbook, ResolvedFrameIndex, PivotLocal))
		{
			const FVector2D PivotFraction = Paper2DPlusFrameGeometry::ConvertFrameDataFromTopLeftToPivotSpace(
				OutContext.FrameData,
				PivotLocal);
			OutContext.WorldPosition = Paper2DPlusFrameGeometry::ApplyPivotFractionToWorldOrigin(
				OutContext.WorldPosition,
				PivotFraction,
				OutContext.bFlipX,
				OutContext.ScaleX,
				OutContext.ScaleY);
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
	// Legacy direct socket conversion (pivot-space Y is up-positive: + Y). Frame-aware gameplay,
	// Blueprint, and editor-preview paths share Paper2DPlusFrameGeometry::MakeWorldSocket so pivot,
	// facing, and world-origin handling cannot drift. Keep this lower-level compatibility helper exact.
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
		Defender,
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
// ACTOR-BASED FRAME DATA HELPERS
// ==========================================

float UPaper2DPlusBlueprintLibrary::GetFrameDamage(AActor* Actor)
{
	FActorHitboxContext Ctx;
	if (!TryResolveActorContext(Actor, Ctx)) return 0.f;

	float TotalDamage = 0.f;
	for (const FHitboxData& Hitbox : Ctx.FrameData.Hitboxes)
	{
		if (Hitbox.Type == EHitboxType::Attack) TotalDamage += Hitbox.Damage;
	}
	return TotalDamage;
}

float UPaper2DPlusBlueprintLibrary::GetFrameKnockback(AActor* Actor)
{
	FActorHitboxContext Ctx;
	if (!TryResolveActorContext(Actor, Ctx)) return 0.f;

	float MaxKnockback = 0.f;
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
	// Base-only by contract: equip-aware = Paper2DPlusLayerCombat::ComposeCombatFrames (the component cached tier).
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
		UPaperFlipbookComponent* FlipbookComp = ProfileComp->GetResolvedFlipbookComponent();
		if (!FlipbookComp || !FlipbookComp->GetFlipbook()) return 0.0f;

		const FFlipbookProfileEntry* Data = Profile->FindByFlipbookPtr(FlipbookComp->GetFlipbook());
		if (!Data) return 0.0f;

		return GetMaxAttackReach(*Data);
	}

	if (!Profile->Flipbooks.IsValidIndex(FlipbookIndex)) return 0.0f;

	return GetMaxAttackReach(Profile->Flipbooks[FlipbookIndex]);
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

FVector UPaper2DPlusBlueprintLibrary::ConsumeActorRootMotionDelta(AActor* Actor)
{
	if (!Actor) return FVector::ZeroVector;

	UPaper2DPlusCharacterProfileComponent* ProfileComp = Actor->FindComponentByClass<UPaper2DPlusCharacterProfileComponent>();
	if (!ProfileComp) return FVector::ZeroVector;

	return ProfileComp->ConsumeRootMotionDelta();
}

bool UPaper2DPlusBlueprintLibrary::QueryActorAttackOverlaps(AActor* Attacker, TArray<FHitboxCollisionResult>& OutResults)
{
	OutResults.Empty();
	if (!IsValid(Attacker)) return false;

	if (UWorld* World = Attacker->GetWorld())
	{
		if (UPaper2DPlusHitboxSubsystem* HitboxSubsystem = World->GetSubsystem<UPaper2DPlusHitboxSubsystem>())
		{
			return HitboxSubsystem->QueryAttackOverlaps(Attacker, OutResults);
		}
	}

	return false;
}

bool UPaper2DPlusBlueprintLibrary::QueryActorAttackClashes(AActor* Attacker, TArray<FHitboxClashResult>& OutResults)
{
	OutResults.Empty();
	if (!IsValid(Attacker)) return false;

	if (UWorld* World = Attacker->GetWorld())
	{
		if (UPaper2DPlusHitboxSubsystem* HitboxSubsystem = World->GetSubsystem<UPaper2DPlusHitboxSubsystem>())
		{
			return HitboxSubsystem->QueryAttackClashes(Attacker, OutResults);
		}
	}

	return false;
}

namespace
{
	// TASK-77 U3: the project's active clash graph asset, NULL-SAFE. DefaultClashGraph ships unassigned
	// (=None), so LoadSynchronous() returns null for every project that hasn't authored one — callers then
	// fall back to an empty graph (a resolver no-op), NEVER deref a null asset. Returns the ASSET (not the
	// graph by value) so the caller passes Graph by const-ref with no per-call edge-array copy.
	const UPaper2DPlusClashGraphAsset* ResolveActiveClashGraphAsset()
	{
		const UPaper2DPlusSettings* Settings = UPaper2DPlusSettings::Get();
		return Settings ? Settings->DefaultClashGraph.LoadSynchronous() : nullptr;
	}
}

EClashOutcome UPaper2DPlusBlueprintLibrary::GetClashOutcome(FGameplayTag AttackerCategory, FGameplayTag DefenderDefenseClass)
{
	static const FClashGraph EmptyGraph;
	const UPaper2DPlusClashGraphAsset* Asset = ResolveActiveClashGraphAsset();
	return Paper2DPlusClash::ResolveClash(AttackerCategory, DefenderDefenseClass, Asset ? Asset->Graph : EmptyGraph);
}

bool UPaper2DPlusBlueprintLibrary::WillAttackConnect(FGameplayTag AttackerCategory, FGameplayTag DefenderDefenseClass)
{
	static const FClashGraph EmptyGraph;
	const UPaper2DPlusClashGraphAsset* Asset = ResolveActiveClashGraphAsset();
	return Paper2DPlusClash::AttackConnects(AttackerCategory, DefenderDefenseClass, Asset ? Asset->Graph : EmptyGraph);
}

// ==========================================
// UTILITIES
// ==========================================

float UPaper2DPlusBlueprintLibrary::GetTotalDamage(const TArray<FHitboxCollisionResult>& Results)
{
	float Total = 0.f;
	// Only count confirmed hits (TASK-77: byte-identical today since QueryAttackOverlaps emits bHit=true only;
	// guards U4 when no-damage clash/whiff results may be kept in the array).
	for (const FHitboxCollisionResult& Result : Results) if (Result.bHit) Total += Result.Damage;
	return Total;
}

float UPaper2DPlusBlueprintLibrary::GetMaxKnockback(const TArray<FHitboxCollisionResult>& Results)
{
	float Max = 0.f;
	for (const FHitboxCollisionResult& Result : Results)
		if (Result.bHit && Result.Knockback > Max) Max = Result.Knockback;
	return Max;
}

// ==========================================
// FRAME DATA (fighting-game frame-data table — TASK-15)
// ==========================================

bool UPaper2DPlusBlueprintLibrary::GetMoveFrameData(const UPaper2DPlusCharacterProfileAsset* Asset, const FString& FlipbookName, FPaper2DPlusMoveFrameData& OutData)
{
	return FPaper2DPlusFrameData::ComputeMoveFrameData(Asset, FlipbookName, OutData);
}

TArray<FPaper2DPlusMoveFrameData> UPaper2DPlusBlueprintLibrary::GetAllMoveFrameData(const UPaper2DPlusCharacterProfileAsset* Asset)
{
	TArray<FPaper2DPlusMoveFrameData> Result;
	FPaper2DPlusFrameData::ComputeAllMoveFrameData(Asset, Result);
	return Result;
}

bool UPaper2DPlusBlueprintLibrary::GetActorMoveFrameData(AActor* Actor, const FString& FlipbookName, FPaper2DPlusMoveFrameData& OutData)
{
	OutData = FPaper2DPlusMoveFrameData();
	if (!IsValid(Actor))
	{
		return false;
	}

	UPaper2DPlusCharacterProfileComponent* ProfileComp = Actor->FindComponentByClass<UPaper2DPlusCharacterProfileComponent>();
	if (!ProfileComp || !ProfileComp->CharacterProfile)
	{
		return false;
	}

	return FPaper2DPlusFrameData::ComputeMoveFrameData(ProfileComp->CharacterProfile, FlipbookName, OutData);
}

// ==========================================
// FRAME CUES (authored placement queries; queries never execute behavior)
// ==========================================

namespace
{
	template <typename PredicateType>
	TArray<UPaper2DPlusCueBase*> Paper2DPlusBPLib_CollectFrameCues(
		const UPaper2DPlusCharacterProfileAsset* Asset,
		PredicateType Matches)
	{
		TArray<UPaper2DPlusCueBase*> Result;
		if (!Asset)
		{
			return Result;
		}

		for (const FFlipbookProfileEntry& Entry : Asset->Flipbooks)
		{
			for (const TObjectPtr<UPaper2DPlusCueBase>& CuePtr : Entry.FrameEventData.FrameCues)
			{
				UPaper2DPlusCueBase* Cue = CuePtr.Get();
				if (IsValid(Cue) && Matches(*Cue))
				{
					Result.Add(Cue);
				}
			}
		}
		return Result;
	}

	template <typename PredicateType>
	TArray<UPaper2DPlusCueBase*> Paper2DPlusBPLib_CollectEntryFrameCues(
		const FFlipbookProfileEntry* Entry,
		PredicateType Matches)
	{
		TArray<UPaper2DPlusCueBase*> Result;
		if (!Entry)
		{
			return Result;
		}

		Result.Reserve(Entry->FrameEventData.FrameCues.Num());
		for (const TObjectPtr<UPaper2DPlusCueBase>& CuePtr : Entry->FrameEventData.FrameCues)
		{
			UPaper2DPlusCueBase* Cue = CuePtr.Get();
			if (IsValid(Cue) && Matches(*Cue))
			{
				Result.Add(Cue);
			}
		}
		return Result;
	}

	TArray<UPaper2DPlusCueBase*> Paper2DPlusBPLib_CopyEntryFrameCues(
		const FFlipbookProfileEntry* Entry)
	{
		return Paper2DPlusBPLib_CollectEntryFrameCues(
			Entry,
			[](const UPaper2DPlusCueBase&) { return true; });
	}
}

EPaper2DPlusFrameCueAnchorResult UPaper2DPlusBlueprintLibrary::ResolveFrameCueAnchor(
	const FPaper2DPlusFrameCueContext& Context,
	EPaper2DPlusFrameCueAnchorKind AnchorKind,
	const FString& ProfileSocketName,
	FTransform& OutWorldTransform,
	UPaperFlipbookComponent*& OutAttachmentComponent)
{
	OutWorldTransform = FTransform::Identity;
	OutAttachmentComponent = nullptr;

	UPaperFlipbookComponent* const PlaybackComponent = Context.PlaybackComponent.Get();
	if (!IsValid(PlaybackComponent))
	{
		return EPaper2DPlusFrameCueAnchorResult::InvalidPlaybackComponent;
	}
	if (!PlaybackComponent->IsRegistered())
	{
		return EPaper2DPlusFrameCueAnchorResult::PlaybackComponentUnregistered;
	}

	AActor* const ComponentOwner = PlaybackComponent->GetOwner();
	if (!IsValid(ComponentOwner))
	{
		return EPaper2DPlusFrameCueAnchorResult::InvalidOwner;
	}
	if (!IsValid(Context.OwningActor) || Context.OwningActor.Get() != ComponentOwner)
	{
		return EPaper2DPlusFrameCueAnchorResult::OwnerMismatch;
	}

	UWorld* const ComponentWorld = PlaybackComponent->GetWorld();
	if (!ComponentWorld || ComponentOwner->GetWorld() != ComponentWorld)
	{
		return EPaper2DPlusFrameCueAnchorResult::WorldMismatch;
	}
	if (Context.ProfileComponent)
	{
		if (!IsValid(Context.ProfileComponent)
			|| Context.ProfileComponent->GetOwner() != ComponentOwner)
		{
			return EPaper2DPlusFrameCueAnchorResult::OwnerMismatch;
		}
		if (Context.ProfileComponent->GetWorld() != ComponentWorld)
		{
			return EPaper2DPlusFrameCueAnchorResult::WorldMismatch;
		}
	}

	const FTransform ComponentTransform = PlaybackComponent->GetComponentTransform();
	if (ComponentTransform.ContainsNaN())
	{
		return EPaper2DPlusFrameCueAnchorResult::NonFiniteGeometry;
	}

	if (AnchorKind == EPaper2DPlusFrameCueAnchorKind::RenderOrigin)
	{
		OutWorldTransform = ComponentTransform;
		OutAttachmentComponent = PlaybackComponent;
		return EPaper2DPlusFrameCueAnchorResult::Success;
	}
	if (AnchorKind != EPaper2DPlusFrameCueAnchorKind::ProfileSocket)
	{
		return EPaper2DPlusFrameCueAnchorResult::InvalidAnchorKind;
	}

	UPaper2DPlusCharacterProfileAsset* const CharacterProfile =
		Context.CharacterProfile.Get();
	if (!IsValid(CharacterProfile))
	{
		return EPaper2DPlusFrameCueAnchorResult::MissingProfile;
	}
	UPaperFlipbook* const Flipbook = Context.Flipbook.Get();
	if (!IsValid(Flipbook))
	{
		return EPaper2DPlusFrameCueAnchorResult::MissingFlipbook;
	}
	if (Context.AnimationName.IsNone())
	{
		return EPaper2DPlusFrameCueAnchorResult::MissingAnimation;
	}
	if (ProfileSocketName.IsEmpty())
	{
		return EPaper2DPlusFrameCueAnchorResult::MissingSocketName;
	}

	bool bProfileRowAmbiguous = false;
	const FFlipbookProfileEntry* const Entry =
		CharacterProfile->FindExactFlipbookData(
			Context.AnimationName,
			Flipbook,
			bProfileRowAmbiguous);
	if (bProfileRowAmbiguous)
	{
		return EPaper2DPlusFrameCueAnchorResult::ProfileRowAmbiguous;
	}
	if (!Entry)
	{
		return EPaper2DPlusFrameCueAnchorResult::ProfileRowNotFound;
	}

	int32 SourceFrame = Context.CurrentFrame;
	if (SourceFrame == INDEX_NONE
		&& Context.Phase == EPaper2DPlusFrameCuePhase::End)
	{
		SourceFrame = Context.PreviousFrame;
	}
	if (!Entry->CombatData.Frames.IsValidIndex(SourceFrame))
	{
		return EPaper2DPlusFrameCueAnchorResult::FrameOutOfRange;
	}

	const FSocketData* MatchingSocket = nullptr;
	for (const FSocketData& Socket : Entry->CombatData.Frames[SourceFrame].Sockets)
	{
		if (!Socket.Name.Equals(ProfileSocketName, ESearchCase::IgnoreCase))
		{
			continue;
		}
		if (MatchingSocket)
		{
			return EPaper2DPlusFrameCueAnchorResult::SocketAmbiguous;
		}
		MatchingSocket = &Socket;
	}
	if (!MatchingSocket)
	{
		return EPaper2DPlusFrameCueAnchorResult::SocketNotFound;
	}

	FVector2D PivotLocal = FVector2D::ZeroVector;
	if (!CharacterProfile->GetFramePivotLocalForEntry(
		*Entry,
		Flipbook,
		SourceFrame,
		PivotLocal))
	{
		return EPaper2DPlusFrameCueAnchorResult::PivotUnavailable;
	}

	const FVector ComponentScale = ComponentTransform.GetScale3D();
	const bool bFacingLeft =
		UPaper2DPlusCharacterProfileComponent::ResolveFacingLeft(
			ComponentScale,
			ComponentTransform.Rotator().Yaw);
	const float ScaleX = FMath::Max(FMath::Abs(ComponentScale.X), KINDA_SMALL_NUMBER);
	const float ScaleZ = FMath::Max(FMath::Abs(ComponentScale.Z), KINDA_SMALL_NUMBER);
	FVector SocketLocation = FVector::ZeroVector;
	if (!Paper2DPlusFrameGeometry::TryMakeWorldSocketLocationFromTopLeft(
		*MatchingSocket,
		PivotLocal,
		ComponentTransform.GetLocation(),
		bFacingLeft,
		ScaleX,
		ScaleZ,
		SocketLocation))
	{
		return EPaper2DPlusFrameCueAnchorResult::NonFiniteGeometry;
	}

	const FTransform SocketTransform(
		ComponentTransform.GetRotation(),
		SocketLocation,
		ComponentScale);
	if (SocketTransform.ContainsNaN())
	{
		return EPaper2DPlusFrameCueAnchorResult::NonFiniteGeometry;
	}

	OutWorldTransform = SocketTransform;
	OutAttachmentComponent = PlaybackComponent;
	return EPaper2DPlusFrameCueAnchorResult::Success;
}

TArray<UPaper2DPlusCueBase*> UPaper2DPlusBlueprintLibrary::GetFrameCuesByClass(
	const UPaper2DPlusCharacterProfileAsset* Asset,
	TSubclassOf<UPaper2DPlusCueBase> CueClass,
	bool bExactClass)
{
	const UClass* DesiredClass = CueClass.Get();
	if (!Asset || !DesiredClass)
	{
		return TArray<UPaper2DPlusCueBase*>();
	}

	return Paper2DPlusBPLib_CollectFrameCues(
		Asset,
		[DesiredClass, bExactClass](const UPaper2DPlusCueBase& Cue)
		{
			return bExactClass ? Cue.GetClass() == DesiredClass : Cue.IsA(DesiredClass);
		});
}

TArray<UPaper2DPlusCueBase*> UPaper2DPlusBlueprintLibrary::GetFrameCuesByTag(
	const UPaper2DPlusCharacterProfileAsset* Asset,
	FGameplayTag CueTag,
	bool bExactTag)
{
	if (!Asset || !CueTag.IsValid())
	{
		return TArray<UPaper2DPlusCueBase*>();
	}

	return Paper2DPlusBPLib_CollectFrameCues(
		Asset,
		[CueTag, bExactTag](const UPaper2DPlusCueBase& Cue)
		{
			return bExactTag ? Cue.CueTag == CueTag : Cue.CueTag.MatchesTag(CueTag);
		});
}

TArray<UPaper2DPlusCueBase*> UPaper2DPlusBlueprintLibrary::GetFrameCuesForAnimation(
	const UPaper2DPlusCharacterProfileAsset* Asset,
	const FString& AnimationName)
{
	if (!Asset || AnimationName.IsEmpty())
	{
		return TArray<UPaper2DPlusCueBase*>();
	}
	return Paper2DPlusBPLib_CopyEntryFrameCues(Asset->FindFlipbookDataPtr(AnimationName));
}

TArray<UPaper2DPlusCueBase*> UPaper2DPlusBlueprintLibrary::GetFrameCuesForFlipbook(
	const UPaper2DPlusCharacterProfileAsset* Asset,
	UPaperFlipbook* Flipbook)
{
	if (!Asset || !Flipbook)
	{
		return TArray<UPaper2DPlusCueBase*>();
	}
	return Paper2DPlusBPLib_CopyEntryFrameCues(Asset->FindByFlipbookPtr(Flipbook));
}

TArray<UPaper2DPlusCueBase*> UPaper2DPlusBlueprintLibrary::GetFrameCuesAtKeyFrame(
	const UPaper2DPlusCharacterProfileAsset* Asset,
	const FString& AnimationName,
	int32 KeyFrame)
{
	if (!Asset || AnimationName.IsEmpty() || KeyFrame < 0)
	{
		return TArray<UPaper2DPlusCueBase*>();
	}
	return Paper2DPlusBPLib_CollectEntryFrameCues(
		Asset->FindFlipbookDataPtr(AnimationName),
		[KeyFrame](const UPaper2DPlusCueBase& Cue)
		{
			return Cue.GetPrimaryAnchorFrame() == KeyFrame;
		});
}

TArray<UPaper2DPlusCueBase*> UPaper2DPlusBlueprintLibrary::GetFrameCuesAtKeyFrameByFlipbook(
	const UPaper2DPlusCharacterProfileAsset* Asset,
	UPaperFlipbook* Flipbook,
	int32 KeyFrame)
{
	if (!Asset || !Flipbook || KeyFrame < 0)
	{
		return TArray<UPaper2DPlusCueBase*>();
	}
	return Paper2DPlusBPLib_CollectEntryFrameCues(
		Asset->FindByFlipbookPtr(Flipbook),
		[KeyFrame](const UPaper2DPlusCueBase& Cue)
		{
			return Cue.GetPrimaryAnchorFrame() == KeyFrame;
		});
}

TArray<UPaper2DPlusCueBase*> UPaper2DPlusBlueprintLibrary::GetFrameCueRangesContainingKeyFrame(
	const UPaper2DPlusCharacterProfileAsset* Asset,
	const FString& AnimationName,
	int32 KeyFrame)
{
	if (!Asset || AnimationName.IsEmpty() || KeyFrame < 0)
	{
		return TArray<UPaper2DPlusCueBase*>();
	}
	return Paper2DPlusBPLib_CollectEntryFrameCues(
		Asset->FindFlipbookDataPtr(AnimationName),
		[KeyFrame](const UPaper2DPlusCueBase& Cue)
		{
			return Cue.IsRangeCue() && Cue.ContainsFrame(KeyFrame);
		});
}

TArray<UPaper2DPlusCueBase*> UPaper2DPlusBlueprintLibrary::GetFrameCueRangesContainingKeyFrameByFlipbook(
	const UPaper2DPlusCharacterProfileAsset* Asset,
	UPaperFlipbook* Flipbook,
	int32 KeyFrame)
{
	if (!Asset || !Flipbook || KeyFrame < 0)
	{
		return TArray<UPaper2DPlusCueBase*>();
	}
	return Paper2DPlusBPLib_CollectEntryFrameCues(
		Asset->FindByFlipbookPtr(Flipbook),
		[KeyFrame](const UPaper2DPlusCueBase& Cue)
		{
			return Cue.IsRangeCue() && Cue.ContainsFrame(KeyFrame);
		});
}

TArray<UPaper2DPlusCueBase*> UPaper2DPlusBlueprintLibrary::GetActorActiveFrameCueRanges(AActor* Actor)
{
	if (!IsValid(Actor))
	{
		return TArray<UPaper2DPlusCueBase*>();
	}

	const UPaper2DPlusCharacterProfileComponent* ProfileComponent =
		Actor->FindComponentByClass<UPaper2DPlusCharacterProfileComponent>();
	return ProfileComponent
		? ProfileComponent->GetActiveFrameCueRanges()
		: TArray<UPaper2DPlusCueBase*>();
}

// ==========================================
// COMBAT PROFILE (derived attack catalog + utility decisions)
// ==========================================

bool UPaper2DPlusBlueprintLibrary::GetActorCombatDecision(
	AActor* Actor,
	const FPaper2DPlusCombatRuntimeContext& Context,
	FPaper2DPlusCombatDecision& OutDecision,
	FName ScoringProfileName)
{
	OutDecision = FPaper2DPlusCombatDecision();
	if (!IsValid(Actor))
	{
		return false;
	}

	const UPaper2DPlusCombatProfileComponent* CombatComp = Actor->FindComponentByClass<UPaper2DPlusCombatProfileComponent>();
	if (!CombatComp || !CombatComp->CombatProfile)
	{
		return false;
	}

	const FName ResolvedProfileName = ScoringProfileName.IsNone() ? CombatComp->DefaultScoringProfileName : ScoringProfileName;
	return CombatComp->CombatProfile->GetCombatDecision(Context, OutDecision, ResolvedProfileName);
}

bool UPaper2DPlusBlueprintLibrary::GetActorAttackRangeForMove(AActor* Actor, FName MoveName, FVector2D& OutRangeLocal)
{
	OutRangeLocal = FVector2D::ZeroVector;
	if (!IsValid(Actor) || MoveName.IsNone())
	{
		return false;
	}

	const UPaper2DPlusCharacterProfileComponent* ProfileComp = Actor->FindComponentByClass<UPaper2DPlusCharacterProfileComponent>();
	if (!ProfileComp || !ProfileComp->CharacterProfile)
	{
		return false;
	}

	const FBox2D Bounds = ProfileComp->CharacterProfile->GetAttackBoundsForFlipbook(MoveName.ToString());
	if (!Bounds.bIsValid)
	{
		return false;
	}

	const float MinX = FMath::Min(Bounds.Min.X, Bounds.Max.X);
	const float MaxX = FMath::Max(Bounds.Min.X, Bounds.Max.X);
	OutRangeLocal = MaxX < 0.0f
		? FVector2D(FMath::Abs(MaxX), FMath::Abs(MinX))
		: FVector2D(FMath::Max(0.0f, MinX), FMath::Max(0.0f, MaxX));
	return true;
}

// ==========================================
// EFFECT PROFILE
// ==========================================

void UPaper2DPlusBlueprintLibrary::BreakEffectProfileEntry(
	const FPaper2DPlusEffectProfileEntry& Entry,
	UPaperFlipbook*& EffectFlipbook,
	FText& DisplayLabel,
	FGameplayTag& TypeTag,
	FGameplayTagContainer& DescriptorTags,
	FGameplayTag& LegacyCategoryAwaitingRemap,
	FName& EffectName,
	FGameplayTag& CategoryTag,
	FVector2D& Offset,
	float& Rotation,
	FVector2D& Scale,
	bool& bFlipWithCharacter,
	FLinearColor& Tint,
	FName& SocketName,
	FString& LayerScope)
{
	EffectFlipbook = Entry.LoadEffectFlipbook();
	DisplayLabel = Entry.DisplayLabel;
	TypeTag = Entry.TypeTag;
	DescriptorTags = Entry.DescriptorTags;
	LegacyCategoryAwaitingRemap = Entry.LegacyCategoryAwaitingRemap;
	EffectName = Entry.EffectName;
	CategoryTag = Entry.CategoryTag;
	Offset = Entry.Offset;
	Rotation = Entry.Rotation;
	Scale = Entry.Scale;
	bFlipWithCharacter = Entry.bFlipWithCharacter;
	Tint = Entry.Tint;
	SocketName = Entry.SocketName;
	LayerScope = Entry.LayerScope;
}

bool UPaper2DPlusBlueprintLibrary::ValidateEffectProfileAsset(
	const UPaper2DPlusEffectProfileAsset* EffectProfile,
	TArray<FPaper2DPlusEffectProfileValidationIssue>& OutIssues)
{
	OutIssues.Reset();
	return EffectProfile && EffectProfile->ValidateEffectProfileAsset(OutIssues);
}

bool UPaper2DPlusBlueprintLibrary::ResolveEffectProfileEntry(
	const UPaper2DPlusEffectProfileAsset* EffectProfile,
	FName EffectName,
	FPaper2DPlusEffectSpawnSettings& OutSettings)
{
	OutSettings = FPaper2DPlusEffectSpawnSettings();
	return EffectProfile && EffectProfile->ResolveEffectSpawnSettings(EffectName, OutSettings);
}

void UPaper2DPlusBlueprintLibrary::GetEffectProfileEntriesForCategory(
	const UPaper2DPlusEffectProfileAsset* EffectProfile,
	FGameplayTag CategoryTag,
	TArray<FPaper2DPlusEffectProfileEntry>& OutEntries)
{
	OutEntries.Reset();
	if (EffectProfile)
	{
		EffectProfile->GetEffectsByCategory(CategoryTag, OutEntries);
	}
}

void UPaper2DPlusBlueprintLibrary::GetEffectProfileFlipbooks(
	const UPaper2DPlusEffectProfileAsset* EffectProfile,
	TArray<UPaperFlipbook*>& OutFlipbooks)
{
	OutFlipbooks.Reset();
	if (EffectProfile)
	{
		OutFlipbooks = EffectProfile->GetEffectFlipbooks();
	}
}

bool UPaper2DPlusBlueprintLibrary::EffectProfileContainsFlipbook(
	const UPaper2DPlusEffectProfileAsset* EffectProfile,
	UPaperFlipbook* EffectFlipbook)
{
	return EffectProfile && EffectProfile->ContainsEffectFlipbook(EffectFlipbook);
}

void UPaper2DPlusBlueprintLibrary::GetEffectProfileFlipbooksByType(
	const UPaper2DPlusEffectProfileAsset* EffectProfile,
	FGameplayTag TypeTag,
	bool bExactMatch,
	TArray<UPaperFlipbook*>& OutFlipbooks)
{
	OutFlipbooks.Reset();
	if (EffectProfile)
	{
		OutFlipbooks = EffectProfile->GetEffectFlipbooksByType(TypeTag, bExactMatch);
	}
}

void UPaper2DPlusBlueprintLibrary::GetEffectProfileFlipbooksByDescriptor(
	const UPaper2DPlusEffectProfileAsset* EffectProfile,
	FGameplayTag DescriptorTag,
	bool bExactMatch,
	TArray<UPaperFlipbook*>& OutFlipbooks)
{
	OutFlipbooks.Reset();
	if (EffectProfile)
	{
		OutFlipbooks = EffectProfile->GetEffectFlipbooksByDescriptor(DescriptorTag, bExactMatch);
	}
}

void UPaper2DPlusBlueprintLibrary::GetEffectProfileFlipbooksWithAllDescriptors(
	const UPaper2DPlusEffectProfileAsset* EffectProfile,
	const FGameplayTagContainer& DescriptorTags,
	bool bExactMatch,
	TArray<UPaperFlipbook*>& OutFlipbooks)
{
	OutFlipbooks.Reset();
	if (EffectProfile)
	{
		OutFlipbooks = EffectProfile->GetEffectFlipbooksWithAllDescriptors(DescriptorTags, bExactMatch);
	}
}

void UPaper2DPlusBlueprintLibrary::GetEffectProfileFlipbooksWithAnyDescriptors(
	const UPaper2DPlusEffectProfileAsset* EffectProfile,
	const FGameplayTagContainer& DescriptorTags,
	bool bExactMatch,
	TArray<UPaperFlipbook*>& OutFlipbooks)
{
	OutFlipbooks.Reset();
	if (EffectProfile)
	{
		OutFlipbooks = EffectProfile->GetEffectFlipbooksWithAnyDescriptors(DescriptorTags, bExactMatch);
	}
}

// ==========================================
// AUXILIARY FRAME CURVES (TASK-74)
// ==========================================

float UPaper2DPlusBlueprintLibrary::GetMoveCurveValueAtFramePosition(
	const UPaper2DPlusCharacterProfileAsset* Asset,
	FName MoveName,
	FName CurveName,
	float FramePosition,
	float DefaultValue)
{
	// Single source of the curve eval math — every curve query funnels through here (R4: never errors,
	// returns DefaultValue when the asset / move / curve is missing or the curve is unauthored). Samples at a
	// (possibly fractional) key-frame position so callers can read the interpolated value at sub-frame playback time.
	if (!Asset)
	{
		return DefaultValue;
	}

	const FFlipbookProfileEntry* Entry = Asset->FindFlipbookDataPtr(MoveName.ToString());
	if (!Entry)
	{
		return DefaultValue;
	}

	const FPaper2DPlusFrameCurve* FrameCurve = Entry->CurveData.Curves.Find(CurveName);
	if (!FrameCurve)
	{
		return DefaultValue;
	}

	return FrameCurve->Eval(FramePosition, DefaultValue);
}

float UPaper2DPlusBlueprintLibrary::GetMoveCurveValueAtFrame(
	const UPaper2DPlusCharacterProfileAsset* Asset,
	FName MoveName,
	FName CurveName,
	int32 Frame,
	float DefaultValue)
{
	// Integer-frame convenience: forwards to the fractional-position core (the single eval source).
	return GetMoveCurveValueAtFramePosition(Asset, MoveName, CurveName, static_cast<float>(Frame), DefaultValue);
}

float UPaper2DPlusBlueprintLibrary::GetActorCurveValue(AActor* Actor, FName CurveName, float DefaultValue)
{
	if (!IsValid(Actor))
	{
		return DefaultValue;
	}

	UPaper2DPlusCharacterProfileComponent* ProfileComp = Actor->FindComponentByClass<UPaper2DPlusCharacterProfileComponent>();
	if (!ProfileComp || !ProfileComp->CharacterProfile)
	{
		return DefaultValue;
	}

	UPaperFlipbookComponent* FBComp = ProfileComp->GetResolvedFlipbookComponent();
	if (!IsValid(FBComp))
	{
		return DefaultValue;
	}

	UPaperFlipbook* Flipbook = FBComp->GetFlipbook();
	if (!Flipbook)
	{
		return DefaultValue;
	}

	// Same resolution the runtime uses for the current key frame (see Paper2DPlusDebugOverlay.cpp).
	const int32 Frame = Flipbook->GetKeyFrameIndexAtTime(FBComp->GetPlaybackPosition());

	// Resolve the move entry by the live flipbook reference (the runtime-correct mapping — the entry's
	// authored FlipbookName may differ from the UPaperFlipbook UObject name), then funnel through the
	// single-source asset query using the entry's authored move name.
	const FFlipbookProfileEntry* Entry = ProfileComp->CharacterProfile->FindByFlipbookPtr(Flipbook);
	const FName MoveName(Entry ? *Entry->Identity.FlipbookName : *Flipbook->GetName());

	return GetMoveCurveValueAtFrame(ProfileComp->CharacterProfile, MoveName, CurveName, Frame, DefaultValue);
}

// ==========================================
// OBJECT-REFERENCE VARIANTS (UPaperFlipbook* in/out)
// ==========================================
// Object-keyed siblings of the name-based queries above. Each resolves the move name via the asset's
// GetFlipbookName(object) bridge (the same FindByFlipbookPtr path the runtime uses), then forwards to
// the existing name function so the two can never drift. The transition target resolvers additionally
// translate the resolved target NAME back to its UPaperFlipbook object via GetFlipbookByName — handing
// callers an object reference out, not just a name. An object that isn't one of the asset's flipbooks
// resolves to an empty name (guarded where the forward would otherwise FName-round-trip "" into "None").

FVector2D UPaper2DPlusBlueprintLibrary::GetRootMotionAtFrameByFlipbook(const UPaper2DPlusCharacterProfileAsset* Asset, UPaperFlipbook* Flipbook, int32 FrameIndex)
{
	if (!Asset)
	{
		return FVector2D::ZeroVector;
	}
	return GetRootMotionAtFrame(Asset, Asset->GetFlipbookName(Flipbook), FrameIndex);
}

bool UPaper2DPlusBlueprintLibrary::GetMoveFrameDataByFlipbook(const UPaper2DPlusCharacterProfileAsset* Asset, UPaperFlipbook* Flipbook, FPaper2DPlusMoveFrameData& OutData)
{
	OutData = FPaper2DPlusMoveFrameData();
	if (!Asset)
	{
		return false;
	}
	return GetMoveFrameData(Asset, Asset->GetFlipbookName(Flipbook), OutData);
}

bool UPaper2DPlusBlueprintLibrary::GetActorMoveFrameDataByFlipbook(AActor* Actor, UPaperFlipbook* Flipbook, FPaper2DPlusMoveFrameData& OutData)
{
	OutData = FPaper2DPlusMoveFrameData();
	if (!IsValid(Actor))
	{
		return false;
	}

	const UPaper2DPlusCharacterProfileComponent* ProfileComp = Actor->FindComponentByClass<UPaper2DPlusCharacterProfileComponent>();
	if (!ProfileComp || !ProfileComp->CharacterProfile)
	{
		return false;
	}

	// Resolve the move name off the actor's own profile, then funnel through the name path.
	return GetActorMoveFrameData(Actor, ProfileComp->CharacterProfile->GetFlipbookName(Flipbook), OutData);
}

bool UPaper2DPlusBlueprintLibrary::GetActorAttackRangeForMoveByFlipbook(AActor* Actor, UPaperFlipbook* Flipbook, FVector2D& OutRangeLocal)
{
	OutRangeLocal = FVector2D::ZeroVector;
	if (!IsValid(Actor))
	{
		return false;
	}

	const UPaper2DPlusCharacterProfileComponent* ProfileComp = Actor->FindComponentByClass<UPaper2DPlusCharacterProfileComponent>();
	if (!ProfileComp || !ProfileComp->CharacterProfile)
	{
		return false;
	}

	const FString MoveName = ProfileComp->CharacterProfile->GetFlipbookName(Flipbook);
	if (MoveName.IsEmpty())
	{
		return false; // object isn't one of this profile's flipbooks
	}
	return GetActorAttackRangeForMove(Actor, FName(*MoveName), OutRangeLocal);
}

float UPaper2DPlusBlueprintLibrary::GetMoveCurveValueAtFramePositionByFlipbook(const UPaper2DPlusCharacterProfileAsset* Asset, UPaperFlipbook* Flipbook, FName CurveName, float FramePosition, float DefaultValue)
{
	if (!Asset)
	{
		return DefaultValue;
	}
	const FString MoveName = Asset->GetFlipbookName(Flipbook);
	if (MoveName.IsEmpty())
	{
		// Guard the empty case: FName("") is NAME_None whose ToString() is "None", which the name path
		// would mis-look-up as a move literally named "None". Short-circuit to the default instead.
		return DefaultValue;
	}
	return GetMoveCurveValueAtFramePosition(Asset, FName(*MoveName), CurveName, FramePosition, DefaultValue);
}

float UPaper2DPlusBlueprintLibrary::GetMoveCurveValueAtFrameByFlipbook(const UPaper2DPlusCharacterProfileAsset* Asset, UPaperFlipbook* Flipbook, FName CurveName, int32 Frame, float DefaultValue)
{
	// Integer-frame convenience: forwards to the object-ref fractional-position variant (which holds the
	// empty-name guard), mirroring how GetMoveCurveValueAtFrame forwards to GetMoveCurveValueAtFramePosition.
	return GetMoveCurveValueAtFramePositionByFlipbook(Asset, Flipbook, CurveName, static_cast<float>(Frame), DefaultValue);
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
	// Base-only by contract: equip-aware = Paper2DPlusLayerCombat::ComposeCombatFrames (the component cached tier).
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
