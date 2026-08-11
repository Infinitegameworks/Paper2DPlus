// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "EffectProfileAssetEditorToolkit.h"

#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Editor.h"
#include "EffectProfileEditor/EffectProfileDetailsPanel.h"
#include "EffectProfileEditor/EffectProfileLibraryPanel.h"
#include "EffectProfileEditor/EffectProfilePreviewPanel.h"
#include "EffectProfileEditorModel.h"
#include "Framework/Application/SlateApplication.h"
#include "IDetailsView.h"
#include "Paper2DPlusEffectProfileAsset.h"
#include "Paper2DPlusProfileEditorToolbar.h"
#include "PaperFlipbook.h"
#include "ProfileCompletionPanel.h"
#include "ProfileValidationPanel.h"
#include "RelatedProfileBar.h"
#include "PropertyEditorModule.h"
#include "UObject/UObjectGlobals.h" // FCoreUObjectDelegates
#include "UObject/UnrealType.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/SWindow.h"
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
#include "EditorStyleSet.h"
#ifndef FAppStyle
#define FAppStyle FEditorStyle
#define GetAppStyleSetName GetStyleSetName
#endif
#else
#include "Styling/AppStyle.h"
#endif

#define LOCTEXT_NAMESPACE "EffectProfileAssetEditor"

const FName FEffectProfileAssetEditorToolkit::LibraryTabId(TEXT("EffectProfileEditor_Library"));
const FName FEffectProfileAssetEditorToolkit::PreviewTabId(TEXT("EffectProfileEditor_Preview"));
const FName FEffectProfileAssetEditorToolkit::DetailsTabId(TEXT("EffectProfileEditor_Details"));
const FName FEffectProfileAssetEditorToolkit::AdvancedDetailsTabId(TEXT("EffectProfileEditor_AdvancedDetails"));
const FName FEffectProfileAssetEditorToolkit::CompletionTabId(TEXT("EffectProfileEditor_Completion"));
const FName FEffectProfileAssetEditorToolkit::RelatedProfilesTabId(TEXT("EffectProfileEditor_RelatedProfiles"));
// v5 narrows the Library and widens the Preview; the key must change or a saved v4 arrangement
// would restore the old coefficients and the change would look like it never landed.
const FName FEffectProfileAssetEditorToolkit::WorkspaceLayoutId(
	TEXT("EffectProfileAssetEditor_Layout_v5_NarrowLibrary"));

FEffectProfileAssetEditorToolkit::~FEffectProfileAssetEditorToolkit()
{
	if (const TSharedPtr<SWindow> Window = ValidationWindow.Pin())
	{
		Window->RequestDestroyWindow();
	}
	FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(PropertyChangedHandle);
	if (FModuleManager::Get().IsModuleLoaded(TEXT("AssetRegistry")))
	{
		FAssetRegistryModule& RegistryModule =
			FModuleManager::GetModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		RegistryModule.Get().OnAssetAdded().Remove(AssetAddedHandle);
		RegistryModule.Get().OnAssetRemoved().Remove(AssetRemovedHandle);
		RegistryModule.Get().OnAssetUpdated().Remove(AssetUpdatedHandle);
		RegistryModule.Get().OnAssetRenamed().Remove(AssetRenamedHandle);
		RegistryModule.Get().OnFilesLoaded().Remove(AssetFilesLoadedHandle);
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
		RegistryModule.Get().OnKnownGathersComplete().Remove(AssetKnownGathersCompleteHandle);
#endif
	}
	if (GEditor)
	{
		GEditor->UnregisterForUndo(this);
	}
}

void FEffectProfileAssetEditorToolkit::InitEditor(
	EToolkitMode::Type Mode,
	const TSharedPtr<IToolkitHost>& InitToolkitHost,
	UPaper2DPlusEffectProfileAsset* InAsset)
{
	EditedAsset = InAsset;
	Model = MakeShared<FEffectProfileEditorModel>();
	Model->Initialize(InAsset);

	if (GEditor)
	{
		GEditor->RegisterForUndo(this);
	}
	PropertyChangedHandle = FCoreUObjectDelegates::OnObjectPropertyChanged.AddRaw(
		this, &FEffectProfileAssetEditorToolkit::HandleObjectPropertyChanged);

	FAssetRegistryModule& RegistryModule =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	AssetAddedHandle = RegistryModule.Get().OnAssetAdded().AddRaw(
		this, &FEffectProfileAssetEditorToolkit::HandleAssetRegistryChanged);
	AssetRemovedHandle = RegistryModule.Get().OnAssetRemoved().AddRaw(
		this, &FEffectProfileAssetEditorToolkit::HandleAssetRegistryChanged);
	AssetUpdatedHandle = RegistryModule.Get().OnAssetUpdated().AddRaw(
		this, &FEffectProfileAssetEditorToolkit::HandleAssetRegistryChanged);
	AssetRenamedHandle = RegistryModule.Get().OnAssetRenamed().AddRaw(
		this, &FEffectProfileAssetEditorToolkit::HandleAssetRenamed);
	AssetFilesLoadedHandle = RegistryModule.Get().OnFilesLoaded().AddRaw(
		this, &FEffectProfileAssetEditorToolkit::HandleAssetRegistryFilesLoaded);
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
	AssetKnownGathersCompleteHandle = RegistryModule.Get().OnKnownGathersComplete().AddRaw(
		this, &FEffectProfileAssetEditorToolkit::HandleAssetRegistryFilesLoaded);
#endif

	FPropertyEditorModule& PropertyModule =
		FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
	FDetailsViewArgs DetailsArgs;
	DetailsArgs.bAllowSearch = true;
	DetailsArgs.bHideSelectionTip = true;
	DetailsArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	AdvancedDetailsView = PropertyModule.CreateDetailView(DetailsArgs);
	AdvancedDetailsView->SetObject(EditedAsset);

	// Character Profile visual grammar: browse left, inspect the selected animation in the center, edit its
	// curated metadata at right. Validation is action-driven in a modeless shared panel; Advanced Details stays
	// a closed compatibility tab and consumes no default workspace space.
	const TSharedRef<FTabManager::FLayout> Layout = BuildDefaultLayout();

	Paper2DPlusProfileEditorToolbar::Install(
		GetToolMenuToolbarName(),
		GetToolkitCommands());

	FAssetEditorToolkit::InitAssetEditor(
		Mode,
		InitToolkitHost,
		TEXT("EffectProfileAssetEditorApp"),
		Layout,
		true,
		true,
		InAsset);
}

TSharedRef<FTabManager::FLayout> FEffectProfileAssetEditorToolkit::BuildDefaultLayout()
{
	return
		FTabManager::NewLayout(WorkspaceLayoutId)
		->AddArea(
			FTabManager::NewPrimaryArea()
			->SetOrientation(Orient_Horizontal)
			// The Library is a searchable list of rows: it needs enough width for a name and its
			// descriptors, not a third of the workspace. The reclaimed space goes to the Preview,
			// which is the surface designers actually study.
			->Split(
				FTabManager::NewStack()
				->SetSizeCoefficient(0.24f)
				->AddTab(LibraryTabId, ETabState::OpenedTab)
				->SetForegroundTab(LibraryTabId))
			->Split(
				FTabManager::NewStack()
				->SetSizeCoefficient(0.50f)
				->AddTab(PreviewTabId, ETabState::OpenedTab)
				->SetForegroundTab(PreviewTabId))
			->Split(
				FTabManager::NewSplitter()
				->SetOrientation(Orient_Vertical)
				->SetSizeCoefficient(0.26f)
				->Split(
					FTabManager::NewStack()
					->SetSizeCoefficient(0.64f)
					->AddTab(DetailsTabId, ETabState::OpenedTab)
					->AddTab(AdvancedDetailsTabId, ETabState::ClosedTab)
					->SetForegroundTab(DetailsTabId))
				->Split(
					FTabManager::NewStack()
					->SetSizeCoefficient(0.36f)
					->AddTab(CompletionTabId, ETabState::OpenedTab)
					->AddTab(RelatedProfilesTabId, ETabState::OpenedTab)
					->SetForegroundTab(CompletionTabId))));
}

bool FEffectProfileAssetEditorToolkit::ValidateDesignerWorkspaceForTests(
	int32 ExpectedFrameCount,
	FString& OutReason)
{
	OutReason.Reset();
	const TSharedPtr<FTabManager> Manager = GetTabManager();
	if (!Manager.IsValid())
	{
		OutReason = TEXT("Effect toolkit has no tab manager");
		return false;
	}
	TArray<TSharedPtr<SDockTab>> RequiredTabs;
	RequiredTabs.Reserve(5);
	for (const FName RequiredTab : {
		LibraryTabId, PreviewTabId, DetailsTabId, CompletionTabId, RelatedProfilesTabId })
	{
		const TSharedPtr<SDockTab> LiveTab = Manager->FindExistingLiveTab(RequiredTab);
		if (!LiveTab.IsValid())
		{
			OutReason = FString::Printf(
				TEXT("Effect workspace did not spawn required tab %s"),
				*RequiredTab.ToString());
			return false;
		}
		RequiredTabs.Add(LiveTab);
	}
	if (Manager->FindExistingLiveTab(AdvancedDetailsTabId).IsValid())
	{
		OutReason = TEXT("Advanced Details was visible in the default Effect workspace");
		return false;
	}

	// The JSON test protects the authored default tree. This live geometry check protects the actual
	// restored workspace the designer is looking at, so a stale/drifted local layout cannot earn a
	// semantic screenshot PASS while presenting different proportions or moving Details away from right.
	const FVector2D LibraryPosition(RequiredTabs[0]->GetCachedGeometry().GetAbsolutePosition());
	const FVector2D PreviewPosition(RequiredTabs[1]->GetCachedGeometry().GetAbsolutePosition());
	const FVector2D DetailsPosition(RequiredTabs[2]->GetCachedGeometry().GetAbsolutePosition());
	if (!(LibraryPosition.X < PreviewPosition.X && PreviewPosition.X < DetailsPosition.X))
	{
		OutReason = TEXT("Effect workspace was not ordered Library / Preview / right Details");
		return false;
	}
	const TSharedPtr<SWindow> HostWindow = FSlateApplication::IsInitialized()
		? FSlateApplication::Get().FindWidgetWindow(RequiredTabs[0].ToSharedRef())
		: nullptr;
	if (!HostWindow.IsValid())
	{
		OutReason = TEXT("Effect workspace had no live host window");
		return false;
	}

	// SDockTab local sizes describe tab content geometry and can be normalized by the docking stack even
	// when sibling splitters have different widths. Adjacent absolute left edges plus the host-window right
	// edge are the stable live column boundaries (and match what the screenshot actually presents).
	const float WindowRight = HostWindow->GetPositionInScreen().X + HostWindow->GetSizeInScreen().X;
	const float LiveWidths[3] =
	{
		PreviewPosition.X - LibraryPosition.X,
		DetailsPosition.X - PreviewPosition.X,
		WindowRight - DetailsPosition.X
	};
	const float TotalWidth = LiveWidths[0] + LiveWidths[1] + LiveWidths[2];
	if (LiveWidths[0] <= 1.0f || LiveWidths[1] <= 1.0f || LiveWidths[2] <= 1.0f
		|| TotalWidth <= 3.0f)
	{
		OutReason = TEXT("Effect workspace live column geometry was not settled");
		return false;
	}

	const float LiveRatios[3] =
	{
		LiveWidths[0] / TotalWidth,
		LiveWidths[1] / TotalWidth,
		LiveWidths[2] / TotalWidth
	};
	// The expectation mirrors EffectProfileAssetEditor_Layout_v5_NarrowLibrary's authored
	// coefficients (0.24 / 0.50 / 0.26) — keep the two in lockstep when the layout changes. The
	// pre-v5 36/38/26 expectation survived the layout bump and failed every capture of a correct
	// workspace.
	const float ExpectedRatios[3] = { 0.24f, 0.50f, 0.26f };
	for (int32 ColumnIndex = 0; ColumnIndex < 3; ++ColumnIndex)
	{
		if (!FMath::IsNearlyEqual(LiveRatios[ColumnIndex], ExpectedRatios[ColumnIndex], 0.04f))
		{
			OutReason = FString::Printf(
				TEXT("Effect live column ratios were %.3f / %.3f / %.3f; expected 0.24 / 0.50 / 0.26"),
				LiveRatios[0], LiveRatios[1], LiveRatios[2]);
			return false;
		}
	}
	if (!LibraryPanel.IsValid() || !PreviewPanel.IsValid() || !DetailsPanel.IsValid())
	{
		OutReason = TEXT("Effect workspace panels were not all constructed");
		return false;
	}
	if (!PreviewPanel->HasSelectionForTests())
	{
		OutReason = TEXT("Effect Preview did not resolve the selected flipbook");
		return false;
	}
	if (PreviewPanel->GetFrameStripFrameCountForTests() != ExpectedFrameCount)
	{
		OutReason = FString::Printf(
			TEXT("Effect frame strip had %d cells; expected %d"),
			PreviewPanel->GetFrameStripFrameCountForTests(),
			ExpectedFrameCount);
		return false;
	}
	if (PreviewPanel->GetPlaybackStateForTests().IsPlaying()
		|| PreviewPanel->HasPlaybackTimerForTests())
	{
		OutReason = TEXT("Effect Preview was not initially paused");
		return false;
	}
	if (PreviewPanel->CountDescendantWidgetsForTests(TEXT("SSlider")) != 0)
	{
		OutReason = TEXT("Effect Preview exposed a draggable transport slider");
		return false;
	}
	// Open Flipbook is the only button in Preview. More buttons means transport chrome has returned.
	if (PreviewPanel->CountDescendantWidgetsForTests(TEXT("SButton")) != 1)
	{
		OutReason = TEXT("Effect Preview exposed unexpected button chrome");
		return false;
	}
	return true;
}

void FEffectProfileAssetEditorToolkit::RegisterTabSpawners(
	const TSharedRef<FTabManager>& InTabManager)
{
	WorkspaceMenuCategory = InTabManager->AddLocalWorkspaceMenuCategory(
		LOCTEXT("WorkspaceMenu", "Effect Profile Editor"));
	FAssetEditorToolkit::RegisterTabSpawners(InTabManager);

	InTabManager->RegisterTabSpawner(
		LibraryTabId,
		FOnSpawnTab::CreateSP(this, &FEffectProfileAssetEditorToolkit::SpawnTab_Library))
		.SetDisplayName(LOCTEXT("LibraryTab", "Library"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef())
		.SetIcon(FSlateIcon(FAppStyle::Get().GetStyleSetName(), TEXT("LevelEditor.Tabs.Outliner")));
	InTabManager->RegisterTabSpawner(
		PreviewTabId,
		FOnSpawnTab::CreateSP(this, &FEffectProfileAssetEditorToolkit::SpawnTab_Preview))
		.SetDisplayName(LOCTEXT("PreviewTab", "Preview"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef())
		.SetIcon(FSlateIcon(FAppStyle::Get().GetStyleSetName(), TEXT("Icons.Play")));
	InTabManager->RegisterTabSpawner(
		DetailsTabId,
		FOnSpawnTab::CreateSP(this, &FEffectProfileAssetEditorToolkit::SpawnTab_Details))
		.SetDisplayName(LOCTEXT("DetailsTab", "Details"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef())
		.SetIcon(FSlateIcon(FAppStyle::Get().GetStyleSetName(), TEXT("LevelEditor.Tabs.Details")));
	InTabManager->RegisterTabSpawner(
		AdvancedDetailsTabId,
		FOnSpawnTab::CreateSP(this, &FEffectProfileAssetEditorToolkit::SpawnTab_AdvancedDetails))
		.SetDisplayName(LOCTEXT("AdvancedDetailsTab", "Advanced Details"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef())
		.SetIcon(FSlateIcon(FAppStyle::Get().GetStyleSetName(), TEXT("Icons.Settings")));
	InTabManager->RegisterTabSpawner(
		CompletionTabId,
		FOnSpawnTab::CreateSP(this, &FEffectProfileAssetEditorToolkit::SpawnTab_Completion))
		.SetDisplayName(LOCTEXT("CompletionTab", "Completion"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef())
		.SetIcon(FSlateIcon(FAppStyle::Get().GetStyleSetName(), TEXT("Icons.Check")));
	InTabManager->RegisterTabSpawner(
		RelatedProfilesTabId,
		FOnSpawnTab::CreateSP(this, &FEffectProfileAssetEditorToolkit::SpawnTab_RelatedProfiles))
		.SetDisplayName(LOCTEXT("RelatedProfilesTab", "Related Profiles"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef())
		.SetIcon(FSlateIcon(FAppStyle::Get().GetStyleSetName(), TEXT("Icons.Link")));
}

void FEffectProfileAssetEditorToolkit::UnregisterTabSpawners(
	const TSharedRef<FTabManager>& InTabManager)
{
	FAssetEditorToolkit::UnregisterTabSpawners(InTabManager);
	InTabManager->UnregisterTabSpawner(LibraryTabId);
	InTabManager->UnregisterTabSpawner(PreviewTabId);
	InTabManager->UnregisterTabSpawner(DetailsTabId);
	InTabManager->UnregisterTabSpawner(AdvancedDetailsTabId);
	InTabManager->UnregisterTabSpawner(CompletionTabId);
	InTabManager->UnregisterTabSpawner(RelatedProfilesTabId);
}

TSharedRef<SDockTab> FEffectProfileAssetEditorToolkit::SpawnTab_Library(
	const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.Label(LOCTEXT("LibraryLabel", "Library"))
		[
			SAssignNew(LibraryPanel, SEffectProfileLibraryPanel)
			.Model(Model)
			.OnOpenValidation(FSimpleDelegate::CreateSP(
				this, &FEffectProfileAssetEditorToolkit::OpenValidation))
		];
}

TSharedRef<SDockTab> FEffectProfileAssetEditorToolkit::SpawnTab_Preview(
	const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.Label(LOCTEXT("PreviewLabel", "Preview"))
		[
			SAssignNew(PreviewPanel, SEffectProfilePreviewPanel)
			.Model(Model)
		];
}

TSharedRef<SDockTab> FEffectProfileAssetEditorToolkit::SpawnTab_Details(
	const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.Label(LOCTEXT("DetailsLabel", "Details"))
		[
			SAssignNew(DetailsPanel, SEffectProfileDetailsPanel)
			.Model(Model)
			.OnOpenAdvancedDetails(FSimpleDelegate::CreateSP(
				this, &FEffectProfileAssetEditorToolkit::OpenAdvancedDetails))
		];
}

TSharedRef<SDockTab> FEffectProfileAssetEditorToolkit::SpawnTab_AdvancedDetails(
	const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.Label(LOCTEXT("AdvancedDetailsLabel", "Advanced Details"))
		[
			AdvancedDetailsView.ToSharedRef()
		];
}

TSharedRef<SDockTab> FEffectProfileAssetEditorToolkit::SpawnTab_Completion(
	const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.Label(LOCTEXT("CompletionLabel", "Completion"))
		[
			SNew(SProfileCompletionPanel)
			.Asset(EditedAsset)
			.ProfileKind(EProfileCompletionKind::Effect)
		];
}

TSharedRef<SDockTab> FEffectProfileAssetEditorToolkit::SpawnTab_RelatedProfiles(
	const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.Label(LOCTEXT("RelatedProfilesLabel", "Related Profiles"))
		[
			SNew(SRelatedProfileBar).EditedAsset(EditedAsset)
		];
}

void FEffectProfileAssetEditorToolkit::OpenAdvancedDetails()
{
	if (const TSharedPtr<FTabManager> Manager = GetTabManager())
	{
		Manager->TryInvokeTab(AdvancedDetailsTabId);
	}
}

void FEffectProfileAssetEditorToolkit::OpenValidation()
{
	if (const TSharedPtr<SWindow> Existing = ValidationWindow.Pin())
	{
		Existing->BringToFront();
		return;
	}
	if (!FSlateApplication::IsInitialized()) return;

	const TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(LOCTEXT("ValidationWindowTitle", "Effect Profile Validation"))
		.ClientSize(FVector2D(720.0f, 520.0f))
		.SupportsMaximize(true)
		.SupportsMinimize(false);
	Window->SetContent(
		SNew(SProfileValidationPanel)
		.Asset(EditedAsset)
		.OnIssueActivated(FOnPaper2DPlusValidationIssueActivated::CreateSP(
			this, &FEffectProfileAssetEditorToolkit::HandleValidationIssueActivated)));
	ValidationWindow = Window;
	FSlateApplication::Get().AddWindow(Window);
}

void FEffectProfileAssetEditorToolkit::HandleValidationIssueActivated(
	const FPaper2DPlusValidationIssue& Issue)
{
	if (!Issue.ToolTarget.IsSet())
	{
		return;
	}
	if (Model.IsValid() && !Issue.ToolTarget->ItemIdentity.IsEmpty())
	{
		Model->SelectEffectIdentityString(Issue.ToolTarget->ItemIdentity);
	}
	const FName TargetTab = Issue.ToolTarget->TabId.IsNone()
		? DetailsTabId : Issue.ToolTarget->TabId;
	if (const TSharedPtr<FTabManager> Manager = GetTabManager())
	{
		Manager->TryInvokeTab(TargetTab);
	}
	if (TargetTab == DetailsTabId && DetailsPanel.IsValid()
		&& !Issue.ToolTarget->Field.IsNone())
	{
		DetailsPanel->FocusField(Issue.ToolTarget->Field);
	}
}

void FEffectProfileAssetEditorToolkit::HandleAssetRegistryChanged(const FAssetData& AssetData)
{
	HandleAssetRenamed(AssetData, FString());
}

void FEffectProfileAssetEditorToolkit::HandleAssetRenamed(
	const FAssetData& AssetData,
	const FString& OldObjectPath)
{
	if (!Model.IsValid() || !EditedAsset)
	{
		return;
	}

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
	const FSoftObjectPath NewObjectPath = AssetData.ToSoftObjectPath();
#else
	const FSoftObjectPath NewObjectPath = AssetData.GetSoftObjectPath();
#endif
	const FSoftObjectPath PreviousObjectPath(OldObjectPath);
	const bool bProfileReferencesChangedAsset = EditedAsset->Effects.ContainsByPredicate(
		[&NewObjectPath, &PreviousObjectPath](const FPaper2DPlusEffectProfileEntry& Entry)
		{
			const FSoftObjectPath EntryPath = Entry.GetEffectFlipbookPath();
			const FSoftObjectPath CanonicalPath =
				UPaper2DPlusEffectProfileAsset::GetCanonicalEffectFlipbookPathNoLoad(EntryPath);
			return EntryPath == NewObjectPath || EntryPath == PreviousObjectPath
				|| CanonicalPath == NewObjectPath || CanonicalPath == PreviousObjectPath;
		});
	if (bProfileReferencesChangedAsset)
	{
		Model->RefreshFromAsset();
	}
}

void FEffectProfileAssetEditorToolkit::HandleAssetRegistryFilesLoaded()
{
	if (Model.IsValid() && EditedAsset)
	{
		// Unknown soft-path validation results are intentionally provisional during discovery.
		Model->RefreshFromAsset();
	}
}

void FEffectProfileAssetEditorToolkit::HandleObjectPropertyChanged(
	UObject* Object,
	FPropertyChangedEvent& Event)
{
	if (Object == EditedAsset && Event.ChangeType != EPropertyChangeType::Interactive
		&& Model.IsValid())
	{
		Model->RefreshAfterExternalMutation();
	}
}

void FEffectProfileAssetEditorToolkit::PostUndo(bool bSuccess)
{
	if (!bSuccess)
	{
		return;
	}
	if (Model.IsValid())
	{
		Model->RefreshAfterExternalMutation();
	}
	if (AdvancedDetailsView.IsValid())
	{
		AdvancedDetailsView->ForceRefresh();
	}
}

void FEffectProfileAssetEditorToolkit::PostRedo(bool bSuccess)
{
	PostUndo(bSuccess);
}

FName FEffectProfileAssetEditorToolkit::GetToolkitFName() const
{
	return FName(TEXT("EffectProfileAssetEditor"));
}

FText FEffectProfileAssetEditorToolkit::GetBaseToolkitName() const
{
	return LOCTEXT("ToolkitName", "Paper2D+ Effect Profile Editor");
}

FString FEffectProfileAssetEditorToolkit::GetWorldCentricTabPrefix() const
{
	return LOCTEXT("WorldCentricTabPrefix", "Effect Profile ").ToString();
}

FLinearColor FEffectProfileAssetEditorToolkit::GetWorldCentricTabColorScale() const
{
	return FLinearColor(0.31f, 0.82f, 0.86f, 1.0f);
}

#undef LOCTEXT_NAMESPACE
