// Copyright 2026 Infinite Gameworks. All Rights Reserved.

// CharacterDataAssetEditorToolkit.cpp - Asset editor toolkit wrapper
// Split from CharacterDataAssetEditor.cpp for maintainability

#include "CharacterDataAssetEditor.h"
#include "Widgets/Docking/SDockTab.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWindow.h"

#define LOCTEXT_NAMESPACE "CharacterDataAssetEditor"

// ==========================================
// FCharacterDataAssetEditorToolkit Implementation
// ==========================================

const FName FCharacterDataAssetEditorToolkit::CharacterDataEditorTabId(TEXT("CharacterDataEditorTab"));

FCharacterDataAssetEditorToolkit::~FCharacterDataAssetEditorToolkit()
{
}

void FCharacterDataAssetEditorToolkit::OpenEditor(UPaper2DPlusCharacterDataAsset* Asset)
{
	if (!Asset)
	{
		return;
	}

	TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(FText::FromString(FString::Printf(TEXT("Character Data Editor - %s"), *Asset->DisplayName)))
		.ClientSize(FVector2D(1200, 800))
		.SupportsMinimize(true)
		.SupportsMaximize(true)
		[
			SNew(SCharacterDataAssetEditor)
			.Asset(Asset)
		];

	FSlateApplication::Get().AddWindow(Window);
	Window->BringToFront();
}

void FCharacterDataAssetEditorToolkit::InitEditor(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost, UPaper2DPlusCharacterDataAsset* InAsset)
{
	EditedAsset = InAsset;

	const TSharedRef<FTabManager::FLayout> StandaloneDefaultLayout = FTabManager::NewLayout("Standalone_CharacterDataAssetEditor_Layout_v1")
		->AddArea
		(
			FTabManager::NewPrimaryArea()
			->SetOrientation(Orient_Vertical)
			->Split
			(
				FTabManager::NewStack()
				->AddTab(CharacterDataEditorTabId, ETabState::OpenedTab)
				->SetHideTabWell(true)
			)
		);

	const bool bCreateDefaultStandaloneMenu = true;
	const bool bCreateDefaultToolbar = false;

	FAssetEditorToolkit::InitAssetEditor(
		Mode,
		InitToolkitHost,
		TEXT("CharacterDataAssetEditorApp"),
		StandaloneDefaultLayout,
		bCreateDefaultStandaloneMenu,
		bCreateDefaultToolbar,
		InAsset
	);

	InvokeTab(CharacterDataEditorTabId);
}

void FCharacterDataAssetEditorToolkit::RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	WorkspaceMenuCategory = InTabManager->AddLocalWorkspaceMenuCategory(LOCTEXT("WorkspaceMenu_CharacterDataEditor", "Character Data Editor"));

	FAssetEditorToolkit::RegisterTabSpawners(InTabManager);

	InTabManager->RegisterTabSpawner(CharacterDataEditorTabId, FOnSpawnTab::CreateSP(this, &FCharacterDataAssetEditorToolkit::SpawnEditorTab))
		.SetDisplayName(LOCTEXT("EditorTab", "Character Data Editor"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef())
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Details"));
}

void FCharacterDataAssetEditorToolkit::UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	FAssetEditorToolkit::UnregisterTabSpawners(InTabManager);
	InTabManager->UnregisterTabSpawner(CharacterDataEditorTabId);
}

TSharedRef<SDockTab> FCharacterDataAssetEditorToolkit::SpawnEditorTab(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.Label(FText::FromString(EditedAsset ? EditedAsset->DisplayName : TEXT("Character Data Editor")))
		[
			SNew(SCharacterDataAssetEditor)
			.Asset(EditedAsset)
		];
}

FName FCharacterDataAssetEditorToolkit::GetToolkitFName() const
{
	return FName("CharacterDataAssetEditor");
}

FText FCharacterDataAssetEditorToolkit::GetBaseToolkitName() const
{
	return LOCTEXT("ToolkitName", "Paper2D+ Character Data Editor");
}

FString FCharacterDataAssetEditorToolkit::GetWorldCentricTabPrefix() const
{
	return LOCTEXT("WorldCentricTabPrefix", "CharacterData ").ToString();
}

FLinearColor FCharacterDataAssetEditorToolkit::GetWorldCentricTabColorScale() const
{
	return FLinearColor(0.3f, 0.2f, 0.5f, 1.0f);
}

#undef LOCTEXT_NAMESPACE
