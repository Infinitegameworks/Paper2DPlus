// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "CombatProfileEditor/CombatAttackInspectorPanel.h"

#include "CombatProfileEditor/CombatProfileEditorSession.h"
#include "Customizations/Paper2DPlusCombatConsiderationText.h"
#include "EditorCanvasUtils.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "IDetailsView.h"
#include "IStructureDetailsView.h"
#include "PaperFlipbook.h"
#include "Paper2DPlusCombatProfileAsset.h"
#include "Paper2DPlusCombatProfileTypes.h"
#include "ProfilePropertyRow.h"
#include "PropertyEditorModule.h"
#include "ScopedTransaction.h"
#include "UObject/StructOnScope.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
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

#define LOCTEXT_NAMESPACE "CombatAttackInspectorPanel"

void SCombatAttackInspectorPanel::Construct(const FArguments& InArgs)
{
	Session = InArgs._Session;
	ChildSlot
	[
		SAssignNew(Host, SBox)
	];

	if (Session.IsValid())
	{
		DataChangedHandle = Session->OnDataChanged().AddSP(
			SharedThis(this), &SCombatAttackInspectorPanel::RefreshFromSession);
		SelectionChangedHandle = Session->OnSelectionChanged().AddLambda(
			[WeakThis = TWeakPtr<SCombatAttackInspectorPanel>(SharedThis(this))](const FProfileItemIdentity&)
			{
				if (const TSharedPtr<SCombatAttackInspectorPanel> Pinned = WeakThis.Pin())
				{
					Pinned->RefreshFromSession();
				}
			});
	}
	Rebuild();
}

SCombatAttackInspectorPanel::~SCombatAttackInspectorPanel()
{
	if (Session.IsValid())
	{
		Session->OnDataChanged().Remove(DataChangedHandle);
		Session->OnSelectionChanged().Remove(SelectionChangedHandle);
	}
}

void SCombatAttackInspectorPanel::RefreshFromSession()
{
	if (!bApplyingEdit)
	{
		Rebuild();
	}
}

FText SCombatAttackInspectorPanel::GetFactsText() const
{
	const FPaper2DPlusCombatAttackDerivedData* Row = Session.IsValid()
		? Session->GetSelectedCatalogRow()
		: nullptr;
	if (!Row)
	{
		return FText::GetEmpty();
	}

	TArray<FText> Facts;
	if (!Row->ForwardRangeLocal.IsNearlyZero())
	{
		Facts.Add(FText::Format(
			LOCTEXT("FactReach", "reach {0}-{1}"),
			FText::AsNumber(FMath::RoundToInt(Row->ForwardRangeLocal.X)),
			FText::AsNumber(FMath::RoundToInt(Row->ForwardRangeLocal.Y))));
	}
	Facts.Add(Row->FrameData.ActiveFrames == 1
		? LOCTEXT("FactOneActiveFrame", "1 active frame")
		: FText::Format(LOCTEXT("FactActiveFrames", "{0} active frames"), FText::AsNumber(Row->FrameData.ActiveFrames)));
	if (Row->FrameData.MaxDamage > 0)
	{
		Facts.Add(FText::Format(LOCTEXT("FactDamage", "damage {0}"), FText::AsNumber(Row->FrameData.MaxDamage)));
	}
	if (Row->FrameData.bHasRootMotion)
	{
		Facts.Add(LOCTEXT("FactRootMotion", "root motion"));
	}
	return FText::Join(LOCTEXT("FactSeparator", " · "), Facts);
}

FText SCombatAttackInspectorPanel::GetProvenanceText() const
{
	if (!Session.IsValid())
	{
		return FText::GetEmpty();
	}
	const FCombatProfileEffectiveAttackSummary Summary = Session->GetSelectedEffectiveSummary();
	if (Summary.bHasMoveTuning)
	{
		return LOCTEXT("ProvenanceCustomized", "Customized on this profile.");
	}
	if (Summary.bHasTagDefaults)
	{
		return LOCTEXT("ProvenanceTagDefaults", "Uses its attack-tag defaults.");
	}
	return LOCTEXT("ProvenanceBuiltIn", "Uses built-in defaults.");
}

FText SCombatAttackInspectorPanel::GetScoreText() const
{
	if (!Session.IsValid() || !Session->GetSelectedAttack().IsValid())
	{
		return FText::GetEmpty();
	}
	const FName MoveName(*Session->GetSelectedAttack().FallbackKey);
	const TArray<FPaper2DPlusCombatRankedOption>& Ranked = Session->GetRankedOptions();
	for (int32 Index = 0; Index < Ranked.Num(); ++Index)
	{
		if (Ranked[Index].Attack.MoveName == MoveName)
		{
			return FText::Format(
				LOCTEXT("MoveScoreFmt", "rank #{0} · score {1}"),
				FText::AsNumber(Index + 1),
				FText::FromString(FString::Printf(TEXT("%.3f"), Ranked[Index].Score)));
		}
	}
	return LOCTEXT("MoveNotScored", "not ranked in this preview");
}

FSlateColor SCombatAttackInspectorPanel::GetScoreColor() const
{
	if (Session.IsValid() && Session->GetSelectedAttack().IsValid())
	{
		const FName MoveName(*Session->GetSelectedAttack().FallbackKey);
		const TArray<FPaper2DPlusCombatRankedOption>& Ranked = Session->GetRankedOptions();
		for (int32 Index = 0; Index < Ranked.Num(); ++Index)
		{
			if (Ranked[Index].Attack.MoveName == MoveName)
			{
				return Index == 0
					? FSlateColor(FLinearColor(0.45f, 0.85f, 0.45f))   // green = current best pick
					: FSlateColor(FLinearColor(0.78f, 0.78f, 0.86f));  // light = scored but not top
			}
		}
	}
	return FSlateColor(FLinearColor(0.7f, 0.55f, 0.3f));                // amber = filtered out of scoring
}

TSharedRef<SWidget> SCombatAttackInspectorPanel::MakeSectionHeader(const FText& Label, const FText& Tooltip)
{
	// The shared section title/hint pair rather than bold text plus a separator, so a run of rows here
	// is introduced exactly the way it is in every other Paper2D+ tool panel.
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 8.f, 0.f, 0.f)
		[
			FProfilePropertyRowUtils::MakeSectionTitle(Label)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 2.f)
		[
			FProfilePropertyRowUtils::MakeSectionHint(Tooltip)
		];
}

TSharedRef<SWidget> SCombatAttackInspectorPanel::BuildHeader(const FPaper2DPlusCombatAttackDerivedData& Row)
{
	// Small preview only when the flipbook already happens to be resident (the card grid streams
	// visible art) — the header never loads anything.
	UPaperFlipbook* ResidentFlipbook = nullptr;
	if (Session.IsValid() && !Session->GetSelectedAttack().ObjectPath.IsNull())
	{
		ResidentFlipbook = Cast<UPaperFlipbook>(Session->GetSelectedAttack().ObjectPath.ResolveObject());
	}

	FString TagLeaf;
	if (Row.AttackTag.IsValid())
	{
		const FString Full = Row.AttackTag.GetTagName().ToString();
		int32 DotIndex = INDEX_NONE;
		TagLeaf = Full.FindLastChar(TEXT('.'), DotIndex) ? Full.RightChop(DotIndex + 1) : Full;
	}

	TSharedRef<SHorizontalBox> Header = SNew(SHorizontalBox);
	if (ResidentFlipbook)
	{
		Header->AddSlot().AutoWidth().VAlign(VAlign_Top).Padding(0.f, 2.f, 6.f, 0.f)
		[
			SNew(SBox).WidthOverride(40.f).HeightOverride(40.f)
			[
				SNew(SFlipbookThumbnail).Flipbook(ResidentFlipbook)
			]
		];
	}
	Header->AddSlot().FillWidth(1.f)
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(FText::FromName(Row.MoveName))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 13))
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6.f, 0.f, 0.f, 0.f)
			[
				SNew(STextBlock)
				.Text(TagLeaf.IsEmpty() ? LOCTEXT("HeaderUntagged", "untagged") : FText::FromString(TagLeaf))
				.ToolTipText(Row.AttackTag.IsValid()
					? FText::FromString(Row.AttackTag.GetTagName().ToString())
					: LOCTEXT("HeaderUntaggedTip", "This move has no Attack Tag on the Character Profile."))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.78f, 0.78f, 0.86f)))
			]
			+ SHorizontalBox::Slot().FillWidth(1.f).HAlign(HAlign_Right).VAlign(VAlign_Center).Padding(8.f, 0.f, 0.f, 0.f)
			[
				// Live score for this move in the current Score Playground situation — updates as you edit.
				SNew(STextBlock)
				.Text(this, &SCombatAttackInspectorPanel::GetScoreText)
				.ColorAndOpacity(this, &SCombatAttackInspectorPanel::GetScoreColor)
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
				.ToolTipText(LOCTEXT("ScoreBadgeTip", "This attack's rank and score for the current Score Playground situation. Edits below rescore instantly."))
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f, 0.f, 0.f)
		[
			SNew(STextBlock)
			.Text(GetFactsText())
			.ToolTipText(LOCTEXT("FactsTip", "Physical facts derived from the Character Profile. Edit hitboxes, frames, and root motion there — this asset only tunes how the AI ranks the attack."))
			.AutoWrapText(true)
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f, 0.f, 0.f)
		[
			SNew(STextBlock)
			.Text(GetProvenanceText())
			.Font(FCoreStyle::GetDefaultFontStyle("Italic", 8))
			.ColorAndOpacity(FSlateColor(FLinearColor(0.65f, 0.65f, 0.65f)))
		]
	];
	return Header;
}

TSharedRef<SWidget> SCombatAttackInspectorPanel::BuildEffectiveRulesSection()
{
	TSharedRef<SVerticalBox> List = SNew(SVerticalBox);
	int32 Count = 0;

	auto AddRule = [&List, &Count](const FText& LevelLabel, const FLinearColor& LevelColor, const FPaper2DPlusCombatConsideration& Rule)
	{
		List->AddSlot().AutoHeight().Padding(0.f, 1.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top)
			[
				SNew(SBox).WidthOverride(48.f)
				[
					SNew(STextBlock).Text(LevelLabel).ColorAndOpacity(FSlateColor(LevelColor)).Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
				]
			]
			+ SHorizontalBox::Slot().FillWidth(1.f)
			[
				SNew(STextBlock).Text(Paper2DPlusCombatText::SummarizeConsideration(Rule)).AutoWrapText(true).Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
			]
		];
		++Count;
	};

	const UPaper2DPlusCombatProfileAsset* Asset = Session.IsValid() ? Session->GetAsset() : nullptr;
	const FPaper2DPlusCombatAttackDerivedData* Row = Session.IsValid() ? Session->GetSelectedCatalogRow() : nullptr;
	if (Asset && Row)
	{
		// Rules apply in layers: every attack shares the active scoring profile's global rules, moves
		// with this attack tag share the tag rules, and this move's own rules apply only to it.
		if (const FPaper2DPlusCombatScoringProfile* ActiveProfile = Asset->FindScoringProfile(Session->GetScoringProfileName()))
		{
			for (const FPaper2DPlusCombatConsideration& Rule : ActiveProfile->GlobalConsiderations)
			{
				AddRule(LOCTEXT("LevelGlobal", "all"), FLinearColor(0.55f, 0.70f, 0.95f), Rule);
			}
		}
		if (Row->AttackTag.IsValid())
		{
			for (const FPaper2DPlusCombatTagDefaults& Defaults : Asset->TagDefaults)
			{
				if (Defaults.AttackTag == Row->AttackTag)
				{
					for (const FPaper2DPlusCombatConsideration& Rule : Defaults.Considerations)
					{
						AddRule(LOCTEXT("LevelTag", "tag"), FLinearColor(0.78f, 0.62f, 0.95f), Rule);
					}
				}
			}
		}
		if (const FPaper2DPlusCombatAttackOption* Option = Session->GetSelectedOption())
		{
			for (const FPaper2DPlusCombatConsideration& Rule : Option->Considerations)
			{
				AddRule(LOCTEXT("LevelMove", "move"), FLinearColor(0.55f, 0.85f, 0.55f), Rule);
			}
		}
	}

	if (Count == 0)
	{
		return SNew(STextBlock)
			.AutoWrapText(true)
			.Text(LOCTEXT("NoRulesYet", "No scoring rules yet — this attack scores the same in every situation. Add one with + Scoring Rule."))
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
			.ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f)));
	}

	return SNew(SExpandableArea)
		.InitiallyCollapsed(false)
		.HeaderContent()
		[
			SNew(STextBlock)
			.Text(FText::Format(LOCTEXT("EffectiveRulesHeader", "Scoring rules that apply to this attack ({0})"), FText::AsNumber(Count)))
			.ToolTipText(LOCTEXT("EffectiveRulesTip", "\"all\" rules come from the active scoring profile, \"tag\" rules from this attack's tag defaults, and \"move\" rules belong to this attack alone."))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
		]
		.BodyContent()
		[
			SNew(SBox).Padding(4.f)
			[
				List
			]
		];
}

TSharedRef<SWidget> SCombatAttackInspectorPanel::BuildAddRuleMenu()
{
	return SNew(SComboButton)
		.ComboButtonStyle(FAppStyle::Get(), "ComboButton")
		.ToolTipText(LOCTEXT("AddRuleTip", "Add a scoring rule from a template. If this attack still inherits everything, it is customized first (one undo step)."))
		.ButtonContent()
		[
			SNew(STextBlock).Text(LOCTEXT("AddRule", "+ Scoring Rule"))
		]
		.OnGetMenuContent_Lambda([this]()
		{
			FMenuBuilder Menu(true, nullptr);
			Menu.BeginSection(NAME_None, LOCTEXT("TemplatesHeader", "Common rules"));
			auto AddItem = [this, &Menu](int32 TemplateIndex, const FText& Label, const FText& Tip)
			{
				Menu.AddMenuEntry(Label, Tip, FSlateIcon(),
					FUIAction(FExecuteAction::CreateSP(this,
						&SCombatAttackInspectorPanel::AddConsiderationFromTemplate, TemplateIndex)));
			};
			AddItem(0, LOCTEXT("TplRange", "Prefer this attack's range"),
				LOCTEXT("TplRangeTip", "Score peaks when the target sits inside this move's reach."));
			AddItem(1, LOCTEXT("TplFinisher", "Finisher — when target is low HP"),
				LOCTEXT("TplFinisherTip", "Score rises as the target's health drops below ~40%."));
			AddItem(2, LOCTEXT("TplCautious", "Cautious — when I'm hurt"),
				LOCTEXT("TplCautiousTip", "Score drops as my own health falls."));
			AddItem(3, LOCTEXT("TplTargetState", "Only when target is in a state…"),
				LOCTEXT("TplTargetStateTip", "Score only when the target has the given state tags (fill in the Required Tags below)."));
			AddItem(4, LOCTEXT("TplVarFloat", "Gate on a number variable…"),
				LOCTEXT("TplVarFloatTip", "Score scales with a custom float variable (pick its tag)."));
			AddItem(5, LOCTEXT("TplVarBool", "Gate on a flag variable…"),
				LOCTEXT("TplVarBoolTip", "Score only when a custom bool variable matches (pick its tag)."));
			Menu.EndSection();
			return Menu.MakeWidget();
		});
}

void SCombatAttackInspectorPanel::AddConsiderationFromTemplate(int32 TemplateIndex)
{
	const FPaper2DPlusCombatAttackDerivedData* Row = Session.IsValid()
		? Session->GetSelectedCatalogRow()
		: nullptr;
	if (!Row)
	{
		return;
	}

	// Build the prefilled rule.
	FPaper2DPlusCombatConsideration Rule;
	switch (TemplateIndex)
	{
	case 0: // Prefer this attack's range
	{
		Rule.ConsiderationName = TEXT("PreferRange");
		Rule.Source = EPaper2DPlusCombatConsiderationSource::DistanceToTarget;
		Rule.Operation = EPaper2DPlusCombatConsiderationOp::RangeWindow;
		FVector2D Range(50.f, 250.f);
		if (!Row->PreferredRangeLocal.IsZero())        { Range = Row->PreferredRangeLocal; }
		else if (!Row->ForwardRangeLocal.IsZero())     { Range = Row->ForwardRangeLocal; }
		Rule.MinValue = Range.X;
		Rule.MaxValue = Range.Y;
		break;
	}
	case 1: // Finisher when target low HP
		Rule.ConsiderationName = TEXT("Finisher");
		Rule.Source = EPaper2DPlusCombatConsiderationSource::TargetHealthPercent;
		Rule.Operation = EPaper2DPlusCombatConsiderationOp::LinearInverse;
		Rule.MinValue = 0.0f;
		Rule.MaxValue = 0.4f;
		Rule.Weight = 1.5f;
		break;
	case 2: // Cautious when I'm hurt
		Rule.ConsiderationName = TEXT("Cautious");
		Rule.Source = EPaper2DPlusCombatConsiderationSource::SelfHealthPercent;
		Rule.Operation = EPaper2DPlusCombatConsiderationOp::Linear;
		Rule.MinValue = 0.2f;
		Rule.MaxValue = 0.7f;
		break;
	case 3: // Only when the target is in a given state (TargetStateTags reads RequiredTags directly).
		Rule.ConsiderationName = TEXT("TargetStateGate");
		Rule.Source = EPaper2DPlusCombatConsiderationSource::TargetStateTags;
		Rule.Operation = EPaper2DPlusCombatConsiderationOp::TagAny;
		break;
	case 4: // Gate on a float variable
		Rule.ConsiderationName = TEXT("VarGate");
		Rule.Source = EPaper2DPlusCombatConsiderationSource::CustomFloat;
		Rule.Operation = EPaper2DPlusCombatConsiderationOp::Linear;
		Rule.MinValue = 0.0f;
		Rule.MaxValue = 1.0f;
		break;
	case 5: // Gate on a bool variable
		Rule.ConsiderationName = TEXT("FlagGate");
		Rule.Source = EPaper2DPlusCombatConsiderationSource::CustomBool;
		Rule.Operation = EPaper2DPlusCombatConsiderationOp::BoolEquals;
		Rule.bExpectedBool = true;
		break;
	default:
		return;
	}

	// One undo step even when the move still inherits: the outer transaction absorbs the session's
	// nested Customize/Commit transactions.
	const FScopedTransaction Transaction(LOCTEXT("AddRuleTransaction", "Add Combat Scoring Rule"));
	bApplyingEdit = true;
	if (const FPaper2DPlusCombatAttackOption* Option = Session->CustomizeSelectedAttack())
	{
		FPaper2DPlusCombatAttackOption Edited = *Option;
		Edited.Considerations.Add(Rule);
		Session->CommitSelectedOption(Edited);
	}
	bApplyingEdit = false;
	Rebuild();
}

void SCombatAttackInspectorPanel::ApplyOptionEdit(TFunctionRef<void(FPaper2DPlusCombatAttackOption&)> Mutator)
{
	const FPaper2DPlusCombatAttackOption* Option = Session.IsValid() ? Session->GetSelectedOption() : nullptr;
	if (!Option)
	{
		return;
	}
	FPaper2DPlusCombatAttackOption Edited = *Option;
	Mutator(Edited);
	bApplyingEdit = true;
	Session->CommitSelectedOption(Edited);
	bApplyingEdit = false;
	// No full Rebuild: the bespoke rows read live values through lambdas and the score badge polls,
	// so leaving the widget tree alone keeps spinbox focus and grid expansion intact. Only the
	// read-only summaries re-render.
	RefreshSummaries();
}

void SCombatAttackInspectorPanel::RefreshSummaries()
{
	const FPaper2DPlusCombatAttackDerivedData* Row = Session.IsValid()
		? Session->GetSelectedCatalogRow()
		: nullptr;
	if (!Row)
	{
		Rebuild();
		return;
	}
	if (SummaryHeaderBox.IsValid())
	{
		SummaryHeaderBox->SetContent(BuildHeader(*Row));
	}
	if (SummaryRulesBox.IsValid())
	{
		SummaryRulesBox->SetContent(BuildEffectiveRulesSection());
	}
}

TSharedRef<SWidget> SCombatAttackInspectorPanel::BuildTuningSections()
{
	TSharedRef<SVerticalBox> Sections = SNew(SVerticalBox);

	// --- Weighting ---
	Sections->AddSlot().AutoHeight()
	[
		MakeSectionHeader(
			LOCTEXT("WeightingSection", "Weighting"),
			LOCTEXT("WeightingSectionTip", "How strongly this attack competes before any scoring rules apply."))
	];
	// Rows go through the shared helper so this panel lands on the SAME draggable column grid, grid
	// line, hover tint and property font as Character/Catalog/Effect — which is the point of a
	// workspace deliberately built to mirror them.
	Sections->AddSlot().AutoHeight()
	[
		FProfilePropertyRowUtils::MakeRow(
			LOCTEXT("WeightLabel", "Weight"),
			SNew(SSpinBox<float>)
			.MinValue(0.0f)
			.Delta(0.05f)
			.Value_Lambda([this]()
			{
				const FPaper2DPlusCombatAttackOption* Option = Session.IsValid() ? Session->GetSelectedOption() : nullptr;
				return Option ? Option->BaseWeight : 1.0f;
			})
			.OnValueCommitted_Lambda([this](float NewValue, ETextCommit::Type)
			{
				ApplyOptionEdit([NewValue](FPaper2DPlusCombatAttackOption& Option)
				{
					Option.BaseWeight = NewValue;
				});
			}),
			LOCTEXT("WeightTip", "Base weight seeding this attack's score. 1.0 is neutral; higher makes the AI favor it."))
	];
	Sections->AddSlot().AutoHeight()
	[
		FProfilePropertyRowUtils::MakeRow(
			LOCTEXT("CooldownLabel", "Cooldown (seconds)"),
			SNew(SSpinBox<float>)
			.MinValue(0.0f)
			.Delta(0.1f)
			.Value_Lambda([this]()
			{
				const FPaper2DPlusCombatAttackOption* Option = Session.IsValid() ? Session->GetSelectedOption() : nullptr;
				return Option ? Option->CooldownSeconds : 0.0f;
			})
			.OnValueCommitted_Lambda([this](float NewValue, ETextCommit::Type)
			{
				ApplyOptionEdit([NewValue](FPaper2DPlusCombatAttackOption& Option)
				{
					Option.CooldownSeconds = NewValue;
				});
			}),
			LOCTEXT("CooldownTip", "Advisory cooldown the scorer respects between picks of this attack. Your game still owns real cooldowns."))
	];

	// --- Range ---
	Sections->AddSlot().AutoHeight()
	[
		MakeSectionHeader(
			LOCTEXT("RangeSection", "Range"),
			LOCTEXT("RangeSectionTip", "The distance band where this attack is considered in range. Derived from its hitboxes and root motion unless overridden."))
	];
	Sections->AddSlot().AutoHeight().Padding(0.f, 1.f)
	[
		SNew(SCheckBox)
		.IsChecked_Lambda([this]()
		{
			const FPaper2DPlusCombatAttackOption* Option = Session.IsValid() ? Session->GetSelectedOption() : nullptr;
			return Option && Option->bOverridePreferredRange ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
		})
		.OnCheckStateChanged_Lambda([this](ECheckBoxState NewState)
		{
			ApplyOptionEdit([NewState](FPaper2DPlusCombatAttackOption& Option)
			{
				Option.bOverridePreferredRange = (NewState == ECheckBoxState::Checked);
			});
		})
		.ToolTipText(LOCTEXT("OverrideRangeTip", "Replace the derived reach with a hand-tuned preferred distance band."))
		[
			SNew(STextBlock).Text(LOCTEXT("OverrideRange", "Override preferred range"))
		]
	];
	// A compound value (two spinboxes) stays at the helper's stock fill behaviour — MaxValueWidth is
	// documented as being for a SINGLE widget.
	Sections->AddSlot().AutoHeight()
	[
		FProfilePropertyRowUtils::MakeRow(
			LOCTEXT("PreferredRangeLabel", "Preferred range (min / max)"),
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(0.5f).Padding(0.f, 0.f, 2.f, 0.f)
			[
				SNew(SSpinBox<float>)
				.MinValue(0.0f)
				.IsEnabled_Lambda([this]()
				{
					const FPaper2DPlusCombatAttackOption* Option = Session.IsValid() ? Session->GetSelectedOption() : nullptr;
					return Option && Option->bOverridePreferredRange;
				})
				.Value_Lambda([this]()
				{
					const FPaper2DPlusCombatAttackOption* Option = Session.IsValid() ? Session->GetSelectedOption() : nullptr;
					return Option ? static_cast<float>(Option->PreferredRangeLocal.X) : 0.0f;
				})
				.OnValueCommitted_Lambda([this](float NewValue, ETextCommit::Type)
				{
					ApplyOptionEdit([NewValue](FPaper2DPlusCombatAttackOption& Option)
					{
						Option.PreferredRangeLocal.X = NewValue;
					});
				})
			]
			+ SHorizontalBox::Slot().FillWidth(0.5f)
			[
				SNew(SSpinBox<float>)
				.MinValue(0.0f)
				.IsEnabled_Lambda([this]()
				{
					const FPaper2DPlusCombatAttackOption* Option = Session.IsValid() ? Session->GetSelectedOption() : nullptr;
					return Option && Option->bOverridePreferredRange;
				})
				.Value_Lambda([this]()
				{
					const FPaper2DPlusCombatAttackOption* Option = Session.IsValid() ? Session->GetSelectedOption() : nullptr;
					return Option ? static_cast<float>(Option->PreferredRangeLocal.Y) : 0.0f;
				})
				.OnValueCommitted_Lambda([this](float NewValue, ETextCommit::Type)
				{
					ApplyOptionEdit([NewValue](FPaper2DPlusCombatAttackOption& Option)
					{
						Option.PreferredRangeLocal.Y = NewValue;
					});
				})
			],
			LOCTEXT("PreferredRangeTip", "The hand-tuned distance band this attack prefers, used instead of the derived reach while the override above is on."))
	];

	// --- Eligibility ---
	Sections->AddSlot().AutoHeight()
	[
		MakeSectionHeader(
			LOCTEXT("EligibilitySection", "Eligibility"),
			LOCTEXT("EligibilitySectionTip", "When this attack is allowed into scoring at all."))
	];
	Sections->AddSlot().AutoHeight().Padding(0.f, 1.f)
	[
		SNew(SCheckBox)
		.IsChecked_Lambda([this]()
		{
			const FPaper2DPlusCombatAttackOption* Option = Session.IsValid() ? Session->GetSelectedOption() : nullptr;
			return Option && Option->bIncludeWhenNotTagged ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
		})
		.OnCheckStateChanged_Lambda([this](ECheckBoxState NewState)
		{
			ApplyOptionEdit([NewState](FPaper2DPlusCombatAttackOption& Option)
			{
				Option.bIncludeWhenNotTagged = (NewState == ECheckBoxState::Checked);
			});
		})
		.ToolTipText(LOCTEXT("IncludeUntaggedTip", "Normally only moves with an Attack Tag are eligible. Check this to include the move even without one."))
		[
			SNew(STextBlock).Text(LOCTEXT("IncludeUntagged", "Include even when the move has no Attack Tag"))
		]
	];

	// --- Deep collections: this move's own rules, tags, and variable overrides ---
	Sections->AddSlot().AutoHeight()
	[
		MakeSectionHeader(
			LOCTEXT("CollectionsSection", "Rules, Tags & Variables"),
			LOCTEXT("CollectionsSectionTip", "This move's own scoring rules, its tag assignments, and sparse variable overrides. Missing variables inherit from tag defaults, then globals."))
	];
	// Without this the grid's own empty Attack Tag reads as a contradiction of the effective tag in
	// the header above — they are different scopes, not disagreeing values.
	Sections->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
	[
		SNew(STextBlock)
		.AutoWrapText(true)
		.Text(LOCTEXT("CollectionsSectionHint", "These are this move's own values. Leaving Attack Tag or Role Tags empty keeps the ones shown at the top, and missing variables fall back to tag defaults, then globals."))
		.Font(FCoreStyle::GetDefaultFontStyle("Italic", 8))
		.ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f)))
	];

	const FPaper2DPlusCombatAttackOption* Option = Session.IsValid() ? Session->GetSelectedOption() : nullptr;
	if (Option)
	{
		StructureScope = MakeShared<FStructOnScope>(FPaper2DPlusCombatAttackOption::StaticStruct());
		FPaper2DPlusCombatAttackOption::StaticStruct()->CopyScriptStruct(
			StructureScope->GetStructMemory(), Option);
		FDetailsViewArgs ViewArgs;
		ViewArgs.bHideSelectionTip = true;
		ViewArgs.bAllowSearch = false;
		ViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
		FStructureDetailsViewArgs StructArgs;
		FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
		StructureView = PropertyModule.CreateStructureDetailView(ViewArgs, StructArgs, StructureScope);
		if (IDetailsView* GridView = StructureView->GetDetailsView())
		{
			// The grid owns only the deep collections; identity and the bespoke-row fields above never
			// render as editable properties (Move Name / Move Flipbook edits used to silently relink rows).
			GridView->SetIsPropertyVisibleDelegate(FIsPropertyVisible::CreateLambda(
				[](const FPropertyAndParent& PropertyAndParent)
				{
					if (PropertyAndParent.ParentProperties.Num() > 0)
					{
						return true; // Nested members follow their visible parent.
					}
					const FName Name = PropertyAndParent.Property.GetFName();
					return Name == GET_MEMBER_NAME_CHECKED(FPaper2DPlusCombatAttackOption, Considerations)
						|| Name == GET_MEMBER_NAME_CHECKED(FPaper2DPlusCombatAttackOption, Variables)
						|| Name == GET_MEMBER_NAME_CHECKED(FPaper2DPlusCombatAttackOption, AttackTag)
						|| Name == GET_MEMBER_NAME_CHECKED(FPaper2DPlusCombatAttackOption, RoleTags);
				}));
			GridView->ForceRefresh();
		}
		StructureView->GetOnFinishedChangingPropertiesDelegate().AddSP(
			SharedThis(this), &SCombatAttackInspectorPanel::CommitStructEdit);
		Sections->AddSlot().AutoHeight()
		[
			StructureView->GetWidget().ToSharedRef()
		];
	}

	// --- Revert ---
	Sections->AddSlot().AutoHeight().Padding(0.f, 10.f, 0.f, 0.f).HAlign(HAlign_Left)
	[
		SNew(SButton)
		.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
		.Text(LOCTEXT("RevertToInherited", "Revert to inherited defaults"))
		.ToolTipText(LOCTEXT("RevertTip", "Removes this move's tuning row (undoable). The attack scores from its attack-tag and global defaults again."))
		.OnClicked(this, &SCombatAttackInspectorPanel::RevertToInherited)
	];

	return Sections;
}

void SCombatAttackInspectorPanel::Rebuild()
{
	if (!Host.IsValid())
	{
		return;
	}
	StructureView.Reset();
	StructureScope.Reset();
	SummaryHeaderBox.Reset();
	SummaryRulesBox.Reset();

	const FPaper2DPlusCombatAttackDerivedData* Row = Session.IsValid()
		? Session->GetSelectedCatalogRow()
		: nullptr;
	if (!Row)
	{
		// Leading sentence is load-bearing: the visual tour asserts it verbatim.
		Host->SetContent(
			SNew(SBox).Padding(12.f).HAlign(HAlign_Center).VAlign(VAlign_Center)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock)
					.Text(LOCTEXT("NoSelection", "Select an attack to see how it scores and tune it."))
					.ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f)))
					.Justification(ETextJustify::Center)
					.AutoWrapText(true)
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 0.f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("NoSelectionHow", "Pick one from the Attacks list on the Overview tab, or use the current-attack control at the top of any tool."))
					.Font(FCoreStyle::GetDefaultFontStyle("Italic", 8))
					.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)))
					.Justification(ETextJustify::Center)
					.AutoWrapText(true)
				]
			]);
		return;
	}

	TSharedRef<SVerticalBox> Content = SNew(SVerticalBox);
	Content->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
	[
		SAssignNew(SummaryHeaderBox, SBox)
		[
			BuildHeader(*Row)
		]
	];
	Content->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.f)
		[
			SAssignNew(SummaryRulesBox, SBox)
			[
				BuildEffectiveRulesSection()
			]
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(6.f, 0.f, 0.f, 0.f)
		[
			BuildAddRuleMenu()
		]
	];

	const FPaper2DPlusCombatAttackOption* Option = Session->GetSelectedOption();
	if (!Option)
	{
		Content->AddSlot().AutoHeight().Padding(0.f, 4.f)
		[
			SNew(STextBlock)
			.AutoWrapText(true)
			.Text(LOCTEXT("InheritedExplainer", "This attack scores from its inherited defaults. Customize it to set its own weight, cooldown, range, or scoring rules."))
			.ColorAndOpacity(FSlateColor(FLinearColor(0.7f, 0.7f, 0.7f)))
		];
		Content->AddSlot().AutoHeight().Padding(0.f, 6.f, 0.f, 0.f).HAlign(HAlign_Left)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
			.Text(LOCTEXT("Customize", "Customize this attack"))
			.ToolTipText(LOCTEXT("CustomizeTip", "Create a move-specific tuning row. Inherited tag rules keep applying and are never copied or double-counted."))
			.OnClicked(this, &SCombatAttackInspectorPanel::CustomizeSelected)
		];
		Host->SetContent(
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			[
				Content
			]);
		return;
	}

	Content->AddSlot().AutoHeight()
	[
		BuildTuningSections()
	];
	Host->SetContent(
		SNew(SScrollBox)
		+ SScrollBox::Slot()
		[
			Content
		]);
}

FReply SCombatAttackInspectorPanel::CustomizeSelected()
{
	if (Session.IsValid())
	{
		Session->CustomizeSelectedAttack();
	}
	return FReply::Handled();
}

FReply SCombatAttackInspectorPanel::RevertToInherited()
{
	if (Session.IsValid())
	{
		Session->RemoveSelectedOptionRow();
	}
	return FReply::Handled();
}

void SCombatAttackInspectorPanel::CommitStructEdit(const FPropertyChangedEvent&)
{
	if (!Session.IsValid() || !StructureScope.IsValid())
	{
		return;
	}
	bApplyingEdit = true;
	const FPaper2DPlusCombatAttackOption& Edited =
		*reinterpret_cast<const FPaper2DPlusCombatAttackOption*>(StructureScope->GetStructMemory());
	Session->CommitSelectedOption(Edited);
	bApplyingEdit = false;
	// Deliberately no full Rebuild: the grid keeps its expansion/focus while the read-only summaries
	// (facts, provenance, effective rules) re-render. External/undo changes rebuild via the session.
	RefreshSummaries();
}

bool SCombatAttackInspectorPanel::CustomizeForTests()
{
	return Session.IsValid() && Session->CustomizeSelectedAttack() != nullptr;
}

bool SCombatAttackInspectorPanel::ApplyOptionForTests(const void* OptionMemory)
{
	if (!OptionMemory || !Session.IsValid())
	{
		return false;
	}
	return Session->CommitSelectedOption(
		*static_cast<const FPaper2DPlusCombatAttackOption*>(OptionMemory));
}

#if WITH_DEV_AUTOMATION_TESTS
bool SCombatAttackInspectorPanel::AddConsiderationTemplateForTests(int32 TemplateIndex)
{
	const FPaper2DPlusCombatAttackOption* Before = Session.IsValid() ? Session->GetSelectedOption() : nullptr;
	const int32 BeforeCount = Before ? Before->Considerations.Num() : 0;
	AddConsiderationFromTemplate(TemplateIndex);
	const FPaper2DPlusCombatAttackOption* After = Session.IsValid() ? Session->GetSelectedOption() : nullptr;
	return After && After->Considerations.Num() == BeforeCount + 1;
}

bool SCombatAttackInspectorPanel::RevertToInheritedForTests()
{
	return Session.IsValid() && Session->RemoveSelectedOptionRow();
}
#endif

#undef LOCTEXT_NAMESPACE
