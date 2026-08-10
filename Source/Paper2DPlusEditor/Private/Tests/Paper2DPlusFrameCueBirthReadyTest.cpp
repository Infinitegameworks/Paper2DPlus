// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "BlueprintEditorTabs.h"
#include "Editor.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "FrameCues/Paper2DPlusFrameCue.h"
#include "FrameCues/Paper2DPlusFrameCueBlueprint.h"
#include "FrameCues/Paper2DPlusFrameCueTypeAuthoring.h"
#include "FrameCues/Paper2DPlusFrameCueTypeBirthReady.h"
#include "FrameCues/Paper2DPlusFrameCueTypeEditor.h"
#include "FrameCues/Paper2DPlusFrameCueTypeFactory.h"
#include "FrameCues/SPaper2DPlusFrameCueTypePicker.h"
#include "HAL/FileManager.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/EngineVersionComparison.h"
#include "Misc/Guid.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "PackageTools.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/GarbageCollection.h"
#include "UObject/ObjectRedirector.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectIterator.h"

namespace Paper2DPlusFrameCueBirthReadyTest
{
	/** Deliberate, countable skip marker; matches the shared convention test. */
	void BirthReady_Skip(FAutomationTestBase& Test, const FString& What, const FString& Why)
	{
		Test.AddInfo(FString::Printf(TEXT("[P2DP-SKIPPED] %s — %s"), *What, *Why));
	}

	FString BirthReady_MakeToken()
	{
		return FGuid::NewGuid().ToString(EGuidFormats::Digits);
	}

	/** A real, mountable, on-disk fixture package: the birth-ready save writes actual files. */
	struct FBirthReadyFixturePackage
	{
		FBirthReadyFixturePackage(FAutomationTestBase& InTest, const TCHAR* Stem)
			: Test(InTest)
		{
			PackageName = FString::Printf(
				TEXT("/Game/__AutomationTemp__/%s_%s"), Stem, *BirthReady_MakeToken());
			FilePath = FPackageName::LongPackageNameToFilename(
				PackageName, FPackageName::GetAssetPackageExtension());
			IFileManager::Get().MakeDirectory(*FPaths::GetPath(FilePath), true);
			Package = CreatePackage(*PackageName);
			if (Package)
			{
				Package->AddToRoot();
			}
		}

		~FBirthReadyFixturePackage()
		{
			Unload();
			DeleteFiles();
		}

		void Unload()
		{
			UPackage* Live = FindPackage(nullptr, *PackageName);
			if (!Live)
			{
				Package = nullptr;
				return;
			}
			if (Live->IsRooted())
			{
				Live->RemoveFromRoot();
			}
			Live->SetDirtyFlag(false);
			FText Error;
			TArray<UPackage*> Packages{Live};
			UPackageTools::UnloadPackages(Packages, Error, true);
			CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
			Package = nullptr;
		}

		void DeleteFiles() const
		{
			IFileManager& FileManager = IFileManager::Get();
			FileManager.Delete(*FilePath, false, true, true);
			FileManager.Delete(*FPaths::ChangeExtension(FilePath, TEXT("uexp")), false, true, true);
		}

		bool FileExists() const
		{
			return IFileManager::Get().FileExists(*FilePath);
		}

		FAutomationTestBase& Test;
		FString PackageName;
		FString FilePath;
		UPackage* Package = nullptr;
	};

	/** Removes any file the fixture's rename target may have produced. */
	void BirthReady_DeletePackageFiles(const FString& PackageName)
	{
		const FString FilePath = FPackageName::LongPackageNameToFilename(
			PackageName, FPackageName::GetAssetPackageExtension());
		IFileManager& FileManager = IFileManager::Get();
		FileManager.Delete(*FilePath, false, true, true);
		FileManager.Delete(*FPaths::ChangeExtension(FilePath, TEXT("uexp")), false, true, true);
	}

	UPaper2DPlusFrameCueBlueprint* BirthReady_CreateCueType(
		UPackage& Package,
		const FName AssetName,
		const EPaper2DPlusFrameCueTypeKind Kind,
		FText& OutError)
	{
		FPaper2DPlusFrameCueTypeCreateRequest Request;
		Request.Package = &Package;
		Request.AssetName = AssetName;
		Request.Kind = Kind;
		const FPaper2DPlusFrameCueTypeCreateResult Result =
			FPaper2DPlusFrameCueTypeAuthoring::CreateCueType(Request);
		OutError = Result.Error;
		return Result.IsSuccess() ? Result.CueType : nullptr;
	}

	bool BirthReady_IsReady(const UBlueprint& CueType)
	{
		return CueType.GeneratedClass
			&& FPaper2DPlusFrameCueTypeAuthoring::DescribeCueType(CueType.GeneratedClass).Availability
				== EPaper2DPlusFrameCueTypeAvailability::Ready;
	}

	/** Adds an authored payload field and recompiles: a real edit with no durable save behind it. */
	bool BirthReady_DirtyWithUnsavedEdit(UPaper2DPlusFrameCueBlueprint& CueType, const FName FieldName)
	{
		FEdGraphPinType IntegerType;
		IntegerType.PinCategory = UEdGraphSchema_K2::PC_Int;
		if (!FBlueprintEditorUtils::AddMemberVariable(&CueType, FieldName, IntegerType, TEXT("3")))
		{
			return false;
		}
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(&CueType);
		FKismetEditorUtilities::CompileBlueprint(&CueType);
		CueType.MarkPackageDirty();
		return CueType.Status == BS_UpToDate || CueType.Status == BS_UpToDateWithWarnings;
	}

	int32 BirthReady_CountRedirectorsIn(const FString& PackageName)
	{
		const UPackage* Package = FindPackage(nullptr, *PackageName);
		if (!Package)
		{
			return 0;
		}
		int32 Count = 0;
		for (TObjectIterator<UObjectRedirector> It; It; ++It)
		{
			if (It->GetOutermost() == Package)
			{
				++Count;
			}
		}
		return Count;
	}

	void BirthReady_CloseEditor(const TSharedRef<FPaper2DPlusFrameCueTypeEditor>& Editor)
	{
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 3
		Editor->CloseWindow(EAssetEditorCloseReason::AssetUnloadingOrInvalid);
#else
		Editor->CloseWindow();
#endif
	}

	const TSharedPtr<FPaper2DPlusFrameCueTypePickerItem>* BirthReady_FindItem(
		const TArray<TSharedPtr<FPaper2DPlusFrameCueTypePickerItem>>& Items,
		const FSoftObjectPath& ClassPath)
	{
		return Items.FindByPredicate(
			[&ClassPath](const TSharedPtr<FPaper2DPlusFrameCueTypePickerItem>& Item)
			{
				return Item.IsValid() && Item->Type.ClassPath == ClassPath;
			});
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCueTimelineCreationIsBornReady,
	"Paper2DPlus.FrameCues.CueType.BirthReady.TimelineCreationIsPlaceableWithoutToolkitSave",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCueTimelineCreationIsBornReady::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusFrameCueBirthReadyTest;
	FBirthReadyFixturePackage Fixture(*this, TEXT("P2DPBirthReadyTimeline"));
	if (!TestNotNull(TEXT("Timeline fixture package exists"), Fixture.Package))
	{
		return false;
	}

	FText CreateError;
	UPaper2DPlusFrameCueBlueprint* CueType = BirthReady_CreateCueType(
		*Fixture.Package,
		TEXT("BP_BornReadyMoment"),
		EPaper2DPlusFrameCueTypeKind::Moment,
		CreateError);
	if (!TestNotNull(TEXT("Timeline creation produces a Cue Type"), CueType))
	{
		AddError(CreateError.ToString());
		return false;
	}

	// This is the state the designer used to be stranded in: created, compiled, and invisible.
	TestFalse(TEXT("A freshly created Cue Type is not placement-ready on its own"),
		BirthReady_IsReady(*CueType));

	// Production order: the timeline opens the restricted toolkit on the new asset and only then
	// completes the save, so anything opening the Blueprint editor touches cannot re-dirty it after
	// the save has landed.
	const TSharedRef<FPaper2DPlusFrameCueTypeEditor> Toolkit =
		MakeShared<FPaper2DPlusFrameCueTypeEditor>();
	Toolkit->InitFrameCueTypeEditor(EToolkitMode::Standalone, nullptr, CueType);

	const FPaper2DPlusFrameCueTypeBirthReadyResult Completion =
		FPaper2DPlusFrameCueTypeBirthReady::CompleteNow(*CueType);
	if (!TestTrue(TEXT("Creation completes its own protected durable save"), Completion.IsSuccess()))
	{
		AddError(Completion.Error.ToString());
		BirthReady_CloseEditor(Toolkit);
		return false;
	}

	TestTrue(TEXT("The new Cue Type is placement-ready with no toolkit Save"),
		BirthReady_IsReady(*CueType));
	TestFalse(TEXT("The saved Cue Type package is clean"), Fixture.Package->IsDirty());
	TestTrue(TEXT("The Cue Type was written to disk"), Fixture.FileExists());

	// A fresh Cue Type carries only seeded ghost stubs, so it must still describe itself as the
	// behavior-free schema version — its durable fingerprint is byte-identical to a version 1 build's.
	FPaper2DPlusFrameCueSchema CompiledSchema;
	FText SchemaError;
	if (TestTrue(TEXT("The new Cue Type describes a compiled schema"),
		FPaper2DPlusFrameCueTypeAuthoring::DescribeSchema(
			*CueType,
			EPaper2DPlusFrameCueSchemaSource::Compiled,
			CompiledSchema,
			&SchemaError)))
	{
		TestEqual(TEXT("A behavior-free new Cue Type stages a version 1 schema"),
			CompiledSchema.Version,
			FPaper2DPlusFrameCueTypeAuthoring::BehaviorFreeSchemaVersion);
		TestEqual(TEXT("The staged durable fingerprint matches the compiled schema"),
			CueType->DurableSchemaFingerprint,
			CompiledSchema.Fingerprint);
	}

	// The picker's own listing source must offer it as placeable.
	const FPaper2DPlusFrameCueTypeDiscoveryResult Discovery =
		FPaper2DPlusFrameCueTypeAuthoring::DiscoverCueTypes(false);
	const TArray<TSharedPtr<FPaper2DPlusFrameCueTypePickerItem>> Items =
		Paper2DPlusFrameCueTypePicker::BuildItems(Discovery);
	const FSoftObjectPath ClassPath(CueType->GeneratedClass);
	const TSharedPtr<FPaper2DPlusFrameCueTypePickerItem>* Item =
		BirthReady_FindItem(Items, ClassPath);
	if (TestTrue(TEXT("The new Cue Type appears in the + Add Cue listing"), Item != nullptr))
	{
		TestTrue(TEXT("The new Cue Type is offered as placeable"), (*Item)->bPlaceable);
	}

	// Idempotent: a second completion pass on a ready asset must not re-dirty it.
	const FPaper2DPlusFrameCueTypeBirthReadyResult Repeat =
		FPaper2DPlusFrameCueTypeBirthReady::CompleteNow(*CueType);
	TestTrue(TEXT("Completing an already-ready Cue Type is a no-op success"), Repeat.IsSuccess());
	TestFalse(TEXT("Repeating completion leaves the package clean"), Fixture.Package->IsDirty());

	BirthReady_CloseEditor(Toolkit);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCueContentBrowserCreationIsBornReady,
	"Paper2DPlus.FrameCues.CueType.BirthReady.ContentBrowserCreationBecomesReadyOnDeferredPass",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCueContentBrowserCreationIsBornReady::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusFrameCueBirthReadyTest;
	FPaper2DPlusFrameCueTypeBirthReady::Shutdown();
	FBirthReadyFixturePackage Fixture(*this, TEXT("P2DPBirthReadyBrowser"));
	if (!TestNotNull(TEXT("Content Browser fixture package exists"), Fixture.Package))
	{
		return false;
	}

	UPaper2DPlusFrameCueTypeFactory* Factory = NewObject<UPaper2DPlusFrameCueTypeFactory>();
	Factory->ConfigureForKind(EPaper2DPlusFrameCueTypeKind::Range);
	if (!TestTrue(TEXT("Preconfigured Range bypasses only the kind dialog"),
		Factory->ConfigureProperties()))
	{
		return false;
	}

	// The Content Browser only calls the factory once the inline rename commits, so this IS the
	// designer's committed name — the rename-to-the-default-name case is exactly this call with no
	// rename step of its own.
	UPaper2DPlusFrameCueBlueprint* CueType = Cast<UPaper2DPlusFrameCueBlueprint>(
		Factory->FactoryCreateNew(
			UPaper2DPlusFrameCueBlueprint::StaticClass(),
			Fixture.Package,
			TEXT("BP_BrowserBornReady"),
			RF_Public | RF_Standalone | RF_Transactional,
			nullptr,
			nullptr));
	if (!TestNotNull(TEXT("The factory creates a Cue State Type"), CueType))
	{
		return false;
	}

	// Nothing may reach disk inside FactoryCreateNew: an inline rename that lands after creation
	// must never have a saved file to leave a redirector for.
	TestFalse(TEXT("The factory writes nothing to disk during creation"), Fixture.FileExists());
	TestEqual(TEXT("The factory queues exactly one deferred completion"),
		FPaper2DPlusFrameCueTypeBirthReady::GetPendingCount(), 1);
	TestFalse(TEXT("The Cue Type is not placement-ready before the deferred pass"),
		BirthReady_IsReady(*CueType));

	// IAssetTools dirties the package right after the factory returns; the deferred pass has to run
	// after that or the save it performs would be immediately undone.
	Fixture.Package->MarkPackageDirty();

	TestEqual(TEXT("The deferred pass processes the queued Cue Type"),
		FPaper2DPlusFrameCueTypeBirthReady::FlushPending(), 1);
	TestEqual(TEXT("The queue drains"), FPaper2DPlusFrameCueTypeBirthReady::GetPendingCount(), 0);
	TestTrue(TEXT("Content Browser creation ends placement-ready"), BirthReady_IsReady(*CueType));
	TestTrue(TEXT("The Cue Type was written to disk"), Fixture.FileExists());
	TestEqual(TEXT("Creation leaves no redirector behind"),
		BirthReady_CountRedirectorsIn(Fixture.PackageName), 0);

	const FPaper2DPlusFrameCueTypeDiscoveryResult Discovery =
		FPaper2DPlusFrameCueTypeAuthoring::DiscoverCueTypes(false);
	const TArray<TSharedPtr<FPaper2DPlusFrameCueTypePickerItem>> Items =
		Paper2DPlusFrameCueTypePicker::BuildItems(Discovery);
	const TSharedPtr<FPaper2DPlusFrameCueTypePickerItem>* Item =
		BirthReady_FindItem(Items, FSoftObjectPath(CueType->GeneratedClass));
	if (TestTrue(TEXT("The Content-Browser-created type appears in + Add Cue"), Item != nullptr))
	{
		TestTrue(TEXT("The Content-Browser-created type is placeable"), (*Item)->bPlaceable);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCueRenameBeforeFirstSaveLeavesNoRedirector,
	"Paper2DPlus.FrameCues.CueType.BirthReady.RenameBeforeDeferredSaveLeavesNoRedirector",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCueRenameBeforeFirstSaveLeavesNoRedirector::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusFrameCueBirthReadyTest;
	FPaper2DPlusFrameCueTypeBirthReady::Shutdown();
	FBirthReadyFixturePackage Fixture(*this, TEXT("P2DPBirthReadyRename"));
	if (!TestNotNull(TEXT("Rename fixture package exists"), Fixture.Package))
	{
		return false;
	}

	UPaper2DPlusFrameCueTypeFactory* Factory = NewObject<UPaper2DPlusFrameCueTypeFactory>();
	Factory->ConfigureForKind(EPaper2DPlusFrameCueTypeKind::Moment);
	Factory->ConfigureProperties();
	UPaper2DPlusFrameCueBlueprint* CueType = Cast<UPaper2DPlusFrameCueBlueprint>(
		Factory->FactoryCreateNew(
			UPaper2DPlusFrameCueBlueprint::StaticClass(),
			Fixture.Package,
			TEXT("BP_BeforeRename"),
			RF_Public | RF_Standalone | RF_Transactional,
			nullptr,
			nullptr));
	if (!TestNotNull(TEXT("The factory creates a Cue Type to rename"), CueType))
	{
		return false;
	}
	// The load-bearing property: at the moment a rename can land, nothing has been written. A
	// redirector only ever stands in for an asset that already exists on disk, so a rename at this
	// point cannot leave one — which is exactly why the first save is deferred past the rename.
	TestFalse(TEXT("No file exists at rename time"), Fixture.FileExists());
	TestEqual(TEXT("The first save is still owed at rename time"),
		FPaper2DPlusFrameCueTypeBirthReady::GetPendingCount(), 1);
	// Drop the queue so the completion below is ordered by this test rather than by tick timing.
	FPaper2DPlusFrameCueTypeBirthReady::Shutdown();

	const FString RenamedPackageName = FString::Printf(
		TEXT("/Game/__AutomationTemp__/P2DPBirthReadyRenamed_%s"), *BirthReady_MakeToken());
	const FString RenamedAssetName = FPackageName::GetLongPackageAssetName(RenamedPackageName);
	UPackage* RenamedPackage = CreatePackage(*RenamedPackageName);
	if (!TestNotNull(TEXT("Rename target package exists"), RenamedPackage))
	{
		return false;
	}
	RenamedPackage->AddToRoot();
	// A plain object rename, not IAssetTools::RenameAssets: the asset-tools path immediately
	// re-saves the renamed package, which is a different flow from the Content Browser's (that one
	// commits the name BEFORE the factory ever runs) and would be measuring the engine, not this.
	const bool bRenamed = CueType->Rename(
		*RenamedAssetName,
		RenamedPackage,
		REN_DontCreateRedirectors | REN_NonTransactional);
	if (!bRenamed || CueType->GetOutermost() != RenamedPackage)
	{
		BirthReady_Skip(
			*this,
			TEXT("Cue Type inline-rename redirector check"),
			TEXT("the engine declined the rename in this environment"));
		RenamedPackage->RemoveFromRoot();
		return true;
	}

	TestEqual(TEXT("Renaming before the first save leaves no redirector"),
		BirthReady_CountRedirectorsIn(Fixture.PackageName), 0);

	// The completion resolves the package from the object, so it follows the rename and writes
	// exactly one file, at the new path.
	const FPaper2DPlusFrameCueTypeBirthReadyResult Completion =
		FPaper2DPlusFrameCueTypeBirthReady::CompleteNow(*CueType);
	if (!TestTrue(TEXT("The owed save completes after the rename"), Completion.IsSuccess()))
	{
		AddError(Completion.Error.ToString());
	}
	TestTrue(TEXT("The renamed Cue Type ends placement-ready"), BirthReady_IsReady(*CueType));
	TestEqual(TEXT("The renamed Cue Type lives in its new package"),
		CueType->GetOutermost()->GetName(), RenamedPackageName);
	TestFalse(TEXT("Nothing was ever written at the pre-rename path"), Fixture.FileExists());
	TestEqual(TEXT("The completed save leaves no redirector"),
		BirthReady_CountRedirectorsIn(Fixture.PackageName), 0);

	RenamedPackage->SetDirtyFlag(false);
	RenamedPackage->RemoveFromRoot();
	{
		FText UnloadError;
		TArray<UPackage*> Packages{RenamedPackage};
		UPackageTools::UnloadPackages(Packages, UnloadError, true);
		CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
	}
	BirthReady_DeletePackageFiles(RenamedPackageName);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCueCancelledCreationDrainsSafely,
	"Paper2DPlus.FrameCues.CueType.BirthReady.CancelledCreationDrainsWithoutSaving",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCueCancelledCreationDrainsSafely::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusFrameCueBirthReadyTest;
	FPaper2DPlusFrameCueTypeBirthReady::Shutdown();
	FBirthReadyFixturePackage Fixture(*this, TEXT("P2DPBirthReadyCancel"));
	if (!TestNotNull(TEXT("Cancel fixture package exists"), Fixture.Package))
	{
		return false;
	}

	FText CreateError;
	UPaper2DPlusFrameCueBlueprint* CueType = BirthReady_CreateCueType(
		*Fixture.Package,
		TEXT("BP_CancelledCreation"),
		EPaper2DPlusFrameCueTypeKind::Moment,
		CreateError);
	if (!TestNotNull(TEXT("Cancel fixture Cue Type is created"), CueType))
	{
		AddError(CreateError.ToString());
		return false;
	}
	FPaper2DPlusFrameCueTypeBirthReady::CompleteOnNextTick(*CueType);
	TestEqual(TEXT("A completion is queued"),
		FPaper2DPlusFrameCueTypeBirthReady::GetPendingCount(), 1);

	// Model a cancelled/rolled-back creation: the queued asset stops being a live asset before the
	// deferred pass runs. Marking it garbage is what structured creation's rollback ends with, and
	// it is what makes the queue's weak entry resolve to nothing.
	CueType->ClearFlags(RF_Public | RF_Standalone | RF_Transactional);
	CueType->MarkAsGarbage();
	CueType = nullptr;
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);

	TestEqual(TEXT("A cancelled creation completes nothing"),
		FPaper2DPlusFrameCueTypeBirthReady::FlushPending(), 0);
	TestEqual(TEXT("The queue still drains"),
		FPaper2DPlusFrameCueTypeBirthReady::GetPendingCount(), 0);
	TestFalse(TEXT("A cancelled creation writes no file"), Fixture.FileExists());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCueCreationNetPolicyDefault,
	"Paper2DPlus.FrameCues.CueType.BirthReady.NewCueTypesDefaultToCosmeticOnly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCueCreationNetPolicyDefault::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusFrameCueBirthReadyTest;
	FBirthReadyFixturePackage Fixture(*this, TEXT("P2DPBirthReadyNetPolicy"));
	if (!TestNotNull(TEXT("Net policy fixture package exists"), Fixture.Package))
	{
		return false;
	}

	FText CreateError;
	UPaper2DPlusFrameCueBlueprint* Moment = BirthReady_CreateCueType(
		*Fixture.Package,
		TEXT("BP_NetPolicyMoment"),
		EPaper2DPlusFrameCueTypeKind::Moment,
		CreateError);
	UPaper2DPlusFrameCueBlueprint* Range = BirthReady_CreateCueType(
		*Fixture.Package,
		TEXT("BP_NetPolicyRange"),
		EPaper2DPlusFrameCueTypeKind::Range,
		CreateError);
	if (!TestNotNull(TEXT("Cue Type is created"), Moment)
		|| !TestNotNull(TEXT("Cue State Type is created"), Range))
	{
		AddError(CreateError.ToString());
		return false;
	}

	const UPaper2DPlusCueBase* MomentDefaults = Moment->GeneratedClass
		? Cast<UPaper2DPlusCueBase>(Moment->GeneratedClass->GetDefaultObject(false))
		: nullptr;
	const UPaper2DPlusCueBase* RangeDefaults = Range->GeneratedClass
		? Cast<UPaper2DPlusCueBase>(Range->GeneratedClass->GetDefaultObject(false))
		: nullptr;
	if (!TestNotNull(TEXT("Moment class defaults exist"), MomentDefaults)
		|| !TestNotNull(TEXT("Range class defaults exist"), RangeDefaults))
	{
		return false;
	}

	TestTrue(TEXT("A new Cue Type is born CosmeticOnly"),
		MomentDefaults->NetPolicy == EPaper2DPlusFrameCueNetPolicy::CosmeticOnly);
	TestTrue(TEXT("A new Cue State Type is born CosmeticOnly"),
		RangeDefaults->NetPolicy == EPaper2DPlusFrameCueNetPolicy::CosmeticOnly);
	// The runtime base default is deliberately unchanged: existing placements keep their meaning,
	// and only structured creation stamps the networked-correct value.
	TestTrue(TEXT("The runtime base default is still LocalAlways"),
		GetDefault<UPaper2DPlusCueBase>()->NetPolicy
			== EPaper2DPlusFrameCueNetPolicy::LocalAlways);

	// Visible in the restricted editor: NetPolicy is an ordinary authorable class default, and the
	// Class Defaults surface is one of the restricted toolkit's tabs.
	const FProperty* NetPolicyProperty = FindFProperty<FProperty>(
		UPaper2DPlusCueBase::StaticClass(), TEXT("NetPolicy"));
	if (TestNotNull(TEXT("NetPolicy is a reflected property"), NetPolicyProperty))
	{
		TestTrue(TEXT("NetPolicy stays visible in the restricted Cue Type editor"),
			FPaper2DPlusFrameCueTypeAuthoring::IsCueAuthoringPropertyVisible(NetPolicyProperty));
		TestTrue(TEXT("NetPolicy is editable"),
			NetPolicyProperty->HasAnyPropertyFlags(CPF_Edit));
	}
	TestTrue(TEXT("The restricted toolkit hosts the Class Defaults surface"),
		FPaper2DPlusFrameCueTypeEditor::GetAuthoringTabIdsForTests().Contains(
			FBlueprintEditorTabs::DefaultEditorID));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCuePickerListsRejectedTypesAsRepairRows,
	"Paper2DPlus.FrameCues.CueType.Picker.RejectedTypesListAsDisabledRepairRows",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCuePickerListsRejectedTypesAsRepairRows::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusFrameCueBirthReadyTest;
	// One asset per package, exactly as both creation flows do it: a package's protected save
	// serializes every Cue Type it contains, and an unstaged sibling would fail the whole save.
	FBirthReadyFixturePackage ReadyFixture(*this, TEXT("P2DPPickerReady"));
	FBirthReadyFixturePackage BrokenFixture(*this, TEXT("P2DPPickerNeedsFix"));
	if (!TestNotNull(TEXT("Ready picker fixture package exists"), ReadyFixture.Package)
		|| !TestNotNull(TEXT("Broken picker fixture package exists"), BrokenFixture.Package))
	{
		return false;
	}

	FText CreateError;
	UPaper2DPlusFrameCueBlueprint* ReadyType = BirthReady_CreateCueType(
		*ReadyFixture.Package,
		TEXT("BP_PickerReady"),
		EPaper2DPlusFrameCueTypeKind::Moment,
		CreateError);
	UPaper2DPlusFrameCueBlueprint* BrokenType = BirthReady_CreateCueType(
		*BrokenFixture.Package,
		TEXT("BP_PickerNeedsFix"),
		EPaper2DPlusFrameCueTypeKind::Moment,
		CreateError);
	if (!TestNotNull(TEXT("Ready picker fixture is created"), ReadyType)
		|| !TestNotNull(TEXT("Broken picker fixture is created"), BrokenType))
	{
		AddError(CreateError.ToString());
		return false;
	}

	const FPaper2DPlusFrameCueTypeBirthReadyResult ReadyCompletion =
		FPaper2DPlusFrameCueTypeBirthReady::CompleteNow(*ReadyType);
	const FPaper2DPlusFrameCueTypeBirthReadyResult BrokenCompletion =
		FPaper2DPlusFrameCueTypeBirthReady::CompleteNow(*BrokenType);
	if (!TestTrue(TEXT("Both fixtures start placement-ready"),
		ReadyCompletion.IsSuccess() && BrokenCompletion.IsSuccess()))
	{
		AddError(ReadyCompletion.Error.ToString());
		AddError(BrokenCompletion.Error.ToString());
		return false;
	}

	// Deliberately make one type non-Ready the way a designer does: edit it and do not save.
	if (!TestTrue(TEXT("The broken fixture takes an unsaved authored edit"),
		BirthReady_DirtyWithUnsavedEdit(*BrokenType, TEXT("UnsavedField"))))
	{
		return false;
	}
	TestFalse(TEXT("The edited Cue Type is no longer placement-ready"),
		BirthReady_IsReady(*BrokenType));

	const FPaper2DPlusFrameCueTypeDiscoveryResult Discovery =
		FPaper2DPlusFrameCueTypeAuthoring::DiscoverCueTypes(false);
	const TArray<TSharedPtr<FPaper2DPlusFrameCueTypePickerItem>> Items =
		Paper2DPlusFrameCueTypePicker::BuildItems(Discovery);

	const TSharedPtr<FPaper2DPlusFrameCueTypePickerItem>* ReadyItem =
		BirthReady_FindItem(Items, FSoftObjectPath(ReadyType->GeneratedClass));
	const TSharedPtr<FPaper2DPlusFrameCueTypePickerItem>* BrokenItem =
		BirthReady_FindItem(Items, FSoftObjectPath(BrokenType->GeneratedClass));
	if (!TestTrue(TEXT("The ready Cue Type is listed"), ReadyItem != nullptr)
		|| !TestTrue(TEXT("The non-ready Cue Type is still listed, not hidden"), BrokenItem != nullptr))
	{
		return false;
	}

	TestTrue(TEXT("The ready row is placeable"), (*ReadyItem)->bPlaceable);
	TestTrue(TEXT("The ready row carries no rejection reason"), (*ReadyItem)->Reason.IsEmpty());
	TestFalse(TEXT("The non-ready row is disabled for placement"), (*BrokenItem)->bPlaceable);
	TestFalse(TEXT("The non-ready row names its reason"), (*BrokenItem)->Reason.IsEmpty());
	TestTrue(TEXT("The reason names the unsaved-change cause"),
		(*BrokenItem)->Reason.ToString().Contains(TEXT("unsaved changes")));
	TestTrue(TEXT("The non-ready row offers the repair click-through"), (*BrokenItem)->bRepairable);
	TestTrue(TEXT("The repair click-through targets the Cue Type's own Blueprint"),
		Paper2DPlusFrameCueTypePicker::ResolveRepairTarget((*BrokenItem)->Type)
			== static_cast<UBlueprint*>(BrokenType));

	// Placeable rows sort ahead of every repair row.
	const int32 ReadyIndex = Items.IndexOfByKey(*ReadyItem);
	const int32 BrokenIndex = Items.IndexOfByKey(*BrokenItem);
	TestTrue(TEXT("Repair rows sort after placeable rows"), ReadyIndex < BrokenIndex);

	// Structural rejections stay out. Discovery also rejects the abstract Moment/Range bases and the
	// skeleton/reinstancing classes every loaded Cue Type Blueprint carries; listing those would bury
	// the rows a designer can act on.
	TestFalse(TEXT("The abstract Moment base is not offered as a repair row"),
		BirthReady_FindItem(Items, FSoftObjectPath(UPaper2DPlusCue::StaticClass())) != nullptr);
	TestFalse(TEXT("The abstract Range base is not offered as a repair row"),
		BirthReady_FindItem(Items, FSoftObjectPath(UPaper2DPlusCueState::StaticClass())) != nullptr);
	int32 CompilerArtifactRows = 0;
	for (const TSharedPtr<FPaper2DPlusFrameCueTypePickerItem>& Item : Items)
	{
		const FString RowName = Item.IsValid()
			? FPackageName::ObjectPathToObjectName(Item->Type.ClassPath.ToString())
			: FString();
		if (RowName.StartsWith(TEXT("SKEL_"))
			|| RowName.StartsWith(TEXT("REINST_"))
			|| RowName.StartsWith(TEXT("TRASHCLASS_")))
		{
			++CompilerArtifactRows;
		}
	}
	TestEqual(TEXT("Compiler artifact classes never appear as picker rows"), CompilerArtifactRows, 0);

	// The repair listing is a decision, not a dump: every surfaced row is an authoring state.
	TestFalse(TEXT("A structural rejection is not surfaced"),
		Paper2DPlusFrameCueTypePicker::IsSurfacedRejection(
			FPaper2DPlusFrameCueTypeAuthoring::DescribeCueType(
				UPaper2DPlusCue::StaticClass())));
	TestTrue(TEXT("An unsaved authoring state is surfaced"),
		Paper2DPlusFrameCueTypePicker::IsSurfacedRejection((*BrokenItem)->Type));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCuePickerRepairOpensRestrictedEditor,
	"Paper2DPlus.FrameCues.CueType.Picker.RepairClickOpensRestrictedCueTypeEditor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCuePickerRepairOpensRestrictedEditor::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusFrameCueBirthReadyTest;
	if (!GEditor)
	{
		BirthReady_Skip(
			*this,
			TEXT("Cue Type repair click-through"),
			TEXT("no editor engine in this run"));
		return true;
	}
	UAssetEditorSubsystem* AssetEditors = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
	if (!AssetEditors)
	{
		BirthReady_Skip(
			*this,
			TEXT("Cue Type repair click-through"),
			TEXT("no asset editor subsystem in this run"));
		return true;
	}

	FBirthReadyFixturePackage Fixture(*this, TEXT("P2DPPickerRepair"));
	if (!TestNotNull(TEXT("Repair fixture package exists"), Fixture.Package))
	{
		return false;
	}

	FText CreateError;
	UPaper2DPlusFrameCueBlueprint* BrokenType = BirthReady_CreateCueType(
		*Fixture.Package,
		TEXT("BP_RepairTarget"),
		EPaper2DPlusFrameCueTypeKind::Range,
		CreateError);
	if (!TestNotNull(TEXT("Repair fixture Cue Type is created"), BrokenType))
	{
		AddError(CreateError.ToString());
		return false;
	}
	// Never saved, so discovery rejects it — the same shape the picker shows as a repair row.
	const FPaper2DPlusFrameCueTypeDescriptor Descriptor =
		FPaper2DPlusFrameCueTypeAuthoring::DescribeCueType(BrokenType->GeneratedClass);
	if (!TestTrue(TEXT("The fixture is a rejected type"),
		Descriptor.Availability != EPaper2DPlusFrameCueTypeAvailability::Ready))
	{
		return false;
	}

	const bool bOpened = Paper2DPlusFrameCueTypePicker::OpenForRepair(Descriptor);
	TestTrue(TEXT("Activating a repair row opens an editor"), bOpened);

	IAssetEditorInstance* Instance =
		AssetEditors->FindEditorForAsset(BrokenType, /*bFocusIfOpen*/ false);
	if (TestNotNull(TEXT("An editor is open for the rejected Cue Type"), Instance))
	{
		TestTrue(TEXT("The repair path opens the restricted Cue Type editor"),
			Instance->GetEditorName() == FName(TEXT("Paper2DPlusFrameCueTypeEditor")));
	}
	AssetEditors->CloseAllEditorsForAsset(BrokenType);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCueBornReadyTypeSurvivesUnload,
	"Paper2DPlus.FrameCues.CueType.BirthReady.SavedCueTypeStaysDiscoverableAfterUnload",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCueBornReadyTypeSurvivesUnload::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusFrameCueBirthReadyTest;
	FBirthReadyFixturePackage Fixture(*this, TEXT("P2DPBirthReadyRestart"));
	if (!TestNotNull(TEXT("Restart fixture package exists"), Fixture.Package))
	{
		return false;
	}

	FText CreateError;
	UPaper2DPlusFrameCueBlueprint* CueType = BirthReady_CreateCueType(
		*Fixture.Package,
		TEXT("BP_RestartDiscovery"),
		EPaper2DPlusFrameCueTypeKind::Moment,
		CreateError);
	if (!TestNotNull(TEXT("Restart fixture Cue Type is created"), CueType))
	{
		AddError(CreateError.ToString());
		return false;
	}
	const FPaper2DPlusFrameCueTypeBirthReadyResult Completion =
		FPaper2DPlusFrameCueTypeBirthReady::CompleteNow(*CueType);
	if (!TestTrue(TEXT("Restart fixture is born ready"), Completion.IsSuccess()))
	{
		AddError(Completion.Error.ToString());
		return false;
	}
	const FString ClassPath = FSoftObjectPath(CueType->GeneratedClass).ToString();
	CueType = nullptr;

	// Unloading is the closest headless stand-in for an editor restart: the class is gone from
	// memory and only the Asset Registry knows the type exists.
	Fixture.Unload();
	if (!TestNull(TEXT("The generated class really left memory"),
		FindObject<UClass>(nullptr, *ClassPath)))
	{
		return false;
	}

	IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(
		TEXT("AssetRegistry")).Get();
	TArray<FString> FilesToScan{Fixture.FilePath};
	Registry.ScanFilesSynchronous(FilesToScan, /*bForceRescan*/ true);

	const FPaper2DPlusFrameCueTypeDiscoveryResult Discovery =
		FPaper2DPlusFrameCueTypeAuthoring::DiscoverCueTypes(/*bIncludeUnloadedBlueprints*/ true);
	const bool bDiscovered = Discovery.Types.ContainsByPredicate(
		[&ClassPath](const FPaper2DPlusFrameCueTypeDescriptor& Type)
		{
			return Type.ClassPath.ToString() == ClassPath;
		});
	TestTrue(TEXT("An unloaded born-ready Cue Type is still discovered as placeable"), bDiscovered);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
