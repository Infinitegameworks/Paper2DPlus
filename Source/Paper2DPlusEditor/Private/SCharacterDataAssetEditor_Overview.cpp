// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "CharacterDataAssetEditor.h"
#include "EditorCanvasUtils.h"
#include "Paper2DPlusSettings.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Images/SImage.h"
#include "PaperFlipbook.h"
#include "PaperSprite.h"
#include "PropertyCustomizationHelpers.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Framework/Notifications/NotificationManager.h"

#define LOCTEXT_NAMESPACE "CharacterDataAssetEditor"

TSharedRef<SWidget> SCharacterDataAssetEditor::BuildOverviewTab()
{
	TSharedRef<SWidget> OverviewWidget = SNew(SSplitter)
		.Orientation(Orient_Horizontal)

		// Left: Main content
		+ SSplitter::Slot()
		.Value(0.7f)
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			.Padding(8)
			[
				SNew(SVerticalBox)

				// Character Info Section (compact)
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0, 0, 0, 8)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
				.Padding(8)
				[
					SNew(SHorizontalBox)
					.ToolTipText(LOCTEXT("DisplayNameTooltip", "A friendly name for this character data asset. Used for display purposes in the editor and can be used in runtime UI."))

					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0, 0, 8, 0)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("DisplayName", "Display Name:"))
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
					]

					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					[
						SNew(SEditableTextBox)
						.Text_Lambda([this]() { return Asset.IsValid() ? FText::FromString(Asset->DisplayName) : FText::GetEmpty(); })
						.OnTextCommitted_Lambda([this](const FText& Text, ETextCommit::Type) {
							if (Asset.IsValid())
							{
								Asset->Modify();
								Asset->DisplayName = Text.ToString();
							}
						})
					]
				]
			]

			// Flipbooks Section (PRIMARY - Flipbook Dashboard)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 0, 0, 8)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
				.Padding(8)
				[
					SNew(SVerticalBox)

					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SHorizontalBox)

						+ SHorizontalBox::Slot()
						.FillWidth(1.0f)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("Flipbooks", "FLIPBOOKS"))
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
						]

						+ SHorizontalBox::Slot()
						.AutoWidth()
						[
							SNew(STextBlock)
							.Text_Lambda([this]() {
								if (!Asset.IsValid()) return FText::GetEmpty();
								return FText::Format(LOCTEXT("AnimCount", "{0} flipbooks"), FText::AsNumber(Asset->Flipbooks.Num()));
							})
							.ColorAndOpacity(FSlateColor(FLinearColor(0.7f, 0.7f, 0.7f)))
						]
					]

					// Flipbook groups panel (search + grouped flipbook cards)
					+ SVerticalBox::Slot()
					.FillHeight(1.0f)
					.Padding(0, 8, 0, 0)
					[
						BuildFlipbookGroupsPanel()
					]
				]
			]

				]
		]

		// Right: Dimensions sidebar
		+ SSplitter::Slot()
		.Value(0.3f)
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			.Padding(8)
			[
				SNew(SVerticalBox)

				// Tag Mappings
				+ SVerticalBox::Slot()
				.FillHeight(1.0f)
				[
					BuildTagMappingsPanel()
				]
			]
		];

	return OverviewWidget;
}



TSharedRef<SWidget> SCharacterDataAssetEditor::BuildFlipbookGrid()
{
	return SAssignNew(OverviewFlipbookListBox, SVerticalBox);
}

bool SCharacterDataAssetEditor::PassesOverviewFlipbookSearch(const FFlipbookHitboxData& FlipbookData) const
{
	const FString Query = OverviewFlipbookSearchText.TrimStartAndEnd();
	if (Query.IsEmpty())
	{
		return true;
	}

	return FlipbookData.FlipbookName.Contains(Query, ESearchCase::IgnoreCase);
}

void SCharacterDataAssetEditor::RefreshOverviewFlipbookList()
{
	// Refresh flipbook groups panel (all call sites route through here)
	RefreshFlipbookGroupsPanel();

	if (!OverviewFlipbookListBox.IsValid() || !Asset.IsValid()) return;

	OverviewFlipbookListBox->ClearChildren();

	OverviewFlipbookNameTexts.Empty();

	// Header row
	OverviewFlipbookListBox->AddSlot()
	.AutoHeight()
	.Padding(0, 0, 0, 4)
	[
		SNew(SHorizontalBox)

		// Flipbook asset column header
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(4, 0)
		[
			SNew(SBox)
			.WidthOverride(68)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("FlipbookAsset", "Asset"))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
			]
		]

		+ SHorizontalBox::Slot()
		.FillWidth(0.25f)
		.Padding(4, 0)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("FlipbookName", "Name"))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
		]

		+ SHorizontalBox::Slot()
		.FillWidth(0.12f)
		.Padding(4, 0)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("FlipbookFrames", "Frames"))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
		]

	];

	// Flipbook rows
	int32 VisibleFlipbookCount = 0;
	TArray<int32> SortedIndices = GetSortedFlipbookIndices();
	for (int32 i : SortedIndices)
	{
		const FFlipbookHitboxData& FBData = Asset->Flipbooks[i];
		if (!PassesOverviewFlipbookSearch(FBData))
		{
			continue;
		}
		VisibleFlipbookCount++;
		bool bSelected = (i == SelectedFlipbookIndex);
		bool bHasFlipbook = !FBData.Flipbook.IsNull();

		int32 FlipbookIndex = i; // Capture for lambda

		// Load flipbook for thumbnail (animates on hover)
		UPaperFlipbook* LoadedFlipbookForThumb = bHasFlipbook ? FBData.Flipbook.LoadSynchronous() : nullptr;

		// Build frame sprites widget for expandable area
		TSharedRef<SHorizontalBox> FrameSpritesBox = SNew(SHorizontalBox);
		if (bHasFlipbook)
		{
			UPaperFlipbook* LoadedFlipbook = FBData.Flipbook.LoadSynchronous();
			if (LoadedFlipbook)
			{
				const int32 NumKeyFrames = LoadedFlipbook->GetNumKeyFrames();
				for (int32 FrameIdx = 0; FrameIdx < NumKeyFrames; FrameIdx++)
				{
					UPaperSprite* FrameSprite = LoadedFlipbook->GetKeyFrameChecked(FrameIdx).Sprite;

					// Capture sprite as weak pointer for the lambda
					TWeakObjectPtr<UPaperSprite> WeakSprite = FrameSprite;

					FrameSpritesBox->AddSlot()
					.AutoWidth()
					.Padding(2, 0)
					[
						SNew(SBorder)
						.BorderImage(FAppStyle::GetBrush("NoBorder"))
						.OnMouseButtonDown_Lambda([this, FlipbookIdx = i, FrameIdx](const FGeometry&, const FPointerEvent& MouseEvent) -> FReply
						{
							if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
							{
								FMenuBuilder MenuBuilder(true, nullptr);
								MenuBuilder.AddMenuEntry(
									FText::Format(LOCTEXT("SetFrameAsRef", "Set as Reference Sprite (Frame {0})"), FText::AsNumber(FrameIdx)),
									LOCTEXT("SetFrameAsRefTooltip", "Set this frame as the alignment reference sprite"),
									FSlateIcon(),
									FUIAction(FExecuteAction::CreateLambda([this, FlipbookIdx, FrameIdx]()
									{
										if (Asset.IsValid() && Asset->Flipbooks.IsValidIndex(FlipbookIdx))
										{
											SetReferenceSprite(FlipbookIdx, FrameIdx);
										}
									}))
								);

								FSlateApplication::Get().PushMenu(
									SharedThis(this),
									FWidgetPath(),
									MenuBuilder.MakeWidget(),
									MouseEvent.GetScreenSpacePosition(),
									FPopupTransitionEffect::ContextMenu
								);
								return FReply::Handled();
							}
							return FReply::Unhandled();
						})
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot()
							.AutoHeight()
							.HAlign(HAlign_Center)
							[
								SNew(SBox)
								.WidthOverride(48)
								.HeightOverride(48)
								[
									SNew(SButton)
									.ButtonStyle(FAppStyle::Get(), "NoBorder")
									.IsEnabled(FrameSprite != nullptr)
									.ToolTipText(FrameSprite ? FText::Format(LOCTEXT("OpenSpriteEditor", "Open {0} in Sprite Editor"), FText::FromString(FrameSprite->GetName())) : LOCTEXT("NoSprite", "No sprite"))
									.OnClicked_Lambda([WeakSprite]()
									{
										if (UPaperSprite* Sprite = WeakSprite.Get())
										{
											GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(Sprite);
										}
										return FReply::Handled();
									})
									[
										FrameSprite
											? StaticCastSharedRef<SWidget>(SNew(SSpriteThumbnail).Sprite(FrameSprite))
											: StaticCastSharedRef<SWidget>(SNew(SBorder)
												.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
												.HAlign(HAlign_Center)
												.VAlign(VAlign_Center)
												[
													SNew(STextBlock)
													.Text(FText::AsNumber(FrameIdx))
													.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
												])
									]
								]
							]
							+ SVerticalBox::Slot()
							.AutoHeight()
							.HAlign(HAlign_Center)
							.Padding(0, 2, 0, 0)
							[
								SNew(STextBlock)
								.Text(FText::AsNumber(FrameIdx))
								.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
								.ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f)))
							]
						]
					];
				}
			}
		}

		OverviewFlipbookListBox->AddSlot()
		.AutoHeight()
		.Padding(0, 2)
		[
			SNew(SVerticalBox)

			// Main row (clickable to select)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "NoBorder")
				.OnClicked_Lambda([this, FlipbookIndex]()
				{
					SelectedFlipbookIndex = FlipbookIndex;
					RefreshOverviewFlipbookList();
					return FReply::Handled();
				})
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
					.BorderBackgroundColor(bSelected ? FLinearColor(0.2f, 0.4f, 0.8f, 0.3f) : FLinearColor(0.1f, 0.1f, 0.1f, 0.5f))
					.Padding(4)
					.OnMouseButtonDown_Lambda([this, FlipbookIndex](const FGeometry&, const FPointerEvent& MouseEvent) -> FReply
					{
						if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
						{
							SelectedFlipbookIndex = FlipbookIndex;
							RefreshOverviewFlipbookList();
							ShowFlipbookContextMenu(FlipbookIndex);
							return FReply::Handled();
						}
						return FReply::Unhandled();
					})
					[
						SNew(SHorizontalBox)

						// Clickable flipbook thumbnail
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(4, 0)
						[
							SNew(SBox)
							.WidthOverride(64)
							.HeightOverride(64)
							[
								SNew(SButton)
								.ButtonStyle(FAppStyle::Get(), "NoBorder")
								.OnClicked_Lambda([this, FlipbookIndex]()
								{
									OpenFlipbookPicker(FlipbookIndex);
									return FReply::Handled();
								})
								.ToolTipText(LOCTEXT("ClickToChangeFlipbook", "Click to change flipbook"))
								[
									LoadedFlipbookForThumb
										? StaticCastSharedRef<SWidget>(SNew(SFlipbookThumbnail).Flipbook(LoadedFlipbookForThumb))
										: StaticCastSharedRef<SWidget>(SNew(SBorder)
											.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
											.HAlign(HAlign_Center)
											.VAlign(VAlign_Center)
											[
												SNew(STextBlock)
												.Text(LOCTEXT("NoFlipbookIcon", "?"))
												.Font(FCoreStyle::GetDefaultFontStyle("Bold", 16))
												.ColorAndOpacity(FSlateColor(FLinearColor(0.4f, 0.4f, 0.4f)))
											])
								]
							]
						]

						// Flipbook name (inline editable)
						+ SHorizontalBox::Slot()
						.FillWidth(0.25f)
						.VAlign(VAlign_Center)
						.Padding(4, 0)
						[
							SAssignNew(OverviewFlipbookNameTexts.Add(FlipbookIndex), SInlineEditableTextBlock)
							.Text(FText::FromString(FBData.FlipbookName))
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
							.OnTextCommitted_Lambda([this, FlipbookIndex](const FText& NewText, ETextCommit::Type CommitType)
							{
								if (CommitType != ETextCommit::OnCleared)
								{
									RenameFlipbook(FlipbookIndex, NewText.ToString());
								}
							})
						]

						// Frame count (use flipbook frame count as authoritative source)
						+ SHorizontalBox::Slot()
						.FillWidth(0.12f)
						.VAlign(VAlign_Center)
						.Padding(4, 0)
						[
							SNew(STextBlock)
							.Text_Lambda([this, FlipbookIndex]() {
								if (!Asset.IsValid() || !Asset->Flipbooks.IsValidIndex(FlipbookIndex))
									return FText::GetEmpty();
								const FFlipbookHitboxData& FBData = Asset->Flipbooks[FlipbookIndex];
								int32 Count = FBData.Frames.Num();
								if (!FBData.Flipbook.IsNull())
								{
									if (UPaperFlipbook* FB = FBData.Flipbook.LoadSynchronous())
									{
										int32 FBFrames = FB->GetNumKeyFrames();
										if (FBFrames > 0) Count = FBFrames;
									}
								}
								return FText::AsNumber(Count);
							})
						]

					]
				]
			]

			// Expandable frame sprites section
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(68, 0, 0, 0) // Indent to align with content after thumbnail
			[
				SNew(SExpandableArea)
				.AreaTitle(LOCTEXT("FrameSpritesTitle", "Frame Sprites"))
				.InitiallyCollapsed(true)
				.BorderImage(FAppStyle::GetBrush("NoBorder"))
				.BodyBorderImage(FAppStyle::GetBrush("NoBorder"))
				.HeaderPadding(FMargin(2, 2))
				.Padding(FMargin(0, 4))
				.BodyContent()
				[
					SNew(SScrollBox)
					.Orientation(Orient_Horizontal)
					+ SScrollBox::Slot()
					[
						FrameSpritesBox
					]
				]
			]
		];
	}

	// Trigger pending rename via deferred active timer
	if (PendingRenameFlipbookIndex != INDEX_NONE)
	{
		int32 RenameIdx = PendingRenameFlipbookIndex;
		PendingRenameFlipbookIndex = INDEX_NONE;

		if (OverviewFlipbookNameTexts.Contains(RenameIdx))
		{
			TWeakPtr<SInlineEditableTextBlock> WeakText = OverviewFlipbookNameTexts[RenameIdx];
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

	// Empty state
	if (Asset->Flipbooks.Num() == 0)
	{
		OverviewFlipbookListBox->AddSlot()
		.AutoHeight()
		.Padding(8)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("NoFlipbooks", "No flipbooks yet. Click '+ Add Flipbook' to get started."))
			.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)))
		];
	}
	else if (VisibleFlipbookCount == 0)
	{
		OverviewFlipbookListBox->AddSlot()
		.AutoHeight()
		.Padding(8)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("NoFlipbooksSearchMatch", "No flipbooks match the current search filter."))
			.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)))
		];
	}

}

#undef LOCTEXT_NAMESPACE
