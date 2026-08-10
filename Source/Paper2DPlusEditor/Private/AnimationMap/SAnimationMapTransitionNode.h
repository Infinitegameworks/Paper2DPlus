// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/EngineVersionComparison.h" // ENGINE_*_VERSION macros for the cross-version guards (U7) - explicit, not PCH-order-dependent
#include "SGraphNode.h"

class UPaper2DPlusAnimationMapNode_Transition;

/** 5.6 FVector2f sweep guard (combo-graph plan U7/A1) — same contract as FAnimationMapMoveNodeVector in
 *  SAnimationMapMoveNode.h: MoveTo's native parameter is FVector2D through 5.5 and FVector2f from 5.6
 *  (the FVector2D virtual is `final` under -WarningsAsErrors there), so the override declares exactly
 *  the native signature per version through this alias. File-unique name per the unity rule. */
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6)
using FAnimationMapTransitionNodeVector = FVector2f;
#else
using FAnimationMapTransitionNodeVector = FVector2D;
#endif

/**
 * Edge-pill widget for the Animation Map's transition (edge) nodes (combo-graph plan U4 shell + U6
 * fidelity, R6/R8/R13/R14). Clone of SGraphNodeAnimTransition with the data-graph divergences:
 *
 *  - Pill content (TASK-108 U2 — the label/condition-glyph/cancel/order-badge cluster died with the
 *    runtime fields in U1): the TARGET's phase badge is the PRIMARY content ("this arrow enters
 *    <Phase>"); a minimal arrow glyph shows only while the target carries no phase tag, so the pill
 *    always has a visible, clickable body. All content is _Lambda-bound, so in-place SetFromRow
 *    snapshot syncs update every affected pill without a widget rebuild (the refresh-rules
 *    discipline).
 *  - Selection ring: visibility-lambda on the owner panel's SelectionManager (clone of
 *    SGraphNodeAnimTransition.cpp:275-290).
 *  - OnPaint boosts LayerId+100 so pills sort in front of move nodes (clone of :471-475).
 *  - Hover: the pill adds its hidden input pin to the panel hover set (clone of :449-469) so the
 *    drawing policy's HoveredPins check highlights the WIRE while the PILL is hovered.
 *  - StaticGetTransitionColor is THE shared color source for pill background AND wire (the anim-SM
 *    pattern, :336-410) — hover tint, warning tint for rows whose target is a stub — so the two can
 *    never disagree.
 *  - MoveTo is a NO-OP (the pill's position derives from the move nodes it connects — and a real
 *    MoveTo would re-enter U5's write-through); position comes from
 *    RequiresSecondPassLayout/PerformSecondPassLayout. One edge per (From, To) pair since TASK-108
 *    U2, so the pre-U2 parallel-sibling lane machinery (GetParallelSiblingOrder + per-index fans)
 *    is GONE — every pill centers on its pair's one wire. Self-loops (From == To, legal, one arc
 *    per pair) place the pill ON the loop arc (the hermite midpoint of MakeSelfLoopSpline, shared
 *    with the policy's arc draw) instead of collapsing onto the degenerate midpoint.
 */
class SAnimationMapTransitionNode : public SGraphNode
{
public:
	SLATE_BEGIN_ARGS(SAnimationMapTransitionNode) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UPaper2DPlusAnimationMapNode_Transition* InNode);

	//~ Begin SNodePanel::SNode interface
	// FAnimationMapTransitionNodeVector = FVector2f on 5.6+, FVector2D before (the 5.6 sweep guard above).
	virtual void MoveTo(const FAnimationMapTransitionNodeVector& NewPosition, FNodeSet& NodeFilter, bool bMarkDirty = true) override;
	virtual bool RequiresSecondPassLayout() const override;
	virtual void PerformSecondPassLayout(const TMap<UObject*, TSharedRef<SNode>>& NodeToWidgetLookup) const override;
	//~ End SNodePanel::SNode interface

	//~ Begin SGraphNode / SWidget interface
	virtual void UpdateGraphNode() override;
	virtual const FSlateBrush* GetShadowBrush(bool bSelected) const override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle,
		bool bParentEnabled) const override;
	virtual void OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void OnMouseLeave(const FPointerEvent& MouseEvent) override;
	//~ End SGraphNode / SWidget interface

	/** THE shared transition color (anim-SM pattern, SGraphNodeAnimTransition.cpp:336-410): used by
	 *  BOTH the pill background and the drawing policy's wire so they cannot disagree. Hovered ->
	 *  accent orange; target-is-stub (or missing endpoint) -> warning; else neutral foreground. */
	static FLinearColor StaticGetTransitionColor(const UPaper2DPlusAnimationMapNode_Transition* TransNode, bool bIsHovered);

	/** Self-loop spline (R14): a cubic hermite leaving the node's RIGHT edge (tangent +X), looping
	 *  over the top-right corner, re-entering the TOP edge (tangent +Y, i.e. arriving downward).
	 *  Shared by the pill placement (graph space, scale 1) and the drawing policy (panel draw space,
	 *  extent pre-multiplied by zoom) — same math, same shape, so the pill sits ON the arc. */
	struct FSelfLoopSpline
	{
		FVector2f Start = FVector2f::ZeroVector;        // right-center anchor
		FVector2f StartTangent = FVector2f::ZeroVector; // +X, hermite derivative
		FVector2f End = FVector2f::ZeroVector;          // top-center anchor
		FVector2f EndTangent = FVector2f::ZeroVector;   // +Y (arrives downward into the top edge)

		/** Cubic hermite midpoint H(0.5) = (P0+P1)/2 + (T0-T1)/8 — the pill's center on the arc. */
		FVector2f Midpoint() const
		{
			return (Start + End) * 0.5f + (StartTangent - EndTangent) * 0.125f;
		}
	};
	static FSelfLoopSpline MakeSelfLoopSpline(const FVector2f& RectMin, const FVector2f& RectSize, float Extent);

	/** Self-loop arc extent (unscaled; the policy multiplies by ZoomFactor). One arc per pair since
	 *  U2 — the pre-U2 per-sibling fan step is gone. */
	static float GetSelfLoopExtent();

private:
	/** Clone of SGraphNodeAnimTransition::PositionBetweenTwoNodesWithOffset
	 *  (SGraphNodeAnimTransition.cpp:477-524, minus CachedRotation and minus the multi-node sibling
	 *  spread — one edge per pair since U2): centers the pill on the connecting line, elevated
	 *  perpendicular. */
	void PositionBetweenTwoNodes(const FGeometry& StartGeom, const FGeometry& EndGeom) const;
};
