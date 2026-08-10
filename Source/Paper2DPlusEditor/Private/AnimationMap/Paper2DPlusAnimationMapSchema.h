// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraphSchema.h"
#include "Misc/EngineVersionComparison.h" // 5.6 FVector2f sweep guard for the schema-action PerformAction
#include "Paper2DPlusAnimationMapSchema.generated.h"

/**
 * Empty-canvas "Add Chain Start" action (chain-start rework): spawns a FLOATING Chain Start marker
 * at the click position, routed through the graph-owned OnAddChainStartRequested delegate (the
 * schema/action layer never spawns nodes or holds state itself — the panel owns the funnel). Only
 * offered by GetGraphContextActions when the graph carries an exact group scope.
 *
 * PerformAction is the 5.6 FVector2f sweep's third instance in this cluster (MoveTo/view-location
 * precedents): 5.6+ must override the FVector2f& form (the FVector2D form is deprecated-final under
 * -WarningsAsErrors), 5.0-5.5 have ONLY the by-value FVector2D form. One shared body.
 */
struct FAnimationMapSchemaAction_AddChainStart : public FEdGraphSchemaAction
{
	FAnimationMapSchemaAction_AddChainStart();

#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6)
	virtual UEdGraphNode* PerformAction(class UEdGraph* ParentGraph, UEdGraphPin* FromPin,
		const FVector2f& Location, bool bSelectNewNode = true) override
	{
		return PerformAddChainStart(ParentGraph, FromPin, FVector2D(Location));
	}
#else
	virtual UEdGraphNode* PerformAction(class UEdGraph* ParentGraph, UEdGraphPin* FromPin,
		const FVector2D Location, bool bSelectNewNode = true) override
	{
		return PerformAddChainStart(ParentGraph, FromPin, Location);
	}
#endif

private:
	/** The one shared body: route to the graph's panel-bound delegate (nullptr when unbound/refused). */
	static UEdGraphNode* PerformAddChainStart(class UEdGraph* ParentGraph, UEdGraphPin* FromPin,
		const FVector2D& Location);
};

/**
 * Empty-canvas "Add Chain End" action (chain-end rework — the AddChainStart action's MIRROR): spawns
 * a FLOATING Chain End marker at the click position, routed through the graph-owned
 * OnAddChainEndRequested delegate (the schema/action layer never spawns nodes or holds state itself
 * — the panel owns the funnel). Only offered by GetGraphContextActions when the graph carries an
 * exact group scope. Same 5.6 FVector2f sweep guard as the start action (one shared body).
 */
struct FAnimationMapSchemaAction_AddChainEnd : public FEdGraphSchemaAction
{
	FAnimationMapSchemaAction_AddChainEnd();

#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6)
	virtual UEdGraphNode* PerformAction(class UEdGraph* ParentGraph, UEdGraphPin* FromPin,
		const FVector2f& Location, bool bSelectNewNode = true) override
	{
		return PerformAddChainEnd(ParentGraph, FromPin, FVector2D(Location));
	}
#else
	virtual UEdGraphNode* PerformAction(class UEdGraph* ParentGraph, UEdGraphPin* FromPin,
		const FVector2D Location, bool bSelectNewNode = true) override
	{
		return PerformAddChainEnd(ParentGraph, FromPin, Location);
	}
#endif

private:
	/** The one shared body: route to the graph's panel-bound delegate (nullptr when unbound/refused). */
	static UEdGraphNode* PerformAddChainEnd(class UEdGraph* ParentGraph, UEdGraphPin* FromPin,
		const FVector2D& Location);
};

/**
 * Schema for the transient Animation Map (combo-graph plan U3) — the anim-state-machine shape minus the
 * entry node (no entry node exists, which deletes the engine's entry-pin special cases).
 *
 * Schema methods run on the CDO and hold NO state: every write-through routes to the panel through
 * the delegates owned by the GRAPH object (UPaper2DPlusAnimationMap) — the schema never mutates the
 * asset and never spawns nodes itself.
 *
 * CreateConnectionDrawingPolicy registers the MINIMAL U4 drawing policy
 * (FAnimationMapConnectionDrawingPolicy — geometry substitution so hidden-pin wires render); U6 grows it
 * into the full state-machine clone with per-index wire offsets.
 */
UCLASS()
class UPaper2DPlusAnimationMapSchema : public UEdGraphSchema
{
	GENERATED_BODY()

public:
	//~ Begin UEdGraphSchema interface
	virtual const FPinConnectionResponse CanCreateConnection(const UEdGraphPin* PinA, const UEdGraphPin* PinB) const override;
	virtual bool TryCreateConnection(UEdGraphPin* PinA, UEdGraphPin* PinB) const override;
	virtual bool CreateAutomaticConversionNodeAndConnections(UEdGraphPin* PinA, UEdGraphPin* PinB) const override;

	/** Chain-start/-end rework: adds "Add Chain Start" and "Add Chain End" when the graph carries an
	 *  exact group scope. Empty-canvas actions spawn floating markers; a menu opened by dragging from
	 *  a real move card offers both choices and immediately links the chosen marker to that move. */
	virtual void GetGraphContextActions(FGraphContextMenuBuilder& ContextMenuBuilder) const override;

	/** Chain-start/-end rework: break gestures touching a Chain Start or Chain End marker's wire
	 *  route to the panel via the graph-owned OnChainStartUnlinkRequested /
	 *  OnChainEndUnlinkRequested (clear the flag + leave the marker floating) — the
	 *  bRebuildInProgress-gated hook pattern; non-marker links keep the stock Super behavior (the
	 *  transition suicide hook's cleanup path is unchanged). */
	virtual void BreakNodeLinks(UEdGraphNode& TargetNode) const override;
	virtual void BreakPinLinks(UEdGraphPin& TargetPin, bool bSendsNodeNotifcation) const override;
	virtual void BreakSinglePinLink(UEdGraphPin* SourcePin, UEdGraphPin* TargetPin) const override;
	virtual class FConnectionDrawingPolicy* CreateConnectionDrawingPolicy(int32 InBackLayerID, int32 InFrontLayerID,
		float InZoomFactor, const FSlateRect& InClippingRect, class FSlateWindowElementList& InDrawElements,
		class UEdGraph* InGraphObj) const override;

	/** Clone of UAnimationStateMachineSchema::GetPinTypeColor (AnimationStateMachineSchema.cpp:460-467):
	 *  the full-body node pins (SAnimationMapNodePin, U6) tint their hover-cue brush via
	 *  SGraphPin::GetPinColor -> Schema->GetPinTypeColor, and the BASE UEdGraphSchema default is BLACK
	 *  (EdGraphSchema.h:964) — without this override the border hover highlight renders invisible. */
	virtual FLinearColor GetPinTypeColor(const FEdGraphPinType& PinType) const override;

	/** REQUIRED false (KTD): the base-class true makes every NotifyNodeChanged purge ALL node widgets,
	 *  which would defeat U4's hybrid in-place diff (field-only updates must keep surviving widgets,
	 *  selection, and in-flight references alive). */
	virtual bool ShouldAlwaysPurgeOnModification() const override { return false; }
	//~ End UEdGraphSchema interface
};
