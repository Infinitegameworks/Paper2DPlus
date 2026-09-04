// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Paper2DPlusProfileEditorToolbar.h"

#include "Framework/Commands/InputBindingManager.h"
#include "Framework/Commands/UICommandList.h"
#include "Kismet2/DebuggerCommands.h"
#include "Misc/AutomationTest.h"
#include "ToolMenu.h"
#include "ToolMenus.h"

namespace Paper2DPlusProfileEditorToolbarTest
{
	class FScopedTestMenu final
	{
	public:
		explicit FScopedTestMenu(FName InMenuName)
			: MenuName(InMenuName)
		{
			UToolMenus::Get()->RemoveMenu(MenuName);
		}

		~FScopedTestMenu()
		{
			UToolMenus::Get()->RemoveMenu(MenuName);
		}

	private:
		FName MenuName;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusProfileEditorPlayToolbarTest,
	"Paper2DPlus.Editor.ProfileToolbar.PlayWorldControls",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusProfileEditorPlayToolbarTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusProfileEditorToolbarTest;

	const FName ToolbarMenuName(
		TEXT("AssetEditor.Paper2DPlusProfileEditorToolbarTest.ToolBar"));
	FScopedTestMenu MenuCleanup(ToolbarMenuName);
	const TSharedRef<FUICommandList> ToolkitCommands = MakeShared<FUICommandList>();

	// Resolve the engine's play-world commands through the process-wide binding manager, never
	// through TCommands<FPlayWorldCommands>::Get() - on UE 5.0-5.4 that template reads this
	// module's own (unset) instance copy and asserts. Install shares the same gate, so a host
	// without the play-world stack skips instead of asserting.
	auto FindPlayWorldCommand = [](const TCHAR* CommandName)
	{
		return FInputBindingManager::Get().FindCommandInContext(
			TEXT("PlayWorld"), FName(CommandName));
	};
	const TSharedPtr<FUICommandInfo> RepeatLastPlay =
		FindPlayWorldCommand(TEXT("RepeatLastPlay"));
	if (!RepeatLastPlay.IsValid()
		|| !FPlayWorldCommands::GlobalPlayWorldActions.IsValid())
	{
		AddInfo(TEXT(
			"Skipped: this host never registered the play-world command stack, so Install "
			"deliberately leaves the shared PIE group out."));
		return true;
	}

	Paper2DPlusProfileEditorToolbar::Install(ToolbarMenuName, ToolkitCommands);
	UToolMenus::Get()->RegisterMenu(
		ToolbarMenuName,
		NAME_None,
		EMultiBoxType::SlimHorizontalToolBar);
	TestTrue(
		TEXT("Profile editor toolbar is registered"),
		UToolMenus::Get()->IsMenuRegistered(ToolbarMenuName));

	UToolMenu* Toolbar = UToolMenus::Get()->FindMenu(ToolbarMenuName);
	if (!TestNotNull(TEXT("Profile editor registers its toolbar menu"), Toolbar))
	{
		return false;
	}

	FToolMenuSection* PlaySection = Toolbar->FindSection(
		Paper2DPlusProfileEditorToolbar::PlaySectionName);
	if (!TestNotNull(TEXT("Profile editor toolbar has a Play section"), PlaySection))
	{
		return false;
	}

	const TSharedPtr<FUICommandInfo> ResumePlaySession =
		FindPlayWorldCommand(TEXT("ResumePlaySession"));
	const TSharedPtr<FUICommandInfo> PausePlaySession =
		FindPlayWorldCommand(TEXT("PausePlaySession"));
	const TSharedPtr<FUICommandInfo> SingleFrameAdvance =
		FindPlayWorldCommand(TEXT("SingleFrameAdvance"));
	const TSharedPtr<FUICommandInfo> StopPlaySession =
		FindPlayWorldCommand(TEXT("StopPlaySession"));
	if (!ResumePlaySession.IsValid() || !PausePlaySession.IsValid()
		|| !SingleFrameAdvance.IsValid() || !StopPlaySession.IsValid())
	{
		AddError(TEXT(
			"The PlayWorld context is registered but is missing standard commands."));
		return false;
	}

	TestNotNull(
		TEXT("Play section exposes Unreal's primary Play action"),
		PlaySection->FindEntry(RepeatLastPlay->GetCommandName()));
	TestNotNull(
		TEXT("Play section exposes Unreal's Resume action"),
		PlaySection->FindEntry(ResumePlaySession->GetCommandName()));
	TestNotNull(
		TEXT("Play section exposes Unreal's Pause action"),
		PlaySection->FindEntry(PausePlaySession->GetCommandName()));
	TestNotNull(
		TEXT("Play section exposes Unreal's frame-step action"),
		PlaySection->FindEntry(SingleFrameAdvance->GetCommandName()));
	TestNotNull(
		TEXT("Play section exposes Unreal's Stop action"),
		PlaySection->FindEntry(StopPlaySession->GetCommandName()));
	TestNotNull(
		TEXT("Play section exposes Unreal's PIE options"),
		PlaySection->FindEntry(TEXT("PIECombo")));

	const int32 FirstInstallEntryCount = PlaySection->Blocks.Num();
	Paper2DPlusProfileEditorToolbar::Install(ToolbarMenuName, ToolkitCommands);
	PlaySection = Toolbar->FindSection(
		Paper2DPlusProfileEditorToolbar::PlaySectionName);
	if (!TestNotNull(TEXT("A repeated install retains the Play section"), PlaySection))
	{
		return false;
	}
	TestEqual(
		TEXT("A repeated install does not duplicate Play controls"),
		PlaySection->Blocks.Num(),
		FirstInstallEntryCount);

	TestNotNull(
		TEXT("Profile toolkit resolves the global Play action"),
		ToolkitCommands->GetActionForCommand(RepeatLastPlay));
	TestNotNull(
		TEXT("Profile toolkit resolves the global Resume action"),
		ToolkitCommands->GetActionForCommand(ResumePlaySession));
	TestNotNull(
		TEXT("Profile toolkit resolves the global Pause action"),
		ToolkitCommands->GetActionForCommand(PausePlaySession));
	TestNotNull(
		TEXT("Profile toolkit resolves the global frame-step action"),
		ToolkitCommands->GetActionForCommand(SingleFrameAdvance));
	TestNotNull(
		TEXT("Profile toolkit resolves the global Stop action"),
		ToolkitCommands->GetActionForCommand(StopPlaySession));

	return true;
}

#endif
