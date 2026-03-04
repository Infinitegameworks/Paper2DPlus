// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "CharacterDataAssetEditor.h"
#include "FrameTimingEditor.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "PaperFlipbook.h"
#include "PaperSprite.h"
#include "Paper2DPlusSettings.h"
#include "ScopedTransaction.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/MessageDialog.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Editor/TransBuffer.h"

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
			for (const FGameplayTag& RequiredTag : Settings->RequiredAnimationGroups)
			{
				if (RequiredTag.IsValid() && !Asset->GroupBindings.Contains(RequiredTag))
				{
					if (!bAddedAny)
					{
						Asset->Modify();
						bAddedAny = true;
					}
					Asset->GroupBindings.Add(RequiredTag, FAnimationGroupBinding());
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

	// Populate the overview tab on initial load
	RefreshOverviewAnimationList();
	RefreshGroupMappingsPanel();
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
				.ToolTipText(LOCTEXT("OpenHelpTooltip", "Open editor help and guidance"))
				.Text(LOCTEXT("HelpButton", "Help"))
				.OnClicked_Lambda([this]() {
					FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("CharacterDataHelpDialog",
						"CharacterData Editor Help\n\n"
						"1) Overview: manage animations, dimensions, validation, and extraction.\n"
						"2) Hitbox Editor: edit per-frame hitboxes/sockets and run frame batch operations.\n"
						"3) Sprite/Flipbook Tools: adjust offsets, apply flips, and refine alignment visuals.\n"
						"4) Frame Timing: adjust playback timing and sequence behavior.\n\n"
						"Tip: Right-click animations for quick actions like editing, validation, and trimming."));
					return FReply::Handled();
				})
			]
		];
}



TSharedRef<SWidget> SCharacterDataAssetEditor::BuildFrameTimingTab()
{
	return SAssignNew(FrameTimingEditor, SFrameTimingEditor)
		.Asset(Asset.Get());
}

void SCharacterDataAssetEditor::OnEditTimingClicked(int32 AnimationIndex)
{
	SelectedAnimationIndex = AnimationIndex;
	SelectedFrameIndex = 0;
	if (FrameTimingEditor.IsValid())
	{
		FrameTimingEditor->SetSelectedAnimation(AnimationIndex);
	}
	SwitchToTab(3);
}

void SCharacterDataAssetEditor::SetReferenceSprite(int32 AnimIndex, int32 FrameIndex)
{
	ReferenceAnimationIndex = AnimIndex;
	ReferenceFrameIndex = FrameIndex;
	bShowReferenceSprite = true;
}

void SCharacterDataAssetEditor::ClearReferenceSprite()
{
	bShowReferenceSprite = false;
	ReferenceAnimationIndex = INDEX_NONE;
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
		RefreshOverviewAnimationList();
	}
	else if (TabIndex == 1)
	{
		RefreshAnimationList();
		RefreshFrameList();
		RefreshHitboxList();
	}
	else if (TabIndex == 2)
	{
		RefreshAlignmentAnimationList();
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

void SCharacterDataAssetEditor::OnEditHitboxesClicked(int32 AnimationIndex)
{
	SelectedAnimationIndex = AnimationIndex;
	SelectedFrameIndex = 0;
	SwitchToTab(1);
}

FReply SCharacterDataAssetEditor::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	// Global shortcuts (all tabs)
	if (InKeyEvent.IsControlDown() && InKeyEvent.GetKey() == EKeys::Z)
	{
		GEditor->UndoTransaction();
		RefreshAll(); // RefreshAll already calls RefreshUndoHistory
		return FReply::Handled();
	}

	if (InKeyEvent.IsControlDown() && InKeyEvent.GetKey() == EKeys::Y)
	{
		GEditor->RedoTransaction();
		RefreshAll(); // RefreshAll already calls RefreshUndoHistory
		return FReply::Handled();
	}

	// Alignment Editor Tab shortcuts (ActiveTabIndex == 2)
	if (ActiveTabIndex == 2)
	{
		// Space - toggle playback
		if (InKeyEvent.GetKey() == EKeys::SpaceBar)
		{
			TogglePlayback();
			return FReply::Handled();
		}

		// Left Arrow / Comma - previous frame (wraps to previous animation)
		if (InKeyEvent.GetKey() == EKeys::Left || InKeyEvent.GetKey() == EKeys::Comma)
		{
			if (SelectedFrameIndex > 0)
			{
				OnPrevFrameClicked();
			}
			else if (Asset.IsValid())
			{
				// At frame 0 — wrap to previous animation's last frame
				int32 PrevAnimIdx = GetAdjacentAnimationIndex(-1);
				if (PrevAnimIdx != INDEX_NONE && PrevAnimIdx != SelectedAnimationIndex)
				{
					OnAnimationSelected(PrevAnimIdx);
					// Jump to last frame
					int32 FrameCount = GetCurrentFrameCount();
					if (FrameCount > 0)
					{
						SelectedFrameIndex = FrameCount - 1;
					}
				}
			}
			RefreshAlignmentFrameList();
			return FReply::Handled();
		}

		// Right Arrow / Period - next frame (wraps to next animation)
		if (InKeyEvent.GetKey() == EKeys::Right || InKeyEvent.GetKey() == EKeys::Period)
		{
			int32 FrameCount = GetCurrentFrameCount();
			if (SelectedFrameIndex < FrameCount - 1)
			{
				OnNextFrameClicked();
			}
			else if (Asset.IsValid())
			{
				// At last frame — wrap to next animation's first frame
				int32 NextAnimIdx = GetAdjacentAnimationIndex(1);
				if (NextAnimIdx != INDEX_NONE && NextAnimIdx != SelectedAnimationIndex)
				{
					OnAnimationSelected(NextAnimIdx);
					// Already at frame 0 from OnAnimationSelected
				}
			}
			RefreshAlignmentFrameList();
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

		// R - toggle reference sprite
		if (InKeyEvent.GetKey() == EKeys::R)
		{
			if (ReferenceAnimationIndex == INDEX_NONE)
			{
				// No reference set — capture current frame
				SetReferenceSprite(SelectedAnimationIndex, SelectedFrameIndex);
			}
			else
			{
				// Toggle visibility
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

		if (InKeyEvent.GetKey() == EKeys::Comma)
		{
			OnPrevFrameClicked();
			return FReply::Handled();
		}
		if (InKeyEvent.GetKey() == EKeys::Period)
		{
			OnNextFrameClicked();
			return FReply::Handled();
		}
	}

	return FReply::Unhandled();
}

void SCharacterDataAssetEditor::RefreshAll()
{
	RefreshAnimationList();
	RefreshFrameList();
	RefreshHitboxList();
	RefreshPropertiesPanel();
	RefreshCurrentFrameFlipState();
	RefreshOverviewAnimationList();
	RefreshGroupMappingsPanel();
	RefreshUndoHistory();

	// Purge queue entries referencing invalid animation indices (e.g., after undo)
	if (Asset.IsValid() && PlaybackQueue.Num() > 0)
	{
		bool bPurged = false;
		for (int32 i = PlaybackQueue.Num() - 1; i >= 0; i--)
		{
			if (!Asset->Animations.IsValidIndex(PlaybackQueue[i]))
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

void SCharacterDataAssetEditor::RefreshUndoHistory()
{
	if (!UndoHistoryBox.IsValid()) return;
	UndoHistoryBox->ClearChildren();

	if (!GEditor || !GEditor->Trans) return;

	UTransBuffer* TransBuffer = Cast<UTransBuffer>(GEditor->Trans);
	if (!TransBuffer) return;

	const TArray<TSharedRef<FTransaction>>& UndoBuffer = TransBuffer->UndoBuffer;
	int32 UndoCount = TransBuffer->GetUndoCount();
	int32 CurrentIndex = UndoBuffer.Num() - UndoCount; // Current position

	int32 StartIndex = FMath::Max(0, UndoBuffer.Num() - 20);
	for (int32 i = UndoBuffer.Num() - 1; i >= StartIndex; i--)
	{
		const FTransaction& Trans = UndoBuffer[i].Get();
		FText Title = Trans.GetTitle();
		bool bIsUndoable = (i < CurrentIndex);
		bool bIsCurrent = (i == CurrentIndex - 1);

		FLinearColor TextColor = bIsUndoable ? FLinearColor::White : FLinearColor(0.5f, 0.5f, 0.5f);
		if (bIsCurrent) TextColor = FLinearColor::Yellow;

		UndoHistoryBox->AddSlot()
		.AutoHeight()
		.Padding(2)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.OnClicked_Lambda([this, i, CurrentIndex]() -> FReply {
				// Undo or redo to reach this position
				int32 TargetIndex = i + 1;
				int32 Steps = CurrentIndex - TargetIndex;
				if (Steps > 0)
				{
					for (int32 s = 0; s < Steps; s++)
						GEditor->UndoTransaction();
				}
				else if (Steps < 0)
				{
					for (int32 s = 0; s < -Steps; s++)
						GEditor->RedoTransaction();
				}
				RefreshAll();
				return FReply::Handled();
			})
			[
				SNew(STextBlock)
				.Text(Title)
				.ColorAndOpacity(FSlateColor(TextColor))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
			]
		];
	}
}

void SCharacterDataAssetEditor::OnAnimationSelected(int32 Index)
{
	// Pause queue playback on manual selection
	if (bIsPlaying && PlaybackQueue.Num() > 0)
	{
		StopPlayback();
	}

	SelectedAnimationIndex = Index;
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
	RefreshUndoHistory();
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

	RefreshUndoHistory();
}

void SCharacterDataAssetEditor::AddNewAnimation()
{
	if (!Asset.IsValid()) return;

	BeginTransaction(LOCTEXT("AddFlipbookTrans", "Add Flipbook"));

	FAnimationHitboxData NewAnim;
	NewAnim.AnimationName = FString::Printf(TEXT("Animation_%d"), Asset->Animations.Num());

	FFrameHitboxData DefaultFrame;
	DefaultFrame.FrameName = TEXT("Frame_0");
	NewAnim.Frames.Add(DefaultFrame);

	int32 NewIndex = Asset->Animations.Add(NewAnim);

	EndTransaction();

	SelectedAnimationIndex = NewIndex;
	SelectedFrameIndex = 0;

	RefreshAnimationList();
	RefreshFrameList();
	RefreshHitboxList();
	RefreshPropertiesPanel();
}

void SCharacterDataAssetEditor::RenameAnimation(int32 AnimIndex, const FString& NewName)
{
	if (!Asset.IsValid()) return;
	if (!Asset->Animations.IsValidIndex(AnimIndex)) return;

	FString TrimmedName = NewName.TrimStartAndEnd();
	if (TrimmedName.IsEmpty()) return;

	FAnimationHitboxData& Anim = Asset->Animations[AnimIndex];
	if (Anim.AnimationName == TrimmedName) return;

	// Check for duplicate names
	for (int32 i = 0; i < Asset->Animations.Num(); i++)
	{
		if (i != AnimIndex && Asset->Animations[i].AnimationName == TrimmedName)
		{
			return; // Name already in use
		}
	}

	BeginTransaction(LOCTEXT("RenameAnimationTrans", "Rename Animation"));

	FString OldName = Anim.AnimationName;
	Anim.AnimationName = TrimmedName;
	Asset->UpdateGroupBindingAnimationName(OldName, TrimmedName);

	EndTransaction();

	RefreshOverviewAnimationList();
	RefreshAnimationList();
	RefreshAlignmentAnimationList();
	RefreshGroupMappingsPanel();
}

void SCharacterDataAssetEditor::DuplicateAnimation(int32 AnimIndex)
{
	if (!Asset.IsValid()) return;
	if (!Asset->Animations.IsValidIndex(AnimIndex)) return;

	BeginTransaction(LOCTEXT("DuplicateAnimationTrans", "Duplicate Animation"));

	FAnimationHitboxData NewAnim = Asset->Animations[AnimIndex]; // Deep copy
	NewAnim.AnimationName = NewAnim.AnimationName + TEXT(" (Copy)");

	// Ensure unique name
	FString BaseName = NewAnim.AnimationName;
	int32 Counter = 2;
	while (true)
	{
		bool bNameExists = false;
		for (const FAnimationHitboxData& Existing : Asset->Animations)
		{
			if (Existing.AnimationName == NewAnim.AnimationName)
			{
				bNameExists = true;
				break;
			}
		}
		if (!bNameExists) break;
		NewAnim.AnimationName = FString::Printf(TEXT("%s %d"), *BaseName, Counter++);
	}

	int32 InsertIndex = AnimIndex + 1;
	Asset->Animations.Insert(NewAnim, InsertIndex);

	EndTransaction();

	SelectedAnimationIndex = InsertIndex;
	SelectedFrameIndex = 0;

	RefreshOverviewAnimationList();
	RefreshAnimationList();
	RefreshAlignmentAnimationList();
	RefreshFrameList();
	RefreshHitboxList();
	RefreshPropertiesPanel();
}

void SCharacterDataAssetEditor::MoveAnimationUp(int32 AnimIndex)
{
	if (!Asset.IsValid()) return;
	if (AnimIndex <= 0 || !Asset->Animations.IsValidIndex(AnimIndex)) return;

	BeginTransaction(LOCTEXT("MoveAnimUpTrans", "Move Animation Up"));

	Asset->Animations.Swap(AnimIndex, AnimIndex - 1);

	EndTransaction();

	SelectedAnimationIndex = AnimIndex - 1;

	RefreshOverviewAnimationList();
	RefreshAnimationList();
	RefreshAlignmentAnimationList();
}

void SCharacterDataAssetEditor::MoveAnimationDown(int32 AnimIndex)
{
	if (!Asset.IsValid()) return;
	if (!Asset->Animations.IsValidIndex(AnimIndex) || AnimIndex >= Asset->Animations.Num() - 1) return;

	BeginTransaction(LOCTEXT("MoveAnimDownTrans", "Move Animation Down"));

	Asset->Animations.Swap(AnimIndex, AnimIndex + 1);

	EndTransaction();

	SelectedAnimationIndex = AnimIndex + 1;

	RefreshOverviewAnimationList();
	RefreshAnimationList();
	RefreshAlignmentAnimationList();
}

void SCharacterDataAssetEditor::ShowAnimationContextMenu(int32 AnimIndex)
{
	if (!Asset.IsValid()) return;
	if (!Asset->Animations.IsValidIndex(AnimIndex)) return;

	FMenuBuilder MenuBuilder(true, nullptr);

	MenuBuilder.AddMenuEntry(
		LOCTEXT("RenameAnim", "Rename"),
		LOCTEXT("RenameAnimTooltip", "Rename this animation"),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([this, AnimIndex]()
		{
			TriggerAnimationRename(AnimIndex);
		}))
	);

	MenuBuilder.AddMenuEntry(
		LOCTEXT("DuplicateAnim", "Duplicate"),
		LOCTEXT("DuplicateAnimTooltip", "Create a copy of this animation"),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([this, AnimIndex]()
		{
			DuplicateAnimation(AnimIndex);
		}))
	);

	MenuBuilder.AddMenuSeparator();

	MenuBuilder.AddMenuEntry(
		LOCTEXT("MoveAnimUp", "Move Up"),
		LOCTEXT("MoveAnimUpTooltip", "Move this animation up in the list"),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([this, AnimIndex]() { MoveAnimationUp(AnimIndex); }),
			FCanExecuteAction::CreateLambda([AnimIndex]() { return AnimIndex > 0; })
		)
	);

	MenuBuilder.AddMenuEntry(
		LOCTEXT("MoveAnimDown", "Move Down"),
		LOCTEXT("MoveAnimDownTooltip", "Move this animation down in the list"),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([this, AnimIndex]() { MoveAnimationDown(AnimIndex); }),
			FCanExecuteAction::CreateLambda([this, AnimIndex]() { return Asset.IsValid() && AnimIndex < Asset->Animations.Num() - 1; })
		)
	);

	MenuBuilder.AddMenuSeparator();

	MenuBuilder.AddMenuEntry(
		LOCTEXT("DeleteAnim", "Delete"),
		LOCTEXT("DeleteAnimTooltip", "Delete this animation"),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([this, AnimIndex]()
			{
				SelectedAnimationIndex = AnimIndex;
				RemoveSelectedAnimation();
				RefreshOverviewAnimationList();
			}),
			FCanExecuteAction::CreateLambda([this]() { return Asset.IsValid() && Asset->Animations.Num() > 1; })
		)
	);

	if (ActiveTabIndex == 2) // Alignment tab
	{
		MenuBuilder.AddMenuSeparator();

		MenuBuilder.AddMenuEntry(
			LOCTEXT("CTXAddToQueue", "Add to Queue"),
			LOCTEXT("CTXAddToQueueTooltip", "Add this animation to the playback queue"),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([this, AnimIndex]()
			{
				AddToPlaybackQueue(AnimIndex);
			}))
		);
	}

	MenuBuilder.AddMenuSeparator();

	MenuBuilder.AddMenuEntry(
		LOCTEXT("CTXEditHitboxes", "Edit Hitboxes"),
		LOCTEXT("CTXEditHitboxesTooltip", "Open this animation in the Hitbox Editor"),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([this, AnimIndex]()
		{
			SelectedAnimationIndex = AnimIndex;
			SwitchToTab(1);
		}))
	);

	MenuBuilder.AddMenuEntry(
		LOCTEXT("CTXEditAlignment", "Edit Alignment"),
		LOCTEXT("CTXEditAlignmentTooltip", "Open this animation in the Sprite/Flipbook Tools"),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([this, AnimIndex]()
		{
			SelectedAnimationIndex = AnimIndex;
			SwitchToTab(2);
		}))
	);

	MenuBuilder.AddMenuEntry(
		LOCTEXT("CTXEditTiming", "Edit Timing"),
		LOCTEXT("CTXEditTimingTooltip", "Open this animation in the Frame Timing editor"),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([this, AnimIndex]()
		{
			SelectedAnimationIndex = AnimIndex;
			SwitchToTab(3);
		}))
	);

	MenuBuilder.AddMenuSeparator();

	MenuBuilder.AddMenuEntry(
		LOCTEXT("CTXSetAsRefSprite", "Set as Reference Sprite"),
		LOCTEXT("CTXSetAsRefSpriteTooltip", "Set frame 0 of this animation as the alignment reference sprite"),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([this, AnimIndex]()
		{
			SetReferenceSprite(AnimIndex, 0);
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

void SCharacterDataAssetEditor::TriggerAnimationRename(int32 AnimIndex)
{
	PendingRenameAnimationIndex = AnimIndex;

	// Refresh whichever tab is active so the inline text block gets created
	if (ActiveTabIndex == 0)
	{
		RefreshOverviewAnimationList();
	}
	else if (ActiveTabIndex == 1)
	{
		RefreshAnimationList();
	}
	else if (ActiveTabIndex == 2)
	{
		RefreshAlignmentAnimationList();
	}
}

void SCharacterDataAssetEditor::RemoveSelectedAnimation()
{
	if (!Asset.IsValid()) return;
	if (!Asset->Animations.IsValidIndex(SelectedAnimationIndex)) return;

	if (Asset->Animations.Num() <= 1) return;

	const FString RemovedName = Asset->Animations[SelectedAnimationIndex].AnimationName;

	BeginTransaction(LOCTEXT("RemoveFlipbookTrans", "Remove Flipbook"));

	// Clean up group bindings referencing this animation (inside same transaction for undo atomicity)
	Asset->RemoveAnimationFromGroupBindings(RemovedName);
	Asset->Animations.RemoveAt(SelectedAnimationIndex);

	EndTransaction();

	if (SelectedAnimationIndex >= Asset->Animations.Num())
	{
		SelectedAnimationIndex = Asset->Animations.Num() - 1;
	}
	SelectedFrameIndex = 0;

	if (EditorCanvas.IsValid())
	{
		EditorCanvas->ClearSelection();
	}

	RefreshAnimationList();
	RefreshFrameList();
	RefreshHitboxList();
	RefreshPropertiesPanel();
}

void SCharacterDataAssetEditor::AddNewFrame()
{
	FAnimationHitboxData* Anim = GetCurrentAnimationMutable();
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
	FAnimationHitboxData* Anim = GetCurrentAnimationMutable();
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
	const FAnimationHitboxData* Anim = GetCurrentAnimation();
	if (!Anim) return nullptr;
	if (!Anim->Frames.IsValidIndex(SelectedFrameIndex)) return nullptr;
	return &Anim->Frames[SelectedFrameIndex];
}

FFrameHitboxData* SCharacterDataAssetEditor::GetCurrentFrameMutable()
{
	FAnimationHitboxData* Anim = GetCurrentAnimationMutable();
	if (!Anim) return nullptr;
	if (!Anim->Frames.IsValidIndex(SelectedFrameIndex)) return nullptr;
	return &Anim->Frames[SelectedFrameIndex];
}

const FAnimationHitboxData* SCharacterDataAssetEditor::GetCurrentAnimation() const
{
	if (!Asset.IsValid()) return nullptr;
	if (!Asset->Animations.IsValidIndex(SelectedAnimationIndex)) return nullptr;
	return &Asset->Animations[SelectedAnimationIndex];
}

FAnimationHitboxData* SCharacterDataAssetEditor::GetCurrentAnimationMutable()
{
	if (!Asset.IsValid()) return nullptr;
	if (!Asset->Animations.IsValidIndex(SelectedAnimationIndex)) return nullptr;
	return &Asset->Animations[SelectedAnimationIndex];
}

int32 SCharacterDataAssetEditor::GetCurrentFrameCount() const
{
	const FAnimationHitboxData* Anim = GetCurrentAnimation();
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
	const FAnimationHitboxData* Anim = GetCurrentAnimation();
	if (!Anim || Anim->Flipbook.IsNull()) return nullptr;

	UPaperFlipbook* FB = Anim->Flipbook.LoadSynchronous();
	if (!FB || SelectedFrameIndex < 0 || SelectedFrameIndex >= FB->GetNumKeyFrames()) return nullptr;

	return FB->GetKeyFrameChecked(SelectedFrameIndex).Sprite;
}

#undef LOCTEXT_NAMESPACE
