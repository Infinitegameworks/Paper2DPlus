// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "ScopedTransaction.h"
#include "CharacterProfileAssetEditor.h"
#include "ProfileToolPanelProvider.h"

class UPaper2DPlusCharacterProfileAsset;
class UPaper2DPlusCharacterLayerAsset;
class UPaperFlipbook;
class FCharacterProfileEditorModel;
class FHitboxFrameDataProvider;
class SVerticalBox;
class SHorizontalBox;
class SWidgetSwitcher;
class SCharacterProfileEditorCanvas;
class SHitbox3DViewport;

/**
 * Independent hitbox editor panel.
 * Owns all hitbox editing UI: tool panel, flipbook sidebar, canvas, frame strip,
 * hitbox list, properties panel, and frame operations.
 * Communicates with other panels through the shared FCharacterProfileEditorModel.
 */
class SHitboxEditorPanel : public SCompoundWidget, public FEditorUndoClient, public IProfileToolPanelProvider
{
public:
	static const FName HitboxesPanelId;
	static const FName PropertiesPanelId;
	static const FName FrameOperationsPanelId;

	SLATE_BEGIN_ARGS(SHitboxEditorPanel) {}
		SLATE_ARGUMENT(TSharedPtr<FCharacterProfileEditorModel>, Model)
		// Optional: when hosted by the Character Layer editor, the frame strip renders the live layered
		// composite instead of the flat base-profile sprite. Null in
		// the Character Profile editor, where the strip keeps showing the single flipbook sprite per frame.
		SLATE_ARGUMENT(TWeakObjectPtr<UPaper2DPlusCharacterLayerAsset>, LayerAsset)
		// Optional data-provider seam (see HitboxDataProvider.h). Null in the Character Profile editor —
		// the panel then constructs the default FProfileHitboxDataProvider itself, so the profile path is
		// byte-identical by construction. The Character Layer editor injects FLayerHitboxDataProvider to
		// scope every read/write to the model-selected real layer's AuthoredAnimations source.
		SLATE_ARGUMENT(TSharedPtr<FHitboxFrameDataProvider>, FrameDataProvider)
		// Exactly one layout owner. Embedded is the legacy/default profile tab; External is used when the
		// same controller is mounted by a workspace/floating host. The enum contract cannot represent both.
		SLATE_ARGUMENT(FProfileToolPanelHostContract, HostContract)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SHitboxEditorPanel();

	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

	// IProfileToolPanelProvider — external Character workspaces consume the same controller's builders.
	virtual FProfileToolPanelHostContract GetHostContract() const override { return HostContract; }
	virtual void GetContextualPanels(TArray<FProfileToolPanelDescriptor>& OutPanels) const override;

	/** Tab/tool deactivation releases canvas capture and commits an open gesture at most once. */
	void HandleHostDeactivated();

	// Refresh entry points
	void RefreshFlipbookList();
	void RefreshFrameList();
	void RefreshHitboxList();
	void RefreshPropertiesPanel();
	void RefreshAll();

	// Delegates for operations that remain on the parent
	DECLARE_DELEGATE_OneParam(FOnShowFlipbookContextMenu, int32);
	FOnShowFlipbookContextMenu OnShowFlipbookContextMenu;

	DECLARE_DELEGATE_TwoParams(FOnShowSpriteContextMenu, UPaperSprite*, FVector2D);
	FOnShowSpriteContextMenu OnShowSpriteContextMenu;

	DECLARE_DELEGATE_OneParam(FOnOpenSpriteAssetEditor, UPaperSprite*);
	FOnOpenSpriteAssetEditor OnOpenSpriteAssetEditor;

	DECLARE_DELEGATE_OneParam(FOnBrowseToSpriteAsset, UPaperSprite*);
	FOnBrowseToSpriteAsset OnBrowseToSpriteAsset;

	// Narrow provider/hosting characterization seams (no mutation; used by the U3 regression tests).
	const FProfileToolPanelHostContract& GetHostContractForTests() const { return HostContract; }
	TSharedPtr<FHitboxFrameDataProvider> GetFrameDataProviderForTests() const { return Provider; }
	int32 GetContextPanelBuildCountForTests(FName PanelId) const
	{
		return ContextPanelBuildCounts.FindRef(PanelId);
	}
	void SetCanvasSelectionForTests(EHitboxSelectionType Type, int32 Index);
	EHitboxSelectionType GetCanvasSelectionTypeForTests() const;
	int32 GetCanvasPrimarySelectionForTests() const;
	EHitboxSelectionType GetLastPropertiesSelectionTypeForTests() const
	{
		return LastPropertiesSelectionType;
	}
	int32 GetLastPropertiesSelectionIndexForTests() const { return LastPropertiesSelectionIndex; }
	bool BeginTransactionForTests(const FText& Description);
	bool HasActiveTransactionForTests() const { return ActiveTransaction.IsValid(); }
	int32 GetTransactionBeginCountForTests() const { return TransactionBeginCount; }
	int32 GetTransactionEndCountForTests() const { return TransactionEndCount; }
	EHitboxEditorTool GetCurrentToolForTests() const { return CurrentTool; }
	int32 GetCurrentFrameCountForTests() const { return GetCurrentFrameCount(); }
	void ArmCanvasDragForTests(EHitboxDragMode Mode, bool bTransactionOpen);
	void SettleCanvasDragForTests();
	bool HasArmedCanvasDragForTests() const;

private:
	TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset> Asset;
	// Set only when hosted by the Character Layer editor — drives the composited frame strip. See the Construct arg.
	TWeakObjectPtr<UPaper2DPlusCharacterLayerAsset> LayerAsset;
	TSharedPtr<FCharacterProfileEditorModel> Model;
	// THE frame-data resolution + transaction-target seam — never null after Construct (defaults to the
	// profile provider). All per-frame combat-data reads/writes go through it; flipbook identity/sprites
	// stay direct profile reads.
	TSharedPtr<FHitboxFrameDataProvider> Provider;
	FProfileToolPanelHostContract HostContract;

	// Delegate handles for model subscriptions
	FDelegateHandle ModelFlipbookSelectionHandle;
	FDelegateHandle ModelFrameSelectionHandle;
	FDelegateHandle ModelGroupCollapseHandle;
	FDelegateHandle ModelLayerVisibilityHandle;
	FDelegateHandle ModelAssetExternallyModifiedHandle;
	FDelegateHandle ModelAssetDataChangedHandle;
	FDelegateHandle ModelLayerSelectionHandle;

	// --- Hitbox-local state (moved from parent) ---
	EHitboxEditorTool CurrentTool = EHitboxEditorTool::Edit;
	EHitboxType ActiveDrawType = EHitboxType::Hurtbox;
	float ZoomLevel = 1.0f;
	EHitboxVisibility HitboxVisibilityMask = EHitboxVisibility::All;
	bool bShowHitboxesOnFrameStrip = true;
	bool bShowSocketsOnFrameStrip = true;
	bool bShow3DView = false;
	bool bSpriteFlipX = false;
	bool bSpriteFlipY = false;

	// Frame operations sentence builder state
	int32 CopySourceIndex = 0;
	int32 CopyTargetIndex = 0;
	bool bCopyMerge = false;
	int32 CopyHitboxScopeIndex = 0;
	// Frame Operations combo sources live with the controller so contextual close/reopen never leaves
	// an SComboBox pointing at a temporary array.
	TArray<TSharedPtr<FString>> CopySourceOptions;
	TArray<TSharedPtr<FString>> CopyTargetOptions;
	TArray<TSharedPtr<FString>> CopyScopeOptions;

	// Merge-policy combo options (layer scope header; persistent source for SComboBox)
	TArray<TSharedPtr<FString>> MergePolicyOptions;

	// Cached flipbook count
	int32 LastKnownFlipbookCount = 0;

	// Active transaction for undo support
	TUniquePtr<FScopedTransaction> ActiveTransaction;

	// Sidebar section order (INI persistence — panel-local keys)
	TArray<FName> HitboxSidebarSectionOrder;

	// Flipbook rename state
	int32 PendingRenameFlipbookIndex = INDEX_NONE;
	TMap<int32, TSharedPtr<SInlineEditableTextBlock>> SidebarFlipbookNameTexts;

	// Widget references
	TSharedPtr<SVerticalBox> FlipbookListBox;
	TSharedPtr<SHorizontalBox> FrameListBox;
	TSharedPtr<SVerticalBox> HitboxListBox;
	TSharedPtr<SVerticalBox> PropertiesBox;
	TSharedPtr<SVerticalBox> HitboxSidebarSectionsBox;
	TSharedPtr<SCharacterProfileEditorCanvas> EditorCanvas;
	TSharedPtr<SHitbox3DViewport> Viewport3D;
	TSharedPtr<SWidgetSwitcher> CanvasViewSwitcher;
	mutable TMap<FName, int32> ContextPanelBuildCounts;
	EHitboxSelectionType LastPropertiesSelectionType = EHitboxSelectionType::None;
	int32 LastPropertiesSelectionIndex = INDEX_NONE;
	int32 TransactionBeginCount = 0;
	int32 TransactionEndCount = 0;

	// Transaction support
	void BeginTransaction(const FText& Description);
	void EndTransaction();

	/** Commit an inline flipbook rename: renames the entry and propagates the new name to tag
	 *  mappings and the thumbnail in one transaction, then refreshes all panels. */
	void CommitFlipbookRename(const FProfileAnimationIdentity& AnimationIdentity, const FString& NewName);

	// FEditorUndoClient interface
	virtual void PostUndo(bool bSuccess) override;
	virtual void PostRedo(bool bSuccess) override;

	// UI Builders
	TSharedRef<SWidget> BuildToolbar();
	TSharedRef<SWidget> BuildCentralWorkspace();
	TSharedRef<SWidget> BuildFlipbookList();
	TSharedRef<SWidget> BuildFrameList();
	TSharedRef<SWidget> BuildCanvasArea();
	// Layer-scope-only strip above the canvas: selected Layer banner + the per-animation
	// AttackMerge/HurtMerge dropdowns (visible only while an override entry exists — find-only contract).
	// Collapsed entirely for the profile provider.
	TSharedRef<SWidget> BuildLayerScopeHeader();
	TSharedRef<SWidget> BuildHitboxList();
	TSharedRef<SWidget> BuildPropertiesPanel();
	TSharedRef<SWidget> BuildCopyOperationsPanel();
	void RebuildHitboxSidebarSections();

	// Section layout helpers
	void InitializeSectionLayout();
	void LoadSectionOrder(const FString& ConfigKey, const TArray<FName>& DefaultOrder, TArray<FName>& InOutOrder) const;
	void SaveSectionOrder(const FString& ConfigKey, const TArray<FName>& Order) const;
	bool CanMoveSection(const TArray<FName>& SectionOrder, FName SectionId, int32 Direction) const;
	void MoveSectionInOrder(TArray<FName>& SectionOrder, FName SectionId, int32 Direction, const FString& ConfigKey);
	void MoveHitboxSidebarSection(FName SectionId, int32 Direction);
	TSharedRef<SWidget> BuildReorderableSectionCard(
		FName SectionId,
		const FText& SectionTitle,
		const FText& SectionTooltip,
		TSharedRef<SWidget> ContentWidget,
		bool bStretchContent = false);

	// Event handlers
	void OnFlipbookSelected(int32 Index);
	void OnFrameSelected(int32 Index);
	void OnToolSelected(EHitboxEditorTool Tool);
	void OnSelectionChanged(EHitboxSelectionType Type, int32 Index);
	void OnHitboxDataModified();
	void OnZoomChanged(float NewZoom);

	// Copy operations
	void OnClearCurrentFrame();

	// Add new hitbox/socket
	void AddNewHitbox();
	void AddNewSocket();
	void DeleteSelected();

	// Frame multi-select helpers
	void ForEachSelectedFrame(TFunctionRef<void(int32)> Op);

	// Visibility filtering
	bool IsHitboxTypeVisible(EHitboxType Type) const;

	// Helpers
	const FFrameHitboxData* GetCurrentFrame() const;
	const FFrameHitboxData* GetCurrentFrame(int32 FrameIdx) const;
	FFrameHitboxData* GetCurrentFrameMutable();
	FFrameHitboxData* GetCurrentFrameMutable(int32 FrameIdx);
	const FFlipbookProfileEntry* GetCurrentFlipbookData() const;
	FFlipbookProfileEntry* GetCurrentFlipbookDataMutable();
	int32 GetCurrentFrameCount() const;
	UPaperSprite* GetCurrentSprite() const;
	void TriggerPendingRenameIfNeeded(TMap<int32, TSharedPtr<SInlineEditableTextBlock>>& NameTexts);
};
