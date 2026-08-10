// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "CombatProfileEditor/CombatProfileEditorSession.h"
#include "CombatProfileEditor/CombatAttackBrowserPanel.h"
#include "CombatProfileEditor/CombatAttackInspectorPanel.h"
#include "CombatProfileEditor/CombatProfileCollectionPanel.h"
#include "CombatProfileEditor/CombatProfileSetupPanel.h"
#include "CombatProfileAssetEditorToolkit.h"
#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "PackageTools.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Paper2DPlusCombatProfileAsset.h"
#include "Paper2DPlusFrameData.h"
#include "PaperFlipbook.h"
#include "ProfileNavigatorPanel.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace
{
	FGameplayTag CombatEditor_AttackTag()
	{
		return FGameplayTag::RequestGameplayTag(
			FName(TEXT("PlayerStates.Attacking.GroundAttack")), false);
	}

	FFlipbookProfileEntry CombatEditor_Move(
		UPaper2DPlusCharacterProfileAsset* Character,
		const TCHAR* Name,
		int32 Reach)
	{
		FFlipbookProfileEntry Entry;
		Entry.Identity.FlipbookName = Name;
		Entry.Identity.Flipbook = NewObject<UPaperFlipbook>(Character);
		Entry.CombatData.Frames.SetNum(1);
		FHitboxData Attack;
		Attack.Type = EHitboxType::Attack;
		Attack.X = 0;
		Attack.Width = Reach;
		Attack.Height = 8;
		Entry.CombatData.Frames[0].Hitboxes.Add(Attack);
		return Entry;
	}

	UPaper2DPlusCombatProfileAsset* CombatEditor_MakeAsset()
	{
		UPaper2DPlusCombatProfileAsset* Combat = NewObject<UPaper2DPlusCombatProfileAsset>(
			GetTransientPackage(), NAME_None, RF_Transactional);
		Combat->CharacterProfile = NewObject<UPaper2DPlusCharacterProfileAsset>(
			Combat, NAME_None, RF_Transactional);
		Combat->CharacterProfile->Flipbooks.Add(CombatEditor_Move(Combat->CharacterProfile, TEXT("Jab"), 20));
		Combat->CharacterProfile->Flipbooks.Add(CombatEditor_Move(Combat->CharacterProfile, TEXT("Sweep"), 60));

		FFlipbookTagMapping& Mapping = Combat->CharacterProfile->TagMappings.FindOrAdd(CombatEditor_AttackTag());
		Mapping.Entries.Add(FFlipbookTagMappingEntry(TEXT("Jab")));
		Mapping.Entries.Add(FFlipbookTagMappingEntry(TEXT("Sweep")));
		return Combat;
	}

	UPaper2DPlusCombatProfileAsset* CombatEditor_MakeLargeAsset(int32 AttackCount)
	{
		UPaper2DPlusCombatProfileAsset* Combat = NewObject<UPaper2DPlusCombatProfileAsset>(
			GetTransientPackage(), NAME_None, RF_Transactional);
		Combat->CharacterProfile = NewObject<UPaper2DPlusCharacterProfileAsset>(
			Combat, NAME_None, RF_Transactional);
		FFlipbookTagMapping& Mapping =
			Combat->CharacterProfile->TagMappings.FindOrAdd(CombatEditor_AttackTag());
		for (int32 Index = 0; Index < AttackCount; ++Index)
		{
			const FString MoveName = FString::Printf(TEXT("Attack_%03d"), Index);
			Combat->CharacterProfile->Flipbooks.Add(
				CombatEditor_Move(Combat->CharacterProfile, *MoveName, 20 + Index));
			Mapping.Entries.Add(FFlipbookTagMappingEntry(*MoveName));
		}
		return Combat;
	}

	struct FScopedCombatThumbnailFixture
	{
		FString Directory;
		FString VisiblePackageName;
		FString OffscreenPackageName;
		FString LruPackageName;
		FString VisibleFilePath;
		FString OffscreenFilePath;
		FString LruFilePath;
		TArray<FSoftObjectPath> VisiblePaths;
		TArray<FSoftObjectPath> OffscreenPaths;
		TArray<FSoftObjectPath> LruPaths;

		FScopedCombatThumbnailFixture()
		{
			const FString Guid = FGuid::NewGuid().ToString(EGuidFormats::Digits);
			Directory = FString::Printf(
				TEXT("/Game/__AutomationTemp__/P2DPCombatThumbnail_%s"), *Guid);
			VisiblePackageName = Directory / TEXT("Visible");
			OffscreenPackageName = Directory / TEXT("Offscreen");
			LruPackageName = Directory / TEXT("Lru");
			VisibleFilePath = FPackageName::LongPackageNameToFilename(
				VisiblePackageName, FPackageName::GetAssetPackageExtension());
			OffscreenFilePath = FPackageName::LongPackageNameToFilename(
				OffscreenPackageName, FPackageName::GetAssetPackageExtension());
			LruFilePath = FPackageName::LongPackageNameToFilename(
				LruPackageName, FPackageName::GetAssetPackageExtension());
			IFileManager::Get().MakeDirectory(*FPaths::GetPath(VisibleFilePath), true);
		}

		~FScopedCombatThumbnailFixture()
		{
			FText Ignored;
			Cleanup(Ignored);
		}

		bool BuildAndUnload(FText& OutError)
		{
			UPackage* VisiblePackage = CreatePackage(*VisiblePackageName);
			UPackage* OffscreenPackage = CreatePackage(*OffscreenPackageName);
			UPackage* LruPackage = CreatePackage(*LruPackageName);
			if (!VisiblePackage || !OffscreenPackage || !LruPackage)
			{
				OutError = FText::FromString(TEXT("Could not create Combat thumbnail packages"));
				return false;
			}
			VisiblePackage->AddToRoot();
			OffscreenPackage->AddToRoot();
			LruPackage->AddToRoot();

			UPaperFlipbook* FirstVisible = nullptr;
			for (int32 Index = 0; Index < 64; ++Index)
			{
				UPaperFlipbook* Flipbook = NewObject<UPaperFlipbook>(
					VisiblePackage,
					*FString::Printf(TEXT("FB_Visible_%03d"), Index),
					RF_Public | RF_Standalone);
				if (!FirstVisible)
				{
					FirstVisible = Flipbook;
				}
				VisiblePaths.Add(FSoftObjectPath(Flipbook));
			}
			UPaperFlipbook* FirstOffscreen = nullptr;
			for (int32 Index = 0; Index < 64; ++Index)
			{
				UPaperFlipbook* Flipbook = NewObject<UPaperFlipbook>(
					OffscreenPackage,
					*FString::Printf(TEXT("FB_Offscreen_%03d"), Index),
					RF_Public | RF_Standalone);
				if (!FirstOffscreen)
				{
					FirstOffscreen = Flipbook;
				}
				OffscreenPaths.Add(FSoftObjectPath(Flipbook));
			}
			UPaperFlipbook* FirstLru = nullptr;
			for (int32 Index = 0; Index < 50; ++Index)
			{
				UPaperFlipbook* Flipbook = NewObject<UPaperFlipbook>(
					LruPackage,
					*FString::Printf(TEXT("FB_Lru_%02d"), Index),
					RF_Public | RF_Standalone);
				if (!FirstLru)
				{
					FirstLru = Flipbook;
				}
				LruPaths.Add(FSoftObjectPath(Flipbook));
			}

			FSavePackageArgs SaveArgs;
			SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
			SaveArgs.SaveFlags = SAVE_NoError;
			const bool bSaved =
				UPackage::SavePackage(VisiblePackage, FirstVisible, *VisibleFilePath, SaveArgs)
				&& UPackage::SavePackage(OffscreenPackage, FirstOffscreen, *OffscreenFilePath, SaveArgs)
				&& UPackage::SavePackage(LruPackage, FirstLru, *LruFilePath, SaveArgs);
			VisiblePackage->RemoveFromRoot();
			OffscreenPackage->RemoveFromRoot();
			LruPackage->RemoveFromRoot();
			if (!bSaved)
			{
				OutError = FText::FromString(TEXT("Could not save Combat thumbnail packages"));
				return false;
			}
			return Unload(OutError);
		}

		bool Unload(FText& OutError) const
		{
			TArray<UPackage*> Packages;
			for (const FString& PackageName : {
				VisiblePackageName, OffscreenPackageName, LruPackageName })
			{
				if (UPackage* Package = FindPackage(nullptr, *PackageName))
				{
					Package->SetDirtyFlag(false);
					Packages.Add(Package);
				}
			}
			const bool bUnloaded = Packages.IsEmpty()
				|| UPackageTools::UnloadPackages(Packages, OutError, true);
			CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
			return bUnloaded;
		}

		bool Cleanup(FText& OutError) const
		{
			const bool bUnloaded = Unload(OutError);
			const FString SafeRoot = FPaths::ConvertRelativePathToFull(
				FPaths::ProjectContentDir() / TEXT("__AutomationTemp__"));
			const FString FixtureDirectory = FPaths::ConvertRelativePathToFull(
				FPaths::GetPath(VisibleFilePath));
			const bool bSafe = Directory.StartsWith(
				TEXT("/Game/__AutomationTemp__/P2DPCombatThumbnail_"))
				&& FPaths::IsUnderDirectory(FixtureDirectory, SafeRoot);
			if (!bSafe)
			{
				OutError = FText::FromString(TEXT("Refused unsafe Combat thumbnail cleanup"));
				return false;
			}
			IFileManager& Files = IFileManager::Get();
			bool bFilesGone = true;
			for (const FString& FilePath : { VisibleFilePath, OffscreenFilePath, LruFilePath })
			{
				for (const FString& PackageFile : {
					FilePath,
					FPaths::ChangeExtension(FilePath, TEXT("uexp")),
					FPaths::ChangeExtension(FilePath, TEXT("ubulk")),
					FPaths::ChangeExtension(FilePath, TEXT("uptnl")) })
				{
					bFilesGone &= !Files.FileExists(*PackageFile)
						|| Files.Delete(*PackageFile, false, true, true);
				}
			}
			const bool bDirectoryGone = !Files.DirectoryExists(*FixtureDirectory)
				|| Files.DeleteDirectory(*FixtureDirectory, false, false);
			return bUnloaded && bFilesGone && bDirectoryGone;
		}
	};

	UPaper2DPlusCombatProfileAsset* CombatEditor_MakeThumbnailAsset(
		const TArray<FSoftObjectPath>& VisiblePaths,
		const TArray<FSoftObjectPath>& OffscreenPaths,
		int32 AttackCount)
	{
		UPaper2DPlusCombatProfileAsset* Combat = NewObject<UPaper2DPlusCombatProfileAsset>(
			GetTransientPackage(), NAME_None, RF_Transactional);
		Combat->CharacterProfile = NewObject<UPaper2DPlusCharacterProfileAsset>(
			Combat, NAME_None, RF_Transactional);
		FFlipbookTagMapping& Mapping =
			Combat->CharacterProfile->TagMappings.FindOrAdd(CombatEditor_AttackTag());
		for (int32 Index = 0; Index < AttackCount; ++Index)
		{
			const FString MoveName = FString::Printf(TEXT("ThumbnailAttack_%03d"), Index);
			FFlipbookProfileEntry Entry;
			Entry.Identity.FlipbookName = MoveName;
			const FSoftObjectPath& FlipbookPath = Index < VisiblePaths.Num()
				? VisiblePaths[Index]
				: OffscreenPaths[Index - VisiblePaths.Num()];
			Entry.Identity.Flipbook = TSoftObjectPtr<UPaperFlipbook>(FlipbookPath);
			Entry.CombatData.Frames.SetNum(1);
			FHitboxData& Attack = Entry.CombatData.Frames[0].Hitboxes.AddDefaulted_GetRef();
			Attack.Type = EHitboxType::Attack;
			Attack.Width = 24;
			Attack.Height = 8;
			Combat->CharacterProfile->Flipbooks.Add(MoveTemp(Entry));
			Mapping.Entries.Add(FFlipbookTagMappingEntry(*MoveName));
		}
		return Combat;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCombatEditorStableSelectionTest,
	"Paper2DPlus.Editor.CombatProfile.Session.StableSelectionRenameReorderAndNoNeighborFallback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCombatEditorStableSelectionTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusCombatProfileAsset* Combat = CombatEditor_MakeAsset();
	TSharedRef<FCombatProfileEditorSession> Session = MakeShared<FCombatProfileEditorSession>(Combat);
	const FProfileItemIdentity SweepIdentity = Session->MakeAttackIdentity(TEXT("Sweep"));
	TestTrue(TEXT("Canonical path-backed selection succeeds"), Session->SelectAttack(SweepIdentity));

	Combat->CharacterProfile->Flipbooks.Swap(0, 1);
	const int32 SweepIndex = Combat->CharacterProfile->Flipbooks.IndexOfByPredicate([](const FFlipbookProfileEntry& Entry)
	{
		return Entry.Identity.FlipbookName == TEXT("Sweep");
	});
	TestTrue(TEXT("Fixture still contains Sweep"), SweepIndex != INDEX_NONE);
	if (SweepIndex != INDEX_NONE)
	{
		Combat->CharacterProfile->RenameFlipbookAndPropagate(SweepIndex, TEXT("WideSweep"));
	}
	Combat->RefreshAttackOptionMoveBindings();
	Session->RefreshFromAsset();
	TestEqual(TEXT("Object identity follows the renamed move"), Session->GetSelectedAttack().FallbackKey, FString(TEXT("WideSweep")));
	TestEqual(TEXT("Object path remains authoritative"), Session->GetSelectedAttack().ObjectPath, SweepIdentity.ObjectPath);

	Combat->CharacterProfile->Flipbooks.RemoveAt(SweepIndex);
	Session->RefreshFromAsset();
	TestFalse(TEXT("Deleted selection clears instead of selecting a neighbor"), Session->GetSelectedAttack().IsValid());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCombatEditorPickerTest,
	"Paper2DPlus.Editor.CombatProfile.Session.SharedPickerSearchProjectionAndSelection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCombatEditorPickerTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusCombatProfileAsset* Combat = CombatEditor_MakeAsset();
	TSharedRef<FCombatProfileEditorSession> Session = MakeShared<FCombatProfileEditorSession>(Combat);
	TSharedRef<FCombatAttackPickerSource> Source = MakeShared<FCombatAttackPickerSource>(Session);
	TArray<FProfilePickerItem> Items;
	Source->GetItems(Items);
	TestEqual(TEXT("Both eligible moves appear"), Items.Num(), 2);
	TestTrue(TEXT("Attack tag is searchable"), Items.Num() > 0 && Items[0].SearchTags.HasTag(CombatEditor_AttackTag()));
	TestTrue(TEXT("Rows expose inherited/tuned state without relying on color"),
		Items.Num() > 0 && !Items[0].SecondaryText.IsEmpty());
	TestTrue(TEXT("Source selection routes through the shared session"),
		Items.Num() > 1 && Source->SelectItem(Items[1].Identity));
	TestTrue(TEXT("Picker and session share the same stable identity"),
		Items.Num() > 1 && Source->GetSelectedIdentity().Matches(Items[1].Identity));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCombatEditorLargeAttackCatalogTest,
	"Paper2DPlus.Editor.CombatProfile.AttackCatalog.LargeVirtualizedSearchSelectionAndVisibleThumbnailLru",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCombatEditorLargeAttackCatalogTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusCombatProfileAsset* LargeCombat = CombatEditor_MakeLargeAsset(128);
	TSharedRef<FCombatProfileEditorSession> Session =
		MakeShared<FCombatProfileEditorSession>(LargeCombat);
	TSharedRef<FCombatAttackPickerSource> Source =
		MakeShared<FCombatAttackPickerSource>(Session);
	TArray<FProfilePickerItem> Items;
	Source->GetItems(Items);
	TestEqual(TEXT("100+ attacks project through the shared source"), Items.Num(), 128);
	const FProfilePickerItem* Target = Items.FindByPredicate([](const FProfilePickerItem& Item)
	{
		return Item.Identity.FallbackKey == TEXT("Attack_117");
	});
	if (!TestNotNull(TEXT("large fixture contains the search target"), Target))
	{
		return false;
	}

	if (FSlateApplication::IsInitialized())
	{
		TSharedRef<SProfileNavigatorPanel> Navigator =
			SNew(SProfileNavigatorPanel)
			.Source(StaticCastSharedRef<IProfileItemPickerSource>(Source))
			.Mode(EProfileNavigatorMode::Pinned);
		TestEqual(TEXT("unfiltered navigator exposes all 128 attacks"),
			Navigator->GetResultCountForTests(), 128);
		const int32 Generated = Navigator->GenerateRowsForViewportForTests(
			FVector2D(300.0f, 180.0f));
		TestTrue(TEXT("constrained navigator constructs visible rows"), Generated > 0);
		TestTrue(TEXT("constrained navigator virtualizes most attack rows"), Generated < 128);
		Navigator->SetQueryForTests(TEXT("Attack_117"));
		TestEqual(TEXT("search narrows 100+ attacks to the intended row"),
			Navigator->GetResultCountForTests(), 1);
		TestTrue(TEXT("search result selects through the shared stable identity"),
			Navigator->SelectIdentityForTests(Target->Identity));
		TestTrue(TEXT("navigator and session agree on the selected attack"),
			Source->GetSelectedIdentity().Matches(Target->Identity));

		LargeCombat->CharacterProfile->Flipbooks.Swap(5, 117);
		Session->RefreshFromAsset();
		Navigator->RefreshResults();
		TestTrue(TEXT("selection survives attack reorder by object identity"),
			Source->GetSelectedIdentity().Matches(Target->Identity));

		// The Setup attack browser is the ONE selectable attack surface (v8); its search box owns
		// the filtering the retired Attack Catalog navigator used to provide.
		TSharedRef<SCombatAttackBrowserPanel> SearchBrowser =
			SNew(SCombatAttackBrowserPanel).Session(Session);
		TestEqual(TEXT("attack browser projects the whole catalog"),
			SearchBrowser->GetRowCountForTests(), 128);
		SearchBrowser->SetSearchTextForTests(TEXT("Attack_117"));
		TestEqual(TEXT("browser search narrows the cards to the intended move"),
			SearchBrowser->GetVisibleRowCountForTests(), 1);
		SearchBrowser->SetSearchTextForTests(TEXT("GroundAttack"));
		TestEqual(TEXT("browser search also matches the attack tag"),
			SearchBrowser->GetVisibleRowCountForTests(), 128);
		SearchBrowser->SetSearchTextForTests(FString());
		TestEqual(TEXT("clearing the search restores every card"),
			SearchBrowser->GetVisibleRowCountForTests(), 128);
	}

	if (!FSlateApplication::IsInitialized())
	{
		return true;
	}

	FScopedCombatThumbnailFixture ThumbnailFixture;
	FText FixtureError;
	if (!TestTrue(TEXT("real thumbnail packages save and unload"),
		ThumbnailFixture.BuildAndUnload(FixtureError)))
	{
		AddError(FixtureError.ToString());
		return false;
	}
	TestNull(TEXT("visible thumbnail starts unloaded"),
		ThumbnailFixture.VisiblePaths[0].ResolveObject());
	TestNull(TEXT("off-screen thumbnail starts unloaded"),
		ThumbnailFixture.OffscreenPaths[0].ResolveObject());

	UPaper2DPlusCombatProfileAsset* ThumbnailCombat = CombatEditor_MakeThumbnailAsset(
		ThumbnailFixture.VisiblePaths,
		ThumbnailFixture.OffscreenPaths,
		128);
	{
		TSharedRef<FCombatProfileEditorSession> ThumbnailSession =
			MakeShared<FCombatProfileEditorSession>(ThumbnailCombat);
		TSharedRef<SCombatAttackBrowserPanel> Browser =
			SNew(SCombatAttackBrowserPanel).Session(ThumbnailSession);
		TestEqual(TEXT("card source contains every large-fixture attack"),
			Browser->GetRowCountForTests(), 128);
		TestNull(TEXT("128-row Catalog derivation leaves the first visible flipbook unloaded"),
			ThumbnailFixture.VisiblePaths[0].ResolveObject());
		TestNull(TEXT("128-row Catalog derivation leaves off-screen flipbooks unloaded"),
			ThumbnailFixture.OffscreenPaths[0].ResolveObject());
		TestEqual(TEXT("catalog projection alone requests no thumbnails"),
			Browser->GetRetainedThumbnailCountForTests(), 0);
		const int32 GeneratedCards = Browser->GenerateCardsForViewportForTests(
			FVector2D(260.0f, 180.0f));
		TestTrue(TEXT("constrained attack browser generates visible cards"), GeneratedCards > 0);
		TestTrue(TEXT("constrained attack browser virtualizes most cards"), GeneratedCards < 128);
		TestTrue(TEXT("visible card requests its unloaded art asynchronously"),
			Browser->WasThumbnailRequestedForTests(ThumbnailFixture.VisiblePaths[0]));
		TestFalse(TEXT("off-screen card starts no thumbnail request"),
			Browser->WasThumbnailRequestedForTests(ThumbnailFixture.OffscreenPaths[0]));
		TestNull(TEXT("off-screen package remains unloaded"),
			ThumbnailFixture.OffscreenPaths[0].ResolveObject());

		FFlipbookProfileEntry TimingPolicyEntry;
		TimingPolicyEntry.Identity.FlipbookName = TEXT("TimingPolicy");
		TimingPolicyEntry.Identity.Flipbook =
			TSoftObjectPtr<UPaperFlipbook>(ThumbnailFixture.LruPaths[49]);
		TimingPolicyEntry.CombatData.Frames.SetNum(3);
		const FPaper2DPlusMoveFrameData ResidentOnlyTiming =
			FPaper2DPlusFrameData::ComputeMoveFrameDataForEntry(
				ThumbnailCombat->CharacterProfile,
				TimingPolicyEntry,
				false);
		TestEqual(TEXT("resident-only timing uses authored-frame fallback"),
			ResidentOnlyTiming.TotalKeyFrames, 3);
		TestNull(TEXT("resident-only timing policy performs no synchronous load"),
			ThumbnailFixture.LruPaths[49].ResolveObject());
		const FPaper2DPlusMoveFrameData DefaultTiming =
			FPaper2DPlusFrameData::ComputeMoveFrameDataForEntry(
				ThumbnailCombat->CharacterProfile,
				TimingPolicyEntry);
		TestNotNull(TEXT("default explicit frame-data policy retains synchronous timing lookup"),
			ThumbnailFixture.LruPaths[49].ResolveObject());
		TestEqual(TEXT("default timing reads the saved empty flipbook's true key-frame count"),
			DefaultTiming.TotalKeyFrames, 0);

		Browser->ResetThumbnailsForTests();
		for (int32 Index = 0; Index < 48; ++Index)
		{
			TestTrue(TEXT("saved LRU thumbnail enters the production async path"),
				Browser->RequestThumbnailForTests(ThumbnailFixture.LruPaths[Index]));
		}
		TestEqual(TEXT("Attack card retention reaches its documented cap"),
			Browser->GetRetainedThumbnailCountForTests(), 48);
		TestTrue(TEXT("re-request touches the first retained thumbnail"),
			Browser->RequestThumbnailForTests(ThumbnailFixture.LruPaths[0]));
		TestTrue(TEXT("new thumbnail enters the capped cache"),
			Browser->RequestThumbnailForTests(ThumbnailFixture.LruPaths[48]));
		TestEqual(TEXT("Attack card thumbnail cache never exceeds 48"),
			Browser->GetRetainedThumbnailCountForTests(), 48);
		TestTrue(TEXT("recently touched thumbnail survives true-LRU eviction"),
			Browser->IsThumbnailRetainedForTests(ThumbnailFixture.LruPaths[0]));
		TestFalse(TEXT("least-recently-used thumbnail is evicted"),
			Browser->IsThumbnailRetainedForTests(ThumbnailFixture.LruPaths[1]));
		TestTrue(TEXT("newest thumbnail is retained"),
			Browser->IsThumbnailRetainedForTests(ThumbnailFixture.LruPaths[48]));
		Browser->ResetThumbnailsForTests();
		FlushAsyncLoading();
	}

	FText CleanupError;
	TestTrue(TEXT("Combat thumbnail packages unload and clean up"),
		ThumbnailFixture.Cleanup(CleanupError));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCombatEditorGenerateMissingTest,
	"Paper2DPlus.Editor.CombatProfile.Session.GenerateOnlyMissingAndPreserveCanonicalIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCombatEditorGenerateMissingTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusCombatProfileAsset* Combat = CombatEditor_MakeAsset();
	FPaper2DPlusCombatAttackOption Existing;
	Existing.MoveName = TEXT("Jab");
	Existing.MoveFlipbook = Combat->CharacterProfile->Flipbooks[0].Identity.Flipbook;
	Existing.BaseWeight = 4.0f;
	Combat->AttackOptions.Add(Existing);
	TSharedRef<FCombatProfileEditorSession> Session = MakeShared<FCombatProfileEditorSession>(Combat);

	TestEqual(TEXT("Only the missing move is generated"), Session->GenerateMissingAttackOptions(), 1);
	TestEqual(TEXT("One row per move remains"), Combat->AttackOptions.Num(), 2);
	TestEqual(TEXT("Existing tuning is preserved"), Combat->AttackOptions[0].BaseWeight, 4.0f);
	TestEqual(TEXT("A second generate is a no-op"), Session->GenerateMissingAttackOptions(), 0);
	TestTrue(TEXT("Generated row stores canonical flipbook identity"),
		Combat->AttackOptions.ContainsByPredicate([](const FPaper2DPlusCombatAttackOption& Option)
		{
			return Option.MoveName == TEXT("Sweep") && !Option.MoveFlipbook.IsNull();
		}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCombatEditorProvenanceTest,
	"Paper2DPlus.Editor.CombatProfile.Session.EffectiveRuleProvenance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCombatEditorProvenanceTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusCombatProfileAsset* Combat = CombatEditor_MakeAsset();
	FPaper2DPlusCombatTagDefaults Defaults;
	Defaults.AttackTag = CombatEditor_AttackTag();
	Defaults.BaseWeight = 2.0f;
	Defaults.Considerations.AddDefaulted();
	Combat->TagDefaults.Add(Defaults);
	Combat->GenerateAttackOptionsFromCharacterProfile(true);
	Combat->AttackOptions[0].BaseWeight = 3.0f;
	Combat->AttackOptions[0].Considerations.AddDefaulted();

	TSharedRef<FCombatProfileEditorSession> Session = MakeShared<FCombatProfileEditorSession>(Combat);
	TestTrue(TEXT("Jab selection succeeds"), Session->SelectAttackByName(TEXT("Jab")));
	const FCombatProfileEffectiveAttackSummary Summary = Session->GetSelectedEffectiveSummary();
	TestTrue(TEXT("Move-specific scope is reported"), Summary.bHasMoveTuning);
	TestTrue(TEXT("Tag-default scope is reported"), Summary.bHasTagDefaults);
	TestEqual(TEXT("Move consideration count is visible"), Summary.MoveConsiderationCount, 1);
	TestEqual(TEXT("Tag consideration count is visible"), Summary.TagConsiderationCount, 1);
	TestEqual(TEXT("Effective base weight is the move value"), Summary.BaseWeight, 3.0f);
	TestEqual(TEXT("Provenance explicitly names move override"),
		Summary.GetBaseWeightProvenanceText().ToString(), FString(TEXT("Move override")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCombatEditorGuidedCollectionsTest,
	"Paper2DPlus.Editor.CombatProfile.GuidedCollections.TransactionIdentityAndVariablePreservation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCombatEditorGuidedCollectionsTest::RunTest(const FString& Parameters)
{
	if (GEditor) GEditor->ResetTransaction(FText::FromString(TEXT("Reset Combat collection transactions")));
	UPaper2DPlusCombatProfileAsset* Combat = CombatEditor_MakeAsset();
	TSharedRef<FCombatProfileEditorSession> Session = MakeShared<FCombatProfileEditorSession>(Combat);

	TSharedRef<SCombatProfileCollectionPanel> Variables =
		SNew(SCombatProfileCollectionPanel).Session(Session).Collection(ECombatProfileCollectionKind::Variables);
	const int32 VariableIndex = Variables->AddRowForTests();
	TestEqual(TEXT("Guided Add creates one variable row"), VariableIndex, 0);
	TestTrue(TEXT("New variable row has a stable panel identity"),
		Variables->SelectIdentityForTests(Variables->GetIdentityAtIndexForTests(VariableIndex)));

	FPaper2DPlusCombatVariableDefinition Definition = Combat->VariableDefinitions[VariableIndex];
	const FGameplayTag AggressionTag = FGameplayTag::RequestGameplayTag(
		FName(TEXT("Paper2DPlus.Combat.Var.Aggression")), false);
	Definition.VariableTag = AggressionTag;
	Definition.DisplayName = FText::FromString(TEXT("Aggression"));
	TestTrue(TEXT("Guided struct edit commits"), Variables->ApplyRowForTests(&Definition));
	TestTrue(TEXT("Variable rebuild materializes every scope value"),
		Combat->GlobalVariables.Contains(AggressionTag));
	Combat->GlobalVariables.FindChecked(AggressionTag).FloatValue = 0.75f;

	const FGameplayTag Replacement = FGameplayTag::RequestGameplayTag(
		FName(TEXT("Paper2DPlus.Combat.Var.CanPunish")), false);
	TestTrue(TEXT("Replacement test tag is registered"), Replacement.IsValid());
	if (Replacement.IsValid())
	{
		Definition.VariableTag = Replacement;
		TestTrue(TEXT("Tag rename commits through guided panel"), Variables->ApplyRowForTests(&Definition));
		TestFalse(TEXT("Old variable key is removed"), Combat->GlobalVariables.Contains(AggressionTag));
		TestEqual(TEXT("Compatible authored value survives tag rename"),
			Combat->GlobalVariables.FindChecked(Replacement).FloatValue, 0.75f);
	}

	TSharedRef<SCombatProfileCollectionPanel> Profiles =
		SNew(SCombatProfileCollectionPanel).Session(Session).Collection(ECombatProfileCollectionKind::ScoringProfiles);
	const int32 FirstProfile = Profiles->AddRowForTests();
	Profiles->AddRowForTests();
	TestTrue(TEXT("Named profile selection resolves independently of array pointers"),
		Profiles->SelectIdentityForTests(Profiles->GetIdentityAtIndexForTests(FirstProfile)));
	TestTrue(TEXT("Reorder follows the selected named profile"), Profiles->MoveSelectedForTests(1));
	TestTrue(TEXT("Remove acts on the selected identity after reorder"), Profiles->RemoveSelectedForTests());
	TestEqual(TEXT("One scoring profile remains"), Combat->ScoringProfiles.Num(), 1);

	TestTrue(TEXT("One Undo restores the removed guided row"), GEditor && GEditor->UndoTransaction(true));
	Profiles->RefreshFromAsset();
	TestEqual(TEXT("Undo restores both scoring profiles"), Combat->ScoringProfiles.Num(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCombatEditorCollectionFallbackIdentityTest,
	"Paper2DPlus.Editor.CombatProfile.GuidedCollections.FallbackAndDuplicateIdentityReorderSafety",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCombatEditorCollectionFallbackIdentityTest::RunTest(const FString& Parameters)
{
	if (GEditor) GEditor->ResetTransaction(FText::FromString(TEXT("Reset Combat fallback identity transactions")));
	UPaper2DPlusCombatProfileAsset* Combat = CombatEditor_MakeAsset();
	Combat->TagDefaults.SetNum(3);
	Combat->TagDefaults[0].BaseWeight = 11.0f;
	Combat->TagDefaults[1].BaseWeight = 22.0f;
	Combat->TagDefaults[2].BaseWeight = 33.0f;
	TSharedRef<FCombatProfileEditorSession> Session = MakeShared<FCombatProfileEditorSession>(Combat);
	TSharedRef<SCombatProfileCollectionPanel> Defaults =
		SNew(SCombatProfileCollectionPanel).Session(Session).Collection(ECombatProfileCollectionKind::TagDefaults);

	TestTrue(TEXT("Unset middle row can be selected through its fallback identity"),
		Defaults->SelectIdentityForTests(Defaults->GetIdentityAtIndexForTests(1)));
	TestTrue(TEXT("Move up keeps selection on the exact unset row"), Defaults->MoveSelectedForTests(-1));
	TestEqual(TEXT("Moved unset row reaches index zero"), Defaults->ResolveSelectedIndexForTests(), 0);
	TestEqual(TEXT("Move up preserved the selected row's authored value"), Combat->TagDefaults[0].BaseWeight, 22.0f);
	TestEqual(TEXT("Selection identity is rebound to the fallback at its new index"),
		Defaults->GetSelectedIdentityForTests(), Defaults->GetIdentityAtIndexForTests(0));

	TestTrue(TEXT("Move down keeps selection on the exact unset row"), Defaults->MoveSelectedForTests(1));
	TestEqual(TEXT("Moved unset row returns to index one"), Defaults->ResolveSelectedIndexForTests(), 1);
	TestEqual(TEXT("Move down preserved the selected row's authored value"), Combat->TagDefaults[1].BaseWeight, 22.0f);
	TestTrue(TEXT("Remove targets the moved unset row"), Defaults->RemoveSelectedForTests());
	TestEqual(TEXT("Only the two untouched unset rows remain"), Combat->TagDefaults.Num(), 2);
	TestEqual(TEXT("First untouched row remains"), Combat->TagDefaults[0].BaseWeight, 11.0f);
	TestEqual(TEXT("Last untouched row remains"), Combat->TagDefaults[1].BaseWeight, 33.0f);

	const FGameplayTag DuplicateTag = CombatEditor_AttackTag();
	TestTrue(TEXT("Duplicate-identity fixture tag is registered"), DuplicateTag.IsValid());
	Combat->TagDefaults.SetNum(2);
	Combat->TagDefaults[0].AttackTag = DuplicateTag;
	Combat->TagDefaults[0].BaseWeight = 41.0f;
	Combat->TagDefaults[1].AttackTag = DuplicateTag;
	Combat->TagDefaults[1].BaseWeight = 42.0f;
	Defaults->RefreshFromAsset();
	TestFalse(TEXT("Duplicate authored identities are deliberately not addressable"),
		Defaults->SelectIdentityForTests(DuplicateTag.ToString()));
	TestFalse(TEXT("Ambiguous duplicate identity cannot reorder either row"), Defaults->MoveSelectedForTests(1));
	TestEqual(TEXT("First duplicate row remains untouched"), Combat->TagDefaults[0].BaseWeight, 41.0f);
	TestEqual(TEXT("Second duplicate row remains untouched"), Combat->TagDefaults[1].BaseWeight, 42.0f);

	if (GEditor) GEditor->ResetTransaction(FText::FromString(TEXT("End Combat fallback identity transactions")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCombatEditorSetupWorkspaceTest,
	"Paper2DPlus.Editor.CombatProfile.GuidedWorkspace.SetupStatusAndDecomposedLayout",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCombatEditorSetupWorkspaceTest::RunTest(const FString& Parameters)
{
	if (GEditor) GEditor->ResetTransaction(FText::FromString(TEXT("Reset Combat setup transactions")));
	UPaper2DPlusCombatProfileAsset* Blank = NewObject<UPaper2DPlusCombatProfileAsset>(
		GetTransientPackage(), NAME_None, RF_Transactional);
	TSharedRef<FCombatProfileEditorSession> Session = MakeShared<FCombatProfileEditorSession>(Blank);
	TSharedRef<SCombatProfileSetupPanel> Setup = SNew(SCombatProfileSetupPanel).Session(Session);
	TestTrue(TEXT("Blank setup explains the required relationship"), Setup->GetStatusText().ToString().Contains(TEXT("Link a Character Profile")));

	const FGameplayTag VariableTag = FGameplayTag::RequestGameplayTag(
		FName(TEXT("Paper2DPlus.Combat.Var.Aggression")), false);
	FPaper2DPlusCombatVariableDefinition Definition;
	Definition.VariableTag = VariableTag;
	Definition.Type = EPaper2DPlusCombatVariableType::Float;
	Blank->VariableDefinitions.Add(Definition);
	Blank->RebuildVariableBags();
	Blank->GlobalVariables.FindChecked(VariableTag).FloatValue = 0.6f;
	Blank->TagDefaults.AddDefaulted_GetRef().Variables.FindOrAdd(VariableTag).FloatValue = 0.6f;
	Blank->VariableOverrideSchemaVersion = 0;
	TestEqual(TEXT("guided review removes the behavior-neutral legacy snapshot"),
		Setup->AdoptSparseVariableOverridesForTests(), 1);
	TestFalse(TEXT("guided review adopts sparse inheritance"), Blank->HasLegacyDenseVariableOverrides());
	TestEqual(TEXT("guided review advances the exact schema version"),
		Blank->VariableOverrideSchemaVersion,
		UPaper2DPlusCombatProfileAsset::CurrentVariableOverrideSchemaVersion);
	TestTrue(TEXT("guided review is one undoable mutation"), GEditor && GEditor->UndoTransaction(true));
	TestTrue(TEXT("Undo restores the legacy review state"), Blank->HasLegacyDenseVariableOverrides());
	TestEqual(TEXT("Undo restores the exact legacy schema sentinel"),
		Blank->VariableOverrideSchemaVersion, static_cast<uint8>(0));
	TestEqual(TEXT("Undo restores the preserved dense row"), Blank->TagDefaults[0].Variables.Num(), 1);
	TestTrue(TEXT("Redo reapplies the guided review"), GEditor && GEditor->RedoTransaction());
	TestFalse(TEXT("Redo restores sparse inheritance"), Blank->HasLegacyDenseVariableOverrides());
	TestEqual(TEXT("Redo restores the exact current schema version"),
		Blank->VariableOverrideSchemaVersion,
		UPaper2DPlusCombatProfileAsset::CurrentVariableOverrideSchemaVersion);
	TestTrue(TEXT("Redo removes the behavior-neutral dense row again"),
		Blank->TagDefaults[0].Variables.IsEmpty());

	UPaper2DPlusCombatProfileAsset* Fixture = CombatEditor_MakeAsset();
	Setup->SetCharacterProfileForTests(Fixture->CharacterProfile);
	TestTrue(TEXT("Linked setup reports attack counts and the advisory boundary in plain language"),
		Setup->GetStatusText().ToString().Contains(TEXT("attacks from"))
		&& Setup->GetStatusText().ToString().Contains(TEXT("never plays")));

	TestTrue(TEXT("Combat workspace layout adopts the Character workspace grammar"),
		FCombatProfileAssetEditorToolkit::WorkspaceLayoutId.ToString().Contains(TEXT("Layout_v9")));
	const FString CombatLayout = FCombatProfileAssetEditorToolkit::BuildDefaultLayout()->ToString();
	TestTrue(TEXT("Combat layout contains Completion"),
		CombatLayout.Contains(FCombatProfileAssetEditorToolkit::CompletionTabId.ToString()));
	TestTrue(TEXT("Combat layout contains Related Profiles"),
		CombatLayout.Contains(FCombatProfileAssetEditorToolkit::RelatedProfilesTabId.ToString()));
	TestTrue(TEXT("Combat layout contains the contextual attack Details panel"),
		CombatLayout.Contains(FCombatProfileAssetEditorToolkit::AttackDetailsTabId.ToString()));
	TestTrue(TEXT("Score Playground is a central tool sibling of Overview"),
		CombatLayout.Contains(FCombatProfileAssetEditorToolkit::ScorePreviewTabId.ToString()));
	TestTrue(TEXT("Combat Lab is a central tool sibling of Overview"),
		CombatLayout.Contains(FCombatProfileAssetEditorToolkit::CombatLabTabId.ToString()));
	TestFalse(TEXT("The retired Attack Catalog tab never returns to the layout"),
		CombatLayout.Contains(TEXT("AttackCatalog")));
	// Overview must open foreground; the tools stack ahead of the right-hand Details/Completion column.
	TestTrue(TEXT("Overview is the foreground central tool"),
		CombatLayout.Contains(FCombatProfileAssetEditorToolkit::SetupTabId.ToString()));
	TestTrue(TEXT("Central tools precede the contextual Details panel in the layout"),
		CombatLayout.Find(FCombatProfileAssetEditorToolkit::SetupTabId.ToString())
			< CombatLayout.Find(FCombatProfileAssetEditorToolkit::AttackDetailsTabId.ToString()));
	TestFalse(TEXT("Variables tab has a stable id"), FCombatProfileAssetEditorToolkit::VariablesTabId.IsNone());
	TestFalse(TEXT("Tag Defaults tab has a stable id"), FCombatProfileAssetEditorToolkit::DefaultsTabId.IsNone());
	TestFalse(TEXT("Scoring Profiles tab has a stable id"), FCombatProfileAssetEditorToolkit::ScoringProfilesTabId.IsNone());
	TestFalse(TEXT("Scenario Presets tab has a stable id"), FCombatProfileAssetEditorToolkit::PresetsTabId.IsNone());
	TestFalse(TEXT("Combat Lab tab has a stable id"), FCombatProfileAssetEditorToolkit::CombatLabTabId.IsNone());

	FFlipbookProfileEntry DeferredPreview;
	DeferredPreview.Identity.FlipbookName = TEXT("DeferredPreview");
	const FSoftObjectPath DeferredPreviewPath(
		TEXT("/__Paper2DPlusMissing__/Combat/FB_DeferredPreview.FB_DeferredPreview"));
	DeferredPreview.Identity.Flipbook = TSoftObjectPtr<UPaperFlipbook>(DeferredPreviewPath);
	Fixture->CharacterProfile->Flipbooks.Add(DeferredPreview);
	const TSoftObjectPtr<UPaperFlipbook> SoftPreview =
		SCombatAttackBrowserPanel::FindAttackFlipbookSoftRef(
			Fixture,
			TEXT("DeferredPreview"));
	TestEqual(TEXT("Attack-card lookup preserves the authored soft path"),
		SoftPreview.ToSoftObjectPath(), DeferredPreviewPath);
	TestNull(TEXT("Attack-card lookup never synchronously loads an unloaded preview"), SoftPreview.Get());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCombatEditorAttackInspectorTest,
	"Paper2DPlus.Editor.CombatProfile.GuidedWorkspace.AttackInspectorCustomizesWithoutFlatteningInheritance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCombatEditorAttackInspectorTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusCombatProfileAsset* Combat = CombatEditor_MakeAsset();
	FPaper2DPlusCombatTagDefaults Defaults;
	Defaults.AttackTag = CombatEditor_AttackTag();
	Defaults.BaseWeight = 2.5f;
	Defaults.Considerations.AddDefaulted();
	Combat->TagDefaults.Add(Defaults);

	TSharedRef<FCombatProfileEditorSession> Session = MakeShared<FCombatProfileEditorSession>(Combat);
	TestTrue(TEXT("Jab can be selected from the derived catalog"), Session->SelectAttackByName(TEXT("Jab")));
	TSharedRef<SCombatAttackInspectorPanel> Inspector =
		SNew(SCombatAttackInspectorPanel).Session(Session);
	TestTrue(TEXT("Customize creates the explicit move tuning row"), Inspector->CustomizeForTests());
	TestEqual(TEXT("Exactly one move row is created"), Combat->AttackOptions.Num(), 1);
	TestEqual(TEXT("Inherited tag considerations are not copied and double-counted"),
		Combat->AttackOptions[0].Considerations.Num(), 0);
	TestEqual(TEXT("Tag provenance remains visible after customization"),
		Session->GetSelectedEffectiveSummary().TagConsiderationCount, 1);

	FPaper2DPlusCombatAttackOption Edited = Combat->AttackOptions[0];
	Edited.BaseWeight = 7.0f;
	Edited.MoveName = TEXT("DoNotRelink");
	Edited.MoveFlipbook.Reset();
	TestTrue(TEXT("Inspector commits tuning"), Inspector->ApplyOptionForTests(&Edited));
	TestEqual(TEXT("Tuning value is committed"), Combat->AttackOptions[0].BaseWeight, 7.0f);
	TestEqual(TEXT("Guided inspector preserves canonical move name"), Combat->AttackOptions[0].MoveName, FName(TEXT("Jab")));
	TestFalse(TEXT("Guided inspector preserves canonical flipbook identity"), Combat->AttackOptions[0].MoveFlipbook.IsNull());

	// The "+ Scoring Rule" templates append exactly one move-level rule (auto-customizing first if needed).
	TestTrue(TEXT("Template menu appends one move-level rule"),
		Inspector->AddConsiderationTemplateForTests(1));
	TestEqual(TEXT("Move rule count reflects the appended template"),
		Session->GetSelectedEffectiveSummary().MoveConsiderationCount, 1);
	TestTrue(TEXT("Provenance reads as customized in plain language"),
		Inspector->GetProvenanceTextForTests().ToString().Contains(TEXT("Customized")));

	// Revert removes the tuning row; tag provenance keeps applying because it was never copied.
	TestTrue(TEXT("Revert removes the move tuning row"), Inspector->RevertToInheritedForTests());
	TestEqual(TEXT("No move rows remain after revert"), Combat->AttackOptions.Num(), 0);
	TestEqual(TEXT("Tag provenance still applies after revert"),
		Session->GetSelectedEffectiveSummary().TagConsiderationCount, 1);
	TestTrue(TEXT("Provenance returns to inherited defaults in plain language"),
		Inspector->GetProvenanceTextForTests().ToString().Contains(TEXT("tag defaults")));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
