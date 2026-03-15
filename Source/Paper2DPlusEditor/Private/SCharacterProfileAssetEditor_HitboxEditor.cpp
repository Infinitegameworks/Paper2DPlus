// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "CharacterProfileAssetEditor.h"
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
#include "Widgets/SToolTip.h"
#include "UnrealClient.h"
#include "SceneView.h"
#include "CanvasItem.h"
#include "CanvasTypes.h"

#define LOCTEXT_NAMESPACE "CharacterProfileAssetEditor"

namespace
{
using AssetUtils = UPaper2DPlusCharacterProfileAsset;

void ClampHitboxForFrame(FHitboxData& Hitbox, UPaperFlipbook* Flipbook, int32 FrameIndex)
{
	int32 BoundsWidth = 0;
	int32 BoundsHeight = 0;
	if (!AssetUtils::GetFrameSpriteBounds(Flipbook, FrameIndex, BoundsWidth, BoundsHeight))
	{
		return;
	}

	AssetUtils::ClampHitboxToBounds(Hitbox, BoundsWidth, BoundsHeight);
}

int32 CountFrameHitboxesNeedingClamp(const FFrameHitboxData& Frame, UPaperFlipbook* Flipbook, int32 FrameIndex)
{
	int32 BoundsWidth = 0;
	int32 BoundsHeight = 0;
	if (!AssetUtils::GetFrameSpriteBounds(Flipbook, FrameIndex, BoundsWidth, BoundsHeight))
	{
		return 0;
	}

	int32 NeedingClampCount = 0;
	for (const FHitboxData& Hitbox : Frame.Hitboxes)
	{
		FHitboxData Temp = Hitbox;
		if (AssetUtils::ClampHitboxToBounds(Temp, BoundsWidth, BoundsHeight))
		{
			++NeedingClampCount;
		}
	}

	return NeedingClampCount;
}

int32 ClampFrameHitboxesToBounds(FFrameHitboxData& Frame, UPaperFlipbook* Flipbook, int32 FrameIndex)
{
	int32 BoundsWidth = 0;
	int32 BoundsHeight = 0;
	if (!AssetUtils::GetFrameSpriteBounds(Flipbook, FrameIndex, BoundsWidth, BoundsHeight))
	{
		return 0;
	}

	int32 ClampedCount = 0;
	for (FHitboxData& Hitbox : Frame.Hitboxes)
	{
		if (AssetUtils::ClampHitboxToBounds(Hitbox, BoundsWidth, BoundsHeight))
		{
			++ClampedCount;
		}
	}

	return ClampedCount;
}
}

TSharedRef<SWidget> SCharacterProfileAssetEditor::BuildHitboxEditorTab()
{
	TSharedRef<SWidget> TabContent = SNew(SVerticalBox)

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
			.Value(HitboxSplitterLeftRatio)
			.OnSlotResized(SSplitter::FOnSlotResized::CreateLambda([this](float NewSize)
			{
				HitboxSplitterLeftRatio = NewSize;
				SaveFloatLayoutValue(TEXT("HitboxSplitterLeft"), NewSize);
			}))
			[
				SNew(SVerticalBox)

				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					BuildToolPanel()
				]

				+ SVerticalBox::Slot()
				.FillHeight(1.0f)
				[
					WrapWithActivePanelHighlight(FName(TEXT("Hitbox.LeftFlipbooks")), 4, BuildFlipbookList())
				]
			]

			+ SSplitter::Slot()
			.Value(HitboxSplitterCenterRatio)
			.OnSlotResized(SSplitter::FOnSlotResized::CreateLambda([this](float NewSize)
			{
				HitboxSplitterCenterRatio = NewSize;
				SaveFloatLayoutValue(TEXT("HitboxSplitterCenter"), NewSize);
			}))
			[
				SNew(SVerticalBox)

				// Canvas Area
				+ SVerticalBox::Slot()
				.FillHeight(1.0f)
				[
					WrapWithActivePanelHighlight(FName(TEXT("Hitbox.Canvas")), 4, BuildCanvasArea())
				]

				// Dimension Management moved to Overview tab (T8)

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(4, 0, 4, 4)
				[
					WrapWithActivePanelHighlight(FName(TEXT("Hitbox.BottomFrames")), 4,
						SNew(SBox)
						.HeightOverride(148.0f)
						[
							BuildFrameList()
						]
					)
				]
			]

			+ SSplitter::Slot()
			.Value(HitboxSplitterRightRatio)
			.OnSlotResized(SSplitter::FOnSlotResized::CreateLambda([this](float NewSize)
			{
				HitboxSplitterRightRatio = NewSize;
				SaveFloatLayoutValue(TEXT("HitboxSplitterRight"), NewSize);
			}))
			[
				SNew(SScrollBox)
				+ SScrollBox::Slot()
				[
					SAssignNew(HitboxSidebarSectionsBox, SVerticalBox)
				]
			]
		];

	RebuildHitboxSidebarSections();
	return TabContent;
}

void SCharacterProfileAssetEditor::RebuildHitboxSidebarSections()
{
	if (!HitboxSidebarSectionsBox.IsValid())
	{
		return;
	}

	HitboxSidebarSectionsBox->ClearChildren();

	for (const FName& SectionId : HitboxSidebarSectionOrder)
	{
		TSharedRef<SWidget> SectionContent = SNullWidget::NullWidget;
		FText SectionTitle;

		if (SectionId == FName(TEXT("Hitboxes")))
		{
			SectionTitle = LOCTEXT("HitboxSidebarSectionHitboxes", "Hitboxes");
			SectionContent = BuildHitboxList();
		}
		else if (SectionId == FName(TEXT("Properties")))
		{
			SectionTitle = LOCTEXT("HitboxSidebarSectionProperties", "Properties");
			SectionContent = BuildPropertiesPanel();
		}
		else if (SectionId == FName(TEXT("FrameOps")))
		{
			SectionTitle = LOCTEXT("HitboxSidebarSectionFrameOps", "Frame Operations");
			SectionContent = BuildCopyOperationsPanel();
		}
		else
		{
			continue;
		}

		HitboxSidebarSectionsBox->AddSlot()
		.AutoHeight()
		.Padding(0, 0, 0, 4)
		[
			BuildReorderableSectionCard(
				FName(*FString::Printf(TEXT("Hitbox.Sidebar.%s"), *SectionId.ToString())),
				SectionTitle,
				LOCTEXT("HitboxSidebarSectionTooltip", "Hitbox editor section"),
				SectionContent)
		];
	}
}

TSharedRef<SWidget> SCharacterProfileAssetEditor::BuildToolbar()
{
	auto BuildVisibilityCheckbox = [this](uint8 Mask, EHitboxType Type, const FText& Label, const FLinearColor& Color) -> TSharedRef<SWidget>
	{
		return SNew(SCheckBox)
			.IsChecked_Lambda([this, Mask]() { return (HitboxVisibilityMask & Mask) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
			.OnCheckStateChanged_Lambda([this, Mask, Type](ECheckBoxState State) {
				HitboxVisibilityMask = (State == ECheckBoxState::Checked) ? (HitboxVisibilityMask | Mask) : (HitboxVisibilityMask & ~Mask);
				if (State == ECheckBoxState::Unchecked && EditorCanvas.IsValid())
				{
					const FFrameHitboxData* Frame = GetCurrentFrame();
					if (Frame)
					{
						for (int32 Idx : EditorCanvas->GetSelectedIndices())
						{
							if (Frame->Hitboxes.IsValidIndex(Idx) && Frame->Hitboxes[Idx].Type == Type)
								EditorCanvas->RemoveFromSelection(Idx);
						}
					}
				}
				RefreshHitboxList();
				RefreshPropertiesPanel();
			})
			.ToolTipText(FText::Format(LOCTEXT("ShowTypeTooltip", "Show {0} hitboxes"), Label))
			[
				SNew(STextBlock)
				.Text(Label)
				.ColorAndOpacity(FSlateColor(Color))
				.Font(FAppStyle::GetFontStyle("SmallFont"))
			];
	};

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
					if (GEditor) GEditor->UndoTransaction();
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
					if (GEditor) GEditor->RedoTransaction();
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
				BuildVisibilityCheckbox(0x01, EHitboxType::Attack, LOCTEXT("ATKFilter", "ATK"), FLinearColor::Red)
			]

			+ SWrapBox::Slot()
			.Padding(2, 0)
			[
				BuildVisibilityCheckbox(0x02, EHitboxType::Hurtbox, LOCTEXT("HRTFilter", "HRT"), FLinearColor::Green)
			]

			+ SWrapBox::Slot()
			.Padding(2, 0)
			[
				BuildVisibilityCheckbox(0x04, EHitboxType::Collision, LOCTEXT("COLFilter", "COL"), FLinearColor::Blue)
			]
		];
}

TSharedRef<SWidget> SCharacterProfileAssetEditor::BuildToolPanel()
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
				// Edit tool (also draws new hitboxes when dragging on empty space)
				SNew(SBorder)
				.BorderImage_Lambda([this]() { return CurrentTool == EHitboxEditorTool::Edit ? FAppStyle::GetBrush("ToolPanel.DarkGroupBorder") : FAppStyle::GetBrush("NoBorder"); })
				.BorderBackgroundColor_Lambda([this]() { return CurrentTool == EHitboxEditorTool::Edit ? FLinearColor(0.2f, 0.4f, 0.8f, 0.5f) : FLinearColor::Transparent; })
				.Padding(2)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "NoBorder")
					.ToolTipText(LOCTEXT("HitboxToolTooltip", "Hitbox Tool (E)\nDrag on empty space to draw new hitboxes\nClick to select, drag to move, double-click to edit"))
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
							.Text(LOCTEXT("HitboxToolShort", "Hitboxes"))
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

TSharedRef<SWidget> SCharacterProfileAssetEditor::BuildFlipbookList()
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

void SCharacterProfileAssetEditor::RefreshFlipbookList()
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
			const bool bIsSelected = (i == SelectedFlipbookIndex);
			UPaperFlipbook* LoadedFlipbook = !Anim.Flipbook.IsNull() ? Anim.Flipbook.LoadSynchronous() : nullptr;
			const bool bHasFlipbook = LoadedFlipbook != nullptr;
			const int32 FrameCount = bHasFlipbook ? LoadedFlipbook->GetNumKeyFrames() : Anim.Frames.Num();
			const FText SourceNameText = FText::FromString(bHasFlipbook ? Anim.Flipbook.GetAssetName() : TEXT("No Flipbook Assigned"));

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
				.ToolTip(
					SNew(SToolTip)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0, 0, 0, 6)
						[
							SNew(STextBlock)
							.Text(FText::FromString(Anim.FlipbookName))
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(SBox)
							.WidthOverride(128)
							.HeightOverride(128)
							[
								bHasFlipbook
									? StaticCastSharedRef<SWidget>(SNew(SFlipbookThumbnail).Flipbook(LoadedFlipbook))
									: StaticCastSharedRef<SWidget>(
										SNew(SBorder)
										.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
										.HAlign(HAlign_Center)
										.VAlign(VAlign_Center)
										[
											SNew(STextBlock)
											.Text(LOCTEXT("NoHitboxFlipbookTooltipPreview", "No FB"))
											.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
											.ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f)))
										])
							]
						]
					])
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "NoBorder")
					.OnClicked_Lambda([this, i]() { OnFlipbookSelected(i); return FReply::Handled(); })
					[
						SNew(SBorder)
						.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
						.BorderBackgroundColor(bIsSelected
							? FLinearColor(0.15f, 0.35f, 0.55f, 1.0f)
							: FLinearColor(0.03f, 0.03f, 0.03f, 1.0f))
						.Padding(FMargin(8, 6))
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							.Padding(0, 0, 8, 0)
							[
								SNew(SBox)
								.WidthOverride(44)
								.HeightOverride(44)
								[
									bHasFlipbook
										? StaticCastSharedRef<SWidget>(SNew(SFlipbookThumbnail).Flipbook(LoadedFlipbook))
										: StaticCastSharedRef<SWidget>(
											SNew(SBorder)
											.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
											.HAlign(HAlign_Center)
											.VAlign(VAlign_Center)
											[
												SNew(STextBlock)
												.Text(LOCTEXT("NoHitboxFlipbookListPreview", "No FB"))
												.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
												.ColorAndOpacity(FSlateColor(FLinearColor(0.55f, 0.55f, 0.55f)))
											])
								]
							]
							+ SHorizontalBox::Slot()
							.FillWidth(1.0f)
							.VAlign(VAlign_Center)
							[
								SNew(SVerticalBox)
								+ SVerticalBox::Slot()
								.AutoHeight()
								[
									SNew(SHorizontalBox)
									+ SHorizontalBox::Slot()
									.FillWidth(1.0f)
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
									.Padding(6, 0, 0, 0)
									[
										SNew(STextBlock)
										.Text(FText::Format(LOCTEXT("HitboxFrameCountLabel", "{0} frames"), FText::AsNumber(FrameCount)))
										.Font(FAppStyle::GetFontStyle("SmallFont"))
										.ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f)))
									]
								]
								+ SVerticalBox::Slot()
								.AutoHeight()
								.Padding(0, 2, 0, 0)
								[
									SNew(STextBlock)
									.Text(SourceNameText)
									.Font(FAppStyle::GetFontStyle("SmallFont"))
									.ColorAndOpacity(bHasFlipbook ? FLinearColor(0.4f, 0.8f, 0.4f) : FLinearColor(0.6f, 0.4f, 0.4f))
								]
							]
						]
					]
				];

			SidebarFlipbookNameTexts.Add(i, NameText);
			return Item;
		});

		// Trigger pending rename
		TriggerPendingRenameIfNeeded(SidebarFlipbookNameTexts);
	}
}

TSharedRef<SWidget> SCharacterProfileAssetEditor::BuildFrameList()
{
	SAssignNew(FrameListBox, SHorizontalBox);

	RefreshFrameList();

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0, 0, 0, 4)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("Frames", "FRAMES"))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
			]
		]
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SNew(SScrollBox)
			.Orientation(Orient_Horizontal)
			.ScrollBarAlwaysVisible(true)
			+ SScrollBox::Slot()
			.Padding(0, 0, 2, 0)
			[
				FrameListBox.ToSharedRef()
			]
		];
}

void SCharacterProfileAssetEditor::RefreshFrameList()
{
	if (!FrameListBox.IsValid()) return;

	FrameListBox->ClearChildren();

	const FFlipbookHitboxData* Anim = GetCurrentFlipbookData();
	if (Anim)
	{
		// Get the flipbook to extract frame sprites
		UPaperFlipbook* Flipbook = nullptr;
		if (!Anim->Flipbook.IsNull())
		{
			Flipbook = Anim->Flipbook.LoadSynchronous();
		}

		const int32 FrameCount = FMath::Min(GetCurrentFrameCount(), Anim->Frames.Num());
		for (int32 i = 0; i < FrameCount; i++)
		{
			const FFrameHitboxData& Frame = Anim->Frames[i];
			const bool bIsSelected = (i == SelectedFrameIndex);

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

			// Determine frame highlight color: active (green), multi-selected (blue), default (dark)
			const bool bIsMultiSelected = SelectedFrames.Contains(i);
			FLinearColor FrameBorderColor = bIsSelected ? ActiveFrameColor
				: bIsMultiSelected ? SelectedFrameHighlightColor
				: FLinearColor(0.12f, 0.12f, 0.12f, 1.0f);

			FrameListBox->AddSlot()
			.AutoWidth()
			.Padding(0, 0, 4, 0)
			[
				SNew(SBox)
				.WidthOverride(96.0f)
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
					.BorderBackgroundColor(FrameBorderColor)
					.Padding(FMargin(2.0f))
					.ToolTipText(FText::Format(
						LOCTEXT("HitboxFrameStripTooltipFmt", "Frame {0}\nAttack: {1}  Hurt: {2}  Collision: {3}  Sockets: {4}\nCtrl+Click: toggle select  Shift+Click: range select"),
						FText::AsNumber(i),
						FText::AsNumber(AttackCount),
						FText::AsNumber(HurtCount),
						FText::AsNumber(ColCount),
						FText::AsNumber(Frame.Sockets.Num())))
					.OnMouseButtonDown_Lambda([this, i, FrameSprite](const FGeometry&, const FPointerEvent& MouseEvent) -> FReply
					{
						if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
						{
							OnFrameSelected(i);
							ShowSpriteContextMenu(FrameSprite, MouseEvent.GetScreenSpacePosition());
							return FReply::Handled();
						}

						if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
						{
							FrameSelectionUtils::HandleFrameClick(SelectedFrames, FrameSelectionAnchorIndex, i, MouseEvent, GetCurrentFrameCount());
							OnFrameSelected(i);
							return FReply::Handled();
						}

						return FReply::Unhandled();
					})
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot()
							.AutoHeight()
							.HAlign(HAlign_Center)
							.Padding(0, 4, 0, 2)
							[
								SNew(SBox)
								.WidthOverride(44)
								.HeightOverride(44)
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
							+ SVerticalBox::Slot()
							.AutoHeight()
							.HAlign(HAlign_Center)
							[
								SNew(STextBlock)
								.Text(FText::Format(LOCTEXT("HitboxFrameStripTitle", "F{0}"), FText::AsNumber(i)))
								.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
							]
							+ SVerticalBox::Slot()
							.AutoHeight()
							.HAlign(HAlign_Center)
							.Padding(0, 1, 0, 0)
							[
								SNew(SHorizontalBox)
								+ SHorizontalBox::Slot()
								.AutoWidth()
								.Padding(0, 0, 4, 0)
								[
									SNew(SHorizontalBox)
									+ SHorizontalBox::Slot()
									.AutoWidth()
									.VAlign(VAlign_Center)
									[
										SNew(STextBlock)
										.Text(FText::AsNumber(AttackCount))
										.Font(FCoreStyle::GetDefaultFontStyle("Regular", 7))
										.ColorAndOpacity(FSlateColor(FLinearColor(0.72f, 0.72f, 0.72f)))
									]
									+ SHorizontalBox::Slot()
									.AutoWidth()
									.VAlign(VAlign_Center)
									.Padding(1, 0, 0, 0)
									[
										SNew(SBox)
										.WidthOverride(6)
										.HeightOverride(6)
										[
											SNew(SImage)
											.Image(FAppStyle::GetBrush("Icons.FilledCircle"))
											.ColorAndOpacity(FLinearColor(0.90f, 0.20f, 0.20f))
										]
									]
								]
								+ SHorizontalBox::Slot()
								.AutoWidth()
								.Padding(0, 0, 4, 0)
								[
									SNew(SHorizontalBox)
									+ SHorizontalBox::Slot()
									.AutoWidth()
									.VAlign(VAlign_Center)
									[
										SNew(STextBlock)
										.Text(FText::AsNumber(HurtCount))
										.Font(FCoreStyle::GetDefaultFontStyle("Regular", 7))
										.ColorAndOpacity(FSlateColor(FLinearColor(0.72f, 0.72f, 0.72f)))
									]
									+ SHorizontalBox::Slot()
									.AutoWidth()
									.VAlign(VAlign_Center)
									.Padding(1, 0, 0, 0)
									[
										SNew(SBox)
										.WidthOverride(6)
										.HeightOverride(6)
										[
											SNew(SImage)
											.Image(FAppStyle::GetBrush("Icons.FilledCircle"))
											.ColorAndOpacity(FLinearColor(0.20f, 0.80f, 0.20f))
										]
									]
								]
								+ SHorizontalBox::Slot()
								.AutoWidth()
								.Padding(0, 0, 4, 0)
								[
									SNew(SHorizontalBox)
									+ SHorizontalBox::Slot()
									.AutoWidth()
									.VAlign(VAlign_Center)
									[
										SNew(STextBlock)
										.Text(FText::AsNumber(ColCount))
										.Font(FCoreStyle::GetDefaultFontStyle("Regular", 7))
										.ColorAndOpacity(FSlateColor(FLinearColor(0.72f, 0.72f, 0.72f)))
									]
									+ SHorizontalBox::Slot()
									.AutoWidth()
									.VAlign(VAlign_Center)
									.Padding(1, 0, 0, 0)
									[
										SNew(SBox)
										.WidthOverride(6)
										.HeightOverride(6)
										[
											SNew(SImage)
											.Image(FAppStyle::GetBrush("Icons.FilledCircle"))
											.ColorAndOpacity(FLinearColor(0.30f, 0.50f, 0.90f))
										]
									]
								]
								+ SHorizontalBox::Slot()
								.AutoWidth()
								[
									SNew(SHorizontalBox)
									+ SHorizontalBox::Slot()
									.AutoWidth()
									.VAlign(VAlign_Center)
									[
										SNew(STextBlock)
										.Text(FText::AsNumber(Frame.Sockets.Num()))
										.Font(FCoreStyle::GetDefaultFontStyle("Regular", 7))
										.ColorAndOpacity(FSlateColor(FLinearColor(0.72f, 0.72f, 0.72f)))
									]
									+ SHorizontalBox::Slot()
									.AutoWidth()
									.VAlign(VAlign_Center)
									.Padding(1, 0, 0, 0)
									[
										SNew(SBox)
										.WidthOverride(6)
										.HeightOverride(6)
										[
											SNew(SImage)
											.Image(FAppStyle::GetBrush("Icons.FilledCircle"))
											.ColorAndOpacity(FLinearColor(0.90f, 0.80f, 0.20f))
										]
									]
								]
							]
							+ SVerticalBox::Slot()
							.AutoHeight()
							.HAlign(HAlign_Center)
							.Padding(0, 1, 0, 2)
							[
								SNew(STextBlock)
								.Text(LOCTEXT("InvulnIndicator", "INV"))
								.Font(FCoreStyle::GetDefaultFontStyle("Bold", 7))
								.ColorAndOpacity(FLinearColor(0.2f, 0.8f, 0.9f))
								.Visibility(Frame.bInvulnerable ? EVisibility::Visible : EVisibility::Collapsed)
							]
						]
					]
			];
		}
	}
}

TSharedRef<SWidget> SCharacterProfileAssetEditor::BuildCanvasArea()
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
				SAssignNew(EditorCanvas, SCharacterProfileEditorCanvas)
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

TSharedRef<SWidget> SCharacterProfileAssetEditor::BuildHitboxList()
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

void SCharacterProfileAssetEditor::RefreshHitboxList()
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

TSharedRef<SWidget> SCharacterProfileAssetEditor::BuildPropertiesPanel()
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

void SCharacterProfileAssetEditor::RefreshPropertiesPanel()
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

	// Batch Damage / Knockback setter (above hitbox properties)
	PropertiesBox->AddSlot()
	.AutoHeight()
	.Padding(4, 4, 4, 2)
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
				+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0, 0, 2, 0)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
					.Text(LOCTEXT("ApplyToFrame", "Frame"))
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
						RefreshFrameList();
						RefreshPropertiesPanel();
						return FReply::Handled();
					})
				]
				+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(2, 0)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
					.Text(LOCTEXT("ApplyToSelected", "Selected"))
					.IsEnabled_Lambda([this]() { return SelectedFrames.Num() > 0; })
					.ToolTipText(LOCTEXT("ApplyDmgSelectedTip", "Set damage and knockback on all attack hitboxes in selected frames."))
					.OnClicked_Lambda([this]()
					{
						FFlipbookHitboxData* Anim = GetCurrentFlipbookDataMutable();
						if (!Anim) return FReply::Handled();
						BeginTransaction(LOCTEXT("BatchDmgSelected", "Batch Set Damage (Selected)"));
						ForEachSelectedFrame([&](int32 Idx)
						{
							if (Anim->Frames.IsValidIndex(Idx))
							{
								for (FHitboxData& HB : Anim->Frames[Idx].Hitboxes)
								{
									if (HB.Type == EHitboxType::Attack)
									{
										HB.Damage = BatchDamageValue;
										HB.Knockback = BatchKnockbackValue;
									}
								}
							}
						});
						EndTransaction();
						RefreshFrameList();
						RefreshPropertiesPanel();
						return FReply::Handled();
					})
				]
				+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(2, 0, 0, 0)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
					.Text(LOCTEXT("ApplyToAllFrames", "All Frames"))
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
						RefreshFrameList();
						RefreshPropertiesPanel();
						return FReply::Handled();
					})
				]
			]
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

TSharedRef<SWidget> SCharacterProfileAssetEditor::BuildCopyOperationsPanel()
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

		// Copy to Selected Frames
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(2)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
			.IsEnabled_Lambda([this]() { return SelectedFrames.Num() > 0; })
			.ToolTipText(LOCTEXT("CopyToSelectedTooltip", "Copy all hitboxes and sockets from this frame to the selected frames (Ctrl/Shift+Click in frame strip to select)"))
			.OnClicked_Lambda([this]()
			{
				FFlipbookHitboxData* Anim = GetCurrentFlipbookDataMutable();
				if (!Anim || !Asset.IsValid()) return FReply::Handled();
				BeginTransaction(LOCTEXT("CopyToSelected", "Copy to Selected Frames"));
				ForEachSelectedFrame([&](int32 TargetIdx)
				{
					if (TargetIdx != SelectedFrameIndex)
					{
						Asset->CopyFrameDataToRange(Anim->FlipbookName, SelectedFrameIndex, TargetIdx, TargetIdx);
					}
				});
				EndTransaction();
				RefreshFrameList();
				RefreshHitboxList();
				RefreshPropertiesPanel();
				return FReply::Handled();
			})
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(4, 2)
				[
					SNew(SImage)
					.Image(FAppStyle::GetBrush("Icons.SelectInViewport"))
					.ColorAndOpacity(FLinearColor(0.15f, 0.45f, 0.75f))
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				.Padding(4, 2)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("CopyToSelectedShort", "Copy to Selected Frames"))
				]
			]
		]

		// Copy Selected Hitbox to All Frames
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
					.Text(LOCTEXT("CopySelectedShort", "Copy Selected Hitbox to All"))
				]
			]
		]

		// Copy to Remaining Frames
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(2)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
			.ToolTipText(LOCTEXT("CopyToRemainingTooltip", "Copy current frame hitboxes/sockets to all subsequent frames in this flipbook"))
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
					.Text(LOCTEXT("CopyToRemainingShort", "Copy to Remaining Frames"))
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
		]

		// Clear Selected Frames
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(2)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
			.IsEnabled_Lambda([this]() { return SelectedFrames.Num() > 0; })
			.ToolTipText(LOCTEXT("ClearSelectedTooltip", "Remove all hitboxes and sockets from the selected frames"))
			.OnClicked_Lambda([this]()
			{
				FFlipbookHitboxData* Anim = GetCurrentFlipbookDataMutable();
				if (!Anim) return FReply::Handled();
				BeginTransaction(LOCTEXT("ClearSelected", "Clear Selected Frames"));
				ForEachSelectedFrame([&](int32 Idx)
				{
					if (Anim->Frames.IsValidIndex(Idx))
					{
						Anim->Frames[Idx].Hitboxes.Empty();
						Anim->Frames[Idx].Sockets.Empty();
					}
				});
				EndTransaction();
				RefreshFrameList();
				RefreshHitboxList();
				RefreshPropertiesPanel();
				return FReply::Handled();
			})
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(4, 2)
				[
					SNew(SImage)
					.Image(FAppStyle::GetBrush("Icons.Delete"))
					.ColorAndOpacity(FLinearColor(0.75f, 0.35f, 0.35f))
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				.Padding(4, 2)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("ClearSelectedShort", "Clear Selected Frames"))
				]
			]
		];
}

void SCharacterProfileAssetEditor::OnCopyFromPrevious()
{
	if (SelectedFrameIndex <= 0 || !Asset.IsValid()) return;

	FFlipbookHitboxData* Anim = GetCurrentFlipbookDataMutable();
	if (!Anim) return;

	if (!Anim->Frames.IsValidIndex(SelectedFrameIndex) || !Anim->Frames.IsValidIndex(SelectedFrameIndex - 1)) return;

	BeginTransaction(LOCTEXT("CopyFromPrev", "Copy from Previous Frame"));
	const bool bCopied = Asset->CopyFrameDataToRange(
		Anim->FlipbookName,
		SelectedFrameIndex - 1,
		SelectedFrameIndex,
		SelectedFrameIndex,
		true
	);
	EndTransaction();

	if (bCopied)
	{
		RefreshFrameList();
		RefreshHitboxList();
		RefreshPropertiesPanel();
	}
}

void SCharacterProfileAssetEditor::OnPropagateAllToGroup()
{
	if (!Asset.IsValid()) return;

	FFlipbookHitboxData* Anim = GetCurrentFlipbookDataMutable();
	if (!Anim) return;

	if (SelectedFrameIndex < 0 || SelectedFrameIndex >= Anim->Frames.Num()) return;
	if (Anim->Frames.Num() <= 1) return;

	BeginTransaction(LOCTEXT("PropagateAll", "Propagate All to Group"));
	const bool bCopied = Asset->CopyFrameDataToRange(
		Anim->FlipbookName,
		SelectedFrameIndex,
		0,
		Anim->Frames.Num() - 1,
		true
	);
	EndTransaction();

	if (bCopied)
	{
		RefreshFrameList();
		RefreshHitboxList();
		RefreshPropertiesPanel();
	}
}

void SCharacterProfileAssetEditor::OnPropagateSelectedToGroup()
{
	if (!EditorCanvas.IsValid()) return;

	FFlipbookHitboxData* Anim = GetCurrentFlipbookDataMutable();
	if (!Anim) return;

	FFrameHitboxData* CurrentFrame = GetCurrentFrameMutable();
	if (!CurrentFrame) return;

	EHitboxSelectionType SelType = EditorCanvas->GetSelectionType();
	int32 SelIndex = EditorCanvas->GetPrimarySelectedIndex();

	if (SelType == EHitboxSelectionType::None) return;

	UPaperFlipbook* Flipbook = !Anim->Flipbook.IsNull() ? Anim->Flipbook.LoadSynchronous() : nullptr;

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
					FHitboxData& TargetHitbox = Anim->Frames[i].Hitboxes[SelIndex];
					TargetHitbox = SelectedHitbox;
					ClampHitboxForFrame(TargetHitbox, Flipbook, i);
				}
				else
				{
					const int32 NewHitboxIndex = Anim->Frames[i].Hitboxes.Add(SelectedHitbox);
					ClampHitboxForFrame(Anim->Frames[i].Hitboxes[NewHitboxIndex], Flipbook, i);
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

void SCharacterProfileAssetEditor::OnCopyToNextFrames()
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

void SCharacterProfileAssetEditor::OnClampCurrentFlipbookHitboxesToBounds()
{
	if (!Asset.IsValid())
	{
		return;
	}

	FFlipbookHitboxData* Anim = GetCurrentFlipbookDataMutable();
	if (!Anim || Anim->Frames.Num() <= 0)
	{
		return;
	}

	UPaperFlipbook* Flipbook = !Anim->Flipbook.IsNull() ? Anim->Flipbook.LoadSynchronous() : nullptr;
	if (!Flipbook)
	{
		return;
	}

	int32 NeedingClampCount = 0;
	for (int32 FrameIndex = 0; FrameIndex < Anim->Frames.Num(); ++FrameIndex)
	{
		NeedingClampCount += CountFrameHitboxesNeedingClamp(Anim->Frames[FrameIndex], Flipbook, FrameIndex);
	}

	if (NeedingClampCount <= 0)
	{
		FNotificationInfo Info(LOCTEXT("ClampCurrentNone", "No out-of-bounds hitboxes found in this flipbook."));
		Info.ExpireDuration = 2.0f;
		FSlateNotificationManager::Get().AddNotification(Info);
		return;
	}

	BeginTransaction(LOCTEXT("ClampCurrentFlipbookBoundsTxn", "Clamp Hitboxes to Frame Bounds"));

	int32 ClampedCount = 0;
	for (int32 FrameIndex = 0; FrameIndex < Anim->Frames.Num(); ++FrameIndex)
	{
		ClampedCount += ClampFrameHitboxesToBounds(Anim->Frames[FrameIndex], Flipbook, FrameIndex);
	}

	EndTransaction();

	RefreshFrameList();
	RefreshHitboxList();
	RefreshPropertiesPanel();

	FNotificationInfo Info(FText::Format(
		LOCTEXT("ClampCurrentDone", "Clamped {0} hitbox(es) in this flipbook."),
		FText::AsNumber(ClampedCount)));
	Info.ExpireDuration = 3.0f;
	FSlateNotificationManager::Get().AddNotification(Info);
}

void SCharacterProfileAssetEditor::OnClampAllFlipbookHitboxesToBounds()
{
	if (!Asset.IsValid() || Asset->Flipbooks.Num() <= 0)
	{
		return;
	}

	int32 NeedingClampCount = 0;
	for (FFlipbookHitboxData& Anim : Asset->Flipbooks)
	{
		UPaperFlipbook* Flipbook = !Anim.Flipbook.IsNull() ? Anim.Flipbook.LoadSynchronous() : nullptr;
		if (!Flipbook)
		{
			continue;
		}

		for (int32 FrameIndex = 0; FrameIndex < Anim.Frames.Num(); ++FrameIndex)
		{
			NeedingClampCount += CountFrameHitboxesNeedingClamp(Anim.Frames[FrameIndex], Flipbook, FrameIndex);
		}
	}

	if (NeedingClampCount <= 0)
	{
		FNotificationInfo Info(LOCTEXT("ClampAllNone", "No out-of-bounds hitboxes found across all flipbooks."));
		Info.ExpireDuration = 2.0f;
		FSlateNotificationManager::Get().AddNotification(Info);
		return;
	}

	BeginTransaction(LOCTEXT("ClampAllFlipbooksBoundsTxn", "Clamp Hitboxes to Frame Bounds (All Flipbooks)"));

	int32 ClampedCount = 0;
	for (FFlipbookHitboxData& Anim : Asset->Flipbooks)
	{
		UPaperFlipbook* Flipbook = !Anim.Flipbook.IsNull() ? Anim.Flipbook.LoadSynchronous() : nullptr;
		if (!Flipbook)
		{
			continue;
		}

		for (int32 FrameIndex = 0; FrameIndex < Anim.Frames.Num(); ++FrameIndex)
		{
			ClampedCount += ClampFrameHitboxesToBounds(Anim.Frames[FrameIndex], Flipbook, FrameIndex);
		}
	}

	EndTransaction();

	RefreshFrameList();
	RefreshHitboxList();
	RefreshPropertiesPanel();
	RefreshOverviewFlipbookList();
	RefreshAlignmentFlipbookList();
	RefreshAlignmentFrameList();

	FNotificationInfo Info(FText::Format(
		LOCTEXT("ClampAllDone", "Clamped {0} hitbox(es) across all flipbooks."),
		FText::AsNumber(ClampedCount)));
	Info.ExpireDuration = 3.0f;
	FSlateNotificationManager::Get().AddNotification(Info);
}

void SCharacterProfileAssetEditor::OnMirrorAllFrames()
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


void SCharacterProfileAssetEditor::OnClearCurrentFrame()
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

void SCharacterProfileAssetEditor::AddNewHitbox()
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

void SCharacterProfileAssetEditor::AddNewSocket()
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

void SCharacterProfileAssetEditor::DeleteSelected()
{
	if (EditorCanvas.IsValid())
	{
		EditorCanvas->DeleteSelection();
	}
}

#undef LOCTEXT_NAMESPACE
