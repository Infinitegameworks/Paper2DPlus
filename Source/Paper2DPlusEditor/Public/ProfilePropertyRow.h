// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"
#include "Styling/SlateTypes.h"
#include "Widgets/SWidget.h"
#include "Templates/Function.h"

/**
 * FProfilePropertyRowUtils — the ONE shared "details-style" row for every hand-rolled editor
 * side panel (Hitbox properties/frame operations, Frame Timing batch tools, Sprite and Root
 * Motion batch panels, and future tool panels).
 *
 * Hand-rolled `AutoWidth label + FillWidth(1.0) value` boxes read as foreign next to a real
 * Details panel for three reasons this helper fixes:
 *   1. No shared name/value column — every value widget starts wherever its label ends, so
 *      nothing lines up vertically. Rows here are per-row splitters over ONE shared column
 *      state (the stock SDetailSingleItemRow/FDetailColumnSizeData recipe), so the 1px
 *      vertical divider aligns across rows and dragging it on any row resizes the whole
 *      column together.
 *   2. No grid rowing — stock property rows draw a 1px line under each row plus a hover tint,
 *      which visually separates lines. Rows here do the same.
 *   3. Full-bleed value widgets — FillWidth(1.0) stretches a combo holding the word "None"
 *      across the whole dock. Values here fill only the value column, whose width the user
 *      controls with the shared divider.
 *
 * New panels should build label+value rows through MakeRow (and string dropdowns through
 * MakeStringCombo) instead of hand-rolling the layout, so every detail surface in the plugin
 * stays visually consistent from one call site.
 */

/** Shared draggable name/value split. Rows created with the same state resize together, exactly
 *  like stock Details rows sharing one FDetailColumnSizeData. MakeRow defaults to one
 *  process-wide state so every hand-rolled panel stays column-aligned; a panel that wants an
 *  independent split can pass its own instance. */
struct FProfilePropertyRowColumns
{
	float NameColumnFraction = 0.42f;
};

/**
 * One onion/reference channel in the shared "Onion & Skins" control (Sprite and Root Motion).
 * Both panels stack these rows vertically so the two tools read identically; the checkbox and
 * colored channel name live in the name column and the opacity slider plus optional frame
 * stepper fill the value column, so the channels line up with every other property row.
 */
struct FProfileOnionChannelArgs
{
	FText Label;
	FLinearColor LabelColor = FLinearColor::White;
	FText Tooltip;

	TAttribute<ECheckBoxState> IsChecked;
	TFunction<void(ECheckBoxState)> OnCheckStateChanged;

	TAttribute<float> Opacity;
	TFunction<void(float)> OnOpacityChanged;
	float MinOpacity = 0.1f;
	float MaxOpacity = 0.8f;

	/** The Reference channel pins a single sprite, so it has no frame count. */
	bool bShowFrameCount = true;
	TAttribute<int32> FrameCount;
	TFunction<void(int32)> OnFrameCountChanged;
	int32 MinFrameCount = 1;
	int32 MaxFrameCount = 3;
};

class FProfilePropertyRowUtils
{
public:
	/** Default minimum width for single value widgets (combos, spinboxes, text boxes). */
	static constexpr float DefaultMinValueWidth = 110.0f;

	/** The process-wide shared column split used when MakeRow receives no explicit state. */
	static TSharedRef<FProfilePropertyRowColumns> GetDefaultColumns();

	/**
	 * One name/value property row: a draggable shared column split with a 1px vertical grid
	 * line (stock "DetailsView.Splitter" style), standard property font, 1px bottom grid line,
	 * and a subtle hover tint. The value widget fills the value column; pass MaxValueWidth > 0
	 * to additionally cap a single widget's growth, or leave 0 for stock fill behavior
	 * (compound content — button pairs, multi-spinbox groups — should stay at 0).
	 *
	 * A capped value is LEFT-ALIGNED in its column rather than stretched — capping a widget that
	 * still fills is a contradiction, and short numeric editors are the reason to cap at all.
	 */
	static TSharedRef<SWidget> MakeRow(
		const FText& Label,
		TSharedRef<SWidget> ValueWidget,
		const FText& Tooltip = FText::GetEmpty(),
		float MinValueWidth = DefaultMinValueWidth,
		float MaxValueWidth = 0.0f,
		TSharedPtr<FProfilePropertyRowColumns> Columns = nullptr);

	/**
	 * The standard string dropdown for property rows: property font in the button and the
	 * menu rows, sized by MakeRow's value column rather than FillWidth. Options/SelectedIndex
	 * follow the established panel pattern — both point at panel members so contextual
	 * close/reopen never leaves the combo reading a temporary (see CLAUDE.md Frame Operations
	 * note). OnChanged is optional; the selected index is always written first.
	 */
	/**
	 * MakeRow's widget-label sibling, for rows whose name column needs more than text (a
	 * checkbox plus a colored channel name, an icon, a chip). Same shared column, grid line,
	 * and hover tint.
	 */
	static TSharedRef<SWidget> MakeCustomRow(
		TSharedRef<SWidget> LabelWidget,
		TSharedRef<SWidget> ValueWidget,
		const FText& Tooltip = FText::GetEmpty(),
		float MinValueWidth = DefaultMinValueWidth,
		float MaxValueWidth = 0.0f,
		TSharedPtr<FProfilePropertyRowColumns> Columns = nullptr);

	/**
	 * A read-only "exposed fact" row — derived data a designer reads but never authors here
	 * (frame counts, hitbox tallies, resolved source assets). It uses the SAME column grid as
	 * editable rows so a mixed panel still reads as one table; only the value's color marks it
	 * as non-authored. Pass RowVisibility for facts that collapse when they do not apply, so
	 * the row's grid line hides with it.
	 */
	static TSharedRef<SWidget> MakeFactRow(
		const FText& Label,
		TAttribute<FText> Value,
		const FText& Tooltip = FText::GetEmpty(),
		TAttribute<EVisibility> RowVisibility = TAttribute<EVisibility>());

	/**
	 * A sub-heading inside a Details category, for panels that group several row runs under one
	 * category. Replaces ad-hoc bold text inside a dark "shadow box" border.
	 */
	static TSharedRef<SWidget> MakeSectionTitle(
		TAttribute<FText> Title,
		TAttribute<EVisibility> Visibility = TAttribute<EVisibility>());

	/**
	 * Explanatory copy under a section title (the subdued wrapped sentence stock Details uses
	 * for category help), at the shared property font rather than a hand-picked small size.
	 */
	static TSharedRef<SWidget> MakeSectionHint(
		TAttribute<FText> Hint,
		TAttribute<EVisibility> Visibility = TAttribute<EVisibility>());

	/** One channel row of the shared Onion & Skins control. See FProfileOnionChannelArgs. */
	static TSharedRef<SWidget> MakeOnionChannelRow(
		const FProfileOnionChannelArgs& Channel,
		TSharedPtr<FProfilePropertyRowColumns> Columns = nullptr);

	static TSharedRef<SWidget> MakeStringCombo(
		TArray<TSharedPtr<FString>>* Options,
		int32* SelectedIndex,
		TFunction<void(int32)> OnChanged = nullptr);

	/** The one property font for hand-rolled rows and their dropdown contents. */
	static FSlateFontInfo GetPropertyFont();

	/** Label color matching stock Details name-column text. */
	static FSlateColor GetLabelColor();

	/** Value color for read-only derived facts (MakeFactRow). */
	static FSlateColor GetFactValueColor();
};
