// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "ClashGraph/Paper2DPlusClashGraphNode_Tag.h"

#include "ClashGraph/SClashGraphNode_Tag.h"
#include "EdGraph/EdGraphPin.h"

#define LOCTEXT_NAMESPACE "Paper2DPlusClashGraphNode_Tag"

void UPaper2DPlusClashGraphNode_Tag::AllocateDefaultPins()
{
	// One visible input + one visible output pin in a shared "Clash" category. INPUT = the Loser side
	// (incoming "X beats me" wires); OUTPUT = the Winner side (outgoing "I beat Y" wires). Both are
	// MULTI-link (a category beats many, and is beaten by many) — the engine default for a pin with no
	// single-link metadata, so no auto-break on a second connection.
	CreatePin(EGPD_Input, TEXT("Clash"), TEXT("In"));
	CreatePin(EGPD_Output, TEXT("Clash"), TEXT("Out"));
}

FText UPaper2DPlusClashGraphNode_Tag::GetNodeTitle(ENodeTitleType::Type /*TitleType*/) const
{
	return FText::FromString(GetShortName());
}

FLinearColor UPaper2DPlusClashGraphNode_Tag::GetNodeTitleColor() const
{
	// The clash asset's signature red (FColor(220,90,90)) so the node reads as a clash-category at a glance.
	return FLinearColor(0.78f, 0.32f, 0.32f);
}

TSharedPtr<SGraphNode> UPaper2DPlusClashGraphNode_Tag::CreateVisualWidget()
{
	return SNew(SClashGraphNode_Tag, this);
}

UEdGraphPin* UPaper2DPlusClashGraphNode_Tag::GetInputPin() const
{
	for (UEdGraphPin* Pin : Pins)
	{
		if (Pin && Pin->Direction == EGPD_Input)
		{
			return Pin;
		}
	}
	return nullptr;
}

UEdGraphPin* UPaper2DPlusClashGraphNode_Tag::GetOutputPin() const
{
	for (UEdGraphPin* Pin : Pins)
	{
		if (Pin && Pin->Direction == EGPD_Output)
		{
			return Pin;
		}
	}
	return nullptr;
}

FString UPaper2DPlusClashGraphNode_Tag::GetShortName() const
{
	if (!CategoryTag.IsValid())
	{
		return TEXT("(invalid)");
	}
	const FString Full = CategoryTag.ToString();
	const FString Prefix = TEXT("Paper2DPlus.Clash.Category.");
	if (Full.StartsWith(Prefix))
	{
		return Full.RightChop(Prefix.Len());
	}
	return Full;
}

#undef LOCTEXT_NAMESPACE
