// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "CharacterProfileAssetEditor.h"
#include "EditorCanvasUtils.h"
#include "Paper2DPlusSettings.h"
#include "AnimSequences/PaperZDAnimSequence.h"
// SGameplayTagCombo was added in UE 5.3
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 3)
#include "SGameplayTagCombo.h"
#else
#include "SGameplayTagWidget.h"
#endif
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Framework/Application/SlateApplication.h"
#include "PaperFlipbook.h"
#include "PropertyCustomizationHelpers.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Framework/Notifications/NotificationManager.h"

/** Tag Mappings tab — Gameplay tag to flipbook array bindings with PaperZD sequence scanning and auto-creation. */

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

		// Count + hint
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0, 0, 0, 4)
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
				return FText::Format(LOCTEXT("TagMappingsCountFmt", "{0} / {1} mapped — drag flipbooks from the list to assign"),
					FText::AsNumber(WithFlipbooks), FText::AsNumber(Mapped));
			})
			.ColorAndOpacity(FSlateColor(FLinearColor(0.45f, 0.45f, 0.45f)))
			.Font(FCoreStyle::GetDefaultFontStyle("Italic", 8))
		]

		// Tag mappings list (PaperZD AnimSource picker is in Overview tab)
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
		]

		// PaperZD sequence buttons
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0, 4, 0, 0)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0, 0, 2, 0)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
				.Text(LOCTEXT("ScanSequencesBtn", "Scan for Sequences"))
				.ToolTipText(LOCTEXT("ScanSequencesTip", "Match existing PaperZD sequences to tag mapping flipbooks by name"))
				.IsEnabled_Lambda([this]() { return Asset.IsValid() && !Asset->PaperZDAnimSource.IsNull(); })
				.OnClicked_Lambda([this]() -> FReply
				{
					ScanAndMatchTagMappingSequences();
					return FReply::Handled();
				})
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(2, 0, 0, 0)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
				.Text(LOCTEXT("AutoCreateSeqBtn", "Auto-Create Sequences"))
				.ToolTipText(LOCTEXT("AutoCreateSeqTip", "Create PaperZD sequences for tag mapping flipbooks that don't have one yet"))
				.IsEnabled_Lambda([this]() { return Asset.IsValid() && !Asset->PaperZDAnimSource.IsNull(); })
				.OnClicked_Lambda([this]() -> FReply
				{
					AutoCreateTagMappingSequences();
					return FReply::Handled();
				})
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
		TagMappingFlipbookNameOptions.Add(MakeShared<FString>(Asset->Flipbooks[Idx].Identity.FlipbookName));
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

			// Resolve flipbook for thumbnail
			UPaperFlipbook* EntryFlipbook = nullptr;
			if (Asset.IsValid())
			{
				const FFlipbookProfileEntry* FBData = Asset->FindFlipbookDataPtr(Binding.FlipbookNames[FlipbookIdx]);
				if (FBData) EntryFlipbook = FBData->Identity.Flipbook.LoadSynchronous();
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

				// Flipbook thumbnail
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0, 0, 4, 0)
				[
					SNew(SBox)
					.WidthOverride(28)
					.HeightOverride(28)
					[
						SNew(SFlipbookThumbnail)
						.Flipbook(EntryFlipbook)
					]
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
								RefreshTagMappingsPanel();
							}
						}
					})
					.OnGenerateWidget_Lambda([this](TSharedPtr<FString> Item) -> TSharedRef<SWidget>
					{
						UPaperFlipbook* OptionFB = nullptr;
						if (Asset.IsValid() && Item.IsValid())
						{
							const FFlipbookProfileEntry* FBData = Asset->FindFlipbookDataPtr(*Item);
							if (FBData) OptionFB = FBData->Identity.Flipbook.LoadSynchronous();
						}

						return SNew(SHorizontalBox)

							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							.Padding(0, 0, 4, 0)
							[
								SNew(SBox)
								.WidthOverride(24)
								.HeightOverride(24)
								[
									SNew(SFlipbookThumbnail)
									.Flipbook(OptionFB)
								]
							]

							+ SHorizontalBox::Slot()
							.FillWidth(1.0f)
							.VAlign(VAlign_Center)
							[
								SNew(STextBlock).Text(FText::FromString(Item.IsValid() ? *Item : TEXT("")))
							];
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
					.ToolTipText_Lambda([CapturedFlipbookIdx]()
					{
						return CapturedFlipbookIdx > 0
							? LOCTEXT("MoveUpTooltip", "Move up in combo order")
							: LOCTEXT("MoveUpDisabledTooltip", "Already at top");
					})
					.IsEnabled_Lambda([CapturedFlipbookIdx]() { return CapturedFlipbookIdx > 0; })
					.OnClicked_Lambda([this, CapturedTag, CapturedFlipbookIdx]()
					{
						if (!Asset.IsValid()) return FReply::Handled();
						if (FFlipbookTagMapping* Bind = Asset->TagMappings.Find(CapturedTag))
						{
							if (CapturedFlipbookIdx > 0 && Bind->FlipbookNames.IsValidIndex(CapturedFlipbookIdx))
							{
								BeginTransaction(LOCTEXT("ReorderTagMappingFlipbook", "Reorder Tag Mapping"));
								// Grow PaperZDSequences to match FlipbookNames so swap never desyncs
								while (Bind->PaperZDSequences.Num() < Bind->FlipbookNames.Num())
								{
									Bind->PaperZDSequences.AddDefaulted();
								}
								Bind->FlipbookNames.Swap(CapturedFlipbookIdx, CapturedFlipbookIdx - 1);
								Bind->PaperZDSequences.Swap(CapturedFlipbookIdx, CapturedFlipbookIdx - 1);
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
					.ToolTipText_Lambda([this, CapturedTag, CapturedFlipbookIdx]()
					{
						if (!Asset.IsValid()) return LOCTEXT("MoveDownDisabledTooltip", "Already at bottom");
						if (const FFlipbookTagMapping* Bind = Asset->TagMappings.Find(CapturedTag))
						{
							if (CapturedFlipbookIdx < Bind->FlipbookNames.Num() - 1)
								return LOCTEXT("MoveDownTooltip", "Move down in combo order");
						}
						return LOCTEXT("MoveDownDisabledTooltip", "Already at bottom");
					})
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
								// Grow PaperZDSequences to match FlipbookNames so swap never desyncs
								while (Bind->PaperZDSequences.Num() < Bind->FlipbookNames.Num())
								{
									Bind->PaperZDSequences.AddDefaulted();
								}
								Bind->FlipbookNames.Swap(CapturedFlipbookIdx, CapturedFlipbookIdx + 1);
								Bind->PaperZDSequences.Swap(CapturedFlipbookIdx, CapturedFlipbookIdx + 1);
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
								// Grow PaperZDSequences to match before removing so both arrays stay synced
								while (Bind->PaperZDSequences.Num() < Bind->FlipbookNames.Num())
								{
									Bind->PaperZDSequences.AddDefaulted();
								}
								Bind->FlipbookNames.RemoveAt(CapturedFlipbookIdx);
								Bind->PaperZDSequences.RemoveAt(CapturedFlipbookIdx);
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
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
				UClass* PZDSequenceClass = FindObject<UClass>(ANY_PACKAGE, TEXT("PaperZDAnimSequence"));
#else
				UClass* PZDSequenceClass = UClass::TryFindTypeSlow<UClass>(TEXT("/Script/PaperZD.PaperZDAnimSequence"));
#endif
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
						.OnShouldFilterAsset_Lambda([this](const FAssetData& AssetData) -> bool
						{
							// Return true to EXCLUDE. If no AnimSource set, show all.
							if (!Asset.IsValid() || Asset->PaperZDAnimSource.IsNull()) return false;

							const FString DesiredSourcePath = Asset->PaperZDAnimSource.ToSoftObjectPath().ToString();
							FAssetDataTagMapSharedView::FFindTagResult TagResult = AssetData.TagsAndValues.FindTag(FName("AnimSource"));
							if (TagResult.IsSet())
							{
								// Tag value may be in export text format (e.g. "ClassName'/Game/Path'") for TObjectPtr properties
								FString TagObjectPath = FPackageName::ExportTextPathToObjectPath(TagResult.GetValue());
								return TagObjectPath != DesiredSourcePath;
							}
							return true; // Exclude if no AnimSource tag
						})
						.ObjectPath_Lambda([this, CapturedTag, CapturedFlipbookIdx]() -> FString
						{
							if (!Asset.IsValid()) return FString();
							if (const FFlipbookTagMapping* Bind = Asset->TagMappings.Find(CapturedTag))
							{
								if (Bind->PaperZDSequences.IsValidIndex(CapturedFlipbookIdx))
								{
									UPaperZDAnimSequence* Seq = Bind->PaperZDSequences[CapturedFlipbookIdx];
									return Seq ? Seq->GetPathName() : FString();
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
								// Validate index against FlipbookNames before growing
								if (CapturedFlipbookIdx >= Bind->FlipbookNames.Num())
								{
									EndTransaction();
									return;
								}
								// Grow array to match if needed
								while (Bind->PaperZDSequences.Num() <= CapturedFlipbookIdx)
								{
									Bind->PaperZDSequences.AddDefaulted();
								}
								if (AssetData.IsValid())
								{
									// static_cast avoids linking against PaperZD — asset picker is class-filtered
									Bind->PaperZDSequences[CapturedFlipbookIdx] = static_cast<UPaperZDAnimSequence*>(AssetData.GetAsset());
								}
								else
								{
									Bind->PaperZDSequences[CapturedFlipbookIdx] = nullptr;
								}
							}
							EndTransaction();
						})
					]

					// AnimSource mismatch warning
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(4, 0, 0, 0)
					[
						SNew(SImage)
						.Image(FAppStyle::GetBrush("Icons.Warning"))
						.ColorAndOpacity(FLinearColor(1.0f, 0.7f, 0.0f))
						.DesiredSizeOverride(FVector2D(14, 14))
						.ToolTipText(LOCTEXT("SequenceSourceMismatch", "This sequence belongs to a different AnimSource than the one set on this asset"))
						.Visibility_Lambda([this, CapturedTag, CapturedFlipbookIdx]() -> EVisibility
						{
							if (!Asset.IsValid() || Asset->PaperZDAnimSource.IsNull()) return EVisibility::Collapsed;

							const FFlipbookTagMapping* Bind = Asset->TagMappings.Find(CapturedTag);
							if (!Bind || !Bind->PaperZDSequences.IsValidIndex(CapturedFlipbookIdx)) return EVisibility::Collapsed;

							UPaperZDAnimSequence* Seq = Bind->PaperZDSequences[CapturedFlipbookIdx];
							if (!Seq) return EVisibility::Collapsed;

							// Get AnimSource from sequence via reflection (avoids PaperZD module dependency)
							FObjectProperty* AnimSourceProp = FindFProperty<FObjectProperty>(Seq->GetClass(), TEXT("AnimSource"));
							if (!AnimSourceProp) return EVisibility::Collapsed;

							UObject* SeqAnimSource = AnimSourceProp->GetObjectPropertyValue(AnimSourceProp->ContainerPtrToValuePtr<void>(Seq));
							if (!SeqAnimSource) return EVisibility::Collapsed;

							FSoftObjectPath SeqSourcePath(SeqAnimSource);
							return SeqSourcePath == Asset->PaperZDAnimSource.ToSoftObjectPath() ? EVisibility::Collapsed : EVisibility::Visible;
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
	#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 3)
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
#else
						// SGameplayTagCombo requires UE 5.3+; show read-only tag name on older engines
						SNew(STextBlock)
						.Text(FText::FromName(GroupTag.GetTagName()))
#endif
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
			if (!Asset.IsValid() || FlipbookIndices.Num() == 0) return;

			FFlipbookTagMapping* Bind = Asset->TagMappings.Find(CapturedTag);
			if (!Bind) return;

			BeginTransaction(LOCTEXT("DropFlipbookOnTag", "Add Flipbook to Tag Mapping"));
			for (int32 FlipIdx : FlipbookIndices)
			{
				if (Asset->Flipbooks.IsValidIndex(FlipIdx))
				{
					const FString& Name = Asset->Flipbooks[FlipIdx].Identity.FlipbookName;
					if (!Bind->FlipbookNames.Contains(Name))
					{
						Bind->FlipbookNames.Add(Name);
						while (Bind->PaperZDSequences.Num() < Bind->FlipbookNames.Num())
						{
							Bind->PaperZDSequences.AddDefaulted();
						}
						UPaperZDAnimSequence* Seq = Asset->Flipbooks[FlipIdx].Identity.PaperZDSequence;
						if (Seq)
						{
							Bind->PaperZDSequences.Last() = Seq;
						}
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

void SCharacterProfileAssetEditor::ScanAndMatchTagMappingSequences()
{
	if (!Asset.IsValid() || Asset->PaperZDAnimSource.IsNull()) return;

	BeginTransaction(LOCTEXT("ScanMatchSequences", "Scan & Match PaperZD Sequences"));

	int32 Matched = 0;
	for (auto& Pair : Asset->TagMappings)
	{
		FFlipbookTagMapping& Bind = Pair.Value;
		while (Bind.PaperZDSequences.Num() < Bind.FlipbookNames.Num())
		{
			Bind.PaperZDSequences.AddDefaulted();
		}

		for (int32 i = 0; i < Bind.FlipbookNames.Num(); i++)
		{
			if (Bind.PaperZDSequences[i]) continue;

			const FFlipbookProfileEntry* Entry = Asset->FindFlipbookDataPtr(Bind.FlipbookNames[i]);
			if (!Entry) continue;
			UPaperFlipbook* FB = Entry->Identity.Flipbook.LoadSynchronous();
			if (!FB) continue;

			UPaperZDAnimSequence* Found = Asset->FindPaperZDSequenceForFlipbook(FB);
			if (Found)
			{
				Bind.PaperZDSequences[i] = Found;
				Matched++;
			}
		}
	}

	EndTransaction();
	RefreshTagMappingsPanel();

	FNotificationInfo Notif(FText::Format(
		LOCTEXT("ScanMatchResult", "Matched {0} sequence(s) to tag mapping flipbooks."),
		FText::AsNumber(Matched)));
	Notif.bFireAndForget = true;
	Notif.ExpireDuration = 4.0f;
	FSlateNotificationManager::Get().AddNotification(Notif);
}

void SCharacterProfileAssetEditor::AutoCreateTagMappingSequences()
{
	if (!Asset.IsValid() || Asset->PaperZDAnimSource.IsNull()) return;

	UObject* AnimSource = Asset->PaperZDAnimSource.LoadSynchronous();
	if (!AnimSource) return;

#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 1)
	UClass* SeqClass = UClass::TryFindTypeSlow<UClass>(TEXT("PaperZDAnimSequence_Flipbook"));
#else
	UClass* SeqClass = FindObject<UClass>(ANY_PACKAGE, TEXT("PaperZDAnimSequence_Flipbook"));
#endif
	if (!SeqClass) return;

	const FString ProfilePath = FPackageName::GetLongPackagePath(Asset->GetPackage()->GetName());
	const FString SequenceFolderPath = ProfilePath / TEXT("Sequences");
	const FString ProfileName = Asset->GetName();

	// Collect flipbooks that need sequences
	struct FPendingSequence
	{
		FString FlipbookName;
		FString SequenceName;
		UPaperFlipbook* Flipbook;
	};
	TArray<TSharedPtr<FPendingSequence>> PendingList;

	auto CollectMissing = [&](const FString& FBName, UPaperFlipbook* FB)
	{
		if (!FB) return;
		if (Asset->FindPaperZDSequenceForFlipbook(FB)) return;
		for (auto& P : PendingList) { if (P->Flipbook == FB) return; }
		TSharedPtr<FPendingSequence> Entry = MakeShared<FPendingSequence>();
		Entry->FlipbookName = FBName;
		Entry->SequenceName = ProfileName + TEXT("_") + FB->GetName();
		Entry->Flipbook = FB;
		PendingList.Add(Entry);
	};

	for (auto& Pair : Asset->TagMappings)
	{
		for (const FString& FBName : Pair.Value.FlipbookNames)
		{
			const FFlipbookProfileEntry* E = Asset->FindFlipbookDataPtr(FBName);
			if (E) CollectMissing(FBName, E->Identity.Flipbook.LoadSynchronous());
		}
	}
	for (FFlipbookProfileEntry& Entry : Asset->Flipbooks)
	{
		if (!Entry.Identity.PaperZDSequence)
		{
			CollectMissing(Entry.Identity.FlipbookName, Entry.Identity.Flipbook.LoadSynchronous());
		}
	}

	if (PendingList.Num() == 0)
	{
		FNotificationInfo Notif(LOCTEXT("NoSeqNeeded", "All flipbooks already have PaperZD sequences."));
		Notif.bFireAndForget = true;
		Notif.ExpireDuration = 3.0f;
		FSlateNotificationManager::Get().AddNotification(Notif);
		return;
	}

	// Show confirmation dialog with editable names
	bool bConfirmed = false;
	TSharedRef<SWindow> ConfirmWindow = SNew(SWindow)
		.Title(LOCTEXT("CreateSeqTitle", "Create PaperZD Sequences"))
		.ClientSize(FVector2D(550, 400))
		.SupportsMinimize(false).SupportsMaximize(false);

	TSharedRef<bool> bIncludeProfilePrefix = MakeShared<bool>(true);
	TSharedPtr<SVerticalBox> NameListBox;

	auto RefreshNames = [&PendingList, bIncludeProfilePrefix, &ProfileName]()
	{
		for (auto& P : PendingList)
		{
			P->SequenceName = (*bIncludeProfilePrefix ? ProfileName + TEXT("_") : FString()) + P->Flipbook->GetName();
		}
	};

	ConfirmWindow->SetContent(
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(8, 8, 8, 4)
		[
			SNew(STextBlock)
			.Text(FText::Format(LOCTEXT("CreateSeqHeader", "Creating {0} PaperZD sequence(s):"), FText::AsNumber(PendingList.Num())))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(8, 2, 8, 4)
		[
			SNew(SCheckBox)
			.IsChecked(ECheckBoxState::Checked)
			.OnCheckStateChanged_Lambda([bIncludeProfilePrefix, RefreshNames, &NameListBox](ECheckBoxState S)
			{
				*bIncludeProfilePrefix = (S == ECheckBoxState::Checked);
				RefreshNames();
				if (NameListBox.IsValid()) NameListBox->Invalidate(EInvalidateWidgetReason::Paint);
			})
			[
				SNew(STextBlock)
				.Text(FText::Format(LOCTEXT("IncludeProfilePfx", "Include profile name prefix \"{0}_\""), FText::FromString(ProfileName)))
			]
		]
		+ SVerticalBox::Slot().FillHeight(1.0f).Padding(8, 0)
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			[
				SAssignNew(NameListBox, SVerticalBox)
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(8, 4)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f)
			+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
				.Text(LOCTEXT("CreateSeqConfirm", "Create"))
				.OnClicked_Lambda([&bConfirmed, ConfirmWindow]() -> FReply
				{
					bConfirmed = true;
					ConfirmWindow->RequestDestroyWindow();
					return FReply::Handled();
				})
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
				.Text(LOCTEXT("CreateSeqCancel", "Cancel"))
				.OnClicked_Lambda([ConfirmWindow]() -> FReply
				{
					ConfirmWindow->RequestDestroyWindow();
					return FReply::Handled();
				})
			]
		]
	);

	for (int32 i = 0; i < PendingList.Num(); i++)
	{
		NameListBox->AddSlot()
		.AutoHeight()
		.Padding(0, 1)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
			.Padding(FMargin(6, 3))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(0.35f).VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(FText::FromString(PendingList[i]->FlipbookName))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
					.ColorAndOpacity(FSlateColor(FLinearColor(0.45f, 0.45f, 0.45f)))
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4, 0)
				[
					SNew(STextBlock).Text(FText::FromString(TEXT("\u2192")))
						.ColorAndOpacity(FSlateColor(FLinearColor(0.35f, 0.35f, 0.35f)))
				]
				+ SHorizontalBox::Slot().FillWidth(0.65f).VAlign(VAlign_Center).Padding(4, 0)
				[
					SNew(SEditableTextBox)
					.Text_Lambda([Pending = PendingList[i]]() { return FText::FromString(Pending->SequenceName); })
					.OnTextCommitted_Lambda([Pending = PendingList[i]](const FText& T, ETextCommit::Type)
					{
						Pending->SequenceName = T.ToString();
					})
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
				]
			]
		];
	}

	FSlateApplication::Get().AddModalWindow(ConfirmWindow, AsShared());

	if (!bConfirmed || PendingList.Num() == 0) return;

	// Now create the sequences
	BeginTransaction(LOCTEXT("AutoCreateSequences", "Auto-Create PaperZD Sequences"));

	int32 Created = 0;

	auto CreateSequenceForFlipbook = [&](UPaperFlipbook* FB, const FString& SequenceName) -> UPaperZDAnimSequence*
	{
		if (!FB) return nullptr;

		const FString PackagePath = SequenceFolderPath / SequenceName;
		UPackage* Package = CreatePackage(*PackagePath);
		if (!Package) return nullptr;

		UObject* ExistingObj = StaticFindObject(UObject::StaticClass(), Package, *SequenceName);
		if (ExistingObj)
		{
			if (ExistingObj->GetClass()->IsChildOf(SeqClass))
			{
				return static_cast<UPaperZDAnimSequence*>(ExistingObj);
			}
			UE_LOG(LogTemp, Warning, TEXT("Paper2DPlus: Cannot create sequence '%s' — asset already exists with type %s"), *SequenceName, *ExistingObj->GetClass()->GetName());
			return nullptr;
		}

		UObject* NewSeq = NewObject<UObject>(Package, SeqClass, *SequenceName, RF_Public | RF_Standalone);
		if (!NewSeq) return nullptr;

		FObjectProperty* SourceProp = FindFProperty<FObjectProperty>(SeqClass, TEXT("AnimSource"));
		if (SourceProp)
		{
			SourceProp->SetObjectPropertyValue(SourceProp->ContainerPtrToValuePtr<void>(NewSeq), AnimSource);
		}

		FArrayProperty* AnimDataProp = FindFProperty<FArrayProperty>(SeqClass, TEXT("AnimData"));
		if (AnimDataProp)
		{
			FScriptArrayHelper ArrayHelper(AnimDataProp, AnimDataProp->ContainerPtrToValuePtr<void>(NewSeq));
			ArrayHelper.AddValue();
			FStructProperty* InnerStruct = CastField<FStructProperty>(AnimDataProp->Inner);
			if (InnerStruct)
			{
				FObjectProperty* AnimProp = FindFProperty<FObjectProperty>(InnerStruct->Struct, TEXT("Animation"));
				if (AnimProp)
				{
					AnimProp->SetObjectPropertyValue(AnimProp->ContainerPtrToValuePtr<void>(ArrayHelper.GetRawPtr(0)), FB);
				}
			}
		}

		Package->MarkPackageDirty();
		FAssetRegistryModule::AssetCreated(NewSeq);
		Created++;
		return static_cast<UPaperZDAnimSequence*>(NewSeq);
	};

	// Build a map from flipbook pointer to created sequence for quick lookup
	TMap<UPaperFlipbook*, UPaperZDAnimSequence*> CreatedMap;
	for (auto& P : PendingList)
	{
		UPaperZDAnimSequence* Seq = CreateSequenceForFlipbook(P->Flipbook, P->SequenceName);
		if (Seq) CreatedMap.Add(P->Flipbook, Seq);
	}

	// Assign to tag mappings
	for (auto& Pair : Asset->TagMappings)
	{
		FFlipbookTagMapping& Bind = Pair.Value;
		while (Bind.PaperZDSequences.Num() < Bind.FlipbookNames.Num())
		{
			Bind.PaperZDSequences.AddDefaulted();
		}

		for (int32 i = 0; i < Bind.FlipbookNames.Num(); i++)
		{
			if (Bind.PaperZDSequences[i]) continue;
			const FFlipbookProfileEntry* Entry = Asset->FindFlipbookDataPtr(Bind.FlipbookNames[i]);
			if (!Entry) continue;
			UPaperFlipbook* FB = Entry->Identity.Flipbook.LoadSynchronous();
			if (UPaperZDAnimSequence** Found = CreatedMap.Find(FB))
			{
				Bind.PaperZDSequences[i] = *Found;
			}
		}
	}

	// Also create for flipbook entries that don't have a sequence on Identity
	for (FFlipbookProfileEntry& Entry : Asset->Flipbooks)
	{
		if (Entry.Identity.PaperZDSequence) continue;
		UPaperFlipbook* FB = Entry.Identity.Flipbook.LoadSynchronous();
		if (UPaperZDAnimSequence** Found = CreatedMap.Find(FB))
		{
			Entry.Identity.PaperZDSequence = *Found;
		}
	}

	EndTransaction();
	RefreshTagMappingsPanel();

	FNotificationInfo Notif(FText::Format(
		LOCTEXT("AutoCreateResult", "Created {0} PaperZD sequence(s)."),
		FText::AsNumber(Created)));
	Notif.bFireAndForget = true;
	Notif.ExpireDuration = 4.0f;
	FSlateNotificationManager::Get().AddNotification(Notif);
}

#undef LOCTEXT_NAMESPACE
