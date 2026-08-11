// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "ProfileCompletionPanel.h"

#include "Editor.h"
#include "Paper2DPlusCharacterLayerAsset.h"
#include "Paper2DPlusCombatProfileAsset.h"
#include "Paper2DPlusEffectProfileAsset.h"
#include "ProfilePropertyRow.h"
#include "ScopedTransaction.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
#include "EditorStyleSet.h"
#ifndef FAppStyle
#define FAppStyle FEditorStyle
#endif
#else
#include "Styling/AppStyle.h"
#endif

#define LOCTEXT_NAMESPACE "ProfileCompletionPanel"

const TArray<SProfileCompletionPanel::FCriterion>& SProfileCompletionPanel::GetCriteria() const
{
	static const TArray<FCriterion> LayerCriteria = {
		{ 0, LOCTEXT("LayerStructure", "Structure"), LOCTEXT("LayerStructureTip", "Layers, optional Exclusive Groups, and global order are organized and complete.") },
		{ 1, LOCTEXT("LayerArt", "Art"), LOCTEXT("LayerArtTip", "Layer art and animation mappings are complete.") },
		{ 2, LOCTEXT("LayerHitboxes", "Hitboxes"), LOCTEXT("LayerHitboxesTip", "Layer-authored hitboxes and sockets are complete.") },
		{ 3, LOCTEXT("LayerFrameCues", "Frame Cues"), LOCTEXT("LayerFrameCuesTip", "Layer-authored Frame Cues are complete.") },
		{ 4, LOCTEXT("LayerPublishing", "Publishing"), LOCTEXT("LayerPublishingTip", "The chosen delivery mode and required publishing workflow are complete.") },
	};
	static const TArray<FCriterion> EffectCriteria = {
		{ 0, LOCTEXT("EffectLibrary", "Library"), LOCTEXT("EffectLibraryTip", "The ordered effect library membership is complete.") },
		{ 1, LOCTEXT("EffectClassification", "Classification"), LOCTEXT("EffectClassificationTip", "Effect type and descriptor classification is complete.") },
		{ 2, LOCTEXT("EffectPreview", "Preview"), LOCTEXT("EffectPreviewTip", "Effect animation playback and frames have been reviewed.") },
		{ 3, LOCTEXT("EffectPlacement", "Placement"), LOCTEXT("EffectPlacementTip", "Expected Frame Cue placement and facing behavior have been reviewed.") },
		{ 4, LOCTEXT("EffectValidation", "Validation"), LOCTEXT("EffectValidationTip", "The Effect Profile has passed its final validation review.") },
	};
	static const TArray<FCriterion> CombatCriteria = {
		{ 0, LOCTEXT("CombatSetup", "Setup"), LOCTEXT("CombatSetupTip", "The linked Character Profile and core setup are complete.") },
		{ 1, LOCTEXT("CombatAttackTuning", "Attack Tuning"), LOCTEXT("CombatAttackTuningTip", "Attack-specific tuning is complete.") },
		{ 2, LOCTEXT("CombatVariablesDefaults", "Variables / Defaults"), LOCTEXT("CombatVariablesDefaultsTip", "Variables and tag defaults are complete.") },
		{ 3, LOCTEXT("CombatScoring", "Scoring"), LOCTEXT("CombatScoringTip", "Scoring profiles and considerations are complete.") },
		{ 4, LOCTEXT("CombatScenarioTesting", "Scenario Testing"), LOCTEXT("CombatScenarioTestingTip", "Score Playground and Combat Lab scenarios have been reviewed.") },
	};

	switch (ProfileKind)
	{
	case EProfileCompletionKind::Effect:
		return EffectCriteria;
	case EProfileCompletionKind::Combat:
		return CombatCriteria;
	default:
		return LayerCriteria;
	}
}

void SProfileCompletionPanel::Construct(const FArguments& InArgs)
{
	Asset = InArgs._Asset;
	ProfileKind = InArgs._ProfileKind;

	if (GEditor)
	{
		GEditor->RegisterForUndo(this);
	}

	TSharedRef<SVerticalBox> CriteriaBox = SNew(SVerticalBox);
	for (const FCriterion& Criterion : GetCriteria())
	{
		CriteriaBox->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 2.0f)
		[
			SNew(SCheckBox)
			.IsEnabled(Asset.IsValid())
			.IsChecked(this, &SProfileCompletionPanel::GetCriterionCheckState, Criterion.Bit)
			.OnCheckStateChanged(this, &SProfileCompletionPanel::HandleCriterionCheckStateChanged, Criterion.Bit)
			.ToolTipText(Criterion.ToolTip)
			[
				SNew(STextBlock).Text(Criterion.Label)
			]
		];
	}

	ChildSlot
	[
		SNew(SBorder)
		.Padding(8.0f)
		.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(STextBlock)
				.Text(this, &SProfileCompletionPanel::GetHeaderText)
				.Font(FAppStyle::GetFontStyle("BoldFont"))
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 2.0f, 0.0f, 4.0f)
			[
				SNew(STextBlock).Text(this, &SProfileCompletionPanel::GetProgressText)
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 6.0f)
			[
				SNew(SProgressBar).Percent(this, &SProfileCompletionPanel::GetProgressFraction)
			]
			// Nothing here is derived from asset content, so a fully authored profile still opens at
			// 0 / 5 — which reads as a broken meter unless the panel says who does the ticking.
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 4.0f)
			[
				FProfilePropertyRowUtils::MakeSectionHint(LOCTEXT(
					"ManualChecklistHint",
					"You tick these yourself as you finish each area. Nothing here is detected "
					"automatically, so a finished profile still starts at zero."))
			]
			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			[
				SNew(SScrollBox)
				+ SScrollBox::Slot()[CriteriaBox]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 6.0f, 0.0f, 0.0f)
			[
				SNew(SUniformGridPanel)
				.SlotPadding(FMargin(2.0f, 0.0f))
				+ SUniformGridPanel::Slot(0, 0)
				[
					SNew(SButton)
					.IsEnabled(Asset.IsValid())
					.Text(LOCTEXT("All", "All"))
					.OnClicked(this, &SProfileCompletionPanel::HandleSetAllClicked, true)
				]
				+ SUniformGridPanel::Slot(1, 0)
				[
					SNew(SButton)
					.IsEnabled(Asset.IsValid())
					.Text(LOCTEXT("None", "None"))
					.OnClicked(this, &SProfileCompletionPanel::HandleSetAllClicked, false)
				]
			]
		]
	];
}

SProfileCompletionPanel::~SProfileCompletionPanel()
{
	Shutdown();
}

void SProfileCompletionPanel::Shutdown()
{
	if (bShutdown) return;
	bShutdown = true;
	if (GEditor)
	{
		GEditor->UnregisterForUndo(this);
	}
}

void SProfileCompletionPanel::PostUndo(bool bSuccess)
{
	(void)bSuccess;
}

void SProfileCompletionPanel::PostRedo(bool bSuccess)
{
	(void)bSuccess;
}

int32 SProfileCompletionPanel::GetDisplayedCriterionCountForTests() const
{
	return GetCriteria().Num();
}

FText SProfileCompletionPanel::GetCriterionLabelForTests(int32 CriterionBit) const
{
	for (const FCriterion& Criterion : GetCriteria())
	{
		if (Criterion.Bit == CriterionBit)
		{
			return Criterion.Label;
		}
	}
	return FText::GetEmpty();
}

int32 SProfileCompletionPanel::GetCompletionMaskForTests() const
{
	return GetCompletionFlags();
}

bool SProfileCompletionPanel::SetCriterionCompletedForTests(int32 CriterionBit, bool bCompleted)
{
	if (CriterionBit < 0 || CriterionBit >= GetCriteria().Num()) return false;
	const int32 CurrentFlags = GetCompletionFlags();
	const int32 NewFlags = bCompleted
		? CurrentFlags | (1 << CriterionBit)
		: CurrentFlags & ~(1 << CriterionBit);
	return MutateCompletionFlags(NewFlags, LOCTEXT("SetCriterionTransaction", "Set Profile Completion Criterion"));
}

bool SProfileCompletionPanel::SetAllCompletedForTests(bool bCompleted)
{
	return MutateCompletionFlags(
		bCompleted ? AllCriteriaMask : 0,
		bCompleted
			? LOCTEXT("SetAllTransaction", "Complete Profile Criteria")
			: LOCTEXT("ClearAllTransaction", "Clear Profile Completion"));
}

int32* SProfileCompletionPanel::ResolveCompletionFlags() const
{
	UObject* Object = Asset.Get();
	if (!Object) return nullptr;
	switch (ProfileKind)
	{
	case EProfileCompletionKind::Layer:
		if (UPaper2DPlusCharacterLayerAsset* Layer = Cast<UPaper2DPlusCharacterLayerAsset>(Object))
		{
			return &Layer->EditorCompletionFlags;
		}
		break;
	case EProfileCompletionKind::Effect:
		if (UPaper2DPlusEffectProfileAsset* Effect = Cast<UPaper2DPlusEffectProfileAsset>(Object))
		{
			return &Effect->EditorCompletionFlags;
		}
		break;
	case EProfileCompletionKind::Combat:
		if (UPaper2DPlusCombatProfileAsset* Combat = Cast<UPaper2DPlusCombatProfileAsset>(Object))
		{
			return &Combat->EditorCompletionFlags;
		}
		break;
	}
	return nullptr;
}

int32 SProfileCompletionPanel::GetCompletionFlags() const
{
	const int32* Flags = ResolveCompletionFlags();
	return Flags ? (*Flags & AllCriteriaMask) : 0;
}

int32 SProfileCompletionPanel::GetCompletedCriterionCount() const
{
	return FMath::CountBits(static_cast<uint32>(GetCompletionFlags()));
}

FText SProfileCompletionPanel::GetHeaderText() const
{
	switch (ProfileKind)
	{
	case EProfileCompletionKind::Effect:
		return LOCTEXT("EffectHeader", "Effect Profile Completion");
	case EProfileCompletionKind::Combat:
		return LOCTEXT("CombatHeader", "Combat Profile Completion");
	default:
		return LOCTEXT("LayerHeader", "Layer Profile Completion");
	}
}

FText SProfileCompletionPanel::GetProgressText() const
{
	return FText::Format(
		LOCTEXT("Progress", "{0} / {1} criteria complete"),
		FText::AsNumber(GetCompletedCriterionCount()),
		FText::AsNumber(GetCriteria().Num()));
}

TOptional<float> SProfileCompletionPanel::GetProgressFraction() const
{
	return GetCriteria().IsEmpty()
		? TOptional<float>()
		: TOptional<float>(static_cast<float>(GetCompletedCriterionCount()) / GetCriteria().Num());
}

ECheckBoxState SProfileCompletionPanel::GetCriterionCheckState(int32 CriterionBit) const
{
	return (GetCompletionFlags() & (1 << CriterionBit)) != 0
		? ECheckBoxState::Checked
		: ECheckBoxState::Unchecked;
}

void SProfileCompletionPanel::HandleCriterionCheckStateChanged(
	ECheckBoxState NewState,
	int32 CriterionBit)
{
	SetCriterionCompletedForTests(CriterionBit, NewState == ECheckBoxState::Checked);
}

FReply SProfileCompletionPanel::HandleSetAllClicked(bool bCompleted)
{
	SetAllCompletedForTests(bCompleted);
	return FReply::Handled();
}

bool SProfileCompletionPanel::MutateCompletionFlags(
	int32 NewFlags,
	const FText& TransactionText)
{
	int32* Flags = ResolveCompletionFlags();
	UObject* Object = Asset.Get();
	if (!Flags || !Object) return false;
	NewFlags &= AllCriteriaMask;
	if (*Flags == NewFlags) return false;

	const FScopedTransaction Transaction(TransactionText);
	Object->SetFlags(RF_Transactional);
	Object->Modify();
	*Flags = NewFlags;
	Object->PostEditChange();
	Object->MarkPackageDirty();
	return true;
}

#undef LOCTEXT_NAMESPACE
