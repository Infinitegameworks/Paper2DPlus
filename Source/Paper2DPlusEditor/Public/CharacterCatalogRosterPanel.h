// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "CharacterCatalogEditorModel.h"
#include "Input/DragAndDrop.h"
#include "ProfileThumbnailBudget.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/STableViewBase.h"

class SInlineEditableTextBlock;
class SSearchBox;
class SBox;
class SComboButton;
struct FStreamableHandle;
template <typename ItemType> class SListView;
template <typename ItemType> class STileView;

/** Character card(s) dragged from the roster grid onto a Groups rail row, or onto another card. */
class PAPER2DPLUSEDITOR_API FCatalogCharacterDragDropOp : public FDragDropOperation
{
public:
	DRAG_DROP_OPERATOR_TYPE(FCatalogCharacterDragDropOp, FDragDropOperation)

	TArray<FSoftObjectPath> CharacterPaths;
	/** Non-None while the grid is scoped to one group, which makes a card-on-card drop a REORDER. */
	FName SourceGroup;
	/** Identity guard: a drop must refuse an operation minted against a different Catalog. */
	TWeakObjectPtr<UPaper2DPlusCharacterCatalogAsset> SourceCatalog;

	static TSharedRef<FCatalogCharacterDragDropOp> New(
		const TArray<FSoftObjectPath>& InCharacterPaths,
		FName InSourceGroup,
		UPaper2DPlusCharacterCatalogAsset* InSourceCatalog);
	virtual TSharedPtr<SWidget> GetDefaultDecorator() const override;

private:
	FText DefaultHoverText;
};

/** One rail row dragged onto another rail row: an authored group reorder. */
class PAPER2DPLUSEDITOR_API FCatalogGroupRowDragDropOp : public FDragDropOperation
{
public:
	DRAG_DROP_OPERATOR_TYPE(FCatalogGroupRowDragDropOp, FDragDropOperation)

	FName SourceGroupName;
	TWeakObjectPtr<UPaper2DPlusCharacterCatalogAsset> SourceCatalog;

	static TSharedRef<FCatalogGroupRowDragDropOp> New(
		FName InGroupName,
		UPaper2DPlusCharacterCatalogAsset* InSourceCatalog);
	virtual TSharedPtr<SWidget> GetDefaultDecorator() const override;

private:
	FText DefaultHoverText;
};

/**
 * Searchable, virtualized character-card grid. Selected-character authoring lives in Details.
 * The grid is the direct-manipulation surface: "+ Add Characters…" is the only intake and
 * multi-select removal (context menu / Delete key) is the only exit; both are one transaction.
 */
class PAPER2DPLUSEDITOR_API SCharacterCatalogRosterPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SCharacterCatalogRosterPanel) {}
		SLATE_ARGUMENT(TSharedPtr<FCharacterCatalogEditorModel>, Model)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SCharacterCatalogRosterPanel() override;
	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

	/** Injected confirmation seam so automation can drive removal without a modal dialog. */
	TFunction<bool(const FText&)> RemoveSelectedConfirmation;

	/**
	 * Scope the grid to one authored group (NAME_None = All Characters). This is the production entry
	 * point warning activation and the designer tour both use; the rail row follows the model filter.
	 */
	bool SelectGroupRail(FName GroupName);

	void RefreshForTests();
	void RefreshSourcesForTests();
	FName GetThumbnailSourceForTests(const FSoftObjectPath& CharacterPath) const;
	FSoftObjectPath GetDesiredThumbnailFlipbookPathForTests(
		const FSoftObjectPath& CharacterPath) const;
	int32 GetVisibleCharacterCountForTests() const;
	int32 GenerateTilesForViewportForTests(const FVector2D& ViewportSize);
	bool WasAsyncThumbnailRequestedForTests(const FSoftObjectPath& CharacterPath) const;
	bool RequestThumbnailForPathForTests(const FSoftObjectPath& CharacterPath);
	int32 GetRetainedAsyncThumbnailCountForTests() const { return AsyncThumbnailLoads.Num(); }
	bool IsAsyncThumbnailRetainedForTests(const FSoftObjectPath& CharacterPath) const
	{
		return AsyncThumbnailLoads.Contains(CharacterPath);
	}
	bool HasLoadedAsyncThumbnailObjectsForTests(const FSoftObjectPath& CharacterPath) const;
	bool HasCardSurfaceForTests() const { return CharacterTiles.IsValid(); }
	bool ShouldHideAddCandidateForTests(const FAssetData& AssetData) const
	{
		return ShouldHideAddCandidate(AssetData);
	}
	void SetShowOnlyNewCharactersForTests(bool bValue) { bShowOnlyNewCharacters = bValue; }
	void SetTileSelectionForTests(const TArray<FSoftObjectPath>& CharacterPaths);
	bool RemoveSelectedCharactersForTests() { return RemoveSelectedCharacters(); }

	// ---- Groups rail (replaces the retired Groups tab) ----
	bool HasGroupRailForTests() const { return GroupRail.IsValid(); }
	/** Rail entries in display order: NAME_None first (All Characters), then authored group order. */
	TArray<FName> GetGroupRailItemsForTests() const;
	bool SelectGroupRailItemForTests(FName GroupName);
	FName GetSelectedGroupRailItemForTests() const { return SelectedRailGroup; }
	bool DropCharactersOnGroupForTests(FName GroupName, const TArray<FSoftObjectPath>& CharacterPaths);
	bool ReorderGroupMemberForTests(const FSoftObjectPath& CharacterPath, int32 TargetIndex);
	/**
	 * Drive the REAL card-on-card and rail-row drop bodies, anchor and drop-zone exactly as Slate
	 * delivers them. The index-taking seam above cannot see a view-to-authored translation defect
	 * because it never performs one — these do what the designer's drag actually does.
	 */
	bool DropCardOnCardForTests(
		const FSoftObjectPath& MovedPath,
		const FSoftObjectPath& AnchorPath,
		bool bBelowAnchor)
	{
		return HandleCardOnCardDrop(MovedPath, AnchorPath, bBelowAnchor);
	}
	bool DropGroupOnGroupRowForTests(FName MovedGroup, FName AnchorGroup, bool bBelowAnchor)
	{
		return HandleGroupRowDrop(MovedGroup, AnchorGroup, AnchorGroup.IsNone(), bBelowAnchor);
	}
	bool CreateGroupFromRailForTests(const FText& DisplayLabel);
	bool RemoveGroupFromRailForTests(FName GroupName);
	bool RenameGroupFromRailForTests(FName GroupName, const FText& NewLabel);
	bool HasBaseExpectedAnimationTagsControlForTests() const
	{
		return BaseExpectedAnimationTagsCombo.IsValid();
	}
	bool HasSelectedGroupExpectedAnimationTagsControlForTests() const
	{
		return GroupExpectedAnimationTagsCombo.IsValid();
	}
	/** NAME_None targets the Catalog-wide base set; a real group name targets that group's extras. */
	void QueueExpectedAnimationTagsCommitForTests(
		FName GroupName,
		const FGameplayTagContainer& Tags);
	bool HasPendingExpectedAnimationTagsCommitForTests() const
	{
		return PendingExpectedAnimationTagsCommit.IsSet();
	}
	void FlushExpectedAnimationTagsCommitForTests();
	/** Injected confirmation seam for the rail's group deletion (headless tests bypass the dialog). */
	TFunction<bool(const FText&)> RemoveGroupConfirmation;

private:
	struct FPendingExpectedAnimationTagsCommit
	{
		/** NAME_None means the Catalog-wide ExpectedAnimationTags set. */
		FName GroupName = NAME_None;
		FGameplayTagContainer Tags;
	};

	using FCatalogRowPtr = TSharedPtr<FPaper2DPlusCharacterCatalogEditorRow>;
	/** How many visible cards' art stays resident. Shared with the budget below. */
	static constexpr int32 MaxRetainedAsyncCharacterThumbnails = 48;
	struct FAsyncThumbnailLoadState
	{
		TSharedPtr<FStreamableHandle> ProfileHandle;
		TSharedPtr<FStreamableHandle> FlipbookHandle;
		TStrongObjectPtr<UObject> ProfileKeepAlive;
		TStrongObjectPtr<UObject> FlipbookKeepAlive;
		bool bRequested = false;
		bool bFailed = false;
	};
	void HandleModelChanged();
	void RequestVisibleThumbnailAsync(const FCatalogRowPtr& Row);
	void HandleThumbnailProfileLoaded(FSoftObjectPath CharacterPath);
	void HandleThumbnailFlipbookLoaded(FSoftObjectPath CharacterPath, FSoftObjectPath FlipbookPath);
	FAsyncThumbnailLoadState& TouchAsyncThumbnailLoad(const FSoftObjectPath& CharacterPath);
	void RefreshGeneratedTiles();
	void ResetAsyncThumbnailLoads();
	void SynchronizeCardSelection();
	void UpdateRosterSummary();
	TSharedRef<ITableRow> GenerateCharacterTile(
		FCatalogRowPtr Row,
		const TSharedRef<STableViewBase>& OwnerTable);
	void HandleCharacterTileSelected(FCatalogRowPtr Row, ESelectInfo::Type SelectInfo);
	TSharedPtr<SWidget> HandleTileContextMenu();
	TSharedRef<SWidget> BuildAuthorityBanner();
	TSharedRef<SWidget> BuildCommandBar();
	TSharedRef<SWidget> BuildAddCharactersMenu();
	TSharedRef<SWidget> BuildFilters();
	TSharedRef<SWidget> BuildRequirementMenu();
	TSharedRef<SWidget> BuildCompletionMenu();
	TSharedRef<SWidget> BuildGroupMenu();
	TSharedRef<SWidget> BuildTagFilterMenu();
	FText GetRosterSummaryText() const;
	FText GetRequirementFilterText() const;
	FText GetCompletionFilterText() const;
	FText GetGroupFilterText() const;
	FText GetTagFilterText() const;
	FReply HandleRefresh();
	FReply HandleSetAuthority();
	FReply HandleOpenSettings();
	FReply HandleClearFilters();
	bool ShouldHideAddCandidate(const FAssetData& AssetData) const;
	void AddCharactersFromAssets(const TArray<FAssetData>& Assets);
	TArray<FSoftObjectPath> GetSelectedCharacterPaths() const;
	bool RemoveSelectedCharacters();
	void ShowMessage(const FText& Message) const;

	TSharedRef<SWidget> BuildGroupRail();
	void RefreshGroupRail();
	TSharedRef<SWidget> BuildExpectedAnimationTagsEditors();
	TSharedRef<SWidget> BuildExpectedAnimationTagsControl(
		FName GroupName,
		const FGameplayTagContainer& Snapshot,
		const FText& EmptyText,
		const FText& Tooltip);
	void RefreshExpectedAnimationTagsEditors();
	void QueueExpectedAnimationTagsCommit(
		FName GroupName,
		const FGameplayTagContainer& Tags);
	EActiveTimerReturnType HandleDeferredExpectedAnimationTagsCommit(
		double CurrentTime,
		float DeltaTime);
	TSharedRef<ITableRow> GenerateGroupRailRow(
		TSharedPtr<FName> Item,
		const TSharedRef<STableViewBase>& OwnerTable);
	void HandleGroupRailSelectionChanged(TSharedPtr<FName> Item, ESelectInfo::Type SelectInfo);
	TSharedPtr<SWidget> HandleGroupRailContextMenu();
	void SynchronizeGroupRailSelection();
	bool ApplyGroupSelection(FName GroupName);
	/**
	 * The two reorder drop bodies. Both take the ANCHOR the designer dropped on rather than a view
	 * index: the grid is filtered before it is member-sorted, and the rail omits unnamed groups, so
	 * neither view offers an index that addresses the authored array.
	 */
	bool HandleCardOnCardDrop(
		const FSoftObjectPath& MovedPath,
		const FSoftObjectPath& AnchorPath,
		bool bBelowAnchor);
	bool HandleGroupRowDrop(
		FName MovedGroup,
		FName AnchorGroup,
		bool bAnchorIsAllRow,
		bool bBelowAnchor);
	bool CreateGroupFromRail(const FText& DisplayLabel);
	bool RemoveGroupFromRail(FName GroupName);
	bool RenameGroupFromRail(FName GroupName, const FText& NewLabel);
	int32 GetGroupMemberCount(FName GroupName) const;
	TSharedPtr<FDragDropOperation> BeginCharacterDrag();

	TSharedPtr<FCharacterCatalogEditorModel> Model;
	TSharedPtr<STileView<FCatalogRowPtr>> CharacterTiles;
	TSharedPtr<SListView<TSharedPtr<FName>>> GroupRail;
	TSharedPtr<SBox> ExpectedAnimationTagsEditorBox;
	TSharedPtr<SComboButton> BaseExpectedAnimationTagsCombo;
	TSharedPtr<SComboButton> GroupExpectedAnimationTagsCombo;
	TArray<TSharedPtr<FName>> GroupRailItems;
	/** NAME_None means the All Characters row. Mirrors the model's group filter; never leads it. */
	FName SelectedRailGroup = NAME_None;
	FName PendingRailRenameGroup = NAME_None;
	TMap<FName, TWeakPtr<SInlineEditableTextBlock>> GroupRailNameWidgets;
	TOptional<FPendingExpectedAnimationTagsCommit> PendingExpectedAnimationTagsCommit;
	TWeakPtr<FActiveTimerHandle> ExpectedAnimationTagsCommitTimerHandle;
	bool bSyncingRailSelection = false;
	TSharedPtr<SSearchBox> SearchBox;
	FText CachedRosterSummary;
	/** Retention for visible card art. The shared budget owns the LRU; this panel owns the two-stage
	 *  Profile -> flipbook load graph that lives in the payload. */
	TProfileThumbnailBudget<FAsyncThumbnailLoadState> AsyncThumbnailLoads{
		MaxRetainedAsyncCharacterThumbnails};
	FDelegateHandle ModelChangedHandle;
	/** The Add picker's default lens: hide profiles that are already saved in this Catalog. */
	bool bShowOnlyNewCharacters = true;
};
