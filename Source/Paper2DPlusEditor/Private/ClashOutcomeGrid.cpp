// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "ClashOutcomeGrid.h"

#include "Paper2DPlusClashGraphAsset.h"
#include "Paper2DPlusClash.h"
#include "Paper2DPlusClashTypes.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Text/STextBlock.h"
// UE 5.0 compat: FAppStyle statics do not exist; use the established FEditorStyle alias.
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
#include "EditorStyleSet.h"
#ifndef FAppStyle
#define FAppStyle FEditorStyle
#define GetAppStyleSetName GetStyleSetName
#endif
#else
#include "Styling/AppStyle.h"
#endif

#define LOCTEXT_NAMESPACE "ClashOutcomeGrid"

namespace
{
	// Short label + color for an outcome (row = the "A" / winner-candidate, col = the "B").
	void ClashGrid_OutcomeVisual(EClashOutcome Outcome, FText& OutLabel, FLinearColor& OutColor)
	{
		switch (Outcome)
		{
		case EClashOutcome::AWins:  OutLabel = LOCTEXT("Win",   "beats"); OutColor = FLinearColor(0.16f, 0.45f, 0.20f); break; // green
		case EClashOutcome::BWins:  OutLabel = LOCTEXT("Lose",  "loses"); OutColor = FLinearColor(0.50f, 0.16f, 0.16f); break; // red
		case EClashOutcome::Clash:  OutLabel = LOCTEXT("Clash", "clash"); OutColor = FLinearColor(0.16f, 0.30f, 0.50f); break; // blue
		case EClashOutcome::Whiff:  OutLabel = LOCTEXT("Whiff", "whiff"); OutColor = FLinearColor(0.20f, 0.20f, 0.20f); break;
		case EClashOutcome::Trade:
		default:                    OutLabel = LOCTEXT("Trade", "trade"); OutColor = FLinearColor(0.34f, 0.30f, 0.10f); break; // amber
		}
	}

	FText ClashGrid_Leaf(const FGameplayTag& Tag)
	{
		FString S = Tag.ToString();
		int32 Dot; if (S.FindLastChar(TEXT('.'), Dot)) { S = S.RightChop(Dot + 1); }
		return FText::FromString(S);
	}
}

void SClashOutcomeGrid::Construct(const FArguments& InArgs)
{
	Asset = InArgs._Asset;

	ChildSlot
	[
		SNew(SScrollBox)
		.Orientation(Orient_Vertical)
		+ SScrollBox::Slot()
		[
			SNew(SScrollBox)
			.Orientation(Orient_Horizontal)
			+ SScrollBox::Slot()
			[
				SAssignNew(GridPanel, SGridPanel)
			]
		]
	];

	Rebuild();
}

void SClashOutcomeGrid::Rebuild()
{
	if (!GridPanel.IsValid()) return;
	GridPanel->ClearChildren();

	UPaper2DPlusClashGraphAsset* A = Asset.Get();
	if (!A) return;

	// Tag set = edge tags UNION valid TagNodePositions keys (so an added-but-unwired category appears too).
	TArray<FGameplayTag> Tags;
	A->GetConcreteEdgeTags(Tags);
#if WITH_EDITORONLY_DATA
	for (const TPair<FGameplayTag, FVector2D>& Pair : A->TagNodePositions)
	{
		if (Pair.Key.IsValid()) { Tags.AddUnique(Pair.Key); }
	}
#endif

	if (Tags.Num() == 0)
	{
		GridPanel->AddSlot(0, 0)
		[
			SNew(STextBlock).Text(LOCTEXT("Empty", "No categories yet — add 'beats' edges (or categories) to populate the grid."))
		];
		return;
	}

	// Conflict (Error) pairs from the validator -> red borders.
	TSet<TPair<FGameplayTag, FGameplayTag>> ConflictPairs;
	{
		TArray<FClashValidationIssue> Issues;
		A->ValidateClashGraphAsset(TArray<FGameplayTag>(), Issues);
		for (const FClashValidationIssue& Issue : Issues)
		{
			if (Issue.Severity == EClashValidationSeverity::Error)
			{
				ConflictPairs.Add(TPair<FGameplayTag, FGameplayTag>(Issue.CategoryA, Issue.CategoryB));
				ConflictPairs.Add(TPair<FGameplayTag, FGameplayTag>(Issue.CategoryB, Issue.CategoryA));
			}
		}
	}

	const FSlateFontInfo HeaderFont = FCoreStyle::GetDefaultFontStyle("Bold", 8);

	// Corner + headers (row 0 = column headers, col 0 = row headers; body offset by 1).
	GridPanel->AddSlot(0, 0).Padding(2)[ SNew(STextBlock).Text(LOCTEXT("Corner", "A \\ B")).Font(HeaderFont) ];
	for (int32 j = 0; j < Tags.Num(); ++j)
	{
		GridPanel->AddSlot(j + 1, 0).Padding(2).HAlign(HAlign_Center)
		[ SNew(STextBlock).Text(ClashGrid_Leaf(Tags[j])).Font(HeaderFont).ToolTipText(FText::FromString(Tags[j].ToString())) ];
	}
	for (int32 i = 0; i < Tags.Num(); ++i)
	{
		GridPanel->AddSlot(0, i + 1).Padding(2).VAlign(VAlign_Center)
		[ SNew(STextBlock).Text(ClashGrid_Leaf(Tags[i])).Font(HeaderFont).ToolTipText(FText::FromString(Tags[i].ToString())) ];
	}

	// Body cells.
	for (int32 i = 0; i < Tags.Num(); ++i)
	{
		for (int32 j = 0; j < Tags.Num(); ++j)
		{
			const EClashOutcome Outcome = Paper2DPlusClash::ResolveClash(Tags[i], Tags[j], A->Graph);
			FText Label; FLinearColor Color;
			ClashGrid_OutcomeVisual(Outcome, Label, Color);

			const bool bConflict = ConflictPairs.Contains(TPair<FGameplayTag, FGameplayTag>(Tags[i], Tags[j]));
			const FText Tip = FText::Format(LOCTEXT("CellTip", "{0} vs {1} -> {2}{3}"),
				ClashGrid_Leaf(Tags[i]), ClashGrid_Leaf(Tags[j]), Label,
				bConflict ? LOCTEXT("ConflictTip", "  (AMBIGUOUS — authoring conflict)") : FText::GetEmpty());

			GridPanel->AddSlot(j + 1, i + 1).Padding(1)
			[
				SNew(SBox).MinDesiredWidth(54.f).MinDesiredHeight(22.f)
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::Get().GetBrush("WhiteBrush"))
					.BorderBackgroundColor(bConflict ? FLinearColor(0.85f, 0.10f, 0.10f) : Color)
					.Padding(bConflict ? FMargin(2) : FMargin(0))
					.HAlign(HAlign_Center).VAlign(VAlign_Center)
					.ToolTipText(Tip)
					[
						SNew(SBorder)
						.BorderImage(FAppStyle::Get().GetBrush("WhiteBrush"))
						.BorderBackgroundColor(Color)
						.HAlign(HAlign_Center).VAlign(VAlign_Center)
						[ SNew(STextBlock).Text(Label).ColorAndOpacity(FLinearColor::White).Font(FCoreStyle::GetDefaultFontStyle("Regular", 7)) ]
					]
				]
			];
		}
	}
}

#undef LOCTEXT_NAMESPACE
