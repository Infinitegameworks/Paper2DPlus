// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AssetTypeActions_Base.h"
#include "Paper2DPlusEffectProfileAsset.h"
#include "Paper2DPlusEditorModule.h"

class PAPER2DPLUSEDITOR_API FEffectProfileAssetActions : public FAssetTypeActions_Base
{
public:
	virtual FText GetName() const override { return NSLOCTEXT("AssetTypeActions", "EffectProfileAsset", "Paper2D+ Effect Profile"); }
	virtual FColor GetTypeColor() const override { return FColor(80, 210, 130); }
	virtual UClass* GetSupportedClass() const override { return UPaper2DPlusEffectProfileAsset::StaticClass(); }
	virtual uint32 GetCategories() override { return FPaper2DPlusEditorModule::GetAssetCategory(); }
	virtual void OpenAssetEditor(const TArray<UObject*>& InObjects, TSharedPtr<class IToolkitHost> EditWithinLevelEditor = TSharedPtr<IToolkitHost>()) override;
	virtual void GetActions(const TArray<UObject*>& InObjects, struct FToolMenuSection& Section) override;
};
