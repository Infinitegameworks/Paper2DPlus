// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UPaper2DPlusFrameCueBlueprint;

/** Outcome of finishing a brand-new Cue Type's durable staging plus its first package save. */
enum class EPaper2DPlusFrameCueTypeBirthReadyStatus : uint8
{
	/** The asset is on disk with a matching durable baseline, so discovery classifies it Ready. */
	Succeeded,
	InvalidCueType,
	/** Transient or otherwise unmountable package: there is no file to write, so nothing was staged. */
	PackageNotSaveable,
	StagingFailed,
	SaveFailed,
	/** The package saved, but the saved class still fails placement classification. */
	NotPlacementReady
};

struct FPaper2DPlusFrameCueTypeBirthReadyResult
{
	EPaper2DPlusFrameCueTypeBirthReadyStatus Status =
		EPaper2DPlusFrameCueTypeBirthReadyStatus::InvalidCueType;
	FText Error;

	bool IsSuccess() const
	{
		return Status == EPaper2DPlusFrameCueTypeBirthReadyStatus::Succeeded;
	}
};

/**
 * Makes a newly created Cue Type placement-ready at birth.
 *
 * A fresh Cue Type used to be created dirty and without the durable schema fingerprint that only the
 * restricted toolkit's protected Save stages, so it was classified NeedsDurableSave and silently
 * absent from the + Add Cue picker until the designer found and pressed that Save. This service runs
 * exactly the toolkit's protected save sequence — stage the candidate, write the package, then keep
 * or roll back the candidate based on what actually landed — outside the toolkit, so both creation
 * flows end with Availability == Ready.
 *
 * Timing matters and differs per flow, so both entry points exist:
 *  - CompleteNow is for the inline timeline flow, where the asset is named up front and the caller
 *    controls the whole sequence.
 *  - CompleteOnNextTick is for the Content Browser factory. The Content Browser only calls the
 *    factory once the inline rename commits, so the asset already carries its final name and a save
 *    leaves no redirector behind — but IAssetTools re-dirties the package immediately after
 *    FactoryCreateNew returns, and the asset editor opens in the same frame. Saving inside the
 *    factory would therefore be undone. One deferred pass lands after both.
 */
class FPaper2DPlusFrameCueTypeBirthReady
{
public:
	/** Stages the durable schema and saves the package now. Safe to call on an already-ready asset. */
	static FPaper2DPlusFrameCueTypeBirthReadyResult CompleteNow(
		UPaper2DPlusFrameCueBlueprint& CueType);

	/** Queues CompleteNow for the next core tick. Re-queuing the same asset is a no-op. */
	static void CompleteOnNextTick(UPaper2DPlusFrameCueBlueprint& CueType);

	/** Runs every queued completion immediately. Returns how many assets were still resolvable. */
	static int32 FlushPending();

	static int32 GetPendingCount();

	/** Drops the queue and any registered ticker; called from editor module shutdown. */
	static void Shutdown();

	/** Logs, and (when Slate is up) toasts, a failed completion with its repair instruction. */
	static void ReportFailure(
		const FPaper2DPlusFrameCueTypeBirthReadyResult& Result,
		const UPaper2DPlusFrameCueBlueprint* CueType);
};
