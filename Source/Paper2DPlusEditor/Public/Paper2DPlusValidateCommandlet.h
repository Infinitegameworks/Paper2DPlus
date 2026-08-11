// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "Paper2DPlusValidationService.h"
#include "Paper2DPlusValidateCommandlet.generated.h"

class IAssetRegistry;
struct FAssetData;

/** Optional deterministic discovery filters accepted by the validation commandlet. */
struct PAPER2DPLUSEDITOR_API FPaper2DPlusValidateFilters
{
	/** Short aliases (Character, Layer, Effect, Combat, Catalog), class names, or class paths. */
	TArray<FString> AssetTypes;

	/** Package/object path prefixes. At least one prefix must match when this list is non-empty. */
	TArray<FString> AssetPaths;
};

/** Issues emitted while one registered asset was scanned. */
struct PAPER2DPLUSEDITOR_API FPaper2DPlusValidateAssetReport
{
	FString ScannedAssetPath;
	FName ScannedAssetType;
	TArray<FPaper2DPlusValidationIssue> Issues;
};

/** Stable, presentation-neutral run result used by JSON, console, tests, and release scripts. */
struct PAPER2DPLUSEDITOR_API FPaper2DPlusValidateRunReport
{
	TArray<FPaper2DPlusValidateAssetReport> Assets;
	TMap<FName, int32> AssetTypeCounts;
	int32 AssetsWithIssues = 0;
	int32 ErrorCount = 0;
	int32 WarningCount = 0;
	int32 InfoCount = 0;

	void Recount();
};

/** Complete deterministic result of the shared scan/report core. */
struct PAPER2DPLUSEDITOR_API FPaper2DPlusValidateExecutionResult
{
	FPaper2DPlusValidateRunReport Report;
	FString Json;
	int32 ExitCode = 2;
};

namespace Paper2DPlusValidateCommandlet
{
	static constexpr int32 ReportSchemaVersion = 2;
	using FAssetLoader = TFunction<UObject*(const FAssetData&)>;

	PAPER2DPLUSEDITOR_API bool MatchesFilters(
		const FString& AssetPath,
		const FString& AssetClassPath,
		const FPaper2DPlusValidateFilters& Filters);

	/** Build actual/native/Blueprint-derived class -> registered base adapter class lookup. */
	PAPER2DPLUSEDITOR_API void BuildSupportedAssetClassLookup(
		IAssetRegistry& Registry,
		const TArray<FName>& RegisteredClassPaths,
		TMap<FString, FString>& OutRegisteredClassByActualClass);

	/** Resolve an Asset Registry row to the registered adapter base without loading the asset. */
	PAPER2DPLUSEDITOR_API bool ResolveRegisteredAssetClassPath(
		const FAssetData& AssetData,
		const TMap<FString, FString>& RegisteredClassByActualClass,
		FString& OutRegisteredClassPath);

	/**
	 * Run the exact discovery -> shared-adapter validation -> configured-Catalog requirement ->
	 * schema-v2 JSON -> exit-policy pipeline over an immutable Asset Registry snapshot.
	 * Main supplies the live registry snapshot; automation may supply an in-memory snapshot without
	 * mutating the process-wide registry or writing a report file.
	 */
	PAPER2DPLUSEDITOR_API FPaper2DPlusValidateExecutionResult RunValidation(
		const TArray<FAssetData>& AllAssets,
		const TMap<FString, FString>& RegisteredClassByActualClass,
		const FSoftObjectPath& ConfiguredCatalogPath,
		const FPaper2DPlusValidateFilters& Filters = FPaper2DPlusValidateFilters(),
		const FAssetLoader& AssetLoader = FAssetLoader());

	/**
	 * Add the project Catalog authority as a synthetic validation subject when the setting is empty,
	 * its soft path is missing, or its path resolves to the wrong class. Correct Catalog rows remain
	 * owned by the normal adapter pass. Type filters use the expected Catalog class; a path-scoped run
	 * excludes an empty setting because it has no truthful asset path to match.
	 */
	PAPER2DPLUSEDITOR_API bool AppendConfiguredCharacterCatalogRequirement(
		const FSoftObjectPath& ConfiguredCatalogPath,
		const TArray<FAssetData>& AllAssets,
		const TMap<FString, FString>& RegisteredClassByActualClass,
		const FPaper2DPlusValidateFilters& Filters,
		FPaper2DPlusValidateRunReport& InOutReport);

	PAPER2DPLUSEDITOR_API FString BuildJsonReport(
		const FPaper2DPlusValidateRunReport& Report,
		const FPaper2DPlusValidateFilters& Filters = FPaper2DPlusValidateFilters());

	/** 0 for clean/warning/info/no-assets, 1 when any normalized Error exists. */
	PAPER2DPLUSEDITOR_API int32 ValidationExitCode(const FPaper2DPlusValidateRunReport& Report);
}

/**
 * Project-wide Paper2D+ validation commandlet.
 *
 * Discovers every class in the shared adapter registry (Character, Layer, Effect, Combat, Catalog),
 * validates without Sync/migration/repair, and emits the same normalized issue identities used by the
 * editor panels, Content Browser, and Data Validation subsystem.
 *
 * Usage:
 *   UnrealEditor-Cmd.exe <uproject> -run=Paper2DPlusValidate
 *     [-Type=Character,Layer] [-Path=/Game/Characters] [-JsonOutput=path.json]
 *
 * Exit codes: 0 = no Errors, 1 = validation Error(s), 2 = infrastructure/report-write failure.
 */
UCLASS()
class PAPER2DPLUSEDITOR_API UPaper2DPlusValidateCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UPaper2DPlusValidateCommandlet();

	virtual int32 Main(const FString& Params) override;
};
