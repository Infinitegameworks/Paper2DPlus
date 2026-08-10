// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Paper2DPlusAppearanceBudget.h"

/**
 * FPaper2DPlusDebugOverlay — global, no-per-actor-component debug overlay for
 * all Paper2DPlus actors, driven entirely from the console.
 *
 * Registers two dev-only console variables in Init() (called from the module's
 * StartupModule):
 *   Paper2DPlus.ShowHitboxes  (int, 0/1) — draws each P2DP actor's current-frame
 *                              attack/hurtboxes + sockets via the existing
 *                              UPaper2DPlusBlueprintLibrary::DrawActorDebugHitboxes
 *                              path (pivot-correct world conversion, not reimplemented).
 *   Paper2DPlus.ShowFrameData (int, 0/1) — draws "<Flipbook> Frame:<i>/<n>"
 *                              above each P2DP actor.
 *   Paper2DPlus.ShowAppearance(int, 0/1) — draws aggregate tier, primitive, queue, cache, build,
 *                              synchronous-load, and replicated-byte pressure for the active world.
 *
 * A single core ticker iterates UPaper2DPlusCharacterProfileComponent instances in
 * the active PIE/Game world each frame. When BOTH cvars are 0 the ticker early-outs
 * with zero iteration cost. The whole feature compiles out of Shipping builds.
 */
class FPaper2DPlusDebugOverlay
{
public:
	/** Register the console variables + the per-frame ticker. Idempotent. */
	static void Init();

	/** Unregister the ticker (and leave the cvars; they are harmless). Idempotent. */
	static void Shutdown();

	/** Stable one-line aggregate used by the live overlay, tests, and captured profiling evidence. */
	static FString FormatAppearanceStats(const FPaper2DPlusAppearanceStatsSnapshot& Stats);

	/** Stable per-component line used by the live overlay once a renderer submits a tier decision. */
	static FString FormatAppearanceDecision(const FPaper2DPlusAppearanceBudgetDecision& Decision);
};
