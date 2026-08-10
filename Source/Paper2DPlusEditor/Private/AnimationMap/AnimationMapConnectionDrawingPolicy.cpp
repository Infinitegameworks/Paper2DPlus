// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "AnimationMap/AnimationMapConnectionDrawingPolicy.h"

#include "AnimationMap/Paper2DPlusAnimationMapNode_ChainEnd.h"
#include "AnimationMap/Paper2DPlusAnimationMapNode_ChainStart.h"
#include "AnimationMap/Paper2DPlusAnimationMapNode_Move.h"
#include "AnimationMap/Paper2DPlusAnimationMapNode_Transition.h"
#include "AnimationMap/SAnimationMapTransitionNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Layout/ArrangedChildren.h"
#include "Layout/ArrangedWidget.h"
#include "Rendering/DrawElements.h"
#include "SGraphNode.h"
#include "Styling/AppStyle.h"

FAnimationMapConnectionDrawingPolicy::FAnimationMapConnectionDrawingPolicy(int32 InBackLayerID, int32 InFrontLayerID,
	float InZoomFactor, const FSlateRect& InClippingRect, FSlateWindowElementList& InDrawElements, UEdGraph* InGraphObj)
	: FConnectionDrawingPolicy(InBackLayerID, InFrontLayerID, InZoomFactor, InClippingRect, InDrawElements)
	, GraphObj(InGraphObj)
{
	// The anim state machine's arrowhead glyph (verified in StarshipStyle.cpp:4057).
	ArrowImage = FAppStyle::Get().GetBrush(TEXT("Graph.AnimStateNode.ConnectionArrow"));
}

UPaper2DPlusAnimationMapNode_Transition* FAnimationMapConnectionDrawingPolicy::ResolveTransitionNode(const FConnectionParams& Params)
{
	// AssociatedPin2 is the edge's hidden INPUT pin (stamped by DetermineWiringStyle per link); the
	// drag-preview connector leaves it unset -> null -> lane offset 0, no self-loop branch.
	return Params.AssociatedPin2
		? Cast<UPaper2DPlusAnimationMapNode_Transition>(Params.AssociatedPin2->GetOwningNodeUnchecked())
		: nullptr;
}

void FAnimationMapConnectionDrawingPolicy::DetermineWiringStyle(UEdGraphPin* OutputPin, UEdGraphPin* InputPin,
	/*inout*/ FConnectionParams& Params)
{
	// Clone of FStateMachineConnectionDrawingPolicy::DetermineWiringStyle
	// (StateMachineConnectionDrawingPolicy.cpp:42-69): wire color via THE shared static on the pill
	// widget (so wire and pill cannot disagree), hover via HoveredPins (the pill's OnMouseEnter adds
	// its hidden input pin), then the engine's deemphasis pass.
	Params.AssociatedPin1 = OutputPin;
	Params.AssociatedPin2 = InputPin;
	Params.WireThickness = 1.5f;

	if (InputPin)
	{
		if (UPaper2DPlusAnimationMapNode_Transition* TransNode =
			Cast<UPaper2DPlusAnimationMapNode_Transition>(InputPin->GetOwningNodeUnchecked()))
		{
			const bool bInputPinHovered = HoveredPins.Contains(InputPin);
			Params.WireColor = SAnimationMapTransitionNode::StaticGetTransitionColor(TransNode, bInputPinHovered);
		}
	}

	// Chain Start marker -> move wires (chain-start rework) carry the marker family's amber (the
	// ▶ glyph / pill label color) so the designation wire never reads as a transition.
	if (OutputPin && Cast<UPaper2DPlusAnimationMapNode_ChainStart>(OutputPin->GetOwningNodeUnchecked()))
	{
		Params.WireColor = FLinearColor(0.95f, 0.78f, 0.20f);
	}

	// Move -> Chain End marker wires (chain-end rework — the mirror; the end marker owns the INPUT
	// side of its wire) carry the end family's slate-blue (the ⏹ glyph / pill label color), same
	// never-reads-as-a-transition rationale.
	if (InputPin && Cast<UPaper2DPlusAnimationMapNode_ChainEnd>(InputPin->GetOwningNodeUnchecked()))
	{
		Params.WireColor = FLinearColor(0.45f, 0.55f, 0.95f);
	}

	const bool bDeemphasizeUnhoveredPins = HoveredPins.Num() > 0;
	if (bDeemphasizeUnhoveredPins)
	{
		ApplyHoverDeemphasis(OutputPin, InputPin, /*inout*/ Params.WireThickness, /*inout*/ Params.WireColor);
	}
}

void FAnimationMapConnectionDrawingPolicy::Draw(TMap<TSharedRef<SWidget>, FArrangedWidget>& InPinGeometries, FArrangedChildren& ArrangedNodes)
{
	// Build the node -> arranged-geometry acceleration map (StateMachineConnectionDrawingPolicy.cpp:104-118)
	// so DetermineLinkGeometry can substitute MOVE NODE geometry for the edge node's hidden pins.
	NodeWidgetMap.Empty();
	for (int32 NodeIndex = 0; NodeIndex < ArrangedNodes.Num(); ++NodeIndex)
	{
		FArrangedWidget& CurWidget = ArrangedNodes[NodeIndex];
		TSharedRef<SGraphNode> ChildNode = StaticCastSharedRef<SGraphNode>(CurWidget.Widget);
		NodeWidgetMap.Add(ChildNode->GetNodeObj(), NodeIndex);
	}

	FConnectionDrawingPolicy::Draw(InPinGeometries, ArrangedNodes);
}

void FAnimationMapConnectionDrawingPolicy::DetermineLinkGeometry(
	FArrangedChildren& ArrangedNodes,
	TSharedRef<SWidget>& OutputPinWidget,
	UEdGraphPin* OutputPin,
	UEdGraphPin* InputPin,
	/*out*/ FArrangedWidget*& StartWidgetGeometry,
	/*out*/ FArrangedWidget*& EndWidgetGeometry)
{
	// The geometry substitution: a link INTO a transition node draws From-node -> To-node. Null-safe
	// throughout — a half-rebuilt edge (missing endpoint, widget not yet arranged) just doesn't draw
	// this frame (both out-geometries stay null and the base Draw skips the connection). Self-loops
	// resolve BOTH ends to the same arranged widget — DrawSplineWithArrow detects that and arcs.
	if (UPaper2DPlusAnimationMapNode_Transition* TransNode = InputPin ? Cast<UPaper2DPlusAnimationMapNode_Transition>(InputPin->GetOwningNodeUnchecked()) : nullptr)
	{
		UPaper2DPlusAnimationMapNode_Move* PrevNode = TransNode->GetPreviousMoveNode();
		UPaper2DPlusAnimationMapNode_Move* NextNode = TransNode->GetNextMoveNode();
		if (PrevNode && NextNode)
		{
			const int32* PrevNodeIndex = NodeWidgetMap.Find(PrevNode);
			const int32* NextNodeIndex = NodeWidgetMap.Find(NextNode);
			if (PrevNodeIndex && NextNodeIndex)
			{
				StartWidgetGeometry = &(ArrangedNodes[*PrevNodeIndex]);
				EndWidgetGeometry = &(ArrangedNodes[*NextNodeIndex]);
			}
		}
		return;
	}

	// Chain Start marker -> move links (chain-start rework) land here: both ends are VISIBLE
	// full-body pins with real widget geometry, so the engine's pin-to-pin default draws them
	// correctly (no substitution needed). Also the defensive fallback for any future link shape.
	FConnectionDrawingPolicy::DetermineLinkGeometry(ArrangedNodes, OutputPinWidget, OutputPin, InputPin,
		StartWidgetGeometry, EndWidgetGeometry);
}

void FAnimationMapConnectionDrawingPolicy::DrawSplineWithArrow(const FGeometry& StartGeom, const FGeometry& EndGeom, const FConnectionParams& Params)
{
	// Seed/anchor recipe from StateMachineConnectionDrawingPolicy.cpp (geometry overload). All vector
	// locals use the U7 FAnimationMapDrawVector alias (FVector2D pre-5.6 / FVector2f 5.6+) so this single
	// body matches FGeometryHelper's per-version return type (FVector2D through 5.5,
	// FDeprecateVector2DResult from 5.6 — converts to either).
	const FAnimationMapDrawVector StartCenter = FGeometryHelper::CenterOf(StartGeom);
	const FAnimationMapDrawVector EndCenter = FGeometryHelper::CenterOf(EndGeom);

	// SELF-LOOP (R14: From == To is legal data): coincident geometries mean both link ends resolved to
	// the same move node — draw the loop arc the pill also sits on (U6; U4 drew nothing here).
	if ((EndCenter - StartCenter).IsNearlyZero(1.0f))
	{
		if (UPaper2DPlusAnimationMapNode_Transition* TransNode = ResolveTransitionNode(Params))
		{
			DrawSelfLoopArc(StartGeom, TransNode, Params);
		}
		return; // never the degenerate zero-length arrow
	}

	const FAnimationMapDrawVector SeedPoint = (StartCenter + EndCenter) * 0.5f;
	const FAnimationMapDrawVector StartAnchorPoint = FGeometryHelper::FindClosestPointOnGeom(StartGeom, SeedPoint);
	const FAnimationMapDrawVector EndAnchorPoint = FGeometryHelper::FindClosestPointOnGeom(EndGeom, SeedPoint);

	DrawSplineWithArrow(StartAnchorPoint, EndAnchorPoint, Params);
}

void FAnimationMapConnectionDrawingPolicy::DrawSplineWithArrow(const FAnimationMapDrawVector& StartPoint, const FAnimationMapDrawVector& EndPoint, const FConnectionParams& Params)
{
	// No bidirectional second pass (the animation map has no bidirectional wires). The alias signature
	// resolves to the native FVector2D (pre-5.6) / FVector2f (5.6+) virtual — one body, both versions.
	Internal_DrawLineWithArrow(StartPoint, EndPoint, Params);
}

void FAnimationMapConnectionDrawingPolicy::Internal_DrawLineWithArrow(const FAnimationMapDrawVector& StartAnchorPoint, const FAnimationMapDrawVector& EndAnchorPoint, const FConnectionParams& Params)
{
	// Simplified clone of FStateMachineConnectionDrawingPolicy::Internal_DrawLineWithArrow
	// (StateMachineConnectionDrawingPolicy.cpp:147-274): perpendicular separation bias (keeps an A->B /
	// B->A pair from overlapping) + arrow-length bias + straight line + MakeRotatedBox arrowhead.
	// Dropped: relink grab handles, hover circles, relink-count text, bidirectional.
	const FAnimationMapDrawVector DeltaPos = EndAnchorPoint - StartAnchorPoint;
	if (DeltaPos.IsNearlyZero())
	{
		return; // belt-and-braces for degenerate anchors (the geometry overload already filters these)
	}

	const FAnimationMapDrawVector UnitDelta = DeltaPos.GetSafeNormal();
	const FAnimationMapDrawVector Normal = FAnimationMapDrawVector(DeltaPos.Y, -DeltaPos.X).GetSafeNormal();

	// One edge per (From, To) pair since TASK-108 U2 — the pre-U2 per-index parallel-lane offset is
	// gone (it was identically zero at index 0 / count 1). The engine's fixed 6px direction bias
	// remains: Normal flips with wire direction, so an A->B / B->A pair still separates.
	const float LineSeparationAmount = 6.0f * ZoomFactor;
	const FAnimationMapDrawVector DirectionBias = Normal * LineSeparationAmount;
	const FAnimationMapDrawVector LengthBias = UnitDelta * ArrowRadius.X;
	const FAnimationMapDrawVector StartPoint = StartAnchorPoint + DirectionBias + LengthBias;
	const FAnimationMapDrawVector EndPoint = EndAnchorPoint + DirectionBias - LengthBias;

	// LengthBias * 0.8f keeps the line from overlapping the arrowhead glyph (engine comment).
	// DrawConnection resolves to the native FVector2D (pre-5.6) / FVector2f (5.6+) overload via the alias.
	DrawConnection(WireLayerID, StartPoint, EndPoint - (LengthBias * 0.8f), Params);

	const FAnimationMapDrawVector ArrowDrawPos = EndPoint - ArrowRadius;
	const double AngleInRadians = FMath::Atan2(DeltaPos.Y, DeltaPos.X);

	// FPaintGeometry/MakeRotatedBox take FVector2D pre-5.2 (PaintGeometry.h:111, DrawElements.h:117-126
	// in 5.0), the deprecation-bridge parameter types from 5.2 — TOptional<FAnimationMapDrawVector>
	// matches each version's rotation-point parameter (5.1's FVector2d overload included).
	FSlateDrawElement::MakeRotatedBox(
		DrawElementsList,
		ArrowLayerID,
		FPaintGeometry(ArrowDrawPos, ArrowImage->ImageSize * ZoomFactor, ZoomFactor),
		ArrowImage,
		ESlateDrawEffect::None,
		static_cast<float>(AngleInRadians),
		TOptional<FAnimationMapDrawVector>(),
		FSlateDrawElement::RelativeToElement,
		Params.WireColor);
}

void FAnimationMapConnectionDrawingPolicy::DrawSelfLoopArc(const FGeometry& NodeGeom,
	UPaper2DPlusAnimationMapNode_Transition* TransNode, const FConnectionParams& Params)
{
	// U6 (R14): the self-loop arc — THE shared MakeSelfLoopSpline (the pill's second-pass layout sits
	// on the same spline's midpoint, so arc and pill always agree). One arc per pair since TASK-108
	// U2 (the per-sibling fan is gone); extent scales with zoom (the pill computes in graph space;
	// this draws in panel draw space).

	// The spline math itself stays FVector2f on every version (the struct is shared with the pill's
	// second-pass layout); FGeometry::AbsolutePosition is FVector2f in ALL of 5.0-5.7. GetDrawSize
	// returns FVector2D through 5.1 and the bridge type from 5.2 — the FVector2D way-station converts
	// cleanly on every version (the FVector2D->FVector2f constructor is explicit, hence two steps).
	const FVector2f RectMin = FVector2f(NodeGeom.AbsolutePosition); // draw space (FGeometryHelper::CenterOf's base)
	const FVector2D RectSizeD = NodeGeom.GetDrawSize();
	const FVector2f RectSize(RectSizeD);
	const float Extent = SAnimationMapTransitionNode::GetSelfLoopExtent() * ZoomFactor;
	const SAnimationMapTransitionNode::FSelfLoopSpline Loop =
		SAnimationMapTransitionNode::MakeSelfLoopSpline(RectMin, RectSize, Extent);

	// Arrival is straight DOWN into the top edge (the spline's end tangent) — mirror the straight-line
	// arrowhead recipe with UnitDelta = (0, 1). Boundary conversions out of the FVector2f spline into
	// the per-version draw vector are explicit (FAnimationMapDrawVector(FVector2f) is the explicit
	// cross-precision constructor pre-5.6 and a plain copy on 5.6+).
	const FAnimationMapDrawVector ArrivalDir(0.0f, 1.0f);
	const FAnimationMapDrawVector LengthBias = ArrivalDir * ArrowRadius.X;
	const FAnimationMapDrawVector LoopStart(Loop.Start);
	const FAnimationMapDrawVector EndPoint = FAnimationMapDrawVector(Loop.End) - LengthBias;

#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 2)
	// Explicit-tangent hermite through DrawConnection (keeps the engine's hover/slice machinery on
	// the loop): DrawConnection prefers Params tangents when non-zero (ConnectionDrawingPolicy.cpp:
	// 307-315 DrawConnection_DeprecationHelper). FConnectionParams::Start/EndTangent exist from 5.2
	// (ConnectionDrawingPolicy.h:71-72 in 5.2; absent in 5.0/5.1) — FVector2D assigns into both the
	// 5.2-5.5 FVector2D members and the 5.6+ FDeprecateSlateVector2D bridge.
	FConnectionParams LoopParams = Params;
	LoopParams.StartTangent = FVector2D(Loop.StartTangent);
	LoopParams.EndTangent = FVector2D(Loop.EndTangent);

	DrawConnection(WireLayerID, LoopStart, EndPoint - (LengthBias * 0.8f), LoopParams);
#else
	// 5.0/5.1: FConnectionParams has no Start/EndTangent (added 5.2) — draw the SAME hermite directly
	// via MakeDrawSpaceSpline, which is exactly what DrawConnection bottoms out in (5.0
	// ConnectionDrawingPolicy.cpp:331-339). BEHAVIORAL DIFFERENCE on 5.0/5.1 ONLY: bypassing
	// DrawConnection skips the engine's spline-overlap (wire hover/click) machinery for the self-loop
	// ARC — the pill itself still hovers/selects, and the wire color is unchanged (DetermineWiringStyle
	// already stamped Params.WireColor). Same shape, same midpoint, so the pill still sits on the arc.
	FSlateDrawElement::MakeDrawSpaceSpline(
		DrawElementsList,
		WireLayerID,
		LoopStart, FVector2D(Loop.StartTangent),
		EndPoint - (LengthBias * 0.8f), FVector2D(Loop.EndTangent),
		Params.WireThickness,
		ESlateDrawEffect::None,
		Params.WireColor);
#endif

	const FAnimationMapDrawVector ArrowDrawPos = EndPoint - ArrowRadius;
	const double AngleInRadians = FMath::Atan2(ArrivalDir.Y, ArrivalDir.X); // straight down

	FSlateDrawElement::MakeRotatedBox(
		DrawElementsList,
		ArrowLayerID,
		FPaintGeometry(ArrowDrawPos, ArrowImage->ImageSize * ZoomFactor, ZoomFactor),
		ArrowImage,
		ESlateDrawEffect::None,
		static_cast<float>(AngleInRadians),
		TOptional<FAnimationMapDrawVector>(),
		FSlateDrawElement::RelativeToElement,
		Params.WireColor);
}

FAnimationMapDrawVector FAnimationMapConnectionDrawingPolicy::ComputeSplineTangent(const FAnimationMapDrawVector& Start, const FAnimationMapDrawVector& End) const
{
	// Straight lines (the state-machine recipe): the tangent IS the normalized delta, so DrawConnection's
	// spline degenerates to a straight segment. (Self-loops bypass this via explicit Params tangents on
	// 5.2+, and draw the hermite directly on 5.0/5.1.) Alias signature = the native virtual per version.
	const FAnimationMapDrawVector Delta = End - Start;
	return Delta.GetSafeNormal();
}
