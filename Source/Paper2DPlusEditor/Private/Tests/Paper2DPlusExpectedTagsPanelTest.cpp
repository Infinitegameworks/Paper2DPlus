// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CharacterCoverage/SExpectedTagsPanel.h"

#include "CharacterProfileEditorModel.h"
#include "Containers/Ticker.h"
#include "Editor.h"
#include "Editor/Transactor.h"
#include "Misc/AutomationTest.h"
#include "Paper2DPlusAnimationTagQuery.h"
#include "Paper2DPlusAnimationTags.h"
#include "Paper2DPlusCharacterCatalogAsset.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Paper2DPlusSettings.h"
#include "PaperFlipbook.h"
#include "UObject/Package.h"

namespace
{
	struct FExpectedTagsPanelTestFixture
	{
		UPackage* Package = nullptr;
		UPaper2DPlusCharacterProfileAsset* Profile = nullptr;
		UPaper2DPlusCharacterCatalogAsset* CatalogA = nullptr;
		UPaper2DPlusCharacterCatalogAsset* CatalogB = nullptr;
		TSharedPtr<FCharacterProfileEditorModel> Model;
		/** Counts the workspace-wide rebuild signal, so a test can prove a Catalog edit does not
		 *  reach panels that have nothing to do with the Catalog. */
		int32 ExternalModifiedBroadcastCount = 0;
		FDelegateHandle ExternalModifiedCounterHandle;

		explicit FExpectedTagsPanelTestFixture(const TCHAR* Suffix)
		{
			const FString PackageName = FString::Printf(
				TEXT("/Engine/Transient/Paper2DPlusExpectedTagsPanel_%s_%s"),
				Suffix,
				*FGuid::NewGuid().ToString(EGuidFormats::Digits));
			Package = CreatePackage(*PackageName);
			Profile = NewObject<UPaper2DPlusCharacterProfileAsset>(
				Package,
				TEXT("Profile"),
				RF_Public | RF_Standalone | RF_Transactional);
			CatalogA = NewObject<UPaper2DPlusCharacterCatalogAsset>(
				Package,
				TEXT("CatalogA"),
				RF_Public | RF_Standalone | RF_Transactional);
			CatalogB = NewObject<UPaper2DPlusCharacterCatalogAsset>(
				Package,
				TEXT("CatalogB"),
				RF_Public | RF_Standalone | RF_Transactional);
			Model = MakeShared<FCharacterProfileEditorModel>();
			Model->InitializeFromAsset(Profile);
			ExternalModifiedCounterHandle =
				Model->OnAssetExternallyModified.AddLambda([this]()
				{
					++ExternalModifiedBroadcastCount;
				});
		}

		~FExpectedTagsPanelTestFixture()
		{
			if (Model.IsValid() && ExternalModifiedCounterHandle.IsValid())
			{
				Model->OnAssetExternallyModified.Remove(ExternalModifiedCounterHandle);
			}
		}

		int32 AddAnimation(
			UPaper2DPlusCharacterProfileAsset* TargetProfile,
			const TCHAR* AuthoredName)
		{
			const FString ObjectName = FString::Printf(
				TEXT("Flipbook_%s_%d"),
				AuthoredName,
				TargetProfile->Flipbooks.Num());
			UPaperFlipbook* Flipbook = NewObject<UPaperFlipbook>(
				TargetProfile,
				*ObjectName,
				RF_Transactional);
			FFlipbookProfileEntry Entry;
			Entry.Identity.FlipbookName = AuthoredName;
			Entry.Identity.Flipbook =
				TSoftObjectPtr<UPaperFlipbook>(Flipbook);
			return TargetProfile->Flipbooks.Add(MoveTemp(Entry));
		}

		void AddProfileToCatalog(
			UPaper2DPlusCharacterCatalogAsset* Catalog,
			UPaper2DPlusCharacterProfileAsset* TargetProfile = nullptr)
		{
			FPaper2DPlusCharacterCatalogEntry Entry;
			Entry.CharacterProfile =
				TargetProfile ? TargetProfile : Profile;
			Catalog->Entries.Add(MoveTemp(Entry));
		}

		TSharedRef<SExpectedTagsPanel> MakePanel(
			const FExpectedTagsCatalogProvider& Provider) const
		{
			return SNew(SExpectedTagsPanel)
				.Model(Model)
				.CatalogProvider(Provider);
		}
	};

	const FCharacterCoverageRow* ExpectedTagsPanelTest_FindRow(
		const TArray<FCharacterCoverageRow>& Rows,
		const FGameplayTag& ExpectedTag)
	{
		return Rows.FindByPredicate(
			[ExpectedTag](const FCharacterCoverageRow& Row)
			{
				return Row.ExpectedTag == ExpectedTag;
			});
	}

	void ExpectedTagsPanelTest_PumpDeferredModify()
	{
		FTSTicker::GetCoreTicker().Tick(0.01f);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusExpectedTagsPanelZeroCarrierTest,
	"Paper2DPlus.CharacterCoverage.Panel.ZeroCarrierAndSingleBulkResolve",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusExpectedTagsPanelZeroCarrierTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	const FGameplayTag HeavyTag =
		Paper2DPlusAnimationTags::Combat_Heavy.GetTag();
	const FGameplayTag BlockTag =
		Paper2DPlusAnimationTags::Combat_Block.GetTag();
	if (!TestTrue(
		TEXT("The native expected tags are registered"),
		HeavyTag.IsValid() && BlockTag.IsValid()))
	{
		return false;
	}

	FExpectedTagsPanelTestFixture Fixture(TEXT("ZeroCarrier"));
	Fixture.AddAnimation(Fixture.Profile, TEXT("Idle"));
	Fixture.AddProfileToCatalog(Fixture.CatalogA);
	Fixture.CatalogA->ExpectedAnimationTags.AddTag(HeavyTag);
	const FExpectedTagsCatalogProvider Provider = [&Fixture]()
	{
		return FExpectedTagsCatalogResolution::Loaded(Fixture.CatalogA);
	};

	const TSharedRef<SExpectedTagsPanel> Panel = Fixture.MakePanel(Provider);
	TestTrue(
		TEXT("A loaded member with expectations enters the ready state"),
		Panel->GetPanelStateForTests() == EExpectedTagsPanelState::Ready);
	TestEqual(
		TEXT("The Catalog accessor is called exactly once"),
		Panel->GetCatalogAccessorCallsInLastRefreshForTests(),
		1);
	TestEqual(
		TEXT("The shared bulk resolver is called exactly once"),
		Panel->GetResolverCallsInLastRefreshForTests(),
		1);
	TestEqual(
		TEXT("Every expectation produces a row even with zero carriers"),
		Panel->GetCoverageRowsForTests().Num(),
		1);
	if (Panel->GetCoverageRowsForTests().Num() == 1)
	{
		TestTrue(
			TEXT("A zero-carrier expectation is Missing"),
			Panel->GetCoverageRowsForTests()[0].Status
				== ECharacterCoverageStatus::Missing);
	}
	Panel->ArmExpectedTagDragForTests(HeavyTag);
	Panel->Refresh();
	TestTrue(
		TEXT("Panel-owned drag arming survives a row rebuild while its tag remains current"),
		Panel->IsExpectedTagDragArmedForTests());
	Fixture.CatalogA->ExpectedAnimationTags.Reset();
	Fixture.CatalogA->ExpectedAnimationTags.AddTag(BlockTag);
	Panel->Refresh();
	TestFalse(
		TEXT("A row rebuild fails closed when the armed expectation disappears"),
		Panel->IsExpectedTagDragArmedForTests());
	Panel->Shutdown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusExpectedTagsPanelPresentationProjectionTest,
	"Paper2DPlus.CharacterCoverage.Panel.ResolverProjectionAndStatusLanguage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusExpectedTagsPanelPresentationProjectionTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	using Paper2DPlusAnimationTagQuery::FAnimationTagSet;

	const FGameplayTag HeavyTag =
		Paper2DPlusAnimationTags::Combat_Heavy.GetTag();
	const FGameplayTag LightTag =
		Paper2DPlusAnimationTags::Combat_Light.GetTag();
	const FGameplayTag CombatTag =
		Paper2DPlusAnimationTags::Combat.GetTag();
	const FGameplayTag BlockTag =
		Paper2DPlusAnimationTags::Combat_Block.GetTag();
	const FGameplayTag AirborneTag =
		Paper2DPlusAnimationTags::Context_Airborne.GetTag();
	if (!TestTrue(
		TEXT("Native presentation tags are registered"),
		HeavyTag.IsValid()
			&& LightTag.IsValid()
			&& CombatTag.IsValid()
			&& BlockTag.IsValid()
			&& AirborneTag.IsValid()))
	{
		return false;
	}

	TMap<FString, FAnimationTagSet> TagMap;
	FAnimationTagSet& Exact = TagMap.Add(TEXT("exact"));
	Exact.AnimationName = TEXT("ExactHeavy");
	Exact.OwnTags.AddTag(HeavyTag);
	FAnimationTagSet& Superset = TagMap.Add(TEXT("superset"));
	Superset.AnimationName = TEXT("AirLight");
	Superset.OwnTags.AddTag(LightTag);
	Superset.OwnTags.AddTag(AirborneTag);
	FAnimationTagSet& GroupCarrier = TagMap.Add(TEXT("group"));
	GroupCarrier.AnimationName = TEXT("GroupCarrier");
	GroupCarrier.GroupImpliedTags.AddTag(CombatTag);
	FAnimationTagSet& ChainCarrier = TagMap.Add(TEXT("chain"));
	ChainCarrier.AnimationName = TEXT("ChainCarrier");
	ChainCarrier.ChainInheritedTags.AddTag(CombatTag);

	FGameplayTagContainer ExpectedTags;
	ExpectedTags.AddTag(HeavyTag);
	ExpectedTags.AddTag(LightTag);
	ExpectedTags.AddTag(CombatTag);
	ExpectedTags.AddTag(BlockTag);
	const FCharacterCoverageResolveResult Resolved =
		FCharacterCoverageResolver::ResolveFromTagMapForTests(
			TagMap,
			ExpectedTags);

	const FCharacterCoverageRow* Covered =
		ExpectedTagsPanelTest_FindRow(Resolved.Rows, HeavyTag);
	const FCharacterCoverageRow* Qualified =
		ExpectedTagsPanelTest_FindRow(Resolved.Rows, LightTag);
	const FCharacterCoverageRow* NearMiss =
		ExpectedTagsPanelTest_FindRow(Resolved.Rows, CombatTag);
	const FCharacterCoverageRow* Missing =
		ExpectedTagsPanelTest_FindRow(Resolved.Rows, BlockTag);
	if (!TestNotNull(TEXT("Covered projection row exists"), Covered)
		|| !TestNotNull(TEXT("Qualified projection row exists"), Qualified)
		|| !TestNotNull(TEXT("Near-miss projection row exists"), NearMiss)
		|| !TestNotNull(TEXT("Missing projection row exists"), Missing))
	{
		return false;
	}

	TestTrue(
		TEXT("Exact authored coverage presents as Covered"),
		SExpectedTagsPanel::GetRowPresentationForTests(*Covered)
			== EExpectedTagsRowPresentation::Covered);
	TestTrue(
		TEXT("Superset-only authored coverage presents as Qualified"),
		SExpectedTagsPanel::GetRowPresentationForTests(*Qualified)
			== EExpectedTagsRowPresentation::Qualified);
	TestTrue(
		TEXT("Inherited/implied-only coverage presents as Near Miss"),
		SExpectedTagsPanel::GetRowPresentationForTests(*NearMiss)
			== EExpectedTagsRowPresentation::NearMiss);
	TestTrue(
		TEXT("No carrier presents as Missing"),
		SExpectedTagsPanel::GetRowPresentationForTests(*Missing)
			== EExpectedTagsRowPresentation::Missing);

	const FString NearMissText =
		SExpectedTagsPanel::GetRowStatusTextForTests(*NearMiss).ToString();
	TestTrue(
		TEXT("Group-implied carriers remain separately explained"),
		NearMissText.Contains(TEXT("group-implied on GroupCarrier")));
	TestTrue(
		TEXT("Chain-inherited carriers remain separately explained"),
		NearMissText.Contains(TEXT("chain-inherited on ChainCarrier")));
	TestTrue(
		TEXT("Near-miss guidance uses the approved authored-language phrase"),
		NearMissText.Contains(TEXT("assign to make authored")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusExpectedTagsPanelEmptyStatesTest,
	"Paper2DPlus.CharacterCoverage.Panel.AuthorityEmptyStatesFailClosed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusExpectedTagsPanelEmptyStatesTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	FExpectedTagsPanelTestFixture Fixture(TEXT("EmptyStates"));

	{
		const FExpectedTagsCatalogProvider Provider = []()
		{
			return FExpectedTagsCatalogResolution::NotConfigured();
		};
		const TSharedRef<SExpectedTagsPanel> Panel = Fixture.MakePanel(Provider);
		TestTrue(
			TEXT("No configured authority has its own informational state"),
			Panel->GetPanelStateForTests()
				== EExpectedTagsPanelState::NoAuthoritativeCatalog);
		TestTrue(
			TEXT("No-authority guidance names the Default Character Catalog"),
			Panel->GetEmptyStateTextForTests().ToString().Contains(
				TEXT("Default Character Catalog")));
		Panel->Shutdown();
	}

	{
		const FExpectedTagsCatalogProvider Provider = []()
		{
			return FExpectedTagsCatalogResolution::ConfiguredButUnavailable(
				FSoftObjectPath(TEXT("/Game/Missing/MissingCatalog.MissingCatalog")));
		};
		const TSharedRef<SExpectedTagsPanel> Panel = Fixture.MakePanel(Provider);
		TestTrue(
			TEXT("A configured-but-unloadable Catalog fails closed"),
			Panel->GetPanelStateForTests()
				== EExpectedTagsPanelState::ConfiguredCatalogUnavailable);
		TestEqual(
			TEXT("An unloadable authority never calls the Catalog accessor"),
			Panel->GetCatalogAccessorCallsInLastRefreshForTests(),
			0);
		TestEqual(
			TEXT("An unloadable authority never invents coverage"),
			Panel->GetResolverCallsInLastRefreshForTests(),
			0);
		Panel->Shutdown();
	}

	{
		const FExpectedTagsCatalogProvider Provider = [&Fixture]()
		{
			return FExpectedTagsCatalogResolution::Loaded(Fixture.CatalogA);
		};
		const TSharedRef<SExpectedTagsPanel> Panel = Fixture.MakePanel(Provider);
		TestTrue(
			TEXT("A Profile absent from the Catalog has an informational state"),
			Panel->GetPanelStateForTests()
				== EExpectedTagsPanelState::ProfileNotInCatalog);
		TestEqual(
			TEXT("Absent membership still uses one accessor call"),
			Panel->GetCatalogAccessorCallsInLastRefreshForTests(),
			1);
		TestEqual(
			TEXT("Absent membership decides its state before the resolver runs"),
			Panel->GetResolverCallsInLastRefreshForTests(),
			0);
		Panel->Shutdown();
	}

	Fixture.AddProfileToCatalog(Fixture.CatalogA);
	{
		const FExpectedTagsCatalogProvider Provider = [&Fixture]()
		{
			return FExpectedTagsCatalogResolution::Loaded(Fixture.CatalogA);
		};
		const TSharedRef<SExpectedTagsPanel> Panel = Fixture.MakePanel(Provider);
		TestTrue(
			TEXT("A member with no expectations has an informational state"),
			Panel->GetPanelStateForTests()
				== EExpectedTagsPanelState::CatalogHasNoExpectations);
		TestEqual(
			TEXT("No-expectation membership still uses one Catalog accessor"),
			Panel->GetCatalogAccessorCallsInLastRefreshForTests(),
			1);
		TestEqual(
			TEXT("No-expectation membership decides its state before the resolver runs"),
			Panel->GetResolverCallsInLastRefreshForTests(),
			0);
		Panel->Shutdown();
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusExpectedTagsPanelReadOnlyLifecycleTest,
	"Paper2DPlus.CharacterCoverage.Panel.OpenRefreshAndProfileSwitchAreReadOnly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusExpectedTagsPanelReadOnlyLifecycleTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	if (!TestNotNull(TEXT("GEditor is available"), GEditor))
	{
		return false;
	}

	const FGameplayTag HeavyTag =
		Paper2DPlusAnimationTags::Combat_Heavy.GetTag();
	FExpectedTagsPanelTestFixture Fixture(TEXT("ReadOnly"));
	const int32 FirstIndex =
		Fixture.AddAnimation(Fixture.Profile, TEXT("FirstHeavy"));
	Fixture.Profile->Flipbooks[FirstIndex]
		.EditorMeta.AnimationTags.AddTag(HeavyTag);
	UPaper2DPlusCharacterProfileAsset* SecondProfile =
		NewObject<UPaper2DPlusCharacterProfileAsset>(
			Fixture.Package,
			TEXT("SecondProfile"),
			RF_Public | RF_Standalone | RF_Transactional);
	const int32 SecondIndex =
		Fixture.AddAnimation(SecondProfile, TEXT("SecondHeavy"));
	SecondProfile->Flipbooks[SecondIndex]
		.EditorMeta.AnimationTags.AddTag(HeavyTag);
	Fixture.AddProfileToCatalog(Fixture.CatalogA, Fixture.Profile);
	Fixture.AddProfileToCatalog(Fixture.CatalogA, SecondProfile);
	Fixture.CatalogA->ExpectedAnimationTags.AddTag(HeavyTag);
	Fixture.Package->SetDirtyFlag(false);

	GEditor->ResetTransaction(FText::FromString(
		TEXT("Expected Tags panel read-only start")));
	const int32 QueueLengthBefore = GEditor->Trans->GetQueueLength();
	const int32 UndoCountBefore = GEditor->Trans->GetUndoCount();
	const FExpectedTagsCatalogProvider Provider = [&Fixture]()
	{
		return FExpectedTagsCatalogResolution::Loaded(Fixture.CatalogA);
	};

	const TSharedRef<SExpectedTagsPanel> Panel = Fixture.MakePanel(Provider);
	Panel->Refresh();
	Fixture.Model->OnAssetDataChanged.Broadcast();
	Fixture.Model->InitializeFromAsset(SecondProfile);

	TestTrue(
		TEXT("Profile switching re-resolves the new member read-only"),
		Panel->GetPanelStateForTests() == EExpectedTagsPanelState::Ready);
	TestFalse(
		TEXT("Opening, refreshing, and switching never dirty authored assets"),
		Fixture.Package->IsDirty());
	TestEqual(
		TEXT("Read-only lifecycle opens no transaction"),
		GEditor->Trans->GetQueueLength(),
		QueueLengthBefore);
	TestEqual(
		TEXT("Read-only lifecycle changes no undo cursor"),
		GEditor->Trans->GetUndoCount(),
		UndoCountBefore);

	Panel->Shutdown();
	GEditor->ResetTransaction(FText::FromString(
		TEXT("Expected Tags panel read-only end")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusExpectedTagsPanelCatalogWatchTest,
	"Paper2DPlus.CharacterCoverage.Panel.CatalogWatchEditSwapAndConditionalClear",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusExpectedTagsPanelCatalogWatchTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	const FGameplayTag HeavyTag =
		Paper2DPlusAnimationTags::Combat_Heavy.GetTag();
	const FGameplayTag LightTag =
		Paper2DPlusAnimationTags::Combat_Light.GetTag();
	const FGameplayTag BlockTag =
		Paper2DPlusAnimationTags::Combat_Block.GetTag();
	const FGameplayTag SwimmingTag =
		Paper2DPlusAnimationTags::Context_Swimming.GetTag();
	FExpectedTagsPanelTestFixture Fixture(TEXT("CatalogWatch"));
	Fixture.AddAnimation(Fixture.Profile, TEXT("Idle"));
	Fixture.AddProfileToCatalog(Fixture.CatalogA);
	Fixture.AddProfileToCatalog(Fixture.CatalogB);
	Fixture.CatalogA->ExpectedAnimationTags.AddTag(HeavyTag);
	Fixture.CatalogB->ExpectedAnimationTags.AddTag(LightTag);

	UPaper2DPlusCharacterCatalogAsset* CurrentCatalog = Fixture.CatalogA;
	const FExpectedTagsCatalogProvider Provider = [&CurrentCatalog]()
	{
		return CurrentCatalog
			? FExpectedTagsCatalogResolution::Loaded(CurrentCatalog)
			: FExpectedTagsCatalogResolution::NotConfigured();
	};
	const TSharedRef<SExpectedTagsPanel> Panel = Fixture.MakePanel(Provider);

	// The panel watches the Catalog for ITSELF. It must never install it into the shared model's
	// secondary watched-object slot: that slot drives OnAssetExternallyModified, which rebuilds every
	// panel in the Character Profile editor, so routing Catalog edits through it tore down in-progress
	// inline renames and reset scroll positions in an unrelated workspace.
	TestNull(
		TEXT("The panel does not install the Catalog into the shared secondary watch"),
		Fixture.Model->GetSecondaryWatchedObject());
	TestTrue(
		TEXT("The panel records its own Catalog watch"),
		Panel->OwnsCatalogWatchForTests());
	TestEqual(
		TEXT("The open panel strongly retains its soft-loaded authority"),
		Panel->GetRetainedCatalogForTests(),
		static_cast<UObject*>(Fixture.CatalogA));

	Fixture.CatalogA->Modify();
	Fixture.CatalogA->ExpectedAnimationTags.Reset();
	Fixture.CatalogA->ExpectedAnimationTags.AddTag(BlockTag);
	ExpectedTagsPanelTest_PumpDeferredModify();
	TestNotNull(
		TEXT("An external edit of the watched Catalog refreshes expected rows"),
		ExpectedTagsPanelTest_FindRow(
			Panel->GetCoverageRowsForTests(),
			BlockTag));

	CurrentCatalog = Fixture.CatalogB;
	Panel->Refresh();
	TestNull(
		TEXT("A Catalog swap still leaves the shared secondary watch untouched"),
		Fixture.Model->GetSecondaryWatchedObject());
	TestEqual(
		TEXT("The strong authority guard follows a Catalog swap"),
		Panel->GetRetainedCatalogForTests(),
		static_cast<UObject*>(Fixture.CatalogB));
	TestNotNull(
		TEXT("A Catalog swap immediately projects the new expectations"),
		ExpectedTagsPanelTest_FindRow(
			Panel->GetCoverageRowsForTests(),
			LightTag));

	const uint32 RefreshBeforeOldCatalogEdit =
		Panel->GetRefreshSerialForTests();
	Fixture.CatalogA->Modify();
	ExpectedTagsPanelTest_PumpDeferredModify();
	TestEqual(
		TEXT("The retired Catalog is no longer watched after a swap"),
		Panel->GetRefreshSerialForTests(),
		RefreshBeforeOldCatalogEdit);

	Fixture.CatalogB->Modify();
	Fixture.CatalogB->ExpectedAnimationTags.Reset();
	Fixture.CatalogB->ExpectedAnimationTags.AddTag(SwimmingTag);
	ExpectedTagsPanelTest_PumpDeferredModify();
	TestNotNull(
		TEXT("The replacement Catalog is live-watched"),
		ExpectedTagsPanelTest_FindRow(
			Panel->GetCoverageRowsForTests(),
			SwimmingTag));

	// A Layer workspace owns that slot legitimately. This panel must be inert with respect to it —
	// before, during, and after its own Catalog lifecycle.
	UPaperFlipbook* ForeignWatch = NewObject<UPaperFlipbook>(
		Fixture.Package,
		TEXT("ForeignSecondaryWatch"),
		RF_Transactional);
	Fixture.Model->SetSecondaryWatchedObject(ForeignWatch);

	// An edit to the panel's own Catalog must not disturb the foreign watch, and — the actual
	// regression — must not raise the model's external-modified broadcast at all.
	const int32 ExternalBroadcastsBefore = Fixture.ExternalModifiedBroadcastCount;
	Fixture.CatalogB->Modify();
	ExpectedTagsPanelTest_PumpDeferredModify();
	TestEqual(
		TEXT("A Catalog edit does not fan out to the profile editor's panels"),
		Fixture.ExternalModifiedBroadcastCount,
		ExternalBroadcastsBefore);

	CurrentCatalog = nullptr;
	Panel->Refresh();
	TestEqual(
		TEXT("Losing its Catalog preserves a secondary watch owned by another surface"),
		Fixture.Model->GetSecondaryWatchedObject(),
		static_cast<UObject*>(ForeignWatch));
	TestFalse(
		TEXT("The panel reports no Catalog watch once its Catalog is gone"),
		Panel->OwnsCatalogWatchForTests());
	Panel->Shutdown();
	TestEqual(
		TEXT("Shutdown also preserves the foreign watch"),
		Fixture.Model->GetSecondaryWatchedObject(),
		static_cast<UObject*>(ForeignWatch));
	Fixture.Model->SetSecondaryWatchedObject(nullptr);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusExpectedTagsPanelSignalsAndNoOpPickerTest,
	"Paper2DPlus.CharacterCoverage.Panel.ColorUndoSettingsAndPickerNoOp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusExpectedTagsPanelSignalsAndNoOpPickerTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	if (!TestNotNull(TEXT("GEditor is available"), GEditor))
	{
		return false;
	}

	const FGameplayTag HeavyTag =
		Paper2DPlusAnimationTags::Combat_Heavy.GetTag();
	FExpectedTagsPanelTestFixture Fixture(TEXT("Signals"));
	const int32 HeavyIndex =
		Fixture.AddAnimation(Fixture.Profile, TEXT("Heavy"));
	Fixture.Profile->Flipbooks[HeavyIndex]
		.EditorMeta.AnimationTags.AddTag(HeavyTag);
	Fixture.AddProfileToCatalog(Fixture.CatalogA);
	Fixture.CatalogA->ExpectedAnimationTags.AddTag(HeavyTag);
	const FExpectedTagsCatalogProvider Provider = [&Fixture]()
	{
		return FExpectedTagsCatalogResolution::Loaded(Fixture.CatalogA);
	};
	const TSharedRef<SExpectedTagsPanel> Panel = Fixture.MakePanel(Provider);

	uint32 Serial = Panel->GetRefreshSerialForTests();
	const int32 BodyRebuildsBefore = Panel->GetBodyRebuildCountForTests();
	UPaper2DPlusSettings::OnTagColorsChanged().Broadcast();
	TestEqual(
		TEXT("Tag-color changes rebuild the rows so baked chip tints repaint"),
		Panel->GetBodyRebuildCountForTests(),
		BodyRebuildsBefore + 1);
	TestEqual(
		TEXT("Tag-color changes never re-run the coverage refresh"),
		Panel->GetRefreshSerialForTests(),
		Serial);
	Serial = Panel->GetRefreshSerialForTests();
	UPaper2DPlusSettings::OnCharacterCatalogSettingsChanged().Broadcast();
	TestEqual(
		TEXT("Catalog-settings changes refresh authority and coverage"),
		Panel->GetRefreshSerialForTests(),
		Serial + 1);
	Serial = Panel->GetRefreshSerialForTests();
	Panel->PostUndo(true);
	TestEqual(
		TEXT("Undo refreshes Catalog-derived coverage"),
		Panel->GetRefreshSerialForTests(),
		Serial + 1);
	Serial = Panel->GetRefreshSerialForTests();
	Panel->PostRedo(true);
	TestEqual(
		TEXT("Redo refreshes Catalog-derived coverage"),
		Panel->GetRefreshSerialForTests(),
		Serial + 1);

	Fixture.Package->SetDirtyFlag(false);
	GEditor->ResetTransaction(FText::FromString(
		TEXT("Expected Tags panel no-op picker start")));
	const int32 QueueLengthBefore = GEditor->Trans->GetQueueLength();
	const int32 UndoCountBefore = GEditor->Trans->GetUndoCount();
	const EExpectedTagAssignmentResult Assignment =
		Panel->AssignTagToAnimationForTests(HeavyTag, HeavyIndex);
	TestTrue(
		TEXT("Picking an animation that already authors the tag is a no-op"),
		Assignment == EExpectedTagAssignmentResult::NoChange);
	TestFalse(
		TEXT("A picker no-op does not dirty the profile"),
		Fixture.Package->IsDirty());
	TestEqual(
		TEXT("A picker no-op opens no transaction"),
		GEditor->Trans->GetQueueLength(),
		QueueLengthBefore);
	TestEqual(
		TEXT("A picker no-op changes no undo cursor"),
		GEditor->Trans->GetUndoCount(),
		UndoCountBefore);

	Panel->Shutdown();
	GEditor->ResetTransaction(FText::FromString(
		TEXT("Expected Tags panel no-op picker end")));
	return true;
}

#endif // WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
