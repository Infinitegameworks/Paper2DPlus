// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "CharacterLayerAssetEditorToolkit.h"
#include "AnimationProfilePickerSource.h"
#include "AnimationProfileSwitcher.h"
#include "CharacterLayerAssetActions.h"
#include "CharacterLayerBakeCoordinator.h"
#include "CharacterLayerBakeStatusWidget.h"
#include "CharacterProfileEditorModel.h"
#include "FlipbookListPanel.h"
#include "FrameCueDataProvider.h"
#include "FrameEventEditor.h"
#include "HitboxEditorPanel.h"
#include "HitboxDataProvider.h"
#include "LayerAppearancePanel.h"
#include "LayerArtInspector.h"
#include "LayerOverviewPanel.h"
#include "LayerStructureTree.h"
#include "Paper2DPlusCharacterLayerAsset.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Paper2DPlusEditorIcons.h"
#include "Paper2DPlusProfileEditorToolbar.h"
#include "ProfileCompletionPanel.h"
#include "ProfileToolPanelHost.h"
#include "ProfileToolPanelProvider.h"
#include "ProfileValidationPanel.h"
#include "RelatedProfileBar.h"
#include "PlaybackQueuePanel.h"
#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Notifications/NotificationManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/EngineVersionComparison.h"
#include "Misc/MessageDialog.h"
#include "Misc/ScopedSlowTask.h"
#include "ScopedTransaction.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/MultiBox/MultiBoxExtender.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Text/STextBlock.h"
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

#define LOCTEXT_NAMESPACE "CharacterLayerAssetEditor"

const FName FCharacterLayerAssetEditorToolkit::StructureTabId(TEXT("CharacterLayerEditor_Structure"));
const FName FCharacterLayerAssetEditorToolkit::ArtTabId(TEXT("CharacterLayerEditor_Art"));
const FName FCharacterLayerAssetEditorToolkit::FlipbookListTabId(TEXT("CharacterLayerEditor_FlipbookList"));
const FName FCharacterLayerAssetEditorToolkit::HitboxesTabId(TEXT("CharacterLayerEditor_Hitboxes"));
const FName FCharacterLayerAssetEditorToolkit::FrameCuesTabId(TEXT("CharacterLayerEditor_FrameCues"));
const FName FCharacterLayerAssetEditorToolkit::AppearanceTabId(TEXT("CharacterLayerEditor_Appearance"));
const FName FCharacterLayerAssetEditorToolkit::ContextHostTabId(TEXT("CharacterLayerEditor_ContextPanels"));
const FName FCharacterLayerAssetEditorToolkit::CompletionTabId(TEXT("CharacterLayerEditor_Completion"));
const FName FCharacterLayerAssetEditorToolkit::RelatedProfilesTabId(TEXT("CharacterLayerEditor_RelatedProfiles"));
const FName FCharacterLayerAssetEditorToolkit::PlaybackQueueTabId(TEXT("CharacterLayerEditor_PlaybackQueue"));
// _v13: Playback Queue joins the lower-right Completion/Related Profiles stack, mirroring the Character
// workspace. A saved _v12 arrangement has no slot for it, so the key must move.
const FName FCharacterLayerAssetEditorToolkit::WorkspaceLayoutId(TEXT("CharacterLayerAssetEditor_Layout_v13_PlaybackQueue"));

namespace Paper2DPlusLayerEditorPrivate
{
	class FLayerToolPanelProvider final : public IProfileToolPanelProvider
	{
	public:
		explicit FLayerToolPanelProvider(TArray<FProfileToolPanelDescriptor> InPanels)
			: Panels(MoveTemp(InPanels))
		{
		}

		virtual FProfileToolPanelHostContract GetHostContract() const override
		{
			return FProfileToolPanelHostContract::External();
		}

		virtual void GetContextualPanels(
			TArray<FProfileToolPanelDescriptor>& OutPanels) const override
		{
			OutPanels.Append(Panels);
		}

	private:
		TArray<FProfileToolPanelDescriptor> Panels;
	};

	FProfileToolPanelDescriptor MakePanel(
		FName PanelId,
		const FText& Label,
		const FText& ToolTip,
		TFunction<TSharedRef<SWidget>()> WidgetFactory)
	{
		FProfileToolPanelDescriptor Descriptor;
		Descriptor.PanelId = PanelId;
		Descriptor.Label = Label;
		Descriptor.ToolTip = ToolTip;
		Descriptor.CapabilityId = PanelId;
		Descriptor.WidgetFactory = MoveTemp(WidgetFactory);
		return Descriptor;
	}
}

FCharacterLayerAssetEditorToolkit::~FCharacterLayerAssetEditorToolkit()
{
	if (const TSharedPtr<SHitboxEditorPanel> Panel = HitboxPanel.Pin())
	{
		Panel->HandleHostDeactivated();
	}
	if (const TSharedPtr<SFrameEventEditor> Panel = FrameCuePanel.Pin())
	{
		Panel->HandleHostDeactivated();
	}
	if (const TSharedPtr<SLayerOverviewPanel> Panel = LayerOverviewPanel.Pin())
	{
		Panel->HandleAuthoringModeDeactivated();
	}
	if (const TSharedPtr<SWindow> Window = ValidationWindow.Pin())
	{
		Window->RequestDestroyWindow();
	}
	ValidationWindow.Reset();
	if (const TSharedPtr<SProfileToolPanelHost> Host = ContextPanelHost.Pin())
	{
		Host->Shutdown();
	}
	ContextPanelHost.Reset();
	if (EditorModel.IsValid())
	{
		if (BakeStatusAssetDataHandle.IsValid())
		{
			EditorModel->OnAssetDataChanged.Remove(BakeStatusAssetDataHandle);
		}
		if (BakeStatusExternalHandle.IsValid())
		{
			EditorModel->OnAssetExternallyModified.Remove(BakeStatusExternalHandle);
		}
		if (BakeStatusSelectionHandle.IsValid())
		{
			EditorModel->OnFlipbookSelectionChanged.Remove(BakeStatusSelectionHandle);
		}
		// The sidebar's "Frame Data…" button can open the floating window from this editor too (over
		// the BaseProfile) — the window holds a strong model ref, so close it with the editor.
		EditorModel->CloseFrameDataWindow();
	}
	ToolPanelProviders.Reset();
	LayerCueProvider.Reset();
	LayerHitboxProvider.Reset();
	AnimationPickerSource.Reset();
	EditorModel.Reset();
}

void FCharacterLayerAssetEditorToolkit::InitEditor(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost, UPaper2DPlusCharacterLayerAsset* InAsset)
{
	EditedLayerAsset = InAsset;

	EditorModel = MakeShared<FCharacterProfileEditorModel>();
	ActiveToolId = ArtTabId;

	UPaper2DPlusCharacterProfileAsset* BaseProfile = InAsset->BaseProfile.LoadSynchronous();
	if (BaseProfile)
	{
		EditorModel->InitializeFromAsset(BaseProfile);
	}

	// U1 trust fixes: the shared model natively watches only its profile asset — register the LAYER
	// asset as the secondary watched object so external Modify()s of it (or its subobjects) route
	// through the SAME deferred OnAssetExternallyModified broadcast the panels already subscribe to.
	// Registered even when there is no BaseProfile (the layer asset is still editable then).
	EditorModel->SetSecondaryWatchedObject(InAsset);
	AnimationPickerSource = MakeShared<FAnimationProfilePickerSource>(EditorModel);

	// The selected real layer owns the authored hitbox data edited by this provider.
	LayerHitboxProvider = MakeShared<FLayerHitboxDataProvider>(EditedLayerAsset, EditorModel);

	// v12 keeps Structure persistently visible beside the central authoring tabs. Contextual Details
	// remain above the compact Completion/Related stack in a deliberately narrower right rail.
	const TSharedRef<FTabManager::FLayout> Layout = BuildDefaultLayout();

	// The default toolbar carries the REAL save affordance (Save + Browse). With the borrowed
	// profile-editing tabs gone, everything this editor can dirty is the layer asset itself — which
	// InitAssetEditor registers below — so the toolbar's Save covers exactly what the editor edits.
	const bool bCreateDefaultStandaloneMenu = true;
	const bool bCreateDefaultToolbar = true;

	// Extend the default toolbar with "Open Character Profile" (must be added BEFORE InitAssetEditor,
	// which generates the toolbar from the registered extenders).
	TSharedPtr<FExtender> ToolbarExtender = MakeShared<FExtender>();
	ToolbarExtender->AddToolBarExtension(
		"Asset",
		EExtensionHook::After,
		GetToolkitCommands(),
		FToolBarExtensionDelegate::CreateSP(this, &FCharacterLayerAssetEditorToolkit::ExtendToolbar));
	AddToolbarExtender(ToolbarExtender);

	Paper2DPlusProfileEditorToolbar::Install(
		GetToolMenuToolbarName(),
		GetToolkitCommands());

	FAssetEditorToolkit::InitAssetEditor(
		Mode,
		InitToolkitHost,
		TEXT("CharacterLayerAssetEditorApp"),
		Layout,
		bCreateDefaultStandaloneMenu,
		bCreateDefaultToolbar,
		InAsset
	);

	EditorModel->SetTabManager(GetTabManager());
	EditorModel->SetPlaybackQueueTabId(PlaybackQueueTabId);
	BakeStatusAssetDataHandle = EditorModel->OnAssetDataChanged.AddSP(
		this, &FCharacterLayerAssetEditorToolkit::HandleBakeStatusSourceChanged);
	BakeStatusExternalHandle = EditorModel->OnAssetExternallyModified.AddSP(
		this, &FCharacterLayerAssetEditorToolkit::HandleBakeStatusSourceChanged);
	BakeStatusSelectionHandle = EditorModel->OnFlipbookSelectionChanged.AddSP(
		this, &FCharacterLayerAssetEditorToolkit::HandleBakeStatusSelectionChanged);
	RefreshBakeStatus();
}

TSharedRef<FTabManager::FLayout> FCharacterLayerAssetEditorToolkit::BuildDefaultLayout()
{
	return FTabManager::NewLayout(WorkspaceLayoutId)
		->AddArea
		(
			FTabManager::NewPrimaryArea()
			->SetOrientation(Orient_Horizontal)
			->Split
			(
				FTabManager::NewStack()
				->SetSizeCoefficient(0.12f)
				->AddTab(FlipbookListTabId, ETabState::ClosedTab)
			)
			->Split
			(
				FTabManager::NewStack()
				->SetSizeCoefficient(0.16f)
				->AddTab(StructureTabId, ETabState::OpenedTab)
			)
			->Split
			(
				FTabManager::NewStack()
				->SetSizeCoefficient(0.55f)
				->AddTab(ArtTabId, ETabState::OpenedTab)
				->AddTab(HitboxesTabId, ETabState::OpenedTab)
				->AddTab(FrameCuesTabId, ETabState::OpenedTab)
				->AddTab(AppearanceTabId, ETabState::OpenedTab)
				->SetForegroundTab(ArtTabId)
			)
			->Split
			(
				FTabManager::NewSplitter()
				->SetOrientation(Orient_Vertical)
				->SetSizeCoefficient(0.17f)
				->Split
				(
					FTabManager::NewStack()
					->SetSizeCoefficient(0.64f)
					->AddTab(ContextHostTabId, ETabState::OpenedTab)
				)
				->Split
				(
					FTabManager::NewStack()
					->SetSizeCoefficient(0.36f)
					->AddTab(CompletionTabId, ETabState::OpenedTab)
					->AddTab(RelatedProfilesTabId, ETabState::OpenedTab)
					->AddTab(PlaybackQueueTabId, ETabState::OpenedTab)
					->SetForegroundTab(CompletionTabId)
				)
			)
		);
}

void FCharacterLayerAssetEditorToolkit::RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	WorkspaceMenuCategory = InTabManager->AddLocalWorkspaceMenuCategory(LOCTEXT("WorkspaceMenu_CharacterLayerEditor", "Character Layer Editor"));

	FAssetEditorToolkit::RegisterTabSpawners(InTabManager);

	InTabManager->RegisterTabSpawner(StructureTabId, FOnSpawnTab::CreateSP(this, &FCharacterLayerAssetEditorToolkit::SpawnTab_Structure))
		.SetDisplayName(LOCTEXT("StructureTab", "Structure"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef())
		.SetIcon(FSlateIcon(FAppStyle::Get().GetStyleSetName(), Paper2DPlusEditorIcons::TabFlipbooks));

	InTabManager->RegisterTabSpawner(ArtTabId, FOnSpawnTab::CreateSP(this, &FCharacterLayerAssetEditorToolkit::SpawnTab_Art))
		.SetDisplayName(LOCTEXT("ArtTab", "Art"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef())
		.SetIcon(FSlateIcon(Paper2DPlusEditorIcons::StyleSet, Paper2DPlusEditorIcons::TabSprite));

	InTabManager->RegisterTabSpawner(FlipbookListTabId, FOnSpawnTab::CreateSP(this, &FCharacterLayerAssetEditorToolkit::SpawnTab_FlipbookList))
		.SetDisplayName(LOCTEXT("NavigatorTab", "Navigator"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef())
		.SetIcon(FSlateIcon(FAppStyle::Get().GetStyleSetName(), Paper2DPlusEditorIcons::TabFlipbooks));

	InTabManager->RegisterTabSpawner(HitboxesTabId, FOnSpawnTab::CreateSP(this, &FCharacterLayerAssetEditorToolkit::SpawnTab_Hitboxes))
		.SetDisplayName(LOCTEXT("HitboxesTab", "Hitboxes"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef())
		.SetIcon(FSlateIcon(Paper2DPlusEditorIcons::StyleSet, Paper2DPlusEditorIcons::TabHitbox));
	InTabManager->RegisterTabSpawner(FrameCuesTabId, FOnSpawnTab::CreateSP(this, &FCharacterLayerAssetEditorToolkit::SpawnTab_FrameCues))
		.SetDisplayName(LOCTEXT("FrameCuesTab", "Frame Cues"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef())
		.SetIcon(FSlateIcon(FAppStyle::Get().GetStyleSetName(), Paper2DPlusEditorIcons::TabFrameEvents));
	InTabManager->RegisterTabSpawner(AppearanceTabId, FOnSpawnTab::CreateSP(this, &FCharacterLayerAssetEditorToolkit::SpawnTab_Appearance))
		.SetDisplayName(LOCTEXT("AppearanceTab", "Appearance"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef())
		.SetIcon(FSlateIcon(FAppStyle::Get().GetStyleSetName(), "Icons.Settings"));
	InTabManager->RegisterTabSpawner(ContextHostTabId, FOnSpawnTab::CreateSP(this, &FCharacterLayerAssetEditorToolkit::SpawnTab_ContextHost))
		.SetDisplayName(LOCTEXT("ContextHostTab", "Tool Panels"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef())
		.SetIcon(FSlateIcon(FAppStyle::Get().GetStyleSetName(), "Icons.Layout"));
	InTabManager->RegisterTabSpawner(CompletionTabId, FOnSpawnTab::CreateSP(this, &FCharacterLayerAssetEditorToolkit::SpawnTab_Completion))
		.SetDisplayName(LOCTEXT("CompletionTab", "Completion"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef())
		.SetIcon(FSlateIcon(FAppStyle::Get().GetStyleSetName(), "Icons.Check"));
	InTabManager->RegisterTabSpawner(RelatedProfilesTabId, FOnSpawnTab::CreateSP(this, &FCharacterLayerAssetEditorToolkit::SpawnTab_RelatedProfiles))
		.SetDisplayName(LOCTEXT("RelatedProfilesTab", "Related Profiles"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef())
		.SetIcon(FSlateIcon(FAppStyle::Get().GetStyleSetName(), "Icons.Link"));
	InTabManager->RegisterTabSpawner(PlaybackQueueTabId, FOnSpawnTab::CreateSP(this, &FCharacterLayerAssetEditorToolkit::SpawnTab_PlaybackQueue))
		.SetDisplayName(LOCTEXT("PlaybackQueueTab", "Playback Queue"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef())
		.SetIcon(FSlateIcon(FAppStyle::Get().GetStyleSetName(), "Icons.Play"));
}

void FCharacterLayerAssetEditorToolkit::UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	FAssetEditorToolkit::UnregisterTabSpawners(InTabManager);
	InTabManager->UnregisterTabSpawner(StructureTabId);
	InTabManager->UnregisterTabSpawner(ArtTabId);
	InTabManager->UnregisterTabSpawner(FlipbookListTabId);
	InTabManager->UnregisterTabSpawner(HitboxesTabId);
	InTabManager->UnregisterTabSpawner(FrameCuesTabId);
	InTabManager->UnregisterTabSpawner(AppearanceTabId);
	InTabManager->UnregisterTabSpawner(ContextHostTabId);
	InTabManager->UnregisterTabSpawner(CompletionTabId);
	InTabManager->UnregisterTabSpawner(RelatedProfilesTabId);
	InTabManager->UnregisterTabSpawner(PlaybackQueueTabId);
}

TSharedRef<SDockTab> FCharacterLayerAssetEditorToolkit::SpawnTab_Structure(const FSpawnTabArgs& Args)
{
	const TSharedRef<SLayerStructureTree> Panel = SNew(SLayerStructureTree)
		.Model(EditorModel)
		.LayerAsset(EditedLayerAsset);

	// Structure is a persistent companion rail, not a mutually exclusive authoring mode. Keeping it
	// outside MakeMainToolTab avoids a duplicate animation header and leaves Tool Panels routed to the
	// active central tool while designers select/reorder Layers here.
	return SNew(SDockTab)
		.Label(LOCTEXT("StructureTabLabel", "Structure"))
		[
			Panel
		];
}

TSharedRef<SDockTab> FCharacterLayerAssetEditorToolkit::SpawnTab_Art(const FSpawnTabArgs& Args)
{
	const TSharedRef<SLayerOverviewPanel> Panel = SNew(SLayerOverviewPanel)
		.Model(EditorModel)
		.LayerAsset(EditedLayerAsset)
		.LayerFirstPresentation(true);
	LayerOverviewPanel = Panel;

	const TWeakPtr<FCharacterProfileEditorModel> WeakModel = EditorModel;
	const TWeakObjectPtr<UPaper2DPlusCharacterLayerAsset> WeakAsset = EditedLayerAsset;
	TArray<FProfileToolPanelDescriptor> Panels;
	Panels.Add(Paper2DPlusLayerEditorPrivate::MakePanel(
		TEXT("LayerArt"),
		LOCTEXT("ArtPanel", "Art"),
		LOCTEXT("ArtPanelTip", "Sprite mapping and layer-local placement for the selected Layer."),
		[WeakModel, WeakAsset]() -> TSharedRef<SWidget>
		{
			const TSharedPtr<FCharacterProfileEditorModel> Model = WeakModel.Pin();
			if (!Model.IsValid() || !WeakAsset.IsValid())
			{
				return SNullWidget::NullWidget;
			}
			return SNew(SLayerArtInspector)
				.Model(Model)
				.LayerAsset(WeakAsset.Get());
		}));
	RegisterToolPanelProvider(
		ArtTabId,
		MakeShared<Paper2DPlusLayerEditorPrivate::FLayerToolPanelProvider>(MoveTemp(Panels)));
	return MakeMainToolTab(LOCTEXT("ArtTabLabel", "Art"), ArtTabId, Panel);
}

TSharedRef<SDockTab> FCharacterLayerAssetEditorToolkit::SpawnTab_FlipbookList(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.Label(LOCTEXT("NavigatorTabLabel", "Navigator"))
		[
			SNew(SFlipbookListPanel)
			.Model(EditorModel)
			.LayerAsset(EditedLayerAsset)
		];
}

TSharedRef<SDockTab> FCharacterLayerAssetEditorToolkit::SpawnTab_Hitboxes(const FSpawnTabArgs& Args)
{
	// The real hitbox editor, scoped to the model's selected Layer through the Layer provider. The
	// LayerAsset arg drives the frame-strip composite from the committed appearance; the provider redirects the
	// per-frame reads/writes to the selected variant's AnimationOverrides on the layer asset.
	TSharedPtr<SHitboxEditorPanel> Panel;
	if (const TSharedPtr<IProfileToolPanelProvider> ExistingProvider =
		FindToolPanelProvider(HitboxesTabId))
	{
		Panel = StaticCastSharedPtr<SHitboxEditorPanel>(ExistingProvider);
	}
	if (!Panel.IsValid())
	{
		Panel = SNew(SHitboxEditorPanel)
			.Model(EditorModel)
			.LayerAsset(EditedLayerAsset)
			.FrameDataProvider(LayerHitboxProvider)
			.HostContract(FProfileToolPanelHostContract::External());
		RegisterToolPanelProvider(HitboxesTabId, StaticCastSharedPtr<IProfileToolPanelProvider>(Panel));
	}
	HitboxPanel = Panel;
	return MakeMainToolTab(LOCTEXT("HitboxesTabLabel", "Hitboxes"), HitboxesTabId, Panel.ToSharedRef());
}

TSharedRef<SDockTab> FCharacterLayerAssetEditorToolkit::SpawnTab_FrameCues(const FSpawnTabArgs& Args)
{
	TSharedPtr<SFrameEventEditor> Panel;
	if (const TSharedPtr<IProfileToolPanelProvider> ExistingProvider =
		FindToolPanelProvider(FrameCuesTabId))
	{
		Panel = StaticCastSharedPtr<SFrameEventEditor>(ExistingProvider);
	}
	if (!Panel.IsValid())
	{
		if (!LayerCueProvider.IsValid())
		{
			LayerCueProvider = MakeShared<FLayerFrameCueDataProvider>(EditedLayerAsset, EditorModel);
		}
		Panel = SNew(SFrameEventEditor)
			.Model(EditorModel)
			.DataProvider(LayerCueProvider)
			.HostContract(FProfileToolPanelHostContract::External());
		RegisterToolPanelProvider(FrameCuesTabId, StaticCastSharedPtr<IProfileToolPanelProvider>(Panel));
	}
	FrameCuePanel = Panel;
	return MakeMainToolTab(LOCTEXT("FrameCuesTabLabel", "Frame Cues"), FrameCuesTabId, Panel.ToSharedRef());
}

TSharedRef<SDockTab> FCharacterLayerAssetEditorToolkit::SpawnTab_Appearance(const FSpawnTabArgs& Args)
{
	const TSharedRef<SLayerAppearancePanel> Panel = SNew(SLayerAppearancePanel)
		.LayerAsset(EditedLayerAsset);

	TArray<FProfileToolPanelDescriptor> Panels;
	Panels.Add(Paper2DPlusLayerEditorPrivate::MakePanel(
		TEXT("AppearanceGuidance"),
		LOCTEXT("AppearanceGuidancePanel", "Appearance"),
		LOCTEXT("AppearanceGuidancePanelTip", "How complete presets, global Layer order, and Exclusive Groups interact."),
		[]() -> TSharedRef<SWidget>
		{
			return SNew(SBorder)
				.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
				.Padding(8.0f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT(
						"AppearanceGuidance",
						"Presets are complete snapshots. Layer order comes only from Structure; Exclusive Groups are optional and allow zero or one active member."))
					.AutoWrapText(true)
				];
		}));
	RegisterToolPanelProvider(
		AppearanceTabId,
		MakeShared<Paper2DPlusLayerEditorPrivate::FLayerToolPanelProvider>(MoveTemp(Panels)));
	return MakeMainToolTab(LOCTEXT("AppearanceTabLabel", "Appearance"), AppearanceTabId, Panel);
}

TSharedRef<SDockTab> FCharacterLayerAssetEditorToolkit::SpawnTab_ContextHost(const FSpawnTabArgs& Args)
{
	const TSharedRef<SDockTab> DockTab = SNew(SDockTab)
		.Label(LOCTEXT("ContextHostTabLabel", "Tool Panels"));
	const TSharedRef<SProfileToolPanelHost> Host = SNew(SProfileToolPanelHost)
		.OwnerTab(DockTab)
		.LayoutScope(TEXT("Layer"));
	ContextPanelHost = Host;
	DockTab->SetContent(Host);
	Host->RequestActiveTool(ActiveToolId, FindToolPanelProvider(ActiveToolId));
	return DockTab;
}

TSharedRef<SDockTab> FCharacterLayerAssetEditorToolkit::SpawnTab_Completion(const FSpawnTabArgs& Args)
{
	const TSharedRef<SCharacterLayerBakeStatusWidget> StatusWidget =
		SNew(SCharacterLayerBakeStatusWidget)
		.Presentation(BuildBakeStatusPresentation())
		.OnBakeCurrent(FSimpleDelegate::CreateSP(this, &FCharacterLayerAssetEditorToolkit::BakeCurrent))
		.OnBakeAll(FSimpleDelegate::CreateSP(this, &FCharacterLayerAssetEditorToolkit::BakeAll))
		.OnSaveBakeSet(FSimpleDelegate::CreateSP(this, &FCharacterLayerAssetEditorToolkit::SaveBakeSet))
		.OnOverwrite(FSimpleDelegate::CreateSP(this, &FCharacterLayerAssetEditorToolkit::OverwriteFromLayerSource))
		.OnRepair(FSimpleDelegate::CreateSP(this, &FCharacterLayerAssetEditorToolkit::RepairBakeSet))
		.OnRebase(FSimpleDelegate::CreateSP(this, &FCharacterLayerAssetEditorToolkit::RebaseRegistration))
		.OnDetach(FSimpleDelegate::CreateSP(this, &FCharacterLayerAssetEditorToolkit::DetachBakeSet))
		.OnEnableRuntimeCustomization(FSimpleDelegate::CreateSP(
			this, &FCharacterLayerAssetEditorToolkit::EnableRuntimeCustomization));
	BakeStatusWidget = StatusWidget;

	return SNew(SDockTab)
		.Label(LOCTEXT("CompletionTabLabel", "Completion"))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().FillHeight(1.0f)
			[
				SNew(SProfileCompletionPanel)
				.Asset(EditedLayerAsset)
				.ProfileKind(EProfileCompletionKind::Layer)
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right).Padding(5.0f)
			[
				StatusWidget
			]
		];
}

TSharedRef<SDockTab> FCharacterLayerAssetEditorToolkit::SpawnTab_RelatedProfiles(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.Label(LOCTEXT("RelatedProfilesTabLabel", "Related Profiles"))
		[
			SNew(SRelatedProfileBar).EditedAsset(EditedLayerAsset)
		];
}

TSharedRef<SDockTab> FCharacterLayerAssetEditorToolkit::SpawnTab_PlaybackQueue(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.Label(LOCTEXT("PlaybackQueueTabLabel", "Playback Queue"))
		[
			SNew(SPlaybackQueuePanel).Model(EditorModel)
		];
}

TSharedRef<SDockTab> FCharacterLayerAssetEditorToolkit::MakeMainToolTab(
	const FText& Label,
	FName ToolId,
	TSharedRef<SWidget> ToolContent)
{
	const TSharedRef<SDockTab> DockTab = SNew(SDockTab)
		.Label(Label);
	DockTab->SetContent(WrapMainToolContent(ToolContent));
	DockTab->SetOnTabActivated(SDockTab::FOnTabActivatedCallback::CreateSP(
		this,
		&FCharacterLayerAssetEditorToolkit::HandleMainToolActivated,
		ToolId));
	return DockTab;
}

TSharedRef<SWidget> FCharacterLayerAssetEditorToolkit::WrapMainToolContent(
	TSharedRef<SWidget> ToolContent)
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
			.Padding(FMargin(6.0f, 4.0f))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.0f)
				[
					SNew(SAnimationProfileSwitcher)
					.Source(AnimationPickerSource)
					.EmptySelectionText(LOCTEXT("NoCurrentAnimation", "Select an animation"))
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(SButton)
					.Text(LOCTEXT("ValidateAction", "Validate"))
					.ToolTipText(LOCTEXT("ValidateActionTip", "Open read-only validation without adding another workspace tab."))
					.OnClicked_Lambda([this]()
					{
						OpenValidation();
						return FReply::Handled();
					})
				]
			]
		]
		+ SVerticalBox::Slot().FillHeight(1.0f)
		[
			ToolContent
		];
}

void FCharacterLayerAssetEditorToolkit::HandleMainToolActivated(
	TSharedRef<SDockTab> ActivatedTab,
	ETabActivationCause Cause,
	FName ToolId)
{
	if (ActiveToolId == ArtTabId && ToolId != ArtTabId)
	{
		if (const TSharedPtr<SLayerOverviewPanel> Panel = LayerOverviewPanel.Pin())
		{
			Panel->HandleAuthoringModeDeactivated();
		}
	}
	if (ActiveToolId == HitboxesTabId && ToolId != HitboxesTabId)
	{
		if (const TSharedPtr<SHitboxEditorPanel> Panel = HitboxPanel.Pin())
		{
			Panel->HandleHostDeactivated();
		}
	}
	if (ActiveToolId == FrameCuesTabId && ToolId != FrameCuesTabId)
	{
		if (const TSharedPtr<SFrameEventEditor> Panel = FrameCuePanel.Pin())
		{
			Panel->HandleHostDeactivated();
		}
	}
	if (ToolId == FrameCuesTabId)
	{
		if (const TSharedPtr<SFrameEventEditor> Panel = FrameCuePanel.Pin())
		{
			Panel->HandleHostActivated();
		}
	}

	ActiveToolId = ToolId;
	if (const TSharedPtr<SProfileToolPanelHost> Host = ContextPanelHost.Pin())
	{
		Host->RequestActiveTool(ToolId, FindToolPanelProvider(ToolId));
	}
}

TSharedPtr<IProfileToolPanelProvider> FCharacterLayerAssetEditorToolkit::FindToolPanelProvider(
	FName ToolId) const
{
	const TSharedPtr<IProfileToolPanelProvider>* Provider = ToolPanelProviders.Find(ToolId);
	return Provider ? *Provider : nullptr;
}

void FCharacterLayerAssetEditorToolkit::RegisterToolPanelProvider(
	FName ToolId,
	TSharedPtr<IProfileToolPanelProvider> Provider)
{
	if (ToolId.IsNone() || !Provider.IsValid())
	{
		return;
	}
	ToolPanelProviders.Add(ToolId, Provider);
	if (ToolId == ActiveToolId)
	{
		if (const TSharedPtr<SProfileToolPanelHost> Host = ContextPanelHost.Pin())
		{
			Host->RequestActiveTool(ToolId, Provider);
		}
	}
}

void FCharacterLayerAssetEditorToolkit::OpenValidation()
{
	if (const TSharedPtr<SWindow> Existing = ValidationWindow.Pin())
	{
		Existing->BringToFront();
		return;
	}
	if (!FSlateApplication::IsInitialized()) return;

	const TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(LOCTEXT("ValidationWindowTitle", "Character Layer Validation"))
		.ClientSize(FVector2D(720.0f, 520.0f))
		.SupportsMaximize(true)
		.SupportsMinimize(false);
	Window->SetContent(
		SNew(SProfileValidationPanel)
		.Asset(EditedLayerAsset)
		.OnIssueActivated(FOnPaper2DPlusValidationIssueActivated::CreateSP(
			this,
			&FCharacterLayerAssetEditorToolkit::HandleValidationIssueActivated)));
	ValidationWindow = Window;
	FSlateApplication::Get().AddWindow(Window);
}

void FCharacterLayerAssetEditorToolkit::HandleValidationIssueActivated(
	const FPaper2DPlusValidationIssue& Issue)
{
	if (Issue.ToolTarget.IsSet() && !Issue.ToolTarget->TabId.IsNone())
	{
		if (const TSharedPtr<FTabManager> Manager = GetTabManager())
		{
			Manager->TryInvokeTab(Issue.ToolTarget->TabId);
		}
	}
}

void FCharacterLayerAssetEditorToolkit::ExtendToolbar(FToolBarBuilder& ToolbarBuilder)
{
	ToolbarBuilder.BeginSection("CharacterLayer");
	ToolbarBuilder.AddToolBarButton(
		FUIAction(
			FExecuteAction::CreateSP(this, &FCharacterLayerAssetEditorToolkit::OpenCharacterProfileEditor),
			FCanExecuteAction::CreateSP(this, &FCharacterLayerAssetEditorToolkit::CanOpenCharacterProfileEditor)),
		NAME_None,
		LOCTEXT("OpenCharacterProfileLabel", "Open Character Profile"),
		TAttribute<FText>::CreateSP(this, &FCharacterLayerAssetEditorToolkit::GetOpenCharacterProfileTooltip),
		FSlateIcon(FAppStyle::Get().GetStyleSetName(), "Icons.Edit"));
	ToolbarBuilder.AddToolBarButton(
		FUIAction(FExecuteAction::CreateSP(this, &FCharacterLayerAssetEditorToolkit::OpenValidation)),
		NAME_None,
		LOCTEXT("ValidateLabel", "Validate"),
		LOCTEXT("ValidateTooltip", "Open the shared read-only validation panel for this Character Layer asset."),
		FSlateIcon(FAppStyle::Get().GetStyleSetName(), "Icons.Check"));
	ToolbarBuilder.EndSection();
}

void FCharacterLayerAssetEditorToolkit::OpenCharacterProfileEditor()
{
	UPaper2DPlusCharacterProfileAsset* BaseProfile = EditedLayerAsset ? EditedLayerAsset->BaseProfile.LoadSynchronous() : nullptr;
	if (BaseProfile && GEditor)
	{
		if (UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
		{
			AssetEditorSubsystem->OpenEditorForAsset(BaseProfile);
		}
	}
}

bool FCharacterLayerAssetEditorToolkit::CanOpenCharacterProfileEditor() const
{
	return EditedLayerAsset != nullptr && !EditedLayerAsset->BaseProfile.IsNull();
}

FText FCharacterLayerAssetEditorToolkit::GetOpenCharacterProfileTooltip() const
{
	return CanOpenCharacterProfileEditor()
		? LOCTEXT("OpenCharacterProfileTooltip", "Open the Base Profile for character-wide animation metadata, frame timing, transitions, tags, and root motion.")
		: LOCTEXT("OpenCharacterProfileTooltip_NoProfile", "No Base Profile is set on this layer asset. Assign one in the Art tab to edit its character data.");
}

UPaper2DPlusCharacterProfileAsset* FCharacterLayerAssetEditorToolkit::GetBaseProfile() const
{
	return EditorModel.IsValid() ? EditorModel->GetAsset() : nullptr;
}

int32 FCharacterLayerAssetEditorToolkit::ResolveCurrentRegistrationIndex() const
{
	if (!EditedLayerAsset || !EditorModel.IsValid()) return INDEX_NONE;
	const FSoftObjectPath SelectedPath = EditorModel->GetSelectedFlipbookPath();
	if (!SelectedPath.IsNull())
	{
		for (int32 Index = 0; Index < EditedLayerAsset->AnimationRegistration.Num(); ++Index)
		{
			if (EditedLayerAsset->AnimationRegistration[Index].Flipbook.ToSoftObjectPath() == SelectedPath)
			{
				return Index;
			}
		}
	}
	const FString SelectedName = EditorModel->GetSelectedFlipbookName().ToString();
	if (!SelectedName.IsEmpty())
	{
		for (int32 Index = 0; Index < EditedLayerAsset->AnimationRegistration.Num(); ++Index)
		{
			if (EditedLayerAsset->AnimationRegistration[Index].LegacyAnimationName.Equals(
				SelectedName, ESearchCase::IgnoreCase))
			{
				return Index;
			}
		}
	}
	return INDEX_NONE;
}

void FCharacterLayerAssetEditorToolkit::HandleBakeStatusSourceChanged()
{
	if (bPublishOperationInProgress)
	{
		bBakeStatusRefreshPending = true;
		return;
	}
	RefreshBakeStatus();
}

void FCharacterLayerAssetEditorToolkit::HandleBakeStatusSelectionChanged(int32 NewIndex)
{
	(void)NewIndex;
	if (bPublishOperationInProgress)
	{
		bBakeStatusRefreshPending = true;
		return;
	}
	if (!CachedBakeStatus || !EditedLayerAsset)
	{
		RefreshBakeStatus();
		return;
	}

	// Selection does not change source, output, package, or ownership state. Preserve the deep snapshot
	// and update only the two selection-derived fields instead of recompiling every animation plan.
	FCharacterLayerBakeStatusSnapshot Updated = *CachedBakeStatus;
	Updated.CurrentRegistrationIndex = ResolveCurrentRegistrationIndex();
	const bool bAttached = Updated.AttachmentState == ECharacterLayerBakeAttachmentState::Attached;
	const bool bCurrentValid = EditedLayerAsset->AnimationRegistration.IsValidIndex(
		Updated.CurrentRegistrationIndex);
	Updated.bCanBakeCurrent = bAttached && bCurrentValid
		&& Updated.OutputConflictCount == 0 && !Updated.bIntegrityBlocked;
	CachedBakeStatus = MakeShared<FCharacterLayerBakeStatusSnapshot>(MoveTemp(Updated));
	if (const TSharedPtr<SCharacterLayerBakeStatusWidget> Widget = BakeStatusWidget.Pin())
	{
		Widget->SetPresentation(BuildBakeStatusPresentation());
	}
}

void FCharacterLayerAssetEditorToolkit::RefreshBakeStatus()
{
	if (bPublishOperationInProgress)
	{
		bBakeStatusRefreshPending = true;
		return;
	}
	CachedBakeStatus = MakeShared<FCharacterLayerBakeStatusSnapshot>(
		CharacterLayerBakeCoordinator::EvaluateStatus(
			EditedLayerAsset, GetBaseProfile(), ResolveCurrentRegistrationIndex()));
	if (const TSharedPtr<SCharacterLayerBakeStatusWidget> Widget = BakeStatusWidget.Pin())
	{
		Widget->SetPresentation(BuildBakeStatusPresentation());
	}
}

FCharacterLayerBakeStatusPresentation
FCharacterLayerAssetEditorToolkit::BuildBakeStatusPresentation() const
{
	FCharacterLayerBakeStatusPresentation Presentation;
	Presentation.bBusy = bPublishOperationInProgress;
	if (EditedLayerAsset)
	{
		switch (EditedLayerAsset->UsageMode)
		{
		case ECharacterLayerUsageMode::FixedBaked:
			Presentation.DeliveryText = LOCTEXT("DeliveryFixed", "Delivery: Fixed Baked");
			break;
		case ECharacterLayerUsageMode::RuntimeCustomizable:
			Presentation.DeliveryText = LOCTEXT("DeliveryRuntime", "Delivery: Runtime Customizable · Hybrid Runtime Renderer is opt-in per component");
			break;
		default:
			Presentation.DeliveryText = LOCTEXT("DeliveryUnknown", "Delivery: Unknown");
			break;
		}

		const bool bFixedLifecycleProtected =
			EditedLayerAsset->UsageMode == ECharacterLayerUsageMode::FixedBaked
			&& EditedLayerAsset->BakeAttachmentState != ECharacterLayerBakeAttachmentState::Detached;
		Presentation.bShowEnableRuntimeCustomization =
			EditedLayerAsset->UsageMode != ECharacterLayerUsageMode::RuntimeCustomizable
			&& !bFixedLifecycleProtected;
	}
	if (!CachedBakeStatus)
	{
		Presentation.StatusText = LOCTEXT("BakeStatusUnavailable", "Publish status unavailable");
		Presentation.SummaryText = LOCTEXT("BakeStatusUnavailableSummary", "Refresh the Layer workspace after assigning a Character Profile.");
		Presentation.StatusColor = FSlateColor(FLinearColor::Gray);
		return Presentation;
	}

	const FCharacterLayerBakeStatusSnapshot& Status = *CachedBakeStatus;
	switch (Status.Status)
	{
	case ECharacterLayerBakeStatus::NeverBaked:
		Presentation.StatusText = LOCTEXT("NeverBaked", "Never Baked");
		Presentation.StatusColor = FSlateColor(FLinearColor(0.3f, 0.65f, 1.0f));
		Presentation.SummaryText = Status.bCanBakeAll
			? LOCTEXT("FirstBakeReady", "Ready to publish the complete fixed-baked layer set. The operation never auto-saves.")
			: LOCTEXT("FirstBakeBlocked", "The first fixed bake is blocked. Resolve the listed profile or ownership issue.");
		break;
	case ECharacterLayerBakeStatus::UpToDate:
		Presentation.StatusText = Status.bPartialSave
			? LOCTEXT("UpToDatePartial", "Up to Date · Partial Save")
			: Status.bNeedsSave
				? LOCTEXT("UpToDateNeedsSave", "Up to Date · Needs Save")
				: LOCTEXT("UpToDate", "Up to Date");
		Presentation.SummaryText = LOCTEXT("UpToDateSummary", "Layer source, canonical flipbooks, Profile output, and manifest agree.");
		Presentation.StatusColor = FSlateColor(FLinearColor(0.25f, 0.8f, 0.35f));
		break;
	case ECharacterLayerBakeStatus::NeedsBake:
		Presentation.StatusText = LOCTEXT("NeedsBake", "Needs Bake");
		Presentation.SummaryText = FText::Format(
			LOCTEXT("NeedsBakeSummary", "{0} animation(s) differ from their last published Layer source."),
			FText::AsNumber(Status.SourceDriftCount));
		Presentation.StatusColor = FSlateColor(FLinearColor(1.0f, 0.68f, 0.15f));
		break;
	case ECharacterLayerBakeStatus::OutputConflict:
		Presentation.StatusText = LOCTEXT("OutputConflict", "Output Conflict");
		Presentation.SummaryText = FText::Format(
			LOCTEXT("OutputConflictSummary", "{0} managed animation output(s) changed outside this Layer source. Ordinary bake is blocked."),
			FText::AsNumber(Status.OutputConflictCount));
		Presentation.StatusColor = FSlateColor(FLinearColor(1.0f, 0.3f, 0.2f));
		break;
	case ECharacterLayerBakeStatus::Detached:
		Presentation.StatusText = LOCTEXT("Detached", "Detached");
		Presentation.SummaryText = LOCTEXT("DetachedSummary", "Canonical output is frozen in place. This Layer retains evidence but no longer manages those outputs.");
		Presentation.StatusColor = FSlateColor(FLinearColor::Gray);
		break;
	default:
		Presentation.StatusText = LOCTEXT("RecoveryRequired", "Recovery Required");
		Presentation.SummaryText = LOCTEXT("RecoveryRequiredSummary", "Integrity could not be proven. Ordinary publish is blocked; use Repair or explicitly freeze the current output with Advanced Detach.");
		Presentation.StatusColor = FSlateColor(FLinearColor(1.0f, 0.18f, 0.15f));
		break;
	}

	Presentation.SelectionText = FText::Format(
		LOCTEXT("DefaultSelection", "Default Appearance selects {0} Layer(s). Preview visibility is never published."),
		FText::AsNumber(Status.DefaultActiveLayerCount));

	Presentation.DiagnosticsText = SCharacterLayerBakeStatusWidget::BuildDiagnosticsText(Status);
	if (!Status.LastOperation.Report.IsEmpty())
	{
		Presentation.LastReportText = FText::Format(
			LOCTEXT("LastPublishReport", "Last publish report: {0}"),
			FText::FromString(Status.LastOperation.Report));
	}

	Presentation.bShowBakeCurrent = Status.bCanBakeCurrent;
	Presentation.bShowBakeAll = Status.bCanBakeAll;
	Presentation.bShowSaveBakeSet = Status.bCanSaveBakeSet;
	Presentation.bShowOverwrite = Status.bCanOverwriteFromSource;
	Presentation.bShowRepair = Status.bCanRepair;
	Presentation.bShowRebase = Status.bCanRebaseRegistration;
	Presentation.bShowDetach = Status.bCanDetach;
	return Presentation;
}

void FCharacterLayerAssetEditorToolkit::BeginPublishOperation()
{
	if (bPublishOperationInProgress) return;
	bPublishOperationInProgress = true;
	bBakeStatusRefreshPending = false;
	if (EditorModel.IsValid())
	{
		EditorModel->SetQueuePlaying(false);
	}
	if (const TSharedPtr<SLayerOverviewPanel> Panel = LayerOverviewPanel.Pin())
	{
		Panel->HandleAuthoringModeDeactivated();
	}
	if (const TSharedPtr<SHitboxEditorPanel> Panel = HitboxPanel.Pin())
	{
		Panel->HandleHostDeactivated();
	}
	if (const TSharedPtr<SFrameEventEditor> Panel = FrameCuePanel.Pin())
	{
		Panel->StopPlayback();
		Panel->HandleHostDeactivated();
	}
	if (const TSharedPtr<SCharacterLayerBakeStatusWidget> Widget = BakeStatusWidget.Pin())
	{
		Widget->SetPresentation(BuildBakeStatusPresentation());
	}
}

void FCharacterLayerAssetEditorToolkit::EndPublishOperation()
{
	if (ActiveToolId == FrameCuesTabId)
	{
		if (const TSharedPtr<SFrameEventEditor> Panel = FrameCuePanel.Pin())
		{
			Panel->HandleHostActivated();
		}
	}
	bPublishOperationInProgress = false;
	bBakeStatusRefreshPending = false;
	if (EditorModel.IsValid()) EditorModel->NotifyAssetDataChanged();
	else RefreshBakeStatus();
}

bool FCharacterLayerAssetEditorToolkit::ConfirmPublishAction(const FText& Message) const
{
	return FMessageDialog::Open(EAppMsgType::YesNo, Message) == EAppReturnType::Yes;
}

void FCharacterLayerAssetEditorToolkit::ShowPublishNotification(
	const FText& Message,
	bool bSuccess) const
{
	FNotificationInfo Info(Message);
	Info.bFireAndForget = true;
	Info.ExpireDuration = bSuccess ? 5.0f : 9.0f;
	if (const TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info))
	{
		Item->SetCompletionState(bSuccess
			? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);
	}
}

void FCharacterLayerAssetEditorToolkit::HandleBakeResult(
	const FCharacterLayerBakeResult& Result,
	const FText& OperationLabel)
{
	const FText Report = Result.Report.IsEmpty()
		? FText::Format(
			Result.bSuccess
				? LOCTEXT("PublishSucceeded", "{0} completed. Save Bake Set remains explicit.")
				: LOCTEXT("PublishFailed", "{0} failed. Review the Layer publish status."),
			OperationLabel)
		: FText::FromString(Result.Report);
	ShowPublishNotification(Report, Result.bSuccess);
}

void FCharacterLayerAssetEditorToolkit::HandleSaveResult(
	const FCharacterLayerBakeSaveResult& Result)
{
	ShowPublishNotification(
		Result.Report.IsEmpty()
			? (Result.bSuccess
				? LOCTEXT("SaveSucceeded", "Bake set saved.")
				: LOCTEXT("SaveFailed", "Bake set save was incomplete. Retry Save Bake Set after resolving the failed packages."))
			: FText::FromString(Result.Report),
		Result.bSuccess);
}

void FCharacterLayerAssetEditorToolkit::RunBakeCommand(
	ECharacterLayerBakeOperation Operation,
	bool bOverwrite,
	const FText& NotificationLabel)
{
	if (bPublishOperationInProgress || !EditedLayerAsset || !GetBaseProfile()) return;
	BeginPublishOperation();
	FCharacterLayerBakeRequest Request;
	Request.LayerAsset = EditedLayerAsset;
	Request.CharacterProfile = GetBaseProfile();
	Request.Operation = Operation;
	Request.CurrentRegistrationIndex = ResolveCurrentRegistrationIndex();
	Request.bOverwriteManagedOutputConflict = bOverwrite;
	FScopedSlowTask Progress(
		1.0f,
		FText::Format(
			LOCTEXT("PublishStagingProgress", "Staging {0}. Cancel is honored until commit begins; commit is atomic and non-cancellable."),
			NotificationLabel));
	Progress.MakeDialog(/*bShowCancelButton=*/true);
	Progress.EnterProgressFrame(
		1.0f,
		LOCTEXT("PublishStagingFrame", "Preflighting source, ownership, managed outputs, and rollback snapshots…"));
	Request.IsCancellationRequested = [&Progress]()
	{
#if UE_VERSION_OLDER_THAN(5, 1, 0)
		FPlatformMisc::PumpMessagesForSlowTask();
#else
		Progress.TickProgress();
#endif
		return Progress.ShouldCancel();
	};
	const FCharacterLayerBakeResult Result = CharacterLayerBakeCoordinator::Execute(Request);
	EndPublishOperation();
	HandleBakeResult(Result, NotificationLabel);
}

void FCharacterLayerAssetEditorToolkit::BakeCurrent()
{
	if (!CachedBakeStatus || !CachedBakeStatus->bCanBakeCurrent) return;
	RunBakeCommand(ECharacterLayerBakeOperation::Current, false,
		LOCTEXT("BakeCurrentLabel", "Bake Current"));
}

void FCharacterLayerAssetEditorToolkit::BakeAll()
{
	if (!CachedBakeStatus || !CachedBakeStatus->bCanBakeAll) return;
	RunBakeCommand(ECharacterLayerBakeOperation::All, false,
		LOCTEXT("BakeAllLabel", "Bake All"));
}

void FCharacterLayerAssetEditorToolkit::SaveBakeSet()
{
	if (bPublishOperationInProgress || !CachedBakeStatus
		|| !CachedBakeStatus->bCanSaveBakeSet || !EditedLayerAsset) return;
	BeginPublishOperation();
	const FCharacterLayerBakeSaveResult Result =
		CharacterLayerBakeCoordinator::SaveBakeSet(EditedLayerAsset);
	EndPublishOperation();
	HandleSaveResult(Result);
}

void FCharacterLayerAssetEditorToolkit::OverwriteFromLayerSource()
{
	if (!CachedBakeStatus || !CachedBakeStatus->bCanOverwriteFromSource) return;
	TArray<FString> Scope;
	for (const FSoftObjectPath& Path : CachedBakeStatus->OverwriteScope) Scope.Add(Path.ToString());
	const FText Message = FText::Format(
		LOCTEXT("ConfirmOverwrite", "Overwrite these owner-matching managed outputs from current Layer source?\n\n{0}\n\nThis discards external edits to the listed outputs. Competing ownership is never overwritten. Save remains explicit."),
		FText::FromString(FString::Join(Scope, TEXT("\n"))));
	if (!ConfirmPublishAction(Message)) return;
	RunBakeCommand(ECharacterLayerBakeOperation::All, true,
		LOCTEXT("OverwriteLabel", "Overwrite from Layer Source"));
}

void FCharacterLayerAssetEditorToolkit::RepairBakeSet()
{
	if (bPublishOperationInProgress || !CachedBakeStatus || !CachedBakeStatus->bCanRepair
		|| !EditedLayerAsset || !GetBaseProfile()) return;
	if (!ConfirmPublishAction(LOCTEXT("ConfirmRepair", "Repair will rebuild and verify the complete fixed-publishing bake set from authoritative Layer source. Continue? Save Bake Set remains explicit."))) return;
	BeginPublishOperation();
	const FCharacterLayerBakeResult Result = CharacterLayerBakeCoordinator::Repair(
		EditedLayerAsset, GetBaseProfile());
	EndPublishOperation();
	HandleBakeResult(Result, LOCTEXT("RepairLabel", "Repair"));
}

void FCharacterLayerAssetEditorToolkit::RebaseRegistration()
{
	if (bPublishOperationInProgress || !CachedBakeStatus
		|| !CachedBakeStatus->bCanRebaseRegistration || !EditedLayerAsset || !GetBaseProfile()) return;
	const FText Message = FText::Format(
		LOCTEXT("ConfirmRebase", "Rebase Registration will explicitly accept the Character Profile's current {0} animation row(s) in place of the registered {1}, then rebuild every current animation.\n\nOriginal source behind proven managed frames is preserved. New frames must point to explicit non-managed source. Removed outputs are frozen in place, never deleted. Continue?"),
		FText::AsNumber(GetBaseProfile()->Flipbooks.Num()),
		FText::AsNumber(EditedLayerAsset->AnimationRegistration.Num()));
	if (!ConfirmPublishAction(Message)) return;
	BeginPublishOperation();
	const FCharacterLayerBakeResult Result =
		CharacterLayerBakeCoordinator::RebaseRegistration(EditedLayerAsset, GetBaseProfile());
	EndPublishOperation();
	HandleBakeResult(Result, LOCTEXT("RebaseLabel", "Rebase Registration"));
}

void FCharacterLayerAssetEditorToolkit::DetachBakeSet()
{
	if (bPublishOperationInProgress || !CachedBakeStatus || !CachedBakeStatus->bCanDetach
		|| !EditedLayerAsset || !GetBaseProfile()) return;
	TArray<FString> Scope;
	for (const FSoftObjectPath& Path : EditedLayerAsset->BakeManifest.TouchedPackages)
	{
		Scope.Add(Path.ToString());
	}
	const FText Message = FText::Format(
		LOCTEXT("ConfirmDetach", "Advanced Detach freezes the exact current canonical output and releases only this bake set's matching management claims. It does not restore old art, reconstruct layers, or delete output.\n\nFrozen package scope:\n{0}\n\nThe manifest, adoption archive, and operation evidence remain on the Layer asset. Continue?"),
		FText::FromString(FString::Join(Scope, TEXT("\n"))));
	if (!ConfirmPublishAction(Message)) return;
	BeginPublishOperation();
	const FCharacterLayerBakeResult Result =
		CharacterLayerBakeCoordinator::Detach(EditedLayerAsset, GetBaseProfile());
	EndPublishOperation();
	HandleBakeResult(Result, LOCTEXT("DetachLabel", "Advanced Detach"));
}

void FCharacterLayerAssetEditorToolkit::EnableRuntimeCustomization()
{
	if (bPublishOperationInProgress || !EditedLayerAsset) return;
	FText Message;
	if (FCharacterLayerAssetActions::EnableRuntimeCustomization(
		EditedLayerAsset, /*bRequestConfirmation=*/true, &Message))
	{
		if (EditorModel.IsValid()) EditorModel->NotifyAssetDataChanged();
		else RefreshBakeStatus();
	}
}

FName FCharacterLayerAssetEditorToolkit::GetToolkitFName() const
{
	return FName("CharacterLayerAssetEditor");
}

FText FCharacterLayerAssetEditorToolkit::GetBaseToolkitName() const
{
	return LOCTEXT("ToolkitName", "Paper2D+ Character Layer Editor");
}

FString FCharacterLayerAssetEditorToolkit::GetWorldCentricTabPrefix() const
{
	return LOCTEXT("WorldCentricTabPrefix", "CharacterLayer ").ToString();
}

FLinearColor FCharacterLayerAssetEditorToolkit::GetWorldCentricTabColorScale() const
{
	return FLinearColor(0.5f, 0.3f, 0.1f, 1.0f);
}

#undef LOCTEXT_NAMESPACE
