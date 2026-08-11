// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "ProfileToolsWindow.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "CharacterProfileAssetEditor.h"
#include "CharacterProfileEditorModel.h"
#include "Editor.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/IConsoleManager.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "ProfileDetailsPanel.h"
#include "ProfileTools/SProfileCharacterSizingTool.h"
#include "ProfileTools/SProfileReExtractTool.h"
#include "ProfileTools/SProfileSpriteBoundsTool.h"
#include "ProfileValidationPanel.h"
#include "Styling/AppStyle.h"
#include "UObject/UObjectGlobals.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "Paper2DPlusProfileTools"

const FName SProfileToolsWindow::ToolId_CharacterData(TEXT("CharacterData"));
const FName SProfileToolsWindow::ToolId_CharacterSizing(TEXT("CharacterSizing"));
const FName SProfileToolsWindow::ToolId_SpriteBounds(TEXT("SpriteBounds"));
const FName SProfileToolsWindow::ToolId_ReExtract(TEXT("ReExtract"));
const FName SProfileToolsWindow::ToolId_Validation(TEXT("Validation"));
const FName SProfileToolsWindow::ToolId_PaperZDSequences(TEXT("PaperZDSequences"));

namespace ProfileToolsWindow_Internal
{
	/** Single-instance tracking, mirroring the bulk extractor: the window for focusing, the content
	 *  for selecting a tool on an already-open instance instead of destroy-and-replace. */
	static TWeakPtr<SWindow> ActiveWindow;
	static TWeakPtr<SProfileToolsWindow> ActiveContent;

	/** Scripting / ECABridge seam. Retargeted from the retired Character Data editor; the old command
	 *  name is kept so existing scripts and tour steps do not break, with a clearer alias beside it. */
	void OpenProfileToolsConsole(const TArray<FString>& Args)
	{
		UPaper2DPlusCharacterProfileAsset* Asset = nullptr;
		if (Args.Num() > 0)
		{
			Asset = LoadObject<UPaper2DPlusCharacterProfileAsset>(nullptr, *Args[0]);
		}
		if (!Asset)
		{
			IAssetRegistry& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
			TArray<FAssetData> Found;
			// UClass::GetClassPathName + the FTopLevelAssetPath GetAssetsByClass overload are 5.1+;
			// 5.0 has only the FName form (the cross-version harness caught it).
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 1)
			AR.GetAssetsByClass(UPaper2DPlusCharacterProfileAsset::StaticClass()->GetClassPathName(), Found);
#else
			AR.GetAssetsByClass(UPaper2DPlusCharacterProfileAsset::StaticClass()->GetFName(), Found);
#endif
			if (Found.Num() > 0)
			{
				Asset = Cast<UPaper2DPlusCharacterProfileAsset>(Found[0].GetAsset());
			}
		}
		if (!Asset)
		{
			UE_LOG(LogTemp, Warning, TEXT("Paper2DPlus.OpenProfileTools: no Character Profile found"));
			return;
		}

		const FName ToolId = Args.Num() > 1 ? FName(*Args[1]) : NAME_None;
		SProfileToolsWindow::OpenProfileTools(Asset, ToolId);
		UE_LOG(LogTemp, Display, TEXT("Paper2DPlus.OpenProfileTools: opened %s"), *Asset->GetPathName());
	}
}

static FAutoConsoleCommand GOpenProfileToolsCmd(
	TEXT("Paper2DPlus.OpenProfileTools"),
	TEXT("Open the Profile Tools window on a Character Profile (object path optional; default = first found). Optional second argument selects a tool by id: CharacterData, Validation, PaperZDSequences."),
	FConsoleCommandWithArgsDelegate::CreateStatic(&ProfileToolsWindow_Internal::OpenProfileToolsConsole));

static FAutoConsoleCommand GOpenCharacterDataCmd(
	TEXT("Paper2DPlus.OpenCharacterData"),
	TEXT("Retained alias for Paper2DPlus.OpenProfileTools -- the Character Data editor it originally opened was replaced by the Profile Tools window."),
	FConsoleCommandWithArgsDelegate::CreateStatic(&ProfileToolsWindow_Internal::OpenProfileToolsConsole));

void SProfileToolsWindow::Construct(const FArguments& InArgs)
{
	Profile = InArgs._Profile;

	// The rehosted panes derive their asset from a model and render empty without one. Deliberately
	// does NOT claim FCharacterProfileEditorModel::GActiveEditorModel: that static is the SwitchTab
	// console seam and belongs to the main Character Profile editor. The model's destructor only
	// resets the static if it owns it, so this secondary model never clears the main binding.
	EditorModel = MakeShared<FCharacterProfileEditorModel>();
	if (UPaper2DPlusCharacterProfileAsset* Asset = Profile.Get())
	{
		EditorModel->InitializeFromAsset(Asset);
	}

	BuildTools();

	// A bare SWindow gets none of the asset-lifetime services InitAssetEditor gave the retired
	// toolkit, so the window watches for its own subject disappearing.
	AssetsPreDeleteHandle = FEditorDelegates::OnAssetsPreDelete.AddSP(
		this, &SProfileToolsWindow::HandleAssetsPreDelete);
	ObjectsReplacedHandle = FCoreUObjectDelegates::OnObjectsReplaced.AddSP(
		this, &SProfileToolsWindow::HandleObjectsReplaced);

	TSharedRef<SWidgetSwitcher> Switcher = SNew(SWidgetSwitcher);
	for (const TSharedPtr<FProfileToolDescriptor>& Tool : Tools)
	{
		++SurfaceBuildCount;
		Switcher->AddSlot()
		[
			Tool->MakeSurface()
		];
	}
	ToolSwitcher = Switcher;

	ChildSlot
	[
		SNew(SSplitter)
		.Orientation(Orient_Horizontal)

		+ SSplitter::Slot()
		.Value(0.24f)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
			.Padding(2.0f)
			[
				SAssignNew(ToolListView, SListView<TSharedPtr<FProfileToolDescriptor>>)
				.ListItemsSource(&Tools)
				.SelectionMode(ESelectionMode::Single)
				.OnGenerateRow(this, &SProfileToolsWindow::MakeToolRow)
				.OnSelectionChanged(this, &SProfileToolsWindow::HandleToolSelectionChanged)
			]
		]

		// The switcher goes INSIDE the splitter slot. Never host the splitter inside a switcher.
		+ SSplitter::Slot()
		.Value(0.76f)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
			.Padding(4.0f)
			[
				Switcher
			]
		]
	];

	// Select on open so the right pane is never blank. An unknown id falls back to the first entry.
	if (!SelectToolById(InArgs._InitialToolId))
	{
		SelectTool(Tools.Num() > 0 ? 0 : INDEX_NONE);
	}
}

SProfileToolsWindow::~SProfileToolsWindow()
{
	FEditorDelegates::OnAssetsPreDelete.Remove(AssetsPreDeleteHandle);
	FCoreUObjectDelegates::OnObjectsReplaced.Remove(ObjectsReplacedHandle);
}

void SProfileToolsWindow::BuildTools()
{
	Tools.Reset();

	TWeakPtr<FCharacterProfileEditorModel> WeakModel = EditorModel;

	// Character Data -- the retired toolkit's Character tab, rehosted unchanged. Its two halves are
	// replaced separately (Sprite Bounds by the Sprite Bounds tool, Relative Transform by Character
	// Sizing), so it stays until both exist rather than being deleted with the toolkit and stranding
	// them in between.
	{
		TSharedRef<FProfileToolDescriptor> Tool = MakeShared<FProfileToolDescriptor>();
		Tool->Id = ToolId_CharacterData;
		Tool->Label = LOCTEXT("CharacterDataTool", "Character Data");
		Tool->Tooltip = LOCTEXT("CharacterDataToolTip",
			"Sprite Bounds health and the profile's Relative Transform.");
		Tool->Icon = FSlateIcon(FAppStyle::Get().GetStyleSetName(), "LevelEditor.Tabs.Details");
		Tool->MakeSurface = [WeakModel]() -> TSharedRef<SWidget>
		{
			return SNew(SProfileDetailsPanel)
				.Model(WeakModel.Pin())
				.PaneMode(EProfileDetailsPaneMode::Character);
		};
		Tools.Add(Tool);
	}

	// Character Sizing -- the headline workflow: size against the capsule without a Blueprint.
	{
		TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset> WeakProfile = Profile;
		TSharedRef<FProfileToolDescriptor> Tool = MakeShared<FProfileToolDescriptor>();
		Tool->Id = ToolId_CharacterSizing;
		Tool->Label = LOCTEXT("CharacterSizingTool", "Character Sizing");
		Tool->Tooltip = LOCTEXT("CharacterSizingToolTip",
			"Size the character against its gameplay capsule, inside the editor.");
		Tool->Icon = FSlateIcon(FAppStyle::Get().GetStyleSetName(), "Icons.Transform");
		Tool->MakeSurface = [WeakProfile]() -> TSharedRef<SWidget>
		{
			return SNew(SProfileCharacterSizingTool).Profile(WeakProfile);
		};
		Tools.Add(Tool);
	}

	// Sprite Bounds -- verification only. Its attention rows route into Re-extract, which is why the
	// two are built together and why the window (not either tool) owns the hop between them.
	{
		TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset> WeakProfile = Profile;
		TSharedRef<FProfileToolDescriptor> Tool = MakeShared<FProfileToolDescriptor>();
		Tool->Id = ToolId_SpriteBounds;
		Tool->Label = LOCTEXT("SpriteBoundsTool", "Sprite Bounds");
		Tool->Tooltip = LOCTEXT("SpriteBoundsToolTip",
			"Check every frame's source region against its animation's uniform target. Reports only -- the fix is Re-extract.");
		Tool->Icon = FSlateIcon(FAppStyle::Get().GetStyleSetName(), "Icons.Search");
		Tool->MakeSurface = [this, WeakProfile]() -> TSharedRef<SWidget>
		{
			return SAssignNew(SpriteBoundsTool, SProfileSpriteBoundsTool)
				.Profile(WeakProfile)
				.OnRouteToReExtract(FOnProfileSpriteBoundsRouteToReExtract::CreateSP(
					this, &SProfileToolsWindow::HandleRouteToReExtract));
		};
		Tools.Add(Tool);
	}

	// Re-extract -- the manual door to the existing rollback-safe re-extraction path.
	{
		TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset> WeakProfile = Profile;
		TSharedRef<FProfileToolDescriptor> Tool = MakeShared<FProfileToolDescriptor>();
		Tool->Id = ToolId_ReExtract;
		Tool->Label = LOCTEXT("ReExtractTool", "Re-extract");
		Tool->Tooltip = LOCTEXT("ReExtractToolTip",
			"Re-cut sprites from their source texture using the settings stored at first extraction.");
		Tool->Icon = FSlateIcon(FAppStyle::Get().GetStyleSetName(), "Icons.Refresh");
		Tool->MakeSurface = [this, WeakProfile]() -> TSharedRef<SWidget>
		{
			return SAssignNew(ReExtractTool, SProfileReExtractTool)
				.Profile(WeakProfile);
		};
		Tools.Add(Tool);
	}

	// Validation -- hosts the SHARED panel, in its docked shape rather than its modeless one.
	// RunInitially(false) so merely opening the window costs no validation run, and
	// RefreshOnObservedChanges(false) because this tool stays alive across tool switches and would
	// otherwise re-validate on every observed change for the whole life of the window.
	{
		TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset> WeakProfile = Profile;
		TSharedRef<FProfileToolDescriptor> Tool = MakeShared<FProfileToolDescriptor>();
		Tool->Id = ToolId_Validation;
		Tool->Label = LOCTEXT("ValidationTool", "Validation");
		Tool->Tooltip = LOCTEXT("ValidationToolTip",
			"Check the whole Character Profile. Validation is read-only and never changes the asset.");
		Tool->Icon = FSlateIcon(FAppStyle::Get().GetStyleSetName(), "Icons.Check");
		Tool->MakeSurface = [WeakProfile]() -> TSharedRef<SWidget>
		{
			return SNew(SProfileValidationPanel)
				.Asset_Lambda([WeakProfile]() -> UObject* { return WeakProfile.Get(); })
				.RunInitially(false)
				.RefreshOnObservedChanges(false)
				// Activation still has to navigate the Character Profile editor to the offending
				// tool tab. The panel no longer lives inside that editor, so the route back is an
				// explicit lookup; with no editor open there is simply nothing to navigate.
				.OnIssueActivated(FOnPaper2DPlusValidationIssueActivated::CreateLambda(
					[WeakProfile](const FPaper2DPlusValidationIssue& Issue)
					{
						UPaper2DPlusCharacterProfileAsset* Asset = WeakProfile.Get();
						if (!Asset || !GEditor)
						{
							return;
						}
						UAssetEditorSubsystem* Subsystem =
							GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
						if (!Subsystem)
						{
							return;
						}
						IAssetEditorInstance* Instance =
							Subsystem->FindEditorForAsset(Asset, /*bFocusIfOpen=*/true);
						// Identity-checked before the downcast: FindEditorForAsset is typed only as
						// IAssetEditorInstance, and casting whatever comes back would be a silent
						// wrong-type deref the day anything else registers for this asset.
						if (Instance && Instance->GetEditorName() == FName("CharacterProfileAssetEditor"))
						{
							static_cast<FCharacterProfileAssetEditorToolkit*>(Instance)
								->HandleValidationIssueActivated(Issue);
						}
					}));
		};
		Tools.Add(Tool);
	}

#if WITH_PAPERZD
	// Absent rather than disabled when PaperZD is not compiled in, so the list stays contiguous.
	{
		TSharedRef<FProfileToolDescriptor> Tool = MakeShared<FProfileToolDescriptor>();
		Tool->Id = ToolId_PaperZDSequences;
		Tool->Label = LOCTEXT("PaperZDSequencesTool", "PaperZD Sequences");
		Tool->Tooltip = LOCTEXT("PaperZDSequencesToolTip",
			"PaperZD source, sequence creation, matching, and health.");
		Tool->Icon = FSlateIcon(FAppStyle::Get().GetStyleSetName(), "Icons.Play");
		Tool->MakeSurface = [WeakModel]() -> TSharedRef<SWidget>
		{
			return SNew(SProfileDetailsPanel)
				.Model(WeakModel.Pin())
				.PaneMode(EProfileDetailsPaneMode::PaperZDSequences);
		};
		Tools.Add(Tool);
	}
#endif
}

TSharedRef<ITableRow> SProfileToolsWindow::MakeToolRow(
	TSharedPtr<FProfileToolDescriptor> Item,
	const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(STableRow<TSharedPtr<FProfileToolDescriptor>>, OwnerTable)
		.ToolTipText(Item.IsValid() ? Item->Tooltip : FText::GetEmpty())
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(6.0f, 4.0f, 6.0f, 4.0f)
			[
				SNew(SImage)
				.Image(Item.IsValid() ? Item->Icon.GetIcon() : FAppStyle::Get().GetDefaultBrush())
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			.Padding(0.0f, 4.0f, 6.0f, 4.0f)
			[
				SNew(STextBlock)
				.Text(Item.IsValid() ? Item->Label : FText::GetEmpty())
			]
		];
}

void SProfileToolsWindow::HandleToolSelectionChanged(
	TSharedPtr<FProfileToolDescriptor> Item,
	ESelectInfo::Type SelectInfo)
{
	if (bSelectingTool)
	{
		return;
	}
	SelectTool(Tools.IndexOfByKey(Item));
}

void SProfileToolsWindow::SelectTool(int32 Index)
{
	// Same-index no-op, INCLUDING an invalid index. The list mirrors the selection back through
	// OnSelectionChanged, so a guard that only no-ops on valid indices lets an out-of-range value
	// fall through and re-enter without end.
	if (Index == SelectedToolIndex)
	{
		return;
	}

	SelectedToolIndex = Index;

	if (ToolSwitcher.IsValid() && Tools.IsValidIndex(Index))
	{
		ToolSwitcher->SetActiveWidgetIndex(Index);
	}

	if (ToolListView.IsValid())
	{
		TGuardValue<bool> Guard(bSelectingTool, true);
		if (Tools.IsValidIndex(Index))
		{
			ToolListView->SetSelection(Tools[Index], ESelectInfo::Direct);
		}
		else
		{
			ToolListView->ClearSelection();
		}
	}
}

bool SProfileToolsWindow::SelectToolById(FName ToolId)
{
	if (ToolId.IsNone())
	{
		return false;
	}
	const int32 Index = Tools.IndexOfByPredicate(
		[ToolId](const TSharedPtr<FProfileToolDescriptor>& Tool)
		{
			return Tool.IsValid() && Tool->Id == ToolId;
		});
	if (Index == INDEX_NONE)
	{
		return false;
	}
	SelectTool(Index);
	return true;
}

void SProfileToolsWindow::HandleAssetsPreDelete(const TArray<UObject*>& Objects)
{
	UPaper2DPlusCharacterProfileAsset* Asset = Profile.Get();
	if (!Asset)
	{
		return;
	}
	if (Objects.Contains(Asset))
	{
		// Force-deleting the subject leaves every tool bound to a dead object. Close rather than
		// linger -- there is nothing coherent left to author.
		Profile.Reset();
		CloseHostWindow();
	}
}

void SProfileToolsWindow::HandleObjectsReplaced(const TMap<UObject*, UObject*>& Replacements)
{
	UPaper2DPlusCharacterProfileAsset* Asset = Profile.Get();
	if (!Asset)
	{
		return;
	}
	if (UObject* const* Replacement = Replacements.Find(Asset))
	{
		// A reimport replaces the object. Rebind so the tools follow rather than write into the
		// stale one; if the replacement is not a profile at all, close instead.
		if (UPaper2DPlusCharacterProfileAsset* NewProfile =
			Cast<UPaper2DPlusCharacterProfileAsset>(*Replacement))
		{
			Profile = NewProfile;
			if (EditorModel.IsValid())
			{
				EditorModel->InitializeFromAsset(NewProfile);
			}
		}
		else
		{
			Profile.Reset();
			CloseHostWindow();
		}
	}
}

void SProfileToolsWindow::HandleRouteToReExtract(FString AnimationName)
{
	// Select the tool FIRST so its surface is the visible one, then carry the animation across. The
	// Re-extract surface is built at construction, so it exists whether or not it has been shown.
	SelectToolById(ToolId_ReExtract);
	if (ReExtractTool.IsValid())
	{
		ReExtractTool->SelectAnimation(AnimationName);
	}
}

void SProfileToolsWindow::CloseHostWindow()
{
	if (!FSlateApplication::IsInitialized())
	{
		return;
	}
	if (const TSharedPtr<SWindow> Window = FSlateApplication::Get().FindWidgetWindow(AsShared()))
	{
		Window->RequestDestroyWindow();
	}
}

void SProfileToolsWindow::OpenProfileTools(UPaper2DPlusCharacterProfileAsset* InProfile, FName ToolId)
{
	if (!InProfile)
	{
		return;
	}
	if (!FSlateApplication::IsInitialized())
	{
		return;
	}

	// Single instance: focus the live window and select the requested tool ON it, rather than
	// replacing a session the designer may be part-way through.
	if (const TSharedPtr<SWindow> Existing = ProfileToolsWindow_Internal::ActiveWindow.Pin())
	{
		if (const TSharedPtr<SProfileToolsWindow> Content = ProfileToolsWindow_Internal::ActiveContent.Pin())
		{
			Content->SelectToolById(ToolId);
		}
		Existing->BringToFront();
		FSlateApplication::Get().SetKeyboardFocus(Existing);
		return;
	}

	const TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(FText::Format(
			LOCTEXT("ProfileToolsTitleFmt", "Profile Tools - {0}"),
			FText::FromString(InProfile->GetName())))
		.ClientSize(FVector2D(1000.0f, 700.0f))
		.MinWidth(640.0f)
		.MinHeight(420.0f)
		.SupportsMaximize(true)
		.SupportsMinimize(true);

	const TSharedRef<SProfileToolsWindow> Content = SNew(SProfileToolsWindow)
		.Profile(InProfile)
		.InitialToolId(ToolId);

	Window->SetContent(Content);

	// Non-modal -- the editor stays interactive underneath.
	FSlateApplication::Get().AddWindow(Window, /*bShowImmediately=*/true);

	ProfileToolsWindow_Internal::ActiveWindow = Window;
	ProfileToolsWindow_Internal::ActiveContent = Content;
}

#if WITH_DEV_AUTOMATION_TESTS
TSharedPtr<SProfileToolsWindow> SProfileToolsWindow::GetActiveContentForTests()
{
	return ProfileToolsWindow_Internal::ActiveContent.Pin();
}
#endif

#undef LOCTEXT_NAMESPACE
