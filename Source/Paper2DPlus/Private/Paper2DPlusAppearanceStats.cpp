// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "Paper2DPlusAppearanceStats.h"

#include "Misc/ScopeLock.h"

namespace
{
	FCriticalSection GPaper2DPlusAppearanceStatsMutex;
	FPaper2DPlusAppearanceExternalStats GPaper2DPlusAppearanceStats;
}

void Paper2DPlusAppearanceStats::SetCacheReservation(int64 Bytes, int32 Entries)
{
	FScopeLock Lock(&GPaper2DPlusAppearanceStatsMutex);
	GPaper2DPlusAppearanceStats.CacheReservationBytes = FMath::Max<int64>(0, Bytes);
	GPaper2DPlusAppearanceStats.ResidentCacheEntries = FMath::Max(0, Entries);
}

void Paper2DPlusAppearanceStats::RecordCacheHit(int64 Count)
{
	FScopeLock Lock(&GPaper2DPlusAppearanceStatsMutex);
	GPaper2DPlusAppearanceStats.CacheHits += FMath::Max<int64>(0, Count);
}

void Paper2DPlusAppearanceStats::RecordCacheMiss(int64 Count)
{
	FScopeLock Lock(&GPaper2DPlusAppearanceStatsMutex);
	GPaper2DPlusAppearanceStats.CacheMisses += FMath::Max<int64>(0, Count);
}

void Paper2DPlusAppearanceStats::RecordCacheEviction(int64 Count)
{
	FScopeLock Lock(&GPaper2DPlusAppearanceStatsMutex);
	GPaper2DPlusAppearanceStats.CacheEvictions += FMath::Max<int64>(0, Count);
}

void Paper2DPlusAppearanceStats::RecordCompositeBuildMilliseconds(double Milliseconds)
{
	FScopeLock Lock(&GPaper2DPlusAppearanceStatsMutex);
	GPaper2DPlusAppearanceStats.CompositeBuildMilliseconds += FMath::Max(0.0, Milliseconds);
}

void Paper2DPlusAppearanceStats::SetCompositeWorkFrame(
	double MeasuredMilliseconds,
	double GrantedMilliseconds,
	int32 ClaimedBuildUnits)
{
	FScopeLock Lock(&GPaper2DPlusAppearanceStatsMutex);
	GPaper2DPlusAppearanceStats.CompositeWorkMillisecondsMeasuredThisFrame =
		FMath::Max(0.0, MeasuredMilliseconds);
	GPaper2DPlusAppearanceStats.CompositeWorkMillisecondsGrantedPerFrame =
		FMath::Max(0.0, GrantedMilliseconds);
	GPaper2DPlusAppearanceStats.CompositeBuildUnitsClaimedThisFrame = FMath::Max(0, ClaimedBuildUnits);
}

void Paper2DPlusAppearanceStats::RecordSynchronousLoadViolation(int64 Count)
{
	FScopeLock Lock(&GPaper2DPlusAppearanceStatsMutex);
	GPaper2DPlusAppearanceStats.SynchronousLoadViolations += FMath::Max<int64>(0, Count);
}

void Paper2DPlusAppearanceStats::RecordSerializedAppearanceDescriptorBytes(int64 Bytes)
{
	FScopeLock Lock(&GPaper2DPlusAppearanceStatsMutex);
	GPaper2DPlusAppearanceStats.SerializedAppearanceDescriptorBytes += FMath::Max<int64>(0, Bytes);
}

FPaper2DPlusAppearanceExternalStats Paper2DPlusAppearanceStats::Snapshot()
{
	FScopeLock Lock(&GPaper2DPlusAppearanceStatsMutex);
	return GPaper2DPlusAppearanceStats;
}

#if WITH_DEV_AUTOMATION_TESTS
void Paper2DPlusAppearanceStats::ResetForTests()
{
	FScopeLock Lock(&GPaper2DPlusAppearanceStatsMutex);
	GPaper2DPlusAppearanceStats = FPaper2DPlusAppearanceExternalStats();
}
#endif
