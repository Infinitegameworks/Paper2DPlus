// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraphNode.h"
#include "Paper2DPlusAnimationMapNode_ChainStart.generated.h"

/**
 * The "Chain Start" MARKER node (chain-start rework — replaces the numbered-root R# badge as the
 * authoring surface). A small standalone node, PaperZD-jump-style: its single OUTGOING wire
 * designates the wired move as the exact group's combo-chain opener (the mapping entry's
 * bIsChainStart flag on FFlipbookTagMappingEntry — the flat data stays the single source of truth;
 * this node is pure projection/gesture surface, exactly like the move/transition nodes).
 *
 * Two states, keyed by TargetMoveLower:
 *  - TARGETED (non-empty): mirrors an authored bIsChainStart flag. The panel's reconcile sync owns
 *    its lifetime wholesale — exactly one wired marker per flagged move; a marker whose target move
 *    vanished or lost its flag is removed by the sync.
 *  - FLOATING (empty): un-aimed authoring intent (spawned from the empty-canvas "Add Chain Start"
 *    action, or left behind by a wire break). SESSION-TRANSIENT by design — the reconcile sync NEVER
 *    removes a floating marker; only the user deletes it.
 *
 * Spawned exclusively through UPaper2DPlusAnimationMap::SpawnNodeUntransactional (the
 * ClearFlags-before-MakeLinkTo zombie invariant — the panel's sync and the wire gesture both perform
 * link surgery after the funnel cleared RF_Transactional).
 *
 * Fields are bare UPROPERTY() — display-only reflection, nothing BP/editor-exposed, so the
 * installed-UHT Category rule (PR #134 Fab gotcha) cannot trip.
 */
UCLASS()
class UPaper2DPlusAnimationMapNode_ChainStart : public UEdGraphNode
{
	GENERATED_BODY()

public:
	/** LOWERED name of the move this marker designates. EMPTY = floating/unaimed marker. */
	UPROPERTY()
	FString TargetMoveLower;

	//~ Begin UEdGraphNode interface
	/** One OUTPUT pin only (the marker aims AT a move; nothing ever wires INTO a marker). Same
	 *  category/creation shape as the move node's pins. */
	virtual void AllocateDefaultPins() override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FLinearColor GetNodeTitleColor() const override;
	virtual bool CanUserDeleteNode() const override { return true; }
	virtual bool CanDuplicateNode() const override { return false; }
	/** The compact pill widget (SAnimationMapChainStartNode) — resolved FIRST by
	 *  FNodeFactory::CreateNodeWidget (NodeFactory.cpp:88-98), so no global factory registration. */
	virtual TSharedPtr<SGraphNode> CreateVisualWidget() override;
	//~ End UEdGraphNode interface

	/** The single output pin. Null-safe lookup by direction (never by index). */
	UEdGraphPin* GetOutputPin() const;
};
