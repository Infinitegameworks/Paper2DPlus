// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FrameCueDataProvider.h"

/** The native timing shape required from the saved Cue Type class. */
enum class EPaper2DPlusFrameCuePlacementKind : uint8
{
	Moment,
	Range
};

/** Explicit lifecycle for one create-type-and-place request. */
enum class EPaper2DPlusFrameCuePlacementRequestState : uint8
{
	/** The Cue Type editor still owns the request and has not published a ready class. */
	EditingType,
	/** A saved, compiled class may be committed to the captured target. */
	ReadyToPlace,
	/** Re-entrant completion callbacks are rejected while the provider is appending. */
	CommitInProgress,
	/** The provider restored all state after a failed append; the same request may be retried. */
	RetryableAfterRollback,
	/** Exactly one placement was appended. */
	PlacedConsumed,
	/** The Cue Type was retained without a placement, or the captured target expired. */
	RetainedTypeConsumed
};

/** Structured completion result; callers never infer success from editor/toolkit lifetime. */
enum class EPaper2DPlusFrameCuePlacementStatus : uint8
{
	Success,
	AlreadyConsumed,
	CommitInProgress,
	NotReadyToPlace,
	InvalidRequest,
	InvalidCueClass,
	TargetUnavailable,
	PlacementRolledBack,
	RetainedTypeWithoutPlacement
};

/** Deterministic failure seams used to prove rollback at every mutation boundary. */
enum class EPaper2DPlusFrameCuePlacementFailurePoint : uint8
{
	None,
	AfterTargetModify,
	AfterStorageEnsure,
	AfterPlacementConstruction,
	AfterArrayAppend,
	AfterIdentityDerivation
};

enum class EPaper2DPlusFrameCuePlacementAppendStatus : uint8
{
	Success,
	InvalidCueClass,
	TargetUnavailable,
	RolledBack
};

struct PAPER2DPLUSEDITOR_API FPaper2DPlusFrameCuePlacementAppendResult
{
	EPaper2DPlusFrameCuePlacementAppendStatus Status =
		EPaper2DPlusFrameCuePlacementAppendStatus::TargetUnavailable;
	FFrameCueStableIdentity PlacementIdentity;
};

/**
 * Frozen storage authority captured before a Cue Type editor is opened.
 *
 * Concrete provider snapshots strongly retain their Profile/Layer assets and resolve the captured animation
 * and LayerId directly. They deliberately retain no editor model, selection, toolkit, or Slate widget.
 */
class PAPER2DPLUSEDITOR_API FPaper2DPlusFrameCuePlacementProviderSnapshot
{
public:
	virtual ~FPaper2DPlusFrameCuePlacementProviderSnapshot() = default;

	virtual FPaper2DPlusFrameCuePlacementAppendResult AppendPlacementAtomically(
		UClass* CueClass,
		EPaper2DPlusFrameCuePlacementKind Kind,
		int32 CapturedFrame,
		FGuid CapturedTrackId,
		EPaper2DPlusFrameCuePlacementFailurePoint FailurePoint) = 0;
};

/** Immutable origin captured at the moment the designer chooses to create/place a Cue Type. */
struct PAPER2DPLUSEDITOR_API FPaper2DPlusFrameCuePlacementTarget
{
	FProfileScopedAnimationIdentity Scope;
	int32 CapturedFrame = INDEX_NONE;
	/** Invalid means the permanent Default track; valid IDs must still exist when commit begins. */
	FGuid CapturedTrackId;
	TSharedPtr<FPaper2DPlusFrameCuePlacementProviderSnapshot> ProviderSnapshot;

	bool IsValid() const
	{
		return Scope.IsValid() && CapturedFrame >= 0 && ProviderSnapshot.IsValid();
	}
};

struct PAPER2DPLUSEDITOR_API FPaper2DPlusFrameCuePlacementResult
{
	EPaper2DPlusFrameCuePlacementStatus Status =
		EPaper2DPlusFrameCuePlacementStatus::InvalidRequest;
	EPaper2DPlusFrameCuePlacementRequestState State =
		EPaper2DPlusFrameCuePlacementRequestState::EditingType;
	FFrameCueStableIdentity PlacementIdentity;
};

/**
 * Optional one-shot notification delivered only after a placement commit succeeds.
 *
 * The pending request owns no Slate/editor object. UI callers bind a weak-origin lambda so the exact
 * captured placement remains valid even when its originating toolkit closes before completion.
 */
using FPaper2DPlusFrameCuePlacementCompletion =
	TFunction<void(const FPaper2DPlusFrameCuePlacementResult&)>;

/** One-shot request shared by the origin editor and the separately opened Cue Type editor. */
class PAPER2DPLUSEDITOR_API FPaper2DPlusFrameCuePendingPlacement
{
public:
	static TSharedRef<FPaper2DPlusFrameCuePendingPlacement> Create(
		const FPaper2DPlusFrameCuePlacementTarget& Target,
		EPaper2DPlusFrameCuePlacementKind Kind,
		bool bReadyToPlace,
		FPaper2DPlusFrameCuePlacementCompletion Completion = {});

	FPaper2DPlusFrameCuePendingPlacement(
		const FPaper2DPlusFrameCuePlacementTarget& InTarget,
		EPaper2DPlusFrameCuePlacementKind InKind,
		bool bReadyToPlace,
		FPaper2DPlusFrameCuePlacementCompletion InCompletion = {});

	EPaper2DPlusFrameCuePlacementRequestState GetState() const { return State; }

	/** Publish the saved/compiled-class readiness boundary without coupling to Cue Type compile/save code. */
	bool MarkReadyToPlace();
	/** Retain the authored Cue Type while explicitly declining the placement. */
	FPaper2DPlusFrameCuePlacementResult ConsumeWithoutPlacement();
	/** Append the class defaults exactly once to the frozen target. */
	FPaper2DPlusFrameCuePlacementResult Commit(UClass* CueClass);

#if WITH_DEV_AUTOMATION_TESTS
	void SetFailurePointForTests(EPaper2DPlusFrameCuePlacementFailurePoint InFailurePoint)
	{
		FailurePointForTests = InFailurePoint;
	}
#endif

private:
	FPaper2DPlusFrameCuePlacementTarget Target;
	EPaper2DPlusFrameCuePlacementKind Kind = EPaper2DPlusFrameCuePlacementKind::Moment;
	EPaper2DPlusFrameCuePlacementRequestState State =
		EPaper2DPlusFrameCuePlacementRequestState::EditingType;
	FPaper2DPlusFrameCuePlacementCompletion Completion;
#if WITH_DEV_AUTOMATION_TESTS
	EPaper2DPlusFrameCuePlacementFailurePoint FailurePointForTests =
		EPaper2DPlusFrameCuePlacementFailurePoint::None;
#endif
};

namespace Paper2DPlusFrameCuePlacementAuthoring
{
	/** Capture only when animation, Layer/Profile scope, and frame resolve at the origin. */
	PAPER2DPLUSEDITOR_API FPaper2DPlusFrameCuePlacementTarget CaptureTarget(
		const TSharedPtr<FFrameCueDataProvider>& Provider,
		const FProfileScopedAnimationIdentity& Scope,
		int32 CapturedFrame,
		FGuid CapturedTrackId = FGuid());

	PAPER2DPLUSEDITOR_API bool IsCueClassCompatible(
		const UClass* CueClass,
		EPaper2DPlusFrameCuePlacementKind Kind);

	PAPER2DPLUSEDITOR_API void ClampPlacementToAnimation(
		UPaper2DPlusCueBase* Cue,
		int32 AnimationFrameCount);

	/** Instantiate a placement from the saved class CDO without invoking Cue Type compile/save authority. */
	PAPER2DPLUSEDITOR_API UPaper2DPlusCueBase* CreatePlacementFromClassDefaults(
		UObject* Owner,
		UClass* CueClass,
		EPaper2DPlusFrameCuePlacementKind Kind,
		int32 AnchorFrame,
		int32 AnimationFrameCount);
}
