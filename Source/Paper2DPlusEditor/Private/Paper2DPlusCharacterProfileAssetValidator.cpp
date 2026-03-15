// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "Paper2DPlusCharacterProfileAssetValidator.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Logging/TokenizedMessage.h"

bool UPaper2DPlusCharacterProfileAssetValidator::CanValidateAsset_Implementation(
	const FAssetData& InAssetData,
	UObject* InObject,
	FDataValidationContext& InContext) const
{
	if (InObject)
	{
		return InObject->IsA<UPaper2DPlusCharacterProfileAsset>();
	}

	const FTopLevelAssetPath CharacterProfileClassPath = UPaper2DPlusCharacterProfileAsset::StaticClass()->GetClassPathName();
	return InAssetData.AssetClassPath == CharacterProfileClassPath;
}

EDataValidationResult UPaper2DPlusCharacterProfileAssetValidator::ValidateLoadedAsset_Implementation(
	const FAssetData& InAssetData,
	UObject* InAsset,
	FDataValidationContext& Context)
{
	UPaper2DPlusCharacterProfileAsset* CharacterProfileAsset = Cast<UPaper2DPlusCharacterProfileAsset>(InAsset);
	if (!CharacterProfileAsset)
	{
		return EDataValidationResult::NotValidated;
	}

	TArray<FCharacterProfileValidationIssue> Issues;
	const bool bValid = CharacterProfileAsset->ValidateCharacterProfileAsset(Issues);

	for (const FCharacterProfileValidationIssue& Issue : Issues)
	{
		const FString ContextPrefix = Issue.Context.IsEmpty() ? FString() : FString::Printf(TEXT("%s: "), *Issue.Context);
		const FText MessageText = FText::FromString(ContextPrefix + Issue.Message);

		switch (Issue.Severity)
		{
		case ECharacterProfileValidationSeverity::Error:
			AssetFails(CharacterProfileAsset, MessageText);
			break;
		case ECharacterProfileValidationSeverity::Warning:
			AssetWarning(CharacterProfileAsset, MessageText);
			break;
		default:
			AssetMessage(InAssetData, EMessageSeverity::Info, MessageText);
			break;
		}
	}

	if (bValid)
	{
		AssetPasses(CharacterProfileAsset);
		return EDataValidationResult::Valid;
	}

	return EDataValidationResult::Invalid;
}
