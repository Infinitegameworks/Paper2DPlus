// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "Paper2DPlusValidateCommandlet.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "Policies/CondensedJsonPrintPolicy.h"

/** UPaper2DPlusValidateCommandlet — Commandline batch validation runner for CI/build pipelines. */

DEFINE_LOG_CATEGORY_STATIC(LogPaper2DPlusValidate, Log, All);

UPaper2DPlusValidateCommandlet::UPaper2DPlusValidateCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
	ShowErrorCount = false; // we track our own
}

namespace
{
	static const TCHAR* SeverityToString(ECharacterProfileValidationSeverity Sev)
	{
		switch (Sev)
		{
			case ECharacterProfileValidationSeverity::Error:   return TEXT("Error");
			case ECharacterProfileValidationSeverity::Warning: return TEXT("Warning");
			case ECharacterProfileValidationSeverity::Info:    return TEXT("Info");
			default:                                           return TEXT("Unknown");
		}
	}

	struct FAssetReport
	{
		FString AssetPath;
		TArray<FCharacterProfileValidationIssue> Issues;
	};

	static FString BuildJsonReport(const TArray<FAssetReport>& Reports,
		int32 ErrorCount, int32 WarningCount, int32 InfoCount)
	{
		const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetNumberField(TEXT("assets_scanned"), Reports.Num());
		Root->SetNumberField(TEXT("error_count"), ErrorCount);
		Root->SetNumberField(TEXT("warning_count"), WarningCount);
		Root->SetNumberField(TEXT("info_count"), InfoCount);

		TArray<TSharedPtr<FJsonValue>> ResultsArray;
		ResultsArray.Reserve(Reports.Num());
		for (const FAssetReport& Report : Reports)
		{
			const TSharedRef<FJsonObject> AssetObj = MakeShared<FJsonObject>();
			AssetObj->SetStringField(TEXT("asset"), Report.AssetPath);

			TArray<TSharedPtr<FJsonValue>> IssueArray;
			IssueArray.Reserve(Report.Issues.Num());
			for (const FCharacterProfileValidationIssue& Issue : Report.Issues)
			{
				const TSharedRef<FJsonObject> IssueObj = MakeShared<FJsonObject>();
				IssueObj->SetStringField(TEXT("severity"), SeverityToString(Issue.Severity));
				IssueObj->SetStringField(TEXT("context"), Issue.Context);
				IssueObj->SetStringField(TEXT("message"), Issue.Message);
				IssueArray.Add(MakeShared<FJsonValueObject>(IssueObj));
			}
			AssetObj->SetArrayField(TEXT("issues"), IssueArray);
			ResultsArray.Add(MakeShared<FJsonValueObject>(AssetObj));
		}
		Root->SetArrayField(TEXT("results"), ResultsArray);

		FString Out;
		const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
		FJsonSerializer::Serialize(Root, Writer);
		return Out;
	}
}

int32 UPaper2DPlusValidateCommandlet::Main(const FString& Params)
{
	// Parse -JsonOutput=<path> if present
	TMap<FString, FString> ParamsMap;
	TArray<FString> Tokens, Switches;
	ParseCommandLine(*Params, Tokens, Switches, ParamsMap);

	FString JsonOutputPath;
	if (const FString* Found = ParamsMap.Find(TEXT("JsonOutput")))
	{
		JsonOutputPath = *Found;
	}

	// Locate asset registry and force-scan so newly-added assets are discovered.
	FAssetRegistryModule& RegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(
		AssetRegistryConstants::ModuleName);
	IAssetRegistry& Registry = RegistryModule.Get();
	Registry.SearchAllAssets(/*bSynchronousSearch=*/true);

	TArray<FAssetData> Assets;
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
	Registry.GetAssetsByClass(UPaper2DPlusCharacterProfileAsset::StaticClass()->GetFName(), Assets);
#else
	Registry.GetAssetsByClass(UPaper2DPlusCharacterProfileAsset::StaticClass()->GetClassPathName(), Assets);
#endif

	if (Assets.Num() == 0)
	{
		UE_LOG(LogPaper2DPlusValidate, Display,
			TEXT("Paper2DPlusValidate: no CharacterProfile assets found in project. Exit 0."));
		return 0;
	}

	UE_LOG(LogPaper2DPlusValidate, Display,
		TEXT("Paper2DPlusValidate: scanning %d CharacterProfile asset(s)..."), Assets.Num());

	TArray<FAssetReport> Reports;
	Reports.Reserve(Assets.Num());
	int32 ErrorCount = 0;
	int32 WarningCount = 0;
	int32 InfoCount = 0;

	for (const FAssetData& AssetData : Assets)
	{
		UPaper2DPlusCharacterProfileAsset* Asset = Cast<UPaper2DPlusCharacterProfileAsset>(AssetData.GetAsset());
		if (!Asset)
		{
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
			UE_LOG(LogPaper2DPlusValidate, Warning,
				TEXT("  [SKIP] %s — failed to load asset."), *AssetData.ObjectPath.ToString());
#else
			UE_LOG(LogPaper2DPlusValidate, Warning,
				TEXT("  [SKIP] %s — failed to load asset."), *AssetData.GetObjectPathString());
#endif
			continue;
		}

		FAssetReport Report;
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
		Report.AssetPath = AssetData.ObjectPath.ToString();
#else
		Report.AssetPath = AssetData.GetObjectPathString();
#endif
		Asset->ValidateCharacterProfileAsset(Report.Issues);

		for (const FCharacterProfileValidationIssue& Issue : Report.Issues)
		{
			switch (Issue.Severity)
			{
				case ECharacterProfileValidationSeverity::Error:   ++ErrorCount; break;
				case ECharacterProfileValidationSeverity::Warning: ++WarningCount; break;
				case ECharacterProfileValidationSeverity::Info:    ++InfoCount; break;
				default: break;
			}
		}

		// Plain-text per-asset report (stdout)
		UE_LOG(LogPaper2DPlusValidate, Display, TEXT(""));
		UE_LOG(LogPaper2DPlusValidate, Display, TEXT("[Asset: %s]"), *Report.AssetPath);
		if (Report.Issues.Num() == 0)
		{
			UE_LOG(LogPaper2DPlusValidate, Display, TEXT("  (no issues)"));
		}
		else
		{
			for (const FCharacterProfileValidationIssue& Issue : Report.Issues)
			{
				UE_LOG(LogPaper2DPlusValidate, Display, TEXT("  [%s] %s: %s"),
					SeverityToString(Issue.Severity), *Issue.Context, *Issue.Message);
			}
		}

		Reports.Add(MoveTemp(Report));
	}

	// Summary
	const int32 AssetsWithIssues = [&]() {
		int32 N = 0;
		for (const FAssetReport& R : Reports) if (R.Issues.Num() > 0) ++N;
		return N;
	}();

	UE_LOG(LogPaper2DPlusValidate, Display, TEXT(""));
	UE_LOG(LogPaper2DPlusValidate, Display,
		TEXT("Summary: %d assets scanned, %d with issues. Errors=%d Warnings=%d Info=%d."),
		Reports.Num(), AssetsWithIssues, ErrorCount, WarningCount, InfoCount);

	// Optional JSON output
	if (!JsonOutputPath.IsEmpty())
	{
		const FString Json = BuildJsonReport(Reports, ErrorCount, WarningCount, InfoCount);
		if (FFileHelper::SaveStringToFile(Json, *JsonOutputPath))
		{
			UE_LOG(LogPaper2DPlusValidate, Display, TEXT("Wrote JSON report to %s"), *JsonOutputPath);
		}
		else
		{
			UE_LOG(LogPaper2DPlusValidate, Warning, TEXT("Failed to write JSON report to %s"), *JsonOutputPath);
		}
	}

	const int32 ExitCode = (ErrorCount > 0) ? 1 : 0;
	UE_LOG(LogPaper2DPlusValidate, Display, TEXT("Paper2DPlusValidate: exit %d."), ExitCode);
	return ExitCode;
}
