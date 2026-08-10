// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "FrameCues/Paper2DPlusFrameCuePlacementAuthoring.h"

#include "Engine/Engine.h"
#include "FrameCues/Paper2DPlusFrameCueTypeAuthoring.h"

TSharedRef<FPaper2DPlusFrameCuePendingPlacement> FPaper2DPlusFrameCuePendingPlacement::Create(
	const FPaper2DPlusFrameCuePlacementTarget& Target,
	EPaper2DPlusFrameCuePlacementKind Kind,
	bool bReadyToPlace,
	FPaper2DPlusFrameCuePlacementCompletion Completion)
{
	return MakeShared<FPaper2DPlusFrameCuePendingPlacement>(
		Target, Kind, bReadyToPlace, MoveTemp(Completion));
}

FPaper2DPlusFrameCuePendingPlacement::FPaper2DPlusFrameCuePendingPlacement(
	const FPaper2DPlusFrameCuePlacementTarget& InTarget,
	EPaper2DPlusFrameCuePlacementKind InKind,
	bool bReadyToPlace,
	FPaper2DPlusFrameCuePlacementCompletion InCompletion)
	: Target(InTarget)
	, Kind(InKind)
	, State(bReadyToPlace
		? EPaper2DPlusFrameCuePlacementRequestState::ReadyToPlace
		: EPaper2DPlusFrameCuePlacementRequestState::EditingType)
	, Completion(MoveTemp(InCompletion))
{
}

bool FPaper2DPlusFrameCuePendingPlacement::MarkReadyToPlace()
{
	if (State != EPaper2DPlusFrameCuePlacementRequestState::EditingType)
	{
		return false;
	}
	State = EPaper2DPlusFrameCuePlacementRequestState::ReadyToPlace;
	return true;
}

FPaper2DPlusFrameCuePlacementResult
FPaper2DPlusFrameCuePendingPlacement::ConsumeWithoutPlacement()
{
	FPaper2DPlusFrameCuePlacementResult Result;
	if (State == EPaper2DPlusFrameCuePlacementRequestState::PlacedConsumed
		|| State == EPaper2DPlusFrameCuePlacementRequestState::RetainedTypeConsumed)
	{
		Result.Status = EPaper2DPlusFrameCuePlacementStatus::AlreadyConsumed;
		Result.State = State;
		return Result;
	}
	if (State == EPaper2DPlusFrameCuePlacementRequestState::CommitInProgress)
	{
		Result.Status = EPaper2DPlusFrameCuePlacementStatus::CommitInProgress;
		Result.State = State;
		return Result;
	}

	State = EPaper2DPlusFrameCuePlacementRequestState::RetainedTypeConsumed;
	Completion = FPaper2DPlusFrameCuePlacementCompletion();
	Result.Status = EPaper2DPlusFrameCuePlacementStatus::RetainedTypeWithoutPlacement;
	Result.State = State;
	return Result;
}

FPaper2DPlusFrameCuePlacementResult FPaper2DPlusFrameCuePendingPlacement::Commit(UClass* CueClass)
{
	FPaper2DPlusFrameCuePlacementResult Result;
	Result.State = State;
	if (State == EPaper2DPlusFrameCuePlacementRequestState::PlacedConsumed
		|| State == EPaper2DPlusFrameCuePlacementRequestState::RetainedTypeConsumed)
	{
		Result.Status = EPaper2DPlusFrameCuePlacementStatus::AlreadyConsumed;
		return Result;
	}
	if (State == EPaper2DPlusFrameCuePlacementRequestState::CommitInProgress)
	{
		Result.Status = EPaper2DPlusFrameCuePlacementStatus::CommitInProgress;
		return Result;
	}
	if (State == EPaper2DPlusFrameCuePlacementRequestState::EditingType)
	{
		Result.Status = EPaper2DPlusFrameCuePlacementStatus::NotReadyToPlace;
		return Result;
	}
	if (!Target.IsValid())
	{
		State = EPaper2DPlusFrameCuePlacementRequestState::RetainedTypeConsumed;
		Completion = FPaper2DPlusFrameCuePlacementCompletion();
		Result.Status = EPaper2DPlusFrameCuePlacementStatus::InvalidRequest;
		Result.State = State;
		return Result;
	}
	if (!Paper2DPlusFrameCuePlacementAuthoring::IsCueClassCompatible(CueClass, Kind))
	{
		Result.Status = EPaper2DPlusFrameCuePlacementStatus::InvalidCueClass;
		return Result;
	}

	State = EPaper2DPlusFrameCuePlacementRequestState::CommitInProgress;
#if WITH_DEV_AUTOMATION_TESTS
	const EPaper2DPlusFrameCuePlacementFailurePoint FailurePoint = FailurePointForTests;
#else
	const EPaper2DPlusFrameCuePlacementFailurePoint FailurePoint =
		EPaper2DPlusFrameCuePlacementFailurePoint::None;
#endif
	const FPaper2DPlusFrameCuePlacementAppendResult AppendResult =
		Target.ProviderSnapshot->AppendPlacementAtomically(
			CueClass, Kind, Target.CapturedFrame, Target.CapturedTrackId, FailurePoint);

	switch (AppendResult.Status)
	{
	case EPaper2DPlusFrameCuePlacementAppendStatus::Success:
		State = EPaper2DPlusFrameCuePlacementRequestState::PlacedConsumed;
		Result.Status = EPaper2DPlusFrameCuePlacementStatus::Success;
		Result.PlacementIdentity = AppendResult.PlacementIdentity;
		break;
	case EPaper2DPlusFrameCuePlacementAppendStatus::RolledBack:
		State = EPaper2DPlusFrameCuePlacementRequestState::RetryableAfterRollback;
		Result.Status = EPaper2DPlusFrameCuePlacementStatus::PlacementRolledBack;
		break;
	case EPaper2DPlusFrameCuePlacementAppendStatus::InvalidCueClass:
		State = EPaper2DPlusFrameCuePlacementRequestState::ReadyToPlace;
		Result.Status = EPaper2DPlusFrameCuePlacementStatus::InvalidCueClass;
		break;
	case EPaper2DPlusFrameCuePlacementAppendStatus::TargetUnavailable:
	default:
		State = EPaper2DPlusFrameCuePlacementRequestState::RetainedTypeConsumed;
		Completion = FPaper2DPlusFrameCuePlacementCompletion();
		Result.Status = EPaper2DPlusFrameCuePlacementStatus::TargetUnavailable;
		break;
	}
	Result.State = State;
	if (Result.Status == EPaper2DPlusFrameCuePlacementStatus::Success)
	{
		// Clear before invoking so a callback that re-enters Commit observes the consumed state and can
		// never cause a second origin refresh. A weak UI lambda may harmlessly expire before this point.
		FPaper2DPlusFrameCuePlacementCompletion CompletionToInvoke = MoveTemp(Completion);
		Completion = FPaper2DPlusFrameCuePlacementCompletion();
		if (CompletionToInvoke)
		{
			CompletionToInvoke(Result);
		}
	}
	return Result;
}

namespace Paper2DPlusFrameCuePlacementAuthoring
{
	FPaper2DPlusFrameCuePlacementTarget CaptureTarget(
		const TSharedPtr<FFrameCueDataProvider>& Provider,
		const FProfileScopedAnimationIdentity& Scope,
		int32 CapturedFrame,
		const FGuid CapturedTrackId)
	{
		FPaper2DPlusFrameCuePlacementTarget Target;
		if (!Provider.IsValid()
			|| !Scope.IsValid()
			|| CapturedFrame < 0
			|| Provider->ResolveAnimationIndex(Scope) == INDEX_NONE
			|| !Provider->CanEnsureCues(Scope))
		{
			return Target;
		}
		const int32 FrameCount = Provider->GetFrameCount(Scope);
		if (FrameCount <= 0 || CapturedFrame >= FrameCount)
		{
			return Target;
		}
		if (CapturedTrackId.IsValid()
			&& !Provider->GetOptionalTracks(Scope).ContainsByPredicate(
				[CapturedTrackId](const FPaper2DPlusFrameCueTrackDefinition& Track)
				{
					return Track.TrackId == CapturedTrackId;
				}))
		{
			return Target;
		}

		Target.Scope = Scope;
		Target.CapturedFrame = CapturedFrame;
		Target.CapturedTrackId = CapturedTrackId;
		Target.ProviderSnapshot = Provider->CapturePlacementSnapshot(Scope);
		if (!Target.ProviderSnapshot.IsValid())
		{
			return {};
		}
		return Target;
	}

	bool IsCueClassCompatible(
		const UClass* CueClass,
		EPaper2DPlusFrameCuePlacementKind Kind)
	{
		if (!CueClass)
		{
			return false;
		}

		// The shared Cue Type descriptor is the sole readiness authority. In particular, a freshly
		// compiled but unsaved generated class must not escape through direct placement while its
		// package/durable fingerprint is still provisional.
		const FPaper2DPlusFrameCueTypeDescriptor Descriptor =
			FPaper2DPlusFrameCueTypeAuthoring::DescribeCueType(
				const_cast<UClass*>(CueClass));
		if (Descriptor.Availability != EPaper2DPlusFrameCueTypeAvailability::Ready)
		{
			return false;
		}
		return Kind == EPaper2DPlusFrameCuePlacementKind::Moment
			? Descriptor.Kind == EPaper2DPlusFrameCueTypeKind::Moment
			: Descriptor.Kind == EPaper2DPlusFrameCueTypeKind::Range;
	}

	void ClampPlacementToAnimation(
		UPaper2DPlusCueBase* Cue,
		int32 AnimationFrameCount)
	{
		if (!Cue)
		{
			return;
		}
		const int32 SafeFrameCount = FMath::Max(0, AnimationFrameCount);
		const int32 LastFrame = FMath::Max(0, SafeFrameCount - 1);
		if (UPaper2DPlusCueState* RangeCue = Cast<UPaper2DPlusCueState>(Cue))
		{
			RangeCue->StartFrame = FMath::Clamp(RangeCue->StartFrame, 0, LastFrame);
			const int32 MaxRangeLength = SafeFrameCount > 0
				? FMath::Max(1, SafeFrameCount - RangeCue->StartFrame)
				: 1;
			RangeCue->FrameCount = FMath::Clamp(RangeCue->FrameCount, 1, MaxRangeLength);
		}
		else if (UPaper2DPlusCue* MomentCue = Cast<UPaper2DPlusCue>(Cue))
		{
			MomentCue->TriggerFrame = FMath::Clamp(MomentCue->TriggerFrame, 0, LastFrame);
		}
	}

	UPaper2DPlusCueBase* CreatePlacementFromClassDefaults(
		UObject* Owner,
		UClass* CueClass,
		EPaper2DPlusFrameCuePlacementKind Kind,
		int32 AnchorFrame,
		int32 AnimationFrameCount)
	{
		if (!IsValid(Owner) || !IsCueClassCompatible(CueClass, Kind))
		{
			return nullptr;
		}
		UPaper2DPlusCueBase* CueTypeDefaults =
			Cast<UPaper2DPlusCueBase>(CueClass->GetDefaultObject());
		if (!CueTypeDefaults)
		{
			return nullptr;
		}
		UPaper2DPlusCueBase* NewCue = NewObject<UPaper2DPlusCueBase>(
			Owner, CueClass, NAME_None, RF_Transactional);
		if (!NewCue)
		{
			return nullptr;
		}

		UEngine::FCopyPropertiesForUnrelatedObjectsParams CopyParams;
		CopyParams.bDoDelta = false;
		CopyParams.bNotifyObjectReplacement = false;
		UEngine::CopyPropertiesForUnrelatedObjects(CueTypeDefaults, NewCue, CopyParams);
		NewCue->SetPrimaryAnchorFrame(AnchorFrame);
		ClampPlacementToAnimation(NewCue, AnimationFrameCount);
		return NewCue;
	}
}
