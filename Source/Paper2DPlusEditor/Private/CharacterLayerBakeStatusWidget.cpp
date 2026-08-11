// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "CharacterLayerBakeStatusWidget.h"

#include "CharacterLayerBakeCoordinator.h"

#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
#include "EditorStyleSet.h"
#ifndef FAppStyle
#define FAppStyle FEditorStyle
#endif
#else
#include "Styling/AppStyle.h"
#endif

#define LOCTEXT_NAMESPACE "CharacterLayerBakeStatusWidget"

void SCharacterLayerBakeStatusWidget::Construct(const FArguments& InArgs)
{
	Presentation = InArgs._Presentation;
	OnBakeCurrent = InArgs._OnBakeCurrent;
	OnBakeAll = InArgs._OnBakeAll;
	OnSaveBakeSet = InArgs._OnSaveBakeSet;
	OnOverwrite = InArgs._OnOverwrite;
	OnRepair = InArgs._OnRepair;
	OnRebase = InArgs._OnRebase;
	OnDetach = InArgs._OnDetach;
	OnEnableRuntimeCustomization = InArgs._OnEnableRuntimeCustomization;

	ChildSlot
	[
		SNew(SComboButton)
		.ButtonStyle(FAppStyle::Get(), "FlatButton.Success")
		.ContentPadding(FMargin(12.0f, 3.0f))
		.HasDownArrow(false)
		.MenuPlacement(MenuPlacement_AboveAnchor)
		.ToolTipText(LOCTEXT("BakeButtonTip", "Open bake status, publishing details, and available bake actions."))
		.AccessibleText(LOCTEXT("BakeButtonAccessible", "Bake and publishing actions"))
		.IsEnabled(this, &SCharacterLayerBakeStatusWidget::AreActionsEnabled)
		.OnGetMenuContent(this, &SCharacterLayerBakeStatusWidget::BuildDetailsMenu)
		.ButtonContent()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("BakeButton", "Bake"))
			.Justification(ETextJustify::Center)
			.Font(FAppStyle::GetFontStyle("NormalFontBold"))
		]
	];
}

void SCharacterLayerBakeStatusWidget::SetPresentation(
	const FCharacterLayerBakeStatusPresentation& InPresentation)
{
	Presentation = InPresentation;
	Invalidate(EInvalidateWidgetReason::Layout);
}

SCharacterLayerBakeStatusWidget::EAction SCharacterLayerBakeStatusWidget::ResolvePrimaryAction() const
{
	// Recovery and explicit conflict resolution take precedence over routine publishing. Delivery-mode
	// switches and detach remain deliberate secondary actions even when they are the only actions present.
	if (Presentation.bShowRepair) return EAction::Repair;
	if (Presentation.bShowRebase) return EAction::Rebase;
	if (Presentation.bShowOverwrite) return EAction::Overwrite;
	if (Presentation.bShowSaveBakeSet) return EAction::SaveBakeSet;
	if (Presentation.bShowBakeAll) return EAction::BakeAll;
	if (Presentation.bShowBakeCurrent) return EAction::BakeCurrent;
	return EAction::None;
}

FText SCharacterLayerBakeStatusWidget::GetPrimaryActionText() const
{
	switch (ResolvePrimaryAction())
	{
	case EAction::BakeCurrent: return LOCTEXT("BakeCurrent", "Bake Current");
	case EAction::BakeAll: return LOCTEXT("BakeAll", "Bake All");
	case EAction::SaveBakeSet: return LOCTEXT("SaveBakeSet", "Save Bake Set");
	case EAction::Overwrite: return LOCTEXT("Overwrite", "Overwrite from Layer Source");
	case EAction::Repair: return LOCTEXT("Repair", "Repair");
	case EAction::Rebase: return LOCTEXT("Rebase", "Rebase Registration");
	default: return FText::GetEmpty();
	}
}

FText SCharacterLayerBakeStatusWidget::GetPrimaryActionTooltip() const
{
	switch (ResolvePrimaryAction())
	{
	case EAction::BakeCurrent:
		return LOCTEXT("BakeCurrentTip", "Recompile the selected animation from the Default Appearance. Preview eyes are ignored.");
	case EAction::BakeAll:
		return LOCTEXT("BakeAllTip", "Recompile every registered animation from persisted Layer source. This does not save packages.");
	case EAction::SaveBakeSet:
		return LOCTEXT("SaveBakeSetTip", "Save exactly the latest bake manifest package set, with the Layer asset checkpoint saved last.");
	case EAction::Overwrite:
		return LOCTEXT("OverwriteTip", "Explicitly replace owner-matching managed output that differs from the manifest.");
	case EAction::Repair:
		return LOCTEXT("RepairTip", "Recovery Required only: rebuild and verify the complete attached bake set.");
	case EAction::Rebase:
		return LOCTEXT("RebaseTip", "Explicitly accept intentional Character Profile animation topology changes, then rebuild all current animations.");
	default:
		return FText::GetEmpty();
	}
}

TSharedRef<SWidget> SCharacterLayerBakeStatusWidget::BuildDetailsMenu()
{
	FMenuBuilder Menu(true, nullptr);
	Menu.BeginSection(FName(TEXT("LayerPublishDetails")), LOCTEXT("PublishDetailsSection", "Publishing Details"));
	Menu.AddWidget(
		SNew(SBox)
		.WidthOverride(500.0f)
		.Padding(FMargin(8.0f, 4.0f))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock)
				.Text(this, &SCharacterLayerBakeStatusWidget::GetStatusText)
				.ColorAndOpacity(this, &SCharacterLayerBakeStatusWidget::GetStatusColor)
				.Font(FAppStyle::GetFontStyle("HeadingExtraSmall"))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 1.0f, 0.0f, 5.0f)
			[
				SNew(STextBlock)
				.Text(this, &SCharacterLayerBakeStatusWidget::GetSummaryText)
				.AutoWrapText(true)
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock)
				.Text(this, &SCharacterLayerBakeStatusWidget::GetDeliveryText)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f, 0.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(this, &SCharacterLayerBakeStatusWidget::GetSelectionText)
				.AutoWrapText(true)
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 5.0f, 0.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(this, &SCharacterLayerBakeStatusWidget::GetDiagnosticsText)
				.AutoWrapText(true)
				.Visibility(this, &SCharacterLayerBakeStatusWidget::GetDiagnosticsVisibility)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 3.0f, 0.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(this, &SCharacterLayerBakeStatusWidget::GetLastReportText)
				.AutoWrapText(true)
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				.Visibility(this, &SCharacterLayerBakeStatusWidget::GetLastReportVisibility)
			]
		],
		FText::GetEmpty());
	Menu.EndSection();

	Menu.BeginSection(FName(TEXT("LayerPublishActions")), LOCTEXT("PublishActionsSection", "Actions"));
	const EAction Primary = ResolvePrimaryAction();
	if (Primary != EAction::None)
	{
		Menu.AddMenuEntry(
			GetPrimaryActionText(),
			GetPrimaryActionTooltip(),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP(this, &SCharacterLayerBakeStatusWidget::ExecuteMenuAction, Primary),
				FCanExecuteAction::CreateSP(this, &SCharacterLayerBakeStatusWidget::AreActionsEnabled)));
	}
	const auto AddAction = [this, &Menu, Primary](
		bool bShow,
		EAction Action,
		const FText& Label,
		const FText& Tooltip)
	{
		if (!bShow || Action == Primary) return;
		Menu.AddMenuEntry(
			Label,
			Tooltip,
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP(this, &SCharacterLayerBakeStatusWidget::ExecuteMenuAction, Action),
				FCanExecuteAction::CreateSP(this, &SCharacterLayerBakeStatusWidget::AreActionsEnabled)));
	};
	AddAction(Presentation.bShowBakeCurrent, EAction::BakeCurrent,
		LOCTEXT("BakeCurrent", "Bake Current"),
		LOCTEXT("BakeCurrentTip", "Recompile the selected animation from the Default Appearance. Preview eyes are ignored."));
	AddAction(Presentation.bShowBakeAll, EAction::BakeAll,
		LOCTEXT("BakeAll", "Bake All"),
		LOCTEXT("BakeAllTip", "Recompile every registered animation from persisted Layer source. This does not save packages."));
	AddAction(Presentation.bShowSaveBakeSet, EAction::SaveBakeSet,
		LOCTEXT("SaveBakeSet", "Save Bake Set"),
		LOCTEXT("SaveBakeSetTip", "Save exactly the latest bake manifest package set, with the Layer asset checkpoint saved last."));
	AddAction(Presentation.bShowOverwrite, EAction::Overwrite,
		LOCTEXT("Overwrite", "Overwrite from Layer Source"),
		LOCTEXT("OverwriteTip", "Explicitly replace owner-matching managed output that differs from the manifest."));
	AddAction(Presentation.bShowRepair, EAction::Repair,
		LOCTEXT("Repair", "Repair"),
		LOCTEXT("RepairTip", "Recovery Required only: rebuild and verify the complete attached bake set."));
	AddAction(Presentation.bShowRebase, EAction::Rebase,
		LOCTEXT("Rebase", "Rebase Registration"),
		LOCTEXT("RebaseTip", "Explicitly accept intentional Character Profile animation topology changes, then rebuild all current animations."));
	AddAction(Presentation.bShowDetach, EAction::Detach,
		LOCTEXT("Detach", "Advanced Detach"),
		LOCTEXT("DetachTip", "Freeze the current canonical output in place and release only this bake set's matching management claims. This never reconstructs layers."));
	AddAction(Presentation.bShowEnableRuntimeCustomization, EAction::EnableRuntimeCustomization,
		LOCTEXT("EnableRuntimeCustomization", "Enable Runtime Customization"),
		LOCTEXT("EnableRuntimeCustomizationTip", "Allow the selected layer set to change at runtime."));
	Menu.EndSection();
	return Menu.MakeWidget();
}

void SCharacterLayerBakeStatusWidget::ExecuteMenuAction(EAction Action)
{
	switch (Action)
	{
	case EAction::BakeCurrent: Execute(OnBakeCurrent); break;
	case EAction::BakeAll: Execute(OnBakeAll); break;
	case EAction::SaveBakeSet: Execute(OnSaveBakeSet); break;
	case EAction::Overwrite: Execute(OnOverwrite); break;
	case EAction::Repair: Execute(OnRepair); break;
	case EAction::Rebase: Execute(OnRebase); break;
	case EAction::Detach: Execute(OnDetach); break;
	case EAction::EnableRuntimeCustomization: Execute(OnEnableRuntimeCustomization); break;
	default: break;
	}
}

FText SCharacterLayerBakeStatusWidget::BuildDiagnosticsText(
	const FCharacterLayerBakeStatusSnapshot& Status)
{
	TArray<FString> Lines;
	for (const FString& Blocker : Status.Blockers)
	{
		Lines.Add(FString::Printf(TEXT("Blocked: %s"), *Blocker));
	}
	for (const FString& Warning : Status.Warnings)
	{
		Lines.Add(FString::Printf(TEXT("Warning: %s"), *Warning));
	}
	const auto AppendPaths = [&Lines](const TCHAR* Heading, const TArray<FSoftObjectPath>& Paths)
	{
		if (Paths.IsEmpty()) return;
		Lines.Add(Heading);
		for (const FSoftObjectPath& Path : Paths)
		{
			Lines.Add(FString::Printf(TEXT("  %s"), *Path.ToString()));
		}
	};
	AppendPaths(TEXT("Dirty packages still needing Save Bake Set:"), Status.DirtyPackages);
	AppendPaths(TEXT("Missing bake packages:"), Status.MissingPackages);
	AppendPaths(TEXT("Packages that failed the latest Save Bake Set:"), Status.FailedSavePackages);
	AppendPaths(TEXT("Latest Save Bake Set succeeded for:"), Status.LastOperation.SavedPackages);
	AppendPaths(TEXT("Latest Save Bake Set failed for:"), Status.LastOperation.FailedPackages);
	return FText::FromString(FString::Join(Lines, TEXT("\n")));
}

EVisibility SCharacterLayerBakeStatusWidget::GetDiagnosticsVisibility() const { return Presentation.DiagnosticsText.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; }
EVisibility SCharacterLayerBakeStatusWidget::GetLastReportVisibility() const { return Presentation.LastReportText.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; }

void SCharacterLayerBakeStatusWidget::Execute(const FSimpleDelegate& Delegate)
{
	if (!Presentation.bBusy && Delegate.IsBound()) Delegate.Execute();
}

#undef LOCTEXT_NAMESPACE
