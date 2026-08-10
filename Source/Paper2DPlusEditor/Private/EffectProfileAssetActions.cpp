// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "EffectProfileAssetActions.h"

#include "EffectProfileAssetEditorToolkit.h"
#include "Paper2DPlusValidationService.h"
#include "ToolMenus.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Styling/AppStyle.h"
#include "Widgets/Notifications/SNotificationList.h"

#define LOCTEXT_NAMESPACE "EffectProfileAssetActions"

namespace
{
	void EffectProfileActions_ShowNotification(const FText& Message, SNotificationItem::ECompletionState State = SNotificationItem::CS_Success)
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

void FEffectProfileAssetActions::OpenAssetEditor(const TArray<UObject*>& InObjects, TSharedPtr<IToolkitHost> EditWithinLevelEditor)
{
	const EToolkitMode::Type Mode = EditWithinLevelEditor.IsValid() ? EToolkitMode::WorldCentric : EToolkitMode::Standalone;

	for (UObject* Object : InObjects)
	{
		if (UPaper2DPlusEffectProfileAsset* Asset = Cast<UPaper2DPlusEffectProfileAsset>(Object))
		{
			TSharedRef<FEffectProfileAssetEditorToolkit> Toolkit = MakeShared<FEffectProfileAssetEditorToolkit>();
			Toolkit->InitEditor(Mode, EditWithinLevelEditor, Asset);
		}
	}
}

void FEffectProfileAssetActions::GetActions(const TArray<UObject*>& InObjects, FToolMenuSection& Section)
{
	TArray<TWeakObjectPtr<UPaper2DPlusEffectProfileAsset>> Assets;
	for (UObject* Obj : InObjects)
	{
		if (UPaper2DPlusEffectProfileAsset* Asset = Cast<UPaper2DPlusEffectProfileAsset>(Obj))
		{
			Assets.Add(Asset);
		}
	}
	if (Assets.IsEmpty())
	{
		return;
	}

	Section.AddMenuEntry(
		"Paper2DPlus_EffectProfile_Validate",
		LOCTEXT("Validate", "Validate"),
		LOCTEXT("ValidateTooltip", "Run Effect Profile validation and print issues to the Output Log."),
		FSlateIcon(FAppStyle::Get().GetStyleSetName(), "Icons.Help"),
		FUIAction(FExecuteAction::CreateLambda([Assets]()
		{
			int32 TotalIssues = 0;
			bool bHasErrors = false;
			for (const TWeakObjectPtr<UPaper2DPlusEffectProfileAsset>& WeakAsset : Assets)
			{
				UPaper2DPlusEffectProfileAsset* Asset = WeakAsset.Get();
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
					UE_LOG(LogPaper2DPlusEditor, Warning, TEXT("[%s] Effect Profile %s [%s/%s]: %s"),
						*Asset->GetName(),
						*FPaper2DPlusValidationService::SeverityText(Issue.Severity).ToString(),
						*Issue.ItemIdentity,
						*Issue.Field.ToString(),
						*Issue.Message.ToString());
				}
			}

			EffectProfileActions_ShowNotification(
				TotalIssues == 0
					? LOCTEXT("ValidationOk", "Effect Profile validation passed.")
					: FText::Format(LOCTEXT("ValidationIssues", "Effect Profile validation found {0} issue(s). See Output Log."), FText::AsNumber(TotalIssues)),
				TotalIssues == 0 ? SNotificationItem::CS_Success : (bHasErrors ? SNotificationItem::CS_Fail : SNotificationItem::CS_Pending));
		})));
}

#undef LOCTEXT_NAMESPACE
