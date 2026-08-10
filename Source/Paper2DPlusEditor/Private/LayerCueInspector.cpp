// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "LayerCueInspector.h"

#include "CharacterProfileEditorModel.h"
#include "FrameCueDataProvider.h"
#include "FrameEventEditor.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SScrollBox.h"
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

#define LOCTEXT_NAMESPACE "LayerCueInspector"

void SLayerCueInspector::Construct(const FArguments& InArgs)
{
	Model = InArgs._Model;
	Controller = InArgs._Controller;
	TArray<FProfileToolPanelDescriptor> Descriptors;
	if (Controller.IsValid()) Controller->GetContextualPanels(Descriptors);
	TSharedRef<SVerticalBox> Sections = SNew(SVerticalBox);
	for (const FProfileToolPanelDescriptor& Descriptor : Descriptors)
	{
		const TSharedPtr<SWidget> Panel = Descriptor.TryCreateWidget();
		if (!Panel.IsValid()) continue;
		Sections->AddSlot().AutoHeight().Padding(0, 0, 0, 4)
		[
			SNew(SExpandableArea)
			.InitiallyCollapsed(false)
			.AreaTitle(Descriptor.Label)
			.ToolTipText(Descriptor.ToolTip)
			.BodyContent()[Panel.ToSharedRef()]
		];
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
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(2, 0, 2, 8)
				[
					SNew(STextBlock).Text(this, &SLayerCueInspector::GetSourceSummary)
					.AutoWrapText(true)
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					Sections
				]
			]
		]
	];
}

TSharedPtr<FFrameCueDataProvider> SLayerCueInspector::GetProviderForTests() const
{
	return Controller.IsValid() ? Controller->GetDataProviderForTests() : nullptr;
}

void SLayerCueInspector::HandleHostActivated()
{
	if (Controller.IsValid()) Controller->HandleHostActivated();
}

void SLayerCueInspector::HandleHostDeactivated()
{
	if (Controller.IsValid()) Controller->HandleHostDeactivated();
}

FText SLayerCueInspector::GetSourceSummary() const
{
	const TSharedPtr<FFrameCueDataProvider> Provider = GetProviderForTests();
	if (!Provider.IsValid() || !Provider->HasResolvedScope())
	{
		return LOCTEXT("ChooseLayer", "Choose a source layer to author Cues and Cue States. Character-wide Cues belong to the attached Profile Baseline.");
	}
	const int32 AnimationIndex = Model.IsValid() ? Model->GetSelectedFlipbookIndex() : INDEX_NONE;
	const TArray<TObjectPtr<UPaper2DPlusCueBase>>* Source = Provider->GetCues(AnimationIndex);
	const TArray<TObjectPtr<UPaper2DPlusCueBase>>* Lower = Provider->GetLowerCues(AnimationIndex);
	const TArray<TObjectPtr<UPaper2DPlusCueBase>>* Final = Provider->GetFinalCues(AnimationIndex);
	return FText::Format(
		LOCTEXT("CueSourceSummary", "Editing {0} · Source {1} · Lower {2} · Final {3}. Lower/Final are read-only projections; Add, drag, resize, duplicate, remove, Details, preview, and Resync all target Source."),
		Provider->GetScopeDisplayText(),
		FText::AsNumber(Source ? Source->Num() : 0),
		FText::AsNumber(Lower ? Lower->Num() : 0),
		FText::AsNumber(Final ? Final->Num() : 0));
}

#undef LOCTEXT_NAMESPACE
