// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "FrameCues/K2Node_ListenForFrameCue.h"
#include "FrameCues/Paper2DPlusFrameCue.h"
#include "FrameCues/Paper2DPlusFrameCueBlueprint.h"
#include "FrameCues/Paper2DPlusFrameCueTypeEditor.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "GameFramework/Actor.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "UObject/UnrealType.h"

namespace Paper2DPlusFrameCueK2NodeTest
{
	UPaper2DPlusFrameCueBlueprint* MakeSpecializedCueType()
	{
		const FName Name = MakeUniqueObjectName(
			GetTransientPackage(),
			UPaper2DPlusFrameCueBlueprint::StaticClass(),
			TEXT("BP_TypedListenerCueType"));
		UPaper2DPlusFrameCueBlueprint* Blueprint =
			Cast<UPaper2DPlusFrameCueBlueprint>(FKismetEditorUtilities::CreateBlueprint(
				UPaper2DPlusCue::StaticClass(),
				GetTransientPackage(),
				Name,
				BPTYPE_Normal,
				UPaper2DPlusFrameCueBlueprint::StaticClass(),
				UPaper2DPlusFrameCueBlueprintGeneratedClass::StaticClass(),
				NAME_None));
		if (!Blueprint
			|| !FPaper2DPlusFrameCueTypeEditor::PrepareNewlyCreatedBlueprint(*Blueprint))
		{
			return nullptr;
		}

		FEdGraphPinType PayloadType;
		PayloadType.PinCategory = UEdGraphSchema_K2::PC_Int;
		if (!FBlueprintEditorUtils::AddMemberVariable(
			Blueprint, TEXT("ImpactPower"), PayloadType, TEXT("27")))
		{
			return nullptr;
		}
		FBPVariableDescription* Payload = Blueprint->NewVariables.FindByPredicate(
			[](const FBPVariableDescription& Variable)
			{
				return Variable.VarName == TEXT("ImpactPower");
			});
		if (!Payload)
		{
			return nullptr;
		}
		Payload->PropertyFlags &= ~CPF_DisableEditOnInstance;
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		FText ContractError;
		return Blueprint->Status != BS_Error
			&& UPaper2DPlusFrameCueBlueprint::ValidateCompiledDataOnlyContract(
				*Blueprint, &ContractError)
			? Blueprint
			: nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCueK2TypedPinTest,
	"Paper2DPlus.FrameCues.Editor.K2Node.TypedCuePin",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCueK2TypedPinTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusFrameCueK2NodeTest;
	UPaper2DPlusFrameCueBlueprint* CueType = MakeSpecializedCueType();
	if (!TestNotNull(TEXT("Modern specialized Cue Type fixture compiles"), CueType)
		|| !TestNotNull(
			TEXT("Modern Cue Type has a generated class"),
			CueType->GeneratedClass.Get()))
	{
		return false;
	}
	UClass* SpecializedCueClass = CueType->GeneratedClass;
	TestTrue(TEXT("Fixture uses the specialized generated-class envelope"),
		Cast<UPaper2DPlusFrameCueBlueprintGeneratedClass>(SpecializedCueClass) != nullptr);
	const FIntProperty* PayloadProperty =
		FindFProperty<FIntProperty>(SpecializedCueClass, TEXT("ImpactPower"));
	if (!TestNotNull(TEXT("Specialized Cue Type exposes its concrete payload field"),
		PayloadProperty))
	{
		return false;
	}
	TestTrue(TEXT("Payload field belongs to the specialized generated class"),
		PayloadProperty->GetOwnerClass() == SpecializedCueClass);

	UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(
		AActor::StaticClass(),
		GetTransientPackage(),
		TEXT("BP_FrameCueTypedPinFixture"),
		BPTYPE_Normal,
		UBlueprint::StaticClass(),
		UBlueprintGeneratedClass::StaticClass(),
		NAME_None);
	UEdGraph* Graph = Blueprint ? FBlueprintEditorUtils::FindEventGraph(Blueprint) : nullptr;
	if (!TestNotNull(TEXT("Fixture event graph"), Graph))
	{
		return false;
	}

	UK2Node_ListenForFrameCue* Node = NewObject<UK2Node_ListenForFrameCue>(Graph);
	Graph->AddNode(Node);
	Node->AllocateDefaultPins();
	UEdGraphPin* ClassPin = Node->FindPin(TEXT("CueClass"), EGPD_Input);
	UEdGraphPin* CuePin = Node->FindPin(TEXT("Cue"), EGPD_Output);
	if (!TestNotNull(TEXT("Cue Class input exists"), ClassPin)
		|| !TestNotNull(TEXT("Cue output exists"), CuePin))
	{
		return false;
	}

	ClassPin->DefaultObject = SpecializedCueClass;
	Node->PinDefaultValueChanged(ClassPin);
	TestTrue(TEXT("Modern specialized class is resolved"),
		Node->GetSelectedCueClass() == SpecializedCueClass);
	TestTrue(TEXT("Cue output uses the exact specialized generated class"),
		CuePin->PinType.PinSubCategoryObject.Get() == SpecializedCueClass);
	UClass* TypedOutputClass = Cast<UClass>(CuePin->PinType.PinSubCategoryObject.Get());
	if (TestNotNull(TEXT("Typed Cue output resolves to a class"), TypedOutputClass))
	{
		TestTrue(TEXT("Typed Cue output exposes the selected class payload"),
			FindFProperty<FIntProperty>(TypedOutputClass, TEXT("ImpactPower"))
				== PayloadProperty);
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	TestTrue(TEXT("Blueprint fixture with dedicated listener expands and compiles"),
		Blueprint->Status != BS_Error && Blueprint->GeneratedClass != nullptr);

	ClassPin->DefaultObject = nullptr;
	Node->PinDefaultValueChanged(ClassPin);
	TestTrue(TEXT("Cleared selection safely falls back to base cue type"),
		CuePin->PinType.PinSubCategoryObject.Get() == UPaper2DPlusCueBase::StaticClass());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
