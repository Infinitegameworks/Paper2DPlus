// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CharacterCoverage/ExpectedTagAssignment.h"

#include "CharacterProfileEditorModel.h"
#include "Editor.h"
#include "Editor/Transactor.h"
#include "Misc/AutomationTest.h"
#include "Paper2DPlusAnimationTags.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "PaperFlipbook.h"
#include "UObject/Package.h"

namespace
{
	struct FExpectedTagAssignmentFixture
	{
		UPackage* Package = nullptr;
		UPaper2DPlusCharacterProfileAsset* Profile = nullptr;
		TSharedPtr<FCharacterProfileEditorModel> Model;

		explicit FExpectedTagAssignmentFixture(const TCHAR* Suffix)
		{
			const FString PackageName = FString::Printf(
				TEXT("/Engine/Transient/Paper2DPlusExpectedTagAssignment_%s_%s"),
				Suffix,
				*FGuid::NewGuid().ToString(EGuidFormats::Digits));
			Package = CreatePackage(*PackageName);
			Profile = NewObject<UPaper2DPlusCharacterProfileAsset>(
				Package,
				TEXT("Profile"),
				RF_Public | RF_Standalone | RF_Transactional);
		}

		int32 AddAnimation(const TCHAR* AuthoredName)
		{
			const FString ObjectName = FString::Printf(
				TEXT("Flipbook_%d"),
				Profile->Flipbooks.Num());
			UPaperFlipbook* Flipbook = NewObject<UPaperFlipbook>(
				Profile,
				*ObjectName,
				RF_Transactional);

			FFlipbookProfileEntry Entry;
			Entry.Identity.FlipbookName = AuthoredName;
			Entry.Identity.Flipbook = TSoftObjectPtr<UPaperFlipbook>(Flipbook);
			return Profile->Flipbooks.Add(MoveTemp(Entry));
		}

		void InitializeModel()
		{
			Model = MakeShared<FCharacterProfileEditorModel>();
			Model->InitializeFromAsset(Profile);
			Package->SetDirtyFlag(false);
		}
	};

	FString ExpectedTagAssignmentTest_TagContainerSnapshot(
		const FGameplayTagContainer& Tags)
	{
		TArray<FGameplayTag> SortedTags;
		Tags.GetGameplayTagArray(SortedTags);
		SortedTags.Sort(
			[](const FGameplayTag& A, const FGameplayTag& B)
			{
				return A.GetTagName().LexicalLess(B.GetTagName());
			});
		TArray<FString> TagNames;
		TagNames.Reserve(SortedTags.Num());
		for (const FGameplayTag& Tag : SortedTags)
		{
			TagNames.Add(Tag.ToString());
		}
		return FString::Join(TagNames, TEXT(","));
	}

	FString ExpectedTagAssignmentTest_LegacySurfaceSnapshot(
		const UPaper2DPlusCharacterProfileAsset* Profile)
	{
		FString Snapshot = FString::Printf(
			TEXT("AnimSource=%s\n"),
			*Profile->PaperZDAnimSource.ToSoftObjectPath().ToString());

		TArray<FGameplayTag> MappingTags;
		Profile->TagMappings.GenerateKeyArray(MappingTags);
		MappingTags.Sort(
			[](const FGameplayTag& A, const FGameplayTag& B)
			{
				return A.GetTagName().LexicalLess(B.GetTagName());
			});
		for (const FGameplayTag& MappingTag : MappingTags)
		{
			const FFlipbookTagMapping* Mapping = Profile->TagMappings.Find(MappingTag);
			Snapshot += FString::Printf(
				TEXT("Mapping=%s;Entries=%d;LegacyNames=%d;LegacyPaperZD=%d\n"),
				*MappingTag.ToString(),
				Mapping ? Mapping->Entries.Num() : 0,
				Mapping ? Mapping->FlipbookNames_DEPRECATED.Num() : 0,
				Mapping ? Mapping->PaperZDSequences_DEPRECATED.Num() : 0);
			if (!Mapping)
			{
				continue;
			}
			for (int32 EntryIndex = 0; EntryIndex < Mapping->Entries.Num(); ++EntryIndex)
			{
				const FFlipbookTagMappingEntry& Entry = Mapping->Entries[EntryIndex];
				Snapshot += FString::Printf(
					TEXT("Entry=%d;Name=%s;Start=%d;End=%d;Chain=%s;Root=%d;PaperZD=%s\n"),
					EntryIndex,
					*Entry.FlipbookName,
					Entry.bIsChainStart ? 1 : 0,
					Entry.bIsChainEnd ? 1 : 0,
					*ExpectedTagAssignmentTest_TagContainerSnapshot(Entry.ChainTags),
					Entry.RootNumber_DEPRECATED,
					Entry.PaperZDSequence
						? *Entry.PaperZDSequence->GetPathName()
						: TEXT(""));
			}
			for (int32 NameIndex = 0;
				NameIndex < Mapping->FlipbookNames_DEPRECATED.Num();
				++NameIndex)
			{
				Snapshot += FString::Printf(
					TEXT("LegacyName=%d;%s\n"),
					NameIndex,
					*Mapping->FlipbookNames_DEPRECATED[NameIndex]);
			}
			for (int32 SequenceIndex = 0;
				SequenceIndex < Mapping->PaperZDSequences_DEPRECATED.Num();
				++SequenceIndex)
			{
				const UObject* Sequence =
					Mapping->PaperZDSequences_DEPRECATED[SequenceIndex];
				Snapshot += FString::Printf(
					TEXT("LegacyPaperZD=%d;%s\n"),
					SequenceIndex,
					Sequence ? *Sequence->GetPathName() : TEXT(""));
			}
		}

		for (int32 Index = 0; Index < Profile->Flipbooks.Num(); ++Index)
		{
			const FFlipbookProfileEntry& Entry = Profile->Flipbooks[Index];
			Snapshot += FString::Printf(
				TEXT("Flipbook=%d;Group=%s;PaperZD=%s\n"),
				Index,
				*Entry.FlipbookGroup.ToString(),
				Entry.Identity.PaperZDSequence
					? *Entry.Identity.PaperZDSequence->GetPathName()
					: TEXT(""));
		}
		return Snapshot;
	}

	void ExpectedTagAssignmentTest_ResetTransactions(const TCHAR* Reason)
	{
		if (GEditor)
		{
			GEditor->ResetTransaction(FText::FromString(Reason));
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusExpectedTagAssignmentAdditiveUndoRedoTest,
	"Paper2DPlus.CharacterCoverage.Assignment.AdditiveUndoRedoAndLegacyIsolation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusExpectedTagAssignmentAdditiveUndoRedoTest::RunTest(
	const FString& Parameters)
{
	if (!TestNotNull(TEXT("GEditor is available"), GEditor))
	{
		return false;
	}

	const FGameplayTag CombatTag = Paper2DPlusAnimationTags::Combat.GetTag();
	const FGameplayTag HeavyTag = Paper2DPlusAnimationTags::Combat_Heavy.GetTag();
	const FGameplayTag SwimmingTag =
		Paper2DPlusAnimationTags::Context_Swimming.GetTag();
	if (!TestTrue(
		TEXT("Native assignment-test tags are registered"),
		CombatTag.IsValid() && HeavyTag.IsValid() && SwimmingTag.IsValid()))
	{
		return false;
	}

	FExpectedTagAssignmentFixture Fixture(TEXT("Applied"));
	const int32 TargetIndex = Fixture.AddAnimation(TEXT("HeavySwim"));
	Fixture.Profile->Flipbooks[TargetIndex].EditorMeta.AnimationTags.AddTag(SwimmingTag);
	Fixture.Profile->Flipbooks[TargetIndex].FlipbookGroup = TEXT("LegacyGroup");

	UObject* PaperZDSequence = NewObject<UPaperFlipbook>(
		Fixture.Profile,
		TEXT("PaperZDSequence"));
	Fixture.Profile->Flipbooks[TargetIndex].Identity.PaperZDSequence =
		PaperZDSequence;
	FFlipbookTagMappingEntry LegacyEntry(TEXT("HeavySwim"), PaperZDSequence);
	LegacyEntry.bIsChainStart = true;
	LegacyEntry.bIsChainEnd = true;
	LegacyEntry.ChainTags.AddTag(HeavyTag);
	Fixture.Profile->TagMappings.FindOrAdd(CombatTag).Entries.Add(
		MoveTemp(LegacyEntry));

	Fixture.InitializeModel();
	const FExpectedTagAnimationTarget Target =
		FExpectedTagAnimationTarget::Capture(Fixture.Profile, TargetIndex);
	const FString LegacyBefore =
		ExpectedTagAssignmentTest_LegacySurfaceSnapshot(Fixture.Profile);

	int32 AssetDataNotificationCount = 0;
	const FDelegateHandle NotificationHandle =
		Fixture.Model->OnAssetDataChanged.AddLambda(
			[&AssetDataNotificationCount]()
			{
				++AssetDataNotificationCount;
			});

	ExpectedTagAssignmentTest_ResetTransactions(TEXT("Expected tag applied start"));
	const int32 QueueLengthBefore = GEditor->Trans->GetQueueLength();
	const EExpectedTagAssignmentResult Result = FExpectedTagAssignment::Assign(
		HeavyTag,
		Fixture.Profile,
		Target,
		Fixture.Model);

	TestTrue(
		TEXT("A unique valid target is applied"),
		Result == EExpectedTagAssignmentResult::Applied);
	TestTrue(
		TEXT("Assignment is additive and preserves the prior exact tag"),
		Fixture.Profile->Flipbooks[TargetIndex]
			.EditorMeta.AnimationTags.HasTagExact(SwimmingTag));
	TestTrue(
		TEXT("Assignment adds only the requested exact tag"),
		Fixture.Profile->Flipbooks[TargetIndex]
			.EditorMeta.AnimationTags.HasTagExact(HeavyTag));
	TestTrue(TEXT("Applied assignment dirties the package"), Fixture.Package->IsDirty());
	TestEqual(
		TEXT("Applied assignment opens exactly one transaction"),
		GEditor->Trans->GetQueueLength(),
		QueueLengthBefore + 1);
	TestEqual(
		TEXT("Applied assignment emits one explicit model data notification"),
		AssetDataNotificationCount,
		1);
	TestEqual(
		TEXT("TagMappings, FlipbookGroup, chain flags, and PaperZD surfaces remain byte-identical"),
		ExpectedTagAssignmentTest_LegacySurfaceSnapshot(Fixture.Profile),
		LegacyBefore);

	TestTrue(
		TEXT("One undo reverts the assignment gesture"),
		GEditor->UndoTransaction(true));
	TestFalse(
		TEXT("Undo removes only the newly assigned tag"),
		Fixture.Profile->Flipbooks[TargetIndex]
			.EditorMeta.AnimationTags.HasTagExact(HeavyTag));
	TestTrue(
		TEXT("Undo preserves the pre-existing additive tag"),
		Fixture.Profile->Flipbooks[TargetIndex]
			.EditorMeta.AnimationTags.HasTagExact(SwimmingTag));
	TestEqual(
		TEXT("Undo preserves every legacy surface"),
		ExpectedTagAssignmentTest_LegacySurfaceSnapshot(Fixture.Profile),
		LegacyBefore);

	TestTrue(
		TEXT("One redo reapplies the assignment gesture"),
		GEditor->RedoTransaction());
	TestTrue(
		TEXT("Redo restores the assigned exact tag"),
		Fixture.Profile->Flipbooks[TargetIndex]
			.EditorMeta.AnimationTags.HasTagExact(HeavyTag));
	TestEqual(
		TEXT("Redo still preserves every legacy surface"),
		ExpectedTagAssignmentTest_LegacySurfaceSnapshot(Fixture.Profile),
		LegacyBefore);

	Fixture.Model->OnAssetDataChanged.Remove(NotificationHandle);
	ExpectedTagAssignmentTest_ResetTransactions(TEXT("Expected tag applied end"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusExpectedTagAssignmentRejectedNoOpTest,
	"Paper2DPlus.CharacterCoverage.Assignment.NullForeignInvalidAndNoOpStayClean",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusExpectedTagAssignmentRejectedNoOpTest::RunTest(
	const FString& Parameters)
{
	if (!TestNotNull(TEXT("GEditor is available"), GEditor))
	{
		return false;
	}

	const FGameplayTag HeavyTag = Paper2DPlusAnimationTags::Combat_Heavy.GetTag();
	FExpectedTagAssignmentFixture Fixture(TEXT("Rejected"));
	const int32 TargetIndex = Fixture.AddAnimation(TEXT("Heavy"));
	Fixture.Profile->Flipbooks[TargetIndex].EditorMeta.AnimationTags.AddTag(HeavyTag);
	Fixture.InitializeModel();
	const FExpectedTagAnimationTarget Target =
		FExpectedTagAnimationTarget::Capture(Fixture.Profile, TargetIndex);

	FExpectedTagAssignmentFixture ForeignFixture(TEXT("Foreign"));
	ForeignFixture.AddAnimation(TEXT("Foreign"));
	ForeignFixture.InitializeModel();

	int32 AssetDataNotificationCount = 0;
	const FDelegateHandle NotificationHandle =
		Fixture.Model->OnAssetDataChanged.AddLambda(
			[&AssetDataNotificationCount]()
			{
				++AssetDataNotificationCount;
			});

	ExpectedTagAssignmentTest_ResetTransactions(TEXT("Expected tag rejected start"));
	Fixture.Package->SetDirtyFlag(false);
	const int32 QueueLengthBefore = GEditor->Trans->GetQueueLength();
	const int32 UndoCountBefore = GEditor->Trans->GetUndoCount();
	const FString LegacyBefore =
		ExpectedTagAssignmentTest_LegacySurfaceSnapshot(Fixture.Profile);

	TestTrue(
		TEXT("An already-authored exact tag preflights as NoChange"),
		FExpectedTagAssignment::Evaluate(
			HeavyTag,
			Fixture.Profile,
			Target,
			Fixture.Model) == EExpectedTagAssignmentDisposition::NoChange);
	TestTrue(
		TEXT("An already-authored exact tag performs no mutation"),
		FExpectedTagAssignment::Assign(
			HeavyTag,
			Fixture.Profile,
			Target,
			Fixture.Model) == EExpectedTagAssignmentResult::NoChange);
	TestTrue(
		TEXT("A null SourceAsset is rejected"),
		FExpectedTagAssignment::Assign(
			HeavyTag,
			TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset>(),
			Target,
			Fixture.Model) == EExpectedTagAssignmentResult::Rejected);
	TestTrue(
		TEXT("A foreign SourceAsset is rejected"),
		FExpectedTagAssignment::Assign(
			HeavyTag,
			ForeignFixture.Profile,
			Target,
			Fixture.Model) == EExpectedTagAssignmentResult::Rejected);
	TestTrue(
		TEXT("An invalid tag is rejected"),
		FExpectedTagAssignment::Assign(
			FGameplayTag(),
			Fixture.Profile,
			Target,
			Fixture.Model) == EExpectedTagAssignmentResult::Rejected);
	TestTrue(
		TEXT("An unstamped target is rejected"),
		FExpectedTagAssignment::Assign(
			HeavyTag,
			Fixture.Profile,
			FExpectedTagAnimationTarget(),
			Fixture.Model) == EExpectedTagAssignmentResult::Rejected);

	TestFalse(TEXT("Rejected and no-op assignments leave the package clean"), Fixture.Package->IsDirty());
	TestEqual(
		TEXT("Rejected and no-op assignments open no transactions"),
		GEditor->Trans->GetQueueLength(),
		QueueLengthBefore);
	TestEqual(
		TEXT("Rejected and no-op assignments do not move the undo cursor"),
		GEditor->Trans->GetUndoCount(),
		UndoCountBefore);
	TestEqual(
		TEXT("Rejected and no-op assignments emit no model data notification"),
		AssetDataNotificationCount,
		0);
	TestEqual(
		TEXT("Rejected and no-op assignments preserve every legacy surface"),
		ExpectedTagAssignmentTest_LegacySurfaceSnapshot(Fixture.Profile),
		LegacyBefore);

	Fixture.Model->OnAssetDataChanged.Remove(NotificationHandle);
	ExpectedTagAssignmentTest_ResetTransactions(TEXT("Expected tag rejected end"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusExpectedTagAssignmentStableIdentityTest,
	"Paper2DPlus.CharacterCoverage.Assignment.ReorderResolvesStaleAndAmbiguousFailClosed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusExpectedTagAssignmentStableIdentityTest::RunTest(
	const FString& Parameters)
{
	if (!TestNotNull(TEXT("GEditor is available"), GEditor))
	{
		return false;
	}

	const FGameplayTag HeavyTag = Paper2DPlusAnimationTags::Combat_Heavy.GetTag();

	FExpectedTagAssignmentFixture ReorderFixture(TEXT("Reorder"));
	const int32 OriginalTargetIndex = ReorderFixture.AddAnimation(TEXT("Target"));
	ReorderFixture.AddAnimation(TEXT("Other"));
	ReorderFixture.InitializeModel();
	const FExpectedTagAnimationTarget ReorderedTarget =
		FExpectedTagAnimationTarget::Capture(
			ReorderFixture.Profile,
			OriginalTargetIndex);
	ReorderFixture.Profile->Flipbooks.Swap(0, 1);
	ReorderFixture.Package->SetDirtyFlag(false);
	ExpectedTagAssignmentTest_ResetTransactions(TEXT("Expected tag reorder"));

	TestTrue(
		TEXT("A reordered target re-resolves by its stable path and authored name"),
		FExpectedTagAssignment::Assign(
			HeavyTag,
			ReorderFixture.Profile,
			ReorderedTarget,
			ReorderFixture.Model) == EExpectedTagAssignmentResult::Applied);
	TestFalse(
		TEXT("The animation now occupying the captured index is not mutated"),
		ReorderFixture.Profile->Flipbooks[OriginalTargetIndex]
			.EditorMeta.AnimationTags.HasTagExact(HeavyTag));
	TestTrue(
		TEXT("The uniquely re-resolved animation receives the tag"),
		ReorderFixture.Profile->Flipbooks[1]
			.EditorMeta.AnimationTags.HasTagExact(HeavyTag));

	FExpectedTagAssignmentFixture StaleFixture(TEXT("Stale"));
	const int32 StaleIndex = StaleFixture.AddAnimation(TEXT("BeforeRename"));
	StaleFixture.InitializeModel();
	const FExpectedTagAnimationTarget StaleTarget =
		FExpectedTagAnimationTarget::Capture(StaleFixture.Profile, StaleIndex);
	StaleFixture.Profile->Flipbooks[StaleIndex].Identity.FlipbookName =
		TEXT("AfterRename");
	StaleFixture.Package->SetDirtyFlag(false);
	ExpectedTagAssignmentTest_ResetTransactions(TEXT("Expected tag stale"));
	const int32 StaleQueueLengthBefore = GEditor->Trans->GetQueueLength();
	TestTrue(
		TEXT("A stale authored-name stamp is rejected"),
		FExpectedTagAssignment::Assign(
			HeavyTag,
			StaleFixture.Profile,
			StaleTarget,
			StaleFixture.Model) == EExpectedTagAssignmentResult::Rejected);
	TestFalse(TEXT("A stale target leaves the package clean"), StaleFixture.Package->IsDirty());
	TestEqual(
		TEXT("A stale target fails before opening a transaction"),
		GEditor->Trans->GetQueueLength(),
		StaleQueueLengthBefore);

	FExpectedTagAssignmentFixture AmbiguousFixture(TEXT("Ambiguous"));
	const int32 AmbiguousIndex = AmbiguousFixture.AddAnimation(TEXT("Duplicate"));
	AmbiguousFixture.InitializeModel();
	const FExpectedTagAnimationTarget AmbiguousTarget =
		FExpectedTagAnimationTarget::Capture(
			AmbiguousFixture.Profile,
			AmbiguousIndex);
	const FFlipbookProfileEntry AmbiguousDuplicate =
		AmbiguousFixture.Profile->Flipbooks[AmbiguousIndex];
	AmbiguousFixture.Profile->Flipbooks.Add(AmbiguousDuplicate);
	AmbiguousFixture.Package->SetDirtyFlag(false);
	ExpectedTagAssignmentTest_ResetTransactions(TEXT("Expected tag ambiguous"));
	const int32 AmbiguousQueueLengthBefore = GEditor->Trans->GetQueueLength();
	TestTrue(
		TEXT("A duplicated exact identity is rejected as ambiguous"),
		FExpectedTagAssignment::Assign(
			HeavyTag,
			AmbiguousFixture.Profile,
			AmbiguousTarget,
			AmbiguousFixture.Model) == EExpectedTagAssignmentResult::Rejected);
	TestFalse(TEXT("An ambiguous target leaves the package clean"), AmbiguousFixture.Package->IsDirty());
	TestEqual(
		TEXT("An ambiguous target fails before opening a transaction"),
		GEditor->Trans->GetQueueLength(),
		AmbiguousQueueLengthBefore);
	TestFalse(
		TEXT("Ambiguous identity mutates neither candidate"),
		AmbiguousFixture.Profile->Flipbooks[0]
			.EditorMeta.AnimationTags.HasTagExact(HeavyTag)
			|| AmbiguousFixture.Profile->Flipbooks[1]
				.EditorMeta.AnimationTags.HasTagExact(HeavyTag));

	ExpectedTagAssignmentTest_ResetTransactions(TEXT("Expected tag stable identity end"));
	return true;
}

#endif // WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
