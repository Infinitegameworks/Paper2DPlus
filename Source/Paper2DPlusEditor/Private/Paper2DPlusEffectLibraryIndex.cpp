// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "Paper2DPlusEffectLibraryIndex.h"

#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Async/Async.h"
#include "Modules/ModuleManager.h"
#include "Paper2DPlusCharacterCatalogAsset.h"
#include "Paper2DPlusEffectProfileAsset.h"
#include "PaperFlipbook.h"
#include "Runtime/Launch/Resources/Version.h"

#include "EffectProfileContextResolver.h"

#define LOCTEXT_NAMESPACE "Paper2DPlusEffectLibraryIndex"

namespace Paper2DPlusEffectLibraryIndexPrivate
{
	TSharedPtr<FPaper2DPlusEffectLibraryIndex> SharedIndex;

	FSoftObjectPath AssetPath(const FAssetData& AssetData)
	{
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
		return FSoftObjectPath(AssetData.ObjectPath.ToString());
#else
		return AssetData.GetSoftObjectPath();
#endif
	}

	FName AssetClassName(const FAssetData& AssetData)
	{
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
		return AssetData.AssetClass;
#else
		return AssetData.AssetClassPath.GetAssetName();
#endif
	}

	FSoftObjectPath CanonicalEffectPath(const FSoftObjectPath& Path)
	{
		return UPaper2DPlusEffectProfileAsset::GetCanonicalEffectFlipbookPathNoLoad(Path);
	}

	FString NormalizeEffectPath(const FSoftObjectPath& Path)
	{
		return FPaper2DPlusEffectLibraryIndex::NormalizePath(CanonicalEffectPath(Path));
	}

	bool MatchesHierarchically(FGameplayTag Authored, FGameplayTag Required)
	{
		return Authored.IsValid() && Required.IsValid() && Authored.MatchesTag(Required);
	}

	bool MatchesFilters(
		const FPaper2DPlusEffectLibraryRow& Row,
		const FPaper2DPlusEffectLibraryQuery& Query)
	{
		if (Query.TypeFilter.IsValid() && !MatchesHierarchically(Row.TypeTag, Query.TypeFilter))
		{
			return false;
		}

		for (const FGameplayTag& Required : Query.DescriptorFilters)
		{
			bool bMatched = false;
			for (const FGameplayTag& Authored : Row.DescriptorTags)
			{
				if (MatchesHierarchically(Authored, Required))
				{
					bMatched = true;
					break;
				}
			}
			if (!bMatched)
			{
				return false;
			}
		}
		return true;
	}

	bool MatchesSearch(
		const FPaper2DPlusEffectLibraryRow& Row,
		const FString& SearchText)
	{
		FString Search = SearchText;
		Search.TrimStartAndEndInline();
		if (Search.IsEmpty())
		{
			return true;
		}

		return Row.DisplayName.ToString().Contains(Search, ESearchCase::IgnoreCase)
			|| Row.FlipbookPath.GetAssetName().Contains(Search, ESearchCase::IgnoreCase)
			|| Row.FlipbookPath.ToString().Contains(Search, ESearchCase::IgnoreCase)
			|| Row.TypeTag.ToString().Contains(Search, ESearchCase::IgnoreCase)
			|| Row.DescriptorTags.ToStringSimple().Contains(Search, ESearchCase::IgnoreCase);
	}

	struct FProjectedRows
	{
		TArray<FPaper2DPlusEffectLibraryRow> Rows;
		int32 AuthoredRows = 0;
		int32 EligibleRows = 0;
		int32 SearchMatches = 0;
	};

	FProjectedRows ProjectRows(
		TArray<FPaper2DPlusEffectLibraryRow> Authored,
		const FPaper2DPlusEffectLibraryQuery& Query,
		EPaper2DPlusEffectRowDiagnostic AbsentCurrentDiagnostic)
	{
		for (FPaper2DPlusEffectLibraryRow& Row : Authored)
		{
			Row.FlipbookPath = CanonicalEffectPath(Row.FlipbookPath);
			Row.NormalizedFlipbookPath = NormalizeEffectPath(Row.FlipbookPath);
			if (Row.DisplayName.IsEmpty() && !Row.FlipbookPath.IsNull())
			{
				Row.DisplayName = FText::FromString(Row.FlipbookPath.GetAssetName());
			}
		}
		Authored.StableSort([](
			const FPaper2DPlusEffectLibraryRow& A,
			const FPaper2DPlusEffectLibraryRow& B)
		{
			const FString AProfile = FPaper2DPlusEffectLibraryIndex::NormalizePath(A.SourceProfilePath);
			const FString BProfile = FPaper2DPlusEffectLibraryIndex::NormalizePath(B.SourceProfilePath);
			if (AProfile != BProfile)
			{
				return AProfile < BProfile;
			}
			return A.SourceRowIndex < B.SourceRowIndex;
		});

		FProjectedRows Projection;
		TArray<FPaper2DPlusEffectLibraryRow> Eligible;
		TSet<FString> SeenEligiblePaths;
		const FString CurrentKey = NormalizeEffectPath(Query.CurrentValue);
		TOptional<FPaper2DPlusEffectLibraryRow> FirstCurrentAuthored;
		TOptional<FPaper2DPlusEffectLibraryRow> FirstCurrentEligible;

		for (const FPaper2DPlusEffectLibraryRow& Row : Authored)
		{
			if (!Row.HasIdentity())
			{
				continue;
			}
			++Projection.AuthoredRows;
			if (!CurrentKey.IsEmpty()
				&& Row.NormalizedFlipbookPath == CurrentKey
				&& !FirstCurrentAuthored.IsSet())
			{
				FirstCurrentAuthored = Row;
			}

			// Product policy evaluates Type/Descriptor eligibility per authored row before deduping.
			if (!MatchesFilters(Row, Query) || SeenEligiblePaths.Contains(Row.NormalizedFlipbookPath))
			{
				continue;
			}
			SeenEligiblePaths.Add(Row.NormalizedFlipbookPath);
			Eligible.Add(Row);
			if (!CurrentKey.IsEmpty()
				&& Row.NormalizedFlipbookPath == CurrentKey
				&& !FirstCurrentEligible.IsSet())
			{
				FirstCurrentEligible = Row;
			}
		}

		Projection.EligibleRows = Eligible.Num();
		for (FPaper2DPlusEffectLibraryRow& Row : Eligible)
		{
			if (MatchesSearch(Row, Query.SearchText))
			{
				Row.bEligible = true;
				Row.Diagnostic = EPaper2DPlusEffectRowDiagnostic::Eligible;
				Projection.Rows.Add(Row);
			}
		}
		Projection.SearchMatches = Projection.Rows.Num();

		if (!CurrentKey.IsEmpty())
		{
			const int32 VisibleIndex = Projection.Rows.IndexOfByPredicate(
				[&CurrentKey](const FPaper2DPlusEffectLibraryRow& Row)
				{
					return Row.NormalizedFlipbookPath == CurrentKey;
				});
			FPaper2DPlusEffectLibraryRow Pinned;
			if (Projection.Rows.IsValidIndex(VisibleIndex))
			{
				Pinned = Projection.Rows[VisibleIndex];
				Projection.Rows.RemoveAt(VisibleIndex);
			}
			else if (FirstCurrentEligible.IsSet())
			{
				Pinned = FirstCurrentEligible.GetValue();
				Pinned.bEligible = false;
				Pinned.Diagnostic = EPaper2DPlusEffectRowDiagnostic::SearchMismatch;
			}
			else if (FirstCurrentAuthored.IsSet())
			{
				Pinned = FirstCurrentAuthored.GetValue();
				Pinned.bEligible = false;
				Pinned.Diagnostic = EPaper2DPlusEffectRowDiagnostic::FilterMismatch;
			}
			else
			{
				Pinned.FlipbookPath = Query.CurrentValue;
				Pinned.NormalizedFlipbookPath = CurrentKey;
				Pinned.DisplayName = FText::FromString(Query.CurrentValue.GetAssetName());
				Pinned.bEligible = false;
				Pinned.Diagnostic = AbsentCurrentDiagnostic;
			}
			Pinned.bPinnedCurrent = true;
			Projection.Rows.Insert(MoveTemp(Pinned), 0);
		}

		return Projection;
	}

	FText DescribeState(
		EPaper2DPlusEffectQueryState State,
		int32 Loaded,
		int32 Total,
		int32 Failed)
	{
		switch (State)
		{
		case EPaper2DPlusEffectQueryState::ResolvingContext:
			return LOCTEXT("ResolvingContext", "Resolving this Character's Effect library...");
		case EPaper2DPlusEffectQueryState::LoadingAssignedLibrary:
			return LOCTEXT("LoadingAssigned", "Loading the Catalog-assigned Effect library...");
		case EPaper2DPlusEffectQueryState::Ready:
			return LOCTEXT("Ready", "Effect choices are ready.");
		case EPaper2DPlusEffectQueryState::EmptyAssignedLibrary:
			return LOCTEXT("EmptyAssigned", "The assigned Effect Profile has no authored Flipbook entries.");
		case EPaper2DPlusEffectQueryState::NoEligibleRows:
			return LOCTEXT("NoEligible", "No authored Effects match the required Type and Descriptor filters.");
		case EPaper2DPlusEffectQueryState::NoSearchMatches:
			return LOCTEXT("NoSearch", "No eligible Effects match this search.");
		case EPaper2DPlusEffectQueryState::MissingContextOrAssignment:
			return LOCTEXT("MissingContext", "Character Effects needs a configured Catalog, Character context, and Effect Profile assignment. All Project Effects remains available.");
		case EPaper2DPlusEffectQueryState::RetryableError:
			return LOCTEXT("Retryable", "The assigned Effect library could not be read. Retry after correcting the Catalog or asset.");
		case EPaper2DPlusEffectQueryState::DiscoveringProject:
			return FText::Format(
				LOCTEXT("Discovering", "Discovering Effect Profiles ({0} of {1} loaded)..."),
				FText::AsNumber(Loaded), FText::AsNumber(Total));
		case EPaper2DPlusEffectQueryState::PartialResults:
			return FText::Format(
				LOCTEXT("Partial", "Showing partial Effect results ({0} of {1} libraries loaded)."),
				FText::AsNumber(Loaded), FText::AsNumber(Total));
		case EPaper2DPlusEffectQueryState::EmptyProject:
			return LOCTEXT("EmptyProject", "No authored entries exist in any discovered Effect Profile.");
		case EPaper2DPlusEffectQueryState::PartialFailure:
			return FText::Format(
				LOCTEXT("PartialFailure", "Showing usable results, but {0} of {1} Effect libraries failed. Retry failed libraries."),
				FText::AsNumber(Failed), FText::AsNumber(Total));
		case EPaper2DPlusEffectQueryState::TotalFailure:
			return LOCTEXT("TotalFailure", "Effect Profile discovery or loading failed. The exact current value is preserved; retry is available.");
		case EPaper2DPlusEffectQueryState::Refreshing:
			return LOCTEXT("Refreshing", "Refreshing Effect choices; the previous coherent results remain available.");
		default:
			return FText::GetEmpty();
		}
	}

	void FinalizeResult(FPaper2DPlusEffectLibraryQueryResult& Result)
	{
		Result.bHasUsableRows = Result.Rows.ContainsByPredicate(
			[](const FPaper2DPlusEffectLibraryRow& Row) { return Row.bEligible; });
		Result.StatusText = DescribeState(
			Result.State,
			Result.LoadedLibraries,
			Result.TotalLibraries,
			Result.FailedLibraries);
	}
}

const FPaper2DPlusEffectLibraryRow* FPaper2DPlusEffectLibraryQueryResult::FindRow(
	const FSoftObjectPath& Path) const
{
	const FString Key = Paper2DPlusEffectLibraryIndexPrivate::NormalizeEffectPath(Path);
	return Rows.FindByPredicate([&Key](const FPaper2DPlusEffectLibraryRow& Row)
	{
		return Row.NormalizedFlipbookPath == Key;
	});
}

bool FPaper2DPlusEffectProjectQuerySession::HasPendingLibraries() const
{
	return Libraries.ContainsByPredicate([](const FPaper2DPlusEffectLibraryLoadRecord& Record)
	{
		return Record.Status == EPaper2DPlusEffectLibraryLoadStatus::Pending;
	});
}

bool FPaper2DPlusEffectLibraryChange::Affects(
	EPaper2DPlusEffectLibraryChangeDomain Domain) const
{
	return EnumHasAnyFlags(Domains, Domain);
}

bool FPaper2DPlusEffectLibraryChange::ContainsPath(const FSoftObjectPath& Path) const
{
	return NormalizedPaths.Contains(FPaper2DPlusEffectLibraryIndex::NormalizePath(Path));
}

FPaper2DPlusEffectLibraryIndex::FPaper2DPlusEffectLibraryIndex(bool bBindAssetRegistry)
{
	if (bBindAssetRegistry)
	{
		BindAssetRegistry();
	}
}

FPaper2DPlusEffectLibraryIndex::~FPaper2DPlusEffectLibraryIndex()
{
	UnbindAssetRegistry();
}

TSharedRef<FPaper2DPlusEffectLibraryIndex> FPaper2DPlusEffectLibraryIndex::Get()
{
	using namespace Paper2DPlusEffectLibraryIndexPrivate;
	if (!SharedIndex.IsValid())
	{
		SharedIndex = MakeShared<FPaper2DPlusEffectLibraryIndex>();
	}
	return SharedIndex.ToSharedRef();
}

void FPaper2DPlusEffectLibraryIndex::Shutdown()
{
	Paper2DPlusEffectLibraryIndexPrivate::SharedIndex.Reset();
}

FString FPaper2DPlusEffectLibraryIndex::NormalizePath(const FSoftObjectPath& Path)
{
	FString Result = Path.ToString();
	Result.TrimStartAndEndInline();
	Result.ToLowerInline();
	return Result;
}

FPaper2DPlusEffectProfileSnapshot FPaper2DPlusEffectLibraryIndex::MakeSnapshot(
	const UPaper2DPlusEffectProfileAsset& Profile,
	const FSoftObjectPath& ProfilePath)
{
	FPaper2DPlusEffectProfileSnapshot Snapshot;
	Snapshot.ProfilePath = ProfilePath.IsNull() ? FSoftObjectPath(&Profile) : ProfilePath;
	Snapshot.NormalizedProfilePath = NormalizePath(Snapshot.ProfilePath);
	Snapshot.Rows.Reserve(Profile.Effects.Num());
	for (int32 Index = 0; Index < Profile.Effects.Num(); ++Index)
	{
		const FPaper2DPlusEffectProfileEntry& Entry = Profile.Effects[Index];
		// Snapshot construction is metadata-only: opening or querying a library must not warm its art.
		const FSoftObjectPath FlipbookPath =
			Paper2DPlusEffectLibraryIndexPrivate::CanonicalEffectPath(
				Entry.GetEffectFlipbookPath());
		if (FlipbookPath.IsNull())
		{
			continue;
		}

		FPaper2DPlusEffectLibraryRow& Row = Snapshot.Rows.AddDefaulted_GetRef();
		Row.FlipbookPath = FlipbookPath;
		Row.NormalizedFlipbookPath =
			Paper2DPlusEffectLibraryIndexPrivate::NormalizeEffectPath(FlipbookPath);
		Row.DisplayName = Entry.DisplayLabel.IsEmpty()
			? FText::FromString(FlipbookPath.GetAssetName())
			: Entry.DisplayLabel;
		Row.SourceProfilePath = Snapshot.ProfilePath;
		Row.SourceRowIndex = Index;
		Row.TypeTag = Entry.TypeTag;
		Row.DescriptorTags = Entry.DescriptorTags;
	}
	return Snapshot;
}

FPaper2DPlusEffectLibraryQueryResult FPaper2DPlusEffectLibraryIndex::BuildCharacterResult(
	EPaper2DPlusEffectCharacterSourceState SourceState,
	const FPaper2DPlusEffectProfileSnapshot* Snapshot,
	const FPaper2DPlusEffectLibraryQuery& Query,
	uint64 InGeneration)
{
	using namespace Paper2DPlusEffectLibraryIndexPrivate;
	FPaper2DPlusEffectLibraryQueryResult Result;
	Result.Scope = EPaper2DPlusEffectPickerScope::CharacterEffects;
	Result.Generation = InGeneration;
	Result.TotalLibraries = SourceState == EPaper2DPlusEffectCharacterSourceState::Ready ? 1 : 0;

	TArray<FPaper2DPlusEffectLibraryRow> Authored;
	if (Snapshot)
	{
		Authored = Snapshot->Rows;
		Result.LoadedLibraries = 1;
	}
	const EPaper2DPlusEffectRowDiagnostic MissingDiagnostic =
		SourceState == EPaper2DPlusEffectCharacterSourceState::Ready && Snapshot
		? EPaper2DPlusEffectRowDiagnostic::OutsideCharacterEffects
		: EPaper2DPlusEffectRowDiagnostic::MembershipUnknown;
	const FProjectedRows Projection = ProjectRows(MoveTemp(Authored), Query, MissingDiagnostic);
	Result.Rows = Projection.Rows;

	switch (SourceState)
	{
	case EPaper2DPlusEffectCharacterSourceState::Resolving:
		Result.State = EPaper2DPlusEffectQueryState::ResolvingContext;
		break;
	case EPaper2DPlusEffectCharacterSourceState::MissingContextOrAssignment:
		Result.State = EPaper2DPlusEffectQueryState::MissingContextOrAssignment;
		Result.bComplete = true;
		break;
	case EPaper2DPlusEffectCharacterSourceState::RetryableError:
		Result.State = EPaper2DPlusEffectQueryState::RetryableError;
		Result.bCanRetry = true;
		Result.bComplete = true;
		break;
	case EPaper2DPlusEffectCharacterSourceState::Ready:
		if (!Snapshot)
		{
			Result.State = EPaper2DPlusEffectQueryState::LoadingAssignedLibrary;
		}
		else if (Projection.AuthoredRows == 0)
		{
			Result.State = EPaper2DPlusEffectQueryState::EmptyAssignedLibrary;
			Result.bComplete = true;
		}
		else if (Projection.EligibleRows == 0)
		{
			Result.State = EPaper2DPlusEffectQueryState::NoEligibleRows;
			Result.bComplete = true;
		}
		else if (Projection.SearchMatches == 0)
		{
			Result.State = EPaper2DPlusEffectQueryState::NoSearchMatches;
			Result.bComplete = true;
		}
		else
		{
			Result.State = EPaper2DPlusEffectQueryState::Ready;
			Result.bComplete = true;
		}
		break;
	default:
		break;
	}

	FinalizeResult(Result);
	return Result;
}

FPaper2DPlusEffectLibraryQueryResult FPaper2DPlusEffectLibraryIndex::BuildProjectResult(
	TArray<FPaper2DPlusEffectLibraryLoadRecord> Libraries,
	int32 InTotalLibraries,
	bool bDiscoveryComplete,
	bool bDiscoveryFailed,
	const FPaper2DPlusEffectLibraryQuery& Query,
	uint64 InGeneration)
{
	using namespace Paper2DPlusEffectLibraryIndexPrivate;
	Libraries.StableSort([](
		const FPaper2DPlusEffectLibraryLoadRecord& A,
		const FPaper2DPlusEffectLibraryLoadRecord& B)
	{
		return FPaper2DPlusEffectLibraryIndex::NormalizePath(A.ProfilePath)
			< FPaper2DPlusEffectLibraryIndex::NormalizePath(B.ProfilePath);
	});

	FPaper2DPlusEffectLibraryQueryResult Result;
	Result.Scope = EPaper2DPlusEffectPickerScope::AllProjectEffects;
	Result.Generation = InGeneration;
	Result.TotalLibraries = FMath::Max(InTotalLibraries, Libraries.Num());
	TArray<FPaper2DPlusEffectLibraryRow> Authored;
	for (const FPaper2DPlusEffectLibraryLoadRecord& Library : Libraries)
	{
		if (Library.Status == EPaper2DPlusEffectLibraryLoadStatus::Loaded)
		{
			++Result.LoadedLibraries;
			Authored.Append(Library.Snapshot.Rows);
		}
		else if (Library.Status == EPaper2DPlusEffectLibraryLoadStatus::Failed)
		{
			++Result.FailedLibraries;
			Result.Failures.Add(Library.Failure);
		}
	}

	const bool bGloballyComplete = bDiscoveryComplete
		&& !bDiscoveryFailed
		&& Result.FailedLibraries == 0
		&& Result.LoadedLibraries == Result.TotalLibraries;
	const FProjectedRows Projection = ProjectRows(
		MoveTemp(Authored),
		Query,
		bGloballyComplete
			? EPaper2DPlusEffectRowDiagnostic::OutsideAllProjectEffects
			: EPaper2DPlusEffectRowDiagnostic::MembershipUnknown);
	Result.Rows = Projection.Rows;
	Result.bCanRetry = bDiscoveryFailed || Result.FailedLibraries > 0;

	if (!bDiscoveryComplete)
	{
		Result.State = Projection.SearchMatches > 0
			? EPaper2DPlusEffectQueryState::PartialResults
			: EPaper2DPlusEffectQueryState::DiscoveringProject;
	}
	else if (bDiscoveryFailed || Result.FailedLibraries > 0)
	{
		Result.State = Result.LoadedLibraries > 0
			? EPaper2DPlusEffectQueryState::PartialFailure
			: EPaper2DPlusEffectQueryState::TotalFailure;
		Result.bComplete = true;
	}
	else if (Result.TotalLibraries == 0 || Projection.AuthoredRows == 0)
	{
		Result.State = EPaper2DPlusEffectQueryState::EmptyProject;
		Result.bComplete = true;
	}
	else if (Projection.EligibleRows == 0)
	{
		Result.State = EPaper2DPlusEffectQueryState::NoEligibleRows;
		Result.bComplete = true;
	}
	else if (Projection.SearchMatches == 0)
	{
		Result.State = EPaper2DPlusEffectQueryState::NoSearchMatches;
		Result.bComplete = true;
	}
	else
	{
		Result.State = EPaper2DPlusEffectQueryState::Ready;
		Result.bComplete = true;
	}

	FinalizeResult(Result);
	return Result;
}

FPaper2DPlusEffectLibraryQueryResult FPaper2DPlusEffectLibraryIndex::MakeRefreshingResult(
	const FPaper2DPlusEffectLibraryQueryResult& Previous,
	uint64 InGeneration)
{
	FPaper2DPlusEffectLibraryQueryResult Result = Previous;
	Result.State = EPaper2DPlusEffectQueryState::Refreshing;
	Result.Generation = InGeneration;
	Result.bComplete = false;
	Result.StatusText = Paper2DPlusEffectLibraryIndexPrivate::DescribeState(
		Result.State, Result.LoadedLibraries, Result.TotalLibraries, Result.FailedLibraries);
	return Result;
}

FPaper2DPlusEffectLibraryQueryResult FPaper2DPlusEffectLibraryIndex::OpenCharacterQuery(
	const FPaper2DPlusEffectProfileContext& Context,
	const FPaper2DPlusEffectLibraryQuery& Query)
{
	EPaper2DPlusEffectCharacterSourceState SourceState =
		EPaper2DPlusEffectCharacterSourceState::Resolving;
	switch (Context.Status)
	{
	case EPaper2DPlusEffectContextStatus::Unresolved:
		break;
	case EPaper2DPlusEffectContextStatus::UnscopedNoDefaultCatalog:
	case EPaper2DPlusEffectContextStatus::ScopedCharacterMissing:
	case EPaper2DPlusEffectContextStatus::ScopedEffectUnassigned:
		SourceState = EPaper2DPlusEffectCharacterSourceState::MissingContextOrAssignment;
		break;
	case EPaper2DPlusEffectContextStatus::ScopedCatalogUnavailable:
	case EPaper2DPlusEffectContextStatus::ScopedEffectUnavailable:
		SourceState = EPaper2DPlusEffectCharacterSourceState::RetryableError;
		break;
	case EPaper2DPlusEffectContextStatus::ScopedReady:
		SourceState = EPaper2DPlusEffectCharacterSourceState::Ready;
		break;
	default:
		break;
	}

	if (SourceState != EPaper2DPlusEffectCharacterSourceState::Ready)
	{
		return BuildCharacterResult(SourceState, nullptr, Query, Generation);
	}

	FPaper2DPlusEffectProfileSnapshot Snapshot;
	if (FindCachedSnapshot(Context.EffectProfilePath, Snapshot))
	{
		return BuildCharacterResult(SourceState, &Snapshot, Query, Generation);
	}

	const uint64 ExpectedGeneration = Generation;
	++DemandLoadCount;
	const UPaper2DPlusEffectProfileAsset* Profile =
		Cast<UPaper2DPlusEffectProfileAsset>(Context.EffectProfilePath.TryLoad());
	if (!Profile)
	{
		return BuildCharacterResult(
			EPaper2DPlusEffectCharacterSourceState::RetryableError,
			nullptr,
			Query,
			Generation);
	}

	Snapshot = MakeSnapshot(*Profile, Context.EffectProfilePath);
	if (!CommitSnapshot(ExpectedGeneration, Snapshot))
	{
		return MakeRefreshingResult(
			BuildCharacterResult(SourceState, nullptr, Query, ExpectedGeneration),
			Generation);
	}
	return BuildCharacterResult(SourceState, &Snapshot, Query, Generation);
}

FPaper2DPlusEffectLibraryQueryResult FPaper2DPlusEffectLibraryIndex::OpenAllProjectQuery(
	const FPaper2DPlusEffectLibraryQuery& Query,
	bool bRetryFailedLibrariesOnly)
{
	// Validation/headless callers may consume the complete synchronous seam. The Slate picker uses the
	// staged Begin/Advance path below so hundreds of libraries never load in one interaction frame.
	(void)bRetryFailedLibrariesOnly;
	FPaper2DPlusEffectProjectQuerySession Session;
	FPaper2DPlusEffectLibraryQueryResult Result = BeginAllProjectQuery(Query, Session);
	while (Session.HasPendingLibraries() && IsGenerationCurrent(Session.Generation))
	{
		Result = AdvanceAllProjectQuery(Session, Session.TotalLibraries);
	}
	return Result;
}

FPaper2DPlusEffectLibraryQueryResult FPaper2DPlusEffectLibraryIndex::BeginAllProjectQuery(
	const FPaper2DPlusEffectLibraryQuery& Query,
	FPaper2DPlusEffectProjectQuerySession& OutSession)
{
	OutSession = FPaper2DPlusEffectProjectQuerySession();
	OutSession.Query = Query;
	OutSession.Generation = Generation;
	if (!BoundAssetRegistry)
	{
		OutSession.bDiscoveryFailed = true;
		return BuildProjectResult({}, 0, true, true, Query, Generation);
	}

	FARFilter Filter;
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
	Filter.ClassNames.Add(UPaper2DPlusEffectProfileAsset::StaticClass()->GetFName());
#else
	Filter.ClassPaths.Add(UPaper2DPlusEffectProfileAsset::StaticClass()->GetClassPathName());
#endif
	TArray<FAssetData> Assets;
	BoundAssetRegistry->GetAssets(Filter, Assets);
	Assets.Sort([](const FAssetData& A, const FAssetData& B)
	{
		return FPaper2DPlusEffectLibraryIndex::NormalizePath(
			Paper2DPlusEffectLibraryIndexPrivate::AssetPath(A))
			< FPaper2DPlusEffectLibraryIndex::NormalizePath(
				Paper2DPlusEffectLibraryIndexPrivate::AssetPath(B));
	});

	TSet<FString> SeenProfiles;
	for (const FAssetData& Asset : Assets)
	{
		const FSoftObjectPath ProfilePath = Paper2DPlusEffectLibraryIndexPrivate::AssetPath(Asset);
		const FString ProfileKey = NormalizePath(ProfilePath);
		if (ProfileKey.IsEmpty() || SeenProfiles.Contains(ProfileKey))
		{
			continue;
		}
		SeenProfiles.Add(ProfileKey);

		FPaper2DPlusEffectLibraryLoadRecord& Record = OutSession.Libraries.AddDefaulted_GetRef();
		Record.ProfilePath = ProfilePath;
		// A retry/refresh keeps already coherent copied snapshots visible and never reloads their
		// assets. Only new, changed, or previously failed Profile paths remain pending.
		if (FindCachedSnapshot(ProfilePath, Record.Snapshot))
		{
			Record.Status = EPaper2DPlusEffectLibraryLoadStatus::Loaded;
		}
	}
	OutSession.TotalLibraries = SeenProfiles.Num();
	return BuildProjectResult(
		OutSession.Libraries,
		OutSession.TotalLibraries,
		!OutSession.HasPendingLibraries(),
		false,
		Query,
		Generation);
}

FPaper2DPlusEffectLibraryQueryResult FPaper2DPlusEffectLibraryIndex::AdvanceAllProjectQuery(
	FPaper2DPlusEffectProjectQuerySession& Session,
	int32 MaxLibrariesToLoad)
{
	if (!IsGenerationCurrent(Session.Generation))
	{
		const FPaper2DPlusEffectLibraryQueryResult Previous = BuildProjectResult(
			Session.Libraries,
			Session.TotalLibraries,
			false,
			Session.bDiscoveryFailed,
			Session.Query,
			Session.Generation);
		return MakeRefreshingResult(Previous, Generation);
	}

	int32 Remaining = FMath::Max(1, MaxLibrariesToLoad);
	for (FPaper2DPlusEffectLibraryLoadRecord& Record : Session.Libraries)
	{
		if (Remaining <= 0)
		{
			break;
		}
		if (Record.Status != EPaper2DPlusEffectLibraryLoadStatus::Pending)
		{
			continue;
		}
		--Remaining;
		if (FindCachedSnapshot(Record.ProfilePath, Record.Snapshot))
		{
			Record.Status = EPaper2DPlusEffectLibraryLoadStatus::Loaded;
			continue;
		}

		++DemandLoadCount;
		const UPaper2DPlusEffectProfileAsset* Profile =
			Cast<UPaper2DPlusEffectProfileAsset>(Record.ProfilePath.TryLoad());
		if (!Profile)
		{
			Record.Status = EPaper2DPlusEffectLibraryLoadStatus::Failed;
			Record.Failure = FText::Format(
				LOCTEXT("ProfileLoadFailed", "Could not load Effect Profile {0}."),
				FText::FromString(Record.ProfilePath.ToString()));
			continue;
		}

		Record.Snapshot = MakeSnapshot(*Profile, Record.ProfilePath);
		Record.Status = EPaper2DPlusEffectLibraryLoadStatus::Loaded;
		if (!CommitSnapshot(Session.Generation, Record.Snapshot))
		{
			return MakeRefreshingResult(
				BuildProjectResult(
					Session.Libraries,
					Session.TotalLibraries,
					false,
					Session.bDiscoveryFailed,
					Session.Query,
					Session.Generation),
				Generation);
		}
	}

	return BuildProjectResult(
		Session.Libraries,
		Session.TotalLibraries,
		!Session.HasPendingLibraries(),
		Session.bDiscoveryFailed,
		Session.Query,
		Generation);
}

bool FPaper2DPlusEffectLibraryIndex::CommitSnapshot(
	uint64 ExpectedGeneration,
	FPaper2DPlusEffectProfileSnapshot Snapshot)
{
	if (!IsGenerationCurrent(ExpectedGeneration))
	{
		return false;
	}
	if (Snapshot.NormalizedProfilePath.IsEmpty())
	{
		Snapshot.NormalizedProfilePath = NormalizePath(Snapshot.ProfilePath);
	}
	if (Snapshot.NormalizedProfilePath.IsEmpty())
	{
		return false;
	}
	SnapshotsByProfilePath.Add(Snapshot.NormalizedProfilePath, MoveTemp(Snapshot));
	return true;
}

bool FPaper2DPlusEffectLibraryIndex::FindCachedSnapshot(
	const FSoftObjectPath& ProfilePath,
	FPaper2DPlusEffectProfileSnapshot& OutSnapshot) const
{
	if (const FPaper2DPlusEffectProfileSnapshot* Snapshot =
		SnapshotsByProfilePath.Find(NormalizePath(ProfilePath)))
	{
		OutSnapshot = *Snapshot;
		return true;
	}
	return false;
}

void FPaper2DPlusEffectLibraryIndex::BindAssetRegistry()
{
	IAssetRegistry& Registry =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	BoundAssetRegistry = &Registry;
	AssetAddedHandle = Registry.OnAssetAdded().AddRaw(
		this, &FPaper2DPlusEffectLibraryIndex::HandleAssetChanged);
	AssetRemovedHandle = Registry.OnAssetRemoved().AddRaw(
		this, &FPaper2DPlusEffectLibraryIndex::HandleAssetChanged);
	AssetUpdatedHandle = Registry.OnAssetUpdated().AddRaw(
		this, &FPaper2DPlusEffectLibraryIndex::HandleAssetChanged);
	AssetRenamedHandle = Registry.OnAssetRenamed().AddRaw(
		this, &FPaper2DPlusEffectLibraryIndex::HandleAssetRenamed);
}

void FPaper2DPlusEffectLibraryIndex::UnbindAssetRegistry()
{
	// The registry implementation can be replaced or torn down before this editor singleton during
	// unattended exit. Resolve the currently loaded module instead of calling through the cached raw
	// interface pointer, which may already refer to a destroyed implementation.
	if (BoundAssetRegistry && FModuleManager::Get().IsModuleLoaded(TEXT("AssetRegistry")))
	{
		IAssetRegistry& Registry = FModuleManager::GetModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
		if (AssetAddedHandle.IsValid()) Registry.OnAssetAdded().Remove(AssetAddedHandle);
		if (AssetRemovedHandle.IsValid()) Registry.OnAssetRemoved().Remove(AssetRemovedHandle);
		if (AssetUpdatedHandle.IsValid()) Registry.OnAssetUpdated().Remove(AssetUpdatedHandle);
		if (AssetRenamedHandle.IsValid()) Registry.OnAssetRenamed().Remove(AssetRenamedHandle);
	}
	AssetAddedHandle.Reset();
	AssetRemovedHandle.Reset();
	AssetUpdatedHandle.Reset();
	AssetRenamedHandle.Reset();
	BoundAssetRegistry = nullptr;
}

EPaper2DPlusEffectLibraryChangeDomain FPaper2DPlusEffectLibraryIndex::ClassifyAsset(
	const FAssetData& AssetData) const
{
	using Domain = EPaper2DPlusEffectLibraryChangeDomain;
	const FName ClassName = Paper2DPlusEffectLibraryIndexPrivate::AssetClassName(AssetData);
	if (ClassName == UPaper2DPlusCharacterCatalogAsset::StaticClass()->GetFName())
	{
		return Domain::Catalog | Domain::AssetDiscovery;
	}
	if (ClassName == UPaper2DPlusEffectProfileAsset::StaticClass()->GetFName())
	{
		return Domain::EffectProfile | Domain::AssetDiscovery;
	}
	if (ClassName == UPaperFlipbook::StaticClass()->GetFName())
	{
		return Domain::Flipbook | Domain::AssetDiscovery;
	}
	return Domain::None;
}

void FPaper2DPlusEffectLibraryIndex::HandleAssetChanged(const FAssetData& AssetData)
{
	const EPaper2DPlusEffectLibraryChangeDomain Domain = ClassifyAsset(AssetData);
	if (Domain != EPaper2DPlusEffectLibraryChangeDomain::None)
	{
		PublishChange(Domain, Paper2DPlusEffectLibraryIndexPrivate::AssetPath(AssetData));
	}
}

void FPaper2DPlusEffectLibraryIndex::HandleAssetRenamed(
	const FAssetData& AssetData,
	const FString& OldObjectPath)
{
	const EPaper2DPlusEffectLibraryChangeDomain Domain = ClassifyAsset(AssetData);
	if (Domain != EPaper2DPlusEffectLibraryChangeDomain::None)
	{
		PublishChange(
			Domain,
			Paper2DPlusEffectLibraryIndexPrivate::AssetPath(AssetData),
			FSoftObjectPath(OldObjectPath));
	}
}

void FPaper2DPlusEffectLibraryIndex::PublishChange(
	EPaper2DPlusEffectLibraryChangeDomain Domain,
	const FSoftObjectPath& Path,
	const FSoftObjectPath& PreviousPath)
{
	if (Domain == EPaper2DPlusEffectLibraryChangeDomain::None)
	{
		return;
	}
	if (!IsInGameThread())
	{
		QueueGameThreadChange(Domain, Path, PreviousPath);
		return;
	}

	PendingDomains |= Domain;
	const FString Key = NormalizePath(Path);
	if (!Key.IsEmpty()) PendingNormalizedPaths.Add(Key);
	const FString PreviousKey = NormalizePath(PreviousPath);
	if (!PreviousKey.IsEmpty()) PendingNormalizedPaths.Add(PreviousKey);

	if (!bFlushScheduled)
	{
		bFlushScheduled = true;
		++Generation;
		SnapshotsByProfilePath.Reset();
		ScheduleFlush();
	}
}

void FPaper2DPlusEffectLibraryIndex::QueueGameThreadChange(
	EPaper2DPlusEffectLibraryChangeDomain Domain,
	FSoftObjectPath Path,
	FSoftObjectPath PreviousPath)
{
	TWeakPtr<FPaper2DPlusEffectLibraryIndex> WeakSelf = AsShared();
	AsyncTask(ENamedThreads::GameThread,
		[WeakSelf, Domain, Path = MoveTemp(Path), PreviousPath = MoveTemp(PreviousPath)]()
		{
			if (const TSharedPtr<FPaper2DPlusEffectLibraryIndex> Pinned = WeakSelf.Pin())
			{
				Pinned->PublishChange(Domain, Path, PreviousPath);
			}
		});
}

void FPaper2DPlusEffectLibraryIndex::ScheduleFlush()
{
	TWeakPtr<FPaper2DPlusEffectLibraryIndex> WeakSelf = AsShared();
	AsyncTask(ENamedThreads::GameThread, [WeakSelf]()
	{
		if (const TSharedPtr<FPaper2DPlusEffectLibraryIndex> Pinned = WeakSelf.Pin())
		{
			Pinned->FlushPendingInvalidation();
		}
	});
}

bool FPaper2DPlusEffectLibraryIndex::FlushPendingInvalidationForTests()
{
	if (!bFlushScheduled || PendingDomains == EPaper2DPlusEffectLibraryChangeDomain::None)
	{
		return false;
	}
	FlushPendingInvalidation();
	return true;
}

void FPaper2DPlusEffectLibraryIndex::FlushPendingInvalidation()
{
	if (!bFlushScheduled || PendingDomains == EPaper2DPlusEffectLibraryChangeDomain::None)
	{
		bFlushScheduled = false;
		return;
	}

	FPaper2DPlusEffectLibraryChange Change;
	Change.Domains = PendingDomains;
	Change.Generation = Generation;
	Change.NormalizedPaths = PendingNormalizedPaths.Array();
	Change.NormalizedPaths.Sort();
	PendingDomains = EPaper2DPlusEffectLibraryChangeDomain::None;
	PendingNormalizedPaths.Reset();
	bFlushScheduled = false;
	Invalidated.Broadcast(Change);
}

#undef LOCTEXT_NAMESPACE
