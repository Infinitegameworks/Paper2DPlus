// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IPropertyTypeCustomization.h"
#include "Paper2DPlusCombatProfileTypes.h"

class IPropertyHandle;

/**
 * Property-type customization for FPaper2DPlusCombatConsideration.
 *
 * A raw consideration has 11 fields but only 3-4 matter for any given Source+Operation, so the stock
 * details view (all 11 fields, always) is the single worst part of authoring a Combat Profile. This
 * customization:
 *   - leads each consideration with a plain-English summary ("When distance to target is within
 *     100-300, multiply score x1.0"), shown on the collapsed header row, and
 *   - in the expanded body shows ONLY the operands that matter for the chosen Source/Operation
 *     (Min/Max for range ops, the bool for BoolEquals, the variable for Custom*, the tags for TagAny/All).
 *
 * Because it is a property-TYPE customization it applies everywhere FPaper2DPlusCombatConsideration is
 * shown — the per-attack editor, the tag-defaults, the scoring-profile globals, the Details tab — from
 * one registration in the editor module. UE 5.0-5.7 safe (no version-specific property-editor APIs).
 */
class FPaper2DPlusCombatConsiderationCustomization : public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	//~ IPropertyTypeCustomization
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils) override;

private:
	TSharedPtr<IPropertyHandle> StructHandle;   // the whole consideration (for the summary)
	TSharedPtr<IPropertyHandle> SourceHandle;
	TSharedPtr<IPropertyHandle> OperationHandle;
	TSharedPtr<IPropertyHandle> CombineModeHandle;
	TSharedPtr<IPropertyHandle> VariableTagHandle;
	TSharedPtr<IPropertyHandle> RequiredTagsHandle;
	TSharedPtr<IPropertyHandle> MinValueHandle;
	TSharedPtr<IPropertyHandle> MaxValueHandle;
	TSharedPtr<IPropertyHandle> WeightHandle;
	TSharedPtr<IPropertyHandle> ExpectedBoolHandle;
	TSharedPtr<IPropertyHandle> NameHandle;

	EPaper2DPlusCombatConsiderationSource GetSource() const;
	EPaper2DPlusCombatConsiderationOp GetOperation() const;

	/** The plain-English summary shown on the header row (re-evaluated live as fields change). */
	FText GetSummaryText() const;

	/** Visibility predicates driven by the live Source/Operation values — kept in lockstep with which
	 *  operands CombatScoring_EvaluateConsideration actually reads for each source. */
	EVisibility GetOperationRowVisibility() const;  // hidden for sources that ignore Operation
	EVisibility GetVariableRowVisibility() const;   // Source is CustomBool/CustomFloat
	EVisibility GetRangeRowVisibility() const;      // numeric source + a range Operation reads Min/Max
	EVisibility GetBoolRowVisibility() const;       // Source is CustomBool, or Operation is BoolEquals
	EVisibility GetTagsRowVisibility() const;       // Source is TargetStateTags (the only reader of RequiredTags)
};
