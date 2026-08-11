// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "CharacterProfileAssetFactory.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Paper2DPlusEditorModule.h"
#include "AssetTypeCategories.h"

/** UCharacterProfileAssetFactory — Asset factory for creating new CharacterProfile assets. */

#define LOCTEXT_NAMESPACE "CharacterProfileAssetFactory"

UCharacterProfileAssetFactory::UCharacterProfileAssetFactory()
{
	bCreateNew = true;
	bEditAfterNew = true;
	SupportedClass = UPaper2DPlusCharacterProfileAsset::StaticClass();
}

UObject* UCharacterProfileAssetFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	UPaper2DPlusCharacterProfileAsset* NewAsset = NewObject<UPaper2DPlusCharacterProfileAsset>(InParent, Class, Name, Flags);

	if (NewAsset)
	{
		NewAsset->DisplayName = Name.ToString();
		// RootMotionVersion is stamped to current in UPaper2DPlusCharacterProfileAsset::PostInitProperties,
		// which already ran inside the NewObject<> call above — no factory stamp needed (single source of truth).

		FFlipbookProfileEntry DefaultFlipbook;
		DefaultFlipbook.Identity.FlipbookName = TEXT("Default");

		FFrameHitboxData DefaultFrame;
		DefaultFrame.FrameName = TEXT("Frame_0");
		DefaultFlipbook.CombatData.Frames.Add(DefaultFrame);

		NewAsset->Flipbooks.Add(DefaultFlipbook);
	}

	return NewAsset;
}

FText UCharacterProfileAssetFactory::GetDisplayName() const
{
	return LOCTEXT("FactoryDisplayName", "Paper2D+ Character Profile");
}

uint32 UCharacterProfileAssetFactory::GetMenuCategories() const
{
	return FPaper2DPlusEditorModule::GetAssetCategory();
}

#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 8)
TArray<FAssetCategoryPath> UCharacterProfileAssetFactory::GetAssetMenuPathsForCategory(FName InCategory) const
{
	return { FAssetCategoryPath(
		FText::FromName(InCategory),
		NSLOCTEXT("Paper2DPlusAssetMenu", "CharactersSection", "Characters"),
		ECategoryMenuType::Section) };
}
#endif

#undef LOCTEXT_NAMESPACE
