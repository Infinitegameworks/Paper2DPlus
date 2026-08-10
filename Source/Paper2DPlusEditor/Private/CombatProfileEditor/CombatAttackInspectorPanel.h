// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateColor.h"
#include "Widgets/SCompoundWidget.h"

class FCombatProfileEditorSession;
class FStructOnScope;
class IStructureDetailsView;
class SBox;
struct FPaper2DPlusCombatAttackDerivedData;
struct FPaper2DPlusCombatAttackOption;
struct FPropertyChangedEvent;

/**
 * The guided inspector for the selected attack.
 *
 * Read-only identity/facts header (art, name, plain-language reach/frames/damage line, live
 * rank+score badge), the layered effective-scoring-rules view, and a "+ Scoring Rule" template
 * menu lead; move-specific tuning follows as bespoke Weighting/Range/Eligibility rows plus a
 * scoped property grid restricted to the deep collections (rules, tags, variables). Identity
 * (Move Name / Move Flipbook) is never an editable row. A tuning row is created only when the
 * designer presses Customize; no passive refresh mutates the Combat Profile.
 */
class SCombatAttackInspectorPanel final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SCombatAttackInspectorPanel) {}
		SLATE_ARGUMENT(TSharedPtr<FCombatProfileEditorSession>, Session)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SCombatAttackInspectorPanel() override;

	void RefreshFromSession();
	bool CustomizeForTests();
	bool ApplyOptionForTests(const void* OptionMemory);
#if WITH_DEV_AUTOMATION_TESTS
	bool AddConsiderationTemplateForTests(int32 TemplateIndex);
	bool RevertToInheritedForTests();
	FText GetFactsTextForTests() const { return GetFactsText(); }
	FText GetProvenanceTextForTests() const { return GetProvenanceText(); }
#endif

private:
	FReply CustomizeSelected();
	FReply RevertToInherited();
	void Rebuild();
	/** Re-renders the facts header and effective-rules list without touching the tuning widgets,
	 *  so a grid or spinbox commit never collapses the grid's expansion or steals focus. */
	void RefreshSummaries();
	void CommitStructEdit(const FPropertyChangedEvent& Event);
	/** Copy-mutate-commit funnel for the bespoke rows; every edit is one undoable session commit. */
	void ApplyOptionEdit(TFunctionRef<void(FPaper2DPlusCombatAttackOption&)> Mutator);

	FText GetFactsText() const;
	FText GetProvenanceText() const;
	FText GetScoreText() const;
	FSlateColor GetScoreColor() const;

	TSharedRef<SWidget> BuildHeader(const FPaper2DPlusCombatAttackDerivedData& Row);
	TSharedRef<SWidget> BuildEffectiveRulesSection();
	TSharedRef<SWidget> BuildAddRuleMenu();
	void AddConsiderationFromTemplate(int32 TemplateIndex);
	TSharedRef<SWidget> BuildTuningSections();
	TSharedRef<SWidget> MakeSectionHeader(const FText& Label, const FText& Tooltip);

	TSharedPtr<FCombatProfileEditorSession> Session;
	TSharedPtr<SBox> Host;
	TSharedPtr<SBox> SummaryHeaderBox;
	TSharedPtr<SBox> SummaryRulesBox;
	TSharedPtr<IStructureDetailsView> StructureView;
	TSharedPtr<FStructOnScope> StructureScope;
	FDelegateHandle DataChangedHandle;
	FDelegateHandle SelectionChangedHandle;
	bool bApplyingEdit = false;
};
