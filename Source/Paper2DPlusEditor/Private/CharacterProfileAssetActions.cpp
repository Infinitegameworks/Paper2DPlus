// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "CharacterProfileAssetActions.h"
#include "CharacterProfileAssetEditor.h"
#include "ToolMenus.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "DesktopPlatformModule.h"
#include "IDesktopPlatform.h"
#include "Misc/Paths.h"

/** FCharacterProfileAssetActions — Content Browser asset actions: open editor, context menu, thumbnails. */

#define LOCTEXT_NAMESPACE "CharacterProfileAssetActions"

void FCharacterProfileAssetActions::OpenAssetEditor(const TArray<UObject*>& InObjects, TSharedPtr<IToolkitHost> EditWithinLevelEditor)
{
	EToolkitMode::Type Mode = EditWithinLevelEditor.IsValid() ? EToolkitMode::WorldCentric : EToolkitMode::Standalone;

	for (UObject* Object : InObjects)
	{
		if (UPaper2DPlusCharacterProfileAsset* Asset = Cast<UPaper2DPlusCharacterProfileAsset>(Object))
		{
			// UAssetEditorSubsystem handles single-instance — if already open, focuses existing tab
			TSharedRef<FCharacterProfileAssetEditorToolkit> Toolkit = MakeShared<FCharacterProfileAssetEditorToolkit>();
			Toolkit->InitEditor(Mode, EditWithinLevelEditor, Asset);
		}
	}
}

static void ShowNotification(const FText& Message, SNotificationItem::ECompletionState State = SNotificationItem::CS_Success)
{
	FNotificationInfo Info(Message);
	Info.ExpireDuration = 3.0f;
	TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info);
	if (Item.IsValid()) { Item->SetCompletionState(State); }
}

void FCharacterProfileAssetActions::GetActions(const TArray<UObject*>& InObjects, FToolMenuSection& Section)
{
	TArray<TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset>> Assets;
	for (UObject* Obj : InObjects)
	{
		if (auto* Asset = Cast<UPaper2DPlusCharacterProfileAsset>(Obj))
		{
			Assets.Add(Asset);
		}
	}
	if (Assets.IsEmpty()) return;

	Section.AddMenuEntry(
		"Paper2DPlus_ExportJson",
		LOCTEXT("ExportJson", "Export to JSON..."),
		LOCTEXT("ExportJsonTooltip", "Export this Character Profile asset to a JSON file"),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Save"),
		FUIAction(FExecuteAction::CreateLambda([Assets]()
		{
			IDesktopPlatform* DP = FDesktopPlatformModule::Get();
			if (!DP || Assets.Num() == 0) return;
			auto* Asset = Assets[0].Get();
			if (!Asset) return;

			TArray<FString> OutFilenames;
			const bool bPicked = DP->SaveFileDialog(
				nullptr, LOCTEXT("ExportJsonDlg", "Export CharacterProfile JSON").ToString(),
				FPaths::ProjectDir(), Asset->GetName() + TEXT(".json"),
				TEXT("JSON files (*.json)|*.json"), EFileDialogFlags::None, OutFilenames);
			if (!bPicked || OutFilenames.Num() == 0) return;

			if (Asset->ExportToJsonFile(OutFilenames[0]))
			{
				ShowNotification(FText::Format(LOCTEXT("ExportSuccess", "Exported to {0}"), FText::FromString(OutFilenames[0])));
			}
			else
			{
				ShowNotification(LOCTEXT("ExportFail", "Export failed — see Output Log"), SNotificationItem::CS_Fail);
			}
		})));

	Section.AddMenuEntry(
		"Paper2DPlus_ImportJson",
		LOCTEXT("ImportJson", "Import from JSON..."),
		LOCTEXT("ImportJsonTooltip", "Replace this asset's contents with a JSON file"),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Import"),
		FUIAction(FExecuteAction::CreateLambda([Assets]()
		{
			IDesktopPlatform* DP = FDesktopPlatformModule::Get();
			if (!DP || Assets.Num() == 0) return;
			auto* Asset = Assets[0].Get();
			if (!Asset) return;

			TArray<FString> OutFilenames;
			const bool bPicked = DP->OpenFileDialog(
				nullptr, LOCTEXT("ImportJsonDlg", "Import CharacterProfile JSON").ToString(),
				FPaths::ProjectDir(), TEXT(""),
				TEXT("JSON files (*.json)|*.json"), EFileDialogFlags::None, OutFilenames);
			if (!bPicked || OutFilenames.Num() == 0) return;

			if (Asset->ImportFromJsonFile(OutFilenames[0]))
			{
				ShowNotification(LOCTEXT("ImportSuccess", "Imported — asset marked dirty, save to commit."));
			}
			else
			{
				ShowNotification(LOCTEXT("ImportFail", "Import failed — see Output Log"), SNotificationItem::CS_Fail);
			}
		})));

	Section.AddMenuEntry(
		"Paper2DPlus_Validate",
		LOCTEXT("Validate", "Validate"),
		LOCTEXT("ValidateTooltip", "Run validation and print issues to the Output Log"),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Help"),
		FUIAction(FExecuteAction::CreateLambda([Assets]()
		{
			for (const TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset>& WeakAsset : Assets)
			{
				auto* Asset = WeakAsset.Get();
				if (!Asset) continue;
				TArray<FCharacterProfileValidationIssue> Issues;
				Asset->ValidateCharacterProfileAsset(Issues);
				if (Issues.IsEmpty())
				{
					ShowNotification(FText::Format(LOCTEXT("ValidateOk", "{0}: no issues found"),
						FText::FromString(Asset->GetName())));
				}
				else
				{
					ShowNotification(FText::Format(LOCTEXT("ValidateIssues", "{0}: {1} issue(s) — see Output Log"),
						FText::FromString(Asset->GetName()), FText::AsNumber(Issues.Num())),
						SNotificationItem::CS_Fail);
					for (const FCharacterProfileValidationIssue& Issue : Issues)
					{
						UE_LOG(LogTemp, Warning, TEXT("[%s] %s: %s"), *Asset->GetName(), *Issue.Context, *Issue.Message);
					}
				}
			}
		})));
}

#undef LOCTEXT_NAMESPACE
