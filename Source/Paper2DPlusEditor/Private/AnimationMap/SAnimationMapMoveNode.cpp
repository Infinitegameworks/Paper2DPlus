// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "AnimationMap/SAnimationMapMoveNode.h"

#include "AnimationMap/Paper2DPlusAnimationMap.h"
#include "AnimationMap/Paper2DPlusAnimationMapNode_Move.h"
#include "AnimationTagChipUtils.h" // TASK-108 U6: the shared provenance-styled tag chips (R6)
#include "EdGraph/EdGraphPin.h"
#include "EditorCanvasUtils.h"
#include "GenericPlatform/ICursor.h"
#include "PaperFlipbook.h"
#include "SGraphPanel.h"
#include "SGraphPin.h"
#include "Styling/AppStyle.h"
#include "Styling/StyleColors.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Notifications/SErrorText.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SAnimationMapMoveNode"

/////////////////////////////////////////////////////
// SAnimationMapNodePin

/**
 * Full-node-body pin (U6 polish): ONE class cloning the anim state machine's
 * SStateMachineOutputPin + SStateMachineInputPin pair (SStateMachineOutputPin.cpp:11-35 /
 * SStateMachineInputPin.cpp:7-22 — the same widget PaperZD inlines as SPaperZDStateNodeOutputPin,
 * SPaperZDStateGraphNode_State.cpp:19-65). The mode is decided from the pin itself:
 *
 *  - OUTPUT pin (real moves only): the interactive border ring. Overlay-filled across the whole node,
 *    FSlateNoResource normally / Graph.StateNode.Pin.BackgroundHovered on hover (both brushes
 *    VERIFIED 5.0-5.7 — StarshipStyle.cpp 5.7:4037-4038, 5.0:3597-3598), modifier-free LMB ->
 *    SGraphPin::OnPinMouseDown starts the wire drag, and wire drops land here (the schema's
 *    same-direction swap re-targets out->out gestures to the input — incl. the R14 self-loop when the
 *    drop is back on the SAME node).
 *  - INPUT pin on a REAL move: the engine's invisible anchor (HitTestInvisible, brush-less) — it
 *    exists only so the pin object has live widget geometry; all interaction happens on the output.
 *  - INPUT pin on a STUB (no output pin exists, U3): takes the INTERACTIVE form instead, so stubs
 *    still hover-highlight and still accept incoming drops; a drag FROM a stub starts at its input
 *    pin, which the conversion path resolves to a row owned by the OTHER move (stubs own no rows —
 *    CreateAutomaticConversionNodeAndConnections guards the From side regardless).
 *
 * Editor-module Slate only (no UCLASS). SGraphPin::Construct is deliberately bypassed for a direct
 * SBorder::Construct, exactly like both clone sources. The hover cue is TINTED by
 * SGraphPin::GetPinColor -> Schema->GetPinTypeColor — the schema overrides "Transition" -> White
 * (the base UEdGraphSchema default is BLACK, which would render the highlight invisible).
 */
class SAnimationMapNodePin : public SGraphPin
{
public:
	SLATE_BEGIN_ARGS(SAnimationMapNodePin) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UEdGraphPin* InPin)
	{
		SetCursor(EMouseCursor::Default);

		bShowLabel = true;

		GraphPinObj = InPin;
		check(GraphPinObj);
		check(GraphPinObj->GetSchema());

		const UPaper2DPlusAnimationMapNode_Move* MoveNode =
			Cast<UPaper2DPlusAnimationMapNode_Move>(InPin->GetOwningNodeUnchecked());
		const bool bStubNode = MoveNode && MoveNode->bIsStub;

		if (InPin->Direction == EGPD_Input && !bStubNode)
		{
			// SStateMachineInputPin clone: a brush-less, hit-test-invisible geometry anchor — the
			// OUTPUT pin underneath owns the whole interactive surface on real moves.
			bShowLabel = false;
			SetVisibility(EVisibility::HitTestInvisible);
			SBorder::Construct(SBorder::FArguments()
				.BorderImage(nullptr));
			return;
		}

		// SStateMachineOutputPin clone: hover-cue border + wire-drag mouse-down + pin grab cursor.
		SBorder::Construct(SBorder::FArguments()
			.BorderImage(this, &SAnimationMapNodePin::GetPinBorder)
			.BorderBackgroundColor(this, &SAnimationMapNodePin::GetPinColor)
			.OnMouseButtonDown(this, &SAnimationMapNodePin::OnFullBodyPinMouseDown)
			.Cursor(this, &SAnimationMapNodePin::GetPinCursor));
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
		// Modifier guard (deliberate divergence from the clones): SGraphPin::OnPinMouseDown's Alt-LMB
		// = instant Schema->BreakPinLinks and Ctrl/Shift-LMB = move/copy-links drags
		// (SGraphPin.cpp:444-453) — GRAPH-side edits that bypass U5's write-through and silently
		// desync the projection from the flat rows. The full-body pin turns the entire border into
		// pin surface, so instead of enlarging that hole, modifier clicks fall through UNHANDLED to
		// SGraphPanel (Ctrl-click node-select-toggle keeps working). Transitions are deleted via
		// their PILL — the confirmed write-through path. Modifier-free LMB is the stock wire-drag.
		if (MouseEvent.IsAltDown() || MouseEvent.IsControlDown() || MouseEvent.IsShiftDown())
		{
			return FReply::Unhandled();
		}
		return OnPinMouseDown(SenderGeometry, MouseEvent);
	}
};

/////////////////////////////////////////////////////
// SAnimationMapMoveNode

void SAnimationMapMoveNode::Construct(const FArguments& InArgs, UPaper2DPlusAnimationMapNode_Move* InNode)
{
	// The anim-state-machine node shape (SGraphNodeAnimState::Construct, SGraphNodeAnimState.cpp:58-65):
	// UpdateGraphNode below builds the whole-node-body pin clone.
	GraphNode = InNode;
	SetCursor(EMouseCursor::CardinalCross);
	UpdateGraphNode();
}

void SAnimationMapMoveNode::UpdateGraphNode()
{
	// Clone of SGraphNodeAnimState::UpdateGraphNode (SGraphNodeAnimState.cpp:202-353) — see the header
	// class comment for the resulting hit-test routing. Divergences from the engine original:
	//  - no selection-overlay border (selection shows via the base GetShadowBrush halo, the PaperZD
	//    shape) and no state-machine gear icon (the thumbnail is the node's image);
	//  - the content plate carries the U6 thumbnail well under the title;
	//  - body/spill tints fold in the U6 stub warning styling (the engine binds debug-weight colors).
	InputPins.Empty();
	OutputPins.Empty();
	ThumbnailHost.Reset();
	TagChipsHost.Reset();

	// Reset variables that are going to be exposed, in case we are refreshing an already setup node.
	RightNodeBox.Reset();
	LeftNodeBox.Reset();

	TSharedPtr<SErrorText> ErrorText;
	TSharedPtr<SNodeTitle> NodeTitle = SNew(SNodeTitle, GraphNode);

	// SelfHitTestInvisible (engine :233-234) so the node never beats its full-body pins to the hit
	// test — "we dont end up with a 1-unit border where the node is selected rather than the pin".
	SetVisibility(EVisibility::SelfHitTestInvisible);

	this->ContentScale.Bind(this, &SGraphNode::GetContentScale);
	this->GetOrAddSlot(ENodeZone::Center)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::Get().GetBrush("Graph.StateNode.Body"))
			.Padding(0.0f)
			.BorderBackgroundColor(this, &SAnimationMapMoveNode::GetBodyBorderColor)
			// TASK-108 U6 (R8): the Map tag filter DIMS non-matching nodes (never hides — wires to a
			// dimmed node stay visible). Read per paint from the panel-stamped bDimmedByFilter, so a
			// filter change needs only a re-stamp + repaint, no widget rebuild.
			.ColorAndOpacity_Lambda([this]()
			{
				const UPaper2DPlusAnimationMapNode_Move* DimMoveNode = Cast<UPaper2DPlusAnimationMapNode_Move>(GraphNode);
				return (DimMoveNode && DimMoveNode->bDimmedByFilter)
					? FLinearColor(1.0f, 1.0f, 1.0f, 0.28f)
					: FLinearColor::White;
			})
			[
				SNew(SOverlay)

				// PIN AREA — fills the WHOLE node; the full-body pins slotted here by AddPin are the
				// border ring's interactive surface (wire-drag start, drop target, hover cue).
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

				// CONTENT PLATE — centered ON TOP of the pin area; its 12px surround is the visible
				// pin ring (the engine's padding, :272). The plate is hit-testable and handles no
				// mouse events itself, so grabbing the title/thumbnail bubbles to the panel = node
				// select + move (the anim-SM split between "move me" and "pull a wire").
				+ SOverlay::Slot()
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				.Padding(12.0f)
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::Get().GetBrush("Graph.StateNode.ColorSpill"))
					.BorderBackgroundColor(this, &SAnimationMapMoveNode::GetSpillColor)
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					.Padding(FMargin(6.0f, 4.0f))
					[
						SNew(SVerticalBox)

						// Title row: error badge + the node title (stub "(missing)" suffix included —
						// GetNodeTitle stays the single source). Read-only: the move node never sets
						// bCanRenameNode, so IsNameReadOnly keeps edit mode off (renames happen in the
						// browser and propagate, U2).
						+ SVerticalBox::Slot()
						.AutoHeight()
						.HAlign(HAlign_Center)
						[
							SNew(SHorizontalBox)

							// Chain-start glyph — reinforcement for the wired Chain Start marker node (the
							// authoring surface); this is deliberately a tiny decorative ▶, not a control.
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							.Padding(FMargin(0.0f, 0.0f, 3.0f, 0.0f))
							[
								SNew(STextBlock)
								.Text(LOCTEXT("ChainStartBadge", "▶"))
								.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
								.ColorAndOpacity(FSlateColor(FLinearColor(0.95f, 0.78f, 0.20f)))
								// HitTestInvisible (NOT Visible): a purely-decorative marker must NOT capture the
								// right-click, or it shadows the full-body pin underneath so the node's context
								// menu loses its pin actions (break-link etc.) — observed on the flagged opener
								// while non-flagged nodes kept them. Drawn, never interactive.
								.Visibility_Lambda([this]()
								{
									const UPaper2DPlusAnimationMapNode_Move* MoveNode =
										Cast<UPaper2DPlusAnimationMapNode_Move>(GraphNode);
									return MoveNode && MoveNode->bIsChainStart
										? EVisibility::HitTestInvisible
										: EVisibility::Collapsed;
								})
							]

							// Chain-end glyph — the ▶ glyph's MIRROR (chain-end rework): reinforcement for
							// the wired Chain End marker node (the authoring surface); a tiny decorative ⏹
							// in the end marker's slate-blue, not a control. Same HitTestInvisible rule.
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							.Padding(FMargin(0.0f, 0.0f, 3.0f, 0.0f))
							[
								SNew(STextBlock)
								.Text(LOCTEXT("ChainEndBadge", "⏹"))
								.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
								.ColorAndOpacity(FSlateColor(FLinearColor(0.45f, 0.55f, 0.95f)))
								.Visibility_Lambda([this]()
								{
									const UPaper2DPlusAnimationMapNode_Move* MoveNode =
										Cast<UPaper2DPlusAnimationMapNode_Move>(GraphNode);
									return MoveNode && MoveNode->bIsChainEnd
										? EVisibility::HitTestInvisible
										: EVisibility::Collapsed;
								})
							]

							// Auto-derived combo main-line step (#N) — the exact index the
							// Get Combo Chain Flipbook at Index BP node returns for this move, so the graph and
							// gameplay can never disagree about the numbering. Hidden for the chain
							// start itself (index 0 — the ▶ glyph already marks the opener),
							// side-branch members, and moves on no unique chain. Same
							// HitTestInvisible rule as the ▶ glyph.
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							.Padding(FMargin(0.0f, 0.0f, 3.0f, 0.0f))
							[
								SNew(STextBlock)
								.Text_Lambda([this]()
								{
									const UPaper2DPlusAnimationMapNode_Move* MoveNode =
										Cast<UPaper2DPlusAnimationMapNode_Move>(GraphNode);
									return FText::Format(
										LOCTEXT("ComboSpineBadgeFmt", "#{0}"),
										FText::AsNumber(MoveNode ? MoveNode->ComboSpineIndex : 0));
								})
								.ToolTipText_Lambda([this]()
								{
									const UPaper2DPlusAnimationMapNode_Move* MoveNode =
										Cast<UPaper2DPlusAnimationMapNode_Move>(GraphNode);
									return FText::Format(
										LOCTEXT("ComboSpineBadgeTip",
											"Combo chain index {0} of {1} — Get Combo Chain Flipbook at Index returns this move for this index."),
										FText::AsNumber(MoveNode ? MoveNode->ComboSpineIndex : 0),
										FText::AsNumber(MoveNode ? MoveNode->ComboSpineLength : 0));
								})
								.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
								.ColorAndOpacity(FSlateColor(FLinearColor(0.55f, 0.80f, 0.95f)))
								.Visibility_Lambda([this]()
								{
									const UPaper2DPlusAnimationMapNode_Move* MoveNode =
										Cast<UPaper2DPlusAnimationMapNode_Move>(GraphNode);
									return MoveNode && MoveNode->ComboSpineIndex > 0
										? EVisibility::HitTestInvisible
										: EVisibility::Collapsed;
								})
							]

							+ SHorizontalBox::Slot()
							.AutoWidth()
							[
								// POPUP ERROR MESSAGE (engine clone; ErrorMsg is empty today, but the
								// base RefreshErrorInfo expects a live ErrorReporting widget)
								SAssignNew(ErrorText, SErrorText)
								.BackgroundColor(this, &SAnimationMapMoveNode::GetErrorColor)
								.ToolTipText(this, &SAnimationMapMoveNode::GetErrorMsgToolTip)
							]

							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							.Padding(FMargin(2.0f, 0.0f))
							[
								SNew(SVerticalBox)
								+ SVerticalBox::Slot()
								.AutoHeight()
								[
									SAssignNew(InlineEditableText, SInlineEditableTextBlock)
									.Style(FAppStyle::Get(), "Graph.StateNode.NodeTitleInlineEditableText")
									.Text(NodeTitle.Get(), &SNodeTitle::GetHeadTitle)
									.OnVerifyTextChanged(this, &SAnimationMapMoveNode::OnVerifyNameTextChanged)
									.OnTextCommitted(this, &SAnimationMapMoveNode::OnNameTextCommited)
									.IsReadOnly(this, &SAnimationMapMoveNode::IsNameReadOnly)
									.IsSelected(this, &SAnimationMapMoveNode::IsSelectedExclusively)
								]
								+ SVerticalBox::Slot()
								.AutoHeight()
								[
									NodeTitle.ToSharedRef()
								]
							]
						]

						// The U6 64x64 flipbook-thumbnail well.
						+ SVerticalBox::Slot()
						.AutoHeight()
						.HAlign(HAlign_Center)
						.Padding(FMargin(0.0f, 3.0f, 0.0f, 0.0f))
						[
							SAssignNew(ThumbnailHost, SBox)
							[
								MakeThumbnailWell()
							]
						]

						// TASK-108 U6 (R6): the animation-tag chips (own = solid, chain-inherited =
						// ghosted, group-implied = outlined; "+N" overflow) — the SAME shared visuals
						// as the browser grid cards, read from the node's panel-stamped denormalized
						// containers at BUILD time (a tag edit re-stamps via the AnimationTagChanges
						// diff, then replaces only this host's child). Read-only display on the card;
						// authoring is available through the context menu and selection cohort card.
						+ SVerticalBox::Slot()
						.AutoHeight()
						.HAlign(HAlign_Center)
						.Padding(FMargin(0.0f, 3.0f, 0.0f, 0.0f))
						[
							SAssignNew(TagChipsHost, SBox)
							[
								MakeTagChipsRow()
							]
						]
					]
				]
			]
		];

	ErrorReporting = ErrorText;
	ErrorReporting->SetError(ErrorMsg);
	CreatePinWidgets();
}

void SAnimationMapMoveNode::RefreshThumbnail()
{
	if (ThumbnailHost.IsValid())
	{
		ThumbnailHost->SetContent(MakeThumbnailWell());
	}
}

void SAnimationMapMoveNode::RefreshTagChips()
{
	if (TagChipsHost.IsValid())
	{
		TagChipsHost->SetContent(MakeTagChipsRow());
	}
}

void SAnimationMapMoveNode::CreatePinWidgets()
{
	// Clone of SGraphNodeAnimState::CreatePinWidgets (:355-371) — output FIRST, input SECOND (the
	// input sits topmost in the overlay: inert on real moves [HitTestInvisible] and THE interactive
	// surface on stubs). Null-safe lookups: a stub allocates no output pin at all (U3).
	UPaper2DPlusAnimationMapNode_Move* MoveNode = CastChecked<UPaper2DPlusAnimationMapNode_Move>(GraphNode);

	if (UEdGraphPin* OutputPin = MoveNode->GetOutputPin())
	{
		if (!OutputPin->bHidden)
		{
			AddPin(SNew(SAnimationMapNodePin, OutputPin));
		}
	}
	if (UEdGraphPin* InputPin = MoveNode->GetInputPin())
	{
		if (!InputPin->bHidden)
		{
			AddPin(SNew(SAnimationMapNodePin, InputPin));
		}
	}
}

void SAnimationMapMoveNode::AddPin(const TSharedRef<SGraphPin>& PinToAdd)
{
	// Clone of SGraphNodeAnimState::AddPin (:373-394): every pin fills the whole-node PinOverlay.
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

void SAnimationMapMoveNode::OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	// Clone of SGraphNodeAnimState::OnMouseEnter (:162-180): hovering the node adds the output pin's
	// links (the transition pills' hidden input pins) to the panel hover set, so every OUTGOING
	// wire + pill lights while the node is hovered (DetermineWiringStyle's HoveredPins check — the
	// pill's own hover already does the single-edge version, U6, untouched). Null-safe where the
	// engine check()s: stubs have no output pin, and a panel-less widget just skips.
	if (const UPaper2DPlusAnimationMapNode_Move* MoveNode = Cast<UPaper2DPlusAnimationMapNode_Move>(GraphNode))
	{
		if (const UEdGraphPin* OutputPin = MoveNode->GetOutputPin())
		{
			if (TSharedPtr<SGraphPanel> OwnerPanel = GetOwnerPanel())
			{
				for (UEdGraphPin* LinkedPin : OutputPin->LinkedTo)
				{
					OwnerPanel->AddPinToHoverSet(LinkedPin);
				}
			}
		}
	}

	SGraphNode::OnMouseEnter(MyGeometry, MouseEvent);
}

void SAnimationMapMoveNode::OnMouseLeave(const FPointerEvent& MouseEvent)
{
	// Mirror of OnMouseEnter (clone of SGraphNodeAnimState::OnMouseLeave, :182-200).
	if (const UPaper2DPlusAnimationMapNode_Move* MoveNode = Cast<UPaper2DPlusAnimationMapNode_Move>(GraphNode))
	{
		if (const UEdGraphPin* OutputPin = MoveNode->GetOutputPin())
		{
			if (TSharedPtr<SGraphPanel> OwnerPanel = GetOwnerPanel())
			{
				for (UEdGraphPin* LinkedPin : OutputPin->LinkedTo)
				{
					OwnerPanel->RemovePinFromHoverSet(LinkedPin);
				}
			}
		}
	}

	SGraphNode::OnMouseLeave(MouseEvent);
}

FSlateColor SAnimationMapMoveNode::GetBodyBorderColor() const
{
	// Anim-SM inactive body (SGraphNodeAnimState.cpp:121); stubs swap in a warm warning cast — the
	// old GetNodeBodyTintColor stub signal (visible at any zoom), re-expressed as the body tint now
	// that this widget owns the body brush. The tag filter's dim (U6, R8) fades the body brush too —
	// the content-side ColorAndOpacity lambda can't reach it (SBorder tints content, not its brush).
	const UPaper2DPlusAnimationMapNode_Move* MoveNode = Cast<UPaper2DPlusAnimationMapNode_Move>(GraphNode);
	FLinearColor Body = FLinearColor(0.08f, 0.08f, 0.08f);
	if (MoveNode && MoveNode->bIsStub)
	{
		Body = FLinearColor(0.30f, 0.20f, 0.05f);
	}
	if (MoveNode && MoveNode->bDimmedByFilter)
	{
		Body.A *= 0.28f;
	}
	return Body;
}

FSlateColor SAnimationMapMoveNode::GetSpillColor() const
{
	// The node's GetNodeTitleColor stays the single source of the title-plate tint (stub = warning
	// orange, real = neutral dark — the exact values the old standard title bar showed). Dim with the
	// filter (U6, R8) like the body.
	FLinearColor Spill = GraphNode ? GraphNode->GetNodeTitleColor() : FLinearColor(0.10f, 0.10f, 0.10f);
	const UPaper2DPlusAnimationMapNode_Move* MoveNode = Cast<UPaper2DPlusAnimationMapNode_Move>(GraphNode);
	if (MoveNode && MoveNode->bDimmedByFilter)
	{
		Spill.A *= 0.28f;
	}
	return Spill;
}

TSharedRef<SWidget> SAnimationMapMoveNode::MakeThumbnailWell()
{
	// U6 body (R1): the 64x64 SFlipbookThumbnail well (the browser phase-slot size), rebuilt only
	// inside ThumbnailHost when ResolvedFlipbook changes. bIsStub is safe to read at build time: a
	// stub<->real flip destroys+recreates the node because its pin set changes.
	UPaper2DPlusAnimationMapNode_Move* MoveNode = Cast<UPaper2DPlusAnimationMapNode_Move>(GraphNode);
	UPaperFlipbook* Flipbook = MoveNode ? MoveNode->ResolvedFlipbook.Get() : nullptr;
	const bool bIsStub = MoveNode && MoveNode->bIsStub;

	// Stubs: a 2px warning frame around the thumbnail well (the thumbnail itself renders its natural
	// "No FB" status — ResolvedFlipbook is null for stubs by the stamping contract). Real moves get a
	// quiet dark inset frame so the checkerboard reads as a deliberate well.
	const FLinearColor FrameColor = bIsStub
		? FStyleColors::Warning.GetSpecifiedColor()
		: FLinearColor(0.02f, 0.02f, 0.02f);

	return SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("WhiteBrush"))
		.BorderBackgroundColor(FrameColor)
		.Padding(2.0f)
		[
			SNew(SBox)
			.WidthOverride(64.0f)
			.HeightOverride(64.0f)
			[
				SNew(SFlipbookThumbnail)
				.Flipbook(Flipbook)
			]
		];
}

TSharedRef<SWidget> SAnimationMapMoveNode::MakeTagChipsRow()
{
	// U6 (R6): flatten the node's denormalized provenance containers into ordered chip items (own >
	// chain > group when a tag arrives from several sources) and render the shared chips row. The
	// AnimationTagChanges diff replaces only TagChipsHost's child. Empty tags -> a null widget.
	const UPaper2DPlusAnimationMapNode_Move* MoveNode = Cast<UPaper2DPlusAnimationMapNode_Move>(GraphNode);
	if (!MoveNode)
	{
		return SNullWidget::NullWidget;
	}
	const TArray<Paper2DPlusAnimationTagChips::FAnimationTagChipItem> Items =
		Paper2DPlusAnimationTagChips::BuildChipItems(MoveNode->OwnAnimationTags,
			MoveNode->ChainInheritedAnimationTags, MoveNode->GroupImpliedAnimationTags);
	return Paper2DPlusAnimationTagChips::MakeChipsRow(Items, /*MaxVisible=*/3);
}

void SAnimationMapMoveNode::MoveTo(const FAnimationMapMoveNodeVector& NewPosition, FNodeSet& NodeFilter, bool bMarkDirty)
{
	// Super ALWAYS: the live drag preview is NodePosX/Y-driven, and the base's
	// GraphNode->Modify(bMarkDirty) records nothing — the node was spawned non-transactional through
	// UPaper2DPlusAnimationMap::SpawnNodeUntransactional (the asset stays the only transacted object).
	SGraphNode::MoveTo(NewPosition, NodeFilter, bMarkDirty);

	// Persist ONLY when bMarkDirty is true (see the header contract): false = the engine's per-mouse-
	// move calls (no transaction open — writing here would be a bare untransacted Modify per mouse-move,
	// the rule-9 refresh storm) AND FinalizeNodeMovements' restore pass (persisting it would clobber the
	// gesture's final position with the original).
	if (!bMarkDirty)
	{
		return;
	}

	// Seam: widget -> node -> owning UPaper2DPlusAnimationMap -> panel-bound OnNodeMoveCommitted (the
	// same graph-owned delegate seam the schema's wire-create hook uses). Suppressed during rebuilds —
	// the reconcile reapplies positions by DIRECT NodePosX/Y assignment, never MoveTo, so this firing
	// mid-rebuild would mean a foreign caller; fail safe and skip.
	UPaper2DPlusAnimationMapNode_Move* MoveNode = Cast<UPaper2DPlusAnimationMapNode_Move>(GraphNode);
	if (!MoveNode)
	{
		return;
	}
	UPaper2DPlusAnimationMap* AnimationMap = Cast<UPaper2DPlusAnimationMap>(MoveNode->GetGraph());
	if (!AnimationMap || AnimationMap->bRebuildInProgress)
	{
		return;
	}
	AnimationMap->OnNodeMoveCommitted.ExecuteIfBound(MoveNode);
}

#undef LOCTEXT_NAMESPACE
