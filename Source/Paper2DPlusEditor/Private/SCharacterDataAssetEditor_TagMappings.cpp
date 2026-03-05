// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "CharacterDataAssetEditor.h"
#include "Paper2DPlusSettings.h"
#include "SGameplayTagCombo.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Framework/Application/SlateApplication.h"
#include "PaperFlipbook.h"
#include "PropertyCustomizationHelpers.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Framework/Notifications/NotificationManager.h"

#define LOCTEXT_NAMESPACE "CharacterDataAssetEditor"

// ==========================================
// FLIPBOOK TAG MAPPINGS PANEL
// ==========================================

TSharedRef<SWidget> SCharacterDataAssetEditor::BuildTagMappingsPanel()
{
	return SNew(SVerticalBox)

		// Title + count
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0, 0, 0, 4)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(STextBlock)
				.Text(LOCTEXT("TagMappingsTitle", "FLIPBOOK TAG MAPPINGS"))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(8, 0, 0, 0)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text_Lambda([this]()
				{
					if (!Asset.IsValid()) return FText::GetEmpty();
					const int32 Mapped = Asset->TagMappings.Num();
					const UPaper2DPlusSettings* Settings = UPaper2DPlusSettings::Get();
					const int32 Required = Settings ? Settings->RequiredTagMappings.Num() : 0;
					if (Required > 0)
					{
						return FText::Format(LOCTEXT("TagMappingsCount", "{0} mapped / {1} required"),
							FText::AsNumber(Mapped), FText::AsNumber(Required));
					}
					return FText::Format(LOCTEXT("TagMappingsCountOnly", "{0} mapped"), FText::AsNumber(Mapped));
				})
				.ColorAndOpacity(FSlateColor(FLinearColor(0.7f, 0.7f, 0.7f)))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
			]
		]

		// Tag mappings list
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			[
				SAssignNew(TagMappingsListBox, SVerticalBox)
			]
		]

		// Add button at bottom
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0, 4, 0, 0)
		[
			SNew(SComboButton)
			.ButtonContent()
			[
				SNew(STextBlock)
				.Text(LOCTEXT("AddTagMapping", "+ Add Tag Mapping"))
			]
			.MenuContent()
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SBox)
					.MaxDesiredHeight(300.0f)
					[
						SNew(SScrollBox)
						+ SScrollBox::Slot()
						[
							BuildAddTagMappingMenuContent()
						]
					]
				]
			]
			.ToolTipText(LOCTEXT("AddTagMappingTooltip", "Add a new flipbook tag mapping for this character from required tags."))
		];
}

void SCharacterDataAssetEditor::RefreshTagMappingsPanel()
{
	if (!TagMappingsListBox.IsValid() || !Asset.IsValid()) return;

	TagMappingsListBox->ClearChildren();

	// Collect flipbook names for combo box, sorted alphabetically
	TagMappingFlipbookNameOptions.Reset();
	TagMappingFlipbookNameOptions.Add(MakeShared<FString>(TEXT("(none)")));
	TArray<int32> SortedIndices = GetSortedFlipbookIndices();
	for (int32 Idx : SortedIndices)
	{
		TagMappingFlipbookNameOptions.Add(MakeShared<FString>(Asset->Flipbooks[Idx].FlipbookName));
	}

	// Get settings for required tags and descriptions
	const UPaper2DPlusSettings* Settings = UPaper2DPlusSettings::Get();

	// Build sorted key list (stable iteration order)
	TArray<FGameplayTag> Tags;
	Asset->TagMappings.GetKeys(Tags);

	// Show unmapped required tags at the bottom with warning
	TArray<FGameplayTag> UnmappedRequired;
	if (Settings)
	{
		for (const FGameplayTag& RequiredTag : Settings->RequiredTagMappings)
		{
			if (RequiredTag.IsValid() && !Asset->TagMappings.Contains(RequiredTag))
			{
				UnmappedRequired.Add(RequiredTag);
			}
		}
	}

	for (auto It = Asset->TagMappings.CreateIterator(); It; ++It)
	{
		const FGameplayTag& GroupTag = It->Key;
		FFlipbookTagMapping& Binding = It->Value;

		FText GroupTooltip = FText::GetEmpty();
		bool bIsRequired = false;
		if (Settings)
		{
			GroupTooltip = Settings->GetDescriptionForTag(GroupTag);
			bIsRequired = Settings->RequiredTagMappings.Contains(GroupTag);
		}

		// Build flipbook entries widget
		TSharedRef<SVerticalBox> FlipbookEntriesBox = SNew(SVerticalBox);

		for (int32 FlipbookIdx = 0; FlipbookIdx < Binding.FlipbookNames.Num(); ++FlipbookIdx)
		{
			const int32 CapturedFlipbookIdx = FlipbookIdx;
			const FGameplayTag CapturedTag = GroupTag;

			// Find matching option for current name
			TSharedPtr<FString> CurrentSelection;
			for (const TSharedPtr<FString>& Option : TagMappingFlipbookNameOptions)
			{
				if (Option->Equals(Binding.FlipbookNames[FlipbookIdx], ESearchCase::IgnoreCase))
				{
					CurrentSelection = Option;
					break;
				}
			}
			if (!CurrentSelection)
			{
				CurrentSelection = TagMappingFlipbookNameOptions[0]; // "(none)"
			}

			FlipbookEntriesBox->AddSlot()
			.AutoHeight()
			.Padding(0, 1)
			[
				SNew(SHorizontalBox)

				// Flipbook name dropdown
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					SNew(SComboBox<TSharedPtr<FString>>)
					.OptionsSource(&TagMappingFlipbookNameOptions)
					.InitiallySelectedItem(CurrentSelection)
					.OnSelectionChanged_Lambda([this, CapturedTag, CapturedFlipbookIdx](TSharedPtr<FString> NewValue, ESelectInfo::Type)
					{
						if (!Asset.IsValid() || !NewValue.IsValid()) return;

						if (FFlipbookTagMapping* Bind = Asset->TagMappings.Find(CapturedTag))
						{
							if (Bind->FlipbookNames.IsValidIndex(CapturedFlipbookIdx))
							{
								BeginTransaction(LOCTEXT("ChangeTagMappingFlipbook", "Change Tag Mapping Flipbook"));
								Bind->FlipbookNames[CapturedFlipbookIdx] = *NewValue;
								EndTransaction();
							}
						}
					})
					.OnGenerateWidget_Lambda([](TSharedPtr<FString> Item)
					{
						return SNew(STextBlock).Text(FText::FromString(*Item));
					})
					.Content()
					[
						SNew(STextBlock)
						.Text_Lambda([this, CapturedTag, CapturedFlipbookIdx]()
						{
							if (!Asset.IsValid()) return FText::GetEmpty();
							if (const FFlipbookTagMapping* Bind = Asset->TagMappings.Find(CapturedTag))
							{
								if (Bind->FlipbookNames.IsValidIndex(CapturedFlipbookIdx))
								{
									return FText::FromString(Bind->FlipbookNames[CapturedFlipbookIdx]);
								}
							}
							return LOCTEXT("None", "(none)");
						})
					]
				]

				// Move up button
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(2, 0)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
					.Text(LOCTEXT("MoveUp", "^"))
					.ToolTipText(LOCTEXT("MoveUpTooltip", "Move this flipbook up in the combo order."))
					.IsEnabled_Lambda([CapturedFlipbookIdx]() { return CapturedFlipbookIdx > 0; })
					.OnClicked_Lambda([this, CapturedTag, CapturedFlipbookIdx]()
					{
						if (!Asset.IsValid()) return FReply::Handled();
						if (FFlipbookTagMapping* Bind = Asset->TagMappings.Find(CapturedTag))
						{
							if (CapturedFlipbookIdx > 0 && Bind->FlipbookNames.IsValidIndex(CapturedFlipbookIdx))
							{
								BeginTransaction(LOCTEXT("ReorderTagMappingFlipbook", "Reorder Tag Mapping Flipbook"));
								Bind->FlipbookNames.Swap(CapturedFlipbookIdx, CapturedFlipbookIdx - 1);
								EndTransaction();
								RefreshTagMappingsPanel();
							}
						}
						return FReply::Handled();
					})
				]

				// Move down button
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(2, 0)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
					.Text(LOCTEXT("MoveDown", "v"))
					.ToolTipText(LOCTEXT("MoveDownTooltip", "Move this flipbook down in the combo order."))
					.IsEnabled_Lambda([this, CapturedTag, CapturedFlipbookIdx]()
					{
						if (!Asset.IsValid()) return false;
						if (const FFlipbookTagMapping* Bind = Asset->TagMappings.Find(CapturedTag))
						{
							return CapturedFlipbookIdx < Bind->FlipbookNames.Num() - 1;
						}
						return false;
					})
					.OnClicked_Lambda([this, CapturedTag, CapturedFlipbookIdx]()
					{
						if (!Asset.IsValid()) return FReply::Handled();
						if (FFlipbookTagMapping* Bind = Asset->TagMappings.Find(CapturedTag))
						{
							if (Bind->FlipbookNames.IsValidIndex(CapturedFlipbookIdx + 1))
							{
								BeginTransaction(LOCTEXT("ReorderTagMappingFlipbook2", "Reorder Tag Mapping Flipbook"));
								Bind->FlipbookNames.Swap(CapturedFlipbookIdx, CapturedFlipbookIdx + 1);
								EndTransaction();
								RefreshTagMappingsPanel();
							}
						}
						return FReply::Handled();
					})
				]

				// Remove flipbook button
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(2, 0)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
					.Text(LOCTEXT("RemoveAnim", "-"))
					.ToolTipText(LOCTEXT("RemoveFlipbookTooltip", "Remove this flipbook from the tag mapping."))
					.OnClicked_Lambda([this, CapturedTag, CapturedFlipbookIdx]()
					{
						if (!Asset.IsValid()) return FReply::Handled();
						if (FFlipbookTagMapping* Bind = Asset->TagMappings.Find(CapturedTag))
						{
							if (Bind->FlipbookNames.IsValidIndex(CapturedFlipbookIdx))
							{
								BeginTransaction(LOCTEXT("RemoveTagMappingFlipbookTrans", "Remove Flipbook from Tag Mapping"));
								Bind->FlipbookNames.RemoveAt(CapturedFlipbookIdx);
								EndTransaction();
								RefreshTagMappingsPanel();
							}
						}
						return FReply::Handled();
					})
				]
			];
		}

		// Add "add flipbook to tag mapping" button
		const FGameplayTag CapturedTag = GroupTag;
		FlipbookEntriesBox->AddSlot()
		.AutoHeight()
		.Padding(0, 2)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
			.Text(LOCTEXT("AddFlipbookToTagMapping", "+ Add Flipbook"))
			.ToolTipText(LOCTEXT("AddFlipbookToTagMappingTooltip", "Add a flipbook to this tag mapping."))
			.OnClicked_Lambda([this, CapturedTag]()
			{
				if (!Asset.IsValid()) return FReply::Handled();
				if (FFlipbookTagMapping* Bind = Asset->TagMappings.Find(CapturedTag))
				{
					BeginTransaction(LOCTEXT("AddFlipbookToTagMappingTrans", "Add Flipbook to Tag Mapping"));
					// Default to first available flipbook name, or empty
					FString DefaultName;
					if (Asset->Flipbooks.Num() > 0)
					{
						DefaultName = Asset->Flipbooks[0].FlipbookName;
					}
					Bind->FlipbookNames.Add(DefaultName);
					EndTransaction();
					RefreshTagMappingsPanel();
				}
				return FReply::Handled();
			})
		];

		// PaperZD Sequence field (only shown when PaperZD module is loaded)
		if (FModuleManager::Get().IsModuleLoaded(TEXT("PaperZD")))
		{
			const FGameplayTag PZDCapturedTag = GroupTag;

			// Find PaperZD AnimSequence class for filtered picker
			UClass* PZDSequenceClass = UClass::TryFindTypeSlow<UClass>(TEXT("PaperZDAnimSequence"));
			if (!PZDSequenceClass) PZDSequenceClass = UObject::StaticClass();

			FlipbookEntriesBox->AddSlot()
			.AutoHeight()
			.Padding(0, 4, 0, 0)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0, 0, 4, 0)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("PaperZDSequenceLabel", "PaperZD Sequence"))
					.Font(FAppStyle::GetFontStyle("SmallFont"))
				]

				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					SNew(SObjectPropertyEntryBox)
					.AllowedClass(PZDSequenceClass)
					.AllowClear(true)
					.ObjectPath_Lambda([this, PZDCapturedTag]() -> FString
					{
						if (!Asset.IsValid()) return FString();
						if (const FFlipbookTagMapping* Bind = Asset->TagMappings.Find(PZDCapturedTag))
						{
							return Bind->PaperZDSequence.ToSoftObjectPath().ToString();
						}
						return FString();
					})
					.OnObjectChanged_Lambda([this, PZDCapturedTag](const FAssetData& AssetData)
					{
						if (!Asset.IsValid()) return;
						BeginTransaction(LOCTEXT("SetPZDSequence", "Set PaperZD Sequence"));
						if (FFlipbookTagMapping* Bind = Asset->TagMappings.Find(PZDCapturedTag))
						{
							if (AssetData.IsValid())
							{
								Bind->PaperZDSequence = TSoftObjectPtr<UObject>(AssetData.GetSoftObjectPath());
							}
							else
							{
								Bind->PaperZDSequence = nullptr;
							}
						}
						EndTransaction();
					})
				]
			];
		}

		// Unmapped warning
		bool bHasValidFlipbooks = false;
		for (const FString& FlipbookNameStr : Binding.FlipbookNames)
		{
			if (Asset.IsValid() && Asset->FindFlipbookDataPtr(FlipbookNameStr) != nullptr)
			{
				bHasValidFlipbooks = true;
				break;
			}
		}

		TagMappingsListBox->AddSlot()
		.AutoHeight()
		.Padding(0, 2)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
			.Padding(4)
			[
				SNew(SVerticalBox)

				// Row header: Tag picker + info + remove
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SHorizontalBox)

					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.VAlign(VAlign_Center)
					[
						SNew(SGameplayTagCombo)
						.Filter(TEXT("Paper2DPlus.Animation"))
						.Tag(GroupTag)
						.OnTagChanged_Lambda([this, CapturedTag](const FGameplayTag& NewTag)
						{
							if (!Asset.IsValid() || NewTag == CapturedTag) return;

							if (Asset->TagMappings.Contains(NewTag))
							{
								FNotificationInfo Info(FText::Format(
									LOCTEXT("DuplicateTagWarning", "Tag '{0}' already exists. Choose a different tag."),
									NewTag.GetTagName().IsNone() ? LOCTEXT("NoneTagLabel", "None") : FText::FromName(NewTag.GetTagName())));
								Info.ExpireDuration = 4.0f;
								FSlateNotificationManager::Get().AddNotification(Info);
								RefreshTagMappingsPanel();
								return;
							}

							BeginTransaction(LOCTEXT("ChangeTagMappingTag", "Change Tag Mapping Tag"));
							if (FFlipbookTagMapping* OldBinding = Asset->TagMappings.Find(CapturedTag))
							{
								FFlipbookTagMapping Copy = *OldBinding;
								Asset->TagMappings.Remove(CapturedTag);
								Asset->TagMappings.Add(NewTag, MoveTemp(Copy));
							}
							EndTransaction();
							RefreshTagMappingsPanel();
						})
					]

					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(4, 0)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("RequiredStar", "*"))
						.ToolTipText(LOCTEXT("RequiredTagMappingTip", "Required by project settings"))
						.ColorAndOpacity(FLinearColor::Yellow)
						.Visibility(bIsRequired ? EVisibility::Visible : EVisibility::Collapsed)
					]

					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(4, 0)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("UnmappedWarning", "! UNMAPPED"))
						.ColorAndOpacity(FLinearColor(1.0f, 0.5f, 0.0f))
						.Visibility((!bHasValidFlipbooks && Binding.FlipbookNames.Num() == 0) ? EVisibility::Visible : EVisibility::Collapsed)
					]

					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(SButton)
						.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
						.Text(LOCTEXT("RemoveTagMappingX", "X"))
						.ToolTipText(LOCTEXT("RemoveTagMappingTip", "Remove this tag mapping"))
						.OnClicked_Lambda([this, CapturedTag]()
						{
							if (!Asset.IsValid()) return FReply::Handled();
							BeginTransaction(LOCTEXT("RemoveTagMappingTrans", "Remove Tag Mapping"));
							Asset->TagMappings.Remove(CapturedTag);
							EndTransaction();
							RefreshTagMappingsPanel();
							return FReply::Handled();
						})
					]
				]

				// Description tooltip
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0, 2, 0, 0)
				[
					SNew(STextBlock)
					.Text(GroupTooltip)
					.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)))
					.Visibility(GroupTooltip.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible)
				]

				// Flipbook entries
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(16, 4, 0, 0)
				[
					FlipbookEntriesBox
				]
			]
		];
	}

	// Show unmapped required tags as warning entries
	for (const FGameplayTag& UnmappedTag : UnmappedRequired)
	{
		FText GroupTooltip = Settings ? Settings->GetDescriptionForTag(UnmappedTag) : FText::GetEmpty();
		const FGameplayTag CapturedTag = UnmappedTag;

		TagMappingsListBox->AddSlot()
		.AutoHeight()
		.Padding(0, 2)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
			.BorderBackgroundColor(FLinearColor(0.3f, 0.2f, 0.0f, 0.3f))
			.Padding(4)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(FText::Format(LOCTEXT("UnmappedRequiredTag", "! {0} — UNMAPPED (required)"),
						FText::FromString(UnmappedTag.ToString())))
					.ColorAndOpacity(FLinearColor(1.0f, 0.5f, 0.0f))
					.ToolTipText(GroupTooltip)
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
					.Text(LOCTEXT("MapTag", "+ Map"))
					.ToolTipText(LOCTEXT("MapTagTooltip", "Create a mapping for this required tag."))
					.OnClicked_Lambda([this, CapturedTag]()
					{
						if (!Asset.IsValid()) return FReply::Handled();
						BeginTransaction(LOCTEXT("MapRequiredTag", "Map Required Tag"));
						Asset->TagMappings.Add(CapturedTag, FFlipbookTagMapping());
						EndTransaction();
						RefreshTagMappingsPanel();
						return FReply::Handled();
					})
				]
			]
		];
	}
}

TSharedRef<SWidget> SCharacterDataAssetEditor::BuildAddTagMappingMenuContent()
{
	TSharedRef<SVerticalBox> MenuBox = SNew(SVerticalBox);

	if (!Asset.IsValid()) return MenuBox;

	const UPaper2DPlusSettings* Settings = UPaper2DPlusSettings::Get();

	// Show unmapped required tags first
	if (Settings)
	{
		for (const FGameplayTag& RequiredTag : Settings->RequiredTagMappings)
		{
			if (!RequiredTag.IsValid() || Asset->TagMappings.Contains(RequiredTag))
			{
				continue;
			}

			const FGameplayTag CapturedTag = RequiredTag;
			FText Description = Settings->GetDescriptionForTag(RequiredTag);
			FText Label = Description.IsEmpty()
				? FText::FromString(RequiredTag.ToString())
				: FText::Format(LOCTEXT("TagMappingMenuLabel", "{0} — {1}"), FText::FromString(RequiredTag.ToString()), Description);

			MenuBox->AddSlot()
			.AutoHeight()
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "Menu.Button")
				.ContentPadding(FMargin(4, 2))
				.OnClicked_Lambda([this, CapturedTag]()
				{
					if (!Asset.IsValid()) return FReply::Handled();
					BeginTransaction(LOCTEXT("AddTagMappingFromMenu", "Add Tag Mapping"));
					Asset->TagMappings.Add(CapturedTag, FFlipbookTagMapping());
					EndTransaction();
					RefreshTagMappingsPanel();
					FSlateApplication::Get().DismissAllMenus();
					return FReply::Handled();
				})
				[
					SNew(STextBlock)
					.Text(Label)
				]
			];
		}
	}

	// Always show a "Custom..." option to add an arbitrary tag
	MenuBox->AddSlot()
	.AutoHeight()
	.Padding(0, 2, 0, 0)
	[
		SNew(SButton)
		.ButtonStyle(FAppStyle::Get(), "Menu.Button")
		.ContentPadding(FMargin(4, 2))
		.OnClicked_Lambda([this]()
		{
			if (!Asset.IsValid()) return FReply::Handled();
			BeginTransaction(LOCTEXT("AddCustomTagMapping", "Add Custom Tag Mapping"));
			Asset->TagMappings.Add(FGameplayTag(), FFlipbookTagMapping());
			EndTransaction();
			RefreshTagMappingsPanel();
			FSlateApplication::Get().DismissAllMenus();
			return FReply::Handled();
		})
		[
			SNew(STextBlock)
			.Text(LOCTEXT("AddCustomTagMappingLabel", "Custom (empty tag)..."))
			.ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f)))
		]
	];

	return MenuBox;
}

#undef LOCTEXT_NAMESPACE
