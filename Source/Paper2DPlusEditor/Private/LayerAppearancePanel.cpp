// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "LayerAppearancePanel.h"

#include "Paper2DPlusAppearanceResolver.h"
#include "Paper2DPlusCharacterLayerAsset.h"
#include "ScopedTransaction.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
#include "EditorStyleSet.h"
#ifndef FAppStyle
#define FAppStyle FEditorStyle
#define GetAppStyleSetName GetStyleSetName
#endif
#else
#include "Styling/AppStyle.h"
#endif

#define LOCTEXT_NAMESPACE "LayerAppearancePanel"

void SLayerAppearancePanel::Construct(const FArguments& InArgs)
{
	LayerAsset = InArgs._LayerAsset;
	if (const UPaper2DPlusCharacterLayerAsset* Asset = LayerAsset.Get())
	{
		SelectedPresetId = Asset->DefaultAppearancePresetId;
		if (!SelectedPresetId.IsValid() && Asset->AppearancePresets.Num() > 0)
		{
			SelectedPresetId = Asset->AppearancePresets[0].PresetId;
		}
	}

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
		.Padding(6.0f)
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			[
				SAssignNew(Content, SVerticalBox)
			]
		]
	];
	Rebuild();
}

int32 SLayerAppearancePanel::GetPresetCountForTests() const
{
	const UPaper2DPlusCharacterLayerAsset* Asset = LayerAsset.Get();
	return Asset ? Asset->AppearancePresets.Num() : 0;
}

int32 SLayerAppearancePanel::GetExclusiveGroupCountForTests() const
{
	const UPaper2DPlusCharacterLayerAsset* Asset = LayerAsset.Get();
	return Asset ? Asset->ExclusiveGroups.Num() : 0;
}

bool SLayerAppearancePanel::AddPreset(const FString& DisplayName, FGuid& OutPresetId)
{
	UPaper2DPlusCharacterLayerAsset* Asset = LayerAsset.Get();
	if (!Asset) return false;

	const FScopedTransaction Transaction(LOCTEXT("AddAppearancePreset", "Add Appearance Preset"));
	Asset->Modify();
	FCharacterLayerAppearancePreset& Preset = Asset->AppearancePresets.AddDefaulted_GetRef();
	Preset.PresetId = FGuid::NewGuid();
	Preset.DisplayName = DisplayName.IsEmpty()
		? FString::Printf(TEXT("Appearance %d"), Asset->AppearancePresets.Num())
		: DisplayName;
	if (const FCharacterLayerAppearancePreset* Selected = Asset->GetAppearancePresetById(SelectedPresetId))
	{
		Preset.ActiveLayerIds = Selected->ActiveLayerIds;
	}
	else
	{
		TArray<FGuid> Normalized;
		Paper2DPlusAppearanceResolver::NormalizeLayerSelection(Asset, {}, Normalized, nullptr);
		Preset.ActiveLayerIds = MoveTemp(Normalized);
	}
	if (!Asset->DefaultAppearancePresetId.IsValid())
	{
		Asset->DefaultAppearancePresetId = Preset.PresetId;
	}
	SelectedPresetId = Preset.PresetId;
	OutPresetId = Preset.PresetId;
	Asset->PostEditChange();
	ScheduleRebuild();
	return true;
}

bool SLayerAppearancePanel::RemovePreset(FGuid PresetId)
{
	UPaper2DPlusCharacterLayerAsset* Asset = LayerAsset.Get();
	if (!Asset || Asset->AppearancePresets.Num() <= 1) return false;
	const int32 Index = Asset->AppearancePresets.IndexOfByPredicate([PresetId](const auto& Preset)
	{
		return Preset.PresetId == PresetId;
	});
	if (Index == INDEX_NONE) return false;

	const FScopedTransaction Transaction(LOCTEXT("RemoveAppearancePreset", "Remove Appearance Preset"));
	Asset->Modify();
	const bool bWasDefault = Asset->DefaultAppearancePresetId == PresetId;
	Asset->AppearancePresets.RemoveAt(Index);
	SelectedPresetId = Asset->AppearancePresets[FMath::Min(Index, Asset->AppearancePresets.Num() - 1)].PresetId;
	if (bWasDefault) Asset->DefaultAppearancePresetId = SelectedPresetId;
	Asset->PostEditChange();
	ScheduleRebuild();
	return true;
}

bool SLayerAppearancePanel::SelectPreset(FGuid PresetId)
{
	const UPaper2DPlusCharacterLayerAsset* Asset = LayerAsset.Get();
	if (!Asset || !Asset->GetAppearancePresetById(PresetId)) return false;
	SelectedPresetId = PresetId;
	ScheduleRebuild();
	return true;
}

bool SLayerAppearancePanel::SetDefaultPreset(FGuid PresetId)
{
	UPaper2DPlusCharacterLayerAsset* Asset = LayerAsset.Get();
	if (!Asset || !Asset->GetAppearancePresetById(PresetId)) return false;
	if (Asset->DefaultAppearancePresetId == PresetId) return true;
	const FScopedTransaction Transaction(LOCTEXT("SetDefaultAppearance", "Set Default Appearance"));
	Asset->Modify();
	Asset->DefaultAppearancePresetId = PresetId;
	Asset->PostEditChange();
	ScheduleRebuild();
	return true;
}

bool SLayerAppearancePanel::SetLayerActiveInPreset(
	FGuid PresetId,
	FGuid LayerId,
	bool bActive)
{
	UPaper2DPlusCharacterLayerAsset* Asset = LayerAsset.Get();
	FCharacterLayerAppearancePreset* Preset = Asset
		? Asset->AppearancePresets.FindByPredicate([PresetId](const auto& Candidate)
		{
			return Candidate.PresetId == PresetId;
		})
		: nullptr;
	if (!Preset) return false;

	TArray<FGuid> Normalized;
	if (!Paper2DPlusAppearanceResolver::ApplyLayerActivation(
		Asset, Preset->ActiveLayerIds, LayerId, bActive, Normalized, nullptr))
	{
		return false;
	}
	if (Normalized == Preset->ActiveLayerIds) return true;
	const FScopedTransaction Transaction(LOCTEXT("EditAppearancePreset", "Edit Appearance Preset"));
	Asset->Modify();
	Preset->ActiveLayerIds = MoveTemp(Normalized);
	Asset->PostEditChange();
	ScheduleRebuild();
	return true;
}

bool SLayerAppearancePanel::AddExclusiveGroup(const FString& DisplayName, FGuid& OutGroupId)
{
	UPaper2DPlusCharacterLayerAsset* Asset = LayerAsset.Get();
	if (!Asset) return false;
	const FScopedTransaction Transaction(LOCTEXT("AddExclusiveGroup", "Add Exclusive Group"));
	Asset->Modify();
	FCharacterLayerExclusiveGroup& Group = Asset->ExclusiveGroups.AddDefaulted_GetRef();
	Group.GroupId = FGuid::NewGuid();
	Group.DisplayName = DisplayName.IsEmpty()
		? FString::Printf(TEXT("Exclusive Group %d"), Asset->ExclusiveGroups.Num())
		: DisplayName;
	OutGroupId = Group.GroupId;
	Asset->PostEditChange();
	ScheduleRebuild();
	return true;
}

bool SLayerAppearancePanel::AssignLayerToExclusiveGroup(FGuid LayerId, FGuid GroupId)
{
	UPaper2DPlusCharacterLayerAsset* Asset = LayerAsset.Get();
	FCharacterLayer* Layer = Asset ? Asset->Layers.FindByPredicate([LayerId](const FCharacterLayer& Candidate)
	{
		return Candidate.LayerId == LayerId;
	}) : nullptr;
	if (!Layer || (GroupId.IsValid() && !Asset->GetExclusiveGroupById(GroupId))) return false;
	if (Layer->ExclusiveGroupId == GroupId) return true;

	// Reject an assignment that would make any stored complete preset invalid. Evaluate the hypothetical
	// membership without mutating the UObject before its transaction begins.
	const bool bWouldInvalidate = GroupId.IsValid() && Asset->AppearancePresets.ContainsByPredicate(
		[Asset, LayerId, GroupId](const auto& Preset)
	{
		if (!Preset.ActiveLayerIds.Contains(LayerId)) return false;
		return Preset.ActiveLayerIds.ContainsByPredicate([Asset, LayerId, GroupId](const FGuid& ActiveId)
		{
			if (ActiveId == LayerId) return false;
			const FCharacterLayer* ActiveLayer = Asset->FindLayerById(ActiveId);
			return ActiveLayer && ActiveLayer->ExclusiveGroupId == GroupId;
		});
	});
	if (bWouldInvalidate) return false;

	const FScopedTransaction Transaction(LOCTEXT("AssignExclusiveGroup", "Assign Layer Exclusive Group"));
	Asset->Modify();
	Layer->ExclusiveGroupId = GroupId;
	Asset->PostEditChange();
	ScheduleRebuild();
	return true;
}

FText SLayerAppearancePanel::GetSummaryText() const
{
	const UPaper2DPlusCharacterLayerAsset* Asset = LayerAsset.Get();
	if (!Asset) return LOCTEXT("Unavailable", "Layer Asset unavailable.");
	const TArray<FString> Issues = Paper2DPlusAppearanceResolver::ValidateGenericAppearanceSource(Asset);
	if (Issues.IsEmpty())
	{
		return LOCTEXT("Ready", "Every preset is complete and one Default Appearance is designated.");
	}
	return FText::Format(
		LOCTEXT("IssueCount", "{0} appearance issue(s). Publishing remains blocked until repaired."),
		FText::AsNumber(Issues.Num()));
}

FReply SLayerAppearancePanel::AddPresetFromUi()
{
	FGuid Ignored;
	AddPreset(FString(), Ignored);
	return FReply::Handled();
}

FReply SLayerAppearancePanel::SetSelectedAsDefault()
{
	SetDefaultPreset(SelectedPresetId);
	return FReply::Handled();
}

FReply SLayerAppearancePanel::AddExclusiveGroupFromUi()
{
	FGuid Ignored;
	AddExclusiveGroup(FString(), Ignored);
	return FReply::Handled();
}

FReply SLayerAppearancePanel::CycleLayerExclusiveGroup(FGuid LayerId)
{
	UPaper2DPlusCharacterLayerAsset* Asset = LayerAsset.Get();
	const FCharacterLayer* Layer = Asset ? Asset->FindLayerById(LayerId) : nullptr;
	if (!Asset || !Layer) return FReply::Handled();
	int32 CurrentIndex = INDEX_NONE;
	if (Layer->ExclusiveGroupId.IsValid())
	{
		CurrentIndex = Asset->ExclusiveGroups.IndexOfByPredicate([Layer](const auto& Group)
		{
			return Group.GroupId == Layer->ExclusiveGroupId;
		});
	}
	const int32 NextIndex = CurrentIndex + 1;
	const FGuid NextGroupId = Asset->ExclusiveGroups.IsValidIndex(NextIndex)
		? Asset->ExclusiveGroups[NextIndex].GroupId
		: FGuid();
	AssignLayerToExclusiveGroup(LayerId, NextGroupId);
	return FReply::Handled();
}

void SLayerAppearancePanel::Rebuild()
{
	if (!Content.IsValid()) return;
	Content->ClearChildren();
	UPaper2DPlusCharacterLayerAsset* Asset = LayerAsset.Get();

	Content->AddSlot().AutoHeight().Padding(2.0f)
	[
		SNew(STextBlock)
		.Text(LOCTEXT("Title", "Appearance"))
		.Font(FAppStyle::GetFontStyle("HeadingExtraSmall"))
	];
	Content->AddSlot().AutoHeight().Padding(2.0f)
	[
		SNew(STextBlock).Text(GetSummaryText()).AutoWrapText(true)
	];
	Content->AddSlot().AutoHeight().Padding(2.0f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth()
		[
			SNew(SButton).Text(LOCTEXT("AddPreset", "Add Preset")).OnClicked(this, &SLayerAppearancePanel::AddPresetFromUi)
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(4.0f, 0.0f)
		[
			SNew(SButton).Text(LOCTEXT("SetDefault", "Set as Default")).OnClicked(this, &SLayerAppearancePanel::SetSelectedAsDefault)
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(4.0f, 0.0f)
		[
			SNew(SButton).Text(LOCTEXT("AddGroup", "Add Exclusive Group")).OnClicked(this, &SLayerAppearancePanel::AddExclusiveGroupFromUi)
		]
	];

	if (!Asset) return;
	for (const FCharacterLayerAppearancePreset& Preset : Asset->AppearancePresets)
	{
		const FGuid PresetId = Preset.PresetId;
		const bool bSelected = SelectedPresetId == PresetId;
		const bool bDefault = Asset->DefaultAppearancePresetId == PresetId;
		Content->AddSlot().AutoHeight().Padding(2.0f)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), bSelected ? "FlatButton.Primary" : "FlatButton")
			.Text(FText::FromString(Preset.DisplayName + (bDefault ? TEXT("  • Default") : TEXT(""))))
			.OnClicked_Lambda([this, PresetId]()
			{
				SelectPreset(PresetId);
				return FReply::Handled();
			})
		];
	}

	const FCharacterLayerAppearancePreset* Selected = Asset->GetAppearancePresetById(SelectedPresetId);
	if (!Selected)
	{
		Content->AddSlot().AutoHeight().Padding(4.0f)
		[
			SNew(STextBlock).Text(LOCTEXT("NeedPreset", "Add an Appearance Preset, then designate the Default Appearance."))
			.AutoWrapText(true)
		];
		return;
	}

	Content->AddSlot().AutoHeight().Padding(FMargin(2.0f, 8.0f, 2.0f, 2.0f))
	[
		SNew(STextBlock).Text(LOCTEXT("Layers", "Active Layers (global order)"))
		.Font(FAppStyle::GetFontStyle("HeadingExtraSmall"))
	];
	for (const FCharacterLayer& Layer : Asset->Layers)
	{
		const FGuid LayerId = Layer.LayerId;
		Content->AddSlot().AutoHeight().Padding(2.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f)
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([this, LayerId]()
				{
					const UPaper2DPlusCharacterLayerAsset* Current = LayerAsset.Get();
					const FCharacterLayerAppearancePreset* Preset = Current
						? Current->GetAppearancePresetById(SelectedPresetId) : nullptr;
					return Preset && Preset->ActiveLayerIds.Contains(LayerId)
						? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				})
				.OnCheckStateChanged_Lambda([this, LayerId](ECheckBoxState State)
				{
					SetLayerActiveInPreset(SelectedPresetId, LayerId, State == ECheckBoxState::Checked);
				})
				[
					SNew(STextBlock).Text(FText::FromString(Layer.LayerName))
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(4.0f, 0.0f)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.Text_Lambda([this, LayerId]()
				{
					const UPaper2DPlusCharacterLayerAsset* Current = LayerAsset.Get();
					const FCharacterLayer* CurrentLayer = Current ? Current->FindLayerById(LayerId) : nullptr;
					const FCharacterLayerExclusiveGroup* Group = CurrentLayer && Current
						? Current->GetExclusiveGroupById(CurrentLayer->ExclusiveGroupId) : nullptr;
					return Group ? FText::FromString(Group->DisplayName) : LOCTEXT("NoExclusiveGroup", "No Exclusive Group");
				})
				.ToolTipText(LOCTEXT("CycleExclusiveGroupTip", "Cycle this Layer through the authored Exclusive Groups. A change that would invalidate a complete preset is rejected."))
				.OnClicked_Lambda([this, LayerId]() { return CycleLayerExclusiveGroup(LayerId); })
			]
		];
	}
}

void SLayerAppearancePanel::ScheduleRebuild()
{
	if (bRebuildPending) return;
	bRebuildPending = true;
	RegisterActiveTimer(0.0f, FWidgetActiveTimerDelegate::CreateLambda(
		[this](double, float)
		{
			bRebuildPending = false;
			Rebuild();
			return EActiveTimerReturnType::Stop;
		}));
}

#undef LOCTEXT_NAMESPACE
