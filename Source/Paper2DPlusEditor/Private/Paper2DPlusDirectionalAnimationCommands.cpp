// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "Paper2DPlusDirectionalAnimationCommands.h"

#include "InputCoreTypes.h"
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
#include "EditorStyleSet.h"
#define FAppStyle FEditorStyle
#define GetAppStyleSetName GetStyleSetName
#else
#include "Styling/AppStyle.h"
#endif

#define LOCTEXT_NAMESPACE "Paper2DPlusDirectionalAnimationCommands"

FPaper2DPlusDirectionalAnimationCommands::FPaper2DPlusDirectionalAnimationCommands()
	: TCommands<FPaper2DPlusDirectionalAnimationCommands>(
		TEXT("Paper2DPlus.CharacterProfile"),
		LOCTEXT("ContextDescription", "Character Profile"),
		NAME_None,
		FAppStyle::GetAppStyleSetName())
{
}

void FPaper2DPlusDirectionalAnimationCommands::RegisterCommands()
{
	UI_COMMAND(
		OpenDirectionWheel,
		"Direction Wheel",
		"Open the Character Profile direction wheel at the pointer.",
		EUserInterfaceActionType::Button,
		FInputChord(EKeys::D, EModifierKey::Alt));
}

#undef LOCTEXT_NAMESPACE
