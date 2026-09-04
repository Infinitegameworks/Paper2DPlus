// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Input/DragAndDrop.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/STableViewBase.h"

class FCharacterProfileEditorModel;
class ITableRow;
class SInlineEditableTextBlock;
class STableViewBase;
class UPaper2DPlusCharacterLayerAsset;
template <typename ItemType> class STreeView;

enum class ELayerStructureNodeKind : uint8
{
	Group,
	Layer
};

/** Where a dragged row landed relative to the row under the cursor. Deliberately NOT Slate's
 *  EItemDropZone: the drop DECISION is a pure, worldless, headless-testable function, and only the
 *  widget translates Slate's enum onto this one. */
enum class ELayerStructureDropZone : uint8
{
	Above,
	Onto,
	Below
};

/** What a drag would do, resolved BEFORE any transaction opens, so the same answer drives the drop
 *  marker (can this land here?) and the commit (what exactly does it do?). */
struct PAPER2DPLUSEDITOR_API FLayerStructureDropPlan
{
	bool bValid = false;
	/** Index in the GLOBAL Layers array to insert BEFORE, expressed against the PRE-move array
	 *  (insert-before semantics, like the Catalog rail). INDEX_NONE = do not reorder at all, which is
	 *  what a drop onto a Section header means. */
	int32 InsertBeforeIndex = INDEX_NONE;
	/** True when the drop also changes which Section the layer belongs to. */
	bool bAssignGroup = false;
	/** Destination Section. Invalid + bAssignGroup means "remove from its Section" (the Ungrouped row). */
	FGuid GroupId;
};

/** Stable tree identity. Groups are organization-only; layers always key through LayerId. */
struct PAPER2DPLUSEDITOR_API FLayerStructureNode
{
	ELayerStructureNodeKind Kind = ELayerStructureNodeKind::Layer;
	FGuid GroupId;
	FGuid LayerId;
	FText Label;
	TArray<TSharedPtr<FLayerStructureNode>> Children;

	/** Layer rows only: this layer's index in the asset's GLOBAL Layers array — the one paint,
	 *  gameplay and publish order. Sections never reorder anything, so showing this on every row is
	 *  what keeps the grouped view honest: a Move Up that swaps past a member of another section
	 *  leaves the row where it is and changes THIS number. */
	int32 GlobalOrder = INDEX_NONE;

	/** Section rows only: how many layers the section holds before any search filter, so the header
	 *  can say "6" normally and "4 matches" while filtering without recounting the asset per paint. */
	int32 TotalChildCount = 0;
};

/**
 * One proposed Section, derived from the top-level folder of imported `.ase` layer paths.
 *
 * No parser work is involved: `FCharacterLayer::LayerName` IS the `.ase` group path ("Armor/Chest"),
 * so the folder is the substring before the first `/`. The same strings exist pre-commit on the bulk
 * row's `AseLayerNames` and per-source as `FAsepriteSourceContext::LayerContentHashes` keys, which is
 * how a suggestion scopes itself to ONE contributing file on a multi-source Layer Profile.
 */
struct PAPER2DPLUSEDITOR_API FLayerSectionSuggestion
{
	/** The `.ase` folder this was derived from. NEVER edited — it is the key a later re-derivation
	 *  matches against, so renaming the Section cannot orphan the designer's earlier choice. */
	FString SourceFolder;

	/** Proposed Section name; `SourceFolder` by default, renameable before applying. */
	FString SectionName;

	/** Full layer paths, in source order, that would join the Section. */
	TArray<FString> LayerNames;

	/** Unticked = removed/ignored. Apply skips it entirely — no row, no GroupId. */
	bool bAccepted = true;

	/** True when the target asset already has a Section under this name: applying MERGES into that
	 *  row rather than minting a duplicate the dock could not tell apart. */
	bool bMatchesExistingSection = false;
};

/** What one apply actually did, so the caller reports the truth instead of claiming success. */
struct PAPER2DPLUSEDITOR_API FLayerSectionApplyReport
{
	int32 SectionsCreated = 0;
	int32 SectionsReused = 0;
	int32 LayersAssigned = 0;

	/** Accepted member paths with no layer on the asset. Expected BEFORE an import runs — which is
	 *  exactly why an accepted set is re-applied once the import has created them. */
	TArray<FString> UnmatchedLayerNames;

	bool ChangedAnything() const { return SectionsCreated > 0 || LayersAssigned > 0; }
};

/** Transactional mutations shared by the widget and headless tests. */
class PAPER2DPLUSEDITOR_API FLayerStructureController
{
public:
	static ECheckBoxState GetGroupPreviewState(
		const UPaper2DPlusCharacterLayerAsset& Asset,
		const FCharacterProfileEditorModel& Model,
		const FGuid& GroupId);

	static void SetLayerPreview(
		const UPaper2DPlusCharacterLayerAsset& Asset,
		FCharacterProfileEditorModel& Model,
		const FGuid& LayerId,
		bool bVisible);
	static void SetGroupPreview(
		const UPaper2DPlusCharacterLayerAsset& Asset,
		FCharacterProfileEditorModel& Model,
		const FGuid& GroupId,
		bool bVisible);

	static FGuid AddGroup(UPaper2DPlusCharacterLayerAsset& Asset, FCharacterProfileEditorModel& Model);
	static bool RenameGroup(
		UPaper2DPlusCharacterLayerAsset& Asset,
		FCharacterProfileEditorModel& Model,
		const FGuid& GroupId,
		const FText& NewName);
	/** Deletes only the organizational row; every child is safely reparented to Ungrouped. */
	static bool DeleteGroup(
		UPaper2DPlusCharacterLayerAsset& Asset,
		FCharacterProfileEditorModel& Model,
		const FGuid& GroupId);
	static bool MoveLayerToGroup(
		UPaper2DPlusCharacterLayerAsset& Asset,
		FCharacterProfileEditorModel& Model,
		const FGuid& LayerId,
		const FGuid& GroupId);
	/** Reorders the authored Layers array, the sole visual/gameplay/publish order. */
	static bool MoveLayerByDelta(
		UPaper2DPlusCharacterLayerAsset& Asset,
		FCharacterProfileEditorModel& Model,
		const FGuid& LayerId,
		int32 Delta);
	/** The one pure drop decision, shared by the drop marker and the commit. Refuses no-ops, self-drops
	 *  and a target from another asset. A drop onto a Section header carries membership ONLY —
	 *  InsertBeforeIndex stays INDEX_NONE — because Sections reorder nothing.
	 *
	 *  Search does NOT disable this: a drop means "insert immediately before/after the TARGET's global
	 *  position", and every row displays that global position, so a filtered gap between rows 3 and 12
	 *  still names exactly one index. Do not add a search guard back without also removing the numbers. */
	static FLayerStructureDropPlan PlanDrop(
		const UPaper2DPlusCharacterLayerAsset& Asset,
		const FGuid& DraggedLayerId,
		ELayerStructureNodeKind TargetKind,
		const FGuid& TargetId,
		ELayerStructureDropZone Zone);

	/** Commits a plan as ONE transaction: a drag that crosses a Section boundary must not be two
	 *  undo steps, or undoing the reorder leaves the layer in a Section the designer never chose. */
	static bool ApplyDropPlan(
		UPaper2DPlusCharacterLayerAsset& Asset,
		FCharacterProfileEditorModel& Model,
		const FGuid& LayerId,
		const FLayerStructureDropPlan& Plan);

	/**
	 * PURE: derive one-level Section suggestions from `.ase` layer paths, in first-seen source order.
	 *
	 * ONE level only — "Armor/Chest/Plate" suggests "Armor", not "Armor/Chest" — because Sections are
	 * a one-level model and inventing depth here would be a second organization system. A layer with
	 * no folder contributes NOTHING: it stays ungrouped rather than joining an invented "Other".
	 *
	 * `TargetAsset` may be null; when given, suggestions whose name already exists on it are flagged
	 * so the preview can say "merges into the existing Section" before the designer commits.
	 */
	static TArray<FLayerSectionSuggestion> DeriveSectionSuggestions(
		const TArray<FString>& LayerNames,
		const UPaper2DPlusCharacterLayerAsset* TargetAsset);

	/**
	 * Writes the Section rows AND the member layers' GroupIds in ONE transaction.
	 *
	 * That is not a style preference. `EnsureLayerAuthoringIdentity` invalidates any GroupId with no
	 * matching `LayerGroups` row, and it runs on EVERY import — so a GroupId written without its row
	 * silently un-sections everything on the next reimport.
	 *
	 * Idempotent by construction: Sections resolve by NAME (find-or-create) and a layer is touched
	 * only when its GroupId actually changes. Re-applying an accepted set after an import created the
	 * remaining layers therefore places just those, and an apply that would change nothing opens no
	 * transaction and leaves the package undirtied.
	 *
	 * `Model` may be null — the bulk extractor applies these with no Layer editor open.
	 */
	static bool ApplySectionSuggestions(
		UPaper2DPlusCharacterLayerAsset& Asset,
		FCharacterProfileEditorModel* Model,
		const TArray<FLayerSectionSuggestion>& Suggestions,
		FLayerSectionApplyReport& OutReport);

	/** Destructive layer delete. Callers own confirmation; preset references to the stable ID are pruned atomically. */
	static bool DeleteLayer(
		UPaper2DPlusCharacterLayerAsset& Asset,
		FCharacterProfileEditorModel& Model,
		const FGuid& LayerId);
};

/** One layer row dragged onto a Section header or between two rows. */
class PAPER2DPLUSEDITOR_API FLayerStructureRowDragDropOp : public FDragDropOperation
{
public:
	DRAG_DROP_OPERATOR_TYPE(FLayerStructureRowDragDropOp, FDragDropOperation)

	FGuid LayerId;
	/** Identity guard: a drop refuses an operation minted against a different Layer asset. */
	TWeakObjectPtr<UPaper2DPlusCharacterLayerAsset> SourceAsset;

	static TSharedRef<FLayerStructureRowDragDropOp> New(
		const FGuid& InLayerId,
		const FText& InLabel,
		UPaper2DPlusCharacterLayerAsset* InSourceAsset);
	virtual TSharedPtr<SWidget> GetDefaultDecorator() const override;

private:
	FText HoverText;
};

/** Virtualized one-level organization tree with transient Preview controls. */
class PAPER2DPLUSEDITOR_API SLayerStructureTree : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SLayerStructureTree) {}
		SLATE_ARGUMENT(TSharedPtr<FCharacterProfileEditorModel>, Model)
		SLATE_ARGUMENT(UPaper2DPlusCharacterLayerAsset*, LayerAsset)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SLayerStructureTree() override;

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

	void Refresh();
	int32 GetRootCountForTests() const { return Roots.Num(); }
	int32 GetLayerRowCountForTests() const;
	bool SelectLayerForTests(const FGuid& LayerId);

	/** Filter the visible layer rows. Sections are NEVER filtered out — a matching section keeps
	 *  its children, and a section with a matching child stays visible as that child's ancestor. */
	void SetSearchTextForTests(const FString& InSearchText);
	int32 GetMatchingLayerCountForTests() const;

	/** Reveal a layer that may be inside a collapsed section: expands its section, selects it, and
	 *  scrolls its row into view. This is what the Art canvas calls when the user clicks the art. */
	void RevealLayer(const FGuid& LayerId);

private:
	TSharedRef<ITableRow> GenerateRow(
		TSharedPtr<FLayerStructureNode> Item,
		const TSharedRef<STableViewBase>& OwnerTable);
	void HandleGetChildren(
		TSharedPtr<FLayerStructureNode> Item,
		TArray<TSharedPtr<FLayerStructureNode>>& OutChildren) const;
	void HandleSelectionChanged(TSharedPtr<FLayerStructureNode> Item, ESelectInfo::Type SelectInfo);
	void HandleModelLayerSelectionChanged(int32 NewIndex);
	void HandleExternalChange();
	/** Resolves the row under the cursor into a plan; empty when the drop must be refused. */
	FLayerStructureDropPlan PlanDropOnNode(
		const FDragDropEvent& DragDropEvent,
		TSharedPtr<FLayerStructureNode> TargetItem,
		ELayerStructureDropZone Zone) const;

	TSharedRef<SWidget> BuildMoveMenu(const FGuid& LayerId);
	TSharedRef<SWidget> BuildGroupMenu(const FGuid& GroupId);
	FReply AddGroupClicked();
	FReply DeleteGroupClicked(FGuid GroupId);
	FReply DeleteLayerClicked(FGuid LayerId);
	FReply MoveLayerClicked(FGuid LayerId, int32 Delta);
	void BeginGroupRename(FGuid GroupId);
	EActiveTimerReturnType HandleDeferredGroupRename(double CurrentTime, float DeltaTime);
	void CommitGroupName(const FText& Text, ETextCommit::Type CommitType, FGuid GroupId);

	/** True when the row passes the active search. Empty search matches everything. */
	bool MatchesSearch(const FString& LayerName) const;

	TSharedPtr<FCharacterProfileEditorModel> Model;
	TWeakObjectPtr<UPaper2DPlusCharacterLayerAsset> LayerAsset;
	TSharedPtr<STreeView<TSharedPtr<FLayerStructureNode>>> TreeView;
	TArray<TSharedPtr<FLayerStructureNode>> Roots;
	/** Lowercased active search term; empty = no filter. Matches layer names (which carry the
	 *  imported source path, e.g. "Clothes/Jacket/Front") and section names. */
	FString SearchText;
	/** The dock's own arranged width, sampled each tick. At the shipped 0.16 layout coefficient a
	 *  640px window leaves this column near 100px, so rows must shed their LOW-priority elements
	 *  (order number, member count, actions) before the layer NAME, which is the thing a designer
	 *  actually reads. Bare numbers in a nameless list are worse than no list. */
	float CachedDockWidth = 0.0f;
	/** Layers passing the filter, and the asset's total, for the "4 / 38" readout. */
	int32 MatchingLayerCount = 0;
	int32 TotalLayerCount = 0;
	TMap<FGuid, TWeakPtr<SInlineEditableTextBlock>> GroupNameWidgets;
	TWeakPtr<FActiveTimerHandle> RenameTimerHandle;
	FGuid PendingRenameGroupId;
	FDelegateHandle ModelLayerSelectionHandle;
	FDelegateHandle ModelAssetDataHandle;
	FDelegateHandle ModelExternalHandle;
	bool bSyncingSelection = false;
};
