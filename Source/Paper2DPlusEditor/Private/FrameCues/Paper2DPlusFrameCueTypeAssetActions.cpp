// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "FrameCues/Paper2DPlusFrameCueTypeAssetActions.h"

#include "FrameCues/Paper2DPlusFrameCueBlueprint.h"
#include "FrameCues/Paper2DPlusFrameCueTypeEditor.h"
#include "Misc/MessageDialog.h"
#include "Paper2DPlusEditorModule.h"

#define LOCTEXT_NAMESPACE "Paper2DPlusFrameCueTypeAssetActions"

FText FPaper2DPlusFrameCueTypeAssetActions::GetName() const
{
	return LOCTEXT("AssetTypeName", "Paper2D+ Frame Cue Type");
}

FColor FPaper2DPlusFrameCueTypeAssetActions::GetTypeColor() const
{
	return FColor(235, 110, 190);
}

UClass* FPaper2DPlusFrameCueTypeAssetActions::GetSupportedClass() const
{
	return UPaper2DPlusFrameCueBlueprint::StaticClass();
}

uint32 FPaper2DPlusFrameCueTypeAssetActions::GetCategories()
{
	return FPaper2DPlusEditorModule::GetAssetCategory();
}

void FPaper2DPlusFrameCueTypeAssetActions::OpenAssetEditor(
	const TArray<UObject*>& InObjects,
	TSharedPtr<IToolkitHost> EditWithinLevelEditor)
{
	const EToolkitMode::Type Mode = EditWithinLevelEditor.IsValid()
		? EToolkitMode::WorldCentric
		: EToolkitMode::Standalone;

	for (UObject* Object : InObjects)
	{
		UPaper2DPlusFrameCueBlueprint* CueType =
			Cast<UPaper2DPlusFrameCueBlueprint>(Object);
		if (!FPaper2DPlusFrameCueTypeEditor::IsSupportedBlueprint(CueType))
		{
			FMessageDialog::Open(
				EAppMsgType::Ok,
				FText::Format(
					LOCTEXT(
						"InvalidCueType",
						"'{0}' is not a valid Paper2D+ Cue or Cue State Type."),
					FText::FromString(GetNameSafe(Object))));
			continue;
		}

		const TSharedRef<FPaper2DPlusFrameCueTypeEditor> Editor =
			MakeShared<FPaper2DPlusFrameCueTypeEditor>();
		Editor->InitFrameCueTypeEditor(Mode, EditWithinLevelEditor, CueType);
	}
}

#undef LOCTEXT_NAMESPACE
