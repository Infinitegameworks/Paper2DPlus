// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Paper2DPlusMoveTransition.h"

class UPaper2DPlusCharacterProfileAsset;
enum class EAnimationPhase : uint8;

/**
 * Pure, Slate-free projection / diff / write-through core for the Animation Map tab (combo-graph plan
 * U3 — the named-risk sync layer). The Paper2DPlusCurveTracks namespace (CurveTrackPanel.h; preceded
 * by the retired Curves tab's Paper2DPlusCurveGraph) is the precedent:
 * free functions, asset + plain values in -> values out, worldless-testable
 * (Paper2DPlusAnimationMapTest.cpp), and deliberately NO UEdGraph/Slate types in this header so both the
 * tests and the U4 panel consume the same seam.
 *
 * Contract pillars (Key Technical Decisions):
 *  - The flat FFlipbookTransitionData is the SINGLE source of truth; the graph is a projection.
 *  - Node set = {position-map keys} u {moves owning >= 1 row} u {row targets}; names compare
 *    case-insensitively ("Jab"/"jab" = one node); degenerate empty names are excluded entirely.
 *  - The stub predicate REUSES the validator's lowered flipbook-name-set logic
 *    (ValidateCharacterProfileAsset, Paper2DPlusCharacterProfileAsset.cpp:2275-2288) — never a
 *    re-derived second predicate.
 *  - Rows with an EMPTY TargetMove are authoring-in-progress: they project NO edge and are never
 *    touched by any helper except whole-move deletion (the data layer's empty-target skip rule).
 *  - Write-through APPENDS only, never inserts/reorders. Mutations here are PURE data edits: NO
 *    transactions, NO Modify(), NO broadcasts — the panel owns all of those around these calls.
 */
namespace Paper2DPlusAnimationMap
{
	// =====================================================================================
	// Projection value types
	// =====================================================================================

	/** One projected move node. Identity = MoveNameLower; DisplayName keeps the asset's authored
	 *  case for real moves (the row/key spelling for stubs). */
	struct FProjectedNode
	{
		FString MoveNameLower;
		FString DisplayName;
		bool bIsStub = false;
		/** True when this move's exact-group mapping entry carries the authored Chain Start flag —
		 *  rendered as a wired Chain Start marker node beside the move. Never structural inference. */
		bool bIsChainStart = false;

		/** True when this move's exact-group mapping entry carries the authored Chain End flag —
		 *  rendered as a wired Chain End marker node beside the move. The derived main line stops AT
		 *  a flagged end (inclusive), so moves wired after it project no combo step. */
		bool bIsChainEnd = false;

		/** The move's own descriptive phase tag. Display/edit state for map context and multi-selection;
		 *  transition pills separately mirror their TARGET move's phase. */
		FGameplayTag PhaseTag;

		/** Auto-derived combo MAIN-LINE position (Paper2DPlusComboChain::DeriveComboSpine — the same
		 *  numbering the Get Combo Chain Flipbook at Index BP node returns, so the editor can never
		 *  disagree with gameplay). INDEX_NONE when the move is on no unique chain or only on a side
		 *  branch; stamped only when exactly ONE chain start profile-wide reaches the move (the
		 *  runtime's AmbiguousChain fail-closed mirrored). ComboSpineLength is that line's length. */
		int32 ComboSpineIndex = INDEX_NONE;
		int32 ComboSpineLength = 0;

		/** TASK-108 U6 (R6): per-node animation-tag provenance, resolved by ProjectGraph from ONE
		 *  `Paper2DPlusAnimationTagQuery::BuildAnimationTagMap` batch per projection (real moves only;
		 *  stubs stay empty). Own = authored on the entry (solid chip); ChainInherited = the OWN tags of
		 *  reaching chain roots (ghosted chip); GroupImplied = TagMappings group keys (outlined chip).
		 *  Effective = the union — the Map filter bar's match dimension. In operator== (and the diff's
		 *  AnimationTagChanges) so a tag edit re-stamps the node's chips without a topology rebuild. */
		FGameplayTagContainer OwnAnimationTags;
		FGameplayTagContainer ChainInheritedAnimationTags;
		FGameplayTagContainer GroupImpliedAnimationTags;

		/** Own ∪ chain-inherited ∪ group-implied (the batch's EffectiveTags — exactly what
		 *  bIncludeInherited=true BP queries match, so filter dimming can't drift from query results). */
		FGameplayTagContainer EffectiveAnimationTags() const
		{
			FGameplayTagContainer Out = OwnAnimationTags;
			Out.AppendTags(ChainInheritedAnimationTags);
			Out.AppendTags(GroupImpliedAnimationTags);
			return Out;
		}

		bool operator==(const FProjectedNode& Other) const
		{
			return bIsStub == Other.bIsStub
				&& bIsChainStart == Other.bIsChainStart
				&& bIsChainEnd == Other.bIsChainEnd
				&& ComboSpineIndex == Other.ComboSpineIndex
				&& ComboSpineLength == Other.ComboSpineLength
				&& PhaseTag == Other.PhaseTag
				&& MoveNameLower.Equals(Other.MoveNameLower, ESearchCase::CaseSensitive)
				&& DisplayName.Equals(Other.DisplayName, ESearchCase::CaseSensitive)
				&& OwnAnimationTags == Other.OwnAnimationTags
				&& ChainInheritedAnimationTags == Other.ChainInheritedAnimationTags
				&& GroupImpliedAnimationTags == Other.GroupImpliedAnimationTags;
		}
		bool operator!=(const FProjectedNode& Other) const { return !(*this == Other); }
	};

	/** One projected transition row as a value tuple. IDENTITY = (From, To) since TASK-108 U2: the
	 *  U1 load-time dedupe + the U2 wire-create refusal make one-row-per-(From, To) an invariant, so
	 *  the row index is no longer part of edge identity (ProjectGraph additionally CANONICALIZES —
	 *  at most one edge per pair even when a raw Details-array edit re-creates an in-memory
	 *  duplicate mid-session; the surviving duplicate row still projects the pair's one edge, and
	 *  the next load dedupes it away). */
	struct FProjectedEdge
	{
		FString FromMoveLower;
		FString TargetMoveLower;

		/** The transition's effective phase: its row-owned override, or the target animation's PhaseTag
		 *  when the override is empty (legacy compatibility). Display-only here: the row snapshot guard
		 *  intentionally remains identity-based, so an in-flight delete/retarget gesture is not aborted
		 *  by a concurrent descriptive phase edit. Included in full value equality; DiffProjection
		 *  reports phase-only changes separately so the existing pill is re-stamped in place. */
		FGameplayTag TransitionPhaseTag;

		bool operator==(const FProjectedEdge& Other) const
		{
			return TransitionPhaseTag == Other.TransitionPhaseTag
				&& FromMoveLower.Equals(Other.FromMoveLower, ESearchCase::CaseSensitive)
				&& TargetMoveLower.Equals(Other.TargetMoveLower, ESearchCase::CaseSensitive);
		}
		bool operator!=(const FProjectedEdge& Other) const { return !(*this == Other); }
	};

	/** The full projection: nodes in deterministic order (Flipbooks ARRAY order interleaved with
	 *  first-appearance row targets, then SORTED placed-only keys — never raw TMap iteration order),
	 *  edges in row-array order. */
	struct FGraphProjection
	{
		TArray<FProjectedNode> Nodes;
		TArray<FProjectedEdge> Edges;
	};

	/** Project the asset's flat transition data (+ editor-only placement keys) into graph values.
	 *  Duplicate same-named Flipbooks entries (a validation Error) follow the first-match contract:
	 *  only the FIRST entry per lowered name contributes rows. Null asset -> empty projection.
	 *  Optional MoveFilterLower scopes the projection to a group surface: filtered real moves project
	 *  as nodes even without rows/placements, and edges only project when both ends are in the filter.
	 *  ExactGroupScope supplies the matching TagMappings key for Chain Start projection; pass null with
	 *  a filter for the unassigned surface (no chain starts). */
	FGraphProjection ProjectGraph(const UPaper2DPlusCharacterProfileAsset* Asset,
		const TSet<FString>* MoveFilterLower = nullptr,
		const FGameplayTag* ExactGroupScope = nullptr);

	// =====================================================================================
	// Full diff — U4's reconcile early-out gate
	// =====================================================================================

	/**
	 * Every dimension U4's gate must see (KTD: a tuples-and-names-only gate would swallow
	 * position-only undos, stub promotion, and thumbnail refresh — reconcile may skip graph mutation
	 * ONLY when IsEmpty()). bAnyPositionDrift / bAnyFlipbookPtrChanged are CALLER-SUPPLIED (the panel
	 * compares live NodePosX/Y against the stored map, and resolved flipbook object pointers against
	 * the node widgets' last-known objects) — keeping them parameters keeps this core pure and
	 * UEdGraph-free.
	 */
	struct FProjectionDiff
	{
		/** Lowered names present in Current but not Previous. */
		TArray<FString> MovesToAdd;

		/** Lowered names present in Previous but not Current. */
		TArray<FString> MovesToRemove;

		/** Lowered names present in both whose bIsStub differs (stub promotion/demotion in place). */
		TArray<FString> StubFlagFlips;

		/** Lowered names present in both whose DISPLAY case changed (case-only rename) — surviving
		 *  in-place nodes need a title refresh the lowered identity can't see. */
		TArray<FString> DisplayNameChanges;

		/** Lowered names present in both whose exact-group Chain Start OR Chain End flag changed —
		 *  the surviving node's markers need re-syncing (the move node/edge set is unchanged). */
		TArray<FString> ChainStartChanges;

		/** Lowered names present in both whose derived combo main-line index/length changed — the
		 *  surviving node needs its step badge re-stamped/refreshed (a transition edit elsewhere in
		 *  the chain can renumber a node whose own rows are untouched). */
		TArray<FString> ComboSpineChanges;

		/** Lowered names present in both whose animation-tag provenance changed (TASK-108 U6) — the
		 *  surviving node needs its tag chips re-stamped/refreshed (a tag edit changes no topology). */
		TArray<FString> AnimationTagChanges;

		/** Lowered real moves whose own descriptive phase changed. */
		TArray<FString> PhaseTagChanges;

		/** Current edge values whose (From, To) identity survived while the target's descriptive
		 *  PhaseTag changed. Identity matching is independent of authored
		 *  row order, so a reorder can restamp the same surviving pill instead of recreating it. */
		TArray<FProjectedEdge> EdgePhaseChanges;

		/** Edge identities present only in Current. The panel creates exactly these pills. */
		TArray<FProjectedEdge> EdgesToAdd;

		/** Edge identities present only in Previous. The panel removes exactly these pills. */
		TArray<FProjectedEdge> EdgesToRemove;

		/** Edge identity lists compare equal in authored order. False with empty add/remove lists means
		 *  the same edge objects merely changed row order and only their row handles need restamping. */
		bool bEdgesEqual = true;

		/** Caller-supplied: any surviving node's stored position differs from its live position. */
		bool bAnyPositionDrift = false;

		/** Caller-supplied: any surviving node's resolved flipbook OBJECT changed (e.g. reimport). */
		bool bAnyFlipbookPtrChanged = false;

		/** THE early-out gate: true only when every dimension is clean. */
		bool IsEmpty() const
		{
			return MovesToAdd.Num() == 0
				&& MovesToRemove.Num() == 0
				&& StubFlagFlips.Num() == 0
				&& DisplayNameChanges.Num() == 0
				&& ChainStartChanges.Num() == 0
				&& ComboSpineChanges.Num() == 0
				&& AnimationTagChanges.Num() == 0
				&& PhaseTagChanges.Num() == 0
				&& EdgePhaseChanges.Num() == 0
				&& EdgesToAdd.Num() == 0
				&& EdgesToRemove.Num() == 0
				&& bEdgesEqual
				&& !bAnyPositionDrift
				&& !bAnyFlipbookPtrChanged;
		}
	};

	/** Diff two projections (Previous = the prior projection or a snapshot of the live graph) and
	 *  fold in the caller-supplied position/flipbook dimensions (see FProjectionDiff). */
	FProjectionDiff DiffProjection(const FGraphProjection& Previous, const FGraphProjection& Current,
		bool bAnyPositionDrift = false, bool bAnyFlipbookPtrChanged = false);

	/** Resolve the deterministic target names for a card-invoked bulk edit. If the requested real move
	 *  is part of SelectedRealMoveLowers, every selected real projected move is returned in projection
	 *  order; otherwise only the requested real projected move is returned. Stubs and unknown names
	 *  are always omitted. This pure seam keeps context-menu and cohort-card targeting testable. */
	TArray<FString> ResolveMultiEditTargetNames(const FString& RequestedMoveLower,
		const TSet<FString>& SelectedRealMoveLowers, const FGraphProjection& Projection);

	// =====================================================================================
	// Write-through helpers — pure mutations; the panel owns transactions/Modify/broadcasts
	// =====================================================================================

	/** Row equivalence for the write-side guard — case-insensitive TargetMove. Case-insensitive
	 *  matching follows the data layer and avoids a case-only stale-handle deadlock. */
	bool TransitionRowsEquivalent(const FPaper2DPlusMoveTransition& A, const FPaper2DPlusMoveTransition& B);

	/** The CURRENT index of FromMove's first row targeting TargetMove (both case-insensitive, the
	 *  first-match entry contract), or INDEX_NONE. The (From, To) pair-existence probe (the U2
	 *  write-time invariant's pre-check) AND the reconcile's row resolve (the projection no longer
	 *  carries a row index). Post-U1 there is at most one such row per pair. */
	int32 FindTransitionRowIndex(const UPaper2DPlusCharacterProfileAsset* Asset, const FString& FromMove,
		const FString& TargetMove);

	/** Append a row targeting TargetMove (display case preserved) onto FromMove's FIRST
	 *  case-insensitive entry match. APPEND only — never insert/reorder. False when the asset/names
	 *  are empty/missing (a stub owns no entry and can never gain rows) OR when a (From, To) row
	 *  already exists (KTD: one-row-per-pair is enforced at EVERY row-creation path, so it holds by
	 *  construction mid-session, not only after the next load's dedupe). */
	bool AppendTransitionRow(UPaper2DPlusCharacterProfileAsset* Asset, const FString& FromMove,
		const FString& TargetMove);

	/** The write-side guard (KTD: node identity): remove FromMove's row at RowIndex ONLY when the
	 *  index is valid AND the live row is still equivalent to ExpectedSnapshot (the edge node's
	 *  denormalized snapshot). Returns true on removal; false aborts the gesture untouched — the
	 *  caller then reconciles. */
	bool RemoveTransitionRowChecked(UPaper2DPlusCharacterProfileAsset* Asset, const FString& FromMove,
		int32 RowIndex, const FPaper2DPlusMoveTransition& ExpectedSnapshot);

	/** Counts for RemoveAllRowsForMove — the node-delete confirm dialog's numbers (AE2/AE7). */
	struct FRemoveMoveResult
	{
		/** Rows removed from the move's own Transitions array (self-loops count here, once). */
		int32 OutgoingRemoved = 0;

		/** Rows on OTHER moves targeting the deleted name (case-insensitive), removed descending. */
		int32 IncomingRemoved = 0;

		/** Whether an editor-only placement entry existed and was removed. */
		bool bPositionEntryRemoved = false;
	};

	/** Node-delete pruning (R7): clears the move's outgoing rows (first-match entry), removes every
	 *  row on any entry targeting it (case-insensitive, descending by index), and drops its
	 *  position-map entry. The MOVE ITSELF stays on the profile (AE2). Stub deletion flows through
	 *  the same call — a stub owns no entry, so OutgoingRemoved is 0 (incoming + placement only). */
	FRemoveMoveResult RemoveAllRowsForMove(UPaper2DPlusCharacterProfileAsset* Asset, const FString& MoveName);

	/** Counts for the node-delete confirm dialog (U5, AE2/AE7) — the COUNT-ONLY mirror of
	 *  RemoveAllRowsForMove, pinned test-equal to its actual removal counts on the same data:
	 *  OutgoingRows = the first-match entry's WHOLE row array (incl. empty-target authoring rows
	 *  invisible as wires — which is why the dialog copy says "rows", never "transitions");
	 *  IncomingRows = rows targeting the name case-insensitively on every OTHER entry (self-loops
	 *  count exactly once, as outgoing — the removal's wipe-outgoing-first order). */
	struct FMoveRowCounts
	{
		int32 OutgoingRows = 0;
		int32 IncomingRows = 0;
		bool bPlacementExists = false;
		int32 TotalRows() const { return OutgoingRows + IncomingRows; }
	};

	/** Pure count — mutates nothing. Null asset / empty name -> all-zero counts. */
	FMoveRowCounts CountRowsForMove(const UPaper2DPlusCharacterProfileAsset* Asset, const FString& MoveName);

	/** Deterministic per-index offset for multi-move drop placement (R2): index 0 lands exactly on the
	 *  drop point; later indices cascade diagonally so a batch never stacks exactly (every index maps
	 *  to a distinct offset). Pure math — worldless-tested. */
	FVector2D DropFanOffset(int32 PlacementIndex);

	// =====================================================================================
	// First layout (R4) — PURE: computes positions, never writes the map, never dirties
	// =====================================================================================

	/** Deterministic grid positions (keyed by lowered name) for every projected node WITHOUT an entry
	 *  in OccupiedPositions (typically the asset's position map merged with the panel's session
	 *  cache; keys lowercased). When MoveFilterLower is supplied, both occupancy and its bounding box
	 *  are scoped to that filter, so positions in another group cannot displace a new arrival. Order
	 *  derives from the Flipbooks ARRAY (never TMap iteration order); new arrivals place clear of the
	 *  occupied bounding box (one cell below it) without moving any stored entry. ZERO map writes,
	 *  zero dirty — opening the tab never dirties the asset. */
	TMap<FString, FVector2D> FirstLayout(const UPaper2DPlusCharacterProfileAsset* Asset,
		const TMap<FString, FVector2D>& OccupiedPositions, const TSet<FString>* MoveFilterLower = nullptr);

	// =====================================================================================
	// U6 visual / selection helpers — PURE (Slate-free), worldless-tested
	// =====================================================================================

	/** Value-tuple equality for edges, IGNORING TransitionPhaseTag (the re-match dimension): From/Target
	 *  lowered strings compare case-insensitively. Deliberately LOOSER than FProjectedEdge::operator==
	 *  (the display-faithful full-diff gate) — a selection must survive a concurrent phase edit.
	 *  (The order-badge / condition-glyph subsystem was deleted with the label/condition/cancel
	 *  fields, TASK-108 U1; RowIndex left edge identity in U2.) */
	bool EdgeValueTuplesEqual(const FProjectedEdge& A, const FProjectedEdge& B);

	/** WS3 U4/U5: a colored chip for a phase tag — THE single shared phase-color source (the transition
	 *  pill's "entering phase" badge AND the List view's per-row tint both call this, so they cannot
	 *  disagree, and the new Active=green convention lives in exactly one place). Label is the tag's LEAF
	 *  segment ("Active" for Paper2DPlus.Phase.Active), so ANY phase tag — the 10 registered ones or a
	 *  user-authored phase — renders. Color is keyed by leaf with a neutral gray fallback for unknown
	 *  leaves; TextColor is the luminance-contrasting fill for text drawn ON the colored chip. An empty
	 *  tag yields an empty Label (callers hide the badge). Pure / worldless-testable. */
	struct FPhaseTagBadge
	{
		FString Label;
		FLinearColor Color = FLinearColor::Gray;
		FLinearColor TextColor = FLinearColor::White;
		bool IsValid() const { return !Label.IsEmpty(); }
	};
	FPhaseTagBadge GetPhaseTagBadge(const FGameplayTag& PhaseTag);

	/**
	 * TASK-108 U6 (R11): THE single shared chip/badge color resolution for ANY tag surfaced by the
	 * editor (browser card chips, Map node chips, group tiles wanting a family default). Precedence —
	 * pinned by Paper2DPlus.AnimationTagSurfacing.ColorPrecedence:
	 *   1. Tag Colors registry, EXACT entry (`UPaper2DPlusSettings::ResolveTagColor`),
	 *   2. Tag Colors registry, nearest ANCESTOR entry (same call — its documented fall-up),
	 *   3. code-level CONVENTION fallback palettes: `Paper2DPlus.Phase.*` = the GetPhaseTagBadge leaf
	 *      table (the SAME seam — never a fork), `Paper2DPlus.Animation.Combat.*` = warm ember family,
	 *      `Paper2DPlus.Animation.Context.*` = cool sky family (per-leaf entries for the native
	 *      taxonomy, a family base for user-authored children),
	 *   4. neutral gray for everything else.
	 * NEVER seeds registry rows (the registry stays a pure override list — the CDO-constructor
	 * tag-manager-init hazard, see ue-gameplay-tag-color-registry-patterns.md §1). TextColor is the
	 * luminance-contrasting fill for text drawn ON the color. Pure / worldless-testable.
	 */
	struct FTagChipColor
	{
		FLinearColor Color = FLinearColor(0.45f, 0.45f, 0.50f);
		FLinearColor TextColor = FLinearColor::White;
	};
	FTagChipColor GetAnimationTagChipColor(const FGameplayTag& InTag);

	/**
	 * TASK-108 U6 (R7): the browser search predicate's tag dimension — true when Query (already
	 * trimmed) substring-matches the flipbook NAME (the pre-existing behavior), OR any effective tag's
	 * LEAF name, OR any effective tag's FULL path — all case-insensitive. An empty query matches
	 * everything. Pure / worldless-testable; the browser feeds EffectiveTags from its per-refresh
	 * BuildAnimationTagMap batch.
	 */
	bool AnimationSearchMatches(const FString& FlipbookName, const FGameplayTagContainer& EffectiveTags,
		const FString& Query);

	/**
	 * TASK-108 U6 (R8): the Map tag-filter match — true when the container carries InFilterTag or a
	 * DESCENDANT of it (hierarchical, `FGameplayTagContainer::HasTag`: filter `Combat` matches a node
	 * tagged `Combat.Heavy`). An INVALID filter matches everything (filter off). Pure.
	 */
	bool AnimationTagFilterMatches(const FGameplayTagContainer& EffectiveTags, const FGameplayTag& InFilterTag);

	/**
	 * TASK-108 U6 (R16): the animation's HOME TagMappings group — the FIRST mapping in TAG-NAME-SORTED
	 * key order whose Entries contains FlipbookName case-insensitively, matching ProjectAnimationMap's
	 * single-home dedup (the browser card's GetHomeAnimationTag delegates here). Invalid tag when
	 * unmapped/null.
	 * The Change-Group no-op guard compares this against the picked target BEFORE any transaction.
	 */
	FGameplayTag FindHomeAnimationGroupTag(const UPaper2DPlusCharacterProfileAsset* Asset,
		const FString& FlipbookName);

	// =====================================================================================
	// Animation Map projection — pure grouping layer over TagMappings
	// =====================================================================================

	/** One flipbook card as it appears in the Animation Map. */
	struct FAnimationMapEntry
	{
		int32 FlipbookIndex = INDEX_NONE;
		FString FlipbookName;
		FGameplayTag GroupTag;
		FGameplayTag PhaseTag;
		/** True when this entry carries the authored Chain Start flag inside GroupTag. */
		bool bIsChainStart = false;
	};

	/** One tag group/lane in the Animation Map. */
	struct FAnimationMapGroup
	{
		FGameplayTag Tag;
		TArray<FAnimationMapEntry> Entries;
	};

	/** Full Animation Map projection: tag groups plus the flipbooks that are not mapped anywhere. */
	struct FAnimationMapProjection
	{
		TArray<FAnimationMapGroup> Groups;
		TArray<FAnimationMapEntry> Unmapped;
	};

	/** Project the asset's tag mappings into ordered groups. Empty groups are omitted; remaining
	 *  groups are sorted by tag name. A flipbook appears at most once in the result, even if
	 *  legacy/dirty data still contains duplicate mapping entries. */
	FAnimationMapProjection ProjectAnimationMap(const UPaper2DPlusCharacterProfileAsset* Asset);

	// =====================================================================================
	// "By Tag Group" browser lens (TASK-108 U7, R10) — pure section derivation
	// =====================================================================================

	/** One derived section of the flipbook browser's "By Tag Group" lens. */
	struct FTagLensSection
	{
		/** The TagMappings group key. INVALID for the "Unmapped" fallback section. */
		FGameplayTag GroupTag;

		/** Display title: the tag path relative to "Paper2DPlus.Animation." ("Combat",
		 *  "Context.Airborne"), the full tag name for keys outside that taxonomy, or the LOCKED
		 *  "Unmapped" for the fallback section. */
		FString Title;

		/** True only for the fallback section (animations in NO mapping group). */
		bool bUnmapped = false;

		/** Members in lens order — exact-group chain-start clusters first (authored entry order, each cluster
		 *  in root→finisher order, group-bounded and cycle-cut), then the non-root rest ALPHABETICALLY
		 *  (case-insensitive). Parallel arrays: index into Asset->Flipbooks + authored display name. */
		TArray<int32> MemberIndices;
		TArray<FString> MemberNames;
	};

	/**
	 * Derive the "By Tag Group" lens sections (TASK-108 U7, R10). PURE — the Slate code just renders
	 * this output. Pinned semantics (Paper2DPlus.AnimationTagSurfacing.TagLensSections):
	 *  - one section per `TagMappings` key that resolves at least ONE member, SORTED by tag name
	 *    (never raw TMap iteration order); a key whose entries all fail to resolve emits NO section
	 *    (legacy-cleanup 2026-07 — a removed/emptied mapping disappears from the lens instead of
	 *    lingering as an empty group shell),
	 *  - the "Unmapped" fallback section comes LAST and is emitted only when at least one animation
	 *    is in no mapping group; an EMPTY asset yields NO sections at all,
	 *  - an animation in TWO mapping groups appears in BOTH sections (per-SECTION dedup only —
	 *    deliberately different from ProjectAnimationMap's global single-home dedup: the lens is a
	 *    faithful membership view, not a board),
	 *  - mapping entries that resolve to no unambiguous real flipbook entry (dangling names,
	 *    PaperZD-only entries, duplicate membership, or duplicate profile names) are skipped.
	 */
	TArray<FTagLensSection> BuildTagLensSections(const UPaper2DPlusCharacterProfileAsset* Asset);

	/**
	 * Edge selection re-match key (U6 — replaces the U4 ClearSelectionSet-only stopgap): after a
	 * a consumer genuinely replaces edge objects, the previously selected edge is re-found BEST-EFFORT
	 * by its VALUE tuple (From, To) — one-row-per-pair since TASK-108, so the tuple is the whole
	 * identity (the pre-U2 Occurrence rank died with duplicate edges). No match = selection drops
	 * (engine-standard).
	 */
	struct FEdgeReselectKey
	{
		FString FromMoveLower;
		FString TargetMoveLower;

		/** A default key (both names empty) never matches. */
		bool IsValid() const { return !FromMoveLower.IsEmpty() && !TargetMoveLower.IsEmpty(); }
	};

	/** Build the re-select key for Edges[EdgeIndex]: its (From, To) value tuple. Invalid index -> a
	 *  default (never-matching) key. */
	FEdgeReselectKey MakeEdgeReselectKey(const TArray<FProjectedEdge>& Edges, int32 EdgeIndex);

	/** Find the edge matching Key's value tuple, in array order. Returns the index into Edges, or
	 *  INDEX_NONE (selection drops). Matches by TUPLE — an external row reorder that moved the
	 *  same-valued row re-matches it at its new position; a concurrent phase edit (TransitionPhaseTag
	 *  not in the tuple) never drops the selection. */
	int32 FindEdgeByReselectKey(const TArray<FProjectedEdge>& Edges, const FEdgeReselectKey& Key);

	// =====================================================================================
	// Root / phase analysis — pure, over the flat FFlipbookTransitionData
	// =====================================================================================

	/** The "confirm path" filter for combo/phase chains: every non-empty-target row is a chain edge
	 *  (TASK-108 — the Always-or-OnHit condition filter died with the per-row Condition field). */
	bool IsConfirmTransition(const FPaper2DPlusMoveTransition& Transition);

	/** Ordered chain for one exact mapping group and flagged chain-start move. Traversal is
	 *  deterministic, cycle-safe, group-bounded, and stops before another chain start. Invalid or
	 *  unflagged starts return empty; a valid start with no in-scope transition returns only itself. */
	TArray<FString> DeriveComboChain(
		const UPaper2DPlusCharacterProfileAsset* Asset,
		const FGameplayTag& GroupTag,
		const FString& RootMoveName);

	/** Phase from a position in an ordered chain: index 0 => Startup, last => Recovery, middle =>
	 *  Active. Chains shorter than 2 (a lone clip) => None for every position. Pure — no tag registry. */
	EAnimationPhase PhaseForChainPosition(int32 IndexInChain, int32 ChainLength);

	/**
	 * "derive phases from the map": walk each exact TagMappings group independently, processing its
	 * flagged chain starts in authored entry order. Traversal follows authored transitions only while
	 * the target remains in that group and stops before another chain start. Returns lowered name ->
	 * the EAnimationPhase implied by chain position (PhaseForChainPosition); deterministic first
	 * group/root wins for invalid shared-sink data. Groups without chain starts and lone clips
	 * contribute nothing.
	 * PURE — computes, never writes; the panel/console action skips existing manual PhaseTags. */
	TMap<FString, EAnimationPhase> DerivePhasesFromMap(const UPaper2DPlusCharacterProfileAsset* Asset);

	/** The registered Paper2DPlus.Phase.* tag for a derivable phase (Startup/Active/Recovery); an INVALID
	 *  tag for None or an unregistered phase. The single EAnimationPhase -> phase-tag mapping (U3 apply). */
	FGameplayTag PhaseToGameplayTag(EAnimationPhase Phase);
}
