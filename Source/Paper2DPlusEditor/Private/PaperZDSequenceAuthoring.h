// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class SWidget;
class UClass;
class UObject;
class UPaper2DPlusCharacterProfileAsset;
class UPaperFlipbook;

namespace Paper2DPlus::PaperZDSequenceAuthoring
{
	/** One missing flipbook sequence presented by the shared Character Data/Bulk creation flow. */
	struct FPendingSequence
	{
		FString FlipbookName;
		FString SequenceName;
		TWeakObjectPtr<UPaperFlipbook> Flipbook;
	};

	/** Pure availability predicate used by the runtime plugin/class probe and focused tests. */
	bool EvaluateOptionalAvailability(
		bool bPluginInstalledAndEnabled,
		UClass* AnimationSourceClass,
		UClass* FlipbookSequenceClass);

	/** Checks the plugin descriptor and reflected classes without linking PaperZD/PaperZDEditor. */
	bool IsOptionalAuthoringAvailable();

	/** Fully-qualified reflected class resolvers; null when the expected PaperZD classes are absent. */
	UClass* ResolveAnimationSourceClass();
	UClass* ResolveFlipbookSequenceClass();

	FString BuildDefaultSequenceName(
		const FString& ProfileName,
		const FString& FlipbookName,
		bool bIncludeProfilePrefix);

	/**
	 * THE shared "is there anything for the sequence creator to do" answer.
	 *
	 * Both the pre-check surfaces (the Character Data / Bulk "Create" button) and the creator's own
	 * terminal message read this, so they cannot diverge again: an EMPTY candidate set is not the
	 * same statement as "every flipbook already has a sequence". A Character Profile that has not
	 * been extracted into yet has no flipbooks at all, so the creator has nothing in scope — saying
	 * "all selected flipbooks already have PaperZD sequences" there is simply false.
	 */
	struct FSequenceWorkSummary
	{
		/** In-scope flipbooks the creator can even consider (0 == nothing to check yet). */
		int32 CandidateFlipbooks = 0;
		/** Of those, the ones with at least one unset sequence reference. */
		int32 FlipbooksMissingSequences = 0;
		/** Of the missing ones, those an already-existing sequence can simply be linked to. */
		int32 FlipbooksResolvableFromExisting = 0;

		bool HasCandidates() const { return CandidateFlipbooks > 0; }
	};

	/** Run the shared gather purely to answer FSequenceWorkSummary. Creates and links nothing.
	 *  FlipbookScope null == the whole Character Profile. */
	FSequenceWorkSummary SummarizeSequenceWork(
		UPaper2DPlusCharacterProfileAsset& Profile,
		bool bDiscoverExistingSequences,
		const TArray<UPaperFlipbook*>* FlipbookScope);

	/** Gather missing work only for the supplied flipbooks, preserving Character Profile order. */
	void GatherSequenceWork(
		UPaper2DPlusCharacterProfileAsset& Profile,
		bool bDiscoverExistingSequences,
		const TArray<UPaperFlipbook*>& FlipbookScope,
		TMap<UPaperFlipbook*, UObject*>& OutResolvedSequences,
		TArray<TSharedPtr<FPendingSequence>>& OutPendingSequences);

	/** Gather missing work for every flipbook referenced by the Character Profile. */
	void GatherAllSequenceWork(
		UPaper2DPlusCharacterProfileAsset& Profile,
		bool bDiscoverExistingSequences,
		TMap<UPaperFlipbook*, UObject*>& OutResolvedSequences,
		TArray<TSharedPtr<FPendingSequence>>& OutPendingSequences);

	/** Fill only missing Profile identity/tag-entry references; never overwrite authored values. */
	bool AssignSequenceToMissingReferences(
		UPaper2DPlusCharacterProfileAsset& Profile,
		UPaperFlipbook* Flipbook,
		UObject* Sequence);

	/** Shared reflected creator used by Character Data. Returns true when Profile links changed. */
	bool CreateAndLinkMissingSequences(
		UPaper2DPlusCharacterProfileAsset& Profile,
		const TSharedRef<SWidget>& ParentWidget);

	/** Bulk variant restricted to the successfully committed flipbooks from one extraction run. */
	bool CreateAndLinkMissingSequencesForFlipbooks(
		UPaper2DPlusCharacterProfileAsset& Profile,
		const TSharedRef<SWidget>& ParentWidget,
		const TArray<UPaperFlipbook*>& FlipbookScope);
}
