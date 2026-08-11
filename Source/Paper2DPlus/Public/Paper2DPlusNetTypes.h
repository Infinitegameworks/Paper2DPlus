// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectKey.h"
#include "Paper2DPlusAppearanceTypes.h"
#include "Paper2DPlusNetTypes.generated.h"

class AActor;

/**
 * The component's resolved network context — THE single gating seam every Paper2DPlus authority
 * decision funnels through (TASK-57 KTD-2). Standalone = replication off / no world / NM_Standalone:
 * every gate is an identity function on it, which is what keeps single-player behavior byte-identical
 * by construction. The four values deliberately do NOT encode locality — gates that need it
 * (OwnerOnly frame events) take an explicit bIsLocallyControlled input.
 */
UENUM(BlueprintType)
enum class EPaper2DPlusNetContext : uint8
{
	/** Not networked: replication disabled on the component, no world, or NM_Standalone.
	 *  Every authority gate passes unconditionally in this context — the single-player
	 *  identity guarantee. Worldless test rigs resolve here. */
	Standalone UMETA(DisplayName = "Standalone"),

	/** The server-side instance (listen or dedicated) — ROLE_Authority on a networked owner.
	 *  The only context allowed to mutate gameplay state. */
	Authority UMETA(DisplayName = "Authority"),

	/** The owning client's instance of a remotely-authoritative actor (ROLE_AutonomousProxy).
	 *  Routes intent to the server; plays owner-targeted cosmetics. */
	AutonomousProxy UMETA(DisplayName = "Autonomous Proxy"),

	/** Any other client's instance (ROLE_SimulatedProxy). Receives replicated state;
	 *  plays world-visible cosmetics only. */
	SimulatedProxy UMETA(DisplayName = "Simulated Proxy")
};

/**
 * Per-Frame-Cue network dispatch policy (TASK-57 KTD-14). Consulted by the runtime dispatch
 * predicate through Paper2DPlusNetGating::ShouldDispatchFrameCue. Any Cue that mutates gameplay state MUST be AuthorityOnly; LocalAlways is for
 * per-machine cosmetics only.
 */
UENUM(BlueprintType)
enum class EPaper2DPlusFrameCueNetPolicy : uint8
{
	/** Broadcast on every machine that advances the flipbook. Use for per-machine cosmetics only. */
	LocalAlways UMETA(DisplayName = "Local Always"),

	/** Broadcast only on authority (or standalone). Use for gameplay-mutating receivers. */
	AuthorityOnly UMETA(DisplayName = "Authority Only"),

	/** Broadcast everywhere except a dedicated server. */
	CosmeticOnly UMETA(DisplayName = "Cosmetic Only"),

	/** Broadcast only on the locally controlled owner. */
	OwnerOnly UMETA(DisplayName = "Owner Only")
};

/**
 * Deprecated Frame Event spelling retained until the cue migration replaces every call site.
 * Serialized values are byte-compatible with EPaper2DPlusFrameCueNetPolicy.
 */
UENUM(BlueprintType, meta = (Deprecated, DeprecationMessage = "Use EPaper2DPlusFrameCueNetPolicy"))
enum class EPaper2DPlusFrameEventNetPolicy : uint8
{
	/** Dispatch on every machine that plays the flipbook (the pre-networking behavior, and the
	 *  default for user subclasses). For per-machine cosmetics ONLY — a LocalAlways event that
	 *  mutates gameplay state double-fires in any networked game. */
	LocalAlways UMETA(DisplayName = "Local Always"),

	/** Dispatch only on the authority (server / standalone). REQUIRED for any event that mutates
	 *  gameplay state — projectile spawns, gameplay tags, damage triggers. */
	AuthorityOnly UMETA(DisplayName = "Authority Only"),

	/** Dispatch on every machine EXCEPT a dedicated server (which has no rendering/audio).
	 *  For world-visible cosmetics — effects, sounds, camera shakes, screen flashes. */
	CosmeticOnly UMETA(DisplayName = "Cosmetic Only"),

	/** Dispatch only on the machine that locally controls the owning pawn (the autonomous proxy,
	 *  or a listen server's locally-controlled pawn). For owner-targeted feedback — HUD pings,
	 *  controller rumble, first-person-only flourishes. */
	OwnerOnly UMETA(DisplayName = "Owner Only")
};

/**
 * The replicated animation/move-state snapshot published by the authority from the
 * OnFlipbookChanged cache funnel (TASK-57 U2; declared in U1 so the whole type surface lands
 * together). Sequence-numbered ("different => apply", 0 = never-published, authority starts at 1)
 * and server-time-anchored so late join / relevancy regain / packet loss all resolve to the same
 * apply path.
 *
 * Replication-only payload: members are plain UPROPERTY() with NO Blueprint exposure — uint16 and
 * bitfields are not BP-exposable under the installed-engine UHT rules (5.0-5.7), and games consume
 * this through the advisory delegate, never the raw struct.
 */
USTRUCT()
struct PAPER2DPLUS_API FPaper2DPlusRepAnimState
{
	GENERATED_BODY()

	/** Publish counter. 0 = never published — the sentinel is EXCLUDED from the live range
	 *  (ADV-2/U1-NET-001). Producer contract at every publish:
	 *      uint16 Seq = uint16(MoveInstanceCounter);
	 *      if (Seq == 0) { ++MoveInstanceCounter; Seq = 1; }
	 *  A raw uint16 cast collides with the sentinel every 65,536th move instance, and that publish
	 *  would be eternally dropped (ShouldApplySequence refuses 0). The COUNTER itself is bumped —
	 *  not just the wire stamp — so U3's `ClientAnimSeq != uint16(MoveInstanceCounter)` stale check
	 *  stays aligned with the wire value. Consumers apply on "different", never on ">" — wraparound
	 *  and relevancy gaps are handled by identity, not ordering. */
	UPROPERTY()
	uint16 Sequence = 0;

	/** Profile generation pairing (KTD-6): mirrors the authority's RepProfileSeq at publish time.
	 *  A mismatch on the client means the paired profile OnRep has not landed yet — stash and
	 *  re-apply after it does. */
	UPROPERTY()
	uint16 ProfileSeq = 0;

	/** Authored FlipbookName of the move on the profile asset (resolved client-side against the
	 *  replicated profile). Meaningless when bIsProfileMove is false. */
	UPROPERTY()
	FName MoveName;

	/** Server-clock anchor: the server time at which playback position 0 of this state occurred
	 *  (ServerNow - CurrentPos/PlayRate at publish). Double per KTD-21 — the engine server-time API
	 *  is float in 5.0 and double in 5.7, and double anchors don't erode on multi-day dedicated
	 *  uptimes. -1 = no anchor yet (GameState unavailable at publish; re-anchored later). */
	UPROPERTY()
	double StartServerTime = -1.0;

	/** Playback rate at publish time (KTD-4 — the stock flipbook component does NOT replicate it). */
	UPROPERTY()
	float PlayRate = 1.0f;

	/** Looping flag at publish time (not replicated by the stock component). */
	UPROPERTY()
	uint8 bLooping : 1;

	/** Reverse-playback flag at publish time (not replicated by the stock component). */
	UPROPERTY()
	uint8 bReverse : 1;

	/** Facing flip at publish time — hitbox mirroring and root-motion flip depend on it (KTD-4). */
	UPROPERTY()
	uint8 bFlipX : 1;

	/** True when the playing flipbook resolves to a profile entry (a MOVE). False = an authoritative
	 *  "no active move" CLEAR (locomotion — the game's/PaperZD's domain, A2). */
	UPROPERTY()
	uint8 bIsProfileMove : 1;

	/** RESERVED prediction seam (A1/KTD-3): echo of the owning client's request id. Unused in v1. */
	UPROPERTY()
	uint16 LastRequestId = 0;

	FPaper2DPlusRepAnimState()
		: bLooping(false)
		, bReverse(false)
		, bFlipX(false)
		, bIsProfileMove(false)
	{
	}
};

/**
 * Self-expiring replicated hit-stop snapshot (TASK-57 KTD-1/KTD-21; consumed in U5). Duration +
 * server-time anchor instead of paired Begin/End events makes relevancy-drop stranding structurally
 * impossible — a late receiver computes the clamped remaining freeze locally. Replication-only
 * payload (plain UPROPERTY(), no BP exposure).
 */
USTRUCT()
struct PAPER2DPLUS_API FPaper2DPlusRepHitStop
{
	GENERATED_BODY()

	/** Bumped on each NEW freeze; stable while an active freeze is EXTENDED (latest-snapshot-wins).
	 *  0 = never-published sentinel, EXCLUDED from the live range: the producer skips 0 on
	 *  wraparound (the same skip-0 contract as FPaper2DPlusRepAnimState::Sequence — a raw cast
	 *  that lands on 0 would be eternally dropped by the "different => apply" consumer). */
	UPROPERTY()
	uint16 HitStopSeq = 0;

	/** Total freeze length in REAL seconds. 0 = no freeze / explicit clear (EndPlay publishes 0). */
	UPROPERTY()
	float DurationSeconds = 0.0f;

	/** Server time the freeze began. Double per KTD-21 (see FPaper2DPlusRepAnimState::StartServerTime).
	 *  Receivers clamp remaining = Duration - (ServerNow - StartServerTime) to [0, Duration]. */
	UPROPERTY()
	double StartServerTime = -1.0;

	/** The frozen victim, when the attacker's freeze included one. Null is tolerated
	 *  (attacker-only freeze); victim-side freezes publish on the victim's OWN component (KTD-24). */
	UPROPERTY()
	TObjectPtr<AActor> Victim = nullptr;
};

/**
 * Replicated committed appearance envelope. It contains one current generic descriptor plus a sequence.
 * Previews / manual visibility / transient recolor stay local by construction — they never reach
 * the publish chokepoints. Replication-only payload (plain UPROPERTY(), no BP exposure).
 */
USTRUCT()
struct PAPER2DPLUS_API FPaper2DPlusRepAppearanceState
{
	GENERATED_BODY()

	/** Publish counter — bumped only when the committed snapshot actually CHANGED (no-op
	 *  republishes must not stomp client-local previews). 0 = never published. */
	UPROPERTY()
	uint16 Sequence = 0;

	/** IDs/version/delivery intent only; never derived render/cache/tier state. */
	UPROPERTY()
	FPaper2DPlusAppearanceDescriptor Appearance;

	/** Current generic envelope version. */
	UPROPERTY()
	uint8 PayloadVersion = 1;
};

/**
 * Result of a server-side hit validation query (TASK-57 R3/U4, declared in U1). BP-facing — games
 * read it to apply damage on authority. bValidHit is true ONLY when the server's own re-query
 * resolved to the PASSED victim's actor, and every populated field comes FROM the matched overlap,
 * never from caller input (KTD-17).
 */
USTRUCT(BlueprintType)
struct PAPER2DPLUS_API FPaper2DPlusHitValidationResult
{
	GENERATED_BODY()

	/** True when the server re-query confirmed an attack overlap resolving to the passed victim. */
	UPROPERTY(BlueprintReadOnly, Category = "Paper2DPlus|Networking")
	bool bValidHit = false;

	/** True when the (attacker, victim, move instance, hit window) tuple was already registered —
	 *  the hit is real but must not apply damage twice. */
	UPROPERTY(BlueprintReadOnly, Category = "Paper2DPlus|Networking")
	bool bDuplicate = false;

	/** The validated victim actor (the matched overlap's actor; null when bValidHit is false). */
	UPROPERTY(BlueprintReadOnly, Category = "Paper2DPlus|Networking")
	TObjectPtr<AActor> Victim = nullptr;

	/** Authored name of the attacking move on the server at validation time. */
	UPROPERTY(BlueprintReadOnly, Category = "Paper2DPlus|Networking")
	FString MoveName;

	/** Server key-frame index the matched attack frame resolved at (span-based — may be earlier in
	 *  the last tick's frame span than the current frame). INDEX_NONE when invalid. */
	UPROPERTY(BlueprintReadOnly, Category = "Paper2DPlus|Networking")
	int32 FrameIndex = INDEX_NONE;

	/** Resolved hit-window index ("HitWindow" aux curve; 0 when the curve is absent).
	 *  INDEX_NONE when invalid. */
	UPROPERTY(BlueprintReadOnly, Category = "Paper2DPlus|Networking")
	int32 HitWindowIndex = INDEX_NONE;

	/** Damage authored on the matched attack hitbox (server data, never caller input). */
	UPROPERTY(BlueprintReadOnly, Category = "Paper2DPlus|Networking")
	float Damage = 0.f;

	/** Knockback authored on the matched attack hitbox (server data, never caller input). */
	UPROPERTY(BlueprintReadOnly, Category = "Paper2DPlus|Networking")
	float Knockback = 0.f;

	/** RESERVED lag-compensation seam: the attacker's MoveInstanceCounter at validation. */
	UPROPERTY(BlueprintReadOnly, Category = "Paper2DPlus|Networking")
	int32 MoveInstanceSeq = 0;

	/** RESERVED lag-compensation seam: server time at validation. -1 until populated. */
	UPROPERTY(BlueprintReadOnly, Category = "Paper2DPlus|Networking")
	double ServerTimestamp = -1.0;
};

/**
 * Typed hit-dedup key (TASK-57 KTD-17): once per (attacker, victim, move instance, hit window).
 * Plain struct, NOT a USTRUCT — FObjectKey members are not reflectable, and the set is transient
 * component state that never serializes or replicates.
 */
struct FPaper2DPlusHitDedupKey
{
	FObjectKey Attacker;
	FObjectKey Victim;
	uint32 MoveInstance = 0;
	int32 HitWindowIndex = INDEX_NONE;

	FPaper2DPlusHitDedupKey() = default;
	FPaper2DPlusHitDedupKey(const UObject* InAttacker, const UObject* InVictim, uint32 InMoveInstance, int32 InHitWindowIndex)
		: Attacker(InAttacker)
		, Victim(InVictim)
		, MoveInstance(InMoveInstance)
		, HitWindowIndex(InHitWindowIndex)
	{
	}

	bool operator==(const FPaper2DPlusHitDedupKey& Other) const
	{
		return Attacker == Other.Attacker
			&& Victim == Other.Victim
			&& MoveInstance == Other.MoveInstance
			&& HitWindowIndex == Other.HitWindowIndex;
	}

	bool operator!=(const FPaper2DPlusHitDedupKey& Other) const
	{
		return !(*this == Other);
	}

	friend uint32 GetTypeHash(const FPaper2DPlusHitDedupKey& Key)
	{
		uint32 Hash = GetTypeHash(Key.Attacker);
		Hash = HashCombine(Hash, GetTypeHash(Key.Victim));
		Hash = HashCombine(Hash, GetTypeHash(Key.MoveInstance));
		Hash = HashCombine(Hash, GetTypeHash(Key.HitWindowIndex));
		return Hash;
	}
};
