// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AssetRegistry/AssetData.h"
#include "CharacterCatalogSourceTypes.h"
#include "Paper2DPlusCharacterCatalogAsset.h"
#include "Paper2DPlusValidationService.h"

class UObject;
class UPaper2DPlusCharacterCatalogAsset;

/** Read-only result of inspecting the host project's Asset Manager policy for one Catalog path. */
struct PAPER2DPLUSEDITOR_API FPaper2DPlusCharacterCatalogCookInspection
{
	bool bTypeRegistered = false;
	bool bCatalogPathRegistered = false;
	bool bTypeIsEditorOnly = false;
	bool bProductionCookBlocked = false;
	FName RegisteredPrimaryAssetType = NAME_None;

	bool IsCookReady() const
	{
		return bTypeRegistered
			&& bCatalogPathRegistered
			&& !bTypeIsEditorOnly
			&& !bProductionCookBlocked;
	}
};

/** One prepared, path-keyed Catalog row. Slate consumers read this value and never validate or load. */
struct PAPER2DPLUSEDITOR_API FPaper2DPlusCharacterCatalogAuditRow
{
	FSoftObjectPath CharacterPath;
	FPaper2DPlusCharacterCatalogCompletion RuntimeCompletion;
	FPaper2DPlusValidationSummary IssueSummary;
	bool bAuditComplete = false;
};

/** Aggregate counts derived from the final normalized issue list and prepared rows. */
struct PAPER2DPLUSEDITOR_API FPaper2DPlusCharacterCatalogAuditSummary
{
	int32 NumCharacters = 0;
	int32 NumRuntimeComplete = 0;
	int32 NumAuditComplete = 0;
	int32 NumRowsWithWarnings = 0;
	int32 NumRowsWithErrors = 0;
	FPaper2DPlusValidationSummary Issues;

	bool IsCatalogReady() const { return !Issues.HasErrors(); }
};

/** Deterministic output shared by the Catalog editor and U21 validation adapters. */
struct PAPER2DPLUSEDITOR_API FPaper2DPlusCharacterCatalogAuditReport
{
	FSoftObjectPath CatalogPath;
	FPaper2DPlusCharacterCatalogCookInspection CookInspection;
	TArray<FPaper2DPlusCharacterCatalogAuditRow> Rows;
	TArray<FPaper2DPlusValidationIssue> Issues;
	FPaper2DPlusCharacterCatalogAuditSummary Summary;

	const FPaper2DPlusCharacterCatalogAuditRow* FindRow(const FSoftObjectPath& CharacterPath) const;
};

/**
 * Editor-only Catalog audit truth.
 *
 * BuildReport is an explicit deep-validation command over the SAVED roster: it may resolve only the
 * Character/companion objects the saved entries name so their existing native validators can run.
 * It never repairs, modifies, saves, or dirties an asset. UI row generation consumes the returned
 * report only and therefore performs no validation or loading. Expected-tag coverage runs only when
 * a native resolver is supplied, so registry-only reports never vary with Character Profile residency.
 */
class PAPER2DPLUSEDITOR_API FCharacterCatalogAuditService
{
public:
	using FNativeAssetResolver = TFunction<UObject*(const FSoftObjectPath&)>;
	using FCookRegistrationInspector = TFunction<FPaper2DPlusCharacterCatalogCookInspection(
		const FSoftObjectPath&)>;
	using FRowProgressCallback = TFunction<void(
		int32 RowIndex,
		int32 RowCount,
		const FSoftObjectPath& CharacterPath)>;

	static FPaper2DPlusCharacterCatalogAuditReport BuildReport(
		const UPaper2DPlusCharacterCatalogAsset* CatalogAsset,
		const TArray<FAssetData>& Assets,
		const FPaper2DPlusCharacterCatalogSettingsSnapshot& Settings,
		const FNativeAssetResolver& AssetResolver = FNativeAssetResolver(),
		const FCookRegistrationInspector& CookInspector = FCookRegistrationInspector(),
		const FRowProgressCallback& RowProgress = FRowProgressCallback());

	/** Host Asset Manager inspection used by the default BuildReport path. Never loads an asset. */
	static FPaper2DPlusCharacterCatalogCookInspection InspectHostAssetManager(
		const FSoftObjectPath& CatalogPath);
};
