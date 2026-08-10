// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AssetTypeActions_Base.h"
#include "Paper2DPlusCharacterLayerAsset.h"
#include "Paper2DPlusEditorModule.h"

class PAPER2DPLUSEDITOR_API FCharacterLayerAssetActions : public FAssetTypeActions_Base
{
public:
	/** Transactional designer workflow. Fixed-baked assets must be safely detached before changing delivery mode. */
	static bool EnableRuntimeCustomization(
		UPaper2DPlusCharacterLayerAsset* Asset,
		bool bRequestConfirmation = true,
		FText* OutMessage = nullptr);

	virtual FText GetName() const override { return NSLOCTEXT("AssetTypeActions", "CharacterLayerAsset", "Paper2D+ Character Layer Asset"); }
	virtual FColor GetTypeColor() const override { return FColor(255, 170, 80); }
	virtual UClass* GetSupportedClass() const override { return UPaper2DPlusCharacterLayerAsset::StaticClass(); }
	virtual uint32 GetCategories() override { return FPaper2DPlusEditorModule::GetAssetCategory(); }
	virtual void OpenAssetEditor(const TArray<UObject*>& InObjects, TSharedPtr<class IToolkitHost> EditWithinLevelEditor = TSharedPtr<IToolkitHost>()) override;
	virtual void GetActions(const TArray<UObject*>& InObjects, struct FToolMenuSection& Section) override;
};
