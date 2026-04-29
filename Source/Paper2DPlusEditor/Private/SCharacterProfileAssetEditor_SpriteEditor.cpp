// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "CharacterProfileAssetEditor.h"
#include "EditorCanvasUtils.h"
#include "FrameEvents/Paper2DPlusFrameEvent.h"
#include "FrameEvents/Paper2DPlusFrameEventState.h"
#include "Input/DragAndDrop.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Images/SImage.h"
#include "PaperFlipbook.h"
#include "PaperSprite.h"
#include "SpriteEditorOnlyTypes.h"
#include "SpriteExtractionUtils.h"
#include "SpriteEditorOnlyTypes.h"
#include "Engine/Texture2D.h"
#include "Paper2DPlusSettings.h"
#include "Framework/Application/SlateApplication.h"
#include "SDragClickWrapper.h"
#include "SSpriteEditorDragDropWidgets.h"
#include "PropertyCustomizationHelpers.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/SToolTip.h"

/** Sprite Editor tab — Per-frame sprite offset adjustment, flip controls, alignment visualization, and sprite queue management. */

#define LOCTEXT_NAMESPACE "CharacterProfileAssetEditor"

namespace
{
int32 FindSpriteEditorFrameIndexBySourceIndex(const FFlipbookProfileEntry& Anim, int32 SourceFrameIndex)
{
	for (int32 Index = 0; Index < Anim.CombatData.FrameExtractionInfo.Num(); ++Index)
	{
		if (Anim.CombatData.FrameExtractionInfo[Index].SourceFrameIndex == SourceFrameIndex)
		{
			return Index;
		}
	}
	return INDEX_NONE;
}
}

// Frame and queue drag-drop widgets extracted to SSpriteEditorDragDropWidgets.h


TSharedRef<SWidget> SCharacterProfileAssetEditor::BuildSpriteEditorTab()
{
	TSharedRef<SWidget> TabContent = SNew(SVerticalBox)

		// Main content area (toolbar lives inside the center column so the left flipbook list stays top-flush)
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		.Padding(2, 0)
		[
			SNew(SSplitter)
			.Orientation(Orient_Horizontal)

			// Left panel: Flipbook and Frame lists
			+ SSplitter::Slot()
			.Value(0.2f)
			[
				SAssignNew(SpriteEditorLeftSectionsBox, SSplitter)
			.Orientation(Orient_Vertical)
			]

			// Center: Toolbar + Canvas
			+ SSplitter::Slot()
			.Value(0.65f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					BuildSpriteEditorToolbar()
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(4, 2, 4, 0)
				[
					SNew(STextBlock)
					.Text_Lambda([this]() {
						const FFlipbookProfileEntry* Anim = GetCurrentFlipbookData();
						if (!Anim) return FText::FromString(TEXT("No Flipbook"));
						int32 FrameCount = GetCurrentFrameCount();
						return FText::Format(LOCTEXT("SpriteFlipbookTitleFmt", "{0}  Frame {1}/{2}"),
							FText::FromString(Anim->Identity.FlipbookName),
							FText::AsNumber(SelectedFrameIndex + 1),
							FText::AsNumber(FrameCount));
					})
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
					.ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f)))
				]
				+ SVerticalBox::Slot()
				.FillHeight(1.0f)
				[
					WrapWithActivePanelHighlight(FName(TEXT("SpriteEditor.Canvas")), 2, BuildSpriteEditorCanvasArea())
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					BuildSpriteEditorFrameList()
				]
			]

			// Right panel: Offset controls
			+ SSplitter::Slot()
			.Value(0.15f)
			[
				WrapWithActivePanelHighlight(FName(TEXT("SpriteEditor.OffsetControls")), 4,
					SNew(SScrollBox)
					+ SScrollBox::Slot()
					[
						BuildOffsetControlsPanel()
					]
				)
			]
		];

	RebuildSpriteEditorLeftSections();
	return TabContent;
}

void SCharacterProfileAssetEditor::RebuildSpriteEditorLeftSections()
{
	if (!SpriteEditorLeftSectionsBox.IsValid())
	{
		return;
	}

#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6)
	SpriteEditorLeftSectionsBox->ClearChildren();
#else
	// SSplitter::ClearChildren() was added in UE 5.6.
	// For older versions, remove slots in reverse order.
	while (SpriteEditorLeftSectionsBox->GetChildren()->Num() > 0)
	{
		SpriteEditorLeftSectionsBox->RemoveAt(SpriteEditorLeftSectionsBox->GetChildren()->Num() - 1);
	}
#endif

	auto AddSection = [this](FName SectionId, const FText& SectionTitle, float FillWeight, TSharedRef<SWidget> SectionContent)
	{
		SpriteEditorLeftSectionsBox->AddSlot()
		.Value(FillWeight)
		[
			BuildReorderableSectionCard(
				FName(*FString::Printf(TEXT("SpriteEditor.Left.%s"), *SectionId.ToString())),
				SectionTitle,
				LOCTEXT("SpriteEditorSectionTooltip", "Sprite/Flipbook section"),
				SectionContent,
				true)
		];
	};

	for (const FName& SectionId : SpriteEditorLeftSectionOrder)
	{
		if (SectionId == FName(TEXT("Flipbooks")))
		{
			AddSection(
				SectionId,
				LOCTEXT("SpriteEditorSectionFlipbooks", "Flipbooks"),
				0.35f,
				BuildSpriteEditorFlipbookList());
		}
		else if (SectionId == FName(TEXT("Queue")))
		{
			AddSection(
				SectionId,
				LOCTEXT("SpriteEditorSectionQueue", "Playback Queue"),
				0.25f,
				BuildPlaybackQueuePanel());
		}
	}

	RefreshSpriteEditorFlipbookList();
	RefreshPlaybackQueueList();
	RefreshSpriteEditorFrameList();
}

TSharedRef<SWidget> SCharacterProfileAssetEditor::BuildSpriteEditorToolbar()
{
	// Playback mode options
	TSharedPtr<TArray<TSharedPtr<FString>>> PlaybackModes = MakeShared<TArray<TSharedPtr<FString>>>();
	PlaybackModes->Add(MakeShared<FString>(TEXT("Forward")));
	PlaybackModes->Add(MakeShared<FString>(TEXT("Ping-Pong")));
	PlaybackModes->Add(MakeShared<FString>(TEXT("Reverse")));

	// Determine initial selection from current state
	int32 InitialModeIdx = bPingPongPlayback ? 1 : (bPlaybackReversed ? 2 : 0);

	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(FMargin(8, 4))
		[
			SNew(SHorizontalBox)

			// === Playback Mode ===
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0, 0, 4, 0)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("PlaybackLabel", "Playback:"))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0, 0, 8, 0)
			[
				SNew(SComboBox<TSharedPtr<FString>>)
				.OptionsSource(PlaybackModes.Get())
				.InitiallySelectedItem((*PlaybackModes)[InitialModeIdx])
				.OnSelectionChanged_Lambda([this, PlaybackModes](TSharedPtr<FString> Item, ESelectInfo::Type)
				{
					if (!Item.IsValid() || !PlaybackModes.IsValid()) return;
					int32 Idx = PlaybackModes->IndexOfByKey(Item);
					bPingPongPlayback = (Idx == 1);
					bPlaybackReversed = (Idx == 2);
				})
				.OnGenerateWidget_Lambda([](TSharedPtr<FString> Item) -> TSharedRef<SWidget>
				{
					return SNew(STextBlock).Text(FText::FromString(*Item)).Font(FCoreStyle::GetDefaultFontStyle("Regular", 8));
				})
				[
					SNew(STextBlock)
					.Text_Lambda([this]() -> FText
					{
						if (bPingPongPlayback) return LOCTEXT("ModePingPong", "Ping-Pong");
						if (bPlaybackReversed) return LOCTEXT("ModeReverse", "Reverse");
						return LOCTEXT("ModeForward", "Forward");
					})
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
				]
			]

			// Separator
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(8, 0)
			[
				SNew(SSeparator)
				.Orientation(Orient_Vertical)
			]

			// === Skins Mixer ===
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0, 0, 8, 0)
			[
				SNew(SComboButton)
				.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
				.ContentPadding(FMargin(4, 2))
				.HasDownArrow(true)
				.ButtonContent()
				[
					SNew(STextBlock)
					.Text_Lambda([this]() -> FText
					{
						int32 ActiveCount = (bShowOnionSkin ? 1 : 0) + (bShowForwardOnionSkin ? 1 : 0)
							+ ((bShowReferenceSprite && ReferenceFlipbookIndex != INDEX_NONE) ? 1 : 0);
						if (ActiveCount > 0)
							return FText::Format(LOCTEXT("SkinsActive", "Skins ({0})"), FText::AsNumber(ActiveCount));
						return LOCTEXT("SkinsNone", "Skins");
					})
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
				]
				.MenuContent()
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
					.Padding(8)
					[
						SNew(SVerticalBox)

						// Mixer channels row
						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(SHorizontalBox)

							// --- Onion channel ---
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.Padding(0, 0, 12, 0)
							[
								SNew(SVerticalBox)
								+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 0, 0, 4)
								[
									SNew(STextBlock).Text(LOCTEXT("MixerOnion", "Onion"))
										.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
										.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 1.0f)))
								]
								+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 0, 0, 4)
								[
									SNew(SBox).WidthOverride(24).HeightOverride(80)
									[
										SNew(SSlider)
										.Orientation(Orient_Vertical)
										.MinValue(0.1f).MaxValue(0.8f)
										.Value_Lambda([this]() { return OnionSkinOpacity; })
										.OnValueChanged_Lambda([this](float V) { OnionSkinOpacity = V; })
										.IsEnabled_Lambda([this]() { return bShowOnionSkin; })
									]
								]
								+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 0, 0, 2)
								[
									SNew(SCheckBox)
									.IsChecked_Lambda([this]() { return bShowOnionSkin ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
									.OnCheckStateChanged_Lambda([this](ECheckBoxState S) { bShowOnionSkin = (S == ECheckBoxState::Checked); })
								]
								+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 2, 0, 0)
								[
									SNew(SBox).WidthOverride(32)
									[
										SNew(SSpinBox<int32>)
										.MinValue(1).MaxValue(3)
										.Value_Lambda([this]() { return OnionSkinFrames; })
										.OnValueChanged_Lambda([this](int32 V) { OnionSkinFrames = V; })
										.IsEnabled_Lambda([this]() { return bShowOnionSkin; })
									]
								]
							]

							// --- Forward channel ---
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.Padding(0, 0, 12, 0)
							[
								SNew(SVerticalBox)
								+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 0, 0, 4)
								[
									SNew(STextBlock).Text(LOCTEXT("MixerForward", "Forward"))
										.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
										.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 1.0f, 0.5f)))
								]
								+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 0, 0, 4)
								[
									SNew(SBox).WidthOverride(24).HeightOverride(80)
									[
										SNew(SSlider)
										.Orientation(Orient_Vertical)
										.MinValue(0.1f).MaxValue(0.8f)
										.Value_Lambda([this]() { return OnionSkinOpacity; })
										.OnValueChanged_Lambda([this](float V) { OnionSkinOpacity = V; })
										.IsEnabled_Lambda([this]() { return bShowForwardOnionSkin; })
									]
								]
								+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 0, 0, 2)
								[
									SNew(SCheckBox)
									.IsChecked_Lambda([this]() { return bShowForwardOnionSkin ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
									.OnCheckStateChanged_Lambda([this](ECheckBoxState S) { bShowForwardOnionSkin = (S == ECheckBoxState::Checked); })
								]
								+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 2, 0, 0)
								[
									SNew(SBox).WidthOverride(32)
									[
										SNew(SSpinBox<int32>)
										.MinValue(1).MaxValue(3)
										.Value_Lambda([this]() { return ForwardOnionSkinFrames; })
										.OnValueChanged_Lambda([this](int32 V) { ForwardOnionSkinFrames = V; })
										.IsEnabled_Lambda([this]() { return bShowForwardOnionSkin; })
									]
								]
							]

							// --- Reference channel ---
							+ SHorizontalBox::Slot()
							.AutoWidth()
							[
								SNew(SVerticalBox)
								+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 0, 0, 4)
								[
									SNew(STextBlock).Text(LOCTEXT("MixerRef", "Reference"))
										.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
										.ColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.8f, 0.4f)))
								]
								+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 0, 0, 4)
								[
									SNew(SBox).WidthOverride(24).HeightOverride(80)
									[
										SNew(SSlider)
										.Orientation(Orient_Vertical)
										.MinValue(0.1f).MaxValue(0.8f)
										.Value_Lambda([this]() { return ReferenceSpriteOpacity; })
										.OnValueChanged_Lambda([this](float V) { ReferenceSpriteOpacity = V; })
										.IsEnabled_Lambda([this]() { return bShowReferenceSprite && ReferenceFlipbookIndex != INDEX_NONE; })
									]
								]
								+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 0, 0, 2)
								[
									SNew(SCheckBox)
									.IsChecked_Lambda([this]() { return bShowReferenceSprite && ReferenceFlipbookIndex != INDEX_NONE ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
									.OnCheckStateChanged_Lambda([this](ECheckBoxState S) {
										if (S == ECheckBoxState::Checked)
										{
											if (ReferenceFlipbookIndex == INDEX_NONE)
												SetReferenceSprite(SelectedFlipbookIndex, SelectedFrameIndex);
											else
												bShowReferenceSprite = true;
										}
										else
										{
											bShowReferenceSprite = false;
										}
									})
								]
								+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 2, 0, 0)
								[
									SNew(SButton)
									.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
									.ContentPadding(FMargin(4, 1))
									.ToolTipText(LOCTEXT("SetRefTip", "Set the current frame as the reference"))
									.OnClicked_Lambda([this]()
									{
										SetReferenceSprite(SelectedFlipbookIndex, SelectedFrameIndex);
										return FReply::Handled();
									})
									[
										SNew(STextBlock)
										.Text(LOCTEXT("SetRef", "Set"))
										.Font(FCoreStyle::GetDefaultFontStyle("Regular", 7))
									]
								]
								+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 2, 0, 0)
								[
									SNew(SButton)
									.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
									.ContentPadding(FMargin(4, 1))
									.ToolTipText(LOCTEXT("GoToRefTip", "Navigate to the reference flipbook and frame"))
									.IsEnabled_Lambda([this]() { return ReferenceFlipbookIndex != INDEX_NONE; })
									.OnClicked_Lambda([this]()
									{
										if (ReferenceFlipbookIndex != INDEX_NONE)
										{
											OnFlipbookSelected(ReferenceFlipbookIndex);
											OnFrameSelected(ReferenceFrameIndex);
										}
										return FReply::Handled();
									})
									[
										SNew(STextBlock)
										.Text(LOCTEXT("GoToRef", "Go To"))
										.Font(FCoreStyle::GetDefaultFontStyle("Regular", 7))
									]
								]
							]
						]
					]
				]
			]

			// Separator
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(8, 0)
			[
				SNew(SSeparator)
				.Orientation(Orient_Vertical)
			]

			// === Reticle ===
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0, 0, 4, 0)
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([this]() { return bShowSpriteEditorReticle ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this](ECheckBoxState NewState) { bShowSpriteEditorReticle = (NewState == ECheckBoxState::Checked); })
				.ToolTipText(LOCTEXT("ReticleToggleTooltip", "Show alignment reticle. Alt+left-drag on canvas to reposition."))
				[
					SNew(STextBlock)
					.Text(LOCTEXT("ReticleLabel", "Reticle"))
				]
			]

			// Spacer
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SNullWidget::NullWidget
			]
		];
}

TSharedRef<SWidget> SCharacterProfileAssetEditor::BuildSpriteEditorFlipbookList()
{
	return SNew(SVerticalBox)

		// Search bar
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0, 0, 0, 4)
		[
			SAssignNew(SpriteEditorFlipbookSearchBox, SSearchBox)
			.HintText(LOCTEXT("SearchFlipbooks", "Search..."))
			.OnTextChanged_Lambda([this](const FText& NewText) {
				SpriteEditorFlipbookSearchFilter = NewText.ToString();

				// Debounce: cancel previous timer, start new one
				if (SpriteEditorFlipbookSearchDebounceTimer.IsValid())
				{
					UnRegisterActiveTimer(SpriteEditorFlipbookSearchDebounceTimer.Pin().ToSharedRef());
				}
				SpriteEditorFlipbookSearchDebounceTimer = RegisterActiveTimer(0.2f,
					FWidgetActiveTimerDelegate::CreateLambda(
						[this](double, float) {
							RefreshSpriteEditorFlipbookList();
							return EActiveTimerReturnType::Stop;
						}));
			})
		]

		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			[
				SAssignNew(SpriteEditorFlipbookListBox, SVerticalBox)
			]
		];
}

TSharedRef<SWidget> SCharacterProfileAssetEditor::BuildSpriteEditorFrameList()
{
	SAssignNew(SpriteEditorFrameListBox, SHorizontalBox);

	return SNew(SVerticalBox)

		// Excluded count + restore — only visible when frames are excluded
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SHorizontalBox)
			.Visibility_Lambda([this]() -> EVisibility
			{
				return (Asset.IsValid() && Asset->GetExcludedFlipbookFrameCount(SelectedFlipbookIndex) > 0)
					? EVisibility::Visible : EVisibility::Collapsed;
			})
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text_Lambda([this]() {
					const int32 ExcludedCount = Asset.IsValid()
						? Asset->GetExcludedFlipbookFrameCount(SelectedFlipbookIndex)
						: 0;
					return FText::Format(LOCTEXT("ExcludedCountLabel", "Excluded: {0}"), FText::AsNumber(ExcludedCount));
				})
				.ColorAndOpacity(FSlateColor(FLinearColor(0.7f, 0.7f, 0.7f)))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SComboButton)
				.ToolTipText(LOCTEXT("RestoreExcludedSpriteEditorTooltip", "Restore excluded frames back into this flipbook"))
				.OnGetMenuContent_Lambda([this]() { return BuildSpriteEditorRestoreExcludedMenu(); })
				.ButtonContent()
				[
					SNew(STextBlock)
					.Text(LOCTEXT("RestoreExcludedFramesShort", "Restore"))
				]
			]
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.MaxHeight(70.0f)
		[
			SNew(SScrollBox)
			.Orientation(Orient_Horizontal)
			+ SScrollBox::Slot()
			[
				SpriteEditorFrameListBox.ToSharedRef()
			]
		];
}

TSharedRef<SWidget> SCharacterProfileAssetEditor::BuildPlaybackQueuePanel()
{
	TSharedPtr<SQueueEntryDragDropWrapper> EmptyQueueDropTarget;

	TSharedRef<SWidget> Panel = SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0, 0, 0, 4)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("PlaybackQueue", "PLAYBACK QUEUE"))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
				.ToolTipText(LOCTEXT("ClearQueue", "Clear queue"))
				.OnClicked_Lambda([this]() { ClearPlaybackQueue(); return FReply::Handled(); })
				.IsEnabled_Lambda([this]() { return PlaybackQueue.Num() > 0; })
				[
					SNew(STextBlock).Text(LOCTEXT("Clear", "Clear"))
				]
			]
		]

		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SNew(SOverlay)

			+ SOverlay::Slot()
			[
				SNew(SScrollBox)
				+ SScrollBox::Slot()
				[
					SAssignNew(PlaybackQueueListBox, SVerticalBox)
				]
			]

			// Empty state: full-area drop target (accepts flipbook drags when queue is empty)
			+ SOverlay::Slot()
			[
				SAssignNew(EmptyQueueDropTarget, SQueueEntryDragDropWrapper)
				.Visibility_Lambda([this]() {
					return PlaybackQueue.Num() == 0 ? EVisibility::Visible : EVisibility::Collapsed;
				})
				[
					SNew(SBox)
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("DragHint", "Drag flipbooks here"))
						.ColorAndOpacity(FSlateColor::UseSubduedForeground())
					]
				]
			]
		];

	// Wire up the empty-state drop target
	if (EmptyQueueDropTarget.IsValid())
	{
		EmptyQueueDropTarget->QueueIndex = 0;
		EmptyQueueDropTarget->OnAnimsDroppedFunc = [this](const TArray<int32>& FlipbookIndices, int32 InsertAt) {
			if (!Asset.IsValid()) return;
			PushQueueUndoSnapshot();
			for (int32 Idx : FlipbookIndices)
			{
				if (Asset->Flipbooks.IsValidIndex(Idx))
				{
					PlaybackQueue.Add(Idx);
				}
			}
			RefreshPlaybackQueueList();
		};
	}

	return Panel;
}

TSharedRef<SWidget> SCharacterProfileAssetEditor::BuildSpriteEditorCanvasArea()
{
	TSharedRef<SBorder> Border = SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(0)
		[
			SAssignNew(SpriteEditorCanvas, SSpriteEditorCanvas)
			.Asset(Asset.Get())
			.SelectedFlipbookIndex_Lambda([this]() { return SelectedFlipbookIndex; })
			.SelectedFrameIndex_Lambda([this]() { return SelectedFrameIndex; })
			.Zoom_Lambda([this]() { return SpriteEditorZoomLevel; })
			.ShowOnionSkin_Lambda([this]() { return bShowOnionSkin; })
			.OnionSkinFrames_Lambda([this]() { return OnionSkinFrames; })
			.ForwardOnionSkinFrames_Lambda([this]() { return ForwardOnionSkinFrames; })
			.OnionSkinOpacity_Lambda([this]() { return OnionSkinOpacity; })
			// Cross-flipbook onion skin shows frames from the PREVIOUS/NEXT queue
			// entry ONLY when a playback queue is active on this tab. Without a
			// queue, GetQueueAdjacentFlipbookIndex returns INDEX_NONE, the canvas
			// falls back to same-flipbook onion only, and no purple neighbor
			// frames leak in. This is the contract — do not substitute
			// GetVisualAdjacentFlipbookIndex here; that would re-introduce the
			// "ghost frames from a different flipbook" bug.
			.PreviousFlipbookIndex_Lambda([this]() { return GetQueueAdjacentFlipbookIndex(-1); })
			.ShowForwardOnionSkin_Lambda([this]() { return bShowForwardOnionSkin; })
			.NextFlipbookIndex_Lambda([this]() { return GetQueueAdjacentFlipbookIndex(1); })
			.ShowReticle_Lambda([this]() { return bShowSpriteEditorReticle; })
			.ReticlePosition_Lambda([this]() { return SpriteEditorReticlePos; })
			.FlipX_Lambda([this]() { return bSpriteFlipX; })
			.FlipY_Lambda([this]() { return bSpriteFlipY; })
			.ShowReferenceSprite_Lambda([this]() { return bShowReferenceSprite && ReferenceFlipbookIndex != INDEX_NONE; })
			.ReferenceSprite_Lambda([this]() -> TWeakObjectPtr<UPaperSprite> {
				if (!bShowReferenceSprite || !Asset.IsValid()) return nullptr;
				if (!Asset->Flipbooks.IsValidIndex(ReferenceFlipbookIndex)) return nullptr;
				const FFlipbookProfileEntry& Anim = Asset->Flipbooks[ReferenceFlipbookIndex];
				if (Anim.Identity.Flipbook.IsNull()) return nullptr;
				UPaperFlipbook* FB = Anim.Identity.Flipbook.LoadSynchronous();
				if (!FB || ReferenceFrameIndex >= FB->GetNumKeyFrames()) return nullptr;
				return FB->GetKeyFrameChecked(ReferenceFrameIndex).Sprite;
			})
			.ReferenceSpriteOffset_Lambda([this]() -> FIntPoint {
				if (!Asset.IsValid() || !Asset->Flipbooks.IsValidIndex(ReferenceFlipbookIndex)) return FIntPoint::ZeroValue;
				const FFlipbookProfileEntry& Anim = Asset->Flipbooks[ReferenceFlipbookIndex];
				if (!Anim.CombatData.FrameExtractionInfo.IsValidIndex(ReferenceFrameIndex)) return FIntPoint::ZeroValue;
				return Anim.CombatData.FrameExtractionInfo[ReferenceFrameIndex].SpriteOffset;
			})
			.ReferenceSpriteOpacity_Lambda([this]() { return ReferenceSpriteOpacity; })
			.QueueLargestDims_Lambda([this]() -> FIntPoint {
				if (PlaybackQueue.Num() < 2 || !Asset.IsValid())
				{
					return FIntPoint::ZeroValue;
				}
				FIntPoint Largest(1, 1);
				for (int32 Idx : PlaybackQueue)
				{
					if (!Asset->Flipbooks.IsValidIndex(Idx)) continue;
					const FFlipbookProfileEntry& FBData = Asset->Flipbooks[Idx];
					if (FBData.Identity.Flipbook.IsNull()) continue;
					UPaperFlipbook* FB = FBData.Identity.Flipbook.Get();
					if (!FB) continue;
					for (int32 i = 0; i < FB->GetNumKeyFrames(); ++i)
					{
						if (UPaperSprite* S = FB->GetKeyFrameChecked(i).Sprite)
						{
							FVector2D Sz = S->GetSourceSize();
							Largest.X = FMath::Max(Largest.X, FMath::RoundToInt(Sz.X));
							Largest.Y = FMath::Max(Largest.Y, FMath::RoundToInt(Sz.Y));
						}
					}
				}
				return Largest;
			})
			.ExcludedPreviewSprite_Lambda([this]() -> TWeakObjectPtr<UPaperSprite> {
				if (SelectedExcludedFrameIndex == INDEX_NONE || !Asset.IsValid()
					|| !Asset->Flipbooks.IsValidIndex(SelectedFlipbookIndex)) return nullptr;
				const auto& Excluded = Asset->Flipbooks[SelectedFlipbookIndex].CombatData.ExcludedFrames;
				if (!Excluded.IsValidIndex(SelectedExcludedFrameIndex)) return nullptr;
				return Excluded[SelectedExcludedFrameIndex].KeyFrame.Sprite;
			})
			.ExcludedPreviewOffset_Lambda([this]() -> FIntPoint {
				if (SelectedExcludedFrameIndex == INDEX_NONE || !Asset.IsValid()
					|| !Asset->Flipbooks.IsValidIndex(SelectedFlipbookIndex)) return FIntPoint::ZeroValue;
				const auto& Excluded = Asset->Flipbooks[SelectedFlipbookIndex].CombatData.ExcludedFrames;
				if (!Excluded.IsValidIndex(SelectedExcludedFrameIndex)) return FIntPoint::ZeroValue;
				return Excluded[SelectedExcludedFrameIndex].ExtractionInfo.SpriteOffset;
			})
		];

	// Wire up canvas delegates
	if (SpriteEditorCanvas.IsValid())
	{
		SpriteEditorCanvas->OnOffsetChanged.BindSP(this, &SCharacterProfileAssetEditor::OnSpriteEditorOffsetChanged);
		SpriteEditorCanvas->OnReticlePositionChanged.BindLambda([this](FVector2D NewPos) {
			SpriteEditorReticlePos = NewPos;
		});
		SpriteEditorCanvas->OnZoomChanged.BindLambda([this](float NewZoom) {
			SpriteEditorZoomLevel = NewZoom;
		});
		SpriteEditorCanvas->OnDragStarted.BindLambda([this]() {
			bSpriteEditorDragActive = true;
			// Transaction deferred until first actual offset change in NudgeOffset
		});
		SpriteEditorCanvas->OnDragEnded.BindLambda([this]() {
			if (bSpriteEditorDragActive)
			{
				if (ActiveTransaction.IsValid())
				{
					EndTransaction();
				}
				bSpriteEditorDragActive = false;
			}
		});
	}

	return Border;
}

TSharedRef<SWidget> SCharacterProfileAssetEditor::BuildOffsetControlsPanel()
{
	TSharedPtr<SVerticalBox> ControlsBox;
	SAssignNew(ControlsBox, SVerticalBox)

		// === Section: Offset Values ===
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0, 0, 0, 4)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("OffsetControls", "OFFSET"))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0, 0, 0, 12)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
			.Padding(8)
			[
				SNew(SVerticalBox)

				// X offset
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0, 0, 0, 4)
				[
					SNew(SHorizontalBox)
					.ToolTipText(LOCTEXT("OffsetXTooltip", "Horizontal offset in pixels. Positive values move the sprite right relative to the anchor point."))

					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0, 0, 8, 0)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("OffsetX", "X:"))
					]

					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					[
						SNew(SSpinBox<int32>)
						.MinValue(-500)
						.MaxValue(500)
						.Value_Lambda([this]() {
							const FFlipbookProfileEntry* Anim = GetCurrentFlipbookData();
							if (Anim && Anim->CombatData.FrameExtractionInfo.IsValidIndex(SelectedFrameIndex))
							{
								return Anim->CombatData.FrameExtractionInfo[SelectedFrameIndex].SpriteOffset.X;
							}
							return 0;
						})
						.OnValueChanged_Lambda([this](int32 NewValue) { OnOffsetXChanged(NewValue); })
					]
				]

				// Y offset
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0, 0, 0, 4)
				[
					SNew(SHorizontalBox)
					.ToolTipText(LOCTEXT("OffsetYTooltip", "Vertical offset in pixels. Positive values move the sprite down relative to the anchor point."))

					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0, 0, 8, 0)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("OffsetY", "Y:"))
					]

					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					[
						SNew(SSpinBox<int32>)
						.MinValue(-500)
						.MaxValue(500)
						.Value_Lambda([this]() {
							const FFlipbookProfileEntry* Anim = GetCurrentFlipbookData();
							if (Anim && Anim->CombatData.FrameExtractionInfo.IsValidIndex(SelectedFrameIndex))
							{
								return Anim->CombatData.FrameExtractionInfo[SelectedFrameIndex].SpriteOffset.Y;
							}
							return 0;
						})
						.OnValueChanged_Lambda([this](int32 NewValue) { OnOffsetYChanged(NewValue); })
					]
				]

				// Reset offset
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
					.HAlign(HAlign_Center)
					.ToolTipText(LOCTEXT("ResetOffsetTooltip", "Reset offset to zero"))
					.OnClicked_Lambda([this]() { OnResetOffset(); return FReply::Handled(); })
					[
						SNew(STextBlock)
						.Text(LOCTEXT("ResetOffset", "Reset Offset"))
					]
				]
			]
		]

		// === Section: Nudge Controls ===
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0, 0, 0, 4)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("NudgeControls", "NUDGE"))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0, 0, 0, 4)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("NudgeHint", "WASD or Arrows (Shift = 10px)"))
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
			.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)))
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0, 0, 0, 12)
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Center)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
				.ToolTipText(LOCTEXT("NudgeUpTooltip", "Nudge Up (W or Up Arrow)"))
				.OnClicked_Lambda([this]() { NudgeOffset(0, -1); return FReply::Handled(); })
				[
					SNew(SBox)
					.WidthOverride(28)
					.HAlign(HAlign_Center)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("NudgeUp", "W"))
					]
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Center)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0, 0, 2, 0)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
					.ToolTipText(LOCTEXT("NudgeLeftTooltip", "Nudge Left (A)"))
					.OnClicked_Lambda([this]() { NudgeOffset(-1, 0); return FReply::Handled(); })
					[
						SNew(SBox)
						.WidthOverride(28)
						.HAlign(HAlign_Center)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("NudgeLeft", "A"))
						]
					]
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0, 0, 2, 0)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
					.ToolTipText(LOCTEXT("NudgeDownTooltip", "Nudge Down (S or Down Arrow)"))
					.OnClicked_Lambda([this]() { NudgeOffset(0, 1); return FReply::Handled(); })
					[
						SNew(SBox)
						.WidthOverride(28)
						.HAlign(HAlign_Center)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("NudgeDown", "S"))
						]
					]
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
					.ToolTipText(LOCTEXT("NudgeRightTooltip", "Nudge Right (D)"))
					.OnClicked_Lambda([this]() { NudgeOffset(1, 0); return FReply::Handled(); })
					[
						SNew(SBox)
						.WidthOverride(28)
						.HAlign(HAlign_Center)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("NudgeRight", "D"))
						]
					]
				]
			]
		]

		// === Section: Clipboard ===
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0, 0, 0, 4)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("Clipboard", "CLIPBOARD"))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0, 0, 0, 12)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.Padding(0, 0, 2, 0)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
				.HAlign(HAlign_Center)
				.ToolTipText(LOCTEXT("CopyOffsetTooltip", "Copy the current frame's offset to the clipboard for pasting to other frames."))
				.OnClicked_Lambda([this]() { OnCopyOffset(); return FReply::Handled(); })
				[
					SNew(STextBlock)
					.Text(LOCTEXT("Copy", "Copy"))
				]
			]

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.Padding(2, 0, 0, 0)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
				.HAlign(HAlign_Center)
				.ToolTipText(LOCTEXT("PasteOffsetTooltip", "Apply the copied offset to the current frame."))
				.IsEnabled_Lambda([this]() { return bHasCopiedOffset; })
				.OnClicked_Lambda([this]() { OnPasteOffset(); return FReply::Handled(); })
				[
					SNew(STextBlock)
					.Text(LOCTEXT("Paste", "Paste"))
				]
			]
		]

		// === Section: Batch Offset Tools ===
		;

		// Build combos before the Slate chain (lambdas can't be inside Slate [ ] content blocks)
		TSharedPtr<TArray<TSharedPtr<FString>>> AlignActionOptions = MakeShared<TArray<TSharedPtr<FString>>>();
		AlignActionOptions->Add(MakeShared<FString>(TEXT("Current Frame's Offset")));
		AlignActionOptions->Add(MakeShared<FString>(TEXT("Reset (0, 0)")));
		AlignActionOptions->Add(MakeShared<FString>(TEXT("Mirror X")));
		AlignActionOptions->Add(MakeShared<FString>(TEXT("Mirror Y")));
		AlignActionOptions->Add(MakeShared<FString>(TEXT("Custom Offset")));

		TSharedPtr<TArray<TSharedPtr<FString>>> AlignTargetOptions = MakeShared<TArray<TSharedPtr<FString>>>();
		AlignTargetOptions->Add(MakeShared<FString>(TEXT("All Frames")));
		AlignTargetOptions->Add(MakeShared<FString>(TEXT("Selected Frames")));
		AlignTargetOptions->Add(MakeShared<FString>(TEXT("Remaining Frames")));
		AlignTargetOptions->Add(MakeShared<FString>(TEXT("Custom Range")));

		auto MakeAlignCombo = [](TSharedPtr<TArray<TSharedPtr<FString>>> Options, int32* SelectedIdx) -> TSharedRef<SWidget>
		{
			return SNew(SComboBox<TSharedPtr<FString>>)
				.OptionsSource(Options.Get())
				.OnSelectionChanged_Lambda([SelectedIdx, Options](TSharedPtr<FString> Item, ESelectInfo::Type)
				{
					if (Item.IsValid() && Options.IsValid())
					{
						*SelectedIdx = Options->IndexOfByKey(Item);
					}
				})
				.OnGenerateWidget_Lambda([](TSharedPtr<FString> Item) -> TSharedRef<SWidget>
				{
					return SNew(STextBlock).Text(FText::FromString(*Item)).Font(FCoreStyle::GetDefaultFontStyle("Regular", 8));
				})
				.InitiallySelectedItem((*Options)[*SelectedIdx])
				[
					SNew(STextBlock)
					.Text_Lambda([Options, SelectedIdx]() -> FText
					{
						return Options->IsValidIndex(*SelectedIdx) ? FText::FromString(*(*Options)[*SelectedIdx]) : FText();
					})
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
				];
		};

		ControlsBox->AddSlot()
		.AutoHeight()
		.Padding(0, 0, 0, 4)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("BatchOffsetTools", "Batch Offset Tools"))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
		];

		// "Apply" [Action v]
		ControlsBox->AddSlot()
		.AutoHeight()
		.Padding(0, 2)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
			[
				SNew(STextBlock).Text(LOCTEXT("AlignApplyLabel", "Apply")).Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0, 0, 4, 0)
			[
				MakeAlignCombo(AlignActionOptions, &AlignBatchActionIndex)
			]
		];

		// Custom offset X/Y spinboxes
		ControlsBox->AddSlot()
		.AutoHeight()
		.Padding(16, 2, 0, 2)
		[
			SNew(SBox)
			.Visibility_Lambda([this]() { return AlignBatchActionIndex == 4 ? EVisibility::Visible : EVisibility::Collapsed; })
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
				[
					SNew(STextBlock).Text(LOCTEXT("CustomX", "X:")).Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
				]
				+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0, 0, 8, 0)
				[
					SNew(SSpinBox<int32>).MinValue(-999).MaxValue(999)
					.Value_Lambda([this]() { return AlignBatchCustomValue.X; })
					.OnValueChanged_Lambda([this](int32 V) { AlignBatchCustomValue.X = V; })
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
				[
					SNew(STextBlock).Text(LOCTEXT("CustomY", "Y:")).Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
				]
				+ SHorizontalBox::Slot().FillWidth(1.0f)
				[
					SNew(SSpinBox<int32>).MinValue(-999).MaxValue(999)
					.Value_Lambda([this]() { return AlignBatchCustomValue.Y; })
					.OnValueChanged_Lambda([this](int32 V) { AlignBatchCustomValue.Y = V; })
				]
			]
		];

		// "to" [Target v]
		ControlsBox->AddSlot()
		.AutoHeight()
		.Padding(0, 2)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
			[
				SNew(STextBlock).Text(LOCTEXT("AlignToLabel", "to")).Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0, 0, 4, 0)
			[
				MakeAlignCombo(AlignTargetOptions, &AlignBatchTargetIndex)
			]
		];

		// Custom range spinboxes (visible when "Custom Range" selected)
		ControlsBox->AddSlot()
		.AutoHeight()
		.Padding(16, 2, 0, 2)
		[
			SNew(SBox)
			.Visibility_Lambda([this]() { return AlignBatchTargetIndex == 3 ? EVisibility::Visible : EVisibility::Collapsed; })
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
				[
					SNew(STextBlock).Text(LOCTEXT("AlignRangeFrom", "From:")).Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
				]
				+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0, 0, 8, 0)
				[
					SNew(SSpinBox<int32>).MinValue(0)
					.MaxValue_Lambda([this]() { return FMath::Max(0, GetCurrentFrameCount() - 1); })
					.Value_Lambda([this]() { return AlignBatchRangeStart; })
					.OnValueChanged_Lambda([this](int32 V) { AlignBatchRangeStart = V; })
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
				[
					SNew(STextBlock).Text(LOCTEXT("AlignRangeTo", "To:")).Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
				]
				+ SHorizontalBox::Slot().FillWidth(1.0f)
				[
					SNew(SSpinBox<int32>).MinValue(0)
					.MaxValue_Lambda([this]() { return FMath::Max(0, GetCurrentFrameCount() - 1); })
					.Value_Lambda([this]() { return AlignBatchRangeEnd; })
					.OnValueChanged_Lambda([this](int32 V) { AlignBatchRangeEnd = V; })
				]
			]
		];

		// Apply button
		ControlsBox->AddSlot()
		.AutoHeight()
		.Padding(0, 4, 0, 12)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
			.HAlign(HAlign_Center)
			.IsEnabled_Lambda([this]()
			{
				return AlignBatchTargetIndex != 1 || SelectedFrames.Num() > 0;
			})
			.OnClicked_Lambda([this]() { OnApplyAlignmentBatchOperation(); return FReply::Handled(); })
			[
				SNew(STextBlock)
				.Text(LOCTEXT("AlignBatchApply", "Apply"))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
			]
		];

	// Spacer
	ControlsBox->AddSlot()
	.FillHeight(1.0f)
	[
		SNullWidget::NullWidget
	];

	return ControlsBox.ToSharedRef();
}

void SCharacterProfileAssetEditor::RefreshSpriteEditorFlipbookList()
{
	if (!SpriteEditorFlipbookListBox.IsValid() || !Asset.IsValid()) return;

	SpriteEditorFlipbookListBox->ClearChildren();
	SpriteEditorFlipbookNameTexts.Empty();

	// Filter for search
	TFunction<bool(int32)> SearchFilter = nullptr;
	if (!SpriteEditorFlipbookSearchFilter.IsEmpty())
	{
		SearchFilter = [this](int32 Idx) -> bool
		{
			return Asset->Flipbooks[Idx].Identity.FlipbookName.Contains(SpriteEditorFlipbookSearchFilter, ESearchCase::IgnoreCase);
		};
	}

	BuildGroupedFlipbookList(SpriteEditorFlipbookListBox, [this](int32 i) -> TSharedRef<SWidget>
	{
		const FFlipbookProfileEntry& Anim = Asset->Flipbooks[i];
		UPaperFlipbook* LoadedFlipbook = !Anim.Identity.Flipbook.IsNull() ? Anim.Identity.Flipbook.LoadSynchronous() : nullptr;
		const bool bHasFlipbook = LoadedFlipbook != nullptr;
		const int32 FrameCount = bHasFlipbook ? LoadedFlipbook->GetNumKeyFrames() : Anim.CombatData.Frames.Num();
		const FText SourceNameText = FText::FromString(bHasFlipbook ? Anim.Identity.Flipbook.GetAssetName() : TEXT("No Flipbook Assigned"));

		TSharedPtr<SInlineEditableTextBlock> NameText;
		TSharedPtr<SDragClickWrapper> Wrapper;

		TSharedRef<SWidget> Item = SAssignNew(Wrapper, SDragClickWrapper)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("NoBorder"))
				.Padding(2)
				.ToolTip(
					SNew(SToolTip)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0, 0, 0, 6)
						[
							SNew(STextBlock)
							.Text(FText::FromString(Anim.Identity.FlipbookName))
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
											.Text(LOCTEXT("NoFlipbookTooltipPreview", "No FB"))
											.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
											.ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f)))
										])
							]
						]
					])
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::GetBrush("NoBorder"))
					.ForegroundColor(FLinearColor::White)
					[
						SNew(SBorder)
						.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
						.BorderBackgroundColor_Lambda([this, i]()
						{
							if (i == SelectedFlipbookIndex)
								return FLinearColor(0.15f, 0.35f, 0.55f, 1.0f);
							if (SpriteEditorSelectedFlipbooks.Contains(i))
								return FLinearColor(0.12f, 0.28f, 0.45f, 1.0f);
							return FLinearColor(0.03f, 0.03f, 0.03f, 1.0f);
						})
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
												.Text(LOCTEXT("NoFlipbookListPreview", "No FB"))
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
										.Text(FText::FromString(Anim.Identity.FlipbookName))
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
										.Text(FText::Format(LOCTEXT("SpriteEditorFrameCount", "{0} frames"), FText::AsNumber(FrameCount)))
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
				]
			];

		FString CapturedFlipbookName = Anim.Identity.FlipbookName;
		Wrapper->OnClickedFunc = [this, i](const FGeometry&, const FPointerEvent& MouseEvent) {
			if (MouseEvent.IsControlDown())
			{
				// Ctrl+click: toggle in multi-select, update primary selection
				if (SpriteEditorSelectedFlipbooks.Contains(i))
				{
					SpriteEditorSelectedFlipbooks.Remove(i);
				}
				else
				{
					SpriteEditorSelectedFlipbooks.Add(i);
				}
				SpriteEditorFlipbookSelectionAnchor = i;
				SelectedFlipbookIndex = i;
				SelectedFrameIndex = 0;
				RefreshSpriteEditorFlipbookList();
				RefreshSpriteEditorFrameList();
			}
			else if (MouseEvent.IsShiftDown() && SpriteEditorFlipbookSelectionAnchor != INDEX_NONE)
			{
				// Shift+click: range select from anchor
				TArray<int32> VisualOrder = GetVisualFlipbookOrder();
				int32 AnchorPos = VisualOrder.IndexOfByKey(SpriteEditorFlipbookSelectionAnchor);
				int32 ClickPos = VisualOrder.IndexOfByKey(i);
				if (AnchorPos != INDEX_NONE && ClickPos != INDEX_NONE)
				{
					int32 Start = FMath::Min(AnchorPos, ClickPos);
					int32 End = FMath::Max(AnchorPos, ClickPos);
					for (int32 Pos = Start; Pos <= End; ++Pos)
					{
						SpriteEditorSelectedFlipbooks.Add(VisualOrder[Pos]);
					}
				}
				SelectedFlipbookIndex = i;
				SelectedFrameIndex = 0;
				RefreshSpriteEditorFlipbookList();
				RefreshSpriteEditorFrameList();
			}
			else
			{
				// Plain click: clear multi-select, single select
				SpriteEditorSelectedFlipbooks.Empty();
				SpriteEditorFlipbookSelectionAnchor = i;
				OnFlipbookSelected(i);
				RefreshSpriteEditorFrameList();
			}
		};
		Wrapper->OnRightClickedFunc = [this, i](const FGeometry&, const FPointerEvent&) {
			// If right-clicking an item not in the multi-select, clear and select just that one
			if (SpriteEditorSelectedFlipbooks.Num() > 0 && !SpriteEditorSelectedFlipbooks.Contains(i))
			{
				SpriteEditorSelectedFlipbooks.Empty();
				RefreshSpriteEditorFlipbookList();
			}
			ShowFlipbookContextMenu(i);
		};
		Wrapper->OnDragDetectedFunc = [this, i, CapturedFlipbookName]() -> TSharedPtr<FDragDropOperation> {
			// Multi-select drag: if this item is part of a selection, drag all selected
			if (SpriteEditorSelectedFlipbooks.Contains(i) && SpriteEditorSelectedFlipbooks.Num() > 1)
			{
				// Return in visual order for predictable queue ordering
				TArray<int32> VisualOrder = GetVisualFlipbookOrder();
				TArray<int32> Result;
				for (int32 Idx : VisualOrder)
				{
					if (SpriteEditorSelectedFlipbooks.Contains(Idx))
					{
						Result.Add(Idx);
					}
				}
				if (Result.Num() > 1)
				{
					FString Label = FString::Printf(TEXT("%d flipbooks"), Result.Num());
					return FQueueDragDropOp::NewFromFlipbookListMulti(Result, Label);
				}
			}
			return FQueueDragDropOp::NewFromFlipbookList(i, CapturedFlipbookName);
		};

		SpriteEditorFlipbookNameTexts.Add(i, NameText);
		return Item;
	}, SearchFilter);

	// Trigger pending rename
	TriggerPendingRenameIfNeeded(SpriteEditorFlipbookNameTexts);
}

void SCharacterProfileAssetEditor::RefreshPlaybackQueueList()
{
	if (!PlaybackQueueListBox.IsValid() || !Asset.IsValid()) return;

	bool bPurgedInvalidEntries = false;
	for (int32 i = PlaybackQueue.Num() - 1; i >= 0; --i)
	{
		if (!Asset->Flipbooks.IsValidIndex(PlaybackQueue[i]))
		{
			if (i < PlaybackQueueIndex)
			{
				PlaybackQueueIndex--;
			}
			else if (i == PlaybackQueueIndex)
			{
				PlaybackPosition = 0.0f;
				CachedPlaybackTiming = FFlipbookTimingData();
			}
			PlaybackQueue.RemoveAt(i);
			bPurgedInvalidEntries = true;
		}
	}

	if (bPurgedInvalidEntries)
	{
		if (bIsPlaying)
		{
			StopPlayback();
			return;
		}
		if (PlaybackQueue.Num() == 0)
		{
			PlaybackQueueIndex = 0;
			PlaybackPosition = 0.0f;
			CachedPlaybackTiming = FFlipbookTimingData();
		}
		else
		{
			PlaybackQueueIndex = FMath::Clamp(PlaybackQueueIndex, 0, PlaybackQueue.Num() - 1);
		}
	}

	PlaybackQueueListBox->ClearChildren();

	for (int32 QueueIdx = 0; QueueIdx < PlaybackQueue.Num(); QueueIdx++)
	{
		int32 AnimIdx = PlaybackQueue[QueueIdx];

		const FFlipbookProfileEntry& Anim = Asset->Flipbooks[AnimIdx];
		bool bIsActive = bIsPlaying && QueueIdx == PlaybackQueueIndex;
		bool bIsCurrentSelection = (AnimIdx == SelectedFlipbookIndex);
		UPaperFlipbook* LoadedFlipbook = !Anim.Identity.Flipbook.IsNull() ? Anim.Identity.Flipbook.LoadSynchronous() : nullptr;
		const bool bHasFlipbook = LoadedFlipbook != nullptr;
		const int32 FrameCount = bHasFlipbook ? LoadedFlipbook->GetNumKeyFrames() : Anim.CombatData.Frames.Num();

		// Background color: green for actively playing, blue for selected, dark default
		FLinearColor BgColor = FLinearColor(0.03f, 0.03f, 0.03f, 1.0f);
		if (bIsActive)
		{
			BgColor = FLinearColor(0.1f, 0.35f, 0.1f, 1.0f);
		}
		else if (bIsCurrentSelection)
		{
			BgColor = FLinearColor(0.15f, 0.35f, 0.55f, 1.0f);
		}

		TSharedPtr<SQueueEntryDragDropWrapper> Wrapper;

		PlaybackQueueListBox->AddSlot()
		.AutoHeight()
		.Padding(0, 0, 0, 2)
		[
			SAssignNew(Wrapper, SQueueEntryDragDropWrapper)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
				.BorderBackgroundColor(BgColor)
				.Padding(FMargin(8, 6))
				[
					SNew(SHorizontalBox)

					// Queue index number
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0, 0, 6, 0)
					[
						SNew(STextBlock)
						.Text(FText::AsNumber(QueueIdx + 1))
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
						.ColorAndOpacity(FSlateColor(FLinearColor(0.4f, 0.4f, 0.4f)))
					]

					// Flipbook thumbnail
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0, 0, 8, 0)
					[
						SNew(SBox)
						.WidthOverride(36)
						.HeightOverride(36)
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
										.Text(LOCTEXT("NoFlipbookQueuePreview", "?"))
										.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
										.ColorAndOpacity(FSlateColor(FLinearColor(0.55f, 0.55f, 0.55f)))
									])
						]
					]

					// Name + frame count
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.VAlign(VAlign_Center)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(STextBlock)
							.Text(FText::FromString(Anim.Identity.FlipbookName))
							.Font(bIsActive ? FCoreStyle::GetDefaultFontStyle("Bold", 9) : FCoreStyle::GetDefaultFontStyle("Regular", 9))
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0, 1, 0, 0)
						[
							SNew(STextBlock)
							.Text(FText::Format(LOCTEXT("QueueFrameCount", "{0} frames"), FText::AsNumber(FrameCount)))
							.Font(FAppStyle::GetFontStyle("SmallFont"))
							.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)))
						]
					]

					// Remove button
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(SButton)
						.ButtonStyle(FAppStyle::Get(), "SimpleButton")
						.ToolTipText(LOCTEXT("RemoveFromQueue", "Remove from queue"))
						.OnClicked_Lambda([this, QueueIdx]() {
							RemoveFromPlaybackQueue(QueueIdx);
							return FReply::Handled();
						})
						[
							SNew(STextBlock)
							.Text(FText::FromString(TEXT("X")))
							.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
							.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)))
						]
					]
				]
			]
		];

		Wrapper->QueueIndex = QueueIdx;
		Wrapper->FlipbookIndex = AnimIdx;
		Wrapper->FlipbookName = Anim.Identity.FlipbookName;
		Wrapper->OnClickedFunc = [this, QueueIdx, AnimIdx]() {
			PlaybackQueueIndex = QueueIdx;
			OnFlipbookSelected(AnimIdx);
			RefreshSpriteEditorFlipbookList();
			RefreshSpriteEditorFrameList();
		};
		Wrapper->OnRightClickFunc = [this, QueueIdx, AnimIdx]() {
			FMenuBuilder MenuBuilder(true, nullptr);

			MenuBuilder.AddMenuEntry(
				LOCTEXT("QueueOpenFlipbookAsset", "Open Flipbook Asset"),
				LOCTEXT("QueueOpenFlipbookAssetTooltip", "Open this queued flipbook asset in its editor"),
				FSlateIcon(),
				FUIAction(
					FExecuteAction::CreateLambda([this, AnimIdx]() { OpenFlipbookAssetEditor(AnimIdx); }),
					FCanExecuteAction::CreateLambda([this, AnimIdx]() { return GetFlipbookAssetForIndex(AnimIdx) != nullptr; })
				)
			);

			MenuBuilder.AddMenuEntry(
				LOCTEXT("QueueBrowseToFlipbookAsset", "Browse to Flipbook in Content Browser"),
				LOCTEXT("QueueBrowseToFlipbookAssetTooltip", "Sync the Content Browser to this queued flipbook asset"),
				FSlateIcon(),
				FUIAction(
					FExecuteAction::CreateLambda([this, AnimIdx]() { BrowseToFlipbookAssetInContentBrowser(AnimIdx); }),
					FCanExecuteAction::CreateLambda([this, AnimIdx]() { return GetFlipbookAssetForIndex(AnimIdx) != nullptr; })
				)
			);

			MenuBuilder.AddMenuSeparator();

			MenuBuilder.AddMenuEntry(
				LOCTEXT("QueueMoveUp", "Move Up"),
				LOCTEXT("QueueMoveUpTooltip", "Move this entry up in the queue"),
				FSlateIcon(),
				FUIAction(
					FExecuteAction::CreateLambda([this, QueueIdx]() { ReorderQueueEntry(QueueIdx, QueueIdx - 1); }),
					FCanExecuteAction::CreateLambda([QueueIdx]() { return QueueIdx > 0; })
				)
			);

			MenuBuilder.AddMenuEntry(
				LOCTEXT("QueueMoveDown", "Move Down"),
				LOCTEXT("QueueMoveDownTooltip", "Move this entry down in the queue"),
				FSlateIcon(),
				FUIAction(
					FExecuteAction::CreateLambda([this, QueueIdx]() { ReorderQueueEntry(QueueIdx, QueueIdx + 2); }),
					FCanExecuteAction::CreateLambda([this, QueueIdx]() { return QueueIdx < PlaybackQueue.Num() - 1; })
				)
			);

			MenuBuilder.AddMenuSeparator();

			MenuBuilder.AddMenuEntry(
				LOCTEXT("QueueRemove", "Remove"),
				LOCTEXT("QueueRemoveTooltip", "Remove this entry from the queue"),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateLambda([this, QueueIdx]() { RemoveFromPlaybackQueue(QueueIdx); }))
			);

			FSlateApplication::Get().PushMenu(
				SharedThis(this),
				FWidgetPath(),
				MenuBuilder.MakeWidget(),
				FSlateApplication::Get().GetCursorPos(),
				FPopupTransitionEffect::ContextMenu
			);
		};
		Wrapper->OnQueueReorderFunc = [this](int32 From, int32 To) {
			ReorderQueueEntry(From, To);
		};
		Wrapper->OnAnimsDroppedFunc = [this](const TArray<int32>& FlipbookIndices, int32 InsertAt) {
			if (!Asset.IsValid()) return;
			PushQueueUndoSnapshot();
			int32 Offset = 0;
			for (int32 Idx : FlipbookIndices)
			{
				if (Asset->Flipbooks.IsValidIndex(Idx))
				{
					PlaybackQueue.Insert(Idx, InsertAt + Offset);
					if (bIsPlaying && (InsertAt + Offset) <= PlaybackQueueIndex)
					{
						PlaybackQueueIndex++;
					}
					Offset++;
				}
			}
			RefreshPlaybackQueueList();
		};
	}

	// Always-present drop target at end of queue (handles empty queue + append)
	{
		TSharedPtr<SQueueEntryDragDropWrapper> DropTarget;

		PlaybackQueueListBox->AddSlot()
		.AutoHeight()
		[
			SAssignNew(DropTarget, SQueueEntryDragDropWrapper)
			[
				SNew(SBox)
				.HeightOverride(24)
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::GetBrush("NoBorder"))
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(FText::GetEmpty())
						.Font(FCoreStyle::GetDefaultFontStyle("Italic", 8))
						.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)))
					]
				]
			]
		];

		DropTarget->QueueIndex = PlaybackQueue.Num();
		DropTarget->OnAnimsDroppedFunc = [this](const TArray<int32>& FlipbookIndices, int32 InsertAt) {
			if (!Asset.IsValid()) return;
			PushQueueUndoSnapshot();
			int32 Offset = 0;
			for (int32 Idx : FlipbookIndices)
			{
				if (Asset->Flipbooks.IsValidIndex(Idx))
				{
					PlaybackQueue.Insert(Idx, InsertAt + Offset);
					if (bIsPlaying && (InsertAt + Offset) <= PlaybackQueueIndex)
					{
						PlaybackQueueIndex++;
					}
					Offset++;
				}
			}
			RefreshPlaybackQueueList();
		};
		DropTarget->OnQueueReorderFunc = [this](int32 From, int32 To) {
			ReorderQueueEntry(From, To);
		};
	}
}

void SCharacterProfileAssetEditor::RefreshSpriteEditorFrameList()
{
	if (!SpriteEditorFrameListBox.IsValid() || !Asset.IsValid()) return;

	SpriteEditorFrameListBox->ClearChildren();

	const FFlipbookProfileEntry* Anim = GetCurrentFlipbookData();
	if (!Anim) return;

	// Get the live flipbook for active frame sprites.
	UPaperFlipbook* Flipbook = nullptr;
	if (!Anim->Identity.Flipbook.IsNull())
	{
		Flipbook = Anim->Identity.Flipbook.LoadSynchronous();
	}

	const int32 ActiveFrameCount = FMath::Min(GetCurrentFrameCount(), Anim->CombatData.Frames.Num());
	if (ActiveFrameCount > 0)
	{
		SelectedFrameIndex = FMath::Clamp(SelectedFrameIndex, 0, ActiveFrameCount - 1);
	}
	else
	{
		SelectedFrameIndex = 0;
	}

	struct FDisplayedSpriteEditorFrameEntry
	{
		bool bExcluded = false;
		int32 SourceFrameIndex = INDEX_NONE;
		int32 ActiveFrameIndex = INDEX_NONE;
		int32 ExcludedFrameIndex = INDEX_NONE;
		const FFrameHitboxData* FrameData = nullptr;
		const FSpriteExtractionInfo* ExtractionInfo = nullptr;
		UPaperSprite* Sprite = nullptr;
	};

	TArray<FDisplayedSpriteEditorFrameEntry> DisplayEntries;
	DisplayEntries.Reserve(ActiveFrameCount + Anim->CombatData.ExcludedFrames.Num());

	for (int32 ActiveIndex = 0; ActiveIndex < ActiveFrameCount; ++ActiveIndex)
	{
		const FSpriteExtractionInfo* ExtractionInfo = Anim->CombatData.FrameExtractionInfo.IsValidIndex(ActiveIndex)
			? &Anim->CombatData.FrameExtractionInfo[ActiveIndex]
			: nullptr;
		const int32 SourceIndex = (ExtractionInfo && ExtractionInfo->SourceFrameIndex != INDEX_NONE)
			? ExtractionInfo->SourceFrameIndex
			: ActiveIndex;

		UPaperSprite* Sprite = nullptr;
		if (Flipbook && ActiveIndex < Flipbook->GetNumKeyFrames())
		{
			Sprite = Flipbook->GetKeyFrameChecked(ActiveIndex).Sprite;
		}

		FDisplayedSpriteEditorFrameEntry& Entry = DisplayEntries.AddDefaulted_GetRef();
		Entry.bExcluded = false;
		Entry.SourceFrameIndex = SourceIndex;
		Entry.ActiveFrameIndex = ActiveIndex;
		Entry.FrameData = &Anim->CombatData.Frames[ActiveIndex];
		Entry.ExtractionInfo = ExtractionInfo;
		Entry.Sprite = Sprite;
	}

	for (int32 ExcludedIndex = 0; ExcludedIndex < Anim->CombatData.ExcludedFrames.Num(); ++ExcludedIndex)
	{
		const FExcludedFlipbookFrameData& ExcludedFrame = Anim->CombatData.ExcludedFrames[ExcludedIndex];
		const int32 SourceIndex = ExcludedFrame.ExtractionInfo.SourceFrameIndex != INDEX_NONE
			? ExcludedFrame.ExtractionInfo.SourceFrameIndex
			: (ActiveFrameCount + ExcludedIndex);

		FDisplayedSpriteEditorFrameEntry& Entry = DisplayEntries.AddDefaulted_GetRef();
		Entry.bExcluded = true;
		Entry.SourceFrameIndex = SourceIndex;
		Entry.ExcludedFrameIndex = ExcludedIndex;
		Entry.FrameData = &ExcludedFrame.FrameData;
		Entry.ExtractionInfo = &ExcludedFrame.ExtractionInfo;
		Entry.Sprite = ExcludedFrame.KeyFrame.Sprite;
	}

	DisplayEntries.Sort([](const FDisplayedSpriteEditorFrameEntry& A, const FDisplayedSpriteEditorFrameEntry& B)
	{
		if (A.SourceFrameIndex != B.SourceFrameIndex)
		{
			return A.SourceFrameIndex < B.SourceFrameIndex;
		}
		if (A.bExcluded != B.bExcluded)
		{
			return !A.bExcluded;
		}
		if (!A.bExcluded && !B.bExcluded)
		{
			return A.ActiveFrameIndex < B.ActiveFrameIndex;
		}
		return A.ExcludedFrameIndex < B.ExcludedFrameIndex;
	});

	for (const FDisplayedSpriteEditorFrameEntry& Entry : DisplayEntries)
	{
		const int32 DisplayNumber = Entry.SourceFrameIndex != INDEX_NONE ? Entry.SourceFrameIndex + 1 : 0;
		const bool bExcluded = Entry.bExcluded;
		const int32 ActiveFrameIndex = Entry.ActiveFrameIndex;
		const int32 ExcludedFrameIndex = Entry.ExcludedFrameIndex;
		UPaperSprite* CapturedSprite = Entry.Sprite;

		// Build the toggle (exclude/restore) overlay button — visible on hover or when selected/excluded
		TSharedRef<TSharedPtr<SBox>> HoverTargetRef = MakeShared<TSharedPtr<SBox>>();
		TSharedRef<TWeakPtr<SWidget>> CellHoverRef = MakeShared<TWeakPtr<SWidget>>();
		const FText ToggleText = bExcluded ? LOCTEXT("FrameRestoreGlyph", "+") : LOCTEXT("FrameExcludeGlyph", "-");
		const FText ToggleToolTip = bExcluded
			? LOCTEXT("RestoreFrameTooltip", "Restore this frame")
			: LOCTEXT("ExcludeFrameTooltip", "Exclude this frame");

		TSharedRef<SWidget> OverlayContent = SNew(SOverlay)
			// Excluded frame darkening — HitTestInvisible so right-click reaches the cell
			+ SOverlay::Slot()
			[
				bExcluded
					? StaticCastSharedRef<SWidget>(SNew(SBorder)
						.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
						.Padding(0)
						.BorderBackgroundColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.42f))
						.Visibility(EVisibility::HitTestInvisible))
					: StaticCastSharedRef<SWidget>(SNullWidget::NullWidget)
			]
			// Toggle exclude/restore button (top-left)
			+ SOverlay::Slot()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Top)
			.Padding(FMargin(2, 2, 0, 0))
			[
				SAssignNew(*HoverTargetRef, SBox)
				.Visibility_Lambda([this, ActiveFrameIndex, bExcluded, CellHoverRef]() -> EVisibility
				{
					const bool bSel = !bExcluded && (ActiveFrameIndex == SelectedFrameIndex);
					TSharedPtr<SWidget> PinnedCell = CellHoverRef->Pin();
				const bool bCellHovered = !bExcluded && PinnedCell.IsValid() && PinnedCell->IsHovered();
					return (bExcluded || bSel || bCellHovered) ? EVisibility::Visible : EVisibility::Collapsed;
				})
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "NoBorder")
					.ContentPadding(FMargin(0))
					.ToolTipText(ToggleToolTip)
					.IsEnabled(!bExcluded ? (ActiveFrameCount > 1) : true)
					.OnClicked_Lambda([this, ActiveFrameIndex, ExcludedFrameIndex, bExcluded]() -> FReply
					{
						if (bExcluded)
						{
							OnRestoreExcludedSpriteEditorFrame(ExcludedFrameIndex);
						}
						else if (ActiveFrameIndex != INDEX_NONE)
						{
							SelectedFrameIndex = ActiveFrameIndex;
							OnExcludeCurrentSpriteEditorFrame();
						}
						return FReply::Handled();
					})
					[
						SNew(SBorder)
						.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
						.BorderBackgroundColor_Lambda([HoverTargetRef, bExcluded]() -> FSlateColor
						{
							const bool bBtnHovered = HoverTargetRef->IsValid() && (*HoverTargetRef)->IsHovered();
							if (bExcluded)
								return bBtnHovered ? FLinearColor(0.3f, 0.65f, 0.3f, 1.0f) : FLinearColor(0.2f, 0.45f, 0.2f, 1.0f);
							else
								return bBtnHovered ? FLinearColor(0.65f, 0.3f, 0.3f, 1.0f) : FLinearColor(0.45f, 0.2f, 0.2f, 1.0f);
						})
						.Padding(FMargin(1))
						[
							SNew(SBox)
							.WidthOverride(12.0f)
							.HeightOverride(12.0f)
							.HAlign(HAlign_Center)
							.VAlign(VAlign_Center)
							[
								SNew(STextBlock)
								.Text(ToggleText)
								.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
								.ColorAndOpacity(FSlateColor(FLinearColor::White))
								.Justification(ETextJustify::Center)
							]
						]
					]
				]
			];

		FFrameStripCellArgs CellArgs;
		CellArgs.Sprite = CapturedSprite;
		CellArgs.FrameIndex = DisplayNumber;
		CellArgs.IsSelected = [this, ActiveFrameIndex, ExcludedFrameIndex, bExcluded]()
		{
			if (bExcluded)
				return ExcludedFrameIndex == SelectedExcludedFrameIndex;
			return ActiveFrameIndex == SelectedFrameIndex && SelectedExcludedFrameIndex == INDEX_NONE;
		};
		CellArgs.IsMultiSelected = [this, ActiveFrameIndex, bExcluded]()
		{
			return !bExcluded && SelectedFrames.Contains(ActiveFrameIndex);
		};
		CellArgs.OnMouseButtonDown = [this, ActiveFrameIndex, ExcludedFrameIndex, bExcluded, CapturedSprite](const FPointerEvent& MouseEvent) -> FReply
		{
			if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
			{
				ShowSpriteContextMenu(CapturedSprite, MouseEvent.GetScreenSpacePosition(), INDEX_NONE, INDEX_NONE,
					bExcluded ? INDEX_NONE : ActiveFrameIndex,
					bExcluded ? ExcludedFrameIndex : INDEX_NONE);
				return FReply::Handled();
			}
			if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
			{
				if (bExcluded)
				{
					// Select excluded frame for greyed-out preview
					SelectedExcludedFrameIndex = ExcludedFrameIndex;
					ClearFrameSelection();
					RefreshSpriteEditorFrameList();
				}
				else
				{
					SelectedExcludedFrameIndex = INDEX_NONE;
					FrameSelectionUtils::HandleFrameClick(SelectedFrames, FrameSelectionAnchorIndex, ActiveFrameIndex, MouseEvent, GetCurrentFrameCount());
					OnFrameSelected(ActiveFrameIndex);
					RefreshSpriteEditorFrameList();
				}
				return FReply::Handled();
			}
			return FReply::Unhandled();
		};
		CellArgs.Overlay = OverlayContent;

		TSharedRef<SWidget> CellWidget = FFrameStripCellUtils::Build(CellArgs);
		*CellHoverRef = CellWidget;

		if (!bExcluded)
		{
			TSharedPtr<SFrameDragDropWrapper> Wrapper;
			SpriteEditorFrameListBox->AddSlot()
			.AutoWidth()
			.Padding(1, 0)
			[
				SAssignNew(Wrapper, SFrameDragDropWrapper)
				[
					CellWidget
				]
			];

			Wrapper->FrameIndex = ActiveFrameIndex;
			// Click handling moved into the cell's OnMouseButtonDown
			Wrapper->OnClickedFunc = [](const FPointerEvent&) {};
			Wrapper->OnRightClickedFunc = [this, CapturedSprite](int32 ClickedFrameIndex, const FPointerEvent& MouseEvent)
			{
				ShowSpriteContextMenu(CapturedSprite, MouseEvent.GetScreenSpacePosition(), INDEX_NONE, INDEX_NONE, ClickedFrameIndex);
			};
			Wrapper->OnFrameDroppedFunc = [this](int32 From, int32 To)
			{
				ReorderFrame(From, To);
			};
		}
		else
		{
			SpriteEditorFrameListBox->AddSlot()
			.AutoWidth()
			.Padding(1, 0)
			[
				CellWidget
			];
		}
	}

}

void SCharacterProfileAssetEditor::NudgeOffset(int32 DeltaX, int32 DeltaY)
{
	// Pause queue playback during offset editing
	if (bIsPlaying && PlaybackQueue.Num() > 0) StopPlayback();

	FFlipbookProfileEntry* Anim = GetCurrentFlipbookDataMutable();
	if (!Anim) return;

	// Auto-populate FrameExtractionInfo if it doesn't cover this frame
	int32 RequiredSize = SelectedFrameIndex + 1;
	if (Anim->CombatData.FrameExtractionInfo.Num() < RequiredSize)
	{
		Anim->CombatData.FrameExtractionInfo.SetNum(RequiredSize);
	}

	bool bInDragGesture = bSpriteEditorDragActive;

	if (bInDragGesture)
	{
		// Mouse drag — defer transaction start until first actual movement
		if (!ActiveTransaction.IsValid())
		{
			BeginTransaction(LOCTEXT("DragOffset", "Drag Sprite Offset"));
		}
	}
	else
	{
		// Keyboard nudge — keep one transaction open, debounce commit
		if (!ActiveTransaction.IsValid())
		{
			BeginTransaction(LOCTEXT("NudgeOffset", "Nudge Sprite Offset"));
		}

		// Cancel previous debounce timer
		if (TSharedPtr<FActiveTimerHandle> OldTimer = NudgeDebounceTimer.Pin())
		{
			UnRegisterActiveTimer(OldTimer.ToSharedRef());
		}

		// Schedule commit after 300ms idle
		NudgeDebounceTimer = RegisterActiveTimer(0.3f, FWidgetActiveTimerDelegate::CreateLambda(
			[this](double, float) -> EActiveTimerReturnType
			{
				CommitNudgeTransaction();
				return EActiveTimerReturnType::Stop;
			}));
	}

	// Apply to all selected frames (batch), or just the current frame
	auto ApplyOffsetDelta = [&](int32 FrameIndex)
	{
		if (Anim->CombatData.FrameExtractionInfo.Num() <= FrameIndex)
		{
			Anim->CombatData.FrameExtractionInfo.SetNum(FrameIndex + 1);
		}
		Anim->CombatData.FrameExtractionInfo[FrameIndex].SpriteOffset.X += DeltaX;
		Anim->CombatData.FrameExtractionInfo[FrameIndex].SpriteOffset.Y += DeltaY;
		Anim->CombatData.FrameExtractionInfo[FrameIndex].bHasCustomAlignment = true;
	};

	if (SelectedFrames.Num() > 0)
	{
		for (int32 FrameIdx : SelectedFrames)
		{
			ApplyOffsetDelta(FrameIdx);
		}
		// Ensure current frame is also included
		if (!SelectedFrames.Contains(SelectedFrameIndex))
		{
			ApplyOffsetDelta(SelectedFrameIndex);
		}
	}
	else
	{
		ApplyOffsetDelta(SelectedFrameIndex);
	}
}

void SCharacterProfileAssetEditor::CommitNudgeTransaction()
{
	if (ActiveTransaction.IsValid())
	{
		EndTransaction();
	}
}

void SCharacterProfileAssetEditor::OnOffsetXChanged(int32 NewValue)
{
	if (bIsPlaying && PlaybackQueue.Num() > 0) StopPlayback();

	FFlipbookProfileEntry* Anim = GetCurrentFlipbookDataMutable();
	if (!Anim) return;

	int32 RequiredSize = SelectedFrameIndex + 1;
	if (Anim->CombatData.FrameExtractionInfo.Num() < RequiredSize)
	{
		Anim->CombatData.FrameExtractionInfo.SetNum(RequiredSize);
	}

	BeginTransaction(LOCTEXT("ChangeOffsetX", "Change Sprite Offset X"));

	Anim->CombatData.FrameExtractionInfo[SelectedFrameIndex].SpriteOffset.X = NewValue;
	Anim->CombatData.FrameExtractionInfo[SelectedFrameIndex].bHasCustomAlignment = true;

	EndTransaction();
}

void SCharacterProfileAssetEditor::OnOffsetYChanged(int32 NewValue)
{
	if (bIsPlaying && PlaybackQueue.Num() > 0) StopPlayback();

	FFlipbookProfileEntry* Anim = GetCurrentFlipbookDataMutable();
	if (!Anim) return;

	int32 RequiredSize = SelectedFrameIndex + 1;
	if (Anim->CombatData.FrameExtractionInfo.Num() < RequiredSize)
	{
		Anim->CombatData.FrameExtractionInfo.SetNum(RequiredSize);
	}

	BeginTransaction(LOCTEXT("ChangeOffsetY", "Change Sprite Offset Y"));

	Anim->CombatData.FrameExtractionInfo[SelectedFrameIndex].SpriteOffset.Y = NewValue;
	Anim->CombatData.FrameExtractionInfo[SelectedFrameIndex].bHasCustomAlignment = true;

	EndTransaction();
}

void SCharacterProfileAssetEditor::OnCopyOffset()
{
	const FFlipbookProfileEntry* Anim = GetCurrentFlipbookData();
	if (!Anim || !Anim->CombatData.FrameExtractionInfo.IsValidIndex(SelectedFrameIndex))
	{
		CopiedOffset = FIntPoint::ZeroValue;
		bHasCopiedOffset = true;
		return;
	}

	CopiedOffset = Anim->CombatData.FrameExtractionInfo[SelectedFrameIndex].SpriteOffset;
	bHasCopiedOffset = true;
}

void SCharacterProfileAssetEditor::OnPasteOffset()
{
	if (!bHasCopiedOffset) return;

	FFlipbookProfileEntry* Anim = GetCurrentFlipbookDataMutable();
	if (!Anim) return;

	BeginTransaction(LOCTEXT("PasteOffset", "Paste Sprite Offset"));
	Asset->Modify();

	// Apply to all selected frames (batch), or just the current frame
	auto ApplyPaste = [&](int32 FrameIndex)
	{
		if (Anim->CombatData.FrameExtractionInfo.Num() <= FrameIndex)
		{
			Anim->CombatData.FrameExtractionInfo.SetNum(FrameIndex + 1);
		}
		Anim->CombatData.FrameExtractionInfo[FrameIndex].SpriteOffset = CopiedOffset;
		Anim->CombatData.FrameExtractionInfo[FrameIndex].bHasCustomAlignment = true;
	};

	if (SelectedFrames.Num() > 0)
	{
		for (int32 FrameIdx : SelectedFrames) { ApplyPaste(FrameIdx); }
		if (!SelectedFrames.Contains(SelectedFrameIndex)) { ApplyPaste(SelectedFrameIndex); }
	}
	else
	{
		ApplyPaste(SelectedFrameIndex);
	}

	EndTransaction();
	RefreshSpriteEditorFrameList();
}

void SCharacterProfileAssetEditor::OnResetOffset()
{
	FFlipbookProfileEntry* Anim = GetCurrentFlipbookDataMutable();
	if (!Anim) return;

	BeginTransaction(LOCTEXT("ResetOffset", "Reset Sprite Offset"));
	Asset->Modify();

	// Reset all selected frames (batch), or just the current frame
	auto ApplyReset = [&](int32 FrameIndex)
	{
		if (Anim->CombatData.FrameExtractionInfo.IsValidIndex(FrameIndex))
		{
			Anim->CombatData.FrameExtractionInfo[FrameIndex].SpriteOffset = FIntPoint::ZeroValue;
			Anim->CombatData.FrameExtractionInfo[FrameIndex].bHasCustomAlignment = false;
		}
	};

	if (SelectedFrames.Num() > 0)
	{
		for (int32 FrameIdx : SelectedFrames) { ApplyReset(FrameIdx); }
		if (!SelectedFrames.Contains(SelectedFrameIndex)) { ApplyReset(SelectedFrameIndex); }
	}
	else
	{
		ApplyReset(SelectedFrameIndex);
	}

	EndTransaction();
	RefreshSpriteEditorFrameList();
}

void SCharacterProfileAssetEditor::OnMirrorOffsetsX()
{
	FFlipbookProfileEntry* Anim = GetCurrentFlipbookDataMutable();
	if (!Anim) return;

	const int32 FrameCount = GetCurrentFrameCount();
	if (FrameCount == 0) return;

	BeginTransaction(LOCTEXT("MirrorOffsetsX", "Mirror Offsets X"));

	auto ApplyMirror = [&](int32 FrameIndex)
	{
		if (Anim->CombatData.FrameExtractionInfo.IsValidIndex(FrameIndex))
		{
			Anim->CombatData.FrameExtractionInfo[FrameIndex].SpriteOffset.X *= -1;
		}
	};

	if (SelectedFrames.Num() > 0)
	{
		for (int32 Idx : SelectedFrames) { ApplyMirror(Idx); }
	}
	else
	{
		for (int32 i = 0; i < FrameCount; i++) { ApplyMirror(i); }
	}

	EndTransaction();
	RefreshSpriteEditorFrameList();
}

void SCharacterProfileAssetEditor::OnMirrorOffsetsY()
{
	FFlipbookProfileEntry* Anim = GetCurrentFlipbookDataMutable();
	if (!Anim) return;

	const int32 FrameCount = GetCurrentFrameCount();
	if (FrameCount == 0) return;

	BeginTransaction(LOCTEXT("MirrorOffsetsY", "Mirror Offsets Y"));

	auto ApplyMirror = [&](int32 FrameIndex)
	{
		if (Anim->CombatData.FrameExtractionInfo.IsValidIndex(FrameIndex))
		{
			Anim->CombatData.FrameExtractionInfo[FrameIndex].SpriteOffset.Y *= -1;
		}
	};

	if (SelectedFrames.Num() > 0)
	{
		for (int32 Idx : SelectedFrames) { ApplyMirror(Idx); }
	}
	else
	{
		for (int32 i = 0; i < FrameCount; i++) { ApplyMirror(i); }
	}

	EndTransaction();
	RefreshSpriteEditorFrameList();
}

void SCharacterProfileAssetEditor::OnSpreadOffsetToAll()
{
	FFlipbookProfileEntry* Anim = GetCurrentFlipbookDataMutable();
	if (!Anim || !Anim->CombatData.FrameExtractionInfo.IsValidIndex(SelectedFrameIndex)) return;

	const FIntPoint SourceOffset = Anim->CombatData.FrameExtractionInfo[SelectedFrameIndex].SpriteOffset;
	const int32 FrameCount = GetCurrentFrameCount();
	if (FrameCount == 0) return;

	BeginTransaction(LOCTEXT("SpreadOffsetToAll", "Spread Offset to All Frames"));

	if (Anim->CombatData.FrameExtractionInfo.Num() < FrameCount)
	{
		Anim->CombatData.FrameExtractionInfo.SetNum(FrameCount);
	}

	for (int32 i = 0; i < FrameCount; i++)
	{
		Anim->CombatData.FrameExtractionInfo[i].SpriteOffset = SourceOffset;
		Anim->CombatData.FrameExtractionInfo[i].bHasCustomAlignment = (SourceOffset != FIntPoint::ZeroValue);
	}

	EndTransaction();
	RefreshSpriteEditorFrameList();
}

void SCharacterProfileAssetEditor::OnSpriteEditorOffsetChanged(int32 DeltaX, int32 DeltaY)
{
	NudgeOffset(DeltaX, DeltaY);
}

void SCharacterProfileAssetEditor::AutoApplyCurrentFrameOffset()
{
	if (!Asset.IsValid() || !Asset->Flipbooks.IsValidIndex(SelectedFlipbookIndex)) return;

	FFlipbookProfileEntry& Anim = Asset->Flipbooks[SelectedFlipbookIndex];
	if (!Anim.CombatData.FrameExtractionInfo.IsValidIndex(SelectedFrameIndex)) return;

	FSpriteExtractionInfo& Info = Anim.CombatData.FrameExtractionInfo[SelectedFrameIndex];
	if (Info.SpriteOffset == FIntPoint::ZeroValue) return;

	UPaperFlipbook* Flipbook = Anim.Identity.Flipbook.LoadSynchronous();
	if (!Flipbook || SelectedFrameIndex >= Flipbook->GetNumKeyFrames()) return;

	UPaperSprite* Sprite = Flipbook->GetKeyFrameChecked(SelectedFrameIndex).Sprite;
	if (!Sprite) return;

	// Use BeginTransaction only if not already inside a parent transaction.
	// When called from CommitNudgeTransaction (before its EndTransaction),
	// ActiveTransaction is already valid — piggyback on it for atomic undo.
	const bool bNeedsOwnTransaction = !ActiveTransaction.IsValid();
	if (bNeedsOwnTransaction)
	{
		BeginTransaction(LOCTEXT("AutoApplyOffset", "Apply Sprite Offset"));
	}

	Sprite->Modify();

	FVector2D CurrentPivot = Sprite->GetPivotPosition();
	FVector2D NewPivot = CurrentPivot - FVector2D(Info.SpriteOffset.X, Info.SpriteOffset.Y);
	Sprite->SetPivotMode(ESpritePivotMode::Custom, NewPivot);
	Sprite->PostEditChange();
	Sprite->GetPackage()->MarkPackageDirty();

	Info.SpriteOffset = FIntPoint::ZeroValue;
	Info.bHasCustomAlignment = false;

	Asset->MarkPackageDirty();

	if (bNeedsOwnTransaction)
	{
		EndTransaction();
	}

	if (SpriteEditorCanvas.IsValid())
	{
		SpriteEditorCanvas->InvalidateCachedDims();
		SpriteEditorCanvas->Invalidate(EInvalidateWidgetReason::Paint);
	}
	// Invalidate frame list instead of full rebuild — alignment indicator uses live data lambda
	if (SpriteEditorFrameListBox.IsValid())
	{
		SpriteEditorFrameListBox->Invalidate(EInvalidateWidgetReason::Paint);
	}
}

void SCharacterProfileAssetEditor::StartPlayback()
{
	if (bIsPlaying) return;

	ClearFrameSelection();
	bIsPlaying = true;

	// Queue playback only on the Sprite Editor tab — other tabs always play the single selected flipbook.
	if (IsSpriteEditorQueueActive())
	{
		if (PlaybackQueueIndex >= PlaybackQueue.Num())
		{
			PlaybackQueueIndex = 0;
			PlaybackPosition = 0.0f;
		}
		// else: resume from current position

		// Cache timing for current queue entry
		int32 AnimIdx = PlaybackQueue[PlaybackQueueIndex];
		if (Asset.IsValid() && Asset->Flipbooks.IsValidIndex(AnimIdx))
		{
			UPaperFlipbook* FB = Asset->Flipbooks[AnimIdx].Identity.Flipbook.LoadSynchronous();
			if (FB)
			{
				CachedPlaybackTiming = FFlipbookTimingData::ReadFromFlipbook(FB);
			}
		}
	}
	else
	{
		// Seed playback position from current frame so playback resumes where the user is
		const FFlipbookProfileEntry* Anim = GetCurrentFlipbookData();
		UPaperFlipbook* FB = (Anim && !Anim->Identity.Flipbook.IsNull()) ? Anim->Identity.Flipbook.LoadSynchronous() : nullptr;
		if (FB)
		{
			FFlipbookTimingData Timing = FFlipbookTimingData::ReadFromFlipbook(FB);
			PlaybackPosition = Timing.GetFrameStartTime(SelectedFrameIndex);
		}
		else
		{
			PlaybackPosition = 0.0f;
		}
	}

	PlaybackTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateSP(this, &SCharacterProfileAssetEditor::OnPlaybackTick),
		1.0f / 60.0f
	);

	RefreshPlaybackQueueList();
}

void SCharacterProfileAssetEditor::StopPlayback()
{
	if (!bIsPlaying) return;

	bIsPlaying = false;
	if (PlaybackTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(PlaybackTickerHandle);
		PlaybackTickerHandle.Reset();
	}

	RefreshPlaybackQueueList();
}

void SCharacterProfileAssetEditor::TogglePlayback()
{
	if (bIsPlaying)
	{
		StopPlayback();
	}
	else
	{
		StartPlayback();
	}
}

int32 SCharacterProfileAssetEditor::FrameIndexFromPlaybackPosition(
	const FFlipbookTimingData& Timing, float Position) const
{
	float AccumulatedTime = 0.0f;
	for (int32 i = 0; i < Timing.FrameDurations.Num(); i++)
	{
		float FrameDur = Timing.GetFrameDurationSeconds(i);
		if (Position < AccumulatedTime + FrameDur)
		{
			return i;
		}
		AccumulatedTime += FrameDur;
	}
	return FMath::Max(0, Timing.FrameDurations.Num() - 1);
}

bool SCharacterProfileAssetEditor::OnPlaybackTick(float DeltaTime)
{
	if (!Asset.IsValid()) return true;

	if (IsSpriteEditorQueueActive())
	{
		return OnQueuePlaybackTick(DeltaTime);
	}

	// Single flipbook playback (time-based)
	const FFlipbookProfileEntry* Anim = GetCurrentFlipbookData();
	if (!Anim || Anim->Identity.Flipbook.IsNull()) return true;

	UPaperFlipbook* FB = Anim->Identity.Flipbook.LoadSynchronous();
	if (!FB) return true;

	FFlipbookTimingData Timing = FFlipbookTimingData::ReadFromFlipbook(FB);
	if (Timing.TotalDurationSeconds <= 0.0f) return true;

	if (bPingPongPlayback)
	{
		if (bPlaybackReversed)
		{
			PlaybackPosition -= DeltaTime;
			if (PlaybackPosition < 0.0f)
			{
				PlaybackPosition = FMath::Abs(PlaybackPosition);
				bPlaybackReversed = false;
			}
		}
		else
		{
			PlaybackPosition += DeltaTime;
			if (PlaybackPosition >= Timing.TotalDurationSeconds)
			{
				PlaybackPosition = Timing.TotalDurationSeconds - (PlaybackPosition - Timing.TotalDurationSeconds);
				PlaybackPosition = FMath::Max(0.0f, PlaybackPosition);
				bPlaybackReversed = true;
			}
		}
	}
	else
	{
		PlaybackPosition += DeltaTime;
		if (PlaybackPosition >= Timing.TotalDurationSeconds)
		{
			PlaybackPosition = FMath::Fmod(PlaybackPosition, Timing.TotalDurationSeconds);
		}
	}

	int32 NewFrameIndex = FrameIndexFromPlaybackPosition(Timing, PlaybackPosition);

	if (NewFrameIndex != SelectedFrameIndex)
	{
		SelectedFrameIndex = NewFrameIndex;

		// Repaint the active tab's visuals during playback
		if (ActiveTab == ECharacterProfileTab::Hitboxes)
		{
			if (EditorCanvas.IsValid()) EditorCanvas->Invalidate(EInvalidateWidgetReason::Paint);
		}
		else
		{
			RefreshSpriteEditorFrameList();
		}
	}

	return true;
}

bool SCharacterProfileAssetEditor::OnQueuePlaybackTick(float DeltaTime)
{
	// Validate current queue entry — skip invalid entries
	while (PlaybackQueueIndex < PlaybackQueue.Num())
	{
		int32 AnimIdx = PlaybackQueue[PlaybackQueueIndex];
		if (Asset->Flipbooks.IsValidIndex(AnimIdx)
			&& !Asset->Flipbooks[AnimIdx].Identity.Flipbook.IsNull())
		{
			break; // Valid entry found
		}
		// Skip invalid entry
		PlaybackQueueIndex++;
		PlaybackPosition = 0.0f;
		CachedPlaybackTiming = FFlipbookTimingData();
	}

	// If we ran past the end, loop the queue
	if (PlaybackQueueIndex >= PlaybackQueue.Num())
	{
		PlaybackQueueIndex = 0;
		PlaybackPosition = 0.0f;

		// Find first valid entry
		bool bFoundValid = false;
		for (int32 i = 0; i < PlaybackQueue.Num(); i++)
		{
			int32 AnimIdx = PlaybackQueue[i];
			if (Asset->Flipbooks.IsValidIndex(AnimIdx)
				&& !Asset->Flipbooks[AnimIdx].Identity.Flipbook.IsNull())
			{
				PlaybackQueueIndex = i;
				bFoundValid = true;
				break;
			}
		}
		if (!bFoundValid) return true;
		CachedPlaybackTiming = FFlipbookTimingData();
	}

	int32 AnimIdx = PlaybackQueue[PlaybackQueueIndex];
	FFlipbookProfileEntry& Anim = Asset->Flipbooks[AnimIdx];

	// Use cached timing — only rebuild if invalid
	if (CachedPlaybackTiming.TotalDurationSeconds <= 0.0f)
	{
		UPaperFlipbook* FB = Anim.Identity.Flipbook.LoadSynchronous();
		if (!FB) { PlaybackQueueIndex++; PlaybackPosition = 0.0f; return true; }
		CachedPlaybackTiming = FFlipbookTimingData::ReadFromFlipbook(FB);
		if (CachedPlaybackTiming.TotalDurationSeconds <= 0.0f)
		{
			PlaybackQueueIndex++;
			PlaybackPosition = 0.0f;
			CachedPlaybackTiming = FFlipbookTimingData();
			return true;
		}
	}

	if (bPingPongPlayback)
	{
		if (bPlaybackReversed)
		{
			PlaybackPosition -= DeltaTime;
			if (PlaybackPosition < 0.0f)
			{
				// Completed a full forward+backward cycle — advance to next queue entry
				float Overflow = FMath::Abs(PlaybackPosition);
				PlaybackQueueIndex++;
				PlaybackPosition = Overflow;
				bPlaybackReversed = false;
				CachedPlaybackTiming = FFlipbookTimingData();

				if (PlaybackQueueIndex < PlaybackQueue.Num())
				{
					SyncSelectionToQueueEntry(PlaybackQueueIndex);
				}
				RefreshPlaybackQueueList();
				return true;
			}
		}
		else
		{
			PlaybackPosition += DeltaTime;
			if (PlaybackPosition >= CachedPlaybackTiming.TotalDurationSeconds)
			{
				PlaybackPosition = CachedPlaybackTiming.TotalDurationSeconds - (PlaybackPosition - CachedPlaybackTiming.TotalDurationSeconds);
				PlaybackPosition = FMath::Max(0.0f, PlaybackPosition);
				bPlaybackReversed = true;
			}
		}
	}
	else
	{
		PlaybackPosition += DeltaTime;

		// Check if we've exceeded this flipbook's duration
		if (PlaybackPosition >= CachedPlaybackTiming.TotalDurationSeconds)
		{
			float Overflow = PlaybackPosition - CachedPlaybackTiming.TotalDurationSeconds;
			PlaybackQueueIndex++;
			PlaybackPosition = Overflow;
			CachedPlaybackTiming = FFlipbookTimingData();

			if (PlaybackQueueIndex < PlaybackQueue.Num())
			{
				SyncSelectionToQueueEntry(PlaybackQueueIndex);
			}
			RefreshPlaybackQueueList();
			return true;
		}
	}

	// Sync selection if flipbook changed
	if (AnimIdx != SelectedFlipbookIndex)
	{
		SyncSelectionToQueueEntry(PlaybackQueueIndex);
	}

	// Determine frame from cached timing
	int32 NewFrameIndex = FrameIndexFromPlaybackPosition(CachedPlaybackTiming, PlaybackPosition);

	if (NewFrameIndex != SelectedFrameIndex)
	{
		SelectedFrameIndex = NewFrameIndex;
		RefreshSpriteEditorFrameList();
	}

	return true;
}

void SCharacterProfileAssetEditor::SyncSelectionToQueueEntry(int32 QueueIndex)
{
	if (!PlaybackQueue.IsValidIndex(QueueIndex)) return;

	int32 AnimIdx = PlaybackQueue[QueueIndex];
	if (AnimIdx != SelectedFlipbookIndex)
	{
		SelectedFlipbookIndex = AnimIdx;
		SelectedFrameIndex = 0;
		RefreshSpriteEditorFlipbookList();
		RefreshSpriteEditorFrameList();
	}
	// Always refresh queue list to update active entry highlight,
	// even when navigating between duplicate flipbooks in the queue
	RefreshPlaybackQueueList();
}

void SCharacterProfileAssetEditor::AddToPlaybackQueue(int32 FlipbookIndex)
{
	if (Asset.IsValid() && Asset->Flipbooks.IsValidIndex(FlipbookIndex))
	{
		PushQueueUndoSnapshot();
		PlaybackQueue.Add(FlipbookIndex);
		// Select the added flipbook so the canvas and other tabs reflect it
		OnFlipbookSelected(FlipbookIndex);
	}
}

void SCharacterProfileAssetEditor::RemoveFromPlaybackQueue(int32 QueueIndex)
{
	if (PlaybackQueue.IsValidIndex(QueueIndex))
	{
		PushQueueUndoSnapshot();
		PlaybackQueue.RemoveAt(QueueIndex);

		// Adjust PlaybackQueueIndex for removal
		if (PlaybackQueue.Num() == 0)
		{
			PlaybackQueueIndex = 0;
			PlaybackPosition = 0.0f;
			if (bIsPlaying) StopPlayback();
		}
		else if (QueueIndex < PlaybackQueueIndex)
		{
			// Entry removed before current — shift index back
			PlaybackQueueIndex--;
		}
		else if (QueueIndex == PlaybackQueueIndex)
		{
			// Removed the currently-playing entry — reset position
			PlaybackPosition = 0.0f;
			CachedPlaybackTiming = FFlipbookTimingData();
			if (PlaybackQueueIndex >= PlaybackQueue.Num())
			{
				// Was the last entry — stop playback
				if (bIsPlaying) StopPlayback();
				PlaybackQueueIndex = 0;
			}
		}

		RefreshPlaybackQueueList();
	}
}

void SCharacterProfileAssetEditor::ClearPlaybackQueue()
{
	PushQueueUndoSnapshot();
	if (bIsPlaying) StopPlayback();
	PlaybackQueue.Empty();
	PlaybackQueueIndex = 0;
	PlaybackPosition = 0.0f;
	CachedPlaybackTiming = FFlipbookTimingData();
	RefreshPlaybackQueueList();
}

void SCharacterProfileAssetEditor::ReorderQueueEntry(int32 FromIndex, int32 ToIndex)
{
	// ToIndex = insert-before position in the *original* array. Num() means append to end.
	if (!PlaybackQueue.IsValidIndex(FromIndex) || ToIndex < 0 || ToIndex > PlaybackQueue.Num()) return;
	if (FromIndex == ToIndex) return;

	PushQueueUndoSnapshot();
	int32 MovedAnim = PlaybackQueue[FromIndex];
	PlaybackQueue.RemoveAt(FromIndex);

	// After removal, adjust target index for the shift
	int32 InsertAt = (ToIndex > FromIndex) ? ToIndex - 1 : ToIndex;
	InsertAt = FMath::Clamp(InsertAt, 0, PlaybackQueue.Num());
	PlaybackQueue.Insert(MovedAnim, InsertAt);

	// Track the currently-playing entry through the reorder
	if (bIsPlaying)
	{
		if (PlaybackQueueIndex == FromIndex)
		{
			PlaybackQueueIndex = InsertAt;
		}
		else
		{
			if (FromIndex < PlaybackQueueIndex && InsertAt >= PlaybackQueueIndex)
				PlaybackQueueIndex--;
			else if (FromIndex > PlaybackQueueIndex && InsertAt <= PlaybackQueueIndex)
				PlaybackQueueIndex++;
		}
	}

	RefreshPlaybackQueueList();
}

void SCharacterProfileAssetEditor::PushQueueUndoSnapshot()
{
	QueueUndoStack.Add(PlaybackQueue);
	QueueRedoStack.Empty();
}

bool SCharacterProfileAssetEditor::PopQueueUndo()
{
	if (QueueUndoStack.Num() == 0) return false;

	QueueRedoStack.Add(PlaybackQueue);
	PlaybackQueue = QueueUndoStack.Pop();

	// Reset playback state to avoid stale indices
	if (bIsPlaying) StopPlayback();
	PlaybackQueueIndex = 0;
	PlaybackPosition = 0.0f;
	CachedPlaybackTiming = FFlipbookTimingData();
	RefreshPlaybackQueueList();
	return true;
}

bool SCharacterProfileAssetEditor::PopQueueRedo()
{
	if (QueueRedoStack.Num() == 0) return false;

	QueueUndoStack.Add(PlaybackQueue);
	PlaybackQueue = QueueRedoStack.Pop();

	if (bIsPlaying) StopPlayback();
	PlaybackQueueIndex = 0;
	PlaybackPosition = 0.0f;
	CachedPlaybackTiming = FFlipbookTimingData();
	RefreshPlaybackQueueList();
	return true;
}

// ──────────────────────────────────────────────────────────────────────────
// Playback queue scope helpers — see CLAUDE.md "Playback Queue Architecture".
//
// The playback queue is a SPRITE EDITOR TAB feature only. Queue state must
// never leak into other tabs (that regression has been fixed many times —
// any new code that wants "previous/next flipbook" MUST pick the right
// helper below or it will re-break the architecture).
// ──────────────────────────────────────────────────────────────────────────

bool SCharacterProfileAssetEditor::IsSpriteEditorQueueActive() const
{
	return PlaybackQueue.Num() > 0
		&& ActiveTab == ECharacterProfileTab::SpriteEditor;
}

int32 SCharacterProfileAssetEditor::GetVisualAdjacentFlipbookIndex(int32 Direction) const
{
	if (!Asset.IsValid() || Asset->Flipbooks.Num() <= 1) return INDEX_NONE;

	// Pure visual order (matches BuildGroupedFlipbookList traversal). Never
	// consults PlaybackQueue — by design. Used for flipbook-card navigation
	// across all tabs.
	TArray<int32> VisualOrder = GetVisualFlipbookOrder();
	int32 CurrentPos = VisualOrder.IndexOfByKey(SelectedFlipbookIndex);
	if (CurrentPos == INDEX_NONE) return INDEX_NONE;

	int32 TargetPos = CurrentPos + Direction;
	if (VisualOrder.IsValidIndex(TargetPos))
	{
		return VisualOrder[TargetPos];
	}
	return INDEX_NONE;
}

int32 SCharacterProfileAssetEditor::GetQueueAdjacentFlipbookIndex(int32 Direction) const
{
	if (!IsSpriteEditorQueueActive()) return INDEX_NONE;
	if (PlaybackQueue.Num() == 0) return INDEX_NONE;

	int32 TargetQueuePos = PlaybackQueueIndex + Direction;
	// Wrap around the queue: past the end → first entry, before the start → last entry.
	TargetQueuePos = ((TargetQueuePos % PlaybackQueue.Num()) + PlaybackQueue.Num()) % PlaybackQueue.Num();
	if (PlaybackQueue.IsValidIndex(TargetQueuePos))
	{
		return PlaybackQueue[TargetQueuePos];
	}
	return INDEX_NONE;
}

TArray<int32> SCharacterProfileAssetEditor::GetVisualFlipbookOrder() const
{
	TArray<int32> Result;
	if (!Asset.IsValid()) return Result;

	TArray<int32> SortedIndices = GetSortedFlipbookIndices();
	TMap<FName, TArray<int32>> FlipbooksByGroup;
	for (int32 i : SortedIndices)
	{
		FlipbooksByGroup.FindOrAdd(Asset->Flipbooks[i].FlipbookGroup).Add(i);
	}

	// If no groups exist (all ungrouped), return flat sorted list
	if (FlipbooksByGroup.Num() <= 1 && FlipbooksByGroup.Contains(NAME_None))
	{
		return FlipbooksByGroup[NAME_None];
	}

	TMap<FName, TArray<const FFlipbookGroupInfo*>> Tree = Asset->GetFlipbookGroupTree();

	// Recursive traversal matching BuildGroupedFlipbookList order
	TFunction<void(FName)> TraverseGroup = [&](FName GroupName)
	{
		// Add flipbooks in this group
		if (const TArray<int32>* GroupIndices = FlipbooksByGroup.Find(GroupName))
		{
			Result.Append(*GroupIndices);
		}

		// Recurse into child groups
		const TArray<const FFlipbookGroupInfo*>* ChildGroups = GroupName.IsNone() ? nullptr : Tree.Find(GroupName);
		if (ChildGroups)
		{
			for (const FFlipbookGroupInfo* ChildGroup : *ChildGroups)
			{
				TraverseGroup(ChildGroup->GroupName);
			}
		}
	};

	// Ungrouped first
	TraverseGroup(NAME_None);

	// Root-level groups
	if (const TArray<const FFlipbookGroupInfo*>* RootGroups = Tree.Find(NAME_None))
	{
		for (const FFlipbookGroupInfo* GroupInfo : *RootGroups)
		{
			TraverseGroup(GroupInfo->GroupName);
		}
	}

	return Result;
}

void SCharacterProfileAssetEditor::OnEditSpriteEditorClicked(int32 FlipbookIndex)
{
	SelectedFlipbookIndex = FlipbookIndex;
	SelectedFrameIndex = 0;
	SwitchToTab(static_cast<int32>(ECharacterProfileTab::SpriteEditor));
}

bool SCharacterProfileAssetEditor::CanExcludeCurrentSpriteEditorFrame() const
{
	if (!Asset.IsValid() || !Asset->Flipbooks.IsValidIndex(SelectedFlipbookIndex))
	{
		return false;
	}

	UPaperFlipbook* Flipbook = Asset->Flipbooks[SelectedFlipbookIndex].Identity.Flipbook.LoadSynchronous();
	return Flipbook && Flipbook->GetNumKeyFrames() > 1 &&
		SelectedFrameIndex >= 0 && SelectedFrameIndex < Flipbook->GetNumKeyFrames();
}

void SCharacterProfileAssetEditor::OnExcludeCurrentSpriteEditorFrame()
{
	if (!Asset.IsValid() || !Asset->Flipbooks.IsValidIndex(SelectedFlipbookIndex))
	{
		return;
	}

	UPaperFlipbook* Flipbook = Asset->Flipbooks[SelectedFlipbookIndex].Identity.Flipbook.LoadSynchronous();
	if (!Flipbook || !CanExcludeCurrentSpriteEditorFrame())
	{
		return;
	}

	if (bIsPlaying)
	{
		StopPlayback();
	}

	BeginTransaction(LOCTEXT("ExcludeSpriteEditorFrameTxn", "Exclude Sprite Frame"));
	Flipbook->SetFlags(RF_Transactional);
	Flipbook->Modify();
	const bool bExcluded = Asset->ExcludeFlipbookFrame(SelectedFlipbookIndex, SelectedFrameIndex);
	EndTransaction();

	if (!bExcluded)
	{
		return;
	}

	const int32 ExcludedIndex = SelectedFrameIndex;
	const int32 NewFrameCount = Flipbook->GetNumKeyFrames();
	SelectedFrameIndex = NewFrameCount > 0
		? FMath::Clamp(SelectedFrameIndex, 0, NewFrameCount - 1)
		: 0;

	if (ReferenceFlipbookIndex == SelectedFlipbookIndex)
	{
		if (ExcludedIndex < ReferenceFrameIndex)
		{
			// Shift reference down to preserve identity
			ReferenceFrameIndex--;
		}
		else if (ExcludedIndex == ReferenceFrameIndex)
		{
			// Reference itself was excluded — clamp to valid range
			ReferenceFrameIndex = NewFrameCount > 0
				? FMath::Clamp(ReferenceFrameIndex, 0, NewFrameCount - 1)
				: 0;
		}
		// ExcludedIndex > ReferenceFrameIndex: no change needed
	}

	RefreshAfterFrameExclusion();
}

void SCharacterProfileAssetEditor::OnRestoreExcludedSpriteEditorFrame(int32 ExcludedFrameIndex)
{
	if (!Asset.IsValid() || !Asset->Flipbooks.IsValidIndex(SelectedFlipbookIndex))
	{
		return;
	}

	FFlipbookProfileEntry& Anim = Asset->Flipbooks[SelectedFlipbookIndex];
	if (!Anim.CombatData.ExcludedFrames.IsValidIndex(ExcludedFrameIndex))
	{
		return;
	}

	UPaperFlipbook* Flipbook = Anim.Identity.Flipbook.LoadSynchronous();
	if (!Flipbook)
	{
		return;
	}

	const int32 SourceFrameIndex = Anim.CombatData.ExcludedFrames[ExcludedFrameIndex].ExtractionInfo.SourceFrameIndex;

	BeginTransaction(LOCTEXT("RestoreExcludedSpriteEditorFrameTxn", "Restore Excluded Sprite Frame"));
	Flipbook->SetFlags(RF_Transactional);
	Flipbook->Modify();
	const bool bRestored = Asset->RestoreExcludedFlipbookFrame(SelectedFlipbookIndex, ExcludedFrameIndex);
	EndTransaction();

	if (!bRestored || !Asset->Flipbooks.IsValidIndex(SelectedFlipbookIndex))
	{
		return;
	}

	const FFlipbookProfileEntry& UpdatedAnim = Asset->Flipbooks[SelectedFlipbookIndex];
	const int32 RestoredIndex = FindSpriteEditorFrameIndexBySourceIndex(UpdatedAnim, SourceFrameIndex);
	if (RestoredIndex != INDEX_NONE)
	{
		SelectedFrameIndex = RestoredIndex;
	}
	else
	{
		SelectedFrameIndex = FMath::Clamp(SelectedFrameIndex, 0, FMath::Max(0, Flipbook->GetNumKeyFrames() - 1));
	}

	RefreshAfterFrameExclusion(/*bDismissMenus=*/ true);
}

void SCharacterProfileAssetEditor::OnRestoreAllExcludedSpriteEditorFrames()
{
	if (!Asset.IsValid() || !Asset->Flipbooks.IsValidIndex(SelectedFlipbookIndex))
	{
		return;
	}

	FFlipbookProfileEntry& Anim = Asset->Flipbooks[SelectedFlipbookIndex];
	if (Anim.CombatData.ExcludedFrames.Num() <= 0)
	{
		return;
	}

	UPaperFlipbook* Flipbook = Anim.Identity.Flipbook.LoadSynchronous();
	if (!Flipbook)
	{
		return;
	}

	BeginTransaction(LOCTEXT("RestoreAllExcludedSpriteEditorFramesTxn", "Restore All Excluded Sprite Frames"));
	Flipbook->SetFlags(RF_Transactional);
	Flipbook->Modify();
	const int32 RestoredCount = Asset->RestoreAllExcludedFlipbookFrames(SelectedFlipbookIndex);
	EndTransaction();

	if (RestoredCount <= 0)
	{
		return;
	}

	SelectedFrameIndex = FMath::Clamp(SelectedFrameIndex, 0, FMath::Max(0, Flipbook->GetNumKeyFrames() - 1));

	RefreshAfterFrameExclusion(/*bDismissMenus=*/ true);
}

void SCharacterProfileAssetEditor::RefreshAfterFrameExclusion(bool bDismissMenus)
{
	ClearFrameSelection();
	RefreshCurrentFrameFlipState();
	RefreshSpriteEditorFrameList();
	RefreshFrameList();
	RefreshFlipbookList();
	RefreshOverviewFlipbookList();
	RefreshSpriteEditorFlipbookList();
	RefreshPlaybackQueueList();
	if (bDismissMenus)
	{
		FSlateApplication::Get().DismissAllMenus();
	}
	if (SpriteEditorCanvas.IsValid())
	{
		SpriteEditorCanvas->InvalidateCachedDims();
	}
}

void SCharacterProfileAssetEditor::RemoveSpriteRegionFromTexture(UPaperSprite* TargetSprite)
{
	if (!TargetSprite || !Asset.IsValid()) return;

	UTexture2D* SourceTexture = Cast<UTexture2D>(TargetSprite->GetSourceTexture());
	if (!SourceTexture) return;

	SourceTexture->SetFlags(RF_Transactional);
	SourceTexture->Modify();

	const FVector2D DeletedUV = TargetSprite->GetSourceUV();
	const FVector2D DeletedSize = TargetSprite->GetSourceSize();
	const int32 DelX = FMath::RoundToInt(DeletedUV.X);
	const int32 DelW = FMath::RoundToInt(DeletedSize.X);

	TArray<FColor> SrcPixels;
	int32 SrcW = 0, SrcH = 0;
	if (!FSpriteExtractionUtils::LoadTextureData(SourceTexture, SrcPixels, SrcW, SrcH) || DelW <= 0 || SrcW <= DelW) return;

	const int32 NewW = SrcW - DelW;
	TArray<FColor> NewPixels;
	NewPixels.SetNumZeroed(NewW * SrcH);

	for (int32 Y = 0; Y < SrcH; Y++)
	{
		for (int32 X = 0; X < DelX && X < NewW; X++)
			NewPixels[Y * NewW + X] = SrcPixels[Y * SrcW + X];
		for (int32 X = DelX + DelW; X < SrcW; X++)
		{
			const int32 DstX = X - DelW;
			if (DstX >= 0 && DstX < NewW)
				NewPixels[Y * NewW + DstX] = SrcPixels[Y * SrcW + X];
		}
	}

	// Write to Source, rebuild PlatformData, recreate GPU resource
	SourceTexture->Source.Init(NewW, SrcH, 1, 1, TSF_BGRA8);
	{
		uint8* DestData = SourceTexture->Source.LockMip(0);
		FMemory::Memcpy(DestData, NewPixels.GetData(), NewW * SrcH * sizeof(FColor));
		SourceTexture->Source.UnlockMip(0);
	}
	SourceTexture->ForceRebuildPlatformData();
	SourceTexture->ReleaseResource();
	SourceTexture->UpdateResource();
	SourceTexture->MarkPackageDirty();

	// Shift SourceUV for sprites to the right of the deleted region
	TSet<UPaperSprite*> Processed;
	auto ShiftSprite = [&](UPaperSprite* S)
	{
		if (!S || S == TargetSprite || Processed.Contains(S)) return;
		if (Cast<UTexture2D>(S->GetSourceTexture()) != SourceTexture) return;
		Processed.Add(S);

		const int32 SpriteX = FMath::RoundToInt(S->GetSourceUV().X);
		if (SpriteX > DelX)
		{
			S->SetFlags(RF_Transactional);
			S->Modify();
			const FIntPoint NewOffset(SpriteX - DelW, FMath::RoundToInt(S->GetSourceUV().Y));
			const FIntPoint SpriteDim(FMath::RoundToInt(S->GetSourceSize().X), FMath::RoundToInt(S->GetSourceSize().Y));
			FSpriteAssetInitParameters InitParams;
			InitParams.Texture = SourceTexture;
			InitParams.Offset = NewOffset;
			InitParams.Dimension = SpriteDim;
			S->InitializeSprite(InitParams);
			S->MarkPackageDirty();
		}
	};

	for (const FFlipbookProfileEntry& FB : Asset->Flipbooks)
	{
		UPaperFlipbook* FB_Flip = FB.Identity.Flipbook.LoadSynchronous();
		if (FB_Flip)
		{
			for (int32 K = 0; K < FB_Flip->GetNumKeyFrames(); K++)
				ShiftSprite(FB_Flip->GetKeyFrameChecked(K).Sprite);
		}
		for (const FExcludedFlipbookFrameData& Excl : FB.CombatData.ExcludedFrames)
			ShiftSprite(Excl.KeyFrame.Sprite);
	}
}

void SCharacterProfileAssetEditor::DeleteFlipbookFrame(int32 FlipbookIndex, int32 FrameIndex, bool bRemoveFromTexture)
{
	if (!Asset.IsValid() || !Asset->Flipbooks.IsValidIndex(FlipbookIndex)) return;

	FFlipbookProfileEntry& Entry = Asset->Flipbooks[FlipbookIndex];
	UPaperFlipbook* Flipbook = Entry.Identity.Flipbook.LoadSynchronous();
	if (!Flipbook || FrameIndex < 0 || FrameIndex >= Flipbook->GetNumKeyFrames()) return;
	if (Flipbook->GetNumKeyFrames() <= 1) return;

	if (bIsPlaying) StopPlayback();

	UPaperSprite* TargetSprite = Flipbook->GetKeyFrameChecked(FrameIndex).Sprite;

	BeginTransaction(LOCTEXT("DeleteFrameTxn", "Delete Frame"));
	Flipbook->SetFlags(RF_Transactional);
	Flipbook->Modify();

	if (bRemoveFromTexture)
	{
		RemoveSpriteRegionFromTexture(TargetSprite);
	}

	// Remove keyframe from flipbook
	{
		FScopedFlipbookMutator Mutator(Flipbook);
		if (Mutator.KeyFrames.IsValidIndex(FrameIndex))
		{
			Mutator.KeyFrames.RemoveAt(FrameIndex);
		}
	}
	Flipbook->MarkPackageDirty();

	// Remove from all parallel data arrays
	if (Entry.CombatData.Frames.IsValidIndex(FrameIndex))
		Entry.CombatData.Frames.RemoveAt(FrameIndex);
	if (Entry.CombatData.FrameExtractionInfo.IsValidIndex(FrameIndex))
		Entry.CombatData.FrameExtractionInfo.RemoveAt(FrameIndex);
	if (Entry.MotionData.RootMotion.IsValidIndex(FrameIndex))
		Entry.MotionData.RootMotion.RemoveAt(FrameIndex);

	// Reindex frame events so references to frames after the deleted one stay
	// aligned with their content. One-shot events on the deleted frame are
	// dropped (their trigger frame is gone); ranged events that contained the
	// deleted frame shrink by 1 (if FrameCount hits 0 the event is dropped).
	// Mirrors the remap discipline shipped in ReorderFrame (see PR #98 review #2).
	{
		TArray<int32> EventsToDrop;
		for (int32 EventIdx = 0; EventIdx < Entry.FrameEventData.FrameEvents.Num(); ++EventIdx)
		{
			UPaper2DPlusFrameEventBase* Event = Entry.FrameEventData.FrameEvents[EventIdx];
			if (!Event) continue;

			if (UPaper2DPlusFrameEvent* OneShot = Cast<UPaper2DPlusFrameEvent>(Event))
			{
				if (OneShot->TriggerFrame == FrameIndex)
				{
					EventsToDrop.Add(EventIdx);
				}
				else if (OneShot->TriggerFrame > FrameIndex)
				{
					OneShot->TriggerFrame--;
				}
			}
			else if (UPaper2DPlusFrameEventState* Ranged = Cast<UPaper2DPlusFrameEventState>(Event))
			{
				if (FrameIndex < Ranged->StartFrame)
				{
					// Deleted before range — shift StartFrame back, range length preserved.
					Ranged->StartFrame--;
				}
				else if (FrameIndex < Ranged->StartFrame + Ranged->FrameCount)
				{
					// Deleted inside range — range shrinks. Drop if it hits 0.
					Ranged->FrameCount--;
					if (Ranged->FrameCount <= 0)
					{
						EventsToDrop.Add(EventIdx);
					}
				}
				// else: deleted after range, no change.
			}
		}
		// Drop highest-first so indices stay valid.
		for (int32 Idx = EventsToDrop.Num() - 1; Idx >= 0; --Idx)
		{
			Entry.FrameEventData.FrameEvents.RemoveAt(EventsToDrop[Idx]);
		}
	}

	EndTransaction();

	// Adjust selection
	const int32 NewFrameCount = Flipbook->GetNumKeyFrames();
	SelectedFrameIndex = NewFrameCount > 0 ? FMath::Clamp(SelectedFrameIndex, 0, NewFrameCount - 1) : 0;

	// Adjust reference frame if needed
	if (ReferenceFlipbookIndex == FlipbookIndex)
	{
		if (FrameIndex < ReferenceFrameIndex)
			ReferenceFrameIndex--;
		else if (FrameIndex == ReferenceFrameIndex)
			ReferenceFrameIndex = NewFrameCount > 0 ? FMath::Clamp(ReferenceFrameIndex, 0, NewFrameCount - 1) : 0;
	}

	// Full refresh — rebuilds all thumbnails to pick up modified sprite render data
	ClearFrameSelection();
	RefreshAll();
}

TSharedRef<SWidget> SCharacterProfileAssetEditor::BuildSpriteEditorRestoreExcludedMenu()
{
	if (!Asset.IsValid() || !Asset->Flipbooks.IsValidIndex(SelectedFlipbookIndex))
	{
		return SNew(STextBlock).Text(LOCTEXT("NoSpriteEditorFlipbookSelected", "No flipbook selected"));
	}

	const FFlipbookProfileEntry& Anim = Asset->Flipbooks[SelectedFlipbookIndex];
	if (Anim.CombatData.ExcludedFrames.Num() <= 0)
	{
		return SNew(STextBlock).Text(LOCTEXT("NoSpriteEditorExcludedFrames", "No excluded frames"));
	}

	TArray<int32> SortedIndices;
	SortedIndices.Reserve(Anim.CombatData.ExcludedFrames.Num());
	for (int32 Index = 0; Index < Anim.CombatData.ExcludedFrames.Num(); ++Index)
	{
		SortedIndices.Add(Index);
	}
	SortedIndices.Sort([&Anim](int32 A, int32 B)
	{
		return Anim.CombatData.ExcludedFrames[A].ExtractionInfo.SourceFrameIndex <
			Anim.CombatData.ExcludedFrames[B].ExtractionInfo.SourceFrameIndex;
	});

	TSharedRef<SVerticalBox> MenuBox = SNew(SVerticalBox);

	MenuBox->AddSlot()
	.AutoHeight()
	.Padding(2, 0, 2, 4)
	[
		SNew(SButton)
		.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
		.OnClicked_Lambda([this]() {
			OnRestoreAllExcludedSpriteEditorFrames();
			return FReply::Handled();
		})
		[
			SNew(STextBlock)
			.Text(LOCTEXT("RestoreAllExcludedSpriteEditor", "Restore All"))
		]
	];

	for (int32 ExcludedIndex : SortedIndices)
	{
		const FExcludedFlipbookFrameData& ExcludedFrame = Anim.CombatData.ExcludedFrames[ExcludedIndex];
		const int32 SourceFrameNumber = FMath::Max(0, ExcludedFrame.ExtractionInfo.SourceFrameIndex) + 1;
		const FText FrameNameText = ExcludedFrame.FrameData.FrameName.IsEmpty()
			? FText::Format(LOCTEXT("ExcludedSpriteEditorFrameDefaultName", "Frame {0}"), FText::AsNumber(SourceFrameNumber))
			: FText::FromString(ExcludedFrame.FrameData.FrameName);

		MenuBox->AddSlot()
		.AutoHeight()
		.Padding(2, 0, 2, 2)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
			.OnClicked_Lambda([this, ExcludedIndex]() {
				OnRestoreExcludedSpriteEditorFrame(ExcludedIndex);
				return FReply::Handled();
			})
			[
				SNew(STextBlock)
				.Text(FText::Format(LOCTEXT("RestoreOneExcludedSpriteEditorFmt", "Restore #{0}: {1}"),
					FText::AsNumber(SourceFrameNumber),
					FrameNameText))
			]
		];
	}

	return SNew(SBox)
		.MinDesiredWidth(260.0f)
		.MaxDesiredHeight(280.0f)
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			[
				MenuBox
			]
		];
}

void SCharacterProfileAssetEditor::RefreshCurrentFrameFlipState()
{
	const FFlipbookProfileEntry* Anim = GetCurrentFlipbookData();
	if (!Anim || !Anim->CombatData.FrameExtractionInfo.IsValidIndex(SelectedFrameIndex))
	{
		bSpriteFlipX = false;
		bSpriteFlipY = false;
		return;
	}

	bSpriteFlipX = Anim->CombatData.FrameExtractionInfo[SelectedFrameIndex].bFlipX;
	bSpriteFlipY = Anim->CombatData.FrameExtractionInfo[SelectedFrameIndex].bFlipY;
}

void SCharacterProfileAssetEditor::OnApplyFlipToCurrentFrame()
{
	if (!Asset.IsValid()) return;
	FFlipbookProfileEntry* Anim = GetCurrentFlipbookDataMutable();
	if (!Anim || !Anim->CombatData.Frames.IsValidIndex(SelectedFrameIndex)) return;

	BeginTransaction(LOCTEXT("ApplyFlipCurrentFrameTxn", "Apply Sprite Flip to Current Frame"));
	Asset->SetSpriteFlipInRange(Anim->Identity.FlipbookName, SelectedFrameIndex, SelectedFrameIndex, bSpriteFlipX, bSpriteFlipY);
	EndTransaction();
	RefreshCurrentFrameFlipState();
}

void SCharacterProfileAssetEditor::OnApplyFlipToCurrentFlipbook()
{
	if (!Asset.IsValid()) return;
	FFlipbookProfileEntry* Anim = GetCurrentFlipbookDataMutable();
	if (!Anim || Anim->CombatData.Frames.Num() == 0) return;

	BeginTransaction(LOCTEXT("ApplyFlipCurrentFlipbookTxn", "Apply Sprite Flip to Flipbook"));
	Asset->SetSpriteFlipForFlipbook(Anim->Identity.FlipbookName, bSpriteFlipX, bSpriteFlipY);
	EndTransaction();
	RefreshCurrentFrameFlipState();
}

void SCharacterProfileAssetEditor::OnApplyFlipToAllFlipbooks()
{
	if (!Asset.IsValid()) return;

	BeginTransaction(LOCTEXT("ApplyFlipAllFlipbooksTxn", "Apply Sprite Flip to All Flipbooks"));
	Asset->SetSpriteFlipForAllFlipbooks(bSpriteFlipX, bSpriteFlipY);
	EndTransaction();
	RefreshCurrentFrameFlipState();
}

void SCharacterProfileAssetEditor::ReorderFrame(int32 FromIndex, int32 ToIndex)
{
	if (!Asset.IsValid()) return;
	FFlipbookProfileEntry* Anim = GetCurrentFlipbookDataMutable();
	if (!Anim) return;
	if (FromIndex == ToIndex) return;
	if (!Anim->CombatData.Frames.IsValidIndex(FromIndex) || !Anim->CombatData.Frames.IsValidIndex(ToIndex)) return;

	BeginTransaction(LOCTEXT("ReorderFrameTrans", "Reorder Frame"));
	Asset->Modify();

	// Bubble-swap the frame from FromIndex to ToIndex in all parallel arrays
	auto BubbleSwap = [](auto& Array, int32 From, int32 To)
	{
		if (From < To)
		{
			for (int32 i = From; i < To; i++)
				Array.Swap(i, i + 1);
		}
		else
		{
			for (int32 i = From; i > To; i--)
				Array.Swap(i, i - 1);
		}
	};

	// Reorder hitbox frame data
	BubbleSwap(Anim->CombatData.Frames, FromIndex, ToIndex);

	// Reorder extraction info — only when already populated, pad to match Frames length first
	if (Anim->CombatData.FrameExtractionInfo.Num() > 0)
	{
		if (Anim->CombatData.FrameExtractionInfo.Num() < Anim->CombatData.Frames.Num())
		{
			Anim->CombatData.FrameExtractionInfo.SetNum(Anim->CombatData.Frames.Num());
		}
		BubbleSwap(Anim->CombatData.FrameExtractionInfo, FromIndex, ToIndex);
	}

	// Reorder root motion
	if (Anim->MotionData.RootMotion.Num() > 0)
	{
		if (Anim->MotionData.RootMotion.Num() < Anim->CombatData.Frames.Num())
		{
			Anim->MotionData.RootMotion.SetNum(Anim->CombatData.Frames.Num());
		}
		BubbleSwap(Anim->MotionData.RootMotion, FromIndex, ToIndex);
	}

	// Reorder flipbook key frames
	if (!Anim->Identity.Flipbook.IsNull())
	{
		if (UPaperFlipbook* FB = Anim->Identity.Flipbook.LoadSynchronous())
		{
			FB->Modify();
			{
				FScopedFlipbookMutator Mutator(FB);
				if (Mutator.KeyFrames.IsValidIndex(FromIndex) && Mutator.KeyFrames.IsValidIndex(ToIndex))
				{
					BubbleSwap(Mutator.KeyFrames, FromIndex, ToIndex);
				}
			}
			FB->MarkPackageDirty();
		}
	}

	// Rename all frames to sequential indices
	for (int32 i = 0; i < Anim->CombatData.Frames.Num(); i++)
	{
		Anim->CombatData.Frames[i].FrameName = FString::Printf(TEXT("Frame_%d"), i);
	}

	// Remap frame event TriggerFrame/StartFrame so events follow their content
	// rather than staying fixed to absolute positions (which would silently mis-fire).
	auto RemapFrame = [FromIndex, ToIndex](int32 Frame) -> int32
	{
		if (Frame == FromIndex) return ToIndex;
		if (FromIndex < ToIndex && Frame > FromIndex && Frame <= ToIndex) return Frame - 1;
		if (FromIndex > ToIndex && Frame >= ToIndex && Frame < FromIndex) return Frame + 1;
		return Frame;
	};
	for (UPaper2DPlusFrameEventBase* Event : Anim->FrameEventData.FrameEvents)
	{
		if (!Event) continue;
		if (UPaper2DPlusFrameEvent* OneShot = Cast<UPaper2DPlusFrameEvent>(Event))
		{
			OneShot->TriggerFrame = RemapFrame(OneShot->TriggerFrame);
		}
		if (UPaper2DPlusFrameEventState* Ranged = Cast<UPaper2DPlusFrameEventState>(Event))
		{
			Ranged->StartFrame = RemapFrame(Ranged->StartFrame);
		}
	}

	EndTransaction();

	// Follow the moved frame
	SelectedFrameIndex = ToIndex;

	RefreshSpriteEditorFrameList();
	RefreshFrameList();
}

void SCharacterProfileAssetEditor::OnApplyAlignmentBatchOperation()
{
	FFlipbookProfileEntry* Anim = GetCurrentFlipbookDataMutable();
	if (!Anim) return;

	const int32 FrameCount = GetCurrentFrameCount();
	if (FrameCount == 0) return;

	// Build target frame indices based on AlignBatchTargetIndex
	TArray<int32> TargetFrames;
	switch (AlignBatchTargetIndex)
	{
	case 0: // All Frames
		for (int32 i = 0; i < FrameCount; i++) { TargetFrames.Add(i); }
		break;
	case 1: // Selected Frames
		if (SelectedFrames.Num() > 0)
		{
			TargetFrames = SelectedFrames.Array();
		}
		else
		{
			TargetFrames.Add(SelectedFrameIndex);
		}
		break;
	case 2: // Remaining Frames
		for (int32 i = SelectedFrameIndex; i < FrameCount; i++) { TargetFrames.Add(i); }
		break;
	case 3: // Custom Range
		{
			int32 First = FMath::Clamp(FMath::Min(AlignBatchRangeStart, AlignBatchRangeEnd), 0, FrameCount - 1);
			int32 Last = FMath::Clamp(FMath::Max(AlignBatchRangeStart, AlignBatchRangeEnd), 0, FrameCount - 1);
			for (int32 i = First; i <= Last; i++) { TargetFrames.Add(i); }
		}
		break;
	}

	if (TargetFrames.Num() == 0) return;

	// Ensure FrameExtractionInfo is sized
	if (Anim->CombatData.FrameExtractionInfo.Num() < FrameCount)
	{
		Anim->CombatData.FrameExtractionInfo.SetNum(FrameCount);
	}

	BeginTransaction(LOCTEXT("AlignBatchOp", "Batch Offset Operation"));

	switch (AlignBatchActionIndex)
	{
	case 0: // Current Frame's Offset
	{
		if (!Anim->CombatData.FrameExtractionInfo.IsValidIndex(SelectedFrameIndex)) break;
		const FIntPoint SourceOffset = Anim->CombatData.FrameExtractionInfo[SelectedFrameIndex].SpriteOffset;
		for (int32 Idx : TargetFrames)
		{
			if (Anim->CombatData.FrameExtractionInfo.IsValidIndex(Idx))
			{
				Anim->CombatData.FrameExtractionInfo[Idx].SpriteOffset = SourceOffset;
				Anim->CombatData.FrameExtractionInfo[Idx].bHasCustomAlignment = (SourceOffset != FIntPoint::ZeroValue);
			}
		}
		break;
	}
	case 1: // Reset (0, 0)
	{
		for (int32 Idx : TargetFrames)
		{
			if (Anim->CombatData.FrameExtractionInfo.IsValidIndex(Idx))
			{
				Anim->CombatData.FrameExtractionInfo[Idx].SpriteOffset = FIntPoint::ZeroValue;
				Anim->CombatData.FrameExtractionInfo[Idx].bHasCustomAlignment = false;
			}
		}
		break;
	}
	case 2: // Mirror X
	{
		for (int32 Idx : TargetFrames)
		{
			if (Anim->CombatData.FrameExtractionInfo.IsValidIndex(Idx))
			{
				Anim->CombatData.FrameExtractionInfo[Idx].SpriteOffset.X *= -1;
			}
		}
		break;
	}
	case 3: // Mirror Y
	{
		for (int32 Idx : TargetFrames)
		{
			if (Anim->CombatData.FrameExtractionInfo.IsValidIndex(Idx))
			{
				Anim->CombatData.FrameExtractionInfo[Idx].SpriteOffset.Y *= -1;
			}
		}
		break;
	}
	case 4: // Custom Offset
	{
		for (int32 Idx : TargetFrames)
		{
			if (Anim->CombatData.FrameExtractionInfo.IsValidIndex(Idx))
			{
				Anim->CombatData.FrameExtractionInfo[Idx].SpriteOffset = AlignBatchCustomValue;
				Anim->CombatData.FrameExtractionInfo[Idx].bHasCustomAlignment = (AlignBatchCustomValue != FIntPoint::ZeroValue);
			}
		}
		break;
	}
	}

	EndTransaction();
	RefreshSpriteEditorFrameList();
}

#undef LOCTEXT_NAMESPACE
