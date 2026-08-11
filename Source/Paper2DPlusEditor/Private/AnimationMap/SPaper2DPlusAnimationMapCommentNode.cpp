// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "AnimationMap/SPaper2DPlusAnimationMapCommentNode.h"

#include "AnimationMap/Paper2DPlusAnimationMap.h"
#include "EdGraphNode_Comment.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"

void SPaper2DPlusAnimationMapCommentNode::Construct(const FArguments& /*InArgs*/, UEdGraphNode_Comment* InNode)
{
	SGraphNodeComment::Construct(SGraphNodeComment::FArguments(), InNode);
}

void SPaper2DPlusAnimationMapCommentNode::MoveTo(const FAnimationMapCommentMoveVector& NewPosition, FNodeSet& NodeFilter, bool bMarkDirty)
{
	// Super ALWAYS: live drag visuals + contained-node dragging (SGraphNodeComment drags the move nodes
	// inside the box; those persist via their own SAnimationMapMoveNode::MoveTo). The comment node is
	// non-transactional (spawn-funnel invariant), so the base's Modify records nothing.
	SGraphNodeComment::MoveTo(NewPosition, NodeFilter, bMarkDirty);

	// Persist ONLY on bMarkDirty=true — same contract as the move node: false = per-mouse-move + the
	// FinalizeNodeMovements restore pass (no transaction open), true = the single in-transaction finalize/
	// nudge call per gesture.
	if (!bMarkDirty)
	{
		return;
	}
	NotifyCommentChanged();
}

FReply SPaper2DPlusAnimationMapCommentNode::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	// bUserIsDragging is SGraphNodeResizable's resize-gesture flag. Super consumes it on the ending
	// mouse-up, performs the final ResizeNode(UserSize), and closes the engine transaction. Capture the
	// state before Super and persist the final size afterward, once per gesture.
	const bool bWasResizing = bUserIsDragging;
	const FReply Reply = SGraphNodeComment::OnMouseButtonUp(MyGeometry, MouseEvent);
	if (bWasResizing && !bUserIsDragging)
	{
		NotifyCommentChanged();
	}
	return Reply;
}

void SPaper2DPlusAnimationMapCommentNode::Tick(
	const FGeometry& AllottedGeometry,
	const double InCurrentTime,
	const float InDeltaTime)
{
	SGraphNodeComment::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	// SGraphNode::OnNameTextCommited is non-virtual and bound by the base widget, so the reliable hook is
	// the inline editor leaving edit mode. Escape-cancel also lands here; the panel's no-change guard
	// absorbs it without creating an undo step.
	const bool bInEditMode = InlineEditableText.IsValid() && InlineEditableText->IsInEditMode();
	if (bWasTitleInEditMode && !bInEditMode)
	{
		NotifyCommentChanged();
	}
	bWasTitleInEditMode = bInEditMode;
}

void SPaper2DPlusAnimationMapCommentNode::NotifyCommentChanged()
{
	UEdGraphNode_Comment* CommentNode = Cast<UEdGraphNode_Comment>(GraphNode);
	if (!CommentNode)
	{
		return;
	}
	UPaper2DPlusAnimationMap* AnimationMap = Cast<UPaper2DPlusAnimationMap>(CommentNode->GetGraph());
	if (!AnimationMap || AnimationMap->bRebuildInProgress)
	{
		return;
	}
	AnimationMap->OnCommentChanged.ExecuteIfBound(CommentNode);
}

TSharedPtr<SGraphNode> FPaper2DPlusAnimationMapCommentNodeFactory::CreateNode(UEdGraphNode* Node) const
{
	UEdGraphNode_Comment* CommentNode = Cast<UEdGraphNode_Comment>(Node);
	if (CommentNode && CommentNode->GetGraph() && CommentNode->GetGraph()->IsA<UPaper2DPlusAnimationMap>())
	{
		return SNew(SPaper2DPlusAnimationMapCommentNode, CommentNode);
	}
	return nullptr;
}
