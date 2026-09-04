// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "HAL/FileManager.h"
#include "K2Node_CallFunction.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/CommandLine.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Paper2DPlusDirectionalAnimationLibrary.h"
#include "PaperFlipbook.h"
#include "PackageTools.h"
#include "UObject/SavePackage.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"

namespace Paper2DPlusDirectionalAnimationCookedFixtureTest
{
	static const TCHAR* ProfilePackageName =
		TEXT("/Game/Paper2DPlusDirectionalAutomation/P2DPDirectionalAnimationProfile");
	static const TCHAR* BlueprintPackageName =
		TEXT("/Game/Paper2DPlusDirectionalAutomation/P2DPDirectionalAnimationBlueprintFixture");
	static const TCHAR* DirectionalBasePackageName =
		TEXT("/Game/Paper2DPlusDirectionalAutomation/P2DPDirectionalBase");
	static const TCHAR* SlotZeroPackageName =
		TEXT("/Game/Paper2DPlusDirectionalAutomation/P2DPDirectionalSlotZero");
	static const TCHAR* SlotThreePackageName =
		TEXT("/Game/Paper2DPlusDirectionalAutomation/P2DPDirectionalSlotThree");
	static const TCHAR* LegacyBasePackageName =
		TEXT("/Game/Paper2DPlusDirectionalAutomation/P2DPDirectionalLegacyBase");
	static const TCHAR* ConfiguredEmptyBasePackageName =
		TEXT("/Game/Paper2DPlusDirectionalAutomation/P2DPDirectionalConfiguredEmptyBase");

	static const FName ResolveProofFunctionName(TEXT("RunDirectionalResolveProof"));
	static const FName EnumerationProofFunctionName(TEXT("RunDirectionalEnumerationProof"));

	struct FFixtureAsset
	{
		UPackage* Package = nullptr;
		UObject* Asset = nullptr;
	};

	FString GetPackageFilename(const TCHAR* LongPackageName)
	{
		return FPackageName::LongPackageNameToFilename(
			LongPackageName,
			FPackageName::GetAssetPackageExtension());
	}

	TArray<FString> GetFixturePackageNames()
	{
		return {
			ProfilePackageName,
			BlueprintPackageName,
			DirectionalBasePackageName,
			SlotZeroPackageName,
			SlotThreePackageName,
			LegacyBasePackageName,
			ConfiguredEmptyBasePackageName
		};
	}

	void RemoveFixtureFiles(FAutomationTestBase& Test)
	{
		for (const FString& PackageName : GetFixturePackageNames())
		{
			const FString Filename = GetPackageFilename(*PackageName);
			if (IFileManager::Get().FileExists(*Filename)
				&& !IFileManager::Get().Delete(*Filename, false, true, true))
			{
				Test.AddError(FString::Printf(
					TEXT("Could not remove directional cooked fixture '%s'."),
					*Filename));
			}
		}
	}

	UPaperFlipbook* MakeFlipbook(const TCHAR* PackageName, const TCHAR* AssetName)
	{
		UPackage* Package = CreatePackage(PackageName);
		if (!Package)
		{
			return nullptr;
		}
		UPaperFlipbook* Flipbook = NewObject<UPaperFlipbook>(
			Package,
			AssetName,
			RF_Public | RF_Standalone);
		if (Flipbook)
		{
			FAssetRegistryModule::AssetCreated(Flipbook);
			Package->MarkPackageDirty();
		}
		return Flipbook;
	}

	int32 AddAnimation(
		UPaper2DPlusCharacterProfileAsset& Profile,
		const TCHAR* LogicalName,
		UPaperFlipbook& Base)
	{
		FFlipbookProfileEntry& Entry = Profile.Flipbooks.AddDefaulted_GetRef();
		Entry.Identity.FlipbookName = LogicalName;
		Entry.Identity.Flipbook = TSoftObjectPtr<UPaperFlipbook>(&Base);
		return Profile.Flipbooks.Num() - 1;
	}

	UK2Node_CallFunction* AddCall(
		UEdGraph& Graph,
		UClass& FunctionOwner,
		const FName FunctionName)
	{
		FGraphNodeCreator<UK2Node_CallFunction> Creator(Graph);
		UK2Node_CallFunction* Node = Creator.CreateNode(false);
		Node->FunctionReference.SetExternalMember(FunctionName, &FunctionOwner);
		Creator.Finalize();
		return Node;
	}

	struct FFunctionGraph
	{
		UEdGraph* Graph = nullptr;
		UK2Node_FunctionEntry* Entry = nullptr;
		UK2Node_FunctionResult* Result = nullptr;
	};

	FFunctionGraph AddFunctionGraph(UBlueprint& Blueprint, const FName FunctionName)
	{
		FFunctionGraph Out;
		Out.Graph = FBlueprintEditorUtils::CreateNewGraph(
			&Blueprint,
			FunctionName,
			UEdGraph::StaticClass(),
			UEdGraphSchema_K2::StaticClass());
		if (!Out.Graph)
		{
			return Out;
		}

		FBlueprintEditorUtils::AddFunctionGraph(
			&Blueprint,
			Out.Graph,
			true,
			static_cast<UFunction*>(nullptr));
		for (UEdGraphNode* Node : Out.Graph->Nodes)
		{
			if (!Out.Entry)
			{
				Out.Entry = Cast<UK2Node_FunctionEntry>(Node);
			}
			if (!Out.Result)
			{
				Out.Result = Cast<UK2Node_FunctionResult>(Node);
			}
		}
		if (Out.Entry && !Out.Result)
		{
			Out.Result = FBlueprintEditorUtils::FindOrCreateFunctionResultNode(Out.Entry);
		}
		if (Out.Entry && Out.Result)
		{
			const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
			Schema->TryCreateConnection(
				Out.Entry->FindPinChecked(UEdGraphSchema_K2::PN_Then),
				Out.Result->FindPinChecked(UEdGraphSchema_K2::PN_Execute));
		}
		return Out;
	}

	bool SetObjectInput(
		FAutomationTestBase& Test,
		UK2Node_CallFunction& Call,
		const FName PinName,
		UObject& Value)
	{
		UEdGraphPin* Pin = Call.FindPin(PinName, EGPD_Input);
		if (!Test.TestNotNull(*FString::Printf(TEXT("Input pin %s"), *PinName.ToString()), Pin))
		{
			return false;
		}
		GetDefault<UEdGraphSchema_K2>()->TrySetDefaultObject(*Pin, &Value);
		return Test.TestTrue(
			*FString::Printf(TEXT("Input pin %s accepts its asset default"), *PinName.ToString()),
			Pin->DefaultObject == &Value);
	}

	bool SetDirectionInput(
		FAutomationTestBase& Test,
		UK2Node_CallFunction& Call,
		const TCHAR* Value)
	{
		UEdGraphPin* Pin = Call.FindPin(TEXT("Direction"), EGPD_Input);
		if (!Test.TestNotNull(TEXT("Direction input pin"), Pin))
		{
			return false;
		}
		GetDefault<UEdGraphSchema_K2>()->TrySetDefaultValue(*Pin, Value);
		return true;
	}

	bool ConnectOutput(
		FAutomationTestBase& Test,
		UK2Node_CallFunction& Call,
		const FName SourcePinName,
		UK2Node_FunctionResult& Result,
		const FName ResultPinName)
	{
		UEdGraphPin* SourcePin = Call.FindPin(SourcePinName, EGPD_Output);
		if (!Test.TestNotNull(
			*FString::Printf(TEXT("Call output pin %s"), *SourcePinName.ToString()),
			SourcePin))
		{
			return false;
		}
		FEdGraphPinType ResultPinType = SourcePin->PinType;
		ResultPinType.bIsReference = false;
		ResultPinType.bIsConst = false;
		UEdGraphPin* ResultPin = Result.CreateUserDefinedPin(
			ResultPinName,
			ResultPinType,
			EGPD_Input);
		if (!Test.TestNotNull(
			*FString::Printf(TEXT("Function result pin %s"), *ResultPinName.ToString()),
			ResultPin))
		{
			return false;
		}
		return Test.TestTrue(
			*FString::Printf(
				TEXT("Call output %s connects to result %s"),
				*SourcePinName.ToString(),
				*ResultPinName.ToString()),
			GetDefault<UEdGraphSchema_K2>()->TryCreateConnection(SourcePin, ResultPin));
	}

	bool ConfigureResolveCall(
		FAutomationTestBase& Test,
		UK2Node_CallFunction& Call,
		UPaper2DPlusCharacterProfileAsset& Profile,
		UPaperFlipbook& QueryFlipbook,
		const TCHAR* Direction,
		UK2Node_FunctionResult& Result,
		const TCHAR* Prefix)
	{
		return SetObjectInput(Test, Call, TEXT("Profile"), Profile)
			&& SetObjectInput(Test, Call, TEXT("Flipbook"), QueryFlipbook)
			&& SetDirectionInput(Test, Call, Direction)
			&& ConnectOutput(
				Test,
				Call,
				UEdGraphSchema_K2::PN_ReturnValue,
				Result,
				*FString::Printf(TEXT("%sResult"), Prefix))
			&& ConnectOutput(
				Test,
				Call,
				TEXT("OutFlipbook"),
				Result,
				*FString::Printf(TEXT("%sFlipbook"), Prefix))
			&& ConnectOutput(
				Test,
				Call,
				TEXT("OutSlotIndex"),
				Result,
				*FString::Printf(TEXT("%sSlotIndex"), Prefix));
	}

	bool BuildBlueprintFixture(
		FAutomationTestBase& Test,
		UBlueprint& Blueprint,
		UPaper2DPlusCharacterProfileAsset& Profile,
		UPaperFlipbook& DirectionalBase,
		UPaperFlipbook& SlotZero,
		UPaperFlipbook& LegacyBase,
		UPaperFlipbook& ConfiguredEmptyBase)
	{
		FFunctionGraph ResolveGraph = AddFunctionGraph(Blueprint, ResolveProofFunctionName);
		FFunctionGraph EnumerationGraph = AddFunctionGraph(Blueprint, EnumerationProofFunctionName);
		if (!Test.TestNotNull(TEXT("Resolve proof graph"), ResolveGraph.Graph)
			|| !Test.TestNotNull(TEXT("Resolve proof entry"), ResolveGraph.Entry)
			|| !Test.TestNotNull(TEXT("Resolve proof result"), ResolveGraph.Result)
			|| !Test.TestNotNull(TEXT("Enumeration proof graph"), EnumerationGraph.Graph)
			|| !Test.TestNotNull(TEXT("Enumeration proof entry"), EnumerationGraph.Entry)
			|| !Test.TestNotNull(TEXT("Enumeration proof result"), EnumerationGraph.Result))
		{
			return false;
		}

		UK2Node_CallFunction* HasBase = AddCall(
			*ResolveGraph.Graph,
			*UPaper2DPlusDirectionalAnimationLibrary::StaticClass(),
			GET_FUNCTION_NAME_CHECKED(
				UPaper2DPlusDirectionalAnimationLibrary,
				HasMultiDirection));
		UK2Node_CallFunction* HasVariant = AddCall(
			*ResolveGraph.Graph,
			*UPaper2DPlusDirectionalAnimationLibrary::StaticClass(),
			GET_FUNCTION_NAME_CHECKED(
				UPaper2DPlusDirectionalAnimationLibrary,
				HasMultiDirection));
		if (!SetObjectInput(Test, *HasBase, TEXT("Profile"), Profile)
			|| !SetObjectInput(Test, *HasBase, TEXT("Flipbook"), DirectionalBase)
			|| !SetObjectInput(Test, *HasVariant, TEXT("Profile"), Profile)
			|| !SetObjectInput(Test, *HasVariant, TEXT("Flipbook"), SlotZero)
			|| !ConnectOutput(
				Test,
				*HasBase,
				UEdGraphSchema_K2::PN_ReturnValue,
				*ResolveGraph.Result,
				TEXT("HasBase"))
			|| !ConnectOutput(
				Test,
				*HasVariant,
				UEdGraphSchema_K2::PN_ReturnValue,
				*ResolveGraph.Result,
				TEXT("HasVariant")))
		{
			return false;
		}

		UK2Node_CallFunction* OccupiedResolve = AddCall(
			*ResolveGraph.Graph,
			*UPaper2DPlusDirectionalAnimationLibrary::StaticClass(),
			GET_FUNCTION_NAME_CHECKED(
				UPaper2DPlusDirectionalAnimationLibrary,
				ResolveDirectionalFlipbook));
		UK2Node_CallFunction* VariantResolve = AddCall(
			*ResolveGraph.Graph,
			*UPaper2DPlusDirectionalAnimationLibrary::StaticClass(),
			GET_FUNCTION_NAME_CHECKED(
				UPaper2DPlusDirectionalAnimationLibrary,
				ResolveDirectionalFlipbook));
		UK2Node_CallFunction* EmptyResolve = AddCall(
			*ResolveGraph.Graph,
			*UPaper2DPlusDirectionalAnimationLibrary::StaticClass(),
			GET_FUNCTION_NAME_CHECKED(
				UPaper2DPlusDirectionalAnimationLibrary,
				ResolveDirectionalFlipbook));
		UK2Node_CallFunction* BaseResolve = AddCall(
			*ResolveGraph.Graph,
			*UPaper2DPlusDirectionalAnimationLibrary::StaticClass(),
			GET_FUNCTION_NAME_CHECKED(
				UPaper2DPlusDirectionalAnimationLibrary,
				ResolveDirectionalFlipbook));
		UK2Node_CallFunction* ConfiguredEmptyResolve = AddCall(
			*ResolveGraph.Graph,
			*UPaper2DPlusDirectionalAnimationLibrary::StaticClass(),
			GET_FUNCTION_NAME_CHECKED(
				UPaper2DPlusDirectionalAnimationLibrary,
				ResolveDirectionalFlipbook));
		if (!ConfigureResolveCall(
				Test,
				*OccupiedResolve,
				Profile,
				DirectionalBase,
				TEXT("(X=0.70710678,Y=-0.70710678)"),
				*ResolveGraph.Result,
				TEXT("Occupied"))
			|| !ConfigureResolveCall(
				Test,
				*VariantResolve,
				Profile,
				SlotZero,
				TEXT("(X=0.70710678,Y=-0.70710678)"),
				*ResolveGraph.Result,
				TEXT("Variant"))
			|| !ConfigureResolveCall(
				Test,
				*EmptyResolve,
				Profile,
				DirectionalBase,
				TEXT("(X=-1.0,Y=0.0)"),
				*ResolveGraph.Result,
				TEXT("Empty"))
			|| !ConfigureResolveCall(
				Test,
				*BaseResolve,
				Profile,
				LegacyBase,
				TEXT("(X=0.0,Y=1.0)"),
				*ResolveGraph.Result,
				TEXT("Base"))
			|| !ConfigureResolveCall(
				Test,
				*ConfiguredEmptyResolve,
				Profile,
				ConfiguredEmptyBase,
				TEXT("(X=0.0,Y=1.0)"),
				*ResolveGraph.Result,
				TEXT("ConfiguredEmpty")))
		{
			return false;
		}

		UK2Node_CallFunction* Enumeration = AddCall(
			*EnumerationGraph.Graph,
			*UPaper2DPlusDirectionalAnimationLibrary::StaticClass(),
			GET_FUNCTION_NAME_CHECKED(
				UPaper2DPlusDirectionalAnimationLibrary,
				GetOccupiedDirectionSlots));
		// GetOccupiedDirectionSlots is deliberately impure (its synchronous load is an execution
		// step), so the call must sit ON the function's exec chain rather than hang off pins.
		{
			const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
			UEdGraphPin* EntryThen =
				EnumerationGraph.Entry->FindPinChecked(UEdGraphSchema_K2::PN_Then);
			UEdGraphPin* ResultExec =
				EnumerationGraph.Result->FindPinChecked(UEdGraphSchema_K2::PN_Execute);
			EntryThen->BreakLinkTo(ResultExec);
			if (!Test.TestTrue(
					TEXT("Enumeration call joins the exec chain after function entry"),
					Schema->TryCreateConnection(
						EntryThen,
						Enumeration->FindPinChecked(UEdGraphSchema_K2::PN_Execute)))
				|| !Test.TestTrue(
					TEXT("Enumeration call continues the exec chain into the function result"),
					Schema->TryCreateConnection(
						Enumeration->FindPinChecked(UEdGraphSchema_K2::PN_Then),
						ResultExec)))
			{
				return false;
			}
		}
		if (!SetObjectInput(Test, *Enumeration, TEXT("Profile"), Profile)
			|| !SetObjectInput(Test, *Enumeration, TEXT("Flipbook"), DirectionalBase)
			|| !ConnectOutput(
				Test,
				*Enumeration,
				UEdGraphSchema_K2::PN_ReturnValue,
				*EnumerationGraph.Result,
				TEXT("EnumerationResult"))
			|| !ConnectOutput(
				Test,
				*Enumeration,
				TEXT("OutSlots"),
				*EnumerationGraph.Result,
				TEXT("OccupiedSlots")))
		{
			return false;
		}

		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(&Blueprint);
		FKismetEditorUtilities::CompileBlueprint(
			&Blueprint,
			EBlueprintCompileOptions::SkipGarbageCollection);
		return Test.TestTrue(
			TEXT("Directional cooked Blueprint fixture compiles"),
			Blueprint.Status != BS_Error && Blueprint.GeneratedClass != nullptr);
	}

	bool SaveFixtureAsset(FAutomationTestBase& Test, const FFixtureAsset& Fixture)
	{
		if (!Fixture.Package || !Fixture.Asset)
		{
			return false;
		}
		const FString Filename = GetPackageFilename(*Fixture.Package->GetName());
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true);
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.SaveFlags = SAVE_NoError;
		return Test.TestTrue(
			*FString::Printf(TEXT("Saved fixture %s"), *Fixture.Asset->GetPathName()),
			UPackage::SavePackage(
				Fixture.Package,
				Fixture.Asset,
				*Filename,
				SaveArgs));
	}

	void ReleaseFixtureAssets(
		FAutomationTestBase& Test,
		const TArray<FFixtureAsset>& Fixtures)
	{
		TArray<UPackage*> Packages;
		for (const FFixtureAsset& Fixture : Fixtures)
		{
			if (Fixture.Asset)
			{
				FAssetRegistryModule::AssetDeleted(Fixture.Asset);
			}
			if (Fixture.Package)
			{
				Fixture.Package->SetDirtyFlag(false);
				Packages.AddUnique(Fixture.Package);
			}
		}
		FText Error;
		if (!UPackageTools::UnloadPackages(Packages, Error, true))
		{
			Test.AddError(FString::Printf(
				TEXT("Directional cooked fixture packages could not be unloaded: %s"),
				*Error.ToString()));
		}
	}

	int64 ReadEnumValue(UFunction& Function, uint8* Parameters, const FName Name)
	{
		if (const FEnumProperty* EnumProperty = FindFProperty<FEnumProperty>(&Function, Name))
		{
			const void* Value = EnumProperty->ContainerPtrToValuePtr<void>(Parameters);
			const FNumericProperty* Underlying = EnumProperty->GetUnderlyingProperty();
			return Underlying->GetSignedIntPropertyValue(Value);
		}
		if (const FByteProperty* ByteProperty = FindFProperty<FByteProperty>(&Function, Name))
		{
			return ByteProperty->GetPropertyValue_InContainer(Parameters);
		}
		return INDEX_NONE;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalAnimationCookedFixtureTest,
	"Paper2DPlus.DirectionalAnimation.CookedRuntime.AuthorAndRetainFixture",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalAnimationCookedFixtureTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusDirectionalAnimationCookedFixtureTest;

	const bool bRetainFixture = FParse::Param(
		FCommandLine::Get(),
		TEXT("Paper2DPlusRetainDirectionalAnimationCookFixture"));
	RemoveFixtureFiles(*this);

	UPaperFlipbook* DirectionalBase = MakeFlipbook(
		DirectionalBasePackageName,
		TEXT("DirectionalBase"));
	UPaperFlipbook* SlotZero = MakeFlipbook(
		SlotZeroPackageName,
		TEXT("DirectionalSlotZero"));
	UPaperFlipbook* SlotThree = MakeFlipbook(
		SlotThreePackageName,
		TEXT("DirectionalSlotThree"));
	UPaperFlipbook* LegacyBase = MakeFlipbook(
		LegacyBasePackageName,
		TEXT("DirectionalLegacyBase"));
	UPaperFlipbook* ConfiguredEmptyBase = MakeFlipbook(
		ConfiguredEmptyBasePackageName,
		TEXT("DirectionalConfiguredEmptyBase"));
	UPackage* ProfilePackage = CreatePackage(ProfilePackageName);
	UPaper2DPlusCharacterProfileAsset* Profile = ProfilePackage
		? NewObject<UPaper2DPlusCharacterProfileAsset>(
			ProfilePackage,
			TEXT("DirectionalProfile"),
			RF_Public | RF_Standalone)
		: nullptr;
	if (!TestNotNull(TEXT("Directional base flipbook"), DirectionalBase)
		|| !TestNotNull(TEXT("Slot-zero flipbook"), SlotZero)
		|| !TestNotNull(TEXT("Slot-three flipbook"), SlotThree)
		|| !TestNotNull(TEXT("Legacy base flipbook"), LegacyBase)
		|| !TestNotNull(TEXT("Configured-empty base flipbook"), ConfiguredEmptyBase)
		|| !TestNotNull(TEXT("Directional Profile package"), ProfilePackage)
		|| !TestNotNull(TEXT("Directional Profile"), Profile))
	{
		if (!bRetainFixture)
		{
			RemoveFixtureFiles(*this);
		}
		return false;
	}

	FAssetRegistryModule::AssetCreated(Profile);
	ProfilePackage->MarkPackageDirty();
	Profile->DisplayName = TEXT("Directional Cooked Runtime Fixture");
	Profile->DefaultDirectionalCount = 8;
	Profile->DefaultDirectionalAngleOffset = 0.0f;
	const int32 DirectionalIndex = AddAnimation(*Profile, TEXT("Shoot"), *DirectionalBase);
	AddAnimation(*Profile, TEXT("Idle"), *LegacyBase);
	const int32 ConfiguredEmptyIndex = AddAnimation(
		*Profile,
		TEXT("ConfiguredEmpty"),
		*ConfiguredEmptyBase);
	TestTrue(
		TEXT("Fixture authors an explicitly configured-empty directional set"),
		Profile->EnableDirectionalSet(ConfiguredEmptyIndex));
	TestTrue(
		TEXT("Fixture assigns directional slot zero"),
		Profile->SetDirectionalSlot(DirectionalIndex, 0, SlotZero));
	TestTrue(
		TEXT("Fixture assigns sparse directional slot three"),
		Profile->SetDirectionalSlot(DirectionalIndex, 3, SlotThree));
	FPaper2DPlusDirectionalStructureResult DirectionalStructure;
	TestTrue(
		TEXT("Directional fixture has a valid cooked structure"),
		Profile->CheckDirectionalAnimationStructure(DirectionalIndex, DirectionalStructure));
	FPaper2DPlusDirectionalStructureResult ConfiguredEmptyStructure;
	TestTrue(
		TEXT("Configured-empty fixture has a valid cooked structure"),
		Profile->CheckDirectionalAnimationStructure(
			ConfiguredEmptyIndex,
			ConfiguredEmptyStructure));
	FString FixtureJson;
	TestTrue(
		TEXT("Directional cooked fixture exports through the Character Profile schema"),
		Profile->ExportToJsonString(FixtureJson));
	TestTrue(
		TEXT("Directional cooked fixture is authored as current schema 9"),
		FixtureJson.Contains(TEXT("\"SchemaVersion\":9"), ESearchCase::IgnoreCase));

	UPackage* BlueprintPackage = CreatePackage(BlueprintPackageName);
	UBlueprint* Blueprint = BlueprintPackage
		? FKismetEditorUtilities::CreateBlueprint(
			UBlueprintFunctionLibrary::StaticClass(),
			BlueprintPackage,
			TEXT("BP_DirectionalAnimationCookedRuntimeFixture"),
			BPTYPE_FunctionLibrary,
			UBlueprint::StaticClass(),
			UBlueprintGeneratedClass::StaticClass(),
			NAME_None)
		: nullptr;
	if (!TestNotNull(TEXT("Directional Blueprint package"), BlueprintPackage)
		|| !TestNotNull(TEXT("Directional Blueprint fixture"), Blueprint)
		|| !BuildBlueprintFixture(
			*this,
			*Blueprint,
			*Profile,
			*DirectionalBase,
			*SlotZero,
			*LegacyBase,
			*ConfiguredEmptyBase))
	{
		if (!bRetainFixture)
		{
			RemoveFixtureFiles(*this);
		}
		return false;
	}
	FAssetRegistryModule::AssetCreated(Blueprint);
	BlueprintPackage->MarkPackageDirty();

	UFunction* ResolveProof = Blueprint->GeneratedClass->FindFunctionByName(
		ResolveProofFunctionName);
	UFunction* EnumerationProof = Blueprint->GeneratedClass->FindFunctionByName(
		EnumerationProofFunctionName);
	if (!TestNotNull(TEXT("Compiled resolve proof function"), ResolveProof)
		|| !TestNotNull(TEXT("Compiled enumeration proof function"), EnumerationProof))
	{
		if (!bRetainFixture)
		{
			RemoveFixtureFiles(*this);
		}
		return false;
	}
	const FObjectPropertyBase* OccupiedFlipbookProperty =
		FindFProperty<FObjectPropertyBase>(ResolveProof, TEXT("OccupiedFlipbook"));
	TestTrue(
		TEXT("Blueprint resolver output is projected as UPaperFlipbook"),
		OccupiedFlipbookProperty
			&& OccupiedFlipbookProperty->PropertyClass == UPaperFlipbook::StaticClass());

	FStructOnScope ResolveParameters(ResolveProof);
	Blueprint->GeneratedClass->GetDefaultObject()->ProcessEvent(
		ResolveProof,
		ResolveParameters.GetStructMemory());
	const FBoolProperty* HasBaseProperty = FindFProperty<FBoolProperty>(
		ResolveProof,
		TEXT("HasBase"));
	const FBoolProperty* HasVariantProperty = FindFProperty<FBoolProperty>(
		ResolveProof,
		TEXT("HasVariant"));
	TestTrue(
		TEXT("Blueprint Has Multi Direction recognizes the canonical base"),
		HasBaseProperty && HasBaseProperty->GetPropertyValue_InContainer(
			ResolveParameters.GetStructMemory()));
	TestTrue(
		TEXT("Blueprint Has Multi Direction recognizes a variant alias"),
		HasVariantProperty && HasVariantProperty->GetPropertyValue_InContainer(
			ResolveParameters.GetStructMemory()));
	TestEqual(
		TEXT("Blueprint occupied resolution succeeds"),
		ReadEnumValue(*ResolveProof, ResolveParameters.GetStructMemory(), TEXT("OccupiedResult")),
		static_cast<int64>(EPaper2DPlusDirectionalAnimationResult::Success));
	TestEqual(
		TEXT("Blueprint exact-empty resolution fails explicitly"),
		ReadEnumValue(*ResolveProof, ResolveParameters.GetStructMemory(), TEXT("EmptyResult")),
		static_cast<int64>(EPaper2DPlusDirectionalAnimationResult::DirectionUnoccupied));

	const TArray<FFixtureAsset> Fixtures = {
		{ DirectionalBase->GetOutermost(), DirectionalBase },
		{ SlotZero->GetOutermost(), SlotZero },
		{ SlotThree->GetOutermost(), SlotThree },
		{ LegacyBase->GetOutermost(), LegacyBase },
		{ ConfiguredEmptyBase->GetOutermost(), ConfiguredEmptyBase },
		{ ProfilePackage, Profile },
		{ BlueprintPackage, Blueprint }
	};
	for (const FFixtureAsset& Fixture : Fixtures)
	{
		SaveFixtureAsset(*this, Fixture);
	}
	for (const FString& PackageName : GetFixturePackageNames())
	{
		const FString Filename = GetPackageFilename(*PackageName);
		TestTrue(
			*FString::Printf(TEXT("Fixture package exists at %s"), *Filename),
			IFileManager::Get().FileExists(*Filename));
	}

	if (!bRetainFixture)
	{
		ReleaseFixtureAssets(*this, Fixtures);
		RemoveFixtureFiles(*this);
	}
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
