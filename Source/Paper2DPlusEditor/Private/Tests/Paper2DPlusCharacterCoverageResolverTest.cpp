// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CharacterCoverage/CharacterCoverageResolver.h"

#include "Editor.h"
#include "Editor/Transactor.h"
#include "GameplayTagContainer.h"
#include "Misc/AutomationTest.h"
#include "Paper2DPlusAnimationTagQuery.h"
#include "Paper2DPlusAnimationTags.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Paper2DPlusMoveTransition.h"
#include "PaperFlipbook.h"
#include "UObject/Package.h"

namespace
{
	using ECoverageStatus = ECharacterCoverageStatus;
	using FCoverageResult = FCharacterCoverageResolveResult;
	using FCoverageRow = FCharacterCoverageRow;

	int32 CoverageResolverTest_AddMove(
		UPaper2DPlusCharacterProfileAsset* Profile,
		const TCHAR* AnimationName)
	{
		FFlipbookProfileEntry Entry;
		Entry.Identity.FlipbookName = AnimationName;
		Entry.Identity.Flipbook = NewObject<UPaperFlipbook>(Profile);
		return Profile->Flipbooks.Add(Entry);
	}

	void CoverageResolverTest_AddGroupMember(
		UPaper2DPlusCharacterProfileAsset* Profile,
		const FGameplayTag& GroupTag,
		const TCHAR* AnimationName,
		bool bIsChainStart = false)
	{
		FFlipbookTagMappingEntry Entry(AnimationName);
		Entry.bIsChainStart = bIsChainStart;
		Profile->TagMappings.FindOrAdd(GroupTag).Entries.Add(Entry);
	}

	void CoverageResolverTest_AddTransition(
		UPaper2DPlusCharacterProfileAsset* Profile,
		int32 FromIndex,
		const TCHAR* ToAnimationName)
	{
		Profile->Flipbooks[FromIndex].TransitionData.Transitions.Add(
			FPaper2DPlusMoveTransition(ToAnimationName));
	}

	const FCoverageRow* CoverageResolverTest_FindRow(
		const FCoverageResult& Result,
		const FGameplayTag& ExpectedTag)
	{
		return Result.Rows.FindByPredicate(
			[ExpectedTag](const FCoverageRow& Row)
			{
				return Row.ExpectedTag == ExpectedTag;
			});
	}

	bool CoverageResolverTest_AreResultsEqual(
		const FCoverageResult& A,
		const FCoverageResult& B)
	{
		if (A.Rows.Num() != B.Rows.Num())
		{
			return false;
		}

		for (int32 Index = 0; Index < A.Rows.Num(); ++Index)
		{
			const FCoverageRow& Left = A.Rows[Index];
			const FCoverageRow& Right = B.Rows[Index];
			if (Left.ExpectedTag != Right.ExpectedTag
				|| Left.Status != Right.Status
				|| Left.AuthoredAnimationNames != Right.AuthoredAnimationNames
				|| Left.GroupImpliedAnimationNames != Right.GroupImpliedAnimationNames
				|| Left.ChainInheritedAnimationNames != Right.ChainInheritedAnimationNames
				|| Left.bSupersetOnly != Right.bSupersetOnly)
			{
				return false;
			}
		}
		return A.VisitedAnimationNamesForTests == B.VisitedAnimationNamesForTests;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCharacterCoverageResolverProvenanceCannotDriftTest,
	"Paper2DPlus.CharacterCoverage.Resolver.ProvenanceCannotDrift",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCharacterCoverageResolverProvenanceCannotDriftTest::RunTest(
	const FString& Parameters)
{
	using namespace Paper2DPlusAnimationTagQuery;

	const FGameplayTag CombatTag = Paper2DPlusAnimationTags::Combat.GetTag();
	const FGameplayTag HeavyTag = Paper2DPlusAnimationTags::Combat_Heavy.GetTag();
	const FGameplayTag SwimmingTag = Paper2DPlusAnimationTags::Context_Swimming.GetTag();
	if (!TestTrue(
		TEXT("Native animation tags are registered"),
		CombatTag.IsValid() && HeavyTag.IsValid() && SwimmingTag.IsValid()))
	{
		return false;
	}

	TMap<FString, FAnimationTagSet> SyntheticTagMap;
	FAnimationTagSet& Authored = SyntheticTagMap.Add(TEXT("authored"));
	Authored.AnimationName = TEXT("AuthoredHeavy");
	Authored.OwnTags.AddTag(HeavyTag);
	Authored.GroupImpliedTags.AddTag(HeavyTag);
	Authored.ChainInheritedTags.AddTag(HeavyTag);
	FAnimationTagSet& GroupOnly = SyntheticTagMap.Add(TEXT("group"));
	GroupOnly.AnimationName = TEXT("GroupOnly");
	GroupOnly.GroupImpliedTags.AddTag(CombatTag);
	FAnimationTagSet& ChainOnly = SyntheticTagMap.Add(TEXT("chain"));
	ChainOnly.AnimationName = TEXT("ChainOnly");
	ChainOnly.ChainInheritedTags.AddTag(SwimmingTag);

	FGameplayTagContainer ExpectedTags;
	ExpectedTags.AddTag(SwimmingTag);
	ExpectedTags.AddTag(HeavyTag);
	ExpectedTags.AddTag(CombatTag);
	const FCoverageResult Result =
		FCharacterCoverageResolver::ResolveFromTagMapForTests(SyntheticTagMap, ExpectedTags);

	const FCoverageRow* AuthoredRow = CoverageResolverTest_FindRow(Result, HeavyTag);
	const FCoverageRow* GroupRow = CoverageResolverTest_FindRow(Result, CombatTag);
	const FCoverageRow* ChainRow = CoverageResolverTest_FindRow(Result, SwimmingTag);
	if (!TestNotNull(TEXT("Authored row exists"), AuthoredRow)
		|| !TestNotNull(TEXT("Group-implied row exists"), GroupRow)
		|| !TestNotNull(TEXT("Chain-inherited row exists"), ChainRow))
	{
		return false;
	}

	TestTrue(TEXT("Own exact tags are covered"), AuthoredRow->Status == ECoverageStatus::Covered);
	TestTrue(TEXT("The canonical authored name is preserved"),
		AuthoredRow->AuthoredAnimationNames == TArray<FString>({ TEXT("AuthoredHeavy") }));
	TestEqual(TEXT("An authored match is not repeated as a group near miss"),
		AuthoredRow->GroupImpliedAnimationNames.Num(), 0);
	TestEqual(TEXT("An authored match is not repeated as a chain near miss"),
		AuthoredRow->ChainInheritedAnimationNames.Num(), 0);

	TestTrue(TEXT("A group-implied-only exact hit is a near miss"),
		GroupRow->Status == ECoverageStatus::NearMiss);
	TestTrue(TEXT("Group near misses retain their own provenance"),
		GroupRow->GroupImpliedAnimationNames == TArray<FString>({ TEXT("GroupOnly") }));
	TestEqual(TEXT("Group provenance never leaks into authored matches"),
		GroupRow->AuthoredAnimationNames.Num(), 0);

	TestTrue(TEXT("A chain-inherited-only exact hit is a near miss"),
		ChainRow->Status == ECoverageStatus::NearMiss);
	TestTrue(TEXT("Chain near misses retain their own provenance"),
		ChainRow->ChainInheritedAnimationNames == TArray<FString>({ TEXT("ChainOnly") }));
	TestEqual(TEXT("Chain provenance never leaks into authored matches"),
		ChainRow->AuthoredAnimationNames.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCharacterCoverageResolverBulkSemanticsTest,
	"Paper2DPlus.CharacterCoverage.Resolver.BulkSemanticsAndDeterminism",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCharacterCoverageResolverBulkSemanticsTest::RunTest(
	const FString& Parameters)
{
	const FGameplayTag CombatTag = Paper2DPlusAnimationTags::Combat.GetTag();
	const FGameplayTag BlockTag = Paper2DPlusAnimationTags::Combat_Block.GetTag();
	const FGameplayTag GrabTag = Paper2DPlusAnimationTags::Combat_Grab.GetTag();
	const FGameplayTag HeavyTag = Paper2DPlusAnimationTags::Combat_Heavy.GetTag();
	const FGameplayTag ContextTag = Paper2DPlusAnimationTags::Context.GetTag();
	const FGameplayTag SwimmingTag = Paper2DPlusAnimationTags::Context_Swimming.GetTag();

	UPaper2DPlusCharacterProfileAsset* Profile =
		NewObject<UPaper2DPlusCharacterProfileAsset>();
	const int32 MultiIndex = CoverageResolverTest_AddMove(Profile, TEXT("ZetaMulti"));
	Profile->Flipbooks[MultiIndex].EditorMeta.AnimationTags.AddTag(SwimmingTag);
	Profile->Flipbooks[MultiIndex].EditorMeta.AnimationTags.AddTag(HeavyTag);
	const int32 ExactIndex = CoverageResolverTest_AddMove(Profile, TEXT("AlphaExact"));
	Profile->Flipbooks[ExactIndex].EditorMeta.AnimationTags.AddTag(HeavyTag);
	CoverageResolverTest_AddMove(Profile, TEXT("MiddleGroup"));
	const int32 RootIndex = CoverageResolverTest_AddMove(Profile, TEXT("ChainRoot"));
	Profile->Flipbooks[RootIndex].EditorMeta.AnimationTags.AddTag(GrabTag);
	CoverageResolverTest_AddMove(Profile, TEXT("ChainChild"));

	CoverageResolverTest_AddGroupMember(Profile, CombatTag, TEXT("MiddleGroup"));
	CoverageResolverTest_AddGroupMember(
		Profile, CombatTag, TEXT("ChainRoot"), /*bIsChainStart=*/true);
	CoverageResolverTest_AddGroupMember(Profile, CombatTag, TEXT("ChainChild"));
	CoverageResolverTest_AddTransition(Profile, RootIndex, TEXT("ChainChild"));

	FGameplayTagContainer ExpectedTags;
	ExpectedTags.AddTag(ContextTag);
	ExpectedTags.AddTag(SwimmingTag);
	ExpectedTags.AddTag(BlockTag);
	ExpectedTags.AddTag(CombatTag);
	ExpectedTags.AddTag(HeavyTag);
	ExpectedTags.AddTag(GrabTag);

	const FCoverageResult First = FCharacterCoverageResolver::Resolve(Profile, ExpectedTags);
	const FCoverageResult Second = FCharacterCoverageResolver::Resolve(Profile, ExpectedTags);
	TestTrue(TEXT("Repeated resolves are byte-for-byte deterministic at the row seam"),
		CoverageResolverTest_AreResultsEqual(First, Second));
	TestEqual(TEXT("One row is emitted per expected tag"), First.Rows.Num(), ExpectedTags.Num());

	for (int32 Index = 1; Index < First.Rows.Num(); ++Index)
	{
		TestTrue(
			TEXT("Expected-tag rows are sorted lexically"),
			First.Rows[Index - 1].ExpectedTag.GetTagName().LexicalLess(
				First.Rows[Index].ExpectedTag.GetTagName()));
	}

	const FCoverageRow* CombatRow = CoverageResolverTest_FindRow(First, CombatTag);
	const FCoverageRow* BlockRow = CoverageResolverTest_FindRow(First, BlockTag);
	const FCoverageRow* GrabRow = CoverageResolverTest_FindRow(First, GrabTag);
	const FCoverageRow* HeavyRow = CoverageResolverTest_FindRow(First, HeavyTag);
	const FCoverageRow* ContextRow = CoverageResolverTest_FindRow(First, ContextTag);
	const FCoverageRow* SwimmingRow = CoverageResolverTest_FindRow(First, SwimmingTag);
	if (!CombatRow || !BlockRow || !GrabRow || !HeavyRow || !ContextRow || !SwimmingRow)
	{
		AddError(TEXT("Every expected tag must resolve to a row"));
		return false;
	}

	TestTrue(TEXT("Group-only exact carriers produce NearMiss"), CombatRow->Status == ECoverageStatus::NearMiss);
	TestFalse(TEXT("A near miss is never superset-only"), CombatRow->bSupersetOnly);
	TestTrue(TEXT("Group carriers use canonical names in deterministic order"),
		CombatRow->GroupImpliedAnimationNames
			== TArray<FString>({ TEXT("ChainChild"), TEXT("ChainRoot"), TEXT("MiddleGroup") }));
	TestTrue(TEXT("No exact carrier is Missing"), BlockRow->Status == ECoverageStatus::Missing);
	TestFalse(TEXT("A missing row is never superset-only"), BlockRow->bSupersetOnly);

	TestTrue(TEXT("A real root authors coverage"), GrabRow->Status == ECoverageStatus::Covered);
	TestTrue(TEXT("Only the root is an authored Grab match"),
		GrabRow->AuthoredAnimationNames == TArray<FString>({ TEXT("ChainRoot") }));
	TestTrue(TEXT("The reached member remains separately chain-inherited"),
		GrabRow->ChainInheritedAnimationNames == TArray<FString>({ TEXT("ChainChild") }));

	TestTrue(TEXT("Heavy is covered"), HeavyRow->Status == ECoverageStatus::Covered);
	TestFalse(TEXT("Any singleton exact own-container match clears superset-only"),
		HeavyRow->bSupersetOnly);
	TestTrue(TEXT("A descendant tag does not cover its expected parent"),
		ContextRow->Status == ECoverageStatus::Missing);
	TestTrue(TEXT("Swimming is covered by an exact own-tag hit"),
		SwimmingRow->Status == ECoverageStatus::Covered);
	TestTrue(TEXT("Coverage with only a multi-tag own container is superset-only"),
		SwimmingRow->bSupersetOnly);

	const TArray<FString> ExpectedVisits = {
		TEXT("AlphaExact"),
		TEXT("ChainChild"),
		TEXT("ChainRoot"),
		TEXT("MiddleGroup"),
		TEXT("ZetaMulti")
	};
	TestTrue(TEXT("Every built tag-map row is visited exactly once"),
		First.VisitedAnimationNamesForTests == ExpectedVisits);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCharacterCoverageResolverReadOnlyEdgesTest,
	"Paper2DPlus.CharacterCoverage.Resolver.NullEmptyAndReadOnly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCharacterCoverageResolverReadOnlyEdgesTest::RunTest(
	const FString& Parameters)
{
	if (!TestNotNull(TEXT("GEditor is available for the transaction assertion"), GEditor))
	{
		return false;
	}

	const FGameplayTag HeavyTag = Paper2DPlusAnimationTags::Combat_Heavy.GetTag();
	FGameplayTagContainer ExpectedHeavy;
	ExpectedHeavy.AddTag(HeavyTag);

	const FCoverageResult NullResult =
		FCharacterCoverageResolver::Resolve(nullptr, ExpectedHeavy);
	TestEqual(TEXT("A null profile still emits deterministic missing rows"), NullResult.Rows.Num(), 1);
	if (NullResult.Rows.Num() == 1)
	{
		TestTrue(TEXT("Null-profile expected tags are Missing"),
			NullResult.Rows[0].Status == ECoverageStatus::Missing);
	}
	TestEqual(TEXT("A null profile visits no animation rows"),
		NullResult.VisitedAnimationNamesForTests.Num(), 0);

	const FString PackageName = FString::Printf(
		TEXT("/Engine/Transient/Paper2DPlusCoverageResolver_%s"),
		*FGuid::NewGuid().ToString(EGuidFormats::Digits));
	UPackage* Package = CreatePackage(*PackageName);
	UPaper2DPlusCharacterProfileAsset* Profile =
		NewObject<UPaper2DPlusCharacterProfileAsset>(
			Package, TEXT("Profile"), RF_Public | RF_Standalone | RF_Transactional);
	const int32 MoveIndex = CoverageResolverTest_AddMove(Profile, TEXT("Heavy"));
	Profile->Flipbooks[MoveIndex].EditorMeta.AnimationTags.AddTag(HeavyTag);
	Package->SetDirtyFlag(false);

	GEditor->ResetTransaction(FText::FromString(TEXT("Coverage resolver read-only start")));
	const int32 QueueLengthBefore = GEditor->Trans->GetQueueLength();
	const int32 UndoCountBefore = GEditor->Trans->GetUndoCount();
	const EObjectFlags FlagsBefore = Profile->GetFlags();

	const FCoverageResult Resolved =
		FCharacterCoverageResolver::Resolve(Profile, ExpectedHeavy);
	const FCoverageResult EmptyExpected =
		FCharacterCoverageResolver::Resolve(Profile, FGameplayTagContainer());

	TestEqual(TEXT("The populated resolve returns one row"), Resolved.Rows.Num(), 1);
	TestEqual(TEXT("An empty expected set returns no rows"), EmptyExpected.Rows.Num(), 0);
	TestTrue(TEXT("Even an empty expected set visits each built map row once"),
		EmptyExpected.VisitedAnimationNamesForTests == TArray<FString>({ TEXT("Heavy") }));
	TestFalse(TEXT("Resolve never dirties the profile package"), Package->IsDirty());
	TestTrue(TEXT("Resolve preserves the profile object flags"), Profile->GetFlags() == FlagsBefore);
	TestEqual(TEXT("Resolve preserves the profile animation count"), Profile->Flipbooks.Num(), 1);
	TestEqual(TEXT("Resolve preserves the canonical animation name"),
		Profile->Flipbooks[0].Identity.FlipbookName, FString(TEXT("Heavy")));
	TestTrue(TEXT("Resolve preserves authored tags"),
		Profile->Flipbooks[0].EditorMeta.AnimationTags.HasTagExact(HeavyTag));
	TestEqual(TEXT("Resolve opens no transaction"), GEditor->Trans->GetQueueLength(), QueueLengthBefore);
	TestEqual(TEXT("Resolve changes no undo cursor"), GEditor->Trans->GetUndoCount(), UndoCountBefore);
	GEditor->ResetTransaction(FText::FromString(TEXT("Coverage resolver read-only end")));
	return true;
}

#endif // WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
