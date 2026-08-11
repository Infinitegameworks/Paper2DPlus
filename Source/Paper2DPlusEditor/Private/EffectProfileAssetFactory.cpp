// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "EffectProfileAssetFactory.h"

#include "Paper2DPlusEditorModule.h"
#include "Paper2DPlusEffectProfileAsset.h"

#define LOCTEXT_NAMESPACE "EffectProfileAssetFactory"

UEffectProfileAssetFactory::UEffectProfileAssetFactory()
{
	bCreateNew = true;
	bEditAfterNew = true;
	SupportedClass = UPaper2DPlusEffectProfileAsset::StaticClass();
}

UObject* UEffectProfileAssetFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	UPaper2DPlusEffectProfileAsset* NewAsset = NewObject<UPaper2DPlusEffectProfileAsset>(InParent, Class, Name, Flags);
	if (NewAsset)
	{
		NewAsset->DisplayName = Name.ToString();
	}
	return NewAsset;
}

FText UEffectProfileAssetFactory::GetDisplayName() const
{
	return LOCTEXT("FactoryDisplayName", "Paper2D+ Effect Profile");
}

uint32 UEffectProfileAssetFactory::GetMenuCategories() const
{
	return FPaper2DPlusEditorModule::GetAssetCategory();
}

#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 8)
TArray<FAssetCategoryPath> UEffectProfileAssetFactory::GetAssetMenuPathsForCategory(FName InCategory) const
{
	return { FAssetCategoryPath(
		FText::FromName(InCategory),
		NSLOCTEXT("Paper2DPlusAssetMenu", "EffectsAndCuesSection", "Effects & Cues"),
		ECategoryMenuType::Section) };
}
#endif

#undef LOCTEXT_NAMESPACE
