// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "ProfileTools/SProfileSpriteBoundsTool.h"

#include "Paper2DPlusCharacterProfileAsset.h"
#include "ProfilePropertyRow.h"
#include "Styling/AppStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "Paper2DPlusProfileTools"

namespace
{
	/** True when this frame is something the designer must act on. Repairable and Unsupported both
	 *  qualify; "unsupported" means this check cannot fix it, NOT that the frame is fine. */
	bool SpriteBoundsTool_NeedsAttention(const FProfileSpriteBoundsFrameReport& Frame)
	{
		return Frame.Kind == EProfileSpriteBoundsFrameKind::Unsupported || Frame.bNeedsRepair;
	}

	FLinearColor SpriteBoundsTool_StatusColor(const FProfileSpriteBoundsFlipbookReport& Flipbook)
	{
		if (Flipbook.UnsupportedFrames > 0)
		{
			return FLinearColor(0.95f, 0.30f, 0.25f);
		}
		if (Flipbook.RepairableFrames > 0)
		{
			return FLinearColor(0.95f, 0.65f, 0.18f);
		}
		return FLinearColor(0.30f, 0.80f, 0.42f);
	}
}

void SProfileSpriteBoundsTool::Construct(const FArguments& InArgs)
{
	Profile = InArgs._Profile;
	OnRouteToReExtract = InArgs._OnRouteToReExtract;

	ChildSlot
	[
		SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 6.0f)
		[
			FProfilePropertyRowUtils::MakeSectionTitle(
				LOCTEXT("SpriteBoundsTitle", "Sprite Bounds"))
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 8.0f)
		[
			FProfilePropertyRowUtils::MakeSectionHint(
				LOCTEXT("SpriteBoundsHint",
					"Checks every frame's source region against the animation's uniform target. This check only reports — frames that need attention are fixed by re-extracting them."))
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 8.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
				.Text_Lambda([this]()
				{
					return State == EState::Results
						? LOCTEXT("SpriteBoundsRescan", "Check Again")
						: LOCTEXT("SpriteBoundsScan", "Check Sprite Bounds");
				})
				.ToolTipText(LOCTEXT("SpriteBoundsScanTip",
					"Reads source pixels for every frame. Runs only when you ask for it."))
				.IsEnabled_Lambda([this]() { return CanScan(); })
				.OnClicked(this, &SProfileSpriteBoundsTool::HandleScanClicked)
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			.Padding(10.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(this, &SProfileSpriteBoundsTool::GetSummaryText)
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			]
		]

		// Distinct in-progress indicator. Without it a long scan reads as a frozen window.
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 8.0f)
		[
			SNew(SBox)
			.HeightOverride(4.0f)
			.Visibility_Lambda([this]()
			{
				return State == EState::Scanning ? EVisibility::Visible : EVisibility::Collapsed;
			})
			[
				SNew(SProgressBar)
			]
		]

		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			[
				SAssignNew(ResultsBox, SVerticalBox)
			]
		]
	];

	RebuildResults();
}

bool SProfileSpriteBoundsTool::CanScan() const
{
	// Disabled while a scan is live so a second click cannot queue another pass.
	return State != EState::Scanning && Profile.IsValid();
}

FReply SProfileSpriteBoundsTool::HandleScanClicked()
{
	BeginScan();
	return FReply::Handled();
}

void SProfileSpriteBoundsTool::BeginScan()
{
	if (!CanScan())
	{
		return;
	}

	State = EState::Scanning;
	RebuildResults();

	// Deferred by one frame ON PURPOSE: AnalyzeProfile is synchronous, so running it inline would
	// never let the Scanning state paint and the indicator would be a lie.
	RegisterActiveTimer(0.0f, FWidgetActiveTimerDelegate::CreateLambda(
		[this](double, float)
		{
			FinishScan();
			return EActiveTimerReturnType::Stop;
		}));
}

void SProfileSpriteBoundsTool::FinishScan()
{
	Report = FProfileSpriteBoundsService::AnalyzeProfile(Profile.Get());
	State = EState::Results;
	RebuildResults();
}

void SProfileSpriteBoundsTool::RouteToReExtract(const FString& AnimationName)
{
	OnRouteToReExtract.ExecuteIfBound(AnimationName);
}

FText SProfileSpriteBoundsTool::GetSummaryText() const
{
	switch (State)
	{
	case EState::PreScan:
		return LOCTEXT("SpriteBoundsNotChecked", "Not checked yet.");
	case EState::Scanning:
		return LOCTEXT("SpriteBoundsScanning", "Reading source pixels…");
	default:
		break;
	}

	if (Report.FlipbooksChecked == 0)
	{
		return LOCTEXT("SpriteBoundsNoFlipbooks", "This profile has no animations to check.");
	}
	if (Report.AttentionFrames > 0)
	{
		return FText::Format(
			LOCTEXT("SpriteBoundsAttentionFmt", "{0} frame(s) need attention across {1} animation(s)."),
			FText::AsNumber(Report.AttentionFrames),
			FText::AsNumber(Report.FlipbooksChecked));
	}
	return FText::Format(
		LOCTEXT("SpriteBoundsCleanFmt", "All {0} frame(s) healthy."),
		FText::AsNumber(Report.HealthyFrames));
}

void SProfileSpriteBoundsTool::RebuildResults()
{
	if (!ResultsBox.IsValid())
	{
		return;
	}
	ResultsBox->ClearChildren();

	if (State == EState::PreScan)
	{
		ResultsBox->AddSlot()
		.AutoHeight()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("SpriteBoundsPreScan",
				"Press Check Sprite Bounds to scan this profile's frames."))
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			.AutoWrapText(true)
		];
		return;
	}

	if (State == EState::Scanning)
	{
		ResultsBox->AddSlot()
		.AutoHeight()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("SpriteBoundsScanningBody", "Checking every frame of every animation…"))
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
		];
		return;
	}

	for (const FProfileSpriteBoundsFlipbookReport& Flipbook : Report.Flipbooks)
	{
		const FLinearColor StatusColor = SpriteBoundsTool_StatusColor(Flipbook);
		const FString AnimationName = Flipbook.FlipbookName;
		const bool bNeedsAttention = Flipbook.AttentionFrames > 0;

		ResultsBox->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 2.0f)
		[
			FProfilePropertyRowUtils::MakeCustomRow(
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0.0f, 0.0f, 6.0f, 0.0f)
				[
					SNew(SBox)
					.WidthOverride(4.0f)
					.HeightOverride(14.0f)
					[
						SNew(SBorder)
						.BorderImage(FAppStyle::Get().GetBrush("WhiteBrush"))
						.BorderBackgroundColor(StatusColor)
					]
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock).Text(FText::FromString(AnimationName))
				],

				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(FText::Format(
						LOCTEXT("SpriteBoundsRowFmt", "{0} healthy · {1} need attention · {2} empty"),
						FText::AsNumber(Flipbook.HealthyFrames),
						FText::AsNumber(Flipbook.AttentionFrames),
						FText::AsNumber(Flipbook.EmptyFrames)))
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					// The ONLY action on an attention row: go fix it by re-extracting. Never repair.
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
					.Text(LOCTEXT("SpriteBoundsFixRow", "Re-extract…"))
					.ToolTipText(LOCTEXT("SpriteBoundsFixRowTip",
						"Open the Re-extract tool with this animation selected."))
					.Visibility(bNeedsAttention ? EVisibility::Visible : EVisibility::Collapsed)
					.OnClicked_Lambda([this, AnimationName]()
					{
						RouteToReExtract(AnimationName);
						return FReply::Handled();
					})
				])
		];

		// EVERY diagnostic reaches the user, whatever the frame's classification. The surface this
		// replaces showed them for Unsupported frames only, so a repairable frame's explanation was
		// produced and then discarded.
		for (const FProfileSpriteBoundsFrameReport& Frame : Flipbook.Frames)
		{
			if (Frame.Diagnostic.IsEmpty())
			{
				continue;
			}
			ResultsBox->AddSlot()
			.AutoHeight()
			.Padding(18.0f, 0.0f, 0.0f, 2.0f)
			[
				SNew(STextBlock)
				.Text(FText::Format(
					LOCTEXT("SpriteBoundsFrameDiagFmt", "Frame {0}: {1}"),
					FText::AsNumber(Frame.FrameIndex),
					FText::FromString(Frame.Diagnostic)))
				.ColorAndOpacity(SpriteBoundsTool_NeedsAttention(Frame)
					? FSlateColor(FLinearColor(0.95f, 0.65f, 0.18f))
					: FSlateColor::UseSubduedForeground())
				.AutoWrapText(true)
			];
		}
	}

	if (Report.Flipbooks.Num() == 0)
	{
		ResultsBox->AddSlot()
		.AutoHeight()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("SpriteBoundsEmptyProfile", "This profile has no animations to check."))
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
		];
	}
}

#if WITH_DEV_AUTOMATION_TESTS
void SProfileSpriteBoundsTool::RunScanForTests()
{
	State = EState::Scanning;
	FinishScan();
}

TArray<FString> SProfileSpriteBoundsTool::GetVisibleDiagnosticsForTests() const
{
	TArray<FString> Out;
	if (State != EState::Results)
	{
		return Out;
	}
	for (const FProfileSpriteBoundsFlipbookReport& Flipbook : Report.Flipbooks)
	{
		for (const FProfileSpriteBoundsFrameReport& Frame : Flipbook.Frames)
		{
			if (!Frame.Diagnostic.IsEmpty())
			{
				Out.Add(Frame.Diagnostic);
			}
		}
	}
	return Out;
}

bool SProfileSpriteBoundsTool::ActivateFirstAttentionRowForTests()
{
	for (const FProfileSpriteBoundsFlipbookReport& Flipbook : Report.Flipbooks)
	{
		if (Flipbook.AttentionFrames > 0)
		{
			RouteToReExtract(Flipbook.FlipbookName);
			return true;
		}
	}
	return false;
}
#endif

#undef LOCTEXT_NAMESPACE
