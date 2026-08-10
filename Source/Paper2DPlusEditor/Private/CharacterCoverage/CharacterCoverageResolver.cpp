// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "CharacterCoverage/CharacterCoverageResolver.h"

#include "Paper2DPlusAnimationTagQuery.h"
#include "Paper2DPlusCharacterProfileAsset.h"

namespace
{
	using FAnimationTagSet = Paper2DPlusAnimationTagQuery::FAnimationTagSet;

	struct FMutableCharacterCoverageRow
	{
		FCharacterCoverageRow Row;
		bool bHasSingletonExactOwnMatch = false;
	};

	void CharacterCoverageResolver_SortNames(TArray<FString>& Names)
	{
		Names.Sort(
			[](const FString& A, const FString& B)
			{
				const int32 IgnoreCaseOrder = A.Compare(B, ESearchCase::IgnoreCase);
				return IgnoreCaseOrder == 0
					? A.Compare(B, ESearchCase::CaseSensitive) < 0
					: IgnoreCaseOrder < 0;
			});
	}

	FCharacterCoverageResolveResult CharacterCoverageResolver_ResolveBuiltTagMap(
		const TMap<FString, FAnimationTagSet>& TagMap,
		const FGameplayTagContainer& ExpectedTags)
	{
		TArray<FGameplayTag> SortedExpectedTags;
		ExpectedTags.GetGameplayTagArray(SortedExpectedTags);
		SortedExpectedTags.Sort(
			[](const FGameplayTag& A, const FGameplayTag& B)
			{
				return A.GetTagName().LexicalLess(B.GetTagName());
			});

		TArray<FMutableCharacterCoverageRow> MutableRows;
		MutableRows.Reserve(SortedExpectedTags.Num());
		for (const FGameplayTag& ExpectedTag : SortedExpectedTags)
		{
			FMutableCharacterCoverageRow& Mutable = MutableRows.AddDefaulted_GetRef();
			Mutable.Row.ExpectedTag = ExpectedTag;
		}

		FCharacterCoverageResolveResult Result;
#if WITH_DEV_AUTOMATION_TESTS
		Result.VisitedAnimationNamesForTests.Reserve(TagMap.Num());
#endif

		// Deliberately consume the built map in one pass. Each animation can contribute exact hits to
		// several expected rows, but no row performs its own animation-map traversal.
		for (const TPair<FString, FAnimationTagSet>& Pair : TagMap)
		{
			const FAnimationTagSet& AnimationTags = Pair.Value;
#if WITH_DEV_AUTOMATION_TESTS
			Result.VisitedAnimationNamesForTests.Add(AnimationTags.AnimationName);
#endif
			for (FMutableCharacterCoverageRow& Mutable : MutableRows)
			{
				const FGameplayTag ExpectedTag = Mutable.Row.ExpectedTag;
				const bool bOwnExactMatch = AnimationTags.OwnTags.HasTagExact(ExpectedTag);
				if (bOwnExactMatch)
				{
					Mutable.Row.AuthoredAnimationNames.Add(AnimationTags.AnimationName);

					FGameplayTagContainer SingletonExpectedTag;
					SingletonExpectedTag.AddTag(ExpectedTag);
					Mutable.bHasSingletonExactOwnMatch |=
						Paper2DPlusAnimationTagQuery::AreExactTagSetsEqual(
							AnimationTags.OwnTags,
							SingletonExpectedTag);
				}
				if (!bOwnExactMatch && AnimationTags.GroupImpliedTags.HasTagExact(ExpectedTag))
				{
					Mutable.Row.GroupImpliedAnimationNames.Add(AnimationTags.AnimationName);
				}
				if (!bOwnExactMatch && AnimationTags.ChainInheritedTags.HasTagExact(ExpectedTag))
				{
					Mutable.Row.ChainInheritedAnimationNames.Add(AnimationTags.AnimationName);
				}
			}
		}

#if WITH_DEV_AUTOMATION_TESTS
		CharacterCoverageResolver_SortNames(Result.VisitedAnimationNamesForTests);
#endif
		Result.Rows.Reserve(MutableRows.Num());
		for (FMutableCharacterCoverageRow& Mutable : MutableRows)
		{
			CharacterCoverageResolver_SortNames(Mutable.Row.AuthoredAnimationNames);
			CharacterCoverageResolver_SortNames(Mutable.Row.GroupImpliedAnimationNames);
			CharacterCoverageResolver_SortNames(Mutable.Row.ChainInheritedAnimationNames);

			if (Mutable.Row.AuthoredAnimationNames.Num() > 0)
			{
				Mutable.Row.Status = ECharacterCoverageStatus::Covered;
				Mutable.Row.bSupersetOnly = !Mutable.bHasSingletonExactOwnMatch;
			}
			else if (Mutable.Row.GroupImpliedAnimationNames.Num() > 0
				|| Mutable.Row.ChainInheritedAnimationNames.Num() > 0)
			{
				Mutable.Row.Status = ECharacterCoverageStatus::NearMiss;
			}
			Result.Rows.Add(MoveTemp(Mutable.Row));
		}
		return Result;
	}
}

FCharacterCoverageResolveResult FCharacterCoverageResolver::Resolve(
	const UPaper2DPlusCharacterProfileAsset* Profile,
	const FGameplayTagContainer& ExpectedTags)
{
	const TMap<FString, FAnimationTagSet> TagMap =
		Paper2DPlusAnimationTagQuery::BuildAnimationTagMap(Profile);
	return CharacterCoverageResolver_ResolveBuiltTagMap(TagMap, ExpectedTags);
}

#if WITH_DEV_AUTOMATION_TESTS
FCharacterCoverageResolveResult FCharacterCoverageResolver::ResolveFromTagMapForTests(
	const TMap<FString, Paper2DPlusAnimationTagQuery::FAnimationTagSet>& TagMap,
	const FGameplayTagContainer& ExpectedTags)
{
	return CharacterCoverageResolver_ResolveBuiltTagMap(TagMap, ExpectedTags);
}
#endif
