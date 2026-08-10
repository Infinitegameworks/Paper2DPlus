// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Paper2DPlusCombatProfileTypes.h"
#include "Widgets/SCompoundWidget.h"

class FCombatLabModel;
class FCombatProfileEditorSession;
class FStructOnScope;
class IStructureDetailsView;
class SBox;
class STableViewBase;
class SVerticalBox;
template <typename ItemType> class SComboBox;
template <typename ItemType> class SListView;

/** Full-context, explainable scoring sandbox over the runtime scoring helpers. */
class SCombatScorePlaygroundPanel final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SCombatScorePlaygroundPanel) {}
		SLATE_ARGUMENT(TSharedPtr<FCombatProfileEditorSession>, Session)
		SLATE_ARGUMENT(TSharedPtr<FCombatLabModel>, LabModel)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SCombatScorePlaygroundPanel() override;

	void SetContextForTests(const FPaper2DPlusCombatRuntimeContext& Context);
	bool SavePresetForTests(FName PresetName);
	bool LoadPresetForTests(FName PresetName);
	int32 GetRankedCountForTests() const { return Rows.Num(); }
	FName GetTopMoveForTests() const;
	const FPaper2DPlusCombatRankedOption* FindRankedForTests(FName MoveName) const;

private:
	using FRowPtr = TSharedPtr<FPaper2DPlusCombatRankedOption>;

	void BuildContextEditor();
	/** Plain-language rows for the three situation values designers change constantly. */
	TSharedRef<SWidget> BuildSituationSection();
	/** Scoring profile, scenario preset, and their load/save actions — built once, beside Situation. */
	TSharedRef<SWidget> BuildScenarioSection();
	/** ValueWidth > 0 pins a short editor (a number) instead of letting it fill the value column. */
	TSharedRef<SWidget> MakeSituationRow(
		const FText& Label,
		const FText& Tooltip,
		TSharedRef<SWidget> Editor,
		float ValueWidth = 0.0f);
	FText GetSituationSummary() const;
	/** Copy-mutate-commit funnel shared by every bespoke situation row. */
	void ApplyContextEdit(TFunctionRef<void(FPaper2DPlusCombatRuntimeContext&)> Mutator);
	void CommitContextEdit(const FPropertyChangedEvent& Event);
	void RefreshFromAsset();
	void RefreshScores();
	void RefreshPresetOptions();
	void RefreshScoringProfileOptions();
	void SynchronizeSelection();
	void RebuildBreakdown();
	TSharedRef<ITableRow> GenerateRow(FRowPtr Row, const TSharedRef<STableViewBase>& OwnerTable);
	void HandleRowSelected(FRowPtr Row, ESelectInfo::Type SelectInfo);
	FReply SavePreset();
	FReply LoadPreset();
	bool SavePresetNamed(FName PresetName);
	bool LoadPresetNamed(FName PresetName);
	FName MakeUniquePresetName() const;

	TSharedPtr<FCombatProfileEditorSession> Session;
	TSharedPtr<FCombatLabModel> LabModel;
	TSharedPtr<FStructOnScope> ContextScope;
	TSharedPtr<IStructureDetailsView> ContextView;
	TSharedPtr<SBox> ContextHost;
	TArray<FRowPtr> Rows;
	TSharedPtr<SListView<FRowPtr>> ListView;
	TSharedPtr<SVerticalBox> BreakdownBox;
	TArray<TSharedPtr<FName>> PresetOptions;
	TSharedPtr<SComboBox<TSharedPtr<FName>>> PresetCombo;
	TSharedPtr<FName> SelectedPreset;
	TArray<TSharedPtr<FName>> ScoringProfileOptions;
	TSharedPtr<SComboBox<TSharedPtr<FName>>> ScoringProfileCombo;
	TSharedPtr<FName> SelectedScoringProfile;
	FDelegateHandle DataChangedHandle;
	FDelegateHandle ScoreChangedHandle;
	FDelegateHandle SelectionChangedHandle;
	bool bApplyingContext = false;
	bool bSynchronizingSelection = false;
};
