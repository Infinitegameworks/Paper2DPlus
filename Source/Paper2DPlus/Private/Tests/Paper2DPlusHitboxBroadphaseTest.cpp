// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#if WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "Paper2DPlusHitboxSubsystem.h"

/**
 * Audit F3 — broadphase rebuild GATING. The fix adds a same-frame transform-drift trigger so an
 * authority hit can't depend on query order (a defender that moves after the per-frame index built must
 * force a rebuild). This worldless test pins the rebuild-gating scaffolding + the no-needless-rebuild
 * contract via the test seams: a fresh index builds once; a second same-frame query with NO change does
 * NOT rebuild (an empty index has no drift); an explicit dirty rebuilds.
 *
 * The full same-frame-MOVE-into-range integration (scenarios needing registered, flipbook-backed
 * components with valid cached world hurtboxes + a UWorld + controllable transforms) is exercised by the
 * manual PIE matrix in Plugins/Paper2DPlus/docs/authority-contract.md, since the broadphase is a
 * UWorldSubsystem and the worldless harness cannot construct flipbook-backed hurtbox caches.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusHitboxBroadphaseRebuildGating,
	"Paper2DPlus.Hitbox.BroadphaseRebuildGating",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusHitboxBroadphaseRebuildGating::RunTest(const FString& Parameters)
{
	UPaper2DPlusHitboxSubsystem* Subsystem = NewObject<UPaper2DPlusHitboxSubsystem>();

	// First query builds the index (starts dirty).
	Subsystem->Test_BuildIndexIfNeeded();
	TestEqual(TEXT("First build occurs"), Subsystem->Test_IndexRebuildCount, 1);

	// Same frame, nothing registered/moved -> the new drift clause must NOT cause a spurious rebuild.
	Subsystem->Test_BuildIndexIfNeeded();
	Subsystem->Test_BuildIndexIfNeeded();
	TestEqual(TEXT("No needless rebuild without a frame/transform change"), Subsystem->Test_IndexRebuildCount, 1);

	// An explicit dirty (registration/unregistration path) forces a rebuild.
	Subsystem->MarkIndexDirty();
	Subsystem->Test_BuildIndexIfNeeded();
	TestEqual(TEXT("Dirty forces a rebuild"), Subsystem->Test_IndexRebuildCount, 2);

	// And only once for the dirty.
	Subsystem->Test_BuildIndexIfNeeded();
	TestEqual(TEXT("Cleared dirty does not keep rebuilding"), Subsystem->Test_IndexRebuildCount, 2);

	return true;
}

#endif // WITH_EDITOR
