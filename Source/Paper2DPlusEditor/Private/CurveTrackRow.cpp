// Copyright 2026 Infinite Gameworks. All Rights Reserved.

/**
 * SCurveTrackRow — compact identity row for the shared Frame Cues curve graph.
 * The shared SCurveEditor lives in SCurveTrackStack; selection lives in the stack's focus shell,
 * and curve controls/diagnostics live in the host's contextual Details panel.
 */

#include "CurveTrackPanel.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Colors/SColorBlock.h"

// =====================================================================================================
// SCurveTrackRow
// =====================================================================================================

void SCurveTrackRow::Construct(const FArguments& InArgs)
{
	CurveName = InArgs._CurveName;

	ChildSlot
	[
		SNew(SHorizontalBox)

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(4.0f, 2.0f, 5.0f, 2.0f)
		[
			SNew(SColorBlock)
			.Color(Paper2DPlusCurveTracks::NameToColor(CurveName))
			.Size(FVector2D(10.0f, 10.0f))
		]

		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		.VAlign(VAlign_Center)
		.Padding(0.0f, 2.0f, 4.0f, 2.0f)
		[
			SNew(STextBlock)
			.Text(FText::FromName(CurveName))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
		]
	];
}
