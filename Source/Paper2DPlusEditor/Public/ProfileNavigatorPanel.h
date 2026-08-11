// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ProfileItemPicker.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/STableViewBase.h"

template <typename ItemType> class SListView;
class SSearchBox;
class ITableRow;
class STableViewBase;

enum class EProfileNavigatorMode : uint8
{
	Popup,
	Pinned
};

DECLARE_DELEGATE(FOnProfileNavigatorDismissed);
DECLARE_DELEGATE_OneParam(FOnProfileNavigatorQueryChanged, const FString&);
DECLARE_DELEGATE_OneParam(FOnProfileNavigatorItemDoubleClicked, const FProfileItemIdentity&);
DECLARE_DELEGATE_RetVal_OneParam(TSharedRef<SWidget>, FOnGenerateProfileNavigatorItemExtension, const FProfileItemIdentity&);
DECLARE_DELEGATE_RetVal_TwoParams(TSharedRef<SWidget>, FOnGenerateProfileNavigatorItemContent,
	const FProfileItemIdentity&, const TSharedRef<SWidget>&);

/** Shared virtualized result surface used by compact popups and pinned navigators. */
class PAPER2DPLUSEDITOR_API SProfileNavigatorPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SProfileNavigatorPanel)
		: _Mode(EProfileNavigatorMode::Pinned)
	{}
		SLATE_ARGUMENT(TSharedPtr<IProfileItemPickerSource>, Source)
		SLATE_ARGUMENT(EProfileNavigatorMode, Mode)
		SLATE_ARGUMENT(FString, InitialQuery)
		SLATE_EVENT(FOnProfileNavigatorDismissed, OnDismissed)
		SLATE_EVENT(FOnProfileNavigatorQueryChanged, OnQueryChanged)
		SLATE_EVENT(FOnProfileNavigatorItemDoubleClicked, OnItemDoubleClicked)
		SLATE_EVENT(FOnGenerateProfileNavigatorItemPreview, OnGenerateItemPreview)
		SLATE_EVENT(FOnGenerateProfileNavigatorItemExtension, OnGenerateItemExtension)
		/** Optional domain wrapper around the standard row body; invoked only for virtualized item rows. */
		SLATE_EVENT(FOnGenerateProfileNavigatorItemContent, OnGenerateItemContent)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SProfileNavigatorPanel() override;
	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

	void RefreshResults();
	void SetQueryForTests(const FString& Query);
	int32 GetConstructedRowCountForTests() const { return ConstructedRowCount; }
	int32 GetConstructedPreviewCountForTests() const { return ConstructedPreviewCount; }
	int32 GetResultCountForTests() const;
	int32 GenerateRowsForViewportForTests(const FVector2D& ViewportSize);
	bool SelectIdentityForTests(const FProfileItemIdentity& Identity);
	bool DoubleClickIdentityForTests(const FProfileItemIdentity& Identity);

private:
	TSharedRef<ITableRow> GenerateRow(TSharedPtr<FProfilePickerDisplayRow> Row,
		const TSharedRef<STableViewBase>& OwnerTable);
	void HandleSelectionChanged(TSharedPtr<FProfilePickerDisplayRow> Row, ESelectInfo::Type SelectInfo);
	void HandleDoubleClick(TSharedPtr<FProfilePickerDisplayRow> Row);
	void HandleQueryChanged(const FText& Text);
	FReply HandleSearchKeyDown(const FGeometry& Geometry, const FKeyEvent& KeyEvent);
	FReply HandleNavigationKey(const FKeyEvent& KeyEvent);
	void RestoreListSelection();
	bool CommitRow(const TSharedPtr<FProfilePickerDisplayRow>& Row);
	void RequestDismiss();
	FText GetEmptyStateText() const;

	TSharedPtr<IProfileItemPickerSource> Source;
	TSharedPtr<FProfilePickerResultModel> ResultModel;
	TSharedPtr<SSearchBox> SearchBox;
	TSharedPtr<SListView<TSharedPtr<FProfilePickerDisplayRow>>> ListView;
	EProfileNavigatorMode Mode = EProfileNavigatorMode::Pinned;
	FOnProfileNavigatorDismissed OnDismissed;
	FOnProfileNavigatorQueryChanged OnQueryChanged;
	FOnProfileNavigatorItemDoubleClicked OnItemDoubleClicked;
	FOnGenerateProfileNavigatorItemPreview OnGenerateItemPreview;
	FOnGenerateProfileNavigatorItemExtension OnGenerateItemExtension;
	FOnGenerateProfileNavigatorItemContent OnGenerateItemContent;
	FDelegateHandle SourceChangedHandle;
	FDelegateHandle StoreChangedHandle;
	bool bCommittingSelection = false;
	bool bRefreshingResults = false;
	bool bRefreshDeferred = false;
	bool bDismissRequested = false;
	int32 ConstructedRowCount = 0;
	int32 ConstructedPreviewCount = 0;
};
