// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Paper2DPlusTypes.generated.h"

class UPaperFlipbook;
class UPaperZDAnimSequence;

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
	int32 Damage = 0;

	/** Knockback (for attack type) */
	UPROPERTY(BlueprintReadOnly, Category = "Hitbox")
	int32 Knockback = 0;
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

/**
 * A user-defined extra phase slot beyond Startup/Active/Recovery.
 * Each custom slot has its own name, color, flipbook assignment, and optional
 * PaperZD sequence — used for things like charge-up frames, held poses,
 * followthrough animations, etc. that don't fit the classic 3-phase pattern.
 */
USTRUCT(BlueprintType)
struct PAPER2DPLUS_API FCustomPhaseSlot
{
	GENERATED_BODY()

	/** Unique display name for this slot within its phase group (e.g., "Charge"). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Custom Phase")
	FString SlotName;

	/** Display color for the phase indicator bar on the assigned flipbook card. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Custom Phase")
	FLinearColor Color = FLinearColor(0.60f, 0.40f, 0.85f);

	/** Flipbook name assigned to this slot (empty = unassigned). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Custom Phase")
	FString FlipbookName;

	/** Optional PaperZD AnimSequence for this slot. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Custom Phase")
	TObjectPtr<UPaperZDAnimSequence> Sequence;
};

/**
 * A phase group represents one complete attack/action sequence.
 * Each slot references a flipbook by name — the whole flipbook IS that phase.
 */
USTRUCT(BlueprintType)
struct PAPER2DPLUS_API FPhaseGroup
{
	GENERATED_BODY()

	/** Display name for this phase group (e.g., "Ground Attack") */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Phase Group")
	FString GroupName;

	/** Flipbook name for Startup phase slot (empty = unassigned) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Phase Group")
	FString StartupFlipbook;

	/** Flipbook name for Active phase slot (empty = unassigned) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Phase Group")
	FString ActiveFlipbook;

	/** Flipbook name for Recovery phase slot (empty = unassigned) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Phase Group")
	FString RecoveryFlipbook;

	/** Optional PaperZD AnimSequence for Startup phase */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Phase Group")
	TObjectPtr<UPaperZDAnimSequence> StartupSequence;

	/** Optional PaperZD AnimSequence for Active phase */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Phase Group")
	TObjectPtr<UPaperZDAnimSequence> ActiveSequence;

	/** Optional PaperZD AnimSequence for Recovery phase */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Phase Group")
	TObjectPtr<UPaperZDAnimSequence> RecoverySequence;

	/** Extra phase slots beyond the built-in 3. Each slot is user-defined
	 *  (name, color, flipbook, optional sequence) and queryable at runtime by
	 *  name via UPaper2DPlusBlueprintLibrary::GetPhaseGroupCustomFlipbook /
	 *  HasPhaseGroupCustomSlot. Runtime lookup is a linear scan — keep slot
	 *  counts reasonable (<16 per group). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Phase Group")
	TArray<FCustomPhaseSlot> CustomSlots;

	/** Get the flipbook name for a given phase. Returns empty string if not assigned. */
	const FString& GetFlipbookForPhase(EAnimationPhase Phase) const
	{
		switch (Phase)
		{
			case EAnimationPhase::Startup: return StartupFlipbook;
			case EAnimationPhase::Active: return ActiveFlipbook;
			case EAnimationPhase::Recovery: return RecoveryFlipbook;
			default: { static FString Empty; return Empty; }
		}
	}

	/** Set the flipbook name for a given phase. */
	void SetFlipbookForPhase(EAnimationPhase Phase, const FString& FlipbookName)
	{
		switch (Phase)
		{
			case EAnimationPhase::Startup: StartupFlipbook = FlipbookName; break;
			case EAnimationPhase::Active: ActiveFlipbook = FlipbookName; break;
			case EAnimationPhase::Recovery: RecoveryFlipbook = FlipbookName; break;
			default: break;
		}
	}

	/** Get the PaperZD sequence for a given phase. */
	UPaperZDAnimSequence* GetSequenceForPhase(EAnimationPhase Phase) const
	{
		switch (Phase)
		{
			case EAnimationPhase::Startup: return StartupSequence;
			case EAnimationPhase::Active: return ActiveSequence;
			case EAnimationPhase::Recovery: return RecoverySequence;
			default: return nullptr;
		}
	}

	/** Set the PaperZD sequence for a given phase. */
	void SetSequenceForPhase(EAnimationPhase Phase, UPaperZDAnimSequence* Sequence)
	{
		switch (Phase)
		{
			case EAnimationPhase::Startup: StartupSequence = Sequence; break;
			case EAnimationPhase::Active: ActiveSequence = Sequence; break;
			case EAnimationPhase::Recovery: RecoverySequence = Sequence; break;
			default: break;
		}
	}

	/** Check if a specific phase has a flipbook assigned. */
	bool HasPhase(EAnimationPhase Phase) const
	{
		return !GetFlipbookForPhase(Phase).IsEmpty();
	}

	/** Check if this flipbook name is in any BUILT-IN slot of this group. Returns
	 *  the phase it occupies. Does NOT search custom slots — use
	 *  FindCustomSlotNameForFlipbook for that. */
	EAnimationPhase GetPhaseForFlipbook(const FString& FlipbookName) const
	{
		if (!FlipbookName.IsEmpty())
		{
			if (StartupFlipbook == FlipbookName) return EAnimationPhase::Startup;
			if (ActiveFlipbook == FlipbookName) return EAnimationPhase::Active;
			if (RecoveryFlipbook == FlipbookName) return EAnimationPhase::Recovery;
		}
		return EAnimationPhase::None;
	}

	/** Find a custom slot by name. Returns nullptr if not found. */
	const FCustomPhaseSlot* FindCustomSlot(const FString& SlotName) const
	{
		for (const FCustomPhaseSlot& Slot : CustomSlots)
		{
			if (Slot.SlotName == SlotName) return &Slot;
		}
		return nullptr;
	}

	/** Mutable version of FindCustomSlot. */
	FCustomPhaseSlot* FindCustomSlotMutable(const FString& SlotName)
	{
		for (FCustomPhaseSlot& Slot : CustomSlots)
		{
			if (Slot.SlotName == SlotName) return &Slot;
		}
		return nullptr;
	}

	/** Returns the name of the custom slot containing this flipbook, or empty
	 *  string if the flipbook isn't in any custom slot. Built-in slots are
	 *  NOT searched — use GetPhaseForFlipbook for that. */
	FString FindCustomSlotNameForFlipbook(const FString& FlipbookName) const
	{
		if (FlipbookName.IsEmpty()) return FString();
		for (const FCustomPhaseSlot& Slot : CustomSlots)
		{
			if (Slot.FlipbookName == FlipbookName) return Slot.SlotName;
		}
		return FString();
	}
};

// ==========================================
// FLIPBOOK EFFECT DATA (deprecated — retained for PostLoad migration)
// ==========================================

/**
 * DEPRECATED: Retained for PostLoad migration of legacy assets to the
 * UPaper2DPlusSpawnEffectFrameEvent frame event system. Do not use for
 * new code. Will be removed in vNEXT cleanup pass.
 */
USTRUCT(BlueprintType)
struct PAPER2DPLUS_API FFlipbookEffectData
{
	GENERATED_BODY()

	/** User-facing name for this effect. Unique within the parent flipbook. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effect")
	FString EffectName;

	/** The VFX flipbook asset (hard ref — auto-loads with owning asset). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effect")
	TObjectPtr<UPaperFlipbook> EffectFlipbook;

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

	/** Whether this frame has custom alignment applied */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sprite Alignment")
	bool bHasCustomAlignment = false;

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
};
