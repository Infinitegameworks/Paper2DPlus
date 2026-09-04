// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "CharacterProfileAssetEditor.h"
#include "CharacterCoverage/SExpectedTagsPanel.h"
#include "CharacterProfileEditorModel.h"
#include "CharacterProfileJsonInteraction.h"
#include "Paper2DPlusDirectionalAnimationCommands.h"
#include "SDirectionalAnimationCommandRouter.h"
#include "SDirectionalAnimationWheel.h"
#include "SlateShortcutUtils.h"
#include "OverviewPanel.h"
#include "FlipbookListPanel.h"
#include "HitboxEditorPanel.h"
#include "SpriteEditorPanel.h"
#include "FrameTimingEditor.h"
#include "FrameEventEditor.h"
#include "RootMotionEditor.h"
#include "AnimationMapPanel.h"
#include "AnimationsPanel.h"
#include "Paper2DPlusEditorIcons.h"
#include "Paper2DPlusProfileEditorToolbar.h"
#include "ProfileValidationPanel.h"
#include "ProfileToolsWindow.h"
#include "RelatedProfileBar.h"
#include "AnimationProfilePickerSource.h"
#include "AnimationProfileSwitcher.h"
#include "CharacterCompletionPanel.h"
#include "PlaybackQueuePanel.h"
#include "ProfileToolPanelHost.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/IMenu.h"
#include "Framework/Commands/InputChord.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/MultiBox/MultiBoxExtender.h"
#include "InputCoreTypes.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "AssetRegistry/AssetData.h"
#include "Async/Async.h"
#include "PaperFlipbook.h"
#include "ScopedTransaction.h"
#include "Styling/CoreStyle.h"
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
#include "EditorStyleSet.h"
#ifndef FAppStyle
#define FAppStyle FEditorStyle
#define GetAppStyleSetName GetStyleSetName
#endif
#else
#include "Styling/AppStyle.h"
#endif

#define LOCTEXT_NAMESPACE "CharacterProfileAssetEditor"

const FName FCharacterProfileAssetEditorToolkit::AnimationsTabId(TEXT("CharacterProfileEditor_Animations"));
const FName FCharacterProfileAssetEditorToolkit::FlipbookListTabId(TEXT("CharacterProfileEditor_FlipbookList"));
const FName FCharacterProfileAssetEditorToolkit::HitboxEditorTabId(TEXT("CharacterProfileEditor_HitboxEditor"));
const FName FCharacterProfileAssetEditorToolkit::SpriteEditorTabId(TEXT("CharacterProfileEditor_SpriteEditor"));
const FName FCharacterProfileAssetEditorToolkit::FrameTimingTabId(TEXT("CharacterProfileEditor_FrameTiming"));
const FName FCharacterProfileAssetEditorToolkit::FrameEventsTabId(TEXT("CharacterProfileEditor_FrameEvents"));
const FName FCharacterProfileAssetEditorToolkit::RootMotionTabId(TEXT("CharacterProfileEditor_RootMotion"));
const FName FCharacterProfileAssetEditorToolkit::ContextHostTabId(TEXT("CharacterProfileEditor_ContextPanels"));
const FName FCharacterProfileAssetEditorToolkit::ExpectedTagsTabId(TEXT("CharacterProfileEditor_ExpectedTags"));
const FName FCharacterProfileAssetEditorToolkit::CompletionTabId(TEXT("CharacterProfileEditor_Completion"));
const FName FCharacterProfileAssetEditorToolkit::RelatedProfilesTabId(TEXT("CharacterProfileEditor_RelatedProfiles"));
const FName FCharacterProfileAssetEditorToolkit::PlaybackQueueTabId(TEXT("CharacterProfileEditor_PlaybackQueue"));
TWeakPtr<FCharacterProfileAssetEditorToolkit> FCharacterProfileAssetEditorToolkit::GActiveCharacterProfileToolkit;

TSharedRef<FTabManager::FLayout> FCharacterProfileAssetEditorToolkit::CreateDefaultWorkspaceLayout()
{
	// _v15 is intentionally a new key: Expected Tags is now a permanent sibling of contextual Details
	// in the upper-right stack. A saved _v14 arrangement has no slot for the new tab, so the key must
	// move. Details remains foreground; Validation remains modeless and Navigator begins closed.
	return FTabManager::NewLayout("CharacterProfileAssetEditor_Layout_v15_ExpectedTags")
		->AddArea
		(
			FTabManager::NewPrimaryArea()
			->SetOrientation(Orient_Horizontal)
			->Split
			(
				FTabManager::NewStack()
				->SetSizeCoefficient(0.18f)
				->AddTab(FlipbookListTabId, ETabState::ClosedTab)
			)
			->Split
			(
				FTabManager::NewStack()
				->SetSizeCoefficient(0.57f)
				->AddTab(AnimationsTabId, ETabState::OpenedTab)
				->AddTab(HitboxEditorTabId, ETabState::OpenedTab)
				->AddTab(SpriteEditorTabId, ETabState::OpenedTab)
				->AddTab(FrameTimingTabId, ETabState::OpenedTab)
				->AddTab(FrameEventsTabId, ETabState::OpenedTab)
				->AddTab(RootMotionTabId, ETabState::OpenedTab)
				->SetForegroundTab(AnimationsTabId)
			)
			->Split
			(
				FTabManager::NewSplitter()
				->SetOrientation(Orient_Vertical)
				->SetSizeCoefficient(0.25f)
				->Split
				(
					FTabManager::NewStack()
					->SetSizeCoefficient(0.64f)
					->AddTab(ContextHostTabId, ETabState::OpenedTab)
					->AddTab(ExpectedTagsTabId, ETabState::OpenedTab)
					->SetForegroundTab(ContextHostTabId)
				)
				->Split
				(
					FTabManager::NewStack()
					->SetSizeCoefficient(0.36f)
					->AddTab(CompletionTabId, ETabState::OpenedTab)
					->AddTab(RelatedProfilesTabId, ETabState::OpenedTab)
					->AddTab(PlaybackQueueTabId, ETabState::OpenedTab)
					->SetForegroundTab(CompletionTabId)
				)
			)
		);
}

FCharacterProfileAssetEditorToolkit::~FCharacterProfileAssetEditorToolkit()
{
	CancelDirectionalWheel(/*bDismissMenu=*/true, /*bRestoreFocus=*/false);
	DirectionButtons.Reset();
	DirectionAssignButtons.Reset();
	if (const TSharedPtr<SExpectedTagsPanel> Panel = ExpectedTagsPanel.Pin())
	{
		Panel->Shutdown();
	}
	ExpectedTagsPanel.Reset();
	// Child managers and their floating tabs must die before the shared model/providers they reference.
	if (const TSharedPtr<SProfileToolPanelHost> Host = ContextPanelHost.Pin())
	{
		Host->Shutdown();
	}
	if (const TSharedPtr<SCharacterCompletionPanel> Panel = CompletionPanel.Pin())
	{
		Panel->Shutdown();
	}
	ContextPanelHost.Reset();
	CompletionPanel.Reset();
	ToolPanelProviders.Reset();
	AnimationPickerSource.Reset();
	if (EditorModel.IsValid())
	{
		// The floating Frame Data window holds a strong ref to the model — close it with the editor
		// or it would outlive us and keep the model alive.
		EditorModel->CloseFrameDataWindow();
	}
	if (FCharacterProfileEditorModel::GActiveEditorModel.Pin() == EditorModel)
	{
		FCharacterProfileEditorModel::GActiveEditorModel.Reset();
	}
	if (GActiveCharacterProfileToolkit.Pin().Get() == this)
	{
		GActiveCharacterProfileToolkit.Reset();
	}
	EditorModel.Reset();
}

void FCharacterProfileAssetEditorToolkit::InitEditor(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost, UPaper2DPlusCharacterProfileAsset* InAsset)
{
	EditedAsset = InAsset;

	EditorModel = MakeShared<FCharacterProfileEditorModel>();
	EditorModel->InitializeFromAsset(InAsset);
	// Character Profiles opt into the shared visual-only direction projection. Layer hosts use the
	// same model class but deliberately keep its default-off, base-only behavior.
	EnableDirectionalPreviewForCharacterProfileHost(EditorModel.ToSharedRef());
	FCharacterProfileEditorModel::GActiveEditorModel = EditorModel;
	AnimationPickerSource = MakeShared<FAnimationProfilePickerSource>(EditorModel);
	ActiveToolId = AnimationsTabId;

	const TSharedRef<FTabManager::FLayout> Layout = CreateDefaultWorkspaceLayout();

	const bool bCreateDefaultStandaloneMenu = true;
	const bool bCreateDefaultToolbar = true;

	// Decided BEFORE InitAssetEditor, because InitAssetEditor restores the layout and therefore spawns the
	// main tool tabs — WrapMainToolContent reads this flag while building each shared header. Resolving it
	// afterwards would give every initially restored tab the world-centric fallback menu.
	// FAssetEditorToolkit::AddMenuExtender forwards to the standalone host, which a world-centric editor
	// does not have, so its Asset-menu section would be silently dropped there.
	bStandaloneAssetMenuAvailable = (Mode != EToolkitMode::WorldCentric);

	Paper2DPlusProfileEditorToolbar::Install(
		GetToolMenuToolbarName(),
		GetToolkitCommands());
	GetToolkitCommands()->MapAction(
		FPaper2DPlusDirectionalAnimationCommands::Get().OpenDirectionWheel,
		FExecuteAction::CreateSP(
			this,
			&FCharacterProfileAssetEditorToolkit::OpenDirectionalWheelFromShortcut),
		FCanExecuteAction::CreateSP(
			this,
			&FCharacterProfileAssetEditorToolkit::CanOpenDirectionalWheel));

	FAssetEditorToolkit::InitAssetEditor(
		Mode,
		InitToolkitHost,
		TEXT("CharacterProfileAssetEditorApp"),
		Layout,
		bCreateDefaultStandaloneMenu,
		bCreateDefaultToolbar,
		InAsset
	);

	EditorModel->SetTabManager(GetTabManager());
	// Lets any panel reveal the queue after appending to it without knowing this toolkit's tab ids.
	EditorModel->SetPlaybackQueueTabId(PlaybackQueueTabId);
	GActiveCharacterProfileToolkit = StaticCastSharedRef<FCharacterProfileAssetEditorToolkit>(AsShared());

	if (bStandaloneAssetMenuAvailable)
	{
		// Registration must run AFTER InitAssetEditor: the standalone host (and therefore its menu-extender
		// list) does not exist until then, and the already-generated menus need one regeneration to pick it
		// up. World-centric hosts get the same commands from the shared header's Profile Actions fallback.
		RegisterAssetMenuExtender();
	}
}

void FCharacterProfileAssetEditorToolkit::RegisterAssetMenuExtender()
{
	if (AssetMenuExtender.IsValid())
	{
		return;
	}
	AssetMenuExtender = MakeShared<FExtender>();
	AssetMenuExtender->AddMenuExtension(
		"AssetEditorActions",
		EExtensionHook::After,
		GetToolkitCommands(),
		FMenuExtensionDelegate::CreateSP(
			this,
			&FCharacterProfileAssetEditorToolkit::FillCharacterProfileMenuSection));
	AddMenuExtender(AssetMenuExtender);
	RegenerateMenusAndToolbars();
}

bool FCharacterProfileAssetEditorToolkit::HasEditedProfileAsset() const
{
	return IsValid(EditedAsset);
}

void FCharacterProfileAssetEditorToolkit::FillCharacterProfileMenuSection(FMenuBuilder& MenuBuilder)
{
	MenuBuilder.BeginSection(
		FName(TEXT("Paper2DPlusCharacterProfile")),
		LOCTEXT("CharacterProfileMenuSection", "Character Profile"));

	MenuBuilder.AddMenuEntry(
		LOCTEXT("ProfileToolsMenu", "Profile Tools…"),
		LOCTEXT("ProfileToolsMenuTip", "Open the Profile Tools window: Sprite Bounds, Relative Transform, validation, and optional PaperZD sequences."),
		FSlateIcon(FAppStyle::Get().GetStyleSetName(), "LevelEditor.Tabs.Details"),
		FUIAction(
			FExecuteAction::CreateSP(this, &FCharacterProfileAssetEditorToolkit::OpenProfileTools),
			FCanExecuteAction::CreateSP(this, &FCharacterProfileAssetEditorToolkit::HasEditedProfileAsset)));

	MenuBuilder.AddMenuEntry(
		LOCTEXT("ValidateProfileMenu", "Validate Character Profile…"),
		LOCTEXT("ValidateProfileMenuTip", "Check the whole Character Profile in the Profile Tools window's Validation tool, which hosts the shared read-only panel. Validation never changes the asset."),
		FSlateIcon(FAppStyle::Get().GetStyleSetName(), "Icons.Check"),
		FUIAction(
			FExecuteAction::CreateSP(this, &FCharacterProfileAssetEditorToolkit::OpenValidation),
			FCanExecuteAction::CreateSP(this, &FCharacterProfileAssetEditorToolkit::HasEditedProfileAsset)));

	MenuBuilder.AddMenuEntry(
		LOCTEXT("ImportProfileJsonMenu", "Import JSON…"),
		LOCTEXT("ImportProfileJsonMenuTip", "Preview and apply gameplay/profile JSON. Named Cue track organization is not imported; imported Cues use Default."),
		FSlateIcon(FAppStyle::Get().GetStyleSetName(), "Icons.Import"),
		FUIAction(
			FExecuteAction::CreateSP(this, &FCharacterProfileAssetEditorToolkit::ImportProfileJson),
			FCanExecuteAction::CreateSP(this, &FCharacterProfileAssetEditorToolkit::HasEditedProfileAsset)));

	MenuBuilder.AddMenuEntry(
		LOCTEXT("ExportProfileJsonMenu", "Export JSON…"),
		Paper2DPlusCharacterProfileJsonInteraction::GetExportLayoutDisclosure(),
		FSlateIcon(FAppStyle::Get().GetStyleSetName(), "Icons.Save"),
		FUIAction(
			FExecuteAction::CreateSP(this, &FCharacterProfileAssetEditorToolkit::ExportProfileJson),
			FCanExecuteAction::CreateSP(this, &FCharacterProfileAssetEditorToolkit::HasEditedProfileAsset)));

	MenuBuilder.EndSection();
}

TSharedRef<SWidget> FCharacterProfileAssetEditorToolkit::BuildProfileActionsMenu()
{
	FMenuBuilder MenuBuilder(/*bInShouldCloseWindowAfterMenuSelection=*/true, GetToolkitCommands());
	FillCharacterProfileMenuSection(MenuBuilder);
	return MenuBuilder.MakeWidget();
}

void FCharacterProfileAssetEditorToolkit::ExportProfileJson()
{
	if (IsValid(EditedAsset))
	{
		Paper2DPlusCharacterProfileJsonInteraction::RunInteractiveExport(*EditedAsset);
	}
}

void FCharacterProfileAssetEditorToolkit::ImportProfileJson()
{
	if (IsValid(EditedAsset))
	{
		Paper2DPlusCharacterProfileJsonInteraction::RunInteractiveImport(*EditedAsset);
	}
}

void FCharacterProfileAssetEditorToolkit::RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	WorkspaceMenuCategory = InTabManager->AddLocalWorkspaceMenuCategory(LOCTEXT("WorkspaceMenu_CharacterProfileEditor", "Character Profile Editor"));

	FAssetEditorToolkit::RegisterTabSpawners(InTabManager);

	// Tab icons via the centralized Paper2DPlusEditorIcons registry (audit) — one distinct, shared glyph per
	// concept (was: Sprite duplicated Hitbox; Timing/Events/RootMotion shared one icon). The Character Layer
	// editor's mirrored tabs use the SAME constants so the two editors no longer drift.
	InTabManager->RegisterTabSpawner(AnimationsTabId, FOnSpawnTab::CreateSP(this, &FCharacterProfileAssetEditorToolkit::SpawnTab_Animations))
		.SetDisplayName(LOCTEXT("AnimationsTab", "Animations"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef())
		.SetIcon(FSlateIcon(Paper2DPlusEditorIcons::StyleSet, Paper2DPlusEditorIcons::TabAnimations));

	InTabManager->RegisterTabSpawner(FlipbookListTabId, FOnSpawnTab::CreateSP(this, &FCharacterProfileAssetEditorToolkit::SpawnTab_FlipbookList))
		.SetDisplayName(LOCTEXT("NavigatorTab", "Navigator"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef())
		.SetIcon(FSlateIcon(FAppStyle::Get().GetStyleSetName(), Paper2DPlusEditorIcons::TabFlipbooks));

	InTabManager->RegisterTabSpawner(HitboxEditorTabId, FOnSpawnTab::CreateSP(this, &FCharacterProfileAssetEditorToolkit::SpawnTab_HitboxEditor))
		.SetDisplayName(LOCTEXT("HitboxEditorTab", "Hitbox Editor"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef())
		.SetIcon(FSlateIcon(Paper2DPlusEditorIcons::StyleSet, Paper2DPlusEditorIcons::TabHitbox));

	InTabManager->RegisterTabSpawner(SpriteEditorTabId, FOnSpawnTab::CreateSP(this, &FCharacterProfileAssetEditorToolkit::SpawnTab_SpriteEditor))
		.SetDisplayName(LOCTEXT("SpriteEditorTab", "Sprite Editor"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef())
		.SetIcon(FSlateIcon(Paper2DPlusEditorIcons::StyleSet, Paper2DPlusEditorIcons::TabSprite));

	InTabManager->RegisterTabSpawner(FrameTimingTabId, FOnSpawnTab::CreateSP(this, &FCharacterProfileAssetEditorToolkit::SpawnTab_FrameTiming))
		.SetDisplayName(LOCTEXT("FrameTimingTab", "Frame Timing"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef())
		.SetIcon(FSlateIcon(Paper2DPlusEditorIcons::StyleSet, Paper2DPlusEditorIcons::TabFrameTiming)); // custom clock

	InTabManager->RegisterTabSpawner(FrameEventsTabId, FOnSpawnTab::CreateSP(this, &FCharacterProfileAssetEditorToolkit::SpawnTab_FrameEvents))
		.SetDisplayName(LOCTEXT("FrameCuesTab", "Frame Cues"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef())
		.SetIcon(FSlateIcon(FAppStyle::Get().GetStyleSetName(), Paper2DPlusEditorIcons::TabFrameEvents));

	InTabManager->RegisterTabSpawner(RootMotionTabId, FOnSpawnTab::CreateSP(this, &FCharacterProfileAssetEditorToolkit::SpawnTab_RootMotion))
		.SetDisplayName(LOCTEXT("RootMotionTab", "Root Motion"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef())
		.SetIcon(FSlateIcon(FAppStyle::Get().GetStyleSetName(), Paper2DPlusEditorIcons::TabRootMotion));

	InTabManager->RegisterTabSpawner(ContextHostTabId, FOnSpawnTab::CreateSP(this, &FCharacterProfileAssetEditorToolkit::SpawnTab_ContextHost))
		.SetDisplayName(LOCTEXT("ContextHostTab", "Details"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef())
		.SetIcon(FSlateIcon(FAppStyle::Get().GetStyleSetName(), "Icons.Layout"));

	InTabManager->RegisterTabSpawner(ExpectedTagsTabId, FOnSpawnTab::CreateSP(this, &FCharacterProfileAssetEditorToolkit::SpawnTab_ExpectedTags))
		.SetDisplayName(LOCTEXT("ExpectedTagsTab", "Expected Tags"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef())
		.SetIcon(FSlateIcon(FAppStyle::Get().GetStyleSetName(), "Icons.Check"));

	InTabManager->RegisterTabSpawner(CompletionTabId, FOnSpawnTab::CreateSP(this, &FCharacterProfileAssetEditorToolkit::SpawnTab_Completion))
		.SetDisplayName(LOCTEXT("CompletionTab", "Completion"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef())
		.SetIcon(FSlateIcon(FAppStyle::Get().GetStyleSetName(), "Icons.Check"));

	InTabManager->RegisterTabSpawner(RelatedProfilesTabId, FOnSpawnTab::CreateSP(this, &FCharacterProfileAssetEditorToolkit::SpawnTab_RelatedProfiles))
		.SetDisplayName(LOCTEXT("RelatedProfilesTab", "Related Profiles"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef())
		.SetIcon(FSlateIcon(FAppStyle::Get().GetStyleSetName(), "Icons.Link"));

	InTabManager->RegisterTabSpawner(PlaybackQueueTabId, FOnSpawnTab::CreateSP(this, &FCharacterProfileAssetEditorToolkit::SpawnTab_PlaybackQueue))
		.SetDisplayName(LOCTEXT("PlaybackQueueTab", "Playback Queue"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef())
		.SetIcon(FSlateIcon(FAppStyle::Get().GetStyleSetName(), "Icons.Play"));
}

void FCharacterProfileAssetEditorToolkit::UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	FAssetEditorToolkit::UnregisterTabSpawners(InTabManager);
	InTabManager->UnregisterTabSpawner(AnimationsTabId);
	InTabManager->UnregisterTabSpawner(FlipbookListTabId);
	InTabManager->UnregisterTabSpawner(HitboxEditorTabId);
	InTabManager->UnregisterTabSpawner(SpriteEditorTabId);
	InTabManager->UnregisterTabSpawner(FrameTimingTabId);
	InTabManager->UnregisterTabSpawner(FrameEventsTabId);
	InTabManager->UnregisterTabSpawner(RootMotionTabId);
	InTabManager->UnregisterTabSpawner(ContextHostTabId);
	InTabManager->UnregisterTabSpawner(ExpectedTagsTabId);
	InTabManager->UnregisterTabSpawner(CompletionTabId);
	InTabManager->UnregisterTabSpawner(RelatedProfilesTabId);
	InTabManager->UnregisterTabSpawner(PlaybackQueueTabId);
}

TSharedRef<SDockTab> FCharacterProfileAssetEditorToolkit::SpawnTab_Animations(const FSpawnTabArgs& Args)
{
	if (!FindToolPanelProvider(AnimationsTabId).IsValid())
	{
		const TSharedPtr<IProfileToolPanelProvider> Provider =
			MakeShared<FAnimationsContextPanelProvider>(EditorModel);
		RegisterToolPanelProvider(AnimationsTabId, Provider);
	}
	const TSharedRef<SAnimationsPanel> AnimationsPanel = SNew(SAnimationsPanel)
		.Model(EditorModel)
		.OnContextPanelRequested(SAnimationsPanel::FOnContextPanelRequested::CreateSP(
			this,
			&FCharacterProfileAssetEditorToolkit::HandleAnimationsContextPanelRequested));

	// List/Grid/Map remain the full central canvas. Focused Details/Transitions/Tags are supplied to
	// the workspace's contextual Details host by the provider registered above.
	//
	// Animations is the ONLY tool that contributes a header action: its compact View combo, built fresh
	// for this tab host (the panel is recreated per spawn, so the control is never reparented).
	return MakeMainToolTab(
		LOCTEXT("AnimationsTabLabel", "Animations"),
		AnimationsTabId,
		AnimationsPanel,
		AnimationsPanel->MakeHeaderViewControl());
}

TSharedRef<SDockTab> FCharacterProfileAssetEditorToolkit::SpawnTab_FlipbookList(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.Label(LOCTEXT("NavigatorTabLabel", "Navigator"))
		[
			SNew(SFlipbookListPanel)
			.Model(EditorModel)
		];
}

TSharedRef<SDockTab> FCharacterProfileAssetEditorToolkit::SpawnTab_HitboxEditor(const FSpawnTabArgs& Args)
{
	TSharedPtr<SHitboxEditorPanel> Panel;
	if (const TSharedPtr<IProfileToolPanelProvider> ExistingProvider =
		FindToolPanelProvider(HitboxEditorTabId))
	{
		// The provider IS the controller/widget. Reusing it on tab reopen preserves one transaction
		// owner and avoids duplicating model subscriptions or attaching contextual panels to a clone.
		Panel = StaticCastSharedPtr<SHitboxEditorPanel>(ExistingProvider);
	}
	if (!Panel.IsValid())
	{
		Panel = SNew(SHitboxEditorPanel)
			.Model(EditorModel)
			.HostContract(FProfileToolPanelHostContract::External());
		RegisterToolPanelProvider(
			HitboxEditorTabId,
			StaticCastSharedPtr<IProfileToolPanelProvider>(Panel));
	}
	return MakeMainToolTab(
		LOCTEXT("HitboxEditorTabLabel", "Hitbox Editor"),
		HitboxEditorTabId,
		Panel.ToSharedRef());
}

TSharedRef<SDockTab> FCharacterProfileAssetEditorToolkit::SpawnTab_SpriteEditor(const FSpawnTabArgs& Args)
{
	TSharedPtr<SSpriteEditorPanel> Panel;
	if (const TSharedPtr<IProfileToolPanelProvider> ExistingProvider =
		FindToolPanelProvider(SpriteEditorTabId))
	{
		Panel = StaticCastSharedPtr<SSpriteEditorPanel>(ExistingProvider);
	}
	if (!Panel.IsValid())
	{
		Panel = SNew(SSpriteEditorPanel)
			.Model(EditorModel)
			.HostContract(FProfileToolPanelHostContract::External());
		RegisterToolPanelProvider(
			SpriteEditorTabId,
			StaticCastSharedPtr<IProfileToolPanelProvider>(Panel));
	}
	return MakeMainToolTab(
		LOCTEXT("SpriteEditorTabLabel", "Sprite Editor"),
		SpriteEditorTabId,
		Panel.ToSharedRef());
}

TSharedRef<SDockTab> FCharacterProfileAssetEditorToolkit::SpawnTab_FrameTiming(const FSpawnTabArgs& Args)
{
	TSharedPtr<SFrameTimingEditor> Panel;
	if (const TSharedPtr<IProfileToolPanelProvider> ExistingProvider =
		FindToolPanelProvider(FrameTimingTabId))
	{
		Panel = StaticCastSharedPtr<SFrameTimingEditor>(ExistingProvider);
	}
	if (!Panel.IsValid())
	{
		Panel = SNew(SFrameTimingEditor)
			.Asset(EditedAsset)
			.Model(EditorModel)
			.HostContract(FProfileToolPanelHostContract::External());
		RegisterToolPanelProvider(
			FrameTimingTabId,
			StaticCastSharedPtr<IProfileToolPanelProvider>(Panel));
	}
	return MakeMainToolTab(
		LOCTEXT("FrameTimingTabLabel", "Frame Timing"),
		FrameTimingTabId,
		Panel.ToSharedRef());
}

TSharedRef<SDockTab> FCharacterProfileAssetEditorToolkit::SpawnTab_FrameEvents(const FSpawnTabArgs& Args)
{
	TSharedPtr<SFrameEventEditor> Panel;
	if (const TSharedPtr<IProfileToolPanelProvider> ExistingProvider =
		FindToolPanelProvider(FrameEventsTabId))
	{
		Panel = StaticCastSharedPtr<SFrameEventEditor>(ExistingProvider);
	}
	if (!Panel.IsValid())
	{
		Panel = SNew(SFrameEventEditor)
			.Model(EditorModel)
			.HostContract(FProfileToolPanelHostContract::External());
		RegisterToolPanelProvider(
			FrameEventsTabId,
			StaticCastSharedPtr<IProfileToolPanelProvider>(Panel));
	}
	return MakeMainToolTab(
		LOCTEXT("FrameCuesTabLabel", "Frame Cues"),
		FrameEventsTabId,
		Panel.ToSharedRef());
}

TSharedRef<SDockTab> FCharacterProfileAssetEditorToolkit::SpawnTab_RootMotion(const FSpawnTabArgs& Args)
{
	TSharedPtr<SRootMotionEditor> Panel;
	if (const TSharedPtr<IProfileToolPanelProvider> ExistingProvider =
		FindToolPanelProvider(RootMotionTabId))
	{
		Panel = StaticCastSharedPtr<SRootMotionEditor>(ExistingProvider);
	}
	if (!Panel.IsValid())
	{
		Panel = SNew(SRootMotionEditor)
			.Asset(EditedAsset)
			.Model(EditorModel)
			.HostContract(FProfileToolPanelHostContract::External());
		RegisterToolPanelProvider(
			RootMotionTabId,
			StaticCastSharedPtr<IProfileToolPanelProvider>(Panel));
	}
	return MakeMainToolTab(
		LOCTEXT("RootMotionTabLabel", "Root Motion"),
		RootMotionTabId,
		Panel.ToSharedRef());
}

TSharedRef<SDockTab> FCharacterProfileAssetEditorToolkit::SpawnTab_ContextHost(const FSpawnTabArgs& Args)
{
	const TSharedRef<SDockTab> DockTab = SNew(SDockTab)
		.Label(LOCTEXT("ContextHostTabLabel", "Details"));
	const TSharedRef<SProfileToolPanelHost> Host = SNew(SProfileToolPanelHost)
		.OwnerTab(DockTab)
		.LayoutScope(TEXT("Character"));
	ContextPanelHost = Host;
	DockTab->SetContent(WrapDirectionalShortcutScope(Host));
	Host->RequestActiveTool(ActiveToolId, FindToolPanelProvider(ActiveToolId));
	return DockTab;
}

TSharedRef<SDockTab> FCharacterProfileAssetEditorToolkit::SpawnTab_ExpectedTags(
	const FSpawnTabArgs& Args)
{
	const TSharedRef<SExpectedTagsPanel> Panel = SNew(SExpectedTagsPanel)
		.Model(EditorModel);
	ExpectedTagsPanel = Panel;
	return SNew(SDockTab)
		.Label(LOCTEXT("ExpectedTagsTabLabel", "Expected Tags"))
		[
			WrapDirectionalShortcutScope(Panel)
		];
}

TSharedRef<SDockTab> FCharacterProfileAssetEditorToolkit::SpawnTab_Completion(const FSpawnTabArgs& Args)
{
	const TSharedRef<SCharacterCompletionPanel> Panel = SNew(SCharacterCompletionPanel)
		.Model(EditorModel);
	CompletionPanel = Panel;
	return SNew(SDockTab)
		.Label(LOCTEXT("CompletionTabLabel", "Completion"))
		[
			WrapDirectionalShortcutScope(Panel)
		];
}

TSharedRef<SDockTab> FCharacterProfileAssetEditorToolkit::SpawnTab_RelatedProfiles(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.Label(LOCTEXT("RelatedProfilesTabLabel", "Related Profiles"))
		[
			WrapDirectionalShortcutScope(
				SNew(SRelatedProfileBar).EditedAsset(EditedAsset))
		];
}

TSharedRef<SDockTab> FCharacterProfileAssetEditorToolkit::SpawnTab_PlaybackQueue(const FSpawnTabArgs& Args)
{
	const TSharedRef<SPlaybackQueuePanel> Panel = SNew(SPlaybackQueuePanel)
		.Model(EditorModel);
	PlaybackQueuePanel = Panel;
	return SNew(SDockTab)
		.Label(LOCTEXT("PlaybackQueueTabLabel", "Playback Queue"))
		[
			WrapDirectionalShortcutScope(Panel)
		];
}

TSharedRef<SDockTab> FCharacterProfileAssetEditorToolkit::MakeMainToolTab(
	const FText& Label,
	FName ToolId,
	TSharedRef<SWidget> ToolContent,
	TSharedPtr<SWidget> HeaderActions)
{
	const TSharedRef<SDockTab> DockTab = SNew(SDockTab)
		.Label(Label);
	DockTab->SetContent(WrapMainToolContent(ToolId, ToolContent, HeaderActions));
	DockTab->SetOnTabActivated(SDockTab::FOnTabActivatedCallback::CreateSP(
		this,
		&FCharacterProfileAssetEditorToolkit::HandleMainToolActivated,
		ToolId));
	return DockTab;
}

TSharedRef<SWidget> FCharacterProfileAssetEditorToolkit::WrapMainToolContent(
	FName ToolId,
	TSharedRef<SWidget> ToolContent,
	TSharedPtr<SWidget> HeaderActions)
{
	// Built imperatively so a tool that supplies no header action contributes no slot at all — an
	// always-present empty spacer would be indistinguishable from a missing control at narrow widths.
	const TSharedRef<SHorizontalBox> HeaderRow = SNew(SHorizontalBox);
	const TWeakPtr<FCharacterProfileEditorModel> WeakModel = EditorModel;
	HeaderRow->AddSlot()
		.FillWidth(1.0f)
		[
			SNew(SAnimationProfileSwitcher)
			.Source(AnimationPickerSource)
			.PreviewFlipbook_Lambda([WeakModel]() -> UPaperFlipbook*
			{
				const TSharedPtr<FCharacterProfileEditorModel> Model = WeakModel.Pin();
				if (!Model.IsValid())
				{
					return nullptr;
				}
				const FCharacterProfileDirectionalPreview& Preview =
					Model->GetDirectionalPreview();
				return ResolveDirectionalHeaderPreviewFlipbook(Preview);
			})
			.EmptySelectionText(LOCTEXT("NoCurrentAnimation", "Select an animation"))
		];
	if (SupportsDirectionalHeader(ToolId))
	{
		HeaderRow->AddSlot()
			.FillWidth(0.8f)
			.VAlign(VAlign_Center)
			.Padding(6.0f, 0.0f, 0.0f, 0.0f)
			[
				// Build a fresh control tree for every tab host; Slate widgets cannot have two parents.
				BuildDirectionalHeaderControl(ToolId)
			];
	}
	if (HeaderActions.IsValid())
	{
		HeaderRow->AddSlot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(6.0f, 0.0f, 0.0f, 0.0f)
			[
				HeaderActions.ToSharedRef()
			];
	}
	if (!bStandaloneAssetMenuAvailable)
	{
		// World-centric hosts have no Asset menu, so the same three whole-profile commands appear here
		// instead — one compact menu, never a row of persistent buttons.
		HeaderRow->AddSlot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(6.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SComboButton)
				.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
				.ContentPadding(FMargin(8.0f, 2.0f))
				.ToolTipText(LOCTEXT("ProfileActionsTip", "Whole-profile actions: validate, import JSON, and export JSON."))
				.AccessibleText(LOCTEXT("ProfileActionsAccessible", "Character Profile actions"))
				.OnGetMenuContent(this, &FCharacterProfileAssetEditorToolkit::BuildProfileActionsMenu)
				.ButtonContent()
				[
					SNew(STextBlock)
					.Text(LOCTEXT("ProfileActions", "Profile Actions"))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
				]
			];
	}

	const TSharedRef<SVerticalBox> Workspace = SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
			.Padding(FMargin(6.0f, 4.0f))
			[
				HeaderRow
			]
		]
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			ToolContent
		];
	return WrapDirectionalShortcutScope(Workspace);
}

void FCharacterProfileAssetEditorToolkit::OpenProfileTools()
{
	SProfileToolsWindow::OpenProfileTools(EditedAsset);
}

void FCharacterProfileAssetEditorToolkit::OpenValidation()
{
	// Character Profile validation lives in the Profile Tools window's Validation tool, which hosts
	// the SAME shared SProfileValidationPanel. This editor deliberately no longer opens a standalone
	// modeless window: the panel would then be constructible in two places at once for one profile.
	// Layer, Effect, and Combat are unchanged -- they have no tools window to host anything, so they
	// keep their standalone modeless window.
	SProfileToolsWindow::OpenProfileTools(EditedAsset, SProfileToolsWindow::ToolId_Validation);
}

void FCharacterProfileAssetEditorToolkit::HandleMainToolActivated(
	TSharedRef<SDockTab> ActivatedTab,
	ETabActivationCause Cause,
	FName ToolId)
{
	if (ToolId != ActiveToolId)
	{
		CancelDirectionalWheel(/*bDismissMenu=*/true, /*bRestoreFocus=*/false);
	}
	if (ActiveToolId == HitboxEditorTabId && ToolId != HitboxEditorTabId)
	{
		if (const TSharedPtr<IProfileToolPanelProvider> Provider =
			FindToolPanelProvider(HitboxEditorTabId))
		{
			StaticCastSharedPtr<SHitboxEditorPanel>(Provider)->HandleHostDeactivated();
		}
	}
	if (ActiveToolId == SpriteEditorTabId && ToolId != SpriteEditorTabId)
	{
		if (const TSharedPtr<IProfileToolPanelProvider> Provider =
			FindToolPanelProvider(SpriteEditorTabId))
		{
			StaticCastSharedPtr<SSpriteEditorPanel>(Provider)->HandleHostDeactivated();
		}
	}
	if (ActiveToolId == FrameTimingTabId && ToolId != FrameTimingTabId)
	{
		if (const TSharedPtr<IProfileToolPanelProvider> Provider =
			FindToolPanelProvider(FrameTimingTabId))
		{
			StaticCastSharedPtr<SFrameTimingEditor>(Provider)->HandleHostDeactivated();
		}
	}
	if (ActiveToolId == FrameEventsTabId && ToolId != FrameEventsTabId)
	{
		if (const TSharedPtr<IProfileToolPanelProvider> Provider =
			FindToolPanelProvider(FrameEventsTabId))
		{
			StaticCastSharedPtr<SFrameEventEditor>(Provider)->HandleHostDeactivated();
		}
	}
	if (ActiveToolId == RootMotionTabId && ToolId != RootMotionTabId)
	{
		if (const TSharedPtr<IProfileToolPanelProvider> Provider =
			FindToolPanelProvider(RootMotionTabId))
		{
			StaticCastSharedPtr<SRootMotionEditor>(Provider)->HandleHostDeactivated();
		}
	}
	if (ToolId == FrameTimingTabId)
	{
		if (const TSharedPtr<IProfileToolPanelProvider> Provider =
			FindToolPanelProvider(FrameTimingTabId))
		{
			StaticCastSharedPtr<SFrameTimingEditor>(Provider)->HandleHostActivated();
		}
	}
	if (ToolId == FrameEventsTabId)
	{
		if (const TSharedPtr<IProfileToolPanelProvider> Provider =
			FindToolPanelProvider(FrameEventsTabId))
		{
			StaticCastSharedPtr<SFrameEventEditor>(Provider)->HandleHostActivated();
		}
	}
	if (ToolId == RootMotionTabId)
	{
		if (const TSharedPtr<IProfileToolPanelProvider> Provider =
			FindToolPanelProvider(RootMotionTabId))
		{
			StaticCastSharedPtr<SRootMotionEditor>(Provider)->HandleHostActivated();
		}
	}
	ActiveToolId = ToolId;
	FCharacterProfileEditorModel::GActiveEditorModel = EditorModel;
	GActiveCharacterProfileToolkit = StaticCastSharedRef<FCharacterProfileAssetEditorToolkit>(AsShared());
	if (const TSharedPtr<SProfileToolPanelHost> Host = ContextPanelHost.Pin())
	{
		Host->RequestActiveTool(ToolId, FindToolPanelProvider(ToolId));
	}
}

void FCharacterProfileAssetEditorToolkit::HandleAnimationsContextPanelRequested(FName PanelId)
{
	if (ActiveToolId != AnimationsTabId)
	{
		return;
	}

	// The contextual stack is normally open in the canonical workspace. If a designer closed the outer
	// host, selecting a Map edge is explicit transition-authoring intent and may restore it.
	if (!ContextPanelHost.IsValid())
	{
		if (const TSharedPtr<FTabManager> Manager = GetTabManager())
		{
			Manager->TryInvokeTab(ContextHostTabId);
		}
	}
	if (const TSharedPtr<SProfileToolPanelHost> Host = ContextPanelHost.Pin())
	{
		Host->ForegroundPanel(PanelId);
	}
}

TSharedPtr<IProfileToolPanelProvider> FCharacterProfileAssetEditorToolkit::FindToolPanelProvider(FName ToolId) const
{
	const TSharedPtr<IProfileToolPanelProvider>* Provider = ToolPanelProviders.Find(ToolId);
	return Provider ? *Provider : nullptr;
}

void FCharacterProfileAssetEditorToolkit::RegisterToolPanelProvider(
	FName ToolId,
	TSharedPtr<IProfileToolPanelProvider> Provider)
{
	if (ToolId.IsNone() || !Provider.IsValid())
	{
		return;
	}
	ToolPanelProviders.Add(ToolId, Provider);
	if (ToolId == ActiveToolId)
	{
		if (const TSharedPtr<SProfileToolPanelHost> Host = ContextPanelHost.Pin())
		{
			Host->RequestActiveTool(ToolId, Provider);
		}
	}
}

void FCharacterProfileAssetEditorToolkit::UnregisterToolPanelProvider(
	FName ToolId,
	const TSharedPtr<IProfileToolPanelProvider>& Provider)
{
	const TSharedPtr<IProfileToolPanelProvider>* Registered = ToolPanelProviders.Find(ToolId);
	if (!Registered || *Registered != Provider)
	{
		return;
	}
	ToolPanelProviders.Remove(ToolId);
	if (ToolId == ActiveToolId)
	{
		if (const TSharedPtr<SProfileToolPanelHost> Host = ContextPanelHost.Pin())
		{
			Host->RequestActiveTool(ToolId, nullptr);
		}
	}
}

FString FCharacterProfileAssetEditorToolkit::BuildWorkspaceProbeString()
{
	const TSharedPtr<FTabManager> Manager = GetTabManager();
	const bool bNavigatorOpen = Manager.IsValid()
		&& Manager->FindExistingLiveTab(FlipbookListTabId).IsValid();
	const bool bCompletionOpen = Manager.IsValid()
		&& Manager->FindExistingLiveTab(CompletionTabId).IsValid();
	const bool bRelatedProfilesOpen = Manager.IsValid()
		&& Manager->FindExistingLiveTab(RelatedProfilesTabId).IsValid();
	const bool bContextHostOpen = Manager.IsValid()
		&& Manager->FindExistingLiveTab(ContextHostTabId).IsValid();
	const bool bExpectedTagsOpen = Manager.IsValid()
		&& Manager->FindExistingLiveTab(ExpectedTagsTabId).IsValid();
	const TSharedPtr<SProfileToolPanelHost> Host = ContextPanelHost.Pin();
	const FString ContextDiagnostic = Host.IsValid()
		? Host->BuildDiagnosticString()
		: TEXT("Tool=None StateSection=None State=HostClosed DetailsSections=[]");
	return FString::Printf(
		TEXT("ActiveTool=%s Flipbook=%d Frame=%d Navigator=%s Completion=%s RelatedProfiles=%s ContextHost=%s ExpectedTags=%s %s"),
		*ActiveToolId.ToString(),
		EditorModel.IsValid() ? EditorModel->GetSelectedFlipbookIndex() : INDEX_NONE,
		EditorModel.IsValid() ? EditorModel->GetSelectedFrameIndex() : INDEX_NONE,
		bNavigatorOpen ? TEXT("Open") : TEXT("Closed"),
		bCompletionOpen ? TEXT("Open") : TEXT("Closed"),
		bRelatedProfilesOpen ? TEXT("Open") : TEXT("Closed"),
		bContextHostOpen ? TEXT("Open") : TEXT("Closed"),
		bExpectedTagsOpen ? TEXT("Open") : TEXT("Closed"),
		*ContextDiagnostic);
}

void FCharacterProfileAssetEditorToolkit::HandleValidationIssueActivated(
	const FPaper2DPlusValidationIssue& Issue)
{
	if (Issue.ToolTarget.IsSet() && !Issue.ToolTarget->TabId.IsNone())
	{
		if (const TSharedPtr<FTabManager> Manager = GetTabManager())
		{
			Manager->TryInvokeTab(Issue.ToolTarget->TabId);
		}
	}
}

FName FCharacterProfileAssetEditorToolkit::GetToolkitFName() const
{
	return FName("CharacterProfileAssetEditor");
}

FText FCharacterProfileAssetEditorToolkit::GetBaseToolkitName() const
{
	return LOCTEXT("ToolkitName", "Paper2D+ Character Profile Editor");
}

FString FCharacterProfileAssetEditorToolkit::GetWorldCentricTabPrefix() const
{
	return LOCTEXT("WorldCentricTabPrefix", "CharacterProfile ").ToString();
}

FLinearColor FCharacterProfileAssetEditorToolkit::GetWorldCentricTabColorScale() const
{
	return FLinearColor(0.3f, 0.2f, 0.5f, 1.0f);
}

#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 3)
bool FCharacterProfileAssetEditorToolkit::OnRequestClose(EAssetEditorCloseReason InCloseReason)
#else
bool FCharacterProfileAssetEditorToolkit::OnRequestClose()
#endif
{
	// A close attempt is a lifecycle boundary even when the asset-save prompt later rejects it. The
	// shared radial interaction must not survive behind that prompt or resume with stale selection.
	CancelDirectionalWheel(/*bDismissMenu=*/true, /*bRestoreFocus=*/false);
	const bool bWasTimingActive = ActiveToolId == FrameTimingTabId;
	const bool bWasFrameCuesActive = ActiveToolId == FrameEventsTabId;
	const bool bWasRootMotionActive = ActiveToolId == RootMotionTabId;
	TSharedPtr<SFrameTimingEditor> TimingEditor;
	TSharedPtr<SFrameEventEditor> FrameCueEditor;
	TSharedPtr<SRootMotionEditor> RootMotionEditor;
	if (const TSharedPtr<IProfileToolPanelProvider> Provider =
		FindToolPanelProvider(SpriteEditorTabId))
	{
		StaticCastSharedPtr<SSpriteEditorPanel>(Provider)->HandleHostDeactivated();
	}
	if (const TSharedPtr<IProfileToolPanelProvider> Provider =
		FindToolPanelProvider(FrameTimingTabId))
	{
		TimingEditor = StaticCastSharedPtr<SFrameTimingEditor>(Provider);
		TimingEditor->HandleHostDeactivated();
	}
	if (const TSharedPtr<IProfileToolPanelProvider> Provider =
		FindToolPanelProvider(FrameEventsTabId))
	{
		FrameCueEditor = StaticCastSharedPtr<SFrameEventEditor>(Provider);
		FrameCueEditor->HandleHostDeactivated();
	}
	if (const TSharedPtr<IProfileToolPanelProvider> Provider =
		FindToolPanelProvider(RootMotionTabId))
	{
		RootMotionEditor = StaticCastSharedPtr<SRootMotionEditor>(Provider);
		RootMotionEditor->HandleHostDeactivated();
	}
	const auto ReactivateToolsAfterRejectedClose =
		[bWasTimingActive, bWasFrameCuesActive, bWasRootMotionActive,
		 TimingEditor, FrameCueEditor, RootMotionEditor]()
	{
		if (bWasTimingActive && TimingEditor.IsValid())
		{
			TimingEditor->HandleHostActivated();
		}
		if (bWasFrameCuesActive && FrameCueEditor.IsValid())
		{
			FrameCueEditor->HandleHostActivated();
		}
		if (bWasRootMotionActive && RootMotionEditor.IsValid())
		{
			RootMotionEditor->HandleHostActivated();
		}
	};
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 3)
	const bool bCanClose = FAssetEditorToolkit::OnRequestClose(InCloseReason);
#else
	const bool bCanClose = FAssetEditorToolkit::OnRequestClose();
#endif
	if (!bCanClose)
	{
		ReactivateToolsAfterRejectedClose();
	}
	return bCanClose;
}

#undef LOCTEXT_NAMESPACE
