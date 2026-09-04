// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "Paper2DPlusAnimationMapLibrary.h"

#include "Paper2DPlusCharacterProfileAsset.h"
#include "Paper2DPlusAnimationTagQuery.h"
#include "Paper2DPlusComboChain.h"
#include "Paper2DPlusMoveTransition.h"
#include "PaperFlipbook.h"

namespace
{
	enum class EAnimationMapEntryResult : uint8
	{
		Success,
		NotFound,
		Ambiguous
	};

	/** The base or active variant IS the key. Candidate collection and directional collision rules
	 *  come from the Profile-owned seam; Animation Map preserves its stricter legacy duplicate policy. */
	EAnimationMapEntryResult AnimationMap_ResolveEntryByFlipbook(
		const UPaper2DPlusCharacterProfileAsset* Profile,
		UPaperFlipbook* Flipbook,
		const FFlipbookProfileEntry*& OutEntry)
	{
		OutEntry = nullptr;
		if (!Profile || !Flipbook)
		{
			return EAnimationMapEntryResult::NotFound;
		}

		bool bAmbiguous = false;
		OutEntry = Profile->ResolveLogicalAnimationOwner(
			Flipbook,
			bAmbiguous,
			EPaper2DPlusLogicalOwnerDuplicatePolicy::RequireUniqueOwner);
		if (bAmbiguous)
		{
			return EAnimationMapEntryResult::Ambiguous;
		}
		return OutEntry ? EAnimationMapEntryResult::Success : EAnimationMapEntryResult::NotFound;
	}

	bool AnimationMap_MatchesCriteria(
		const FGameplayTagContainer& Tags,
		const FPaper2DPlusAnimationSelectionCriteria& Criteria)
	{
		if (!Criteria.RequiredAllTags.IsEmpty() && !Tags.HasAll(Criteria.RequiredAllTags))
		{
			return false;
		}
		if (!Criteria.RequiredAnyTags.IsEmpty() && !Tags.HasAny(Criteria.RequiredAnyTags))
		{
			return false;
		}
		return Criteria.ExcludedTags.IsEmpty() || !Tags.HasAny(Criteria.ExcludedTags);
	}

	struct FAnimationMapDirectTransition
	{
		const FFlipbookProfileEntry* Target = nullptr;
		FGameplayTag EffectivePhase;
	};

	/** Every authored From -> To row on Original is in view — no group or root scoping. Criteria
	 *  match against each target's EFFECTIVE tags (the shared Paper2DPlusAnimationTagQuery batch,
	 *  built only when Criteria is non-empty). */
	void AnimationMap_CollectDirectTargets(
		const UPaper2DPlusCharacterProfileAsset* Profile,
		const FFlipbookProfileEntry& Original,
		const FPaper2DPlusAnimationSelectionCriteria& Criteria,
		TArray<FAnimationMapDirectTransition>& OutTransitions)
	{
		OutTransitions.Reset();
		const bool bHasCriteria = !Criteria.RequiredAllTags.IsEmpty()
			|| !Criteria.RequiredAnyTags.IsEmpty()
			|| !Criteria.ExcludedTags.IsEmpty();
		TMap<FString, Paper2DPlusAnimationTagQuery::FAnimationTagSet> TagMap;
		if (bHasCriteria)
		{
			TagMap = Paper2DPlusAnimationTagQuery::BuildAnimationTagMap(Profile);
		}

		TSet<FString> SeenTargets;
		for (const FPaper2DPlusMoveTransition& Row : Original.TransitionData.Transitions)
		{
			const FString TargetLower = Row.TargetMove.ToLower();
			if (!Paper2DPlusComboChain::IsConfirmTransition(Row) || SeenTargets.Contains(TargetLower))
			{
				continue;
			}
			SeenTargets.Add(TargetLower);

			const FFlipbookProfileEntry* Target = Profile->FindFlipbookDataPtr(Row.TargetMove);
			if (!Target)
			{
				continue;
			}

			if (bHasCriteria)
			{
				const Paper2DPlusAnimationTagQuery::FAnimationTagSet* TagSet = TagMap.Find(TargetLower);
				const FGameplayTagContainer& TargetTags = TagSet
					? TagSet->EffectiveTags
					: Target->EditorMeta.AnimationTags;
				if (!AnimationMap_MatchesCriteria(TargetTags, Criteria))
				{
					continue;
				}
			}
			FAnimationMapDirectTransition& Transition = OutTransitions.AddDefaulted_GetRef();
			Transition.Target = Target;
			Transition.EffectivePhase = Row.GetEffectivePhaseTag(Target->EditorMeta.PhaseTag);
		}
	}
}

EPaper2DPlusAnimationTagFindResult UPaper2DPlusAnimationMapLibrary::FindAnimationByExactTags(
	UPaper2DPlusCharacterProfileAsset* Profile,
	const FGameplayTagContainer& ExactTags,
	UPaperFlipbook*& OutFlipbook)
{
	OutFlipbook = nullptr;
	if (!Profile || ExactTags.IsEmpty())
	{
		return EPaper2DPlusAnimationTagFindResult::InvalidRequest;
	}

	const TArray<const FFlipbookProfileEntry*> Matches =
		Paper2DPlusAnimationTagQuery::FindExactOwnTagMatches(Profile, ExactTags);
	if (Matches.Num() == 0)
	{
		return EPaper2DPlusAnimationTagFindResult::NoMatch;
	}
	if (Matches.Num() > 1)
	{
		return EPaper2DPlusAnimationTagFindResult::Ambiguous;
	}

	OutFlipbook = Matches[0]->Identity.Flipbook.LoadSynchronous();
	return OutFlipbook
		? EPaper2DPlusAnimationTagFindResult::Success
		: EPaper2DPlusAnimationTagFindResult::NoMatch;
}

TArray<UPaperFlipbook*> UPaper2DPlusAnimationMapLibrary::GetComboOpenerFlipbooks(
	UPaper2DPlusCharacterProfileAsset* Profile)
{
	TArray<UPaperFlipbook*> Result;
	if (!Profile)
	{
		return Result;
	}

	for (const Paper2DPlusComboChain::FScopedAnimationRoot& Root :
		Paper2DPlusComboChain::GetAnimationRoots(Profile))
	{
		const FFlipbookProfileEntry* Entry = Profile->FindFlipbookDataPtr(Root.RootMove);
		if (UPaperFlipbook* Loaded = Entry ? Entry->Identity.Flipbook.LoadSynchronous() : nullptr)
		{
			Result.AddUnique(Loaded);
		}
	}
	return Result;
}

bool UPaper2DPlusAnimationMapLibrary::GetAnimationTransitionInfo(
	UPaper2DPlusCharacterProfileAsset* Profile,
	UPaperFlipbook* Flipbook,
	const FPaper2DPlusAnimationSelectionCriteria& Criteria,
	FPaper2DPlusAnimationTransitionInfo& OutInfo)
{
	OutInfo = FPaper2DPlusAnimationTransitionInfo();
	const FFlipbookProfileEntry* Original = nullptr;
	if (AnimationMap_ResolveEntryByFlipbook(Profile, Flipbook, Original)
		!= EAnimationMapEntryResult::Success)
	{
		return false;
	}

	TArray<FAnimationMapDirectTransition> Transitions;
	AnimationMap_CollectDirectTargets(Profile, *Original, Criteria, Transitions);
	OutInfo.bHasTransitions = Transitions.Num() > 0;
	for (const FAnimationMapDirectTransition& Transition : Transitions)
	{
		if (Transition.EffectivePhase.IsValid())
		{
			OutInfo.bHasPhases = true;
			OutInfo.AvailablePhases.AddUnique(Transition.EffectivePhase);
		}
	}
	return true;
}

namespace
{
	/** The resolve proper; the public node wraps it so bSuccess is derived at ONE exit for every return path. */
	EPaper2DPlusAnimationResolveResult AnimationMap_ResolveTransition(
		UPaper2DPlusCharacterProfileAsset* Profile,
		UPaperFlipbook* Flipbook,
		FGameplayTag RequestedPhase,
		const FPaper2DPlusAnimationSelectionCriteria& Criteria,
		UPaperFlipbook*& OutFlipbook);
}

EPaper2DPlusAnimationResolveResult UPaper2DPlusAnimationMapLibrary::ResolveAnimationTransition(
	UPaper2DPlusCharacterProfileAsset* Profile,
	UPaperFlipbook* Flipbook,
	FGameplayTag RequestedPhase,
	const FPaper2DPlusAnimationSelectionCriteria& Criteria,
	UPaperFlipbook*& OutFlipbook,
	bool& bSuccess)
{
	const EPaper2DPlusAnimationResolveResult Result =
		AnimationMap_ResolveTransition(Profile, Flipbook, RequestedPhase, Criteria, OutFlipbook);
	bSuccess = (Result == EPaper2DPlusAnimationResolveResult::Success);
	return Result;
}

namespace
{
EPaper2DPlusAnimationResolveResult AnimationMap_ResolveTransition(
	UPaper2DPlusCharacterProfileAsset* Profile,
	UPaperFlipbook* Flipbook,
	FGameplayTag RequestedPhase,
	const FPaper2DPlusAnimationSelectionCriteria& Criteria,
	UPaperFlipbook*& OutFlipbook)
{
	OutFlipbook = nullptr;
	if (!Profile || !Flipbook || !RequestedPhase.IsValid())
	{
		return EPaper2DPlusAnimationResolveResult::InvalidRequest;
	}

	const FFlipbookProfileEntry* Original = nullptr;
	switch (AnimationMap_ResolveEntryByFlipbook(Profile, Flipbook, Original))
	{
	case EAnimationMapEntryResult::NotFound:
		return EPaper2DPlusAnimationResolveResult::FlipbookNotInMap;
	case EAnimationMapEntryResult::Ambiguous:
		return EPaper2DPlusAnimationResolveResult::AmbiguousFlipbook;
	default:
		break;
	}

	TArray<FAnimationMapDirectTransition> Transitions;
	AnimationMap_CollectDirectTargets(Profile, *Original, Criteria, Transitions);
	Transitions.RemoveAll([RequestedPhase](const FAnimationMapDirectTransition& Transition)
	{
		return !Transition.EffectivePhase.IsValid()
			|| !Transition.EffectivePhase.MatchesTag(RequestedPhase);
	});
	if (Transitions.Num() == 0)
	{
		return EPaper2DPlusAnimationResolveResult::NoMatch;
	}
	if (Transitions.Num() > 1)
	{
		return EPaper2DPlusAnimationResolveResult::Ambiguous;
	}

	OutFlipbook = Transitions[0].Target->Identity.Flipbook.LoadSynchronous();
	return OutFlipbook
		? EPaper2DPlusAnimationResolveResult::Success
		: EPaper2DPlusAnimationResolveResult::NoMatch;
}
}

namespace
{
	/** Resolve OpenerName (a FLAGGED Chain Start in exactly one exact group) to its derived main
	 *  line. NotChainStart when unflagged/dangling; AmbiguousChain when flagged in 2+ groups. */
	EPaper2DPlusComboChainResult AnimationMap_ResolveComboLine(
		const UPaper2DPlusCharacterProfileAsset* Profile,
		const FString& OpenerName,
		TArray<FString>& OutSpine)
	{
		const Paper2DPlusComboChain::FScopedAnimationRoot* Match = nullptr;
		int32 FlagCount = 0;
		const TArray<Paper2DPlusComboChain::FScopedAnimationRoot> Roots =
			Paper2DPlusComboChain::GetAnimationRoots(Profile);
		for (const Paper2DPlusComboChain::FScopedAnimationRoot& Root : Roots)
		{
			if (Root.RootMove.Equals(OpenerName, ESearchCase::IgnoreCase))
			{
				++FlagCount;
				Match = &Root;
			}
		}
		if (FlagCount == 0)
		{
			return EPaper2DPlusComboChainResult::NotChainStart;
		}
		if (FlagCount > 1)
		{
			return EPaper2DPlusComboChainResult::AmbiguousChain;
		}

		OutSpine = Paper2DPlusComboChain::DeriveComboSpine(Profile, Match->GroupTag, Match->RootMove);
		return OutSpine.Num() > 0
			? EPaper2DPlusComboChainResult::Success
			: EPaper2DPlusComboChainResult::NotChainStart; // dangling/invalid start fails closed
	}

	/**
	 * Shared tail for the by-index entry modes: Index walks the resolved main line. OutChainLength
	 * is reported on Success AND IndexOutOfRange (the "combo finished" signal a counter needs);
	 * every other failure leaves both outputs cleared by the caller.
	 */
	EPaper2DPlusComboChainResult AnimationMap_ResolveComboStep(
		const UPaper2DPlusCharacterProfileAsset* Profile,
		const FString& OpenerName,
		int32 Index,
		UPaperFlipbook*& OutFlipbook,
		int32& OutChainLength)
	{
		TArray<FString> Spine;
		const EPaper2DPlusComboChainResult LineResult =
			AnimationMap_ResolveComboLine(Profile, OpenerName, Spine);
		if (LineResult != EPaper2DPlusComboChainResult::Success)
		{
			return LineResult;
		}

		OutChainLength = Spine.Num();
		if (!Spine.IsValidIndex(Index))
		{
			return EPaper2DPlusComboChainResult::IndexOutOfRange; // OutChainLength stays valid here
		}

		const FFlipbookProfileEntry* Entry = Profile->FindFlipbookDataPtr(Spine[Index]);
		OutFlipbook = Entry ? Entry->Identity.Flipbook.LoadSynchronous() : nullptr;
		if (!OutFlipbook)
		{
			OutChainLength = 0;
			return EPaper2DPlusComboChainResult::LoadFailed;
		}
		return EPaper2DPlusComboChainResult::Success;
	}

	/**
	 * Resolve ExactTags to a chain OPENER name. Precedence: (1) exactly one Chain Start whose own
	 * authored CHAIN TAGS container equals the query (the chain's identity); (2) only when NO chain
	 * container matches, exactly one animation whose own AnimationTags equal the query (the
	 * FindAnimationByExactTags fallback). Multiple matches in the winning dimension fail closed.
	 */
	EPaper2DPlusComboChainResult AnimationMap_ResolveOpenerByTags(
		const UPaper2DPlusCharacterProfileAsset* Profile,
		const FGameplayTagContainer& ExactTags,
		FString& OutOpenerName)
	{
		const Paper2DPlusComboChain::FScopedAnimationRoot* ChainMatch = nullptr;
		int32 ChainMatchCount = 0;
		// NAMED local, not a range-for over the returned temporary: ChainMatch points into this
		// array and is read AFTER the loop (a direct range-for temporary dies with the loop).
		const TArray<Paper2DPlusComboChain::FScopedAnimationRoot> Roots =
			Paper2DPlusComboChain::GetAnimationRoots(Profile);
		for (const Paper2DPlusComboChain::FScopedAnimationRoot& Root : Roots)
		{
			if (!Root.ChainTags.IsEmpty()
				&& Paper2DPlusAnimationTagQuery::AreExactTagSetsEqual(Root.ChainTags, ExactTags))
			{
				++ChainMatchCount;
				ChainMatch = &Root;
			}
		}
		if (ChainMatchCount > 1)
		{
			return EPaper2DPlusComboChainResult::AmbiguousInput;
		}
		if (ChainMatchCount == 1)
		{
			OutOpenerName = ChainMatch->RootMove;
			return EPaper2DPlusComboChainResult::Success;
		}

		const TArray<const FFlipbookProfileEntry*> Matches =
			Paper2DPlusAnimationTagQuery::FindExactOwnTagMatches(Profile, ExactTags);
		if (Matches.Num() == 0)
		{
			return EPaper2DPlusComboChainResult::NotFound;
		}
		if (Matches.Num() > 1)
		{
			return EPaper2DPlusComboChainResult::AmbiguousInput;
		}
		OutOpenerName = Matches[0]->Identity.FlipbookName;
		return EPaper2DPlusComboChainResult::Success;
	}
}

EPaper2DPlusComboChainResult UPaper2DPlusAnimationMapLibrary::GetComboChainFlipbookAtIndex(
	UPaper2DPlusCharacterProfileAsset* Profile,
	UPaperFlipbook* ChainStartFlipbook,
	int32 Index,
	UPaperFlipbook*& OutFlipbook,
	int32& OutChainLength)
{
	OutFlipbook = nullptr;
	OutChainLength = 0;
	if (!Profile || !ChainStartFlipbook || Index < 0)
	{
		return EPaper2DPlusComboChainResult::InvalidRequest;
	}

	const FFlipbookProfileEntry* Original = nullptr;
	switch (AnimationMap_ResolveEntryByFlipbook(Profile, ChainStartFlipbook, Original))
	{
	case EAnimationMapEntryResult::NotFound:
		return EPaper2DPlusComboChainResult::NotFound;
	case EAnimationMapEntryResult::Ambiguous:
		return EPaper2DPlusComboChainResult::AmbiguousInput;
	default:
		break;
	}

	return AnimationMap_ResolveComboStep(
		Profile, Original->Identity.FlipbookName, Index, OutFlipbook, OutChainLength);
}

EPaper2DPlusComboChainResult UPaper2DPlusAnimationMapLibrary::GetComboChainFlipbookAtIndexByTags(
	UPaper2DPlusCharacterProfileAsset* Profile,
	const FGameplayTagContainer& ExactTags,
	int32 Index,
	UPaperFlipbook*& OutFlipbook,
	int32& OutChainLength)
{
	OutFlipbook = nullptr;
	OutChainLength = 0;
	if (!Profile || ExactTags.IsEmpty() || Index < 0)
	{
		return EPaper2DPlusComboChainResult::InvalidRequest;
	}

	FString OpenerName;
	const EPaper2DPlusComboChainResult TagResult =
		AnimationMap_ResolveOpenerByTags(Profile, ExactTags, OpenerName);
	if (TagResult != EPaper2DPlusComboChainResult::Success)
	{
		return TagResult;
	}

	return AnimationMap_ResolveComboStep(Profile, OpenerName, Index, OutFlipbook, OutChainLength);
}

EPaper2DPlusComboChainResult UPaper2DPlusAnimationMapLibrary::GetComboChainLength(
	UPaper2DPlusCharacterProfileAsset* Profile,
	UPaperFlipbook* ChainStartFlipbook,
	int32& OutChainLength)
{
	OutChainLength = 0;
	if (!Profile || !ChainStartFlipbook)
	{
		return EPaper2DPlusComboChainResult::InvalidRequest;
	}

	const FFlipbookProfileEntry* Original = nullptr;
	switch (AnimationMap_ResolveEntryByFlipbook(Profile, ChainStartFlipbook, Original))
	{
	case EAnimationMapEntryResult::NotFound:
		return EPaper2DPlusComboChainResult::NotFound;
	case EAnimationMapEntryResult::Ambiguous:
		return EPaper2DPlusComboChainResult::AmbiguousInput;
	default:
		break;
	}

	TArray<FString> Spine;
	const EPaper2DPlusComboChainResult LineResult =
		AnimationMap_ResolveComboLine(Profile, Original->Identity.FlipbookName, Spine);
	if (LineResult != EPaper2DPlusComboChainResult::Success)
	{
		return LineResult;
	}
	OutChainLength = Spine.Num();
	return EPaper2DPlusComboChainResult::Success;
}

bool UPaper2DPlusAnimationMapLibrary::HasComboChain(
	UPaper2DPlusCharacterProfileAsset* Profile,
	UPaperFlipbook* ChainStartFlipbook)
{
	int32 ChainLength = 0;
	return GetComboChainLength(Profile, ChainStartFlipbook, ChainLength)
			== EPaper2DPlusComboChainResult::Success
		&& ChainLength >= 2;
}

EPaper2DPlusComboChainResult UPaper2DPlusAnimationMapLibrary::GetComboChainLengthByTags(
	UPaper2DPlusCharacterProfileAsset* Profile,
	const FGameplayTagContainer& ExactTags,
	int32& OutChainLength)
{
	OutChainLength = 0;
	if (!Profile || ExactTags.IsEmpty())
	{
		return EPaper2DPlusComboChainResult::InvalidRequest;
	}

	FString OpenerName;
	const EPaper2DPlusComboChainResult TagResult =
		AnimationMap_ResolveOpenerByTags(Profile, ExactTags, OpenerName);
	if (TagResult != EPaper2DPlusComboChainResult::Success)
	{
		return TagResult;
	}

	TArray<FString> Spine;
	const EPaper2DPlusComboChainResult LineResult =
		AnimationMap_ResolveComboLine(Profile, OpenerName, Spine);
	if (LineResult != EPaper2DPlusComboChainResult::Success)
	{
		return LineResult;
	}
	OutChainLength = Spine.Num();
	return EPaper2DPlusComboChainResult::Success;
}
