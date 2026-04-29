// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "Paper2DPlusEditorModule.h"
#include "AssetToolsModule.h"

DEFINE_LOG_CATEGORY(LogPaper2DPlusEditor);
#include "IAssetTools.h"
#include "CharacterProfileAssetActions.h"
#include "ContentBrowserModule.h"
#include "ContentBrowserMenuContexts.h"
#include "ToolMenus.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 4)
#include "Paper2DPlusCharacterProfileAssetValidator.h"
#include "EditorValidatorSubsystem.h"
#endif
#include "SpriteExtractorWindow.h"
#include "AsepriteImporter.h"
#include "TextureWatcherService.h"
#include "CharacterProfileAssetThumbnailRenderer.h"
#include "ThumbnailRendering/ThumbnailManager.h"
#include "Editor.h"
#include "PaperFlipbook.h"
#include "ScopedTransaction.h"
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
#include "PropertyCustomizationHelpers.h"
#include "Widgets/Input/SButton.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBox.h"

/** FPaper2DPlusEditorModule — Editor plugin startup: asset type registration, menu extensions, validator setup. */

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
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 4)
	RegisterDataValidators();
#endif
	FSpriteExtractorActions::RegisterMenus();
	FAsepriteImporter::RegisterMenus();

	// Register CharacterProfile thumbnail renderer so the Content Browser shows a
	// preview of the first flipbook's first-frame sprite instead of the generic icon.
	UThumbnailManager::Get().RegisterCustomRenderer(
		UPaper2DPlusCharacterProfileAsset::StaticClass(),
		UPaper2DPlusCharacterProfileThumbnailRenderer::StaticClass());

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

	// Unregister thumbnail renderer (safe if UThumbnailManager was never used)
	if (UObjectInitialized())
	{
		UThumbnailManager::Get().UnregisterCustomRenderer(UPaper2DPlusCharacterProfileAsset::StaticClass());
	}

	UnregisterAssetTools();
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 4)
	UnregisterDataValidators();
#endif
	FSpriteExtractorActions::UnregisterMenus();
	FAsepriteImporter::UnregisterMenus();
}

void FPaper2DPlusEditorModule::RegisterAssetTools()
{
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

	// Register asset type actions for Paper2DPlusCharacterProfileAsset
	TSharedPtr<IAssetTypeActions> CharacterProfileActions = MakeShareable(new FCharacterProfileAssetActions());
	AssetTools.RegisterAssetTypeActions(CharacterProfileActions.ToSharedRef());
	RegisteredAssetTypeActions.Add(CharacterProfileActions);
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
// UE 5.0 UContentBrowserAssetContextMenuContext lacks SelectedAssets/LoadSelectedObjects — skip context menu extension
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 1)
	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateLambda([]()
	{
		UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("ContentBrowser.AssetContextMenu.PaperFlipbook");
		if (!Menu) return;

		FToolMenuSection& Section = Menu->FindOrAddSection("Paper2DPlus");
		Section.AddDynamicEntry("AddToCharacterProfile", FNewToolMenuSectionDelegate::CreateLambda([](FToolMenuSection& InSection)
		{
			UContentBrowserAssetContextMenuContext* Context = InSection.FindContext<UContentBrowserAssetContextMenuContext>();
			if (!Context || Context->SelectedAssets.IsEmpty()) return;

			InSection.AddMenuEntry(
				"AddToCharacterProfileAsset",
				LOCTEXT("AddToCharacterProfile", "Add to Character Profile Asset..."),
				LOCTEXT("AddToCharacterProfileTooltip", "Add the selected flipbook(s) as new animations on a Character Profile Asset"),
				FSlateIcon(FAppStyle::GetAppStyleSetName(), "ClassIcon.PaperFlipbook"),
				FUIAction(FExecuteAction::CreateLambda([Context]()
				{
					// Load selected flipbooks
					TArray<UPaperFlipbook*> Flipbooks;
					Flipbooks = Context->LoadSelectedObjects<UPaperFlipbook>();
					if (Flipbooks.IsEmpty()) return;

					// Create asset picker window
					TSharedRef<SWindow> PickerWindow = SNew(SWindow)
						.Title(LOCTEXT("PickCharacterProfileTitle", "Select Character Profile Asset"))
						.ClientSize(FVector2D(400, 120))
						.SupportsMinimize(false)
						.SupportsMaximize(false);

					TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset> SelectedAsset;

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
							.AllowedClass(UPaper2DPlusCharacterProfileAsset::StaticClass())
							.OnObjectChanged_Lambda([&SelectedAsset](const FAssetData& AssetData)
							{
								SelectedAsset = Cast<UPaper2DPlusCharacterProfileAsset>(AssetData.GetAsset());
							})
						]
						+ SVerticalBox::Slot()
						.Padding(8, 0, 8, 8)
						.AutoHeight()
						.HAlign(HAlign_Right)
						[
							SNew(SButton)
							.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
							.Text(LOCTEXT("AddBtn", "Add"))
							.IsEnabled_Lambda([&SelectedAsset]() { return SelectedAsset.IsValid(); })
							.OnClicked_Lambda([&SelectedAsset, &Flipbooks, &PickerWindow]() -> FReply
							{
								if (!SelectedAsset.IsValid()) return FReply::Handled();

								UPaper2DPlusCharacterProfileAsset* Asset = SelectedAsset.Get();
								FScopedTransaction Transaction(LOCTEXT("AddFlipbooksToCDA", "Add Flipbooks to Character Profile Asset"));
								Asset->Modify();

								int32 AddedCount = 0;
								for (UPaperFlipbook* FB : Flipbooks)
								{
									if (!FB) continue;

									// Check for duplicate
									TSoftObjectPtr<UPaperFlipbook> SoftRef(FB);
									bool bAlreadyExists = false;
									for (const FFlipbookProfileEntry& Existing : Asset->Flipbooks)
									{
										if (Existing.Identity.Flipbook == SoftRef)
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

									FFlipbookProfileEntry NewAnim;
									NewAnim.Identity.FlipbookName = AnimName;
									NewAnim.Identity.Flipbook = SoftRef;

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
#endif // ENGINE_MINOR_VERSION >= 1
}

#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 4)
void FPaper2DPlusEditorModule::RegisterDataValidators()
{
	if (!GEditor)
	{
		return;
	}

	UEditorValidatorSubsystem* ValidatorSubsystem = GEditor->GetEditorSubsystem<UEditorValidatorSubsystem>();
	if (!ValidatorSubsystem)
	{
		return;
	}

	if (!RegisteredCharacterProfileValidator)
	{
		RegisteredCharacterProfileValidator = NewObject<UPaper2DPlusCharacterProfileAssetValidator>(GetTransientPackage());
	}

	ValidatorSubsystem->AddValidator(RegisteredCharacterProfileValidator);
}

void FPaper2DPlusEditorModule::UnregisterDataValidators()
{
	if (!RegisteredCharacterProfileValidator || !GEditor)
	{
		RegisteredCharacterProfileValidator = nullptr;
		return;
	}

	if (UEditorValidatorSubsystem* ValidatorSubsystem = GEditor->GetEditorSubsystem<UEditorValidatorSubsystem>())
	{
		// Note: the entire register/unregister pair is already gated by the outer
		// UE 5.4+ #if, so RemoveValidator is always available here. (PR #98 review #4
		// flagged this as a UE 5.0 leak but the outer guard already excludes 5.0 —
		// inner 5.1+ guard was redundant and has been removed for clarity.)
		ValidatorSubsystem->RemoveValidator(RegisteredCharacterProfileValidator);
	}

	RegisteredCharacterProfileValidator = nullptr;
}
#endif // UE 5.4+

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FPaper2DPlusEditorModule, Paper2DPlusEditor)
