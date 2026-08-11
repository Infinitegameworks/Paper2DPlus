// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Paper2DPlusAnimationTagQuery.h" // TASK-108 U6: the per-refresh effective-tag batch cache
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "ScopedTransaction.h"
#include "CharacterProfileAssetEditor.h"

class UPaper2DPlusCharacterProfileAsset;
class UPaperFlipbook;
class FCharacterProfileEditorModel;
class SButton;
class SComboButton;
class SVerticalBox;
class SScrollBox;
namespace Paper2DPlusAnimationMap { struct FTagLensSection; } // TASK-108 U7: the "By Tag Group" lens sections

/** Group-maintenance commands hosted by the compact browse bar's labeled "Browse" overflow menu.
 *  They stay VISIBLE in every organization mode and are only disabled (with an explanatory tooltip)
 *  where they cannot apply, so a designer never has to guess where a command went. */
enum class EFlipbookBrowseOverflowAction : uint8
{
	NewGroup,
	AutoGroup,
};

/** One resolved Browse-overflow row. `GetBrowseOverflowActions()` is the single source the menu
 *  builder and the workspace acceptance tests both read, so label/enablement/tooltip cannot drift. */
struct FFlipbookBrowseOverflowEntry
{
	EFlipbookBrowseOverflowAction Action = EFlipbookBrowseOverflowAction::NewGroup;
	FText Label;
	FText ToolTip;
	bool bEnabled = true;
};

class SFlipbookBrowserPanel : public SCompoundWidget, public FEditorUndoClient
{
public:
	SLATE_BEGIN_ARGS(SFlipbookBrowserPanel) {}
		SLATE_ARGUMENT(TSharedPtr<FCharacterProfileEditorModel>, Model)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SFlipbookBrowserPanel();

	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

	void RefreshFlipbookGroupsPanel();

	DECLARE_DELEGATE_OneParam(FOnShowFlipbookContextMenu, int32);
	FOnShowFlipbookContextMenu OnShowFlipbookContextMenu;

	void ScrollToFlipbookWidget(int32 FlipbookIndex);

#if WITH_DEV_AUTOMATION_TESTS
	/** Ids of the browse bar's PERSISTENT collection mutations: controls the bar actually constructed
	 *  whose visibility can never collapse. Exactly one is allowed (Add Flipbooks), so a second
	 *  always-visible mutation control — or a contextual one that loses its conditional visibility —
	 *  changes this list immediately. Contextual/current visibility is a separate question, answered by
	 *  GetVisibleCollectionActionsForTests(). */
	TArray<FName> GetPersistentCollectionActionsForTests() const
	{
		return GetPersistentCollectionMutationControls();
	}
	int32 GetPersistentCollectionActionCountForTests() const
	{
		return GetPersistentCollectionMutationControls().Num();
	}
	/** Ids of every constructed collection mutation that is NOT collapsed right now, with the live
	 *  selection and Layer-ownership state applied. */
	TArray<FName> GetVisibleCollectionActionsForTests() const
	{
		return GetVisibleCollectionMutationControls();
	}
	bool IsDeleteActionVisibleForTests() const
	{
		return ShouldShowDeleteAction();
	}
	/** Delete is visible-but-DISABLED while Fixed / Baked Layer ownership blocks collection mutation —
	 *  the same affordance Add uses for that cause — so visibility alone no longer tells the whole
	 *  story. Pair this with GetDeleteActionTooltipForTests() for the explanation shown. */
	bool IsDeleteActionEnabledForTests() const
	{
		return IsDeleteActionEnabled();
	}
	FText GetDeleteActionTooltipForTests() const
	{
		return GetCollectionMutationTooltip(true);
	}
	FText GetOrganizeLabelForTests() const
	{
		return GetOrganizeLabel();
	}
	TArray<FFlipbookBrowseOverflowEntry> GetBrowseOverflowActionsForTests() const
	{
		return GetBrowseOverflowActions();
	}
	bool IsCompletionFilterActiveForTests() const
	{
		return IsCompletionFilterActive();
	}
	void ClearCompletionFilterForTests()
	{
		ClearCompletionFilter();
	}
	void SetByTagLensForTests(bool bInLens)
	{
		SetByTagLens(bInLens);
	}
	bool IsByTagLensForTests() const
	{
		return bByTagLens;
	}
	/** Which stable browse control the last button-driven Delete handed focus back to
	 *  (NAME_None when no Delete has run or no stable control could take focus). */
	FName GetLastBrowseFocusTargetForTests() const
	{
		return LastBrowseFocusTarget;
	}
	bool DeleteSelectedFlipbookFromBarForTests()
	{
		return DeleteSelectedFromBrowseBar(false);
	}
	int32 AddFlipbooksForTests(const TArray<UPaperFlipbook*>& InFlipbooks)
	{
		return AddFlipbooks(InFlipbooks);
	}
	bool DeleteSelectedFlipbookForTests()
	{
		return DeleteSelectedFlipbook(false);
	}
	void CommitCardAnimationTagForTests(
		int32 FlipbookIndex,
		FGameplayTag CurrentTag,
		FGameplayTag NewTag)
	{
		CommitCardAnimationTag(FlipbookIndex, CurrentTag, NewTag);
	}
	void MoveFlipbooksToGroupForTests(
		const TArray<int32>& FlipbookIndices,
		FName TargetGroup)
	{
		OnFlipbookGroupFlipbooksDrop(FlipbookIndices, TargetGroup);
	}
	void RenameFlipbookGroupForTests(FName GroupName, FName NewName)
	{
		OnFlipbookGroupNameCommitted(
			FText::FromName(NewName),
			ETextCommit::OnEnter,
			GroupName);
	}
	void DeleteFlipbookGroupForTests(FName GroupName)
	{
		DeleteFlipbookGroup(GroupName, false);
	}
	void ReparentFlipbookGroupForTests(FName GroupName, FName NewParent)
	{
		OnGroupDrop(GroupName, NewParent);
	}
	void AutoGroupByPrefixForTests()
	{
		AutoGroupByPrefix();
	}
#endif

private:
	TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset> Asset;
	TSharedPtr<FCharacterProfileEditorModel> Model;

	// Delegate handles for model subscriptions
	FDelegateHandle ModelFlipbookSelectionHandle;
	FDelegateHandle ModelGroupCollapseHandle;
	FDelegateHandle ModelSearchTextHandle;
	FDelegateHandle ModelAssetDataChangedHandle;
	FDelegateHandle ModelAssetExternallyModifiedHandle;
	FDelegateHandle ModelCompletionFilterHandle;
	FDelegateHandle ModelGroupViewModeHandle;
	FDelegateHandle TagColorsChangedHandle;   // Tag Colors registry edits -> live chip/tint refresh

	// Widget references
	TSharedPtr<SButton> AddFlipbooksButton;
	TSharedPtr<SButton> DeleteFlipbookButton;
	TSharedPtr<SButton> ClearCompletionFilterButton;
	TSharedPtr<SComboButton> OrganizeComboButton;
	TSharedPtr<SComboButton> BrowseComboButton;
	TSharedPtr<SVerticalBox> FlipbookGroupsListBox;
	TSharedPtr<SScrollBox> FlipbookGroupsScrollBox;

	/** One browse-bar control that mutates the animation COLLECTION (adds or removes profile entries).
	 *  The persistent/visible seams below measure this list rather than a hand-maintained constant, so
	 *  every collection-mutation control the bar builds MUST be registered through
	 *  RegisterCollectionMutationControl with the exact visibility attribute the widget was given. */
	struct FCollectionMutationControl
	{
		FName Id;
		TSharedPtr<SWidget> Control;
		TAttribute<EVisibility> Visibility;
	};
	TArray<FCollectionMutationControl> CollectionMutationControls;

	void RegisterCollectionMutationControl(
		FName Id,
		TSharedPtr<SWidget> Control,
		const TAttribute<EVisibility>& InVisibility);

	/** Constructed controls whose visibility is a fixed visible value (no state binding) — the R4
	 *  "always-visible collection mutation" set. */
	TArray<FName> GetPersistentCollectionMutationControls() const;
	/** Constructed controls that are not collapsed for the current selection/ownership state. */
	TArray<FName> GetVisibleCollectionMutationControls() const;

	/** Recorded focus hand-off target of the last button-driven Delete (see FocusStableBrowseControl). */
	FName LastBrowseFocusTarget;

	// Panel-local state
	FName PendingRenameFlipbookGroup;
	TMap<FName, TSharedPtr<SInlineEditableTextBlock>> FlipbookGroupNameTexts;
	TMap<int32, TSharedPtr<SInlineEditableTextBlock>> FlipbookGroupFlipbookNameTexts;
	TMap<int32, TWeakPtr<SWidget>> FlipbookGroupWidgetMap;
	TWeakPtr<FActiveTimerHandle> FlipbookGroupSearchDebounceTimer;
	int32 PendingRenameFlipbookIndex = INDEX_NONE;

	// Transaction support
	TUniquePtr<FScopedTransaction> ActiveTransaction;
	void BeginTransaction(const FText& Description);
	void EndTransaction();

	/** Commit an inline flipbook rename: renames the entry and propagates the new name to tag
	 *  mappings and the thumbnail in one transaction, then refreshes all panels. */
	void CommitFlipbookRename(int32 FlipbookIndex, const FString& NewName);

	/** Inline grid-card tag chips (TASK-96 P4): re-home a flipbook to a (possibly deeper)
	 *  Paper2DPlus.Animation tag (single-home), or set its descriptive EditorMeta.PhaseTag. Same
	 *  synchronous BeginTransaction -> mutate -> EndTransaction -> NotifyAssetDataChanged shape as
	 *  CommitFlipbookRename (rebuilding the firing combo from its own OnTagChanged is proven safe). */
	void CommitCardAnimationTag(int32 FlipbookIndex, FGameplayTag CurrentTag, FGameplayTag NewTag);
	void CommitCardPhaseTag(int32 FlipbookIndex, FGameplayTag NewTag);
	/** First-tag-wins home Animation tag for a flipbook (single-home; matches the projection). */
	FGameplayTag GetHomeAnimationTag(const FString& FlipbookName) const;

	/** TASK-108 U6 (R6): commit the card's OWN AnimationTags container (the multi-select picker on the
	 *  "Anims" chips row). Same-value early-out; same synchronous transaction + notify shape as
	 *  CommitCardAnimationTag. */
	void CommitCardAnimationTags(int32 FlipbookIndex, const FGameplayTagContainer& NewTags);

	/** TASK-108 U6 (R6/R7): lowered-flipbook-name -> provenance-separated animation tags, rebuilt from
	 *  ONE Paper2DPlusAnimationTagQuery::BuildAnimationTagMap call at the top of every
	 *  RefreshFlipbookGroupsPanel — the card chips AND the search predicate read it (never re-batch
	 *  per card/per predicate call). */
	TMap<FString, Paper2DPlusAnimationTagQuery::FAnimationTagSet> CachedAnimationTagMap;

	// ── TASK-108 U7 (R10): the derived "By Tag Group" lens — a grouping TOGGLE, never a replacement ──

	/** False = "My Groups" (the existing user-authored groups view, byte-identical); true = the
	 *  derived read-only "By Tag Group" lens. Persisted per-asset in GEditorPerProjectIni (the
	 *  Animations-tab view-mode recipe). BuildFlipbookCard reads it to suppress card drag-out. */
	bool bByTagLens = false;

	/** Panel-local collapse state for lens sections (keyed "__TagLens_<tag>" / "__TagLens_Unmapped").
	 *  Deliberately NOT the model's collapsed-groups set — the My Groups stale-name purge would wipe
	 *  these, and lens collapse is per-panel transient state. */
	TSet<FName> CollapsedLensSections;

	/** Flip the lens (no-op on same value), persist the choice, refresh. */
	void SetByTagLens(bool bInLens);
	bool IsByTagLensActive(bool bLens) const { return bByTagLens == bLens; }
	void RestoreLensFromConfig();
	void SaveLensToConfig() const;

	// ── The compact one-line browse bar (R4–R8): Add | search | completion filter (+ clear) |
	//    contextual Delete | Organize | Browse overflow. Every command still funnels the existing
	//    mutation/lens/filter methods — this layer only decides placement, label, and applicability. ──

	/** Contextual Delete VISIBILITY (R6): a deletable entry is selected. This is the genuine
	 *  "no target" question, so it is the only reason the action disappears. */
	bool ShouldShowDeleteAction() const;
	/** Contextual Delete ENABLEMENT: collection mutation is allowed. Fixed / Baked Layer ownership is a
	 *  BLOCKED cause, not a missing target, so — exactly like Add — Delete stays visible and explains
	 *  itself through GetCollectionMutationTooltip() instead of vanishing without a reason (R16/R19). */
	bool IsDeleteActionEnabled() const;
	EVisibility GetDeleteActionVisibility() const;

	/** "Organize: My Groups" / "Organize: By Tag Group" — the labeled combo that replaced the
	 *  segmented two-button lens toggle (R8). */
	FText GetOrganizeLabel() const;
	FText GetOrganizeTooltip() const;
	TSharedRef<SWidget> BuildOrganizeMenu();

	/** Browse overflow (R7). Entries stay listed in both organization modes; only enablement and the
	 *  explanatory tooltip change. */
	TArray<FFlipbookBrowseOverflowEntry> GetBrowseOverflowActions() const;
	bool CanExecuteBrowseOverflowAction(EFlipbookBrowseOverflowAction Action) const;
	/** The EXACT reason an overflow entry is unavailable. The derived lens and a missing Character
	 *  Profile are different problems, so they never share one explanation (R19). */
	FText GetBrowseOverflowDisabledTooltip(EFlipbookBrowseOverflowAction Action) const;
	void ExecuteBrowseOverflowAction(EFlipbookBrowseOverflowAction Action);
	TSharedRef<SWidget> BuildBrowseMenu();

	// ── Narrow-width behaviour (KTD11/R17). The bar never wraps and never paints outside its allotted
	//    geometry: search yields flexible width FIRST, then long labels drop to compact forms and elide.
	//    Automation constructs this panel outside any window, so unmeasured geometry is never compact —
	//    the accessible/tooltip/seam text stays the full label in headless runs. ──

	/** Allotted panel width (px) below which the bar switches to its compact labels. */
	static constexpr float CompactBrowseBarWidth = 560.0f;
	/**
	 * Guaranteed width (px) of the search box — a REAL floor, honoured because the search sits on an
	 * AutoWidth slot. It previously sat on the FillWidth slot, where SHorizontalBox ignores the
	 * child's desired size entirely and allots only the leftover space, so the box could be given
	 * zero width and become untypable at exactly the widths the floor existed to protect.
	 */
	static constexpr float SearchBoxMinWidth = 80.0f;
	bool IsBrowseBarCompact() const;
	FText GetAddActionButtonLabel() const;
	FText GetDeleteActionButtonLabel() const;
	/** Compact form of GetOrganizeLabel() for the closed control only; the accessible name, tooltip and
	 *  the test seam keep the full "Organize: <current>" text. */
	FText GetOrganizeButtonLabel() const;

	/** Completion filtering keeps a visible active state plus a one-click clear (R5/R16). */
	bool IsCompletionFilterActive() const;
	void ClearCompletionFilter();
	FReply HandleClearCompletionFilterClicked();

	/** Button-driven Delete removes its own target, so focus returns to a stable browse control
	 *  (Add, else the Organize combo) instead of being lost to the destroyed card (R19/KTD12). */
	bool DeleteSelectedFromBrowseBar(bool bRequireConfirmation);
	void FocusStableBrowseControl();

	/** One collapsible lens section: accent-swatched header (GetAnimationTagChipColor on the group
	 *  key) + a card wrap box whose members keep the helper's chain-then-alphabetical order, each card
	 *  led by a phase-tint bar (GetPhaseTagBadge — the List-row left-bar precedent). Members compose
	 *  with the search/completion filter; an all-filtered section hides while a filter is active
	 *  (returns a null widget), matching group behavior. */
	TSharedRef<SWidget> BuildTagLensSectionWidget(const Paper2DPlusAnimationMap::FTagLensSection& Section,
		const TMap<FString, int32>& FlipbookNameUsageCounts);

	// FEditorUndoClient interface
	virtual void PostUndo(bool bSuccess) override;
	virtual void PostRedo(bool bSuccess) override;

	// Profile animation collection
	FReply HandleAddFlipbooksClicked();
	FReply HandleDeleteSelectedClicked();
	void OpenFlipbookPicker();
	int32 AddFlipbooks(const TArray<UPaperFlipbook*>& InFlipbooks);
	bool DeleteSelectedFlipbook(bool bRequireConfirmation);
	bool CanMutateFlipbookCollection() const;
	FText GetCollectionMutationTooltip(bool bDelete) const;

	// UI Builders
	TSharedRef<SWidget> BuildFlipbookGroupsPanel();
	TSharedRef<SWidget> BuildGroupSection(const FFlipbookGroupInfo* GroupInfo, FName GroupName, int32 NestLevel,
		const TMap<FName, TArray<const FFlipbookGroupInfo*>>& Tree,
		const TMap<FName, TArray<int32>>& AnimsByGroup,
		const TMap<FString, int32>& FlipbookNameUsageCounts);
	TSharedRef<SWidget> BuildFlipbookCard(int32 FlipbookIndex, const TMap<FString, int32>& FlipbookNameUsageCounts);
	TSharedRef<SWidget> BuildCompletionFilterButton(TFunction<void()> OnChanged);
	void OnFlipbookGroupCardClicked(int32 FlipbookIndex, const FPointerEvent& MouseEvent);
	bool PassesFlipbookGroupSearch(const FFlipbookProfileEntry& Animation) const;

	// Group management
	void CreateFlipbookGroup(FName ParentGroup = NAME_None);
	void DeleteFlipbookGroup(FName GroupName, bool bRequireConfirmation = true);
	void ShowFlipbookGroupContextMenu(FName GroupName, const FVector2D& CursorPos);
	void TriggerFlipbookGroupRename(FName GroupName);
	bool OnVerifyFlipbookGroupNameChanged(const FText& InText, FText& OutErrorMessage, FName CurrentGroupName);
	void OnFlipbookGroupNameCommitted(const FText& InText, ETextCommit::Type CommitType, FName OriginalGroupName);
	void OnOpenFlipbookGroupColorPicker(FName GroupName, FLinearColor CurrentColor);
	void OnFlipbookGroupColorCommitted(FLinearColor NewColor, FName GroupName);
	void AutoGroupByPrefix();
	void OnFlipbookGroupFlipbooksDrop(const TArray<int32>& FlipbookIndices, FName TargetGroup);
	void OnGroupDrop(FName SourceGroupName, FName TargetParentGroup);

	// Helpers
	TArray<int32> GetSortedFlipbookIndices() const;
	void TriggerPendingRenameIfNeeded(TMap<int32, TSharedPtr<SInlineEditableTextBlock>>& NameTexts);

};
