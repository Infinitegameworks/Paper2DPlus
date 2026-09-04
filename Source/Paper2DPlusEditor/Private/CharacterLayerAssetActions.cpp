// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "CharacterLayerAssetActions.h"
#include "AsepriteImporter.h" // TASK-192 U8: Force Full Reimport
#include "CharacterLayerAssetEditorToolkit.h"
#include "Paper2DPlusCharacterLayerAsset.h"
#include "Paper2DPlusValidationService.h"
#include "ToolMenus.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Misc/MessageDialog.h"
#include "ScopedTransaction.h"
#include "Runtime/Launch/Resources/Version.h"
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION == 0
#include "EditorStyleSet.h"
#ifndef FAppStyle
#define FAppStyle FEditorStyle
#endif
#else
#include "Styling/AppStyle.h"
#endif
#include "Widgets/Notifications/SNotificationList.h"

#define LOCTEXT_NAMESPACE "CharacterLayerAssetActions"

namespace
{
	void ShowLayerValidationNotification(const FText& Message, SNotificationItem::ECompletionState State)
	{
		FNotificationInfo Info(Message);
		Info.ExpireDuration = 3.0f;
		TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info);
		if (Item.IsValid())
		{
			Item->SetCompletionState(State);
		}
	}

	bool SetLayerDeliveryMode(
		UPaper2DPlusCharacterLayerAsset* Asset,
		ECharacterLayerUsageMode TargetMode,
		bool bRequestConfirmation,
		FText* OutMessage)
	{
		auto SetMessage = [OutMessage](const FText& Message)
		{
			if (OutMessage) { *OutMessage = Message; }
		};
		if (!Asset)
		{
			SetMessage(LOCTEXT("ModeNullAsset", "No Character Layer Asset was supplied."));
			return false;
		}
		if (Asset->UsageMode == TargetMode)
		{
			SetMessage(LOCTEXT("ModeAlreadySet", "The Character Layer Asset already uses this delivery mode."));
			return true;
		}

#if WITH_EDITORONLY_DATA
		if (Asset->UsageMode == ECharacterLayerUsageMode::FixedBaked
			&& Asset->BakeAttachmentState != ECharacterLayerBakeAttachmentState::Detached)
		{
			const FText Message = Asset->BakeAttachmentState == ECharacterLayerBakeAttachmentState::RecoveryRequired
				? LOCTEXT("ModeRecoveryBlocked", "This fixed-baked asset requires recovery. Use Repair, then Detach, before changing runtime delivery mode.")
				: LOCTEXT("ModeAttachedBlocked", "This fixed-baked asset still manages baked output. Use Detach before changing runtime delivery mode.");
			SetMessage(Message);
			ShowLayerValidationNotification(Message, SNotificationItem::CS_Fail);
			return false;
		}
#endif

		const bool bEnableRuntime = TargetMode == ECharacterLayerUsageMode::RuntimeCustomizable;
		const FText Confirmation = FText::Format(
			LOCTEXT("EnableRuntimeConfirm", "Enable Runtime Customization for '{0}'?\n\nThe asset will compose its selected layers at runtime."),
			FText::FromString(Asset->GetName()));
		if (bRequestConfirmation
			&& FMessageDialog::Open(EAppMsgType::YesNo, Confirmation) != EAppReturnType::Yes)
		{
			SetMessage(LOCTEXT("ModeCancelled", "Delivery-mode change cancelled."));
			return false;
		}

		const FScopedTransaction Transaction(LOCTEXT("EnableRuntimeTransaction", "Enable Runtime Customization"));
		Asset->Modify();
		Asset->UsageMode = TargetMode;
		Asset->PostEditChange();
		Asset->MarkPackageDirty();

		TArray<FPaper2DPlusValidationIssue> Issues;
		FPaper2DPlusValidationService::Get().ValidateObject(Asset, Issues);
		const bool bHasErrors = Issues.ContainsByPredicate([](const FPaper2DPlusValidationIssue& Issue)
		{
			return Issue.Severity == EPaper2DPlusValidationSeverity::Error;
		});
		const FText Result = bHasErrors
			? LOCTEXT("ModeRuntimeEnabledWithIssues", "Runtime Customization enabled. Validation found errors; open Validation for details.")
			: LOCTEXT("ModeRuntimeEnabled", "Runtime Customization enabled.");
		SetMessage(Result);
		ShowLayerValidationNotification(Result,
			bHasErrors ? SNotificationItem::CS_Fail : SNotificationItem::CS_Success);
		return true;
	}
}

bool FCharacterLayerAssetActions::EnableRuntimeCustomization(
	UPaper2DPlusCharacterLayerAsset* Asset,
	bool bRequestConfirmation,
	FText* OutMessage)
{
	return SetLayerDeliveryMode(
		Asset, ECharacterLayerUsageMode::RuntimeCustomizable, bRequestConfirmation, OutMessage);
}

void FCharacterLayerAssetActions::OpenAssetEditor(const TArray<UObject*>& InObjects, TSharedPtr<IToolkitHost> EditWithinLevelEditor)
{
	EToolkitMode::Type Mode = EditWithinLevelEditor.IsValid() ? EToolkitMode::WorldCentric : EToolkitMode::Standalone;

	for (UObject* Object : InObjects)
	{
		if (UPaper2DPlusCharacterLayerAsset* Asset = Cast<UPaper2DPlusCharacterLayerAsset>(Object))
		{
			TSharedRef<FCharacterLayerAssetEditorToolkit> Toolkit = MakeShared<FCharacterLayerAssetEditorToolkit>();
			Toolkit->InitEditor(Mode, EditWithinLevelEditor, Asset);
		}
	}
}

void FCharacterLayerAssetActions::GetActions(const TArray<UObject*>& InObjects, FToolMenuSection& Section)
{
	TArray<TWeakObjectPtr<UPaper2DPlusCharacterLayerAsset>> Assets;
	for (UObject* Object : InObjects)
	{
		if (UPaper2DPlusCharacterLayerAsset* Asset = Cast<UPaper2DPlusCharacterLayerAsset>(Object))
		{
			Assets.Add(Asset);
		}
	}
	if (Assets.IsEmpty())
	{
		return;
	}

	Section.AddMenuEntry(
		"Paper2DPlus_CharacterLayer_Validate",
		LOCTEXT("Validate", "Validate"),
		LOCTEXT("ValidateTooltip", "Run Character Layer validation and print issues to the Output Log."),
		FSlateIcon(FAppStyle::Get().GetStyleSetName(), "Icons.Help"),
		FUIAction(FExecuteAction::CreateLambda([Assets]()
		{
			int32 TotalIssues = 0;
			bool bHasErrors = false;
			for (const TWeakObjectPtr<UPaper2DPlusCharacterLayerAsset>& WeakAsset : Assets)
			{
				UPaper2DPlusCharacterLayerAsset* Asset = WeakAsset.Get();
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
					UE_LOG(LogPaper2DPlusEditor, Warning, TEXT("[%s] Character Layer %s [%s/%s]: %s"),
						*Asset->GetName(),
						*FPaper2DPlusValidationService::SeverityText(Issue.Severity).ToString(),
						*Issue.ItemIdentity,
						*Issue.Field.ToString(),
						*Issue.Message.ToString());
				}
			}

			ShowLayerValidationNotification(
				TotalIssues == 0
					? LOCTEXT("ValidationOk", "Character Layer validation passed.")
					: FText::Format(LOCTEXT("ValidationIssues", "Character Layer validation found {0} issue(s). See Output Log."), FText::AsNumber(TotalIssues)),
				TotalIssues == 0 ? SNotificationItem::CS_Success : (bHasErrors ? SNotificationItem::CS_Fail : SNotificationItem::CS_Pending));
		})));

	Section.AddMenuEntry(
		"Paper2DPlus_CharacterLayer_EnableRuntimeCustomization",
		LOCTEXT("EnableRuntimeCustomization", "Enable Runtime Customization"),
		LOCTEXT("EnableRuntimeCustomizationTooltip", "Allow this asset's selected layers to change at runtime."),
		FSlateIcon(FAppStyle::Get().GetStyleSetName(), "Icons.Settings"),
		FUIAction(FExecuteAction::CreateLambda([Assets]()
		{
			for (const TWeakObjectPtr<UPaper2DPlusCharacterLayerAsset>& WeakAsset : Assets)
			{
				FCharacterLayerAssetActions::EnableRuntimeCustomization(WeakAsset.Get());
			}
		})));

	Section.AddMenuEntry(
		"Paper2DPlus_CharacterLayer_ForceFullReimport",
		LOCTEXT("ForceFullReimport", "Force Full Reimport"),
		LOCTEXT("ForceFullReimportTooltip",
			"Replay the complete Aseprite import for every tracked source, bypassing the incremental skip gates. The recovery path when a skip looks wrong; it also works while Live .ase Auto-Reimport is disabled."),
		FSlateIcon(FAppStyle::Get().GetStyleSetName(), "Icons.Refresh"),
		FUIAction(FExecuteAction::CreateLambda([Assets]()
		{
			int32 SucceededCount = 0;
			int32 AttemptedCount = 0;
			for (const TWeakObjectPtr<UPaper2DPlusCharacterLayerAsset>& WeakAsset : Assets)
			{
				UPaper2DPlusCharacterLayerAsset* LayerAsset = WeakAsset.Get();
				if (!LayerAsset)
				{
					continue;
				}

				// Materialize the source list first: the import restamps (and can reallocate)
				// ImportedAseSources, so iterating the live array would dangle mid-loop.
				TArray<FAsepriteSourceContext> Sources = LayerAsset->ImportedAseSources;
				if (Sources.Num() == 0 && !LayerAsset->SourceAseFilePath.IsEmpty())
				{
					FAsepriteSourceContext Legacy;
					Legacy.StoredSourcePath = LayerAsset->SourceAseFilePath;
					Legacy.AssetPrefix = LayerAsset->ImportAssetPrefix;
					Legacy.DisabledTagNames = LayerAsset->ImportDisabledTagNames;
					Sources.Add(MoveTemp(Legacy));
				}

				for (const FAsepriteSourceContext& Source : Sources)
				{
					++AttemptedCount;
					FAsepriteImportCostReport Report;
					const bool bReplayed = FAsepriteImporter::ForceReimportLayerAssetSource(
						*LayerAsset, Source, /*bForceFullReimport*/ true, &Report);
					if (bReplayed)
					{
						++SucceededCount;
					}
					// A refusal (shared rows) or a dangling-reference verdict is recorded on the same audit page
					// as a replay, so the reason is one click away instead of buried in the Output Log.
					if (bReplayed || Report.DecisionLines.Num() > 0)
					{
						FAsepriteImporter::PublishIncrementalAuditPage(
							FString::Printf(TEXT("%s (%s)"), *LayerAsset->GetName(), *Source.StoredSourcePath),
							Report);
					}
				}
			}

			ShowLayerValidationNotification(
				FText::Format(
					LOCTEXT("ForceFullReimportDone", "Force Full Reimport replayed {0} of {1} tracked source(s). See the Aseprite Import message log for the decision audit. A source whose layers are shared with another source is refused: re-import every source together through the Bulk Sprite Extractor."),
					FText::AsNumber(SucceededCount), FText::AsNumber(AttemptedCount)),
				SucceededCount == AttemptedCount && AttemptedCount > 0
					? SNotificationItem::CS_Success
					: SNotificationItem::CS_Fail);
		})));
}

#undef LOCTEXT_NAMESPACE
