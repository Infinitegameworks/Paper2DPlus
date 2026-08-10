// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Paper2DPlusClashTypes.generated.h"

/**
 * Hit-priority / clash-resolution data model (TASK-77 U1).
 *
 * The clash matrix is a DIRECTED "beats" graph stored as a FLAT edge list (FClashGraph) — a node-graph
 * editor (U5) is a pure VIEW over this same data. The pure resolver Paper2DPlusClash::ResolveClash and the
 * detect-and-warn ValidateClashGraph operate on these plain reflected types (cross-version 5.0–5.8 safe;
 * NO FInstancedPropertyBag).
 */

/** The outcome of resolving two overlapping hitbox categories. */
UENUM(BlueprintType)
enum class EClashOutcome : uint8
{
	/** The first category (A) wins outright — A connects, B is beaten clean. */
	AWins	UMETA(DisplayName = "A Wins"),
	/** The second category (B) wins outright. */
	BWins	UMETA(DisplayName = "B Wins"),
	/** Both land — mutual hit (the same-category default when the project trades). */
	Trade	UMETA(DisplayName = "Trade"),
	/** Both negate — no damage, both stay actionable (the same-category default when the project clashes). */
	Clash	UMETA(DisplayName = "Clash"),
	/** Neither connects — the boxes overlap but ignore each other (throw-vs-throw, projectile pass-through; U4). */
	Whiff	UMETA(DisplayName = "Whiff")
};

/** What two SAME-category (or no-rule) attacks do by default. A single project-wide choice. */
UENUM(BlueprintType)
enum class EClashSameCategoryDefault : uint8
{
	/** Both land (Street Fighter style). */
	Trade	UMETA(DisplayName = "Trade"),
	/** Both negate, both actionable (Guilty Gear style). */
	Clash	UMETA(DisplayName = "Clash")
};

/** One directed "Winner beats Loser" rule. Tags are hierarchical: a Strike edge applies to Strike.Heavy. */
USTRUCT(BlueprintType)
struct PAPER2DPLUS_API FClashEdge
{
	GENERATED_BODY()

	/** The category that WINS when it overlaps the Loser (hierarchy-matched). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clash", meta = (Categories = "Paper2DPlus.Clash.Category"))
	FGameplayTag Winner;

	/** The category that LOSES to the Winner. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clash", meta = (Categories = "Paper2DPlus.Clash.Category"))
	FGameplayTag Loser;
};

/** The whole clash matrix: a flat list of "beats" edges + the same-category default. */
USTRUCT(BlueprintType)
struct PAPER2DPLUS_API FClashGraph
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clash")
	TArray<FClashEdge> Edges;

	/** What two same-category (or no-rule) attacks do. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clash")
	EClashSameCategoryDefault Default = EClashSameCategoryDefault::Trade;
};

/** Severity of a clash-graph validation issue (mirrors ECharacterProfileValidationSeverity). */
UENUM(BlueprintType)
enum class EClashValidationSeverity : uint8
{
	Info	UMETA(DisplayName = "Info"),
	Warning UMETA(DisplayName = "Warning"),
	Error	UMETA(DisplayName = "Error")
};

/** One authored-desync (or advisory) finding from ValidateClashGraph. */
USTRUCT(BlueprintType)
struct PAPER2DPLUS_API FClashValidationIssue
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Validation")
	EClashValidationSeverity Severity = EClashValidationSeverity::Info;

	UPROPERTY(BlueprintReadOnly, Category = "Validation")
	FString Message;

	/** The two categories whose resolution is ambiguous/affected (for editor highlighting). */
	UPROPERTY(BlueprintReadOnly, Category = "Validation")
	FGameplayTag CategoryA;

	UPROPERTY(BlueprintReadOnly, Category = "Validation")
	FGameplayTag CategoryB;
};
