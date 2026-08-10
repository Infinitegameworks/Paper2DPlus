// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "LayerStructureTree.h"

#include "CharacterProfileEditorModel.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Misc/MessageDialog.h"
#include "Paper2DPlusCharacterLayerAsset.h"
#include "ScopedTransaction.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/STreeView.h"
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
#include "EditorStyleSet.h"
#ifndef FAppStyle
#define FAppStyle FEditorStyle
#endif
#else
#include "Styling/AppStyle.h"
#endif

#define LOCTEXT_NAMESPACE "LayerStructureTree"

namespace Paper2DPlusLayerStructurePrivate
{
	struct FRawModelMutation
	{
		explicit FRawModelMutation(FCharacterProfileEditorModel& InModel) : Model(InModel)
		{
			Model.BeginModelMutation();
		}
		~FRawModelMutation() { Model.EndModelMutation(); }
		FCharacterProfileEditorModel& Model;
	};

	FCharacterLayer* FindLayer(UPaper2DPlusCharacterLayerAsset& Asset, const FGuid& LayerId)
	{
		return Asset.Layers.FindByPredicate([&LayerId](const FCharacterLayer& Layer)
		{
			return Layer.LayerId == LayerId;
		});
	}

	const FCharacterLayer* FindLayer(const UPaper2DPlusCharacterLayerAsset& Asset, const FGuid& LayerId)
	{
		return Asset.Layers.FindByPredicate([&LayerId](const FCharacterLayer& Layer)
		{
			return Layer.LayerId == LayerId;
		});
	}

	ECheckBoxState Aggregate(int32 Enabled, int32 Total)
	{
		if (Total <= 0 || Enabled <= 0) return ECheckBoxState::Unchecked;
		return Enabled == Total ? ECheckBoxState::Checked : ECheckBoxState::Undetermined;
	}

	void NotifyAssetChanged(FCharacterProfileEditorModel& Model)
	{
		Model.ReconcileLayerSelectionIdentity();
		Model.NotifyAssetDataChanged();
	}
}

ECheckBoxState FLayerStructureController::GetGroupPreviewState(
	const UPaper2DPlusCharacterLayerAsset& Asset,
	const FCharacterProfileEditorModel& Model,
	const FGuid& GroupId)
{
	int32 Total = 0;
	int32 Visible = 0;
	for (const FCharacterLayer& Layer : Asset.Layers)
	{
		if (Layer.GroupId != GroupId) continue;
		++Total;
		if (Model.IsLayerVisible(Layer.LayerName)) ++Visible;
	}
	return Paper2DPlusLayerStructurePrivate::Aggregate(Visible, Total);
}

void FLayerStructureController::SetLayerPreview(
	const UPaper2DPlusCharacterLayerAsset& Asset,
	FCharacterProfileEditorModel& Model,
	const FGuid& LayerId,
	bool bVisible)
{
	if (const FCharacterLayer* Layer = Paper2DPlusLayerStructurePrivate::FindLayer(Asset, LayerId))
		Model.SetLayerVisibility(Layer->LayerName, bVisible);
}

void FLayerStructureController::SetGroupPreview(
	const UPaper2DPlusCharacterLayerAsset& Asset,
	FCharacterProfileEditorModel& Model,
	const FGuid& GroupId,
	bool bVisible)
{
	Paper2DPlusLayerStructurePrivate::FRawModelMutation Mutation(Model);
	for (const FCharacterLayer& Layer : Asset.Layers)
		if (Layer.GroupId == GroupId) Model.SetLayerVisibility(Layer.LayerName, bVisible);
}

FGuid FLayerStructureController::AddGroup(
	UPaper2DPlusCharacterLayerAsset& Asset,
	FCharacterProfileEditorModel& Model)
{
	TSet<FString> Existing;
	for (const FCharacterLayerGroupInfo& Group : Asset.LayerGroups)
		Existing.Add(Group.DisplayName.ToString().ToLower());
	FString Name = TEXT("New Group");
	for (int32 Suffix = 2; Existing.Contains(Name.ToLower()); ++Suffix)
		Name = FString::Printf(TEXT("New Group %d"), Suffix);

	Paper2DPlusLayerStructurePrivate::FRawModelMutation Mutation(Model);
	FScopedTransaction Transaction(LOCTEXT("AddLayerGroup", "Add Layer Group"));
	Asset.Modify();
	FCharacterLayerGroupInfo& Group = Asset.LayerGroups.AddDefaulted_GetRef();
	Group.GroupId = FGuid::NewGuid();
	Group.DisplayName = FText::FromString(Name);
	Paper2DPlusLayerStructurePrivate::NotifyAssetChanged(Model);
	return Group.GroupId;
}

bool FLayerStructureController::RenameGroup(
	UPaper2DPlusCharacterLayerAsset& Asset,
	FCharacterProfileEditorModel& Model,
	const FGuid& GroupId,
	const FText& NewName)
{
	FCharacterLayerGroupInfo* Group = Asset.LayerGroups.FindByPredicate([&GroupId](const FCharacterLayerGroupInfo& Row)
	{
		return Row.GroupId == GroupId;
	});
	const FString Trimmed = NewName.ToString().TrimStartAndEnd();
	if (!Group || Trimmed.IsEmpty() || Group->DisplayName.ToString().Equals(Trimmed, ESearchCase::CaseSensitive))
		return false;
	if (Asset.LayerGroups.ContainsByPredicate([&](const FCharacterLayerGroupInfo& Row)
	{
		return Row.GroupId != GroupId && Row.DisplayName.ToString().Equals(Trimmed, ESearchCase::IgnoreCase);
	})) return false;
	Paper2DPlusLayerStructurePrivate::FRawModelMutation Mutation(Model);
	FScopedTransaction Transaction(LOCTEXT("RenameLayerGroup", "Rename Layer Group"));
	Asset.Modify();
	Group->DisplayName = FText::FromString(Trimmed);
	Paper2DPlusLayerStructurePrivate::NotifyAssetChanged(Model);
	return true;
}

bool FLayerStructureController::DeleteGroup(
	UPaper2DPlusCharacterLayerAsset& Asset,
	FCharacterProfileEditorModel& Model,
	const FGuid& GroupId)
{
	const int32 Index = Asset.LayerGroups.IndexOfByPredicate([&GroupId](const FCharacterLayerGroupInfo& Row)
	{
		return Row.GroupId == GroupId;
	});
	if (!Asset.LayerGroups.IsValidIndex(Index)) return false;
	Paper2DPlusLayerStructurePrivate::FRawModelMutation Mutation(Model);
	FScopedTransaction Transaction(LOCTEXT("DeleteLayerGroup", "Delete Layer Group"));
	Asset.Modify();
	for (FCharacterLayer& Layer : Asset.Layers)
		if (Layer.GroupId == GroupId) Layer.GroupId.Invalidate();
	Asset.LayerGroups.RemoveAt(Index);
	Paper2DPlusLayerStructurePrivate::NotifyAssetChanged(Model);
	return true;
}

bool FLayerStructureController::MoveLayerToGroup(
	UPaper2DPlusCharacterLayerAsset& Asset,
	FCharacterProfileEditorModel& Model,
	const FGuid& LayerId,
	const FGuid& GroupId)
{
	FCharacterLayer* Layer = Paper2DPlusLayerStructurePrivate::FindLayer(Asset, LayerId);
	if (!Layer || Layer->GroupId == GroupId) return false;
	if (GroupId.IsValid() && !Asset.LayerGroups.ContainsByPredicate([&GroupId](const FCharacterLayerGroupInfo& Row)
	{
		return Row.GroupId == GroupId;
	})) return false;
	Paper2DPlusLayerStructurePrivate::FRawModelMutation Mutation(Model);
	FScopedTransaction Transaction(LOCTEXT("MoveLayerGroup", "Move Layer to Group"));
	Asset.Modify();
	Layer->GroupId = GroupId;
	Paper2DPlusLayerStructurePrivate::NotifyAssetChanged(Model);
	return true;
}

bool FLayerStructureController::MoveLayerByDelta(
	UPaper2DPlusCharacterLayerAsset& Asset,
	FCharacterProfileEditorModel& Model,
	const FGuid& LayerId,
	int32 Delta)
{
	const int32 From = Asset.Layers.IndexOfByPredicate([&LayerId](const FCharacterLayer& Layer)
	{
		return Layer.LayerId == LayerId;
	});
	if (!Asset.Layers.IsValidIndex(From)) return false;
	const int32 To = FMath::Clamp(From + Delta, 0, Asset.Layers.Num() - 1);
	if (From == To) return false;
	Paper2DPlusLayerStructurePrivate::FRawModelMutation Mutation(Model);
	FScopedTransaction Transaction(LOCTEXT("ReorderLayer", "Reorder Character Layer"));
	Asset.Modify();
	FCharacterLayer Moving = Asset.Layers[From];
	Asset.Layers.RemoveAt(From);
	Asset.Layers.Insert(MoveTemp(Moving), To);
	Model.ReconcileLayerSelectionIdentity();
	Paper2DPlusLayerStructurePrivate::NotifyAssetChanged(Model);
	return true;
}

bool FLayerStructureController::DeleteLayer(
	UPaper2DPlusCharacterLayerAsset& Asset,
	FCharacterProfileEditorModel& Model,
	const FGuid& LayerId)
{
	const int32 Index = Asset.Layers.IndexOfByPredicate([&LayerId](const FCharacterLayer& Layer)
	{
		return Layer.LayerId == LayerId;
	});
	if (!Asset.Layers.IsValidIndex(Index)) return false;
	const FGuid RemovedLayerId = Asset.Layers[Index].LayerId;
	Paper2DPlusLayerStructurePrivate::FRawModelMutation Mutation(Model);
	FScopedTransaction Transaction(LOCTEXT("DeleteCharacterLayer", "Delete Character Layer"));
	Asset.Modify();
	Asset.Layers.RemoveAt(Index);
	for (FCharacterLayerAppearancePreset& Preset : Asset.AppearancePresets)
	{
		Preset.ActiveLayerIds.Remove(RemovedLayerId);
	}
	// Stable selection never jumps to an arbitrary neighbor. Deleting another row preserves the selected
	// LayerId; deleting the selected row leaves that identity explicit-but-unresolved until the designer
	// chooses another source. NotifyAssetChanged reconciles only the cached array index.
	Paper2DPlusLayerStructurePrivate::NotifyAssetChanged(Model);
	return true;
}

void SLayerStructureTree::Construct(const FArguments& InArgs)
{
	Model = InArgs._Model;
	LayerAsset = InArgs._LayerAsset;
	if (Model.IsValid())
	{
		ModelLayerSelectionHandle = Model->OnLayerSelectionChanged.AddSP(
			this, &SLayerStructureTree::HandleModelLayerSelectionChanged);
		ModelAssetDataHandle = Model->OnAssetDataChanged.AddSP(this, &SLayerStructureTree::HandleExternalChange);
		ModelExternalHandle = Model->OnAssetExternallyModified.AddSP(this, &SLayerStructureTree::HandleExternalChange);
	}

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(3.0f, 2.0f))
		[
			SNew(SHorizontalBox)
			.ToolTipText(LOCTEXT("StructureHeaderTip", "Layer organization and global paint/gameplay/publish order. P is temporary Preview visibility."))
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("StructureHeading", "Structure"))
				.Font(FAppStyle::GetFontStyle("HeadingExtraSmall"))
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SBox).WidthOverride(24.0f).HAlign(HAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("PreviewColumn", "P"))
					.ToolTipText(LOCTEXT("PreviewColumnTip", "Preview only; never published."))
					.AccessibleText(LOCTEXT("PreviewColumnAccessible", "Preview visibility column"))
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "NoBorder")
				.ContentPadding(FMargin(5.0f, 0.0f))
				.Text(LOCTEXT("AddGroup", "+"))
				.ToolTipText(LOCTEXT("AddGroupTip", "Add an organization-only layer group."))
				.AccessibleText(LOCTEXT("AddGroupAccessible", "Add layer group"))
				.OnClicked(this, &SLayerStructureTree::AddGroupClicked)
			]
		]
		+ SVerticalBox::Slot().FillHeight(1.0f)
		[
			SNew(SOverlay)
			+ SOverlay::Slot()
			[
				SAssignNew(TreeView, STreeView<TSharedPtr<FLayerStructureNode>>)
				.TreeItemsSource(&Roots)
				.OnGenerateRow(this, &SLayerStructureTree::GenerateRow)
				.OnGetChildren(this, &SLayerStructureTree::HandleGetChildren)
				.OnSelectionChanged(this, &SLayerStructureTree::HandleSelectionChanged)
				.SelectionMode(ESelectionMode::Single)
			]
			+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("Empty", "No layers yet. Import layered art or add a layer to begin."))
				.AutoWrapText(true)
				.Visibility_Lambda([this]()
				{
					const UPaper2DPlusCharacterLayerAsset* Asset = LayerAsset.Get();
					return !Asset || Asset->Layers.IsEmpty()
						? EVisibility::Visible : EVisibility::Collapsed;
				})
			]
		]
	];
	Refresh();
}

SLayerStructureTree::~SLayerStructureTree()
{
	if (Model.IsValid())
	{
		Model->OnLayerSelectionChanged.Remove(ModelLayerSelectionHandle);
		Model->OnAssetDataChanged.Remove(ModelAssetDataHandle);
		Model->OnAssetExternallyModified.Remove(ModelExternalHandle);
	}
}

void SLayerStructureTree::Refresh()
{
	UPaper2DPlusCharacterLayerAsset* Asset = LayerAsset.Get();
	if (!Asset || !TreeView.IsValid()) return;
	TSet<FGuid> Collapsed;
	for (const TSharedPtr<FLayerStructureNode>& Root : Roots)
		if (Root.IsValid() && !TreeView->IsItemExpanded(Root)) Collapsed.Add(Root->GroupId);
	Roots.Reset();
	GroupNameWidgets.Reset();
	TMap<FGuid, TSharedPtr<FLayerStructureNode>> Groups;
	for (const FCharacterLayerGroupInfo& Group : Asset->LayerGroups)
	{
		TSharedPtr<FLayerStructureNode> Node = MakeShared<FLayerStructureNode>();
		Node->Kind = ELayerStructureNodeKind::Group;
		Node->GroupId = Group.GroupId;
		Node->Label = Group.DisplayName;
		Roots.Add(Node);
		Groups.Add(Group.GroupId, Node);
	}
	TSharedPtr<FLayerStructureNode> Ungrouped = MakeShared<FLayerStructureNode>();
	Ungrouped->Kind = ELayerStructureNodeKind::Group;
	Ungrouped->Label = LOCTEXT("Ungrouped", "Ungrouped");
	TSharedPtr<FLayerStructureNode> SelectedNode;
	for (const FCharacterLayer& Layer : Asset->Layers)
	{
		TSharedPtr<FLayerStructureNode> Node = MakeShared<FLayerStructureNode>();
		Node->Kind = ELayerStructureNodeKind::Layer;
		Node->LayerId = Layer.LayerId;
		Node->GroupId = Layer.GroupId;
		Node->Label = FText::FromString(Layer.LayerName);
		TSharedPtr<FLayerStructureNode>* Parent = Groups.Find(Layer.GroupId);
		(Parent ? *Parent : Ungrouped)->Children.Add(Node);
		if (Model.IsValid() && Layer.LayerId == Model->GetSelectedLayerId()) SelectedNode = Node;
	}
	if (!Ungrouped->Children.IsEmpty()) Roots.Add(Ungrouped);
	TreeView->RequestTreeRefresh();
	for (const TSharedPtr<FLayerStructureNode>& Root : Roots)
		TreeView->SetItemExpansion(Root, !Collapsed.Contains(Root->GroupId));
	if (!SelectedNode.IsValid() && Model.IsValid() && !Asset->Layers.IsEmpty()
		&& !Model->GetSelectedLayerId().IsValid())
	{
		Model->SetSelectedLayerById(Asset->Layers[0].LayerId);
		return;
	}
	bSyncingSelection = true;
	TreeView->ClearSelection();
	if (SelectedNode.IsValid()) TreeView->SetItemSelection(SelectedNode, true, ESelectInfo::Direct);
	bSyncingSelection = false;
}

int32 SLayerStructureTree::GetLayerRowCountForTests() const
{
	int32 Count = 0;
	for (const TSharedPtr<FLayerStructureNode>& Root : Roots) if (Root.IsValid()) Count += Root->Children.Num();
	return Count;
}

bool SLayerStructureTree::SelectLayerForTests(const FGuid& LayerId)
{
	if (!Model.IsValid()) return false;
	Model->SetSelectedLayerById(LayerId);
	return Model->GetSelectedLayerId() == LayerId;
}

TSharedRef<ITableRow> SLayerStructureTree::GenerateRow(
	TSharedPtr<FLayerStructureNode> Item,
	const TSharedRef<STableViewBase>& OwnerTable)
{
	const bool bGroup = Item.IsValid() && Item->Kind == ELayerStructureNodeKind::Group;
	const FGuid GroupId = Item.IsValid() ? Item->GroupId : FGuid();
	const FGuid LayerId = Item.IsValid() ? Item->LayerId : FGuid();
	const FText Label = Item.IsValid() ? Item->Label : FText::GetEmpty();
	const TWeakPtr<SLayerStructureTree> WeakTree = SharedThis(this);

	TSharedRef<SWidget> LabelWidget = SNew(STextBlock).Text(Label);
	if (bGroup && GroupId.IsValid())
	{
		TSharedRef<SInlineEditableTextBlock> EditableName = SNew(SInlineEditableTextBlock)
			.Text(Label)
			.Font(FAppStyle::GetFontStyle("HeadingExtraSmall"))
			.ToolTipText(LOCTEXT("RenameGroupTip", "Double-click or press F2 to rename this group."))
			.OnTextCommitted(this, &SLayerStructureTree::CommitGroupName, GroupId);
		GroupNameWidgets.Add(GroupId, EditableName);
		LabelWidget = EditableName;
	}
	else if (bGroup)
	{
		LabelWidget = SNew(STextBlock)
			.Text(Label)
			.Font(FAppStyle::GetFontStyle("HeadingExtraSmall"));
	}

	TSharedRef<SWidget> ActionWidget = SNew(SComboButton)
		.ButtonStyle(FAppStyle::Get(), "NoBorder")
		.ContentPadding(FMargin(4.0f, 0.0f))
		.ButtonContent()
		[
			SNew(STextBlock).Text(FText::FromString(TEXT("⋯")))
		]
		.ToolTipText(LOCTEXT("LayerActionsTip", "Move, reorder, or delete this layer."))
		.AccessibleText(LOCTEXT("LayerActionsAccessible", "Layer actions"))
		.OnGetMenuContent_Lambda([WeakTree, LayerId]() -> TSharedRef<SWidget>
		{
			const TSharedPtr<SLayerStructureTree> Pinned = WeakTree.Pin();
			return Pinned.IsValid()
				? Pinned->BuildMoveMenu(LayerId)
				: StaticCastSharedRef<SWidget>(SNew(STextBlock));
		});
	if (bGroup)
	{
		ActionWidget = SNew(SComboButton)
			.ButtonStyle(FAppStyle::Get(), "NoBorder")
			.ContentPadding(FMargin(4.0f, 0.0f))
			.ButtonContent()[SNew(STextBlock).Text(FText::FromString(TEXT("⋯")))]
			.ToolTipText(GroupId.IsValid()
				? LOCTEXT("GroupActionsTip", "Rename or delete this organization group.")
				: LOCTEXT("UngroupedTip", "Ungrouped is derived and has no group actions."))
			.AccessibleText(LOCTEXT("GroupActionsAccessible", "Group actions"))
			.IsEnabled(GroupId.IsValid())
			.OnGetMenuContent_Lambda([WeakTree, GroupId]() -> TSharedRef<SWidget>
			{
				const TSharedPtr<SLayerStructureTree> Pinned = WeakTree.Pin();
				return Pinned.IsValid()
					? Pinned->BuildGroupMenu(GroupId)
					: StaticCastSharedRef<SWidget>(SNew(STextBlock));
			});
	}

	return SNew(STableRow<TSharedPtr<FLayerStructureNode>>, OwnerTable)
	.Padding(bGroup ? FMargin(2.0f, 1.0f) : FMargin(2.0f, 0.0f))
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
		[
			LabelWidget
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2.0f, 0.0f)
		[
			SNew(SCheckBox)
			.ToolTipText(bGroup
				? LOCTEXT("GroupPreviewTip", "Preview only: show or hide every child without changing the asset.")
				: LOCTEXT("LayerPreviewTip", "Preview only: show or hide this layer without changing the asset."))
			.AccessibleText(bGroup ? LOCTEXT("GroupPreviewAccessible", "Group Preview visibility")
				: LOCTEXT("LayerPreviewAccessible", "Layer Preview visibility"))
			.IsChecked_Lambda([this, Item, bGroup]()
			{
				UPaper2DPlusCharacterLayerAsset* Asset = LayerAsset.Get();
				if (!Asset || !Model.IsValid()) return ECheckBoxState::Unchecked;
				if (bGroup) return FLayerStructureController::GetGroupPreviewState(*Asset, *Model, Item->GroupId);
				const FCharacterLayer* Layer = Paper2DPlusLayerStructurePrivate::FindLayer(*Asset, Item->LayerId);
				return Layer && Model->IsLayerVisible(Layer->LayerName)
					? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
			})
			.OnCheckStateChanged_Lambda([this, Item, bGroup](ECheckBoxState State)
			{
				UPaper2DPlusCharacterLayerAsset* Asset = LayerAsset.Get();
				if (!Asset || !Model.IsValid()) return;
				const bool bVisible = State == ECheckBoxState::Checked;
				if (bGroup) FLayerStructureController::SetGroupPreview(*Asset, *Model, Item->GroupId, bVisible);
				else FLayerStructureController::SetLayerPreview(*Asset, *Model, Item->LayerId, bVisible);
			})
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(1.0f, 0.0f)
		[
			ActionWidget
		]
	];
}

void SLayerStructureTree::HandleGetChildren(
	TSharedPtr<FLayerStructureNode> Item,
	TArray<TSharedPtr<FLayerStructureNode>>& OutChildren) const
{
	if (Item.IsValid()) OutChildren.Append(Item->Children);
}

void SLayerStructureTree::HandleSelectionChanged(
	TSharedPtr<FLayerStructureNode> Item,
	ESelectInfo::Type SelectInfo)
{
	if (bSyncingSelection || !Item.IsValid() || Item->Kind != ELayerStructureNodeKind::Layer || !Model.IsValid()) return;
	Model->SetSelectedLayerById(Item->LayerId);
}

void SLayerStructureTree::HandleModelLayerSelectionChanged(int32 NewIndex)
{
	Refresh();
}

void SLayerStructureTree::HandleExternalChange()
{
	Refresh();
}

TSharedRef<SWidget> SLayerStructureTree::BuildMoveMenu(const FGuid& LayerId)
{
	const TWeakPtr<SLayerStructureTree> WeakTree = SharedThis(this);
	FMenuBuilder Menu(true, nullptr);
	Menu.AddMenuEntry(LOCTEXT("MoveUp", "Move Up"), LOCTEXT("MoveUpTip", "Move one step earlier in global order."),
		FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([WeakTree, LayerId]()
		{
			if (const TSharedPtr<SLayerStructureTree> Pinned = WeakTree.Pin()) Pinned->MoveLayerClicked(LayerId, -1);
		}), FCanExecuteAction::CreateLambda([WeakTree, LayerId]()
		{
			const TSharedPtr<SLayerStructureTree> Pinned = WeakTree.Pin();
			const UPaper2DPlusCharacterLayerAsset* Asset = Pinned.IsValid() ? Pinned->LayerAsset.Get() : nullptr;
			return Asset && Asset->Layers.IndexOfByPredicate([&LayerId](const FCharacterLayer& Layer) { return Layer.LayerId == LayerId; }) > 0;
		})));
	Menu.AddMenuEntry(LOCTEXT("MoveDown", "Move Down"), LOCTEXT("MoveDownTip", "Move one step later in global order."),
		FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([WeakTree, LayerId]()
		{
			if (const TSharedPtr<SLayerStructureTree> Pinned = WeakTree.Pin()) Pinned->MoveLayerClicked(LayerId, 1);
		}), FCanExecuteAction::CreateLambda([WeakTree, LayerId]()
		{
			const TSharedPtr<SLayerStructureTree> Pinned = WeakTree.Pin();
			const UPaper2DPlusCharacterLayerAsset* Asset = Pinned.IsValid() ? Pinned->LayerAsset.Get() : nullptr;
			if (!Asset) return false;
			const int32 Index = Asset->Layers.IndexOfByPredicate([&LayerId](const FCharacterLayer& Layer) { return Layer.LayerId == LayerId; });
			return Index != INDEX_NONE && Index + 1 < Asset->Layers.Num();
		})));
	Menu.AddMenuSeparator();
	UPaper2DPlusCharacterLayerAsset* Asset = LayerAsset.Get();
	if (Asset)
	{
		Menu.AddMenuEntry(LOCTEXT("MoveUngrouped", "Move to Ungrouped"), FText::GetEmpty(), FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([WeakTree, LayerId]()
			{
				const TSharedPtr<SLayerStructureTree> Pinned = WeakTree.Pin();
				if (!Pinned.IsValid()) return;
				if (UPaper2DPlusCharacterLayerAsset* Current = Pinned->LayerAsset.Get(); Current && Pinned->Model.IsValid())
					FLayerStructureController::MoveLayerToGroup(*Current, *Pinned->Model, LayerId, FGuid());
				Pinned->Refresh();
			})));
		for (const FCharacterLayerGroupInfo& Group : Asset->LayerGroups)
		{
			Menu.AddMenuEntry(Group.DisplayName, FText::GetEmpty(), FSlateIcon(),
				FUIAction(FExecuteAction::CreateLambda([WeakTree, LayerId, GroupId = Group.GroupId]()
				{
					const TSharedPtr<SLayerStructureTree> Pinned = WeakTree.Pin();
					if (!Pinned.IsValid()) return;
					if (UPaper2DPlusCharacterLayerAsset* Current = Pinned->LayerAsset.Get(); Current && Pinned->Model.IsValid())
						FLayerStructureController::MoveLayerToGroup(*Current, *Pinned->Model, LayerId, GroupId);
					Pinned->Refresh();
				})));
		}
	}
	Menu.AddMenuSeparator();
	Menu.AddMenuEntry(LOCTEXT("DeleteLayer", "Delete Layer…"),
		LOCTEXT("DeleteLayerTip", "Delete this layer and prune its stable ID from every appearance preset."),
		FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([WeakTree, LayerId]()
		{
			if (const TSharedPtr<SLayerStructureTree> Pinned = WeakTree.Pin()) Pinned->DeleteLayerClicked(LayerId);
		})));
	return Menu.MakeWidget();
}

TSharedRef<SWidget> SLayerStructureTree::BuildGroupMenu(const FGuid& GroupId)
{
	const TWeakPtr<SLayerStructureTree> WeakTree = SharedThis(this);
	FMenuBuilder Menu(true, nullptr);
	Menu.AddMenuEntry(
		LOCTEXT("RenameGroup", "Rename Group"),
		LOCTEXT("RenameGroupMenuTip", "Edit this organization group's display name."),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([WeakTree, GroupId]()
		{
			if (const TSharedPtr<SLayerStructureTree> Pinned = WeakTree.Pin())
			{
				Pinned->BeginGroupRename(GroupId);
			}
		})));
	Menu.AddMenuSeparator();
	Menu.AddMenuEntry(
		LOCTEXT("DeleteGroup", "Delete Group…"),
		LOCTEXT("DeleteGroupTip", "Delete this group and move its layers to Ungrouped."),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([WeakTree, GroupId]()
		{
			if (const TSharedPtr<SLayerStructureTree> Pinned = WeakTree.Pin())
			{
				Pinned->DeleteGroupClicked(GroupId);
			}
		})));
	return Menu.MakeWidget();
}

FReply SLayerStructureTree::AddGroupClicked()
{
	if (UPaper2DPlusCharacterLayerAsset* Asset = LayerAsset.Get(); Asset && Model.IsValid())
		FLayerStructureController::AddGroup(*Asset, *Model);
	Refresh();
	return FReply::Handled();
}

FReply SLayerStructureTree::DeleteGroupClicked(FGuid GroupId)
{
	if (UPaper2DPlusCharacterLayerAsset* Asset = LayerAsset.Get(); Asset && Model.IsValid())
		FLayerStructureController::DeleteGroup(*Asset, *Model, GroupId);
	Refresh();
	return FReply::Handled();
}

FReply SLayerStructureTree::DeleteLayerClicked(FGuid LayerId)
{
	UPaper2DPlusCharacterLayerAsset* Asset = LayerAsset.Get();
	const FCharacterLayer* Layer = Asset ? Paper2DPlusLayerStructurePrivate::FindLayer(*Asset, LayerId) : nullptr;
	if (!Layer || !Model.IsValid()) return FReply::Handled();
	const FText Message = FText::Format(
		LOCTEXT("ConfirmDeleteLayer", "Delete layer “{0}”?\n\nIts art mappings, local gameplay, offsets, and every appearance-preset reference will be removed. This is undoable."),
		FText::FromString(Layer->LayerName));
	if (FMessageDialog::Open(EAppMsgType::YesNo, Message) == EAppReturnType::Yes)
		FLayerStructureController::DeleteLayer(*Asset, *Model, LayerId);
	Refresh();
	return FReply::Handled();
}

FReply SLayerStructureTree::MoveLayerClicked(FGuid LayerId, int32 Delta)
{
	if (UPaper2DPlusCharacterLayerAsset* Asset = LayerAsset.Get(); Asset && Model.IsValid())
		FLayerStructureController::MoveLayerByDelta(*Asset, *Model, LayerId, Delta);
	Refresh();
	return FReply::Handled();
}

void SLayerStructureTree::BeginGroupRename(FGuid GroupId)
{
	PendingRenameGroupId = GroupId;
	if (RenameTimerHandle.IsValid())
	{
		UnRegisterActiveTimer(RenameTimerHandle.Pin().ToSharedRef());
	}
	RenameTimerHandle = RegisterActiveTimer(
		0.0f,
		FWidgetActiveTimerDelegate::CreateSP(
			this,
			&SLayerStructureTree::HandleDeferredGroupRename));
}

EActiveTimerReturnType SLayerStructureTree::HandleDeferredGroupRename(double, float)
{
	RenameTimerHandle.Reset();
	if (TWeakPtr<SInlineEditableTextBlock>* Found = GroupNameWidgets.Find(PendingRenameGroupId))
	{
		if (const TSharedPtr<SInlineEditableTextBlock> NameWidget = Found->Pin())
		{
			NameWidget->EnterEditingMode();
		}
	}
	PendingRenameGroupId.Invalidate();
	return EActiveTimerReturnType::Stop;
}

void SLayerStructureTree::CommitGroupName(
	const FText& Text,
	ETextCommit::Type CommitType,
	FGuid GroupId)
{
	if (CommitType == ETextCommit::OnCleared) return;
	if (UPaper2DPlusCharacterLayerAsset* Asset = LayerAsset.Get(); Asset && Model.IsValid())
		FLayerStructureController::RenameGroup(*Asset, *Model, GroupId, Text);
	Refresh();
}

#undef LOCTEXT_NAMESPACE
