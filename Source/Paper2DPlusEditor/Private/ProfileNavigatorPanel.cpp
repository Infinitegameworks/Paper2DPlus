// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "ProfileNavigatorPanel.h"

#include "Framework/Application/SlateApplication.h"
#include "HAL/PlatformTime.h"
#include "Input/Events.h"
#include "InputCoreTypes.h"
#include "SlateShortcutUtils.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
#include "EditorStyleSet.h"
#ifndef FAppStyle
#define FAppStyle FEditorStyle
#endif
#else
#include "Styling/AppStyle.h"
#endif

#define LOCTEXT_NAMESPACE "ProfileNavigatorPanel"

void SProfileNavigatorPanel::Construct(const FArguments& InArgs)
{
	Source = InArgs._Source;
	Mode = InArgs._Mode;
	OnDismissed = InArgs._OnDismissed;
	OnQueryChanged = InArgs._OnQueryChanged;
	OnItemDoubleClicked = InArgs._OnItemDoubleClicked;
	OnGenerateItemPreview = InArgs._OnGenerateItemPreview;
	OnGenerateItemExtension = InArgs._OnGenerateItemExtension;
	OnGenerateItemContent = InArgs._OnGenerateItemContent;
	ResultModel = MakeShared<FProfilePickerResultModel>(Source);
	ResultModel->SetQuery(InArgs._InitialQuery);

	if (Source.IsValid())
	{
		SourceChangedHandle = Source->OnSourceChanged().AddSP(this, &SProfileNavigatorPanel::RefreshResults);
	}
	StoreChangedHandle = FProfilePickerCatalogStore::Get().OnStoreChanged().AddSP(this, &SProfileNavigatorPanel::RefreshResults);

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
		.Padding(FMargin(4))
		.AccessibleText(LOCTEXT("NavigatorAccessible", "Profile item navigator"))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 0, 0, 4)
			[
				SAssignNew(SearchBox, SSearchBox)
				.InitialText(FText::FromString(InArgs._InitialQuery))
				.HintText(LOCTEXT("SearchHint", "Search name, alias, group, or animation tag…"))
				.AccessibleText(LOCTEXT("SearchAccessible", "Search profile items by name, alias, details, group, or gameplay tag"))
				.OnTextChanged(this, &SProfileNavigatorPanel::HandleQueryChanged)
				.OnKeyDownHandler(this, &SProfileNavigatorPanel::HandleSearchKeyDown)
			]
			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			[
				SNew(SOverlay)
				+ SOverlay::Slot()
				[
					SAssignNew(ListView, SListView<TSharedPtr<FProfilePickerDisplayRow>>)
					.AccessibleText(LOCTEXT("ResultsAccessible", "Profile item results. Use Up and Down to navigate and Enter to select."))
					.ListItemsSource(&ResultModel->GetRows())
					.SelectionMode(ESelectionMode::Single)
					.OnGenerateRow(this, &SProfileNavigatorPanel::GenerateRow)
					.OnSelectionChanged(this, &SProfileNavigatorPanel::HandleSelectionChanged)
					.OnMouseButtonDoubleClick(this, &SProfileNavigatorPanel::HandleDoubleClick)
				]
				+ SOverlay::Slot()
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				.Padding(16)
				[
					SNew(STextBlock)
					.Visibility_Lambda([this]()
					{
						return ResultModel.IsValid() && ResultModel->GetMatchingItems().IsEmpty()
							? EVisibility::HitTestInvisible : EVisibility::Collapsed;
					})
					.Text(this, &SProfileNavigatorPanel::GetEmptyStateText)
					.Justification(ETextJustify::Center)
					.AutoWrapText(true)
				]
			]
		]
	];

	if (Mode == EProfileNavigatorMode::Popup)
	{
		RegisterActiveTimer(0.0f, FWidgetActiveTimerDelegate::CreateLambda(
			[WeakSearch = TWeakPtr<SSearchBox>(SearchBox)](double, float)
			{
				if (TSharedPtr<SSearchBox> Search = WeakSearch.Pin())
				{
					FSlateApplication::Get().SetKeyboardFocus(Search, EFocusCause::SetDirectly);
				}
				return EActiveTimerReturnType::Stop;
			}));
	}
	RefreshResults();
}

SProfileNavigatorPanel::~SProfileNavigatorPanel()
{
	if (Source.IsValid()) Source->OnSourceChanged().Remove(SourceChangedHandle);
	FProfilePickerCatalogStore::Get().OnStoreChanged().Remove(StoreChangedHandle);
}

void SProfileNavigatorPanel::RefreshResults()
{
	if (!ResultModel.IsValid() || bRefreshingResults) return;
	if (bCommittingSelection)
	{
		bRefreshDeferred = true;
		return;
	}
	TGuardValue<bool> RefreshGuard(bRefreshingResults, true);
	ResultModel->Refresh();
	ConstructedRowCount = 0;
	ConstructedPreviewCount = 0;
	if (ListView.IsValid())
	{
		ListView->RequestListRefresh();
		RestoreListSelection();
	}
}

void SProfileNavigatorPanel::SetQueryForTests(const FString& Query)
{
	if (ResultModel.IsValid() && ResultModel->GetQuery().Equals(Query.TrimStartAndEnd(), ESearchCase::CaseSensitive))
	{
		return;
	}
	if (SearchBox.IsValid()) SearchBox->SetText(FText::FromString(Query));
	else if (ResultModel.IsValid()) ResultModel->SetQuery(Query);
}

int32 SProfileNavigatorPanel::GetResultCountForTests() const
{
	return ResultModel.IsValid() ? ResultModel->GetMatchingItems().Num() : 0;
}

int32 SProfileNavigatorPanel::GenerateRowsForViewportForTests(const FVector2D& ViewportSize)
{
	if (!ListView.IsValid()) return 0;
	ConstructedRowCount = 0;
	ConstructedPreviewCount = 0;
	ListView->SlatePrepass(1.0f);
	ListView->Tick(FGeometry::MakeRoot(ViewportSize, FSlateLayoutTransform()), FPlatformTime::Seconds(), 0.0f);
	return ListView->GetNumGeneratedChildren();
}

bool SProfileNavigatorPanel::SelectIdentityForTests(const FProfileItemIdentity& Identity)
{
	if (!ResultModel.IsValid()) return false;
	TSharedPtr<FProfilePickerItem> Item = ResultModel->Resolve(Identity);
	if (!Item.IsValid()) return false;
	TSharedPtr<FProfilePickerDisplayRow> Row = MakeShared<FProfilePickerDisplayRow>();
	Row->Item = Item;
	return CommitRow(Row);
}

bool SProfileNavigatorPanel::DoubleClickIdentityForTests(const FProfileItemIdentity& Identity)
{
	if (!ResultModel.IsValid()) return false;
	TSharedPtr<FProfilePickerItem> Item = ResultModel->Resolve(Identity);
	if (!Item.IsValid()) return false;
	TSharedPtr<FProfilePickerDisplayRow> Row = MakeShared<FProfilePickerDisplayRow>();
	Row->Item = Item;
	HandleDoubleClick(Row);
	return true;
}

TSharedRef<ITableRow> SProfileNavigatorPanel::GenerateRow(TSharedPtr<FProfilePickerDisplayRow> Row,
	const TSharedRef<STableViewBase>& OwnerTable)
{
	++ConstructedRowCount;
	if (!Row.IsValid() || Row->Kind == FProfilePickerDisplayRow::EKind::Header)
	{
		return SNew(STableRow<TSharedPtr<FProfilePickerDisplayRow>>, OwnerTable)
			.IsEnabled(false)
			.Padding(FMargin(4, 5, 4, 2))
			.AccessibleText(Row.IsValid() ? Row->Header : FText::GetEmpty())
			[
				SNew(STextBlock)
				.Text(Row.IsValid() ? Row->Header : FText::GetEmpty())
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
			];
	}

	const FProfileItemIdentity Identity = Row->Item->Identity;
	const FString Scope = Source.IsValid() ? Source->GetLogicalCatalogScope() : FString();
	const TSharedPtr<SWidget> Preview = OnGenerateItemPreview.IsBound()
		? OnGenerateItemPreview.Execute(*Row->Item)
		: nullptr;
	if (Preview.IsValid())
	{
		++ConstructedPreviewCount;
	}
	const TSharedRef<SWidget> Extension = OnGenerateItemExtension.IsBound()
		? OnGenerateItemExtension.Execute(Identity)
		: SNullWidget::NullWidget;

	const TSharedRef<SWidget> DefaultContent = SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(Preview.IsValid() ? FMargin(0, 0, 7, 0) : FMargin(0))
			[
				Preview.IsValid() ? Preview.ToSharedRef() : SNullWidget::NullWidget
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock)
					.Text(Row->Item->Label)
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock)
					.Text(Row->Item->SecondaryText)
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
					.ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.6f, 0.62f)))
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(4, 0)
			[
				SNew(STextBlock)
				.Text_Lambda([WeakSource = TWeakPtr<IProfileItemPickerSource>(Source), Identity]()
				{
					const TSharedPtr<IProfileItemPickerSource> PinnedSource = WeakSource.Pin();
					return PinnedSource.IsValid() && PinnedSource->GetSelectedIdentity().Matches(Identity)
						? LOCTEXT("SelectedState", "Selected") : FText::GetEmpty();
				})
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.AccessibleText_Lambda([Scope, Identity]()
				{
					return FProfilePickerCatalogStore::Get().GetPinActionLabel(Scope, Identity);
				})
				.ToolTipText_Lambda([Scope, Identity]()
				{
					return FProfilePickerCatalogStore::Get().IsPinned(Scope, Identity)
						? LOCTEXT("UnpinTooltip", "Unpin this item from this profile catalog")
						: LOCTEXT("PinTooltip", "Pin this item for Character and Layer editors using this profile");
				})
				.OnClicked_Lambda([Scope, Identity]()
				{
					FProfilePickerCatalogStore::Get().TogglePin(Scope, Identity);
					return FReply::Handled();
				})
				[
					SNew(STextBlock)
					.Text_Lambda([Scope, Identity]()
					{
						return FProfilePickerCatalogStore::Get().GetPinActionLabel(Scope, Identity);
					})
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(3, 0, 0, 0)
			[
				Extension
			];
	const bool bHasCustomItemContent = OnGenerateItemContent.IsBound();
	const TSharedRef<SWidget> ItemContent = bHasCustomItemContent
		? OnGenerateItemContent.Execute(Identity, DefaultContent)
		: DefaultContent;

	return SNew(STableRow<TSharedPtr<FProfilePickerDisplayRow>>, OwnerTable)
		.Style(bHasCustomItemContent
			? &FAppStyle::Get().GetWidgetStyle<FTableRowStyle>(TEXT("ContentBrowser.AssetListView.TileTableRow"))
			: &FCoreStyle::Get().GetWidgetStyle<FTableRowStyle>(TEXT("TableView.Row")))
		.ShowSelection(!bHasCustomItemContent)
		.Padding(FMargin(3, 2))
		.AccessibleText(FText::Format(LOCTEXT("ItemAccessible", "{0}. {1}. Group {2}."),
			Row->Item->Label, Row->Item->SecondaryText,
			FText::FromString(Row->Item->Group.IsEmpty() ? TEXT("Ungrouped") : Row->Item->Group)))
		.ToolTipText(FText::Format(LOCTEXT("ItemTooltip", "{0}\n{1}\nGroup: {2}"),
			Row->Item->Label, Row->Item->SecondaryText,
			FText::FromString(Row->Item->Group.IsEmpty() ? TEXT("Ungrouped") : Row->Item->Group)))
		[
			ItemContent
		];
}

void SProfileNavigatorPanel::HandleSelectionChanged(TSharedPtr<FProfilePickerDisplayRow> Row, ESelectInfo::Type SelectInfo)
{
	if (!Row.IsValid())
	{
		return;
	}
	if (Row->Kind == FProfilePickerDisplayRow::EKind::Header)
	{
		if (SelectInfo != ESelectInfo::Direct && ListView.IsValid()) ListView->ClearSelection();
		return;
	}
	// Keyboard arrows move the visible focus/selection only; Enter commits. A mouse click is the one
	// selection-change path that commits immediately (the standard compact-picker behavior).
	if (SelectInfo == ESelectInfo::OnMouseClick)
	{
		CommitRow(Row);
	}
}

void SProfileNavigatorPanel::HandleDoubleClick(TSharedPtr<FProfilePickerDisplayRow> Row)
{
	if (!Row.IsValid() || !Row->Item.IsValid()) return;
	// SListView delivers the preceding single-click first. Do not call the provider twice for one
	// double-click gesture; only commit here if the row was not already selected by that click.
	if (!Source.IsValid() || !Source->GetSelectedIdentity().Matches(Row->Item->Identity))
	{
		CommitRow(Row);
	}
	if (Mode == EProfileNavigatorMode::Pinned && OnItemDoubleClicked.IsBound())
	{
		OnItemDoubleClicked.Execute(Row->Item->Identity);
	}
}

void SProfileNavigatorPanel::HandleQueryChanged(const FText& Text)
{
	if (!ResultModel.IsValid()) return;
	ResultModel->SetQuery(Text.ToString());
	ConstructedRowCount = 0;
	ConstructedPreviewCount = 0;
	if (ListView.IsValid())
	{
		ListView->RequestListRefresh();
		RestoreListSelection();
	}
	if (OnQueryChanged.IsBound()) OnQueryChanged.Execute(ResultModel->GetQuery());
}

FReply SProfileNavigatorPanel::HandleSearchKeyDown(const FGeometry& /*Geometry*/, const FKeyEvent& KeyEvent)
{
	return HandleNavigationKey(KeyEvent);
}

void SProfileNavigatorPanel::RestoreListSelection()
{
	if (!ListView.IsValid() || !ResultModel.IsValid()) return;
	ListView->ClearSelection();
	const FProfileItemIdentity SelectedIdentity = Source.IsValid()
		? Source->GetSelectedIdentity() : FProfileItemIdentity();
	for (const TSharedPtr<FProfilePickerDisplayRow>& Row : ResultModel->GetRows())
	{
		if (Row.IsValid() && Row->Item.IsValid() && Row->Item->Identity.Matches(SelectedIdentity))
		{
			ListView->SetSelection(Row, ESelectInfo::Direct);
			return;
		}
	}
}

FReply SProfileNavigatorPanel::HandleNavigationKey(const FKeyEvent& KeyEvent)
{
	if (!ListView.IsValid() || !ResultModel.IsValid()) return FReply::Unhandled();
	if (KeyEvent.GetKey() == EKeys::Escape)
	{
		if (Mode == EProfileNavigatorMode::Popup)
		{
			RequestDismiss();
			return FReply::Handled();
		}
		return FReply::Unhandled();
	}
	if (KeyEvent.GetKey() == EKeys::Enter)
	{
		const TArray<TSharedPtr<FProfilePickerDisplayRow>> Selected = ListView->GetSelectedItems();
		return Selected.Num() > 0 && CommitRow(Selected[0]) ? FReply::Handled() : FReply::Unhandled();
	}
	if (KeyEvent.GetKey() != EKeys::Up && KeyEvent.GetKey() != EKeys::Down)
	{
		return FReply::Unhandled(); // printable keys remain owned by the focused search field
	}

	TArray<TSharedPtr<FProfilePickerDisplayRow>> Selectable;
	for (const TSharedPtr<FProfilePickerDisplayRow>& Row : ResultModel->GetRows())
	{
		if (Row.IsValid() && Row->Kind == FProfilePickerDisplayRow::EKind::Item && Row->Item.IsValid())
		{
			Selectable.Add(Row);
		}
	}
	if (Selectable.IsEmpty()) return FReply::Handled();
	const TArray<TSharedPtr<FProfilePickerDisplayRow>> Selected = ListView->GetSelectedItems();
	int32 Current = Selected.IsEmpty() ? INDEX_NONE : Selectable.IndexOfByKey(Selected[0]);
	const int32 Direction = KeyEvent.GetKey() == EKeys::Down ? 1 : -1;
	const int32 Next = Current == INDEX_NONE
		? (Direction > 0 ? 0 : Selectable.Num() - 1)
		: FMath::Clamp(Current + Direction, 0, Selectable.Num() - 1);
	ListView->SetSelection(Selectable[Next], ESelectInfo::Direct);
	ListView->RequestScrollIntoView(Selectable[Next]);
	return FReply::Handled();
}

bool SProfileNavigatorPanel::CommitRow(const TSharedPtr<FProfilePickerDisplayRow>& Row)
{
	if (bCommittingSelection || !Source.IsValid() || !Row.IsValid()
		|| Row->Kind != FProfilePickerDisplayRow::EKind::Item || !Row->Item.IsValid())
	{
		return false;
	}
	bool bSelected = false;
	{
		TGuardValue<bool> Guard(bCommittingSelection, true);
		bSelected = Source->SelectItem(Row->Item->Identity);
		if (bSelected)
		{
			FProfilePickerCatalogStore::Get().AddRecent(Source->GetLogicalCatalogScope(), Row->Item->Identity);
		}
	}
	if (!bSelected)
	{
		bRefreshDeferred = false;
		RefreshResults(); // prune a row deleted between menu-open and activation
		return false;
	}
	if (bRefreshDeferred)
	{
		bRefreshDeferred = false;
		RefreshResults(); // coalesce provider + recent-store notifications into one rebuild
	}
	if (Mode == EProfileNavigatorMode::Popup)
	{
		RequestDismiss();
	}
	else if (ListView.IsValid())
	{
		ListView->RequestListRefresh();
	}
	return true;
}

void SProfileNavigatorPanel::RequestDismiss()
{
	if (bDismissRequested) return;
	bDismissRequested = true;
	if (OnDismissed.IsBound()) OnDismissed.Execute();
}

FText SProfileNavigatorPanel::GetEmptyStateText() const
{
	if (!ResultModel.IsValid() || !ResultModel->HasAnySourceItems())
	{
		return LOCTEXT("NoItems", "No items are available. The current selection was not changed.");
	}
	return FText::Format(LOCTEXT("NoMatches", "No items match “{0}”.\nClear the search to keep browsing; the current selection is unchanged."),
		FText::FromString(ResultModel->GetQuery()));
}

FReply SProfileNavigatorPanel::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (Paper2DPlusEditor::SlateShortcutUtils::ShouldIgnoreShortcutForFocusedWidget())
	{
		return SCompoundWidget::OnKeyDown(MyGeometry, InKeyEvent);
	}
	const FReply NavigationReply = HandleNavigationKey(InKeyEvent);
	return NavigationReply.IsEventHandled() ? NavigationReply : SCompoundWidget::OnKeyDown(MyGeometry, InKeyEvent);
}

#undef LOCTEXT_NAMESPACE
