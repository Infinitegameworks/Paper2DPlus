// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Paper2DPlusEffectLibraryIndex.h"
#include "Paper2DPlusEffectProfileAsset.h"
#include "Paper2DPlusEffectTags.h"
#include "PaperFlipbook.h"
#include "EffectProfileEditorModel.h"
#include "SPaper2DPlusEffectPicker.h"
#include "InputCoreTypes.h"
#include "Widgets/Input/SSearchBox.h"

namespace Paper2DPlusEffectLibraryQueryTestPrivate
{
	FPaper2DPlusEffectLibraryRow MakeRow(
		const TCHAR* FlipbookPath,
		const TCHAR* ProfilePath,
		int32 RowIndex,
		FGameplayTag Type,
		std::initializer_list<FGameplayTag> Descriptors = {})
	{
		FPaper2DPlusEffectLibraryRow Row;
		Row.FlipbookPath = FSoftObjectPath(FlipbookPath);
		Row.NormalizedFlipbookPath = FPaper2DPlusEffectLibraryIndex::NormalizePath(Row.FlipbookPath);
		Row.DisplayName = FText::FromString(Row.FlipbookPath.GetAssetName());
		Row.SourceProfilePath = FSoftObjectPath(ProfilePath);
		Row.SourceRowIndex = RowIndex;
		Row.TypeTag = Type;
		for (const FGameplayTag Descriptor : Descriptors)
		{
			Row.DescriptorTags.AddTag(Descriptor);
		}
		return Row;
	}

	FPaper2DPlusEffectProfileSnapshot MakeSnapshot(
		const TCHAR* ProfilePath,
		std::initializer_list<FPaper2DPlusEffectLibraryRow> Rows)
	{
		FPaper2DPlusEffectProfileSnapshot Snapshot;
		Snapshot.ProfilePath = FSoftObjectPath(ProfilePath);
		Snapshot.NormalizedProfilePath = FPaper2DPlusEffectLibraryIndex::NormalizePath(Snapshot.ProfilePath);
		Snapshot.Rows.Append(Rows.begin(), static_cast<int32>(Rows.size()));
		return Snapshot;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusEffectLibraryDeterministicQueryTest,
	"Paper2DPlus.FrameCue.EffectLibrary.DeterministicFiltersDedupingAndPinnedCurrent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusEffectLibraryDeterministicQueryTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusEffectLibraryQueryTestPrivate;
	const FSoftObjectPath Shared(TEXT("/Game/FX/Shared.Shared"));
	const FSoftObjectPath FireOnly(TEXT("/Game/FX/FireOnly.FireOnly"));
	const FSoftObjectPath Unlisted(TEXT("/Game/FX/Unlisted.Unlisted"));

	const FPaper2DPlusEffectProfileSnapshot ProfileB = MakeSnapshot(
		TEXT("/Game/FX/Z_Profile.Z_Profile"),
		{ MakeRow(*Shared.ToString(), TEXT("/Game/FX/Z_Profile.Z_Profile"), 0,
			Paper2DPlusEffectTags::Type_Impact.GetTag(),
			{ Paper2DPlusEffectTags::Descriptor_Fire.GetTag(), Paper2DPlusEffectTags::Descriptor_Electric.GetTag() }) });
	const FPaper2DPlusEffectProfileSnapshot ProfileA = MakeSnapshot(
		TEXT("/Game/FX/A_Profile.A_Profile"),
		{
			// Filters run before duplicate removal: this ineligible row cannot hide the next eligible row.
			MakeRow(*Shared.ToString(), TEXT("/Game/FX/A_Profile.A_Profile"), 0,
				Paper2DPlusEffectTags::Type_Trail.GetTag(),
				{ Paper2DPlusEffectTags::Descriptor_Fire.GetTag() }),
			MakeRow(*Shared.ToString(), TEXT("/Game/FX/A_Profile.A_Profile"), 1,
				Paper2DPlusEffectTags::Type_Impact.GetTag(),
				{ Paper2DPlusEffectTags::Descriptor_Fire.GetTag(), Paper2DPlusEffectTags::Descriptor_Electric.GetTag() }),
			MakeRow(*FireOnly.ToString(), TEXT("/Game/FX/A_Profile.A_Profile"), 2,
				Paper2DPlusEffectTags::Type_Impact.GetTag(),
				{ Paper2DPlusEffectTags::Descriptor_Fire.GetTag() })
		});

	FPaper2DPlusEffectLibraryQuery Query;
	Query.TypeFilter = Paper2DPlusEffectTags::Type.GetTag(); // Hierarchical parent accepts Impact.
	Query.DescriptorFilters.AddTag(Paper2DPlusEffectTags::Descriptor_Fire.GetTag());
	Query.DescriptorFilters.AddTag(Paper2DPlusEffectTags::Descriptor_Electric.GetTag());
	Query.CurrentValue = Unlisted;

	FPaper2DPlusEffectLibraryLoadRecord B;
	B.ProfilePath = ProfileB.ProfilePath;
	B.Status = EPaper2DPlusEffectLibraryLoadStatus::Loaded;
	B.Snapshot = ProfileB;
	FPaper2DPlusEffectLibraryLoadRecord A;
	A.ProfilePath = ProfileA.ProfilePath;
	A.Status = EPaper2DPlusEffectLibraryLoadStatus::Loaded;
	A.Snapshot = ProfileA;

	const FPaper2DPlusEffectLibraryQueryResult Result =
		FPaper2DPlusEffectLibraryIndex::BuildProjectResult({ B, A }, 2, true, false, Query, 7);
	TestEqual(TEXT("Complete project query is ready"), Result.State, EPaper2DPlusEffectQueryState::Ready);
	TestEqual(TEXT("Pinned exact current plus one eligible deduplicated row are visible"), Result.Rows.Num(), 2);
	TestTrue(TEXT("Unlisted current value remains pinned"), Result.Rows[0].bPinnedCurrent);
	TestEqual(TEXT("Complete global knowledge reports the pinned value outside all project libraries"),
		Result.Rows[0].Diagnostic, EPaper2DPlusEffectRowDiagnostic::OutsideAllProjectEffects);
	TestEqual(TEXT("First eligible duplicate follows normalized Profile path then authored row order"),
		Result.Rows[1].SourceProfilePath, ProfileA.ProfilePath);
	TestEqual(TEXT("Ineligible earlier duplicate was discarded before deduping"), Result.Rows[1].SourceRowIndex, 1);
	TestFalse(TEXT("Raw unlisted flipbooks never become ordinary result rows"),
		Result.Rows.ContainsByPredicate([&Unlisted](const FPaper2DPlusEffectLibraryRow& Row)
		{
			return Row.FlipbookPath == Unlisted && !Row.bPinnedCurrent;
		}));

	Query.SearchText = TEXT("does-not-match");
	Query.CurrentValue.Reset();
	const FPaper2DPlusEffectLibraryQueryResult SearchResult =
		FPaper2DPlusEffectLibraryIndex::BuildProjectResult({ A, B }, 2, true, false, Query, 8);
	TestEqual(TEXT("Search miss remains distinct from filter miss"),
		SearchResult.State, EPaper2DPlusEffectQueryState::NoSearchMatches);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusEffectLibrarySoftSnapshotTest,
	"Paper2DPlus.FrameCue.EffectLibrary.AuthoredFlipbookRowsRetainPathIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusEffectLibrarySoftSnapshotTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusEffectProfileAsset* Profile = NewObject<UPaper2DPlusEffectProfileAsset>();
	UPaperFlipbook* Flipbook = NewObject<UPaperFlipbook>(Profile, TEXT("AuthoredEffect"));
	const FSoftObjectPath AuthoredPath(Flipbook);
	FPaper2DPlusEffectProfileEntry& Entry = Profile->Effects.AddDefaulted_GetRef();
	Entry.EffectFlipbook = Flipbook;
	Entry.EffectName = TEXT("AuthoredEffect");

	const FPaper2DPlusEffectProfileSnapshot Snapshot =
		FPaper2DPlusEffectLibraryIndex::MakeSnapshot(
			*Profile,
			FSoftObjectPath(TEXT("/Game/FX/Profile.Profile")));
	TestEqual(TEXT("A valid authored row is retained"), Snapshot.Rows.Num(), 1);
	if (Snapshot.Rows.Num() == 1)
	{
		TestEqual(TEXT("Snapshot preserves the exact authored object path"),
			Snapshot.Rows[0].FlipbookPath, AuthoredPath);
	}
	TestTrue(TEXT("Snapshot construction does not rewrite the authored reference"),
		Entry.EffectFlipbook.Get() == Flipbook);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusEffectLibraryTruthfulStatesTest,
	"Paper2DPlus.FrameCue.EffectLibrary.TruthfulCharacterAndProjectStates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusEffectLibraryTruthfulStatesTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusEffectLibraryQueryTestPrivate;
	FPaper2DPlusEffectLibraryQuery Query;
	Query.CurrentValue = FSoftObjectPath(TEXT("/Game/FX/Current.Current"));

	TestEqual(TEXT("Unresolved context is resolving"),
		FPaper2DPlusEffectLibraryIndex::BuildCharacterResult(
			EPaper2DPlusEffectCharacterSourceState::Resolving, nullptr, Query, 1).State,
		EPaper2DPlusEffectQueryState::ResolvingContext);
	TestEqual(TEXT("Missing context is explicit"),
		FPaper2DPlusEffectLibraryIndex::BuildCharacterResult(
			EPaper2DPlusEffectCharacterSourceState::MissingContextOrAssignment, nullptr, Query, 2).State,
		EPaper2DPlusEffectQueryState::MissingContextOrAssignment);
	TestEqual(TEXT("Assigned library load failure is retryable"),
		FPaper2DPlusEffectLibraryIndex::BuildCharacterResult(
			EPaper2DPlusEffectCharacterSourceState::RetryableError, nullptr, Query, 3).State,
		EPaper2DPlusEffectQueryState::RetryableError);

	const FPaper2DPlusEffectProfileSnapshot Empty = MakeSnapshot(
		TEXT("/Game/FX/Empty.Empty"), {});
	TestEqual(TEXT("Loaded zero-row assigned library is empty rather than failed"),
		FPaper2DPlusEffectLibraryIndex::BuildCharacterResult(
			EPaper2DPlusEffectCharacterSourceState::Ready, &Empty, Query, 4).State,
		EPaper2DPlusEffectQueryState::EmptyAssignedLibrary);

	FPaper2DPlusEffectLibraryLoadRecord Pending;
	Pending.ProfilePath = FSoftObjectPath(TEXT("/Game/FX/Pending.Pending"));
	Pending.Status = EPaper2DPlusEffectLibraryLoadStatus::Pending;
	const FPaper2DPlusEffectLibraryQueryResult Discovering =
		FPaper2DPlusEffectLibraryIndex::BuildProjectResult({ Pending }, 2, false, false, Query, 5);
	TestEqual(TEXT("Incomplete discovery does not claim global absence"),
		Discovering.State, EPaper2DPlusEffectQueryState::DiscoveringProject);
	TestEqual(TEXT("Pinned current membership stays unknown until discovery completes"),
		Discovering.Rows[0].Diagnostic, EPaper2DPlusEffectRowDiagnostic::MembershipUnknown);

	FPaper2DPlusEffectLibraryLoadRecord Failed = Pending;
	Failed.Status = EPaper2DPlusEffectLibraryLoadStatus::Failed;
	Failed.Failure = FText::FromString(TEXT("Fixture load failure"));
	const FPaper2DPlusEffectLibraryQueryResult TotalFailure =
		FPaper2DPlusEffectLibraryIndex::BuildProjectResult({ Failed }, 1, true, false, Query, 6);
	TestEqual(TEXT("No successful library is total failure"),
		TotalFailure.State, EPaper2DPlusEffectQueryState::TotalFailure);
	TestTrue(TEXT("Total failure is retryable"), TotalFailure.bCanRetry);
	TestEqual(TEXT("Failure still preserves the pinned current value"), TotalFailure.Rows.Num(), 1);

	const FPaper2DPlusEffectLibraryQueryResult Refreshing =
		FPaper2DPlusEffectLibraryIndex::MakeRefreshingResult(TotalFailure, 7);
	TestEqual(TEXT("Live invalidation exposes an explicit refreshing state"),
		Refreshing.State, EPaper2DPlusEffectQueryState::Refreshing);
	TestEqual(TEXT("Refreshing preserves the last coherent rows until replacement is complete"),
		Refreshing.Rows.Num(), TotalFailure.Rows.Num());
	TestEqual(TEXT("Refreshing binds the replacement generation"),
		Refreshing.Generation, static_cast<uint64>(7));

	FPaper2DPlusEffectProjectQuerySession WarmSession;
	WarmSession.TotalLibraries = 1;
	FPaper2DPlusEffectLibraryLoadRecord WarmLibrary;
	WarmLibrary.ProfilePath = Empty.ProfilePath;
	WarmLibrary.Status = EPaper2DPlusEffectLibraryLoadStatus::Loaded;
	WarmLibrary.Snapshot = Empty;
	WarmSession.Libraries.Add(WarmLibrary);
	TestFalse(TEXT("A fully cached project session has no pending libraries"),
		WarmSession.HasPendingLibraries());
	const FPaper2DPlusEffectLibraryQueryResult WarmResult =
		FPaper2DPlusEffectLibraryIndex::BuildProjectResult(
			WarmSession.Libraries,
			WarmSession.TotalLibraries,
			!WarmSession.HasPendingLibraries(),
			false,
			Query,
			8);
	TestEqual(TEXT("A fully cached project session completes immediately"),
		WarmResult.State, EPaper2DPlusEffectQueryState::EmptyProject);
	TestTrue(TEXT("Warm-cache completion is authoritative"), WarmResult.bComplete);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusEffectLibraryModelPublishTest,
	"Paper2DPlus.FrameCue.EffectLibrary.EffectEditorCommitPublishesLiveInvalidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusEffectLibraryModelPublishTest::RunTest(const FString& Parameters)
{
	TSharedRef<FPaper2DPlusEffectLibraryIndex> Index =
		FPaper2DPlusEffectLibraryIndex::Get();
	Index->FlushPendingInvalidationForTests();
	int32 MatchingBroadcasts = 0;
	UPaper2DPlusEffectProfileAsset* Profile = NewObject<UPaper2DPlusEffectProfileAsset>();
	const FSoftObjectPath ProfilePath(Profile);
	const FDelegateHandle Handle = Index->OnInvalidated().AddLambda(
		[&MatchingBroadcasts, ProfilePath](const FPaper2DPlusEffectLibraryChange& Change)
		{
			if (Change.Affects(EPaper2DPlusEffectLibraryChangeDomain::EffectProfile)
				&& Change.ContainsPath(ProfilePath))
			{
				++MatchingBroadcasts;
			}
		});

	FEffectProfileEditorModel Model;
	Model.Initialize(Profile, false);
	UPaperFlipbook* Flipbook = NewObject<UPaperFlipbook>(Profile);
	const FEffectProfileIntakeResult Intake = Model.AddFlipbooks({ Flipbook });
	TestEqual(TEXT("Fixture commits one Effect Profile entry"), Intake.AddedCount, 1);
	TestTrue(TEXT("Successful Effect authoring queues the shared index invalidation"),
		Index->FlushPendingInvalidationForTests());
	TestEqual(TEXT("The open-picker invalidation stream receives the exact Profile once"),
		MatchingBroadcasts, 1);
	Profile->Effects[0].DisplayLabel = FText::FromString(TEXT("Advanced Details Edit"));
	Model.RefreshAfterExternalMutation();
	TestTrue(TEXT("Advanced Details/undo reconciliation also queues the shared invalidation"),
		Index->FlushPendingInvalidationForTests());
	TestEqual(TEXT("External committed mutation reaches open pickers once"),
		MatchingBroadcasts, 2);
	Index->OnInvalidated().Remove(Handle);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusEffectLibraryInvalidationTest,
	"Paper2DPlus.FrameCue.EffectLibrary.IndexOwnsCoalescedTypedInvalidationAndGenerations",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusEffectLibraryInvalidationTest::RunTest(const FString& Parameters)
{
	TSharedRef<FPaper2DPlusEffectLibraryIndex> Index = MakeShared<FPaper2DPlusEffectLibraryIndex>(false);
	TestEqual(TEXT("Constructing the index performs no demand loads"), Index->GetDemandLoadCountForTests(), 0);
	const uint64 OriginalGeneration = Index->GetGeneration();
	int32 Broadcasts = 0;
	FPaper2DPlusEffectLibraryChange LastChange;
	const FDelegateHandle Handle = Index->OnInvalidated().AddLambda(
		[&Broadcasts, &LastChange](const FPaper2DPlusEffectLibraryChange& Change)
		{
			++Broadcasts;
			LastChange = Change;
		});

	Index->PublishChange(
		EPaper2DPlusEffectLibraryChangeDomain::Catalog,
		FSoftObjectPath(TEXT("/Game/Data/Catalog.Catalog")));
	Index->PublishChange(
		EPaper2DPlusEffectLibraryChangeDomain::EffectProfile,
		FSoftObjectPath(TEXT("/Game/FX/Profile.Profile")));
	TestEqual(TEXT("First pending invalidation advances the stale-result generation once"),
		Index->GetGeneration(), OriginalGeneration + 1);
	TestFalse(TEXT("Stale generations are rejected before the coalesced broadcast"),
		Index->IsGenerationCurrent(OriginalGeneration));
	TestTrue(TEXT("A pending burst flushes once"), Index->FlushPendingInvalidationForTests());
	TestEqual(TEXT("One typed invalidation represents the burst"), Broadcasts, 1);
	TestTrue(TEXT("Catalog domain is retained"),
		LastChange.Affects(EPaper2DPlusEffectLibraryChangeDomain::Catalog));
	TestTrue(TEXT("Effect Profile domain is retained"),
		LastChange.Affects(EPaper2DPlusEffectLibraryChangeDomain::EffectProfile));
	TestEqual(TEXT("Normalized changed paths are retained deterministically"), LastChange.NormalizedPaths.Num(), 2);
	TestFalse(TEXT("Nothing remains to flush"), Index->FlushPendingInvalidationForTests());
	Index->OnInvalidated().Remove(Handle);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusEffectPickerCommitContractTest,
	"Paper2DPlus.FrameCue.EffectPicker.ExplicitCommitOnlyAndStaleResultRejection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusEffectPickerCommitContractTest::RunTest(const FString& Parameters)
{
	const FSoftObjectPath Current(TEXT("/Game/FX/Current.Current"));
	const FSoftObjectPath Focused(TEXT("/Game/FX/Focused.Focused"));
	FPaper2DPlusEffectPickerModel Model;
	Model.SetCurrentValue(Current);
	Model.SetFocusedPath(Focused);
	Model.SetScope(EPaper2DPlusEffectPickerScope::AllProjectEffects);
	Model.SetSearchText(TEXT("impact"));
	const uint64 InitialRequest = Model.BeginRefresh(10);
	FPaper2DPlusEffectLibraryQueryResult InitialResult;
	InitialResult.Generation = 10;
	FPaper2DPlusEffectLibraryRow CurrentRow;
	CurrentRow.FlipbookPath = Current;
	CurrentRow.NormalizedFlipbookPath = FPaper2DPlusEffectLibraryIndex::NormalizePath(Current);
	CurrentRow.bEligible = false;
	CurrentRow.bPinnedCurrent = true;
	CurrentRow.Diagnostic = EPaper2DPlusEffectRowDiagnostic::OutsideAllProjectEffects;
	InitialResult.Rows.Add(CurrentRow);
	FPaper2DPlusEffectLibraryRow FocusedRow;
	FocusedRow.FlipbookPath = Focused;
	FocusedRow.NormalizedFlipbookPath = FPaper2DPlusEffectLibraryIndex::NormalizePath(Focused);
	FocusedRow.bEligible = true;
	InitialResult.Rows.Add(FocusedRow);
	TestTrue(TEXT("Eligible picker fixture result is accepted"),
		Model.ApplyResult(InitialRequest, 10, MoveTemp(InitialResult)));

	int32 Writes = 0;
	FSoftObjectPath WrittenValue;
	const FOnPaper2DPlusEffectCommitted Commit = FOnPaper2DPlusEffectCommitted::CreateLambda(
		[&Writes, &WrittenValue](const FSoftObjectPath& Value)
		{
			++Writes;
			WrittenValue = Value;
			return true;
		});
	TestEqual(TEXT("Focus, scope, and search never write"), Writes, 0);
	TestTrue(TEXT("Use Selected explicitly commits a different focused value"),
		Model.TryCommit(EPaper2DPlusEffectCommitGesture::UseSelected, Commit));
	TestEqual(TEXT("One explicit gesture produces one write"), Writes, 1);
	TestEqual(TEXT("Exact focused path is written"), WrittenValue, Focused);
	Model.SetCurrentValue(Focused);
	TestFalse(TEXT("Confirming the already-current value is a no-op"),
		Model.TryCommit(EPaper2DPlusEffectCommitGesture::Enter, Commit));
	TestEqual(TEXT("No-op confirmation creates no second write"), Writes, 1);
	TestTrue(TEXT("Clear is an explicit null commit"),
		Model.TryCommit(EPaper2DPlusEffectCommitGesture::Clear, Commit));
	TestEqual(TEXT("Clear writes once"), Writes, 2);
	TestTrue(TEXT("Clear writes null"), WrittenValue.IsNull());
	Model.DismissWithoutCommit();
	TestEqual(TEXT("Escape/outside-dismiss seam writes nothing"), Writes, 2);

	Model.SetCurrentValue(Current);
	Model.SetFocusedPath(Focused);
	const FOnPaper2DPlusEffectCommitted RejectingCommit =
		FOnPaper2DPlusEffectCommitted::CreateLambda(
			[](const FSoftObjectPath&) { return false; });
	TestFalse(TEXT("A stale/reinstanced host may reject the live write"),
		Model.TryCommit(EPaper2DPlusEffectCommitGesture::DoubleClick, RejectingCommit));
	TestEqual(TEXT("Rejected host write restores the model's exact current value"),
		Model.GetCurrentValue(), Current);

	const uint64 IneligibleRequest = Model.BeginRefresh(11);
	FPaper2DPlusEffectLibraryQueryResult IneligibleResult;
	IneligibleResult.Generation = 11;
	FPaper2DPlusEffectLibraryRow IneligibleFocused = FocusedRow;
	IneligibleFocused.bEligible = false;
	IneligibleFocused.bPinnedCurrent = true;
	IneligibleFocused.Diagnostic = EPaper2DPlusEffectRowDiagnostic::FilterMismatch;
	IneligibleResult.Rows.Add(IneligibleFocused);
	TestTrue(TEXT("Pinned ineligible result is accepted for truthful presentation"),
		Model.ApplyResult(IneligibleRequest, 11, MoveTemp(IneligibleResult)));
	TestFalse(TEXT("Pinned filter-mismatch rows cannot commit through double click or Enter"),
		Model.TryCommit(EPaper2DPlusEffectCommitGesture::Enter, Commit));
	TestEqual(TEXT("Ineligible confirmation produces no write"), Writes, 2);

	const uint64 Request = Model.BeginRefresh(12);
	FPaper2DPlusEffectLibraryQueryResult Result;
	Result.Generation = 12;
	TestFalse(TEXT("A stale index generation cannot replace picker state"),
		Model.ApplyResult(Request, 13, MoveTemp(Result)));
	TestEqual(TEXT("Scope survives rejected refresh"),
		Model.GetScope(), EPaper2DPlusEffectPickerScope::AllProjectEffects);
	TestEqual(TEXT("Search survives rejected refresh"), Model.GetSearchText(), FString(TEXT("impact")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusEffectPickerSearchKeyboardTest,
	"Paper2DPlus.FrameCue.EffectPicker.SearchOwnsNavigationPreviewCommitFailureAndEscape",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusEffectPickerSearchKeyboardTest::RunTest(const FString& Parameters)
{
	const FSoftObjectPath Current(TEXT("/Game/FX/Current.Current"));
	const FSoftObjectPath Next(TEXT("/Game/FX/Next.Next"));
	const TSharedRef<FPaper2DPlusEffectLibraryIndex> Index =
		MakeShared<FPaper2DPlusEffectLibraryIndex>(false);
	int32 PreviewCount = 0;
	FSoftObjectPath PreviewedPath;
	int32 RejectedWrites = 0;
	bool bDismissed = false;
	const TSharedRef<SPaper2DPlusEffectPicker> Picker =
		SNew(SPaper2DPlusEffectPicker)
		.LibraryIndex(Index)
		.CurrentValue(Current)
		.OnPreviewed(FOnPaper2DPlusEffectPreviewed::CreateLambda(
			[&PreviewCount, &PreviewedPath](const FSoftObjectPath& Path)
			{
				++PreviewCount;
				PreviewedPath = Path;
			}))
		.OnCommitted(FOnPaper2DPlusEffectCommitted::CreateLambda(
			[&RejectedWrites](const FSoftObjectPath&)
			{
				++RejectedWrites;
				return false;
			}))
		.OnDismissed(FSimpleDelegate::CreateLambda([&bDismissed]() { bDismissed = true; }));

	FPaper2DPlusEffectLibraryQueryResult Result;
	Result.Generation = Index->GetGeneration();
	for (const FSoftObjectPath& Path : { Current, Next })
	{
		FPaper2DPlusEffectLibraryRow& Row = Result.Rows.AddDefaulted_GetRef();
		Row.FlipbookPath = Path;
		Row.NormalizedFlipbookPath = FPaper2DPlusEffectLibraryIndex::NormalizePath(Path);
		Row.DisplayName = FText::FromString(Path.GetAssetName());
		Row.bEligible = true;
		Row.Diagnostic = EPaper2DPlusEffectRowDiagnostic::Eligible;
	}
	Picker->ApplyResultForTests(MoveTemp(Result));
	const TSharedPtr<SSearchBox> Search = Picker->GetSearchBoxForTests();
	TestTrue(TEXT("The real search widget exists"), Search.IsValid());
	if (!Search.IsValid())
	{
		return false;
	}
	const auto KeyEvent = [](const FKey& Key)
	{
		return FKeyEvent(Key, FModifierKeysState(), 0, false, 0, 0);
	};
	TestTrue(TEXT("The search-field key delegate handles Down"),
		Picker->HandleSearchKeyDownForTests(KeyEvent(EKeys::Down)).IsEventHandled());
	TestEqual(TEXT("Down moves exact picker focus"),
		Picker->GetModelForTests().GetFocusedPath(), Next);
	TestEqual(TEXT("Keyboard focus movement previews once"), PreviewCount, 1);
	TestEqual(TEXT("Keyboard preview receives the exact soft path"), PreviewedPath, Next);

	TestTrue(TEXT("The search-field key delegate handles Enter"),
		Picker->HandleSearchKeyDownForTests(KeyEvent(EKeys::Enter)).IsEventHandled());
	TestEqual(TEXT("Enter attempts exactly one explicit write"), RejectedWrites, 1);
	TestFalse(TEXT("A rejected live write keeps the picker open"), bDismissed);
	TestFalse(TEXT("A rejected stale/deleted selection exposes a visible error"),
		Picker->GetCommitErrorForTests().IsEmpty());
	TestEqual(TEXT("Rejected write restores the exact current value"),
		Picker->GetModelForTests().GetCurrentValue(), Current);

	Picker->ApplyResultForTests([&]()
	{
		FPaper2DPlusEffectLibraryQueryResult Rebuilt;
		Rebuilt.Generation = Index->GetGeneration();
		for (const FSoftObjectPath& Path : { Current, Next })
		{
			FPaper2DPlusEffectLibraryRow& Row = Rebuilt.Rows.AddDefaulted_GetRef();
			Row.FlipbookPath = Path;
			Row.NormalizedFlipbookPath = FPaper2DPlusEffectLibraryIndex::NormalizePath(Path);
			Row.DisplayName = FText::FromString(Path.GetAssetName());
			Row.bEligible = true;
			Row.Diagnostic = EPaper2DPlusEffectRowDiagnostic::Eligible;
		}
		return Rebuilt;
	}());
	Picker->HandleSearchKeyDownForTests(KeyEvent(EKeys::Down));
	Search->SetText(FText::FromString(TEXT("no-current-result")));
	TestTrue(TEXT("Fast type plus Enter is handled"),
		Picker->HandleSearchKeyDownForTests(KeyEvent(EKeys::Enter)).IsEventHandled());
	TestEqual(TEXT("Enter flushes the pending search instead of committing its stale focused row"),
		RejectedWrites, 1);
	TestFalse(TEXT("A search refresh that removes the stale result keeps the picker open"),
		bDismissed);

	TestTrue(TEXT("The search-field key delegate handles Escape"),
		Picker->HandleSearchKeyDownForTests(KeyEvent(EKeys::Escape)).IsEventHandled());
	TestTrue(TEXT("Escape dismisses without another write"), bDismissed);
	TestEqual(TEXT("Escape never retries the rejected write"), RejectedWrites, 1);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
