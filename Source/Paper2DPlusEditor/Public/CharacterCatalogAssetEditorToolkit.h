// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EditorUndoClient.h"
#include "ProfileValidationPanel.h"
#include "Toolkits/AssetEditorToolkit.h"

class FCharacterCatalogEditorModel;
class IDetailsView;
class SCharacterCatalogDetailsPanel;
class SCharacterCatalogRosterPanel;
class SVerticalBox;
class UPaper2DPlusCharacterCatalogAsset;
struct FPaper2DPlusValidationIssue;
struct FPropertyChangedEvent;

/** Character Catalog workspace: the Roster (grid plus its Groups rail) plus docked Details and Warnings, with low-frequency Advanced recovery. */
class FCharacterCatalogAssetEditorToolkit final : public FAssetEditorToolkit, public FEditorUndoClient
{
public:
	virtual ~FCharacterCatalogAssetEditorToolkit() override;

	void InitEditor(
		EToolkitMode::Type Mode,
		const TSharedPtr<IToolkitHost>& InitToolkitHost,
		UPaper2DPlusCharacterCatalogAsset* InAsset);

	virtual FName GetToolkitFName() const override;
	virtual FText GetBaseToolkitName() const override;
	virtual FString GetWorldCentricTabPrefix() const override;
	virtual FLinearColor GetWorldCentricTabColorScale() const override;
	virtual void RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager) override;
	virtual void UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager) override;
	virtual void PostUndo(bool bSuccess) override;
	virtual void PostRedo(bool bSuccess) override;

	static const FName RosterTabId;
	static const FName DetailsTabId;
	static const FName WarningsTabId;
	static const FName AdvancedTabId;

	/** Live semantic gates used by the render-capable designer tour. */
	bool ValidateRosterWorkspaceForTests(
		const TArray<FSoftObjectPath>& ExpectedAnimatedCharacters,
		FString& OutReason);
	/** The retired Groups tab's gate, re-aimed at the Roster's Groups rail. */
	bool ValidateGroupsRailForTests(FName ExpectedGroup, FString& OutReason);

	/** One production/test composition seam for the explicit-only docked Warnings surface. */
	static TSharedRef<SProfileValidationPanel> MakeWarningsPanel(
		UPaper2DPlusCharacterCatalogAsset* InAsset,
		const TSharedPtr<FCharacterCatalogEditorModel>& InModel,
		FOnPaper2DPlusValidationIssueActivated OnIssueActivated =
			FOnPaper2DPlusValidationIssueActivated());

private:
	TSharedRef<SDockTab> SpawnTab_Roster(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_Details(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_Warnings(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_Advanced(const FSpawnTabArgs& Args);
	void HandleWarningActivated(const FPaper2DPlusValidationIssue& Issue);
	void RefreshAfterUndoRedo();
	void HandleAdvancedPropertiesChanged(const FPropertyChangedEvent& PropertyChangedEvent);

	UPaper2DPlusCharacterCatalogAsset* EditedAsset = nullptr;
	TSharedPtr<FCharacterCatalogEditorModel> Model;
	TSharedPtr<IDetailsView> AdvancedDetailsView;
	TSharedPtr<SCharacterCatalogRosterPanel> RosterPanel;
	TSharedPtr<SCharacterCatalogDetailsPanel> DetailsPanel;
	TSharedPtr<SProfileValidationPanel> WarningsPanel;
	FDelegateHandle ModelChangedHandle;
	FDelegateHandle AdvancedPropertiesChangedHandle;
	bool bRefreshingAdvancedProperties = false;
};
