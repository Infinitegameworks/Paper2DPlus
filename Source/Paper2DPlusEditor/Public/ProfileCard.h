// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

/**
 * Shared visual shell for compact Paper2DPlus profile cards.
 *
 * Callers retain ownership of domain content, interaction, and selection authority. This widget
 * owns the Character Profile card's rounded background, one-pixel selection accent, and clipping,
 * and exposes its canonical compact geometry so profile surfaces cannot drift visually.
 */
class PAPER2DPLUSEDITOR_API SProfileCard : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SProfileCard)
		: _IsSelected(false)
		, _ContentPadding(FMargin(4.0f))
	{}
		SLATE_ATTRIBUTE(bool, IsSelected)
		SLATE_ARGUMENT(FMargin, ContentPadding)
		SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Canonical Character Profile card colors, exposed for focused style-contract automation. */
	static FLinearColor GetSelectionBorderColor(bool bIsSelected);
	static FLinearColor GetCardBackgroundColor(bool bIsSelected);

	/** Canonical compact card geometry shared by Character, Catalog, Effect, and Combat surfaces. */
	static float GetCanonicalCardWidth();
	static float GetCanonicalCompactCardHeight();
	static float GetCanonicalThumbnailSize();

private:
	FSlateColor ResolveSelectionBorderColor() const;
	FSlateColor ResolveCardBackgroundColor() const;

	TAttribute<bool> IsSelected;
};
