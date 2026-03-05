// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "Paper2DPlusEditorModule.h"
#include "AssetToolsModule.h"

DEFINE_LOG_CATEGORY(LogPaper2DPlusEditor);
#include "IAssetTools.h"
#include "CharacterDataAssetActions.h"
#include "ContentBrowserModule.h"
#include "ContentBrowserMenuContexts.h"
#include "ToolMenus.h"
#include "Paper2DPlusCharacterDataAsset.h"
#include "SpriteExtractorWindow.h"
#include "AsepriteImporter.h"
#include "TextureWatcherService.h"
#include "Editor.h"
#include "PaperFlipbook.h"
#include "ScopedTransaction.h"
#include "PropertyCustomizationHelpers.h"
#include "Widgets/Input/SButton.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBox.h"

#define LOCTEXT_NAMESPACE "FPaper2DPlusEditorModule"

// Initialize static member
EAssetTypeCategories::Type FPaper2DPlusEditorModule::Paper2DPlusAssetCategory = EAssetTypeCategories::Misc;

void FPaper2DPlusEditorModule::StartupModule()
{
	// Register custom asset category
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	Paper2DPlusAssetCategory = AssetTools.RegisterAdvancedAssetCategory(
		FName(TEXT("Paper2DPlus")),
		LOCTEXT("Paper2DPlusAssetCategory", "Paper2D+")
	);

	RegisterAssetTools();
	RegisterMenuExtensions();
	FSpriteExtractorActions::RegisterMenus();
	FAsepriteImporter::RegisterMenus();

	// Initialize texture watcher service after a short delay to ensure asset registry is ready
	if (GEditor)
	{
		GEditor->GetTimerManager()->SetTimerForNextTick([]()
		{
			FTextureWatcherService::Get().Initialize();
		});
	}
}

void FPaper2DPlusEditorModule::ShutdownModule()
{
	// Shutdown texture watcher service first
	FTextureWatcherService::Get().Shutdown();

	UnregisterAssetTools();
	FSpriteExtractorActions::UnregisterMenus();
	FAsepriteImporter::UnregisterMenus();
}

void FPaper2DPlusEditorModule::RegisterAssetTools()
{
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

	// Register asset type actions for Paper2DPlusCharacterDataAsset
	TSharedPtr<IAssetTypeActions> CharacterDataActions = MakeShareable(new FCharacterDataAssetActions());
	AssetTools.RegisterAssetTypeActions(CharacterDataActions.ToSharedRef());
	RegisteredAssetTypeActions.Add(CharacterDataActions);
}

void FPaper2DPlusEditorModule::UnregisterAssetTools()
{
	FAssetToolsModule* AssetToolsModule = FModuleManager::GetModulePtr<FAssetToolsModule>("AssetTools");
	if (AssetToolsModule)
	{
		IAssetTools& AssetTools = AssetToolsModule->Get();
		for (TSharedPtr<IAssetTypeActions>& Actions : RegisteredAssetTypeActions)
		{
			AssetTools.UnregisterAssetTypeActions(Actions.ToSharedRef());
		}
	}
	RegisteredAssetTypeActions.Empty();
}

void FPaper2DPlusEditorModule::RegisterMenuExtensions()
{
	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateLambda([]()
	{
		UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("ContentBrowser.AssetContextMenu.PaperFlipbook");
		if (!Menu) return;

		FToolMenuSection& Section = Menu->FindOrAddSection("Paper2DPlus");
		Section.AddDynamicEntry("AddToCharacterData", FNewToolMenuSectionDelegate::CreateLambda([](FToolMenuSection& InSection)
		{
			UContentBrowserAssetContextMenuContext* Context = InSection.FindContext<UContentBrowserAssetContextMenuContext>();
			if (!Context || Context->SelectedAssets.IsEmpty()) return;

			InSection.AddMenuEntry(
				"AddToCharacterDataAsset",
				LOCTEXT("AddToCharacterData", "Add to Character Data Asset..."),
				LOCTEXT("AddToCharacterDataTooltip", "Add the selected flipbook(s) as new animations on a Character Data Asset"),
				FSlateIcon(FAppStyle::GetAppStyleSetName(), "ClassIcon.PaperFlipbook"),
				FUIAction(FExecuteAction::CreateLambda([Context]()
				{
					// Load selected flipbooks
					TArray<UPaperFlipbook*> Flipbooks = Context->LoadSelectedObjects<UPaperFlipbook>();
					if (Flipbooks.IsEmpty()) return;

					// Create asset picker window
					TSharedRef<SWindow> PickerWindow = SNew(SWindow)
						.Title(LOCTEXT("PickCharacterDataTitle", "Select Character Data Asset"))
						.ClientSize(FVector2D(400, 120))
						.SupportsMinimize(false)
						.SupportsMaximize(false);

					TWeakObjectPtr<UPaper2DPlusCharacterDataAsset> SelectedAsset;

					PickerWindow->SetContent(
						SNew(SVerticalBox)
						+ SVerticalBox::Slot()
						.Padding(8)
						.AutoHeight()
						[
							SNew(STextBlock)
							.Text(FText::Format(LOCTEXT("PickerPrompt", "Add {0} flipbook(s) to:"), FText::AsNumber(Flipbooks.Num())))
						]
						+ SVerticalBox::Slot()
						.Padding(8, 0, 8, 8)
						.AutoHeight()
						[
							SNew(SObjectPropertyEntryBox)
							.AllowedClass(UPaper2DPlusCharacterDataAsset::StaticClass())
							.OnObjectChanged_Lambda([&SelectedAsset](const FAssetData& AssetData)
							{
								SelectedAsset = Cast<UPaper2DPlusCharacterDataAsset>(AssetData.GetAsset());
							})
						]
						+ SVerticalBox::Slot()
						.Padding(8, 0, 8, 8)
						.AutoHeight()
						.HAlign(HAlign_Right)
						[
							SNew(SButton)
							.Text(LOCTEXT("AddBtn", "Add"))
							.IsEnabled_Lambda([&SelectedAsset]() { return SelectedAsset.IsValid(); })
							.OnClicked_Lambda([&SelectedAsset, &Flipbooks, &PickerWindow]() -> FReply
							{
								if (!SelectedAsset.IsValid()) return FReply::Handled();

								UPaper2DPlusCharacterDataAsset* Asset = SelectedAsset.Get();
								FScopedTransaction Transaction(LOCTEXT("AddFlipbooksToCDA", "Add Flipbooks to Character Data Asset"));
								Asset->Modify();

								int32 AddedCount = 0;
								for (UPaperFlipbook* FB : Flipbooks)
								{
									if (!FB) continue;

									// Check for duplicate
									TSoftObjectPtr<UPaperFlipbook> SoftRef(FB);
									bool bAlreadyExists = false;
									for (const FFlipbookHitboxData& Existing : Asset->Flipbooks)
									{
										if (Existing.Flipbook == SoftRef)
										{
											bAlreadyExists = true;
											UE_LOG(LogPaper2DPlusEditor, Log, TEXT("Skipping duplicate flipbook: %s"), *FB->GetName());
											break;
										}
									}
									if (bAlreadyExists) continue;

									// Generate unique name
									FString BaseName = FB->GetName();
									FString AnimName = BaseName;
									int32 Suffix = 2;
									while (Asset->FindFlipbookDataPtr(AnimName) != nullptr)
									{
										AnimName = FString::Printf(TEXT("%s (%d)"), *BaseName, Suffix++);
									}

									FFlipbookHitboxData NewAnim;
									NewAnim.FlipbookName = AnimName;
									NewAnim.Flipbook = SoftRef;

									int32 NewIndex = Asset->Flipbooks.Add(NewAnim);
									Asset->SyncFramesToFlipbook(NewIndex);
									AddedCount++;
								}

								Asset->MarkPackageDirty();
								PickerWindow->RequestDestroyWindow();

								if (AddedCount > 0)
								{
									FNotificationInfo Info(FText::Format(
										LOCTEXT("AddedFlipbooks", "Added {0} animation(s) to {1}"),
										FText::AsNumber(AddedCount),
										FText::FromString(Asset->GetName())
									));
									Info.ExpireDuration = 3.0f;
									FSlateNotificationManager::Get().AddNotification(Info);
								}

								return FReply::Handled();
							})
						]
					);

					FSlateApplication::Get().AddModalWindow(PickerWindow, FSlateApplication::Get().GetActiveTopLevelWindow());
				}))
			);
		}));
	}));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FPaper2DPlusEditorModule, Paper2DPlusEditor)
