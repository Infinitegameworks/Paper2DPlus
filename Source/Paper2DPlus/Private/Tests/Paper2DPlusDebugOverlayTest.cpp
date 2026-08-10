// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#if WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "Paper2DPlusDebugOverlay.h"
#include "HAL/IConsoleManager.h"

/**
 * Smoke test for the global debug overlay's console-variable registration (TASK-70).
 * Worldless: it only asserts that FPaper2DPlusDebugOverlay::Init() leaves both
 * dev-only cvars findable via IConsoleManager. The actual draw/tick path is
 * world/ticker/draw dependent and is not meaningfully worldless-testable.
 *
 * NOTE (unity-build rule): all helpers/identifiers in this file carry the
 * DebugOverlayTest_ prefix so they cannot collide with other test .cpp files
 * grouped into the same unity translation unit.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDebugOverlayTest_CVarsRegistered,
	"Paper2DPlus.DebugOverlay.CVarsRegistered",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDebugOverlayTest_CVarsRegistered::RunTest(const FString& Parameters)
{
	// Idempotent — the module already called Init() at startup; calling again is a no-op.
	FPaper2DPlusDebugOverlay::Init();

	IConsoleVariable* DebugOverlayTest_ShowHitboxes =
		IConsoleManager::Get().FindConsoleVariable(TEXT("Paper2DPlus.ShowHitboxes"));
	IConsoleVariable* DebugOverlayTest_ShowFrameData =
		IConsoleManager::Get().FindConsoleVariable(TEXT("Paper2DPlus.ShowFrameData"));
	IConsoleVariable* DebugOverlayTest_ShowAppearance =
		IConsoleManager::Get().FindConsoleVariable(TEXT("Paper2DPlus.ShowAppearance"));

	TestNotNull(TEXT("Paper2DPlus.ShowHitboxes cvar is registered"), DebugOverlayTest_ShowHitboxes);
	TestNotNull(TEXT("Paper2DPlus.ShowFrameData cvar is registered"), DebugOverlayTest_ShowFrameData);
	TestNotNull(TEXT("Paper2DPlus.ShowAppearance cvar is registered"), DebugOverlayTest_ShowAppearance);

	if (DebugOverlayTest_ShowHitboxes)
	{
		TestEqual(TEXT("Paper2DPlus.ShowHitboxes defaults to 0"), DebugOverlayTest_ShowHitboxes->GetInt(), 0);
	}
	if (DebugOverlayTest_ShowFrameData)
	{
		TestEqual(TEXT("Paper2DPlus.ShowFrameData defaults to 0"), DebugOverlayTest_ShowFrameData->GetInt(), 0);
	}
	if (DebugOverlayTest_ShowAppearance)
	{
		TestEqual(TEXT("Paper2DPlus.ShowAppearance defaults to 0"), DebugOverlayTest_ShowAppearance->GetInt(), 0);
	}

	FPaper2DPlusAppearanceStatsSnapshot Stats;
	Stats.RegisteredCount = 200;
	Stats.VisibleCount = 200;
	Stats.FarCompositeCount = 140;
	Stats.NearCompositeCount = 50;
	Stats.ChangingLiveCount = 10;
	Stats.VisiblePrimitiveCount = 370;
	Stats.CompositeQueueDepth = 8;
	Stats.CompositeWorkMillisecondsMeasuredThisFrame = 0.35;
	Stats.CompositeWorkMillisecondsGrantedPerFrame = 1.0;
	Stats.CompositeBuildUnitsClaimedThisFrame = 2;
	Stats.SynchronousLoadViolations = 0;
	const FString Aggregate = FPaper2DPlusDebugOverlay::FormatAppearanceStats(Stats);
	TestTrue(TEXT("appearance summary names the primitive gate"), Aggregate.Contains(TEXT("Prims:370")));
	TestTrue(TEXT("appearance summary names the tier distribution"), Aggregate.Contains(TEXT("140/50/10")));
	TestTrue(TEXT("appearance summary exposes queue depth"), Aggregate.Contains(TEXT("Queue:8")));
	TestTrue(TEXT("appearance summary exposes load violations"), Aggregate.Contains(TEXT("SyncLoads:0")));
	TestTrue(TEXT("appearance summary exposes measured/granted work milliseconds and actual units"),
		Aggregate.Contains(TEXT("WorkMs:0.350/1.000 U:2")));

	FPaper2DPlusAppearanceBudgetDecision Decision;
	Decision.Tier = EPaper2DPlusAppearanceTier::FarComposite;
	Decision.PendingReason = EPaper2DPlusAppearancePendingReason::WaitingForCompositeWork;
	Decision.VisiblePrimitiveCount = 1;
	Decision.bCacheHit = true;
	Decision.CacheKeyLabel = TEXT("abc123");
	const FString PerActor = FPaper2DPlusDebugOverlay::FormatAppearanceDecision(Decision);
	TestTrue(TEXT("per-actor summary exposes tier"), PerActor.Contains(TEXT("Tier:FarComposite")));
	TestTrue(TEXT("per-actor summary exposes pending reason"),
		PerActor.Contains(TEXT("Pending:WaitingForCompositeWork")));
	TestTrue(TEXT("per-actor summary exposes cache identity"),
		PerActor.Contains(TEXT("Cache:Hit")) && PerActor.Contains(TEXT("abc123")));

	return true;
}

#endif // WITH_EDITOR
