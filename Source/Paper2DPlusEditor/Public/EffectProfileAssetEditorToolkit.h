// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EditorUndoClient.h"
#include "Toolkits/AssetEditorToolkit.h"

struct FAssetData;
class FEffectProfileEditorModel;
class IDetailsView;
class SEffectProfileDetailsPanel;
class SEffectProfileLibraryPanel;
class SEffectProfilePreviewPanel;
class SWindow;
class UPaper2DPlusEffectProfileAsset;
struct FPaper2DPlusValidationIssue;
struct FPropertyChangedEvent;

/**
 * Effect Profile visual-library workspace. Primary tabs expose curated membership, preview, and
 * active metadata over one path-keyed model; Advanced Details is the compatibility escape hatch.
 */
class FEffectProfileAssetEditorToolkit : public FAssetEditorToolkit, public FEditorUndoClient
{
public:
	virtual ~FEffectProfileAssetEditorToolkit() override;

	void InitEditor(
		EToolkitMode::Type Mode,
		const TSharedPtr<IToolkitHost>& InitToolkitHost,
		UPaper2DPlusEffectProfileAsset* InAsset);

	virtual FName GetToolkitFName() const override;
	virtual FText GetBaseToolkitName() const override;
	virtual FString GetWorldCentricTabPrefix() const override;
	virtual FLinearColor GetWorldCentricTabColorScale() const override;
	virtual void RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager) override;
	virtual void UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager) override;

	virtual void PostUndo(bool bSuccess) override;
	virtual void PostRedo(bool bSuccess) override;

	static const FName LibraryTabId;
	static const FName PreviewTabId;
	static const FName DetailsTabId;
	static const FName AdvancedDetailsTabId;
	static const FName CompletionTabId;
	static const FName RelatedProfilesTabId;
	/** Versioned so saved wide/validation-first layouts cannot override the compact library workspace. */
	static const FName WorkspaceLayoutId;
	/** One authoritative default tree used by both InitEditor and structural automation. */
	static TSharedRef<FTabManager::FLayout> BuildDefaultLayout();
	/** Live semantic gate used by the render-capable designer tour. */
	bool ValidateDesignerWorkspaceForTests(int32 ExpectedFrameCount, FString& OutReason);

private:
	TSharedRef<SDockTab> SpawnTab_Library(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_Preview(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_Details(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_AdvancedDetails(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_Completion(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_RelatedProfiles(const FSpawnTabArgs& Args);

	void OpenAdvancedDetails();
	void OpenValidation();
	void HandleValidationIssueActivated(const FPaper2DPlusValidationIssue& Issue);
	void HandleAssetRegistryChanged(const FAssetData& AssetData);
	void HandleAssetRenamed(const FAssetData& AssetData, const FString& OldObjectPath);
	void HandleAssetRegistryFilesLoaded();
	void HandleObjectPropertyChanged(UObject* Object, FPropertyChangedEvent& Event);

	UPaper2DPlusEffectProfileAsset* EditedAsset = nullptr;
	TSharedPtr<FEffectProfileEditorModel> Model;
	TSharedPtr<IDetailsView> AdvancedDetailsView;
	TSharedPtr<SEffectProfileLibraryPanel> LibraryPanel;
	TSharedPtr<SEffectProfilePreviewPanel> PreviewPanel;
	TSharedPtr<SEffectProfileDetailsPanel> DetailsPanel;
	TWeakPtr<SWindow> ValidationWindow;
	FDelegateHandle AssetAddedHandle;
	FDelegateHandle AssetRemovedHandle;
	FDelegateHandle AssetUpdatedHandle;
	FDelegateHandle AssetRenamedHandle;
	FDelegateHandle AssetFilesLoadedHandle;
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
	FDelegateHandle AssetKnownGathersCompleteHandle;
#endif
	FDelegateHandle PropertyChangedHandle;
};
