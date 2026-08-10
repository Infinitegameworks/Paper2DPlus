// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "AnimationMap/Paper2DPlusAnimationMapSchema.h"

#include "AnimationMap/AnimationMapConnectionDrawingPolicy.h"
#include "AnimationMap/Paper2DPlusAnimationMap.h"
#include "AnimationMap/Paper2DPlusAnimationMapNode_ChainEnd.h"
#include "AnimationMap/Paper2DPlusAnimationMapNode_ChainStart.h"
#include "AnimationMap/Paper2DPlusAnimationMapNode_Move.h"
#include "AnimationMap/Paper2DPlusAnimationMapNode_Transition.h"
#include "EdGraph/EdGraphPin.h"

#define LOCTEXT_NAMESPACE "Paper2DPlusAnimationMapSchema"

/////////////////////////////////////////////////////
// FAnimationMapSchemaAction_AddChainStart

FAnimationMapSchemaAction_AddChainStart::FAnimationMapSchemaAction_AddChainStart()
	: FEdGraphSchemaAction(
		LOCTEXT("ChainStartActionCategory", "Animation Map"),
		LOCTEXT("AddChainStartAction", "Add Chain Start"),
		LOCTEXT("AddChainStartActionTip",
			"Place a floating Chain Start marker. Drag a wire from its border onto a move to designate that move as this group's combo-chain opener."),
		/*InGrouping=*/0)
{
}

UEdGraphNode* FAnimationMapSchemaAction_AddChainStart::PerformAddChainStart(
	UEdGraph* ParentGraph, UEdGraphPin* FromPin, const FVector2D& Location)
{
	// Action layer = pure routing (the schema/action never spawns nodes itself — the panel owns the
	// untransactional spawn funnel). bRebuildInProgress-gated like every other hook.
	UPaper2DPlusAnimationMap* AnimationMap = Cast<UPaper2DPlusAnimationMap>(ParentGraph);
	if (!AnimationMap || AnimationMap->bRebuildInProgress)
	{
		return nullptr;
	}
	UEdGraphNode* Spawned = AnimationMap->OnAddChainStartRequested.IsBound()
		? AnimationMap->OnAddChainStartRequested.Execute(Location)
		: nullptr;
	if (!FromPin || !Spawned)
	{
		return Spawned;
	}
	UPaper2DPlusAnimationMapNode_Move* SourceMove =
		Cast<UPaper2DPlusAnimationMapNode_Move>(FromPin->GetOwningNodeUnchecked());
	UPaper2DPlusAnimationMapNode_ChainStart* Marker =
		Cast<UPaper2DPlusAnimationMapNode_ChainStart>(Spawned);
	if (!SourceMove || SourceMove->bIsStub || !Marker
		|| !AnimationMap->OnChainStartLinkRequested.IsBound()
		|| !AnimationMap->OnChainStartLinkRequested.Execute(Marker, SourceMove->MoveName))
	{
		AnimationMap->RemoveNode(Spawned);
		return nullptr;
	}
	return Spawned;
}

/////////////////////////////////////////////////////
// FAnimationMapSchemaAction_AddChainEnd

FAnimationMapSchemaAction_AddChainEnd::FAnimationMapSchemaAction_AddChainEnd()
	: FEdGraphSchemaAction(
		LOCTEXT("ChainEndActionCategory", "Animation Map"),
		LOCTEXT("AddChainEndAction", "Add Chain End"),
		LOCTEXT("AddChainEndActionTip",
			"Place a floating Chain End marker. Drag a wire from a move onto its border to designate that move as this group's combo main-line end."),
		/*InGrouping=*/0)
{
}

UEdGraphNode* FAnimationMapSchemaAction_AddChainEnd::PerformAddChainEnd(
	UEdGraph* ParentGraph, UEdGraphPin* FromPin, const FVector2D& Location)
{
	// Action layer = pure routing (the schema/action never spawns nodes itself — the panel owns the
	// untransactional spawn funnel). bRebuildInProgress-gated like every other hook.
	UPaper2DPlusAnimationMap* AnimationMap = Cast<UPaper2DPlusAnimationMap>(ParentGraph);
	if (!AnimationMap || AnimationMap->bRebuildInProgress)
	{
		return nullptr;
	}
	UEdGraphNode* Spawned = AnimationMap->OnAddChainEndRequested.IsBound()
		? AnimationMap->OnAddChainEndRequested.Execute(Location)
		: nullptr;
	if (!FromPin || !Spawned)
	{
		return Spawned;
	}
	UPaper2DPlusAnimationMapNode_Move* SourceMove =
		Cast<UPaper2DPlusAnimationMapNode_Move>(FromPin->GetOwningNodeUnchecked());
	UPaper2DPlusAnimationMapNode_ChainEnd* Marker = Cast<UPaper2DPlusAnimationMapNode_ChainEnd>(Spawned);
	if (!SourceMove || SourceMove->bIsStub || !Marker
		|| !AnimationMap->OnChainEndLinkRequested.IsBound()
		|| !AnimationMap->OnChainEndLinkRequested.Execute(Marker, SourceMove->MoveName))
	{
		AnimationMap->RemoveNode(Spawned);
		return nullptr;
	}
	return Spawned;
}

/////////////////////////////////////////////////////
// UPaper2DPlusAnimationMapSchema

const FPinConnectionResponse UPaper2DPlusAnimationMapSchema::CanCreateConnection(
	const UEdGraphPin* PinA, const UEdGraphPin* PinB) const
{
	// Null-safety first: stub nodes own no output pin at all, so a half-formed gesture can reach the
	// schema with a missing pin/node — disallow rather than special-case stubs anywhere below.
	// (Unchecked accessor: GetOwningNode() check()s on a null owner, EdGraphPin.h:468.)
	const UEdGraphNode* NodeA = PinA ? PinA->GetOwningNodeUnchecked() : nullptr;
	const UEdGraphNode* NodeB = PinB ? PinB->GetOwningNodeUnchecked() : nullptr;
	if (!NodeA || !NodeB)
	{
		return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, LOCTEXT("AnimationMapInvalidPin", "Invalid pin"));
	}

	// The transition pill exposes only its OUTPUT as a small target-rewire grip. Dragging that grip to
	// a real move replaces the existing target; every other direct transition-pin combination stays
	// forbidden.
	if (const UPaper2DPlusAnimationMapNode_Transition* EdgeNode =
		Cast<UPaper2DPlusAnimationMapNode_Transition>(NodeA))
	{
		const UPaper2DPlusAnimationMapNode_Move* TargetMove = Cast<UPaper2DPlusAnimationMapNode_Move>(NodeB);
		if (PinA == EdgeNode->GetOutputPin() && TargetMove && !TargetMove->bIsStub)
		{
			if (EdgeNode->TargetMove.Equals(TargetMove->MoveName, ESearchCase::IgnoreCase))
			{
				return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW,
					LOCTEXT("AnimationMapRewireSameTarget", "This transition already targets that move"));
			}
			return FPinConnectionResponse(CONNECT_RESPONSE_MAKE,
				LOCTEXT("AnimationMapRewireTarget", "Replace the transition target"));
		}
	}
	if (NodeA->IsA<UPaper2DPlusAnimationMapNode_Transition>() || NodeB->IsA<UPaper2DPlusAnimationMapNode_Transition>())
	{
		return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW,
			LOCTEXT("AnimationMapNoTransitionWires", "Drag the transition pill's target grip onto a move"));
	}

	// Chain Start / Chain End markers (the mirrored marker pair): a start marker's single output pin
	// aims AT exactly one REAL move node (the wire designates the group's combo-chain opener); an end
	// marker's single input pin is aimed AT (a move's output wires INTO it — the wire designates the
	// group's combo main-line end). Everything else refuses with the existing refusal-message
	// pattern. (Each marker owns one pin, so PinA/PinB direction mixes are decided by the node kinds,
	// not pin direction — the move side's same-direction drop is normalized by TryCreateConnection's
	// swap, exactly like move->move gestures.)
	const bool bAIsChainStart = NodeA->IsA<UPaper2DPlusAnimationMapNode_ChainStart>();
	const bool bBIsChainStart = NodeB->IsA<UPaper2DPlusAnimationMapNode_ChainStart>();
	const bool bAIsChainEnd = NodeA->IsA<UPaper2DPlusAnimationMapNode_ChainEnd>();
	const bool bBIsChainEnd = NodeB->IsA<UPaper2DPlusAnimationMapNode_ChainEnd>();
	if ((bAIsChainStart || bAIsChainEnd) && (bBIsChainStart || bBIsChainEnd))
	{
		// Marker-to-marker (start-start, end-end, and the cross pairs) — markers connect to MOVES.
		return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW,
			LOCTEXT("AnimationMapMarkerToMarker", "Chain markers connect to moves, not to each other"));
	}
	if (bAIsChainStart || bBIsChainStart)
	{
		// The non-marker side must be a MOVE node (transitions were refused above).
		const UPaper2DPlusAnimationMapNode_Move* MoveNode =
			Cast<UPaper2DPlusAnimationMapNode_Move>(bAIsChainStart ? NodeB : NodeA);
		if (!MoveNode)
		{
			return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW,
				LOCTEXT("AnimationMapChainStartMovesOnly", "A Chain Start marker can only aim at a move"));
		}
		// Stubs own no mapping entry — there is no flag to set (R3's "stubs cannot own rows" sibling).
		if (MoveNode->bIsStub)
		{
			return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW,
				LOCTEXT("AnimationMapChainStartNoStubs", "A missing move cannot be a chain start"));
		}
		// The gesture must START at the marker (spec: FROM the marker's output pin). PinA is the
		// DRAGGED pin (FDragConnection passes dragged-first to both Can/TryCreateConnection) — a drag
		// FROM a move dropped ON the marker reads backwards, so refuse it for clarity.
		if (!bAIsChainStart)
		{
			return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW,
				LOCTEXT("AnimationMapChainStartInvalidPin", "Drag from the Chain Start marker onto a move"));
		}
		return FPinConnectionResponse(CONNECT_RESPONSE_MAKE,
			LOCTEXT("AnimationMapSetChainStart", "Set this move as the chain start"));
	}
	if (bAIsChainEnd || bBIsChainEnd)
	{
		// The non-marker side must be a MOVE node (transitions were refused above).
		const UPaper2DPlusAnimationMapNode_Move* MoveNode =
			Cast<UPaper2DPlusAnimationMapNode_Move>(bAIsChainEnd ? NodeB : NodeA);
		if (!MoveNode)
		{
			return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW,
				LOCTEXT("AnimationMapChainEndMovesOnly", "Only a move can wire into a Chain End marker"));
		}
		// Stubs own no mapping entry — there is no flag to set (the start marker's rule mirrored).
		if (MoveNode->bIsStub)
		{
			return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW,
				LOCTEXT("AnimationMapChainEndNoStubs", "A missing move cannot be a chain end"));
		}
		// The gesture must START at the MOVE (spec: move output -> end marker input — the start
		// gesture's direction MIRRORED). PinA is the DRAGGED pin — a drag OUT of the end marker's
		// input reads backwards, so refuse it for clarity.
		if (bAIsChainEnd)
		{
			return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW,
				LOCTEXT("AnimationMapChainEndInvalidPin", "Drag from a move onto the Chain End marker"));
		}
		return FPinConnectionResponse(CONNECT_RESPONSE_MAKE,
			LOCTEXT("AnimationMapSetChainEnd", "Set this move as the chain end"));
	}

	// Move-pin <-> move-pin, INCLUDING the same node: self-transitions are legal data with a documented
	// runtime recipe (R14) — a DELIBERATE divergence from the engine state machine's same-node
	// rejection (AnimationStateMachineSchema.cpp:145-151). The conversion-node response routes the
	// gesture into CreateAutomaticConversionNodeAndConnections below.
	if (NodeA->IsA<UPaper2DPlusAnimationMapNode_Move>() && NodeB->IsA<UPaper2DPlusAnimationMapNode_Move>())
	{
		return FPinConnectionResponse(CONNECT_RESPONSE_MAKE_WITH_CONVERSION_NODE,
			LOCTEXT("AnimationMapCreateTransition", "Create a transition"));
	}

	return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW,
		LOCTEXT("AnimationMapMovesOnly", "Only move nodes can be connected"));
}

bool UPaper2DPlusAnimationMapSchema::TryCreateConnection(UEdGraphPin* PinA, UEdGraphPin* PinB) const
{
	// Same-direction pin swap, cloned from UAnimationStateMachineSchema::TryCreateConnection
	// (AnimationStateMachineSchema.cpp:216-242): when a drag lands on a pin of the SAME direction
	// (out->out / in->in — e.g. dropping on the node body resolves to its like pin), substitute the
	// target node's OPPOSITE pin so the gesture still forms a wire. Extra null-safety vs the engine
	// original: a stub's output pin does not exist, so a swap toward a stub's output aborts cleanly.
	if (PinA && PinB && PinB->Direction == PinA->Direction)
	{
		if (const UPaper2DPlusAnimationMapNode_Move* MoveNode = Cast<UPaper2DPlusAnimationMapNode_Move>(PinB->GetOwningNodeUnchecked()))
		{
			UEdGraphPin* SwappedPin = (PinA->Direction == EGPD_Input) ? MoveNode->GetOutputPin() : MoveNode->GetInputPin();
			if (!SwappedPin)
			{
				return false;
			}
			PinB = SwappedPin;
		}
	}

	// Chain-start rework: a wire from a marker's output onto a move routes through the graph-owned
	// delegate INSTEAD of Super — the panel handler owns both the flag write AND the link surgery
	// (Super would form a bare pin link with no data behind it). CanCreateConnection already refused
	// every other marker combination, so reaching here with a marker means marker(A) -> move(B).
	if (PinA && PinB)
	{
		UPaper2DPlusAnimationMapNode_Transition* EdgeNode =
			Cast<UPaper2DPlusAnimationMapNode_Transition>(PinA->GetOwningNodeUnchecked());
		UPaper2DPlusAnimationMapNode_Move* RewireTarget =
			Cast<UPaper2DPlusAnimationMapNode_Move>(PinB->GetOwningNodeUnchecked());
		if (EdgeNode && PinA == EdgeNode->GetOutputPin() && RewireTarget && !RewireTarget->bIsStub)
		{
			UPaper2DPlusAnimationMap* AnimationMap = Cast<UPaper2DPlusAnimationMap>(EdgeNode->GetGraph());
			return AnimationMap && !AnimationMap->bRebuildInProgress
				&& AnimationMap->OnTransitionTargetRewireRequested.IsBound()
				&& AnimationMap->OnTransitionTargetRewireRequested.Execute(EdgeNode, RewireTarget->MoveName);
		}
		if (EdgeNode || Cast<UPaper2DPlusAnimationMapNode_Transition>(PinB->GetOwningNodeUnchecked()))
		{
			return false;
		}

		UPaper2DPlusAnimationMapNode_ChainStart* MarkerNode =
			Cast<UPaper2DPlusAnimationMapNode_ChainStart>(PinA->GetOwningNodeUnchecked());
		UPaper2DPlusAnimationMapNode_Move* TargetMoveNode =
			Cast<UPaper2DPlusAnimationMapNode_Move>(PinB->GetOwningNodeUnchecked());
		if (MarkerNode && TargetMoveNode)
		{
			UPaper2DPlusAnimationMap* AnimationMap = Cast<UPaper2DPlusAnimationMap>(MarkerNode->GetGraph());
			if (!AnimationMap || AnimationMap->bRebuildInProgress)
			{
				return false; // hooks never act during a panel reconcile (the bRebuildInProgress lock)
			}
			return AnimationMap->OnChainStartLinkRequested.IsBound()
				&& AnimationMap->OnChainStartLinkRequested.Execute(MarkerNode, TargetMoveNode->MoveName);
		}
		if (MarkerNode || Cast<UPaper2DPlusAnimationMapNode_ChainStart>(PinB->GetOwningNodeUnchecked()))
		{
			return false; // belt-and-braces: no other marker gesture may fall through to Super's raw link
		}

		// Chain-end rework (the marker route MIRRORED): a wire from a real move's output onto an end
		// marker's input routes through the graph-owned delegate INSTEAD of Super — the panel handler
		// owns both the flag write AND the link surgery (Super would form a bare pin link with no data
		// behind it). CanCreateConnection already refused every other end-marker combination, so
		// reaching here with an end marker means move(A) -> marker(B).
		UPaper2DPlusAnimationMapNode_ChainEnd* EndMarkerNode =
			Cast<UPaper2DPlusAnimationMapNode_ChainEnd>(PinB->GetOwningNodeUnchecked());
		UPaper2DPlusAnimationMapNode_Move* SourceMoveNode =
			Cast<UPaper2DPlusAnimationMapNode_Move>(PinA->GetOwningNodeUnchecked());
		if (EndMarkerNode && SourceMoveNode)
		{
			UPaper2DPlusAnimationMap* AnimationMap = Cast<UPaper2DPlusAnimationMap>(EndMarkerNode->GetGraph());
			if (!AnimationMap || AnimationMap->bRebuildInProgress)
			{
				return false; // hooks never act during a panel reconcile (the bRebuildInProgress lock)
			}
			return AnimationMap->OnChainEndLinkRequested.IsBound()
				&& AnimationMap->OnChainEndLinkRequested.Execute(EndMarkerNode, SourceMoveNode->MoveName);
		}
		if (EndMarkerNode || Cast<UPaper2DPlusAnimationMapNode_ChainEnd>(PinA->GetOwningNodeUnchecked()))
		{
			return false; // belt-and-braces: no other end-marker gesture may fall through to Super's raw link
		}
	}

	return Super::TryCreateConnection(PinA, PinB);
}

void UPaper2DPlusAnimationMapSchema::GetGraphContextActions(FGraphContextMenuBuilder& ContextMenuBuilder) const
{
	Super::GetGraphContextActions(ContextMenuBuilder);

	// Empty canvas offers both marker kinds. A move card's interactive drag surface is its OUTPUT pin,
	// so a drag from any real move pin must also offer BOTH actions: choosing Start places a marker
	// that aims into the source move; choosing End places one the source move aims into. Performing a
	// pin-scoped action spawns and links atomically; refusal removes the just-spawned marker.
	const UPaper2DPlusAnimationMap* AnimationMap = Cast<const UPaper2DPlusAnimationMap>(ContextMenuBuilder.CurrentGraph);
	if (AnimationMap && AnimationMap->bHasExactGroupScope && !AnimationMap->bRebuildInProgress)
	{
		if (!ContextMenuBuilder.FromPin)
		{
			ContextMenuBuilder.AddAction(MakeShared<FAnimationMapSchemaAction_AddChainStart>());
			ContextMenuBuilder.AddAction(MakeShared<FAnimationMapSchemaAction_AddChainEnd>());
			return;
		}
		const UPaper2DPlusAnimationMapNode_Move* MoveNode =
			Cast<UPaper2DPlusAnimationMapNode_Move>(ContextMenuBuilder.FromPin->GetOwningNodeUnchecked());
		if (!MoveNode || MoveNode->bIsStub)
		{
			return;
		}
		ContextMenuBuilder.AddAction(MakeShared<FAnimationMapSchemaAction_AddChainStart>());
		ContextMenuBuilder.AddAction(MakeShared<FAnimationMapSchemaAction_AddChainEnd>());
	}
}

namespace
{
	/** File-scope helper (unity rule: file-unique name): fire the graph's unlink delegate for every
	 *  DISTINCT Chain Start marker owning a pin in Links. Returns the number fired. The panel handler
	 *  performs the flag clear + the marker's own link surgery, so callers must NOT ALSO Super-break
	 *  the marker's pins (nothing is left to break; the marker goes floating). */
	int32 AnimationMapSchema_FireUnlinkForMarkersIn(const TArray<UEdGraphPin*>& Links)
	{
		TArray<UPaper2DPlusAnimationMapNode_ChainStart*> Markers;
		for (UEdGraphPin* LinkedPin : Links)
		{
			if (!LinkedPin)
			{
				continue;
			}
			if (UPaper2DPlusAnimationMapNode_ChainStart* Marker =
				Cast<UPaper2DPlusAnimationMapNode_ChainStart>(LinkedPin->GetOwningNodeUnchecked()))
			{
				Markers.AddUnique(Marker);
			}
		}
		int32 Fired = 0;
		for (UPaper2DPlusAnimationMapNode_ChainStart* Marker : Markers)
		{
			UPaper2DPlusAnimationMap* AnimationMap = Cast<UPaper2DPlusAnimationMap>(Marker->GetGraph());
			if (AnimationMap && AnimationMap->OnChainStartUnlinkRequested.IsBound())
			{
				AnimationMap->OnChainStartUnlinkRequested.Execute(Marker);
				++Fired;
			}
		}
		return Fired;
	}

	/** The Chain End mirror of the helper above (file-unique name): fire the graph's end-unlink
	 *  delegate for every DISTINCT Chain End marker owning a pin in Links. Returns the number fired.
	 *  Same caller contract — the panel handler performs the flag clear + the marker's own link
	 *  surgery, so callers must NOT ALSO Super-break the marker's pins. */
	int32 AnimationMapSchema_FireUnlinkForChainEndsIn(const TArray<UEdGraphPin*>& Links)
	{
		TArray<UPaper2DPlusAnimationMapNode_ChainEnd*> Markers;
		for (UEdGraphPin* LinkedPin : Links)
		{
			if (!LinkedPin)
			{
				continue;
			}
			if (UPaper2DPlusAnimationMapNode_ChainEnd* Marker =
				Cast<UPaper2DPlusAnimationMapNode_ChainEnd>(LinkedPin->GetOwningNodeUnchecked()))
			{
				Markers.AddUnique(Marker);
			}
		}
		int32 Fired = 0;
		for (UPaper2DPlusAnimationMapNode_ChainEnd* Marker : Markers)
		{
			UPaper2DPlusAnimationMap* AnimationMap = Cast<UPaper2DPlusAnimationMap>(Marker->GetGraph());
			if (AnimationMap && AnimationMap->OnChainEndUnlinkRequested.IsBound())
			{
				AnimationMap->OnChainEndUnlinkRequested.Execute(Marker);
				++Fired;
			}
		}
		return Fired;
	}
}

void UPaper2DPlusAnimationMapSchema::BreakNodeLinks(UEdGraphNode& TargetNode) const
{
	// bRebuildInProgress-gated hook pattern: reconcile-internal surgery takes the raw Super path.
	UPaper2DPlusAnimationMap* AnimationMap = Cast<UPaper2DPlusAnimationMap>(TargetNode.GetGraph());
	if (!AnimationMap || AnimationMap->bRebuildInProgress)
	{
		Super::BreakNodeLinks(TargetNode);
		return;
	}
	// Breaking ON a marker = the unaim gesture (flag clear via the panel; marker goes floating).
	if (UPaper2DPlusAnimationMapNode_ChainStart* Marker = Cast<UPaper2DPlusAnimationMapNode_ChainStart>(&TargetNode))
	{
		if (AnimationMap->OnChainStartUnlinkRequested.IsBound())
		{
			AnimationMap->OnChainStartUnlinkRequested.Execute(Marker);
		}
		return;
	}
	if (UPaper2DPlusAnimationMapNode_ChainEnd* EndMarker = Cast<UPaper2DPlusAnimationMapNode_ChainEnd>(&TargetNode))
	{
		if (AnimationMap->OnChainEndUnlinkRequested.IsBound())
		{
			AnimationMap->OnChainEndUnlinkRequested.Execute(EndMarker);
		}
		return;
	}
	// Breaking on a MOVE node: route its marker links (both kinds) through the unaim gestures first
	// (the panel breaks those pins itself), then Super severs whatever remains (transition hidden
	// pins — the suicide hook's existing cleanup path).
	for (UEdGraphPin* Pin : TargetNode.Pins)
	{
		if (Pin)
		{
			AnimationMapSchema_FireUnlinkForMarkersIn(Pin->LinkedTo);
			AnimationMapSchema_FireUnlinkForChainEndsIn(Pin->LinkedTo);
		}
	}
	Super::BreakNodeLinks(TargetNode);
}

void UPaper2DPlusAnimationMapSchema::BreakPinLinks(UEdGraphPin& TargetPin, bool bSendsNodeNotifcation) const
{
	UPaper2DPlusAnimationMap* AnimationMap = Cast<UPaper2DPlusAnimationMap>(TargetPin.GetOwningNodeUnchecked()
		? TargetPin.GetOwningNodeUnchecked()->GetGraph() : nullptr);
	if (!AnimationMap || AnimationMap->bRebuildInProgress)
	{
		Super::BreakPinLinks(TargetPin, bSendsNodeNotifcation);
		return;
	}
	if (UPaper2DPlusAnimationMapNode_ChainStart* Marker =
		Cast<UPaper2DPlusAnimationMapNode_ChainStart>(TargetPin.GetOwningNodeUnchecked()))
	{
		if (AnimationMap->OnChainStartUnlinkRequested.IsBound())
		{
			AnimationMap->OnChainStartUnlinkRequested.Execute(Marker);
		}
		return;
	}
	if (UPaper2DPlusAnimationMapNode_ChainEnd* EndMarker =
		Cast<UPaper2DPlusAnimationMapNode_ChainEnd>(TargetPin.GetOwningNodeUnchecked()))
	{
		if (AnimationMap->OnChainEndUnlinkRequested.IsBound())
		{
			AnimationMap->OnChainEndUnlinkRequested.Execute(EndMarker);
		}
		return;
	}
	AnimationMapSchema_FireUnlinkForMarkersIn(TargetPin.LinkedTo);
	AnimationMapSchema_FireUnlinkForChainEndsIn(TargetPin.LinkedTo);
	Super::BreakPinLinks(TargetPin, bSendsNodeNotifcation);
}

void UPaper2DPlusAnimationMapSchema::BreakSinglePinLink(UEdGraphPin* SourcePin, UEdGraphPin* TargetPin) const
{
	UEdGraphNode* SourceNode = SourcePin ? SourcePin->GetOwningNodeUnchecked() : nullptr;
	UEdGraphNode* TargetNode = TargetPin ? TargetPin->GetOwningNodeUnchecked() : nullptr;
	UPaper2DPlusAnimationMap* AnimationMap = SourceNode ? Cast<UPaper2DPlusAnimationMap>(SourceNode->GetGraph()) : nullptr;
	if (!AnimationMap || AnimationMap->bRebuildInProgress)
	{
		Super::BreakSinglePinLink(SourcePin, TargetPin);
		return;
	}
	UPaper2DPlusAnimationMapNode_ChainStart* Marker = Cast<UPaper2DPlusAnimationMapNode_ChainStart>(SourceNode);
	if (!Marker)
	{
		Marker = Cast<UPaper2DPlusAnimationMapNode_ChainStart>(TargetNode);
	}
	if (Marker)
	{
		if (AnimationMap->OnChainStartUnlinkRequested.IsBound())
		{
			AnimationMap->OnChainStartUnlinkRequested.Execute(Marker);
		}
		return;
	}
	UPaper2DPlusAnimationMapNode_ChainEnd* EndMarker = Cast<UPaper2DPlusAnimationMapNode_ChainEnd>(SourceNode);
	if (!EndMarker)
	{
		EndMarker = Cast<UPaper2DPlusAnimationMapNode_ChainEnd>(TargetNode);
	}
	if (EndMarker)
	{
		if (AnimationMap->OnChainEndUnlinkRequested.IsBound())
		{
			AnimationMap->OnChainEndUnlinkRequested.Execute(EndMarker);
		}
		return;
	}
	Super::BreakSinglePinLink(SourcePin, TargetPin);
}

bool UPaper2DPlusAnimationMapSchema::CreateAutomaticConversionNodeAndConnections(
	UEdGraphPin* PinA, UEdGraphPin* PinB) const
{
	// MANDATORY override: the base returns false, and UEdGraphSchema::TryCreateConnection dispatches
	// here on CONNECT_RESPONSE_MAKE_WITH_CONVERSION_NODE (EdGraphSchema.cpp:504-506) — without this,
	// every move->move wire silently no-ops.
	UPaper2DPlusAnimationMapNode_Move* NodeA = PinA ? Cast<UPaper2DPlusAnimationMapNode_Move>(PinA->GetOwningNodeUnchecked()) : nullptr;
	UPaper2DPlusAnimationMapNode_Move* NodeB = PinB ? Cast<UPaper2DPlusAnimationMapNode_Move>(PinB->GetOwningNodeUnchecked()) : nullptr;
	if (!NodeA || !NodeB)
	{
		return false;
	}

	// Direction resolves the row's owner: the output side is the FROM move (PinA and PinB have
	// opposite directions here — TryCreateConnection's swap normalized same-direction gestures).
	UPaper2DPlusAnimationMapNode_Move* FromNode = (PinA->Direction == EGPD_Output) ? NodeA : NodeB;
	UPaper2DPlusAnimationMapNode_Move* ToNode = (PinA->Direction == EGPD_Output) ? NodeB : NodeA;

	// Stubs cannot own rows (R3). A stub has no output pin so a wire can never START there — this is
	// belt-and-braces against a future code path handing us a synthesized pin.
	if (FromNode->bIsStub || !FromNode->GetOutputPin())
	{
		return false;
	}

	UPaper2DPlusAnimationMap* AnimationMap = Cast<UPaper2DPlusAnimationMap>(FromNode->GetGraph());
	if (!AnimationMap || AnimationMap->bRebuildInProgress)
	{
		// Hooks never act during a panel reconcile (the bRebuildInProgress lock).
		return false;
	}

	// Schema methods run on the CDO and hold no state: the data write-through (append row) AND the
	// edge-node spawn both live in the panel-bound delegate on the GRAPH object (U4). NO asset data is
	// written from the schema. Unbound or refused => no connection is made (return false).
	return AnimationMap->OnWireCreateRequested.IsBound()
		&& AnimationMap->OnWireCreateRequested.Execute(FromNode->MoveName, ToNode->MoveName);
}

FLinearColor UPaper2DPlusAnimationMapSchema::GetPinTypeColor(const FEdGraphPinType& PinType) const
{
	// The anim-SM recipe (AnimationStateMachineSchema.cpp:460-467) minus the K2 fallback: White for the
	// one category this graph allocates ("Transition" — Paper2DPlusAnimationMapNode_Move/_Transition
	// AllocateDefaultPins), Super (black) for anything else. White is the multiplicative identity over
	// the Graph.StateNode.Pin.BackgroundHovered cue brush the full-body pins use.
	if (PinType.PinCategory == TEXT("Transition"))
	{
		return FLinearColor::White;
	}
	return Super::GetPinTypeColor(PinType);
}

FConnectionDrawingPolicy* UPaper2DPlusAnimationMapSchema::CreateConnectionDrawingPolicy(int32 InBackLayerID,
	int32 InFrontLayerID, float InZoomFactor, const FSlateRect& InClippingRect,
	FSlateWindowElementList& InDrawElements, UEdGraph* InGraphObj) const
{
	// Schema-scoped registration (NOT a global FGraphPanelPinConnectionFactory — the Live-Coding
	// stale-factory hazard, KTD). The caller (SGraphPanel::OnPaint) owns and deletes the policy.
	return new FAnimationMapConnectionDrawingPolicy(InBackLayerID, InFrontLayerID, InZoomFactor,
		InClippingRect, InDrawElements, InGraphObj);
}

#undef LOCTEXT_NAMESPACE
