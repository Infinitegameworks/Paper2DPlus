// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "AnimationMap/Paper2DPlusAnimationMapNode_ChainEnd.h"

#include "AnimationMap/SAnimationMapChainEndNode.h"
#include "EdGraph/EdGraphPin.h"

#define LOCTEXT_NAMESPACE "Paper2DPlusAnimationMapNode_ChainEnd"

void UPaper2DPlusAnimationMapNode_ChainEnd::AllocateDefaultPins()
{
	// The Chain Start marker's pin recipe MIRRORED (Paper2DPlusAnimationMapNode_ChainStart minus the
	// output, plus the input): moves only ever aim INTO end markers, so the single input pin is the
	// whole pin set. Shares the "Transition" category so the schema's GetPinTypeColor White override
	// (hover-cue tint) applies.
	CreatePin(EGPD_Input, TEXT("Transition"), TEXT("In"));
}

FText UPaper2DPlusAnimationMapNode_ChainEnd::GetNodeTitle(ENodeTitleType::Type /*TitleType*/) const
{
	return LOCTEXT("ChainEndNodeTitle", "Chain End");
}

FLinearColor UPaper2DPlusAnimationMapNode_ChainEnd::GetNodeTitleColor() const
{
	// The move node's ⏹ glyph slate-blue — the marker and the glyph are one visual family, and the
	// tint deliberately reads OPPOSITE the start marker's amber so the pair never blur together.
	return FLinearColor(0.45f, 0.55f, 0.95f);
}

TSharedPtr<SGraphNode> UPaper2DPlusAnimationMapNode_ChainEnd::CreateVisualWidget()
{
	return SNew(SAnimationMapChainEndNode, this);
}

UEdGraphPin* UPaper2DPlusAnimationMapNode_ChainEnd::GetInputPin() const
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

#undef LOCTEXT_NAMESPACE
