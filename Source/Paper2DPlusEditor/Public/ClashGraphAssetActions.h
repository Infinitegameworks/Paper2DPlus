// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AssetTypeActions_Base.h"
#include "Paper2DPlusClashGraphAsset.h"
#include "Paper2DPlusEditorModule.h"

/**
 * Content-Browser type for UPaper2DPlusClashGraphAsset. Double-click opens the bespoke
 * FClashGraphAssetEditorToolkit (TASK-77 U5 — the computed outcome grid + raw details, with the node-graph
 * tab following). Right-click adds **Validate** (the detect-and-warn conflict pass) which logs + toasts.
 */
class PAPER2DPLUSEDITOR_API FClashGraphAssetActions : public FAssetTypeActions_Base
{
public:
	virtual FText GetName() const override { return NSLOCTEXT("AssetTypeActions", "ClashGraphAsset", "Paper2D+ Clash Graph"); }
	virtual FColor GetTypeColor() const override { return FColor(220, 90, 90); }
	virtual UClass* GetSupportedClass() const override { return UPaper2DPlusClashGraphAsset::StaticClass(); }
	virtual uint32 GetCategories() override { return FPaper2DPlusEditorModule::GetAssetCategory(); }
	virtual void OpenAssetEditor(const TArray<UObject*>& InObjects, TSharedPtr<class IToolkitHost> EditWithinLevelEditor = TSharedPtr<IToolkitHost>()) override;
	virtual void GetActions(const TArray<UObject*>& InObjects, struct FToolMenuSection& Section) override;
};
