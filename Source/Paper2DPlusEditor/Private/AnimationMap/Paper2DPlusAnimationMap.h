// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraph.h"
#include "GameplayTagContainer.h"
#include "Paper2DPlusAnimationMap.generated.h"

class UPaper2DPlusAnimationMapNode_ChainEnd;
class UPaper2DPlusAnimationMapNode_ChainStart;
class UPaper2DPlusAnimationMapNode_Move;
class UPaper2DPlusAnimationMapNode_Transition;
class UEdGraphNode_Comment;

/** Wire-create request (combo-graph plan U3). Fired by the schema's
 *  CreateAutomaticConversionNodeAndConnections with the resolved FROM/TO move display names. The
 *  panel-bound handler (U4) owns the whole gesture: it appends the transition row through the
 *  write-through funnel AND spawns/links the edge node. Returns true when the gesture was handled —
 *  false (or unbound) means NO connection is made and the schema returns false to the engine. */
DECLARE_DELEGATE_RetVal_TwoParams(bool, FPaper2DPlusComboWireCreateDelegate,
	const FString& /*FromMoveName*/, const FString& /*ToMoveName*/);

/** Replace an existing transition's target endpoint in place. The panel validates the edge's live
 *  row snapshot and duplicate-pair invariant before atomically changing data + links. */
DECLARE_DELEGATE_RetVal_TwoParams(bool, FPaper2DPlusTransitionTargetRewireDelegate,
	UPaper2DPlusAnimationMapNode_Transition* /*EdgeNode*/, const FString& /*NewTargetMoveName*/);

/** Edge-removal request (combo-graph plan U3). Fired by the transition node's suicide hook
 *  (PinConnectionListChanged with an empty pin) — CLEANUP-ONLY by KTD: the handler may destroy the
 *  orphaned edge WIDGET/node and request a reconcile, but row deletion enters EXCLUSIVELY via the
 *  panel's delete helper (snapshot-validated), never from here. Identical duplicate rows (legal, R14)
 *  would otherwise be double-deleted by a re-entrant suicide cascade. */
DECLARE_DELEGATE_OneParam(FPaper2DPlusComboEdgeRemovalRequestDelegate,
	UPaper2DPlusAnimationMapNode_Transition* /*EdgeNode*/);

/** Node-move commit (combo-graph plan U5, R11). Fired by SAnimationMapMoveNode::MoveTo EXCLUSIVELY on
 *  bMarkDirty=true calls — always inside an ENGINE-opened transaction, of which 5.7 has TWO
 *  producers: the drag finalize pass (SNodePanel::FinalizeNodeMovements's NodeMoveTransaction,
 *  SNodePanel.cpp:1906-1961) and the arrow-key nudge (SGraphPanel::UpdateSelectedNodesPositions's
 *  "Nudge Node" transaction, SGraphPanel.cpp:797-830); per-mouse-move calls pass false with no
 *  transaction open and must never reach here. The panel-bound handler performs the plan's ONE
 *  sanctioned direct Asset->Modify() (inside the ENGINE's transaction) + the position-map write. */
DECLARE_DELEGATE_OneParam(FPaper2DPlusComboNodeMoveCommittedDelegate,
	UPaper2DPlusAnimationMapNode_Move* /*MoveNode*/);

/** Comment-box change commit (comment boxes feature). The comment node is the PLAIN engine
 *  UEdGraphNode_Comment (a plugin subclass is unlinkable on 5.0-5.4 due MinimalAPI), so every gesture
 *  hook lives on SPaper2DPlusAnimationMapCommentNode (position MoveTo commit / resize mouse-up /
 *  title-edit falling edge); details-panel edits arrive via the panel's OnObjectPropertyChanged
 *  subscription. The panel-bound handler writes the matching AnimationMapComments entry (by
 *  CommentId == NodeGuid) for the currently-open group scope. Suppressed during a reconcile/rebuild
 *  like the node-move seam. */
DECLARE_DELEGATE_OneParam(FPaper2DPlusComboCommentChangedDelegate,
	UEdGraphNode_Comment* /*CommentNode*/);

/** Change-group request (TASK-108 U6, R16 — the Map node's right-click "Change Group…"). Fired by the
 *  move node's context-menu tag picker with the move NAME (captured by value, never a node pointer)
 *  and the PICKED target group tag (existing or freshly created via the
 *  picker's add-tag UI). The panel-bound handler owns the whole commit: home-group no-op guard, then
 *  ONE transaction moving the animation's TagMappings membership (removed from the current group's
 *  Entries, APPENDED at the END of the target's — first-match runtime resolution undisturbed —
 *  creating the mapping key when the tag is new) + NotifyAssetDataChanged. */
DECLARE_DELEGATE_TwoParams(FPaper2DPlusAnimMapChangeGroupDelegate,
	const FString& /*MoveName*/, const FGameplayTag& /*TargetGroupTag*/);

/** Exact phase replacement requested by a move's context menu. When the clicked move belongs to the
 *  current multi-selection, the panel applies the replacement to the whole selected real-move
 *  cohort in one transaction. An invalid tag means Clear. */
DECLARE_DELEGATE_TwoParams(FPaper2DPlusAnimMapSetPhaseDelegate,
	const FString& /*MoveName*/, const FGameplayTag& /*PhaseTag*/);

/** Exact authored AnimationTags-container replacement. Multi-selection semantics match Change Group;
 *  an empty container deliberately clears every target. */
DECLARE_DELEGATE_TwoParams(FPaper2DPlusAnimMapSetAnimationTagsDelegate,
	const FString& /*MoveName*/, const FGameplayTagContainer& /*AnimationTags*/);

/** Chain-start AIM request (chain-start rework). Fired by the schema's TryCreateConnection when a
 *  wire from a Chain Start marker's output pin lands on a real move node — INSIDE the engine's open
 *  GraphEd_CreateConnection transaction, like the move-move wire funnel. The panel-bound handler owns
 *  the whole gesture: the mapping-entry flag write (clear-old + set-new inside its transaction
 *  template) AND the marker's link surgery/retarget, in lockstep. Returns true when handled — false
 *  (or unbound) means NO connection is made and the schema returns false to the engine. */
DECLARE_DELEGATE_RetVal_TwoParams(bool, FPaper2DPlusChainStartLinkRequestedDelegate,
	UPaper2DPlusAnimationMapNode_ChainStart* /*MarkerNode*/, const FString& /*TargetMoveName*/);

/** Chain-start BREAK request (chain-start rework). Fired by the schema's Break* overrides
 *  (bRebuildInProgress-gated) for any break gesture touching a marker's wire. The panel-bound
 *  handler clears the target entry's flag in a transaction and leaves the marker FLOATING at its
 *  position; the schema does NOT run its default break for marker links (the handler owns the
 *  surgery). */
DECLARE_DELEGATE_OneParam(FPaper2DPlusChainStartUnlinkRequestedDelegate,
	UPaper2DPlusAnimationMapNode_ChainStart* /*MarkerNode*/);

/** Empty-canvas "Add Chain Start" request (chain-start rework). Fired by the schema's graph context
 *  action with the click's graph-space position; the panel-bound handler spawns a FLOATING marker
 *  through the untransactional funnel (no data change, no transaction) and returns it (nullptr on
 *  refusal — e.g. no exact group scope). */
DECLARE_DELEGATE_RetVal_OneParam(UEdGraphNode*, FPaper2DPlusChainStartAddRequestedDelegate,
	const FVector2D& /*GraphPosition*/);

/** Chain-start marker move commit (chain-start rework — the OnNodeMoveCommitted sibling). Fired by
 *  SAnimationMapChainStartNode::MoveTo EXCLUSIVELY on bMarkDirty=true calls (the engine-transacted
 *  drag-finalize/nudge, see the move-node delegate's contract). The panel-bound handler records a
 *  TARGETED marker's position in its session marker-position map under the synthetic
 *  "__chainstart__:<target>" key; floating markers keep their only position on the live node. */
DECLARE_DELEGATE_OneParam(FPaper2DPlusChainStartMarkerMovedDelegate,
	UPaper2DPlusAnimationMapNode_ChainStart* /*MarkerNode*/);

/** Chain-end AIM request (chain-end rework — the chain-start link delegate's MIRROR). Fired by the
 *  schema's TryCreateConnection when a wire from a real move node's output lands on a Chain End
 *  marker's input — INSIDE the engine's open GraphEd_CreateConnection transaction, like the
 *  move-move wire funnel. The panel-bound handler owns the whole gesture: the mapping-entry flag
 *  write (clear-old + set-new inside its transaction template) AND the marker's link
 *  surgery/retarget, in lockstep. Returns true when handled — false (or unbound) means NO
 *  connection is made and the schema returns false to the engine. */
DECLARE_DELEGATE_RetVal_TwoParams(bool, FPaper2DPlusChainEndLinkRequestedDelegate,
	UPaper2DPlusAnimationMapNode_ChainEnd* /*MarkerNode*/, const FString& /*TargetMoveName*/);

/** Chain-end BREAK request (chain-end rework). Fired by the schema's Break* overrides
 *  (bRebuildInProgress-gated) for any break gesture touching an end marker's wire. The panel-bound
 *  handler clears the target entry's flag in a transaction and leaves the marker FLOATING at its
 *  position; the schema does NOT run its default break for marker links (the handler owns the
 *  surgery). */
DECLARE_DELEGATE_OneParam(FPaper2DPlusChainEndUnlinkRequestedDelegate,
	UPaper2DPlusAnimationMapNode_ChainEnd* /*MarkerNode*/);

/** Empty-canvas "Add Chain End" request (chain-end rework). Fired by the schema's graph context
 *  action with the click's graph-space position; the panel-bound handler spawns a FLOATING marker
 *  through the untransactional funnel (no data change, no transaction) and returns it (nullptr on
 *  refusal — e.g. no exact group scope). */
DECLARE_DELEGATE_RetVal_OneParam(UEdGraphNode*, FPaper2DPlusChainEndAddRequestedDelegate,
	const FVector2D& /*GraphPosition*/);

/** Chain-end marker move commit (chain-end rework — the OnChainStartMarkerMoveCommitted sibling).
 *  Fired by SAnimationMapChainEndNode::MoveTo EXCLUSIVELY on bMarkDirty=true calls (the
 *  engine-transacted drag-finalize/nudge, see the move-node delegate's contract). The panel-bound
 *  handler records a TARGETED marker's position in its session marker-position map under the
 *  synthetic "__chainend__:<target>" key; floating markers keep their only position on the live
 *  node. */
DECLARE_DELEGATE_OneParam(FPaper2DPlusChainEndMarkerMovedDelegate,
	UPaper2DPlusAnimationMapNode_ChainEnd* /*MarkerNode*/);

/**
 * The Animation Map's transient UEdGraph (combo-graph plan U3).
 *
 * A fully transient PROJECTION over the profile asset's flat FFlipbookTransitionData — created by the
 * panel into the transient package with RF_Transient, rooted by the panel, rebuilt on reconcile, and
 * NEVER serialized (KTD: transient projection graph; the flat data stays the single source of truth).
 *
 * Panel-binding seam: the schema's conversion hook and the nodes' connection hooks must route
 * write-through to the panel, but schema methods run on the CDO and cannot hold per-editor state — so
 * THIS graph object owns the delegates the panel binds (the schema/nodes reach them via GetGraph()).
 */
UCLASS()
class UPaper2DPlusAnimationMap : public UEdGraph
{
	GENERATED_BODY()

public:
	/** Bound by the panel (U4). See FPaper2DPlusComboWireCreateDelegate — the delegate performs the
	 *  data write-through and spawns the edge node; the schema NEVER writes asset data itself. */
	FPaper2DPlusComboWireCreateDelegate OnWireCreateRequested;

	/** Bound by the panel. See FPaper2DPlusTransitionTargetRewireDelegate. */
	FPaper2DPlusTransitionTargetRewireDelegate OnTransitionTargetRewireRequested;

	/** Bound by the panel (U4). See FPaper2DPlusComboEdgeRemovalRequestDelegate — cleanup-only; the
	 *  suicide hook never mutates rows. */
	FPaper2DPlusComboEdgeRemovalRequestDelegate OnEdgeRemovalRequested;

	/** Bound by the panel (U5). See FPaper2DPlusComboNodeMoveCommittedDelegate — the node-move
	 *  persistence seam (the widget reaches the panel through its graph, like the wire-create hook;
	 *  CreateVisualWidget has no argument channel for a panel pointer). */
	FPaper2DPlusComboNodeMoveCommittedDelegate OnNodeMoveCommitted;

	/** Bound by the panel (comment boxes feature). See FPaper2DPlusComboCommentChangedDelegate — the
	 *  comment write-through seam (the comment widget/node reaches the panel through its graph, like the
	 *  node-move and wire-create hooks). */
	FPaper2DPlusComboCommentChangedDelegate OnCommentChanged;

	/** Bound by the panel (TASK-108 U6, R16). See FPaper2DPlusAnimMapChangeGroupDelegate — the move
	 *  node's right-click "Change Group…" picker reaches the panel through its graph (same channel as
	 *  the other graph delegates); the panel performs the no-op guard + single transaction + notify. */
	FPaper2DPlusAnimMapChangeGroupDelegate OnChangeGroupRequested;

	/** Bound by the panel. See FPaper2DPlusAnimMapSetPhaseDelegate. */
	FPaper2DPlusAnimMapSetPhaseDelegate OnSetPhaseRequested;

	/** Bound by the panel. See FPaper2DPlusAnimMapSetAnimationTagsDelegate. */
	FPaper2DPlusAnimMapSetAnimationTagsDelegate OnSetAnimationTagsRequested;

	/** Bound by the panel (chain-start rework). See FPaper2DPlusChainStartLinkRequestedDelegate — the
	 *  marker-aim gesture funnel (flag write + link surgery live in the panel handler). */
	FPaper2DPlusChainStartLinkRequestedDelegate OnChainStartLinkRequested;

	/** Bound by the panel (chain-start rework). See FPaper2DPlusChainStartUnlinkRequestedDelegate —
	 *  wire break clears the flag and leaves the marker floating. */
	FPaper2DPlusChainStartUnlinkRequestedDelegate OnChainStartUnlinkRequested;

	/** Bound by the panel (chain-start rework). See FPaper2DPlusChainStartAddRequestedDelegate — the
	 *  empty-canvas "Add Chain Start" action's spawn seam. */
	FPaper2DPlusChainStartAddRequestedDelegate OnAddChainStartRequested;

	/** Bound by the panel (chain-start rework). See FPaper2DPlusChainStartMarkerMovedDelegate — the
	 *  marker-position persistence seam (the widget reaches the panel through its graph, like every
	 *  other graph delegate). */
	FPaper2DPlusChainStartMarkerMovedDelegate OnChainStartMarkerMoveCommitted;

	/** Bound by the panel (chain-end rework). See FPaper2DPlusChainEndLinkRequestedDelegate — the
	 *  marker-aim gesture funnel (flag write + link surgery live in the panel handler). */
	FPaper2DPlusChainEndLinkRequestedDelegate OnChainEndLinkRequested;

	/** Bound by the panel (chain-end rework). See FPaper2DPlusChainEndUnlinkRequestedDelegate —
	 *  wire break clears the flag and leaves the marker floating. */
	FPaper2DPlusChainEndUnlinkRequestedDelegate OnChainEndUnlinkRequested;

	/** Bound by the panel (chain-end rework). See FPaper2DPlusChainEndAddRequestedDelegate — the
	 *  empty-canvas "Add Chain End" action's spawn seam. */
	FPaper2DPlusChainEndAddRequestedDelegate OnAddChainEndRequested;

	/** Bound by the panel (chain-end rework). See FPaper2DPlusChainEndMarkerMovedDelegate — the
	 *  marker-position persistence seam (the widget reaches the panel through its graph, like every
	 *  other graph delegate). */
	FPaper2DPlusChainEndMarkerMovedDelegate OnChainEndMarkerMoveCommitted;

	/** Set by the panel alongside its group open/close state: true only while an EXACT tag-group
	 *  surface is open (never the unassigned bucket or the board). The schema's empty-canvas "Add
	 *  Chain Start" / "Add Chain End" actions consult this (schema methods run on the CDO and cannot
	 *  see the panel) — chain starts/ends are exact-group identity, so the actions are omitted
	 *  without a group scope. Plain member on the transient graph — never serialized. */
	bool bHasExactGroupScope = false;

	/** Set by the panel for the whole scope of a reconcile/rebuild. Schema and node hooks early-out
	 *  while set so the reconcile's own pin surgery can't re-enter write-through or fire the suicide
	 *  hook against half-rebuilt topology (the repo's suppress-or-coerce rule). Plain member on the
	 *  transient graph — never serialized, reset with every rebuild-from-scratch. */
	bool bRebuildInProgress = false;

	/**
	 * THE shared node-spawn funnel — ALL combo-graph node creation (the U4 conversion/wire path AND
	 * the reconcile path) must go through here.
	 *
	 * LOAD-BEARING ORDERING INVARIANT: ClearFlags(RF_Transactional) runs IMMEDIATELY after
	 * FGraphNodeCreator::Finalize(), BEFORE any MakeLinkTo/link surgery. UEdGraph::CreateNode spawns
	 * nodes RF_Transactional (EdGraph.cpp:218) and SaveToTransactionBuffer has NO transient-package
	 * exemption (UObjectGlobals.cpp:3361-3385) — wire-create spawns the edge node inside the engine's
	 * open GraphEd_CreateConnection transaction, whose MakeLinkTo Modify()s the owning nodes; a
	 * still-transactional node recorded there RESURRECTS AS A ZOMBIE after a reconcile destroys it
	 * (undo restores the recorded node into a graph that no longer owns it). With the flag cleared
	 * before any link surgery, the asset is the only object the transaction ever records.
	 *
	 * ConfigureBeforePins runs between CreateNode and Finalize so identity fields that shape pin
	 * allocation (UPaper2DPlusAnimationMapNode_Move::bIsStub — stubs allocate NO output pin) are set
	 * before AllocateDefaultPins fires. FGraphNodeCreator::Finalize unconditionally assigns a fresh
	 * NodeGuid, so a valid GUID supplied by the configure callback is restored afterward. Persisted
	 * projection nodes (comments) therefore keep the same identity every time they materialize.
	 */
	template <typename NodeType, typename ConfigureFnType>
	static NodeType* SpawnNodeUntransactional(UEdGraph& Graph, ConfigureFnType ConfigureBeforePins)
	{
		FGraphNodeCreator<NodeType> Creator(Graph);
		NodeType* Node = Creator.CreateNode(/*bSelectNewNode=*/false);
		ConfigureBeforePins(*Node);
		const FGuid ConfiguredNodeGuid = Node->NodeGuid;
		Creator.Finalize();
		if (ConfiguredNodeGuid.IsValid())
		{
			Node->NodeGuid = ConfiguredNodeGuid;
		}
		// INVARIANT (see above): clear BEFORE any MakeLinkTo — a still-transactional node recorded in
		// the engine's open connection transaction resurrects as a zombie after reconcile destroys it.
		Node->ClearFlags(RF_Transactional);
		return Node;
	}
};
