// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Paper2DPlusTypes.generated.h"

// ==========================================
// HITBOX TYPES
// ==========================================

/**
 * Type of hitbox - matches the editor export format
 */
UENUM(BlueprintType)
enum class EHitboxType : uint8
{
	Attack		UMETA(DisplayName = "Attack"),
	Hurtbox		UMETA(DisplayName = "Hurtbox"),
	Collision	UMETA(DisplayName = "Collision")
};

/**
 * Single hitbox data
 */
USTRUCT(BlueprintType)
struct PAPER2DPLUS_API FHitboxData
{
	GENERATED_BODY()

	/** Type of hitbox: attack, hurtbox, or collision */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hitbox")
	EHitboxType Type = EHitboxType::Attack;

	/** X position relative to sprite origin */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hitbox")
	int32 X = 0;

	/** Y position relative to sprite origin */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hitbox")
	int32 Y = 0;

	/** Width of the hitbox */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hitbox")
	int32 Width = 0;

	/** Height of the hitbox */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hitbox")
	int32 Height = 0;

	/** Z position (depth offset) for 2.5D games */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hitbox|Depth")
	int32 Z = 0;

	/** Depth (thickness in Z axis) for 2.5D games - 0 means use default */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hitbox|Depth")
	int32 Depth = 0;

	/** Damage dealt (for attack type) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hitbox")
	int32 Damage = 0;

	/** Knockback force (for attack type) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hitbox")
	int32 Knockback = 0;

	/** Get center point (2D) */
	FVector2D GetCenter() const
	{
		return FVector2D(X + Width * 0.5f, Y + Height * 0.5f);
	}

	/** Get center point (3D) */
	FVector GetCenter3D() const
	{
		return FVector(X + Width * 0.5f, Y + Height * 0.5f, Z + Depth * 0.5f);
	}

	/** Get rect as FBox2D */
	FBox2D GetBox2D() const
	{
		return FBox2D(FVector2D(X, Y), FVector2D(X + Width, Y + Height));
	}

	/** Get box as FBox (3D) */
	FBox GetBox3D() const
	{
		return FBox(FVector(X, Y, Z), FVector(X + Width, Y + Height, Z + Depth));
	}
};

/**
 * Socket/attachment point data
 */
USTRUCT(BlueprintType)
struct PAPER2DPLUS_API FSocketData
{
	GENERATED_BODY()

	/** Name of the socket (e.g., "Muzzle", "Hand", "Foot") */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Socket")
	FString Name;

	/** X position relative to sprite origin */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Socket")
	int32 X = 0;

	/** Y position relative to sprite origin */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Socket")
	int32 Y = 0;

	/** Get position as FVector2D */
	FVector2D GetPosition() const
	{
		return FVector2D(X, Y);
	}
};

/**
 * All hitbox/socket data for a single frame
 */
USTRUCT(BlueprintType)
struct PAPER2DPLUS_API FFrameHitboxData
{
	GENERATED_BODY()

	/** Name of the frame (filename without extension) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Frame")
	FString FrameName;

	/** All hitboxes on this frame */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Frame")
	TArray<FHitboxData> Hitboxes;

	/** All sockets/attachment points on this frame */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Frame")
	TArray<FSocketData> Sockets;

	/** When true, the character is invulnerable on this frame (e.g., dodge/roll i-frames) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Frame")
	bool bInvulnerable = false;

	/** Check if frame has any hitboxes of a specific type */
	bool HasHitboxOfType(EHitboxType Type) const
	{
		for (const FHitboxData& Hitbox : Hitboxes)
		{
			if (Hitbox.Type == Type) return true;
		}
		return false;
	}

	/** Get all hitboxes of a specific type */
	TArray<FHitboxData> GetHitboxesByType(EHitboxType Type) const
	{
		TArray<FHitboxData> Result;
		for (const FHitboxData& Hitbox : Hitboxes)
		{
			if (Hitbox.Type == Type) Result.Add(Hitbox);
		}
		return Result;
	}

	/** Find socket by name */
	const FSocketData* FindSocket(const FString& SocketName) const
	{
		for (const FSocketData& Socket : Sockets)
		{
			if (Socket.Name.Equals(SocketName, ESearchCase::IgnoreCase))
			{
				return &Socket;
			}
		}
		return nullptr;
	}
};

/**
 * Result of a hitbox collision check
 */
USTRUCT(BlueprintType)
struct PAPER2DPLUS_API FHitboxCollisionResult
{
	GENERATED_BODY()

	/** Did a collision occur? */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collision")
	bool bHit = false;

	/** The attack hitbox that made contact */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collision")
	FHitboxData AttackHitbox;

	/** The hurtbox that was hit */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collision")
	FHitboxData HurtHitbox;

	/** World-space center of the collision overlap */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collision")
	FVector2D HitLocation = FVector2D::ZeroVector;

	/** Total damage from this hit */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collision")
	int32 Damage = 0;

	/** Total knockback from this hit */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collision")
	int32 Knockback = 0;
};

// ==========================================
// SPRITE TYPES
// ==========================================

/**
 * Anchor point for sprite alignment
 */
UENUM(BlueprintType)
enum class ESpriteAnchor : uint8
{
	TopLeft         UMETA(DisplayName = "Top Left"),
	TopCenter       UMETA(DisplayName = "Top Center"),
	TopRight        UMETA(DisplayName = "Top Right"),
	CenterLeft      UMETA(DisplayName = "Center Left"),
	Center          UMETA(DisplayName = "Center"),
	CenterRight     UMETA(DisplayName = "Center Right"),
	BottomLeft      UMETA(DisplayName = "Bottom Left"),
	BottomCenter    UMETA(DisplayName = "Bottom Center"),
	BottomRight     UMETA(DisplayName = "Bottom Right")
};

/**
 * Extraction metadata for sprites
 */
USTRUCT(BlueprintType)
struct PAPER2DPLUS_API FSpriteExtractionInfo
{
	GENERATED_BODY()

	/** Position in source texture (top-left corner) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Extraction")
	FIntPoint SourceOffset = FIntPoint::ZeroValue;

	/** Alpha threshold used during extraction */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Extraction")
	int32 AlphaThreshold = 10;

	/** Padding applied during extraction */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Extraction")
	int32 Padding = 0;

	/** Timestamp of last extraction */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Extraction")
	FDateTime ExtractionTime;

	/** Display offset for sprite within animation (pixels) - used for alignment editor */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sprite Alignment")
	FIntPoint SpriteOffset = FIntPoint::ZeroValue;

	/** Whether this frame has custom alignment applied */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sprite Alignment")
	bool bHasCustomAlignment = false;

	/** Flip sprite horizontally for this frame */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sprite Transform")
	bool bFlipX = false;

	/** Flip sprite vertically for this frame */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sprite Transform")
	bool bFlipY = false;
};
