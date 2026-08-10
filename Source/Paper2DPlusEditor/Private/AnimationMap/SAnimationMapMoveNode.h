// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/EngineVersionComparison.h" // ENGINE_*_VERSION macros for the cross-version guards (U7) - explicit, not PCH-order-dependent
#include "SGraphNode.h"
#include "Styling/SlateColor.h"

class SOverlay;
class SBox;
class UPaper2DPlusAnimationMapNode_Move;

/** 5.6 FVector2f sweep guard (combo-graph plan U7/A1): SNodePanel::SNode::MoveTo takes FVector2D
 *  through 5.5 (SNodePanel.h:542 in 5.0, :570 in 5.5) and FVector2f from 5.6 (:617) — where the
 *  FVector2D virtual is `final` under -WarningsAsErrors (UE_SLATE_DEPRECATED_VECTOR_VIRTUAL_FUNCTION,
 *  SlateVector2.h:52), so each version must override EXACTLY its native signature. The alias keeps
 *  declaration + definition to ONE body (no duplicated logic across guards). */
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6)
using FAnimationMapMoveNodeVector = FVector2f;
#else
using FAnimationMapMoveNodeVector = FVector2D;
#endif

/**
 * Move-node widget for the Animation Map (combo-graph plan U5 + U6 visuals). The node is a clone of the
 * anim state machine's WHOLE-NODE-BODY pin shape (SGraphNodeAnimState::UpdateGraphNode,
 * SGraphNodeAnimState.cpp:202-353; the same shape PaperZD's SPaperZDStateGraphNode_State clones):
 * the widget is SelfHitTestInvisible; one rounded Graph.StateNode.Body border holds an SOverlay whose
 * FIRST slot is a Fill/Fill PIN AREA (both pins overlay-filled across the whole node via the
 * SAnimationMapNodePin full-body pin — see the .cpp) and whose SECOND slot is the centered CONTENT
 * PLATE (Graph.StateNode.ColorSpill tinted by the node's GetNodeTitleColor — stub warning orange
 * preserved — carrying the title + the 64x64 SFlipbookThumbnail well; stubs keep the 2px warning
 * frame). All brushes verified present 5.0-5.7 (StarshipStyle.cpp — 5.7:4029/4030/4037/4038,
 * 5.0:3590/3591/3597/3598).
 *
 * HIT-TEST ROUTING (the anim-SM behavior, stated per the U6 polish contract):
 *  - The CONTENT PLATE (title + thumbnail) is hit-testable and handles nothing itself, so LMB-drag
 *    there bubbles to SGraphPanel = node SELECT + MOVE (and RMB = node context menu).
 *  - The BORDER RING around the plate (the overlay padding) belongs to the full-body pin: LMB-drag
 *    there starts a WIRE (SGraphPin::OnPinMouseDown), hover swaps in the
 *    Graph.StateNode.Pin.BackgroundHovered cue, and wire DROPS land on the pin (out->out resolves via
 *    the schema's same-direction swap; a drag from the ring dropped back on the SAME node forms the
 *    legal self-transition, R14). Drops exactly on the content plate bubble to
 *    FDragConnection::DroppedOnNode -> no SupportsDropPinOnNode -> no-op, engine-identical.
 *  - STUBS have no output pin (U3): their overlay carries the INPUT pin in the interactive full-body
 *    form instead, so a stub still hover-highlights and still accepts drops; a drag FROM a stub's ring
 *    resolves to an incoming row on the other move (stubs still own no rows).
 *  - Hovering the node adds the output pin's links to the panel hover set (clone of
 *    SGraphNodeAnimState.cpp:162-200) so OUTGOING wires + pills light up; null-safe for stubs.
 *
 * Field refresh route: the thumbnail and tag chips each live inside a stable SBox host. The panel
 * replaces only the affected host child after re-stamping the transient node, so tag edits never
 * recreate SFlipbookThumbnail and thumbnail reloads never recreate the whole move card.
 *
 * MoveTo contract — VERIFIED 5.7 SNodePanel drag topology: during a drag, per-mouse-move calls pass
 * bMarkDirty=false with NO transaction open (SNodePanel.cpp:921-949); on mouse-up/capture-lost,
 * FinalizeNodeMovements first RESTORES originals untransacted (bMarkDirty=false), then opens the single
 * NodeMoveTransaction and re-applies finals with exactly ONE bMarkDirty=true call per node
 * (SNodePanel.cpp:1906-1961). A SECOND bMarkDirty=true producer exists: the arrow-key nudge
 * (SGraphPanel::UpdateSelectedNodesPositions, SGraphPanel.cpp:797-830) — also engine-transacted
 * ("Nudge Node"), one call per press. Super runs ALWAYS (live drag visuals; the node is
 * non-transactional by the spawn-funnel invariant, so the base's GraphNode->Modify(bMarkDirty) records
 * nothing); the position-map persist fires ONLY on bMarkDirty=true — guaranteed in-transaction, one
 * recorded write per gesture, capture-lost handled by the engine.
 */
class SAnimationMapMoveNode : public SGraphNode
{
public:
	SLATE_BEGIN_ARGS(SAnimationMapMoveNode) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UPaper2DPlusAnimationMapNode_Move* InNode);

	//~ Begin SNodePanel::SNode interface
	// FAnimationMapMoveNodeVector = FVector2f on 5.6+, FVector2D before (the 5.6 sweep guard above).
	virtual void MoveTo(const FAnimationMapMoveNodeVector& NewPosition, FNodeSet& NodeFilter, bool bMarkDirty = true) override;
	//~ End SNodePanel::SNode interface

	//~ Begin SWidget interface
	/** Node hover lights every OUTGOING wire/pill via the panel hover set (anim-SM clone). */
	virtual void OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void OnMouseLeave(const FPointerEvent& MouseEvent) override;
	//~ End SWidget interface

	/** Replace only the thumbnail well after ResolvedFlipbook changes. */
	void RefreshThumbnail();

	/** Replace only the tag-chip row after provenance changes; the thumbnail stays alive. */
	void RefreshTagChips();

protected:
	//~ Begin SGraphNode interface
	/** The full clone of the anim-SM node shape (see the class comment) — the base default-node build
	 *  (title bar + LeftNodeBox/RightNodeBox side pins) is fully replaced, so the old
	 *  CreateNodeContentArea override is absorbed here (its thumbnail well lives in MakeThumbnailWell). */
	virtual void UpdateGraphNode() override;
	/** Output pin first, input second (engine order) — both as SAnimationMapNodePin; null-safe for the
	 *  stub's missing output pin (U3). */
	virtual void CreatePinWidgets() override;
	/** Slots every pin HAlign_Fill/VAlign_Fill into the full-node PinOverlay (engine clone). */
	virtual void AddPin(const TSharedRef<SGraphPin>& PinToAdd) override;
	//~ End SGraphNode interface

private:
	/** Body-border tint: anim-SM inactive dark for real moves; a warm warning cast for stubs (the old
	 *  GetNodeBodyTintColor stub signal, preserved under the new body brush). */
	FSlateColor GetBodyBorderColor() const;

	/** Content-plate (ColorSpill) tint: the node's GetNodeTitleColor stays the single source — stub
	 *  warning orange / real neutral dark, exactly what the old standard title bar showed. */
	FSlateColor GetSpillColor() const;

	/** The U6 (R1) 64x64 SFlipbookThumbnail well (stub 2px warning frame / real dark inset frame) —
	 *  the old CreateNodeContentArea body minus the side pin boxes. */
	TSharedRef<SWidget> MakeThumbnailWell();

	/** TASK-108 U6 (R6): the animation-tag chips row under the thumbnail (shared
	 *  Paper2DPlusAnimationTagChips visuals, built from the node's denormalized containers). */
	TSharedRef<SWidget> MakeTagChipsRow();

	/** The full-node pin layer (anim-SM clone, SGraphNodeAnimState.h:57): every pin is an overlay
	 *  fill across the whole node body, stacked under the content plate. */
	TSharedPtr<SOverlay> PinOverlay;

	/** Stable field-only refresh hosts. */
	TSharedPtr<SBox> ThumbnailHost;
	TSharedPtr<SBox> TagChipsHost;
};
