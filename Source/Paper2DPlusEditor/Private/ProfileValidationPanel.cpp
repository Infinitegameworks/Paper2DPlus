// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "ProfileValidationPanel.h"

#include "Runtime/Launch/Resources/Version.h"
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION == 0
#include "EditorStyleSet.h"
#ifndef FAppStyle
#define FAppStyle FEditorStyle
#endif
#else
#include "Styling/AppStyle.h"
#endif

#include "UObject/UObjectGlobals.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Paper2DPlusCharacterCatalogAsset.h"
#include "Paper2DPlusCharacterLayerAsset.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Paper2DPlusCombatProfileAsset.h"
#include "Paper2DPlusEffectProfileAsset.h"
#include "Paper2DPlusSettings.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "Paper2DPlusProfileValidationPanel"

namespace
{
	bool SeverityMatches(
		EPaper2DPlusValidationSeverity Severity,
		EPaper2DPlusValidationSeverity Expected)
	{
		return Severity == Expected;
	}

	FSlateColor SeverityColor(EPaper2DPlusValidationSeverity Severity)
	{
		switch (Severity)
		{
		case EPaper2DPlusValidationSeverity::Error:
			return FSlateColor(FLinearColor(0.95f, 0.25f, 0.2f));
		case EPaper2DPlusValidationSeverity::Warning:
			return FSlateColor(FLinearColor(1.0f, 0.65f, 0.1f));
		default:
			return FSlateColor(FLinearColor(0.35f, 0.65f, 1.0f));
		}
	}

	FString BuildIssueSearchText(const FPaper2DPlusValidationIssue& Issue)
	{
		return FString::Printf(
			TEXT("%s %s %s %s %s %s %s %s %s"),
			*FPaper2DPlusValidationService::SeverityText(Issue.Severity).ToString(),
			*Issue.Code.ToString(),
			*Issue.AssetPath.ToString(),
			*Issue.AssetType.ToString(),
			*Issue.Scope.ToString(),
			*Issue.ItemIdentity,
			*Issue.Field.ToString(),
			*Issue.Message.ToString(),
			*Issue.Remediation.ToString()).ToLower();
	}

	FText BuildIssueContextText(const FPaper2DPlusValidationIssue& Issue)
	{
		TArray<FString> Parts;
		if (Issue.AssetPath.IsValid()) Parts.Add(Issue.AssetPath.ToString());
		if (!Issue.Scope.IsNone()) Parts.Add(Issue.Scope.ToString());
		if (!Issue.ItemIdentity.IsEmpty()) Parts.Add(Issue.ItemIdentity);
		if (!Issue.Field.IsNone()) Parts.Add(Issue.Field.ToString());
		return Parts.IsEmpty()
			? FText::FromName(Issue.Code)
			: FText::FromString(FString::Join(Parts, TEXT("  ·  ")));
	}

	FText BuildIssueTooltip(const FPaper2DPlusValidationIssue& Issue)
	{
		return FText::Format(
			LOCTEXT("IssueTooltip", "{0}\nAsset: {1}\nCode: {2}\nSuggested action: {3}"),
			Issue.Message,
			FText::FromString(Issue.AssetPath.ToString()),
			FText::FromName(Issue.Code),
			Issue.Remediation.IsEmpty() ? LOCTEXT("NoRemediation", "Review the owning asset.") : Issue.Remediation);
	}

	FString NormalizeRegistryObjectPath(const FString& ObjectPath)
	{
		FString Normalized = ObjectPath.TrimStartAndEnd();
		const FSoftObjectPath SoftPath(Normalized);
		if (SoftPath.IsValid())
		{
			Normalized = SoftPath.ToString();
		}
		Normalized.ToLowerInline();
		return Normalized;
	}

	bool AssetDataIsClassOrChildOf(const FAssetData& AssetData, const UClass* ExpectedClass)
	{
		if (!ExpectedClass)
		{
			return false;
		}
		if (const UClass* AssetClass = AssetData.GetClass())
		{
			return AssetClass->IsChildOf(ExpectedClass);
		}
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
		return AssetData.AssetClass == ExpectedClass->GetFName();
#else
		return AssetData.AssetClassPath == ExpectedClass->GetClassPathName();
#endif
	}

	bool RegistryEventMatchesPath(
		const FAssetData& AssetData,
		const FString& OldObjectPath,
		const FSoftObjectPath& ExpectedPath)
	{
		if (!ExpectedPath.IsValid())
		{
			return false;
		}
		const FString Expected = NormalizeRegistryObjectPath(ExpectedPath.ToString());
		return NormalizeRegistryObjectPath(AssetData.ToSoftObjectPath().ToString()) == Expected
			|| (!OldObjectPath.IsEmpty() && NormalizeRegistryObjectPath(OldObjectPath) == Expected);
	}

	bool RegistryEventMatchesEffectPath(
		const FAssetData& AssetData,
		const FString& OldObjectPath,
		const FSoftObjectPath& EffectPath)
	{
		return RegistryEventMatchesPath(AssetData, OldObjectPath, EffectPath)
			|| RegistryEventMatchesPath(
				AssetData,
				OldObjectPath,
				UPaper2DPlusEffectProfileAsset::GetCanonicalEffectFlipbookPathNoLoad(
					EffectPath));
	}

}

void FPaper2DPlusValidationPanelModel::SetIssues(
	const TArray<FPaper2DPlusValidationIssue>& InIssues)
{
	Issues = InIssues;
	FPaper2DPlusValidationService::NormalizeAndSort(Issues);
	Refilter();
}

void FPaper2DPlusValidationPanelModel::SetSearchText(const FString& InSearchText)
{
	SearchText = InSearchText.TrimStartAndEnd().ToLower();
	Refilter();
}

void FPaper2DPlusValidationPanelModel::SetSeverityVisible(
	EPaper2DPlusValidationSeverity Severity,
	bool bVisible)
{
	switch (Severity)
	{
	case EPaper2DPlusValidationSeverity::Error:
		bShowErrors = bVisible;
		break;
	case EPaper2DPlusValidationSeverity::Warning:
		bShowWarnings = bVisible;
		break;
	default:
		bShowInfo = bVisible;
		break;
	}
	Refilter();
}

bool FPaper2DPlusValidationPanelModel::IsSeverityVisible(
	EPaper2DPlusValidationSeverity Severity) const
{
	switch (Severity)
	{
	case EPaper2DPlusValidationSeverity::Error:
		return bShowErrors;
	case EPaper2DPlusValidationSeverity::Warning:
		return bShowWarnings;
	default:
		return bShowInfo;
	}
}

void FPaper2DPlusValidationPanelModel::SelectIssue(const FString& StableKey)
{
	SelectedStableKey = FilteredIssues.ContainsByPredicate([&StableKey](const FPaper2DPlusValidationIssue& Issue)
	{
		return Issue.StableKey == StableKey;
	}) ? StableKey : FString();
}

FPaper2DPlusValidationSummary FPaper2DPlusValidationPanelModel::GetSummary(bool bValidating) const
{
	return FPaper2DPlusValidationSummary::FromIssues(Issues, bValidating);
}

void FPaper2DPlusValidationPanelModel::Refilter()
{
	FilteredIssues.Reset();
	for (const FPaper2DPlusValidationIssue& Issue : Issues)
	{
		if (PassesFilter(Issue))
		{
			FilteredIssues.Add(Issue);
		}
	}

	if (!SelectedStableKey.IsEmpty()
		&& !FilteredIssues.ContainsByPredicate([this](const FPaper2DPlusValidationIssue& Issue)
		{
			return Issue.StableKey == SelectedStableKey;
		}))
	{
		SelectedStableKey.Reset();
	}
}

bool FPaper2DPlusValidationPanelModel::PassesFilter(
	const FPaper2DPlusValidationIssue& Issue) const
{
	const bool bSeverityVisible =
		(SeverityMatches(Issue.Severity, EPaper2DPlusValidationSeverity::Info) && bShowInfo)
		|| (SeverityMatches(Issue.Severity, EPaper2DPlusValidationSeverity::Warning) && bShowWarnings)
		|| (SeverityMatches(Issue.Severity, EPaper2DPlusValidationSeverity::Error) && bShowErrors);
	if (!bSeverityVisible)
	{
		return false;
	}
	return SearchText.IsEmpty() || BuildIssueSearchText(Issue).Contains(SearchText);
}

void SProfileValidationSummary::Construct(const FArguments& InArgs)
{
	SummaryAttribute = InArgs._Summary;

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
		.Padding(FMargin(8.0f, 4.0f))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, 6.0f, 0.0f)
			[
				SNew(SImage)
				.Image(this, &SProfileValidationSummary::GetSummaryIcon)
				.ColorAndOpacity(this, &SProfileValidationSummary::GetSummaryColor)
				.ToolTipText(this, &SProfileValidationSummary::GetSummaryText)
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(this, &SProfileValidationSummary::GetSummaryText)
				.ColorAndOpacity(this, &SProfileValidationSummary::GetSummaryColor)
				.ToolTipText(this, &SProfileValidationSummary::GetSummaryText)
			]
		]
	];
}

FText SProfileValidationSummary::GetSummaryText() const
{
	return SummaryAttribute.Get(FPaper2DPlusValidationSummary()).GetStatusText();
}

FSlateColor SProfileValidationSummary::GetSummaryColor() const
{
	const FPaper2DPlusValidationSummary Summary = SummaryAttribute.Get(FPaper2DPlusValidationSummary());
	if (Summary.bValidating)
	{
		return FSlateColor::UseForeground();
	}
	if (Summary.HasErrors())
	{
		return SeverityColor(EPaper2DPlusValidationSeverity::Error);
	}
	if (Summary.HasWarnings())
	{
		return SeverityColor(EPaper2DPlusValidationSeverity::Warning);
	}
	return FSlateColor::UseForeground();
}

const FSlateBrush* SProfileValidationSummary::GetSummaryIcon() const
{
	return FAppStyle::Get().GetBrush(SummaryAttribute.Get(FPaper2DPlusValidationSummary()).GetStatusIconName());
}

void SProfileValidationPanel::Construct(const FArguments& InArgs)
{
	AssetAttribute = InArgs._Asset;
	OnIssueActivated = InArgs._OnIssueActivated;
	OnCustomValidationRun = InArgs._OnCustomValidationRun;
	RunActionText = InArgs._RunActionText.IsEmpty()
		? LOCTEXT("DefaultRunAction", "Validate")
		: InArgs._RunActionText;

	if (InArgs._RefreshOnObservedChanges)
	{
		PropertyChangedHandle = FCoreUObjectDelegates::OnObjectPropertyChanged.AddSP(
			this,
			&SProfileValidationPanel::HandleObjectPropertyChanged);
		ObjectModifiedHandle = FCoreUObjectDelegates::OnObjectModified.AddSP(
			this,
			&SProfileValidationPanel::HandleObjectModified);
		ObjectTransactedHandle = FCoreUObjectDelegates::OnObjectTransacted.AddSP(
			this,
			&SProfileValidationPanel::HandleObjectTransacted);
		AdaptersChangedHandle = FPaper2DPlusValidationService::Get().OnAdaptersChanged().AddSP(
			this,
			&SProfileValidationPanel::HandleAdaptersChanged);
		FAssetRegistryModule& RegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(
			AssetRegistryConstants::ModuleName);
		BoundAssetRegistry = &RegistryModule.Get();
		AssetAddedHandle = BoundAssetRegistry->OnAssetAdded().AddSP(
			this, &SProfileValidationPanel::HandleAssetRegistryChanged);
		AssetRemovedHandle = BoundAssetRegistry->OnAssetRemoved().AddSP(
			this, &SProfileValidationPanel::HandleAssetRegistryChanged);
		AssetUpdatedHandle = BoundAssetRegistry->OnAssetUpdated().AddSP(
			this, &SProfileValidationPanel::HandleAssetRegistryChanged);
		AssetRenamedHandle = BoundAssetRegistry->OnAssetRenamed().AddSP(
			this, &SProfileValidationPanel::HandleAssetRegistryRenamed);
		AssetFilesLoadedHandle = BoundAssetRegistry->OnFilesLoaded().AddSP(
			this, &SProfileValidationPanel::HandleAssetRegistryFilesLoaded);
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
		AssetKnownGathersCompleteHandle = BoundAssetRegistry->OnKnownGathersComplete().AddSP(
			this, &SProfileValidationPanel::HandleAssetRegistryFilesLoaded);
#endif
	}

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
		.Padding(8.0f)
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					SNew(SOverlay)
					+ SOverlay::Slot()
					[
						SNew(SBorder)
						.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
						.Padding(FMargin(8.0f, 4.0f))
						.Visibility_Lambda([this]() { return bHasRunValidation ? EVisibility::Collapsed : EVisibility::Visible; })
						[
							SNew(STextBlock)
							.Text(LOCTEXT("NotCheckedStatus", "Not checked yet"))
						]
					]
					+ SOverlay::Slot()
					[
						SNew(SProfileValidationSummary)
						.Summary(this, &SProfileValidationPanel::GetSummary)
						.Visibility_Lambda([this]() { return bHasRunValidation ? EVisibility::Visible : EVisibility::Collapsed; })
					]
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(6.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(SButton)
					.Text(RunActionText)
					.ToolTipText(LOCTEXT("RefreshTooltip", "Run validation now. This action does not modify the asset."))
					.OnClicked(this, &SProfileValidationPanel::HandleRefreshClicked)
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 6.0f)
			[
				SNew(SSearchBox)
				.HintText(LOCTEXT("SearchHint", "Filter by message, item, field, code, or asset…"))
				.OnTextChanged(this, &SProfileValidationPanel::HandleSearchChanged)
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 6.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0.0f, 0.0f, 10.0f, 0.0f)
				[
					SNew(SCheckBox)
					.IsChecked_Lambda([this]() { return Model.IsSeverityVisible(EPaper2DPlusValidationSeverity::Error) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
					.OnCheckStateChanged_Lambda([this](ECheckBoxState State) { HandleSeverityChanged(EPaper2DPlusValidationSeverity::Error, State); })
					[
						SNew(STextBlock).Text(LOCTEXT("ErrorsFilter", "Errors"))
					]
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0.0f, 0.0f, 10.0f, 0.0f)
				[
					SNew(SCheckBox)
					.IsChecked_Lambda([this]() { return Model.IsSeverityVisible(EPaper2DPlusValidationSeverity::Warning) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
					.OnCheckStateChanged_Lambda([this](ECheckBoxState State) { HandleSeverityChanged(EPaper2DPlusValidationSeverity::Warning, State); })
					[
						SNew(STextBlock).Text(LOCTEXT("WarningsFilter", "Warnings"))
					]
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SCheckBox)
					.IsChecked_Lambda([this]() { return Model.IsSeverityVisible(EPaper2DPlusValidationSeverity::Info) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
					.OnCheckStateChanged_Lambda([this](ECheckBoxState State) { HandleSeverityChanged(EPaper2DPlusValidationSeverity::Info, State); })
					[
						SNew(STextBlock).Text(LOCTEXT("InfoFilter", "Info"))
					]
				]
			]

			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			[
				SNew(SOverlay)
				+ SOverlay::Slot()
				[
					SAssignNew(IssueList, SListView<FIssuePtr>)
					.ListItemsSource(&VisibleRows)
					.SelectionMode(ESelectionMode::Single)
					.OnGenerateRow(this, &SProfileValidationPanel::GenerateIssueRow)
					.OnSelectionChanged(this, &SProfileValidationPanel::HandleSelectionChanged)
					.OnMouseButtonDoubleClick(this, &SProfileValidationPanel::HandleIssueDoubleClicked)
					.Visibility(this, &SProfileValidationPanel::GetListVisibility)
				]
				+ SOverlay::Slot()
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				.Padding(20.0f)
				[
					SNew(STextBlock)
					.Text(this, &SProfileValidationPanel::GetEmptyStateText)
					.Justification(ETextJustify::Center)
					.AutoWrapText(true)
					.Visibility(this, &SProfileValidationPanel::GetEmptyStateVisibility)
				]
			]
		]
	];

	if (InArgs._RunInitially)
	{
		RunValidationNow();
	}
}

SProfileValidationPanel::~SProfileValidationPanel()
{
	if (PropertyChangedHandle.IsValid())
	{
		FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(PropertyChangedHandle);
	}
	if (ObjectTransactedHandle.IsValid())
	{
		FCoreUObjectDelegates::OnObjectTransacted.Remove(ObjectTransactedHandle);
	}
	if (ObjectModifiedHandle.IsValid())
	{
		FCoreUObjectDelegates::OnObjectModified.Remove(ObjectModifiedHandle);
	}
	if (AdaptersChangedHandle.IsValid())
	{
		FPaper2DPlusValidationService::Get().OnAdaptersChanged().Remove(AdaptersChangedHandle);
	}
	if (BoundAssetRegistry)
	{
		BoundAssetRegistry->OnAssetAdded().Remove(AssetAddedHandle);
		BoundAssetRegistry->OnAssetRemoved().Remove(AssetRemovedHandle);
		BoundAssetRegistry->OnAssetUpdated().Remove(AssetUpdatedHandle);
		BoundAssetRegistry->OnAssetRenamed().Remove(AssetRenamedHandle);
		BoundAssetRegistry->OnFilesLoaded().Remove(AssetFilesLoadedHandle);
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
		BoundAssetRegistry->OnKnownGathersComplete().Remove(AssetKnownGathersCompleteHandle);
#endif
		BoundAssetRegistry = nullptr;
	}
}

void SProfileValidationPanel::RunValidationNow()
{
	bHasRunValidation = true;
	bRefreshPending = false;
	bValidating = true;
	++ValidationRunCount;

	TArray<FPaper2DPlusValidationIssue> Issues;
	const bool bCustomRunHandled = OnCustomValidationRun.IsBound()
		&& OnCustomValidationRun.Execute(Issues);
	if (bCustomRunHandled)
	{
		bHasAdapter = true;
	}
	else
	{
		// A host can decline (for example, when its deep check is not authoritative). Discard any
		// partial host output so the ordinary shared-service projection remains the sole result.
		Issues.Reset();
		UObject* Asset = AssetAttribute.Get(nullptr);
		bHasAdapter = FPaper2DPlusValidationService::Get().ValidateObject(Asset, Issues);
	}
	Model.SetIssues(Issues);
	bValidating = false;
	RebuildVisibleRows();
}

void SProfileValidationPanel::RequestRefresh()
{
	if (bRefreshPending)
	{
		return;
	}
	bRefreshPending = true;
	RegisterActiveTimer(
		0.0f,
		FWidgetActiveTimerDelegate::CreateSP(this, &SProfileValidationPanel::HandleDeferredRefresh));
}

#if WITH_DEV_AUTOMATION_TESTS
void SProfileValidationPanel::FlushPendingRefreshForTests()
{
	if (bRefreshPending)
	{
		bRefreshPending = false;
		RunValidationNow();
	}
}

bool SProfileValidationPanel::ActivateIssueForTests(const FPaper2DPlusValidationIssue& Issue)
{
	return ActivateIssue(Issue);
}

bool SProfileValidationPanel::NotifyAssetRegistryChangedForTests(
	const FAssetData& AssetData,
	const FString& OldObjectPath)
{
	const bool bRelevant = ShouldRefreshForAssetRegistryEvent(AssetData, OldObjectPath);
	if (bRelevant)
	{
		RequestRefresh();
	}
	return bRelevant;
}

void SProfileValidationPanel::NotifyAssetRegistryFilesLoadedForTests()
{
	HandleAssetRegistryFilesLoaded();
}
#endif

TSharedRef<ITableRow> SProfileValidationPanel::GenerateIssueRow(
	FIssuePtr Issue,
	const TSharedRef<STableViewBase>& OwnerTable)
{
	check(Issue.IsValid());
	const bool bCanActivate = Issue->CanActivate() && OnIssueActivated.IsBound();

	return SNew(STableRow<FIssuePtr>, OwnerTable)
		.ToolTipText(BuildIssueTooltip(*Issue))
		.Padding(FMargin(4.0f, 3.0f))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Top)
			.Padding(0.0f, 2.0f, 4.0f, 0.0f)
			[
				SNew(SImage)
				.Image(FAppStyle::Get().GetBrush(FPaper2DPlusValidationService::SeverityIconName(Issue->Severity)))
				.ToolTipText(FPaper2DPlusValidationService::SeverityText(Issue->Severity))
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Top)
			.Padding(0.0f, 2.0f, 10.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(FPaper2DPlusValidationService::SeverityText(Issue->Severity))
				.ColorAndOpacity(SeverityColor(Issue->Severity))
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(STextBlock)
					.Text(Issue->Message)
					.AutoWrapText(true)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 2.0f, 0.0f, 0.0f)
				[
					SNew(STextBlock)
					.Text(BuildIssueContextText(*Issue))
					.Font(FAppStyle::GetFontStyle("SmallFont"))
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
					.AutoWrapText(true)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 2.0f, 0.0f, 0.0f)
				[
					SNew(STextBlock)
					.Text(Issue->Remediation)
					.Font(FAppStyle::GetFontStyle("SmallFont"))
					.AutoWrapText(true)
					.Visibility(Issue->Remediation.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible)
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(8.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("OpenIssueTarget", "Open"))
				.ToolTipText(LOCTEXT("OpenIssueTargetTooltip", "Open the owning asset and focus this issue's tool target."))
				.Visibility(bCanActivate ? EVisibility::Visible : EVisibility::Collapsed)
				.OnClicked(this, &SProfileValidationPanel::HandleActivateClicked, Issue)
			]
		];
}

void SProfileValidationPanel::RebuildVisibleRows()
{
	const FString PreviousSelection = Model.GetSelectedStableKey();
	VisibleRows.Reset();
	for (const FPaper2DPlusValidationIssue& Issue : Model.GetFilteredIssues())
	{
		VisibleRows.Add(MakeShared<FPaper2DPlusValidationIssue>(Issue));
	}

	if (IssueList.IsValid())
	{
		IssueList->RequestListRefresh();
		IssueList->ClearSelection();
		if (!PreviousSelection.IsEmpty())
		{
			const FIssuePtr* Match = VisibleRows.FindByPredicate([&PreviousSelection](const FIssuePtr& Row)
			{
				return Row.IsValid() && Row->StableKey == PreviousSelection;
			});
			if (Match)
			{
				IssueList->SetSelection(*Match, ESelectInfo::Direct);
			}
		}
	}
}

void SProfileValidationPanel::HandleSearchChanged(const FText& NewText)
{
	Model.SetSearchText(NewText.ToString());
	RebuildVisibleRows();
}

void SProfileValidationPanel::HandleSeverityChanged(
	EPaper2DPlusValidationSeverity Severity,
	ECheckBoxState NewState)
{
	Model.SetSeverityVisible(Severity, NewState == ECheckBoxState::Checked);
	RebuildVisibleRows();
}

void SProfileValidationPanel::HandleSelectionChanged(FIssuePtr Issue, ESelectInfo::Type /*SelectInfo*/)
{
	Model.SelectIssue(Issue.IsValid() ? Issue->StableKey : FString());
}

void SProfileValidationPanel::HandleIssueDoubleClicked(FIssuePtr Issue)
{
	if (Issue.IsValid())
	{
		ActivateIssue(*Issue);
	}
}

FReply SProfileValidationPanel::HandleRefreshClicked()
{
	RunValidationNow();
	return FReply::Handled();
}

FReply SProfileValidationPanel::HandleActivateClicked(FIssuePtr Issue)
{
	if (Issue.IsValid() && ActivateIssue(*Issue))
	{
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

bool SProfileValidationPanel::ActivateIssue(const FPaper2DPlusValidationIssue& Issue)
{
	if (!Issue.CanActivate() || !OnIssueActivated.IsBound())
	{
		return false;
	}
	OnIssueActivated.Execute(Issue);
	return true;
}

EActiveTimerReturnType SProfileValidationPanel::HandleDeferredRefresh(
	double /*CurrentTime*/,
	float /*DeltaTime*/)
{
	if (!bRefreshPending)
	{
		return EActiveTimerReturnType::Stop;
	}
	bRefreshPending = false;
	RunValidationNow();
	return EActiveTimerReturnType::Stop;
}

void SProfileValidationPanel::HandleObjectPropertyChanged(
	UObject* Object,
	FPropertyChangedEvent& /*Event*/)
{
	if (ObservesObject(Object))
	{
		RequestRefresh();
	}
}

void SProfileValidationPanel::HandleObjectModified(UObject* Object)
{
	if (ObservesObject(Object))
	{
		RequestRefresh();
	}
}

void SProfileValidationPanel::HandleObjectTransacted(
	UObject* Object,
	const FTransactionObjectEvent& /*Event*/)
{
	if (ObservesObject(Object))
	{
		RequestRefresh();
	}
}

void SProfileValidationPanel::HandleAdaptersChanged()
{
	RequestRefresh();
}

void SProfileValidationPanel::HandleAssetRegistryChanged(const FAssetData& AssetData)
{
	if (ShouldRefreshForAssetRegistryEvent(AssetData, FString()))
	{
		RequestRefresh();
	}
}

void SProfileValidationPanel::HandleAssetRegistryRenamed(
	const FAssetData& AssetData,
	const FString& OldObjectPath)
{
	if (ShouldRefreshForAssetRegistryEvent(AssetData, OldObjectPath))
	{
		RequestRefresh();
	}
}

void SProfileValidationPanel::HandleAssetRegistryFilesLoaded()
{
	// Unknown is intentionally not reported as missing while discovery is active. Re-run once the
	// Registry has complete knowledge so a missing or wrong-class soft target cannot remain cached clean.
	RequestRefresh();
}

bool SProfileValidationPanel::ShouldRefreshForAssetRegistryEvent(
	const FAssetData& AssetData,
	const FString& OldObjectPath) const
{
	const UObject* Asset = AssetAttribute.Get(nullptr);
	if (!Asset || !AssetData.IsValid()
		|| !FPaper2DPlusValidationService::Get().HasAdapterFor(Asset))
	{
		return false;
	}

	// Every adapter observes its own registry lifecycle. This also keeps late/custom adapters correct without
	// turning every registry notification into a validation run.
	if (RegistryEventMatchesPath(AssetData, OldObjectPath, FSoftObjectPath(Asset)))
	{
		return true;
	}

	if (Asset->IsA<UPaper2DPlusCharacterProfileAsset>())
	{
		const UPaper2DPlusSettings* Settings = UPaper2DPlusSettings::Get();
		if (!Settings)
		{
			return false;
		}
		if (AssetDataIsClassOrChildOf(AssetData, UPaper2DPlusCharacterCatalogAsset::StaticClass())
			&& RegistryEventMatchesPath(
				AssetData, OldObjectPath, Settings->DefaultCharacterCatalog.ToSoftObjectPath()))
		{
			return true;
		}
		return false;
	}

	if (const UPaper2DPlusCharacterLayerAsset* Layer = Cast<UPaper2DPlusCharacterLayerAsset>(Asset))
	{
		return AssetDataIsClassOrChildOf(AssetData, UPaper2DPlusCharacterProfileAsset::StaticClass())
			&& RegistryEventMatchesPath(AssetData, OldObjectPath, Layer->BaseProfile.ToSoftObjectPath());
	}

	if (const UPaper2DPlusCombatProfileAsset* Combat = Cast<UPaper2DPlusCombatProfileAsset>(Asset))
	{
		return AssetDataIsClassOrChildOf(AssetData, UPaper2DPlusCharacterProfileAsset::StaticClass())
			&& RegistryEventMatchesPath(
				AssetData, OldObjectPath, FSoftObjectPath(Combat->CharacterProfile.Get()));
	}

	if (const UPaper2DPlusEffectProfileAsset* Effect = Cast<UPaper2DPlusEffectProfileAsset>(Asset))
	{
		for (const FPaper2DPlusEffectProfileEntry& Entry : Effect->Effects)
		{
			if (RegistryEventMatchesEffectPath(
				AssetData, OldObjectPath, Entry.GetEffectFlipbookPath()))
			{
				return true;
			}
		}
		return false;
	}

	if (const UPaper2DPlusCharacterCatalogAsset* Catalog = Cast<UPaper2DPlusCharacterCatalogAsset>(Asset))
	{
		const bool bSupportedCompanion =
			AssetDataIsClassOrChildOf(AssetData, UPaper2DPlusCharacterProfileAsset::StaticClass())
			|| AssetDataIsClassOrChildOf(AssetData, UPaper2DPlusCharacterLayerAsset::StaticClass())
			|| AssetDataIsClassOrChildOf(AssetData, UPaper2DPlusEffectProfileAsset::StaticClass())
			|| AssetDataIsClassOrChildOf(AssetData, UPaper2DPlusCombatProfileAsset::StaticClass());
		if (!bSupportedCompanion)
		{
			return false;
		}
		for (const FPaper2DPlusCharacterCatalogEntry& Entry : Catalog->Entries)
		{
			if (RegistryEventMatchesPath(AssetData, OldObjectPath, Entry.CharacterProfile.ToSoftObjectPath())
				|| RegistryEventMatchesPath(AssetData, OldObjectPath, Entry.LayerProfile.ToSoftObjectPath())
				|| RegistryEventMatchesPath(AssetData, OldObjectPath, Entry.EffectProfile.ToSoftObjectPath())
				|| RegistryEventMatchesPath(AssetData, OldObjectPath, Entry.CombatProfile.ToSoftObjectPath()))
			{
				return true;
			}
		}
	}

	return false;
}

bool SProfileValidationPanel::ObservesObject(const UObject* Object) const
{
	const UObject* Asset = AssetAttribute.Get(nullptr);
	if (!Asset || !Object)
	{
		return false;
	}
	if (Object == Asset || Object->IsIn(Asset))
	{
		return true;
	}
	if (Object->IsA<UPaper2DPlusSettings>())
	{
		return Asset->IsA<UPaper2DPlusCharacterProfileAsset>()
			|| Asset->IsA<UPaper2DPlusCharacterCatalogAsset>();
	}
	if (Asset->IsA<UPaper2DPlusCharacterProfileAsset>())
	{
		// Character validation depends on the configured Catalog, not its optional companion profiles.
		return Object->IsA<UPaper2DPlusCharacterCatalogAsset>();
	}
	if (const UPaper2DPlusCharacterLayerAsset* Layer = Cast<UPaper2DPlusCharacterLayerAsset>(Asset))
	{
		const UObject* BaseProfile = Layer->BaseProfile.Get();
		return BaseProfile && (Object == BaseProfile || Object->IsIn(BaseProfile));
	}
	if (const UPaper2DPlusCombatProfileAsset* Combat = Cast<UPaper2DPlusCombatProfileAsset>(Asset))
	{
		const UObject* Character = Combat->CharacterProfile.Get();
		return Character && (Object == Character || Object->IsIn(Character));
	}
	if (Asset->IsA<UPaper2DPlusCharacterCatalogAsset>())
	{
		// Catalog discovery/completion/relationships can change when any companion profile changes.
		return Object->IsA<UPaper2DPlusCharacterProfileAsset>()
			|| Object->IsA<UPaper2DPlusCharacterLayerAsset>()
			|| Object->IsA<UPaper2DPlusEffectProfileAsset>()
			|| Object->IsA<UPaper2DPlusCombatProfileAsset>();
	}
	return false;
}

FText SProfileValidationPanel::GetEmptyStateText() const
{
	if (bValidating)
	{
		return LOCTEXT("ValidatingEmptyState", "Validating this profile…");
	}
	if (!AssetAttribute.Get(nullptr))
	{
		return LOCTEXT("NoAssetEmptyState", "Select a profile to see its validation issues.");
	}
	if (!bHasRunValidation)
	{
		return FText::Format(
			LOCTEXT("NotCheckedEmptyState", "Choose {0} to check this asset for issues."),
			RunActionText);
	}
	if (!bHasAdapter)
	{
		return LOCTEXT("NoAdapterEmptyState", "No validation adapter is registered for this asset type.");
	}
	if (!Model.GetIssues().IsEmpty() && Model.GetFilteredIssues().IsEmpty())
	{
		return LOCTEXT("NoMatchesEmptyState", "No issues match the current text and severity filters. The asset selection was not changed.");
	}
	return LOCTEXT("CleanEmptyState", "No validation issues were found.");
}

FPaper2DPlusValidationSummary SProfileValidationPanel::GetSummary() const
{
	return Model.GetSummary(bValidating);
}

EVisibility SProfileValidationPanel::GetEmptyStateVisibility() const
{
	return VisibleRows.IsEmpty() ? EVisibility::Visible : EVisibility::Collapsed;
}

EVisibility SProfileValidationPanel::GetListVisibility() const
{
	return VisibleRows.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible;
}

#undef LOCTEXT_NAMESPACE
