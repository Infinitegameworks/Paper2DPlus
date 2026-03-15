// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "CharacterProfileAssetEditor.h"
#include "FrameTimingEditor.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "PaperFlipbook.h"
#include "PaperSprite.h"
#include "Paper2DPlusSettings.h"
#include "ScopedTransaction.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/MessageDialog.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Framework/Notifications/NotificationManager.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Misc/ConfigCacheIni.h"
#include "PropertyCustomizationHelpers.h"
#include "Widgets/Input/SNumericEntryBox.h"

#define LOCTEXT_NAMESPACE "CharacterProfileAssetEditor"

namespace CharacterProfileEditorLayoutConfig
{
	static const TCHAR* SectionName = TEXT("Paper2DPlus.CharacterProfileEditor.Layout");
	static const TCHAR* HitboxSidebarOrderKey = TEXT("HitboxSidebarOrder");
	static const TCHAR* AlignmentLeftOrderKey = TEXT("AlignmentLeftOrder");
	static const TCHAR* OverviewSplitterLeftKey = TEXT("OverviewSplitterLeft");
	static const TCHAR* OverviewSplitterRightKey = TEXT("OverviewSplitterRight");
	static const TCHAR* HitboxSplitterLeftKey = TEXT("HitboxSplitterLeft");
	static const TCHAR* HitboxSplitterCenterKey = TEXT("HitboxSplitterCenter");
	static const TCHAR* HitboxSplitterRightKey = TEXT("HitboxSplitterRight");
	static const TCHAR* AlignmentSplitterLeftKey = TEXT("AlignmentSplitterLeft");
	static const TCHAR* AlignmentSplitterCenterKey = TEXT("AlignmentSplitterCenter");
	static const TCHAR* AlignmentSplitterRightKey = TEXT("AlignmentSplitterRight");
}

// SCharacterProfileEditorCanvas implementation -> SCharacterProfileEditorCanvas.cpp
// FHitbox3DViewportClient + SHitbox3DViewport implementations -> SHitbox3DViewport.cpp
// FCharacterProfileAssetEditorToolkit implementation -> CharacterProfileAssetEditorToolkit.cpp
// SSpriteAlignmentCanvas implementation -> SSpriteAlignmentCanvas.cpp

// ==========================================
// SCharacterProfileAssetEditor Implementation
// ==========================================

SCharacterProfileAssetEditor::~SCharacterProfileAssetEditor()
{
	StopPlayback();
	if (GEditor)
	{
		GEditor->UnregisterForUndo(this);
	}
	FCoreUObjectDelegates::OnObjectModified.Remove(OnObjectModifiedHandle);
}

void SCharacterProfileAssetEditor::InitializeSectionLayouts()
{
	HitboxSidebarSectionOrder = {
		FName(TEXT("Hitboxes")),
		FName(TEXT("Properties")),
		FName(TEXT("FrameOps"))
	};
	LoadSectionOrder(CharacterProfileEditorLayoutConfig::HitboxSidebarOrderKey, HitboxSidebarSectionOrder, HitboxSidebarSectionOrder);

	AlignmentLeftSectionOrder = {
		FName(TEXT("Flipbooks")),
		FName(TEXT("Queue")),
		FName(TEXT("Frames"))
	};
	LoadSectionOrder(CharacterProfileEditorLayoutConfig::AlignmentLeftOrderKey, AlignmentLeftSectionOrder, AlignmentLeftSectionOrder);

	LoadFloatLayoutValue(CharacterProfileEditorLayoutConfig::OverviewSplitterLeftKey, 0.7f, OverviewSplitterLeftRatio);
	LoadFloatLayoutValue(CharacterProfileEditorLayoutConfig::OverviewSplitterRightKey, 0.3f, OverviewSplitterRightRatio);
	LoadFloatLayoutValue(CharacterProfileEditorLayoutConfig::HitboxSplitterLeftKey, 0.2f, HitboxSplitterLeftRatio);
	LoadFloatLayoutValue(CharacterProfileEditorLayoutConfig::HitboxSplitterCenterKey, 0.55f, HitboxSplitterCenterRatio);
	LoadFloatLayoutValue(CharacterProfileEditorLayoutConfig::HitboxSplitterRightKey, 0.25f, HitboxSplitterRightRatio);
	LoadFloatLayoutValue(CharacterProfileEditorLayoutConfig::AlignmentSplitterLeftKey, 0.2f, AlignmentSplitterLeftRatio);
	LoadFloatLayoutValue(CharacterProfileEditorLayoutConfig::AlignmentSplitterCenterKey, 0.6f, AlignmentSplitterCenterRatio);
	LoadFloatLayoutValue(CharacterProfileEditorLayoutConfig::AlignmentSplitterRightKey, 0.2f, AlignmentSplitterRightRatio);
}

void SCharacterProfileAssetEditor::LoadSectionOrder(const FString& ConfigKey, const TArray<FName>& DefaultOrder, TArray<FName>& InOutOrder) const
{
	InOutOrder = DefaultOrder;

	if (!GConfig)
	{
		return;
	}

	FString StoredOrder;
	if (!GConfig->GetString(CharacterProfileEditorLayoutConfig::SectionName, *ConfigKey, StoredOrder, GEditorPerProjectIni)
		|| StoredOrder.IsEmpty())
	{
		return;
	}

	TArray<FString> Tokens;
	StoredOrder.ParseIntoArray(Tokens, TEXT(","), true);

	TArray<FName> ParsedOrder;
	for (const FString& Token : Tokens)
	{
		const FString Trimmed = Token.TrimStartAndEnd();
		if (Trimmed.IsEmpty())
		{
			continue;
		}

		const FName SectionId(*Trimmed);
		if (DefaultOrder.Contains(SectionId) && !ParsedOrder.Contains(SectionId))
		{
			ParsedOrder.Add(SectionId);
		}
	}

	for (const FName& DefaultId : DefaultOrder)
	{
		if (!ParsedOrder.Contains(DefaultId))
		{
			ParsedOrder.Add(DefaultId);
		}
	}

	if (ParsedOrder.Num() == DefaultOrder.Num())
	{
		InOutOrder = MoveTemp(ParsedOrder);
	}
}

void SCharacterProfileAssetEditor::SaveSectionOrder(const FString& ConfigKey, const TArray<FName>& Order) const
{
	if (!GConfig)
	{
		return;
	}

	const FString StoredOrder = FString::JoinBy(Order, TEXT(","), [](const FName& SectionId)
	{
		return SectionId.ToString();
	});

	GConfig->SetString(CharacterProfileEditorLayoutConfig::SectionName, *ConfigKey, *StoredOrder, GEditorPerProjectIni);
	GConfig->Flush(false, GEditorPerProjectIni);
}

void SCharacterProfileAssetEditor::LoadFloatLayoutValue(const FString& ConfigKey, float DefaultValue, float& OutValue) const
{
	OutValue = DefaultValue;
	if (!GConfig)
	{
		return;
	}

	float StoredValue = DefaultValue;
	if (GConfig->GetFloat(CharacterProfileEditorLayoutConfig::SectionName, *ConfigKey, StoredValue, GEditorPerProjectIni))
	{
		OutValue = FMath::Clamp(StoredValue, 0.05f, 0.95f);
	}
}

void SCharacterProfileAssetEditor::SaveFloatLayoutValue(const FString& ConfigKey, float Value) const
{
	if (!GConfig)
	{
		return;
	}

	GConfig->SetFloat(CharacterProfileEditorLayoutConfig::SectionName, *ConfigKey, FMath::Clamp(Value, 0.05f, 0.95f), GEditorPerProjectIni);
	GConfig->Flush(false, GEditorPerProjectIni);
}

bool SCharacterProfileAssetEditor::CanMoveSection(const TArray<FName>& SectionOrder, FName SectionId, int32 Direction) const
{
	const int32 CurrentIndex = SectionOrder.IndexOfByKey(SectionId);
	if (CurrentIndex == INDEX_NONE)
	{
		return false;
	}

	const int32 TargetIndex = CurrentIndex + Direction;
	return TargetIndex >= 0 && TargetIndex < SectionOrder.Num();
}

void SCharacterProfileAssetEditor::MoveSectionInOrder(TArray<FName>& SectionOrder, FName SectionId, int32 Direction, const FString& ConfigKey)
{
	const int32 CurrentIndex = SectionOrder.IndexOfByKey(SectionId);
	if (CurrentIndex == INDEX_NONE)
	{
		return;
	}

	const int32 TargetIndex = CurrentIndex + Direction;
	if (TargetIndex < 0 || TargetIndex >= SectionOrder.Num())
	{
		return;
	}

	SectionOrder.Swap(CurrentIndex, TargetIndex);
	SaveSectionOrder(ConfigKey, SectionOrder);
}

void SCharacterProfileAssetEditor::MoveHitboxSidebarSection(FName SectionId, int32 Direction)
{
	MoveSectionInOrder(HitboxSidebarSectionOrder, SectionId, Direction, CharacterProfileEditorLayoutConfig::HitboxSidebarOrderKey);
	RebuildHitboxSidebarSections();
}

void SCharacterProfileAssetEditor::MoveAlignmentLeftSection(FName SectionId, int32 Direction)
{
	MoveSectionInOrder(AlignmentLeftSectionOrder, SectionId, Direction, CharacterProfileEditorLayoutConfig::AlignmentLeftOrderKey);
	RebuildAlignmentLeftSections();
}

void SCharacterProfileAssetEditor::SetActivePanelSection(FName SectionId)
{
	ActivePanelSectionId = SectionId;
}

bool SCharacterProfileAssetEditor::IsActivePanelSection(FName SectionId) const
{
	return ActivePanelSectionId == SectionId;
}

const FLinearColor SCharacterProfileAssetEditor::ActivePanelHighlightColor = FLinearColor(0.0f, 0.0f, 0.0f, 0.0f);
const FLinearColor SCharacterProfileAssetEditor::InactivePanelColor = FLinearColor(0.0f, 0.0f, 0.0f, 0.0f);
const FLinearColor SCharacterProfileAssetEditor::SelectedFrameHighlightColor = FLinearColor(0.15f, 0.45f, 0.75f, 1.0f);
const FLinearColor SCharacterProfileAssetEditor::ActiveFrameColor = FLinearColor(0.20f, 0.60f, 0.30f, 1.0f);

void SCharacterProfileAssetEditor::ForEachSelectedFrame(TFunctionRef<void(int32)> Op)
{
	const int32 FrameCount = GetCurrentFrameCount();
	if (SelectedFrames.Num() > 0)
	{
		for (int32 Idx : SelectedFrames)
		{
			if (Idx >= 0 && Idx < FrameCount)
			{
				Op(Idx);
			}
		}
	}
	else
	{
		if (SelectedFrameIndex >= 0 && SelectedFrameIndex < FrameCount)
		{
			Op(SelectedFrameIndex);
		}
	}
}

void SCharacterProfileAssetEditor::ClearFrameSelection()
{
	SelectedFrames.Empty();
	FrameSelectionAnchorIndex = INDEX_NONE;
}

TSharedRef<SWidget> SCharacterProfileAssetEditor::WrapWithActivePanelHighlight(FName SectionId, float InnerPadding, TSharedRef<SWidget> Content)
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
		.BorderBackgroundColor_Lambda([this, SectionId]()
		{
			return IsActivePanelSection(SectionId)
				? ActivePanelHighlightColor
				: InactivePanelColor;
		})
		.Padding(1)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
			.Padding(InnerPadding)
			.OnMouseButtonDown_Lambda([this, SectionId](const FGeometry&, const FPointerEvent&)
			{
				SetActivePanelSection(SectionId);
				return FReply::Unhandled();
			})
			[
				Content
			]
		];
}

void SCharacterProfileAssetEditor::TriggerPendingRenameIfNeeded(TMap<int32, TSharedPtr<SInlineEditableTextBlock>>& NameTexts)
{
	if (PendingRenameFlipbookIndex != INDEX_NONE)
	{
		int32 RenameIdx = PendingRenameFlipbookIndex;
		PendingRenameFlipbookIndex = INDEX_NONE;

		if (TSharedPtr<SInlineEditableTextBlock>* FoundText = NameTexts.Find(RenameIdx))
		{
			TWeakPtr<SInlineEditableTextBlock> WeakText = *FoundText;
			RegisterActiveTimer(0.0f, FWidgetActiveTimerDelegate::CreateLambda(
				[WeakText](double, float) -> EActiveTimerReturnType
				{
					if (TSharedPtr<SInlineEditableTextBlock> Text = WeakText.Pin())
					{
						Text->EnterEditingMode();
					}
					return EActiveTimerReturnType::Stop;
				}));
		}
	}
}

TSharedRef<SWidget> SCharacterProfileAssetEditor::BuildReorderableSectionCard(
	FName SectionId,
	const FText& SectionTitle,
	const FText& SectionTooltip,
	TSharedRef<SWidget> ContentWidget,
	bool bStretchContent)
{
	TSharedRef<SVerticalBox> CardContent = SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0, 0, 0, 4)
		[
			SNew(STextBlock)
			.Text(SectionTitle)
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
		];

	if (bStretchContent)
	{
		CardContent->AddSlot()
		.FillHeight(1.0f)
		[
			ContentWidget
		];
	}
	else
	{
		CardContent->AddSlot()
		.AutoHeight()
		[
			ContentWidget
		];
	}

	// Note: BuildReorderableSectionCard adds extra .ToolTipText and .Clipping on the inner border,
	// so we can't use WrapWithActivePanelHighlight directly here.
	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
		.BorderBackgroundColor_Lambda([this, SectionId]()
		{
			return IsActivePanelSection(SectionId)
				? ActivePanelHighlightColor
				: InactivePanelColor;
		})
		.Padding(1)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
			.Padding(4)
			.ToolTipText(SectionTooltip)
			.Clipping(EWidgetClipping::ClipToBounds)
			.OnMouseButtonDown_Lambda([this, SectionId](const FGeometry&, const FPointerEvent&)
			{
				SetActivePanelSection(SectionId);
				return FReply::Unhandled();
			})
			[
				CardContent
			]
		];
}

void SCharacterProfileAssetEditor::Construct(const FArguments& InArgs)
{
	Asset = InArgs._Asset;

	if (Asset.IsValid())
	{
		Asset->SetFlags(RF_Transactional);

		// Auto-populate required groups from project settings
		if (const UPaper2DPlusSettings* Settings = UPaper2DPlusSettings::Get())
		{
			bool bAddedAny = false;
			for (const FGameplayTag& RequiredTag : Settings->RequiredTagMappings)
			{
				if (RequiredTag.IsValid() && !Asset->TagMappings.Contains(RequiredTag))
				{
					if (!bAddedAny)
					{
						Asset->Modify();
						bAddedAny = true;
					}
					Asset->TagMappings.Add(RequiredTag, FFlipbookTagMapping());
				}
			}
		}
	}

	InitializeSectionLayouts();

	ChildSlot
	[
		SNew(SVerticalBox)

		// Tab bar at top
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			BuildTabBar()
		]

		// Tab content switcher
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SAssignNew(TabSwitcher, SWidgetSwitcher)
			.WidgetIndex_Lambda([this]() { return ActiveTabIndex; })

			// Tab 0: Overview
			+ SWidgetSwitcher::Slot()
			[
				BuildOverviewTab()
			]

			// Tab 1: Hitbox Editor
			+ SWidgetSwitcher::Slot()
			[
				BuildHitboxEditorTab()
			]

			// Tab 2: Alignment Editor
			+ SWidgetSwitcher::Slot()
			[
				BuildAlignmentEditorTab()
			]

			// Tab 3: Frame Timing Editor
			+ SWidgetSwitcher::Slot()
			[
				BuildFrameTimingTab()
			]
		]
	];

	// Auto-sync all frame arrays to flipbook frame counts on editor open
	if (Asset.IsValid())
	{
		Asset->SyncAllFramesToFlipbooks();
	}

	// Register for undo/redo notifications
	if (GEditor)
	{
		GEditor->RegisterForUndo(this);
	}

	// Listen for external modifications to the asset (e.g., sprite extractor adding flipbooks)
	OnObjectModifiedHandle = FCoreUObjectDelegates::OnObjectModified.AddSP(this, &SCharacterProfileAssetEditor::OnAssetExternallyModified);

	// Populate the overview tab on initial load
	RefreshOverviewFlipbookList(); // Also refreshes FlipbookGroupsPanel internally
	RefreshTagMappingsPanel();
	RefreshCurrentFrameFlipState();
	SetActivePanelSection(FName(TEXT("Overview.Flipbooks")));
}

TSharedRef<SWidget> SCharacterProfileAssetEditor::BuildTabBar()
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(FMargin(8, 4))
		[
			SNew(SHorizontalBox)

			// Overview tab button
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0, 0, 4, 0)
			[
				SNew(SCheckBox)
				.Style(FAppStyle::Get(), "ToggleButtonCheckbox")
				.IsChecked_Lambda([this]() { return ActiveTabIndex == 0 ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this](ECheckBoxState) { SwitchToTab(0); })
				.Padding(FMargin(8, 4))
				[
					SNew(STextBlock)
					.Text(LOCTEXT("OverviewTab", "Overview"))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
				]
			]

			// Hitbox Editor tab button
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0, 0, 4, 0)
			[
				SNew(SCheckBox)
				.Style(FAppStyle::Get(), "ToggleButtonCheckbox")
				.IsChecked_Lambda([this]() { return ActiveTabIndex == 1 ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this](ECheckBoxState) { SwitchToTab(1); })
				.Padding(FMargin(8, 4))
				[
					SNew(STextBlock)
					.Text(LOCTEXT("HitboxEditorTab", "Hitbox Editor"))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
				]
			]

			// Alignment Editor tab button
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0, 0, 4, 0)
			[
				SNew(SCheckBox)
				.Style(FAppStyle::Get(), "ToggleButtonCheckbox")
				.IsChecked_Lambda([this]() { return ActiveTabIndex == 2 ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this](ECheckBoxState) { SwitchToTab(2); })
				.Padding(FMargin(8, 4))
				[
					SNew(STextBlock)
					.Text(LOCTEXT("AlignmentEditorTab", "Sprite/Flipbook Tools"))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
				]
			]

			// Frame Timing tab button
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0, 0, 16, 0)
			[
				SNew(SCheckBox)
				.Style(FAppStyle::Get(), "ToggleButtonCheckbox")
				.IsChecked_Lambda([this]() { return ActiveTabIndex == 3 ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this](ECheckBoxState) { SwitchToTab(3); })
				.Padding(FMargin(8, 4))
				[
					SNew(STextBlock)
					.Text(LOCTEXT("FrameTimingTab", "Frame Timing"))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
				]
			]

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SNullWidget::NullWidget
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0, 0, 6, 0)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
				.ToolTipText(LOCTEXT("OpenShortcutsTooltip", "Open keyboard shortcut reference"))
				.Text(LOCTEXT("ShortcutsButton", "? Shortcuts"))
				.OnClicked_Lambda([this]()
				{
					ShowShortcutReferenceDialog();
					return FReply::Handled();
				})
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
				.ToolTipText(LOCTEXT("OpenHelpTooltip", "Open editor help and guidance"))
				.Text(LOCTEXT("HelpButton", "Help"))
				.OnClicked_Lambda([this]() {
					FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("CharacterProfileHelpDialog",
						"CharacterProfile Editor Help\n\n"
						"1) Overview: manage flipbooks, dimensions, validation, and extraction.\n"
						"2) Hitbox Editor: edit per-frame hitboxes/sockets and run frame batch operations.\n"
						"3) Sprite/Flipbook Tools: adjust offsets, apply flips, and refine alignment visuals.\n"
						"4) Frame Timing: adjust playback timing and sequence behavior.\n\n"
						"Tip: Right-click flipbooks for quick actions like editing, validation, and trimming."));
					return FReply::Handled();
				})
			]
		];
}

void SCharacterProfileAssetEditor::ShowShortcutReferenceDialog() const
{
	FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("CharacterProfileShortcutsDialog",
		"CharacterProfile Editor Shortcuts\n\n"
		"Global\n"
		"  Ctrl+Z: Undo\n"
		"  Ctrl+Y / Ctrl+Shift+Z: Redo\n"
		"  F2: Rename selected flipbook\n"
		"  Left/Right (or ,/.): Previous/Next frame\n"
		"  Up/Down: Previous/Next flipbook\n\n"
		"Hitbox Editor\n"
		"  E: Edit tool (draw + select/move/resize)\n"
		"  S: Socket tool\n"
		"  G: Toggle grid\n\n"
		"Sprite/Flipbook Tools\n"
		"  Space: Play/Pause\n"
		"  G: Toggle grid\n"
		"  O: Toggle onion skin\n"
		"  F: Toggle forward onion skin\n"
		"  P: Toggle ping-pong playback\n"
		"  R: Toggle/set reference sprite"));
}



TSharedRef<SWidget> SCharacterProfileAssetEditor::BuildFrameTimingTab()
{
	return SAssignNew(FrameTimingEditor, SFrameTimingEditor)
		.Asset(Asset.Get())
		.CollapsedFlipbookGroups(&CollapsedFlipbookGroups)
		.SelectedFrames(&SelectedFrames);
}

void SCharacterProfileAssetEditor::OnEditTimingClicked(int32 FlipbookIndex)
{
	SelectedFlipbookIndex = FlipbookIndex;
	SelectedFrameIndex = 0;
	if (FrameTimingEditor.IsValid())
	{
		FrameTimingEditor->SetSelectedFlipbook(FlipbookIndex);
	}
	SwitchToTab(3);
}

void SCharacterProfileAssetEditor::SetReferenceSprite(int32 FlipbookIndex, int32 FrameIndex)
{
	ReferenceFlipbookIndex = FlipbookIndex;
	ReferenceFrameIndex = FrameIndex;
	bShowReferenceSprite = true;
}

void SCharacterProfileAssetEditor::ClearReferenceSprite()
{
	bShowReferenceSprite = false;
	ReferenceFlipbookIndex = INDEX_NONE;
	ReferenceFrameIndex = INDEX_NONE;
}

void SCharacterProfileAssetEditor::NavigateToFlipbookAlignment(int32 FlipbookIndex)
{
	OnEditAlignmentClicked(FlipbookIndex);
}

void SCharacterProfileAssetEditor::SwitchToTab(int32 TabIndex)
{
	// Stop playback when switching away from alignment tab
	if (ActiveTabIndex == 2 && TabIndex != 2 && bIsPlaying)
	{
		StopPlayback();
	}

	// Stop playback when switching away from frame timing tab
	if (ActiveTabIndex == 3 && TabIndex != 3 && FrameTimingEditor.IsValid())
	{
		FrameTimingEditor->StopPlayback();
	}

	ClearFrameSelection();
	ActiveTabIndex = TabIndex;
	if (TabIndex == 0)
	{
		SetActivePanelSection(FName(TEXT("Overview.Flipbooks")));
	}
	else if (TabIndex == 1)
	{
		SetActivePanelSection(FName(TEXT("Hitbox.Canvas")));
	}
	else if (TabIndex == 2)
	{
		SetActivePanelSection(FName(TEXT("Alignment.Canvas")));
	}
	else
	{
		SetActivePanelSection(NAME_None);
	}

	if (TabSwitcher.IsValid())
	{
		TabSwitcher->SetActiveWidgetIndex(TabIndex);
	}

	// Refresh appropriate content
	if (TabIndex == 0)
	{
		RefreshOverviewFlipbookList();
	}
	else if (TabIndex == 1)
	{
		RefreshFlipbookList();
		RefreshFrameList();
		RefreshHitboxList();
	}
	else if (TabIndex == 2)
	{
		RefreshAlignmentFlipbookList();
		RefreshAlignmentFrameList();
	}
	else if (TabIndex == 3)
	{
		if (FrameTimingEditor.IsValid())
		{
			FrameTimingEditor->RefreshAll();
		}
	}
}

void SCharacterProfileAssetEditor::OnEditHitboxesClicked(int32 FlipbookIndex)
{
	SelectedFlipbookIndex = FlipbookIndex;
	SelectedFrameIndex = 0;
	SwitchToTab(1);
}

FReply SCharacterProfileAssetEditor::OnPreviewKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	FKey Key = InKeyEvent.GetKey();

	// In hitbox editor, defer arrow keys to canvas when a hitbox/socket is selected (for nudging)
	if (ActiveTabIndex == 1 && EditorCanvas.IsValid()
		&& EditorCanvas->GetSelectionType() != EHitboxSelectionType::None
		&& EditorCanvas->GetSelectedIndices().Num() > 0
		&& (Key == EKeys::Left || Key == EKeys::Right || Key == EKeys::Up || Key == EKeys::Down))
	{
		return FReply::Unhandled();
	}

	// Frame Timing Editor (tab 3) handles its own Left/Right frame navigation internally
	if (ActiveTabIndex == 3 && (Key == EKeys::Left || Key == EKeys::Right))
	{
		return FReply::Unhandled();
	}

	// Universal arrow key navigation (all tabs)
	// Left/Right = frame navigation, Up/Down = flipbook navigation

	if (Key == EKeys::Left || Key == EKeys::Comma)
	{
		if (SelectedFrameIndex > 0)
		{
			OnPrevFrameClicked();
		}
		else if (Asset.IsValid())
		{
			// At frame 0 — wrap to previous flipbook's last frame
			int32 PrevIdx = GetAdjacentFlipbookIndex(-1);
			if (PrevIdx != INDEX_NONE && PrevIdx != SelectedFlipbookIndex)
			{
				OnFlipbookSelected(PrevIdx);
				int32 FrameCount = GetCurrentFrameCount();
				if (FrameCount > 0)
				{
					SelectedFrameIndex = FrameCount - 1;
				}
			}
		}
		RefreshAfterNavigation();
		return FReply::Handled();
	}

	if (Key == EKeys::Right || Key == EKeys::Period)
	{
		int32 FrameCount = GetCurrentFrameCount();
		if (SelectedFrameIndex < FrameCount - 1)
		{
			OnNextFrameClicked();
		}
		else if (Asset.IsValid())
		{
			// At last frame — wrap to next flipbook's first frame
			int32 NextIdx = GetAdjacentFlipbookIndex(1);
			if (NextIdx != INDEX_NONE && NextIdx != SelectedFlipbookIndex)
			{
				OnFlipbookSelected(NextIdx);
			}
		}
		RefreshAfterNavigation();
		return FReply::Handled();
	}

	if (Key == EKeys::Up)
	{
		int32 PrevIdx = GetAdjacentFlipbookIndex(-1);
		if (PrevIdx != INDEX_NONE && PrevIdx != SelectedFlipbookIndex)
		{
			OnFlipbookSelected(PrevIdx);
			RefreshAfterNavigation();
		}
		return FReply::Handled();
	}

	if (Key == EKeys::Down)
	{
		int32 NextIdx = GetAdjacentFlipbookIndex(1);
		if (NextIdx != INDEX_NONE && NextIdx != SelectedFlipbookIndex)
		{
			OnFlipbookSelected(NextIdx);
			RefreshAfterNavigation();
		}
		return FReply::Handled();
	}

	return FReply::Unhandled();
}

FReply SCharacterProfileAssetEditor::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	// Global shortcuts (all tabs)
	if (GEditor && InKeyEvent.IsControlDown() && !InKeyEvent.IsShiftDown() && InKeyEvent.GetKey() == EKeys::Z)
	{
		GEditor->UndoTransaction();
		RefreshAll();
		return FReply::Handled();
	}

	if (GEditor && InKeyEvent.IsControlDown() && (InKeyEvent.GetKey() == EKeys::Y || (InKeyEvent.IsShiftDown() && InKeyEvent.GetKey() == EKeys::Z)))
	{
		GEditor->RedoTransaction();
		RefreshAll();
		return FReply::Handled();
	}

	// F2 - rename currently selected flipbook (when not editing text fields)
	if (!InKeyEvent.IsControlDown() && !InKeyEvent.IsShiftDown() && !InKeyEvent.IsAltDown()
		&& InKeyEvent.GetKey() == EKeys::F2)
	{
		TSharedPtr<SWidget> FocusedWidget = FSlateApplication::Get().GetKeyboardFocusedWidget();
		if (FocusedWidget.IsValid())
		{
			const FName WidgetType = FocusedWidget->GetType();
			if (WidgetType == TEXT("SEditableText") || WidgetType == TEXT("SMultiLineEditableText"))
			{
				return FReply::Unhandled();
			}
		}

		if (ActiveTabIndex <= 2 && Asset.IsValid() && Asset->Flipbooks.IsValidIndex(SelectedFlipbookIndex))
		{
			TriggerFlipbookRename(SelectedFlipbookIndex);
			return FReply::Handled();
		}
	}

	// Alignment Editor Tab shortcuts (ActiveTabIndex == 2)
	if (ActiveTabIndex == 2)
	{
		// Space - toggle playback (but not Ctrl+Space, which opens the content browser)
		if (InKeyEvent.GetKey() == EKeys::SpaceBar && !InKeyEvent.IsControlDown())
		{
			TogglePlayback();
			return FReply::Handled();
		}

		// G - toggle grid
		if (InKeyEvent.GetKey() == EKeys::G)
		{
			bShowAlignmentGrid = !bShowAlignmentGrid;
			return FReply::Handled();
		}

		// O - toggle onion skin
		if (InKeyEvent.GetKey() == EKeys::O)
		{
			bShowOnionSkin = !bShowOnionSkin;
			return FReply::Handled();
		}

		// F - toggle forward onion skin
		if (InKeyEvent.GetKey() == EKeys::F)
		{
			bShowForwardOnionSkin = !bShowForwardOnionSkin;
			return FReply::Handled();
		}

		// P - toggle ping-pong playback
		if (InKeyEvent.GetKey() == EKeys::P)
		{
			bPingPongPlayback = !bPingPongPlayback;
			if (!bPingPongPlayback) { bPlaybackReversed = false; }
			return FReply::Handled();
		}

		// R - toggle reference sprite
		if (InKeyEvent.GetKey() == EKeys::R)
		{
			if (ReferenceFlipbookIndex == INDEX_NONE)
			{
				SetReferenceSprite(SelectedFlipbookIndex, SelectedFrameIndex);
			}
			else
			{
				bShowReferenceSprite = !bShowReferenceSprite;
			}
			return FReply::Handled();
		}

		return FReply::Unhandled();
	}

	// Overview Tab shortcuts (ActiveTabIndex == 0)
	if (ActiveTabIndex == 0)
	{
		if (InKeyEvent.GetKey() == EKeys::Delete || InKeyEvent.GetKey() == EKeys::BackSpace)
		{
			if (Asset.IsValid() && Asset->Flipbooks.Num() > 1)
			{
				// Multi-select: delete all selected cards (highest index first to preserve indices)
				TArray<int32> ToDelete;
				if (SelectedFlipbookCards.Num() > 1)
				{
					ToDelete = SelectedFlipbookCards.Array();
				}
				else if (Asset->Flipbooks.IsValidIndex(SelectedFlipbookIndex))
				{
					ToDelete.Add(SelectedFlipbookIndex);
				}

				// Don't delete all flipbooks
				if (ToDelete.Num() >= Asset->Flipbooks.Num())
				{
					ToDelete.SetNum(Asset->Flipbooks.Num() - 1);
				}

				if (ToDelete.Num() > 0)
				{
					ToDelete.Sort([](int32 A, int32 B) { return A > B; }); // highest first

					BeginTransaction(LOCTEXT("DeleteFlipbooksTrans", "Delete Flipbooks"));
					for (int32 Idx : ToDelete)
					{
						if (Asset->Flipbooks.IsValidIndex(Idx))
						{
							Asset->RemoveFlipbookFromTagMappings(Asset->Flipbooks[Idx].FlipbookName);
							Asset->Flipbooks.RemoveAt(Idx);
						}
					}
					EndTransaction();

					SelectedFlipbookCards.Empty();
					SelectedFlipbookIndex = FMath::Clamp(SelectedFlipbookIndex, 0, Asset->Flipbooks.Num() - 1);
					RefreshAll();
				}
			}
			return FReply::Handled();
		}
	}

	// Hitbox Editor Tab shortcuts (ActiveTabIndex == 1)
	if (ActiveTabIndex == 1)
	{
		if (InKeyEvent.GetKey() == EKeys::E)
		{
			OnToolSelected(EHitboxEditorTool::Edit);
			return FReply::Handled();
		}
		if (InKeyEvent.GetKey() == EKeys::S && !InKeyEvent.IsControlDown())
		{
			OnToolSelected(EHitboxEditorTool::Socket);
			return FReply::Handled();
		}

		if (InKeyEvent.GetKey() == EKeys::G)
		{
			bShowGrid = !bShowGrid;
			return FReply::Handled();
		}
	}

	return FReply::Unhandled();
}

void SCharacterProfileAssetEditor::OnAssetExternallyModified(UObject* Object)
{
	if (Object && Object == Asset.Get())
	{
		// Defer refresh to avoid re-entrant issues during the current transaction
		RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateLambda(
			[this](double, float) {
				RefreshAll();
				return EActiveTimerReturnType::Stop;
			}));
	}
}

void SCharacterProfileAssetEditor::RefreshAfterNavigation()
{
	// Tab-aware refresh after arrow key frame/flipbook navigation
	switch (ActiveTabIndex)
	{
		case 1: // Hitbox Editor
			RefreshFrameList();
			RefreshHitboxList();
			RefreshPropertiesPanel();
			break;
		case 2: // Alignment Editor
			RefreshAlignmentFrameList();
			break;
		case 3: // Frame Timing Editor
			if (FrameTimingEditor.IsValid())
			{
				FrameTimingEditor->SetSelectedFlipbook(SelectedFlipbookIndex);
			}
			break;
		default:
			break;
	}
}

void SCharacterProfileAssetEditor::RefreshAll()
{
	RefreshFlipbookList();
	RefreshFrameList();
	RefreshHitboxList();
	RefreshPropertiesPanel();
	RefreshCurrentFrameFlipState();
	RefreshOverviewFlipbookList(); // Also refreshes FlipbookGroupsPanel internally
	RefreshAlignmentFlipbookList();
	RefreshAlignmentFrameList();
	RefreshTagMappingsPanel();

	// Purge queue entries referencing invalid flipbook indices (e.g., after undo)
	if (Asset.IsValid() && PlaybackQueue.Num() > 0)
	{
		bool bPurged = false;
		for (int32 i = PlaybackQueue.Num() - 1; i >= 0; i--)
		{
			if (!Asset->Flipbooks.IsValidIndex(PlaybackQueue[i]))
			{
				if (i < PlaybackQueueIndex) PlaybackQueueIndex--;
				else if (i == PlaybackQueueIndex) { PlaybackPosition = 0.0f; CachedPlaybackTiming = FFlipbookTimingData(); }
				PlaybackQueue.RemoveAt(i);
				bPurged = true;
			}
		}
		if (bPurged)
		{
			if (bIsPlaying) StopPlayback();
			PlaybackQueueIndex = FMath::Clamp(PlaybackQueueIndex, 0, FMath::Max(0, PlaybackQueue.Num() - 1));
			RefreshPlaybackQueueList();
		}
	}

	// Refresh 3D viewport (needed after undo/redo to update copied frame data)
	if (Viewport3D.IsValid())
	{
		const FFrameHitboxData* Frame = GetCurrentFrame();
		Viewport3D->SetFrameData(Frame);
		Viewport3D->SetSprite(GetCurrentSprite());
	}

}

void SCharacterProfileAssetEditor::PostUndo(bool bSuccess)
{
	if (bSuccess)
	{
		ClearFrameSelection();
		RefreshAll();
	}
}

void SCharacterProfileAssetEditor::PostRedo(bool bSuccess)
{
	if (bSuccess)
	{
		ClearFrameSelection();
		RefreshAll();
	}
}

void SCharacterProfileAssetEditor::OnFlipbookSelected(int32 Index)
{
	// Pause queue playback on manual selection
	if (bIsPlaying && PlaybackQueue.Num() > 0)
	{
		StopPlayback();
	}

	SelectedFlipbookIndex = Index;
	SelectedFrameIndex = 0;
	ClearFrameSelection();
	if (EditorCanvas.IsValid())
	{
		EditorCanvas->ClearSelection();
	}
	RefreshAll();
	RefreshCurrentFrameFlipState();

	// Safety: sprite/alignment bottom frame strip should always react immediately to flipbook changes.
	if (ActiveTabIndex == 2)
	{
		RefreshAlignmentFrameList();
	}
}

void SCharacterProfileAssetEditor::OnFrameSelected(int32 Index)
{
	SelectedFrameIndex = Index;
	if (EditorCanvas.IsValid())
	{
		EditorCanvas->ClearSelection();
	}
	RefreshFrameList();
	RefreshHitboxList();
	RefreshPropertiesPanel();
	RefreshCurrentFrameFlipState();

	// Update 3D viewport with new frame data
	if (Viewport3D.IsValid())
	{
		const FFrameHitboxData* Frame = GetCurrentFrame();
		Viewport3D->SetFrameData(Frame);
		Viewport3D->SetSprite(GetCurrentSprite());
		Viewport3D->SetSelectedHitbox(-1);
		Viewport3D->SetSelectedSocket(-1);
	}
}

void SCharacterProfileAssetEditor::OnToolSelected(EHitboxEditorTool Tool)
{
	CurrentTool = Tool;
}

bool SCharacterProfileAssetEditor::IsHitboxTypeVisible(EHitboxType Type) const
{
	return (HitboxVisibilityMask & (1 << static_cast<uint8>(Type))) != 0;
}

void SCharacterProfileAssetEditor::OnSelectionChanged(EHitboxSelectionType Type, int32 Index)
{
	RefreshHitboxList();
	RefreshPropertiesPanel();

	// Update 3D viewport selection
	if (Viewport3D.IsValid())
	{
		if (Type == EHitboxSelectionType::Hitbox)
		{
			Viewport3D->SetSelectedHitbox(Index);
			Viewport3D->SetSelectedSocket(-1);
		}
		else if (Type == EHitboxSelectionType::Socket)
		{
			Viewport3D->SetSelectedHitbox(-1);
			Viewport3D->SetSelectedSocket(Index);
		}
		else
		{
			Viewport3D->SetSelectedHitbox(-1);
			Viewport3D->SetSelectedSocket(-1);
		}
	}
}

void SCharacterProfileAssetEditor::OnHitboxDataModified()
{
	if (Asset.IsValid())
	{
		Asset->MarkPackageDirty();
	}
	RefreshHitboxList();

	// Refresh 3D viewport
	if (Viewport3D.IsValid())
	{
		const FFrameHitboxData* Frame = GetCurrentFrame();
		Viewport3D->SetFrameData(Frame);
		Viewport3D->SetSprite(GetCurrentSprite());
	}
	RefreshPropertiesPanel();
}

void SCharacterProfileAssetEditor::OnZoomChanged(float NewZoom)
{
	ZoomLevel = NewZoom;
}

void SCharacterProfileAssetEditor::OnPrevFrameClicked()
{
	if (SelectedFrameIndex > 0)
	{
		SelectedFrameIndex--;
		ClearFrameSelection();
		if (EditorCanvas.IsValid())
		{
			EditorCanvas->ClearSelection();
		}
		RefreshFrameList();
		RefreshHitboxList();
		RefreshPropertiesPanel();
	}
}

void SCharacterProfileAssetEditor::OnNextFrameClicked()
{
	int32 FrameCount = GetCurrentFrameCount();
	if (SelectedFrameIndex < FrameCount - 1)
	{
		SelectedFrameIndex++;
		ClearFrameSelection();
		if (EditorCanvas.IsValid())
		{
			EditorCanvas->ClearSelection();
		}
		RefreshFrameList();
		RefreshHitboxList();
		RefreshPropertiesPanel();
	}
}

void SCharacterProfileAssetEditor::BeginTransaction(const FText& Description)
{
	if (Asset.IsValid())
	{
		if (ActiveTransaction.IsValid())
		{
			ActiveTransaction.Reset();
		}

		ActiveTransaction = MakeUnique<FScopedTransaction>(Description);

		Asset->SetFlags(RF_Transactional);
		Asset->Modify();
		Asset->PreEditChange(nullptr);
	}
}

void SCharacterProfileAssetEditor::EndTransaction()
{
	if (Asset.IsValid())
	{
		FPropertyChangedEvent ChangedEvent(nullptr);
		Asset->PostEditChangeProperty(ChangedEvent);
		Asset->MarkPackageDirty();
	}

	if (ActiveTransaction.IsValid())
	{
		ActiveTransaction.Reset();
	}
}

void SCharacterProfileAssetEditor::AddNewFlipbook()
{
	if (!Asset.IsValid()) return;

	BeginTransaction(LOCTEXT("AddFlipbookTrans", "Add Flipbook"));

	FFlipbookHitboxData NewAnim;
	NewAnim.FlipbookName = FString::Printf(TEXT("Flipbook_%d"), Asset->Flipbooks.Num());

	FFrameHitboxData DefaultFrame;
	DefaultFrame.FrameName = TEXT("Frame_0");
	NewAnim.Frames.Add(DefaultFrame);

	int32 NewIndex = Asset->Flipbooks.Add(NewAnim);

	EndTransaction();

	SelectedFlipbookIndex = NewIndex;
	SelectedFrameIndex = 0;

	RefreshFlipbookList();
	RefreshFrameList();
	RefreshHitboxList();
	RefreshPropertiesPanel();
}

void SCharacterProfileAssetEditor::OpenFlipbookPicker(int32 FlipbookIndex)
{
	if (!Asset.IsValid() || !Asset->Flipbooks.IsValidIndex(FlipbookIndex)) return;

	FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");

	FAssetPickerConfig PickerConfig;
	PickerConfig.Filter.ClassPaths.Add(UPaperFlipbook::StaticClass()->GetClassPathName());
	PickerConfig.bAllowNullSelection = true;
	PickerConfig.InitialAssetViewType = EAssetViewType::Tile;
	PickerConfig.OnAssetSelected = FOnAssetSelected::CreateLambda([this, FlipbookIndex](const FAssetData& AssetData)
	{
		if (!Asset.IsValid()) return;
		if (!Asset->Flipbooks.IsValidIndex(FlipbookIndex)) return;

		FFlipbookHitboxData& FBData = Asset->Flipbooks[FlipbookIndex];
		const bool bShouldAutoRename =
			(FBData.FlipbookName.StartsWith(TEXT("Flipbook_")) || FBData.FlipbookName.TrimStartAndEnd().IsEmpty()) &&
			AssetData.IsValid();

		BeginTransaction(LOCTEXT("ChangeFlipbook", "Change Flipbook"));
		if (AssetData.IsValid())
		{
			FBData.Flipbook = TSoftObjectPtr<UPaperFlipbook>(AssetData.ToSoftObjectPath());
		}
		else
		{
			FBData.Flipbook.Reset();
		}
		Asset->SyncFramesToFlipbook(FlipbookIndex);
		EndTransaction();

		FSlateApplication::Get().DismissAllMenus();
		RefreshOverviewFlipbookList();
		RefreshFlipbookList();
		RefreshAlignmentFlipbookList();

		if (bShouldAutoRename)
		{
			TriggerFlipbookRename(FlipbookIndex);
		}
	});

	// Initial selection
	const FFlipbookHitboxData& FBData = Asset->Flipbooks[FlipbookIndex];
	if (!FBData.Flipbook.IsNull())
	{
		PickerConfig.InitialAssetSelection = FAssetData(FBData.Flipbook.LoadSynchronous());
	}

	FMenuBuilder MenuBuilder(true, nullptr);
	MenuBuilder.BeginSection("FlipbookPicker", LOCTEXT("SelectFlipbook", "Select Flipbook"));
	{
		TSharedRef<SWidget> PickerWidget = ContentBrowserModule.Get().CreateAssetPicker(PickerConfig);
		MenuBuilder.AddWidget(
			SNew(SBox)
			.WidthOverride(400)
			.HeightOverride(500)
			[
				PickerWidget
			],
			FText::GetEmpty(),
			true
		);
	}
	MenuBuilder.EndSection();

	FSlateApplication::Get().PushMenu(
		AsShared(),
		FWidgetPath(),
		MenuBuilder.MakeWidget(),
		FSlateApplication::Get().GetCursorPos(),
		FPopupTransitionEffect::ContextMenu
	);
}

void SCharacterProfileAssetEditor::RenameFlipbook(int32 FlipbookIndex, const FString& NewName)
{
	if (!Asset.IsValid()) return;
	if (!Asset->Flipbooks.IsValidIndex(FlipbookIndex)) return;

	FString TrimmedName = NewName.TrimStartAndEnd();
	if (TrimmedName.IsEmpty()) return;

	FFlipbookHitboxData& Anim = Asset->Flipbooks[FlipbookIndex];
	if (Anim.FlipbookName == TrimmedName) return;

	// Check for duplicate names
	for (int32 i = 0; i < Asset->Flipbooks.Num(); i++)
	{
		if (i != FlipbookIndex && Asset->Flipbooks[i].FlipbookName == TrimmedName)
		{
			return; // Name already in use
		}
	}

	BeginTransaction(LOCTEXT("RenameFlipbookTrans", "Rename Flipbook"));

	FString OldName = Anim.FlipbookName;
	Anim.FlipbookName = TrimmedName;
	Asset->UpdateTagMappingFlipbookName(OldName, TrimmedName);

	EndTransaction();

	RefreshOverviewFlipbookList();
	RefreshFlipbookList();
	RefreshAlignmentFlipbookList();
	RefreshTagMappingsPanel();
}

void SCharacterProfileAssetEditor::DuplicateFlipbook(int32 FlipbookIndex)
{
	if (!Asset.IsValid()) return;
	if (!Asset->Flipbooks.IsValidIndex(FlipbookIndex)) return;

	BeginTransaction(LOCTEXT("DuplicateFlipbookTrans", "Duplicate Flipbook"));

	FFlipbookHitboxData NewAnim = Asset->Flipbooks[FlipbookIndex]; // Deep copy
	NewAnim.FlipbookName = NewAnim.FlipbookName + TEXT(" (Copy)");

	// Ensure unique name
	FString BaseName = NewAnim.FlipbookName;
	int32 Counter = 2;
	while (true)
	{
		bool bNameExists = false;
		for (const FFlipbookHitboxData& Existing : Asset->Flipbooks)
		{
			if (Existing.FlipbookName == NewAnim.FlipbookName)
			{
				bNameExists = true;
				break;
			}
		}
		if (!bNameExists) break;
		NewAnim.FlipbookName = FString::Printf(TEXT("%s %d"), *BaseName, Counter++);
	}

	int32 InsertIndex = FlipbookIndex + 1;
	Asset->Flipbooks.Insert(NewAnim, InsertIndex);

	EndTransaction();

	// Keep queue entries bound to the same logical flipbooks after insertion.
	for (int32& QueueFlipbookIndex : PlaybackQueue)
	{
		if (QueueFlipbookIndex >= InsertIndex)
		{
			QueueFlipbookIndex++;
		}
	}

	SelectedFlipbookIndex = InsertIndex;
	SelectedFrameIndex = 0;

	RefreshOverviewFlipbookList();
	RefreshFlipbookList();
	RefreshAlignmentFlipbookList();
	RefreshPlaybackQueueList();
	RefreshFrameList();
	RefreshHitboxList();
	RefreshPropertiesPanel();
}

void SCharacterProfileAssetEditor::MoveFlipbookUp(int32 FlipbookIndex)
{
	if (!Asset.IsValid()) return;
	if (FlipbookIndex <= 0 || !Asset->Flipbooks.IsValidIndex(FlipbookIndex)) return;

	BeginTransaction(LOCTEXT("MoveFlipbookUpTrans", "Move Flipbook Up"));

	Asset->Flipbooks.Swap(FlipbookIndex, FlipbookIndex - 1);

	EndTransaction();

	// Remap queue indices so queued entries still point to the same flipbooks after swap.
	for (int32& QueueFlipbookIndex : PlaybackQueue)
	{
		if (QueueFlipbookIndex == FlipbookIndex)
		{
			QueueFlipbookIndex = FlipbookIndex - 1;
		}
		else if (QueueFlipbookIndex == (FlipbookIndex - 1))
		{
			QueueFlipbookIndex = FlipbookIndex;
		}
	}

	SelectedFlipbookIndex = FlipbookIndex - 1;

	RefreshOverviewFlipbookList();
	RefreshFlipbookList();
	RefreshAlignmentFlipbookList();
	RefreshPlaybackQueueList();
}

void SCharacterProfileAssetEditor::MoveFlipbookDown(int32 FlipbookIndex)
{
	if (!Asset.IsValid()) return;
	if (!Asset->Flipbooks.IsValidIndex(FlipbookIndex) || FlipbookIndex >= Asset->Flipbooks.Num() - 1) return;

	BeginTransaction(LOCTEXT("MoveFlipbookDownTrans", "Move Flipbook Down"));

	Asset->Flipbooks.Swap(FlipbookIndex, FlipbookIndex + 1);

	EndTransaction();

	// Remap queue indices so queued entries still point to the same flipbooks after swap.
	for (int32& QueueFlipbookIndex : PlaybackQueue)
	{
		if (QueueFlipbookIndex == FlipbookIndex)
		{
			QueueFlipbookIndex = FlipbookIndex + 1;
		}
		else if (QueueFlipbookIndex == (FlipbookIndex + 1))
		{
			QueueFlipbookIndex = FlipbookIndex;
		}
	}

	SelectedFlipbookIndex = FlipbookIndex + 1;

	RefreshOverviewFlipbookList();
	RefreshFlipbookList();
	RefreshAlignmentFlipbookList();
	RefreshPlaybackQueueList();
}

UPaperFlipbook* SCharacterProfileAssetEditor::GetFlipbookAssetForIndex(int32 FlipbookIndex) const
{
	if (!Asset.IsValid() || !Asset->Flipbooks.IsValidIndex(FlipbookIndex))
	{
		return nullptr;
	}

	const FFlipbookHitboxData& FlipbookData = Asset->Flipbooks[FlipbookIndex];
	if (FlipbookData.Flipbook.IsNull())
	{
		return nullptr;
	}

	return FlipbookData.Flipbook.LoadSynchronous();
}

void SCharacterProfileAssetEditor::OpenFlipbookAssetEditor(int32 FlipbookIndex)
{
	UPaperFlipbook* FlipbookAsset = GetFlipbookAssetForIndex(FlipbookIndex);
	if (!FlipbookAsset || !GEditor)
	{
		return;
	}

	if (UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
	{
		AssetEditorSubsystem->OpenEditorForAsset(FlipbookAsset);
	}
}

void SCharacterProfileAssetEditor::BrowseToFlipbookAssetInContentBrowser(int32 FlipbookIndex)
{
	UPaperFlipbook* FlipbookAsset = GetFlipbookAssetForIndex(FlipbookIndex);
	if (!FlipbookAsset)
	{
		return;
	}

	TArray<UObject*> AssetsToSync;
	AssetsToSync.Add(FlipbookAsset);

	FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
	ContentBrowserModule.Get().SyncBrowserToAssets(AssetsToSync);
}

void SCharacterProfileAssetEditor::OpenSpriteAssetEditor(UPaperSprite* Sprite)
{
	if (!Sprite || !GEditor)
	{
		return;
	}

	if (UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
	{
		AssetEditorSubsystem->OpenEditorForAsset(Sprite);
	}
}

void SCharacterProfileAssetEditor::BrowseToSpriteAssetInContentBrowser(UPaperSprite* Sprite)
{
	if (!Sprite)
	{
		return;
	}

	TArray<UObject*> AssetsToSync;
	AssetsToSync.Add(Sprite);

	FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
	ContentBrowserModule.Get().SyncBrowserToAssets(AssetsToSync);
}

void SCharacterProfileAssetEditor::ShowSpriteContextMenu(UPaperSprite* Sprite, const FVector2D& ScreenSpacePosition, int32 InReferenceFlipbookIndex, int32 InReferenceFrameIndex)
{
	TWeakObjectPtr<UPaperSprite> WeakSprite = Sprite;

	FMenuBuilder MenuBuilder(true, nullptr);

	if (InReferenceFlipbookIndex != INDEX_NONE && InReferenceFrameIndex != INDEX_NONE)
	{
		MenuBuilder.AddMenuEntry(
			FText::Format(LOCTEXT("SetFrameAsRef", "Set as Reference Sprite (Frame {0})"), FText::AsNumber(InReferenceFrameIndex)),
			LOCTEXT("SetFrameAsRefTooltip", "Set this frame as the alignment reference sprite"),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([this, InReferenceFlipbookIndex, InReferenceFrameIndex]()
				{
					if (Asset.IsValid() && Asset->Flipbooks.IsValidIndex(InReferenceFlipbookIndex))
					{
						SetReferenceSprite(InReferenceFlipbookIndex, InReferenceFrameIndex);
					}
				}),
				FCanExecuteAction::CreateLambda([this, InReferenceFlipbookIndex]()
				{
					return Asset.IsValid() && Asset->Flipbooks.IsValidIndex(InReferenceFlipbookIndex);
				})
			)
		);

		MenuBuilder.AddMenuSeparator();
	}

	MenuBuilder.AddMenuEntry(
		LOCTEXT("OpenSpriteAsset", "Open Sprite Asset"),
		LOCTEXT("OpenSpriteAssetTooltip", "Open this sprite asset in the Sprite Editor"),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([this, WeakSprite]()
			{
				OpenSpriteAssetEditor(WeakSprite.Get());
			}),
			FCanExecuteAction::CreateLambda([WeakSprite]()
			{
				return WeakSprite.IsValid();
			})
		)
	);

	MenuBuilder.AddMenuEntry(
		LOCTEXT("BrowseToSpriteAsset", "Browse to Sprite in Content Browser"),
		LOCTEXT("BrowseToSpriteAssetTooltip", "Sync the Content Browser to this sprite asset"),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([this, WeakSprite]()
			{
				BrowseToSpriteAssetInContentBrowser(WeakSprite.Get());
			}),
			FCanExecuteAction::CreateLambda([WeakSprite]()
			{
				return WeakSprite.IsValid();
			})
		)
	);

	FSlateApplication::Get().PushMenu(
		SharedThis(this),
		FWidgetPath(),
		MenuBuilder.MakeWidget(),
		ScreenSpacePosition,
		FPopupTransitionEffect::ContextMenu
	);
}

void SCharacterProfileAssetEditor::ShowFlipbookContextMenu(int32 FlipbookIndex)
{
	if (!Asset.IsValid()) return;
	if (!Asset->Flipbooks.IsValidIndex(FlipbookIndex)) return;

	FMenuBuilder MenuBuilder(true, nullptr);

	MenuBuilder.AddMenuEntry(
		LOCTEXT("RenameAnim", "Rename"),
		LOCTEXT("RenameFlipbookTooltip", "Rename this flipbook"),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([this, FlipbookIndex]()
		{
			TriggerFlipbookRename(FlipbookIndex);
		}))
	);

	MenuBuilder.AddMenuEntry(
		LOCTEXT("DuplicateAnim", "Duplicate"),
		LOCTEXT("DuplicateFlipbookTooltip", "Create a copy of this flipbook"),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([this, FlipbookIndex]()
		{
			DuplicateFlipbook(FlipbookIndex);
		}))
	);

	MenuBuilder.AddMenuSeparator();

	MenuBuilder.AddMenuEntry(
		LOCTEXT("OpenFlipbookAsset", "Open Flipbook Asset"),
		LOCTEXT("OpenFlipbookAssetTooltip", "Open this flipbook asset in its editor"),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([this, FlipbookIndex]()
			{
				OpenFlipbookAssetEditor(FlipbookIndex);
			}),
			FCanExecuteAction::CreateLambda([this, FlipbookIndex]()
			{
				return GetFlipbookAssetForIndex(FlipbookIndex) != nullptr;
			})
		)
	);

	MenuBuilder.AddMenuEntry(
		LOCTEXT("BrowseToFlipbookAsset", "Browse to Flipbook in Content Browser"),
		LOCTEXT("BrowseToFlipbookAssetTooltip", "Sync the Content Browser to this flipbook asset"),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([this, FlipbookIndex]()
			{
				BrowseToFlipbookAssetInContentBrowser(FlipbookIndex);
			}),
			FCanExecuteAction::CreateLambda([this, FlipbookIndex]()
			{
				return GetFlipbookAssetForIndex(FlipbookIndex) != nullptr;
			})
		)
	);

	MenuBuilder.AddMenuSeparator();

	MenuBuilder.AddMenuEntry(
		LOCTEXT("DeleteAnim", "Delete"),
		LOCTEXT("DeleteFlipbookTooltip", "Delete this flipbook"),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([this, FlipbookIndex]()
			{
				SelectedFlipbookIndex = FlipbookIndex;
				RemoveSelectedFlipbook();
				RefreshOverviewFlipbookList();
			}),
			FCanExecuteAction::CreateLambda([this]() { return Asset.IsValid() && Asset->Flipbooks.Num() > 1; })
		)
	);

	if (ActiveTabIndex == 2) // Alignment tab
	{
		MenuBuilder.AddMenuSeparator();

		MenuBuilder.AddMenuEntry(
			LOCTEXT("CTXAddToQueue", "Add to Queue"),
			LOCTEXT("CTXAddToQueueTooltip", "Add this flipbook to the playback queue"),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([this, FlipbookIndex]()
			{
				AddToPlaybackQueue(FlipbookIndex);
			}))
		);
	}

	MenuBuilder.AddMenuSeparator();

	MenuBuilder.AddMenuEntry(
		LOCTEXT("CTXEditHitboxes", "Edit Hitboxes"),
		LOCTEXT("CTXEditHitboxesTooltip", "Open this flipbook in the Hitbox Editor"),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([this, FlipbookIndex]()
		{
			SelectedFlipbookIndex = FlipbookIndex;
			SwitchToTab(1);
		}))
	);

	MenuBuilder.AddMenuEntry(
		LOCTEXT("CTXEditAlignment", "Edit Alignment"),
		LOCTEXT("CTXEditAlignmentTooltip", "Open this flipbook in the Sprite/Flipbook Tools"),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([this, FlipbookIndex]()
		{
			SelectedFlipbookIndex = FlipbookIndex;
			SwitchToTab(2);
		}))
	);

	MenuBuilder.AddMenuEntry(
		LOCTEXT("CTXEditTiming", "Edit Timing"),
		LOCTEXT("CTXEditTimingTooltip", "Open this flipbook in the Frame Timing editor"),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([this, FlipbookIndex]()
		{
			SelectedFlipbookIndex = FlipbookIndex;
			SwitchToTab(3);
		}))
	);

	MenuBuilder.AddMenuSeparator();

	MenuBuilder.AddMenuEntry(
		LOCTEXT("CTXSetAsRefSprite", "Set as Reference Sprite"),
		LOCTEXT("CTXSetAsRefSpriteTooltip", "Set frame 0 of this flipbook as the alignment reference sprite"),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([this, FlipbookIndex]()
		{
			SetReferenceSprite(FlipbookIndex, 0);
		}))
	);

	MenuBuilder.AddMenuSeparator();

	MenuBuilder.AddMenuEntry(
		LOCTEXT("CTXProperties", "Properties..."),
		LOCTEXT("CTXPropertiesTooltip", "View and edit flipbook metadata (source texture, extraction info, etc.)"),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([this, FlipbookIndex]()
		{
			ShowFlipbookPropertiesWindow(FlipbookIndex);
		}))
	);

	MenuBuilder.AddMenuSeparator();

	MenuBuilder.AddMenuEntry(
		LOCTEXT("CTXValidate", "Validate Asset"),
		LOCTEXT("CTXValidateTooltip", "Run CharacterProfile validation checks"),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([this]()
		{
			if (!Asset.IsValid()) return;
			TArray<FCharacterProfileValidationIssue> Issues;
			const bool bValid = Asset->ValidateCharacterProfileAsset(Issues);
			const FText Msg = bValid
				? LOCTEXT("CTXValidatePass", "Validation passed with no errors.")
				: FText::Format(LOCTEXT("CTXValidateFail", "Validation found {0} issues."), FText::AsNumber(Issues.Num()));
			FNotificationInfo Info(Msg);
			Info.ExpireDuration = 3.0f;
			FSlateNotificationManager::Get().AddNotification(Info);
		}))
	);

	MenuBuilder.AddMenuEntry(
		LOCTEXT("CTXTrim", "Trim Trailing Frames"),
		LOCTEXT("CTXTrimTooltip", "Trim trailing frame/extraction entries past flipbook keyframes"),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([this]()
		{
			if (!Asset.IsValid()) return;
			BeginTransaction(LOCTEXT("CTXTrimTxn", "Trim Trailing Frames"));
			const int32 Removed = Asset->TrimAllTrailingFrameData();
			EndTransaction();
			FNotificationInfo Info(FText::Format(LOCTEXT("CTXTrimResult", "Removed {0} trailing entries."), FText::AsNumber(Removed)));
			Info.ExpireDuration = 3.0f;
			FSlateNotificationManager::Get().AddNotification(Info);
			RefreshAll();
		}))
	);

	FSlateApplication::Get().PushMenu(
		AsShared(),
		FWidgetPath(),
		MenuBuilder.MakeWidget(),
		FSlateApplication::Get().GetCursorPos(),
		FPopupTransitionEffect::ContextMenu
	);
}

void SCharacterProfileAssetEditor::ShowFlipbookPropertiesWindow(int32 FlipbookIndex)
{
	if (!Asset.IsValid() || !Asset->Flipbooks.IsValidIndex(FlipbookIndex)) return;

	FFlipbookHitboxData& Anim = Asset->Flipbooks[FlipbookIndex];
	UPaperFlipbook* LoadedFlipbook = Anim.Flipbook.IsNull() ? nullptr : Anim.Flipbook.LoadSynchronous();

	// Compute stats
	const int32 FrameCount = Anim.Frames.Num();
	const int32 ExcludedCount = Anim.ExcludedFrames.Num();
	const int32 ExtractionCount = Anim.FrameExtractionInfo.Num();
	int32 TotalHitboxes = 0;
	int32 TotalSockets = 0;
	for (const FFrameHitboxData& Frame : Anim.Frames)
	{
		TotalHitboxes += Frame.Hitboxes.Num();
		TotalSockets += Frame.Sockets.Num();
	}

	// Run validation for this flipbook only
	TArray<FCharacterProfileValidationIssue> AllIssues;
	Asset->ValidateCharacterProfileAsset(AllIssues);
	TArray<FCharacterProfileValidationIssue> FlipbookIssues;
	const FString AnimLabel = FString::Printf(TEXT("Flipbook[%d]"), FlipbookIndex);
	for (const FCharacterProfileValidationIssue& Issue : AllIssues)
	{
		if (Issue.Context.Contains(AnimLabel))
		{
			FlipbookIssues.Add(Issue);
		}
	}

	TSharedRef<SWindow> PropertiesWindow = SNew(SWindow)
		.Title(FText::Format(LOCTEXT("FlipbookPropertiesTitle", "Properties: {0}"), FText::FromString(Anim.FlipbookName)))
		.ClientSize(FVector2D(420, 0))
		.SizingRule(ESizingRule::Autosized)
		.SupportsMaximize(false)
		.SupportsMinimize(false);

	// We need a WeakPtr to asset for lambdas
	TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset> WeakAsset = Asset;

	TSharedRef<SVerticalBox> Content = SNew(SVerticalBox);

	// === Info Section ===
	Content->AddSlot().AutoHeight().Padding(8, 8, 8, 4)
	[
		SNew(STextBlock)
		.Text(LOCTEXT("PropsInfoHeader", "INFO"))
		.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
	];

	auto AddInfoRow = [&Content](const FText& Label, const FText& Value)
	{
		Content->AddSlot().AutoHeight().Padding(16, 2, 8, 2)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
			[
				SNew(SBox).WidthOverride(120)
				[
					SNew(STextBlock).Text(Label)
					.Font(FAppStyle::GetFontStyle("SmallFont"))
					.ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f)))
				]
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f)
			[
				SNew(STextBlock).Text(Value)
				.Font(FAppStyle::GetFontStyle("SmallFont"))
			]
		];
	};

	AddInfoRow(LOCTEXT("PropsName", "Name"), FText::FromString(Anim.FlipbookName));
	AddInfoRow(LOCTEXT("PropsGroup", "Group"), Anim.FlipbookGroup.IsNone() ? LOCTEXT("Ungrouped", "(Ungrouped)") : FText::FromName(Anim.FlipbookGroup));
	AddInfoRow(LOCTEXT("PropsFrames", "Frames"), FText::AsNumber(FrameCount));
	if (ExcludedCount > 0)
	{
		AddInfoRow(LOCTEXT("PropsExcluded", "Excluded Frames"), FText::AsNumber(ExcludedCount));
	}
	AddInfoRow(LOCTEXT("PropsHitboxes", "Total Hitboxes"), FText::AsNumber(TotalHitboxes));
	AddInfoRow(LOCTEXT("PropsSockets", "Total Sockets"), FText::AsNumber(TotalSockets));
	AddInfoRow(LOCTEXT("PropsFlipbook", "Flipbook Asset"), LoadedFlipbook
		? FText::FromString(Anim.Flipbook.GetAssetName())
		: (Anim.Flipbook.IsNull() ? LOCTEXT("None", "(None)") : LOCTEXT("Unloaded", "(Not Loaded)")));
	AddInfoRow(LOCTEXT("PropsOutputPath", "Sprites Output"), Anim.SpritesOutputPath.IsEmpty()
		? LOCTEXT("NoOutputPath", "(Not Set)")
		: FText::FromString(Anim.SpritesOutputPath));

	// === Source Texture Section (editable) ===
	Content->AddSlot().AutoHeight().Padding(8, 12, 8, 4)
	[
		SNew(STextBlock)
		.Text(LOCTEXT("PropsSourceHeader", "SOURCE TEXTURE"))
		.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
	];

	Content->AddSlot().AutoHeight().Padding(16, 2, 8, 2)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0).VAlign(VAlign_Center)
		[
			SNew(SBox).WidthOverride(120)
			[
				SNew(STextBlock).Text(LOCTEXT("PropsSourceTex", "Source Texture"))
				.Font(FAppStyle::GetFontStyle("SmallFont"))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f)))
			]
		]
		+ SHorizontalBox::Slot().FillWidth(1.0f)
		[
			SNew(SObjectPropertyEntryBox)
			.AllowedClass(UTexture2D::StaticClass())
			.ObjectPath_Lambda([WeakAsset, FlipbookIndex]() -> FString
			{
				if (WeakAsset.IsValid() && WeakAsset->Flipbooks.IsValidIndex(FlipbookIndex))
				{
					return WeakAsset->Flipbooks[FlipbookIndex].SourceTexture.ToSoftObjectPath().ToString();
				}
				return FString();
			})
			.OnObjectChanged_Lambda([WeakAsset, FlipbookIndex, this](const FAssetData& NewAsset)
			{
				if (!WeakAsset.IsValid() || !WeakAsset->Flipbooks.IsValidIndex(FlipbookIndex)) return;
				BeginTransaction(LOCTEXT("SetSourceTexture", "Set Source Texture"));
				Asset->Modify();
				WeakAsset->Flipbooks[FlipbookIndex].SourceTexture = TSoftObjectPtr<UTexture2D>(NewAsset.GetSoftObjectPath());
				EndTransaction();
			})
		]
	];

	// === Sprites Output Path (editable) ===
	Content->AddSlot().AutoHeight().Padding(16, 6, 8, 2)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0).VAlign(VAlign_Center)
		[
			SNew(SBox).WidthOverride(120)
			[
				SNew(STextBlock).Text(LOCTEXT("PropsOutputPathLabel", "Output Path"))
				.Font(FAppStyle::GetFontStyle("SmallFont"))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f)))
			]
		]
		+ SHorizontalBox::Slot().FillWidth(1.0f)
		[
			SNew(SEditableTextBox)
			.Text_Lambda([WeakAsset, FlipbookIndex]() -> FText
			{
				if (WeakAsset.IsValid() && WeakAsset->Flipbooks.IsValidIndex(FlipbookIndex))
				{
					return FText::FromString(WeakAsset->Flipbooks[FlipbookIndex].SpritesOutputPath);
				}
				return FText::GetEmpty();
			})
			.OnTextCommitted_Lambda([WeakAsset, FlipbookIndex, this](const FText& NewText, ETextCommit::Type CommitType)
			{
				if (CommitType == ETextCommit::OnCleared) return;
				if (!WeakAsset.IsValid() || !WeakAsset->Flipbooks.IsValidIndex(FlipbookIndex)) return;
				BeginTransaction(LOCTEXT("SetOutputPath", "Set Sprites Output Path"));
				Asset->Modify();
				WeakAsset->Flipbooks[FlipbookIndex].SpritesOutputPath = NewText.ToString();
				EndTransaction();
			})
			.Font(FAppStyle::GetFontStyle("SmallFont"))
		]
	];

	// === Validation Section ===
	if (FlipbookIssues.Num() > 0)
	{
		Content->AddSlot().AutoHeight().Padding(8, 12, 8, 4)
		[
			SNew(STextBlock)
			.Text(FText::Format(LOCTEXT("PropsValidationHeader", "VALIDATION ({0} issues)"), FText::AsNumber(FlipbookIssues.Num())))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
			.ColorAndOpacity(FLinearColor(1.0f, 0.7f, 0.3f))
		];

		for (const FCharacterProfileValidationIssue& Issue : FlipbookIssues)
		{
			FLinearColor IssueColor = Issue.Severity == ECharacterProfileValidationSeverity::Error
				? FLinearColor(1.0f, 0.4f, 0.4f)
				: FLinearColor(1.0f, 0.8f, 0.3f);

			Content->AddSlot().AutoHeight().Padding(16, 2, 8, 2)
			[
				SNew(STextBlock)
				.Text(FText::FromString(Issue.Message))
				.Font(FAppStyle::GetFontStyle("SmallFont"))
				.ColorAndOpacity(IssueColor)
				.AutoWrapText(true)
			];
		}
	}
	else
	{
		Content->AddSlot().AutoHeight().Padding(8, 12, 8, 4)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("PropsValidationOK", "VALIDATION: No issues"))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
			.ColorAndOpacity(FLinearColor(0.3f, 0.8f, 0.3f))
		];
	}

	// Bottom padding
	Content->AddSlot().AutoHeight().Padding(0, 8) [ SNew(SSpacer) ];

	PropertiesWindow->SetContent(
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(0)
		[
			Content
		]
	);

	FSlateApplication::Get().AddModalWindow(PropertiesWindow, SharedThis(this));
}

void SCharacterProfileAssetEditor::TriggerFlipbookRename(int32 FlipbookIndex)
{
	if (!Asset.IsValid() || !Asset->Flipbooks.IsValidIndex(FlipbookIndex))
	{
		return;
	}

	PendingRenameFlipbookIndex = FlipbookIndex;

	if (ActiveTabIndex == 0)
	{
		RefreshFlipbookGroupsPanel();
	}
	else if (ActiveTabIndex == 1)
	{
		RefreshFlipbookList();
	}
	else if (ActiveTabIndex == 2)
	{
		RefreshAlignmentFlipbookList();
	}
}

void SCharacterProfileAssetEditor::RemoveSelectedFlipbook()
{
	if (!Asset.IsValid()) return;
	if (!Asset->Flipbooks.IsValidIndex(SelectedFlipbookIndex)) return;

	if (Asset->Flipbooks.Num() <= 1) return;

	const int32 RemovedIndex = SelectedFlipbookIndex;
	const FString RemovedName = Asset->Flipbooks[SelectedFlipbookIndex].FlipbookName;

	BeginTransaction(LOCTEXT("RemoveFlipbookTrans", "Remove Flipbook"));

	// Clean up group bindings referencing this flipbook (inside same transaction for undo atomicity)
	Asset->RemoveFlipbookFromTagMappings(RemovedName);
	Asset->Flipbooks.RemoveAt(RemovedIndex);

	bool bQueueChanged = false;
	bool bRemovedActiveQueueEntry = false;
	for (int32 i = PlaybackQueue.Num() - 1; i >= 0; --i)
	{
		if (PlaybackQueue[i] == RemovedIndex)
		{
			if (i < PlaybackQueueIndex)
			{
				PlaybackQueueIndex--;
			}
			else if (i == PlaybackQueueIndex)
			{
				bRemovedActiveQueueEntry = true;
			}
			PlaybackQueue.RemoveAt(i);
			bQueueChanged = true;
		}
		else if (PlaybackQueue[i] > RemovedIndex)
		{
			PlaybackQueue[i]--;
			bQueueChanged = true;
		}
	}

	EndTransaction();

	if (bQueueChanged)
	{
		if (bRemovedActiveQueueEntry)
		{
			PlaybackPosition = 0.0f;
			CachedPlaybackTiming = FFlipbookTimingData();
			if (bIsPlaying)
			{
				StopPlayback();
			}
		}
		PlaybackQueueIndex = FMath::Clamp(PlaybackQueueIndex, 0, FMath::Max(0, PlaybackQueue.Num() - 1));
	}

	if (SelectedFlipbookIndex >= Asset->Flipbooks.Num())
	{
		SelectedFlipbookIndex = Asset->Flipbooks.Num() - 1;
	}
	SelectedFrameIndex = 0;

	if (EditorCanvas.IsValid())
	{
		EditorCanvas->ClearSelection();
	}

	RefreshAll();
}

void SCharacterProfileAssetEditor::AddNewFrame()
{
	FFlipbookHitboxData* Anim = GetCurrentFlipbookDataMutable();
	if (!Anim) return;

	BeginTransaction(LOCTEXT("AddFrame", "Add Frame"));

	FFrameHitboxData NewFrame;
	NewFrame.FrameName = FString::Printf(TEXT("Frame_%d"), Anim->Frames.Num());

	int32 NewIndex = Anim->Frames.Add(NewFrame);

	EndTransaction();

	SelectedFrameIndex = NewIndex;

	if (EditorCanvas.IsValid())
	{
		EditorCanvas->ClearSelection();
	}

	RefreshFrameList();
	RefreshHitboxList();
	RefreshPropertiesPanel();
}

void SCharacterProfileAssetEditor::RemoveSelectedFrame()
{
	FFlipbookHitboxData* Anim = GetCurrentFlipbookDataMutable();
	if (!Anim) return;
	if (!Anim->Frames.IsValidIndex(SelectedFrameIndex)) return;

	if (Anim->Frames.Num() <= 1) return;

	BeginTransaction(LOCTEXT("RemoveFrame", "Remove Frame"));

	Anim->Frames.RemoveAt(SelectedFrameIndex);

	EndTransaction();

	if (SelectedFrameIndex >= Anim->Frames.Num())
	{
		SelectedFrameIndex = Anim->Frames.Num() - 1;
	}

	if (EditorCanvas.IsValid())
	{
		EditorCanvas->ClearSelection();
	}

	RefreshFrameList();
	RefreshHitboxList();
	RefreshPropertiesPanel();
}

const FFrameHitboxData* SCharacterProfileAssetEditor::GetCurrentFrame() const
{
	const FFlipbookHitboxData* Anim = GetCurrentFlipbookData();
	if (!Anim) return nullptr;
	if (!Anim->Frames.IsValidIndex(SelectedFrameIndex)) return nullptr;
	return &Anim->Frames[SelectedFrameIndex];
}

FFrameHitboxData* SCharacterProfileAssetEditor::GetCurrentFrameMutable()
{
	FFlipbookHitboxData* Anim = GetCurrentFlipbookDataMutable();
	if (!Anim) return nullptr;
	if (!Anim->Frames.IsValidIndex(SelectedFrameIndex)) return nullptr;
	return &Anim->Frames[SelectedFrameIndex];
}

const FFlipbookHitboxData* SCharacterProfileAssetEditor::GetCurrentFlipbookData() const
{
	if (!Asset.IsValid()) return nullptr;
	if (!Asset->Flipbooks.IsValidIndex(SelectedFlipbookIndex)) return nullptr;
	return &Asset->Flipbooks[SelectedFlipbookIndex];
}

FFlipbookHitboxData* SCharacterProfileAssetEditor::GetCurrentFlipbookDataMutable()
{
	if (!Asset.IsValid()) return nullptr;
	if (!Asset->Flipbooks.IsValidIndex(SelectedFlipbookIndex)) return nullptr;
	return &Asset->Flipbooks[SelectedFlipbookIndex];
}

TArray<int32> SCharacterProfileAssetEditor::GetSortedFlipbookIndices() const
{
	TArray<int32> Indices;
	if (!Asset.IsValid()) return Indices;

	Indices.SetNum(Asset->Flipbooks.Num());
	for (int32 i = 0; i < Indices.Num(); i++) { Indices[i] = i; }
	Indices.Sort([this](int32 A, int32 B)
	{
		return Asset->Flipbooks[A].FlipbookName.Compare(Asset->Flipbooks[B].FlipbookName, ESearchCase::IgnoreCase) < 0;
	});
	return Indices;
}

void SCharacterProfileAssetEditor::BuildGroupedFlipbookList(TSharedPtr<SVerticalBox> ListBox, TFunction<TSharedRef<SWidget>(int32)> ItemBuilder, TFunction<bool(int32)> Filter)
{
	if (!ListBox.IsValid() || !Asset.IsValid()) return;

	// Partition sorted indices by group, applying filter
	TArray<int32> SortedIndices = GetSortedFlipbookIndices();
	TMap<FName, TArray<int32>> FlipbooksByGroup;
	for (int32 i : SortedIndices)
	{
		if (Filter && !Filter(i)) continue;
		FlipbooksByGroup.FindOrAdd(Asset->Flipbooks[i].FlipbookGroup).Add(i);
	}

	// Collect group names in display order: ungrouped first, then named groups alphabetically
	TArray<FName> GroupOrder;
	if (FlipbooksByGroup.Contains(NAME_None))
	{
		GroupOrder.Add(NAME_None);
	}
	TArray<FName> NamedGroups;
	for (const auto& Pair : FlipbooksByGroup)
	{
		if (Pair.Key != NAME_None)
		{
			NamedGroups.Add(Pair.Key);
		}
	}
	NamedGroups.Sort([](const FName& A, const FName& B) { return A.Compare(B) < 0; });
	GroupOrder.Append(NamedGroups);

	// If no groups exist (all ungrouped), render flat list
	if (GroupOrder.Num() <= 1 && GroupOrder.Contains(NAME_None))
	{
		const TArray<int32>& Indices = FlipbooksByGroup[NAME_None];
		for (int32 Idx : Indices)
		{
			ListBox->AddSlot().AutoHeight()[ItemBuilder(Idx)];
		}
		return;
	}

	// Render grouped list with collapsible headers
	for (FName GroupName : GroupOrder)
	{
		const TArray<int32>& GroupIndices = FlipbooksByGroup[GroupName];
		bool bCollapsed = CollapsedFlipbookGroups.Contains(GroupName);
		FString DisplayName = GroupName.IsNone() ? TEXT("Ungrouped") : GroupName.ToString();

		// Group header
		ListBox->AddSlot()
		.AutoHeight()
		.Padding(0, 4, 0, 0)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "NoBorder")
			.OnClicked_Lambda([this, GroupName]()
			{
				if (CollapsedFlipbookGroups.Contains(GroupName))
				{
					CollapsedFlipbookGroups.Remove(GroupName);
				}
				else
				{
					CollapsedFlipbookGroups.Add(GroupName);
				}
				RefreshAll();
				return FReply::Handled();
			})
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0, 0, 4, 0)
				[
					SNew(STextBlock)
					.Text(FText::FromString(bCollapsed ? TEXT("\x25B6") : TEXT("\x25BC")))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 7))
					.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)))
				]

				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(FText::Format(LOCTEXT("GroupHeaderFmt", "{0} ({1})"),
						FText::FromString(DisplayName), FText::AsNumber(GroupIndices.Num())))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
					.ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f)))
				]
			]
		];

		// Group items (if not collapsed)
		if (!bCollapsed)
		{
			for (int32 Idx : GroupIndices)
			{
				ListBox->AddSlot()
				.AutoHeight()
				.Padding(8, 0, 0, 0)
				[
					ItemBuilder(Idx)
				];
			}
		}
	}
}

int32 SCharacterProfileAssetEditor::GetCurrentFrameCount() const
{
	const FFlipbookHitboxData* Anim = GetCurrentFlipbookData();
	if (!Anim) return 0;

	// Prefer flipbook frame count as the authoritative source
	if (!Anim->Flipbook.IsNull())
	{
		if (UPaperFlipbook* FB = Anim->Flipbook.LoadSynchronous())
		{
			int32 FlipbookFrames = FB->GetNumKeyFrames();
			if (FlipbookFrames > 0)
			{
				return FlipbookFrames;
			}
		}
	}

	// Fallback to hitbox frames array
	return Anim->Frames.Num();
}

UPaperSprite* SCharacterProfileAssetEditor::GetCurrentSprite() const
{
	const FFlipbookHitboxData* Anim = GetCurrentFlipbookData();
	if (!Anim || Anim->Flipbook.IsNull()) return nullptr;

	UPaperFlipbook* FB = Anim->Flipbook.LoadSynchronous();
	if (!FB || SelectedFrameIndex < 0 || SelectedFrameIndex >= FB->GetNumKeyFrames()) return nullptr;

	return FB->GetKeyFrameChecked(SelectedFrameIndex).Sprite;
}

#undef LOCTEXT_NAMESPACE
