// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "CharacterDataAssetActions.h"
#include "CharacterDataAssetEditor.h"

void FCharacterDataAssetActions::OpenAssetEditor(const TArray<UObject*>& InObjects, TSharedPtr<IToolkitHost> EditWithinLevelEditor)
{
	for (UObject* Object : InObjects)
	{
		if (UPaper2DPlusCharacterDataAsset* Asset = Cast<UPaper2DPlusCharacterDataAsset>(Object))
		{
			FCharacterDataAssetEditorToolkit::OpenEditor(Asset);
		}
	}
}
