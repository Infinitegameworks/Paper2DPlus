// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "Paper2DPlusComboChain.h"

#include "Paper2DPlusCharacterProfileAsset.h"
#include "Paper2DPlusTypes.h" // EAnimationPhase

namespace
{
	struct FComboChainProfileMatch
	{
		const FFlipbookProfileEntry* Entry = nullptr;
		int32 Count = 0;
	};

	struct FComboChainGroupMember
	{
		const FFlipbookTagMappingEntry* MappingEntry = nullptr;
		const FFlipbookProfileEntry* ProfileEntry = nullptr;
	};

	using FComboChainProfileIndex = TMap<FString, FComboChainProfileMatch>;
	using FComboChainGroupMembers = TMap<FString, FComboChainGroupMember>;

	void ComboChain_BuildProfileIndex(
		const UPaper2DPlusCharacterProfileAsset* Asset,
		FComboChainProfileIndex& OutIndex)
	{
		OutIndex.Reset();
		if (!Asset)
		{
			return;
		}

		for (const FFlipbookProfileEntry& Entry : Asset->Flipbooks)
		{
			if (Entry.Identity.FlipbookName.TrimStartAndEnd().IsEmpty())
			{
				continue;
			}

			FComboChainProfileMatch& Match = OutIndex.FindOrAdd(Entry.Identity.FlipbookName.ToLower());
			if (++Match.Count == 1)
			{
				Match.Entry = &Entry;
			}
		}
	}

	bool ComboChain_BuildGroupMembers(
		const UPaper2DPlusCharacterProfileAsset* Asset,
		const FGameplayTag& GroupTag,
		const FComboChainProfileIndex& ProfileIndex,
		const FFlipbookTagMapping*& OutMapping,
		FComboChainGroupMembers& OutMembers)
	{
		OutMapping = nullptr;
		OutMembers.Reset();
		if (!Asset || !GroupTag.IsValid())
		{
			return false;
		}

		const FFlipbookTagMapping* Mapping = Asset->TagMappings.Find(GroupTag);
		if (!Mapping)
		{
			return false;
		}

		TSet<FString> DuplicateNames;
		for (const FFlipbookTagMappingEntry& MappingEntry : Mapping->Entries)
		{
			if (MappingEntry.FlipbookName.TrimStartAndEnd().IsEmpty())
			{
				continue;
			}

			const FString NameLower = MappingEntry.FlipbookName.ToLower();
			if (OutMembers.Contains(NameLower))
			{
				DuplicateNames.Add(NameLower);
				continue;
			}

			const FComboChainProfileMatch* Match = ProfileIndex.Find(NameLower);
			// Group/root topology is authored soft-reference data and must not change with asset
			// residency. Hard object outputs load only after a unique root/transition is selected.
			if (Match && Match->Count == 1 && Match->Entry && !Match->Entry->Identity.Flipbook.IsNull())
			{
				OutMembers.Add(NameLower, { &MappingEntry, Match->Entry });
			}
		}

		for (const FString& DuplicateName : DuplicateNames)
		{
			OutMembers.Remove(DuplicateName);
		}

		OutMapping = Mapping;
		return OutMembers.Num() > 0;
	}

	void ComboChain_AppendScopedMembers(
		const FFlipbookTagMapping& Mapping,
		const FComboChainGroupMembers& Members,
		TArray<Paper2DPlusComboChain::FScopedGroupMember>& OutMembers)
	{
		for (const FFlipbookTagMappingEntry& MappingEntry : Mapping.Entries)
		{
			const FComboChainGroupMember* Member = Members.Find(MappingEntry.FlipbookName.ToLower());
			if (!Member || Member->MappingEntry != &MappingEntry)
			{
				continue;
			}

			Paper2DPlusComboChain::FScopedGroupMember& OutMember = OutMembers.AddDefaulted_GetRef();
			OutMember.MoveName = Member->ProfileEntry->Identity.FlipbookName;
		}
	}

	const FComboChainGroupMember* ComboChain_ResolveRoot(
		const FFlipbookTagMapping& Mapping,
		const FComboChainGroupMembers& Members,
		const FString& RootMoveName)
	{
		if (RootMoveName.TrimStartAndEnd().IsEmpty())
		{
			return nullptr;
		}

		const FString NameLower = RootMoveName.ToLower();
		const FComboChainGroupMember* Member = Members.Find(NameLower);
		// Members already excluded duplicate/dangling membership; the entry must carry the flag.
		return Member && Member->MappingEntry->bIsChainStart ? Member : nullptr;
	}

	/** Flagged chain-start names in AUTHORED ENTRY ORDER (first spelling wins on dirty duplicates —
	 *  duplicated membership fails later in ResolveRoot because it was dropped from Members). */
	TArray<FString> ComboChain_GetCandidateRootNames(const FFlipbookTagMapping& Mapping)
	{
		TArray<FString> Candidates;
		TSet<FString> SeenLower;
		for (const FFlipbookTagMappingEntry& Entry : Mapping.Entries)
		{
			const FString NameLower = Entry.FlipbookName.ToLower();
			if (Entry.bIsChainStart && !NameLower.TrimStartAndEnd().IsEmpty()
				&& !SeenLower.Contains(NameLower))
			{
				SeenLower.Add(NameLower);
				Candidates.Add(Entry.FlipbookName);
			}
		}
		return Candidates;
	}

	TArray<FString> ComboChain_DeriveChain(
		const FComboChainGroupMembers& Members,
		const FComboChainGroupMember* RootMember)
	{
		TArray<FString> Chain;
		if (!RootMember)
		{
			return Chain;
		}

		TSet<FString> Visited;
		TArray<const FComboChainGroupMember*> Stack;
		Stack.Push(RootMember);
		while (Stack.Num() > 0)
		{
			const FComboChainGroupMember* Member = Stack.Pop();
			const FString MemberLower = Member->ProfileEntry->Identity.FlipbookName.ToLower();
			if (Visited.Contains(MemberLower))
			{
				continue;
			}

			Visited.Add(MemberLower);
			Chain.Add(Member->ProfileEntry->Identity.FlipbookName);

			const TArray<FPaper2DPlusMoveTransition>& Rows = Member->ProfileEntry->TransitionData.Transitions;
			for (int32 Index = Rows.Num() - 1; Index >= 0; --Index)
			{
				if (!Paper2DPlusComboChain::IsConfirmTransition(Rows[Index]))
				{
					continue;
				}

				const FString TargetLower = Rows[Index].TargetMove.ToLower();
				const FComboChainGroupMember* Target = Members.Find(TargetLower);
				if (!Target || Visited.Contains(TargetLower))
				{
					continue;
				}

				// Every OTHER flagged chain start is a hard boundary — one chain never absorbs another.
				if (Target != RootMember && Target->MappingEntry->bIsChainStart)
				{
					continue;
				}
				Stack.Push(Target);
			}
		}
		return Chain;
	}

	bool ComboChain_BuildGroupAnalysis(
		const UPaper2DPlusCharacterProfileAsset* Asset,
		const FGameplayTag& GroupTag,
		const FComboChainProfileIndex& ProfileIndex,
		bool bDeriveChains,
		const FString& SelectedRootName,
		Paper2DPlusComboChain::FScopedAnimationGroupAnalysis& OutAnalysis)
	{
		OutAnalysis = Paper2DPlusComboChain::FScopedAnimationGroupAnalysis();
		const FFlipbookTagMapping* Mapping = nullptr;
		FComboChainGroupMembers Members;
		if (!ComboChain_BuildGroupMembers(Asset, GroupTag, ProfileIndex, Mapping, Members) || !Mapping)
		{
			return false;
		}

		OutAnalysis.GroupTag = GroupTag;
		ComboChain_AppendScopedMembers(*Mapping, Members, OutAnalysis.Members);
		for (const FString& RootName : ComboChain_GetCandidateRootNames(*Mapping))
		{
			if (!SelectedRootName.IsEmpty()
				&& !RootName.Equals(SelectedRootName, ESearchCase::IgnoreCase))
			{
				continue;
			}
			const FComboChainGroupMember* RootMember = ComboChain_ResolveRoot(*Mapping, Members, RootName);
			if (!RootMember)
			{
				continue;
			}

			Paper2DPlusComboChain::FScopedAnimationRootAnalysis& RootAnalysis =
				OutAnalysis.Roots.AddDefaulted_GetRef();
			RootAnalysis.Root.GroupTag = GroupTag;
			RootAnalysis.Root.RootMove = RootMember->ProfileEntry->Identity.FlipbookName;
			RootAnalysis.Root.ChainTags = RootMember->MappingEntry->ChainTags;
			if (bDeriveChains)
			{
				RootAnalysis.ChainMoves = ComboChain_DeriveChain(Members, RootMember);
			}
		}
		return true;
	}

	// Backtracking search for the combo SPINE. Preference order: a path TERMINATING at a flagged
	// Chain End beats any longer path that does not (the definitive start→end line — trailing
	// recovery wired after the end never counts); among equals, STRICTLY longer wins; candidates
	// iterate in authored row order and Best only replaces on strict improvement, so the earliest
	// authored route wins remaining ties — deterministic. Traversal never expands PAST an end. The
	// expansion cap bounds pathologically dense authored graphs; hitting it keeps the best line
	// found so far (still deterministic — traversal order is pure data).
	constexpr int32 ComboChain_MaxSpineExpansions = 100000;

	void ComboChain_SpineRecurse(
		const FComboChainGroupMembers& Members,
		const FComboChainGroupMember* Member,
		TSet<FString>& Visited,
		TArray<const FComboChainGroupMember*>& Current,
		TArray<const FComboChainGroupMember*>& Best,
		bool& bBestEndsAtEnd,
		int32& Expansions)
	{
		const bool bHereEndsAtEnd = Member->MappingEntry->bIsChainEnd;
		const bool bBetter = bBestEndsAtEnd
			? (bHereEndsAtEnd && Current.Num() > Best.Num())
			: (bHereEndsAtEnd || Current.Num() > Best.Num());
		if (bBetter)
		{
			Best = Current;
			bBestEndsAtEnd = bHereEndsAtEnd;
		}
		if (++Expansions > ComboChain_MaxSpineExpansions)
		{
			return;
		}
		// A flagged Chain End is terminal — the countable line never continues past it.
		if (bHereEndsAtEnd)
		{
			return;
		}

		for (const FPaper2DPlusMoveTransition& Row : Member->ProfileEntry->TransitionData.Transitions)
		{
			if (!Paper2DPlusComboChain::IsConfirmTransition(Row))
			{
				continue;
			}

			const FString TargetLower = Row.TargetMove.ToLower();
			const FComboChainGroupMember* Target = Members.Find(TargetLower);
			if (!Target || Visited.Contains(TargetLower))
			{
				continue;
			}
			// Same hard boundary as ComboChain_DeriveChain: every other flagged chain start stops the
			// walk (the starting opener itself is already in Visited, so the bare flag test is safe).
			if (Target->MappingEntry->bIsChainStart)
			{
				continue;
			}

			Visited.Add(TargetLower);
			Current.Push(Target);
			ComboChain_SpineRecurse(Members, Target, Visited, Current, Best, bBestEndsAtEnd, Expansions);
			Current.Pop();
			Visited.Remove(TargetLower);
			if (Expansions > ComboChain_MaxSpineExpansions)
			{
				return;
			}
		}
	}

	TArray<FGameplayTag> ComboChain_GetSortedGroupTags(const UPaper2DPlusCharacterProfileAsset* Asset)
	{
		TArray<FGameplayTag> GroupTags;
		if (!Asset)
		{
			return GroupTags;
		}

		for (const TPair<FGameplayTag, FFlipbookTagMapping>& Pair : Asset->TagMappings)
		{
			if (Pair.Key.IsValid())
			{
				GroupTags.Add(Pair.Key);
			}
		}
		GroupTags.Sort([](const FGameplayTag& A, const FGameplayTag& B)
		{
			return A.ToString() < B.ToString();
		});
		return GroupTags;
	}
}

namespace Paper2DPlusComboChain
{
	bool IsConfirmTransition(const FPaper2DPlusMoveTransition& Transition)
	{
		// TASK-108: pure From->To arrows — every non-empty-target row is a chain edge (the historical
		// Always-or-OnHit filter died with the per-row Condition field).
		return !Transition.TargetMove.IsEmpty();
	}

	bool GetAnimationGroupMembers(
		const UPaper2DPlusCharacterProfileAsset* Asset,
		const FGameplayTag& GroupTag,
		TArray<FScopedGroupMember>& OutMembers)
	{
		OutMembers.Reset();
		FComboChainProfileIndex ProfileIndex;
		ComboChain_BuildProfileIndex(Asset, ProfileIndex);
		const FFlipbookTagMapping* Mapping = nullptr;
		FComboChainGroupMembers Members;
		if (!ComboChain_BuildGroupMembers(Asset, GroupTag, ProfileIndex, Mapping, Members) || !Mapping)
		{
			return false;
		}

		ComboChain_AppendScopedMembers(*Mapping, Members, OutMembers);
		return OutMembers.Num() > 0;
	}

	bool AnalyzeAnimationGroup(
		const UPaper2DPlusCharacterProfileAsset* Asset,
		const FGameplayTag& GroupTag,
		FScopedAnimationGroupAnalysis& OutAnalysis)
	{
		FComboChainProfileIndex ProfileIndex;
		ComboChain_BuildProfileIndex(Asset, ProfileIndex);
		return ComboChain_BuildGroupAnalysis(
			Asset, GroupTag, ProfileIndex, true, FString(), OutAnalysis);
	}

	bool AnalyzeAnimationGroupTopology(
		const UPaper2DPlusCharacterProfileAsset* Asset,
		const FGameplayTag& GroupTag,
		FScopedAnimationGroupAnalysis& OutAnalysis)
	{
		FComboChainProfileIndex ProfileIndex;
		ComboChain_BuildProfileIndex(Asset, ProfileIndex);
		return ComboChain_BuildGroupAnalysis(
			Asset, GroupTag, ProfileIndex, false, FString(), OutAnalysis);
	}

	bool AnalyzeAnimationRoot(
		const UPaper2DPlusCharacterProfileAsset* Asset,
		const FGameplayTag& GroupTag,
		const FString& RootMoveName,
		FScopedAnimationGroupAnalysis& OutAnalysis)
	{
		OutAnalysis = FScopedAnimationGroupAnalysis();
		if (RootMoveName.TrimStartAndEnd().IsEmpty())
		{
			// Keep the exact-group topology available to callers that distinguish a missing start from
			// a missing group, but never let an empty name double as a "select every start" request.
			AnalyzeAnimationGroupTopology(Asset, GroupTag, OutAnalysis);
			OutAnalysis.Roots.Reset();
			return false;
		}

		FComboChainProfileIndex ProfileIndex;
		ComboChain_BuildProfileIndex(Asset, ProfileIndex);
		return ComboChain_BuildGroupAnalysis(
				Asset, GroupTag, ProfileIndex, true, RootMoveName, OutAnalysis)
			&& OutAnalysis.Roots.Num() == 1;
	}

	TArray<FScopedAnimationGroupAnalysis> AnalyzeAnimationGroups(
		const UPaper2DPlusCharacterProfileAsset* Asset)
	{
		TArray<FScopedAnimationGroupAnalysis> Analyses;
		if (!Asset)
		{
			return Analyses;
		}

		FComboChainProfileIndex ProfileIndex;
		ComboChain_BuildProfileIndex(Asset, ProfileIndex);
		for (const FGameplayTag& GroupTag : ComboChain_GetSortedGroupTags(Asset))
		{
			FScopedAnimationGroupAnalysis Analysis;
			if (ComboChain_BuildGroupAnalysis(
				Asset, GroupTag, ProfileIndex, true, FString(), Analysis))
			{
				Analyses.Add(MoveTemp(Analysis));
			}
		}
		return Analyses;
	}

	TArray<FScopedAnimationGroupAnalysis> AnalyzeAnimationGroupTopologies(
		const UPaper2DPlusCharacterProfileAsset* Asset)
	{
		TArray<FScopedAnimationGroupAnalysis> Analyses;
		if (!Asset)
		{
			return Analyses;
		}

		FComboChainProfileIndex ProfileIndex;
		ComboChain_BuildProfileIndex(Asset, ProfileIndex);
		for (const FGameplayTag& GroupTag : ComboChain_GetSortedGroupTags(Asset))
		{
			FScopedAnimationGroupAnalysis Analysis;
			if (ComboChain_BuildGroupAnalysis(
				Asset, GroupTag, ProfileIndex, false, FString(), Analysis))
			{
				Analyses.Add(MoveTemp(Analysis));
			}
		}
		return Analyses;
	}

	TArray<FScopedAnimationRoot> GetAnimationRoots(const UPaper2DPlusCharacterProfileAsset* Asset)
	{
		TArray<FScopedAnimationRoot> Roots;
		for (const FScopedAnimationGroupAnalysis& Group : AnalyzeAnimationGroupTopologies(Asset))
		{
			for (const FScopedAnimationRootAnalysis& Root : Group.Roots)
			{
				Roots.Add(Root.Root);
			}
		}
		return Roots;
	}

	bool FindAnimationRoot(
		const UPaper2DPlusCharacterProfileAsset* Asset,
		const FGameplayTag& GroupTag,
		const FString& RootMoveName,
		FScopedAnimationRoot& OutRoot)
	{
		OutRoot = FScopedAnimationRoot();
		FScopedAnimationGroupAnalysis Group;
		if (!AnalyzeAnimationGroupTopology(Asset, GroupTag, Group))
		{
			return false;
		}

		for (const FScopedAnimationRootAnalysis& Root : Group.Roots)
		{
			if (Root.Root.RootMove.Equals(RootMoveName, ESearchCase::IgnoreCase))
			{
				OutRoot = Root.Root;
				return true;
			}
		}
		return false;
	}

	TArray<FString> DeriveComboChain(
		const UPaper2DPlusCharacterProfileAsset* Asset,
		const FGameplayTag& GroupTag,
		const FString& RootMoveName)
	{
		FScopedAnimationGroupAnalysis Group;
		if (!AnalyzeAnimationRoot(Asset, GroupTag, RootMoveName, Group))
		{
			return TArray<FString>();
		}

		for (const FScopedAnimationRootAnalysis& Root : Group.Roots)
		{
			if (Root.Root.RootMove.Equals(RootMoveName, ESearchCase::IgnoreCase))
			{
				return Root.ChainMoves;
			}
		}
		return TArray<FString>();
	}

	TArray<FString> DeriveComboSpine(
		const UPaper2DPlusCharacterProfileAsset* Asset,
		const FGameplayTag& GroupTag,
		const FString& RootMoveName)
	{
		TArray<FString> Spine;
		if (RootMoveName.TrimStartAndEnd().IsEmpty())
		{
			return Spine;
		}

		FComboChainProfileIndex ProfileIndex;
		ComboChain_BuildProfileIndex(Asset, ProfileIndex);
		const FFlipbookTagMapping* Mapping = nullptr;
		FComboChainGroupMembers Members;
		if (!ComboChain_BuildGroupMembers(Asset, GroupTag, ProfileIndex, Mapping, Members) || !Mapping)
		{
			return Spine;
		}

		const FComboChainGroupMember* RootMember = ComboChain_ResolveRoot(*Mapping, Members, RootMoveName);
		if (!RootMember)
		{
			return Spine;
		}

		TSet<FString> Visited;
		TArray<const FComboChainGroupMember*> Current;
		TArray<const FComboChainGroupMember*> Best;
		Visited.Add(RootMember->ProfileEntry->Identity.FlipbookName.ToLower());
		Current.Push(RootMember);
		Best = Current;
		bool bBestEndsAtEnd = RootMember->MappingEntry->bIsChainEnd;
		int32 Expansions = 0;
		ComboChain_SpineRecurse(Members, RootMember, Visited, Current, Best, bBestEndsAtEnd, Expansions);

		Spine.Reserve(Best.Num());
		for (const FComboChainGroupMember* Member : Best)
		{
			Spine.Add(Member->ProfileEntry->Identity.FlipbookName);
		}
		return Spine;
	}

	EComboSpineFindResult FindComboSpineForMove(
		const UPaper2DPlusCharacterProfileAsset* Asset,
		const FString& MoveName,
		FScopedAnimationRoot& OutRoot,
		TArray<FString>& OutSpine)
	{
		OutRoot = FScopedAnimationRoot();
		OutSpine.Reset();
		if (MoveName.TrimStartAndEnd().IsEmpty())
		{
			return EComboSpineFindResult::NotChained;
		}

		const FScopedAnimationRoot* ReachingRoot = nullptr;
		int32 ReachCount = 0;
		const TArray<FScopedAnimationGroupAnalysis> Groups = AnalyzeAnimationGroups(Asset);
		for (const FScopedAnimationGroupAnalysis& Group : Groups)
		{
			for (const FScopedAnimationRootAnalysis& Root : Group.Roots)
			{
				const bool bReaches = Root.ChainMoves.ContainsByPredicate(
					[&MoveName](const FString& ChainMove)
					{
						return ChainMove.Equals(MoveName, ESearchCase::IgnoreCase);
					});
				if (bReaches)
				{
					++ReachCount;
					ReachingRoot = &Root.Root;
				}
			}
		}

		if (ReachCount == 0)
		{
			return EComboSpineFindResult::NotChained;
		}
		if (ReachCount > 1)
		{
			return EComboSpineFindResult::AmbiguousChain;
		}

		OutSpine = DeriveComboSpine(Asset, ReachingRoot->GroupTag, ReachingRoot->RootMove);
		if (OutSpine.Num() == 0)
		{
			return EComboSpineFindResult::NotChained;
		}
		OutRoot = *ReachingRoot;
		return EComboSpineFindResult::Found;
	}

	EAnimationPhase PhaseForChainPosition(int32 IndexInChain, int32 ChainLength)
	{
		if (ChainLength < 2 || IndexInChain < 0 || IndexInChain >= ChainLength)
		{
			return EAnimationPhase::None;
		}
		if (IndexInChain == 0)
		{
			return EAnimationPhase::Startup;
		}
		if (IndexInChain == ChainLength - 1)
		{
			return EAnimationPhase::Recovery;
		}
		return EAnimationPhase::Active;
	}
}
