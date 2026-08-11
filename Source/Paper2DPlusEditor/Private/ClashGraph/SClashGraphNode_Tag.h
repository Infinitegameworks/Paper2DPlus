// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/EngineVersionComparison.h" // ENGINE_*_VERSION macros for the cross-version MoveTo guard
#include "SGraphNode.h"

class UPaper2DPlusClashGraphNode_Tag;

/** 5.6 FVector2f sweep guard (clone of FAnimationMapMoveNodeVector): SNodePanel::SNode::MoveTo takes
 *  FVector2D through 5.5 and FVector2f from 5.6, where the FVector2D virtual is `final` under
 *  -WarningsAsErrors — so each version must override EXACTLY its native signature. */
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6)
using FClashGraphNodeVector = FVector2f;
#else
using FClashGraphNodeVector = FVector2D;
#endif

/**
 * The clash-category node widget (TASK-77 U5). Deliberately MINIMAL — it keeps the stock SGraphNode shape
 * (title bar + standard left/right pins; no whole-body pins, which the plan drops for the clash graph) and
 * adds ONLY the bMarkDirty-gated MoveTo override that persists node positions through the graph-owned
 * OnNodeMoveCommitted seam (the same contract as SAnimationMapMoveNode::MoveTo).
 *
 * MoveTo contract (verified vs SAnimationMapMoveNode): Super ALWAYS runs (live drag visuals; the node is
 * non-transactional by the spawn-funnel invariant). The position persist fires ONLY on bMarkDirty=true,
 * which the engine guarantees is in-transaction and exactly once per gesture (drag finalize / arrow nudge).
 */
class SClashGraphNode_Tag : public SGraphNode
{
public:
	SLATE_BEGIN_ARGS(SClashGraphNode_Tag) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UPaper2DPlusClashGraphNode_Tag* InNode);

	//~ Begin SNodePanel::SNode interface
	// FClashGraphNodeVector = FVector2f on 5.6+, FVector2D before (the 5.6 sweep guard above).
	virtual void MoveTo(const FClashGraphNodeVector& NewPosition, FNodeSet& NodeFilter, bool bMarkDirty = true) override;
	//~ End SNodePanel::SNode interface
};
