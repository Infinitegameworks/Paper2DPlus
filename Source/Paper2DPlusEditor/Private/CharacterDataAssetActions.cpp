// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "CharacterDataAssetActions.h"
#include "CharacterDataAssetEditor.h"

void FCharacterDataAssetActions::OpenAssetEditor(const TArray<UObject*>& InObjects, TSharedPtr<IToolkitHost> EditWithinLevelEditor)
{
	EToolkitMode::Type Mode = EditWithinLevelEditor.IsValid() ? EToolkitMode::WorldCentric : EToolkitMode::Standalone;

	for (UObject* Object : InObjects)
	{
		if (UPaper2DPlusCharacterDataAsset* Asset = Cast<UPaper2DPlusCharacterDataAsset>(Object))
		{
			// UAssetEditorSubsystem handles single-instance — if already open, focuses existing tab
			TSharedRef<FCharacterDataAssetEditorToolkit> Toolkit = MakeShared<FCharacterDataAssetEditorToolkit>();
			Toolkit->InitEditor(Mode, EditWithinLevelEditor, Asset);
		}
	}
}
