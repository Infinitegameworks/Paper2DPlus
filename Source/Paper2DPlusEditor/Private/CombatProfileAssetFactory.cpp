// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "CombatProfileAssetFactory.h"

#include "AssetTypeCategories.h"
#include "Paper2DPlusCombatProfileAsset.h"
#include "Paper2DPlusEditorModule.h"

#define LOCTEXT_NAMESPACE "CombatProfileAssetFactory"

UCombatProfileAssetFactory::UCombatProfileAssetFactory()
{
	bCreateNew = true;
	bEditAfterNew = true;
	SupportedClass = UPaper2DPlusCombatProfileAsset::StaticClass();
}

UObject* UCombatProfileAssetFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	UPaper2DPlusCombatProfileAsset* NewAsset = NewObject<UPaper2DPlusCombatProfileAsset>(InParent, Class, Name, Flags);
	if (NewAsset && NewAsset->ScoringProfiles.IsEmpty())
	{
		FPaper2DPlusCombatScoringProfile DefaultProfile;
		DefaultProfile.ProfileName = TEXT("Default");
		NewAsset->ScoringProfiles.Add(DefaultProfile);
		NewAsset->RebuildVariableBags();
	}
	return NewAsset;
}

FText UCombatProfileAssetFactory::GetDisplayName() const
{
	return LOCTEXT("FactoryDisplayName", "Paper2D+ Combat Profile");
}

uint32 UCombatProfileAssetFactory::GetMenuCategories() const
{
	return FPaper2DPlusEditorModule::GetAssetCategory();
}

#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 8)
TArray<FAssetCategoryPath> UCombatProfileAssetFactory::GetAssetMenuPathsForCategory(FName InCategory) const
{
	return { FAssetCategoryPath(
		FText::FromName(InCategory),
		NSLOCTEXT("Paper2DPlusAssetMenu", "GameplaySection", "Gameplay"),
		ECategoryMenuType::Section) };
}
#endif

#undef LOCTEXT_NAMESPACE
