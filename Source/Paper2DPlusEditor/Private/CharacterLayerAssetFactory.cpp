// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "CharacterLayerAssetFactory.h"
#include "Paper2DPlusCharacterLayerAsset.h"
#include "Paper2DPlusEditorModule.h"
#include "AssetTypeCategories.h"

#define LOCTEXT_NAMESPACE "CharacterLayerAssetFactory"

UCharacterLayerAssetFactory::UCharacterLayerAssetFactory()
{
	bCreateNew = true;
	bEditAfterNew = true;
	SupportedClass = UPaper2DPlusCharacterLayerAsset::StaticClass();
}

UObject* UCharacterLayerAssetFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	UPaper2DPlusCharacterLayerAsset* NewAsset = NewObject<UPaper2DPlusCharacterLayerAsset>(InParent, Class, Name, Flags);
	if (NewAsset)
	{
		NewAsset->DisplayName = Name.ToString();
	}
	return NewAsset;
}

FText UCharacterLayerAssetFactory::GetDisplayName() const
{
	return LOCTEXT("FactoryDisplayName", "Paper2D+ Character Layer Asset");
}

uint32 UCharacterLayerAssetFactory::GetMenuCategories() const
{
	return FPaper2DPlusEditorModule::GetAssetCategory();
}

#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 8)
TArray<FAssetCategoryPath> UCharacterLayerAssetFactory::GetAssetMenuPathsForCategory(FName InCategory) const
{
	return { FAssetCategoryPath(
		FText::FromName(InCategory),
		NSLOCTEXT("Paper2DPlusAssetMenu", "CharactersSection", "Characters"),
		ECategoryMenuType::Section) };
}
#endif

#undef LOCTEXT_NAMESPACE
