// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "Paper2DPlusNetGating.h"

namespace Paper2DPlusNetGating
{

bool ShouldDispatchFrameCue(
	EPaper2DPlusFrameCueNetPolicy Policy,
	EPaper2DPlusNetContext Context,
	bool bDedicatedServer,
	bool bIsLocallyControlled,
	bool bIsCatchUp)
{
	// Standalone is the identity context (C1): constant-true regardless of EVERY other argument —
	// the 312-test single-player guarantee. Catch-up never reaches single-player dispatch.
	if (Context == EPaper2DPlusNetContext::Standalone)
	{
		return true;
	}

	// Base matrix row (see the header table).
	bool bBasePass = false;
	switch (Policy)
	{
	case EPaper2DPlusFrameCueNetPolicy::LocalAlways:
		bBasePass = true;
		break;

	case EPaper2DPlusFrameCueNetPolicy::AuthorityOnly:
		bBasePass = (Context == EPaper2DPlusNetContext::Authority);
		break;

	case EPaper2DPlusFrameCueNetPolicy::CosmeticOnly:
		// A dedicated server has no rendering/audio — cosmetics are skipped on that process.
		bBasePass = !bDedicatedServer;
		break;

	case EPaper2DPlusFrameCueNetPolicy::OwnerOnly:
		// "On the owner": the autonomous proxy, or a listen server's locally-controlled pawn.
		// A dedicated server never locally controls a pawn; simulated proxies are never the owner.
		bBasePass = !bDedicatedServer
			&& ((Context == EPaper2DPlusNetContext::Authority && bIsLocallyControlled)
				|| Context == EPaper2DPlusNetContext::AutonomousProxy);
		break;
	}

	if (!bBasePass)
	{
		return false;
	}

	// Catch-up (KTD-15): the caller suppresses one-shots entirely; this gate covers the ranged-Begin
	// rebuild — cosmetic-grade policies only. AuthorityOnly never fires Begin on catch-up (its
	// gameplay side effects already happened on the live authority timeline).
	if (bIsCatchUp && Policy == EPaper2DPlusFrameCueNetPolicy::AuthorityOnly)
	{
		return false;
	}

	return true;
}

bool ShouldApplyRootMotion(EPaper2DPlusNetContext Context)
{
	return Context == EPaper2DPlusNetContext::Standalone
		|| Context == EPaper2DPlusNetContext::Authority;
}

bool ShouldRunAutoHitDetection(EPaper2DPlusNetContext Context)
{
	// Auto hit detection is gameplay adjudication (TASK-145): it runs exactly where
	// ValidateAndRegisterHit is allowed to run.
	return Context == EPaper2DPlusNetContext::Standalone
		|| Context == EPaper2DPlusNetContext::Authority;
}

bool ShouldApplySequence(uint16 Incoming, uint16 LastApplied)
{
	// 0 = never-published sentinel (authority starts at 1): nothing to apply.
	if (Incoming == 0)
	{
		return false;
	}
	// "Different => apply" — identity, never ordering, so wraparound and relevancy gaps just work.
	return Incoming != LastApplied;
}

bool IsSupportedAppearancePayload(uint8 PayloadVersion)
{
	return PayloadVersion == 1;
}

bool ShouldApplyAppearanceSnapshot(
	uint16 IncomingSequence,
	uint16 LastAppliedSequence,
	uint8 PayloadVersion)
{
	return IsSupportedAppearancePayload(PayloadVersion)
		&& ShouldApplySequence(IncomingSequence, LastAppliedSequence);
}

float ComputeReplicatedPlaybackPosition(
	double StartServerTime,
	double ServerNow,
	float PlayRate,
	float Length,
	bool bLooping,
	bool& bOutAlreadyFinished)
{
	bOutAlreadyFinished = false;

	if (Length <= 0.0f)
	{
		bOutAlreadyFinished = !bLooping;
		return 0.0f;
	}

	// Elapsed PLAYBACK seconds (wall seconds scaled by rate). Negative (clock skew / future
	// anchor) clamps to the move's start rather than extrapolating backwards.
	const double Elapsed = (ServerNow - StartServerTime) * static_cast<double>(PlayRate);
	if (Elapsed <= 0.0)
	{
		return 0.0f;
	}

	if (bLooping)
	{
		// The double fmod is strictly < Length, but the float narrowing can round UP to exactly
		// Length (U1-NET-002) — coerce back into the promised [0, Length) (== Length is frame 0).
		float Result = static_cast<float>(FMath::Fmod(Elapsed, static_cast<double>(Length)));
		if (Result >= Length)
		{
			Result = 0.0f;
		}
		return Result;
	}

	if (Elapsed >= static_cast<double>(Length))
	{
		bOutAlreadyFinished = true;
		return Length;
	}

	// Elapsed < Length held in double, but the float narrowing can round UP to exactly Length
	// (U1-NET-002) — report the same finished end-state the >= branch above would have.
	const float Result = static_cast<float>(Elapsed);
	if (Result >= Length)
	{
		bOutAlreadyFinished = true;
		return Length;
	}
	return Result;
}

} // namespace Paper2DPlusNetGating
