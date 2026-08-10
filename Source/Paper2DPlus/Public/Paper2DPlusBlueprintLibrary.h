// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Paper2DPlusTypes.h"
#include "Paper2DPlusClashTypes.h"  // EClashOutcome (TASK-77)
#include "Paper2DPlusFrameData.h"
#include "FrameCues/Paper2DPlusFrameCue.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Paper2DPlusCharacterCatalogAsset.h"
#include "Paper2DPlusCharacterProfileComponent.h"
#include "Paper2DPlusCombatProfileAsset.h"
#include "Paper2DPlusCombatProfileComponent.h"
#include "Paper2DPlusEffectProfileAsset.h"
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
// GetRootMotionAtFrame                  | SAFE     | pure asset lookup, no baseline
// GetActorRootMotionDelta               | DOCUMENT | reads advancing baseline; const peek, does NOT consume. See component docstring.
// GetTotalDamage                        | SAFE     | pure math on results array
// GetMaxKnockback                       | SAFE     | pure math on results array
// --- Frame Cue queries (TASK-118 U3 hardening) ---
// GetFrameCuesByClass/ByTag             | SAFE     | ordered reads of stable authored cue data
// GetFrameCuesForFlipbook/AtKeyFrame    | SAFE     | ordered reads of one stable animation entry
// GetFrameCueRangesContainingKeyFrame   | SAFE     | pure authored range containment
// GetActorActiveFrameCueRanges          | SAFE     | ordered read of the component active-range snapshot
// --- CharacterProfileComponent ---
// GetResolvedFlipbookComponent          | SAFE     | reads stable component ref
// GetRootMotionDelta                    | DOCUMENT | reads advancing baseline; const peek, does NOT consume.
// ConsumeRootMotionDelta                | MUTATES  | advances root-motion baseline for manual movement components.
// --- CharacterProfileAsset ---
// Pure accessors                        | SAFE     | all read stable asset data, no advancing state
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
	/**
	 * Returns the project-configured Character Catalog as a typed soft reference. This function never
	 * synchronously loads the Catalog; use Unreal's normal Async Load Asset flow before instance queries.
	 */
	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Character Catalog")
	static TSoftObjectPtr<UPaper2DPlusCharacterCatalogAsset> GetDefaultCharacterCatalog();

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

	// (deprecated GetActor* hitbox/socket aliases removed — use the GetActorWorld* set)

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
	static float GetFrameDamage(AActor* Actor);

	/** Get the max knockback of all attack hitboxes for the actor's current frame */
	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Frame")
	static float GetFrameKnockback(AActor* Actor);

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
	// ROOT MOTION QUERIES
	// ==========================================

	/** Get the root motion offset at a specific frame (pixels). Returns ZeroVector if no data or out of range. */
	static FVector2D GetRootMotionAtFrame(const UPaper2DPlusCharacterProfileAsset* Asset, const FString& FlipbookName, int32 FrameIndex);

	/** Object-ref form of GetRootMotionAtFrame — keys the move off the flipbook reference instead of its name. */
	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Root Motion")
	static FVector2D GetRootMotionAtFrameByFlipbook(const UPaper2DPlusCharacterProfileAsset* Asset, UPaperFlipbook* Flipbook, int32 FrameIndex);

	/**
	 * Get the root motion delta for the actor's current frame transition.
	 * Returns the world-space movement vector based on the change from the previous
	 * frame's root motion position to the current frame's. Requires the actor to
	 * have a UPaper2DPlusCharacterProfileComponent with root motion data authored.
	 * Returns ZeroVector if no component, no data, or no frame change.
	 *
	 * Note: This is a const peek into the component's advancing baseline state.
	 * Repeated calls within the same frame return the same delta. The baseline
	 * advances automatically when bAutoApplyRootMotion is enabled. Manual-drive
	 * callers should use ConsumeActorRootMotionDelta instead.
	 */
	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Root Motion")
	static FVector GetActorRootMotionDelta(AActor* Actor);

	/** Consume the actor's current root motion delta and advance the component baseline. */
	UFUNCTION(BlueprintCallable, Category = "Paper2DPlus|Root Motion")
	static FVector ConsumeActorRootMotionDelta(AActor* Actor);

	/** Query scalable broadphase attack overlaps through the world hitbox subsystem. */
	UFUNCTION(BlueprintCallable, Category = "Paper2DPlus|Collision")
	static bool QueryActorAttackOverlaps(AActor* Attacker, TArray<FHitboxCollisionResult>& OutResults);

	/** TASK-77 U4: query ADVISORY attack-vs-attack clashes through the hitbox subsystem — the attacker's attack
	 *  boxes vs OTHER actors' attack boxes, resolved by the project clash graph. Gate+advise: REPORTS the
	 *  per-clash Outcome; the game acts on it. SEPARATE from QueryActorAttackOverlaps (the damage/hurtbox path
	 *  is untouched). No results when no clash graph is assigned. */
	UFUNCTION(BlueprintCallable, Category = "Paper2DPlus|Clash")
	static bool QueryActorAttackClashes(AActor* Attacker, TArray<FHitboxClashResult>& OutResults);

	/** Hit-priority (TASK-77 U3): resolve an attacker category vs a defender DEFENSE class against the project's
	 *  DefaultClashGraph. BlueprintCallable (NOT Pure — it sync-loads the graph asset). AWins = attacker beats
	 *  the defense, BWins = the defense beats the attacker, Trade/Clash = no rule (the default). */
	UFUNCTION(BlueprintCallable, Category = "Paper2DPlus|Clash")
	static EClashOutcome GetClashOutcome(FGameplayTag AttackerCategory, FGameplayTag DefenderDefenseClass);

	/** Hit-priority (TASK-77 U3): the keep/suppress boolean the broadphase uses — true if the attack connects,
	 *  false if the defender's DEFENSE class beats it. The decision GetTotalDamage/overlap suppression follows.
	 *  An untagged defender always connects. BlueprintCallable (sync-loads the project clash graph). */
	UFUNCTION(BlueprintCallable, Category = "Paper2DPlus|Clash")
	static bool WillAttackConnect(FGameplayTag AttackerCategory, FGameplayTag DefenderDefenseClass);

	// ==========================================
	// UTILITIES
	// ==========================================

	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Utilities")
	static float GetTotalDamage(const TArray<FHitboxCollisionResult>& Results);

	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Utilities")
	static float GetMaxKnockback(const TArray<FHitboxCollisionResult>& Results);

	// ==========================================
	// FRAME DATA (fighting-game frame-data table — TASK-15)
	// ==========================================

	/** Compute the fighting-game frame-data summary for one move/flipbook on an asset (case-insensitive).
	 *  All values are derived from existing data — see FPaper2DPlusMoveFrameData. @return false if the
	 *  asset is null or the name is unknown (OutData left default). */
	static bool GetMoveFrameData(const UPaper2DPlusCharacterProfileAsset* Asset, const FString& FlipbookName, FPaper2DPlusMoveFrameData& OutData);

	/** Compute the frame-data summary for every move/flipbook on an asset, in Flipbooks[] order. */
	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Frame Data")
	static TArray<FPaper2DPlusMoveFrameData> GetAllMoveFrameData(const UPaper2DPlusCharacterProfileAsset* Asset);

	/** Actor convenience overload: resolves the CharacterProfile via the actor's
	 *  UPaper2DPlusCharacterProfileComponent, then computes the move's frame data. */
	static bool GetActorMoveFrameData(AActor* Actor, const FString& FlipbookName, FPaper2DPlusMoveFrameData& OutData);

	/** Object-ref form of GetMoveFrameData — keys the move off the flipbook reference instead of its name. */
	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Frame Data")
	static bool GetMoveFrameDataByFlipbook(const UPaper2DPlusCharacterProfileAsset* Asset, UPaperFlipbook* Flipbook, FPaper2DPlusMoveFrameData& OutData);

	/** Object-ref form of GetActorMoveFrameData — keys the move off the flipbook reference instead of its name. */
	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Frame Data")
	static bool GetActorMoveFrameDataByFlipbook(AActor* Actor, UPaperFlipbook* Flipbook, FPaper2DPlusMoveFrameData& OutData);

	// ==========================================
	// FRAME CUES (authored placement queries; queries never execute behavior)
	// ==========================================

	/**
	 * Resolve a Cue invocation snapshot to a spawn/attachment transform.
	 *
	 * Render Origin returns the live playback component transform. Profile Socket reads only the
	 * exact base/compiled Character Profile row identified by the context's animation plus flipbook
	 * and uses the context frame; it never resamples current playback or falls back to Render Origin.
	 * The returned component is an attachment parent with keep-world semantics, not an Unreal socket
	 * name target. Every failure returns an attributable result and resets both outputs.
	 */
	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Frame Cues")
	static EPaper2DPlusFrameCueAnchorResult ResolveFrameCueAnchor(
		const FPaper2DPlusFrameCueContext& Context,
		EPaper2DPlusFrameCueAnchorKind AnchorKind,
		const FString& ProfileSocketName,
		FTransform& OutWorldTransform,
		UPaperFlipbookComponent*& OutAttachmentComponent);

	/** Return every valid cue placement whose class matches CueClass, walking animations and their cue
	 *  arrays in authored order. Derived classes match by default; bExactClass requires the placement's
	 *  concrete class to equal CueClass. Null asset/class returns an empty array. */
	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Frame Cues")
	static TArray<UPaper2DPlusCueBase*> GetFrameCuesByClass(
		const UPaper2DPlusCharacterProfileAsset* Asset,
		TSubclassOf<UPaper2DPlusCueBase> CueClass,
		bool bExactClass = false);

	/** Return every valid cue placement whose optional tag matches CueTag, in authored order.
	 *  Hierarchical matching is the default (`Cue.Child` matches a `Cue` query); bExactTag compares the
	 *  complete tag. Invalid tags never act as match-all and return an empty array. */
	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Frame Cues", meta = (GameplayTagFilter = "Paper2DPlus.Cue"))
	static TArray<UPaper2DPlusCueBase*> GetFrameCuesByTag(
		const UPaper2DPlusCharacterProfileAsset* Asset,
		FGameplayTag CueTag,
		bool bExactTag = false);

	/** Native name-keyed query for one animation. The lookup is case-insensitive and preserves that
	 *  animation's cue array order. Unknown/empty animation names and null assets return empty. */
	static TArray<UPaper2DPlusCueBase*> GetFrameCuesForAnimation(
		const UPaper2DPlusCharacterProfileAsset* Asset,
		const FString& AnimationName);

	/** Object-keyed Blueprint form of GetFrameCuesForAnimation. A null or foreign flipbook returns empty. */
	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Frame Cues")
	static TArray<UPaper2DPlusCueBase*> GetFrameCuesForFlipbook(
		const UPaper2DPlusCharacterProfileAsset* Asset,
		UPaperFlipbook* Flipbook);

	/** Native query for placements whose primary anchor is exactly KeyFrame. For a Cue State this is
	 *  its start frame; use GetFrameCueRangesContainingKeyFrame for ranges already in progress. */
	static TArray<UPaper2DPlusCueBase*> GetFrameCuesAtKeyFrame(
		const UPaper2DPlusCharacterProfileAsset* Asset,
		const FString& AnimationName,
		int32 KeyFrame);

	/** Object-keyed Blueprint form of GetFrameCuesAtKeyFrame. */
	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Frame Cues")
	static TArray<UPaper2DPlusCueBase*> GetFrameCuesAtKeyFrameByFlipbook(
		const UPaper2DPlusCharacterProfileAsset* Asset,
		UPaperFlipbook* Flipbook,
		int32 KeyFrame);

	/** Native query for Cue States whose authored span contains KeyFrame. Cues are excluded. */
	static TArray<UPaper2DPlusCueBase*> GetFrameCueRangesContainingKeyFrame(
		const UPaper2DPlusCharacterProfileAsset* Asset,
		const FString& AnimationName,
		int32 KeyFrame);

	/** Object-keyed Blueprint form of GetFrameCueRangesContainingKeyFrame. */
	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Frame Cues")
	static TArray<UPaper2DPlusCueBase*> GetFrameCueRangesContainingKeyFrameByFlipbook(
		const UPaper2DPlusCharacterProfileAsset* Asset,
		UPaperFlipbook* Flipbook,
		int32 KeyFrame);

	/** Return the actor component's currently active Cue States in base-then-Layer authored order.
	 *  Invalid actors or actors without a Character Profile Component return empty. */
	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Frame Cues")
	static TArray<UPaper2DPlusCueBase*> GetActorActiveFrameCueRanges(AActor* Actor);

	// ==========================================
	// COMBAT PROFILE (derived attack catalog + utility decisions)
	// ==========================================

	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Combat Profile")
	static bool GetActorCombatDecision(
		AActor* Actor,
		const FPaper2DPlusCombatRuntimeContext& Context,
		FPaper2DPlusCombatDecision& OutDecision,
		FName ScoringProfileName = NAME_None);

	static bool GetActorAttackRangeForMove(AActor* Actor, FName MoveName, FVector2D& OutRangeLocal);

	/** Object-ref form of GetActorAttackRangeForMove — keys the move off the flipbook reference instead of its name. */
	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Combat Profile")
	static bool GetActorAttackRangeForMoveByFlipbook(AActor* Actor, UPaperFlipbook* Flipbook, FVector2D& OutRangeLocal);

	// ==========================================
	// EFFECT PROFILE (character-agnostic tagged flipbook library)
	// ==========================================

	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Effect Profile")
	static bool ValidateEffectProfileAsset(
		const UPaper2DPlusEffectProfileAsset* EffectProfile,
		TArray<FPaper2DPlusEffectProfileValidationIssue>& OutIssues);

	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Effect Profile", meta = (DeprecatedFunction, DeprecationMessage = "Use direct flipbook membership and tag queries. Spawn Effect Cues no longer resolve profile entries by name."))
	static bool ResolveEffectProfileEntry(
		const UPaper2DPlusEffectProfileAsset* EffectProfile,
		FName EffectName,
		FPaper2DPlusEffectSpawnSettings& OutSettings);

	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Effect Profile", meta = (DeprecatedFunction, DeprecationMessage = "Use GetEffectProfileFlipbooksByType or descriptor queries."))
	static void GetEffectProfileEntriesForCategory(
		const UPaper2DPlusEffectProfileAsset* EffectProfile,
		FGameplayTag CategoryTag,
		TArray<FPaper2DPlusEffectProfileEntry>& OutEntries);

	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Effect Profile")
	static void GetEffectProfileFlipbooks(
		const UPaper2DPlusEffectProfileAsset* EffectProfile,
		TArray<UPaperFlipbook*>& OutFlipbooks);

	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Effect Profile")
	static bool EffectProfileContainsFlipbook(
		const UPaper2DPlusEffectProfileAsset* EffectProfile,
		UPaperFlipbook* EffectFlipbook);

	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Effect Profile", meta = (GameplayTagFilter = "Paper2DPlus.Effect.Type"))
	static void GetEffectProfileFlipbooksByType(
		const UPaper2DPlusEffectProfileAsset* EffectProfile,
		FGameplayTag TypeTag,
		bool bExactMatch,
		TArray<UPaperFlipbook*>& OutFlipbooks);

	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Effect Profile", meta = (GameplayTagFilter = "Paper2DPlus.Effect.Descriptor"))
	static void GetEffectProfileFlipbooksByDescriptor(
		const UPaper2DPlusEffectProfileAsset* EffectProfile,
		FGameplayTag DescriptorTag,
		bool bExactMatch,
		TArray<UPaperFlipbook*>& OutFlipbooks);

	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Effect Profile", meta = (GameplayTagFilter = "Paper2DPlus.Effect.Descriptor"))
	static void GetEffectProfileFlipbooksWithAllDescriptors(
		const UPaper2DPlusEffectProfileAsset* EffectProfile,
		const FGameplayTagContainer& DescriptorTags,
		bool bExactMatch,
		TArray<UPaperFlipbook*>& OutFlipbooks);

	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Effect Profile", meta = (GameplayTagFilter = "Paper2DPlus.Effect.Descriptor"))
	static void GetEffectProfileFlipbooksWithAnyDescriptors(
		const UPaper2DPlusEffectProfileAsset* EffectProfile,
		const FGameplayTagContainer& DescriptorTags,
		bool bExactMatch,
		TArray<UPaperFlipbook*>& OutFlipbooks);

	// ==========================================
	// AUXILIARY FRAME CURVES (TASK-74)
	// ==========================================

	/**
	 * Sample a named auxiliary float curve on a move at a (possibly fractional) key-frame index.
	 * Resolves the move entry by name (case-insensitive), finds the curve by name, and evaluates it.
	 * Returns DefaultValue when the asset, move, or curve is absent, or the curve has no keys — never
	 * errors (R4). THE single source of the eval math; GetActorCurveValue funnels through this.
	 * @param Asset        Character profile to query (null -> DefaultValue).
	 * @param MoveName     Flipbook/move name (FFlipbookIdentity::FlipbookName), case-insensitive.
	 * @param CurveName    Curve name (key in FFlipbookCurveData::Curves).
	 * @param Frame        Key-frame index to sample at (exact at integers, interpolated between).
	 * @param DefaultValue Returned when anything is missing.
	 */
	static float GetMoveCurveValueAtFrame(
		const UPaper2DPlusCharacterProfileAsset* Asset,
		FName MoveName,
		FName CurveName,
		int32 Frame,
		float DefaultValue = 0.f);

	/**
	 * Sub-frame variant of GetMoveCurveValueAtFrame: sample at a FRACTIONAL key-frame position (e.g. 3.5) for
	 * callers reading a smoothly-interpolated value at continuous playback time (R3). Same lookup + defaults as the
	 * integer version; GetMoveCurveValueAtFrame forwards to this. THE single source of the eval math.
	 */
	static float GetMoveCurveValueAtFramePosition(
		const UPaper2DPlusCharacterProfileAsset* Asset,
		FName MoveName,
		FName CurveName,
		float FramePosition,
		float DefaultValue = 0.f);

	/** Object-ref form of GetMoveCurveValueAtFrame — keys the move off the flipbook reference instead of its name. */
	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Curves")
	static float GetMoveCurveValueAtFrameByFlipbook(
		const UPaper2DPlusCharacterProfileAsset* Asset,
		UPaperFlipbook* Flipbook,
		FName CurveName,
		int32 Frame,
		float DefaultValue = 0.f);

	/** Object-ref form of GetMoveCurveValueAtFramePosition — keys the move off the flipbook reference instead of its name. */
	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Curves")
	static float GetMoveCurveValueAtFramePositionByFlipbook(
		const UPaper2DPlusCharacterProfileAsset* Asset,
		UPaperFlipbook* Flipbook,
		FName CurveName,
		float FramePosition,
		float DefaultValue = 0.f);

	/**
	 * Sample a named auxiliary curve for an actor's CURRENT move + current key frame. Resolves the
	 * profile via the actor's UPaper2DPlusCharacterProfileComponent and the live flipbook/key-frame the
	 * runtime uses (GetResolvedFlipbookComponent -> GetKeyFrameIndexAtTime(GetPlaybackPosition())), then
	 * delegates to GetMoveCurveValueAtFrame. Null-safe — returns DefaultValue when anything is missing.
	 */
	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Curves")
	static float GetActorCurveValue(AActor* Actor, FName CurveName, float DefaultValue = 0.f);

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

	/** Compatibility break boundary: resolves only this row's internal soft Flipbook identity. */
	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Effect Profile", meta = (NativeBreakFunc))
	static void BreakEffectProfileEntry(
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
		FString& LayerScope);

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
