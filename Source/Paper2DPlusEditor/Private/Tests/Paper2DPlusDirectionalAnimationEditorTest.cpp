// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "AssetRegistry/AssetRegistryModule.h"

#include "CharacterProfileEditorModel.h"
#include "Containers/Ticker.h"
#include "Editor.h"
#include "Input/Events.h"
#include "InputCoreTypes.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "PaperFlipbook.h"
#include "ProfileDetailsPanel.h"
#include "ProfileValidationPanel.h"
#include "SDirectionalAnimationWheel.h"
#include "UObject/Package.h"
#include "Widgets/Input/SSpinBox.h"

#include <limits>

namespace Paper2DPlusDirectionalAnimationEditorTest
{
	UPaperFlipbook* MakeFlipbook(
		UPaper2DPlusCharacterProfileAsset* Profile,
		const TCHAR* Name)
	{
		return NewObject<UPaperFlipbook>(Profile, Name);
	}

	int32 AddAnimation(
		UPaper2DPlusCharacterProfileAsset* Profile,
		const TCHAR* Name,
		UPaperFlipbook* Flipbook = nullptr)
	{
		FFlipbookProfileEntry Entry;
		Entry.Identity.FlipbookName = Name;
		Entry.Identity.Flipbook = Flipbook;
		return Profile->Flipbooks.Add(MoveTemp(Entry));
	}

	FKeyEvent MakeKeyEvent(const FKey& Key)
	{
		return FKeyEvent(Key, FModifierKeysState(), 0, false, 0, 0);
	}

	TSharedPtr<SWidget> FindWidgetByTag(
		const TSharedRef<SWidget>& Widget,
		const FName Tag)
	{
		if (Widget->GetTag() == Tag)
		{
			return Widget;
		}
		if (FChildren* Children = Widget->GetChildren())
		{
			for (int32 ChildIndex = 0; ChildIndex < Children->Num(); ++ChildIndex)
			{
				if (TSharedPtr<SWidget> Match = FindWidgetByTag(
					Children->GetChildAt(ChildIndex), Tag))
				{
					return Match;
				}
			}
		}
		return nullptr;
	}

	struct FPanelFixture
	{
		UPaper2DPlusCharacterProfileAsset* Profile = nullptr;
		TSharedRef<FCharacterProfileEditorModel> Model;
		TSharedRef<SProfileDetailsPanel> Panel;

		explicit FPanelFixture(UPaper2DPlusCharacterProfileAsset* InProfile)
			: Profile(InProfile)
			, Model(MakeShared<FCharacterProfileEditorModel>())
			, Panel(SNew(SProfileDetailsPanel)
				.Model(Model)
				.PaneMode(EProfileDetailsPaneMode::FlipbookDetails))
		{
			// The panel construction above intentionally precedes initialization in order to exercise the
			// same live model-reinitialization path a reused toolkit follows.
			Model->InitializeFromAsset(Profile);
			if (Profile && !Profile->Flipbooks.IsEmpty())
			{
				Model->SetSelectedFlipbook(0);
			}
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalAuthoringSetLifecycleTest,
	"Paper2DPlus.DirectionalAnimation.Editor.Authoring.SetLifecycleAndSlotPresence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalAuthoringSetLifecycleTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusDirectionalAnimationEditorTest;
	UPaper2DPlusCharacterProfileAsset* Profile =
		NewObject<UPaper2DPlusCharacterProfileAsset>();
	UPaperFlipbook* Base = MakeFlipbook(Profile, TEXT("LifecycleBase"));
	UPaperFlipbook* Variant = MakeFlipbook(Profile, TEXT("LifecycleNorth"));
	const int32 AnimationIndex = AddAnimation(Profile, TEXT("Idle"), Base);
	FPanelFixture Fixture(Profile);
	const FProfileAnimationIdentity Identity =
		Fixture.Panel->GetSelectedDirectionalAnimationIdentity();

	TestFalse(TEXT("A legacy entry begins without explicit set presence"),
		Profile->HasDirectionalSet(AnimationIndex));
	TestTrue(TEXT("Enable creates explicit configured-empty presence"),
		Fixture.Panel->EnableDirectionalSet(Identity));
	TestTrue(TEXT("Configured-empty presence is distinct from active multidirectionality"),
		Profile->HasDirectionalSet(AnimationIndex));
	TestFalse(TEXT("Configured-empty presence has no active slot"),
		Profile->HasActiveDirectionalSlots(AnimationIndex));
	TestTrue(TEXT("An empty set can be removed explicitly"),
		Fixture.Panel->RemoveDirectionalSet(Identity));
	TestFalse(TEXT("Explicit removal restores absent/base-only state"),
		Profile->HasDirectionalSet(AnimationIndex));

	TestTrue(TEXT("First assignment auto-enables an absent set in the same commit"),
		Fixture.Panel->CommitDirectionalSlot(Identity, 0, Variant));
	TestTrue(TEXT("Assignment establishes explicit presence"),
		Profile->HasDirectionalSet(AnimationIndex));
	TestTrue(TEXT("Assignment makes the entry multidirectional"),
		Profile->HasActiveDirectionalSlots(AnimationIndex));
	TestFalse(TEXT("Remove is blocked while any slot remains occupied"),
		Fixture.Panel->RemoveDirectionalSet(Identity));
	TestTrue(TEXT("The block explains the required recovery"),
		Fixture.Panel->GetLastDirectionalIssueTextForTests().Contains(TEXT("cleared")));

	TestTrue(TEXT("Clear removes art without removing explicit set presence"),
		Fixture.Panel->ClearDirectionalSlot(Identity, 0));
	TestTrue(TEXT("Clearing the final slot preserves configured-empty presence"),
		Profile->HasDirectionalSet(AnimationIndex));
	TestFalse(TEXT("Clearing the final slot removes active multidirectionality"),
		Profile->HasActiveDirectionalSlots(AnimationIndex));
	TestTrue(TEXT("The now-empty set can be removed"),
		Fixture.Panel->RemoveDirectionalSet(Identity));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalAuthoringSlotMirrorTest,
	"Paper2DPlus.DirectionalAnimation.Editor.Authoring.SlotMirrorCommitAndPreservation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalAuthoringSlotMirrorTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusDirectionalAnimationEditorTest;
	UPaper2DPlusCharacterProfileAsset* Profile =
		NewObject<UPaper2DPlusCharacterProfileAsset>();
	UPaperFlipbook* Base = MakeFlipbook(Profile, TEXT("MirrorBase"));
	UPaperFlipbook* Variant = MakeFlipbook(Profile, TEXT("MirrorEast"));
	UPaperFlipbook* Replacement = MakeFlipbook(Profile, TEXT("MirrorEastRedraw"));
	const int32 AnimationIndex = AddAnimation(Profile, TEXT("Walk"), Base);
	FPanelFixture Fixture(Profile);
	const FProfileAnimationIdentity Identity =
		Fixture.Panel->GetSelectedDirectionalAnimationIdentity();

	TestFalse(TEXT("An unoccupied slot refuses a mirror commit"),
		Fixture.Panel->CommitDirectionalSlotMirror(Identity, 2, true));
	TestTrue(TEXT("The refusal explains the missing art"),
		Fixture.Panel->GetLastDirectionalIssueTextForTests().Contains(TEXT("no art")));

	TestTrue(TEXT("The fixture assigns slot 2"),
		Fixture.Panel->CommitDirectionalSlot(Identity, 2, Variant));
	TestFalse(TEXT("Re-writing the current unmirrored state is a no-op"),
		Fixture.Panel->CommitDirectionalSlotMirror(Identity, 2, false));
	TestTrue(TEXT("The occupied slot accepts a mirror commit"),
		Fixture.Panel->CommitDirectionalSlotMirror(Identity, 2, true));

	TSoftObjectPtr<UPaperFlipbook> Stored;
	bool bStoredMirror = false;
	TestTrue(TEXT("The mirrored slot stays occupied"),
		Profile->GetDirectionalSlot(AnimationIndex, 2, Stored, bStoredMirror));
	TestTrue(TEXT("The commit stored the mirror flag"), bStoredMirror);

	TestTrue(TEXT("Re-picking the slot's art succeeds"),
		Fixture.Panel->CommitDirectionalSlot(Identity, 2, Replacement));
	bStoredMirror = false;
	TestTrue(TEXT("The re-picked slot stays occupied"),
		Profile->GetDirectionalSlot(AnimationIndex, 2, Stored, bStoredMirror));
	TestEqual(TEXT("The re-pick stored the replacement art"),
		Stored.ToSoftObjectPath(), FSoftObjectPath(Replacement));
	TestTrue(TEXT("An art re-pick through the panel preserves the mirror flag"),
		bStoredMirror);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalAuthoringSlotTransactionPipelineTest,
	"Paper2DPlus.DirectionalAnimation.Editor.Authoring.AssignReplaceClearTransactionPipeline",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalAuthoringSlotTransactionPipelineTest::RunTest(
	const FString& Parameters)
{
	using namespace Paper2DPlusDirectionalAnimationEditorTest;
	if (!TestNotNull(TEXT("GEditor is required for directional slot transaction proof"), GEditor))
	{
		return false;
	}

	const FString PackageName = FString::Printf(
		TEXT("/Game/Paper2DPlusTests/DirectionalSlotTransactions_%s"),
		*FGuid::NewGuid().ToString(EGuidFormats::Digits));
	UPackage* Package = CreatePackage(*PackageName);
	UPaper2DPlusCharacterProfileAsset* Profile =
		NewObject<UPaper2DPlusCharacterProfileAsset>(Package, TEXT("Profile"));
	// Real authored flipbooks are independent assets, not subobjects of the Profile. Keep these
	// top-level objects resident so transaction replay must restore usable soft references rather
	// than depending on an invalid nested-object fixture path.
	constexpr EObjectFlags FlipbookFlags =
		RF_Public | RF_Standalone | RF_Transactional;
	UPaperFlipbook* Base = NewObject<UPaperFlipbook>(
		Package, TEXT("TransactionBase"), FlipbookFlags);
	UPaperFlipbook* Assigned = NewObject<UPaperFlipbook>(
		Package, TEXT("TransactionAssigned"), FlipbookFlags);
	UPaperFlipbook* Replacement = NewObject<UPaperFlipbook>(
		Package, TEXT("TransactionReplacement"), FlipbookFlags);
	const int32 AnimationIndex = AddAnimation(Profile, TEXT("Aim"), Base);
	FPanelFixture Fixture(Profile);
	Fixture.Model->SetDirectionalPreviewEnabled(true);
	Fixture.Model->SetCommittedDirectionalBearing(0.0);
	const FProfileAnimationIdentity Identity =
		Fixture.Panel->GetSelectedDirectionalAnimationIdentity();

	TSharedRef<SProfileValidationPanel> ValidationPanel =
		SNew(SProfileValidationPanel)
		.Asset(Profile)
		.RunInitially(false)
		.OnCustomValidationRun(FOnPaper2DPlusCustomValidationRun::CreateLambda(
			[](TArray<FPaper2DPlusValidationIssue>& OutIssues)
			{
				OutIssues.Reset();
				return true;
			}));
	int32 ModelDataRefreshCount = 0;
	const FDelegateHandle ModelRefreshHandle = Fixture.Model->OnAssetDataChanged.AddLambda(
		[&ModelDataRefreshCount]()
		{
			++ModelDataRefreshCount;
		});

	const auto FlushObservedRefreshes = [&ValidationPanel]()
	{
		FTSTicker::GetCoreTicker().Tick(0.0f);
		ValidationPanel->FlushPendingRefreshForTests();
	};
	const auto TestStoredSlot = [this, Profile, AnimationIndex](
		const TCHAR* Message,
		UPaperFlipbook* Expected)
	{
		TSoftObjectPtr<UPaperFlipbook> Stored;
		if (TestTrue(Message, Profile->GetDirectionalSlot(AnimationIndex, 0, Stored)))
		{
			TestEqual(
				FString::Printf(TEXT("%s stores the expected asset"), Message),
				Stored.ToSoftObjectPath(),
				FSoftObjectPath(Expected));
		}
	};
	const auto TestOwner = [this, Profile, AnimationIndex](
		const TCHAR* Message,
		UPaperFlipbook* Flipbook,
		bool bExpectedOwned)
	{
		bool bAmbiguous = false;
		const FFlipbookProfileEntry* Owner =
			Profile->ResolveLogicalAnimationOwner(Flipbook, bAmbiguous);
		const FFlipbookProfileEntry* ExpectedOwner =
			bExpectedOwned ? &Profile->Flipbooks[AnimationIndex] : nullptr;
		TestFalse(FString::Printf(TEXT("%s is not ambiguous"), Message), bAmbiguous);
		TestTrue(Message, Owner == ExpectedOwner);
	};

	Package->SetDirtyFlag(false);
	GEditor->ResetTransaction(FText::FromString(
		TEXT("TASK-190 directional slot assignment start")));
	const int32 ValidationBeforeAssign = ValidationPanel->GetValidationRunCountForTests();
	const int32 ModelBeforeAssign = ModelDataRefreshCount;
	const uint32 RevisionBeforeAssign = Profile->GetEditorContentRevision();
	TestTrue(TEXT("Assign commits through the rendered slot authoring seam"),
		Fixture.Panel->CommitDirectionalSlot(Identity, 0, Assigned));
	TestStoredSlot(TEXT("Assign creates slot zero"), Assigned);
	TestTrue(TEXT("Assign dirties the owning Profile package"), Package->IsDirty());
	TestTrue(TEXT("Assign enrolls the Profile in one editor transaction"),
		Profile->GetEditorContentRevision() > RevisionBeforeAssign);
	TestEqual(TEXT("Assign emits one model data refresh"),
		ModelDataRefreshCount, ModelBeforeAssign + 1);
	TestEqual(TEXT("Assign refreshes the model's displayed directional art"),
		Fixture.Model->GetDirectionalPreviewFlipbook(), Assigned);
	TestOwner(TEXT("Assign publishes the variant owner alias"), Assigned, true);
	FlushObservedRefreshes();
	TestEqual(TEXT("Assign schedules one coalesced validation refresh"),
		ValidationPanel->GetValidationRunCountForTests(), ValidationBeforeAssign + 1);

	const int32 ValidationBeforeAssignUndo =
		ValidationPanel->GetValidationRunCountForTests();
	TestTrue(TEXT("One Undo reverts assignment and its implicit set enable"),
		GEditor->UndoTransaction(true));
	FlushObservedRefreshes();
	TestFalse(TEXT("Assignment Undo restores absent set presence"),
		Profile->HasDirectionalSet(AnimationIndex));
	TestOwner(TEXT("Assignment Undo removes the assigned owner alias"), Assigned, false);
	TestEqual(TEXT("Assignment Undo restores base preview art"),
		Fixture.Model->GetDirectionalPreviewFlipbook(), Base);
	TestEqual(TEXT("Assignment Undo refreshes validation"),
		ValidationPanel->GetValidationRunCountForTests(), ValidationBeforeAssignUndo + 1);

	const int32 ValidationBeforeAssignRedo =
		ValidationPanel->GetValidationRunCountForTests();
	TestTrue(TEXT("One Redo reapplies assignment and set presence"), GEditor->RedoTransaction());
	FlushObservedRefreshes();
	TestStoredSlot(TEXT("Assignment Redo restores slot zero"), Assigned);
	TestEqual(TEXT("Assignment Redo restores displayed directional art"),
		Fixture.Model->GetDirectionalPreviewFlipbook(), Assigned);
	TestOwner(TEXT("Assignment Redo restores the variant owner alias"), Assigned, true);
	TestEqual(TEXT("Assignment Redo refreshes validation"),
		ValidationPanel->GetValidationRunCountForTests(), ValidationBeforeAssignRedo + 1);

	// Warm the alias lookup before replacement. The following assertions observe caller-visible cache
	// coherence: the old variant must stop resolving and the new variant must resolve immediately.
	TestOwner(TEXT("The assigned alias is resident in the owner lookup before replacement"),
		Assigned, true);
	Package->SetDirtyFlag(false);
	GEditor->ResetTransaction(FText::FromString(
		TEXT("TASK-190 directional slot replacement start")));
	const int32 ValidationBeforeReplace = ValidationPanel->GetValidationRunCountForTests();
	const int32 ModelBeforeReplace = ModelDataRefreshCount;
	TestTrue(TEXT("Replace commits through the same slot authoring seam"),
		Fixture.Panel->CommitDirectionalSlot(Identity, 0, Replacement));
	TestStoredSlot(TEXT("Replace writes slot zero"), Replacement);
	TestTrue(TEXT("Replace dirties the owning Profile package"), Package->IsDirty());
	TestEqual(TEXT("Replace emits one model data refresh"),
		ModelDataRefreshCount, ModelBeforeReplace + 1);
	TestEqual(TEXT("Replace refreshes the model's displayed directional art"),
		Fixture.Model->GetDirectionalPreviewFlipbook(), Replacement);
	TestOwner(TEXT("Replace invalidates the old variant owner alias"), Assigned, false);
	TestOwner(TEXT("Replace publishes the new variant owner alias"), Replacement, true);
	FlushObservedRefreshes();
	TestEqual(TEXT("Replace schedules one coalesced validation refresh"),
		ValidationPanel->GetValidationRunCountForTests(), ValidationBeforeReplace + 1);

	const int32 ValidationBeforeReplaceUndo =
		ValidationPanel->GetValidationRunCountForTests();
	TestTrue(TEXT("One Undo restores the replaced slot"), GEditor->UndoTransaction(true));
	FlushObservedRefreshes();
	TestStoredSlot(TEXT("Replace Undo restores the assigned asset"), Assigned);
	TestEqual(TEXT("Replace Undo restores displayed directional art"),
		Fixture.Model->GetDirectionalPreviewFlipbook(), Assigned);
	TestOwner(TEXT("Replace Undo restores the old owner alias"), Assigned, true);
	TestOwner(TEXT("Replace Undo removes the replacement owner alias"), Replacement, false);
	TestEqual(TEXT("Replace Undo refreshes validation"),
		ValidationPanel->GetValidationRunCountForTests(), ValidationBeforeReplaceUndo + 1);

	const int32 ValidationBeforeReplaceRedo =
		ValidationPanel->GetValidationRunCountForTests();
	TestTrue(TEXT("One Redo reapplies the replacement"), GEditor->RedoTransaction());
	FlushObservedRefreshes();
	TestStoredSlot(TEXT("Replace Redo restores the replacement asset"), Replacement);
	TestEqual(TEXT("Replace Redo restores displayed replacement art"),
		Fixture.Model->GetDirectionalPreviewFlipbook(), Replacement);
	TestOwner(TEXT("Replace Redo removes the old owner alias"), Assigned, false);
	TestOwner(TEXT("Replace Redo restores the replacement owner alias"), Replacement, true);
	TestEqual(TEXT("Replace Redo refreshes validation"),
		ValidationPanel->GetValidationRunCountForTests(), ValidationBeforeReplaceRedo + 1);

	Package->SetDirtyFlag(false);
	GEditor->ResetTransaction(FText::FromString(
		TEXT("TASK-190 directional slot clear start")));
	const int32 ValidationBeforeClear = ValidationPanel->GetValidationRunCountForTests();
	const int32 ModelBeforeClear = ModelDataRefreshCount;
	TestTrue(TEXT("Clear commits through the rendered slot authoring seam"),
		Fixture.Panel->ClearDirectionalSlot(Identity, 0));
	TSoftObjectPtr<UPaperFlipbook> ClearedSlot;
	TestFalse(TEXT("Clear removes the authored slot record"),
		Profile->GetDirectionalSlot(AnimationIndex, 0, ClearedSlot));
	TestTrue(TEXT("Clear preserves configured-empty set presence"),
		Profile->HasDirectionalSet(AnimationIndex));
	TestFalse(TEXT("Clear removes active multidirectionality"),
		Profile->HasActiveDirectionalSlots(AnimationIndex));
	TestTrue(TEXT("Clear dirties the owning Profile package"), Package->IsDirty());
	TestEqual(TEXT("Clear emits one model data refresh"),
		ModelDataRefreshCount, ModelBeforeClear + 1);
	TestEqual(TEXT("Clear restores base art through the model"),
		Fixture.Model->GetDirectionalPreviewFlipbook(), Base);
	TestOwner(TEXT("Clear invalidates the removed variant owner alias"), Replacement, false);
	FlushObservedRefreshes();
	TestEqual(TEXT("Clear schedules one coalesced validation refresh"),
		ValidationPanel->GetValidationRunCountForTests(), ValidationBeforeClear + 1);

	const int32 ValidationBeforeClearUndo =
		ValidationPanel->GetValidationRunCountForTests();
	TestTrue(TEXT("One Undo restores the cleared slot"), GEditor->UndoTransaction(true));
	FlushObservedRefreshes();
	TestStoredSlot(TEXT("Clear Undo restores the replacement asset"), Replacement);
	TestEqual(TEXT("Clear Undo restores displayed directional art"),
		Fixture.Model->GetDirectionalPreviewFlipbook(), Replacement);
	TestOwner(TEXT("Clear Undo restores the replacement owner alias"), Replacement, true);
	TestEqual(TEXT("Clear Undo refreshes validation"),
		ValidationPanel->GetValidationRunCountForTests(), ValidationBeforeClearUndo + 1);

	const int32 ValidationBeforeClearRedo =
		ValidationPanel->GetValidationRunCountForTests();
	TestTrue(TEXT("One Redo reapplies the clear"), GEditor->RedoTransaction());
	FlushObservedRefreshes();
	TestFalse(TEXT("Clear Redo restores configured-empty occupancy"),
		Profile->HasActiveDirectionalSlots(AnimationIndex));
	TestEqual(TEXT("Clear Redo restores base preview art"),
		Fixture.Model->GetDirectionalPreviewFlipbook(), Base);
	TestOwner(TEXT("Clear Redo removes the replacement owner alias"), Replacement, false);
	TestEqual(TEXT("Clear Redo refreshes validation"),
		ValidationPanel->GetValidationRunCountForTests(), ValidationBeforeClearRedo + 1);

	Fixture.Model->OnAssetDataChanged.Remove(ModelRefreshHandle);
	GEditor->ResetTransaction(FText::FromString(
		TEXT("TASK-190 directional slot transaction end")));
	Package->SetDirtyFlag(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalWheelAccessibleSegmentStateTest,
	"Paper2DPlus.DirectionalAnimation.Editor.Authoring.AccessibleSegmentStateIsComplete",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalWheelAccessibleSegmentStateTest::RunTest(
	const FString& Parameters)
{
	TArray<FPaper2DPlusDirectionalWheelSegment> Segments;
	Segments.SetNum(8);
	for (int32 SlotIndex = 0; SlotIndex < Segments.Num(); ++SlotIndex)
	{
		Segments[SlotIndex].SlotIndex = SlotIndex;
		Segments[SlotIndex].bValid = true;
	}
	Segments[0].bOccupied = true;
	Segments[0].AssetLabel = FText::FromString(TEXT("NorthVariant"));
	Segments[2].bOccupied = true;
	Segments[2].bValid = false;
	Segments[2].AssetLabel = FText::FromString(TEXT("BrokenEastVariant"));

	TSharedRef<SDirectionalAnimationWheel> Wheel = SNew(SDirectionalAnimationWheel)
		.DirectionCount(8)
		.AngleOffsetDegrees(0.0f)
		.CurrentSelection(1)
		.BaseAnimationText(FText::FromString(TEXT("Aim")))
		.SegmentStates(Segments);

	const FString Occupied = Wheel->GetAccessibleSegmentText(0).ToString();
	TestTrue(TEXT("Occupied segment text identifies its slot"),
		Occupied.Contains(TEXT("Slot 0")));
	TestTrue(TEXT("Occupied segment text exposes its independent center bearing"),
		Occupied.Contains(TEXT("center 0.0 degrees clockwise from up")));
	TestTrue(TEXT("Occupied segment text names occupancy and asset without color"),
		Occupied.Contains(TEXT("occupied"))
		&& Occupied.Contains(TEXT("NorthVariant")));
	TestTrue(TEXT("Occupied segment text distinguishes selection and validation"),
		Occupied.Contains(TEXT("not selected"))
		&& Occupied.Contains(TEXT("valid")));

	const FString SelectedEmpty = Wheel->GetAccessibleSegmentText(1).ToString();
	TestTrue(TEXT("Selected empty segment text exposes the exact 45-degree center"),
		SelectedEmpty.Contains(TEXT("Slot 1"))
		&& SelectedEmpty.Contains(TEXT("center 45.0 degrees clockwise from up")));
	TestTrue(TEXT("Selected empty segment remains distinguishable without color"),
		SelectedEmpty.Contains(TEXT("empty"))
		&& SelectedEmpty.Contains(TEXT("no asset"))
		&& SelectedEmpty.Contains(TEXT("selected"))
		&& SelectedEmpty.Contains(TEXT("valid")));

	const FString Invalid = Wheel->GetAccessibleSegmentText(2).ToString();
	TestTrue(TEXT("Invalid occupied segment names its asset and validation failure"),
		Invalid.Contains(TEXT("Slot 2"))
		&& Invalid.Contains(TEXT("center 90.0 degrees clockwise from up"))
		&& Invalid.Contains(TEXT("occupied"))
		&& Invalid.Contains(TEXT("BrokenEastVariant"))
		&& Invalid.Contains(TEXT("invalid")));
	TestTrue(TEXT("Out-of-range segment text fails closed as unavailable"),
		Wheel->GetAccessibleSegmentText(99).ToString().Contains(TEXT("unavailable")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalAuthoringProspectiveRepairTest,
	"Paper2DPlus.DirectionalAnimation.Editor.Authoring.ProspectiveValidationDoesNotTrapRepair",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalAuthoringProspectiveRepairTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusDirectionalAnimationEditorTest;
	UPaper2DPlusCharacterProfileAsset* Profile =
		NewObject<UPaper2DPlusCharacterProfileAsset>();
	UPaperFlipbook* Variant = MakeFlipbook(Profile, TEXT("RepairVariant"));
	const int32 AnimationIndex = AddAnimation(Profile, TEXT("AimWithoutBase"));
	FPanelFixture Fixture(Profile);
	const FProfileAnimationIdentity Identity =
		Fixture.Panel->GetSelectedDirectionalAnimationIdentity();

	TestFalse(TEXT("Enabling cannot create a set without its canonical base"),
		Fixture.Panel->EnableDirectionalSet(Identity));
	TestFalse(TEXT("First assignment cannot implicitly create a set without its canonical base"),
		Fixture.Panel->CommitDirectionalSlot(Identity, 0, Variant));
	TestFalse(TEXT("Both prospective refusals leave explicit set presence absent"),
		Profile->HasDirectionalSet(AnimationIndex));
	TestTrue(TEXT("The refusal names the actionable canonical-base requirement"),
		Fixture.Panel->GetLastDirectionalIssueTextForTests().Contains(
			TEXT("canonical base")));

	// An absent-set-with-records state can also arrive from old serialized data. Clear repairs that
	// state without permanently enabling a set that still lacks its required base.
	FPaper2DPlusDirectionalAnimationData& AbsentData =
		Profile->Flipbooks[AnimationIndex].DirectionalAnimationData;
	FPaper2DPlusDirectionalAnimationSlot& AbsentRecord =
		AbsentData.Slots.AddDefaulted_GetRef();
	AbsentRecord.SlotIndex = 0;
	AbsentRecord.Flipbook = Variant;
	TestTrue(TEXT("Clear repairs an authored record whose set-presence flag is absent"),
		Fixture.Panel->ClearDirectionalSlot(Identity, 0));
	TestFalse(TEXT("Repair does not implicitly enable the base-less set"),
		Profile->HasDirectionalSet(AnimationIndex));
	TestTrue(TEXT("The inconsistent absent-set records are removed"),
		AbsentData.Slots.IsEmpty());

	// Simulate a malformed legacy/edit-external state. Clear and Remove must remain available as the
	// recovery path even though validating the source state would fail on the missing base.
	FPaper2DPlusDirectionalAnimationData& MissingBaseData =
		Profile->Flipbooks[AnimationIndex].DirectionalAnimationData;
	MissingBaseData.bHasDirectionalSet = true;
	FPaper2DPlusDirectionalAnimationSlot& Occupied =
		MissingBaseData.Slots.AddDefaulted_GetRef();
	Occupied.SlotIndex = 0;
	Occupied.Flipbook = Variant;
	TestTrue(TEXT("Clear remains available while the current set is structurally invalid"),
		Fixture.Panel->ClearDirectionalSlot(Identity, 0));
	TestTrue(TEXT("Remove completes recovery after the invalid set is emptied"),
		Fixture.Panel->RemoveDirectionalSet(Identity));
	TestFalse(TEXT("Repair returns the animation to a valid absent/base-only set state"),
		Profile->HasDirectionalSet(AnimationIndex));

	// Replacement is also prospective: duplicate source keys are repaired by the runtime mutator's
	// replace-one-slot operation instead of being rejected merely because the source is malformed.
	Profile->Flipbooks[AnimationIndex].Identity.Flipbook =
		MakeFlipbook(Profile, TEXT("RepairBase"));
	FPaper2DPlusDirectionalAnimationData& DuplicateData =
		Profile->Flipbooks[AnimationIndex].DirectionalAnimationData;
	DuplicateData.bHasDirectionalSet = true;
	FPaper2DPlusDirectionalAnimationSlot& FirstDuplicate =
		DuplicateData.Slots.AddDefaulted_GetRef();
	FirstDuplicate.SlotIndex = 0;
	FirstDuplicate.Flipbook = Variant;
	FPaper2DPlusDirectionalAnimationSlot& SecondDuplicate =
		DuplicateData.Slots.AddDefaulted_GetRef();
	SecondDuplicate.SlotIndex = 0;
	SecondDuplicate.Flipbook = MakeFlipbook(Profile, TEXT("RepairOldDuplicate"));
	UPaperFlipbook* Replacement = MakeFlipbook(Profile, TEXT("RepairReplacement"));
	TestTrue(TEXT("Replacement repairs duplicate source keys in one mutation"),
		Fixture.Panel->CommitDirectionalSlot(Identity, 0, Replacement));
	FPaper2DPlusDirectionalStructureResult StructureResult;
	TestTrue(TEXT("The replacement leaves the complete directional structure valid"),
		Profile->CheckDirectionalAnimationStructure(AnimationIndex, StructureResult));
	TSoftObjectPtr<UPaperFlipbook> Stored;
	TestTrue(TEXT("The repaired slot remains occupied"),
		Profile->GetDirectionalSlot(AnimationIndex, 0, Stored));
	TestEqual(TEXT("The repaired slot stores the requested replacement"),
		Stored.ToSoftObjectPath(), FSoftObjectPath(Replacement));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalAuthoringInvalidProfileDefaultsRepairTest,
	"Paper2DPlus.DirectionalAnimation.Editor.Authoring.InvalidProfileDefaultsCanBeRepaired",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalAuthoringInvalidProfileDefaultsRepairTest::RunTest(
	const FString& Parameters)
{
	using namespace Paper2DPlusDirectionalAnimationEditorTest;
	UPaper2DPlusCharacterProfileAsset* Profile =
		NewObject<UPaper2DPlusCharacterProfileAsset>();
	const int32 AnimationIndex = AddAnimation(
		Profile, TEXT("Idle"), MakeFlipbook(Profile, TEXT("InvalidDefaultsBase")));
	FPaper2DPlusDirectionalAnimationData& DirectionalData =
		Profile->Flipbooks[AnimationIndex].DirectionalAnimationData;
	DirectionalData.bHasDirectionalSet = true;
	FPaper2DPlusDirectionalAnimationSlot& Occupied =
		DirectionalData.Slots.AddDefaulted_GetRef();
	Occupied.SlotIndex = 1;
	Occupied.Flipbook = MakeFlipbook(Profile, TEXT("InvalidDefaultsVariant"));
	Profile->DefaultDirectionalCount = 2;
	FPanelFixture Fixture(Profile);

	const FProfileDirectionalTopologyImpact Impact =
		SProfileDetailsPanel::BuildDirectionalDefaultsImpactForTests(Profile, 8, 0.0f);
	TestTrue(TEXT("A valid proposal can preflight malformed current Profile defaults"),
		Impact.bRequestValid);
	if (TestEqual(TEXT("The occupied slot receives one explicit repair impact row"),
		Impact.Items.Num(), 1))
	{
		TestFalse(TEXT("The report does not fabricate a bearing from the invalid count"),
			Impact.Items[0].bOldBearingValid);
		TestTrue(TEXT("The report identifies the unknown old bearing"),
			Impact.Items[0].ToDeterministicString().Contains(TEXT("invalid bearing")));
	}
	TestTrue(TEXT("Confirming the repair commits the valid Profile defaults"),
		Fixture.Panel->CommitDirectionalDefaultsForTests(8, 0.0f, true));
	TestEqual(TEXT("The repaired Profile stores the requested direction count"),
		Profile->DefaultDirectionalCount, 8);
	FPaper2DPlusDirectionalStructureResult StructureResult;
	TestTrue(TEXT("The repaired animation has a valid complete directional structure"),
		Profile->CheckDirectionalAnimationStructure(AnimationIndex, StructureResult));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalAuthoringInvalidProfileOffsetRepairTest,
	"Paper2DPlus.DirectionalAnimation.Editor.Authoring.InvalidProfileOffsetCanCrossConfirmationBoundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalAuthoringInvalidProfileOffsetRepairTest::RunTest(
	const FString& Parameters)
{
	using namespace Paper2DPlusDirectionalAnimationEditorTest;
	UPaper2DPlusCharacterProfileAsset* Profile =
		NewObject<UPaper2DPlusCharacterProfileAsset>();
	const int32 AnimationIndex = AddAnimation(
		Profile, TEXT("Idle"), MakeFlipbook(Profile, TEXT("InvalidOffsetBase")));
	FPaper2DPlusDirectionalAnimationData& DirectionalData =
		Profile->Flipbooks[AnimationIndex].DirectionalAnimationData;
	DirectionalData.bHasDirectionalSet = true;
	FPaper2DPlusDirectionalAnimationSlot& Occupied =
		DirectionalData.Slots.AddDefaulted_GetRef();
	Occupied.SlotIndex = 1;
	Occupied.Flipbook = MakeFlipbook(Profile, TEXT("InvalidOffsetVariant"));
	Profile->DefaultDirectionalAngleOffset =
		std::numeric_limits<float>::quiet_NaN();
	FPanelFixture Fixture(Profile);
	const FProfileDirectionalTopologyImpact Impact =
		SProfileDetailsPanel::BuildDirectionalDefaultsImpactForTests(Profile, 8, 0.0f);
	TestTrue(TEXT("The invalid source offset still produces a valid repair preflight"),
		Impact.bRequestValid);
	TestFalse(TEXT("The repair leaves the occupied slot active"),
		Impact.HasStrandedAssignments());
	TestEqual(TEXT("The repair explicitly reports the occupied bearing reinterpretation"),
		Impact.CountReinterpretedAssignments(), 1);

	TestTrue(TEXT("An unchanged NaN source offset remains stable across confirmation revalidation"),
		Fixture.Panel->CommitDirectionalDefaultsForTests(8, 0.0f, true));
	TestTrue(TEXT("The confirmed repair stores a finite offset"),
		FMath::IsFinite(Profile->DefaultDirectionalAngleOffset));
	TestEqual(TEXT("The confirmed repair stores exact positive zero bits"),
		FPlatformMath::AsUInt(Profile->DefaultDirectionalAngleOffset),
		FPlatformMath::AsUInt(0.0f));
	FPaper2DPlusDirectionalStructureResult StructureResult;
	TestTrue(TEXT("The repaired offset leaves the directional structure valid"),
		Profile->CheckDirectionalAnimationStructure(AnimationIndex, StructureResult));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalAuthoringInactiveSlotRepairTest,
	"Paper2DPlus.DirectionalAnimation.Editor.Authoring.InactiveOccupiedSlotCanBeReactivated",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalAuthoringInactiveSlotRepairTest::RunTest(
	const FString& Parameters)
{
	using namespace Paper2DPlusDirectionalAnimationEditorTest;
	UPaper2DPlusCharacterProfileAsset* Profile =
		NewObject<UPaper2DPlusCharacterProfileAsset>();
	const int32 AnimationIndex = AddAnimation(
		Profile, TEXT("Run"), MakeFlipbook(Profile, TEXT("InactiveSlotBase")));
	Profile->DefaultDirectionalCount = 4;
	FPaper2DPlusDirectionalAnimationData& DirectionalData =
		Profile->Flipbooks[AnimationIndex].DirectionalAnimationData;
	DirectionalData.bHasDirectionalSet = true;
	FPaper2DPlusDirectionalAnimationSlot& Occupied =
		DirectionalData.Slots.AddDefaulted_GetRef();
	Occupied.SlotIndex = 5;
	Occupied.Flipbook = MakeFlipbook(Profile, TEXT("InactiveSlotVariant"));
	FPanelFixture Fixture(Profile);

	const FProfileDirectionalTopologyImpact Impact =
		SProfileDetailsPanel::BuildDirectionalDefaultsImpactForTests(Profile, 8, 0.0f);
	TestTrue(TEXT("Increasing the count can preflight an inactive occupied slot repair"),
		Impact.bRequestValid);
	if (TestEqual(TEXT("The reactivated slot receives one repair impact row"),
		Impact.Items.Num(), 1))
	{
		TestFalse(TEXT("An inactive old slot has no physical old bearing"),
			Impact.Items[0].bOldBearingValid);
		TestTrue(TEXT("The report renders the old slot as inactive instead of fabricating a center"),
			Impact.Items[0].ToDeterministicString().Contains(TEXT("invalid bearing")));
	}
	TestTrue(TEXT("Confirming the repair reactivates the occupied slot"),
		Fixture.Panel->CommitDirectionalDefaultsForTests(8, 0.0f, true));
	FPaper2DPlusDirectionalStructureResult StructureResult;
	TestTrue(TEXT("The count increase leaves the repaired directional structure valid"),
		Profile->CheckDirectionalAnimationStructure(AnimationIndex, StructureResult));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalAuthoringInvalidLocalOverrideRepairTest,
	"Paper2DPlus.DirectionalAnimation.Editor.Authoring.InvalidLocalOverrideCanBeRepaired",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalAuthoringInvalidLocalOverrideRepairTest::RunTest(
	const FString& Parameters)
{
	using namespace Paper2DPlusDirectionalAnimationEditorTest;
	UPaper2DPlusCharacterProfileAsset* Profile =
		NewObject<UPaper2DPlusCharacterProfileAsset>();
	const int32 AnimationIndex = AddAnimation(
		Profile, TEXT("Shoot"), MakeFlipbook(Profile, TEXT("InvalidOverrideBase")));
	FPaper2DPlusDirectionalAnimationData& DirectionalData =
		Profile->Flipbooks[AnimationIndex].DirectionalAnimationData;
	DirectionalData.bHasDirectionalSet = true;
	DirectionalData.bOverrideProfileSettings = true;
	DirectionalData.DirectionCount = 5;
	DirectionalData.AngleOffsetDegrees =
		std::numeric_limits<float>::quiet_NaN();
	FPaper2DPlusDirectionalAnimationSlot& Occupied =
		DirectionalData.Slots.AddDefaulted_GetRef();
	Occupied.SlotIndex = 1;
	Occupied.Flipbook = MakeFlipbook(Profile, TEXT("InvalidOverrideVariant"));
	FPanelFixture Fixture(Profile);
	const FProfileAnimationIdentity Identity =
		Fixture.Panel->GetSelectedDirectionalAnimationIdentity();

	const FProfileDirectionalTopologyImpact Impact =
		SProfileDetailsPanel::BuildDirectionalOverrideImpactForTests(
			Profile, Identity, true, 5, 0.0f);
	TestTrue(TEXT("A valid proposal can preflight a malformed current local override"),
		Impact.bRequestValid);
	if (TestEqual(TEXT("The occupied slot receives one explicit local repair row"),
		Impact.Items.Num(), 1))
	{
		TestFalse(TEXT("The local report does not fabricate a bearing from the invalid offset"),
			Impact.Items[0].bOldBearingValid);
	}
	TestTrue(TEXT("Confirming the repair commits the valid local override"),
		Fixture.Panel->CommitDirectionalOverrideForTests(
			Identity, true, 5, 0.0f, true));
	TestEqual(TEXT("The repaired animation stores the requested local count"),
		Profile->Flipbooks[AnimationIndex].DirectionalAnimationData.DirectionCount, 5);
	TestTrue(TEXT("The repaired animation stores a finite local offset"),
		FMath::IsFinite(
			Profile->Flipbooks[AnimationIndex].DirectionalAnimationData.AngleOffsetDegrees));
	TestEqual(TEXT("The repaired animation stores exact positive zero offset bits"),
		FPlatformMath::AsUInt(
			Profile->Flipbooks[AnimationIndex].DirectionalAnimationData.AngleOffsetDegrees),
		FPlatformMath::AsUInt(0.0f));
	FPaper2DPlusDirectionalStructureResult StructureResult;
	TestTrue(TEXT("The repaired local override has a valid complete directional structure"),
		Profile->CheckDirectionalAnimationStructure(AnimationIndex, StructureResult));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalAuthoringInvalidProfilePairNumericRepairTest,
	"Paper2DPlus.DirectionalAnimation.Editor.Authoring.EmptyProfileDefaultsRemainVisibleAndRepairInvalidPairAtomically",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalAuthoringInvalidProfilePairNumericRepairTest::RunTest(
	const FString& Parameters)
{
	using namespace Paper2DPlusDirectionalAnimationEditorTest;
	if (!TestNotNull(TEXT("GEditor is required for the authoritative transaction proof"), GEditor))
	{
		return false;
	}

	const FString PackageName = FString::Printf(
		TEXT("/Game/Paper2DPlusTests/DirectionalEmptyProfileRepair_%s"),
		*FGuid::NewGuid().ToString(EGuidFormats::Digits));
	UPackage* Package = CreatePackage(*PackageName);
	UPaper2DPlusCharacterProfileAsset* Profile =
		NewObject<UPaper2DPlusCharacterProfileAsset>(Package, TEXT("Profile"));
	Profile->DefaultDirectionalCount = 2;
	Profile->DefaultDirectionalAngleOffset =
		std::numeric_limits<float>::quiet_NaN();
	FPanelFixture Fixture(Profile);
	Package->SetDirtyFlag(false);
	int32 AssetDataNotificationCount = 0;
	const FDelegateHandle NotificationHandle = Fixture.Model->OnAssetDataChanged.AddLambda(
		[&AssetDataNotificationCount]()
		{
			++AssetDataNotificationCount;
		});
	GEditor->ResetTransaction(FText::FromString(
		TEXT("TASK-190 empty Profile defaults repair start")));

	TestTrue(TEXT("Profile defaults remain rendered with no animations and no selection"),
		Fixture.Panel->IsDirectionalProfileDefaultsSurfaceVisibleForTests());
	TestFalse(TEXT("Animation-local numeric authoring remains gated without a selection"),
		Fixture.Panel->BeginLocalDirectionalCountEditForTests());
	TestTrue(TEXT("The Profile Count control captures the empty Profile owner"),
		Fixture.Panel->BeginProfileDirectionalCountEditForTests());
	TestTrue(TEXT("One user-facing Count commit repairs the simultaneously invalid pair"),
		Fixture.Panel->CommitProfileDirectionalCountEditForTests(8, true));
	TestEqual(TEXT("The Profile Count control stores the requested value"),
		Profile->DefaultDirectionalCount, 8);
	TestEqual(TEXT("The invalid Profile offset counterpart repairs to zero"),
		Profile->DefaultDirectionalAngleOffset, 0.0f);
	TestTrue(TEXT("The successful commit dirties the owning Profile package"),
		Package->IsDirty());
	TestEqual(TEXT("The paired repair emits exactly one model data notification"),
		AssetDataNotificationCount, 1);
	TestTrue(TEXT("One Undo reverts the complete paired repair"),
		GEditor->UndoTransaction(true));
	TestEqual(TEXT("Undo restores the malformed source count"),
		Profile->DefaultDirectionalCount, 2);
	TestTrue(TEXT("Undo restores the malformed source offset as NaN"),
		FMath::IsNaN(Profile->DefaultDirectionalAngleOffset));

	Fixture.Model->OnAssetDataChanged.Remove(NotificationHandle);
	GEditor->ResetTransaction(FText::FromString(
		TEXT("TASK-190 empty Profile defaults repair end")));
	Package->SetDirtyFlag(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalAuthoringProfileNumericOwnerExpiryTest,
	"Paper2DPlus.DirectionalAnimation.Editor.Authoring.ProfileNumericEditExpiresOnModelProfileSwap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalAuthoringProfileNumericOwnerExpiryTest::RunTest(
	const FString& Parameters)
{
	using namespace Paper2DPlusDirectionalAnimationEditorTest;
	UPaper2DPlusCharacterProfileAsset* FirstProfile =
		NewObject<UPaper2DPlusCharacterProfileAsset>();
	FPanelFixture Fixture(FirstProfile);
	TestTrue(TEXT("An empty Profile can begin a delayed Profile-default edit"),
		Fixture.Panel->BeginProfileDirectionalCountEditForTests());

	UPaper2DPlusCharacterProfileAsset* SecondProfile =
		NewObject<UPaper2DPlusCharacterProfileAsset>();
	Fixture.Model->InitializeFromAsset(SecondProfile);
	TestFalse(TEXT("A Profile swap expires the captured Profile identity and model generation"),
		Fixture.Panel->CommitProfileDirectionalCountEditForTests(5, true));
	TestEqual(TEXT("The detached Profile is not mutated"),
		FirstProfile->DefaultDirectionalCount, 8);
	TestEqual(TEXT("The newly attached Profile is never retargeted"),
		SecondProfile->DefaultDirectionalCount, 8);
	TestTrue(TEXT("The expired edit projects its Profile/model ownership boundary"),
		Fixture.Panel->GetLastDirectionalIssueTextForTests().Contains(
			TEXT("Profile owner")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalAuthoringInvalidLocalPairNumericRepairTest,
	"Paper2DPlus.DirectionalAnimation.Editor.Authoring.LocalCountControlRepairsInvalidPair",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalAuthoringInvalidLocalPairNumericRepairTest::RunTest(
	const FString& Parameters)
{
	using namespace Paper2DPlusDirectionalAnimationEditorTest;
	UPaper2DPlusCharacterProfileAsset* Profile =
		NewObject<UPaper2DPlusCharacterProfileAsset>();
	const int32 AnimationIndex = AddAnimation(
		Profile, TEXT("Shoot"), MakeFlipbook(Profile, TEXT("InvalidLocalPairBase")));
	FPaper2DPlusDirectionalAnimationData& DirectionalData =
		Profile->Flipbooks[AnimationIndex].DirectionalAnimationData;
	DirectionalData.bHasDirectionalSet = true;
	DirectionalData.bOverrideProfileSettings = true;
	DirectionalData.DirectionCount = 2;
	DirectionalData.AngleOffsetDegrees = std::numeric_limits<float>::quiet_NaN();
	FPanelFixture Fixture(Profile);

	TestTrue(TEXT("The local Count control captures its stable animation owner"),
		Fixture.Panel->BeginLocalDirectionalCountEditForTests());
	TestTrue(TEXT("Entering a valid local count repairs its invalid offset counterpart"),
		Fixture.Panel->CommitLocalDirectionalCountEditForTests(5, true));
	TestEqual(TEXT("The local Count control stores the requested value"),
		DirectionalData.DirectionCount, 5);
	TestEqual(TEXT("The invalid local offset counterpart inherits the valid Profile offset"),
		DirectionalData.AngleOffsetDegrees, 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalAuthoringInvalidLocalOverrideDisableTest,
	"Paper2DPlus.DirectionalAnimation.Editor.Authoring.LocalSettingsCheckboxCanDisableInvalidOverride",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalAuthoringInvalidLocalOverrideDisableTest::RunTest(
	const FString& Parameters)
{
	using namespace Paper2DPlusDirectionalAnimationEditorTest;
	UPaper2DPlusCharacterProfileAsset* Profile =
		NewObject<UPaper2DPlusCharacterProfileAsset>();
	const int32 AnimationIndex = AddAnimation(
		Profile, TEXT("Aim"), MakeFlipbook(Profile, TEXT("InvalidDisableBase")));
	FPaper2DPlusDirectionalAnimationData& DirectionalData =
		Profile->Flipbooks[AnimationIndex].DirectionalAnimationData;
	DirectionalData.bHasDirectionalSet = true;
	DirectionalData.bOverrideProfileSettings = true;
	DirectionalData.DirectionCount = 2;
	FPanelFixture Fixture(Profile);

	TestTrue(TEXT("Unchecking Local Settings can leave a malformed override"),
		Fixture.Panel->CommitSelectedDirectionalOverrideEnabledForTests(false, true));
	TestFalse(TEXT("The invalid local override is disabled"),
		DirectionalData.bOverrideProfileSettings);
	int32 EffectiveCount = 0;
	float EffectiveOffset = 0.0f;
	TestTrue(TEXT("The repaired entry resolves the valid inherited Profile topology"),
		Profile->GetEffectiveDirectionalSettings(
			AnimationIndex, EffectiveCount, EffectiveOffset));
	TestEqual(TEXT("The inherited count is restored"), EffectiveCount, 8);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalAuthoringInheritanceTest,
	"Paper2DPlus.DirectionalAnimation.Editor.Authoring.InheritedAndOverrideThreeToSixteen",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalAuthoringInheritanceTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusDirectionalAnimationEditorTest;
	UPaper2DPlusCharacterProfileAsset* Profile =
		NewObject<UPaper2DPlusCharacterProfileAsset>();
	const int32 AnimationIndex = AddAnimation(
		Profile, TEXT("Aim"), MakeFlipbook(Profile, TEXT("InheritanceBase")));
	FPanelFixture Fixture(Profile);
	const FProfileAnimationIdentity Identity =
		Fixture.Panel->GetSelectedDirectionalAnimationIdentity();

	TestTrue(TEXT("Empty Profile defaults can author the minimum count"),
		Fixture.Panel->CommitDirectionalDefaultsForTests(3, -45.0f, true));
	TestTrue(TEXT("The animation can create configured-empty presence"),
		Fixture.Panel->EnableDirectionalSet(Identity));
	int32 EffectiveCount = 0;
	float EffectiveOffset = 0.0f;
	TestTrue(TEXT("Inherited topology resolves"),
		Profile->GetEffectiveDirectionalSettings(
			AnimationIndex, EffectiveCount, EffectiveOffset));
	TestEqual(TEXT("Inherited count reaches the supported minimum"), EffectiveCount, 3);
	TestEqual(TEXT("Inherited offset is the Profile value"), EffectiveOffset, -45.0f);
	TestFalse(TEXT("The configured set initially inherits both values"),
		Profile->Flipbooks[AnimationIndex]
			.DirectionalAnimationData.bOverrideProfileSettings);

	TestTrue(TEXT("One local override authors both values at the supported maximum"),
		Fixture.Panel->CommitDirectionalOverrideForTests(
			Identity, true, 16, 45.0f, true));
	TestTrue(TEXT("Local override topology resolves"),
		Profile->GetEffectiveDirectionalSettings(
			AnimationIndex, EffectiveCount, EffectiveOffset));
	TestEqual(TEXT("Override count reaches sixteen"), EffectiveCount, 16);
	TestEqual(TEXT("Override offset reaches positive forty-five"), EffectiveOffset, 45.0f);
	TestTrue(TEXT("Override presence is explicit"),
		Profile->Flipbooks[AnimationIndex]
			.DirectionalAnimationData.bOverrideProfileSettings);

	TestTrue(TEXT("Disabling the override returns both values to inheritance"),
		Fixture.Panel->CommitDirectionalOverrideForTests(
			Identity, false, 16, 45.0f, true));
	TestTrue(TEXT("Restored inherited topology resolves"),
		Profile->GetEffectiveDirectionalSettings(
			AnimationIndex, EffectiveCount, EffectiveOffset));
	TestEqual(TEXT("Inherited minimum count is restored"), EffectiveCount, 3);
	TestEqual(TEXT("Inherited Profile offset is restored"), EffectiveOffset, -45.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalAuthoringStrandingTest,
	"Paper2DPlus.DirectionalAnimation.Editor.Authoring.LocalAndProfileStrandingAreAtomic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalAuthoringStrandingTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusDirectionalAnimationEditorTest;
	UPaper2DPlusCharacterProfileAsset* Profile =
		NewObject<UPaper2DPlusCharacterProfileAsset>();
	const int32 IdleIndex = AddAnimation(
		Profile, TEXT("Idle"), MakeFlipbook(Profile, TEXT("StrandingIdleBase")));
	const int32 RunIndex = AddAnimation(
		Profile, TEXT("Run"), MakeFlipbook(Profile, TEXT("StrandingRunBase")));
	UPaperFlipbook* IdleVariant = MakeFlipbook(Profile, TEXT("StrandingIdleRear"));
	UPaperFlipbook* RunVariant = MakeFlipbook(Profile, TEXT("StrandingRunRear"));
	FPanelFixture Fixture(Profile);
	const FProfileAnimationIdentity IdleIdentity =
		Paper2DPlusProfileToolProvider::MakeAnimationIdentity(Profile, IdleIndex);
	const FProfileAnimationIdentity RunIdentity =
		Paper2DPlusProfileToolProvider::MakeAnimationIdentity(Profile, RunIndex);

	TestTrue(TEXT("Idle slot seven is assigned through the stable-owner seam"),
		Fixture.Panel->CommitDirectionalSlot(IdleIdentity, 7, IdleVariant));
	TestTrue(TEXT("Run slot five is assigned through the stable-owner seam"),
		Fixture.Panel->CommitDirectionalSlot(RunIdentity, 5, RunVariant));
	const FProfileDirectionalTopologyImpact DefaultImpact =
		SProfileDetailsPanel::BuildDirectionalDefaultsImpactForTests(Profile, 4, 0.0f);
	TestTrue(TEXT("A valid proposal receives a complete impact report"),
		DefaultImpact.bRequestValid);
	TestTrue(TEXT("The report identifies a Profile-default proposal"),
		DefaultImpact.bProfileDefaultsChange);
	TestEqual(TEXT("The report preserves the current Profile count"),
		DefaultImpact.CurrentDirectionCount, 8);
	TestEqual(TEXT("The report preserves the attempted Profile count"),
		DefaultImpact.ProposedDirectionCount, 4);
	if (TestEqual(TEXT("Both inheriting entries are reported as stranded"),
		DefaultImpact.CountStrandedAssignments(), 2)
		&& TestEqual(TEXT("The complete impact contains two rows"),
			DefaultImpact.Items.Num(), 2))
	{
		TestEqual(TEXT("Affected rows remain in authored animation order"),
			DefaultImpact.Items[0].AnimationName, FString(TEXT("Idle")));
		TestEqual(TEXT("The second affected row is Run"),
			DefaultImpact.Items[1].AnimationName, FString(TEXT("Run")));
	}
	TestTrue(TEXT("The deterministic issue names Idle slot seven"),
		DefaultImpact.IssueText.Contains(TEXT("Idle | slot 7")));
	TestTrue(TEXT("The deterministic issue names Run slot five"),
		DefaultImpact.IssueText.Contains(TEXT("Run | slot 5")));

	TestFalse(TEXT("Confirmation cannot override a stranding block"),
		Fixture.Panel->CommitDirectionalDefaultsForTests(
			4, 0.0f, true));
	TestEqual(TEXT("A blocked Profile change leaves the count untouched"),
		Profile->DefaultDirectionalCount, 8);
	TestEqual(TEXT("A blocked Profile change leaves both assignments untouched"),
		Profile->Flipbooks[IdleIndex].DirectionalAnimationData.Slots.Num()
			+ Profile->Flipbooks[RunIndex].DirectionalAnimationData.Slots.Num(),
		2);

	TestTrue(TEXT("Idle can opt into the current eight-direction topology"),
		Fixture.Panel->CommitDirectionalOverrideForTests(
			IdleIdentity, true, 8, 0.0f, true));
	TestFalse(TEXT("A local count reduction also blocks its occupied slot atomically"),
		Fixture.Panel->CommitDirectionalOverrideForTests(
			IdleIdentity, true, 4, 0.0f, true));
	TestEqual(TEXT("The local count remains eight after refusal"),
		Profile->Flipbooks[IdleIndex].DirectionalAnimationData.DirectionCount, 8);
	const FProfileDirectionalTopologyImpact& LocalImpact =
		Fixture.Panel->GetLastDirectionalImpactForTests();
	TestFalse(TEXT("The local report is not labeled as a Profile-default change"),
		LocalImpact.bProfileDefaultsChange);
	TestEqual(TEXT("The local report preserves the current count"),
		LocalImpact.CurrentDirectionCount, 8);
	TestEqual(TEXT("The local report preserves the attempted count"),
		LocalImpact.ProposedDirectionCount, 4);
	if (TestEqual(TEXT("The local refusal has one impact row"),
		LocalImpact.Items.Num(), 1))
	{
		TestEqual(TEXT("The local report identifies the exact stranded slot"),
			LocalImpact.Items[0].SlotIndex, 7);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalAuthoringReinterpretationTest,
	"Paper2DPlus.DirectionalAnimation.Editor.Authoring.ReinterpretationCancelAndConfirm",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalAuthoringReinterpretationTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusDirectionalAnimationEditorTest;
	UPaper2DPlusCharacterProfileAsset* Profile =
		NewObject<UPaper2DPlusCharacterProfileAsset>();
	const int32 AnimationIndex = AddAnimation(
		Profile, TEXT("Shoot"), MakeFlipbook(Profile, TEXT("ReinterpretBase")));
	FPanelFixture Fixture(Profile);
	const FProfileAnimationIdentity Identity =
		Fixture.Panel->GetSelectedDirectionalAnimationIdentity();
	TestTrue(TEXT("Slot one is occupied before changing offset"),
		Fixture.Panel->CommitDirectionalSlot(
			Identity, 1, MakeFlipbook(Profile, TEXT("ReinterpretNorthEast"))));

	const FProfileDirectionalTopologyImpact Impact =
		SProfileDetailsPanel::BuildDirectionalDefaultsImpactForTests(Profile, 8, 10.0f);
	TestTrue(TEXT("The offset proposal is structurally valid"), Impact.bRequestValid);
	TestEqual(TEXT("The report preserves the current offset"),
		Impact.CurrentAngleOffsetDegrees, 0.0f);
	TestEqual(TEXT("The report preserves the attempted offset"),
		Impact.ProposedAngleOffsetDegrees, 10.0f);
	TestFalse(TEXT("The occupied slot remains active"), Impact.HasStrandedAssignments());
	if (TestEqual(TEXT("Exactly one occupied bearing is reinterpreted"),
		Impact.CountReinterpretedAssignments(), 1)
		&& TestEqual(TEXT("The complete impact contains one row"), Impact.Items.Num(), 1))
	{
		TestEqual(TEXT("The old slot-one center is forty-five degrees"),
			Impact.Items[0].OldBearingDegrees, 45.0);
		TestEqual(TEXT("A positive ten-degree offset moves the center to thirty-five"),
			Impact.Items[0].NewBearingDegrees, 35.0);
	}

	TestFalse(TEXT("The explicit cancel decision opens no mutation"),
		Fixture.Panel->CommitDirectionalDefaultsForTests(8, 10.0f, false));
	TestEqual(TEXT("Cancel preserves the old offset"),
		Profile->DefaultDirectionalAngleOffset, 0.0f);
	TestTrue(TEXT("Cancel is projected as deterministic issue text"),
		Fixture.Panel->GetLastDirectionalIssueTextForTests().Contains(TEXT("cancelled")));
	TestTrue(TEXT("The explicit confirm decision commits the same proposal"),
		Fixture.Panel->CommitDirectionalDefaultsForTests(8, 10.0f, true));
	TestEqual(TEXT("Confirm stores the proposed offset"),
		Profile->DefaultDirectionalAngleOffset, 10.0f);
	const TArray<FPaper2DPlusDirectionalAnimationSlot>& Slots =
		Profile->Flipbooks[AnimationIndex].DirectionalAnimationData.Slots;
	if (TestEqual(TEXT("The occupied slot remains assigned after reinterpretation"),
		Slots.Num(), 1))
	{
		TestEqual(TEXT("The surviving assignment keeps slot one"), Slots[0].SlotIndex, 1);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalAuthoringConfirmationBoundaryTest,
	"Paper2DPlus.DirectionalAnimation.Editor.Authoring.ConfirmationBoundaryRechecksLiveOwnerAndImpact",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalAuthoringConfirmationBoundaryTest::RunTest(
	const FString& Parameters)
{
	using namespace Paper2DPlusDirectionalAnimationEditorTest;
	UPaper2DPlusCharacterProfileAsset* FirstProfile =
		NewObject<UPaper2DPlusCharacterProfileAsset>();
	AddAnimation(
		FirstProfile, TEXT("Idle"), MakeFlipbook(FirstProfile, TEXT("ConfirmIdleBase")));
	AddAnimation(
		FirstProfile, TEXT("Run"), MakeFlipbook(FirstProfile, TEXT("ConfirmRunBase")));
	FPanelFixture Fixture(FirstProfile);
	const FProfileAnimationIdentity IdleIdentity =
		Fixture.Panel->GetSelectedDirectionalAnimationIdentity();
	TestTrue(TEXT("An occupied slot establishes a confirmation-worthy topology impact"),
		Fixture.Panel->CommitDirectionalSlot(
			IdleIdentity,
			1,
			MakeFlipbook(FirstProfile, TEXT("ConfirmIdleNorthEast"))));

	TestFalse(TEXT("A Profile reorder while confirmation is open invalidates the decision"),
		Fixture.Panel->CommitDirectionalDefaultsWithConfirmationForTests(
			8,
			10.0f,
			[&Fixture, FirstProfile](const FProfileDirectionalTopologyImpact&)
			{
				FirstProfile->Flipbooks.Swap(0, 1);
				Fixture.Model->NotifyAssetDataChanged();
				return true;
			}));
	TestEqual(TEXT("The stale confirmation cannot change Profile defaults"),
		FirstProfile->DefaultDirectionalAngleOffset, 0.0f);
	TestTrue(TEXT("The refusal explains that confirmation context changed"),
		Fixture.Panel->GetLastDirectionalIssueTextForTests().Contains(
			TEXT("confirmation was open")));

	UPaper2DPlusCharacterProfileAsset* SecondProfile =
		NewObject<UPaper2DPlusCharacterProfileAsset>();
	const int32 SecondIdleIndex = AddAnimation(
		SecondProfile,
		TEXT("Idle"),
		MakeFlipbook(SecondProfile, TEXT("ConfirmSecondIdleBase")));
	TestFalse(TEXT("Model reinitialization while confirming a local override expires the owner"),
		Fixture.Panel->CommitDirectionalOverrideWithConfirmationForTests(
			IdleIdentity,
			true,
			8,
			12.0f,
			[&Fixture, SecondProfile](const FProfileDirectionalTopologyImpact&)
			{
				Fixture.Model->InitializeFromAsset(SecondProfile);
				Fixture.Model->SetSelectedFlipbook(0);
				return true;
			}));
	TestFalse(TEXT("The old Profile's directional override remains untouched"),
		FirstProfile->Flipbooks[1]
			.DirectionalAnimationData.bOverrideProfileSettings);
	TestFalse(TEXT("An identical-looking owner in the new Profile is not retargeted"),
		SecondProfile->Flipbooks[SecondIdleIndex]
			.DirectionalAnimationData.bOverrideProfileSettings);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalAuthoringStableOwnerTest,
	"Paper2DPlus.DirectionalAnimation.Editor.Authoring.StableOwnerSurvivesReorderAndExpiresOnReinit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalAuthoringStableOwnerTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusDirectionalAnimationEditorTest;
	UPaper2DPlusCharacterProfileAsset* FirstProfile =
		NewObject<UPaper2DPlusCharacterProfileAsset>();
	AddAnimation(FirstProfile, TEXT("Idle"), MakeFlipbook(FirstProfile, TEXT("OwnerIdle")));
	AddAnimation(FirstProfile, TEXT("Run"), MakeFlipbook(FirstProfile, TEXT("OwnerRun")));
	FPanelFixture Fixture(FirstProfile);
	const FProfileAnimationIdentity IdleIdentity =
		Fixture.Panel->GetSelectedDirectionalAnimationIdentity();

	FirstProfile->Flipbooks.Swap(0, 1);
	TestTrue(TEXT("A delayed commit re-resolves Idle after array reorder"),
		Fixture.Panel->EnableDirectionalSet(IdleIdentity));
	TestFalse(TEXT("The row now at the captured index was not mutated"),
		FirstProfile->Flipbooks[0].DirectionalAnimationData.bHasDirectionalSet);
	TestTrue(TEXT("The stable Idle owner at its new index was mutated"),
		FirstProfile->Flipbooks[1].DirectionalAnimationData.bHasDirectionalSet);

	UPaper2DPlusCharacterProfileAsset* SecondProfile =
		NewObject<UPaper2DPlusCharacterProfileAsset>();
	AddAnimation(SecondProfile, TEXT("Idle"), MakeFlipbook(SecondProfile, TEXT("OwnerIdle")));
	Fixture.Model->InitializeFromAsset(SecondProfile);
	Fixture.Model->SetSelectedFlipbook(0);
	TestFalse(TEXT("The old Profile's captured identity expires after model reinitialization"),
		Fixture.Panel->EnableDirectionalSet(IdleIdentity));
	TestFalse(TEXT("An identical-looking animation in the new Profile remains untouched"),
		SecondProfile->Flipbooks[0].DirectionalAnimationData.bHasDirectionalSet);
	TestTrue(TEXT("The expiry diagnostic names the owner boundary"),
		Fixture.Panel->GetLastDirectionalIssueTextForTests().Contains(TEXT("Profile owner")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalAuthoringImpactNavigationTest,
	"Paper2DPlus.DirectionalAnimation.Editor.Authoring.ImpactRowsAreCompleteAndNavigateAfterReorder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalAuthoringImpactNavigationTest::RunTest(
	const FString& Parameters)
{
	using namespace Paper2DPlusDirectionalAnimationEditorTest;
	UPaper2DPlusCharacterProfileAsset* Profile =
		NewObject<UPaper2DPlusCharacterProfileAsset>();
	TArray<FProfileAnimationIdentity> Identities;
	for (int32 Index = 0; Index < 10; ++Index)
	{
		const FString AnimationName = FString::Printf(TEXT("Move%02d"), Index);
		const FString BaseName = FString::Printf(TEXT("ImpactBase%02d"), Index);
		const int32 AnimationIndex = AddAnimation(
			Profile,
			*AnimationName,
			MakeFlipbook(Profile, *BaseName));
		Identities.Add(Paper2DPlusProfileToolProvider::MakeAnimationIdentity(
			Profile, AnimationIndex));
	}
	FPanelFixture Fixture(Profile);
	for (int32 Index = 0; Index < Identities.Num(); ++Index)
	{
		const FString VariantName = FString::Printf(TEXT("ImpactVariant%02d"), Index);
		TestTrue(
			*FString::Printf(TEXT("Move %d receives occupied slot one"), Index),
			Fixture.Panel->CommitDirectionalSlot(
				Identities[Index],
				1,
				MakeFlipbook(Profile, *VariantName)));
	}

	TestFalse(TEXT("Cancelling an offset reinterpretation preserves the authored topology"),
		Fixture.Panel->CommitDirectionalDefaultsForTests(
			8, 10.0f, false));
	TestEqual(TEXT("The persistent impact report retains every affected assignment"),
		Fixture.Panel->GetLastDirectionalImpactForTests().Items.Num(), 10);

	Fixture.Model->SetDirectionalPreviewEnabled(true);
	Profile->Flipbooks.Swap(0, 9);
	Fixture.Model->NotifyAssetDataChanged();
	TestTrue(TEXT("The first rendered impact button remains wired after reorder"),
		Fixture.Panel->ActivateDirectionalImpactRowForTests(0));
	TestEqual(TEXT("The first row selects reordered Move00"),
		Fixture.Model->GetSelectedFlipbookIndex(), 9);
	TestTrue(TEXT("A middle rendered impact button remains wired after reorder"),
		Fixture.Panel->ActivateDirectionalImpactRowForTests(5));
	TestEqual(TEXT("The middle row selects Move05"),
		Fixture.Model->GetSelectedFlipbookIndex(), 5);
	TestTrue(TEXT("The untruncated final impact button re-resolves its stable owner"),
		Fixture.Panel->ActivateDirectionalImpactRowForTests(9));
	TestEqual(TEXT("Navigation selects Move09 at its live reordered index"),
		Fixture.Model->GetSelectedFlipbookIndex(), 0);
	TestEqual(TEXT("Navigation commits the affected slot's physical center bearing"),
		Fixture.Model->GetCommittedDirectionalBearing(), 45.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalAuthoringTypedOwnerTest,
	"Paper2DPlus.DirectionalAnimation.Editor.Authoring.TypedNumericEditKeepsCapturedOwner",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalAuthoringTypedOwnerTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusDirectionalAnimationEditorTest;
	UPaper2DPlusCharacterProfileAsset* FirstProfile =
		NewObject<UPaper2DPlusCharacterProfileAsset>();
	const int32 IdleIndex = AddAnimation(
		FirstProfile, TEXT("Idle"), MakeFlipbook(FirstProfile, TEXT("TypedIdleBase")));
	UPaperFlipbook* SharedRunBase =
		MakeFlipbook(FirstProfile, TEXT("TypedSharedRunBase"));
	const int32 RunIndex = AddAnimation(
		FirstProfile, TEXT("Run"), SharedRunBase);
	FPanelFixture Fixture(FirstProfile);
	const FProfileAnimationIdentity IdleIdentity =
		Paper2DPlusProfileToolProvider::MakeAnimationIdentity(FirstProfile, IdleIndex);
	const FProfileAnimationIdentity RunIdentity =
		Paper2DPlusProfileToolProvider::MakeAnimationIdentity(FirstProfile, RunIndex);
	TestTrue(TEXT("Idle creates its local settings owner"),
		Fixture.Panel->EnableDirectionalSet(IdleIdentity));
	TestTrue(TEXT("Idle enables a local override before editing"),
		Fixture.Panel->CommitDirectionalOverrideForTests(
			IdleIdentity, true, 8, 0.0f, true));
	TestTrue(TEXT("Run creates its local settings owner"),
		Fixture.Panel->EnableDirectionalSet(RunIdentity));
	TestTrue(TEXT("Run enables a local override before editing"),
		Fixture.Panel->CommitDirectionalOverrideForTests(
			RunIdentity, true, 8, 0.0f, true));

	Fixture.Model->SetSelectedFlipbook(IdleIndex);
	TestTrue(TEXT("Typing captures Idle before any selection change"),
		Fixture.Panel->BeginLocalDirectionalCountEditForTests());
	Fixture.Model->SetSelectedFlipbook(RunIndex);
	TestTrue(TEXT("Commit still resolves and mutates the captured Idle owner"),
		Fixture.Panel->CommitLocalDirectionalCountEditForTests(6, true));
	TestEqual(TEXT("Idle receives the typed count"),
		FirstProfile->Flipbooks[IdleIndex]
			.DirectionalAnimationData.DirectionCount, 6);
	TestEqual(TEXT("The newly selected Run owner is never retargeted"),
		FirstProfile->Flipbooks[RunIndex]
			.DirectionalAnimationData.DirectionCount, 8);

	TestTrue(TEXT("A second typed edit captures Run"),
		Fixture.Panel->BeginLocalDirectionalCountEditForTests());
	UPaper2DPlusCharacterProfileAsset* SecondProfile =
		NewObject<UPaper2DPlusCharacterProfileAsset>();
	const int32 SecondRunIndex = AddAnimation(
		SecondProfile,
		TEXT("Run"),
		SharedRunBase);
	// Deliberately reuse both the authored name and the exact base-object path. The Profile owner,
	// rather than either animation key, must expire the edit when the model is reinitialized.
	Fixture.Model->InitializeFromAsset(SecondProfile);
	Fixture.Model->SetSelectedFlipbook(SecondRunIndex);
	TestFalse(TEXT("Reinitialization cancels the expired captured edit"),
		Fixture.Panel->CommitLocalDirectionalCountEditForTests(5, true));
	TestEqual(TEXT("The detached Profile cannot receive the expired local count"),
		FirstProfile->Flipbooks[RunIndex]
			.DirectionalAnimationData.DirectionCount, 8);
	TestFalse(TEXT("The new Profile cannot receive the expired local override"),
		SecondProfile->Flipbooks[SecondRunIndex]
			.DirectionalAnimationData.bOverrideProfileSettings);
	TestEqual(TEXT("The new Profile cannot receive the expired local count"),
		SecondProfile->Flipbooks[SecondRunIndex]
			.DirectionalAnimationData.DirectionCount, 8);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalAuthoringCountArrowCommitOwnerTest,
	"Paper2DPlus.DirectionalAnimation.Editor.Authoring.CountArrowCommitDoesNotRecaptureCompletedOwner",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalAuthoringCountArrowCommitOwnerTest::RunTest(
	const FString& Parameters)
{
	using namespace Paper2DPlusDirectionalAnimationEditorTest;
	UPaper2DPlusCharacterProfileAsset* Profile =
		NewObject<UPaper2DPlusCharacterProfileAsset>();
	const int32 IdleIndex = AddAnimation(
		Profile, TEXT("Idle"), MakeFlipbook(Profile, TEXT("CountArrowIdleBase")));
	const int32 RunIndex = AddAnimation(
		Profile, TEXT("Run"), MakeFlipbook(Profile, TEXT("CountArrowRunBase")));
	FPanelFixture Fixture(Profile);
	const FProfileAnimationIdentity IdleIdentity =
		Paper2DPlusProfileToolProvider::MakeAnimationIdentity(Profile, IdleIndex);
	const FProfileAnimationIdentity RunIdentity =
		Paper2DPlusProfileToolProvider::MakeAnimationIdentity(Profile, RunIndex);
	TestTrue(TEXT("Idle creates local directional settings"),
		Fixture.Panel->EnableDirectionalSet(IdleIdentity));
	TestTrue(TEXT("Idle enables a local count override"),
		Fixture.Panel->CommitDirectionalOverrideForTests(
			IdleIdentity, true, 8, 0.0f, true));
	TestTrue(TEXT("Run creates local directional settings"),
		Fixture.Panel->EnableDirectionalSet(RunIdentity));
	TestTrue(TEXT("Run enables a local count override"),
		Fixture.Panel->CommitDirectionalOverrideForTests(
			RunIdentity, true, 8, 0.0f, true));

	TSharedPtr<SWidget> TaggedWidget = FindWidgetByTag(
		Fixture.Panel,
		FName(TEXT("Paper2DPlus.Directional.LocalCount")));
	if (!TestTrue(TEXT("The authored Local Count spin box is available"), TaggedWidget.IsValid()))
	{
		return false;
	}
	TSharedRef<SSpinBox<int32>> LocalCount =
		StaticCastSharedRef<SSpinBox<int32>>(TaggedWidget.ToSharedRef());

	Fixture.Model->SetSelectedFlipbook(IdleIndex);
	Fixture.Panel->SlatePrepass(1.0f);
	TestTrue(TEXT("The Local Count spin box handles Idle's increment"),
		LocalCount->OnKeyDown(FGeometry(), MakeKeyEvent(EKeys::Right)).IsEventHandled());
	Fixture.Model->SetSelectedFlipbook(RunIndex);
	Fixture.Panel->SlatePrepass(1.0f);
	TestTrue(TEXT("The Local Count spin box handles Run's decrement"),
		LocalCount->OnKeyDown(FGeometry(), MakeKeyEvent(EKeys::Left)).IsEventHandled());

	TestEqual(TEXT("Idle keeps the completed increment"),
		Profile->Flipbooks[IdleIndex].DirectionalAnimationData.DirectionCount, 9);
	TestEqual(TEXT("The next arrow commit belongs to newly selected Run"),
		Profile->Flipbooks[RunIndex].DirectionalAnimationData.DirectionCount, 7);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalAuthoringOffsetArrowCommitOwnerTest,
	"Paper2DPlus.DirectionalAnimation.Editor.Authoring.OffsetArrowCommitDoesNotRecaptureCompletedOwner",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalAuthoringOffsetArrowCommitOwnerTest::RunTest(
	const FString& Parameters)
{
	using namespace Paper2DPlusDirectionalAnimationEditorTest;
	UPaper2DPlusCharacterProfileAsset* Profile =
		NewObject<UPaper2DPlusCharacterProfileAsset>();
	const int32 IdleIndex = AddAnimation(
		Profile, TEXT("Idle"), MakeFlipbook(Profile, TEXT("OffsetArrowIdleBase")));
	const int32 RunIndex = AddAnimation(
		Profile, TEXT("Run"), MakeFlipbook(Profile, TEXT("OffsetArrowRunBase")));
	FPanelFixture Fixture(Profile);
	const FProfileAnimationIdentity IdleIdentity =
		Paper2DPlusProfileToolProvider::MakeAnimationIdentity(Profile, IdleIndex);
	const FProfileAnimationIdentity RunIdentity =
		Paper2DPlusProfileToolProvider::MakeAnimationIdentity(Profile, RunIndex);
	TestTrue(TEXT("Idle creates local directional settings"),
		Fixture.Panel->EnableDirectionalSet(IdleIdentity));
	TestTrue(TEXT("Idle enables a local offset override"),
		Fixture.Panel->CommitDirectionalOverrideForTests(
			IdleIdentity, true, 8, 0.0f, true));
	TestTrue(TEXT("Run creates local directional settings"),
		Fixture.Panel->EnableDirectionalSet(RunIdentity));
	TestTrue(TEXT("Run enables a local offset override"),
		Fixture.Panel->CommitDirectionalOverrideForTests(
			RunIdentity, true, 8, 0.0f, true));

	TSharedPtr<SWidget> TaggedWidget = FindWidgetByTag(
		Fixture.Panel,
		FName(TEXT("Paper2DPlus.Directional.LocalOffset")));
	if (!TestTrue(TEXT("The authored Local Offset spin box is available"), TaggedWidget.IsValid()))
	{
		return false;
	}
	TSharedRef<SSpinBox<float>> LocalOffset =
		StaticCastSharedRef<SSpinBox<float>>(TaggedWidget.ToSharedRef());

	Fixture.Model->SetSelectedFlipbook(IdleIndex);
	Fixture.Panel->SlatePrepass(1.0f);
	TestTrue(TEXT("The Local Offset spin box handles Idle's increment"),
		LocalOffset->OnKeyDown(FGeometry(), MakeKeyEvent(EKeys::Right)).IsEventHandled());
	Fixture.Model->SetSelectedFlipbook(RunIndex);
	Fixture.Panel->SlatePrepass(1.0f);
	TestTrue(TEXT("The Local Offset spin box handles Run's decrement"),
		LocalOffset->OnKeyDown(FGeometry(), MakeKeyEvent(EKeys::Left)).IsEventHandled());

	TestEqual(TEXT("Idle keeps the completed offset increment"),
		Profile->Flipbooks[IdleIndex]
			.DirectionalAnimationData.AngleOffsetDegrees, 1.0f);
	TestEqual(TEXT("The next offset commit belongs to newly selected Run"),
		Profile->Flipbooks[RunIndex]
			.DirectionalAnimationData.AngleOffsetDegrees, -1.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalAuthoringTypedGenerationExpiryTest,
	"Paper2DPlus.DirectionalAnimation.Editor.Authoring.TypedNumericEditExpiresAcrossModelGeneration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalAuthoringTypedGenerationExpiryTest::RunTest(
	const FString& Parameters)
{
	using namespace Paper2DPlusDirectionalAnimationEditorTest;
	UPaper2DPlusCharacterProfileAsset* Profile =
		NewObject<UPaper2DPlusCharacterProfileAsset>();
	const int32 IdleIndex = AddAnimation(
		Profile, TEXT("Idle"), MakeFlipbook(Profile, TEXT("GenerationIdleBase")));
	FPanelFixture Fixture(Profile);
	const FProfileAnimationIdentity IdleIdentity =
		Paper2DPlusProfileToolProvider::MakeAnimationIdentity(Profile, IdleIndex);
	TestTrue(TEXT("Idle creates its local settings owner"),
		Fixture.Panel->EnableDirectionalSet(IdleIdentity));
	TestTrue(TEXT("Idle enables a local override before editing"),
		Fixture.Panel->CommitDirectionalOverrideForTests(
			IdleIdentity, true, 8, 0.0f, true));
	TestTrue(TEXT("Typing captures the current Profile and model generation"),
		Fixture.Panel->BeginLocalDirectionalCountEditForTests());

	Fixture.Model->NotifyAssetDataChanged();
	TestFalse(TEXT("A model-data refresh expires the captured local numeric edit"),
		Fixture.Panel->CommitLocalDirectionalCountEditForTests(5, true));
	TestEqual(TEXT("The expired edit cannot mutate the refreshed local count"),
		Profile->Flipbooks[IdleIndex].DirectionalAnimationData.DirectionCount, 8);
	TestTrue(TEXT("The expiry diagnostic names the Profile/model ownership boundary"),
		Fixture.Panel->GetLastDirectionalIssueTextForTests().Contains(
			TEXT("Profile owner")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalAuthoringUndoBoundaryTest,
	"Paper2DPlus.DirectionalAnimation.Editor.Authoring.OneCommitIsOneUndoBoundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalAuthoringUndoBoundaryTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusDirectionalAnimationEditorTest;
	if (!TestNotNull(TEXT("GEditor is required for the authoritative transaction proof"), GEditor))
	{
		return false;
	}

	UPaper2DPlusCharacterProfileAsset* Profile =
		NewObject<UPaper2DPlusCharacterProfileAsset>();
	AddAnimation(Profile, TEXT("Idle"), MakeFlipbook(Profile, TEXT("UndoBase")));
	FPanelFixture Fixture(Profile);
	GEditor->ResetTransaction(FText::FromString(TEXT("TASK-190 directional authoring undo start")));

	TestTrue(TEXT("One defaults commit succeeds"),
		Fixture.Panel->CommitDirectionalDefaultsForTests(3, 12.0f, true));
	TestEqual(TEXT("The commit writes both topology fields together"),
		Profile->DefaultDirectionalCount, 3);
	TestEqual(TEXT("The paired offset is part of the same commit"),
		Profile->DefaultDirectionalAngleOffset, 12.0f);
	TestTrue(TEXT("One Undo reverts the complete paired mutation"),
		GEditor->UndoTransaction(true));
	TestEqual(TEXT("Undo restores the default count"),
		Profile->DefaultDirectionalCount, 8);
	TestEqual(TEXT("Undo restores the default offset"),
		Profile->DefaultDirectionalAngleOffset, 0.0f);
	TestTrue(TEXT("One Redo reapplies the complete paired mutation"),
		GEditor->RedoTransaction());
	TestEqual(TEXT("Redo restores the committed count"),
		Profile->DefaultDirectionalCount, 3);
	TestEqual(TEXT("Redo restores the committed offset"),
		Profile->DefaultDirectionalAngleOffset, 12.0f);
	GEditor->ResetTransaction(FText::FromString(TEXT("TASK-190 directional authoring undo end")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalNameSuffixMatcherTest,
	"Paper2DPlus.DirectionalAnimation.Editor.Details.NameSuffixMatcher",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalNameSuffixMatcherTest::RunTest(const FString& Parameters)
{
	const TArray<FString> Candidates =
	{
		TEXT("Walk_N"),     // 0
		TEXT("Walk_NE"),    // 1
		TEXT("walk_e"),     // 2: case-insensitive
		TEXT("Walk-S"),     // 3: dash separator
		TEXT("Walk 7"),     // 4: space separator, numeric suffix
		TEXT("Walk_Foo"),   // 5: no direction suffix
		TEXT("Run_N"),      // 6: different base
		TEXT("Walk_3"),     // 7: numeric twin of Walk_SE
		TEXT("Walk_SE")     // 8: compass twin of Walk_3
	};

	// TMap::FindRef with an explicit default is not available across the whole 5.0-5.8 range.
	const auto FindSlotMatch = [](const TMap<int32, int32>& Map, const int32 SlotIndex)
	{
		const int32* Found = Map.Find(SlotIndex);
		return Found ? *Found : INDEX_NONE;
	};

	const TMap<int32, int32> EightWay =
		SProfileDetailsPanel::MatchDirectionalSlotsByNameSuffix(
			TEXT("Walk"), Candidates, 8, 0.0f);
	TestEqual(TEXT("Compass N maps to slot 0"), FindSlotMatch(EightWay, 0), 0);
	TestEqual(TEXT("Compass NE maps to slot 1"), FindSlotMatch(EightWay, 1), 1);
	TestEqual(TEXT("Matching is case-insensitive"), FindSlotMatch(EightWay, 2), 2);
	TestEqual(TEXT("A dash separator matches"), FindSlotMatch(EightWay, 4), 3);
	TestEqual(TEXT("A space separator with a numeric suffix matches"),
		FindSlotMatch(EightWay, 7), 4);
	TestFalse(TEXT("Two distinct assets claiming one slot are skipped, never guessed"),
		EightWay.Contains(3));
	TestEqual(TEXT("Unrelated names and other bases never match"), EightWay.Num(), 5);

	const TMap<int32, int32> FiveWay =
		SProfileDetailsPanel::MatchDirectionalSlotsByNameSuffix(
			TEXT("Walk"), Candidates, 5, 0.0f);
	TestEqual(TEXT("A non-compass count matches numeric suffixes only"),
		FindSlotMatch(FiveWay, 3), 7);
	TestFalse(TEXT("Compass names do not match a non-compass count"),
		FiveWay.Contains(0));

	TestEqual(TEXT("An empty base name matches nothing"),
		SProfileDetailsPanel::MatchDirectionalSlotsByNameSuffix(
			FString(), Candidates, 8, 0.0f).Num(),
		0);
	TestEqual(TEXT("Invalid directional settings match nothing"),
		SProfileDetailsPanel::MatchDirectionalSlotsByNameSuffix(
			TEXT("Walk"), Candidates, 2, 0.0f).Num(),
		0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalAutoFillBehaviorTest,
	"Paper2DPlus.DirectionalAnimation.Editor.Details.NameSuffixAutoFill",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalAutoFillBehaviorTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusDirectionalAnimationEditorTest;

	// Same-folder registry-visible candidates: AssetCreated registers in-memory assets, which is
	// exactly what the auto-fill's PackagePaths scan reads.
	const FString Folder = TEXT("/Game/__AutomationTemp__/P2DPDirectionalAutoFill");
	auto MakeRegistryFlipbook = [&Folder](const TCHAR* AssetName) -> UPaperFlipbook*
	{
		UPackage* Package = CreatePackage(*(Folder / AssetName));
		UPaperFlipbook* Flipbook = NewObject<UPaperFlipbook>(
			Package, AssetName, RF_Public | RF_Standalone | RF_Transactional);
		FAssetRegistryModule::AssetCreated(Flipbook);
		return Flipbook;
	};
	UPaperFlipbook* Base = MakeRegistryFlipbook(TEXT("AFWalk"));
	UPaperFlipbook* North = MakeRegistryFlipbook(TEXT("AFWalk_N"));
	UPaperFlipbook* NorthEast = MakeRegistryFlipbook(TEXT("AFWalk_NE"));
	UPaperFlipbook* South = MakeRegistryFlipbook(TEXT("AFWalk_S"));

	UPaper2DPlusCharacterProfileAsset* Profile =
		NewObject<UPaper2DPlusCharacterProfileAsset>();
	AddAnimation(Profile, TEXT("AFWalk"), Base);
	FPanelFixture Fixture(Profile);
	const FProfileAnimationIdentity Identity =
		Fixture.Panel->GetSelectedDirectionalAnimationIdentity();
	TestTrue(TEXT("Directional set enables for the auto-fill fixture"),
		Fixture.Panel->EnableDirectionalSet(Identity));
	// Deliberately wrong art in the N slot: the fill must never overwrite an authored assignment.
	TestTrue(TEXT("Pre-occupying the N slot succeeds"),
		Fixture.Panel->CommitDirectionalSlot(Identity, 0, South));

	TSoftObjectPtr<UPaperFlipbook> Assigned;
	TestFalse(TEXT("A declined confirmation applies nothing"),
		Fixture.Panel->RunDirectionalNameSuffixAutoFillForTests(false));
	TestFalse(TEXT("Declined: the NE slot stays empty"),
		Profile->GetDirectionalSlot(0, 1, Assigned));

	TestTrue(TEXT("A confirmed auto-fill applies"),
		Fixture.Panel->RunDirectionalNameSuffixAutoFillForTests(true));
	TestTrue(TEXT("The NE slot was filled"),
		Profile->GetDirectionalSlot(0, 1, Assigned));
	TestEqual(TEXT("The NE slot holds the NE-suffixed candidate"),
		Assigned.ToSoftObjectPath(), FSoftObjectPath(NorthEast));
	TestTrue(TEXT("The S slot was filled"),
		Profile->GetDirectionalSlot(0, 4, Assigned));
	TestEqual(TEXT("The S slot holds the S-suffixed candidate"),
		Assigned.ToSoftObjectPath(), FSoftObjectPath(South));
	TestTrue(TEXT("The pre-occupied N slot still holds an assignment"),
		Profile->GetDirectionalSlot(0, 0, Assigned));
	TestEqual(TEXT("The occupied N slot was never overwritten"),
		Assigned.ToSoftObjectPath(), FSoftObjectPath(South));
	int32 OccupiedCount = 0;
	for (const FPaper2DPlusDirectionalAnimationSlot& Slot :
		Profile->Flipbooks[0].DirectionalAnimationData.Slots)
	{
		OccupiedCount += !Slot.Flipbook.IsNull() ? 1 : 0;
	}
	TestEqual(TEXT("Exactly the two empty matched slots were filled"), OccupiedCount, 3);

	// The whole fill is ONE transaction: a single undo reverts both filled slots and nothing else.
	GEditor->UndoTransaction();
	TestFalse(TEXT("Undo empties the NE slot"),
		Profile->GetDirectionalSlot(0, 1, Assigned));
	TestFalse(TEXT("Undo empties the S slot"),
		Profile->GetDirectionalSlot(0, 4, Assigned));
	TestTrue(TEXT("Undo leaves the pre-existing N assignment alone"),
		Profile->GetDirectionalSlot(0, 0, Assigned));

	GEditor->ResetTransaction(
		FText::FromString(TEXT("TASK-190 directional auto-fill test end")));
	for (UPaperFlipbook* FixtureAsset : { Base, North, NorthEast, South })
	{
		FAssetRegistryModule::AssetDeleted(FixtureAsset);
		FixtureAsset->ClearFlags(RF_Public | RF_Standalone);
		FixtureAsset->Rename(
			nullptr,
			GetTransientPackage(),
			REN_DontCreateRedirectors | REN_NonTransactional);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
