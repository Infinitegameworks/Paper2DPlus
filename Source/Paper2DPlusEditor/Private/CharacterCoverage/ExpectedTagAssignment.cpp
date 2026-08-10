// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "CharacterCoverage/ExpectedTagAssignment.h"

#include "CharacterProfileEditorModel.h"
#include "Editor.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "ScopedTransaction.h"

namespace
{
	struct FExpectedTagAssignmentResolution
	{
		EExpectedTagAssignmentDisposition Disposition =
			EExpectedTagAssignmentDisposition::Rejected;
		UPaper2DPlusCharacterProfileAsset* Asset = nullptr;
		int32 FlipbookIndex = INDEX_NONE;
	};

	bool ExpectedTagAssignment_EntryMatches(
		const FFlipbookProfileEntry& Entry,
		const FExpectedTagAnimationTarget& Target)
	{
		return Entry.Identity.FlipbookName == Target.AuthoredName
			&& Entry.Identity.Flipbook.ToSoftObjectPath() == Target.FlipbookPath;
	}

	FExpectedTagAssignmentResolution ExpectedTagAssignment_Resolve(
		const FGameplayTag& ExpectedTag,
		TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset> SourceAsset,
		const FExpectedTagAnimationTarget& Target,
		const TSharedPtr<FCharacterProfileEditorModel>& Model)
	{
		FExpectedTagAssignmentResolution Resolution;
		UPaper2DPlusCharacterProfileAsset* Asset = Target.OwningAsset.Get();
		if (!ExpectedTag.IsValid()
			|| !SourceAsset.IsValid()
			|| !Asset
			|| SourceAsset.Get() != Asset
			|| !Model.IsValid()
			|| Model->GetAsset() != Asset
			|| !Target.IsStamped())
		{
			return Resolution;
		}

		int32 MatchCount = 0;
		for (int32 Index = 0; Index < Asset->Flipbooks.Num(); ++Index)
		{
			if (!ExpectedTagAssignment_EntryMatches(Asset->Flipbooks[Index], Target))
			{
				continue;
			}

			Resolution.FlipbookIndex = Index;
			++MatchCount;
			if (MatchCount > 1)
			{
				return FExpectedTagAssignmentResolution();
			}
		}

		if (MatchCount != 1)
		{
			return FExpectedTagAssignmentResolution();
		}

		Resolution.Asset = Asset;
		Resolution.Disposition =
			Asset->Flipbooks[Resolution.FlipbookIndex]
					.EditorMeta.AnimationTags.HasTagExact(ExpectedTag)
				? EExpectedTagAssignmentDisposition::NoChange
				: EExpectedTagAssignmentDisposition::Assignable;
		return Resolution;
	}
}

FExpectedTagAnimationTarget FExpectedTagAnimationTarget::Capture(
	UPaper2DPlusCharacterProfileAsset* Asset,
	int32 FlipbookIndex)
{
	FExpectedTagAnimationTarget Target;
	if (!Asset || !Asset->Flipbooks.IsValidIndex(FlipbookIndex))
	{
		return Target;
	}

	const FFlipbookProfileEntry& Entry = Asset->Flipbooks[FlipbookIndex];
	Target.OwningAsset = Asset;
	Target.CapturedIndex = FlipbookIndex;
	Target.FlipbookPath = Entry.Identity.Flipbook.ToSoftObjectPath();
	Target.AuthoredName = Entry.Identity.FlipbookName;
	return Target;
}

bool FExpectedTagAnimationTarget::IsStamped() const
{
	return OwningAsset.IsValid()
		&& CapturedIndex >= 0
		&& !FlipbookPath.IsNull()
		&& !AuthoredName.TrimStartAndEnd().IsEmpty();
}

EExpectedTagAssignmentDisposition FExpectedTagAssignment::Evaluate(
	const FGameplayTag& ExpectedTag,
	TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset> SourceAsset,
	const FExpectedTagAnimationTarget& Target,
	const TSharedPtr<FCharacterProfileEditorModel>& Model)
{
	return ExpectedTagAssignment_Resolve(ExpectedTag, SourceAsset, Target, Model).Disposition;
}

EExpectedTagAssignmentResult FExpectedTagAssignment::Assign(
	const FGameplayTag& ExpectedTag,
	TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset> SourceAsset,
	const FExpectedTagAnimationTarget& Target,
	const TSharedPtr<FCharacterProfileEditorModel>& Model)
{
	const FExpectedTagAssignmentResolution Resolution =
		ExpectedTagAssignment_Resolve(ExpectedTag, SourceAsset, Target, Model);
	if (Resolution.Disposition == EExpectedTagAssignmentDisposition::Rejected)
	{
		return EExpectedTagAssignmentResult::Rejected;
	}
	if (Resolution.Disposition == EExpectedTagAssignmentDisposition::NoChange)
	{
		return EExpectedTagAssignmentResult::NoChange;
	}
	if (!GEditor
		|| !Resolution.Asset
		|| !Resolution.Asset->Flipbooks.IsValidIndex(Resolution.FlipbookIndex))
	{
		return EExpectedTagAssignmentResult::Rejected;
	}

	{
		const FScopedTransaction Transaction(NSLOCTEXT(
			"ExpectedTagAssignment",
			"AssignExpectedAnimationTag",
			"Assign Expected Animation Tag"));
		Resolution.Asset->Modify();
		Resolution.Asset->Flipbooks[Resolution.FlipbookIndex]
			.EditorMeta.AnimationTags.AddTag(ExpectedTag);
	}
	Resolution.Asset->MarkPackageDirty();
	// Browser subscribers rebuild synchronously, so notify only after the drop transaction closes.
	Model->NotifyAssetDataChanged();
	return EExpectedTagAssignmentResult::Applied;
}
