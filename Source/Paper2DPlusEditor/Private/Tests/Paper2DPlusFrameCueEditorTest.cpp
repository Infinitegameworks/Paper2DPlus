// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#if WITH_EDITOR

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "FrameEventEditor.h"
#include "Paper2DPlusEditorTestFrameCueTypes.h"
#include "FrameCues/Paper2DPlusFrameCueBlueprint.h"
#include "FrameCues/Paper2DPlusFrameCuePlacementAuthoring.h"
#include "FrameCues/Paper2DPlusFrameCuePreviewContext.h"
#include "FrameCues/Paper2DPlusFrameCuePreviewHost.h"
#include "FrameCues/Paper2DPlusFrameCueTypeAuthoring.h"
#include "CharacterProfileEditorModel.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "SFrameEventTimelineTrack.h"
#include "SlateShortcutUtils.h"
#include "ScopedTransaction.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/App.h"
#include "PaperFlipbook.h"
#include "PaperFlipbookComponent.h"
#include "PackageTools.h"
#include "UObject/GarbageCollection.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/SWindow.h"

#define LOCTEXT_NAMESPACE "Paper2DPlusFrameCueEditorTest"

namespace Paper2DPlusFrameCueEditorTest
{
	UPaper2DPlusCharacterProfileAsset* MakeAsset()
	{
		UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>(
			GetTransientPackage(), NAME_None, RF_Transactional);
		Asset->Flipbooks.AddDefaulted();
		return Asset;
	}

	UPaperFlipbook* MakeFlipbook(int32 FrameCount, UObject* Outer)
	{
		UPaperFlipbook* Flipbook = NewObject<UPaperFlipbook>(Outer);
		FScopedFlipbookMutator Mutator(Flipbook);
		Mutator.KeyFrames.AddDefaulted(FMath::Max(0, FrameCount));
		return Flipbook;
	}

	FKeyEvent MakeKeyEvent(const FKey& Key, bool bShift = false, bool bControl = false)
	{
		const FModifierKeysState Modifiers(
			bShift, false,
			bControl, false,
			false, false,
			false, false,
			false);
		return FKeyEvent(Key, Modifiers, 0, false, 0, 0);
	}

	struct FUnsavedCueTypeFixture
	{
		explicit FUnsavedCueTypeFixture(
			FAutomationTestBase& InTest,
			EPaper2DPlusFrameCueTypeKind Kind = EPaper2DPlusFrameCueTypeKind::Moment)
			: Test(InTest)
		{
			const FString PackageName = FString::Printf(
				TEXT("/Game/__AutomationTemp__/P2DPInlineCue_%s"),
				*FGuid::NewGuid().ToString(EGuidFormats::Digits));
			Package = CreatePackage(*PackageName);
			if (!Package)
			{
				return;
			}
			Package->AddToRoot();
			Package->SetDirtyFlag(false);

			FPaper2DPlusFrameCueTypeCreateRequest Request;
			Request.Package = Package;
			Request.AssetName = TEXT("BP_UnsavedInlineCue");
			Request.Kind = Kind;
			const FPaper2DPlusFrameCueTypeCreateResult Result =
				FPaper2DPlusFrameCueTypeAuthoring::CreateCueType(Request);
			CueType = Result.CueType;
		}

		~FUnsavedCueTypeFixture()
		{
			if (Package)
			{
				CueType = nullptr;
				Package->SetDirtyFlag(false);
				Package->RemoveFromRoot();
				FText Error;
				TArray<UPackage*> Packages = { Package };
				if (!UPackageTools::UnloadPackages(Packages, Error, true))
				{
					Test.AddError(FString::Printf(
						TEXT("Unsaved inline Cue Type fixture cleanup failed: %s"),
						*Error.ToString()));
				}
				CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
			}
		}

		FAutomationTestBase& Test;
		UPackage* Package = nullptr;
		UPaper2DPlusFrameCueBlueprint* CueType = nullptr;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCuePlacementAuthoringTest,
	"Paper2DPlus.FrameCues.Editor.Authoring.PlacementOuterDuplicateAndClamp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCuePlacementAuthoringTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusFrameCueEditorAuthoring;
	UPaper2DPlusCharacterProfileAsset* Asset = Paper2DPlusFrameCueEditorTest::MakeAsset();

	UPaper2DPlusEditorTestMomentCue* Moment = Cast<UPaper2DPlusEditorTestMomentCue>(CreatePlacement(
		Asset, UPaper2DPlusEditorTestMomentCue::StaticClass(), 99, 6));
	if (!TestNotNull(TEXT("Concrete Moment placement created"), Moment))
	{
		return false;
	}
	TestTrue(TEXT("Placement is owned directly by the profile asset"), Moment->GetOuter() == Asset);
	TestTrue(TEXT("Placement is transactional"), Moment->HasAnyFlags(RF_Transactional));
	TestEqual(TEXT("Moment anchor clamps to the last animation key frame"), Moment->TriggerFrame, 5);
	TestEqual(TEXT("Placement inherits class default scale"), Moment->Scale, FVector2D(1.0, 1.0));

	Moment->DebugName = TEXT("DuplicatedPlacement");
	Moment->Offset = FVector2D(12.0, -4.0);
	UPaper2DPlusEditorTestMomentCue* Duplicate = Cast<UPaper2DPlusEditorTestMomentCue>(
		DuplicatePlacement(Asset, Moment, 6));
	if (TestNotNull(TEXT("Placement duplicates"), Duplicate))
	{
		TestTrue(TEXT("Duplicate has independent identity"), Duplicate != Moment);
		TestTrue(TEXT("Duplicate retains the asset outer"), Duplicate->GetOuter() == Asset);
		TestTrue(TEXT("Duplicate remains transactional"), Duplicate->HasAnyFlags(RF_Transactional));
		TestEqual(TEXT("Duplicate preserves label"), Duplicate->DebugName, Moment->DebugName);
		TestEqual(TEXT("Duplicate preserves custom payload"), Duplicate->Offset, Moment->Offset);
		TestEqual(TEXT("Duplicate preserves anchor"), Duplicate->TriggerFrame, Moment->TriggerFrame);
	}

	UPaper2DPlusEditorTestRangeCue* Range = Cast<UPaper2DPlusEditorTestRangeCue>(CreatePlacement(
		Asset, UPaper2DPlusEditorTestRangeCue::StaticClass(), 4, 6));
	if (TestNotNull(TEXT("Concrete Range placement created"), Range))
	{
		Range->FrameCount = 20;
		ClampPlacementToAnimation(Range, 6);
		TestEqual(TEXT("Range start remains on its key frame"), Range->StartFrame, 4);
		TestEqual(TEXT("Range end clamps to animation bounds"), Range->FrameCount, 2);

		Range->StartFrame = -10;
		Range->FrameCount = 20;
		ClampPlacementToAnimation(Range, 6);
		TestEqual(TEXT("Range start clamps to zero"), Range->StartFrame, 0);
		TestEqual(TEXT("Range can span at most the animation"), Range->FrameCount, 6);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCuePlacementIdentityResolutionTest,
	"Paper2DPlus.FrameCues.Editor.Authoring.PlacementIdentitySurvivesCallbackReorder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCuePlacementIdentityResolutionTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusFrameCueEditorAuthoring;
	UPaper2DPlusCharacterProfileAsset* Asset = Paper2DPlusFrameCueEditorTest::MakeAsset();
	TArray<TObjectPtr<UPaper2DPlusCueBase>>& Cues =
		Asset->Flipbooks[0].FrameEventData.FrameCues;
	UPaper2DPlusCueBase* First = CreatePlacement(
		Asset, UPaper2DPlusEditorTestMomentCue::StaticClass(), 0, 6);
	UPaper2DPlusCueBase* Target = CreatePlacement(
		Asset, UPaper2DPlusEditorTestRangeCue::StaticClass(), 1, 6);
	UPaper2DPlusCueBase* InsertedByCallback = CreatePlacement(
		Asset, UPaper2DPlusEditorTestMomentCue::StaticClass(), 2, 6);
	Cues = { First, Target };
	const TWeakObjectPtr<UPaper2DPlusCueBase> TargetIdentity(Target);

	// Simulate a creator preview callback mutating the source before the destructive action resumes.
	Cues.Insert(InsertedByCallback, 0);
	const int32 ResolvedIndex = ResolvePlacementIndex(Cues, TargetIdentity);
	TestEqual(TEXT("Target identity re-resolves after an insertion"), ResolvedIndex, 2);
	if (Cues.IsValidIndex(ResolvedIndex)) Cues.RemoveAt(ResolvedIndex);
	TestFalse(TEXT("Only the intended target is removed"), Cues.Contains(Target));
	TestTrue(TEXT("Original neighbor survives"), Cues.Contains(First));
	TestTrue(TEXT("Callback insertion survives"), Cues.Contains(InsertedByCallback));
	TestEqual(TEXT("An already-removed identity resolves safely to none"),
		ResolvePlacementIndex(Cues, TargetIdentity), INDEX_NONE);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCuePlacementUndoTest,
	"Paper2DPlus.FrameCues.Editor.Authoring.UndoRedoNestedPlacement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCuePlacementUndoTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusFrameCueEditorAuthoring;
	if (!TestNotNull(TEXT("GEditor available"), GEditor))
	{
		return false;
	}

	GEditor->ResetTransaction(LOCTEXT("ResetCueTransactions", "Frame Cue Transaction Test Reset"));
	UPaper2DPlusCharacterProfileAsset* Asset = Paper2DPlusFrameCueEditorTest::MakeAsset();
	Asset->AddToRoot();
	// Undo may reconstruct/reallocate nested USTRUCT arrays. Re-resolve the live array for every
	// assertion/action exactly like the editor does; retaining a reference across undo is invalid.
	const auto GetCues = [Asset]() -> TArray<TObjectPtr<UPaper2DPlusCueBase>>&
	{
		return Asset->Flipbooks[0].FrameEventData.FrameCues;
	};

	{
		FScopedTransaction Transaction(LOCTEXT("AddCue", "Add Frame Cue"));
		Asset->Modify();
		GetCues().Add(CreatePlacement(Asset, UPaper2DPlusEditorTestRangeCue::StaticClass(), 1, 6));
	}
	TestEqual(TEXT("Add creates one placement"), GetCues().Num(), 1);
	TestTrue(TEXT("Undo add succeeds"), GEditor->UndoTransaction(true));
	TestEqual(TEXT("Undo removes nested placement"), GetCues().Num(), 0);
	TestTrue(TEXT("Redo add succeeds"), GEditor->RedoTransaction());
	TestEqual(TEXT("Redo restores nested placement"), GetCues().Num(), 1);

	UPaper2DPlusCueState* Range = GetCues().IsValidIndex(0) ? Cast<UPaper2DPlusCueState>(GetCues()[0]) : nullptr;
	if (TestNotNull(TEXT("Restored placement remains typed"), Range))
	{
		TestTrue(TEXT("Restored placement keeps the asset outer"), Range->GetOuter() == Asset);
		{
			FScopedTransaction Transaction(LOCTEXT("ResizeCue", "Resize Frame Cue"));
			Asset->Modify();
			Range->Modify();
			Range->FrameCount = 5;
		}
		TestEqual(TEXT("Gesture edit applies"), Range->FrameCount, 5);
		TestTrue(TEXT("Undo gesture edit succeeds"), GEditor->UndoTransaction(true));
		TestEqual(TEXT("Undo restores cue subobject data"), Range->FrameCount, 1);
		TestTrue(TEXT("Redo gesture edit succeeds"), GEditor->RedoTransaction());
		TestEqual(TEXT("Redo restores cue subobject data"), Range->FrameCount, 5);
	}

	{
		FScopedTransaction Transaction(LOCTEXT("DuplicateCue", "Duplicate Frame Cue"));
		Asset->Modify();
		GetCues().Add(DuplicatePlacement(Asset, GetCues()[0], 6));
	}
	TestEqual(TEXT("Duplicate creates a second placement"), GetCues().Num(), 2);
	TestTrue(TEXT("Undo duplicate succeeds"), GEditor->UndoTransaction(true));
	TestEqual(TEXT("Undo duplicate restores one placement"), GetCues().Num(), 1);
	TestTrue(TEXT("Redo duplicate succeeds"), GEditor->RedoTransaction());
	TestEqual(TEXT("Redo duplicate restores two placements"), GetCues().Num(), 2);
	if (GetCues().IsValidIndex(1) && GetCues()[1])
	{
		TestTrue(TEXT("Redone duplicate retains the asset outer"), GetCues()[1]->GetOuter() == Asset);
		TestTrue(TEXT("Redone duplicate is independent"), GetCues()[1] != GetCues()[0]);
	}

	GEditor->ResetTransaction(LOCTEXT("EndCueTransactions", "Frame Cue Transaction Test End"));
	Asset->RemoveFromRoot();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCueControllerCrudUndoTest,
	"Paper2DPlus.FrameCues.Editor.Authoring.ControllerCrudUndoNeverRetainsDeletedIndex",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCueControllerCrudUndoTest::RunTest(const FString& Parameters)
{
	if (!TestNotNull(TEXT("GEditor available"), GEditor)) return false;
	GEditor->ResetTransaction(LOCTEXT("ResetControllerCrudTransactions", "Controller CRUD Test Reset"));
	UPaper2DPlusCharacterProfileAsset* Asset = Paper2DPlusFrameCueEditorTest::MakeAsset();
	Asset->AddToRoot();
	Asset->Flipbooks[0].Identity.FlipbookName = TEXT("ControllerCrud");
	Asset->Flipbooks[0].Identity.Flipbook =
		Paper2DPlusFrameCueEditorTest::MakeFlipbook(6, Asset);
	TSharedPtr<FCharacterProfileEditorModel> Model = MakeShared<FCharacterProfileEditorModel>();
	Model->InitializeFromAsset(Asset);
	Model->SetSelectedFlipbook(0);
	Model->SetSelectedFrame(2);
	TSharedPtr<SFrameEventEditor> Editor = SNew(SFrameEventEditor)
		.Model(Model)
		.HostContract(FProfileToolPanelHostContract::Embedded());

	// AddCueForTests routes through the production readiness gate, which only a durably saved Cue
	// Type can pass. See FPaper2DPlusEditorTestPlaceableCueType for why a native editor-module
	// fixture class can never be Ready.
	FPaper2DPlusEditorTestPlaceableCueType MomentType(
		EPaper2DPlusFrameCueTypeKind::Moment, TEXT("ControllerCrudMoment"));
	FPaper2DPlusEditorTestPlaceableCueType RangeType(
		EPaper2DPlusFrameCueTypeKind::Range, TEXT("ControllerCrudRange"));
	if (!MomentType.IsReady() || !RangeType.IsReady())
	{
		AddError(MomentType.IsReady() ? RangeType.GetError() : MomentType.GetError());
		Editor.Reset();
		GEditor->ResetTransaction(LOCTEXT("EndControllerCrudTransactions", "Controller CRUD Test End"));
		Asset->RemoveFromRoot();
		return false;
	}

	Editor->AddCueForTests(MomentType.GetCueClass());
	UPaper2DPlusCueBase* Moment = Editor->GetSelectedCueForTests();
	TestEqual(TEXT("controller adds one Cue"), Editor->GetCueCountForTests(), 1);
	TestTrue(TEXT("new Cue is selected by stable identity"),
		Moment && Moment->GetClass() == MomentType.GetCueClass());
	TestTrue(TEXT("new Cue keeps the profile outer"), Moment && Moment->GetOuter() == Asset);
	TestFalse(TEXT("add closes its transaction"), Editor->HasActiveTransaction());

	Editor->AddCueForTests(RangeType.GetCueClass());
	UPaper2DPlusCueBase* Range = Editor->GetSelectedCueForTests();
	TestEqual(TEXT("controller adds one Cue State"), Editor->GetCueCountForTests(), 2);
	TestTrue(TEXT("new Cue State is selected by stable identity"),
		Range && Range->GetClass() == RangeType.GetCueClass());
	Editor->DuplicateSelectedCueForTests();
	UPaper2DPlusCueBase* Duplicate = Editor->GetSelectedCueForTests();
	TestEqual(TEXT("controller duplicate adds one placement"), Editor->GetCueCountForTests(), 3);
	TestTrue(TEXT("duplicate has independent identity"), Duplicate && Duplicate != Range);
	TestTrue(TEXT("duplicate remains directly asset-owned"), Duplicate && Duplicate->GetOuter() == Asset);

	Editor->RemoveSelectedCueForTests();
	TestEqual(TEXT("controller remove deletes exactly the selected placement"),
		Editor->GetCueCountForTests(), 2);
	TestNull(TEXT("deleted selection clears instead of retaining its former array index"),
		Editor->GetSelectedCueForTests());
	TestEqual(TEXT("deleted selection exposes INDEX_NONE"),
		Editor->GetSelectedCueIndexForTests(), INDEX_NONE);
	TestTrue(TEXT("one Undo restores the removed nested placement"), GEditor->UndoTransaction(true));
	TestEqual(TEXT("Undo restore returns the authored placement count"),
		Editor->GetCueCountForTests(), 3);
	TestNull(TEXT("Undo does not bind Details to an arbitrary replacement array index"),
		Editor->GetSelectedCueForTests());
	TestTrue(TEXT("one Redo removes the restored placement again"), GEditor->RedoTransaction());
	TestEqual(TEXT("Redo returns the post-remove count"), Editor->GetCueCountForTests(), 2);
	TestFalse(TEXT("CRUD and undo/redo leave no transaction live"), Editor->HasActiveTransaction());

	Editor.Reset();
	GEditor->ResetTransaction(LOCTEXT("EndControllerCrudTransactions", "Controller CRUD Test End"));
	// Drop every placement of the fixture Cue Types before their packages unload below.
	Asset->Flipbooks[0].FrameEventData.FrameCues.Reset();
	Asset->RemoveFromRoot();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCueDetailsClampTransactionTest,
	"Paper2DPlus.FrameCues.Editor.Authoring.DetailsClampSharesPropertyTransaction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCueDetailsClampTransactionTest::RunTest(const FString& Parameters)
{
	if (!TestNotNull(TEXT("GEditor available"), GEditor)) return false;
	GEditor->ResetTransaction(LOCTEXT("ResetDetailsClampTransactions", "Details Clamp Test Reset"));

	UPaper2DPlusCharacterProfileAsset* Asset = Paper2DPlusFrameCueEditorTest::MakeAsset();
	Asset->AddToRoot();
	Asset->Flipbooks[0].Identity.Flipbook =
		Paper2DPlusFrameCueEditorTest::MakeFlipbook(6, Asset);
	UPaper2DPlusEditorTestRangeCue* Range =
		NewObject<UPaper2DPlusEditorTestRangeCue>(Asset, NAME_None, RF_Transactional);
	Range->StartFrame = 4;
	Range->FrameCount = 2;
	Asset->Flipbooks[0].FrameEventData.FrameCues.Add(Range);

	TSharedPtr<FCharacterProfileEditorModel> Model = MakeShared<FCharacterProfileEditorModel>();
	Model->InitializeFromAsset(Asset);
	TSharedPtr<SFrameEventEditor> Editor = SNew(SFrameEventEditor).Model(Model);
	Editor->SelectCueForTests(0);

	FProperty* StartProperty = FindFProperty<FProperty>(
		UPaper2DPlusCueState::StaticClass(),
		GET_MEMBER_NAME_CHECKED(UPaper2DPlusCueState, StartFrame));
	{
		FScopedTransaction Transaction(LOCTEXT("DetailsClampTransaction", "Edit Cue State Start"));
		Asset->Modify();
		Range->Modify();
		Range->StartFrame = 5;
		FPropertyChangedEvent ChangeEvent(StartProperty, EPropertyChangeType::ValueSet);
		Editor->NotifyPostChange(ChangeEvent, StartProperty);
	}
	Editor->FinishEventDetailsPropertyChangeForTests();
	TestEqual(TEXT("Details edit applies the requested start"), Range->StartFrame, 5);
	TestEqual(TEXT("Dependent duration clamps inside the same transaction"), Range->FrameCount, 1);

	TestTrue(TEXT("One Undo restores the full pre-edit placement"), GEditor->UndoTransaction(true));
	TestEqual(TEXT("Undo restores start"), Range->StartFrame, 4);
	TestEqual(TEXT("Undo restores duration"), Range->FrameCount, 2);
	TestTrue(TEXT("One Redo reapplies the edit and clamp"), GEditor->RedoTransaction());
	TestEqual(TEXT("Redo reapplies start"), Range->StartFrame, 5);
	TestEqual(TEXT("Redo reapplies duration"), Range->FrameCount, 1);

	Editor.Reset();
	GEditor->ResetTransaction(LOCTEXT("EndDetailsClampTransactions", "Details Clamp Test End"));
	Asset->RemoveFromRoot();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCueTimelineKeyboardAuthoringTest,
	"Paper2DPlus.FrameCues.Editor.Authoring.TimelineKeyboardTransactionsAndAccessibility",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCueTimelineKeyboardAuthoringTest::RunTest(const FString& Parameters)
{
	if (!TestNotNull(TEXT("GEditor available"), GEditor)) return false;
	GEditor->ResetTransaction(LOCTEXT("ResetTimelineKeyboardTransactions", "Timeline Keyboard Test Reset"));

	UPaper2DPlusCharacterProfileAsset* Asset = Paper2DPlusFrameCueEditorTest::MakeAsset();
	Asset->AddToRoot();
	Asset->Flipbooks[0].Identity.Flipbook =
		Paper2DPlusFrameCueEditorTest::MakeFlipbook(6, Asset);
	UPaper2DPlusEditorTestRangeCue* Range =
		NewObject<UPaper2DPlusEditorTestRangeCue>(Asset, NAME_None, RF_Transactional);
	Range->StartFrame = 1;
	Range->FrameCount = 2;
	// Keep the authored identity neutral so the retired-wording assertion below measures only
	// framework-owned accessibility copy, not a user-authored Cue Type or DebugName.
	Range->DebugName = TEXT("Keyboard Span");
	Asset->Flipbooks[0].FrameEventData.FrameCues.Add(Range);

	TSharedPtr<SFrameEventTimelineTrack> Timeline =
		SNew(SFrameEventTimelineTrack)
		.Asset(TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset>(Asset))
		.SelectedFlipbookIndex(0)
		.SelectedEventIndex(0)
		.SelectedFrameIndex(0);
	TUniquePtr<FScopedTransaction> ActiveTransaction;
	int32 TransactionStarts = 0;
	int32 TransactionEnds = 0;
	Timeline->OnEventDragStarted.BindLambda([&](int32 CueIndex)
	{
		if (!Asset->Flipbooks[0].FrameEventData.FrameCues.IsValidIndex(CueIndex)) return;
		++TransactionStarts;
		ActiveTransaction = MakeUnique<FScopedTransaction>(
			LOCTEXT("TimelineKeyboardEdit", "Keyboard Edit Frame Cue"));
		Asset->Modify();
		Asset->Flipbooks[0].FrameEventData.FrameCues[CueIndex]->Modify();
	});
	Timeline->OnEventDragEnded.BindLambda([&]()
	{
		++TransactionEnds;
		ActiveTransaction.Reset();
	});
	Timeline->OnEventFrameChanged.BindLambda([&](int32 CueIndex, int32 NewFrame)
	{
		if (Asset->Flipbooks[0].FrameEventData.FrameCues.IsValidIndex(CueIndex))
		{
			Asset->Flipbooks[0].FrameEventData.FrameCues[CueIndex]->SetPrimaryAnchorFrame(NewFrame);
		}
	});
	Timeline->OnEventDurationChanged.BindLambda([&](int32 CueIndex, int32 NewFrameCount)
	{
		if (Asset->Flipbooks[0].FrameEventData.FrameCues.IsValidIndex(CueIndex))
		{
			if (UPaper2DPlusCueState* CurrentRange = Cast<UPaper2DPlusCueState>(
				Asset->Flipbooks[0].FrameEventData.FrameCues[CueIndex]))
			{
				CurrentRange->FrameCount = NewFrameCount;
			}
		}
	});

	TestTrue(TEXT("Timeline accepts keyboard focus"), Timeline->SupportsKeyboardFocus());
	const FString AccessibleSummary = Timeline->GetAccessibleSummaryText().ToString();
	TestTrue(TEXT("Accessible text names Cue State controls"),
		AccessibleSummary.Contains(TEXT("Selected Cue State"))
		&& AccessibleSummary.Contains(TEXT("Shift")));
	TestFalse(TEXT("Framework accessibility copy never exposes the retired type labels"),
		AccessibleSummary.Contains(TEXT("Range Cue"))
		|| AccessibleSummary.Contains(TEXT("Moment Cue")));
	TestTrue(TEXT("Right key edit is handled"), Timeline->OnKeyDown(
		FGeometry(), Paper2DPlusFrameCueEditorTest::MakeKeyEvent(EKeys::Right)).IsEventHandled());
	TestEqual(TEXT("Right moves one definitive key frame"), Range->StartFrame, 2);
	TestEqual(TEXT("Move owns one transaction start"), TransactionStarts, 1);
	TestEqual(TEXT("Move owns one transaction end"), TransactionEnds, 1);

	TestTrue(TEXT("Shift Right resize is handled"), Timeline->OnKeyDown(
		FGeometry(), Paper2DPlusFrameCueEditorTest::MakeKeyEvent(EKeys::Right, true)).IsEventHandled());
	TestEqual(TEXT("Shift Right grows the Range by one frame"), Range->FrameCount, 3);
	TestEqual(TEXT("Resize owns one additional transaction"), TransactionStarts, 2);
	TestEqual(TEXT("Resize closes that transaction"), TransactionEnds, 2);

	TestTrue(TEXT("Undo resize succeeds"), GEditor->UndoTransaction(true));
	TestEqual(TEXT("Undo resize preserves the prior move"), Range->StartFrame, 2);
	TestEqual(TEXT("Undo resize restores duration"), Range->FrameCount, 2);
	TestTrue(TEXT("Undo move succeeds"), GEditor->UndoTransaction(true));
	TestEqual(TEXT("Undo move restores start"), Range->StartFrame, 1);

	Range->StartFrame = 0;
	const int32 StartsBeforeBoundaryKey = TransactionStarts;
	TestTrue(TEXT("Boundary Left remains handled"), Timeline->OnKeyDown(
		FGeometry(), Paper2DPlusFrameCueEditorTest::MakeKeyEvent(EKeys::Left)).IsEventHandled());
	TestEqual(TEXT("A clamped no-op key creates no transaction"),
		TransactionStarts, StartsBeforeBoundaryKey);

	Timeline.Reset();
	ActiveTransaction.Reset();
	GEditor->ResetTransaction(LOCTEXT("EndTimelineKeyboardTransactions", "Timeline Keyboard Test End"));
	Asset->RemoveFromRoot();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCueFirstSpacePlaybackTest,
	"Paper2DPlus.FrameCues.Editor.Playback.FirstSpaceWorksAfterTabActivation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCueFirstSpacePlaybackTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusFrameCueEditorTest;

	if (!FSlateApplication::IsInitialized())
	{
		AddError(TEXT("Slate must be initialized to verify real keyboard-focus routing."));
		return false;
	}

	UPaper2DPlusCharacterProfileAsset* Asset = MakeAsset();
	Asset->AddToRoot();
	Asset->Flipbooks[0].Identity.Flipbook = MakeFlipbook(4, Asset);
	Asset->Flipbooks[0].CombatData.Frames.SetNum(4);

	TSharedPtr<FCharacterProfileEditorModel> Model =
		MakeShared<FCharacterProfileEditorModel>();
	Model->InitializeFromAsset(Asset);
	Model->SetSelectedFlipbook(0);

	TSharedPtr<SButton> PriorFocusTarget;
	TSharedPtr<SFrameEventEditor> Editor;
	TSharedPtr<SDockTab> FrameCuesTab;
	const FString DockToken = FGuid::NewGuid().ToString(EGuidFormats::Digits);
	const FName PriorTabId(*FString::Printf(
		TEXT("Paper2DPlusFrameCuePrior_%s"),
		*DockToken));
	const FName FrameCuesTabId(*FString::Printf(
		TEXT("Paper2DPlusFrameCueFocus_%s"),
		*DockToken));
	const TSharedRef<SDockTab> OwnerTab =
		SNew(SDockTab)
		.TabRole(ETabRole::MajorTab);
	const TSharedRef<FTabManager> TestTabManager =
		FGlobalTabmanager::Get()->NewTabManager(OwnerTab);
	TestTabManager->RegisterTabSpawner(
		PriorTabId,
		FOnSpawnTab::CreateLambda(
			[&PriorFocusTarget](const FSpawnTabArgs&)
			{
				return SNew(SDockTab)
					.TabRole(ETabRole::PanelTab)
					.Label(LOCTEXT("FirstSpacePriorTabLabel", "Prior"))
					[
						SAssignNew(PriorFocusTarget, SButton)
							.Text(LOCTEXT("PriorFocusTarget", "Prior Focus Target"))
					];
			}));
	TestTabManager->RegisterTabSpawner(
		FrameCuesTabId,
		FOnSpawnTab::CreateLambda(
			[Model, &Editor, &FrameCuesTab](const FSpawnTabArgs&)
			{
				return SAssignNew(FrameCuesTab, SDockTab)
					.TabRole(ETabRole::PanelTab)
					.Label(LOCTEXT("FirstSpaceFrameCuesTabLabel", "Frame Cues"))
					[
						SAssignNew(Editor, SFrameEventEditor)
							.Model(Model)
							.HostContract(FProfileToolPanelHostContract::External())
					];
			}));

	const TWeakPtr<SWidget> PreviousKeyboardFocus =
		FSlateApplication::Get().GetKeyboardFocusedWidget();
	const TSharedRef<SWindow> TestWindow =
		SNew(SWindow)
		.Title(LOCTEXT("FirstSpaceTestWindowTitle", "Frame Cue First Space Test"))
		.ClientSize(FVector2D(720.0f, 480.0f))
		.FocusWhenFirstShown(false)
		.SupportsMaximize(false)
		.SupportsMinimize(false);
	const TSharedRef<FTabManager::FLayout> DockLayout =
		FTabManager::NewLayout(FName(*FString::Printf(
			TEXT("Paper2DPlusFrameCueFocusLayout_%s"),
			*DockToken)))
		->AddArea(
			FTabManager::NewPrimaryArea()
			->Split(
				FTabManager::NewStack()
				->AddTab(PriorTabId, ETabState::OpenedTab)
				->AddTab(FrameCuesTabId, ETabState::OpenedTab)
				->SetForegroundTab(PriorTabId)));
	const TSharedPtr<SWidget> RestoredDocking =
		TestTabManager->RestoreFrom(DockLayout, TestWindow);
	if (!TestTrue(
		TEXT("real docking fixture restores"),
		RestoredDocking.IsValid())
		|| !TestTrue(
			TEXT("real docking fixture spawns its prior focus target"),
			PriorFocusTarget.IsValid())
		|| !TestTrue(
			TEXT("real docking fixture spawns Frame Cues"),
			Editor.IsValid())
		|| !TestTrue(
			TEXT("real docking fixture owns the Frame Cues tab"),
			FrameCuesTab.IsValid()))
	{
		TestTabManager->CloseAllAreas();
		TestTabManager->UnregisterAllTabSpawners();
		Model.Reset();
		Asset->RemoveFromRoot();
		return false;
	}
	TestWindow->SetContent(RestoredDocking.ToSharedRef());
	FSlateApplication::Get().AddWindow(
		TestWindow,
		/*bShowImmediately=*/FApp::CanEverRender());
	if (FApp::CanEverRender())
	{
		FSlateApplication::Get().Tick(ESlateTickType::TimeAndWidgets);
	}
	FSlateApplication::Get().SetKeyboardFocus(
		PriorFocusTarget,
		EFocusCause::SetDirectly);
	if (FApp::CanEverRender())
	{
		TestTrue(
			TEXT("fixture begins with keyboard focus outside the Frame Cues panel"),
			PriorFocusTarget->HasKeyboardFocus());
	}

	int32 ActivationDepth = 0;
	int32 MaximumActivationDepth = 0;
	FrameCuesTab->SetOnTabActivated(
		SDockTab::FOnTabActivatedCallback::CreateLambda(
			[Editor, &ActivationDepth, &MaximumActivationDepth](
				TSharedRef<SDockTab>,
				ETabActivationCause)
			{
				++ActivationDepth;
				MaximumActivationDepth = FMath::Max(
					MaximumActivationDepth,
					ActivationDepth);
				// Bound the broken implementation instead of allowing the test process to overflow.
				// Production invokes the controller on every activation; observing depth two here is
				// the regression signal this fixture exists to report safely.
				if (ActivationDepth == 1)
				{
					Editor->HandleHostActivated();
				}
				--ActivationDepth;
			}));

	FrameCuesTab->ActivateInParent(ETabActivationCause::SetDirectly);
	TestEqual(
		TEXT("Frame Cues activation never re-enters its tab callback synchronously"),
		MaximumActivationDepth,
		1);
	TestTrue(
		TEXT("Frame Cues activation queues a widget-owned deferred focus pass"),
		Editor->HasActiveTimers());
	if (FApp::CanEverRender())
	{
		TestTrue(
			TEXT("Frame Cues defers focus until the activating tab callback has unwound"),
			PriorFocusTarget->HasKeyboardFocus());
	}
	if (FApp::CanEverRender())
	{
		for (int32 TickIndex = 0;
			TickIndex < 3 && Editor->HasActiveTimers();
			++TickIndex)
		{
			FSlateApplication::Get().Tick(ESlateTickType::TimeAndWidgets);
		}
	}
	else
	{
		// NullRHI does not paint hidden windows, so pump this widget's production active-timer
		// delegate directly instead of letting the headless suite skip the crash regression.
		Editor->ApplyDeferredHostFocusForTests();
	}
	TestFalse(
		TEXT("the deferred focus timer executes once and drains"),
		Editor->HasActiveTimers());
	TestEqual(
		TEXT("deferred focus may reactivate the tab only after the original callback returns"),
		MaximumActivationDepth,
		1);
	const bool bSlateFocusRouteAvailable = Editor->HasKeyboardFocus();
	if (FApp::CanEverRender())
	{
		TestTrue(
			TEXT("activating Frame Cues seats keyboard focus on its playback controller"),
			bSlateFocusRouteAvailable);
	}
	TestTrue(
		TEXT("the first Space key routes through Slate to Frame Cues"),
		bSlateFocusRouteAvailable
			? FSlateApplication::Get().ProcessKeyDownEvent(MakeKeyEvent(EKeys::SpaceBar))
			: Editor->OnKeyDown(
				FGeometry(),
				MakeKeyEvent(EKeys::SpaceBar)).IsEventHandled());
	TestTrue(
		TEXT("the first Space key starts playback without a preparatory click"),
		Editor->IsPlayingForTests());

	Editor->StopPlayback();
	FrameCuesTab->SetOnTabActivated(SDockTab::FOnTabActivatedCallback());
	TestTabManager->CloseAllAreas();
	TestTabManager->UnregisterAllTabSpawners();
	FSlateApplication::Get().DestroyWindowImmediately(TestWindow);
	TestWindow->SetContent(SNullWidget::NullWidget);
	if (const TSharedPtr<SWidget> PreviousFocus = PreviousKeyboardFocus.Pin())
	{
		FSlateApplication::Get().SetKeyboardFocus(
			PreviousFocus,
			EFocusCause::SetDirectly);
	}
	else
	{
		FSlateApplication::Get().ClearKeyboardFocus(EFocusCause::Cleared);
	}
	Editor.Reset();
	PriorFocusTarget.Reset();
	Model.Reset();
	Asset->RemoveFromRoot();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCueHostFocusSeatPredicateTest,
	"Paper2DPlus.FrameCues.Editor.Playback.HostFocusSeatPredicate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCueHostFocusSeatPredicateTest::RunTest(const FString& Parameters)
{
	// Pure seat-vs-skip decisions for the deferred host-focus seat shared by the Frame Cues, Frame
	// Timing, and Root Motion tools. No Slate focus routing involved: the facts are the inputs.
	namespace Seat = Paper2DPlusEditor::SlateShortcutUtils;

	// A completed seat needs no re-seat, regardless of what the descendant facts claim.
	TestFalse(
		TEXT("panel holding keyboard focus skips the seat"),
		Seat::ShouldSeatDeferredHostFocus(true, false, FString()));
	TestFalse(
		TEXT("panel focus wins even when descendant facts are supplied"),
		Seat::ShouldSeatDeferredHostFocus(true, true, TEXT("SButton")));

	// Nothing inside the panel holds focus: the activation seat proceeds.
	TestTrue(
		TEXT("no focused descendant seats the panel"),
		Seat::ShouldSeatDeferredHostFocus(false, false, FString()));

	// Space-forwarding descendants keep focus — their unhandled Space bubbles to the panel.
	TestFalse(
		TEXT("a focused SCurveEditor keeps focus (Space bubbles to the panel)"),
		Seat::ShouldSeatDeferredHostFocus(false, true, TEXT("SCurveEditor")));
	TestFalse(
		TEXT("a focused Cue lane keeps focus (Space bubbles to the panel)"),
		Seat::ShouldSeatDeferredHostFocus(false, true, TEXT("SFrameEventTimelineTrack")));

	// A live caret must never lose its keystrokes.
	TestFalse(
		TEXT("a live SEditableText caret keeps focus"),
		Seat::ShouldSeatDeferredHostFocus(false, true, TEXT("SEditableText")));
	TestFalse(
		TEXT("an SEditableTextBox caret keeps focus"),
		Seat::ShouldSeatDeferredHostFocus(false, true, TEXT("SEditableTextBox")));
	TestFalse(
		TEXT("a multi-line caret keeps focus"),
		Seat::ShouldSeatDeferredHostFocus(false, true, TEXT("SMultiLineEditableTextBox")));

	// The regression this predicate exists for: a stale Space-swallowing descendant left focused
	// from the previous activation must be re-seated, or Space dies in that widget.
	TestTrue(
		TEXT("stale SButton focus is re-seated (Space = Accept would swallow playback)"),
		Seat::ShouldSeatDeferredHostFocus(false, true, TEXT("SButton")));
	TestTrue(
		TEXT("a merely focused search-box shell is re-seated"),
		Seat::ShouldSeatDeferredHostFocus(false, true, TEXT("SSearchBox")));
	TestTrue(
		TEXT("a merely focused spinbox shell is re-seated"),
		Seat::ShouldSeatDeferredHostFocus(false, true, TEXT("SSpinBox")));
	TestTrue(
		TEXT("an unresolvable focused-widget type is treated as stale and re-seated"),
		Seat::ShouldSeatDeferredHostFocus(false, true, FString()));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCuePreviewMutationTeardownTest,
	"Paper2DPlus.FrameCues.Editor.Authoring.ActiveRangeMutationTeardown",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCuePreviewMutationTeardownTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusFrameCueEditorAuthoring;
	UPaper2DPlusCharacterProfileAsset* Asset = Paper2DPlusFrameCueEditorTest::MakeAsset();
	UPaper2DPlusCueBase* Range = CreatePlacement(
		Asset, UPaper2DPlusEditorTestRangeCue::StaticClass(), 1, 6);
	if (!TestNotNull(TEXT("Range placement created"), Range))
	{
		return false;
	}

	FPaper2DPlusFrameCuePreviewHost PreviewHost;
	UPaper2DPlusFrameCuePreviewContext* PreviewContext = PreviewHost.GetContext();
	UPaperFlipbook* PreviewFlipbook =
		Paper2DPlusFrameCueEditorTest::MakeFlipbook(6, Asset);
	Asset->Flipbooks[0].Identity.FlipbookName = TEXT("MutationPreview");
	Asset->Flipbooks[0].Identity.Flipbook = PreviewFlipbook;
	Asset->Flipbooks[0].CombatData.Frames.SetNum(6);
	PreviewHost.SyncSubject(
		Asset,
		PreviewFlipbook,
		2,
		TEXT("MutationPreview"));
	TSet<TWeakObjectPtr<UPaper2DPlusCueBase>> ActiveRanges;
	ActiveRanges.Add(Range);
	PreviewContext->SpawnProjectileProxy(FVector2D::ZeroVector, FVector2D(100.0, 0.0));
	PreviewContext->ShowOverlay(FLinearColor::Red, 1.0f);
	TestEqual(TEXT("Fixture owns two preview resources"), PreviewContext->GetOwnedResourceCount(), 2);

	FPaper2DPlusFrameCueContext EndContext;
	TestTrue(TEXT("Active range mutation emits End"), EndActiveRangePreview(
		Range,
		EPaper2DPlusFrameCueEndReason::CueMutation,
		2,
		ActiveRanges,
		&PreviewHost,
		&EndContext,
		/*bResetPreviewHost=*/false,
		EPaper2DPlusFrameCueEvaluationMode::EditorSelectionPreview));
	TestEqual(TEXT("Mutation phase is End"), EndContext.Phase, EPaper2DPlusFrameCuePhase::End);
	TestEqual(TEXT("Mutation reason is CueMutation"), EndContext.EndReason, EPaper2DPlusFrameCueEndReason::CueMutation);
	TestEqual(TEXT("Mutation End retains its selection-preview mode"),
		EndContext.EvaluationMode,
		EPaper2DPlusFrameCueEvaluationMode::EditorSelectionPreview);
	TestTrue(TEXT("Mutation End keeps legacy editor-preview compatibility"),
		EndContext.bIsEditorPreview && !EndContext.bIsCatchUp);
	TestTrue(TEXT("Mutation End carries the exact preview Profile"),
		EndContext.CharacterProfile == Asset);
	TestTrue(TEXT("Mutation End carries the registered live render component"),
		EndContext.PlaybackComponent == PreviewHost.GetFlipbookComponent()
			&& EndContext.PlaybackComponent
			&& EndContext.PlaybackComponent->IsRegistered());
	TestEqual(TEXT("Mutation End retains the last usable source frame"),
		EndContext.PreviousFrame, 2);
	TestEqual(TEXT("Mutation clears the active set"), ActiveRanges.Num(), 0);
	PreviewHost.Reset();
	TestEqual(TEXT("Mutation leaves a zero-resource ledger"), PreviewContext->GetOwnedResourceCount(), 0);
	TestFalse(TEXT("Repeating the same teardown cannot emit a second End"), EndActiveRangePreview(
		Range,
		EPaper2DPlusFrameCueEndReason::CueMutation,
		2,
		ActiveRanges,
		&PreviewHost));

	ActiveRanges.Add(Range);
	PreviewContext->ShowOverlay(FLinearColor::White, 1.0f);
	FPaper2DPlusFrameCueContext RemovedContext;
	TestTrue(TEXT("Removing an active range emits End"), EndActiveRangePreview(
		Range,
		EPaper2DPlusFrameCueEndReason::SourceRemoved,
		2,
		ActiveRanges,
		&PreviewHost,
		&RemovedContext));
	TestEqual(TEXT("Removal reason is SourceRemoved"), RemovedContext.EndReason, EPaper2DPlusFrameCueEndReason::SourceRemoved);
	TestEqual(TEXT("Removal clears active range state"), ActiveRanges.Num(), 0);
	TestEqual(TEXT("Removal leaves a zero-resource ledger"), PreviewContext->GetOwnedResourceCount(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCueDetailsGestureRefreshGateTest,
	"Paper2DPlus.FrameCues.Editor.Authoring.DetailsGesturePendsModelRefresh",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCueDetailsGestureRefreshGateTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = Paper2DPlusFrameCueEditorTest::MakeAsset();
	TSharedPtr<FCharacterProfileEditorModel> Model = MakeShared<FCharacterProfileEditorModel>();
	Model->InitializeFromAsset(Asset);
	TSharedPtr<SFrameEventEditor> Editor = SNew(SFrameEventEditor).Model(Model);
	FPropertyChangedEvent InteractiveChange(nullptr, EPropertyChangeType::Interactive);

	Editor->NotifyPreChange(nullptr);
	TestTrue(TEXT("details pre-change raises the shared transaction/rebuild gate"),
		Editor->HasActiveTransaction());
	Model->NotifyAssetDataChanged();
	TestTrue(TEXT("model data callback pends instead of rebuilding under the details gesture"),
		Editor->HasPendingRefreshForTests());
	Editor->NotifyPostChange(InteractiveChange, nullptr);
	TestTrue(TEXT("interactive post-change keeps the details gesture gate raised"),
		Editor->HasActiveTransaction());
	Editor->FinishEventDetailsPropertyChangeForTests();
	TestFalse(TEXT("finished change lowers the details gesture gate"),
		Editor->HasActiveTransaction());
	TestFalse(TEXT("finished change flushes the pended model refresh"),
		Editor->HasPendingRefreshForTests());

	Editor->NotifyPreChange(nullptr);
	Model->OnAssetExternallyModified.Broadcast();
	TestTrue(TEXT("external-modify callback also pends during a details gesture"),
		Editor->HasPendingRefreshForTests());
	Editor->FinishEventDetailsPropertyChangeForTests();
	TestFalse(TEXT("external-modify pend flushes exactly at finished change"),
		Editor->HasPendingRefreshForTests());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCuePendingPlacementExactTargetTest,
	"Paper2DPlus.FrameCues.Editor.Authoring.PendingPlacementExactTargetOneShotAndUndo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCuePendingPlacementExactTargetTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusFrameCuePlacementAuthoring;
	if (!TestNotNull(TEXT("GEditor available"), GEditor))
	{
		return false;
	}

	GEditor->ResetTransaction(LOCTEXT(
		"ResetPendingPlacementTransactions", "Pending Placement Test Reset"));
	UPaper2DPlusCharacterProfileAsset* Asset = Paper2DPlusFrameCueEditorTest::MakeAsset();
	Asset->AddToRoot();
	Asset->Flipbooks[0].Identity.FlipbookName = TEXT("CapturedAttack");
	Asset->Flipbooks[0].Identity.Flipbook =
		Paper2DPlusFrameCueEditorTest::MakeFlipbook(6, Asset);
	Asset->Flipbooks.AddDefaulted();
	Asset->Flipbooks[1].Identity.FlipbookName = TEXT("LaterSelection");
	Asset->Flipbooks[1].Identity.Flipbook =
		Paper2DPlusFrameCueEditorTest::MakeFlipbook(3, Asset);

	// Commit routes through the production readiness gate; only a durably saved Cue Type passes it.
	FPaper2DPlusEditorTestPlaceableCueType MomentType(
		EPaper2DPlusFrameCueTypeKind::Moment, TEXT("PendingPlacementExactTarget"));
	if (!MomentType.IsReady())
	{
		AddError(MomentType.GetError());
		Asset->RemoveFromRoot();
		return false;
	}

	TSharedPtr<FCharacterProfileEditorModel> Model = MakeShared<FCharacterProfileEditorModel>();
	Model->InitializeFromAsset(Asset);
	Model->SetSelectedFlipbook(0);
	Model->SetSelectedFrame(4);
	TSharedPtr<FProfileFrameCueDataProvider> Provider =
		MakeShared<FProfileFrameCueDataProvider>(Model);
	const FProfileScopedAnimationIdentity CapturedScope = Provider->GetScopedAnimationIdentity(0);
	const FGuid ImpactTrackId = Provider->AddTrack(CapturedScope, TEXT("Impact"));
	TestTrue(TEXT("optional target track is authored before placement capture"), ImpactTrackId.IsValid());
	const FPaper2DPlusFrameCuePlacementTarget Target = CaptureTarget(
		Provider,
		CapturedScope,
		4,
		ImpactTrackId);
	TestTrue(TEXT("captured Profile target is immutable and valid"), Target.IsValid());
	TestEqual(TEXT("captured target retains exact optional track identity"),
		Target.CapturedTrackId, ImpactTrackId);

	TSharedRef<FPaper2DPlusFrameCuePendingPlacement> Pending =
		FPaper2DPlusFrameCuePendingPlacement::Create(
			Target,
			EPaper2DPlusFrameCuePlacementKind::Moment,
			/*bReadyToPlace*/ true);

	// The request must no longer depend on the mutable selected animation or on the editor model/provider
	// remaining alive after the Cue Type editor opens in another toolkit.
	Model->SetSelectedFlipbook(1);
	Model->SetSelectedFrame(1);
	Provider.Reset();
	Model.Reset();

	const FPaper2DPlusFrameCuePlacementResult Result = Pending->Commit(MomentType.GetCueClass());
	TestEqual(TEXT("ready request commits successfully"),
		Result.Status, EPaper2DPlusFrameCuePlacementStatus::Success);
	TestEqual(TEXT("success consumes the request"),
		Pending->GetState(), EPaper2DPlusFrameCuePlacementRequestState::PlacedConsumed);
	TestTrue(TEXT("success returns a stable placement identity"),
		Result.PlacementIdentity.IsValid());
	TestEqual(TEXT("exactly one placement lands on the captured animation"),
		Asset->Flipbooks[0].FrameEventData.FrameCues.Num(), 1);
	TestEqual(TEXT("later selected animation receives no placement"),
		Asset->Flipbooks[1].FrameEventData.FrameCues.Num(), 0);
	UPaper2DPlusCue* Placed = Asset->Flipbooks[0].FrameEventData.FrameCues.IsValidIndex(0)
		? Cast<UPaper2DPlusCue>(Asset->Flipbooks[0].FrameEventData.FrameCues[0])
		: nullptr;
	TestTrue(TEXT("placed Cue retains the saved class"),
		Placed && Placed->GetClass() == MomentType.GetCueClass());
	TestEqual(TEXT("placed Cue retains the captured frame"),
		Placed ? Placed->TriggerFrame : INDEX_NONE, 4);
	TestEqual(TEXT("placed Cue lands on the exact captured track"),
		Asset->Flipbooks[0].FrameEventData.CueTrackLayout.ResolveStoredTrackId(Placed),
		ImpactTrackId);

	const FPaper2DPlusFrameCuePlacementResult DuplicateCompletion =
		Pending->Commit(MomentType.GetCueClass());
	TestEqual(TEXT("post-consumption callback is rejected"),
		DuplicateCompletion.Status, EPaper2DPlusFrameCuePlacementStatus::AlreadyConsumed);
	TestEqual(TEXT("post-consumption callback cannot duplicate the placement"),
		Asset->Flipbooks[0].FrameEventData.FrameCues.Num(), 1);

	TestTrue(TEXT("placement transaction alone can be undone"),
		GEditor->UndoTransaction(true));
	TestEqual(TEXT("Undo removes only the placement"),
		Asset->Flipbooks[0].FrameEventData.FrameCues.Num(), 0);
	TestTrue(TEXT("Undo removes the placement membership key"),
		Asset->Flipbooks[0].FrameEventData.CueTrackLayout.CueTrackIds.IsEmpty());
	TestTrue(TEXT("placement transaction can be redone"), GEditor->RedoTransaction());
	TestEqual(TEXT("Redo restores exactly one placement"),
		Asset->Flipbooks[0].FrameEventData.FrameCues.Num(), 1);
	TestEqual(TEXT("Redo restores exact target-track membership"),
		Asset->Flipbooks[0].FrameEventData.CueTrackLayout.ResolveStoredTrackId(
			Asset->Flipbooks[0].FrameEventData.FrameCues[0]),
		ImpactTrackId);

	GEditor->ResetTransaction(LOCTEXT(
		"EndPendingPlacementTransactions", "Pending Placement Test End"));
	// Drop placements of the fixture Cue Type before its package unloads.
	Asset->Flipbooks[0].FrameEventData.FrameCues.Reset();
	Asset->RemoveFromRoot();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCuePendingPlacementStaleTrackTest,
	"Paper2DPlus.FrameCues.Editor.Authoring.PendingPlacementRejectsDeletedTrack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCuePendingPlacementStaleTrackTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusFrameCuePlacementAuthoring;
	UPaper2DPlusCharacterProfileAsset* Asset = Paper2DPlusFrameCueEditorTest::MakeAsset();
	Asset->AddToRoot();
	Asset->Flipbooks[0].Identity.FlipbookName = TEXT("StaleTrack");
	Asset->Flipbooks[0].Identity.Flipbook =
		Paper2DPlusFrameCueEditorTest::MakeFlipbook(4, Asset);
	// The stale-target status is only reachable with a Cue class the readiness gate accepts;
	// an unready class would short-circuit to InvalidCueClass and prove nothing about the track.
	FPaper2DPlusEditorTestPlaceableCueType MomentType(
		EPaper2DPlusFrameCueTypeKind::Moment, TEXT("PendingPlacementStaleTrack"));
	if (!MomentType.IsReady())
	{
		AddError(MomentType.GetError());
		Asset->RemoveFromRoot();
		return false;
	}
	TSharedPtr<FCharacterProfileEditorModel> Model = MakeShared<FCharacterProfileEditorModel>();
	Model->InitializeFromAsset(Asset);
	TSharedPtr<FProfileFrameCueDataProvider> Provider =
		MakeShared<FProfileFrameCueDataProvider>(Model);
	const FProfileScopedAnimationIdentity Scope = Provider->GetScopedAnimationIdentity(0);
	const FGuid TrackId = Provider->AddTrack(Scope, TEXT("Temporary"));
	const FPaper2DPlusFrameCuePlacementTarget Target = CaptureTarget(
		Provider, Scope, 2, TrackId);
	TestTrue(TEXT("target captures the existing optional track"), Target.IsValid());
	TestTrue(TEXT("track can be deleted while Cue Type authoring remains open"),
		Provider->RemoveTrack(
			Scope,
			TrackId,
			EPaper2DPlusFrameCueTrackRemovalMode::MoveCuesToDefault));

	const TSharedRef<FPaper2DPlusFrameCuePendingPlacement> Pending =
		FPaper2DPlusFrameCuePendingPlacement::Create(
			Target,
			EPaper2DPlusFrameCuePlacementKind::Moment,
			/*bReadyToPlace=*/ true);
	const FPaper2DPlusFrameCuePlacementResult Result = Pending->Commit(MomentType.GetCueClass());
	TestEqual(TEXT("deleted optional track is a stale target, never a Default fallback"),
		Result.Status, EPaper2DPlusFrameCuePlacementStatus::TargetUnavailable);
	TestTrue(TEXT("stale target creates no placement"),
		Asset->Flipbooks[0].FrameEventData.FrameCues.IsEmpty());
	Asset->RemoveFromRoot();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCueInlineActionModelTest,
	"Paper2DPlus.FrameCues.Editor.Authoring.InlineActionModelAndEditRouting",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCueInlineActionModelTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusFrameCueEditorAuthoring;

	const TArray<FText> CreateActions = GetCreateCueTypeActionLabels();
	TestEqual(TEXT("inline picker exposes exactly one Cue Type creation action"),
		CreateActions.Num(), 1);
	if (CreateActions.IsValidIndex(0))
	{
		TestEqual(TEXT("the one creation action uses Cue-specific language"),
			CreateActions[0].ToString(), FString(TEXT("New Frame Cue Type...")));
		TestFalse(TEXT("the creation action never exposes Unreal's generic Blueprint workflow"),
			CreateActions[0].ToString().Contains(TEXT("Blueprint")));
		TestNotEqual(TEXT("the removed generic Moment Blueprint action is absent"),
			CreateActions[0].ToString(), FString(TEXT("New Cue Blueprint...")));
		TestNotEqual(TEXT("the removed generic Range Blueprint action is absent"),
			CreateActions[0].ToString(), FString(TEXT("New Cue State Blueprint...")));
	}

	UBlueprint* ResolvedBlueprint = nullptr;
	TestEqual(TEXT("a native Cue class exposes no Edit Cue Type route"),
		ResolveCueTypeEditRoute(
			UPaper2DPlusEditorTestMomentCue::StaticClass(), ResolvedBlueprint),
		EPaper2DPlusFrameCueTypeEditRoute::None);
	TestNull(TEXT("native edit classification returns no Blueprint"), ResolvedBlueprint);

	Paper2DPlusFrameCueEditorTest::FUnsavedCueTypeFixture Specialized(*this);
	if (TestNotNull(TEXT("specialized Cue Type fixture exists"), Specialized.CueType)
		&& TestNotNull(TEXT("specialized Cue Type has a generated class"),
			Specialized.CueType ? Specialized.CueType->GeneratedClass.Get() : nullptr))
	{
		ResolvedBlueprint = nullptr;
		TestEqual(TEXT("specialized custom Cue placement routes to the restricted editor"),
			ResolveCueTypeEditRoute(Specialized.CueType->GeneratedClass, ResolvedBlueprint),
			EPaper2DPlusFrameCueTypeEditRoute::RestrictedSpecialized);
		TestTrue(TEXT("specialized edit route returns its exact owning asset"),
			ResolvedBlueprint == Specialized.CueType);
	}

	const FName LegacyName = MakeUniqueObjectName(
		GetTransientPackage(), UBlueprint::StaticClass(), TEXT("BP_LegacyInlineCue"));
	UBlueprint* Legacy = FKismetEditorUtilities::CreateBlueprint(
		UPaper2DPlusCue::StaticClass(),
		GetTransientPackage(),
		LegacyName,
		BPTYPE_Normal,
		UBlueprint::StaticClass(),
		UBlueprintGeneratedClass::StaticClass(),
		FName(TEXT("Paper2DPlusFrameCueInlineActionModelTest")));
	if (TestNotNull(TEXT("generic legacy Cue Blueprint fixture exists"), Legacy)
		&& TestNotNull(TEXT("legacy Cue Blueprint has a generated class"),
			Legacy ? Legacy->GeneratedClass.Get() : nullptr))
	{
		ResolvedBlueprint = nullptr;
		TestEqual(TEXT("generic legacy Cue Types are classified for explicit recovery, not normal Edit"),
			ResolveCueTypeEditRoute(Legacy->GeneratedClass, ResolvedBlueprint),
			EPaper2DPlusFrameCueTypeEditRoute::LegacyRecoveryRequired);
		TestTrue(TEXT("legacy recovery classification retains the exact owning Blueprint"),
			ResolvedBlueprint == Legacy);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCuePendingPlacementTerminalStateTest,
	"Paper2DPlus.FrameCues.Editor.Authoring.PendingPlacementTerminalStatesDefaultsAndReadiness",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCuePendingPlacementTerminalStateTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusFrameCuePlacementAuthoring;

	UPaper2DPlusCharacterProfileAsset* Asset = Paper2DPlusFrameCueEditorTest::MakeAsset();
	Asset->AddToRoot();
	Asset->Flipbooks[0].Identity.FlipbookName = TEXT("TerminalStates");
	Asset->Flipbooks[0].Identity.Flipbook =
		Paper2DPlusFrameCueEditorTest::MakeFlipbook(6, Asset);
	// Terminal-state coverage needs classes the readiness gate accepts, so that AlreadyConsumed and
	// TargetUnavailable are reached instead of the earlier InvalidCueClass rejection.
	FPaper2DPlusEditorTestPlaceableCueType MomentType(
		EPaper2DPlusFrameCueTypeKind::Moment, TEXT("PendingPlacementTerminalMoment"));
	FPaper2DPlusEditorTestPlaceableCueType RangeType(
		EPaper2DPlusFrameCueTypeKind::Range,
		TEXT("PendingPlacementTerminalRange"),
		[](UPaper2DPlusCueBase& Defaults)
		{
			CastChecked<UPaper2DPlusCueState>(&Defaults)->bEmitUpdates = true;
		});
	if (!MomentType.IsReady() || !RangeType.IsReady())
	{
		AddError(MomentType.IsReady() ? RangeType.GetError() : MomentType.GetError());
		Asset->RemoveFromRoot();
		return false;
	}
	TSharedPtr<FCharacterProfileEditorModel> Model = MakeShared<FCharacterProfileEditorModel>();
	Model->InitializeFromAsset(Asset);
	TSharedPtr<FProfileFrameCueDataProvider> Provider =
		MakeShared<FProfileFrameCueDataProvider>(Model);
	const FPaper2DPlusFrameCuePlacementTarget Target = CaptureTarget(
		Provider, Provider->GetScopedAnimationIdentity(0), 2);

	TSharedRef<FPaper2DPlusFrameCuePendingPlacement> Consumed =
		FPaper2DPlusFrameCuePendingPlacement::Create(
			Target, EPaper2DPlusFrameCuePlacementKind::Moment, false);
	const FPaper2DPlusFrameCuePlacementResult Retained = Consumed->ConsumeWithoutPlacement();
	TestEqual(TEXT("closing after type creation retains the type without placement"),
		Retained.Status,
		EPaper2DPlusFrameCuePlacementStatus::RetainedTypeWithoutPlacement);
	TestEqual(TEXT("ConsumeWithoutPlacement reaches its explicit terminal state"),
		Consumed->GetState(),
		EPaper2DPlusFrameCuePlacementRequestState::RetainedTypeConsumed);
	TestEqual(TEXT("a consumed request cannot later place"),
		Consumed->Commit(MomentType.GetCueClass()).Status,
		EPaper2DPlusFrameCuePlacementStatus::AlreadyConsumed);
	TestEqual(TEXT("consume creates no placement"),
		Asset->Flipbooks[0].FrameEventData.FrameCues.Num(), 0);

	TSharedRef<FPaper2DPlusFrameCuePendingPlacement> StaleTarget =
		FPaper2DPlusFrameCuePendingPlacement::Create(
			Target, EPaper2DPlusFrameCuePlacementKind::Moment, true);
	Asset->Flipbooks.Reset();
	const FPaper2DPlusFrameCuePlacementResult TargetUnavailable =
		StaleTarget->Commit(MomentType.GetCueClass());
	TestEqual(TEXT("a removed captured animation reports target unavailable"),
		TargetUnavailable.Status,
		EPaper2DPlusFrameCuePlacementStatus::TargetUnavailable);
	TestEqual(TEXT("stale target consumes the placement request without mutation"),
		StaleTarget->GetState(),
		EPaper2DPlusFrameCuePlacementRequestState::RetainedTypeConsumed);
	Asset->RemoveFromRoot();

	UPaper2DPlusCharacterProfileAsset* ValidationAsset =
		Paper2DPlusFrameCueEditorTest::MakeAsset();
	ValidationAsset->AddToRoot();
	ValidationAsset->Flipbooks[0].Identity.FlipbookName = TEXT("ClassValidation");
	ValidationAsset->Flipbooks[0].Identity.Flipbook =
		Paper2DPlusFrameCueEditorTest::MakeFlipbook(6, ValidationAsset);
	TSharedPtr<FCharacterProfileEditorModel> ValidationModel =
		MakeShared<FCharacterProfileEditorModel>();
	ValidationModel->InitializeFromAsset(ValidationAsset);
	TSharedPtr<FProfileFrameCueDataProvider> ValidationProvider =
		MakeShared<FProfileFrameCueDataProvider>(ValidationModel);
	const FPaper2DPlusFrameCuePlacementTarget ValidationTarget = CaptureTarget(
		ValidationProvider, ValidationProvider->GetScopedAnimationIdentity(0), 4);
	TSharedRef<FPaper2DPlusFrameCuePendingPlacement> ValidationPending =
		FPaper2DPlusFrameCuePendingPlacement::Create(
			ValidationTarget, EPaper2DPlusFrameCuePlacementKind::Moment, true);
	TestEqual(TEXT("a non-Cue class is rejected before provider mutation"),
		ValidationPending->Commit(UObject::StaticClass()).Status,
		EPaper2DPlusFrameCuePlacementStatus::InvalidCueClass);

	Paper2DPlusFrameCueEditorTest::FUnsavedCueTypeFixture Unsaved(*this);
	if (TestNotNull(TEXT("unsaved custom Cue Type fixture exists"), Unsaved.CueType)
		&& TestNotNull(TEXT("unsaved custom Cue Type generated class exists"),
			Unsaved.CueType ? Unsaved.CueType->GeneratedClass.Get() : nullptr))
	{
		TestTrue(TEXT("unsaved custom Cue Type is not placement-ready"),
			FPaper2DPlusFrameCueTypeAuthoring::DescribeCueType(
				Unsaved.CueType->GeneratedClass).Availability
				!= EPaper2DPlusFrameCueTypeAvailability::Ready);
		TestEqual(TEXT("an unsaved generated class is rejected before provider mutation"),
			ValidationPending->Commit(Unsaved.CueType->GeneratedClass).Status,
			EPaper2DPlusFrameCuePlacementStatus::InvalidCueClass);
	}
	TestEqual(TEXT("invalid and unsaved classes leave placement storage unchanged"),
		ValidationAsset->Flipbooks[0].FrameEventData.FrameCues.Num(), 0);

	UPaper2DPlusCueState* RangeDefaults = CastChecked<UPaper2DPlusCueState>(
		RangeType.GetCueClass()->GetDefaultObject());
	const int32 SavedDefaultFrameCount = RangeDefaults->FrameCount;
	RangeDefaults->FrameCount = 99;
	UPaper2DPlusCueState* PlacedRange =
		Cast<UPaper2DPlusCueState>(CreatePlacementFromClassDefaults(
			ValidationAsset,
			RangeType.GetCueClass(),
			EPaper2DPlusFrameCuePlacementKind::Range,
			4,
			6));
	RangeDefaults->FrameCount = SavedDefaultFrameCount;
	if (TestNotNull(TEXT("Range placement is created from its CDO"), PlacedRange))
	{
		TestEqual(TEXT("Range placement keeps the captured anchor"),
			PlacedRange->StartFrame, 4);
		TestEqual(TEXT("Range CDO duration clamps to the animation boundary"),
			PlacedRange->FrameCount, 2);
		TestTrue(TEXT("non-timing Range CDO defaults survive placement"),
			PlacedRange->bEmitUpdates);
	}
	// Drop placements of the fixture Cue Types before their packages unload.
	PlacedRange = nullptr;
	ValidationAsset->Flipbooks[0].FrameEventData.FrameCues.Reset();
	ValidationAsset->RemoveFromRoot();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCuePendingPlacementOriginCompletionTest,
	"Paper2DPlus.FrameCues.Editor.Authoring.PendingPlacementOriginCompletionIsWeakAndOneShot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCuePendingPlacementOriginCompletionTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusFrameCuePlacementAuthoring;

	UPaper2DPlusCharacterProfileAsset* Asset = Paper2DPlusFrameCueEditorTest::MakeAsset();
	Asset->AddToRoot();
	Asset->Flipbooks[0].Identity.FlipbookName = TEXT("CapturedOrigin");
	Asset->Flipbooks[0].Identity.Flipbook =
		Paper2DPlusFrameCueEditorTest::MakeFlipbook(6, Asset);
	// Commit routes through the production readiness gate; only a durably saved Cue Type passes it.
	FPaper2DPlusEditorTestPlaceableCueType MomentType(
		EPaper2DPlusFrameCueTypeKind::Moment, TEXT("PendingPlacementOrigin"));
	if (!MomentType.IsReady())
	{
		AddError(MomentType.GetError());
		Asset->RemoveFromRoot();
		return false;
	}
	Asset->Flipbooks.AddDefaulted();
	Asset->Flipbooks[1].Identity.FlipbookName = TEXT("LaterSelection");
	Asset->Flipbooks[1].Identity.Flipbook =
		Paper2DPlusFrameCueEditorTest::MakeFlipbook(3, Asset);

	TSharedPtr<FCharacterProfileEditorModel> Model = MakeShared<FCharacterProfileEditorModel>();
	Model->InitializeFromAsset(Asset);
	Model->SetSelectedFlipbook(0);
	Model->SetSelectedFrame(4);
	TSharedPtr<SFrameEventEditor> Editor = SNew(SFrameEventEditor).Model(Model);
	const FPaper2DPlusFrameCuePlacementTarget Target = CaptureTarget(
		Editor->GetDataProviderForTests(),
		Editor->GetDataProviderForTests()->GetScopedAnimationIdentity(0),
		4);
	TSharedRef<FPaper2DPlusFrameCuePendingPlacement> Pending =
		Editor->CreatePendingPlacementForTests(
			Target, EPaper2DPlusFrameCuePlacementKind::Moment, true);
	Model->SetSelectedFlipbook(1);
	Model->SetSelectedFrame(1);

	const FPaper2DPlusFrameCuePlacementResult Placed = Pending->Commit(MomentType.GetCueClass());
	TestEqual(TEXT("pending placement succeeds"),
		Placed.Status, EPaper2DPlusFrameCuePlacementStatus::Success);
	TestEqual(TEXT("completion returns the live origin to the captured animation"),
		Model->GetSelectedFlipbookIndex(), 0);
	TestEqual(TEXT("completion returns the live origin to the captured frame"),
		Model->GetSelectedFrameIndex(), 4);
	TestTrue(TEXT("completion selects the newly created placement by stable identity"),
		Asset->Flipbooks[0].FrameEventData.FrameCues.IsValidIndex(0)
			&& Editor->GetSelectedCueForTests()
				== Asset->Flipbooks[0].FrameEventData.FrameCues[0]);
	TestEqual(TEXT("successful completion refresh callback fires exactly once"),
		Editor->GetPendingPlacementCompletionCountForTests(), 1);
	Pending->Commit(MomentType.GetCueClass());
	TestEqual(TEXT("duplicate completion cannot notify the origin twice"),
		Editor->GetPendingPlacementCompletionCountForTests(), 1);
	Editor.Reset();
	Asset->Flipbooks[0].FrameEventData.FrameCues.Reset();
	Asset->RemoveFromRoot();

	UPaper2DPlusCharacterProfileAsset* ClosedAsset =
		Paper2DPlusFrameCueEditorTest::MakeAsset();
	ClosedAsset->AddToRoot();
	ClosedAsset->Flipbooks[0].Identity.FlipbookName = TEXT("ClosedOrigin");
	ClosedAsset->Flipbooks[0].Identity.Flipbook =
		Paper2DPlusFrameCueEditorTest::MakeFlipbook(4, ClosedAsset);
	TSharedPtr<FCharacterProfileEditorModel> ClosedModel =
		MakeShared<FCharacterProfileEditorModel>();
	ClosedModel->InitializeFromAsset(ClosedAsset);
	TSharedPtr<SFrameEventEditor> ClosedEditor = SNew(SFrameEventEditor).Model(ClosedModel);
	TWeakPtr<SFrameEventEditor> WeakClosedEditor = ClosedEditor;
	const FPaper2DPlusFrameCuePlacementTarget ClosedTarget = CaptureTarget(
		ClosedEditor->GetDataProviderForTests(),
		ClosedEditor->GetDataProviderForTests()->GetScopedAnimationIdentity(0),
		2);
	TSharedRef<FPaper2DPlusFrameCuePendingPlacement> ClosedPending =
		ClosedEditor->CreatePendingPlacementForTests(
			ClosedTarget, EPaper2DPlusFrameCuePlacementKind::Moment, true);
	ClosedEditor.Reset();
	TestFalse(TEXT("origin widget is genuinely closed before completion"),
		WeakClosedEditor.IsValid());
	TestEqual(TEXT("placement remains valid when its origin widget has closed"),
		ClosedPending->Commit(MomentType.GetCueClass()).Status,
		EPaper2DPlusFrameCuePlacementStatus::Success);
	TestEqual(TEXT("closed-origin completion still creates exactly one captured placement"),
		ClosedAsset->Flipbooks[0].FrameEventData.FrameCues.Num(), 1);
	ClosedAsset->Flipbooks[0].FrameEventData.FrameCues.Reset();
	ClosedAsset->RemoveFromRoot();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCuePendingPlacementReentrantCompletionTest,
	"Paper2DPlus.FrameCues.Editor.Authoring.PendingPlacementCompletionRejectsReentrantCommit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCuePendingPlacementReentrantCompletionTest::RunTest(
	const FString& Parameters)
{
	using namespace Paper2DPlusFrameCuePlacementAuthoring;

	UPaper2DPlusCharacterProfileAsset* Asset = Paper2DPlusFrameCueEditorTest::MakeAsset();
	Asset->AddToRoot();
	Asset->Flipbooks[0].Identity.FlipbookName = TEXT("ReentrantCompletion");
	Asset->Flipbooks[0].Identity.Flipbook =
		Paper2DPlusFrameCueEditorTest::MakeFlipbook(5, Asset);
	// Commit routes through the production readiness gate; only a durably saved Cue Type passes it.
	FPaper2DPlusEditorTestPlaceableCueType MomentType(
		EPaper2DPlusFrameCueTypeKind::Moment, TEXT("PendingPlacementReentrant"));
	if (!MomentType.IsReady())
	{
		AddError(MomentType.GetError());
		Asset->RemoveFromRoot();
		return false;
	}
	TSharedPtr<FCharacterProfileEditorModel> Model = MakeShared<FCharacterProfileEditorModel>();
	Model->InitializeFromAsset(Asset);
	TSharedPtr<FProfileFrameCueDataProvider> Provider =
		MakeShared<FProfileFrameCueDataProvider>(Model);

	int32 CompletionCount = 0;
	EPaper2DPlusFrameCuePlacementStatus CompletionStatus =
		EPaper2DPlusFrameCuePlacementStatus::InvalidRequest;
	EPaper2DPlusFrameCuePlacementStatus ReentrantStatus =
		EPaper2DPlusFrameCuePlacementStatus::InvalidRequest;
	EPaper2DPlusFrameCuePlacementRequestState ReentrantState =
		EPaper2DPlusFrameCuePlacementRequestState::EditingType;
	TSharedPtr<FPaper2DPlusFrameCuePendingPlacement> Pending;
	Pending = FPaper2DPlusFrameCuePendingPlacement::Create(
		CaptureTarget(Provider, Provider->GetScopedAnimationIdentity(0), 3),
		EPaper2DPlusFrameCuePlacementKind::Moment,
		/*bReadyToPlace*/ true,
		[&](const FPaper2DPlusFrameCuePlacementResult& CompletionResult)
		{
			++CompletionCount;
			CompletionStatus = CompletionResult.Status;
			if (Pending.IsValid())
			{
				const FPaper2DPlusFrameCuePlacementResult NestedResult =
					Pending->Commit(MomentType.GetCueClass());
				ReentrantStatus = NestedResult.Status;
				ReentrantState = NestedResult.State;
			}
		});

	const FPaper2DPlusFrameCuePlacementResult Result = Pending->Commit(MomentType.GetCueClass());
	TestEqual(TEXT("outer Commit succeeds"),
		Result.Status, EPaper2DPlusFrameCuePlacementStatus::Success);
	TestEqual(TEXT("completion is invoked exactly once"), CompletionCount, 1);
	TestEqual(TEXT("completion receives the successful outer result"),
		CompletionStatus, EPaper2DPlusFrameCuePlacementStatus::Success);
	TestEqual(TEXT("Commit called from inside completion observes the consumed request"),
		ReentrantStatus, EPaper2DPlusFrameCuePlacementStatus::AlreadyConsumed);
	TestEqual(TEXT("reentrant result exposes the terminal consumed state"),
		ReentrantState, EPaper2DPlusFrameCuePlacementRequestState::PlacedConsumed);
	TestEqual(TEXT("reentrant Commit cannot append a duplicate placement"),
		Asset->Flipbooks[0].FrameEventData.FrameCues.Num(), 1);
	const UPaper2DPlusCue* Cue =
		Asset->Flipbooks[0].FrameEventData.FrameCues.IsValidIndex(0)
			? Cast<UPaper2DPlusCue>(
				Asset->Flipbooks[0].FrameEventData.FrameCues[0])
			: nullptr;
	TestEqual(TEXT("the one placement retains the original captured frame"),
		Cue ? Cue->TriggerFrame : INDEX_NONE, 3);

	Pending.Reset();
	Provider.Reset();
	Model.Reset();
	Asset->Flipbooks[0].FrameEventData.FrameCues.Reset();
	Asset->RemoveFromRoot();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCuePendingPlacementRollbackTest,
	"Paper2DPlus.FrameCues.Editor.Authoring.PendingPlacementFailureAtomicRetry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCuePendingPlacementRollbackTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusFrameCuePlacementAuthoring;
	if (!TestNotNull(TEXT("GEditor available"), GEditor))
	{
		return false;
	}

	UPaper2DPlusCharacterProfileAsset* Asset = Paper2DPlusFrameCueEditorTest::MakeAsset();
	Asset->AddToRoot();
	Asset->Flipbooks[0].Identity.FlipbookName = TEXT("RollbackAttack");
	Asset->Flipbooks[0].Identity.Flipbook =
		Paper2DPlusFrameCueEditorTest::MakeFlipbook(6, Asset);
	// Commit routes through the production readiness gate; only a durably saved Cue Type passes it.
	FPaper2DPlusEditorTestPlaceableCueType MomentType(
		EPaper2DPlusFrameCueTypeKind::Moment, TEXT("PendingPlacementRollback"));
	if (!MomentType.IsReady())
	{
		AddError(MomentType.GetError());
		Asset->RemoveFromRoot();
		return false;
	}
	TSharedPtr<FCharacterProfileEditorModel> Model = MakeShared<FCharacterProfileEditorModel>();
	Model->InitializeFromAsset(Asset);
	TSharedPtr<FProfileFrameCueDataProvider> Provider =
		MakeShared<FProfileFrameCueDataProvider>(Model);
	TSharedRef<FPaper2DPlusFrameCuePendingPlacement> Pending =
		FPaper2DPlusFrameCuePendingPlacement::Create(
			CaptureTarget(Provider, Provider->GetScopedAnimationIdentity(0), 2),
			EPaper2DPlusFrameCuePlacementKind::Moment,
			/*bReadyToPlace*/ true);

#if WITH_DEV_AUTOMATION_TESTS
	const EPaper2DPlusFrameCuePlacementFailurePoint FailurePoints[] = {
		EPaper2DPlusFrameCuePlacementFailurePoint::AfterTargetModify,
		EPaper2DPlusFrameCuePlacementFailurePoint::AfterStorageEnsure,
		EPaper2DPlusFrameCuePlacementFailurePoint::AfterPlacementConstruction,
		EPaper2DPlusFrameCuePlacementFailurePoint::AfterArrayAppend,
		EPaper2DPlusFrameCuePlacementFailurePoint::AfterIdentityDerivation
	};
	for (const EPaper2DPlusFrameCuePlacementFailurePoint FailurePoint : FailurePoints)
	{
		GEditor->ResetTransaction(LOCTEXT(
			"ResetInjectedPlacementFailure", "Injected Placement Failure Reset"));
		Asset->GetOutermost()->SetDirtyFlag(false);
		Pending->SetFailurePointForTests(FailurePoint);
		const FPaper2DPlusFrameCuePlacementResult Failed = Pending->Commit(MomentType.GetCueClass());
		TestEqual(TEXT("injected provider failure is reported as a complete rollback"),
			Failed.Status, EPaper2DPlusFrameCuePlacementStatus::PlacementRolledBack);
		TestEqual(TEXT("rolled-back request remains retryable"),
			Pending->GetState(),
			EPaper2DPlusFrameCuePlacementRequestState::RetryableAfterRollback);
		TestEqual(TEXT("rolled-back Profile placement leaves the Cue array byte-shape empty"),
			Asset->Flipbooks[0].FrameEventData.FrameCues.Num(), 0);
		TestFalse(TEXT("rolled-back Profile placement restores package dirty state"),
			Asset->GetOutermost()->IsDirty());
		TestFalse(TEXT("cancelled provider transaction leaves no undo entry"),
			GEditor->UndoTransaction(true));
	}

	Pending->SetFailurePointForTests(EPaper2DPlusFrameCuePlacementFailurePoint::None);
#endif
	const FPaper2DPlusFrameCuePlacementResult Retried = Pending->Commit(MomentType.GetCueClass());
	TestEqual(TEXT("same request succeeds after the recoverable failure clears"),
		Retried.Status, EPaper2DPlusFrameCuePlacementStatus::Success);
	TestEqual(TEXT("retry creates one placement, not one per failed attempt"),
		Asset->Flipbooks[0].FrameEventData.FrameCues.Num(), 1);

	GEditor->ResetTransaction(LOCTEXT(
		"EndInjectedPlacementFailure", "Injected Placement Failure End"));
	Asset->Flipbooks[0].FrameEventData.FrameCues.Reset();
	Asset->RemoveFromRoot();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCueTypePickerStateModelTest,
	"Paper2DPlus.FrameCues.Editor.Authoring.CueTypePickerStateModel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCueTypePickerStateModelTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusFrameCueEditorAuthoring;
	// Discovery is synchronous, so the retired transient "refreshing" state has no row here: every
	// resolve describes a settled result.
	TestEqual(TEXT("ready results remain usable"),
		ResolveCueTypePickerStatus(2, 2, 0, 0, false),
		EPaper2DPlusFrameCueTypePickerStatus::Ready);
	TestEqual(TEXT("partial discovery errors remain visible beside usable results"),
		ResolveCueTypePickerStatus(2, 2, 1, 1, false),
		EPaper2DPlusFrameCueTypePickerStatus::ReadyWithDiscoveryErrors);
	TestEqual(TEXT("total discovery failure is not mislabeled as first run"),
		ResolveCueTypePickerStatus(0, 0, 2, 2, false),
		EPaper2DPlusFrameCueTypePickerStatus::DiscoveryError);
	TestEqual(TEXT("search miss is distinct from empty project"),
		ResolveCueTypePickerStatus(3, 0, 0, 0, true),
		EPaper2DPlusFrameCueTypePickerStatus::NoSearchMatch);
	TestEqual(TEXT("invalid saved types expose recovery state"),
		ResolveCueTypePickerStatus(0, 0, 2, 0, false),
		EPaper2DPlusFrameCueTypePickerStatus::NeedsRepair);
	TestEqual(TEXT("empty valid discovery exposes first-run state"),
		ResolveCueTypePickerStatus(0, 0, 0, 0, false),
		EPaper2DPlusFrameCueTypePickerStatus::FirstRun);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCueTypePickerKeyboardNavigationTest,
	"Paper2DPlus.FrameCues.Editor.Authoring.CueTypePickerKeyboardNavigation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCueTypePickerKeyboardNavigationTest::RunTest(const FString& Parameters)
{
	using Paper2DPlusFrameCueEditorAuthoring::ResolveCueTypePickerNavigationIndex;
	TestEqual(TEXT("Down chooses first result from no selection"),
		ResolveCueTypePickerNavigationIndex(INDEX_NONE, 3, 1), 0);
	TestEqual(TEXT("Up chooses last result from no selection"),
		ResolveCueTypePickerNavigationIndex(INDEX_NONE, 3, -1), 2);
	TestEqual(TEXT("Down advances"),
		ResolveCueTypePickerNavigationIndex(0, 3, 1), 1);
	TestEqual(TEXT("Down clamps at final result"),
		ResolveCueTypePickerNavigationIndex(2, 3, 1), 2);
	TestEqual(TEXT("Up clamps at first result"),
		ResolveCueTypePickerNavigationIndex(0, 3, -1), 0);
	TestEqual(TEXT("empty results have no navigation target"),
		ResolveCueTypePickerNavigationIndex(INDEX_NONE, 0, 1), INDEX_NONE);
	return true;
}

#undef LOCTEXT_NAMESPACE

#endif // WITH_EDITOR
