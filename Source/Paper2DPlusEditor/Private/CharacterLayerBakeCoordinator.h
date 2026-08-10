// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Paper2DPlusLayerBakeTypes.h"

class UPaper2DPlusCharacterLayerAsset;
class UPaper2DPlusCharacterProfileAsset;
class UPackage;

enum class ECharacterLayerBakeOperation : uint8
{
	Current,
	All,
	AdoptAndBakeAll
};

/** Deterministic mutation boundaries used only by integration tests and recovery tooling. */
enum class ECharacterLayerBakeFailurePoint : uint8
{
	None,
	AfterAdoption,
	AfterManagedObjects,
	AfterTexture,
	AfterSprites,
	AfterFlipbook,
	AfterProfile,
	AfterClaim,
	AfterManifest,
	RollbackVerification
};

struct FCharacterLayerBakeRequest
{
	UPaper2DPlusCharacterLayerAsset* LayerAsset = nullptr;
	UPaper2DPlusCharacterProfileAsset* CharacterProfile = nullptr;
	ECharacterLayerBakeOperation Operation = ECharacterLayerBakeOperation::All;
	int32 CurrentRegistrationIndex = INDEX_NONE;

	/** Explicit rows chosen by the adoption dialog. Suggested one-owner rows may be omitted. */
	TArray<FCharacterLayerAdoptionDecision> AdoptionDecisions;

	/** Advanced repair action: source is still authoritative, but competing ownership is never overwritten. */
	bool bOverwriteManagedOutputConflict = false;

	/** Honored while staging only. Once preconditions are revalidated, commit is non-cancellable. */
	TFunction<bool()> IsCancellationRequested;

	ECharacterLayerBakeFailurePoint InjectFailure = ECharacterLayerBakeFailurePoint::None;
};

struct FCharacterLayerBakeResult
{
	bool bSuccess = false;
	bool bCancelled = false;
	bool bRolledBack = false;
	bool bRecoveryRequired = false;
	FString Report;
	TArray<FString> Errors;
	TArray<FSoftObjectPath> TouchedPackages;
	TArray<FSoftObjectPath> CreatedAssets;
};

struct FCharacterLayerBakeSaveResult
{
	bool bSuccess = false;
	bool bCheckpointSaved = false;
	FString Report;
	TArray<FSoftObjectPath> SavedPackages;
	TArray<FSoftObjectPath> FailedPackages;
};

/**
 * Optional package persistence backend. Production callers use the editor save implementation by omitting it;
 * focused automation supplies a deterministic backend so checkpoint-failure recovery is testable without I/O.
 */
struct FCharacterLayerBakeSaveBackend
{
	TFunction<bool(const TArray<UPackage*>& Packages, TArray<UPackage*>& OutFailedPackages)> SavePackages;
};

/** Per-registration evidence used by the shared publish-status projection. */
struct FCharacterLayerBakeAnimationStatus
{
	int32 RegistrationIndex = INDEX_NONE;
	FSoftObjectPath FlipbookPath;
	FString AnimationName;
	bool bHasManifestRecord = false;
	bool bSourceDrift = false;
	bool bOutputConflict = false;
	TArray<FString> Diagnostics;
};

/**
 * One presentation-neutral, deep status snapshot for toolkits, validation, Content Browser and commandlets.
 * Expensive source/output hashing happens only while building this snapshot; Slate caches the result and never
 * invokes it from Tick or OnPaint.
 */
struct FCharacterLayerBakeStatusSnapshot
{
	ECharacterLayerBakeStatus Status = ECharacterLayerBakeStatus::NeverBaked;
	ECharacterLayerBakeAttachmentState AttachmentState = ECharacterLayerBakeAttachmentState::Unclaimed;
	FCharacterLayerAdoptionAnalysis AdoptionAnalysis;
	TArray<FCharacterLayerBakeAnimationStatus> Animations;
	TArray<FString> Blockers;
	TArray<FString> Warnings;
	TArray<FSoftObjectPath> DirtyPackages;
	TArray<FSoftObjectPath> MissingPackages;
	TArray<FSoftObjectPath> FailedSavePackages;
	TArray<FSoftObjectPath> OverwriteScope;
	FCharacterLayerBakeOperationRecord LastOperation;

	int32 CurrentRegistrationIndex = INDEX_NONE;
	int32 DefaultActiveLayerCount = 0;
	int32 SourceDriftCount = 0;
	int32 OutputConflictCount = 0;

	bool bReadyToAdopt = false;
	bool bAdoptionNeedsDecisions = false;
	bool bAdoptionBlocked = false;
	bool bIntegrityBlocked = false;
	bool bNeedsSave = false;
	bool bPartialSave = false;
	bool bHasCompetingOwnership = false;

	bool bCanAdopt = false;
	bool bCanBakeCurrent = false;
	bool bCanBakeAll = false;
	bool bCanSaveBakeSet = false;
	bool bCanOverwriteFromSource = false;
	bool bCanRepair = false;
	bool bCanRebaseRegistration = false;
	bool bCanDetach = false;
};

namespace CharacterLayerBakeCoordinator
{
	/** Sole mutator for canonical Layer output. Never starts an undo transaction and never saves packages. */
	FCharacterLayerBakeResult Execute(const FCharacterLayerBakeRequest& Request);

	/** Explicit persistence action: saves exactly the latest successful manifest package set. */
	FCharacterLayerBakeSaveResult SaveBakeSet(
		UPaper2DPlusCharacterLayerAsset* LayerAsset,
		const FCharacterLayerBakeSaveBackend* SaveBackend = nullptr);

	/** Sole deep lifecycle/status evaluator. Call from explicit refresh/event boundaries, never Slate paint/tick. */
	FCharacterLayerBakeStatusSnapshot EvaluateStatus(
		const UPaper2DPlusCharacterLayerAsset* LayerAsset,
		const UPaper2DPlusCharacterProfileAsset* CharacterProfile,
		int32 CurrentRegistrationIndex = INDEX_NONE);

	/** RecoveryRequired's only path back to an attached, verified bake set. */
	FCharacterLayerBakeResult Repair(
		UPaper2DPlusCharacterLayerAsset* LayerAsset,
		UPaper2DPlusCharacterProfileAsset* CharacterProfile,
		const TArray<FCharacterLayerAdoptionDecision>& AdoptionDecisions = {});

	/** Safely accepts intentional canonical topology changes, then immediately recompiles the complete bake set. */
	FCharacterLayerBakeResult RebaseRegistration(
		UPaper2DPlusCharacterLayerAsset* LayerAsset,
		UPaper2DPlusCharacterProfileAsset* CharacterProfile);

	/** Freeze verified canonical output in place and release only this bake set's matching management claims. */
	FCharacterLayerBakeResult Detach(
		UPaper2DPlusCharacterLayerAsset* LayerAsset,
		UPaper2DPlusCharacterProfileAsset* CharacterProfile);

	/** Build the immutable source topology used by first adoption and explicit Rebase Registration. */
	bool CaptureRegistration(
		const UPaper2DPlusCharacterProfileAsset* CharacterProfile,
		TArray<FCharacterLayerAnimationRegistration>& OutRegistration,
		FString& OutError);

	/** Recompute the actual managed output digest for conflict/status/validation consumers. */
	FString ComputeCurrentOutputDigest(
		const UPaper2DPlusCharacterLayerAsset* LayerAsset,
		const UPaper2DPlusCharacterProfileAsset* CharacterProfile,
		const FCharacterLayerAnimationBakeRecord& Record,
		FString* OutError = nullptr);

	/** Canonical package root for this Layer asset's bounded generated outputs. */
	FString GetManagedOutputRoot(const UPaper2DPlusCharacterLayerAsset* LayerAsset);

	/** Removes abandoned operation directories beneath the dedicated Intermediate root only. */
	void CleanupAbandonedStaging();
	FString GetStagingRoot();

	/** No-follow filesystem probe shared by cleanup and its platform safety regression. */
	bool IsPathLinkOrReparsePoint(const TCHAR* Path);
}
