// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "AnimationMap/Paper2DPlusAnimationMapNode_Transition.h"

#include "AnimationMap/Paper2DPlusAnimationMap.h"
#include "AnimationMap/Paper2DPlusAnimationMapNode_Move.h"
#include "AnimationMap/SAnimationMapTransitionNode.h"
#include "EdGraph/EdGraphPin.h"

#define LOCTEXT_NAMESPACE "Paper2DPlusAnimationMapNode_Transition"

void UPaper2DPlusAnimationMapNode_Transition::AllocateDefaultPins()
{
	// Clone of UAnimStateTransitionNode::AllocateDefaultPins (AnimStateTransitionNode.cpp:101-107):
	// both pins HIDDEN — the edge node rides the wire, it never shows pins of its own.
	UEdGraphPin* Inputs = CreatePin(EGPD_Input, TEXT("Transition"), TEXT("In"));
	Inputs->bHidden = true;
	UEdGraphPin* Outputs = CreatePin(EGPD_Output, TEXT("Transition"), TEXT("Out"));
	Outputs->bHidden = true;
}

FText UPaper2DPlusAnimationMapNode_Transition::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	// Pure From->To arrow (TASK-108): the effective transition-phase badge is the pill's PRIMARY
	// content; this fixed arrow glyph is the fallback while the row and target carry no phase tag,
	// so the pill always stays visible and clickable.
	return FText::FromString(TEXT("\x2192"));
}

void UPaper2DPlusAnimationMapNode_Transition::PinConnectionListChanged(UEdGraphPin* Pin)
{
	Super::PinConnectionListChanged(Pin);

	// Suicide hook, defused (KTD: the hook NEVER mutates data). The anim-SM original DestroyNode()s
	// itself here; ours only REQUESTS cleanup from the panel — row deletion enters exclusively via the
	// panel's snapshot-validated delete helper, and a re-entrant destroy cascade would double-delete
	// identical duplicate rows (legal per R14). Suppressed while the panel rebuilds the projection
	// (bRebuildInProgress), where link surgery transiently empties LinkedTo lists by design.
	if (Pin && Pin->LinkedTo.Num() == 0)
	{
		UPaper2DPlusAnimationMap* AnimationMap = Cast<UPaper2DPlusAnimationMap>(GetGraph());
		if (AnimationMap && !AnimationMap->bRebuildInProgress)
		{
			AnimationMap->OnEdgeRemovalRequested.ExecuteIfBound(this);
		}
	}
}

void UPaper2DPlusAnimationMapNode_Transition::CreateConnections(
	UPaper2DPlusAnimationMapNode_Move* FromNode, UPaper2DPlusAnimationMapNode_Move* ToNode)
{
	// Clone of UAnimStateTransitionNode::CreateConnections (AnimStateTransitionNode.cpp:340-355):
	// empty LinkedTo, then From.Out -> this.In and this.Out -> To.In. Null-safe beyond the engine
	// original because stubs have no output pin (a stub can never be the FROM side).
	UEdGraphPin* InputPin = GetInputPin();
	UEdGraphPin* OutputPin = GetOutputPin();
	UEdGraphPin* FromOutputPin = FromNode ? FromNode->GetOutputPin() : nullptr;
	UEdGraphPin* ToInputPin = ToNode ? ToNode->GetInputPin() : nullptr;
	if (!InputPin || !OutputPin || !FromOutputPin || !ToInputPin)
	{
		return;
	}

	// Previous to this. (All four owning nodes were spawned non-transactional through the shared
	// funnel, so these Modify()s record nothing — the asset stays the only transacted object.)
	InputPin->Modify();
	InputPin->LinkedTo.Empty();
	FromOutputPin->Modify();
	InputPin->MakeLinkTo(FromOutputPin);

	// This to next.
	OutputPin->Modify();
	OutputPin->LinkedTo.Empty();
	ToInputPin->Modify();
	OutputPin->MakeLinkTo(ToInputPin);
}

UPaper2DPlusAnimationMapNode_Move* UPaper2DPlusAnimationMapNode_Transition::GetPreviousMoveNode() const
{
	const UEdGraphPin* InputPin = GetInputPin();
	if (InputPin && InputPin->LinkedTo.Num() > 0 && InputPin->LinkedTo[0])
	{
		return Cast<UPaper2DPlusAnimationMapNode_Move>(InputPin->LinkedTo[0]->GetOwningNodeUnchecked());
	}
	return nullptr;
}

UPaper2DPlusAnimationMapNode_Move* UPaper2DPlusAnimationMapNode_Transition::GetNextMoveNode() const
{
	const UEdGraphPin* OutputPin = GetOutputPin();
	if (OutputPin && OutputPin->LinkedTo.Num() > 0 && OutputPin->LinkedTo[0])
	{
		return Cast<UPaper2DPlusAnimationMapNode_Move>(OutputPin->LinkedTo[0]->GetOwningNodeUnchecked());
	}
	return nullptr;
}

void UPaper2DPlusAnimationMapNode_Transition::SetFromRow(
	const FString& InFromMove, int32 InRowIndex, const FPaper2DPlusMoveTransition& Row,
	const FGameplayTag& InTransitionPhaseTag)
{
	FromMove = InFromMove;
	RowIndex = InRowIndex;
	TargetMove = Row.TargetMove;
	// Display-only denormalization; the live row owns its override, but the write guard stays identity-based.
	TransitionPhaseTag = InTransitionPhaseTag;
}

FPaper2DPlusMoveTransition UPaper2DPlusAnimationMapNode_Transition::MakeRowSnapshot() const
{
	return FPaper2DPlusMoveTransition(TargetMove);
}

TSharedPtr<SGraphNode> UPaper2DPlusAnimationMapNode_Transition::CreateVisualWidget()
{
	return SNew(SAnimationMapTransitionNode, this);
}

#undef LOCTEXT_NAMESPACE
