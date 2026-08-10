// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraphSchema.h"
#include "Paper2DPlusClashGraphSchema.generated.h"

/**
 * Schema for the transient clash graph (TASK-77 U5). A clash edge is metadata-free, so a "Winner beats
 * Loser" relationship is a PLAIN PIN WIRE (Winner OUTPUT -> Loser INPUT) — there is no edge-as-node and no
 * conversion node (the Animation Map schema's CreateAutomaticConversionNodeAndConnections path is dropped).
 *
 * Schema methods run on the CDO and hold NO state: every write-through routes to the panel through the
 * delegates owned by the GRAPH object (UPaper2DPlusClashGraph). The schema never mutates the asset and
 * never spawns nodes itself.
 *
 * WIRE CREATE (HIGH #1): CanCreateConnection returns CONNECT_RESPONSE_MAKE for two DISTINCT tag nodes;
 * TryCreateConnection normalizes the direction, then fires OnWireCreateRequested(Winner, Loser) — and the
 * panel-bound handler appends the FClashEdge AND makes the visual pin link DIRECTLY (then sets LastProjection
 * so the deferred Modify echo diffs empty and the wire survives). The schema does NOT call Super::
 * TryCreateConnection — the handler owns the link.
 *
 * WIRE BREAK (HIGH #2): all three Break* overrides early-out to Super while a reconcile is in flight
 * (bRebuildInProgress — the reconcile tears wires via raw Pin->BreakAllPinLinks(), never through the schema).
 * For a genuine USER break they fire OnWireRemoveRequested per affected link (the panel removes the row +
 * RequestReconcile without touching LastProjection, so the rebuild cleans up the orphaned wire).
 */
UCLASS()
class UPaper2DPlusClashGraphSchema : public UEdGraphSchema
{
	GENERATED_BODY()

public:
	//~ Begin UEdGraphSchema interface
	virtual const FPinConnectionResponse CanCreateConnection(const UEdGraphPin* PinA, const UEdGraphPin* PinB) const override;
	virtual bool TryCreateConnection(UEdGraphPin* PinA, UEdGraphPin* PinB) const override;
	virtual FLinearColor GetPinTypeColor(const FEdGraphPinType& PinType) const override;

	/** REQUIRED false: the base-class true makes every NotifyNodeChanged purge ALL node widgets, which would
	 *  defeat the reconcile's full-diff early-out (surviving widgets/selection must stay alive). */
	virtual bool ShouldAlwaysPurgeOnModification() const override { return false; }

	// Wire-break hooks (HIGH #2) — gate on bRebuildInProgress -> Super; else fire the remove delegate + defer.
	virtual void BreakSinglePinLink(UEdGraphPin* SourcePin, UEdGraphPin* TargetPin) const override;
	virtual void BreakPinLinks(UEdGraphPin& TargetPin, bool bSendsNodeNotification) const override;
	virtual void BreakNodeLinks(UEdGraphNode& TargetNode) const override;
	//~ End UEdGraphSchema interface
};
