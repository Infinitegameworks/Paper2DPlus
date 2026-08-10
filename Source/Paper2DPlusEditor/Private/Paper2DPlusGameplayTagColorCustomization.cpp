// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "Paper2DPlusGameplayTagColorCustomization.h"
#include "AnimationTagChipUtils.h" // SMenuHostedTagPickerGuard - menu-hosted pickers are selection-only

// The whole customization is 5.3+ (SGameplayTagPicker is not public before 5.3) — the header gates the
// class; this gates the translation unit body so pre-5.3 engines compile the file to nothing.
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 3)

#include "Paper2DPlusSettings.h"
#include "DetailWidgetRow.h"
#include "GameplayTagsManager.h"
#include "PropertyHandle.h"
#include "SGameplayTagPicker.h"
#include "Styling/AppStyle.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "Paper2DPlusGameplayTagColor"

TSharedRef<IPropertyTypeCustomization> FPaper2DPlusGameplayTagColorCustomization::MakeInstance()
{
	return MakeShared<FPaper2DPlusGameplayTagColorCustomization>();
}

void FPaper2DPlusGameplayTagColorCustomization::CustomizeHeader(
	TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils&)
{
	StructPropertyHandle = PropertyHandle;

	// Borderless combo so the COLORED fill IS the whole pill (a default SComboButton frame is grey and
	// would dominate, making the tint read as "unchanged"). The down-arrow keeps it obviously a picker.
	// Uncolored tags fall back to a neutral grey fill (stay grey). Rounded pill (radius 3) matches the
	// engine gameplay-tag chip look (GameplayTagStyle.cpp); White fill is tinted by BorderBackgroundColor.
	static const FSlateRoundedBoxBrush TagChipBrush(FLinearColor::White, 3.0f);
	TSharedRef<SComboButton> Combo = SNew(SComboButton)
		.ComboButtonStyle(&FAppStyle::Get().GetWidgetStyle<FComboButtonStyle>("SimpleComboButton"))
		.ContentPadding(FMargin(2.f, 0.f))
		.HasDownArrow(true)
		.OnGetMenuContent(this, &FPaper2DPlusGameplayTagColorCustomization::OnGetMenuContent)
		.ButtonContent()
		[
			SNew(SBorder)
			.BorderImage(&TagChipBrush)
			.BorderBackgroundColor(TAttribute<FSlateColor>::CreateSP(this, &FPaper2DPlusGameplayTagColorCustomization::GetChipBorderColor))
			.Padding(FMargin(10.f, 3.f))
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(TAttribute<FText>::CreateSP(this, &FPaper2DPlusGameplayTagColorCustomization::GetTagText))
				.ColorAndOpacity(TAttribute<FSlateColor>::CreateSP(this, &FPaper2DPlusGameplayTagColorCustomization::GetChipTextColor))
				.Font(FAppStyle::GetFontStyle("PropertyWindow.NormalFont"))
			]
		];

	ComboButton = Combo;

	HeaderRow
		.NameContent()
		[
			PropertyHandle->CreatePropertyNameWidget()
		]
		.ValueContent()
		.MinDesiredWidth(200.f)
		.MaxDesiredWidth(0.f)
		[
			Combo
		];
}

FGameplayTag FPaper2DPlusGameplayTagColorCustomization::GetCurrentTag() const
{
	FGameplayTag Result;
	if (StructPropertyHandle.IsValid())
	{
		StructPropertyHandle->EnumerateConstRawData(
			[&Result](const void* RawData, const int32 /*DataIndex*/, const int32 /*NumDatas*/) -> bool
			{
				if (RawData)
				{
					Result = *static_cast<const FGameplayTag*>(RawData);
				}
				return false; // first object only — the chip reflects the primary value on multi-edit
			});
	}
	return Result;
}

FText FPaper2DPlusGameplayTagColorCustomization::GetTagText() const
{
	const FGameplayTag Tag = GetCurrentTag();
	if (!Tag.IsValid())
	{
		return LOCTEXT("NoneTag", "None");
	}
	return FText::FromName(Tag.GetTagName());
}

FSlateColor FPaper2DPlusGameplayTagColorCustomization::GetChipBorderColor() const
{
	bool bFound = false;
	const FLinearColor Color = UPaper2DPlusSettings::ResolveTagColor(GetCurrentTag(), bFound);
	// Unset tags get a neutral dark chip so an absent color doesn't masquerade as one.
	return bFound ? FSlateColor(Color) : FSlateColor(FLinearColor(0.12f, 0.12f, 0.12f, 1.0f));
}

FSlateColor FPaper2DPlusGameplayTagColorCustomization::GetChipTextColor() const
{
	bool bFound = false;
	const FLinearColor Color = UPaper2DPlusSettings::ResolveTagColor(GetCurrentTag(), bFound);
	if (!bFound)
	{
		return FSlateColor::UseForeground();
	}
	// Luminance-contrasting fill (Rec. 709) so the tag name stays legible on the colored chip.
	const float Luminance = 0.2126f * Color.R + 0.7152f * Color.G + 0.0722f * Color.B;
	return FSlateColor(Luminance > 0.5f
		? FLinearColor(0.05f, 0.05f, 0.05f)
		: FLinearColor(0.97f, 0.97f, 0.97f));
}

TSharedRef<SWidget> FPaper2DPlusGameplayTagColorCustomization::OnGetMenuContent()
{
	if (!StructPropertyHandle.IsValid())
	{
		return SNullWidget::NullWidget;
	}

	// Honor the Categories filter the same way the ENGINE widget does — the manager helper walks the
	// property, its outer/parent handles, owner-function metadata, and tag-typed struct fields, where a
	// bare PropertyHandle->GetMetaData("Categories") reads only the property's own row and silently
	// UNFILTERS pickers whose Categories come from a parent handle or function param (review finding).
	const FString Filter = UGameplayTagsManager::Get().GetCategoriesMetaFromPropertyHandle(StructPropertyHandle);

	TWeakPtr<SComboButton> WeakCombo = ComboButton;

	return SNew(SBox)
		.MinDesiredWidth(300.f)
		.Padding(2.f)
		[
			// This customization overrides the FGameplayTag row EDITOR-WIDE, so its combo-hosted
			// picker is the one users right-click most. Menu-hosted picker = selection-only: the
			// guard eats right-clicks so the engine row context menu (Add Sub-Tag / Rename / Delete)
			// can't crash the menu stack with "Window Creation Failed (1400)" — see the guard doc.
			// Tag management lives in the Gameplay Tag Manager window / Project Settings.
			SNew(SMenuHostedTagPickerGuard)
			[
			SNew(SGameplayTagPicker)
			.Filter(Filter)
			.MultiSelect(false)
			.PropertyHandle(StructPropertyHandle)
			.OnTagChanged_Lambda([WeakCombo](const TArray<FGameplayTagContainer>&)
			{
				// Single-select: dismiss the dropdown once a tag is picked (mirrors the engine widget).
				if (TSharedPtr<SComboButton> Combo = WeakCombo.Pin())
				{
					Combo->SetIsOpen(false);
				}
			})
			]
		];
}

#undef LOCTEXT_NAMESPACE

#endif // >= 5.3
