// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraphNode.h"
#include "GameplayTagContainer.h"
#include "Paper2DPlusClashGraphNode_Tag.generated.h"

/**
 * One clash-category node in the transient clash-graph projection (TASK-77 U5).
 *
 * Identity is the CATEGORY TAG. A node exists because an edge mentions its tag OR a TagNodePositions key
 * places it (an "Add Category" node with zero edges). The clash edge is metadata-free, so a "Winner beats
 * Loser" relationship is a PLAIN PIN WIRE (output -> input) — there is no edge-as-node here (the ~60% of the
 * Animation Map surface this drops). Spawned exclusively through
 * UPaper2DPlusClashGraph::SpawnNodeUntransactional (the ClearFlags ordering invariant).
 *
 * Fields are bare UPROPERTY() — display-only reflection, nothing BP/editor-exposed, so the installed-UHT
 * Category rule (PR #134 Fab gotcha) cannot trip.
 */
UCLASS()
class UPaper2DPlusClashGraphNode_Tag : public UEdGraphNode
{
	GENERATED_BODY()

public:
	/** The clash category this node represents (the node's identity). */
	UPROPERTY()
	FGameplayTag CategoryTag;

	//~ Begin UEdGraphNode interface
	virtual void AllocateDefaultPins() override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FLinearColor GetNodeTitleColor() const override;
	virtual bool CanUserDeleteNode() const override { return true; }
	virtual bool CanDuplicateNode() const override { return false; }
	/** The node widget (SClashGraphNode_Tag): the base SGraphNode shape (title + standard input/output
	 *  pins) plus the bMarkDirty-gated MoveTo override (position persistence). Resolved FIRST by
	 *  FNodeFactory::CreateNodeWidget, so no global factory registration. */
	virtual TSharedPtr<SGraphNode> CreateVisualWidget() override;
	//~ End UEdGraphNode interface

	/** The INPUT pin = "beaten by" (the Loser side of incoming wires). Null-safe lookup by direction. */
	UEdGraphPin* GetInputPin() const;

	/** The OUTPUT pin = "beats" (the Winner side of outgoing wires). Null-safe lookup by direction. */
	UEdGraphPin* GetOutputPin() const;

	/** Short label = the tag string with the "Paper2DPlus.Clash.Category." prefix stripped (e.g.
	 *  "Strike.Heavy"). Unambiguous (unlike the bare leaf) and concise. Falls back to the full string. */
	FString GetShortName() const;
};
