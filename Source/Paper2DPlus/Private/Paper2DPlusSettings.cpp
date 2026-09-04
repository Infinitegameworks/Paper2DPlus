// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "Paper2DPlusSettings.h"

/** UPaper2DPlusSettings — Project-wide plugin settings singleton. */

namespace
{
	FPaper2DPlusKnownCurve Paper2DPlusSettings_MakeKnownCurve(
		FName Name,
		const FString& Description,
		float DefaultValue,
		EPaper2DPlusKnownCurveSemantic Semantic,
		EPaper2DPlusCurveInterp DefaultMode,
		bool bUseFixedValueRange,
		float ValueMin,
		float ValueMax,
		bool bWarnWhenOutsideValueRange,
		bool bSnapOutputValues,
		float OutputSnap,
		const FString& Units = FString())
	{
		FPaper2DPlusKnownCurve Known(Name, Description, DefaultValue);
		Known.Semantic = Semantic;
		Known.DefaultMode = DefaultMode;
		Known.bUseFixedValueRange = bUseFixedValueRange;
		Known.ValueMin = ValueMin;
		Known.ValueMax = ValueMax;
		Known.bWarnWhenOutsideValueRange = bWarnWhenOutsideValueRange;
		Known.bSnapOutputValues = bSnapOutputValues;
		Known.OutputSnap = OutputSnap;
		Known.Units = Units;
		return Known;
	}
}

UPaper2DPlusSettings::UPaper2DPlusSettings()
{
	// Seed the well-known auxiliary curve registry with combat-roadmap defaults (TASK-74/TASK-74.1).
	// Designers may still author any name; config overrides win on load.
	KnownCurves = {
		Paper2DPlusSettings_MakeKnownCurve(TEXT("HitStop"),
			TEXT("Authored hit-stop frame-count convention. Paper2DPlus does not read it automatically; game logic may query it and call TriggerHitStop explicitly. 0/absent = no authored suggestion."),
			0.f, EPaper2DPlusKnownCurveSemantic::FrameCount, EPaper2DPlusCurveInterp::Constant,
			/*bUseFixedValueRange*/ false, 0.f, 60.f, /*bWarnWhenOutsideValueRange*/ false,
			/*bSnapOutputValues*/ true, 1.f, TEXT("frames")),
		Paper2DPlusSettings_MakeKnownCurve(TEXT("Cancel_Normal"),
			TEXT("Reserved 0/1 authoring convention for a future Normal cancel window. The transition driver was removed; Paper2DPlus has no runtime reader for this curve."),
			0.f, EPaper2DPlusKnownCurveSemantic::StepWindow, EPaper2DPlusCurveInterp::Constant,
			/*bUseFixedValueRange*/ true, 0.f, 1.f, /*bWarnWhenOutsideValueRange*/ true,
			/*bSnapOutputValues*/ true, 1.f),
		Paper2DPlusSettings_MakeKnownCurve(TEXT("Cancel_Special"),
			TEXT("Reserved 0/1 authoring convention for a future Special cancel window. The transition driver was removed; Paper2DPlus has no runtime reader for this curve."),
			0.f, EPaper2DPlusKnownCurveSemantic::StepWindow, EPaper2DPlusCurveInterp::Constant,
			/*bUseFixedValueRange*/ true, 0.f, 1.f, /*bWarnWhenOutsideValueRange*/ true,
			/*bSnapOutputValues*/ true, 1.f),
		Paper2DPlusSettings_MakeKnownCurve(TEXT("HitWindow"),
			TEXT("Constant/step curve indexing multi-hit windows (0,1,2...); ValidateAndRegisterHit/RegisterHitOnce floor it span-aware so each window dedupes per victim (TASK-57). Absent = single window 0."),
			0.f, EPaper2DPlusKnownCurveSemantic::StepWindow, EPaper2DPlusCurveInterp::Constant,
			/*bUseFixedValueRange*/ false, 0.f, 8.f, /*bWarnWhenOutsideValueRange*/ false,
			/*bSnapOutputValues*/ true, 1.f),
		Paper2DPlusSettings_MakeKnownCurve(TEXT("Damage"),
			TEXT("Per-frame damage multiplier / magnitude for charged moves."),
			0.f, EPaper2DPlusKnownCurveSemantic::Continuous, EPaper2DPlusCurveInterp::Linear,
			/*bUseFixedValueRange*/ false, 0.f, 1.f, /*bWarnWhenOutsideValueRange*/ false,
			/*bSnapOutputValues*/ false, 1.f),
		Paper2DPlusSettings_MakeKnownCurve(TEXT("Armor"),
			TEXT("Hyper-armor active on this frame (0/1 step curve)."),
			0.f, EPaper2DPlusKnownCurveSemantic::BooleanStep, EPaper2DPlusCurveInterp::Constant,
			/*bUseFixedValueRange*/ true, 0.f, 1.f, /*bWarnWhenOutsideValueRange*/ true,
			/*bSnapOutputValues*/ true, 1.f),
		Paper2DPlusSettings_MakeKnownCurve(TEXT("ShakeIntensity"),
			TEXT("Camera-shake intensity to drive on this frame."),
			0.f, EPaper2DPlusKnownCurveSemantic::Continuous, EPaper2DPlusCurveInterp::Linear,
			/*bUseFixedValueRange*/ false, 0.f, 1.f, /*bWarnWhenOutsideValueRange*/ false,
			/*bSnapOutputValues*/ false, 1.f)
	};
}

const UPaper2DPlusSettings* UPaper2DPlusSettings::Get()
{
	return GetDefault<UPaper2DPlusSettings>();
}

#if WITH_EDITOR
void UPaper2DPlusSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// Skip INTERACTIVE change notifications (a color-wheel drag fires one per mouse-move): consumers
	// rebuild whole panels on this signal (Animation Map board, browser chips), so broadcasting per
	// drag tick is a rebuild storm. The final ValueSet on mouse-up broadcasts once (review finding).
	if (PropertyChangedEvent.ChangeType == EPropertyChangeType::Interactive)
	{
		return;
	}

	const FName PropertyName = PropertyChangedEvent.GetPropertyName();
	// NOT GetMemberPropertyName(): that accessor does not exist on UE 5.0 — read the member property
	// directly (identical semantics on every version; the cross-version harness caught it).
	const FName MemberName = PropertyChangedEvent.MemberProperty ? PropertyChangedEvent.MemberProperty->GetFName() : NAME_None;
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UPaper2DPlusSettings, TagColors)
		|| MemberName == GET_MEMBER_NAME_CHECKED(UPaper2DPlusSettings, TagColors))
	{
		OnTagColorsChanged().Broadcast();
	}

	if (PropertyName == GET_MEMBER_NAME_CHECKED(UPaper2DPlusSettings, DefaultCharacterCatalog)
		|| MemberName == GET_MEMBER_NAME_CHECKED(UPaper2DPlusSettings, DefaultCharacterCatalog))
	{
		OnCharacterCatalogSettingsChanged().Broadcast();
	}

	if (PropertyName == GET_MEMBER_NAME_CHECKED(UPaper2DPlusSettings, bEnableAseLiveReimport)
		|| MemberName == GET_MEMBER_NAME_CHECKED(UPaper2DPlusSettings, bEnableAseLiveReimport))
	{
		OnAseLiveReimportSettingChanged().Broadcast();
	}
}
#endif

FSimpleMulticastDelegate& UPaper2DPlusSettings::OnTagColorsChanged()
{
	static FSimpleMulticastDelegate Delegate;
	return Delegate;
}

FSimpleMulticastDelegate& UPaper2DPlusSettings::OnCharacterCatalogSettingsChanged()
{
	static FSimpleMulticastDelegate Delegate;
	return Delegate;
}

FSimpleMulticastDelegate& UPaper2DPlusSettings::OnAseLiveReimportSettingChanged()
{
	static FSimpleMulticastDelegate Delegate;
	return Delegate;
}

FLinearColor UPaper2DPlusSettings::ResolveTagColor(const FGameplayTag& Tag, bool& bOutFound)
{
	bOutFound = false;
	if (!Tag.IsValid())
	{
		return FLinearColor::Gray;
	}

	const UPaper2DPlusSettings* Settings = Get();
	if (!Settings || Settings->TagColors.Num() == 0)
	{
		return FLinearColor::Gray;
	}

	// Walk the tag and its ancestors (Paper2DPlus.Phase.Active → Paper2DPlus.Phase → Paper2DPlus),
	// taking the FIRST registered color found. Nearest tag wins; an ancestor color tints descendants.
	for (FGameplayTag Current = Tag; Current.IsValid(); Current = Current.RequestDirectParent())
	{
		for (const FPaper2DPlusTagColor& Entry : Settings->TagColors)
		{
			if (Entry.Tag == Current)
			{
				bOutFound = true;
				return Entry.Color;
			}
		}
	}

	return FLinearColor::Gray;
}

FLinearColor UPaper2DPlusSettings::ResolveTagColorOrDefault(const FGameplayTag& Tag, const FLinearColor& Fallback)
{
	bool bFound = false;
	const FLinearColor Resolved = ResolveTagColor(Tag, bFound);
	return bFound ? Resolved : Fallback;
}

bool UPaper2DPlusSettings::HasAnyTagColors()
{
	const UPaper2DPlusSettings* Settings = Get();
	return Settings && Settings->TagColors.Num() > 0;
}

void UPaper2DPlusSettings::SetTagColor(const FGameplayTag& Tag, const FLinearColor& Color)
{
	if (!Tag.IsValid())
	{
		return;
	}

	UPaper2DPlusSettings* Settings = GetMutableDefault<UPaper2DPlusSettings>();
	if (!Settings)
	{
		return;
	}

	bool bChanged = false;
	if (FPaper2DPlusTagColor* Existing = Settings->TagColors.FindByPredicate(
		[&Tag](const FPaper2DPlusTagColor& Entry) { return Entry.Tag == Tag; }))
	{
		if (!Existing->Color.Equals(Color))
		{
			Existing->Color = Color;
			bChanged = true;
		}
	}
	else
	{
		Settings->TagColors.Emplace(Tag, Color);
		bChanged = true;
	}

	if (bChanged)
	{
#if WITH_EDITOR
		Settings->TryUpdateDefaultConfigFile();
#else
		Settings->SaveConfig();
#endif
		OnTagColorsChanged().Broadcast();
	}
}

void UPaper2DPlusSettings::ClearTagColor(const FGameplayTag& Tag)
{
	UPaper2DPlusSettings* Settings = GetMutableDefault<UPaper2DPlusSettings>();
	if (!Settings)
	{
		return;
	}

	const int32 RemovedCount = Settings->TagColors.RemoveAll(
		[&Tag](const FPaper2DPlusTagColor& Entry) { return Entry.Tag == Tag; });
	if (RemovedCount > 0)
	{
#if WITH_EDITOR
		Settings->TryUpdateDefaultConfigFile();
#else
		Settings->SaveConfig();
#endif
		OnTagColorsChanged().Broadcast();
	}
}

FText UPaper2DPlusSettings::GetDescriptionForTag(const FGameplayTag& Tag) const
{
	for (const FTagMappingDescription& Mapping : TagMappingDescriptions)
	{
		if (Mapping.Tag == Tag)
		{
			return Mapping.Description;
		}
	}
	return FText::GetEmpty();
}
