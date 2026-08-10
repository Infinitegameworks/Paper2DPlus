// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/EngineVersionComparison.h"

// 5.3+ only: the dropdown is the engine's public SGameplayTagPicker, which does not exist before 5.3.
// Older engines keep the stock FGameplayTag widget (the registry + every other consumer still works).
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 3)

#include "IPropertyTypeCustomization.h"
#include "GameplayTagContainer.h"

class IPropertyHandle;
class SComboButton;
class SWidget;

/**
 * Editor-wide FGameplayTag property customization that draws the selected tag on a COLORED chip — the
 * color comes from the project-wide UPaper2DPlusSettings tag-color registry (UPaper2DPlusSettings::
 * ResolveTagColor). The dropdown reuses the engine's public SGameplayTagPicker (bound to the property
 * handle), so the full tag tree / search / add-tag behavior is preserved; only the closed-state chip is
 * colorized (the "Gameplay Tag Colors" Fab-plugin look).
 *
 * Registered for the "GameplayTag" property type in FPaper2DPlusEditorModule::StartupModule when
 * UPaper2DPlusSettings::bColorizeGameplayTagPickers is true (it OVERRIDES Unreal's default FGameplayTag
 * widget editor-wide). Honors the property's `Categories` meta so filtered pickers stay filtered.
 */
class FPaper2DPlusGameplayTagColorCustomization : public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, class FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, class IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils) override {}

private:
	/** Read the single FGameplayTag value behind the property handle (first object on multi-edit). */
	FGameplayTag GetCurrentTag() const;

	FText GetTagText() const;
	FSlateColor GetChipBorderColor() const;
	FSlateColor GetChipTextColor() const;

	/** Build the dropdown — an SGameplayTagPicker bound to the property handle. */
	TSharedRef<SWidget> OnGetMenuContent();

	TSharedPtr<IPropertyHandle> StructPropertyHandle;
	TWeakPtr<SComboButton> ComboButton;
};

#endif // >= 5.3
