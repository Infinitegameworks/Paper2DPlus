// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "FrameCues/K2Node_ListenForFrameCue.h"
#include "Paper2DPlusFrameCueListener.h"
#include "FrameCues/Paper2DPlusFrameCue.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_DynamicCast.h"
#include "KismetCompiler.h"
#include "Runtime/Launch/Resources/Version.h"

#define LOCTEXT_NAMESPACE "Paper2DPlusFrameCueNodes"

const FName UK2Node_ListenForFrameCue::CueClassPinName(TEXT("CueClass"));
const FName UK2Node_ListenForFrameCue::CueOutputPinName(TEXT("Cue"));

UK2Node_ListenForFrameCue::UK2Node_ListenForFrameCue(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	ProxyFactoryFunctionName = GET_FUNCTION_NAME_CHECKED(UPaper2DPlusFrameCueListener, ListenForFrameCue);
	ProxyFactoryClass = UPaper2DPlusFrameCueListener::StaticClass();
	ProxyClass = UPaper2DPlusFrameCueListener::StaticClass();
	ProxyActivateFunctionName = GET_FUNCTION_NAME_CHECKED(UPaper2DPlusFrameCueListener, Activate);
}

void UK2Node_ListenForFrameCue::AllocateDefaultPins()
{
	Super::AllocateDefaultPins();
	RefreshCueOutputType();
}

void UK2Node_ListenForFrameCue::PinDefaultValueChanged(UEdGraphPin* ChangedPin)
{
	Super::PinDefaultValueChanged(ChangedPin);
	if (ChangedPin && ChangedPin->PinName == CueClassPinName)
	{
		RefreshCueOutputType();
	}
}

void UK2Node_ListenForFrameCue::PinConnectionListChanged(UEdGraphPin* Pin)
{
	Super::PinConnectionListChanged(Pin);
	if (Pin && Pin->PinName == CueClassPinName)
	{
		RefreshCueOutputType();
	}
}

void UK2Node_ListenForFrameCue::PostReconstructNode()
{
	Super::PostReconstructNode();
	RefreshCueOutputType();
}

UClass* UK2Node_ListenForFrameCue::GetSelectedCueClass() const
{
	const UEdGraphPin* ClassPin = FindPin(CueClassPinName, EGPD_Input);
	if (!ClassPin || ClassPin->LinkedTo.Num() > 0)
	{
		return nullptr;
	}
	UClass* SelectedClass = Cast<UClass>(ClassPin->DefaultObject);
	return SelectedClass && SelectedClass->IsChildOf(UPaper2DPlusCueBase::StaticClass())
		? SelectedClass
		: nullptr;
}

void UK2Node_ListenForFrameCue::RefreshCueOutputType()
{
	UEdGraphPin* CuePin = FindPin(CueOutputPinName, EGPD_Output);
	if (!CuePin)
	{
		return;
	}

	UClass* SelectedClass = GetSelectedCueClass();
	CuePin->PinType.PinCategory = UEdGraphSchema_K2::PC_Object;
	CuePin->PinType.PinSubCategoryObject = SelectedClass
		? SelectedClass
		: UPaper2DPlusCueBase::StaticClass();
	if (UEdGraph* Graph = GetGraph())
	{
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 3)
		Graph->NotifyNodeChanged(this);
#else
		Graph->NotifyGraphChanged();
#endif
	}
}

void UK2Node_ListenForFrameCue::ValidateNodeDuringCompilation(FCompilerResultsLog& MessageLog) const
{
	Super::ValidateNodeDuringCompilation(MessageLog);
	const UEdGraphPin* ClassPin = FindPin(CueClassPinName, EGPD_Input);
	if (!ClassPin || ClassPin->LinkedTo.Num() > 0)
	{
		MessageLog.Error(TEXT("@@ requires a literal Cue Class so its Cue output can be typed safely."), this);
		return;
	}

	const UClass* SelectedClass = GetSelectedCueClass();
	if (!SelectedClass)
	{
		MessageLog.Error(TEXT("@@ requires a Frame Cue class."), this);
	}
	else if (SelectedClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
	{
		MessageLog.Error(TEXT("@@ requires a concrete, current Frame Cue class."), this);
	}
}

void UK2Node_ListenForFrameCue::ExpandNode(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph)
{
	UEdGraphPin* CuePin = FindPin(CueOutputPinName, EGPD_Output);
	UClass* SelectedClass = GetSelectedCueClass();
	if (CuePin && SelectedClass && SelectedClass != UPaper2DPlusCueBase::StaticClass())
	{
		UK2Node_DynamicCast* CastNode = CompilerContext.SpawnIntermediateNode<UK2Node_DynamicCast>(this, SourceGraph);
		CastNode->TargetType = SelectedClass;
		CastNode->SetPurity(true);
		CastNode->AllocateDefaultPins();

		CompilerContext.MovePinLinksToIntermediate(*CuePin, *CastNode->GetCastResultPin());
		CuePin->PinType.PinCategory = UEdGraphSchema_K2::PC_Object;
		CuePin->PinType.PinSubCategoryObject = UPaper2DPlusCueBase::StaticClass();
		if (!CompilerContext.GetSchema()->TryCreateConnection(CuePin, CastNode->GetCastSourcePin()))
		{
			CompilerContext.MessageLog.Error(TEXT("@@ could not create its internal safe cue cast."), this);
		}
	}

	Super::ExpandNode(CompilerContext, SourceGraph);
}

FText UK2Node_ListenForFrameCue::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return LOCTEXT("ListenForFrameCueTitle", "Listen for Frame Cue");
}

FText UK2Node_ListenForFrameCue::GetTooltipText() const
{
	return LOCTEXT("ListenForFrameCueTooltip",
		"Listens to one Character Profile Component and emits filtered Frame Cue lifecycle phases. "
		"The Cue output adopts the selected literal Cue Class.");
}

FText UK2Node_ListenForFrameCue::GetMenuCategory() const
{
	return LOCTEXT("FrameCueNodeCategory", "Paper2DPlus|Frame Cues");
}

#undef LOCTEXT_NAMESPACE
