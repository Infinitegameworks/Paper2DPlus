// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Factories/BlueprintFactory.h"
#include "FrameCues/Paper2DPlusFrameCue.h"
#include "FrameCues/Paper2DPlusFrameCueBlueprint.h"
#include "FrameCues/Paper2DPlusFrameCueTypeAssetActions.h"
#include "FrameCues/Paper2DPlusFrameCueTypeAuthoring.h"
#include "FrameCues/Paper2DPlusFrameCueTypeEditor.h"
#include "FrameCues/Paper2DPlusFrameCueTypeFactory.h"
#include "PackageTools.h"
#include "UObject/GarbageCollection.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

namespace Paper2DPlusFrameCueTypeFactoryTest
{
	struct FScopedFactoryPackage
	{
		explicit FScopedFactoryPackage(
			FAutomationTestBase& InTest,
			const TCHAR* Stem)
			: Test(InTest)
		{
			PackageName = FString::Printf(
				TEXT("/Game/__AutomationTemp__/%s_%s"),
				Stem,
				*FGuid::NewGuid().ToString(EGuidFormats::Digits));
			Package = CreatePackage(*PackageName);
			if (Package)
			{
				Package->AddToRoot();
				Package->SetDirtyFlag(false);
			}
		}

		~FScopedFactoryPackage()
		{
			if (!Package)
			{
				return;
			}

			Package->SetDirtyFlag(false);
			if (Package->IsRooted())
			{
				Package->RemoveFromRoot();
			}

			FText Error;
			TArray<UPackage*> Packages = { Package };
			if (!UPackageTools::UnloadPackages(Packages, Error, true))
			{
				Test.AddError(FString::Printf(
					TEXT("Cue Type factory fixture cleanup failed for '%s': %s"),
					*PackageName,
					*Error.ToString()));
			}
			CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
		}

		FAutomationTestBase& Test;
		FString PackageName;
		UPackage* Package = nullptr;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCueTypeFactoryContract,
	"Paper2DPlus.FrameCues.CueType.Factory.GuidedContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCueTypeFactoryContract::RunTest(const FString& Parameters)
{
	UPaper2DPlusFrameCueTypeFactory* Factory =
		NewObject<UPaper2DPlusFrameCueTypeFactory>();
	if (!TestNotNull(TEXT("Dedicated Cue Type factory exists"), Factory))
	{
		return false;
	}

	TestFalse(TEXT("Cue Type creation cannot inherit Unreal's arbitrary parent picker"),
		Factory->IsA<UBlueprintFactory>());
	TestNull(TEXT("Dedicated factory exposes no reflected ParentClass escape hatch"),
		FindFProperty<FProperty>(Factory->GetClass(), TEXT("ParentClass")));
	TestTrue(TEXT("Factory is visible in the new-asset menu"), Factory->ShouldShowInNewMenu());
	TestTrue(TEXT("Factory creates the specialized persistent envelope only"),
		Factory->GetSupportedClass() == UPaper2DPlusFrameCueBlueprint::StaticClass());
	TestEqual(TEXT("Factory presents Cue-specific designer language"),
		Factory->GetDisplayName().ToString(), FString(TEXT("Paper2D+ Frame Cue Type")));
	TestEqual(TEXT("Factory uses the shared default Cue Type name"),
		Factory->GetDefaultNewAssetName(), FString(TEXT("NewFrameCueType")));

	Factory->SetKindDialogResponseForTests(
		/*bAccept*/ false,
		EPaper2DPlusFrameCueTypeKind::Moment);
	TestFalse(TEXT("Cancel closes guided configuration without selecting a parent"),
		Factory->ConfigureProperties());
	TestTrue(TEXT("Cancel leaves the factory kind invalid"),
		Factory->GetConfiguredKind() == EPaper2DPlusFrameCueTypeKind::Invalid);

	Factory->ConfigureForKind(EPaper2DPlusFrameCueTypeKind::Invalid);
	TestFalse(TEXT("A caller cannot preconfigure an arbitrary/invalid Cue kind"),
		Factory->ConfigureProperties());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCueTypeFactoryCreatesExactKinds,
	"Paper2DPlus.FrameCues.CueType.Factory.CreatesExactMomentAndRange",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCueTypeFactoryCreatesExactKinds::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusFrameCueTypeFactoryTest;
	FScopedFactoryPackage Fixture(*this, TEXT("P2DPCueTypeFactory"));
	if (!TestNotNull(TEXT("Factory fixture package exists"), Fixture.Package))
	{
		return false;
	}

	UPaper2DPlusFrameCueTypeFactory* Factory =
		NewObject<UPaper2DPlusFrameCueTypeFactory>();
	Factory->ConfigureForKind(EPaper2DPlusFrameCueTypeKind::Moment);
	TestTrue(TEXT("Preconfigured Moment bypasses only the kind dialog"),
		Factory->ConfigureProperties());
	UPaper2DPlusFrameCueBlueprint* Moment = Cast<UPaper2DPlusFrameCueBlueprint>(
		Factory->FactoryCreateNew(
			UPaper2DPlusFrameCueBlueprint::StaticClass(),
			Fixture.Package,
			TEXT("BP_FactoryMoment"),
			RF_Public | RF_Standalone | RF_Transactional,
			nullptr,
			nullptr));
	if (!TestNotNull(TEXT("Factory creates a Cue Type"), Moment))
	{
		return false;
	}
	TestTrue(TEXT("Moment maps to exactly the native Moment base"),
		Moment->ParentClass == UPaper2DPlusCue::StaticClass());
	TestTrue(TEXT("Moment generated class uses the specialized runtime envelope"),
		Moment->GeneratedClass
			&& Cast<UPaper2DPlusFrameCueBlueprintGeneratedClass>(
				Moment->GeneratedClass.Get()) != nullptr);
	TestTrue(TEXT("Moment generated class has the exact selected parent"),
		Moment->GeneratedClass
			&& Moment->GeneratedClass->GetSuperClass()
				== UPaper2DPlusCue::StaticClass());
	TestEqual(TEXT("New Cue Type owns no graphs beyond its permitted event graph"),
		FPaper2DPlusFrameCueTypeAuthoring::InventoryLegacyGraphs(*Moment).CountUnsupportedGraphs(),
		0);
	TestNotNull(TEXT("Factory-created Cue Type pre-seeds its On Cue Triggered stub"),
		FPaper2DPlusFrameCueTypeAuthoring::FindBehaviorEventNode(*Moment, TEXT("OnCueTriggered")));
	TestTrue(TEXT("Moment creation reports structured success"),
		Factory->GetLastCreateStatus()
			== EPaper2DPlusFrameCueTypeCreateStatus::Succeeded);
	TestTrue(TEXT("Created Moment is accepted by the restricted editor"),
		FPaper2DPlusFrameCueTypeEditor::IsSupportedBlueprint(Moment));

	Factory->ConfigureForKind(EPaper2DPlusFrameCueTypeKind::Range);
	TestTrue(TEXT("Preconfigured Range bypasses only the kind dialog"),
		Factory->ConfigureProperties());
	UPaper2DPlusFrameCueBlueprint* Range = Cast<UPaper2DPlusFrameCueBlueprint>(
		Factory->FactoryCreateNew(
			UPaper2DPlusFrameCueBlueprint::StaticClass(),
			Fixture.Package,
			TEXT("BP_FactoryRange"),
			RF_Public | RF_Standalone | RF_Transactional,
			nullptr,
			nullptr));
	if (!TestNotNull(TEXT("Factory creates a Cue State Type"), Range))
	{
		return false;
	}
	TestTrue(TEXT("Range maps to exactly the native Range base"),
		Range->ParentClass == UPaper2DPlusCueState::StaticClass());
	TestTrue(TEXT("Range generated class uses the specialized runtime envelope"),
		Range->GeneratedClass
			&& Cast<UPaper2DPlusFrameCueBlueprintGeneratedClass>(
				Range->GeneratedClass.Get()) != nullptr);
	TestTrue(TEXT("Range generated class has the exact selected parent"),
		Range->GeneratedClass
			&& Range->GeneratedClass->GetSuperClass()
				== UPaper2DPlusCueState::StaticClass());
	TestEqual(TEXT("New Cue State Type owns no graphs beyond its permitted event graph"),
		FPaper2DPlusFrameCueTypeAuthoring::InventoryLegacyGraphs(*Range).CountUnsupportedGraphs(),
		0);
	TestNotNull(TEXT("Factory-created Cue State Type pre-seeds its On Cue Begin stub"),
		FPaper2DPlusFrameCueTypeAuthoring::FindBehaviorEventNode(*Range, TEXT("OnCueBegin")));
	TestNotNull(TEXT("Factory-created Cue State Type pre-seeds its On Cue End stub"),
		FPaper2DPlusFrameCueTypeAuthoring::FindBehaviorEventNode(*Range, TEXT("OnCueEnd")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCueTypeFactoryFailureCleanup,
	"Paper2DPlus.FrameCues.CueType.Factory.CancelInvalidAndCollisionCreateNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCueTypeFactoryFailureCleanup::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusFrameCueTypeFactoryTest;
	FScopedFactoryPackage Fixture(*this, TEXT("P2DPCueTypeFactoryFailures"));
	if (!TestNotNull(TEXT("Failure fixture package exists"), Fixture.Package))
	{
		return false;
	}

	UPaper2DPlusFrameCueTypeFactory* Factory =
		NewObject<UPaper2DPlusFrameCueTypeFactory>();
	Factory->SetKindDialogResponseForTests(
		/*bAccept*/ false,
		EPaper2DPlusFrameCueTypeKind::Moment);
	TestFalse(TEXT("Guided kind selection can be cancelled"), Factory->ConfigureProperties());
	TestNull(TEXT("Calling creation after cancel still creates nothing"),
		Factory->FactoryCreateNew(
			UPaper2DPlusFrameCueBlueprint::StaticClass(),
			Fixture.Package,
			TEXT("BP_Cancelled"),
			RF_Public | RF_Standalone | RF_Transactional,
			nullptr,
			nullptr));
	TestNull(TEXT("Cancelled object path is absent"),
		StaticFindObject(UObject::StaticClass(), Fixture.Package, TEXT("BP_Cancelled")));

	Factory->ConfigureForKind(EPaper2DPlusFrameCueTypeKind::Moment);
	TestTrue(TEXT("Valid preconfiguration is accepted"), Factory->ConfigureProperties());
	TestNull(TEXT("Factory refuses an unrelated requested asset class"),
		Factory->FactoryCreateNew(
			UBlueprint::StaticClass(),
			Fixture.Package,
			TEXT("BP_WrongAssetClass"),
			RF_Public | RF_Standalone | RF_Transactional,
			nullptr,
			nullptr));
	TestNull(TEXT("Wrong-class object path is absent"),
		StaticFindObject(
			UObject::StaticClass(), Fixture.Package, TEXT("BP_WrongAssetClass")));

	UObject* Occupant = NewObject<UBlueprint>(
		Fixture.Package,
		TEXT("BP_FactoryCollision"),
		RF_Public | RF_Standalone);
	Fixture.Package->SetDirtyFlag(false);
	Factory->ConfigureForKind(EPaper2DPlusFrameCueTypeKind::Range);
	TestTrue(TEXT("Range preconfiguration is accepted"), Factory->ConfigureProperties());
	TestNull(TEXT("Name collision returns no Cue Type"),
		Factory->FactoryCreateNew(
			UPaper2DPlusFrameCueBlueprint::StaticClass(),
			Fixture.Package,
			TEXT("BP_FactoryCollision"),
			RF_Public | RF_Standalone | RF_Transactional,
			nullptr,
			nullptr));
	TestTrue(TEXT("Collision preserves the existing object"),
		StaticFindObject(
			UObject::StaticClass(), Fixture.Package, TEXT("BP_FactoryCollision"))
			== Occupant);
	TestTrue(TEXT("Factory preserves U2's collision diagnostic"),
		Factory->GetLastCreateStatus()
			== EPaper2DPlusFrameCueTypeCreateStatus::NameCollision);
	TestFalse(TEXT("Rejected creation restores the package dirty state"),
		Fixture.Package->IsDirty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCueTypeAssetActionsContract,
	"Paper2DPlus.FrameCues.CueType.AssetActions.SpecializedRoutingOnly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCueTypeAssetActionsContract::RunTest(const FString& Parameters)
{
	FPaper2DPlusFrameCueTypeAssetActions Actions;
	TestTrue(TEXT("Asset actions support only the specialized Cue Type envelope"),
		Actions.GetSupportedClass() == UPaper2DPlusFrameCueBlueprint::StaticClass());
	TestEqual(TEXT("Asset actions present the Cue-specific asset identity"),
		Actions.GetName().ToString(), FString(TEXT("Paper2D+ Frame Cue Type")));
	TestFalse(TEXT("Cue Type assets cannot expose Create Child Blueprint"),
		Actions.CanCreateDerivedBlueprintForTests());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
