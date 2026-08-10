// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "Paper2DPlusCharacterCatalogAsset.generated.h"

class UPaper2DPlusCharacterLayerAsset;
class UPaper2DPlusCharacterProfileAsset;
class UPaper2DPlusCombatProfileAsset;
class UPaper2DPlusEffectProfileAsset;

/** Companion slots carried by a cooked Character Catalog entry. */
UENUM(BlueprintType)
enum class EPaper2DPlusCatalogCompanion : uint8
{
	Layer,
	Effect,
	Combat
};

UENUM(BlueprintType)
enum class EPaper2DPlusCharacterCatalogIssueSeverity : uint8
{
	Info,
	Warning,
	Error
};

/** Which optional companion slots become requirements for this character. */
USTRUCT(BlueprintType)
struct PAPER2DPLUS_API FPaper2DPlusCharacterCatalogRequirements
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Character Catalog|Requirements")
	bool bRequireLayer = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Character Catalog|Requirements")
	bool bRequireEffect = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Character Catalog|Requirements")
	bool bRequireCombat = false;
};

/** Pure, cooked completion state. Presence means a non-null soft path; it never loads an asset. */
USTRUCT(BlueprintType)
struct PAPER2DPLUS_API FPaper2DPlusCharacterCatalogCompletion
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Character Catalog|Completion")
	bool bComplete = true;

	UPROPERTY(BlueprintReadOnly, Category = "Character Catalog|Completion")
	int32 RequiredCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Character Catalog|Completion")
	int32 PresentRequiredCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Character Catalog|Completion")
	TArray<EPaper2DPlusCatalogCompanion> MissingRequiredCompanions;
};

/** One character in the saved, cook-safe Catalog snapshot. CharacterProfile is the stable identity. */
USTRUCT(BlueprintType)
struct PAPER2DPLUS_API FPaper2DPlusCharacterCatalogEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Character Catalog")
	TSoftObjectPtr<UPaper2DPlusCharacterProfileAsset> CharacterProfile;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Character Catalog|Companions")
	TSoftObjectPtr<UPaper2DPlusCharacterLayerAsset> LayerProfile;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Character Catalog|Companions")
	TSoftObjectPtr<UPaper2DPlusEffectProfileAsset> EffectProfile;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Character Catalog|Companions")
	TSoftObjectPtr<UPaper2DPlusCombatProfileAsset> CombatProfile;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Character Catalog|Requirements")
	FPaper2DPlusCharacterCatalogRequirements Requirements;

	/** Project-defined classification. Tags never imply or mutate named group membership. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Character Catalog|Organization")
	FGameplayTagContainer Tags;
};

/** Explicit, ordered character membership. Tags do not drive this list. */
USTRUCT(BlueprintType)
struct PAPER2DPLUS_API FPaper2DPlusCharacterCatalogGroup
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Character Catalog|Groups")
	FName GroupName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Character Catalog|Groups")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Character Catalog|Groups")
	TArray<TSoftObjectPtr<UPaper2DPlusCharacterProfileAsset>> Members;

	/** Animation tags expected in addition to the Catalog-wide list for every member of this group. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Character Catalog|Groups", meta = (Categories = "Paper2DPlus.Animation", ToolTip = "Animation tags expected in addition to the Catalog-wide list for every character in this group."))
	FGameplayTagContainer AdditionalExpectedAnimationTags;
};

/** Stable structural truth emitted by the runtime asset without any editor service dependency. */
USTRUCT(BlueprintType)
struct PAPER2DPLUS_API FPaper2DPlusCharacterCatalogIssue
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Character Catalog|Validation")
	EPaper2DPlusCharacterCatalogIssueSeverity Severity = EPaper2DPlusCharacterCatalogIssueSeverity::Info;

	UPROPERTY(BlueprintReadOnly, Category = "Character Catalog|Validation")
	FName Code = NAME_None;

	UPROPERTY(BlueprintReadOnly, Category = "Character Catalog|Validation")
	FSoftObjectPath CharacterPath;

	UPROPERTY(BlueprintReadOnly, Category = "Character Catalog|Validation")
	FName Scope = NAME_None;

	UPROPERTY(BlueprintReadOnly, Category = "Character Catalog|Validation")
	FName Field = NAME_None;

	UPROPERTY(BlueprintReadOnly, Category = "Character Catalog|Validation")
	FText Message;
};

/**
 * Cook-safe, soft-first directory of the project's authored characters.
 *
 * Runtime queries preserve authored Catalog order, never synchronously load a referenced object,
 * never spawn actors, and never choose an encounter. Avoid running full-scan tag queries every tick.
 */
UCLASS(BlueprintType)
class PAPER2DPLUS_API UPaper2DPlusCharacterCatalogAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Character Catalog", meta = (TitleProperty = "CharacterProfile"))
	TArray<FPaper2DPlusCharacterCatalogEntry> Entries;

	/** Animation tags every character in this Catalog is expected to author in its Character Profile. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Character Catalog|Animation Coverage", meta = (Categories = "Paper2DPlus.Animation", ToolTip = "Animation tags every character in this Catalog is expected to author in its Character Profile."))
	FGameplayTagContainer ExpectedAnimationTags;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Character Catalog|Groups", meta = (TitleProperty = "GroupName"))
	TArray<FPaper2DPlusCharacterCatalogGroup> Groups;

	virtual FPrimaryAssetId GetPrimaryAssetId() const override;

	static const FPrimaryAssetType& CharacterCatalogPrimaryAssetType();

	/** O(N). Returns unique, non-null entries in authored order; the first duplicate identity wins. */
	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Character Catalog")
	TArray<FPaper2DPlusCharacterCatalogEntry> GetCatalogEntries() const;

	/** O(N). Looks up a normalized Character Profile soft path without loading it. */
	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Character Catalog")
	bool FindEntryByCharacterProfile(
		TSoftObjectPtr<UPaper2DPlusCharacterProfileAsset> CharacterProfile,
		FPaper2DPlusCharacterCatalogEntry& OutEntry) const;

	/** O(N). Hierarchical matching accepts descendants; exact matching does not. Invalid tags match nothing. */
	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Character Catalog")
	TArray<FPaper2DPlusCharacterCatalogEntry> GetEntriesWithTag(FGameplayTag Tag, bool bExactMatch = false) const;

	/** O(N*M). Empty or invalid query containers match nothing. Results preserve Catalog order. */
	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Character Catalog")
	TArray<FPaper2DPlusCharacterCatalogEntry> GetEntriesWithAllTags(
		const FGameplayTagContainer& Tags,
		bool bExactMatch = false) const;

	/** O(N*M). Empty or invalid query containers match nothing. Results preserve Catalog order. */
	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Character Catalog")
	TArray<FPaper2DPlusCharacterCatalogEntry> GetEntriesWithAnyTags(
		const FGameplayTagContainer& Tags,
		bool bExactMatch = false) const;

	/** Returns unique non-empty group names in authored order; the first duplicate name wins. */
	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Character Catalog")
	TArray<FName> GetCatalogGroupNames() const;

	/** O(N+M). Preserves authored member order and skips null, out-of-Catalog, and repeated members. */
	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Character Catalog")
	TArray<FPaper2DPlusCharacterCatalogEntry> GetEntriesInGroup(FName GroupName) const;

	/**
	 * Returns the Catalog-wide expected tags plus every addressable member-group addition for one
	 * roster character. Group validity matches GetEntriesInGroup: an unnamed group or a later
	 * duplicate-named group is unaddressable and contributes nothing.
	 * Returns false and clears OutExpectedTags when CharacterProfile is not a normalized Catalog member.
	 * This query never loads referenced assets, caches results, or writes authored data.
	 */
	bool GetExpectedAnimationTagsForCharacter(
		TSoftObjectPtr<UPaper2DPlusCharacterProfileAsset> CharacterProfile,
		FGameplayTagContainer& OutExpectedTags) const;

	/** Computes required/present/missing state from soft-path presence only; no referenced object is loaded. */
	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Character Catalog")
	FPaper2DPlusCharacterCatalogCompletion GetEntryCompletion(
		const FPaper2DPlusCharacterCatalogEntry& Entry) const;

	/** Looks up a character and computes its pure runtime completion without loading linked assets. */
	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Character Catalog")
	bool GetCharacterCompletion(
		TSoftObjectPtr<UPaper2DPlusCharacterProfileAsset> CharacterProfile,
		FPaper2DPlusCharacterCatalogCompletion& OutCompletion) const;

	/** Read-only structural validation. It never discovers, Syncs, repairs, loads, or dirties assets. */
	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Character Catalog")
	bool ValidateCharacterCatalogAsset(TArray<FPaper2DPlusCharacterCatalogIssue>& OutIssues) const;

private:
	static FString NormalizeProfilePath(const FSoftObjectPath& Path);
	static bool TagsContain(const FGameplayTagContainer& Container, FGameplayTag Tag, bool bExactMatch);

	/**
	 * The one group-validity rule shared by GetEntriesInGroup and the expected-tag union: a name
	 * addresses at most one group. Returns null for an unnamed name; otherwise the first authored
	 * group carrying it, so later duplicate-named groups are never addressable.
	 */
	const FPaper2DPlusCharacterCatalogGroup* FindAddressableGroup(FName GroupName) const;
};
