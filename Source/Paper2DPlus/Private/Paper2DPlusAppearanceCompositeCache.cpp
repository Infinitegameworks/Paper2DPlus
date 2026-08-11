// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "Paper2DPlusAppearanceCompositeCache.h"

#include "Paper2DPlusAppearanceStats.h"
#include "Paper2DPlusSettings.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Misc/App.h"
#include "Misc/EngineVersionComparison.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

namespace Paper2DPlusAppearanceCompositeCacheCompat
{
#if UE_VERSION_OLDER_THAN(5, 4, 0)
	constexpr bool NoShrink = false;
#else
	constexpr EAllowShrinking NoShrink = EAllowShrinking::No;
#endif
}

FPaper2DPlusAppearanceCompositeCache& FPaper2DPlusAppearanceCompositeCache::Get()
{
	static FPaper2DPlusAppearanceCompositeCache Cache;
	return Cache;
}

FPaper2DPlusAppearanceCompositeCache::FPaper2DPlusAppearanceCompositeCache()
{
#if WITH_EDITOR
	ObjectModifiedHandle = FCoreUObjectDelegates::OnObjectModified.AddRaw(
		this, &FPaper2DPlusAppearanceCompositeCache::HandleObjectModified);
#endif
}

FPaper2DPlusAppearanceCompositeCache::~FPaper2DPlusAppearanceCompositeCache()
{
#if WITH_EDITOR
	if (ObjectModifiedHandle.IsValid())
	{
		FCoreUObjectDelegates::OnObjectModified.Remove(ObjectModifiedHandle);
	}
#endif
}

FPaper2DPlusAppearanceCacheAcquireResult FPaper2DPlusAppearanceCompositeCache::Acquire(
	const FPaper2DPlusAppearanceCompositeKey& Key,
	const FPaper2DPlusAppearanceBuildRecipe& Recipe)
{
	RefreshLimitsFromSettings();
	FPaper2DPlusAppearanceCacheAcquireResult Result;
	if (!Key.IsValid())
	{
		++LifetimeStats.RejectedAdmissions;
		Paper2DPlusAppearanceStats::RecordCacheMiss();
		PublishStats();
		return Result;
	}

	if (FEntry* Existing = Entries.Find(Key))
	{
		if (Existing->Resource && !Existing->Resource->bInvalidated)
		{
			Existing->LastUseSerial = ++UseSerial;
			Result.Resource = Existing->Resource;
			Result.bAdmitted = true;
			Result.bPreparedHit = Existing->Resource->IsPrepared();
			Result.bSharedPendingBuild = !Result.bPreparedHit && Existing->PendingRecipe.IsValid();
			if (Result.bPreparedHit) { ++LifetimeStats.PreparedHits; }
			if (Result.bSharedPendingBuild) { ++LifetimeStats.SharedPendingHits; }
			Paper2DPlusAppearanceStats::RecordCacheHit();
			PublishStats();
			return Result;
		}
		RemoveEntry(Key, /*bCountEviction=*/false);
	}
	Paper2DPlusAppearanceStats::RecordCacheMiss();

	const uint64 RequiredBytes = Recipe.EstimateCacheReservationBytes();
	if (RequiredBytes == 0 || RequiredBytes > MaxBytes || !MakeRoomFor(RequiredBytes))
	{
		++LifetimeStats.RejectedAdmissions;
		PublishStats();
		return Result;
	}

	UPaper2DPlusAppearanceCompositeResource* Resource = NewObject<UPaper2DPlusAppearanceCompositeResource>(
		GetTransientPackage(), NAME_None, RF_Transient | RF_DuplicateTransient);
	Resource->FrameTargets.SetNum(Recipe.Frames.Num());
	Resource->FrameReady.Init(0, Recipe.Frames.Num());
	Resource->PixelSize = Recipe.PixelSize;
	Resource->PivotPixels = Recipe.PivotPixels;
	Resource->PixelsPerUnrealUnit = Recipe.PixelsPerUnrealUnit;
	Resource->CacheReservationBytes = RequiredBytes;
	Resource->CacheIdentity = Key.DebugIdentity;
	FPaper2DPlusAppearanceRenderBackend::CollectDependencies(
		Recipe, Resource->DependencyObjects, Resource->DependencyPackages);

	FEntry NewEntry;
	NewEntry.Resource = Resource;
	NewEntry.PendingRecipe = MakeUnique<FPaper2DPlusAppearanceBuildRecipe>(Recipe);
	NewEntry.ClaimedFrames.Init(false, Recipe.Frames.Num());
	NewEntry.QueuedFrames.Init(false, Recipe.Frames.Num());
	NewEntry.LastUseSerial = ++UseSerial;
	Entries.Add(Key, MoveTemp(NewEntry));
	ResourceKeys.Add(Resource, Key);
	CacheReservedBytes += RequiredBytes;
	if (MutationSerial < MAX_uint64) { ++MutationSerial; }
	++LifetimeStats.Admissions;

	Result.Resource = Resource;
	Result.bAdmitted = true;
	PublishStats();
	return Result;
}

const FPaper2DPlusAppearanceBuildRecipe* FPaper2DPlusAppearanceCompositeCache::FindPendingRecipe(
	const UPaper2DPlusAppearanceCompositeResource* Resource) const
{
	const FEntry* Entry = FindEntry(Resource);
	return Entry && Entry->PendingRecipe ? Entry->PendingRecipe.Get() : nullptr;
}

bool FPaper2DPlusAppearanceCompositeCache::TryReserveNextUnbuiltFrame(
	UPaper2DPlusAppearanceCompositeResource* Resource,
	uint64 GameFrameNumber,
	int32& OutFrameIndex)
{
	OutFrameIndex = INDEX_NONE;
	RefreshLimitsFromSettings();
	EnsureBuildWorkFrame(GameFrameNumber);
	FEntry* Entry = FindEntry(Resource);
	if (!Entry || !Entry->PendingRecipe || !Resource || Resource->bInvalidated
		|| Entry->ClaimedFrames.Num() != Resource->GetFrameCount()
		|| Entry->QueuedFrames.Num() != Resource->GetFrameCount())
	{
		return false;
	}

	AdvanceNextUnbuiltFrame(*Entry);
	if (!Entry->ClaimedFrames.IsValidIndex(Entry->NextUnbuiltFrame))
	{
		return false;
	}
	// Do not consume a global unit while every frame is already ready/queued, and do not reserve a frame when the
	// unit/ms gate denies work. The game-thread cache owns both decisions, so no interleaving can leak a reservation.
	if (BuildUnitsClaimedThisFrame >= MaxBuildUnitsPerGameFrame
		|| BuildWorkMillisecondsThisFrame >= MaxBuildWorkMillisecondsPerGameFrame)
	{
		return false;
	}
	++BuildUnitsClaimedThisFrame;
	PublishStats();

	OutFrameIndex = Entry->NextUnbuiltFrame;
	Entry->ClaimedFrames[OutFrameIndex] = true;
	++Entry->NextUnbuiltFrame;
	AdvanceNextUnbuiltFrame(*Entry);
	return true;
}

bool FPaper2DPlusAppearanceCompositeCache::TryClaimBuildUnit(uint64 GameFrameNumber)
{
	RefreshLimitsFromSettings();
	EnsureBuildWorkFrame(GameFrameNumber);
	if (BuildUnitsClaimedThisFrame >= MaxBuildUnitsPerGameFrame
		|| BuildWorkMillisecondsThisFrame >= MaxBuildWorkMillisecondsPerGameFrame)
	{
		return false;
	}
	++BuildUnitsClaimedThisFrame;
	PublishStats();
	return true;
}

void FPaper2DPlusAppearanceCompositeCache::ReportBuildWorkMilliseconds(
	uint64 GameFrameNumber,
	double Milliseconds)
{
	RefreshLimitsFromSettings();
	EnsureBuildWorkFrame(GameFrameNumber);
	const double ClampedMilliseconds = FMath::Max(0.0, Milliseconds);
	BuildWorkMillisecondsThisFrame += ClampedMilliseconds;
	Paper2DPlusAppearanceStats::RecordCompositeBuildMilliseconds(ClampedMilliseconds);
	PublishStats();
}

bool FPaper2DPlusAppearanceCompositeCache::QueueSubmittedFrame(
	UPaper2DPlusAppearanceCompositeResource* Resource,
	int32 FrameIndex,
	const TSharedPtr<FPaper2DPlusAppearanceBuildSubmission, ESPMode::ThreadSafe>& Submission)
{
	FEntry* Entry = FindEntry(Resource);
	if (!Submission.IsValid())
	{
		return false;
	}
	if (!Entry || !Entry->PendingRecipe || !Resource || Resource->bInvalidated
		|| !Resource->FrameTargets.IsValidIndex(FrameIndex)
		|| !Entry->ClaimedFrames.IsValidIndex(FrameIndex)
		|| !Entry->QueuedFrames.IsValidIndex(FrameIndex)
		|| Resource->IsFrameReady(FrameIndex)
		|| Entry->QueuedFrames[FrameIndex])
	{
		// The render command may already be queued. Retain its target until the completion token turns over.
		RetiredSubmissions.Add(Submission);
		return false;
	}

	// QueueSubmittedFrame remains tolerant of direct backend/proof callers that did not use the cursor first. Marking
	// the frame claimed here preserves the same no-duplicate invariant, and the cursor skips it on its next advance.
	Entry->ClaimedFrames[FrameIndex] = true;
	Entry->QueuedFrames[FrameIndex] = true;
	AdvanceNextUnbuiltFrame(*Entry);
	FPendingSubmission& Pending = Entry->PendingSubmissions.AddDefaulted_GetRef();
	Pending.FrameIndex = FrameIndex;
	Pending.Submission = Submission;
	return true;
}

int32 FPaper2DPlusAppearanceCompositeCache::PollCompletedFrames()
{
	return PollCompletedFramesInternal(/*bGpuIdleProvenForTests=*/false);
}

#if WITH_EDITOR
int32 FPaper2DPlusAppearanceCompositeCache::PollCompletedFramesAfterGpuIdleForTests()
{
	return PollCompletedFramesInternal(/*bGpuIdleProvenForTests=*/true);
}
#endif

int32 FPaper2DPlusAppearanceCompositeCache::PollCompletedFramesInternal(
	bool bGpuIdleProvenForTests)
{
	RefreshLimitsFromSettings();
	EnsureBuildWorkFrame(GFrameCounter);
	PruneEvictedDependencyConsumers();
	int32 CommittedFrames = 0;
	TArray<FPaper2DPlusAppearanceCompositeKey> FailedKeys;
	for (TPair<FPaper2DPlusAppearanceCompositeKey, FEntry>& Pair : Entries)
	{
		FEntry& Entry = Pair.Value;
		for (int32 Index = Entry.PendingSubmissions.Num() - 1; Index >= 0; --Index)
		{
			const FPendingSubmission& Pending = Entry.PendingSubmissions[Index];
			const bool bComplete = Pending.Submission.IsValid()
				&& (bGpuIdleProvenForTests
					? Pending.Submission->IsRenderCommandComplete()
					: Pending.Submission->IsComplete());
			if (!bComplete)
			{
				continue;
			}

			const bool bSucceeded = bGpuIdleProvenForTests
				? Pending.Submission->DidRenderCommandSucceed()
				: Pending.Submission->DidRenderSucceed();
			UTextureRenderTarget2D* Target = Pending.Submission->GetOutputTarget();
			const int32 FrameIndex = Pending.FrameIndex;
			Entry.PendingSubmissions.RemoveAtSwap(
				Index, 1, Paper2DPlusAppearanceCompositeCacheCompat::NoShrink);
			if (bSucceeded && CommitCompletedFrame(Entry.Resource, FrameIndex, Target))
			{
				++CommittedFrames;
			}
			else
			{
				FailedKeys.AddUnique(Pair.Key);
			}
		}
	}

	for (const FPaper2DPlusAppearanceCompositeKey& Key : FailedKeys)
	{
		if (FEntry* Entry = Entries.Find(Key); Entry && Entry->Resource)
		{
			Entry->Resource->bInvalidated = true;
		}
		RemoveEntry(Key, /*bCountEviction=*/false);
	}

	RetiredSubmissions.RemoveAllSwap(
		[bGpuIdleProvenForTests](
			const TSharedPtr<FPaper2DPlusAppearanceBuildSubmission, ESPMode::ThreadSafe>& Submission)
		{
			return !Submission.IsValid()
				|| (bGpuIdleProvenForTests
					? Submission->IsRenderCommandComplete()
					: Submission->IsComplete());
		},
		Paper2DPlusAppearanceCompositeCacheCompat::NoShrink);
	PublishStats();
	return CommittedFrames;
}

bool FPaper2DPlusAppearanceCompositeCache::IsFramePendingOrReady(
	const UPaper2DPlusAppearanceCompositeResource* Resource,
	int32 FrameIndex) const
{
	if (!Resource || Resource->IsFrameReady(FrameIndex))
	{
		return Resource != nullptr;
	}
	const FEntry* Entry = FindEntry(Resource);
	return Entry && Entry->ClaimedFrames.IsValidIndex(FrameIndex)
		&& Entry->ClaimedFrames[FrameIndex];
}

bool FPaper2DPlusAppearanceCompositeCache::CommitCompletedFrame(
	UPaper2DPlusAppearanceCompositeResource* Resource,
	int32 FrameIndex,
	UTextureRenderTarget2D* Target)
{
	FEntry* Entry = FindEntry(Resource);
	if (!Entry || !Entry->PendingRecipe || !Resource || Resource->bInvalidated || !Target
		|| !Resource->FrameTargets.IsValidIndex(FrameIndex))
	{
		return false;
	}

	Resource->FrameTargets[FrameIndex] = Target;
	if (Entry->ClaimedFrames.IsValidIndex(FrameIndex))
	{
		Entry->ClaimedFrames[FrameIndex] = false;
	}
	if (Entry->QueuedFrames.IsValidIndex(FrameIndex))
	{
		Entry->QueuedFrames[FrameIndex] = false;
	}
	if (Resource->FrameReady[FrameIndex] == 0)
	{
		Resource->FrameReady[FrameIndex] = 1;
		++Resource->NumReadyFrames;
	}
	if (Resource->NumReadyFrames == Resource->FrameTargets.Num())
	{
		Entry->PendingRecipe.Reset();
		if (MutationSerial < MAX_uint64) { ++MutationSerial; }
	}
	return true;
}

void FPaper2DPlusAppearanceCompositeCache::FailBuild(UPaper2DPlusAppearanceCompositeResource* Resource)
{
	InvalidateResource(Resource);
}

void FPaper2DPlusAppearanceCompositeCache::InvalidateDependency(const UObject* ModifiedObject)
{
	if (!ModifiedObject)
	{
		return;
	}
	const FSoftObjectPath ObjectPath(ModifiedObject);
	const UPackage* Package = ModifiedObject->GetOutermost();
	const FName PackageName = Package ? Package->GetFName() : NAME_None;
	const auto MatchesDependency = [&ObjectPath, PackageName](
		const UPaper2DPlusAppearanceCompositeResource* Resource)
	{
		return Resource && (Resource->DependencyObjects.Contains(ObjectPath)
			|| (!PackageName.IsNone() && Resource->DependencyPackages.Contains(PackageName)));
	};

	TArray<FPaper2DPlusAppearanceCompositeKey> InvalidKeys;
	for (const TPair<FPaper2DPlusAppearanceCompositeKey, FEntry>& Pair : Entries)
	{
		const UPaper2DPlusAppearanceCompositeResource* Resource = Pair.Value.Resource;
		if (MatchesDependency(Resource))
		{
			InvalidKeys.Add(Pair.Key);
		}
	}
	uint64 NumInvalidated = 0;
	for (const FPaper2DPlusAppearanceCompositeKey& Key : InvalidKeys)
	{
		if (FEntry* Entry = Entries.Find(Key); Entry && Entry->Resource)
		{
			Entry->Resource->bInvalidated = true;
			++NumInvalidated;
		}
		RemoveEntry(Key, /*bCountEviction=*/false);
	}

	bool bInvalidatedEvictedConsumer = false;
	EvictedDependencyConsumers.RemoveAllSwap(
		[&MatchesDependency, &NumInvalidated, &bInvalidatedEvictedConsumer](
			const TWeakObjectPtr<UPaper2DPlusAppearanceCompositeResource>& WeakResource)
		{
			UPaper2DPlusAppearanceCompositeResource* Resource = WeakResource.Get();
			if (!Resource)
			{
				return true;
			}
			if (!MatchesDependency(Resource))
			{
				return false;
			}
			if (!Resource->bInvalidated)
			{
				Resource->bInvalidated = true;
				++NumInvalidated;
				bInvalidatedEvictedConsumer = true;
			}
			return true;
		},
		Paper2DPlusAppearanceCompositeCacheCompat::NoShrink);
	if (bInvalidatedEvictedConsumer && MutationSerial < MAX_uint64)
	{
		++MutationSerial;
	}
	LifetimeStats.Invalidations += NumInvalidated;
	PublishStats();
}

void FPaper2DPlusAppearanceCompositeCache::InvalidateResource(
	UPaper2DPlusAppearanceCompositeResource* Resource)
{
	if (!Resource)
	{
		return;
	}
	Resource->bInvalidated = true;
	bool bWasResident = false;
	if (const FPaper2DPlusAppearanceCompositeKey* MatchingKey = ResourceKeys.Find(Resource))
	{
		const FPaper2DPlusAppearanceCompositeKey KeyCopy = *MatchingKey;
		bWasResident = RemoveEntry(KeyCopy, /*bCountEviction=*/false);
	}
	bool bWasEvictedConsumer = false;
	EvictedDependencyConsumers.RemoveAllSwap(
		[Resource, &bWasEvictedConsumer](
			const TWeakObjectPtr<UPaper2DPlusAppearanceCompositeResource>& WeakResource)
		{
			UPaper2DPlusAppearanceCompositeResource* Candidate = WeakResource.Get();
			if (!Candidate)
			{
				return true;
			}
			if (Candidate == Resource)
			{
				bWasEvictedConsumer = true;
				return true;
			}
			return false;
		},
		Paper2DPlusAppearanceCompositeCacheCompat::NoShrink);
	if (bWasEvictedConsumer && !bWasResident && MutationSerial < MAX_uint64)
	{
		++MutationSerial;
	}
	if (bWasResident || bWasEvictedConsumer)
	{
		++LifetimeStats.Invalidations;
	}
	PublishStats();
}

void FPaper2DPlusAppearanceCompositeCache::InvalidateAll()
{
	uint64 NumEvictedInvalidations = 0;
	for (const TWeakObjectPtr<UPaper2DPlusAppearanceCompositeResource>& WeakResource
		: EvictedDependencyConsumers)
	{
		if (UPaper2DPlusAppearanceCompositeResource* Resource = WeakResource.Get())
		{
			if (!Resource->bInvalidated)
			{
				Resource->bInvalidated = true;
				++NumEvictedInvalidations;
			}
		}
	}
	for (TPair<FPaper2DPlusAppearanceCompositeKey, FEntry>& Pair : Entries)
	{
		if (Pair.Value.Resource)
		{
			Pair.Value.Resource->bInvalidated = true;
		}
		for (FPendingSubmission& Pending : Pair.Value.PendingSubmissions)
		{
			if (Pending.Submission.IsValid())
			{
				RetiredSubmissions.Add(MoveTemp(Pending.Submission));
			}
		}
	}
	LifetimeStats.Invalidations += static_cast<uint64>(Entries.Num()) + NumEvictedInvalidations;
	Entries.Reset();
	ResourceKeys.Reset();
	EvictedDependencyConsumers.Reset();
	CacheReservedBytes = 0;
	if (MutationSerial < MAX_uint64) { ++MutationSerial; }
	PublishStats();
}

FPaper2DPlusAppearanceCacheStats FPaper2DPlusAppearanceCompositeCache::GetStats() const
{
	FPaper2DPlusAppearanceCacheStats Result = LifetimeStats;
	Result.ResidentEntries = Entries.Num();
	Result.CacheReservedBytes = CacheReservedBytes;
	Result.BuildUnitsClaimedThisFrame = BuildUnitsClaimedThisFrame;
	Result.BuildWorkMillisecondsMeasuredThisFrame = BuildWorkMillisecondsThisFrame;
	Result.BuildWorkMillisecondsGrantedPerFrame = MaxBuildWorkMillisecondsPerGameFrame;
	for (const TPair<FPaper2DPlusAppearanceCompositeKey, FEntry>& Pair : Entries)
	{
		Result.PendingEntries += Pair.Value.PendingRecipe ? 1 : 0;
	}
	return Result;
}

#if WITH_EDITOR
FPaper2DPlusAppearanceCacheEntryDiagnostics FPaper2DPlusAppearanceCompositeCache::GetEntryDiagnosticsForTests(
	const UPaper2DPlusAppearanceCompositeResource* Resource) const
{
	FPaper2DPlusAppearanceCacheEntryDiagnostics Result;
	if (!Resource)
	{
		return Result;
	}

	Result.FrameCount = Resource->GetFrameCount();
	Result.ReadyFrames = 0;
	for (int32 FrameIndex = 0; FrameIndex < Result.FrameCount; ++FrameIndex)
	{
		Result.ReadyFrames += Resource->IsFrameReady(FrameIndex) ? 1 : 0;
	}

	const FEntry* Entry = FindEntry(Resource);
	if (!Entry)
	{
		return Result;
	}

	Result.bIndexed = true;
	Result.bHasPendingRecipe = Entry->PendingRecipe.IsValid();
	Result.PendingSubmissions = Entry->PendingSubmissions.Num();
	Result.NextUnbuiltFrame = Entry->NextUnbuiltFrame;
	for (int32 FrameIndex = 0; FrameIndex < Entry->ClaimedFrames.Num(); ++FrameIndex)
	{
		Result.ClaimedFrames += Entry->ClaimedFrames[FrameIndex] ? 1 : 0;
	}
	for (int32 FrameIndex = 0; FrameIndex < Entry->QueuedFrames.Num(); ++FrameIndex)
	{
		Result.QueuedFrames += Entry->QueuedFrames[FrameIndex] ? 1 : 0;
	}
	return Result;
}

void FPaper2DPlusAppearanceCompositeCache::SetLimitsForTests(int32 InMaxEntries, uint64 InMaxBytes)
{
	bUseTestLimits = true;
	MaxEntries = FMath::Max(1, InMaxEntries);
	MaxBytes = FMath::Max<uint64>(1, InMaxBytes);
	EnforceCurrentLimits();
}

void FPaper2DPlusAppearanceCompositeCache::SetBuildWorkLimitsForTests(
	int32 InMaxUnitsPerFrame,
	double InMaxMillisecondsPerFrame)
{
	bUseTestLimits = true;
	MaxBuildUnitsPerGameFrame = FMath::Max(0, InMaxUnitsPerFrame);
	MaxBuildWorkMillisecondsPerGameFrame = FMath::Max(0.0, InMaxMillisecondsPerFrame);
	PublishStats();
}

void FPaper2DPlusAppearanceCompositeCache::ResetForTests()
{
	PollCompletedFrames();
	InvalidateAll();
	RetiredSubmissions.RemoveAllSwap(
		[](const TSharedPtr<FPaper2DPlusAppearanceBuildSubmission, ESPMode::ThreadSafe>& Submission)
		{
			return !Submission.IsValid() || Submission->IsComplete();
		},
		Paper2DPlusAppearanceCompositeCacheCompat::NoShrink);
	MaxEntries = DefaultMaxEntries;
	MaxBytes = DefaultMaxBytes;
	MaxBuildUnitsPerGameFrame = 2;
	MaxBuildWorkMillisecondsPerGameFrame = 0.300;
	bUseTestLimits = false;
	LifetimeStats = FPaper2DPlusAppearanceCacheStats();
	UseSerial = 0;
	MutationSerial = 0;
	LastBuildWorkFrame = MAX_uint64;
	BuildUnitsClaimedThisFrame = 0;
	BuildWorkMillisecondsThisFrame = 0.0;
	PublishStats();
}

void FPaper2DPlusAppearanceCompositeCache::MarkPreparedForTests(
	UPaper2DPlusAppearanceCompositeResource* Resource)
{
	if (!Resource)
	{
		return;
	}
	for (int32 FrameIndex = 0; FrameIndex < Resource->FrameTargets.Num(); ++FrameIndex)
	{
		UTextureRenderTarget2D* Target = NewObject<UTextureRenderTarget2D>(
			Resource, NAME_None, RF_Transient | RF_DuplicateTransient);
		// Render-capable profile runs need initialized resources so they can report engine resource
		// bytes. NullRHI/worldless tests retain the lightweight logical-ready seam.
		if (FApp::CanEverRender() && Resource->PixelSize.X > 0 && Resource->PixelSize.Y > 0)
		{
			Target->RenderTargetFormat = RTF_RGBA8_SRGB;
			Target->ClearColor = FLinearColor::Transparent;
			Target->bAutoGenerateMips = false;
			Target->Filter = TF_Nearest;
			Target->InitAutoFormat(Resource->PixelSize.X, Resource->PixelSize.Y);
		}
		CommitCompletedFrame(Resource, FrameIndex, Target);
	}
}

bool FPaper2DPlusAppearanceCompositeCache::CommitFrameForTests(
	UPaper2DPlusAppearanceCompositeResource* Resource,
	int32 FrameIndex)
{
	if (!Resource || !Resource->FrameTargets.IsValidIndex(FrameIndex))
	{
		return false;
	}
	UTextureRenderTarget2D* Target = NewObject<UTextureRenderTarget2D>(
		Resource, NAME_None, RF_Transient | RF_DuplicateTransient);
	return CommitCompletedFrame(Resource, FrameIndex, Target);
}
#endif

void FPaper2DPlusAppearanceCompositeCache::AddReferencedObjects(FReferenceCollector& Collector)
{
	for (TPair<FPaper2DPlusAppearanceCompositeKey, FEntry>& Pair : Entries)
	{
		Collector.AddReferencedObject(Pair.Value.Resource);
		if (Pair.Value.PendingRecipe)
		{
			FPaper2DPlusAppearanceRenderBackend::AddReferencedObjects(Collector, *Pair.Value.PendingRecipe);
		}
	}
}

FPaper2DPlusAppearanceCompositeCache::FEntry* FPaper2DPlusAppearanceCompositeCache::FindEntry(
	UPaper2DPlusAppearanceCompositeResource* Resource)
{
	const FPaper2DPlusAppearanceCompositeKey* Key = ResourceKeys.Find(Resource);
	return Key ? Entries.Find(*Key) : nullptr;
}

const FPaper2DPlusAppearanceCompositeCache::FEntry* FPaper2DPlusAppearanceCompositeCache::FindEntry(
	const UPaper2DPlusAppearanceCompositeResource* Resource) const
{
	const FPaper2DPlusAppearanceCompositeKey* Key = ResourceKeys.Find(Resource);
	return Key ? Entries.Find(*Key) : nullptr;
}

bool FPaper2DPlusAppearanceCompositeCache::MakeRoomFor(uint64 RequiredBytes)
{
	while (Entries.Num() >= MaxEntries || CacheReservedBytes + RequiredBytes > MaxBytes)
	{
		const FEntry* Oldest = nullptr;
		const FPaper2DPlusAppearanceCompositeKey* OldestKey = nullptr;
		for (const TPair<FPaper2DPlusAppearanceCompositeKey, FEntry>& Pair : Entries)
		{
			if (Pair.Value.Resource && Pair.Value.Resource->IsPrepared()
				&& (!Oldest || Pair.Value.LastUseSerial < Oldest->LastUseSerial))
			{
				Oldest = &Pair.Value;
				OldestKey = &Pair.Key;
			}
		}
		if (!OldestKey)
		{
			return false; // pending builds keep ownership; caller stays on exact live fallback
		}
		const FPaper2DPlusAppearanceCompositeKey KeyCopy = *OldestKey;
		RemoveEntry(KeyCopy, /*bCountEviction=*/true);
	}
	return true;
}

bool FPaper2DPlusAppearanceCompositeCache::RemoveEntry(
	const FPaper2DPlusAppearanceCompositeKey& Key,
	bool bCountEviction)
{
	FEntry* Entry = Entries.Find(Key);
	if (!Entry)
	{
		return false;
	}
	if (Entry->Resource)
	{
		UPaper2DPlusAppearanceCompositeResource* RemovedResource = Entry->Resource.Get();
		CacheReservedBytes = CacheReservedBytes >= Entry->Resource->CacheReservationBytes
			? CacheReservedBytes - Entry->Resource->CacheReservationBytes : 0;
		ResourceKeys.Remove(RemovedResource);
		if (bCountEviction && RemovedResource->IsPrepared() && !RemovedResource->bInvalidated
			&& (RemovedResource->DependencyObjects.Num() > 0
				|| RemovedResource->DependencyPackages.Num() > 0))
		{
			PruneEvictedDependencyConsumers();
			EvictedDependencyConsumers.AddUnique(
				TWeakObjectPtr<UPaper2DPlusAppearanceCompositeResource>(RemovedResource));
		}
	}
	for (FPendingSubmission& Pending : Entry->PendingSubmissions)
	{
		if (Pending.Submission.IsValid())
		{
			RetiredSubmissions.Add(MoveTemp(Pending.Submission));
		}
	}
	Entries.Remove(Key);
	if (MutationSerial < MAX_uint64) { ++MutationSerial; }
	if (bCountEviction)
	{
		++LifetimeStats.Evictions;
		Paper2DPlusAppearanceStats::RecordCacheEviction();
	}
	PublishStats();
	return true;
}

void FPaper2DPlusAppearanceCompositeCache::RefreshLimitsFromSettings()
{
#if WITH_EDITOR
	if (bUseTestLimits)
	{
		return;
	}
#endif
	MaxEntries = DefaultMaxEntries;
	if (const UPaper2DPlusSettings* Settings = UPaper2DPlusSettings::Get())
	{
		MaxBytes = static_cast<uint64>(FMath::Max<int64>(0, Settings->AppearanceTransientCacheBytes));
		MaxBuildUnitsPerGameFrame = FMath::Max(0, Settings->AppearanceCompositeBuildUnitsPerFrame);
		MaxBuildWorkMillisecondsPerGameFrame =
			FMath::Max(0.0, static_cast<double>(Settings->AppearanceCompositeWorkBudgetMs));
	}
	else
	{
		MaxBytes = DefaultMaxBytes;
		MaxBuildUnitsPerGameFrame = 2;
		MaxBuildWorkMillisecondsPerGameFrame = 0.300;
	}
	EnforceCurrentLimits();
}

void FPaper2DPlusAppearanceCompositeCache::EnsureBuildWorkFrame(uint64 GameFrameNumber)
{
	if (GameFrameNumber == LastBuildWorkFrame)
	{
		return;
	}
	LastBuildWorkFrame = GameFrameNumber;
	BuildUnitsClaimedThisFrame = 0;
	BuildWorkMillisecondsThisFrame = 0.0;
	PublishStats();
}

void FPaper2DPlusAppearanceCompositeCache::EnforceCurrentLimits()
{
	while (Entries.Num() > MaxEntries || CacheReservedBytes > MaxBytes)
	{
		const FEntry* OldestPrepared = nullptr;
		const FEntry* OldestPending = nullptr;
		const FPaper2DPlusAppearanceCompositeKey* OldestPreparedKey = nullptr;
		const FPaper2DPlusAppearanceCompositeKey* OldestPendingKey = nullptr;
		for (const TPair<FPaper2DPlusAppearanceCompositeKey, FEntry>& Pair : Entries)
		{
			const FEntry& Candidate = Pair.Value;
			if (Candidate.Resource && Candidate.Resource->IsPrepared())
			{
				if (!OldestPrepared || Candidate.LastUseSerial < OldestPrepared->LastUseSerial)
				{
					OldestPrepared = &Candidate;
					OldestPreparedKey = &Pair.Key;
				}
			}
			else if (!OldestPending || Candidate.LastUseSerial < OldestPending->LastUseSerial)
			{
				OldestPending = &Candidate;
				OldestPendingKey = &Pair.Key;
			}
		}

		const FPaper2DPlusAppearanceCompositeKey* VictimKey = OldestPreparedKey
			? OldestPreparedKey : OldestPendingKey;
		if (!VictimKey)
		{
			break;
		}
		const FPaper2DPlusAppearanceCompositeKey KeyCopy = *VictimKey;
		FEntry* Victim = Entries.Find(KeyCopy);
		if (!OldestPrepared && Victim && Victim->Resource)
		{
			// A lowered hard byte cap cancels oldest pending work only after every reusable prepared entry is gone.
			// Mark it invalid before retiring queued submissions so strong consumers fail back to exact live art.
			Victim->Resource->bInvalidated = true;
		}
		RemoveEntry(KeyCopy, /*bCountEviction=*/true);
	}
}

void FPaper2DPlusAppearanceCompositeCache::PruneEvictedDependencyConsumers()
{
	EvictedDependencyConsumers.RemoveAllSwap(
		[](const TWeakObjectPtr<UPaper2DPlusAppearanceCompositeResource>& WeakResource)
		{
			return !WeakResource.IsValid();
		},
		Paper2DPlusAppearanceCompositeCacheCompat::NoShrink);
}

void FPaper2DPlusAppearanceCompositeCache::AdvanceNextUnbuiltFrame(FEntry& Entry)
{
	const UPaper2DPlusAppearanceCompositeResource* Resource = Entry.Resource;
	while (Resource && Entry.ClaimedFrames.IsValidIndex(Entry.NextUnbuiltFrame)
		&& (Entry.ClaimedFrames[Entry.NextUnbuiltFrame]
			|| Resource->IsFrameReady(Entry.NextUnbuiltFrame)))
	{
		++Entry.NextUnbuiltFrame;
	}
}

void FPaper2DPlusAppearanceCompositeCache::PublishStats() const
{
	Paper2DPlusAppearanceStats::SetCacheReservation(
		static_cast<int64>(FMath::Min<uint64>(CacheReservedBytes, static_cast<uint64>(MAX_int64))),
		Entries.Num());
	Paper2DPlusAppearanceStats::SetCompositeWorkFrame(
		BuildWorkMillisecondsThisFrame,
		MaxBuildWorkMillisecondsPerGameFrame,
		BuildUnitsClaimedThisFrame);
}

void FPaper2DPlusAppearanceCompositeCache::HandleObjectModified(UObject* ModifiedObject)
{
	InvalidateDependency(ModifiedObject);
}
