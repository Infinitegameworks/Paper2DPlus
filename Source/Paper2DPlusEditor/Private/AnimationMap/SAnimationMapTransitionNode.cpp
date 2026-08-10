// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "AnimationMap/SAnimationMapTransitionNode.h"

#include "AnimationMap/Paper2DPlusAnimationMapNode_Move.h"
#include "AnimationMap/Paper2DPlusAnimationMapNode_Transition.h"
#include "AnimationMapCore.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "ConnectionDrawingPolicy.h" // FGeometryHelper
#include "EdGraph/EdGraphPin.h"
#include "SGraphPanel.h"
#include "SGraphPin.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Styling/StyleColors.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SAnimationMapTransitionNode"

namespace
{
	// Self-loop arc clearance past the node's top-right corner (file-unique name per the unity rule).
	// Graph-space px (the policy scales by zoom). One arc per (From, To) pair since TASK-108 U2 —
	// the per-sibling fan step died with duplicate edges.
	constexpr float AnimationMapTransition_SelfLoopBaseExtent = 48.0f;

	// Dark content color over the light ColorSpill pill background.
	const FLinearColor AnimationMapTransition_PillTextColor(0.02f, 0.02f, 0.02f);

	/** Small explicit target-endpoint grip. It wraps the transition node's existing hidden OUTPUT pin,
	 *  so stock FDragConnection drives the gesture while the schema redirects the drop to the panel's
	 *  atomic rewire funnel. Modifier drags are refused to prevent break/copy behavior bypassing data. */
	class SAnimationMapTransitionTargetPin : public SGraphPin
	{
	public:
		SLATE_BEGIN_ARGS(SAnimationMapTransitionTargetPin) {}
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs, UEdGraphPin* InPin)
		{
			GraphPinObj = InPin;
			check(GraphPinObj && GraphPinObj->GetSchema());
			bShowLabel = false;
			SBorder::Construct(SBorder::FArguments()
				.BorderImage(FAppStyle::Get().GetBrush("WhiteBrush"))
				.BorderBackgroundColor(FLinearColor(0.08f, 0.11f, 0.15f, 0.9f))
				.Padding(FMargin(3.0f, 1.0f))
				.OnMouseButtonDown(this, &SAnimationMapTransitionTargetPin::OnGripMouseDown)
				.Cursor(this, &SAnimationMapTransitionTargetPin::GetPinCursor)
				.ToolTipText(LOCTEXT("RewireTargetGripTip", "Drag onto another move to replace this transition's target."))
				[
					SNew(STextBlock)
					.Text(LOCTEXT("RewireTargetGripGlyph", "↗"))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 7))
					.ColorAndOpacity(FSlateColor(FLinearColor(0.72f, 0.84f, 1.0f)))
				]);
		}

	protected:
		virtual TSharedRef<SWidget> GetDefaultValueWidget() override { return SNullWidget::NullWidget; }

	private:
		FReply OnGripMouseDown(const FGeometry& Geometry, const FPointerEvent& MouseEvent)
		{
			if (MouseEvent.IsAltDown() || MouseEvent.IsControlDown() || MouseEvent.IsShiftDown())
			{
				return FReply::Unhandled();
			}
			return OnPinMouseDown(Geometry, MouseEvent);
		}
	};
}

void SAnimationMapTransitionNode::Construct(const FArguments& InArgs, UPaper2DPlusAnimationMapNode_Transition* InNode)
{
	this->GraphNode = InNode;
	this->UpdateGraphNode();
}

void SAnimationMapTransitionNode::MoveTo(const FAnimationMapTransitionNodeVector& NewPosition, FNodeSet& NodeFilter, bool bMarkDirty)
{
	// Deliberate no-op (SGraphNodeAnimTransition.cpp:69-72): the pill's position is derived from the
	// two move nodes it connects (second-pass layout below). It must also NEVER write through — U5's
	// move-node MoveTo override persists positions, and an edge has no position of its own to persist.
}

bool SAnimationMapTransitionNode::RequiresSecondPassLayout() const
{
	return true;
}

float SAnimationMapTransitionNode::GetSelfLoopExtent()
{
	return AnimationMapTransition_SelfLoopBaseExtent;
}

SAnimationMapTransitionNode::FSelfLoopSpline SAnimationMapTransitionNode::MakeSelfLoopSpline(
	const FVector2f& RectMin, const FVector2f& RectSize, float Extent)
{
	// Out the RIGHT edge, around the top-right corner, back into the TOP edge arriving downward.
	// Tangent magnitude tuned so the hermite midpoint (= the pill spot) clears the corner by roughly
	// Extent and the arc bulge clears the node edges (bulge ~= 4/27 * |T| past each edge).
	FSelfLoopSpline Loop;
	Loop.Start = FVector2f(RectMin.X + RectSize.X, RectMin.Y + RectSize.Y * 0.5f);
	Loop.End = FVector2f(RectMin.X + RectSize.X * 0.5f, RectMin.Y);

	const float TangentMagnitude = 6.0f * FMath::Max(Extent, 1.0f) + 0.5f * (RectSize.X + RectSize.Y);
	Loop.StartTangent = FVector2f(TangentMagnitude, 0.0f);
	Loop.EndTangent = FVector2f(0.0f, TangentMagnitude);
	return Loop;
}

void SAnimationMapTransitionNode::PerformSecondPassLayout(const TMap<UObject*, TSharedRef<SNode>>& NodeToWidgetLookup) const
{
	// Clone of SGraphNodeAnimTransition::PerformSecondPassLayout (SGraphNodeAnimTransition.cpp:79-121)
	// with TWO deliberate divergences: no parallel-sibling spread (one edge per (From, To) pair since
	// TASK-108 U2 — the pill centers on its pair's one wire), and self-loops place the pill ON the
	// loop arc (the engine's anim SM rejects self-wires; this graph projects them, one arc per pair).
	UPaper2DPlusAnimationMapNode_Transition* TransNode = CastChecked<UPaper2DPlusAnimationMapNode_Transition>(GraphNode);

	FGeometry StartGeom;
	FGeometry EndGeom;

	UPaper2DPlusAnimationMapNode_Move* PrevNode = TransNode->GetPreviousMoveNode();
	UPaper2DPlusAnimationMapNode_Move* NextNode = TransNode->GetNextMoveNode();
	if (PrevNode && NextNode)
	{
		const TSharedRef<SNode>* pPrevNodeWidget = NodeToWidgetLookup.Find(PrevNode);
		const TSharedRef<SNode>* pNextNodeWidget = NodeToWidgetLookup.Find(NextNode);
		if (pPrevNodeWidget && pNextNodeWidget)
		{
			StartGeom = FGeometry(FVector2D(PrevNode->NodePosX, PrevNode->NodePosY), FVector2D::ZeroVector, (*pPrevNodeWidget)->GetDesiredSize(), 1.0f);
			EndGeom = FGeometry(FVector2D(NextNode->NodePosX, NextNode->NodePosY), FVector2D::ZeroVector, (*pNextNodeWidget)->GetDesiredSize(), 1.0f);

			if (PrevNode == NextNode)
			{
				// SELF-LOOP: center the pill on the arc's hermite midpoint — graph space (scale 1),
				// the same MakeSelfLoopSpline the policy draws.
				const FVector2f RectMin(static_cast<float>(PrevNode->NodePosX), static_cast<float>(PrevNode->NodePosY));
				const FVector2f RectSize = FVector2f((*pPrevNodeWidget)->GetDesiredSize());
				const FSelfLoopSpline Loop = MakeSelfLoopSpline(RectMin, RectSize, GetSelfLoopExtent());
				const FVector2f PillCenter = Loop.Midpoint();
				const FVector2D DesiredNodeSize = GetDesiredSize();
				GraphNode->NodePosX = static_cast<int32>(PillCenter.X - 0.5f * DesiredNodeSize.X);
				GraphNode->NodePosY = static_cast<int32>(PillCenter.Y - 0.5f * DesiredNodeSize.Y);
				return;
			}
		}
	}

	// Null/missing endpoints leave the geometries zero — the offset math falls back to a small fixed
	// delta (no crash), matching the engine original's behavior for half-formed transitions.
	PositionBetweenTwoNodes(StartGeom, EndGeom);
}

void SAnimationMapTransitionNode::PositionBetweenTwoNodes(const FGeometry& StartGeom, const FGeometry& EndGeom) const
{
	// Clone of SGraphNodeAnimTransition::PositionBetweenTwoNodesWithOffset
	// (SGraphNodeAnimTransition.cpp:477-524), minus the CachedRotation stamp (no direction icon) and
	// minus the MultiNodeOffset sibling spread (one edge per pair since TASK-108 U2 — the offset was
	// identically zero at index 0 / count 1).
	const FVector2D StartCenter = FGeometryHelper::CenterOf(StartGeom);
	const FVector2D EndCenter = FGeometryHelper::CenterOf(EndGeom);
	const FVector2D SeedPoint = (StartCenter + EndCenter) * 0.5f;

	const FVector2D StartAnchorPoint = FGeometryHelper::FindClosestPointOnGeom(StartGeom, SeedPoint);
	const FVector2D EndAnchorPoint = FGeometryHelper::FindClosestPointOnGeom(EndGeom, SeedPoint);

	// Position halfway along the connecting line, elevated away perpendicular to it.
	const float Height = 24.0f;
	const FVector2D DesiredNodeSize = GetDesiredSize();

	FVector2D DeltaPos(EndAnchorPoint - StartAnchorPoint);
	if (DeltaPos.IsNearlyZero())
	{
		// Degenerate geometry (half-formed transition): the engine fallback keeps the math finite.
		DeltaPos = FVector2D(10.0f, 0.0f);
	}

	const FVector2D Normal = FVector2D(DeltaPos.Y, -DeltaPos.X).GetSafeNormal();
	const FVector2D NewCenter = StartAnchorPoint + (0.5f * DeltaPos) + (Height * Normal);
	const FVector2D NewCorner = NewCenter - (0.5f * DesiredNodeSize);

	GraphNode->NodePosX = static_cast<int32>(NewCorner.X);
	GraphNode->NodePosY = static_cast<int32>(NewCorner.Y);
}

FLinearColor SAnimationMapTransitionNode::StaticGetTransitionColor(
	const UPaper2DPlusAnimationMapNode_Transition* TransNode, bool bIsHovered)
{
	// THE shared color (anim-SM pattern, SGraphNodeAnimTransition.cpp:336-410): the pill background
	// and the policy's wire both call this, so they cannot disagree. Hover wins; a row whose target
	// is a stub (or whose endpoint is missing — half-formed) reads warning.
	if (bIsHovered)
	{
		return FStyleColors::AccentOrange.GetSpecifiedColor();
	}
	if (TransNode)
	{
		const UPaper2DPlusAnimationMapNode_Move* NextNode = TransNode->GetNextMoveNode();
		if (!NextNode || NextNode->bIsStub)
		{
			return FStyleColors::Warning.GetSpecifiedColor();
		}
	}
	return FStyleColors::Foreground.GetSpecifiedColor();
}

void SAnimationMapTransitionNode::UpdateGraphNode()
{
	InputPins.Empty();
	OutputPins.Empty();

	// Reset exposed boxes in case this is a refresh of an already-built node.
	RightNodeBox.Reset();
	LeftNodeBox.Reset();

	UPaper2DPlusAnimationMapNode_Transition* TransNode = CastChecked<UPaper2DPlusAnimationMapNode_Transition>(GraphNode);
	const FSlateFontInfo PillFont = FCoreStyle::GetDefaultFontStyle("Regular", 8);
	const FSlateFontInfo BadgeFont = FCoreStyle::GetDefaultFontStyle("Bold", 7);

	// Rounded white chip brush for the U4 phase badge — tinted live by BorderBackgroundColor (the
	// FlipbookBrowserPanel phase-chip idiom). Function-local static so its address outlives the widget.
	static FSlateRoundedBoxBrush AnimationMapTransition_PhaseChipBrush(FLinearColor::White, 3.0f);

	TSharedPtr<SGraphPin> TargetRewirePin;
	if (UEdGraphPin* OutputPin = TransNode->GetOutputPin())
	{
		TargetRewirePin = SNew(SAnimationMapTransitionTargetPin, OutputPin);
		TargetRewirePin->SetOwner(SharedThis(this));
	}

	// DELIBERATELY no CreatePinWidgets(), and the explicit rewire grip above stays OUT of InputPins /
	// OutputPins. SGraphPanel discovers drawable link geometry through those arrays; registering the
	// grip there exposes Edge.Out -> To.In as a second arrow beside the policy's intended direct
	// From -> To arrow. The grip remains interactive in the content tree: its mouse handler calls
	// SGraphPin::OnPinMouseDown directly, which needs the owner and pin object but not registration.
	this->ContentScale.Bind(this, &SGraphNode::GetContentScale);
	this->GetOrAddSlot(ENodeZone::Center)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(SOverlay)

			// Pill body: the anim transition's ColorSpill tinted by THE shared color function (the
			// same one the drawing policy tints the wire with — they cannot disagree).
			+ SOverlay::Slot()
			[
				SNew(SImage)
				.Image(FAppStyle::Get().GetBrush("Graph.AnimTransitionNode.ColorSpill"))
				.ColorAndOpacity_Lambda([this]()
				{
					const UPaper2DPlusAnimationMapNode_Transition* Node =
						Cast<UPaper2DPlusAnimationMapNode_Transition>(GraphNode);
					return FSlateColor(StaticGetTransitionColor(Node, IsHovered()));
				})
			]

			// Pill content (TASK-108 U2): the TARGET's phase badge is the PRIMARY content — the arrow
			// glyph shows only while the target carries no phase tag, so the pill always has a
			// visible, clickable body of stable height.
			+ SOverlay::Slot()
			.Padding(FMargin(6.0f, 3.0f))
			[
				SNew(SHorizontalBox)

				// Fallback arrow glyph — visible only when the effective transition phase is empty.
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Font(PillFont)
					.ColorAndOpacity(FSlateColor(AnimationMapTransition_PillTextColor))
					.Text_Lambda([this]()
					{
						return GraphNode ? GraphNode->GetNodeTitle(ENodeTitleType::ListView) : FText::GetEmpty();
					})
					.Visibility_Lambda([TransNode]()
					{
						return Paper2DPlusAnimationMap::GetPhaseTagBadge(TransNode->TransitionPhaseTag).IsValid()
							? EVisibility::Collapsed : EVisibility::Visible;
					})
				]

				// PHASE BADGE: this row's override, falling back to the target animation phase for an
				// unset legacy row. The shared badge formatter keeps pill and list colors consistent.
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(SBorder)
					.BorderImage(&AnimationMapTransition_PhaseChipBrush)
					.BorderBackgroundColor_Lambda([TransNode]()
					{
						return FSlateColor(
							Paper2DPlusAnimationMap::GetPhaseTagBadge(TransNode->TransitionPhaseTag).Color);
					})
					.Padding(FMargin(4.0f, 1.0f))
					.ToolTipText(LOCTEXT("PhaseBadgeTooltip",
						"Transition phase: this arrow's override, or the target animation phase when no override is authored"))
					.Visibility_Lambda([TransNode]()
					{
						return Paper2DPlusAnimationMap::GetPhaseTagBadge(TransNode->TransitionPhaseTag).IsValid()
							? EVisibility::Visible : EVisibility::Collapsed;
					})
					[
						SNew(STextBlock)
						.Font(BadgeFont)
						.Text_Lambda([TransNode]()
						{
							return FText::FromString(
								Paper2DPlusAnimationMap::GetPhaseTagBadge(TransNode->TransitionPhaseTag).Label);
						})
						.ColorAndOpacity_Lambda([TransNode]()
						{
							return FSlateColor(
								Paper2DPlusAnimationMap::GetPhaseTagBadge(TransNode->TransitionPhaseTag).TextColor);
						})
					]
				]

				// Small target-endpoint rewire grip. Dragging it never disconnects the original first;
				// the panel replaces data + links only after full validation.
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(FMargin(4.0f, 0.0f, 0.0f, 0.0f))
				[
					TargetRewirePin.IsValid()
						? TargetRewirePin.ToSharedRef()
						: SNullWidget::NullWidget
				]
			]

			// Selection ring (clone of SGraphNodeAnimTransition.cpp:275-290): visibility-lambda on the
			// owner panel's selection manager — survives in-place content refreshes for free.
			+ SOverlay::Slot()
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::Get().GetBrush("Graph.AnimTransitionNode.Selection"))
				.Padding(0.0f)
				.Visibility_Lambda([this]()
				{
					TSharedPtr<SGraphPanel> OwnerPanel = OwnerGraphPanelPtr.Pin();
					if (!OwnerPanel.IsValid())
					{
						return EVisibility::Hidden;
					}
					return OwnerPanel->SelectionManager.IsNodeSelected(GraphNode)
						? EVisibility::HitTestInvisible : EVisibility::Hidden;
				})
			]
		];
}

int32 SAnimationMapTransitionNode::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId,
	const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	// Boost layer ID to sort pills in front of move nodes (SGraphNodeAnimTransition.cpp:471-475).
	return SGraphNode::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId + 100,
		InWidgetStyle, bParentEnabled);
}

void SAnimationMapTransitionNode::OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	// Clone of SGraphNodeAnimTransition::OnMouseEnter (:449-458), null-safe on the owner panel: adding
	// the hidden input pin to the hover set is what makes the drawing policy's HoveredPins check tint
	// the WIRE while the PILL is hovered (the shared-color hover path).
	if (UPaper2DPlusAnimationMapNode_Transition* TransNode = Cast<UPaper2DPlusAnimationMapNode_Transition>(GraphNode))
	{
		if (UEdGraphPin* Pin = TransNode->GetInputPin())
		{
			if (TSharedPtr<SGraphPanel> OwnerPanel = GetOwnerPanel())
			{
				OwnerPanel->AddPinToHoverSet(Pin);
			}
		}
	}
	SGraphNode::OnMouseEnter(MyGeometry, MouseEvent);
}

void SAnimationMapTransitionNode::OnMouseLeave(const FPointerEvent& MouseEvent)
{
	// Clone of SGraphNodeAnimTransition::OnMouseLeave (:460-469), null-safe on the owner panel.
	if (UPaper2DPlusAnimationMapNode_Transition* TransNode = Cast<UPaper2DPlusAnimationMapNode_Transition>(GraphNode))
	{
		if (UEdGraphPin* Pin = TransNode->GetInputPin())
		{
			if (TSharedPtr<SGraphPanel> OwnerPanel = GetOwnerPanel())
			{
				OwnerPanel->RemovePinFromHoverSet(Pin);
			}
		}
	}
	SGraphNode::OnMouseLeave(MouseEvent);
}

const FSlateBrush* SAnimationMapTransitionNode::GetShadowBrush(bool bSelected) const
{
	// The default node shadow is sized for full blueprint nodes and dwarfs a small pill — use the anim
	// transition's shadow (verified in StarshipStyle.cpp:4118).
	return FAppStyle::Get().GetBrush(TEXT("Graph.AnimTransitionNode.Shadow"));
}

#undef LOCTEXT_NAMESPACE
