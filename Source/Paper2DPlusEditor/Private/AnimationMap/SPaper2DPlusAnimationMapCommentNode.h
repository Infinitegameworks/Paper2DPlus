// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/EngineVersionComparison.h" // ENGINE_*_VERSION for the cross-version MoveTo guard
#include "EdGraphUtilities.h"
#include "SGraphNodeComment.h"

class UEdGraphNode_Comment;

/** 5.6 FVector2f sweep guard for SNodePanel::SNode::MoveTo (mirror SAnimationMapMoveNode.h). */
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6)
using FAnimationMapCommentMoveVector = FVector2f;
#else
using FAnimationMapCommentMoveVector = FVector2D;
#endif

/**
 * Comment-box widget for the Animation Map (comment boxes feature). Subclasses the engine
 * SGraphNodeComment so the visual + the contained-node drag behavior are native.
 *
 * The NODE is the plain engine UEdGraphNode_Comment, not a plugin subclass. UEdGraphNode_Comment is
 * MinimalAPI on UE 5.0-5.4 (its FObjectInitializer ctor / PostEditChangeProperty / IsSelectedInEditor
 * are unexported, and any subclass vtable references them), so a node subclass links only on 5.5+.
 * All write-through therefore lives here on the widget (fully linkable on every supported engine)
 * plus one panel-side property hook:
 *  - POSITION: MoveTo persists on the bMarkDirty commit (the SAnimationMapMoveNode contract: Super
 *    always for live visuals + contained-node drag, persist only on the in-transaction commit call).
 *  - RESIZE:   OnMouseButtonUp persists once per resize gesture (bUserIsDragging falling edge; Super
 *    performs the final ResizeNode and ends the engine resize transaction first).
 *  - TEXT:     the title-commit path is non-virtual and delegate-bound by Super's UpdateGraphNode, so
 *    Tick detects the inline editable's edit-mode falling edge. Escape also ends edit mode; the panel
 *    no-ops when nothing actually changed.
 *  - COLOR:    the Details panel has no widget gesture, so SAnimationMapPanel subscribes to
 *    OnObjectPropertyChanged.
 */
class SPaper2DPlusAnimationMapCommentNode : public SGraphNodeComment
{
public:
	SLATE_BEGIN_ARGS(SPaper2DPlusAnimationMapCommentNode) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UEdGraphNode_Comment* InNode);

protected:
	//~ Begin SNodePanel::SNode interface (MoveTo is protected on SGraphNodeComment)
	// FAnimationMapCommentMoveVector = FVector2f on 5.6+, FVector2D before (the sweep guard above).
	virtual void MoveTo(const FAnimationMapCommentMoveVector& NewPosition, FNodeSet& NodeFilter, bool bMarkDirty = true) override;
	//~ End SNodePanel::SNode interface

	//~ Begin SWidget interface
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	//~ End SWidget interface

private:
	/** Fire the graph's OnCommentChanged (suppressed during a reconcile/rebuild). */
	void NotifyCommentChanged();

	/** Title inline-edit state last Tick; the falling edge is the text write-through trigger. */
	bool bWasTitleInEditMode = false;
};

/**
 * Plain UEdGraphNode_Comment nodes inside a UPaper2DPlusAnimationMap receive the write-through widget.
 * A registered visual factory is the cross-version-safe attachment seam because the engine node has no
 * CreateVisualWidget override. Registered and unregistered by FPaper2DPlusEditorModule; fast-rejects
 * every non-comment or non-Animation Map node.
 */
struct FPaper2DPlusAnimationMapCommentNodeFactory : public FGraphPanelNodeFactory
{
	virtual TSharedPtr<SGraphNode> CreateNode(UEdGraphNode* Node) const override;
};
