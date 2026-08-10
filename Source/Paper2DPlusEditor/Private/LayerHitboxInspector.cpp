// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "LayerHitboxInspector.h"

#include "CharacterProfileEditorModel.h"
#include "HitboxDataProvider.h"
#include "HitboxEditorPanel.h"
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

#define LOCTEXT_NAMESPACE "LayerHitboxInspector"

void SLayerHitboxInspector::Construct(const FArguments& InArgs)
{
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
			.InitiallyCollapsed(Descriptor.PanelId == SHitboxEditorPanel::FrameOperationsPanelId)
			.AreaTitle(Descriptor.Label)
			.ToolTipText(Descriptor.ToolTip)
			.BodyContent()
			[
				Panel.ToSharedRef()
			]
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
					SNew(STextBlock).Text(this, &SLayerHitboxInspector::GetProjectionSummary)
					.AutoWrapText(true).ColorAndOpacity(FSlateColor::UseSubduedForeground())
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					Sections
				]
			]
		]
	];
}

FText SLayerHitboxInspector::GetProjectionSummary() const
{
	if (!Controller.IsValid()) return LOCTEXT("Unavailable", "Hitbox authoring is unavailable.");
	const TSharedPtr<FHitboxFrameDataProvider> Provider = Controller->GetFrameDataProviderForTests();
	if (!Provider.IsValid() || !Provider->HasResolvedScope())
	{
		return LOCTEXT("ChooseLayer", "Choose a source layer. Lower and Final projections are read-only; only the selected layer is editable.");
	}
	return FText::Format(
		LOCTEXT("ProjectionSummary", "Source: {0}. Geometry is stored layer-local. Dim red/green = Lower; dim blue = compiled Final; ghosts cannot be selected."),
		Provider->GetScopeDisplayText());
}

#undef LOCTEXT_NAMESPACE

