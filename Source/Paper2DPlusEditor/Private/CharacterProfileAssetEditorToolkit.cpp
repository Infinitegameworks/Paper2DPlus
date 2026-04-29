// Copyright 2026 Infinite Gameworks. All Rights Reserved.

// CharacterProfileAssetEditorToolkit.cpp - Asset editor toolkit wrapper
// Split from CharacterProfileAssetEditor.cpp for maintainability

#include "CharacterProfileAssetEditor.h"
#include "Widgets/Docking/SDockTab.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWindow.h"
#include "Misc/MessageDialog.h"
// UE 5.0 compat: FAppStyle/AppStyle.h doesn't exist, use FEditorStyle
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
#include "EditorStyleSet.h"
#ifndef FAppStyle
#define FAppStyle FEditorStyle
#define GetAppStyleSetName GetStyleSetName
#endif
#else
#include "Styling/AppStyle.h"
#endif

/** FCharacterProfileAssetEditorToolkit — FAssetEditorToolkit lifecycle: single-instance editor window management. */

#define LOCTEXT_NAMESPACE "CharacterProfileAssetEditor"

// ==========================================
// FCharacterProfileAssetEditorToolkit Implementation
// ==========================================

const FName FCharacterProfileAssetEditorToolkit::CharacterProfileEditorTabId(TEXT("CharacterProfileEditorTab"));

FCharacterProfileAssetEditorToolkit::~FCharacterProfileAssetEditorToolkit()
{
}

void FCharacterProfileAssetEditorToolkit::OpenEditor(UPaper2DPlusCharacterProfileAsset* Asset)
{
	if (!Asset)
	{
		return;
	}

	TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(FText::FromString(FString::Printf(TEXT("Character Profile Editor - %s"), *Asset->DisplayName)))
		.ClientSize(FVector2D(1200, 800))
		.SupportsMinimize(true)
		.SupportsMaximize(true)
		[
			SNew(SCharacterProfileAssetEditor)
			.Asset(Asset)
		];

	FSlateApplication::Get().AddWindow(Window);
	Window->BringToFront();
}

void FCharacterProfileAssetEditorToolkit::InitEditor(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost, UPaper2DPlusCharacterProfileAsset* InAsset)
{
	EditedAsset = InAsset;

	const TSharedRef<FTabManager::FLayout> StandaloneDefaultLayout = FTabManager::NewLayout("Standalone_CharacterProfileAssetEditor_Layout_v2")
		->AddArea
		(
			FTabManager::NewPrimaryArea()
			->SetOrientation(Orient_Vertical)
			->Split
			(
				FTabManager::NewStack()
				->AddTab(CharacterProfileEditorTabId, ETabState::OpenedTab)
				->SetHideTabWell(true)
			)
		);

	const bool bCreateDefaultStandaloneMenu = true;
	const bool bCreateDefaultToolbar = false;

	FAssetEditorToolkit::InitAssetEditor(
		Mode,
		InitToolkitHost,
		TEXT("CharacterProfileAssetEditorApp"),
		StandaloneDefaultLayout,
		bCreateDefaultStandaloneMenu,
		bCreateDefaultToolbar,
		InAsset
	);

	InvokeTab(CharacterProfileEditorTabId);
}

void FCharacterProfileAssetEditorToolkit::RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	WorkspaceMenuCategory = InTabManager->AddLocalWorkspaceMenuCategory(LOCTEXT("WorkspaceMenu_CharacterProfileEditor", "Character Profile Editor"));

	FAssetEditorToolkit::RegisterTabSpawners(InTabManager);

	InTabManager->RegisterTabSpawner(CharacterProfileEditorTabId, FOnSpawnTab::CreateSP(this, &FCharacterProfileAssetEditorToolkit::SpawnEditorTab))
		.SetDisplayName(LOCTEXT("EditorTab", "Character Profile Editor"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef())
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Details"));
}

void FCharacterProfileAssetEditorToolkit::UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	FAssetEditorToolkit::UnregisterTabSpawners(InTabManager);
	InTabManager->UnregisterTabSpawner(CharacterProfileEditorTabId);
}

TSharedRef<SDockTab> FCharacterProfileAssetEditorToolkit::SpawnEditorTab(const FSpawnTabArgs& Args)
{
	TSharedRef<SCharacterProfileAssetEditor> Editor = SNew(SCharacterProfileAssetEditor)
		.Asset(EditedAsset);
	EditorWidget = Editor;

	return SNew(SDockTab)
		.Label(FText::FromString(EditedAsset ? EditedAsset->DisplayName : TEXT("Character Profile Editor")))
		[
			Editor
		];
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

#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 4)
bool FCharacterProfileAssetEditorToolkit::OnRequestClose(EAssetEditorCloseReason InCloseReason)
#else
bool FCharacterProfileAssetEditorToolkit::OnRequestClose()
#endif
{
	return true;
}

#undef LOCTEXT_NAMESPACE
