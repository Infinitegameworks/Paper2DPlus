// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Containers/Ticker.h"
#include "Engine/EngineTypes.h"
#include "GameplayTagContainer.h"
#include "Paper2DPlusTypes.h"
#include "Paper2DPlusAppearanceTypes.h"
#include "Paper2DPlusNetTypes.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "UObject/ObjectKey.h"
#include "Paper2DPlusCharacterProfileComponent.generated.h"
class UPaperFlipbook;
class UPaperFlipbookComponent;
class UPaper2DPlusCharacterLayerAsset;
class UPaper2DPlusCharacterProfileComponent;
class UPaper2DPlusFlipbookComponent;
class UPaper2DPlusFrameCueStockPlaybackObserver;

UENUM(BlueprintType)
enum class EPaper2DPlusRootMotionApplicationMode : uint8
{
	/** Move the owner with swept actor movement. Blocks on the owner's root collision. */
	SweptActorOffset UMETA(DisplayName = "Swept Actor Offset"),

	/** Move the owner's movement component UpdatedComponent when available; falls back to swept actor movement. */
	MovementComponent UMETA(DisplayName = "Movement Component"),

	/** Legacy behavior: move the owner directly without sweeping. Can clip through blockers. */
	DirectActorOffset UMETA(DisplayName = "Direct Actor Offset (Legacy)")
};

/** Cached runtime snapshot for the actor's current Paper2DPlus frame. */
USTRUCT(BlueprintType)
struct PAPER2DPLUS_API FPaper2DPlusRuntimeFrameState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Paper2DPlus|Runtime")
	TObjectPtr<UPaperFlipbook> Flipbook = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Paper2DPlus|Runtime")
	int32 FrameIndex = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Category = "Paper2DPlus|Runtime")
	FFrameHitboxData LocalFrameData;

	UPROPERTY(BlueprintReadOnly, Category = "Paper2DPlus|Runtime")
	FVector WorldPosition = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Paper2DPlus|Runtime")
	bool bFlipX = false;

	UPROPERTY(BlueprintReadOnly, Category = "Paper2DPlus|Runtime")
	float ScaleX = 1.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Paper2DPlus|Runtime")
	float ScaleY = 1.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Paper2DPlus|Runtime")
	bool bHasAttack = false;

	UPROPERTY(BlueprintReadOnly, Category = "Paper2DPlus|Runtime")
	bool bHasHurtbox = false;

	UPROPERTY(BlueprintReadOnly, Category = "Paper2DPlus|Runtime")
	bool bHasRootMotion = false;

	UPROPERTY(BlueprintReadOnly, Category = "Paper2DPlus|Runtime")
	bool bHasFrameCues = false;

	UPROPERTY(BlueprintReadOnly, Category = "Paper2DPlus|Runtime")
	TArray<FWorldHitbox> WorldHitboxes;

	UPROPERTY(BlueprintReadOnly, Category = "Paper2DPlus|Runtime")
	TArray<FWorldHitbox> WorldAttackBoxes;

	UPROPERTY(BlueprintReadOnly, Category = "Paper2DPlus|Runtime")
	TArray<FWorldHitbox> WorldHurtboxes;

	UPROPERTY(BlueprintReadOnly, Category = "Paper2DPlus|Runtime")
	TArray<FWorldSocket> WorldSockets;
};

/** Payload for the automatic hit-detection broadcasts. One struct serves both sides:
 *  OnHitConnected delivers it on the attacker's component, OnHitReceived on the victim's — the
 *  fields are absolute (attacker/victim), never relative to the receiving side. Everything a
 *  damage-apply receiver needs is here; no follow-up query is required. */
USTRUCT(BlueprintType)
struct PAPER2DPLUS_API FPaper2DPlusAutoHitResult
{
	GENERATED_BODY()

	/** The attacking actor (the side that ran detection). */
	UPROPERTY(BlueprintReadOnly, Category = "Paper2DPlus|Hit Detection")
	TObjectPtr<AActor> Attacker = nullptr;

	/** The attacker's profile component (the broadcaster of OnHitConnected). */
	UPROPERTY(BlueprintReadOnly, Category = "Paper2DPlus|Hit Detection")
	TObjectPtr<UPaper2DPlusCharacterProfileComponent> AttackerComponent = nullptr;

	/** The actor whose hurtbox was hit. */
	UPROPERTY(BlueprintReadOnly, Category = "Paper2DPlus|Hit Detection")
	TObjectPtr<AActor> Victim = nullptr;

	/** The victim's profile component (the broadcaster of OnHitReceived; subsystem victims always
	 *  carry one). */
	UPROPERTY(BlueprintReadOnly, Category = "Paper2DPlus|Hit Detection")
	TObjectPtr<UPaper2DPlusCharacterProfileComponent> VictimComponent = nullptr;

	/** The overlapping attack box in world space (carries its resolved ClashCategory). */
	UPROPERTY(BlueprintReadOnly, Category = "Paper2DPlus|Hit Detection")
	FWorldHitbox AttackBox;

	/** The overlapping hurtbox in world space (carries its DefenseClass). */
	UPROPERTY(BlueprintReadOnly, Category = "Paper2DPlus|Hit Detection")
	FWorldHitbox HurtBox;

	/** World-space center of the overlap region. */
	UPROPERTY(BlueprintReadOnly, Category = "Paper2DPlus|Hit Detection")
	FVector2D HitLocation = FVector2D::ZeroVector;

	/** Damage authored on the matched attack hitbox. */
	UPROPERTY(BlueprintReadOnly, Category = "Paper2DPlus|Hit Detection")
	float Damage = 0.f;

	/** Knockback authored on the matched attack hitbox. */
	UPROPERTY(BlueprintReadOnly, Category = "Paper2DPlus|Hit Detection")
	float Knockback = 0.f;

	/** Authored name of the attacking move. */
	UPROPERTY(BlueprintReadOnly, Category = "Paper2DPlus|Hit Detection")
	FString MoveName;

	/** Attacker key-frame index the hit registered on. */
	UPROPERTY(BlueprintReadOnly, Category = "Paper2DPlus|Hit Detection")
	int32 FrameIndex = INDEX_NONE;

	/** Resolved hit-window index ("HitWindow" aux curve; 0 when the curve is absent). */
	UPROPERTY(BlueprintReadOnly, Category = "Paper2DPlus|Hit Detection")
	int32 HitWindowIndex = 0;
};

/** Listener broadcast for the single Frame Cue funnel; admitted Cue Type behavior runs immediately first. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnPaper2DPlusFrameCue,
	UPaper2DPlusCueBase*, Cue, const FPaper2DPlusFrameCueContext&, Context);

/** Auto-hit broadcast funnel: the plugin detects and dedupes; receivers apply damage —
 *  the Frame Cue ownership split. Fires only where adjudication ran (Standalone/Authority). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPaper2DPlusAutoHit, const FPaper2DPlusAutoHitResult&, Hit);

/** Armed-window edge broadcast: the move name plus the key frame the edge landed on. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnPaper2DPlusAttackWindow, const FString&, MoveName, int32, KeyFrame);

/** Whiff broadcast: the move instance armed at least one window but registered no hits. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPaper2DPlusAttackWhiff, const FString&, MoveName);

/** Native lifecycle seam used by cancellable cue listeners. */
DECLARE_MULTICAST_DELEGATE(FOnPaper2DPlusFrameCueSourceEndedNative);

/** Broadcast when hit-stop begins (TASK-76 PR4). Victim may be null (attacker-only freeze). Game code
 *  binds to layer screen-freeze / SFX / camera shake on top. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnPaper2DPlusHitStopBegin, AActor*, Victim, float, DurationSeconds);

/** Broadcast when hit-stop ends (timer expiry, CancelHitStop, or component teardown). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnPaper2DPlusHitStopEnd);

/** ADVISORY broadcast for every APPLIED replicated anim state (TASK-57 U2 — gate + advise survives
 *  networking: the component never plays flipbooks; the game/PaperZD binds here and performs the
 *  switch). MoveName/Flipbook are null/None for an authoritative CLEAR (server playing a non-profile
 *  flipbook — locomotion is the game's domain) and for the asset-version-skew resolve-failure clear.
 *  PlaybackPosition is the server-anchor-derived position the move should be at RIGHT NOW
 *  (anchor −1, i.e. a paused/zero-rate or clockless publish, reports position 0 with
 *  bAlreadyFinished false). ConfirmedLabel is retained for delegate-signature stability but is now
 *  ALWAYS None (the transition-confirm driver was removed — the animation map is pure data). bAlreadyFinished is true when
 *  a non-looping move's anchor says it already ran out (late join after the move ended). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FiveParams(FOnPaper2DPlusReplicatedAnimStateChanged,
	FName, MoveName, UPaperFlipbook*, Flipbook, float, PlaybackPosition, FName, ConfirmedLabel, bool, bAlreadyFinished);

/**
 * Component that provides Paper2DPlus character profile context for an actor.
 * Add to any actor with a PaperFlipbookComponent so that actor-based hitbox
 * functions (CheckAttackCollision, QuickHitCheck, etc.) can auto-resolve context.
 *
 * Set CharacterProfile to your character's data asset. The FlipbookComponent is
 * auto-found at BeginPlay if not explicitly assigned.
 *
 * Enable bAutoApplyRootMotion to have root motion deltas automatically applied.
 * For full character-controller behavior, leave it off and consume deltas from
 * your movement code with ConsumeRootMotionDelta().
 */
UCLASS(ClassGroup=(Paper2DPlus), meta=(BlueprintSpawnableComponent, DisplayName="Character Profile Component"))
class PAPER2DPLUS_API UPaper2DPlusCharacterProfileComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPaper2DPlusCharacterProfileComponent();

	/** The character profile asset for this actor. Blueprint writes route through SetCharacterProfile().
	 *  Replicated (TASK-57 U2): server-authoritative when bEnableReplication is on — clients receive it
	 *  through OnRep_CharacterProfile (the engine OnRep_SourceFlipbook stash-restore pattern), paired
	 *  with RepProfileSeq in the same bunch (KTD-6) and registered REPNOTIFY_Always. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetCharacterProfile, ReplicatedUsing = OnRep_CharacterProfile, Category = "Paper2DPlus")
	TObjectPtr<UPaper2DPlusCharacterProfileAsset> CharacterProfile = nullptr;

	/** The flipbook component used for frame resolution. Auto-found at BeginPlay if not set.
	 *  Blueprint writes route through SetFrameCuePlaybackSource so lifecycle bindings change
	 *  atomically; C++ callers should use the same setter after BeginPlay. */
	UPROPERTY(BlueprintReadWrite, BlueprintSetter = SetFrameCuePlaybackSource, Category = "Paper2DPlus")
	TObjectPtr<UPaperFlipbookComponent> FlipbookComponent = nullptr;

	/** Broadcast for every admitted Trigger, Begin, Update, and End cue phase. */
	UPROPERTY(BlueprintAssignable, Category = "Paper2DPlus|Frame Cues")
	FOnPaper2DPlusFrameCue OnFrameCue;

	/** Native-only: fires once when this component can no longer produce cue notifications. */
	FOnPaper2DPlusFrameCueSourceEndedNative OnFrameCueSourceEndedNative;

	/** Broadcast once when a hit-stop freeze begins (after the dilations are applied). Victim is
	 *  passed as supplied — it may be null or invalid (an invalid victim is not frozen). Re-triggers
	 *  while active EXTEND the freeze without re-broadcasting (TASK-76 PR4). */
	UPROPERTY(BlueprintAssignable, Category = "Paper2DPlus|Hit Stop")
	FOnPaper2DPlusHitStopBegin OnHitStopBegin;

	/** Broadcast once when the hit-stop freeze ends — timer expiry, CancelHitStop, or EndPlay teardown
	 *  — after every frozen actor's prior dilation is restored (TASK-76 PR4). */
	UPROPERTY(BlueprintAssignable, Category = "Paper2DPlus|Hit Stop")
	FOnPaper2DPlusHitStopEnd OnHitStopEnd;

	/**
	 * When true, root motion deltas are automatically applied to the owning actor's
	 * position each tick. The delta is computed from the change between the previous
	 * and current frame's authored root motion position, scaled by the flipbook
	 * component's world scale and flipped based on facing direction.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paper2DPlus|Root Motion")
	bool bAutoApplyRootMotion = false;

	/**
	 * How auto root motion applies movement when bAutoApplyRootMotion is enabled.
	 * Use ConsumeRootMotionDelta with bAutoApplyRootMotion=false for custom
	 * character controllers that need floor checks, ledge logic, or stateful
	 * movement-mode handling.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paper2DPlus|Root Motion", meta = (EditCondition = "bAutoApplyRootMotion"))
	EPaper2DPlusRootMotionApplicationMode RootMotionApplicationMode = EPaper2DPlusRootMotionApplicationMode::SweptActorOffset;

	// ─── Sprite placement from the profile (TASK-157) ──────────────────

	/** Opt into applying the Character Profile's authored Relative Transform to the resolved flipbook
	 *  component, making the PROFILE the runtime authority for sprite placement instead of a value the
	 *  game transcribes out of it by hand. Default FALSE on purpose: every project that already reads
	 *  GetRelativeTransform() in its own spawn code would otherwise place the sprite twice, and a silent
	 *  doubled offset is expensive to trace. The apply is an ABSOLUTE assignment of the profile's
	 *  transform — not a retained delta — so it is idempotent if it runs again and cannot stack on top of
	 *  a game-side apply of the same values. Turning this off and re-running the funnel restores the
	 *  component's own authored transform, captured before the first apply.
	 *
	 *  Runs through ApplyProfileRelativeTransform() at BeginPlay AND on late profile / flipbook
	 *  assignment, always AFTER the frame-zero dispatch for that assignment, so a profile handed over
	 *  after spawn is placed exactly like one set on the archetype.
	 *
	 *  AUTHORITY CAVEAT: when the flipbook component IS the actor root, changing its transform moves the
	 *  whole actor and can fight replicated movement. Prefer a flipbook component parented under the
	 *  root for networked actors. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paper2DPlus|Placement")
	bool bApplyProfileRelativeTransform = false;

	/** Register this component with the world hitbox subsystem for scalable broadphase queries. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paper2DPlus|Hitboxes")
	bool bRegisterWithHitboxSubsystem = true;

	// ─── Automatic hit detection (TASK-145) ─────────────────────────────

	/** Opt into automatic, event-driven hit detection. The authored data decides WHEN: entering a key
	 *  frame that carries attack boxes ARMS detection (OnAttackWindowBegin), the component queries the
	 *  hitbox subsystem at every armed frame entry AND between key-frame transitions via the
	 *  subsystem's armed-set tick (a held active frame still connects when actors move into overlap
	 *  mid-hold), hits dedup once per (victim, move instance, "HitWindow"-curve window) through the
	 *  SAME ProcessedHits ledger as ValidateAndRegisterHit, and results broadcast OnHitConnected
	 *  (this side) + OnHitReceived (the victim's component). The plugin never applies damage —
	 *  receivers do (the Frame Cue ownership split). Authority-gated: detection and every auto-hit
	 *  broadcast run on Standalone/Authority only; proxies never arm (cosmetics ride Frame Cues —
	 *  see authority-contract.md). Victims must register with the hitbox subsystem
	 *  (bRegisterWithHitboxSubsystem); works best with UPaper2DPlusFlipbookComponent (the stock
	 *  slow-poll fallback arms up to 50 ms late). A non-looping move PARKED on an attack frame keeps
	 *  its window open until the animation changes — the window follows the frames by design. Loop
	 *  wraps do NOT reopen dedup windows (author a "HitWindow" curve, or call ResetHitDedupWindow
	 *  from OnAttackWindowEnd, for per-loop re-hits). Default off — the pull APIs are unaffected. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paper2DPlus|Hit Detection")
	bool bAutoHitDetection = false;

	/** ATTACKER-side auto-detection broadcast: fires once per victim per hit window while
	 *  armed. Commit-before-broadcast — the dedup entry is registered before receivers run. */
	UPROPERTY(BlueprintAssignable, Category = "Paper2DPlus|Hit Detection")
	FOnPaper2DPlusAutoHit OnHitConnected;

	/** VICTIM-side auto-detection broadcast: fires on THIS component whenever an
	 *  attacker's auto detection registers a hit on this actor — same payload as OnHitConnected. */
	UPROPERTY(BlueprintAssignable, Category = "Paper2DPlus|Hit Detection")
	FOnPaper2DPlusAutoHit OnHitReceived;

	/** Armed-window opened: the entered key frame carries attack boxes (auto detection only). */
	UPROPERTY(BlueprintAssignable, Category = "Paper2DPlus|Hit Detection")
	FOnPaper2DPlusAttackWindow OnAttackWindowBegin;

	/** Armed-window closed: entered a frame without attack boxes, the move ended, or teardown. */
	UPROPERTY(BlueprintAssignable, Category = "Paper2DPlus|Hit Detection")
	FOnPaper2DPlusAttackWindow OnAttackWindowEnd;

	/** The move instance ended having armed at least one window but registering zero hits
	 *  (auto or manual) — the whiff-punish/recovery-branch signal. Suppressed on EndPlay teardown. */
	UPROPERTY(BlueprintAssignable, Category = "Paper2DPlus|Hit Detection")
	FOnPaper2DPlusAttackWhiff OnAttackWhiffed;

	// ─── Networking opt-in (TASK-57 U2) ────────────────────────────────

	/** Opt into Paper2DPlus server-authoritative replication for this component (TASK-57). Default
	 *  false — single-player behavior is byte-identical with this off. MUST match between the server
	 *  and client archetypes: a mismatch is diagnosed with a one-time warning when replicated data
	 *  arrives while this is false locally. CONSUMED AT BEGINPLAY (SetIsReplicated + the configuration
	 *  warning battery run there) — flipping it at runtime after BeginPlay is inert and logs a
	 *  one-time warning the next time the net context is consulted. */
	UPROPERTY(EditAnywhere, Category = "Networking")
	bool bEnableReplication = false;

	/** When enabled, simulated proxies will additionally PLAY the replicated anim state themselves —
	 *  the documented second gate+advise exception (hit-stop precedent; TASK-57 U8). On every applied
	 *  NEW-sequence profile move the proxy sets PlayRate/looping, switches the flipbook through the
	 *  EXISTING HandleFlipbookChanged funnel (full catch-up warm: one-shots suppressed, ranged events
	 *  rebuilt by ContainsFrame with bIsCatchUp=true), and seeks to the server-anchor-derived position —
	 *  all committed and applied BEFORE the advisory broadcast. Scope limits (deliberate): FACING is
	 *  never applied (the advisory carries no bFlipX — flipping the sprite is the game's domain);
	 *  authoritative CLEARs (locomotion) are never applied (the game returns to its own locomotion);
	 *  reversed publishes carry no anchor, so a reversed move applies as ReverseFromEnd() (approximate —
	 *  the forward anchor model does not support reverse; reversed moves should not carry catch-up-grade
	 *  ranged events, whose catch-up warm targets frame 0). A locally-driven drift corrector re-snaps the
	 *  playback position on each frame change when it drifts beyond NetPlaybackSnapToleranceSeconds
	 *  (KTD-20), suppressed while a local hit-stop freeze is active. MUTUALLY EXCLUSIVE with a game that
	 *  also plays flipbooks from OnReplicatedAnimStateChanged on simulated proxies — pick one (apply mode
	 *  = the component plays; advise-only = the game plays). */
	UPROPERTY(EditAnywhere, Category = "Networking")
	bool bApplyReplicatedAnimStateOnSimulatedProxies = false;

	/** Whether this component's hit-stop freezes publish to other machines as cosmetic, self-expiring
	 *  replicated state. DECLARED in this version so archetypes can author it; the replicated hit-stop
	 *  behavior lands in a later PR (TASK-57 U5). */
	UPROPERTY(EditAnywhere, Category = "Networking")
	bool bReplicateHitStop = true;

	/** Apply-mode playback drift tolerance in playback SECONDS: a simulated proxy playing the
	 *  replicated state snaps to the server-formula position when its local position drifts beyond
	 *  this (TASK-57 U8/KTD-20). Also the authority self-heal threshold — when the authority's actual
	 *  playback diverges from its own published anchor by more than this (a game-side mid-move playback
	 *  mutation made without calling RepublishAnimState), the authority auto-re-anchors. */
	UPROPERTY(EditAnywhere, Category = "Networking", meta = (ClampMin = "0.0"))
	float NetPlaybackSnapToleranceSeconds = 0.1f;

	/** ADVISORY (gate+advise): broadcast on every applied replicated anim state — moves, authoritative
	 *  clears, and outcome-carrying re-publishes with a NEW sequence. The game/PaperZD binds here and
	 *  performs the actual flipbook switch; the component never plays it (TASK-57 U2). */
	UPROPERTY(BlueprintAssignable, Category = "Networking")
	FOnPaper2DPlusReplicatedAnimStateChanged OnReplicatedAnimStateChanged;

	// ────────────────────────────────────────────────────────────────────

	/** Set the character profile asset at runtime. */
	UFUNCTION(BlueprintCallable, Category = "Paper2DPlus")
	void SetCharacterProfile(UPaper2DPlusCharacterProfileAsset* NewCharacterProfile);

	/**
	 * Atomically replace the Paper component that drives Frame Cue evaluation.
	 * The outgoing generation ends as SourceRemoved, old delegates are unbound, and the new source
	 * is warmed before it can dispatch. This never starts or stops the component itself.
	 */
	UFUNCTION(BlueprintCallable, Category = "Paper2DPlus|Frame Cues")
	void SetFrameCuePlaybackSource(UPaperFlipbookComponent* NewPlaybackSource);

	/**
	 * Begin a playback generation owned by an external timeline such as PaperZD.
	 *
	 * External timelines may keep the render component stopped and drive SetPlaybackPosition
	 * manually, so IsPlaying cannot identify their sessions. Call this once per session (including
	 * same-flipbook restarts), retain the returned positive generation, drive the desired frames,
	 * then report Completed or PlaybackStopped with that source/generation pair. Returns 0 when the
	 * expected source is not the currently bound source or carries no flipbook.
	 */
	UFUNCTION(BlueprintCallable, Category = "Paper2DPlus|Frame Cues")
	int64 BeginExternalFrameCuePlayback(UPaperFlipbookComponent* ExpectedSource);

	/** Current positive Frame Cue playback generation, or 0 before a source is warmed. */
	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Frame Cues")
	int64 GetFrameCuePlaybackGeneration() const { return FrameCuePlaybackGeneration; }

	/**
	 * Idempotently close the expected playback generation after an explicit/manual stop.
	 * This finalizes Cue lifecycle only; it never calls Stop() on the external playback system.
	 */
	UFUNCTION(BlueprintCallable, Category = "Paper2DPlus|Frame Cues")
	bool ReportFrameCuePlaybackStopped(
		UPaperFlipbookComponent* ExpectedSource,
		int64 ExpectedGeneration);

	/**
	 * Idempotently close an externally owned generation after natural completion.
	 * Drive/evaluate the definitive final frame before reporting so final-frame work precedes End.
	 */
	UFUNCTION(BlueprintCallable, Category = "Paper2DPlus|Frame Cues")
	bool ReportFrameCuePlaybackCompleted(
		UPaperFlipbookComponent* ExpectedSource,
		int64 ExpectedGeneration);

	/** Get the resolved flipbook component (auto-finds if needed). */
	UFUNCTION(BlueprintPure, Category = "Paper2DPlus")
	UPaperFlipbookComponent* GetResolvedFlipbookComponent() const;

	/** THE shared facing rule: true when the character faces left, by flipbook YAW (Yaw in (90,270)) OR
	 *  negative X scale — identical to hitbox/root-motion/replicated-anim-state resolution. Public so the
	 *  Frame Cue receivers and game code resolve facing the same way (audit F1). */
	UFUNCTION(BlueprintPure, Category = "Paper2DPlus")
	bool IsFacingLeft() const;

	/** Pure, static facing rule — the ONE source `IsFacingLeft()` and the cached-runtime-frame-state
	 *  path both call, so they can never drift. `|Yaw|` in (90,270) OR negative X scale = facing left.
	 *  Worldless-testable seam for the F1 yaw-flip coverage. */
	static bool ResolveFacingLeft(const FVector& ComponentScale, float YawDegrees);

	/** Enable or disable the profile-driven sprite placement at runtime, re-running the apply funnel so
	 *  the change takes effect immediately. Turning it OFF restores the flipbook component's own authored
	 *  relative transform rather than leaving the profile's values behind. */
	UFUNCTION(BlueprintCallable, Category = "Paper2DPlus|Placement")
	void SetApplyProfileRelativeTransform(bool bEnable);

	/** THE single entry point for profile-driven sprite placement — called at BeginPlay and on late
	 *  profile / flipbook assignment, and safe to call directly after changing the profile's transform
	 *  from editor tooling. Assigns the profile's Relative Transform absolutely (idempotent), or restores
	 *  the captured authored transform when the option is off. Re-seeds the per-frame sprite-offset
	 *  record around the assignment, because that record is stored in world units computed against the
	 *  component's scale — changing scale without re-seeding leaves the sprite displaced until the next
	 *  frame change, which never comes on a single-key-frame idle. No-op with no resolvable flipbook
	 *  component. */
	UFUNCTION(BlueprintCallable, Category = "Paper2DPlus|Placement")
	void ApplyProfileRelativeTransform();

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

	/** Return the cached current-frame snapshot. World-space arrays refresh lazily if the actor moved. */
	UFUNCTION(BlueprintCallable, Category = "Paper2DPlus|Runtime")
	bool GetCurrentRuntimeFrameState(FPaper2DPlusRuntimeFrameState& OutState) const;

	/** Lightweight context used by optimized C++ callers and the Blueprint library hot path. */
	bool TryGetCachedHitboxContext(FFrameHitboxData& OutFrameData, FVector& OutWorldPosition, bool& bOutFlipX, float& OutScaleX, float& OutScaleY) const;

	bool GetCachedWorldHitboxes(TArray<FWorldHitbox>& OutHitboxes) const;
	bool GetCachedWorldAttackBoxes(TArray<FWorldHitbox>& OutHitboxes) const;
	bool GetCachedWorldHurtboxes(TArray<FWorldHitbox>& OutHitboxes) const;
	bool GetCachedWorldSockets(TArray<FWorldSocket>& OutSockets) const;
	bool GetCachedWorldSocketByName(const FString& SocketName, FVector& OutLocation) const;

	/**
	 * Committed Layer-gameplay compose push seam. The render component pushes its normalized appearance
	 * descriptor here after a commit; previews never reach this seam. Clients follow the same OnRep commit
	 * path, so authority and presentation compose identical Layer-owned gameplay.
	 *
	 * Caches component-owned COPIES of the digest, recomposes NOW for the current animation into the
	 * owned ComposedCombatFrames buffer (Paper2DPlusLayerCombat::ComposeCombatFrames — committed state
	 * only), marks the lazy world tier dirty, and re-copies the current frame's local state so queries
	 * this frame already see the new equip (AE3). OnFlipbookChanged reuses the STORED digest to recompose
	 * for each new animation — no additional push is needed on flipbook change.
	 *
	 * Re-entrancy (guard family): called from inside Frame Cue dispatch / a flipbook-change teardown
	 * (an equip from a Cue receiver), the digest is cached immediately but the recompose defers
	 * to the end of dispatch (FlushPendingAnimationDispatch) so the in-flight dispatch state is never
	 * mutated under a live iteration.
	 *
	 * An empty digest (no overrides anywhere) composes to nothing and flips the readers back to the raw
	 * CachedCombatData path — actors with no layer sibling never reach here and never allocate.
	 */
	/** The descriptor is copied as the sole committed selection digest. */
	void NotifyAppearanceCombatDirty(
		const FPaper2DPlusAppearanceDescriptor& CommittedAppearance,
		const UPaper2DPlusCharacterLayerAsset* LayerAsset);

	/**
	 * Teardown clear for the composed Layer gameplay tier. Drops the
	 * committed descriptor digest (default descriptor + null asset), recomposes-to-empty, and runs the stale-ranged-event
	 * sweep immediately, so removed Layers' in-flight Cue States end while their owning
	 * layer asset is STILL ALIVE — the caller's contract: the layer render component calls this from its
	 * EndPlay / OnComponentDestroyed
	 * while it still hard-references the asset. Without this, destroying/reassigning the layer component
	 * leaves bComposedCombatActive serving stale boxes and leaves Layer-owned Cue States active
	 * after their objects die with the GC'd layer asset (a later force-end = use-after-free).
	 *
	 * Respects the dispatch re-entrancy deferral exactly like NotifyAppearanceCombatDirty: the digest
	 * clears immediately (plain data writes), the recompose + sweep defer to end-of-dispatch through the
	 * SAME bPendingAppearanceCombatDirty mechanism. No-op for components that never received a push, so
	 * non-layered actors stay byte-identical (the composed buffer never allocates).
	 */
	void ClearAppearanceCombatDigest();

	/** Name of the current move — the profile entry matching the live flipbook (authored FlipbookName),
	 *  falling back to the flipbook's object name when the profile has no entry for it; empty when no
	 *  profile/flipbook is resolved. */
	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Transitions")
	FString GetCurrentMoveName() const;

	/** Object-ref form of GetCurrentMoveName: the live flipbook currently playing on the resolved flipbook
	 *  component (the move-as-object). Null when no profile/flipbook is resolved. */
	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Transitions")
	UPaperFlipbook* GetCurrentMoveFlipbook() const;

	/** Currently admitted Cue States in deterministic authored order: base animation first, followed by
	 *  composed Layer cues. A valid stale member briefly awaiting forced End is appended by stable
	 *  object path; invalid objects are omitted. A component with no active ranges returns empty. */
	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Frame Cues")
	TArray<UPaper2DPlusCueBase*> GetActiveFrameCueRanges() const;

	/** The hit-stop freeze sentinel written to CustomTimeDilation: small non-zero so components that
	 *  divide by their own scaled dt can't NaN. The final restore applies only while an actor still
	 *  carries this exact value (external mid-freeze writes are respected) — TASK-76 PR4. */
	static constexpr float HitStopFrozenDilation = 0.0001f;

	/** Direct hit-stop mechanism (games call it explicitly for custom impacts).
	 *  Freezes the owner and OtherActor (optional) by setting CustomTimeDilation to the
	 *  HitStopFrozenDilation sentinel (~0), restoring each actor's PRIOR dilation after DurationSeconds of
	 *  REAL (undilated) time via a core ticker — global/world time-dilation cannot deadlock the unfreeze.
	 *  THE deliberate exception to gate+advise. Re-triggering while active extends the freeze (multi-hit)
	 *  and adds new actors without re-capturing frozen dilations; overlapping hit-stops from OTHER
	 *  components are refcounted through a shared registry — an actor unfreezes when the LAST overlapping
	 *  hit-stop ends (fighting-game trades / gang hits restore correctly). Calling this from inside an
	 *  OnHitStopBegin/End handler is rejected (logged). Runs on REAL time via the core ticker: a game
	 *  PAUSE does not pause hit-stop — the freeze can expire while paused. External CustomTimeDilation
	 *  writes DURING the freeze are respected — the final restore only applies while the actor still
	 *  carries the freeze sentinel.
	 *  SCOPE — what CustomTimeDilation freezes (and what it does NOT): actor tick, component ticks,
	 *  flipbook playback, CharacterMovement, and timeline components (they ride actor tick) all freeze.
	 *  World-TimerManager timers (SetTimer/delays), latent actions, and anything else not driven by
	 *  this actor's tick are EXPLICITLY EXCLUDED — they keep advancing on world time during hit-stop
	 *  (the engine has no pause-all-timers-for-actor API, and the plugin cannot know the game's timer
	 *  handles). Games that drive gameplay via timers should pause/resume their own handles from
	 *  OnHitStopBegin/OnHitStopEnd, or check IsHitStopActive() inside timer callbacks. */
	UFUNCTION(BlueprintCallable, Category = "Paper2DPlus|Hit Stop")
	void TriggerHitStop(AActor* OtherActor, float DurationSeconds);

	/** True while a hit-stop freeze is active. */
	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Hit Stop")
	bool IsHitStopActive() const;

	/** End an active hit-stop early, restoring all frozen actors' prior dilations (broadcasts OnHitStopEnd).
	 *  Safe no-op when inactive. Called automatically on EndPlay so a dying attacker never leaves the
	 *  victim frozen. Safe from inside an OnHitStopBegin handler: the freeze is undone immediately, but
	 *  the OnHitStopEnd broadcast is suppressed (logged). */
	UFUNCTION(BlueprintCallable, Category = "Paper2DPlus|Hit Stop")
	void CancelHitStop();

	// ─── Server-owned combat (TASK-57 U4/R3/KTD-17) ─────────────────────
	// Server-authoritative hit adjudication. The plugin NEVER applies damage — the game applies damage
	// and calls these ON AUTHORITY; these validate + dedupe + report. Client-side
	// hitbox queries (UPaper2DPlusBlueprintLibrary) remain usable for COSMETICS only.

	/** Server-authoritative hit validation + dedup (KTD-17). On a non-authority NETWORKED context this
	 *  warns once and returns false WITHOUT mutating anything (server-authoritative — clients adjudicate
	 *  cosmetically only). Otherwise it RE-QUERIES the hitbox subsystem against the SERVER's own current
	 *  frame state and:
	 *   - bValidHit is true ONLY when a returned overlap result resolves to the PASSED Victim's actor
	 *     (the victim-membership contract). Out.Damage / Out.Knockback / Out.Victim / Out.MoveName /
	 *     Out.FrameIndex are populated FROM the matched overlap (the SERVER's authored data), NEVER from
	 *     caller input. (The U1 FPaper2DPlusHitValidationResult struct carries no HitLocation field; the
	 *     game reads the matched overlap directly if it needs the impact point.)
	 *   - When bRequireActiveAttackFrame is true, the move must be on an ATTACK key frame within the last
	 *     per-tick span (LastFrameSpanBegin, NewFrame] — a 1-frame active window crossed entirely inside
	 *     one multi-frame server tick still validates (hitbox geometry is sampled at the latest in-span
	 *     attack frame). Key-frame indices only (GetKeyFrameIndexAtTime, never GetPlaybackPositionInFrames).
	 *   - HitWindowIndex defaults to -1 = AUTO (resolve the "HitWindow" aux curve span-aware, like
	 *     GetCurrentHitWindowIndex). Dedup key = (this, Victim, MoveInstanceCounter, ResolvedWindow):
	 *     a duplicate sets Out.bDuplicate, returns false, and applies no second hit.
	 *     CAUTION: an EXPLICIT HitWindowIndex (>=0) MUST be SERVER-DERIVED. It is part of the dedup key,
	 *     so a client-echoed window integer could open a SECOND dedup slot inside the same real window
	 *     (a double-hit). It is intentionally NOT clamped — an explicit window is a legitimate
	 *     server-side decision for curve-less multi-hit moves — but games should prefer the -1 AUTO path
	 *     and NEVER forward a client-observed GetCurrentHitWindowIndex() value into this authoritative
	 *     validator.
	 *  Works in Standalone too (single-player hit dedup is a real feature — the move-instance counter
	 *  increments unconditionally). The real UWorldSubsystem path is exercised by U7's Tier-2/PIE; the
	 *  worldless Tier-1 suite injects overlaps through the test seam. */
	UFUNCTION(BlueprintCallable, Category = "Networking")
	bool ValidateAndRegisterHit(AActor* Victim, FPaper2DPlusHitValidationResult& Out, int32 HitWindowIndex = -1, bool bRequireActiveAttackFrame = true);

	/** The LIGHT dedup-only path (KTD-17): same non-authority gate as ValidateAndRegisterHit, but NO
	 *  overlap re-query — it trusts the caller's own hit adjudication and only registers the
	 *  (this, Victim, MoveInstanceCounter, ResolvedWindow) dedup tuple. Returns true the first time the
	 *  tuple is registered, false on a duplicate (no re-damage). HitWindowIndex -1 = AUTO (span-aware
	 *  "HitWindow" curve). An EXPLICIT HitWindowIndex (>=0) MUST be SERVER-DERIVED — it is part of the
	 *  dedup key, so a client-echoed window integer could open a second dedup slot in the same real window
	 *  (a double-hit). Prefer the -1 AUTO path; never forward a client-observed GetCurrentHitWindowIndex()
	 *  value here. Works in Standalone. */
	UFUNCTION(BlueprintCallable, Category = "Networking")
	bool RegisterHitOnce(AActor* Victim, int32 HitWindowIndex = -1);

	/** The current hit-window index for multi-hit moves: the span-aware floor of the move's "HitWindow"
	 *  aux float curve (TASK-74). Resolved against the last per-tick span (LastFrameSpanBegin, NewFrame]
	 *  — the LATEST in-span attack key frame's value, matching ValidateAndRegisterHit's sampler so the
	 *  pick-window-then-validate pattern can't straddle a tick boundary. 0 when the curve is absent
	 *  (single un-indexed window) or there is no current move. Key-frame indices only.
	 *  This is a BlueprintPure LOCAL query: it reads the caller machine's OWN span, so on a proxy it is
	 *  COSMETIC/advisory (cosmetic-safe — it mutates nothing). NEVER feed a proxy's reading of this back
	 *  into the authoritative ValidateAndRegisterHit/RegisterHitOnce as an explicit window — those derive
	 *  the window server-side from the SAME sampler. */
	UFUNCTION(BlueprintPure, Category = "Networking")
	int32 GetCurrentHitWindowIndex() const;

	/** Manually reopen the hit-dedup window (KTD-17): clears ProcessedHits so the current move can hit
	 *  the same victims again. The multi-hit seam for games that drive their own re-hit cadence (the
	 *  "HitWindow" curve handles authored multi-hit windows automatically). Works in Standalone. */
	UFUNCTION(BlueprintCallable, Category = "Networking")
	void ResetHitDedupWindow();

	/** Subsystem-driven auto-detection re-check between key-frame transitions: the hitbox
	 *  subsystem calls this each tick for every ARMED component so movement during a held active
	 *  frame still connects. Safe external entry — mirrors HandleFrameChanged's re-entry protocol
	 *  (a receiver switching flipbooks mid-broadcast queues through the same pending machinery);
	 *  no-op while a frame dispatch is already live on this stack or when disarmed. */
	void TickAutoHitDetection();

	/** This component's current network context — THE single authority seam every Paper2DPlus
	 *  gating decision reads (TASK-57 U1/U2). Standalone = not networked (replication off / no world /
	 *  NM_Standalone): every gate passes, so single-player behavior is identity. With the
	 *  bEnableReplication opt-in on a networked world, the owner's role maps to
	 *  Authority / AutonomousProxy / SimulatedProxy (U2). */
	UFUNCTION(BlueprintPure, Category = "Networking")
	EPaper2DPlusNetContext GetNetContext() const;

	/** Re-broadcast OnReplicatedAnimStateChanged from the last APPLIED replicated state — the
	 *  BIND-THEN-PULL pattern for late binders (TASK-57 U2): initial-bunch RepNotifies land before
	 *  the actor's BP Event BeginPlay can bind, so bind first, then call this to pull the advisory
	 *  you missed. No-op until a state has applied (LastAppliedSequence == 0) or while a newer state
	 *  sits stashed/unapplied (its own drain broadcasts shortly). Routes through the same single
	 *  advisory broadcast site under the sixth re-entry guard. */
	UFUNCTION(BlueprintCallable, Category = "Networking")
	void RebroadcastReplicatedAnimState();

	/** Re-anchor the published anim state to the CURRENT actual playback position + params, keeping the
	 *  SAME Sequence (no new move instance — the counter is not bumped, so the producer invariant holds).
	 *  Authority-only (warn-once on a non-authority call). Call after ANY game-side mid-move playback
	 *  mutation the plugin cannot observe — SetPlayRate, SetPlaybackPosition, pause/resume, per-actor
	 *  CustomTimeDilation — so the server-time anchor stays truthful and apply-mode proxies / advisory
	 *  consumers re-derive the right position (TASK-57 U8/KTD-19). A cheap authority stale-detect on each
	 *  key-frame change re-anchors automatically when the divergence exceeds NetPlaybackSnapToleranceSeconds,
	 *  so this is the explicit escape hatch for immediate re-anchoring (e.g. on the same frame as the mutation). */
	UFUNCTION(BlueprintCallable, Category = "Networking")
	void RepublishAnimState();

	/** Replication registration (TASK-57 U2) — UNCONDITIONAL (registration is not replication: the
	 *  component only actually replicates when bEnableReplication opted in via SetIsReplicated at
	 *  BeginPlay). CharacterProfile is REPNOTIFY_Always and pairs with RepProfileSeq in the same
	 *  bunch (KTD-6). */
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Net-internal RepNotify for CharacterProfile (TASK-57 U2/KTD-7): the engine OnRep_SourceFlipbook
	 *  stash-restore pattern — the net driver pre-writes the property, so the body restores the old
	 *  value and routes the new one through the EXISTING SetCharacterProfile funnel (under the
	 *  bApplyingReplicatedState guard so frame-0 one-shots are catch-up-suppressed and the proxy gate
	 *  is bypassed), then adopts the profile generation from the PAIRED RepProfileSeq (never from the
	 *  anim struct — spawn-order deadlock) and drains any stashed anim state. PUBLIC so the worldless
	 *  automation rigs can drive it (the repo's public-UFUNCTION testability pattern). */
	UFUNCTION()
	void OnRep_CharacterProfile(UPaper2DPlusCharacterProfileAsset* OldProfile);

	/** Net-internal RepNotify for RepAnimState (TASK-57 U2): the decision-tree apply — stash/defer
	 *  (BeginPlay pending, no server clock, generation mismatch, no profile), same-sequence
	 *  outcome-only handling, authoritative clears, resolve-failure clears (never a wedged stash),
	 *  and commit-EVERYTHING-then-broadcast for new sequences. PUBLIC for the worldless rigs. */
	UFUNCTION()
	void OnRep_AnimState(const FPaper2DPlusRepAnimState& OldState);

	/** Net-internal RepNotify for RepHitStop (TASK-57 U5): the self-expiring snapshot receiver. Computes
	 *  Remaining = clamp(Duration - (ServerNow - StartServerTime), 0, Duration) UNCONDITIONALLY (KTD-21:
	 *  a seamless-travel/garbage clock can never freeze for an arbitrary span); Duration==0 or Remaining<=0
	 *  ends any local freeze (a stale/late-join-expired snapshot is ignored if no freeze is active); else
	 *  BeginHitStopLocal(Remaining, Victim) reuses the EXISTING refcounted registry + real-time ticker. No
	 *  server clock yet (GameState unreplicated) => stash + retry one frame. Null Victim tolerated
	 *  (attacker-only freeze, no warn — KTD-24). Gated by bReplicateHitStop. OnReps never run on the
	 *  authority, so no listen-server double-freeze guard is needed. PUBLIC for the worldless rigs. */
	UFUNCTION()
	void OnRep_HitStop();

#if !UE_BUILD_SHIPPING
	/** TEST SEAM (TASK-57 U1): force ResolveNetContext to return a fixed context so the worldless
	 *  automation rigs can exercise the full authority matrix. Unset = normal resolution. Plain C++
	 *  method, deliberately NOT a UFUNCTION — a reflection-callable context spoof on a replicated
	 *  component would be a cheat vector. Compiled out of Shipping. */
	void SetNetContextOverrideForTests(TOptional<EPaper2DPlusNetContext> InOverride);

	/** TEST SEAM (TASK-57 U1): override the server clock the net code reads (consumed by the U2
	 *  publish/anchor paths; stored here so the seam lands with the rest of the U1 surface). Same
	 *  plain-C++/non-UFUNCTION/Shipping-stripped rules as SetNetContextOverrideForTests. */
	void SetServerTimeOverrideForTests(TOptional<double> InOverride);

	// TEST SEAMS (TASK-57 U2) — plain C++ accessors over the replication internals so the worldless
	// rigs can pre-write properties (net-driver simulation), pin the publish producer invariants
	// (incl. driving MoveInstanceCounter to the uint16 sentinel boundary), and observe stash/commit
	// state. Same non-UFUNCTION/Shipping-stripped rules as the U1 seams.
	void SetMoveInstanceCounterForTests(uint32 InCounter) { MoveInstanceCounter = InCounter; }
	uint32 GetMoveInstanceCounterForTests() const { return MoveInstanceCounter; }
	FPaper2DPlusRepAnimState& GetRepAnimStateForTests() { return RepAnimState; }
	uint16& GetRepProfileSeqForTests() { return RepProfileSeq; }
	uint16 GetProfileChangeCounterForTests() const { return ProfileChangeCounter; }
	uint16 GetLastAppliedSequenceForTests() const { return LastAppliedSequence; }
	bool HasPendingRepAnimStateForTests() const { return PendingRepAnimState.IsSet(); }
	/** Drives the SAME private retry BeginPlay's deferred one-tick ticker uses
	 *  (RetryPendingRepAnimState) — worldless rigs cannot run BeginPlay, so the stash-drain pin
	 *  exercises this path. */
	void FlushPendingRepAnimStateForTests() { RetryPendingRepAnimState(); }
	TSet<FPaper2DPlusHitDedupKey>& GetProcessedHitsForTests() { return ProcessedHits; }

	// TEST SEAMS (TASK-57 U4) — same non-UFUNCTION / Shipping-stripped rules as the U1/U2/U3 seams.

	/** OVERLAP-QUERY seam (TASK-57 U4): UPaper2DPlusHitboxSubsystem is a UWorldSubsystem unreachable
	 *  from worldless rigs, so ValidateAndRegisterHit routes its overlap re-query through one private
	 *  indirection (QueryAttackOverlapsForValidation) that consults THIS override before the subsystem.
	 *  Set it to inject the server-side overlap results every overlap-dependent Tier-1 scenario asserts
	 *  against; the real-subsystem path is exercised by U7's Tier-2/PIE. */
	void SetOverlapQueryOverrideForTests(TFunction<void(TArray<FHitboxCollisionResult>&)> InOverride)
	{
		OverlapQueryOverrideForTests = MoveTemp(InOverride);
	}

	/** Drive the per-tick frame span the validator / GetCurrentHitWindowIndex sample (TASK-57 U4):
	 *  worldless rigs that don't pump HandleFrameChanged can set the span directly so the span-based
	 *  frame check / window resolution run against a known (begin, end] window. */
	void SetFrameSpanForTests(int32 SpanBegin, int32 SpanEnd) { LastFrameSpanBegin = SpanBegin; LastFrameSpanEnd = SpanEnd; }

	/** TASK-91 (Codex F197a): the frame-parameterized attack-box build against the CURRENT move entry —
	 *  worldless rigs pin that the resolved span frame's boxes build even while the current frame
	 *  carries none, and that the build byte-matches the cached current-frame build for the same frame. */
	bool BuildWorldAttackBoxesForFrameForTests(int32 KeyFrame, TArray<FWorldHitbox>& OutBoxes) const
	{
		return BuildWorldAttackBoxesForFrame(ResolveCurrentMoveEntry(), KeyFrame, OutBoxes);
	}

	// TEST SEAMS (TASK-57 U5) — same non-UFUNCTION / Shipping-stripped rules as the U1/U2/U3/U4 seams.

	/** The replicated hit-stop snapshot (KTD-1): pre-write it (net-driver simulation) then drive
	 *  OnRep_HitStop, or observe the authority-side publishes (HitStopSeq bump on a new freeze, stable
	 *  Duration update on an extend, Duration=0 on a clear). */
	FPaper2DPlusRepHitStop& GetRepHitStopForTests() { return RepHitStop; }

	/** True while a deferred RepHitStop snapshot is stashed awaiting a server clock (TASK-57 U5). */
	bool HasPendingRepHitStopForTests() const { return PendingRepHitStop.IsSet(); }

	/** Drive the SAME private retry the no-server-clock deferral uses — worldless rigs cannot run the
	 *  one-shot ticker that drains it, so the defer-then-apply pin exercises this path. */
	void FlushPendingRepHitStopForTests() { RetryPendingRepHitStop(); }

	/** Read the real seconds the current/last freeze span elapsed (the KTD-19 re-anchor amount). */
	float GetActualFrozenSecondsForTests() const { return ActualFrozenSeconds; }

	// Test seams for composed Layer gameplay observability. Same
	// non-UFUNCTION / Shipping-stripped rules as the seams above.

	/** True while the composed Layer tier is active (cached readers consume ComposedCombatFrames). */
	bool IsComposedCombatActiveForTests() const { return bComposedCombatActive; }

	/** The composed buffer's allocated capacity — 0 pins "actors with no layer sibling never allocate". */
	int32 GetComposedCombatCapacityForTests() const { return ComposedCombatFrames.Max(); }

	/** Count of NotifyAppearanceCombatDirty pushes received — pins "previews never reach the push seam". */
	uint32 GetAppearanceCombatPushCountForTests() const { return AppearanceCombatPushCountForTests; }

	/** True while a push is deferred to end-of-dispatch (for example, equip from a Cue receiver). */
	bool HasPendingAppearanceCombatDirtyForTests() const { return bPendingAppearanceCombatDirty; }

	// TEST SEAMS — composed Frame Cue view observability.
	int32 GetComposedLayerCueCountForTests() const { return ComposedLayerFrameCues.Num(); }
	int32 GetWarmedFrameCueEffectCountForTests() const { return WarmedFrameCueEffectFlipbooks.Num(); }

	/** Identity, not just count — the warm set is a contract about WHICH art the animation pulled in. */
	bool IsFrameCueEffectWarmedForTests(const UPaperFlipbook* Flipbook) const
	{
		if (!Flipbook)
		{
			return false;
		}
		for (const TObjectPtr<UPaperFlipbook>& Warmed : WarmedFrameCueEffectFlipbooks)
		{
			if (Warmed.Get() == Flipbook)
			{
				return true;
			}
		}
		return false;
	}

	bool IsRangeCueActiveForTests(UPaper2DPlusCueBase* Cue) const { return ActiveRangeCues.Contains(Cue); }

	// TEST SEAMS (Cue behavior execution) — same non-UFUNCTION / Shipping-stripped rules as the seams
	// above. The worldless rigs have no UWorld and cannot run the engine teardown paths, so these
	// drive the exact production funnels rather than reimplementing them.

	/** Force the dedicated-server input the Frame Cue net gate reads. Unset = the world's net mode. */
	void SetDedicatedServerOverrideForTests(TOptional<bool> InOverride)
	{
		DedicatedServerOverrideForTests = InOverride;
	}

	/** Drive the component-teardown force-end (engine EndPlay checks bHasBegunPlay, which no
	 *  worldless rig can satisfy). Same funnel EndPlay and the animation boundary use. */
	void ForceEndActiveRangeCuesForTests(EPaper2DPlusFrameCueEndReason EndReason)
	{
		ForceEndActiveRangeCuesNow(EndReason, CachedCurrentFrameIndex);
	}

	/** Drive the unequip-immediate End-only sweep without a Layer render component. */
	void SweepStaleFrameCueRangesForTests() { SweepStaleComposedRangeCuesNow(); }

	// TEST SEAMS (TASK-145) — auto hit detection observability. Same non-UFUNCTION /
	// Shipping-stripped rules as the seams above.
	bool IsAutoHitDetectionArmedForTests() const { return bAutoDetectArmed; }

	// TEST SEAMS (Frame Cue detection) — same non-UFUNCTION / Shipping-stripped rules as the seams
	// above. Detection is a POLICY, so what a test needs to see is which rate was chosen, whether the
	// poll ran at all, and what the component said out loud when it could not deliver.

	/** How many detection polls have actually executed. Stays at zero on the event-driven path —
	 *  that is the zero-tick contract, observed rather than assumed. */
	uint32 GetDetectionPollCountForTests() const { return DetectionPollCountForTests; }

	/** Every loud detection diagnostic this component has emitted, in order. */
	const TArray<FString>& GetDetectionDiagnosticsForTests() const { return DetectionDiagnosticsForTests; }

	/**
	 * Force the PRE-FIX detection policy: the 20 Hz watch chosen from the CURRENT animation's
	 * features alone.
	 *
	 * This exists so the defect it reproduces stays a runnable test instead of becoming folklore —
	 * under this policy an animation shorter than the poll window is never observed, and no cue on it
	 * ever fires. Call it before the component begins play (the policy is read by UpdateTickState).
	 */
	void SetLegacySlowPollPolicyForTests(bool bEnable)
	{
		bLegacySlowPollPolicyForTests = bEnable;
		UpdateTickState();
	}
#endif

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	// Root motion tracking state
	int32 PreviousFrameIndex = INDEX_NONE;
	TWeakObjectPtr<UPaperFlipbook> PreviousFlipbook;
	FVector2D LastAppliedRootMotionPos = FVector2D::ZeroVector;

	// ─── Per-tick frame span (TASK-57 U4) ──────────────────────────────
	// The last key-frame SPAN HandleFrameChanged crossed, recorded at the top of HandleFrameChanged
	// BEFORE PreviousFrameIndex advances: span = (LastFrameSpanBegin, LastFrameSpanEnd] of key-frame
	// indices. Server tick quantization means a 1-frame active attack window is often never the
	// "current" frame — it can be crossed entirely inside one multi-frame tick (prev=3 -> new=6).
	// ValidateAndRegisterHit / GetCurrentHitWindowIndex sample the LATEST in-span attack key frame so
	// such a window still validates. SpanBegin is the PRIOR frame (exclusive); SpanEnd is the current
	// frame (inclusive). Both INDEX_NONE until the first frame change. NOT a UPROPERTY — transient.
	int32 LastFrameSpanBegin = INDEX_NONE;
	int32 LastFrameSpanEnd = INDEX_NONE;

	// Sprite offset tracking — the PARENT-SPACE delta currently applied to the flipbook component's
	// relative location (converted from the world-space frame offset at apply time), so it can be
	// undone exactly on frame/flipbook change. Parent-space, not world-space: a facing flip that
	// rotates the actor/capsule mirrors the applied delta in world space by itself, which left a
	// world-space record stale and made every subsequent commit double-apply the correction — the
	// sprite drifted 2x the authored offset further from the capsule on each left/right flick.
	FVector LastAppliedSpriteOffsetLocal = FVector::ZeroVector;

	// ─── Profile-driven sprite placement (TASK-157 U1) ─────────────────
	// The flipbook component's OWN relative transform, captured immediately before the first apply and
	// restored when the option is turned off. Captured with the per-frame sprite offset retired, so the
	// offset is never baked into what we call "authored". Re-captured when the apply funnel resolves a
	// DIFFERENT flipbook component, because that component carries its own authored value.
	FTransform AuthoredFlipbookRelativeTransform = FTransform::Identity;
	TWeakObjectPtr<UPaperFlipbookComponent> RelativeTransformCaptureTarget;
	bool bRelativeTransformApplied = false;

	/** Pure per-frame sprite-offset computation, shared by the frame-change path and the placement
	 *  re-seed so the two can never disagree about pixels-per-unit, facing, or scale. */
	FVector ComputeFrameSpriteOffset(int32 FrameIndex) const;

	/** Applies a newly computed world-space sprite offset as a PARENT-SPACE delta against the retained
	 *  record under the cosmetic-vs-root authority gate. The AddRelativeLocation and the record move
	 *  together — never split. */
	void CommitSpriteOffset(const FVector& NewOffset, EPaper2DPlusNetContext NetCtx);

	/** Physically undoes the currently applied sprite offset (the exact parent-space delta that was
	 *  added) and clears the record, leaving the component at its offset-free pose. */
	void RetireAppliedSpriteOffset();

	/** Recomputes and re-applies the sprite offset for the live key frame at the CURRENT scale. */
	void ReseedSpriteOffsetForCurrentFrame();

	// Current-frame cache. Local frame data is updated only on frame changes;
	// world-space arrays refresh lazily when queried and the transform changed.
	TWeakObjectPtr<UPaperFlipbook> CachedRuntimeFlipbook;
	int32 CachedCurrentFrameIndex = INDEX_NONE;
	FFrameHitboxData CachedLocalFrameData;
	FVector2D CachedPivotFraction = FVector2D::ZeroVector;
	bool bCachedLocalFrameDataValid = false;
	bool bCachedHasAttack = false;
	bool bCachedHasHurtbox = false;
	mutable bool bCachedWorldStateValid = false;
	mutable FTransform CachedWorldTransform = FTransform::Identity;
	mutable FPaper2DPlusRuntimeFrameState CachedRuntimeFrameState;

	// ─── Shared cache (owned by OnFlipbookChanged) ────────────────────
	// Owner: OnFlipbookChanged() — the only legal writer
	// Warmed on: SetFlipbook delegate fire OR slow-poll tick detecting change
	// Populator: FFlipbookProfileEntry::GetCacheView() (single helper)
	// Invalidated on: new flipbook change (overwrites in place)
	// Readers:
	//   CachedCombatData     → hitbox/socket queries via Paper2DPlusBlueprintLibrary
	//   CachedMotionData     → ApplyRootMotionForFrame, GetRootMotionDelta
	//   CachedFrameEventData → HandleFrameChanged Frame Cue dispatch loop (Phase 2)
	// DO NOT WRITE outside OnFlipbookChanged. Narrow-named resets
	// (ResetRootMotionTracking, etc.) must not touch these fields.
	// See docs/solutions/ue-shared-cache-ownership-patterns.md
	const struct FFlipbookCombatData*      CachedCombatData     = nullptr;
	const struct FFlipbookMotionData*      CachedMotionData     = nullptr;
	const struct FFlipbookFrameEventData*  CachedFrameEventData = nullptr;
	// ──────────────────────────────────────────────────────────────────

	// ─── Composed Layer gameplay tier ───────────────────────────────────
	// A SECOND, component-OWNED tier layered over the raw CachedCombatData view for actors with a layer
	// render sibling. Ownership mirrors the block above:
	// Owner: RecomposeAppearanceCombatFrames() — the only legal writer of bComposedCombatActive /
	//   ComposedCombatFrames and ComposedLayerFrameCues. Driven by three callers:
	//     (a) NotifyAppearanceCombatDirty — the render component's committed appearance push
	//         (committed setters + the OnRep apply funnels; previews never push by construction),
	//     (b) OnFlipbookChanged — recompose for the NEW animation from the STORED digest (no push
	//         happens on a flipbook change; a mid-preview flipbook change therefore still composes from
	//         committed state), and
	//     (c) the HandleFrameChanged dispatch gate's DEAD-ASSET defensive drop (U6) — a died
	//         AppearanceDigestLayerAsset clears the composed views before they reach a snapshot.
	// Digest: one component-owned copy of the canonical appearance descriptor plus a weak Layer Asset.
	// Compatibility maps are derived only on the immediate compose stack; no second persistent selection
	// model survives beside the descriptor.
	// Readers: RefreshCachedLocalFrameState (frame copy) and the hit-validation span sampler
	//   (FrameHasAttackHitboxEffective) read ComposedCombatFrames when bComposedCombatActive, else the
	//   raw CachedCombatData->Frames path BYTE-IDENTICALLY. FrameExtractionInfo / DefaultClashCategory /
	//   frame-level fields always resolve from the raw base view (base-only by contract).
	// Non-layered actors: no push ever happens ⇒ the flag stays false and the array never allocates.
	// DO NOT WRITE these fields outside the two named callers; narrow-named resets must not touch them.
	bool bHasAppearanceCombatDigest = false;
	FPaper2DPlusAppearanceDescriptor AppearanceDigest;
	TWeakObjectPtr<const UPaper2DPlusCharacterLayerAsset> AppearanceDigestLayerAsset;
	bool bComposedCombatActive = false;
	TArray<FFrameHitboxData> ComposedCombatFrames;
	// The current cached move's authored FlipbookName (set in OnFlipbookChanged alongside the raw warm) —
	// the animation key ComposeCombatFrames matches AnimationOverrides entries against.
	FString CachedMoveNameForCompose;
	// A push landed while Frame Cue dispatch / a flipbook-change teardown was live: the digest is
	// already cached; the recompose runs at FlushPendingAnimationDispatch (end of dispatch).
	bool bPendingAppearanceCombatDirty = false;

	// Equip-aware Cue sibling of ComposedCombatFrames. Every equipped variant's matching animation
	// override contributes FrameCues in stable stack order; base Cues are prepended at dispatch time.
	TArray<TObjectPtr<UPaper2DPlusCueBase>> ComposedLayerFrameCues;

	/** Strongly retains only Spawn Effect flipbooks reachable by the current base+Layer cue union. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UPaperFlipbook>> WarmedFrameCueEffectFlipbooks;

	/** Explicit animation/equip boundary: resolve current Spawn Effect cues before frame-0 dispatch. */
	void WarmCurrentFrameCueEffects();

	/** Keeps the current animation's directional variant art resident (released on animation change). */
	TSharedPtr<struct FStreamableHandle> DirectionalVariantWarmHandle;

	/** Async-warm the entry's occupied directional slots so a first facing change never sync-loads. */
	void WarmDirectionalVariantArt(const FFlipbookProfileEntry& Entry);
	void ResetDirectionalVariantWarm();

public:
	/** Test-only visibility for the warm handle's lifecycle (created on cache warm, released on change). */
	bool HasDirectionalVariantWarmForTests() const
	{
		return DirectionalVariantWarmHandle.IsValid();
	}

private:

	/** Recompose ComposedCombatFrames for the current cached move from the stored digest (see block
	 *  comment above — the ONLY legal writer of the composed tier). No digest / no cache / dead asset /
	 *  no matching Layer rows ⇒ composed inactive, buffer reset (raw path). Also recomposes the Layer-owned
	 *  Frame Cue view. */
	void RecomposeAppearanceCombatFrames();

	/** Recompose NOW + MarkCachedWorldStateDirty + re-copy the current frame's local state (the dirty-
	 *  notify tail shared by the direct push and the deferred end-of-dispatch drain), then run the U6
	 *  stale Cue State sweep so cues dropped from the composed view End immediately — guaranteed
	 *  outside Frame Cue dispatch. */
	void ApplyAppearanceCombatDigestNow();

	/** Unequip-immediate End: sweep the union of base and composed Frame Cues with a never-admit
	 *  predicate, ending active Layer ranges that left the view before the next frame change. */
	void SweepStaleComposedRangeCuesNow();

	/** THE one notification sink behind every dispatch site: the cue's own behavior runs first, then
	 *  the world reacts through the listener broadcast. Ordering is fixed here so no call site can
	 *  disagree, and an orphaned placement simply contributes no behavior. */
	void NotifyFrameCue(
		UPaper2DPlusCueBase& Cue,
		const FPaper2DPlusFrameCueContext& CueContext);

	/** Test-only force-end seam (reached via ForceEndActiveRangeCuesForTests): purge Layer ranges
	 *  whose asset died, End every remaining active range with the supplied reason, then drop the
	 *  net-gating latches. Production teardown does NOT route through here — component teardown and
	 *  the animation-change boundary run the first-wins terminal transaction (ClaimFrameCueTerminal +
	 *  DrainPendingFrameCueTerminal), and the wardrobe boundary uses SweepStaleComposedRangeCuesNow.
	 *  The End notifications run under the dispatch re-entrancy guard, so playback mutations made from
	 *  a teardown handler defer through the pending machinery instead of re-entering. */
	void ForceEndActiveRangeCuesNow(
		EPaper2DPlusFrameCueEndReason EndReason,
		int32 CurrentFrameForContext);

	/** When the digest's layer asset has
	 *  DIED (weak ref IsStale — collected or marked garbage; an explicitly-pushed null asset does NOT
	 *  qualify), every ActiveRangeCues member not in the base view came from that dead asset's composed
	 *  view, so it is dropped by pointer identity and never dereferenced (the primary path —
	 *  the layer component's teardown CLEAR — force-ends them properly while still alive). Called before
	 *  every site that dereferences the set: the HandleFrameChanged dispatch gate + HandleFlipbookChanged's
	 *  teardown loop (via the dead-asset recompose), NotifyAppearanceCombatDirty (before the digest
	 *  overwrite hides the staleness), and ClearAppearanceCombatDigest. No-op when the digest asset is
	 *  alive, null-pushed, or never set. */
	void PurgeDanglingComposedRangeCuesIfDigestAssetDied();

	/** The hit-validation span sampler's frame read (TASK-57 U4 path): the composed tier when active AND
	 *  Entry is the current cached move, else the raw static FrameHasAttackHitbox — so hit validation and
	 *  the world-box tier can never disagree about what the equipped character's frames carry. */
	bool FrameHasAttackHitboxEffective(const struct FFlipbookProfileEntry* Entry, int32 KeyFrame) const;
	// ──────────────────────────────────────────────────────────────────

	// ─── Hit-stop state (TASK-76 PR4) ──────────────────────────────────
	// Ownership: WRITTEN only by TriggerHitStop/FreezeActorOnce (freeze+track) and
	// EndHitStopInternal (THE single restoration path — timer expiry, CancelHitStop, and
	// EndPlay teardown all land there). Transient runtime state — never serialized.
	// The captured prior dilations live in the file-scope shared refcounted registry
	// (FPaper2DPlusHitStopRegistry, cpp) so overlapping hit-stops from DIFFERENT components
	// (trades, gang hits) never capture another component's freeze sentinel as a "prior".

	// Every actor THIS component froze in the CURRENT hit-stop (weak — tolerates actors dying
	// mid-freeze). An actor appears at most once — FreezeActorOnce skips already-tracked actors
	// so a retrigger can never double-refcount the same actor from one component.
	TArray<TWeakObjectPtr<AActor>> FrozenActors;

	bool bHitStopActive = false;

	// REAL (undilated) seconds left on the freeze; decremented by the core-ticker lambda.
	float HitStopRemainingSeconds = 0.f;

	// Handle for the core ticker driving the unfreeze. Unregistered by EndHitStopInternal
	// (and therefore by EndPlay), so a stale ticker never outlives play.
	FTSTicker::FDelegateHandle HitStopTickerHandle;

	// Re-entry guard for the OnHitStopBegin/End broadcasts (the commit-before-broadcast guard
	// pattern): a handler that synchronously calls
	// TriggerHitStop is logged and rejected. CancelHitStop is NEVER rejected — restoration always
	// runs (the EndPlay dying-attacker rescue path goes through it, possibly from inside a Begin
	// broadcast); only the nested OnHitStopEnd broadcast is suppressed (logged Verbose).
	bool bBroadcastingHitStop = false;

	// ─── Replicated hit-stop (TASK-57 U5) ──────────────────────────────
	// REAL seconds the current freeze span has actually elapsed — accumulated by the unfreeze ticker
	// lambda (the SAME DeltaTime that decrements HitStopRemainingSeconds), captured by the authority
	// clock re-anchor at EndHitStopInternal (RepAnimState.StartServerTime += ActualFrozenSeconds —
	// KTD-19). Reset to 0 when a fresh freeze begins (BeginHitStopLocal, new-freeze path). Transient.
	float ActualFrozenSeconds = 0.f;

	// Deferred RepHitStop snapshot (TASK-57 U5): a snapshot arriving before the server clock exists
	// (GameState not yet replicated — initial join / seamless travel) stashes here and re-applies one
	// frame later via the BeginPlay-style one-shot core ticker (KTD-21 — applying full Duration off a
	// missing clock could freeze for an arbitrary span; deferring one frame is safer). Latest-wins.
	TOptional<FPaper2DPlusRepHitStop> PendingRepHitStop;

	// Warn-once latch (TASK-57 U5): a non-authority networked TriggerHitStop is a no-op (the freeze is
	// authority-published cosmetic state, never client-triggered).
	bool bWarnedTriggerHitStopOnNonAuthority = false;

	// Receiver-side last-applied HitStopSeq (TASK-57 U5). The published HitStopSeq distinguishes a fresh
	// freeze (bumped seq) from an EXTEND (same seq) — but a proxy decides extend-vs-new on its LOCAL
	// freeze state, which conflates the two when a genuinely NEW freeze (the prior freeze's clear
	// coalesced away on the wire) lands while a stale local freeze is still draining. Tracking the wire
	// seq lets OnRep_HitStop tell BeginHitStopLocal to REPLACE (not Max) on a distinct seq, so the proxy
	// adopts the server's CURRENT freeze span instead of the elapsed one. Authority/Standalone never go
	// through OnRep, so this is proxy-only. Transient.
	uint16 LastReceivedHitStopSeq = 0;
	// ──────────────────────────────────────────────────────────────────

	// Derive has-feature flags on read instead of storing separately
	bool HasCachedRootMotion() const { return CachedMotionData && CachedMotionData->HasRootMotion(); }
	bool HasCachedFrameCues() const
	{
		return (CachedFrameEventData && CachedFrameEventData->FrameCues.Num() > 0)
			|| ComposedLayerFrameCues.Num() > 0;
	}

	/**
	 * True when this actor can dispatch a Frame Cue from SOME animation, not just the current one.
	 *
	 * The detection RATE has to be chosen from this rather than from HasCachedFrameCues(), because a
	 * rate chosen from the current animation is always one animation out of date — which is exactly
	 * how a short cue-carrying animation slips between two slow polls unobserved. Memoized against
	 * the profile it was scanned from AND that profile's editor content revision, OR-ed with the
	 * equipped Layer asset's own scan and with the composed-Layer latch, because a wardrobe Layer can
	 * add cues to an actor whose base profile has none.
	 *
	 * The revision is what makes the memo safe to hold: pointer identity alone cannot see a cue
	 * authored into an ALREADY-assigned profile (same pointer, same element count), and for an
	 * animation shorter than one poll the RATE decides whether cues dispatch at all — so a stale
	 * "no cues" answer costs dispatches, not merely latency. Cooked builds report revision 0 always,
	 * where nothing can edit an asset in place. MaybeWarnDetectionPollTooCoarse remains the backstop
	 * that names the actor out loud if a cue-carrying animation is nevertheless sampled too slowly.
	 */
	bool ProfileCarriesAnyFrameCues() const;
	mutable TWeakObjectPtr<const UPaper2DPlusCharacterProfileAsset> CueScanProfileKey;
	mutable uint32 CueScanProfileRevision = 0;
	mutable bool bCueScanValid = false;
	mutable bool bCueScanFoundCues = false;

	/**
	 * True when the LIVE appearance digest's Layer asset carries a Frame Cue on ANY animation.
	 *
	 * The latch below cannot answer this: it flips only after an animation whose composed set is
	 * non-empty has been observed at least once, and on a cue-free base profile the slow watch is
	 * exactly what prevents that first observation from ever happening — the same "one step out of
	 * date" defect as the rate itself, one level up. Reading the ASSET at digest-push time knows
	 * about a Layer cue on an animation that has never played. Memoized per Layer asset (FObjectKey,
	 * so a dead asset is never mistaken for "no digest") AND that asset's editor content revision, so
	 * a cue authored into an already-equipped Layer re-opens the question instead of inheriting the
	 * pre-edit answer. Revision is always 0 in cooked builds.
	 */
	bool AppearanceDigestCarriesAnyFrameCues() const;
	mutable FObjectKey CueScanLayerKey;
	mutable uint32 CueScanLayerRevision = 0;
	mutable bool bCueScanLayerValid = false;
	mutable bool bCueScanLayerFoundCues = false;

	/** Latched once this component has composed any Layer-owned Frame Cue — a backstop behind the
	 *  asset scan above, never the primary answer (it is one observation out of date by nature).
	 *  Cleared wherever the observation stops describing this component: on a profile swap (alongside
	 *  bCueScanValid) and in the EndPlay digest teardown, so a component re-pointed at a cue-free
	 *  profile stops claiming a Layer cue it saw in a previous life. */
	bool bObservedComposedLayerCues = false;

	// Active Cue State tracking — used by HandleFrameChanged for Begin/End diff.
	// Cleared on flipbook change (HandleFlipbookChanged ends all active before clearing).
	TSet<TObjectPtr<UPaper2DPlusCueBase>> ActiveRangeCues;
	bool bFrameCueSourceEndedBroadcast = false;
	void NotifyFrameCueSourceEnded();

	// Re-entry guard for HandleFrameChanged. A BP event handler that synchronously
	// triggers SetFlipbook queues the nested change until the current dispatch exits.
	bool bDispatchingFrameCues = false;

	// Re-entry guard for HandleFlipbookChanged. A Cue receiver can request another animation while the
	// previous Cue State set is ending.
	bool bHandlingFlipbookChange = false;

	bool bHasPendingFlipbookChange = false;
	TWeakObjectPtr<UPaperFlipbook> PendingFlipbookChange;
	bool bHasPendingFrameChange = false;
	int32 PendingFrameChange = INDEX_NONE;

	// ─── Frame Cue playback source + generation lifecycle ──────────────

	/** Separately tracked binding identity. Public FlipbookComponent can be assigned directly by
	 *  legacy C++; sender-aware callbacks compare both and fail closed before processing stale work. */
	UPROPERTY(Transient)
	TObjectPtr<UPaperFlipbookComponent> BoundFrameCuePlaybackSource = nullptr;

	/** Last complete runtime invocation snapshot. Forced Ends copy this rather than rereading mutable
	 *  Profile/source properties, preserving Profile A through a reentrant A→B replacement. */
	UPROPERTY(Transient)
	FPaper2DPlusFrameCueContext RuntimeFrameCueContext;

	/** First-wins terminal transaction. Its context and authored Cue order strongly retain the
	 *  outgoing generation until End delivery has drained. */
	UPROPERTY(Transient)
	FPaper2DPlusFrameCueContext PendingFrameCueTerminalContext;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UPaper2DPlusCueBase>> PendingFrameCueTerminalOrder;

	UPROPERTY(Transient)
	TObjectPtr<UPaperFlipbookComponent> PendingFrameCueTerminalSource = nullptr;

	bool bHasPendingFrameCueTerminal = false;
	bool bFrameCueTerminalAccepted = false;
	bool bPendingTerminalAllowsFinalFrame = false;
	EPaper2DPlusFrameCueEndReason PendingFrameCueTerminalReason =
		EPaper2DPlusFrameCueEndReason::None;
	int64 PendingFrameCueTerminalGeneration = 0;
	int64 FrameCuePlaybackGeneration = 0;

	/** External generations opt out of IsPlaying observation: PaperZD deliberately leaves its render
	 *  component stopped while driving playback position manually. */
	bool bExternalFrameCuePlaybackGeneration = false;
	/** A custom source may restart from inside the external generation's End callback. Its start
	 *  signal arrives before external ownership can be released, so the terminal drain replays it. */
	bool bNativePlaybackStartPendingAfterExternalTerminal = false;

	bool bHasObservedFrameCuePlayingState = false;
	bool bObservedFrameCuePlaying = false;
	bool bBoundStockNaturalCompletionLatched = false;
	/** Generation claimed by the custom component's early natural-finish signal. Its matching
	 *  post-Super terminal notification is a drain marker, never a terminal for a replacement gen. */
	int64 EarlyCustomNaturalFinishGeneration = 0;
	uint64 FrameCuePlaybackBindingEpoch = 0;

	FDelegateHandle BoundFlipbookChangedHandle;
	FDelegateHandle BoundFrameChangedHandle;
	FDelegateHandle BoundPlaybackStartedHandle;
	FDelegateHandle BoundPlaybackObservedHandle;
	FDelegateHandle BoundPlaybackNaturalFinishHandle;
	FDelegateHandle BoundPlaybackTerminalHandle;
	FDelegateHandle BoundPlaybackSourceDestroyedHandle;

	UPROPERTY(Transient)
	TObjectPtr<UPaper2DPlusFrameCueStockPlaybackObserver> BoundStockPlaybackObserver = nullptr;

	bool bHasPendingFrameCuePlaybackSource = false;
	UPROPERTY(Transient)
	TObjectPtr<UPaperFlipbookComponent> PendingFrameCuePlaybackSource = nullptr;

	bool bHasPendingCharacterProfile = false;
	UPROPERTY(Transient)
	TObjectPtr<UPaper2DPlusCharacterProfileAsset> PendingCharacterProfile = nullptr;

	void BindFrameCuePlaybackSource(UPaperFlipbookComponent* Source);
	void UnbindFrameCuePlaybackSource();
	void ApplyFrameCuePlaybackSource(UPaperFlipbookComponent* NewPlaybackSource);
	bool ValidateBoundFrameCuePlaybackSource(
		const UPaperFlipbookComponent* ExpectedSource,
		const TCHAR* Operation);

	void HandleBoundFlipbookChanged(
		UPaper2DPlusFlipbookComponent* Source,
		UPaperFlipbook* NewFlipbook);
	void HandleBoundFrameChanged(
		UPaper2DPlusFlipbookComponent* Source,
		int32 NewFrame);
	void HandleBoundPlaybackStarted(UPaper2DPlusFlipbookComponent* Source);
	void HandleBoundPlaybackObserved(UPaper2DPlusFlipbookComponent* Source);
	void HandleBoundPlaybackNaturalFinishDetected(UPaper2DPlusFlipbookComponent* Source);
	void HandleBoundPlaybackTerminal(
		UPaper2DPlusFlipbookComponent* Source,
		bool bCompletedNaturally);
	void HandleBoundPlaybackSourceDestroyed(UPaper2DPlusFlipbookComponent* Source);

	void HandleBoundStockPlaybackFinished(
		UPaperFlipbookComponent* Source,
		uint64 BindingEpoch);

	void BeginFrameCuePlaybackGeneration(
		UPaperFlipbookComponent* Source,
		bool bExternalOwner,
		bool bForceCurrentFrameEvaluation);
	void RefreshRuntimeFrameCueContextIdentity();
	FPaper2DPlusFrameCueContext MakeRuntimeFrameCueContext(
		int32 CurrentFrame,
		int32 PreviousFrame,
		bool bWasLoopWrap,
		EPaper2DPlusNetContext NetContext,
		EPaper2DPlusFrameCueEvaluationMode EvaluationMode) const;
	bool ClaimFrameCueTerminal(
		EPaper2DPlusFrameCueEndReason EndReason,
		UPaperFlipbookComponent* ExpectedSource,
		int64 ExpectedGeneration,
		bool bAllowFinalFrameBeforeDrain = false,
		bool bRequirePublicSourceMatch = true);
	void DrainPendingFrameCueTerminal();
	void ProcessObservedFrameCuePlaybackState(UPaperFlipbookComponent* Source);

	friend class UPaper2DPlusFrameCueStockPlaybackObserver;

	// True when bound to UPaper2DPlusFlipbookComponent::OnFlipbookChanged delegate.
	// Event-driven path: profile component tick stays disabled.
	// Slow-poll fallback: stock PaperFlipbookComponent compatibility.
	bool bEventDrivenFlipbookDetection = false;

	// ─── Networking seam (TASK-57 U1/U2) ───────────────────────────────
#if !UE_BUILD_SHIPPING
	// Test-only forced context / server clock (see the SetXOverrideForTests seams above).
	// Transient, never serialized, compiled out of Shipping.
	TOptional<EPaper2DPlusNetContext> NetContextOverrideForTests;
	TOptional<double> ServerTimeOverrideForTests;

	// Test-only dedicated-server input for the Frame Cue net gate (see the seam above).
	TOptional<bool> DedicatedServerOverrideForTests;

	// Test-only push counter for the U5 composed-combat seam (see GetAppearanceCombatPushCountForTests).
	uint32 AppearanceCombatPushCountForTests = 0;
#endif

	/** THE single context resolver behind GetNetContext and every gating call site (TASK-57 U2):
	 *  the test override when set (highest priority); else Standalone unless replication is enabled
	 *  on a networked world (bEnableReplication — the BeginPlay-consumed snapshot once BeginPlay has
	 *  run — && GetWorld() && GetNetMode() != NM_Standalone); else the owner's role maps to
	 *  Authority / AutonomousProxy / SimulatedProxy. Logs once if bEnableReplication was flipped
	 *  after BeginPlay (the flip is inert — SetIsReplicated was consumed at BeginPlay). */
	EPaper2DPlusNetContext ResolveNetContext() const;

	// ─── Replicated state (TASK-57 U2) ──────────────────────────────────
	// Registered UNCONDITIONALLY in GetLifetimeReplicatedProps; actually replicated only when
	// bEnableReplication opted in (SetIsReplicated at BeginPlay). Replication-only payloads —
	// no BP exposure (installed-UHT rules; games consume the advisory delegate).

	/** Profile generation counter (KTD-6) — rides the SAME bunch as CharacterProfile so the pairing
	 *  arrives atomically. Authority bumps it in SetCharacterProfile BEFORE the publish funnel runs;
	 *  clients adopt it in OnRep_CharacterProfile. */
	UPROPERTY(Replicated)
	uint16 RepProfileSeq = 0;

	/** The sequence-numbered, server-time-anchored anim-state snapshot (KTD-4/5). Written ONLY by
	 *  PublishAnimStateIfAuthority on the authority; consumed via OnRep_AnimState on clients. */
	UPROPERTY(ReplicatedUsing = OnRep_AnimState)
	FPaper2DPlusRepAnimState RepAnimState;

	/** Self-expiring replicated hit-stop snapshot (KTD-1) — declared in U2, behavior lands in PR5. */
	UPROPERTY(ReplicatedUsing = OnRep_HitStop)
	FPaper2DPlusRepHitStop RepHitStop;

	// ─── Non-replicated net bookkeeping (TASK-57 U2) ────────────────────
	// MoveInstanceCounter/ProcessedHits advance UNCONDITIONALLY (Standalone too) in OnFlipbookChanged
	// and the self-loop confirm — they feed single-player hit dedup in U4, not just the wire.

	/** Monotonic move-instance counter; uint16(MoveInstanceCounter) IS the wire Sequence at every
	 *  publish (the producer invariant U3's stale-seq check consumes), with the sentinel-skip rule
	 *  applied to the COUNTER before stamping (0 is the never-published sentinel). */
	uint32 MoveInstanceCounter = 0;

	/** Once-per-(attacker,victim,move-instance,hit-window) dedup set — Reset on every move instance;
	 *  consumed by U4's ValidateAndRegisterHit. */
	TSet<FPaper2DPlusHitDedupKey> ProcessedHits;

	/** Local profile generation: authority increments (SetCharacterProfile); clients adopt from the
	 *  paired RepProfileSeq (OnRep_CharacterProfile). The anim struct's ProfileSeq is compared
	 *  against THIS — never adopted from the anim struct (KTD-6, spawn-order deadlock). */
	uint16 ProfileChangeCounter = 0;

	/** Last wire Sequence committed by an apply ("different => apply"; 0 = nothing applied yet). */
	uint16 LastAppliedSequence = 0;

	/** Last wire StartServerTime committed by an apply (Codex F195a). A SAME-Sequence republish carries
	 *  a CHANGED anchor when the authority re-anchors mid-move (RepublishAnimState after SetPlaybackPosition/
	 *  SetPlayRate, or the U5 hit-stop clock re-anchor). The same-seq apply branch detects the change against
	 *  this and re-derives + re-broadcasts so advise-only consumers (the default gate+advise mode) actually
	 *  learn the re-anchor instead of drifting. -1 = nothing applied yet / no anchor. */
	double LastAppliedStartServerTime = -1.0;

	/** Deferred anim state (latest-wins): stashed when an apply can't run yet (BeginPlay pending /
	 *  no server clock / generation mismatch / profile-move without a profile); drained one tick
	 *  AFTER the BeginPlay tail (FTSTicker deferral — initial-bunch RepNotifies precede BP binds),
	 *  at the profile OnRep, and superseded by any later applied OnRep. */
	TOptional<FPaper2DPlusRepAnimState> PendingRepAnimState;

	/** True while an OnRep funnel drives the existing mutators (SetCharacterProfile /
	 *  HandleFlipbookChanged): bypasses the KTD-22 proxy gate and catch-up-suppresses one-shot
	 *  Frame Cue dispatch (minimal U2 form — full WarmForReplicatedApply lands in U8). */
	bool bApplyingReplicatedState = false;

	/** Re-entry guard for the OnReplicatedAnimStateChanged broadcast — SIXTH member of the guard
	 *  family. Commit-before-broadcast means a re-entrant apply only loses the nested broadcast,
	 *  never committed state (the suppress-or-coerce rule). */
	bool bBroadcastingReplicatedAnimState = false;

	/** bEnableReplication as consumed at BeginPlay — the value ResolveNetContext honors once
	 *  BeginPlay has run (post-BeginPlay flips are inert + logged once). */
	bool bReplicationEnabledAtBeginPlay = false;

	// Warn-once latches (TASK-57 U2 diagnostics).
	bool bWarnedArchetypeMismatch = false;
	bool bWarnedSetProfileOnProxy = false;
	mutable bool bLoggedReplicationFlipPostBeginPlay = false;

	// Warn-once latch (TASK-57 U4 diagnostics): the hit-adjudication gate
	// (ValidateAndRegisterHit/RegisterHitOnce on a non-authority context).
	bool bWarnedHitAdjudicationOnNonAuthority = false;

	// ─── Automatic hit detection state (TASK-145) ───────────────────────
	// Armed = the CURRENT key frame carries attack boxes while bAutoHitDetection is on and the
	// context adjudicates (Standalone/Authority). Written only by UpdateAutoHitDetectionForFrame,
	// FinalizeAutoHitDetectionForMoveEnd, and TickAutoHitDetection's direct-write disarm. Transient.
	bool bAutoDetectArmed = false;

	// Whiff bookkeeping, reset per move instance: the move armed at least one window / registered at
	// least one hit. The registered flag is ALSO set by the manual ValidateAndRegisterHit /
	// RegisterHitOnce success paths so mixed manual+auto games never see a false whiff.
	bool bMoveInstanceHadArmedFrames = false;
	bool bMoveInstanceRegisteredHit = false;

	// Re-entry guard for the auto-detection pass (guard family): a receiver that synchronously
	// triggers another pass is dropped, never recursed.
	bool bRunningAutoHitPass = false;

	/** The per-frame arming/edge/detection driver — called from HandleFrameChanged after Frame Cue
	 *  dispatch (the Begin edge precedes the window's first hits; End fires on the first frame
	 *  WITHOUT attack boxes). Fast-path no-op when the feature is off and nothing is armed. */
	void UpdateAutoHitDetectionForFrame(int32 NewFrame, EPaper2DPlusNetContext NetCtx);

	/** One detection pass: query through QueryAttackOverlapsForValidation (the same worldless test
	 *  seam as the validator), dedup through ProcessedHits, broadcast OnHitConnected/OnHitReceived.
	 *  Commit-before-broadcast: the dedup entry registers before receivers run. */
	void RunAutoHitDetectionPass();

	/** Move-end finalize (TASK-145): close an open armed window (OnAttackWindowEnd), evaluate the
	 *  whiff (armed frames but zero registered hits; suppressed for EndPlay teardown), and reset the
	 *  per-move flags. Called from HandleFlipbookChanged BEFORE the cache swap (the OLD move's name
	 *  is still cached) and from EndPlay. */
	void FinalizeAutoHitDetectionForMoveEnd(bool bBroadcastWhiff);

	/** Register/unregister this component on the subsystem's armed set (world-guarded — a no-op in
	 *  worldless rigs, where the frame-entry pass alone drives detection). */
	void SetAutoDetectArmedRegistration(bool bArmed);

#if !UE_BUILD_SHIPPING
	// Test-only overlap-query override (TASK-57 U4): when set, QueryAttackOverlapsForValidation fills
	// the caller's array from this instead of the unreachable UWorldSubsystem. Transient, Shipping-out.
	TFunction<void(TArray<FHitboxCollisionResult>&)> OverlapQueryOverrideForTests;
#endif

	// ─── Apply mode + gate latch (TASK-57 U8) ───────────────────────────
	/** Cue State classes rejected by the net-policy gate at their Begin edge. Cleared on animation
	 *  change and loop wrap so admission is re-evaluated at the next lifecycle boundary. */
	TSet<TObjectPtr<UPaper2DPlusCueBase>> LatchedOutRangeCues;

	/** Apply-mode catch-up target: set (TGuardValue) by the proxy apply path so the flipbook-change
	 *  funnel warms to THIS key frame (a mid-move position) instead of frame 0. INDEX_NONE = warm to
	 *  frame 0 (the normal, non-catch-up path). */
	int32 PendingCatchUpTargetFrame = INDEX_NONE;

	// Warn-once latches (TASK-57 U8 diagnostics).
	bool bWarnedRootMotionGatedToProxy = false;
	bool bWarnedConsumeRootMotionOnProxy = false;
	bool bWarnedRepublishOnNonAuthority = false;

	// Warn-once latch: a re-warmed animation still carries legacy executable Frame Events in
	// FrameEventData FrameEvents, which runtime dispatch never reads (it consumes only FrameCues).
	// Set at the OnFlipbookChanged cache-warm site; guarded by HasBegunPlay so preview/CDO never spam.
	bool bWarnedUndispatchedLegacyFrameEvents = false;

	// ─── Frame Cue detection diagnostics (R8) ──────────────────────────
	// The contract these serve: an animation carrying cues either DISPATCHES, or this component says
	// so loudly and attributably — once per actor, on screen as well as in the log, because a log
	// line alone is the silence R8 exists to end. Both latches are per component, both emission sites
	// are guarded by HasBegunPlay so preview/CDO/worldless construction never spam.

	/** Set once the detection poll was found to be coarser than a cue-carrying animation is long. */
	bool bWarnedDetectionPollTooCoarse = false;

	/** Set once a warmed animation was found to carry placements dispatch cannot deliver. */
	bool bWarnedUndispatchableFrameCuePlacements = false;

	/** One loud, attributable, once-per-actor diagnostic: a LogPaper2DPlus warning AND, outside
	 *  Shipping, an on-screen message keyed uniquely per actor so actors never overwrite each other. */
	void EmitFrameCueDetectionDiagnostic(int32 DiagnosticId, const FString& Message);

	/** Warn when the current cue-carrying animation's WALL-CLOCK life (authored length / |PlayRate|) is
	 *  no longer than the sampling period that found it — the shape where a whole animation is MISSED
	 *  rather than merely detected late. Judged on the REQUESTED PrimaryComponentTick.TickInterval, not
	 *  on an observed DeltaTime: an actor already at TickInterval == 0 has nothing coarser to fix, and a
	 *  frame hitch on such an actor must never latch this alarm. */
	void MaybeWarnDetectionPollTooCoarse(float RequestedPollIntervalSeconds);

	/** Warn when the freshly warmed animation carries placements dispatch will silently skip. */
	void MaybeWarnUndispatchableFrameCuePlacements();

#if !UE_BUILD_SHIPPING
	// Detection observability + the legacy-policy switch (see the test seams above).
	uint32 DetectionPollCountForTests = 0;
	TArray<FString> DetectionDiagnosticsForTests;
	bool bLegacySlowPollPolicyForTests = false;
#endif

	/** Apply the resolved replicated state on a simulated proxy (the second gate+advise exception):
	 *  set PlayRate/looping, switch the flipbook through the EXISTING HandleFlipbookChanged funnel under
	 *  the bApplyingReplicatedState + PendingCatchUpTargetFrame guards (full catch-up warm), and seek to
	 *  the anchor-derived position. No-op when there is no resolved flipbook component (TASK-57 U8). */
	void ApplyReplicatedFlipbookToProxy(UPaperFlipbook* Flipbook, float PlaybackPosition, const FPaper2DPlusRepAnimState& State);

	/** Drift/clock correction, called on each key-frame change (KTD-19/20). On a SimulatedProxy in apply
	 *  mode: snap the local playback position to the server-formula position when it drifts beyond
	 *  NetPlaybackSnapToleranceSeconds. On Authority: auto-re-anchor the published state when the actual
	 *  position diverges from the model (a game-side mutation made without RepublishAnimState). Skipped
	 *  during a local hit-stop freeze (the clock legitimately pauses) and during the apply warm itself.
	 *  Early-outs in O(1) when nothing is published (Sequence==0) — zero single-player cost. */
	void MaybeCorrectReplicatedPlaybackDrift(EPaper2DPlusNetContext NetCtx);

	/** Publish the current anim state into RepAnimState — Authority context only, no-op otherwise.
	 *  Called at the TAIL of OnFlipbookChanged (the single cache funnel) and from the self-loop
	 *  confirm (KTD-9). Applies the Sequence sentinel-skip to the COUNTER before stamping. */
	void PublishAnimStateIfAuthority();

	/** Fenced server-clock access (KTD-21): the test override first, then GameState (null during
	 *  initial join / seamless travel => unset). Double — the engine API is float in 5.0 and double
	 *  in 5.7; anchors must not erode on multi-day uptimes. */
	TOptional<double> GetServerTimeSecondsForNet() const;

	/** The OnRep_AnimState decision-tree body, also driven by the stash-retry sites. */
	void ApplyReplicatedAnimState(const FPaper2DPlusRepAnimState& State);

	/** Drain PendingRepAnimState through ApplyReplicatedAnimState (may legitimately re-stash).
	 *  Called one tick AFTER the BeginPlay tail (FTSTicker one-shot deferral — see BeginPlay) and
	 *  after OnRep_CharacterProfile's generation adoption. */
	void RetryPendingRepAnimState();

	/** The single advisory broadcast site, held under bBroadcastingReplicatedAnimState. */
	void BroadcastReplicatedAnimStateAdvisory(FName MoveName, UPaperFlipbook* Flipbook, float PlaybackPosition, FName ConfirmedLabel, bool bAlreadyFinished);

	/** Resolve a profile entry by authored move name (case-insensitive, the house move-name rule). */
	const FFlipbookProfileEntry* FindProfileEntryByMoveName(const FString& MoveName) const;

	/** Archetype-mismatch diagnostic: warn once when replicated data lands while the LOCAL
	 *  bEnableReplication is false (server/client archetype disagreement). */
	void WarnOnceOnRepWithoutLocalReplication(const TCHAR* OnRepName);
	// ──────────────────────────────────────────────────────────────────

	/** Resolve caches when the flipbook changes. */
	void OnFlipbookChanged(UPaperFlipbook* NewFlipbook);

	void RefreshCachedLocalFrameState(int32 NewFrame);
	bool RefreshCachedWorldState() const;
	void MarkCachedWorldStateDirty() const;

	void QueuePendingFlipbookChange(UPaperFlipbook* NewFlipbook);
	void QueuePendingFrameChange(int32 NewFrame);
	void FlushPendingAnimationDispatch();

	/** Unified frame-driven root-motion application. bApplyWorldDelta=false (TASK-57 U8/KTD-16) advances
	 *  the baseline (LastAppliedRootMotionPos) IDENTICALLY but skips the world-offset apply — the single
	 *  source of the baseline math, used by non-authority proxies so GetRootMotionDelta stays a
	 *  meaningful per-frame advisory instead of an accumulating lie, while movement replication (not the
	 *  plugin) moves the proxy. */
	void ApplyRootMotionForFrame(int32 NewFrame, bool bLoopWrap, bool bApplyWorldDelta = true);

	/** Applies an already-computed world-space root-motion delta using the selected auto-apply mode. */
	void ApplyRootMotionWorldDelta(const FVector& WorldDelta);

	/**
	 * Pure conversion from a pixel-space trajectory delta to a world-space offset.
	 * Shared between ApplyRootMotionForFrame (auto path) and GetRootMotionDelta
	 * (manual BP query) so scale + facing-flip math lives in exactly one place.
	 */
	FVector ComputeRootMotionWorldDelta(const FVector2D& PixelDelta) const;

	/** THE single owner of the detection tick's enable + rate. Every site that wants to change either
	 *  calls this; no second copy of the policy exists. */
	void UpdateTickState();

	/** True when the CURRENT animation is actively dispatching something (root motion, Frame Cues, or
	 *  per-frame combat data) and therefore needs a full-rate tick. Deliberately NOT the input to the
	 *  fallback poll RATE — see ProfileCarriesAnyFrameCues for why that question is different. */
	bool NeedsTick() const;

	/** Resolve the profile entry for the live flipbook — the runtime-correct current-move mapping
	 *  (the GetActorCurveValue resolution chain). Null when no profile, flipbook component,
	 *  flipbook, or matching entry. When OutFramePosition is provided it receives the current
	 *  KEY-FRAME index as a float (Flipbook->GetKeyFrameIndexAtTime(GetPlaybackPosition()) — NEVER
	 *  GetPlaybackPositionInFrames, see docs/solutions/ue-paper2d-keyframe-vs-timeline-frame.md),
	 *  the position the cancel-window gate evaluates at; it is reset to 0 on any null return. */
	const FFlipbookProfileEntry* ResolveCurrentMoveEntry(float* OutFramePosition = nullptr) const;

	// ─── Server-owned combat helpers (TASK-57 U4) ──────────────────────

	/** The overlap-query indirection (TASK-57 U4): consults OverlapQueryOverrideForTests when set
	 *  (worldless seam), else routes to UPaper2DPlusHitboxSubsystem::QueryAttackOverlaps against the
	 *  owner's current server frame state. The single source of the validator's overlap results.
	 *  TASK-91 (Codex F197a): when Entry is set and SampleAttackFrame is a valid key frame OTHER than
	 *  the current cached frame (a coarse tick crossed the active window and settled elsewhere), the
	 *  real path sweeps the RESOLVED frame's attack boxes (BuildWorldAttackBoxesForFrame) through the
	 *  subsystem's explicit-boxes overload instead of the current frame's cached boxes. */
	void QueryAttackOverlapsForValidation(TArray<FHitboxCollisionResult>& OutResults,
		const FFlipbookProfileEntry* Entry = nullptr, int32 SampleAttackFrame = INDEX_NONE) const;

	/** TASK-91 (Codex F197a): build the world-space ATTACK boxes for an arbitrary key frame of Entry,
	 *  using the SAME effective-frame tier rule as FrameHasAttackHitboxEffective, the same per-frame
	 *  pivot source as RefreshCachedLocalFrameState (GetFramePivotLocal + top-left->pivot conversion),
	 *  and the same shared world-transform context as RefreshCachedWorldState — so span validation can
	 *  sample the resolved in-span attack frame's geometry when the current frame carries no boxes.
	 *  Fast-paths to the cached current-frame boxes when KeyFrame IS the cached frame. Returns false
	 *  (OutBoxes reset) when the entry/frame/flipbook component cannot resolve. */
	bool BuildWorldAttackBoxesForFrame(const FFlipbookProfileEntry* Entry, int32 KeyFrame,
		TArray<FWorldHitbox>& OutBoxes) const;

	/** The ONE world-transform context read shared by RefreshCachedWorldState and
	 *  BuildWorldAttackBoxesForFrame (TASK-91): component location, facing (ResolveFacingLeft), and
	 *  the clamped absolute X/Z scales — extracted so the frame-parameterized box build can never
	 *  diverge from the cached build's facing/scale rules. */
	static void ResolveWorldBoxTransformContext(const UPaperFlipbookComponent& FBComp,
		FVector& OutWorldPosition, bool& bOutFlipX, float& OutScaleX, float& OutScaleY);

	/** Resolve the latest ATTACK key frame inside the recorded per-tick span (LastFrameSpanBegin,
	 *  LastFrameSpanEnd]. When bRequireActive is false, returns the span-end frame unconditionally (no
	 *  attack-frame requirement). Returns INDEX_NONE when no in-span attack frame exists (and active is
	 *  required) or there is no current move. The shared sampler for ValidateAndRegisterHit's frame
	 *  check and GetCurrentHitWindowIndex's curve read so the two never disagree on which frame to use. */
	int32 ResolveLatestInSpanAttackFrame(const FFlipbookProfileEntry* Entry, bool bRequireActive) const;

	/** True when the move's frame-data marks an attack hitbox on the given key frame. */
	static bool FrameHasAttackHitbox(const FFlipbookProfileEntry* Entry, int32 KeyFrame);

	/** The TASK-74 auxiliary curve multi-hit windows index off ("HitWindow", defined in the cpp). */
	static const FName HitWindowCurveName;

	/** The shared non-authority gate for the outcome-report + hit-adjudication APIs (TASK-57 U4/KTD-18):
	 *  returns false (caller no-ops) on a NETWORKED non-authority context — AutonomousProxy/SimulatedProxy
	 *  — warning once via the supplied latch; returns true on Authority/Standalone (the existing body runs,
	 *  byte-identical in single-player). WhichApi names the API in the warning. */
	bool NotifyOutcomeAuthorityGate(const TCHAR* WhichApi, bool& bWarnedLatch);

	/** Freeze Actor through the shared refcounted hit-stop registry (first freezer captures the
	 *  TRUE prior dilation) and track it in FrozenActors. Skips null/invalid actors and actors
	 *  already tracked by THIS component's hit-stop, so one component never double-refcounts an
	 *  actor (TASK-76 PR4). */
	void FreezeActorOnce(AActor* Actor);

	/** THE shared local freeze primitive (TASK-57 U5 factoring of TASK-76 PR4's body): freeze the owner
	 *  + Victim through the refcounted registry, EXTEND (never shorten) when a freeze is already active
	 *  (no second Begin broadcast, no prior re-capture), else start the freeze + register the real-time
	 *  unfreeze ticker + broadcast OnHitStopBegin under bBroadcastingHitStop (commit-before-broadcast).
	 *  RemainingSeconds <= 0 is a no-op. Called by BOTH the local TriggerHitStop (Standalone/Authority)
	 *  AND OnRep_HitStop (proxies, with the already-clamped self-expiring Remaining). Sets bHitStopActive
	 *  so the U8 drift corrector suppresses on a proxy under a replicated freeze (IsHitStopActive covers
	 *  it). Resets ActualFrozenSeconds on a NEW freeze (the re-anchor span starts here).
	 *  bReplaceActiveFreeze (proxy receive path only, TASK-57 U5): when a genuinely NEW freeze — a
	 *  distinct wire HitStopSeq whose prior freeze's clear coalesced away — arrives while a stale local
	 *  freeze is still draining, REPLACE the remaining with the new span instead of Max-extending it, so
	 *  the proxy matches the server's CURRENT freeze. The authority/Standalone TriggerHitStop path leaves
	 *  it false, so a same-actor re-hit still EXTENDs (single-player byte-identical). */
	void BeginHitStopLocal(float RemainingSeconds, AActor* Victim, bool bReplaceActiveFreeze = false);

	/** Publish the local hit-stop state into RepHitStop — Authority context only, no-op otherwise
	 *  (Standalone publishes nothing; the local freeze is unchanged). bReplicateHitStop gates it. A
	 *  NEW freeze bumps HitStopSeq (sentinel-skip on the uint16 wrap, the FPaper2DPlusRepHitStop::
	 *  HitStopSeq contract) and stamps Duration + StartServerTime + Victim; an EXTEND keeps HitStopSeq
	 *  and updates Duration (latest-snapshot-wins); a CLEAR publishes Duration=0 on a bumped seq so the
	 *  "different => apply" receiver detects the end (TASK-57 U5/KTD-1). */
	void PublishHitStopIfAuthority(float DurationSeconds, AActor* Victim, bool bNewFreeze);

	/** Drain PendingRepHitStop through OnRep_HitStop's apply path once the server clock exists. Called
	 *  one frame after a no-clock OnRep stash (the BeginPlay-style one-shot core ticker, TASK-57 U5). */
	void RetryPendingRepHitStop();

	/** THE single hit-stop restoration path: unregister the ticker, release this component's
	 *  registry refcounts (an actor's prior dilation is restored when the LAST overlapping
	 *  hit-stop ends), clear the state, broadcast OnHitStopEnd. Timer expiry, CancelHitStop, and
	 *  EndPlay all funnel here and restoration is NEVER gated — only the End broadcast is
	 *  suppressed (logged Verbose) when ending from inside a hit-stop broadcast (TASK-76 PR4). */
	void EndHitStopInternal();
};
