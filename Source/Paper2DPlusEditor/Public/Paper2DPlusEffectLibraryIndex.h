// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "UObject/SoftObjectPath.h"

struct FAssetData;
struct FPaper2DPlusEffectProfileContext;
class IAssetRegistry;
class UPaper2DPlusEffectProfileAsset;

/** Picker scope is transient presentation state and is never serialized on a Cue or Cue Type. */
enum class EPaper2DPlusEffectPickerScope : uint8
{
	CharacterEffects,
	AllProjectEffects
};

/** Locally knowable status for one exact Flipbook row. */
enum class EPaper2DPlusEffectRowDiagnostic : uint8
{
	Eligible,
	FilterMismatch,
	SearchMismatch,
	OutsideCharacterEffects,
	OutsideAllProjectEffects,
	MembershipUnknown
};

/** Explicit query states shared by every Effect picker host. */
enum class EPaper2DPlusEffectQueryState : uint8
{
	ResolvingContext,
	LoadingAssignedLibrary,
	Ready,
	EmptyAssignedLibrary,
	NoEligibleRows,
	NoSearchMatches,
	MissingContextOrAssignment,
	RetryableError,
	DiscoveringProject,
	PartialResults,
	EmptyProject,
	PartialFailure,
	TotalFailure,
	Refreshing
};

enum class EPaper2DPlusEffectCharacterSourceState : uint8
{
	Resolving,
	MissingContextOrAssignment,
	Ready,
	RetryableError
};

enum class EPaper2DPlusEffectLibraryLoadStatus : uint8
{
	Pending,
	Loaded,
	Failed
};

/**
 * Copied presentation/query row. The normalized Flipbook path is identity; source Profile metadata
 * is explanatory only and is never written to a Cue field.
 */
struct PAPER2DPLUSEDITOR_API FPaper2DPlusEffectLibraryRow
{
	FSoftObjectPath FlipbookPath;
	FString NormalizedFlipbookPath;
	FText DisplayName;
	FSoftObjectPath SourceProfilePath;
	int32 SourceRowIndex = INDEX_NONE;
	FGameplayTag TypeTag;
	FGameplayTagContainer DescriptorTags;
	EPaper2DPlusEffectRowDiagnostic Diagnostic = EPaper2DPlusEffectRowDiagnostic::Eligible;
	bool bEligible = true;
	bool bPinnedCurrent = false;

	bool HasIdentity() const { return !NormalizedFlipbookPath.IsEmpty(); }
};

struct PAPER2DPLUSEDITOR_API FPaper2DPlusEffectProfileSnapshot
{
	FSoftObjectPath ProfilePath;
	FString NormalizedProfilePath;
	TArray<FPaper2DPlusEffectLibraryRow> Rows;
};

struct PAPER2DPLUSEDITOR_API FPaper2DPlusEffectLibraryQuery
{
	FGameplayTag TypeFilter;
	FGameplayTagContainer DescriptorFilters;
	FString SearchText;
	FSoftObjectPath CurrentValue;
};

struct PAPER2DPLUSEDITOR_API FPaper2DPlusEffectLibraryLoadRecord
{
	FSoftObjectPath ProfilePath;
	EPaper2DPlusEffectLibraryLoadStatus Status = EPaper2DPlusEffectLibraryLoadStatus::Pending;
	FPaper2DPlusEffectProfileSnapshot Snapshot;
	FText Failure;
};

/** Per-picker staged project load. It holds copied paths/results only; the shared index owns caches. */
struct PAPER2DPLUSEDITOR_API FPaper2DPlusEffectProjectQuerySession
{
	FPaper2DPlusEffectLibraryQuery Query;
	TArray<FPaper2DPlusEffectLibraryLoadRecord> Libraries;
	uint64 Generation = 0;
	int32 TotalLibraries = 0;
	bool bDiscoveryFailed = false;

	bool HasPendingLibraries() const;
};

struct PAPER2DPLUSEDITOR_API FPaper2DPlusEffectLibraryQueryResult
{
	EPaper2DPlusEffectPickerScope Scope = EPaper2DPlusEffectPickerScope::CharacterEffects;
	EPaper2DPlusEffectQueryState State = EPaper2DPlusEffectQueryState::ResolvingContext;
	TArray<FPaper2DPlusEffectLibraryRow> Rows;
	TArray<FText> Failures;
	FText StatusText;
	int32 LoadedLibraries = 0;
	int32 TotalLibraries = 0;
	int32 FailedLibraries = 0;
	uint64 Generation = 0;
	bool bComplete = false;
	bool bCanRetry = false;
	bool bHasUsableRows = false;

	const FPaper2DPlusEffectLibraryRow* FindRow(const FSoftObjectPath& Path) const;
};

/** Domains for the one Effect-library authoring invalidation stream. */
enum class EPaper2DPlusEffectLibraryChangeDomain : uint8
{
	None = 0,
	Catalog = 1 << 0,
	EffectProfile = 1 << 1,
	Flipbook = 1 << 2,
	AssetDiscovery = 1 << 3
};
ENUM_CLASS_FLAGS(EPaper2DPlusEffectLibraryChangeDomain);

struct PAPER2DPLUSEDITOR_API FPaper2DPlusEffectLibraryChange
{
	EPaper2DPlusEffectLibraryChangeDomain Domains = EPaper2DPlusEffectLibraryChangeDomain::None;
	TArray<FString> NormalizedPaths;
	uint64 Generation = 0;

	bool Affects(EPaper2DPlusEffectLibraryChangeDomain Domain) const;
	bool ContainsPath(const FSoftObjectPath& Path) const;
};

DECLARE_MULTICAST_DELEGATE_OneParam(
	FOnPaper2DPlusEffectLibraryInvalidated,
	const FPaper2DPlusEffectLibraryChange&);

/**
 * Shared, copied-snapshot Effect Profile index.
 *
 * Construction binds lightweight registry notifications only. Effect Profile discovery and loading
 * occur exclusively through OpenCharacterQuery/OpenAllProjectQuery after a picker explicitly opens.
 * Generation checks are public so a future version-specific async loader can reject stale work
 * without creating a second change hub.
 */
class PAPER2DPLUSEDITOR_API FPaper2DPlusEffectLibraryIndex final
	: public TSharedFromThis<FPaper2DPlusEffectLibraryIndex>
{
public:
	explicit FPaper2DPlusEffectLibraryIndex(bool bBindAssetRegistry = true);
	~FPaper2DPlusEffectLibraryIndex();

	static TSharedRef<FPaper2DPlusEffectLibraryIndex> Get();
	static void Shutdown();

	static FString NormalizePath(const FSoftObjectPath& Path);
	static FPaper2DPlusEffectProfileSnapshot MakeSnapshot(
		const UPaper2DPlusEffectProfileAsset& Profile,
		const FSoftObjectPath& ProfilePath = FSoftObjectPath());

	static FPaper2DPlusEffectLibraryQueryResult BuildCharacterResult(
		EPaper2DPlusEffectCharacterSourceState SourceState,
		const FPaper2DPlusEffectProfileSnapshot* Snapshot,
		const FPaper2DPlusEffectLibraryQuery& Query,
		uint64 Generation);
	static FPaper2DPlusEffectLibraryQueryResult BuildProjectResult(
		TArray<FPaper2DPlusEffectLibraryLoadRecord> Libraries,
		int32 TotalLibraries,
		bool bDiscoveryComplete,
		bool bDiscoveryFailed,
		const FPaper2DPlusEffectLibraryQuery& Query,
		uint64 Generation);
	static FPaper2DPlusEffectLibraryQueryResult MakeRefreshingResult(
		const FPaper2DPlusEffectLibraryQueryResult& Previous,
		uint64 Generation);

	/** Explicit picker-demand boundaries. Neither is called by ordinary Details painting. */
	FPaper2DPlusEffectLibraryQueryResult OpenCharacterQuery(
		const FPaper2DPlusEffectProfileContext& Context,
		const FPaper2DPlusEffectLibraryQuery& Query);
	FPaper2DPlusEffectLibraryQueryResult OpenAllProjectQuery(
		const FPaper2DPlusEffectLibraryQuery& Query,
		bool bRetryFailedLibrariesOnly = false);
	/** Staged picker path: discovery is load-free, then each Advance call loads a bounded batch. */
	FPaper2DPlusEffectLibraryQueryResult BeginAllProjectQuery(
		const FPaper2DPlusEffectLibraryQuery& Query,
		FPaper2DPlusEffectProjectQuerySession& OutSession);
	FPaper2DPlusEffectLibraryQueryResult AdvanceAllProjectQuery(
		FPaper2DPlusEffectProjectQuerySession& Session,
		int32 MaxLibrariesToLoad = 1);

	uint64 GetGeneration() const { return Generation; }
	bool IsGenerationCurrent(uint64 Candidate) const { return Candidate == Generation; }
	bool CommitSnapshot(uint64 ExpectedGeneration, FPaper2DPlusEffectProfileSnapshot Snapshot);
	bool FindCachedSnapshot(
		const FSoftObjectPath& ProfilePath,
		FPaper2DPlusEffectProfileSnapshot& OutSnapshot) const;

	/** Production models publish only after a successful authoring commit. Bursts coalesce next tick. */
	void PublishChange(
		EPaper2DPlusEffectLibraryChangeDomain Domain,
		const FSoftObjectPath& Path = FSoftObjectPath(),
		const FSoftObjectPath& PreviousPath = FSoftObjectPath());
	FOnPaper2DPlusEffectLibraryInvalidated& OnInvalidated() { return Invalidated; }

	/** Synchronous seam for automation; returns false when no invalidation is pending. */
	bool FlushPendingInvalidationForTests();
	int32 GetDemandLoadCountForTests() const { return DemandLoadCount; }

private:
	void BindAssetRegistry();
	void UnbindAssetRegistry();
	void HandleAssetChanged(const FAssetData& AssetData);
	void HandleAssetRenamed(const FAssetData& AssetData, const FString& OldObjectPath);
	EPaper2DPlusEffectLibraryChangeDomain ClassifyAsset(const FAssetData& AssetData) const;
	void QueueGameThreadChange(
		EPaper2DPlusEffectLibraryChangeDomain Domain,
		FSoftObjectPath Path,
		FSoftObjectPath PreviousPath);
	void ScheduleFlush();
	void FlushPendingInvalidation();

	IAssetRegistry* BoundAssetRegistry = nullptr;
	FDelegateHandle AssetAddedHandle;
	FDelegateHandle AssetRemovedHandle;
	FDelegateHandle AssetUpdatedHandle;
	FDelegateHandle AssetRenamedHandle;
	TMap<FString, FPaper2DPlusEffectProfileSnapshot> SnapshotsByProfilePath;
	TSet<FString> PendingNormalizedPaths;
	EPaper2DPlusEffectLibraryChangeDomain PendingDomains = EPaper2DPlusEffectLibraryChangeDomain::None;
	uint64 Generation = 1;
	int32 DemandLoadCount = 0;
	bool bFlushScheduled = false;
	FOnPaper2DPlusEffectLibraryInvalidated Invalidated;
};
