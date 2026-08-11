// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/EngineVersionComparison.h"
#include "EditorValidatorBase.h"
#include "Paper2DPlusCharacterProfileAssetValidator.generated.h"

/**
 * DataValidation subsystem bridge for every asset registered with the Paper2D+ validation service.
 * Character, Layer, Effect, Combat, and Catalog therefore report the same normalized issues in
 * editor panels, Content Browser validation, Data Validation, and the CI commandlet.
 * UE 5.4+ uses FDataValidationContext; UE 5.0-5.3 use the legacy BlueprintNativeEvent signatures.
 */
UCLASS()
class PAPER2DPLUSEDITOR_API UPaper2DPlusCharacterProfileAssetValidator : public UEditorValidatorBase
{
	GENERATED_BODY()

public:
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 4)
	virtual bool CanValidateAsset_Implementation(const FAssetData& InAssetData, UObject* InObject, FDataValidationContext& InContext) const override;
	virtual EDataValidationResult ValidateLoadedAsset_Implementation(const FAssetData& InAssetData, UObject* InAsset, FDataValidationContext& Context) override;
#else
	virtual bool CanValidateAsset_Implementation(UObject* InAsset) const override;
	virtual EDataValidationResult ValidateLoadedAsset_Implementation(
		UObject* InAsset,
		TArray<FText>& ValidationErrors) override;
#endif
};
