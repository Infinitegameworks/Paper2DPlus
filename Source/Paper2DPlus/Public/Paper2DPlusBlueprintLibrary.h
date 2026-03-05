// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Paper2DPlusTypes.h"
#include "Paper2DPlusCharacterDataAsset.h"
#include "Paper2DPlusBlueprintLibrary.generated.h"

/**
 * Blueprint function library for Paper2DPlus operations.
 * Provides actor-based collision detection, world-space conversion, and utility functions.
 *
 * Actor-based functions auto-resolve context via UPaper2DPlusCharacterDataComponent.
 * Layer B math primitives (FFrameHitboxData-based) are available for advanced use cases.
 */
UCLASS()
class PAPER2DPLUS_API UPaper2DPlusBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// ==========================================
	// WORLD SPACE CONVERSION
	// ==========================================

	/** Convert a hitbox to world space Box2D */
	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Conversion")
	static FBox2D HitboxToWorldSpace(const FHitboxData& Hitbox, FVector2D WorldPosition, bool bFlipX, float Scale = 1.0f);

	/** Convert a hitbox to world space with 3D vector position (uses X and Z for 2D) */
	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Conversion")
	static FBox2D HitboxToWorldSpace3D(const FHitboxData& Hitbox, FVector WorldPosition, bool bFlipX, float Scale = 1.0f);

	/** Convert socket position to world space */
	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Conversion")
	static FVector2D SocketToWorldSpace(const FSocketData& Socket, FVector2D WorldPosition, bool bFlipX, float Scale = 1.0f);

	/** Convert socket to world space with 3D vector (uses X and Z for 2D) */
	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Conversion")
	static FVector SocketToWorldSpace3D(const FSocketData& Socket, FVector WorldPosition, bool bFlipX, float Scale = 1.0f);

	// ==========================================
	// ACTOR-BASED COLLISION DETECTION
	// ==========================================

	/**
	 * Check collision between two actors' current animation frames.
	 * Auto-resolves hitbox data, position, flip, and scale from actors via UPaper2DPlusCharacterDataComponent.
	 * @param Attacker The attacking actor (must have a UPaper2DPlusCharacterDataComponent)
	 * @param Defender The defending actor (must have a UPaper2DPlusCharacterDataComponent)
	 * @param OutResults Detailed collision results for each attack-hurtbox overlap
	 * @return True if any attack hitbox overlaps any hurtbox
	 */
	UFUNCTION(BlueprintCallable, Category = "Paper2DPlus|Collision")
	static bool CheckAttackCollision(AActor* Attacker, AActor* Defender, TArray<FHitboxCollisionResult>& OutResults);

	/**
	 * Quick boolean check for any attack-hurtbox overlap between two actors.
	 * Faster than CheckAttackCollision — no detailed results.
	 */
	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Collision")
	static bool QuickHitCheck(AActor* Attacker, AActor* Defender);

	/**
	 * Get the hitbox frame data for an actor's current animation frame.
	 * Resolves flipbook and playback position automatically.
	 */
	UFUNCTION(BlueprintCallable, Category = "Paper2DPlus|Collision")
	static bool GetHitboxFrame(AActor* Actor, FFrameHitboxData& OutFrameData);

	// ==========================================
	// ACTOR-BASED WORLD HITBOXES
	// ==========================================

	/** Get all hitboxes for the actor's current frame in world space */
	UFUNCTION(BlueprintCallable, Category = "Paper2DPlus|Hitboxes")
	static bool GetActorHitboxes(AActor* Actor, TArray<FWorldHitbox>& OutHitboxes);

	/** Get only attack hitboxes for the actor's current frame in world space */
	UFUNCTION(BlueprintCallable, Category = "Paper2DPlus|Hitboxes")
	static bool GetActorAttackBoxes(AActor* Actor, TArray<FWorldHitbox>& OutHitboxes);

	/** Get only hurtboxes for the actor's current frame in world space */
	UFUNCTION(BlueprintCallable, Category = "Paper2DPlus|Hitboxes")
	static bool GetActorHurtboxes(AActor* Actor, TArray<FWorldHitbox>& OutHitboxes);

	/** Get only collision boxes for the actor's current frame in world space */
	UFUNCTION(BlueprintCallable, Category = "Paper2DPlus|Hitboxes")
	static bool GetActorCollisionBoxes(AActor* Actor, TArray<FWorldHitbox>& OutHitboxes);

	/** Get all sockets for the actor's current frame in world space */
	UFUNCTION(BlueprintCallable, Category = "Paper2DPlus|Hitboxes")
	static bool GetActorSockets(AActor* Actor, TArray<FWorldSocket>& OutSockets);

	/** Get a specific socket by name for the actor's current frame in world space */
	UFUNCTION(BlueprintCallable, Category = "Paper2DPlus|Hitboxes")
	static bool GetActorSocketByName(AActor* Actor, const FString& SocketName, FVector& OutLocation);

	// ==========================================
	// COLLISION DETECTION (Frame Data)
	// ==========================================

	/** Check if two Box2D overlap */
	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Collision")
	static bool DoBoxesOverlap(const FBox2D& BoxA, const FBox2D& BoxB);

	/** Check collision between attacker and defender hitboxes (single frame) */
	UFUNCTION(BlueprintCallable, Category = "Paper2DPlus|Collision")
	static bool CheckHitboxCollision(
		const FFrameHitboxData& AttackerFrame,
		FVector2D AttackerPosition,
		bool bAttackerFlipX,
		float AttackerScale,
		const FFrameHitboxData& DefenderFrame,
		FVector2D DefenderPosition,
		bool bDefenderFlipX,
		float DefenderScale,
		TArray<FHitboxCollisionResult>& OutResults
	);

	/** Check collision using 3D positions (uses X and Z) */
	UFUNCTION(BlueprintCallable, Category = "Paper2DPlus|Collision")
	static bool CheckHitboxCollision3D(
		const FFrameHitboxData& AttackerFrame,
		FVector AttackerPosition,
		bool bAttackerFlipX,
		float AttackerScale,
		const FFrameHitboxData& DefenderFrame,
		FVector DefenderPosition,
		bool bDefenderFlipX,
		float DefenderScale,
		TArray<FHitboxCollisionResult>& OutResults
	);

	/** Quick check if any attack hitbox overlaps any hurtbox from frame data (no detailed results) */
	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Collision")
	static bool QuickHitCheckFromFrames(
		const FFrameHitboxData& AttackerFrame,
		FVector2D AttackerPosition,
		bool bAttackerFlipX,
		float AttackerScale,
		const FFrameHitboxData& DefenderFrame,
		FVector2D DefenderPosition,
		bool bDefenderFlipX,
		float DefenderScale
	);

	// ==========================================
	// ACTOR-BASED FRAME DATA HELPERS
	// ==========================================

	/** Get the total damage of all attack hitboxes for the actor's current frame */
	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Frame")
	static int32 GetFrameDamage(AActor* Actor);

	/** Get the max knockback of all attack hitboxes for the actor's current frame */
	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Frame")
	static int32 GetFrameKnockback(AActor* Actor);

	/** Get both damage and knockback for the actor's current frame */
	UFUNCTION(BlueprintCallable, Category = "Paper2DPlus|Frame")
	static bool GetFrameDamageAndKnockback(AActor* Actor, int32& OutDamage, int32& OutKnockback);

	/** Check if the actor's current frame has any attack hitboxes */
	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Frame")
	static bool FrameHasAttack(AActor* Actor);

	/** Check if the actor's current frame is marked as invulnerable (i-frames) */
	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Frame")
	static bool IsFrameInvulnerable(AActor* Actor);

	// ==========================================
	// FRAME DATA HELPERS (from FFrameHitboxData)
	// ==========================================

	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Frame")
	static TArray<FHitboxData> GetAttackHitboxes(const FFrameHitboxData& FrameData);

	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Frame")
	static TArray<FHitboxData> GetHurtboxes(const FFrameHitboxData& FrameData);

	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Frame")
	static TArray<FHitboxData> GetCollisionBoxes(const FFrameHitboxData& FrameData);

	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Frame")
	static bool HasAttackHitboxes(const FFrameHitboxData& FrameData);

	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Frame")
	static bool HasHurtboxes(const FFrameHitboxData& FrameData);

	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Frame")
	static bool HasAnyData(const FFrameHitboxData& FrameData);

	// ==========================================
	// UTILITIES
	// ==========================================

	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Utilities")
	static FString HitboxTypeToString(EHitboxType Type);

	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Utilities")
	static EHitboxType StringToHitboxType(const FString& TypeString);

	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Utilities")
	static FVector2D GetBoxCenter(const FBox2D& Box);

	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Utilities")
	static FVector2D GetBoxSize(const FBox2D& Box);

	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Utilities")
	static FBox2D MakeBox2D(FVector2D Center, FVector2D HalfExtents);

	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Utilities")
	static int32 GetTotalDamage(const TArray<FHitboxCollisionResult>& Results);

	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Utilities")
	static int32 GetMaxKnockback(const TArray<FHitboxCollisionResult>& Results);

	// ==========================================
	// DEBUG VISUALIZATION
	// ==========================================

	/** Draw debug hitboxes for an actor's current animation frame */
	UFUNCTION(BlueprintCallable, Category = "Paper2DPlus|Debug", meta = (WorldContext = "WorldContext", DevelopmentOnly))
	static void DrawActorDebugHitboxes(
		UObject* WorldContext,
		AActor* Actor,
		float Duration = 0.0f,
		float Thickness = 1.0f,
		bool bDrawSockets = true
	);

	/** Draw debug hitboxes from frame data (Layer B) */
	UFUNCTION(BlueprintCallable, Category = "Paper2DPlus|Debug", meta = (WorldContext = "WorldContext", DevelopmentOnly))
	static void DrawDebugHitboxes(
		UObject* WorldContext,
		const FFrameHitboxData& FrameData,
		FVector WorldPosition,
		bool bFlipX,
		float Scale = 1.0f,
		float Duration = 0.0f,
		float Thickness = 1.0f,
		bool bDrawSockets = true
	);

	/** Draw a single debug hitbox */
	UFUNCTION(BlueprintCallable, Category = "Paper2DPlus|Debug", meta = (WorldContext = "WorldContext", DevelopmentOnly))
	static void DrawDebugHitbox(
		UObject* WorldContext,
		const FHitboxData& Hitbox,
		FVector WorldPosition,
		bool bFlipX,
		float Scale = 1.0f,
		FLinearColor Color = FLinearColor::White,
		bool bUseTypeColor = true,
		float Duration = 0.0f,
		float Thickness = 1.0f
	);

	// ==========================================
	// TAG MAPPING VALIDATION
	// ==========================================

	/** Get required tag mappings that are not mapped in the given asset. */
	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Tag Mappings")
	static TArray<FGameplayTag> GetUnmappedRequiredTags(const UPaper2DPlusCharacterDataAsset* Asset);

	// ==========================================
	// FRAME RESOLUTION
	// ==========================================

	/** Resolve the current frame's hitbox data from a CharacterDataAsset, flipbook, and playback position */
	static bool ResolveFrameFromPlayback(
		UPaper2DPlusCharacterDataAsset* CharacterData,
		UPaperFlipbook* Flipbook,
		float PlaybackPosition,
		FFrameHitboxData& OutFrameData);

private:
	static FColor GetDebugColorForType(EHitboxType Type);
};
