// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/EngineVersionComparison.h" // ENGINE_*_VERSION macros for the cross-version guards (U7) - explicit, not PCH-order-dependent
#include "ConnectionDrawingPolicy.h"

class UEdGraph;
class UPaper2DPlusAnimationMapNode_Transition;

/** 5.6 FVector2f sweep guard (combo-graph plan U7/A1): FConnectionDrawingPolicy's point-based
 *  virtuals (DrawSplineWithArrow point overload, ComputeSplineTangent, DrawConnection,
 *  DrawPreviewConnector) take FVector2D through 5.5 (ConnectionDrawingPolicy.h:114-120 in 5.0,
 *  :156-162 in 5.5) and FVector2f from 5.6 (:162-178) — where the FVector2D virtuals are `final`
 *  under -WarningsAsErrors. Override declarations AND shared bodies are written against this alias,
 *  so each engine version compiles exactly its native signature with ONE body per site. The alias
 *  also matches the same-version drift in the engine types the bodies touch (FGeometryHelper
 *  returns, ArrowRadius, FPaintGeometry/MakeRotatedBox parameters — FVector2D pre-5.6, FVector2f /
 *  deprecation-bridged from 5.6). File-unique name per the unity rule. */
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6)
using FAnimationMapDrawVector = FVector2f;
#else
using FAnimationMapDrawVector = FVector2D;
#endif

/**
 * Connection drawing policy for the Animation Map (combo-graph plan U4 shell + U6 fidelity, R8/R14) —
 * the full FStateMachineConnectionDrawingPolicy clone with the data-graph divergences:
 *
 * Geometry substitution cloned from the state-machine policy
 * (StateMachineConnectionDrawingPolicy.cpp:71-118): a link whose INPUT pin belongs to a transition
 * (edge) node draws as ONE arrow from the From-move-node's geometry to the To-move-node's geometry,
 * resolved through the NodeWidgetMap built in the Draw override. The second physical link
 * (Edge.Out -> To.In) is silently skipped for free: the edge widget creates no pin widgets, so its
 * output pin never enters the pin-geometry map the base Draw iterates.
 *
 * U6 additions:
 *  - WIRE COLOR comes from SAnimationMapTransitionNode::StaticGetTransitionColor — THE shared color
 *    function the pill background also uses (hover via HoveredPins: the pill's OnMouseEnter adds its
 *    hidden input pin to the panel hover set), plus the engine's ApplyHoverDeemphasis pass — clone of
 *    StateMachineConnectionDrawingPolicy.cpp:42-69.
 *  - (The pre-TASK-108-U2 per-index parallel-lane offsets are GONE: one edge per (From, To) pair by
 *    construction, so same-pair wires cannot exist. The fixed 6px direction bias still separates an
 *    A->B / B->A opposite-direction pair.)
 *  - SELF-LOOP ARC (R14): From == To draws SAnimationMapTransitionNode::MakeSelfLoopSpline (out the
 *    right edge, over the top-right corner, back into the top edge) as an explicit-tangent hermite
 *    via DrawConnection (hover/slice machinery included) + a downward arrowhead at the re-entry —
 *    and the pill's second-pass layout places itself on the SAME spline's midpoint.
 */
class FAnimationMapConnectionDrawingPolicy : public FConnectionDrawingPolicy
{
public:
	FAnimationMapConnectionDrawingPolicy(int32 InBackLayerID, int32 InFrontLayerID, float InZoomFactor,
		const FSlateRect& InClippingRect, FSlateWindowElementList& InDrawElements, UEdGraph* InGraphObj);

	//~ Begin FConnectionDrawingPolicy interface (DetermineWiringStyle / Draw / DetermineLinkGeometry /
	//~ the FGeometry DrawSplineWithArrow are signature-stable across 5.0-5.7; the point-based pair
	//~ resolves through FAnimationMapDrawVector to the native FVector2D (pre-5.6) / FVector2f (5.6+)
	//~ virtual — the U7 dual-signature guard with one body)
	virtual void DetermineWiringStyle(UEdGraphPin* OutputPin, UEdGraphPin* InputPin, /*inout*/ FConnectionParams& Params) override;
	virtual void Draw(TMap<TSharedRef<SWidget>, FArrangedWidget>& InPinGeometries, FArrangedChildren& ArrangedNodes) override;
	virtual void DetermineLinkGeometry(
		FArrangedChildren& ArrangedNodes,
		TSharedRef<SWidget>& OutputPinWidget,
		UEdGraphPin* OutputPin,
		UEdGraphPin* InputPin,
		/*out*/ FArrangedWidget*& StartWidgetGeometry,
		/*out*/ FArrangedWidget*& EndWidgetGeometry) override;
	virtual void DrawSplineWithArrow(const FGeometry& StartGeom, const FGeometry& EndGeom, const FConnectionParams& Params) override;
	virtual void DrawSplineWithArrow(const FAnimationMapDrawVector& StartPoint, const FAnimationMapDrawVector& EndPoint, const FConnectionParams& Params) override;
	virtual FAnimationMapDrawVector ComputeSplineTangent(const FAnimationMapDrawVector& Start, const FAnimationMapDrawVector& End) const override;
	//~ End FConnectionDrawingPolicy interface

protected:
	/** The edge node behind the link being drawn, derived per call from Params.AssociatedPin2 (the
	 *  edge's hidden input pin, stamped by DetermineWiringStyle). NO stashed member: the drag-preview
	 *  connector (base DrawPreviewConnector) reaches DrawSplineWithArrow without a fresh
	 *  DetermineWiringStyle for an edge link, and a stale stash would lane-offset the preview wire.
	 *  Null for the preview (AssociatedPin2 unset) -> lane offset 0, no self-loop branch. */
	static UPaper2DPlusAnimationMapNode_Transition* ResolveTransitionNode(const FConnectionParams& Params);

	/** Straight line + MakeRotatedBox arrowhead (StateMachineConnectionDrawingPolicy.cpp:148-274,
	 *  simplified: no relink handles, no bidirectional pass) + the U6 per-index perpendicular lane
	 *  offset from the shared sibling order. */
	void Internal_DrawLineWithArrow(const FAnimationMapDrawVector& StartAnchorPoint, const FAnimationMapDrawVector& EndAnchorPoint, const FConnectionParams& Params);

	/** U6 (R14): the self-loop arc + arrowhead for From == To, fanned per sibling index. NodeGeom is
	 *  the single move node's arranged geometry (draw space). */
	void DrawSelfLoopArc(const FGeometry& NodeGeom, UPaper2DPlusAnimationMapNode_Transition* TransNode,
		const FConnectionParams& Params);

	UEdGraph* GraphObj;

	/** Node -> ArrangedNodes index acceleration map, rebuilt per Draw (the state-machine recipe). */
	TMap<UEdGraphNode*, int32> NodeWidgetMap;
};
