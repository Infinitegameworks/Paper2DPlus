// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "CharacterProfileAssetEditor.h"
#include "BulkSpriteExtractorWindow.h"
#include "EditorCanvasUtils.h"
#include "Paper2DPlusSettings.h"
#include "AnimSequences/PaperZDAnimSequence.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Colors/SColorPicker.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "DragAndDrop/AssetDragDropOp.h"
#include "ISinglePropertyView.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Colors/SColorBlock.h"
#include "PaperFlipbook.h"
#include "PaperSprite.h"
#include "PropertyCustomizationHelpers.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Framework/Notifications/NotificationManager.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "AssetRegistry/ARFilter.h"

/** Overview tab — Flipbook card grid, PaperZD source panel, re-extract button, display name, and filter controls. */

#define LOCTEXT_NAMESPACE "CharacterProfileAssetEditor"

// Drop zone that accepts UPaperFlipbook assets dragged from the content browser.
class SOverviewFlipbookDropZone : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SOverviewFlipbookDropZone) {}
		SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_END_ARGS()

	TFunction<void(const TArray<FAssetData>&)> OnDropFlipbooks;

	void Construct(const FArguments& InArgs)
	{
		ChildSlot[ InArgs._Content.Widget ];
	}

	virtual void OnDragEnter(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override
	{
		if (HasValidFlipbooks(DragDropEvent))
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
		return HasValidFlipbooks(DragDropEvent) ? FReply::Handled() : FReply::Unhandled();
	}

	virtual FReply OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override
	{
		bDragOver = false;
		Invalidate(EInvalidateWidgetReason::Paint);

		TSharedPtr<FAssetDragDropOp> AssetOp = DragDropEvent.GetOperationAs<FAssetDragDropOp>();
		if (!AssetOp.IsValid() || !OnDropFlipbooks) return FReply::Unhandled();

		TArray<FAssetData> Flipbooks;
		for (const FAssetData& AD : AssetOp->GetAssets())
		{
			if (AD.GetClass() && AD.GetClass()->IsChildOf(UPaperFlipbook::StaticClass()))
			{
				Flipbooks.Add(AD);
			}
		}

		if (Flipbooks.Num() > 0)
		{
			OnDropFlipbooks(Flipbooks);
			return FReply::Handled();
		}
		return FReply::Unhandled();
	}

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override
	{
		if (bDragOver)
		{
			const FVector2D Size = AllottedGeometry.GetLocalSize();
			const float Thickness = 2.0f;
			const FLinearColor HighlightColor(0.3f, 0.5f, 0.8f, 0.6f);
			const FSlateBrush* WhiteBrush = FAppStyle::GetBrush("WhiteBrush");

			FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 1, MakePaintGeometry(AllottedGeometry, FVector2D(Size.X, Thickness), FSlateLayoutTransform()), WhiteBrush, ESlateDrawEffect::None, HighlightColor);
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 1, MakePaintGeometry(AllottedGeometry, FVector2D(Size.X, Thickness), FSlateLayoutTransform(FVector2D(0, Size.Y - Thickness))), WhiteBrush, ESlateDrawEffect::None, HighlightColor);
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 1, MakePaintGeometry(AllottedGeometry, FVector2D(Thickness, Size.Y), FSlateLayoutTransform()), WhiteBrush, ESlateDrawEffect::None, HighlightColor);
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 1, MakePaintGeometry(AllottedGeometry, FVector2D(Thickness, Size.Y), FSlateLayoutTransform(FVector2D(Size.X - Thickness, 0))), WhiteBrush, ESlateDrawEffect::None, HighlightColor);
		}
		return SCompoundWidget::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	}

private:
	bool bDragOver = false;

	static bool HasValidFlipbooks(const FDragDropEvent& DragDropEvent)
	{
		TSharedPtr<FAssetDragDropOp> AssetOp = DragDropEvent.GetOperationAs<FAssetDragDropOp>();
		if (!AssetOp.IsValid()) return false;
		for (const FAssetData& AD : AssetOp->GetAssets())
		{
			if (AD.GetClass() && AD.GetClass()->IsChildOf(UPaperFlipbook::StaticClass()))
				return true;
		}
		return false;
	}
};

TSharedRef<SWidget> SCharacterProfileAssetEditor::CreateRelativeTransformPropertyView(
	FName PropertyName, TSharedPtr<ISinglePropertyView>& OutView)
{
	FPropertyEditorModule& PropModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	FSinglePropertyParams Params;
	Params.NamePlacement = EPropertyNamePlacement::Left;
	OutView = PropModule.CreateSingleProperty(Asset.Get(), PropertyName, Params);
	if (OutView.IsValid())
	{
		return OutView.ToSharedRef();
	}
	return SNullWidget::NullWidget;
}

TSharedRef<SWidget> SCharacterProfileAssetEditor::BuildOverviewTab()
{
	TSharedRef<SWidget> OverviewWidget = SNew(SSplitter)
		.Orientation(Orient_Horizontal)

		// Left: Main content
		+ SSplitter::Slot()
		.Value(0.75f)
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
					.ToolTipText(LOCTEXT("DisplayNameTooltip", "A friendly name for this character profile asset. Used for display purposes in the editor and can be used in runtime UI."))

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
						.OnTextCommitted_Lambda([this](const FText& Text, ETextCommit::Type CommitType) {
							if (CommitType == ETextCommit::OnCleared) return;
							if (!Asset.IsValid()) return;
							BeginTransaction(LOCTEXT("SetDisplayName", "Set Display Name"));
							Asset->DisplayName = Text.ToString();
							EndTransaction();
						})
					]
				]
			]

			// Flipbooks Section (PRIMARY - Flipbook Dashboard)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 0, 0, 8)
			[
				SAssignNew(OverviewFlipbookDropZone, SOverviewFlipbookDropZone)
				[
				WrapWithActivePanelHighlight(FName(TEXT("Overview.Flipbooks")), 8,
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
						.VAlign(VAlign_Center)
						.Padding(8, 0, 0, 0)
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
				)
				]
			]

				]
		]

		// Right: Dimensions sidebar
		+ SSplitter::Slot()
		.Value(0.25f)
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			.Padding(8)
			[
				SNew(SVerticalBox)

				// Card Overview (collapsible)
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0, 0, 0, 4)
				[
					SNew(SExpandableArea)
					.InitiallyCollapsed(false)
					.HeaderPadding(FMargin(4, 2))
					.HeaderContent()
					[
						SNew(STextBlock)
						.Text_Lambda([this]()
						{
							return SelectedPhaseGroupName.IsNone()
								? LOCTEXT("DetailsSection", "Details")
								: FText::Format(LOCTEXT("DetailsSectionPhaseGroup", "Details  ·  {0}"),
									FText::FromName(SelectedPhaseGroupName));
						})
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
					]
					.BodyContent()
					[
						BuildDetailsPanel()
					]
				]

				// PaperZD Anim Source (collapsible — only visible when PaperZD is loaded)
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0, 0, 0, 4)
				[
					SNew(SExpandableArea)
					.Visibility_Lambda([]()
					{
						return FModuleManager::Get().IsModuleLoaded(TEXT("PaperZD")) ? EVisibility::Visible : EVisibility::Collapsed;
					})
					.InitiallyCollapsed(true)
					.HeaderPadding(FMargin(4, 2))
					.HeaderContent()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("PZDAnimSourceSection", "PaperZD Anim Source"))
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
						]
						+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center).Padding(8, 0, 0, 0)
						[
							SNew(STextBlock)
							.Text_Lambda([this]() -> FText
							{
								if (!Asset.IsValid()) return FText::GetEmpty();
								int32 Matched = 0;
								const int32 Total = Asset->Flipbooks.Num();
								for (const FFlipbookProfileEntry& FB : Asset->Flipbooks)
								{
									if (FB.Identity.PaperZDSequence) Matched++;
								}
								return FText::FromString(FString::Printf(TEXT("%d / %d matched"), Matched, Total));
							})
							.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
							.ColorAndOpacity(FSlateColor(FLinearColor(0.55f, 0.55f, 0.55f)))
						]
					]
					.BodyContent()
					[
						BuildPaperZDAnimSourcePanel()
					]
				]

				// Tag Mappings (collapsible)
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0, 0, 0, 4)
				[
					SNew(SExpandableArea)
					.InitiallyCollapsed(false)
					.HeaderPadding(FMargin(4, 2))
					.HeaderContent()
					[
						SNew(STextBlock)
						.Text(LOCTEXT("TagMappingsSection", "Tag Mappings"))
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
					]
					.BodyContent()
					[
						WrapWithActivePanelHighlight(FName(TEXT("Overview.TagMappings")), 4, BuildTagMappingsPanel())
					]
				]

				// Relative Transform
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0, 0, 0, 4)
				[
					SNew(SExpandableArea)
					.InitiallyCollapsed(true)
					.HeaderPadding(FMargin(4, 2))
					.HeaderContent()
					[
						SNew(STextBlock)
						.Text(LOCTEXT("RelativeTransformSection", "Relative Transform"))
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
					]
					.BodyContent()
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight().Padding(4, 4, 4, 0)
						[
							CreateRelativeTransformPropertyView(GET_MEMBER_NAME_CHECKED(UPaper2DPlusCharacterProfileAsset, RelativeLocation), RelativeLocationView)
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(4, 0)
						[
							CreateRelativeTransformPropertyView(GET_MEMBER_NAME_CHECKED(UPaper2DPlusCharacterProfileAsset, RelativeRotation), RelativeRotationView)
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(4, 0, 4, 4)
						[
							CreateRelativeTransformPropertyView(GET_MEMBER_NAME_CHECKED(UPaper2DPlusCharacterProfileAsset, RelativeScale3D), RelativeScale3DView)
						]
					]
				]
			]
		];

	if (OverviewFlipbookDropZone.IsValid())
	{
		OverviewFlipbookDropZone->OnDropFlipbooks = [this](const TArray<FAssetData>& Assets)
		{
			OnContentBrowserFlipbooksDrop(Assets);
		};
	}

	return OverviewWidget;
}

void SCharacterProfileAssetEditor::OnContentBrowserFlipbooksDrop(const TArray<FAssetData>& DroppedAssets)
{
	if (!Asset.IsValid() || DroppedAssets.Num() == 0) return;

	BeginTransaction(LOCTEXT("DropFlipbooks", "Add Flipbooks from Content Browser"));

	int32 LastIndex = INDEX_NONE;
	for (const FAssetData& AssetData : DroppedAssets)
	{
		FFlipbookProfileEntry NewEntry;
		NewEntry.Identity.Flipbook = TSoftObjectPtr<UPaperFlipbook>(AssetData.ToSoftObjectPath());
		NewEntry.Identity.FlipbookName = AssetData.AssetName.ToString();

		FFrameHitboxData DefaultFrame;
		DefaultFrame.FrameName = TEXT("Frame_0");
		NewEntry.CombatData.Frames.Add(DefaultFrame);

		LastIndex = Asset->Flipbooks.Add(NewEntry);
		Asset->SyncFramesToFlipbook(LastIndex);
	}

	EndTransaction();

	if (LastIndex != INDEX_NONE)
	{
		SelectedFlipbookIndex = LastIndex;
		SelectedFrameIndex = 0;
		SelectedFlipbookCards.Empty();
		SelectedFlipbookCards.Add(LastIndex);
		SelectionAnchorIndex = LastIndex;
	}

	RefreshOverviewFlipbookList();
	RefreshFlipbookList();
	RefreshSpriteEditorFlipbookList();
	RefreshFrameList();
	RefreshHitboxList();
	RefreshPropertiesPanel();
}

TSharedRef<SWidget> SCharacterProfileAssetEditor::BuildCompletionFilterButton(TFunction<void()> OnChanged)
{
	return SNew(SComboButton)
		.ButtonStyle(FAppStyle::Get(), "SimpleButton")
		.HasDownArrow(false)
		.ContentPadding(FMargin(2, 0))
		.ToolTipText_Lambda([this]()
		{
			return CompletionFilterMask != 0
				? LOCTEXT("FilterActiveTip", "Filters active (click to modify)")
				: LOCTEXT("FilterTip", "Filter by completion status");
		})
		.ButtonContent()
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SImage)
				.Image(FAppStyle::GetBrush("Icons.Filter"))
				.DesiredSizeOverride(FVector2D(14, 14))
				.ColorAndOpacity_Lambda([this]()
				{
					return CompletionFilterMask != 0
						? FSlateColor(FLinearColor(0.4f, 0.7f, 1.0f))
						: FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f));
				})
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(2, 0, 0, 0)
			[
				SNew(STextBlock)
				.Text_Lambda([this]()
				{
					if (CompletionFilterMask == 0) return FText::GetEmpty();
					int32 Count = 0;
					int32 Mask = CompletionFilterMask;
					while (Mask) { Count += (Mask & 1); Mask >>= 1; }
					return FText::AsNumber(Count);
				})
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.4f, 0.7f, 1.0f)))
			]
		]
		.MenuContent()
		[
			SNew(SVerticalBox)

			// Status filters
			+ SVerticalBox::Slot().AutoHeight().Padding(8, 6, 8, 2)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("StatusFiltersLabel", "STATUS"))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(8, 2)
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([this]() { return (CompletionFilterMask & (1 << 8)) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this, OnChanged](ECheckBoxState) { CompletionFilterMask ^= (1 << 8); OnChanged(); })
				[ SNew(STextBlock).Text(LOCTEXT("FiltIncomplete", "Incomplete")).Font(FCoreStyle::GetDefaultFontStyle("Regular", 9)) ]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(8, 2)
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([this]() { return (CompletionFilterMask & (1 << 7)) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this, OnChanged](ECheckBoxState) { CompletionFilterMask ^= (1 << 7); OnChanged(); })
				[ SNew(STextBlock).Text(LOCTEXT("FiltComplete", "Complete")).Font(FCoreStyle::GetDefaultFontStyle("Regular", 9)) ]
			]

			// Separator
			+ SVerticalBox::Slot().AutoHeight().Padding(8, 4)
			[ SNew(SSeparator) ]

			// Per-task filters
			+ SVerticalBox::Slot().AutoHeight().Padding(8, 2, 8, 2)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("NeedsWorkLabel", "NEEDS WORK"))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(8, 2)
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([this]() { return (CompletionFilterMask & (1 << 0)) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this, OnChanged](ECheckBoxState) { CompletionFilterMask ^= (1 << 0); OnChanged(); })
				[ SNew(STextBlock).Text(LOCTEXT("FiltHitboxes", "Hitboxes")).Font(FCoreStyle::GetDefaultFontStyle("Regular", 9)) ]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(8, 2)
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([this]() { return (CompletionFilterMask & (1 << 1)) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this, OnChanged](ECheckBoxState) { CompletionFilterMask ^= (1 << 1); OnChanged(); })
				[ SNew(STextBlock).Text(LOCTEXT("FiltAlignment", "Sprite Alignment")).Font(FCoreStyle::GetDefaultFontStyle("Regular", 9)) ]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(8, 2)
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([this]() { return (CompletionFilterMask & (1 << 2)) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this, OnChanged](ECheckBoxState) { CompletionFilterMask ^= (1 << 2); OnChanged(); })
				[ SNew(STextBlock).Text(LOCTEXT("FiltTiming", "Frame Timing")).Font(FCoreStyle::GetDefaultFontStyle("Regular", 9)) ]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(8, 2)
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([this]() { return (CompletionFilterMask & (1 << 3)) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this, OnChanged](ECheckBoxState) { CompletionFilterMask ^= (1 << 3); OnChanged(); })
				[ SNew(STextBlock).Text(LOCTEXT("FiltPhases", "Phases")).Font(FCoreStyle::GetDefaultFontStyle("Regular", 9)) ]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(8, 2)
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([this]() { return (CompletionFilterMask & (1 << 4)) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this, OnChanged](ECheckBoxState) { CompletionFilterMask ^= (1 << 4); OnChanged(); })
				[ SNew(STextBlock).Text(LOCTEXT("FiltEffects", "Effects")).Font(FCoreStyle::GetDefaultFontStyle("Regular", 9)) ]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(8, 2)
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([this]() { return (CompletionFilterMask & (1 << 5)) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this, OnChanged](ECheckBoxState) { CompletionFilterMask ^= (1 << 5); OnChanged(); })
				[ SNew(STextBlock).Text(LOCTEXT("FiltRootMotion", "Root Motion")).Font(FCoreStyle::GetDefaultFontStyle("Regular", 9)) ]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(8, 2, 8, 4)
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([this]() { return (CompletionFilterMask & (1 << 6)) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this, OnChanged](ECheckBoxState) { CompletionFilterMask ^= (1 << 6); OnChanged(); })
				[ SNew(STextBlock).Text(LOCTEXT("FiltTags", "Tag Mappings")).Font(FCoreStyle::GetDefaultFontStyle("Regular", 9)) ]
			]

			// Clear all
			+ SVerticalBox::Slot().AutoHeight().Padding(8, 2, 8, 6)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
				.OnClicked_Lambda([this, OnChanged]()
				{
					CompletionFilterMask = 0;
					OnChanged();
					return FReply::Handled();
				})
				.IsEnabled_Lambda([this]() { return CompletionFilterMask != 0; })
				[ SNew(STextBlock).Text(LOCTEXT("ClearFilters", "Clear All Filters")).Font(FCoreStyle::GetDefaultFontStyle("Regular", 9)) ]
			]
		];
}

TSharedRef<SWidget> SCharacterProfileAssetEditor::BuildPaperZDAnimSourcePanel()
{
	// AnimSource picker + dropdown listing all sequences with matched/unmatched indication
	TSharedRef<SWidget> PanelRoot = SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
		.Padding(FMargin(8, 6))
		[
			SNew(SVerticalBox)

			// Source picker row
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 0, 0, 6)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0, 0, 6, 0)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("PZDSourceLabel2", "Source"))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
					.ColorAndOpacity(FSlateColor(FLinearColor(0.55f, 0.55f, 0.6f)))
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					SNew(SObjectPropertyEntryBox)
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
					.AllowedClass(FindObject<UClass>(ANY_PACKAGE, TEXT("PaperZDAnimationSource")) ? FindObject<UClass>(ANY_PACKAGE, TEXT("PaperZDAnimationSource")) : UObject::StaticClass())
#else
					.AllowedClass(UClass::TryFindTypeSlow<UClass>(TEXT("PaperZDAnimationSource")) ? UClass::TryFindTypeSlow<UClass>(TEXT("PaperZDAnimationSource")) : UObject::StaticClass())
#endif
					.AllowClear(true)
					.ObjectPath_Lambda([this]() -> FString
					{
						if (!Asset.IsValid() || Asset->PaperZDAnimSource.IsNull()) return FString();
						return Asset->PaperZDAnimSource.ToSoftObjectPath().ToString();
					})
					.OnObjectChanged_Lambda([this](const FAssetData& AssetData)
					{
						if (!Asset.IsValid()) return;
						BeginTransaction(LOCTEXT("SetAnimSource3", "Set PaperZD Anim Source"));
						if (AssetData.IsValid())
						{
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
							Asset->PaperZDAnimSource = TSoftObjectPtr<UObject>(AssetData.ToSoftObjectPath());
#else
							Asset->PaperZDAnimSource = TSoftObjectPtr<UObject>(AssetData.GetSoftObjectPath());
#endif
							Asset->AutoPopulatePaperZDSequences();
						}
						else
						{
							Asset->PaperZDAnimSource = nullptr;
						}
						EndTransaction();
						RefreshOverviewFlipbookList();
						RefreshPaperZDSequencesList();
					})
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(6, 0, 0, 0)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
					.ToolTipText(LOCTEXT("RescanPZDTip", "Re-scan flipbooks for matching PaperZD sequences"))
					.OnClicked_Lambda([this]()
					{
						if (!Asset.IsValid()) return FReply::Handled();
						BeginTransaction(LOCTEXT("RescanPZDSeqs2", "Re-scan PaperZD Sequences"));
						Asset->AutoPopulatePaperZDSequences();
						EndTransaction();
						RefreshPaperZDSequencesList();
						Invalidate(EInvalidateWidgetReason::Paint);
						return FReply::Handled();
					})
					[
						SNew(STextBlock)
						.Text(LOCTEXT("RescanBtn2", "Re-scan"))
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
					]
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(4, 0, 0, 0)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
					.ToolTipText(LOCTEXT("CreatePZDTip", "Create PaperZD sequences for all flipbooks that don't have one"))
					.IsEnabled_Lambda([this]() { return Asset.IsValid() && !Asset->PaperZDAnimSource.IsNull(); })
					.OnClicked_Lambda([this]()
					{
						AutoCreateTagMappingSequences();
						RefreshPaperZDSequencesList();
						RefreshOverviewFlipbookList();
						Invalidate(EInvalidateWidgetReason::Paint);
						return FReply::Handled();
					})
					[
						SNew(STextBlock)
						.Text(LOCTEXT("CreatePZDBtn", "Create"))
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
					]
				]
			]

			// Sequences dropdown header
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 4, 0, 2)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("PZDSequencesLabel", "Sequences in Source"))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.55f, 0.55f, 0.6f)))
				.Visibility_Lambda([this]()
				{
					return (Asset.IsValid() && !Asset->PaperZDAnimSource.IsNull()) ? EVisibility::Visible : EVisibility::Collapsed;
				})
			]

			// Scrollable list of all sequences — matched shown in white with flipbook name, unmatched in grey italic
			+ SVerticalBox::Slot()
			.AutoHeight()
			.MaxHeight(180.0f)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
				.Padding(FMargin(4))
				.Visibility_Lambda([this]()
				{
					return (Asset.IsValid() && !Asset->PaperZDAnimSource.IsNull()) ? EVisibility::Visible : EVisibility::Collapsed;
				})
				[
					SNew(SScrollBox)
					+ SScrollBox::Slot()
					[
						SAssignNew(PaperZDSequencesListBox, SVerticalBox)
					]
				]
			]
		];

	// Populate the list immediately
	RefreshPaperZDSequencesList();

	return PanelRoot;
}

void SCharacterProfileAssetEditor::RefreshPaperZDSequencesList()
{
	if (!PaperZDSequencesListBox.IsValid()) return;
	PaperZDSequencesListBox->ClearChildren();

	if (!Asset.IsValid() || Asset->PaperZDAnimSource.IsNull()) return;

	// Query asset registry for all PaperZDAnimSequence_Flipbook assets under this AnimSource
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
	UClass* SeqClass = FindObject<UClass>(ANY_PACKAGE, TEXT("PaperZDAnimSequence_Flipbook"));
#else
	UClass* SeqClass = UClass::TryFindTypeSlow<UClass>(TEXT("PaperZDAnimSequence_Flipbook"));
#endif
	if (!SeqClass) return;

	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	FARFilter Filter;
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
	Filter.ClassNames.Add(SeqClass->GetFName());
#else
	Filter.ClassPaths.Add(SeqClass->GetClassPathName());
#endif
	Filter.bRecursiveClasses = true;

	TArray<FAssetData> SequenceAssets;
	AssetRegistry.GetAssets(Filter, SequenceAssets);

	const FString DesiredSourcePath = Asset->PaperZDAnimSource.ToSoftObjectPath().ToString();

	// Build a set of sequences currently matched by any flipbook
	TSet<UPaperZDAnimSequence*> MatchedSequences;
	for (const FFlipbookProfileEntry& FB : Asset->Flipbooks)
	{
		if (FB.Identity.PaperZDSequence) MatchedSequences.Add(FB.Identity.PaperZDSequence);
	}

	// Collect sequences that belong to our AnimSource
	struct FSequenceRow
	{
		FString Name;
		bool bMatched;
		FString MatchedFlipbookName;
	};
	TArray<FSequenceRow> Rows;

	for (const FAssetData& AssetData : SequenceAssets)
	{
		FAssetDataTagMapSharedView::FFindTagResult TagResult = AssetData.TagsAndValues.FindTag(FName("AnimSource"));
		if (!TagResult.IsSet()) continue;

		FString TagObjectPath = FPackageName::ExportTextPathToObjectPath(TagResult.GetValue());
		if (TagObjectPath != DesiredSourcePath) continue;

		FSequenceRow Row;
		Row.Name = AssetData.AssetName.ToString();
		Row.bMatched = false;

		// Check if any of our flipbooks have this sequence
		// static_cast avoids linking against PaperZD — asset registry query is class-filtered
		UPaperZDAnimSequence* LoadedSeq = static_cast<UPaperZDAnimSequence*>(AssetData.GetAsset());
		if (LoadedSeq && MatchedSequences.Contains(LoadedSeq))
		{
			Row.bMatched = true;
			// Find which flipbook matches
			for (const FFlipbookProfileEntry& FB : Asset->Flipbooks)
			{
				if (FB.Identity.PaperZDSequence == LoadedSeq)
				{
					Row.MatchedFlipbookName = FB.Identity.FlipbookName;
					break;
				}
			}
		}
		Rows.Add(Row);
	}

	// Sort: matched first, then alphabetical by name
	Rows.Sort([](const FSequenceRow& A, const FSequenceRow& B)
	{
		if (A.bMatched != B.bMatched) return A.bMatched;
		return A.Name < B.Name;
	});

	for (const FSequenceRow& Row : Rows)
	{
		FLinearColor DotColor = Row.bMatched ? FLinearColor(0.3f, 0.8f, 0.3f) : FLinearColor(0.5f, 0.5f, 0.5f);
		FLinearColor TextColor = Row.bMatched ? FLinearColor(0.9f, 0.9f, 0.9f) : FLinearColor(0.55f, 0.55f, 0.55f);

		PaperZDSequencesListBox->AddSlot()
		.AutoHeight()
		.Padding(0, 1)
		[
			SNew(SHorizontalBox)
			// Status dot
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 6, 0)
			[
				SNew(SBox).WidthOverride(8).HeightOverride(8)
				[
					SNew(SImage)
					.Image(FAppStyle::GetBrush("Icons.FilledCircle"))
					.ColorAndOpacity(DotColor)
				]
			]
			// Sequence name
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(FText::FromString(Row.Name))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
				.ColorAndOpacity(FSlateColor(TextColor))
			]
			// Matched flipbook name (if any)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6, 0, 0, 0)
			[
				SNew(STextBlock)
				.Text(Row.bMatched ? FText::Format(NSLOCTEXT("PZD", "MatchedTo", "\x2192 {0}"), FText::FromString(Row.MatchedFlipbookName)) : NSLOCTEXT("PZD", "Unmatched", "(unmatched)"))
				.Font(FCoreStyle::GetDefaultFontStyle("Italic", 8))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)))
			]
		];
	}
}

TSharedRef<SWidget> SCharacterProfileAssetEditor::BuildDetailsPanel()
{
	// Dispatch between the flipbook details view and the phase group details view.
	// The switcher's active index reads SelectedPhaseGroupName each paint, so a
	// simple Invalidate(Paint) on the overview tab updates the view when the
	// user clicks a phase group header or a flipbook card. No ClearChildren
	// rebuilds — both views are constructed once and share the same splitter.
	return SNew(SWidgetSwitcher)
		.WidgetIndex_Lambda([this]() { return SelectedPhaseGroupName.IsNone() ? 0 : 1; })
		+ SWidgetSwitcher::Slot() [ BuildFlipbookDetailsView() ]
		+ SWidgetSwitcher::Slot() [ BuildPhaseGroupDetailsView() ];
}

TSharedRef<SWidget> SCharacterProfileAssetEditor::BuildFlipbookDetailsView()
{
	// Helper: make a labeled info row (dim label, bright value)
	auto MakeInfoRow = [this](const FText& Label, TFunction<FText()> ValueFunc, TFunction<EVisibility()> VisFunc = nullptr) -> TSharedRef<SWidget>
	{
		TSharedRef<STextBlock> ValueText = SNew(STextBlock)
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
			.ColorAndOpacity(FSlateColor(FLinearColor(0.75f, 0.75f, 0.78f)))
			.AutoWrapText(true);
		ValueText->SetText(MakeAttributeLambda(MoveTemp(ValueFunc)));

		TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox)
			.Visibility_Lambda([this, VisFunc]()
			{
				if (!(Asset.IsValid() && Asset->Flipbooks.IsValidIndex(SelectedFlipbookIndex))) return EVisibility::Collapsed;
				return VisFunc ? VisFunc() : EVisibility::Visible;
			});
		Row->AddSlot().AutoWidth().VAlign(VAlign_Top).Padding(0, 0, 6, 0)
			[ SNew(STextBlock).Text(Label).Font(FCoreStyle::GetDefaultFontStyle("Regular", 8)).ColorAndOpacity(FSlateColor(FLinearColor(0.40f, 0.40f, 0.45f))) ];
		Row->AddSlot().FillWidth(1.0f).VAlign(VAlign_Top)
			[ ValueText ];
		return Row;
	};

	// Helper: make a completion checkbox for the 2-column grid
	auto MakeCheckbox = [this](const FText& Label, int32 Bit) -> TSharedRef<SWidget>
	{
		return SNew(SCheckBox)
			.IsChecked_Lambda([this, Bit]() { return (Asset.IsValid() && Asset->Flipbooks.IsValidIndex(SelectedFlipbookIndex) && (Asset->Flipbooks[SelectedFlipbookIndex].EditorMeta.CompletionFlags & (1 << Bit))) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
			.OnCheckStateChanged_Lambda([this, Bit](ECheckBoxState)
			{
				if (!Asset.IsValid() || !Asset->Flipbooks.IsValidIndex(SelectedFlipbookIndex)) return;
				BeginTransaction(LOCTEXT("ToggleCompletionFlag", "Toggle Completion Flag"));
				Asset->Flipbooks[SelectedFlipbookIndex].EditorMeta.CompletionFlags ^= (1 << Bit);
				EndTransaction();
				// Lambdas on checkboxes/progress/count text update reactively.
				// Only the groups panel's card-level completion indicators need a paint nudge.
				if (FlipbookGroupsListBox.IsValid()) FlipbookGroupsListBox->Invalidate(EInvalidateWidgetReason::Paint);
			})
			.Padding(FMargin(0, 1))
			[ SNew(STextBlock).Text(Label).Font(FCoreStyle::GetDefaultFontStyle("Regular", 8)) ];
	};

	return SNew(SVerticalBox)

		// Placeholder
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("SelectFlipbookHint", "Select a flipbook to see details"))
			.ColorAndOpacity(FSlateColor(FLinearColor(0.4f, 0.4f, 0.4f)))
			.Font(FCoreStyle::GetDefaultFontStyle("Italic", 8))
			.Visibility_Lambda([this]() { return (Asset.IsValid() && Asset->Flipbooks.IsValidIndex(SelectedFlipbookIndex)) ? EVisibility::Collapsed : EVisibility::Visible; })
		]

		// Flipbook name
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 4)
		[
			SNew(STextBlock)
			.Text_Lambda([this]() { return (Asset.IsValid() && Asset->Flipbooks.IsValidIndex(SelectedFlipbookIndex)) ? FText::FromString(Asset->Flipbooks[SelectedFlipbookIndex].Identity.FlipbookName) : FText::GetEmpty(); })
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
			.Visibility_Lambda([this]() { return (Asset.IsValid() && Asset->Flipbooks.IsValidIndex(SelectedFlipbookIndex)) ? EVisibility::Visible : EVisibility::Collapsed; })
		]

		// --- Timing section ---
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 2, 0, 0)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
			.Padding(FMargin(6, 4))
			.Visibility_Lambda([this]() { return (Asset.IsValid() && Asset->Flipbooks.IsValidIndex(SelectedFlipbookIndex)) ? EVisibility::Visible : EVisibility::Collapsed; })
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					MakeInfoRow(LOCTEXT("LblFrames", "Frames"), [this]()
					{
						if (!Asset.IsValid() || !Asset->Flipbooks.IsValidIndex(SelectedFlipbookIndex)) return FText::GetEmpty();
						const FFlipbookProfileEntry& FB = Asset->Flipbooks[SelectedFlipbookIndex];
						UPaperFlipbook* LoadedFB = FB.Identity.Flipbook.IsValid() ? FB.Identity.Flipbook.Get() : nullptr;
						if (LoadedFB && LoadedFB->GetFramesPerSecond() > 0.0f)
							return FText::FromString(FString::Printf(TEXT("%d   %.0f FPS   %.2fs"), FB.CombatData.Frames.Num(), LoadedFB->GetFramesPerSecond(), LoadedFB->GetTotalDuration()));
						return FText::AsNumber(FB.CombatData.Frames.Num());
					})
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 1, 0, 0)
				[
					MakeInfoRow(LOCTEXT("LblExcluded", "Excluded"), [this]()
					{
						if (!Asset.IsValid() || !Asset->Flipbooks.IsValidIndex(SelectedFlipbookIndex)) return FText::GetEmpty();
						return FText::Format(LOCTEXT("ExclVal", "{0} frames"), FText::AsNumber(Asset->Flipbooks[SelectedFlipbookIndex].CombatData.ExcludedFrames.Num()));
					},
					[this]() { return (Asset.IsValid() && Asset->Flipbooks.IsValidIndex(SelectedFlipbookIndex) && Asset->Flipbooks[SelectedFlipbookIndex].CombatData.ExcludedFrames.Num() > 0) ? EVisibility::Visible : EVisibility::Collapsed; })
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 1, 0, 0)
				[
					MakeInfoRow(LOCTEXT("LblHitboxes", "Hitboxes"), [this]()
					{
						if (!Asset.IsValid() || !Asset->Flipbooks.IsValidIndex(SelectedFlipbookIndex)) return FText::GetEmpty();
						const FFlipbookProfileEntry& FB = Asset->Flipbooks[SelectedFlipbookIndex];
						int32 Atk = 0, Hrt = 0, Sock = 0;
						for (const FFrameHitboxData& F : FB.CombatData.Frames) { for (const FHitboxData& H : F.Hitboxes) { if (H.Type == EHitboxType::Attack) Atk++; else if (H.Type == EHitboxType::Hurtbox) Hrt++; } Sock += F.Sockets.Num(); }
						if (Atk == 0 && Hrt == 0 && Sock == 0) return LOCTEXT("HBNone", "None");
						TArray<FString> P; if (Atk > 0) P.Add(FString::Printf(TEXT("%d ATK"), Atk)); if (Hrt > 0) P.Add(FString::Printf(TEXT("%d HRT"), Hrt)); if (Sock > 0) P.Add(FString::Printf(TEXT("%d sockets"), Sock));
						return FText::FromString(FString::Join(P, TEXT("   ")));
					})
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 1, 0, 0)
				[
					MakeInfoRow(LOCTEXT("LblDamage", "Damage"), [this]()
					{
						if (!Asset.IsValid() || !Asset->Flipbooks.IsValidIndex(SelectedFlipbookIndex)) return FText::GetEmpty();
						const FFlipbookProfileEntry& FB = Asset->Flipbooks[SelectedFlipbookIndex];
						int32 MinDmg = INT32_MAX, MaxDmg = 0;
						bool bHasAtk = false;
						for (const FFrameHitboxData& F : FB.CombatData.Frames)
						{
							for (const FHitboxData& H : F.Hitboxes)
							{
								if (H.Type == EHitboxType::Attack && H.Damage > 0)
								{
									bHasAtk = true;
									MinDmg = FMath::Min(MinDmg, H.Damage);
									MaxDmg = FMath::Max(MaxDmg, H.Damage);
								}
							}
						}
						if (!bHasAtk) return FText::GetEmpty();
						if (MinDmg == MaxDmg) return FText::FromString(FString::Printf(TEXT("%d"), MaxDmg));
						return FText::FromString(FString::Printf(TEXT("%d - %d"), MinDmg, MaxDmg));
					})
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 1, 0, 0)
				[
					SNew(SHorizontalBox)
					.Visibility_Lambda([this]() {
						if (!Asset.IsValid() || !Asset->Flipbooks.IsValidIndex(SelectedFlipbookIndex)) return EVisibility::Collapsed;
						const FFlipbookProfileEntry& FB = Asset->Flipbooks[SelectedFlipbookIndex];
						for (int32 i = 0; i < FB.CombatData.Frames.Num(); i++) { if (FB.CombatData.Frames[i].bInvulnerable) return EVisibility::Visible; }
						return EVisibility::Collapsed;
					})
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 6, 0)
					[
						SNew(STextBlock).Text(LOCTEXT("LblInvul", "Invul Frames"))
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
						.ColorAndOpacity(FSlateColor(FLinearColor(0.40f, 0.40f, 0.45f)))
					]
					+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text_Lambda([this]() -> FText {
							if (!Asset.IsValid() || !Asset->Flipbooks.IsValidIndex(SelectedFlipbookIndex)) return FText::GetEmpty();
							const FFlipbookProfileEntry& FB = Asset->Flipbooks[SelectedFlipbookIndex];
							TArray<FString> FrameNums;
							for (int32 i = 0; i < FB.CombatData.Frames.Num(); i++) { if (FB.CombatData.Frames[i].bInvulnerable) FrameNums.Add(FString::FromInt(i)); }
							return FText::FromString(FString::Join(FrameNums, TEXT(", ")));
						})
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
						.ColorAndOpacity(FSlateColor(FLinearColor(0.3f, 0.7f, 1.0f)))
					]
				]
			]
		]

		// --- Connections section ---
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
			.Padding(FMargin(6, 4))
			.Visibility_Lambda([this]() { return (Asset.IsValid() && Asset->Flipbooks.IsValidIndex(SelectedFlipbookIndex)) ? EVisibility::Visible : EVisibility::Collapsed; })
			[
				SNew(SVerticalBox)

				// Phase assignment row — label + current value + change/remove buttons
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 6, 0)
					[ SNew(STextBlock).Text(LOCTEXT("LblPhase2", "Phase")).Font(FCoreStyle::GetDefaultFontStyle("Regular", 8)).ColorAndOpacity(FSlateColor(FLinearColor(0.40f, 0.40f, 0.45f))) ]

					+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
					[
						SNew(SComboButton)
						.ButtonStyle(FAppStyle::Get(), "SimpleButton")
						.HasDownArrow(true)
						.ContentPadding(FMargin(4, 1))
						.ButtonContent()
						[
							SNew(STextBlock)
							.Text_Lambda([this]()
							{
								if (!Asset.IsValid() || !Asset->Flipbooks.IsValidIndex(SelectedFlipbookIndex)) return LOCTEXT("PhaseNone2", "Not assigned");
								const FPhaseGroup* PG = Asset->FindPhaseGroupForFlipbook(Asset->Flipbooks[SelectedFlipbookIndex].Identity.FlipbookName);
								if (!PG) return LOCTEXT("PhaseNone2", "Not assigned");
								EAnimationPhase Phase = Asset->GetPhaseForFlipbook(Asset->Flipbooks[SelectedFlipbookIndex].Identity.FlipbookName);
								FString N; switch (Phase) { case EAnimationPhase::Startup: N = TEXT("Startup"); break; case EAnimationPhase::Active: N = TEXT("Active"); break; case EAnimationPhase::Recovery: N = TEXT("Recovery"); break; default: N = TEXT("?"); }
								return FText::FromString(FString::Printf(TEXT("%s  (%s)"), *N, *PG->GroupName));
							})
							.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
							.ColorAndOpacity_Lambda([this]()
							{
								if (!Asset.IsValid() || !Asset->Flipbooks.IsValidIndex(SelectedFlipbookIndex)) return FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f));
								return Asset->FindPhaseGroupForFlipbook(Asset->Flipbooks[SelectedFlipbookIndex].Identity.FlipbookName)
									? FSlateColor(FLinearColor(0.75f, 0.75f, 0.78f))
									: FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f));
							})
						]
						.OnGetMenuContent_Lambda([this]() -> TSharedRef<SWidget>
						{
							if (!Asset.IsValid() || !Asset->Flipbooks.IsValidIndex(SelectedFlipbookIndex))
								return SNullWidget::NullWidget;

							const FString& FBName = Asset->Flipbooks[SelectedFlipbookIndex].Identity.FlipbookName;
							TSharedRef<SVerticalBox> PopupContent = SNew(SVerticalBox);

							// Helper: clickable phase slot mini-card
							auto MakeSlotCard = [this, FBName](const FString& GroupName, EAnimationPhase Phase, const FText& Label, const FLinearColor& Color) -> TSharedRef<SWidget>
							{
								FPhaseGroup* PG = Asset->FindPhaseGroupMutable(GroupName);
								FString SlotFB = PG ? PG->GetFlipbookForPhase(Phase) : FString();
								bool bIsCurrent = (SlotFB == FBName);
								bool bEmpty = SlotFB.IsEmpty();
								FString Display = bEmpty ? TEXT("+") : SlotFB;
								if (Display.Len() > 10) Display = Display.Left(8) + TEXT("..");

								// Resolve flipbook for thumbnail
								UPaperFlipbook* SlotFlipbook = nullptr;
								if (!bEmpty)
								{
									for (const FFlipbookProfileEntry& FB : Asset->Flipbooks)
									{
										if (FB.Identity.FlipbookName == SlotFB)
										{
											SlotFlipbook = FB.Identity.Flipbook.LoadSynchronous();
											break;
										}
									}
								}

								// Build card content — thumbnail + text when filled, centered "+" when empty
								TSharedRef<SWidget> CardContent = bEmpty
									? StaticCastSharedRef<SWidget>(SNew(STextBlock)
										.Text(FText::FromString(TEXT("+")))
										.Font(FCoreStyle::GetDefaultFontStyle("Regular", 14))
										.ColorAndOpacity(FSlateColor(FLinearColor(0.35f, 0.35f, 0.35f)))
										.Justification(ETextJustify::Center))
									: StaticCastSharedRef<SWidget>(SNew(SHorizontalBox)
										+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 3, 0)
										[
											SNew(SBox).WidthOverride(28.f).HeightOverride(28.f)
											[
												SNew(SFlipbookThumbnail).Flipbook(SlotFlipbook)
											]
										]
										+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
										[
											SNew(STextBlock)
											.Text(FText::FromString(Display))
											.Font(FCoreStyle::GetDefaultFontStyle("Regular", 7))
											.ColorAndOpacity(FSlateColor(bIsCurrent ? FLinearColor::White : FLinearColor(0.65f, 0.65f, 0.65f)))
											.AutoWrapText(true)
										]);

								return SNew(SBox).WidthOverride(110.f).HeightOverride(48.f)
								[
									SNew(SButton).ButtonStyle(FAppStyle::Get(), "NoBorder")
									.ToolTipText(bEmpty ? FText::Format(LOCTEXT("AssignSlotTip", "Assign to {0}"), Label) : FText::FromString(SlotFB))
									.OnClicked_Lambda([this, FBName, GroupName, Phase]()
									{
										if (!Asset.IsValid()) return FReply::Handled();
										BeginTransaction(LOCTEXT("AssignPhaseSlot2", "Assign to Phase"));
										const FPhaseGroup* Old = Asset->FindPhaseGroupForFlipbook(FBName);
										if (Old) { FPhaseGroup* M = Asset->FindPhaseGroupMutable(Old->GroupName); if (M) M->SetFlipbookForPhase(Asset->GetPhaseForFlipbook(FBName), FString()); }
										// Displace existing occupant of the target slot to Ungrouped
										FPhaseGroup* Target = Asset->FindPhaseGroupMutable(GroupName);
										if (Target)
										{
											FString DisplacedFB = Target->GetFlipbookForPhase(Phase);
											if (!DisplacedFB.IsEmpty() && DisplacedFB != FBName)
											{
												for (FFlipbookProfileEntry& FB : Asset->Flipbooks)
												{
													if (FB.Identity.FlipbookName == DisplacedFB) { FB.FlipbookGroup = NAME_None; break; }
												}
											}
											Target->SetFlipbookForPhase(Phase, FBName);
										}
										for (FFlipbookProfileEntry& FB : Asset->Flipbooks) if (FB.Identity.FlipbookName == FBName) { FB.FlipbookGroup = FName(*GroupName); break; }
										EndTransaction();
										FSlateApplication::Get().DismissAllMenus();
										RefreshOverviewFlipbookList();
										return FReply::Handled();
									})
									[
										SNew(SBorder)
										.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
										.BorderBackgroundColor(bIsCurrent ? FLinearColor(0.18f, 0.35f, 0.55f) : FLinearColor(0.06f, 0.06f, 0.08f))
										.Padding(FMargin(3, 2))
										[
											SNew(SVerticalBox)
											+ SVerticalBox::Slot().FillHeight(1.0f).VAlign(VAlign_Center)
											[ CardContent ]
											+ SVerticalBox::Slot().AutoHeight().Padding(0, 1, 0, 0)
											[ SNew(SBox).HeightOverride(3.f) [ SNew(SColorBlock).Color(Color) ] ]
											+ SVerticalBox::Slot().AutoHeight()
											[ SNew(STextBlock).Text(Label).Font(FCoreStyle::GetDefaultFontStyle("Regular", 6)).ColorAndOpacity(FSlateColor(Color * 0.7f)).Justification(ETextJustify::Center) ]
										]
									]
								];
							};

							const FLinearColor ColS(0.85f, 0.65f, 0.15f), ColA(0.85f, 0.25f, 0.25f), ColR(0.35f, 0.55f, 0.85f);

							// Remove button (if assigned)
							if (Asset->FindPhaseGroupForFlipbook(FBName))
							{
								PopupContent->AddSlot().AutoHeight().Padding(8, 4)
								[
									SNew(SButton).ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
									.OnClicked_Lambda([this, FBName]()
									{
										if (!Asset.IsValid()) return FReply::Handled();
										BeginTransaction(LOCTEXT("RemovePhase2", "Remove Phase Assignment"));
										const FPhaseGroup* PG = Asset->FindPhaseGroupForFlipbook(FBName);
										if (PG) { FPhaseGroup* M = Asset->FindPhaseGroupMutable(PG->GroupName); if (M) M->SetFlipbookForPhase(Asset->GetPhaseForFlipbook(FBName), FString()); }
										for (FFlipbookProfileEntry& FB : Asset->Flipbooks) if (FB.Identity.FlipbookName == FBName) { FB.FlipbookGroup = NAME_None; break; }
										EndTransaction();
										FSlateApplication::Get().DismissAllMenus();
										RefreshOverviewFlipbookList();
										return FReply::Handled();
									})
									[ SNew(STextBlock).Text(LOCTEXT("RemovePhase3", "Remove from Phase")).Font(FCoreStyle::GetDefaultFontStyle("Regular", 8)).ColorAndOpacity(FSlateColor(FLinearColor(0.8f, 0.4f, 0.4f))) ]
								];
								PopupContent->AddSlot().AutoHeight().Padding(8, 0, 8, 2) [ SNew(SSeparator) ];
							}

							// Phase groups with visual slot cards
							for (const FPhaseGroup& PG : Asset->PhaseGroups)
							{
								FString GN = PG.GroupName;
								FName GNName = FName(*GN);
								PopupContent->AddSlot().AutoHeight().Padding(8, 4)
								[
									SNew(SVerticalBox)
									+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 2)
									[
										SNew(SHorizontalBox)
										+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
										[
											SNew(SEditableTextBox)
											.Text(FText::FromString(GN))
											.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
											.ForegroundColor(FLinearColor(0.6f, 0.6f, 0.65f))
											.BackgroundColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.0f))
											.SelectAllTextWhenFocused(true)
											.OnTextCommitted_Lambda([this, GNName](const FText& InText, ETextCommit::Type CommitType)
											{
												OnFlipbookGroupNameCommitted(InText, CommitType, GNName);
											})
											.OnVerifyTextChanged_Lambda([this, GNName](const FText& InText, FText& OutError) -> bool
											{
												return OnVerifyFlipbookGroupNameChanged(InText, OutError, GNName);
											})
										]
									]
									+ SVerticalBox::Slot().AutoHeight()
									[
										SNew(SHorizontalBox)
										+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 2, 0) [ MakeSlotCard(GN, EAnimationPhase::Startup, LOCTEXT("SlS", "Startup"), ColS) ]
										+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 2, 0) [ MakeSlotCard(GN, EAnimationPhase::Active, LOCTEXT("SlA", "Active"), ColA) ]
										+ SHorizontalBox::Slot().AutoWidth() [ MakeSlotCard(GN, EAnimationPhase::Recovery, LOCTEXT("SlR", "Recovery"), ColR) ]
									]
								];
							}

							// New phase group
							PopupContent->AddSlot().AutoHeight().Padding(8, 6, 8, 4)
							[
								SNew(SButton).ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
								.OnClicked_Lambda([this, FBName]()
								{
									if (!Asset.IsValid()) return FReply::Handled();
									FString Base = TEXT("New Phase Group"); FString NewName = Base; int32 S = 1;
									while (Asset->FindPhaseGroup(NewName)) { NewName = FString::Printf(TEXT("%s %d"), *Base, ++S); }
									BeginTransaction(LOCTEXT("CreatePGCard", "Create Phase Group"));
									FPhaseGroup NG; NG.GroupName = NewName; NG.StartupFlipbook = FBName;
									Asset->PhaseGroups.Add(MoveTemp(NG));
									FFlipbookGroupInfo GI; GI.GroupName = FName(*NewName); GI.bIsPhaseGroup = true;
									Asset->FlipbookGroups.Add(GI);
									for (FFlipbookProfileEntry& FB : Asset->Flipbooks) if (FB.Identity.FlipbookName == FBName) { FB.FlipbookGroup = FName(*NewName); break; }
									EndTransaction();
									FSlateApplication::Get().DismissAllMenus();
									RefreshOverviewFlipbookList();
									return FReply::Handled();
								})
								[ SNew(STextBlock).Text(LOCTEXT("NewPG2", "+ New Phase Group")).Font(FCoreStyle::GetDefaultFontStyle("Regular", 8)) ]
							];

							return SNew(SBox).MaxDesiredHeight(400.f)
								[
									SNew(SScrollBox)
									+ SScrollBox::Slot() [ PopupContent ]
								];
						})
					]
				]

				// Phase slots summary (visible 3-row list)
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)
				[
					SNew(SVerticalBox)
					.Visibility_Lambda([this]() {
						if (!Asset.IsValid() || !Asset->Flipbooks.IsValidIndex(SelectedFlipbookIndex)) return EVisibility::Collapsed;
						return Asset->FindPhaseGroupForFlipbook(Asset->Flipbooks[SelectedFlipbookIndex].Identity.FlipbookName) ? EVisibility::Visible : EVisibility::Collapsed;
					})
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
						[ SNew(SBorder).BorderImage(FAppStyle::GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor(0.85f, 0.65f, 0.15f)).Padding(0)[ SNew(SBox).WidthOverride(6).HeightOverride(6) ] ]
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 6, 0)
						[ SNew(STextBlock).Text(LOCTEXT("PhSlotS", "Startup")).Font(FCoreStyle::GetDefaultFontStyle("Regular", 8)).ColorAndOpacity(FSlateColor(FLinearColor(0.40f, 0.40f, 0.45f))) ]
						+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
						[
							SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
							.Text_Lambda([this]() {
								if (!Asset.IsValid() || !Asset->Flipbooks.IsValidIndex(SelectedFlipbookIndex)) return FText::GetEmpty();
								const FPhaseGroup* PG = Asset->FindPhaseGroupForFlipbook(Asset->Flipbooks[SelectedFlipbookIndex].Identity.FlipbookName);
								if (!PG) return FText::GetEmpty();
								return PG->StartupFlipbook.IsEmpty() ? LOCTEXT("PhEmpty", "\x2014") : FText::FromString(PG->StartupFlipbook);
							})
							.ColorAndOpacity_Lambda([this]() {
								if (!Asset.IsValid() || !Asset->Flipbooks.IsValidIndex(SelectedFlipbookIndex)) return FSlateColor(FLinearColor(0.4f, 0.4f, 0.4f));
								const FPhaseGroup* PG = Asset->FindPhaseGroupForFlipbook(Asset->Flipbooks[SelectedFlipbookIndex].Identity.FlipbookName);
								if (!PG || PG->StartupFlipbook.IsEmpty()) return FSlateColor(FLinearColor(0.4f, 0.4f, 0.4f));
								return PG->StartupFlipbook == Asset->Flipbooks[SelectedFlipbookIndex].Identity.FlipbookName ? FSlateColor(FLinearColor(1.0f, 1.0f, 1.0f)) : FSlateColor(FLinearColor(0.65f, 0.65f, 0.7f));
							})
						]
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 1, 0, 0)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
						[ SNew(SBorder).BorderImage(FAppStyle::GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor(0.85f, 0.25f, 0.25f)).Padding(0)[ SNew(SBox).WidthOverride(6).HeightOverride(6) ] ]
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 6, 0)
						[ SNew(STextBlock).Text(LOCTEXT("PhSlotA", "Active")).Font(FCoreStyle::GetDefaultFontStyle("Regular", 8)).ColorAndOpacity(FSlateColor(FLinearColor(0.40f, 0.40f, 0.45f))) ]
						+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
						[
							SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
							.Text_Lambda([this]() {
								if (!Asset.IsValid() || !Asset->Flipbooks.IsValidIndex(SelectedFlipbookIndex)) return FText::GetEmpty();
								const FPhaseGroup* PG = Asset->FindPhaseGroupForFlipbook(Asset->Flipbooks[SelectedFlipbookIndex].Identity.FlipbookName);
								if (!PG) return FText::GetEmpty();
								return PG->ActiveFlipbook.IsEmpty() ? LOCTEXT("PhEmpty", "\x2014") : FText::FromString(PG->ActiveFlipbook);
							})
							.ColorAndOpacity_Lambda([this]() {
								if (!Asset.IsValid() || !Asset->Flipbooks.IsValidIndex(SelectedFlipbookIndex)) return FSlateColor(FLinearColor(0.4f, 0.4f, 0.4f));
								const FPhaseGroup* PG = Asset->FindPhaseGroupForFlipbook(Asset->Flipbooks[SelectedFlipbookIndex].Identity.FlipbookName);
								if (!PG || PG->ActiveFlipbook.IsEmpty()) return FSlateColor(FLinearColor(0.4f, 0.4f, 0.4f));
								return PG->ActiveFlipbook == Asset->Flipbooks[SelectedFlipbookIndex].Identity.FlipbookName ? FSlateColor(FLinearColor(1.0f, 1.0f, 1.0f)) : FSlateColor(FLinearColor(0.65f, 0.65f, 0.7f));
							})
						]
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 1, 0, 0)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
						[ SNew(SBorder).BorderImage(FAppStyle::GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor(0.35f, 0.55f, 0.85f)).Padding(0)[ SNew(SBox).WidthOverride(6).HeightOverride(6) ] ]
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 6, 0)
						[ SNew(STextBlock).Text(LOCTEXT("PhSlotR", "Recovery")).Font(FCoreStyle::GetDefaultFontStyle("Regular", 8)).ColorAndOpacity(FSlateColor(FLinearColor(0.40f, 0.40f, 0.45f))) ]
						+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
						[
							SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
							.Text_Lambda([this]() {
								if (!Asset.IsValid() || !Asset->Flipbooks.IsValidIndex(SelectedFlipbookIndex)) return FText::GetEmpty();
								const FPhaseGroup* PG = Asset->FindPhaseGroupForFlipbook(Asset->Flipbooks[SelectedFlipbookIndex].Identity.FlipbookName);
								if (!PG) return FText::GetEmpty();
								return PG->RecoveryFlipbook.IsEmpty() ? LOCTEXT("PhEmpty", "\x2014") : FText::FromString(PG->RecoveryFlipbook);
							})
							.ColorAndOpacity_Lambda([this]() {
								if (!Asset.IsValid() || !Asset->Flipbooks.IsValidIndex(SelectedFlipbookIndex)) return FSlateColor(FLinearColor(0.4f, 0.4f, 0.4f));
								const FPhaseGroup* PG = Asset->FindPhaseGroupForFlipbook(Asset->Flipbooks[SelectedFlipbookIndex].Identity.FlipbookName);
								if (!PG || PG->RecoveryFlipbook.IsEmpty()) return FSlateColor(FLinearColor(0.4f, 0.4f, 0.4f));
								return PG->RecoveryFlipbook == Asset->Flipbooks[SelectedFlipbookIndex].Identity.FlipbookName ? FSlateColor(FLinearColor(1.0f, 1.0f, 1.0f)) : FSlateColor(FLinearColor(0.65f, 0.65f, 0.7f));
							})
						]
					]
				]

				+ SVerticalBox::Slot().AutoHeight().Padding(0, 1, 0, 0)
				[
					MakeInfoRow(LOCTEXT("LblTags", "Tags"), [this]()
					{
						if (!Asset.IsValid() || !Asset->Flipbooks.IsValidIndex(SelectedFlipbookIndex)) return FText::GetEmpty();
						const FString& Name = Asset->Flipbooks[SelectedFlipbookIndex].Identity.FlipbookName;
						TArray<FString> Tags;
						for (auto& Pair : Asset->TagMappings) { if (Pair.Value.FlipbookNames.Contains(Name)) { FString T = Pair.Key.IsValid() ? Pair.Key.ToString() : TEXT("(none)"); static const FString Pre = TEXT("Paper2DPlus.Animation."); if (T.StartsWith(Pre)) T = T.RightChop(Pre.Len()); Tags.Add(T); } }
						return Tags.Num() > 0 ? FText::FromString(FString::Join(Tags, TEXT(", "))) : LOCTEXT("TagNone", "---");
					})
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 1, 0, 0)
				[
					MakeInfoRow(LOCTEXT("LblEffects", "Effects"), [this]()
					{
						if (!Asset.IsValid() || !Asset->Flipbooks.IsValidIndex(SelectedFlipbookIndex)) return FText::GetEmpty();
						const auto& FX = Asset->Flipbooks[SelectedFlipbookIndex].Effects;
						if (FX.Num() == 0) return LOCTEXT("FXNone", "---");
						TArray<FString> Names; for (const auto& E : FX) { Names.Add(FString::Printf(TEXT("%s (f%d)"), E.EffectFlipbook ? *E.EffectFlipbook->GetName() : TEXT("?"), E.TriggerFrame)); }
						return FText::FromString(FString::Join(Names, TEXT(", ")));
					})
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 1, 0, 0)
				[
					MakeInfoRow(LOCTEXT("LblMotion", "Motion"), [this]()
					{
						if (!Asset.IsValid() || !Asset->Flipbooks.IsValidIndex(SelectedFlipbookIndex)) return LOCTEXT("MotNone", "---");
						const FFlipbookProfileEntry& FB = Asset->Flipbooks[SelectedFlipbookIndex];
						if (!FB.MotionData.HasRootMotion()) return LOCTEXT("MotNone", "---");
						FVector2D Tot = FVector2D::ZeroVector, Prev = FVector2D::ZeroVector;
						for (const FRootMotionFrameData& RM : FB.MotionData.RootMotion) { if (!RM.Position.IsNearlyZero()) { Tot += RM.Position - Prev; Prev = RM.Position; } }
						return FText::FromString(FString::Printf(TEXT("%.0f, %.0f px"), Tot.X, Tot.Y));
					})
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 1, 0, 0)
				[
					MakeInfoRow(LOCTEXT("LblPZD", "PaperZD Sequence"), [this]()
					{
						if (!Asset.IsValid() || !Asset->Flipbooks.IsValidIndex(SelectedFlipbookIndex)) return FText::GetEmpty();
						UPaperZDAnimSequence* Seq = Asset->Flipbooks[SelectedFlipbookIndex].Identity.PaperZDSequence;
						return Seq ? FText::FromString(Seq->GetName()) : LOCTEXT("PZDNone", "---");
					})
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 1, 0, 0)
				[
					MakeInfoRow(LOCTEXT("LblSource", "Source"), [this]()
					{
						if (!Asset.IsValid() || !Asset->Flipbooks.IsValidIndex(SelectedFlipbookIndex)) return FText::GetEmpty();
						const auto& Tex = Asset->Flipbooks[SelectedFlipbookIndex].SourceTexture;
						return !Tex.IsNull() ? FText::FromString(FPaths::GetBaseFilename(Tex.GetAssetName())) : LOCTEXT("SrcNone", "---");
					})
				]
			]
		]

		// --- Completion section ---
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0)
		[
			SNew(SVerticalBox)
			.Visibility_Lambda([this]() { return (Asset.IsValid() && Asset->Flipbooks.IsValidIndex(SelectedFlipbookIndex)) ? EVisibility::Visible : EVisibility::Collapsed; })

			// Header + progress + buttons
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 4)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(STextBlock)
						.Text_Lambda([this]()
						{
							if (!Asset.IsValid() || !Asset->Flipbooks.IsValidIndex(SelectedFlipbookIndex)) return LOCTEXT("CompHdr", "Completion");
							int32 Flags = Asset->Flipbooks[SelectedFlipbookIndex].EditorMeta.CompletionFlags & 0x7F;
							int32 Done = 0; int32 M = Flags; while (M) { Done += (M & 1); M >>= 1; }
							return FText::Format(LOCTEXT("CompHdrCount", "Completion  {0}/7"), FText::AsNumber(Done));
						})
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 2, 0, 0)
					[
						SNew(SProgressBar)
						.Percent_Lambda([this]() -> TOptional<float>
						{
							if (!Asset.IsValid() || !Asset->Flipbooks.IsValidIndex(SelectedFlipbookIndex)) return 0.0f;
							int32 Flags = Asset->Flipbooks[SelectedFlipbookIndex].EditorMeta.CompletionFlags & 0x7F;
							int32 Done = 0; int32 M = Flags; while (M) { Done += (M & 1); M >>= 1; }
							return Done / 7.0f;
						})
						.FillColorAndOpacity_Lambda([this]() -> FLinearColor
						{
							if (!Asset.IsValid() || !Asset->Flipbooks.IsValidIndex(SelectedFlipbookIndex)) return FLinearColor(0.2f, 0.5f, 0.2f);
							int32 Flags = Asset->Flipbooks[SelectedFlipbookIndex].EditorMeta.CompletionFlags & 0x7F;
							int32 Done = 0; int32 M = Flags; while (M) { Done += (M & 1); M >>= 1; }
							return (Done == 7) ? FLinearColor(0.2f, 0.7f, 0.3f) : FLinearColor(0.25f, 0.45f, 0.65f);
						})
					]
				]

				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8, 0, 0, 0)
				[
					SNew(SButton).ButtonStyle(FAppStyle::Get(), "SimpleButton")
					.ToolTipText(LOCTEXT("MarkAllTip", "Mark all as complete"))
					.OnClicked_Lambda([this]()
					{
						if (!Asset.IsValid() || !Asset->Flipbooks.IsValidIndex(SelectedFlipbookIndex)) return FReply::Handled();
						BeginTransaction(LOCTEXT("MarkAllComplete", "Mark All Complete"));
						Asset->Flipbooks[SelectedFlipbookIndex].EditorMeta.CompletionFlags = 0x7F;
						EndTransaction();
						if (FlipbookGroupsListBox.IsValid()) FlipbookGroupsListBox->Invalidate(EInvalidateWidgetReason::Paint);
						return FReply::Handled();
					})
					[ SNew(STextBlock).Text(LOCTEXT("MarkAllBtn2", "All")).Font(FCoreStyle::GetDefaultFontStyle("Regular", 8)).ColorAndOpacity(FSlateColor(FLinearColor(0.4f, 0.7f, 0.4f))) ]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2, 0, 0, 0)
				[
					SNew(SButton).ButtonStyle(FAppStyle::Get(), "SimpleButton")
					.ToolTipText(LOCTEXT("UncheckAllTip", "Uncheck all"))
					.OnClicked_Lambda([this]()
					{
						if (!Asset.IsValid() || !Asset->Flipbooks.IsValidIndex(SelectedFlipbookIndex)) return FReply::Handled();
						BeginTransaction(LOCTEXT("UncheckAllComplete", "Uncheck All Complete"));
						Asset->Flipbooks[SelectedFlipbookIndex].EditorMeta.CompletionFlags = 0;
						EndTransaction();
						if (FlipbookGroupsListBox.IsValid()) FlipbookGroupsListBox->Invalidate(EInvalidateWidgetReason::Paint);
						return FReply::Handled();
					})
					[ SNew(STextBlock).Text(LOCTEXT("UncheckAllBtn2", "None")).Font(FCoreStyle::GetDefaultFontStyle("Regular", 8)).ColorAndOpacity(FSlateColor(FLinearColor(0.7f, 0.4f, 0.4f))) ]
				]
			]

			// 2-column checkbox grid. Bits 3 (Phases) and 4 (Effects) are retired —
			// the corresponding tabs were deleted. PostLoad clears those bits on load.
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SUniformGridPanel).SlotPadding(FMargin(0, 1))
				+ SUniformGridPanel::Slot(0, 0) [ MakeCheckbox(LOCTEXT("CkHitboxes", "Hitboxes"), 0) ]
				+ SUniformGridPanel::Slot(1, 0) [ MakeCheckbox(LOCTEXT("CkAlignment", "Alignment"), 1) ]
				+ SUniformGridPanel::Slot(0, 1) [ MakeCheckbox(LOCTEXT("CkTiming", "Timing"), 2) ]
				+ SUniformGridPanel::Slot(1, 1) [ MakeCheckbox(LOCTEXT("CkMotion", "Motion"), 5) ]
				+ SUniformGridPanel::Slot(0, 2) [ MakeCheckbox(LOCTEXT("CkTags", "Tags"), 6) ]
			]
		];
}

TSharedRef<SWidget> SCharacterProfileAssetEditor::BuildPhaseGroupDetailsView()
{
	// Phase group details: editable name + color, list of custom slots with
	// per-slot name/color, add-slot button, back button. All state mutations
	// go through BeginTransaction/EndTransaction per the project's button-
	// lambda transaction template (CLAUDE.md Refresh Architecture rule 13).
	return SNew(SVerticalBox)

		// Back button — clears SelectedPhaseGroupName, switches panel to flipbook view
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
			.ToolTipText(LOCTEXT("PhaseGroupDetailsBackTip", "Clear phase group selection and return to flipbook details"))
			.OnClicked_Lambda([this]()
			{
				SelectedPhaseGroupName = NAME_None;
				Invalidate(EInvalidateWidgetReason::Paint);
				return FReply::Handled();
			})
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
				[ SNew(STextBlock).Text(LOCTEXT("PhaseGroupDetailsBackGlyph", "<")).Font(FCoreStyle::GetDefaultFontStyle("Bold", 9)) ]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[ SNew(STextBlock).Text(LOCTEXT("PhaseGroupDetailsBack", "Back to Flipbook")).Font(FCoreStyle::GetDefaultFontStyle("Regular", 9)) ]
			]
		]

		// Placeholder if no phase group selected (shouldn't usually render — dispatch catches it)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("PhaseGroupDetailsNone", "No phase group selected"))
			.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)))
			.Visibility_Lambda([this]() { return SelectedPhaseGroupName.IsNone() ? EVisibility::Visible : EVisibility::Collapsed; })
		]

		// Phase group name header
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 4)
		[
			SNew(STextBlock)
			.Text_Lambda([this]() { return FText::FromName(SelectedPhaseGroupName); })
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
			.Visibility_Lambda([this]() { return SelectedPhaseGroupName.IsNone() ? EVisibility::Collapsed : EVisibility::Visible; })
		]

		// Slot count summary
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
		[
			SNew(STextBlock)
			.Text_Lambda([this]()
			{
				if (!Asset.IsValid() || SelectedPhaseGroupName.IsNone()) return FText::GetEmpty();
				const FPhaseGroup* G = Asset->FindPhaseGroup(SelectedPhaseGroupName.ToString());
				if (!G) return FText::GetEmpty();
				int32 BuiltInAssigned = (!G->StartupFlipbook.IsEmpty() ? 1 : 0)
					+ (!G->ActiveFlipbook.IsEmpty() ? 1 : 0)
					+ (!G->RecoveryFlipbook.IsEmpty() ? 1 : 0);
				return FText::Format(LOCTEXT("PhaseGroupDetailsSummary",
					"Built-in: {0}/3 assigned  ·  Custom slots: {1}"),
					FText::AsNumber(BuiltInAssigned),
					FText::AsNumber(G->CustomSlots.Num()));
			})
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
			.ColorAndOpacity(FSlateColor(FLinearColor(0.55f, 0.55f, 0.58f)))
			.Visibility_Lambda([this]() { return SelectedPhaseGroupName.IsNone() ? EVisibility::Collapsed : EVisibility::Visible; })
		]

		// Custom slots section header
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 2)
		[
			SNew(SHorizontalBox)
			.Visibility_Lambda([this]() { return SelectedPhaseGroupName.IsNone() ? EVisibility::Collapsed : EVisibility::Visible; })

			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("CustomSlotsHeader", "CUSTOM SLOTS"))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)))
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.ToolTipText(LOCTEXT("AddCustomSlotTip",
					"Add a new custom phase slot (e.g., Charge, Hold, Followthrough).\n"
					"Each slot has its own name, color, and flipbook assignment, and\n"
					"can be queried at runtime via the Paper2DPlus Blueprint library."))
				.OnClicked_Lambda([this]()
				{
					if (!Asset.IsValid() || SelectedPhaseGroupName.IsNone()) return FReply::Handled();
					BeginTransaction(LOCTEXT("AddCustomPhaseSlot", "Add Custom Phase Slot"));
					FPhaseGroup* G = Asset->FindPhaseGroupMutable(SelectedPhaseGroupName.ToString());
					if (G)
					{
						// Generate a unique slot name within this group
						FString BaseName = TEXT("Custom");
						FString NewName = BaseName;
						int32 Suffix = 1;
						while (G->FindCustomSlot(NewName) != nullptr)
						{
							NewName = FString::Printf(TEXT("%s%d"), *BaseName, ++Suffix);
						}
						FCustomPhaseSlot NewSlot;
						NewSlot.SlotName = NewName;
						G->CustomSlots.Add(NewSlot);
					}
					EndTransaction();
					// Force a full rebuild of the overview tab: the phase group card row
					// has new slot widgets that need to be constructed.
					RefreshFlipbookGroupsPanel();
					RefreshPhaseGroupCustomSlotsList();
					Invalidate(EInvalidateWidgetReason::Paint);
					return FReply::Handled();
				})
				[
					SNew(STextBlock)
					.Text(LOCTEXT("AddCustomSlotBtn", "+ Add Slot"))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
					.ColorAndOpacity(FSlateColor(FLinearColor(0.4f, 0.7f, 0.9f)))
				]
			]
		]

		// Custom slots list — populated/rebuilt by RefreshPhaseGroupCustomSlotsList()
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SBox)
			.Visibility_Lambda([this]() { return SelectedPhaseGroupName.IsNone() ? EVisibility::Collapsed : EVisibility::Visible; })
			[
				SAssignNew(PhaseGroupCustomSlotsBox, SVerticalBox)
			]
		];
}

void SCharacterProfileAssetEditor::SelectPhaseGroup(FName GroupName)
{
	if (SelectedPhaseGroupName == GroupName) return;
	SelectedPhaseGroupName = GroupName;
	RefreshPhaseGroupCustomSlotsList();
	Invalidate(EInvalidateWidgetReason::Paint);
}

void SCharacterProfileAssetEditor::RefreshPhaseGroupCustomSlotsList()
{
	if (!PhaseGroupCustomSlotsBox.IsValid()) return;

	PhaseGroupCustomSlotsBox->ClearChildren();

	if (!Asset.IsValid() || SelectedPhaseGroupName.IsNone()) return;
	const FPhaseGroup* Group = Asset->FindPhaseGroup(SelectedPhaseGroupName.ToString());
	if (!Group) return;

	if (Group->CustomSlots.Num() == 0)
	{
		PhaseGroupCustomSlotsBox->AddSlot()
		.AutoHeight()
		.Padding(0, 4)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("NoCustomSlots", "No custom slots yet. Click + Add Slot to create one."))
			.Font(FCoreStyle::GetDefaultFontStyle("Italic", 8))
			.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)))
			.AutoWrapText(true)
		];
		return;
	}

	for (int32 SlotIdx = 0; SlotIdx < Group->CustomSlots.Num(); ++SlotIdx)
	{
		const FString GroupNameStr = SelectedPhaseGroupName.ToString();
		const int32 CapturedSlotIdx = SlotIdx;
		const FString InitialSlotName = Group->CustomSlots[SlotIdx].SlotName;

		PhaseGroupCustomSlotsBox->AddSlot()
		.AutoHeight()
		.Padding(0, 2)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
			.Padding(FMargin(6, 4))
			[
				SNew(SVerticalBox)

				// Row 1: color swatch + editable slot name + delete button
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SHorizontalBox)

					// Color swatch (click to open color picker)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 6, 0)
					[
						SNew(SBox)
						.WidthOverride(16).HeightOverride(16)
						[
							SNew(SButton)
							.ButtonStyle(FAppStyle::Get(), "NoBorder")
							.ContentPadding(FMargin(0))
							.ToolTipText(LOCTEXT("CustomSlotColorTip", "Click to change the slot's display color"))
							.OnClicked_Lambda([this, GroupNameStr, CapturedSlotIdx]()
							{
								if (!Asset.IsValid()) return FReply::Handled();
								FPhaseGroup* G = Asset->FindPhaseGroupMutable(GroupNameStr);
								if (!G || !G->CustomSlots.IsValidIndex(CapturedSlotIdx)) return FReply::Handled();

								FColorPickerArgs PickerArgs;
								PickerArgs.bUseAlpha = false;
								PickerArgs.bOnlyRefreshOnOk = true;
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 2)
								PickerArgs.InitialColor = G->CustomSlots[CapturedSlotIdx].Color;
#endif
								PickerArgs.OnColorCommitted = FOnLinearColorValueChanged::CreateLambda(
									[this, GroupNameStr, CapturedSlotIdx](FLinearColor NewColor)
									{
										if (!Asset.IsValid()) return;
										FPhaseGroup* G2 = Asset->FindPhaseGroupMutable(GroupNameStr);
										if (!G2 || !G2->CustomSlots.IsValidIndex(CapturedSlotIdx)) return;
										BeginTransaction(LOCTEXT("SetCustomSlotColor", "Set Custom Slot Color"));
										G2->CustomSlots[CapturedSlotIdx].Color = NewColor;
										EndTransaction();
										if (FlipbookGroupsListBox.IsValid()) FlipbookGroupsListBox->Invalidate(EInvalidateWidgetReason::Paint);
									});
								OpenColorPicker(PickerArgs);
								return FReply::Handled();
							})
							[
								SNew(SColorBlock)
								.Color_Lambda([this, GroupNameStr, CapturedSlotIdx]() -> FLinearColor
								{
									if (!Asset.IsValid()) return FLinearColor::Black;
									const FPhaseGroup* G = Asset->FindPhaseGroup(GroupNameStr);
									if (!G || !G->CustomSlots.IsValidIndex(CapturedSlotIdx)) return FLinearColor::Black;
									return G->CustomSlots[CapturedSlotIdx].Color;
								})
								.Size(FVector2D(16, 16))
							]
						]
					]

					// Slot name (editable)
					+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
					[
						SNew(SEditableTextBox)
						.Text(FText::FromString(InitialSlotName))
						.ToolTipText(LOCTEXT("CustomSlotNameTip",
							"Unique name for this slot within the phase group. Used as the\n"
							"lookup key for HasPhaseGroupCustomSlot / GetPhaseGroupCustomFlipbook."))
						.OnTextCommitted_Lambda([this, GroupNameStr, CapturedSlotIdx](const FText& NewText, ETextCommit::Type CommitType)
						{
							if (CommitType == ETextCommit::OnCleared) return;
							if (!Asset.IsValid()) return;
							FPhaseGroup* G = Asset->FindPhaseGroupMutable(GroupNameStr);
							if (!G || !G->CustomSlots.IsValidIndex(CapturedSlotIdx)) return;
							const FString Trimmed = NewText.ToString().TrimStartAndEnd();
							if (Trimmed.IsEmpty()) return;
							// Uniqueness check within this group
							for (int32 i = 0; i < G->CustomSlots.Num(); ++i)
							{
								if (i != CapturedSlotIdx && G->CustomSlots[i].SlotName == Trimmed)
								{
									FNotificationInfo Info(LOCTEXT("CustomSlotNameDup", "A custom slot with that name already exists."));
									Info.ExpireDuration = 3.0f;
									FSlateNotificationManager::Get().AddNotification(Info);
									return;
								}
							}
							BeginTransaction(LOCTEXT("RenameCustomSlot", "Rename Custom Slot"));
							G->CustomSlots[CapturedSlotIdx].SlotName = Trimmed;
							EndTransaction();
							RefreshFlipbookGroupsPanel();
							RefreshPhaseGroupCustomSlotsList();
						})
					]

					// Delete button
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6, 0, 0, 0)
					[
						SNew(SButton)
						.ButtonStyle(FAppStyle::Get(), "SimpleButton")
						.ToolTipText(LOCTEXT("DeleteCustomSlotTip", "Delete this custom slot"))
						.OnClicked_Lambda([this, GroupNameStr, CapturedSlotIdx]()
						{
							if (!Asset.IsValid()) return FReply::Handled();
							FPhaseGroup* G = Asset->FindPhaseGroupMutable(GroupNameStr);
							if (!G || !G->CustomSlots.IsValidIndex(CapturedSlotIdx)) return FReply::Handled();
							BeginTransaction(LOCTEXT("DeleteCustomSlot", "Delete Custom Slot"));
							G->CustomSlots.RemoveAt(CapturedSlotIdx);
							EndTransaction();
							RefreshFlipbookGroupsPanel();
							RefreshPhaseGroupCustomSlotsList();
							return FReply::Handled();
						})
						[
							SNew(STextBlock)
							.Text(LOCTEXT("DeleteCustomSlotBtn", "x"))
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
							.ColorAndOpacity(FSlateColor(FLinearColor(0.75f, 0.35f, 0.35f)))
						]
					]
				]

				// Row 2: assigned flipbook name (read-only display)
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 2, 0, 0)
				[
					SNew(STextBlock)
					.Text_Lambda([this, GroupNameStr, CapturedSlotIdx]() -> FText
					{
						if (!Asset.IsValid()) return FText::GetEmpty();
						const FPhaseGroup* G = Asset->FindPhaseGroup(GroupNameStr);
						if (!G || !G->CustomSlots.IsValidIndex(CapturedSlotIdx)) return FText::GetEmpty();
						const FString& FBName = G->CustomSlots[CapturedSlotIdx].FlipbookName;
						return FBName.IsEmpty()
							? LOCTEXT("CustomSlotUnassigned", "Drag a flipbook onto this slot in the phase group")
							: FText::Format(LOCTEXT("CustomSlotAssigned", "→ {0}"), FText::FromString(FBName));
					})
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
					.ColorAndOpacity_Lambda([this, GroupNameStr, CapturedSlotIdx]() -> FSlateColor
					{
						if (!Asset.IsValid()) return FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f));
						const FPhaseGroup* G = Asset->FindPhaseGroup(GroupNameStr);
						if (!G || !G->CustomSlots.IsValidIndex(CapturedSlotIdx)) return FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f));
						return G->CustomSlots[CapturedSlotIdx].FlipbookName.IsEmpty()
							? FSlateColor(FLinearColor(0.5f, 0.45f, 0.35f))
							: FSlateColor(FLinearColor(0.45f, 0.75f, 0.45f));
					})
				]
			]
		];
	}
}

TSharedRef<SWidget> SCharacterProfileAssetEditor::BuildFlipbookGrid()
{
	return SAssignNew(OverviewFlipbookListBox, SVerticalBox);
}

bool SCharacterProfileAssetEditor::PassesOverviewFlipbookSearch(const FFlipbookProfileEntry& FlipbookData) const
{
	const FString Query = OverviewFlipbookSearchText.TrimStartAndEnd();
	if (Query.IsEmpty())
	{
		return true;
	}

	return FlipbookData.Identity.FlipbookName.Contains(Query, ESearchCase::IgnoreCase);
}

void SCharacterProfileAssetEditor::RefreshOverviewFlipbookList()
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
		const FFlipbookProfileEntry& FBData = Asset->Flipbooks[i];
		if (!PassesOverviewFlipbookSearch(FBData))
		{
			continue;
		}
		VisibleFlipbookCount++;
		bool bSelected = (i == SelectedFlipbookIndex);
		bool bHasFlipbook = !FBData.Identity.Flipbook.IsNull();

		int32 FlipbookIndex = i; // Capture for lambda

		// Load flipbook for thumbnail (animates on hover)
		UPaperFlipbook* LoadedFlipbookForThumb = bHasFlipbook ? FBData.Identity.Flipbook.LoadSynchronous() : nullptr;

		// Build frame sprites widget for expandable area
		TSharedRef<SHorizontalBox> FrameSpritesBox = SNew(SHorizontalBox);
		if (bHasFlipbook)
		{
			UPaperFlipbook* LoadedFlipbook = FBData.Identity.Flipbook.LoadSynchronous();
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
						.OnMouseButtonDown_Lambda([this, FlipbookIdx = i, FrameIdx, WeakSprite](const FGeometry&, const FPointerEvent& MouseEvent) -> FReply
						{
							if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
							{
								ShowSpriteContextMenu(WeakSprite.Get(), MouseEvent.GetScreenSpacePosition(), FlipbookIdx, FrameIdx);
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
									.OnClicked_Lambda([this, WeakSprite]()
									{
										OpenSpriteAssetEditor(WeakSprite.Get());
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
					MarkAllTabsDirty();
					ClearTabDirty(0);
					Invalidate(EInvalidateWidgetReason::Paint);
					return FReply::Handled();
				})
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
					.BorderBackgroundColor_Lambda([this, FlipbookIndex]() -> FSlateColor
					{
						return (FlipbookIndex == SelectedFlipbookIndex)
							? FLinearColor(0.2f, 0.4f, 0.8f, 0.3f)
							: FLinearColor(0.1f, 0.1f, 0.1f, 0.5f);
					})
					.Padding(4)
					.OnMouseButtonDown_Lambda([this, FlipbookIndex](const FGeometry&, const FPointerEvent& MouseEvent) -> FReply
					{
						if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
						{
							SelectedFlipbookIndex = FlipbookIndex;
							Invalidate(EInvalidateWidgetReason::Paint);
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
										.Text(LOCTEXT("NoFlipbookIcon", "No FB"))
										.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
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
							.Text(FText::FromString(FBData.Identity.FlipbookName))
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
								const FFlipbookProfileEntry& FBData = Asset->Flipbooks[FlipbookIndex];
								int32 Count = FBData.CombatData.Frames.Num();
								if (!FBData.Identity.Flipbook.IsNull())
								{
									if (UPaperFlipbook* FB = FBData.Identity.Flipbook.LoadSynchronous())
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
	TriggerPendingRenameIfNeeded(OverviewFlipbookNameTexts);

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
