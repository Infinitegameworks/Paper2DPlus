// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "Paper2DPlusValidateCommandlet.h"

#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Paper2DPlusCharacterCatalogAsset.h"
#include "Paper2DPlusSettings.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "ProfileValidationAdapter.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

DEFINE_LOG_CATEGORY_STATIC(LogPaper2DPlusValidate, Log, All);

namespace
{
	const TCHAR* ValidationCommandletSeverityString(EPaper2DPlusValidationSeverity Severity)
	{
		switch (Severity)
		{
		case EPaper2DPlusValidationSeverity::Error: return TEXT("Error");
		case EPaper2DPlusValidationSeverity::Warning: return TEXT("Warning");
		default: return TEXT("Info");
		}
	}

	FString AssetObjectPath(const FAssetData& AssetData)
	{
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
		return AssetData.ObjectPath.ToString();
#else
		return AssetData.GetObjectPathString();
#endif
	}

	FString ShortClassName(const FString& ClassPath)
	{
		FString Result;
		if (ClassPath.Split(TEXT("."), nullptr, &Result, ESearchCase::CaseSensitive, ESearchDir::FromEnd))
		{
			return Result;
		}
		return ClassPath;
	}

	FString AssetDataClassLookupKey(const FAssetData& AssetData)
	{
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
		return AssetData.AssetClass.ToString().ToLower();
#else
		return AssetData.AssetClassPath.ToString().ToLower();
#endif
	}

	FString AssetDataClassDisplayName(const FAssetData& AssetData)
	{
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
		return AssetData.AssetClass.ToString();
#else
		return ShortClassName(AssetData.AssetClassPath.ToString());
#endif
	}

	FString ClassPathLookupKey(const FString& ClassPath)
	{
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
		return ShortClassName(ClassPath).ToLower();
#else
		return ClassPath.ToLower();
#endif
	}

	void AppendListValue(const FString& Value, TArray<FString>& OutValues)
	{
		TArray<FString> Pieces;
		Value.ParseIntoArray(Pieces, TEXT(","), true);
		for (FString& Piece : Pieces)
		{
			TArray<FString> SemicolonPieces;
			Piece.ParseIntoArray(SemicolonPieces, TEXT(";"), true);
			for (FString& Candidate : SemicolonPieces)
			{
				Candidate.TrimStartAndEndInline();
				if (!Candidate.IsEmpty())
				{
					OutValues.AddUnique(Candidate);
				}
			}
		}
		OutValues.Sort([](const FString& A, const FString& B)
		{
			return A.Compare(B, ESearchCase::IgnoreCase) < 0;
		});
	}

	TArray<TSharedPtr<FJsonValue>> StringArrayJson(TArray<FString> Values)
	{
		Values.Sort([](const FString& A, const FString& B)
		{
			return A.Compare(B, ESearchCase::IgnoreCase) < 0;
		});
		TArray<TSharedPtr<FJsonValue>> Result;
		Result.Reserve(Values.Num());
		for (const FString& Value : Values)
		{
			Result.Add(MakeShared<FJsonValueString>(Value));
		}
		return Result;
	}

	bool MatchesType(const FString& ClassPath, const FString& Filter)
	{
		const FString FoldedPath = ClassPath.ToLower();
		const FString FoldedShort = ShortClassName(ClassPath).ToLower();
		FString FoldedFilter = Filter.ToLower();
		FoldedFilter.TrimStartAndEndInline();
		if (FoldedFilter.IsEmpty()) return false;
		if (FoldedFilter == FoldedPath || FoldedFilter == FoldedShort) return true;

		if (FoldedShort.Contains(TEXT("characterprofile")))
		{
			return FoldedFilter == TEXT("character") || FoldedFilter == TEXT("characterprofile");
		}
		if (FoldedShort.Contains(TEXT("characterlayer")))
		{
			return FoldedFilter == TEXT("layer") || FoldedFilter == TEXT("characterlayer")
				|| FoldedFilter == TEXT("layered");
		}
		if (FoldedShort.Contains(TEXT("effectprofile")))
		{
			return FoldedFilter == TEXT("effect") || FoldedFilter == TEXT("effectprofile");
		}
		if (FoldedShort.Contains(TEXT("combatprofile")))
		{
			return FoldedFilter == TEXT("combat") || FoldedFilter == TEXT("combatprofile");
		}
		if (FoldedShort.Contains(TEXT("charactercatalog")))
		{
			return FoldedFilter == TEXT("catalog") || FoldedFilter == TEXT("charactercatalog");
		}
		return false;
	}

	bool MatchesPath(const FString& AssetPath, FString Filter)
	{
		Filter.TrimStartAndEndInline();
		Filter.ReplaceInline(TEXT("\\"), TEXT("/"));
		while (Filter.EndsWith(TEXT("/")) && Filter.Len() > 1)
		{
			Filter.LeftChopInline(1);
		}
		if (Filter.IsEmpty()) return false;
		return AssetPath.Equals(Filter, ESearchCase::IgnoreCase)
			|| AssetPath.StartsWith(Filter + TEXT("/"), ESearchCase::IgnoreCase)
			|| AssetPath.StartsWith(Filter + TEXT("."), ESearchCase::IgnoreCase);
	}

	FPaper2DPlusValidationIssue MakeLoadFailure(const FString& AssetPath, FName AssetType)
	{
		FPaper2DPlusValidationIssue Issue;
		Issue.Severity = EPaper2DPlusValidationSeverity::Error;
		Issue.Code = TEXT("Paper2DPlus.Validation.AssetLoadFailed");
		Issue.AssetPath = FSoftObjectPath(AssetPath);
		Issue.AssetType = AssetType;
		Issue.Scope = TEXT("Infrastructure");
		Issue.Field = TEXT("Asset");
		Issue.Message = FText::FromString(TEXT("The registered Paper2D+ asset could not be loaded for validation."));
		Issue.Remediation = FText::FromString(TEXT("Restore or repair the asset package, then rerun project validation."));
		Issue.StableKey = FPaper2DPlusValidationService::MakeStableKey(Issue);
		return Issue;
	}

	void LogRunReport(const FPaper2DPlusValidateRunReport& Report)
	{
		for (const FPaper2DPlusValidateAssetReport& Asset : Report.Assets)
		{
			UE_LOG(LogPaper2DPlusValidate, Display, TEXT("[%s: %s]"),
				*Asset.ScannedAssetType.ToString(), *Asset.ScannedAssetPath);
			if (Asset.Issues.IsEmpty())
			{
				UE_LOG(LogPaper2DPlusValidate, Display, TEXT("  (no issues)"));
				continue;
			}
			for (const FPaper2DPlusValidationIssue& Issue : Asset.Issues)
			{
				UE_LOG(LogPaper2DPlusValidate, Display, TEXT("  [%s] %s (%s/%s): %s"),
					ValidationCommandletSeverityString(Issue.Severity),
					*Issue.Code.ToString(),
					*Issue.Scope.ToString(),
					*Issue.Field.ToString(),
					*Issue.Message.ToString());
			}
		}
		UE_LOG(LogPaper2DPlusValidate, Display,
			TEXT("Summary: %d assets scanned, %d with issues. Errors=%d Warnings=%d Info=%d."),
			Report.Assets.Num(), Report.AssetsWithIssues, Report.ErrorCount,
			Report.WarningCount, Report.InfoCount);
	}
}

void FPaper2DPlusValidateRunReport::Recount()
{
	AssetTypeCounts.Reset();
	AssetsWithIssues = 0;
	ErrorCount = 0;
	WarningCount = 0;
	InfoCount = 0;
	for (const FPaper2DPlusValidateAssetReport& Asset : Assets)
	{
		++AssetTypeCounts.FindOrAdd(Asset.ScannedAssetType);
		if (!Asset.Issues.IsEmpty()) ++AssetsWithIssues;
		for (const FPaper2DPlusValidationIssue& Issue : Asset.Issues)
		{
			switch (Issue.Severity)
			{
			case EPaper2DPlusValidationSeverity::Error: ++ErrorCount; break;
			case EPaper2DPlusValidationSeverity::Warning: ++WarningCount; break;
			default: ++InfoCount; break;
			}
		}
	}
}

bool Paper2DPlusValidateCommandlet::MatchesFilters(
	const FString& AssetPath,
	const FString& AssetClassPath,
	const FPaper2DPlusValidateFilters& Filters)
{
	if (!Filters.AssetTypes.IsEmpty())
	{
		bool bTypeMatch = false;
		for (const FString& Filter : Filters.AssetTypes)
		{
			bTypeMatch |= MatchesType(AssetClassPath, Filter);
		}
		if (!bTypeMatch) return false;
	}

	if (!Filters.AssetPaths.IsEmpty())
	{
		bool bPathMatch = false;
		for (const FString& Filter : Filters.AssetPaths)
		{
			bPathMatch |= MatchesPath(AssetPath, Filter);
		}
		if (!bPathMatch) return false;
	}
	return true;
}

void Paper2DPlusValidateCommandlet::BuildSupportedAssetClassLookup(
	IAssetRegistry& Registry,
	const TArray<FName>& RegisteredClassPaths,
	TMap<FString, FString>& OutRegisteredClassByActualClass)
{
	OutRegisteredClassByActualClass.Reset();
	auto AddMapping = [&OutRegisteredClassByActualClass](
		const FString& ActualClassPath,
		const FString& RegisteredClassPath)
	{
		const FString ActualKey = ClassPathLookupKey(ActualClassPath);
		if (ActualKey.IsEmpty()) return;

		FString* Existing = OutRegisteredClassByActualClass.Find(ActualKey);
		if (!Existing)
		{
			OutRegisteredClassByActualClass.Add(ActualKey, RegisteredClassPath);
			return;
		}

		// Preserve the validation service's most-derived-adapter rule when registered bases overlap.
		UClass* ExistingClass = LoadObject<UClass>(nullptr, *(*Existing));
		UClass* CandidateClass = LoadObject<UClass>(nullptr, *RegisteredClassPath);
		if (CandidateClass && ExistingClass && CandidateClass->IsChildOf(ExistingClass))
		{
			*Existing = RegisteredClassPath;
		}
	};

	for (const FName RegisteredClassPathName : RegisteredClassPaths)
	{
		const FString RegisteredClassPath = RegisteredClassPathName.ToString();
		UClass* BaseClass = LoadObject<UClass>(nullptr, *RegisteredClassPath);
		if (!BaseClass)
		{
			continue;
		}
		AddMapping(RegisteredClassPath, RegisteredClassPath);

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
		TArray<FName> BaseClassNames;
		BaseClassNames.Add(BaseClass->GetFName());
		TSet<FName> ExcludedClassNames;
		TSet<FName> DerivedClassNames;
		Registry.GetDerivedClassNames(BaseClassNames, ExcludedClassNames, DerivedClassNames);
		for (const FName DerivedClassName : DerivedClassNames)
		{
			AddMapping(DerivedClassName.ToString(), RegisteredClassPath);
		}
#else
		TArray<FTopLevelAssetPath> BaseClassPaths;
		BaseClassPaths.Add(BaseClass->GetClassPathName());
		TSet<FTopLevelAssetPath> ExcludedClassPaths;
		TSet<FTopLevelAssetPath> DerivedClassPaths;
		Registry.GetDerivedClassNames(BaseClassPaths, ExcludedClassPaths, DerivedClassPaths);
		for (const FTopLevelAssetPath& DerivedClassPath : DerivedClassPaths)
		{
			AddMapping(DerivedClassPath.ToString(), RegisteredClassPath);
		}
#endif
	}
}

bool Paper2DPlusValidateCommandlet::ResolveRegisteredAssetClassPath(
	const FAssetData& AssetData,
	const TMap<FString, FString>& RegisteredClassByActualClass,
	FString& OutRegisteredClassPath)
{
	OutRegisteredClassPath.Reset();
	if (const FString* Registered = RegisteredClassByActualClass.Find(AssetDataClassLookupKey(AssetData)))
	{
		OutRegisteredClassPath = *Registered;
		return true;
	}
	return false;
}

FPaper2DPlusValidateExecutionResult Paper2DPlusValidateCommandlet::RunValidation(
	const TArray<FAssetData>& AllAssets,
	const TMap<FString, FString>& RegisteredClassByActualClass,
	const FSoftObjectPath& ConfiguredCatalogPath,
	const FPaper2DPlusValidateFilters& Filters,
	const FAssetLoader& AssetLoader)
{
	struct FDiscoveredAsset
	{
		FAssetData Data;
		FString Path;
		FString ClassPath;
	};

	TArray<FDiscoveredAsset> Discovered;
	for (const FAssetData& AssetData : AllAssets)
	{
		const FString Path = AssetObjectPath(AssetData);
		FString RegisteredClassPath;
		if (ResolveRegisteredAssetClassPath(
				AssetData,
				RegisteredClassByActualClass,
				RegisteredClassPath)
			&& MatchesFilters(Path, RegisteredClassPath, Filters))
		{
			FDiscoveredAsset& Row = Discovered.AddDefaulted_GetRef();
			Row.Data = AssetData;
			Row.Path = Path;
			Row.ClassPath = RegisteredClassPath;
		}
	}
	Discovered.Sort([](const FDiscoveredAsset& A, const FDiscoveredAsset& B)
	{
		if (A.Path != B.Path) return A.Path < B.Path;
		return A.ClassPath < B.ClassPath;
	});

	FPaper2DPlusValidateExecutionResult Result;
	Result.Report.Assets.Reserve(Discovered.Num());
	FPaper2DPlusValidationService& ValidationService = FPaper2DPlusValidationService::Get();
	for (const FDiscoveredAsset& DiscoveredAsset : Discovered)
	{
		FPaper2DPlusValidateAssetReport& AssetReport = Result.Report.Assets.AddDefaulted_GetRef();
		AssetReport.ScannedAssetPath = DiscoveredAsset.Path;
		AssetReport.ScannedAssetType = FName(*ShortClassName(DiscoveredAsset.ClassPath));
		UObject* Asset = AssetLoader
			? AssetLoader(DiscoveredAsset.Data)
			: DiscoveredAsset.Data.GetAsset();
		if (!Asset)
		{
			AssetReport.Issues.Add(MakeLoadFailure(
				DiscoveredAsset.Path, AssetReport.ScannedAssetType));
			continue;
		}

		AssetReport.ScannedAssetType = Asset->GetClass()->GetFName();
		if (!ValidationService.ValidateObject(Asset, AssetReport.Issues))
		{
			AssetReport.Issues.Add(MakeLoadFailure(
				DiscoveredAsset.Path, AssetReport.ScannedAssetType));
		}
	}

	AppendConfiguredCharacterCatalogRequirement(
		ConfiguredCatalogPath,
		AllAssets,
		RegisteredClassByActualClass,
		Filters,
		Result.Report);
	Result.Report.Recount();
	Result.Json = BuildJsonReport(Result.Report, Filters);
	Result.ExitCode = ValidationExitCode(Result.Report);
	return Result;
}

bool Paper2DPlusValidateCommandlet::AppendConfiguredCharacterCatalogRequirement(
	const FSoftObjectPath& ConfiguredCatalogPath,
	const TArray<FAssetData>& AllAssets,
	const TMap<FString, FString>& RegisteredClassByActualClass,
	const FPaper2DPlusValidateFilters& Filters,
	FPaper2DPlusValidateRunReport& InOutReport)
{
	const UClass* ExpectedClass = UPaper2DPlusCharacterCatalogAsset::StaticClass();
	const FString ExpectedClassPath = ExpectedClass->GetPathName();
	const FString ConfiguredPathString = ConfiguredCatalogPath.ToString();
	if (!MatchesFilters(ConfiguredPathString, ExpectedClassPath, Filters))
	{
		return false;
	}

	const FAssetData* ConfiguredAsset = nullptr;
	if (ConfiguredCatalogPath.IsValid())
	{
		ConfiguredAsset = AllAssets.FindByPredicate(
			[&ConfiguredPathString](const FAssetData& Candidate)
			{
				return AssetObjectPath(Candidate).Equals(ConfiguredPathString, ESearchCase::IgnoreCase);
			});
	}

	EPaper2DPlusConfiguredCatalogReferenceProblem Problem =
		EPaper2DPlusConfiguredCatalogReferenceProblem::Unset;
	FName ActualAssetType = NAME_None;
	if (ConfiguredCatalogPath.IsValid())
	{
		if (!ConfiguredAsset)
		{
			Problem = EPaper2DPlusConfiguredCatalogReferenceProblem::Missing;
		}
		else
		{
			FString RegisteredClassPath;
			const bool bHasRegisteredClass = ResolveRegisteredAssetClassPath(
				*ConfiguredAsset,
				RegisteredClassByActualClass,
				RegisteredClassPath);
			const UClass* RegisteredClass = bHasRegisteredClass
				? LoadObject<UClass>(nullptr, *RegisteredClassPath)
				: nullptr;
			if (RegisteredClass && RegisteredClass->IsChildOf(ExpectedClass))
			{
				// Discovery already queued this object for the Catalog adapter (or its more-derived
				// registered adapter). That normal row is the sole owner of object-backed issues.
				return false;
			}
			Problem = EPaper2DPlusConfiguredCatalogReferenceProblem::WrongClass;
			ActualAssetType = FName(*AssetDataClassDisplayName(*ConfiguredAsset));
		}
	}

	TArray<FPaper2DPlusValidationIssue> ProjectedIssues;
	Paper2DPlusProfileValidationAdapter::ProjectConfiguredCatalogReferenceProblem(
		ConfiguredCatalogPath,
		Problem,
		ActualAssetType,
		ProjectedIssues);
	FPaper2DPlusValidationService::NormalizeAndSort(ProjectedIssues);
	if (ProjectedIssues.IsEmpty())
	{
		return false;
	}

	const FString& StableKey = ProjectedIssues[0].StableKey;
	for (const FPaper2DPlusValidateAssetReport& ExistingReport : InOutReport.Assets)
	{
		if (ExistingReport.Issues.ContainsByPredicate(
			[&StableKey](const FPaper2DPlusValidationIssue& ExistingIssue)
			{
				return ExistingIssue.StableKey == StableKey;
			}))
		{
			return false;
		}
	}

	FPaper2DPlusValidateAssetReport& AssetReport = InOutReport.Assets.AddDefaulted_GetRef();
	AssetReport.ScannedAssetPath = ConfiguredPathString;
	AssetReport.ScannedAssetType = ExpectedClass->GetFName();
	AssetReport.Issues = MoveTemp(ProjectedIssues);
	InOutReport.Assets.Sort([](
		const FPaper2DPlusValidateAssetReport& A,
		const FPaper2DPlusValidateAssetReport& B)
	{
		if (A.ScannedAssetPath != B.ScannedAssetPath)
		{
			return A.ScannedAssetPath < B.ScannedAssetPath;
		}
		return A.ScannedAssetType.ToString() < B.ScannedAssetType.ToString();
	});
	return true;
}

FString Paper2DPlusValidateCommandlet::BuildJsonReport(
	const FPaper2DPlusValidateRunReport& InReport,
	const FPaper2DPlusValidateFilters& Filters)
{
	FPaper2DPlusValidateRunReport Report = InReport;
	for (FPaper2DPlusValidateAssetReport& Asset : Report.Assets)
	{
		FPaper2DPlusValidationService::NormalizeAndSort(Asset.Issues);
	}
	Report.Assets.Sort([](const FPaper2DPlusValidateAssetReport& A, const FPaper2DPlusValidateAssetReport& B)
	{
		if (A.ScannedAssetPath != B.ScannedAssetPath) return A.ScannedAssetPath < B.ScannedAssetPath;
		return A.ScannedAssetType.ToString() < B.ScannedAssetType.ToString();
	});
	Report.Recount();

	const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("schema_version"), ReportSchemaVersion);
	Root->SetStringField(TEXT("tool"), TEXT("Paper2DPlusValidate"));
	Root->SetNumberField(TEXT("assets_scanned"), Report.Assets.Num());
	Root->SetNumberField(TEXT("assets_with_issues"), Report.AssetsWithIssues);
	Root->SetNumberField(TEXT("error_count"), Report.ErrorCount);
	Root->SetNumberField(TEXT("warning_count"), Report.WarningCount);
	Root->SetNumberField(TEXT("info_count"), Report.InfoCount);

	TArray<FName> CountKeys;
	Report.AssetTypeCounts.GetKeys(CountKeys);
	CountKeys.Sort(FNameLexicalLess());
	const TSharedRef<FJsonObject> Counts = MakeShared<FJsonObject>();
	for (const FName Key : CountKeys)
	{
		Counts->SetNumberField(Key.ToString(), Report.AssetTypeCounts.FindChecked(Key));
	}
	Root->SetObjectField(TEXT("asset_type_counts"), Counts);

	const TSharedRef<FJsonObject> FilterObject = MakeShared<FJsonObject>();
	FilterObject->SetArrayField(TEXT("types"), StringArrayJson(Filters.AssetTypes));
	FilterObject->SetArrayField(TEXT("paths"), StringArrayJson(Filters.AssetPaths));
	Root->SetObjectField(TEXT("filters"), FilterObject);

	TArray<TSharedPtr<FJsonValue>> ResultValues;
	ResultValues.Reserve(Report.Assets.Num());
	for (const FPaper2DPlusValidateAssetReport& Asset : Report.Assets)
	{
		const TSharedRef<FJsonObject> AssetObject = MakeShared<FJsonObject>();
		AssetObject->SetStringField(TEXT("scanned_asset"), Asset.ScannedAssetPath);
		AssetObject->SetStringField(TEXT("scanned_asset_type"), Asset.ScannedAssetType.ToString());

		TArray<TSharedPtr<FJsonValue>> IssueValues;
		IssueValues.Reserve(Asset.Issues.Num());
		for (const FPaper2DPlusValidationIssue& Issue : Asset.Issues)
		{
			const TSharedRef<FJsonObject> IssueObject = MakeShared<FJsonObject>();
			IssueObject->SetStringField(TEXT("severity"), ValidationCommandletSeverityString(Issue.Severity));
			IssueObject->SetStringField(TEXT("code"), Issue.Code.ToString());
			IssueObject->SetStringField(TEXT("stable_key"), Issue.StableKey);
			IssueObject->SetStringField(TEXT("asset"), Issue.AssetPath.ToString());
			IssueObject->SetStringField(TEXT("asset_type"), Issue.AssetType.ToString());
			IssueObject->SetStringField(TEXT("character"), Issue.CharacterPath.ToString());
			IssueObject->SetStringField(TEXT("scope"), Issue.Scope.ToString());
			IssueObject->SetStringField(TEXT("item"), Issue.ItemIdentity);
			IssueObject->SetStringField(TEXT("field"), Issue.Field.ToString());
			IssueObject->SetStringField(TEXT("message"), Issue.Message.ToString());
			IssueObject->SetStringField(TEXT("remediation"), Issue.Remediation.ToString());

			const TSharedRef<FJsonObject> Navigation = MakeShared<FJsonObject>();
			Navigation->SetBoolField(TEXT("activatable"), Issue.CanActivate());
			Navigation->SetStringField(TEXT("asset"), Issue.GetNavigationAssetPath().ToString());
			Navigation->SetStringField(TEXT("tool"), Issue.ToolTarget.IsSet() ? Issue.ToolTarget->ToolId.ToString() : FString());
			Navigation->SetStringField(TEXT("tab"), Issue.ToolTarget.IsSet() ? Issue.ToolTarget->TabId.ToString() : FString());
			Navigation->SetStringField(TEXT("item"), Issue.ToolTarget.IsSet() ? Issue.ToolTarget->ItemIdentity : FString());
			Navigation->SetStringField(TEXT("field"), Issue.ToolTarget.IsSet() ? Issue.ToolTarget->Field.ToString() : FString());
			IssueObject->SetObjectField(TEXT("navigation"), Navigation);
			IssueValues.Add(MakeShared<FJsonValueObject>(IssueObject));
		}
		AssetObject->SetArrayField(TEXT("issues"), IssueValues);
		ResultValues.Add(MakeShared<FJsonValueObject>(AssetObject));
	}
	Root->SetArrayField(TEXT("results"), ResultValues);

	FString Output;
	const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
	FJsonSerializer::Serialize(Root, Writer);
	return Output;
}

int32 Paper2DPlusValidateCommandlet::ValidationExitCode(
	const FPaper2DPlusValidateRunReport& InReport)
{
	FPaper2DPlusValidateRunReport Report = InReport;
	Report.Recount();
	return Report.ErrorCount > 0 ? 1 : 0;
}

UPaper2DPlusValidateCommandlet::UPaper2DPlusValidateCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
	ShowErrorCount = false;
}

int32 UPaper2DPlusValidateCommandlet::Main(const FString& Params)
{
	TMap<FString, FString> ParamsMap;
	TArray<FString> Tokens;
	TArray<FString> Switches;
	ParseCommandLine(*Params, Tokens, Switches, ParamsMap);

	FPaper2DPlusValidateFilters Filters;
	AppendListValue(ParamsMap.FindRef(TEXT("Type")), Filters.AssetTypes);
	AppendListValue(ParamsMap.FindRef(TEXT("Types")), Filters.AssetTypes);
	AppendListValue(ParamsMap.FindRef(TEXT("Path")), Filters.AssetPaths);
	AppendListValue(ParamsMap.FindRef(TEXT("Paths")), Filters.AssetPaths);
	const FString JsonOutputParameter = ParamsMap.FindRef(TEXT("JsonOutput"));

	FPaper2DPlusValidationService& ValidationService = FPaper2DPlusValidationService::Get();
	TArray<FName> RegisteredClassPaths;
	ValidationService.GetRegisteredAssetClassPaths(RegisteredClassPaths);
	if (RegisteredClassPaths.IsEmpty())
	{
		UE_LOG(LogPaper2DPlusValidate, Error, TEXT("The Paper2D+ validation adapter registry is empty."));
		return 2;
	}

	FAssetRegistryModule& RegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(
		AssetRegistryConstants::ModuleName);
	IAssetRegistry& Registry = RegistryModule.Get();
	Registry.SearchAllAssets(/*bSynchronousSearch=*/true);
	TMap<FString, FString> RegisteredClassByActualClass;
	Paper2DPlusValidateCommandlet::BuildSupportedAssetClassLookup(
		Registry,
		RegisteredClassPaths,
		RegisteredClassByActualClass);
	TArray<FAssetData> AllAssets;
	Registry.GetAllAssets(AllAssets);
	const UPaper2DPlusSettings* Settings = UPaper2DPlusSettings::Get();
	const FPaper2DPlusValidateExecutionResult Result =
		Paper2DPlusValidateCommandlet::RunValidation(
			AllAssets,
			RegisteredClassByActualClass,
		Settings ? Settings->DefaultCharacterCatalog.ToSoftObjectPath() : FSoftObjectPath(),
			Filters);
	LogRunReport(Result.Report);

	if (!JsonOutputParameter.IsEmpty())
	{
		const FString OutputPath = FPaths::ConvertRelativePathToFull(JsonOutputParameter);
		const FString OutputDirectory = FPaths::GetPath(OutputPath);
		if (!OutputDirectory.IsEmpty())
		{
			IFileManager::Get().MakeDirectory(*OutputDirectory, /*Tree=*/true);
		}
		if (!FFileHelper::SaveStringToFile(Result.Json, *OutputPath))
		{
			UE_LOG(LogPaper2DPlusValidate, Error, TEXT("Failed to write JSON report to %s"), *OutputPath);
			return 2;
		}
		UE_LOG(LogPaper2DPlusValidate, Display, TEXT("Wrote schema v%d JSON report to %s"),
			Paper2DPlusValidateCommandlet::ReportSchemaVersion, *OutputPath);
	}

	UE_LOG(LogPaper2DPlusValidate, Display, TEXT("Paper2DPlusValidate: exit %d."), Result.ExitCode);
	return Result.ExitCode;
}
