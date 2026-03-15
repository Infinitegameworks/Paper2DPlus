// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "CharacterProfileAssetFactory.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Paper2DPlusEditorModule.h"
#include "AssetTypeCategories.h"

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

		FFlipbookHitboxData DefaultFlipbook;
		DefaultFlipbook.FlipbookName = TEXT("Default");

		FFrameHitboxData DefaultFrame;
		DefaultFrame.FrameName = TEXT("Frame_0");
		DefaultFlipbook.Frames.Add(DefaultFrame);

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

#undef LOCTEXT_NAMESPACE
