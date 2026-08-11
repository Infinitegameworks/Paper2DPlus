// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "Paper2DPlusAnimationTagQuery.h"

#include "Paper2DPlusCharacterProfileAsset.h"
#include "Paper2DPlusComboChain.h"

namespace Paper2DPlusAnimationTagQuery
{
	TMap<FString, FAnimationTagSet> BuildAnimationTagMap(const UPaper2DPlusCharacterProfileAsset* Asset)
	{
		return BuildAnimationTagMap(
			Asset,
			Paper2DPlusComboChain::AnalyzeAnimationGroups(Asset));
	}

	TMap<FString, FAnimationTagSet> BuildAnimationTagMap(
		const UPaper2DPlusCharacterProfileAsset* Asset,
		const TArray<Paper2DPlusComboChain::FScopedAnimationGroupAnalysis>& GroupAnalyses)
	{
		TMap<FString, FAnimationTagSet> Map;
		if (!Asset)
		{
			return Map;
		}

		// 1. Own tags — one map row per lowered flipbook name, FIRST occurrence wins (duplicate names are
		// a validation Error but representable in-memory; first-match mirrors DeriveComboChain/FindEntry).
		for (const FFlipbookProfileEntry& Anim : Asset->Flipbooks)
		{
			if (Anim.Identity.FlipbookName.TrimStartAndEnd().IsEmpty())
			{
				continue;
			}
			const FString Lower = Anim.Identity.FlipbookName.ToLower();
			if (Map.Contains(Lower))
			{
				continue;
			}
			FAnimationTagSet& Set = Map.Add(Lower);
			Set.AnimationName = Anim.Identity.FlipbookName;
			Set.OwnTags = Anim.EditorMeta.AnimationTags;
		}

		// 2. Group-implied — dangling, PaperZD-only, duplicate-membership, and duplicate-profile-name
		// rows fail closed consistently across runtime queries, Blueprint resolution, and projections.
		for (const Paper2DPlusComboChain::FScopedAnimationGroupAnalysis& Group : GroupAnalyses)
		{
			for (const Paper2DPlusComboChain::FScopedGroupMember& Member : Group.Members)
			{
				if (FAnimationTagSet* Set = Map.Find(Member.MoveName.ToLower()))
				{
					Set->GroupImpliedTags.AddTag(Group.GroupTag);
				}
			}
		}

		// 3. Chain-inherited — every in-scope member gains its reaching root's OWN tags (never the
		// root's effective/group tags). Root descriptors preserve exact group identity; the scoped chain
		// cannot cross into another group or pass through another numbered root. Shared members reached
		// independently by multiple valid roots still union every root's authored tags.
		for (const Paper2DPlusComboChain::FScopedAnimationGroupAnalysis& Group : GroupAnalyses)
		{
			for (const Paper2DPlusComboChain::FScopedAnimationRootAnalysis& Root : Group.Roots)
			{
				const FString RootLower = Root.Root.RootMove.ToLower();
				const FAnimationTagSet* RootSet = Map.Find(RootLower);
				const FGameplayTagContainer RootOwnTags =
					RootSet ? RootSet->OwnTags : FGameplayTagContainer();
				for (const FString& Member : Root.ChainMoves)
				{
					const FString MemberLower = Member.ToLower();
					if (MemberLower == RootLower)
					{
						continue; // a root never inherits from itself
					}
					if (FAnimationTagSet* Set = Map.Find(MemberLower))
					{
						Set->ChainInheritedTags.AppendTags(RootOwnTags);
						Set->ReachingRootNames.AddUnique(Root.Root.RootMove);
					}
				}
			}
		}

		// 4. Effective = own ∪ chain-inherited ∪ group-implied (AppendTags dedupes exact duplicates).
		for (TPair<FString, FAnimationTagSet>& Pair : Map)
		{
			Pair.Value.EffectiveTags = Pair.Value.OwnTags;
			Pair.Value.EffectiveTags.AppendTags(Pair.Value.ChainInheritedTags);
			Pair.Value.EffectiveTags.AppendTags(Pair.Value.GroupImpliedTags);
		}
		return Map;
	}

	bool AreExactTagSetsEqual(
		const FGameplayTagContainer& A,
		const FGameplayTagContainer& B)
	{
		return A.Num() == B.Num()
			&& A.HasAllExact(B)
			&& B.HasAllExact(A);
	}

	TArray<const FFlipbookProfileEntry*> FindExactOwnTagMatches(
		const UPaper2DPlusCharacterProfileAsset* Asset,
		const FGameplayTagContainer& QueryTags)
	{
		TArray<const FFlipbookProfileEntry*> Matches;
		if (!Asset || QueryTags.IsEmpty())
		{
			return Matches;
		}

		for (const FFlipbookProfileEntry& Entry : Asset->Flipbooks)
		{
			if (Entry.Identity.FlipbookName.TrimStartAndEnd().IsEmpty()
				|| Entry.Identity.Flipbook.IsNull()
				|| !AreExactTagSetsEqual(Entry.EditorMeta.AnimationTags, QueryTags))
			{
				continue;
			}
			Matches.Add(&Entry);
		}
		return Matches;
	}

	bool AreMatchesContainedByOneRootChain(
		const UPaper2DPlusCharacterProfileAsset* Asset,
		const TArray<const FFlipbookProfileEntry*>& Matches)
	{
		if (!Asset || Matches.Num() < 2)
		{
			return false;
		}

		TSet<FString> MatchNamesLower;
		for (const FFlipbookProfileEntry* Match : Matches)
		{
			if (!Match || Match->Identity.FlipbookName.TrimStartAndEnd().IsEmpty())
			{
				return false;
			}
			MatchNamesLower.Add(Match->Identity.FlipbookName.ToLower());
		}
		if (MatchNamesLower.Num() != Matches.Num())
		{
			return false;
		}

		int32 ContainingChains = 0;
		for (const Paper2DPlusComboChain::FScopedAnimationGroupAnalysis& Group :
			Paper2DPlusComboChain::AnalyzeAnimationGroups(Asset))
		{
			for (const Paper2DPlusComboChain::FScopedAnimationRootAnalysis& Root : Group.Roots)
			{
				TSet<FString> ChainNamesLower;
				for (const FString& MoveName : Root.ChainMoves)
				{
					ChainNamesLower.Add(MoveName.ToLower());
				}

				bool bContainsAll = true;
				for (const FString& MatchNameLower : MatchNamesLower)
				{
					if (!ChainNamesLower.Contains(MatchNameLower))
					{
						bContainsAll = false;
						break;
					}
				}
				if (bContainsAll && ++ContainingChains > 1)
				{
					return false;
				}
			}
		}
		return ContainingChains == 1;
	}
}
