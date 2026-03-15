// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "CharacterProfileAssetActions.h"
#include "CharacterProfileAssetEditor.h"

void FCharacterProfileAssetActions::OpenAssetEditor(const TArray<UObject*>& InObjects, TSharedPtr<IToolkitHost> EditWithinLevelEditor)
{
	EToolkitMode::Type Mode = EditWithinLevelEditor.IsValid() ? EToolkitMode::WorldCentric : EToolkitMode::Standalone;

	for (UObject* Object : InObjects)
	{
		if (UPaper2DPlusCharacterProfileAsset* Asset = Cast<UPaper2DPlusCharacterProfileAsset>(Object))
		{
			// UAssetEditorSubsystem handles single-instance — if already open, focuses existing tab
			TSharedRef<FCharacterProfileAssetEditorToolkit> Toolkit = MakeShared<FCharacterProfileAssetEditorToolkit>();
			Toolkit->InitEditor(Mode, EditWithinLevelEditor, Asset);
		}
	}
}
