// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Toolkits/AssetEditorToolkit.h"

class UPaper2DPlusCharacterLayerAsset;
class FAnimationProfilePickerSource;
class FCharacterProfileEditorModel;
class FFrameCueDataProvider;
class FHitboxFrameDataProvider;
class FToolBarBuilder;
class IProfileToolPanelProvider;
class SFrameEventEditor;
class SLayerOverviewPanel;
class SHitboxEditorPanel;
class SCharacterLayerBakeStatusWidget;
class SProfileToolPanelHost;
class SWindow;
class UPaper2DPlusCharacterProfileAsset;
struct FCharacterLayerBakeResult;
struct FCharacterLayerBakeSaveResult;
struct FCharacterLayerBakeStatusSnapshot;
struct FCharacterLayerBakeStatusPresentation;
struct FPaper2DPlusValidationIssue;
enum class ECharacterLayerBakeOperation : uint8;

class FCharacterLayerAssetEditorToolkit : public FAssetEditorToolkit
{
public:
	virtual ~FCharacterLayerAssetEditorToolkit();

	void InitEditor(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost, UPaper2DPlusCharacterLayerAsset* InAsset);

	virtual FName GetToolkitFName() const override;
	virtual FText GetBaseToolkitName() const override;
	virtual FString GetWorldCentricTabPrefix() const override;
	virtual FLinearColor GetWorldCentricTabColorScale() const override;
	virtual void RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager) override;
	virtual void UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager) override;

	static const FName StructureTabId;
	static const FName ArtTabId;
	static const FName FlipbookListTabId;
	// Layer-scoped hitbox editor backed by the selected real layer's authored animation data.
	static const FName HitboxesTabId;
	static const FName FrameCuesTabId;
	static const FName AppearanceTabId;
	static const FName ContextHostTabId;
	static const FName CompletionTabId;
	static const FName RelatedProfilesTabId;
	/** Playback Queue — mirrors the Character workspace's lower-right Completion/Related stack. */
	static const FName PlaybackQueueTabId;
	static const FName WorkspaceLayoutId;
	static TSharedRef<FTabManager::FLayout> BuildDefaultLayout();

private:
	UPaper2DPlusCharacterLayerAsset* EditedLayerAsset = nullptr;
	TSharedPtr<FCharacterProfileEditorModel> EditorModel;
	TSharedPtr<FAnimationProfilePickerSource> AnimationPickerSource;
	TMap<FName, TSharedPtr<IProfileToolPanelProvider>> ToolPanelProviders;
	FName ActiveToolId;

	TWeakPtr<SLayerOverviewPanel> LayerOverviewPanel;
	TWeakPtr<SHitboxEditorPanel> HitboxPanel;
	TWeakPtr<SFrameEventEditor> FrameCuePanel;
	TWeakPtr<SProfileToolPanelHost> ContextPanelHost;
	TWeakPtr<SCharacterLayerBakeStatusWidget> BakeStatusWidget;
	TWeakPtr<SWindow> ValidationWindow;
	TSharedPtr<FCharacterLayerBakeStatusSnapshot> CachedBakeStatus;
	bool bPublishOperationInProgress = false;
	bool bBakeStatusRefreshPending = false;
	FDelegateHandle BakeStatusAssetDataHandle;
	FDelegateHandle BakeStatusExternalHandle;
	FDelegateHandle BakeStatusSelectionHandle;

	/** The layer-scoped hitbox provider injected into the Hitboxes tab. */
	TSharedPtr<FHitboxFrameDataProvider> LayerHitboxProvider;
	TSharedPtr<FFrameCueDataProvider> LayerCueProvider;

	TSharedRef<SDockTab> SpawnTab_Structure(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_Art(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_FlipbookList(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_Hitboxes(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_FrameCues(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_Appearance(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_ContextHost(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_Completion(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_RelatedProfiles(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_PlaybackQueue(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> MakeMainToolTab(
		const FText& Label,
		FName ToolId,
		TSharedRef<SWidget> ToolContent);
	TSharedRef<SWidget> WrapMainToolContent(TSharedRef<SWidget> ToolContent);
	void HandleMainToolActivated(
		TSharedRef<SDockTab> ActivatedTab,
		ETabActivationCause Cause,
		FName ToolId);
	TSharedPtr<IProfileToolPanelProvider> FindToolPanelProvider(FName ToolId) const;
	void RegisterToolPanelProvider(FName ToolId, TSharedPtr<IProfileToolPanelProvider> Provider);
	void OpenValidation();
	void HandleValidationIssueActivated(const FPaper2DPlusValidationIssue& Issue);

	// "Open Character Profile" toolbar button keeps character-wide timing, motion, transitions, and
	// tags in their owning asset while Layer-local Art, Hitboxes, and Frame Cues stay in this editor.
	void ExtendToolbar(FToolBarBuilder& ToolbarBuilder);
	void OpenCharacterProfileEditor();
	bool CanOpenCharacterProfileEditor() const;
	FText GetOpenCharacterProfileTooltip() const;

	// Canonical publish lifecycle. Every command makes one coordinator call; bulk package mutation is
	// snapshot/rollback based and intentionally not placed in Unreal's undo buffer.
	void RefreshBakeStatus();
	void HandleBakeStatusSourceChanged();
	void HandleBakeStatusSelectionChanged(int32 NewIndex);
	int32 ResolveCurrentRegistrationIndex() const;
	UPaper2DPlusCharacterProfileAsset* GetBaseProfile() const;
	FCharacterLayerBakeStatusPresentation BuildBakeStatusPresentation() const;
	void BeginPublishOperation();
	void EndPublishOperation();
	void BakeCurrent();
	void BakeAll();
	void SaveBakeSet();
	void OverwriteFromLayerSource();
	void RepairBakeSet();
	void RebaseRegistration();
	void DetachBakeSet();
	void EnableRuntimeCustomization();
	void RunBakeCommand(
		ECharacterLayerBakeOperation Operation,
		bool bOverwrite,
		const FText& NotificationLabel);
	void HandleBakeResult(const FCharacterLayerBakeResult& Result, const FText& OperationLabel);
	void HandleSaveResult(const FCharacterLayerBakeSaveResult& Result);
	void ShowPublishNotification(const FText& Message, bool bSuccess) const;
	bool ConfirmPublishAction(const FText& Message) const;
};
