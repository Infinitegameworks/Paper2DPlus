// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"
#include "Paper2DPlusFrameData.h"

class FCharacterProfileEditorModel;
class ITableRow;
class STableViewBase;
template <typename ItemType> class SListView;
class SHeaderRow;

/**
 * Read-only fighting-game frame-data summary (TASK-15). Hosted in a floating window opened from the
 * Flipbooks sidebar's "Frame Data…" button via FCharacterProfileEditorModel::OpenFrameDataWindow()
 * (it was a dockable tab until layout _v8; the `framedata` console alias opens the window now).
 *
 * One row per flipbook/move with the columns of FPaper2DPlusMoveFrameData (Startup/Active/Recovery
 * via Phase, Total Duration, Active/I-Frame counts, Max Damage/Knockback/Reach, Has Root Motion,
 * Cancel Window count). Sortable on every column header.
 *
 * The panel MUTATES NOTHING — it is a pure read-out, so it opens no transaction. Rows are produced
 * by FPaper2DPlusFrameData::ComputeAllMoveFrameData(Model->GetAsset()), the SAME single source of
 * truth used by the BP query and the CSV/JSON export. It refreshes on the model's OnAssetDataChanged
 * / OnAssetExternallyModified / OnFlipbookSelectionChanged delegates (data-only -> rebuild rows).
 */
class PAPER2DPLUSEDITOR_API SFrameDataPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SFrameDataPanel) {}
		SLATE_ARGUMENT(TSharedPtr<FCharacterProfileEditorModel>, Model)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SFrameDataPanel();

	/** Recompute rows from the asset and refresh the list. */
	void RefreshRows();

private:
	using FRowPtr = TSharedPtr<FPaper2DPlusMoveFrameData>;

	TSharedRef<ITableRow> OnGenerateRow(FRowPtr Item, const TSharedRef<STableViewBase>& OwnerTable);

	EColumnSortMode::Type GetSortModeForColumn(const FName ColumnId) const;
	void OnColumnSort(EColumnSortPriority::Type SortPriority, const FName& ColumnId, EColumnSortMode::Type NewSortMode);
	void SortRows();

	/** Export the current rows to a clipboard-friendly CSV (toolbar "Copy CSV" button). */
	FReply OnCopyCsvClicked();

	TSharedPtr<FCharacterProfileEditorModel> Model;

	TArray<FRowPtr> Rows;
	TSharedPtr<SListView<FRowPtr>> ListView;
	TSharedPtr<SHeaderRow> HeaderRow;

	FName SortColumnId;
	EColumnSortMode::Type SortMode = EColumnSortMode::None;

	FDelegateHandle DataChangedHandle;
	FDelegateHandle ExternalModifiedHandle;

public:
	// Column identifiers (also used as the sort keys). PUBLIC so the row widget (SFrameDataRow, a separate
	// SMultiColumnTableRow class) can map a ColumnId to the matching field in GenerateWidgetForColumn.
	static const FName Col_Flipbook;
	static const FName Col_KeyFrames;
	static const FName Col_DurationFrames;
	static const FName Col_DurationMs;
	static const FName Col_Active;
	static const FName Col_IFrames;
	static const FName Col_Damage;
	static const FName Col_Knockback;
	static const FName Col_Reach;
	static const FName Col_RootMotion;
	static const FName Col_Phase;
	static const FName Col_CancelWindows;
	static const FName Col_Tags; // TASK-108 U6 (R9): effective animation tags (semicolon-separated)
};
