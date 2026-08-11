// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/** Cross-thread counters shared by the cache/backend, network seam, scheduler, and dev overlay. */
struct FPaper2DPlusAppearanceExternalStats
{
	int64 CacheReservationBytes = 0;
	int32 ResidentCacheEntries = 0;
	int64 CacheHits = 0;
	int64 CacheMisses = 0;
	int64 CacheEvictions = 0;
	int64 SynchronousLoadViolations = 0;
	int64 SerializedAppearanceDescriptorBytes = 0;
	double CompositeBuildMilliseconds = 0.0;
	double CompositeWorkMillisecondsMeasuredThisFrame = 0.0;
	double CompositeWorkMillisecondsGrantedPerFrame = 0.0;
	int32 CompositeBuildUnitsClaimedThisFrame = 0;
};

namespace Paper2DPlusAppearanceStats
{
	void SetCacheReservation(int64 Bytes, int32 Entries);
	void RecordCacheHit(int64 Count = 1);
	void RecordCacheMiss(int64 Count = 1);
	void RecordCacheEviction(int64 Count = 1);
	void RecordCompositeBuildMilliseconds(double Milliseconds);
	void SetCompositeWorkFrame(
		double MeasuredMilliseconds,
		double GrantedMilliseconds,
		int32 ClaimedBuildUnits);
	void RecordSynchronousLoadViolation(int64 Count = 1);
	void RecordSerializedAppearanceDescriptorBytes(int64 Bytes);
	FPaper2DPlusAppearanceExternalStats Snapshot();

#if WITH_DEV_AUTOMATION_TESTS
	void ResetForTests();
#endif
}
