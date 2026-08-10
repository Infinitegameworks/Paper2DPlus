// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "LayerAuthoringWorkspace.h"

#include "AnimationProfilePickerSource.h"
#include "AnimationProfileSwitcher.h"
#include "CharacterProfileEditorModel.h"
#include "FrameCueDataProvider.h"
#include "FrameEventEditor.h"
#include "HitboxDataProvider.h"
#include "HitboxEditorPanel.h"
#include "LayerArtInspector.h"
#include "LayerAppearancePanel.h"
#include "LayerCueInspector.h"
#include "LayerHitboxInspector.h"
#include "LayerOverviewPanel.h"
#include "LayerStructureTree.h"
#include "Paper2DPlusCharacterLayerAsset.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "ProfileNavigatorPanel.h"
#include "SlateShortcutUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "InputCoreTypes.h"
#include "Misc/ConfigCacheIni.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
#include "EditorStyleSet.h"
#ifndef FAppStyle
#define FAppStyle FEditorStyle
#define GetAppStyleSetName GetStyleSetName
#endif
#else
#include "Styling/AppStyle.h"
#endif

#define LOCTEXT_NAMESPACE "LayerAuthoringWorkspace"

namespace Paper2DPlusLayerWorkspacePrivate
{
	const TCHAR* ConfigSection = TEXT("Paper2DPlus.CharacterLayerWorkspace");
	const TCHAR* DrawerOpenKey = TEXT("AnimationDrawerOpen");
	const TCHAR* DrawerRatioKey = TEXT("AnimationDrawerRatio");
	constexpr float DrawerMinimumWidth = 900.0f;
	constexpr float StructureColumnMinimumWidth = 680.0f;

}

FLayerWorkspaceResponsiveState FLayerWorkspaceResponsiveState::Resolve(
	float Width,
	bool bDrawerRequested)
{
	FLayerWorkspaceResponsiveState State;
	State.bShowAnimationDrawer = bDrawerRequested
		&& Width >= Paper2DPlusLayerWorkspacePrivate::DrawerMinimumWidth;
	State.bShowStructureColumn = Width >= Paper2DPlusLayerWorkspacePrivate::StructureColumnMinimumWidth;
	State.bShowStructureOverlayButton = !State.bShowStructureColumn;
	return State;
}

void SLayerAuthoringWorkspace::Construct(const FArguments& InArgs)
{
	Model = InArgs._Model;
	LayerAsset = InArgs._LayerAsset;
	LoadPreferences();
	ResponsiveState = FLayerWorkspaceResponsiveState::Resolve(LastWidth, bAnimationDrawerRequested);
	AnimationSource = MakeShared<FAnimationProfilePickerSource>(Model);
	const TSharedPtr<IProfileItemPickerSource> SharedAnimationSource =
		StaticCastSharedPtr<IProfileItemPickerSource>(AnimationSource);

	// U12 mounts each proven domain controller exactly once. All three read the same model selection;
	// the mode switch changes presentation only and never creates a second frame/playback state.
	LayerHitboxProvider = MakeShared<FLayerHitboxDataProvider>(LayerAsset, Model);
	HitboxController = SNew(SHitboxEditorPanel)
		.Model(Model)
		.LayerAsset(LayerAsset)
		.FrameDataProvider(LayerHitboxProvider)
		.HostContract(FProfileToolPanelHostContract::External());
	LayerCueProvider = MakeShared<FLayerFrameCueDataProvider>(LayerAsset, Model);
	CueController = SNew(SFrameEventEditor)
		.Model(Model)
		.DataProvider(LayerCueProvider)
		.HostContract(FProfileToolPanelHostContract::External());
	ArtInspector = SNew(SLayerArtInspector)
		.Model(Model)
		.LayerAsset(LayerAsset.Get());
	HitboxInspector = SNew(SLayerHitboxInspector)
		.Controller(HitboxController);
	CueInspector = SNew(SLayerCueInspector)
		.Model(Model)
		.Controller(CueController);
	OverviewPanel = SNew(SLayerOverviewPanel)
		.Model(Model)
		.LayerAsset(LayerAsset.Get())
		.LayerFirstPresentation(true);
	AppearancePanel = SNew(SLayerAppearancePanel)
		.LayerAsset(LayerAsset.Get());

	TSharedRef<SWidget> InspectorModes =
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(3.0f, 2.0f))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f)
			[
				SNew(SCheckBox)
				.Style(FAppStyle::Get(), "ToggleButtonCheckbox")
				.IsChecked(this, &SLayerAuthoringWorkspace::GetModeCheckState, ELayerAuthoringMode::Art)
				.OnCheckStateChanged_Lambda([this](ECheckBoxState State)
				{
					if (State == ECheckBoxState::Checked) SetMode(ELayerAuthoringMode::Art);
				})
				.ToolTipText(LOCTEXT("ArtModeTip", "Author the selected layer's art and placement."))
				.AccessibleText(LOCTEXT("ArtModeAccessible", "Layer Art mode"))
				[
					SNew(STextBlock).Text(LOCTEXT("ArtMode", "Art")).Justification(ETextJustify::Center)
				]
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(2.0f, 0.0f)
			[
				SNew(SCheckBox)
				.Style(FAppStyle::Get(), "ToggleButtonCheckbox")
				.IsChecked(this, &SLayerAuthoringWorkspace::GetModeCheckState, ELayerAuthoringMode::Hitboxes)
				.OnCheckStateChanged_Lambda([this](ECheckBoxState State)
				{
					if (State == ECheckBoxState::Checked) SetMode(ELayerAuthoringMode::Hitboxes);
				})
				.ToolTipText(LOCTEXT("HitboxModeTip", "Author layer-local combat boxes and sockets."))
				.AccessibleText(LOCTEXT("HitboxModeAccessible", "Layer Hitboxes mode"))
				[
					SNew(STextBlock).Text(LOCTEXT("HitboxMode", "Hitboxes")).Justification(ETextJustify::Center)
				]
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f)
			[
				SNew(SCheckBox)
				.Style(FAppStyle::Get(), "ToggleButtonCheckbox")
				.IsChecked(this, &SLayerAuthoringWorkspace::GetModeCheckState, ELayerAuthoringMode::FrameCues)
				.OnCheckStateChanged_Lambda([this](ECheckBoxState State)
				{
					if (State == ECheckBoxState::Checked) SetMode(ELayerAuthoringMode::FrameCues);
				})
				.ToolTipText(LOCTEXT("CueModeTip", "Author Frame Cues owned by the selected layer."))
				.AccessibleText(LOCTEXT("CueModeAccessible", "Layer Frame Cues mode"))
				[
					SNew(STextBlock).Text(LOCTEXT("CueMode", "Frame Cues")).Justification(ETextJustify::Center)
				]
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(2.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SCheckBox)
				.Style(FAppStyle::Get(), "ToggleButtonCheckbox")
				.IsChecked(this, &SLayerAuthoringWorkspace::GetModeCheckState, ELayerAuthoringMode::Appearance)
				.OnCheckStateChanged_Lambda([this](ECheckBoxState State)
				{
					if (State == ECheckBoxState::Checked) SetMode(ELayerAuthoringMode::Appearance);
				})
				.ToolTipText(LOCTEXT("AppearanceModeTip", "Author complete Layer selections, the Default Appearance, and optional Exclusive Groups."))
				.AccessibleText(LOCTEXT("AppearanceModeAccessible", "Layer Appearance mode"))
				[
					SNew(STextBlock).Text(LOCTEXT("AppearanceMode", "Appearance")).Justification(ETextJustify::Center)
				]
			]
		]
		+ SVerticalBox::Slot().FillHeight(1.0f)
		[
			SAssignNew(InspectorSwitcher, SWidgetSwitcher)
			.WidgetIndex(0)
			+ SWidgetSwitcher::Slot()
			[
				ArtInspector.ToSharedRef()
			]
			+ SWidgetSwitcher::Slot()
			[
				HitboxInspector.ToSharedRef()
			]
			+ SWidgetSwitcher::Slot()
			[
				CueInspector.ToSharedRef()
			]
			+ SWidgetSwitcher::Slot()
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
				.Padding(8.0f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("AppearanceInspectorHelp", "Presets are complete snapshots. Layer order comes only from Structure; Exclusive Groups are optional and allow zero or one active member."))
					.AutoWrapText(true)
				]
			]
		];

	TSharedRef<SWidget> WorkspaceBody =
		SNew(SOverlay)
		+ SOverlay::Slot()
		[
			SNew(SSplitter)
			.Orientation(Orient_Horizontal)
			+ SSplitter::Slot()
			.Value_Lambda([this]() { return AnimationDrawerRatio; })
			.MinSize(210.0f)
			.OnSlotResized(this, &SLayerAuthoringWorkspace::HandleDrawerResized)
			[
				SNew(SBox)
				.Visibility(this, &SLayerAuthoringWorkspace::GetAnimationDrawerVisibility)
				.Padding(FMargin(2.0f))
				[
					SAssignNew(AnimationDrawer, SProfileNavigatorPanel)
					.Source(SharedAnimationSource)
					.Mode(EProfileNavigatorMode::Pinned)
					.OnGenerateItemPreview(FOnGenerateProfileNavigatorItemPreview::CreateStatic(
						&BuildAnimationProfileItemPreview))
				]
			]
			+ SSplitter::Slot()
			.Value(0.20f)
			.MinSize(220.0f)
			[
				SNew(SBox)
				.Visibility(this, &SLayerAuthoringWorkspace::GetStructureColumnVisibility)
				[
					SAssignNew(StructureTree, SLayerStructureTree)
					.Model(Model)
					.LayerAsset(LayerAsset.Get())
				]
			]
			+ SSplitter::Slot()
			.Value(0.80f)
			.MinSize(360.0f)
			[
				SNew(SSplitter)
				.Orientation(Orient_Horizontal)
				+ SSplitter::Slot().Value(0.67f).MinSize(280.0f)
				[
					SAssignNew(CentralModeSwitcher, SWidgetSwitcher)
					.WidgetIndex(0)
					+ SWidgetSwitcher::Slot()
					[
						OverviewPanel.ToSharedRef()
					]
					+ SWidgetSwitcher::Slot()
					[
						HitboxController.ToSharedRef()
					]
					+ SWidgetSwitcher::Slot()
					[
						CueController.ToSharedRef()
					]
					+ SWidgetSwitcher::Slot()
					[
						AppearancePanel.ToSharedRef()
					]
				]
				+ SSplitter::Slot().Value(0.33f).MinSize(190.0f)
				[
					InspectorModes
				]
			]
		]
		+ SOverlay::Slot()
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Fill)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
			.Padding(4.0f)
			.Visibility(this, &SLayerAuthoringWorkspace::GetStructureOverlayVisibility)
			[
				SNew(SBox)
				.WidthOverride(320.0f)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right)
					[
						SNew(SButton)
						.Text(FText::FromString(TEXT("×")))
						.ToolTipText(LOCTEXT("CloseStructureOverlayTip", "Close the layer structure drawer."))
						.AccessibleText(LOCTEXT("CloseStructureOverlayAccessible", "Close layer structure drawer"))
						.OnClicked(this, &SLayerAuthoringWorkspace::ToggleStructureOverlay)
					]
					+ SVerticalBox::Slot().FillHeight(1.0f)
					[
						SAssignNew(OverlayStructureTree, SLayerStructureTree)
						.Model(Model)
						.LayerAsset(LayerAsset.Get())
					]
				]
			]
		];

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(3.0f, 0.0f, 3.0f, 3.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
			[
				SAssignNew(AnimationSwitcher, SAnimationProfileSwitcher)
				.Source(SharedAnimationSource)
				.EmptySelectionText(this, &SLayerAuthoringWorkspace::GetEmptyProfileText)
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SButton)
				.Text_Lambda([this]()
				{
					return bAnimationDrawerRequested
						? LOCTEXT("HideAnimations", "Hide Animations")
						: LOCTEXT("ShowAnimations", "Browse Animations");
				})
				.ToolTipText(LOCTEXT("AnimationDrawerTip", "Toggle the optional searchable animation drawer. It collapses automatically at narrow widths."))
				.AccessibleText(LOCTEXT("AnimationDrawerAccessible", "Toggle searchable animation drawer"))
				.OnClicked(this, &SLayerAuthoringWorkspace::ToggleAnimationDrawer)
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("StructureButton", "Layers"))
				.ToolTipText(LOCTEXT("StructureButtonTip", "Open the layer structure drawer."))
				.AccessibleText(LOCTEXT("StructureButtonAccessible", "Open layer structure drawer"))
				.Visibility(this, &SLayerAuthoringWorkspace::GetStructureOverlayButtonVisibility)
				.OnClicked(this, &SLayerAuthoringWorkspace::ToggleStructureOverlay)
			]
		]
		+ SVerticalBox::Slot().FillHeight(1.0f)
		[
			WorkspaceBody
		]
	];
}

SLayerAuthoringWorkspace::~SLayerAuthoringWorkspace()
{
	if (HitboxController.IsValid()) HitboxController->HandleHostDeactivated();
	if (CueInspector.IsValid()) CueInspector->HandleHostDeactivated();
	if (OverviewPanel.IsValid()) OverviewPanel->HandleAuthoringModeDeactivated();
	SavePreferences();
}

TSharedPtr<IProfileItemPickerSource> SLayerAuthoringWorkspace::GetAnimationSourceForTests() const
{
	return StaticCastSharedPtr<IProfileItemPickerSource>(AnimationSource);
}

void SLayerAuthoringWorkspace::SetModeForTests(ELayerAuthoringMode Mode)
{
	SetMode(Mode);
}

void SLayerAuthoringWorkspace::PrepareForPublish()
{
	if (bPublishPrepared) return;
	bPublishPrepared = true;
	if (Model.IsValid()) Model->SetQueuePlaying(false);
	if (CueController.IsValid()) CueController->StopPlayback();
	switch (ActiveMode)
	{
	case ELayerAuthoringMode::Art:
		if (OverviewPanel.IsValid()) OverviewPanel->HandleAuthoringModeDeactivated();
		break;
	case ELayerAuthoringMode::Hitboxes:
		if (HitboxController.IsValid()) HitboxController->HandleHostDeactivated();
		break;
	case ELayerAuthoringMode::FrameCues:
		if (CueInspector.IsValid()) CueInspector->HandleHostDeactivated();
		break;
	case ELayerAuthoringMode::Appearance:
		break;
	}
}

void SLayerAuthoringWorkspace::RestoreAfterPublish()
{
	if (!bPublishPrepared) return;
	bPublishPrepared = false;
	if (ActiveMode == ELayerAuthoringMode::FrameCues && CueInspector.IsValid())
	{
		CueInspector->HandleHostActivated();
	}
}

bool SLayerAuthoringWorkspace::IsAnimationDrawerVisibleForTests() const
{
	return ResponsiveState.bShowAnimationDrawer;
}

FReply SLayerAuthoringWorkspace::OnKeyDown(
	const FGeometry& MyGeometry,
	const FKeyEvent& InKeyEvent)
{
	if (Paper2DPlusEditor::SlateShortcutUtils::ShouldIgnoreShortcutForFocusedWidget())
	{
		return SCompoundWidget::OnKeyDown(MyGeometry, InKeyEvent);
	}
	if (InKeyEvent.GetKey() == EKeys::Escape && bStructureOverlayOpen)
	{
		bStructureOverlayOpen = false;
		Invalidate(EInvalidateWidgetReason::Layout);
		return FReply::Handled();
	}
	if (InKeyEvent.IsControlDown())
	{
		if (InKeyEvent.GetKey() == EKeys::PageUp) return NavigateAnimation(-1);
		if (InKeyEvent.GetKey() == EKeys::PageDown) return NavigateAnimation(1);
		if (InKeyEvent.GetKey() == EKeys::B) return ToggleAnimationDrawer();
		if (InKeyEvent.GetKey() == EKeys::One) return SetMode(ELayerAuthoringMode::Art);
		if (InKeyEvent.GetKey() == EKeys::Two) return SetMode(ELayerAuthoringMode::Hitboxes);
		if (InKeyEvent.GetKey() == EKeys::Three) return SetMode(ELayerAuthoringMode::FrameCues);
		if (InKeyEvent.GetKey() == EKeys::Four) return SetMode(ELayerAuthoringMode::Appearance);
	}
	return SCompoundWidget::OnKeyDown(MyGeometry, InKeyEvent);
}

void SLayerAuthoringWorkspace::Tick(
	const FGeometry& AllottedGeometry,
	const double InCurrentTime,
	const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	const float Width = AllottedGeometry.GetLocalSize().X;
	if (FMath::IsNearlyEqual(Width, LastWidth, 0.5f)) return;
	LastWidth = Width;
	const FLayerWorkspaceResponsiveState NewState =
		FLayerWorkspaceResponsiveState::Resolve(Width, bAnimationDrawerRequested);
	const bool bChanged = NewState.bShowAnimationDrawer != ResponsiveState.bShowAnimationDrawer
		|| NewState.bShowStructureColumn != ResponsiveState.bShowStructureColumn
		|| NewState.bShowStructureOverlayButton != ResponsiveState.bShowStructureOverlayButton;
	ResponsiveState = NewState;
	if (ResponsiveState.bShowStructureColumn) bStructureOverlayOpen = false;
	if (bChanged) Invalidate(EInvalidateWidgetReason::Layout);
}

FReply SLayerAuthoringWorkspace::NavigateAnimation(int32 Delta)
{
	if (!CanNavigateAnimation(Delta) || !Model.IsValid()) return FReply::Handled();
	Model->SetSelectedFlipbook(Model->GetSelectedFlipbookIndex() + Delta);
	return FReply::Handled();
}

bool SLayerAuthoringWorkspace::CanNavigateAnimation(int32 Delta) const
{
	const UPaper2DPlusCharacterProfileAsset* Profile = Model.IsValid() ? Model->GetAsset() : nullptr;
	if (!Profile) return false;
	const int32 Target = Model->GetSelectedFlipbookIndex() + Delta;
	return Profile->Flipbooks.IsValidIndex(Target);
}

FReply SLayerAuthoringWorkspace::ToggleAnimationDrawer()
{
	bAnimationDrawerRequested = !bAnimationDrawerRequested;
	ResponsiveState = FLayerWorkspaceResponsiveState::Resolve(LastWidth, bAnimationDrawerRequested);
	SavePreferences();
	Invalidate(EInvalidateWidgetReason::Layout);
	return FReply::Handled();
}

FReply SLayerAuthoringWorkspace::ToggleStructureOverlay()
{
	bStructureOverlayOpen = !bStructureOverlayOpen;
	Invalidate(EInvalidateWidgetReason::Layout);
	return FReply::Handled();
}

FReply SLayerAuthoringWorkspace::SetMode(ELayerAuthoringMode Mode)
{
	if (ActiveMode == Mode) return FReply::Handled();
	if (ActiveMode == ELayerAuthoringMode::Art && OverviewPanel.IsValid())
	{
		OverviewPanel->HandleAuthoringModeDeactivated();
	}
	else if (ActiveMode == ELayerAuthoringMode::Hitboxes && HitboxController.IsValid())
	{
		HitboxController->HandleHostDeactivated();
	}
	else if (ActiveMode == ELayerAuthoringMode::FrameCues && CueInspector.IsValid())
	{
		CueInspector->HandleHostDeactivated();
	}
	ActiveMode = Mode;
	if (ActiveMode == ELayerAuthoringMode::FrameCues && CueInspector.IsValid())
	{
		CueInspector->HandleHostActivated();
	}
	if (CentralModeSwitcher.IsValid()) CentralModeSwitcher->SetActiveWidgetIndex(static_cast<int32>(Mode));
	if (InspectorSwitcher.IsValid()) InspectorSwitcher->SetActiveWidgetIndex(static_cast<int32>(Mode));
	// Ctrl+1/2/3/4 does not naturally transfer keyboard focus the way clicking a toggle does. Move focus
	// explicitly so a collapsed controller can never continue receiving Delete/nudge/playback shortcuts.
	if (FSlateApplication::IsInitialized())
	{
		switch (ActiveMode)
		{
		case ELayerAuthoringMode::Art:
			if (OverviewPanel.IsValid())
			{
				FSlateApplication::Get().SetKeyboardFocus(OverviewPanel, EFocusCause::SetDirectly);
			}
			break;
		case ELayerAuthoringMode::Hitboxes:
			if (HitboxController.IsValid())
			{
				FSlateApplication::Get().SetKeyboardFocus(HitboxController, EFocusCause::SetDirectly);
			}
			break;
		case ELayerAuthoringMode::FrameCues:
			if (CueController.IsValid())
			{
				FSlateApplication::Get().SetKeyboardFocus(CueController, EFocusCause::SetDirectly);
			}
			break;
		case ELayerAuthoringMode::Appearance:
			if (AppearancePanel.IsValid())
			{
				FSlateApplication::Get().SetKeyboardFocus(AppearancePanel, EFocusCause::SetDirectly);
			}
			break;
		}
	}
	return FReply::Handled();
}

ECheckBoxState SLayerAuthoringWorkspace::GetModeCheckState(ELayerAuthoringMode Mode) const
{
	return ActiveMode == Mode ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

EVisibility SLayerAuthoringWorkspace::GetAnimationDrawerVisibility() const
{
	return ResponsiveState.bShowAnimationDrawer ? EVisibility::Visible : EVisibility::Collapsed;
}

EVisibility SLayerAuthoringWorkspace::GetStructureColumnVisibility() const
{
	return ResponsiveState.bShowStructureColumn ? EVisibility::Visible : EVisibility::Collapsed;
}

EVisibility SLayerAuthoringWorkspace::GetStructureOverlayButtonVisibility() const
{
	return ResponsiveState.bShowStructureOverlayButton ? EVisibility::Visible : EVisibility::Collapsed;
}

EVisibility SLayerAuthoringWorkspace::GetStructureOverlayVisibility() const
{
	return bStructureOverlayOpen && ResponsiveState.bShowStructureOverlayButton
		? EVisibility::Visible : EVisibility::Collapsed;
}

FText SLayerAuthoringWorkspace::GetSelectedLayerTitle() const
{
	const UPaper2DPlusCharacterLayerAsset* Asset = LayerAsset.Get();
	const FCharacterLayer* Layer = Asset && Model.IsValid()
		? Asset->GetLayerById(Model->GetSelectedLayerId()) : nullptr;
	return Layer
		? FText::FromString(Layer->LayerName)
		: LOCTEXT("NoLayerSelected", "No Layer Selected");
}

FText SLayerAuthoringWorkspace::GetSelectedLayerSummary() const
{
	const UPaper2DPlusCharacterLayerAsset* Asset = LayerAsset.Get();
	if (!Asset || !Model.IsValid()) return LOCTEXT("LayerAssetUnavailable", "Layer Asset unavailable.");
	if (Asset->Layers.IsEmpty())
		return LOCTEXT("NoLayersSummary", "No layers exist yet. Import layered art or add source data to begin.");
	const FCharacterLayer* Layer = Asset->GetLayerById(Model->GetSelectedLayerId());
	if (!Layer)
		return LOCTEXT("ChooseLayerSummary", "Choose a layer in Character Structure or click visible art in the canvas.");

	FString GroupName = TEXT("Ungrouped");
	if (const FCharacterLayerGroupInfo* Group = Asset->LayerGroups.FindByPredicate([Layer](const FCharacterLayerGroupInfo& Row)
	{
		return Row.GroupId == Layer->GroupId;
	}))
	{
		GroupName = Group->DisplayName.ToString();
	}
	const UPaper2DPlusCharacterProfileAsset* Profile = Model->GetAsset();
	FString AnimationName;
	if (Profile && Profile->Flipbooks.IsValidIndex(Model->GetSelectedFlipbookIndex()))
	{
		AnimationName = Profile->Flipbooks[Model->GetSelectedFlipbookIndex()].Identity.FlipbookName;
	}
	const bool bMapped = !AnimationName.IsEmpty() && Layer->FindAnimationMapping(AnimationName) != nullptr;
	const int32 GlobalOrderIndex = Asset->Layers.IndexOfByPredicate([Layer](const FCharacterLayer& Candidate)
	{
		return Candidate.LayerId == Layer->LayerId;
	});
	return FText::Format(
		LOCTEXT("LayerSummaryFormat", "Group: {0}\nVisual order: {1}\nCurrent animation: {2}\nArt mapping: {3}"),
		FText::FromString(GroupName),
		FText::AsNumber(GlobalOrderIndex),
		AnimationName.IsEmpty() ? LOCTEXT("NoCurrentAnimation", "None") : FText::FromString(AnimationName),
		bMapped ? LOCTEXT("Mapped", "Mapped") : LOCTEXT("NotMapped", "Not mapped"));
}

FText SLayerAuthoringWorkspace::GetEmptyProfileText() const
{
	const UPaper2DPlusCharacterLayerAsset* Asset = LayerAsset.Get();
	if (!Asset) return LOCTEXT("NoLayerAsset", "Layer Asset unavailable");
	if (Asset->BaseProfile.IsNull()) return LOCTEXT("AssignBaseProfile", "Assign a Base Profile to choose animations");
	const UPaper2DPlusCharacterProfileAsset* Profile = Model.IsValid() ? Model->GetAsset() : nullptr;
	if (!Profile) return LOCTEXT("BaseProfileUnavailable", "Base Profile unavailable");
	if (Profile->Flipbooks.IsEmpty()) return LOCTEXT("EmptyBaseProfile", "Base Profile has no animations");
	return LOCTEXT("ChooseAnimation", "Choose an animation");
}

void SLayerAuthoringWorkspace::LoadPreferences()
{
	if (!GConfig) return;
	GConfig->GetBool(
		Paper2DPlusLayerWorkspacePrivate::ConfigSection,
		Paper2DPlusLayerWorkspacePrivate::DrawerOpenKey,
		bAnimationDrawerRequested,
		GEditorPerProjectIni);
	float StoredRatio = AnimationDrawerRatio;
	if (GConfig->GetFloat(
		Paper2DPlusLayerWorkspacePrivate::ConfigSection,
		Paper2DPlusLayerWorkspacePrivate::DrawerRatioKey,
		StoredRatio,
		GEditorPerProjectIni))
	{
		AnimationDrawerRatio = FMath::Clamp(StoredRatio, 0.12f, 0.45f);
	}
}

void SLayerAuthoringWorkspace::SavePreferences() const
{
	if (!GConfig) return;
	GConfig->SetBool(
		Paper2DPlusLayerWorkspacePrivate::ConfigSection,
		Paper2DPlusLayerWorkspacePrivate::DrawerOpenKey,
		bAnimationDrawerRequested,
		GEditorPerProjectIni);
	GConfig->SetFloat(
		Paper2DPlusLayerWorkspacePrivate::ConfigSection,
		Paper2DPlusLayerWorkspacePrivate::DrawerRatioKey,
		AnimationDrawerRatio,
		GEditorPerProjectIni);
	// Intentionally no Flush: the editor owns lifecycle persistence for GEditorPerProjectIni.
}

void SLayerAuthoringWorkspace::HandleDrawerResized(float NewSize)
{
	if (NewSize <= 0.0f || !ResponsiveState.bShowAnimationDrawer) return;
	AnimationDrawerRatio = FMath::Clamp(NewSize, 0.12f, 0.45f);
	SavePreferences();
}

#undef LOCTEXT_NAMESPACE
