// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Paper2DPlusNetTypes.h"
#include "Paper2DPlusMoveTransition.h"

/**
 * Paper2DPlusNetGating — THE single, pure, worldless-testable authority seam (TASK-57 KTD-2).
 * Every networking authority decision in the runtime module funnels through these statics (the
 * AnimationMapCore pure-seam precedent): no UWorld, no component state, no side effects. The
 * component resolves its EPaper2DPlusNetContext (plus locality / dedicated-server / server-time
 * inputs) and passes them in, so the whole gating matrix is unit-testable without a world.
 *
 * Standalone is the identity context: every gate passes for it unconditionally — the
 * single-player byte-identical guarantee (C1).
 */
namespace Paper2DPlusNetGating
{
	/**
	 * The Frame Cue dispatch gate (KTD-14/KTD-15) — the policy x context matrix:
	 *
	 *   Policy \ Context | Standalone | Authority (listen) | Authority (dedicated) | AutonomousProxy | SimulatedProxy
	 *   LocalAlways      |     Y      |         Y          |           Y           |        Y        |       Y
	 *   AuthorityOnly    |     Y      |         Y          |           Y           |        N        |       N
	 *   CosmeticOnly     |     Y      |         Y          |     N (no rendering)  |        Y        |       Y
	 *   OwnerOnly        |     Y      | Y if loc.controlled|           N           |        Y        |       N
	 *
	 * bIsLocallyControlled is REQUIRED because the 4-value context enum cannot encode locality —
	 * the OwnerOnly row distinguishes a locally-controlled listen-server pawn from a dedicated /
	 * remote one. The component resolves it via the owner pawn's IsLocallyControlled() (false when
	 * there is no world or no pawn).
	 *
	 * Catch-up (bIsCatchUp=true; late join, profile-OnRep re-warm, non-funnel flipbook changes on
	 * non-authority): the CALLER never dispatches one-shots on catch-up at all (KTD-15) — this
	 * function's catch-up semantics cover the ranged-Begin rebuild: false for every policy EXCEPT
	 * LocalAlways / CosmeticOnly (and OwnerOnly on the owner), each still subject to its base
	 * matrix row. Standalone returns true ALWAYS, regardless of every other argument.
	 */
	PAPER2DPLUS_API bool ShouldDispatchFrameCue(
		EPaper2DPlusFrameCueNetPolicy Policy,
		EPaper2DPlusNetContext Context,
		bool bDedicatedServer,
		bool bIsLocallyControlled,
		bool bIsCatchUp);

	/** Root-motion world-offset gate (KTD-16): only Standalone and Authority apply world deltas.
	 *  Proxies advance baselines without moving the actor (movement replication owns proxy motion). */
	PAPER2DPLUS_API bool ShouldApplyRootMotion(EPaper2DPlusNetContext Context);

	/** Automatic hit detection gate (TASK-145): arming, detection passes, and every auto-hit
	 *  broadcast (window edges, hits, whiffs) run where hit adjudication runs — Standalone and
	 *  Authority. Proxies never arm; cosmetic reactions ride Frame Cues or the game's own
	 *  replication of the authoritative outcome (authority-contract.md). */
	PAPER2DPLUS_API bool ShouldRunAutoHitDetection(EPaper2DPlusNetContext Context);

	/** Sequence-application rule (KTD-5): "different => apply". Incoming 0 is the never-published
	 *  sentinel and never applies. No ordering comparison — wraparound (65535 -> 3) and large
	 *  relevancy gaps apply by identity, not by ">". */
	PAPER2DPLUS_API bool ShouldApplySequence(uint16 Incoming, uint16 LastApplied);

	/** Current appearance envelope payload; future payloads fail closed until explicitly supported. */
	PAPER2DPLUS_API bool IsSupportedAppearancePayload(uint8 PayloadVersion);

	/** Sequence + payload compatibility gate for the appearance OnRep path. */
	PAPER2DPLUS_API bool ShouldApplyAppearanceSnapshot(
		uint16 IncomingSequence,
		uint16 LastAppliedSequence,
		uint8 PayloadVersion);

	/**
	 * Derive the local playback position from a server-time anchor (KTD-19/KTD-20).
	 * Elapsed playback seconds = (ServerNow - StartServerTime) * PlayRate, clamped to 0 when
	 * negative (clock skew / anchor from the future). Looping = fmod into [0, Length);
	 * non-looping clamps to Length and reports bOutAlreadyFinished. Length <= 0 returns 0
	 * (finished when non-looping). Returns float — flipbook playback positions are float across
	 * 5.0-5.7; the double anchor math happens here, once, with an explicit narrowing cast.
	 * The [0, Length) loop promise (and the non-loop "< Length unless finished" promise) is
	 * enforced AFTER the cast: float narrowing of a double just under Length can round up to
	 * exactly Length, so post-cast guards coerce loop results to 0 and report the non-loop case
	 * as finished at Length (U1-NET-002).
	 */
	PAPER2DPLUS_API float ComputeReplicatedPlaybackPosition(
		double StartServerTime,
		double ServerNow,
		float PlayRate,
		float Length,
		bool bLooping,
		bool& bOutAlreadyFinished);
}
