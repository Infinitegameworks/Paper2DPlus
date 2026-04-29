// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "CharacterProfileAssetEditor.h"
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
#include "Widgets/Colors/SColorBlock.h"
#include "SDragClickWrapper.h"
#include "SFlipbookGroupWidgets.h"

/** Flipbook Groups tab — Visual tree for organizing flipbooks into named groups with drag-drop, collapse, and phase slot assignment. */

#define LOCTEXT_NAMESPACE "CharacterProfileAssetEditor"

namespace
{
static TMap<FString, int32> BuildFlipbookNameUsageCounts(const UPaper2DPlusCharacterProfileAsset* Asset)
{
	TMap<FString, int32> UsageCounts;
	if (!Asset)
	{
		return UsageCounts;
	}

	for (const FFlipbookProfileEntry& Entry : Asset->Flipbooks)
	{
		const FString Key = Entry.Identity.FlipbookName.TrimStartAndEnd();
		if (!Key.IsEmpty())
		{
			UsageCounts.FindOrAdd(Key)++;
		}
	}
	return UsageCounts;
}

static int32 GetFlipbookValidationIssueCount(const FFlipbookProfileEntry& Data, const TMap<FString, int32>& NameUsageCounts, FString* OutTooltip = nullptr)
{
	TArray<FString> Issues;

	const UPaperFlipbook* LoadedFlipbook = Data.Identity.Flipbook.IsNull() ? nullptr : Data.Identity.Flipbook.LoadSynchronous();

	if (!LoadedFlipbook)
	{
		Issues.Add(TEXT("Missing flipbook asset"));
	}

	int32 FrameCount = Data.CombatData.Frames.Num();
	if (FrameCount <= 0)
	{
		if (LoadedFlipbook)
		{
			FrameCount = LoadedFlipbook->GetNumKeyFrames();
		}
		if (FrameCount <= 0)
		{
			Issues.Add(TEXT("Zero frames"));
		}
	}

	const FString NameKey = Data.Identity.FlipbookName.TrimStartAndEnd();
	const int32* NameCount = NameUsageCounts.Find(NameKey);
	if (!NameKey.IsEmpty() && NameCount && *NameCount > 1)
	{
		Issues.Add(TEXT("Duplicate flipbook name"));
	}

	if (OutTooltip)
	{
		*OutTooltip = FString::Join(Issues, TEXT("\n"));
	}

	return Issues.Num();
}
}

// ==========================================
// DRAG-DROP OPERATIONS
// ==========================================

// FGroupDragDropOp — for dragging entire groups (class declared in CharacterProfileAssetEditor.h)

TSharedRef<FGroupDragDropOp> FGroupDragDropOp::New(FName GroupName)
{
	TSharedRef<FGroupDragDropOp> Op = MakeShareable(new FGroupDragDropOp());
	Op->SourceGroupName = GroupName;
	Op->DefaultHoverText = FText::Format(LOCTEXT("DragGroup", "Move group: {0}"), FText::FromName(GroupName));
	Op->Construct();
	return Op;
}

TSharedPtr<SWidget> FGroupDragDropOp::GetDefaultDecorator() const
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
		.Padding(FMargin(6, 2))
		[
			SNew(STextBlock).Text(DefaultHoverText)
		];
}

// FFlipbookGroupDragDropOp factory methods (class declared in CharacterProfileAssetEditor.h)

TSharedRef<FFlipbookGroupDragDropOp> FFlipbookGroupDragDropOp::NewFromCardDrag(const TArray<int32>& InFlipbookIndices, FName FromGroup)
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

TSharedPtr<SWidget> FFlipbookGroupDragDropOp::GetDefaultDecorator() const
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
		.Padding(FMargin(6, 2))
		[
			SNew(STextBlock).Text(DefaultHoverText)
		];
}

// SFlipbookCardDragWrapper replaced by shared SDragClickWrapper (see SDragClickWrapper.h)

// ==========================================
// BUILD & REFRESH
// ==========================================

TSharedRef<SWidget> SCharacterProfileAssetEditor::BuildFlipbookGroupsPanel()
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
			.AutoWidth()
			.Padding(0, 0, 4, 0)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
				.Text(LOCTEXT("AddPhaseGroup", "+ Phase Group"))
				.ToolTipText(LOCTEXT("AddPhaseGroupTooltip", "Create a new phase group (Startup/Active/Recovery)"))
				.OnClicked_Lambda([this]()
				{
					if (!Asset.IsValid()) return FReply::Handled();
					FString BaseName = TEXT("New Phase Group");
					FString NewName = BaseName;
					int32 Counter = 1;
					while (Asset->FindPhaseGroup(NewName) != nullptr)
					{
						NewName = FString::Printf(TEXT("%s %d"), *BaseName, Counter++);
					}
					BeginTransaction(LOCTEXT("CreatePhaseGroupOverview", "Create Phase Group"));
					FPhaseGroup NewGroup;
					NewGroup.GroupName = NewName;
					Asset->PhaseGroups.Add(MoveTemp(NewGroup));
					// Also create a matching FFlipbookGroupInfo so it renders through the normal group tree
					FFlipbookGroupInfo GroupInfo;
					GroupInfo.GroupName = FName(*NewName);
					GroupInfo.bIsPhaseGroup = true;
					Asset->FlipbookGroups.Add(GroupInfo);
					EndTransaction();
					PendingRenameFlipbookGroup = FName(*NewName);
					RefreshFlipbookGroupsPanel();
					MarkTabDirty(4); // Phase editor uses groups — deferred refresh
					return FReply::Handled();
				})
			]

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SNullWidget::NullWidget
			]

			// List | Grid segmented toggle
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
				.Padding(FMargin(1))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(SButton)
						.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
						.ButtonColorAndOpacity_Lambda([this]()
						{
							return bFlipbookGroupGridView
								? FLinearColor(0.05f, 0.05f, 0.05f, 0.5f)
								: FLinearColor(0.15f, 0.35f, 0.55f, 1.0f);
						})
						.OnClicked_Lambda([this]()
						{
							if (bFlipbookGroupGridView) { bFlipbookGroupGridView = false; RefreshFlipbookGroupsPanel(); }
							return FReply::Handled();
						})
						[
							SNew(STextBlock)
							.Text(LOCTEXT("ListToggle", "List"))
							.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
							.Margin(FMargin(6, 1))
						]
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(SButton)
						.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
						.ButtonColorAndOpacity_Lambda([this]()
						{
							return bFlipbookGroupGridView
								? FLinearColor(0.15f, 0.35f, 0.55f, 1.0f)
								: FLinearColor(0.05f, 0.05f, 0.05f, 0.5f);
						})
						.OnClicked_Lambda([this]()
						{
							if (!bFlipbookGroupGridView) { bFlipbookGroupGridView = true; RefreshFlipbookGroupsPanel(); }
							return FReply::Handled();
						})
						[
							SNew(STextBlock)
							.Text(LOCTEXT("GridToggle", "Grid"))
							.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
							.Margin(FMargin(6, 1))
						]
					]
				]
			]
		]

		// Search + Filter
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0, 0, 0, 4)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
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

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(4, 0, 0, 0)
			[
				BuildCompletionFilterButton([this]() { RefreshFlipbookGroupsPanel(); })
			]
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

void SCharacterProfileAssetEditor::RefreshFlipbookGroupsPanel()
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
	FlipbookGroupFlipbookNameTexts.Empty();

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
	const TMap<FString, int32> FlipbookNameUsageCounts = BuildFlipbookNameUsageCounts(Asset.Get());

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
		BuildGroupSection(nullptr, NAME_None, 0, Tree, FlipbooksByGroup, FlipbookNameUsageCounts)
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
				BuildGroupSection(GroupInfo, GroupInfo->GroupName, 0, Tree, FlipbooksByGroup, FlipbookNameUsageCounts)
			];
		}
	}

	// Phase groups now render through BuildGroupSection via bIsPhaseGroup on FFlipbookGroupInfo.
	// The "+ Phase Group" toolbar button creates both FPhaseGroup and FFlipbookGroupInfo entries.

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

	// Deferred flipbook rename entry for overview group cards/rows
	TriggerPendingRenameIfNeeded(FlipbookGroupFlipbookNameTexts);
}

// ==========================================
// GROUP SECTION RENDERING
// ==========================================

TSharedRef<SWidget> SCharacterProfileAssetEditor::BuildGroupSection(
	const FFlipbookGroupInfo* GroupInfo,
	FName GroupName,
	int32 NestLevel,
	const TMap<FName, TArray<const FFlipbookGroupInfo*>>& Tree,
	const TMap<FName, TArray<int32>>& FlipbooksByGroup,
	const TMap<FString, int32>& FlipbookNameUsageCounts)
{
	// Get flipbooks for this group
	const TArray<int32>* FlipbookIndices = FlipbooksByGroup.Find(GroupName);
	int32 FlipbookCount = FlipbookIndices ? FlipbookIndices->Num() : 0;
	int32 GroupIssueCount = 0;
	if (FlipbookIndices && Asset.IsValid())
	{
		for (int32 FlipbookIdx : *FlipbookIndices)
		{
			if (Asset->Flipbooks.IsValidIndex(FlipbookIdx)
				&& GetFlipbookValidationIssueCount(Asset->Flipbooks[FlipbookIdx], FlipbookNameUsageCounts) > 0)
			{
				++GroupIssueCount;
			}
		}
	}

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

	// Check if any descendants have matching flipbooks (for search/filter visibility)
	const bool bHasActiveFilter = !FlipbookGroupSearchText.IsEmpty() || CompletionFilterMask != 0;
	bool bHasMatchingChildren = FilteredIndices.Num() > 0;
	if (!bHasMatchingChildren && bHasActiveFilter)
	{
		// Recursive check through all descendant groups
		TArray<FName> GroupsToCheck;
		GroupsToCheck.Add(GroupName);
		while (GroupsToCheck.Num() > 0 && !bHasMatchingChildren)
		{
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 5)
			FName CheckGroup = GroupsToCheck.Pop(EAllowShrinking::No);
#else
			FName CheckGroup = GroupsToCheck.Pop(false);
#endif
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

		// Also check if group name matches text search (not applicable for filter-only)
		if (!bHasMatchingChildren && !FlipbookGroupSearchText.IsEmpty() && GroupName != NAME_None)
		{
			bHasMatchingChildren = GroupName.ToString().Contains(FlipbookGroupSearchText, ESearchCase::IgnoreCase);
		}
	}

	// Hide groups with no matches during search or filter
	if (bHasActiveFilter && !bHasMatchingChildren)
	{
		return SNullWidget::NullWidget;
	}

	// Build the body content (flipbook cards + child groups)
	TSharedRef<SVerticalBox> BodyContent = SNew(SVerticalBox);

	// Phase group rendering — show S/A/R slot cards instead of regular flipbook cards
	if (GroupInfo && GroupInfo->bIsPhaseGroup && Asset.IsValid())
	{
		const FPhaseGroup* PhaseGroup = Asset->FindPhaseGroup(GroupName.ToString());
		if (PhaseGroup)
		{
			static FSlateRoundedBoxBrush PhaseCardBrush(FLinearColor::White, 8.0f);
			TSharedRef<SHorizontalBox> SlotsRow = SNew(SHorizontalBox);
			FString CapturedGroupNameStr = GroupName.ToString();

			for (auto& [Phase, Label, Color, PhaseTip] : TArray<TTuple<EAnimationPhase, FText, FLinearColor, FText>>{
				{ EAnimationPhase::Startup, LOCTEXT("PhaseS_GS", "Startup"), FLinearColor(0.85f, 0.65f, 0.15f),
					LOCTEXT("PhaseSTip", "Startup phase — the wind-up frames before the attack becomes active. Cancel windows and anticipation frames go here.") },
				{ EAnimationPhase::Active, LOCTEXT("PhaseA_GS", "Active"), FLinearColor(0.85f, 0.25f, 0.25f),
					LOCTEXT("PhaseATip", "Active phase — the frames where hitboxes are live and damage can be dealt. The core of the attack.") },
				{ EAnimationPhase::Recovery, LOCTEXT("PhaseR_GS", "Recovery"), FLinearColor(0.35f, 0.55f, 0.85f),
					LOCTEXT("PhaseRTip", "Recovery phase — the wind-down frames after the active phase. The character is vulnerable during recovery.") }
			})
			{
				const FString& AssignedName = PhaseGroup->GetFlipbookForPhase(Phase);
				const bool bHasFlipbook = !AssignedName.IsEmpty();
				EAnimationPhase CapturedPhase = Phase;

				UPaperFlipbook* SlotFlipbook = nullptr;
				int32 SlotFlipbookIndex = INDEX_NONE;
				if (bHasFlipbook)
				{
					const FFlipbookProfileEntry* FBData = Asset->FindFlipbookDataPtr(AssignedName);
					if (FBData) SlotFlipbook = FBData->Identity.Flipbook.LoadSynchronous();
					for (int32 fi = 0; fi < Asset->Flipbooks.Num(); fi++)
					{
						if (Asset->Flipbooks[fi].Identity.FlipbookName == AssignedName)
						{
							SlotFlipbookIndex = fi;
							break;
						}
					}
				}

				// Build filled card content with drag wrapper
				TSharedPtr<SWidget> FilledCardWidget;
				if (bHasFlipbook)
				{
					// Check if this card passes the active completion filter (incomplete for filtered task)
					bool bPassesFilter = true;
					if (CompletionFilterMask != 0 && SlotFlipbookIndex != INDEX_NONE)
					{
						bPassesFilter = PassesFlipbookGroupSearch(Asset->Flipbooks[SlotFlipbookIndex]);
					}

					TSharedRef<SDragClickWrapper> DragWrapper = SNew(SDragClickWrapper)
					[
						SNew(SOverlay)

						+ SOverlay::Slot()
						[
							SNew(SVerticalBox)

							// Thumbnail
							+ SVerticalBox::Slot()
							.AutoHeight()
							.HAlign(HAlign_Center)
							.Padding(0, 4, 0, 2)
							[
								SNew(SBox)
								.WidthOverride(64)
								.HeightOverride(64)
								[
									SNew(SFlipbookThumbnail)
									.Flipbook(SlotFlipbook)
								]
							]

							// Flipbook name
							+ SVerticalBox::Slot()
							.AutoHeight()
							.HAlign(HAlign_Center)
							.Padding(2, 0)
							[
								SNew(STextBlock)
								.Text(FText::FromString(AssignedName))
								.Font(FCoreStyle::GetDefaultFontStyle("Regular", 7))
								.ColorAndOpacity(FSlateColor(FLinearColor(0.7f, 0.7f, 0.7f)))
								.Justification(ETextJustify::Center)
							]

							// Phase color bar
							+ SVerticalBox::Slot()
							.AutoHeight()
							.Padding(0, 2, 0, 0)
							[
								SNew(SBox)
								.HeightOverride(3.0f)
								[
									SNew(SColorBlock).Color(Color)
								]
							]
						]

						// Filter completion indicator — dim overlay + checkmark for cards that are already complete
						+ SOverlay::Slot()
						[
							(CompletionFilterMask != 0 && !bPassesFilter)
							? StaticCastSharedRef<SWidget>(
								SNew(SBorder)
								.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
								.BorderBackgroundColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.35f))
								.HAlign(HAlign_Right)
								.VAlign(VAlign_Top)
								.Padding(FMargin(0, 2, 4, 0))
								[
									SNew(STextBlock)
									.Text(LOCTEXT("PhaseSlotDone", "Done"))
									.Font(FCoreStyle::GetDefaultFontStyle("Bold", 7))
									.ColorAndOpacity(FSlateColor(FLinearColor(0.3f, 0.8f, 0.3f)))
								])
							: StaticCastSharedRef<SWidget>(SNullWidget::NullWidget)
						]
					];
					DragWrapper->OnClickedFunc = [this, SlotFlipbookIndex](const FGeometry&, const FPointerEvent& MouseEvent)
					{
						if (SlotFlipbookIndex != INDEX_NONE)
						{
							OnFlipbookGroupCardClicked(SlotFlipbookIndex, MouseEvent);
						}
					};
					DragWrapper->OnRightClickedFunc = [this, SlotFlipbookIndex](const FGeometry&, const FPointerEvent&)
					{
						if (SlotFlipbookIndex != INDEX_NONE)
						{
							SelectedFlipbookIndex = SlotFlipbookIndex;
							SelectedFlipbookCards.Empty();
							SelectedFlipbookCards.Add(SlotFlipbookIndex);
							Invalidate(EInvalidateWidgetReason::Paint);
							ShowFlipbookContextMenu(SlotFlipbookIndex);
						}
					};
					DragWrapper->OnDoubleClickedFunc = [this, SlotFlipbookIndex]()
					{
						if (SlotFlipbookIndex != INDEX_NONE) OpenFlipbookPicker(SlotFlipbookIndex);
					};
					DragWrapper->OnDragDetectedFunc = [SlotFlipbookIndex, CapturedGroupNameStr]() -> TSharedPtr<FDragDropOperation>
					{
						TArray<int32> DragIndices;
						if (SlotFlipbookIndex != INDEX_NONE)
						{
							DragIndices.Add(SlotFlipbookIndex);
						}
						FName FromGroup = FName(*CapturedGroupNameStr);
						return FFlipbookGroupDragDropOp::NewFromCardDrag(DragIndices, FromGroup);
					};
					FilledCardWidget = DragWrapper;
				}

				// Card content
				auto IsPhaseSlotSelected = [this, SlotFlipbookIndex]()
				{
					return SlotFlipbookIndex != INDEX_NONE &&
						(SlotFlipbookIndex == SelectedFlipbookIndex || SelectedFlipbookCards.Contains(SlotFlipbookIndex));
				};

				TSharedRef<SWidget> SlotContent = SNew(SBox)
					.WidthOverride(140.f)
					.HeightOverride(120.f)
					.ToolTipText(PhaseTip)
					[
						SNew(SBorder)
						.BorderImage(&PhaseCardBrush)
						.BorderBackgroundColor_Lambda([IsPhaseSlotSelected]() -> FSlateColor
						{
							return IsPhaseSlotSelected()
								? FLinearColor(0.18f, 0.30f, 0.50f, 1.0f)
								: FLinearColor(0.08f, 0.08f, 0.10f, 1.0f);
						})
						.Padding(4)
						[
							bHasFlipbook
							? FilledCardWidget.ToSharedRef()
							: StaticCastSharedRef<SWidget>(
								SNew(SComboButton)
								.ButtonStyle(FAppStyle::Get(), "NoBorder")
								.ContentPadding(0)
								.ButtonContent()
								[
									SNew(SVerticalBox)

									+ SVerticalBox::Slot()
									.FillHeight(1.0f)
									.HAlign(HAlign_Center)
									.VAlign(VAlign_Center)
									[
										SNew(SVerticalBox)

										+ SVerticalBox::Slot()
										.AutoHeight()
										.HAlign(HAlign_Center)
										[
											SNew(STextBlock)
											.Text(LOCTEXT("PlusSlotGS", "+"))
											.Font(FCoreStyle::GetDefaultFontStyle("Bold", 18))
											.ColorAndOpacity(FSlateColor(Color * 0.5f))
										]

										+ SVerticalBox::Slot()
										.AutoHeight()
										.HAlign(HAlign_Center)
										[
											SNew(STextBlock)
											.Text(Label)
											.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
											.ColorAndOpacity(FSlateColor(Color * 0.4f))
										]
									]

									// Phase color bar at bottom
									+ SVerticalBox::Slot()
									.AutoHeight()
									[
										SNew(SBox)
										.HeightOverride(3.0f)
										[
											SNew(SColorBlock).Color(Color * 0.3f)
										]
									]
								]
								.OnGetMenuContent_Lambda([this, CapturedGroupNameStr, CapturedPhase]() -> TSharedRef<SWidget>
								{
									TSet<FString> AssignedFBs;
									if (Asset.IsValid())
									{
										for (const FPhaseGroup& PG : Asset->PhaseGroups)
										{
											if (!PG.StartupFlipbook.IsEmpty()) AssignedFBs.Add(PG.StartupFlipbook);
											if (!PG.ActiveFlipbook.IsEmpty()) AssignedFBs.Add(PG.ActiveFlipbook);
											if (!PG.RecoveryFlipbook.IsEmpty()) AssignedFBs.Add(PG.RecoveryFlipbook);
										}
									}

									TSharedRef<SVerticalBox> PickerList = SNew(SVerticalBox);
									if (Asset.IsValid())
									{
										bool bAnyAvailable = false;
										for (int32 j = 0; j < Asset->Flipbooks.Num(); j++)
										{
											FString FBName = Asset->Flipbooks[j].Identity.FlipbookName;
											if (AssignedFBs.Contains(FBName)) continue;
											bAnyAvailable = true;

											UPaperFlipbook* FB = Asset->Flipbooks[j].Identity.Flipbook.LoadSynchronous();

											PickerList->AddSlot()
											.AutoHeight()
											[
												SNew(SButton)
												.ButtonStyle(FAppStyle::Get(), "NoBorder")
												.OnClicked_Lambda([this, CapturedGroupNameStr, CapturedPhase, FBName]()
												{
													if (Asset.IsValid())
													{
														BeginTransaction(LOCTEXT("AssignPhaseSlotPickerGS", "Assign Flipbook to Phase Slot"));
														FPhaseGroup* G = Asset->FindPhaseGroupMutable(CapturedGroupNameStr);
														if (G) G->SetFlipbookForPhase(CapturedPhase, FBName);
														// Set FlipbookGroup on the assigned flipbook
														FName GroupFName = FName(*CapturedGroupNameStr);
														for (FFlipbookProfileEntry& Anim : Asset->Flipbooks)
														{
															if (Anim.Identity.FlipbookName == FBName)
															{
																Anim.FlipbookGroup = GroupFName;
																break;
															}
														}
														EndTransaction();
													}
													FSlateApplication::Get().DismissAllMenus();
													RegisterActiveTimer(0.0f, FWidgetActiveTimerDelegate::CreateLambda(
														[this](double, float) -> EActiveTimerReturnType
														{
															RefreshFlipbookGroupsPanel();
															MarkTabDirty(4); // Phase editor uses groups — deferred refresh
															return EActiveTimerReturnType::Stop;
														}));
													return FReply::Handled();
												})
												[
													SNew(SHorizontalBox)

													+ SHorizontalBox::Slot()
													.AutoWidth()
													.VAlign(VAlign_Center)
													.Padding(0, 0, 6, 0)
													[
														SNew(SBox)
														.WidthOverride(28)
														.HeightOverride(28)
														[
															SNew(SFlipbookThumbnail)
															.Flipbook(FB)
														]
													]

													+ SHorizontalBox::Slot()
													.FillWidth(1.0f)
													.VAlign(VAlign_Center)
													[
														SNew(STextBlock)
														.Text(FText::FromString(FBName))
														.Font(FAppStyle::GetFontStyle("SmallFont"))
													]
												]
											];
										}
										if (!bAnyAvailable)
										{
											PickerList->AddSlot()
											.AutoHeight()
											.Padding(8, 4)
											[
												SNew(STextBlock)
												.Text(LOCTEXT("AllAssignedGS", "All flipbooks are assigned"))
												.ColorAndOpacity(FSlateColor(FLinearColor(0.4f, 0.4f, 0.4f)))
											];
										}
									}
									return StaticCastSharedRef<SWidget>(
										SNew(SBox)
										.MinDesiredWidth(200.0f)
										.MaxDesiredHeight(300.0f)
										[
											SNew(SScrollBox)
											+ SScrollBox::Slot()
											[
												PickerList
											]
										]
									);
								})
							)
						]
					];

				// Wrap in drop target
				TSharedRef<SPhaseSlotDropTarget> DropTarget = SNew(SPhaseSlotDropTarget)
				[
					SlotContent
				];

				DropTarget->OnDropFunc = [this, CapturedGroupNameStr, CapturedPhase](const TArray<int32>& FlipbookIndices)
				{
					if (FlipbookIndices.Num() > 0 && Asset.IsValid())
					{
						int32 FBIndex = FlipbookIndices[0];
						if (Asset->Flipbooks.IsValidIndex(FBIndex))
						{
							const FString& FBName = Asset->Flipbooks[FBIndex].Identity.FlipbookName;
							BeginTransaction(LOCTEXT("AssignPhaseSlotDropGS", "Assign Flipbook to Phase Slot"));

							// Clear old phase slot if this flipbook is already assigned elsewhere
							const FPhaseGroup* OldGroup = Asset->FindPhaseGroupForFlipbook(FBName);
							if (OldGroup)
							{
								FPhaseGroup* OldGroupMut = Asset->FindPhaseGroupMutable(OldGroup->GroupName);
								if (OldGroupMut)
								{
									EAnimationPhase OldPhase = OldGroupMut->GetPhaseForFlipbook(FBName);
									if (OldPhase != EAnimationPhase::None)
									{
										OldGroupMut->SetFlipbookForPhase(OldPhase, FString());
										OldGroupMut->SetSequenceForPhase(OldPhase, nullptr);
									}
								}
							}

							FPhaseGroup* G = Asset->FindPhaseGroupMutable(CapturedGroupNameStr);
							if (G)
							{
								// Clear any existing flipbook in the target slot and reset its group
								FString ExistingFB = G->GetFlipbookForPhase(CapturedPhase);
								if (!ExistingFB.IsEmpty() && ExistingFB != FBName)
								{
									for (FFlipbookProfileEntry& Anim : Asset->Flipbooks)
									{
										if (Anim.Identity.FlipbookName == ExistingFB)
										{
											Anim.FlipbookGroup = NAME_None;
											break;
										}
									}
								}
								G->SetFlipbookForPhase(CapturedPhase, FBName);
							}
							// Set FlipbookGroup on the assigned flipbook
							FName GroupFName = FName(*CapturedGroupNameStr);
							for (FFlipbookProfileEntry& Anim : Asset->Flipbooks)
							{
								if (Anim.Identity.FlipbookName == FBName)
								{
									Anim.FlipbookGroup = GroupFName;
									break;
								}
							}
							EndTransaction();
							RegisterActiveTimer(0.0f, FWidgetActiveTimerDelegate::CreateLambda(
								[this](double, float) -> EActiveTimerReturnType
								{
									RefreshFlipbookGroupsPanel();
									MarkTabDirty(4); // Phase editor uses groups — deferred refresh
									return EActiveTimerReturnType::Stop;
								}));
						}
					}
				};

				// No right-click action — drag cards out of phase slots instead

				SlotsRow->AddSlot()
				.AutoWidth()
				.Padding(4, 0)
				[
					DropTarget
				];
			}

			// ──────────────────────────────────────────────────────────────
			// Custom phase slots: after the built-in 3 (Startup/Active/Recovery)
			// render any user-defined extra slots. Each custom slot has its
			// own name + color set from the Details panel, and behaves like a
			// built-in slot for drag-drop purposes. The backing store is
			// PhaseGroup->CustomSlots (queried by slot name at runtime via
			// UPaper2DPlusBlueprintLibrary::GetPhaseGroupCustomFlipbook).
			// ──────────────────────────────────────────────────────────────
			for (int32 CustomIdx = 0; CustomIdx < PhaseGroup->CustomSlots.Num(); ++CustomIdx)
			{
				const FCustomPhaseSlot& CustomSlot = PhaseGroup->CustomSlots[CustomIdx];
				const FText Label = FText::FromString(CustomSlot.SlotName);
				const FLinearColor Color = CustomSlot.Color;
				const FString AssignedName = CustomSlot.FlipbookName;
				const bool bHasFlipbook = !AssignedName.IsEmpty();
				const FString CapturedSlotName = CustomSlot.SlotName;
				const int32 CapturedCustomIdx = CustomIdx;

				UPaperFlipbook* SlotFlipbook = nullptr;
				int32 SlotFlipbookIndex = INDEX_NONE;
				if (bHasFlipbook)
				{
					const FFlipbookProfileEntry* FBData = Asset->FindFlipbookDataPtr(AssignedName);
					if (FBData) SlotFlipbook = FBData->Identity.Flipbook.LoadSynchronous();
					for (int32 fi = 0; fi < Asset->Flipbooks.Num(); fi++)
					{
						if (Asset->Flipbooks[fi].Identity.FlipbookName == AssignedName)
						{
							SlotFlipbookIndex = fi;
							break;
						}
					}
				}

				// Filled card — drag wrapper so the assigned flipbook can be dragged OUT
				TSharedPtr<SWidget> FilledCardWidget;
				if (bHasFlipbook)
				{
					bool bPassesFilter = true;
					if (CompletionFilterMask != 0 && SlotFlipbookIndex != INDEX_NONE)
					{
						bPassesFilter = PassesFlipbookGroupSearch(Asset->Flipbooks[SlotFlipbookIndex]);
					}

					TSharedRef<SDragClickWrapper> DragWrapper = SNew(SDragClickWrapper)
					[
						SNew(SOverlay)
						+ SOverlay::Slot()
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 4, 0, 2)
							[
								SNew(SBox).WidthOverride(64).HeightOverride(64)
								[ SNew(SFlipbookThumbnail).Flipbook(SlotFlipbook) ]
							]
							+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(2, 0)
							[
								SNew(STextBlock)
								.Text(FText::FromString(AssignedName))
								.Font(FCoreStyle::GetDefaultFontStyle("Regular", 7))
								.ColorAndOpacity(FSlateColor(FLinearColor(0.7f, 0.7f, 0.7f)))
								.Justification(ETextJustify::Center)
							]
							+ SVerticalBox::Slot().AutoHeight().Padding(0, 2, 0, 0)
							[
								SNew(SBox).HeightOverride(3.0f)
								[ SNew(SColorBlock).Color(Color) ]
							]
						]
						+ SOverlay::Slot()
						[
							(CompletionFilterMask != 0 && !bPassesFilter)
							? StaticCastSharedRef<SWidget>(
								SNew(SBorder)
								.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
								.BorderBackgroundColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.35f))
								.HAlign(HAlign_Right).VAlign(VAlign_Top)
								.Padding(FMargin(0, 2, 4, 0))
								[
									SNew(STextBlock)
									.Text(LOCTEXT("CustomSlotDone", "Done"))
									.Font(FCoreStyle::GetDefaultFontStyle("Bold", 7))
									.ColorAndOpacity(FSlateColor(FLinearColor(0.3f, 0.8f, 0.3f)))
								])
							: StaticCastSharedRef<SWidget>(SNullWidget::NullWidget)
						]
					];
					DragWrapper->OnClickedFunc = [this, SlotFlipbookIndex](const FGeometry&, const FPointerEvent& MouseEvent)
					{
						if (SlotFlipbookIndex != INDEX_NONE)
						{
							OnFlipbookGroupCardClicked(SlotFlipbookIndex, MouseEvent);
						}
					};
					DragWrapper->OnRightClickedFunc = [this, SlotFlipbookIndex](const FGeometry&, const FPointerEvent&)
					{
						if (SlotFlipbookIndex != INDEX_NONE)
						{
							SelectedFlipbookIndex = SlotFlipbookIndex;
							SelectedFlipbookCards.Empty();
							SelectedFlipbookCards.Add(SlotFlipbookIndex);
							Invalidate(EInvalidateWidgetReason::Paint);
							ShowFlipbookContextMenu(SlotFlipbookIndex);
						}
					};
					DragWrapper->OnDoubleClickedFunc = [this, SlotFlipbookIndex]()
					{
						if (SlotFlipbookIndex != INDEX_NONE) OpenFlipbookPicker(SlotFlipbookIndex);
					};
					DragWrapper->OnDragDetectedFunc = [SlotFlipbookIndex, CapturedGroupNameStr]() -> TSharedPtr<FDragDropOperation>
					{
						TArray<int32> DragIndices;
						if (SlotFlipbookIndex != INDEX_NONE)
						{
							DragIndices.Add(SlotFlipbookIndex);
						}
						FName FromGroup = FName(*CapturedGroupNameStr);
						return FFlipbookGroupDragDropOp::NewFromCardDrag(DragIndices, FromGroup);
					};
					FilledCardWidget = DragWrapper;
				}

				auto IsCustomSlotSelected = [this, SlotFlipbookIndex]()
				{
					return SlotFlipbookIndex != INDEX_NONE &&
						(SlotFlipbookIndex == SelectedFlipbookIndex || SelectedFlipbookCards.Contains(SlotFlipbookIndex));
				};

				TSharedRef<SWidget> SlotContent = SNew(SBox)
					.WidthOverride(140.f)
					.HeightOverride(120.f)
					.ToolTipText(FText::Format(LOCTEXT("CustomSlotTip", "Custom phase slot: {0}. Drag a flipbook card here to assign it."), Label))
					[
						SNew(SBorder)
						.BorderImage(&PhaseCardBrush)
						.BorderBackgroundColor_Lambda([IsCustomSlotSelected]() -> FSlateColor
						{
							return IsCustomSlotSelected()
								? FLinearColor(0.18f, 0.30f, 0.50f, 1.0f)
								: FLinearColor(0.08f, 0.08f, 0.10f, 1.0f);
						})
						.Padding(4)
						[
							bHasFlipbook
							? FilledCardWidget.ToSharedRef()
							: StaticCastSharedRef<SWidget>(
								SNew(SVerticalBox)
								+ SVerticalBox::Slot().FillHeight(1.0f).HAlign(HAlign_Center).VAlign(VAlign_Center)
								[
									SNew(SVerticalBox)
									+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
									[
										SNew(STextBlock)
										.Text(LOCTEXT("PlusCustomSlotGS", "+"))
										.Font(FCoreStyle::GetDefaultFontStyle("Bold", 18))
										.ColorAndOpacity(FSlateColor(Color * 0.5f))
									]
									+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
									[
										SNew(STextBlock)
										.Text(Label)
										.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
										.ColorAndOpacity(FSlateColor(Color * 0.4f))
									]
								]
								+ SVerticalBox::Slot().AutoHeight()
								[
									SNew(SBox).HeightOverride(3.0f)
									[ SNew(SColorBlock).Color(Color * 0.3f) ]
								])
						]
					];

				// Drop target — accepts flipbook drags and assigns to this custom slot
				TSharedRef<SPhaseSlotDropTarget> CustomDropTarget = SNew(SPhaseSlotDropTarget)
				[
					SlotContent
				];
				CustomDropTarget->OnDropFunc = [this, CapturedGroupNameStr, CapturedSlotName](const TArray<int32>& FlipbookIndices)
				{
					if (FlipbookIndices.Num() == 0 || !Asset.IsValid()) return;
					int32 FBIndex = FlipbookIndices[0];
					if (!Asset->Flipbooks.IsValidIndex(FBIndex)) return;

					const FString FBName = Asset->Flipbooks[FBIndex].Identity.FlipbookName;
					BeginTransaction(LOCTEXT("AssignCustomSlotDrop", "Assign Flipbook to Custom Slot"));

					// If this flipbook is already in a built-in slot somewhere, clear that assignment first
					const FPhaseGroup* OldGroup = Asset->FindPhaseGroupForFlipbook(FBName);
					if (OldGroup)
					{
						FPhaseGroup* OldGroupMut = Asset->FindPhaseGroupMutable(OldGroup->GroupName);
						if (OldGroupMut)
						{
							EAnimationPhase OldPhase = OldGroupMut->GetPhaseForFlipbook(FBName);
							if (OldPhase != EAnimationPhase::None)
							{
								OldGroupMut->SetFlipbookForPhase(OldPhase, FString());
								OldGroupMut->SetSequenceForPhase(OldPhase, nullptr);
							}
						}
					}
					// Clear any custom-slot assignment (same or different group)
					for (FPhaseGroup& PG : Asset->PhaseGroups)
					{
						for (FCustomPhaseSlot& CS : PG.CustomSlots)
						{
							if (CS.FlipbookName == FBName)
							{
								CS.FlipbookName.Empty();
								CS.Sequence = nullptr;
							}
						}
					}

					FPhaseGroup* G = Asset->FindPhaseGroupMutable(CapturedGroupNameStr);
					if (G)
					{
						FCustomPhaseSlot* Slot = G->FindCustomSlotMutable(CapturedSlotName);
						if (Slot)
						{
							// Clear any existing flipbook previously in this slot
							if (!Slot->FlipbookName.IsEmpty() && Slot->FlipbookName != FBName)
							{
								for (FFlipbookProfileEntry& Anim : Asset->Flipbooks)
								{
									if (Anim.Identity.FlipbookName == Slot->FlipbookName)
									{
										Anim.FlipbookGroup = NAME_None;
										break;
									}
								}
							}
							Slot->FlipbookName = FBName;
						}
					}
					// Mark the flipbook as belonging to this phase group
					FName GroupFName = FName(*CapturedGroupNameStr);
					for (FFlipbookProfileEntry& Anim : Asset->Flipbooks)
					{
						if (Anim.Identity.FlipbookName == FBName)
						{
							Anim.FlipbookGroup = GroupFName;
							break;
						}
					}
					EndTransaction();
					RegisterActiveTimer(0.0f, FWidgetActiveTimerDelegate::CreateLambda(
						[this](double, float) -> EActiveTimerReturnType
						{
							RefreshFlipbookGroupsPanel();
							RefreshPhaseGroupCustomSlotsList();
							return EActiveTimerReturnType::Stop;
						}));
				};

				SlotsRow->AddSlot()
				.AutoWidth()
				.Padding(4, 0)
				[
					CustomDropTarget
				];
			}

			BodyContent->AddSlot()
			.AutoHeight()
			.Padding(4)
			[
				SlotsRow
			];
		}
	}
	else
	// Flipbook cards (regular groups)
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
					BuildFlipbookCard(FlipbookIdx, FlipbookNameUsageCounts)
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
				const FFlipbookProfileEntry& FBData = Asset->Flipbooks[FlipbookIdx];

				UPaperFlipbook* RowFlipbook = !FBData.Identity.Flipbook.IsNull() ? FBData.Identity.Flipbook.LoadSynchronous() : nullptr;
				FString ValidationTooltip;
				const int32 ValidationIssueCount = GetFlipbookValidationIssueCount(FBData, FlipbookNameUsageCounts, &ValidationTooltip);
				const FText ValidationTooltipText = ValidationTooltip.IsEmpty() ? FText::GetEmpty() : FText::FromString(ValidationTooltip);
				TSharedPtr<SInlineEditableTextBlock> RowNameText;

				auto IsRowSelected = [this, FlipbookIdx]() { return FlipbookIdx == SelectedFlipbookIndex || SelectedFlipbookCards.Contains(FlipbookIdx); };

				TSharedRef<SWidget> RowContent = SNew(SBorder)
					.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
					.BorderBackgroundColor_Lambda([IsRowSelected]() -> FSlateColor { return IsRowSelected() ? FLinearColor(0.18f, 0.30f, 0.50f, 1.0f) : FLinearColor(0.0f, 0.0f, 0.0f, 0.0f); })
					.Padding(1)
					[
						SNew(SBorder)
						.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
						.BorderBackgroundColor_Lambda([IsRowSelected]() -> FSlateColor { return IsRowSelected() ? FLinearColor(0.18f, 0.30f, 0.50f, 0.92f) : FLinearColor(0.1f, 0.1f, 0.1f, 0.5f); })
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
									SNew(SBorder)
									.BorderImage(FAppStyle::GetBrush("NoBorder"))
									.ToolTipText(LOCTEXT("ClickToChangeFlipbookRow", "Double-click to change flipbook"))
									.OnMouseDoubleClick_Lambda([this, FlipbookIdx](const FGeometry&, const FPointerEvent&)
									{
										OpenFlipbookPicker(FlipbookIdx);
										return FReply::Handled();
									})
									[
										RowFlipbook
											? StaticCastSharedRef<SWidget>(SNew(SFlipbookThumbnail).Flipbook(RowFlipbook))
											: StaticCastSharedRef<SWidget>(SNew(SBorder)
												.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
												.HAlign(HAlign_Center)
												.VAlign(VAlign_Center)
												[
													SNew(STextBlock)
													.Text(LOCTEXT("MissingFlipbookRow", "No FB"))
													.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
													.ColorAndOpacity(FSlateColor(FLinearColor(0.55f, 0.55f, 0.55f)))
												])
									]
								]
							]

							+ SHorizontalBox::Slot()
							.FillWidth(1.0f)
							.VAlign(VAlign_Center)
							[
								SAssignNew(RowNameText, SInlineEditableTextBlock)
								.Text(FText::FromString(FBData.Identity.FlipbookName))
								.OnTextCommitted_Lambda([this, FlipbookIdx](const FText& NewText, ETextCommit::Type CommitType)
								{
									if (CommitType != ETextCommit::OnCleared)
									{
										RenameFlipbook(FlipbookIdx, NewText.ToString());
									}
								})
							]

							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							[
								SNew(STextBlock)
								.Text(FText::AsNumber(FBData.CombatData.Frames.Num()))
								.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)))
							]

							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							.Padding(6, 0, 0, 0)
							[
								SNew(SBorder)
								.Visibility(ValidationIssueCount > 0 ? EVisibility::Visible : EVisibility::Collapsed)
								.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
								.BorderBackgroundColor(FLinearColor(0.8f, 0.45f, 0.05f, 0.95f))
								.Padding(FMargin(4, 1))
								.ToolTipText(ValidationTooltipText)
								[
									SNew(STextBlock)
									.Text(FText::Format(LOCTEXT("FlipbookIssueBadgeCompact", "! {0}"), FText::AsNumber(ValidationIssueCount)))
									.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
									.ColorAndOpacity(FSlateColor(FLinearColor::White))
								]
							]
						]
					];

				TSharedRef<SDragClickWrapper> RowWrapper = SNew(SDragClickWrapper)
					[RowContent];

				RowWrapper->OnClickedFunc = [this, FlipbookIdx](const FGeometry&, const FPointerEvent& MouseEvent)
				{
					OnFlipbookGroupCardClicked(FlipbookIdx, MouseEvent);
				};
				RowWrapper->OnRightClickedFunc = [this, FlipbookIdx](const FGeometry&, const FPointerEvent& MouseEvent)
				{
					if (!SelectedFlipbookCards.Contains(FlipbookIdx))
					{
						SelectedFlipbookCards.Empty();
						SelectedFlipbookCards.Add(FlipbookIdx);
						SelectionAnchorIndex = FlipbookIdx;
					}
					SelectedFlipbookIndex = FlipbookIdx;
					Invalidate(EInvalidateWidgetReason::Paint);
					ShowFlipbookContextMenu(FlipbookIdx);
				};
				RowWrapper->OnDragDetectedFunc = [this, FlipbookIdx]() -> TSharedPtr<FDragDropOperation>
				{
					TArray<int32> DragIndices;
					if (SelectedFlipbookCards.Contains(FlipbookIdx) && SelectedFlipbookCards.Num() > 1)
					{
						DragIndices = SelectedFlipbookCards.Array();
					}
					else
					{
						DragIndices.Add(FlipbookIdx);
					}
					FName FromGroup = NAME_None;
					if (Asset.IsValid() && Asset->Flipbooks.IsValidIndex(FlipbookIdx))
					{
						FromGroup = Asset->Flipbooks[FlipbookIdx].FlipbookGroup;
					}
					return FFlipbookGroupDragDropOp::NewFromCardDrag(DragIndices, FromGroup);
				};
				if (RowNameText.IsValid())
				{
					FlipbookGroupFlipbookNameTexts.Add(FlipbookIdx, RowNameText);
				}

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
					BuildGroupSection(ChildGroup, ChildGroup->GroupName, NestLevel + 1, Tree, FlipbooksByGroup, FlipbookNameUsageCounts)
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
				.AllowAnimatedTransition(false)
				.InitiallyCollapsed(!bHasActiveFilter && CollapsedFlipbookGroups.Contains(FName("__Ungrouped")))
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
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(6, 0, 0, 0)
					[
						SNew(SBorder)
						.Visibility(GroupIssueCount > 0 ? EVisibility::Visible : EVisibility::Collapsed)
						.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
						.BorderBackgroundColor(FLinearColor(0.8f, 0.45f, 0.05f, 0.95f))
						.Padding(FMargin(6, 2))
						.ToolTipText(LOCTEXT("UngroupedIssueTooltip", "Flipbooks in this section need attention"))
						[
							SNew(STextBlock)
							.Text(FText::Format(LOCTEXT("UngroupedIssueCount", "! {0}"), FText::AsNumber(GroupIssueCount)))
							.TextStyle(FAppStyle::Get(), "SmallText")
							.ColorAndOpacity(FSlateColor(FLinearColor::White))
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
		DropTarget->OnGroupDropFunc = [this](FName SourceGroupName, FName TargetParentGroup)
		{
			OnGroupDrop(SourceGroupName, TargetParentGroup);
		};

		return SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[ DropTarget ];
	}

	// Named group with full header
	check(GroupInfo != nullptr);

	TSharedPtr<SInlineEditableTextBlock> GroupNameText;
	TSharedPtr<SGroupDragHandle> GroupDragHandle;

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
				.AllowAnimatedTransition(false)
				.InitiallyCollapsed(!bHasActiveFilter && CollapsedFlipbookGroups.Contains(GroupName))
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
					// Visual highlight when this phase group is selected in the Details panel.
					.BorderBackgroundColor_Lambda([this, GroupName]() -> FSlateColor
					{
						return SelectedPhaseGroupName == GroupName
							? FLinearColor(0.18f, 0.30f, 0.50f, 0.8f)
							: FLinearColor(0.0f, 0.0f, 0.0f, 0.0f);
					})
					.OnMouseButtonDown_Lambda([this, GroupName](const FGeometry& Geom, const FPointerEvent& MouseEvent) -> FReply
					{
						if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
						{
							ShowFlipbookGroupContextMenu(GroupName, MouseEvent.GetScreenSpacePosition());
							return FReply::Handled();
						}
						if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton
							&& Asset.IsValid())
						{
							// Only phase groups show details in the side panel.
							// Regular groups are structural — clicking them doesn't
							// switch the Details view. (User can still expand/collapse
							// them via the SExpandableArea triangle, which handles
							// its own click separately.)
							for (const FFlipbookGroupInfo& GI : Asset->FlipbookGroups)
							{
								if (GI.GroupName == GroupName && GI.bIsPhaseGroup)
								{
									SelectPhaseGroup(GroupName);
									// Don't mark handled — let SExpandableArea still
									// process the expand/collapse toggle.
									break;
								}
							}
						}
						return FReply::Unhandled();
					})
					[
						SNew(SHorizontalBox)

						// Drag handle (grip dots + color dot — drag to reparent group)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(2, 0)
						[
							SAssignNew(GroupDragHandle, SGroupDragHandle)
							.GroupColor(GroupInfo->Color)
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
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(6, 0, 0, 0)
						[
							SNew(SBorder)
							.Visibility(GroupIssueCount > 0 ? EVisibility::Visible : EVisibility::Collapsed)
							.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
							.BorderBackgroundColor(FLinearColor(0.8f, 0.45f, 0.05f, 0.95f))
							.Padding(FMargin(6, 2))
							.ToolTipText(LOCTEXT("GroupIssueTooltip", "Flipbooks in this group need attention"))
							[
								SNew(STextBlock)
								.Text(FText::Format(LOCTEXT("GroupIssueCount", "! {0}"), FText::AsNumber(GroupIssueCount)))
								.TextStyle(FAppStyle::Get(), "SmallText")
								.ColorAndOpacity(FSlateColor(FLinearColor::White))
							]
						]

						// Delete group button
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(4, 0, 0, 0)
						[
							SNew(SButton)
							.ButtonStyle(FAppStyle::Get(), "SimpleButton")
							.ContentPadding(FMargin(1))
							.ToolTipText(LOCTEXT("DeleteGroupTip", "Delete this group"))
							.OnClicked_Lambda([this, GroupName]()
							{
								DeleteFlipbookGroup(GroupName);
								return FReply::Handled();
							})
							[
								SNew(SImage)
								.Image(FAppStyle::GetBrush("Icons.Delete"))
								.DesiredSizeOverride(FVector2D(12, 12))
								.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)))
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
	DropTarget->OnGroupDropFunc = [this](FName SourceGroupName, FName TargetParentGroup)
	{
		OnGroupDrop(SourceGroupName, TargetParentGroup);
	};

	// Set group name on drag handle
	if (GroupDragHandle.IsValid())
	{
		GroupDragHandle->GroupName = GroupName;
	}

	// Store text widget for programmatic rename
	if (GroupNameText.IsValid())
	{
		FlipbookGroupNameTexts.Add(GroupName, GroupNameText);
	}

	return StaticCastSharedRef<SWidget>(DropTarget);
}

TSharedRef<SWidget> SCharacterProfileAssetEditor::BuildFlipbookCard(int32 FlipbookIndex, const TMap<FString, int32>& FlipbookNameUsageCounts)
{
	if (!Asset.IsValid() || !Asset->Flipbooks.IsValidIndex(FlipbookIndex))
	{
		return SNullWidget::NullWidget;
	}

	const FFlipbookProfileEntry& FBData = Asset->Flipbooks[FlipbookIndex];

	// Load flipbook for animated thumbnail
	UPaperFlipbook* LoadedFlipbook = !FBData.Identity.Flipbook.IsNull() ? FBData.Identity.Flipbook.LoadSynchronous() : nullptr;
	FString ValidationTooltip;
	const int32 ValidationIssueCount = GetFlipbookValidationIssueCount(FBData, FlipbookNameUsageCounts, &ValidationTooltip);
	const FText ValidationTooltipText = ValidationTooltip.IsEmpty() ? FText::GetEmpty() : FText::FromString(ValidationTooltip);
	TSharedPtr<SInlineEditableTextBlock> CardNameText;

	auto IsCardSelected = [this, FlipbookIndex]() { return FlipbookIndex == SelectedFlipbookIndex || SelectedFlipbookCards.Contains(FlipbookIndex); };

	static FSlateRoundedBoxBrush CardBrush(FLinearColor::White, 8.0f);
	static FSlateRoundedBoxBrush CardSelectionBrush(FLinearColor::White, 9.0f);

	TSharedRef<SWidget> CardContent = SNew(SBox)
		.WidthOverride(140.f)
		.HeightOverride(120.f)
		[
			SNew(SBorder)
			.BorderImage(&CardSelectionBrush)
			.BorderBackgroundColor_Lambda([IsCardSelected]() -> FSlateColor { return IsCardSelected() ? FLinearColor(0.28f, 0.44f, 0.68f, 1.0f) : FLinearColor(0.0f, 0.0f, 0.0f, 0.0f); })
			.Padding(1)
			[
				SNew(SBorder)
				.BorderImage(&CardBrush)
				.BorderBackgroundColor_Lambda([IsCardSelected]() -> FSlateColor { return IsCardSelected() ? FLinearColor(0.18f, 0.30f, 0.50f, 1.0f) : FLinearColor(0.22f, 0.22f, 0.24f, 1.0f); })
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
							SNew(SBorder)
							.BorderImage(FAppStyle::GetBrush("NoBorder"))
							.ToolTipText(LOCTEXT("ClickToChangeFlipbookCard", "Double-click to change flipbook"))
							.OnMouseDoubleClick_Lambda([this, FlipbookIndex](const FGeometry&, const FPointerEvent&)
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
											.Text(LOCTEXT("NoFlipbook", "No FB"))
											.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
											.ColorAndOpacity(FSlateColor(FLinearColor(0.55f, 0.55f, 0.55f)))
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
						SAssignNew(CardNameText, SInlineEditableTextBlock)
						.Text(FText::FromString(FBData.Identity.FlipbookName))
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
						.Justification(ETextJustify::Center)
						.OnTextCommitted_Lambda([this, FlipbookIndex](const FText& NewText, ETextCommit::Type CommitType)
						{
							if (CommitType != ETextCommit::OnCleared)
							{
								RenameFlipbook(FlipbookIndex, NewText.ToString());
							}
						})
					]

					// Frame count + validation badge
					+ SVerticalBox::Slot()
					.AutoHeight()
					.HAlign(HAlign_Center)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						[
							SNew(STextBlock)
							.Text_Lambda([this, FlipbookIndex]()
							{
								if (Asset.IsValid() && Asset->Flipbooks.IsValidIndex(FlipbookIndex))
								{
									const FFlipbookProfileEntry& FB = Asset->Flipbooks[FlipbookIndex];
									if (FB.FrameEventData.FrameEvents.Num() > 0)
									{
										return FText::Format(LOCTEXT("FrameCountWithFX", "{0}f  FX:{1}"),
											FText::AsNumber(FB.CombatData.Frames.Num()), FText::AsNumber(FB.FrameEventData.FrameEvents.Num()));
									}
									return FText::Format(LOCTEXT("FrameCountBadge", "{0} frames"),
										FText::AsNumber(FB.CombatData.Frames.Num()));
								}
								return FText::GetEmpty();
							})
							.Font(FCoreStyle::GetDefaultFontStyle("Regular", 7))
							.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)))
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(6, 0, 0, 0)
						.VAlign(VAlign_Center)
						[
							SNew(SBorder)
							.Visibility(ValidationIssueCount > 0 ? EVisibility::Visible : EVisibility::Collapsed)
							.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
							.BorderBackgroundColor(FLinearColor(0.8f, 0.45f, 0.05f, 0.95f))
							.Padding(FMargin(4, 1))
							.ToolTipText(ValidationTooltipText)
							[
								SNew(STextBlock)
								.Text(FText::Format(LOCTEXT("FlipbookIssueBadge", "! {0}"), FText::AsNumber(ValidationIssueCount)))
								.Font(FCoreStyle::GetDefaultFontStyle("Bold", 7))
								.ColorAndOpacity(FSlateColor(FLinearColor::White))
							]
						]
					]
				]
			]
		];

	// Wrap in drag wrapper
	TSharedRef<SDragClickWrapper> Wrapper = SNew(SDragClickWrapper)
		[CardContent];

	Wrapper->OnClickedFunc = [this, FlipbookIndex](const FGeometry&, const FPointerEvent& MouseEvent)
	{
		OnFlipbookGroupCardClicked(FlipbookIndex, MouseEvent);
	};
	Wrapper->OnRightClickedFunc = [this, FlipbookIndex](const FGeometry&, const FPointerEvent& MouseEvent)
	{
		if (!SelectedFlipbookCards.Contains(FlipbookIndex))
		{
			SelectedFlipbookCards.Empty();
			SelectedFlipbookCards.Add(FlipbookIndex);
			SelectionAnchorIndex = FlipbookIndex;
		}
		SelectedFlipbookIndex = FlipbookIndex;
		Invalidate(EInvalidateWidgetReason::Paint);
		ShowFlipbookContextMenu(FlipbookIndex);
	};
	Wrapper->OnDragDetectedFunc = [this, FlipbookIndex]() -> TSharedPtr<FDragDropOperation>
	{
		TArray<int32> DragIndices;
		if (SelectedFlipbookCards.Contains(FlipbookIndex) && SelectedFlipbookCards.Num() > 1)
		{
			DragIndices = SelectedFlipbookCards.Array();
		}
		else
		{
			DragIndices.Add(FlipbookIndex);
		}
		FName FromGroup = NAME_None;
		if (Asset.IsValid() && Asset->Flipbooks.IsValidIndex(FlipbookIndex))
		{
			FromGroup = Asset->Flipbooks[FlipbookIndex].FlipbookGroup;
		}
		return FFlipbookGroupDragDropOp::NewFromCardDrag(DragIndices, FromGroup);
	};
	if (CardNameText.IsValid())
	{
		FlipbookGroupFlipbookNameTexts.Add(FlipbookIndex, CardNameText);
	}

	return Wrapper;
}

// ==========================================
// SELECTION
// ==========================================

void SCharacterProfileAssetEditor::OnFlipbookGroupCardClicked(int32 FlipbookIndex, const FPointerEvent& MouseEvent)
{
	SetActivePanelSection(FName(TEXT("Overview.Flipbooks")));

	// Clicking a flipbook card clears any selected phase group so the
	// Details side panel switches back to the flipbook details view.
	if (!SelectedPhaseGroupName.IsNone())
	{
		SelectedPhaseGroupName = NAME_None;
	}

	// Sync canonical selection — on tab 0, skip full RefreshAll since selection
	// highlights are _Lambda bindings. Just update state and invalidate paint.
	if (FlipbookIndex != SelectedFlipbookIndex)
	{
		if (bIsPlaying && PlaybackQueue.Num() > 0)
		{
			StopPlayback();
		}
		SelectedFlipbookIndex = FlipbookIndex;
		SelectedFrameIndex = 0;
		ClearFrameSelection();
		if (EditorCanvas.IsValid())
		{
			EditorCanvas->ClearSelection();
		}
		// Mark non-overview tabs dirty for deferred refresh
		MarkAllTabsDirty();
		ClearTabDirty(0);
		// Paint-only invalidation — lambdas pick up new SelectedFlipbookIndex
		Invalidate(EInvalidateWidgetReason::Paint);
	}

	// Now apply the desired card selection on top
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
}

bool SCharacterProfileAssetEditor::PassesFlipbookGroupSearch(const FFlipbookProfileEntry& FlipbookData) const
{
	// Text search
	const FString Query = FlipbookGroupSearchText.TrimStartAndEnd();
	if (!Query.IsEmpty() && !FlipbookData.Identity.FlipbookName.Contains(Query, ESearchCase::IgnoreCase))
	{
		return false;
	}

	// Completion filters
	if (CompletionFilterMask != 0)
	{
		const int32 Flags = FlipbookData.EditorMeta.CompletionFlags;
		constexpr int32 AllTaskBits = 0x7F; // Bits 0-6

		// Bit 7: "All Complete" — only show fully complete
		if (CompletionFilterMask & (1 << 7))
		{
			if ((Flags & AllTaskBits) != AllTaskBits) return false;
		}

		// Bit 8: "Incomplete" — only show flipbooks missing at least one task
		if (CompletionFilterMask & (1 << 8))
		{
			if ((Flags & AllTaskBits) == AllTaskBits) return false;
		}

		// Bits 0-6: per-task "needs work" filters — show only flipbooks where that task is NOT done
		const int32 TaskFilterBits = CompletionFilterMask & AllTaskBits;
		if (TaskFilterBits != 0)
		{
			// Flipbook must be missing at least one of the filtered tasks
			if ((~Flags & TaskFilterBits) == 0) return false;
		}
	}

	return true;
}

// ==========================================
// GROUP MANAGEMENT
// ==========================================

void SCharacterProfileAssetEditor::CreateFlipbookGroup(FName ParentGroup)
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

void SCharacterProfileAssetEditor::DeleteFlipbookGroup(FName GroupName)
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
	// If this is a phase group, also remove the FPhaseGroup and clear FlipbookGroup on assigned flipbooks
	const FFlipbookGroupInfo* GI = nullptr;
	for (const FFlipbookGroupInfo& G : Asset->FlipbookGroups)
	{
		if (G.GroupName == GroupName) { GI = &G; break; }
	}
	if (GI && GI->bIsPhaseGroup)
	{
		FString GroupNameStr = GroupName.ToString();
		Asset->PhaseGroups.RemoveAll([&GroupNameStr](const FPhaseGroup& PG) { return PG.GroupName == GroupNameStr; });
		for (FFlipbookProfileEntry& Anim : Asset->Flipbooks)
		{
			if (Anim.FlipbookGroup == GroupName)
			{
				Anim.FlipbookGroup = NAME_None;
			}
		}
	}
	Asset->RemoveFlipbookGroup(GroupName);
	EndTransaction();

	RefreshFlipbookGroupsPanel();
	MarkTabDirty(4); // Phase editor uses groups — deferred refresh
}

void SCharacterProfileAssetEditor::ShowFlipbookGroupContextMenu(FName GroupName, const FVector2D& CursorPos)
{
	FMenuBuilder MenuBuilder(true, nullptr);

	MenuBuilder.AddMenuEntry(
		LOCTEXT("RenameGroup", "Rename"),
		LOCTEXT("RenameGroupTooltip", "Rename this group"),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateSP(this, &SCharacterProfileAssetEditor::TriggerFlipbookGroupRename, GroupName))
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
					FUIAction(FExecuteAction::CreateSP(this, &SCharacterProfileAssetEditor::OnFlipbookGroupColorCommitted, Color.Value, GroupName))
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

	MenuBuilder.AddMenuEntry(
		LOCTEXT("AddPhaseSubGroup", "Add Phase Sub-group"),
		LOCTEXT("AddPhaseSubGroupTooltip", "Create a phase group (Startup/Active/Recovery) nested under this one"),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([this, GroupName]()
		{
			if (!Asset.IsValid()) return;
			FString BaseName = TEXT("New Phase Group");
			FString NewName = BaseName;
			int32 Counter = 1;
			while (Asset->FindPhaseGroup(NewName) != nullptr)
			{
				NewName = FString::Printf(TEXT("%s %d"), *BaseName, Counter++);
			}
			BeginTransaction(LOCTEXT("CreatePhaseSubGroup", "Create Phase Sub-group"));
			FPhaseGroup NewGroup;
			NewGroup.GroupName = NewName;
			Asset->PhaseGroups.Add(MoveTemp(NewGroup));
			FFlipbookGroupInfo GroupInfo;
			GroupInfo.GroupName = FName(*NewName);
			GroupInfo.ParentGroup = GroupName;
			GroupInfo.bIsPhaseGroup = true;
			Asset->FlipbookGroups.Add(GroupInfo);
			EndTransaction();
			PendingRenameFlipbookGroup = FName(*NewName);
			RefreshFlipbookGroupsPanel();
			MarkTabDirty(4); // Phase editor uses groups — deferred refresh
		}))
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
void SCharacterProfileAssetEditor::TriggerFlipbookGroupRename(FName GroupName)
{
	PendingRenameFlipbookGroup = GroupName;
	RefreshFlipbookGroupsPanel();
}

// ==========================================
// RENAME VALIDATION
// ==========================================

bool SCharacterProfileAssetEditor::OnVerifyFlipbookGroupNameChanged(const FText& InText, FText& OutErrorMessage, FName CurrentGroupName)
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

void SCharacterProfileAssetEditor::OnFlipbookGroupNameCommitted(const FText& InText, ETextCommit::Type CommitType, FName OriginalGroupName)
{
	if (CommitType == ETextCommit::OnEnter)
	{
		FName NewName(*InText.ToString());
		if (NewName != NAME_None && NewName != OriginalGroupName && Asset.IsValid())
		{
			BeginTransaction(LOCTEXT("RenameGroup", "Rename Flipbook Group"));
			// If this is a phase group, also rename the FPhaseGroup
			for (const FFlipbookGroupInfo& GI : Asset->FlipbookGroups)
			{
				if (GI.GroupName == OriginalGroupName && GI.bIsPhaseGroup)
				{
					FString OldNameStr = OriginalGroupName.ToString();
					FString NewNameStr = NewName.ToString();
					FPhaseGroup* PG = Asset->FindPhaseGroupMutable(OldNameStr);
					if (PG) PG->GroupName = NewNameStr;
					break;
				}
			}
			Asset->RenameFlipbookGroup(OriginalGroupName, NewName);
			EndTransaction();
			RefreshFlipbookGroupsPanel();
			MarkTabDirty(4); // Phase editor uses groups — deferred refresh
		}
	}
	// OnUserMovedFocus and OnCleared: revert (don't auto-commit)
}

// ==========================================
// COLOR PICKER
// ==========================================

void SCharacterProfileAssetEditor::OnOpenFlipbookGroupColorPicker(FName GroupName, FLinearColor CurrentColor)
{
	FColorPickerArgs PickerArgs;
	PickerArgs.bIsModal = true;
	PickerArgs.ParentWidget = SharedThis(this);
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 2)
	PickerArgs.InitialColor = CurrentColor;
#endif
	PickerArgs.OnColorCommitted = FOnLinearColorValueChanged::CreateSP(
		this, &SCharacterProfileAssetEditor::OnFlipbookGroupColorCommitted, GroupName);
	OpenColorPicker(PickerArgs);
}

void SCharacterProfileAssetEditor::OnFlipbookGroupColorCommitted(FLinearColor NewColor, FName GroupName)
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

void SCharacterProfileAssetEditor::AutoGroupByPrefix()
{
	if (!Asset.IsValid()) return;

	// Only operate on ungrouped flipbooks
	TMap<FString, TArray<int32>> PrefixToFlipbooks;

	for (int32 i = 0; i < Asset->Flipbooks.Num(); ++i)
	{
		if (Asset->Flipbooks[i].FlipbookGroup != NAME_None) continue;

		const FString& Name = Asset->Flipbooks[i].Identity.FlipbookName;

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

void SCharacterProfileAssetEditor::OnFlipbookGroupFlipbooksDrop(const TArray<int32>& FlipbookIndices, FName TargetGroup)
{
	if (!Asset.IsValid() || FlipbookIndices.Num() == 0) return;

	// Check if target is a phase group — auto-assign to first empty slot
	const FFlipbookGroupInfo* TargetGI = nullptr;
	for (const FFlipbookGroupInfo& GI : Asset->FlipbookGroups)
	{
		if (GI.GroupName == TargetGroup) { TargetGI = &GI; break; }
	}

	if (TargetGI && TargetGI->bIsPhaseGroup)
	{
		FPhaseGroup* PG = Asset->FindPhaseGroupMutable(TargetGroup.ToString());
		if (!PG) return;

		// Find first empty slot, or displace first slot if all full
		EAnimationPhase TargetSlot = EAnimationPhase::None;
		static const EAnimationPhase SlotOrder[] = { EAnimationPhase::Startup, EAnimationPhase::Active, EAnimationPhase::Recovery };
		for (EAnimationPhase Phase : SlotOrder)
		{
			if (PG->GetFlipbookForPhase(Phase).IsEmpty()) { TargetSlot = Phase; break; }
		}
		if (TargetSlot == EAnimationPhase::None)
		{
			// All slots full — displace the first slot's occupant to Ungrouped
			TargetSlot = EAnimationPhase::Startup;
		}

		// Only use the first dropped flipbook for phase assignment
		int32 Idx = FlipbookIndices[0];
		if (!Asset->Flipbooks.IsValidIndex(Idx)) return;
		const FString& FBName = Asset->Flipbooks[Idx].Identity.FlipbookName;

		// Already in this slot?
		if (PG->GetPhaseForFlipbook(FBName) != EAnimationPhase::None) return;

		BeginTransaction(LOCTEXT("DropToPhaseSlot", "Assign Flipbook to Phase Slot"));

		// Clear old phase assignment if any
		const FPhaseGroup* OldPG = Asset->FindPhaseGroupForFlipbook(FBName);
		if (OldPG)
		{
			FPhaseGroup* OldPGMut = Asset->FindPhaseGroupMutable(OldPG->GroupName);
			if (OldPGMut)
			{
				EAnimationPhase OldPhase = OldPGMut->GetPhaseForFlipbook(FBName);
				if (OldPhase != EAnimationPhase::None)
				{
					OldPGMut->SetFlipbookForPhase(OldPhase, FString());
					OldPGMut->SetSequenceForPhase(OldPhase, nullptr);
				}
			}
		}

		// Displace existing occupant of the target slot to Ungrouped
		FString DisplacedFB = PG->GetFlipbookForPhase(TargetSlot);
		if (!DisplacedFB.IsEmpty() && DisplacedFB != FBName)
		{
			PG->SetFlipbookForPhase(TargetSlot, FString());
			for (FFlipbookProfileEntry& FB : Asset->Flipbooks)
			{
				if (FB.Identity.FlipbookName == DisplacedFB) { FB.FlipbookGroup = NAME_None; break; }
			}
		}

		PG->SetFlipbookForPhase(TargetSlot, FBName);
		Asset->Flipbooks[Idx].FlipbookGroup = TargetGroup;
		EndTransaction();

		CollapsedFlipbookGroups.Remove(TargetGroup);
		RefreshFlipbookGroupsPanel();
		return;
	}

	// Regular group drop — existing logic
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
		// Clear phase slot assignment if dragging out of a phase group
		const FString& FBName = Asset->Flipbooks[Idx].Identity.FlipbookName;
		const FPhaseGroup* OldPhaseGroup = Asset->FindPhaseGroupForFlipbook(FBName);
		if (OldPhaseGroup)
		{
			FPhaseGroup* OldPGMut = Asset->FindPhaseGroupMutable(OldPhaseGroup->GroupName);
			if (OldPGMut)
			{
				EAnimationPhase OldPhase = OldPGMut->GetPhaseForFlipbook(FBName);
				if (OldPhase != EAnimationPhase::None)
				{
					OldPGMut->SetFlipbookForPhase(OldPhase, FString());
					OldPGMut->SetSequenceForPhase(OldPhase, nullptr);
				}
			}
		}
		Asset->MoveFlipbookToFlipbookGroup(Idx, TargetGroup);
	}
	EndTransaction();

	// Expand the target group to show dropped cards
	CollapsedFlipbookGroups.Remove(TargetGroup == NAME_None ? FName("__Ungrouped") : TargetGroup);

	RefreshFlipbookGroupsPanel();
}

void SCharacterProfileAssetEditor::OnGroupDrop(FName SourceGroupName, FName TargetParentGroup)
{
	if (!Asset.IsValid() || SourceGroupName == NAME_None) return;

	// Can't drop onto self
	if (SourceGroupName == TargetParentGroup) return;

	// Can't drop a group into its own descendant (would create a cycle)
	if (TargetParentGroup != NAME_None && Asset->IsDescendantOfFlipbookGroup(TargetParentGroup, SourceGroupName)) return;

	// Check if already at target parent (no-op)
	for (const FFlipbookGroupInfo& Group : Asset->FlipbookGroups)
	{
		if (Group.GroupName == SourceGroupName)
		{
			if (Group.ParentGroup == TargetParentGroup) return;
			break;
		}
	}

	BeginTransaction(LOCTEXT("ReparentGroup", "Move Group"));
	Asset->ReparentFlipbookGroup(SourceGroupName, TargetParentGroup);
	EndTransaction();

	// Expand target to show the moved group
	CollapsedFlipbookGroups.Remove(TargetParentGroup == NAME_None ? FName("__Ungrouped") : TargetParentGroup);

	RefreshFlipbookGroupsPanel();
	MarkTabDirty(4); // Phase editor uses groups — deferred refresh
}

#undef LOCTEXT_NAMESPACE
