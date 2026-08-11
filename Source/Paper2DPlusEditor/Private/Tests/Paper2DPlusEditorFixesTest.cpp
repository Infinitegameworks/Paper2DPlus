// Copyright 2026 Infinite Gameworks. All Rights Reserved.
//
// Regression tests for focused editor fixes. Most exercise pure-logic cores extracted
// from Slate event handlers; a few construct lightweight widgets when that is the
// smallest reliable seam:
//   U9 — HitboxCanvasUtils::ClampHitboxPositionToBounds (hitbox move-clamp inversion)
//   U8 — FSpriteExtractionUtils::FilterValidSpriteIndices (merge stale-index validation)
//   U7 — FSpriteExtractionUtils::GetOrCreateTextureForName (NewObject name-collision guard)
//
// Editor-module test TU (these are Paper2DPlusEditor symbols; the runtime test module
// cannot depend on the editor module — same rule as Paper2DPlusCrossSheetAlignmentTest.cpp).

#if WITH_EDITOR

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"
#include "UObject/Package.h"
#include "UObject/ObjectRedirector.h"
#include "Engine/Texture2D.h"
#include "CharacterProfileAssetEditor.h"   // HitboxCanvasUtils::ClampHitboxPositionToBounds (U9)
#include "SpriteExtractionUtils.h"          // FSpriteExtractionUtils helpers (U7, U8)
#include "CharacterProfileEditorModel.h"    // FCharacterProfileEditorModel (WS1 layer-editor tabs)
#include "RootMotionEditor.h"               // SRootMotionEditor construction selection regression
#include "FrameTimingEditor.h"              // SFrameTimingEditor construction selection regression
#include "SpriteEditorPanel.h"              // SpriteEditorPanelUtils::PrepareFrameStripFrameCount
#include "Paper2DPlusCharacterProfileAsset.h"
#include "PaperFlipbook.h"
#include "PaperSprite.h"
#include "Editor.h"

namespace
{
UPaperFlipbook* MakeEditorFixesFlipbook(UObject* Outer, int32 NumKeyFrames)
{
	UPaperFlipbook* Flipbook = NewObject<UPaperFlipbook>(Outer);
	FScopedFlipbookMutator Mutator(Flipbook);
	Mutator.KeyFrames.Empty();

	for (int32 Index = 0; Index < NumKeyFrames; ++Index)
	{
		FPaperFlipbookKeyFrame KeyFrame;
		KeyFrame.FrameRun = 1;
		KeyFrame.Sprite = NewObject<UPaperSprite>(Flipbook);
		Mutator.KeyFrames.Add(KeyFrame);
	}

	return Flipbook;
}
}

// =============================================================================
// U9 — Hitbox move-clamp non-negative upper bound.
// Pre-fix, an oversized hitbox (W > MaxX) made the clamp upper bound (MaxX - W)
// negative, inverting FMath::Clamp and pinning the hitbox to a negative coord.
// =============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusHitboxMoveClampNonNegativeUpperBound,
	"Paper2DPlus.HitboxCanvas.MoveClamp.NonNegativeUpperBound",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusHitboxMoveClampNonNegativeUpperBound::RunTest(const FString& Parameters)
{
	using HitboxCanvasUtils::ClampHitboxPositionToBounds;

	// Oversized (W > MaxX, H > MaxY) pins at 0, NOT negative. Pre-fix: (MaxX-W, MaxY-H) = (-36, -36).
	{
		const FIntPoint R = ClampHitboxPositionToBounds(/*X*/10, /*Y*/10, /*W*/100, /*H*/100, /*MaxX*/64, /*MaxY*/64);
		TestEqual(TEXT("Oversized pins X at 0 (not negative)"), R.X, 0);
		TestEqual(TEXT("Oversized pins Y at 0 (not negative)"), R.Y, 0);
	}
	// Oversized + dragged far positive still pins at 0.
	{
		const FIntPoint R = ClampHitboxPositionToBounds(500, 500, 100, 100, 64, 64);
		TestEqual(TEXT("Oversized dragged positive pins X at 0"), R.X, 0);
		TestEqual(TEXT("Oversized dragged positive pins Y at 0"), R.Y, 0);
	}
	// Oversized + dragged negative also pins at 0.
	{
		const FIntPoint R = ClampHitboxPositionToBounds(-500, -500, 100, 100, 64, 64);
		TestEqual(TEXT("Oversized dragged negative pins X at 0"), R.X, 0);
		TestEqual(TEXT("Oversized dragged negative pins Y at 0"), R.Y, 0);
	}
	// Normal (W <= MaxX): clamps to [0, MaxX - W]. 20-wide in 64 => valid X range [0,44].
	{
		const FIntPoint Past = ClampHitboxPositionToBounds(200, 200, 20, 20, 64, 64);
		TestEqual(TEXT("Normal past-right clamps X to MaxX-W (44)"), Past.X, 44);
		TestEqual(TEXT("Normal past-bottom clamps Y to MaxY-H (44)"), Past.Y, 44);

		const FIntPoint Neg = ClampHitboxPositionToBounds(-50, -50, 20, 20, 64, 64);
		TestEqual(TEXT("Normal past-left clamps X to 0"), Neg.X, 0);
		TestEqual(TEXT("Normal past-top clamps Y to 0"), Neg.Y, 0);

		const FIntPoint In = ClampHitboxPositionToBounds(30, 12, 20, 20, 64, 64);
		TestEqual(TEXT("Normal in-range X unchanged"), In.X, 30);
		TestEqual(TEXT("Normal in-range Y unchanged"), In.Y, 12);
	}
	// Exact-fit (W == MaxX): only valid position is 0.
	{
		const FIntPoint R = ClampHitboxPositionToBounds(200, 200, 64, 64, 64, 64);
		TestEqual(TEXT("Exact-fit clamps X to 0"), R.X, 0);
		TestEqual(TEXT("Exact-fit clamps Y to 0"), R.Y, 0);
	}
	// Mixed axis: normal X, oversized Y in the same call.
	{
		const FIntPoint R = ClampHitboxPositionToBounds(200, 200, 20, 100, 64, 64);
		TestEqual(TEXT("Mixed: normal X clamps to MaxX-W (44)"), R.X, 44);
		TestEqual(TEXT("Mixed: oversized Y pins at 0"), R.Y, 0);
	}

	return true;
}

// =============================================================================
// U8 — MergeSelectedSprites stale-index validation.
// FilterValidSpriteIndices drops indices that went stale after the sprite array
// was re-indexed/shrunk; callers treat < 2 survivors as "nothing to merge".
// =============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFilterValidSpriteIndices,
	"Paper2DPlus.SpriteExtractor.Merge.FilterValidSpriteIndices",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFilterValidSpriteIndices::RunTest(const FString& Parameters)
{
	// Stale index (99) dropped from a 3-sprite array; survivors keep order, >= 2 => merge proceeds.
	{
		const TArray<int32> Out = FSpriteExtractionUtils::FilterValidSpriteIndices({ 0, 1, 99 }, 3);
		TestEqual(TEXT("Stale 99 dropped -> 2 survivors"), Out.Num(), 2);
		if (Out.Num() == 2)
		{
			TestEqual(TEXT("Survivor[0] preserved"), Out[0], 0);
			TestEqual(TEXT("Survivor[1] preserved"), Out[1], 1);
		}
	}
	// Only one valid index => fewer than 2 => caller bails (no merge, no PushUndoState).
	{
		const TArray<int32> Out = FSpriteExtractionUtils::FilterValidSpriteIndices({ 0, 99 }, 3);
		TestTrue(TEXT("[0,99] vs 3 yields < 2 survivors"), Out.Num() < 2);
	}
	// All indices stale (array shrank to 3) => empty => bail.
	{
		const TArray<int32> Out = FSpriteExtractionUtils::FilterValidSpriteIndices({ 5, 6, 7 }, 3);
		TestEqual(TEXT("All-stale -> empty"), Out.Num(), 0);
	}
	// Edge cases never crash and never reach a 2+ result spuriously.
	{
		TestEqual(TEXT("Empty in -> empty out"),
			FSpriteExtractionUtils::FilterValidSpriteIndices(TArray<int32>{}, 3).Num(), 0);
		TestEqual(TEXT("Single valid index -> 1 survivor"),
			FSpriteExtractionUtils::FilterValidSpriteIndices({ 1 }, 3).Num(), 1);
		TestEqual(TEXT("Any indices vs Count 0 -> empty"),
			FSpriteExtractionUtils::FilterValidSpriteIndices({ 0, 1 }, 0).Num(), 0);
	}
	// Negative index is out of range and dropped (IsValidIndex semantics).
	{
		const TArray<int32> Out = FSpriteExtractionUtils::FilterValidSpriteIndices({ -1, 0, 1 }, 3);
		TestEqual(TEXT("Negative dropped -> 2 survivors"), Out.Num(), 2);
	}

	return true;
}

// =============================================================================
// U7 — GetOrCreateTextureForName NewObject name-collision guard.
// Reuse same-class in place; evict a different-class occupant to transient before
// NewObject; never crash; idempotent.
// =============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusGetOrCreateTextureForName,
	"Paper2DPlus.SpriteExtraction.GetOrCreateTextureForName.CollisionGuard",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusGetOrCreateTextureForName::RunTest(const FString& Parameters)
{
	// Unique in-memory package so reruns don't collide with each other.
	const FString PkgName = FString::Printf(TEXT("/Temp/Paper2DPlusU7Test_%s"), *FGuid::NewGuid().ToString());
	UPackage* Package = CreatePackage(*PkgName);
	TestNotNull(TEXT("Test package created"), Package);
	if (!Package) { return false; }

	const FString TargetName = TEXT("Combined_Sheet");

	// Plant a stray DIFFERENT-class occupant of the target name — a UObjectRedirector is the exact
	// real-world case the fix guards against (a leftover redirector after the prior _Sheet was renamed).
	UObjectRedirector* Stray = NewObject<UObjectRedirector>(Package, *TargetName, RF_Public | RF_Standalone);
	TestNotNull(TEXT("Stray occupant created"), Stray);
	if (!Stray) { return false; }
	TestTrue(TEXT("Stray initially outered to test package"), Stray->GetOuter() == Package);

	// First call: must evict the stray and return a real UTexture2D of the requested name.
	UTexture2D* Created = FSpriteExtractionUtils::GetOrCreateTextureForName(Package, TargetName);
	TestNotNull(TEXT("Helper returns non-null on collision (no crash, no null)"), Created);
	if (!Created) { return false; }
	TestEqual(TEXT("Created texture has requested name"), Created->GetName(), TargetName);
	TestTrue(TEXT("Created texture outered to package"), Created->GetOuter() == Package);

	// The same-name different-class occupant was evicted out of the package.
	TestTrue(TEXT("Stray evicted off the test package"), Stray->GetOuter() != Package);
	UObject* NowAtName = StaticFindObject(UObject::StaticClass(), Package, *TargetName);
	TestTrue(TEXT("The object now at that name IS the created texture"), NowAtName == Created);

	// Idempotent reuse-in-place: a second call returns the SAME texture.
	UTexture2D* Again = FSpriteExtractionUtils::GetOrCreateTextureForName(Package, TargetName);
	TestTrue(TEXT("Second call reuses the same texture in place"), Again == Created);

	// Null package guard.
	TestNull(TEXT("Null package returns null"),
		FSpriteExtractionUtils::GetOrCreateTextureForName(nullptr, TargetName));

	return true;
}

// =============================================================================
// FSpriteSheetGrid — dimension-aware sprite-sheet packing (TASK-60 AC#7 / Codex #111).
// Replaces the duplicated "<=16 -> single row, else ceil(sqrt)" rule across the Aseprite
// import/reimport sites. Must stay byte-identical for sheets that already fit, and pack
// (not over-reject) wide sheets that overflow a single row but fit a 2D grid.
// =============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusSpriteSheetGridLayout,
	"Paper2DPlus.Aseprite.SpriteSheetGrid.Layout",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusSpriteSheetGridLayout::RunTest(const FString& Parameters)
{
	// The legacy formula the 5 sites used before extraction — kept here as the regression oracle.
	auto LegacyCols = [](int32 FrameCount) -> int32
	{
		return (FrameCount <= 16) ? FrameCount : FMath::CeilToInt(FMath::Sqrt(static_cast<float>(FrameCount)));
	};
	auto LegacyRows = [&](int32 FrameCount) -> int32
	{
		const int32 C = LegacyCols(FrameCount);
		return (FrameCount <= 16) ? 1 : FMath::CeilToInt(static_cast<float>(FrameCount) / C);
	};

	// 1) Byte-identical to the legacy layout for small cells that comfortably fit MaxDimension.
	const int32 CellW = 32, CellH = 32;
	for (int32 FrameCount : {1, 2, 7, 16, 17, 25, 64, 100})
	{
		const FSpriteSheetGrid Grid = FSpriteSheetGrid::Compute(FrameCount, CellW, CellH);
		TestTrue(FString::Printf(TEXT("FrameCount=%d valid"), FrameCount), Grid.bValid);
		TestEqual(FString::Printf(TEXT("FrameCount=%d columns match legacy"), FrameCount), Grid.Columns, LegacyCols(FrameCount));
		TestEqual(FString::Printf(TEXT("FrameCount=%d rows match legacy"), FrameCount), Grid.Rows, LegacyRows(FrameCount));
		TestEqual(FString::Printf(TEXT("FrameCount=%d sheet width"), FrameCount), Grid.SheetW, Grid.Columns * CellW);
		TestEqual(FString::Printf(TEXT("FrameCount=%d sheet height"), FrameCount), Grid.SheetH, Grid.Rows * CellH);
		for (int32 i = 0; i < FrameCount; i++)
		{
			const int32 Col = i % Grid.Columns;
			const int32 Row = i / Grid.Columns;
			const FIntRect R = Grid.GetCellRect(i);
			TestEqual(FString::Printf(TEXT("FC=%d cell %d minX"), FrameCount, i), R.Min.X, Col * CellW);
			TestEqual(FString::Printf(TEXT("FC=%d cell %d minY"), FrameCount, i), R.Min.Y, Row * CellH);
			TestEqual(FString::Printf(TEXT("FC=%d cell %d maxX"), FrameCount, i), R.Max.X, (Col + 1) * CellW);
			TestEqual(FString::Printf(TEXT("FC=%d cell %d maxY"), FrameCount, i), R.Max.Y, (Row + 1) * CellH);
		}
	}

	// 2) Over-rejection fix: 16 frames of 1100x1100 = 17600px in a single row (> 16384) used to ABORT.
	//    The helper now packs them into a 2D grid that fits the max dimension.
	{
		const FSpriteSheetGrid Grid = FSpriteSheetGrid::Compute(16, 1100, 1100);
		TestTrue(TEXT("Wide 16-frame sheet now packs (was over-rejected)"), Grid.bValid);
		TestTrue(TEXT("Wide sheet width within max dimension"), Grid.SheetW <= FSpriteSheetGrid::DefaultMaxDimension);
		TestTrue(TEXT("Wide sheet height within max dimension"), Grid.SheetH <= FSpriteSheetGrid::DefaultMaxDimension);
		TestTrue(TEXT("Wide sheet uses fewer than 16 columns"), Grid.Columns < 16);
		TestTrue(TEXT("Wide sheet grid covers all 16 frames"), Grid.Columns * Grid.Rows >= 16);
	}

	// 2b) Tall-sheet over-rejection fix (Codex #118): 100 frames of 100x2000px. ceil(sqrt(100))=10 columns
	//     forces 10 rows = 20000px tall (> 16384) under a downward-only clamp, but 13 columns pack to 8 rows
	//     = 16000px, which fits. The helper must ADD columns to shrink rows instead of rejecting.
	{
		const FSpriteSheetGrid Grid = FSpriteSheetGrid::Compute(100, 100, 2000);
		TestTrue(TEXT("Tall 100-frame sheet now packs (was over-rejected)"), Grid.bValid);
		TestTrue(TEXT("Tall sheet width within max dimension"), Grid.SheetW <= FSpriteSheetGrid::DefaultMaxDimension);
		TestTrue(TEXT("Tall sheet height within max dimension"), Grid.SheetH <= FSpriteSheetGrid::DefaultMaxDimension);
		TestTrue(TEXT("Tall sheet uses more than the sqrt column count to shrink rows"), Grid.Columns > 10);
		TestTrue(TEXT("Tall sheet grid covers all 100 frames"), Grid.Columns * Grid.Rows >= 100);
	}

	// 3) True reject: a single cell taller than the max dimension cannot fit at any packing.
	{
		const FSpriteSheetGrid Grid = FSpriteSheetGrid::Compute(2, 100, 20000);
		TestFalse(TEXT("Cell taller than max dimension is rejected"), Grid.bValid);
	}

	// 4) Degenerate inputs are rejected, not crashing.
	TestFalse(TEXT("Zero frames rejected"), FSpriteSheetGrid::Compute(0, 32, 32).bValid);
	TestFalse(TEXT("Zero cell width rejected"), FSpriteSheetGrid::Compute(4, 0, 32).bValid);
	TestFalse(TEXT("Zero cell height rejected"), FSpriteSheetGrid::Compute(4, 32, 0).bValid);

	return true;
}

// =============================================================================
// WS1 — Character Layer editor reuses FCharacterProfileEditorModel for its new
// Root Motion / Frame Timing tabs. Those panels re-resolve their Asset from the
// model on OnAssetDataChanged (the WS1-U1 fix, in their Slate RefreshAll, which
// can't be driven headlessly). This proves the model seam they rely on: a re-init
// retargets GetAsset() and broadcasts OnAssetDataChanged so the panels refresh.
// =============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusEditorModelReinitRetargetsAsset,
	"Paper2DPlus.Editor.Model.ReinitRetargetsAsset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusEditorModelReinitRetargetsAsset::RunTest(const FString& Parameters)
{
	TSharedRef<FCharacterProfileEditorModel> Model = MakeShared<FCharacterProfileEditorModel>();
	UPaper2DPlusCharacterProfileAsset* ProfileA = NewObject<UPaper2DPlusCharacterProfileAsset>();
	UPaper2DPlusCharacterProfileAsset* ProfileB = NewObject<UPaper2DPlusCharacterProfileAsset>();

	int32 DataChangedCount = 0;
	Model->OnAssetDataChanged.AddLambda([&DataChangedCount]() { DataChangedCount++; });

	Model->InitializeFromAsset(ProfileA);
	TestEqual(TEXT("GetAsset() == ProfileA after first init"), Model->GetAsset(), ProfileA);

	Model->InitializeFromAsset(ProfileB);
	TestEqual(TEXT("GetAsset() retargets to ProfileB on re-init (seam WS1-U1 relies on)"), Model->GetAsset(), ProfileB);
	TestTrue(TEXT("OnAssetDataChanged broadcast on re-init (drives the panels' RefreshAll re-resolve)"), DataChangedCount >= 1);
	return true;
}

// =============================================================================
// Root Motion tab construction must adopt the shared model's current selection.
// Pre-fix it subscribed to future selection broadcasts but kept INDEX_NONE until
// the user clicked a flipbook, leaving the tab blank immediately after opening.
// =============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusRootMotionEditorConstructsWithModelSelection,
	"Paper2DPlus.RootMotion.Editor.ConstructsWithModelSelection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusRootMotionEditorConstructsWithModelSelection::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Profile = NewObject<UPaper2DPlusCharacterProfileAsset>();

	FFlipbookProfileEntry Idle;
	Idle.Identity.FlipbookName = TEXT("Idle");
	Idle.Identity.Flipbook = MakeEditorFixesFlipbook(Profile, 2);
	Profile->Flipbooks.Add(Idle);

	FFlipbookProfileEntry Attack;
	Attack.Identity.FlipbookName = TEXT("Attack");
	Attack.Identity.Flipbook = MakeEditorFixesFlipbook(Profile, 4);
	Profile->Flipbooks.Add(Attack);

	TSharedRef<FCharacterProfileEditorModel> Model = MakeShared<FCharacterProfileEditorModel>();
	Model->InitializeFromAsset(Profile);
	Model->SetSelectedFlipbook(1);
	Model->SetSelectedFrame(2);

	TSharedRef<SRootMotionEditor> Editor = SNew(SRootMotionEditor)
		.Asset(Profile)
		.Model(Model);

	TestEqual(TEXT("Root Motion editor seeds selected flipbook from model on construct"), Editor->GetSelectedFlipbookIndexForTests(), 1);
	TestEqual(TEXT("Root Motion editor seeds selected frame from model on construct"), Editor->GetSelectedFrameIndexForTests(), 2);
	TestTrue(TEXT("legacy construction keeps the embedded host contract"),
		Editor->GetHostContractForTests().OwnsEmbeddedNavigation());
	TArray<FProfileToolPanelDescriptor> ContextPanels;
	Editor->GetContextualPanels(ContextPanels);
	TestEqual(TEXT("embedded Root Motion does not duplicate contextual panels"), ContextPanels.Num(), 0);
	return true;
}

// =============================================================================
// TASK-90 — Sprite Editor frame-strip playback must be bounded by the flipbook's
// key frames, not by stale/short CombatData.Frames rows. Queue playback sets the
// model frame first; frame-strip refresh used to clamp it back to row index 3
// when a profile had 8 keyframes but only 4 frame-data rows.
// =============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusSpriteEditorFrameStripGrowsShortFrameRows,
	"Paper2DPlus.SpriteEditor.FrameStrip.GrowsShortFrameRowsForPlayback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusSpriteEditorFrameStripGrowsShortFrameRows::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Profile = NewObject<UPaper2DPlusCharacterProfileAsset>();

	FFlipbookProfileEntry Entry;
	Entry.Identity.FlipbookName = TEXT("QueuedAttack");
	Entry.Identity.Flipbook = MakeEditorFixesFlipbook(Profile, 8);
	Entry.CombatData.Frames.SetNum(4);
	Profile->Flipbooks.Add(Entry);

	const int32 DisplayCount = Paper2DPlusEditor::SpriteEditorPanelUtils::PrepareFrameStripFrameCount(Profile, 0);
	TestEqual(TEXT("Frame strip display count follows flipbook keyframes, not short frame-data rows"), DisplayCount, 8);
	TestEqual(TEXT("Short frame-data rows are grown before frame-strip clamp can fire"), Profile->Flipbooks[0].CombatData.Frames.Num(), 8);

	Profile->Flipbooks[0].CombatData.Frames.SetNum(10);
	const int32 OverlongDisplayCount = Paper2DPlusEditor::SpriteEditorPanelUtils::PrepareFrameStripFrameCount(Profile, 0);
	TestEqual(TEXT("Display count still follows keyframes when frame-data rows are overlong"), OverlongDisplayCount, 8);
	TestEqual(TEXT("Grow-only repair does not truncate overlong frame-data rows"), Profile->Flipbooks[0].CombatData.Frames.Num(), 10);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameTimingEditorConstructsWithModelSelection,
	"Paper2DPlus.FrameTiming.Editor.ConstructsWithModelSelection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameTimingEditorConstructsWithModelSelection::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Profile = NewObject<UPaper2DPlusCharacterProfileAsset>();
	FFlipbookProfileEntry Idle;
	Idle.Identity.FlipbookName = TEXT("Idle");
	Idle.Identity.Flipbook = MakeEditorFixesFlipbook(Profile, 2);
	Profile->Flipbooks.Add(Idle);
	FFlipbookProfileEntry Attack;
	Attack.Identity.FlipbookName = TEXT("Attack");
	Attack.Identity.Flipbook = MakeEditorFixesFlipbook(Profile, 4);
	Profile->Flipbooks.Add(Attack);

	TSharedRef<FCharacterProfileEditorModel> Model = MakeShared<FCharacterProfileEditorModel>();
	Model->InitializeFromAsset(Profile);
	Model->SetSelectedFlipbook(1);
	Model->SetSelectedFrame(2);
	TSharedRef<SFrameTimingEditor> Editor = SNew(SFrameTimingEditor)
		.Asset(Profile)
		.Model(Model)
		.HostContract(FProfileToolPanelHostContract::External());

	TestEqual(TEXT("Frame Timing editor seeds selected flipbook from model on construct"),
		Editor->GetSelectedFlipbookIndexForTests(), 1);
	TestEqual(TEXT("Frame Timing editor seeds selected frame from model on construct"),
		Editor->GetSelectedFrameIndexForTests(), 2);
	TestTrue(TEXT("Frame Timing shortcuts yield to editable text"),
		SFrameTimingEditor::IsShortcutProtectedWidgetTypeForTests(TEXT("SEditableText")));
	TestTrue(TEXT("Frame Timing shortcuts yield to numeric fields"),
		SFrameTimingEditor::IsShortcutProtectedWidgetTypeForTests(TEXT("SSpinBox<float>")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusSpriteEditorContextCommandsTest,
	"Paper2DPlus.SpriteEditor.ContextCommands.ValuesUndoAndStableSelection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusSpriteEditorContextCommandsTest::RunTest(const FString& Parameters)
{
	if (!TestNotNull(TEXT("GEditor available"), GEditor))
	{
		return false;
	}
	GEditor->ResetTransaction(NSLOCTEXT(
		"Paper2DPlusEditorFixesTest", "ResetSpriteContext", "Sprite Context Test Reset"));

	UPaper2DPlusCharacterProfileAsset* Profile = NewObject<UPaper2DPlusCharacterProfileAsset>(
		GetTransientPackage(), NAME_None, RF_Transactional);
	for (const TCHAR* Name : { TEXT("Idle"), TEXT("Attack") })
	{
		FFlipbookProfileEntry Entry;
		Entry.Identity.FlipbookName = Name;
		Entry.Identity.Flipbook = MakeEditorFixesFlipbook(Profile, 3);
		Entry.CombatData.Frames.SetNum(3);
		Entry.CombatData.FrameExtractionInfo.SetNum(3);
		Profile->Flipbooks.Add(MoveTemp(Entry));
	}

	const FString SpritePackageName = FString::Printf(
		TEXT("/Engine/Transient/Paper2DPlusSpriteCommand_%s"),
		*FGuid::NewGuid().ToString(EGuidFormats::Digits));
	UPackage* SpritePackage = CreatePackage(*SpritePackageName);
	UPaperSprite* SharedSprite = NewObject<UPaperSprite>(
		SpritePackage, TEXT("SharedSprite"), RF_Public | RF_Standalone);
	UPaperFlipbook* IdleFlipbook = Profile->Flipbooks[0].Identity.Flipbook.Get();
	TestNotNull(TEXT("Idle flipbook available for shared-sprite command coverage"), IdleFlipbook);
	if (!IdleFlipbook)
	{
		SharedSprite->ClearFlags(RF_Standalone);
		SpritePackage->SetDirtyFlag(false);
		return false;
	}
	{
		FScopedFlipbookMutator Mutator(IdleFlipbook);
		for (FPaperFlipbookKeyFrame& KeyFrame : Mutator.KeyFrames)
		{
			KeyFrame.Sprite = SharedSprite;
		}
	}
	SpritePackage->SetDirtyFlag(false);
	const FVector2D SharedPivotBefore = SharedSprite->GetPivotPosition();

	Profile->AddToRoot();
	TSharedPtr<FCharacterProfileEditorModel> Model = MakeShared<FCharacterProfileEditorModel>();
	Model->InitializeFromAsset(Profile);
	Model->SetSelectedFlipbook(0);
	Model->SetSelectedFrame(0);
	TSharedPtr<SSpriteEditorPanel> Panel = SNew(SSpriteEditorPanel)
		.Model(Model)
		.HostContract(FProfileToolPanelHostContract::External());

	const int32 DirectBeginBefore = Panel->GetTransactionBeginCountForTests();
	Panel->SetOffsetXForTests(4);
	Panel->SetOffsetYForTests(-2);
	TestEqual(TEXT("direct X/Y controls preserve their authored value"),
		Panel->GetCurrentOffsetForTests(), FIntPoint(4, -2));
	TestEqual(TEXT("X and Y commits retain their two established undo boundaries"),
		Panel->GetTransactionBeginCountForTests() - DirectBeginBefore, 2);
	TestTrue(TEXT("undo Y leaves the earlier X commit"), GEditor->UndoTransaction(true));
	TestEqual(TEXT("Y undo result"), Panel->GetCurrentOffsetForTests(), FIntPoint(4, 0));
	TestTrue(TEXT("undo X restores the original offset"), GEditor->UndoTransaction(true));
	TestEqual(TEXT("X undo result"), Panel->GetCurrentOffsetForTests(), FIntPoint::ZeroValue);
	TestTrue(TEXT("redo X"), GEditor->RedoTransaction());
	TestTrue(TEXT("redo Y"), GEditor->RedoTransaction());

	const int32 CopyBeginBefore = Panel->GetTransactionBeginCountForTests();
	Panel->CopyOffsetForTests();
	TestEqual(TEXT("copy remains a read-only command"),
		Panel->GetTransactionBeginCountForTests() - CopyBeginBefore, 0);
	Model->SetSelectedFrame(1);
	const int32 PasteBeginBefore = Panel->GetTransactionBeginCountForTests();
	Panel->PasteOffsetForTests();
	TestEqual(TEXT("clipboard paste targets the live selected frame"),
		Profile->Flipbooks[0].CombatData.FrameExtractionInfo[1].SpriteOffset,
		FIntPoint(4, -2));
	TestEqual(TEXT("paste is one transaction"),
		Panel->GetTransactionBeginCountForTests() - PasteBeginBefore, 1);
	TestTrue(TEXT("undo paste"), GEditor->UndoTransaction(true));
	TestEqual(TEXT("undo paste restores frame one"),
		Profile->Flipbooks[0].CombatData.FrameExtractionInfo[1].SpriteOffset,
		FIntPoint::ZeroValue);
	TestTrue(TEXT("redo paste"), GEditor->RedoTransaction());

	const int32 ResetBeginBefore = Panel->GetTransactionBeginCountForTests();
	Panel->ResetOffsetForTests();
	TestEqual(TEXT("reset clears the live frame"), Panel->GetCurrentOffsetForTests(), FIntPoint::ZeroValue);
	TestEqual(TEXT("reset is one transaction"),
		Panel->GetTransactionBeginCountForTests() - ResetBeginBefore, 1);
	TestTrue(TEXT("undo reset restores pasted value"), GEditor->UndoTransaction(true));
	TestEqual(TEXT("undo reset value"), Panel->GetCurrentOffsetForTests(), FIntPoint(4, -2));

	const int32 NudgeBeginBefore = Panel->GetTransactionBeginCountForTests();
	const int32 NudgeEndBefore = Panel->GetTransactionEndCountForTests();
	Panel->NudgeOffsetForTests(1, 0);
	Panel->NudgeOffsetForTests(0, 2);
	TestTrue(TEXT("repeated nudge owns one pending debounce"), Panel->HasNudgeDebounceForTests());
	TestEqual(TEXT("repeated nudge begins one transaction"),
		Panel->GetTransactionBeginCountForTests() - NudgeBeginBefore, 1);
	Panel->CommitPendingEditForTests();
	TestEqual(TEXT("nudge values accumulate before one commit"),
		Panel->GetCurrentOffsetForTests(), FIntPoint(5, 0));
	TestEqual(TEXT("nudge debounce closes once"),
		Panel->GetTransactionEndCountForTests() - NudgeEndBefore, 1);
	TestTrue(TEXT("undo grouped nudge"), GEditor->UndoTransaction(true));
	TestEqual(TEXT("grouped nudge undo restores prior value"),
		Panel->GetCurrentOffsetForTests(), FIntPoint(4, -2));

	const int32 BatchBeginBefore = Panel->GetTransactionBeginCountForTests();
	Panel->ConfigureBatchForTests(4, 0, FIntPoint(7, -3));
	Panel->ApplyBatchForTests();
	for (int32 FrameIndex = 0; FrameIndex < 3; ++FrameIndex)
	{
		TestEqual(*FString::Printf(TEXT("batch writes frame %d"), FrameIndex),
			Profile->Flipbooks[0].CombatData.FrameExtractionInfo[FrameIndex].SpriteOffset,
			FIntPoint(7, -3));
	}
	TestEqual(TEXT("batch apply is one transaction"),
		Panel->GetTransactionBeginCountForTests() - BatchBeginBefore, 1);
	TestTrue(TEXT("undo batch"), GEditor->UndoTransaction(true));
	TestEqual(TEXT("undo batch restores frame zero"),
		Profile->Flipbooks[0].CombatData.FrameExtractionInfo[0].SpriteOffset,
		FIntPoint(4, -2));
	TestEqual(TEXT("undo batch restores frame one"),
		Profile->Flipbooks[0].CombatData.FrameExtractionInfo[1].SpriteOffset,
		FIntPoint(4, -2));
	TestEqual(TEXT("undo batch restores untouched frame two"),
		Profile->Flipbooks[0].CombatData.FrameExtractionInfo[2].SpriteOffset,
		FIntPoint::ZeroValue);

	Model->SetSelectedFrame(0);
	const int32 SwitchEndBefore = Panel->GetTransactionEndCountForTests();
	Panel->NudgeOffsetForTests(3, 0);
	Panel->HandleHostDeactivated();
	Panel->HandleHostDeactivated();
	Model->SetSelectedFlipbook(1);
	TestFalse(TEXT("tool switch clears the nudge timer"), Panel->HasNudgeDebounceForTests());
	TestFalse(TEXT("tool switch leaves no active edit transaction"), Panel->HasActiveTransaction());
	TestEqual(TEXT("double tool-switch callback commits once"),
		Panel->GetTransactionEndCountForTests() - SwitchEndBefore, 1);
	TestEqual(TEXT("the newly selected animation receives no delayed offset"),
		Profile->Flipbooks[1].CombatData.FrameExtractionInfo[0].SpriteOffset,
		FIntPoint::ZeroValue);

	TestTrue(TEXT("Sprite shortcuts yield to editable text"),
		SSpriteEditorPanel::IsShortcutProtectedWidgetTypeForTests(TEXT("SEditableText")));
	TestTrue(TEXT("Sprite shortcuts yield to numeric fields"),
		SSpriteEditorPanel::IsShortcutProtectedWidgetTypeForTests(TEXT("SSpinBox<int32>")));
	TestFalse(TEXT("Sprite shortcuts remain active over ordinary buttons"),
		SSpriteEditorPanel::IsShortcutProtectedWidgetTypeForTests(TEXT("SButton")));
	TestEqual(TEXT("production offset commands never change a shared sprite pivot"),
		SharedSprite->GetPivotPosition(), SharedPivotBefore);
	TestFalse(TEXT("production offset commands never dirty a shared sprite package"),
		SpritePackage->IsDirty());

	GEditor->ResetTransaction(NSLOCTEXT(
		"Paper2DPlusEditorFixesTest", "EndSpriteContext", "Sprite Context Test End"));
	Profile->RemoveFromRoot();
	SharedSprite->ClearFlags(RF_Standalone);
	SpritePackage->SetDirtyFlag(false);
	return true;
}

#endif // WITH_EDITOR
