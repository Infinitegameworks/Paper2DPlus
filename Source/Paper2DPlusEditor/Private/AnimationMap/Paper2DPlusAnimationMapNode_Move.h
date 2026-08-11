// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraphNode.h"
#include "GameplayTagContainer.h"
#include "UObject/WeakObjectPtrTemplates.h"
#include "Paper2DPlusAnimationMapNode_Move.generated.h"

class UPaperFlipbook;

/**
 * One move (flipbook) node in the transient Animation Map projection (combo-graph plan U3).
 *
 * Identity is the move NAME (case-insensitive everywhere, like the asset's name semantics); the
 * flipbook/index is resolved per-use via FindFlipbookDataPtr — never cached here. Spawned exclusively
 * through UPaper2DPlusAnimationMap::SpawnNodeUntransactional (the ClearFlags ordering invariant), with
 * bIsStub set BEFORE Finalize so pin allocation sees it.
 *
 * Fields are bare UPROPERTY() — display-only reflection, nothing BP/editor-exposed, so the
 * installed-UHT Category rule (PR #134 Fab gotcha) cannot trip.
 */
UCLASS()
class UPaper2DPlusAnimationMapNode_Move : public UEdGraphNode
{
	GENERATED_BODY()

public:
	/** Move name in DISPLAY case (the asset's authored spelling; the dangling spelling for stubs). */
	UPROPERTY()
	FString MoveName;

	/** Stub = the name resolves to no flipbook on the asset (dangling transition target, or a kept
	 *  placement key whose move was removed) — the validator's lowered-name-set predicate (R3).
	 *  Stubs accept incoming wires but get NO output pin: they cannot own transition rows. */
	UPROPERTY()
	bool bIsStub = false;

	/** True when this move's exact-group mapping entry carries the authored Chain Start flag. The
	 *  visual authoring surface is the wired Chain Start MARKER node (the panel syncs one per flagged
	 *  move); this mirror drives the probe + marker sync. Stamped from the projection. */
	UPROPERTY()
	bool bIsChainStart = false;

	/** True when this move's exact-group mapping entry carries the authored Chain End flag — the
	 *  bIsChainStart mirror (chain-end rework). The visual authoring surface is the wired Chain End
	 *  MARKER node; this mirror drives the probe + the ⏹ glyph. Stamped from the projection. */
	UPROPERTY()
	bool bIsChainEnd = false;

	/** Auto-derived combo MAIN-LINE step (FProjectedNode::ComboSpineIndex — the exact index the
	 *  Get Combo Chain Flipbook at Index BP node returns for this move). INDEX_NONE = on no unique
	 *  chain or a side branch; index 0 = the chain start itself (the ▶ glyph/marker already marks it,
	 *  so the #N chip renders only for positive indices). Stamped from the projection during
	 *  reconcile; display-only. */
	UPROPERTY()
	int32 ComboSpineIndex = INDEX_NONE;

	/** The main line's total length (0 when ComboSpineIndex is INDEX_NONE). */
	UPROPERTY()
	int32 ComboSpineLength = 0;

	/** TASK-108 U6 (R6) display state: per-provenance animation tags, DENORMALIZED from the projection
	 *  (FProjectedNode's own/chain/group containers) by the panel's reconcile stamp loop — the widget
	 *  renders chips from these without ever touching the asset from paint. A tag edit re-stamps via
	 *  the projection's `AnimationTagChanges` diff + NotifyNodeChanged. Display-only, never identity. */
	UPROPERTY()
	FGameplayTagContainer OwnAnimationTags;
	UPROPERTY()
	FGameplayTagContainer ChainInheritedAnimationTags;
	UPROPERTY()
	FGameplayTagContainer GroupImpliedAnimationTags;

	/** Own descriptive phase, mirrored for context-menu and cohort editing. */
	UPROPERTY()
	FGameplayTag PhaseTag;

	/** Own ∪ chain-inherited ∪ group-implied — the node-side mirror of
	 *  FProjectedNode::EffectiveAnimationTags() (AnimationMapCore.h), so filter re-stamps over LIVE
	 *  nodes union the same three containers the projection's dim stamp does (never a manual fork). */
	FGameplayTagContainer EffectiveAnimationTags() const
	{
		FGameplayTagContainer Out = OwnAnimationTags;
		Out.AppendTags(ChainInheritedAnimationTags);
		Out.AppendTags(GroupImpliedAnimationTags);
		return Out;
	}

	/** TASK-108 U6 (R8) display state: true when the Map's tag filter is active and this node's
	 *  EFFECTIVE tags do not match it — the widget renders the whole node translucent (never hidden:
	 *  wires to dimmed nodes stay visible). Stamped by the panel (reconcile stamp loop + on every
	 *  filter change); the widget reads it per paint via _Lambda bindings, so a re-stamp needs no
	 *  widget rebuild. Display-only, never identity. */
	UPROPERTY()
	bool bDimmedByFilter = false;

	/** DISPLAY STATE, not identity (U6): the flipbook OBJECT the node's thumbnail renders, stamped by
	 *  the panel during reconcile (one LoadSynchronous-on-first-display per move — the transitions-row
	 *  idiom) and re-stamped when the resolved object changes (reimport). DELIBERATELY a plain
	 *  non-UPROPERTY weak member: the graph PINS NOTHING (the U3 no-rooting invariant) — the widget's
	 *  own SFlipbookThumbnail holds the strong ref while it displays. Never read for identity; the
	 *  move NAME stays the only identity (resolved per-use against the asset). */
	TWeakObjectPtr<UPaperFlipbook> ResolvedFlipbook;

	//~ Begin UEdGraphNode interface
	virtual void AllocateDefaultPins() override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	/** U6 stub warning styling: stubs get a warning title bar / warm body tint (the validator's
	 *  dangling-target Warning made visible); real moves keep a neutral dark title. */
	virtual FLinearColor GetNodeTitleColor() const override;
	virtual FLinearColor GetNodeBodyTintColor() const override;
	virtual bool CanUserDeleteNode() const override { return true; }
	virtual bool CanDuplicateNode() const override { return false; }
	/** Real moves expose the group reassignment context action; stubs own no mapping entry. */
	virtual void GetNodeContextMenuActions(UToolMenu* Menu, UGraphNodeContextMenuContext* Context) const override;
	/** U5/U6: the move-node widget (SAnimationMapMoveNode) — 64x64 flipbook-thumbnail body (U6) + the
	 *  bMarkDirty-gated MoveTo override (R11 position persistence). Resolved FIRST by
	 *  FNodeFactory::CreateNodeWidget (NodeFactory.cpp:88-98), so no global factory registration. */
	virtual TSharedPtr<SGraphNode> CreateVisualWidget() override;
	//~ End UEdGraphNode interface

	/** The visible input pin. Null-safe lookup by direction (never by index). */
	UEdGraphPin* GetInputPin() const;

	/** The visible output pin — nullptr for stubs (no output pin exists; callers must null-check). */
	UEdGraphPin* GetOutputPin() const;
};
