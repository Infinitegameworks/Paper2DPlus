// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Input/Reply.h"
#include "Paper2DPlusEffectLibraryIndex.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"

class FActiveTimerHandle;
class FEffectProfileContextResolver;
class SSearchBox;

/** Host returns true only after the exact live property/default accepted the write. */
DECLARE_DELEGATE_RetVal_OneParam(bool, FOnPaper2DPlusEffectCommitted, const FSoftObjectPath&);
DECLARE_DELEGATE_OneParam(FOnPaper2DPlusEffectPreviewed, const FSoftObjectPath&);

enum class EPaper2DPlusEffectCommitGesture : uint8
{
	UseSelected,
	DoubleClick,
	Enter,
	Clear
};

/** Pure per-picker state. It owns no assets and has no mutation path except TryCommit's delegate. */
class PAPER2DPLUSEDITOR_API FPaper2DPlusEffectPickerModel
{
public:
	void SetScope(EPaper2DPlusEffectPickerScope InScope) { Scope = InScope; }
	EPaper2DPlusEffectPickerScope GetScope() const { return Scope; }

	void SetSearchText(const FString& InSearchText) { SearchText = InSearchText; }
	const FString& GetSearchText() const { return SearchText; }

	void SetFilters(FGameplayTag InTypeFilter, const FGameplayTagContainer& InDescriptorFilters);
	void SetCurrentValue(const FSoftObjectPath& InCurrentValue) { CurrentValue = InCurrentValue; }
	const FSoftObjectPath& GetCurrentValue() const { return CurrentValue; }
	void SetFocusedPath(const FSoftObjectPath& InFocusedPath) { FocusedPath = InFocusedPath; }
	const FSoftObjectPath& GetFocusedPath() const { return FocusedPath; }
	bool CanCommitFocused() const;
	bool IsFocusedCurrent() const;

	FPaper2DPlusEffectLibraryQuery MakeQuery() const;
	uint64 BeginRefresh(uint64 IndexGeneration);
	bool ApplyResult(
		uint64 RequestGeneration,
		uint64 CurrentIndexGeneration,
		FPaper2DPlusEffectLibraryQueryResult InResult);
	const FPaper2DPlusEffectLibraryQueryResult& GetResult() const { return Result; }

	/** Only these four explicit gestures can execute the write delegate. */
	bool TryCommit(
		EPaper2DPlusEffectCommitGesture Gesture,
		const FOnPaper2DPlusEffectCommitted& OnCommitted);
	void DismissWithoutCommit() {}

private:
	EPaper2DPlusEffectPickerScope Scope = EPaper2DPlusEffectPickerScope::CharacterEffects;
	FGameplayTag TypeFilter;
	FGameplayTagContainer DescriptorFilters;
	FString SearchText;
	FSoftObjectPath CurrentValue;
	FSoftObjectPath FocusedPath;
	FPaper2DPlusEffectLibraryQueryResult Result;
	uint64 RefreshRequestGeneration = 0;
	uint64 ExpectedIndexGeneration = 0;
};

/**
 * Shared Paper2DPlus Effect picker surface.
 *
 * Construct is the explicit open boundary: only then may the supplied index discover/load Effect
 * Profiles. Hosts own the live property handle and supply the commit delegate. Outside-popover close
 * must call DismissWithoutCommit so cancellation and focus return remain identical across hosts.
 */
class PAPER2DPLUSEDITOR_API SPaper2DPlusEffectPicker : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SPaper2DPlusEffectPicker) {}
		SLATE_ARGUMENT(TSharedPtr<FEffectProfileContextResolver>, ContextResolver)
		SLATE_ARGUMENT(TSharedPtr<FPaper2DPlusEffectLibraryIndex>, LibraryIndex)
		SLATE_ARGUMENT(FSoftObjectPath, CurrentValue)
		SLATE_ARGUMENT(FGameplayTag, TypeFilter)
		SLATE_ARGUMENT(FGameplayTagContainer, DescriptorFilters)
		SLATE_EVENT(FOnPaper2DPlusEffectCommitted, OnCommitted)
		SLATE_EVENT(FOnPaper2DPlusEffectPreviewed, OnPreviewed)
		SLATE_EVENT(FSimpleDelegate, OnDismissed)
		SLATE_EVENT(FSimpleDelegate, OnReturnFocus)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SPaper2DPlusEffectPicker() override;

	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

	void Refresh(bool bRetryFailedLibrariesOnly = false);
	void DismissWithoutCommit();
	const FPaper2DPlusEffectPickerModel& GetModelForTests() const { return Model; }
	TSharedPtr<SSearchBox> GetSearchBoxForTests() const { return SearchBox; }
	const FText& GetCommitErrorForTests() const { return CommitError; }
	void ApplyResultForTests(FPaper2DPlusEffectLibraryQueryResult InResult);
	FReply HandleSearchKeyDownForTests(const FKeyEvent& KeyEvent)
	{
		return HandleSearchKeyDown(FGeometry(), KeyEvent);
	}

private:
	using FRowItem = TSharedPtr<FPaper2DPlusEffectLibraryRow>;

	TSharedRef<ITableRow> GenerateRow(FRowItem Item, const TSharedRef<STableViewBase>& OwnerTable);
	void HandleSelectionChanged(FRowItem Item, ESelectInfo::Type SelectInfo);
	void HandleDoubleClick(FRowItem Item);
	void HandleSearchChanged(const FText& SearchText);
	void HandleSearchCommitted(const FText& SearchText, ETextCommit::Type CommitType);
	FReply HandleSearchKeyDown(const FGeometry& Geometry, const FKeyEvent& KeyEvent);
	FReply ChooseScope(EPaper2DPlusEffectPickerScope Scope);
	FReply UseSelected();
	FReply ClearValue();
	FReply Retry();
	bool CanUseSelected() const;
	bool CanClear() const;
	EVisibility GetRetryVisibility() const;
	FText GetScopeButtonText(EPaper2DPlusEffectPickerScope Scope) const;
	FText GetStatusText() const;
	FText GetRowText(FRowItem Item) const;
	FText GetRowTooltip(FRowItem Item) const;
	void RebuildRows();
	void MoveFocus(int32 Direction);
	void CommitAndDismiss(EPaper2DPlusEffectCommitGesture Gesture);
	void FinishDismissal();
	void QueueRefresh();
	EActiveTimerReturnType HandleQueuedRefresh(double CurrentTime, float DeltaTime);
	void QueueProjectLoad();
	EActiveTimerReturnType HandleProjectLoad(double CurrentTime, float DeltaTime);
	void HandleIndexInvalidated(const FPaper2DPlusEffectLibraryChange& Change);
	void HandleContextInvalidated();

	TSharedPtr<FEffectProfileContextResolver> ContextResolver;
	TSharedPtr<FPaper2DPlusEffectLibraryIndex> LibraryIndex;
	FPaper2DPlusEffectPickerModel Model;
	TArray<FRowItem> RowItems;
	TSharedPtr<SListView<FRowItem>> ListView;
	TSharedPtr<SSearchBox> SearchBox;
	FOnPaper2DPlusEffectCommitted OnCommitted;
	FOnPaper2DPlusEffectPreviewed OnPreviewed;
	FSimpleDelegate OnDismissed;
	FSimpleDelegate OnReturnFocus;
	FDelegateHandle IndexInvalidatedHandle;
	FDelegateHandle ContextInvalidatedHandle;
	TWeakPtr<FActiveTimerHandle> RefreshTimerHandle;
	TWeakPtr<FActiveTimerHandle> ProjectLoadTimerHandle;
	TOptional<FPaper2DPlusEffectProjectQuerySession> ProjectSession;
	TOptional<FPaper2DPlusEffectLibraryQueryResult> RetainedProjectRefreshResult;
	FSoftObjectPath PreviewedPath;
	FText CommitError;
	bool bRetainRowsOnNextRefresh = false;
	bool bDismissed = false;
};
