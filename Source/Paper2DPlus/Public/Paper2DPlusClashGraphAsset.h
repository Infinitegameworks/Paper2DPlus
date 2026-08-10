// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Paper2DPlusClashTypes.h"
#include "Paper2DPlusClashGraphAsset.generated.h"

/**
 * The project-level clash matrix (TASK-77 U1) — a directed "what beats what" graph over hierarchical
 * category tags, stored as a FLAT edge list. ONE clash graph governs a whole game (referenced by
 * UPaper2DPlusSettings::DefaultClashGraph, overridable per game-mode/subsystem at runtime).
 *
 * The data is intentionally editor-agnostic: a stock details panel edits the flat Edges here; the U5
 * node-graph editor (and the computed outcome grid) are pure VIEWS over this same FClashGraph. Resolution
 * + the detect-and-warn conflict validator live in the pure Paper2DPlusClash core.
 */
UCLASS(BlueprintType)
class PAPER2DPLUS_API UPaper2DPlusClashGraphAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPaper2DPlusClashGraphAsset();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clash Graph")
	FString DisplayName;

	/** The flat "beats" edge list + the same-category default. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clash Graph")
	FClashGraph Graph;

#if WITH_EDITORONLY_DATA
	/** TASK-77 U5: editor-only node positions for the clash node-graph editor (the analogue of the Character
	 *  Profile's AnimationMapNodePositions). Keyed by category tag — also lets a tag node persist on the graph
	 *  with ZERO edges (an "Add Category" placement). WITH_EDITORONLY_DATA so it cook-strips automatically. */
	UPROPERTY()
	TMap<FGameplayTag, FVector2D> TagNodePositions;
#endif

	/** Resolve two categories against this asset's graph (BlueprintPure convenience over the pure core). */
	UFUNCTION(BlueprintPure, Category = "Paper2D+|Clash")
	EClashOutcome ResolveClash(FGameplayTag A, FGameplayTag B) const;

	/** The deduped, first-seen-order set of valid category tags appearing on the graph's edges (the concrete
	 *  tag set the validator + the U5 computed grid both enumerate). The single shared source. */
	void GetConcreteEdgeTags(TArray<FGameplayTag>& OutTags) const;

	/**
	 * Validate the graph for authored desync (the F1 detect-and-warn). Enumerates every concrete pair over
	 * the graph's edge tags PLUS ExtraCategories (e.g. categories assigned to project hitboxes, U2) and flags
	 * ambiguous (equal-specificity both-ways) pairs as Errors. Returns false iff any Error.
	 */
	UFUNCTION(BlueprintCallable, Category = "Paper2D+|Clash")
	bool ValidateClashGraphAsset(const TArray<FGameplayTag>& ExtraCategories, TArray<FClashValidationIssue>& OutIssues) const;
};
