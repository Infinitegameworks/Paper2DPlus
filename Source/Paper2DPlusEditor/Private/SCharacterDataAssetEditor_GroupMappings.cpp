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
// GROUP MAPPINGS PANEL
// ==========================================

TSharedRef<SWidget> SCharacterDataAssetEditor::BuildGroupMappingsPanel()
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
				.Text(LOCTEXT("GroupMappingsTitle", "GROUP MAPPINGS"))
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
					const int32 Mapped = Asset->GroupBindings.Num();
					const UPaper2DPlusSettings* Settings = UPaper2DPlusSettings::Get();
					const int32 Required = Settings ? Settings->RequiredAnimationGroups.Num() : 0;
					if (Required > 0)
					{
						return FText::Format(LOCTEXT("RoleMappingsCount", "{0} mapped / {1} required"),
							FText::AsNumber(Mapped), FText::AsNumber(Required));
					}
					return FText::Format(LOCTEXT("RoleMappingsCountOnly", "{0} mapped"), FText::AsNumber(Mapped));
				})
				.ColorAndOpacity(FSlateColor(FLinearColor(0.7f, 0.7f, 0.7f)))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
			]
		]

		// Group mappings list
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			[
				SAssignNew(GroupMappingsListBox, SVerticalBox)
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
				.Text(LOCTEXT("AddRoleMapping", "+ Add Group Mapping"))
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
							BuildAddGroupMappingMenuContent()
						]
					]
				]
			]
			.ToolTipText(LOCTEXT("AddRoleMappingTooltip", "Add a new animation group mapping for this character from required groups."))
		];
}

void SCharacterDataAssetEditor::RefreshGroupMappingsPanel()
{
	if (!GroupMappingsListBox.IsValid() || !Asset.IsValid()) return;

	GroupMappingsListBox->ClearChildren();

	// Collect animation names for combo box (member variable — SComboBox OptionsSource needs persistent pointer)
	GroupMappingAnimNameOptions.Reset();
	GroupMappingAnimNameOptions.Add(MakeShared<FString>(TEXT("(none)")));
	for (const FAnimationHitboxData& Anim : Asset->Animations)
	{
		GroupMappingAnimNameOptions.Add(MakeShared<FString>(Anim.AnimationName));
	}

	// Get settings for required groups and descriptions
	const UPaper2DPlusSettings* Settings = UPaper2DPlusSettings::Get();

	// Build sorted key list (stable iteration order)
	TArray<FGameplayTag> Tags;
	Asset->GroupBindings.GetKeys(Tags);

	// Show unmapped required groups at the bottom with warning
	TArray<FGameplayTag> UnmappedRequired;
	if (Settings)
	{
		for (const FGameplayTag& RequiredTag : Settings->RequiredAnimationGroups)
		{
			if (RequiredTag.IsValid() && !Asset->GroupBindings.Contains(RequiredTag))
			{
				UnmappedRequired.Add(RequiredTag);
			}
		}
	}

	for (auto It = Asset->GroupBindings.CreateIterator(); It; ++It)
	{
		const FGameplayTag& GroupTag = It->Key;
		FAnimationGroupBinding& Binding = It->Value;

		FText GroupTooltip = FText::GetEmpty();
		bool bIsRequired = false;
		if (Settings)
		{
			GroupTooltip = Settings->GetDescriptionForGroup(GroupTag);
			bIsRequired = Settings->RequiredAnimationGroups.Contains(GroupTag);
		}

		// Build animation entries widget
		TSharedRef<SVerticalBox> AnimEntriesBox = SNew(SVerticalBox);

		for (int32 AnimIdx = 0; AnimIdx < Binding.AnimationNames.Num(); ++AnimIdx)
		{
			const int32 CapturedAnimIdx = AnimIdx;
			const FGameplayTag CapturedTag = GroupTag;

			// Find matching option for current name
			TSharedPtr<FString> CurrentSelection;
			for (const TSharedPtr<FString>& Option : GroupMappingAnimNameOptions)
			{
				if (Option->Equals(Binding.AnimationNames[AnimIdx], ESearchCase::IgnoreCase))
				{
					CurrentSelection = Option;
					break;
				}
			}
			if (!CurrentSelection)
			{
				CurrentSelection = GroupMappingAnimNameOptions[0]; // "(none)"
			}

			AnimEntriesBox->AddSlot()
			.AutoHeight()
			.Padding(0, 1)
			[
				SNew(SHorizontalBox)

				// Animation name dropdown
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					SNew(SComboBox<TSharedPtr<FString>>)
					.OptionsSource(&GroupMappingAnimNameOptions)
					.InitiallySelectedItem(CurrentSelection)
					.OnSelectionChanged_Lambda([this, CapturedTag, CapturedAnimIdx](TSharedPtr<FString> NewValue, ESelectInfo::Type)
					{
						if (!Asset.IsValid() || !NewValue.IsValid()) return;

						if (FAnimationGroupBinding* Bind = Asset->GroupBindings.Find(CapturedTag))
						{
							if (Bind->AnimationNames.IsValidIndex(CapturedAnimIdx))
							{
								BeginTransaction(LOCTEXT("ChangeRoleAnim", "Change Group Animation"));
								Bind->AnimationNames[CapturedAnimIdx] = *NewValue;
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
						.Text_Lambda([this, CapturedTag, CapturedAnimIdx]()
						{
							if (!Asset.IsValid()) return FText::GetEmpty();
							if (const FAnimationGroupBinding* Bind = Asset->GroupBindings.Find(CapturedTag))
							{
								if (Bind->AnimationNames.IsValidIndex(CapturedAnimIdx))
								{
									return FText::FromString(Bind->AnimationNames[CapturedAnimIdx]);
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
					.Text(LOCTEXT("MoveUp", "^"))
					.ToolTipText(LOCTEXT("MoveUpTooltip", "Move this animation up in the combo order."))
					.IsEnabled_Lambda([CapturedAnimIdx]() { return CapturedAnimIdx > 0; })
					.OnClicked_Lambda([this, CapturedTag, CapturedAnimIdx]()
					{
						if (!Asset.IsValid()) return FReply::Handled();
						if (FAnimationGroupBinding* Bind = Asset->GroupBindings.Find(CapturedTag))
						{
							if (CapturedAnimIdx > 0 && Bind->AnimationNames.IsValidIndex(CapturedAnimIdx))
							{
								BeginTransaction(LOCTEXT("ReorderRoleAnim", "Reorder Group Animation"));
								Bind->AnimationNames.Swap(CapturedAnimIdx, CapturedAnimIdx - 1);
								EndTransaction();
								RefreshGroupMappingsPanel();
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
					.Text(LOCTEXT("MoveDown", "v"))
					.ToolTipText(LOCTEXT("MoveDownTooltip", "Move this animation down in the combo order."))
					.IsEnabled_Lambda([this, CapturedTag, CapturedAnimIdx]()
					{
						if (!Asset.IsValid()) return false;
						if (const FAnimationGroupBinding* Bind = Asset->GroupBindings.Find(CapturedTag))
						{
							return CapturedAnimIdx < Bind->AnimationNames.Num() - 1;
						}
						return false;
					})
					.OnClicked_Lambda([this, CapturedTag, CapturedAnimIdx]()
					{
						if (!Asset.IsValid()) return FReply::Handled();
						if (FAnimationGroupBinding* Bind = Asset->GroupBindings.Find(CapturedTag))
						{
							if (Bind->AnimationNames.IsValidIndex(CapturedAnimIdx + 1))
							{
								BeginTransaction(LOCTEXT("ReorderRoleAnim2", "Reorder Group Animation"));
								Bind->AnimationNames.Swap(CapturedAnimIdx, CapturedAnimIdx + 1);
								EndTransaction();
								RefreshGroupMappingsPanel();
							}
						}
						return FReply::Handled();
					})
				]

				// Remove animation button
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(2, 0)
				[
					SNew(SButton)
					.Text(LOCTEXT("RemoveAnim", "-"))
					.ToolTipText(LOCTEXT("RemoveAnimTooltip", "Remove this animation from the group."))
					.OnClicked_Lambda([this, CapturedTag, CapturedAnimIdx]()
					{
						if (!Asset.IsValid()) return FReply::Handled();
						if (FAnimationGroupBinding* Bind = Asset->GroupBindings.Find(CapturedTag))
						{
							if (Bind->AnimationNames.IsValidIndex(CapturedAnimIdx))
							{
								BeginTransaction(LOCTEXT("RemoveRoleAnimTrans", "Remove Animation from Group"));
								Bind->AnimationNames.RemoveAt(CapturedAnimIdx);
								EndTransaction();
								RefreshGroupMappingsPanel();
							}
						}
						return FReply::Handled();
					})
				]
			];
		}

		// Add "add animation to group" button
		const FGameplayTag CapturedTag = GroupTag;
		AnimEntriesBox->AddSlot()
		.AutoHeight()
		.Padding(0, 2)
		[
			SNew(SButton)
			.Text(LOCTEXT("AddAnimToRole", "+ Add Animation"))
			.ToolTipText(LOCTEXT("AddAnimToGroupTooltip", "Add an animation to this group mapping."))
			.OnClicked_Lambda([this, CapturedTag]()
			{
				if (!Asset.IsValid()) return FReply::Handled();
				if (FAnimationGroupBinding* Bind = Asset->GroupBindings.Find(CapturedTag))
				{
					BeginTransaction(LOCTEXT("AddAnimToRoleTrans", "Add Animation to Group"));
					// Default to first available animation name, or empty
					FString DefaultName;
					if (Asset->Animations.Num() > 0)
					{
						DefaultName = Asset->Animations[0].AnimationName;
					}
					Bind->AnimationNames.Add(DefaultName);
					EndTransaction();
					RefreshGroupMappingsPanel();
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

			AnimEntriesBox->AddSlot()
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
						if (const FAnimationGroupBinding* Bind = Asset->GroupBindings.Find(PZDCapturedTag))
						{
							return Bind->PaperZDSequence.ToSoftObjectPath().ToString();
						}
						return FString();
					})
					.OnObjectChanged_Lambda([this, PZDCapturedTag](const FAssetData& AssetData)
					{
						if (!Asset.IsValid()) return;
						BeginTransaction(LOCTEXT("SetPZDSequence", "Set PaperZD Sequence"));
						if (FAnimationGroupBinding* Bind = Asset->GroupBindings.Find(PZDCapturedTag))
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
		bool bHasValidAnims = false;
		for (const FString& AnimName : Binding.AnimationNames)
		{
			if (Asset.IsValid() && Asset->FindAnimationPtr(AnimName) != nullptr)
			{
				bHasValidAnims = true;
				break;
			}
		}

		GroupMappingsListBox->AddSlot()
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

							if (Asset->GroupBindings.Contains(NewTag))
							{
								FNotificationInfo Info(FText::Format(
									LOCTEXT("DuplicateGroupTagWarning", "Group '{0}' already exists. Choose a different tag."),
									NewTag.GetTagName().IsNone() ? LOCTEXT("NoneGroupTagLabel", "None") : FText::FromName(NewTag.GetTagName())));
								Info.ExpireDuration = 4.0f;
								FSlateNotificationManager::Get().AddNotification(Info);
								RefreshGroupMappingsPanel();
								return;
							}

							BeginTransaction(LOCTEXT("ChangeGroupTag2", "Change Group Tag"));
							if (FAnimationGroupBinding* OldBinding = Asset->GroupBindings.Find(CapturedTag))
							{
								FAnimationGroupBinding Copy = *OldBinding;
								Asset->GroupBindings.Remove(CapturedTag);
								Asset->GroupBindings.Add(NewTag, MoveTemp(Copy));
							}
							EndTransaction();
							RefreshGroupMappingsPanel();
						})
					]

					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(4, 0)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("RequiredStar", "*"))
						.ToolTipText(LOCTEXT("RequiredRoleTip", "Required by project settings"))
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
						.Visibility((!bHasValidAnims && Binding.AnimationNames.Num() == 0) ? EVisibility::Visible : EVisibility::Collapsed)
					]

					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(SButton)
						.Text(LOCTEXT("RemoveRoleX", "X"))
						.ToolTipText(LOCTEXT("RemoveRoleTip", "Remove this group mapping"))
						.OnClicked_Lambda([this, CapturedTag]()
						{
							if (!Asset.IsValid()) return FReply::Handled();
							BeginTransaction(LOCTEXT("RemoveRoleTrans", "Remove Group Mapping"));
							Asset->GroupBindings.Remove(CapturedTag);
							EndTransaction();
							RefreshGroupMappingsPanel();
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

				// Animation entries
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(16, 4, 0, 0)
				[
					AnimEntriesBox
				]
			]
		];
	}

	// Show unmapped required groups as warning entries
	for (const FGameplayTag& UnmappedTag : UnmappedRequired)
	{
		FText GroupTooltip = Settings ? Settings->GetDescriptionForGroup(UnmappedTag) : FText::GetEmpty();
		const FGameplayTag CapturedTag = UnmappedTag;

		GroupMappingsListBox->AddSlot()
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
					.Text(FText::Format(LOCTEXT("UnmappedRequiredRole", "! {0} — UNMAPPED (required)"),
						FText::FromString(UnmappedTag.ToString())))
					.ColorAndOpacity(FLinearColor(1.0f, 0.5f, 0.0f))
					.ToolTipText(GroupTooltip)
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SButton)
					.Text(LOCTEXT("MapRole", "+ Map"))
					.ToolTipText(LOCTEXT("MapGroupTooltip", "Create a mapping for this required group."))
					.OnClicked_Lambda([this, CapturedTag]()
					{
						if (!Asset.IsValid()) return FReply::Handled();
						BeginTransaction(LOCTEXT("MapRequiredRole", "Map Required Group"));
						Asset->GroupBindings.Add(CapturedTag, FAnimationGroupBinding());
						EndTransaction();
						RefreshGroupMappingsPanel();
						return FReply::Handled();
					})
				]
			]
		];
	}
}

TSharedRef<SWidget> SCharacterDataAssetEditor::BuildAddGroupMappingMenuContent()
{
	TSharedRef<SVerticalBox> MenuBox = SNew(SVerticalBox);

	if (!Asset.IsValid()) return MenuBox;

	const UPaper2DPlusSettings* Settings = UPaper2DPlusSettings::Get();

	// Show unmapped required groups first
	if (Settings)
	{
		for (const FGameplayTag& RequiredTag : Settings->RequiredAnimationGroups)
		{
			if (!RequiredTag.IsValid() || Asset->GroupBindings.Contains(RequiredTag))
			{
				continue;
			}

			const FGameplayTag CapturedTag = RequiredTag;
			FText Description = Settings->GetDescriptionForGroup(RequiredTag);
			FText Label = Description.IsEmpty()
				? FText::FromString(RequiredTag.ToString())
				: FText::Format(LOCTEXT("GroupMenuLabel", "{0} — {1}"), FText::FromString(RequiredTag.ToString()), Description);

			MenuBox->AddSlot()
			.AutoHeight()
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "Menu.Button")
				.ContentPadding(FMargin(4, 2))
				.OnClicked_Lambda([this, CapturedTag]()
				{
					if (!Asset.IsValid()) return FReply::Handled();
					BeginTransaction(LOCTEXT("AddGroupFromMenu", "Add Group Mapping"));
					Asset->GroupBindings.Add(CapturedTag, FAnimationGroupBinding());
					EndTransaction();
					RefreshGroupMappingsPanel();
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
			BeginTransaction(LOCTEXT("AddCustomGroup", "Add Custom Group Mapping"));
			Asset->GroupBindings.Add(FGameplayTag(), FAnimationGroupBinding());
			EndTransaction();
			RefreshGroupMappingsPanel();
			FSlateApplication::Get().DismissAllMenus();
			return FReply::Handled();
		})
		[
			SNew(STextBlock)
			.Text(LOCTEXT("AddCustomGroupLabel", "Custom (empty tag)..."))
			.ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f)))
		]
	];

	return MenuBox;
}

#undef LOCTEXT_NAMESPACE
