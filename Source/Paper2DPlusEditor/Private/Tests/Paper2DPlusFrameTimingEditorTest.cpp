// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#if WITH_EDITOR

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Editor.h"
#include "FrameTimingEditor.h"
#include "CharacterProfileEditorModel.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "PaperFlipbook.h"

namespace Paper2DPlusFrameTimingEditorTest
{
UPaperFlipbook* MakeFlipbook(
	UObject* Outer,
	const TArray<int32>& FrameRuns,
	float FPS)
{
	UPaperFlipbook* Flipbook = NewObject<UPaperFlipbook>(Outer, NAME_None, RF_Transactional);
	FScopedFlipbookMutator Mutator(Flipbook);
	Mutator.FramesPerSecond = FPS;
	for (const int32 FrameRun : FrameRuns)
	{
		FPaperFlipbookKeyFrame KeyFrame;
		KeyFrame.FrameRun = FrameRun;
		Mutator.KeyFrames.Add(KeyFrame);
	}
	return Flipbook;
}

struct FFixture
{
	UPaper2DPlusCharacterProfileAsset* Profile = nullptr;
	TSharedPtr<FCharacterProfileEditorModel> Model;
	UPaperFlipbook* First = nullptr;
	UPaperFlipbook* Second = nullptr;
};

FFixture MakeFixture()
{
	FFixture Fixture;
	Fixture.Profile = NewObject<UPaper2DPlusCharacterProfileAsset>(
		GetTransientPackage(), NAME_None, RF_Transactional);
	Fixture.First = MakeFlipbook(Fixture.Profile, { 1, 2, 4, 5, 6 }, 12.0f);
	Fixture.Second = MakeFlipbook(Fixture.Profile, { 1, 1, 1, 1, 1 }, 30.0f);

	FFlipbookProfileEntry FirstEntry;
	FirstEntry.Identity.FlipbookName = TEXT("Idle");
	FirstEntry.Identity.Flipbook = Fixture.First;
	Fixture.Profile->Flipbooks.Add(MoveTemp(FirstEntry));
	FFlipbookProfileEntry SecondEntry;
	SecondEntry.Identity.FlipbookName = TEXT("Attack");
	SecondEntry.Identity.Flipbook = Fixture.Second;
	Fixture.Profile->Flipbooks.Add(MoveTemp(SecondEntry));

	Fixture.Model = MakeShared<FCharacterProfileEditorModel>();
	Fixture.Model->InitializeFromAsset(Fixture.Profile);
	Fixture.Model->SetSelectedFlipbook(0);
	Fixture.Model->SetSelectedFrame(1);
	return Fixture;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameTimingConversionsUndoTest,
	"Paper2DPlus.FrameTiming.Context.ConversionsTotalAndUndo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameTimingConversionsUndoTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusFrameTimingEditorTest;
	if (!TestNotNull(TEXT("GEditor available"), GEditor)) return false;
	GEditor->ResetTransaction(NSLOCTEXT(
		"Paper2DPlusFrameTimingEditorTest", "ResetConversions", "Timing Conversion Test Reset"));

	FFixture Fixture = MakeFixture();
	Fixture.Profile->AddToRoot();
	TSharedPtr<SFrameTimingEditor> Editor = SNew(SFrameTimingEditor)
		.Asset(Fixture.Profile)
		.Model(Fixture.Model)
		.HostContract(FProfileToolPanelHostContract::External());
	Editor->HandleHostActivated();

	TestEqual(TEXT("editor seeds selected flipbook from the live model"),
		Editor->GetSelectedFlipbookIndexForTests(), 0);
	TestEqual(TEXT("editor seeds selected frame from the live model"),
		Editor->GetSelectedFrameIndexForTests(), 1);
	TestEqual(TEXT("initial FPS"), Editor->GetFPSForTests(), 12.0f);
	TestTrue(TEXT("initial total duration uses all FrameRun values"),
		FMath::IsNearlyEqual(Editor->GetTotalDurationSecondsForTests(), 18.0f / 12.0f));

	const int32 FPSBeginBefore = Editor->GetTransactionBeginCountForTests();
	Editor->SetFPSForTests(24.0f);
	TestEqual(TEXT("FPS command writes the canonical flipbook"), Fixture.First->GetFramesPerSecond(), 24.0f);
	TestEqual(TEXT("FPS edit is one undo boundary"),
		Editor->GetTransactionBeginCountForTests() - FPSBeginBefore, 1);
	TestTrue(TEXT("undo FPS"), GEditor->UndoTransaction(true));
	TestEqual(TEXT("FPS undo restores 12"), Fixture.First->GetFramesPerSecond(), 12.0f);
	TestTrue(TEXT("redo FPS"), GEditor->RedoTransaction());

	const int32 RunBeginBefore = Editor->GetTransactionBeginCountForTests();
	Editor->SetFrameRunForTests(1, 3);
	TestEqual(TEXT("frame-run edit writes selected row"), Editor->GetFrameRunForTests(1), 3);
	TestEqual(TEXT("frame-run edit is one undo boundary"),
		Editor->GetTransactionBeginCountForTests() - RunBeginBefore, 1);
	TestTrue(TEXT("undo frame-run"), GEditor->UndoTransaction(true));
	TestEqual(TEXT("frame-run undo restores two"), Editor->GetFrameRunForTests(1), 2);
	TestTrue(TEXT("redo frame-run"), GEditor->RedoTransaction());

	int32 GestureRefreshCount = 0;
	Fixture.Model->OnAssetDataChanged.AddLambda([&GestureRefreshCount]() { ++GestureRefreshCount; });
	const int32 DragRunBeginBefore = Editor->GetTransactionBeginCountForTests();
	const int32 DragRunEndBefore = Editor->GetTransactionEndCountForTests();
	Editor->BeginFrameDurationGestureForTests();
	Editor->SetFrameRunForTests(2, 7);
	Editor->SetFrameRunForTests(2, 8);
	TestTrue(TEXT("frame-duration gesture keeps one transaction open"), Editor->HasActiveTransaction());
	TestEqual(TEXT("continuous frame-duration edits defer model refresh"), GestureRefreshCount, 0);
	Editor->EndFrameDurationGestureForTests();
	TestEqual(TEXT("continuous frame-duration changes share one undo boundary"),
		Editor->GetTransactionBeginCountForTests() - DragRunBeginBefore, 1);
	TestEqual(TEXT("frame-duration gesture closes exactly once"),
		Editor->GetTransactionEndCountForTests() - DragRunEndBefore, 1);
	TestEqual(TEXT("frame-duration gesture refreshes the model once on completion"),
		GestureRefreshCount, 1);
	TestTrue(TEXT("one undo reverts the entire frame-duration gesture"), GEditor->UndoTransaction(true));
	TestEqual(TEXT("gesture undo restores the original frame duration"),
		Editor->GetFrameRunForTests(2), 4);

	const int32 DragFPSBeginBefore = Editor->GetTransactionBeginCountForTests();
	Editor->BeginFPSGestureForTests();
	Editor->SetFPSForTests(30.0f);
	Editor->SetFPSForTests(48.0f);
	Editor->EndFPSGestureForTests();
	TestEqual(TEXT("continuous FPS changes share one undo boundary"),
		Editor->GetTransactionBeginCountForTests() - DragFPSBeginBefore, 1);
	TestTrue(TEXT("one undo reverts the entire FPS gesture"), GEditor->UndoTransaction(true));
	TestEqual(TEXT("FPS gesture undo restores 24"), Fixture.First->GetFramesPerSecond(), 24.0f);

	Editor->SetDisplayUnitForTests(ETimingDisplayUnit::Milliseconds);
	Editor->SetFrameMillisecondsForTests(1, 250);
	TestEqual(TEXT("250ms at 24 FPS converts to six frame ticks"), Editor->GetFrameRunForTests(1), 6);
	TestEqual(TEXT("millisecond display round-trips the authored duration"),
		Editor->GetFrameMillisecondsForTests(1), 250);
	TestTrue(TEXT("duration-derived total updates after millisecond editing"),
		FMath::IsNearlyEqual(Editor->GetTotalDurationSecondsForTests(), 22.0f / 24.0f));
	TestTrue(TEXT("undo millisecond edit"), GEditor->UndoTransaction(true));
	TestEqual(TEXT("millisecond undo restores three ticks"), Editor->GetFrameRunForTests(1), 3);

	TestEqual(TEXT("one millisecond clamps to the minimum one frame tick"),
		Paper2DPlusEditor::FrameTimingEditorUtils::MillisecondsToFrameRun(1, 24.0f), 1);
	TestEqual(TEXT("six ticks at 24 FPS are exactly 250ms"),
		Paper2DPlusEditor::FrameTimingEditorUtils::FrameRunToMilliseconds(6, 24.0f), 250);

	Editor->HandleHostDeactivated();
	GEditor->ResetTransaction(NSLOCTEXT(
		"Paper2DPlusFrameTimingEditorTest", "EndConversions", "Timing Conversion Test End"));
	Fixture.Profile->RemoveFromRoot();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameTimingBatchClampTest,
	"Paper2DPlus.FrameTiming.Context.BatchSelectionRangeAndUndo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameTimingBatchClampTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusFrameTimingEditorTest;
	if (!TestNotNull(TEXT("GEditor available"), GEditor)) return false;
	GEditor->ResetTransaction(NSLOCTEXT(
		"Paper2DPlusFrameTimingEditorTest", "ResetBatch", "Timing Batch Test Reset"));

	FFixture Fixture = MakeFixture();
	Fixture.Profile->AddToRoot();
	TSharedPtr<SFrameTimingEditor> Editor = SNew(SFrameTimingEditor)
		.Asset(Fixture.Profile)
		.Model(Fixture.Model)
		.HostContract(FProfileToolPanelHostContract::External());
	Editor->HandleHostActivated();

	Fixture.Model->HandleFrameClick(1, true, false, 5);
	Fixture.Model->HandleFrameClick(3, true, false, 5);
	Fixture.Model->SetSelectedFrame(1);
	Editor->ConfigureBatchForTests(3, 1, 7, 0, 0);
	const int32 SelectedBeginBefore = Editor->GetTransactionBeginCountForTests();
	Editor->ApplyBatchForTests();
	TestEqual(TEXT("selected batch writes frame one"), Editor->GetFrameRunForTests(1), 7);
	TestEqual(TEXT("selected batch writes frame three"), Editor->GetFrameRunForTests(3), 7);
	TestEqual(TEXT("selected batch leaves frame zero"), Editor->GetFrameRunForTests(0), 1);
	TestEqual(TEXT("selected batch leaves frame two"), Editor->GetFrameRunForTests(2), 4);
	TestEqual(TEXT("selected batch is one transaction"),
		Editor->GetTransactionBeginCountForTests() - SelectedBeginBefore, 1);
	TestTrue(TEXT("undo selected batch"), GEditor->UndoTransaction(true));
	TestEqual(TEXT("undo restores frame one"), Editor->GetFrameRunForTests(1), 2);
	TestEqual(TEXT("undo restores frame three"), Editor->GetFrameRunForTests(3), 5);

	Editor->ConfigureBatchForTests(3, 3, 5, -100, 100);
	Editor->ApplyBatchForTests();
	for (int32 FrameIndex = 0; FrameIndex < 5; ++FrameIndex)
	{
		TestEqual(*FString::Printf(TEXT("clamped custom range writes frame %d"), FrameIndex),
			Editor->GetFrameRunForTests(FrameIndex), 5);
	}
	TestTrue(TEXT("undo clamped range"), GEditor->UndoTransaction(true));

	Fixture.Model->ClearFrameSelection();
	Fixture.Model->SetSelectedFrame(2);
	Editor->ConfigureBatchForTests(3, 2, 9, 0, 0);
	Editor->ApplyBatchForTests();
	TestEqual(TEXT("remaining leaves frame one"), Editor->GetFrameRunForTests(1), 2);
	for (int32 FrameIndex = 2; FrameIndex < 5; ++FrameIndex)
	{
		TestEqual(*FString::Printf(TEXT("remaining writes frame %d"), FrameIndex),
			Editor->GetFrameRunForTests(FrameIndex), 9);
	}

	Editor->HandleHostDeactivated();
	GEditor->ResetTransaction(NSLOCTEXT(
		"Paper2DPlusFrameTimingEditorTest", "EndBatch", "Timing Batch Test End"));
	Fixture.Profile->RemoveFromRoot();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameTimingToolSwitchSafetyTest,
	"Paper2DPlus.FrameTiming.Context.ToolSwitchRejectsStaleCommitAndDefersUndoRefresh",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameTimingToolSwitchSafetyTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusFrameTimingEditorTest;
	if (!TestNotNull(TEXT("GEditor available"), GEditor)) return false;
	GEditor->ResetTransaction(NSLOCTEXT(
		"Paper2DPlusFrameTimingEditorTest", "ResetSwitch", "Timing Switch Test Reset"));

	FFixture Fixture = MakeFixture();
	Fixture.Profile->AddToRoot();
	TSharedPtr<SFrameTimingEditor> Editor = SNew(SFrameTimingEditor)
		.Asset(Fixture.Profile)
		.Model(Fixture.Model)
		.HostContract(FProfileToolPanelHostContract::External());
	Editor->HandleHostActivated();
	UPaperFlipbook* OldSelectionFlipbook = Fixture.First;

	Editor->HandleHostDeactivated();
	Editor->HandleHostDeactivated();
	Fixture.Model->SetSelectedFlipbook(1);
	Fixture.Model->SetSelectedFrame(0);
	Editor->ApplySelectionDurationForTests(OldSelectionFlipbook, 0, 12);
	Editor->SetFPSForTests(60.0f);
	TestEqual(TEXT("inactive stale row cannot write the newly selected flipbook"),
		Fixture.Second->GetKeyFrameChecked(0).FrameRun, 1);
	TestEqual(TEXT("inactive timing control cannot write the new flipbook FPS"),
		Fixture.Second->GetFramesPerSecond(), 30.0f);

	Editor->HandleHostActivated();
	Editor->ApplySelectionDurationForTests(OldSelectionFlipbook, 0, 12);
	TestEqual(TEXT("old row identity remains rejected after reactivation"),
		Fixture.Second->GetKeyFrameChecked(0).FrameRun, 1);
	Editor->SetFrameRunForTests(0, 4);
	TestEqual(TEXT("fresh active command writes the live selected flipbook"),
		Fixture.Second->GetKeyFrameChecked(0).FrameRun, 4);

	Editor->HandleHostDeactivated();
	TestTrue(TEXT("undo active edit while hidden"), GEditor->UndoTransaction(true));
	TestTrue(TEXT("hidden timing tool records deferred refresh"), Editor->HasPendingRefreshForTests());
	Editor->HandleHostActivated();
	TestFalse(TEXT("reactivation flushes deferred refresh"), Editor->HasPendingRefreshForTests());
	TestEqual(TEXT("reactivation retains live selected flipbook"),
		Editor->GetSelectedFlipbookIndexForTests(), 1);
	TestEqual(TEXT("undo value visible after deferred refresh"), Editor->GetFrameRunForTests(0), 1);

	Editor->HandleHostDeactivated();
	GEditor->ResetTransaction(NSLOCTEXT(
		"Paper2DPlusFrameTimingEditorTest", "EndSwitch", "Timing Switch Test End"));
	Fixture.Profile->RemoveFromRoot();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameTimingHostFocusSeatTest,
	"Paper2DPlus.FrameTiming.Context.HostActivationQueuesFocusSeat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameTimingHostFocusSeatTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusFrameTimingEditorTest;
	// Frame Timing handles Space in its own OnKeyDown, so activation must seat keyboard focus the
	// same deferred way the Frame Cues tool does — otherwise the first Space is dead until a click.
	FFixture Fixture = MakeFixture();
	Fixture.Profile->AddToRoot();
	TSharedPtr<SFrameTimingEditor> Editor = SNew(SFrameTimingEditor)
		.Asset(Fixture.Profile)
		.Model(Fixture.Model)
		.HostContract(FProfileToolPanelHostContract::External());

	TestFalse(TEXT("construction alone queues no focus seat"),
		Editor->HasPendingHostFocusSeatForTests());
	Editor->HandleHostActivated();
	TestTrue(TEXT("host activation marks the tool active"), Editor->IsHostActiveForTests());
	TestTrue(TEXT("host activation queues the deferred focus seat"),
		Editor->HasPendingHostFocusSeatForTests());
	// A second activation before the seat fires must not stack a second timer; the one pending
	// seat still drains through the production body.
	Editor->HandleHostActivated();
	TestTrue(TEXT("re-activation keeps exactly the pending seat"),
		Editor->HasPendingHostFocusSeatForTests());
	Editor->ApplyDeferredHostFocusForTests();
	TestFalse(TEXT("the deferred seat executes once and drains"),
		Editor->HasPendingHostFocusSeatForTests());

	Editor->HandleHostDeactivated();
	Editor.Reset();
	Fixture.Profile->RemoveFromRoot();
	return true;
}

#endif // WITH_EDITOR
