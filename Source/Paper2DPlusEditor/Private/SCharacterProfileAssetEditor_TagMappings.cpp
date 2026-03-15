// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "CharacterProfileAssetEditor.h"
#include "Paper2DPlusSettings.h"
#include "SGameplayTagCombo.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Framework/Application/SlateApplication.h"
#include "PaperFlipbook.h"
#include "PropertyCustomizationHelpers.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Framework/Notifications/NotificationManager.h"

#define LOCTEXT_NAMESPACE "CharacterProfileAssetEditor"

// ==========================================
// TAG MAPPING DROP TARGET
// ==========================================

/** Wrapper widget that accepts FFlipbookGroupDragDropOp drops onto a tag mapping card. */
class STagMappingDropTarget : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(STagMappingDropTarget) {}
		SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_END_ARGS()

	TFunction<void(const TArray<int32>&)> OnDropFunc;

	void Construct(const FArguments& InArgs)
	{
		ChildSlot[ InArgs._Content.Widget ];
	}

	virtual void OnDragEnter(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override
	{
		if (DragDropEvent.GetOperationAs<FFlipbookGroupDragDropOp>().IsValid())
		{
			bDragOver = true;
			Invalidate(EInvalidateWidgetReason::Paint);
		}
	}

	virtual void OnDragLeave(const FDragDropEvent& DragDropEvent) override
	{
		bDragOver = false;
		Invalidate(EInvalidateWidgetReason::Paint);
	}

	virtual FReply OnDragOver(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override
	{
		if (DragDropEvent.GetOperationAs<FFlipbookGroupDragDropOp>().IsValid())
		{
			return FReply::Handled();
		}
		return FReply::Unhandled();
	}

	virtual FReply OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override
	{
		bDragOver = false;
		Invalidate(EInvalidateWidgetReason::Paint);

		TSharedPtr<FFlipbookGroupDragDropOp> Op = DragDropEvent.GetOperationAs<FFlipbookGroupDragDropOp>();
		if (Op.IsValid() && OnDropFunc)
		{
			OnDropFunc(Op->FlipbookIndices);
			return FReply::Handled();
		}
		return FReply::Unhandled();
	}

private:
	bool bDragOver = false;
};

// ==========================================
// FLIPBOOK TAG MAPPINGS PANEL
// ==========================================

TSharedRef<SWidget> SCharacterProfileAssetEditor::BuildTagMappingsPanel()
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
				.Text(LOCTEXT("TagMappingsTitle", "TAG MAPPINGS"))
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
					int32 WithFlipbooks = 0;
					for (auto& Pair : Asset->TagMappings)
					{
						if (Pair.Value.FlipbookNames.Num() > 0) WithFlipbooks++;
					}
					return FText::Format(LOCTEXT("TagMappingsCountFmt", "{0} / {1} mapped"),
						FText::AsNumber(WithFlipbooks), FText::AsNumber(Mapped));
				})
				.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
			]
		]

		// Hint text
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0, 0, 0, 6)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("TagMappingsDragHint", "Drag flipbooks from the list to assign them to a tag."))
			.ColorAndOpacity(FSlateColor(FLinearColor(0.4f, 0.4f, 0.4f)))
			.Font(FCoreStyle::GetDefaultFontStyle("Italic", 8))
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

		// Add custom tag button at bottom
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0, 6, 0, 0)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
			.ToolTipText(LOCTEXT("AddCustomTagTip", "Add a tag mapping with a custom gameplay tag"))
			.OnClicked_Lambda([this]()
			{
				if (!Asset.IsValid()) return FReply::Handled();
				BeginTransaction(LOCTEXT("AddCustomTagMapping", "Add Custom Tag Mapping"));
				Asset->TagMappings.Add(FGameplayTag(), FFlipbookTagMapping());
				EndTransaction();
				RefreshTagMappingsPanel();
				return FReply::Handled();
			})
			[
				SNew(STextBlock)
				.Text(LOCTEXT("AddCustomTag", "+ Add Custom Tag"))
			]
		];
}

void SCharacterProfileAssetEditor::EnsureRequiredTagMappingsExist()
{
	if (!Asset.IsValid()) return;

	const UPaper2DPlusSettings* Settings = UPaper2DPlusSettings::Get();
	if (!Settings) return;

	bool bAdded = false;
	for (const FGameplayTag& RequiredTag : Settings->RequiredTagMappings)
	{
		if (RequiredTag.IsValid() && !Asset->TagMappings.Contains(RequiredTag))
		{
			if (!bAdded)
			{
				BeginTransaction(LOCTEXT("AutoAddRequiredTags", "Auto-Add Required Tag Mappings"));
				bAdded = true;
			}
			Asset->TagMappings.Add(RequiredTag, FFlipbookTagMapping());
		}
	}
	if (bAdded)
	{
		EndTransaction();
	}
}

void SCharacterProfileAssetEditor::RefreshTagMappingsPanel()
{
	if (!TagMappingsListBox.IsValid() || !Asset.IsValid()) return;

	// Auto-populate required tags from project settings
	EnsureRequiredTagMappingsExist();

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

	// Build sorted key list (alphabetical by tag string)
	TArray<FGameplayTag> Tags;
	Asset->TagMappings.GetKeys(Tags);
	Tags.Sort([](const FGameplayTag& A, const FGameplayTag& B)
	{
		return A.ToString() < B.ToString();
	});

	for (const FGameplayTag& GroupTag : Tags)
	{
		FFlipbookTagMapping& Binding = Asset->TagMappings[GroupTag];
		const FGameplayTag CapturedTag = GroupTag;

		FText GroupDescription = FText::GetEmpty();
		bool bIsRequired = false;
		if (Settings)
		{
			GroupDescription = Settings->GetDescriptionForTag(GroupTag);
			bIsRequired = Settings->RequiredTagMappings.Contains(GroupTag);
		}

		// Check if this mapping has any valid flipbooks assigned
		bool bHasFlipbooks = Binding.FlipbookNames.Num() > 0;

		// Readable tag display name: strip common prefix for cleaner display
		FString TagDisplayStr = GroupTag.IsValid() ? GroupTag.ToString() : TEXT("(no tag)");
		// Strip "Paper2DPlus.Animation." prefix if present for cleaner display
		static const FString CommonPrefix = TEXT("Paper2DPlus.Animation.");
		FString ShortTagName = TagDisplayStr;
		if (ShortTagName.StartsWith(CommonPrefix))
		{
			ShortTagName = ShortTagName.RightChop(CommonPrefix.Len());
		}

		// Build flipbook entries widget
		TSharedRef<SVerticalBox> FlipbookEntriesBox = SNew(SVerticalBox);

		for (int32 FlipbookIdx = 0; FlipbookIdx < Binding.FlipbookNames.Num(); ++FlipbookIdx)
		{
			const int32 CapturedFlipbookIdx = FlipbookIdx;

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

				// Index number
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0, 0, 4, 0)
				[
					SNew(STextBlock)
					.Text(FText::AsNumber(FlipbookIdx + 1))
					.ColorAndOpacity(FSlateColor(FLinearColor(0.4f, 0.4f, 0.4f)))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
				]

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

				// Move up
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(2, 0)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "SimpleButton")
					.ToolTipText(LOCTEXT("MoveUpTooltip", "Move up in combo order"))
					.IsEnabled_Lambda([CapturedFlipbookIdx]() { return CapturedFlipbookIdx > 0; })
					.OnClicked_Lambda([this, CapturedTag, CapturedFlipbookIdx]()
					{
						if (!Asset.IsValid()) return FReply::Handled();
						if (FFlipbookTagMapping* Bind = Asset->TagMappings.Find(CapturedTag))
						{
							if (CapturedFlipbookIdx > 0 && Bind->FlipbookNames.IsValidIndex(CapturedFlipbookIdx))
							{
								BeginTransaction(LOCTEXT("ReorderTagMappingFlipbook", "Reorder Tag Mapping"));
								Bind->FlipbookNames.Swap(CapturedFlipbookIdx, CapturedFlipbookIdx - 1);
								if (Bind->PaperZDSequences.IsValidIndex(CapturedFlipbookIdx) && Bind->PaperZDSequences.IsValidIndex(CapturedFlipbookIdx - 1))
								{
									Bind->PaperZDSequences.Swap(CapturedFlipbookIdx, CapturedFlipbookIdx - 1);
								}
								EndTransaction();
								RefreshTagMappingsPanel();
							}
						}
						return FReply::Handled();
					})
					[
						SNew(STextBlock)
						.Text(FText::FromString(TEXT("\x25B2")))
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
					]
				]

				// Move down
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(2, 0)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "SimpleButton")
					.ToolTipText(LOCTEXT("MoveDownTooltip", "Move down in combo order"))
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
								BeginTransaction(LOCTEXT("ReorderTagMappingFlipbook2", "Reorder Tag Mapping"));
								Bind->FlipbookNames.Swap(CapturedFlipbookIdx, CapturedFlipbookIdx + 1);
								if (Bind->PaperZDSequences.IsValidIndex(CapturedFlipbookIdx) && Bind->PaperZDSequences.IsValidIndex(CapturedFlipbookIdx + 1))
								{
									Bind->PaperZDSequences.Swap(CapturedFlipbookIdx, CapturedFlipbookIdx + 1);
								}
								EndTransaction();
								RefreshTagMappingsPanel();
							}
						}
						return FReply::Handled();
					})
					[
						SNew(STextBlock)
						.Text(FText::FromString(TEXT("\x25BC")))
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
					]
				]

				// Remove flipbook
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(2, 0)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "SimpleButton")
					.ToolTipText(LOCTEXT("RemoveFlipbookTooltip", "Remove this flipbook from the tag mapping"))
					.OnClicked_Lambda([this, CapturedTag, CapturedFlipbookIdx]()
					{
						if (!Asset.IsValid()) return FReply::Handled();
						if (FFlipbookTagMapping* Bind = Asset->TagMappings.Find(CapturedTag))
						{
							if (Bind->FlipbookNames.IsValidIndex(CapturedFlipbookIdx))
							{
								BeginTransaction(LOCTEXT("RemoveTagMappingFlipbookTrans", "Remove Flipbook from Tag Mapping"));
								Bind->FlipbookNames.RemoveAt(CapturedFlipbookIdx);
								if (Bind->PaperZDSequences.IsValidIndex(CapturedFlipbookIdx))
								{
									Bind->PaperZDSequences.RemoveAt(CapturedFlipbookIdx);
								}
								EndTransaction();
								RefreshTagMappingsPanel();
							}
						}
						return FReply::Handled();
					})
					[
						SNew(STextBlock)
						.Text(FText::FromString(TEXT("\x2715")))
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
						.ColorAndOpacity(FSlateColor(FLinearColor(0.8f, 0.3f, 0.3f)))
					]
				]
			];

			// Per-flipbook PaperZD Sequence picker (only shown when PaperZD module is loaded)
			if (FModuleManager::Get().IsModuleLoaded(TEXT("PaperZD")))
			{
				UClass* PZDSequenceClass = UClass::TryFindTypeSlow<UClass>(TEXT("PaperZDAnimSequence"));
				if (!PZDSequenceClass) PZDSequenceClass = UObject::StaticClass();

				FlipbookEntriesBox->AddSlot()
				.AutoHeight()
				.Padding(20, 0, 0, 2) // Indent under flipbook row
				[
					SNew(SHorizontalBox)

					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0, 0, 4, 0)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("PaperZDSequenceLabel", "Sequence"))
						.Font(FAppStyle::GetFontStyle("SmallFont"))
						.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)))
					]

					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					[
						SNew(SObjectPropertyEntryBox)
						.AllowedClass(PZDSequenceClass)
						.AllowClear(true)
						.ObjectPath_Lambda([this, CapturedTag, CapturedFlipbookIdx]() -> FString
						{
							if (!Asset.IsValid()) return FString();
							if (const FFlipbookTagMapping* Bind = Asset->TagMappings.Find(CapturedTag))
							{
								if (Bind->PaperZDSequences.IsValidIndex(CapturedFlipbookIdx))
								{
									return Bind->PaperZDSequences[CapturedFlipbookIdx].ToSoftObjectPath().ToString();
								}
							}
							return FString();
						})
						.OnObjectChanged_Lambda([this, CapturedTag, CapturedFlipbookIdx](const FAssetData& AssetData)
						{
							if (!Asset.IsValid()) return;
							BeginTransaction(LOCTEXT("SetPZDSequence", "Set PaperZD Sequence"));
							if (FFlipbookTagMapping* Bind = Asset->TagMappings.Find(CapturedTag))
							{
								// Grow array to match if needed
								while (Bind->PaperZDSequences.Num() <= CapturedFlipbookIdx)
								{
									Bind->PaperZDSequences.AddDefaulted();
								}
								if (AssetData.IsValid())
								{
									Bind->PaperZDSequences[CapturedFlipbookIdx] = TSoftObjectPtr<UObject>(AssetData.GetSoftObjectPath());
								}
								else
								{
									Bind->PaperZDSequences[CapturedFlipbookIdx] = nullptr;
								}
							}
							EndTransaction();
						})
					]
				];
			}
		}

		// === Card border color based on state ===
		// === Build the card ===
		TSharedRef<STagMappingDropTarget> DropTarget = SNew(STagMappingDropTarget)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
			.Padding(8)
			[
				SNew(SVerticalBox)

				// === Row 1: Tag name ===
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(STextBlock)
					.Text(FText::FromString(ShortTagName))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
					.AutoWrapText(true)
					.ColorAndOpacity(GroupTag.IsValid()
						? FSlateColor(FLinearColor(0.8f, 0.9f, 1.0f))
						: FSlateColor(FLinearColor(0.6f, 0.4f, 0.4f)))
				]

				// === Row 2: Badges + tag picker + remove ===
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0, 3, 0, 0)
				[
					SNew(SHorizontalBox)

					// Required badge
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0, 0, 6, 0)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("RequiredBadge", "REQUIRED"))
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 7))
						.ColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.85f, 0.3f)))
						.Visibility(bIsRequired ? EVisibility::Visible : EVisibility::Collapsed)
					]

					// Empty warning
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0, 0, 6, 0)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("EmptyTagWarning", "No flipbooks assigned"))
						.Font(FCoreStyle::GetDefaultFontStyle("Italic", 8))
						.ColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.5f, 0.0f)))
						.Visibility(!bHasFlipbooks ? EVisibility::Visible : EVisibility::Collapsed)
					]

					// Spacer
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					[
						SNullWidget::NullWidget
					]

					// Tag picker (change tag)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(4, 0)
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
									LOCTEXT("DuplicateTagWarning", "Tag '{0}' already exists."),
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

					// Remove tag mapping button
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(SButton)
						.ButtonStyle(FAppStyle::Get(), "SimpleButton")
						.ToolTipText(LOCTEXT("RemoveTagMappingTip", "Remove this tag mapping"))
						.Visibility(bIsRequired ? EVisibility::Collapsed : EVisibility::Visible)
						.OnClicked_Lambda([this, CapturedTag]()
						{
							if (!Asset.IsValid()) return FReply::Handled();
							BeginTransaction(LOCTEXT("RemoveTagMappingTrans", "Remove Tag Mapping"));
							Asset->TagMappings.Remove(CapturedTag);
							EndTransaction();
							RefreshTagMappingsPanel();
							return FReply::Handled();
						})
						[
							SNew(STextBlock)
							.Text(FText::FromString(TEXT("\x2715")))
							.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
							.ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.3f, 0.3f)))
						]
					]
				]

				// Description (if available from settings)
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0, 2, 0, 0)
				[
					SNew(STextBlock)
					.Text(GroupDescription)
					.Font(FCoreStyle::GetDefaultFontStyle("Italic", 8))
					.ColorAndOpacity(FSlateColor(FLinearColor(0.45f, 0.45f, 0.45f)))
					.Visibility(GroupDescription.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible)
				]

				// Flipbook entries (indented)
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(8, 4, 0, 0)
				[
					FlipbookEntriesBox
				]

				// Drop hint when empty
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(8, 2, 0, 0)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("DropHint", "Drop flipbooks here or use the dropdown"))
					.Font(FCoreStyle::GetDefaultFontStyle("Italic", 8))
					.ColorAndOpacity(FSlateColor(FLinearColor(0.35f, 0.35f, 0.35f)))
					.Visibility(!bHasFlipbooks ? EVisibility::Visible : EVisibility::Collapsed)
				]
			]
		];

		// Wire up the drop handler
		DropTarget->OnDropFunc = [this, CapturedTag](const TArray<int32>& FlipbookIndices)
		{
			if (!Asset.IsValid()) return;

			FFlipbookTagMapping* Bind = Asset->TagMappings.Find(CapturedTag);
			if (!Bind) return;

			BeginTransaction(LOCTEXT("DropFlipbookOnTag", "Add Flipbook to Tag Mapping"));
			for (int32 FlipIdx : FlipbookIndices)
			{
				if (Asset->Flipbooks.IsValidIndex(FlipIdx))
				{
					const FString& Name = Asset->Flipbooks[FlipIdx].FlipbookName;
					if (!Bind->FlipbookNames.Contains(Name))
					{
						Bind->FlipbookNames.Add(Name);
					}
				}
			}
			EndTransaction();
			RefreshTagMappingsPanel();
		};

		TagMappingsListBox->AddSlot()
		.AutoHeight()
		.Padding(0, 3)
		[
			DropTarget
		];
	}
}

#undef LOCTEXT_NAMESPACE
