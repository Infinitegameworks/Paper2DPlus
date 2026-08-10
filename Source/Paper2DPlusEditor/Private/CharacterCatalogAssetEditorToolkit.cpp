// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "CharacterCatalogAssetEditorToolkit.h"

#include "CharacterCatalogDetailsPanel.h"
#include "CharacterCatalogEditorModel.h"
#include "CharacterCatalogRosterPanel.h"
#include "Editor.h"
#include "IDetailsView.h"
#include "Paper2DPlusCharacterCatalogAsset.h"
#include "Paper2DPlusValidationService.h"
#include "ProfileValidationPanel.h"
#include "PropertyEditorModule.h"
#include "Misc/MessageDialog.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
#include "EditorStyleSet.h"
#ifndef FAppStyle
#define FAppStyle FEditorStyle
#define GetAppStyleSetName GetStyleSetName
#endif
#else
#include "Styling/AppStyle.h"
#endif

#define LOCTEXT_NAMESPACE "Paper2DPlusCharacterCatalogEditor"

const FName FCharacterCatalogAssetEditorToolkit::RosterTabId(TEXT("CharacterCatalogEditor_Roster"));
const FName FCharacterCatalogAssetEditorToolkit::DetailsTabId(TEXT("CharacterCatalogEditor_Details"));
const FName FCharacterCatalogAssetEditorToolkit::WarningsTabId(TEXT("CharacterCatalogEditor_Warnings"));
const FName FCharacterCatalogAssetEditorToolkit::AdvancedTabId(TEXT("CharacterCatalogEditor_Advanced"));

FCharacterCatalogAssetEditorToolkit::~FCharacterCatalogAssetEditorToolkit()
{
	if (AdvancedDetailsView.IsValid() && AdvancedPropertiesChangedHandle.IsValid())
	{
		AdvancedDetailsView->OnFinishedChangingProperties().Remove(AdvancedPropertiesChangedHandle);
	}
	if (Model.IsValid())
	{
		if (ModelChangedHandle.IsValid()) Model->OnModelChanged().Remove(ModelChangedHandle);
		WarningsPanel.Reset();
		DetailsPanel.Reset();
		RosterPanel.Reset();
		Model->Shutdown();
	}
	if (GEditor) GEditor->UnregisterForUndo(this);
}

void FCharacterCatalogAssetEditorToolkit::InitEditor(
	EToolkitMode::Type Mode,
	const TSharedPtr<IToolkitHost>& InitToolkitHost,
	UPaper2DPlusCharacterCatalogAsset* InAsset)
{
	EditedAsset = InAsset;
	Model = MakeShared<FCharacterCatalogEditorModel>();
	Model->Initialize(
		InAsset,
		FCharacterCatalogEditorModel::FAssetSnapshotProvider(),
		FCharacterCatalogEditorModel::FSettingsSnapshotProvider(),
		// Initialize stores this resolver without invoking it. Only the explicit model audit
		// consumes it; open, selection, and source refresh remain registry/soft-path only.
		[](const FSoftObjectPath& Path) -> UObject*
		{
			return Path.TryLoad();
		});
	if (GEditor) GEditor->RegisterForUndo(this);

	FPropertyEditorModule& PropertyModule =
		FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
	FDetailsViewArgs DetailsArgs;
	DetailsArgs.bAllowSearch = true;
	DetailsArgs.bHideSelectionTip = true;
	DetailsArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	AdvancedDetailsView = PropertyModule.CreateDetailView(DetailsArgs);
	AdvancedDetailsView->SetObject(InAsset);
	// The finished-change delegate is the natural coalescing boundary for Details edits: interactive
	// drags/type-ahead produce one model rebuild when committed, not one rebuild per intermediate value.
	AdvancedPropertiesChangedHandle = AdvancedDetailsView->OnFinishedChangingProperties().AddSP(
		this, &FCharacterCatalogAssetEditorToolkit::HandleAdvancedPropertiesChanged);

	const TSharedRef<FTabManager::FLayout> Layout =
		FTabManager::NewLayout(TEXT("CharacterCatalogAssetEditor_Layout_v4_GroupsRail"))
		->AddArea(
			FTabManager::NewPrimaryArea()
			->SetOrientation(Orient_Horizontal)
			->Split(
				FTabManager::NewStack()
				->SetSizeCoefficient(0.72f)
				->AddTab(RosterTabId, ETabState::OpenedTab)
				->AddTab(AdvancedTabId, ETabState::ClosedTab)
				->SetForegroundTab(RosterTabId))
			->Split(
				FTabManager::NewSplitter()
				->SetOrientation(Orient_Vertical)
				->SetSizeCoefficient(0.28f)
				->Split(
					FTabManager::NewStack()
					->SetSizeCoefficient(0.64f)
					->AddTab(DetailsTabId, ETabState::OpenedTab))
				->Split(
					FTabManager::NewStack()
					->SetSizeCoefficient(0.36f)
					->AddTab(WarningsTabId, ETabState::OpenedTab))));

	FAssetEditorToolkit::InitAssetEditor(
		Mode,
		InitToolkitHost,
		TEXT("CharacterCatalogAssetEditorApp"),
		Layout,
		true,
		false,
		InAsset);
}

bool FCharacterCatalogAssetEditorToolkit::ValidateRosterWorkspaceForTests(
	const TArray<FSoftObjectPath>& ExpectedAnimatedCharacters,
	FString& OutReason)
{
	OutReason.Reset();
	const TSharedPtr<FTabManager> Manager = GetTabManager();
	if (!Manager.IsValid()
		|| !Manager->FindExistingLiveTab(RosterTabId).IsValid()
		|| !Manager->FindExistingLiveTab(DetailsTabId).IsValid()
		|| !Manager->FindExistingLiveTab(WarningsTabId).IsValid()
		|| Manager->FindExistingLiveTab(AdvancedTabId).IsValid())
	{
		OutReason = TEXT("Catalog default Roster/Details/Warnings/Advanced tab state was incorrect");
		return false;
	}
	if (!RosterPanel.IsValid() || !RosterPanel->HasCardSurfaceForTests())
	{
		OutReason = TEXT("Catalog Roster card grid was absent");
		return false;
	}
	if (RosterPanel->GenerateTilesForViewportForTests(FVector2D(1000.0f, 620.0f)) <= 0)
	{
		OutReason = TEXT("Catalog Roster projected rows but generated no visible cards");
		return false;
	}
	if (!DetailsPanel.IsValid()
		|| !DetailsPanel->HasDetailsSurfaceForTests()
		|| !DetailsPanel->GetStatusFocusTargetForTests().IsValid())
	{
		OutReason = TEXT("Catalog docked selected-character Details was not populated");
		return false;
	}
	if (!WarningsPanel.IsValid()
		|| !WarningsPanel->GetRunActionText().EqualTo(LOCTEXT("CheckAgain", "Check Again")))
	{
		OutReason = TEXT("Catalog docked Warnings surface was absent or exposed the wrong action");
		return false;
	}
	if (RosterPanel->GetVisibleCharacterCountForTests() != ExpectedAnimatedCharacters.Num())
	{
		OutReason = FString::Printf(
			TEXT("Catalog Roster exposed %d cards; expected %d"),
			RosterPanel->GetVisibleCharacterCountForTests(),
			ExpectedAnimatedCharacters.Num());
		return false;
	}
	for (const FSoftObjectPath& CharacterPath : ExpectedAnimatedCharacters)
	{
		if (RosterPanel->GetThumbnailSourceForTests(CharacterPath) != TEXT("ResidentFlipbook"))
		{
			OutReason = FString::Printf(
				TEXT("Catalog card %s fell back to a generic preview"),
				*CharacterPath.ToString());
			return false;
		}
	}
	return true;
}

bool FCharacterCatalogAssetEditorToolkit::ValidateGroupsRailForTests(
	FName ExpectedGroup,
	FString& OutReason)
{
	OutReason.Reset();
	const TSharedPtr<FTabManager> Manager = GetTabManager();
	if (!Manager.IsValid()
		|| !Manager->FindExistingLiveTab(RosterTabId).IsValid()
		|| !Manager->FindExistingLiveTab(DetailsTabId).IsValid()
		|| !Manager->FindExistingLiveTab(WarningsTabId).IsValid()
		|| Manager->FindExistingLiveTab(AdvancedTabId).IsValid())
	{
		OutReason = TEXT("Catalog default Roster/Details/Warnings/Advanced tab state was incorrect");
		return false;
	}
	if (!RosterPanel.IsValid() || !RosterPanel->HasGroupRailForTests())
	{
		OutReason = TEXT("Catalog Roster Groups rail was absent");
		return false;
	}
	const TArray<FName> RailItems = RosterPanel->GetGroupRailItemsForTests();
	if (RailItems.Num() < 1 || !RailItems[0].IsNone())
	{
		OutReason = TEXT("Catalog Groups rail did not lead with the All Characters row");
		return false;
	}
	if (RailItems.Num() < 2)
	{
		OutReason = TEXT("Catalog Groups rail exposed no authored groups");
		return false;
	}
	// Selecting the authored group must SCOPE the grid — that is the rail's whole contract, and a rail
	// that lists groups without filtering the cards would otherwise screenshot as a passing surface.
	if (!RosterPanel->SelectGroupRailItemForTests(ExpectedGroup)
		|| RosterPanel->GetSelectedGroupRailItemForTests() != ExpectedGroup
		|| RosterPanel->GetVisibleCharacterCountForTests() <= 0)
	{
		OutReason = FString::Printf(
			TEXT("Catalog Groups rail did not scope the grid to authored group %s"),
			*ExpectedGroup.ToString());
		return false;
	}
	return true;
}

FName FCharacterCatalogAssetEditorToolkit::GetToolkitFName() const
{
	return TEXT("CharacterCatalogEditor");
}

FText FCharacterCatalogAssetEditorToolkit::GetBaseToolkitName() const
{
	return LOCTEXT("ToolkitName", "Character Catalog");
}

FString FCharacterCatalogAssetEditorToolkit::GetWorldCentricTabPrefix() const
{
	return TEXT("Character Catalog");
}

FLinearColor FCharacterCatalogAssetEditorToolkit::GetWorldCentricTabColorScale() const
{
	return FLinearColor(0.25f, 0.65f, 0.75f, 0.5f);
}

void FCharacterCatalogAssetEditorToolkit::RegisterTabSpawners(
	const TSharedRef<FTabManager>& InTabManager)
{
	WorkspaceMenuCategory = InTabManager->AddLocalWorkspaceMenuCategory(
		LOCTEXT("Workspace", "Character Catalog Editor"));
	FAssetEditorToolkit::RegisterTabSpawners(InTabManager);
	InTabManager->RegisterTabSpawner(RosterTabId, FOnSpawnTab::CreateSP(this, &FCharacterCatalogAssetEditorToolkit::SpawnTab_Roster))
		.SetDisplayName(LOCTEXT("RosterTab", "Roster"))
		.SetTooltipText(LOCTEXT("RosterTabTip", "Search and filter the character roster, and author explicit ordered groups in its Groups rail."))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef())
		.SetIcon(FSlateIcon(FAppStyle::Get().GetStyleSetName(), TEXT("LevelEditor.Tabs.Outliner")));
	InTabManager->RegisterTabSpawner(DetailsTabId, FOnSpawnTab::CreateSP(this, &FCharacterCatalogAssetEditorToolkit::SpawnTab_Details))
		.SetDisplayName(LOCTEXT("DetailsTab", "Details"))
		.SetTooltipText(LOCTEXT("DetailsTabTip", "Edit the selected character's tags and companion profile relationships."))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef())
		.SetIcon(FSlateIcon(FAppStyle::Get().GetStyleSetName(), TEXT("LevelEditor.Tabs.Details")));
	InTabManager->RegisterTabSpawner(WarningsTabId, FOnSpawnTab::CreateSP(this, &FCharacterCatalogAssetEditorToolkit::SpawnTab_Warnings))
		.SetDisplayName(LOCTEXT("WarningsTab", "Warnings"))
		.SetTooltipText(LOCTEXT("WarningsTabTip", "Review, filter, and navigate Catalog warnings and errors."))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef())
		.SetIcon(FSlateIcon(FAppStyle::Get().GetStyleSetName(), TEXT("Icons.Warning")));
	InTabManager->RegisterTabSpawner(AdvancedTabId, FOnSpawnTab::CreateSP(this, &FCharacterCatalogAssetEditorToolkit::SpawnTab_Advanced))
		.SetDisplayName(LOCTEXT("AdvancedTab", "Advanced"))
		.SetTooltipText(LOCTEXT("AdvancedTabTip", "Inspect raw low-frequency Catalog properties."))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef())
		.SetIcon(FSlateIcon(FAppStyle::Get().GetStyleSetName(), TEXT("Icons.Settings")));
}

void FCharacterCatalogAssetEditorToolkit::UnregisterTabSpawners(
	const TSharedRef<FTabManager>& InTabManager)
{
	FAssetEditorToolkit::UnregisterTabSpawners(InTabManager);
	InTabManager->UnregisterTabSpawner(RosterTabId);
	InTabManager->UnregisterTabSpawner(DetailsTabId);
	InTabManager->UnregisterTabSpawner(WarningsTabId);
	InTabManager->UnregisterTabSpawner(AdvancedTabId);
}

TSharedRef<SDockTab> FCharacterCatalogAssetEditorToolkit::SpawnTab_Roster(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.Label(LOCTEXT("RosterLabel", "Roster"))
		[
			SAssignNew(RosterPanel, SCharacterCatalogRosterPanel).Model(Model)
		];
}

TSharedRef<SDockTab> FCharacterCatalogAssetEditorToolkit::SpawnTab_Details(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.Label(LOCTEXT("DetailsLabel", "Details"))
		[
			SAssignNew(DetailsPanel, SCharacterCatalogDetailsPanel).Model(Model)
		];
}

TSharedRef<SDockTab> FCharacterCatalogAssetEditorToolkit::SpawnTab_Warnings(const FSpawnTabArgs& Args)
{
	WarningsPanel = MakeWarningsPanel(
		EditedAsset,
		Model,
		FOnPaper2DPlusValidationIssueActivated::CreateSP(
			this,
			&FCharacterCatalogAssetEditorToolkit::HandleWarningActivated));
	return SNew(SDockTab)
		.Label(LOCTEXT("WarningsLabel", "Warnings"))
		[
			WarningsPanel.ToSharedRef()
		];
}

TSharedRef<SProfileValidationPanel> FCharacterCatalogAssetEditorToolkit::MakeWarningsPanel(
	UPaper2DPlusCharacterCatalogAsset* InAsset,
	const TSharedPtr<FCharacterCatalogEditorModel>& InModel,
	FOnPaper2DPlusValidationIssueActivated OnIssueActivated)
{
	const TWeakPtr<FCharacterCatalogEditorModel> WeakModel = InModel;
	return SNew(SProfileValidationPanel)
		.Asset(InAsset)
		.RunActionText(LOCTEXT("CheckAgain", "Check Again"))
		.RunInitially(false)
		.RefreshOnObservedChanges(false)
		.OnCustomValidationRun(FOnPaper2DPlusCustomValidationRun::CreateLambda(
			[WeakModel](TArray<FPaper2DPlusValidationIssue>& OutIssues)
			{
				const TSharedPtr<FCharacterCatalogEditorModel> PinnedModel = WeakModel.Pin();
				if (!PinnedModel.IsValid())
				{
					return false;
				}

				FText AuditMessage;
				if (!PinnedModel->RunAudit(AuditMessage))
				{
					return false;
				}

				OutIssues = PinnedModel->GetAuditReport().Issues;
				return true;
			}))
		.OnIssueActivated(MoveTemp(OnIssueActivated));
}

void FCharacterCatalogAssetEditorToolkit::HandleWarningActivated(
	const FPaper2DPlusValidationIssue& Issue)
{
	if (!Model.IsValid())
	{
		return;
	}

	Model->ActivateIssue(Issue);
	const TSharedPtr<FTabManager> Manager = GetTabManager();
	if (Issue.ToolTarget.IsSet())
	{
		const FName TargetTab = Issue.ToolTarget->TabId;
		// Group issues now travel to the Roster and are discriminated by ToolId, because the Groups rail
		// lives inside the Roster; the retired Groups tab id no longer exists to aim at.
		const bool bGroupIssue = Issue.ToolTarget->ToolId == TEXT("CharacterCatalogGroups");
		if (Manager.IsValid()
			&& (TargetTab == RosterTabId || TargetTab == DetailsTabId || TargetTab == AdvancedTabId))
		{
			Manager->TryInvokeTab(TargetTab);
			if (bGroupIssue && RosterPanel.IsValid() && !Issue.ToolTarget->ItemIdentity.IsEmpty())
			{
				Manager->TryInvokeTab(RosterTabId);
				// Only scope the grid to a group that is really authored. A Group.MissingName error names
				// the unnamed group's placeholder scope, and a stale warning can name a deleted group;
				// filtering to either would show an empty roster and read as data loss.
				const FName GroupName(*Issue.ToolTarget->ItemIdentity);
				const bool bAuthoredGroup = EditedAsset
					&& EditedAsset->Groups.ContainsByPredicate(
						[GroupName](const FPaper2DPlusCharacterCatalogGroup& Group)
						{
							return Group.GroupName.IsEqual(GroupName, ENameCase::IgnoreCase);
						});
				if (bAuthoredGroup)
				{
					RosterPanel->SelectGroupRail(GroupName);
				}
			}
			else if ((TargetTab == RosterTabId || TargetTab == DetailsTabId) && DetailsPanel.IsValid())
			{
				Manager->TryInvokeTab(DetailsTabId);
				DetailsPanel->FocusSelectedCharacter();
			}
		}
		else if (Manager.IsValid() && !Issue.CharacterPath.IsNull())
		{
			Manager->TryInvokeTab(RosterTabId);
		}
	}

	FText OpenError;
	if (!Model->OpenIssueTarget(Issue, OpenError) && !OpenError.IsEmpty())
	{
		FMessageDialog::Open(EAppMsgType::Ok, OpenError);
	}
}

TSharedRef<SDockTab> FCharacterCatalogAssetEditorToolkit::SpawnTab_Advanced(const FSpawnTabArgs& Args)
{
	TSharedRef<SDockTab> Tab = SNew(SDockTab)
		.Label(LOCTEXT("AdvancedLabel", "Advanced"))
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(8.0f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("AdvancedIntro", "Raw properties are available for low-frequency repair. Use the Roster and its Groups rail for normal authoring; Save remains Unreal's ordinary explicit package save."))
					.AutoWrapText(true)
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(8.0f)
				[
					AdvancedDetailsView.ToSharedRef()
				]
			]
		];
	return Tab;
}

void FCharacterCatalogAssetEditorToolkit::HandleAdvancedPropertiesChanged(
	const FPropertyChangedEvent& PropertyChangedEvent)
{
	(void)PropertyChangedEvent;
	if (bRefreshingAdvancedProperties || !Model.IsValid())
	{
		return;
	}

	TGuardValue<bool> RefreshGuard(bRefreshingAdvancedProperties, true);
	// The Details panel already owns the transaction; reconcile the Roster and its rail projection once.
	Model->RefreshAfterExternalMutation();
}

void FCharacterCatalogAssetEditorToolkit::PostUndo(bool bSuccess)
{
	if (bSuccess) RefreshAfterUndoRedo();
}

void FCharacterCatalogAssetEditorToolkit::PostRedo(bool bSuccess)
{
	if (bSuccess) RefreshAfterUndoRedo();
}

void FCharacterCatalogAssetEditorToolkit::RefreshAfterUndoRedo()
{
	if (Model.IsValid()) Model->RefreshAfterExternalMutation();
	if (AdvancedDetailsView.IsValid()) AdvancedDetailsView->ForceRefresh();
}

#undef LOCTEXT_NAMESPACE
