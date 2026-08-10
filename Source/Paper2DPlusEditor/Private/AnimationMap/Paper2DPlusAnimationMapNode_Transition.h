// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraphNode.h"
#include "GameplayTagContainer.h"
#include "Paper2DPlusMoveTransition.h"
#include "Paper2DPlusAnimationMapNode_Transition.generated.h"

class UPaper2DPlusAnimationMapNode_Move;

/**
 * One transition ROW rendered as an edge node (combo-graph plan U3) — the engine's edges-with-
 * properties answer, cloned from UAnimStateTransitionNode: two HIDDEN pins wired
 * From.Out -> this.In and this.Out -> To.In, so the row is individually selectable on the wire.
 *
 * Identity is (FromMove, TargetMove) since TASK-108 U2 — one row per pair by construction (U1
 * load-time dedupe + the wire funnel's duplicate refusal). RowIndex survives as a CONVENIENCE
 * HANDLE (probe output, descending-per-move delete ordering, the write-side row address),
 * re-stamped on every reconcile; the one-tick deferral window is closed by the WRITE-SIDE guard —
 * every write-through validates IsValidIndex + snapshot-equals-row before mutating, else aborts
 * the gesture and reconciles (KTD: node identity).
 *
 * Fields are bare UPROPERTY() — display-only reflection, nothing BP/editor-exposed (installed-UHT
 * Category rule, PR #134). Spawned exclusively through
 * UPaper2DPlusAnimationMap::SpawnNodeUntransactional (the ClearFlags-before-MakeLinkTo invariant —
 * CreateConnections performs link surgery, so the flag MUST already be cleared when it runs).
 */
UCLASS()
class UPaper2DPlusAnimationMapNode_Transition : public UEdGraphNode
{
	GENERATED_BODY()

public:
	/** Owning move's name (display case) — the FROM half of the row handle. */
	UPROPERTY()
	FString FromMove;

	/** Index into the from-move's TransitionData.Transitions — re-stamped on every reconcile. NOT
	 *  identity since U2 (that is (FromMove, TargetMove)); kept as the row ADDRESS for the delete
	 *  funnel + the probe's row= field. */
	UPROPERTY()
	int32 RowIndex = INDEX_NONE;

	// --- Denormalized row snapshot (the write-side guard's expected value) ---

	/** Snapshot: the row's target move name, in the row's authored case. (The Label/Condition/
	 *  CancelCategory snapshot fields died with the runtime fields, TASK-108 U1.) */
	UPROPERTY()
	FString TargetMove;

	/** Display-only denormalization of the transition's effective phase. A row override wins; an empty
	 *  override inherits the target animation phase. Deliberately excluded from MakeRowSnapshot so
	 *  phase-only edits re-stamp the existing pill without invalidating identity-based gestures. */
	UPROPERTY()
	FGameplayTag TransitionPhaseTag;

	//~ Begin UEdGraphNode interface
	virtual void AllocateDefaultPins() override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual bool CanUserDeleteNode() const override { return true; }
	virtual bool CanDuplicateNode() const override { return false; }
	virtual void PinConnectionListChanged(UEdGraphPin* Pin) override;
	/** U4: the minimal edge-pill widget (SAnimationMapTransitionNode) — resolved FIRST by
	 *  FNodeFactory::CreateNodeWidget (NodeFactory.cpp:88-98), so no global factory registration. */
	virtual TSharedPtr<SGraphNode> CreateVisualWidget() override;
	//~ End UEdGraphNode interface

	/** Wire this edge between two move nodes: empty both LinkedTo lists, then
	 *  From.Out -> this.In and this.Out -> To.In (clone of AnimStateTransitionNode.cpp:340-355).
	 *  Self-transitions (FromNode == ToNode) are legal (R14). No-op when either side (or the from
	 *  node's output pin — stubs have none) is missing. */
	void CreateConnections(UPaper2DPlusAnimationMapNode_Move* FromNode, UPaper2DPlusAnimationMapNode_Move* ToNode);

	/** The move node feeding this edge (via the hidden input pin's link), or nullptr. */
	UPaper2DPlusAnimationMapNode_Move* GetPreviousMoveNode() const;

	/** The move node this edge points at (via the hidden output pin's link), or nullptr. */
	UPaper2DPlusAnimationMapNode_Move* GetNextMoveNode() const;

	/** Hidden input pin (Pins[0]) — null-safe. */
	UEdGraphPin* GetInputPin() const { return Pins.IsValidIndex(0) ? Pins[0] : nullptr; }

	/** Hidden output pin (Pins[1]) — null-safe. */
	UEdGraphPin* GetOutputPin() const { return Pins.IsValidIndex(1) ? Pins[1] : nullptr; }

	/** Stamp the handle + denormalized snapshot from a row (reconcile and wire-create both use this).
	 *  InTransitionPhaseTag is the effective phase (display-only here, not part of the identity
	 *  snapshot); defaulted empty for callers/tests that only need the row fields. */
	void SetFromRow(const FString& InFromMove, int32 InRowIndex, const FPaper2DPlusMoveTransition& Row,
		const FGameplayTag& InTransitionPhaseTag = FGameplayTag());

	/** The snapshot as a row value — what the write-side guard compares against the live row. */
	FPaper2DPlusMoveTransition MakeRowSnapshot() const;
};
