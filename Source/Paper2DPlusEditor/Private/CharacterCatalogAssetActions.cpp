// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "CharacterCatalogAssetActions.h"

#include "CharacterCatalogAssetEditorToolkit.h"
#include "Paper2DPlusCharacterCatalogAsset.h"
#include "Paper2DPlusEditorModule.h"

#define LOCTEXT_NAMESPACE "Paper2DPlusCharacterCatalogAssetActions"

FText FCharacterCatalogAssetActions::GetName() const
{
	return LOCTEXT("CharacterCatalogAssetName", "Character Catalog");
}

FColor FCharacterCatalogAssetActions::GetTypeColor() const
{
	return FColor(240, 220, 70);
}

UClass* FCharacterCatalogAssetActions::GetSupportedClass() const
{
	return UPaper2DPlusCharacterCatalogAsset::StaticClass();
}

uint32 FCharacterCatalogAssetActions::GetCategories()
{
	return FPaper2DPlusEditorModule::GetAssetCategory();
}

void FCharacterCatalogAssetActions::OpenAssetEditor(
	const TArray<UObject*>& InObjects,
	TSharedPtr<IToolkitHost> EditWithinLevelEditor)
{
	const EToolkitMode::Type Mode = EditWithinLevelEditor.IsValid()
		? EToolkitMode::WorldCentric
		: EToolkitMode::Standalone;
	for (UObject* Object : InObjects)
	{
		if (UPaper2DPlusCharacterCatalogAsset* Catalog = Cast<UPaper2DPlusCharacterCatalogAsset>(Object))
		{
			TSharedRef<FCharacterCatalogAssetEditorToolkit> Toolkit = MakeShared<FCharacterCatalogAssetEditorToolkit>();
			Toolkit->InitEditor(Mode, EditWithinLevelEditor, Catalog);
		}
	}
}

#undef LOCTEXT_NAMESPACE
