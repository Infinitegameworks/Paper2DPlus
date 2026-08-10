// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "AnimationMap/SAnimationMapChainEndNode.h"

#include "AnimationMap/Paper2DPlusAnimationMap.h"
#include "AnimationMap/Paper2DPlusAnimationMapNode_ChainEnd.h"
#include "EdGraph/EdGraphPin.h"
#include "GenericPlatform/ICursor.h"
#include "SGraphPin.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SAnimationMapChainEndNode"

/////////////////////////////////////////////////////
// SAnimationMapChainEndPin

/**
 * Full-pill input pin for the Chain End marker — SAnimationMapChainStartPin's mirror (file-local
 * clone; that class is file-local to SAnimationMapChainStartNode.cpp and this one is file-local
 * here; file-unique name per the unity rule). The whole visible border ring is the pin:
 * FSlateNoResource-style Background brush normally, Graph.StateNode.Pin.BackgroundHovered on hover
 * (tinted White via the schema's GetPinTypeColor override). Modifier-free LMB still routes to
 * SGraphPin::OnPinMouseDown — a drag OUT of the input pin is refused at the schema
 * (CanCreateConnection's "Drag from a move onto the Chain End marker"), keeping the two marker
 * pills behaviorally identical rather than special-casing the widget.
 */
class SAnimationMapChainEndPin : public SGraphPin
{
public:
	SLATE_BEGIN_ARGS(SAnimationMapChainEndPin) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UEdGraphPin* InPin)
	{
		SetCursor(EMouseCursor::Default);
		bShowLabel = true;
		GraphPinObj = InPin;
		check(GraphPinObj);
		check(GraphPinObj->GetSchema());

		// SGraphPin::Construct deliberately bypassed for a direct SBorder::Construct — the same shape
		// as the start marker's interactive pin (itself the move node's SStateMachineOutputPin clone).
		SBorder::Construct(SBorder::FArguments()
			.BorderImage(this, &SAnimationMapChainEndPin::GetPinBorder)
			.BorderBackgroundColor(this, &SAnimationMapChainEndPin::GetPinColor)
			.OnMouseButtonDown(this, &SAnimationMapChainEndPin::OnFullBodyPinMouseDown)
			.Cursor(this, &SAnimationMapChainEndPin::GetPinCursor));
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
		// Modifier guard — same rationale as the start marker's pin: Alt-LMB's instant
		// Schema->BreakPinLinks and Ctrl/Shift-LMB's link drags are graph-side edits; the Alt-break IS
		// routed through the schema's ChainEnd Break* overrides (so it would be safe), but keeping
		// the full-body pins behaviorally identical keeps the surface predictable. Modifier clicks
		// fall through to the panel (Ctrl-click select-toggle keeps working).
		if (MouseEvent.IsAltDown() || MouseEvent.IsControlDown() || MouseEvent.IsShiftDown())
		{
			return FReply::Unhandled();
		}
		return OnPinMouseDown(SenderGeometry, MouseEvent);
	}
};

/////////////////////////////////////////////////////
// SAnimationMapChainEndNode

void SAnimationMapChainEndNode::Construct(const FArguments& InArgs, UPaper2DPlusAnimationMapNode_ChainEnd* InNode)
{
	GraphNode = InNode;
	SetCursor(EMouseCursor::CardinalCross);
	UpdateGraphNode();
}

void SAnimationMapChainEndNode::UpdateGraphNode()
{
	// SAnimationMapChainStartNode::UpdateGraphNode MIRRORED (itself the scaled-down clone of the
	// anim-SM whole-node-body pin shape): pin area under a centered content plate. Divergences from
	// the start pill: the slate-blue END tint family and the trailing ⏹ label — the marker must read
	// as the start marker's opposite bookend at a glance.
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
			.BorderBackgroundColor(FLinearColor(0.03f, 0.05f, 0.12f))
			[
				SNew(SOverlay)

				// PIN AREA — fills the whole pill; the ring around the content plate is the wire-drop
				// surface (AddPin slots the input pin here).
				+ SOverlay::Slot()
				.HAlign(HAlign_Fill)
				.VAlign(VAlign_Fill)
				[
					SAssignNew(LeftNodeBox, SVerticalBox)
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
					.BorderBackgroundColor(FLinearColor(0.07f, 0.10f, 0.26f))
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					.Padding(FMargin(6.0f, 2.0f))
					[
						SNew(STextBlock)
						.Text(LOCTEXT("ChainEndPillLabel", "Chain End ⏹"))
						.ToolTipText(LOCTEXT("ChainEndPillTip",
							"Marks this group's combo main-line END — combo indexing counts up to (and including) the wired move; trailing recovery animations after it are excluded. Drag a wire from a move onto the marker's border to set that move as the chain end; break the wire to clear it. An unwired marker is a floating note — aim or delete it."))
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
						.ColorAndOpacity(FSlateColor(FLinearColor(0.45f, 0.55f, 0.95f)))
					]
				]
			]
		];

	CreatePinWidgets();
}

void SAnimationMapChainEndNode::CreatePinWidgets()
{
	// One input pin, full-pill interactive form. Null-safe: a half-built node just shows no ring.
	UPaper2DPlusAnimationMapNode_ChainEnd* MarkerNode =
		CastChecked<UPaper2DPlusAnimationMapNode_ChainEnd>(GraphNode);
	if (UEdGraphPin* InputPin = MarkerNode->GetInputPin())
	{
		if (!InputPin->bHidden)
		{
			AddPin(SNew(SAnimationMapChainEndPin, InputPin));
		}
	}
}

void SAnimationMapChainEndNode::AddPin(const TSharedRef<SGraphPin>& PinToAdd)
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

void SAnimationMapChainEndNode::MoveTo(const FAnimationMapChainEndNodeVector& NewPosition, FNodeSet& NodeFilter, bool bMarkDirty)
{
	// Super ALWAYS (live drag visuals; the marker is non-transactional by the spawn-funnel invariant,
	// so the base's GraphNode->Modify(bMarkDirty) records nothing). Persist ONLY on bMarkDirty=true —
	// the exact SAnimationMapChainStartNode::MoveTo contract (one engine-transacted call per
	// drag/nudge; false = per-mouse-move preview + the finalize restore pass).
	SGraphNode::MoveTo(NewPosition, NodeFilter, bMarkDirty);
	if (!bMarkDirty)
	{
		return;
	}

	UPaper2DPlusAnimationMapNode_ChainEnd* MarkerNode = Cast<UPaper2DPlusAnimationMapNode_ChainEnd>(GraphNode);
	if (!MarkerNode)
	{
		return;
	}
	UPaper2DPlusAnimationMap* AnimationMap = Cast<UPaper2DPlusAnimationMap>(MarkerNode->GetGraph());
	if (!AnimationMap || AnimationMap->bRebuildInProgress)
	{
		return; // reconcile repositions by direct NodePosX/Y, never MoveTo — a mid-rebuild fire is foreign
	}
	AnimationMap->OnChainEndMarkerMoveCommitted.ExecuteIfBound(MarkerNode);
}

#undef LOCTEXT_NAMESPACE
