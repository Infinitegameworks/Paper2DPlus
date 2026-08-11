// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AssetRegistry/AssetData.h"
#include "Paper2DPlusCharacterCatalogAsset.h"

class UFactory;
class UObject;

/** Passive relationship state for one Layer or Combat companion slot. */
enum class EPaper2DPlusProfileRelationshipState : uint8
{
	None,
	Unique,
	Ambiguous,
	LegacyUnknown,
	ManualValid,
	/** The companion exists and names a DIFFERENT Character Profile — a real contradiction. */
	ManualMismatch,
	/**
	 * The companion exists but names no Character Profile at all.
	 *
	 * Distinct from ManualMismatch because it is an ordinary half-finished authoring state, not a
	 * contradiction: Layer and Combat assets always publish the inward relationship tag, writing the
	 * literal "None" when their own link is unset, so a freshly created companion assigned to a
	 * Catalog row before its link is filled in lands here. Collapsing it into ManualMismatch made the
	 * Catalog report a hard Error — and fail the validation gate — for a project with no defect.
	 */
	ManualUnlinked,
	MissingAssignedAsset
};

/** One path-only relationship candidate. Building this record never loads the asset. */
struct PAPER2DPLUSEDITOR_API FPaper2DPlusProfileRelationshipCandidate
{
	EPaper2DPlusCatalogCompanion Companion = EPaper2DPlusCatalogCompanion::Layer;
	FAssetData AssetData;
	FSoftObjectPath AssetPath;
	FSoftObjectPath CharacterProfilePath;
	bool bFromLegacyInspection = false;
};

/** Result consumed by Catalog discovery, audit, and related-profile UI. */
struct PAPER2DPLUSEDITOR_API FPaper2DPlusProfileRelationshipResolution
{
	EPaper2DPlusProfileRelationshipState State = EPaper2DPlusProfileRelationshipState::None;
	FSoftObjectPath AssignedAssetPath;
	FSoftObjectPath SuggestedAssetPath;
	TArray<FSoftObjectPath> CandidatePaths;
	bool bManualAssignmentPreserved = false;

	bool IsAmbiguous() const { return State == EPaper2DPlusProfileRelationshipState::Ambiguous; }
	bool IsLegacyUnknown() const { return State == EPaper2DPlusProfileRelationshipState::LegacyUnknown; }
};

/**
 * Path-sorted Layer/Combat candidate index. The searchable Character Profile tag is the passive
 * source of truth. LegacyAssets contains only assets missing that tag and is inspected solely by
 * an explicit Sync/Validate call.
 */
struct PAPER2DPLUSEDITOR_API FPaper2DPlusProfileRelationshipIndex
{
	TArray<FPaper2DPlusProfileRelationshipCandidate> LayerCandidates;
	TArray<FPaper2DPlusProfileRelationshipCandidate> CombatCandidates;
	TArray<FAssetData> LegacyAssets;

	const TArray<FPaper2DPlusProfileRelationshipCandidate>& GetCandidates(
		EPaper2DPlusCatalogCompanion Companion) const;
	const FPaper2DPlusProfileRelationshipCandidate* FindByAssetPath(
		EPaper2DPlusCatalogCompanion Companion,
		const FSoftObjectPath& AssetPath) const;
	void AddCandidate(FPaper2DPlusProfileRelationshipCandidate Candidate);
	void SortDeterministically();
};

/**
 * Shared editor-only owner of companion relationship extraction and related-asset actions.
 *
 * Layer and Combat emit the same Paper2DPlus.CharacterProfile Asset Registry tag. Effect is
 * deliberately absent: its Character assignment is owned by a Catalog row, and its retired
 * deprecated back-link is never consulted by current code.
 */
class PAPER2DPLUSEDITOR_API FProfileRelationshipService
{
public:
	using FExplicitAssetLoader = TFunction<UObject*(const FAssetData&)>;

	static const FName& CharacterProfileRelationshipTag();
	static FString NormalizeObjectPath(const FSoftObjectPath& Path);
	static FSoftObjectPath GetAssetObjectPath(const FAssetData& AssetData);

	/** Returns true only for Layer/Combat assets. This is path/class metadata only and never loads. */
	static bool GetCompanionKind(
		const FAssetData& AssetData,
		EPaper2DPlusCatalogCompanion& OutCompanion);

	/**
	 * Reads the stable relationship tag. bOutTagPresent distinguishes a resaved empty relationship
	 * (the tag value "None") from a legacy asset for which the tag does not exist.
	 */
	static FSoftObjectPath ExtractRelationshipPath(
		const FAssetData& AssetData,
		bool& bOutTagPresent);

	/** Pure, registry-order-independent construction over FAssetData. Never calls GetAsset(). */
	static FPaper2DPlusProfileRelationshipIndex BuildCandidateIndex(
		const TArray<FAssetData>& Assets);

	/**
	 * Explicit compatibility bridge. Loads only Index.LegacyAssets, adds any recovered Layer/Combat
	 * relationships to the transient index, and never mutates or dirties those assets.
	 */
	static int32 InspectLegacyRelationships(
		FPaper2DPlusProfileRelationshipIndex& Index,
		const FExplicitAssetLoader& Loader = FExplicitAssetLoader());

	/**
	 * Report the state of an AUTHORED assignment: the assignment is always retained, and the result
	 * says whether it matches this Character (ManualValid), points elsewhere (ManualMismatch), lacks
	 * the relationship tag (LegacyUnknown), or no longer exists as its class (MissingAssignedAsset).
	 */
	static FPaper2DPlusProfileRelationshipResolution ResolveAssigned(
		EPaper2DPlusCatalogCompanion Companion,
		const FSoftObjectPath& CharacterProfilePath,
		const FSoftObjectPath& AssignedAssetPath,
		const FPaper2DPlusProfileRelationshipIndex& Index);

	/**
	 * Suggest a companion for an EMPTY slot: exactly one path-sorted inward match is Unique with a
	 * SuggestedAssetPath; zero is None; more than one is Ambiguous and never guesses. An untagged
	 * legacy asset of the same kind makes even one known match LegacyUnknown rather than Unique.
	 */
	static FPaper2DPlusProfileRelationshipResolution SuggestCandidate(
		EPaper2DPlusCatalogCompanion Companion,
		const FSoftObjectPath& CharacterProfilePath,
		const FPaper2DPlusProfileRelationshipIndex& Index);

	/** Explicit designer action. Loads and opens only the requested asset. */
	static bool OpenRelatedAsset(const FSoftObjectPath& AssetPath, FText& OutError);

	/**
	 * Explicit guided creation. Layer/Combat receive their Character relationship; Effect deliberately
	 * receives no inward link. The new asset is left dirty for Unreal's ordinary Save workflow.
	 */
	static UObject* CreateRelatedAsset(
		EPaper2DPlusCatalogCompanion Companion,
		const FSoftObjectPath& CharacterProfilePath,
		const FString& DestinationPackagePath,
		const FString& DesiredAssetName,
		FText& OutError);
};

