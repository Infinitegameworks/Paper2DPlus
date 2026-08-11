// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "CombatProfileEditor/CombatScorePlaygroundPanel.h"

#include "CombatProfileEditor/CombatLabPanel.h"
#include "CombatProfileEditor/CombatProfileEditorSession.h"
#include "IDetailsView.h"
#include "IStructureDetailsView.h"
#include "Paper2DPlusCombatProfileAsset.h"
#include "ProfilePropertyRow.h"
#include "PropertyEditorModule.h"
#include "ScopedTransaction.h"
#include "UObject/StructOnScope.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
#include "EditorStyleSet.h"
#ifndef FAppStyle
#define FAppStyle FEditorStyle
#endif
#else
#include "Styling/AppStyle.h"
#endif

#define LOCTEXT_NAMESPACE "CombatScorePlaygroundPanel"

namespace
{
	// File-unique names: editor .cpp files unity-build together, so short generic constants collide.
	// Distance and both health percentages are short whole numbers — a filling value column stretched
	// three two-to-three digit numbers across the whole dock.
	constexpr float CombatPlaygroundValueWidth = 90.0f;
	// Roughly a dozen score terms before the explanation starts scrolling instead of growing.
	constexpr float CombatPlaygroundBreakdownMaxHeight = 260.0f;
}

void SCombatScorePlaygroundPanel::Construct(const FArguments& InArgs)
{
	Session = InArgs._Session;
	LabModel = InArgs._LabModel;
	if (Session.IsValid())
	{
		DataChangedHandle = Session->OnDataChanged().AddSP(
			SharedThis(this), &SCombatScorePlaygroundPanel::RefreshFromAsset);
		ScoreChangedHandle = Session->OnScoreChanged().AddSP(
			SharedThis(this), &SCombatScorePlaygroundPanel::RefreshScores);
		SelectionChangedHandle = Session->OnSelectionChanged().AddLambda(
			[WeakThis = TWeakPtr<SCombatScorePlaygroundPanel>(SharedThis(this))](const FProfileItemIdentity&)
			{
				if (const TSharedPtr<SCombatScorePlaygroundPanel> Pinned = WeakThis.Pin())
				{
					Pinned->SynchronizeSelection();
					Pinned->RebuildBreakdown();
				}
			});
	}

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().FillHeight(1.f).Padding(5.f)
		[
			SNew(SSplitter)
			+ SSplitter::Slot().Value(0.42f)
			[
				// The scenario controls are built ONCE and live outside ContextHost: BuildContextEditor
				// re-runs on every preset load, and the refreshers hold these combos by pointer.
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					BuildScenarioSection()
				]
				+ SVerticalBox::Slot().FillHeight(1.f)
				[
					SAssignNew(ContextHost, SBox)
				]
			]
			+ SSplitter::Slot().Value(0.58f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(4.f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("RankedTitle", "Ranked attacks — select one to inspect every score term"))
					.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 10))
				]
				// The ranked list takes every row it can get; the explanation below hugs its content.
				// A fixed 45/55 split left the breakdown as a half-empty bordered box at tall window
				// sizes, which reads as broken rather than as "nothing is selected".
				+ SVerticalBox::Slot().FillHeight(1.f)
				[
					SAssignNew(ListView, SListView<FRowPtr>)
					.ListItemsSource(&Rows)
					.SelectionMode(ESelectionMode::Single)
					.OnGenerateRow(this, &SCombatScorePlaygroundPanel::GenerateRow)
					.OnSelectionChanged(this, &SCombatScorePlaygroundPanel::HandleRowSelected)
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(4.f)
				[
					SNew(SBorder)
					.Padding(5.f)
					.BorderImage(FAppStyle::Get().GetBrush(TEXT("ToolPanel.GroupBorder")))
					[
						// The cap is what gives the scroll box a bound to scroll against — without it an
						// AutoHeight slot would grow with the term list and squeeze the ranked list.
						SNew(SBox)
						.MaxDesiredHeight(CombatPlaygroundBreakdownMaxHeight)
						[
							SNew(SScrollBox)
							+ SScrollBox::Slot()
							[
								SAssignNew(BreakdownBox, SVerticalBox)
							]
						]
					]
				]
			]
		]
	];
	RefreshScoringProfileOptions();
	RefreshPresetOptions();
	BuildContextEditor();
	RefreshScores();
}

SCombatScorePlaygroundPanel::~SCombatScorePlaygroundPanel()
{
	if (Session.IsValid())
	{
		Session->OnDataChanged().Remove(DataChangedHandle);
		Session->OnScoreChanged().Remove(ScoreChangedHandle);
		Session->OnSelectionChanged().Remove(SelectionChangedHandle);
	}
}

FText SCombatScorePlaygroundPanel::GetSituationSummary() const
{
	if (!Session.IsValid())
	{
		return FText::GetEmpty();
	}
	const FPaper2DPlusCombatRuntimeContext& Context = Session->GetPreviewContext();
	return FText::Format(
		LOCTEXT(
			"SituationSummaryFmt",
			"The target is {0} units away. I'm at {1}% health, the target at {2}%."),
		FText::AsNumber(FMath::RoundToInt(Context.DistanceToTarget)),
		FText::AsNumber(FMath::RoundToInt(Context.SelfHealthPercent * 100.0f)),
		FText::AsNumber(FMath::RoundToInt(Context.TargetHealthPercent * 100.0f)));
}

void SCombatScorePlaygroundPanel::ApplyContextEdit(
	TFunctionRef<void(FPaper2DPlusCombatRuntimeContext&)> Mutator)
{
	if (!Session.IsValid())
	{
		return;
	}
	bApplyingContext = true;
	Mutator(Session->EditPreviewContext());
	// The Advanced grid edits an owning COPY and commits the whole struct back. Re-sync it here or a
	// later tag/recent-move edit would copy its stale distance/health over what these rows just set.
	if (ContextScope.IsValid())
	{
		FPaper2DPlusCombatRuntimeContext::StaticStruct()->CopyScriptStruct(
			ContextScope->GetStructMemory(), &Session->GetPreviewContext());
	}
	Session->RefreshScores();
	bApplyingContext = false;
	RefreshScores();
}

TSharedRef<SWidget> SCombatScorePlaygroundPanel::MakeSituationRow(
	const FText& Label,
	const FText& Tooltip,
	TSharedRef<SWidget> Editor,
	float ValueWidth)
{
	// The shared details-row helper every other hand-rolled panel uses: one draggable name/value
	// column, grid line, and hover tint. It matters most here because the real property grid in the
	// Advanced area directly below these rows is the thing they have to line up with.
	return ValueWidth > 0.0f
		? FProfilePropertyRowUtils::MakeRow(Label, MoveTemp(Editor), Tooltip, ValueWidth, ValueWidth)
		: FProfilePropertyRowUtils::MakeRow(Label, MoveTemp(Editor), Tooltip);
}

TSharedRef<SWidget> SCombatScorePlaygroundPanel::BuildScenarioSection()
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SBox)
			.ToolTipText(LOCTEXT(
				"ScenarioTip",
				"Which rules do the scoring, and the saved situations you can reload. Presets are the "
				"only part of this panel that is saved to the asset."))
			[
				FProfilePropertyRowUtils::MakeSectionTitle(LOCTEXT("ScenarioHeader", "Scenario"))
			]
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			MakeSituationRow(
				LOCTEXT("ScoringProfileLabel", "Scoring profile"),
				LOCTEXT("ScoringProfileTip", "Which set of scoring rules ranks the attacks."),
				SAssignNew(ScoringProfileCombo, SComboBox<TSharedPtr<FName>>)
				.OptionsSource(&ScoringProfileOptions)
				.OnGenerateWidget_Lambda([](TSharedPtr<FName> Name)
				{
					return SNew(STextBlock)
						.Text(Name.IsValid() ? FText::FromName(*Name) : FText::GetEmpty())
						.Font(FProfilePropertyRowUtils::GetPropertyFont());
				})
				.OnSelectionChanged_Lambda([this](TSharedPtr<FName> Name, ESelectInfo::Type)
				{
					SelectedScoringProfile = MoveTemp(Name);
					if (Session.IsValid() && SelectedScoringProfile.IsValid()) Session->SetScoringProfileName(*SelectedScoringProfile);
				})
				[
					SNew(STextBlock)
					.Font(FProfilePropertyRowUtils::GetPropertyFont())
					.Text_Lambda([this]()
					{
						return SelectedScoringProfile.IsValid() ? FText::FromName(*SelectedScoringProfile) : LOCTEXT("DefaultScoring", "Default");
					})
				])
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			MakeSituationRow(
				LOCTEXT("PresetLabel", "Scenario preset"),
				LOCTEXT("PresetTip", "A saved situation. Load replaces the values below; Save current overwrites the selected preset."),
				SAssignNew(PresetCombo, SComboBox<TSharedPtr<FName>>)
				.OptionsSource(&PresetOptions)
				.OnGenerateWidget_Lambda([](TSharedPtr<FName> Name)
				{
					return SNew(STextBlock)
						.Text(Name.IsValid() ? FText::FromName(*Name) : FText::GetEmpty())
						.Font(FProfilePropertyRowUtils::GetPropertyFont());
				})
				.OnSelectionChanged_Lambda([this](TSharedPtr<FName> Name, ESelectInfo::Type) { SelectedPreset = MoveTemp(Name); })
				[
					SNew(STextBlock)
					.Font(FProfilePropertyRowUtils::GetPropertyFont())
					.Text_Lambda([this]()
					{
						return SelectedPreset.IsValid() ? FText::FromName(*SelectedPreset) : LOCTEXT("NoPreset", "New scenario");
					})
				])
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right).Padding(8.f, 4.f, 8.f, 2.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 4.f, 0.f)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), TEXT("FlatButton.Default"))
				.Text(LOCTEXT("Load", "Load"))
				.ToolTipText(LOCTEXT("LoadTip", "Replace the situation below with the selected preset."))
				.OnClicked(this, &SCombatScorePlaygroundPanel::LoadPreset)
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), TEXT("FlatButton.Default"))
				.Text(LOCTEXT("Save", "Save current"))
				.ToolTipText(LOCTEXT("SaveTip", "Save the situation below onto the selected preset, or a new one when none is selected."))
				.OnClicked(this, &SCombatScorePlaygroundPanel::SavePreset)
			]
		];
}

TSharedRef<SWidget> SCombatScorePlaygroundPanel::BuildSituationSection()
{
	// The three values a designer changes constantly get plain-language rows; everything else stays
	// available in the collapsed Advanced grid below rather than leading with a raw struct dump.
	auto MakePercentBox = [this](bool bSelf)
	{
		return SNew(SSpinBox<float>)
			.MinValue(0.f).MaxValue(100.f).Delta(1.f)
			// BOTH digit bounds: ToString takes Max(MaxFractionalDigits, MinFractionalDigits), and
			// Min defaults to 1 — capping only the maximum still renders "100.0".
			.MinFractionalDigits(0).MaxFractionalDigits(0)
			.Value_Lambda([this, bSelf]()
			{
				if (!Session.IsValid()) return 100.f;
				const FPaper2DPlusCombatRuntimeContext& C = Session->GetPreviewContext();
				return (bSelf ? C.SelfHealthPercent : C.TargetHealthPercent) * 100.f;
			})
			.OnValueCommitted_Lambda([this, bSelf](float NewValue, ETextCommit::Type)
			{
				ApplyContextEdit([bSelf, NewValue](FPaper2DPlusCombatRuntimeContext& C)
				{
					const float Normalized = FMath::Clamp(NewValue / 100.f, 0.f, 1.f);
					if (bSelf) { C.SelfHealthPercent = Normalized; }
					else       { C.TargetHealthPercent = Normalized; }
				});
			});
	};

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SBox)
			.ToolTipText(LOCTEXT("SituationTip", "The made-up combat moment the ranking is scored against. Nothing here is saved to the asset unless you save a Scenario preset."))
			[
				FProfilePropertyRowUtils::MakeSectionTitle(LOCTEXT("SituationHeader", "Situation"))
			]
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			MakeSituationRow(
				LOCTEXT("DistanceLabel", "Distance to target"),
				LOCTEXT("DistanceTip", "How far the target is, in world units. Compare it with each attack's reach."),
				SNew(SSpinBox<float>)
				.MinValue(0.f).Delta(5.f)
				.MinFractionalDigits(0).MaxFractionalDigits(0)
				.Value_Lambda([this]()
				{
					return Session.IsValid() ? Session->GetPreviewContext().DistanceToTarget : 0.f;
				})
				.OnValueCommitted_Lambda([this](float NewValue, ETextCommit::Type)
				{
					ApplyContextEdit([NewValue](FPaper2DPlusCombatRuntimeContext& C)
					{
						C.DistanceToTarget = NewValue;
					});
				}),
				CombatPlaygroundValueWidth)
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			MakeSituationRow(
				LOCTEXT("SelfHealthLabel", "My health (%)"),
				LOCTEXT("SelfHealthTip", "The attacker's health as a percentage. Cautious-style rules read this."),
				MakePercentBox(true),
				CombatPlaygroundValueWidth)
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			MakeSituationRow(
				LOCTEXT("TargetHealthLabel", "Target health (%)"),
				LOCTEXT("TargetHealthTip", "The target's health as a percentage. Finisher-style rules read this."),
				MakePercentBox(false),
				CombatPlaygroundValueWidth)
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			FProfilePropertyRowUtils::MakeSectionHint(TAttribute<FText>::Create(
				TAttribute<FText>::FGetter::CreateSP(this, &SCombatScorePlaygroundPanel::GetSituationSummary)))
		];
}

void SCombatScorePlaygroundPanel::BuildContextEditor()
{
	if (!ContextHost.IsValid() || !Session.IsValid()) return;
	ContextScope = MakeShared<FStructOnScope>(FPaper2DPlusCombatRuntimeContext::StaticStruct());
	FPaper2DPlusCombatRuntimeContext::StaticStruct()->CopyScriptStruct(
		ContextScope->GetStructMemory(), &Session->GetPreviewContext());
	FDetailsViewArgs ViewArgs;
	ViewArgs.bHideSelectionTip = true;
	ViewArgs.bAllowSearch = false;
	ViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	FStructureDetailsViewArgs StructArgs;
	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
	ContextView = PropertyModule.CreateStructureDetailView(ViewArgs, StructArgs, ContextScope);
	if (IDetailsView* Grid = ContextView->GetDetailsView())
	{
		// The three headline values own bespoke rows above; the grid keeps only what has no simple
		// editor (tag containers, recent moves, runtime variables) so the two never disagree.
		Grid->SetIsPropertyVisibleDelegate(FIsPropertyVisible::CreateLambda(
			[](const FPropertyAndParent& PropertyAndParent)
			{
				if (PropertyAndParent.ParentProperties.Num() > 0)
				{
					return true;
				}
				const FName Name = PropertyAndParent.Property.GetFName();
				return Name != GET_MEMBER_NAME_CHECKED(FPaper2DPlusCombatRuntimeContext, DistanceToTarget)
					&& Name != GET_MEMBER_NAME_CHECKED(FPaper2DPlusCombatRuntimeContext, SelfHealthPercent)
					&& Name != GET_MEMBER_NAME_CHECKED(FPaper2DPlusCombatRuntimeContext, TargetHealthPercent);
			}));
		Grid->ForceRefresh();
	}
	ContextView->GetOnFinishedChangingPropertiesDelegate().AddSP(
		SharedThis(this), &SCombatScorePlaygroundPanel::CommitContextEdit);

	ContextHost->SetContent(
		SNew(SScrollBox)
		+ SScrollBox::Slot()
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(4.f)
			[
				BuildSituationSection()
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(4.f, 8.f, 4.f, 4.f)
			[
				SNew(SExpandableArea)
				.InitiallyCollapsed(true)
				.HeaderContent()
				[
					SNew(STextBlock)
					.Text(LOCTEXT("AdvancedSituation", "Roles, states & recent moves"))
					.ToolTipText(LOCTEXT("AdvancedSituationTip", "Role and state tags, recently used moves, and runtime variable values for this made-up moment."))
					.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 9))
				]
				.BodyContent()
				[
					ContextView->GetWidget().ToSharedRef()
				]
			]
		]);
}

void SCombatScorePlaygroundPanel::CommitContextEdit(const FPropertyChangedEvent&)
{
	if (!Session.IsValid() || !ContextScope.IsValid()) return;
	bApplyingContext = true;
	FPaper2DPlusCombatRuntimeContext::StaticStruct()->CopyScriptStruct(
		&Session->EditPreviewContext(), ContextScope->GetStructMemory());
	Session->RefreshScores();
	bApplyingContext = false;
	RefreshScores();
}

void SCombatScorePlaygroundPanel::RefreshFromAsset()
{
	RefreshScoringProfileOptions();
	RefreshPresetOptions();
	RefreshScores();
}

void SCombatScorePlaygroundPanel::RefreshScoringProfileOptions()
{
	// The shared session is canonical: presets, the toolkit, and other panels can
	// change the profile without touching this combo's cached shared pointer.
	const FName SessionProfile = Session.IsValid()
		? Session->GetScoringProfileName()
		: (SelectedScoringProfile.IsValid() ? *SelectedScoringProfile : NAME_None);
	const FName DesiredProfile = SessionProfile.IsNone() ? FName(TEXT("Default")) : SessionProfile;
	ScoringProfileOptions.Reset();
	ScoringProfileOptions.Add(MakeShared<FName>(FName(TEXT("Default"))));
	const UPaper2DPlusCombatProfileAsset* Asset = Session.IsValid() ? Session->GetAsset() : nullptr;
	if (Asset)
	{
		for (const FPaper2DPlusCombatScoringProfile& Profile : Asset->ScoringProfiles)
		{
			if (!Profile.ProfileName.IsNone() && !Profile.ProfileName.IsEqual(TEXT("Default"), ENameCase::IgnoreCase))
			{
				ScoringProfileOptions.Add(MakeShared<FName>(Profile.ProfileName));
			}
		}
	}
	SelectedScoringProfile = ScoringProfileOptions[0];
	for (const TSharedPtr<FName>& Name : ScoringProfileOptions)
	{
		if (Name.IsValid() && *Name == DesiredProfile) { SelectedScoringProfile = Name; break; }
	}
	if (ScoringProfileCombo.IsValid())
	{
		ScoringProfileCombo->RefreshOptions();
		ScoringProfileCombo->SetSelectedItem(SelectedScoringProfile);
	}
}

void SCombatScorePlaygroundPanel::RefreshScores()
{
	if (bApplyingContext) return;
	if (Session.IsValid())
	{
		const FName SessionProfile = Session->GetScoringProfileName().IsNone()
			? FName(TEXT("Default"))
			: Session->GetScoringProfileName();
		if (!SelectedScoringProfile.IsValid() || *SelectedScoringProfile != SessionProfile)
		{
			RefreshScoringProfileOptions();
		}
	}
	Rows.Reset();
	if (Session.IsValid())
	{
		for (const FPaper2DPlusCombatRankedOption& Option : Session->GetRankedOptions())
		{
			Rows.Add(MakeShared<FPaper2DPlusCombatRankedOption>(Option));
		}
	}
	if (ListView.IsValid()) ListView->RequestListRefresh();
	SynchronizeSelection();
	RebuildBreakdown();
}

void SCombatScorePlaygroundPanel::RefreshPresetOptions()
{
	const FName Previous = SelectedPreset.IsValid() ? *SelectedPreset : NAME_None;
	PresetOptions.Reset();
	const UPaper2DPlusCombatProfileAsset* Asset = Session.IsValid() ? Session->GetAsset() : nullptr;
	if (Asset)
	{
		for (const FPaper2DPlusCombatScenarioPreset& Preset : Asset->ScenarioPresets)
		{
			if (!Preset.PresetName.IsNone()) PresetOptions.Add(MakeShared<FName>(Preset.PresetName));
		}
	}
	SelectedPreset.Reset();
	for (const TSharedPtr<FName>& Name : PresetOptions)
	{
		if (Name.IsValid() && *Name == Previous) { SelectedPreset = Name; break; }
	}
	if (!SelectedPreset.IsValid() && PresetOptions.Num() > 0) SelectedPreset = PresetOptions[0];
	if (PresetCombo.IsValid())
	{
		PresetCombo->RefreshOptions();
		if (SelectedPreset.IsValid()) PresetCombo->SetSelectedItem(SelectedPreset);
		else PresetCombo->ClearSelection();
	}
}

void SCombatScorePlaygroundPanel::SynchronizeSelection()
{
	if (!ListView.IsValid() || bSynchronizingSelection) return;
	TGuardValue<bool> Guard(bSynchronizingSelection, true);
	const FProfileItemIdentity Identity = Session.IsValid() ? Session->GetSelectedAttack() : FProfileItemIdentity();
	const FRowPtr* Match = Rows.FindByPredicate([this, &Identity](const FRowPtr& Row)
	{
		return Row.IsValid() && Session.IsValid() && Session->MakeAttackIdentity(Row->Attack.MoveName).Matches(Identity);
	});
	if (Match) ListView->SetSelection(*Match, ESelectInfo::Direct);
	else ListView->ClearSelection();
}

TSharedRef<ITableRow> SCombatScorePlaygroundPanel::GenerateRow(
	FRowPtr Row,
	const TSharedRef<STableViewBase>& OwnerTable)
{
	const int32 Rank = Rows.IndexOfByKey(Row);
	// Scores are relative, so the bar is normalised against the current best rather than an absolute
	// ceiling; that keeps "how close was second place" readable at any weighting scale.
	const float TopScore = (Rows.Num() > 0 && Rows[0].IsValid()) ? Rows[0]->Score : 0.f;
	const float Fraction = (Row.IsValid() && TopScore > KINDA_SMALL_NUMBER)
		? FMath::Clamp(Row->Score / TopScore, 0.f, 1.f)
		: 0.f;
	const bool bIsBest = (Rank == 0) && Row.IsValid() && Row->Score > KINDA_SMALL_NUMBER;

	return SNew(STableRow<FRowPtr>, OwnerTable)
		.Padding(FMargin(0.f, 1.f))
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6.f, 4.f, 4.f, 4.f)
		[
			SNew(SBox).WidthOverride(26.f)
			[
				SNew(STextBlock)
				.Text(FText::Format(LOCTEXT("RankFmt", "#{0}"), FText::AsNumber(Rank + 1)))
				.Font(FCoreStyle::GetDefaultFontStyle(bIsBest ? TEXT("Bold") : TEXT("Regular"), 9))
				.ColorAndOpacity(bIsBest
					? FSlateColor(FLinearColor(0.45f, 0.85f, 0.45f))
					: FSlateColor::UseSubduedForeground())
			]
		]
		+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).Padding(0.f, 4.f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock)
				.Text(Row.IsValid() ? FText::FromName(Row->Attack.MoveName) : FText::GetEmpty())
				.Font(FCoreStyle::GetDefaultFontStyle(bIsBest ? TEXT("Bold") : TEXT("Regular"), 10))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 3.f, 8.f, 0.f)
			[
				SNew(SBox).HeightOverride(4.f)
				[
					SNew(SProgressBar)
					.Percent(Fraction)
					.FillColorAndOpacity(bIsBest
						? FSlateColor(FLinearColor(0.45f, 0.85f, 0.45f))
						: FSlateColor(FLinearColor(0.45f, 0.55f, 0.75f)))
				]
			]
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6.f, 4.f, 8.f, 4.f)
		[
			SNew(STextBlock)
			.Text(Row.IsValid()
				? FText::FromString(FString::Printf(TEXT("%.3f"), Row->Score))
				: FText::GetEmpty())
			.Font(FCoreStyle::GetDefaultFontStyle(bIsBest ? TEXT("Bold") : TEXT("Regular"), 10))
			.ColorAndOpacity(Row.IsValid() && Row->Score <= KINDA_SMALL_NUMBER
				? FSlateColor(FLinearColor(0.7f, 0.55f, 0.3f))
				: FSlateColor::UseForeground())
		]
	];
}

void SCombatScorePlaygroundPanel::HandleRowSelected(FRowPtr Row, ESelectInfo::Type)
{
	if (!bSynchronizingSelection && Row.IsValid() && Session.IsValid())
	{
		Session->SelectAttackByName(Row->Attack.MoveName);
	}
	RebuildBreakdown();
}

void SCombatScorePlaygroundPanel::RebuildBreakdown()
{
	if (!BreakdownBox.IsValid()) return;
	BreakdownBox->ClearChildren();
	const FProfileItemIdentity Identity = Session.IsValid() ? Session->GetSelectedAttack() : FProfileItemIdentity();
	const FRowPtr* Match = Rows.FindByPredicate([this, &Identity](const FRowPtr& Row)
	{
		return Row.IsValid() && Session.IsValid() && Session->MakeAttackIdentity(Row->Attack.MoveName).Matches(Identity);
	});
	if (!Match)
	{
		BreakdownBox->AddSlot().AutoHeight()[SNew(STextBlock).Text(LOCTEXT("NoBreakdown", "Select a ranked attack to see its explanation."))];
		return;
	}
	BreakdownBox->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
	[
		SNew(STextBlock)
		.Text(FText::Format(LOCTEXT("ScoreHeader", "{0}: final score {1}"),
			FText::FromName((*Match)->Attack.MoveName), FText::AsNumber((*Match)->Score)))
		.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 10))
	];
	for (const FPaper2DPlusCombatScoreTerm& Term : (*Match)->Breakdown.Terms)
	{
		const FText CombineMode = Term.CombineMode == EPaper2DPlusCombatScoreCombineMode::Add
			? LOCTEXT("CombineAdd", "add")
			: LOCTEXT("CombineMultiply", "multiply");
		BreakdownBox->AddSlot().AutoHeight().Padding(0.f, 1.f)
		[
			SNew(STextBlock)
			.Text(FText::Format(LOCTEXT("TermFmt", "{0}: raw {1}, normalized {2}, weight {3}, {4}"),
				FText::FromName(Term.TermName), FText::AsNumber(Term.RawValue),
				FText::AsNumber(Term.NormalizedValue), FText::AsNumber(Term.Weight), CombineMode))
			.AutoWrapText(true)
		];
	}
}

FName SCombatScorePlaygroundPanel::MakeUniquePresetName() const
{
	const UPaper2DPlusCombatProfileAsset* Asset = Session.IsValid() ? Session->GetAsset() : nullptr;
	for (int32 Suffix = 1; Suffix < MAX_int32; ++Suffix)
	{
		const FName Candidate(*FString::Printf(TEXT("Scenario %d"), Suffix));
		if (!Asset || !Asset->ScenarioPresets.ContainsByPredicate(
			[Candidate](const FPaper2DPlusCombatScenarioPreset& Preset) { return Preset.PresetName == Candidate; }))
		{
			return Candidate;
		}
	}
	return FName(TEXT("Scenario"));
}

bool SCombatScorePlaygroundPanel::SavePresetNamed(FName PresetName)
{
	UPaper2DPlusCombatProfileAsset* Asset = Session.IsValid() ? Session->GetAsset() : nullptr;
	if (!Asset || PresetName.IsNone()) return false;
	const FScopedTransaction Transaction(LOCTEXT("SaveScenario", "Save Combat Scenario Preset"));
	Asset->Modify();
	FPaper2DPlusCombatScenarioPreset* Preset = Asset->ScenarioPresets.FindByPredicate(
		[PresetName](const FPaper2DPlusCombatScenarioPreset& Item) { return Item.PresetName == PresetName; });
	if (!Preset)
	{
		Preset = &Asset->ScenarioPresets.AddDefaulted_GetRef();
		Preset->PresetName = PresetName;
	}
	if (LabModel.IsValid()) LabModel->CaptureIntoPreset(*Preset);
	else
	{
		Preset->Context = Session->GetPreviewContext();
		Preset->ScoringProfileName = Session->GetScoringProfileName();
	}
	Asset->MarkPackageDirty();
	// Seed the value before the synchronous data refresh so the rebuilt combo can
	// restore the newly saved row by value instead of falling back to its first item.
	SelectedPreset = MakeShared<FName>(PresetName);
	Session->RefreshFromAsset();
	return true;
}

bool SCombatScorePlaygroundPanel::LoadPresetNamed(FName PresetName)
{
	const UPaper2DPlusCombatProfileAsset* Asset = Session.IsValid() ? Session->GetAsset() : nullptr;
	if (!Asset || PresetName.IsNone()) return false;
	const FPaper2DPlusCombatScenarioPreset* Preset = Asset->ScenarioPresets.FindByPredicate(
		[PresetName](const FPaper2DPlusCombatScenarioPreset& Item) { return Item.PresetName == PresetName; });
	if (!Preset) return false;
	for (const TSharedPtr<FName>& Name : PresetOptions)
	{
		if (Name.IsValid() && *Name == PresetName)
		{
			SelectedPreset = Name;
			if (PresetCombo.IsValid()) PresetCombo->SetSelectedItem(Name);
			break;
		}
	}
	if (LabModel.IsValid()) LabModel->ApplyPreset(*Preset);
	else
	{
		Session->EditPreviewContext() = Preset->Context;
		if (Session->GetScoringProfileName() != Preset->ScoringProfileName)
		{
			Session->SetScoringProfileName(Preset->ScoringProfileName);
		}
		else
		{
			Session->RefreshScores();
		}
	}
	RefreshScoringProfileOptions();
	BuildContextEditor();
	return true;
}

FReply SCombatScorePlaygroundPanel::SavePreset()
{
	SavePresetNamed(SelectedPreset.IsValid() ? *SelectedPreset : MakeUniquePresetName());
	return FReply::Handled();
}

FReply SCombatScorePlaygroundPanel::LoadPreset()
{
	if (SelectedPreset.IsValid()) LoadPresetNamed(*SelectedPreset);
	return FReply::Handled();
}

void SCombatScorePlaygroundPanel::SetContextForTests(const FPaper2DPlusCombatRuntimeContext& Context)
{
	if (!Session.IsValid()) return;
	Session->EditPreviewContext() = Context;
	Session->RefreshScores();
	BuildContextEditor();
}

bool SCombatScorePlaygroundPanel::SavePresetForTests(FName PresetName)
{
	return SavePresetNamed(PresetName);
}

bool SCombatScorePlaygroundPanel::LoadPresetForTests(FName PresetName)
{
	return LoadPresetNamed(PresetName);
}

FName SCombatScorePlaygroundPanel::GetTopMoveForTests() const
{
	return Rows.Num() > 0 && Rows[0].IsValid() ? Rows[0]->Attack.MoveName : NAME_None;
}

const FPaper2DPlusCombatRankedOption* SCombatScorePlaygroundPanel::FindRankedForTests(FName MoveName) const
{
	const FRowPtr* Match = Rows.FindByPredicate([MoveName](const FRowPtr& Row)
	{
		return Row.IsValid() && Row->Attack.MoveName == MoveName;
	});
	return Match ? Match->Get() : nullptr;
}

#undef LOCTEXT_NAMESPACE
