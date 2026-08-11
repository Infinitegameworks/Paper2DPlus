// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "FrameDataPanel.h"
#include "CharacterProfileEditorModel.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Views/SHeaderRow.h"
#include "Widgets/Views/STableRow.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Styling/AppStyle.h"

/** SFrameDataPanel — read-only sortable fighting-game frame-data table (TASK-15). Rows come from the
 *  single shared FPaper2DPlusFrameData::ComputeAllMoveFrameData; the panel mutates nothing. */

#define LOCTEXT_NAMESPACE "FrameDataPanel"

const FName SFrameDataPanel::Col_Flipbook(TEXT("Flipbook"));
const FName SFrameDataPanel::Col_KeyFrames(TEXT("KeyFrames"));
const FName SFrameDataPanel::Col_DurationFrames(TEXT("DurationFrames"));
const FName SFrameDataPanel::Col_DurationMs(TEXT("DurationMs"));
const FName SFrameDataPanel::Col_Active(TEXT("Active"));
const FName SFrameDataPanel::Col_IFrames(TEXT("IFrames"));
const FName SFrameDataPanel::Col_Damage(TEXT("Damage"));
const FName SFrameDataPanel::Col_Knockback(TEXT("Knockback"));
const FName SFrameDataPanel::Col_Reach(TEXT("Reach"));
const FName SFrameDataPanel::Col_RootMotion(TEXT("RootMotion"));
const FName SFrameDataPanel::Col_Phase(TEXT("Phase"));
const FName SFrameDataPanel::Col_CancelWindows(TEXT("CancelWindows"));
const FName SFrameDataPanel::Col_Tags(TEXT("Tags")); // TASK-108 U6 (R9)

namespace
{
	FText FrameDataPanel_PhaseText(EAnimationPhase Phase)
	{
		switch (Phase)
		{
			case EAnimationPhase::Startup:  return LOCTEXT("PhaseStartup", "Startup");
			case EAnimationPhase::Active:   return LOCTEXT("PhaseActive", "Active");
			case EAnimationPhase::Recovery: return LOCTEXT("PhaseRecovery", "Recovery");
			default:                        return LOCTEXT("PhaseNone", "None");
		}
	}
}

/** One read-only row of the frame-data table. */
class SFrameDataRow : public SMultiColumnTableRow<TSharedPtr<FPaper2DPlusMoveFrameData>>
{
public:
	SLATE_BEGIN_ARGS(SFrameDataRow) {}
		SLATE_ARGUMENT(TSharedPtr<FPaper2DPlusMoveFrameData>, Item)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& OwnerTable)
	{
		Item = InArgs._Item;
		SMultiColumnTableRow<TSharedPtr<FPaper2DPlusMoveFrameData>>::Construct(FSuperRowType::FArguments(), OwnerTable);
	}

	virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnId) override
	{
		const FPaper2DPlusMoveFrameData& D = *Item;
		FText CellText;

		if (ColumnId == SFrameDataPanel::Col_Flipbook)            CellText = FText::FromString(D.FlipbookName);
		else if (ColumnId == SFrameDataPanel::Col_KeyFrames)      CellText = FText::AsNumber(D.TotalKeyFrames);
		else if (ColumnId == SFrameDataPanel::Col_DurationFrames) CellText = FText::AsNumber(D.TotalDurationFrames);
		else if (ColumnId == SFrameDataPanel::Col_DurationMs)     CellText = FText::AsNumber(FMath::RoundToInt(D.TotalDurationMs));
		else if (ColumnId == SFrameDataPanel::Col_Active)         CellText = FText::AsNumber(D.ActiveFrames);
		else if (ColumnId == SFrameDataPanel::Col_IFrames)        CellText = FText::AsNumber(D.IFrameCount);
		else if (ColumnId == SFrameDataPanel::Col_Damage)         CellText = FText::AsNumber(D.MaxDamage);
		else if (ColumnId == SFrameDataPanel::Col_Knockback)      CellText = FText::AsNumber(D.MaxKnockback);
		else if (ColumnId == SFrameDataPanel::Col_Reach)          CellText = FText::AsNumber(FMath::RoundToInt(D.MaxReach));
		else if (ColumnId == SFrameDataPanel::Col_RootMotion)     CellText = D.bHasRootMotion ? LOCTEXT("Yes", "Yes") : LOCTEXT("No", "—");
		else if (ColumnId == SFrameDataPanel::Col_Phase)          CellText = FrameDataPanel_PhaseText(D.Phase);
		else if (ColumnId == SFrameDataPanel::Col_CancelWindows)  CellText = FText::AsNumber(D.CancelWindowCount);
		else if (ColumnId == SFrameDataPanel::Col_Tags)           CellText = FText::FromString(D.AnimationTags);

		const bool bLeftAlign = (ColumnId == SFrameDataPanel::Col_Flipbook
			|| ColumnId == SFrameDataPanel::Col_Phase || ColumnId == SFrameDataPanel::Col_RootMotion
			|| ColumnId == SFrameDataPanel::Col_Tags);

		return SNew(SBox)
			.Padding(FMargin(6.f, 2.f))
			.HAlign(bLeftAlign ? HAlign_Left : HAlign_Right)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text(CellText)
			];
	}

private:
	TSharedPtr<FPaper2DPlusMoveFrameData> Item;
};

void SFrameDataPanel::Construct(const FArguments& InArgs)
{
	Model = InArgs._Model;
	SortColumnId = Col_Flipbook;
	SortMode = EColumnSortMode::Ascending;

	HeaderRow = SNew(SHeaderRow);

	auto AddColumn = [this](const FName Id, const FText& Label, float Width, EHorizontalAlignment HAlign)
	{
		HeaderRow->AddColumn(
			SHeaderRow::Column(Id)
			.DefaultLabel(Label)
			.FillWidth(Width)
			.HAlignHeader(HAlign)
			.HAlignCell(HAlign)
			.SortMode(this, &SFrameDataPanel::GetSortModeForColumn, Id)
			.OnSort(this, &SFrameDataPanel::OnColumnSort));
	};

	AddColumn(Col_Flipbook,       LOCTEXT("ColFlipbook", "Move"),        2.0f, HAlign_Left);
	AddColumn(Col_KeyFrames,      LOCTEXT("ColKeyFrames", "Frames"),     1.0f, HAlign_Right);
	AddColumn(Col_DurationFrames, LOCTEXT("ColDurFrames", "Dur (f)"),    1.0f, HAlign_Right);
	AddColumn(Col_DurationMs,     LOCTEXT("ColDurMs", "Dur (ms)"),       1.0f, HAlign_Right);
	AddColumn(Col_Active,         LOCTEXT("ColActive", "Active"),        1.0f, HAlign_Right);
	AddColumn(Col_IFrames,        LOCTEXT("ColIFrames", "I-Frames"),     1.0f, HAlign_Right);
	AddColumn(Col_Damage,         LOCTEXT("ColDamage", "Max Dmg"),       1.0f, HAlign_Right);
	AddColumn(Col_Knockback,      LOCTEXT("ColKnockback", "Max KB"),     1.0f, HAlign_Right);
	AddColumn(Col_Reach,          LOCTEXT("ColReach", "Reach"),          1.0f, HAlign_Right);
	AddColumn(Col_RootMotion,     LOCTEXT("ColRootMotion", "Root Mtn"),  1.0f, HAlign_Left);
	AddColumn(Col_Phase,          LOCTEXT("ColPhase", "Phase"),          1.0f, HAlign_Left);
	AddColumn(Col_CancelWindows,  LOCTEXT("ColCancel", "Cancels"),       1.0f, HAlign_Right);
	AddColumn(Col_Tags,           LOCTEXT("ColTags", "Tags"),            2.0f, HAlign_Left); // R9 — appended LAST

	ChildSlot
	[
		SNew(SVerticalBox)

		// Header toolbar
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.f)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("FrameDataTitle", "Frame Data (read-only — computed from existing data)"))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
				.ToolTipText(LOCTEXT("CopyCsvTooltip", "Copy the full table as CSV to the clipboard"))
				.OnClicked(this, &SFrameDataPanel::OnCopyCsvClicked)
				[
					SNew(STextBlock).Text(LOCTEXT("CopyCsv", "Copy CSV"))
				]
			]
		]

		// The table
		+ SVerticalBox::Slot()
		.FillHeight(1.f)
		[
			SAssignNew(ListView, SListView<FRowPtr>)
			.ListItemsSource(&Rows)
			.SelectionMode(ESelectionMode::Single)
			.OnGenerateRow(this, &SFrameDataPanel::OnGenerateRow)
			.HeaderRow(HeaderRow)
		]
	];

	if (Model.IsValid())
	{
		// Refresh ONLY on data changes — the table is selection-independent. Binding to OnFlipbookSelectionChanged
		// would rebuild every row (re-resolving each soft flipbook ref via LoadSynchronous) on every flipbook click
		// anywhere in the editor, for no visible change. (Review finding.)
		DataChangedHandle = Model->OnAssetDataChanged.AddSP(this, &SFrameDataPanel::RefreshRows);
		ExternalModifiedHandle = Model->OnAssetExternallyModified.AddSP(this, &SFrameDataPanel::RefreshRows);
	}

	RefreshRows();
}

SFrameDataPanel::~SFrameDataPanel()
{
	if (Model.IsValid())
	{
		Model->OnAssetDataChanged.Remove(DataChangedHandle);
		Model->OnAssetExternallyModified.Remove(ExternalModifiedHandle);
	}
}

void SFrameDataPanel::RefreshRows()
{
	Rows.Reset();

	UPaper2DPlusCharacterProfileAsset* Asset = Model.IsValid() ? Model->GetAsset() : nullptr;
	if (Asset)
	{
		TArray<FPaper2DPlusMoveFrameData> Computed;
		FPaper2DPlusFrameData::ComputeAllMoveFrameData(Asset, Computed);
		Rows.Reserve(Computed.Num());
		for (const FPaper2DPlusMoveFrameData& Row : Computed)
		{
			Rows.Add(MakeShared<FPaper2DPlusMoveFrameData>(Row));
		}
	}

	SortRows();

	if (ListView.IsValid())
	{
		ListView->RequestListRefresh();
	}
}

TSharedRef<ITableRow> SFrameDataPanel::OnGenerateRow(FRowPtr Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(SFrameDataRow, OwnerTable).Item(Item);
}

EColumnSortMode::Type SFrameDataPanel::GetSortModeForColumn(const FName ColumnId) const
{
	return (ColumnId == SortColumnId) ? SortMode : EColumnSortMode::None;
}

void SFrameDataPanel::OnColumnSort(EColumnSortPriority::Type SortPriority, const FName& ColumnId, EColumnSortMode::Type NewSortMode)
{
	SortColumnId = ColumnId;
	SortMode = NewSortMode;
	SortRows();
	if (ListView.IsValid())
	{
		ListView->RequestListRefresh();
	}
}

void SFrameDataPanel::SortRows()
{
	if (SortMode == EColumnSortMode::None || SortColumnId.IsNone())
	{
		return;
	}

	const bool bAscending = (SortMode == EColumnSortMode::Ascending);
	const FName Col = SortColumnId;

	Rows.Sort([Col, bAscending](const FRowPtr& A, const FRowPtr& B)
	{
		const FPaper2DPlusMoveFrameData& L = *A;
		const FPaper2DPlusMoveFrameData& R = *B;

		// Primary key, then a stable secondary key on FlipbookName so tie-heavy columns (Root Motion, I-Frames,
		// Cancel Windows) don't shuffle arbitrarily between clicks (TArray::Sort is not stable).
		auto Cmp = [bAscending, &L, &R](auto LV, auto RV) -> bool
		{
			if (LV < RV) return bAscending;
			if (RV < LV) return !bAscending;
			return L.FlipbookName < R.FlipbookName;
		};

		if (Col == Col_Flipbook)            return bAscending ? (L.FlipbookName < R.FlipbookName) : (R.FlipbookName < L.FlipbookName);
		if (Col == Col_KeyFrames)           return Cmp(L.TotalKeyFrames, R.TotalKeyFrames);
		if (Col == Col_DurationFrames)      return Cmp(L.TotalDurationFrames, R.TotalDurationFrames);
		if (Col == Col_DurationMs)          return Cmp(L.TotalDurationMs, R.TotalDurationMs);
		if (Col == Col_Active)              return Cmp(L.ActiveFrames, R.ActiveFrames);
		if (Col == Col_IFrames)             return Cmp(L.IFrameCount, R.IFrameCount);
		if (Col == Col_Damage)              return Cmp(L.MaxDamage, R.MaxDamage);
		if (Col == Col_Knockback)           return Cmp(L.MaxKnockback, R.MaxKnockback);
		if (Col == Col_Reach)               return Cmp(L.MaxReach, R.MaxReach);
		if (Col == Col_RootMotion)          return Cmp((int32)L.bHasRootMotion, (int32)R.bHasRootMotion);
		if (Col == Col_Phase)               return Cmp((uint8)L.Phase, (uint8)R.Phase);
		if (Col == Col_CancelWindows)       return Cmp(L.CancelWindowCount, R.CancelWindowCount);
		if (Col == Col_Tags)
		{
			if (L.AnimationTags != R.AnimationTags) return bAscending ? (L.AnimationTags < R.AnimationTags) : (R.AnimationTags < L.AnimationTags);
			return L.FlipbookName < R.FlipbookName; // stable tiebreak (many moves share a tag set)
		}
		return false;
	});
}

FReply SFrameDataPanel::OnCopyCsvClicked()
{
	UPaper2DPlusCharacterProfileAsset* Asset = Model.IsValid() ? Model->GetAsset() : nullptr;
	if (Asset)
	{
		TArray<FPaper2DPlusMoveFrameData> Computed;
		FPaper2DPlusFrameData::ComputeAllMoveFrameData(Asset, Computed);
		const FString Csv = FPaper2DPlusFrameData::ExportFrameDataToCsv(Computed);
		FPlatformApplicationMisc::ClipboardCopy(*Csv);
	}
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
