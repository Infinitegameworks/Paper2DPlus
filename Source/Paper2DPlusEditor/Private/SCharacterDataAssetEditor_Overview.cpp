// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "CharacterDataAssetEditor.h"
#include "EditorCanvasUtils.h"
#include "Paper2DPlusSettings.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SWrapBox.h"
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
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
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

			// Flipbooks Section (PRIMARY - Animation Dashboard)
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
							.Text(LOCTEXT("Flipbooks", "ANIMATIONS"))
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
						]

						+ SHorizontalBox::Slot()
						.AutoWidth()
						[
							SNew(STextBlock)
							.Text_Lambda([this]() {
								if (!Asset.IsValid()) return FText::GetEmpty();
								return FText::Format(LOCTEXT("AnimCount", "{0} animations"), FText::AsNumber(Asset->Animations.Num()));
							})
							.ColorAndOpacity(FSlateColor(FLinearColor(0.7f, 0.7f, 0.7f)))
						]
					]

					// Search
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0, 8, 0, 0)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(0, 0, 8, 0)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("AnimationSearchLabel", "Search:"))
						]
						+ SHorizontalBox::Slot()
						.FillWidth(1.0f)
						[
							SNew(SEditableTextBox)
							.HintText(LOCTEXT("AnimationSearchHint", "Filter animations by name..."))
							.Text_Lambda([this]() { return FText::FromString(OverviewAnimationSearchText); })
							.OnTextChanged_Lambda([this](const FText& NewText)
							{
								OverviewAnimationSearchText = NewText.ToString();
								RefreshOverviewAnimationList();
							})
						]
					]

					// Flipbook grid
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0, 8, 0, 0)
					[
						BuildAnimationGrid()
					]

					// Action buttons
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0, 8, 0, 0)
					[
						SNew(SWrapBox)
						.UseAllottedSize(true)

						+ SWrapBox::Slot()
						.Padding(0, 0, 8, 4)
						[
							SNew(SButton)
							.Text(LOCTEXT("AddFlipbook", "+ Add Animation"))
							.ToolTipText(LOCTEXT("AddFlipbookOverviewTooltip", "Add a new animation entry to this character. You can then assign a flipbook and configure hitboxes for the animation."))
							.OnClicked_Lambda([this]() { AddNewAnimation(); RefreshOverviewAnimationList(); return FReply::Handled(); })
						]

						+ SWrapBox::Slot()
						.Padding(0, 0, 8, 4)
						[
							SNew(SButton)
							.Text(LOCTEXT("RemoveFlipbook", "- Remove Selected"))
							.ToolTipText(LOCTEXT("RemoveFlipbookOverviewTooltip", "Remove the currently selected animation from this character. This will also delete all associated hitbox data."))
							.IsEnabled_Lambda([this]() { return Asset.IsValid() && Asset->Animations.IsValidIndex(SelectedAnimationIndex); })
							.OnClicked_Lambda([this]() { RemoveSelectedAnimation(); RefreshOverviewAnimationList(); return FReply::Handled(); })
						]



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

				// Group Mappings
				+ SVerticalBox::Slot()
				.FillHeight(1.0f)
				[
					BuildGroupMappingsPanel()
				]
			]
		];

	return OverviewWidget;
}



TSharedRef<SWidget> SCharacterDataAssetEditor::BuildAnimationGrid()
{
	return SAssignNew(OverviewAnimationListBox, SVerticalBox);
}

bool SCharacterDataAssetEditor::PassesOverviewAnimationSearch(const FAnimationHitboxData& Animation) const
{
	const FString Query = OverviewAnimationSearchText.TrimStartAndEnd();
	if (Query.IsEmpty())
	{
		return true;
	}

	return Animation.AnimationName.Contains(Query, ESearchCase::IgnoreCase);
}

void SCharacterDataAssetEditor::RefreshOverviewAnimationList()
{
	if (!OverviewAnimationListBox.IsValid() || !Asset.IsValid()) return;

	OverviewAnimationListBox->ClearChildren();

	OverviewAnimNameTexts.Empty();

	// Header row
	OverviewAnimationListBox->AddSlot()
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
			.Text(LOCTEXT("FlipbookDimensions", "Size"))
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
	int32 VisibleAnimationCount = 0;
	for (int32 i = 0; i < Asset->Animations.Num(); i++)
	{
		const FAnimationHitboxData& Anim = Asset->Animations[i];
		if (!PassesOverviewAnimationSearch(Anim))
		{
			continue;
		}
		VisibleAnimationCount++;
		bool bSelected = (i == SelectedAnimationIndex);
		bool bHasFlipbook = !Anim.Flipbook.IsNull();

		int32 AnimIndex = i; // Capture for lambda

		// Load flipbook for thumbnail (animates on hover)
		UPaperFlipbook* LoadedFlipbookForThumb = bHasFlipbook ? Anim.Flipbook.LoadSynchronous() : nullptr;

		// Build frame sprites widget for expandable area
		TSharedRef<SHorizontalBox> FrameSpritesBox = SNew(SHorizontalBox);
		if (bHasFlipbook)
		{
			UPaperFlipbook* LoadedFlipbook = Anim.Flipbook.LoadSynchronous();
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
						.OnMouseButtonDown_Lambda([this, AnimIdx = i, FrameIdx](const FGeometry&, const FPointerEvent& MouseEvent) -> FReply
						{
							if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
							{
								FMenuBuilder MenuBuilder(true, nullptr);
								MenuBuilder.AddMenuEntry(
									FText::Format(LOCTEXT("SetFrameAsRef", "Set as Reference Sprite (Frame {0})"), FText::AsNumber(FrameIdx)),
									LOCTEXT("SetFrameAsRefTooltip", "Set this frame as the alignment reference sprite"),
									FSlateIcon(),
									FUIAction(FExecuteAction::CreateLambda([this, AnimIdx, FrameIdx]()
									{
										if (Asset.IsValid() && Asset->Animations.IsValidIndex(AnimIdx))
										{
											SetReferenceSprite(AnimIdx, FrameIdx);
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

		OverviewAnimationListBox->AddSlot()
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
				.OnClicked_Lambda([this, AnimIndex]()
				{
					SelectedAnimationIndex = AnimIndex;
					RefreshOverviewAnimationList();
					return FReply::Handled();
				})
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
					.BorderBackgroundColor(bSelected ? FLinearColor(0.2f, 0.4f, 0.8f, 0.3f) : FLinearColor(0.1f, 0.1f, 0.1f, 0.5f))
					.Padding(4)
					.OnMouseButtonDown_Lambda([this, AnimIndex](const FGeometry&, const FPointerEvent& MouseEvent) -> FReply
					{
						if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
						{
							SelectedAnimationIndex = AnimIndex;
							RefreshOverviewAnimationList();
							ShowAnimationContextMenu(AnimIndex);
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
								.OnClicked_Lambda([this, AnimIndex]()
								{
									// Open asset picker for flipbooks
									FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");

									FAssetPickerConfig PickerConfig;
									PickerConfig.Filter.ClassPaths.Add(UPaperFlipbook::StaticClass()->GetClassPathName());
									PickerConfig.bAllowNullSelection = true;
									PickerConfig.InitialAssetViewType = EAssetViewType::Tile;
									PickerConfig.OnAssetSelected = FOnAssetSelected::CreateLambda([this, AnimIndex](const FAssetData& AssetData)
									{
										if (!Asset.IsValid()) return;
										if (!Asset->Animations.IsValidIndex(AnimIndex)) return;

										BeginTransaction(LOCTEXT("ChangeFlipbookOverview", "Change Flipbook"));
										FAnimationHitboxData& AnimData = Asset->Animations[AnimIndex];
										if (AssetData.IsValid())
										{
											AnimData.Flipbook = TSoftObjectPtr<UPaperFlipbook>(AssetData.ToSoftObjectPath());
										}
										else
										{
											AnimData.Flipbook.Reset();
										}
										// Auto-sync frames to match new flipbook
										Asset->SyncFramesToFlipbook(AnimIndex);
										EndTransaction();

										// Close the picker menu
										FSlateApplication::Get().DismissAllMenus();
										RefreshOverviewAnimationList();
									});

									// Get current asset for initial selection
									if (Asset.IsValid() && Asset->Animations.IsValidIndex(AnimIndex))
									{
										const FAnimationHitboxData& AnimData = Asset->Animations[AnimIndex];
										if (!AnimData.Flipbook.IsNull())
										{
											PickerConfig.InitialAssetSelection = FAssetData(AnimData.Flipbook.LoadSynchronous());
										}
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
							SAssignNew(OverviewAnimNameTexts.Add(AnimIndex), SInlineEditableTextBlock)
							.Text(FText::FromString(Anim.AnimationName))
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
							.OnTextCommitted_Lambda([this, AnimIndex](const FText& NewText, ETextCommit::Type CommitType)
							{
								if (CommitType != ETextCommit::OnCleared)
								{
									RenameAnimation(AnimIndex, NewText.ToString());
								}
							})
						]

						// Dimensions (from flipbook's first sprite source size)
						+ SHorizontalBox::Slot()
						.FillWidth(0.12f)
						.VAlign(VAlign_Center)
						.Padding(4, 0)
						[
							SNew(STextBlock)
							.Text_Lambda([this, AnimIndex]() {
								if (!Asset.IsValid() || !Asset->Animations.IsValidIndex(AnimIndex))
									return FText::GetEmpty();
								const FAnimationHitboxData& AnimData = Asset->Animations[AnimIndex];
								FIntPoint Dims(0, 0);
								if (!AnimData.Flipbook.IsNull())
								{
									if (UPaperFlipbook* FB = AnimData.Flipbook.LoadSynchronous())
									{
										if (FB->GetNumKeyFrames() > 0)
										{
											if (UPaperSprite* Spr = FB->GetKeyFrameChecked(0).Sprite)
											{
												FVector2D Src = Spr->GetSourceSize();
												Dims = FIntPoint(FMath::RoundToInt(Src.X), FMath::RoundToInt(Src.Y));
											}
										}
									}
								}
								return FText::Format(LOCTEXT("DimFormat", "{0}x{1}"),
									FText::AsNumber(Dims.X), FText::AsNumber(Dims.Y));
							})
						]

						// Frame count (use flipbook frame count as authoritative source)
						+ SHorizontalBox::Slot()
						.FillWidth(0.12f)
						.VAlign(VAlign_Center)
						.Padding(4, 0)
						[
							SNew(STextBlock)
							.Text_Lambda([this, AnimIndex]() {
								if (!Asset.IsValid() || !Asset->Animations.IsValidIndex(AnimIndex))
									return FText::GetEmpty();
								const FAnimationHitboxData& AnimData = Asset->Animations[AnimIndex];
								int32 Count = AnimData.Frames.Num();
								if (!AnimData.Flipbook.IsNull())
								{
									if (UPaperFlipbook* FB = AnimData.Flipbook.LoadSynchronous())
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
	if (PendingRenameAnimationIndex != INDEX_NONE)
	{
		int32 RenameIdx = PendingRenameAnimationIndex;
		PendingRenameAnimationIndex = INDEX_NONE;

		if (OverviewAnimNameTexts.Contains(RenameIdx))
		{
			TWeakPtr<SInlineEditableTextBlock> WeakText = OverviewAnimNameTexts[RenameIdx];
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
	if (Asset->Animations.Num() == 0)
	{
		OverviewAnimationListBox->AddSlot()
		.AutoHeight()
		.Padding(8)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("NoFlipbooks", "No animations yet. Click '+ Add Animation' to get started."))
			.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)))
		];
	}
	else if (VisibleAnimationCount == 0)
	{
		OverviewAnimationListBox->AddSlot()
		.AutoHeight()
		.Padding(8)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("NoFlipbooksSearchMatch", "No animations match the current search filter."))
			.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)))
		];
	}

}

#undef LOCTEXT_NAMESPACE
