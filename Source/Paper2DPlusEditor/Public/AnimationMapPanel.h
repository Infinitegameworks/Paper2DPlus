// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AnimationMapCore.h"
#include "EditorUndoClient.h"
#include "GraphEditor.h" // SGraphEditor::FGraphEditorEvents + FGraphPanelSelectionSet (the strip's selection seam)
#include "UObject/WeakObjectPtrTemplates.h"
#include "Widgets/SCompoundWidget.h"

class FActiveTimerHandle;
class FCharacterProfileEditorModel;
class FScopedTransaction;
class FUICommandList;
class SBorder;
class SBox;
class SComboButton;
class SEditableTextBox;
class SVerticalBox;
class SWidgetSwitcher;
class UPaper2DPlusCharacterProfileAsset;
class UPaper2DPlusAnimationMap;
class UPaper2DPlusAnimationMapNode_ChainEnd;
class UPaper2DPlusAnimationMapNode_ChainStart;
class UPaper2DPlusAnimationMapNode_Move;
class UPaper2DPlusAnimationMapNode_Transition;
class UEdGraph;
class UEdGraphNode;
class UEdGraphNode_Comment;
class UEdGraphPin;
class UPaperFlipbook;
struct FPropertyChangedEvent;
struct FGraphAppearanceInfo;
struct FPaper2DPlusMoveTransition;

/**
 * The "Animation Map" editor tab (combo-graph plan U4 surface + U5 gestures): an SGraphEditor surface
 * rendering the LIVE projection of the profile's flat FFlipbookTransitionData (the single source of
 * truth) as move nodes + edge-pill transition nodes. EDITABLE since U5: every authoring gesture (wire
 * create, delete command, drag-drop placement, node move) funnels its data write through this panel's
 * transaction template — the graph itself is never an authority. U6 added the visual-fidelity layer
 * (thumbnail nodes, pill badges, self-loop arcs) and the tuple-based edge selection RE-MATCH after
 * wholesale rebuilds (replacing the U4 clear-only stopgap). TASK-96 P3 RETIRED the in-graph details
 * strip: a single node selection now drives the merged Animations tab's shared SProfileDetailsPanel
 * via Model->SetSelectedFlipbook (move node -> its flipbook; edge -> its FromMove), and the Map FOLLOWS
 * the model's OnFlipbookSelectionChanged by focusing the matching node (paint-deferred, gesture-guarded).
 *
 * Mirrors the other tool tabs: SCompoundWidget + FEditorUndoClient, constructed with .Model(EditorModel).
 * The asset is resolved STRICTLY via Model->GetAsset() per use — never cached on the panel or on nodes —
 * so a Character Layer editor Base-Profile swap retargets the whole tab (model re-init broadcasts
 * OnAssetDataChanged, which reconciles).
 *
 * Reconcile engine (the plan's named-risk core, "Reconcile contract (R9)"):
 *  - Triggers: Model->OnAssetDataChanged, Model->OnAssetExternallyModified, own PostUndo/PostRedo,
 *    first construct, and the edge suicide hook's cleanup request.
 *  - Handlers PEND, never drop: a trigger landing mid-gesture (HasActiveTransaction() ||
 *    bGraphWriteInProgress) sets bReconcilePending; EndTransaction / the timer flush it.
 *  - Application is a POLL-UNTIL-APPLIED active timer (EActiveTimerReturnType::Continue) that re-checks
 *    gesture liveness each tick (Slate drag-drop, graph-panel mouse capture, GEditor transaction,
 *    GIsTransacting) and only reconciles when quiet — an escape-cancelled wire drag produces no drop
 *    event, so a one-shot application would strand the pending flag. A rebuild under a live
 *    FDragConnection is a crash class.
 *  - Hidden tab: Slate executes widget active timers from SWidget::Paint (SWidget.cpp:1432-1436), so a
 *    backgrounded tab's pending reconcile naturally waits and applies on the first paint after the tab
 *    is foregrounded — bReconcilePending doubles as the hidden-tab bNeedsRefresh.
 *  - Algorithm: full-diff early-out (Paper2DPlusAnimationMap::DiffProjection incl. position drift +
 *    flipbook-pointer changes) -> zero graph mutation on the echo path; otherwise HYBRID — move nodes
 *    diffed in place by lowered name, edge nodes rebuilt wholesale in row order with re-stamped
 *    indices; positions reapplied by DIRECT NodePosX/Y assignment (never SGraphNode::MoveTo) from
 *    map-else-SESSION-CACHE; view location/zoom snapshot-restored around graph-object set changes.
 */
class PAPER2DPLUSEDITOR_API SAnimationMapPanel : public SCompoundWidget, public FEditorUndoClient
{
public:
	SLATE_BEGIN_ARGS(SAnimationMapPanel) {}
		SLATE_ARGUMENT(TSharedPtr<FCharacterProfileEditorModel>, Model)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SAnimationMapPanel();

	//~ Begin FEditorUndoClient interface
	virtual void PostUndo(bool bSuccess) override;
	virtual void PostRedo(bool bSuccess) override;
	//~ End FEditorUndoClient interface

	//~ Begin SWidget interface — the thin outer drop target for FFlipbookGroupDragDropOp (U5, R2):
	// VERIFIED 5.7 routing — with IsEditable=true, SGraphPanel returns Unhandled for ops that are not
	// FGraphEditorDragDropAction / FExternalDragOperation / FAssetDragDropOp (OnDragOver
	// SGraphPanel.cpp:1444-1447; OnDrop :1501-1508 after ExtractAssetDataFromDrag finds no assets), so
	// the event BUBBLES here — the panel itself is the thin outer drop wrapper, without an extra
	// widget layer.
	virtual FReply OnDragOver(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;
	virtual FReply OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;
	/** Preview (tunnel) phase: record the graph-space anchor of a left-drag over the canvas so a subsequent
	 *  C-key comment can wrap an EMPTY-space rubber-band (the stock C-key only wraps SELECTED nodes; a marquee
	 *  over blank space selects nothing). Always returns Unhandled — the graph keeps its own marquee/selection. */
	virtual FReply OnPreviewMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	//~ End SWidget interface

	/** Consulted by the reconcile guards and the model's external-modify suppression. */
	bool HasActiveTransaction() const { return ActiveTransaction.IsValid(); }

	/** Coalesced reconcile request — sets the pending flag and arms the poll-until-applied timer. */
	void RequestReconcile();

	/** Paper2DPlus.AnimationMapGraphProbe payload: logs
	 *  "nodes=N edges=N stubs=N filter=<TagName|none> refresh=<current|pending|blocked>", per-node
	 *  "name@x,y[,stub][,start][,end][,combo=N/M][,dim] tags=<leaf1>;<leaf2>", per-marker
	 *  "chainstart -> <TargetMove|floating> @x,y" then "chainend -> <TargetMove|floating> @x,y"
	 *  (chain-start/-end rework — markers are not counted in the header's nodes=), and per-edge
	 *  "edge From->Target row=N phase=<leaf|none>" lines. The headless/ECABridge gates grep these;
	 *  TASK-108 stripped label=/cond=/cancel=/badge=, the chain-start rework replaced ,root=R# with
	 *  the bare ,start flag, the chain-end rework inserted ,end after it, and future fields append
	 *  to their record. */
	void LogProbe() const;

	/** Headless/ECABridge seam: drive the Animation Map view. Spec "groups"/"close"/"map" shows the
	 *  group board; "full" opens the complete composition graph; "unassigned" opens the unassigned
	 *  graph; any other value opens the group whose tag leaf name matches (case-insensitive), else
	 *  falls back to the group board. */
	void OpenAnimationMapView(const FString& Spec);
	/** Test/automation seam (the Paper2DPlus.AddComment console command): ensure a scoped graph is open
	 *  (unassigned fallback) and drop a comment box, then frame it. */
	void ConsoleAddComment();
	/** Render-tour acceptance seam: typed graph readiness avoids inferring live state from Slate type strings. */
	bool SynchronizeAndFrameScopedGraphForAutomation();
	bool HasLiveScopedGraphForAutomation() const;
	int32 GetProjectedMoveNodeCountForAutomation() const;
	int32 GetProjectedTransitionNodeCountForAutomation() const;

	/** Worldless reconcile seams for the ordinary move/external-edit regression test. */
	void ReconcileNowForTests() { ReconcileNow(); }
	UPaper2DPlusAnimationMapNode_Move* FindProjectedMoveNodeForTests(
		const FString& MoveName) const;
	void CommitProjectedMovePositionForTests(
		UPaper2DPlusAnimationMapNode_Move* MoveNode)
	{
		HandleNodeMoveCommitted(MoveNode);
	}
	/** TASK-171 acceptance seams: exercise the exact production Map gestures that author the
	 *  retained TagMappings chain flags. The transient marker is created first, then the ordinary
	 *  aim/link funnel performs the asset transaction and graph lockstep update. */
	UEdGraphNode* AddChainStartMarkerForTests(const FVector2D& GraphPosition)
	{
		return HandleAddChainStartRequested(GraphPosition);
	}
	bool LinkChainStartMarkerForTests(
		UPaper2DPlusAnimationMapNode_ChainStart* MarkerNode,
		const FString& TargetMoveName)
	{
		return HandleChainStartLinkRequested(MarkerNode, TargetMoveName);
	}
	UEdGraphNode* AddChainEndMarkerForTests(const FVector2D& GraphPosition)
	{
		return HandleAddChainEndRequested(GraphPosition);
	}
	bool LinkChainEndMarkerForTests(
		UPaper2DPlusAnimationMapNode_ChainEnd* MarkerNode,
		const FString& TargetMoveName)
	{
		return HandleChainEndLinkRequested(MarkerNode, TargetMoveName);
	}
	/** TASK-171 recovery seam: the production Map-overflow action repairs the one possible invalid
	 *  TagMappings key by moving it to an unused valid tag, or removes it only when it is empty. */
	bool RepairInvalidTagMappingForTests(const FGameplayTag& ReplacementTag)
	{
		return RepairInvalidTagMapping(ReplacementTag);
	}

	/** Paper2DPlus.AnimationMapProbe payload: logs group and unmapped counts from the current asset. */
	void LogAnimationMapProbe() const;

	/** WS3 U3 — "Derive Missing Phases…" (Map overflow entry + Paper2DPlus.DerivePhasesFromMap console command): set
	 *  each move's EditorMeta.PhaseTag from its combo-chain position via DerivePhasesFromMap, skipping
	 *  moves that already carry a manual tag. One transaction; refreshes the pills/rows via the modify
	 *  broadcast. Returns the number of phase tags written (0 = nothing to derive / all already tagged). */
	int32 DerivePhasesFromMapAction();

private:
	// --- Model (the asset is resolved via Model->GetAsset() PER USE — never cached) ---
	TSharedPtr<FCharacterProfileEditorModel> Model;
	FDelegateHandle ModelAssetDataChangedHandle;
	FDelegateHandle ModelAssetExternallyModifiedHandle;

	/** The transient projection graph: NewObject into GetTransientPackage(), RF_Transient, never
	 *  transactional, rooted by the panel (AddToRoot; destructor unroots behind the engine's GExitPurge
	 *  guard — the SReferenceViewer.cpp:127-138 pattern). Raw pointer is GC-safe via the root. */
	UPaper2DPlusAnimationMap* GraphObj = nullptr;

	TSharedPtr<SGraphEditor> GraphEditorWidget;
	TSharedPtr<SWidgetSwitcher> MainViewSwitcher;

	/** Convert an absolute screen position to graph (node) space via the live SGraphPanel. False if no panel. */
	bool ScreenToGraphPosition(const FVector2D& ScreenPos, FVector2D& OutGraphPos) const;

	/** Graph-space anchor of an in-progress left-drag on the canvas (set by OnPreviewMouseButtonDown), consumed
	 *  once by AddCommentForCurrentGroup to size a comment around an empty-canvas rubber-band. */
	FVector2D CommentDragStartGraph = FVector2D::ZeroVector;
	bool bCommentDragStartValid = false;

	/** U5: Delete + SelectAll, plus comment-only F2 Rename, bound via SGraphEditor's AdditionalCommands (the FAIGraphEditor
	 *  CreateCommandList shape, AIGraphEditor.cpp:41-189). Copy/Cut/Paste/Duplicate stay unbound
	 *  (CanDuplicateNode()=false; the engine binds none by default). */
	TSharedPtr<FUICommandList> GraphEditorCommands;

	// --- Transaction helpers (cloned from SCurvesPanel) ---
	TUniquePtr<FScopedTransaction> ActiveTransaction;
	void BeginTransaction(const FText& Description);
	void EndTransaction();

	/** Set by U5's write helpers for their whole scope; reconcile triggers pend while it is up. */
	bool bGraphWriteInProgress = false;

	// --- Reconcile engine state ---
	/** Coalesced trigger flag — cleared at reconcile START (a mid-reconcile trigger re-pends). Doubles
	 *  as the hidden-tab bNeedsRefresh: the applying timer only runs while the panel paints. */
	bool bReconcilePending = false;
	TWeakPtr<FActiveTimerHandle> ReconcileTimerHandle;

	/** The previous projection — DiffProjection's baseline for the full-diff early-out. INVARIANT for
	 *  gesture code (U5+): any scope that mutates graph+data in LOCKSTEP must set
	 *  LastProjection = ProjectGraphForCurrentView(Asset) before releasing bGraphWriteInProgress (or run
	 *  ReconcileNow, which updates it) — otherwise the gesture's own Modify() echo diffs non-empty
	 *  and performs redundant reconciliation work. */
	Paper2DPlusAnimationMap::FGraphProjection LastProjection;

	/** Set when the LIVE GRAPH is known to have diverged from LastProjection without a data change
	 *  (e.g. an external pin-link break fired the edge-removal hook): forces the next reconcile past
	 *  the full-diff early-out so surviving edge links are repaired and orphaned nodes are removed. */
	bool bForceTopologyRepair = false;

	/** SESSION-TRANSIENT auto-layout cache (lowered name -> position): FirstLayout results land here,
	 *  NEVER on the asset (zero dirty on tab open, R4); load-bearing for undo-of-first-move visibly
	 *  restoring the auto-laid spot. */
	TMap<FString, FVector2D> SessionPositionCache;

	/** Lowered name -> the position ReapplyPositions LAST wrote to the node. Lets the reconcile tell a
	 *  legitimate STORED change (undo / external edit / a committed drag — RE-ASSERT it) apart from a node
	 *  whose live position drifted because the USER moved it uncommitted (KEEP it — never snap back). Without
	 *  this, every reconcile re-asserted the stored/auto-grid value, so a dragged-but-uncommitted node (or any
	 *  node not yet in the durable AnimationMapNodePositions map) "moved itself" back to the FirstLayout grid
	 *  on the next reconcile — and TASK-96 made reconciles fire on every selection, so it shuffled constantly. */
	TMap<FString, FVector2D> LastAppliedPositions;

	/** Per move node (lowered name): the flipbook OBJECT the node last displayed — reimport swaps the
	 *  object, the diff sees it, and the existing card replaces only its thumbnail child. */
	TMap<FString, TWeakObjectPtr<UPaperFlipbook>> LastResolvedFlipbookByName;

	// --- Reconcile engine internals ---
	void HandleAssetChangedSignal();
	EActiveTimerReturnType OnReconcileTimer(double InCurrentTime, float InDeltaTime);
	bool IsGestureLive() const;
	bool BuildAnimationMapGraphFilter(const UPaper2DPlusCharacterProfileAsset* Asset, TSet<FString>& OutMoveNamesLower) const;
	Paper2DPlusAnimationMap::FGraphProjection ProjectGraphForCurrentView(const UPaper2DPlusCharacterProfileAsset* Asset) const;
	int32 GetMainViewSwitcherIndex() const;
	void RefreshMainViewSwitcher();
	void ReconcileNow();
	void ReapplyPositions(const TMap<FString, UPaper2DPlusAnimationMapNode_Move*>& LiveMovesByLower,
		UPaper2DPlusCharacterProfileAsset* Asset, const TSet<FString>& NewlyAddedLower);
	void CollectLiveNodes(TMap<FString, UPaper2DPlusAnimationMapNode_Move*>& OutMovesByLower,
		TArray<UPaper2DPlusAnimationMapNode_Transition*>& OutEdges) const;

	/** Complete the write-funnel lockstep for the DERIVED combo display dimensions. A transition or
	 *  chain start/end write renumbers the derived main line on nodes whose OWN rows never changed,
	 *  and every write funnel adopts the fresh projection as the diff baseline — so the deferred echo
	 *  reconcile early-outs and would leave stale #N chips / ▶⏹ glyphs until an unrelated reconcile
	 *  (live-observed: aiming a Chain End left the trailing recovery's chip un-renumbered). Called by
	 *  each funnel right after its `LastProjection` refresh: re-stamps
	 *  bIsChainStart/bIsChainEnd/ComboSpineIndex/ComboSpineLength on every live move node from the
	 *  adopted baseline + one paint invalidate. Display-state only — never mutates the asset. */
	void RestampComboDisplayFromBaseline();

	// --- U3/U5 graph-delegate handlers ---
	bool HandleWireCreateRequested(const FString& FromMoveName, const FString& ToMoveName);
	bool HandleTransitionTargetRewireRequested(UPaper2DPlusAnimationMapNode_Transition* EdgeNode,
		const FString& NewTargetMoveName);
	void HandleEdgeRemovalRequested(UPaper2DPlusAnimationMapNode_Transition* EdgeNode);
	void HandleNodeMoveCommitted(UPaper2DPlusAnimationMapNode_Move* MoveNode);
	/** TASK-108 U6 (R16): the move node's right-click "Change Group…" pick — no-op when the target IS
	 *  the animation's current home group (no transaction, no dirty); otherwise ONE transaction through
	 *  AssignFlipbookToTagMapping (removed from the current group's Entries preserving the others'
	 *  order, APPENDED at the END of the target's, mapping key created when the tag is new) + clears
	 *  the moved card's old durable/session placement so its destination group lays it out locally.
	 *  Board tiles / graph / group-implied effective tags refresh via the normal reconcile. */
	void HandleChangeGroupRequested(const FString& MoveName, const FGameplayTag& InTargetTag);
	/** Set/clear one descriptive phase across the clicked move or its selected real-move cohort. */
	void HandleSetPhaseRequested(const FString& MoveName, const FGameplayTag& InPhaseTag);
	/** Replace the exact authored AnimationTags container across the same target cohort. */
	void HandleSetAnimationTagsRequested(const FString& MoveName, const FGameplayTagContainer& InTags);
	/** Deterministic multi-edit targeting: clicked-selected => all selected real moves; otherwise only
	 *  the clicked real move. Returned in the current projection's authored order. */
	TArray<FString> ResolveRealMoveTargets(const FString& RequestedMoveName) const;

	// --- Selection Cohort Card (genuine multi-selection only; UI-only, details pane unchanged) ---
	TSharedRef<SWidget> BuildSelectionCohortCard();
	TArray<FString> GetSelectedRealMoveNamesInProjectionOrder() const;
	/** Recompute cached cohort copy. Returns true only when visible summary state changed. */
	bool RefreshSelectionCohortSummary();
	FText GetSelectionCohortCountText() const;
	FText GetSelectionCohortGroupText() const;
	FText GetSelectionCohortPhaseText() const;
	FText GetSelectionCohortAnimationTagsText() const;
	FText GetSelectionCohortChainFlagText(bool bChainStart) const;
	TSharedRef<SWidget> BuildCohortGroupMenu();
	TSharedRef<SWidget> BuildCohortPhaseMenu();
	TSharedRef<SWidget> BuildCohortAnimationTagsMenu();
	TSharedPtr<SBorder> SelectionCohortCard;
	int32 SelectionCohortCount = 0;
	FText SelectionCohortGroupText;
	FText SelectionCohortPhaseText;
	FText SelectionCohortAnimationTagsText;
	FText SelectionCohortChainStartText;
	FText SelectionCohortChainEndText;

	// --- Chain Start markers (chain-start rework: bIsChainStart on the exact-group mapping entry) ---
	// The marker node (UPaper2DPlusAnimationMapNode_ChainStart) is the AUTHORING surface for the
	// per-entry bIsChainStart flag: its single outgoing wire designates the group's chain opener.
	// The flag lives on FFlipbookTagMappingEntry (the flat truth); markers are pure projection.
	/** The aim gesture (schema TryCreateConnection -> graph delegate, INSIDE the engine's connection
	 *  transaction — the HandleWireCreateRequested shape): clear-old + set-new flag in the panel's
	 *  nested "Set Chain Start" transaction, retarget + rewire the marker in lockstep, refresh
	 *  LastProjection. Refusal toasts (no transaction): no exact group scope, target not in the
	 *  group's mapping, target already flagged. */
	bool HandleChainStartLinkRequested(UPaper2DPlusAnimationMapNode_ChainStart* MarkerNode, const FString& TargetMoveName);
	/** The break gesture (schema Break* -> graph delegate): clear the previous target's flag in ONE
	 *  transaction and leave the marker FLOATING at its position (its links broken here, not by the
	 *  schema). No-op for already-floating markers. */
	void HandleChainStartUnlinkRequested(UPaper2DPlusAnimationMapNode_ChainStart* MarkerNode);
	/** The empty-canvas "Add Chain Start" action: spawn a FLOATING marker (untransactional funnel; no
	 *  data change, no transaction) at the click position. Nullptr + toast without an exact group
	 *  scope (belt-and-braces — the schema already omits the action there). */
	UEdGraphNode* HandleAddChainStartRequested(const FVector2D& GraphPosition);
	/** Marker-move persistence (the HandleNodeMoveCommitted sibling): record a TARGETED marker's
	 *  position in ChainStartMarkerPositions under its synthetic key. Floating markers persist
	 *  nothing — the live node is their only position. */
	void HandleChainStartMarkerMoveCommitted(UPaper2DPlusAnimationMapNode_ChainStart* MarkerNode);
	/** Reconcile-tail WHOLESALE marker sync (projection-driven — reads Current, never node stamps):
	 *  (a) every projected bIsChainStart move gets exactly ONE wired marker (spawned through the
	 *  untransactional funnel; position from ChainStartMarkerPositions else ~180 left / 40 up of the
	 *  move), (b) TARGETED markers whose move vanished or lost its flag are removed, (c) FLOATING
	 *  markers are NEVER removed (session-transient authoring intent). Spawning is additionally gated
	 *  to the exact-group surface so the hidden board-view projection (which flags ALL groups'
	 *  openers) never mints markers. Runs under its own bRebuildInProgress guard on BOTH reconcile
	 *  paths (early-out + full), the MaterializeCommentsForCurrentGroup shape. */
	void SynchronizeChainStartMarkers(UPaper2DPlusCharacterProfileAsset* Asset,
		const Paper2DPlusAnimationMap::FGraphProjection& Current);
	/** All live marker nodes on the graph (the CollectLiveComments sibling). */
	void CollectLiveChainStartMarkers(TArray<UPaper2DPlusAnimationMapNode_ChainStart*>& OutMarkers) const;

	/** SESSION-TRANSIENT marker positions, keyed by the synthetic "__chainstart__:<targetlower>" key.
	 *  DELIBERATELY panel-owned, NOT Asset->AnimationMapNodePositions: ProjectGraph turns every
	 *  unrecognized position-map key into a projected stub node on the unfiltered (board) projection,
	 *  so a synthetic key in the asset map would mint ghost "__chainstart__:x" stubs — and the
	 *  projection core is outside this surface's ownership. Cost: marker positions reset to the
	 *  deterministic near-the-move default on editor restart (the flag itself is asset data and
	 *  survives). The map is never pruned — a re-flagged move resurrects its old marker spot, the
	 *  kept-stale-placements idiom. */
	TMap<FString, FVector2D> ChainStartMarkerPositions;

	// --- Chain End markers (chain-end rework: bIsChainEnd on the exact-group mapping entry) ---
	// The Chain Start cluster's MIRROR: the marker node (UPaper2DPlusAnimationMapNode_ChainEnd) is
	// the AUTHORING surface for the per-entry bIsChainEnd flag — a move's output wires INTO the
	// marker's single input to designate the group's combo main-line END (the runtime spine
	// derivation prefers paths terminating at a flagged end and never walks past one). The flag
	// lives on FFlipbookTagMappingEntry (the flat truth); markers are pure projection.
	/** The aim gesture (schema TryCreateConnection -> graph delegate, INSIDE the engine's connection
	 *  transaction — the HandleChainStartLinkRequested mirror, with the wire direction reversed:
	 *  move output -> marker input): clear-old + set-new flag in the panel's nested "Set Chain End"
	 *  transaction, retarget + rewire the marker in lockstep, refresh LastProjection. Refusal toasts
	 *  (no transaction): no exact group scope, source not in the group's mapping, source already
	 *  flagged. */
	bool HandleChainEndLinkRequested(UPaper2DPlusAnimationMapNode_ChainEnd* MarkerNode, const FString& TargetMoveName);
	/** The break gesture (schema Break* -> graph delegate): clear the previous target's flag in ONE
	 *  transaction and leave the marker FLOATING at its position (its links broken here, not by the
	 *  schema). No-op for already-floating markers. */
	void HandleChainEndUnlinkRequested(UPaper2DPlusAnimationMapNode_ChainEnd* MarkerNode);
	/** The empty-canvas "Add Chain End" action: spawn a FLOATING marker (untransactional funnel; no
	 *  data change, no transaction) at the click position. Nullptr + toast without an exact group
	 *  scope (belt-and-braces — the schema already omits the action there). */
	UEdGraphNode* HandleAddChainEndRequested(const FVector2D& GraphPosition);
	/** Marker-move persistence (the HandleChainStartMarkerMoveCommitted sibling): record a TARGETED
	 *  marker's position in ChainEndMarkerPositions under its synthetic key. Floating markers
	 *  persist nothing — the live node is their only position. */
	void HandleChainEndMarkerMoveCommitted(UPaper2DPlusAnimationMapNode_ChainEnd* MarkerNode);
	/** Reconcile-tail WHOLESALE marker sync — SynchronizeChainStartMarkers MIRRORED off
	 *  FProjectedNode::bIsChainEnd: (a) every projected bIsChainEnd move gets exactly ONE marker
	 *  wired move-output -> marker-input (spawned through the untransactional funnel; position from
	 *  ChainEndMarkerPositions else ~180 right / 40 down of the move — the opposite corner from the
	 *  start marker), (b) TARGETED markers whose move vanished, lost its flag, or degraded to a stub
	 *  (stubs own no output pin to wire from) are removed, (c) FLOATING markers are NEVER removed.
	 *  Spawning gated to the exact-group surface; runs under its own bRebuildInProgress guard on
	 *  BOTH reconcile paths. */
	void SynchronizeChainEndMarkers(UPaper2DPlusCharacterProfileAsset* Asset,
		const Paper2DPlusAnimationMap::FGraphProjection& Current);
	/** All live end-marker nodes on the graph (the CollectLiveChainStartMarkers sibling). */
	void CollectLiveChainEndMarkers(TArray<UPaper2DPlusAnimationMapNode_ChainEnd*>& OutMarkers) const;

	/** SESSION-TRANSIENT end-marker positions, keyed by the synthetic "__chainend__:<targetlower>"
	 *  key — the ChainStartMarkerPositions contract verbatim (panel-owned, never the asset's
	 *  position map, never pruned; see that member's comment for the ghost-stub rationale). */
	TMap<FString, FVector2D> ChainEndMarkerPositions;

	// --- TASK-151 U3: the Map canvas rail (replaces the retired persistent horizontal toolbar) ---
	// One compact NON-WRAPPING rail overlaid top-left on the board/graph switcher, with the base
	// content inset by the rail height so it can never cover an actionable node or group tile.
	// Placement only: every command still calls the SAME callbacks/predicates the toolbar used.
	// GEOMETRY CONTRACT (owned by Construct, not by the builder below): the hosting SOverlay clips to the
	// panel bounds and the rail's wrapper SBox clamps its width to the allotted geometry — an HAlign_Left
	// overlay child is otherwise arranged at its full desired width and overhangs the neighbouring
	// details panel at narrow widths — while the same SBox HeightOverrides the rail to exactly the space
	// the canvas inset reserves, so inset and rail height are one derived number.
	/** The rail itself: scope navigation, Zoom to Fit, Filter state, and the Map overflow entry
	 *  point. */
	TSharedRef<SWidget> BuildMapCanvasRail();
	/** Map overflow content, including phase derivation and invalid TagMappings-key recovery. */
	TSharedRef<SWidget> BuildMapOverflowMenu();
	/** Menu-hosted valid-tag picker shown only when the asset contains an invalid mapping with members. */
	TSharedRef<SWidget> BuildInvalidTagMappingRepairWidget();
	/** Undoable recovery funnel. A valid unused replacement preserves every entry; an invalid
	 *  replacement removes the key only when it has no entries. */
	bool RepairInvalidTagMapping(const FGameplayTag& ReplacementTag);
	/** Dismiss/defer the menu-hosted picker callback before mutating the asset and rebuilding the Map. */
	void CommitInvalidTagMappingRepairFromPicker(const FGameplayTag InNewTag);
	/** Value handed to the cross-version Gameplay Tags widget for invalid-group repair. */
	TSharedPtr<FGameplayTag> InvalidGroupRepairPickerValue;
	/** The open graph's scope name ("<Group> graph" / "Unassigned graph" / "Full composition graph"),
	 *  shown elided on the rail with the full text in its tooltip. */
	FText GetAnimationMapScopeLabel() const;
	/** The stable rail control used for returning focus after a contextual action. */
	TSharedPtr<SComboButton> MapOverflowButton;

	// --- TASK-108 U6 (R8) filter state, reworked for TASK-151 U3 (R10/R16) ---
	/** The active filter tag (invalid = off). Non-matching nodes DIM (never hide); board tiles dim when
	 *  their group KEY doesn't match hierarchically. Persisted in GEditorPerProjectIni. */
	FGameplayTag MapFilterTag;
	/** Static-rebuilt host for the filter control: a compact "Filter" combo while inactive, a directly
	 *  removable tag chip while active (the details-pane static-content idiom — never a bound
	 *  .Tag_Lambda in an always-visible surface). The chip's TAG is static (rebuilt on commit), but its
	 *  COLORS are _Lambda bound so a live Tag Colors registry edit repaints it in place. */
	TSharedPtr<SBox> MapFilterPickerBox;
	/** The live invoker inside MapFilterPickerBox — focus returns here after a menu-hosted commit
	 *  replaces the widget. */
	TSharedPtr<SWidget> MapFilterInvoker;
	/** Value handed to IGameplayTagsEditorModule::MakeGameplayTagWidget; re-seeded per menu open. */
	TSharedPtr<FGameplayTag> MapFilterPickerValue;
	/** Set + persist + re-stamp live nodes + rebuild the board. Same-tag = no-op. */
	void SetMapFilterTag(const FGameplayTag& InFilterTag);
	/** Repopulate MapFilterPickerBox with the inactive Filter control or the active removable chip. */
	void RebuildMapFilterPicker();
	/** The menu-hosted single-tag picker (IGameplayTagsEditorModule::MakeGameplayTagWidget, so UE
	 *  5.0-5.8 all get an interactive tree), wrapped in SMenuHostedTagPickerGuard. */
	TSharedRef<SWidget> BuildMapFilterPickerMenu();
	/** Picker commit path: DISMISS the hosting menu and DEFER the commit a tick, because committing
	 *  rebuilds MapFilterPickerBox — i.e. destroys the invoker widget from inside its own callback. */
	void CommitMapFilterTagFromPicker(const FGameplayTag InNewTag);
	/** Return keyboard focus to the (possibly rebuilt) filter invoker after a picker commit. */
	void FocusMapFilterControl();
	/** Re-stamp every live move node's bDimmedByFilter from its effective tags vs MapFilterTag. */
	void ApplyMapFilterToLiveNodes();

	// --- U5 delete command (the SINGLE row-deletion funnel — the Delete key is its only caller this
	// PR; the suicide hook stays cleanup-only) ---
	void DeleteSelectedNodes();
	bool CanDeleteSelectedNodes() const;
	void RenameSelectedComment();
	bool CanRenameSelectedComment() const;
	void SelectAllNodes();
	bool CanSelectAllNodes() const { return true; }

	// --- Comment boxes (Blueprint-graph-style annotations, scoped to the open group's graph) ---
	/** The store key for the currently-open group's comments: the active group tag string,
	 *  "__unassigned__" for the unassigned bucket, or empty for the board/full composition view. */
	FString GetCommentScopeKey() const;
	bool CanAddComment() const;
	/** C-key: create a comment in the current group (wrapping the selected nodes if any). */
	void AddCommentForCurrentGroup();
	/** Write-through: persist a comment's move/resize/text to AnimationMapComments. The node is the plain
	 *  engine UEdGraphNode_Comment because a plugin subclass is unlinkable on UE 5.0-5.4; gesture hooks
	 *  live on SPaper2DPlusAnimationMapCommentNode. No-ops when the store already matches the node. */
	void HandleCommentChanged(UEdGraphNode_Comment* CommentNode);
	/** Persist Details-panel edits (Comment Color etc.) from the global property-changed broadcast,
	 *  filtered to comment nodes belonging to this panel's graph. */
	void HandleCommentObjectPropertyChanged(UObject* Object, FPropertyChangedEvent& Event);
	/** Reconcile sync: spawn/remove/refresh the current group's comment nodes from the store (NOT in
	 *  LiveMovesByLower — comments are independent of the move/edge projection). */
	void MaterializeCommentsForCurrentGroup(UPaper2DPlusCharacterProfileAsset* Asset);
	void CollectLiveComments(TArray<UEdGraphNode_Comment*>& OutComments) const;
	FDelegateHandle CommentPropertyChangedHandle;

	// --- TASK-96 P3: cross-view selection sync (replaces the retired U6 in-graph details strip) ---
	// A single node selection drives the SHARED SProfileDetailsPanel through the model (a move node
	// selects its own flipbook; an edge selects its FromMove's flipbook). Conversely the Map FOLLOWS the
	// model's OnFlipbookSelectionChanged by focusing the matching move node — deferred to a paint-time
	// active timer (a hidden switcher slot does not paint, so the follow waits and applies when the Map
	// is next shown, after any pending reconcile and never mid-gesture).
	void HandleGraphSelectionChanged(const FGraphPanelSelectionSet& NewSelection);
	void HandleModelFlipbookSelectionChanged(int32 NewIndex);
	void FocusMoveNodeByFlipbookIndex(int32 FlipbookIndex);
	void QueueFocusMove();
	EActiveTimerReturnType OnFocusMoveTimer(double InCurrentTime, float InDeltaTime);
	int32 FindFlipbookIndexByName(const FString& MoveName) const;

	/** Guards the two-way selection sync against self-echo: set true while pushing a graph selection into
	 *  the model OR while focusing a node from a model selection, so the paired handler no-ops. */
	bool bSuppressSelectionSync = false;
	/** Latest flipbook index the Map should focus (model->Map follow); applied by OnFocusMoveTimer once
	 *  the Map paints and no gesture is live. INDEX_NONE = nothing pending. */
	int32 PendingFocusFlipbookIndex = INDEX_NONE;
	bool bFocusTimerQueued = false;
	FDelegateHandle ModelFlipbookSelectionHandle;

	/** Tag Colors registry change subscription — repaints this panel's tag-colored surfaces when a tag's
	 *  color changes (via the in-editor swatch or the Project Settings table). */
	FDelegateHandle TagColorsChangedHandle;
	/** The registry-change handler: rebuild the group board's tiles AND repaint the canvas rail's
	 *  _Lambda-bound active-filter chip, so no tag-colored surface can keep a stale color. */
	void HandleTagColorsChanged();
	/** Opens a modal color picker for a group's tag and commits to the project-wide Tag Colors registry. */
	void OpenAnimationMapGroupColorPicker(FGameplayTag InTag, FLinearColor InCurrentColor);

	// --- Animation Map ---
	TSharedPtr<SVerticalBox> AnimationMapGroupsBox;
	/** True whenever the graph surface is open. An invalid ActiveAnimationMapGroupTag together with
	 *  !bAnimationMapGroupGraphIsUnassigned denotes the unfiltered full composition graph. */
	bool bAnimationMapGroupGraphOpen = false;
	bool bAnimationMapGroupGraphIsUnassigned = false;
	/** Audit F14: set when a group/unassigned graph is OPENED so the next ReconcileNow frames the scoped
	 *  nodes once (ZoomToFit) instead of leaving the viewport at a stale origin. Cleared by a node-focus jump. */
	bool bPendingFitToView = false;
	FGameplayTag ActiveAnimationMapGroupTag;
	bool bAnimationMapRefreshQueued = false;
	/** Asset signals can arrive twice (explicit model signal + deferred object-modified echo). The
	 *  board rebuilds only when this exact visible-content signature changes. */
	FString LastAnimationMapBoardSignature;
	bool bAnimationMapMigrationPromptQueued = false;

	void QueueAnimationMapRefresh();
	EActiveTimerReturnType OnAnimationMapRefreshTimer(double InCurrentTime, float InDeltaTime);
	void RefreshAnimationMap();
	void OpenAnimationMapFullGraph();
	void OpenAnimationMapGroup(FGameplayTag GroupTag);
	void OpenAnimationMapUnassigned();
	void CloseAnimationMapGroup();
	TSharedRef<SWidget> BuildAnimationMapGroupTile(const Paper2DPlusAnimationMap::FAnimationMapGroup& Group);
	TSharedRef<SWidget> BuildAnimationMapUnassignedTile(int32 UnassignedCount);
	void QueueAnimationMapMigrationPrompt();
	EActiveTimerReturnType OnAnimationMapMigrationPromptTimer(double InCurrentTime, float InDeltaTime);
	bool ShouldShowAnimationMapMigrationPrompt() const;
	void MarkAnimationMapMigrationPromptSeen() const;
	FString GetAnimationMapMigrationPromptConfigKey() const;
	FReply OpenAnimationMapMigrationDialog(bool bMarkPromptSeenOnApply);

	// --- Helpers ---
	UPaper2DPlusCharacterProfileAsset* GetAsset() const;
	FGraphAppearanceInfo GetGraphAppearance() const;
	void RestoreViewFromConfig();
	void SaveViewToConfig() const;
};
