// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "CombatProfileAssetEditorToolkit.h"

#include "IDetailsView.h"
#include "Editor.h"
#include "Paper2DPlusCombatProfileAsset.h"
#include "Paper2DPlusProfileEditorToolbar.h"
#include "CombatProfileEditor/CombatAttackBrowserPanel.h"
#include "CombatProfileEditor/CombatAttackInspectorPanel.h"
#include "CombatProfileEditor/CombatLabPanel.h"
#include "CombatProfileEditor/CombatProfileCollectionPanel.h"
#include "CombatProfileEditor/CombatProfileEditorSession.h"
#include "CombatProfileEditor/CombatProfileSetupPanel.h"
#include "CombatProfileEditor/CombatScorePlaygroundPanel.h"
#include "AnimationProfileSwitcher.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "GameplayTagsManager.h"
#include "ProfileCompletionPanel.h"
#include "ProfileValidationPanel.h"
#include "PropertyEditorModule.h"
#include "RelatedProfileBar.h"
#include "ScopedTransaction.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"
#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "CombatProfileAssetEditor"

const FName FCombatProfileAssetEditorToolkit::SetupTabId(TEXT("CombatProfileEditor_Setup"));
const FName FCombatProfileAssetEditorToolkit::AttackDetailsTabId(TEXT("CombatProfileEditor_AttackDetails"));
const FName FCombatProfileAssetEditorToolkit::DetailsTabId(TEXT("CombatProfileEditor_Details"));
const FName FCombatProfileAssetEditorToolkit::ScorePreviewTabId(TEXT("CombatProfileEditor_ScorePreview"));
const FName FCombatProfileAssetEditorToolkit::CombatLabTabId(TEXT("CombatProfileEditor_CombatLab"));
const FName FCombatProfileAssetEditorToolkit::VariablesTabId(TEXT("CombatProfileEditor_Variables"));
const FName FCombatProfileAssetEditorToolkit::DefaultsTabId(TEXT("CombatProfileEditor_Defaults"));
const FName FCombatProfileAssetEditorToolkit::ScoringProfilesTabId(TEXT("CombatProfileEditor_ScoringProfiles"));
const FName FCombatProfileAssetEditorToolkit::PresetsTabId(TEXT("CombatProfileEditor_Presets"));
const FName FCombatProfileAssetEditorToolkit::CompletionTabId(TEXT("CombatProfileEditor_Completion"));
const FName FCombatProfileAssetEditorToolkit::RelatedProfilesTabId(TEXT("CombatProfileEditor_RelatedProfiles"));
// v9 adopts the Character Profile workspace grammar: central designer tools sharing one current-attack
// header, an upper-right contextual Details panel, and a lower-right Completion/Related sibling stack.
const FName FCombatProfileAssetEditorToolkit::WorkspaceLayoutId(TEXT("CombatProfileAssetEditor_Layout_v9_CharacterGrammar"));

namespace
{
	FText CombatVarTypeLabel(EPaper2DPlusCombatVariableType Type)
	{
		switch (Type)
		{
		case EPaper2DPlusCombatVariableType::Bool:                 return LOCTEXT("VarTypeBool", "Bool");
		case EPaper2DPlusCombatVariableType::Int32:               return LOCTEXT("VarTypeInt", "Int");
		case EPaper2DPlusCombatVariableType::Float:               return LOCTEXT("VarTypeFloat", "Float");
		case EPaper2DPlusCombatVariableType::Name:               return LOCTEXT("VarTypeName", "Name");
		case EPaper2DPlusCombatVariableType::String:             return LOCTEXT("VarTypeString", "String");
		case EPaper2DPlusCombatVariableType::GameplayTag:        return LOCTEXT("VarTypeTag", "Tag");
		case EPaper2DPlusCombatVariableType::GameplayTagContainer:return LOCTEXT("VarTypeTags", "Tags");
		case EPaper2DPlusCombatVariableType::Vector2D:           return LOCTEXT("VarTypeVec2", "Vector2D");
		case EPaper2DPlusCombatVariableType::Vector:             return LOCTEXT("VarTypeVec", "Vector");
		default:                                                  return LOCTEXT("VarTypeUnknown", "?");
		}
	}

	/** All registered tags under the Paper2DPlus.Combat.Var root, sorted, for the inline variable tag picker. */
	void GatherCombatVarTags(TArray<FGameplayTag>& Out)
	{
		Out.Reset();
		const FGameplayTag Root = FGameplayTag::RequestGameplayTag(FName(TEXT("Paper2DPlus.Combat.Var")), /*ErrorIfNotFound*/ false);
		if (!Root.IsValid())
		{
			return;
		}
		FGameplayTagContainer All;
		UGameplayTagsManager::Get().RequestAllGameplayTags(All, /*OnlyIncludeDictionaryTags*/ false);
		for (const FGameplayTag& Tag : All)
		{
			if (Tag != Root && Tag.MatchesTag(Root))
			{
				Out.Add(Tag);
			}
		}
		Out.Sort([](const FGameplayTag& A, const FGameplayTag& B) { return A.GetTagName().LexicalLess(B.GetTagName()); });
	}
}

FCombatProfileAssetEditorToolkit::~FCombatProfileAssetEditorToolkit()
{
	if (const TSharedPtr<SWindow> Window = ValidationWindow.Pin())
	{
		Window->RequestDestroyWindow();
	}
	if (DetailsView.IsValid())
	{
		DetailsView->OnFinishedChangingProperties().Remove(DetailsPropertyChangedHandle);
	}
	CombatLabModel.Reset();
	AttackPickerSource.Reset();
	EditorSession.Reset();
	if (GEditor && bRegisteredForUndo)
	{
		GEditor->UnregisterForUndo(this);
		bRegisteredForUndo = false;
	}
}

void FCombatProfileAssetEditorToolkit::PostUndo(bool bSuccess)
{
	if (!bSuccess)
	{
		return;
	}
	// The session refresh re-derives the catalog and rebroadcasts, so the browser/inspector panels
	// rebuild and re-snapshot their struct copies from the restored live options. Without this the
	// stale post-edit struct copy re-applies the undone edit (audit F2).
	RefreshAllDerivedViews();
	if (DetailsView.IsValid())
	{
		DetailsView->ForceRefresh();
	}
}

void FCombatProfileAssetEditorToolkit::PostRedo(bool bSuccess)
{
	PostUndo(bSuccess);
}

void FCombatProfileAssetEditorToolkit::InitEditor(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost, UPaper2DPlusCombatProfileAsset* InAsset)
{
	EditedAsset = InAsset;
	EditorSession = MakeShared<FCombatProfileEditorSession>(InAsset);
	AttackPickerSource = MakeShared<FCombatAttackPickerSource>(EditorSession);
	CombatLabModel = MakeShared<FCombatLabModel>(EditorSession);

	if (GEditor)
	{
		GEditor->RegisterForUndo(this);
		bRegisteredForUndo = true;
	}

	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	FDetailsViewArgs DetailsArgs;
	DetailsArgs.bAllowSearch = true;
	DetailsArgs.bHideSelectionTip = true;
	DetailsArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	DetailsView = PropertyModule.CreateDetailView(DetailsArgs);
	DetailsView->SetObject(EditedAsset);
	DetailsPropertyChangedHandle = DetailsView->OnFinishedChangingProperties().AddLambda([this](const FPropertyChangedEvent&)
	{
		RefreshAllDerivedViews();
	});

	RefreshAllDerivedViews();

	const TSharedRef<FTabManager::FLayout> Layout = BuildDefaultLayout();

	Paper2DPlusProfileEditorToolbar::Install(
		GetToolMenuToolbarName(),
		GetToolkitCommands());

	FAssetEditorToolkit::InitAssetEditor(
		Mode,
		InitToolkitHost,
		TEXT("CombatProfileAssetEditorApp"),
		Layout,
		true,
		true,
		InAsset);
}

TSharedRef<FTabManager::FLayout> FCombatProfileAssetEditorToolkit::BuildDefaultLayout()
{
	// Mirrors CharacterProfileAssetEditor_Layout_v14: one central stack of designer tools, then a
	// vertical right column whose upper panel is the contextual Details for the current selection and
	// whose lower stack holds Completion/Related Profiles.
	return FTabManager::NewLayout(WorkspaceLayoutId)
		->AddArea
		(
			FTabManager::NewPrimaryArea()
			->SetOrientation(Orient_Horizontal)
			->Split
			(
				FTabManager::NewStack()
				->SetSizeCoefficient(0.72f)
				->AddTab(SetupTabId, ETabState::OpenedTab)
				->AddTab(ScorePreviewTabId, ETabState::OpenedTab)
				->AddTab(CombatLabTabId, ETabState::OpenedTab)
				->AddTab(VariablesTabId, ETabState::ClosedTab)
				->AddTab(DefaultsTabId, ETabState::ClosedTab)
				->AddTab(ScoringProfilesTabId, ETabState::ClosedTab)
				->AddTab(PresetsTabId, ETabState::ClosedTab)
				->AddTab(DetailsTabId, ETabState::ClosedTab)
				->SetForegroundTab(SetupTabId)
			)
			->Split
			(
				FTabManager::NewSplitter()
				->SetOrientation(Orient_Vertical)
				->SetSizeCoefficient(0.28f)
				->Split
				(
					FTabManager::NewStack()
					->SetSizeCoefficient(0.64f)
					->AddTab(AttackDetailsTabId, ETabState::OpenedTab)
				)
				->Split
				(
					FTabManager::NewStack()
					->SetSizeCoefficient(0.36f)
					->AddTab(CompletionTabId, ETabState::OpenedTab)
					->AddTab(RelatedProfilesTabId, ETabState::OpenedTab)
					->SetForegroundTab(CompletionTabId)
				)
			)
		);
}

void FCombatProfileAssetEditorToolkit::RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	WorkspaceMenuCategory = InTabManager->AddLocalWorkspaceMenuCategory(LOCTEXT("WorkspaceMenu_CombatProfileEditor", "Combat Profile Editor"));

	FAssetEditorToolkit::RegisterTabSpawners(InTabManager);

	InTabManager->RegisterTabSpawner(SetupTabId, FOnSpawnTab::CreateSP(this, &FCombatProfileAssetEditorToolkit::SpawnTab_Setup))
		.SetDisplayName(LOCTEXT("SetupTab", "Overview"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef())
		.SetIcon(FSlateIcon(FAppStyle::Get().GetStyleSetName(), "LevelEditor.Tabs.Details"));

	InTabManager->RegisterTabSpawner(AttackDetailsTabId, FOnSpawnTab::CreateSP(this, &FCombatProfileAssetEditorToolkit::SpawnTab_AttackDetails))
		.SetDisplayName(LOCTEXT("AttackDetailsTab", "Details"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef())
		.SetIcon(FSlateIcon(FAppStyle::Get().GetStyleSetName(), "LevelEditor.Tabs.Details"));

	InTabManager->RegisterTabSpawner(DetailsTabId, FOnSpawnTab::CreateSP(this, &FCombatProfileAssetEditorToolkit::SpawnTab_Details))
		.SetDisplayName(LOCTEXT("DetailsTab", "Details (Advanced)"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef())
		.SetIcon(FSlateIcon(FAppStyle::Get().GetStyleSetName(), "LevelEditor.Tabs.Details"));

	InTabManager->RegisterTabSpawner(ScorePreviewTabId, FOnSpawnTab::CreateSP(this, &FCombatProfileAssetEditorToolkit::SpawnTab_ScorePreview))
		.SetDisplayName(LOCTEXT("ScorePreviewTab", "Score Playground"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef())
		.SetIcon(FSlateIcon(FAppStyle::Get().GetStyleSetName(), "LevelEditor.Tabs.StatsViewer"));

	InTabManager->RegisterTabSpawner(CombatLabTabId, FOnSpawnTab::CreateSP(this, &FCombatProfileAssetEditorToolkit::SpawnTab_CombatLab))
		.SetDisplayName(LOCTEXT("CombatLabTab", "Combat Lab"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef())
		.SetIcon(FSlateIcon(FAppStyle::Get().GetStyleSetName(), "LevelEditor.Tabs.Viewports"));

	InTabManager->RegisterTabSpawner(VariablesTabId, FOnSpawnTab::CreateSP(this, &FCombatProfileAssetEditorToolkit::SpawnTab_Variables))
		.SetDisplayName(LOCTEXT("VariablesTab", "Variables"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef())
		.SetIcon(FSlateIcon(FAppStyle::Get().GetStyleSetName(), "Icons.Edit"));

	InTabManager->RegisterTabSpawner(DefaultsTabId, FOnSpawnTab::CreateSP(this, &FCombatProfileAssetEditorToolkit::SpawnTab_Defaults))
		.SetDisplayName(LOCTEXT("DefaultsTab", "Tag Defaults"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef())
		.SetIcon(FSlateIcon(FAppStyle::Get().GetStyleSetName(), "Icons.Settings"));

	InTabManager->RegisterTabSpawner(ScoringProfilesTabId, FOnSpawnTab::CreateSP(this, &FCombatProfileAssetEditorToolkit::SpawnTab_ScoringProfiles))
		.SetDisplayName(LOCTEXT("ScoringProfilesTab", "Scoring Profiles"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef())
		.SetIcon(FSlateIcon(FAppStyle::Get().GetStyleSetName(), "LevelEditor.Tabs.StatsViewer"));

	InTabManager->RegisterTabSpawner(PresetsTabId, FOnSpawnTab::CreateSP(this, &FCombatProfileAssetEditorToolkit::SpawnTab_Presets))
		.SetDisplayName(LOCTEXT("PresetsTab", "Scenario Presets"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef())
		.SetIcon(FSlateIcon(FAppStyle::Get().GetStyleSetName(), "Icons.Save"));
	InTabManager->RegisterTabSpawner(CompletionTabId, FOnSpawnTab::CreateSP(this, &FCombatProfileAssetEditorToolkit::SpawnTab_Completion))
		.SetDisplayName(LOCTEXT("CompletionTab", "Completion"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef())
		.SetIcon(FSlateIcon(FAppStyle::Get().GetStyleSetName(), "Icons.Check"));
	InTabManager->RegisterTabSpawner(RelatedProfilesTabId, FOnSpawnTab::CreateSP(this, &FCombatProfileAssetEditorToolkit::SpawnTab_RelatedProfiles))
		.SetDisplayName(LOCTEXT("RelatedProfilesTab", "Related Profiles"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef())
		.SetIcon(FSlateIcon(FAppStyle::Get().GetStyleSetName(), "Icons.Link"));
}

void FCombatProfileAssetEditorToolkit::UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	FAssetEditorToolkit::UnregisterTabSpawners(InTabManager);
	InTabManager->UnregisterTabSpawner(SetupTabId);
	InTabManager->UnregisterTabSpawner(AttackDetailsTabId);
	InTabManager->UnregisterTabSpawner(DetailsTabId);
	InTabManager->UnregisterTabSpawner(ScorePreviewTabId);
	InTabManager->UnregisterTabSpawner(CombatLabTabId);
	InTabManager->UnregisterTabSpawner(VariablesTabId);
	InTabManager->UnregisterTabSpawner(DefaultsTabId);
	InTabManager->UnregisterTabSpawner(ScoringProfilesTabId);
	InTabManager->UnregisterTabSpawner(PresetsTabId);
	InTabManager->UnregisterTabSpawner(CompletionTabId);
	InTabManager->UnregisterTabSpawner(RelatedProfilesTabId);
}

TSharedRef<SDockTab> FCombatProfileAssetEditorToolkit::SpawnTab_Variables(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab).Label(LOCTEXT("VariablesTabLabel", "Variables"))
	[
		SNew(SCombatProfileCollectionPanel).Session(EditorSession).Collection(ECombatProfileCollectionKind::Variables)
	];
}

TSharedRef<SDockTab> FCombatProfileAssetEditorToolkit::SpawnTab_Defaults(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab).Label(LOCTEXT("DefaultsTabLabel", "Attack-tag Defaults"))
	[
		SNew(SCombatProfileCollectionPanel).Session(EditorSession).Collection(ECombatProfileCollectionKind::TagDefaults)
	];
}

TSharedRef<SDockTab> FCombatProfileAssetEditorToolkit::SpawnTab_ScoringProfiles(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab).Label(LOCTEXT("ScoringProfilesTabLabel", "Scoring Profiles"))
	[
		SNew(SCombatProfileCollectionPanel).Session(EditorSession).Collection(ECombatProfileCollectionKind::ScoringProfiles)
	];
}

TSharedRef<SDockTab> FCombatProfileAssetEditorToolkit::SpawnTab_Presets(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab).Label(LOCTEXT("PresetsTabLabel", "Scenario Presets"))
	[
		SNew(SCombatProfileCollectionPanel).Session(EditorSession).Collection(ECombatProfileCollectionKind::ScenarioPresets)
	];
}

TSharedRef<SDockTab> FCombatProfileAssetEditorToolkit::SpawnTab_Completion(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab).Label(LOCTEXT("CompletionTabLabel", "Completion"))
	[
		SNew(SProfileCompletionPanel)
		.Asset(EditedAsset)
		.ProfileKind(EProfileCompletionKind::Combat)
	];
}

TSharedRef<SDockTab> FCombatProfileAssetEditorToolkit::SpawnTab_RelatedProfiles(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab).Label(LOCTEXT("RelatedProfilesTabLabel", "Related Profiles"))
	[
		SNew(SRelatedProfileBar).EditedAsset(EditedAsset)
	];
}

TSharedRef<SDockTab> FCombatProfileAssetEditorToolkit::SpawnTab_Details(const FSpawnTabArgs& Args)
{
	// "(Advanced)" keeps this raw property view distinct from the contextual attack Details panel.
	return SNew(SDockTab)
		.Label(LOCTEXT("DetailsTabLabel", "Details (Advanced)"))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				BuildDetailsToolbar()
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(8.f, 6.f)
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.Text(LOCTEXT("AdvancedFutureNotice", "Advanced/Future data. Dormant transition outcome and cancel-category foundations are authored here but are not active runtime behavior."))
			]
			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			[
				DetailsView.ToSharedRef()
			]
		];
}

void FCombatProfileAssetEditorToolkit::OpenValidation()
{
	if (const TSharedPtr<SWindow> Existing = ValidationWindow.Pin())
	{
		Existing->BringToFront();
		return;
	}
	if (!FSlateApplication::IsInitialized()) return;

	const TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(LOCTEXT("ValidationWindowTitle", "Combat Profile Validation"))
		.ClientSize(FVector2D(720.0f, 520.0f))
		.SupportsMaximize(true)
		.SupportsMinimize(false);
	Window->SetContent(
		SNew(SProfileValidationPanel)
		.Asset(EditedAsset)
		.OnIssueActivated(FOnPaper2DPlusValidationIssueActivated::CreateSP(
			this, &FCombatProfileAssetEditorToolkit::HandleValidationIssueActivated)));
	ValidationWindow = Window;
	FSlateApplication::Get().AddWindow(Window);
}

void FCombatProfileAssetEditorToolkit::HandleValidationIssueActivated(
	const FPaper2DPlusValidationIssue& Issue)
{
	if (!Issue.ToolTarget.IsSet()) return;
	if (!Issue.ToolTarget->ItemIdentity.IsEmpty() && EditorSession.IsValid())
	{
		EditorSession->SelectAttackByName(FName(*Issue.ToolTarget->ItemIdentity));
	}
	const FName TargetTab = Issue.ToolTarget->TabId.IsNone()
		? SetupTabId : Issue.ToolTarget->TabId;
	if (const TSharedPtr<FTabManager> Manager = GetTabManager())
	{
		Manager->TryInvokeTab(TargetTab);
	}
}

TSharedRef<SDockTab> FCombatProfileAssetEditorToolkit::SpawnTab_ScorePreview(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.Label(LOCTEXT("ScorePreviewTabLabel", "Score Playground"))
		[
			WrapToolContent(SNew(SCombatScorePlaygroundPanel).Session(EditorSession).LabModel(CombatLabModel))
		];
}

TSharedRef<SDockTab> FCombatProfileAssetEditorToolkit::SpawnTab_CombatLab(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.Label(LOCTEXT("CombatLabTabLabel", "Combat Lab"))
		[
			WrapToolContent(SNew(SCombatLabPanel).Model(CombatLabModel))
		];
}

TSharedRef<SDockTab> FCombatProfileAssetEditorToolkit::SpawnTab_AttackDetails(const FSpawnTabArgs& Args)
{
	// The contextual Details panel for the current attack. It lives beside the tools rather than inside
	// one of them, so the selection's tuning stays visible while you work in Score Playground or the Lab.
	return SNew(SDockTab)
		.Label(LOCTEXT("AttackDetailsTabLabel", "Details"))
		[
			SNew(SBorder)
			.Padding(6.f)
			.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
			[
				SNew(SCombatAttackInspectorPanel).Session(EditorSession)
			]
		];
}

TSharedRef<SWidget> FCombatProfileAssetEditorToolkit::WrapToolContent(
	TSharedRef<SWidget> ToolContent,
	TSharedPtr<SWidget> HeaderActions)
{
	// Built imperatively so a tool that supplies no header action contributes no slot at all — an
	// always-present empty spacer would be indistinguishable from a missing control at narrow widths.
	const TSharedRef<SHorizontalBox> HeaderRow = SNew(SHorizontalBox);
	HeaderRow->AddSlot()
		.FillWidth(1.0f)
		[
			SNew(SAnimationProfileSwitcher)
			.Source(StaticCastSharedPtr<IProfileItemPickerSource>(AttackPickerSource))
			.EmptySelectionText(LOCTEXT("NoCurrentAttack", "Select an attack"))
			.CaptionText(LOCTEXT("CurrentAttackCaption", "Current attack"))
			.ItemNounText(LOCTEXT("AttackNoun", "attack"))
		];
	if (HeaderActions.IsValid())
	{
		HeaderRow->AddSlot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(6.0f, 0.0f, 0.0f, 0.0f)
			[
				HeaderActions.ToSharedRef()
			];
	}

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
			.Padding(FMargin(6.0f, 4.0f))
			[
				HeaderRow
			]
		]
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			ToolContent
		];
}

TSharedRef<SWidget> FCombatProfileAssetEditorToolkit::BuildDetailsToolbar()
{
	return SNew(SBorder)
		.Padding(4.f)
		.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
				.Text(LOCTEXT("Validate", "Validate"))
				.OnClicked_Lambda([this]()
				{
					OpenValidation();
					return FReply::Handled();
				})
			]
		];
}

void FCombatProfileAssetEditorToolkit::RefreshAllDerivedViews()
{
	// The session owns the derived catalog, scores, preview context, and scoring-profile choice;
	// its broadcast rebuilds the browser, inspector, playground, and lab. The toolkit only owns
	// the inline variable rows.
	if (EditorSession.IsValid())
	{
		EditorSession->RefreshFromAsset();
	}
	RefreshGlobalVariables();
}

FName FCombatProfileAssetEditorToolkit::GetToolkitFName() const
{
	return FName("CombatProfileAssetEditor");
}

FText FCombatProfileAssetEditorToolkit::GetBaseToolkitName() const
{
	return LOCTEXT("ToolkitName", "Paper2D+ Combat Profile Editor");
}

FString FCombatProfileAssetEditorToolkit::GetWorldCentricTabPrefix() const
{
	return LOCTEXT("WorldCentricTabPrefix", "CombatProfile ").ToString();
}

FLinearColor FCombatProfileAssetEditorToolkit::GetWorldCentricTabColorScale() const
{
	return FLinearColor(0.35f, 0.2f, 0.5f, 1.0f);
}

// =====================================================================================================
// OVERVIEW TAB — asset status + scoring variables + the one attack browser. Per-attack tuning lives in
// the right-hand Details panel, exactly as the Character workspace separates its browser from Details.
// =====================================================================================================

TSharedRef<SDockTab> FCombatProfileAssetEditorToolkit::SpawnTab_Setup(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.Label(LOCTEXT("SetupTabLabel", "Overview"))
		[
			WrapToolContent(
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SCombatProfileSetupPanel)
					.Session(EditorSession)
					.OnValidate(FSimpleDelegate::CreateSP(this, &FCombatProfileAssetEditorToolkit::OpenValidation))
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(4.f, 2.f)
				[
					BuildGlobalVariablesSection()
				]
				+ SVerticalBox::Slot()
				.FillHeight(1.0f)
				.Padding(4.f, 2.f, 4.f, 4.f)
				[
					SNew(SBorder)
					.Padding(6.f)
					.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
					[
						SNew(SCombatAttackBrowserPanel).Session(EditorSession)
					]
				])
		];
}

TSharedRef<SWidget> FCombatProfileAssetEditorToolkit::BuildGlobalVariablesSection()
{
	const bool bStartCollapsed = !EditedAsset || EditedAsset->VariableDefinitions.IsEmpty();
	TSharedRef<SWidget> Section = SNew(SBorder)
		.Padding(4.f)
		.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
		[
			SAssignNew(GlobalVariablesExpander, SExpandableArea)
			.InitiallyCollapsed(bStartCollapsed)
			.HeaderContent()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text_Lambda([this]()
					{
						const int32 Count = EditedAsset ? EditedAsset->VariableDefinitions.Num() : 0;
						return Count == 0
							? LOCTEXT("GlobalVariablesEmptyHeader", "Scoring Variables — none yet")
							: FText::Format(LOCTEXT("GlobalVariablesCountHeader", "Scoring Variables ({0})"), FText::AsNumber(Count));
					})
					.ToolTipText(LOCTEXT("GlobalVariablesTip", "Numbers and flags your game sets at runtime that scoring rules can read. Values set here are the shared defaults; tag defaults and customized attacks may override them."))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
					.ToolTipText(LOCTEXT("AddVarTip", "Add a scoring variable, then set its name, tag, type and default value on the new row."))
					.Text(LOCTEXT("AddVariable", "+ Add Variable"))
					.OnClicked(this, &FCombatProfileAssetEditorToolkit::OnAddVariableClicked)
				]
			]
			.BodyContent()
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SAssignNew(GlobalVariablesBox, SVerticalBox)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.f, 4.f, 0.f, 0.f)
				[
					SNew(STextBlock)
					.AutoWrapText(true)
					.Text_Lambda([this]()
					{
						const int32 Count = EditedAsset ? EditedAsset->VariableDefinitions.Num() : 0;
						return Count == 0
							? LOCTEXT("GlobalVarEmptyHint", "Scoring variables are numbers or flags your game sets at runtime (through the combat context); scoring rules can read them. Add one, then give it a name, tag, type, and default value.")
							: LOCTEXT("GlobalVarHint", "Default values apply across all attacks. Edit name, tag, type and value right here.");
					})
					.Font(FCoreStyle::GetDefaultFontStyle("Italic", 8))
					.ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f)))
				]
			]
		];

	RefreshGlobalVariables();
	return Section;
}

void FCombatProfileAssetEditorToolkit::RefreshGlobalVariables()
{
	if (!GlobalVariablesBox.IsValid())
	{
		return;
	}
	GlobalVariablesBox->ClearChildren();
	if (!EditedAsset)
	{
		return;
	}

	if (EditedAsset->VariableDefinitions.Num() == 0)
	{
		return;
	}

	for (int32 Index = 0; Index < EditedAsset->VariableDefinitions.Num(); ++Index)
	{
		GlobalVariablesBox->AddSlot().AutoHeight().Padding(0.f, 2.f)
		[
			BuildGlobalVariableRow(Index)
		];
	}
}

TSharedRef<SWidget> FCombatProfileAssetEditorToolkit::BuildGlobalVariableRow(int32 DefinitionIndex)
{
	if (!EditedAsset || !EditedAsset->VariableDefinitions.IsValidIndex(DefinitionIndex))
	{
		return SNullWidget::NullWidget;
	}

	const FPaper2DPlusCombatVariableDefinition& Def = EditedAsset->VariableDefinitions[DefinitionIndex];

	FString TagLeaf = TEXT("pick tag…");
	if (Def.VariableTag.IsValid())
	{
		TagLeaf = Def.VariableTag.GetTagName().ToString();
		TagLeaf.ReplaceInline(TEXT("Paper2DPlus.Combat.Var."), TEXT(""));
	}

	return SNew(SHorizontalBox)
		// Display name — editable inline (no Details-tab bounce).
		+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).Padding(0.f, 0.f, 4.f, 0.f)
		[
			SNew(SEditableTextBox)
			.Text(Def.DisplayName)
			.HintText(LOCTEXT("VarNameHint", "name"))
			.ToolTipText(LOCTEXT("VarNameTip", "Display name for this scoring variable."))
			.OnTextCommitted_Lambda([this, DefinitionIndex](const FText& NewText, ETextCommit::Type)
			{
				if (!EditedAsset || !EditedAsset->VariableDefinitions.IsValidIndex(DefinitionIndex)) { return; }
				FScopedTransaction Transaction(LOCTEXT("SetVarName", "Set Variable Name"));
				EditedAsset->Modify();
				EditedAsset->VariableDefinitions[DefinitionIndex].DisplayName = NewText;
				EditedAsset->MarkPackageDirty();
			})
		]
		// Tag — inline picker over the registered Paper2DPlus.Combat.Var.* tags.
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 4.f, 0.f)
		[
			SNew(SComboButton)
			.ComboButtonStyle(FAppStyle::Get(), "ComboButton")
			.ToolTipText(LOCTEXT("VarTagTip", "The Paper2DPlus.Combat.Var tag that identifies this variable. Considerations reference it by this tag. (Add new tags in Project Settings > GameplayTags.)"))
			.ButtonContent()
			[
				SNew(SBox).MinDesiredWidth(70.f)[ SNew(STextBlock).Text(FText::FromString(TagLeaf)) ]
			]
			.OnGetMenuContent_Lambda([this, DefinitionIndex]()
			{
				FMenuBuilder Menu(true, nullptr);
				TArray<FGameplayTag> Tags;
				GatherCombatVarTags(Tags);
				if (Tags.Num() == 0)
				{
					Menu.AddWidget(
						SNew(SBox).Padding(8.f)
						[
							SNew(STextBlock).AutoWrapText(true)
							.Text(LOCTEXT("NoVarTags", "No Paper2DPlus.Combat.Var.* tags exist yet.\nAdd them in Project Settings > GameplayTags."))
							.ColorAndOpacity(FSlateColor(FLinearColor(0.7f, 0.55f, 0.3f)))
						],
						FText::GetEmpty());
				}
				for (const FGameplayTag& Tag : Tags)
				{
					FString Leaf = Tag.GetTagName().ToString();
					Leaf.ReplaceInline(TEXT("Paper2DPlus.Combat.Var."), TEXT(""));
					Menu.AddMenuEntry(FText::FromString(Leaf), FText::FromString(Tag.GetTagName().ToString()), FSlateIcon(),
						FUIAction(FExecuteAction::CreateLambda([this, DefinitionIndex, Tag]()
						{
							if (!EditedAsset || !EditedAsset->VariableDefinitions.IsValidIndex(DefinitionIndex)) { return; }
							FScopedTransaction Transaction(LOCTEXT("SetVarTag", "Set Variable Tag"));
							EditedAsset->Modify();
							const FGameplayTag OldTag = EditedAsset->VariableDefinitions[DefinitionIndex].VariableTag;
							EditedAsset->VariableDefinitions[DefinitionIndex].VariableTag = Tag;
							EditedAsset->RenameVariableTag(OldTag, Tag);
							EditedAsset->RebuildVariableBags();
							EditedAsset->MarkPackageDirty();
							RefreshGlobalVariables();
							if (EditorSession.IsValid()) { EditorSession->RefreshScores(); }
						})));
				}
				return Menu.MakeWidget();
			})
		]
		// Type — inline picker.
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 4.f, 0.f)
		[
			SNew(SComboButton)
			.ComboButtonStyle(FAppStyle::Get(), "ComboButton")
			.ToolTipText(LOCTEXT("VarTypeTip", "The variable's value type (Float/Bool/Int are editable inline; others edit in the Details tab)."))
			.ButtonContent()
			[
				SNew(SBox).MinDesiredWidth(48.f)[ SNew(STextBlock).Text(CombatVarTypeLabel(Def.Type)) ]
			]
			.OnGetMenuContent_Lambda([this, DefinitionIndex]()
			{
				FMenuBuilder Menu(true, nullptr);
				const EPaper2DPlusCombatVariableType AllTypes[] = {
					EPaper2DPlusCombatVariableType::Float, EPaper2DPlusCombatVariableType::Bool, EPaper2DPlusCombatVariableType::Int32,
					EPaper2DPlusCombatVariableType::Name, EPaper2DPlusCombatVariableType::String, EPaper2DPlusCombatVariableType::GameplayTag,
					EPaper2DPlusCombatVariableType::GameplayTagContainer, EPaper2DPlusCombatVariableType::Vector2D, EPaper2DPlusCombatVariableType::Vector };
				for (EPaper2DPlusCombatVariableType VarType : AllTypes)
				{
					Menu.AddMenuEntry(CombatVarTypeLabel(VarType), FText::GetEmpty(), FSlateIcon(),
						FUIAction(FExecuteAction::CreateLambda([this, DefinitionIndex, VarType]()
						{
							if (!EditedAsset || !EditedAsset->VariableDefinitions.IsValidIndex(DefinitionIndex)) { return; }
							FScopedTransaction Transaction(LOCTEXT("SetVarType", "Set Variable Type"));
							EditedAsset->Modify();
							EditedAsset->VariableDefinitions[DefinitionIndex].Type = VarType;
							EditedAsset->RebuildVariableBags();
							EditedAsset->MarkPackageDirty();
							RefreshGlobalVariables();
							if (EditorSession.IsValid()) { EditorSession->RefreshScores(); }
						})));
				}
				return Menu.MakeWidget();
			})
		]
		// Value editor.
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(SBox).WidthOverride(96.f)
			[
				BuildVariableValueEditor(DefinitionIndex)
			]
		]
		// Remove.
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4.f, 0.f, 0.f, 0.f)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.ToolTipText(LOCTEXT("RemoveVarTip", "Remove this variable"))
			.OnClicked_Lambda([this, DefinitionIndex]() { RemoveVariable(DefinitionIndex); return FReply::Handled(); })
			[
				SNew(STextBlock).Text(FText::FromString(TEXT("✕")))
			]
		];
}

TSharedRef<SWidget> FCombatProfileAssetEditorToolkit::BuildVariableValueEditor(int32 DefinitionIndex)
{
	if (!EditedAsset || !EditedAsset->VariableDefinitions.IsValidIndex(DefinitionIndex))
	{
		return SNullWidget::NullWidget;
	}
	const FGameplayTag VarTag = EditedAsset->VariableDefinitions[DefinitionIndex].VariableTag;
	const EPaper2DPlusCombatVariableType Type = EditedAsset->VariableDefinitions[DefinitionIndex].Type;

	if (!VarTag.IsValid())
	{
		return SNew(STextBlock)
			.Text(LOCTEXT("ValueNeedsTag", "—"))
			.ColorAndOpacity(FSlateColor(FLinearColor(0.7f, 0.55f, 0.3f)));
	}

	if (Type == EPaper2DPlusCombatVariableType::Float)
	{
		return SNew(SSpinBox<float>)
			.Value_Lambda([this, VarTag]()
			{
				float Value = 0.0f;
				if (EditedAsset) { EditedAsset->TryGetFloatVariable(EditedAsset->GlobalVariables, VarTag, Value); }
				return Value;
			})
			.OnValueCommitted_Lambda([this, VarTag](float NewValue, ETextCommit::Type)
			{
				if (!EditedAsset) { return; }
				FScopedTransaction Transaction(LOCTEXT("SetCombatVarValue", "Set Combat Variable Value"));
				EditedAsset->Modify();
				EditedAsset->GlobalVariables.FindOrAdd(VarTag).FloatValue = NewValue;
				EditedAsset->MarkPackageDirty();
				if (EditorSession.IsValid()) { EditorSession->RefreshScores(); }
			});
	}

	if (Type == EPaper2DPlusCombatVariableType::Bool)
	{
		return SNew(SCheckBox)
			.IsChecked_Lambda([this, VarTag]()
			{
				bool bValue = false;
				if (EditedAsset) { EditedAsset->TryGetBoolVariable(EditedAsset->GlobalVariables, VarTag, bValue); }
				return bValue ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
			})
			.OnCheckStateChanged_Lambda([this, VarTag](ECheckBoxState NewState)
			{
				if (!EditedAsset) { return; }
				FScopedTransaction Transaction(LOCTEXT("SetCombatVarValue", "Set Combat Variable Value"));
				EditedAsset->Modify();
				EditedAsset->GlobalVariables.FindOrAdd(VarTag).BoolValue = (NewState == ECheckBoxState::Checked);
				EditedAsset->MarkPackageDirty();
				if (EditorSession.IsValid()) { EditorSession->RefreshScores(); }
			});
	}

	if (Type == EPaper2DPlusCombatVariableType::Int32)
	{
		return SNew(SSpinBox<int32>)
			.Value_Lambda([this, VarTag]()
			{
				if (EditedAsset)
				{
					if (const FPaper2DPlusCombatVariableValue* Value = EditedAsset->GlobalVariables.Find(VarTag)) { return Value->IntValue; }
				}
				return 0;
			})
			.OnValueCommitted_Lambda([this, VarTag](int32 NewValue, ETextCommit::Type)
			{
				if (!EditedAsset) { return; }
				FScopedTransaction Transaction(LOCTEXT("SetCombatVarValue", "Set Combat Variable Value"));
				EditedAsset->Modify();
				EditedAsset->GlobalVariables.FindOrAdd(VarTag).IntValue = NewValue;
				EditedAsset->MarkPackageDirty();
				if (EditorSession.IsValid()) { EditorSession->RefreshScores(); }
			});
	}

	// Name / String / tag / vector value editing lives in the Details tab (the value map's typed fields).
	return SNew(STextBlock)
		.Text(LOCTEXT("ValueInDetails", "(in Details)"))
		.ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f)));
}

FReply FCombatProfileAssetEditorToolkit::OnAddVariableClicked()
{
	if (!EditedAsset)
	{
		return FReply::Handled();
	}
	{
		FScopedTransaction Transaction(LOCTEXT("AddCombatVariable", "Add Combat Variable"));
		EditedAsset->Modify();
		EditedAsset->VariableDefinitions.Add(FPaper2DPlusCombatVariableDefinition());
		EditedAsset->RebuildVariableBags();
		EditedAsset->MarkPackageDirty();
	}
	RefreshGlobalVariables();
	if (GlobalVariablesExpander.IsValid())
	{
		GlobalVariablesExpander->SetExpanded(true);
	}
	if (DetailsView.IsValid()) { DetailsView->ForceRefresh(); }
	// Tag, type, name and value are now all editable inline on the new row — no Details-tab bounce.
	return FReply::Handled();
}

void FCombatProfileAssetEditorToolkit::RemoveVariable(int32 DefinitionIndex)
{
	if (!EditedAsset || !EditedAsset->VariableDefinitions.IsValidIndex(DefinitionIndex))
	{
		return;
	}
	{
		FScopedTransaction Transaction(LOCTEXT("RemoveCombatVariable", "Remove Combat Variable"));
		EditedAsset->Modify();
		EditedAsset->VariableDefinitions.RemoveAt(DefinitionIndex);
		EditedAsset->RebuildVariableBags();
		EditedAsset->MarkPackageDirty();
	}
	RefreshGlobalVariables();
	if (DetailsView.IsValid()) { DetailsView->ForceRefresh(); }
}

#undef LOCTEXT_NAMESPACE
