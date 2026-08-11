// Copyright 2026 Infinite Gameworks. All Rights Reserved.
//
// U7 - Frame Cues tab simplification. The default surface is: pick a frame, add a Cue, edit its
// properties, preview. These tests pin the four things that had to recede for that to be true:
//
//  1. named Cue tracks live behind one "Manage tracks..." overflow, never in the default chrome;
//  2. migration-era chrome (the Frame Event provenance flag and its receiver acknowledgement) renders
//     only for a placement that actually carries legacy data;
//  3. network policy and Cue State End Reasons are details-pane material, still editable, never timeline
//     chrome;
//  4. the + Add Cue picker's diagnostic ROWS carry discovery friction, so the standing status line
//     under the list is gone and only an empty state remains.
//
// Widget-level assertions walk the real production widget tree. Where a bound Slate attribute would
// only re-evaluate on invalidation, the test reads the panel's own predicate instead of the cached
// attribute, so nothing here depends on a paint pass that a headless run never performs. Menu
// assertions target ENTRY LABELS rather than section headings for the same reason: entry labels are
// the part of an FMenuBuilder widget that is always materialized.

#if WITH_EDITOR

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "CharacterProfileEditorModel.h"
#include "CurveTrackPanel.h"
#include "Editor.h"
#include "FrameCueDataProvider.h"
#include "Paper2DPlusEditorTestFrameCueTypes.h"
#include "Paper2DPlusVisualTourFixtureCue.h"
#include "FrameCues/Paper2DPlusFrameCue.h"
#include "FrameCues/Paper2DPlusFrameCueTabPresentation.h"
#include "FrameCues/SPaper2DPlusFrameCueTypePicker.h"
#include "FrameEventEditor.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Paper2DPlusNetTypes.h"
#include "PaperFlipbook.h"
#include "SFrameCueTimeline.h"
#include "Types/SlateAttributeMetaData.h"
#include "UObject/UnrealType.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/SWidget.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "Paper2DPlusFrameCueTabSimplificationTest"

// File-unique helper prefix: this module builds as a unity translation unit.
namespace Paper2DPlusFrameCueTabSimplificationTest
{
	UPaperFlipbook* TabSimplification_MakeFlipbook(const int32 FrameCount, UObject* Outer)
	{
		UPaperFlipbook* Flipbook = NewObject<UPaperFlipbook>(Outer);
		FScopedFlipbookMutator Mutator(Flipbook);
		Mutator.KeyFrames.AddDefaulted(FMath::Max(0, FrameCount));
		return Flipbook;
	}

	UPaper2DPlusCharacterProfileAsset* TabSimplification_MakeAsset(const TCHAR* AnimationName)
	{
		UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>(
			GetTransientPackage(), NAME_None, RF_Transactional);
		Asset->Flipbooks.AddDefaulted();
		Asset->Flipbooks[0].Identity.FlipbookName = AnimationName;
		Asset->Flipbooks[0].Identity.Flipbook = TabSimplification_MakeFlipbook(6, Asset);
		return Asset;
	}

	/**
	 * Every STextBlock label under a root, split by whether it is actually on screen.
	 *
	 * A `.Visibility_Lambda` binding is a TSlateAttribute and `SWidget::GetVisibility()` returns the
	 * CACHED value: binding does not evaluate the getter, and only the prepass/paint attribute-update
	 * pass re-runs it. A headless panel is constructed but never painted, so a collapsed-by-binding
	 * control would otherwise read as its construction-time default (Visible). Running the same update
	 * SWidget::SlatePrepass runs, top-down as the walk reaches each widget, makes the probe observe
	 * what a painted panel would show. Same approach and signature as the workspace probe on 5.0-5.8.
	 */
	void TabSimplification_CollectTexts(
		const TSharedRef<SWidget>& Widget,
		const bool bAncestorsVisible,
		TArray<FString>& OutVisible,
		TArray<FString>& OutAll)
	{
		FSlateAttributeMetaData::UpdateAllAttributes(
			Widget.Get(),
			FSlateAttributeMetaData::EInvalidationPermission::AllowInvalidationIfConstructed);

		const bool bVisible = bAncestorsVisible && Widget->GetVisibility().IsVisible();
		if (Widget->GetType() == FName(TEXT("STextBlock")))
		{
			const FString Text = StaticCastSharedRef<STextBlock>(Widget)->GetText().ToString();
			OutAll.Add(Text);
			if (bVisible)
			{
				OutVisible.Add(Text);
			}
		}
		if (FChildren* Children = Widget->GetChildren())
		{
			for (int32 Index = 0; Index < Children->Num(); ++Index)
			{
				TabSimplification_CollectTexts(
					Children->GetChildAt(Index), bVisible, OutVisible, OutAll);
			}
		}
	}

	struct FTabSimplificationTexts
	{
		/** Labels a designer can actually read right now. */
		TArray<FString> Visible;
		/** Every label the surface CONSTRUCTED. "Built but collapsed" differs from "moved to overflow". */
		TArray<FString> All;

		bool HasVisibleContaining(const TCHAR* Substring) const
		{
			return Visible.ContainsByPredicate(
				[Substring](const FString& Text) { return Text.Contains(Substring); });
		}
		bool HasAnyContaining(const TCHAR* Substring) const
		{
			return All.ContainsByPredicate(
				[Substring](const FString& Text) { return Text.Contains(Substring); });
		}
		bool HasVisibleExact(const TCHAR* Label) const
		{
			return Visible.Contains(FString(Label));
		}
	};

	FTabSimplificationTexts TabSimplification_ReadTexts(const TSharedRef<SWidget>& Root)
	{
		FTabSimplificationTexts Texts;
		TabSimplification_CollectTexts(Root, /*bAncestorsVisible=*/true, Texts.Visible, Texts.All);
		return Texts;
	}

	FProperty* TabSimplification_MigratedFlagProperty()
	{
		return FindFProperty<FProperty>(
			UPaper2DPlusCueBase::StaticClass(),
			TEXT("bMigratedFromFrameEvent"));
	}

	FProperty* TabSimplification_AcknowledgedFlagProperty()
	{
		return FindFProperty<FProperty>(
			UPaper2DPlusCueBase::StaticClass(),
			TEXT("bMigrationReceiverAcknowledged"));
	}

	FProperty* TabSimplification_NetPolicyProperty()
	{
		return FindFProperty<FProperty>(
			UPaper2DPlusCueBase::StaticClass(),
			GET_MEMBER_NAME_CHECKED(UPaper2DPlusCueBase, NetPolicy));
	}
}

// --- 1. Migration-era chrome only for assets that carry legacy data --------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCueTabNoLegacyDataTest,
	"Paper2DPlus.FrameCues.Editor.TabSimplification.NoLegacyDataShowsNoMigrationUi",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCueTabNoLegacyDataTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusFrameCueTabSimplificationTest;
	using namespace Paper2DPlusFrameCueTabPresentation;

	FProperty* MigratedFlag = TabSimplification_MigratedFlagProperty();
	FProperty* AcknowledgedFlag = TabSimplification_AcknowledgedFlagProperty();
	FProperty* NetPolicyProperty = TabSimplification_NetPolicyProperty();
	if (!TestNotNull(TEXT("migration provenance flag is reflected"), MigratedFlag)
		|| !TestNotNull(TEXT("receiver acknowledgement flag is reflected"), AcknowledgedFlag)
		|| !TestNotNull(TEXT("net policy is reflected"), NetPolicyProperty))
	{
		return false;
	}
	TestTrue(TEXT("both provenance fields classify as migration chrome"),
		IsMigrationProperty(MigratedFlag) && IsMigrationProperty(AcknowledgedFlag));
	TestFalse(TEXT("net policy is never mistaken for migration chrome"),
		IsMigrationProperty(NetPolicyProperty));

	UPaper2DPlusCharacterProfileAsset* Asset = TabSimplification_MakeAsset(TEXT("NoLegacyData"));
	Asset->AddToRoot();
	UPaper2DPlusEditorTestMomentCue* Cue = NewObject<UPaper2DPlusEditorTestMomentCue>(
		Asset, NAME_None, RF_Transactional);
	Asset->Flipbooks[0].FrameEventData.FrameCues.Add(Cue);

	TestFalse(TEXT("a freshly authored placement carries no Frame Event provenance"),
		CarriesLegacyFrameEventData(Cue));

	TSharedPtr<FCharacterProfileEditorModel> Model = MakeShared<FCharacterProfileEditorModel>();
	Model->InitializeFromAsset(Asset);
	Model->SetSelectedFlipbook(0);
	TSharedPtr<SFrameEventEditor> Editor = SNew(SFrameEventEditor).Model(Model);
	Editor->SelectCueForTests(0);
	if (!TestNotNull(TEXT("the placement is the live details selection"),
		Editor->GetSelectedCueForTests()))
	{
		Editor.Reset();
		Asset->RemoveFromRoot();
		return false;
	}

	// The details view is wired to exactly this predicate, so asking it is asking the real UI.
	TestFalse(TEXT("provenance flag is hidden on an asset with no legacy data"),
		Editor->IsCuePlacementPropertyVisibleForTests(MigratedFlag));
	TestFalse(TEXT("acknowledgement checkbox is hidden on an asset with no legacy data"),
		Editor->IsCuePlacementPropertyVisibleForTests(AcknowledgedFlag));
	TestTrue(TEXT("ordinary authoring properties are unaffected"),
		Editor->IsCuePlacementPropertyVisibleForTests(NetPolicyProperty));

	// A Cue Type's DEFAULTS are never a migrated placement, so the restricted toolkit hides the same
	// two fields by asking with a null placement.
	TestFalse(TEXT("Cue Type defaults never show the acknowledgement checkbox"),
		IsPlacementDetailsPropertyVisible(AcknowledgedFlag, nullptr));
	TestTrue(TEXT("Cue Type defaults still show net policy"),
		IsPlacementDetailsPropertyVisible(NetPolicyProperty, nullptr));

	Editor.Reset();
	Asset->RemoveFromRoot();
	return true;
}

// --- 2. Legacy data keeps the acknowledgement flow -------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCueTabLegacyDataTest,
	"Paper2DPlus.FrameCues.Editor.TabSimplification.LegacyDataKeepsAcknowledgementFlow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCueTabLegacyDataTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusFrameCueTabSimplificationTest;
	using namespace Paper2DPlusFrameCueTabPresentation;

	FProperty* MigratedFlag = TabSimplification_MigratedFlagProperty();
	FProperty* AcknowledgedFlag = TabSimplification_AcknowledgedFlagProperty();
	if (!TestNotNull(TEXT("migration provenance flag is reflected"), MigratedFlag)
		|| !TestNotNull(TEXT("receiver acknowledgement flag is reflected"), AcknowledgedFlag))
	{
		return false;
	}

	UPaper2DPlusCharacterProfileAsset* Asset = TabSimplification_MakeAsset(TEXT("CarriesLegacyData"));
	Asset->AddToRoot();
	UPaper2DPlusEditorTestMomentCue* Migrated = NewObject<UPaper2DPlusEditorTestMomentCue>(
		Asset, NAME_None, RF_Transactional);
	UPaper2DPlusEditorTestMomentCue* Ordinary = NewObject<UPaper2DPlusEditorTestMomentCue>(
		Asset, NAME_None, RF_Transactional);
#if WITH_EDITORONLY_DATA
	Migrated->bMigratedFromFrameEvent = true;
#endif
	Asset->Flipbooks[0].FrameEventData.FrameCues.Add(Migrated);
	Asset->Flipbooks[0].FrameEventData.FrameCues.Add(Ordinary);

	// The gate is per placement, not per asset: one imported placement must not put migration
	// bookkeeping on the details pane of every clean sibling sitting next to it in the same animation.
	TestTrue(TEXT("the imported placement is recognised as carrying legacy data"),
		CarriesLegacyFrameEventData(Migrated));
	TestFalse(TEXT("its normally authored sibling in the same animation carries none"),
		CarriesLegacyFrameEventData(Ordinary));

	TSharedPtr<FCharacterProfileEditorModel> Model = MakeShared<FCharacterProfileEditorModel>();
	Model->InitializeFromAsset(Asset);
	Model->SetSelectedFlipbook(0);
	TSharedPtr<SFrameEventEditor> Editor = SNew(SFrameEventEditor).Model(Model);

	Editor->SelectCueForTests(0);
	TestTrue(TEXT("the migrated placement still declares its provenance"),
		Editor->IsCuePlacementPropertyVisibleForTests(MigratedFlag));
	TestTrue(TEXT("the migrated placement still offers the acknowledgement checkbox"),
		Editor->IsCuePlacementPropertyVisibleForTests(AcknowledgedFlag));
	// Editability, not just visibility: the acknowledgement is an action the designer has to take.
	TestTrue(TEXT("the acknowledgement stays an editable checkbox"),
		AcknowledgedFlag->HasAnyPropertyFlags(CPF_Edit)
			&& !AcknowledgedFlag->HasAnyPropertyFlags(CPF_EditConst));
#if WITH_EDITORONLY_DATA
	Migrated->bMigrationReceiverAcknowledged = true;
	TestTrue(TEXT("acknowledging is a plain property write on the placement"),
		Migrated->bMigrationReceiverAcknowledged);
#endif

	// A sibling placement in the SAME asset that was authored normally keeps a clean details pane.
	// Guarded like test 1: an unresolved selection also hides the chrome, and would pass for the wrong
	// reason without proving anything about the sibling.
	Editor->SelectCueForTests(1);
	if (!TestNotNull(TEXT("the sibling placement is the live details selection"),
		Editor->GetSelectedCueForTests()))
	{
		Editor.Reset();
		Asset->RemoveFromRoot();
		return false;
	}
	TestFalse(TEXT("a non-migrated sibling hides the migration chrome"),
		Editor->IsCuePlacementPropertyVisibleForTests(AcknowledgedFlag));

	Editor.Reset();
	Asset->RemoveFromRoot();
	return true;
}

// --- 3. Track management is reachable but out of the default chrome --------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCueTabTrackOverflowTest,
	"Paper2DPlus.FrameCues.Editor.TabSimplification.TrackManagementLeavesTheDefaultChrome",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCueTabTrackOverflowTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusFrameCueTabSimplificationTest;
	if (!TestNotNull(TEXT("GEditor available"), GEditor))
	{
		return false;
	}
	GEditor->ResetTransaction(LOCTEXT("ResetTrackOverflow", "Frame Cue Track Overflow Test Reset"));

	UPaper2DPlusCharacterProfileAsset* Asset = TabSimplification_MakeAsset(TEXT("TrackOverflow"));
	Asset->AddToRoot();
	TSharedPtr<FCharacterProfileEditorModel> Model = MakeShared<FCharacterProfileEditorModel>();
	Model->InitializeFromAsset(Asset);
	Model->SetSelectedFlipbook(0);
	TSharedPtr<SFrameEventEditor> Editor = SNew(SFrameEventEditor).Model(Model);
	TSharedPtr<SFrameCueTimeline> Timeline = Editor->GetUnifiedTimelineForTests();
	TSharedPtr<FFrameCueDataProvider> Provider = Editor->GetDataProviderForTests();
	if (!TestTrue(TEXT("the tab mounts the production timeline"), Timeline.IsValid())
		|| !TestTrue(TEXT("the tab resolves its storage provider"), Provider.IsValid()))
	{
		Editor.Reset();
		Asset->RemoveFromRoot();
		return false;
	}

	TestEqual(TEXT("a fresh animation authors no named tracks"),
		Timeline->GetNamedTrackCountForTests(), 0);
	TestEqual(TEXT("the implicit Default lane is mounted regardless"),
		Timeline->GetVisibleTrackCountForTests(), 1);

	FTabSimplificationTexts Chrome = TabSimplification_ReadTexts(Timeline.ToSharedRef());
	// Absent from the default chrome - and not merely hidden: the button is not built at all.
	TestFalse(TEXT("the persistent + Add Track button is gone from the timeline chrome"),
		Chrome.HasAnyContaining(TEXT("Add Track")));
	TestFalse(TEXT("no Add-target line while Default is the only lane"),
		Chrome.HasVisibleContaining(TEXT("Add target:")));
	TestTrue(TEXT("the primary action is the undecorated + Add Cue with no named tracks"),
		Chrome.HasVisibleExact(TEXT("+ Add Cue")));

	// Reachable - one labeled overflow owns every track command.
	TestTrue(TEXT("the Manage tracks overflow is mounted"),
		Timeline->HasManageTracksOverflowForTests());
	TestTrue(TEXT("the overflow is labeled in the chrome"),
		Chrome.HasVisibleContaining(TEXT("Manage tracks")));
	const FTabSimplificationTexts EmptyOverflow =
		TabSimplification_ReadTexts(Timeline->BuildManageTracksMenuForTests());
	TestTrue(TEXT("the overflow offers track creation"),
		EmptyOverflow.HasAnyContaining(TEXT("New track")));
	TestTrue(TEXT("the empty overflow explains the implicit Default lane"),
		EmptyOverflow.HasAnyContaining(TEXT("Default track")));

	// The overflow's action is the real one: it mutates the editor-only sidecar, nothing else.
	Timeline->AddTrackFromOverflowForTests();
	TestEqual(TEXT("the overflow created exactly one named track"),
		Timeline->GetNamedTrackCountForTests(), 1);
	TestEqual(TEXT("Default plus the new named lane are both presented"),
		Timeline->GetVisibleTrackCountForTests(), 2);
	TestTrue(TEXT("the new track becomes the Add Cue target"),
		Timeline->GetActiveTrackId().IsValid());
	TestTrue(TEXT("track creation never invents Cue placements"),
		Asset->Flipbooks[0].FrameEventData.FrameCues.IsEmpty());

	// With a named track the target finally means something, so the header earns its line back.
	Chrome = TabSimplification_ReadTexts(Timeline.ToSharedRef());
	TestTrue(TEXT("the Add-target line returns once a named track exists"),
		Chrome.HasVisibleContaining(TEXT("Add target:")));
	TestFalse(TEXT("track commands never leak back into the persistent chrome"),
		Chrome.HasAnyContaining(TEXT("Add Track")));

	const FProfileScopedAnimationIdentity Scope = Provider->GetScopedAnimationIdentity(0);
	const TArray<FPaper2DPlusFrameCueTrackDefinition> Tracks = Provider->GetOptionalTracks(Scope);
	const FTabSimplificationTexts PopulatedOverflow =
		TabSimplification_ReadTexts(Timeline->BuildManageTracksMenuForTests());
	if (TestEqual(TEXT("the sidecar holds exactly the one created track"), Tracks.Num(), 1))
	{
		TestTrue(TEXT("the overflow lists the named track for editing"),
			PopulatedOverflow.HasAnyContaining(*Tracks[0].DisplayName));
	}
	TestTrue(TEXT("the overflow offers Default back as the Add Cue target"),
		PopulatedOverflow.HasAnyContaining(TEXT("Default")));

	Editor.Reset();
	GEditor->ResetTransaction(LOCTEXT("EndTrackOverflow", "Frame Cue Track Overflow Test End"));
	Asset->RemoveFromRoot();
	return true;
}

// --- 4. Net policy and End Reasons live in the details pane, still editable ------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCueTabDetailsPaneTest,
	"Paper2DPlus.FrameCues.Editor.TabSimplification.NetPolicyAndEndReasonLiveInTheDetailsPane",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCueTabDetailsPaneTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusFrameCueTabSimplificationTest;
	using namespace Paper2DPlusFrameCueTabPresentation;

	FProperty* NetPolicyProperty = TabSimplification_NetPolicyProperty();
	if (!TestNotNull(TEXT("net policy is reflected"), NetPolicyProperty))
	{
		return false;
	}
	TestTrue(TEXT("net policy remains an editable placement property"),
		NetPolicyProperty->HasAnyPropertyFlags(CPF_Edit)
			&& !NetPolicyProperty->HasAnyPropertyFlags(CPF_EditConst));

	TestTrue(TEXT("Cosmetic Only explains the dedicated-server exclusion"),
		DescribeNetPolicy(EPaper2DPlusFrameCueNetPolicy::CosmeticOnly).ToString()
			.Contains(TEXT("dedicated server")));
	TestTrue(TEXT("Authority Only explains where it runs"),
		DescribeNetPolicy(EPaper2DPlusFrameCueNetPolicy::AuthorityOnly).ToString()
			.Contains(TEXT("authority")));

	UPaper2DPlusCharacterProfileAsset* Asset = TabSimplification_MakeAsset(TEXT("DetailsPane"));
	Asset->AddToRoot();
	UPaper2DPlusEditorTestMomentCue* Moment = NewObject<UPaper2DPlusEditorTestMomentCue>(
		Asset, NAME_None, RF_Transactional);
	Moment->NetPolicy = EPaper2DPlusFrameCueNetPolicy::CosmeticOnly;
	UPaper2DPlusEditorTestRangeCue* Range = NewObject<UPaper2DPlusEditorTestRangeCue>(
		Asset, NAME_None, RF_Transactional);
	Range->StartFrame = 1;
	Range->FrameCount = 3;
	Range->NetPolicy = EPaper2DPlusFrameCueNetPolicy::AuthorityOnly;
	Asset->Flipbooks[0].FrameEventData.FrameCues.Add(Moment);
	Asset->Flipbooks[0].FrameEventData.FrameCues.Add(Range);

	const FString StateDetails = BuildPlacementDetailsSummary(Range).ToString();
	const FString CueDetails = BuildPlacementDetailsSummary(Moment).ToString();
	TestTrue(TEXT("a Cue State placement uses the exact designer-facing type name"),
		StateDetails.StartsWith(TEXT("Cue State placement.")));
	TestTrue(TEXT("a Cue State placement's summary names the End Reason channel"),
		StateDetails.Contains(TEXT("End Reason")));
	TestTrue(TEXT("a Cue placement uses the exact designer-facing type name"),
		CueDetails.StartsWith(TEXT("Cue placement.")));
	TestFalse(TEXT("a Cue placement's summary does not invent an End Reason"),
		CueDetails.Contains(TEXT("End Reason")));
	TestFalse(TEXT("placement details never expose the retired type labels"),
		StateDetails.Contains(TEXT("Range Cue"))
		|| StateDetails.Contains(TEXT("Moment Cue"))
		|| CueDetails.Contains(TEXT("Range Cue"))
		|| CueDetails.Contains(TEXT("Moment Cue")));
	TestTrue(TEXT("a placement's summary carries its own net policy"),
		CueDetails.Contains(TEXT("dedicated server")));

	TSharedPtr<FCharacterProfileEditorModel> Model = MakeShared<FCharacterProfileEditorModel>();
	Model->InitializeFromAsset(Asset);
	Model->SetSelectedFlipbook(0);
	TSharedPtr<SFrameEventEditor> Editor = SNew(SFrameEventEditor).Model(Model);

	Editor->SelectCueForTests(1);
	const FString RangeSummary = Editor->GetDetailsSummaryForTests().ToString();
	TestTrue(TEXT("the details pane header names the selected Cue State"),
		RangeSummary.StartsWith(TEXT("Cue State placement.")));
	TestTrue(TEXT("the details pane header states the selected Cue State's End Reason contract"),
		RangeSummary.Contains(TEXT("End Reason")));
	TestTrue(TEXT("the details pane header states the selected Cue State's net policy"),
		RangeSummary.Contains(TEXT("authority")));
	TestTrue(TEXT("net policy stays visible, and therefore editable, in the details pane"),
		Editor->IsCuePlacementPropertyVisibleForTests(NetPolicyProperty));

	Editor->SelectCueForTests(0);
	const FString MomentSummary = Editor->GetDetailsSummaryForTests().ToString();
	// Positive control first: an empty or unresolved summary would satisfy the absence check below
	// without the selection ever having become a Cue.
	TestTrue(TEXT("the details pane header is now the Cue's own summary"),
		MomentSummary.StartsWith(TEXT("Cue placement.")));
	TestFalse(TEXT("switching to a Cue drops the End Reason sentence"),
		MomentSummary.Contains(TEXT("End Reason")));

	// ...and none of it is timeline chrome.
	TSharedPtr<SFrameCueTimeline> Timeline = Editor->GetUnifiedTimelineForTests();
	if (TestTrue(TEXT("the tab mounts the production timeline"), Timeline.IsValid()))
	{
		const FTabSimplificationTexts Chrome = TabSimplification_ReadTexts(Timeline.ToSharedRef());
		// Positive control on the SAME probe object: the loop below is all absence assertions, and a
		// widget walk that returned an empty tree would satisfy every one of them for free.
		TestTrue(TEXT("the probe actually read the timeline's default chrome"),
			Chrome.HasVisibleExact(TEXT("+ Add Cue")));
		for (const TCHAR* SecondaryConcept : {
			TEXT("End Reason"),
			TEXT("Net Policy"),
			TEXT("Cosmetic Only"),
			TEXT("Authority Only") })
		{
			TestFalse(
				FString::Printf(
					TEXT("'%s' is details-pane material, not timeline chrome"), SecondaryConcept),
				Chrome.HasAnyContaining(SecondaryConcept));
		}
	}

	Editor.Reset();
	Asset->RemoveFromRoot();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCueSelectionDrivenDetailsTest,
	"Paper2DPlus.FrameCues.Editor.TabSimplification.DirectSelectionDrivesOneDetailsPanel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCueSelectionDrivenDetailsTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusFrameCueTabSimplificationTest;
	UPaper2DPlusCharacterProfileAsset* Asset =
		TabSimplification_MakeAsset(TEXT("SelectionDrivenDetails"));
	Asset->AddToRoot();
	FPaper2DPlusFrameCurve& Curve =
		Asset->Flipbooks[0].CurveData.Curves.Add(TEXT("Damage"));
	Curve.SetKeyValue(2, 3.0f);
	Asset->Flipbooks.AddDefaulted();
	Asset->Flipbooks[1].Identity.FlipbookName = TEXT("SelectionDrivenDetailsB");
	Asset->Flipbooks[1].Identity.Flipbook = TabSimplification_MakeFlipbook(6, Asset);
	Asset->Flipbooks[1].CurveData.Curves.Add(TEXT("Damage")).SetKeyValue(1, 2.0f);
	UPaper2DPlusEditorTestMomentCue* Cue = NewObject<UPaper2DPlusEditorTestMomentCue>(
		Asset, NAME_None, RF_Transactional);
	Asset->Flipbooks[0].FrameEventData.FrameCues.Add(Cue);

	TSharedPtr<FCharacterProfileEditorModel> Model = MakeShared<FCharacterProfileEditorModel>();
	Model->InitializeFromAsset(Asset);
	Model->SetSelectedFlipbook(0);
	TSharedPtr<SFrameEventEditor> Editor = SNew(SFrameEventEditor)
		.Model(Model)
		.HostContract(FProfileToolPanelHostContract::External());
	Editor->HandleHostActivated();
	Editor->SelectCueForTests(0);

	TArray<FProfileToolPanelDescriptor> Descriptors;
	Editor->GetContextualPanels(Descriptors);
	TArray<FName> PanelIds;
	for (const FProfileToolPanelDescriptor& Descriptor : Descriptors)
	{
		PanelIds.Add(Descriptor.PanelId);
	}
	TestEqual(
		TEXT("the indexed Cues category is retired; only Details and Preview remain"),
		PanelIds,
		TArray<FName>({
			SFrameEventEditor::DetailsPanelId,
			SFrameEventEditor::PreviewPanelId }));

	const FProfileToolPanelDescriptor* DetailsDescriptor =
		Descriptors.FindByPredicate([](const FProfileToolPanelDescriptor& Descriptor)
		{
			return Descriptor.PanelId == SFrameEventEditor::DetailsPanelId;
		});
	const TSharedPtr<SWidget> DetailsWidget =
		DetailsDescriptor ? DetailsDescriptor->TryCreateWidget() : nullptr;
	if (!TestTrue(TEXT("selection-driven Details builds"), DetailsWidget.IsValid()))
	{
		Editor->HandleHostDeactivated();
		Editor.Reset();
		Asset->RemoveFromRoot();
		return false;
	}

	FTabSimplificationTexts CueTexts =
		TabSimplification_ReadTexts(DetailsWidget.ToSharedRef());
	TestTrue(TEXT("a timeline Cue selection exposes Duplicate in Details"),
		CueTexts.HasVisibleExact(TEXT("Duplicate")));
	TestTrue(TEXT("a timeline Cue selection exposes Remove in Details"),
		CueTexts.HasVisibleExact(TEXT("Remove")));
	TestFalse(TEXT("curve controls stay hidden for a Cue selection"),
		CueTexts.HasVisibleExact(TEXT("Interpolation")));

	TSharedPtr<SCurveTrackStack> CurveStack = Editor->GetCurveStackForTests();
	if (TestTrue(TEXT("the production curve stack is mounted"), CurveStack.IsValid()))
	{
		CurveStack->RebuildRowsImmediatelyForTests();
		CurveStack->SelectCurve(TEXT("Damage"));
	}
	TestEqual(TEXT("direct legend selection becomes the Details identity"),
		Editor->GetSelectedCurveForTests(), FName(TEXT("Damage")));
	TestNull(TEXT("curve selection clears the old Cue payload selection"),
		Editor->GetSelectedCueForTests());
	FTabSimplificationTexts CurveTexts =
		TabSimplification_ReadTexts(DetailsWidget.ToSharedRef());
	TestTrue(TEXT("curve Details exposes graph visibility"),
		CurveTexts.HasVisibleExact(TEXT("Visible")));
	TestTrue(TEXT("curve Details exposes solo"),
		CurveTexts.HasVisibleExact(TEXT("Solo: Off")));
	TestTrue(TEXT("curve Details exposes interpolation"),
		CurveTexts.HasVisibleExact(TEXT("Interpolation")));
	TestTrue(TEXT("curve Details exposes rename"),
		CurveTexts.HasVisibleExact(TEXT("Rename")));
	TestTrue(TEXT("curve Details exposes removal"),
		CurveTexts.HasVisibleExact(TEXT("Remove curve")));
	TestFalse(TEXT("Cue actions stay hidden for a curve selection"),
		CurveTexts.HasVisibleExact(TEXT("Duplicate")));
	TestEqual(TEXT("curve Details reads the selected curve's mode"),
		Editor->GetSelectedCurveModeTextForTests().ToString(), FString(TEXT("Linear")));
	TestTrue(TEXT("curve Details reports clean key range state"),
		Editor->GetSelectedCurveOrphanTextForTests().ToString().Contains(TEXT("No orphan keys")));

	Editor->SelectCueForTests(0);
	TestTrue(TEXT("Cue selection clears the old curve legend highlight"),
		CurveStack.IsValid() && CurveStack->GetSelectedCurve().IsNone());
	TestNotNull(TEXT("Cue selection remains the one primary payload subject"),
		Editor->GetSelectedCueForTests());
	if (CurveStack.IsValid())
	{
		CurveStack->SelectCurve(TEXT("Damage"));
	}
	TestEqual(TEXT("the same curve can reclaim primary selection after a Cue"),
		Editor->GetSelectedCurveForTests(), FName(TEXT("Damage")));
	TestNull(TEXT("reclaimed curve selection clears the Cue payload again"),
		Editor->GetSelectedCueForTests());

	Model->SetSelectedFlipbook(1);
	Model->SetSelectedFlipbook(0);
	TestTrue(TEXT("animation round-trip does not revive a legend-only curve selection"),
		CurveStack.IsValid() && CurveStack->GetSelectedCurve().IsNone());
	if (CurveStack.IsValid())
	{
		CurveStack->SelectCurve(TEXT("Damage"));
	}
	TestEqual(TEXT("the returned curve can be selected immediately after the round-trip"),
		Editor->GetSelectedCurveForTests(), FName(TEXT("Damage")));

	const FProfileScopedAnimationIdentity SourceRenameScope =
		Editor->GetCurrentCurveRenameScope();
	Model->SetSelectedFlipbook(1);
	Editor->CurveRenameSubject = TEXT("Damage");
	Editor->CurveRenameScope = SourceRenameScope;
	Editor->SelectedCurveRenameEditor->SetText(FText::FromString(TEXT("WrongDestination")));
	Editor->CommitSelectedCurveRename(
		FText::FromString(TEXT("WrongDestination")),
		ETextCommit::OnUserMovedFocus);
	TestTrue(TEXT("a stale rename cannot mutate the same-named curve in the destination animation"),
		Asset->Flipbooks[1].CurveData.Curves.Contains(TEXT("Damage"))
		&& !Asset->Flipbooks[1].CurveData.Curves.Contains(TEXT("WrongDestination")));
	TestTrue(TEXT("rejecting the stale rename leaves its source animation unchanged"),
		Asset->Flipbooks[0].CurveData.Curves.Contains(TEXT("Damage"))
		&& !Asset->Flipbooks[0].CurveData.Curves.Contains(TEXT("WrongDestination")));
	Model->SetSelectedFlipbook(0);

	if (const TSharedPtr<SFrameCueTimeline> Timeline = Editor->GetUnifiedTimelineForTests())
	{
		Timeline->SelectTrackForTests(FGuid());
		TestTrue(TEXT("track selection clears the curve legend highlight"),
			CurveStack.IsValid() && CurveStack->GetSelectedCurve().IsNone());
		TestTrue(TEXT("Default track selection drives track guidance"),
			Editor->GetDetailsSummaryForTests().ToString().StartsWith(TEXT("Default Cue track.")));
		if (CurveStack.IsValid())
		{
			CurveStack->SelectCurve(TEXT("Damage"));
		}
		Timeline->ClearPrimarySelection();
		TestTrue(TEXT("empty primary selection clears the curve legend highlight"),
			CurveStack.IsValid() && CurveStack->GetSelectedCurve().IsNone());
	}
	FTabSimplificationTexts EmptyTexts =
		TabSimplification_ReadTexts(DetailsWidget.ToSharedRef());
	TestFalse(TEXT("empty selection exposes no Cue action"),
		EmptyTexts.HasVisibleExact(TEXT("Duplicate")));
	TestFalse(TEXT("empty selection exposes no curve action"),
		EmptyTexts.HasVisibleExact(TEXT("Rename")));

	Editor->HandleHostDeactivated();
	Editor.Reset();

	TSharedPtr<SFrameEventEditor> EmbeddedEditor = SNew(SFrameEventEditor)
		.Model(Model)
		.HostContract(FProfileToolPanelHostContract::Embedded());
	EmbeddedEditor->SelectCueForTests(0);
	FTabSimplificationTexts EmbeddedCueTexts =
		TabSimplification_ReadTexts(EmbeddedEditor.ToSharedRef());
	TestTrue(TEXT("compatibility mounts reuse the same Cue Details actions"),
		EmbeddedCueTexts.HasVisibleExact(TEXT("Duplicate"))
		&& EmbeddedCueTexts.HasVisibleExact(TEXT("Remove")));
	TestFalse(TEXT("compatibility mounts no longer render indexed Cue rows"),
		EmbeddedCueTexts.HasAnyContaining(TEXT("[0]")));
	if (const TSharedPtr<SCurveTrackStack> EmbeddedCurves =
		EmbeddedEditor->GetCurveStackForTests())
	{
		EmbeddedCurves->RebuildRowsImmediatelyForTests();
		EmbeddedCurves->SelectCurve(TEXT("Damage"));
	}
	FTabSimplificationTexts EmbeddedCurveTexts =
		TabSimplification_ReadTexts(EmbeddedEditor.ToSharedRef());
	TestTrue(TEXT("compatibility mounts retain every moved curve action"),
		EmbeddedCurveTexts.HasVisibleExact(TEXT("Interpolation"))
		&& EmbeddedCurveTexts.HasVisibleExact(TEXT("Rename"))
		&& EmbeddedCurveTexts.HasVisibleExact(TEXT("Remove curve")));
	EmbeddedEditor->HandleHostDeactivated();
	EmbeddedEditor.Reset();
	Asset->RemoveFromRoot();
	return true;
}

// --- 5. Picker diagnostic rows carry the friction; no standing status line -------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCueTabPickerStatusTest,
	"Paper2DPlus.FrameCues.Editor.TabSimplification.PickerRowsCarryFrictionWithoutAStatusLine",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCueTabPickerStatusTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusFrameCueTabSimplificationTest;

	// The plugin no longer ships any runtime or designer-placeable native Cue Type. Internal native
	// editor/test fixtures can never be placement-ready (their script package is PKG_EditorOnly, and
	// the visual-tour fixture is also HideDropdown), so discovery would find no placeable row unless
	// this test creates one. It is a fixture, not an ambient assumption: the old version of this test
	// observed a placeable row and then wrapped its final assertion in a branch on that observation,
	// which would have gone quietly green the moment the built-ins were deleted.
	FPaper2DPlusEditorTestPlaceableCueType PlaceableCueType(
		EPaper2DPlusFrameCueTypeKind::Moment, TEXT("TabPickerPlaceable"));
	FPaper2DPlusEditorTestPlaceableCueType PlaceableCueStateType(
		EPaper2DPlusFrameCueTypeKind::Range, TEXT("TabPickerPlaceableState"));
	if (!PlaceableCueType.IsReady() || !PlaceableCueStateType.IsReady())
	{
		AddError(!PlaceableCueType.IsReady()
			? PlaceableCueType.GetError()
			: PlaceableCueStateType.GetError());
		return false;
	}

	TSharedPtr<SPaper2DPlusFrameCueTypePicker> Picker = SNew(SPaper2DPlusFrameCueTypePicker);
	const int32 RowCount = Picker->GetFilteredItemsForTests().Num();
	bool bHasPlaceableRow = false;
	bool bVisualTourFixtureSurfaced = false;
	for (const TSharedPtr<FPaper2DPlusFrameCueTypePickerItem>& Item :
		Picker->GetFilteredItemsForTests())
	{
		if (!Item.IsValid())
		{
			continue;
		}
		bHasPlaceableRow |= Item->bPlaceable;
		bVisualTourFixtureSurfaced |=
			Item->Type.Class.Get() == UPaper2DPlusVisualTourFixtureCue::StaticClass()
			|| Item->Type.Class.Get()
				== UPaper2DPlusVisualTourFixtureCueState::StaticClass();
		if (!Item->bPlaceable)
		{
			// Discovery friction stays visible: every repair row names why it cannot be placed.
			TestFalse(TEXT("a repair row carries its own reason"), Item->Reason.IsEmpty());
		}
	}
	// Asserted, never assumed: the saved fixture Cue Type above always discovers, so a zero-row
	// picker means the probe broke, and the empty-state check below would silently stop running if
	// it stayed a bare branch condition.
	TestTrue(TEXT("discovery produced at least one Cue Type row to carry the friction"), RowCount > 0);
	TestTrue(TEXT("discovery produced a placeable row, not only repair rows"), bHasPlaceableRow);
	TestFalse(TEXT("the HideDropdown visual-tour fixture is absent from every Add Cue picker row"),
		bVisualTourFixtureSurfaced);
	const FVector2D PickerProbeViewport(
		640.0f,
		FMath::Max(480.0f, static_cast<float>(RowCount) * 96.0f));
	TestTrue(TEXT("the UI probe materializes the virtualized picker rows"),
		Picker->GenerateRowsForViewportForTests(PickerProbeViewport) > 0);

	const FTabSimplificationTexts PickerTexts = TabSimplification_ReadTexts(Picker.ToSharedRef());
	// The retired standing status line restated exactly what the rows already say.
	TestFalse(TEXT("no standing status line under a populated result list"),
		PickerTexts.HasVisibleContaining(TEXT("reusable Cue Types")));
	TestTrue(TEXT("the Refresh action survives the status-line removal"),
		PickerTexts.HasVisibleContaining(TEXT("Refresh")));
	TestTrue(TEXT("instant rows use the exact Cue timing-form badge"),
		PickerTexts.HasVisibleExact(TEXT("CUE")));
	TestTrue(TEXT("state rows use the exact Cue State timing-form badge"),
		PickerTexts.HasVisibleExact(TEXT("CUE STATE")));
	TestFalse(TEXT("the picker exposes no standalone STATE timing-form badge"),
		PickerTexts.HasVisibleExact(TEXT("STATE")));

	// A search that matches nothing is the one case with no row to carry the message.
	Picker->SetSearchTextForTests(FText::FromString(
		FString(TEXT("p2dp-no-such-cue-type-")) + FGuid::NewGuid().ToString(EGuidFormats::Digits)));
	TestEqual(TEXT("an unmatched search leaves no rows"),
		Picker->GetFilteredItemsForTests().Num(), 0);
	TestTrue(TEXT("the empty state appears when the list has nothing to show"),
		Picker->IsEmptyStateVisibleForTests());
	TestTrue(TEXT("the empty state names the reason the list is empty"),
		Picker->GetStatusTextForTests().ToString().Contains(TEXT("search")));

	Picker.Reset();
	return true;
}

#undef LOCTEXT_NAMESPACE

#endif // WITH_EDITOR
