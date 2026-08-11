// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "AnimationMap/Paper2DPlusAnimationMapNode_ChainStart.h"

#include "AnimationMap/SAnimationMapChainStartNode.h"
#include "EdGraph/EdGraphPin.h"

#define LOCTEXT_NAMESPACE "Paper2DPlusAnimationMapNode_ChainStart"

void UPaper2DPlusAnimationMapNode_ChainStart::AllocateDefaultPins()
{
	// The move node's pin recipe (Paper2DPlusAnimationMapNode_Move::AllocateDefaultPins) minus the
	// input: markers only ever AIM at moves, so the single output pin is the whole pin set. Shares the
	// "Transition" category so the schema's GetPinTypeColor White override (hover-cue tint) applies.
	CreatePin(EGPD_Output, TEXT("Transition"), TEXT("Out"));
}

FText UPaper2DPlusAnimationMapNode_ChainStart::GetNodeTitle(ENodeTitleType::Type /*TitleType*/) const
{
	return LOCTEXT("ChainStartNodeTitle", "Chain Start");
}

FLinearColor UPaper2DPlusAnimationMapNode_ChainStart::GetNodeTitleColor() const
{
	// The move node's ▶ glyph amber — the marker and the glyph are one visual family.
	return FLinearColor(0.95f, 0.78f, 0.20f);
}

TSharedPtr<SGraphNode> UPaper2DPlusAnimationMapNode_ChainStart::CreateVisualWidget()
{
	return SNew(SAnimationMapChainStartNode, this);
}

UEdGraphPin* UPaper2DPlusAnimationMapNode_ChainStart::GetOutputPin() const
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

#undef LOCTEXT_NAMESPACE
