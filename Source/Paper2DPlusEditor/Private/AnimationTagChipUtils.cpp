// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "AnimationTagChipUtils.h"

#include "AnimationMapCore.h" // GetAnimationTagChipColor — the R11 shared color seam
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateBrush.h"
#include "Styling/SlateColor.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "AnimationTagChipUtils"

namespace
{
	FText AnimTagChips_ProvenanceText(Paper2DPlusAnimationTagChips::EAnimationTagChipProvenance Provenance)
	{
		switch (Provenance)
		{
			case Paper2DPlusAnimationTagChips::EAnimationTagChipProvenance::Own:
				return LOCTEXT("ProvenanceOwn", "own tag (authored on this animation)");
			case Paper2DPlusAnimationTagChips::EAnimationTagChipProvenance::ChainInherited:
				return LOCTEXT("ProvenanceChain", "inherited from a chain root that reaches this animation");
			case Paper2DPlusAnimationTagChips::EAnimationTagChipProvenance::GroupImplied:
			default:
				return LOCTEXT("ProvenanceGroup", "implied by this animation's tag-mapping group");
		}
	}

	// Rounded pill brushes (the engine gameplay-tag chip radius, GameplayTagStyle.cpp — the browser
	// card chip precedent). White fill, tinted per-chip by BorderBackgroundColor.
	const FSlateBrush* AnimTagChips_PillBrush()
	{
		static const FSlateRoundedBoxBrush Brush(FLinearColor::White, 3.0f);
		return &Brush;
	}
}

namespace Paper2DPlusAnimationTagChips
{
	TArray<FAnimationTagChipItem> BuildChipItems(const FGameplayTagContainer& OwnTags,
		const FGameplayTagContainer& ChainInheritedTags, const FGameplayTagContainer& GroupImpliedTags)
	{
		TArray<FAnimationTagChipItem> Items;
		TSet<FName> SeenTagNames;
		auto Append = [&Items, &SeenTagNames](const FGameplayTagContainer& Tags, EAnimationTagChipProvenance Provenance)
		{
			for (auto It = Tags.CreateConstIterator(); It; ++It)
			{
				if (!It->IsValid() || SeenTagNames.Contains(It->GetTagName()))
				{
					continue; // strongest provenance wins: own was appended first, then chain, then group
				}
				SeenTagNames.Add(It->GetTagName());
				FAnimationTagChipItem Item;
				Item.Tag = *It;
				Item.Provenance = Provenance;
				Items.Add(MoveTemp(Item));
			}
		};
		Append(OwnTags, EAnimationTagChipProvenance::Own);
		Append(ChainInheritedTags, EAnimationTagChipProvenance::ChainInherited);
		Append(GroupImpliedTags, EAnimationTagChipProvenance::GroupImplied);
		return Items;
	}

	TSharedRef<SWidget> MakeTagChip(const FAnimationTagChipItem& Item)
	{
		const Paper2DPlusAnimationMap::FTagChipColor ChipColor =
			Paper2DPlusAnimationMap::GetAnimationTagChipColor(Item.Tag);
		const FText LeafText = FText::FromString(GetTagLeafString(Item.Tag)); // the shared cross-version leaf helper (this header)
		const FText ChipTooltip = FText::Format(LOCTEXT("ChipTooltipFmt", "{0}\n{1}"),
			FText::FromName(Item.Tag.GetTagName()), AnimTagChips_ProvenanceText(Item.Provenance));

		switch (Item.Provenance)
		{
			case EAnimationTagChipProvenance::Own:
				// SOLID: the registry/convention color IS the pill; contrast text on top.
				return SNew(SBorder)
					.BorderImage(AnimTagChips_PillBrush())
					.BorderBackgroundColor(FSlateColor(FLinearColor(ChipColor.Color.R, ChipColor.Color.G, ChipColor.Color.B, 0.90f)))
					.Padding(FMargin(5.f, 1.f))
					.VAlign(VAlign_Center)
					.ToolTipText(ChipTooltip)
					.Clipping(EWidgetClipping::ClipToBounds)
					[
						SNew(STextBlock)
						.Text(LeafText)
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 7))
						.ColorAndOpacity(FSlateColor(ChipColor.TextColor))
					];

			case EAnimationTagChipProvenance::ChainInherited:
				// GHOSTED: same hue at a fraction of the fill opacity + muted text — reads as
				// "present but not authored here".
				return SNew(SBorder)
					.BorderImage(AnimTagChips_PillBrush())
					.BorderBackgroundColor(FSlateColor(FLinearColor(ChipColor.Color.R, ChipColor.Color.G, ChipColor.Color.B, 0.28f)))
					.Padding(FMargin(5.f, 1.f))
					.VAlign(VAlign_Center)
					.ToolTipText(ChipTooltip)
					.Clipping(EWidgetClipping::ClipToBounds)
					[
						SNew(STextBlock)
						.Text(LeafText)
						.Font(FCoreStyle::GetDefaultFontStyle("Italic", 7))
						.ColorAndOpacity(FSlateColor(FLinearColor(0.80f, 0.80f, 0.80f, 0.85f)))
					];

			case EAnimationTagChipProvenance::GroupImplied:
			default:
			{
				// OUTLINED: a colored 1px ring (an outer pill tinted the tag color) around a dark inner
				// pill — nested pills instead of an FSlateRoundedBoxBrush outline because the widget
				// tint (BorderBackgroundColor) only reaches the FILL, not the brush's baked outline.
				const FLinearColor RingText = FMath::Lerp(ChipColor.Color, FLinearColor::White, 0.35f);
				return SNew(SBorder)
					.BorderImage(AnimTagChips_PillBrush())
					.BorderBackgroundColor(FSlateColor(FLinearColor(ChipColor.Color.R, ChipColor.Color.G, ChipColor.Color.B, 0.95f)))
					.Padding(FMargin(1.f))
					.VAlign(VAlign_Center)
					.ToolTipText(ChipTooltip)
					.Clipping(EWidgetClipping::ClipToBounds)
					[
						SNew(SBorder)
						.BorderImage(AnimTagChips_PillBrush())
						.BorderBackgroundColor(FSlateColor(FLinearColor(0.09f, 0.09f, 0.10f, 1.0f)))
						.Padding(FMargin(4.f, 0.f))
						.VAlign(VAlign_Center)
						[
							SNew(STextBlock)
							.Text(LeafText)
							.Font(FCoreStyle::GetDefaultFontStyle("Regular", 7))
							.ColorAndOpacity(FSlateColor(RingText))
						]
					];
			}
		}
	}

	TSharedRef<SWidget> MakeChipsRow(const TArray<FAnimationTagChipItem>& Items, int32 MaxVisible)
	{
		if (Items.Num() == 0)
		{
			return SNullWidget::NullWidget;
		}

		const int32 VisibleCount = (Items.Num() > MaxVisible) ? MaxVisible : Items.Num();
		TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
		for (int32 Index = 0; Index < VisibleCount; ++Index)
		{
			Row->AddSlot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.f, 0.f, 2.f, 0.f)
			[
				MakeTagChip(Items[Index])
			];
		}

		const int32 OverflowCount = Items.Num() - VisibleCount;
		if (OverflowCount > 0)
		{
			// "+N" overflow chip: tooltip lists every collapsed tag's FULL path + provenance.
			FString OverflowTooltip;
			for (int32 Index = VisibleCount; Index < Items.Num(); ++Index)
			{
				if (!OverflowTooltip.IsEmpty())
				{
					OverflowTooltip += LINE_TERMINATOR;
				}
				OverflowTooltip += FString::Printf(TEXT("%s — %s"),
					*Items[Index].Tag.GetTagName().ToString(),
					*AnimTagChips_ProvenanceText(Items[Index].Provenance).ToString());
			}

			Row->AddSlot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SBorder)
				.BorderImage(AnimTagChips_PillBrush())
				.BorderBackgroundColor(FSlateColor(FLinearColor(0.20f, 0.20f, 0.22f, 1.0f)))
				.Padding(FMargin(4.f, 1.f))
				.VAlign(VAlign_Center)
				.ToolTipText(FText::FromString(OverflowTooltip))
				[
					SNew(STextBlock)
					.Text(FText::Format(LOCTEXT("OverflowChipFmt", "+{0}"), FText::AsNumber(OverflowCount)))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 7))
					.ColorAndOpacity(FSlateColor(FLinearColor(0.75f, 0.75f, 0.78f)))
				]
			];
		}

		return Row;
	}
}

#undef LOCTEXT_NAMESPACE
