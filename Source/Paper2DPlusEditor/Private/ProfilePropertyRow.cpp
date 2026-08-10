// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "ProfilePropertyRow.h"

#include "Misc/EngineVersionComparison.h"
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
#include "EditorStyleSet.h"
#define FAppStyle FEditorStyle
#else
#include "Styling/AppStyle.h"
#endif
#include "Styling/CoreStyle.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	// Stock Details grid line reads as a near-black 1px seam on the panel background.
	const FLinearColor PropertyRowGridLineColor(0.0f, 0.0f, 0.0f, 0.35f);
	// Whisper of white on hover — enough to read as a row, not enough to shout.
	const FLinearColor PropertyRowHoverColor(1.0f, 1.0f, 1.0f, 0.04f);
	// Keep both columns usable however far the shared divider is dragged.
	constexpr float PropertyRowMinNameFraction = 0.15f;
	constexpr float PropertyRowMaxNameFraction = 0.85f;
}

TSharedRef<FProfilePropertyRowColumns> FProfilePropertyRowUtils::GetDefaultColumns()
{
	static TSharedRef<FProfilePropertyRowColumns> DefaultColumns = MakeShared<FProfilePropertyRowColumns>();
	return DefaultColumns;
}

TSharedRef<SWidget> FProfilePropertyRowUtils::MakeRow(
	const FText& Label,
	TSharedRef<SWidget> ValueWidget,
	const FText& Tooltip,
	float MinValueWidth,
	float MaxValueWidth,
	TSharedPtr<FProfilePropertyRowColumns> Columns)
{
	return MakeCustomRow(
		SNew(STextBlock)
		.Text(Label)
		.Font(GetPropertyFont())
		.ColorAndOpacity(GetLabelColor()),
		ValueWidget,
		Tooltip,
		MinValueWidth,
		MaxValueWidth,
		Columns);
}

TSharedRef<SWidget> FProfilePropertyRowUtils::MakeCustomRow(
	TSharedRef<SWidget> LabelWidget,
	TSharedRef<SWidget> ValueWidget,
	const FText& Tooltip,
	float MinValueWidth,
	float MaxValueWidth,
	TSharedPtr<FProfilePropertyRowColumns> Columns)
{
	const TSharedPtr<FProfilePropertyRowColumns> ColumnState =
		Columns.IsValid() ? Columns : TSharedPtr<FProfilePropertyRowColumns>(GetDefaultColumns());

	// Default (0) fills the value column — the shared divider is the width control, as in stock
	// Details. A positive cap still bounds a single widget for callers that want one.
	TSharedRef<SWidget> ValueContent = ValueWidget;
	// MaxDesiredWidth clamps the DESIRED size only. The value slot below stretches its child, so a
	// capped widget also has to stop the slot from filling (see bCapValueWidth) or the cap is silently
	// inert — which it was until a caller first passed one.
	const bool bCapValueWidth = MaxValueWidth > 0.0f;
	if (bCapValueWidth)
	{
		ValueContent = SNew(SBox)
			.MinDesiredWidth(FMath::Min(MinValueWidth, MaxValueWidth))
			.MaxDesiredWidth(MaxValueWidth)
			.HAlign(HAlign_Fill)
			[
				ValueWidget
			];
	}
	else if (MinValueWidth > 0.0f)
	{
		ValueContent = SNew(SBox)
			.MinDesiredWidth(MinValueWidth)
			[
				ValueWidget
			];
	}

	// The stock details-row anatomy: each row is its own splitter, every row binds the SAME
	// column state, so the 1px divider lines up into a column and dragging it on any row
	// resizes them all together (SDetailSingleItemRow + FDetailColumnSizeData, reimplemented
	// over a plain shared struct).
	TSharedRef<SBorder> RowBorder = SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("WhiteBrush"))
		.Padding(FMargin(0))
		[
			SNew(SSplitter)
			.Style(FAppStyle::Get(), "DetailsView.Splitter")
			.PhysicalSplitterHandleSize(1.0f)
			.HitDetectionSplitterHandleSize(5.0f)

			+ SSplitter::Slot()
			.Value(TAttribute<float>::CreateLambda([ColumnState]() { return ColumnState->NameColumnFraction; }))
			.OnSlotResized(SSplitter::FOnSlotResized::CreateLambda([ColumnState](float NewFraction)
			{
				ColumnState->NameColumnFraction =
					FMath::Clamp(NewFraction, PropertyRowMinNameFraction, PropertyRowMaxNameFraction);
			}))
			[
				SNew(SBox)
				.VAlign(VAlign_Center)
				.MinDesiredHeight(22.0f)
				.Padding(FMargin(8, 3, 4, 3))
				[
					LabelWidget
				]
			]

			+ SSplitter::Slot()
			.Value(TAttribute<float>::CreateLambda([ColumnState]() { return 1.0f - ColumnState->NameColumnFraction; }))
			.OnSlotResized(SSplitter::FOnSlotResized::CreateLambda([ColumnState](float NewFraction)
			{
				ColumnState->NameColumnFraction =
					FMath::Clamp(1.0f - NewFraction, PropertyRowMinNameFraction, PropertyRowMaxNameFraction);
			}))
			[
				SNew(SBox)
				.VAlign(VAlign_Center)
				// Left-align ONLY when a cap was asked for; uncapped rows keep the stock fill so every
				// existing call site renders byte-identically.
				.HAlign(bCapValueWidth ? HAlign_Left : HAlign_Fill)
				.MinDesiredHeight(22.0f)
				.Padding(FMargin(6, 3, 8, 3))
				[
					ValueContent
				]
			]
		];

	// Hover tint has to see the finished widget, so the attribute binds after construction.
	TWeakPtr<SBorder> WeakRow = RowBorder;
	RowBorder->SetBorderBackgroundColor(TAttribute<FSlateColor>::CreateLambda([WeakRow]() -> FSlateColor
	{
		const TSharedPtr<SBorder> Pinned = WeakRow.Pin();
		return (Pinned.IsValid() && Pinned->IsHovered())
			? FSlateColor(PropertyRowHoverColor)
			: FSlateColor(FLinearColor::Transparent);
	}));
	if (!Tooltip.IsEmpty())
	{
		RowBorder->SetToolTipText(Tooltip);
	}

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			RowBorder
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SBox)
			.HeightOverride(1.0f)
			[
				SNew(SImage)
				.Image(FAppStyle::Get().GetBrush("WhiteBrush"))
				.ColorAndOpacity(PropertyRowGridLineColor)
			]
		];
}

TSharedRef<SWidget> FProfilePropertyRowUtils::MakeFactRow(
	const FText& Label,
	TAttribute<FText> Value,
	const FText& Tooltip,
	TAttribute<EVisibility> RowVisibility)
{
	TSharedRef<STextBlock> ValueText = SNew(STextBlock)
		.Font(GetPropertyFont())
		.ColorAndOpacity(GetFactValueColor())
		.AutoWrapText(true);
	ValueText->SetText(MoveTemp(Value));

	TSharedRef<SWidget> Row = MakeRow(Label, ValueText, Tooltip);
	if (!RowVisibility.IsSet() && !RowVisibility.IsBound())
	{
		return Row;
	}

	// Wrap so the row's 1px grid line hides with it — a collapsed value would otherwise leave
	// an orphan separator behind.
	return SNew(SBox)
		.Visibility(MoveTemp(RowVisibility))
		[
			Row
		];
}

TSharedRef<SWidget> FProfilePropertyRowUtils::MakeSectionTitle(
	TAttribute<FText> Title,
	TAttribute<EVisibility> Visibility)
{
	TSharedRef<STextBlock> TitleText = SNew(STextBlock)
		.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.BoldFont")));
	TitleText->SetText(MoveTemp(Title));

	TSharedRef<SWidget> Content = SNew(SBox)
		.Padding(FMargin(8, 6, 8, 3))
		[
			TitleText
		];
	if (!Visibility.IsSet() && !Visibility.IsBound())
	{
		return Content;
	}
	return SNew(SBox).Visibility(MoveTemp(Visibility))[ Content ];
}

TSharedRef<SWidget> FProfilePropertyRowUtils::MakeSectionHint(
	TAttribute<FText> Hint,
	TAttribute<EVisibility> Visibility)
{
	TSharedRef<STextBlock> HintText = SNew(STextBlock)
		.Font(GetPropertyFont())
		.ColorAndOpacity(FSlateColor::UseSubduedForeground())
		.AutoWrapText(true);
	HintText->SetText(MoveTemp(Hint));

	TSharedRef<SWidget> Content = SNew(SBox)
		.Padding(FMargin(8, 2, 8, 5))
		[
			HintText
		];
	if (!Visibility.IsSet() && !Visibility.IsBound())
	{
		return Content;
	}
	return SNew(SBox).Visibility(MoveTemp(Visibility))[ Content ];
}

TSharedRef<SWidget> FProfilePropertyRowUtils::MakeOnionChannelRow(
	const FProfileOnionChannelArgs& Channel,
	TSharedPtr<FProfilePropertyRowColumns> Columns)
{
	// Name column: the enable toggle plus the channel name in its canvas color, so the control
	// reads the same as the overlay it drives.
	TSharedRef<SHorizontalBox> LabelContent = SNew(SHorizontalBox);
	LabelContent->AddSlot()
	.AutoWidth()
	.VAlign(VAlign_Center)
	.Padding(0, 0, 5, 0)
	[
		SNew(SCheckBox)
		.IsChecked(Channel.IsChecked)
		.OnCheckStateChanged_Lambda([Handler = Channel.OnCheckStateChanged](ECheckBoxState NewState)
		{
			if (Handler) { Handler(NewState); }
		})
	];
	LabelContent->AddSlot()
	.FillWidth(1.0f)
	.VAlign(VAlign_Center)
	[
		SNew(STextBlock)
		.Text(Channel.Label)
		.Font(GetPropertyFont())
		.ColorAndOpacity(FSlateColor(Channel.LabelColor))
	];

	// Value column: opacity slider, plus the frame stepper for channels that span frames.
	const TAttribute<bool> IsEnabled = TAttribute<bool>::CreateLambda(
		[Checked = Channel.IsChecked]()
		{
			return Checked.Get(ECheckBoxState::Unchecked) == ECheckBoxState::Checked;
		});

	TSharedRef<SHorizontalBox> ValueContent = SNew(SHorizontalBox);
	ValueContent->AddSlot()
	.FillWidth(1.0f)
	.VAlign(VAlign_Center)
	.Padding(0, 0, 6, 0)
	[
		SNew(SSlider)
		.MinValue(Channel.MinOpacity)
		.MaxValue(Channel.MaxOpacity)
		.Value(Channel.Opacity)
		.IsEnabled(IsEnabled)
		.OnValueChanged_Lambda([Handler = Channel.OnOpacityChanged](float NewValue)
		{
			if (Handler) { Handler(NewValue); }
		})
	];
	if (Channel.bShowFrameCount)
	{
		ValueContent->AddSlot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(SBox)
			.WidthOverride(44.0f)
			[
				SNew(SSpinBox<int32>)
				.MinValue(Channel.MinFrameCount)
				.MaxValue(Channel.MaxFrameCount)
				.Value(Channel.FrameCount)
				.IsEnabled(IsEnabled)
				.Font(GetPropertyFont())
				.OnValueChanged_Lambda([Handler = Channel.OnFrameCountChanged](int32 NewValue)
				{
					if (Handler) { Handler(NewValue); }
				})
			]
		];
	}

	return MakeCustomRow(
		LabelContent,
		ValueContent,
		Channel.Tooltip,
		/*MinValueWidth*/ 0.0f,
		/*MaxValueWidth*/ 0.0f,
		Columns);
}

TSharedRef<SWidget> FProfilePropertyRowUtils::MakeStringCombo(
	TArray<TSharedPtr<FString>>* Options,
	int32* SelectedIndex,
	TFunction<void(int32)> OnChanged)
{
	check(Options && SelectedIndex);
	const TSharedPtr<FString> InitialItem =
		Options->IsValidIndex(*SelectedIndex) ? (*Options)[*SelectedIndex] : nullptr;

	return SNew(SComboBox<TSharedPtr<FString>>)
		.OptionsSource(Options)
		.InitiallySelectedItem(InitialItem)
		.OnSelectionChanged_Lambda([Options, SelectedIndex, OnChanged](TSharedPtr<FString> Item, ESelectInfo::Type)
		{
			if (Item.IsValid())
			{
				*SelectedIndex = Options->IndexOfByKey(Item);
				if (OnChanged)
				{
					OnChanged(*SelectedIndex);
				}
			}
		})
		.OnGenerateWidget_Lambda([](TSharedPtr<FString> Item) -> TSharedRef<SWidget>
		{
			return SNew(STextBlock)
				.Text(FText::FromString(*Item))
				.Font(GetPropertyFont());
		})
		[
			SNew(STextBlock)
			.Text_Lambda([Options, SelectedIndex]() -> FText
			{
				return Options->IsValidIndex(*SelectedIndex)
					? FText::FromString(*(*Options)[*SelectedIndex]) : FText();
			})
			.Font(GetPropertyFont())
		];
}

FSlateFontInfo FProfilePropertyRowUtils::GetPropertyFont()
{
	// The engine's own Details rows use PropertyWindow.NormalFont; matching it is most of
	// what makes a hand-rolled row read as native.
	return FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont"));
}

FSlateColor FProfilePropertyRowUtils::GetLabelColor()
{
	return FSlateColor(FLinearColor(0.72f, 0.72f, 0.72f));
}

FSlateColor FProfilePropertyRowUtils::GetFactValueColor()
{
	// Derived, non-authored values sit a step back from editable ones without becoming the
	// near-invisible grey the hand-rolled panels used.
	return FSlateColor(FLinearColor(0.62f, 0.62f, 0.65f));
}
