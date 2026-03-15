// Copyright 2026 Infinite Gameworks. All Rights Reserved.

// CharacterProfileAssetEditorToolkit.cpp - Asset editor toolkit wrapper
// Split from CharacterProfileAssetEditor.cpp for maintainability

#include "CharacterProfileAssetEditor.h"
#include "Widgets/Docking/SDockTab.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWindow.h"
#include "Misc/MessageDialog.h"

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

bool FCharacterProfileAssetEditorToolkit::OnRequestClose(EAssetEditorCloseReason InCloseReason)
{
	if (EditedAsset && !bCloseDialogShown)
	{
		// Collect flipbook indices with unapplied offsets
		TArray<TPair<int32, FString>> UnappliedFlipbooks;
		for (int32 i = 0; i < EditedAsset->Flipbooks.Num(); ++i)
		{
			const FFlipbookHitboxData& Anim = EditedAsset->Flipbooks[i];
			for (const FSpriteExtractionInfo& Info : Anim.FrameExtractionInfo)
			{
				if (Info.SpriteOffset != FIntPoint::ZeroValue)
				{
					UnappliedFlipbooks.Emplace(i, Anim.FlipbookName);
					break;
				}
			}
		}

		if (UnappliedFlipbooks.Num() > 0)
		{
			// Build a custom dialog with clickable links
			TSharedRef<SVerticalBox> ListBox = SNew(SVerticalBox);

			bCloseDialogShown = true;

			// Store result in a shared bool so the dialog lambda can set it
			TSharedRef<bool> bUserAccepted = MakeShared<bool>(false);
			TSharedRef<int32> NavigateToIndex = MakeShared<int32>(INDEX_NONE);

			for (const TPair<int32, FString>& Entry : UnappliedFlipbooks)
			{
				const int32 FlipbookIdx = Entry.Key;
				const FString& FlipbookName = Entry.Value;

				ListBox->AddSlot()
				.AutoHeight()
				.Padding(4, 2)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
					.ToolTipText(LOCTEXT("GoToFlipbookTooltip", "Go to this flipbook in the Alignment tab"))
					.OnClicked_Lambda([NavigateToIndex, FlipbookIdx, bUserAccepted]()
					{
						*NavigateToIndex = FlipbookIdx;
						*bUserAccepted = false;
						if (TSharedPtr<SWindow> Window = FSlateApplication::Get().GetActiveTopLevelWindow())
						{
							Window->RequestDestroyWindow();
						}
						return FReply::Handled();
					})
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(0, 0, 6, 0)
						[
							SNew(STextBlock)
							.Text(FText::FromString(TEXT("\x2192")))
							.ColorAndOpacity(FSlateColor(FLinearColor(0.3f, 0.6f, 1.0f)))
						]
						+ SHorizontalBox::Slot()
						.FillWidth(1.0f)
						.VAlign(VAlign_Center)
						[
							SNew(STextBlock)
							.Text(FText::FromString(FlipbookName))
							.ColorAndOpacity(FSlateColor(FLinearColor(0.3f, 0.6f, 1.0f)))
						]
					]
				];
			}

			TSharedRef<SWindow> DialogWindow = SNew(SWindow)
				.Title(FText::Format(
					LOCTEXT("UnappliedOffsetsTitle", "Unapplied Offsets - {0}"),
					FText::FromString(EditedAsset->DisplayName)))
				.ClientSize(FVector2D(400, 0))
				.SizingRule(ESizingRule::Autosized)
				.SupportsMaximize(false)
				.SupportsMinimize(false)
				.IsTopmostWindow(true)
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
					.Padding(12)
					[
						SNew(SVerticalBox)

						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0, 0, 0, 8)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("UnappliedOffsetsMsg",
								"The following flipbooks have unapplied alignment offsets.\n"
								"These offsets have NOT been baked into the sprite assets."))
							.AutoWrapText(true)
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0, 0, 0, 4)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("ClickToNavigate", "Click a flipbook to navigate to it:"))
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0, 0, 0, 12)
						[
							ListBox
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot()
							.FillWidth(1.0f)
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.Padding(4, 0)
							[
								SNew(SButton)
								.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
								.OnClicked_Lambda([bUserAccepted]()
								{
									*bUserAccepted = true;
									if (TSharedPtr<SWindow> Window = FSlateApplication::Get().GetActiveTopLevelWindow())
									{
										Window->RequestDestroyWindow();
									}
									return FReply::Handled();
								})
								[
									SNew(STextBlock)
									.Text(LOCTEXT("CloseAnywayBtn", "Close Anyway"))
								]
							]
							+ SHorizontalBox::Slot()
							.AutoWidth()
							[
								SNew(SButton)
								.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
								.OnClicked_Lambda([bUserAccepted]()
								{
									*bUserAccepted = false;
									if (TSharedPtr<SWindow> Window = FSlateApplication::Get().GetActiveTopLevelWindow())
									{
										Window->RequestDestroyWindow();
									}
									return FReply::Handled();
								})
								[
									SNew(STextBlock)
									.Text(LOCTEXT("CancelCloseBtn", "Cancel"))
								]
							]
						]
					]
				];

			FSlateApplication::Get().AddModalWindow(DialogWindow, FSlateApplication::Get().GetActiveTopLevelWindow());

			// If user clicked a flipbook link, navigate to it
			if (*NavigateToIndex != INDEX_NONE && EditorWidget.IsValid())
			{
				EditorWidget->NavigateToFlipbookAlignment(*NavigateToIndex);
			}

			if (!*bUserAccepted)
			{
				bCloseDialogShown = false; // Reset so next close attempt prompts again
				return false;
			}
		}
	}
	return true;
}

#undef LOCTEXT_NAMESPACE
