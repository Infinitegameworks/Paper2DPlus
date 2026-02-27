// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "CharacterDataAssetFactory.h"
#include "Paper2DPlusCharacterDataAsset.h"
#include "Paper2DPlusEditorModule.h"
#include "AssetTypeCategories.h"

#define LOCTEXT_NAMESPACE "CharacterDataAssetFactory"

UCharacterDataAssetFactory::UCharacterDataAssetFactory()
{
	bCreateNew = true;
	bEditAfterNew = true;
	SupportedClass = UPaper2DPlusCharacterDataAsset::StaticClass();
}

UObject* UCharacterDataAssetFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	UPaper2DPlusCharacterDataAsset* NewAsset = NewObject<UPaper2DPlusCharacterDataAsset>(InParent, Class, Name, Flags);

	if (NewAsset)
	{
		NewAsset->DisplayName = Name.ToString();

		FAnimationHitboxData DefaultAnimation;
		DefaultAnimation.AnimationName = TEXT("Default");

		FFrameHitboxData DefaultFrame;
		DefaultFrame.FrameName = TEXT("Frame_0");
		DefaultAnimation.Frames.Add(DefaultFrame);

		NewAsset->Animations.Add(DefaultAnimation);
	}

	return NewAsset;
}

FText UCharacterDataAssetFactory::GetDisplayName() const
{
	return LOCTEXT("FactoryDisplayName", "Paper2D+ Character Data");
}

uint32 UCharacterDataAssetFactory::GetMenuCategories() const
{
	return FPaper2DPlusEditorModule::GetAssetCategory();
}

#undef LOCTEXT_NAMESPACE
