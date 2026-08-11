// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "Customizations/Paper2DPlusCombatConsiderationCustomization.h"
#include "Customizations/Paper2DPlusCombatConsiderationText.h"

#include "DetailWidgetRow.h"
#include "DetailLayoutBuilder.h"
#include "IDetailChildrenBuilder.h"
#include "IDetailPropertyRow.h"
#include "PropertyHandle.h"
#include "GameplayTagContainer.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SNullWidget.h"

#define LOCTEXT_NAMESPACE "CombatConsiderationCustomization"

namespace
{
	/** Read a single value out of a property handle by raw address (works for enums, floats, bools, structs). */
	template <typename T>
	T ReadRaw(const TSharedPtr<IPropertyHandle>& Handle, const T& Default)
	{
		if (Handle.IsValid())
		{
			TArray<void*> RawData;
			Handle->AccessRawData(RawData);
			if (RawData.Num() >= 1 && RawData[0] != nullptr)
			{
				return *static_cast<T*>(RawData[0]);
			}
		}
		return Default;
	}
}

TSharedRef<IPropertyTypeCustomization> FPaper2DPlusCombatConsiderationCustomization::MakeInstance()
{
	return MakeShared<FPaper2DPlusCombatConsiderationCustomization>();
}

EPaper2DPlusCombatConsiderationSource FPaper2DPlusCombatConsiderationCustomization::GetSource() const
{
	return ReadRaw<EPaper2DPlusCombatConsiderationSource>(SourceHandle, EPaper2DPlusCombatConsiderationSource::DistanceToTarget);
}

EPaper2DPlusCombatConsiderationOp FPaper2DPlusCombatConsiderationCustomization::GetOperation() const
{
	return ReadRaw<EPaper2DPlusCombatConsiderationOp>(OperationHandle, EPaper2DPlusCombatConsiderationOp::RangeWindow);
}

FText FPaper2DPlusCombatConsiderationCustomization::GetSummaryText() const
{
	// Read the whole live consideration and hand it to the shared summariser so the collapsed header
	// and the per-attack "effective rules" view always read a rule the same way.
	const FPaper2DPlusCombatConsideration Current = ReadRaw<FPaper2DPlusCombatConsideration>(StructHandle, FPaper2DPlusCombatConsideration());
	return Paper2DPlusCombatText::SummarizeConsideration(Current);
}

namespace
{
	bool IsNumericSource(EPaper2DPlusCombatConsiderationSource Source)
	{
		switch (Source)
		{
		case EPaper2DPlusCombatConsiderationSource::DistanceToTarget:
		case EPaper2DPlusCombatConsiderationSource::SelfHealthPercent:
		case EPaper2DPlusCombatConsiderationSource::TargetHealthPercent:
		case EPaper2DPlusCombatConsiderationSource::CustomFloat:
			return true;
		default:
			return false;
		}
	}
}

EVisibility FPaper2DPlusCombatConsiderationCustomization::GetOperationRowVisibility() const
{
	// DesiredRoleTags and CustomBool ignore Operation in the scorer, so hide it for them.
	const EPaper2DPlusCombatConsiderationSource Source = GetSource();
	return (Source == EPaper2DPlusCombatConsiderationSource::DesiredRoleTags || Source == EPaper2DPlusCombatConsiderationSource::CustomBool)
		? EVisibility::Collapsed : EVisibility::Visible;
}

EVisibility FPaper2DPlusCombatConsiderationCustomization::GetVariableRowVisibility() const
{
	const EPaper2DPlusCombatConsiderationSource Source = GetSource();
	return (Source == EPaper2DPlusCombatConsiderationSource::CustomBool || Source == EPaper2DPlusCombatConsiderationSource::CustomFloat)
		? EVisibility::Visible : EVisibility::Collapsed;
}

EVisibility FPaper2DPlusCombatConsiderationCustomization::GetRangeRowVisibility() const
{
	// Min/Max are only read by the generic operation switch, which only numeric sources reach.
	if (!IsNumericSource(GetSource()))
	{
		return EVisibility::Collapsed;
	}
	switch (GetOperation())
	{
	case EPaper2DPlusCombatConsiderationOp::RangeWindow:
	case EPaper2DPlusCombatConsiderationOp::Linear:
	case EPaper2DPlusCombatConsiderationOp::LinearInverse:
		return EVisibility::Visible;
	default:
		return EVisibility::Collapsed;
	}
}

EVisibility FPaper2DPlusCombatConsiderationCustomization::GetBoolRowVisibility() const
{
	// bExpectedBool is read by the CustomBool source (always) and by the BoolEquals operation.
	return (GetSource() == EPaper2DPlusCombatConsiderationSource::CustomBool || GetOperation() == EPaper2DPlusCombatConsiderationOp::BoolEquals)
		? EVisibility::Visible : EVisibility::Collapsed;
}

EVisibility FPaper2DPlusCombatConsiderationCustomization::GetTagsRowVisibility() const
{
	// RequiredTags is read ONLY by the TargetStateTags source (Operation just picks All vs Any).
	return GetSource() == EPaper2DPlusCombatConsiderationSource::TargetStateTags ? EVisibility::Visible : EVisibility::Collapsed;
}

void FPaper2DPlusCombatConsiderationCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	StructHandle      = PropertyHandle;
	SourceHandle      = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPaper2DPlusCombatConsideration, Source));
	OperationHandle   = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPaper2DPlusCombatConsideration, Operation));
	CombineModeHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPaper2DPlusCombatConsideration, CombineMode));
	VariableTagHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPaper2DPlusCombatConsideration, VariableTag));
	RequiredTagsHandle= PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPaper2DPlusCombatConsideration, RequiredTags));
	MinValueHandle    = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPaper2DPlusCombatConsideration, MinValue));
	MaxValueHandle    = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPaper2DPlusCombatConsideration, MaxValue));
	WeightHandle      = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPaper2DPlusCombatConsideration, Weight));
	ExpectedBoolHandle= PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPaper2DPlusCombatConsideration, bExpectedBool));
	NameHandle        = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPaper2DPlusCombatConsideration, ConsiderationName));

	HeaderRow
	.NameContent()
	.MinDesiredWidth(360.0f)
	[
		SNew(STextBlock)
		.Text(this, &FPaper2DPlusCombatConsiderationCustomization::GetSummaryText)
		.ToolTipText(LOCTEXT("SummaryTip", "Plain-English reading of this scoring rule. Expand to edit the operands that apply to the chosen input + condition."))
		.Font(IDetailLayoutBuilder::GetDetailFont())
	]
	.ValueContent()
	.MinDesiredWidth(90.0f)
	.MaxDesiredWidth(110.0f)
	[
		WeightHandle.IsValid() ? WeightHandle->CreatePropertyValueWidget() : SNullWidget::NullWidget
	];
}

void FPaper2DPlusCombatConsiderationCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	// The two selectors that drive everything else (Operation is hidden for sources that ignore it).
	if (SourceHandle.IsValid())    { ChildBuilder.AddProperty(SourceHandle.ToSharedRef()); }
	if (OperationHandle.IsValid())
	{
		ChildBuilder.AddProperty(OperationHandle.ToSharedRef())
			.Visibility(TAttribute<EVisibility>::CreateSP(this, &FPaper2DPlusCombatConsiderationCustomization::GetOperationRowVisibility));
	}

	// Operand rows — each only visible for the Source/Operation that uses it.
	if (VariableTagHandle.IsValid())
	{
		ChildBuilder.AddProperty(VariableTagHandle.ToSharedRef())
			.Visibility(TAttribute<EVisibility>::CreateSP(this, &FPaper2DPlusCombatConsiderationCustomization::GetVariableRowVisibility));
	}
	if (MinValueHandle.IsValid())
	{
		ChildBuilder.AddProperty(MinValueHandle.ToSharedRef())
			.Visibility(TAttribute<EVisibility>::CreateSP(this, &FPaper2DPlusCombatConsiderationCustomization::GetRangeRowVisibility));
	}
	if (MaxValueHandle.IsValid())
	{
		ChildBuilder.AddProperty(MaxValueHandle.ToSharedRef())
			.Visibility(TAttribute<EVisibility>::CreateSP(this, &FPaper2DPlusCombatConsiderationCustomization::GetRangeRowVisibility));
	}
	if (ExpectedBoolHandle.IsValid())
	{
		ChildBuilder.AddProperty(ExpectedBoolHandle.ToSharedRef())
			.Visibility(TAttribute<EVisibility>::CreateSP(this, &FPaper2DPlusCombatConsiderationCustomization::GetBoolRowVisibility));
	}
	if (RequiredTagsHandle.IsValid())
	{
		ChildBuilder.AddProperty(RequiredTagsHandle.ToSharedRef())
			.Visibility(TAttribute<EVisibility>::CreateSP(this, &FPaper2DPlusCombatConsiderationCustomization::GetTagsRowVisibility));
	}

	// How it folds into the score, then an optional label. (Weight lives on the header row.)
	if (CombineModeHandle.IsValid()) { ChildBuilder.AddProperty(CombineModeHandle.ToSharedRef()); }
	if (NameHandle.IsValid())        { ChildBuilder.AddProperty(NameHandle.ToSharedRef()); }
}

#undef LOCTEXT_NAMESPACE
