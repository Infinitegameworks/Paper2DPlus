// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/EngineTypes.h"
#include "GameplayTagContainer.h"
#include "Paper2DPlusTypes.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Paper2DPlusCharacterProfileComponent.generated.h"
class UPaperFlipbook;
class UPaperFlipbookComponent;
class UPaper2DPlusFrameEventBase;

/** Broadcast after a frame event finishes dispatching for the current frame.
 *  Fires once per event fired — BP listeners can react by event-type via the
 *  Event pointer, or by DebugName. CurrentFrame is the key-frame index. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnPaper2DPlusFrameEventFired,
	UPaper2DPlusFrameEventBase*, Event, int32, CurrentFrame, FName, DebugName);

/** Broadcast when a `UPaper2DPlusApplyGameplayTagFrameEvent` begins or ends.
 *  Game code binds to this and routes to its own tag system (GAS loose tags,
 *  custom tag component, etc.). Paper2DPlus does not own a tag destination. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnPaper2DPlusApplyGameplayTagsRequested,
	const FGameplayTagContainer&, Tags, bool, bAdd);

/** Broadcast when a `UPaper2DPlusScreenFlashFrameEvent` fires. Game code binds
 *  to this and renders the flash via HUD widget / post-process / whatever the
 *  project uses. Paper2DPlus does not own screen-rendering concerns. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnPaper2DPlusScreenFlashRequested,
	FLinearColor, Color, float, Duration);

/**
 * Component that provides Paper2DPlus character profile context for an actor.
 * Add to any actor with a PaperFlipbookComponent so that actor-based hitbox
 * functions (CheckAttackCollision, QuickHitCheck, etc.) can auto-resolve context.
 *
 * Set CharacterProfile to your character's data asset. The FlipbookComponent is
 * auto-found at BeginPlay if not explicitly assigned.
 *
 * Enable bAutoApplyRootMotion to have root motion deltas automatically applied
 * to the actor's position each frame based on the authored root motion data.
 */
UCLASS(ClassGroup=(Paper2DPlus), meta=(BlueprintSpawnableComponent, DisplayName="Character Profile Component"))
class PAPER2DPLUS_API UPaper2DPlusCharacterProfileComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPaper2DPlusCharacterProfileComponent();

	/** The character profile asset for this actor. Blueprint writes route through SetCharacterProfile(). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetCharacterProfile, Category = "Paper2DPlus")
	TObjectPtr<UPaper2DPlusCharacterProfileAsset> CharacterProfile;

	/** The flipbook component used for frame resolution. Auto-found at BeginPlay if not set. */
	UPROPERTY(BlueprintReadWrite, Category = "Paper2DPlus")
	TObjectPtr<UPaperFlipbookComponent> FlipbookComponent;

	/** Broadcast once per frame event after it dispatches. Lets BP gameplay code
	 *  react to any frame event by type or DebugName without subclassing. */
	UPROPERTY(BlueprintAssignable, Category = "Paper2DPlus|Frame Events")
	FOnPaper2DPlusFrameEventFired OnFrameEventFired;

	/** Broadcast by `UPaper2DPlusApplyGameplayTagFrameEvent`. Game code binds to
	 *  route into its tag system (GAS loose tags, custom component, etc.). */
	UPROPERTY(BlueprintAssignable, Category = "Paper2DPlus|Frame Events")
	FOnPaper2DPlusApplyGameplayTagsRequested OnApplyGameplayTagsRequested;

	/** Broadcast by `UPaper2DPlusScreenFlashFrameEvent`. Game code binds to
	 *  render the flash via HUD widget / post-process / etc. */
	UPROPERTY(BlueprintAssignable, Category = "Paper2DPlus|Frame Events")
	FOnPaper2DPlusScreenFlashRequested OnScreenFlashRequested;

	/**
	 * When true, root motion deltas are automatically applied to the owning actor's
	 * position each tick. The delta is computed from the change between the previous
	 * and current frame's authored root motion position, scaled by the flipbook
	 * component's world scale and flipped based on facing direction.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paper2DPlus|Root Motion")
	bool bAutoApplyRootMotion = false;

	/** Set the character profile asset at runtime. */
	UFUNCTION(BlueprintCallable, Category = "Paper2DPlus")
	void SetCharacterProfile(UPaper2DPlusCharacterProfileAsset* NewCharacterProfile);

	/** Get the resolved flipbook component (auto-finds if needed). */
	UFUNCTION(BlueprintPure, Category = "Paper2DPlus")
	UPaperFlipbookComponent* GetResolvedFlipbookComponent() const;

	/** Enable or disable auto root motion at runtime. */
	UFUNCTION(BlueprintCallable, Category = "Paper2DPlus|Root Motion")
	void SetAutoApplyRootMotion(bool bEnable);

	/**
	 * Get the root motion delta that would be applied at the current playback frame.
	 * Root motion positions define a trajectory — the delta between consecutive
	 * authored (non-zero) positions is the per-frame movement. Frames with (0,0)
	 * are treated as unset and produce no movement, preventing snap-back.
	 *
	 * This is a const peek: it does NOT advance the internal baseline. Repeated
	 * calls within the same frame return the same delta.
	 */
	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Root Motion")
	FVector GetRootMotionDelta() const;

	/**
	 * Non-const variant of GetRootMotionDelta: returns the current delta AND
	 * advances the internal baseline so the next call (on the same frame)
	 * returns zero. Use this when driving root motion manually from a custom
	 * movement component — paired with bAutoApplyRootMotion=false.
	 */
	UFUNCTION(BlueprintCallable, Category = "Paper2DPlus|Root Motion")
	FVector ConsumeRootMotionDelta();

	/**
	 * Reset root motion tracking state. Call this when teleporting the actor or
	 * switching animations externally to prevent a large delta spike.
	 */
	UFUNCTION(BlueprintCallable, Category = "Paper2DPlus|Root Motion")
	void ResetRootMotionTracking();

	/**
	 * Delegate target for UPaper2DPlusFlipbookComponent::OnFlipbookChanged.
	 * Public so tests (and manual callers driving the slow-poll fallback path)
	 * can invoke the unified frame-0 dispatch without going through BeginPlay.
	 */
	UFUNCTION()
	void HandleFlipbookChanged(UPaperFlipbook* NewFlipbook);

	/**
	 * Delegate target for UPaper2DPlusFlipbookComponent::OnFrameChanged.
	 * Public for the same reason as HandleFlipbookChanged.
	 */
	UFUNCTION()
	void HandleFrameChanged(int32 NewFrame);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	// Root motion tracking state
	int32 PreviousFrameIndex = INDEX_NONE;
	TWeakObjectPtr<UPaperFlipbook> PreviousFlipbook;
	FVector2D LastAppliedRootMotionPos = FVector2D::ZeroVector;

	// Sprite offset tracking — stores the currently applied offset so we can undo it on frame change
	FVector LastAppliedSpriteOffset = FVector::ZeroVector;

	// ─── Shared cache (owned by OnFlipbookChanged) ────────────────────
	// Owner: OnFlipbookChanged() — the only legal writer
	// Warmed on: SetFlipbook delegate fire OR slow-poll tick detecting change
	// Populator: FFlipbookProfileEntry::GetCacheView() (single helper)
	// Invalidated on: new flipbook change (overwrites in place)
	// Readers:
	//   CachedCombatData     → hitbox/socket queries via Paper2DPlusBlueprintLibrary
	//   CachedMotionData     → ApplyRootMotionForFrame, GetRootMotionDelta
	//   CachedFrameEventData → HandleFrameChanged frame event dispatch loop (Phase 2)
	// DO NOT WRITE outside OnFlipbookChanged. Narrow-named resets
	// (ResetRootMotionTracking, etc.) must not touch these fields.
	// See docs/solutions/ue-shared-cache-ownership-patterns.md
	const struct FFlipbookCombatData*      CachedCombatData     = nullptr;
	const struct FFlipbookMotionData*      CachedMotionData     = nullptr;
	const struct FFlipbookFrameEventData*  CachedFrameEventData = nullptr;
	// ──────────────────────────────────────────────────────────────────

	// Derive has-feature flags on read instead of storing separately
	bool HasCachedRootMotion() const { return CachedMotionData && CachedMotionData->HasRootMotion(); }
	bool HasCachedFrameEvents() const { return CachedFrameEventData && CachedFrameEventData->FrameEvents.Num() > 0; }

	// Active ranged event tracking — used by HandleFrameChanged for Begin/End diff.
	// Cleared on flipbook change (HandleFlipbookChanged ends all active before clearing).
	TSet<TObjectPtr<UPaper2DPlusFrameEventBase>> ActiveRangedEvents;

	// Re-entry guard for HandleFrameChanged. A BP event handler that synchronously
	// triggers SetFlipbook would nest dispatch calls; the guard logs and early-returns.
	bool bDispatchingFrameEvents = false;

	// Re-entry guard for HandleFlipbookChanged. A BP OnFrameEventEnd handler
	// that calls SetFlipbook would otherwise corrupt the ActiveRangedEvents
	// iteration (TSet.Empty() while being walked).
	bool bHandlingFlipbookChange = false;

	// True when bound to UPaper2DPlusFlipbookComponent::OnFlipbookChanged delegate.
	// Event-driven path: tick only during active root motion (zero overhead otherwise).
	// Slow-poll fallback: tick at 20Hz to detect flipbook changes.
	bool bEventDrivenFlipbookDetection = false;

	/** Resolve caches when the flipbook changes. */
	void OnFlipbookChanged(UPaperFlipbook* NewFlipbook);

	/** Unified frame-driven dispatch for both root motion and effect spawning. */
	void ApplyRootMotionForFrame(int32 NewFrame, bool bLoopWrap);

	/**
	 * Pure conversion from a pixel-space trajectory delta to a world-space offset.
	 * Shared between ApplyRootMotionForFrame (auto path) and GetRootMotionDelta
	 * (manual BP query) so scale + facing-flip math lives in exactly one place.
	 */
	FVector ComputeRootMotionWorldDelta(const FVector2D& PixelDelta) const;

	/** Apply tick enable/interval based on current root motion state and detection mode. */
	void UpdateTickState();

	/** Resolve facing direction from the flipbook component. */
	bool IsFacingLeft() const;

	/** True when tick is needed (root motion active). */
	bool NeedsTick() const;
};
