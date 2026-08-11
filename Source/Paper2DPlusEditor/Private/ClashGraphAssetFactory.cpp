// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "ClashGraphAssetFactory.h"

#include "Paper2DPlusEditorModule.h"
#include "Paper2DPlusClashGraphAsset.h"

#define LOCTEXT_NAMESPACE "ClashGraphAssetFactory"

UClashGraphAssetFactory::UClashGraphAssetFactory()
{
	bCreateNew = true;
	bEditAfterNew = true;
	SupportedClass = UPaper2DPlusClashGraphAsset::StaticClass();
}

UObject* UClashGraphAssetFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	UPaper2DPlusClashGraphAsset* NewAsset = NewObject<UPaper2DPlusClashGraphAsset>(InParent, Class, Name, Flags);
	if (NewAsset)
	{
		// The constructor already seeds the genre-default RPS edges; name the asset for readability.
		NewAsset->DisplayName = Name.ToString();
	}
	return NewAsset;
}

FText UClashGraphAssetFactory::GetDisplayName() const
{
	return LOCTEXT("FactoryDisplayName", "Paper2D+ Clash Graph");
}

uint32 UClashGraphAssetFactory::GetMenuCategories() const
{
	return FPaper2DPlusEditorModule::GetAssetCategory();
}

#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 8)
TArray<FAssetCategoryPath> UClashGraphAssetFactory::GetAssetMenuPathsForCategory(FName InCategory) const
{
	return { FAssetCategoryPath(
		FText::FromName(InCategory),
		NSLOCTEXT("Paper2DPlusAssetMenu", "GameplaySection", "Gameplay"),
		ECategoryMenuType::Section) };
}
#endif

#undef LOCTEXT_NAMESPACE
