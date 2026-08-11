// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "ClashGraph/Paper2DPlusClashGraphSchema.h"

#include "ClashGraph/Paper2DPlusClashGraph.h"
#include "ClashGraph/Paper2DPlusClashGraphNode_Tag.h"
#include "EdGraph/EdGraphPin.h"

#define LOCTEXT_NAMESPACE "Paper2DPlusClashGraphSchema"

namespace
{
	/** Resolve the directed (Winner, Loser) tags from two pins: the OUTPUT pin's node is the Winner, the
	 *  INPUT pin's node is the Loser. Returns false unless the pins are one-output + one-input on tag nodes. */
	bool ClashSchema_ResolveWinnerLoser(const UEdGraphPin* PinX, const UEdGraphPin* PinY,
		FGameplayTag& OutWinner, FGameplayTag& OutLoser)
	{
		if (!PinX || !PinY)
		{
			return false;
		}
		const UEdGraphPin* OutputPin = nullptr;
		const UEdGraphPin* InputPin = nullptr;
		if (PinX->Direction == EGPD_Output && PinY->Direction == EGPD_Input)
		{
			OutputPin = PinX;
			InputPin = PinY;
		}
		else if (PinX->Direction == EGPD_Input && PinY->Direction == EGPD_Output)
		{
			OutputPin = PinY;
			InputPin = PinX;
		}
		else
		{
			return false;
		}

		const UPaper2DPlusClashGraphNode_Tag* WinnerNode = Cast<UPaper2DPlusClashGraphNode_Tag>(OutputPin->GetOwningNodeUnchecked());
		const UPaper2DPlusClashGraphNode_Tag* LoserNode = Cast<UPaper2DPlusClashGraphNode_Tag>(InputPin->GetOwningNodeUnchecked());
		if (!WinnerNode || !LoserNode)
		{
			return false;
		}
		OutWinner = WinnerNode->CategoryTag;
		OutLoser = LoserNode->CategoryTag;
		return true;
	}

	/** The owning clash graph of a pin's node (null if not a clash graph). */
	UPaper2DPlusClashGraph* ClashSchema_GraphOf(const UEdGraphPin* Pin)
	{
		const UEdGraphNode* Node = Pin ? Pin->GetOwningNodeUnchecked() : nullptr;
		return Node ? Cast<UPaper2DPlusClashGraph>(Node->GetGraph()) : nullptr;
	}
}

const FPinConnectionResponse UPaper2DPlusClashGraphSchema::CanCreateConnection(
	const UEdGraphPin* PinA, const UEdGraphPin* PinB) const
{
	const UEdGraphNode* NodeA = PinA ? PinA->GetOwningNodeUnchecked() : nullptr;
	const UEdGraphNode* NodeB = PinB ? PinB->GetOwningNodeUnchecked() : nullptr;
	if (!NodeA || !NodeB)
	{
		return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, LOCTEXT("ClashInvalidPin", "Invalid pin"));
	}

	if (!NodeA->IsA<UPaper2DPlusClashGraphNode_Tag>() || !NodeB->IsA<UPaper2DPlusClashGraphNode_Tag>())
	{
		return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW,
			LOCTEXT("ClashTagNodesOnly", "Only category nodes can be connected"));
	}

	// A category cannot beat itself — the validator flags an exact self-loop as a Warning; the graph editor
	// refuses to author one (the deliberate divergence from the Animation Map's legal self-transitions).
	if (NodeA == NodeB)
	{
		return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW,
			LOCTEXT("ClashNoSelfBeat", "A category cannot beat itself"));
	}

	// Plain wire: TryCreateConnection normalizes the direction (output = Winner, input = Loser) and routes
	// the data write + the link through the panel handler. The same-direction case (a drop on a node body
	// resolving to its like pin) is swapped there.
	return FPinConnectionResponse(CONNECT_RESPONSE_MAKE, LOCTEXT("ClashCreateBeats", "Create a 'beats' relationship"));
}

bool UPaper2DPlusClashGraphSchema::TryCreateConnection(UEdGraphPin* PinA, UEdGraphPin* PinB) const
{
	// Same-direction pin swap (cloned from the Animation Map schema): when a drag lands on a pin of the SAME
	// direction (out->out / in->in — e.g. dropping on the node body resolves to its like pin), substitute the
	// target node's OPPOSITE pin so the gesture still forms a directed wire.
	if (PinA && PinB && PinB->Direction == PinA->Direction)
	{
		if (const UPaper2DPlusClashGraphNode_Tag* TagNode = Cast<UPaper2DPlusClashGraphNode_Tag>(PinB->GetOwningNodeUnchecked()))
		{
			UEdGraphPin* SwappedPin = (PinA->Direction == EGPD_Input) ? TagNode->GetOutputPin() : TagNode->GetInputPin();
			if (!SwappedPin)
			{
				return false;
			}
			PinB = SwappedPin;
		}
	}

	FGameplayTag Winner;
	FGameplayTag Loser;
	if (!ClashSchema_ResolveWinnerLoser(PinA, PinB, Winner, Loser) || Winner == Loser)
	{
		return false;
	}

	UPaper2DPlusClashGraph* Graph = ClashSchema_GraphOf(PinA);
	if (!Graph || Graph->bRebuildInProgress)
	{
		// Hooks never act during a panel reconcile (the bRebuildInProgress lock).
		return false;
	}

	// Schema methods run on the CDO and hold no state: the data write-through (append edge) AND the visual
	// pin link both live in the panel-bound delegate (HIGH #1 — the handler makes the link directly, then
	// sets LastProjection so the gesture's own Modify echo diffs empty and the wire survives). The schema
	// does NOT call Super::TryCreateConnection — the handler owns the link. Unbound/refused => no connection.
	return Graph->OnWireCreateRequested.IsBound()
		&& Graph->OnWireCreateRequested.Execute(Winner, Loser);
}

FLinearColor UPaper2DPlusClashGraphSchema::GetPinTypeColor(const FEdGraphPinType& PinType) const
{
	// White for the one category this graph allocates ("Clash"); Super (black) otherwise — so the standard
	// pin/wire renders visibly (the base UEdGraphSchema default is black).
	if (PinType.PinCategory == TEXT("Clash"))
	{
		return FLinearColor::White;
	}
	return Super::GetPinTypeColor(PinType);
}

void UPaper2DPlusClashGraphSchema::BreakSinglePinLink(UEdGraphPin* SourcePin, UEdGraphPin* TargetPin) const
{
	UPaper2DPlusClashGraph* Graph = ClashSchema_GraphOf(SourcePin);
	if (!Graph)
	{
		Graph = ClashSchema_GraphOf(TargetPin);
	}
	if (!Graph || Graph->bRebuildInProgress)
	{
		// Reconcile teardown (or a non-clash graph): raw Super behavior, never the user-remove path.
		Super::BreakSinglePinLink(SourcePin, TargetPin);
		return;
	}

	// USER break of one wire: resolve (Winner, Loser) and route the row removal through the panel. The panel
	// removes the row + RequestReconcile WITHOUT setting LastProjection, so the deferred rebuild tears this
	// (now-orphaned) visual wire — do NOT call Super here (that would double-tear / race the rebuild).
	FGameplayTag Winner;
	FGameplayTag Loser;
	if (ClashSchema_ResolveWinnerLoser(SourcePin, TargetPin, Winner, Loser))
	{
		Graph->OnWireRemoveRequested.ExecuteIfBound(Winner, Loser);
	}
}

void UPaper2DPlusClashGraphSchema::BreakPinLinks(UEdGraphPin& TargetPin, bool bSendsNodeNotification) const
{
	UPaper2DPlusClashGraph* Graph = ClashSchema_GraphOf(&TargetPin);
	if (!Graph || Graph->bRebuildInProgress)
	{
		Super::BreakPinLinks(TargetPin, bSendsNodeNotification);
		return;
	}

	// USER "break all links on this pin" — multi-link: enumerate a COPY of LinkedTo (the handler does not
	// touch pins on the remove path, but copy defensively) and remove each (Winner, Loser) row. The deferred
	// reconcile tears the visual wires.
	const TArray<UEdGraphPin*> LinkedCopy = TargetPin.LinkedTo;
	for (UEdGraphPin* LinkedPin : LinkedCopy)
	{
		FGameplayTag Winner;
		FGameplayTag Loser;
		if (ClashSchema_ResolveWinnerLoser(&TargetPin, LinkedPin, Winner, Loser))
		{
			Graph->OnWireRemoveRequested.ExecuteIfBound(Winner, Loser);
		}
	}
}

void UPaper2DPlusClashGraphSchema::BreakNodeLinks(UEdGraphNode& TargetNode) const
{
	UPaper2DPlusClashGraph* Graph = Cast<UPaper2DPlusClashGraph>(TargetNode.GetGraph());
	if (!Graph || Graph->bRebuildInProgress)
	{
		Super::BreakNodeLinks(TargetNode);
		return;
	}

	// USER break of all of a node's links — remove every (Winner, Loser) row touching this node's pins.
	for (UEdGraphPin* Pin : TargetNode.Pins)
	{
		if (!Pin)
		{
			continue;
		}
		const TArray<UEdGraphPin*> LinkedCopy = Pin->LinkedTo;
		for (UEdGraphPin* LinkedPin : LinkedCopy)
		{
			FGameplayTag Winner;
			FGameplayTag Loser;
			if (ClashSchema_ResolveWinnerLoser(Pin, LinkedPin, Winner, Loser))
			{
				Graph->OnWireRemoveRequested.ExecuteIfBound(Winner, Loser);
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE
