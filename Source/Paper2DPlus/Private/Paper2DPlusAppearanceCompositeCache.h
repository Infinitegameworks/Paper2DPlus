// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Paper2DPlusAppearanceRenderBackend.h"
#include "Paper2DPlusAppearanceRenderPolicy.h"
#include "UObject/GCObject.h"

class UTextureRenderTarget2D;

struct FPaper2DPlusAppearanceCacheAcquireResult
{
	TObjectPtr<UPaper2DPlusAppearanceCompositeResource> Resource = nullptr;
	bool bAdmitted = false;
	bool bPreparedHit = false;
	bool bSharedPendingBuild = false;
};

struct FPaper2DPlusAppearanceCacheStats
{
	int32 ResidentEntries = 0;
	int32 PendingEntries = 0;
	/** Admission reservation rooted by cache ownership; evicted resources can remain alive through consumers. */
	uint64 CacheReservedBytes = 0;
	uint64 Admissions = 0;
	uint64 PreparedHits = 0;
	uint64 SharedPendingHits = 0;
	uint64 Evictions = 0;
	uint64 Invalidations = 0;
	uint64 RejectedAdmissions = 0;
	int32 BuildUnitsClaimedThisFrame = 0;
	double BuildWorkMillisecondsMeasuredThisFrame = 0.0;
	double BuildWorkMillisecondsGrantedPerFrame = 0.0;
};

#if WITH_EDITOR
/** Focused, read-only state for diagnosing one production cache entry from automation. */
struct FPaper2DPlusAppearanceCacheEntryDiagnostics
{
	bool bIndexed = false;
	bool bHasPendingRecipe = false;
	int32 FrameCount = 0;
	int32 ReadyFrames = 0;
	int32 ClaimedFrames = 0;
	int32 QueuedFrames = 0;
	int32 PendingSubmissions = 0;
	int32 NextUnbuiltFrame = INDEX_NONE;
};
#endif

/**
 * Process-local, transient strong cache for prepared base composites. It owns pending recipes and resources while
 * resident; a component's UPROPERTY remains the strong consumer after LRU eviction. Pending entries are never
 * evicted mid-build. When pending work occupies the budget, new admissions fail closed to exact live rendering.
 */
struct FPaper2DPlusAppearanceCompositeCache final : FGCObject
{
	/** Production residency is byte-budgeted; an entry-count limit exists only as a focused-test seam. */
	static constexpr int32 DefaultMaxEntries = MAX_int32;
	static constexpr uint64 DefaultMaxBytes = 128ull * 1024ull * 1024ull;

	static FPaper2DPlusAppearanceCompositeCache& Get();

	FPaper2DPlusAppearanceCompositeCache();
	virtual ~FPaper2DPlusAppearanceCompositeCache() override;

	FPaper2DPlusAppearanceCacheAcquireResult Acquire(
		const FPaper2DPlusAppearanceCompositeKey& Key,
		const FPaper2DPlusAppearanceBuildRecipe& Recipe);

	const FPaper2DPlusAppearanceBuildRecipe* FindPendingRecipe(
		const UPaper2DPlusAppearanceCompositeResource* Resource) const;

	/**
	 * Atomically claim one global build unit and the next frame never claimed for this pending resource. The per-entry
	 * cursor advances monotonically, so shared requesters do not restart at frame zero or submit the same frame twice.
	 */
	bool TryReserveNextUnbuiltFrame(
		UPaper2DPlusAppearanceCompositeResource* Resource,
		uint64 GameFrameNumber,
		int32& OutFrameIndex);

	/** One globally bounded composite frame may be built for each claimed unit while BOTH unit/ms budgets remain. */
	bool TryClaimBuildUnit(uint64 GameFrameNumber);
	/** Report actual game-thread preparation/submission time immediately after the claimed attempt returns. */
	void ReportBuildWorkMilliseconds(uint64 GameFrameNumber, double Milliseconds);

	/**
	 * Retain a queued GPU submission. Queuing never marks the frame ready; PollCompletedFrames is the only
	 * production commit path and is non-blocking.
	 */
	bool QueueSubmittedFrame(
		UPaper2DPlusAppearanceCompositeResource* Resource,
		int32 FrameIndex,
		const TSharedPtr<FPaper2DPlusAppearanceBuildSubmission, ESPMode::ThreadSafe>& Submission);

	/** Poll all render completion tokens without flushing. Returns the number of newly committed frames. */
	int32 PollCompletedFrames();

	bool IsFramePendingOrReady(
		const UPaper2DPlusAppearanceCompositeResource* Resource,
		int32 FrameIndex) const;

	/** Failed/unsupported builds are removed and marked invalid so every strong consumer returns to live fallback. */
	void FailBuild(UPaper2DPlusAppearanceCompositeResource* Resource);

	void InvalidateDependency(const UObject* ModifiedObject);
	void InvalidateResource(UPaper2DPlusAppearanceCompositeResource* Resource);
	void InvalidateAll();

	FPaper2DPlusAppearanceCacheStats GetStats() const;
	/** Monotonic admission/readiness/eviction serial used for event-driven cache-pressure retries. */
	uint64 GetMutationSerial() const { return MutationSerial; }

#if WITH_EDITOR
	void SetLimitsForTests(int32 InMaxEntries, uint64 InMaxBytes);
	void SetBuildWorkLimitsForTests(int32 InMaxUnitsPerFrame, double InMaxMillisecondsPerFrame);
	void ResetForTests();
	/** Caller must first block until the GPU is idle and flush render commands. Production remains fence-polled. */
	int32 PollCompletedFramesAfterGpuIdleForTests();
	void MarkPreparedForTests(UPaper2DPlusAppearanceCompositeResource* Resource);
	bool CommitFrameForTests(UPaper2DPlusAppearanceCompositeResource* Resource, int32 FrameIndex);
	FPaper2DPlusAppearanceCacheEntryDiagnostics GetEntryDiagnosticsForTests(
		const UPaper2DPlusAppearanceCompositeResource* Resource) const;
	int32 GetIndexedResourceCountForTests() const { return ResourceKeys.Num(); }
	bool HasIndexedResourceForTests(const UPaper2DPlusAppearanceCompositeResource* Resource) const
	{
		return ResourceKeys.Contains(Resource);
	}
	int32 GetEvictedDependencyConsumerCountForTests() const
	{
		return EvictedDependencyConsumers.Num();
	}
#endif

	// FGCObject: roots resident resources and every UObject in a pending recipe; eviction tombstones stay weak.
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override
	{
		return TEXT("FPaper2DPlusAppearanceCompositeCache");
	}

private:
	struct FPendingSubmission
	{
		int32 FrameIndex = INDEX_NONE;
		TSharedPtr<FPaper2DPlusAppearanceBuildSubmission, ESPMode::ThreadSafe> Submission;
	};

	struct FEntry
	{
		TObjectPtr<UPaper2DPlusAppearanceCompositeResource> Resource = nullptr;
		TUniquePtr<FPaper2DPlusAppearanceBuildRecipe> PendingRecipe;
		TArray<FPendingSubmission> PendingSubmissions;
		/** Claimed includes the short reservation window before a render submission is retained. */
		TBitArray<> ClaimedFrames;
		/** Queued distinguishes an accepted submission from a reservation; duplicate queues fail closed. */
		TBitArray<> QueuedFrames;
		/** Monotonic first-never-claimed cursor. Claims only become ready or invalidate the whole entry. */
		int32 NextUnbuiltFrame = 0;
		uint64 LastUseSerial = 0;
	};

	FEntry* FindEntry(UPaper2DPlusAppearanceCompositeResource* Resource);
	const FEntry* FindEntry(const UPaper2DPlusAppearanceCompositeResource* Resource) const;
	bool MakeRoomFor(uint64 RequiredBytes);
	bool RemoveEntry(const FPaper2DPlusAppearanceCompositeKey& Key, bool bCountEviction);
	bool CommitCompletedFrame(
		UPaper2DPlusAppearanceCompositeResource* Resource,
		int32 FrameIndex,
		UTextureRenderTarget2D* Target);
	int32 PollCompletedFramesInternal(bool bGpuIdleProvenForTests);
	void RefreshLimitsFromSettings();
	void EnsureBuildWorkFrame(uint64 GameFrameNumber);
	void EnforceCurrentLimits();
	void PruneEvictedDependencyConsumers();
	void PublishStats() const;
	void HandleObjectModified(UObject* ModifiedObject);
	static void AdvanceNextUnbuiltFrame(FEntry& Entry);

	TMap<FPaper2DPlusAppearanceCompositeKey, FEntry> Entries;
	/** Resource identity -> cache key. Keeps all resource-based hot paths O(1) without storing unstable map-node pointers. */
	TMap<const UPaper2DPlusAppearanceCompositeResource*, FPaper2DPlusAppearanceCompositeKey> ResourceKeys;
	/**
	 * Prepared resources can outlive LRU residency through a component's UPROPERTY. Keep only weak tombstones so
	 * editor dependency edits can invalidate those consumers without charging cache bytes or rooting any resource.
	 */
	TArray<TWeakObjectPtr<UPaper2DPlusAppearanceCompositeResource>> EvictedDependencyConsumers;
	int32 MaxEntries = DefaultMaxEntries;
	uint64 MaxBytes = DefaultMaxBytes;
	uint64 CacheReservedBytes = 0;
	uint64 UseSerial = 0;
	uint64 MutationSerial = 0;
	uint64 LastBuildWorkFrame = MAX_uint64;
	int32 BuildUnitsClaimedThisFrame = 0;
	int32 MaxBuildUnitsPerGameFrame = 2;
	double BuildWorkMillisecondsThisFrame = 0.0;
	double MaxBuildWorkMillisecondsPerGameFrame = 0.300;
	FPaper2DPlusAppearanceCacheStats LifetimeStats;
	/** Invalidated submissions remain strongly owned until their render command reports completion. */
	TArray<TSharedPtr<FPaper2DPlusAppearanceBuildSubmission, ESPMode::ThreadSafe>> RetiredSubmissions;
#if WITH_EDITOR
	FDelegateHandle ObjectModifiedHandle;
	bool bUseTestLimits = false;
#endif
};
