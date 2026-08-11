// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CharacterLayerExactCompositor.h"
#include "CoreMinimal.h"
#include "Paper2DPlusLayerBakeTypes.h"
#include "Paper2DPlusTypes.h"
#include "UObject/ObjectPtr.h"
#include "UObject/WeakObjectPtr.h"

class UPaper2DPlusCharacterLayerAsset;
class UPaper2DPlusCharacterProfileAsset;
class UPaper2DPlusCueBase;
class UPaper2DPlusFrameEventBase;
class UPaperFlipbook;
class UPaperSprite;

enum class ECharacterLayerBakeDiagnosticSeverity : uint8
{
	Info,
	Warning,
	Error
};

enum class ECharacterLayerBakeDiagnosticCode : uint8
{
	MissingRegistration,
	InvalidTarget,
	AmbiguousAnimation,
	FrameCountMismatch,
	MissingSourceFrame,
	UnreadableSourcePixels,
	UnsupportedSpriteTopology,
	UnsupportedSpriteMaterial,
	UnsupportedTextureBuildSettings,
	IncompatiblePixelsPerUnit,
	NonIntegralPlacement,
	OutOfRangeCueAnchor,
	InvalidFrameCue,
	InvalidRegistrationGeometry,
	TextureLimitExceeded,
	MissingCharacterBaseline,
	CompositionFailure
};

struct FCharacterLayerBakeDiagnostic
{
	ECharacterLayerBakeDiagnosticSeverity Severity = ECharacterLayerBakeDiagnosticSeverity::Info;
	ECharacterLayerBakeDiagnosticCode Code = ECharacterLayerBakeDiagnosticCode::CompositionFailure;
	FString AnimationName;
	FGuid LayerId;
	int32 FrameIndex = INDEX_NONE;
	FString Message;
};

/** Provenance kept parallel to the final baseline-first, bottom-to-top Cue order. */
struct FCharacterLayerBakeCueSource
{
	TObjectPtr<UPaper2DPlusCueBase> Cue = nullptr;
	FGuid LayerId;
	bool bCharacterBaseline = false;
	int32 SourceOrder = INDEX_NONE;
	FString SemanticDigest;
};

/** Immutable compiler output for one canonical animation. It contains recipes only; no package is changed. */
struct FCharacterLayerBakeAnimationPlan
{
	TWeakObjectPtr<UPaperFlipbook> TargetFlipbook;
	FSoftObjectPath TargetFlipbookPath;
	FString AnimationName;
	int32 KeyFrameCount = 0;
	float FramesPerSecond = 0.0f;
	TArray<int32> FrameRuns;
	TArray<FGuid> IncludedLayerIds;

	FCharacterLayerExactComposite Composite;

	/** Per-output-frame native behavior source. Null canonical frames use the validated registration fallback. */
	TArray<FCharacterLayerRegisteredFrame> NativeFrameRecipes;
	TArray<FFrameHitboxData> OutputGameplayFrames;
	TArray<TObjectPtr<UPaper2DPlusCueBase>> OutputFrameCues;
	TArray<TObjectPtr<UPaper2DPlusFrameEventBase>> BaselineLegacyFrameEvents;
	TArray<FCharacterLayerBakeCueSource> CueSources;

	FString SourceDigest;
	FString ExpectedTargetPreconditionDigest;
	FString OutputDigest;
	TArray<FCharacterLayerBakeDiagnostic> Diagnostics;

	bool HasErrors() const;
	bool CanCommit() const { return !HasErrors() && Composite.bSuccess; }
};

struct FCharacterLayerBakePlan
{
	TWeakObjectPtr<UPaper2DPlusCharacterLayerAsset> LayerAsset;
	TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset> CharacterProfile;
	FGuid BakeSetId;
	uint32 DigestVersion = 0;
	TArray<FCharacterLayerBakeAnimationPlan> Animations;
	TArray<FCharacterLayerBakeDiagnostic> Diagnostics;

	bool HasErrors() const;
	bool CanCommit() const;
};

struct FCharacterLayerBakeBuildOptions
{
	int32 MaxTextureDimension = 16384;
	uint32 DigestVersion = 4;
};

namespace CharacterLayerBakeCore
{
	static constexpr uint32 CurrentDigestVersion = 4;
	static constexpr uint32 NativeBehaviorDigestVersion = 1;
	static constexpr uint32 RegistrationDigestVersion = 1;

	/** Convert an exact whole-pixel vector without rounding before finite/int32 range validation. */
	bool TryIntegralPoint(const FVector2D& Value, FIntPoint& OutPoint);

	/** Pure planning pass: reads source/registration/profile state and returns deterministic recipes. */
	FCharacterLayerBakePlan BuildPlan(
		const UPaper2DPlusCharacterLayerAsset* LayerAsset,
		const UPaper2DPlusCharacterProfileAsset* CharacterProfile,
		const FCharacterLayerBakeBuildOptions& Options = FCharacterLayerBakeBuildOptions());

	/** Bounded-memory compiler entry used by Bake All: build/commit/release one registration at a time. */
	FCharacterLayerBakeAnimationPlan BuildAnimationPlan(
		const UPaper2DPlusCharacterLayerAsset* LayerAsset,
		const UPaper2DPlusCharacterProfileAsset* CharacterProfile,
		int32 RegistrationIndex,
		const FCharacterLayerBakeBuildOptions& Options = FCharacterLayerBakeBuildOptions());

	/** Cue class + persistent reflected payload only; UObject name, outer, flags, and transient state are excluded. */
	FString ComputeCueSemanticDigest(
		const UPaper2DPlusCueBase* Cue,
		uint32 DigestVersion = CurrentDigestVersion);

	/** Recompute the mutation precondition immediately before commit; empty means the target is unresolved/ambiguous. */
	FString ComputeTargetPreconditionDigest(
		const UPaper2DPlusCharacterProfileAsset* CharacterProfile,
		const UPaperFlipbook* TargetFlipbook,
		uint32 DigestVersion = CurrentDigestVersion);

	/** Shared registration/preflight hash for supported native sprite behavior and source topology. */
	FString ComputeNativeSpriteBehaviorDigest(const UPaperSprite* Sprite);

	/** Immutable-registration seal. RegistrationDigest itself is deliberately excluded from the hash. */
	FString ComputeAnimationRegistrationDigest(
		const FCharacterLayerAnimationRegistration& Registration,
		uint32 DigestVersion = RegistrationDigestVersion);
}
