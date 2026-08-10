// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "ClashGraph/SClashGraphNode_Tag.h"

#include "ClashGraph/Paper2DPlusClashGraph.h"
#include "ClashGraph/Paper2DPlusClashGraphNode_Tag.h"

#define LOCTEXT_NAMESPACE "SClashGraphNode_Tag"

void SClashGraphNode_Tag::Construct(const FArguments& InArgs, UPaper2DPlusClashGraphNode_Tag* InNode)
{
	GraphNode = InNode;
	SetCursor(EMouseCursor::CardinalCross);
	UpdateGraphNode(); // the stock SGraphNode shape (title + standard pins)
}

void SClashGraphNode_Tag::MoveTo(const FClashGraphNodeVector& NewPosition, FNodeSet& NodeFilter, bool bMarkDirty)
{
	// Super ALWAYS: the live drag preview is NodePosX/Y-driven, and the base's GraphNode->Modify(bMarkDirty)
	// records nothing — the node was spawned non-transactional through SpawnNodeUntransactional (the asset
	// stays the only transacted object).
	SGraphNode::MoveTo(NewPosition, NodeFilter, bMarkDirty);

	// Persist ONLY when bMarkDirty is true: false = the engine's per-mouse-move calls (no transaction open —
	// writing here would be a bare untransacted Modify per mouse-move) AND FinalizeNodeMovements' restore
	// pass (persisting it would clobber the gesture's final position with the original).
	if (!bMarkDirty)
	{
		return;
	}

	UPaper2DPlusClashGraphNode_Tag* TagNode = Cast<UPaper2DPlusClashGraphNode_Tag>(GraphNode);
	if (!TagNode)
	{
		return;
	}
	// Seam: widget -> node -> owning UPaper2DPlusClashGraph -> panel-bound OnNodeMoveCommitted (the same
	// graph-owned delegate seam the schema's wire hooks use). Suppressed during rebuilds — the reconcile
	// reapplies positions by DIRECT NodePosX/Y assignment, never MoveTo, so a fire mid-rebuild is a foreign
	// caller; fail safe and skip.
	UPaper2DPlusClashGraph* ClashGraph = Cast<UPaper2DPlusClashGraph>(TagNode->GetGraph());
	if (!ClashGraph || ClashGraph->bRebuildInProgress)
	{
		return;
	}
	ClashGraph->OnNodeMoveCommitted.ExecuteIfBound(TagNode);
}

#undef LOCTEXT_NAMESPACE
