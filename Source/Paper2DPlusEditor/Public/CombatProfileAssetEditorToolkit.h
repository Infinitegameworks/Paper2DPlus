// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "EditorUndoClient.h"

class FCombatAttackPickerSource;
class FCombatLabModel;
class FCombatProfileEditorSession;
class IDetailsView;
class SExpandableArea;
class SVerticalBox;
class SWindow;
class UPaper2DPlusCombatProfileAsset;
struct FPaper2DPlusValidationIssue;

/**
 * Combat Profile workspace, built on the same grammar as the Character Profile workspace:
 * central designer tools (Overview, Score Playground, Combat Lab) that each share one compact
 * current-attack header, an upper-right contextual Details panel that follows the selection
 * across every tool, and a lower-right Completion/Related Profiles sibling stack.
 * Variables/Tag Defaults/Scoring Profiles/Scenario Presets remain closed guided-collection tabs.
 */
class FCombatProfileAssetEditorToolkit : public FAssetEditorToolkit, public FEditorUndoClient
{
public:
	virtual ~FCombatProfileAssetEditorToolkit();

	void InitEditor(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost, UPaper2DPlusCombatProfileAsset* InAsset);

	virtual FName GetToolkitFName() const override;
	virtual FText GetBaseToolkitName() const override;
	virtual FString GetWorldCentricTabPrefix() const override;
	virtual FLinearColor GetWorldCentricTabColorScale() const override;
	virtual void RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager) override;
	virtual void UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager) override;

	// FEditorUndoClient — a session refresh rebuilds every Setup surface, and the panels
	// re-snapshot their struct copies from the restored live options; without it a stale
	// post-edit copy re-applies the undone edit (audit F2).
	virtual void PostUndo(bool bSuccess) override;
	virtual void PostRedo(bool bSuccess) override;

	/** Retained FName (label is "Overview") so saved layouts and validation navigation keep resolving. */
	static const FName SetupTabId;
	static const FName AttackDetailsTabId;
	static const FName DetailsTabId;
	static const FName ScorePreviewTabId;
	static const FName CombatLabTabId;
	static const FName VariablesTabId;
	static const FName DefaultsTabId;
	static const FName ScoringProfilesTabId;
	static const FName PresetsTabId;
	static const FName CompletionTabId;
	static const FName RelatedProfilesTabId;
	static const FName WorkspaceLayoutId;
	static TSharedRef<FTabManager::FLayout> BuildDefaultLayout();

private:
	UPaper2DPlusCombatProfileAsset* EditedAsset = nullptr;
	TSharedPtr<IDetailsView> DetailsView;
	TSharedPtr<FCombatProfileEditorSession> EditorSession;
	TSharedPtr<FCombatAttackPickerSource> AttackPickerSource;
	TSharedPtr<FCombatLabModel> CombatLabModel;
	FDelegateHandle DetailsPropertyChangedHandle;

	/** Prepends the shared current-attack header to a central tool, mirroring the Character workspace. */
	TSharedRef<SWidget> WrapToolContent(
		TSharedRef<SWidget> ToolContent,
		TSharedPtr<SWidget> HeaderActions = nullptr);

	TSharedRef<SDockTab> SpawnTab_Setup(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_AttackDetails(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_Details(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_ScorePreview(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_CombatLab(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_Variables(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_Defaults(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_ScoringProfiles(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_Presets(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_Completion(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_RelatedProfiles(const FSpawnTabArgs& Args);
	void OpenValidation();
	void HandleValidationIssueActivated(const FPaper2DPlusValidationIssue& Issue);

	TSharedRef<SWidget> BuildDetailsToolbar();

	// --- Setup tab: shared scoring variables strip (collapsed while empty) ---
	TSharedPtr<SExpandableArea> GlobalVariablesExpander;
	TSharedPtr<SVerticalBox> GlobalVariablesBox;   // rebuilt list of variable value rows

	TSharedRef<SWidget> BuildGlobalVariablesSection();
	void RefreshGlobalVariables();
	TSharedRef<SWidget> BuildGlobalVariableRow(int32 DefinitionIndex);
	TSharedRef<SWidget> BuildVariableValueEditor(int32 DefinitionIndex);
	FReply OnAddVariableClicked();
	void RemoveVariable(int32 DefinitionIndex);

	void RefreshAllDerivedViews();
	TWeakPtr<SWindow> ValidationWindow;
	/** InitEditor owns this registration; headless test harnesses never enter that lifecycle. */
	bool bRegisteredForUndo = false;
};
