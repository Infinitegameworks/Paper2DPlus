// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#if WITH_EDITOR

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "CharacterProfileEditorModel.h"
#include "Editor.h"
#include "FrameEventEditor.h"
#include "FrameCues/Paper2DPlusSpawnFlipbookCue.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Paper2DPlusEditorTestFrameCueTypes.h"
#include "PaperFlipbook.h"

#define LOCTEXT_NAMESPACE "Paper2DPlusSpawnFlipbookGizmoTest"

namespace Paper2DPlusSpawnFlipbookGizmoTest
{
	UPaper2DPlusCharacterProfileAsset* SpawnGizmo_MakeAsset()
	{
		UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>(
			GetTransientPackage(), NAME_None, RF_Transactional);
		Asset->Flipbooks.AddDefaulted();
		return Asset;
	}

	UPaperFlipbook* SpawnGizmo_MakeFlipbook(int32 FrameCount, UObject* Outer)
	{
		UPaperFlipbook* Flipbook = NewObject<UPaperFlipbook>(Outer);
		FScopedFlipbookMutator Mutator(Flipbook);
		Mutator.KeyFrames.AddDefaulted(FMath::Max(0, FrameCount));
		return Flipbook;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusSpawnFlipbookGizmoDragTest,
	"Paper2DPlus.FrameCues.SpawnFlipbookCue.OffsetGizmoDragIsOneTransactionalGesture",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusSpawnFlipbookGizmoDragTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusSpawnFlipbookGizmoTest;
	if (!TestNotNull(TEXT("GEditor available"), GEditor))
	{
		return false;
	}
	GEditor->ResetTransaction(
		LOCTEXT("ResetSpawnGizmoTransactions", "Spawn Gizmo Test Reset"));

	UPaper2DPlusCharacterProfileAsset* Asset = SpawnGizmo_MakeAsset();
	Asset->AddToRoot();
	Asset->Flipbooks[0].Identity.FlipbookName = TEXT("SpawnGizmo");
	Asset->Flipbooks[0].Identity.Flipbook = SpawnGizmo_MakeFlipbook(6, Asset);
	UPaper2DPlusSpawnFlipbookCue* SpawnCue = NewObject<UPaper2DPlusSpawnFlipbookCue>(
		Asset, NAME_None, RF_Transactional);
	SpawnCue->TriggerFrame = 2;
	SpawnCue->Offset = FVector2D(5.0f, 3.0f);
	Asset->Flipbooks[0].FrameEventData.FrameCues.Add(SpawnCue);

	TSharedPtr<FCharacterProfileEditorModel> Model = MakeShared<FCharacterProfileEditorModel>();
	Model->InitializeFromAsset(Asset);
	Model->SetSelectedFlipbook(0);
	TSharedPtr<SFrameEventEditor> Editor = SNew(SFrameEventEditor).Model(Model);
	Editor->SelectCueForTests(0);

	TestTrue(TEXT("the spawn cue placement is the live selection"),
		Editor->GetSelectedCueForTests() == SpawnCue);

	// One gesture: start arms the write gate and opens the one transaction.
	Editor->BeginOffsetGizmoInteractionForTests();
	if (!TestTrue(TEXT("the gizmo gesture is live after start"),
		Editor->HasOffsetGizmoInteractionForTests()))
	{
		Editor.Reset();
		Asset->RemoveFromRoot();
		return false;
	}
	TestTrue(TEXT("a live gizmo gesture raises the write gate"),
		Editor->HasActiveTransaction());

	// The preview subject sits at identity scale facing right, so a world drag of (+10, +4)
	// lands verbatim in authored offset units.
	Editor->ApplyOffsetGizmoDeltaForTests(FVector(10.0f, 0.0f, 4.0f));
	TestTrue(TEXT("the world drag accumulates into the authored offset X"),
		FMath::IsNearlyEqual(SpawnCue->Offset.X, 15.0, KINDA_SMALL_NUMBER));
	TestTrue(TEXT("the world drag accumulates into the authored offset Y"),
		FMath::IsNearlyEqual(SpawnCue->Offset.Y, 7.0, KINDA_SMALL_NUMBER));

	Editor->EndOffsetGizmoInteractionForTests();
	TestFalse(TEXT("the gesture settles exactly once on end"),
		Editor->HasOffsetGizmoInteractionForTests());
	TestFalse(TEXT("settling the gesture closes its transaction"),
		Editor->HasActiveTransaction());
	// Settling again must be a no-op, not a second End.
	Editor->EndOffsetGizmoInteractionForTests();
	TestFalse(TEXT("a re-entrant settle is a no-op"),
		Editor->HasActiveTransaction());

	TestTrue(TEXT("one Undo restores the pre-drag offset"),
		GEditor->UndoTransaction(true));
	TestTrue(TEXT("Undo restores offset X"),
		FMath::IsNearlyEqual(SpawnCue->Offset.X, 5.0, KINDA_SMALL_NUMBER));
	TestTrue(TEXT("Undo restores offset Y"),
		FMath::IsNearlyEqual(SpawnCue->Offset.Y, 3.0, KINDA_SMALL_NUMBER));

	// A placement that is not the one draggable class never arms the gesture.
	UPaper2DPlusEditorTestMomentCue* PlainCue = NewObject<UPaper2DPlusEditorTestMomentCue>(
		Asset, NAME_None, RF_Transactional);
	Asset->Flipbooks[0].FrameEventData.FrameCues.Add(PlainCue);
	Editor->SelectCueForTests(1);
	Editor->BeginOffsetGizmoInteractionForTests();
	TestFalse(TEXT("a non-spawn placement never arms the offset gesture"),
		Editor->HasOffsetGizmoInteractionForTests());
	TestFalse(TEXT("a refused arm opens no transaction"),
		Editor->HasActiveTransaction());

	Editor.Reset();
	GEditor->ResetTransaction(
		LOCTEXT("EndSpawnGizmoTransactions", "Spawn Gizmo Test End"));
	Asset->RemoveFromRoot();
	return true;
}

#undef LOCTEXT_NAMESPACE

#endif // WITH_EDITOR
