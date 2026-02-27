// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "Paper2DPlusBlueprintLibrary.h"
#include "Paper2DPlusSettings.h"
#include "Paper2DPlusCharacterDataComponent.h"
#include "Paper2DPlusModule.h"
#include "PaperFlipbook.h"
#include "PaperFlipbookComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

namespace
{
	void GetScaledHitboxRect(const FHitboxData& Hitbox, bool bFlipX, float Scale, float& OutX, float& OutY, float& OutW, float& OutH)
	{
		OutX = Hitbox.X * Scale;
		OutY = Hitbox.Y * Scale;
		OutW = Hitbox.Width * Scale;
		OutH = Hitbox.Height * Scale;

		if (bFlipX)
		{
			OutX = -(OutX + OutW);
		}
	}

	bool TryGetAttackAndHurtBoxes(
		const FFrameHitboxData& AttackerFrame,
		const FFrameHitboxData& DefenderFrame,
		TArray<FHitboxData>& OutAttackBoxes,
		TArray<FHitboxData>& OutHurtBoxes)
	{
		OutAttackBoxes = AttackerFrame.GetHitboxesByType(EHitboxType::Attack);
		if (OutAttackBoxes.IsEmpty())
		{
			return false;
		}

		OutHurtBoxes = DefenderFrame.GetHitboxesByType(EHitboxType::Hurtbox);
		return !OutHurtBoxes.IsEmpty();
	}

	bool TryResolveFrameData(
		UPaper2DPlusCharacterDataAsset* CharacterData,
		UPaperFlipbook* Flipbook,
		float PlaybackPosition,
		FFrameHitboxData& OutFrameData)
	{
		return UPaper2DPlusBlueprintLibrary::ResolveFrameFromPlayback(CharacterData, Flipbook, PlaybackPosition, OutFrameData);
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
		UPaper2DPlusCharacterDataAsset* CharacterData = nullptr;
		FFrameHitboxData FrameData;
		FVector WorldPosition = FVector::ZeroVector;
		bool bFlipX = false;
		float Scale = 1.0f;
	};

	static TSet<TWeakObjectPtr<AActor>> WarnedActors;

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

		CleanupStaleWarnings();

		UPaper2DPlusCharacterDataComponent* DataComp = Actor->FindComponentByClass<UPaper2DPlusCharacterDataComponent>();
		if (!DataComp)
		{
			if (!WarnedActors.Contains(Actor))
			{
				UE_LOG(LogPaper2DPlus, Warning,
					TEXT("TryResolveActorContext: Actor '%s' has no Paper2DPlusCharacterDataComponent"),
					*Actor->GetName());
				WarnedActors.Add(Actor);
			}
			return false;
		}

		OutContext.CharacterData = DataComp->CharacterData;
		if (!OutContext.CharacterData)
		{
			if (!WarnedActors.Contains(Actor))
			{
				UE_LOG(LogPaper2DPlus, Warning,
					TEXT("TryResolveActorContext: Actor '%s' CharacterDataComponent has no CharacterData asset set"),
					*Actor->GetName());
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
		if (!Flipbook) return false;

		if (!TryResolveFrameData(OutContext.CharacterData, Flipbook, FlipbookComp->GetPlaybackPosition(), OutContext.FrameData))
			return false;

		OutContext.WorldPosition = Actor->GetActorLocation();

		const float XScale = Actor->GetActorScale3D().X;
		OutContext.bFlipX = XScale < 0.0f;
		OutContext.Scale = FMath::Max(FMath::Abs(XScale), KINDA_SMALL_NUMBER);

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

FVector2D UPaper2DPlusBlueprintLibrary::SocketToWorldSpace(const FSocketData& Socket, FVector2D WorldPosition, bool bFlipX, float Scale)
{
	float X = Socket.X * Scale;
	float Y = Socket.Y * Scale;

	if (bFlipX)
	{
		X = -X;
	}

	return FVector2D(WorldPosition.X + X, WorldPosition.Y + Y);
}

FVector UPaper2DPlusBlueprintLibrary::SocketToWorldSpace3D(const FSocketData& Socket, FVector WorldPosition, bool bFlipX, float Scale)
{
	FVector2D Pos2D = SocketToWorldSpace(Socket, FVector2D(WorldPosition.X, WorldPosition.Z), bFlipX, Scale);
	return FVector(Pos2D.X, WorldPosition.Y, Pos2D.Y);
}

// ==========================================
// COLLISION DETECTION
// ==========================================

bool UPaper2DPlusBlueprintLibrary::DoBoxesOverlap(const FBox2D& BoxA, const FBox2D& BoxB)
{
	return BoxA.Intersect(BoxB);
}

bool UPaper2DPlusBlueprintLibrary::CheckHitboxCollision(
	const FFrameHitboxData& AttackerFrame,
	FVector2D AttackerPosition,
	bool bAttackerFlipX,
	float AttackerScale,
	const FFrameHitboxData& DefenderFrame,
	FVector2D DefenderPosition,
	bool bDefenderFlipX,
	float DefenderScale,
	TArray<FHitboxCollisionResult>& OutResults)
{
	OutResults.Empty();

	TArray<FHitboxData> AttackBoxes;
	TArray<FHitboxData> HurtBoxes;
	if (!TryGetAttackAndHurtBoxes(AttackerFrame, DefenderFrame, AttackBoxes, HurtBoxes)) return false;

	bool bAnyHit = false;

	for (const FHitboxData& Attack : AttackBoxes)
	{
		FBox2D AttackWorld = HitboxToWorldSpace(Attack, AttackerPosition, bAttackerFlipX, AttackerScale);

		for (const FHitboxData& Hurt : HurtBoxes)
		{
			FBox2D HurtWorld = HitboxToWorldSpace(Hurt, DefenderPosition, bDefenderFlipX, DefenderScale);

			if (AttackWorld.Intersect(HurtWorld))
			{
				FHitboxCollisionResult Result;
				Result.bHit = true;
				Result.AttackHitbox = Attack;
				Result.HurtHitbox = Hurt;
				Result.Damage = Attack.Damage;
				Result.Knockback = Attack.Knockback;

				FBox2D Overlap(
					FVector2D(FMath::Max(AttackWorld.Min.X, HurtWorld.Min.X), FMath::Max(AttackWorld.Min.Y, HurtWorld.Min.Y)),
					FVector2D(FMath::Min(AttackWorld.Max.X, HurtWorld.Max.X), FMath::Min(AttackWorld.Max.Y, HurtWorld.Max.Y))
				);
				Result.HitLocation = Overlap.GetCenter();

				OutResults.Add(Result);
				bAnyHit = true;
			}
		}
	}

	return bAnyHit;
}

bool UPaper2DPlusBlueprintLibrary::CheckHitboxCollision3D(
	const FFrameHitboxData& AttackerFrame,
	FVector AttackerPosition,
	bool bAttackerFlipX,
	float AttackerScale,
	const FFrameHitboxData& DefenderFrame,
	FVector DefenderPosition,
	bool bDefenderFlipX,
	float DefenderScale,
	TArray<FHitboxCollisionResult>& OutResults)
{
	// First do standard 2D collision check (X/Z axes)
	bool bAnyHit = CheckHitboxCollision(
		AttackerFrame, FVector2D(AttackerPosition.X, AttackerPosition.Z), bAttackerFlipX, AttackerScale,
		DefenderFrame, FVector2D(DefenderPosition.X, DefenderPosition.Z), bDefenderFlipX, DefenderScale,
		OutResults
	);

	// When 3D depth is enabled, filter out results that don't overlap on the Y axis (depth)
	if (bAnyHit && GetDefault<UPaper2DPlusSettings>()->bEnable3DDepth)
	{
		static constexpr float DefaultDepth = 32.0f;

		for (int32 i = OutResults.Num() - 1; i >= 0; --i)
		{
			const FHitboxData& AHB = OutResults[i].AttackHitbox;
			const FHitboxData& DHB = OutResults[i].HurtHitbox;

			float ADepthMin = AttackerPosition.Y + AHB.Z * AttackerScale;
			float ADepthExtent = (AHB.Depth > 0) ? AHB.Depth * AttackerScale : DefaultDepth;
			float ADepthMax = ADepthMin + ADepthExtent;

			float DDepthMin = DefenderPosition.Y + DHB.Z * DefenderScale;
			float DDepthExtent = (DHB.Depth > 0) ? DHB.Depth * DefenderScale : DefaultDepth;
			float DDepthMax = DDepthMin + DDepthExtent;

			if (ADepthMax <= DDepthMin || DDepthMax <= ADepthMin)
			{
				OutResults.RemoveAt(i);
			}
		}

		bAnyHit = OutResults.Num() > 0;
	}

	return bAnyHit;
}

bool UPaper2DPlusBlueprintLibrary::QuickHitCheckFromFrames(
	const FFrameHitboxData& AttackerFrame,
	FVector2D AttackerPosition,
	bool bAttackerFlipX,
	float AttackerScale,
	const FFrameHitboxData& DefenderFrame,
	FVector2D DefenderPosition,
	bool bDefenderFlipX,
	float DefenderScale)
{
	TArray<FHitboxData> AttackBoxes;
	TArray<FHitboxData> HurtBoxes;
	if (!TryGetAttackAndHurtBoxes(AttackerFrame, DefenderFrame, AttackBoxes, HurtBoxes)) return false;

	for (const FHitboxData& Attack : AttackBoxes)
	{
		FBox2D AttackWorld = HitboxToWorldSpace(Attack, AttackerPosition, bAttackerFlipX, AttackerScale);

		for (const FHitboxData& Hurt : HurtBoxes)
		{
			FBox2D HurtWorld = HitboxToWorldSpace(Hurt, DefenderPosition, bDefenderFlipX, DefenderScale);

			if (AttackWorld.Intersect(HurtWorld))
			{
				return true;
			}
		}
	}

	return false;
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

	return CheckHitboxCollision(
		AttackerCtx.FrameData,
		FVector2D(AttackerCtx.WorldPosition.X, AttackerCtx.WorldPosition.Z),
		AttackerCtx.bFlipX, AttackerCtx.Scale,
		DefenderCtx.FrameData,
		FVector2D(DefenderCtx.WorldPosition.X, DefenderCtx.WorldPosition.Z),
		DefenderCtx.bFlipX, DefenderCtx.Scale,
		OutResults
	);
}

bool UPaper2DPlusBlueprintLibrary::QuickHitCheck(AActor* Attacker, AActor* Defender)
{
	FActorHitboxContext AttackerCtx, DefenderCtx;
	if (!TryResolveActorContext(Attacker, AttackerCtx)) return false;
	if (!TryResolveActorContext(Defender, DefenderCtx)) return false;

	return QuickHitCheckFromFrames(
		AttackerCtx.FrameData,
		FVector2D(AttackerCtx.WorldPosition.X, AttackerCtx.WorldPosition.Z),
		AttackerCtx.bFlipX, AttackerCtx.Scale,
		DefenderCtx.FrameData,
		FVector2D(DefenderCtx.WorldPosition.X, DefenderCtx.WorldPosition.Z),
		DefenderCtx.bFlipX, DefenderCtx.Scale
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

bool UPaper2DPlusBlueprintLibrary::GetFrameDamageAndKnockback(AActor* Actor, int32& OutDamage, int32& OutKnockback)
{
	OutDamage = 0;
	OutKnockback = 0;

	FActorHitboxContext Ctx;
	if (!TryResolveActorContext(Actor, Ctx)) return false;

	bool bHasAttack = false;
	for (const FHitboxData& Hitbox : Ctx.FrameData.Hitboxes)
	{
		if (Hitbox.Type == EHitboxType::Attack)
		{
			bHasAttack = true;
			OutDamage += Hitbox.Damage;
			if (Hitbox.Knockback > OutKnockback) OutKnockback = Hitbox.Knockback;
		}
	}
	return bHasAttack;
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

TArray<FHitboxData> UPaper2DPlusBlueprintLibrary::GetAttackHitboxes(const FFrameHitboxData& FrameData)
{
	return FrameData.GetHitboxesByType(EHitboxType::Attack);
}

TArray<FHitboxData> UPaper2DPlusBlueprintLibrary::GetHurtboxes(const FFrameHitboxData& FrameData)
{
	return FrameData.GetHitboxesByType(EHitboxType::Hurtbox);
}

TArray<FHitboxData> UPaper2DPlusBlueprintLibrary::GetCollisionBoxes(const FFrameHitboxData& FrameData)
{
	return FrameData.GetHitboxesByType(EHitboxType::Collision);
}

bool UPaper2DPlusBlueprintLibrary::HasAttackHitboxes(const FFrameHitboxData& FrameData)
{
	return FrameData.HasHitboxOfType(EHitboxType::Attack);
}

bool UPaper2DPlusBlueprintLibrary::HasHurtboxes(const FFrameHitboxData& FrameData)
{
	return FrameData.HasHitboxOfType(EHitboxType::Hurtbox);
}

bool UPaper2DPlusBlueprintLibrary::HasAnyData(const FFrameHitboxData& FrameData)
{
	return FrameData.Hitboxes.Num() > 0 || FrameData.Sockets.Num() > 0;
}

// ==========================================
// UTILITIES
// ==========================================

FString UPaper2DPlusBlueprintLibrary::HitboxTypeToString(EHitboxType Type)
{
	switch (Type)
	{
	case EHitboxType::Attack: return TEXT("Attack");
	case EHitboxType::Hurtbox: return TEXT("Hurtbox");
	case EHitboxType::Collision: return TEXT("Collision");
	default: return TEXT("Unknown");
	}
}

EHitboxType UPaper2DPlusBlueprintLibrary::StringToHitboxType(const FString& TypeString)
{
	if (TypeString.Equals(TEXT("hurtbox"), ESearchCase::IgnoreCase)) return EHitboxType::Hurtbox;
	if (TypeString.Equals(TEXT("collision"), ESearchCase::IgnoreCase)) return EHitboxType::Collision;
	return EHitboxType::Attack;
}

FVector2D UPaper2DPlusBlueprintLibrary::GetBoxCenter(const FBox2D& Box) { return Box.GetCenter(); }
FVector2D UPaper2DPlusBlueprintLibrary::GetBoxSize(const FBox2D& Box) { return Box.GetSize(); }
FBox2D UPaper2DPlusBlueprintLibrary::MakeBox2D(FVector2D Center, FVector2D HalfExtents) { return FBox2D(Center - HalfExtents, Center + HalfExtents); }

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
// ANIMATION GROUP VALIDATION
// ==========================================

TArray<FGameplayTag> UPaper2DPlusBlueprintLibrary::GetUnmappedRequiredGroups(const UPaper2DPlusCharacterDataAsset* Asset)
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

	for (const FGameplayTag& RequiredGroup : Settings->RequiredAnimationGroups)
	{
		if (!RequiredGroup.IsValid())
		{
			continue;
		}

		if (!Asset->HasGroup(RequiredGroup))
		{
			Unmapped.Add(RequiredGroup);
		}
		else
		{
			// Also check if the binding has at least one valid animation name
			if (Asset->GetAnimationCountForGroup(RequiredGroup) == 0)
			{
				Unmapped.Add(RequiredGroup);
			}
		}
	}

	return Unmapped;
}

// ==========================================
// FRAME RESOLUTION
// ==========================================

bool UPaper2DPlusBlueprintLibrary::ResolveFrameFromPlayback(
	UPaper2DPlusCharacterDataAsset* CharacterData,
	UPaperFlipbook* Flipbook,
	float PlaybackPosition,
	FFrameHitboxData& OutFrameData)
{
	if (!CharacterData || !Flipbook) return false;

	const FAnimationHitboxData* AnimData = CharacterData->FindAnimationByFlipbookPtr(Flipbook);
	if (!AnimData) return false;

	const int32 NumKeyFrames = Flipbook->GetNumKeyFrames();
	if (NumKeyFrames <= 0) return false;

	const float TotalDuration = Flipbook->GetTotalDuration();
	if (TotalDuration <= 0.0f) return false;

	float WrappedPosition = FMath::Fmod(PlaybackPosition, TotalDuration);
	if (WrappedPosition < 0.0f) WrappedPosition += TotalDuration;

	const int32 FrameIndex = FMath::Clamp(Flipbook->GetKeyFrameIndexAtTime(WrappedPosition), 0, NumKeyFrames - 1);
	if (!AnimData->Frames.IsValidIndex(FrameIndex)) return false;

	OutFrameData = AnimData->Frames[FrameIndex];
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
	float Scale,
	float Duration,
	float Thickness,
	bool bDrawSockets)
{
	UWorld* World = GetWorldFromContext(WorldContext);
	if (!World) return;

	for (const FHitboxData& Hitbox : FrameData.Hitboxes)
	{
		DrawDebugHitbox(WorldContext, Hitbox, WorldPosition, bFlipX, Scale, FLinearColor::White, true, Duration, Thickness);
	}

	if (bDrawSockets)
	{
		for (const FSocketData& Socket : FrameData.Sockets)
		{
			FVector SocketWorld = SocketToWorldSpace3D(Socket, WorldPosition, bFlipX, Scale);
			float CrossSize = 5.0f * Scale;
			DrawDebugLine(World, SocketWorld - FVector(CrossSize, 0, 0), SocketWorld + FVector(CrossSize, 0, 0), FColor::Yellow, false, Duration, 0, Thickness);
			DrawDebugLine(World, SocketWorld - FVector(0, 0, CrossSize), SocketWorld + FVector(0, 0, CrossSize), FColor::Yellow, false, Duration, 0, Thickness);
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
	DrawDebugHitboxes(WorldContext, Ctx.FrameData, Ctx.WorldPosition, Ctx.bFlipX, Ctx.Scale, Duration, Thickness, bDrawSockets);
}

void UPaper2DPlusBlueprintLibrary::DrawDebugHitbox(
	UObject* WorldContext,
	const FHitboxData& Hitbox,
	FVector WorldPosition,
	bool bFlipX,
	float Scale,
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
	GetScaledHitboxRect(Hitbox, bFlipX, Scale, X, Z, W, H);

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
