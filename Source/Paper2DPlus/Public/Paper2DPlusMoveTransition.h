// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Paper2DPlusCombatProfileTypes.h" // EPaper2DPlusTransitionCondition (deprecated-field type; the enums' home since TASK-108)
#include "Paper2DPlusFrameCurve.h"
#include "Paper2DPlusMoveTransition.generated.h"

/**
 * One move→move transition edge. Identity and topology remain the pure From→To pair; the optional
 * PhaseTagOverride is descriptive resolution metadata and never grants permission or drives playback.
 *
 * Lives on the FROM move's FFlipbookProfileEntry (via FFlipbookTransitionData), pointing at another
 * move on the SAME asset by flipbook name. Paper2DPlus stores these as pure data — the editor's
 * Animation Map draws them and Paper2DPlusComboChain derives combos from them; game code / PaperZD
 * performs every actual switch.
 *
 * INVARIANT (TASK-108): at most ONE row per (owning flipbook, TargetMove case-insensitive) pair.
 * Enforced by the load/import migration (UPaper2DPlusCharacterProfileAsset::MigrateMoveTransitions /
 * DedupeTransitionRows — re-run after renames) and expected of every row-creation path.
 */
USTRUCT(BlueprintType)
struct PAPER2DPLUS_API FPaper2DPlusMoveTransition
{
	GENERATED_BODY()

	/** Target flipbook name on the SAME asset (FFlipbookIdentity::FlipbookName), compared
	 *  case-insensitively like all move names. Dangling names are flagged (Warning) by
	 *  ValidateCharacterProfileAsset; empty = authoring-in-progress row, skipped by all queries. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Transitions")
	FString TargetMove;

	/** Optional phase owned by this exact From -> To transition. Empty preserves existing assets by
	 *  inheriting the target animation's EditorMeta.PhaseTag; once authored, converging transitions
	 *  may describe different phases without mutating one another or the target animation. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Transitions",
		meta = (GameplayTagFilter = "Paper2DPlus.Phase"))
	FGameplayTag PhaseTagOverride;

	/** One shared compatibility rule for runtime queries and editor projection. */
	FGameplayTag GetEffectivePhaseTag(const FGameplayTag& TargetAnimationPhaseTag) const
	{
		return PhaseTagOverride.IsValid() ? PhaseTagOverride : TargetAnimationPhaseTag;
	}

	// --- Soft-deprecated fields (TASK-108 U1) ---
	// UHT registers "_DEPRECATED" members under their BARE legacy names, so old .uassets AND legacy
	// JSON keys ("Tag"/"CancelCategory"/"Condition") still deserialize here; MigrateMoveTransitions
	// then DROPS the values with an Info log (accepted data loss, user-decided). The outcome-condition
	// and cancel-category CONCEPTS re-homed as dormant foundation data on UPaper2DPlusCombatProfileAsset.
	// Do NOT reference in new code.

	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Transition names were removed (TASK-108) - transitions are pure From->To arrows. Loaded values are dropped with a log."))
	FName Tag_DEPRECATED;

	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Cancel categories moved to the Combat Profile (dormant foundation, TASK-108). Loaded values are dropped with a log."))
	FName CancelCategory_DEPRECATED;

	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Outcome conditions moved to the Combat Profile (dormant foundation, TASK-108). Loaded values are dropped with a log."))
	EPaper2DPlusTransitionCondition Condition_DEPRECATED = EPaper2DPlusTransitionCondition::Always;

	FPaper2DPlusMoveTransition() = default;
	explicit FPaper2DPlusMoveTransition(const FString& InTargetMove)
		: TargetMove(InTargetMove) {}
};

/**
 * Sub-struct holding all outgoing move→move transitions for one animation (TASK-76). Sibling to the
 * other FFlipbookProfileEntry sub-structs (FFlipbookCurveData / FFlipbookMotionData). Empty by default
 * and additive-optional, so existing assets deserialize byte-identically (no schema bump, no migration).
 */
USTRUCT(BlueprintType)
struct PAPER2DPLUS_API FFlipbookTransitionData
{
	GENERATED_BODY()

	/** Outgoing transitions in authored order. One row per From→To pair (the TASK-108 dedupe
	 *  invariant); rows may own a phase override, while empty-target drafts are skipped everywhere. */
	UPROPERTY(EditAnywhere, Category = "Transitions")
	TArray<FPaper2DPlusMoveTransition> Transitions;

	/** Legacy input-buffer override. Retained under its original reflected name so historical assets
	 *  and JSON load; the transition driver was removed and current code must not read or write it. */
	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "The Paper2DPlus transition driver and input buffer were removed; this value is ignored."))
	int32 BufferGraceFramesOverride_DEPRECATED = -1;

	/** True if any transition is authored on this animation. */
	bool HasTransitions() const { return Transitions.Num() > 0; }

	/** The literal cancel-gate curve-name prefix — the SINGLE source of the "Cancel_" convention shared
	 *  by name composition (MakeCancelCurveName) and detection (IsCancelCurveName). Editor code that
	 *  special-cases cancel curves (Constant-mode seeding, fixed 0/1 track rows) must go through
	 *  IsCancelCurveName instead of re-hardcoding the string. */
	static constexpr const TCHAR* CancelCurvePrefix = TEXT("Cancel_");

	/** Compose the gate-curve name for a cancel category — THE single source of the "Cancel_<Category>"
	 *  naming convention (TASK-76 PR2). Authored-data/registry convention only since the driver removal;
	 *  the cancel-category concept's future home is the Combat Profile (TASK-108 dormant foundation). */
	static FName MakeCancelCurveName(FName CancelCategory)
	{
		return FName(FString::Printf(TEXT("%s%s"), CancelCurvePrefix, *CancelCategory.ToString()));
	}

	/** True when CurveName follows the cancel-gate naming convention ("Cancel_*", case-insensitive).
	 *  The editor uses this to seed new Cancel_* curves with Constant mode and to give their track rows
	 *  the fixed 0..1 step-curve treatment — single-source so the convention can't drift from
	 *  MakeCancelCurveName. */
	static bool IsCancelCurveName(FName CurveName)
	{
		return CurveName.ToString().StartsWith(CancelCurvePrefix, ESearchCase::IgnoreCase);
	}

};
