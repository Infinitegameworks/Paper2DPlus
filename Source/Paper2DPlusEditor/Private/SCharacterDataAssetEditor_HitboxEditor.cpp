// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "CharacterDataAssetEditor.h"
#include "EditorCanvasUtils.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Images/SImage.h"
#include "PaperFlipbook.h"
#include "PaperSprite.h"
#include "Engine/Texture2D.h"
#include "ScopedTransaction.h"
#include "Paper2DPlusSettings.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Framework/Application/SlateApplication.h"
#include "PropertyCustomizationHelpers.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "UnrealClient.h"
#include "SceneView.h"
#include "CanvasItem.h"
#include "CanvasTypes.h"

#define LOCTEXT_NAMESPACE "CharacterDataAssetEditor"

TSharedRef<SWidget> SCharacterDataAssetEditor::BuildHitboxEditorTab()
{
	return SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			BuildToolbar()
		]

		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		.Padding(4)
		[
			SNew(SSplitter)
			.Orientation(Orient_Horizontal)

			+ SSplitter::Slot()
			.Value(0.2f)
			[
				SNew(SVerticalBox)

				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					BuildToolPanel()
				]

				+ SVerticalBox::Slot()
				.FillHeight(0.4f)
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
					.Padding(4)
					[
						BuildFlipbookList()
					]
				]

				+ SVerticalBox::Slot()
				.FillHeight(0.6f)
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
					.Padding(4)
					[
						BuildFrameList()
					]
				]
			]

			+ SSplitter::Slot()
			.Value(0.55f)
			[
				SNew(SVerticalBox)

				// Canvas Area
				+ SVerticalBox::Slot()
				.FillHeight(1.0f)
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
					.Padding(4)
					[
						BuildCanvasArea()
					]
				]

				// Dimension Management moved to Overview tab (T8)
			]

			+ SSplitter::Slot()
			.Value(0.25f)
			[
				SNew(SScrollBox)

				+ SScrollBox::Slot()
				.Padding(0, 0, 0, 4)
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
					.Padding(4)
					[
						BuildHitboxList()
					]
				]

				+ SScrollBox::Slot()
				.Padding(0, 0, 0, 4)
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
					.Padding(4)
					[
						BuildPropertiesPanel()
					]
				]

				+ SScrollBox::Slot()
				.Padding(0, 0, 0, 4)
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
					.Padding(4)
					[
						BuildCopyOperationsPanel()
					]
				]

				]
		];
}

TSharedRef<SWidget> SCharacterDataAssetEditor::BuildToolbar()
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(4)
		[
			SNew(SWrapBox)
			.UseAllottedSize(true)

			+ SWrapBox::Slot()
			.Padding(2)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
				.Text(LOCTEXT("Undo", "Undo"))
				.OnClicked_Lambda([this]()
				{
					GEditor->UndoTransaction();
					RefreshAll();
					return FReply::Handled();
				})
			]
			+ SWrapBox::Slot()
			.Padding(2)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
				.Text(LOCTEXT("Redo", "Redo"))
				.OnClicked_Lambda([this]()
				{
					GEditor->RedoTransaction();
					RefreshAll();
					return FReply::Handled();
				})
			]

			+ SWrapBox::Slot()
			.Padding(8, 0)
			[
				SNew(SSeparator)
				.Orientation(Orient_Vertical)
			]

			+ SWrapBox::Slot()
			.Padding(2)
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([this]() { return bShowGrid ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this](ECheckBoxState NewState) { bShowGrid = (NewState == ECheckBoxState::Checked); })
				[
					SNew(STextBlock)
					.Text(LOCTEXT("ShowGrid", "Grid (G)"))
				]
			]

			+ SWrapBox::Slot()
			.Padding(8, 0, 2, 0)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("ZoomLabel", "Zoom:"))
			]
			+ SWrapBox::Slot()
			.Padding(2)
			[
				SNew(SBox)
				.WidthOverride(100)
				[
					SNew(SSlider)
					.MinValue(0.5f)
					.MaxValue(4.0f)
					.Value_Lambda([this]() { return ZoomLevel; })
					.OnValueChanged_Lambda([this](float NewValue) { OnZoomChanged(NewValue); })
				]
			]
			+ SWrapBox::Slot()
			.Padding(2)
			[
				SNew(STextBlock)
				.Text_Lambda([this]() { return FText::Format(LOCTEXT("ZoomPercent", "{0}%"), FText::AsNumber(FMath::RoundToInt(ZoomLevel * 100))); })
			]

			+ SWrapBox::Slot()
			.Padding(4, 0, 0, 0)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
				.Text(LOCTEXT("ZoomReset", "Reset"))
				.ToolTipText(LOCTEXT("ZoomResetTooltip", "Reset zoom to 100% (Ctrl+0)"))
				.OnClicked_Lambda([this]() { OnZoomChanged(1.0f); return FReply::Handled(); })
			]

			+ SWrapBox::Slot()
			.Padding(8, 0)
			[
				SNew(SSeparator)
				.Orientation(Orient_Vertical)
			]

			+ SWrapBox::Slot()
			.Padding(2, 0, 0, 0)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("ShowLabel", "Show:"))
			]

			+ SWrapBox::Slot()
			.Padding(4, 0, 2, 0)
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([this]() { return (HitboxVisibilityMask & 0x01) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this](ECheckBoxState State) {
					HitboxVisibilityMask = (State == ECheckBoxState::Checked) ? (HitboxVisibilityMask | 0x01) : (HitboxVisibilityMask & ~0x01);
					if (State == ECheckBoxState::Unchecked && EditorCanvas.IsValid())
					{
						const FFrameHitboxData* Frame = GetCurrentFrame();
						if (Frame)
						{
							for (int32 Idx : EditorCanvas->GetSelectedIndices())
							{
								if (Frame->Hitboxes.IsValidIndex(Idx) && Frame->Hitboxes[Idx].Type == EHitboxType::Attack)
									EditorCanvas->RemoveFromSelection(Idx);
							}
						}
					}
					RefreshHitboxList();
					RefreshPropertiesPanel();
				})
				.ToolTipText(LOCTEXT("ShowAttackTooltip", "Show Attack hitboxes"))
				[
					SNew(STextBlock)
					.Text(LOCTEXT("ATKFilter", "ATK"))
					.ColorAndOpacity(FSlateColor(FLinearColor::Red))
					.Font(FAppStyle::GetFontStyle("SmallFont"))
				]
			]

			+ SWrapBox::Slot()
			.Padding(2, 0)
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([this]() { return (HitboxVisibilityMask & 0x02) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this](ECheckBoxState State) {
					HitboxVisibilityMask = (State == ECheckBoxState::Checked) ? (HitboxVisibilityMask | 0x02) : (HitboxVisibilityMask & ~0x02);
					if (State == ECheckBoxState::Unchecked && EditorCanvas.IsValid())
					{
						const FFrameHitboxData* Frame = GetCurrentFrame();
						if (Frame)
						{
							for (int32 Idx : EditorCanvas->GetSelectedIndices())
							{
								if (Frame->Hitboxes.IsValidIndex(Idx) && Frame->Hitboxes[Idx].Type == EHitboxType::Hurtbox)
									EditorCanvas->RemoveFromSelection(Idx);
							}
						}
					}
					RefreshHitboxList();
					RefreshPropertiesPanel();
				})
				.ToolTipText(LOCTEXT("ShowHurtboxTooltip", "Show Hurtbox hitboxes"))
				[
					SNew(STextBlock)
					.Text(LOCTEXT("HRTFilter", "HRT"))
					.ColorAndOpacity(FSlateColor(FLinearColor::Green))
					.Font(FAppStyle::GetFontStyle("SmallFont"))
				]
			]

			+ SWrapBox::Slot()
			.Padding(2, 0)
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([this]() { return (HitboxVisibilityMask & 0x04) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this](ECheckBoxState State) {
					HitboxVisibilityMask = (State == ECheckBoxState::Checked) ? (HitboxVisibilityMask | 0x04) : (HitboxVisibilityMask & ~0x04);
					if (State == ECheckBoxState::Unchecked && EditorCanvas.IsValid())
					{
						const FFrameHitboxData* Frame = GetCurrentFrame();
						if (Frame)
						{
							for (int32 Idx : EditorCanvas->GetSelectedIndices())
							{
								if (Frame->Hitboxes.IsValidIndex(Idx) && Frame->Hitboxes[Idx].Type == EHitboxType::Collision)
									EditorCanvas->RemoveFromSelection(Idx);
							}
						}
					}
					RefreshHitboxList();
					RefreshPropertiesPanel();
				})
				.ToolTipText(LOCTEXT("ShowCollisionTooltip", "Show Collision hitboxes"))
				[
					SNew(STextBlock)
					.Text(LOCTEXT("COLFilter", "COL"))
					.ColorAndOpacity(FSlateColor(FLinearColor::Blue))
					.Font(FAppStyle::GetFontStyle("SmallFont"))
				]
			]
		];
}

TSharedRef<SWidget> SCharacterDataAssetEditor::BuildToolPanel()
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(4)
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(2, 2, 2, 6)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("Tools", "Tools"))
				.Font(FAppStyle::GetFontStyle("BoldFont"))
			]

			// Tool buttons with icons and clear active states
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(2)
			[
				// Draw tool
				SNew(SBorder)
				.BorderImage_Lambda([this]() { return CurrentTool == EHitboxEditorTool::Draw ? FAppStyle::GetBrush("ToolPanel.DarkGroupBorder") : FAppStyle::GetBrush("NoBorder"); })
				.BorderBackgroundColor_Lambda([this]() { return CurrentTool == EHitboxEditorTool::Draw ? FLinearColor(0.2f, 0.6f, 0.2f, 0.5f) : FLinearColor::Transparent; })
				.Padding(2)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "NoBorder")
					.ToolTipText(LOCTEXT("DrawToolTooltip", "Draw Tool (D)\nClick and drag to create new hitboxes"))
					.OnClicked_Lambda([this]() { OnToolSelected(EHitboxEditorTool::Draw); return FReply::Handled(); })
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(4, 2)
						[
							SNew(SImage)
							.Image(FAppStyle::GetBrush("Icons.Edit"))
							.ColorAndOpacity_Lambda([this]() { return CurrentTool == EHitboxEditorTool::Draw ? FLinearColor(0.3f, 1.0f, 0.3f) : FLinearColor(0.7f, 0.7f, 0.7f); })
						]
						+ SHorizontalBox::Slot()
						.FillWidth(1.0f)
						.VAlign(VAlign_Center)
						.Padding(4, 2)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("DrawToolShort", "Draw"))
							.ColorAndOpacity_Lambda([this]() { return CurrentTool == EHitboxEditorTool::Draw ? FSlateColor(FLinearColor::White) : FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f)); })
						]
					]
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(2)
			[
				// Edit tool
				SNew(SBorder)
				.BorderImage_Lambda([this]() { return CurrentTool == EHitboxEditorTool::Edit ? FAppStyle::GetBrush("ToolPanel.DarkGroupBorder") : FAppStyle::GetBrush("NoBorder"); })
				.BorderBackgroundColor_Lambda([this]() { return CurrentTool == EHitboxEditorTool::Edit ? FLinearColor(0.2f, 0.4f, 0.8f, 0.5f) : FLinearColor::Transparent; })
				.Padding(2)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "NoBorder")
					.ToolTipText(LOCTEXT("EditToolTooltip", "Edit Tool (E)\nSelect and move existing hitboxes"))
					.OnClicked_Lambda([this]() { OnToolSelected(EHitboxEditorTool::Edit); return FReply::Handled(); })
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(4, 2)
						[
							SNew(SImage)
							.Image(FAppStyle::GetBrush("Icons.Transform"))
							.ColorAndOpacity_Lambda([this]() { return CurrentTool == EHitboxEditorTool::Edit ? FLinearColor(0.4f, 0.6f, 1.0f) : FLinearColor(0.7f, 0.7f, 0.7f); })
						]
						+ SHorizontalBox::Slot()
						.FillWidth(1.0f)
						.VAlign(VAlign_Center)
						.Padding(4, 2)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("EditToolShort", "Edit"))
							.ColorAndOpacity_Lambda([this]() { return CurrentTool == EHitboxEditorTool::Edit ? FSlateColor(FLinearColor::White) : FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f)); })
						]
					]
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(2)
			[
				// Socket tool
				SNew(SBorder)
				.BorderImage_Lambda([this]() { return CurrentTool == EHitboxEditorTool::Socket ? FAppStyle::GetBrush("ToolPanel.DarkGroupBorder") : FAppStyle::GetBrush("NoBorder"); })
				.BorderBackgroundColor_Lambda([this]() { return CurrentTool == EHitboxEditorTool::Socket ? FLinearColor(0.8f, 0.6f, 0.2f, 0.5f) : FLinearColor::Transparent; })
				.Padding(2)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "NoBorder")
					.ToolTipText(LOCTEXT("SocketToolTooltip", "Socket Tool (S)\nClick to place attachment points"))
					.OnClicked_Lambda([this]() { OnToolSelected(EHitboxEditorTool::Socket); return FReply::Handled(); })
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(4, 2)
						[
							SNew(SImage)
							.Image(FAppStyle::GetBrush("Icons.Plus"))
							.ColorAndOpacity_Lambda([this]() { return CurrentTool == EHitboxEditorTool::Socket ? FLinearColor(1.0f, 0.8f, 0.3f) : FLinearColor(0.7f, 0.7f, 0.7f); })
						]
						+ SHorizontalBox::Slot()
						.FillWidth(1.0f)
						.VAlign(VAlign_Center)
						.Padding(4, 2)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("SocketToolShort", "Socket"))
							.ColorAndOpacity_Lambda([this]() { return CurrentTool == EHitboxEditorTool::Socket ? FSlateColor(FLinearColor::White) : FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f)); })
						]
					]
				]
			]
		];
}

TSharedRef<SWidget> SCharacterDataAssetEditor::BuildFlipbookList()
{
	SAssignNew(FlipbookListBox, SVerticalBox);

	FlipbookListBox->AddSlot()
	.AutoHeight()
	.Padding(4)
	[
		SNew(STextBlock)
		.Text(LOCTEXT("FlipbooksHeader", "Flipbooks"))
		.Font(FAppStyle::GetFontStyle("BoldFont"))
	];

	RefreshFlipbookList();

	return SNew(SScrollBox) + SScrollBox::Slot()[FlipbookListBox.ToSharedRef()];
}

void SCharacterDataAssetEditor::RefreshFlipbookList()
{
	if (!FlipbookListBox.IsValid()) return;

	while (FlipbookListBox->NumSlots() > 1)
	{
		FlipbookListBox->RemoveSlot(FlipbookListBox->GetSlot(1).GetWidget());
	}

	SidebarFlipbookNameTexts.Empty();

	if (Asset.IsValid())
	{
		BuildGroupedFlipbookList(FlipbookListBox, [this](int32 i) -> TSharedRef<SWidget>
		{
			const FFlipbookHitboxData& Anim = Asset->Flipbooks[i];
			bool bIsSelected = (i == SelectedFlipbookIndex);
			bool bHasFlipbook = !Anim.Flipbook.IsNull();
			int32 FrameCount = bHasFlipbook && Anim.Flipbook.LoadSynchronous() ? Anim.Flipbook.LoadSynchronous()->GetNumKeyFrames() : Anim.Frames.Num();

			TSharedPtr<SInlineEditableTextBlock> NameText;

			TSharedRef<SWidget> Item = SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("NoBorder"))
				.Padding(2)
				.OnMouseButtonDown_Lambda([this, i](const FGeometry&, const FPointerEvent& MouseEvent) -> FReply
				{
					if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
					{
						OnFlipbookSelected(i);
						ShowFlipbookContextMenu(i);
						return FReply::Handled();
					}
					return FReply::Unhandled();
				})
				[
					SNew(SButton)
					.ButtonColorAndOpacity(bIsSelected ? FLinearColor(0.2f, 0.4f, 0.8f) : FLinearColor(0.15f, 0.15f, 0.15f))
					.OnClicked_Lambda([this, i]() { OnFlipbookSelected(i); return FReply::Handled(); })
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							[
								SAssignNew(NameText, SInlineEditableTextBlock)
								.Text(FText::FromString(Anim.FlipbookName))
								.OnTextCommitted_Lambda([this, i](const FText& NewText, ETextCommit::Type CommitType)
								{
									if (CommitType != ETextCommit::OnCleared)
									{
										RenameFlipbook(i, NewText.ToString());
									}
								})
							]
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							.Padding(4, 0, 0, 0)
							[
								SNew(STextBlock)
								.Text(FText::Format(LOCTEXT("FrameCountSuffix", "({0} frames)"), FText::AsNumber(FrameCount)))
								.Font(FAppStyle::GetFontStyle("SmallFont"))
								.ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f)))
							]
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(STextBlock)
							.Text(FText::FromString(bHasFlipbook ? Anim.Flipbook.GetAssetName() : TEXT("No Flipbook Assigned")))
							.Font(FAppStyle::GetFontStyle("SmallFont"))
							.ColorAndOpacity(bHasFlipbook ? FLinearColor(0.4f, 0.8f, 0.4f) : FLinearColor(0.6f, 0.4f, 0.4f))
						]
					]
				];

			SidebarFlipbookNameTexts.Add(i, NameText);
			return Item;
		});

		// Trigger pending rename
		if (PendingRenameFlipbookIndex != INDEX_NONE)
		{
			int32 RenameIdx = PendingRenameFlipbookIndex;
			PendingRenameFlipbookIndex = INDEX_NONE;

			if (TSharedPtr<SInlineEditableTextBlock>* FoundText = SidebarFlipbookNameTexts.Find(RenameIdx))
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
}

TSharedRef<SWidget> SCharacterDataAssetEditor::BuildFrameList()
{
	SAssignNew(FrameListBox, SVerticalBox);

	FrameListBox->AddSlot()
	.AutoHeight()
	.Padding(4)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("Frames", "Frames"))
			.Font(FAppStyle::GetFontStyle("BoldFont"))
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(2, 0)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
			.Text(LOCTEXT("PrevFrame", "<"))
			.ToolTipText(LOCTEXT("PrevFrameTooltip", "Previous frame"))
			.OnClicked_Lambda([this]() { OnPrevFrameClicked(); return FReply::Handled(); })
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
			.Text(LOCTEXT("NextFrame", ">"))
			.ToolTipText(LOCTEXT("NextFrameTooltip", "Next frame"))
			.OnClicked_Lambda([this]() { OnNextFrameClicked(); return FReply::Handled(); })
		]
	];

	RefreshFrameList();

	return SNew(SScrollBox) + SScrollBox::Slot()[FrameListBox.ToSharedRef()];
}

void SCharacterDataAssetEditor::RefreshFrameList()
{
	if (!FrameListBox.IsValid()) return;

	while (FrameListBox->NumSlots() > 1)
	{
		FrameListBox->RemoveSlot(FrameListBox->GetSlot(1).GetWidget());
	}

	const FFlipbookHitboxData* Anim = GetCurrentFlipbookData();
	if (Anim)
	{
		// Get the flipbook to extract frame sprites
		UPaperFlipbook* Flipbook = nullptr;
		if (!Anim->Flipbook.IsNull())
		{
			Flipbook = Anim->Flipbook.LoadSynchronous();
		}

		int32 FrameCount = FMath::Min(GetCurrentFrameCount(), Anim->Frames.Num());
		for (int32 i = 0; i < FrameCount; i++)
		{
			const FFrameHitboxData& Frame = Anim->Frames[i];
			bool bIsSelected = (i == SelectedFrameIndex);

			int32 AttackCount = 0, HurtCount = 0, ColCount = 0;
			for (const FHitboxData& HB : Frame.Hitboxes)
			{
				if (HB.Type == EHitboxType::Attack) AttackCount++;
				else if (HB.Type == EHitboxType::Hurtbox) HurtCount++;
				else ColCount++;
			}

			// Get sprite for this frame's thumbnail
			UPaperSprite* FrameSprite = nullptr;
			if (Flipbook && i < Flipbook->GetNumKeyFrames())
			{
				FrameSprite = Flipbook->GetKeyFrameChecked(i).Sprite;
			}

			FrameListBox->AddSlot()
			.AutoHeight()
			.Padding(1)
			[
				SNew(SButton)
				.ButtonColorAndOpacity(bIsSelected ? FLinearColor(0.2f, 0.6f, 0.3f) : FLinearColor(0.12f, 0.12f, 0.12f))
				.OnClicked_Lambda([this, i]() { OnFrameSelected(i); return FReply::Handled(); })
				[
					SNew(SHorizontalBox)
					// Sprite thumbnail
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(2)
					[
						SNew(SBox)
						.WidthOverride(32)
						.HeightOverride(32)
						[
							FrameSprite
								? StaticCastSharedRef<SWidget>(SNew(SSpriteThumbnail).Sprite(FrameSprite))
								: StaticCastSharedRef<SWidget>(SNew(SBorder)
									.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
									.HAlign(HAlign_Center)
									.VAlign(VAlign_Center)
									[
										SNew(STextBlock)
										.Text(FText::AsNumber(i))
										.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
									])
						]
					]
					// Frame number
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(4, 0, 8, 0)
					[
						SNew(STextBlock)
						.Text(FText::AsNumber(i))
						.Font(FAppStyle::GetFontStyle("BoldFont"))
					]
					// Hitbox indicators with colored dots
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.VAlign(VAlign_Center)
					[
						SNew(SHorizontalBox)
						// Attack count (red dot)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(0, 0, 6, 0)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							[
								SNew(SBox)
								.WidthOverride(8)
								.HeightOverride(8)
								[
									SNew(SImage)
									.Image(FAppStyle::GetBrush("Icons.FilledCircle"))
									.ColorAndOpacity(FLinearColor(0.9f, 0.2f, 0.2f))
								]
							]
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							.Padding(2, 0, 0, 0)
							[
								SNew(STextBlock)
								.Text(FText::AsNumber(AttackCount))
								.Font(FAppStyle::GetFontStyle("SmallFont"))
								.ColorAndOpacity(FSlateColor(FLinearColor(0.7f, 0.7f, 0.7f)))
							]
						]
						// Hurtbox count (green dot)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(0, 0, 6, 0)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							[
								SNew(SBox)
								.WidthOverride(8)
								.HeightOverride(8)
								[
									SNew(SImage)
									.Image(FAppStyle::GetBrush("Icons.FilledCircle"))
									.ColorAndOpacity(FLinearColor(0.2f, 0.8f, 0.2f))
								]
							]
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							.Padding(2, 0, 0, 0)
							[
								SNew(STextBlock)
								.Text(FText::AsNumber(HurtCount))
								.Font(FAppStyle::GetFontStyle("SmallFont"))
								.ColorAndOpacity(FSlateColor(FLinearColor(0.7f, 0.7f, 0.7f)))
							]
						]
						// Collision count (blue dot)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(0, 0, 6, 0)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							[
								SNew(SBox)
								.WidthOverride(8)
								.HeightOverride(8)
								[
									SNew(SImage)
									.Image(FAppStyle::GetBrush("Icons.FilledCircle"))
									.ColorAndOpacity(FLinearColor(0.3f, 0.5f, 0.9f))
								]
							]
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							.Padding(2, 0, 0, 0)
							[
								SNew(STextBlock)
								.Text(FText::AsNumber(ColCount))
								.Font(FAppStyle::GetFontStyle("SmallFont"))
								.ColorAndOpacity(FSlateColor(FLinearColor(0.7f, 0.7f, 0.7f)))
							]
						]
						// Socket count (yellow dot)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							[
								SNew(SBox)
								.WidthOverride(8)
								.HeightOverride(8)
								[
									SNew(SImage)
									.Image(FAppStyle::GetBrush("Icons.FilledCircle"))
									.ColorAndOpacity(FLinearColor(0.9f, 0.8f, 0.2f))
								]
							]
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							.Padding(2, 0, 0, 0)
							[
								SNew(STextBlock)
								.Text(FText::AsNumber(Frame.Sockets.Num()))
								.Font(FAppStyle::GetFontStyle("SmallFont"))
								.ColorAndOpacity(FSlateColor(FLinearColor(0.7f, 0.7f, 0.7f)))
							]
						]
					]

					// Invulnerable indicator
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(4, 0, 2, 0)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("InvulnIndicator", "I"))
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
						.ColorAndOpacity(FLinearColor(0.2f, 0.8f, 0.9f))
						.ToolTipText(LOCTEXT("InvulnIndicatorTip", "Invulnerable frame"))
						.Visibility(Frame.bInvulnerable ? EVisibility::Visible : EVisibility::Collapsed)
					]
				]
			];
		}

	}
}

TSharedRef<SWidget> SCharacterDataAssetEditor::BuildCanvasArea()
{
	TSharedRef<SVerticalBox> CanvasArea = SNew(SVerticalBox)
		// Current flipbook header
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4, 4, 4, 0)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
			.Padding(FMargin(8, 4))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("EditingFlipbook", "EDITING:"))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
					.ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f)))
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(8, 0, 0, 0)
				[
					SNew(STextBlock)
					.Text_Lambda([this]() {
						const FFlipbookHitboxData* Anim = GetCurrentFlipbookData();
						return Anim ? FText::FromString(Anim->FlipbookName) : LOCTEXT("NoFlipbookSelected", "No Flipbook Selected");
					})
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 12))
					.ColorAndOpacity_Lambda([this]() {
						const FFlipbookHitboxData* Anim = GetCurrentFlipbookData();
						return FSlateColor(Anim && !Anim->Flipbook.IsNull() ? FLinearColor::White : FLinearColor(0.8f, 0.4f, 0.4f));
					})
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(12, 0, 0, 0)
				[
					SNew(STextBlock)
					.Text_Lambda([this]() {
						int32 FrameCount = GetCurrentFrameCount();
						if (FrameCount > 0)
						{
							return FText::Format(LOCTEXT("FrameInfo", "Frame {0} of {1}"),
								FText::AsNumber(SelectedFrameIndex + 1),
								FText::AsNumber(FrameCount));
						}
						return LOCTEXT("NoFrames", "No Frames");
					})
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
					.ColorAndOpacity(FSlateColor(FLinearColor(0.7f, 0.7f, 0.7f)))
				]

				// Spacer to push 2D/3D toggle to the right
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					SNullWidget::NullWidget
				]

				// 2D/3D View Toggle
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(8, 0, 0, 0)
				[
					SNew(SHorizontalBox)
					.Visibility_Lambda([]() { return GetDefault<UPaper2DPlusSettings>()->bEnable3DDepth ? EVisibility::Visible : EVisibility::Collapsed; })
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(SCheckBox)
						.Style(FAppStyle::Get(), "ToggleButtonCheckbox")
						.IsChecked_Lambda([this]() { return bShow3DView ? ECheckBoxState::Unchecked : ECheckBoxState::Checked; })
						.OnCheckStateChanged_Lambda([this](ECheckBoxState NewState)
						{
							bShow3DView = false;
							if (CanvasViewSwitcher.IsValid())
							{
								CanvasViewSwitcher->SetActiveWidgetIndex(0);
							}
						})
						.ToolTipText(LOCTEXT("View2DTooltip", "2D View\nStandard top-down view for editing hitboxes"))
						[
							SNew(STextBlock)
							.Text(LOCTEXT("View2D", "2D"))
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
						]
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(2, 0, 0, 0)
					[
						SNew(SCheckBox)
						.Style(FAppStyle::Get(), "ToggleButtonCheckbox")
						.IsChecked_Lambda([this]() { return bShow3DView ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
						.OnCheckStateChanged_Lambda([this](ECheckBoxState NewState)
						{
							bShow3DView = true;
							if (CanvasViewSwitcher.IsValid())
							{
								CanvasViewSwitcher->SetActiveWidgetIndex(1);
							}
						})
						.ToolTipText(LOCTEXT("View3DTooltip", "3D View\nPerspective view to visualize hitbox depth (Z and Depth values)\nDrag to rotate, scroll to zoom"))
						[
							SNew(STextBlock)
							.Text(LOCTEXT("View3D", "3D"))
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
						]
					]
				]
			]
		]

		// Flipbook selector removed - flipbook assignment is now in Overview tab (T8)

		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		.Padding(4)
		[
			SAssignNew(CanvasViewSwitcher, SWidgetSwitcher)
			.WidgetIndex_Lambda([this]() { return (bShow3DView && GetDefault<UPaper2DPlusSettings>()->bEnable3DDepth) ? 1 : 0; })

			// Slot 0: 2D Canvas (default)
			+ SWidgetSwitcher::Slot()
			[
				SAssignNew(EditorCanvas, SCharacterDataEditorCanvas)
				.Asset(Asset)
				.SelectedFlipbookIndex_Lambda([this]() { return SelectedFlipbookIndex; })
				.SelectedFrameIndex_Lambda([this]() { return SelectedFrameIndex; })
				.CurrentTool_Lambda([this]() { return CurrentTool; })
				.ShowGrid_Lambda([this]() { return bShowGrid; })
				.Zoom_Lambda([this]() { return ZoomLevel; })
				.VisibilityMask_Lambda([this]() { return HitboxVisibilityMask; })
			]

			// Slot 1: 3D Viewport (Unreal's built-in viewport system)
			+ SWidgetSwitcher::Slot()
			[
				SAssignNew(Viewport3D, SHitbox3DViewport)
				.Asset(Asset)
			]
		];

	// Update 3D viewport with initial frame data
	if (Viewport3D.IsValid())
	{
		const FFrameHitboxData* Frame = GetCurrentFrame();
		Viewport3D->SetFrameData(Frame);
		Viewport3D->SetSprite(GetCurrentSprite());
	}

	if (EditorCanvas.IsValid())
	{
		EditorCanvas->OnSelectionChanged.BindLambda([this](EHitboxSelectionType Type, int32 Index)
		{
			OnSelectionChanged(Type, Index);
		});

		EditorCanvas->OnHitboxDataModified.BindLambda([this]()
		{
			OnHitboxDataModified();
		});

		EditorCanvas->OnRequestUndo.BindLambda([this]()
		{
			BeginTransaction(LOCTEXT("ModifyHitbox", "Modify Hitbox"));
		});

		EditorCanvas->OnEndTransaction.BindLambda([this]()
		{
			EndTransaction();
		});

		EditorCanvas->OnZoomChanged.BindLambda([this](float NewZoom)
		{
			OnZoomChanged(NewZoom);
		});

		EditorCanvas->OnToolChangeRequested.BindLambda([this](EHitboxEditorTool Tool)
		{
			OnToolSelected(Tool);
		});
	}

	return CanvasArea;
}

TSharedRef<SWidget> SCharacterDataAssetEditor::BuildHitboxList()
{
	SAssignNew(HitboxListBox, SVerticalBox);

	HitboxListBox->AddSlot()
	.AutoHeight()
	.Padding(4)
	[
		SNew(SVerticalBox)

		// Header row with title
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0, 0, 0, 4)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("HitboxesSockets", "Hitboxes & Sockets"))
			.Font(FAppStyle::GetFontStyle("BoldFont"))
		]

		// Action buttons row
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SHorizontalBox)

			// Add Hitbox button
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0, 0, 4, 0)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.ToolTipText(LOCTEXT("AddHitboxTooltip", "Add Hitbox\nCreate a new hitbox on this frame"))
				.OnClicked_Lambda([this]() { AddNewHitbox(); return FReply::Handled(); })
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(SImage)
						.Image(FAppStyle::GetBrush("Icons.PlusCircle"))
						.ColorAndOpacity(FLinearColor(0.3f, 0.8f, 0.3f))
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(4, 0, 0, 0)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("HitboxLabel", "Hitbox"))
						.Font(FAppStyle::GetFontStyle("SmallFont"))
					]
				]
			]

			// Add Socket button
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0, 0, 4, 0)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.ToolTipText(LOCTEXT("AddSocketTooltip", "Add Socket\nCreate a new attachment point on this frame"))
				.OnClicked_Lambda([this]() { AddNewSocket(); return FReply::Handled(); })
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(SImage)
						.Image(FAppStyle::GetBrush("Icons.Plus"))
						.ColorAndOpacity(FLinearColor(0.8f, 0.6f, 0.2f))
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(4, 0, 0, 0)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("SocketLabel", "Socket"))
						.Font(FAppStyle::GetFontStyle("SmallFont"))
					]
				]
			]

			// Spacer
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SNullWidget::NullWidget
			]

			// Delete button
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.ToolTipText(LOCTEXT("DeleteSelectedTooltip", "Delete Selected\nRemove the selected hitbox or socket"))
				.OnClicked_Lambda([this]() { DeleteSelected(); return FReply::Handled(); })
				[
					SNew(SImage)
					.Image(FAppStyle::GetBrush("Icons.Delete"))
					.ColorAndOpacity(FLinearColor(0.8f, 0.3f, 0.3f))
				]
			]
		]
	];

	RefreshHitboxList();

	return SNew(SScrollBox) + SScrollBox::Slot()[HitboxListBox.ToSharedRef()];
}

void SCharacterDataAssetEditor::RefreshHitboxList()
{
	if (!HitboxListBox.IsValid()) return;

	while (HitboxListBox->NumSlots() > 1)
	{
		HitboxListBox->RemoveSlot(HitboxListBox->GetSlot(1).GetWidget());
	}

	const FFrameHitboxData* Frame = GetCurrentFrame();
	if (!Frame) return;

	for (int32 i = 0; i < Frame->Hitboxes.Num(); i++)
	{
		const FHitboxData& HB = Frame->Hitboxes[i];
		if (!IsHitboxTypeVisible(HB.Type)) continue;

		bool bIsSelected = EditorCanvas.IsValid() &&
			EditorCanvas->GetSelectionType() == EHitboxSelectionType::Hitbox &&
			EditorCanvas->IsSelected(i);

		FString TypeStr = HB.Type == EHitboxType::Attack ? TEXT("ATK") :
						  HB.Type == EHitboxType::Hurtbox ? TEXT("HRT") : TEXT("COL");
		FLinearColor TypeColor = HB.Type == EHitboxType::Attack ? FLinearColor::Red :
								 HB.Type == EHitboxType::Hurtbox ? FLinearColor::Green : FLinearColor::Blue;

		HitboxListBox->AddSlot()
		.AutoHeight()
		.Padding(1)
		[
			SNew(SButton)
			.ButtonColorAndOpacity(bIsSelected ? TypeColor * 0.5f : FLinearColor(0.1f, 0.1f, 0.1f))
			.OnClicked_Lambda([this, i]()
			{
				if (EditorCanvas.IsValid())
				{
					if (FSlateApplication::Get().GetModifierKeys().IsShiftDown())
					{
						EditorCanvas->ToggleSelection(i);
					}
					else
					{
						EditorCanvas->SetSelection(EHitboxSelectionType::Hitbox, i);
					}
				}
				return FReply::Handled();
			})
			[
				SNew(STextBlock)
				.Text(FText::Format(LOCTEXT("HitboxListItem", "[{0}] {1} ({2},{3}) {4}x{5}"),
					FText::AsNumber(i),
					FText::FromString(TypeStr),
					FText::AsNumber(HB.X),
					FText::AsNumber(HB.Y),
					FText::AsNumber(HB.Width),
					FText::AsNumber(HB.Height)))
				.ColorAndOpacity(FSlateColor(TypeColor))
				.Font(FAppStyle::GetFontStyle("SmallFont"))
			]
		];
	}

	for (int32 i = 0; i < Frame->Sockets.Num(); i++)
	{
		const FSocketData& Sock = Frame->Sockets[i];
		bool bIsSelected = EditorCanvas.IsValid() &&
			EditorCanvas->GetSelectionType() == EHitboxSelectionType::Socket &&
			EditorCanvas->IsSelected(i);

		HitboxListBox->AddSlot()
		.AutoHeight()
		.Padding(1)
		[
			SNew(SButton)
			.ButtonColorAndOpacity(bIsSelected ? FLinearColor(0.4f, 0.4f, 0.0f) : FLinearColor(0.1f, 0.1f, 0.1f))
			.OnClicked_Lambda([this, i]()
			{
				if (EditorCanvas.IsValid())
				{
					EditorCanvas->SetSelection(EHitboxSelectionType::Socket, i);
				}
				return FReply::Handled();
			})
			[
				SNew(STextBlock)
				.Text(FText::Format(LOCTEXT("SocketListItem", "[S] {0} ({1},{2})"),
					FText::FromString(Sock.Name),
					FText::AsNumber(Sock.X),
					FText::AsNumber(Sock.Y)))
				.ColorAndOpacity(FSlateColor(FLinearColor::Yellow))
				.Font(FAppStyle::GetFontStyle("SmallFont"))
			]
		];
	}
}

TSharedRef<SWidget> SCharacterDataAssetEditor::BuildPropertiesPanel()
{
	SAssignNew(PropertiesBox, SVerticalBox);

	PropertiesBox->AddSlot()
	.AutoHeight()
	.Padding(4)
	[
		SNew(STextBlock)
		.Text(LOCTEXT("Properties", "Properties"))
		.Font(FAppStyle::GetFontStyle("BoldFont"))
	];

	RefreshPropertiesPanel();

	return SNew(SScrollBox) + SScrollBox::Slot()[PropertiesBox.ToSharedRef()];
}

void SCharacterDataAssetEditor::RefreshPropertiesPanel()
{
	if (!PropertiesBox.IsValid()) return;

	while (PropertiesBox->NumSlots() > 1)
	{
		PropertiesBox->RemoveSlot(PropertiesBox->GetSlot(1).GetWidget());
	}

	if (!EditorCanvas.IsValid()) return;

	EHitboxSelectionType SelType = EditorCanvas->GetSelectionType();
	TArray<int32> SelIndices = EditorCanvas->GetSelectedIndices();
	int32 SelIndex = EditorCanvas->GetPrimarySelectedIndex();

	FFrameHitboxData* Frame = GetCurrentFrameMutable();
	if (!Frame) return;

	// Frame-level: Invulnerable checkbox (always visible when a frame is selected)
	PropertiesBox->AddSlot()
	.AutoHeight()
	.Padding(4, 2)
	[
		SNew(SCheckBox)
		.IsChecked_Lambda([this]()
		{
			const FFrameHitboxData* F = GetCurrentFrameMutable();
			return (F && F->bInvulnerable) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
		})
		.OnCheckStateChanged_Lambda([this](ECheckBoxState State)
		{
			if (FFrameHitboxData* F = GetCurrentFrameMutable())
			{
				BeginTransaction(LOCTEXT("ToggleInvulnerable", "Toggle Invulnerable Frame"));
				F->bInvulnerable = (State == ECheckBoxState::Checked);
				EndTransaction();
				RefreshFrameList();
			}
		})
		[
			SNew(STextBlock)
			.Text(LOCTEXT("InvulnerableLabel", "Invulnerable Frame (i-frame)"))
			.ToolTipText(LOCTEXT("InvulnerableTip", "When checked, the character is invulnerable on this frame. Readable via IsFrameInvulnerable() in Blueprints."))
		]
	];

	if (SelType == EHitboxSelectionType::None || SelIndices.Num() == 0)
	{
		return;
	}

	PropertiesBox->AddSlot()
	.AutoHeight()
	.Padding(4, 0, 4, 4)
	[
		SNew(SSeparator)
	];

	// Show multi-select summary when multiple hitboxes are selected
	if (SelType == EHitboxSelectionType::Hitbox && SelIndices.Num() > 1)
	{
		PropertiesBox->AddSlot()
		.AutoHeight()
		.Padding(4, 2)
		[
			SNew(STextBlock)
			.Text(FText::Format(LOCTEXT("MultiSelectInfo", "{0} hitboxes selected"), FText::AsNumber(SelIndices.Num())))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
		];

		PropertiesBox->AddSlot()
		.AutoHeight()
		.Padding(4, 2)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("MultiSelectHint", "Move/Delete selected hitboxes as a group.\nUse arrow keys to nudge, Delete to remove all."))
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
			.ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f)))
		];

		return;
	}

	if (SelType == EHitboxSelectionType::Hitbox && Frame->Hitboxes.IsValidIndex(SelIndex))
	{
		// Type selector
		PropertiesBox->AddSlot()
		.AutoHeight()
		.Padding(4, 2)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0, 0, 8, 0)
			[
				SNew(STextBlock).Text(LOCTEXT("TypeLabel", "Type:"))
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					SNew(SButton)
					.Text(LOCTEXT("AttackType", "Attack"))
					.ButtonColorAndOpacity_Lambda([this, SelIndex]()
					{
						FFrameHitboxData* F = GetCurrentFrameMutable();
						return (F && F->Hitboxes.IsValidIndex(SelIndex) && F->Hitboxes[SelIndex].Type == EHitboxType::Attack)
							? FLinearColor::Red * 0.5f : FLinearColor(0.15f, 0.15f, 0.15f);
					})
					.OnClicked_Lambda([this, SelIndex]()
					{
						if (FFrameHitboxData* F = GetCurrentFrameMutable())
						{
							if (F->Hitboxes.IsValidIndex(SelIndex))
							{
								BeginTransaction(LOCTEXT("ChangeType", "Change Hitbox Type"));
								F->Hitboxes[SelIndex].Type = EHitboxType::Attack;
								EndTransaction();
								RefreshHitboxList();
							}
						}
						return FReply::Handled();
					})
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					SNew(SButton)
					.Text(LOCTEXT("HurtboxType", "Hurtbox"))
					.ButtonColorAndOpacity_Lambda([this, SelIndex]()
					{
						FFrameHitboxData* F = GetCurrentFrameMutable();
						return (F && F->Hitboxes.IsValidIndex(SelIndex) && F->Hitboxes[SelIndex].Type == EHitboxType::Hurtbox)
							? FLinearColor::Green * 0.5f : FLinearColor(0.15f, 0.15f, 0.15f);
					})
					.OnClicked_Lambda([this, SelIndex]()
					{
						if (FFrameHitboxData* F = GetCurrentFrameMutable())
						{
							if (F->Hitboxes.IsValidIndex(SelIndex))
							{
								BeginTransaction(LOCTEXT("ChangeType", "Change Hitbox Type"));
								F->Hitboxes[SelIndex].Type = EHitboxType::Hurtbox;
								EndTransaction();
								RefreshHitboxList();
							}
						}
						return FReply::Handled();
					})
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					SNew(SButton)
					.Text(LOCTEXT("CollisionType", "Collision"))
					.ButtonColorAndOpacity_Lambda([this, SelIndex]()
					{
						FFrameHitboxData* F = GetCurrentFrameMutable();
						return (F && F->Hitboxes.IsValidIndex(SelIndex) && F->Hitboxes[SelIndex].Type == EHitboxType::Collision)
							? FLinearColor::Blue * 0.5f : FLinearColor(0.15f, 0.15f, 0.15f);
					})
					.OnClicked_Lambda([this, SelIndex]()
					{
						if (FFrameHitboxData* F = GetCurrentFrameMutable())
						{
							if (F->Hitboxes.IsValidIndex(SelIndex))
							{
								BeginTransaction(LOCTEXT("ChangeType", "Change Hitbox Type"));
								F->Hitboxes[SelIndex].Type = EHitboxType::Collision;
								EndTransaction();
								RefreshHitboxList();
							}
						}
						return FReply::Handled();
					})
				]
			]
		];

		// Position
		PropertiesBox->AddSlot()
		.AutoHeight()
		.Padding(4, 2)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0, 0, 8, 0)
			[
				SNew(STextBlock).Text(LOCTEXT("PosLabel", "Pos:"))
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SNew(SSpinBox<int32>)
				.MinValue(-9999).MaxValue(9999)
				.MinSliderValue(-500).MaxSliderValue(500)
				.Delta(1)
				.SliderExponent(1.0f)
				.Value_Lambda([this, SelIndex]() {
					FFrameHitboxData* F = GetCurrentFrameMutable();
					return (F && F->Hitboxes.IsValidIndex(SelIndex)) ? F->Hitboxes[SelIndex].X : 0;
				})
				.OnValueCommitted_Lambda([this, SelIndex](int32 Val, ETextCommit::Type) {
					if (FFrameHitboxData* F = GetCurrentFrameMutable())
					{
						if (F->Hitboxes.IsValidIndex(SelIndex))
						{
							BeginTransaction(LOCTEXT("MoveHitbox", "Move Hitbox"));
							F->Hitboxes[SelIndex].X = Val;
							EndTransaction();
						}
					}
				})
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SNew(SSpinBox<int32>)
				.MinValue(-9999).MaxValue(9999)
				.MinSliderValue(-500).MaxSliderValue(500)
				.Delta(1)
				.SliderExponent(1.0f)
				.Value_Lambda([this, SelIndex]() {
					FFrameHitboxData* F = GetCurrentFrameMutable();
					return (F && F->Hitboxes.IsValidIndex(SelIndex)) ? F->Hitboxes[SelIndex].Y : 0;
				})
				.OnValueCommitted_Lambda([this, SelIndex](int32 Val, ETextCommit::Type) {
					if (FFrameHitboxData* F = GetCurrentFrameMutable())
					{
						if (F->Hitboxes.IsValidIndex(SelIndex))
						{
							BeginTransaction(LOCTEXT("MoveHitbox", "Move Hitbox"));
							F->Hitboxes[SelIndex].Y = Val;
							EndTransaction();
						}
					}
				})
			]
		];

		// Size
		PropertiesBox->AddSlot()
		.AutoHeight()
		.Padding(4, 2)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0, 0, 8, 0)
			[
				SNew(STextBlock).Text(LOCTEXT("SizeLabel", "Size:"))
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SNew(SSpinBox<int32>)
				.MinValue(1).MaxValue(9999)
				.MinSliderValue(1).MaxSliderValue(500)
				.Delta(1)
				.SliderExponent(1.0f)
				.Value_Lambda([this, SelIndex]() {
					FFrameHitboxData* F = GetCurrentFrameMutable();
					return (F && F->Hitboxes.IsValidIndex(SelIndex)) ? F->Hitboxes[SelIndex].Width : 16;
				})
				.OnValueCommitted_Lambda([this, SelIndex](int32 Val, ETextCommit::Type) {
					if (FFrameHitboxData* F = GetCurrentFrameMutable())
					{
						if (F->Hitboxes.IsValidIndex(SelIndex))
						{
							BeginTransaction(LOCTEXT("ResizeHitbox", "Resize Hitbox"));
							F->Hitboxes[SelIndex].Width = Val;
							EndTransaction();
						}
					}
				})
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SNew(SSpinBox<int32>)
				.MinValue(1).MaxValue(9999)
				.MinSliderValue(1).MaxSliderValue(500)
				.Delta(1)
				.SliderExponent(1.0f)
				.Value_Lambda([this, SelIndex]() {
					FFrameHitboxData* F = GetCurrentFrameMutable();
					return (F && F->Hitboxes.IsValidIndex(SelIndex)) ? F->Hitboxes[SelIndex].Height : 16;
				})
				.OnValueCommitted_Lambda([this, SelIndex](int32 Val, ETextCommit::Type) {
					if (FFrameHitboxData* F = GetCurrentFrameMutable())
					{
						if (F->Hitboxes.IsValidIndex(SelIndex))
						{
							BeginTransaction(LOCTEXT("ResizeHitbox", "Resize Hitbox"));
							F->Hitboxes[SelIndex].Height = Val;
							EndTransaction();
						}
					}
				})
			]
		];

		// Z Position and Depth - only shown when 3D Depth is enabled in project settings
		if (GetDefault<UPaper2DPlusSettings>()->bEnable3DDepth)
		{
			// Z Position (Depth offset)
			PropertiesBox->AddSlot()
			.AutoHeight()
			.Padding(4, 2)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0, 0, 8, 0)
				[
					SNew(STextBlock).Text(LOCTEXT("ZPosLabel", "Z Pos:"))
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					SNew(SSpinBox<int32>)
					.MinValue(-9999).MaxValue(9999)
					.MinSliderValue(-200).MaxSliderValue(200)
					.Delta(1)
					.SliderExponent(1.0f)
					.ToolTipText(LOCTEXT("ZPosTooltip", "Z position (depth offset) for 2.5D collision"))
					.Value_Lambda([this, SelIndex]() {
						FFrameHitboxData* F = GetCurrentFrameMutable();
						return (F && F->Hitboxes.IsValidIndex(SelIndex)) ? F->Hitboxes[SelIndex].Z : 0;
					})
					.OnValueCommitted_Lambda([this, SelIndex](int32 Val, ETextCommit::Type) {
						if (FFrameHitboxData* F = GetCurrentFrameMutable())
						{
							if (F->Hitboxes.IsValidIndex(SelIndex))
							{
								BeginTransaction(LOCTEXT("ChangeZPos", "Change Z Position"));
								F->Hitboxes[SelIndex].Z = Val;
								EndTransaction();
							}
						}
					})
				]
			];

			// Depth (thickness in Z)
			PropertiesBox->AddSlot()
			.AutoHeight()
			.Padding(4, 2)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0, 0, 8, 0)
				[
					SNew(STextBlock).Text(LOCTEXT("DepthLabel", "Depth:"))
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					SNew(SSpinBox<int32>)
					.MinValue(0).MaxValue(9999)
					.MinSliderValue(0).MaxSliderValue(200)
					.Delta(1)
					.SliderExponent(1.0f)
					.ToolTipText(LOCTEXT("DepthTooltip", "Depth (thickness in Z axis) for 2.5D collision. 0 = use default."))
					.Value_Lambda([this, SelIndex]() {
						FFrameHitboxData* F = GetCurrentFrameMutable();
						return (F && F->Hitboxes.IsValidIndex(SelIndex)) ? F->Hitboxes[SelIndex].Depth : 0;
					})
					.OnValueCommitted_Lambda([this, SelIndex](int32 Val, ETextCommit::Type) {
						if (FFrameHitboxData* F = GetCurrentFrameMutable())
						{
							if (F->Hitboxes.IsValidIndex(SelIndex))
							{
								BeginTransaction(LOCTEXT("ChangeDepth", "Change Depth"));
								F->Hitboxes[SelIndex].Depth = Val;
								EndTransaction();
							}
						}
					})
				]
			];
		}

		// Damage
		PropertiesBox->AddSlot()
		.AutoHeight()
		.Padding(4, 2)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0, 0, 8, 0)
			[
				SNew(STextBlock).Text(LOCTEXT("DamageLabel", "Damage:"))
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SNew(SSpinBox<int32>)
				.MinValue(0).MaxValue(9999)
				.MinSliderValue(0).MaxSliderValue(200)
				.Delta(1)
				.SliderExponent(1.0f)
				.Value_Lambda([this, SelIndex]() {
					FFrameHitboxData* F = GetCurrentFrameMutable();
					return (F && F->Hitboxes.IsValidIndex(SelIndex)) ? F->Hitboxes[SelIndex].Damage : 0;
				})
				.OnValueCommitted_Lambda([this, SelIndex](int32 Val, ETextCommit::Type) {
					if (FFrameHitboxData* F = GetCurrentFrameMutable())
					{
						if (F->Hitboxes.IsValidIndex(SelIndex))
						{
							BeginTransaction(LOCTEXT("ChangeDamage", "Change Damage"));
							F->Hitboxes[SelIndex].Damage = Val;
							EndTransaction();
						}
					}
				})
			]
		];

		// Knockback
		PropertiesBox->AddSlot()
		.AutoHeight()
		.Padding(4, 2)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0, 0, 8, 0)
			[
				SNew(STextBlock).Text(LOCTEXT("KnockbackLabel", "Knockback:"))
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SNew(SSpinBox<int32>)
				.MinValue(0).MaxValue(9999)
				.MinSliderValue(0).MaxSliderValue(200)
				.Delta(1)
				.SliderExponent(1.0f)
				.Value_Lambda([this, SelIndex]() {
					FFrameHitboxData* F = GetCurrentFrameMutable();
					return (F && F->Hitboxes.IsValidIndex(SelIndex)) ? F->Hitboxes[SelIndex].Knockback : 0;
				})
				.OnValueCommitted_Lambda([this, SelIndex](int32 Val, ETextCommit::Type) {
					if (FFrameHitboxData* F = GetCurrentFrameMutable())
					{
						if (F->Hitboxes.IsValidIndex(SelIndex))
						{
							BeginTransaction(LOCTEXT("ChangeKnockback", "Change Knockback"));
							F->Hitboxes[SelIndex].Knockback = Val;
							EndTransaction();
						}
					}
				})
			]
		];
	}
	else if (SelType == EHitboxSelectionType::Socket && Frame->Sockets.IsValidIndex(SelIndex))
	{
		// Name
		PropertiesBox->AddSlot()
		.AutoHeight()
		.Padding(4, 2)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0, 0, 8, 0)
			[
				SNew(STextBlock).Text(LOCTEXT("NameLabel", "Name:"))
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SNew(SEditableTextBox)
				.Text_Lambda([this, SelIndex]() {
					FFrameHitboxData* F = GetCurrentFrameMutable();
					return (F && F->Sockets.IsValidIndex(SelIndex)) ? FText::FromString(F->Sockets[SelIndex].Name) : FText();
				})
				.OnTextCommitted_Lambda([this, SelIndex](const FText& Text, ETextCommit::Type) {
					if (FFrameHitboxData* F = GetCurrentFrameMutable())
					{
						if (F->Sockets.IsValidIndex(SelIndex))
						{
							BeginTransaction(LOCTEXT("RenameSocket", "Rename Socket"));
							F->Sockets[SelIndex].Name = Text.ToString();
							EndTransaction();
							RefreshHitboxList();
						}
					}
				})
			]
		];

		// Position
		PropertiesBox->AddSlot()
		.AutoHeight()
		.Padding(4, 2)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0, 0, 8, 0)
			[
				SNew(STextBlock).Text(LOCTEXT("PosLabel", "Pos:"))
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SNew(SSpinBox<int32>)
				.MinValue(-9999).MaxValue(9999)
				.MinSliderValue(-500).MaxSliderValue(500)
				.Delta(1)
				.SliderExponent(1.0f)
				.LinearDeltaSensitivity(1)
				.Value_Lambda([this, SelIndex]() {
					FFrameHitboxData* F = GetCurrentFrameMutable();
					return (F && F->Sockets.IsValidIndex(SelIndex)) ? F->Sockets[SelIndex].X : 0;
				})
				.OnValueCommitted_Lambda([this, SelIndex](int32 Val, ETextCommit::Type) {
					if (FFrameHitboxData* F = GetCurrentFrameMutable())
					{
						if (F->Sockets.IsValidIndex(SelIndex))
						{
							BeginTransaction(LOCTEXT("MoveSocket", "Move Socket"));
							F->Sockets[SelIndex].X = Val;
							EndTransaction();
						}
					}
				})
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SNew(SSpinBox<int32>)
				.MinValue(-9999).MaxValue(9999)
				.MinSliderValue(-500).MaxSliderValue(500)
				.Delta(1)
				.SliderExponent(1.0f)
				.LinearDeltaSensitivity(1)
				.Value_Lambda([this, SelIndex]() {
					FFrameHitboxData* F = GetCurrentFrameMutable();
					return (F && F->Sockets.IsValidIndex(SelIndex)) ? F->Sockets[SelIndex].Y : 0;
				})
				.OnValueCommitted_Lambda([this, SelIndex](int32 Val, ETextCommit::Type) {
					if (FFrameHitboxData* F = GetCurrentFrameMutable())
					{
						if (F->Sockets.IsValidIndex(SelIndex))
						{
							BeginTransaction(LOCTEXT("MoveSocket", "Move Socket"));
							F->Sockets[SelIndex].Y = Val;
							EndTransaction();
						}
					}
				})
			]
		];
	}
}

TSharedRef<SWidget> SCharacterDataAssetEditor::BuildCopyOperationsPanel()
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4, 4, 4, 8)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("CopyOperations", "Frame Operations"))
			.Font(FAppStyle::GetFontStyle("BoldFont"))
		]

		// Copy from Previous
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(2)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
			.ToolTipText(LOCTEXT("CopyFromPrevTooltip", "Copy hitboxes and sockets from the previous frame to this frame"))
			.OnClicked_Lambda([this]() { OnCopyFromPrevious(); return FReply::Handled(); })
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(4, 2)
				[
					SNew(SImage)
					.Image(FAppStyle::GetBrush("Icons.Import"))
					.ColorAndOpacity(FLinearColor(0.6f, 0.8f, 1.0f))
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				.Padding(4, 2)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("CopyFromPrevShort", "Copy from Previous"))
				]
			]
		]

		// Copy to All Frames
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(2)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
			.ToolTipText(LOCTEXT("CopyToAllTooltip", "Copy all hitboxes and sockets from this frame to all other frames in this flipbook"))
			.OnClicked_Lambda([this]() { OnPropagateAllToGroup(); return FReply::Handled(); })
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(4, 2)
				[
					SNew(SImage)
					.Image(FAppStyle::GetBrush("Icons.Duplicate"))
					.ColorAndOpacity(FLinearColor(0.8f, 0.8f, 0.5f))
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				.Padding(4, 2)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("CopyToAllShort", "Copy to All Frames"))
				]
			]
		]

		// Copy Selected to All Frames
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(2)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
			.ToolTipText(LOCTEXT("CopySelectedToAllTooltip", "Copy the selected hitbox/socket from this frame to all other frames in this flipbook"))
			.OnClicked_Lambda([this]() { OnPropagateSelectedToGroup(); return FReply::Handled(); })
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(4, 2)
				[
					SNew(SImage)
					.Image(FAppStyle::GetBrush("Icons.SelectInViewport"))
					.ColorAndOpacity(FLinearColor(0.5f, 0.8f, 0.5f))
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				.Padding(4, 2)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("CopySelectedShort", "Copy Selected to All"))
				]
			]
		]

		// Copy current frame to next frames (batch range)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(2)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
			.ToolTipText(LOCTEXT("CopyToNextFramesTooltip", "Copy current frame hitboxes/sockets to all subsequent frames in this flipbook"))
			.OnClicked_Lambda([this]() { OnCopyToNextFrames(); return FReply::Handled(); })
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(4, 2)
				[
					SNew(SImage)
					.Image(FAppStyle::GetBrush("Icons.Copy"))
					.ColorAndOpacity(FLinearColor(0.65f, 0.85f, 1.0f))
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				.Padding(4, 2)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("CopyToNextFramesShort", "Copy to Next Frames"))
				]
			]
		]

		// Mirror hitboxes across all frames (batch)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(2)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
			.ToolTipText(LOCTEXT("MirrorAllFramesTooltip", "Mirror all hitboxes across every frame in this flipbook using sprite center as pivot"))
			.OnClicked_Lambda([this]() { OnMirrorAllFrames(); return FReply::Handled(); })
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(4, 2)
				[
					SNew(SImage)
					.Image(FAppStyle::GetBrush("Icons.Refresh"))
					.ColorAndOpacity(FLinearColor(0.7f, 1.0f, 0.7f))
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				.Padding(4, 2)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("MirrorAllFramesShort", "Mirror Hitboxes All Frames"))
				]
			]
		]

		// Batch Damage / Knockback setter
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(2, 8, 2, 2)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
			.Padding(6)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0, 0, 0, 4)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("BatchDamageHeader", "Batch Damage / Knockback"))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0, 2)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 6, 0)
					[
						SNew(STextBlock).Text(LOCTEXT("BatchDmgLabel", "Dmg"))
					]
					+ SHorizontalBox::Slot().FillWidth(1.0f)
					[
						SNew(SSpinBox<int32>)
						.MinValue(0).MaxValue(9999)
						.MinSliderValue(0).MaxSliderValue(200)
						.Delta(1)
						.Value_Lambda([this]() { return BatchDamageValue; })
						.OnValueChanged_Lambda([this](int32 V) { BatchDamageValue = V; })
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8, 0, 6, 0)
					[
						SNew(STextBlock).Text(LOCTEXT("BatchKBLabel", "KB"))
					]
					+ SHorizontalBox::Slot().FillWidth(1.0f)
					[
						SNew(SSpinBox<int32>)
						.MinValue(0).MaxValue(9999)
						.MinSliderValue(0).MaxSliderValue(200)
						.Delta(1)
						.Value_Lambda([this]() { return BatchKnockbackValue; })
						.OnValueChanged_Lambda([this](int32 V) { BatchKnockbackValue = V; })
					]
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0, 4, 0, 0)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0, 0, 4, 0)
					[
						SNew(SButton)
						.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
						.Text(LOCTEXT("ApplyToFrame", "Apply to Frame"))
						.ToolTipText(LOCTEXT("ApplyDmgFrameTip", "Set damage and knockback on all attack hitboxes in the current frame."))
						.OnClicked_Lambda([this]()
						{
							FFrameHitboxData* F = GetCurrentFrameMutable();
							if (!F) return FReply::Handled();
							BeginTransaction(LOCTEXT("BatchDmgFrame", "Batch Set Damage (Frame)"));
							for (FHitboxData& HB : F->Hitboxes)
							{
								if (HB.Type == EHitboxType::Attack)
								{
									HB.Damage = BatchDamageValue;
									HB.Knockback = BatchKnockbackValue;
								}
							}
							EndTransaction();
							RefreshPropertiesPanel();
							return FReply::Handled();
						})
					]
					+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(4, 0, 0, 0)
					[
						SNew(SButton)
						.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
						.Text(LOCTEXT("ApplyToAllFrames", "Apply to All Frames"))
						.ToolTipText(LOCTEXT("ApplyDmgAllTip", "Set damage and knockback on all attack hitboxes across every frame in this flipbook."))
						.OnClicked_Lambda([this]()
						{
							FFlipbookHitboxData* Anim = GetCurrentFlipbookDataMutable();
							if (!Anim) return FReply::Handled();
							BeginTransaction(LOCTEXT("BatchDmgAll", "Batch Set Damage (All Frames)"));
							for (FFrameHitboxData& Frame : Anim->Frames)
							{
								for (FHitboxData& HB : Frame.Hitboxes)
								{
									if (HB.Type == EHitboxType::Attack)
									{
										HB.Damage = BatchDamageValue;
										HB.Knockback = BatchKnockbackValue;
									}
								}
							}
							EndTransaction();
							RefreshPropertiesPanel();
							return FReply::Handled();
						})
					]
				]
			]
		]

		// Clear Frame
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(2, 8, 2, 2)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
			.ToolTipText(LOCTEXT("ClearFrameTooltip", "Remove all hitboxes and sockets from this frame"))
			.OnClicked_Lambda([this]() { OnClearCurrentFrame(); return FReply::Handled(); })
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(4, 2)
				[
					SNew(SImage)
					.Image(FAppStyle::GetBrush("Icons.Delete"))
					.ColorAndOpacity(FLinearColor(1.0f, 0.5f, 0.5f))
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				.Padding(4, 2)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("ClearFrameShort", "Clear Frame"))
				]
			]
		];
}

void SCharacterDataAssetEditor::OnCopyFromPrevious()
{
	if (SelectedFrameIndex <= 0) return;

	FFlipbookHitboxData* Anim = GetCurrentFlipbookDataMutable();
	if (!Anim) return;

	if (!Anim->Frames.IsValidIndex(SelectedFrameIndex) || !Anim->Frames.IsValidIndex(SelectedFrameIndex - 1)) return;

	BeginTransaction(LOCTEXT("CopyFromPrev", "Copy from Previous Frame"));

	const FFrameHitboxData& PrevFrame = Anim->Frames[SelectedFrameIndex - 1];
	FFrameHitboxData& CurrentFrame = Anim->Frames[SelectedFrameIndex];

	CurrentFrame.Hitboxes = PrevFrame.Hitboxes;
	CurrentFrame.Sockets = PrevFrame.Sockets;

	EndTransaction();
	RefreshHitboxList();
}

void SCharacterDataAssetEditor::OnPropagateAllToGroup()
{
	FFlipbookHitboxData* Anim = GetCurrentFlipbookDataMutable();
	if (!Anim) return;

	if (SelectedFrameIndex < 0 || SelectedFrameIndex >= Anim->Frames.Num()) return;

	// Copy source data before iterating — writing to Anim->Frames may invalidate pointers into the same array
	FFrameHitboxData SourceCopy = Anim->Frames[SelectedFrameIndex];

	BeginTransaction(LOCTEXT("PropagateAll", "Propagate All to Group"));

	for (int32 i = 0; i < Anim->Frames.Num(); i++)
	{
		if (i != SelectedFrameIndex)
		{
			Anim->Frames[i].Hitboxes = SourceCopy.Hitboxes;
			Anim->Frames[i].Sockets = SourceCopy.Sockets;
		}
	}

	EndTransaction();
}

void SCharacterDataAssetEditor::OnPropagateSelectedToGroup()
{
	if (!EditorCanvas.IsValid()) return;

	FFlipbookHitboxData* Anim = GetCurrentFlipbookDataMutable();
	if (!Anim) return;

	FFrameHitboxData* CurrentFrame = GetCurrentFrameMutable();
	if (!CurrentFrame) return;

	EHitboxSelectionType SelType = EditorCanvas->GetSelectionType();
	int32 SelIndex = EditorCanvas->GetPrimarySelectedIndex();

	if (SelType == EHitboxSelectionType::None) return;

	BeginTransaction(LOCTEXT("PropagateSelected", "Propagate Selected to Group"));

	if (SelType == EHitboxSelectionType::Hitbox && CurrentFrame->Hitboxes.IsValidIndex(SelIndex))
	{
		const FHitboxData& SelectedHitbox = CurrentFrame->Hitboxes[SelIndex];
		for (int32 i = 0; i < Anim->Frames.Num(); i++)
		{
			if (i != SelectedFrameIndex)
			{
				if (Anim->Frames[i].Hitboxes.IsValidIndex(SelIndex))
				{
					Anim->Frames[i].Hitboxes[SelIndex] = SelectedHitbox;
				}
				else
				{
					Anim->Frames[i].Hitboxes.Add(SelectedHitbox);
				}
			}
		}
	}
	else if (SelType == EHitboxSelectionType::Socket && CurrentFrame->Sockets.IsValidIndex(SelIndex))
	{
		const FSocketData& SelectedSocket = CurrentFrame->Sockets[SelIndex];
		for (int32 i = 0; i < Anim->Frames.Num(); i++)
		{
			if (i != SelectedFrameIndex)
			{
				bool bFound = false;
				for (FSocketData& Sock : Anim->Frames[i].Sockets)
				{
					if (Sock.Name == SelectedSocket.Name)
					{
						Sock = SelectedSocket;
						bFound = true;
						break;
					}
				}
				if (!bFound)
				{
					Anim->Frames[i].Sockets.Add(SelectedSocket);
				}
			}
		}
	}

	EndTransaction();
}

void SCharacterDataAssetEditor::OnCopyToNextFrames()
{
	if (!Asset.IsValid()) return;

	FFlipbookHitboxData* Anim = GetCurrentFlipbookDataMutable();
	if (!Anim || Anim->Frames.Num() <= 1) return;
	if (!Anim->Frames.IsValidIndex(SelectedFrameIndex)) return;
	if (SelectedFrameIndex >= Anim->Frames.Num() - 1) return;

	BeginTransaction(LOCTEXT("CopyToNextFrames", "Copy Frame Data to Next Frames"));
	const bool bCopied = Asset->CopyFrameDataToRange(
		Anim->FlipbookName,
		SelectedFrameIndex,
		SelectedFrameIndex + 1,
		Anim->Frames.Num() - 1,
		true
	);
	EndTransaction();

	if (bCopied)
	{
		RefreshHitboxList();
		RefreshPropertiesPanel();
	}
}

void SCharacterDataAssetEditor::OnMirrorAllFrames()
{
	if (!Asset.IsValid()) return;

	FFlipbookHitboxData* Anim = GetCurrentFlipbookDataMutable();
	if (!Anim || Anim->Frames.Num() == 0) return;

	UPaperFlipbook* FB = nullptr;
	if (!Anim->Flipbook.IsNull())
	{
		FB = Anim->Flipbook.LoadSynchronous();
	}

	BeginTransaction(LOCTEXT("MirrorAllFrames", "Mirror Hitboxes Across All Frames"));
	int32 Mirrored = 0;
	for (int32 i = 0; i < Anim->Frames.Num(); ++i)
	{
		int32 SpriteWidth = 0;
		if (FB && i < FB->GetNumKeyFrames())
		{
			if (UPaperSprite* Spr = FB->GetKeyFrameChecked(i).Sprite)
			{
				SpriteWidth = FMath::RoundToInt(Spr->GetSourceSize().X);
			}
		}
		Mirrored += Asset->MirrorHitboxesInRange(Anim->FlipbookName, i, i, SpriteWidth / 2);
	}
	EndTransaction();

	if (Mirrored > 0)
	{
		RefreshHitboxList();
		RefreshPropertiesPanel();
	}
}


void SCharacterDataAssetEditor::OnClearCurrentFrame()
{
	FFrameHitboxData* Frame = GetCurrentFrameMutable();
	if (!Frame) return;

	BeginTransaction(LOCTEXT("ClearFrame", "Clear Frame"));

	Frame->Hitboxes.Empty();
	Frame->Sockets.Empty();

	if (EditorCanvas.IsValid())
	{
		EditorCanvas->ClearSelection();
	}

	EndTransaction();
	RefreshHitboxList();
	RefreshPropertiesPanel();
}

void SCharacterDataAssetEditor::AddNewHitbox()
{
	FFrameHitboxData* Frame = GetCurrentFrameMutable();
	if (!Frame) return;

	BeginTransaction(LOCTEXT("AddHitbox", "Add Hitbox"));

	FHitboxData NewHitbox;
	NewHitbox.Type = EHitboxType::Hurtbox;
	NewHitbox.Damage = 0;
	NewHitbox.Knockback = 0;

	// Size and position based on sprite dimensions, grid-snapped
	FVector2D SpriteDims(128.0f, 128.0f);
	if (EditorCanvas.IsValid())
	{
		SpriteDims = EditorCanvas->GetSpriteDimensions();
	}
	auto SnapVal = [](int32 Val) { return FMath::Max(16, (FMath::RoundToInt((float)Val / 16) * 16)); };
	int32 DefaultW = SnapVal(FMath::RoundToInt(SpriteDims.X * 0.25f));
	int32 DefaultH = SnapVal(FMath::RoundToInt(SpriteDims.Y * 0.25f));
	NewHitbox.Width = DefaultW;
	NewHitbox.Height = DefaultH;
	NewHitbox.X = FMath::RoundToInt((SpriteDims.X - DefaultW) * 0.5f / 16) * 16;
	NewHitbox.Y = FMath::RoundToInt((SpriteDims.Y - DefaultH) * 0.5f / 16) * 16;

	int32 NewIndex = Frame->Hitboxes.Add(NewHitbox);

	EndTransaction();

	if (EditorCanvas.IsValid())
	{
		EditorCanvas->SetSelection(EHitboxSelectionType::Hitbox, NewIndex);
	}

	RefreshHitboxList();
	RefreshPropertiesPanel();
}

void SCharacterDataAssetEditor::AddNewSocket()
{
	FFrameHitboxData* Frame = GetCurrentFrameMutable();
	if (!Frame) return;

	BeginTransaction(LOCTEXT("AddSocket", "Add Socket"));

	FSocketData NewSocket;
	NewSocket.Name = FString::Printf(TEXT("Socket%d"), Frame->Sockets.Num());

	// Center in sprite bounds, grid-snapped
	FVector2D SpriteDims(128.0f, 128.0f);
	if (EditorCanvas.IsValid())
	{
		SpriteDims = EditorCanvas->GetSpriteDimensions();
	}
	NewSocket.X = FMath::RoundToInt(SpriteDims.X * 0.5f / 16) * 16;
	NewSocket.Y = FMath::RoundToInt(SpriteDims.Y * 0.5f / 16) * 16;

	int32 NewIndex = Frame->Sockets.Add(NewSocket);

	EndTransaction();

	if (EditorCanvas.IsValid())
	{
		EditorCanvas->SetSelection(EHitboxSelectionType::Socket, NewIndex);
	}

	RefreshHitboxList();
	RefreshPropertiesPanel();
}

void SCharacterDataAssetEditor::DeleteSelected()
{
	if (EditorCanvas.IsValid())
	{
		EditorCanvas->DeleteSelection();
	}
}

#undef LOCTEXT_NAMESPACE
