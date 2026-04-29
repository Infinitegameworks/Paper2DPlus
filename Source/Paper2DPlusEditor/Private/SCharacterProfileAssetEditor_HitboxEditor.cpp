// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "CharacterProfileAssetEditor.h"
#include "EditorCanvasUtils.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SSeparator.h"
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
#include "Misc/MessageDialog.h"

// Prompt user: Merge (Yes), Replace (No), or Cancel. Returns EAppReturnType::YesNoCancel result.
// Output param bOutMerge is set to true for Yes, false for No, undefined on Cancel.
static EAppReturnType::Type PromptMergeOrReplace(bool& bOutMerge)
{
	const FText Title = NSLOCTEXT("HitboxBatch", "CopyFrameDataTitle", "Copy Frame Data");
	const FText Message = NSLOCTEXT("HitboxBatch", "CopyFrameDataMsg",
		"Merge source frame's hitboxes and sockets with existing data on target frames?\n\n"
		"Yes  = Merge (add to existing, preserve current data)\n"
		"No   = Replace (overwrite existing data)\n"
		"Cancel = Abort");
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 4
	EAppReturnType::Type Result = FMessageDialog::Open(EAppMsgType::YesNoCancel, Message);
#else
	EAppReturnType::Type Result = FMessageDialog::Open(EAppMsgType::YesNoCancel, Message, Title);
#endif
	bOutMerge = (Result == EAppReturnType::Yes);
	return Result;
}
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/SToolTip.h"
#include "UnrealClient.h"
#include "SceneView.h"
#include "CanvasItem.h"
#include "CanvasTypes.h"

/** Hitbox Editor tab — Per-frame hitbox/hurtbox/collision drawing, socket placement, 2D/3D visualization, and batch operations. */

#define LOCTEXT_NAMESPACE "CharacterProfileAssetEditor"

// ─────────────────────────────────────────────────────────────────────────────
// SFrameStripHitboxOverlay — paints translucent hitbox silhouettes on top of
// a frame strip sprite thumbnail. Used as the Overlay slot of a frame cell
// in the hitbox editor's frame list so the user can see at-a-glance where
// each frame's hitboxes sit without scrubbing to the frame.
//
// Coordinates: hitboxes are stored in sprite pixel space (X, Y, Width, Height
// relative to the sprite's top-left). The sprite thumbnail stretches to fill
// its cell, so we scale hitbox rects by (CellW/SpriteW, CellH/SpriteH) to
// land them in the right place on the thumbnail. If the sprite is missing
// or has zero size the overlay draws nothing.
//
// Respects VisibilityMask so "hide attack boxes" / "hide hurtboxes" in the
// toolbar also hides them on the frame strip. Everything is a semi-
// transparent filled rect — no outlines, keeps the thumbnail readable at
// the tiny frame-strip size.
// ─────────────────────────────────────────────────────────────────────────────
class SFrameStripHitboxOverlay : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SFrameStripHitboxOverlay) {}
		SLATE_ARGUMENT(TWeakObjectPtr<UPaperSprite>, Sprite)
		SLATE_ATTRIBUTE(EHitboxVisibility, VisibilityMask)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		Sprite = InArgs._Sprite;
		VisibilityMask = InArgs._VisibilityMask;
		SetCanTick(false);
	}

	/** Copy the frame's hitboxes into the widget — called once at build time.
	 *  We copy rather than hold a pointer because the frame list rebuilds
	 *  when the asset mutates, and the underlying FFrameHitboxData* would
	 *  dangle if the array reallocated. */
	void SetHitboxes(const TArray<FHitboxData>& InHitboxes)
	{
		Hitboxes = InHitboxes;
	}

	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(48, 48); }

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override
	{
		UPaperSprite* SpritePtr = Sprite.Get();
		if (!SpritePtr || Hitboxes.Num() == 0) return LayerId;

		const FVector2D SpriteSize = SpritePtr->GetSourceSize();
		if (SpriteSize.X <= 0.0f || SpriteSize.Y <= 0.0f) return LayerId;

		const FVector2D CellSize = AllottedGeometry.GetLocalSize();
		const FVector2D Scale(CellSize.X / SpriteSize.X, CellSize.Y / SpriteSize.Y);

		const EHitboxVisibility Mask = VisibilityMask.Get(EHitboxVisibility::All);
		const FSlateBrush* WhiteBrush = FAppStyle::GetBrush("WhiteBrush");

		for (const FHitboxData& HB : Hitboxes)
		{
			const EHitboxVisibility TypeBit = (HB.Type == EHitboxType::Attack) ? EHitboxVisibility::Attack
				: (HB.Type == EHitboxType::Hurtbox) ? EHitboxVisibility::Hurtbox : EHitboxVisibility::None;
			if (TypeBit == EHitboxVisibility::None || !EnumHasAnyFlags(Mask, TypeBit)) continue;

			// Skip degenerate boxes — would paint as invisible pixels anyway
			if (HB.Width <= 0 || HB.Height <= 0) continue;

			FLinearColor Color;
			switch (HB.Type)
			{
				case EHitboxType::Attack:  Color = FLinearColor(1.0f, 0.25f, 0.25f, 0.55f); break;
				case EHitboxType::Hurtbox: Color = FLinearColor(0.25f, 1.0f, 0.25f, 0.55f); break;
				default:                   Color = FLinearColor(1.0f, 1.0f, 1.0f, 0.40f); break;
			}

			const FVector2D Pos(HB.X * Scale.X, HB.Y * Scale.Y);
			const FVector2D Size(HB.Width * Scale.X, HB.Height * Scale.Y);

			FSlateDrawElement::MakeBox(
				OutDrawElements,
				LayerId,
				MakePaintGeometry(AllottedGeometry, Size, FSlateLayoutTransform(Pos)),
				WhiteBrush,
				ESlateDrawEffect::None,
				Color);
		}
		return LayerId + 1;
	}

private:
	TWeakObjectPtr<UPaperSprite> Sprite;
	TAttribute<EHitboxVisibility> VisibilityMask;
	TArray<FHitboxData> Hitboxes;
};

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
		.FillHeight(1.0f)
		.Padding(4)
		[
			SNew(SSplitter)
			.Orientation(Orient_Horizontal)

			+ SSplitter::Slot()
			.Value(0.22f)
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
					BuildFlipbookList()
				]
			]

			+ SSplitter::Slot()
			.Value(0.58f)
			[
				SNew(SVerticalBox)

				// Toolbar (moved here so left flipbook list stays top-flush)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					BuildToolbar()
				]

				// Canvas Area
				+ SVerticalBox::Slot()
				.FillHeight(1.0f)
				[
					BuildCanvasArea()
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					BuildFrameList()
				]
			]

			+ SSplitter::Slot()
			.Value(0.20f)
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
	auto BuildVisibilityCheckbox = [this](EHitboxVisibility Mask, EHitboxType Type, const FText& Label, const FLinearColor& Color) -> TSharedRef<SWidget>
	{
		return SNew(SCheckBox)
			.IsChecked_Lambda([this, Mask]() { return EnumHasAnyFlags(HitboxVisibilityMask, Mask) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
			.OnCheckStateChanged_Lambda([this, Mask, Type](ECheckBoxState State) {
				if (State == ECheckBoxState::Checked) { EnumAddFlags(HitboxVisibilityMask, Mask); }
				else                                  { EnumRemoveFlags(HitboxVisibilityMask, Mask); }
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
				SNew(SCheckBox)
				.ToolTipText(LOCTEXT("ShowHitboxesOnFrameStripTip",
					"Show translucent hitbox silhouettes over the frame strip thumbnails.\n"
					"Useful for seeing which frames have coverage without scrubbing.\n"
					"Respects the Attack/Hurtbox visibility toggles."))
				.IsChecked_Lambda([this]() { return bShowHitboxesOnFrameStrip ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this](ECheckBoxState NewState)
				{
					bShowHitboxesOnFrameStrip = (NewState == ECheckBoxState::Checked);
					if (FrameListBox.IsValid()) FrameListBox->Invalidate(EInvalidateWidgetReason::Paint);
				})
				[
					SNew(STextBlock)
					.Text(LOCTEXT("ShowHitboxesOnFrameStrip", "Hitbox Overlays"))
				]
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
				BuildVisibilityCheckbox(EHitboxVisibility::Attack, EHitboxType::Attack, LOCTEXT("ATKFilter", "ATK"), FLinearColor::Red)
			]

			+ SWrapBox::Slot()
			.Padding(2, 0)
			[
				BuildVisibilityCheckbox(EHitboxVisibility::Hurtbox, EHitboxType::Hurtbox, LOCTEXT("HRTFilter", "HRT"), FLinearColor::Green)
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
				.Text(LOCTEXT("DrawLabel", "Draw:"))
			]

			+ SWrapBox::Slot()
			.Padding(4, 0, 2, 0)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
				.ButtonColorAndOpacity_Lambda([this]() -> FLinearColor
				{
					return ActiveDrawType == EHitboxType::Attack
						? FLinearColor(0.7f, 0.12f, 0.12f, 1.0f)
						: FLinearColor(0.12f, 0.12f, 0.12f, 1.0f);
				})
				.ToolTipText(LOCTEXT("DrawATKTooltip", "Draw Attack hitboxes (1)"))
				.OnClicked_Lambda([this]()
				{
					ActiveDrawType = EHitboxType::Attack;
					EnumAddFlags(HitboxVisibilityMask, EHitboxVisibility::Attack);
					RefreshHitboxList();
					return FReply::Handled();
				})
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0, 0, 4, 0)
					[
						SNew(SBox)
						.WidthOverride(8.0f)
						.HeightOverride(8.0f)
						[
							SNew(SColorBlock).Color(FLinearColor::Red)
						]
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("DrawATK", "Attack"))
						.ColorAndOpacity_Lambda([this]() -> FSlateColor
						{
							return ActiveDrawType == EHitboxType::Attack
								? FSlateColor(FLinearColor::White)
								: FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f));
						})
					]
				]
			]

			+ SWrapBox::Slot()
			.Padding(2, 0)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
				.ButtonColorAndOpacity_Lambda([this]() -> FLinearColor
				{
					return ActiveDrawType == EHitboxType::Hurtbox
						? FLinearColor(0.1f, 0.55f, 0.1f, 1.0f)
						: FLinearColor(0.12f, 0.12f, 0.12f, 1.0f);
				})
				.ToolTipText(LOCTEXT("DrawHRTTooltip", "Draw Hurtbox hitboxes (2)"))
				.OnClicked_Lambda([this]()
				{
					ActiveDrawType = EHitboxType::Hurtbox;
					EnumAddFlags(HitboxVisibilityMask, EHitboxVisibility::Hurtbox);
					RefreshHitboxList();
					return FReply::Handled();
				})
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0, 0, 4, 0)
					[
						SNew(SBox)
						.WidthOverride(8.0f)
						.HeightOverride(8.0f)
						[
							SNew(SColorBlock).Color(FLinearColor::Green)
						]
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("DrawHRT", "Hurtbox"))
						.ColorAndOpacity_Lambda([this]() -> FSlateColor
						{
							return ActiveDrawType == EHitboxType::Hurtbox
								? FSlateColor(FLinearColor::White)
								: FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f));
						})
					]
				]
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
					.ToolTipText(LOCTEXT("HitboxToolTooltip", "Hitbox Tool (E)\nDrag on empty space to draw new hitboxes\nClick to select, WASD to nudge, double-click to edit"))
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
					.ToolTipText(LOCTEXT("SocketToolTooltip", "Socket Tool (Q)\nClick to place attachment points"))
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
		.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
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
			const FFlipbookProfileEntry& Anim = Asset->Flipbooks[i];
			const bool bIsSelected = (i == SelectedFlipbookIndex);
			UPaperFlipbook* LoadedFlipbook = !Anim.Identity.Flipbook.IsNull() ? Anim.Identity.Flipbook.LoadSynchronous() : nullptr;
			const bool bHasFlipbook = LoadedFlipbook != nullptr;
			const int32 FrameCount = bHasFlipbook ? LoadedFlipbook->GetNumKeyFrames() : Anim.CombatData.Frames.Num();
			const FText SourceNameText = FText::FromString(bHasFlipbook ? Anim.Identity.Flipbook.GetAssetName() : TEXT("No Flipbook Assigned"));

			// Count total hitboxes by type across all frames
			int32 TotalAttackCount = 0, TotalHurtCount = 0, TotalSocketCount = 0;
			for (const FFrameHitboxData& Frame : Anim.CombatData.Frames)
			{
				for (const FHitboxData& HB : Frame.Hitboxes)
				{
					if (HB.Type == EHitboxType::Attack) TotalAttackCount++;
					else if (HB.Type == EHitboxType::Hurtbox) TotalHurtCount++;
				}
				TotalSocketCount += Frame.Sockets.Num();
			}

			TSharedPtr<SInlineEditableTextBlock> NameText;

			TSharedRef<SWidget> Item = SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("NoBorder"))
				.Padding(2)
				.OnMouseButtonDown_Lambda([this, i](const FGeometry&, const FPointerEvent& MouseEvent) -> FReply
				{
					if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
					{
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
										.Text(FText::Format(LOCTEXT("HitboxFrameCountLabel", "{0} frames"), FText::AsNumber(FrameCount)))
										.Font(FAppStyle::GetFontStyle("SmallFont"))
										.ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f)))
									]
								]
								+ SVerticalBox::Slot()
								.AutoHeight()
								.Padding(0, 2, 0, 0)
								[
									SNew(SHorizontalBox)
									+ SHorizontalBox::Slot()
									.FillWidth(1.0f)
									.VAlign(VAlign_Center)
									[
										SNew(STextBlock)
										.Text(SourceNameText)
										.Font(FAppStyle::GetFontStyle("SmallFont"))
										.ColorAndOpacity(bHasFlipbook ? FLinearColor(0.4f, 0.8f, 0.4f) : FLinearColor(0.6f, 0.4f, 0.4f))
									]
									+ SHorizontalBox::Slot()
									.AutoWidth()
									.VAlign(VAlign_Center)
									.Padding(6, 0, 0, 0)
									[
										(TotalAttackCount > 0 || TotalHurtCount > 0 || TotalSocketCount > 0)
										? StaticCastSharedRef<SWidget>(
											SNew(SHorizontalBox)
											+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
											[
												TotalAttackCount > 0
												? StaticCastSharedRef<SWidget>(SNew(SHorizontalBox)
													+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
													[
														SNew(SBox).WidthOverride(6).HeightOverride(6)
														[ SNew(SImage).Image(FAppStyle::GetBrush("Icons.FilledCircle")).ColorAndOpacity(FLinearColor(0.95f, 0.30f, 0.30f)) ]
													]
													+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(1, 0, 0, 0)
													[
														SNew(STextBlock).Text(FText::AsNumber(TotalAttackCount)).Font(FCoreStyle::GetDefaultFontStyle("Bold", 7)).ColorAndOpacity(FSlateColor(FLinearColor(0.9f, 0.9f, 0.9f)))
													])
												: StaticCastSharedRef<SWidget>(SNullWidget::NullWidget)
											]
											+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
											[
												TotalHurtCount > 0
												? StaticCastSharedRef<SWidget>(SNew(SHorizontalBox)
													+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
													[
														SNew(SBox).WidthOverride(6).HeightOverride(6)
														[ SNew(SImage).Image(FAppStyle::GetBrush("Icons.FilledCircle")).ColorAndOpacity(FLinearColor(0.30f, 0.90f, 0.30f)) ]
													]
													+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(1, 0, 0, 0)
													[
														SNew(STextBlock).Text(FText::AsNumber(TotalHurtCount)).Font(FCoreStyle::GetDefaultFontStyle("Bold", 7)).ColorAndOpacity(FSlateColor(FLinearColor(0.9f, 0.9f, 0.9f)))
													])
												: StaticCastSharedRef<SWidget>(SNullWidget::NullWidget)
											]
											+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
											[
												TotalSocketCount > 0
												? StaticCastSharedRef<SWidget>(SNew(SHorizontalBox)
													+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
													[
														SNew(SBox).WidthOverride(6).HeightOverride(6)
														[ SNew(SImage).Image(FAppStyle::GetBrush("Icons.FilledCircle")).ColorAndOpacity(FLinearColor(0.95f, 0.85f, 0.30f)) ]
													]
													+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(1, 0, 0, 0)
													[
														SNew(STextBlock).Text(FText::AsNumber(TotalSocketCount)).Font(FCoreStyle::GetDefaultFontStyle("Bold", 7)).ColorAndOpacity(FSlateColor(FLinearColor(0.9f, 0.9f, 0.9f)))
													])
												: StaticCastSharedRef<SWidget>(SNullWidget::NullWidget)
											]
										)
										: StaticCastSharedRef<SWidget>(SNullWidget::NullWidget)
									]
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
		.MaxHeight(80.0f)
		[
			SNew(SScrollBox)
			.Orientation(Orient_Horizontal)
			+ SScrollBox::Slot()
			[
				FrameListBox.ToSharedRef()
			]
		];
}

void SCharacterProfileAssetEditor::RefreshFrameList()
{
	if (!FrameListBox.IsValid()) return;

	FrameListBox->ClearChildren();

	const FFlipbookProfileEntry* Anim = GetCurrentFlipbookData();
	if (!Anim) return;

	// Get the flipbook to extract frame sprites
	UPaperFlipbook* Flipbook = nullptr;
	if (!Anim->Identity.Flipbook.IsNull())
	{
		Flipbook = Anim->Identity.Flipbook.LoadSynchronous();
	}

	const int32 FrameCount = FMath::Min(GetCurrentFrameCount(), Anim->CombatData.Frames.Num());
	for (int32 i = 0; i < FrameCount; i++)
	{
		const FFrameHitboxData& Frame = Anim->CombatData.Frames[i];

		int32 AttackCount = 0, HurtCount = 0;
		for (const FHitboxData& HB : Frame.Hitboxes)
		{
			if (HB.Type == EHitboxType::Attack) AttackCount++;
			else if (HB.Type == EHitboxType::Hurtbox) HurtCount++;
		}
		const int32 SocketCount = Frame.Sockets.Num();
		const bool bInvulnerable = Frame.bInvulnerable;

		UPaperSprite* FrameSprite = nullptr;
		if (Flipbook && i < Flipbook->GetNumKeyFrames())
		{
			FrameSprite = Flipbook->GetKeyFrameChecked(i).Sprite;
		}

		// Build hitbox count badges — colored dot + count number
		TSharedRef<SHorizontalBox> BadgeRow = SNew(SHorizontalBox);

		auto AddBadge = [&](int32 Count, FLinearColor DotColor)
		{
			if (Count <= 0) return;
			BadgeRow->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 3, 0)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SBox).WidthOverride(7).HeightOverride(7)
					[
						SNew(SImage)
						.Image(FAppStyle::GetBrush("Icons.FilledCircle"))
						.ColorAndOpacity(DotColor)
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(1, 0, 0, 0)
				[
					SNew(STextBlock)
					.Text(FText::AsNumber(Count))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 7))
					.ColorAndOpacity(FSlateColor(FLinearColor(0.95f, 0.95f, 0.95f)))
				]
			];
		};

		AddBadge(AttackCount, FLinearColor(0.95f, 0.30f, 0.30f));
		AddBadge(HurtCount, FLinearColor(0.30f, 0.90f, 0.30f));
		AddBadge(SocketCount, FLinearColor(0.95f, 0.85f, 0.30f));

		// Hitbox silhouette painter lives in SpriteOverlay (sprite-aligned
		// 48x48 geometry) so the scale math in SFrameStripHitboxOverlay::OnPaint
		// maps hitbox pixel coords to the exact sprite area, not the whole cell.
		TSharedRef<SFrameStripHitboxOverlay> HitboxPainter = SNew(SFrameStripHitboxOverlay)
			.Sprite(FrameSprite)
			.VisibilityMask_Lambda([this]() { return HitboxVisibilityMask; });
		HitboxPainter->SetHitboxes(Frame.Hitboxes);

		TSharedRef<SWidget> HitboxSpriteOverlay = SNew(SBox)
			.Visibility_Lambda([this]()
			{
				return bShowHitboxesOnFrameStrip ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
			})
			[
				HitboxPainter
			];

		// I-frame toggle button — appears on hover or when invulnerable (top-right)
		TSharedRef<TSharedPtr<SBox>> IFrameHoverRef = MakeShared<TSharedPtr<SBox>>();
		TSharedRef<TWeakPtr<SWidget>> CellHoverRef = MakeShared<TWeakPtr<SWidget>>();

		TSharedRef<SWidget> InvulOverlay = SNew(SOverlay)
			// Blue tint when invulnerable
			+ SOverlay::Slot()
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
				.Padding(0)
				.BorderBackgroundColor(FLinearColor(0.15f, 0.35f, 0.85f, 0.25f))
				.Visibility_Lambda([this, i]() -> EVisibility
				{
					const FFrameHitboxData* F = GetCurrentFrame(i);
					return (F && F->bInvulnerable) ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
				})
			]
			// "I" toggle button (top-right)
			+ SOverlay::Slot()
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Top)
			.Padding(FMargin(0, 2, 2, 0))
			[
				SAssignNew(*IFrameHoverRef, SBox)
				.Visibility_Lambda([this, i, CellHoverRef]() -> EVisibility
				{
					const FFrameHitboxData* F = GetCurrentFrame(i);
					const bool bInvuln = F && F->bInvulnerable;
					const bool bSel = (i == SelectedFrameIndex);
					TSharedPtr<SWidget> PinnedCell = CellHoverRef->Pin();
					const bool bCellHovered = PinnedCell.IsValid() && PinnedCell->IsHovered();
					return (bInvuln || bSel || bCellHovered) ? EVisibility::Visible : EVisibility::Collapsed;
				})
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "NoBorder")
					.ContentPadding(FMargin(0))
					.ToolTipText(LOCTEXT("ToggleIFrameTooltip", "Toggle invulnerable frame (i-frame)"))
					.OnClicked_Lambda([this, i]() -> FReply
					{
						if (FFrameHitboxData* F = GetCurrentFrameMutable(i))
						{
							BeginTransaction(LOCTEXT("ToggleInvulnerable", "Toggle Invulnerable Frame"));
							F->bInvulnerable = !F->bInvulnerable;
							EndTransaction();
							if (FrameListBox.IsValid()) FrameListBox->Invalidate(EInvalidateWidgetReason::Paint);
						}
						return FReply::Handled();
					})
					[
						SNew(SBorder)
						.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
						.BorderBackgroundColor_Lambda([IFrameHoverRef, this, i]() -> FSlateColor
						{
							const FFrameHitboxData* F = GetCurrentFrame(i);
							const bool bInvuln = F && F->bInvulnerable;
							const bool bBtnHovered = IFrameHoverRef->IsValid() && (*IFrameHoverRef)->IsHovered();
							if (bInvuln)
								return bBtnHovered ? FLinearColor(0.3f, 0.5f, 0.9f, 1.0f) : FLinearColor(0.2f, 0.4f, 0.8f, 1.0f);
							else
								return bBtnHovered ? FLinearColor(0.4f, 0.4f, 0.5f, 1.0f) : FLinearColor(0.25f, 0.25f, 0.3f, 1.0f);
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
								.Text(LOCTEXT("IFrameGlyph", "I"))
								.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
								.ColorAndOpacity(FSlateColor(FLinearColor::White))
								.Justification(ETextJustify::Center)
							]
						]
					]
				]
			];

		FFrameStripCellArgs CellArgs;
		CellArgs.Sprite = FrameSprite;
		CellArgs.FrameIndex = i;
		CellArgs.IsSelected = [this, i]() { return i == SelectedFrameIndex; };
		CellArgs.IsMultiSelected = [this, i]() { return SelectedFrames.Contains(i); };
		CellArgs.OnMouseButtonDown = [this, i, FrameSprite](const FPointerEvent& MouseEvent) -> FReply
		{
			if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
			{
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
		};
		CellArgs.BelowLabelContent = BadgeRow;
		CellArgs.SpriteOverlay = HitboxSpriteOverlay;
		CellArgs.Overlay = InvulOverlay;

		TSharedRef<SWidget> CellWidget = FFrameStripCellUtils::Build(CellArgs);
		*CellHoverRef = CellWidget;

		FrameListBox->AddSlot()
		.AutoWidth()
		.Padding(0, 0, 4, 0)
		[
			CellWidget
		];
	}
}

TSharedRef<SWidget> SCharacterProfileAssetEditor::BuildCanvasArea()
{
	TSharedRef<SVerticalBox> CanvasArea = SNew(SVerticalBox)
		// Current flipbook header
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4, 2, 4, 0)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text_Lambda([this]() {
					const FFlipbookProfileEntry* Anim = GetCurrentFlipbookData();
					if (!Anim) return FText::FromString(TEXT("No Flipbook"));
					int32 FrameCount = GetCurrentFrameCount();
					return FText::Format(LOCTEXT("FlipbookTitleFmt", "{0}  Frame {1}/{2}"),
						FText::FromString(Anim->Identity.FlipbookName),
						FText::AsNumber(SelectedFrameIndex + 1),
						FText::AsNumber(FrameCount));
				})
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f)))
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
				.Zoom_Lambda([this]() { return ZoomLevel; })
				.VisibilityMask_Lambda([this]() { return HitboxVisibilityMask; })
				.ActiveDrawType_Lambda([this]() { return ActiveDrawType; })
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

		FString TypeStr = HB.Type == EHitboxType::Attack ? TEXT("ATK") : TEXT("HRT");
		FLinearColor TypeColor = HB.Type == EHitboxType::Attack ? FLinearColor::Red : FLinearColor::Green;

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
								RefreshPropertiesPanel();
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
								RefreshPropertiesPanel();
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

		// Damage, Knockback, and batch apply — only for attack hitboxes
		if (Frame->Hitboxes.IsValidIndex(SelIndex) && Frame->Hitboxes[SelIndex].Type == EHitboxType::Attack)
		{
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
			PropertiesBox->AddSlot()
			.AutoHeight()
			.Padding(4, 4, 4, 2)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0, 0, 4, 0)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("ApplyDmgKBTo", "Apply to:"))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
					.ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f)))
				]
				+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(2, 0)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
					.Text(LOCTEXT("ApplyToFrame", "Frame"))
					.ToolTipText(LOCTEXT("ApplyDmgFrameTip", "Apply this hitbox's damage and knockback to all attack hitboxes in the current frame."))
					.OnClicked_Lambda([this, SelIndex]()
					{
						FFrameHitboxData* F = GetCurrentFrameMutable();
						if (!F || !F->Hitboxes.IsValidIndex(SelIndex)) return FReply::Handled();
						const int32 Dmg = F->Hitboxes[SelIndex].Damage;
						const int32 KB = F->Hitboxes[SelIndex].Knockback;
						BeginTransaction(LOCTEXT("BatchDmgFrame", "Apply Damage to Frame"));
						for (FHitboxData& HB : F->Hitboxes)
						{
							if (HB.Type == EHitboxType::Attack) { HB.Damage = Dmg; HB.Knockback = KB; }
						}
						EndTransaction();
						RefreshPropertiesPanel();
						return FReply::Handled();
					})
				]
				+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(2, 0)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
					.Text(LOCTEXT("ApplyToAllFrames", "All"))
					.ToolTipText(LOCTEXT("ApplyDmgAllTip", "Apply this hitbox's damage and knockback to all attack hitboxes across every frame."))
					.OnClicked_Lambda([this, SelIndex]()
					{
						FFrameHitboxData* F = GetCurrentFrameMutable();
						FFlipbookProfileEntry* Anim = GetCurrentFlipbookDataMutable();
						if (!F || !Anim || !F->Hitboxes.IsValidIndex(SelIndex)) return FReply::Handled();
						const int32 Dmg = F->Hitboxes[SelIndex].Damage;
						const int32 KB = F->Hitboxes[SelIndex].Knockback;
						BeginTransaction(LOCTEXT("BatchDmgAll", "Apply Damage to All Frames"));
						for (FFrameHitboxData& FrameData : Anim->CombatData.Frames)
						{
							for (FHitboxData& HB : FrameData.Hitboxes)
							{
								if (HB.Type == EHitboxType::Attack) { HB.Damage = Dmg; HB.Knockback = KB; }
							}
						}
						EndTransaction();
						RefreshPropertiesPanel();
						return FReply::Handled();
					})
				]
			];
		}
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
	// Source options for the combo box
	TSharedPtr<TArray<TSharedPtr<FString>>> SourceOptions = MakeShared<TArray<TSharedPtr<FString>>>();
	SourceOptions->Add(MakeShared<FString>(TEXT("Current Frame")));
	SourceOptions->Add(MakeShared<FString>(TEXT("Previous Frame")));

	// Target options for the combo box
	TSharedPtr<TArray<TSharedPtr<FString>>> TargetOptions = MakeShared<TArray<TSharedPtr<FString>>>();
	TargetOptions->Add(MakeShared<FString>(TEXT("All Frames")));
	TargetOptions->Add(MakeShared<FString>(TEXT("Selected Frames")));
	TargetOptions->Add(MakeShared<FString>(TEXT("Remaining Frames")));

	auto MakeCombo = [](TSharedPtr<TArray<TSharedPtr<FString>>> Options, int32* SelectedIdx) -> TSharedRef<SWidget>
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

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4, 4, 4, 4)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("CopyOperations", "Frame Operations"))
			.Font(FAppStyle::GetFontStyle("BoldFont"))
		]

		// Sentence builder: [Source ▼] → [Target ▼]  [Apply]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4, 2)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0, 0, 4, 0)
			[
				SNew(STextBlock).Text(LOCTEXT("CopyFromLabel", "From")).Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.Padding(0, 0, 4, 0)
			[
				MakeCombo(SourceOptions, &CopySourceIndex)
			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4, 2)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0, 0, 4, 0)
			[
				SNew(STextBlock).Text(LOCTEXT("CopyToLabel", "To")).Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.Padding(0, 0, 4, 0)
			[
				MakeCombo(TargetOptions, &CopyTargetIndex)
			]
		]

		// Merge checkbox + Apply button
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4, 2)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0, 0, 8, 0)
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([this]() { return bCopyMerge ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this](ECheckBoxState State) { bCopyMerge = (State == ECheckBoxState::Checked); })
				.ToolTipText(LOCTEXT("MergeTip", "Merge: add hitboxes to existing ones instead of replacing"))
				[
					SNew(STextBlock).Text(LOCTEXT("MergeLabel", "Merge")).Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
				]
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
				.Text(LOCTEXT("ApplyCopy", "Apply"))
				.IsEnabled_Lambda([this]()
				{
					// Disable when "Selected Frames" target but no frames selected
					return CopyTargetIndex != 1 || SelectedFrames.Num() > 0;
				})
				.OnClicked_Lambda([this]()
				{
					FFlipbookProfileEntry* Anim = GetCurrentFlipbookDataMutable();
					if (!Anim || !Asset.IsValid()) return FReply::Handled();

					// Determine source frame index
					int32 SourceFrame = SelectedFrameIndex;
					if (CopySourceIndex == 1) // Previous Frame
					{
						SourceFrame = SelectedFrameIndex - 1;
						if (SourceFrame < 0) return FReply::Handled();
					}

					BeginTransaction(LOCTEXT("CopyFrameOp", "Copy Frame Hitboxes"));

					const int32 FrameCount = Anim->CombatData.Frames.Num();
					if (CopyTargetIndex == 0) // All Frames
					{
						Asset->CopyFrameDataToRange(Anim->Identity.FlipbookName, SourceFrame, 0, FrameCount - 1, true, bCopyMerge);
					}
					else if (CopyTargetIndex == 1) // Selected Frames
					{
						ForEachSelectedFrame([&](int32 TargetIdx)
						{
							if (TargetIdx != SourceFrame)
							{
								Asset->CopyFrameDataToRange(Anim->Identity.FlipbookName, SourceFrame, TargetIdx, TargetIdx, true, bCopyMerge);
							}
						});
					}
					else if (CopyTargetIndex == 2) // Remaining Frames
					{
						if (SourceFrame + 1 < FrameCount)
						{
							Asset->CopyFrameDataToRange(Anim->Identity.FlipbookName, SourceFrame, SourceFrame + 1, FrameCount - 1, true, bCopyMerge);
						}
					}

					EndTransaction();
					RefreshFrameList();
					RefreshHitboxList();
					RefreshPropertiesPanel();
					return FReply::Handled();
				})
			]
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4, 6, 4, 2)
		[
			SNew(SSeparator)
		]

		// Clear buttons
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4, 2)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.Padding(0, 0, 2, 0)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
				.Text(LOCTEXT("ClearFrameShort", "Clear Frame"))
				.ToolTipText(LOCTEXT("ClearFrameTooltip", "Remove all hitboxes and sockets from this frame"))
				.OnClicked_Lambda([this]() { OnClearCurrentFrame(); return FReply::Handled(); })
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.Padding(2, 0, 0, 0)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
				.Text(LOCTEXT("ClearSelectedShort", "Clear Selected"))
				.IsEnabled_Lambda([this]() { return SelectedFrames.Num() > 0; })
				.ToolTipText(LOCTEXT("ClearSelectedTooltip", "Remove all hitboxes and sockets from the selected frames"))
				.OnClicked_Lambda([this]()
				{
					FFlipbookProfileEntry* Anim = GetCurrentFlipbookDataMutable();
					if (!Anim) return FReply::Handled();
					BeginTransaction(LOCTEXT("ClearSelected", "Clear Selected Frames"));
					ForEachSelectedFrame([&](int32 Idx)
					{
						if (Anim->CombatData.Frames.IsValidIndex(Idx))
						{
							Anim->CombatData.Frames[Idx].Hitboxes.Empty();
							Anim->CombatData.Frames[Idx].Sockets.Empty();
						}
					});
					EndTransaction();
					RefreshFrameList();
					RefreshHitboxList();
					RefreshPropertiesPanel();
					return FReply::Handled();
				})
			]
		];
}

void SCharacterProfileAssetEditor::OnCopyFromPrevious()
{
	if (SelectedFrameIndex <= 0 || !Asset.IsValid()) return;

	FFlipbookProfileEntry* Anim = GetCurrentFlipbookDataMutable();
	if (!Anim) return;

	if (!Anim->CombatData.Frames.IsValidIndex(SelectedFrameIndex) || !Anim->CombatData.Frames.IsValidIndex(SelectedFrameIndex - 1)) return;

	BeginTransaction(LOCTEXT("CopyFromPrev", "Copy from Previous Frame"));
	const bool bCopied = Asset->CopyFrameDataToRange(
		Anim->Identity.FlipbookName,
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

	FFlipbookProfileEntry* Anim = GetCurrentFlipbookDataMutable();
	if (!Anim) return;

	if (SelectedFrameIndex < 0 || SelectedFrameIndex >= Anim->CombatData.Frames.Num()) return;
	if (Anim->CombatData.Frames.Num() <= 1) return;

	bool bMerge = false;
	if (PromptMergeOrReplace(bMerge) == EAppReturnType::Cancel) return;

	BeginTransaction(LOCTEXT("PropagateAll", "Propagate All to Group"));
	const bool bCopied = Asset->CopyFrameDataToRange(
		Anim->Identity.FlipbookName,
		SelectedFrameIndex,
		0,
		Anim->CombatData.Frames.Num() - 1,
		true,
		bMerge
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

	FFlipbookProfileEntry* Anim = GetCurrentFlipbookDataMutable();
	if (!Anim) return;

	FFrameHitboxData* CurrentFrame = GetCurrentFrameMutable();
	if (!CurrentFrame) return;

	EHitboxSelectionType SelType = EditorCanvas->GetSelectionType();
	int32 SelIndex = EditorCanvas->GetPrimarySelectedIndex();

	if (SelType == EHitboxSelectionType::None) return;

	UPaperFlipbook* Flipbook = !Anim->Identity.Flipbook.IsNull() ? Anim->Identity.Flipbook.LoadSynchronous() : nullptr;

	BeginTransaction(LOCTEXT("PropagateSelected", "Propagate Selected to Group"));

	if (SelType == EHitboxSelectionType::Hitbox && CurrentFrame->Hitboxes.IsValidIndex(SelIndex))
	{
		const FHitboxData& SelectedHitbox = CurrentFrame->Hitboxes[SelIndex];
		for (int32 i = 0; i < Anim->CombatData.Frames.Num(); i++)
		{
			if (i != SelectedFrameIndex)
			{
				if (Anim->CombatData.Frames[i].Hitboxes.IsValidIndex(SelIndex))
				{
					FHitboxData& TargetHitbox = Anim->CombatData.Frames[i].Hitboxes[SelIndex];
					TargetHitbox = SelectedHitbox;
					ClampHitboxForFrame(TargetHitbox, Flipbook, i);
				}
				else
				{
					const int32 NewHitboxIndex = Anim->CombatData.Frames[i].Hitboxes.Add(SelectedHitbox);
					ClampHitboxForFrame(Anim->CombatData.Frames[i].Hitboxes[NewHitboxIndex], Flipbook, i);
				}
			}
		}
	}
	else if (SelType == EHitboxSelectionType::Socket && CurrentFrame->Sockets.IsValidIndex(SelIndex))
	{
		const FSocketData& SelectedSocket = CurrentFrame->Sockets[SelIndex];
		for (int32 i = 0; i < Anim->CombatData.Frames.Num(); i++)
		{
			if (i != SelectedFrameIndex)
			{
				bool bFound = false;
				for (FSocketData& Sock : Anim->CombatData.Frames[i].Sockets)
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
					Anim->CombatData.Frames[i].Sockets.Add(SelectedSocket);
				}
			}
		}
	}

	EndTransaction();
}

void SCharacterProfileAssetEditor::OnCopyToNextFrames()
{
	if (!Asset.IsValid()) return;

	FFlipbookProfileEntry* Anim = GetCurrentFlipbookDataMutable();
	if (!Anim || Anim->CombatData.Frames.Num() <= 1) return;
	if (!Anim->CombatData.Frames.IsValidIndex(SelectedFrameIndex)) return;
	if (SelectedFrameIndex >= Anim->CombatData.Frames.Num() - 1) return;

	bool bMerge = false;
	if (PromptMergeOrReplace(bMerge) == EAppReturnType::Cancel) return;

	BeginTransaction(LOCTEXT("CopyToNextFrames", "Copy Frame Data to Next Frames"));
	const bool bCopied = Asset->CopyFrameDataToRange(
		Anim->Identity.FlipbookName,
		SelectedFrameIndex,
		SelectedFrameIndex + 1,
		Anim->CombatData.Frames.Num() - 1,
		true,
		bMerge
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

	FFlipbookProfileEntry* Anim = GetCurrentFlipbookDataMutable();
	if (!Anim || Anim->CombatData.Frames.Num() <= 0)
	{
		return;
	}

	UPaperFlipbook* Flipbook = !Anim->Identity.Flipbook.IsNull() ? Anim->Identity.Flipbook.LoadSynchronous() : nullptr;
	if (!Flipbook)
	{
		return;
	}

	int32 NeedingClampCount = 0;
	for (int32 FrameIndex = 0; FrameIndex < Anim->CombatData.Frames.Num(); ++FrameIndex)
	{
		NeedingClampCount += CountFrameHitboxesNeedingClamp(Anim->CombatData.Frames[FrameIndex], Flipbook, FrameIndex);
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
	for (int32 FrameIndex = 0; FrameIndex < Anim->CombatData.Frames.Num(); ++FrameIndex)
	{
		ClampedCount += ClampFrameHitboxesToBounds(Anim->CombatData.Frames[FrameIndex], Flipbook, FrameIndex);
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
	for (FFlipbookProfileEntry& Anim : Asset->Flipbooks)
	{
		UPaperFlipbook* Flipbook = !Anim.Identity.Flipbook.IsNull() ? Anim.Identity.Flipbook.LoadSynchronous() : nullptr;
		if (!Flipbook)
		{
			continue;
		}

		for (int32 FrameIndex = 0; FrameIndex < Anim.CombatData.Frames.Num(); ++FrameIndex)
		{
			NeedingClampCount += CountFrameHitboxesNeedingClamp(Anim.CombatData.Frames[FrameIndex], Flipbook, FrameIndex);
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
	for (FFlipbookProfileEntry& Anim : Asset->Flipbooks)
	{
		UPaperFlipbook* Flipbook = !Anim.Identity.Flipbook.IsNull() ? Anim.Identity.Flipbook.LoadSynchronous() : nullptr;
		if (!Flipbook)
		{
			continue;
		}

		for (int32 FrameIndex = 0; FrameIndex < Anim.CombatData.Frames.Num(); ++FrameIndex)
		{
			ClampedCount += ClampFrameHitboxesToBounds(Anim.CombatData.Frames[FrameIndex], Flipbook, FrameIndex);
		}
	}

	EndTransaction();

	RefreshFrameList();
	RefreshHitboxList();
	RefreshPropertiesPanel();
	RefreshOverviewFlipbookList();
	RefreshSpriteEditorFlipbookList();
	RefreshSpriteEditorFrameList();

	FNotificationInfo Info(FText::Format(
		LOCTEXT("ClampAllDone", "Clamped {0} hitbox(es) across all flipbooks."),
		FText::AsNumber(ClampedCount)));
	Info.ExpireDuration = 3.0f;
	FSlateNotificationManager::Get().AddNotification(Info);
}

void SCharacterProfileAssetEditor::OnMirrorAllFrames()
{
	if (!Asset.IsValid()) return;

	FFlipbookProfileEntry* Anim = GetCurrentFlipbookDataMutable();
	if (!Anim || Anim->CombatData.Frames.Num() == 0) return;

	UPaperFlipbook* FB = nullptr;
	if (!Anim->Identity.Flipbook.IsNull())
	{
		FB = Anim->Identity.Flipbook.LoadSynchronous();
	}

	BeginTransaction(LOCTEXT("MirrorAllFrames", "Mirror Hitboxes Across All Frames"));
	int32 Mirrored = 0;
	for (int32 i = 0; i < Anim->CombatData.Frames.Num(); ++i)
	{
		int32 SpriteWidth = 0;
		if (FB && i < FB->GetNumKeyFrames())
		{
			if (UPaperSprite* Spr = FB->GetKeyFrameChecked(i).Sprite)
			{
				SpriteWidth = FMath::RoundToInt(Spr->GetSourceSize().X);
			}
		}
		Mirrored += Asset->MirrorHitboxesInRange(Anim->Identity.FlipbookName, i, i, SpriteWidth / 2);
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
	NewHitbox.Type = ActiveDrawType;
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
