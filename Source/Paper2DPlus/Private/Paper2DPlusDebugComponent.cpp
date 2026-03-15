// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "Paper2DPlusDebugComponent.h"
#include "Paper2DPlusCharacterProfileComponent.h"
#include "Paper2DPlusModule.h"
#include "Paper2DPlusBlueprintLibrary.h"
#include "PaperFlipbookComponent.h"
#include "PaperFlipbook.h"
#include "PaperSprite.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

namespace
{
	bool TryAdjustFrameDataForSpritePivot(
		UPaperFlipbook* Flipbook,
		float PlaybackPosition,
		bool bFlipX,
		float Scale,
		FFrameHitboxData& InOutFrameData,
		FVector& InOutWorldPosition)
	{
		if (!Flipbook)
		{
			return false;
		}

		const int32 NumKeyFrames = Flipbook->GetNumKeyFrames();
		const float TotalDuration = Flipbook->GetTotalDuration();
		if (NumKeyFrames <= 0 || TotalDuration <= 0.0f)
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
		const FVector2D PivotLocal = KeyFrame.Sprite->GetPivotPosition() - KeyFrame.Sprite->GetSourceUV();
		const int32 PivotXInt = FMath::FloorToInt(PivotLocal.X);
		const int32 PivotYInt = FMath::FloorToInt(PivotLocal.Y);
		const float PivotXFrac = PivotLocal.X - static_cast<float>(PivotXInt);
		const float PivotYFrac = PivotLocal.Y - static_cast<float>(PivotYInt);

		for (FHitboxData& Hitbox : InOutFrameData.Hitboxes)
		{
			Hitbox.X -= PivotXInt;
			Hitbox.Y = PivotYInt - Hitbox.Y - Hitbox.Height;
		}

		for (FSocketData& Socket : InOutFrameData.Sockets)
		{
			Socket.X -= PivotXInt;
			Socket.Y = PivotYInt - Socket.Y;
		}

		InOutWorldPosition.X += (bFlipX ? PivotXFrac : -PivotXFrac) * Scale;
		InOutWorldPosition.Z += PivotYFrac * Scale;
		return true;
#else
		return false;
#endif
	}
}
UPaper2DPlusDebugComponent::UPaper2DPlusDebugComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;

#if UE_BUILD_SHIPPING
	PrimaryComponentTick.bCanEverTick = false;
#endif
}

void UPaper2DPlusDebugComponent::BeginPlay()
{
	Super::BeginPlay();

	AActor* Owner = GetOwner();
	if (!Owner) return;

	// Auto-populate from CharacterProfileComponent if present and properties not already set
	UPaper2DPlusCharacterProfileComponent* DataComp = Owner->FindComponentByClass<UPaper2DPlusCharacterProfileComponent>();
	if (DataComp)
	{
		bOwnerHasDataComponent = true;
		if (!CharacterProfile)
		{
			CharacterProfile = DataComp->CharacterProfile;
		}
		if (!FlipbookComponent)
		{
			FlipbookComponent = DataComp->GetResolvedFlipbookComponent();
		}
	}
	else if (!FlipbookComponent)
	{
		AutoFindFlipbookComponent();
	}
}

void UPaper2DPlusDebugComponent::AutoFindFlipbookComponent()
{
	AActor* Owner = GetOwner();
	if (!Owner) return;

	FlipbookComponent = Owner->FindComponentByClass<UPaperFlipbookComponent>();

	if (FlipbookComponent)
	{
		UE_LOG(LogPaper2DPlus, Verbose, TEXT("DebugComponent: Auto-found FlipbookComponent on %s"), *Owner->GetName());
	}
}

void UPaper2DPlusDebugComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

#if !UE_BUILD_SHIPPING
	if (bEnableDebugDraw)
	{
		DrawHitboxesNow(0.0f);
	}
#endif
}

void UPaper2DPlusDebugComponent::SetDebugDrawEnabled(bool bEnabled)
{
	bEnableDebugDraw = bEnabled;
}

void UPaper2DPlusDebugComponent::SetFlipX(bool bNewFlipX)
{
	bFlipX = bNewFlipX;
}

void UPaper2DPlusDebugComponent::DrawHitboxesNow(float Duration)
{
#if !UE_BUILD_SHIPPING
	AActor* Owner = GetOwner();
	if (!Owner) return;

	if (bOwnerHasDataComponent)
	{
		TArray<FWorldHitbox> WorldHitboxes;
		UPaper2DPlusBlueprintLibrary::GetActorHitboxes(Owner, WorldHitboxes);

		for (const FWorldHitbox& Hitbox : WorldHitboxes)
		{
			bool bShouldDraw = false;
			switch (Hitbox.Type)
			{
				case EHitboxType::Attack: bShouldDraw = bDrawAttackHitboxes; break;
				case EHitboxType::Hurtbox: bShouldDraw = bDrawHurtboxes; break;
				case EHitboxType::Collision: bShouldDraw = bDrawCollisionBoxes; break;
			}

			if (bShouldDraw)
			{
				DrawWorldHitbox(Hitbox, Duration);
			}
		}

		if (bDrawSockets)
		{
			TArray<FWorldSocket> WorldSockets;
			UPaper2DPlusBlueprintLibrary::GetActorSockets(Owner, WorldSockets);
			for (const FWorldSocket& Socket : WorldSockets)
			{
				DrawWorldSocket(Socket, Duration);
			}
		}
		return;
	}

	if (!CharacterProfile || !IsValid(FlipbookComponent)) return;

	const FVector CompScale = FlipbookComponent->GetComponentScale();
	const float Yaw = FMath::Abs(FlipbookComponent->GetComponentRotation().Yaw);
	bFlipX = (Yaw > 90.0f && Yaw < 270.0f) || CompScale.X < 0.0f;
	Scale = FMath::Max(FMath::Abs(CompScale.X), KINDA_SMALL_NUMBER);

	UPaperFlipbook* Flipbook = FlipbookComponent->GetFlipbook();
	if (!Flipbook) return;

	FFrameHitboxData FrameData;
	if (!ResolveFrameData(Flipbook, FrameData)) return;

	FVector WorldPosition = FlipbookComponent->GetComponentLocation();
	TryAdjustFrameDataForSpritePivot(Flipbook, FlipbookComponent->GetPlaybackPosition(), bFlipX, Scale, FrameData, WorldPosition);

	for (const FHitboxData& Hitbox : FrameData.Hitboxes)
	{
		bool bShouldDraw = false;

		switch (Hitbox.Type)
		{
			case EHitboxType::Attack: bShouldDraw = bDrawAttackHitboxes; break;
			case EHitboxType::Hurtbox: bShouldDraw = bDrawHurtboxes; break;
			case EHitboxType::Collision: bShouldDraw = bDrawCollisionBoxes; break;
		}

		if (bShouldDraw)
		{
			DrawHitbox(Hitbox, WorldPosition, Duration);
		}
	}

	if (bDrawSockets)
	{
		for (const FSocketData& Socket : FrameData.Sockets)
		{
			DrawSocket(Socket, WorldPosition, Duration);
		}
	}
#endif
}
void UPaper2DPlusDebugComponent::DrawHitbox(const FHitboxData& Hitbox, const FVector& WorldPosition, float Duration)
{
	UWorld* World = GetWorld();
	if (!World) return;

	FColor DrawColor = GetColorForType(Hitbox.Type);

	float X = Hitbox.X * Scale;
	float Z = Hitbox.Y * Scale;
	float W = Hitbox.Width * Scale;
	float H = Hitbox.Height * Scale;

	if (bFlipX) X = -(X + W);

	FVector Min(WorldPosition.X + X, WorldPosition.Y, WorldPosition.Z + Z);
	FVector Max(WorldPosition.X + X + W, WorldPosition.Y, WorldPosition.Z + Z + H);

	DrawDebugLine(World, FVector(Min.X, Min.Y, Min.Z), FVector(Max.X, Min.Y, Min.Z), DrawColor, false, Duration, 0, LineThickness);
	DrawDebugLine(World, FVector(Max.X, Min.Y, Min.Z), FVector(Max.X, Min.Y, Max.Z), DrawColor, false, Duration, 0, LineThickness);
	DrawDebugLine(World, FVector(Max.X, Min.Y, Max.Z), FVector(Min.X, Min.Y, Max.Z), DrawColor, false, Duration, 0, LineThickness);
	DrawDebugLine(World, FVector(Min.X, Min.Y, Max.Z), FVector(Min.X, Min.Y, Min.Z), DrawColor, false, Duration, 0, LineThickness);

	FVector BoxCenter = (Min + Max) * 0.5f;
	FVector BoxExtent((Max.X - Min.X) * 0.5f, 2.0f, (Max.Z - Min.Z) * 0.5f);
	DrawDebugBox(World, BoxCenter, BoxExtent, DrawColor, false, Duration, 0, LineThickness * 0.5f);
}

void UPaper2DPlusDebugComponent::DrawSocket(const FSocketData& Socket, const FVector& WorldPosition, float Duration)
{
	UWorld* World = GetWorld();
	if (!World) return;

	FVector SocketWorld = UPaper2DPlusBlueprintLibrary::SocketToWorldSpace3D(Socket, WorldPosition, bFlipX, Scale, Scale);

	float CrossSize = 5.0f * Scale;
	DrawDebugLine(World, SocketWorld - FVector(CrossSize, 0, 0), SocketWorld + FVector(CrossSize, 0, 0), SocketColor, false, Duration, 0, LineThickness);
	DrawDebugLine(World, SocketWorld - FVector(0, 0, CrossSize), SocketWorld + FVector(0, 0, CrossSize), SocketColor, false, Duration, 0, LineThickness);
	DrawDebugPoint(World, SocketWorld, 8.0f, SocketColor, false, Duration);
	DrawDebugString(World, SocketWorld + FVector(0, 0, CrossSize + 5.0f), Socket.Name, nullptr, SocketColor, Duration, false, 1.0f);
}

void UPaper2DPlusDebugComponent::DrawWorldHitbox(const FWorldHitbox& Hitbox, float Duration)
{
	UWorld* World = GetWorld();
	if (!World) return;

	FColor DrawColor = GetColorForType(Hitbox.Type);
	const FVector Min = Hitbox.Center - Hitbox.Extents;
	const FVector Max = Hitbox.Center + Hitbox.Extents;

	DrawDebugLine(World, FVector(Min.X, Min.Y, Min.Z), FVector(Max.X, Min.Y, Min.Z), DrawColor, false, Duration, 0, LineThickness);
	DrawDebugLine(World, FVector(Max.X, Min.Y, Min.Z), FVector(Max.X, Min.Y, Max.Z), DrawColor, false, Duration, 0, LineThickness);
	DrawDebugLine(World, FVector(Max.X, Min.Y, Max.Z), FVector(Min.X, Min.Y, Max.Z), DrawColor, false, Duration, 0, LineThickness);
	DrawDebugLine(World, FVector(Min.X, Min.Y, Max.Z), FVector(Min.X, Min.Y, Min.Z), DrawColor, false, Duration, 0, LineThickness);

	DrawDebugBox(World, Hitbox.Center, Hitbox.Extents, DrawColor, false, Duration, 0, LineThickness * 0.5f);
}

void UPaper2DPlusDebugComponent::DrawWorldSocket(const FWorldSocket& Socket, float Duration)
{
	UWorld* World = GetWorld();
	if (!World) return;

	const float CrossSize = 5.0f;
	DrawDebugLine(World, Socket.Location - FVector(CrossSize, 0, 0), Socket.Location + FVector(CrossSize, 0, 0), SocketColor, false, Duration, 0, LineThickness);
	DrawDebugLine(World, Socket.Location - FVector(0, 0, CrossSize), Socket.Location + FVector(0, 0, CrossSize), SocketColor, false, Duration, 0, LineThickness);
	DrawDebugPoint(World, Socket.Location, 8.0f, SocketColor, false, Duration);
	DrawDebugString(World, Socket.Location + FVector(0, 0, CrossSize + 5.0f), Socket.Name, nullptr, SocketColor, Duration, false, 1.0f);
}
bool UPaper2DPlusDebugComponent::ResolveFrameData(UPaperFlipbook* Flipbook, FFrameHitboxData& OutFrameData) const
{
	return UPaper2DPlusBlueprintLibrary::ResolveFrameFromPlayback(
		CharacterProfile, Flipbook, FlipbookComponent->GetPlaybackPosition(), OutFrameData);
}

FColor UPaper2DPlusDebugComponent::GetColorForType(EHitboxType Type) const
{
	switch (Type)
	{
		case EHitboxType::Attack: return AttackColor;
		case EHitboxType::Hurtbox: return HurtboxColor;
		case EHitboxType::Collision: return CollisionColor;
		default: return FColor::White;
	}
}
