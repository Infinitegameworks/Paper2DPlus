// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "CharacterCatalogAssetFactory.h"

#include "Paper2DPlusCharacterCatalogAsset.h"

UCharacterCatalogAssetFactory::UCharacterCatalogAssetFactory()
{
	SupportedClass = UPaper2DPlusCharacterCatalogAsset::StaticClass();
	bCreateNew = true;
	bEditAfterNew = true;
}

UObject* UCharacterCatalogAssetFactory::FactoryCreateNew(
	UClass* Class,
	UObject* InParent,
	FName Name,
	EObjectFlags Flags,
	UObject* Context,
	FFeedbackContext* Warn)
{
	return NewObject<UPaper2DPlusCharacterCatalogAsset>(InParent, Class, Name, Flags);
}

#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 8)
TArray<FAssetCategoryPath> UCharacterCatalogAssetFactory::GetAssetMenuPathsForCategory(FName InCategory) const
{
	return { FAssetCategoryPath(
		FText::FromName(InCategory),
		NSLOCTEXT("Paper2DPlusAssetMenu", "CharactersSection", "Characters"),
		ECategoryMenuType::Section) };
}
#endif
