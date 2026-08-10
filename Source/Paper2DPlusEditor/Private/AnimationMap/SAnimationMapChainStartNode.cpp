// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "AnimationMap/SAnimationMapChainStartNode.h"

#include "AnimationMap/Paper2DPlusAnimationMap.h"
#include "AnimationMap/Paper2DPlusAnimationMapNode_ChainStart.h"
#include "EdGraph/EdGraphPin.h"
#include "GenericPlatform/ICursor.h"
#include "SGraphPin.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SAnimationMapChainStartNode"

/////////////////////////////////////////////////////
// SAnimationMapChainStartPin

/**
 * Full-pill output pin for the Chain Start marker — the INTERACTIVE form of the move node's
 * SAnimationMapNodePin (file-local clone; that class is file-local to SAnimationMapMoveNode.cpp and
 * carries move-node stub logic a marker has no use for; file-unique name per the unity rule). The
 * whole visible border ring is the pin: FSlateNoResource-style Background brush normally,
 * Graph.StateNode.Pin.BackgroundHovered on hover (tinted White via the schema's GetPinTypeColor
 * override), modifier-free LMB -> SGraphPin::OnPinMouseDown starts the aiming wire drag.
 */
class SAnimationMapChainStartPin : public SGraphPin
{
public:
	SLATE_BEGIN_ARGS(SAnimationMapChainStartPin) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UEdGraphPin* InPin)
	{
		SetCursor(EMouseCursor::Default);
		bShowLabel = true;
		GraphPinObj = InPin;
		check(GraphPinObj);
		check(GraphPinObj->GetSchema());

		// SGraphPin::Construct deliberately bypassed for a direct SBorder::Construct — the same shape
		// as the move node's interactive pin (itself the SStateMachineOutputPin clone).
		SBorder::Construct(SBorder::FArguments()
			.BorderImage(this, &SAnimationMapChainStartPin::GetPinBorder)
			.BorderBackgroundColor(this, &SAnimationMapChainStartPin::GetPinColor)
			.OnMouseButtonDown(this, &SAnimationMapChainStartPin::OnFullBodyPinMouseDown)
			.Cursor(this, &SAnimationMapChainStartPin::GetPinCursor));
	}

protected:
	//~ Begin SGraphPin interface
	virtual TSharedRef<SWidget> GetDefaultValueWidget() override
	{
		return SNew(STextBlock);
	}
	//~ End SGraphPin interface

	const FSlateBrush* GetPinBorder() const
	{
		return IsHovered()
			? FAppStyle::Get().GetBrush(TEXT("Graph.StateNode.Pin.BackgroundHovered"))
			: FAppStyle::Get().GetBrush(TEXT("Graph.StateNode.Pin.Background"));
	}

	FReply OnFullBodyPinMouseDown(const FGeometry& SenderGeometry, const FPointerEvent& MouseEvent)
	{
		// Modifier guard — same rationale as the move node's pin: Alt-LMB's instant
		// Schema->BreakPinLinks and Ctrl/Shift-LMB's link drags are graph-side edits; the Alt-break IS
		// routed through the schema's ChainStart Break* overrides (so it would be safe), but keeping
		// the two full-body pins behaviorally identical keeps the surface predictable. Modifier clicks
		// fall through to the panel (Ctrl-click select-toggle keeps working).
		if (MouseEvent.IsAltDown() || MouseEvent.IsControlDown() || MouseEvent.IsShiftDown())
		{
			return FReply::Unhandled();
		}
		return OnPinMouseDown(SenderGeometry, MouseEvent);
	}
};

/////////////////////////////////////////////////////
// SAnimationMapChainStartNode

void SAnimationMapChainStartNode::Construct(const FArguments& InArgs, UPaper2DPlusAnimationMapNode_ChainStart* InNode)
{
	GraphNode = InNode;
	SetCursor(EMouseCursor::CardinalCross);
	UpdateGraphNode();
}

void SAnimationMapChainStartNode::UpdateGraphNode()
{
	// Scaled-down clone of SAnimationMapMoveNode::UpdateGraphNode (itself the anim-SM whole-node-body
	// pin shape): pin area under a centered content plate. Divergences: no thumbnail/chips/title
	// editor/error badge (the transition pill precedent shows ErrorReporting is optional), a tighter
	// 6px pin ring, and a single small label — the marker must read as an accessory, not a move.
	InputPins.Empty();
	OutputPins.Empty();
	RightNodeBox.Reset();
	LeftNodeBox.Reset();

	// SelfHitTestInvisible so the node never beats its full-body pin to the hit test (move-node rule).
	SetVisibility(EVisibility::SelfHitTestInvisible);

	// NOTE: deliberately no ContentScale.Bind — 5.8 deprecates SNode::ContentScale as unused (C4996
	// under -WarningsAsErrors), and the only pre-5.8 effect is the node-spawn scale animation, which
	// a tiny fixed-size marker pill does not need.
	this->GetOrAddSlot(ENodeZone::Center)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::Get().GetBrush("Graph.StateNode.Body"))
			.Padding(0.0f)
			.BorderBackgroundColor(FLinearColor(0.10f, 0.08f, 0.02f))
			[
				SNew(SOverlay)

				// PIN AREA — fills the whole pill; the ring around the content plate is the wire-drag
				// surface (AddPin slots the output pin here).
				+ SOverlay::Slot()
				.HAlign(HAlign_Fill)
				.VAlign(VAlign_Fill)
				[
					SAssignNew(RightNodeBox, SVerticalBox)
					+ SVerticalBox::Slot()
					.HAlign(HAlign_Fill)
					.VAlign(VAlign_Fill)
					.FillHeight(1.0f)
					[
						SAssignNew(PinOverlay, SOverlay)
					]
				]

				// CONTENT PLATE — the 6px surround is the visible pin ring. Hit-testable and handles
				// nothing itself, so grabbing the label bubbles to the panel = select + move.
				+ SOverlay::Slot()
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				.Padding(6.0f)
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::Get().GetBrush("Graph.StateNode.ColorSpill"))
					.BorderBackgroundColor(FLinearColor(0.22f, 0.17f, 0.04f))
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					.Padding(FMargin(6.0f, 2.0f))
					[
						SNew(STextBlock)
						.Text(LOCTEXT("ChainStartPillLabel", "▶ Chain Start"))
						.ToolTipText(LOCTEXT("ChainStartPillTip",
							"Marks this group's combo-chain opener. Drag a wire from the marker's border onto a move to set it as the chain start; break the wire to clear it. An unwired marker is a floating note — aim or delete it."))
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
						.ColorAndOpacity(FSlateColor(FLinearColor(0.95f, 0.78f, 0.20f)))
					]
				]
			]
		];

	CreatePinWidgets();
}

void SAnimationMapChainStartNode::CreatePinWidgets()
{
	// One output pin, full-pill interactive form. Null-safe: a half-built node just shows no ring.
	UPaper2DPlusAnimationMapNode_ChainStart* MarkerNode =
		CastChecked<UPaper2DPlusAnimationMapNode_ChainStart>(GraphNode);
	if (UEdGraphPin* OutputPin = MarkerNode->GetOutputPin())
	{
		if (!OutputPin->bHidden)
		{
			AddPin(SNew(SAnimationMapChainStartPin, OutputPin));
		}
	}
}

void SAnimationMapChainStartNode::AddPin(const TSharedRef<SGraphPin>& PinToAdd)
{
	// Move-node clone: every pin fills the whole-pill PinOverlay.
	PinToAdd->SetOwner(SharedThis(this));
	PinOverlay->AddSlot()
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Fill)
		[
			PinToAdd
		];

	const UEdGraphPin* PinObj = PinToAdd->GetPinObj();
	if (PinObj && PinObj->Direction == EGPD_Input)
	{
		InputPins.Add(PinToAdd);
	}
	else
	{
		OutputPins.Add(PinToAdd);
	}
}

void SAnimationMapChainStartNode::MoveTo(const FAnimationMapChainStartNodeVector& NewPosition, FNodeSet& NodeFilter, bool bMarkDirty)
{
	// Super ALWAYS (live drag visuals; the marker is non-transactional by the spawn-funnel invariant,
	// so the base's GraphNode->Modify(bMarkDirty) records nothing). Persist ONLY on bMarkDirty=true —
	// the exact SAnimationMapMoveNode::MoveTo contract (one engine-transacted call per drag/nudge;
	// false = per-mouse-move preview + the finalize restore pass).
	SGraphNode::MoveTo(NewPosition, NodeFilter, bMarkDirty);
	if (!bMarkDirty)
	{
		return;
	}

	UPaper2DPlusAnimationMapNode_ChainStart* MarkerNode = Cast<UPaper2DPlusAnimationMapNode_ChainStart>(GraphNode);
	if (!MarkerNode)
	{
		return;
	}
	UPaper2DPlusAnimationMap* AnimationMap = Cast<UPaper2DPlusAnimationMap>(MarkerNode->GetGraph());
	if (!AnimationMap || AnimationMap->bRebuildInProgress)
	{
		return; // reconcile repositions by direct NodePosX/Y, never MoveTo — a mid-rebuild fire is foreign
	}
	AnimationMap->OnChainStartMarkerMoveCommitted.ExecuteIfBound(MarkerNode);
}

#undef LOCTEXT_NAMESPACE
