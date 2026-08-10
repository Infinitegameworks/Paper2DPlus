// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "CharacterProfileAssetActions.h"
#include "CharacterProfileAssetEditor.h"
#include "CharacterProfileJsonInteraction.h"
#include "ProfileToolsWindow.h"
#include "Paper2DPlusFrameData.h"
#include "Paper2DPlusValidationService.h"
#include "ToolMenus.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Styling/AppStyle.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "DesktopPlatformModule.h"
#include "IDesktopPlatform.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/MessageDialog.h"

/** FCharacterProfileAssetActions — Content Browser asset actions: open editor, context menu, thumbnails. */

#define LOCTEXT_NAMESPACE "CharacterProfileAssetActions"

static void ShowNotification(const FText& Message, SNotificationItem::ECompletionState State = SNotificationItem::CS_Success)
{
	FNotificationInfo Info(Message);
	Info.ExpireDuration = 3.0f;
	TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info);
	if (Item.IsValid()) { Item->SetCompletionState(State); }
}

namespace Paper2DPlusCharacterProfileJsonInteraction
{
	FText GetExportLayoutDisclosure()
	{
		return LOCTEXT(
			"JsonExportLayoutDisclosure",
			"Character Profile JSON exports gameplay/profile semantics only. Named Cue track names, order, and membership are excluded; duplicate or save the .uasset to preserve timeline organization.");
	}

	FText BuildImportLayoutPreview(const FString& FilePath)
	{
		return FText::Format(
			LOCTEXT(
				"JsonImportLayoutPreview",
				"Import gameplay/profile data from:\n{0}\n\nNamed Cue track names, order, and membership are not part of Character Profile JSON. Applying this import replaces the asset's semantic data and every imported Cue will use the Default track.\n\nChoose OK to Apply Import, or Cancel to leave the asset unchanged."),
			FText::FromString(FilePath));
	}

	EImportResult ApplyImportString(
		UPaper2DPlusCharacterProfileAsset& Asset,
		const FString& JsonString,
		EAppReturnType::Type Response,
		TArray<FPaper2DPlusCharacterProfileJsonImportWarning>& OutWarnings)
	{
		OutWarnings.Reset();
		if (Response != EAppReturnType::Ok)
		{
			return EImportResult::Cancelled;
		}
		return Asset.ImportFromJsonStringWithWarnings(JsonString, OutWarnings)
			? EImportResult::Applied
			: EImportResult::Failed;
	}

	void RunInteractiveExport(UPaper2DPlusCharacterProfileAsset& Asset)
	{
		IDesktopPlatform* DP = FDesktopPlatformModule::Get();
		if (!DP)
		{
			return;
		}

		TArray<FString> OutFilenames;
		const bool bPicked = DP->SaveFileDialog(
			nullptr, LOCTEXT("ExportJsonDlg", "Export Character Profile JSON (Timeline Layout Excluded)").ToString(),
			FPaths::ProjectDir(), Asset.GetName() + TEXT(".json"),
			TEXT("JSON files (*.json)|*.json"), EFileDialogFlags::None, OutFilenames);
		if (!bPicked || OutFilenames.Num() == 0)
		{
			return;
		}

		if (Asset.ExportToJsonFile(OutFilenames[0]))
		{
			ShowNotification(FText::Format(
				LOCTEXT("ExportSuccess", "Exported gameplay/profile JSON to {0}. Timeline organization remains in the .uasset."),
				FText::FromString(OutFilenames[0])));
		}
		else
		{
			ShowNotification(LOCTEXT("ExportFail", "Export failed — see Output Log"), SNotificationItem::CS_Fail);
		}
	}

	void RunInteractiveImport(UPaper2DPlusCharacterProfileAsset& Asset)
	{
		IDesktopPlatform* DP = FDesktopPlatformModule::Get();
		if (!DP)
		{
			return;
		}

		TArray<FString> OutFilenames;
		const bool bPicked = DP->OpenFileDialog(
			nullptr, LOCTEXT("ImportJsonDlg", "Import Character Profile JSON (Timeline Layout Is Not Imported)").ToString(),
			FPaths::ProjectDir(), TEXT(""),
			TEXT("JSON files (*.json)|*.json"), EFileDialogFlags::None, OutFilenames);
		if (!bPicked || OutFilenames.Num() == 0)
		{
			return;
		}

		FString JsonString;
		if (!FFileHelper::LoadFileToString(JsonString, *OutFilenames[0]))
		{
			ShowNotification(LOCTEXT("ImportReadFail", "Import failed — the selected JSON file could not be read."), SNotificationItem::CS_Fail);
			return;
		}

		const EAppReturnType::Type Response = FMessageDialog::Open(
			EAppMsgType::OkCancel,
			BuildImportLayoutPreview(OutFilenames[0]));
		TArray<FPaper2DPlusCharacterProfileJsonImportWarning> Warnings;
		const EImportResult Result = ApplyImportString(Asset, JsonString, Response, Warnings);
		if (Result == EImportResult::Cancelled)
		{
			return;
		}
		if (Result == EImportResult::Applied)
		{
			// The importer's machine-readable advisories were previously computed and discarded, so a
			// track-layout reset landed with no designer-visible signal at all. They
			// are ordinary advisories on a SUCCEEDED import, so they ride the ONE success notification:
			// a second, failure-styled toast would tell the designer their import failed when it did not.
			FText SuccessMessage = LOCTEXT("ImportSuccess", "Imported — asset marked dirty; imported Cues use Default and timeline organization remains .uasset-only.");
			if (Warnings.Num() > 0)
			{
				const FString AssetName = Asset.GetName();
				for (const FPaper2DPlusCharacterProfileJsonImportWarning& ImportWarning : Warnings)
				{
					// Loop variable is deliberately NOT named Warning — that is the UE_LOG verbosity token.
					UE_LOG(LogTemp, Warning, TEXT("[%s] JSON import [%s]: %s"),
						*AssetName, *ImportWarning.Code.ToString(), *ImportWarning.Message);
				}
				SuccessMessage = FText::Format(
					LOCTEXT("ImportSuccessWithAdvisories", "Imported — asset marked dirty; imported Cues use Default and timeline organization remains .uasset-only. {0} advisory item(s) reported — see Output Log."),
					FText::AsNumber(Warnings.Num()));
			}
			ShowNotification(SuccessMessage);
		}
		else
		{
			ShowNotification(LOCTEXT("ImportFail", "Import failed — see Output Log"), SNotificationItem::CS_Fail);
		}
	}
}

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
		"Paper2DPlus_ProfileTools",
		LOCTEXT("ProfileTools", "Profile Tools..."),
		LOCTEXT("ProfileToolsTooltip", "Open the Profile Tools window: Sprite Bounds, Relative Transform, validation, and optional PaperZD sequence authoring. Animation groups and combo-chain authoring live in the Animations workspace."),
		FSlateIcon(FAppStyle::Get().GetStyleSetName(), "LevelEditor.Tabs.Details"),
		FUIAction(FExecuteAction::CreateLambda([Assets]()
		{
			UPaper2DPlusCharacterProfileAsset* Asset = Assets.Num() > 0 ? Assets[0].Get() : nullptr;
			if (!Asset) return;
			// Single-instance window (coexists with the main Character Profile editor). A second
			// request focuses the live window rather than opening another.
			SProfileToolsWindow::OpenProfileTools(Asset);
		})));

	Section.AddMenuEntry(
		"Paper2DPlus_ExportJson",
		LOCTEXT("ExportJson", "Export to JSON..."),
		Paper2DPlusCharacterProfileJsonInteraction::GetExportLayoutDisclosure(),
		FSlateIcon(FAppStyle::Get().GetStyleSetName(), "Icons.Save"),
		FUIAction(FExecuteAction::CreateLambda([Assets]()
		{
			if (Assets.Num() == 0) return;
			auto* Asset = Assets[0].Get();
			if (!Asset) return;
			// One implementation, shared with the Character Profile editor's Asset menu.
			Paper2DPlusCharacterProfileJsonInteraction::RunInteractiveExport(*Asset);
		})));

	Section.AddMenuEntry(
		"Paper2DPlus_ImportJson",
		LOCTEXT("ImportJson", "Import from JSON..."),
		LOCTEXT("ImportJsonTooltip", "Preview and apply gameplay/profile JSON. Named Cue track organization is not imported; imported Cues use Default."),
		FSlateIcon(FAppStyle::Get().GetStyleSetName(), "Icons.Import"),
		FUIAction(FExecuteAction::CreateLambda([Assets]()
		{
			if (Assets.Num() == 0) return;
			auto* Asset = Assets[0].Get();
			if (!Asset) return;
			// One implementation, shared with the Character Profile editor's Asset menu.
			Paper2DPlusCharacterProfileJsonInteraction::RunInteractiveImport(*Asset);
		})));

	Section.AddMenuEntry(
		"Paper2DPlus_ExportFrameDataCsv",
		LOCTEXT("ExportFrameDataCsv", "Export Frame Data (CSV)..."),
		LOCTEXT("ExportFrameDataCsvTooltip", "Export the computed fighting-game frame-data table to a CSV file"),
		FSlateIcon(FAppStyle::Get().GetStyleSetName(), "Icons.Save"),
		FUIAction(FExecuteAction::CreateLambda([Assets]()
		{
			IDesktopPlatform* DP = FDesktopPlatformModule::Get();
			if (!DP || Assets.Num() == 0) return;
			auto* Asset = Assets[0].Get();
			if (!Asset) return;

			TArray<FString> OutFilenames;
			const bool bPicked = DP->SaveFileDialog(
				nullptr, LOCTEXT("ExportFrameDataCsvDlg", "Export Frame Data CSV").ToString(),
				FPaths::ProjectDir(), Asset->GetName() + TEXT("_FrameData.csv"),
				TEXT("CSV files (*.csv)|*.csv"), EFileDialogFlags::None, OutFilenames);
			if (!bPicked || OutFilenames.Num() == 0) return;

			TArray<FPaper2DPlusMoveFrameData> Rows;
			FPaper2DPlusFrameData::ComputeAllMoveFrameData(Asset, Rows);
			const FString Csv = FPaper2DPlusFrameData::ExportFrameDataToCsv(Rows);
			if (FFileHelper::SaveStringToFile(Csv, *OutFilenames[0]))
			{
				ShowNotification(FText::Format(LOCTEXT("ExportFrameDataCsvOk", "Exported {0} move(s) to {1}"),
					FText::AsNumber(Rows.Num()), FText::FromString(OutFilenames[0])));
			}
			else
			{
				ShowNotification(LOCTEXT("ExportFrameDataCsvFail", "Frame Data CSV export failed — see Output Log"), SNotificationItem::CS_Fail);
			}
		})));

	Section.AddMenuEntry(
		"Paper2DPlus_ExportFrameDataJson",
		LOCTEXT("ExportFrameDataJson", "Export Frame Data (JSON)..."),
		LOCTEXT("ExportFrameDataJsonTooltip", "Export the computed fighting-game frame-data table to a JSON file"),
		FSlateIcon(FAppStyle::Get().GetStyleSetName(), "Icons.Save"),
		FUIAction(FExecuteAction::CreateLambda([Assets]()
		{
			IDesktopPlatform* DP = FDesktopPlatformModule::Get();
			if (!DP || Assets.Num() == 0) return;
			auto* Asset = Assets[0].Get();
			if (!Asset) return;

			TArray<FString> OutFilenames;
			const bool bPicked = DP->SaveFileDialog(
				nullptr, LOCTEXT("ExportFrameDataJsonDlg", "Export Frame Data JSON").ToString(),
				FPaths::ProjectDir(), Asset->GetName() + TEXT("_FrameData.json"),
				TEXT("JSON files (*.json)|*.json"), EFileDialogFlags::None, OutFilenames);
			if (!bPicked || OutFilenames.Num() == 0) return;

			TArray<FPaper2DPlusMoveFrameData> Rows;
			FPaper2DPlusFrameData::ComputeAllMoveFrameData(Asset, Rows);
			const FString Json = FPaper2DPlusFrameData::ExportFrameDataToJson(Rows);
			if (FFileHelper::SaveStringToFile(Json, *OutFilenames[0]))
			{
				ShowNotification(FText::Format(LOCTEXT("ExportFrameDataJsonOk", "Exported {0} move(s) to {1}"),
					FText::AsNumber(Rows.Num()), FText::FromString(OutFilenames[0])));
			}
			else
			{
				ShowNotification(LOCTEXT("ExportFrameDataJsonFail", "Frame Data JSON export failed — see Output Log"), SNotificationItem::CS_Fail);
			}
		})));

	Section.AddMenuEntry(
		"Paper2DPlus_Validate",
		LOCTEXT("Validate", "Validate"),
		LOCTEXT("ValidateTooltip", "Run validation and print issues to the Output Log"),
		FSlateIcon(FAppStyle::Get().GetStyleSetName(), "Icons.Help"),
		FUIAction(FExecuteAction::CreateLambda([Assets]()
		{
			for (const TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset>& WeakAsset : Assets)
			{
				auto* Asset = WeakAsset.Get();
				if (!Asset) continue;
				TArray<FPaper2DPlusValidationIssue> Issues;
				FPaper2DPlusValidationService::Get().ValidateObject(Asset, Issues);
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
					for (const FPaper2DPlusValidationIssue& Issue : Issues)
					{
						UE_LOG(LogTemp, Warning, TEXT("[%s] %s [%s/%s]: %s"),
							*Asset->GetName(),
							*FPaper2DPlusValidationService::SeverityText(Issue.Severity).ToString(),
							*Issue.ItemIdentity,
							*Issue.Field.ToString(),
							*Issue.Message.ToString());
					}
				}
			}
		})));
}

#undef LOCTEXT_NAMESPACE
