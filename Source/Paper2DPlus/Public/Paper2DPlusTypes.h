// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Paper2DPlusClashTypes.h"  // EClashOutcome (FHitboxClashResult, TASK-77 U4)
#include "Paper2DPlusTypes.generated.h"

class AActor;
class UPaperFlipbook;

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
	Collision	UMETA(DisplayName = "Collision", Hidden)
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

	/** Damage dealt (for attack type). Supports fractional values. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hitbox")
	float Damage = 0.f;

	/** Knockback force (for attack type). Supports fractional values. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hitbox")
	float Knockback = 0.f;

	/** Hit-priority clash category (TASK-77) — which RPS category this box participates as when it overlaps
	 *  another box (resolved by the clash graph). Empty = inherit the move's DefaultClashCategory, else no
	 *  clash participation (trades). Authored on Attack boxes; on a defender frame the DEFENSE class drives
	 *  resolution (see FFrameHitboxData::DefenseClass, U3). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hitbox", meta = (Categories = "Paper2DPlus.Clash.Category"))
	FGameplayTag ClashCategory;

	/** Resolve the effective clash category: this box's own tag if set, else the move-level default. */
	FGameplayTag GetResolvedClashCategory(const FGameplayTag& MoveDefault) const
	{
		return ClashCategory.IsValid() ? ClashCategory : MoveDefault;
	}

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

	/** Hit-priority DEFENSE class for this frame (TASK-77 U3) — the directional RPS defense the character has
	 *  while this frame is active. Intended for Armor / Parry / Invincible (Paper2DPlus.Clash.Category.*): when
	 *  an attacker's ClashCategory overlaps a hurtbox on this frame, the clash graph decides whether the
	 *  defense beats the attack (absorb/parry → the hit is suppressed). ORTHOGONAL to bInvulnerable: that is a
	 *  blanket game-enforced i-frame (omnidirectional, beats throws too); this is a directional, category-aware
	 *  defense (e.g. Invincible is still THROW-PUNISHABLE — no Throw>Invincible edge). Empty = no defense. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Frame", meta = (Categories = "Paper2DPlus.Clash.Category"))
	FGameplayTag DefenseClass;

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
 * A hitbox transformed into world space — ready to use for spawning, overlap checks, etc.
 */
USTRUCT(BlueprintType)
struct PAPER2DPLUS_API FWorldHitbox
{
	GENERATED_BODY()

	/** Type of hitbox */
	UPROPERTY(BlueprintReadOnly, Category = "Hitbox")
	EHitboxType Type = EHitboxType::Attack;

	/** World-space center of the box */
	UPROPERTY(BlueprintReadOnly, Category = "Hitbox")
	FVector Center = FVector::ZeroVector;

	/** Half-extents of the box */
	UPROPERTY(BlueprintReadOnly, Category = "Hitbox")
	FVector Extents = FVector::ZeroVector;

	/** Damage (for attack type) */
	UPROPERTY(BlueprintReadOnly, Category = "Hitbox")
	float Damage = 0.f;

	/** Knockback (for attack type) */
	UPROPERTY(BlueprintReadOnly, Category = "Hitbox")
	float Knockback = 0.f;

	/** Hit-priority clash category (TASK-77) — the RESOLVED category (box tag else the move default), carried
	 *  from FHitboxData by the world-space makers so the broadphase / clash resolver can read it. */
	UPROPERTY(BlueprintReadOnly, Category = "Hitbox")
	FGameplayTag ClashCategory;

	/** Hit-priority DEFENSE class (TASK-77 U3) — the defender FRAME's DefenseClass, stamped onto each cached
	 *  hurtbox in RefreshCachedWorldState so the broadphase clash resolver reads it as the defender side of a
	 *  clash (frame-level, shared by every hurtbox of the frame). Empty for attack boxes / undefended frames. */
	UPROPERTY(BlueprintReadOnly, Category = "Hitbox")
	FGameplayTag DefenseClass;
};

/**
 * A socket transformed into world space.
 */
USTRUCT(BlueprintType)
struct PAPER2DPLUS_API FWorldSocket
{
	GENERATED_BODY()

	/** Socket name */
	UPROPERTY(BlueprintReadOnly, Category = "Socket")
	FString Name;

	/** World-space location */
	UPROPERTY(BlueprintReadOnly, Category = "Socket")
	FVector Location = FVector::ZeroVector;
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

	/** The attack box in world space */
	UPROPERTY(BlueprintReadOnly, Category = "Collision")
	FWorldHitbox AttackBox;

	/** The hurtbox in world space */
	UPROPERTY(BlueprintReadOnly, Category = "Collision")
	FWorldHitbox HurtBox;

	/** Actor that owns the hurtbox hit by this attack. */
	UPROPERTY(BlueprintReadOnly, Category = "Collision")
	TObjectPtr<AActor> DefenderActor = nullptr;

	/** World-space center of the collision overlap */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collision")
	FVector2D HitLocation = FVector2D::ZeroVector;

	/** Total damage from this hit */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collision")
	float Damage = 0.f;

	/** Total knockback from this hit */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collision")
	float Knockback = 0.f;
};

/**
 * TASK-77 U4: the result of an ATTACK-vs-ATTACK clash, from one attacker's perspective. A SEPARATE advisory
 * type (not FHitboxCollisionResult) so it can never be unioned into the authoritative hurtbox-hit / damage
 * path — QueryAttackClashes is gate+advise: it REPORTS the clash; the game decides. Because ResolveClash is
 * antisymmetric, the two attackers' independent queries are automatically mirror-consistent.
 */
USTRUCT(BlueprintType)
struct PAPER2DPLUS_API FHitboxClashResult
{
	GENERATED_BODY()

	/** This attacker's own attack box (world space). */
	UPROPERTY(BlueprintReadOnly, Category = "Clash")
	FWorldHitbox AttackBox;

	/** The OTHER attacker's overlapping attack box (world space — carries its ClashCategory). */
	UPROPERTY(BlueprintReadOnly, Category = "Clash")
	FWorldHitbox OtherBox;

	/** The other attacker's actor. */
	UPROPERTY(BlueprintReadOnly, Category = "Clash")
	TObjectPtr<AActor> OtherActor = nullptr;

	/** The clash verdict from THIS attacker's perspective: AWins = my box wins, BWins = my box loses,
	 *  Trade = both land, Clash = both negate. (Whiff is v1-unreachable.) */
	UPROPERTY(BlueprintReadOnly, Category = "Clash")
	EClashOutcome Outcome = EClashOutcome::Trade;

	/** Does THIS attacker's box land? (Trade or AWins.) The mirror side reads the opposite for a priority win.
	 *  The default MUST agree with Outcome's default per ClashOutcomeDealsDamage (Trade -> true) — both are
	 *  set together in QueryAttackClashes, and the U4 test ratchets the default pair. */
	UPROPERTY(BlueprintReadOnly, Category = "Clash")
	bool bAttackConnects = true;

	/** World-space center of the overlap. */
	UPROPERTY(BlueprintReadOnly, Category = "Clash")
	FVector2D ClashLocation = FVector2D::ZeroVector;
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
	BottomRight     UMETA(DisplayName = "Bottom Right"),
	None            UMETA(DisplayName = "None")
};

// ==========================================
// ANIMATION PHASE TYPES
// ==========================================

/**
 * Animation phase classification for fighting game frame data.
 * Startup = before hitbox active, Active = hitbox can connect, Recovery = after hitbox deactivates.
 */
UENUM(BlueprintType)
enum class EAnimationPhase : uint8
{
	None		UMETA(DisplayName = "None"),
	Startup		UMETA(DisplayName = "Startup"),
	Active		UMETA(DisplayName = "Active"),
	Recovery	UMETA(DisplayName = "Recovery")
};


// (FCustomPhaseSlot + FPhaseGroup are GONE — the phase-groups feature was removed entirely in the
//  2026-07 legacy cleanup. EAnimationPhase stays: it is the derived-phase currency used by frame
//  data and the combo-chain analysis, keyed off per-flipbook `Paper2DPlus.Phase.*` tags now.)

// ==========================================
// FLIPBOOK EFFECT DATA (deprecated — retained for PostLoad migration)
// ==========================================

/**
 * DEPRECATED and inert: retained only so legacy assets keep round-tripping their authored rows
 * instead of losing them on the next save. Nothing reads it at runtime and no load-time conversion
 * remains — the Frame Cue system is the authoring model. Do not use for new code.
 *
 * Internal references (the field declaration and GetEffectFrameCount's definition) are wrapped in
 * PRAGMA_DISABLE/ENABLE_DEPRECATION_WARNINGS; UHT auto-guards the generated reflection code.
 */
USTRUCT(BlueprintType)
struct UE_DEPRECATED(5.7, "FFlipbookEffectData is inert legacy data — author Frame Cues instead.") PAPER2DPLUS_API FFlipbookEffectData
{
	GENERATED_BODY()

	/** User-facing name for this effect. Unique within the parent flipbook. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effect")
	FString EffectName;

	/** The VFX flipbook asset (hard ref — auto-loads with owning asset). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effect")
	TObjectPtr<UPaperFlipbook> EffectFlipbook = nullptr;

	/** Which frame of the character animation triggers this effect to start playing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effect")
	int32 TriggerFrame = 0;

	/** Position offset relative to the character sprite origin. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effect")
	FVector2D Offset = FVector2D::ZeroVector;

	/** Rotation in degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effect")
	float Rotation = 0.f;

	/** Scale (non-uniform). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effect")
	FVector2D Scale = FVector2D(1.0, 1.0);

	/** Mirror Offset.X when the character faces left at runtime. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effect")
	bool bFlipWithCharacter = true;

	/** Multiplicative color tint. Alpha controls opacity. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effect")
	FLinearColor Color = FLinearColor::White;

	/** Check if this effect has a valid flipbook assigned. */
	bool IsValid() const { return EffectFlipbook != nullptr; }

	/** Get the number of frames in the effect flipbook. Returns 0 if no flipbook. */
	int32 GetEffectFrameCount() const;
};

// ==========================================
// ROOT MOTION DATA
// ==========================================

/**
 * Per-frame root motion data — absolute pixel offset from animation origin.
 * Stored as a parallel array on FFlipbookProfileEntry (same pattern as FrameExtractionInfo).
 * Empty array = no root motion for this flipbook (zero cost).
 */
USTRUCT(BlueprintType)
struct PAPER2DPLUS_API FRootMotionFrameData
{
	GENERATED_BODY()

	/** Root motion position for this frame, in pixels relative to animation origin. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Root Motion")
	FVector2D Position = FVector2D::ZeroVector;
};

// ==========================================
// EXTRACTION METADATA
// ==========================================

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

	/** Computed offset from trimming: positions trimmed sprite at its correct aligned grid location.
	 *  Applied additively with SpriteOffset in DrawFlipbookSprite. Set by bulk extractor trim flow. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sprite Alignment")
	FIntPoint TrimOffset = FIntPoint::ZeroValue;

	/** Display offset for sprite within animation (pixels) - used for alignment editor */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sprite Alignment")
	FIntPoint SpriteOffset = FIntPoint::ZeroValue;

	/** Legacy alignment marker. Current alignment derives only from SpriteOffset + TrimOffset.
	 *  Retained under the original reflected name so historical asset/JSON values load; new native
	 *  or Blueprint code must not read or write it. */
	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Custom alignment is derived from SpriteOffset and TrimOffset; this marker is ignored."))
	bool bHasCustomAlignment_DEPRECATED = false;

	/** Flip sprite horizontally for this frame */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sprite Transform")
	bool bFlipX = false;

	/** Flip sprite vertically for this frame */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sprite Transform")
	bool bFlipY = false;

	/** Internal stable ordering index used to restore excluded frames to their original location. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Internal")
	int32 SourceFrameIndex = INDEX_NONE;

	/** True when this frame is excluded from the live flipbook keyframe list (non-destructive disable). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Internal")
	bool bExcludedFromFlipbook = false;

	/** Absolute disk path of the source texture file used during extraction (texture reimport tracking). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Import")
	FString SourceTextureFilePath;

	/** Serialized per-frame sprite pivot in sprite-local TOP-LEFT space (GetPivotPosition() - GetSourceUV()).
	 *  Those UPaperSprite APIs are editor-only, so the runtime hitbox/socket world conversion can't read the
	 *  live pivot in packaged builds. This caches it (refreshed on the owning asset's PreSave, so cooked
	 *  assets always carry a fresh value) and lets the non-editor pivot path apply the same adjustment the
	 *  editor does. Sentinel = un-cached -> runtime falls back to top-left origin (pre-fix behavior) until
	 *  the asset is resaved/re-cooked. See TASK-48. */
	UPROPERTY()
	FVector2D CachedPivotLocal = FVector2D(TNumericLimits<float>::Lowest(), TNumericLimits<float>::Lowest());

	/** True when CachedPivotLocal holds a real serialized pivot rather than the un-cached sentinel. */
	bool IsPivotCached() const { return CachedPivotLocal.X > TNumericLimits<float>::Lowest() * 0.5f; }
};
