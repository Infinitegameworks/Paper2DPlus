// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "CharacterCatalogAssetEditorToolkit.h"
#include "CharacterCatalogAssetFactory.h"
#include "CharacterCatalogDetailsPanel.h"
#include "CharacterCatalogEditorModel.h"
#include "CharacterCatalogRosterPanel.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Editor.h"
#include "EditorCanvasUtils.h"
#include "Engine/Texture2D.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/FileManager.h"
#include "Misc/App.h"
#include "Misc/Guid.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "PackageTools.h"
#include "PaperFlipbook.h"
#include "PaperFlipbookComponent.h"
#include "PaperSprite.h"
#include "Paper2DPlusAnimationTags.h"
#include "Paper2DPlusAuthoringProgressTags.h"
#include "Paper2DPlusBlueprintLibrary.h"
#include "Paper2DPlusCharacterLayerAsset.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Paper2DPlusCharacterProfileComponent.h"
#include "Paper2DPlusCombatProfileAsset.h"
#include "Paper2DPlusEffectProfileAsset.h"
#include "Paper2DPlusSettings.h"
#include "Paper2DPlusTestSkip.h"
#include "Paper2DPlusValidationService.h"
#include "ProfileRelationshipService.h"
#include "ProfileValidationPanel.h"
#include "RelatedProfileBar.h"
#include "Tickable.h"
#include "GameFramework/Actor.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/UObjectGlobals.h"

namespace Paper2DPlusCharacterCatalogEditorTests
{
	/** Real two-package fixture for the visible Profile -> flipbook async-load chain. */
	struct FScopedCatalogThumbnailFixture
	{
		FString Directory;
		FString ProfilePackageName;
		FString FlipbookPackageName;
		FString ProfileFilePath;
		FString FlipbookFilePath;
		FName ProfileAssetName = TEXT("CatalogThumbnailProfile");
		FName FlipbookAssetName = TEXT("CatalogThumbnailFlipbook");
		FName SpriteAssetName = TEXT("CatalogThumbnailSprite");
		FName TextureAssetName = TEXT("CatalogThumbnailTexture");

		FScopedCatalogThumbnailFixture()
		{
			const FGuid Guid = FGuid::NewGuid();
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
			const FString GuidString = Guid.ToString(EGuidFormats::Digits).ToLower();
#else
			const FString GuidString = Guid.ToString(EGuidFormats::DigitsLower);
#endif
			Directory = FString::Printf(TEXT("/Game/__AutomationTemp__/P2DPCatalogThumbnail_%s"), *GuidString);
			ProfilePackageName = Directory / TEXT("Profile");
			FlipbookPackageName = Directory / TEXT("Flipbook");
			ProfileFilePath = FPackageName::LongPackageNameToFilename(
				ProfilePackageName, FPackageName::GetAssetPackageExtension());
			FlipbookFilePath = FPackageName::LongPackageNameToFilename(
				FlipbookPackageName, FPackageName::GetAssetPackageExtension());
			IFileManager::Get().MakeDirectory(*FPaths::GetPath(ProfileFilePath), true);
		}

		~FScopedCatalogThumbnailFixture()
		{
			FText Ignored;
			Cleanup(Ignored);
		}

		FSoftObjectPath ProfilePath() const
		{
			return FSoftObjectPath(FString::Printf(
				TEXT("%s.%s"), *ProfilePackageName, *ProfileAssetName.ToString()));
		}

		FSoftObjectPath FlipbookPath() const
		{
			return FSoftObjectPath(FString::Printf(
				TEXT("%s.%s"), *FlipbookPackageName, *FlipbookAssetName.ToString()));
		}

		bool Unload(FText& OutError) const
		{
			TArray<UPackage*> Packages;
			for (const FString& PackageName : { ProfilePackageName, FlipbookPackageName })
			{
				if (UPackage* Package = FindPackage(nullptr, *PackageName))
				{
					if (Package->IsRooted())
					{
						Package->RemoveFromRoot();
					}
					Package->SetDirtyFlag(false);
					Packages.Add(Package);
				}
			}
			return Packages.IsEmpty() || UPackageTools::UnloadPackages(Packages, OutError, true);
		}

		bool Cleanup(FText& OutError) const
		{
			const bool bUnloaded = Unload(OutError);
			CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
			const FString SafeAutomationRoot = FPaths::ConvertRelativePathToFull(
				FPaths::ProjectContentDir() / TEXT("__AutomationTemp__"));
			const FString FixtureDirectory = FPaths::ConvertRelativePathToFull(
				FPaths::GetPath(ProfileFilePath));
			const bool bSafelyBounded =
				Directory.StartsWith(TEXT("/Game/__AutomationTemp__/P2DPCatalogThumbnail_"))
				&& ProfilePackageName.StartsWith(Directory + TEXT("/"))
				&& FlipbookPackageName.StartsWith(Directory + TEXT("/"))
				&& FPaths::GetPath(ProfileFilePath).Equals(
					FPaths::GetPath(FlipbookFilePath), ESearchCase::IgnoreCase)
				&& FPaths::IsUnderDirectory(FixtureDirectory, SafeAutomationRoot);
			if (!bSafelyBounded)
			{
				OutError = FText::FromString(FString::Printf(
					TEXT("Refused unsafe Catalog thumbnail fixture cleanup outside %s: %s"),
					*SafeAutomationRoot, *FixtureDirectory));
				return false;
			}

			IFileManager& FileManager = IFileManager::Get();
			bool bFilesGone = true;
			for (const FString& FilePath : { ProfileFilePath, FlipbookFilePath })
			{
				for (const FString& PackageFile : {
					FilePath,
					FPaths::ChangeExtension(FilePath, TEXT("uexp")),
					FPaths::ChangeExtension(FilePath, TEXT("ubulk")),
					FPaths::ChangeExtension(FilePath, TEXT("uptnl")) })
				{
					const bool bDeleteSucceeded = !FileManager.FileExists(*PackageFile)
						|| FileManager.Delete(*PackageFile, false, true, true);
					bFilesGone &= bDeleteSucceeded && !FileManager.FileExists(*PackageFile);
				}
			}
			const bool bDirectoryDeleteSucceeded = !FileManager.DirectoryExists(*FixtureDirectory)
				|| FileManager.DeleteDirectory(*FixtureDirectory, false, false);
			const bool bDirectoryGone = bDirectoryDeleteSucceeded
				&& !FileManager.DirectoryExists(*FixtureDirectory);
			const bool bPackagesGone = FindPackage(nullptr, *ProfilePackageName) == nullptr
				&& FindPackage(nullptr, *FlipbookPackageName) == nullptr;
			const bool bSucceeded = bUnloaded && bPackagesGone && bFilesGone && bDirectoryGone;
			if (!bSucceeded && OutError.IsEmpty())
			{
				OutError = FText::FromString(FString::Printf(
					TEXT("Catalog thumbnail cleanup failed: unloaded=%s packagesGone=%s filesGone=%s directoryGone=%s"),
					bUnloaded ? TEXT("true") : TEXT("false"),
					bPackagesGone ? TEXT("true") : TEXT("false"),
					bFilesGone ? TEXT("true") : TEXT("false"),
					bDirectoryGone ? TEXT("true") : TEXT("false")));
			}
			return bSucceeded;
		}
	};

	template <typename AssetType>
	AssetType* NewAsset(const TCHAR* PackageStem)
	{
		static int32 Sequence = 0;
		const FString PackageName = FString::Printf(TEXT("%s_Run%d"), PackageStem, ++Sequence);
		UPackage* Package = CreatePackage(*PackageName);
		const FString AssetName = FPackageName::GetLongPackageAssetName(PackageName);
		AssetType* Asset = NewObject<AssetType>(Package, *AssetName,
			RF_Public | RF_Standalone | RF_Transactional);
		Package->SetDirtyFlag(false);
		return Asset;
	}

	FAssetData MakeAssetData(
		const UObject* Asset,
		bool bRelationshipTag = false,
		const FSoftObjectPath& CharacterPath = FSoftObjectPath())
	{
		const FString PackageName = Asset->GetOutermost()->GetName();
		FAssetDataTagMap Tags;
		if (bRelationshipTag)
		{
			Tags.Add(FProfileRelationshipService::CharacterProfileRelationshipTag(),
				CharacterPath.IsNull() ? FString(TEXT("None")) : CharacterPath.ToString());
		}
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
		return FAssetData(FName(*PackageName), FName(*FPackageName::GetLongPackagePath(PackageName)),
			Asset->GetFName(), Asset->GetClass()->GetFName(), MoveTemp(Tags));
#else
		return FAssetData(FName(*PackageName), FName(*FPackageName::GetLongPackagePath(PackageName)),
			Asset->GetFName(), Asset->GetClass()->GetClassPathName(), MoveTemp(Tags));
#endif
	}

	FPaper2DPlusCharacterCatalogEntry MakeEntry(UPaper2DPlusCharacterProfileAsset* Character)
	{
		FPaper2DPlusCharacterCatalogEntry Entry;
		Entry.CharacterProfile = Character;
		return Entry;
	}

	TSharedPtr<FCharacterCatalogEditorModel> MakeModel(
		UPaper2DPlusCharacterCatalogAsset* Catalog,
		const TSharedRef<TArray<FAssetData>>& Assets,
		const TSharedRef<FPaper2DPlusCharacterCatalogSettingsSnapshot>& Settings,
		const FCharacterCatalogAuditService::FNativeAssetResolver& Resolver = {},
		const FCharacterCatalogAuditService::FCookRegistrationInspector& CookInspector = {})
	{
		TSharedPtr<FCharacterCatalogEditorModel> Model = MakeShared<FCharacterCatalogEditorModel>();
		Model->Initialize(
			Catalog,
			[Assets](TArray<FAssetData>& OutAssets) { OutAssets = *Assets; },
			[Settings]() { return *Settings; },
			Resolver,
			CookInspector);
		return Model;
	}

	FPaper2DPlusCharacterCatalogCookInspection CookReady(const FSoftObjectPath&)
	{
		FPaper2DPlusCharacterCatalogCookInspection Inspection;
		Inspection.bTypeRegistered = true;
		Inspection.bCatalogPathRegistered = true;
		return Inspection;
	}

	/** Visible-row identities in the exact order the grid would paint them. */
	TArray<FSoftObjectPath> CatalogRail_VisibleOrder(
		const TSharedPtr<FCharacterCatalogEditorModel>& Model)
	{
		TArray<FSoftObjectPath> Order;
		for (const TSharedPtr<FPaper2DPlusCharacterCatalogEditorRow>& Row : Model->GetVisibleRows())
		{
			Order.Add(Row.IsValid() ? Row->CharacterPath : FSoftObjectPath());
		}
		return Order;
	}

	/** A Character Profile with one animation per supplied completion bitmask. */
	UPaper2DPlusCharacterProfileAsset* CatalogProgress_MakeCharacter(
		const TCHAR* PackageStem,
		const TArray<int32>& PerAnimationCompletionFlags)
	{
		UPaper2DPlusCharacterProfileAsset* Character =
			NewAsset<UPaper2DPlusCharacterProfileAsset>(PackageStem);
		for (int32 Index = 0; Index < PerAnimationCompletionFlags.Num(); ++Index)
		{
			FFlipbookProfileEntry& Animation = Character->Flipbooks.AddDefaulted_GetRef();
			Animation.Identity.FlipbookName = FString::Printf(TEXT("Anim_%02d"), Index);
			Animation.EditorMeta.CompletionFlags = PerAnimationCompletionFlags[Index];
		}
		return Character;
	}

	/**
	 * Registry metadata carrying exactly the supplied progress tag values.
	 *
	 * A nullptr omits that tag, which is how an asset that has not been resaved since the progress
	 * tags shipped looks to the roster. The values are strings on purpose: FAssetData stores tag
	 * values as text and silently Atoi's a malformed one, which is the case the pair validator exists
	 * to reject.
	 */
	FAssetData CatalogProgress_MakeProgressAssetData(
		const FString& PackageName,
		FName AssetName,
		const UClass* AssetClass,
		const TCHAR* DoneTagValue,
		const TCHAR* TotalTagValue)
	{
		FAssetDataTagMap Tags;
		if (DoneTagValue)
		{
			Tags.Add(Paper2DPlusAuthoringProgress::DoneTag(), DoneTagValue);
		}
		if (TotalTagValue)
		{
			Tags.Add(Paper2DPlusAuthoringProgress::TotalTag(), TotalTagValue);
		}
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
		return FAssetData(FName(*PackageName), FName(*FPackageName::GetLongPackagePath(PackageName)),
			AssetName, AssetClass->GetFName(), MoveTemp(Tags));
#else
		return FAssetData(FName(*PackageName), FName(*FPackageName::GetLongPackagePath(PackageName)),
			AssetName, AssetClass->GetClassPathName(), MoveTemp(Tags));
#endif
	}

	/** Deliberately hand-written tags for an asset that IS resident, so residency can be proven to win. */
	FAssetData CatalogProgress_MakeResidentProgressAssetData(
		const UObject* Asset,
		const TCHAR* DoneTagValue,
		const TCHAR* TotalTagValue)
	{
		return CatalogProgress_MakeProgressAssetData(
			Asset->GetOutermost()->GetName(),
			Asset->GetFName(),
			Asset->GetClass(),
			DoneTagValue,
			TotalTagValue);
	}

	/** A Character row whose asset is NOT resident: the roster may only read its registry tags. */
	struct FCatalogProgressUnloadedCharacter
	{
		FSoftObjectPath Path;
		FAssetData AssetData;
	};

	FCatalogProgressUnloadedCharacter CatalogProgress_MakeUnloadedCharacter(
		const FString& PackageName,
		const TCHAR* DoneTagValue,
		const TCHAR* TotalTagValue)
	{
		FCatalogProgressUnloadedCharacter Fixture;
		const FString AssetName = FPackageName::GetLongPackageAssetName(PackageName);
		Fixture.Path = FSoftObjectPath(FString::Printf(TEXT("%s.%s"), *PackageName, *AssetName));
		Fixture.AssetData = CatalogProgress_MakeProgressAssetData(
			PackageName,
			FName(*AssetName),
			UPaper2DPlusCharacterProfileAsset::StaticClass(),
			DoneTagValue,
			TotalTagValue);
		return Fixture;
	}

	/**
	 * Production registry tags for a live asset.
	 *
	 * The parameter is deliberately a const UObject*: every Paper2DPlus asset DECLARES its own
	 * GetAssetRegistryTags override, which name-hides UObject's FAssetData convenience overload on the
	 * derived type. Looking the call up through UObject keeps one call site compiling on 5.0-5.8 while
	 * still virtual-dispatching into the asset's real override.
	 */
	FAssetData CatalogProgress_MakeExportedAssetData(const UObject* Asset)
	{
		FAssetData AssetData = MakeAssetData(Asset);
		Asset->GetAssetRegistryTags(AssetData);
		return AssetData;
	}

	/** One row's expected projected progress, so the malformed-pair matrix can stay table-driven. */
	struct FCatalogProgressExpectation
	{
		FSoftObjectPath Path;
		FString What;
		bool bKnown = false;
		int32 Done = 0;
		int32 Total = 0;
		int32 SourcesMissingData = 0;
	};
}

namespace Paper2DPlusCharacterCatalogEditorTests
{

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCatalogEditorFactoryTest,
	"Paper2DPlus.CharacterCatalog.Editor.FactoryCreatesEmptyAndExplainsSettings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPaper2DPlusCatalogEditorFactoryTest::RunTest(const FString& Parameters)
{
	UCharacterCatalogAssetFactory* Factory = NewObject<UCharacterCatalogAssetFactory>();
	UPackage* Package = CreatePackage(TEXT("/Game/Paper2DPlusTests/U28/FactoryCatalog"));
	UPaper2DPlusCharacterCatalogAsset* Catalog = Cast<UPaper2DPlusCharacterCatalogAsset>(
		Factory->FactoryCreateNew(UPaper2DPlusCharacterCatalogAsset::StaticClass(), Package,
			TEXT("FactoryCatalog"), RF_Public | RF_Standalone | RF_Transactional, nullptr, GWarn));
	TestNotNull(TEXT("Factory creates a Character Catalog"), Catalog);
	if (!Catalog) return false;
	TestEqual(TEXT("Factory never guesses roster entries"), Catalog->Entries.Num(), 0);
	TestEqual(TEXT("Factory never guesses groups"), Catalog->Groups.Num(), 0);

	TSharedRef<TArray<FAssetData>> Assets = MakeShared<TArray<FAssetData>>();
	TSharedRef<FPaper2DPlusCharacterCatalogSettingsSnapshot> Settings =
		MakeShared<FPaper2DPlusCharacterCatalogSettingsSnapshot>();
	TSharedPtr<FCharacterCatalogEditorModel> Model = MakeModel(Catalog, Assets, Settings);
	TestFalse(TEXT("New Catalog is not silently made authoritative"), Model->IsAuthoritative());
	TestTrue(TEXT("Banner names the explicit action that makes this asset the project authority"),
		Model->GetAuthorityStatusText().ToString().Contains(
			TEXT("Make Project Catalog"), ESearchCase::IgnoreCase));
	Model->Shutdown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCatalogEditorAuthorityTest,
	"Paper2DPlus.CharacterCatalog.Editor.AuthorityGatesAuditAndBindsSourcesWithoutDirtyingCatalog",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPaper2DPlusCatalogEditorAuthorityTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterCatalogAsset* Catalog = NewAsset<UPaper2DPlusCharacterCatalogAsset>(
		TEXT("/Game/Paper2DPlusTests/U28/AuthorityCatalog"));
	TSharedRef<TArray<FAssetData>> Assets = MakeShared<TArray<FAssetData>>();
	TSharedRef<FPaper2DPlusCharacterCatalogSettingsSnapshot> Settings =
		MakeShared<FPaper2DPlusCharacterCatalogSettingsSnapshot>();
	TSharedPtr<FCharacterCatalogEditorModel> Model = MakeModel(Catalog, Assets, Settings);
	// Sources are bound for every opened Catalog, authoritative or not: registry events only
	// re-project saved rows (missing-asset state, renames), so binding them is not a discovery gate.
	TestEqual(TEXT("Initialize binds the four registry delegates plus the settings delegate"),
		Model->GetSourceDelegateCountForTests(), 5);
	TestFalse(TEXT("Audit disabled before authority"), Model->CanAudit());
	TestFalse(TEXT("Non-authority copy avoids internal discovery terminology"),
		Model->GetAuthorityStatusText().ToString().Contains(TEXT("discovery"), ESearchCase::IgnoreCase));
	TestTrue(TEXT("Non-authority copy names the action that fixes it"),
		Model->GetAuthorityStatusText().ToString().Contains(
			TEXT("Make Project Catalog"), ESearchCase::IgnoreCase));

	bool bSettingsOnlyWrite = false;
	Model->SetAuthorityActionForTests([Settings, &bSettingsOnlyWrite](UPaper2DPlusCharacterCatalogAsset* InCatalog, FText& OutMessage)
	{
		bSettingsOnlyWrite = true;
		Settings->DefaultCatalog = InCatalog;
		OutMessage = FText::FromString(TEXT("Test project settings updated"));
		return true;
	});
	FText Message;
	TestTrue(TEXT("Set authority succeeds"), Model->SetAsProjectCatalog(Message));
	TestTrue(TEXT("Only settings writer was invoked"), bSettingsOnlyWrite);
	TestTrue(TEXT("Model becomes authoritative"), Model->IsAuthoritative());
	TestTrue(TEXT("Audit unlocks with authority"), Model->CanAudit());
	TestTrue(TEXT("Authority copy explains the designer-facing intake action"),
		Model->GetAuthorityStatusText().ToString().Contains(TEXT("Add Characters"), ESearchCase::IgnoreCase));
	TestFalse(TEXT("Authority copy avoids aggregate-audit terminology"),
		Model->GetAuthorityStatusText().ToString().Contains(TEXT("aggregate audit"), ESearchCase::IgnoreCase));
	TestEqual(TEXT("Authority designation neither adds nor drops source delegates"),
		Model->GetSourceDelegateCountForTests(), 5);
	TestFalse(TEXT("Authority designation never dirties the Catalog"), Catalog->GetOutermost()->IsDirty());
	Model->Shutdown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCatalogEditorFilteringTest,
	"Paper2DPlus.CharacterCatalog.Editor.SavedRosterCombinedFiltersPreserveSelectionAndCleanliness",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPaper2DPlusCatalogEditorFilteringTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterCatalogAsset* Catalog = NewAsset<UPaper2DPlusCharacterCatalogAsset>(
		TEXT("/Game/Paper2DPlusTests/U28/FilterCatalog"));
	UPaper2DPlusCharacterProfileAsset* Hero = NewAsset<UPaper2DPlusCharacterProfileAsset>(
		TEXT("/Game/Characters/U28_FilterHero"));
	Hero->DisplayName = TEXT("Readable Hero");
	// The second SAVED row is what makes every filter below discriminating: it shares the roster but
	// matches no search token, tag, group, or requirement, so a filter that silently stopped applying
	// would leave two visible rows instead of one.
	UPaper2DPlusCharacterProfileAsset* Bystander = NewAsset<UPaper2DPlusCharacterProfileAsset>(
		TEXT("/Game/Characters/U28_FilterBystander"));
	UPaper2DPlusCharacterLayerAsset* Layer = NewAsset<UPaper2DPlusCharacterLayerAsset>(
		TEXT("/Game/Characters/U28_FilterHeroLayers"));
	Layer->BaseProfile = Hero;
	// The completion filter now means REAL authoring progress, so Hero has to actually be finished for
	// "Complete" to keep it. Both assets are resident here, so the model reads these live rather than
	// from registry tags. Bystander stays unauthored, which is what keeps this filter discriminating.
	FFlipbookProfileEntry& HeroAnimation = Hero->Flipbooks.AddDefaulted_GetRef();
	HeroAnimation.Identity.FlipbookName = TEXT("FilterHeroIdle");
	HeroAnimation.EditorMeta.CompletionFlags = UPaper2DPlusCharacterProfileAsset::LiveTaskBits;
#if WITH_EDITORONLY_DATA
	Layer->EditorCompletionFlags = Paper2DPlusAuthoringProgress::ProfileChecklistCriteriaMask;
#endif
	FPaper2DPlusCharacterCatalogEntry Entry = MakeEntry(Hero);
	Entry.LayerProfile = Layer;
	Entry.Requirements.bRequireLayer = true;
	Entry.Tags.AddTag(Paper2DPlusAnimationTags::Combat_Heavy);
	Catalog->Entries.Add(Entry);
	Catalog->Entries.Add(MakeEntry(Bystander));
	FPaper2DPlusCharacterCatalogGroup& Group = Catalog->Groups.AddDefaulted_GetRef();
	Group.GroupName = TEXT("Heroes");
	Group.DisplayName = FText::FromString(TEXT("Heroes"));
	Group.Members.Add(Hero);
	Catalog->GetOutermost()->SetDirtyFlag(false);

	TSharedRef<TArray<FAssetData>> Assets = MakeShared<TArray<FAssetData>>();
	Assets->Add(MakeAssetData(Bystander));
	Assets->Add(MakeAssetData(Layer, true, FSoftObjectPath(Hero)));
	Assets->Add(MakeAssetData(Hero));
	TSharedRef<FPaper2DPlusCharacterCatalogSettingsSnapshot> Settings =
		MakeShared<FPaper2DPlusCharacterCatalogSettingsSnapshot>();
	Settings->DefaultCatalog = Catalog;
	TSharedPtr<FCharacterCatalogEditorModel> Model = MakeModel(Catalog, Assets, Settings);
	// Every index and row deref below is guarded: an out-of-bounds or null deref inside an automation
	// test aborts the entire process, so a wrong count must fail THIS test rather than the whole suite.
	if (TestEqual(TEXT("Rows project the saved roster only"), Model->GetRows().Num(), 2))
	{
		TestEqual(TEXT("Saved rows keep authored Entries order"),
			Model->GetRows()[0]->CharacterPath, FSoftObjectPath(Hero));
		TestEqual(TEXT("Authored order is not re-sorted by path"),
			Model->GetRows()[1]->CharacterPath, FSoftObjectPath(Bystander));
	}
	const TSharedPtr<FPaper2DPlusCharacterCatalogEditorRow> HeroRow = Model->FindRow(FSoftObjectPath(Hero));
	const TSharedPtr<FPaper2DPlusCharacterCatalogEditorRow> BystanderRow =
		Model->FindRow(FSoftObjectPath(Bystander));
	if (TestTrue(TEXT("Both saved characters project a row"), HeroRow.IsValid() && BystanderRow.IsValid()))
	{
		TestEqual(TEXT("Resident Profiles use their authored designer-facing label"),
			HeroRow->DisplayName.ToString(), FString(TEXT("Readable Hero")));
		TestFalse(TEXT("A registry-backed saved row is not flagged missing"), BystanderRow->bMissingAsset);
		TestFalse(TEXT("Default Profile placeholder never becomes every card's identity"),
			BystanderRow->DisplayName.ToString().Equals(
				TEXT("New Character Profile"), ESearchCase::IgnoreCase));
	}
	Model->SelectCharacter(FSoftObjectPath(Hero));
	Model->SetSearchText(TEXT("FilterHero"));
	Model->SetTagFilter(Paper2DPlusAnimationTags::Combat_Heavy);
	Model->SetGroupFilter(TEXT("Heroes"));
	Model->SetRequirementFilter(EPaper2DPlusCatalogRequirementFilter::Layer);
	Model->SetCompletionFilter(EPaper2DPlusCatalogCompletionFilter::Complete);
	Model->SetSeverityFilter(EPaper2DPlusCatalogSeverityFilter::Clean);
	// Guarded: indexing [0] after a failed count assertion turns one test failure into an out-of-bounds
	// assert that aborts the whole automation process, taking every other test's result with it.
	if (TestEqual(TEXT("Combined filters isolate the intended row"), Model->GetVisibleRows().Num(), 1))
	{
		TestEqual(TEXT("Combined filters keep the intended row, not an arbitrary survivor"),
			Model->GetVisibleRows()[0]->CharacterPath, FSoftObjectPath(Hero));
	}
	if (HeroRow.IsValid() && BystanderRow.IsValid())
	{
		TestTrue(TEXT("The intended row is fully authored, so Complete is a real filter"),
			HeroRow->AuthoringProgress.IsFullyAuthored());
		TestFalse(TEXT("The unauthored row is not fully authored"),
			BystanderRow->AuthoringProgress.IsFullyAuthored());
	}
	Model->RefreshFromSources();
	TestEqual(TEXT("Refresh preserves path-keyed selection"), Model->GetSelectedCharacterPath(), FSoftObjectPath(Hero));
	TestFalse(TEXT("Project/filter/select/refresh stay read-only"), Catalog->GetOutermost()->IsDirty());
	Model->Shutdown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCatalogEditorSoftThumbnailTest,
	"Paper2DPlus.CharacterCatalog.Editor.UnloadedProfilesKeepDesignerIdentityAndUseVisibleAsyncArt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPaper2DPlusCatalogEditorSoftThumbnailTest::RunTest(const FString& Parameters)
{
	FScopedCatalogThumbnailFixture Fixture;
	UPackage* FlipbookPackage = CreatePackage(*Fixture.FlipbookPackageName);
	UPackage* ProfilePackage = CreatePackage(*Fixture.ProfilePackageName);
	if (!TestNotNull(TEXT("Temporary flipbook package is created"), FlipbookPackage)
		|| !TestNotNull(TEXT("Temporary Profile package is created"), ProfilePackage))
	{
		return false;
	}
	FlipbookPackage->AddToRoot();
	ProfilePackage->AddToRoot();
	UPaperFlipbook* SavedFlipbook = NewObject<UPaperFlipbook>(
		FlipbookPackage,
		Fixture.FlipbookAssetName,
		RF_Public | RF_Standalone | RF_Transactional);
	UTexture2D* SavedTexture = NewObject<UTexture2D>(
		FlipbookPackage,
		Fixture.TextureAssetName,
		RF_Public | RF_Standalone | RF_Transactional);
	SavedTexture->SetPlatformData(new FTexturePlatformData());
	SavedTexture->GetPlatformData()->SizeX = 16;
	SavedTexture->GetPlatformData()->SizeY = 16;
	SavedTexture->GetPlatformData()->PixelFormat = PF_B8G8R8A8;
	FTexture2DMipMap* SavedMip = new FTexture2DMipMap();
	SavedTexture->GetPlatformData()->Mips.Add(SavedMip);
	SavedMip->SizeX = 16;
	SavedMip->SizeY = 16;
	SavedMip->BulkData.Lock(LOCK_READ_WRITE);
	uint8* SavedMipPixels = static_cast<uint8*>(SavedMip->BulkData.Realloc(16 * 16 * 4));
	for (int32 PixelIndex = 0; PixelIndex < 16 * 16; ++PixelIndex)
	{
		const int32 ByteIndex = PixelIndex * 4;
		SavedMipPixels[ByteIndex + 0] = 220; // B
		SavedMipPixels[ByteIndex + 1] = 110; // G
		SavedMipPixels[ByteIndex + 2] = 35;  // R
		SavedMipPixels[ByteIndex + 3] = 255; // A
	}
	SavedMip->BulkData.Unlock();
	SavedTexture->Source.Init(16, 16, 1, 1, TSF_BGRA8);
	{
		uint8* Pixels = SavedTexture->Source.LockMip(0);
		const uint8* MipPixels = static_cast<const uint8*>(SavedMip->BulkData.Lock(LOCK_READ_ONLY));
		FMemory::Memcpy(Pixels, MipPixels, 16 * 16 * 4);
		SavedMip->BulkData.Unlock();
		SavedTexture->Source.UnlockMip(0);
	}
	SavedTexture->CompressionSettings = TC_EditorIcon;
	SavedTexture->Filter = TF_Nearest;
	SavedTexture->MipGenSettings = TMGS_NoMipmaps;
	SavedTexture->LODGroup = TEXTUREGROUP_Pixels2D;
	SavedTexture->NeverStream = true;
	SavedTexture->UpdateResource();
	SavedTexture->PostEditChange();
	UPaperSprite* SavedSprite = NewObject<UPaperSprite>(
		FlipbookPackage,
		Fixture.SpriteAssetName,
		RF_Public | RF_Standalone | RF_Transactional);
	FSpriteAssetInitParameters SpriteInit;
	SpriteInit.Texture = SavedTexture;
	SpriteInit.Offset = FIntPoint::ZeroValue;
	SpriteInit.Dimension = FIntPoint(16, 16);
	SpriteInit.SetPixelsPerUnrealUnit(1.0f);
	SavedSprite->InitializeSprite(SpriteInit);
	SavedSprite->PostEditChange();
	{
		FScopedFlipbookMutator Mutator(SavedFlipbook);
		Mutator.FramesPerSecond = 12.0f;
		FPaperFlipbookKeyFrame& KeyFrame = Mutator.KeyFrames.AddDefaulted_GetRef();
		KeyFrame.Sprite = SavedSprite;
		KeyFrame.FrameRun = 1;
	}
	SavedFlipbook->PostEditChange();
	{
		// Keep this widget strictly inside the authoring assertion. SFlipbookThumbnail owns a
		// TStrongObjectPtr, so retaining it through PackageTools::UnloadPackages would make the
		// disk-roundtrip fixture falsely remain resident.
		TSharedRef<SFlipbookThumbnail> AuthoredThumbnail =
			SNew(SFlipbookThumbnail).Flipbook(SavedFlipbook);
		TestTrue(TEXT("Authored disk fixture has a renderable texture before save"),
			AuthoredThumbnail->HasTextureForTests());
	}
	UPaper2DPlusCharacterProfileAsset* SavedProfile = NewObject<UPaper2DPlusCharacterProfileAsset>(
		ProfilePackage,
		Fixture.ProfileAssetName,
		RF_Public | RF_Standalone | RF_Transactional);
	SavedProfile->DisplayName = TEXT("Unloaded Designer Hero");
	SavedProfile->ThumbnailFlipbookName = TEXT("Idle");
	FFlipbookProfileEntry& SavedAnimation = SavedProfile->Flipbooks.AddDefaulted_GetRef();
	SavedAnimation.Identity.FlipbookName = TEXT("Idle");
	SavedAnimation.Identity.Flipbook = SavedFlipbook;
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_NoError;
	const bool bFlipbookSaved = UPackage::SavePackage(
		FlipbookPackage, SavedFlipbook, *Fixture.FlipbookFilePath, SaveArgs);
	const bool bProfileSaved = UPackage::SavePackage(
		ProfilePackage, SavedProfile, *Fixture.ProfileFilePath, SaveArgs);
	FlipbookPackage->RemoveFromRoot();
	ProfilePackage->RemoveFromRoot();
	if (!TestTrue(TEXT("Temporary flipbook saves to disk"), bFlipbookSaved)
		|| !TestTrue(TEXT("Temporary Profile saves to disk"), bProfileSaved))
	{
		return false;
	}

	FText InitialUnloadError;
	if (!TestTrue(TEXT("Saved Profile and flipbook truly unload before the Catalog opens"),
		Fixture.Unload(InitialUnloadError)))
	{
		AddError(InitialUnloadError.ToString());
		return false;
	}
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
	const FSoftObjectPath UnloadedPath = Fixture.ProfilePath();
	const FSoftObjectPath UnloadedFlipbookPath = Fixture.FlipbookPath();
	TestNull(TEXT("Saved Profile starts unresolved"), UnloadedPath.ResolveObject());
	TestNull(TEXT("Saved thumbnail flipbook starts unresolved"), UnloadedFlipbookPath.ResolveObject());

	UPaper2DPlusCharacterCatalogAsset* Catalog = NewAsset<UPaper2DPlusCharacterCatalogAsset>(
		TEXT("/Game/Paper2DPlusTests/U28/SoftThumbnailCatalog"));

	FPaper2DPlusCharacterCatalogEntry UnloadedEntry;
	UnloadedEntry.CharacterProfile = TSoftObjectPtr<UPaper2DPlusCharacterProfileAsset>(UnloadedPath);
	Catalog->Entries.Add(UnloadedEntry);

	FAssetDataTagMap UnloadedTags;
	UnloadedTags.Add(FName(TEXT("Paper2DPlus.CharacterDisplayName")), TEXT("Unloaded Designer Hero"));
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
	FAssetData UnloadedAssetData(
		FName(*Fixture.ProfilePackageName),
		FName(*FPackageName::GetLongPackagePath(Fixture.ProfilePackageName)),
		Fixture.ProfileAssetName,
		UPaper2DPlusCharacterProfileAsset::StaticClass()->GetFName(),
		MoveTemp(UnloadedTags));
#else
	FAssetData UnloadedAssetData(
		FName(*Fixture.ProfilePackageName),
		FName(*FPackageName::GetLongPackagePath(Fixture.ProfilePackageName)),
		Fixture.ProfileAssetName,
		UPaper2DPlusCharacterProfileAsset::StaticClass()->GetClassPathName(),
		MoveTemp(UnloadedTags));
#endif

	// Resident/transient Profiles keep the direct flipbook preview used by editor fixtures and unsaved
	// authoring, rather than being downgraded to a generic asset thumbnail.
	UPaper2DPlusCharacterProfileAsset* ResidentCharacter = NewAsset<UPaper2DPlusCharacterProfileAsset>(
		TEXT("/Game/Characters/U28_ResidentThumbnailHero"));
	ResidentCharacter->DisplayName = TEXT("Resident Designer Hero");
	UPaperFlipbook* ResidentFlipbook = NewAsset<UPaperFlipbook>(
		TEXT("/Game/Characters/U28_ResidentThumbnailFlipbook"));
	FFlipbookProfileEntry& Animation = ResidentCharacter->Flipbooks.AddDefaulted_GetRef();
	Animation.Identity.FlipbookName = TEXT("Idle");
	Animation.Identity.Flipbook = ResidentFlipbook;
	Catalog->Entries.Add(MakeEntry(ResidentCharacter));

	TSharedRef<TArray<FAssetData>> Assets = MakeShared<TArray<FAssetData>>();
	Assets->Add(UnloadedAssetData);
	Assets->Add(MakeAssetData(ResidentCharacter));
	TSharedRef<FPaper2DPlusCharacterCatalogSettingsSnapshot> Settings =
		MakeShared<FPaper2DPlusCharacterCatalogSettingsSnapshot>();
	Settings->DefaultCatalog = Catalog;
	TSharedPtr<FCharacterCatalogEditorModel> Model = MakeModel(Catalog, Assets, Settings);

	const TSharedPtr<FPaper2DPlusCharacterCatalogEditorRow> UnloadedRow = Model->FindRow(UnloadedPath);
	TestTrue(TEXT("Unloaded saved row is projected"), UnloadedRow.IsValid());
	if (UnloadedRow.IsValid())
	{
		TestTrue(TEXT("Unloaded row retains valid registry thumbnail metadata"),
			UnloadedRow->CharacterAssetData.IsValid());
		TestFalse(TEXT("Unloaded row does not gain a resident Profile"),
			UnloadedRow->ResidentCharacterProfile.IsValid());
		TestEqual(TEXT("Registry display identity avoids generic path labels"),
			UnloadedRow->DisplayName.ToString(), FString(TEXT("Unloaded Designer Hero")));
	}
	TestNull(TEXT("Model projection performs no synchronous Profile load"), UnloadedPath.ResolveObject());

	if (FSlateApplication::IsInitialized())
	{
		// Keep the unloaded row outside the synthetic viewport first: tile generation, rather than model
		// projection or panel construction, is the async-load boundary.
		Model->SetSearchText(TEXT("Resident Designer Hero"));
		{
			TSharedRef<SCharacterCatalogRosterPanel> Panel =
				SNew(SCharacterCatalogRosterPanel).Model(Model);
			TestEqual(TEXT("Unloaded saved Profile uses the visible-row async art path"),
				Panel->GetThumbnailSourceForTests(UnloadedPath), FName(TEXT("AsyncProfile")));
			TestEqual(TEXT("Resident authoring fixture preserves direct flipbook preview"),
				Panel->GetThumbnailSourceForTests(FSoftObjectPath(ResidentCharacter)),
				FName(TEXT("ResidentFlipbook")));
			TestTrue(TEXT("A constrained Catalog viewport generates its visible card"),
				Panel->GenerateTilesForViewportForTests(FVector2D(320.0f, 220.0f)) > 0);
			TestFalse(TEXT("An off-screen unloaded Profile starts no async thumbnail request"),
				Panel->WasAsyncThumbnailRequestedForTests(UnloadedPath));
			TestNull(TEXT("Building unrelated thumbnail widgets does not synchronously load the Profile"),
				UnloadedPath.ResolveObject());

			Model->SetSearchText(TEXT("Unloaded Designer Hero"));
			TestTrue(TEXT("Generating the unloaded visible card starts its async request"),
				Panel->GenerateTilesForViewportForTests(FVector2D(320.0f, 220.0f)) > 0
				&& Panel->WasAsyncThumbnailRequestedForTests(UnloadedPath));
			for (int32 Pump = 0; Pump < 4
				&& !Panel->HasLoadedAsyncThumbnailObjectsForTests(UnloadedPath);
				++Pump)
			{
				FlushAsyncLoading();
				FTickableGameObject::TickObjects(nullptr, LEVELTICK_All, false, 0.0f);
				Panel->GenerateTilesForViewportForTests(FVector2D(320.0f, 220.0f));
			}
			TestTrue(TEXT("Profile and flipbook callbacks retain both loaded thumbnail objects"),
				Panel->HasLoadedAsyncThumbnailObjectsForTests(UnloadedPath));
			TestEqual(TEXT("Completed async chain regenerates a real Character-style flipbook thumbnail"),
				Panel->GetThumbnailSourceForTests(UnloadedPath), FName(TEXT("ResidentFlipbook")));
			TestNotNull(TEXT("Visible async chain loads the saved Profile"), UnloadedPath.ResolveObject());
			UPaperFlipbook* LoadedFlipbook = Cast<UPaperFlipbook>(UnloadedFlipbookPath.ResolveObject());
			if (TestNotNull(TEXT("Profile callback loads the saved thumbnail flipbook"), LoadedFlipbook))
			{
				UPaperSprite* LoadedSprite = LoadedFlipbook->GetNumKeyFrames() > 0
					? LoadedFlipbook->GetKeyFrameChecked(0).Sprite.Get()
					: nullptr;
				UTexture2D* LoadedTexture = LoadedSprite ? LoadedSprite->GetBakedTexture() : nullptr;
				if (TestNotNull(TEXT("Reloaded thumbnail frame retains its Paper Sprite"), LoadedSprite)
					&& TestNotNull(TEXT("Reloaded Paper Sprite retains its baked texture"), LoadedTexture))
				{
					// Saved editor textures can finish DDC/platform caching after their UObject is resident.
					// Complete that normal readiness boundary before evaluating the same retry seam used by
					// SFlipbookThumbnail's short initial active timer.
					LoadedTexture->FinishCachePlatformData();
					LoadedTexture->UpdateResource();
				}
				TSharedRef<SFlipbookThumbnail> LoadedThumbnail =
					SNew(SFlipbookThumbnail).Flipbook(LoadedFlipbook);
				TestTrue(TEXT("Reloaded thumbnail refresh resolves its ready sprite texture"),
					LoadedThumbnail->RefreshFromLiveFlipbookForTests());
				TestTrue(TEXT("Loaded Catalog art produces a renderable thumbnail texture"),
					LoadedThumbnail->HasTextureForTests());
				TestNotNull(TEXT("Loaded Catalog thumbnail brush owns a real texture resource"),
					LoadedThumbnail->GetBrushForTests().GetResourceObject());
			}
		}
	}

	Model->Shutdown();
	FText CleanupError;
	TestTrue(TEXT("Async thumbnail fixture unloads and deletes both packages"), Fixture.Cleanup(CleanupError));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCharacterProfileLookupResidencyTest,
	"Paper2DPlus.CharacterProfile.Runtime.ObjectLookupKeepsUnrelatedFlipbooksCold",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPaper2DPlusCharacterProfileLookupResidencyTest::RunTest(const FString& Parameters)
{
	FScopedCatalogThumbnailFixture Fixture;
	UPackage* FlipbookPackage = CreatePackage(*Fixture.FlipbookPackageName);
	if (!TestNotNull(TEXT("Temporary cold-flipbook package is created"), FlipbookPackage))
	{
		return false;
	}

	FlipbookPackage->AddToRoot();
	UPaperFlipbook* ColdFlipbook = NewObject<UPaperFlipbook>(
		FlipbookPackage,
		Fixture.FlipbookAssetName,
		RF_Public | RF_Standalone | RF_Transactional);
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_NoError;
	const bool bFlipbookSaved = UPackage::SavePackage(
		FlipbookPackage, ColdFlipbook, *Fixture.FlipbookFilePath, SaveArgs);
	FlipbookPackage->RemoveFromRoot();
	if (!TestTrue(TEXT("Temporary cold flipbook saves to disk"), bFlipbookSaved))
	{
		return false;
	}

	FText InitialUnloadError;
	if (!TestTrue(TEXT("Saved cold flipbook truly unloads before profile lookup"),
		Fixture.Unload(InitialUnloadError)))
	{
		AddError(InitialUnloadError.ToString());
		return false;
	}
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);

	const FSoftObjectPath ColdFlipbookPath = Fixture.FlipbookPath();
	TestNull(TEXT("Unrelated flipbook starts non-resident"), ColdFlipbookPath.ResolveObject());

	UPaper2DPlusCharacterProfileAsset* Profile = NewObject<UPaper2DPlusCharacterProfileAsset>();
	UPaperFlipbook* ResidentFlipbook = NewObject<UPaperFlipbook>(Profile);
	FFlipbookProfileEntry& ResidentEntry = Profile->Flipbooks.AddDefaulted_GetRef();
	ResidentEntry.Identity.FlipbookName = TEXT("Resident");
	ResidentEntry.Identity.Flipbook = ResidentFlipbook;
	FFlipbookProfileEntry& ColdEntry = Profile->Flipbooks.AddDefaulted_GetRef();
	ColdEntry.Identity.FlipbookName = TEXT("Cold");
	ColdEntry.Identity.Flipbook = TSoftObjectPtr<UPaperFlipbook>(ColdFlipbookPath);

	TestTrue(TEXT("Name-based metadata lookup finds the cold entry without loading its art"),
		Profile->FindFlipbookDataPtr(TEXT("Cold")) == &ColdEntry);
	TestNull(TEXT("Name-based metadata lookup leaves the unrelated flipbook cold"),
		ColdFlipbookPath.ResolveObject());

	AActor* Owner = NewObject<AActor>();
	UPaperFlipbookComponent* FlipbookComponent = NewObject<UPaperFlipbookComponent>(Owner);
	UPaper2DPlusCharacterProfileComponent* ProfileComponent =
		NewObject<UPaper2DPlusCharacterProfileComponent>(Owner);
	FlipbookComponent->SetFlipbook(ResidentFlipbook);
	ProfileComponent->FlipbookComponent = FlipbookComponent;
	ProfileComponent->SetCharacterProfile(Profile);
	TestEqual(TEXT("Normal component profile warm resolves the resident authored move"),
		ProfileComponent->GetCurrentMoveName(), FString(TEXT("Resident")));
	TestNull(TEXT("Normal component profile warm does not load an unrelated soft flipbook"),
		ColdFlipbookPath.ResolveObject());
	TestNull(TEXT("Unrelated profile entry remains unresolved after component warm"),
		ColdEntry.Identity.Flipbook.Get());

	TestEqual(TEXT("Repeated cached object lookup preserves the authored move name"),
		Profile->GetFlipbookName(ResidentFlipbook), FString(TEXT("Resident")));
	TestNull(TEXT("Repeated cached lookup still leaves the unrelated flipbook cold"),
		ColdFlipbookPath.ResolveObject());

	Profile->Flipbooks.Swap(0, 1);
	TestEqual(TEXT("A same-count reorder repairs the stale lookup indexes without a load"),
		Profile->GetFlipbookName(ResidentFlipbook), FString(TEXT("Resident")));
	TestNull(TEXT("Stale-index repair still leaves the unrelated flipbook cold"),
		ColdFlipbookPath.ResolveObject());

	UPaperFlipbook* ExplicitlyLoadedFlipbook = Profile->GetFlipbookByName(TEXT("Cold"));
	if (TestNotNull(TEXT("Explicit name-to-object lookup loads its one requested flipbook"),
		ExplicitlyLoadedFlipbook))
	{
		const FFlipbookProfileEntry* LoadedColdEntry = Profile->FindFlipbookDataPtr(TEXT("Cold"));
		TestTrue(TEXT("A loaded packaged flipbook resolves through the same path cache"),
			Profile->FindByFlipbookPtr(ExplicitlyLoadedFlipbook) == LoadedColdEntry);
	}

	FText CleanupError;
	TestTrue(TEXT("Cold-flipbook fixture unloads and deletes its package"),
		Fixture.Cleanup(CleanupError));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCatalogEditorThumbnailChoiceTest,
	"Paper2DPlus.CharacterCatalog.Editor.ThumbnailChoiceNeverSkipsItsAuthoredOrFirstSoftEntry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPaper2DPlusCatalogEditorThumbnailChoiceTest::RunTest(const FString& Parameters)
{
	if (!FSlateApplication::IsInitialized())
	{
		return true;
	}
	UPaper2DPlusCharacterCatalogAsset* Catalog = NewAsset<UPaper2DPlusCharacterCatalogAsset>(
		TEXT("/Game/Paper2DPlusTests/U28/ThumbnailChoiceCatalog"));
	UPaper2DPlusCharacterProfileAsset* AuthoredChoice = NewAsset<UPaper2DPlusCharacterProfileAsset>(
		TEXT("/Game/Characters/U28_ThumbnailAuthoredChoice"));
	UPaperFlipbook* AuthoredOtherResident = NewAsset<UPaperFlipbook>(
		TEXT("/Game/Characters/U28_ThumbnailAuthoredOtherResident"));
	const FSoftObjectPath AuthoredChosenUnloadedPath(
		TEXT("/Game/Characters/U28_ThumbnailAuthoredChosenUnloaded.U28_ThumbnailAuthoredChosenUnloaded"));
	AuthoredChoice->ThumbnailFlipbookName = TEXT("Chosen");
	FFlipbookProfileEntry& AuthoredOther = AuthoredChoice->Flipbooks.AddDefaulted_GetRef();
	AuthoredOther.Identity.FlipbookName = TEXT("Other");
	AuthoredOther.Identity.Flipbook = AuthoredOtherResident;
	FFlipbookProfileEntry& AuthoredChosen = AuthoredChoice->Flipbooks.AddDefaulted_GetRef();
	AuthoredChosen.Identity.FlipbookName = TEXT("Chosen");
	AuthoredChosen.Identity.Flipbook = TSoftObjectPtr<UPaperFlipbook>(AuthoredChosenUnloadedPath);
	Catalog->Entries.Add(MakeEntry(AuthoredChoice));

	UPaper2DPlusCharacterProfileAsset* FirstChoice = NewAsset<UPaper2DPlusCharacterProfileAsset>(
		TEXT("/Game/Characters/U28_ThumbnailFirstChoice"));
	UPaperFlipbook* SecondResident = NewAsset<UPaperFlipbook>(
		TEXT("/Game/Characters/U28_ThumbnailSecondResident"));
	const FSoftObjectPath FirstUnloadedPath(
		TEXT("/Game/Characters/U28_ThumbnailFirstUnloaded.U28_ThumbnailFirstUnloaded"));
	FFlipbookProfileEntry& FirstUnloaded = FirstChoice->Flipbooks.AddDefaulted_GetRef();
	FirstUnloaded.Identity.FlipbookName = TEXT("First");
	FirstUnloaded.Identity.Flipbook = TSoftObjectPtr<UPaperFlipbook>(FirstUnloadedPath);
	FFlipbookProfileEntry& SecondLoaded = FirstChoice->Flipbooks.AddDefaulted_GetRef();
	SecondLoaded.Identity.FlipbookName = TEXT("Second");
	SecondLoaded.Identity.Flipbook = SecondResident;
	Catalog->Entries.Add(MakeEntry(FirstChoice));

	TSharedRef<TArray<FAssetData>> Assets = MakeShared<TArray<FAssetData>>();
	Assets->Add(MakeAssetData(AuthoredChoice));
	Assets->Add(MakeAssetData(FirstChoice));
	TSharedRef<FPaper2DPlusCharacterCatalogSettingsSnapshot> Settings =
		MakeShared<FPaper2DPlusCharacterCatalogSettingsSnapshot>();
	Settings->DefaultCatalog = Catalog;
	TSharedPtr<FCharacterCatalogEditorModel> Model = MakeModel(Catalog, Assets, Settings);
	{
		TSharedRef<SCharacterCatalogRosterPanel> Panel =
			SNew(SCharacterCatalogRosterPanel).Model(Model);
		TestEqual(TEXT("Authored thumbnail identity wins even while its soft flipbook is unloaded"),
			Panel->GetDesiredThumbnailFlipbookPathForTests(FSoftObjectPath(AuthoredChoice)),
			AuthoredChosenUnloadedPath);
		TestEqual(TEXT("Another resident flipbook never replaces the authored thumbnail choice"),
			Panel->GetThumbnailSourceForTests(FSoftObjectPath(AuthoredChoice)),
			FName(TEXT("AsyncProfile")));
		TestEqual(TEXT("Without an authored name, the first non-null soft entry remains the choice"),
			Panel->GetDesiredThumbnailFlipbookPathForTests(FSoftObjectPath(FirstChoice)),
			FirstUnloadedPath);
		TestEqual(TEXT("A later resident flipbook never skips the unloaded first entry"),
			Panel->GetThumbnailSourceForTests(FSoftObjectPath(FirstChoice)),
			FName(TEXT("AsyncProfile")));
	}
	Model->Shutdown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCatalogEditorThumbnailLruTest,
	"Paper2DPlus.CharacterCatalog.Editor.VisibleThumbnailRetentionIsBoundedTrueLru",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPaper2DPlusCatalogEditorThumbnailLruTest::RunTest(const FString& Parameters)
{
	if (!FSlateApplication::IsInitialized())
	{
		return true;
	}

	UPaper2DPlusCharacterCatalogAsset* Catalog = NewAsset<UPaper2DPlusCharacterCatalogAsset>(
		TEXT("/Game/Paper2DPlusTests/U28/ThumbnailLruCatalog"));
	TSharedRef<TArray<FAssetData>> Assets = MakeShared<TArray<FAssetData>>();
	TArray<FSoftObjectPath> CharacterPaths;
	for (int32 Index = 0; Index < 50; ++Index)
	{
		UPaper2DPlusCharacterProfileAsset* Character = NewAsset<UPaper2DPlusCharacterProfileAsset>(
			*FString::Printf(TEXT("/Game/Characters/U28_ThumbnailLru_%02d"), Index));
		Character->DisplayName = FString::Printf(TEXT("LRU Resident %02d"), Index);
		if (Index == 0)
		{
			UPaperFlipbook* ResidentFlipbook = NewAsset<UPaperFlipbook>(
				TEXT("/Game/Characters/U28_ThumbnailLru_ResidentFlipbook"));
			FFlipbookProfileEntry& Preview = Character->Flipbooks.AddDefaulted_GetRef();
			Preview.Identity.FlipbookName = TEXT("Idle");
			Preview.Identity.Flipbook = ResidentFlipbook;
		}
		Catalog->Entries.Add(MakeEntry(Character));
		Assets->Add(MakeAssetData(Character));
		CharacterPaths.Add(FSoftObjectPath(Character));
	}
	TSharedRef<FPaper2DPlusCharacterCatalogSettingsSnapshot> Settings =
		MakeShared<FPaper2DPlusCharacterCatalogSettingsSnapshot>();
	Settings->DefaultCatalog = Catalog;
	TSharedPtr<FCharacterCatalogEditorModel> Model = MakeModel(Catalog, Assets, Settings);
	Model->SetSearchText(TEXT("No Catalog row should match this sentinel"));
	{
		TSharedRef<SCharacterCatalogRosterPanel> Panel =
			SNew(SCharacterCatalogRosterPanel).Model(Model);
		for (int32 Index = 1; Index <= 48; ++Index)
		{
			TestTrue(TEXT("Resident test row reaches the production async-retention path"),
				Panel->RequestThumbnailForPathForTests(CharacterPaths[Index]));
		}
		TestEqual(TEXT("Thumbnail retention reaches but never exceeds its documented cap"),
			Panel->GetRetainedAsyncThumbnailCountForTests(), 48);
		TestTrue(TEXT("The original least-recently-used async row starts retained"),
			Panel->IsAsyncThumbnailRetainedForTests(CharacterPaths[1]));
		TestEqual(TEXT("The completed resident card resolves directly before generation"),
			Panel->GetThumbnailSourceForTests(CharacterPaths[0]), FName(TEXT("ResidentFlipbook")));
		Model->SetSearchText(TEXT("LRU Resident 00"));
		TestTrue(TEXT("Actual resident-card tile generation touches the shared LRU"),
			Panel->GenerateTilesForViewportForTests(FVector2D(320.0f, 220.0f)) > 0);
		TestTrue(TEXT("Generated resident card is retained"),
			Panel->IsAsyncThumbnailRetainedForTests(CharacterPaths[0]));
		TestFalse(TEXT("Generating the resident card evicts the prior LRU row"),
			Panel->IsAsyncThumbnailRetainedForTests(CharacterPaths[1]));
		TestTrue(TEXT("A new visible row enters the bounded retention set"),
			Panel->RequestThumbnailForPathForTests(CharacterPaths[49]));
		TestEqual(TEXT("Adding another thumbnail keeps the cache capped"),
			Panel->GetRetainedAsyncThumbnailCountForTests(), 48);
		TestTrue(TEXT("Recently generated resident card survives the next LRU eviction"),
			Panel->IsAsyncThumbnailRetainedForTests(CharacterPaths[0]));
		TestFalse(TEXT("Next least-recently-used async row is evicted"),
			Panel->IsAsyncThumbnailRetainedForTests(CharacterPaths[2]));
		TestTrue(TEXT("Newest row is retained"),
			Panel->IsAsyncThumbnailRetainedForTests(CharacterPaths[49]));
		Panel->RefreshSourcesForTests();
		TestEqual(TEXT("Explicit Refresh invalidates retained thumbnail successes and failures"),
			Panel->GetRetainedAsyncThumbnailCountForTests(), 0);
		TestFalse(TEXT("Invalidated failed thumbnail is no longer terminally requested"),
			Panel->WasAsyncThumbnailRequestedForTests(CharacterPaths[49]));
		TestTrue(TEXT("Invalidated thumbnail can enter the production request path again"),
			Panel->RequestThumbnailForPathForTests(CharacterPaths[49]));
		TestTrue(TEXT("Retried thumbnail records a fresh request"),
			Panel->WasAsyncThumbnailRequestedForTests(CharacterPaths[49]));
	}
	Model->Shutdown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCatalogEditorLargeVirtualizedRosterTest,
	"Paper2DPlus.CharacterCatalog.Editor.LargeVirtualizedRosterSearchSelectionVisibleLoadingAndLru",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPaper2DPlusCatalogEditorLargeVirtualizedRosterTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterCatalogAsset* Catalog = NewAsset<UPaper2DPlusCharacterCatalogAsset>(
		TEXT("/Game/Paper2DPlusTests/U28/LargeVirtualizedRosterCatalog"));
	TSharedRef<TArray<FAssetData>> Assets = MakeShared<TArray<FAssetData>>();
	TArray<FSoftObjectPath> CharacterPaths;
	for (int32 Index = 0; Index < 128; ++Index)
	{
		UPaper2DPlusCharacterProfileAsset* Character =
			NewAsset<UPaper2DPlusCharacterProfileAsset>(
				*FString::Printf(TEXT("/Game/Characters/U28_LargeRoster_%03d"), Index));
		Character->DisplayName = FString::Printf(TEXT("Large Catalog Hero DisplayOnly_%03d"), Index);
		Catalog->Entries.Add(MakeEntry(Character));
		Assets->Add(MakeAssetData(Character));
		CharacterPaths.Add(FSoftObjectPath(Character));
	}
	Catalog->GetOutermost()->SetDirtyFlag(false);

	TSharedRef<FPaper2DPlusCharacterCatalogSettingsSnapshot> Settings =
		MakeShared<FPaper2DPlusCharacterCatalogSettingsSnapshot>();
	Settings->DefaultCatalog = Catalog;
	TSharedPtr<FCharacterCatalogEditorModel> Model = MakeModel(Catalog, Assets, Settings);
	TestEqual(TEXT("large Catalog model projects every saved Character"),
		Model->GetVisibleRows().Num(), 128);
	Model->SelectCharacter(CharacterPaths[117]);

	if (FSlateApplication::IsInitialized())
	{
		TSharedRef<SCharacterCatalogRosterPanel> Panel =
			SNew(SCharacterCatalogRosterPanel).Model(Model);
		const int32 Generated = Panel->GenerateTilesForViewportForTests(
			FVector2D(320.0f, 220.0f));
		TestTrue(TEXT("constrained Catalog viewport generates visible cards"), Generated > 0);
		TestTrue(TEXT("constrained Catalog viewport virtualizes most of 128 cards"),
			Generated < 128);
		TestTrue(TEXT("visible cards alone enter thumbnail loading state"),
			Panel->GetRetainedAsyncThumbnailCountForTests() > 0);
		TestFalse(TEXT("far off-screen Character starts no thumbnail request"),
			Panel->WasAsyncThumbnailRequestedForTests(CharacterPaths[127]));

		Model->SetSearchText(TEXT("DisplayOnly_117"));
		TestEqual(TEXT("search narrows the 128-card roster to one result"),
			Panel->GetVisibleCharacterCountForTests(), 1);
		TestTrue(TEXT("search result generates in the same virtualized card surface"),
			Panel->GenerateTilesForViewportForTests(FVector2D(320.0f, 220.0f)) > 0);
		TestTrue(TEXT("search-visible Character requests its thumbnail"),
			Panel->WasAsyncThumbnailRequestedForTests(CharacterPaths[117]));
		TestEqual(TEXT("filtering preserves path-keyed selection"),
			Model->GetSelectedCharacterPath(), CharacterPaths[117]);

		Catalog->Entries.Swap(0, 117);
		Model->RefreshFromSources();
		TestEqual(TEXT("selection survives Catalog reorder by soft path"),
			Model->GetSelectedCharacterPath(), CharacterPaths[117]);

		Panel->RefreshSourcesForTests();
		TestEqual(TEXT("explicit Refresh resets thumbnail retention before LRU exercise"),
			Panel->GetRetainedAsyncThumbnailCountForTests(), 0);
		for (int32 Index = 0; Index < 48; ++Index)
		{
			Model->SetSearchText(FString::Printf(TEXT("DisplayOnly_%03d"), Index));
			TestTrue(TEXT("each searched Character generates through the real card boundary"),
				Panel->GenerateTilesForViewportForTests(FVector2D(320.0f, 220.0f)) > 0);
			TestTrue(TEXT("generated Character records a thumbnail request"),
				Panel->WasAsyncThumbnailRequestedForTests(CharacterPaths[Index]));
		}
		TestEqual(TEXT("large-roster thumbnail retention reaches the bounded cap"),
			Panel->GetRetainedAsyncThumbnailCountForTests(), 48);

		Model->SetSearchText(TEXT("DisplayOnly_000"));
		Panel->GenerateTilesForViewportForTests(FVector2D(320.0f, 220.0f));
		TestTrue(TEXT("revisited visible Character explicitly touches the shared LRU"),
			Panel->RequestThumbnailForPathForTests(CharacterPaths[0]));
		Model->SetSearchText(TEXT("DisplayOnly_048"));
		Panel->GenerateTilesForViewportForTests(FVector2D(320.0f, 220.0f));
		TestEqual(TEXT("large-roster cache remains capped after a new visible card"),
			Panel->GetRetainedAsyncThumbnailCountForTests(), 48);
		TestTrue(TEXT("recently revisited Character survives true-LRU eviction"),
			Panel->IsAsyncThumbnailRetainedForTests(CharacterPaths[0]));
		TestFalse(TEXT("least-recently-used Character is evicted"),
			Panel->IsAsyncThumbnailRetainedForTests(CharacterPaths[1]));
		TestTrue(TEXT("newest searched Character is retained"),
			Panel->IsAsyncThumbnailRetainedForTests(CharacterPaths[48]));
	}

	TestFalse(TEXT("large roster navigation and thumbnail projection stay read-only"),
		Catalog->GetOutermost()->IsDirty());
	Model->Shutdown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCatalogEditorDeferredTagsAndGroupRailScopeTest,
	"Paper2DPlus.CharacterCatalog.Editor.DeferredTagsAndGroupRailScopesTheGrid",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPaper2DPlusCatalogEditorDeferredTagsAndGroupRailScopeTest::RunTest(const FString& Parameters)
{
	if (!FSlateApplication::IsInitialized())
	{
		Paper2DPlusTestSkip::Mark(
			*this,
			TEXT("Character Catalog deferred tag commit and Groups rail scoping"),
			TEXT("Slate is not initialized in this host, so neither panel can be built"));
		return true;
	}
	if (GEditor)
	{
		GEditor->ResetTransaction(FText::FromString(TEXT("U28 deferred tags and groups start")));
	}

	UPaper2DPlusCharacterCatalogAsset* Catalog = NewAsset<UPaper2DPlusCharacterCatalogAsset>(
		TEXT("/Game/Paper2DPlusTests/U28/DeferredTagsAndGroupsCatalog"));
	UPaper2DPlusCharacterProfileAsset* A = NewAsset<UPaper2DPlusCharacterProfileAsset>(
		TEXT("/Game/Characters/U28_DeferredTagsAndGroupsA"));
	UPaper2DPlusCharacterProfileAsset* B = NewAsset<UPaper2DPlusCharacterProfileAsset>(
		TEXT("/Game/Characters/U28_DeferredTagsAndGroupsB"));
	// C is deliberately OUTSIDE every group: with only Squad's own members in the Catalog, scoping the
	// rail to Squad would show the same card count as All Characters and prove nothing.
	UPaper2DPlusCharacterProfileAsset* C = NewAsset<UPaper2DPlusCharacterProfileAsset>(
		TEXT("/Game/Characters/U28_DeferredTagsAndGroupsC"));
	Catalog->Entries = { MakeEntry(A), MakeEntry(B), MakeEntry(C) };
	FPaper2DPlusCharacterCatalogGroup& Squad = Catalog->Groups.AddDefaulted_GetRef();
	Squad.GroupName = TEXT("Squad");
	Squad.DisplayName = FText::FromString(TEXT("Squad"));
	Squad.Members.Add(A);
	Squad.Members.Add(B);
	FPaper2DPlusCharacterCatalogGroup& Reserve = Catalog->Groups.AddDefaulted_GetRef();
	Reserve.GroupName = TEXT("Reserve");
	Reserve.DisplayName = FText::FromString(TEXT("Reserve"));

	TSharedRef<TArray<FAssetData>> Assets = MakeShared<TArray<FAssetData>>();
	*Assets = { MakeAssetData(A), MakeAssetData(B), MakeAssetData(C) };
	TSharedRef<FPaper2DPlusCharacterCatalogSettingsSnapshot> Settings =
		MakeShared<FPaper2DPlusCharacterCatalogSettingsSnapshot>();
	Settings->DefaultCatalog = Catalog;
	TSharedPtr<FCharacterCatalogEditorModel> Model = MakeModel(Catalog, Assets, Settings);
	TSharedRef<SCharacterCatalogDetailsPanel> Details =
		SNew(SCharacterCatalogDetailsPanel).Model(Model);

	FGameplayTagContainer Tags;
	Tags.AddTag(Paper2DPlusAnimationTags::Combat_Heavy);
	Details->QueueEntryTagsCommitForTests(FSoftObjectPath(A), Tags);
	TestTrue(TEXT("Tag picker mutation is queued until its callback can unwind"),
		Details->HasPendingEntryTagsCommitForTests());
	TestFalse(TEXT("Queued tag picker mutation does not rebuild Details synchronously"),
		Catalog->Entries[0].Tags.HasTagExact(Paper2DPlusAnimationTags::Combat_Heavy));
	Details->FlushEntryTagsCommitForTests();
	TestFalse(TEXT("Flushed tag picker mutation clears its pending state"),
		Details->HasPendingEntryTagsCommitForTests());
	TestTrue(TEXT("Deferred tag picker mutation reaches the Catalog"),
		Catalog->Entries[0].Tags.HasTagExact(Paper2DPlusAnimationTags::Combat_Heavy));

	// The retired Groups tab is replaced by a rail INSIDE the roster: group navigation is now a scope
	// on the one card grid rather than a second surface with its own selection to keep in sync.
	TSharedRef<SCharacterCatalogRosterPanel> Roster =
		SNew(SCharacterCatalogRosterPanel).Model(Model);
	TestTrue(TEXT("The roster owns the Groups rail"), Roster->HasGroupRailForTests());
	TestTrue(TEXT("The Groups rail always exposes the base expected-animation tags editor"),
		Roster->HasBaseExpectedAnimationTagsControlForTests());
	TestFalse(TEXT("All Characters does not pretend to own group-only expected tags"),
		Roster->HasSelectedGroupExpectedAnimationTagsControlForTests());
	const TArray<FName> RailItems = Roster->GetGroupRailItemsForTests();
	if (TestEqual(TEXT("The rail lists All Characters plus every authored group"), RailItems.Num(), 3))
	{
		TestTrue(TEXT("All Characters leads the rail as the NAME_None sentinel"), RailItems[0].IsNone());
		TestEqual(TEXT("The rail then follows authored group order"),
			RailItems[1], FName(TEXT("Squad")));
		TestEqual(TEXT("The rail keeps later groups in authored order too"),
			RailItems[2], FName(TEXT("Reserve")));
	}

	TestEqual(TEXT("An unscoped rail shows the whole roster"), Model->GetVisibleRows().Num(), 3);
	TestTrue(TEXT("A rail row scopes the grid"), Roster->SelectGroupRailItemForTests(TEXT("Squad")));
	TestTrue(TEXT("Selecting a real group exposes its additional expected-animation tags editor"),
		Roster->HasSelectedGroupExpectedAnimationTagsControlForTests());
	TestEqual(TEXT("Scoping the rail narrows the grid to that group's members"),
		Model->GetVisibleRows().Num(), 2);
	TestEqual(TEXT("The rail mirrors the scope it applied"),
		Roster->GetSelectedGroupRailItemForTests(), FName(TEXT("Squad")));
	TestEqual(TEXT("Rail scope IS the model's group filter, not private rail state"),
		Model->GetFilters().Group, FName(TEXT("Squad")));

	FGameplayTagContainer BaseExpectedTags;
	BaseExpectedTags.AddTag(Paper2DPlusAnimationTags::Combat_Heavy);
	Roster->QueueExpectedAnimationTagsCommitForTests(NAME_None, BaseExpectedTags);
	TestTrue(TEXT("Base expected tags wait for the deferred menu-dismiss commit"),
		Roster->HasPendingExpectedAnimationTagsCommitForTests());
	TestTrue(TEXT("Queuing base expected tags does not synchronously write the Catalog"),
		Catalog->ExpectedAnimationTags.IsEmpty());
	Roster->FlushExpectedAnimationTagsCommitForTests();
	TestFalse(TEXT("Flushing base expected tags clears the pending commit"),
		Roster->HasPendingExpectedAnimationTagsCommitForTests());
	TestTrue(TEXT("The deferred base expected-tag commit reaches the Catalog"),
		Catalog->ExpectedAnimationTags.HasTagExact(Paper2DPlusAnimationTags::Combat_Heavy));

	FGameplayTagContainer SquadExpectedTags;
	SquadExpectedTags.AddTag(Paper2DPlusAnimationTags::Context_Swimming);
	Roster->QueueExpectedAnimationTagsCommitForTests(TEXT("Squad"), SquadExpectedTags);
	TestTrue(TEXT("Selected-group extras also wait for the deferred menu-dismiss commit"),
		Roster->HasPendingExpectedAnimationTagsCommitForTests());
	TestTrue(TEXT("Queuing selected-group extras does not synchronously write the group"),
		Catalog->Groups[0].AdditionalExpectedAnimationTags.IsEmpty());
	Roster->FlushExpectedAnimationTagsCommitForTests();
	TestTrue(TEXT("The deferred selected-group extras commit reaches the group struct"),
		Catalog->Groups[0].AdditionalExpectedAnimationTags.HasTagExact(
			Paper2DPlusAnimationTags::Context_Swimming));
	Roster->QueueExpectedAnimationTagsCommitForTests(TEXT("Squad"), SquadExpectedTags);
	TestFalse(TEXT("An unchanged picker dismissal queues no refresh or mutation"),
		Roster->HasPendingExpectedAnimationTagsCommitForTests());
	Roster->QueueExpectedAnimationTagsCommitForTests(
		TEXT("Squad"),
		FGameplayTagContainer());
	TestTrue(TEXT("A removable-chip gesture uses the same deferred whole-container commit"),
		Roster->HasPendingExpectedAnimationTagsCommitForTests());
	TestTrue(TEXT("Queuing chip removal does not synchronously write the group"),
		Catalog->Groups[0].AdditionalExpectedAnimationTags.HasTagExact(
			Paper2DPlusAnimationTags::Context_Swimming));
	Roster->FlushExpectedAnimationTagsCommitForTests();
	TestTrue(TEXT("Flushing the removable-chip gesture clears the authored group extras"),
		Catalog->Groups[0].AdditionalExpectedAnimationTags.IsEmpty());

	TestTrue(TEXT("An empty group can be scoped"), Roster->SelectGroupRailItemForTests(TEXT("Reserve")));
	TestEqual(TEXT("An empty group scopes the grid to nothing"), Model->GetVisibleRows().Num(), 0);
	TestEqual(TEXT("The rail mirrors the empty group's scope"),
		Roster->GetSelectedGroupRailItemForTests(), FName(TEXT("Reserve")));
	TestTrue(TEXT("All Characters can be reselected"),
		Roster->SelectGroupRailItemForTests(NAME_None));
	TestTrue(TEXT("Selecting All Characters clears the rail's scope"),
		Roster->GetSelectedGroupRailItemForTests().IsNone());
	TestTrue(TEXT("Selecting All Characters clears the model's group filter"),
		Model->GetFilters().Group.IsNone());
	TestEqual(TEXT("The cleared scope restores the whole roster"), Model->GetVisibleRows().Num(), 3);

	Model->Shutdown();
	if (GEditor)
	{
		GEditor->ResetTransaction(FText::FromString(TEXT("U28 deferred tags and groups end")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCatalogExpectedAnimationTagSetterTest,
	"Paper2DPlus.CharacterCatalog.Editor.ExpectedAnimationTagSettersNoOpDirtyUndoAndGroupLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPaper2DPlusCatalogExpectedAnimationTagSetterTest::RunTest(const FString& Parameters)
{
	if (!GEditor)
	{
		AddError(TEXT("GEditor is required for expected animation-tag transaction coverage."));
		return false;
	}

	UPaper2DPlusCharacterCatalogAsset* Catalog = NewAsset<UPaper2DPlusCharacterCatalogAsset>(
		TEXT("/Game/Paper2DPlusTests/U11/ExpectedAnimationTagCatalog"));
	FPaper2DPlusCharacterCatalogGroup& Heroes = Catalog->Groups.AddDefaulted_GetRef();
	Heroes.GroupName = TEXT("Heroes");
	Heroes.DisplayName = FText::FromString(TEXT("Heroes"));
	FPaper2DPlusCharacterCatalogGroup& Rivals = Catalog->Groups.AddDefaulted_GetRef();
	Rivals.GroupName = TEXT("Rivals");
	Rivals.DisplayName = FText::FromString(TEXT("Rivals"));
	Catalog->GetOutermost()->SetDirtyFlag(false);

	TSharedRef<TArray<FAssetData>> Assets = MakeShared<TArray<FAssetData>>();
	TSharedRef<FPaper2DPlusCharacterCatalogSettingsSnapshot> Settings =
		MakeShared<FPaper2DPlusCharacterCatalogSettingsSnapshot>();
	Settings->DefaultCatalog = Catalog;
	TSharedPtr<FCharacterCatalogEditorModel> Model = MakeModel(Catalog, Assets, Settings);

	FGameplayTagContainer BaseTags;
	BaseTags.AddTag(Paper2DPlusAnimationTags::Combat_Heavy);
	FGameplayTagContainer GroupExtras;
	GroupExtras.AddTag(Paper2DPlusAnimationTags::Context_Swimming);

	GEditor->ResetTransaction(FText::FromString(TEXT("U11 expected animation tags start")));
	const int32 MutationsBeforeNoOp = Model->GetMutationCountForTests();
	TestFalse(TEXT("Empty base replacement is a no-op"),
		Model->SetExpectedAnimationTags(FGameplayTagContainer()));
	TestFalse(TEXT("Base no-op does not dirty the Catalog"), Catalog->GetOutermost()->IsDirty());
	TestEqual(TEXT("Base no-op does not refresh the model"),
		Model->GetMutationCountForTests(), MutationsBeforeNoOp);

	TestTrue(TEXT("A real base replacement succeeds"), Model->SetExpectedAnimationTags(BaseTags));
	TestTrue(TEXT("A real base replacement dirties the Catalog"), Catalog->GetOutermost()->IsDirty());
	TestEqual(TEXT("A real base replacement refreshes once"),
		Model->GetMutationCountForTests(), MutationsBeforeNoOp + 1);
	TestTrue(TEXT("One undo removes the complete base replacement"),
		GEditor->UndoTransaction(true));
	TestTrue(TEXT("Undo restores the empty base container"),
		Catalog->ExpectedAnimationTags.IsEmpty());
	TestTrue(TEXT("Redo restores the complete base replacement"),
		GEditor->RedoTransaction());
	TestTrue(TEXT("Redo restores the base container byte-for-byte"),
		Catalog->ExpectedAnimationTags == BaseTags);

	GEditor->ResetTransaction(FText::FromString(TEXT("U11 group expected animation tags")));
	Catalog->GetOutermost()->SetDirtyFlag(false);
	const int32 MutationsBeforeGroupNoOp = Model->GetMutationCountForTests();
	TestFalse(TEXT("Empty group-extras replacement is a no-op"),
		Model->SetGroupAdditionalExpectedAnimationTags(TEXT("Heroes"), FGameplayTagContainer()));
	TestFalse(TEXT("Group-extras no-op does not dirty the Catalog"),
		Catalog->GetOutermost()->IsDirty());
	TestEqual(TEXT("Group-extras no-op does not refresh the model"),
		Model->GetMutationCountForTests(), MutationsBeforeGroupNoOp);

	TestTrue(TEXT("A real group-extras replacement succeeds"),
		Model->SetGroupAdditionalExpectedAnimationTags(TEXT("Heroes"), GroupExtras));
	TestTrue(TEXT("A real group-extras replacement dirties the Catalog"),
		Catalog->GetOutermost()->IsDirty());
	TestTrue(TEXT("One undo removes the complete group-extras replacement"),
		GEditor->UndoTransaction(true));
	TestTrue(TEXT("Undo restores the empty group-extras container"),
		Catalog->Groups[0].AdditionalExpectedAnimationTags.IsEmpty());
	TestTrue(TEXT("Redo restores the complete group-extras replacement"),
		GEditor->RedoTransaction());
	TestTrue(TEXT("Redo restores group extras byte-for-byte"),
		Catalog->Groups[0].AdditionalExpectedAnimationTags == GroupExtras);

	Catalog->GetOutermost()->SetDirtyFlag(false);
	const int32 MutationsBeforeRefresh = Model->GetMutationCountForTests();
	Model->RefreshFromSources();
	TestFalse(TEXT("A source refresh never dirties expected-tag authoring"),
		Catalog->GetOutermost()->IsDirty());
	TestEqual(TEXT("A source refresh opens no mutation path"),
		Model->GetMutationCountForTests(), MutationsBeforeRefresh);
	TestTrue(TEXT("A source refresh leaves group extras byte-for-byte intact"),
		Catalog->Groups[0].AdditionalExpectedAnimationTags == GroupExtras);

	FText Error;
	TestTrue(TEXT("Renaming a group carries its embedded extras"),
		Model->RenameGroup(
			TEXT("Heroes"),
			TEXT("Champions"),
			FText::FromString(TEXT("Champions")),
			Error));
	const FPaper2DPlusCharacterCatalogGroup* Renamed =
		Catalog->Groups.FindByPredicate([](const FPaper2DPlusCharacterCatalogGroup& Group)
		{
			return Group.GroupName == FName(TEXT("Champions"));
		});
	TestNotNull(TEXT("The renamed group remains present"), Renamed);
	if (Renamed)
	{
		TestTrue(TEXT("Rename preserves embedded expected-tag extras"),
			Renamed->AdditionalExpectedAnimationTags == GroupExtras);
	}

	TestTrue(TEXT("Reordering the renamed group succeeds"),
		Model->MoveGroupToIndex(TEXT("Champions"), Catalog->Groups.Num()));
	TestEqual(TEXT("The renamed group moved as one struct"),
		Catalog->Groups.Last().GroupName, FName(TEXT("Champions")));
	TestTrue(TEXT("Reorder preserves embedded expected-tag extras"),
		Catalog->Groups.Last().AdditionalExpectedAnimationTags == GroupExtras);

	TestTrue(TEXT("Deleting the group removes its embedded extras atomically"),
		Model->RemoveGroup(TEXT("Champions")));
	TestNull(TEXT("No deleted group survives to orphan its extras"),
		Catalog->Groups.FindByPredicate([](const FPaper2DPlusCharacterCatalogGroup& Group)
		{
			return Group.GroupName == FName(TEXT("Champions"));
		}));
	TestFalse(TEXT("No surviving group inherited the deleted group's extras"),
		Catalog->Groups.ContainsByPredicate([&GroupExtras](
			const FPaper2DPlusCharacterCatalogGroup& Group)
		{
			return Group.AdditionalExpectedAnimationTags == GroupExtras;
		}));
	TestTrue(TEXT("One undo restores the deleted group struct"),
		GEditor->UndoTransaction(true));
	const FPaper2DPlusCharacterCatalogGroup* Restored =
		Catalog->Groups.FindByPredicate([](const FPaper2DPlusCharacterCatalogGroup& Group)
		{
			return Group.GroupName == FName(TEXT("Champions"));
		});
	TestNotNull(TEXT("Undo restores the deleted group"), Restored);
	if (Restored)
	{
		TestTrue(TEXT("Undo restores the group's embedded extras"),
			Restored->AdditionalExpectedAnimationTags == GroupExtras);
	}

	GEditor->ResetTransaction(FText::FromString(TEXT("U11 expected animation tags end")));
	Model->Shutdown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCatalogGroupRailMutationTest,
	"Paper2DPlus.CharacterCatalog.Editor.GroupRailCreateDropReorderRenameDeleteUndo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPaper2DPlusCatalogGroupRailMutationTest::RunTest(const FString& Parameters)
{
	if (!GEditor)
	{
		AddError(TEXT("GEditor is required for Groups rail transaction coverage."));
		return false;
	}
	if (!FSlateApplication::IsInitialized())
	{
		Paper2DPlusTestSkip::Mark(
			*this,
			TEXT("Character Catalog Groups rail create/drop/reorder/rename/delete"),
			TEXT("Slate is not initialized in this host, so the rail cannot be built"));
		return true;
	}

	UPaper2DPlusCharacterCatalogAsset* Catalog = NewAsset<UPaper2DPlusCharacterCatalogAsset>(
		TEXT("/Game/Paper2DPlusTests/U29/GroupRailCatalog"));
	UPaper2DPlusCharacterProfileAsset* A = NewAsset<UPaper2DPlusCharacterProfileAsset>(
		TEXT("/Game/Characters/U29_GroupRailA"));
	UPaper2DPlusCharacterProfileAsset* B = NewAsset<UPaper2DPlusCharacterProfileAsset>(
		TEXT("/Game/Characters/U29_GroupRailB"));
	UPaper2DPlusCharacterProfileAsset* C = NewAsset<UPaper2DPlusCharacterProfileAsset>(
		TEXT("/Game/Characters/U29_GroupRailC"));
	// Deliberately NOT added to the Catalog: a group may only ever contain saved roster members.
	UPaper2DPlusCharacterProfileAsset* Outsider = NewAsset<UPaper2DPlusCharacterProfileAsset>(
		TEXT("/Game/Characters/U29_GroupRailOutsider"));
	Catalog->Entries = { MakeEntry(A), MakeEntry(B), MakeEntry(C) };

	TSharedRef<TArray<FAssetData>> Assets = MakeShared<TArray<FAssetData>>();
	*Assets = { MakeAssetData(A), MakeAssetData(B), MakeAssetData(C), MakeAssetData(Outsider) };
	TSharedRef<FPaper2DPlusCharacterCatalogSettingsSnapshot> Settings =
		MakeShared<FPaper2DPlusCharacterCatalogSettingsSnapshot>();
	Settings->DefaultCatalog = Catalog;
	TSharedPtr<FCharacterCatalogEditorModel> Model = MakeModel(Catalog, Assets, Settings);
	TSharedRef<SCharacterCatalogRosterPanel> Roster =
		SNew(SCharacterCatalogRosterPanel).Model(Model);
	TestTrue(TEXT("The roster owns the Groups rail"), Roster->HasGroupRailForTests());

	auto MemberOrder = [Catalog]()
	{
		TArray<FSoftObjectPath> Order;
		if (Catalog->Groups.Num() > 0)
		{
			for (const TSoftObjectPtr<UPaper2DPlusCharacterProfileAsset>& Member : Catalog->Groups[0].Members)
			{
				Order.Add(Member.ToSoftObjectPath());
			}
		}
		return Order;
	};

	// ---- Create: one typed label, one derived internal name, one undo unit ----
	GEditor->ResetTransaction(FText::FromString(TEXT("U29 group rail create start")));
	TestTrue(TEXT("The rail creates a group from a typed label alone"),
		Roster->CreateGroupFromRailForTests(FText::FromString(TEXT("Boss Fight"))));
	if (!TestEqual(TEXT("Rail creation appends exactly one group"), Catalog->Groups.Num(), 1))
	{
		Model->Shutdown();
		return false;
	}
	const FName FirstDerived = Catalog->Groups[0].GroupName;
	TestFalse(TEXT("The derived internal Blueprint name is not empty"), FirstDerived.IsNone());
	TestFalse(TEXT("The derived internal Blueprint name carries no spaces"),
		FirstDerived.ToString().Contains(TEXT(" ")));
	TestEqual(TEXT("The typed label becomes the group's DisplayName verbatim"),
		Catalog->Groups[0].DisplayName.ToString(), FString(TEXT("Boss Fight")));
	TestEqual(TEXT("Rail creation scopes the grid to the new group"),
		Roster->GetSelectedGroupRailItemForTests(), FirstDerived);
	TestTrue(TEXT("The new group appears in the rail"),
		Roster->GetGroupRailItemsForTests().Contains(FirstDerived));
	TestTrue(TEXT("One undo reverts the whole rail creation"), GEditor->UndoTransaction(true));
	Model->RefreshAfterExternalMutation();
	TestEqual(TEXT("Undo leaves no group behind"), Catalog->Groups.Num(), 0);
	TestTrue(TEXT("Redo restores the created group"), GEditor->RedoTransaction());
	Model->RefreshAfterExternalMutation();
	if (!TestEqual(TEXT("Redo restores exactly one group"), Catalog->Groups.Num(), 1))
	{
		Model->Shutdown();
		return false;
	}

	TestTrue(TEXT("A second group can carry the very same designer label"),
		Roster->CreateGroupFromRailForTests(FText::FromString(TEXT("Boss Fight"))));
	if (!TestEqual(TEXT("The duplicate label still adds a second group"), Catalog->Groups.Num(), 2))
	{
		Model->Shutdown();
		return false;
	}
	TestNotEqual(TEXT("A repeated label derives a DIFFERENT unique internal name"),
		Catalog->Groups[1].GroupName, FirstDerived);
	TestEqual(TEXT("Both groups keep the designer's label"),
		Catalog->Groups[1].DisplayName.ToString(), FString(TEXT("Boss Fight")));

	// ---- Drop: one cohort, one transaction, and no silent duplicates ----
	TestTrue(TEXT("The rail scopes the grid before the drop"),
		Roster->SelectGroupRailItemForTests(FirstDerived));
	GEditor->ResetTransaction(FText::FromString(TEXT("U29 group rail drop start")));
	TestTrue(TEXT("A dragged cohort lands in the target group"),
		Roster->DropCharactersOnGroupForTests(FirstDerived, { FSoftObjectPath(A), FSoftObjectPath(B) }));
	TestEqual(TEXT("The whole cohort joins in one drop"), Catalog->Groups[0].Members.Num(), 2);
	// A wholly redundant drop must open NO transaction, or the undo below would consume it instead of
	// the real cohort add.
	TestFalse(TEXT("Re-dropping the same cohort adds nobody"),
		Roster->DropCharactersOnGroupForTests(FirstDerived, { FSoftObjectPath(A), FSoftObjectPath(B) }));
	TestEqual(TEXT("A redundant drop leaves membership untouched"),
		Catalog->Groups[0].Members.Num(), 2);
	TestFalse(TEXT("A character that is not in the Catalog cannot be grouped"),
		Roster->DropCharactersOnGroupForTests(FirstDerived, { FSoftObjectPath(Outsider) }));
	TestEqual(TEXT("Refusing an out-of-Catalog drop leaves membership untouched"),
		Catalog->Groups[0].Members.Num(), 2);
	TestTrue(TEXT("One undo reverts the whole two-character drop"), GEditor->UndoTransaction(true));
	Model->RefreshAfterExternalMutation();
	TestEqual(TEXT("A single transaction covered the whole cohort"),
		Catalog->Groups[0].Members.Num(), 0);
	TestTrue(TEXT("Redo reapplies the cohort drop"), GEditor->RedoTransaction());
	Model->RefreshAfterExternalMutation();
	TestEqual(TEXT("Redo restores both members"), Catalog->Groups[0].Members.Num(), 2);
	TestTrue(TEXT("A third character joins for ordering coverage"),
		Roster->DropCharactersOnGroupForTests(FirstDerived, { FSoftObjectPath(C) }));

	// ---- Scoped grid order IS authored member order ----
	// Entry order and member order deliberately DISAGREE: a scoped grid that merely echoed Catalog entry
	// order would still pass an equal-order assertion, and the rail's drag-reorder would then move data
	// the designer cannot see.
	Catalog->Groups[0].Members.Reset();
	Catalog->Groups[0].Members.Add(C);
	Catalog->Groups[0].Members.Add(A);
	Catalog->Groups[0].Members.Add(B);
	Model->RefreshFromSources();
	TestTrue(TEXT("The rail keeps the group scoped after an external refresh"),
		Roster->SelectGroupRailItemForTests(FirstDerived));
	const TArray<FSoftObjectPath> ScopedOrder = CatalogRail_VisibleOrder(Model);
	if (TestEqual(TEXT("The scoped grid shows every member"), ScopedOrder.Num(), 3))
	{
		TestEqual(TEXT("The scoped grid leads with the first authored MEMBER, not the first entry"),
			ScopedOrder[0], FSoftObjectPath(C));
		TestEqual(TEXT("The scoped grid follows authored member order"),
			ScopedOrder[1], FSoftObjectPath(A));
		TestEqual(TEXT("The scoped grid follows authored member order to the end"),
			ScopedOrder[2], FSoftObjectPath(B));
	}
	Catalog->Groups[0].Members.Reset();
	Catalog->Groups[0].Members.Add(B);
	Catalog->Groups[0].Members.Add(C);
	Catalog->Groups[0].Members.Add(A);
	Model->RefreshFromSources();
	const TArray<FSoftObjectPath> ReorderedScopedOrder = CatalogRail_VisibleOrder(Model);
	if (TestEqual(TEXT("The re-sorted scoped grid still shows every member"),
		ReorderedScopedOrder.Num(), 3))
	{
		TestEqual(TEXT("Reordering members re-sorts the scoped grid"),
			ReorderedScopedOrder[0], FSoftObjectPath(B));
		TestEqual(TEXT("Reordering members re-sorts the whole scoped grid"),
			ReorderedScopedOrder[1], FSoftObjectPath(C));
		TestEqual(TEXT("Reordering members re-sorts the scoped grid to the end"),
			ReorderedScopedOrder[2], FSoftObjectPath(A));
	}

	// ---- Reorder: insert-before semantics, one undo unit ----
	Catalog->Groups[0].Members.Reset();
	Catalog->Groups[0].Members.Add(A);
	Catalog->Groups[0].Members.Add(B);
	Catalog->Groups[0].Members.Add(C);
	Model->RefreshFromSources();
	TestTrue(TEXT("All Characters can be scoped before the unscoped-reorder check"),
		Roster->SelectGroupRailItemForTests(NAME_None));
	TestFalse(TEXT("An unscoped grid has no member order to reorder"),
		Roster->ReorderGroupMemberForTests(FSoftObjectPath(A), 2));
	TestTrue(TEXT("The rail re-scopes the group for the reorder"),
		Roster->SelectGroupRailItemForTests(FirstDerived));
	GEditor->ResetTransaction(FText::FromString(TEXT("U29 group rail reorder start")));
	// Insert-before: the target index is a GAP, so dropping a card onto its own gaps is a no-op and must
	// open no transaction.
	TestFalse(TEXT("Dropping a member onto its own leading gap is a no-op"),
		Roster->ReorderGroupMemberForTests(FSoftObjectPath(A), 0));
	TestFalse(TEXT("Dropping a member onto its own trailing gap is a no-op"),
		Roster->ReorderGroupMemberForTests(FSoftObjectPath(A), 1));
	TestTrue(TEXT("A scoped card-on-card drop reorders the authored membership"),
		Roster->ReorderGroupMemberForTests(FSoftObjectPath(A), 2));
	const TArray<FSoftObjectPath> AfterReorder = MemberOrder();
	if (TestEqual(TEXT("Reorder preserves the member count"), AfterReorder.Num(), 3))
	{
		TestEqual(TEXT("Insert-before moves the old second member up"),
			AfterReorder[0], FSoftObjectPath(B));
		TestEqual(TEXT("Insert-before lands the moved member between the old second and third"),
			AfterReorder[1], FSoftObjectPath(A));
		TestEqual(TEXT("Insert-before leaves the old third member last"),
			AfterReorder[2], FSoftObjectPath(C));
	}
	TestTrue(TEXT("One undo restores the authored member order"), GEditor->UndoTransaction(true));
	Model->RefreshAfterExternalMutation();
	const TArray<FSoftObjectPath> AfterUndo = MemberOrder();
	if (TestEqual(TEXT("Undo preserves the member count"), AfterUndo.Num(), 3))
	{
		TestEqual(TEXT("Undo restores the original first member"), AfterUndo[0], FSoftObjectPath(A));
		TestEqual(TEXT("Undo restores the original second member"), AfterUndo[1], FSoftObjectPath(B));
		TestEqual(TEXT("Undo restores the original third member"), AfterUndo[2], FSoftObjectPath(C));
	}

	// ---- Rename: the LABEL changes, the Blueprint identity does not ----
	TestTrue(TEXT("The rail renames a group's label inline"),
		Roster->RenameGroupFromRailForTests(FirstDerived, FText::FromString(TEXT("Final Boss"))));
	TestEqual(TEXT("Rename writes the new label"),
		Catalog->Groups[0].DisplayName.ToString(), FString(TEXT("Final Boss")));
	TestEqual(TEXT("Rename leaves the internal Blueprint identity untouched"),
		Catalog->Groups[0].GroupName, FirstDerived);
	TestFalse(TEXT("An empty label is refused"),
		Roster->RenameGroupFromRailForTests(FirstDerived, FText::GetEmpty()));
	TestFalse(TEXT("A whitespace-only label is refused"),
		Roster->RenameGroupFromRailForTests(FirstDerived, FText::FromString(TEXT("   "))));
	TestEqual(TEXT("A refused rename leaves the label intact"),
		Catalog->Groups[0].DisplayName.ToString(), FString(TEXT("Final Boss")));
	TestEqual(TEXT("A refused rename leaves the internal name intact"),
		Catalog->Groups[0].GroupName, FirstDerived);

	// ---- Delete: confirmed, character-preserving, and it releases the scope ----
	const int32 MemberCount = Catalog->Groups[0].Members.Num();
	FText DeleteConfirmation;
	Roster->RemoveGroupConfirmation = [&DeleteConfirmation](const FText& Message)
	{
		DeleteConfirmation = Message;
		return false;
	};
	TestFalse(TEXT("Canceling the confirmation refuses the deletion"),
		Roster->RemoveGroupFromRailForTests(FirstDerived));
	TestEqual(TEXT("A canceled deletion leaves both groups in place"), Catalog->Groups.Num(), 2);
	TestTrue(TEXT("The confirmation states how many members the deletion affects"),
		DeleteConfirmation.ToString().Contains(FText::AsNumber(MemberCount).ToString()));
	TestTrue(TEXT("The confirmation says the members stay in the Catalog"),
		DeleteConfirmation.ToString().Contains(TEXT("stay in the Catalog"), ESearchCase::IgnoreCase));

	Roster->RemoveGroupConfirmation = [](const FText&) { return true; };
	TestTrue(TEXT("A confirmed deletion removes the group"),
		Roster->RemoveGroupFromRailForTests(FirstDerived));
	TestEqual(TEXT("Only the deleted group is gone"), Catalog->Groups.Num(), 1);
	TestFalse(TEXT("The deleted group leaves the rail"),
		Roster->GetGroupRailItemsForTests().Contains(FirstDerived));
	TestEqual(TEXT("Deleting a group keeps every character in the Catalog"), Catalog->Entries.Num(), 3);
	for (UPaper2DPlusCharacterProfileAsset* Character : { A, B, C })
	{
		TestTrue(
			FString::Printf(TEXT("%s survives its group's deletion"), *Character->GetName()),
			Model->IsCharacterInCatalog(FSoftObjectPath(Character)));
	}
	TestTrue(TEXT("Deleting the scoped group resets the rail to All Characters"),
		Roster->GetSelectedGroupRailItemForTests().IsNone());
	TestTrue(TEXT("Deleting the scoped group clears the model's group filter too"),
		Model->GetFilters().Group.IsNone());
	TestEqual(TEXT("The released scope shows the whole roster again"),
		Model->GetVisibleRows().Num(), 3);

	// Scoping is navigation, so it must never author anything.
	Catalog->GetOutermost()->SetDirtyFlag(false);
	TestTrue(TEXT("The surviving group can be scoped"),
		Roster->SelectGroupRailItemForTests(Catalog->Groups[0].GroupName));
	TestTrue(TEXT("All Characters can be scoped again"),
		Roster->SelectGroupRailItemForTests(NAME_None));
	TestFalse(TEXT("Rail scoping never dirties the Catalog"), Catalog->GetOutermost()->IsDirty());

	Model->Shutdown();
	GEditor->ResetTransaction(FText::FromString(TEXT("U29 group rail end")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCatalogAuthoringProgressTagsTest,
	"Paper2DPlus.CharacterCatalog.Editor.AuthoringProgressTagsResidencyAndCompanionAggregation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPaper2DPlusCatalogAuthoringProgressTagsTest::RunTest(const FString& Parameters)
{
	// ---- The Character Profile's own aggregate ----
	UPaper2DPlusCharacterProfileAsset* Empty = CatalogProgress_MakeCharacter(
		TEXT("/Game/Characters/U29_ProgressEmpty"), {});
	int32 EmptyDone = -1;
	int32 EmptyTotal = -1;
	Empty->GetAuthoringProgress(EmptyDone, EmptyTotal);
	TestEqual(TEXT("A profile with no animations offers nothing to author"), EmptyTotal, 0);
	TestEqual(TEXT("A profile with no animations has nothing ticked"), EmptyDone, 0);

	// One retired bit per animation, because the exported Done must count LIVE criteria only: a retired
	// bit left over from an older asset must not inflate the roster's progress.
	constexpr int32 RetiredCompletionBit = 1 << 3;
	TestEqual(TEXT("The chosen retired bit really is outside the live mask"),
		RetiredCompletionBit & UPaper2DPlusCharacterProfileAsset::LiveTaskBits, 0);
	UPaper2DPlusCharacterProfileAsset* Mixed = CatalogProgress_MakeCharacter(
		TEXT("/Game/Characters/U29_ProgressMixed"),
		{
			1 << 0,
			(1 << 0) | RetiredCompletionBit,
			UPaper2DPlusCharacterProfileAsset::LiveTaskBits | RetiredCompletionBit
		});
	Mixed->DisplayName = TEXT("Progress Mixed Hero");
	int32 MixedDone = -1;
	int32 MixedTotal = -1;
	Mixed->GetAuthoringProgress(MixedDone, MixedTotal);
	TestEqual(TEXT("Total is one whole checklist per animation"),
		MixedTotal, 3 * UPaper2DPlusCharacterProfileAsset::LiveTaskCount);
	TestEqual(TEXT("Done is the popcount of live ticks and ignores retired bits"),
		MixedDone, 1 + 1 + UPaper2DPlusCharacterProfileAsset::LiveTaskCount);

	// ---- Tag round trip ----
	const FAssetData MixedTags = CatalogProgress_MakeExportedAssetData(Mixed);
	int32 ExportedDone = -1;
	int32 ExportedTotal = -1;
	TestTrue(TEXT("The Profile exports a done tag"),
		MixedTags.GetTagValue<int32>(Paper2DPlusAuthoringProgress::DoneTag(), ExportedDone));
	TestTrue(TEXT("The Profile exports a total tag"),
		MixedTags.GetTagValue<int32>(Paper2DPlusAuthoringProgress::TotalTag(), ExportedTotal));
	TestEqual(TEXT("The exported done tag equals the live aggregate"), ExportedDone, MixedDone);
	TestEqual(TEXT("The exported total tag equals the live aggregate"), ExportedTotal, MixedTotal);
	TestTrue(TEXT("The exported pair passes the shared pair validator"),
		Paper2DPlusAuthoringProgress::IsAuthoringProgressPairValid(ExportedDone, ExportedTotal));
	FString ExportedDisplayName;
	TestTrue(TEXT("Progress tags never displace the existing display-name tag"),
		MixedTags.GetTagValue<FString>(
			FName(TEXT("Paper2DPlus.CharacterDisplayName")), ExportedDisplayName));
	TestEqual(TEXT("The display-name tag still carries the authored identity"),
		ExportedDisplayName, FString(TEXT("Progress Mixed Hero")));

	// Ticking one more criterion must move the exported number, or the round trip above proves nothing.
	Mixed->Flipbooks[0].EditorMeta.CompletionFlags =
		UPaper2DPlusCharacterProfileAsset::LiveTaskBits;
	int32 TickedDone = -1;
	int32 TickedTotal = -1;
	Mixed->GetAuthoringProgress(TickedDone, TickedTotal);
	TestNotEqual(TEXT("Ticking a criterion really changed the live aggregate"), TickedDone, MixedDone);
	const FAssetData TickedTags = CatalogProgress_MakeExportedAssetData(Mixed);
	int32 ReExportedDone = -1;
	TestTrue(TEXT("The re-exported done tag is present"),
		TickedTags.GetTagValue<int32>(Paper2DPlusAuthoringProgress::DoneTag(), ReExportedDone));
	TestEqual(TEXT("The exported tag tracks the live checklist, never a stale snapshot"),
		ReExportedDone, TickedDone);

	// ---- Model rows: tags alone for unloaded characters, and honest "unknown" for bad pairs ----
	UPaper2DPlusCharacterCatalogAsset* Catalog = NewAsset<UPaper2DPlusCharacterCatalogAsset>(
		TEXT("/Game/Paper2DPlusTests/U29/ProgressCatalog"));
	const FCatalogProgressUnloadedCharacter Partial = CatalogProgress_MakeUnloadedCharacter(
		TEXT("/Game/Characters/U29_ProgressUnloadedPartial"), TEXT("4"), TEXT("10"));
	const FCatalogProgressUnloadedCharacter Finished = CatalogProgress_MakeUnloadedCharacter(
		TEXT("/Game/Characters/U29_ProgressUnloadedFinished"), TEXT("10"), TEXT("10"));
	const FCatalogProgressUnloadedCharacter NoTags = CatalogProgress_MakeUnloadedCharacter(
		TEXT("/Game/Characters/U29_ProgressUnloadedNoTags"), nullptr, nullptr);
	const FCatalogProgressUnloadedCharacter DoneOnly = CatalogProgress_MakeUnloadedCharacter(
		TEXT("/Game/Characters/U29_ProgressUnloadedDoneOnly"), TEXT("4"), nullptr);
	const FCatalogProgressUnloadedCharacter ZeroTotal = CatalogProgress_MakeUnloadedCharacter(
		TEXT("/Game/Characters/U29_ProgressUnloadedZeroTotal"), TEXT("0"), TEXT("0"));
	const FCatalogProgressUnloadedCharacter GarbageTotal = CatalogProgress_MakeUnloadedCharacter(
		TEXT("/Game/Characters/U29_ProgressUnloadedGarbageTotal"), TEXT("4"), TEXT("not-a-number"));
	const FCatalogProgressUnloadedCharacter GarbageDone = CatalogProgress_MakeUnloadedCharacter(
		TEXT("/Game/Characters/U29_ProgressUnloadedGarbageDone"), TEXT("not-a-number"), TEXT("10"));
	const FCatalogProgressUnloadedCharacter Overflowing = CatalogProgress_MakeUnloadedCharacter(
		TEXT("/Game/Characters/U29_ProgressUnloadedOverflowing"), TEXT("11"), TEXT("10"));

	// A resident source must beat its own registry tags: tags only refresh on SAVE, so a designer ticking
	// criteria in an open workspace must not see the Catalog contradict the panel in front of them.
	UPaper2DPlusCharacterProfileAsset* Resident = CatalogProgress_MakeCharacter(
		TEXT("/Game/Characters/U29_ProgressResident"), { 1 << 0, (1 << 0) | (1 << 1) });
	// A Character plus one assigned companion: the row total must span BOTH sources.
	UPaper2DPlusCharacterProfileAsset* Aggregated = CatalogProgress_MakeCharacter(
		TEXT("/Game/Characters/U29_ProgressAggregated"), { (1 << 0) | (1 << 1) });
	UPaper2DPlusCharacterLayerAsset* AggregatedLayer = NewAsset<UPaper2DPlusCharacterLayerAsset>(
		TEXT("/Game/Layers/U29_ProgressAggregatedLayer"));
	AggregatedLayer->EditorCompletionFlags = (1 << 0) | (1 << 1) | (1 << 2);
	FPaper2DPlusCharacterCatalogEntry AggregatedEntry = MakeEntry(Aggregated);
	AggregatedEntry.LayerProfile = AggregatedLayer;

	TArray<FSoftObjectPath> UnloadedPaths = {
		Partial.Path, Finished.Path, NoTags.Path, DoneOnly.Path,
		ZeroTotal.Path, GarbageTotal.Path, GarbageDone.Path, Overflowing.Path };
	for (const FSoftObjectPath& UnloadedPath : UnloadedPaths)
	{
		TestNull(
			*FString::Printf(TEXT("Progress fixture %s starts unresolved"), *UnloadedPath.ToString()),
			UnloadedPath.ResolveObject());
		FPaper2DPlusCharacterCatalogEntry UnloadedEntry;
		UnloadedEntry.CharacterProfile =
			TSoftObjectPtr<UPaper2DPlusCharacterProfileAsset>(UnloadedPath);
		Catalog->Entries.Add(UnloadedEntry);
	}
	Catalog->Entries.Add(MakeEntry(Resident));
	Catalog->Entries.Add(AggregatedEntry);
	Catalog->GetOutermost()->SetDirtyFlag(false);

	TSharedRef<TArray<FAssetData>> Assets = MakeShared<TArray<FAssetData>>();
	*Assets = {
		Partial.AssetData, Finished.AssetData, NoTags.AssetData, DoneOnly.AssetData,
		ZeroTotal.AssetData, GarbageTotal.AssetData, GarbageDone.AssetData, Overflowing.AssetData,
		// STALE tags claiming a finished checklist, on an asset that is resident and only 3/10 done.
		CatalogProgress_MakeResidentProgressAssetData(Resident, TEXT("10"), TEXT("10")),
		MakeAssetData(Aggregated),
		MakeAssetData(AggregatedLayer, true, FSoftObjectPath(Aggregated)) };
	TSharedRef<FPaper2DPlusCharacterCatalogSettingsSnapshot> Settings =
		MakeShared<FPaper2DPlusCharacterCatalogSettingsSnapshot>();
	Settings->DefaultCatalog = Catalog;
	TSharedPtr<FCharacterCatalogEditorModel> Model = MakeModel(Catalog, Assets, Settings);
	TestEqual(TEXT("Every progress fixture projects a row"),
		Model->GetRows().Num(), UnloadedPaths.Num() + 2);

	const int32 ProfileChecklist = Paper2DPlusAuthoringProgress::ProfileChecklistCriteriaCount;
	const TArray<FCatalogProgressExpectation> Expectations = {
		{ Partial.Path, TEXT("an unloaded partially-ticked character"), true, 4, 10, 0 },
		{ Finished.Path, TEXT("an unloaded fully-ticked character"), true, 10, 10, 0 },
		// Absent tags: the asset has not been resaved since progress tags shipped.
		{ NoTags.Path, TEXT("an unloaded character with no progress tags"), false, 0, 0, 1 },
		{ DoneOnly.Path, TEXT("an unloaded character missing its total tag"), false, 0, 0, 1 },
		// A well-formed "nothing to author" report answers without a denominator, so it can never fake
		// completeness.
		{ ZeroTotal.Path, TEXT("an unloaded character reporting a zero total"), false, 0, 0, 0 },
		{ GarbageTotal.Path, TEXT("an unloaded character with a non-numeric total"), false, 0, 0, 1 },
		// A non-numeric DONE degrades to zero progress against a real denominator, which is safe: it can
		// read as "no work done", never as finished.
		{ GarbageDone.Path, TEXT("an unloaded character with a non-numeric done"), true, 0, 10, 0 },
		{ Overflowing.Path, TEXT("an unloaded character claiming more done than total"), false, 0, 0, 1 },
		{ FSoftObjectPath(Resident), TEXT("a resident character with stale complete tags"),
			true, 3, 2 * UPaper2DPlusCharacterProfileAsset::LiveTaskCount, 0 },
		{ FSoftObjectPath(Aggregated), TEXT("a character plus its assigned Layer"),
			true, 2 + 3, UPaper2DPlusCharacterProfileAsset::LiveTaskCount + ProfileChecklist, 0 } };

	for (const FCatalogProgressExpectation& Expectation : Expectations)
	{
		const TSharedPtr<FPaper2DPlusCharacterCatalogEditorRow> Row =
			Model->FindRow(Expectation.Path);
		if (!TestTrue(*FString::Printf(TEXT("%s projects a row"), *Expectation.What), Row.IsValid()))
		{
			continue;
		}
		const FPaper2DPlusCatalogAuthoringProgress& Progress = Row->AuthoringProgress;
		if (Expectation.bKnown)
		{
			TestTrue(
				*FString::Printf(TEXT("%s reports usable progress data"), *Expectation.What),
				Progress.IsKnown());
			TestEqual(*FString::Printf(TEXT("%s reports the expected done count"), *Expectation.What),
				Progress.Done, Expectation.Done);
			TestEqual(*FString::Printf(TEXT("%s reports the expected total"), *Expectation.What),
				Progress.Total, Expectation.Total);
			if (Expectation.Done >= Expectation.Total)
			{
				TestTrue(
					*FString::Printf(TEXT("%s reads as fully authored"), *Expectation.What),
					Progress.IsFullyAuthored());
			}
			else
			{
				TestFalse(
					*FString::Printf(TEXT("%s does not read as fully authored"), *Expectation.What),
					Progress.IsFullyAuthored());
			}
		}
		else
		{
			TestFalse(
				*FString::Printf(TEXT("%s reports no usable progress data"), *Expectation.What),
				Progress.IsKnown());
			TestFalse(
				*FString::Printf(TEXT("%s is never presented as fully authored"), *Expectation.What),
				Progress.IsFullyAuthored());
		}
		TestEqual(
			*FString::Printf(TEXT("%s counts its non-reporting sources"), *Expectation.What),
			Progress.SourcesMissingData, Expectation.SourcesMissingData);
	}

	if (const TSharedPtr<FPaper2DPlusCharacterCatalogEditorRow> AggregatedRow =
		Model->FindRow(FSoftObjectPath(Aggregated)))
	{
		TestEqual(TEXT("Both the Character and its assigned Layer report as sources"),
			AggregatedRow->AuthoringProgress.SourcesWithData, 2);
		TestEqual(TEXT("The aggregated fraction spans both sources"),
			AggregatedRow->AuthoringProgress.GetFraction(), 0.5f);
	}
	if (const TSharedPtr<FPaper2DPlusCharacterCatalogEditorRow> ResidentRow =
		Model->FindRow(FSoftObjectPath(Resident)))
	{
		TestFalse(TEXT("Stale complete tags never override a resident partial checklist"),
			ResidentRow->AuthoringProgress.IsFullyAuthored());
	}

	TestFalse(TEXT("Projecting authoring progress never dirties the Catalog"),
		Catalog->GetOutermost()->IsDirty());
	for (const FSoftObjectPath& UnloadedPath : UnloadedPaths)
	{
		TestNull(
			*FString::Printf(TEXT("Reading tag progress never loads %s"), *UnloadedPath.ToString()),
			UnloadedPath.ResolveObject());
	}
	Model->Shutdown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCatalogAuthoringProgressFilterTest,
	"Paper2DPlus.CharacterCatalog.Editor.CompletionFiltersNeverReadMissingProgressAsComplete",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPaper2DPlusCatalogAuthoringProgressFilterTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterCatalogAsset* Catalog = NewAsset<UPaper2DPlusCharacterCatalogAsset>(
		TEXT("/Game/Paper2DPlusTests/U29/ProgressFilterCatalog"));
	UPaper2DPlusCharacterProfileAsset* Authored = CatalogProgress_MakeCharacter(
		TEXT("/Game/Characters/U29_FilterAuthored"),
		{ UPaper2DPlusCharacterProfileAsset::LiveTaskBits });
	UPaper2DPlusCharacterProfileAsset* PartlyAuthored = CatalogProgress_MakeCharacter(
		TEXT("/Game/Characters/U29_FilterPartial"), { (1 << 0) | (1 << 1) });
	// A profile with no animations answers honestly that it has nothing to author, which is NOT the same
	// as being finished.
	UPaper2DPlusCharacterProfileAsset* NothingToAuthor = CatalogProgress_MakeCharacter(
		TEXT("/Game/Characters/U29_FilterNothingToAuthor"), {});
	// The unmigrated case: an unloaded asset whose registry data says nothing at all.
	const FCatalogProgressUnloadedCharacter NoData = CatalogProgress_MakeUnloadedCharacter(
		TEXT("/Game/Characters/U29_FilterNoData"), nullptr, nullptr);
	// If this ever resolved, its live checklist would answer and the Unknown assertion below would pass
	// for the wrong reason.
	TestNull(TEXT("The no-data fixture really is unresolved"), NoData.Path.ResolveObject());

	Catalog->Entries = { MakeEntry(Authored), MakeEntry(PartlyAuthored), MakeEntry(NothingToAuthor) };
	FPaper2DPlusCharacterCatalogEntry NoDataEntry;
	NoDataEntry.CharacterProfile = TSoftObjectPtr<UPaper2DPlusCharacterProfileAsset>(NoData.Path);
	Catalog->Entries.Add(NoDataEntry);
	Catalog->GetOutermost()->SetDirtyFlag(false);

	TSharedRef<TArray<FAssetData>> Assets = MakeShared<TArray<FAssetData>>();
	*Assets = {
		MakeAssetData(Authored), MakeAssetData(PartlyAuthored),
		MakeAssetData(NothingToAuthor), NoData.AssetData };
	TSharedRef<FPaper2DPlusCharacterCatalogSettingsSnapshot> Settings =
		MakeShared<FPaper2DPlusCharacterCatalogSettingsSnapshot>();
	Settings->DefaultCatalog = Catalog;
	TSharedPtr<FCharacterCatalogEditorModel> Model = MakeModel(Catalog, Assets, Settings);

	// Built by hand rather than TSet(TArray): 5.8 injects TSet via a define whose deduction guide makes
	// the TSet(TArray<>) form a compile error on current MSVC.
	auto VisiblePaths = [&Model]()
	{
		TSet<FSoftObjectPath> Paths;
		for (const FSoftObjectPath& Path : CatalogRail_VisibleOrder(Model))
		{
			Paths.Add(Path);
		}
		return Paths;
	};

	TestEqual(TEXT("Every filter fixture projects a row"), Model->GetRows().Num(), 4);
	TestEqual(TEXT("The unfiltered roster shows all four rows"), VisiblePaths().Num(), 4);

	Model->SetCompletionFilter(EPaper2DPlusCatalogCompletionFilter::Complete);
	const TSet<FSoftObjectPath> CompleteRows = VisiblePaths();
	TestEqual(TEXT("Complete matches only the fully-ticked character"), CompleteRows.Num(), 1);
	TestTrue(TEXT("Complete matches the fully-ticked character"),
		CompleteRows.Contains(FSoftObjectPath(Authored)));
	// This is the bug the rework fixes: required-companion completion was vacuously true for every row
	// whose requirements were unconfigured, so a Catalog with no checklist data read as finished.
	TestFalse(TEXT("Complete never matches a row with no progress data at all"),
		CompleteRows.Contains(NoData.Path));
	TestFalse(TEXT("Complete never matches a row that reports nothing to author"),
		CompleteRows.Contains(FSoftObjectPath(NothingToAuthor)));
	TestFalse(TEXT("Complete never matches a partially-ticked row"),
		CompleteRows.Contains(FSoftObjectPath(PartlyAuthored)));

	Model->SetCompletionFilter(EPaper2DPlusCatalogCompletionFilter::Incomplete);
	const TSet<FSoftObjectPath> IncompleteRows = VisiblePaths();
	TestEqual(TEXT("Incomplete matches only known-but-partial rows"), IncompleteRows.Num(), 1);
	TestTrue(TEXT("Incomplete matches the partially-ticked character"),
		IncompleteRows.Contains(FSoftObjectPath(PartlyAuthored)));
	TestFalse(TEXT("Incomplete never claims an unknown row is partially done"),
		IncompleteRows.Contains(NoData.Path));

	Model->SetCompletionFilter(EPaper2DPlusCatalogCompletionFilter::Unknown);
	const TSet<FSoftObjectPath> UnknownRows = VisiblePaths();
	TestEqual(TEXT("Unknown matches exactly the rows with no usable progress data"),
		UnknownRows.Num(), 2);
	TestTrue(TEXT("Unknown surfaces the unmigrated row"), UnknownRows.Contains(NoData.Path));
	TestTrue(TEXT("Unknown surfaces the nothing-to-author row"),
		UnknownRows.Contains(FSoftObjectPath(NothingToAuthor)));

	Model->SetCompletionFilter(EPaper2DPlusCatalogCompletionFilter::Any);
	TestEqual(TEXT("Clearing the completion filter restores every row"), VisiblePaths().Num(), 4);
	TestFalse(TEXT("Completion filtering never dirties the Catalog"),
		Catalog->GetOutermost()->IsDirty());
	Model->Shutdown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCatalogEditorTransactionsTest,
	"Paper2DPlus.CharacterCatalog.Editor.RequirementsRelationshipsTagsAndGroupsUndoRedo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPaper2DPlusCatalogEditorTransactionsTest::RunTest(const FString& Parameters)
{
	if (!GEditor)
	{
		AddError(TEXT("GEditor is required for transaction coverage."));
		return false;
	}
	UPaper2DPlusCharacterCatalogAsset* Catalog = NewAsset<UPaper2DPlusCharacterCatalogAsset>(
		TEXT("/Game/Paper2DPlusTests/U28/TransactionCatalog"));
	UPaper2DPlusCharacterProfileAsset* A = NewAsset<UPaper2DPlusCharacterProfileAsset>(TEXT("/Game/Characters/U28_TransactionA"));
	UPaper2DPlusCharacterProfileAsset* B = NewAsset<UPaper2DPlusCharacterProfileAsset>(TEXT("/Game/Characters/U28_TransactionB"));
	UPaper2DPlusCharacterLayerAsset* Layer = NewAsset<UPaper2DPlusCharacterLayerAsset>(TEXT("/Game/Characters/U28_TransactionLayer"));
	Layer->BaseProfile = A;
	// A starts with its Layer companion ASSIGNED so the clear below has something real to clear:
	// nothing proposes an assignment any more, and SetCompanionAssignment refuses a no-op write.
	FPaper2DPlusCharacterCatalogEntry EntryA = MakeEntry(A);
	EntryA.LayerProfile = Layer;
	Catalog->Entries.Add(EntryA);
	Catalog->Entries.Add(MakeEntry(B));
	Catalog->GetOutermost()->SetDirtyFlag(false);
	TSharedRef<TArray<FAssetData>> Assets = MakeShared<TArray<FAssetData>>();
	*Assets = { MakeAssetData(A), MakeAssetData(B), MakeAssetData(Layer, true, FSoftObjectPath(A)) };
	TSharedRef<FPaper2DPlusCharacterCatalogSettingsSnapshot> Settings = MakeShared<FPaper2DPlusCharacterCatalogSettingsSnapshot>();
	Settings->DefaultCatalog = Catalog;
	TSharedPtr<FCharacterCatalogEditorModel> Model = MakeModel(Catalog, Assets, Settings);

	GEditor->ResetTransaction(FText::FromString(TEXT("U28 direct Catalog edits reset")));
	TestTrue(TEXT("Requirement edit succeeds"), Model->SetRequirement(FSoftObjectPath(A), EPaper2DPlusCatalogCompanion::Layer, true));
	TestTrue(TEXT("Chooser can explicitly clear an authored assignment"), Model->SetCompanionAssignment(FSoftObjectPath(A), EPaper2DPlusCatalogCompanion::Layer, FSoftObjectPath()));
	TestTrue(TEXT("Cleared assignment reaches the saved entry"), Catalog->Entries[0].LayerProfile.IsNull());
	TestTrue(TEXT("Chooser assignment succeeds"), Model->SetCompanionAssignment(FSoftObjectPath(A), EPaper2DPlusCatalogCompanion::Layer, FSoftObjectPath(Layer)));
	FGameplayTagContainer Tags;
	Tags.AddTag(Paper2DPlusAnimationTags::Combat_Heavy);
	TestTrue(TEXT("Tag edit succeeds"), Model->SetTags(FSoftObjectPath(A), Tags));
	FText Error;
	TestTrue(TEXT("First unique group succeeds"), Model->AddGroup(TEXT("Squad"), FText::FromString(TEXT("Squad")), Error));
	TestTrue(TEXT("Second unique group succeeds"), Model->AddGroup(TEXT("Reserve"), FText::FromString(TEXT("Reserve")), Error));
	TestFalse(TEXT("Duplicate group is rejected"), Model->AddGroup(TEXT("squad"), FText(), Error));
	TestTrue(TEXT("Explicit group order changes"), Model->MoveGroup(TEXT("Reserve"), -1));
	TestTrue(TEXT("A joins Squad"), Model->AddGroupMember(TEXT("Squad"), FSoftObjectPath(A)));
	TestTrue(TEXT("B joins the same Squad"), Model->AddGroupMember(TEXT("Squad"), FSoftObjectPath(B)));
	TestTrue(TEXT("Member order changes"), Model->MoveGroupMember(TEXT("Squad"), FSoftObjectPath(B), -1));
	TestEqual(TEXT("B is first after move"), Catalog->Groups.FindByPredicate([](const auto& G){ return G.GroupName == FName(TEXT("Squad")); })->Members[0].ToSoftObjectPath(), FSoftObjectPath(B));
	TestTrue(TEXT("Undo member reorder succeeds"), GEditor->UndoTransaction(true));
	TestEqual(TEXT("Undo restores A first"), Catalog->Groups.FindByPredicate([](const auto& G){ return G.GroupName == FName(TEXT("Squad")); })->Members[0].ToSoftObjectPath(), FSoftObjectPath(A));
	TestTrue(TEXT("Redo member reorder succeeds"), GEditor->RedoTransaction());
	TestEqual(TEXT("Redo restores B first"), Catalog->Groups.FindByPredicate([](const auto& G){ return G.GroupName == FName(TEXT("Squad")); })->Members[0].ToSoftObjectPath(), FSoftObjectPath(B));
	GEditor->ResetTransaction(FText::FromString(TEXT("U28 transactions complete")));
	Model->Shutdown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCatalogEditorDirectManipulationTest,
	"Paper2DPlus.CharacterCatalog.Editor.DirectAddRemoveStickyUndoRedo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPaper2DPlusCatalogEditorDirectManipulationTest::RunTest(const FString& Parameters)
{
	if (!GEditor)
	{
		AddError(TEXT("GEditor is required for direct Catalog add/remove transaction coverage."));
		return false;
	}
	UPaper2DPlusCharacterCatalogAsset* Catalog = NewAsset<UPaper2DPlusCharacterCatalogAsset>(
		TEXT("/Game/Paper2DPlusTests/U28/DirectManipulationCatalog"));
	UPaper2DPlusCharacterProfileAsset* Seeded = NewAsset<UPaper2DPlusCharacterProfileAsset>(
		TEXT("/Game/Characters/U28_DirectSeeded"));
	UPaper2DPlusCharacterProfileAsset* First = NewAsset<UPaper2DPlusCharacterProfileAsset>(
		TEXT("/Game/Characters/U28_DirectFirst"));
	UPaper2DPlusCharacterProfileAsset* Second = NewAsset<UPaper2DPlusCharacterProfileAsset>(
		TEXT("/Game/Characters/U28_DirectSecond"));
	Seeded->DisplayName = TEXT("Hero Seeded");
	First->DisplayName = TEXT("Hero First");
	Second->DisplayName = TEXT("Hero Second");
	Catalog->Entries.Add(MakeEntry(Seeded));
	Catalog->GetOutermost()->SetDirtyFlag(false);

	TSharedRef<TArray<FAssetData>> Assets = MakeShared<TArray<FAssetData>>();
	*Assets = { MakeAssetData(Seeded), MakeAssetData(First), MakeAssetData(Second) };
	TSharedRef<FPaper2DPlusCharacterCatalogSettingsSnapshot> Settings =
		MakeShared<FPaper2DPlusCharacterCatalogSettingsSnapshot>();
	Settings->DefaultCatalog = Catalog;
	TSharedPtr<FCharacterCatalogEditorModel> Model = MakeModel(Catalog, Assets, Settings);

	TestTrue(TEXT("A saved character reports Catalog membership"),
		Model->IsCharacterInCatalog(FSoftObjectPath(Seeded)));
	TestFalse(TEXT("A project character that was never added reports no membership"),
		Model->IsCharacterInCatalog(FSoftObjectPath(First)));
	TestFalse(TEXT("A null path is never a member"), Model->IsCharacterInCatalog(FSoftObjectPath()));

	GEditor->ResetTransaction(FText::FromString(TEXT("U28 direct intake start")));
	TestEqual(TEXT("Intake skips null, in-input duplicate, and already-saved paths"),
		Model->AddCharacters({
			FSoftObjectPath(First),
			FSoftObjectPath(),
			FSoftObjectPath(First),
			FSoftObjectPath(Seeded),
			FSoftObjectPath(Second) }),
		2);
	TestEqual(TEXT("Intake appends exactly the two new entries"), Catalog->Entries.Num(), 3);
	TestEqual(TEXT("Intake preserves the pre-existing roster position"),
		Catalog->Entries[0].CharacterProfile.ToSoftObjectPath(), FSoftObjectPath(Seeded));
	TestEqual(TEXT("New entries land at the END of Entries in input order"),
		Catalog->Entries[1].CharacterProfile.ToSoftObjectPath(), FSoftObjectPath(First));
	TestEqual(TEXT("The second new entry follows the first"),
		Catalog->Entries[2].CharacterProfile.ToSoftObjectPath(), FSoftObjectPath(Second));
	TestEqual(TEXT("Intake selects the last added character"),
		Model->GetSelectedCharacterPath(), FSoftObjectPath(Second));
	TestTrue(TEXT("Added characters report membership"),
		Model->IsCharacterInCatalog(FSoftObjectPath(First))
			&& Model->IsCharacterInCatalog(FSoftObjectPath(Second)));
	TestTrue(TEXT("Intake dirties the Catalog"), Catalog->GetOutermost()->IsDirty());
	// A wholly redundant intake must open no transaction at all, or the undo below would consume it
	// instead of the real add.
	TestEqual(TEXT("A wholly redundant intake is a no-op"),
		Model->AddCharacters({ FSoftObjectPath(Seeded), FSoftObjectPath() }), 0);
	TestTrue(TEXT("One undo reverts the whole multi-character intake"), GEditor->UndoTransaction(true));
	Model->RefreshAfterExternalMutation();
	TestEqual(TEXT("Undo restores the original roster"), Catalog->Entries.Num(), 1);
	TestTrue(TEXT("One redo reapplies the whole multi-character intake"), GEditor->RedoTransaction());
	Model->RefreshAfterExternalMutation();
	TestEqual(TEXT("Redo restores both added entries"), Catalog->Entries.Num(), 3);

	// Two groups, because a removal that pruned only the FIRST membership would still pass a
	// single-group assertion, and because undo must restore the authored member POSITION rather than
	// merely re-append the character.
	FText GroupError;
	TestTrue(TEXT("Squad can be created"),
		Model->AddGroup(TEXT("Squad"), FText::FromString(TEXT("Squad")), GroupError));
	TestTrue(TEXT("Seeded joins Squad"), Model->AddGroupMember(TEXT("Squad"), FSoftObjectPath(Seeded)));
	TestTrue(TEXT("First joins Squad"), Model->AddGroupMember(TEXT("Squad"), FSoftObjectPath(First)));
	TestTrue(TEXT("Second joins Squad"), Model->AddGroupMember(TEXT("Squad"), FSoftObjectPath(Second)));
	TestTrue(TEXT("Reserve can be created"),
		Model->AddGroup(TEXT("Reserve"), FText::FromString(TEXT("Reserve")), GroupError));
	TestTrue(TEXT("First also joins Reserve"), Model->AddGroupMember(TEXT("Reserve"), FSoftObjectPath(First)));

	TestTrue(TEXT("The removal target can be selected"), Model->SelectCharacter(FSoftObjectPath(First)));
	TSharedRef<SCharacterCatalogDetailsPanel> Details =
		SNew(SCharacterCatalogDetailsPanel).Model(Model);
	TestTrue(TEXT("Selected Character Details exposes the removal action"),
		Details->GetRemoveCharacterButtonForTests().IsValid());

	FText ConfirmationText;
	Details->SetRemoveCharacterConfirmationForTests([&ConfirmationText](const FText& Message)
	{
		ConfirmationText = Message;
		return false;
	});
	TestFalse(TEXT("Canceling the confirmation refuses removal"),
		Details->RequestSelectedCharacterRemovalForTests());
	TestTrue(TEXT("Confirmation names the selected character"),
		ConfirmationText.ToString().Contains(TEXT("Hero First")));
	TestTrue(TEXT("Confirmation states that referenced assets are retained"),
		ConfirmationText.ToString().Contains(TEXT("will be kept")));
	TestFalse(TEXT("Confirmation no longer promises a Scan Project round trip"),
		ConfirmationText.ToString().Contains(TEXT("Scan Project"), ESearchCase::IgnoreCase));
	TestEqual(TEXT("Canceled removal leaves the roster unchanged"), Catalog->Entries.Num(), 3);
	TestEqual(TEXT("Canceled removal leaves group membership unchanged"),
		Catalog->Groups[0].Members.Num(), 3);

	GEditor->ResetTransaction(FText::FromString(TEXT("U28 direct removal start")));
	Details->SetRemoveCharacterConfirmationForTests([](const FText&) { return true; });
	TestTrue(TEXT("Confirmed removal succeeds"), Details->RequestSelectedCharacterRemovalForTests());
	TestEqual(TEXT("Confirmed removal deletes only that entry"), Catalog->Entries.Num(), 2);
	TestFalse(TEXT("Removed character no longer reports membership"),
		Model->IsCharacterInCatalog(FSoftObjectPath(First)));
	TestEqual(TEXT("Surviving entries keep their authored order"),
		Catalog->Entries[0].CharacterProfile.ToSoftObjectPath(), FSoftObjectPath(Seeded));
	TestEqual(TEXT("The later survivor stays in place"),
		Catalog->Entries[1].CharacterProfile.ToSoftObjectPath(), FSoftObjectPath(Second));
	TestEqual(TEXT("Removal prunes the character from its first group"),
		Catalog->Groups[0].Members.Num(), 2);
	TestEqual(TEXT("Removal prunes EVERY group membership, not just the first"),
		Catalog->Groups[1].Members.Num(), 0);
	TestEqual(TEXT("Selection advances to a surviving neighbor"),
		Model->GetSelectedCharacterPath(), FSoftObjectPath(Second));
	TestTrue(TEXT("Catalog becomes dirty after confirmed removal"), Catalog->GetOutermost()->IsDirty());

	// Removal STICKS: the Character Profile asset is deliberately still in the injected project
	// snapshot, and no refresh may resurrect a row for it.
	Model->RefreshFromSources();
	TestTrue(TEXT("The removed character's own asset is still discoverable in the project"),
		Assets->ContainsByPredicate([First](const FAssetData& AssetData)
		{
			return FProfileRelationshipService::GetAssetObjectPath(AssetData) == FSoftObjectPath(First);
		}));
	TestFalse(TEXT("Refresh never re-adds a removed character"),
		Model->FindRow(FSoftObjectPath(First)).IsValid());
	TestEqual(TEXT("Refresh keeps only the surviving rows"), Model->GetRows().Num(), 2);
	TestFalse(TEXT("Invalid removal is a no-op"), Model->RemoveCharacter(FSoftObjectPath()));
	TestFalse(TEXT("Removing an already-absent character is a no-op"),
		Model->RemoveCharacter(FSoftObjectPath(First)));

	TestTrue(TEXT("One undo restores the complete removal"), GEditor->UndoTransaction(true));
	Model->RefreshAfterExternalMutation();
	TestEqual(TEXT("Undo restores the removed entry"), Catalog->Entries.Num(), 3);
	TestEqual(TEXT("Undo restores the authored roster position"),
		Catalog->Entries[1].CharacterProfile.ToSoftObjectPath(), FSoftObjectPath(First));
	TestEqual(TEXT("Undo restores the authored member position, not an append"),
		Catalog->Groups[0].Members[1].ToSoftObjectPath(), FSoftObjectPath(First));
	TestEqual(TEXT("Undo restores the second group membership too"),
		Catalog->Groups[1].Members.Num(), 1);
	TestTrue(TEXT("Undo makes the character visible again"),
		Model->FindRow(FSoftObjectPath(First)).IsValid());
	TestTrue(TEXT("Redo removes the character again"), GEditor->RedoTransaction());
	Model->RefreshAfterExternalMutation();
	TestEqual(TEXT("Redo restores the reduced roster"), Catalog->Entries.Num(), 2);
	TestFalse(TEXT("Redo makes the character invisible again"),
		Model->FindRow(FSoftObjectPath(First)).IsValid());
	TestEqual(TEXT("Redo re-empties the second group"), Catalog->Groups[1].Members.Num(), 0);

	TestEqual(TEXT("Intake restores a three-character roster for multi-select coverage"),
		Model->AddCharacters({ FSoftObjectPath(First) }), 1);
	if (FSlateApplication::IsInitialized())
	{
		GEditor->ResetTransaction(FText::FromString(TEXT("U28 multi-select removal start")));
		TSharedRef<SCharacterCatalogRosterPanel> Roster =
			SNew(SCharacterCatalogRosterPanel).Model(Model);
		TestTrue(TEXT("Roster builds its multi-select card surface"), Roster->HasCardSurfaceForTests());
		Roster->SetTileSelectionForTests({ FSoftObjectPath(Seeded), FSoftObjectPath(Second) });
		FText MultiConfirmation;
		Roster->RemoveSelectedConfirmation = [&MultiConfirmation](const FText& Message)
		{
			MultiConfirmation = Message;
			return true;
		};
		TestTrue(TEXT("Multi-select removal succeeds"), Roster->RemoveSelectedCharactersForTests());
		TestTrue(TEXT("Multi-select confirmation states how many characters leave the Catalog"),
			MultiConfirmation.ToString().Contains(TEXT("2 characters")));
		TestTrue(TEXT("Multi-select confirmation still promises referenced assets are kept"),
			MultiConfirmation.ToString().Contains(TEXT("will be kept")));
		TestEqual(TEXT("Multi-select removal deletes both selected characters"),
			Catalog->Entries.Num(), 1);
		TestEqual(TEXT("Multi-select removal keeps the unselected character"),
			Catalog->Entries[0].CharacterProfile.ToSoftObjectPath(), FSoftObjectPath(First));
		TestTrue(TEXT("One undo restores both multi-select removals"), GEditor->UndoTransaction(true));
		Model->RefreshAfterExternalMutation();
		TestEqual(TEXT("A single transaction covered the whole multi-select removal"),
			Catalog->Entries.Num(), 3);
		TestTrue(TEXT("Redo reapplies the multi-select removal"), GEditor->RedoTransaction());
		Model->RefreshAfterExternalMutation();
		TestEqual(TEXT("Redo restores the single surviving character"), Catalog->Entries.Num(), 1);

		TestTrue(TEXT("The Add lens hides a character already saved in this Catalog"),
			Roster->ShouldHideAddCandidateForTests(MakeAssetData(First)));
		TestFalse(TEXT("The Add lens shows a character that is not in this Catalog"),
			Roster->ShouldHideAddCandidateForTests(MakeAssetData(Seeded)));
		Roster->SetShowOnlyNewCharactersForTests(false);
		TestFalse(TEXT("Clearing the lens hides nothing at all"),
			Roster->ShouldHideAddCandidateForTests(MakeAssetData(First)));
	}
	else
	{
		Paper2DPlusTestSkip::Mark(
			*this,
			TEXT("Character Catalog multi-select roster removal and Add-picker lens"),
			TEXT("Slate is not initialized in this host, so the card surface cannot be built"));
	}

	Model->Shutdown();
	GEditor->ResetTransaction(FText::FromString(TEXT("U28 direct add/remove end")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCatalogEditorStatusAccessibilityTest,
	"Paper2DPlus.CharacterCatalog.Editor.StatusPresentationIsTextTooltipAndKeyboardReachable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPaper2DPlusCatalogEditorStatusAccessibilityTest::RunTest(const FString& Parameters)
{
	TSet<FString> StatusTexts;
	for (uint8 Raw = static_cast<uint8>(EPaper2DPlusCatalogRowStatus::Complete);
		Raw <= static_cast<uint8>(EPaper2DPlusCatalogRowStatus::Error); ++Raw)
	{
		FText Text;
		FText Tooltip;
		FCharacterCatalogEditorModel::GetStatusPresentation(static_cast<EPaper2DPlusCatalogRowStatus>(Raw), Text, Tooltip);
		TestFalse(TEXT("Every status has non-color text"), Text.IsEmpty());
		TestFalse(TEXT("Every status explains itself in a tooltip"), Tooltip.IsEmpty());
		StatusTexts.Add(Text.ToString());
	}
	// Complete, OptionalMissing, RequiredMissing, MissingAsset, Warning, Error — the direct-manipulation
	// roster has no Ambiguous/Mismatched/Deleted/PendingSync states left to project.
	TestEqual(TEXT("Every status has a distinct text label"), StatusTexts.Num(), 6);

	if (FSlateApplication::IsInitialized())
	{
		UPaper2DPlusCharacterCatalogAsset* Catalog = NewAsset<UPaper2DPlusCharacterCatalogAsset>(TEXT("/Game/Paper2DPlusTests/U28/AccessibleCatalog"));
		UPaper2DPlusCharacterProfileAsset* Character = NewAsset<UPaper2DPlusCharacterProfileAsset>(TEXT("/Game/Characters/U28_Accessible"));
		Catalog->Entries.Add(MakeEntry(Character));
		TSharedRef<TArray<FAssetData>> Assets = MakeShared<TArray<FAssetData>>();
		TSharedRef<FPaper2DPlusCharacterCatalogSettingsSnapshot> Settings = MakeShared<FPaper2DPlusCharacterCatalogSettingsSnapshot>();
		TSharedPtr<FCharacterCatalogEditorModel> Model = MakeModel(Catalog, Assets, Settings);
		TSharedRef<SCharacterCatalogRosterPanel> Roster = SNew(SCharacterCatalogRosterPanel).Model(Model);
		TSharedRef<SCharacterCatalogDetailsPanel> Details = SNew(SCharacterCatalogDetailsPanel).Model(Model);
		TestTrue(TEXT("Roster supports keyboard focus"), Roster->SupportsKeyboardFocus());
		TestTrue(TEXT("Selected status is represented by a focusable Details control"), Details->GetStatusFocusTargetForTests().IsValid());
		TestTrue(TEXT("Selected character uses the dedicated Details surface"), Details->HasDetailsSurfaceForTests());
		Model->Shutdown();
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCatalogEditorRelationshipsTest,
	"Paper2DPlus.CharacterCatalog.Editor.IssueNavigationAndSharedRelatedProfileOwnership",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPaper2DPlusCatalogEditorRelationshipsTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterCatalogAsset* Catalog = NewAsset<UPaper2DPlusCharacterCatalogAsset>(TEXT("/Game/Paper2DPlusTests/U28/RelatedCatalog"));
	UPaper2DPlusCharacterProfileAsset* Character = NewAsset<UPaper2DPlusCharacterProfileAsset>(TEXT("/Game/Characters/U28_RelatedCharacter"));
	UPaper2DPlusCharacterProfileAsset* WrongCharacter = NewAsset<UPaper2DPlusCharacterProfileAsset>(TEXT("/Game/Characters/U28_WrongCharacter"));
	UPaper2DPlusCharacterLayerAsset* Layer = NewAsset<UPaper2DPlusCharacterLayerAsset>(TEXT("/Game/Characters/U28_RelatedLayer"));
	UPaper2DPlusEffectProfileAsset* Effect = NewAsset<UPaper2DPlusEffectProfileAsset>(TEXT("/Game/Characters/U28_RelatedEffect"));
	UPaper2DPlusCombatProfileAsset* Combat = NewAsset<UPaper2DPlusCombatProfileAsset>(TEXT("/Game/Characters/U28_RelatedCombat"));
	Layer->BaseProfile = Character;
	Combat->CharacterProfile = Character;
	Effect->CharacterProfile = WrongCharacter; // deprecated inward field must be ignored by the new bar
	FPaper2DPlusCharacterCatalogEntry Entry = MakeEntry(Character);
	Entry.LayerProfile = Layer;
	Entry.EffectProfile = Effect;
	Entry.CombatProfile = Combat;
	Catalog->Entries.Add(Entry);

	const FPaper2DPlusRelatedProfileContext LayerContext = SRelatedProfileBar::BuildContext(Layer, Catalog);
	const FPaper2DPlusRelatedProfileContext CombatContext = SRelatedProfileBar::BuildContext(Combat, Catalog);
	const FPaper2DPlusRelatedProfileContext EffectContext = SRelatedProfileBar::BuildContext(Effect, Catalog);
	TestEqual(TEXT("Layer uses its inward Character relationship"), LayerContext.CharacterPath, FSoftObjectPath(Character));
	TestEqual(TEXT("Combat uses its inward Character relationship"), CombatContext.CharacterPath, FSoftObjectPath(Character));
	TestEqual(TEXT("Effect uses only the Catalog's forward assignment"), EffectContext.CharacterPath, FSoftObjectPath(Character));
	TestNotEqual(TEXT("Deprecated Effect back-link is ignored"), EffectContext.CharacterPath, FSoftObjectPath(WrongCharacter));

	UPaper2DPlusCharacterCatalogAsset* CreateCatalog = NewAsset<UPaper2DPlusCharacterCatalogAsset>(TEXT("/Game/Paper2DPlusTests/U28/CreateRelatedCatalog"));
	CreateCatalog->Entries.Add(MakeEntry(Character));
	TSharedRef<TArray<FAssetData>> Assets = MakeShared<TArray<FAssetData>>();
	TSharedRef<FPaper2DPlusCharacterCatalogSettingsSnapshot> Settings = MakeShared<FPaper2DPlusCharacterCatalogSettingsSnapshot>();
	TSharedPtr<FCharacterCatalogEditorModel> Model = MakeModel(CreateCatalog, Assets, Settings);
	UPaper2DPlusEffectProfileAsset* CreatedEffect = nullptr;
	Model->SetCreateAssetActionForTests([&CreatedEffect](EPaper2DPlusCatalogCompanion Companion, const FSoftObjectPath&, const FString&, const FString&, FText&)
	{
		if (Companion != EPaper2DPlusCatalogCompanion::Effect) return static_cast<UObject*>(nullptr);
		CreatedEffect = NewAsset<UPaper2DPlusEffectProfileAsset>(TEXT("/Game/Characters/U28_CreatedEffect"));
		return static_cast<UObject*>(CreatedEffect);
	});
	FText Error;
	TestNotNull(TEXT("Effect create action returns an asset"), Model->CreateCompanion(FSoftObjectPath(Character), EPaper2DPlusCatalogCompanion::Effect, TEXT("/Game/Characters"), TEXT("CreatedEffect"), Error));
	TestTrue(TEXT("Created Effect is assigned to Catalog slot"), CreateCatalog->Entries[0].EffectProfile.Get() == CreatedEffect);
	TestNull(TEXT("Created Effect receives no inward Character link"), CreatedEffect ? CreatedEffect->CharacterProfile.Get() : nullptr);

	FPaper2DPlusValidationIssue Issue;
	Issue.CharacterPath = FSoftObjectPath(Character);
	Issue.Field = TEXT("EffectProfile");
	FPaper2DPlusValidationToolTarget Target;
	Target.AssetPath = FSoftObjectPath(CreateCatalog);
	Target.ToolId = TEXT("CharacterCatalog");
	Target.TabId = TEXT("CharacterCatalogEditor_Roster");
	Target.ItemIdentity = FSoftObjectPath(Character).ToString();
	Target.Field = Issue.Field;
	Issue.ToolTarget = Target;
	Model->SetSearchText(TEXT("does-not-match-this-character"));
	TestEqual(TEXT("The active filter initially hides the warning target"), Model->GetVisibleRows().Num(), 0);
	TestTrue(TEXT("Issue activation selects row/field"), Model->ActivateIssue(Issue));
	TestEqual(TEXT("Issue selects character by path"), Model->GetSelectedCharacterPath(), FSoftObjectPath(Character));
	TestEqual(TEXT("Issue activation reveals a row hidden by Catalog filters"), Model->GetVisibleRows().Num(), 1);
	TestTrue(TEXT("Issue navigation clears obstructing roster filters"), Model->GetFilters().IsDefault());
	TestEqual(TEXT("Issue exposes target field"), Model->GetLastNavigationField(), FName(TEXT("EffectProfile")));
	bool bOpenedExternalTarget = false;
	FSoftObjectPath OpenedTarget;
	Model->SetOpenAssetActionForTests([&bOpenedExternalTarget, &OpenedTarget](const FSoftObjectPath& Path, FText&)
	{
		bOpenedExternalTarget = true;
		OpenedTarget = Path;
		return true;
	});
	Issue.ToolTarget->AssetPath = FSoftObjectPath(Effect);
	// Give the resolver a REAL losing candidate. GetNavigationAssetPath() is a ternary that prefers
	// ToolTarget->AssetPath and falls back to Issue.AssetPath; with Issue.AssetPath left unset the
	// fallback can never be chosen, so asserting the result equals what we just assigned to
	// ToolTarget->AssetPath proves nothing. Pointing Issue.AssetPath at the Catalog makes the
	// assertion below discriminating: it now fails if that precedence ever inverts and navigation
	// starts sending the designer to the Catalog instead of the affected external asset.
	Issue.AssetPath = FSoftObjectPath(CreateCatalog);
	FText OpenIssueError;
	// The external-owner half of issue navigation resolves in every process, but actually opening an
	// asset editor is deliberately fail-closed in a headless one (proved by
	// CharacterCatalog.Editor.HeadlessOpenGuardAndSourceDelegateLifecycle), so assert the routing
	// unconditionally and the opening per process capability.
	//
	// COVERAGE GAP (TASK-142 follow-up): the render-capable branch below - that activation actually
	// reaches the opener with the external owner's path - therefore does NOT execute under -nullrhi,
	// which is what the headless merge gate runs. The behaviour is only exercised by a render-capable
	// pass. Do not read a green headless suite as proof that issue activation opens the right asset.
	TestTrue(TEXT("Actionable warning is activatable"), Issue.CanActivate());
	TestEqual(TEXT("Actionable warning resolves the affected Effect Profile, not the owning Catalog, as its navigation target"),
		Issue.GetNavigationAssetPath(), FSoftObjectPath(Effect));
	const bool bIssueOpened = Model->OpenIssueTarget(Issue, OpenIssueError);
	if (FApp::CanEverRender())
	{
		TestTrue(TEXT("Actionable warning opens its external owning asset"), bIssueOpened);
		TestTrue(TEXT("External warning invokes the asset opener"), bOpenedExternalTarget);
		TestEqual(TEXT("Warning opens the affected Effect Profile"), OpenedTarget, FSoftObjectPath(Effect));
	}
	else
	{
		TestFalse(TEXT("Headless warning activation is rejected before any opener runs"), bIssueOpened);
		TestFalse(TEXT("Headless warning never reaches the asset opener"), bOpenedExternalTarget);
		TestTrue(TEXT("Headless warning rejection names the headless guard, not a missing target"),
			OpenIssueError.ToString().Contains(TEXT("headless"), ESearchCase::IgnoreCase));
	}
	FName OpenedSettingsSection;
	Model->SetOpenSettingsActionForTests([&OpenedSettingsSection](FName Section)
	{
		OpenedSettingsSection = Section;
		return true;
	});
	FPaper2DPlusValidationIssue SettingsIssue;
	SettingsIssue.AssetPath = FSoftObjectPath(Catalog);
	SettingsIssue.ToolTarget = FPaper2DPlusValidationToolTarget{
		FSoftObjectPath(Catalog),
		TEXT("Paper2DPlusSettings"),
		NAME_None,
		FString(),
		NAME_None };
	TestTrue(TEXT("Settings warning opens the relevant Project Settings section"),
		Model->OpenIssueTarget(SettingsIssue, OpenIssueError));
	TestEqual(TEXT("Paper2DPlus warning routes to Paper2DPlus settings"),
		OpenedSettingsSection, FName(TEXT("Paper2DPlus")));
	SettingsIssue.ToolTarget->ToolId = TEXT("AssetManagerSettings");
	TestTrue(TEXT("Cook-readiness warning opens Asset Manager Project Settings"),
		Model->OpenIssueTarget(SettingsIssue, OpenIssueError));
	TestEqual(TEXT("Cook-readiness warning routes to Asset Manager settings"),
		OpenedSettingsSection, FName(TEXT("AssetManager")));

	// The Groups TAB is retired: the rail lives inside the Roster tab, so a group warning must land the
	// designer there. That makes the tab id no longer a discriminator between a character warning and a
	// group warning — ToolId is, and it has to stay one or activation cannot tell them apart.
	FPaper2DPlusValidationIssue GroupIssue;
	GroupIssue.Scope = TEXT("Playable");
	GroupIssue.Field = TEXT("Members");
	GroupIssue.ToolTarget = FPaper2DPlusValidationToolTarget{
		FSoftObjectPath(Catalog),
		TEXT("CharacterCatalogGroups"),
		TEXT("CharacterCatalogEditor_Roster"),
		TEXT("Playable"),
		TEXT("Members") };
	TestTrue(TEXT("Group warning activation keeps non-asset group identity navigable"), Model->ActivateIssue(GroupIssue));
	TestEqual(TEXT("Group warning targets the Roster tab that now hosts the Groups rail"),
		Model->GetLastNavigationTab(), FName(TEXT("CharacterCatalogEditor_Roster")));
	TestEqual(TEXT("Group and character warnings now share one tab, so the tab alone cannot separate them"),
		Model->GetLastNavigationTab(), Target.TabId);
	TestEqual(TEXT("Group warnings stay distinguishable from character warnings by ToolId"),
		GroupIssue.ToolTarget->ToolId, FName(TEXT("CharacterCatalogGroups")));
	TestNotEqual(TEXT("The group ToolId is not the character-scoped Catalog ToolId"),
		GroupIssue.ToolTarget->ToolId, Target.ToolId);
	Model->Shutdown();
	return true;
}

/**
 * Readable relationship-state names.
 *
 * EPaper2DPlusProfileRelationshipState is an enum class, so TestEqual would need an int32 cast and a
 * failure would read "expected 5, got 6". The split between ResolveAssigned and SuggestCandidate is
 * exactly a question of WHICH state comes back, so the assertion messages have to name them.
 */
FString CatalogSuggest_StateName(EPaper2DPlusProfileRelationshipState State)
{
	switch (State)
	{
	case EPaper2DPlusProfileRelationshipState::None: return TEXT("None");
	case EPaper2DPlusProfileRelationshipState::Unique: return TEXT("Unique");
	case EPaper2DPlusProfileRelationshipState::Ambiguous: return TEXT("Ambiguous");
	case EPaper2DPlusProfileRelationshipState::LegacyUnknown: return TEXT("LegacyUnknown");
	case EPaper2DPlusProfileRelationshipState::ManualValid: return TEXT("ManualValid");
	case EPaper2DPlusProfileRelationshipState::ManualMismatch: return TEXT("ManualMismatch");
	case EPaper2DPlusProfileRelationshipState::MissingAssignedAsset: return TEXT("MissingAssignedAsset");
	default: return TEXT("<unhandled>");
	}
}

/** Companion slot names, so a wrongly reported slot names itself instead of printing an ordinal. */
FString CatalogSuggest_SlotNames(const TArray<EPaper2DPlusCatalogCompanion>& Slots)
{
	TArray<FString> Names;
	for (const EPaper2DPlusCatalogCompanion Slot : Slots)
	{
		if (Slot == EPaper2DPlusCatalogCompanion::Layer)
		{
			Names.Add(FString(TEXT("Layer")));
		}
		else if (Slot == EPaper2DPlusCatalogCompanion::Effect)
		{
			Names.Add(FString(TEXT("Effect")));
		}
		else
		{
			Names.Add(FString(TEXT("Combat")));
		}
	}
	return Names.IsEmpty() ? FString(TEXT("<none>")) : FString::Join(Names, TEXT(","));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCatalogSuggestCompanionsTest,
	"Paper2DPlus.CharacterCatalog.Editor.SuggestCompanionsFillsOnlyUniqueEmptySlots",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPaper2DPlusCatalogSuggestCompanionsTest::RunTest(const FString& Parameters)
{
	if (!GEditor)
	{
		AddError(TEXT("GEditor is required for Suggest Companions transaction coverage."));
		return false;
	}

	// ---------------------------------------------------------------------------------------------
	// 1. UNIQUE FILL: both empty slots have exactly one inward match, and the whole run is ONE undo
	//    unit. Effect is present, inward-tagged, and must still never be suggested.
	// ---------------------------------------------------------------------------------------------
	{
		UPaper2DPlusCharacterCatalogAsset* Catalog = NewAsset<UPaper2DPlusCharacterCatalogAsset>(
			TEXT("/Game/Paper2DPlusTests/U31/SuggestUniqueCatalog"));
		UPaper2DPlusCharacterProfileAsset* Hero = NewAsset<UPaper2DPlusCharacterProfileAsset>(
			TEXT("/Game/Characters/U31_SuggestHero"));
		UPaper2DPlusCharacterProfileAsset* Other = NewAsset<UPaper2DPlusCharacterProfileAsset>(
			TEXT("/Game/Characters/U31_SuggestOther"));
		UPaper2DPlusCharacterLayerAsset* HeroLayer = NewAsset<UPaper2DPlusCharacterLayerAsset>(
			TEXT("/Game/Characters/U31_SuggestHeroLayer"));
		UPaper2DPlusCombatProfileAsset* HeroCombat = NewAsset<UPaper2DPlusCombatProfileAsset>(
			TEXT("/Game/Characters/U31_SuggestHeroCombat"));
		// A rival Layer belonging to a DIFFERENT character. Without it, "picked the only Layer in the
		// project" and "picked the Layer that points back at THIS character" are the same assertion.
		UPaper2DPlusCharacterLayerAsset* OtherLayer = NewAsset<UPaper2DPlusCharacterLayerAsset>(
			TEXT("/Game/Characters/U31_SuggestOtherLayer"));
		// Deliberately carries the inward relationship tag AND the deprecated back-link: Effect must be
		// excluded because it is not a suggestable slot, not merely because it happens to be untagged.
		UPaper2DPlusEffectProfileAsset* HeroEffect = NewAsset<UPaper2DPlusEffectProfileAsset>(
			TEXT("/Game/Characters/U31_SuggestHeroEffect"));
		HeroEffect->CharacterProfile = Hero;

		Catalog->Entries.Add(MakeEntry(Hero));
		Catalog->GetOutermost()->SetDirtyFlag(false);
		TSharedRef<TArray<FAssetData>> Assets = MakeShared<TArray<FAssetData>>();
		*Assets = {
			MakeAssetData(Hero),
			MakeAssetData(Other),
			MakeAssetData(HeroLayer, true, FSoftObjectPath(Hero)),
			MakeAssetData(HeroCombat, true, FSoftObjectPath(Hero)),
			MakeAssetData(OtherLayer, true, FSoftObjectPath(Other)),
			MakeAssetData(HeroEffect, true, FSoftObjectPath(Hero)) };
		TSharedRef<FPaper2DPlusCharacterCatalogSettingsSnapshot> Settings =
			MakeShared<FPaper2DPlusCharacterCatalogSettingsSnapshot>();
		Settings->DefaultCatalog = Catalog;
		TSharedPtr<FCharacterCatalogEditorModel> Model = MakeModel(Catalog, Assets, Settings);

		GEditor->ResetTransaction(FText::FromString(TEXT("U31 suggest unique start")));
		FPaper2DPlusCatalogCompanionSuggestion Result;
		TestTrue(TEXT("Suggest Companions runs for a saved character"),
			Model->SuggestCompanions(FSoftObjectPath(Hero), Result));
		TestEqual(TEXT("Both empty slots with exactly one inward match are filled"),
			Result.NumAssigned, 2);
		TestTrue(TEXT("A fully resolved run reports it did something"), Result.DidAnything());
		TestEqual(TEXT("The Layer slot receives the Layer that points back at THIS character"),
			Catalog->Entries[0].LayerProfile.ToSoftObjectPath(), FSoftObjectPath(HeroLayer));
		TestEqual(TEXT("The Combat slot receives this character's Combat Profile"),
			Catalog->Entries[0].CombatProfile.ToSoftObjectPath(), FSoftObjectPath(HeroCombat));
		TestEqual(TEXT("A fully resolved run leaves nothing ambiguous"),
			CatalogSuggest_SlotNames(Result.AmbiguousSlots), FString(TEXT("<none>")));
		TestEqual(TEXT("A fully resolved run leaves nothing unmatched"),
			CatalogSuggest_SlotNames(Result.UnmatchedSlots), FString(TEXT("<none>")));
		TestEqual(TEXT("A fully resolved run leaves nothing legacy-uncertain"),
			CatalogSuggest_SlotNames(Result.LegacySlots), FString(TEXT("<none>")));

		// 5. EFFECT IS NEVER SUGGESTED — not filled, and not even reported as a slot that was tried.
		TestTrue(TEXT("The Effect slot is never filled by suggestion"),
			Catalog->Entries[0].EffectProfile.IsNull());
		TestFalse(TEXT("Effect is not reported ambiguous, because it is not a suggestable slot"),
			Result.AmbiguousSlots.Contains(EPaper2DPlusCatalogCompanion::Effect));
		TestFalse(TEXT("Effect is not reported unmatched, because it is not a suggestable slot"),
			Result.UnmatchedSlots.Contains(EPaper2DPlusCatalogCompanion::Effect));
		TestFalse(TEXT("Effect is not reported legacy, because it is not a suggestable slot"),
			Result.LegacySlots.Contains(EPaper2DPlusCatalogCompanion::Effect));
		TestTrue(TEXT("Suggestion dirties the Catalog"), Catalog->GetOutermost()->IsDirty());

		TestTrue(TEXT("ONE undo reverts the whole suggestion run"), GEditor->UndoTransaction(true));
		Model->RefreshAfterExternalMutation();
		TestTrue(TEXT("One undo clears the suggested Layer assignment"),
			Catalog->Entries[0].LayerProfile.IsNull());
		TestTrue(TEXT("One undo clears the suggested Combat assignment too, so it was one transaction"),
			Catalog->Entries[0].CombatProfile.IsNull());
		TestTrue(TEXT("Redo reapplies the suggestion run"), GEditor->RedoTransaction());
		Model->RefreshAfterExternalMutation();
		TestEqual(TEXT("Redo restores the Layer assignment"),
			Catalog->Entries[0].LayerProfile.ToSoftObjectPath(), FSoftObjectPath(HeroLayer));
		TestEqual(TEXT("Redo restores the Combat assignment"),
			Catalog->Entries[0].CombatProfile.ToSoftObjectPath(), FSoftObjectPath(HeroCombat));

		Model->Shutdown();
		GEditor->ResetTransaction(FText::FromString(TEXT("U31 suggest unique end")));
	}

	// ---------------------------------------------------------------------------------------------
	// 2. NEVER OVERWRITES: an occupied slot is skipped before it is even resolved, so it is neither
	//    reassigned nor reported. Only the empty slot is filled.
	// ---------------------------------------------------------------------------------------------
	{
		UPaper2DPlusCharacterCatalogAsset* Catalog = NewAsset<UPaper2DPlusCharacterCatalogAsset>(
			TEXT("/Game/Paper2DPlusTests/U31/SuggestKeepCatalog"));
		UPaper2DPlusCharacterProfileAsset* Hero = NewAsset<UPaper2DPlusCharacterProfileAsset>(
			TEXT("/Game/Characters/U31_KeepHero"));
		UPaper2DPlusCharacterProfileAsset* Other = NewAsset<UPaper2DPlusCharacterProfileAsset>(
			TEXT("/Game/Characters/U31_KeepOther"));
		// The authored Layer is a real, loadable Layer asset that simply belongs to somebody else. That
		// makes the "left alone" assertion sharp: suggestion has both a reason and a candidate to
		// replace it with, and must still refuse.
		UPaper2DPlusCharacterLayerAsset* AuthoredLayer = NewAsset<UPaper2DPlusCharacterLayerAsset>(
			TEXT("/Game/Characters/U31_KeepAuthoredLayer"));
		UPaper2DPlusCharacterLayerAsset* HeroLayer = NewAsset<UPaper2DPlusCharacterLayerAsset>(
			TEXT("/Game/Characters/U31_KeepHeroLayer"));
		UPaper2DPlusCombatProfileAsset* HeroCombat = NewAsset<UPaper2DPlusCombatProfileAsset>(
			TEXT("/Game/Characters/U31_KeepHeroCombat"));

		FPaper2DPlusCharacterCatalogEntry Entry = MakeEntry(Hero);
		Entry.LayerProfile = AuthoredLayer;
		Catalog->Entries.Add(Entry);
		Catalog->GetOutermost()->SetDirtyFlag(false);
		TSharedRef<TArray<FAssetData>> Assets = MakeShared<TArray<FAssetData>>();
		*Assets = {
			MakeAssetData(Hero),
			MakeAssetData(Other),
			MakeAssetData(AuthoredLayer, true, FSoftObjectPath(Other)),
			MakeAssetData(HeroLayer, true, FSoftObjectPath(Hero)),
			MakeAssetData(HeroCombat, true, FSoftObjectPath(Hero)) };
		TSharedRef<FPaper2DPlusCharacterCatalogSettingsSnapshot> Settings =
			MakeShared<FPaper2DPlusCharacterCatalogSettingsSnapshot>();
		Settings->DefaultCatalog = Catalog;
		TSharedPtr<FCharacterCatalogEditorModel> Model = MakeModel(Catalog, Assets, Settings);

		const FSoftObjectPath LayerBefore = Catalog->Entries[0].LayerProfile.ToSoftObjectPath();
		FPaper2DPlusCatalogCompanionSuggestion Result;
		TestTrue(TEXT("Suggest Companions runs with one slot already authored"),
			Model->SuggestCompanions(FSoftObjectPath(Hero), Result));
		TestEqual(TEXT("Only the EMPTY slot is filled"), Result.NumAssigned, 1);
		TestEqual(TEXT("The authored Layer assignment is left byte-identical"),
			Catalog->Entries[0].LayerProfile.ToSoftObjectPath(), LayerBefore);
		TestEqual(TEXT("The authored Layer is still the other character's Layer, not the inward match"),
			Catalog->Entries[0].LayerProfile.ToSoftObjectPath(), FSoftObjectPath(AuthoredLayer));
		TestNotEqual(TEXT("Suggestion never replaces an authored assignment with its own candidate"),
			Catalog->Entries[0].LayerProfile.ToSoftObjectPath(), FSoftObjectPath(HeroLayer));
		TestEqual(TEXT("The empty Combat slot is filled from its unique inward match"),
			Catalog->Entries[0].CombatProfile.ToSoftObjectPath(), FSoftObjectPath(HeroCombat));
		// An occupied slot is skipped BEFORE resolution, so it must not surface in any report bucket —
		// reporting it would tell the designer to act on a slot they already decided.
		TestFalse(TEXT("An occupied slot is not reported ambiguous"),
			Result.AmbiguousSlots.Contains(EPaper2DPlusCatalogCompanion::Layer));
		TestFalse(TEXT("An occupied slot is not reported unmatched"),
			Result.UnmatchedSlots.Contains(EPaper2DPlusCatalogCompanion::Layer));
		TestFalse(TEXT("An occupied slot is not reported legacy-uncertain"),
			Result.LegacySlots.Contains(EPaper2DPlusCatalogCompanion::Layer));
		Model->Shutdown();
	}

	// ---------------------------------------------------------------------------------------------
	// 3. AMBIGUITY NEVER GUESSES: two inward matches leave the slot NULL and report it, while a
	//    sibling slot that IS unique still gets filled in the same run.
	// ---------------------------------------------------------------------------------------------
	{
		UPaper2DPlusCharacterCatalogAsset* Catalog = NewAsset<UPaper2DPlusCharacterCatalogAsset>(
			TEXT("/Game/Paper2DPlusTests/U31/SuggestAmbiguousCatalog"));
		UPaper2DPlusCharacterProfileAsset* Hero = NewAsset<UPaper2DPlusCharacterProfileAsset>(
			TEXT("/Game/Characters/U31_AmbiguousHero"));
		UPaper2DPlusCharacterLayerAsset* LayerA = NewAsset<UPaper2DPlusCharacterLayerAsset>(
			TEXT("/Game/Characters/U31_AmbiguousLayerA"));
		UPaper2DPlusCharacterLayerAsset* LayerB = NewAsset<UPaper2DPlusCharacterLayerAsset>(
			TEXT("/Game/Characters/U31_AmbiguousLayerB"));
		UPaper2DPlusCombatProfileAsset* HeroCombat = NewAsset<UPaper2DPlusCombatProfileAsset>(
			TEXT("/Game/Characters/U31_AmbiguousHeroCombat"));

		Catalog->Entries.Add(MakeEntry(Hero));
		Catalog->GetOutermost()->SetDirtyFlag(false);
		TSharedRef<TArray<FAssetData>> Assets = MakeShared<TArray<FAssetData>>();
		*Assets = {
			MakeAssetData(Hero),
			MakeAssetData(LayerA, true, FSoftObjectPath(Hero)),
			MakeAssetData(LayerB, true, FSoftObjectPath(Hero)),
			MakeAssetData(HeroCombat, true, FSoftObjectPath(Hero)) };
		TSharedRef<FPaper2DPlusCharacterCatalogSettingsSnapshot> Settings =
			MakeShared<FPaper2DPlusCharacterCatalogSettingsSnapshot>();
		Settings->DefaultCatalog = Catalog;
		TSharedPtr<FCharacterCatalogEditorModel> Model = MakeModel(Catalog, Assets, Settings);

		FPaper2DPlusCatalogCompanionSuggestion Result;
		TestTrue(TEXT("Suggest Companions runs with an ambiguous slot present"),
			Model->SuggestCompanions(FSoftObjectPath(Hero), Result));
		TestTrue(TEXT("An ambiguous Layer slot stays NULL rather than taking either candidate"),
			Catalog->Entries[0].LayerProfile.IsNull());
		TestEqual(TEXT("The ambiguous slot is reported by name"),
			CatalogSuggest_SlotNames(Result.AmbiguousSlots), FString(TEXT("Layer")));
		TestEqual(TEXT("An ambiguous slot is not counted as assigned; only the unique sibling is"),
			Result.NumAssigned, 1);
		TestEqual(TEXT("The unique sibling slot is still filled in the same run"),
			Catalog->Entries[0].CombatProfile.ToSoftObjectPath(), FSoftObjectPath(HeroCombat));
		TestFalse(TEXT("An ambiguous slot is not also reported as unmatched"),
			Result.UnmatchedSlots.Contains(EPaper2DPlusCatalogCompanion::Layer));
		Model->Shutdown();
	}

	// ---------------------------------------------------------------------------------------------
	// 4. NO MATCH opens NO transaction, and 6. a character with no saved entry is refused outright.
	// ---------------------------------------------------------------------------------------------
	{
		UPaper2DPlusCharacterCatalogAsset* Catalog = NewAsset<UPaper2DPlusCharacterCatalogAsset>(
			TEXT("/Game/Paper2DPlusTests/U31/SuggestNoMatchCatalog"));
		UPaper2DPlusCharacterProfileAsset* Hero = NewAsset<UPaper2DPlusCharacterProfileAsset>(
			TEXT("/Game/Characters/U31_NoMatchHero"));
		// Never added to the Catalog: the "not in this Catalog" refusal case.
		UPaper2DPlusCharacterProfileAsset* Stranger = NewAsset<UPaper2DPlusCharacterProfileAsset>(
			TEXT("/Game/Characters/U31_NoMatchStranger"));
		// Both companions exist and are correctly tagged — just to somebody else. Suggestion must find
		// nothing rather than fall back to "the only Layer/Combat in the project".
		UPaper2DPlusCharacterLayerAsset* StrangerLayer = NewAsset<UPaper2DPlusCharacterLayerAsset>(
			TEXT("/Game/Characters/U31_NoMatchStrangerLayer"));
		UPaper2DPlusCombatProfileAsset* StrangerCombat = NewAsset<UPaper2DPlusCombatProfileAsset>(
			TEXT("/Game/Characters/U31_NoMatchStrangerCombat"));

		Catalog->Entries.Add(MakeEntry(Hero));
		Catalog->GetOutermost()->SetDirtyFlag(false);
		TSharedRef<TArray<FAssetData>> Assets = MakeShared<TArray<FAssetData>>();
		*Assets = {
			MakeAssetData(Hero),
			MakeAssetData(Stranger),
			MakeAssetData(StrangerLayer, true, FSoftObjectPath(Stranger)),
			MakeAssetData(StrangerCombat, true, FSoftObjectPath(Stranger)) };
		TSharedRef<FPaper2DPlusCharacterCatalogSettingsSnapshot> Settings =
			MakeShared<FPaper2DPlusCharacterCatalogSettingsSnapshot>();
		Settings->DefaultCatalog = Catalog;
		TSharedPtr<FCharacterCatalogEditorModel> Model = MakeModel(Catalog, Assets, Settings);

		const bool bDirtyBeforeNoMatch = Catalog->GetOutermost()->IsDirty();
		const int32 MutationsBeforeNoMatch = Model->GetMutationCountForTests();
		TestFalse(TEXT("The Catalog starts clean, so a dirty flag afterwards can only be this run"),
			bDirtyBeforeNoMatch);

		FPaper2DPlusCatalogCompanionSuggestion Result;
		TestTrue(TEXT("Suggest Companions still succeeds when it finds nothing to do"),
			Model->SuggestCompanions(FSoftObjectPath(Hero), Result));
		TestEqual(TEXT("Nothing is assigned when no slot has an inward match"), Result.NumAssigned, 0);
		TestFalse(TEXT("A run that assigned nothing reports that it did nothing"), Result.DidAnything());
		TestEqual(TEXT("Both suggestable slots are reported unmatched"),
			CatalogSuggest_SlotNames(Result.UnmatchedSlots), FString(TEXT("Layer,Combat")));
		TestEqual(TEXT("Exactly the two suggestable slots are reported, never Effect"),
			Result.UnmatchedSlots.Num(), 2);
		TestTrue(TEXT("Slots without a match stay null"),
			Catalog->Entries[0].LayerProfile.IsNull() && Catalog->Entries[0].CombatProfile.IsNull());
		TestFalse(TEXT("A run that assigns nothing opens no transaction, so the Catalog stays clean"),
			Catalog->GetOutermost()->IsDirty());
		TestEqual(TEXT("A run that assigns nothing performs no mutation refresh either"),
			Model->GetMutationCountForTests(), MutationsBeforeNoMatch);

		// 6. NOT IN CATALOG: refused outright, with no report and no write.
		FPaper2DPlusCatalogCompanionSuggestion StrangerResult;
		TestFalse(TEXT("Suggest Companions refuses a character with no saved entry"),
			Model->SuggestCompanions(FSoftObjectPath(Stranger), StrangerResult));
		TestEqual(TEXT("A refused run assigns nothing"), StrangerResult.NumAssigned, 0);
		TestEqual(TEXT("A refused run reports no ambiguous slots"),
			CatalogSuggest_SlotNames(StrangerResult.AmbiguousSlots), FString(TEXT("<none>")));
		TestEqual(TEXT("A refused run reports no unmatched slots"),
			CatalogSuggest_SlotNames(StrangerResult.UnmatchedSlots), FString(TEXT("<none>")));
		TestEqual(TEXT("A refused run reports no legacy slots"),
			CatalogSuggest_SlotNames(StrangerResult.LegacySlots), FString(TEXT("<none>")));
		TestFalse(TEXT("A refused run never dirties the Catalog"), Catalog->GetOutermost()->IsDirty());
		TestEqual(TEXT("A refused run never refreshes after a mutation"),
			Model->GetMutationCountForTests(), MutationsBeforeNoMatch);
		TestTrue(TEXT("A refused run leaves the saved roster untouched"),
			Catalog->Entries.Num() == 1
				&& Catalog->Entries[0].LayerProfile.IsNull()
				&& Catalog->Entries[0].CombatProfile.IsNull()
				&& Catalog->Entries[0].EffectProfile.IsNull());
		TestFalse(TEXT("A refused run never invents an entry for the stranger"),
			Model->IsCharacterInCatalog(FSoftObjectPath(Stranger)));
		Model->Shutdown();
	}

	// ---------------------------------------------------------------------------------------------
	// 7. The Details panel seam reaches the same mutation the model API does.
	//
	// Gated on Slate only: the outcome message routes through the injected message sink below, so this
	// runs on an interactive host too instead of blocking the run on a human dismissing a modal.
	// ---------------------------------------------------------------------------------------------
	if (FSlateApplication::IsInitialized())
	{
		UPaper2DPlusCharacterCatalogAsset* Catalog = NewAsset<UPaper2DPlusCharacterCatalogAsset>(
			TEXT("/Game/Paper2DPlusTests/U31/SuggestDetailsCatalog"));
		UPaper2DPlusCharacterProfileAsset* Hero = NewAsset<UPaper2DPlusCharacterProfileAsset>(
			TEXT("/Game/Characters/U31_DetailsHero"));
		UPaper2DPlusCharacterLayerAsset* HeroLayer = NewAsset<UPaper2DPlusCharacterLayerAsset>(
			TEXT("/Game/Characters/U31_DetailsHeroLayer"));
		UPaper2DPlusCombatProfileAsset* HeroCombat = NewAsset<UPaper2DPlusCombatProfileAsset>(
			TEXT("/Game/Characters/U31_DetailsHeroCombat"));

		Catalog->Entries.Add(MakeEntry(Hero));
		Catalog->GetOutermost()->SetDirtyFlag(false);
		TSharedRef<TArray<FAssetData>> Assets = MakeShared<TArray<FAssetData>>();
		*Assets = {
			MakeAssetData(Hero),
			MakeAssetData(HeroLayer, true, FSoftObjectPath(Hero)),
			MakeAssetData(HeroCombat, true, FSoftObjectPath(Hero)) };
		TSharedRef<FPaper2DPlusCharacterCatalogSettingsSnapshot> Settings =
			MakeShared<FPaper2DPlusCharacterCatalogSettingsSnapshot>();
		Settings->DefaultCatalog = Catalog;
		TSharedPtr<FCharacterCatalogEditorModel> Model = MakeModel(Catalog, Assets, Settings);
		TestTrue(TEXT("The suggestion target can be selected"),
			Model->SelectCharacter(FSoftObjectPath(Hero)));

		TSharedRef<SCharacterCatalogDetailsPanel> Details =
			SNew(SCharacterCatalogDetailsPanel).Model(Model);
		TArray<FText> SeamMessages;
		Details->SetMessageSinkForTests([&SeamMessages](const FText& Message)
		{
			SeamMessages.Add(Message);
		});
		TestTrue(TEXT("Suggest Companions is reachable from the Details Character section"),
			Details->RequestCompanionSuggestionForTests(FSoftObjectPath(Hero)));
		// The designer must be TOLD what happened, not just have the data change under them.
		TestEqual(TEXT("The Details seam reports its outcome exactly once"), SeamMessages.Num(), 1);
		if (SeamMessages.Num() == 1)
		{
			TestTrue(TEXT("The outcome names how many companions were assigned"),
				SeamMessages[0].ToString().Contains(TEXT("2")));
		}
		TestEqual(TEXT("The Details seam reaches the same Layer assignment as the model API"),
			Catalog->Entries[0].LayerProfile.ToSoftObjectPath(), FSoftObjectPath(HeroLayer));
		TestEqual(TEXT("The Details seam reaches the same Combat assignment as the model API"),
			Catalog->Entries[0].CombatProfile.ToSoftObjectPath(), FSoftObjectPath(HeroCombat));
		TestTrue(TEXT("The Details seam still leaves Effect alone"),
			Catalog->Entries[0].EffectProfile.IsNull());
		// A second run has nothing left to fill, so the seam must report "did nothing" rather than
		// re-reporting success off the still-populated slots.
		TestFalse(TEXT("Re-running the Details seam reports that it changed nothing"),
			Details->RequestCompanionSuggestionForTests(FSoftObjectPath(Hero)));
		Model->Shutdown();
	}
	else
	{
		Paper2DPlusTestSkip::Mark(
			*this,
			FString(TEXT("Character Catalog Details Suggest Companions seam")),
			FString(TEXT("Slate is not initialized in this host, so the Details panel cannot be built")));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCatalogRelationshipResolveSplitTest,
	"Paper2DPlus.CharacterCatalog.Editor.ResolveAssignedAndSuggestCandidateAreSeparateQuestions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPaper2DPlusCatalogRelationshipResolveSplitTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Hero = NewAsset<UPaper2DPlusCharacterProfileAsset>(
		TEXT("/Game/Characters/U31_SplitHero"));
	UPaper2DPlusCharacterProfileAsset* Twin = NewAsset<UPaper2DPlusCharacterProfileAsset>(
		TEXT("/Game/Characters/U31_SplitTwin"));
	UPaper2DPlusCharacterProfileAsset* Other = NewAsset<UPaper2DPlusCharacterProfileAsset>(
		TEXT("/Game/Characters/U31_SplitOther"));
	UPaper2DPlusCharacterLayerAsset* HeroLayer = NewAsset<UPaper2DPlusCharacterLayerAsset>(
		TEXT("/Game/Characters/U31_SplitHeroLayer"));
	UPaper2DPlusCharacterLayerAsset* OtherLayer = NewAsset<UPaper2DPlusCharacterLayerAsset>(
		TEXT("/Game/Characters/U31_SplitOtherLayer"));
	UPaper2DPlusCharacterLayerAsset* TwinLayerA = NewAsset<UPaper2DPlusCharacterLayerAsset>(
		TEXT("/Game/Characters/U31_SplitTwinLayerA"));
	UPaper2DPlusCharacterLayerAsset* TwinLayerB = NewAsset<UPaper2DPlusCharacterLayerAsset>(
		TEXT("/Game/Characters/U31_SplitTwinLayerB"));
	UPaper2DPlusCombatProfileAsset* OtherCombat = NewAsset<UPaper2DPlusCombatProfileAsset>(
		TEXT("/Game/Characters/U31_SplitOtherCombat"));
	// Deliberately left OUT of the snapshot: an assignment whose asset no longer exists as its class.
	UPaper2DPlusCharacterLayerAsset* GhostLayer = NewAsset<UPaper2DPlusCharacterLayerAsset>(
		TEXT("/Game/Characters/U31_SplitGhostLayer"));

	// Every Layer/Combat row carries the relationship tag, so nothing lands in LegacyAssets. That
	// matters: a single untagged companion of the same kind would turn every SuggestCandidate answer
	// below into LegacyUnknown, and the Unique/None assertions would stop testing what they name.
	TArray<FAssetData> Assets = {
		MakeAssetData(Hero),
		MakeAssetData(Twin),
		MakeAssetData(Other),
		MakeAssetData(HeroLayer, true, FSoftObjectPath(Hero)),
		MakeAssetData(OtherLayer, true, FSoftObjectPath(Other)),
		MakeAssetData(TwinLayerA, true, FSoftObjectPath(Twin)),
		MakeAssetData(TwinLayerB, true, FSoftObjectPath(Twin)),
		MakeAssetData(OtherCombat, true, FSoftObjectPath(Other)) };
	const FPaper2DPlusProfileRelationshipIndex Index =
		FProfileRelationshipService::BuildCandidateIndex(Assets);
	TestEqual(TEXT("Every companion row is indexed as a tagged candidate"),
		Index.LayerCandidates.Num() + Index.CombatCandidates.Num(), 5);
	TestEqual(TEXT("No companion row is mistaken for legacy, which would mask Unique/None"),
		Index.LegacyAssets.Num(), 0);

	// ---- ResolveAssigned answers only "is this AUTHORED assignment right?" ----
	const FPaper2DPlusProfileRelationshipResolution Mismatch =
		FProfileRelationshipService::ResolveAssigned(
			EPaper2DPlusCatalogCompanion::Layer,
			FSoftObjectPath(Hero),
			FSoftObjectPath(OtherLayer),
			Index);
	TestEqual(TEXT("An assignment pointing at another character's companion is a mismatch"),
		CatalogSuggest_StateName(Mismatch.State), FString(TEXT("ManualMismatch")));
	TestEqual(TEXT("A mismatched assignment is still reported back verbatim"),
		Mismatch.AssignedAssetPath, FSoftObjectPath(OtherLayer));
	TestTrue(TEXT("A mismatch never clears the authored assignment"),
		Mismatch.bManualAssignmentPreserved);
	TestTrue(TEXT("ResolveAssigned never proposes a replacement; suggesting is the other question"),
		Mismatch.SuggestedAssetPath.IsNull());

	const FPaper2DPlusProfileRelationshipResolution Missing =
		FProfileRelationshipService::ResolveAssigned(
			EPaper2DPlusCatalogCompanion::Layer,
			FSoftObjectPath(Hero),
			FSoftObjectPath(GhostLayer),
			Index);
	TestEqual(TEXT("An assignment whose asset is absent is missing, not merely mismatched"),
		CatalogSuggest_StateName(Missing.State), FString(TEXT("MissingAssignedAsset")));
	TestEqual(TEXT("A missing assignment is still reported back verbatim"),
		Missing.AssignedAssetPath, FSoftObjectPath(GhostLayer));

	const FPaper2DPlusProfileRelationshipResolution Valid =
		FProfileRelationshipService::ResolveAssigned(
			EPaper2DPlusCatalogCompanion::Layer,
			FSoftObjectPath(Hero),
			FSoftObjectPath(HeroLayer),
			Index);
	TestEqual(TEXT("An assignment that points back at this character is valid"),
		CatalogSuggest_StateName(Valid.State), FString(TEXT("ManualValid")));
	TestTrue(TEXT("Even a valid assignment carries no suggestion"), Valid.SuggestedAssetPath.IsNull());

	const FPaper2DPlusProfileRelationshipResolution Unassigned =
		FProfileRelationshipService::ResolveAssigned(
			EPaper2DPlusCatalogCompanion::Layer,
			FSoftObjectPath(Hero),
			FSoftObjectPath(),
			Index);
	TestEqual(TEXT("An empty slot has no assignment to judge"),
		CatalogSuggest_StateName(Unassigned.State), FString(TEXT("None")));
	TestTrue(TEXT("Judging an empty slot still proposes nothing, because that is SuggestCandidate's job"),
		Unassigned.SuggestedAssetPath.IsNull());

	// ---- SuggestCandidate answers only "what would fill this EMPTY slot?" ----
	const FPaper2DPlusProfileRelationshipResolution Unique =
		FProfileRelationshipService::SuggestCandidate(
			EPaper2DPlusCatalogCompanion::Layer, FSoftObjectPath(Hero), Index);
	TestEqual(TEXT("Exactly one inward match is Unique"),
		CatalogSuggest_StateName(Unique.State), FString(TEXT("Unique")));
	TestEqual(TEXT("A Unique result names the asset that would be assigned"),
		Unique.SuggestedAssetPath, FSoftObjectPath(HeroLayer));
	TestTrue(TEXT("SuggestCandidate reports no assignment, because it was asked about an empty slot"),
		Unique.AssignedAssetPath.IsNull());

	const FPaper2DPlusProfileRelationshipResolution Ambiguous =
		FProfileRelationshipService::SuggestCandidate(
			EPaper2DPlusCatalogCompanion::Layer, FSoftObjectPath(Twin), Index);
	TestEqual(TEXT("Two inward matches are Ambiguous"),
		CatalogSuggest_StateName(Ambiguous.State), FString(TEXT("Ambiguous")));
	TestEqual(TEXT("Both rival candidates are reported"), Ambiguous.CandidatePaths.Num(), 2);
	TestTrue(TEXT("An Ambiguous result never guesses a winner"),
		Ambiguous.SuggestedAssetPath.IsNull());

	const FPaper2DPlusProfileRelationshipResolution NoneFound =
		FProfileRelationshipService::SuggestCandidate(
			EPaper2DPlusCatalogCompanion::Combat, FSoftObjectPath(Hero), Index);
	TestEqual(TEXT("Zero inward matches is None, even with a Combat Profile owned by someone else"),
		CatalogSuggest_StateName(NoneFound.State), FString(TEXT("None")));
	TestTrue(TEXT("A None result proposes nothing"), NoneFound.SuggestedAssetPath.IsNull());
	TestEqual(TEXT("A None result lists no candidates"), NoneFound.CandidatePaths.Num(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCatalogWarningsPanelRunRoutingTest,
	"Paper2DPlus.CharacterCatalog.Editor.WarningsPanelCustomRunAndFallback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPaper2DPlusCatalogWarningsPanelRunRoutingTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Character =
		NewAsset<UPaper2DPlusCharacterProfileAsset>(
			TEXT("/Game/Paper2DPlusTests/U13/PanelFallbackCharacter"));

	TArray<FPaper2DPlusValidationIssue> ExpectedFallbackIssues;
	const bool bSharedAdapterAvailable =
		FPaper2DPlusValidationService::Get().ValidateObject(
			Character,
			ExpectedFallbackIssues);
	if (!TestTrue(TEXT("The fallback fixture has a shared validation adapter"), bSharedAdapterAvailable))
	{
		return false;
	}

	int32 CustomRunCount = 0;
	TSharedRef<SProfileValidationPanel> CustomPanel =
		SNew(SProfileValidationPanel)
		.Asset(Character)
		.RunInitially(false)
		.RefreshOnObservedChanges(false)
		.OnCustomValidationRun(FOnPaper2DPlusCustomValidationRun::CreateLambda(
			[&CustomRunCount](TArray<FPaper2DPlusValidationIssue>& OutIssues)
			{
				++CustomRunCount;
				FPaper2DPlusValidationIssue Issue;
				Issue.Code = TEXT("Paper2DPlus.Catalog.Test.CustomRun");
				Issue.Message = FText::FromString(TEXT("Custom Catalog audit result"));
				OutIssues.Add(MoveTemp(Issue));
				return true;
			}));
	CustomPanel->RunValidationNow();
	TestEqual(TEXT("The explicit panel action calls the custom runner once"), CustomRunCount, 1);
	if (TestEqual(
		TEXT("A handled custom run is the panel's exact issue source"),
		CustomPanel->GetModel().GetIssues().Num(),
		1))
	{
		TestEqual(
			TEXT("The handled custom issue remains intact"),
			CustomPanel->GetModel().GetIssues()[0].Code,
			FName(TEXT("Paper2DPlus.Catalog.Test.CustomRun")));
	}

	int32 DeclinedRunCount = 0;
	TSharedRef<SProfileValidationPanel> FallbackPanel =
		SNew(SProfileValidationPanel)
		.Asset(Character)
		.RunInitially(false)
		.RefreshOnObservedChanges(false)
		.OnCustomValidationRun(FOnPaper2DPlusCustomValidationRun::CreateLambda(
			[&DeclinedRunCount](TArray<FPaper2DPlusValidationIssue>& OutIssues)
			{
				++DeclinedRunCount;
				FPaper2DPlusValidationIssue PartialIssue;
				PartialIssue.Code = TEXT("Paper2DPlus.Catalog.Test.DiscardMe");
				OutIssues.Add(MoveTemp(PartialIssue));
				return false;
			}));
	FallbackPanel->RunValidationNow();
	TestEqual(TEXT("A declining custom runner is still invoked once"), DeclinedRunCount, 1);
	TestEqual(
		TEXT("False falls back to the shared validation-service issue count"),
		FallbackPanel->GetModel().GetIssues().Num(),
		ExpectedFallbackIssues.Num());
	TestFalse(
		TEXT("Partial custom output is discarded before the service fallback"),
		FallbackPanel->GetModel().GetIssues().ContainsByPredicate(
			[](const FPaper2DPlusValidationIssue& Issue)
			{
				return Issue.Code == TEXT("Paper2DPlus.Catalog.Test.DiscardMe");
			}));
	for (const FPaper2DPlusValidationIssue& ExpectedIssue : ExpectedFallbackIssues)
	{
		TestTrue(
			*FString::Printf(
				TEXT("Fallback preserves shared issue %s"),
				*ExpectedIssue.StableKey),
			FallbackPanel->GetModel().GetIssues().ContainsByPredicate(
				[&ExpectedIssue](const FPaper2DPlusValidationIssue& ActualIssue)
				{
					return ActualIssue.StableKey == ExpectedIssue.StableKey;
				}));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCatalogExplicitAuditResolutionTest,
	"Paper2DPlus.CharacterCatalog.Editor.AuditResolutionIsExplicitAndPanelMatchesReport",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPaper2DPlusCatalogExplicitAuditResolutionTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterCatalogAsset* Catalog =
		NewAsset<UPaper2DPlusCharacterCatalogAsset>(
			TEXT("/Game/Paper2DPlusTests/U13/ExplicitAuditCatalog"));
	UPaper2DPlusCharacterProfileAsset* Character =
		NewAsset<UPaper2DPlusCharacterProfileAsset>(
			TEXT("/Game/Paper2DPlusTests/U13/ExplicitAuditCharacter"));
	Catalog->Entries.Add(MakeEntry(Character));

	TSharedRef<TArray<FAssetData>> Assets = MakeShared<TArray<FAssetData>>();
	Assets->Add(MakeAssetData(Character));
	TSharedRef<FPaper2DPlusCharacterCatalogSettingsSnapshot> Settings =
		MakeShared<FPaper2DPlusCharacterCatalogSettingsSnapshot>();
	Settings->DefaultCatalog = Catalog;
	int32 ResolverCallCount = 0;
	TSharedPtr<FCharacterCatalogEditorModel> Model = MakeModel(
		Catalog,
		Assets,
		Settings,
		[&ResolverCallCount](const FSoftObjectPath& Path) -> UObject*
		{
			++ResolverCallCount;
			return Path.ResolveObject();
		},
		CookReady);

	TestEqual(TEXT("Opening/initializing the Catalog resolves no roster assets"), ResolverCallCount, 0);
	TestFalse(TEXT("Opening/initializing the Catalog does not create an audit report"), Model->HasAuditReport());
	Model->SelectCharacter(FSoftObjectPath(Character));
	TestEqual(TEXT("Changing selection resolves no roster assets"), ResolverCallCount, 0);
	TestFalse(TEXT("Changing selection does not create an audit report"), Model->HasAuditReport());
	Model->RefreshFromSources();
	TestEqual(TEXT("A direct row rebuild resolves no roster assets"), ResolverCallCount, 0);
	TestFalse(TEXT("A direct row rebuild does not create an audit report"), Model->HasAuditReport());
	Model->RefreshAfterExternalMutation();
	TestEqual(TEXT("An external-modification refresh resolves no roster assets"), ResolverCallCount, 0);
	TestFalse(TEXT("An external-modification refresh does not create an audit report"), Model->HasAuditReport());

	TSharedRef<SProfileValidationPanel> WarningsPanel =
		FCharacterCatalogAssetEditorToolkit::MakeWarningsPanel(Catalog, Model);
	TestEqual(
		TEXT("The shared Warnings composition pins Check Again"),
		WarningsPanel->GetRunActionText().ToString(),
		FString(TEXT("Check Again")));
	TestEqual(
		TEXT("The shared Warnings composition pins RunInitially(false)"),
		WarningsPanel->GetValidationRunCountForTests(),
		0);
	TestEqual(TEXT("Constructing the deferred Warnings panel resolves no roster assets"), ResolverCallCount, 0);
	TestFalse(TEXT("Constructing the deferred Warnings panel creates no report"), Model->HasAuditReport());

	FCoreUObjectDelegates::OnObjectModified.Broadcast(Catalog);
	WarningsPanel->FlushPendingRefreshForTests();
	TestEqual(
		TEXT("The shared Warnings composition pins RefreshOnObservedChanges(false)"),
		WarningsPanel->GetValidationRunCountForTests(),
		0);
	TestEqual(TEXT("An observed Catalog change resolves no roster assets"), ResolverCallCount, 0);
	TestFalse(TEXT("An observed Catalog change creates no report"), Model->HasAuditReport());

	WarningsPanel->RunValidationNow();
	TestEqual(
		TEXT("The explicit Check Again route runs exactly once"),
		WarningsPanel->GetValidationRunCountForTests(),
		1);
	TestEqual(TEXT("The explicit audit resolves the one unique saved roster Profile once"), ResolverCallCount, 1);
	TestTrue(TEXT("The explicit route leaves one authoritative model report"), Model->HasAuditReport());
	TestEqual(
		TEXT("The Warnings panel and model report expose exactly one issue set"),
		WarningsPanel->GetModel().GetIssues().Num(),
		Model->GetAuditReport().Issues.Num());
	for (const FPaper2DPlusValidationIssue& ReportIssue : Model->GetAuditReport().Issues)
	{
		TestTrue(
			*FString::Printf(
				TEXT("The panel contains report issue %s"),
				*ReportIssue.StableKey),
			WarningsPanel->GetModel().GetIssues().ContainsByPredicate(
				[&ReportIssue](const FPaper2DPlusValidationIssue& PanelIssue)
				{
					return PanelIssue.StableKey == ReportIssue.StableKey;
				}));
	}

	Model->Shutdown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCatalogCoverageIssueNavigationTest,
	"Paper2DPlus.CharacterCatalog.Editor.CoverageIssueSelectsRosterRowAndTargetsProfile",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPaper2DPlusCatalogCoverageIssueNavigationTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterCatalogAsset* Catalog =
		NewAsset<UPaper2DPlusCharacterCatalogAsset>(
			TEXT("/Game/Paper2DPlusTests/U13/CoverageNavigationCatalog"));
	UPaper2DPlusCharacterProfileAsset* Character =
		NewAsset<UPaper2DPlusCharacterProfileAsset>(
			TEXT("/Game/Paper2DPlusTests/U13/CoverageNavigationCharacter"));
	Catalog->Entries.Add(MakeEntry(Character));
	TSharedRef<TArray<FAssetData>> Assets = MakeShared<TArray<FAssetData>>();
	Assets->Add(MakeAssetData(Character));
	TSharedRef<FPaper2DPlusCharacterCatalogSettingsSnapshot> Settings =
		MakeShared<FPaper2DPlusCharacterCatalogSettingsSnapshot>();
	TSharedPtr<FCharacterCatalogEditorModel> Model = MakeModel(Catalog, Assets, Settings);

	FPaper2DPlusValidationIssue Issue;
	Issue.Code = TEXT("Paper2DPlus.Catalog.Coverage.MissingExpectedTag");
	Issue.CharacterPath = FSoftObjectPath(Character);
	Issue.AssetPath = FSoftObjectPath(Catalog);
	Issue.Field = TEXT("ExpectedAnimationTags");
	Issue.ToolTarget = FPaper2DPlusValidationToolTarget{
		FSoftObjectPath(Character),
		TEXT("CharacterCatalog"),
		TEXT("CharacterCatalogEditor_Roster"),
		FSoftObjectPath(Character).ToString(),
		Issue.Field };

	Model->SetSearchText(TEXT("hide-the-coverage-row"));
	TestTrue(TEXT("The focused route starts with its row filtered out"), Model->GetVisibleRows().IsEmpty());
	TestTrue(TEXT("Activating a coverage issue reveals and selects its Catalog row"), Model->ActivateIssue(Issue));
	TestEqual(
		TEXT("Coverage activation selects the affected roster character"),
		Model->GetSelectedCharacterPath(),
		FSoftObjectPath(Character));
	TestEqual(TEXT("Coverage activation clears filters that hid the row"), Model->GetVisibleRows().Num(), 1);
	TestEqual(
		TEXT("Coverage Open targets the Profile rather than the Catalog"),
		Issue.GetNavigationAssetPath(),
		FSoftObjectPath(Character));

	bool bOpenCalled = false;
	FSoftObjectPath OpenedPath;
	Model->SetOpenAssetActionForTests(
		[&bOpenCalled, &OpenedPath](const FSoftObjectPath& Path, FText&)
		{
			bOpenCalled = true;
			OpenedPath = Path;
			return true;
		});
	FText OpenError;
	const bool bOpened = Model->OpenIssueTarget(Issue, OpenError);
	if (FApp::CanEverRender())
	{
		TestTrue(TEXT("Coverage Open reaches the asset opener"), bOpened && bOpenCalled);
		TestEqual(TEXT("Coverage Open navigates to the affected Profile"), OpenedPath, FSoftObjectPath(Character));
	}
	else
	{
		TestFalse(TEXT("Headless coverage Open remains guarded"), bOpened);
		TestFalse(TEXT("The headless guard prevents invoking the asset opener"), bOpenCalled);
	}

	Model->Shutdown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCatalogEditorReadOnlyCommandsTest,
	"Paper2DPlus.CharacterCatalog.Editor.ReadOnlyCommandsNeverDirtyExplicitEditsDo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPaper2DPlusCatalogEditorReadOnlyCommandsTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterCatalogAsset* Catalog = NewAsset<UPaper2DPlusCharacterCatalogAsset>(TEXT("/Game/Paper2DPlusTests/U28/ReadOnlyCatalog"));
	UPaper2DPlusCharacterProfileAsset* Character = NewAsset<UPaper2DPlusCharacterProfileAsset>(TEXT("/Game/Characters/U28_ReadOnly"));
	Catalog->Entries.Add(MakeEntry(Character));
	Catalog->GetOutermost()->SetDirtyFlag(false);
	TSharedRef<TArray<FAssetData>> Assets = MakeShared<TArray<FAssetData>>();
	Assets->Add(MakeAssetData(Character));
	TSharedRef<FPaper2DPlusCharacterCatalogSettingsSnapshot> Settings = MakeShared<FPaper2DPlusCharacterCatalogSettingsSnapshot>();
	Settings->DefaultCatalog = Catalog;
	TSharedPtr<FCharacterCatalogEditorModel> Model = MakeModel(Catalog, Assets, Settings,
		[](const FSoftObjectPath& Path) { return Path.ResolveObject(); }, CookReady);
	bool bOpenCalled = false;
	Model->SetOpenAssetActionForTests([&bOpenCalled](const FSoftObjectPath&, FText&) { bOpenCalled = true; return true; });
	Model->RefreshFromSources();
	Model->SetSearchText(TEXT("ReadOnly"));
	Model->SelectCharacter(FSoftObjectPath(Character));
	FText Message;
	TestTrue(TEXT("Explicit audit succeeds"), Model->RunAudit(Message));
	FText OpenError;
	Model->OpenCharacter(FSoftObjectPath(Character), OpenError);
	TestFalse(TEXT("Refresh/filter/select/audit/open do not dirty"), Catalog->GetOutermost()->IsDirty());
	TestTrue(TEXT("Explicit requirement edit succeeds"), Model->SetRequirement(FSoftObjectPath(Character), EPaper2DPlusCatalogCompanion::Combat, true));
	TestTrue(TEXT("Explicit edit dirties the Catalog"), Catalog->GetOutermost()->IsDirty());
	Model->Shutdown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCatalogEditorHeadlessLifecycleTest,
	"Paper2DPlus.CharacterCatalog.Editor.HeadlessOpenGuardAndSourceDelegateLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPaper2DPlusCatalogEditorHeadlessLifecycleTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterCatalogAsset* Catalog = NewAsset<UPaper2DPlusCharacterCatalogAsset>(TEXT("/Game/Paper2DPlusTests/U28/HeadlessGuardCatalog"));
	UPaper2DPlusCharacterProfileAsset* A = NewAsset<UPaper2DPlusCharacterProfileAsset>(TEXT("/Game/Characters/U28_HeadlessGuardA"));
	Catalog->Entries.Add(MakeEntry(A));
	Catalog->GetOutermost()->SetDirtyFlag(false);
	TSharedRef<TArray<FAssetData>> SharedAssets = MakeShared<TArray<FAssetData>>();
	SharedAssets->Add(MakeAssetData(A));
	TSharedRef<FPaper2DPlusCharacterCatalogSettingsSnapshot> SharedSettings =
		MakeShared<FPaper2DPlusCharacterCatalogSettingsSnapshot>();
	SharedSettings->DefaultCatalog = Catalog;
	TSharedPtr<FCharacterCatalogEditorModel> Model = MakeModel(Catalog, SharedAssets, SharedSettings);
	bool bOpenCalled = false;
	Model->SetOpenAssetActionForTests([&bOpenCalled](const FSoftObjectPath&, FText&) { bOpenCalled = true; return true; });
	FText OpenError;
	const bool bOpened = Model->OpenCharacter(FSoftObjectPath(A), OpenError);
	if (FApp::CanEverRender())
	{
		TestTrue(TEXT("Render-capable command reaches injected opener"), bOpened && bOpenCalled);
	}
	else
	{
		TestFalse(TEXT("Headless command is rejected"), bOpened);
		TestFalse(TEXT("Headless command never reaches opener"), bOpenCalled);
		TestTrue(TEXT("Headless rejection is explained"), OpenError.ToString().Contains(TEXT("headless"), ESearchCase::IgnoreCase));
	}
	TestEqual(TEXT("An initialized model owns its four registry plus one settings delegate"), Model->GetSourceDelegateCountForTests(), 5);
	Model->Shutdown();
	TestEqual(TEXT("Shutdown removes every source delegate"), Model->GetSourceDelegateCountForTests(), 0);
	TestFalse(TEXT("Opening and lifecycle never dirty the Catalog"), Catalog->GetOutermost()->IsDirty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCatalogSoftDefaultAccessorTest,
	"Paper2DPlus.CharacterCatalog.Editor.SoftDefaultAccessorNeverLoads",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPaper2DPlusCatalogSoftDefaultAccessorTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusSettings* MutableSettings = GetMutableDefault<UPaper2DPlusSettings>();
	const TSoftObjectPtr<UPaper2DPlusCharacterCatalogAsset> Previous = MutableSettings->DefaultCharacterCatalog;
	const FSoftObjectPath MissingCatalogPath(
		TEXT("/Game/Characters/P2DPCatalogEditor_UnloadedCatalog.P2DPCatalogEditor_UnloadedCatalog"));
	MutableSettings->DefaultCharacterCatalog =
		TSoftObjectPtr<UPaper2DPlusCharacterCatalogAsset>(MissingCatalogPath);
	TestNull(TEXT("Fixture Catalog begins unloaded"), MissingCatalogPath.ResolveObject());

	const TSoftObjectPtr<UPaper2DPlusCharacterCatalogAsset> Result =
		UPaper2DPlusBlueprintLibrary::GetDefaultCharacterCatalog();
	TestEqual(TEXT("Accessor returns typed configured soft path"), Result.ToSoftObjectPath(), MissingCatalogPath);
	TestNull(TEXT("Accessor never synchronously loads the Catalog"), MissingCatalogPath.ResolveObject());

	MutableSettings->DefaultCharacterCatalog = Previous;
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCatalogFilteredReorderTest,
	"Paper2DPlus.CharacterCatalog.Editor.ReorderDropsUseAuthoredAnchorsNotViewIndices",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPaper2DPlusCatalogFilteredReorderTest::RunTest(const FString& Parameters)
{
	if (!GEditor)
	{
		AddError(TEXT("GEditor is required for Catalog reorder coverage."));
		return false;
	}
	if (!FSlateApplication::IsInitialized())
	{
		Paper2DPlusTestSkip::Mark(
			*this,
			TEXT("Character Catalog anchor-relative reorder drops"),
			TEXT("Slate is not initialized in this host, so the roster cannot be built"));
		return true;
	}

	// ---- Member reorder under an ACTIVE FILTER ----
	// The regression: the drop handler read an index out of the FILTERED visible rows and handed it to
	// an API that indexes the group's authored Members array. RefilterRows filters first and sorts the
	// survivors into member order second, so those indices only agree when nothing is filtered out.
	UPaper2DPlusCharacterCatalogAsset* Catalog = NewAsset<UPaper2DPlusCharacterCatalogAsset>(
		TEXT("/Game/Paper2DPlusTests/Review/FilteredReorderCatalog"));

	// Names chosen so a single search token selects a non-contiguous subset of the authored order.
	UPaper2DPlusCharacterProfileAsset* Alpha = NewAsset<UPaper2DPlusCharacterProfileAsset>(
		TEXT("/Game/Characters/Review_ReorderZulu"));
	UPaper2DPlusCharacterProfileAsset* Match0 = NewAsset<UPaper2DPlusCharacterProfileAsset>(
		TEXT("/Game/Characters/Review_ReorderKeepA"));
	UPaper2DPlusCharacterProfileAsset* Bravo = NewAsset<UPaper2DPlusCharacterProfileAsset>(
		TEXT("/Game/Characters/Review_ReorderYankee"));
	UPaper2DPlusCharacterProfileAsset* Match1 = NewAsset<UPaper2DPlusCharacterProfileAsset>(
		TEXT("/Game/Characters/Review_ReorderKeepB"));

	Catalog->Entries = { MakeEntry(Alpha), MakeEntry(Match0), MakeEntry(Bravo), MakeEntry(Match1) };
	FPaper2DPlusCharacterCatalogGroup& Squad = Catalog->Groups.AddDefaulted_GetRef();
	Squad.GroupName = TEXT("Squad");
	Squad.DisplayName = FText::FromString(TEXT("Squad"));
	Squad.Members = { Alpha, Match0, Bravo, Match1 };

	TSharedRef<TArray<FAssetData>> Assets = MakeShared<TArray<FAssetData>>();
	*Assets = { MakeAssetData(Alpha), MakeAssetData(Match0), MakeAssetData(Bravo), MakeAssetData(Match1) };
	TSharedRef<FPaper2DPlusCharacterCatalogSettingsSnapshot> Settings =
		MakeShared<FPaper2DPlusCharacterCatalogSettingsSnapshot>();
	Settings->DefaultCatalog = Catalog;
	TSharedPtr<FCharacterCatalogEditorModel> Model = MakeModel(Catalog, Assets, Settings);
	TSharedRef<SCharacterCatalogRosterPanel> Roster =
		SNew(SCharacterCatalogRosterPanel).Model(Model);

	TestTrue(TEXT("The rail scopes the grid to the group"),
		Roster->SelectGroupRailItemForTests(TEXT("Squad")));
	Model->SetSearchText(TEXT("ReorderKeep"));
	if (!TestEqual(TEXT("The filter leaves a strict subset of the group visible"),
		Model->GetVisibleRows().Num(), 2))
	{
		Model->Shutdown();
		return false;
	}

	// Drop the LAST filtered card above the FIRST one. Their visible indices are 1 and 0; their
	// AUTHORED indices are 3 and 1. Translating the view index would have inserted at 0 — in front of
	// Alpha, a character the designer could not even see.
	GEditor->ResetTransaction(FText::FromString(TEXT("Review filtered reorder start")));
	TestTrue(TEXT("A card-on-card drop under an active filter is applied"),
		Roster->DropCardOnCardForTests(
			FSoftObjectPath(Match1),
			FSoftObjectPath(Match0),
			/*bBelowAnchor=*/false));

	auto MemberAt = [Catalog](int32 Index)
	{
		return Catalog->Groups[0].Members.IsValidIndex(Index)
			? Catalog->Groups[0].Members[Index].ToSoftObjectPath()
			: FSoftObjectPath();
	};
	TestEqual(TEXT("The unfiltered leading member is untouched"), MemberAt(0), FSoftObjectPath(Alpha));
	TestEqual(TEXT("The dragged card lands immediately before its anchor"),
		MemberAt(1), FSoftObjectPath(Match1));
	TestEqual(TEXT("The anchor keeps its own relative position"), MemberAt(2), FSoftObjectPath(Match0));
	TestEqual(TEXT("The other unfiltered member is untouched"), MemberAt(3), FSoftObjectPath(Bravo));

	// Below-anchor is the other half of the gap contract and must be equally filter-blind.
	TestTrue(TEXT("A below-anchor drop under an active filter is applied"),
		Roster->DropCardOnCardForTests(
			FSoftObjectPath(Match1),
			FSoftObjectPath(Match0),
			/*bBelowAnchor=*/true));
	TestEqual(TEXT("Below-anchor places the dragged card after its anchor"),
		MemberAt(2), FSoftObjectPath(Match1));
	TestEqual(TEXT("Below-anchor leaves the anchor ahead of it"), MemberAt(1), FSoftObjectPath(Match0));
	TestEqual(TEXT("Below-anchor still leaves filtered-out members untouched"),
		MemberAt(0), FSoftObjectPath(Alpha));

	Model->SetSearchText(FString());
	Model->Shutdown();

	// ---- Group reorder with an UNNAMED authored group ----
	// The regression: the rail hides unnamed groups but the handler assumed rail index N == authored
	// index N-1, so every unnamed row ahead of the drop shifted the landing slot.
	UPaper2DPlusCharacterCatalogAsset* RailCatalog = NewAsset<UPaper2DPlusCharacterCatalogAsset>(
		TEXT("/Game/Paper2DPlusTests/Review/UnnamedGroupRailCatalog"));
	// An unnamed group is reachable through the Catalog's EditAnywhere Groups array, and the codebase
	// already treats it as contributing nothing rather than as malformed.
	RailCatalog->Groups.AddDefaulted();
	for (const TCHAR* Name : { TEXT("Playable"), TEXT("Bosses"), TEXT("NPCs") })
	{
		FPaper2DPlusCharacterCatalogGroup& Group = RailCatalog->Groups.AddDefaulted_GetRef();
		Group.GroupName = FName(Name);
		Group.DisplayName = FText::FromString(Name);
	}

	TSharedRef<TArray<FAssetData>> RailAssets = MakeShared<TArray<FAssetData>>();
	TSharedRef<FPaper2DPlusCharacterCatalogSettingsSnapshot> RailSettings =
		MakeShared<FPaper2DPlusCharacterCatalogSettingsSnapshot>();
	RailSettings->DefaultCatalog = RailCatalog;
	TSharedPtr<FCharacterCatalogEditorModel> RailModel = MakeModel(RailCatalog, RailAssets, RailSettings);
	TSharedRef<SCharacterCatalogRosterPanel> RailRoster =
		SNew(SCharacterCatalogRosterPanel).Model(RailModel);

	// All Characters plus the three NAMED groups. A fifth row would mean the rail listed the unnamed
	// group, which is what made rail index and authored index disagree in the first place.
	TestEqual(TEXT("The rail lists the sentinel plus named groups only"),
		RailRoster->GetGroupRailItemsForTests().Num(), 4);

	// Drop NPCs above Bosses. Rail row for Bosses is 2; authored index of Bosses is 2 as well ONLY
	// because of the leading unnamed group — the old RailIndex-1 arithmetic computed 1 and would have
	// landed NPCs in front of Playable.
	TestTrue(TEXT("A rail row drop is applied"),
		RailRoster->DropGroupOnGroupRowForTests(
			TEXT("NPCs"),
			TEXT("Bosses"),
			/*bBelowAnchor=*/false));
	TestTrue(TEXT("The unnamed group is left in place"), RailCatalog->Groups[0].GroupName.IsNone());
	TestEqual(TEXT("Playable keeps its position ahead of the drop"),
		RailCatalog->Groups[1].GroupName, FName(TEXT("Playable")));
	TestEqual(TEXT("NPCs lands immediately before its anchor"),
		RailCatalog->Groups[2].GroupName, FName(TEXT("NPCs")));
	TestEqual(TEXT("Bosses keeps its own relative position"),
		RailCatalog->Groups[3].GroupName, FName(TEXT("Bosses")));

	// The All Characters sentinel owns no authored index; below it is the one absolute position.
	TestTrue(TEXT("Dropping below All Characters makes a group first"),
		RailRoster->DropGroupOnGroupRowForTests(TEXT("Bosses"), NAME_None, /*bBelowAnchor=*/true));
	TestEqual(TEXT("The All-row drop places the group at authored index 0"),
		RailCatalog->Groups[0].GroupName, FName(TEXT("Bosses")));
	TestFalse(TEXT("Above the All row is not a position and is refused"),
		RailRoster->DropGroupOnGroupRowForTests(TEXT("NPCs"), NAME_None, /*bBelowAnchor=*/false));

	GEditor->ResetTransaction(FText::FromString(TEXT("Review filtered reorder end")));
	RailModel->Shutdown();
	return true;
}

} // namespace Paper2DPlusCharacterCatalogEditorTests

#endif // WITH_DEV_AUTOMATION_TESTS
