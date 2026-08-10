// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/EngineVersionComparison.h" // ENGINE_*_VERSION macros for the cross-version guards - explicit, not PCH-order-dependent
#include "SGraphNode.h"

class SOverlay;
class UPaper2DPlusAnimationMapNode_ChainStart;

/** 5.6 FVector2f sweep guard (the SAnimationMapMoveNode precedent): SNodePanel::SNode::MoveTo takes
 *  FVector2D through 5.5 and FVector2f from 5.6, where the FVector2D virtual is `final` under
 *  -WarningsAsErrors (UE_SLATE_DEPRECATED_VECTOR_VIRTUAL_FUNCTION) — each version must override
 *  EXACTLY its native signature. Own alias (file-unique) so this header does not depend on the
 *  move-node widget header. */
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6)
using FAnimationMapChainStartNodeVector = FVector2f;
#else
using FAnimationMapChainStartNodeVector = FVector2D;
#endif

/**
 * Compact widget for the Chain Start MARKER node: a small rounded pill labeled "▶ Chain Start",
 * deliberately much smaller than the move nodes (no thumbnail, no chips — the marker is a pointer,
 * not a subject). Shape is a scaled-down clone of SAnimationMapMoveNode's whole-node-body pin
 * layout: SelfHitTestInvisible root, one Graph.StateNode.Body border holding an SOverlay whose FIRST
 * slot is the Fill/Fill PIN AREA (the single output pin overlay-filled across the whole pill — the
 * visible border ring is the wire-drag surface) and whose SECOND slot is the centered content plate
 * (Graph.StateNode.ColorSpill) carrying the label. Grabbing the label bubbles to SGraphPanel = node
 * select + move; grabbing the ring starts the aiming wire.
 *
 * MoveTo contract: identical to the move node's (Super always; persist ONLY on bMarkDirty=true —
 * the one engine-transacted call per drag/nudge), routed through the graph-owned
 * OnChainStartMarkerMoveCommitted delegate so the panel can record TARGETED markers' positions in
 * its session position map (floating markers' only position is the live node).
 */
class SAnimationMapChainStartNode : public SGraphNode
{
public:
	SLATE_BEGIN_ARGS(SAnimationMapChainStartNode) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UPaper2DPlusAnimationMapNode_ChainStart* InNode);

	//~ Begin SNodePanel::SNode interface
	// FAnimationMapChainStartNodeVector = FVector2f on 5.6+, FVector2D before (the sweep guard above).
	virtual void MoveTo(const FAnimationMapChainStartNodeVector& NewPosition, FNodeSet& NodeFilter, bool bMarkDirty = true) override;
	//~ End SNodePanel::SNode interface

protected:
	//~ Begin SGraphNode interface
	/** The compact pill build (see the class comment) — fully replaces the base default-node shape. */
	virtual void UpdateGraphNode() override;
	/** The single output pin as the full-body ring pin; no input pin exists. */
	virtual void CreatePinWidgets() override;
	/** Slots the pin HAlign_Fill/VAlign_Fill into the full-pill PinOverlay (move-node clone). */
	virtual void AddPin(const TSharedRef<SGraphPin>& PinToAdd) override;
	//~ End SGraphNode interface

private:
	/** The full-pill pin layer (the move node's PinOverlay clone, one output pin only). */
	TSharedPtr<SOverlay> PinOverlay;
};
