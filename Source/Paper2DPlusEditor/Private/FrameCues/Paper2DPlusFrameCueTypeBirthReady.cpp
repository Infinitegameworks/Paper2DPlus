// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "FrameCues/Paper2DPlusFrameCueTypeBirthReady.h"

#include "Containers/Ticker.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Notifications/NotificationManager.h"
#include "FrameCues/Paper2DPlusFrameCueBlueprint.h"
#include "FrameCues/Paper2DPlusFrameCueTypeAuthoring.h"
#include "FrameCues/Paper2DPlusFrameCueTypeEditor.h"
#include "Misc/PackageName.h"
#include "Paper2DPlusEditorModule.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/UObjectGlobals.h"
#include "Widgets/Notifications/SNotificationList.h"

#define LOCTEXT_NAMESPACE "Paper2DPlusFrameCueTypeBirthReady"

namespace Paper2DPlusFrameCueTypeBirthReadyInternal
{
	/**
	 * Assets whose first protected save is owed. Weak on purpose: an inline rename that is cancelled
	 * (or any other creation rollback) destroys the asset before this queue drains, and the queued
	 * entry must then evaporate rather than resurrect or crash.
	 */
	TArray<TWeakObjectPtr<UPaper2DPlusFrameCueBlueprint>> PendingCueTypes;
	FTSTicker::FDelegateHandle PendingTickHandle;

	bool TickPendingCompletions(float /*DeltaTime*/)
	{
		PendingTickHandle.Reset();
		FPaper2DPlusFrameCueTypeBirthReady::FlushPending();
		// One-shot: returning false unregisters this ticker, so nothing stays registered between
		// creations and no permanent per-frame cost is introduced by a creation-time feature.
		return false;
	}

	/** Resolves the on-disk file this package would be written to, or false when it has none. */
	bool ResolvePackageFilename(const UPackage& Package, FString& OutFilename)
	{
		OutFilename.Reset();
		if (&Package == GetTransientPackage())
		{
			return false;
		}
		const FString PackageName = Package.GetName();
		if (!FPackageName::IsValidLongPackageName(PackageName))
		{
			return false;
		}
		return FPackageName::TryConvertLongPackageNameToFilename(
			PackageName,
			OutFilename,
			FPackageName::GetAssetPackageExtension());
	}
}

FPaper2DPlusFrameCueTypeBirthReadyResult FPaper2DPlusFrameCueTypeBirthReady::CompleteNow(
	UPaper2DPlusFrameCueBlueprint& CueType)
{
	using namespace Paper2DPlusFrameCueTypeBirthReadyInternal;
	FPaper2DPlusFrameCueTypeBirthReadyResult Result;

	if (!IsValid(&CueType) || !CueType.GeneratedClass)
	{
		Result.Status = EPaper2DPlusFrameCueTypeBirthReadyStatus::InvalidCueType;
		Result.Error = LOCTEXT(
			"BirthReadyInvalidCueType",
			"The new Cue Type is no longer a valid compiled asset, so it could not be prepared for placement.");
		return Result;
	}

	UPackage* Package = CueType.GetOutermost();
	FString Filename;
	if (!Package || !ResolvePackageFilename(*Package, Filename))
	{
		Result.Status = EPaper2DPlusFrameCueTypeBirthReadyStatus::PackageNotSaveable;
		Result.Error = LOCTEXT(
			"BirthReadyPackageNotSaveable",
			"The new Cue Type does not live in a saveable content package, so it cannot be made placement-ready.");
		return Result;
	}

	// Idempotent by design: the deferred queue can be flushed twice (a test flush plus the ticker),
	// and re-staging a clean, already-ready asset would only dirty it again for no benefit.
	if (FPaper2DPlusFrameCueTypeEditor::IsDurablyReadyForPlacement(CueType))
	{
		Result.Status = EPaper2DPlusFrameCueTypeBirthReadyStatus::Succeeded;
		return Result;
	}

	FPaper2DPlusFrameCueDurableSaveAttempt Attempt;
	FText Error;
	if (!FPaper2DPlusFrameCueTypeEditor::PrepareDurableSchemaSaveAttempt(CueType, Attempt, &Error))
	{
		Result.Status = EPaper2DPlusFrameCueTypeBirthReadyStatus::StagingFailed;
		Result.Error = Error.IsEmpty()
			? LOCTEXT(
				"BirthReadyStagingFailed",
				"The new Cue Type's durable schema could not be staged for its first save.")
			: Error;
		return Result;
	}

	// A brand-new asset has no file yet, so there is nothing for revision control to check out and
	// no prompt worth showing. Everything else about the sequence — stage, write, then keep or roll
	// back the staged candidate — is exactly the restricted toolkit's protected save.
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_NoError;
	const bool bSaved = UPackage::SavePackage(Package, &CueType, *Filename, SaveArgs);

	if (!FPaper2DPlusFrameCueTypeEditor::FinalizeDurableSchemaSaveAttempt(CueType, Attempt, &Error))
	{
		Result.Status = bSaved
			? EPaper2DPlusFrameCueTypeBirthReadyStatus::NotPlacementReady
			: EPaper2DPlusFrameCueTypeBirthReadyStatus::SaveFailed;
		Result.Error = Error.IsEmpty()
			? LOCTEXT(
				"BirthReadySaveFailed",
				"The new Cue Type could not complete its first durable save.")
			: Error;
		return Result;
	}

	Result.Status = EPaper2DPlusFrameCueTypeBirthReadyStatus::Succeeded;
	Result.Error = FText::GetEmpty();
	return Result;
}

void FPaper2DPlusFrameCueTypeBirthReady::CompleteOnNextTick(
	UPaper2DPlusFrameCueBlueprint& CueType)
{
	using namespace Paper2DPlusFrameCueTypeBirthReadyInternal;
	PendingCueTypes.AddUnique(TWeakObjectPtr<UPaper2DPlusFrameCueBlueprint>(&CueType));
	if (!PendingTickHandle.IsValid())
	{
		PendingTickHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateStatic(&TickPendingCompletions),
			0.0f);
	}
}

int32 FPaper2DPlusFrameCueTypeBirthReady::FlushPending()
{
	using namespace Paper2DPlusFrameCueTypeBirthReadyInternal;
	TArray<TWeakObjectPtr<UPaper2DPlusFrameCueBlueprint>> Draining;
	// Drain into a local first: a completion runs a package save, which can re-enter this module.
	Swap(Draining, PendingCueTypes);

	int32 Completed = 0;
	for (const TWeakObjectPtr<UPaper2DPlusFrameCueBlueprint>& Pending : Draining)
	{
		UPaper2DPlusFrameCueBlueprint* CueType = Pending.Get();
		if (!CueType)
		{
			// Creation was cancelled or rolled back before this pass. Nothing was ever written, so
			// there is nothing to save and nothing to report.
			continue;
		}
		++Completed;
		const FPaper2DPlusFrameCueTypeBirthReadyResult Result = CompleteNow(*CueType);
		if (!Result.IsSuccess())
		{
			ReportFailure(Result, CueType);
		}
	}
	return Completed;
}

int32 FPaper2DPlusFrameCueTypeBirthReady::GetPendingCount()
{
	return Paper2DPlusFrameCueTypeBirthReadyInternal::PendingCueTypes.Num();
}

void FPaper2DPlusFrameCueTypeBirthReady::Shutdown()
{
	using namespace Paper2DPlusFrameCueTypeBirthReadyInternal;
	if (PendingTickHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(PendingTickHandle);
		PendingTickHandle.Reset();
	}
	PendingCueTypes.Reset();
}

void FPaper2DPlusFrameCueTypeBirthReady::ReportFailure(
	const FPaper2DPlusFrameCueTypeBirthReadyResult& Result,
	const UPaper2DPlusFrameCueBlueprint* CueType)
{
	if (Result.IsSuccess())
	{
		return;
	}

	const FText Message = FText::Format(
		LOCTEXT(
			"BirthReadyFailedNotification",
			"'{0}' was created but is not ready to place yet. Open it in the Frame Cue Type editor and save it there.\n\n{1}"),
		FText::FromString(GetNameSafe(CueType)),
		Result.Error);

	UE_LOG(
		LogPaper2DPlusEditor,
		Warning,
		TEXT("Frame Cue Type '%s' was created but did not become placement-ready: %s"),
		*GetNameSafe(CueType),
		*Result.Error.ToString());

	if (!FSlateApplication::IsInitialized())
	{
		return;
	}
	FNotificationInfo Info(Message);
	Info.bFireAndForget = true;
	Info.bUseLargeFont = false;
	Info.bUseSuccessFailIcons = true;
	Info.ExpireDuration = 8.0f;
	if (const TSharedPtr<SNotificationItem> Notification =
		FSlateNotificationManager::Get().AddNotification(Info))
	{
		Notification->SetCompletionState(SNotificationItem::CS_Fail);
	}
}

#undef LOCTEXT_NAMESPACE
