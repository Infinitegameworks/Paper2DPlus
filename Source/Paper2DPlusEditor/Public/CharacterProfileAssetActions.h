// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AssetTypeActions_Base.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Paper2DPlusEditorModule.h"

/**
 * Asset type actions for Paper2DPlusCharacterProfileAsset.
 */
class PAPER2DPLUSEDITOR_API FCharacterProfileAssetActions : public FAssetTypeActions_Base
{
public:
	virtual FText GetName() const override { return NSLOCTEXT("AssetTypeActions", "CharacterProfileAsset", "Paper2D+ Character Profile"); }
	virtual FColor GetTypeColor() const override { return FColor(100, 200, 255); }
	virtual UClass* GetSupportedClass() const override { return UPaper2DPlusCharacterProfileAsset::StaticClass(); }
	virtual uint32 GetCategories() override { return FPaper2DPlusEditorModule::GetAssetCategory(); }
	virtual void OpenAssetEditor(const TArray<UObject*>& InObjects, TSharedPtr<class IToolkitHost> EditWithinLevelEditor = TSharedPtr<IToolkitHost>()) override;
};
