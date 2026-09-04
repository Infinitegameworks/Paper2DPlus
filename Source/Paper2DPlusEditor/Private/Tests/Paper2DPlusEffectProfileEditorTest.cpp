// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#if WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "Paper2DPlusTestSkip.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "EffectProfileContextResolver.h"
#include "EffectProfileEditorModel.h"
#include "Paper2DPlusEditorTestFrameCueTypes.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "HAL/FileManager.h"
#include "K2Node_BreakStruct.h"
#include "K2Node_CallFunction.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/Guid.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "PackageTools.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Paper2DPlusCharacterProfileComponent.h"
#include "Paper2DPlusEffectProfileAsset.h"
#include "Paper2DPlusBlueprintLibrary.h"
#include "Paper2DPlusEffectLibraryIndex.h"
#include "Paper2DPlusEffectTags.h"
#include "PaperFlipbook.h"
#include "PaperFlipbookComponent.h"
#include "GameFramework/Actor.h"
#include "Editor.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/Package.h"
#include "UObject/ObjectRedirector.h"
#include "UObject/SavePackage.h"
#include "Misc/App.h"

namespace Paper2DPlusEffectProfileEditorTest
{
	/**
	 * Owns the real /Game package used to prove the Effect migration through linker/PostLoad.
	 * The destructor deliberately deletes only this GUID-named fixture and never recursively removes
	 * the shared automation directory, so a failed assertion cannot pollute later validation runs.
	 */
	struct FScopedEffectMigrationPackage
	{
		FString PackageName;
		FString FilePath;

		explicit FScopedEffectMigrationPackage(const FString& InStem)
		{
			const FGuid Guid = FGuid::NewGuid();
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
			const FString GuidString = Guid.ToString(EGuidFormats::Digits).ToLower();
#else
			const FString GuidString = Guid.ToString(EGuidFormats::DigitsLower);
#endif
			PackageName = FString::Printf(TEXT("/Game/__AutomationTemp__/%s_%s"), *InStem, *GuidString);
			FilePath = FPackageName::LongPackageNameToFilename(
				PackageName, FPackageName::GetAssetPackageExtension());
			IFileManager::Get().MakeDirectory(*FPaths::GetPath(FilePath), true);
		}

		~FScopedEffectMigrationPackage()
		{
			FText IgnoredError;
			Unload(IgnoredError);
			DeleteFiles();
		}

		bool Unload(FText& OutError) const
		{
			UPackage* Package = FindPackage(nullptr, *PackageName);
			if (!Package)
			{
				return true;
			}

			if (Package->IsRooted())
			{
				Package->RemoveFromRoot();
			}
			Package->SetDirtyFlag(false);
			TArray<UPackage*> PackagesToUnload;
			PackagesToUnload.Add(Package);
			return UPackageTools::UnloadPackages(PackagesToUnload, OutError, true);
		}

		bool Cleanup(FText& OutError) const
		{
			const bool bUnloaded = Unload(OutError);
			DeleteFiles();
			return bUnloaded && !IFileManager::Get().FileExists(*FilePath);
		}

		void DeleteFiles() const
		{
			IFileManager& FileManager = IFileManager::Get();
			FileManager.Delete(*FilePath, false, true, true);
			FileManager.Delete(*FPaths::ChangeExtension(FilePath, TEXT("uexp")), false, true, true);
			FileManager.Delete(*FPaths::ChangeExtension(FilePath, TEXT("ubulk")), false, true, true);
			FileManager.Delete(*FPaths::ChangeExtension(FilePath, TEXT("uptnl")), false, true, true);
			FileManager.DeleteDirectory(*FPaths::GetPath(FilePath), false, false);
		}
	};

	FString BuildMigratedStateSnapshot(
		const UPaper2DPlusEffectProfileAsset& EffectProfile)
	{
		const FPaper2DPlusEffectProfileEntry* Row = EffectProfile.Effects.Num() == 1
			? &EffectProfile.Effects[0]
			: nullptr;
		TArray<FString> Fields;
		Fields.Reserve(20);
		Fields.Add(FString::Printf(TEXT("schema=%u"), EffectProfile.EffectLibrarySchemaVersion));
		Fields.Add(TEXT("row_present=") + FString(Row ? TEXT("1") : TEXT("0")));
		if (Row)
		{
			// Cover the complete active library row plus every retained legacy source/default. A
			// second PostLoad mutation anywhere in the migration bridge must change this snapshot.
			const FSoftObjectPath RowFlipbookPath = Row->EffectFlipbook.ToSoftObjectPath();
			Fields.Add(TEXT("row_flipbook=")
				+ (RowFlipbookPath.IsNull() ? TEXT("<null>") : RowFlipbookPath.ToString()));
			Fields.Add(TEXT("row_label=") + Row->DisplayLabel.ToString());
			Fields.Add(TEXT("row_type=") + Row->TypeTag.ToString());
			Fields.Add(TEXT("row_descriptors=") + Row->DescriptorTags.ToStringSimple());
			Fields.Add(TEXT("row_pending_remap=") + Row->LegacyCategoryAwaitingRemap.ToString());
			Fields.Add(TEXT("row_legacy_name=") + Row->EffectName.ToString());
			Fields.Add(TEXT("row_legacy_category=") + Row->CategoryTag.ToString());
			Fields.Add(TEXT("row_legacy_offset=") + Row->Offset.ToString());
			Fields.Add(TEXT("row_legacy_rotation=") + FString::SanitizeFloat(Row->Rotation));
			Fields.Add(TEXT("row_legacy_scale=") + Row->Scale.ToString());
			Fields.Add(FString::Printf(TEXT("row_legacy_flip=%d"), Row->bFlipWithCharacter ? 1 : 0));
			Fields.Add(TEXT("row_legacy_tint=") + Row->Tint.ToString());
			Fields.Add(TEXT("row_legacy_socket=") + Row->SocketName.ToString());
			Fields.Add(TEXT("row_legacy_layer=") + Row->LayerScope);
		}
		return FString::Join(Fields, TEXT("|"));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusEffectProfileOpenEditorTest,
	"Paper2DPlusRender.EffectProfile.Editor.OpenAssetEditor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusEffectProfileOpenEditorTest::RunTest(const FString& Parameters)
{
	// Opening a real asset editor requires a window/RHI; under -nullrhi (headless automation) Slate
	// cannot create one and OpenEditorForAsset fatal-asserts (GenericWindow.cpp). Skip the window-
	// creating check when the process can never render so this test is headless-suite-safe.
	if (Paper2DPlusTestSkip::WithoutRenderer(
		*this,
		TEXT("Editor-open check (window creation)")))
	{
		return true;
	}

	UAssetEditorSubsystem* AssetEditorSubsystem = GEditor ? GEditor->GetEditorSubsystem<UAssetEditorSubsystem>() : nullptr;
	TestNotNull(TEXT("Asset editor subsystem is available"), AssetEditorSubsystem);
	if (!AssetEditorSubsystem)
	{
		return false;
	}

	UPaper2DPlusEffectProfileAsset* EffectProfile = NewObject<UPaper2DPlusEffectProfileAsset>(
		GetTransientPackage(),
		TEXT("TransientEffectProfileEditorOpen"));
	TestNotNull(TEXT("Effect Profile test asset is created"), EffectProfile);
	if (!EffectProfile)
	{
		return false;
	}

	EffectProfile->DisplayName = TEXT("Transient Effect Profile");

	const bool bOpened = AssetEditorSubsystem->OpenEditorForAsset(EffectProfile);
	TestTrue(TEXT("Effect Profile opens in an asset editor"), bOpened);

	AssetEditorSubsystem->CloseAllEditorsForAsset(EffectProfile);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusEffectProfileSavedMigrationRoundtripTest,
	"Paper2DPlus.EffectProfile.Editor.Migration.SavedProfileRoundtripIsStable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusEffectProfileSavedMigrationRoundtripTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusEffectProfileEditorTest;
	FScopedEffectMigrationPackage Fixture(TEXT("P2DPEffectMigration"));
	const FName EffectProfileName(TEXT("LegacyEffectLibrary"));

	UPackage* Package = CreatePackage(*Fixture.PackageName);
	if (!TestNotNull(TEXT("Temporary Effect migration package was created"), Package))
	{
		return false;
	}
	Package->AddToRoot();

	UPaperFlipbook* ProfileFlipbook = NewObject<UPaperFlipbook>(
		Package, TEXT("FB_LegacySlashSpark"), RF_Public | RF_Standalone | RF_Transactional);
	UPaper2DPlusEffectProfileAsset* EffectProfile = NewObject<UPaper2DPlusEffectProfileAsset>(
		Package, EffectProfileName, RF_Public | RF_Standalone | RF_Transactional);
	if (!TestNotNull(TEXT("Legacy profile fixture was created"), EffectProfile)
		|| !TestNotNull(TEXT("Legacy profile flipbook fixture was created"), ProfileFlipbook))
	{
		Package->RemoveFromRoot();
		return false;
	}

	// Force the serialized pre-library schema. The row contains both old identity/classification and
	// old spawn defaults exactly as existing designer assets did before direct Spawn Effect Cues.
	EffectProfile->EffectLibrarySchemaVersion = 0;
	FPaper2DPlusEffectProfileEntry& LegacyRow = EffectProfile->Effects.AddDefaulted_GetRef();
	LegacyRow.EffectName = TEXT("SlashSpark");
	LegacyRow.CategoryTag = Paper2DPlusEffectTags::Type_Impact.GetTag();
	LegacyRow.EffectFlipbook = ProfileFlipbook;
	LegacyRow.Offset = FVector2D(18.0, -4.0);
	LegacyRow.Rotation = 12.0f;
	LegacyRow.Scale = FVector2D(1.5, 0.75);
	LegacyRow.bFlipWithCharacter = false;
	LegacyRow.Tint = FLinearColor(0.9f, 0.8f, 1.0f, 1.0f);
	LegacyRow.SocketName = FName(TEXT("WeaponTip"));
	LegacyRow.LayerScope = TEXT("FrontArm");

	Package->MarkPackageDirty();

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_NoError;
	const bool bSavedLegacy = UPackage::SavePackage(
		Package, EffectProfile, *Fixture.FilePath, SaveArgs);
	Package->RemoveFromRoot();
	if (!TestTrue(TEXT("Legacy Effect Profile fixture saved to disk"), bSavedLegacy))
	{
		return false;
	}

	FText UnloadError;
	if (!TestTrue(TEXT("Legacy Effect fixture truly unloaded before migration reload"),
		Fixture.Unload(UnloadError)))
	{
		AddError(FString::Printf(TEXT("Package unload failed: %s"), *UnloadError.ToString()));
		return false;
	}

	UPackage* MigratedPackage = LoadPackage(nullptr, *Fixture.PackageName, LOAD_None);
	if (!TestNotNull(TEXT("Legacy Effect fixture reloads through linker/PostLoad"), MigratedPackage))
	{
		return false;
	}
	UPaper2DPlusEffectProfileAsset* MigratedEffectProfile = FindObject<UPaper2DPlusEffectProfileAsset>(
		MigratedPackage, *EffectProfileName.ToString());
	UPaperFlipbook* MigratedProfileFlipbook = FindObject<UPaperFlipbook>(
		MigratedPackage, TEXT("FB_LegacySlashSpark"));
	if (!TestNotNull(TEXT("Effect Profile survives disk migration reload"), MigratedEffectProfile)
		|| !TestNotNull(TEXT("Profile flipbook survives disk migration reload"), MigratedProfileFlipbook))
	{
		return false;
	}

	TestEqual(TEXT("Disk-loaded Effect Profile is stamped to the current schema"),
		MigratedEffectProfile->EffectLibrarySchemaVersion,
		UPaper2DPlusEffectProfileAsset::CurrentEffectLibrarySchemaVersion);
	if (TestEqual(TEXT("Disk-loaded Effect Profile retains one library row"),
		MigratedEffectProfile->Effects.Num(), 1))
	{
		const FPaper2DPlusEffectProfileEntry& MigratedRow = MigratedEffectProfile->Effects[0];
		TestEqual(TEXT("Legacy name becomes the presentation label on disk load"),
			MigratedRow.DisplayLabel.ToString(), FString(TEXT("SlashSpark")));
		TestEqual(TEXT("Legacy Type category becomes active library metadata on disk load"),
			MigratedRow.TypeTag, Paper2DPlusEffectTags::Type_Impact.GetTag());
		TestEqual(TEXT("Deprecated Effect name remains serialized as migration provenance"),
			MigratedRow.EffectName, FName(TEXT("SlashSpark")));
		TestEqual(TEXT("Deprecated category remains serialized as migration provenance"),
			MigratedRow.CategoryTag, Paper2DPlusEffectTags::Type_Impact.GetTag());
		TestEqual(TEXT("Library identity remains the saved Effect flipbook"),
			MigratedRow.EffectFlipbook.Get(), MigratedProfileFlipbook);
		TestTrue(TEXT("Legacy row has no invented descriptor metadata"),
			MigratedRow.DescriptorTags.IsEmpty());
		TestFalse(TEXT("A taxonomy-compatible legacy Type needs no manual remap"),
			MigratedRow.LegacyCategoryAwaitingRemap.IsValid());
		TestTrue(TEXT("Legacy row offset remains serialized as provenance"),
			MigratedRow.Offset.Equals(FVector2D(18.0, -4.0)));
		TestTrue(TEXT("Legacy row rotation remains serialized as provenance"),
			FMath::IsNearlyEqual(MigratedRow.Rotation, 12.0f));
		TestTrue(TEXT("Legacy row scale remains serialized as provenance"),
			MigratedRow.Scale.Equals(FVector2D(1.5, 0.75)));
		TestFalse(TEXT("Legacy row facing remains serialized as provenance"),
			MigratedRow.bFlipWithCharacter);
		TestTrue(TEXT("Legacy row tint remains serialized as provenance"),
			MigratedRow.Tint.Equals(FLinearColor(0.9f, 0.8f, 1.0f, 1.0f)));
		TestEqual(TEXT("Legacy row socket remains serialized as provenance"),
			MigratedRow.SocketName, FName(TEXT("WeaponTip")));
		TestEqual(TEXT("Legacy row layer scope remains serialized as provenance"),
			MigratedRow.LayerScope, FString(TEXT("FrontArm")));
	}

	const FString FirstMigratedSnapshot = BuildMigratedStateSnapshot(*MigratedEffectProfile);
	MigratedPackage->MarkPackageDirty();
	const bool bSavedMigrated = UPackage::SavePackage(
		MigratedPackage, MigratedEffectProfile, *Fixture.FilePath, SaveArgs);
	if (!TestTrue(TEXT("Migrated Effect Profile saves back to disk"), bSavedMigrated))
	{
		return false;
	}

	MigratedProfileFlipbook = nullptr;
	MigratedEffectProfile = nullptr;
	MigratedPackage = nullptr;
	UnloadError = FText::GetEmpty();
	if (!TestTrue(TEXT("Migrated Effect fixture unloads before the stability reload"),
		Fixture.Unload(UnloadError)))
	{
		AddError(FString::Printf(TEXT("Package unload failed: %s"), *UnloadError.ToString()));
		return false;
	}

	UPackage* StablePackage = LoadPackage(nullptr, *Fixture.PackageName, LOAD_None);
	if (!TestNotNull(TEXT("Saved migrated Effect fixture reloads a second time"), StablePackage))
	{
		return false;
	}
	UPaper2DPlusEffectProfileAsset* StableEffectProfile = FindObject<UPaper2DPlusEffectProfileAsset>(
		StablePackage, *EffectProfileName.ToString());
	if (!TestNotNull(TEXT("Stable Effect Profile reload succeeds"), StableEffectProfile))
	{
		return false;
	}

	TestEqual(TEXT("Second disk load reproduces the exact migrated serialized state"),
		BuildMigratedStateSnapshot(*StableEffectProfile), FirstMigratedSnapshot);
	TestEqual(TEXT("Second profile migration pass has no remaining work"),
		StableEffectProfile->MigrateLegacyLibrarySchema(), 0);
	TestFalse(TEXT("Stable second load does not dirty the package"), StablePackage->IsDirty());

	StableEffectProfile = nullptr;
	StablePackage = nullptr;
	FText CleanupError;
	if (!TestTrue(TEXT("Temporary Effect migration fixture unloads and deletes cleanly"),
		Fixture.Cleanup(CleanupError)))
	{
		AddError(FString::Printf(TEXT("Fixture cleanup failed: %s"), *CleanupError.ToString()));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusEffectProfileColdResidencyTest,
	"Paper2DPlus.EffectProfile.Runtime.LoadingProfileKeepsLibraryFlipbooksCold",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPaper2DPlusEffectProfileColdResidencyTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusEffectProfileEditorTest;
	FScopedEffectMigrationPackage RequestedFixture(TEXT("P2DPEffectRequested"));
	FScopedEffectMigrationPackage UnrelatedFixture(TEXT("P2DPEffectUnrelated"));
	FScopedEffectMigrationPackage ProfileFixture(TEXT("P2DPEffectProfile"));
	FScopedEffectMigrationPackage WrongClassFixture(TEXT("P2DPEffectWrongClass"));
	FScopedEffectMigrationPackage RedirectorFixture(TEXT("P2DPEffectRedirectors"));
	const FName RequestedName(TEXT("FB_RequestedEffect"));
	const FName UnrelatedName(TEXT("FB_UnrelatedEffect"));
	const FName ProfileName(TEXT("DA_ColdEffectProfile"));
	const FName WrongClassName(TEXT("DA_NotAFlipbook"));
	const FName RedirectedRequestedName(TEXT("FB_OldRequestedEffect"));
	const FName RedirectedWrongClassName(TEXT("FB_OldWrongClassEffect"));

	auto ObjectPath = [](const FScopedEffectMigrationPackage& Fixture, FName AssetName)
	{
		return FSoftObjectPath(FString::Printf(
			TEXT("%s.%s"), *Fixture.PackageName, *AssetName.ToString()));
	};
	auto SaveAsset = [this](FScopedEffectMigrationPackage& Fixture, UObject* Asset, const TCHAR* Label)
	{
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.SaveFlags = SAVE_NoError;
		const bool bSaved = UPackage::SavePackage(
			Asset ? Asset->GetOutermost() : nullptr,
			Asset,
			*Fixture.FilePath,
			SaveArgs);
		return TestTrue(Label, bSaved);
	};

	UPackage* RequestedPackage = CreatePackage(*RequestedFixture.PackageName);
	UPackage* UnrelatedPackage = CreatePackage(*UnrelatedFixture.PackageName);
	UPackage* ProfilePackage = CreatePackage(*ProfileFixture.PackageName);
	UPackage* WrongClassPackage = CreatePackage(*WrongClassFixture.PackageName);
	UPackage* RedirectorPackage = CreatePackage(*RedirectorFixture.PackageName);
	if (!TestNotNull(TEXT("Requested effect package is created"), RequestedPackage)
		|| !TestNotNull(TEXT("Unrelated effect package is created"), UnrelatedPackage)
		|| !TestNotNull(TEXT("Effect Profile package is created"), ProfilePackage)
		|| !TestNotNull(TEXT("Wrong-class package is created"), WrongClassPackage)
		|| !TestNotNull(TEXT("Redirector package is created"), RedirectorPackage))
	{
		return false;
	}
	RequestedPackage->AddToRoot();
	UnrelatedPackage->AddToRoot();
	ProfilePackage->AddToRoot();
	WrongClassPackage->AddToRoot();
	RedirectorPackage->AddToRoot();

	UPaperFlipbook* RequestedFlipbook = NewObject<UPaperFlipbook>(
		RequestedPackage, RequestedName, RF_Public | RF_Standalone | RF_Transactional);
	UPaperFlipbook* UnrelatedFlipbook = NewObject<UPaperFlipbook>(
		UnrelatedPackage, UnrelatedName, RF_Public | RF_Standalone | RF_Transactional);
	UPaper2DPlusEffectProfileAsset* Profile = NewObject<UPaper2DPlusEffectProfileAsset>(
		ProfilePackage, ProfileName, RF_Public | RF_Standalone | RF_Transactional);
	UPaper2DPlusEffectProfileAsset* WrongClassAsset = NewObject<UPaper2DPlusEffectProfileAsset>(
		WrongClassPackage, WrongClassName, RF_Public | RF_Standalone | RF_Transactional);
	UObjectRedirector* RequestedRedirector = NewObject<UObjectRedirector>(
		RedirectorPackage,
		RedirectedRequestedName,
		RF_Public | RF_Standalone | RF_Transactional);
	RequestedRedirector->DestinationObject = RequestedFlipbook;
	UObjectRedirector* WrongClassRedirector = NewObject<UObjectRedirector>(
		RedirectorPackage,
		RedirectedWrongClassName,
		RF_Public | RF_Standalone | RF_Transactional);
	WrongClassRedirector->DestinationObject = WrongClassAsset;
	FPaper2DPlusEffectProfileEntry& RequestedEntry = Profile->Effects.AddDefaulted_GetRef();
	RequestedEntry.EffectFlipbook = RequestedFlipbook;
	RequestedEntry.TypeTag = Paper2DPlusEffectTags::Type_Impact.GetTag();
	FPaper2DPlusEffectProfileEntry& UnrelatedEntry = Profile->Effects.AddDefaulted_GetRef();
	UnrelatedEntry.EffectFlipbook = UnrelatedFlipbook;
	UnrelatedEntry.TypeTag = Paper2DPlusEffectTags::Type_Projectile.GetTag();

	if (!SaveAsset(RequestedFixture, RequestedFlipbook, TEXT("Requested effect saves to disk"))
		|| !SaveAsset(UnrelatedFixture, UnrelatedFlipbook, TEXT("Unrelated effect saves to disk"))
		|| !SaveAsset(ProfileFixture, Profile, TEXT("Effect Profile saves to disk"))
		|| !SaveAsset(WrongClassFixture, WrongClassAsset, TEXT("Wrong-class asset saves to disk"))
		|| !SaveAsset(RedirectorFixture, RequestedRedirector, TEXT("Effect redirectors save to disk")))
	{
		return false;
	}
	RequestedPackage->RemoveFromRoot();
	UnrelatedPackage->RemoveFromRoot();
	ProfilePackage->RemoveFromRoot();
	WrongClassPackage->RemoveFromRoot();
	RedirectorPackage->RemoveFromRoot();

	FText UnloadError;
	if (!TestTrue(TEXT("Effect Profile package unloads before the residency check"),
		ProfileFixture.Unload(UnloadError))
		|| !TestTrue(TEXT("Effect redirector package unloads before the residency check"),
			RedirectorFixture.Unload(UnloadError))
		|| !TestTrue(TEXT("Requested effect package unloads before the residency check"),
			RequestedFixture.Unload(UnloadError))
		|| !TestTrue(TEXT("Unrelated effect package unloads before the residency check"),
			UnrelatedFixture.Unload(UnloadError))
		|| !TestTrue(TEXT("Wrong-class package unloads before the registry check"),
			WrongClassFixture.Unload(UnloadError)))
	{
		AddError(UnloadError.ToString());
		return false;
	}
	Profile = nullptr;
	WrongClassAsset = nullptr;
	RequestedRedirector = nullptr;
	WrongClassRedirector = nullptr;
	RequestedFlipbook = nullptr;
	UnrelatedFlipbook = nullptr;
	RequestedPackage = nullptr;
	UnrelatedPackage = nullptr;
	ProfilePackage = nullptr;
	WrongClassPackage = nullptr;
	RedirectorPackage = nullptr;
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);

	const FSoftObjectPath ProfilePath = ObjectPath(ProfileFixture, ProfileName);
	const FSoftObjectPath RequestedPath = ObjectPath(RequestedFixture, RequestedName);
	const FSoftObjectPath UnrelatedPath = ObjectPath(UnrelatedFixture, UnrelatedName);
	const FSoftObjectPath WrongClassPath = ObjectPath(WrongClassFixture, WrongClassName);
	const FSoftObjectPath RedirectedRequestedPath = ObjectPath(
		RedirectorFixture, RedirectedRequestedName);
	const FSoftObjectPath RedirectedWrongClassPath = ObjectPath(
		RedirectorFixture, RedirectedWrongClassName);
	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(
		TEXT("AssetRegistry")).Get();
	AssetRegistry.ScanFilesSynchronous(
		{ RequestedFixture.FilePath, UnrelatedFixture.FilePath,
			WrongClassFixture.FilePath, RedirectorFixture.FilePath },
		true);
	TestNull(TEXT("Requested effect starts cold"), RequestedPath.ResolveObject());
	TestNull(TEXT("Unrelated effect starts cold"), UnrelatedPath.ResolveObject());
	TestNull(TEXT("Wrong-class target starts cold"), WrongClassPath.ResolveObject());
	TestNull(TEXT("Valid redirector starts cold"), RedirectedRequestedPath.ResolveObject());
	TestNull(TEXT("Wrong-class redirector starts cold"), RedirectedWrongClassPath.ResolveObject());
	TestEqual(TEXT("Asset Registry metadata rejects a cold packaged wrong-class target"),
		UPaper2DPlusEffectProfileAsset::InspectEffectFlipbookPathNoLoad(WrongClassPath),
		EPaper2DPlusEffectFlipbookPathStatus::WrongClass);
	TestEqual(TEXT("Asset Registry metadata follows a cold redirector to a Flipbook"),
		UPaper2DPlusEffectProfileAsset::InspectEffectFlipbookPathNoLoad(RedirectedRequestedPath),
		EPaper2DPlusEffectFlipbookPathStatus::Valid);
	TestEqual(TEXT("Asset Registry metadata follows a cold redirector to reject a wrong class"),
		UPaper2DPlusEffectProfileAsset::InspectEffectFlipbookPathNoLoad(RedirectedWrongClassPath),
		EPaper2DPlusEffectFlipbookPathStatus::WrongClass);
	TestNull(TEXT("Redirect validation leaves the destination Flipbook cold"),
		RequestedPath.ResolveObject());
	TestNull(TEXT("Redirect validation leaves the wrong-class destination cold"),
		WrongClassPath.ResolveObject());

	UPaper2DPlusEffectProfileAsset* LoadedProfile = Cast<UPaper2DPlusEffectProfileAsset>(ProfilePath.TryLoad());
	if (!TestNotNull(TEXT("Effect Profile reloads from disk"), LoadedProfile))
	{
		return false;
	}
	LoadedProfile->AddToRoot();
	TestNull(TEXT("Loading the Effect Profile does not load its requested flipbook"),
		RequestedPath.ResolveObject());
	TestNull(TEXT("Loading the Effect Profile does not load an unrelated library flipbook"),
		UnrelatedPath.ResolveObject());

	TArray<FPaper2DPlusEffectProfileValidationIssue> Issues;
	TestTrue(TEXT("Cold soft references remain structurally valid"),
		LoadedProfile->ValidateEffectProfileAsset(Issues));
	TestNull(TEXT("Validation leaves the requested effect cold"), RequestedPath.ResolveObject());
	TestNull(TEXT("Validation leaves the unrelated effect cold"), UnrelatedPath.ResolveObject());

	UPaper2DPlusEffectProfileAsset* WrongClassProfile = NewObject<UPaper2DPlusEffectProfileAsset>();
	FPaper2DPlusEffectProfileEntry& WrongClassEntry =
		WrongClassProfile->Effects.AddDefaulted_GetRef();
	WrongClassEntry.EffectFlipbook = TSoftObjectPtr<UPaperFlipbook>(WrongClassPath);
	WrongClassEntry.TypeTag = Paper2DPlusEffectTags::Type_Impact.GetTag();
	TArray<FPaper2DPlusEffectProfileValidationIssue> WrongClassIssues;
	TestFalse(TEXT("Effect Profile validation rejects a cold packaged wrong-class target"),
		WrongClassProfile->ValidateEffectProfileAsset(WrongClassIssues));
	TestNull(TEXT("Effect Profile validation leaves the wrong-class target cold"),
		WrongClassPath.ResolveObject());

	UPaperFlipbook* AnimationFlipbook = NewObject<UPaperFlipbook>();
	UPaper2DPlusCharacterProfileAsset* CharacterProfile = NewObject<UPaper2DPlusCharacterProfileAsset>();
	UPaper2DPlusEditorTestMomentCue* ColdCue =
		NewObject<UPaper2DPlusEditorTestMomentCue>(CharacterProfile);
	ColdCue->SoftArt = TSoftObjectPtr<UPaperFlipbook>(RequestedPath);
	TArray<TSoftObjectPtr<UPaperFlipbook>> DeclaredWarmableArt;
	ColdCue->CollectWarmableEffectArt(DeclaredWarmableArt);
	TestEqual(TEXT("A generic Cue soft-flipbook field contributes one warmable identity"),
		DeclaredWarmableArt.Num(), 1);
	if (DeclaredWarmableArt.Num() == 1)
	{
		TestEqual(TEXT("Generic warming preserves the authored soft path"),
			DeclaredWarmableArt[0].ToSoftObjectPath(), RequestedPath);
	}
	TestNull(TEXT("Collecting generic warmable art does not load the effect"),
		RequestedPath.ResolveObject());
	TestNull(TEXT("Collecting generic warmable art leaves unrelated effects cold"),
		UnrelatedPath.ResolveObject());

	FFlipbookProfileEntry AnimationEntry;
	AnimationEntry.Identity.FlipbookName = TEXT("ColdEffectAttack");
	AnimationEntry.Identity.Flipbook = AnimationFlipbook;
	AnimationEntry.FrameEventData.FrameCues.Add(ColdCue);
	CharacterProfile->Flipbooks.Add(MoveTemp(AnimationEntry));
	AActor* PlaybackActor = NewObject<AActor>();
	UPaperFlipbookComponent* PlaybackFlipbookComponent =
		NewObject<UPaperFlipbookComponent>(PlaybackActor);
	UPaper2DPlusCharacterProfileComponent* ProfileComponent =
		NewObject<UPaper2DPlusCharacterProfileComponent>(PlaybackActor);
	PlaybackActor->AddOwnedComponent(PlaybackFlipbookComponent);
	PlaybackActor->AddOwnedComponent(ProfileComponent);
	PlaybackActor->AddToRoot();
	PlaybackFlipbookComponent->SetFlipbook(AnimationFlipbook);
	ProfileComponent->CharacterProfile = CharacterProfile;
	ProfileComponent->FlipbookComponent = PlaybackFlipbookComponent;
	ProfileComponent->HandleFlipbookChanged(AnimationFlipbook);
	TestEqual(TEXT("The current-animation warm boundary loads one distinct cold Cue effect"),
		ProfileComponent->GetWarmedFrameCueEffectCountForTests(), 1);
	TestNotNull(TEXT("Current-animation warming makes the requested effect resident"),
		RequestedPath.ResolveObject());
	TestNull(TEXT("Current-animation warming leaves unrelated library effects cold"),
		UnrelatedPath.ResolveObject());
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
	TestNotNull(TEXT("The active animation strongly retains its warmed effect"),
		RequestedPath.ResolveObject());
	ProfileComponent->HandleFlipbookChanged(NewObject<UPaperFlipbook>());
	TestEqual(TEXT("Changing animations releases the component's warmed effect set"),
		ProfileComponent->GetWarmedFrameCueEffectCountForTests(), 0);
	PlaybackActor->RemoveFromRoot();
	ProfileComponent = nullptr;
	PlaybackFlipbookComponent = nullptr;
	PlaybackActor = nullptr;
	CharacterProfile = nullptr;
	AnimationFlipbook = nullptr;

	// Restore both effects to a cold baseline before exercising the library query boundaries.
	ColdCue = nullptr;
	FText PhaseUnloadError;
	if (!TestTrue(TEXT("The generically warmed effect unloads before the filtered lookup"),
		RequestedFixture.Unload(PhaseUnloadError)))
	{
		LoadedProfile->RemoveFromRoot();
		AddError(PhaseUnloadError.ToString());
		return false;
	}
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
	TestNull(TEXT("The filtered query starts with its matching effect cold"), RequestedPath.ResolveObject());
	TestNull(TEXT("The filtered query starts with its nonmatching effect cold"), UnrelatedPath.ResolveObject());

	TArray<UPaperFlipbook*> FilteredEffects = LoadedProfile->GetEffectFlipbooksByType(
		Paper2DPlusEffectTags::Type_Impact.GetTag(), true);
	TestEqual(TEXT("A filtered Type query returns only its matching effect"), FilteredEffects.Num(), 1);
	if (FilteredEffects.Num() == 1)
	{
		TestEqual(TEXT("The filtered Type query returns the requested loaded flipbook"),
			FilteredEffects[0]->GetPathName(), RequestedPath.ToString());
	}
	TestNotNull(TEXT("A filtered Type query loads its matching effect"), RequestedPath.ResolveObject());
	TestNull(TEXT("A filtered Type query leaves a nonmatching effect cold"), UnrelatedPath.ResolveObject());

	// Drop the query result and unload its package so the indexed boundary starts cold independently.
	FilteredEffects.Reset();
	PhaseUnloadError = FText::GetEmpty();
	if (!TestTrue(TEXT("The filtered-query effect unloads before the indexed lookup"),
		RequestedFixture.Unload(PhaseUnloadError)))
	{
		LoadedProfile->RemoveFromRoot();
		AddError(PhaseUnloadError.ToString());
		return false;
	}
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
	TestNull(TEXT("The indexed lookup starts with its requested effect cold"), UnrelatedPath.ResolveObject());
	TestNull(TEXT("The previously queried effect is cold again"), RequestedPath.ResolveObject());

	UPaperFlipbook* IndexedFlipbook = LoadedProfile->GetEffectFlipbookByIndex(1);
	if (TestNotNull(TEXT("Indexed lookup loads the requested row"), IndexedFlipbook))
	{
		TestEqual(TEXT("Indexed lookup preserves the authored object-path identity"),
			IndexedFlipbook->GetPathName(), UnrelatedPath.ToString());
	}
	TestNotNull(TEXT("Indexed lookup makes only its requested target resident"), UnrelatedPath.ResolveObject());
	TestNull(TEXT("Indexed lookup leaves every other library effect cold"), RequestedPath.ResolveObject());

	// Exercise the Blueprint row projection from another fully cold starting point.
	IndexedFlipbook = nullptr;
	PhaseUnloadError = FText::GetEmpty();
	if (!TestTrue(TEXT("The indexed effect unloads before the row projection lookup"),
		UnrelatedFixture.Unload(PhaseUnloadError)))
	{
		LoadedProfile->RemoveFromRoot();
		AddError(PhaseUnloadError.ToString());
		return false;
	}
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);

	FPaper2DPlusEffectProfileEntry ProjectedEntry;
	TestTrue(TEXT("Row lookup resolves its authored entry"),
		LoadedProfile->GetEffectEntryByIndex(0, ProjectedEntry));
	if (TestNotNull(TEXT("Row lookup exposes a loaded flipbook projection"),
		ProjectedEntry.LoadedEffectFlipbook.Get()))
	{
		TestEqual(TEXT("The loaded row projection preserves the authored object path"),
			ProjectedEntry.LoadedEffectFlipbook->GetPathName(), RequestedPath.ToString());
	}
	TestEqual(TEXT("The row retains its internal soft flipbook identity"),
		ProjectedEntry.EffectFlipbook.ToSoftObjectPath().ToString(), RequestedPath.ToString());
	TestNull(TEXT("Row projection does not load an unrelated library effect"),
		UnrelatedPath.ResolveObject());

	ProjectedEntry = FPaper2DPlusEffectProfileEntry();
	PhaseUnloadError = FText::GetEmpty();
	if (!TestTrue(TEXT("The row-projected effect unloads before redirect migration"),
		RequestedFixture.Unload(PhaseUnloadError)))
	{
		LoadedProfile->RemoveFromRoot();
		AddError(PhaseUnloadError.ToString());
		return false;
	}
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);

	UPaper2DPlusEffectProfileAsset* RedirectProfile = NewObject<UPaper2DPlusEffectProfileAsset>();
	FPaper2DPlusEffectProfileEntry& RedirectEntry =
		RedirectProfile->Effects.AddDefaulted_GetRef();
	RedirectEntry.EffectName = TEXT("RedirectedEffect");
	RedirectEntry.EffectFlipbook = TSoftObjectPtr<UPaperFlipbook>(RedirectedRequestedPath);
	RedirectEntry.TypeTag = Paper2DPlusEffectTags::Type_Impact.GetTag();
	TArray<FPaper2DPlusEffectProfileValidationIssue> RedirectIssues;
	TestTrue(TEXT("Effect Profile validation accepts a cold redirector to a Flipbook"),
		RedirectProfile->ValidateEffectProfileAsset(RedirectIssues));
	TestNull(TEXT("Redirect-aware validation keeps the redirector cold"),
		RedirectedRequestedPath.ResolveObject());
	TestNull(TEXT("Redirect-aware validation keeps the destination cold"),
		RequestedPath.ResolveObject());

	// Loading the redirector warms its package, so keep every cold-path assertion above this point and
	// exercise resident canonical identity only after the metadata-only path has been proven cold.
	UPaperFlipbook* RedirectResolvedFlipbook = RedirectEntry.LoadEffectFlipbook();
	if (TestNotNull(TEXT("Loading the redirect row resolves its destination"),
		RedirectResolvedFlipbook))
	{
		TestEqual(TEXT("The resident redirect resolves to the destination object"),
			RedirectResolvedFlipbook->GetPathName(), RequestedPath.ToString());
		TestTrue(TEXT("Redirect-backed membership remains canonical after residency"),
			RedirectProfile->ContainsEffectFlipbook(RedirectResolvedFlipbook));

		FPaper2DPlusEffectProfileContext RedirectContext;
		RedirectContext.Status = EPaper2DPlusEffectContextStatus::ScopedReady;
		RedirectContext.bChoicesScoped = true;
		RedirectContext.AllowedFlipbooks.Add(RedirectedRequestedPath);
		TestTrue(TEXT("A destination value remains inside a redirect-backed Character Effects library"),
			RedirectContext.IsAllowed(RequestedPath));
		TestFalse(TEXT("Redirect aliases do not produce a false out-of-library warning"),
			RedirectContext.IsOutOfLibrary(RequestedPath));

		const FPaper2DPlusEffectProfileSnapshot RedirectSnapshot =
			FPaper2DPlusEffectLibraryIndex::MakeSnapshot(*RedirectProfile);
		TestTrue(TEXT("Effect picker snapshots publish a redirect row through canonical identity"),
			RedirectSnapshot.Rows.Num() == 1
				&& RedirectSnapshot.Rows[0].FlipbookPath == RequestedPath);

		FEffectProfileEditorModel RedirectModel;
		RedirectModel.Initialize(RedirectProfile, false);
		const FEffectProfileIntakeResult RedirectIntake =
			RedirectModel.AddFlipbooks({RedirectResolvedFlipbook});
		TestEqual(TEXT("Effect editor intake rejects a redirect destination duplicate"),
			RedirectIntake.DuplicateCount, 1);
		TestEqual(TEXT("Redirect destination intake adds no second authored row"),
			RedirectIntake.AddedCount, 0);
		TestEqual(TEXT("Redirect destination intake preserves the one-row library"),
			RedirectProfile->Effects.Num(), 1);

		FPaper2DPlusEffectProfileEntry& DestinationAliasEntry =
			RedirectProfile->Effects.AddDefaulted_GetRef();
		DestinationAliasEntry.EffectName = TEXT("DestinationAlias");
		DestinationAliasEntry.EffectFlipbook = RedirectResolvedFlipbook;
		DestinationAliasEntry.TypeTag = Paper2DPlusEffectTags::Type_Impact.GetTag();
		TArray<FPaper2DPlusEffectProfileValidationIssue> ResidentRedirectIssues;
		TestFalse(TEXT("A resident redirect and its destination remain duplicate identities"),
			RedirectProfile->ValidateEffectProfileAsset(ResidentRedirectIssues));
		TestTrue(TEXT("Resident redirect duplicate validation reports the duplicate flipbook"),
			ResidentRedirectIssues.ContainsByPredicate(
				[](const FPaper2DPlusEffectProfileValidationIssue& Issue)
				{
					return Issue.Severity ==
							EPaper2DPlusEffectProfileValidationSeverity::Error
						&& Issue.Field == TEXT("EffectFlipbook")
						&& Issue.Message.ToString().Contains(TEXT("appears more than once"));
				}));
	}

	LoadedProfile->RemoveFromRoot();
	LoadedProfile = nullptr;
	FText CleanupError;
	if (!TestTrue(TEXT("Temporary Effect Profile fixture cleans up"),
		ProfileFixture.Cleanup(CleanupError))
		|| !TestTrue(TEXT("Temporary redirector fixture cleans up"),
			RedirectorFixture.Cleanup(CleanupError))
		|| !TestTrue(TEXT("Temporary requested-effect fixture cleans up"),
			RequestedFixture.Cleanup(CleanupError))
		|| !TestTrue(TEXT("Temporary unrelated-effect fixture cleans up"),
			UnrelatedFixture.Cleanup(CleanupError))
		|| !TestTrue(TEXT("Temporary wrong-class fixture cleans up"),
			WrongClassFixture.Cleanup(CleanupError)))
	{
		AddError(CleanupError.ToString());
	}
	AssetRegistry.ScanModifiedAssetFiles(
		{ RequestedFixture.FilePath, WrongClassFixture.FilePath, RedirectorFixture.FilePath });
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusEffectProfileLegacyBreakBlueprintTest,
	"Paper2DPlus.EffectProfile.Blueprint.LegacyBreakNodeConverts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPaper2DPlusEffectProfileLegacyBreakBlueprintTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusEffectProfileEditorTest;
	FScopedEffectMigrationPackage Fixture(TEXT("P2DPLegacyEffectBreakBP"));
	UPackage* Package = CreatePackage(*Fixture.PackageName);
	if (!TestNotNull(TEXT("Legacy Break Blueprint package is created"), Package))
	{
		return false;
	}
	Package->AddToRoot();
	const FName BlueprintName(TEXT("BP_LegacyEffectBreak"));
	UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(
		AActor::StaticClass(),
		Package,
		BlueprintName,
		BPTYPE_Normal,
		UBlueprint::StaticClass(),
		UBlueprintGeneratedClass::StaticClass());
	if (!TestNotNull(TEXT("Legacy Break Blueprint is created"), Blueprint)
		|| !TestTrue(TEXT("Legacy Break Blueprint has an event graph"),
			Blueprint && !Blueprint->UbergraphPages.IsEmpty()))
	{
		Package->RemoveFromRoot();
		return false;
	}
	UEdGraph* Graph = Blueprint->UbergraphPages[0];

	FGraphNodeCreator<UK2Node_BreakStruct> BreakCreator(*Graph);
	UK2Node_BreakStruct* LegacyBreakNode = BreakCreator.CreateNode(false);
	LegacyBreakNode->StructType = FPaper2DPlusEffectProfileEntry::StaticStruct();
	LegacyBreakNode->bMadeAfterOverridePinRemoval = true;
	BreakCreator.Finalize();
	if (UEdGraphPin* CurrentProjectionPin = LegacyBreakNode->FindPin(TEXT("LoadedEffectFlipbook")))
	{
		LegacyBreakNode->RemovePin(CurrentProjectionPin);
	}
	if (UEdGraphPin* CurrentSoftPin = LegacyBreakNode->FindPin(TEXT("EffectFlipbook")))
	{
		LegacyBreakNode->RemovePin(CurrentSoftPin);
	}
	UEdGraphPin* LegacyEffectPin = LegacyBreakNode->CreatePin(
		EGPD_Output,
		UEdGraphSchema_K2::PC_Object,
		UPaperFlipbook::StaticClass(),
		TEXT("EffectFlipbook"));

	FGraphNodeCreator<UK2Node_CallFunction> EqualityCreator(*Graph);
	UK2Node_CallFunction* EqualityNode = EqualityCreator.CreateNode(false);
	EqualityNode->SetFromFunction(UKismetMathLibrary::StaticClass()->FindFunctionByName(
		GET_FUNCTION_NAME_CHECKED(UKismetMathLibrary, EqualEqual_ObjectObject)));
	EqualityCreator.Finalize();
	UEdGraphPin* EqualityInput = EqualityNode->FindPin(TEXT("A"));
	if (!TestNotNull(TEXT("Legacy hard Effect Flipbook pin exists"), LegacyEffectPin)
		|| !TestNotNull(TEXT("Object equality input exists"), EqualityInput)
		|| !TestTrue(TEXT("Legacy hard Effect Flipbook pin is connected before save"),
			Graph->GetSchema()->TryCreateConnection(LegacyEffectPin, EqualityInput)))
	{
		Package->RemoveFromRoot();
		return false;
	}

	Blueprint->bIsNewlyCreated = false;
	Package->MarkPackageDirty();
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_NoError;
	if (!TestTrue(TEXT("Legacy Break Blueprint fixture saves"),
		UPackage::SavePackage(Package, Blueprint, *Fixture.FilePath, SaveArgs)))
	{
		Package->RemoveFromRoot();
		return false;
	}

	Package->RemoveFromRoot();
	LegacyBreakNode = nullptr;
	EqualityNode = nullptr;
	LegacyEffectPin = nullptr;
	EqualityInput = nullptr;
	Graph = nullptr;
	Blueprint = nullptr;
	FText UnloadError;
	if (!TestTrue(TEXT("Legacy Break Blueprint fixture unloads"), Fixture.Unload(UnloadError)))
	{
		AddError(UnloadError.ToString());
		return false;
	}
	Package = nullptr;
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);

	UPackage* LoadedPackage = LoadPackage(nullptr, *Fixture.PackageName, LOAD_None);
	UBlueprint* LoadedBlueprint = LoadedPackage
		? FindObject<UBlueprint>(LoadedPackage, *BlueprintName.ToString())
		: nullptr;
	if (!TestNotNull(TEXT("Legacy Break Blueprint fixture reloads"), LoadedBlueprint)
		|| !TestTrue(TEXT("Reloaded Blueprint retains its event graph"),
			LoadedBlueprint && !LoadedBlueprint->UbergraphPages.IsEmpty()))
	{
		return false;
	}
	UEdGraph* LoadedGraph = LoadedBlueprint->UbergraphPages[0];
	CastChecked<UEdGraphSchema_K2>(LoadedGraph->GetSchema())
		->BackwardCompatibilityNodeConversion(LoadedGraph, false);

	TArray<UK2Node_CallFunction*> CallNodes;
	LoadedGraph->GetNodesOfClass(CallNodes);
	UK2Node_CallFunction* ConvertedBreakNode = nullptr;
	UK2Node_CallFunction* LoadedEqualityNode = nullptr;
	for (UK2Node_CallFunction* CallNode : CallNodes)
	{
		const UFunction* Function = CallNode ? CallNode->GetTargetFunction() : nullptr;
		if (Function && Function->GetFName()
			== GET_FUNCTION_NAME_CHECKED(UPaper2DPlusBlueprintLibrary, BreakEffectProfileEntry))
		{
			ConvertedBreakNode = CallNode;
		}
		else if (Function && Function->GetFName()
			== GET_FUNCTION_NAME_CHECKED(UKismetMathLibrary, EqualEqual_ObjectObject))
		{
			LoadedEqualityNode = CallNode;
		}
	}
	if (TestNotNull(TEXT("Reload converts the legacy Break Struct node to the native boundary"),
		ConvertedBreakNode)
		&& TestNotNull(TEXT("Reload retains the connected consumer node"), LoadedEqualityNode))
	{
		UEdGraphPin* ConvertedEffectPin = ConvertedBreakNode->FindPin(TEXT("EffectFlipbook"));
		UEdGraphPin* LoadedEqualityInput = LoadedEqualityNode->FindPin(TEXT("A"));
		TestTrue(TEXT("Conversion preserves the connected legacy hard Effect Flipbook pin"),
			ConvertedEffectPin
				&& LoadedEqualityInput
				&& ConvertedEffectPin->LinkedTo.Contains(LoadedEqualityInput)
				&& LoadedEqualityInput->LinkedTo.Contains(ConvertedEffectPin));
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(LoadedBlueprint);
	FKismetEditorUtilities::CompileBlueprint(
		LoadedBlueprint,
		EBlueprintCompileOptions::SkipGarbageCollection);
	TestTrue(TEXT("Converted legacy Break Blueprint compiles"),
		LoadedBlueprint->Status == BS_UpToDate
			|| LoadedBlueprint->Status == BS_UpToDateWithWarnings);

	LoadedPackage->SetDirtyFlag(false);
	LoadedBlueprint = nullptr;
	LoadedGraph = nullptr;
	LoadedPackage = nullptr;
	FText CleanupError;
	if (!TestTrue(TEXT("Legacy Break Blueprint fixture cleans up"), Fixture.Cleanup(CleanupError)))
	{
		AddError(CleanupError.ToString());
	}
	return true;
}

#endif // WITH_EDITOR
