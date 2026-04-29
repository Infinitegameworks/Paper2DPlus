// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EditorValidatorBase.h"
#include "Paper2DPlusCharacterProfileAssetValidator.generated.h"

/**
 * DataValidation subsystem validator for Paper2D+ Character Profile assets.
 * Validation overrides require UE 5.4+ (FDataValidationContext API).
 * On older versions the class exists but has no overrides — it won't be registered.
 */
UCLASS()
class PAPER2DPLUSEDITOR_API UPaper2DPlusCharacterProfileAssetValidator : public UEditorValidatorBase
{
	GENERATED_BODY()

public:
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 4)
	virtual bool CanValidateAsset_Implementation(const FAssetData& InAssetData, UObject* InObject, FDataValidationContext& InContext) const override;
	virtual EDataValidationResult ValidateLoadedAsset_Implementation(const FAssetData& InAssetData, UObject* InAsset, FDataValidationContext& Context) override;
#endif
};
