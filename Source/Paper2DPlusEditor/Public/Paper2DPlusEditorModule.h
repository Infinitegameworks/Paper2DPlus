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
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 4)
	void RegisterDataValidators();
	void UnregisterDataValidators();
#endif

	TArray<TSharedPtr<class IAssetTypeActions>> RegisteredAssetTypeActions;
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 4)
	class UEditorValidatorBase* RegisteredCharacterProfileValidator = nullptr;
#endif

	/** Custom asset category for Paper2D+ assets */
	static EAssetTypeCategories::Type Paper2DPlusAssetCategory;
};
