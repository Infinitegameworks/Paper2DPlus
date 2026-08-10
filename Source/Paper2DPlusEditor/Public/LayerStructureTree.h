// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
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

/** Stable tree identity. Groups are organization-only; layers always key through LayerId. */
struct PAPER2DPLUSEDITOR_API FLayerStructureNode
{
	ELayerStructureNodeKind Kind = ELayerStructureNodeKind::Layer;
	FGuid GroupId;
	FGuid LayerId;
	FText Label;
	TArray<TSharedPtr<FLayerStructureNode>> Children;
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
	/** Destructive layer delete. Callers own confirmation; preset references to the stable ID are pruned atomically. */
	static bool DeleteLayer(
		UPaper2DPlusCharacterLayerAsset& Asset,
		FCharacterProfileEditorModel& Model,
		const FGuid& LayerId);
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

	void Refresh();
	int32 GetRootCountForTests() const { return Roots.Num(); }
	int32 GetLayerRowCountForTests() const;
	bool SelectLayerForTests(const FGuid& LayerId);

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
	TSharedRef<SWidget> BuildMoveMenu(const FGuid& LayerId);
	TSharedRef<SWidget> BuildGroupMenu(const FGuid& GroupId);
	FReply AddGroupClicked();
	FReply DeleteGroupClicked(FGuid GroupId);
	FReply DeleteLayerClicked(FGuid LayerId);
	FReply MoveLayerClicked(FGuid LayerId, int32 Delta);
	void BeginGroupRename(FGuid GroupId);
	EActiveTimerReturnType HandleDeferredGroupRename(double CurrentTime, float DeltaTime);
	void CommitGroupName(const FText& Text, ETextCommit::Type CommitType, FGuid GroupId);

	TSharedPtr<FCharacterProfileEditorModel> Model;
	TWeakObjectPtr<UPaper2DPlusCharacterLayerAsset> LayerAsset;
	TSharedPtr<STreeView<TSharedPtr<FLayerStructureNode>>> TreeView;
	TArray<TSharedPtr<FLayerStructureNode>> Roots;
	TMap<FGuid, TWeakPtr<SInlineEditableTextBlock>> GroupNameWidgets;
	TWeakPtr<FActiveTimerHandle> RenameTimerHandle;
	FGuid PendingRenameGroupId;
	FDelegateHandle ModelLayerSelectionHandle;
	FDelegateHandle ModelAssetDataHandle;
	FDelegateHandle ModelExternalHandle;
	bool bSyncingSelection = false;
};
