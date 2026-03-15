// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EditorValidatorBase.h"
#include "Paper2DPlusCharacterProfileAssetValidator.generated.h"

/**
 * DataValidation subsystem validator for Paper2D+ Character Profile assets.
 */
UCLASS()
class PAPER2DPLUSEDITOR_API UPaper2DPlusCharacterProfileAssetValidator : public UEditorValidatorBase
{
	GENERATED_BODY()

public:
	virtual bool CanValidateAsset_Implementation(const FAssetData& InAssetData, UObject* InObject, FDataValidationContext& InContext) const override;
	virtual EDataValidationResult ValidateLoadedAsset_Implementation(const FAssetData& InAssetData, UObject* InAsset, FDataValidationContext& Context) override;
};
