// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "AssetTypeCategories.h"

PAPER2DPLUSEDITOR_API DECLARE_LOG_CATEGORY_EXTERN(LogPaper2DPlusEditor, Log, All);

class PAPER2DPLUSEDITOR_API FPaper2DPlusEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	/** Get the custom asset category for Paper2D+ assets */
	static EAssetTypeCategories::Type GetAssetCategory() { return Paper2DPlusAssetCategory; }

private:
	void RegisterAssetTools();
	void UnregisterAssetTools();
	void RegisterMenuExtensions();
	void RegisterDataValidators();
	void UnregisterDataValidators();

	TArray<TSharedPtr<class IAssetTypeActions>> RegisteredAssetTypeActions;
	class UEditorValidatorBase* RegisteredCharacterProfileValidator = nullptr;

	/** Custom asset category for Paper2D+ assets */
	static EAssetTypeCategories::Type Paper2DPlusAssetCategory;
};
