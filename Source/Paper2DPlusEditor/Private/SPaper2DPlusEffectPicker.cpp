// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "SPaper2DPlusEffectPicker.h"

#include "EffectProfileContextResolver.h"
#include "Framework/Application/SlateApplication.h"
#include "InputCoreTypes.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "Paper2DPlusEffectPicker"

namespace
{
	FText DescribeEffectRowDiagnostic(EPaper2DPlusEffectRowDiagnostic Diagnostic)
	{
		switch (Diagnostic)
		{
		case EPaper2DPlusEffectRowDiagnostic::Eligible:
			return LOCTEXT("EligibleRow", "Eligible");
		case EPaper2DPlusEffectRowDiagnostic::FilterMismatch:
			return LOCTEXT("FilterMismatchRow", "Does not match this field's filters");
		case EPaper2DPlusEffectRowDiagnostic::SearchMismatch:
			return LOCTEXT("SearchMismatchRow", "Current value does not match the search");
		case EPaper2DPlusEffectRowDiagnostic::OutsideCharacterEffects:
			return LOCTEXT("OutsideCharacterRow", "Outside Character Effects");
		case EPaper2DPlusEffectRowDiagnostic::OutsideAllProjectEffects:
			return LOCTEXT("OutsideProjectRow", "Not authored in a discovered Effect Profile");
		case EPaper2DPlusEffectRowDiagnostic::MembershipUnknown:
			return LOCTEXT("UnknownMembershipRow", "Project discovery is still in progress");
		default:
			return LOCTEXT("UnknownEffectRow", "Unavailable");
		}
	}
}

void FPaper2DPlusEffectPickerModel::SetFilters(
	FGameplayTag InTypeFilter,
	const FGameplayTagContainer& InDescriptorFilters)
{
	TypeFilter = InTypeFilter;
	DescriptorFilters = InDescriptorFilters;
}

FPaper2DPlusEffectLibraryQuery FPaper2DPlusEffectPickerModel::MakeQuery() const
{
	FPaper2DPlusEffectLibraryQuery Query;
	Query.TypeFilter = TypeFilter;
	Query.DescriptorFilters = DescriptorFilters;
	Query.SearchText = SearchText;
	Query.CurrentValue = CurrentValue;
	return Query;
}

uint64 FPaper2DPlusEffectPickerModel::BeginRefresh(uint64 IndexGeneration)
{
	ExpectedIndexGeneration = IndexGeneration;
	return ++RefreshRequestGeneration;
}

bool FPaper2DPlusEffectPickerModel::ApplyResult(
	uint64 RequestGeneration,
	uint64 CurrentIndexGeneration,
	FPaper2DPlusEffectLibraryQueryResult InResult)
{
	if (RequestGeneration != RefreshRequestGeneration
		|| CurrentIndexGeneration != ExpectedIndexGeneration
		|| InResult.Generation != ExpectedIndexGeneration)
	{
		return false;
	}

	const FSoftObjectPath PreviousFocus = FocusedPath;
	Result = MoveTemp(InResult);
	Result.Scope = Scope;
	if (!PreviousFocus.IsNull() && Result.FindRow(PreviousFocus))
	{
		FocusedPath = PreviousFocus;
	}
	else if (!CurrentValue.IsNull() && Result.FindRow(CurrentValue))
	{
		FocusedPath = CurrentValue;
	}
	else
	{
		const FPaper2DPlusEffectLibraryRow* FirstEligible = Result.Rows.FindByPredicate(
			[](const FPaper2DPlusEffectLibraryRow& Row) { return Row.bEligible; });
		FocusedPath = FirstEligible
			? FirstEligible->FlipbookPath
			: (Result.Rows.IsEmpty() ? FSoftObjectPath() : Result.Rows[0].FlipbookPath);
	}
	return true;
}

bool FPaper2DPlusEffectPickerModel::TryCommit(
	EPaper2DPlusEffectCommitGesture Gesture,
	const FOnPaper2DPlusEffectCommitted& OnCommitted)
{
	FSoftObjectPath Target;
	switch (Gesture)
	{
	case EPaper2DPlusEffectCommitGesture::UseSelected:
	case EPaper2DPlusEffectCommitGesture::DoubleClick:
	case EPaper2DPlusEffectCommitGesture::Enter:
		Target = FocusedPath;
		if (!CanCommitFocused())
		{
			return false;
		}
		break;
	case EPaper2DPlusEffectCommitGesture::Clear:
		break;
	default:
		return false;
	}

	if (FPaper2DPlusEffectLibraryIndex::NormalizePath(Target)
		== FPaper2DPlusEffectLibraryIndex::NormalizePath(CurrentValue))
	{
		return false;
	}
	if (!OnCommitted.IsBound())
	{
		return false;
	}

	// The host owns the live property/default and may fail (asset disappeared, reinstance, stale
	// handle). Stage local truth before the callback because an accepted property write is allowed to
	// synchronously rebuild/close its Details host; do not touch this model after a successful call.
	const FSoftObjectPath PreviousValue = CurrentValue;
	CurrentValue = Target;
	if (!OnCommitted.Execute(Target))
	{
		CurrentValue = PreviousValue;
		return false;
	}
	return true;
}

bool FPaper2DPlusEffectPickerModel::CanCommitFocused() const
{
	if (FocusedPath.IsNull())
	{
		return false;
	}
	const FPaper2DPlusEffectLibraryRow* Row = Result.FindRow(FocusedPath);
	return Row && Row->bEligible;
}

bool FPaper2DPlusEffectPickerModel::IsFocusedCurrent() const
{
	return FPaper2DPlusEffectLibraryIndex::NormalizePath(FocusedPath)
		== FPaper2DPlusEffectLibraryIndex::NormalizePath(CurrentValue);
}

void SPaper2DPlusEffectPicker::Construct(const FArguments& InArgs)
{
	ContextResolver = InArgs._ContextResolver;
	LibraryIndex = InArgs._LibraryIndex.IsValid()
		? InArgs._LibraryIndex
		: (ContextResolver.IsValid() && ContextResolver->GetLibraryIndex().IsValid()
			? ContextResolver->GetLibraryIndex()
			: FPaper2DPlusEffectLibraryIndex::Get());
	OnCommitted = InArgs._OnCommitted;
	OnPreviewed = InArgs._OnPreviewed;
	OnDismissed = InArgs._OnDismissed;
	OnReturnFocus = InArgs._OnReturnFocus;
	Model.SetCurrentValue(InArgs._CurrentValue);
	Model.SetFilters(InArgs._TypeFilter, InArgs._DescriptorFilters);

	if (LibraryIndex.IsValid())
	{
		IndexInvalidatedHandle = LibraryIndex->OnInvalidated().AddSP(
			this, &SPaper2DPlusEffectPicker::HandleIndexInvalidated);
	}
	if (ContextResolver.IsValid())
	{
		ContextInvalidatedHandle = ContextResolver->OnInvalidated().AddSP(
			this, &SPaper2DPlusEffectPicker::HandleContextInvalidated);
	}

	ChildSlot
	[
		SNew(SBorder)
		.Padding(8.0f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					SNew(SButton)
					.Text(this, &SPaper2DPlusEffectPicker::GetScopeButtonText,
						EPaper2DPlusEffectPickerScope::CharacterEffects)
					.ToolTipText(LOCTEXT("CharacterScopeTip", "Show authored entries from this Character's Catalog-assigned Effect Profile."))
					.OnClicked(this, &SPaper2DPlusEffectPicker::ChooseScope,
						EPaper2DPlusEffectPickerScope::CharacterEffects)
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.Padding(4.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(SButton)
					.Text(this, &SPaper2DPlusEffectPicker::GetScopeButtonText,
						EPaper2DPlusEffectPickerScope::AllProjectEffects)
					.ToolTipText(LOCTEXT("ProjectScopeTip", "Explicitly discover authored entries from every Effect Profile in the project."))
					.OnClicked(this, &SPaper2DPlusEffectPicker::ChooseScope,
						EPaper2DPlusEffectPickerScope::AllProjectEffects)
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 6.0f, 0.0f, 0.0f)
			[
				SAssignNew(SearchBox, SSearchBox)
				.HintText(LOCTEXT("SearchHint", "Search Flipbook, label, Type, or Descriptor"))
				.AccessibleText(LOCTEXT(
					"SearchAccessible",
					"Search Paper2DPlus Effects. Use Up and Down to preview, Enter to choose, and Escape to cancel."))
				.OnTextChanged(this, &SPaper2DPlusEffectPicker::HandleSearchChanged)
				.OnTextCommitted(this, &SPaper2DPlusEffectPicker::HandleSearchCommitted)
				.OnKeyDownHandler(this, &SPaper2DPlusEffectPicker::HandleSearchKeyDown)
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 6.0f, 0.0f, 4.0f)
			[
				SNew(STextBlock)
				.Text(this, &SPaper2DPlusEffectPicker::GetStatusText)
				.AutoWrapText(true)
			]
			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			[
				SNew(SBox)
				.MinDesiredWidth(440.0f)
				.MinDesiredHeight(240.0f)
				[
					SAssignNew(ListView, SListView<FRowItem>)
					.ListItemsSource(&RowItems)
					.SelectionMode(ESelectionMode::Single)
					.OnGenerateRow(this, &SPaper2DPlusEffectPicker::GenerateRow)
					.OnSelectionChanged(this, &SPaper2DPlusEffectPicker::HandleSelectionChanged)
					.OnMouseButtonDoubleClick(this, &SPaper2DPlusEffectPicker::HandleDoubleClick)
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 6.0f, 0.0f, 0.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SButton)
					.Text(LOCTEXT("Retry", "Retry"))
					.ToolTipText(LOCTEXT("RetryTip", "Retry the assigned library or failed project libraries without discarding successful rows."))
					.Visibility(this, &SPaper2DPlusEffectPicker::GetRetryVisibility)
					.OnClicked(this, &SPaper2DPlusEffectPicker::Retry)
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					SNew(SBox)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SButton)
					.Text(LOCTEXT("Clear", "Clear"))
					.ToolTipText(LOCTEXT("ClearTip", "Explicitly store no Effect Flipbook."))
					.IsEnabled(this, &SPaper2DPlusEffectPicker::CanClear)
					.OnClicked(this, &SPaper2DPlusEffectPicker::ClearValue)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(6.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(SButton)
					.Text(LOCTEXT("UseSelected", "Use Selected"))
					.ToolTipText(LOCTEXT("UseSelectedTip", "Commit the focused exact Flipbook. Single-click and arrow navigation preview only."))
					.IsEnabled(this, &SPaper2DPlusEffectPicker::CanUseSelected)
					.OnClicked(this, &SPaper2DPlusEffectPicker::UseSelected)
				]
			]
		]
	];

	// Widget construction is the explicit user-open boundary. Present a truthful resolving state now,
	// then perform the first demand load on a one-shot active timer so the popup can paint/focus first.
	if (LibraryIndex.IsValid())
	{
		const uint64 IndexGeneration = LibraryIndex->GetGeneration();
		const uint64 RequestGeneration = Model.BeginRefresh(IndexGeneration);
		Model.ApplyResult(
			RequestGeneration,
			IndexGeneration,
			FPaper2DPlusEffectLibraryIndex::BuildCharacterResult(
				EPaper2DPlusEffectCharacterSourceState::Resolving,
				nullptr,
				Model.MakeQuery(),
				IndexGeneration));
		RebuildRows();
		QueueRefresh();
	}
	RegisterActiveTimer(0.0f, FWidgetActiveTimerDelegate::CreateLambda(
		[WeakSearch = TWeakPtr<SSearchBox>(SearchBox)](double, float)
		{
			if (const TSharedPtr<SSearchBox> PinnedSearch = WeakSearch.Pin())
			{
				FSlateApplication::Get().SetKeyboardFocus(PinnedSearch, EFocusCause::SetDirectly);
			}
			return EActiveTimerReturnType::Stop;
		}));
}

SPaper2DPlusEffectPicker::~SPaper2DPlusEffectPicker()
{
	if (LibraryIndex.IsValid() && IndexInvalidatedHandle.IsValid())
	{
		LibraryIndex->OnInvalidated().Remove(IndexInvalidatedHandle);
	}
	if (ContextResolver.IsValid() && ContextInvalidatedHandle.IsValid())
	{
		ContextResolver->OnInvalidated().Remove(ContextInvalidatedHandle);
	}
}

FReply SPaper2DPlusEffectPicker::OnKeyDown(
	const FGeometry& MyGeometry,
	const FKeyEvent& InKeyEvent)
{
	const FReply NavigationReply = HandleSearchKeyDown(MyGeometry, InKeyEvent);
	return NavigationReply.IsEventHandled()
		? NavigationReply
		: SCompoundWidget::OnKeyDown(MyGeometry, InKeyEvent);
}

void SPaper2DPlusEffectPicker::Refresh(bool bRetryFailedLibrariesOnly)
{
	if (bDismissed || !LibraryIndex.IsValid())
	{
		return;
	}

	const bool bRetainCoherentRows = bRetainRowsOnNextRefresh;
	bRetainRowsOnNextRefresh = false;
	if (Model.GetScope() == EPaper2DPlusEffectPickerScope::AllProjectEffects
		&& bRetainCoherentRows
		&& !Model.GetResult().Rows.IsEmpty())
	{
		RetainedProjectRefreshResult = Model.GetResult();
	}
	else if (Model.GetScope() != EPaper2DPlusEffectPickerScope::AllProjectEffects)
	{
		RetainedProjectRefreshResult.Reset();
	}

	const uint64 ExpectedIndexGeneration = LibraryIndex->GetGeneration();
	const uint64 RequestGeneration = Model.BeginRefresh(ExpectedIndexGeneration);
	const FPaper2DPlusEffectLibraryQuery Query = Model.MakeQuery();
	FPaper2DPlusEffectLibraryQueryResult Result;
	if (Model.GetScope() == EPaper2DPlusEffectPickerScope::CharacterEffects)
	{
		ProjectSession.Reset();
		if (ContextResolver.IsValid())
		{
			Result = LibraryIndex->OpenCharacterQuery(ContextResolver->ResolveForPicker(), Query);
		}
		else
		{
			Result = FPaper2DPlusEffectLibraryIndex::BuildCharacterResult(
				EPaper2DPlusEffectCharacterSourceState::MissingContextOrAssignment,
				nullptr,
				Query,
				ExpectedIndexGeneration);
		}
	}
	else
	{
		// Successful cached libraries survive Retry; a fresh staged session retries only paths that do
		// not already have coherent snapshots and exposes progress between bounded batches.
		(void)bRetryFailedLibrariesOnly;
		FPaper2DPlusEffectProjectQuerySession Session;
		Result = LibraryIndex->BeginAllProjectQuery(Query, Session);
		ProjectSession = MoveTemp(Session);
	}
	if (RetainedProjectRefreshResult.IsSet() && !Result.bComplete)
	{
		Result = FPaper2DPlusEffectLibraryIndex::MakeRefreshingResult(
			RetainedProjectRefreshResult.GetValue(),
			ExpectedIndexGeneration);
		Result.Scope = EPaper2DPlusEffectPickerScope::AllProjectEffects;
		Result.bCanRetry = false;
	}
	else if (Result.bComplete)
	{
		RetainedProjectRefreshResult.Reset();
	}

	if (!Model.ApplyResult(
		RequestGeneration,
		LibraryIndex->GetGeneration(),
		MoveTemp(Result)))
	{
		QueueRefresh();
		return;
	}
	RebuildRows();
	if (ProjectSession.IsSet() && ProjectSession->HasPendingLibraries())
	{
		QueueProjectLoad();
	}
}

void SPaper2DPlusEffectPicker::DismissWithoutCommit()
{
	if (bDismissed)
	{
		return;
	}
	Model.DismissWithoutCommit();
	FinishDismissal();
}

TSharedRef<ITableRow> SPaper2DPlusEffectPicker::GenerateRow(
	FRowItem Item,
	const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(STableRow<FRowItem>, OwnerTable)
		.ToolTipText(GetRowTooltip(Item))
		[
			SNew(STextBlock)
			.Text(GetRowText(Item))
			.Margin(FMargin(4.0f, 3.0f))
		];
}

void SPaper2DPlusEffectPicker::HandleSelectionChanged(
	FRowItem Item,
	ESelectInfo::Type SelectInfo)
{
	CommitError = FText::GetEmpty();
	Model.SetFocusedPath(Item.IsValid() ? Item->FlipbookPath : FSoftObjectPath());
	if (Item.IsValid() && SelectInfo != ESelectInfo::Direct && OnPreviewed.IsBound())
	{
		PreviewedPath = Item->FlipbookPath;
		OnPreviewed.Execute(Item->FlipbookPath);
	}
}

void SPaper2DPlusEffectPicker::HandleDoubleClick(FRowItem Item)
{
	if (Item.IsValid())
	{
		Model.SetFocusedPath(Item->FlipbookPath);
		CommitAndDismiss(EPaper2DPlusEffectCommitGesture::DoubleClick);
	}
}

void SPaper2DPlusEffectPicker::HandleSearchChanged(const FText& SearchText)
{
	CommitError = FText::GetEmpty();
	RetainedProjectRefreshResult.Reset();
	Model.SetSearchText(SearchText.ToString());
	QueueRefresh();
}

void SPaper2DPlusEffectPicker::HandleSearchCommitted(
	const FText& SearchText,
	ETextCommit::Type CommitType)
{
	Model.SetSearchText(SearchText.ToString());
	if (CommitType == ETextCommit::OnEnter)
	{
		Refresh();
		CommitAndDismiss(EPaper2DPlusEffectCommitGesture::Enter);
	}
}

FReply SPaper2DPlusEffectPicker::HandleSearchKeyDown(
	const FGeometry& /*Geometry*/,
	const FKeyEvent& KeyEvent)
{
	const FKey Key = KeyEvent.GetKey();
	if (Key == EKeys::Escape)
	{
		DismissWithoutCommit();
		return FReply::Handled();
	}
	if (Key == EKeys::Enter || Key == EKeys::Up || Key == EKeys::Down)
	{
		// SSearchBox delays change notifications while typing. Read its live text before every result-
		// dependent key so fast type+Enter/Up/Down gestures cannot use the previous result set.
		bool bNeedsImmediateRefresh = false;
		if (SearchBox.IsValid())
		{
			const FString LiveSearchText = SearchBox->GetText().ToString();
			if (LiveSearchText != Model.GetSearchText())
			{
				RetainedProjectRefreshResult.Reset();
				Model.SetSearchText(LiveSearchText);
				bNeedsImmediateRefresh = true;
			}
		}
		if (const TSharedPtr<FActiveTimerHandle> PendingRefresh = RefreshTimerHandle.Pin())
		{
			UnRegisterActiveTimer(PendingRefresh.ToSharedRef());
			RefreshTimerHandle.Reset();
			bNeedsImmediateRefresh = true;
		}
		if (bNeedsImmediateRefresh)
		{
			Refresh();
		}
	}
	if (Key == EKeys::Enter)
	{
		CommitAndDismiss(EPaper2DPlusEffectCommitGesture::Enter);
		return FReply::Handled();
	}
	if (Key == EKeys::Up || Key == EKeys::Down)
	{
		MoveFocus(Key == EKeys::Down ? 1 : -1);
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

FReply SPaper2DPlusEffectPicker::ChooseScope(EPaper2DPlusEffectPickerScope Scope)
{
	if (Model.GetScope() != Scope)
	{
		CommitError = FText::GetEmpty();
		RetainedProjectRefreshResult.Reset();
		Model.SetScope(Scope);
		Refresh();
	}
	return FReply::Handled();
}

FReply SPaper2DPlusEffectPicker::UseSelected()
{
	CommitAndDismiss(EPaper2DPlusEffectCommitGesture::UseSelected);
	return FReply::Handled();
}

FReply SPaper2DPlusEffectPicker::ClearValue()
{
	CommitAndDismiss(EPaper2DPlusEffectCommitGesture::Clear);
	return FReply::Handled();
}

FReply SPaper2DPlusEffectPicker::Retry()
{
	CommitError = FText::GetEmpty();
	bRetainRowsOnNextRefresh = true;
	Refresh(Model.GetScope() == EPaper2DPlusEffectPickerScope::AllProjectEffects);
	return FReply::Handled();
}

bool SPaper2DPlusEffectPicker::CanUseSelected() const
{
	return Model.CanCommitFocused();
}

bool SPaper2DPlusEffectPicker::CanClear() const
{
	return !Model.GetCurrentValue().IsNull();
}

EVisibility SPaper2DPlusEffectPicker::GetRetryVisibility() const
{
	return Model.GetResult().bCanRetry ? EVisibility::Visible : EVisibility::Collapsed;
}

FText SPaper2DPlusEffectPicker::GetScopeButtonText(
	EPaper2DPlusEffectPickerScope Scope) const
{
	const FText Label = Scope == EPaper2DPlusEffectPickerScope::CharacterEffects
		? LOCTEXT("CharacterEffects", "Character Effects")
		: LOCTEXT("AllProjectEffects", "All Project Effects");
	return Model.GetScope() == Scope
		? FText::Format(LOCTEXT("ActiveScope", "{0} (Active)"), Label)
		: Label;
}

FText SPaper2DPlusEffectPicker::GetStatusText() const
{
	return CommitError.IsEmpty() ? Model.GetResult().StatusText : CommitError;
}

FText SPaper2DPlusEffectPicker::GetRowText(FRowItem Item) const
{
	if (!Item.IsValid())
	{
		return FText::GetEmpty();
	}
	if (Item->bPinnedCurrent || !Item->bEligible)
	{
		return FText::Format(
			Item->bPinnedCurrent
				? LOCTEXT("CurrentDiagnosticRow", "{0} (Current · {1})")
				: LOCTEXT("DiagnosticRow", "{0} ({1})"),
			Item->DisplayName,
			DescribeEffectRowDiagnostic(Item->Diagnostic));
	}
	return Item->DisplayName;
}

FText SPaper2DPlusEffectPicker::GetRowTooltip(FRowItem Item) const
{
	if (!Item.IsValid())
	{
		return FText::GetEmpty();
	}
	return FText::Format(
		LOCTEXT("RowTooltip", "Flipbook: {0}\nSource Effect Profile: {1}\nType: {2}\nDescriptors: {3}\nStatus: {4}"),
		FText::FromString(Item->FlipbookPath.ToString()),
		Item->SourceProfilePath.IsNull()
			? LOCTEXT("PinnedSource", "Pinned exact current value")
			: FText::FromString(Item->SourceProfilePath.ToString()),
		FText::FromString(Item->TypeTag.ToString()),
		FText::FromString(Item->DescriptorTags.ToStringSimple()),
		DescribeEffectRowDiagnostic(Item->Diagnostic));
}

void SPaper2DPlusEffectPicker::RebuildRows()
{
	RowItems.Reset(Model.GetResult().Rows.Num());
	for (const FPaper2DPlusEffectLibraryRow& Row : Model.GetResult().Rows)
	{
		RowItems.Add(MakeShared<FPaper2DPlusEffectLibraryRow>(Row));
	}
	if (!ListView.IsValid())
	{
		return;
	}
	ListView->RequestListRefresh();
	const FString FocusKey = FPaper2DPlusEffectLibraryIndex::NormalizePath(Model.GetFocusedPath());
	const FRowItem* FocusedItem = RowItems.FindByPredicate(
		[&FocusKey](const FRowItem& Item)
		{
			return Item.IsValid() && Item->NormalizedFlipbookPath == FocusKey;
		});
	const bool bPreviewStillEligible = FocusedItem && (*FocusedItem).IsValid()
		&& (*FocusedItem)->bEligible
		&& FPaper2DPlusEffectLibraryIndex::NormalizePath(PreviewedPath) == FocusKey;
	if (!PreviewedPath.IsNull() && !bPreviewStillEligible)
	{
		PreviewedPath.Reset();
		OnPreviewed.ExecuteIfBound(FSoftObjectPath());
	}
	if (FocusedItem)
	{
		ListView->SetSelection(*FocusedItem, ESelectInfo::Direct);
	}
	else
	{
		ListView->ClearSelection();
	}
}

void SPaper2DPlusEffectPicker::MoveFocus(int32 Direction)
{
	if (!ListView.IsValid() || RowItems.IsEmpty() || Direction == 0)
	{
		return;
	}
	const FString FocusKey = FPaper2DPlusEffectLibraryIndex::NormalizePath(
		Model.GetFocusedPath());
	const int32 CurrentIndex = RowItems.IndexOfByPredicate(
		[&FocusKey](const FRowItem& Item)
		{
			return Item.IsValid() && Item->NormalizedFlipbookPath == FocusKey;
		});
	const int32 NextIndex = CurrentIndex == INDEX_NONE
		? (Direction > 0 ? 0 : RowItems.Num() - 1)
		: FMath::Clamp(CurrentIndex + Direction, 0, RowItems.Num() - 1);
	if (RowItems.IsValidIndex(NextIndex))
	{
		ListView->SetSelection(RowItems[NextIndex], ESelectInfo::OnKeyPress);
		ListView->RequestScrollIntoView(RowItems[NextIndex]);
	}
}

void SPaper2DPlusEffectPicker::CommitAndDismiss(
	EPaper2DPlusEffectCommitGesture Gesture)
{
	if (bDismissed)
	{
		return;
	}
	// A successful live property write may synchronously rebuild Details and destroy the menu that
	// owns this picker. Retain the callback receiver through dismissal before invoking that boundary.
	const TSharedRef<SPaper2DPlusEffectPicker> KeepAlive = SharedThis(this);
	(void)KeepAlive;
	const bool bNoOp = Gesture == EPaper2DPlusEffectCommitGesture::Clear
		? Model.GetCurrentValue().IsNull()
		: Model.CanCommitFocused() && Model.IsFocusedCurrent();
	if (Gesture != EPaper2DPlusEffectCommitGesture::Clear
		&& !Model.CanCommitFocused()
		&& !bNoOp)
	{
		return;
	}
	CommitError = FText::GetEmpty();
	if (Model.TryCommit(Gesture, OnCommitted) || bNoOp)
	{
		FinishDismissal();
		return;
	}
	CommitError = LOCTEXT(
		"CommitRejected",
		"That Flipbook could not be applied because it was removed or the live Cue field changed. Refresh and choose again.");
}

void SPaper2DPlusEffectPicker::ApplyResultForTests(
	FPaper2DPlusEffectLibraryQueryResult InResult)
{
	// Construct queues the real first refresh so a production popup can paint before discovery.
	// A test-injected result is already the authoritative completion for this seam; leaving that
	// timer alive lets the first keyboard gesture replace the fixture rows with the real index.
	if (const TSharedPtr<FActiveTimerHandle> PendingRefresh = RefreshTimerHandle.Pin())
	{
		UnRegisterActiveTimer(PendingRefresh.ToSharedRef());
		RefreshTimerHandle.Reset();
	}
	const uint64 Generation = InResult.Generation;
	const uint64 RequestGeneration = Model.BeginRefresh(Generation);
	if (Model.ApplyResult(RequestGeneration, Generation, MoveTemp(InResult)))
	{
		RebuildRows();
	}
}

void SPaper2DPlusEffectPicker::FinishDismissal()
{
	if (bDismissed)
	{
		return;
	}
	bDismissed = true;
	if (!PreviewedPath.IsNull())
	{
		PreviewedPath.Reset();
		OnPreviewed.ExecuteIfBound(FSoftObjectPath());
	}
	const FSimpleDelegate Dismissed = OnDismissed;
	const FSimpleDelegate ReturnFocus = OnReturnFocus;
	Dismissed.ExecuteIfBound();
	ReturnFocus.ExecuteIfBound();
}

void SPaper2DPlusEffectPicker::QueueRefresh()
{
	if (bDismissed || RefreshTimerHandle.IsValid())
	{
		return;
	}
	RefreshTimerHandle = RegisterActiveTimer(
		0.0f,
		FWidgetActiveTimerDelegate::CreateSP(
			this, &SPaper2DPlusEffectPicker::HandleQueuedRefresh));
}

EActiveTimerReturnType SPaper2DPlusEffectPicker::HandleQueuedRefresh(
	double CurrentTime,
	float DeltaTime)
{
	RefreshTimerHandle.Reset();
	if (!bDismissed)
	{
		Refresh();
	}
	return EActiveTimerReturnType::Stop;
}

void SPaper2DPlusEffectPicker::QueueProjectLoad()
{
	if (bDismissed || ProjectLoadTimerHandle.IsValid())
	{
		return;
	}
	ProjectLoadTimerHandle = RegisterActiveTimer(
		0.0f,
		FWidgetActiveTimerDelegate::CreateSP(
			this, &SPaper2DPlusEffectPicker::HandleProjectLoad));
}

EActiveTimerReturnType SPaper2DPlusEffectPicker::HandleProjectLoad(
	double CurrentTime,
	float DeltaTime)
{
	ProjectLoadTimerHandle.Reset();
	if (bDismissed
		|| !LibraryIndex.IsValid()
		|| Model.GetScope() != EPaper2DPlusEffectPickerScope::AllProjectEffects
		|| !ProjectSession.IsSet())
	{
		return EActiveTimerReturnType::Stop;
	}
	if (!LibraryIndex->IsGenerationCurrent(ProjectSession->Generation))
	{
		ProjectSession.Reset();
		QueueRefresh();
		return EActiveTimerReturnType::Stop;
	}

	const uint64 RequestGeneration = Model.BeginRefresh(LibraryIndex->GetGeneration());
	FPaper2DPlusEffectLibraryQueryResult Result =
		LibraryIndex->AdvanceAllProjectQuery(ProjectSession.GetValue(), 1);
	if (RetainedProjectRefreshResult.IsSet() && !Result.bComplete)
	{
		Result = FPaper2DPlusEffectLibraryIndex::MakeRefreshingResult(
			RetainedProjectRefreshResult.GetValue(),
			LibraryIndex->GetGeneration());
		Result.Scope = EPaper2DPlusEffectPickerScope::AllProjectEffects;
		Result.bCanRetry = false;
	}
	else if (Result.bComplete)
	{
		RetainedProjectRefreshResult.Reset();
	}
	if (!Model.ApplyResult(
		RequestGeneration,
		LibraryIndex->GetGeneration(),
		MoveTemp(Result)))
	{
		ProjectSession.Reset();
		QueueRefresh();
		return EActiveTimerReturnType::Stop;
	}
	RebuildRows();
	if (ProjectSession->HasPendingLibraries())
	{
		QueueProjectLoad();
	}
	return EActiveTimerReturnType::Stop;
}

void SPaper2DPlusEffectPicker::HandleIndexInvalidated(
	const FPaper2DPlusEffectLibraryChange& Change)
{
	bRetainRowsOnNextRefresh = true;
	QueueRefresh();
}

void SPaper2DPlusEffectPicker::HandleContextInvalidated()
{
	bRetainRowsOnNextRefresh = true;
	QueueRefresh();
}

#undef LOCTEXT_NAMESPACE
