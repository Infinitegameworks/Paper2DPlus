// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "CharacterDataAssetEditor.h"
#include "EditorCanvasUtils.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Images/SImage.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "PaperFlipbook.h"
#include "PaperSprite.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Colors/SColorPicker.h"
#include "Misc/MessageDialog.h"

#define LOCTEXT_NAMESPACE "CharacterDataAssetEditor"

// ==========================================
// DRAG-DROP OPERATION
// ==========================================

class FFlipbookGroupDragDropOp : public FDragDropOperation
{
public:
	DRAG_DROP_OPERATOR_TYPE(FFlipbookGroupDragDropOp, FDragDropOperation)

	TArray<int32> FlipbookIndices;

	static TSharedRef<FFlipbookGroupDragDropOp> NewFromCardDrag(const TArray<int32>& InFlipbookIndices, FName FromGroup)
	{
		TSharedRef<FFlipbookGroupDragDropOp> Op = MakeShareable(new FFlipbookGroupDragDropOp());
		Op->FlipbookIndices = InFlipbookIndices;

		if (InFlipbookIndices.Num() == 1)
		{
			Op->DefaultHoverText = FText::Format(LOCTEXT("DragSingle", "1 flipbook"), FText());
		}
		else if (FromGroup != NAME_None)
		{
			Op->DefaultHoverText = FText::Format(LOCTEXT("DragMultiFrom", "{0} flipbooks from {1}"),
				FText::AsNumber(InFlipbookIndices.Num()), FText::FromName(FromGroup));
		}
		else
		{
			Op->DefaultHoverText = FText::Format(LOCTEXT("DragMulti", "{0} flipbooks"),
				FText::AsNumber(InFlipbookIndices.Num()));
		}

		Op->Construct();
		return Op;
	}

	virtual TSharedPtr<SWidget> GetDefaultDecorator() const override
	{
		return SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
			.Padding(FMargin(6, 2))
			[
				SNew(STextBlock).Text(DefaultHoverText)
			];
	}

private:
	FText DefaultHoverText;
};

// ==========================================
// DRAG WRAPPER FOR FLIPBOOK CARDS
// ==========================================

class SFlipbookCardDragWrapper : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SFlipbookCardDragWrapper) {}
		SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_END_ARGS()

	int32 FlipbookIndex = INDEX_NONE;
	TFunction<void(const FPointerEvent&)> OnClickedFunc;
	TFunction<void(const FPointerEvent&)> OnRightClickFunc;
	TFunction<TArray<int32>()> GetDragIndicesFunc;  // Returns all selected indices for multi-drag
	TFunction<FName()> GetGroupFunc;  // Returns group of this card

	void Construct(const FArguments& InArgs)
	{
		ChildSlot[ InArgs._Content.Widget ];
	}

	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
		{
			bPotentialDrag = true;
			DragStartPos = MouseEvent.GetScreenSpacePosition();
			return FReply::Handled().CaptureMouse(SharedThis(this));
		}
		if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
		{
			if (OnRightClickFunc) { OnRightClickFunc(MouseEvent); }
			return FReply::Handled();
		}
		return FReply::Unhandled();
	}

	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		if (bPotentialDrag && MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
		{
			bPotentialDrag = false;
			if (OnClickedFunc) { OnClickedFunc(MouseEvent); }
			return FReply::Handled().ReleaseMouseCapture();
		}
		return FReply::Unhandled();
	}

	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		if (bPotentialDrag)
		{
			float Distance = FVector2D::Distance(MouseEvent.GetScreenSpacePosition(), DragStartPos);
			if (Distance > 5.0f)
			{
				bPotentialDrag = false;
				TArray<int32> DragIndices;
				if (GetDragIndicesFunc)
				{
					DragIndices = GetDragIndicesFunc();
				}
				if (DragIndices.Num() == 0)
				{
					DragIndices.Add(FlipbookIndex);
				}

				FName FromGroup = GetGroupFunc ? GetGroupFunc() : NAME_None;
				return FReply::Handled()
					.ReleaseMouseCapture()
					.BeginDragDrop(FFlipbookGroupDragDropOp::NewFromCardDrag(DragIndices, FromGroup));
			}
		}
		return FReply::Unhandled();
	}

	virtual void OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent) override
	{
		bPotentialDrag = false;
	}

private:
	bool bPotentialDrag = false;
	FVector2D DragStartPos;
};

// ==========================================
// DROP TARGET WRAPPER
// ==========================================

class SFlipbookGroupDropTarget : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SFlipbookGroupDropTarget) {}
		SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_END_ARGS()

	FName TargetGroup;
	TFunction<void(const TArray<int32>&, FName)> OnDropFunc;  // (FlipbookIndices, TargetGroup)

	void Construct(const FArguments& InArgs)
	{
		ChildSlot[ InArgs._Content.Widget ];
	}

	virtual void OnDragEnter(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override
	{
		if (DragDropEvent.GetOperationAs<FFlipbookGroupDragDropOp>().IsValid())
		{
			bDragOver = true;
			Invalidate(EInvalidateWidgetReason::Paint);
		}
	}

	virtual void OnDragLeave(const FDragDropEvent& DragDropEvent) override
	{
		bDragOver = false;
		Invalidate(EInvalidateWidgetReason::Paint);
	}

	virtual FReply OnDragOver(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override
	{
		if (DragDropEvent.GetOperationAs<FFlipbookGroupDragDropOp>().IsValid())
		{
			return FReply::Handled();
		}
		return FReply::Unhandled();
	}

	virtual FReply OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override
	{
		bDragOver = false;
		Invalidate(EInvalidateWidgetReason::Paint);
		TSharedPtr<FFlipbookGroupDragDropOp> Op = DragDropEvent.GetOperationAs<FFlipbookGroupDragDropOp>();
		if (Op.IsValid() && OnDropFunc)
		{
			OnDropFunc(Op->FlipbookIndices, TargetGroup);
			return FReply::Handled();
		}
		return FReply::Unhandled();
	}

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override
	{
		if (bDragOver)
		{
			// Draw a highlight border when hovering
			const FVector2D Size = AllottedGeometry.GetLocalSize();
			const float Thickness = 2.0f;
			const FLinearColor HighlightColor(0.3f, 0.5f, 0.8f, 0.6f);
			const FSlateBrush* WhiteBrush = FAppStyle::GetBrush("WhiteBrush");

			// Top
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 1, AllottedGeometry.ToPaintGeometry(FVector2D(Size.X, Thickness), FSlateLayoutTransform()),
				WhiteBrush, ESlateDrawEffect::None, HighlightColor);
			// Bottom
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 1, AllottedGeometry.ToPaintGeometry(FVector2D(Size.X, Thickness), FSlateLayoutTransform(FVector2D(0, Size.Y - Thickness))),
				WhiteBrush, ESlateDrawEffect::None, HighlightColor);
			// Left
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 1, AllottedGeometry.ToPaintGeometry(FVector2D(Thickness, Size.Y), FSlateLayoutTransform()),
				WhiteBrush, ESlateDrawEffect::None, HighlightColor);
			// Right
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 1, AllottedGeometry.ToPaintGeometry(FVector2D(Thickness, Size.Y), FSlateLayoutTransform(FVector2D(Size.X - Thickness, 0))),
				WhiteBrush, ESlateDrawEffect::None, HighlightColor);
		}

		return SCompoundWidget::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	}

private:
	bool bDragOver = false;
};

// ==========================================
// BUILD & REFRESH
// ==========================================

TSharedRef<SWidget> SCharacterDataAssetEditor::BuildFlipbookGroupsPanel()
{
	return SNew(SVerticalBox)

		// Toolbar: + Add Flipbook, - Remove Selected, + New Group, Auto-group, View toggle
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0, 0, 0, 4)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0, 0, 4, 0)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
				.Text(LOCTEXT("AddFlipbook", "+ Add Flipbook"))
				.ToolTipText(LOCTEXT("AddFlipbookTooltip", "Add a new flipbook entry and open the flipbook picker"))
				.OnClicked_Lambda([this]()
				{
					AddNewFlipbook();
					RefreshOverviewFlipbookList();
					OpenFlipbookPicker(SelectedFlipbookIndex);
					return FReply::Handled();
				})
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0, 0, 4, 0)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
				.Text(LOCTEXT("RemoveFlipbook", "- Remove Selected"))
				.ToolTipText(LOCTEXT("RemoveFlipbookTooltip", "Remove the currently selected flipbook and all associated hitbox data"))
				.IsEnabled_Lambda([this]() { return Asset.IsValid() && Asset->Flipbooks.IsValidIndex(SelectedFlipbookIndex); })
				.OnClicked_Lambda([this]() { RemoveSelectedFlipbook(); RefreshOverviewFlipbookList(); return FReply::Handled(); })
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0, 0, 4, 0)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
				.Text(LOCTEXT("NewGroup", "+ New Group"))
				.ToolTipText(LOCTEXT("NewGroupTooltip", "Create a new flipbook group at the root level"))
				.OnClicked_Lambda([this]()
				{
					CreateFlipbookGroup();
					return FReply::Handled();
				})
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0, 0, 4, 0)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
				.Text(LOCTEXT("AutoGroup", "Auto-group"))
				.ToolTipText(LOCTEXT("AutoGroupTooltip", "Automatically group ungrouped flipbooks by name prefix"))
				.OnClicked_Lambda([this]()
				{
					AutoGroupByPrefix();
					return FReply::Handled();
				})
			]

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SNullWidget::NullWidget
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "NoBorder")
				.ToolTipText_Lambda([this]()
				{
					return bFlipbookGroupGridView
						? LOCTEXT("SwitchToList", "Switch to list view")
						: LOCTEXT("SwitchToGrid", "Switch to grid view");
				})
				.OnClicked_Lambda([this]()
				{
					bFlipbookGroupGridView = !bFlipbookGroupGridView;
					RefreshFlipbookGroupsPanel();
					return FReply::Handled();
				})
				[
					SNew(STextBlock)
					.Text_Lambda([this]() { return bFlipbookGroupGridView ? LOCTEXT("ListView", "List") : LOCTEXT("GridView", "Grid"); })
				]
			]
		]

		// Search
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0, 0, 0, 4)
		[
			SNew(SEditableTextBox)
			.HintText(LOCTEXT("GroupSearchHint", "Search flipbooks..."))
			.Text_Lambda([this]() { return FText::FromString(FlipbookGroupSearchText); })
			.OnTextChanged_Lambda([this](const FText& NewText)
			{
				FlipbookGroupSearchText = NewText.ToString();

				// Debounce search (150ms)
				if (auto PinnedTimer = FlipbookGroupSearchDebounceTimer.Pin())
				{
					UnRegisterActiveTimer(PinnedTimer.ToSharedRef());
				}
				FlipbookGroupSearchDebounceTimer = RegisterActiveTimer(0.15f,
					FWidgetActiveTimerDelegate::CreateLambda([this](double, float) -> EActiveTimerReturnType
					{
						RefreshFlipbookGroupsPanel();
						return EActiveTimerReturnType::Stop;
					}));
			})
		]

		// Groups content
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			[
				SAssignNew(FlipbookGroupsListBox, SVerticalBox)
			]
		];
}

void SCharacterDataAssetEditor::RefreshFlipbookGroupsPanel()
{
	if (!FlipbookGroupsListBox.IsValid() || !Asset.IsValid()) return;

	// Prune stale selection indices (keep valid ones across refresh)
	if (Asset.IsValid())
	{
		TSet<int32> ValidSelection;
		for (int32 Idx : SelectedFlipbookCards)
		{
			if (Asset->Flipbooks.IsValidIndex(Idx))
			{
				ValidSelection.Add(Idx);
			}
		}
		SelectedFlipbookCards = MoveTemp(ValidSelection);
		if (SelectionAnchorIndex != INDEX_NONE && !Asset->Flipbooks.IsValidIndex(SelectionAnchorIndex))
		{
			SelectionAnchorIndex = INDEX_NONE;
		}
	}
	else
	{
		SelectedFlipbookCards.Empty();
		SelectionAnchorIndex = INDEX_NONE;
	}
	FlipbookGroupNameTexts.Empty();

	FlipbookGroupsListBox->ClearChildren();

	// Prune stale collapse state
	TSet<FName> ValidGroupNames;
	for (const FFlipbookGroupInfo& Group : Asset->FlipbookGroups)
	{
		ValidGroupNames.Add(Group.GroupName);
	}
	TSet<FName> StaleNames;
	for (const FName& Name : CollapsedFlipbookGroups)
	{
		if (!ValidGroupNames.Contains(Name))
		{
			StaleNames.Add(Name);
		}
	}
	for (const FName& Name : StaleNames)
	{
		CollapsedFlipbookGroups.Remove(Name);
	}

	// Build tree
	TMap<FName, TArray<const FFlipbookGroupInfo*>> Tree = Asset->GetFlipbookGroupTree();

	// Partition flipbooks by group, sorted alphabetically within each group
	TMap<FName, TArray<int32>> FlipbooksByGroup;
	TArray<int32> SortedIndices = GetSortedFlipbookIndices();
	for (int32 i : SortedIndices)
	{
		FlipbooksByGroup.FindOrAdd(Asset->Flipbooks[i].FlipbookGroup).Add(i);
	}

	// Ungrouped section first
	FlipbookGroupsListBox->AddSlot()
	.AutoHeight()
	.Padding(0, 0, 0, 4)
	[
		BuildGroupSection(nullptr, NAME_None, 0, Tree, FlipbooksByGroup)
	];

	// Root-level groups
	if (const TArray<const FFlipbookGroupInfo*>* RootGroups = Tree.Find(NAME_None))
	{
		for (const FFlipbookGroupInfo* GroupInfo : *RootGroups)
		{
			FlipbookGroupsListBox->AddSlot()
			.AutoHeight()
			.Padding(0, 0, 0, 4)
			[
				BuildGroupSection(GroupInfo, GroupInfo->GroupName, 0, Tree, FlipbooksByGroup)
			];
		}
	}

	// Deferred rename entry
	if (PendingRenameFlipbookGroup != NAME_None)
	{
		FName PendingName = PendingRenameFlipbookGroup;
		PendingRenameFlipbookGroup = NAME_None;
		RegisterActiveTimer(0.0f, FWidgetActiveTimerDelegate::CreateLambda(
			[this, PendingName](double, float) -> EActiveTimerReturnType
			{
				if (TSharedPtr<SInlineEditableTextBlock>* FoundText = FlipbookGroupNameTexts.Find(PendingName))
				{
					if (FoundText->IsValid())
					{
						(*FoundText)->EnterEditingMode();
					}
				}
				return EActiveTimerReturnType::Stop;
			}));
	}
}

// ==========================================
// GROUP SECTION RENDERING
// ==========================================

TSharedRef<SWidget> SCharacterDataAssetEditor::BuildGroupSection(
	const FFlipbookGroupInfo* GroupInfo,
	FName GroupName,
	int32 NestLevel,
	const TMap<FName, TArray<const FFlipbookGroupInfo*>>& Tree,
	const TMap<FName, TArray<int32>>& FlipbooksByGroup)
{
	// Get flipbooks for this group
	const TArray<int32>* FlipbookIndices = FlipbooksByGroup.Find(GroupName);
	int32 FlipbookCount = FlipbookIndices ? FlipbookIndices->Num() : 0;

	// Filter by search
	TArray<int32> FilteredIndices;
	if (FlipbookIndices)
	{
		for (int32 Idx : *FlipbookIndices)
		{
			if (PassesFlipbookGroupSearch(Asset->Flipbooks[Idx]))
			{
				FilteredIndices.Add(Idx);
			}
		}
	}

	// Check if any descendants have matching flipbooks (for search visibility)
	bool bHasMatchingChildren = FilteredIndices.Num() > 0;
	if (!bHasMatchingChildren && !FlipbookGroupSearchText.IsEmpty())
	{
		// Recursive check through all descendant groups
		TArray<FName> GroupsToCheck;
		GroupsToCheck.Add(GroupName);
		while (GroupsToCheck.Num() > 0 && !bHasMatchingChildren)
		{
			FName CheckGroup = GroupsToCheck.Pop(EAllowShrinking::No);
			if (const TArray<const FFlipbookGroupInfo*>* ChildGroups = Tree.Find(CheckGroup))
			{
				for (const FFlipbookGroupInfo* ChildGroup : *ChildGroups)
				{
					// Check this child's flipbooks
					const TArray<int32>* ChildFlipbooks = FlipbooksByGroup.Find(ChildGroup->GroupName);
					if (ChildFlipbooks)
					{
						for (int32 Idx : *ChildFlipbooks)
						{
							if (PassesFlipbookGroupSearch(Asset->Flipbooks[Idx]))
							{
								bHasMatchingChildren = true;
								break;
							}
						}
					}
					if (bHasMatchingChildren) break;
					// Queue grandchildren for checking
					GroupsToCheck.Add(ChildGroup->GroupName);
				}
			}
		}

		// Also check if group name matches search
		if (!bHasMatchingChildren && GroupName != NAME_None)
		{
			bHasMatchingChildren = GroupName.ToString().Contains(FlipbookGroupSearchText, ESearchCase::IgnoreCase);
		}
	}

	// Hide groups with no matches during search
	if (!FlipbookGroupSearchText.IsEmpty() && !bHasMatchingChildren)
	{
		return SNullWidget::NullWidget;
	}

	// Build the body content (flipbook cards + child groups)
	TSharedRef<SVerticalBox> BodyContent = SNew(SVerticalBox);

	// Flipbook cards
	if (FilteredIndices.Num() > 0)
	{
		if (bFlipbookGroupGridView)
		{
			// Grid view with SWrapBox
			TSharedRef<SWrapBox> WrapBox = SNew(SWrapBox).UseAllottedSize(true);

			for (int32 FlipbookIdx : FilteredIndices)
			{
				WrapBox->AddSlot()
				.Padding(4)
				[
					BuildFlipbookCard(FlipbookIdx)
				];
			}

			BodyContent->AddSlot()
			.AutoHeight()
			.Padding(4)
			[
				WrapBox
			];
		}
		else
		{
			// List view — compact rows with drag wrappers
			for (int32 FlipbookIdx : FilteredIndices)
			{
				const FFlipbookHitboxData& FBData = Asset->Flipbooks[FlipbookIdx];
				bool bSelected = (FlipbookIdx == SelectedFlipbookIndex);
				bool bMultiSelected = SelectedFlipbookCards.Contains(FlipbookIdx);

				UPaperFlipbook* RowFlipbook = !FBData.Flipbook.IsNull() ? FBData.Flipbook.LoadSynchronous() : nullptr;

				TSharedRef<SWidget> RowContent = SNew(SBorder)
					.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
					.BorderBackgroundColor((bSelected || bMultiSelected) ? FLinearColor(0.2f, 0.4f, 0.8f, 0.3f) : FLinearColor(0.1f, 0.1f, 0.1f, 0.5f))
					.Padding(4)
					[
						SNew(SHorizontalBox)

						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(0, 0, 8, 0)
						[
							SNew(SBox)
							.WidthOverride(32)
							.HeightOverride(32)
							[
								SNew(SButton)
								.ButtonStyle(FAppStyle::Get(), "NoBorder")
								.ToolTipText(LOCTEXT("ClickToChangeFlipbookRow", "Click to change flipbook"))
								.OnClicked_Lambda([this, FlipbookIdx]()
								{
									OpenFlipbookPicker(FlipbookIdx);
									return FReply::Handled();
								})
								[
									RowFlipbook
										? StaticCastSharedRef<SWidget>(SNew(SFlipbookThumbnail).Flipbook(RowFlipbook))
										: StaticCastSharedRef<SWidget>(SNew(SBorder)
											.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder")))
								]
							]
						]

						+ SHorizontalBox::Slot()
						.FillWidth(1.0f)
						.VAlign(VAlign_Center)
						[
							SNew(STextBlock)
							.Text(FText::FromString(FBData.FlipbookName))
						]

						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						[
							SNew(STextBlock)
							.Text(FText::AsNumber(FBData.Frames.Num()))
							.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)))
						]
					];

				TSharedRef<SFlipbookCardDragWrapper> RowWrapper = SNew(SFlipbookCardDragWrapper)
					[RowContent];

				RowWrapper->FlipbookIndex = FlipbookIdx;
				RowWrapper->OnClickedFunc = [this, FlipbookIdx](const FPointerEvent& MouseEvent)
				{
					OnFlipbookGroupCardClicked(FlipbookIdx, MouseEvent);
				};
				RowWrapper->OnRightClickFunc = [this, FlipbookIdx](const FPointerEvent& MouseEvent)
				{
					if (!SelectedFlipbookCards.Contains(FlipbookIdx))
					{
						SelectedFlipbookCards.Empty();
						SelectedFlipbookCards.Add(FlipbookIdx);
						SelectionAnchorIndex = FlipbookIdx;
					}
					SelectedFlipbookIndex = FlipbookIdx;
					RefreshFlipbookGroupsPanel();
					ShowFlipbookContextMenu(FlipbookIdx);
				};
				RowWrapper->GetDragIndicesFunc = [this, FlipbookIdx]() -> TArray<int32>
				{
					if (SelectedFlipbookCards.Contains(FlipbookIdx) && SelectedFlipbookCards.Num() > 1)
					{
						return SelectedFlipbookCards.Array();
					}
					return { FlipbookIdx };
				};
				RowWrapper->GetGroupFunc = [this, FlipbookIdx]() -> FName
				{
					if (Asset.IsValid() && Asset->Flipbooks.IsValidIndex(FlipbookIdx))
					{
						return Asset->Flipbooks[FlipbookIdx].FlipbookGroup;
					}
					return NAME_None;
				};

				BodyContent->AddSlot()
				.AutoHeight()
				.Padding(2, 1)
				[
					RowWrapper
				];
			}
		}
	}

	// Child groups (recursive) — skip for Ungrouped since root groups are rendered separately
	if (GroupInfo != nullptr)
	{
		if (const TArray<const FFlipbookGroupInfo*>* ChildGroups = Tree.Find(GroupName))
		{
			for (const FFlipbookGroupInfo* ChildGroup : *ChildGroups)
			{
				BodyContent->AddSlot()
				.AutoHeight()
				.Padding(0, 2)
				[
					BuildGroupSection(ChildGroup, ChildGroup->GroupName, NestLevel + 1, Tree, FlipbooksByGroup)
				];
			}
		}
	}

	// Ungrouped section (no header — just a label)
	if (GroupName == NAME_None)
	{
		TSharedRef<SFlipbookGroupDropTarget> DropTarget = SNew(SFlipbookGroupDropTarget)
			[
				SNew(SExpandableArea)
				.InitiallyCollapsed(CollapsedFlipbookGroups.Contains(FName("__Ungrouped")))
				.OnAreaExpansionChanged_Lambda([this](bool bExpanded)
				{
					if (bExpanded)
						CollapsedFlipbookGroups.Remove(FName("__Ungrouped"));
					else
						CollapsedFlipbookGroups.Add(FName("__Ungrouped"));
				})
				.HeaderContent()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.Padding(4, 0)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("Ungrouped", "Ungrouped"))
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
						.ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f)))
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(8, 0)
					[
						SNew(SBorder)
						.BorderImage(FAppStyle::Get().GetBrush("Brushes.Recessed"))
						.Padding(FMargin(6, 2))
						[
							SNew(STextBlock)
							.Text(FText::AsNumber(FilteredIndices.Num()))
							.TextStyle(FAppStyle::Get(), "SmallText")
						]
					]
				]
				.BodyContent()
				[
					BodyContent
				]
			];
		DropTarget->TargetGroup = NAME_None;
		DropTarget->OnDropFunc = [this](const TArray<int32>& FlipbookIndices, FName TargetGroup)
		{
			OnFlipbookGroupFlipbooksDrop(FlipbookIndices, TargetGroup);
		};

		return SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[ DropTarget ];
	}

	// Named group with full header
	check(GroupInfo != nullptr);

	TSharedPtr<SInlineEditableTextBlock> GroupNameText;

	TSharedRef<SFlipbookGroupDropTarget> DropTarget = SNew(SFlipbookGroupDropTarget)
		[
			SNew(SHorizontalBox)
			// Indentation
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SSpacer).Size(FVector2D(FMath::Min(NestLevel * 16.0f, 80.0f), 0.0f))
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SNew(SExpandableArea)
				.InitiallyCollapsed(CollapsedFlipbookGroups.Contains(GroupName))
				.OnAreaExpansionChanged_Lambda([this, GroupName](bool bExpanded)
				{
					if (bExpanded)
						CollapsedFlipbookGroups.Remove(GroupName);
					else
						CollapsedFlipbookGroups.Add(GroupName);
				})
				.HeaderContent()
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::GetBrush("NoBorder"))
					.OnMouseButtonDown_Lambda([this, GroupName](const FGeometry& Geom, const FPointerEvent& MouseEvent) -> FReply
					{
						if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
						{
							ShowFlipbookGroupContextMenu(GroupName, MouseEvent.GetScreenSpacePosition());
							return FReply::Handled();
						}
						return FReply::Unhandled();
					})
					[
						SNew(SHorizontalBox)

						// Color dot
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(4, 0)
						[
							SNew(SImage)
							.Image(FAppStyle::GetBrush("WhiteBrush"))
							.ColorAndOpacity(GroupInfo->Color)
							.DesiredSizeOverride(FVector2D(12.0f, 12.0f))
						]

						// Group name (inline-editable)
						+ SHorizontalBox::Slot()
						.FillWidth(1.0f)
						.Padding(4, 0)
						[
							SAssignNew(GroupNameText, SInlineEditableTextBlock)
							.Text(FText::FromName(GroupInfo->GroupName))
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
							.OnVerifyTextChanged_Lambda([this, GroupName](const FText& InText, FText& OutErrorMessage) -> bool
							{
								return OnVerifyFlipbookGroupNameChanged(InText, OutErrorMessage, GroupName);
							})
							.OnTextCommitted_Lambda([this, GroupName](const FText& InText, ETextCommit::Type CommitType)
							{
								OnFlipbookGroupNameCommitted(InText, CommitType, GroupName);
							})
						]

						// Count badge
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(8, 0)
						[
							SNew(SBorder)
							.BorderImage(FAppStyle::Get().GetBrush("Brushes.Recessed"))
							.Padding(FMargin(6, 2))
							[
								SNew(STextBlock)
								.Text(FText::AsNumber(FlipbookCount))
								.TextStyle(FAppStyle::Get(), "SmallText")
							]
						]
					]
				]
				.BodyContent()
				[
					BodyContent
				]
			]
		];

	DropTarget->TargetGroup = GroupName;
	DropTarget->OnDropFunc = [this](const TArray<int32>& FlipbookIndices, FName TargetGroup)
	{
		OnFlipbookGroupFlipbooksDrop(FlipbookIndices, TargetGroup);
	};

	// Store text widget for programmatic rename
	if (GroupNameText.IsValid())
	{
		FlipbookGroupNameTexts.Add(GroupName, GroupNameText);
	}

	return StaticCastSharedRef<SWidget>(DropTarget);
}

TSharedRef<SWidget> SCharacterDataAssetEditor::BuildFlipbookCard(int32 FlipbookIndex)
{
	if (!Asset.IsValid() || !Asset->Flipbooks.IsValidIndex(FlipbookIndex))
	{
		return SNullWidget::NullWidget;
	}

	const FFlipbookHitboxData& FBData = Asset->Flipbooks[FlipbookIndex];
	bool bSelected = (FlipbookIndex == SelectedFlipbookIndex);
	bool bMultiSelected = SelectedFlipbookCards.Contains(FlipbookIndex);

	// Load flipbook for animated thumbnail
	UPaperFlipbook* LoadedFlipbook = !FBData.Flipbook.IsNull() ? FBData.Flipbook.LoadSynchronous() : nullptr;

	FLinearColor CardBG = (bMultiSelected || bSelected)
		? FLinearColor(0.18f, 0.30f, 0.50f, 1.0f)
		: FLinearColor(0.22f, 0.22f, 0.24f, 1.0f);

	static FSlateRoundedBoxBrush CardBrush(FLinearColor::White, 8.0f);

	TSharedRef<SWidget> CardContent = SNew(SBox)
		.WidthOverride(140.f)
		.HeightOverride(120.f)
		[
			SNew(SBorder)
			.BorderImage(&CardBrush)
			.BorderBackgroundColor(CardBG)
			.Padding(4)
			[
				SNew(SVerticalBox)

				// Thumbnail (clickable to change flipbook, animates on hover)
				+ SVerticalBox::Slot()
				.AutoHeight()
				.HAlign(HAlign_Center)
				.Padding(0, 4, 0, 4)
				[
					SNew(SBox)
					.WidthOverride(64)
					.HeightOverride(64)
					[
						SNew(SButton)
						.ButtonStyle(FAppStyle::Get(), "NoBorder")
						.ToolTipText(LOCTEXT("ClickToChangeFlipbookCard", "Click to change flipbook"))
						.OnClicked_Lambda([this, FlipbookIndex]()
						{
							OpenFlipbookPicker(FlipbookIndex);
							return FReply::Handled();
						})
						[
							LoadedFlipbook
								? StaticCastSharedRef<SWidget>(SNew(SFlipbookThumbnail).Flipbook(LoadedFlipbook))
								: StaticCastSharedRef<SWidget>(SNew(SBorder)
									.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
									.HAlign(HAlign_Center)
									.VAlign(VAlign_Center)
									[
										SNew(STextBlock)
										.Text(LOCTEXT("NoFlipbook", "?"))
										.Font(FCoreStyle::GetDefaultFontStyle("Bold", 14))
										.ColorAndOpacity(FSlateColor(FLinearColor(0.4f, 0.4f, 0.4f)))
									])
						]
					]
				]

				// Name
				+ SVerticalBox::Slot()
				.AutoHeight()
				.HAlign(HAlign_Center)
				.Padding(2, 0)
				[
					SNew(STextBlock)
					.Text(FText::FromString(FBData.FlipbookName))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
					.Justification(ETextJustify::Center)
					.AutoWrapText(true)
				]

				// Frame count
				+ SVerticalBox::Slot()
				.AutoHeight()
				.HAlign(HAlign_Center)
				[
					SNew(STextBlock)
					.Text_Lambda([this, FlipbookIndex]()
					{
						if (Asset.IsValid() && Asset->Flipbooks.IsValidIndex(FlipbookIndex))
						{
							return FText::Format(LOCTEXT("FrameCountBadge", "{0} frames"),
								FText::AsNumber(Asset->Flipbooks[FlipbookIndex].Frames.Num()));
						}
						return FText::GetEmpty();
					})
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 7))
					.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)))
				]
			]
		];

	// Wrap in drag wrapper
	TSharedRef<SFlipbookCardDragWrapper> Wrapper = SNew(SFlipbookCardDragWrapper)
		[CardContent];

	Wrapper->FlipbookIndex = FlipbookIndex;
	Wrapper->OnClickedFunc = [this, FlipbookIndex](const FPointerEvent& MouseEvent)
	{
		OnFlipbookGroupCardClicked(FlipbookIndex, MouseEvent);
	};
	Wrapper->OnRightClickFunc = [this, FlipbookIndex](const FPointerEvent& MouseEvent)
	{
		if (!SelectedFlipbookCards.Contains(FlipbookIndex))
		{
			SelectedFlipbookCards.Empty();
			SelectedFlipbookCards.Add(FlipbookIndex);
			SelectionAnchorIndex = FlipbookIndex;
		}
		SelectedFlipbookIndex = FlipbookIndex;
		RefreshFlipbookGroupsPanel();
		ShowFlipbookContextMenu(FlipbookIndex);
	};
	Wrapper->GetDragIndicesFunc = [this, FlipbookIndex]() -> TArray<int32>
	{
		if (SelectedFlipbookCards.Contains(FlipbookIndex) && SelectedFlipbookCards.Num() > 1)
		{
			return SelectedFlipbookCards.Array();
		}
		return { FlipbookIndex };
	};
	Wrapper->GetGroupFunc = [this, FlipbookIndex]() -> FName
	{
		if (Asset.IsValid() && Asset->Flipbooks.IsValidIndex(FlipbookIndex))
		{
			return Asset->Flipbooks[FlipbookIndex].FlipbookGroup;
		}
		return NAME_None;
	};

	return Wrapper;
}

// ==========================================
// SELECTION
// ==========================================

void SCharacterDataAssetEditor::OnFlipbookGroupCardClicked(int32 FlipbookIndex, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.IsControlDown())
	{
		// Ctrl+click: toggle selection
		if (SelectedFlipbookCards.Contains(FlipbookIndex))
		{
			SelectedFlipbookCards.Remove(FlipbookIndex);
		}
		else
		{
			SelectedFlipbookCards.Add(FlipbookIndex);
		}
		SelectionAnchorIndex = FlipbookIndex;
	}
	else if (MouseEvent.IsShiftDown() && SelectionAnchorIndex != INDEX_NONE)
	{
		// Shift+click: range select within same group only
		if (!Asset.IsValid()) return;

		FName AnchorGroup = Asset->Flipbooks.IsValidIndex(SelectionAnchorIndex)
			? Asset->Flipbooks[SelectionAnchorIndex].FlipbookGroup : NAME_None;
		FName ClickedGroup = Asset->Flipbooks.IsValidIndex(FlipbookIndex)
			? Asset->Flipbooks[FlipbookIndex].FlipbookGroup : NAME_None;

		if (AnchorGroup != ClickedGroup)
		{
			// Cross-group: clear to single-select
			SelectedFlipbookCards.Empty();
			SelectedFlipbookCards.Add(FlipbookIndex);
			SelectionAnchorIndex = FlipbookIndex;
		}
		else
		{
			// Same group range select
			TArray<int32> GroupFlipbooks = Asset->GetFlipbookIndicesForFlipbookGroup(AnchorGroup);
			int32 AnchorPos = GroupFlipbooks.Find(SelectionAnchorIndex);
			int32 ClickPos = GroupFlipbooks.Find(FlipbookIndex);
			if (AnchorPos != INDEX_NONE && ClickPos != INDEX_NONE)
			{
				int32 Start = FMath::Min(AnchorPos, ClickPos);
				int32 End = FMath::Max(AnchorPos, ClickPos);
				for (int32 i = Start; i <= End; ++i)
				{
					SelectedFlipbookCards.Add(GroupFlipbooks[i]);
				}
			}
		}
	}
	else
	{
		// Normal click: single select
		SelectedFlipbookCards.Empty();
		SelectedFlipbookCards.Add(FlipbookIndex);
		SelectionAnchorIndex = FlipbookIndex;
	}

	SelectedFlipbookIndex = FlipbookIndex;
	RefreshFlipbookGroupsPanel();
}

bool SCharacterDataAssetEditor::PassesFlipbookGroupSearch(const FFlipbookHitboxData& FlipbookData) const
{
	const FString Query = FlipbookGroupSearchText.TrimStartAndEnd();
	if (Query.IsEmpty()) return true;
	return FlipbookData.FlipbookName.Contains(Query, ESearchCase::IgnoreCase);
}

// ==========================================
// GROUP MANAGEMENT
// ==========================================

void SCharacterDataAssetEditor::CreateFlipbookGroup(FName ParentGroup)
{
	if (!Asset.IsValid()) return;

	// Generate unique name
	FString BaseName = TEXT("NewGroup");
	FName NewName(*BaseName);
	int32 Counter = 1;
	while (Asset->HasFlipbookGroup(NewName))
	{
		NewName = FName(*FString::Printf(TEXT("%s%d"), *BaseName, Counter++));
	}

	BeginTransaction(LOCTEXT("CreateGroup", "Create Flipbook Group"));
	Asset->AddFlipbookGroup(NewName, ParentGroup);
	EndTransaction();

	PendingRenameFlipbookGroup = NewName;
	RefreshFlipbookGroupsPanel();
}

void SCharacterDataAssetEditor::DeleteFlipbookGroup(FName GroupName)
{
	if (!Asset.IsValid()) return;

	// Check if group has sub-groups
	bool bHasSubGroups = false;
	for (const FFlipbookGroupInfo& Group : Asset->FlipbookGroups)
	{
		if (Group.ParentGroup == GroupName)
		{
			bHasSubGroups = true;
			break;
		}
	}

	if (bHasSubGroups)
	{
		EAppReturnType::Type Result = FMessageDialog::Open(EAppMsgType::OkCancel,
			LOCTEXT("DeleteGroupConfirm",
				"Sub-groups will be promoted to the parent level. Flipbooks will move to Ungrouped.\n\nContinue?"));
		if (Result != EAppReturnType::Ok) return;
	}

	BeginTransaction(LOCTEXT("DeleteGroup", "Delete Flipbook Group"));
	Asset->RemoveFlipbookGroup(GroupName);
	EndTransaction();

	RefreshFlipbookGroupsPanel();
}

void SCharacterDataAssetEditor::ShowFlipbookGroupContextMenu(FName GroupName, const FVector2D& CursorPos)
{
	FMenuBuilder MenuBuilder(true, nullptr);

	MenuBuilder.AddMenuEntry(
		LOCTEXT("RenameGroup", "Rename"),
		LOCTEXT("RenameGroupTooltip", "Rename this group"),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateSP(this, &SCharacterDataAssetEditor::TriggerFlipbookGroupRename, GroupName))
	);

	// Color submenu
	MenuBuilder.AddSubMenu(
		LOCTEXT("ChangeColor", "Change Color"),
		LOCTEXT("ChangeColorTooltip", "Change the group header color"),
		FNewMenuDelegate::CreateLambda([this, GroupName](FMenuBuilder& SubMenu)
		{
			static const TPair<FText, FLinearColor> Palette[] = {
				{LOCTEXT("Red", "Red"),         FLinearColor(0.8f, 0.2f, 0.2f)},
				{LOCTEXT("Orange", "Orange"),   FLinearColor(0.9f, 0.5f, 0.1f)},
				{LOCTEXT("Yellow", "Yellow"),   FLinearColor(0.9f, 0.8f, 0.2f)},
				{LOCTEXT("Green", "Green"),     FLinearColor(0.2f, 0.7f, 0.3f)},
				{LOCTEXT("Teal", "Teal"),       FLinearColor(0.2f, 0.7f, 0.7f)},
				{LOCTEXT("Blue", "Blue"),       FLinearColor(0.3f, 0.5f, 0.8f)},
				{LOCTEXT("Indigo", "Indigo"),   FLinearColor(0.4f, 0.3f, 0.8f)},
				{LOCTEXT("Purple", "Purple"),   FLinearColor(0.6f, 0.3f, 0.7f)},
				{LOCTEXT("Pink", "Pink"),       FLinearColor(0.8f, 0.3f, 0.5f)},
				{LOCTEXT("Gray", "Gray"),       FLinearColor(0.5f, 0.5f, 0.5f)},
			};

			for (const auto& Color : Palette)
			{
				SubMenu.AddMenuEntry(
					Color.Key,
					FText::GetEmpty(),
					FSlateIcon(),
					FUIAction(FExecuteAction::CreateSP(this, &SCharacterDataAssetEditor::OnFlipbookGroupColorCommitted, Color.Value, GroupName))
				);
			}

			SubMenu.AddMenuSeparator();
			SubMenu.AddMenuEntry(
				LOCTEXT("CustomColor", "Custom..."),
				LOCTEXT("CustomColorTooltip", "Open color picker"),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateLambda([this, GroupName]()
				{
					FLinearColor CurrentColor(0.3f, 0.5f, 0.8f);
					for (const FFlipbookGroupInfo& Group : Asset->FlipbookGroups)
					{
						if (Group.GroupName == GroupName)
						{
							CurrentColor = Group.Color;
							break;
						}
					}
					OnOpenFlipbookGroupColorPicker(GroupName, CurrentColor);
				}))
			);
		})
	);

	MenuBuilder.AddMenuEntry(
		LOCTEXT("AddSubGroup", "Add Sub-group"),
		LOCTEXT("AddSubGroupTooltip", "Create a new group nested under this one"),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([this, GroupName]() { CreateFlipbookGroup(GroupName); }))
	);

	MenuBuilder.AddMenuSeparator();

	MenuBuilder.AddMenuEntry(
		LOCTEXT("DeleteGroupMenu", "Delete Group"),
		LOCTEXT("DeleteGroupMenuTooltip", "Delete this group. Flipbooks move to Ungrouped, sub-groups are promoted."),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([this, GroupName]() { DeleteFlipbookGroup(GroupName); }))
	);

	FSlateApplication::Get().PushMenu(
		SharedThis(this),
		FWidgetPath(),
		MenuBuilder.MakeWidget(),
		CursorPos,
		FPopupTransitionEffect::ContextMenu
	);
}

// Need a helper method for rename trigger
void SCharacterDataAssetEditor::TriggerFlipbookGroupRename(FName GroupName)
{
	PendingRenameFlipbookGroup = GroupName;
	RefreshFlipbookGroupsPanel();
}

// ==========================================
// RENAME VALIDATION
// ==========================================

bool SCharacterDataAssetEditor::OnVerifyFlipbookGroupNameChanged(const FText& InText, FText& OutErrorMessage, FName CurrentGroupName)
{
	FName NewName(*InText.ToString());
	if (NewName == NAME_None)
	{
		OutErrorMessage = LOCTEXT("EmptyGroupName", "Group name cannot be empty");
		return false;
	}
	if (Asset.IsValid() && Asset->HasFlipbookGroup(NewName) && NewName != CurrentGroupName)
	{
		OutErrorMessage = LOCTEXT("DuplicateGroupName", "A group with this name already exists");
		return false;
	}
	return true;
}

void SCharacterDataAssetEditor::OnFlipbookGroupNameCommitted(const FText& InText, ETextCommit::Type CommitType, FName OriginalGroupName)
{
	if (CommitType == ETextCommit::OnEnter)
	{
		FName NewName(*InText.ToString());
		if (NewName != NAME_None && NewName != OriginalGroupName && Asset.IsValid())
		{
			BeginTransaction(LOCTEXT("RenameGroup", "Rename Flipbook Group"));
			Asset->RenameFlipbookGroup(OriginalGroupName, NewName);
			EndTransaction();
			RefreshFlipbookGroupsPanel();
		}
	}
	// OnUserMovedFocus and OnCleared: revert (don't auto-commit)
}

// ==========================================
// COLOR PICKER
// ==========================================

void SCharacterDataAssetEditor::OnOpenFlipbookGroupColorPicker(FName GroupName, FLinearColor CurrentColor)
{
	FColorPickerArgs PickerArgs;
	PickerArgs.bIsModal = true;
	PickerArgs.ParentWidget = SharedThis(this);
	PickerArgs.InitialColor = CurrentColor;
	PickerArgs.OnColorCommitted = FOnLinearColorValueChanged::CreateSP(
		this, &SCharacterDataAssetEditor::OnFlipbookGroupColorCommitted, GroupName);
	OpenColorPicker(PickerArgs);
}

void SCharacterDataAssetEditor::OnFlipbookGroupColorCommitted(FLinearColor NewColor, FName GroupName)
{
	if (!Asset.IsValid()) return;
	BeginTransaction(LOCTEXT("ChangeGroupColor", "Change Group Color"));
	Asset->SetFlipbookGroupColor(GroupName, NewColor);
	EndTransaction();
	RefreshFlipbookGroupsPanel();
}

// ==========================================
// AUTO-GROUP BY PREFIX
// ==========================================

void SCharacterDataAssetEditor::AutoGroupByPrefix()
{
	if (!Asset.IsValid()) return;

	// Only operate on ungrouped flipbooks
	TMap<FString, TArray<int32>> PrefixToFlipbooks;

	for (int32 i = 0; i < Asset->Flipbooks.Num(); ++i)
	{
		if (Asset->Flipbooks[i].FlipbookGroup != NAME_None) continue;

		const FString& Name = Asset->Flipbooks[i].FlipbookName;

		// Phase 1: Underscore-first
		int32 UnderscoreIdx = Name.Find(TEXT("_"));
		if (UnderscoreIdx != INDEX_NONE)
		{
			FString Prefix = Name.Left(UnderscoreIdx);
			if (Prefix.Len() >= 2)
			{
				PrefixToFlipbooks.FindOrAdd(Prefix).Add(i);
				continue;
			}
		}

		// Phase 2: CamelCase fallback
		int32 LastBoundary = 0;
		for (int32 CharIdx = 1; CharIdx < Name.Len(); ++CharIdx)
		{
			TCHAR Current = Name[CharIdx];
			TCHAR Previous = Name[CharIdx - 1];

			bool bBoundary = false;
			if (FChar::IsLower(Previous) && FChar::IsUpper(Current)) bBoundary = true;
			if (FChar::IsAlpha(Previous) && FChar::IsDigit(Current)) bBoundary = true;
			if (FChar::IsDigit(Previous) && FChar::IsAlpha(Current)) bBoundary = true;

			if (bBoundary)
			{
				LastBoundary = CharIdx;
			}
		}

		if (LastBoundary > 1)
		{
			FString Prefix = Name.Left(LastBoundary);
			PrefixToFlipbooks.FindOrAdd(Prefix).Add(i);
		}
	}

	// Filter: only keep prefixes with 2+ matches
	int32 GroupsCreated = 0;
	int32 FlipbooksGrouped = 0;
	int32 FlipbooksAddedToExisting = 0;

	BeginTransaction(LOCTEXT("AutoGroupByPrefix", "Auto-group by Prefix"));

	for (const auto& Pair : PrefixToFlipbooks)
	{
		if (Pair.Value.Num() < 2) continue;

		FName GroupName(*Pair.Key);

		// Conflict resolution: if group already exists, add to it
		if (Asset->HasFlipbookGroup(GroupName))
		{
			for (int32 FlipbookIdx : Pair.Value)
			{
				Asset->MoveFlipbookToFlipbookGroup(FlipbookIdx, GroupName);
				FlipbooksAddedToExisting++;
			}
		}
		else
		{
			Asset->AddFlipbookGroup(GroupName);
			for (int32 FlipbookIdx : Pair.Value)
			{
				Asset->MoveFlipbookToFlipbookGroup(FlipbookIdx, GroupName);
			}
			GroupsCreated++;
			FlipbooksGrouped += Pair.Value.Num();
		}
	}

	EndTransaction();

	// Notification
	FString Message;
	if (GroupsCreated > 0 || FlipbooksAddedToExisting > 0)
	{
		if (GroupsCreated > 0)
		{
			Message += FString::Printf(TEXT("Created %d groups for %d flipbooks."), GroupsCreated, FlipbooksGrouped);
		}
		if (FlipbooksAddedToExisting > 0)
		{
			if (!Message.IsEmpty()) Message += TEXT(" ");
			Message += FString::Printf(TEXT("Added %d flipbooks to existing groups."), FlipbooksAddedToExisting);
		}
	}
	else
	{
		Message = TEXT("No prefixes found with 2+ matching ungrouped flipbooks.");
	}

	FNotificationInfo Info(FText::FromString(Message));
	Info.ExpireDuration = 4.0f;
	FSlateNotificationManager::Get().AddNotification(Info);

	RefreshFlipbookGroupsPanel();
}

// ==========================================
// DROP HANDLER
// ==========================================

void SCharacterDataAssetEditor::OnFlipbookGroupFlipbooksDrop(const TArray<int32>& FlipbookIndices, FName TargetGroup)
{
	if (!Asset.IsValid() || FlipbookIndices.Num() == 0) return;

	// Validate all indices and check if any actually need moving (no-op check)
	bool bAnyNeedsMove = false;
	for (int32 Idx : FlipbookIndices)
	{
		if (!Asset->Flipbooks.IsValidIndex(Idx)) return; // Invalid index — abort entire operation
		if (Asset->Flipbooks[Idx].FlipbookGroup != TargetGroup)
		{
			bAnyNeedsMove = true;
		}
	}

	if (!bAnyNeedsMove) return; // All already in target group

	BeginTransaction(LOCTEXT("MoveToGroup", "Move Flipbooks to Group"));
	for (int32 Idx : FlipbookIndices)
	{
		Asset->MoveFlipbookToFlipbookGroup(Idx, TargetGroup);
	}
	EndTransaction();

	// Expand the target group to show dropped cards
	CollapsedFlipbookGroups.Remove(TargetGroup == NAME_None ? FName("__Ungrouped") : TargetGroup);

	RefreshFlipbookGroupsPanel();
}

#undef LOCTEXT_NAMESPACE
