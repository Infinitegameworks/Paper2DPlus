// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "CharacterCatalogAuditService.h"
#include "Paper2DPlusValidationService.h"

struct FCharacterProfileValidationIssue;
struct FCharacterLayerValidationIssue;
struct FPaper2DPlusEffectProfileValidationIssue;
struct FPaper2DPlusCombatValidationIssue;

class UPaper2DPlusCharacterProfileAsset;
class UPaper2DPlusCharacterLayerAsset;
class UPaper2DPlusEffectProfileAsset;
class UPaper2DPlusCombatProfileAsset;

enum class EPaper2DPlusConfiguredCatalogReferenceProblem : uint8
{
	Unset,
	Missing,
	WrongClass
};

/** Built-in projections. Kept private so domain issue headers do not leak through the public service. */
namespace Paper2DPlusProfileValidationAdapter
{
	/** Immutable dependencies for one injected Catalog validation run. */
	struct FCatalogValidationContext
	{
		const TArray<FAssetData>* Assets = nullptr;
		const FPaper2DPlusCharacterCatalogSettingsSnapshot* Settings = nullptr;
		FCharacterCatalogAuditService::FNativeAssetResolver AssetResolver;
		FCharacterCatalogAuditService::FCookRegistrationInspector CookInspector;

		bool IsUsable() const { return Assets != nullptr && Settings != nullptr; }
	};

	/**
	 * Thread-local, nest-safe dependency injection used by commandlet integration automation. Normal
	 * editor and commandlet validation continue to read the live registry, settings, and Asset Manager.
	 */
	class FScopedCatalogValidationContext
	{
	public:
		explicit FScopedCatalogValidationContext(const FCatalogValidationContext& InContext);
		~FScopedCatalogValidationContext();

		FScopedCatalogValidationContext(const FScopedCatalogValidationContext&) = delete;
		FScopedCatalogValidationContext& operator=(const FScopedCatalogValidationContext&) = delete;

	private:
		const FCatalogValidationContext* PreviousContext = nullptr;
	};

	void RegisterBuiltInAdapters(FPaper2DPlusValidationService& Service);

	/**
	 * Project an unset or broken DefaultCharacterCatalog soft reference into the same stable issue vocabulary as
	 * object-backed Catalog validation. The caller owns Asset Registry classification; this function
	 * only creates data and never resolves, loads, or mutates the configured path.
	 */
	void ProjectConfiguredCatalogReferenceProblem(
		const FSoftObjectPath& ConfiguredCatalogPath,
		EPaper2DPlusConfiguredCatalogReferenceProblem Problem,
		FName ActualAssetType,
		TArray<FPaper2DPlusValidationIssue>& OutIssues);

	void ProjectCharacterIssues(
		const UPaper2DPlusCharacterProfileAsset& Asset,
		const TArray<FCharacterProfileValidationIssue>& SourceIssues,
		TArray<FPaper2DPlusValidationIssue>& OutIssues);

	void ProjectLayerIssues(
		const UPaper2DPlusCharacterLayerAsset& Asset,
		const TArray<FCharacterLayerValidationIssue>& SourceIssues,
		TArray<FPaper2DPlusValidationIssue>& OutIssues);

	void ProjectEffectIssues(
		const UPaper2DPlusEffectProfileAsset& Asset,
		const TArray<FPaper2DPlusEffectProfileValidationIssue>& SourceIssues,
		TArray<FPaper2DPlusValidationIssue>& OutIssues);

	void ProjectCombatIssues(
		const UPaper2DPlusCombatProfileAsset& Asset,
		const TArray<FPaper2DPlusCombatValidationIssue>& SourceIssues,
		TArray<FPaper2DPlusValidationIssue>& OutIssues);
}
