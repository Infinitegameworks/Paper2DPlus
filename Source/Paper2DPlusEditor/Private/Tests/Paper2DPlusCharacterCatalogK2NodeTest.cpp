// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "CharacterCatalog/K2Node_SelectCharacterFromCatalog.h"
#include "CharacterCatalog/Paper2DPlusCatalogGraphPinFactory.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "GameFramework/Actor.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_IfThenElse.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Paper2DPlusCharacterCatalogAsset.h"
#include "Paper2DPlusCharacterProfileAsset.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCharacterCatalogK2NodeTest,
	"Paper2DPlus.CharacterCatalog.Editor.K2Node.CatalogFilteredPicker",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCharacterCatalogK2NodeTest::RunTest(const FString& Parameters)
{
	const FName BlueprintName = MakeUniqueObjectName(
		GetTransientPackage(), UBlueprint::StaticClass(), TEXT("BP_CatalogPickerFixture"));
	UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(
		AActor::StaticClass(),
		GetTransientPackage(),
		BlueprintName,
		BPTYPE_Normal,
		UBlueprint::StaticClass(),
		UBlueprintGeneratedClass::StaticClass(),
		NAME_None);
	UEdGraph* Graph = Blueprint ? FBlueprintEditorUtils::FindEventGraph(Blueprint) : nullptr;
	if (!TestNotNull(TEXT("Transient picker Blueprint"), Blueprint)
		|| !TestNotNull(TEXT("Transient picker event graph"), Graph))
	{
		return false;
	}

	UPaper2DPlusCharacterCatalogAsset* Catalog = NewObject<UPaper2DPlusCharacterCatalogAsset>(
		Blueprint, TEXT("Catalog"));
	UPaper2DPlusCharacterProfileAsset* IncludedProfile =
		NewObject<UPaper2DPlusCharacterProfileAsset>(Blueprint, TEXT("IncludedProfile"));
	UPaper2DPlusCharacterProfileAsset* ExcludedProfile =
		NewObject<UPaper2DPlusCharacterProfileAsset>(Blueprint, TEXT("ExcludedProfile"));
	FPaper2DPlusCharacterCatalogEntry IncludedEntry;
	IncludedEntry.CharacterProfile = IncludedProfile;
	Catalog->Entries.Add(IncludedEntry);

	UK2Node_SelectCharacterFromCatalog* PickerNode =
		NewObject<UK2Node_SelectCharacterFromCatalog>(Graph);
	PickerNode->CreateNewGuid();
	Graph->AddNode(PickerNode, false, false);
	PickerNode->AllocateDefaultPins();

	UEdGraphPin* CatalogPin = PickerNode->FindPin(
		UK2Node_SelectCharacterFromCatalog::CatalogPinName, EGPD_Input);
	UEdGraphPin* CharacterPin = PickerNode->FindPin(
		UK2Node_SelectCharacterFromCatalog::CharacterPinName, EGPD_Input);
	UEdGraphPin* EntryPin = PickerNode->FindPin(
		UK2Node_SelectCharacterFromCatalog::EntryPinName, EGPD_Output);
	UEdGraphPin* FoundPin = PickerNode->FindPin(
		UK2Node_SelectCharacterFromCatalog::FoundPinName, EGPD_Output);
	if (!TestNotNull(TEXT("Catalog input"), CatalogPin)
		|| !TestNotNull(TEXT("Character input"), CharacterPin)
		|| !TestNotNull(TEXT("Complete entry output"), EntryPin)
		|| !TestNotNull(TEXT("Found output"), FoundPin))
	{
		return false;
	}

	TestEqual(TEXT("Character pin remains a soft-object reference"),
		CharacterPin->PinType.PinCategory, UEdGraphSchema_K2::PC_SoftObject);
	TestTrue(TEXT("Character pin is restricted to Character Profiles"),
		CharacterPin->PinType.PinSubCategoryObject.Get()
			== UPaper2DPlusCharacterProfileAsset::StaticClass());
	TestEqual(TEXT("Entry output uses the existing Catalog entry structure"),
		EntryPin->PinType.PinCategory, UEdGraphSchema_K2::PC_Struct);
	TestTrue(TEXT("Entry output keeps the exact Catalog entry type"),
		EntryPin->PinType.PinSubCategoryObject.Get()
			== FPaper2DPlusCharacterCatalogEntry::StaticStruct());

	CatalogPin->DefaultObject = Catalog;
	CharacterPin->DefaultValue = FSoftObjectPath(IncludedProfile).ToString();
	const TSet<FSoftObjectPath> SelectablePaths = PickerNode->GetSelectableCharacterProfilePaths();
	TestEqual(TEXT("Only one Catalog Character is selectable"), SelectablePaths.Num(), 1);
	TestTrue(TEXT("Catalog member is selectable"),
		PickerNode->IsCharacterProfileSelectable(FSoftObjectPath(IncludedProfile)));
	TestFalse(TEXT("Non-member Character Profile is filtered"),
		PickerNode->IsCharacterProfileSelectable(FSoftObjectPath(ExcludedProfile)));

	FPaper2DPlusCatalogGraphPinFactory PinFactory;
	TestTrue(TEXT("Character pin receives the Catalog-filtered picker"),
		PinFactory.CreatePin(CharacterPin).IsValid());
	TestFalse(TEXT("Other pins retain their normal Blueprint widgets"),
		PinFactory.CreatePin(CatalogPin).IsValid());

	FCompilerResultsLog ValidLog;
	PickerNode->ValidateNodeDuringCompilation(ValidLog);
	TestEqual(TEXT("Valid Catalog selection has no compile errors"), ValidLog.NumErrors, 0);
	TestEqual(TEXT("Valid Catalog selection has no compile warnings"), ValidLog.NumWarnings, 0);

	CharacterPin->DefaultValue = FSoftObjectPath(ExcludedProfile).ToString();
	FCompilerResultsLog StaleLog;
	AddExpectedError(TEXT("no longer present in the resolved Catalog"),
		EAutomationExpectedErrorFlags::Contains, 1);
	PickerNode->ValidateNodeDuringCompilation(StaleLog);
	TestEqual(TEXT("Removed Catalog selection produces one warning"), StaleLog.NumWarnings, 1);
	CharacterPin->DefaultValue = FSoftObjectPath(IncludedProfile).ToString();

	UK2Node_CustomEvent* EventNode = NewObject<UK2Node_CustomEvent>(Graph);
	EventNode->CustomFunctionName = TEXT("RunCatalogPickerFixture");
	EventNode->CreateNewGuid();
	Graph->AddNode(EventNode, false, false);
	EventNode->AllocateDefaultPins();

	UK2Node_IfThenElse* BranchNode = NewObject<UK2Node_IfThenElse>(Graph);
	BranchNode->CreateNewGuid();
	Graph->AddNode(BranchNode, false, false);
	BranchNode->AllocateDefaultPins();

	const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
	TestTrue(TEXT("Fixture event connects to the branch"),
		Schema->TryCreateConnection(
			EventNode->FindPinChecked(UEdGraphSchema_K2::PN_Then),
			BranchNode->GetExecPin()));
	TestTrue(TEXT("Picker Found output drives the fixture branch"),
		Schema->TryCreateConnection(FoundPin, BranchNode->GetConditionPin()));

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	TestTrue(TEXT("Catalog picker expands through the existing runtime lookup"),
		Blueprint->Status != BS_Error && Blueprint->GeneratedClass != nullptr);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
