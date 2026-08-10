// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "AnimationMapCore.h"

#include "AnimationTagChipUtils.h" // GetTagLeafString — the ONE shared cross-version tag-leaf helper
#include "Paper2DPlusAnimationTags.h" // native Combat/Context roots for the chip-color family dispatch
#include "Paper2DPlusAnimationTagQuery.h" // TASK-108 U6: ONE effective-tag batch per ProjectGraph
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Paper2DPlusComboChain.h" // WS3 U6: chain statics moved to the runtime module
#include "Paper2DPlusSettings.h"   // Tag Colors registry (project-wide tag → color overrides)

namespace
{
	/** First entry whose FlipbookName matches case-insensitively — THE first-match contract shared
	 *  with FindFlipbookDataPtr (write-through always targets the first entry; the PR1
	 *  two-entries-one-name ambiguity is deliberately unchanged). File-unique helper name per the
	 *  unity-build rule. */
	FFlipbookProfileEntry* AnimationMapCore_FindEntryMutable(UPaper2DPlusCharacterProfileAsset* Asset, const FString& MoveName)
	{
		if (!Asset || MoveName.IsEmpty())
		{
			return nullptr;
		}
		for (FFlipbookProfileEntry& Anim : Asset->Flipbooks)
		{
			if (Anim.Identity.FlipbookName.Equals(MoveName, ESearchCase::IgnoreCase))
			{
				return &Anim;
			}
		}
		return nullptr;
	}

	// First-layout grid (R4): roomy enough for U6's 64px-thumbnail node + title; values only need to
	// be deterministic and non-overlapping — nodes are user-movable afterwards.
	constexpr int32 AnimationMapCore_GridColumns = 4;
	constexpr double AnimationMapCore_GridCellWidth = 220.0;
	constexpr double AnimationMapCore_GridCellHeight = 160.0;

}

namespace Paper2DPlusAnimationMap
{
	FGraphProjection ProjectGraph(
		const UPaper2DPlusCharacterProfileAsset* Asset,
		const TSet<FString>* MoveFilterLower,
		const FGameplayTag* ExactGroupScope)
	{
		FGraphProjection Out;
		if (!Asset)
		{
			return Out;
		}

		// THE stub predicate: the validator's lowered flipbook-name set (ValidateCharacterProfileAsset,
		// Paper2DPlusCharacterProfileAsset.cpp:2019-2037 set build, :2275-2288 dangling-target use) —
		// trimmed-empty names never enter the set, membership is by ToLower(). A projected name absent
		// from this set is a stub. Never re-derive a different predicate.
		TSet<FString> RealNamesLower;
		TMap<FString, FString> RealDisplayByLower; // lowered -> authored display case (first entry wins)
		TMap<FString, FGameplayTag> PhaseTagByLower; // lowered -> EditorMeta.PhaseTag (first entry wins, U4)
		for (const FFlipbookProfileEntry& Anim : Asset->Flipbooks)
		{
			if (Anim.Identity.FlipbookName.TrimStartAndEnd().IsEmpty())
			{
				continue; // degenerate empty name (the validator's empty-name Warning case)
			}
			const FString NameLower = Anim.Identity.FlipbookName.ToLower();
			if (!RealNamesLower.Contains(NameLower))
			{
				RealNamesLower.Add(NameLower);
				RealDisplayByLower.Add(NameLower, Anim.Identity.FlipbookName);
				PhaseTagByLower.Add(NameLower, Anim.EditorMeta.PhaseTag);
			}
		}

		// Chain-start markers project only starts accepted by the shared runtime resolver. Ambiguous
		// membership and dangling starts therefore never masquerade as usable starts in the graph.
		// Exact group views cannot inherit a start from another group; the unassigned graph has none.
		TSet<FString> ChainStartLower;
		TSet<FString> ChainEndLower;
		const TArray<Paper2DPlusComboChain::FScopedAnimationGroupAnalysis> GroupAnalyses =
			Paper2DPlusComboChain::AnalyzeAnimationGroups(Asset);
		const bool bHasExactGroupScope = ExactGroupScope && ExactGroupScope->IsValid();
		if (bHasExactGroupScope || !MoveFilterLower)
		{
			for (const Paper2DPlusComboChain::FScopedAnimationGroupAnalysis& Group : GroupAnalyses)
			{
				if (bHasExactGroupScope && Group.GroupTag != *ExactGroupScope)
				{
					continue;
				}
				for (const Paper2DPlusComboChain::FScopedAnimationRootAnalysis& Root : Group.Roots)
				{
					const FString NameLower = Root.Root.RootMove.ToLower();
					if (!NameLower.IsEmpty())
					{
						ChainStartLower.Add(NameLower);
					}
				}
			}
			// Chain-end flags come straight off the scoped mapping entries — an end is an ordinary
			// member with a boundary flag, so there is no separate "valid end" resolution to mirror
			// (dangling entries never project a marker: AddNode gates flags on !bIsStub).
			for (const TPair<FGameplayTag, FFlipbookTagMapping>& MappingPair : Asset->TagMappings)
			{
				if (bHasExactGroupScope && MappingPair.Key != *ExactGroupScope)
				{
					continue;
				}
				for (const FFlipbookTagMappingEntry& MappingEntry : MappingPair.Value.Entries)
				{
					const FString NameLower = MappingEntry.FlipbookName.ToLower();
					if (MappingEntry.bIsChainEnd && !NameLower.TrimStartAndEnd().IsEmpty())
					{
						ChainEndLower.Add(NameLower);
					}
				}
			}
		}

		// Combo main-line ("spine") step badges — the SAME numbering Get Combo Chain Flipbook at Index
		// returns: stamped only when exactly ONE chain start profile-wide reaches the move (the
		// runtime's AmbiguousChain fail-closed, mirrored) and only for moves ON that start's derived
		// spine (side-branch members show no step). Lowered name -> (spine index, spine length).
		TMap<FString, TPair<int32, int32>> ComboSpineByLower;
		if (bHasExactGroupScope || !MoveFilterLower)
		{
			TMap<FString, const Paper2DPlusComboChain::FScopedAnimationRoot*> UniqueReachingRoot;
			for (const Paper2DPlusComboChain::FScopedAnimationGroupAnalysis& Group : GroupAnalyses)
			{
				for (const Paper2DPlusComboChain::FScopedAnimationRootAnalysis& Root : Group.Roots)
				{
					for (const FString& ChainMove : Root.ChainMoves)
					{
						const FString ChainLower = ChainMove.ToLower();
						if (const Paper2DPlusComboChain::FScopedAnimationRoot** Existing =
							UniqueReachingRoot.Find(ChainLower))
						{
							*Existing = nullptr; // reached by 2+ starts — ambiguous, no step badge
						}
						else
						{
							UniqueReachingRoot.Add(ChainLower, &Root.Root);
						}
					}
				}
			}

			TSet<const Paper2DPlusComboChain::FScopedAnimationRoot*> SpinedRoots;
			for (const TPair<FString, const Paper2DPlusComboChain::FScopedAnimationRoot*>& Pair :
				UniqueReachingRoot)
			{
				const Paper2DPlusComboChain::FScopedAnimationRoot* Root = Pair.Value;
				if (!Root || SpinedRoots.Contains(Root))
				{
					continue;
				}
				SpinedRoots.Add(Root);

				const TArray<FString> Spine =
					Paper2DPlusComboChain::DeriveComboSpine(Asset, Root->GroupTag, Root->RootMove);
				for (int32 SpineIndex = 0; SpineIndex < Spine.Num(); ++SpineIndex)
				{
					const FString SpineLower = Spine[SpineIndex].ToLower();
					if (UniqueReachingRoot.FindRef(SpineLower) == Root)
					{
						ComboSpineByLower.Add(SpineLower, TPair<int32, int32>(SpineIndex, Spine.Num()));
					}
				}
			}
		}

		// TASK-108 U6 (R6): ONE effective-tag batch per projection — every node's chip provenance and
		// the filter bar's match dimension come from here, so they can't drift from the BP queries.
		const TMap<FString, Paper2DPlusAnimationTagQuery::FAnimationTagSet> AnimationTagMap =
			Paper2DPlusAnimationTagQuery::BuildAnimationTagMap(Asset, GroupAnalyses);

		TSet<FString> NodeKeys;
		auto AddNode = [&Out, &NodeKeys, &RealNamesLower, &RealDisplayByLower, &PhaseTagByLower,
			&ChainStartLower, &ChainEndLower, &ComboSpineByLower, &AnimationTagMap,
			MoveFilterLower](const FString& NameAsSeen)
		{
			const FString NameLower = NameAsSeen.ToLower();
			if (MoveFilterLower && !MoveFilterLower->Contains(NameLower))
			{
				return;
			}
			if (NodeKeys.Contains(NameLower))
			{
				return; // case-insensitive merge: "Jab"/"jab" = one node (first spelling wins for stubs)
			}
			NodeKeys.Add(NameLower);

			FProjectedNode Node;
			Node.MoveNameLower = NameLower;
			const FString* RealDisplay = RealDisplayByLower.Find(NameLower);
			Node.DisplayName = RealDisplay ? *RealDisplay : NameAsSeen; // real moves keep authored case
			Node.bIsStub = !RealNamesLower.Contains(NameLower);
			Node.bIsChainStart = !Node.bIsStub && ChainStartLower.Contains(NameLower);
			Node.bIsChainEnd = !Node.bIsStub && ChainEndLower.Contains(NameLower);
			Node.PhaseTag = PhaseTagByLower.FindRef(NameLower);
			if (!Node.bIsStub)
			{
				if (const TPair<int32, int32>* SpinePos = ComboSpineByLower.Find(NameLower))
				{
					Node.ComboSpineIndex = SpinePos->Key;
					Node.ComboSpineLength = SpinePos->Value;
				}
			}
			// U6: provenance-separated tags (real moves only — a stub has no batch entry, stays empty).
			if (const Paper2DPlusAnimationTagQuery::FAnimationTagSet* TagSet = AnimationTagMap.Find(NameLower))
			{
				Node.OwnAnimationTags = TagSet->OwnTags;
				Node.ChainInheritedAnimationTags = TagSet->ChainInheritedTags;
				Node.GroupImpliedAnimationTags = TagSet->GroupImpliedTags;
			}
			Out.Nodes.Add(MoveTemp(Node));
		};

		if (MoveFilterLower)
		{
			for (const FFlipbookProfileEntry& Anim : Asset->Flipbooks)
			{
				if (Anim.Identity.FlipbookName.TrimStartAndEnd().IsEmpty())
				{
					continue;
				}
				AddNode(Anim.Identity.FlipbookName);
			}
		}

		// Moves owning rows + row targets + edges, in Flipbooks ARRAY order / row-array order. Only the
		// FIRST entry per lowered name contributes (first-match contract; duplicate names are a
		// validation Error) — a later duplicate's rows must not mint edges whose (From, RowIndex)
		// handle would resolve against the first entry's rows.
		TSet<FString> SeenFromLower;
		for (const FFlipbookProfileEntry& Anim : Asset->Flipbooks)
		{
			const FString& FromName = Anim.Identity.FlipbookName;
			if (FromName.TrimStartAndEnd().IsEmpty())
			{
				continue; // degenerate: no node, no edges
			}
			const FString FromLower = FromName.ToLower();
			if (MoveFilterLower && !MoveFilterLower->Contains(FromLower))
			{
				continue;
			}
			if (SeenFromLower.Contains(FromLower))
			{
				continue;
			}
			SeenFromLower.Add(FromLower);

			const TArray<FPaper2DPlusMoveTransition>& Rows = Anim.TransitionData.Transitions;
			if (Rows.Num() > 0)
			{
				AddNode(FromName); // a move owning at least one row participates
			}
			TSet<FString> SeenTargetsLower; // U2: at most ONE edge per (From, To) pair (see below)
			for (int32 RowIndex = 0; RowIndex < Rows.Num(); ++RowIndex)
			{
				const FPaper2DPlusMoveTransition& Row = Rows[RowIndex];
				if (Row.TargetMove.IsEmpty())
				{
					// Authoring-in-progress row: NO edge, never touched. DELIBERATE asymmetry with the
					// trim-exclusions above: this mirrors the data layer's IsEmpty-only skip, so a
					// whitespace-only target projects a (whitespace-titled) stub exactly as the runtime
					// queries and the validator treat it — the graph must not be stricter than the
					// data layer it projects.
					continue;
				}
				if (MoveFilterLower && !MoveFilterLower->Contains(Row.TargetMove.ToLower()))
				{
					continue;
				}
				AddNode(Row.TargetMove); // row targets participate (dangling => stub)

				// CANONICAL projection (U2, the ClashGraphCore precedent): one edge per (From, To)
				// pair, first row wins — matching the load-time dedupe's first-wins order. Duplicate
				// rows can only exist mid-session via a raw Details-array edit (the wire funnel and
				// the load migration both refuse them); the pair's edge stays visible while ANY row
				// carries it, and the next load dedupes the array itself.
				const FString TargetLower = Row.TargetMove.ToLower();
				if (SeenTargetsLower.Contains(TargetLower))
				{
					continue;
				}
				SeenTargetsLower.Add(TargetLower);

				FProjectedEdge Edge;
				Edge.FromMoveLower = FromLower;
				Edge.TargetMoveLower = TargetLower;
				// A row-owned phase override wins. An unset legacy row inherits the target animation's
				// phase; a dangling target without an override remains empty and hides the badge.
				Edge.TransitionPhaseTag = Row.GetEffectivePhaseTag(
					PhaseTagByLower.FindRef(Edge.TargetMoveLower));
				Out.Edges.Add(MoveTemp(Edge));
			}
		}

#if WITH_EDITORONLY_DATA
		// Placed-only keys (kept-stale placements are resurrect-friendly, KTD). SORTED for a
		// deterministic node order — never raw TMap iteration order.
		TArray<FString> PlacedKeys;
		Asset->AnimationMapNodePositions.GetKeys(PlacedKeys);
		PlacedKeys.Sort();
		for (const FString& PlacedKey : PlacedKeys)
		{
			if (!PlacedKey.TrimStartAndEnd().IsEmpty())
			{
				AddNode(PlacedKey);
			}
		}

#endif

		return Out;
	}

	FProjectionDiff DiffProjection(const FGraphProjection& Previous, const FGraphProjection& Current,
		bool bAnyPositionDrift, bool bAnyFlipbookPtrChanged)
	{
		FProjectionDiff Diff;
		Diff.bAnyPositionDrift = bAnyPositionDrift;
		Diff.bAnyFlipbookPtrChanged = bAnyFlipbookPtrChanged;

		TMap<FString, const FProjectedNode*> PreviousByName;
		PreviousByName.Reserve(Previous.Nodes.Num());
		for (const FProjectedNode& Node : Previous.Nodes)
		{
			PreviousByName.Add(Node.MoveNameLower, &Node);
		}

		TSet<FString> MatchedPreviousNames;
		MatchedPreviousNames.Reserve(Previous.Nodes.Num());
		for (const FProjectedNode& Node : Current.Nodes)
		{
			const FProjectedNode* PreviousNode =
				PreviousByName.FindRef(Node.MoveNameLower);
			if (PreviousNode)
			{
				MatchedPreviousNames.Add(PreviousNode->MoveNameLower);
				if (PreviousNode->bIsStub != Node.bIsStub)
				{
					Diff.StubFlagFlips.Add(Node.MoveNameLower);
				}
				if (!PreviousNode->DisplayName.Equals(
					Node.DisplayName,
					ESearchCase::CaseSensitive))
				{
					Diff.DisplayNameChanges.Add(Node.MoveNameLower);
				}
				if (PreviousNode->bIsChainStart != Node.bIsChainStart
					|| PreviousNode->bIsChainEnd != Node.bIsChainEnd)
				{
					Diff.ChainStartChanges.Add(Node.MoveNameLower);
				}
				if (PreviousNode->ComboSpineIndex != Node.ComboSpineIndex
					|| PreviousNode->ComboSpineLength != Node.ComboSpineLength)
				{
					Diff.ComboSpineChanges.Add(Node.MoveNameLower); // re-stamp the combo step badge
				}
				if (!(PreviousNode->OwnAnimationTags == Node.OwnAnimationTags)
					|| !(PreviousNode->ChainInheritedAnimationTags
						== Node.ChainInheritedAnimationTags)
					|| !(PreviousNode->GroupImpliedAnimationTags
						== Node.GroupImpliedAnimationTags))
				{
					Diff.AnimationTagChanges.Add(Node.MoveNameLower); // U6: re-stamp the tag chips
				}
				if (PreviousNode->PhaseTag != Node.PhaseTag)
				{
					Diff.PhaseTagChanges.Add(Node.MoveNameLower);
				}
			}
			else
			{
				Diff.MovesToAdd.Add(Node.MoveNameLower);
			}
		}

		for (const FProjectedNode& Node : Previous.Nodes)
		{
			if (!MatchedPreviousNames.Contains(Node.MoveNameLower))
			{
				Diff.MovesToRemove.Add(Node.MoveNameLower);
			}
		}

		// Preserve identity independently from authored row order. Row order still participates in the
		// full-diff gate because surviving edge nodes must restamp their RowIndex handles, but it is not
		// an add/remove signal and therefore never requires pill recreation.
		auto SameEdgeIdentity = [](const FProjectedEdge& A, const FProjectedEdge& B)
		{
			return A.FromMoveLower.Equals(B.FromMoveLower, ESearchCase::CaseSensitive)
				&& A.TargetMoveLower.Equals(B.TargetMoveLower, ESearchCase::CaseSensitive);
		};

		for (const FProjectedEdge& CurrentEdge : Current.Edges)
		{
			const FProjectedEdge* PreviousEdge = Previous.Edges.FindByPredicate(
				[&CurrentEdge, &SameEdgeIdentity](const FProjectedEdge& Candidate)
				{
					return SameEdgeIdentity(Candidate, CurrentEdge);
				});
			if (!PreviousEdge)
			{
				Diff.EdgesToAdd.Add(CurrentEdge);
			}
			else
			{
				if (PreviousEdge->TransitionPhaseTag
					!= CurrentEdge.TransitionPhaseTag)
				{
					Diff.EdgePhaseChanges.Add(CurrentEdge);
				}
			}
		}
		for (const FProjectedEdge& PreviousEdge : Previous.Edges)
		{
			if (!Current.Edges.ContainsByPredicate(
				[&PreviousEdge, &SameEdgeIdentity](const FProjectedEdge& Candidate)
				{
					return SameEdgeIdentity(PreviousEdge, Candidate);
				}))
			{
				Diff.EdgesToRemove.Add(PreviousEdge);
			}
		}

		// Keep the authored-order equality bit for the row-handle restamp path.
		Diff.bEdgesEqual = Previous.Edges.Num() == Current.Edges.Num();
		if (Diff.bEdgesEqual)
		{
			for (int32 EdgeIndex = 0; EdgeIndex < Current.Edges.Num(); ++EdgeIndex)
			{
				const FProjectedEdge& BeforeEdge = Previous.Edges[EdgeIndex];
				const FProjectedEdge& AfterEdge = Current.Edges[EdgeIndex];
				if (!SameEdgeIdentity(BeforeEdge, AfterEdge))
				{
					Diff.bEdgesEqual = false;
					break;
				}
			}
		}
		return Diff;
	}

	TArray<FString> ResolveMultiEditTargetNames(const FString& RequestedMoveLower,
		const TSet<FString>& SelectedRealMoveLowers, const FGraphProjection& Projection)
	{
		TArray<FString> Result;
		if (RequestedMoveLower.IsEmpty())
		{
			return Result;
		}

		const bool bRequestedProjectsAsReal = Projection.Nodes.ContainsByPredicate(
			[&RequestedMoveLower](const FProjectedNode& Node)
			{
				return !Node.bIsStub && Node.MoveNameLower == RequestedMoveLower;
			});
		if (!bRequestedProjectsAsReal)
		{
			return Result;
		}
		const bool bRequestedIsSelected = SelectedRealMoveLowers.Contains(RequestedMoveLower);
		for (const FProjectedNode& Node : Projection.Nodes)
		{
			if (Node.bIsStub)
			{
				continue;
			}
			if (bRequestedIsSelected)
			{
				if (SelectedRealMoveLowers.Contains(Node.MoveNameLower))
				{
					Result.Add(Node.DisplayName);
				}
			}
			else if (Node.MoveNameLower == RequestedMoveLower)
			{
				Result.Add(Node.DisplayName);
				break;
			}
		}
		return Result;
	}

	bool TransitionRowsEquivalent(const FPaper2DPlusMoveTransition& A, const FPaper2DPlusMoveTransition& B)
	{
		return A.TargetMove.Equals(B.TargetMove, ESearchCase::IgnoreCase);
	}

	int32 FindTransitionRowIndex(const UPaper2DPlusCharacterProfileAsset* Asset, const FString& FromMove,
		const FString& TargetMove)
	{
		if (!Asset || FromMove.IsEmpty() || TargetMove.IsEmpty())
		{
			return INDEX_NONE;
		}
		// Same first-match entry contract as the mutable finder (const walk — this is a pure probe).
		const FFlipbookProfileEntry* FromEntry = nullptr;
		for (const FFlipbookProfileEntry& Anim : Asset->Flipbooks)
		{
			if (Anim.Identity.FlipbookName.Equals(FromMove, ESearchCase::IgnoreCase))
			{
				FromEntry = &Anim;
				break;
			}
		}
		if (!FromEntry)
		{
			return INDEX_NONE;
		}
		const TArray<FPaper2DPlusMoveTransition>& Rows = FromEntry->TransitionData.Transitions;
		for (int32 RowIndex = 0; RowIndex < Rows.Num(); ++RowIndex)
		{
			if (Rows[RowIndex].TargetMove.Equals(TargetMove, ESearchCase::IgnoreCase))
			{
				return RowIndex;
			}
		}
		return INDEX_NONE;
	}

	bool AppendTransitionRow(UPaper2DPlusCharacterProfileAsset* Asset, const FString& FromMove,
		const FString& TargetMove)
	{
		if (TargetMove.IsEmpty())
		{
			return false;
		}
		FFlipbookProfileEntry* FromEntry = AnimationMapCore_FindEntryMutable(Asset, FromMove);
		if (!FromEntry)
		{
			return false; // unknown move (or a stub — stubs own no entry and can never gain rows)
		}
		// U2 write-time invariant (KTD): one row per (From, To) pair, true by construction at every
		// row-creation path — a duplicate (incl. case-variant and self-loop twins) refuses, leaving
		// the data untouched. The panel funnel pre-checks this to toast; refusing here too keeps the
		// invariant even for future direct callers.
		if (FindTransitionRowIndex(Asset, FromMove, TargetMove) != INDEX_NONE)
		{
			return false;
		}
		// APPEND only: authored row order stays stable for the projection's edge order.
		FromEntry->TransitionData.Transitions.Add(FPaper2DPlusMoveTransition(TargetMove));
		return true;
	}

	bool RemoveTransitionRowChecked(UPaper2DPlusCharacterProfileAsset* Asset, const FString& FromMove,
		int32 RowIndex, const FPaper2DPlusMoveTransition& ExpectedSnapshot)
	{
		FFlipbookProfileEntry* FromEntry = AnimationMapCore_FindEntryMutable(Asset, FromMove);
		if (!FromEntry)
		{
			return false;
		}
		TArray<FPaper2DPlusMoveTransition>& Rows = FromEntry->TransitionData.Transitions;
		if (!Rows.IsValidIndex(RowIndex) || !TransitionRowsEquivalent(Rows[RowIndex], ExpectedSnapshot))
		{
			// The row shifted/changed underneath the handle (external edit in the one-tick deferral
			// window) — abort untouched; the caller reconciles. With IDENTICAL duplicate rows (legal,
			// R14) the index+snapshot pair still removes exactly the addressed one.
			return false;
		}
		Rows.RemoveAt(RowIndex);
		return true;
	}

	FRemoveMoveResult RemoveAllRowsForMove(UPaper2DPlusCharacterProfileAsset* Asset, const FString& MoveName)
	{
		FRemoveMoveResult Result;
		if (!Asset || MoveName.IsEmpty())
		{
			return Result;
		}

		// Outgoing FIRST so self-loop rows are counted exactly once (as outgoing) — the incoming sweep
		// below would otherwise see and re-count them. Empty-target authoring rows are removed too:
		// any surviving row would keep projecting the node we are deleting.
		if (FFlipbookProfileEntry* FromEntry = AnimationMapCore_FindEntryMutable(Asset, MoveName))
		{
			Result.OutgoingRemoved = FromEntry->TransitionData.Transitions.Num();
			FromEntry->TransitionData.Transitions.Empty();
		}

		// Incoming: every row on ANY entry targeting the name (case-insensitive), removed DESCENDING
		// by index so removals never shift the indices still to visit.
		for (FFlipbookProfileEntry& Anim : Asset->Flipbooks)
		{
			TArray<FPaper2DPlusMoveTransition>& Rows = Anim.TransitionData.Transitions;
			for (int32 RowIndex = Rows.Num() - 1; RowIndex >= 0; --RowIndex)
			{
				if (Rows[RowIndex].TargetMove.Equals(MoveName, ESearchCase::IgnoreCase))
				{
					Rows.RemoveAt(RowIndex);
					++Result.IncomingRemoved;
				}
			}
		}

#if WITH_EDITORONLY_DATA
		// Explicit node delete is the ONE path that prunes a placement key (stale keys are otherwise
		// kept on purpose — resurrect-friendly).
		Result.bPositionEntryRemoved = Asset->AnimationMapNodePositions.Remove(MoveName.ToLower()) > 0;
#endif

		return Result;
	}

	FMoveRowCounts CountRowsForMove(const UPaper2DPlusCharacterProfileAsset* Asset, const FString& MoveName)
	{
		FMoveRowCounts Counts;
		if (!Asset || MoveName.IsEmpty())
		{
			return Counts;
		}

		// Mirror RemoveAllRowsForMove EXACTLY (the dialog's numbers must match the removal, pinned by
		// test): outgoing = the FIRST case-insensitive entry match's whole array (self-loops live there,
		// counted once; empty-target authoring rows included); incoming = rows targeting the name on
		// every OTHER entry — the removal wipes outgoing FIRST, so its incoming sweep never sees the
		// first entry's rows (a later duplicate-named entry's rows DO count as incoming, matching the
		// sweep, which visits all entries).
		const FFlipbookProfileEntry* FirstMatch = nullptr;
		for (const FFlipbookProfileEntry& Anim : Asset->Flipbooks)
		{
			if (!FirstMatch && Anim.Identity.FlipbookName.Equals(MoveName, ESearchCase::IgnoreCase))
			{
				FirstMatch = &Anim;
				Counts.OutgoingRows = Anim.TransitionData.Transitions.Num();
				continue;
			}
			for (const FPaper2DPlusMoveTransition& Row : Anim.TransitionData.Transitions)
			{
				if (Row.TargetMove.Equals(MoveName, ESearchCase::IgnoreCase))
				{
					++Counts.IncomingRows;
				}
			}
		}

#if WITH_EDITORONLY_DATA
		Counts.bPlacementExists = Asset->AnimationMapNodePositions.Contains(MoveName.ToLower());
#endif

		return Counts;
	}

	FVector2D DropFanOffset(int32 PlacementIndex)
	{
		// Diagonal cascade (the cascading-windows recipe): index 0 = exactly the drop point; one title
		// row's worth of stagger per subsequent move keeps every node individually grabbable. Distinct
		// for every distinct index by construction.
		return FVector2D(PlacementIndex * 36.0, PlacementIndex * 28.0);
	}

	bool EdgeValueTuplesEqual(const FProjectedEdge& A, const FProjectedEdge& B)
	{
		// From/Target only (pure From->To arrows, TASK-108); the lowered strings compare IgnoreCase
		// defensively (they are lowered by the projection contract anyway). TransitionPhaseTag is
		// deliberately NOT compared — this is the re-match/value dimension, and a selection must
		// survive a concurrent phase edit.
		return A.FromMoveLower.Equals(B.FromMoveLower, ESearchCase::IgnoreCase)
			&& A.TargetMoveLower.Equals(B.TargetMoveLower, ESearchCase::IgnoreCase);
	}

	namespace
	{
		/** The phase-badge CONVENTION color table (keyed case-insensitively by leaf). THE single home of
		 *  the Active=green/Startup=gold/Recovery=slate convention — GetPhaseTagBadge AND the shared
		 *  chip-color helper both read it (extend-the-seam, never fork; TASK-108 U6/R11). Returns false
		 *  for unknown/custom leaves (callers apply their own neutral). */
		bool AnimationMapCore_PhaseConventionColor(const FString& LeafName, FLinearColor& OutColor)
		{
			struct FPhaseColorEntry { const TCHAR* Leaf; FLinearColor Color; };
			static const FPhaseColorEntry PhaseColors[] =
			{
				{ TEXT("Startup"),  FLinearColor(0.85f, 0.65f, 0.15f) }, // gold — windup
				{ TEXT("Charge"),   FLinearColor(0.85f, 0.50f, 0.15f) }, // amber — hold/buildup
				{ TEXT("Release"),  FLinearColor(0.90f, 0.45f, 0.20f) }, // orange — committed
				{ TEXT("Active"),   FLinearColor(0.30f, 0.75f, 0.35f) }, // green — payload (U4/U5 convention)
				{ TEXT("Impact"),   FLinearColor(0.85f, 0.25f, 0.25f) }, // red — decisive contact
				{ TEXT("Recovery"), FLinearColor(0.35f, 0.55f, 0.85f) }, // slate — control returns
				{ TEXT("Enter"),    FLinearColor(0.25f, 0.70f, 0.70f) }, // teal — structural enter
				{ TEXT("Loop"),     FLinearColor(0.55f, 0.40f, 0.80f) }, // purple — sustained loop
				{ TEXT("Exit"),     FLinearColor(0.40f, 0.50f, 0.70f) }, // muted blue — structural exit
				{ TEXT("End"),      FLinearColor(0.45f, 0.50f, 0.60f) }, // gray-blue — final end state
			};
			for (const FPhaseColorEntry& Entry : PhaseColors)
			{
				if (LeafName.Equals(Entry.Leaf, ESearchCase::IgnoreCase))
				{
					OutColor = Entry.Color;
					return true;
				}
			}
			return false;
		}

		/** Luminance-contrasting text fill (Rec. 709) so a label stays legible ON a colored chip — gold
		 *  and green want dark text, slate/purple want light. Shared by badge + chip helpers. */
		FLinearColor AnimationMapCore_ContrastTextColor(const FLinearColor& InColor)
		{
			const float Luminance = 0.2126f * InColor.R + 0.7152f * InColor.G + 0.0722f * InColor.B;
			return (Luminance > 0.5f)
				? FLinearColor(0.05f, 0.05f, 0.05f)
				: FLinearColor(0.97f, 0.97f, 0.97f);
		}
	}

	FPhaseTagBadge GetPhaseTagBadge(const FGameplayTag& PhaseTag)
	{
		FPhaseTagBadge Out;
		if (!PhaseTag.IsValid())
		{
			return Out; // empty Label -> callers hide the badge
		}

		// Label = the tag's LEAF segment so ANY phase tag renders (the 10 registered ones or a
		// user-authored phase under Paper2DPlus.Phase.*).
		const FString LeafName = Paper2DPlusAnimationTagChips::GetTagLeafString(PhaseTag);
		Out.Label = LeafName;

		// Color by leaf with a neutral fallback (the shared convention table — TASK-108 U6 moved it
		// into AnimationMapCore_PhaseConventionColor so the chip helper reads the SAME seam).
		Out.Color = FLinearColor(0.55f, 0.55f, 0.55f); // neutral fallback for custom/unknown phases
		AnimationMapCore_PhaseConventionColor(LeafName, Out.Color);

		// Tag Colors override: a project-wide registered color for this phase tag (or an ancestor)
		// wins over the built-in leaf table above. Empty registry / no match keeps the default, so
		// an unconfigured project is byte-identical to before.
		bool bRegistryFound = false;
		const FLinearColor RegistryColor = UPaper2DPlusSettings::ResolveTagColor(PhaseTag, bRegistryFound);
		if (bRegistryFound)
		{
			Out.Color = RegistryColor;
		}

		Out.TextColor = AnimationMapCore_ContrastTextColor(Out.Color);
		return Out;
	}

	FTagChipColor GetAnimationTagChipColor(const FGameplayTag& InTag)
	{
		FTagChipColor Out; // neutral gray default (precedence step 4)
		if (!InTag.IsValid())
		{
			Out.TextColor = AnimationMapCore_ContrastTextColor(Out.Color);
			return Out;
		}

		// 1./2. Registry — exact entry, else nearest ANCESTOR (ResolveTagColor's documented fall-up).
		bool bRegistryFound = false;
		const FLinearColor RegistryColor = UPaper2DPlusSettings::ResolveTagColor(InTag, bRegistryFound);
		if (bRegistryFound)
		{
			Out.Color = RegistryColor;
			Out.TextColor = AnimationMapCore_ContrastTextColor(Out.Color);
			return Out;
		}

		// 3. Convention fallback palettes per family. Family membership via real tag hierarchy
		// (MatchesTag), never string prefixes; the parent tags exist implicitly once any child is
		// registered (the native taxonomy always is).
		const FString LeafName = Paper2DPlusAnimationTagChips::GetTagLeafString(InTag);
		// Combat/Context roots are native statics (Paper2DPlusAnimationTags.h) — no per-call
		// RequestGameplayTag lookup. The Phase root has NO native declaration (Paper2DPlus.Phase is
		// registered via config/usage, not natively — deliberately NOT adding a new native tag here),
		// so it keeps the request; ErrorIfNotFound=false → invalid when unregistered, branch skipped.
		const FGameplayTag PhaseRoot = FGameplayTag::RequestGameplayTag(FName(TEXT("Paper2DPlus.Phase")), /*ErrorIfNotFound*/ false);
		const FGameplayTag CombatRoot = Paper2DPlusAnimationTags::Combat;
		const FGameplayTag ContextRoot = Paper2DPlusAnimationTags::Context;

		if (PhaseRoot.IsValid() && InTag.MatchesTag(PhaseRoot))
		{
			// Phase family: the SAME leaf table GetPhaseTagBadge reads (never a fork); its neutral for
			// custom/unknown phase leaves too, so a chip and a badge for one tag can't disagree.
			Out.Color = FLinearColor(0.55f, 0.55f, 0.55f);
			AnimationMapCore_PhaseConventionColor(LeafName, Out.Color);
		}
		else if (CombatRoot.IsValid() && InTag.MatchesTag(CombatRoot))
		{
			// Combat family — warm ember. Per-leaf entries for the native taxonomy; family base for
			// user-authored children (keyed case-insensitively by leaf, like the phase table).
			struct FCombatColorEntry { const TCHAR* Leaf; FLinearColor Color; };
			static const FCombatColorEntry CombatColors[] =
			{
				{ TEXT("Combo"), FLinearColor(0.85f, 0.45f, 0.18f) }, // orange — the flowing string
				{ TEXT("Heavy"), FLinearColor(0.68f, 0.22f, 0.20f) }, // deep red — commitment
				{ TEXT("Light"), FLinearColor(0.90f, 0.62f, 0.35f) }, // pale amber — quick pokes
				{ TEXT("Block"), FLinearColor(0.55f, 0.42f, 0.28f) }, // bronze — defense
				{ TEXT("Grab"),  FLinearColor(0.72f, 0.32f, 0.52f) }, // magenta — throws
			};
			Out.Color = FLinearColor(0.78f, 0.36f, 0.24f); // Combat family base (the root itself / unknown leaves)
			for (const FCombatColorEntry& Entry : CombatColors)
			{
				if (LeafName.Equals(Entry.Leaf, ESearchCase::IgnoreCase))
				{
					Out.Color = Entry.Color;
					break;
				}
			}
		}
		else if (ContextRoot.IsValid() && InTag.MatchesTag(ContextRoot))
		{
			// Context family — cool sky. Same shape as the Combat table.
			struct FContextColorEntry { const TCHAR* Leaf; FLinearColor Color; };
			static const FContextColorEntry ContextColors[] =
			{
				{ TEXT("Airborne"),  FLinearColor(0.32f, 0.62f, 0.85f) }, // sky — in the air
				{ TEXT("Crouching"), FLinearColor(0.30f, 0.48f, 0.60f) }, // dusk — low stance
				{ TEXT("Swimming"),  FLinearColor(0.22f, 0.65f, 0.62f) }, // teal — in water
			};
			Out.Color = FLinearColor(0.30f, 0.55f, 0.75f); // Context family base
			for (const FContextColorEntry& Entry : ContextColors)
			{
				if (LeafName.Equals(Entry.Leaf, ESearchCase::IgnoreCase))
				{
					Out.Color = Entry.Color;
					break;
				}
			}
		}

		Out.TextColor = AnimationMapCore_ContrastTextColor(Out.Color);
		return Out;
	}

	bool AnimationSearchMatches(const FString& FlipbookName, const FGameplayTagContainer& EffectiveTags,
		const FString& Query)
	{
		if (Query.IsEmpty())
		{
			return true;
		}
		// Name dimension — the pre-existing behavior, unchanged.
		if (FlipbookName.Contains(Query, ESearchCase::IgnoreCase))
		{
			return true;
		}
		// Tag dimensions (R7): any effective tag's FULL path OR LEAF name, case-insensitive substring.
		// (Leaf is a subset of the full path only when the query has no dots — "airborne" hits the leaf
		// of Paper2DPlus.Animation.Context.Airborne via either; the explicit leaf check documents the
		// contract and keeps it true if the path format ever changes.)
		for (auto It = EffectiveTags.CreateConstIterator(); It; ++It)
		{
			const FString FullPath = It->GetTagName().ToString();
			if (FullPath.Contains(Query, ESearchCase::IgnoreCase))
			{
				return true;
			}
			if (Paper2DPlusAnimationTagChips::GetTagLeafString(*It).Contains(Query, ESearchCase::IgnoreCase))
			{
				return true;
			}
		}
		return false;
	}

	bool AnimationTagFilterMatches(const FGameplayTagContainer& EffectiveTags, const FGameplayTag& InFilterTag)
	{
		if (!InFilterTag.IsValid())
		{
			return true; // filter off
		}
		// Hierarchical downward: a container tag MATCHES the filter when it IS the filter or a
		// descendant of it (HasTag expands each container tag's parents against InFilterTag).
		return EffectiveTags.HasTag(InFilterTag);
	}

	FGameplayTag FindHomeAnimationGroupTag(const UPaper2DPlusCharacterProfileAsset* Asset,
		const FString& FlipbookName)
	{
		if (!Asset || FlipbookName.IsEmpty())
		{
			return FGameplayTag();
		}
		// First-mapping-wins single-home over SORTED keys (matches ProjectAnimationMap's dedup order —
		// both walk tag-name-sorted keys, never raw TMap order, so a dirty dual-mapped flipbook
		// resolves the same home on both surfaces).
		TArray<FGameplayTag> SortedKeys;
		Asset->TagMappings.GetKeys(SortedKeys);
		SortedKeys.Sort([](const FGameplayTag& A, const FGameplayTag& B)
		{
			return A.ToString() < B.ToString();
		});
		for (const FGameplayTag& Key : SortedKeys)
		{
			const FFlipbookTagMapping& Mapping = Asset->TagMappings.FindChecked(Key);
			for (const FFlipbookTagMappingEntry& Entry : Mapping.Entries)
			{
				if (Entry.FlipbookName.Equals(FlipbookName, ESearchCase::IgnoreCase))
				{
					return Key;
				}
			}
		}
		return FGameplayTag();
	}

	FAnimationMapProjection ProjectAnimationMap(const UPaper2DPlusCharacterProfileAsset* Asset)
	{
		FAnimationMapProjection Out;
		if (!Asset)
		{
			return Out;
		}

		TMap<FString, int32> FlipbookIndexByLower;
		for (int32 Index = 0; Index < Asset->Flipbooks.Num(); ++Index)
		{
			const FString TrimmedName = Asset->Flipbooks[Index].Identity.FlipbookName.TrimStartAndEnd();
			if (TrimmedName.IsEmpty())
			{
				continue;
			}
			const FString LowerName = Asset->Flipbooks[Index].Identity.FlipbookName.ToLower();
			if (!FlipbookIndexByLower.Contains(LowerName))
			{
				FlipbookIndexByLower.Add(LowerName, Index);
			}
		}

		TArray<FGameplayTag> AssetTags;
		Asset->TagMappings.GetKeys(AssetTags);
		AssetTags.Sort([](const FGameplayTag& A, const FGameplayTag& B)
		{
			return A.ToString() < B.ToString();
		});

		TMap<FGameplayTag, TSet<FString>> ValidRootNamesByGroup;
		for (const Paper2DPlusComboChain::FScopedAnimationGroupAnalysis& Group :
			Paper2DPlusComboChain::AnalyzeAnimationGroupTopologies(Asset))
		{
			TSet<FString>& ValidRoots = ValidRootNamesByGroup.FindOrAdd(Group.GroupTag);
			for (const Paper2DPlusComboChain::FScopedAnimationRootAnalysis& Root : Group.Roots)
			{
				ValidRoots.Add(Root.Root.RootMove.ToLower());
			}
		}

		TSet<FString> AssignedLowerNames;
		for (const FGameplayTag& Tag : AssetTags)
		{
			FAnimationMapGroup Group;
			Group.Tag = Tag;
			const TSet<FString>* ValidRoots = ValidRootNamesByGroup.Find(Tag);

			if (const FFlipbookTagMapping* Binding = Asset->TagMappings.Find(Tag))
			{
				for (const FFlipbookTagMappingEntry& MappingEntry : Binding->Entries)
				{
					if (MappingEntry.FlipbookName.TrimStartAndEnd().IsEmpty())
					{
						continue;
					}
					const FString LowerName = MappingEntry.FlipbookName.ToLower();
					const int32* FlipbookIndex = FlipbookIndexByLower.Find(LowerName);
					if (!FlipbookIndex || AssignedLowerNames.Contains(LowerName))
					{
						continue;
					}
					AssignedLowerNames.Add(LowerName);

					FAnimationMapEntry Entry;
					Entry.FlipbookIndex = *FlipbookIndex;
					Entry.FlipbookName = Asset->Flipbooks[*FlipbookIndex].Identity.FlipbookName;
					Entry.GroupTag = Tag;
					Entry.PhaseTag = Asset->Flipbooks[*FlipbookIndex].EditorMeta.PhaseTag;
					Entry.bIsChainStart = ValidRoots && ValidRoots->Contains(LowerName);
					Group.Entries.Add(MoveTemp(Entry));
				}
			}

			if (Group.Entries.Num() > 0)
			{
				Out.Groups.Add(MoveTemp(Group));
			}
		}

		for (int32 Index = 0; Index < Asset->Flipbooks.Num(); ++Index)
		{
			const FString TrimmedName = Asset->Flipbooks[Index].Identity.FlipbookName.TrimStartAndEnd();
			if (TrimmedName.IsEmpty())
			{
				continue;
			}
			const FString LowerName = Asset->Flipbooks[Index].Identity.FlipbookName.ToLower();
			if (AssignedLowerNames.Contains(LowerName))
			{
				continue;
			}

			FAnimationMapEntry Entry;
			Entry.FlipbookIndex = Index;
			Entry.FlipbookName = Asset->Flipbooks[Index].Identity.FlipbookName;
			Entry.PhaseTag = Asset->Flipbooks[Index].EditorMeta.PhaseTag;
			Out.Unmapped.Add(MoveTemp(Entry));
		}

		return Out;
	}

	TArray<FTagLensSection> BuildTagLensSections(const UPaper2DPlusCharacterProfileAsset* Asset)
	{
		TArray<FTagLensSection> Out;
		if (!Asset)
		{
			return Out;
		}

		// First-match flipbook resolution (lowered name -> index) — the data layer's contract:
		// duplicate-named entries contribute only their FIRST occurrence; empty names never resolve.
		TMap<FString, int32> FlipbookIndexByLower;
		for (int32 Index = 0; Index < Asset->Flipbooks.Num(); ++Index)
		{
			const FString TrimmedName = Asset->Flipbooks[Index].Identity.FlipbookName.TrimStartAndEnd();
			if (TrimmedName.IsEmpty())
			{
				continue;
			}
			const FString LowerName = Asset->Flipbooks[Index].Identity.FlipbookName.ToLower();
			if (!FlipbookIndexByLower.Contains(LowerName))
			{
				FlipbookIndexByLower.Add(LowerName, Index);
			}
		}

		// Shared member ordering: exact-group root clusters first, then the non-root rest
		// alphabetically (lowered-name sort == case-insensitive alphabetical). Candidates arrive
		// pre-deduped; ChainOrderLower is built independently for each group so roots and transitions
		// can never leak across mapping boundaries.
		auto AppendOrderedMembers = [Asset, &FlipbookIndexByLower](
			const TArray<FString>& CandidatesLower,
			const TArray<FString>& ChainOrderLower,
			FTagLensSection& Section)
		{
			TSet<FString> CandidateSet(CandidatesLower);
			TSet<FString> EmittedInChain;
			for (const FString& LowerName : ChainOrderLower)
			{
				if (CandidateSet.Contains(LowerName))
				{
					EmittedInChain.Add(LowerName);
					const int32 FlipbookIndex = FlipbookIndexByLower.FindChecked(LowerName);
					Section.MemberIndices.Add(FlipbookIndex);
					Section.MemberNames.Add(Asset->Flipbooks[FlipbookIndex].Identity.FlipbookName);
				}
			}
			TArray<FString> RestLower;
			for (const FString& LowerName : CandidatesLower)
			{
				if (!EmittedInChain.Contains(LowerName))
				{
					RestLower.Add(LowerName);
				}
			}
			RestLower.Sort();
			for (const FString& LowerName : RestLower)
			{
				const int32 FlipbookIndex = FlipbookIndexByLower.FindChecked(LowerName);
				Section.MemberIndices.Add(FlipbookIndex);
				Section.MemberNames.Add(Asset->Flipbooks[FlipbookIndex].Identity.FlipbookName);
			}
		};

		// Section title: the taxonomy-relative path when the key lives under Paper2DPlus.Animation
		// ("Combat", "Context.Airborne" — unambiguous and compact), else the full tag name.
		auto LensTitleForTag = [](const FGameplayTag& InGroupTag) -> FString
		{
			const FString FullName = InGroupTag.GetTagName().ToString();
			static const FString TaxonomyPrefix = TEXT("Paper2DPlus.Animation.");
			if (FullName.StartsWith(TaxonomyPrefix, ESearchCase::IgnoreCase))
			{
				return FullName.RightChop(TaxonomyPrefix.Len());
			}
			return FullName;
		};

		// One shared, deterministic group analysis supplies both membership and already-derived root
		// chains. Invalid/empty groups are omitted by the runtime authority.
		const TArray<Paper2DPlusComboChain::FScopedAnimationGroupAnalysis> GroupAnalyses =
			Paper2DPlusComboChain::AnalyzeAnimationGroups(Asset);

		TSet<FString> MappedLowerNames; // union across ALL groups — drives the Unmapped fallback
		for (const Paper2DPlusComboChain::FScopedAnimationGroupAnalysis& Group : GroupAnalyses)
		{
			FTagLensSection Section;
			Section.GroupTag = Group.GroupTag;
			Section.Title = LensTitleForTag(Group.GroupTag);

			TArray<FString> CandidatesLower;
			for (const Paper2DPlusComboChain::FScopedGroupMember& Member : Group.Members)
			{
				const FString LowerName = Member.MoveName.ToLower();
				if (!FlipbookIndexByLower.Contains(LowerName))
				{
					continue;
				}
				MappedLowerNames.Add(LowerName);
				CandidatesLower.Add(LowerName);
			}
			// A key that resolved no member emits NO section — an emptied/removed mapping must
			// disappear from the lens instead of lingering as an empty group shell (legacy-cleanup
			// 2026-07; supersedes the R10 empty-section-mirrors-My-Groups semantic).
			if (CandidatesLower.Num() == 0)
			{
				continue;
			}

			TArray<FString> ChainOrderLower;
			TSet<FString> ChainSeenLower;
			for (const Paper2DPlusComboChain::FScopedAnimationRootAnalysis& Root : Group.Roots)
			{
				for (const FString& ChainName : Root.ChainMoves)
				{
					const FString LowerName = ChainName.ToLower();
					if (!ChainSeenLower.Contains(LowerName))
					{
						ChainSeenLower.Add(LowerName);
						ChainOrderLower.Add(LowerName);
					}
				}
			}
			AppendOrderedMembers(CandidatesLower, ChainOrderLower, Section);
			Out.Add(MoveTemp(Section));
		}

		// The "Unmapped" fallback (LOCKED name — review decision): real flipbooks in NO mapping
		// group, alphabetically (there is no exact group in which a Root Number could be meaningful).
		// Emitted only when non-empty, LAST.
		TArray<FString> UnmappedLower;
		TSet<FString> UnmappedSeen;
		for (int32 Index = 0; Index < Asset->Flipbooks.Num(); ++Index)
		{
			const FString TrimmedName = Asset->Flipbooks[Index].Identity.FlipbookName.TrimStartAndEnd();
			if (TrimmedName.IsEmpty())
			{
				continue;
			}
			const FString LowerName = Asset->Flipbooks[Index].Identity.FlipbookName.ToLower();
			if (MappedLowerNames.Contains(LowerName) || UnmappedSeen.Contains(LowerName))
			{
				continue;
			}
			UnmappedSeen.Add(LowerName);
			UnmappedLower.Add(LowerName);
		}
		if (UnmappedLower.Num() > 0)
		{
			FTagLensSection Fallback;
			Fallback.Title = TEXT("Unmapped");
			Fallback.bUnmapped = true;
			AppendOrderedMembers(UnmappedLower, TArray<FString>(), Fallback);
			Out.Add(MoveTemp(Fallback));
		}

		return Out;
	}

	FEdgeReselectKey MakeEdgeReselectKey(const TArray<FProjectedEdge>& Edges, int32 EdgeIndex)
	{
		FEdgeReselectKey Key;
		if (Edges.IsValidIndex(EdgeIndex))
		{
			Key.FromMoveLower = Edges[EdgeIndex].FromMoveLower;
			Key.TargetMoveLower = Edges[EdgeIndex].TargetMoveLower;
		}
		// Invalid index -> default (empty) key, which IsValid()==false and never matches.
		return Key;
	}

	int32 FindEdgeByReselectKey(const TArray<FProjectedEdge>& Edges, const FEdgeReselectKey& Key)
	{
		if (!Key.IsValid())
		{
			return INDEX_NONE;
		}
		FProjectedEdge KeyAsEdge;
		KeyAsEdge.FromMoveLower = Key.FromMoveLower;
		KeyAsEdge.TargetMoveLower = Key.TargetMoveLower;

		for (int32 Index = 0; Index < Edges.Num(); ++Index)
		{
			if (EdgeValueTuplesEqual(Edges[Index], KeyAsEdge))
			{
				return Index; // one-row-per-pair: the first tuple match IS the edge
			}
		}
		return INDEX_NONE;
	}

	TMap<FString, FVector2D> FirstLayout(const UPaper2DPlusCharacterProfileAsset* Asset,
		const TMap<FString, FVector2D>& OccupiedPositions, const TSet<FString>* MoveFilterLower)
	{
		TMap<FString, FVector2D> Out;
		if (!Asset)
		{
			return Out;
		}

		TSet<FString> OccupiedKeys;
		FBox2D OccupiedBounds(ForceInit);
		for (const TPair<FString, FVector2D>& Pair : OccupiedPositions)
		{
			const FString LowerKey = Pair.Key.ToLower(); // keys are lowercased by contract; lower defensively
			if (MoveFilterLower && !MoveFilterLower->Contains(LowerKey))
			{
				continue; // another group's durable/session placement must not push this group away
			}
			OccupiedKeys.Add(LowerKey);
			OccupiedBounds += Pair.Value;
		}

		// New arrivals place CLEAR of the occupied bounding box — one full cell below it, aligned to
		// its left edge — without moving any stored entry.
		double OriginX = 0.0;
		double OriginY = 0.0;
		if (OccupiedKeys.Num() > 0)
		{
			OriginX = OccupiedBounds.Min.X;
			OriginY = OccupiedBounds.Max.Y + AnimationMapCore_GridCellHeight;
		}

		// Projection node order IS the deterministic participant order: Flipbooks ARRAY order
		// interleaved with first-appearance row targets (placed-only keys are occupied by definition
		// when the caller passes the position map, and sorted regardless).
		const FGraphProjection Projection = ProjectGraph(Asset, MoveFilterLower);
		int32 PlacedCount = 0;
		for (const FProjectedNode& Node : Projection.Nodes)
		{
			if (OccupiedKeys.Contains(Node.MoveNameLower))
			{
				continue; // stored entries are never moved
			}
			const int32 Column = PlacedCount % AnimationMapCore_GridColumns;
			const int32 GridRow = PlacedCount / AnimationMapCore_GridColumns;
			Out.Add(Node.MoveNameLower, FVector2D(
				OriginX + Column * AnimationMapCore_GridCellWidth,
				OriginY + GridRow * AnimationMapCore_GridCellHeight));
			++PlacedCount;
		}
		return Out;
	}

	// =====================================================================================
	// Root / phase analysis — editor-facing aliases forwarding to the RUNTIME core
	// (Paper2DPlusComboChain) so the BP library and the editor share ONE implementation (U6).
	// =====================================================================================

	bool IsConfirmTransition(const FPaper2DPlusMoveTransition& Transition)
	{
		return Paper2DPlusComboChain::IsConfirmTransition(Transition);
	}

	TArray<FString> DeriveComboChain(
		const UPaper2DPlusCharacterProfileAsset* Asset,
		const FGameplayTag& GroupTag,
		const FString& RootMoveName)
	{
		return Paper2DPlusComboChain::DeriveComboChain(Asset, GroupTag, RootMoveName);
	}

	EAnimationPhase PhaseForChainPosition(int32 IndexInChain, int32 ChainLength)
	{
		return Paper2DPlusComboChain::PhaseForChainPosition(IndexInChain, ChainLength);
	}

	TMap<FString, EAnimationPhase> DerivePhasesFromMap(const UPaper2DPlusCharacterProfileAsset* Asset)
	{
		TMap<FString, EAnimationPhase> Out;
		if (!Asset)
		{
			return Out;
		}

		// The runtime batch supplies deterministic group/root ordering, rejects ambiguous roots, keeps
		// traversal inside each exact group, and stops before another numbered root.
		for (const Paper2DPlusComboChain::FScopedAnimationGroupAnalysis& Group :
			Paper2DPlusComboChain::AnalyzeAnimationGroups(Asset))
		{
			for (const Paper2DPlusComboChain::FScopedAnimationRootAnalysis& Root : Group.Roots)
			{
				for (int32 Index = 0; Index < Root.ChainMoves.Num(); ++Index)
				{
					const FString MoveLower = Root.ChainMoves[Index].ToLower();
					if (Out.Contains(MoveLower))
					{
						continue; // deterministic first group/root wins for shared-sink data
					}
					const EAnimationPhase Phase =
						PhaseForChainPosition(Index, Root.ChainMoves.Num());
					if (Phase != EAnimationPhase::None)
					{
						Out.Add(MoveLower, Phase);
					}
				}
			}
		}
		return Out;
	}

	FGameplayTag PhaseToGameplayTag(EAnimationPhase Phase)
	{
		const TCHAR* TagName = nullptr;
		switch (Phase)
		{
		case EAnimationPhase::Startup:  TagName = TEXT("Paper2DPlus.Phase.Startup"); break;
		case EAnimationPhase::Active:   TagName = TEXT("Paper2DPlus.Phase.Active"); break;
		case EAnimationPhase::Recovery: TagName = TEXT("Paper2DPlus.Phase.Recovery"); break;
		default: return FGameplayTag();
		}
		return FGameplayTag::RequestGameplayTag(FName(TagName), /*ErrorIfNotFound=*/false);
	}
}
