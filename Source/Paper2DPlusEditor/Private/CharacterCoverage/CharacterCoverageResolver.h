// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Paper2DPlusAnimationTagQuery.h"
#endif

class UPaper2DPlusCharacterProfileAsset;

/** Coverage state for one Catalog-declared expected animation tag. */
enum class ECharacterCoverageStatus : uint8
{
	Missing,
	NearMiss,
	Covered
};

/**
 * Read-only projection for one expected tag.
 *
 * Only AuthoredAnimationNames can make the row Covered. The two provenance arrays remain separate
 * so consumers can explain why an effective tag is merely carried rather than authored.
 */
struct FCharacterCoverageRow
{
	FGameplayTag ExpectedTag;
	ECharacterCoverageStatus Status = ECharacterCoverageStatus::Missing;
	TArray<FString> AuthoredAnimationNames;
	TArray<FString> GroupImpliedAnimationNames;
	TArray<FString> ChainInheritedAnimationNames;

	/** True only when authored coverage exists but every covering animation also authors other tags. */
	bool bSupersetOnly = false;
};

/** Deterministic bulk result for one profile and one complete expected-tag set. */
struct FCharacterCoverageResolveResult
{
	TArray<FCharacterCoverageRow> Rows;

#if WITH_DEV_AUTOMATION_TESTS
	/** Sorted canonical names visited while consuming the built tag map; one entry per map row. */
	TArray<FString> VisitedAnimationNamesForTests;
#endif
};

/**
 * Editor-private, pure/worldless coverage projection. It never loads assets, opens transactions,
 * mutates the profile, or stores persistent state.
 */
class FCharacterCoverageResolver
{
public:
	/** The sole production entry point. Builds the runtime tag map exactly once for this resolve. */
	static FCharacterCoverageResolveResult Resolve(
		const UPaper2DPlusCharacterProfileAsset* Profile,
		const FGameplayTagContainer& ExpectedTags);

#if WITH_DEV_AUTOMATION_TESTS
	/**
	 * Narrow decision-table seam for provenance combinations that cannot naturally exist across a
	 * complete profile (a chain-inherited tag always originates on its in-profile root).
	 */
	static FCharacterCoverageResolveResult ResolveFromTagMapForTests(
		const TMap<FString, Paper2DPlusAnimationTagQuery::FAnimationTagSet>& TagMap,
		const FGameplayTagContainer& ExpectedTags);
#endif
};
