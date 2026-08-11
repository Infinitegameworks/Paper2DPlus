// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FrameCues/Paper2DPlusFrameCue.h"
#include "GameplayTagContainer.h"
#include "Paper2DPlusLayerUsageMode.h"
#include "Paper2DPlusTypes.h"
#include "UObject/SoftObjectPtr.h"
#include "Paper2DPlusLayerBakeTypes.generated.h"

class UPaper2DPlusCueBase;
class UPaper2DPlusCharacterLayerAsset;
class UPaperFlipbook;
class UPaperSprite;

namespace Paper2DPlusLayerBakeVersion
{
	/** Structural schema for the complete bake manifest; independent from digest-algorithm versions. */
	static constexpr uint32 CurrentManifestVersion = 1;
}

/** Persistent editor lifecycle for an exclusively owned fixed-publishing bake set. */
UENUM()
enum class ECharacterLayerBakeAttachmentState : uint8
{
	Unclaimed,
	Attached,
	Detached,
	RecoveryRequired
};

/** Designer-facing state derived from attachment plus versioned source/output digests. */
UENUM()
enum class ECharacterLayerBakeStatus : uint8
{
	NeverBaked,
	UpToDate,
	NeedsBake,
	OutputConflict,
	Detached,
	RecoveryRequired
};

/** Durable editor audit identity for the most recent explicit publish command. */
UENUM()
enum class ECharacterLayerBakeLifecycleOperation : uint8
{
	None,
	BakeCurrent,
	BakeAll,
	AdoptAndBakeAll,
	SaveBakeSet,
	Repair,
	RebaseRegistration,
	Detach
};

/**
 * Additive, editor-only audit record for the latest explicit Layer publish command.
 *
 * The bake manifest remains the consistency contract. This record is presentation/evidence only and
 * never authorizes an overwrite. Save Bake Set writes the Layer package last so a partial package save
 * can be reported durably whenever that final checkpoint package itself can be saved.
 */
USTRUCT()
struct PAPER2DPLUS_API FCharacterLayerBakeOperationRecord
{
	GENERATED_BODY()

	UPROPERTY()
	ECharacterLayerBakeLifecycleOperation Operation = ECharacterLayerBakeLifecycleOperation::None;

	UPROPERTY()
	FDateTime CompletedUtc;

	UPROPERTY()
	bool bSuccess = false;

	UPROPERTY()
	bool bCancelled = false;

	UPROPERTY()
	bool bRolledBack = false;

	UPROPERTY()
	bool bRecoveryRequired = false;

	UPROPERTY()
	FString Report;

	UPROPERTY()
	TArray<FSoftObjectPath> TouchedPackages;

	UPROPERTY()
	TArray<FSoftObjectPath> SavedPackages;

	UPROPERTY()
	TArray<FSoftObjectPath> FailedPackages;

	bool IsEmpty() const
	{
		return Operation == ECharacterLayerBakeLifecycleOperation::None && CompletedUtc == FDateTime();
	}
};

/** A layer channel either adds to lower output or replaces only that channel's lower output. */
UENUM(BlueprintType)
enum class EPaper2DPlusLayerSourceMerge : uint8
{
	Add UMETA(DisplayName = "Add"),
	ReplaceLower UMETA(DisplayName = "Replace Lower")
};

/** One-level, organization-only group. Group identity and membership never enter a bake digest. */
USTRUCT()
struct PAPER2DPLUS_API FCharacterLayerGroupInfo
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Layer Group", meta = (IgnoreForMemberInitializationTest))
	FGuid GroupId = FGuid::NewGuid();

	UPROPERTY(EditAnywhere, Category = "Layer Group")
	FText DisplayName;
};

/** Layer-local gameplay authored at one canonical key frame. Character-wide frame flags live in Profile Baseline. */
USTRUCT()
struct PAPER2DPLUS_API FCharacterLayerAuthoredFrameData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Layer Gameplay")
	TArray<FHitboxData> AttackBoxes;

	UPROPERTY(EditAnywhere, Category = "Layer Gameplay")
	TArray<FHitboxData> HurtBoxes;

	UPROPERTY(EditAnywhere, Category = "Layer Gameplay")
	TArray<FSocketData> Sockets;
};

/**
 * Canonically bound per-animation source data owned by one real layer.
 * Flipbook is authoritative; LegacyAnimationName is a display/load fallback and must resolve uniquely before bake.
 */
USTRUCT()
struct PAPER2DPLUS_API FCharacterLayerAuthoredAnimationData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Layer Gameplay")
	TSoftObjectPtr<UPaperFlipbook> Flipbook;

	UPROPERTY(EditAnywhere, Category = "Layer Gameplay")
	FString LegacyAnimationName;

	UPROPERTY(EditAnywhere, Category = "Layer Gameplay")
	EPaper2DPlusLayerSourceMerge AttackMerge = EPaper2DPlusLayerSourceMerge::Add;

	UPROPERTY(EditAnywhere, Category = "Layer Gameplay")
	EPaper2DPlusLayerSourceMerge HurtMerge = EPaper2DPlusLayerSourceMerge::Add;

	UPROPERTY(EditAnywhere, Category = "Layer Gameplay")
	EPaper2DPlusLayerSourceMerge SocketMerge = EPaper2DPlusLayerSourceMerge::Add;

	UPROPERTY(EditAnywhere, Category = "Layer Gameplay")
	TArray<FCharacterLayerAuthoredFrameData> Frames;

	UPROPERTY(EditAnywhere, Instanced, Category = "Layer Gameplay")
	TArray<TObjectPtr<UPaper2DPlusCueBase>> FrameCues;

#if WITH_EDITORONLY_DATA
	/** Named-track organization belongs only to AuthoredAnimations; cooked projections clear this sidecar. */
	UPROPERTY()
	FPaper2DPlusFrameCueTrackLayout CueTrackLayout;
#endif
};

/** Original canonical sprite contract captured before managed output replaces flipbook sprite bindings. */
USTRUCT()
struct PAPER2DPLUS_API FCharacterLayerRegisteredFrame
{
	GENERATED_BODY()

	UPROPERTY()
	TSoftObjectPtr<UPaperSprite> SourceSprite;

	UPROPERTY()
	FVector2D SourceUV = FVector2D::ZeroVector;

	UPROPERTY()
	FVector2D SourceDimension = FVector2D::ZeroVector;

	UPROPERTY()
	FVector2D PivotLocal = FVector2D::ZeroVector;

	UPROPERTY()
	float PixelsPerUnrealUnit = 1.0f;

	UPROPERTY()
	FSoftObjectPath MaterialPath;

	/** Hash of supported native render/collision geometry captured by the registration builder. */
	UPROPERTY()
	FString NativeBehaviorDigest;

	UPROPERTY()
	bool bUsesUnsupportedAtlasGroup = false;

	UPROPERTY()
	bool bUsesUnsupportedAdditionalTextures = false;
};

/** Immutable-until-Rebase source topology for one canonical animation. */
USTRUCT()
struct PAPER2DPLUS_API FCharacterLayerAnimationRegistration
{
	GENERATED_BODY()

	UPROPERTY()
	TSoftObjectPtr<UPaperFlipbook> Flipbook;

	UPROPERTY()
	FString LegacyAnimationName;

	UPROPERTY()
	float FramesPerSecond = 0.0f;

	UPROPERTY()
	TArray<int32> FrameRuns;

	UPROPERTY()
	TArray<FCharacterLayerRegisteredFrame> Frames;

	UPROPERTY()
	FString RegistrationDigest;
};

/** Versioned semantic record for one compiled animation. */
USTRUCT()
struct PAPER2DPLUS_API FCharacterLayerAnimationBakeRecord
{
	GENERATED_BODY()

	UPROPERTY()
	TSoftObjectPtr<UPaperFlipbook> Flipbook;

	UPROPERTY()
	FString LegacyAnimationName;

	UPROPERTY()
	FString SourceDigest;

	UPROPERTY()
	FString OutputDigest;

	UPROPERTY()
	uint32 OutputRevision = 0;

	UPROPERTY()
	FSoftObjectPath ManagedTexture;

	UPROPERTY()
	TArray<FSoftObjectPath> ManagedSprites;
};

/** The complete consistency set written last by the bake coordinator. */
USTRUCT()
struct PAPER2DPLUS_API FCharacterLayerBakeManifest
{
	GENERATED_BODY()

	/** Older schemas may be upgraded only by a complete Bake All; newer schemas are read-only. */
	UPROPERTY()
	uint32 ManifestVersion = Paper2DPlusLayerBakeVersion::CurrentManifestVersion;

	/** Older versions may be upgraded only by a complete Bake All; newer versions are read-only. */
	UPROPERTY()
	uint32 DigestVersion = 1;

	UPROPERTY()
	FGuid BakeSetId;

	UPROPERTY()
	uint32 BakeRevision = 0;

	UPROPERTY()
	FString CharacterProfilePathHint;

	UPROPERTY()
	TArray<FCharacterLayerAnimationBakeRecord> Animations;

	/** Complete active bake-set packages plus any retired/frozen packages awaiting their one explicit save. */
	UPROPERTY()
	TArray<FSoftObjectPath> TouchedPackages;

	UPROPERTY()
	FDateTime LastBakeUtc;

	UPROPERTY()
	FString LastReport;

	bool IsEmpty() const
	{
		return BakeRevision == 0 && Animations.IsEmpty() && TouchedPackages.IsEmpty();
	}
};

/** Passive migration/adoption never chooses a destructive resolution for a designer. */
UENUM()
enum class ECharacterLayerAdoptionResolution : uint8
{
	Suggested,
	NeedsDecision,
	Assign,
	Merge,
	ArchiveOnly
};

/** One legacy source row and the stable candidates a later adoption dialog may resolve. */
USTRUCT()
struct PAPER2DPLUS_API FCharacterLayerAdoptionDecision
{
	GENERATED_BODY()

	UPROPERTY()
	FGuid DecisionId;

	UPROPERTY()
	FString SourceDescription;

	UPROPERTY()
	TSoftObjectPtr<UPaperFlipbook> Flipbook;

	UPROPERTY()
	FString LegacyAnimationName;

	UPROPERTY()
	TArray<FGuid> CandidateLayerIds;

	UPROPERTY()
	FGuid DestinationLayerId;

	UPROPERTY()
	ECharacterLayerAdoptionResolution Resolution = ECharacterLayerAdoptionResolution::NeedsDecision;

	UPROPERTY()
	FString Reason;
};

/** Mutation-free preview consumed by Adopt and Bake All. */
USTRUCT()
struct PAPER2DPLUS_API FCharacterLayerAdoptionAnalysis
{
	GENERATED_BODY()

	UPROPERTY()
	FGuid ProspectiveBakeSetId;

	UPROPERTY()
	TArray<FCharacterLayerAdoptionDecision> Decisions;

	UPROPERTY()
	TArray<FString> Blockers;

	UPROPERTY()
	int32 SuggestedCount = 0;

	UPROPERTY()
	int32 NeedsDecisionCount = 0;

	bool CanAdopt() const { return Blockers.IsEmpty() && NeedsDecisionCount == 0; }
};

#if WITH_EDITOR
namespace Paper2DPlusLayerBake
{
	/** Stable semantic source hash used by migration, status, and the pure compiler. */
	PAPER2DPLUS_API FString ComputeSourceDigest(const UPaper2DPlusCharacterLayerAsset& Asset);

	/** Deterministic decision identity; unlike FGuid::NewGuid it is safe in a mutation-free preview. */
	PAPER2DPLUS_API FGuid MakeStableDecisionId(const FString& StableSourceKey);
}
#endif
