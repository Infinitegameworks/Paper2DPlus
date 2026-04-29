// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Paper2DPlusTypes.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Paper2DPlusCharacterProfileComponent.h"
#include "Paper2DPlusBlueprintLibrary.generated.h"

// ─── BP Const-Query Audit (Phase 5, 2026-04-09) ─────────────────
// Audited all BlueprintPure const methods across the 3 runtime headers.
//
// Function                              | Verdict  | Notes
// --- BlueprintLibrary (static) ---
// HitboxToWorldSpace                    | SAFE     | pure math, no internal state
// HitboxToWorldSpace3D                  | SAFE     | pure math
// SocketToWorldSpace                    | SAFE     | pure math
// SocketToWorldSpace3D                  | SAFE     | pure math
// QuickHitCheck                         | SAFE     | reads current frame snapshot
// GetFrameDamage                        | SAFE     | reads current frame snapshot
// GetFrameKnockback                     | SAFE     | reads current frame snapshot
// FrameHasAttack                        | SAFE     | reads current frame snapshot
// IsFrameInvulnerable                   | SAFE     | reads current frame snapshot
// GetActorMaxAttackReach                | SAFE     | reads stable asset data
// GetActorCurrentPhase                  | SAFE     | reads stable phase data
// GetActorCurrentPhaseGroup             | SAFE     | reads stable phase data
// GetRootMotionAtFrame                  | SAFE     | pure asset lookup, no baseline
// GetActorRootMotionDelta               | DOCUMENT | reads advancing baseline; const peek, does NOT consume. See component docstring.
// GetTotalDamage                        | SAFE     | pure math on results array
// GetMaxKnockback                       | SAFE     | pure math on results array
// GetUnmappedRequiredTags               | SAFE     | reads stable settings + asset data
// --- CharacterProfileComponent ---
// GetResolvedFlipbookComponent          | SAFE     | reads stable component ref
// GetRootMotionDelta                    | DOCUMENT | reads advancing baseline; see commit 44c3280. ConsumeRootMotionDelta deferred to follow-up.
// --- CharacterProfileAsset ---
// (39 pure accessors)                   | SAFE     | all read stable asset data, no advancing state
// ─────────────────────────────────────────────────────────────────

/**
 * Blueprint function library for Paper2DPlus operations.
 * Provides actor-based collision detection, world-space conversion, and utility functions.
 *
 * Actor-based functions auto-resolve context via UPaper2DPlusCharacterProfileComponent.
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
	static FVector2D SocketToWorldSpace(const FSocketData& Socket, FVector2D WorldPosition, bool bFlipX, float ScaleX = 1.0f, float ScaleY = 1.0f);

	/** Convert socket to world space with 3D vector (uses X and Z for 2D) */
	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Conversion")
	static FVector SocketToWorldSpace3D(const FSocketData& Socket, FVector WorldPosition, bool bFlipX, float ScaleX = 1.0f, float ScaleY = 1.0f);

	// ==========================================
	// ACTOR-BASED COLLISION DETECTION
	// ==========================================

	/**
	 * Check collision between two actors' current animation frames.
	 * Auto-resolves hitbox data, position, flip, and scale from actors via UPaper2DPlusCharacterProfileComponent.
	 * @param Attacker The attacking actor (must have a UPaper2DPlusCharacterProfileComponent)
	 * @param Defender The defending actor (must have a UPaper2DPlusCharacterProfileComponent)
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

	// --- World Space ---

	/** Get all hitboxes for the actor's current frame in world space */
	UFUNCTION(BlueprintCallable, Category = "Paper2DPlus|Hitboxes|World")
	static bool GetActorWorldHitboxes(AActor* Actor, TArray<FWorldHitbox>& OutHitboxes);

	/** Get only attack hitboxes for the actor's current frame in world space */
	UFUNCTION(BlueprintCallable, Category = "Paper2DPlus|Hitboxes|World")
	static bool GetActorWorldAttackBoxes(AActor* Actor, TArray<FWorldHitbox>& OutHitboxes);

	/** Get only hurtboxes for the actor's current frame in world space */
	UFUNCTION(BlueprintCallable, Category = "Paper2DPlus|Hitboxes|World")
	static bool GetActorWorldHurtboxes(AActor* Actor, TArray<FWorldHitbox>& OutHitboxes);

	/** Get all sockets for the actor's current frame in world space */
	UFUNCTION(BlueprintCallable, Category = "Paper2DPlus|Hitboxes|World")
	static bool GetActorWorldSockets(AActor* Actor, TArray<FWorldSocket>& OutSockets);

	/** Get a specific socket by name for the actor's current frame in world space */
	UFUNCTION(BlueprintCallable, Category = "Paper2DPlus|Hitboxes|World")
	static bool GetActorWorldSocketByName(AActor* Actor, const FString& SocketName, FVector& OutLocation);

	// --- Local Space (pixel coordinates relative to sprite origin) ---

	/** Get all hitboxes for the actor's current frame in local/pixel space */
	UFUNCTION(BlueprintCallable, Category = "Paper2DPlus|Hitboxes|Local")
	static bool GetActorLocalHitboxes(AActor* Actor, TArray<FHitboxData>& OutHitboxes);

	/** Get only attack hitboxes for the actor's current frame in local/pixel space */
	UFUNCTION(BlueprintCallable, Category = "Paper2DPlus|Hitboxes|Local")
	static bool GetActorLocalAttackBoxes(AActor* Actor, TArray<FHitboxData>& OutHitboxes);

	/** Get only hurtboxes for the actor's current frame in local/pixel space */
	UFUNCTION(BlueprintCallable, Category = "Paper2DPlus|Hitboxes|Local")
	static bool GetActorLocalHurtboxes(AActor* Actor, TArray<FHitboxData>& OutHitboxes);

	/** Get all sockets for the actor's current frame in local/pixel space */
	UFUNCTION(BlueprintCallable, Category = "Paper2DPlus|Hitboxes|Local")
	static bool GetActorLocalSockets(AActor* Actor, TArray<FSocketData>& OutSockets);

	// --- Deprecated (old names, redirect to World variants) ---

	UFUNCTION(BlueprintCallable, Category = "Paper2DPlus|Hitboxes", meta=(DeprecatedFunction, DeprecationMessage="Use GetActorWorldHitboxes instead."))
	static bool GetActorHitboxes(AActor* Actor, TArray<FWorldHitbox>& OutHitboxes);

	UFUNCTION(BlueprintCallable, Category = "Paper2DPlus|Hitboxes", meta=(DeprecatedFunction, DeprecationMessage="Use GetActorWorldAttackBoxes instead."))
	static bool GetActorAttackBoxes(AActor* Actor, TArray<FWorldHitbox>& OutHitboxes);

	UFUNCTION(BlueprintCallable, Category = "Paper2DPlus|Hitboxes", meta=(DeprecatedFunction, DeprecationMessage="Use GetActorWorldHurtboxes instead."))
	static bool GetActorHurtboxes(AActor* Actor, TArray<FWorldHitbox>& OutHitboxes);

	UFUNCTION(BlueprintCallable, Category = "Paper2DPlus|Hitboxes", meta=(DeprecatedFunction, DeprecationMessage="Collision hitbox type is deprecated. Use Attack and Hurtbox types instead."))
	static bool GetActorCollisionBoxes(AActor* Actor, TArray<FWorldHitbox>& OutHitboxes);

	UFUNCTION(BlueprintCallable, Category = "Paper2DPlus|Hitboxes", meta=(DeprecatedFunction, DeprecationMessage="Use GetActorWorldSockets instead."))
	static bool GetActorSockets(AActor* Actor, TArray<FWorldSocket>& OutSockets);

	UFUNCTION(BlueprintCallable, Category = "Paper2DPlus|Hitboxes", meta=(DeprecatedFunction, DeprecationMessage="Use GetActorWorldSocketByName instead."))
	static bool GetActorSocketByName(AActor* Actor, const FString& SocketName, FVector& OutLocation);

	/**
	 * Set the CharacterProfile asset on an actor's Paper2DPlusCharacterProfileComponent.
	 * Convenience function — finds the component automatically.
	 * @return True if the component was found and the asset was set.
	 */
	UFUNCTION(BlueprintCallable, Category = "Paper2DPlus|Setup")
	static bool SetActorCharacterProfile(AActor* Actor, UPaper2DPlusCharacterProfileAsset* NewCharacterProfile);

	// ==========================================
	// ACTOR-BASED FRAME DATA HELPERS
	// ==========================================

	/** Get the total damage of all attack hitboxes for the actor's current frame */
	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Frame")
	static int32 GetFrameDamage(AActor* Actor);

	/** Get the max knockback of all attack hitboxes for the actor's current frame */
	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Frame")
	static int32 GetFrameKnockback(AActor* Actor);

	/** Check if the actor's current frame has any attack hitboxes */
	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Frame")
	static bool FrameHasAttack(AActor* Actor);

	/** Check if the actor's current frame is marked as invulnerable (i-frames) */
	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Frame")
	static bool IsFrameInvulnerable(AActor* Actor);

	/**
	 * Actor-based version: Get the maximum attack reach radius for a specific flipbook on the actor.
	 * Auto-resolves CharacterProfile from the actor's component.
	 * @param Actor The actor with a Paper2DPlusCharacterProfileComponent
	 * @param FlipbookIndex Index into the CharacterProfile's Flipbooks array (-1 for current flipbook)
	 * @return Maximum reach radius in pixels. 0 if not found or no attack hitboxes.
	 */
	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Hitboxes")
	static float GetActorMaxAttackReach(AActor* Actor, int32 FlipbookIndex = -1);

	// ==========================================
	// ANIMATION PHASE QUERIES (Actor-based)
	// ==========================================

	/**
	 * Get the animation phase for the actor's current flipbook frame.
	 * Auto-resolves from CharacterProfileComponent + FlipbookComponent.
	 */
	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Phases")
	static EAnimationPhase GetActorCurrentPhase(AActor* Actor);

	/** Get the name of the phase group the actor's current flipbook belongs to. Returns empty string if not in any group. */
	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Phases")
	static FString GetActorCurrentPhaseGroup(AActor* Actor);

	// ==========================================
	// CUSTOM PHASE SLOTS (user-defined slots beyond Startup/Active/Recovery)
	// ==========================================

	/**
	 * True if the given phase group has a custom slot with this name AND a
	 * flipbook is assigned to it. Use to gate behavior on "does this attack
	 * have a charge phase authored".
	 */
	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Phases")
	static bool HasPhaseGroupCustomSlot(
		const UPaper2DPlusCharacterProfileAsset* Asset,
		const FString& GroupName,
		const FString& CustomSlotName);

	/**
	 * Get the flipbook assigned to a custom slot in a phase group. Returns
	 * null if the group doesn't exist, the slot doesn't exist, or the slot
	 * has no flipbook assigned.
	 */
	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Phases")
	static UPaperFlipbook* GetPhaseGroupCustomFlipbook(
		const UPaper2DPlusCharacterProfileAsset* Asset,
		const FString& GroupName,
		const FString& CustomSlotName);

	/**
	 * Get the optional PaperZD AnimSequence assigned to a custom slot in a
	 * phase group. Returns null if no sequence is assigned.
	 */
	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Phases")
	static UPaperZDAnimSequence* GetPhaseGroupCustomSequence(
		const UPaper2DPlusCharacterProfileAsset* Asset,
		const FString& GroupName,
		const FString& CustomSlotName);

	/**
	 * If the actor's currently-playing flipbook is in a custom slot of the
	 * given phase group, returns the slot name. Empty string if the current
	 * flipbook is in a built-in (Startup/Active/Recovery) slot or not in this
	 * group at all.
	 */
	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Phases")
	static FString GetActorCurrentCustomSlotName(AActor* Actor, const FString& GroupName);

	// ==========================================
	// ROOT MOTION QUERIES
	// ==========================================

	/** Get the root motion offset at a specific frame (pixels). Returns ZeroVector if no data or out of range. */
	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Root Motion")
	static FVector2D GetRootMotionAtFrame(const UPaper2DPlusCharacterProfileAsset* Asset, const FString& FlipbookName, int32 FrameIndex);

	/**
	 * Get the root motion delta for the actor's current frame transition.
	 * Returns the world-space movement vector based on the change from the previous
	 * frame's root motion position to the current frame's. Requires the actor to
	 * have a UPaper2DPlusCharacterProfileComponent with root motion data authored.
	 * Returns ZeroVector if no component, no data, or no frame change.
	 *
	 * Note: This is a const peek into the component's advancing baseline state.
	 * Repeated calls within the same frame return the same delta. The baseline
	 * advances automatically when bAutoApplyRootMotion is enabled. A non-const
	 * ConsumeRootMotionDelta() for manual-drive callers is planned for a follow-up.
	 */
	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Root Motion")
	static FVector GetActorRootMotionDelta(AActor* Actor);

	// ==========================================
	// UTILITIES
	// ==========================================

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

	// ==========================================
	// TAG MAPPING VALIDATION
	// ==========================================

	/** Get required tag mappings that are not mapped in the given asset. */
	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Tag Mappings")
	static TArray<FGameplayTag> GetUnmappedRequiredTags(const UPaper2DPlusCharacterProfileAsset* Asset);

	// ==========================================
	// INTERNAL (not Blueprint-exposed)
	// ==========================================

	static bool ResolveFrameFromPlayback(
		UPaper2DPlusCharacterProfileAsset* CharacterProfile,
		UPaperFlipbook* Flipbook,
		float PlaybackPosition,
		FFrameHitboxData& OutFrameData);

	static float GetMaxAttackReach(const FFlipbookProfileEntry& FlipbookData);

private:
	static void DrawDebugHitboxes(UObject* WorldContext, const FFrameHitboxData& FrameData, FVector WorldPosition, bool bFlipX, float ScaleX, float ScaleY, float Duration, float Thickness, bool bDrawSockets);
	static void DrawDebugHitbox(UObject* WorldContext, const FHitboxData& Hitbox, FVector WorldPosition, bool bFlipX, float ScaleX, float ScaleY, FLinearColor Color, bool bUseTypeColor, float Duration, float Thickness);
	static FColor GetDebugColorForType(EHitboxType Type);
};
