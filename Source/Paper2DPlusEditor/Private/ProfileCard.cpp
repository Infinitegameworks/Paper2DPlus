// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "ProfileCard.h"

#include "Brushes/SlateRoundedBoxBrush.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"

namespace
{
	const FSlateRoundedBoxBrush& GetCardBrush()
	{
		static const FSlateRoundedBoxBrush Brush(FLinearColor::White, 8.0f);
		return Brush;
	}

	const FSlateRoundedBoxBrush& GetSelectionBrush()
	{
		static const FSlateRoundedBoxBrush Brush(FLinearColor::White, 9.0f);
		return Brush;
	}
}

void SProfileCard::Construct(const FArguments& InArgs)
{
	IsSelected = InArgs._IsSelected;

	ChildSlot
	[
		SNew(SBox)
		.Clipping(EWidgetClipping::ClipToBounds)
		[
			SNew(SBorder)
			.BorderImage(&GetSelectionBrush())
			.BorderBackgroundColor(this, &SProfileCard::ResolveSelectionBorderColor)
			.Padding(1.0f)
			[
				SNew(SBorder)
				.BorderImage(&GetCardBrush())
				.BorderBackgroundColor(this, &SProfileCard::ResolveCardBackgroundColor)
				.Padding(InArgs._ContentPadding)
				[
					InArgs._Content.Widget
				]
			]
		]
	];
}

FLinearColor SProfileCard::GetSelectionBorderColor(bool bIsSelected)
{
	return bIsSelected
		? FLinearColor(0.28f, 0.44f, 0.68f, 1.0f)
		: FLinearColor::Transparent;
}

FLinearColor SProfileCard::GetCardBackgroundColor(bool bIsSelected)
{
	return bIsSelected
		? FLinearColor(0.18f, 0.30f, 0.50f, 1.0f)
		: FLinearColor(0.22f, 0.22f, 0.24f, 1.0f);
}

float SProfileCard::GetCanonicalCardWidth()
{
	return 150.0f;
}

float SProfileCard::GetCanonicalCompactCardHeight()
{
	return 142.0f;
}

float SProfileCard::GetCanonicalThumbnailSize()
{
	return 64.0f;
}

FSlateColor SProfileCard::ResolveSelectionBorderColor() const
{
	return GetSelectionBorderColor(IsSelected.Get(false));
}

FSlateColor SProfileCard::ResolveCardBackgroundColor() const
{
	return GetCardBackgroundColor(IsSelected.Get(false));
}
