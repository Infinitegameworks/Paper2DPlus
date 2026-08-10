// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Paper2DPlusFrameData.h"
#include "Paper2DPlusCombatProfileTypes.generated.h"

class UPaperFlipbook;

UENUM(BlueprintType)
enum class EPaper2DPlusCombatValidationSeverity : uint8
{
	Info,
	Warning,
	Error
};

UENUM(BlueprintType)
enum class EPaper2DPlusCombatVariableType : uint8
{
	Bool,
	Int32,
	Float,
	Name,
	String,
	GameplayTag,
	GameplayTagContainer,
	Vector2D,
	Vector
};

UENUM(BlueprintType)
enum class EPaper2DPlusCombatConsiderationSource : uint8
{
	DistanceToTarget,
	SelfHealthPercent,
	TargetHealthPercent,
	DesiredRoleTags,
	TargetStateTags,
	CustomBool,
	CustomFloat
};

UENUM(BlueprintType)
enum class EPaper2DPlusCombatConsiderationOp : uint8
{
	/** Score is 1 inside MinValue..MaxValue and falls off outside the window. */
	RangeWindow,

	/** Score rises from 0 at MinValue to 1 at MaxValue. */
	Linear,

	/** Score falls from 1 at MinValue to 0 at MaxValue. */
	LinearInverse,

	/** Score is 1 when the bool value matches bExpectedBool. */
	BoolEquals,

	/** Score is 1 when any RequiredTags are present. */
	TagAny,

	/** Score is 1 when every RequiredTags entry is present. */
	TagAll
};

UENUM(BlueprintType)
enum class EPaper2DPlusCombatScoreCombineMode : uint8
{
	Multiply,
	Add
};

/** Outcome of the current move so far. Moved here from Paper2DPlusMoveTransition.h by TASK-108 U1 —
 *  the Combat Profile is the concept's designated home (dormant foundation; see the Transitions block
 *  on UPaper2DPlusCombatProfileAsset). RESERVED / INERT — no runtime driver reads it yet; kept declared
 *  (BlueprintType) so existing assets/graphs referencing it stay loadable. None = nothing connected yet. */
UENUM(BlueprintType)
enum class EPaper2DPlusMoveOutcome : uint8
{
	/** Nothing reported yet — the move has whiffed so far. */
	None UMETA(DisplayName = "None (Whiff So Far)"),

	/** The game reported the move CONNECTED (its damage pipeline adjudicated a hit). */
	Hit UMETA(DisplayName = "Hit"),

	/** The game reported the move was BLOCKED. */
	Block UMETA(DisplayName = "Block")
};

/** Branching condition on a move→move link. Moved here from Paper2DPlusMoveTransition.h by TASK-108 U1
 *  (same module, so serialized enum references stay resolvable). The transition rows themselves are pure
 *  From→To now — this enum types the DORMANT Combat Profile foundation rows below and the transition
 *  rows' Condition_DEPRECATED field (asset loadability). Always = the neutral default. */
UENUM(BlueprintType)
enum class EPaper2DPlusTransitionCondition : uint8
{
	/** Unconditional — resolves regardless of outcome (the historical default). */
	Always UMETA(DisplayName = "Always"),

	/** (Reserved) A HIT has been reported for the current move. */
	OnHit UMETA(DisplayName = "On Hit"),

	/** (Reserved) A BLOCK has been reported for the current move. */
	OnBlock UMETA(DisplayName = "On Block"),

	/** Only while NOTHING has been reported for the current move (outcome None) — the
	 *  whiff-cancel reading. */
	OnWhiff UMETA(DisplayName = "On Whiff")
};

/**
 * DORMANT FOUNDATION (TASK-108 U1 / R14) — one outcome-condition rule: names a From→To move pair on
 * the linked CharacterProfile and the outcome condition under which a future Combat-Profile
 * outcome-branch would allow it. Re-homes the concept the transition rows' removed Condition field
 * carried. Authored + serialized but CONSUMED BY NOTHING yet — do not build runtime reads against it
 * without a design pass.
 */
USTRUCT(BlueprintType)
struct PAPER2DPLUS_API FPaper2DPlusTransitionOutcomeRule
{
	GENERATED_BODY()

	/** The FROM move name on the linked CharacterProfile (case-insensitive, like all move names). */
	UPROPERTY(EditAnywhere, Category = "Combat Profile|Transitions")
	FString FromMove;

	/** The TO move name on the linked CharacterProfile. */
	UPROPERTY(EditAnywhere, Category = "Combat Profile|Transitions")
	FString TargetMove;

	/** The outcome condition under which the From→To link would be taken/allowed. */
	UPROPERTY(EditAnywhere, Category = "Combat Profile|Transitions")
	EPaper2DPlusTransitionCondition Condition = EPaper2DPlusTransitionCondition::Always;
};

/**
 * DORMANT FOUNDATION (TASK-108 U1 / R14) — one cancel-category registry entry. Re-homes the concept
 * the transition rows' removed CancelCategory field carried: a named cancel window whose gate curve
 * follows the "Cancel_<Category>" convention (FFlipbookTransitionData::MakeCancelCurveName) in the
 * from-move's CurveData. Authored + serialized but CONSUMED BY NOTHING yet.
 */
USTRUCT(BlueprintType)
struct PAPER2DPLUS_API FPaper2DPlusCancelCategoryEntry
{
	GENERATED_BODY()

	/** The category name — enter just the category (e.g. "Normal"); the gate curve is "Cancel_Normal". */
	UPROPERTY(EditAnywhere, Category = "Combat Profile|Transitions")
	FName Category;

	/** Designer note for what this cancel window means (never read by code). */
	UPROPERTY(EditAnywhere, Category = "Combat Profile|Transitions", meta = (MultiLine = true))
	FString Description;
};

// Stores one combat-variable value. A single value struct holds every supported variable type (only the field
// matching the variable's EPaper2DPlusCombatVariableType is meaningful). This is a plain reflected struct — no
// StructUtils PropertyBag — so the Combat Profile variable system compiles and serialises identically on UE 5.0-5.7.
USTRUCT(BlueprintType)
struct PAPER2DPLUS_API FPaper2DPlusCombatVariableValue
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat Profile")
	bool BoolValue = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat Profile")
	int32 IntValue = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat Profile")
	float FloatValue = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat Profile")
	FName NameValue;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat Profile")
	FString StringValue;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat Profile")
	FGameplayTag TagValue;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat Profile")
	FGameplayTagContainer TagContainerValue;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat Profile")
	FVector2D Vector2DValue = FVector2D::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat Profile")
	FVector VectorValue = FVector::ZeroVector;
};

USTRUCT(BlueprintType)
struct PAPER2DPLUS_API FPaper2DPlusCombatValidationIssue
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Combat Profile")
	EPaper2DPlusCombatValidationSeverity Severity = EPaper2DPlusCombatValidationSeverity::Info;

	UPROPERTY(BlueprintReadOnly, Category = "Combat Profile")
	FText Message;

	UPROPERTY(BlueprintReadOnly, Category = "Combat Profile")
	FName Field;

	UPROPERTY(BlueprintReadOnly, Category = "Combat Profile")
	FName MoveName;
};

USTRUCT(BlueprintType)
struct PAPER2DPLUS_API FPaper2DPlusCombatVariableDefinition
{
	GENERATED_BODY()

	/** Stable identity for this scoring variable. Pick or create a tag under Paper2DPlus.Combat.Var — the
	 *  tag (not a free-form name) is what considerations and the runtime context reference, so it stays
	 *  searchable and rename-safe. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat Profile", meta = (Categories = "Paper2DPlus.Combat.Var"))
	FGameplayTag VariableTag;

	/** Legacy free-form identity — folded into VariableTag by PostLoad (best-effort: resolves a registered
	 *  Paper2DPlus.Combat.Var.<Name> tag). Retained only for deserializing pre-tag assets. */
	UPROPERTY()
	FName VariableName_DEPRECATED;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat Profile")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat Profile")
	EPaper2DPlusCombatVariableType Type = EPaper2DPlusCombatVariableType::Float;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat Profile")
	FText Description;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat Profile")
	FName Category;
};

USTRUCT(BlueprintType)
struct PAPER2DPLUS_API FPaper2DPlusCombatConsideration
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat Profile")
	FName ConsiderationName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat Profile")
	EPaper2DPlusCombatConsiderationSource Source = EPaper2DPlusCombatConsiderationSource::DistanceToTarget;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat Profile")
	EPaper2DPlusCombatConsiderationOp Operation = EPaper2DPlusCombatConsiderationOp::RangeWindow;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat Profile")
	EPaper2DPlusCombatScoreCombineMode CombineMode = EPaper2DPlusCombatScoreCombineMode::Multiply;

	/** For CustomBool / CustomFloat sources: the variable (by Paper2DPlus.Combat.Var tag) this rule reads. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat Profile", meta = (Categories = "Paper2DPlus.Combat.Var"))
	FGameplayTag VariableTag;

	/** Legacy free-form variable reference — folded into VariableTag by PostLoad. */
	UPROPERTY()
	FName VariableName_DEPRECATED;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat Profile")
	FGameplayTagContainer RequiredTags;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat Profile")
	float MinValue = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat Profile")
	float MaxValue = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat Profile", meta = (ClampMin = "0.0"))
	float Weight = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat Profile")
	bool bExpectedBool = true;
};

USTRUCT(BlueprintType)
struct PAPER2DPLUS_API FPaper2DPlusCombatTagDefaults
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat Profile")
	FGameplayTag AttackTag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat Profile")
	FGameplayTagContainer RoleTags;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat Profile", meta = (ClampMin = "0.0"))
	float BaseWeight = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat Profile")
	bool bOverridePreferredRange = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat Profile", meta = (EditCondition = "bOverridePreferredRange"))
	FVector2D PreferredRangeLocal = FVector2D::ZeroVector;

	/** Sparse tag-scope overrides. Missing keys inherit from GlobalVariables. */
	UPROPERTY(EditAnywhere, Category = "Combat Profile")
	TMap<FGameplayTag, FPaper2DPlusCombatVariableValue> Variables;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat Profile")
	TArray<FPaper2DPlusCombatConsideration> Considerations;
};

USTRUCT(BlueprintType)
struct PAPER2DPLUS_API FPaper2DPlusCombatAttackOption
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat Profile")
	FName MoveName;

	/** Canonical move identity. MoveName remains a cached display/legacy fallback for old assets. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat Profile")
	TSoftObjectPtr<UPaperFlipbook> MoveFlipbook;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat Profile")
	bool bIncludeWhenNotTagged = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat Profile")
	FGameplayTag AttackTag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat Profile")
	FGameplayTagContainer RoleTags;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat Profile", meta = (ClampMin = "0.0"))
	float BaseWeight = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat Profile", meta = (ClampMin = "0.0"))
	float CooldownSeconds = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat Profile")
	bool bOverridePreferredRange = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat Profile", meta = (EditCondition = "bOverridePreferredRange"))
	FVector2D PreferredRangeLocal = FVector2D::ZeroVector;

	/** Sparse move-scope overrides. Missing keys inherit from tag defaults, then GlobalVariables. */
	UPROPERTY(EditAnywhere, Category = "Combat Profile")
	TMap<FGameplayTag, FPaper2DPlusCombatVariableValue> Variables;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat Profile")
	TArray<FPaper2DPlusCombatConsideration> Considerations;
};

USTRUCT(BlueprintType)
struct PAPER2DPLUS_API FPaper2DPlusCombatScoringProfile
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat Profile")
	FName ProfileName = TEXT("Default");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat Profile", meta = (ClampMin = "0.0"))
	float MinimumViableScore = 0.01f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat Profile")
	bool bUseWeightedSelection = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat Profile")
	TArray<FPaper2DPlusCombatConsideration> GlobalConsiderations;
};

USTRUCT(BlueprintType)
struct PAPER2DPLUS_API FPaper2DPlusCombatRuntimeContext
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat Profile")
	float DistanceToTarget = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat Profile")
	float SelfHealthPercent = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat Profile")
	float TargetHealthPercent = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat Profile")
	FGameplayTagContainer DesiredRoleTags;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat Profile")
	FGameplayTagContainer TargetStateTags;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat Profile")
	TArray<FName> RecentMoves;

	/** Sparse runtime/scenario overrides. Missing keys fall through to move, tag, then global values. */
	UPROPERTY(EditAnywhere, Category = "Combat Profile")
	TMap<FGameplayTag, FPaper2DPlusCombatVariableValue> RuntimeVariables;
};

USTRUCT(BlueprintType)
struct PAPER2DPLUS_API FPaper2DPlusCombatAttackDerivedData
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Combat Profile")
	FName MoveName;

	UPROPERTY(BlueprintReadOnly, Category = "Combat Profile")
	FGameplayTag AttackTag;

	UPROPERTY(BlueprintReadOnly, Category = "Combat Profile")
	FGameplayTagContainer RoleTags;

	UPROPERTY(BlueprintReadOnly, Category = "Combat Profile")
	bool bInheritedFromCharacterProfile = false;

	UPROPERTY(BlueprintReadOnly, Category = "Combat Profile")
	bool bHasCombatProfileOption = false;

	UPROPERTY(BlueprintReadOnly, Category = "Combat Profile")
	FBox2D LocalAttackBounds = FBox2D(ForceInit);

	UPROPERTY(BlueprintReadOnly, Category = "Combat Profile")
	FBox2D EffectiveAttackBoundsLocal = FBox2D(ForceInit);

	UPROPERTY(BlueprintReadOnly, Category = "Combat Profile")
	FVector2D HitboxForwardRangeLocal = FVector2D::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Combat Profile")
	FVector2D RootMotionAttackOffsetRangeLocal = FVector2D::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Combat Profile")
	FVector2D ForwardRangeLocal = FVector2D::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Combat Profile")
	FVector2D PreferredRangeLocal = FVector2D::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Combat Profile")
	FPaper2DPlusMoveFrameData FrameData;

	UPROPERTY(BlueprintReadOnly, Category = "Combat Profile")
	float BaseWeight = 1.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Combat Profile")
	float CooldownSeconds = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Combat Profile")
	bool bHasAttack = false;
};

USTRUCT(BlueprintType)
struct PAPER2DPLUS_API FPaper2DPlusCombatScoreTerm
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Combat Profile")
	FName TermName;

	UPROPERTY(BlueprintReadOnly, Category = "Combat Profile")
	float RawValue = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Combat Profile")
	float NormalizedValue = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Combat Profile")
	float Weight = 1.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Combat Profile")
	EPaper2DPlusCombatScoreCombineMode CombineMode = EPaper2DPlusCombatScoreCombineMode::Multiply;
};

USTRUCT(BlueprintType)
struct PAPER2DPLUS_API FPaper2DPlusCombatScoreBreakdown
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Combat Profile")
	FName MoveName;

	UPROPERTY(BlueprintReadOnly, Category = "Combat Profile")
	float FinalScore = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Combat Profile")
	TArray<FPaper2DPlusCombatScoreTerm> Terms;
};

USTRUCT(BlueprintType)
struct PAPER2DPLUS_API FPaper2DPlusCombatRankedOption
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Combat Profile")
	FPaper2DPlusCombatAttackDerivedData Attack;

	UPROPERTY(BlueprintReadOnly, Category = "Combat Profile")
	float Score = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Combat Profile")
	FPaper2DPlusCombatScoreBreakdown Breakdown;
};

USTRUCT(BlueprintType)
struct PAPER2DPLUS_API FPaper2DPlusCombatDecision
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Combat Profile")
	bool bHasGoodAttack = false;

	UPROPERTY(BlueprintReadOnly, Category = "Combat Profile")
	FName BestMove;

	UPROPERTY(BlueprintReadOnly, Category = "Combat Profile")
	FGameplayTag BestAttackTag;

	UPROPERTY(BlueprintReadOnly, Category = "Combat Profile")
	FVector2D DesiredRangeLocal = FVector2D::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Combat Profile")
	float CurrentDistance = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Combat Profile")
	float BestScore = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Combat Profile")
	FPaper2DPlusCombatScoreBreakdown BestBreakdown;

	UPROPERTY(BlueprintReadOnly, Category = "Combat Profile")
	TArray<FPaper2DPlusCombatRankedOption> RankedOptions;
};

USTRUCT(BlueprintType)
struct PAPER2DPLUS_API FPaper2DPlusCombatScenarioPreset
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat Profile")
	FName PresetName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat Profile")
	FPaper2DPlusCombatRuntimeContext Context;

	/** Scoring rules are part of the scenario input required to reproduce its ranking. None uses Default. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat Profile")
	FName ScoringProfileName;

	/** Optional Combat Lab participant inputs. Playback/frame clocks remain transient editor state. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat Profile|Lab")
	FName AttackerMove;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat Profile|Lab")
	FName DefenderMove;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat Profile|Lab")
	FVector2D AttackerPosition = FVector2D(-75.0f, 0.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat Profile|Lab")
	FVector2D DefenderPosition = FVector2D(75.0f, 0.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat Profile|Lab", meta = (ClampMin = "0", ClampMax = "1"))
	int32 SelectedParticipant = 0;
};
