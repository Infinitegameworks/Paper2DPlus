// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Paper2DPlusValidationService.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"

class SSearchBox;
class STextBlock;
class FTransactionObjectEvent;
class IAssetRegistry;
struct FAssetData;
struct FSlateBrush;
struct FPropertyChangedEvent;

/** Mutation-free, worldless filtering/selection model used by every validation panel host. */
class PAPER2DPLUSEDITOR_API FPaper2DPlusValidationPanelModel
{
public:
	void SetIssues(const TArray<FPaper2DPlusValidationIssue>& InIssues);
	void SetSearchText(const FString& InSearchText);
	void SetSeverityVisible(EPaper2DPlusValidationSeverity Severity, bool bVisible);
	bool IsSeverityVisible(EPaper2DPlusValidationSeverity Severity) const;

	void SelectIssue(const FString& StableKey);
	const FString& GetSelectedStableKey() const { return SelectedStableKey; }

	const TArray<FPaper2DPlusValidationIssue>& GetIssues() const { return Issues; }
	const TArray<FPaper2DPlusValidationIssue>& GetFilteredIssues() const { return FilteredIssues; }
	FPaper2DPlusValidationSummary GetSummary(bool bValidating = false) const;

private:
	void Refilter();
	bool PassesFilter(const FPaper2DPlusValidationIssue& Issue) const;

	TArray<FPaper2DPlusValidationIssue> Issues;
	TArray<FPaper2DPlusValidationIssue> FilteredIssues;
	FString SearchText;
	FString SelectedStableKey;
	bool bShowInfo = true;
	bool bShowWarnings = true;
	bool bShowErrors = true;
};

DECLARE_DELEGATE_OneParam(
	FOnPaper2DPlusValidationIssueActivated,
	const FPaper2DPlusValidationIssue&);

/**
 * Optional host-owned validation run. Return true when OutIssues is authoritative; returning false
 * asks the panel to discard it and use the shared validation service instead.
 */
DECLARE_DELEGATE_RetVal_OneParam(
	bool,
	FOnPaper2DPlusCustomValidationRun,
	TArray<FPaper2DPlusValidationIssue>&);

/** Small text-first summary suitable for toolbars and profile cards. */
class PAPER2DPLUSEDITOR_API SProfileValidationSummary : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SProfileValidationSummary) {}
		SLATE_ATTRIBUTE(FPaper2DPlusValidationSummary, Summary)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	FText GetSummaryText() const;
	FSlateColor GetSummaryColor() const;
	const FSlateBrush* GetSummaryIcon() const;
	TAttribute<FPaper2DPlusValidationSummary> SummaryAttribute;
};

/**
 * Virtualized, filterable issue list. Validation is explicit or deferred after observed changes; it is
 * never executed from paint/layout callbacks. Hosts own navigation through OnIssueActivated.
 */
class PAPER2DPLUSEDITOR_API SProfileValidationPanel : public SCompoundWidget
{
public:
	using FIssuePtr = TSharedPtr<FPaper2DPlusValidationIssue>;

	SLATE_BEGIN_ARGS(SProfileValidationPanel)
		: _RunInitially(true)
		, _RefreshOnObservedChanges(true)
	{}
		SLATE_ATTRIBUTE(UObject*, Asset)
		SLATE_EVENT(FOnPaper2DPlusValidationIssueActivated, OnIssueActivated)
		SLATE_EVENT(FOnPaper2DPlusCustomValidationRun, OnCustomValidationRun)
		SLATE_ARGUMENT(FText, RunActionText)
		SLATE_ARGUMENT(bool, RunInitially)
		SLATE_ARGUMENT(bool, RefreshOnObservedChanges)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SProfileValidationPanel() override;

	/** Explicit validation action (action button and, by default, initial construction). */
	void RunValidationNow();

	/** Coalesces repeated undo/property/registry notifications into one active-timer refresh. */
	void RequestRefresh();

	const FPaper2DPlusValidationPanelModel& GetModel() const { return Model; }
	const FText& GetRunActionText() const { return RunActionText; }

#if WITH_DEV_AUTOMATION_TESTS
	void FlushPendingRefreshForTests();
	int32 GetValidationRunCountForTests() const { return ValidationRunCount; }
	FText GetEmptyStateTextForTests() const { return GetEmptyStateText(); }
	bool ActivateIssueForTests(const FPaper2DPlusValidationIssue& Issue);
	/** Drives the same filtered registry-event seam as the live delegates and reports whether it scheduled. */
	bool NotifyAssetRegistryChangedForTests(const FAssetData& AssetData, const FString& OldObjectPath = FString());
	/** Simulates Asset Registry discovery completion, which resolves prior Unknown path states. */
	void NotifyAssetRegistryFilesLoadedForTests();
#endif

private:
	TSharedRef<ITableRow> GenerateIssueRow(FIssuePtr Issue, const TSharedRef<STableViewBase>& OwnerTable);
	void RebuildVisibleRows();
	void HandleSearchChanged(const FText& NewText);
	void HandleSeverityChanged(EPaper2DPlusValidationSeverity Severity, ECheckBoxState NewState);
	void HandleSelectionChanged(FIssuePtr Issue, ESelectInfo::Type SelectInfo);
	void HandleIssueDoubleClicked(FIssuePtr Issue);
	FReply HandleRefreshClicked();
	FReply HandleActivateClicked(FIssuePtr Issue);
	bool ActivateIssue(const FPaper2DPlusValidationIssue& Issue);

	EActiveTimerReturnType HandleDeferredRefresh(double CurrentTime, float DeltaTime);
	void HandleObjectPropertyChanged(UObject* Object, FPropertyChangedEvent& Event);
	void HandleObjectModified(UObject* Object);
	void HandleObjectTransacted(UObject* Object, const FTransactionObjectEvent& Event);
	void HandleAdaptersChanged();
	void HandleAssetRegistryChanged(const FAssetData& AssetData);
	void HandleAssetRegistryRenamed(const FAssetData& AssetData, const FString& OldObjectPath);
	void HandleAssetRegistryFilesLoaded();
	bool ObservesObject(const UObject* Object) const;
	bool ShouldRefreshForAssetRegistryEvent(const FAssetData& AssetData, const FString& OldObjectPath) const;

	FText GetEmptyStateText() const;
	FPaper2DPlusValidationSummary GetSummary() const;
	EVisibility GetEmptyStateVisibility() const;
	EVisibility GetListVisibility() const;

	TAttribute<UObject*> AssetAttribute;
	FOnPaper2DPlusValidationIssueActivated OnIssueActivated;
	FOnPaper2DPlusCustomValidationRun OnCustomValidationRun;
	FText RunActionText;
	FPaper2DPlusValidationPanelModel Model;
	TArray<FIssuePtr> VisibleRows;
	TSharedPtr<SListView<FIssuePtr>> IssueList;

	FDelegateHandle PropertyChangedHandle;
	FDelegateHandle ObjectModifiedHandle;
	FDelegateHandle ObjectTransactedHandle;
	FDelegateHandle AdaptersChangedHandle;
	FDelegateHandle AssetAddedHandle;
	FDelegateHandle AssetRemovedHandle;
	FDelegateHandle AssetUpdatedHandle;
	FDelegateHandle AssetRenamedHandle;
	FDelegateHandle AssetFilesLoadedHandle;
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
	FDelegateHandle AssetKnownGathersCompleteHandle;
#endif
	IAssetRegistry* BoundAssetRegistry = nullptr;
	bool bRefreshPending = false;
	bool bValidating = false;
	bool bHasAdapter = false;
	bool bHasRunValidation = false;
	int32 ValidationRunCount = 0;
};
