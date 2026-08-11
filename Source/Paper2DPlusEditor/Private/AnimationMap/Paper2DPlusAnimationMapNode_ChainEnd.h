// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraphNode.h"
#include "Paper2DPlusAnimationMapNode_ChainEnd.generated.h"

/**
 * The "Chain End" MARKER node — the Chain Start marker's MIRROR (chain-end rework). A small
 * standalone node whose single INCOMING wire designates the wired move as the exact group's combo
 * main-line END (the mapping entry's bIsChainEnd flag on FFlipbookTagMappingEntry — the runtime spine
 * derivation prefers paths terminating at a flagged end and never walks past one, so trailing
 * recovery animations are excluded from combo indexing). The flat data stays the single source of
 * truth; this node is pure projection/gesture surface, exactly like the Chain Start marker.
 *
 * MIRRORED PIN CONTRACT: where the start marker aims OUT at a move (marker output -> move input),
 * the end marker is aimed AT (move output -> marker input) — a chain flows INTO its end.
 *
 * Two states, keyed by TargetMoveLower (identical to the start marker's contract):
 *  - TARGETED (non-empty): mirrors an authored bIsChainEnd flag. The panel's reconcile sync owns
 *    its lifetime wholesale — exactly one wired marker per flagged move; a marker whose target move
 *    vanished or lost its flag is removed by the sync.
 *  - FLOATING (empty): un-aimed authoring intent (spawned from the empty-canvas "Add Chain End"
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
class UPaper2DPlusAnimationMapNode_ChainEnd : public UEdGraphNode
{
	GENERATED_BODY()

public:
	/** LOWERED name of the move this marker designates. EMPTY = floating/unaimed marker. */
	UPROPERTY()
	FString TargetMoveLower;

	//~ Begin UEdGraphNode interface
	/** One INPUT pin only (a move's output wires INTO the marker; nothing ever wires OUT of it — the
	 *  start marker's mirror). Same category/creation shape as the move node's pins. */
	virtual void AllocateDefaultPins() override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FLinearColor GetNodeTitleColor() const override;
	virtual bool CanUserDeleteNode() const override { return true; }
	virtual bool CanDuplicateNode() const override { return false; }
	/** The compact pill widget (SAnimationMapChainEndNode) — resolved FIRST by
	 *  FNodeFactory::CreateNodeWidget (NodeFactory.cpp:88-98), so no global factory registration. */
	virtual TSharedPtr<SGraphNode> CreateVisualWidget() override;
	//~ End UEdGraphNode interface

	/** The single input pin. Null-safe lookup by direction (never by index). */
	UEdGraphPin* GetInputPin() const;
};
