// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "ClashGraphAssetActions.h"

#include "ClashGraphAssetEditorToolkit.h"
#include "Paper2DPlusClashTypes.h"
#include "ToolMenus.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Styling/AppStyle.h"
#include "Widgets/Notifications/SNotificationList.h"

#define LOCTEXT_NAMESPACE "ClashGraphAssetActions"

namespace
{
	void ClashGraphActions_ShowNotification(const FText& Message, SNotificationItem::ECompletionState State = SNotificationItem::CS_Success)
	{
		FNotificationInfo Info(Message);
		Info.ExpireDuration = 3.0f;
		TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info);
		if (Item.IsValid())
		{
			Item->SetCompletionState(State);
		}
	}

	const TCHAR* ClashGraphActions_SeverityText(EClashValidationSeverity Severity)
	{
		switch (Severity)
		{
		case EClashValidationSeverity::Error:
			return TEXT("Error");
		case EClashValidationSeverity::Warning:
			return TEXT("Warning");
		default:
			return TEXT("Info");
		}
	}
}

void FClashGraphAssetActions::OpenAssetEditor(const TArray<UObject*>& InObjects, TSharedPtr<IToolkitHost> EditWithinLevelEditor)
{
	const EToolkitMode::Type Mode = EditWithinLevelEditor.IsValid() ? EToolkitMode::WorldCentric : EToolkitMode::Standalone;
	for (UObject* Object : InObjects)
	{
		if (UPaper2DPlusClashGraphAsset* Asset = Cast<UPaper2DPlusClashGraphAsset>(Object))
		{
			TSharedRef<FClashGraphAssetEditorToolkit> Toolkit = MakeShared<FClashGraphAssetEditorToolkit>();
			Toolkit->InitEditor(Mode, EditWithinLevelEditor, Asset);
		}
	}
}

void FClashGraphAssetActions::GetActions(const TArray<UObject*>& InObjects, FToolMenuSection& Section)
{
	TArray<TWeakObjectPtr<UPaper2DPlusClashGraphAsset>> Assets;
	for (UObject* Obj : InObjects)
	{
		if (UPaper2DPlusClashGraphAsset* Asset = Cast<UPaper2DPlusClashGraphAsset>(Obj))
		{
			Assets.Add(Asset);
		}
	}
	if (Assets.IsEmpty())
	{
		return;
	}

	Section.AddMenuEntry(
		"Paper2DPlus_ClashGraph_Validate",
		LOCTEXT("Validate", "Validate"),
		LOCTEXT("ValidateTooltip", "Run clash-graph validation (detects authored desync / ambiguous pairs) and print issues to the Output Log."),
		FSlateIcon(FAppStyle::Get().GetStyleSetName(), "Icons.Help"),
		FUIAction(FExecuteAction::CreateLambda([Assets]()
		{
			int32 TotalIssues = 0;
			bool bHasErrors = false;
			for (const TWeakObjectPtr<UPaper2DPlusClashGraphAsset>& WeakAsset : Assets)
			{
				UPaper2DPlusClashGraphAsset* Asset = WeakAsset.Get();
				if (!Asset)
				{
					continue;
				}

				TArray<FClashValidationIssue> Issues;
				const bool bOk = Asset->ValidateClashGraphAsset(TArray<FGameplayTag>(), Issues);
				bHasErrors |= !bOk;
				TotalIssues += Issues.Num();
				for (const FClashValidationIssue& Issue : Issues)
				{
					UE_LOG(LogPaper2DPlusEditor, Warning, TEXT("[%s] Clash Graph %s: %s"),
						*Asset->GetName(),
						ClashGraphActions_SeverityText(Issue.Severity),
						*Issue.Message);
				}
			}

			ClashGraphActions_ShowNotification(
				TotalIssues == 0
					? LOCTEXT("ValidationOk", "Clash Graph validation passed.")
					: FText::Format(LOCTEXT("ValidationIssues", "Clash Graph validation found {0} issue(s). See Output Log."), FText::AsNumber(TotalIssues)),
				TotalIssues == 0 ? SNotificationItem::CS_Success : (bHasErrors ? SNotificationItem::CS_Fail : SNotificationItem::CS_Pending));
		})));
}

#undef LOCTEXT_NAMESPACE
