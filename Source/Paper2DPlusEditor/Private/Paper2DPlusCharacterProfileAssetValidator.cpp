// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "Paper2DPlusCharacterProfileAssetValidator.h"

#include "Logging/TokenizedMessage.h"
#include "Paper2DPlusValidationService.h"

#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 4)
namespace
{
	bool AssetDataMatchesRegisteredClass(const FAssetData& AssetData)
	{
		TArray<FName> ClassPaths;
		FPaper2DPlusValidationService::Get().GetRegisteredAssetClassPaths(ClassPaths);
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
		for (const FName ClassPath : ClassPaths)
		{
			FString Path = ClassPath.ToString();
			FString ShortName;
			if (!Path.Split(TEXT("."), nullptr, &ShortName, ESearchCase::CaseSensitive, ESearchDir::FromEnd))
			{
				ShortName = Path;
			}
			if (AssetData.AssetClass == FName(*ShortName))
			{
				return true;
			}
		}
#else
		for (const FName ClassPath : ClassPaths)
		{
			if (AssetData.AssetClassPath.ToString().Equals(ClassPath.ToString(), ESearchCase::IgnoreCase))
			{
				return true;
			}
		}
#endif
		return false;
	}
}

bool UPaper2DPlusCharacterProfileAssetValidator::CanValidateAsset_Implementation(
	const FAssetData& InAssetData,
	UObject* InObject,
	FDataValidationContext& InContext) const
{
	if (InObject)
	{
		return FPaper2DPlusValidationService::Get().HasAdapterFor(InObject);
	}
	return AssetDataMatchesRegisteredClass(InAssetData);
}

EDataValidationResult UPaper2DPlusCharacterProfileAssetValidator::ValidateLoadedAsset_Implementation(
	const FAssetData& InAssetData,
	UObject* InAsset,
	FDataValidationContext& Context)
{
	TArray<FPaper2DPlusValidationIssue> Issues;
	if (!InAsset || !FPaper2DPlusValidationService::Get().ValidateObject(InAsset, Issues))
	{
		return EDataValidationResult::NotValidated;
	}

	bool bHasErrors = false;
	for (const FPaper2DPlusValidationIssue& Issue : Issues)
	{
		switch (Issue.Severity)
		{
		case EPaper2DPlusValidationSeverity::Error:
			bHasErrors = true;
			AssetFails(InAsset, Issue.Message);
			break;
		case EPaper2DPlusValidationSeverity::Warning:
			AssetWarning(InAsset, Issue.Message);
			break;
		default:
			AssetMessage(InAssetData, EMessageSeverity::Info, Issue.Message);
			break;
		}
	}

	if (bHasErrors)
	{
		return EDataValidationResult::Invalid;
	}
	AssetPasses(InAsset);
	return EDataValidationResult::Valid;
}

#else

bool UPaper2DPlusCharacterProfileAssetValidator::CanValidateAsset_Implementation(
	UObject* InAsset) const
{
	return FPaper2DPlusValidationService::Get().HasAdapterFor(InAsset);
}

EDataValidationResult UPaper2DPlusCharacterProfileAssetValidator::ValidateLoadedAsset_Implementation(
	UObject* InAsset,
	TArray<FText>& ValidationErrors)
{
	TArray<FPaper2DPlusValidationIssue> Issues;
	if (!InAsset || !FPaper2DPlusValidationService::Get().ValidateObject(InAsset, Issues))
	{
		return EDataValidationResult::NotValidated;
	}

	bool bHasErrors = false;
	for (const FPaper2DPlusValidationIssue& Issue : Issues)
	{
		if (Issue.Severity == EPaper2DPlusValidationSeverity::Error)
		{
			bHasErrors = true;
			AssetFails(InAsset, Issue.Message, ValidationErrors);
		}
		else if (Issue.Severity == EPaper2DPlusValidationSeverity::Warning)
		{
			AssetWarning(InAsset, Issue.Message);
		}
		// UE 5.0-5.3's validator base has no Info message channel. Info stays nonfatal and remains
		// available through the shared editor panel, Content Browser action, and commandlet report.
	}

	if (bHasErrors)
	{
		return EDataValidationResult::Invalid;
	}
	AssetPasses(InAsset);
	return EDataValidationResult::Valid;
}

#endif
