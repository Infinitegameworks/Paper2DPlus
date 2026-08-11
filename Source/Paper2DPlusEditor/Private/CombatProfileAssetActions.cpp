// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "CombatProfileAssetActions.h"

#include "CombatProfileAssetEditorToolkit.h"
#include "Paper2DPlusValidationService.h"
#include "ToolMenus.h"
#include "Framework/Notifications/NotificationManager.h"
#include "ScopedTransaction.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "CombatProfileAssetActions"

namespace
{
	void CombatProfileActions_ShowNotification(const FText& Message, SNotificationItem::ECompletionState State = SNotificationItem::CS_Success)
	{
		FNotificationInfo Info(Message);
		Info.ExpireDuration = 3.0f;
		TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info);
		if (Item.IsValid())
		{
			Item->SetCompletionState(State);
		}
	}
}

void FCombatProfileAssetActions::OpenAssetEditor(const TArray<UObject*>& InObjects, TSharedPtr<IToolkitHost> EditWithinLevelEditor)
{
	const EToolkitMode::Type Mode = EditWithinLevelEditor.IsValid() ? EToolkitMode::WorldCentric : EToolkitMode::Standalone;

	for (UObject* Object : InObjects)
	{
		if (UPaper2DPlusCombatProfileAsset* Asset = Cast<UPaper2DPlusCombatProfileAsset>(Object))
		{
			TSharedRef<FCombatProfileAssetEditorToolkit> Toolkit = MakeShared<FCombatProfileAssetEditorToolkit>();
			Toolkit->InitEditor(Mode, EditWithinLevelEditor, Asset);
		}
	}
}

void FCombatProfileAssetActions::GetActions(const TArray<UObject*>& InObjects, FToolMenuSection& Section)
{
	TArray<TWeakObjectPtr<UPaper2DPlusCombatProfileAsset>> Assets;
	for (UObject* Obj : InObjects)
	{
		if (UPaper2DPlusCombatProfileAsset* Asset = Cast<UPaper2DPlusCombatProfileAsset>(Obj))
		{
			Assets.Add(Asset);
		}
	}
	if (Assets.IsEmpty())
	{
		return;
	}

	Section.AddMenuEntry(
		"Paper2DPlus_CombatProfile_GenerateAttackOptions",
		LOCTEXT("GenerateAttackOptions", "Generate Missing Attack Options"),
		LOCTEXT("GenerateAttackOptionsTooltip", "Add one attack option row for each Character Profile move that has attack hitboxes and no existing combat option."),
		FSlateIcon(FAppStyle::Get().GetStyleSetName(), "Icons.Plus"),
		FUIAction(FExecuteAction::CreateLambda([Assets]()
		{
			FScopedTransaction Transaction(LOCTEXT("GenerateCombatProfileAttackOptions", "Generate Combat Profile Attack Options"));
			int32 TotalAdded = 0;
			for (const TWeakObjectPtr<UPaper2DPlusCombatProfileAsset>& WeakAsset : Assets)
			{
				if (UPaper2DPlusCombatProfileAsset* Asset = WeakAsset.Get())
				{
					Asset->Modify();
					TotalAdded += Asset->GenerateAttackOptionsFromCharacterProfile(true);
					Asset->MarkPackageDirty();
				}
			}

			CombatProfileActions_ShowNotification(FText::Format(
				LOCTEXT("GeneratedAttackOptions", "Generated {0} Combat Profile attack option(s)."),
				FText::AsNumber(TotalAdded)));
		})));

	Section.AddMenuEntry(
		"Paper2DPlus_CombatProfile_RebuildVariables",
		LOCTEXT("RebuildVariables", "Rebuild Variable Bags"),
		LOCTEXT("RebuildVariablesTooltip", "Rebuild all Combat Profile property bags from the variable registry, preserving compatible values."),
		FSlateIcon(FAppStyle::Get().GetStyleSetName(), "Icons.Refresh"),
		FUIAction(FExecuteAction::CreateLambda([Assets]()
		{
			FScopedTransaction Transaction(LOCTEXT("RebuildCombatProfileVariableBags", "Rebuild Combat Profile Variable Bags"));
			for (const TWeakObjectPtr<UPaper2DPlusCombatProfileAsset>& WeakAsset : Assets)
			{
				if (UPaper2DPlusCombatProfileAsset* Asset = WeakAsset.Get())
				{
					Asset->Modify();
					Asset->RebuildVariableBags();
					Asset->MarkPackageDirty();
				}
			}

			CombatProfileActions_ShowNotification(LOCTEXT("RebuiltVariables", "Rebuilt Combat Profile variable bags."));
		})));

	Section.AddMenuEntry(
		"Paper2DPlus_CombatProfile_Validate",
		LOCTEXT("Validate", "Validate"),
		LOCTEXT("ValidateTooltip", "Run Combat Profile validation and print issues to the Output Log."),
		FSlateIcon(FAppStyle::Get().GetStyleSetName(), "Icons.Help"),
		FUIAction(FExecuteAction::CreateLambda([Assets]()
		{
			int32 TotalIssues = 0;
			bool bHasErrors = false;
			for (const TWeakObjectPtr<UPaper2DPlusCombatProfileAsset>& WeakAsset : Assets)
			{
				UPaper2DPlusCombatProfileAsset* Asset = WeakAsset.Get();
				if (!Asset)
				{
					continue;
				}

				TArray<FPaper2DPlusValidationIssue> Issues;
				FPaper2DPlusValidationService::Get().ValidateObject(Asset, Issues);
				TotalIssues += Issues.Num();
				for (const FPaper2DPlusValidationIssue& Issue : Issues)
				{
					bHasErrors |= Issue.Severity == EPaper2DPlusValidationSeverity::Error;
					UE_LOG(LogPaper2DPlusEditor, Warning, TEXT("[%s] Combat Profile %s [%s/%s]: %s"),
						*Asset->GetName(),
						*FPaper2DPlusValidationService::SeverityText(Issue.Severity).ToString(),
						*Issue.ItemIdentity,
						*Issue.Field.ToString(),
						*Issue.Message.ToString());
				}
			}

			CombatProfileActions_ShowNotification(
				TotalIssues == 0
					? LOCTEXT("ValidationOk", "Combat Profile validation passed.")
					: FText::Format(LOCTEXT("ValidationIssues", "Combat Profile validation found {0} issue(s). See Output Log."), FText::AsNumber(TotalIssues)),
				TotalIssues == 0 ? SNotificationItem::CS_Success : (bHasErrors ? SNotificationItem::CS_Fail : SNotificationItem::CS_Pending));
		})));
}

#undef LOCTEXT_NAMESPACE
