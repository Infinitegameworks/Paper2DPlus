// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "CharacterDataAssetEditor.h"
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

#define LOCTEXT_NAMESPACE "CharacterDataAssetEditor"

// SCharacterDataEditorCanvas implementation -> SCharacterDataEditorCanvas.cpp
// FHitbox3DViewportClient + SHitbox3DViewport implementations -> SHitbox3DViewport.cpp
// FCharacterDataAssetEditorToolkit implementation -> CharacterDataAssetEditorToolkit.cpp
// SSpriteAlignmentCanvas implementation -> SSpriteAlignmentCanvas.cpp

// ==========================================
// SCharacterDataAssetEditor Implementation
// ==========================================

SCharacterDataAssetEditor::~SCharacterDataAssetEditor()
{
	StopPlayback();
	if (GEditor)
	{
		GEditor->UnregisterForUndo(this);
	}
	FCoreUObjectDelegates::OnObjectModified.Remove(OnObjectModifiedHandle);
}

void SCharacterDataAssetEditor::Construct(const FArguments& InArgs)
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
	OnObjectModifiedHandle = FCoreUObjectDelegates::OnObjectModified.AddSP(this, &SCharacterDataAssetEditor::OnAssetExternallyModified);

	// Populate the overview tab on initial load
	RefreshOverviewFlipbookList();
	RefreshFlipbookGroupsPanel();
	RefreshTagMappingsPanel();
	RefreshCurrentFrameFlipState();
}

TSharedRef<SWidget> SCharacterDataAssetEditor::BuildTabBar()
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
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
				.ToolTipText(LOCTEXT("OpenHelpTooltip", "Open editor help and guidance"))
				.Text(LOCTEXT("HelpButton", "Help"))
				.OnClicked_Lambda([this]() {
					FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("CharacterDataHelpDialog",
						"CharacterData Editor Help\n\n"
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



TSharedRef<SWidget> SCharacterDataAssetEditor::BuildFrameTimingTab()
{
	return SAssignNew(FrameTimingEditor, SFrameTimingEditor)
		.Asset(Asset.Get())
		.CollapsedFlipbookGroups(&CollapsedFlipbookGroups);
}

void SCharacterDataAssetEditor::OnEditTimingClicked(int32 FlipbookIndex)
{
	SelectedFlipbookIndex = FlipbookIndex;
	SelectedFrameIndex = 0;
	if (FrameTimingEditor.IsValid())
	{
		FrameTimingEditor->SetSelectedFlipbook(FlipbookIndex);
	}
	SwitchToTab(3);
}

void SCharacterDataAssetEditor::SetReferenceSprite(int32 FlipbookIndex, int32 FrameIndex)
{
	ReferenceFlipbookIndex = FlipbookIndex;
	ReferenceFrameIndex = FrameIndex;
	bShowReferenceSprite = true;
}

void SCharacterDataAssetEditor::ClearReferenceSprite()
{
	bShowReferenceSprite = false;
	ReferenceFlipbookIndex = INDEX_NONE;
	ReferenceFrameIndex = INDEX_NONE;
}

void SCharacterDataAssetEditor::SwitchToTab(int32 TabIndex)
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

	ActiveTabIndex = TabIndex;
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

void SCharacterDataAssetEditor::OnEditHitboxesClicked(int32 FlipbookIndex)
{
	SelectedFlipbookIndex = FlipbookIndex;
	SelectedFrameIndex = 0;
	SwitchToTab(1);
}

FReply SCharacterDataAssetEditor::OnPreviewKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
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

FReply SCharacterDataAssetEditor::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	// Global shortcuts (all tabs)
	if (InKeyEvent.IsControlDown() && !InKeyEvent.IsShiftDown() && InKeyEvent.GetKey() == EKeys::Z)
	{
		GEditor->UndoTransaction();
		RefreshAll();
		return FReply::Handled();
	}

	if (InKeyEvent.IsControlDown() && (InKeyEvent.GetKey() == EKeys::Y || (InKeyEvent.IsShiftDown() && InKeyEvent.GetKey() == EKeys::Z)))
	{
		GEditor->RedoTransaction();
		RefreshAll();
		return FReply::Handled();
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

	// Hitbox Editor Tab shortcuts (ActiveTabIndex == 1)
	if (ActiveTabIndex == 1)
	{
		if (InKeyEvent.GetKey() == EKeys::D)
		{
			OnToolSelected(EHitboxEditorTool::Draw);
			return FReply::Handled();
		}
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

void SCharacterDataAssetEditor::OnAssetExternallyModified(UObject* Object)
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

void SCharacterDataAssetEditor::RefreshAfterNavigation()
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

void SCharacterDataAssetEditor::RefreshAll()
{
	RefreshFlipbookList();
	RefreshFrameList();
	RefreshHitboxList();
	RefreshPropertiesPanel();
	RefreshCurrentFrameFlipState();
	RefreshOverviewFlipbookList();
	RefreshFlipbookGroupsPanel();
	RefreshAlignmentFlipbookList();
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

void SCharacterDataAssetEditor::PostUndo(bool bSuccess)
{
	if (bSuccess)
	{
		RefreshAll();
	}
}

void SCharacterDataAssetEditor::PostRedo(bool bSuccess)
{
	if (bSuccess)
	{
		RefreshAll();
	}
}

void SCharacterDataAssetEditor::OnFlipbookSelected(int32 Index)
{
	// Pause queue playback on manual selection
	if (bIsPlaying && PlaybackQueue.Num() > 0)
	{
		StopPlayback();
	}

	SelectedFlipbookIndex = Index;
	SelectedFrameIndex = 0;
	if (EditorCanvas.IsValid())
	{
		EditorCanvas->ClearSelection();
	}
	RefreshAll();
	RefreshCurrentFrameFlipState();
}

void SCharacterDataAssetEditor::OnFrameSelected(int32 Index)
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

void SCharacterDataAssetEditor::OnToolSelected(EHitboxEditorTool Tool)
{
	CurrentTool = Tool;
}

bool SCharacterDataAssetEditor::IsHitboxTypeVisible(EHitboxType Type) const
{
	return (HitboxVisibilityMask & (1 << static_cast<uint8>(Type))) != 0;
}

void SCharacterDataAssetEditor::OnSelectionChanged(EHitboxSelectionType Type, int32 Index)
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

void SCharacterDataAssetEditor::OnHitboxDataModified()
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

void SCharacterDataAssetEditor::OnZoomChanged(float NewZoom)
{
	ZoomLevel = NewZoom;
}

void SCharacterDataAssetEditor::OnPrevFrameClicked()
{
	if (SelectedFrameIndex > 0)
	{
		SelectedFrameIndex--;
		if (EditorCanvas.IsValid())
		{
			EditorCanvas->ClearSelection();
		}
		RefreshFrameList();
		RefreshHitboxList();
		RefreshPropertiesPanel();
	}
}

void SCharacterDataAssetEditor::OnNextFrameClicked()
{
	int32 FrameCount = GetCurrentFrameCount();
	if (SelectedFrameIndex < FrameCount - 1)
	{
		SelectedFrameIndex++;
		if (EditorCanvas.IsValid())
		{
			EditorCanvas->ClearSelection();
		}
		RefreshFrameList();
		RefreshHitboxList();
		RefreshPropertiesPanel();
	}
}

void SCharacterDataAssetEditor::BeginTransaction(const FText& Description)
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

void SCharacterDataAssetEditor::EndTransaction()
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

void SCharacterDataAssetEditor::AddNewFlipbook()
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

void SCharacterDataAssetEditor::OpenFlipbookPicker(int32 FlipbookIndex)
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

		BeginTransaction(LOCTEXT("ChangeFlipbook", "Change Flipbook"));
		FFlipbookHitboxData& FBData = Asset->Flipbooks[FlipbookIndex];
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

void SCharacterDataAssetEditor::RenameFlipbook(int32 FlipbookIndex, const FString& NewName)
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

void SCharacterDataAssetEditor::DuplicateFlipbook(int32 FlipbookIndex)
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

	SelectedFlipbookIndex = InsertIndex;
	SelectedFrameIndex = 0;

	RefreshOverviewFlipbookList();
	RefreshFlipbookList();
	RefreshAlignmentFlipbookList();
	RefreshFrameList();
	RefreshHitboxList();
	RefreshPropertiesPanel();
}

void SCharacterDataAssetEditor::MoveFlipbookUp(int32 FlipbookIndex)
{
	if (!Asset.IsValid()) return;
	if (FlipbookIndex <= 0 || !Asset->Flipbooks.IsValidIndex(FlipbookIndex)) return;

	BeginTransaction(LOCTEXT("MoveFlipbookUpTrans", "Move Flipbook Up"));

	Asset->Flipbooks.Swap(FlipbookIndex, FlipbookIndex - 1);

	EndTransaction();

	SelectedFlipbookIndex = FlipbookIndex - 1;

	RefreshOverviewFlipbookList();
	RefreshFlipbookList();
	RefreshAlignmentFlipbookList();
}

void SCharacterDataAssetEditor::MoveFlipbookDown(int32 FlipbookIndex)
{
	if (!Asset.IsValid()) return;
	if (!Asset->Flipbooks.IsValidIndex(FlipbookIndex) || FlipbookIndex >= Asset->Flipbooks.Num() - 1) return;

	BeginTransaction(LOCTEXT("MoveFlipbookDownTrans", "Move Flipbook Down"));

	Asset->Flipbooks.Swap(FlipbookIndex, FlipbookIndex + 1);

	EndTransaction();

	SelectedFlipbookIndex = FlipbookIndex + 1;

	RefreshOverviewFlipbookList();
	RefreshFlipbookList();
	RefreshAlignmentFlipbookList();
}

void SCharacterDataAssetEditor::ShowFlipbookContextMenu(int32 FlipbookIndex)
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
		LOCTEXT("MoveAnimUp", "Move Up"),
		LOCTEXT("MoveFlipbookUpTooltip", "Move this flipbook up in the list"),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([this, FlipbookIndex]() { MoveFlipbookUp(FlipbookIndex); }),
			FCanExecuteAction::CreateLambda([FlipbookIndex]() { return FlipbookIndex > 0; })
		)
	);

	MenuBuilder.AddMenuEntry(
		LOCTEXT("MoveAnimDown", "Move Down"),
		LOCTEXT("MoveFlipbookDownTooltip", "Move this flipbook down in the list"),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([this, FlipbookIndex]() { MoveFlipbookDown(FlipbookIndex); }),
			FCanExecuteAction::CreateLambda([this, FlipbookIndex]() { return Asset.IsValid() && FlipbookIndex < Asset->Flipbooks.Num() - 1; })
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
		LOCTEXT("CTXValidate", "Validate Asset"),
		LOCTEXT("CTXValidateTooltip", "Run CharacterData validation checks"),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([this]()
		{
			if (!Asset.IsValid()) return;
			TArray<FCharacterDataValidationIssue> Issues;
			const bool bValid = Asset->ValidateCharacterDataAsset(Issues);
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

void SCharacterDataAssetEditor::TriggerFlipbookRename(int32 FlipbookIndex)
{
	if (ActiveTabIndex == 0)
	{
		// Overview tab uses flipbook groups panel without inline-editable flipbook names.
		// Show a simple text input dialog instead.
		if (!Asset.IsValid() || !Asset->Flipbooks.IsValidIndex(FlipbookIndex)) return;

		TSharedPtr<SWindow> RenameWindow;
		TSharedPtr<SEditableTextBox> TextBox;

		SAssignNew(RenameWindow, SWindow)
			.Title(LOCTEXT("RenameFlipbookTitle", "Rename Flipbook"))
			.SizingRule(ESizingRule::Autosized)
			.SupportsMinimize(false)
			.SupportsMaximize(false)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(8)
				[
					SNew(SBox)
					.WidthOverride(300)
					[
						SAssignNew(TextBox, SEditableTextBox)
						.Text(FText::FromString(Asset->Flipbooks[FlipbookIndex].FlipbookName))
						.SelectAllTextWhenFocused(true)
						.OnTextCommitted_Lambda([this, FlipbookIndex, &RenameWindow](const FText& InText, ETextCommit::Type CommitType)
						{
							if (CommitType == ETextCommit::OnEnter)
							{
								RenameFlipbook(FlipbookIndex, InText.ToString());
								RefreshOverviewFlipbookList();
								if (RenameWindow.IsValid())
								{
									RenameWindow->RequestDestroyWindow();
								}
							}
						})
					]
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(8, 0, 8, 8)
				.HAlign(HAlign_Right)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
					.Text(LOCTEXT("RenameOK", "OK"))
					.OnClicked_Lambda([this, FlipbookIndex, &TextBox, &RenameWindow]()
					{
						if (TextBox.IsValid())
						{
							RenameFlipbook(FlipbookIndex, TextBox->GetText().ToString());
							RefreshOverviewFlipbookList();
						}
						if (RenameWindow.IsValid())
						{
							RenameWindow->RequestDestroyWindow();
						}
						return FReply::Handled();
					})
				]
			];

		FSlateApplication::Get().AddModalWindow(RenameWindow.ToSharedRef(), SharedThis(this));
	}
	else
	{
		PendingRenameFlipbookIndex = FlipbookIndex;

		if (ActiveTabIndex == 1)
		{
			RefreshFlipbookList();
		}
		else if (ActiveTabIndex == 2)
		{
			RefreshAlignmentFlipbookList();
		}
	}
}

void SCharacterDataAssetEditor::RemoveSelectedFlipbook()
{
	if (!Asset.IsValid()) return;
	if (!Asset->Flipbooks.IsValidIndex(SelectedFlipbookIndex)) return;

	if (Asset->Flipbooks.Num() <= 1) return;

	const FString RemovedName = Asset->Flipbooks[SelectedFlipbookIndex].FlipbookName;

	BeginTransaction(LOCTEXT("RemoveFlipbookTrans", "Remove Flipbook"));

	// Clean up group bindings referencing this flipbook (inside same transaction for undo atomicity)
	Asset->RemoveFlipbookFromTagMappings(RemovedName);
	Asset->Flipbooks.RemoveAt(SelectedFlipbookIndex);

	EndTransaction();

	if (SelectedFlipbookIndex >= Asset->Flipbooks.Num())
	{
		SelectedFlipbookIndex = Asset->Flipbooks.Num() - 1;
	}
	SelectedFrameIndex = 0;

	if (EditorCanvas.IsValid())
	{
		EditorCanvas->ClearSelection();
	}

	RefreshFlipbookList();
	RefreshFrameList();
	RefreshHitboxList();
	RefreshPropertiesPanel();
}

void SCharacterDataAssetEditor::AddNewFrame()
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

void SCharacterDataAssetEditor::RemoveSelectedFrame()
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

const FFrameHitboxData* SCharacterDataAssetEditor::GetCurrentFrame() const
{
	const FFlipbookHitboxData* Anim = GetCurrentFlipbookData();
	if (!Anim) return nullptr;
	if (!Anim->Frames.IsValidIndex(SelectedFrameIndex)) return nullptr;
	return &Anim->Frames[SelectedFrameIndex];
}

FFrameHitboxData* SCharacterDataAssetEditor::GetCurrentFrameMutable()
{
	FFlipbookHitboxData* Anim = GetCurrentFlipbookDataMutable();
	if (!Anim) return nullptr;
	if (!Anim->Frames.IsValidIndex(SelectedFrameIndex)) return nullptr;
	return &Anim->Frames[SelectedFrameIndex];
}

const FFlipbookHitboxData* SCharacterDataAssetEditor::GetCurrentFlipbookData() const
{
	if (!Asset.IsValid()) return nullptr;
	if (!Asset->Flipbooks.IsValidIndex(SelectedFlipbookIndex)) return nullptr;
	return &Asset->Flipbooks[SelectedFlipbookIndex];
}

FFlipbookHitboxData* SCharacterDataAssetEditor::GetCurrentFlipbookDataMutable()
{
	if (!Asset.IsValid()) return nullptr;
	if (!Asset->Flipbooks.IsValidIndex(SelectedFlipbookIndex)) return nullptr;
	return &Asset->Flipbooks[SelectedFlipbookIndex];
}

TArray<int32> SCharacterDataAssetEditor::GetSortedFlipbookIndices() const
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

void SCharacterDataAssetEditor::BuildGroupedFlipbookList(TSharedPtr<SVerticalBox> ListBox, TFunction<TSharedRef<SWidget>(int32)> ItemBuilder, TFunction<bool(int32)> Filter)
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

int32 SCharacterDataAssetEditor::GetCurrentFrameCount() const
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

UPaperSprite* SCharacterDataAssetEditor::GetCurrentSprite() const
{
	const FFlipbookHitboxData* Anim = GetCurrentFlipbookData();
	if (!Anim || Anim->Flipbook.IsNull()) return nullptr;

	UPaperFlipbook* FB = Anim->Flipbook.LoadSynchronous();
	if (!FB || SelectedFrameIndex < 0 || SelectedFrameIndex >= FB->GetNumKeyFrames()) return nullptr;

	return FB->GetKeyFrameChecked(SelectedFrameIndex).Sprite;
}

#undef LOCTEXT_NAMESPACE
