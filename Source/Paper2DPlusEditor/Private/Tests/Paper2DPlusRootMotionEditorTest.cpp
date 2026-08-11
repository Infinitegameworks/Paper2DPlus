// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#if WITH_EDITOR

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Editor.h"
#include "CharacterProfileEditorModel.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "PaperFlipbook.h"
#include "RootMotionEditor.h"

namespace Paper2DPlusRootMotionEditorTest
{
UPaperFlipbook* MakeFlipbook(UObject* Outer, int32 FrameCount)
{
	UPaperFlipbook* Flipbook = NewObject<UPaperFlipbook>(Outer, NAME_None, RF_Transactional);
	FScopedFlipbookMutator Mutator(Flipbook);
	Mutator.KeyFrames.Empty();
	for (int32 FrameIndex = 0; FrameIndex < FrameCount; ++FrameIndex)
	{
		FPaperFlipbookKeyFrame KeyFrame;
		KeyFrame.FrameRun = 1;
		Mutator.KeyFrames.Add(KeyFrame);
	}
	return Flipbook;
}

struct FFixture
{
	UPaper2DPlusCharacterProfileAsset* Profile = nullptr;
	TSharedPtr<FCharacterProfileEditorModel> Model;
};

FFixture MakeFixture()
{
	FFixture Fixture;
	Fixture.Profile = NewObject<UPaper2DPlusCharacterProfileAsset>(
		GetTransientPackage(), NAME_None, RF_Transactional);
	for (const TCHAR* Name : { TEXT("Idle"), TEXT("Attack") })
	{
		FFlipbookProfileEntry Entry;
		Entry.Identity.FlipbookName = Name;
		Entry.Identity.Flipbook = MakeFlipbook(Fixture.Profile, 5);
		Entry.MotionData.RootMotion.SetNum(5);
		for (int32 FrameIndex = 0; FrameIndex < 5; ++FrameIndex)
		{
			Entry.MotionData.RootMotion[FrameIndex].Position = FVector2D(
				static_cast<float>(FrameIndex),
				static_cast<float>(FrameIndex * 10));
		}
		Fixture.Profile->Flipbooks.Add(MoveTemp(Entry));
	}
	Fixture.Model = MakeShared<FCharacterProfileEditorModel>();
	Fixture.Model->InitializeFromAsset(Fixture.Profile);
	Fixture.Model->SetSelectedFlipbook(0);
	Fixture.Model->SetSelectedFrame(1);
	return Fixture;
}

TSharedPtr<SRootMotionEditor> MakeExternalEditor(FFixture& Fixture)
{
	TSharedPtr<SRootMotionEditor> Editor = SNew(SRootMotionEditor)
		.Asset(Fixture.Profile)
		.Model(Fixture.Model)
		.HostContract(FProfileToolPanelHostContract::External());
	Editor->HandleHostActivated();
	return Editor;
}

bool IsPosition(
	FAutomationTestBase& Test,
	const TCHAR* What,
	const FVector2D& Actual,
	const FVector2D& Expected)
{
	return Test.TestTrue(
		What,
		Actual.Equals(Expected, KINDA_SMALL_NUMBER));
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusRootMotionPositionUndoTest,
	"Paper2DPlus.RootMotion.Context.PositionDragResetNudgeAndUndo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusRootMotionPositionUndoTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusRootMotionEditorTest;
	if (!TestNotNull(TEXT("GEditor available"), GEditor)) return false;
	GEditor->ResetTransaction(NSLOCTEXT(
		"Paper2DPlusRootMotionEditorTest", "ResetPosition", "Root Motion Position Test Reset"));

	FFixture Fixture = MakeFixture();
	Fixture.Profile->AddToRoot();
	TSharedPtr<SRootMotionEditor> Editor = MakeExternalEditor(Fixture);

	const int32 SpinBeginBefore = Editor->GetTransactionBeginCountForTests();
	Editor->SetCurrentFrameXForTests(30.0f);
	IsPosition(*this, TEXT("X edit preserves Y"),
		Editor->GetCurrentFramePositionForTests(), FVector2D(30.0f, 10.0f));
	TestEqual(TEXT("X edit opens one transaction"),
		Editor->GetTransactionBeginCountForTests() - SpinBeginBefore, 1);
	TestTrue(TEXT("undo X edit"), GEditor->UndoTransaction(true));
	IsPosition(*this, TEXT("undo restores X and Y"),
		Editor->GetCurrentFramePositionForTests(), FVector2D(1.0f, 10.0f));
	TestTrue(TEXT("redo X edit"), GEditor->RedoTransaction());

	const int32 DragBeginBefore = Editor->GetTransactionBeginCountForTests();
	Editor->BeginCanvasDragForTests();
	Editor->DragCanvasToForTests(FVector2D(42.0f, -8.0f));
	Editor->EndCanvasDragForTests();
	IsPosition(*this, TEXT("canvas drag preserves Paper2D signed coordinates"),
		Editor->GetCurrentFramePositionForTests(), FVector2D(42.0f, -8.0f));
	TestEqual(TEXT("canvas drag is one transaction"),
		Editor->GetTransactionBeginCountForTests() - DragBeginBefore, 1);
	TestTrue(TEXT("undo canvas drag"), GEditor->UndoTransaction(true));
	IsPosition(*this, TEXT("canvas undo restores spinbox result"),
		Editor->GetCurrentFramePositionForTests(), FVector2D(30.0f, 10.0f));

	Editor->ResetCurrentFrameForTests();
	IsPosition(*this, TEXT("reset writes zero"),
		Editor->GetCurrentFramePositionForTests(), FVector2D::ZeroVector);
	TestTrue(TEXT("undo reset"), GEditor->UndoTransaction(true));
	IsPosition(*this, TEXT("reset undo restores position"),
		Editor->GetCurrentFramePositionForTests(), FVector2D(30.0f, 10.0f));

	Editor->NudgeCurrentFrameForTests(FVector2D(1.0f, -2.0f));
	TestTrue(TEXT("nudge keeps one pending transaction"), Editor->HasActiveTransaction());
	Editor->CommitPendingEditForTests();
	IsPosition(*this, TEXT("nudge applies one-pixel coordinate deltas"),
		Editor->GetCurrentFramePositionForTests(), FVector2D(31.0f, 8.0f));
	TestTrue(TEXT("undo nudge"), GEditor->UndoTransaction(true));
	IsPosition(*this, TEXT("nudge undo restores position"),
		Editor->GetCurrentFramePositionForTests(), FVector2D(30.0f, 10.0f));

	Editor->HandleHostDeactivated();
	Editor.Reset();
	GEditor->ResetTransaction(NSLOCTEXT(
		"Paper2DPlusRootMotionEditorTest", "EndPosition", "Root Motion Position Test End"));
	Fixture.Profile->RemoveFromRoot();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusRootMotionBatchTest,
	"Paper2DPlus.RootMotion.Context.BatchSetOffsetMirrorInterpolateRanges",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusRootMotionBatchTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusRootMotionEditorTest;
	if (!TestNotNull(TEXT("GEditor available"), GEditor)) return false;
	GEditor->ResetTransaction(NSLOCTEXT(
		"Paper2DPlusRootMotionEditorTest", "ResetBatch", "Root Motion Batch Test Reset"));

	FFixture Fixture = MakeFixture();
	Fixture.Profile->AddToRoot();
	TSharedPtr<SRootMotionEditor> Editor = MakeExternalEditor(Fixture);
	auto Motion = [&Fixture]() -> TArray<FRootMotionFrameData>&
	{
		return Fixture.Profile->Flipbooks[0].MotionData.RootMotion;
	};

	Editor->ConfigureBatchForTests(2, 3, FVector2D(7.0f, 8.0f), -100, 1);
	Editor->ApplyBatchForTests();
	IsPosition(*this, TEXT("Set clamps custom range start"), Motion()[0].Position, FVector2D(7.0f, 8.0f));
	IsPosition(*this, TEXT("Set includes clamped range end"), Motion()[1].Position, FVector2D(7.0f, 8.0f));
	IsPosition(*this, TEXT("Set leaves frame outside range"), Motion()[2].Position, FVector2D(2.0f, 20.0f));
	TestTrue(TEXT("undo Set"), GEditor->UndoTransaction(true));

	Editor->ConfigureBatchForTests(4, 3, FVector2D(2.0f, -1.0f), 1, 3);
	Editor->ApplyBatchForTests();
	IsPosition(*this, TEXT("Offset adds to range start"), Motion()[1].Position, FVector2D(3.0f, 9.0f));
	IsPosition(*this, TEXT("Offset adds to middle"), Motion()[2].Position, FVector2D(4.0f, 19.0f));
	IsPosition(*this, TEXT("Offset adds to range end"), Motion()[3].Position, FVector2D(5.0f, 29.0f));
	IsPosition(*this, TEXT("Offset leaves prior frame"), Motion()[0].Position, FVector2D::ZeroVector);
	TestTrue(TEXT("undo Offset"), GEditor->UndoTransaction(true));

	Fixture.Model->SetSelectedFrame(2);
	Editor->ConfigureBatchForTests(5, 2, FVector2D::ZeroVector, 0, 0);
	Editor->ApplyBatchForTests();
	IsPosition(*this, TEXT("Mirror leaves prior frame"), Motion()[1].Position, FVector2D(1.0f, 10.0f));
	IsPosition(*this, TEXT("Mirror negates remaining X"), Motion()[2].Position, FVector2D(-2.0f, 20.0f));
	IsPosition(*this, TEXT("Mirror preserves remaining Y"), Motion()[4].Position, FVector2D(-4.0f, 40.0f));
	TestTrue(TEXT("undo Mirror"), GEditor->UndoTransaction(true));

	Motion()[1].Position = FVector2D(10.0f, 10.0f);
	Motion()[2].Position = FVector2D(99.0f, 99.0f);
	Motion()[3].Position = FVector2D(-50.0f, 75.0f);
	Motion()[4].Position = FVector2D(40.0f, 40.0f);
	const int32 InvalidBeginBefore = Editor->GetTransactionBeginCountForTests();
	Editor->ConfigureBatchForTests(3, 0, FVector2D::ZeroVector, 1, 4);
	Editor->ApplyBatchForTests();
	TestEqual(TEXT("Interpolate rejects a non-range target before transaction"),
		Editor->GetTransactionBeginCountForTests(), InvalidBeginBefore);
	IsPosition(*this, TEXT("invalid Interpolate leaves data"), Motion()[2].Position, FVector2D(99.0f, 99.0f));

	Editor->ConfigureBatchForTests(3, 3, FVector2D::ZeroVector, 4, 1);
	Editor->ApplyBatchForTests();
	IsPosition(*this, TEXT("Interpolate accepts reversed range and keeps start"),
		Motion()[1].Position, FVector2D(10.0f, 10.0f));
	IsPosition(*this, TEXT("Interpolate writes first interior frame"),
		Motion()[2].Position, FVector2D(20.0f, 20.0f));
	IsPosition(*this, TEXT("Interpolate writes second interior frame"),
		Motion()[3].Position, FVector2D(30.0f, 30.0f));
	IsPosition(*this, TEXT("Interpolate keeps end"), Motion()[4].Position, FVector2D(40.0f, 40.0f));
	TestTrue(TEXT("undo Interpolate"), GEditor->UndoTransaction(true));
	IsPosition(*this, TEXT("Interpolate undo restores irregular frame"),
		Motion()[2].Position, FVector2D(99.0f, 99.0f));

	Editor->HandleHostDeactivated();
	Editor.Reset();
	GEditor->ResetTransaction(NSLOCTEXT(
		"Paper2DPlusRootMotionEditorTest", "EndBatch", "Root Motion Batch Test End"));
	Fixture.Profile->RemoveFromRoot();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusRootMotionContextLifecycleTest,
	"Paper2DPlus.RootMotion.Context.LivePanelsSkinsAndToolSwitchSafety",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusRootMotionContextLifecycleTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusRootMotionEditorTest;
	if (!TestNotNull(TEXT("GEditor available"), GEditor)) return false;
	GEditor->ResetTransaction(NSLOCTEXT(
		"Paper2DPlusRootMotionEditorTest", "ResetLifecycle", "Root Motion Lifecycle Test Reset"));

	FFixture Fixture = MakeFixture();
	Fixture.Profile->AddToRoot();
	TSharedPtr<SRootMotionEditor> Editor = SNew(SRootMotionEditor)
		.Asset(Fixture.Profile)
		.Model(Fixture.Model)
		.HostContract(FProfileToolPanelHostContract::External());
	TestFalse(TEXT("external Root Motion controller starts inactive"), Editor->IsHostActiveForTests());
	Editor->HandleHostActivated();

	TArray<FProfileToolPanelDescriptor> Descriptors;
	Editor->GetContextualPanels(Descriptors);
	TestEqual(TEXT("Root Motion exposes exactly three contextual panels"), Descriptors.Num(), 3);
	const TArray<FName> ExpectedIds = {
		SRootMotionEditor::PositionPanelId,
		SRootMotionEditor::OnionSkinsPanelId,
		SRootMotionEditor::BatchPanelId };
	for (int32 Index = 0; Index < ExpectedIds.Num(); ++Index)
	{
		TestEqual(TEXT("context panel order is stable"), Descriptors[Index].PanelId, ExpectedIds[Index]);
		TestNotNull(TEXT("context panel factory builds a live view"), Descriptors[Index].TryCreateWidget().Get());
		TestEqual(TEXT("initial factory resolves live frame one"),
			Editor->GetContextPanelResolvedFrameForTests(ExpectedIds[Index]), 1);
	}

	Editor->SetPathSkinStateForTests(true, true, 2, 0.55f);
	TestTrue(TEXT("Onion setting drives the central canvas"), Editor->IsCanvasShowingOnionForTests());
	TestTrue(TEXT("Forward setting drives the central canvas"), Editor->IsCanvasShowingForwardOnionForTests());

	Fixture.Model->SetSelectedFrame(3);
	TestNotNull(TEXT("Position panel can be reopened"), Descriptors[0].TryCreateWidget().Get());
	TestEqual(TEXT("reopened Position resolves current frame instead of captured frame"),
		Editor->GetContextPanelResolvedFrameForTests(SRootMotionEditor::PositionPanelId), 3);
	Editor->SetCurrentFrameXForTests(77.0f);
	IsPosition(*this, TEXT("reopened Position writes live frame three"),
		Fixture.Profile->Flipbooks[0].MotionData.RootMotion[3].Position,
		FVector2D(77.0f, 30.0f));
	IsPosition(*this, TEXT("reopened Position does not write old frame one"),
		Fixture.Profile->Flipbooks[0].MotionData.RootMotion[1].Position,
		FVector2D(1.0f, 10.0f));

	const int32 EndBeforeSwitch = Editor->GetTransactionEndCountForTests();
	Editor->BeginCanvasDragForTests();
	Editor->DragCanvasToForTests(FVector2D(88.0f, -6.0f));
	Editor->HandleHostDeactivated();
	Editor->HandleHostDeactivated();
	TestFalse(TEXT("tool switch leaves no Root Motion transaction"), Editor->HasActiveTransaction());
	TestEqual(TEXT("repeated deactivation ends the canvas gesture exactly once"),
		Editor->GetTransactionEndCountForTests() - EndBeforeSwitch, 1);
	Editor->DragCanvasToForTests(FVector2D(999.0f, 999.0f));
	Editor->EndCanvasDragForTests();
	IsPosition(*this, TEXT("delayed canvas callback cannot write while inactive"),
		Fixture.Profile->Flipbooks[0].MotionData.RootMotion[3].Position,
		FVector2D(88.0f, -6.0f));

	Fixture.Model->SetSelectedFrame(4);
	Editor->SetCurrentFrameYForTests(-12.0f);
	IsPosition(*this, TEXT("inactive contextual command cannot write new frame"),
		Fixture.Profile->Flipbooks[0].MotionData.RootMotion[4].Position,
		FVector2D(4.0f, 40.0f));
	Editor->HandleHostActivated();
	Editor->SetCurrentFrameYForTests(-12.0f);
	IsPosition(*this, TEXT("reactivated command writes current frame"),
		Fixture.Profile->Flipbooks[0].MotionData.RootMotion[4].Position,
		FVector2D(4.0f, -12.0f));

	Editor->HandleHostDeactivated();
	Editor.Reset();
	GEditor->ResetTransaction(NSLOCTEXT(
		"Paper2DPlusRootMotionEditorTest", "EndLifecycle", "Root Motion Lifecycle Test End"));
	Fixture.Profile->RemoveFromRoot();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusRootMotionHostFocusSeatTest,
	"Paper2DPlus.RootMotion.Context.HostActivationQueuesFocusSeat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusRootMotionHostFocusSeatTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusRootMotionEditorTest;
	// Root Motion handles Space in its own OnKeyDown, so activation must seat keyboard focus the
	// same deferred way the Frame Cues tool does — otherwise the first Space is dead until a click.
	FFixture Fixture = MakeFixture();
	Fixture.Profile->AddToRoot();
	TSharedPtr<SRootMotionEditor> Editor = SNew(SRootMotionEditor)
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
