// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "Paper2DPlusProfileEditorToolbar.h"

#include "Framework/Commands/InputBindingManager.h"
#include "Framework/Commands/UICommandList.h"
#include "Kismet2/DebuggerCommands.h"
#include "ToolMenu.h"
#include "ToolMenus.h"

namespace Paper2DPlusProfileEditorToolbar
{
	const FName OwnerName(TEXT("Paper2DPlus.ProfileEditorPlayToolbar"));
	const FName PlaySectionName(TEXT("Paper2DPlusPlay"));

	void Install(
		FName ToolbarMenuName,
		const TSharedRef<FUICommandList>& ToolkitCommands)
	{
		// Every TCommands<FPlayWorldCommands> call from this module is a trap on UE 5.0-5.4:
		// the template's backing instance is per-module there, so Get() reads a copy that stays
		// unset even after the level editor registered the commands (an IsValid assert), and
		// Register() does not even link - its instantiation needs the private, unexported
		// FPlayWorldCommands constructor. Resolve the engine-side registration through the
		// process-wide input binding manager instead; a registered "PlayWorld" context plus a
		// bound global action list prove the engine's own instance is live, so BuildToolbar -
		// which executes inside UnrealEd against that instance - is safe to call.
		const TSharedPtr<FUICommandInfo> RepeatLastPlay =
			FInputBindingManager::Get().FindCommandInContext(
				TEXT("PlayWorld"), TEXT("RepeatLastPlay"));
		if (!RepeatLastPlay.IsValid()
			|| !FPlayWorldCommands::GlobalPlayWorldActions.IsValid())
		{
			// A minimal automation host can reach editor startup without the play-world stack.
			// The shared PIE group is everything this helper installs, so skip it there.
			return;
		}

		ToolkitCommands->Append(
			FPlayWorldCommands::GlobalPlayWorldActions.ToSharedRef());

		FToolMenuOwnerScoped OwnerScope(OwnerName);
		UToolMenu& Toolbar = *UToolMenus::Get()->ExtendMenu(ToolbarMenuName);
		FToolMenuSection& PlaySection = Toolbar.AddSection(
			PlaySectionName,
			TAttribute<FText>(),
			FToolMenuInsert(TEXT("Asset"), EToolMenuInsertType::After));
		if (!PlaySection.FindEntry(RepeatLastPlay->GetCommandName()))
		{
			FPlayWorldCommands::BuildToolbar(PlaySection);
		}
	}
}
