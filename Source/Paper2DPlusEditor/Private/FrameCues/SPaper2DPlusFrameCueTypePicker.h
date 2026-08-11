// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FrameCues/Paper2DPlusFrameCueTypeAuthoring.h"
#include "Widgets/SCompoundWidget.h"

class FActiveTimerHandle;
class ITableRow;
class SSearchBox;
class STableViewBase;
class UBlueprint;
template <typename ItemType> class SListView;

/**
 * One + Add Cue row: either a placeable Cue Type, or a rejected one shown for repair.
 *
 * Rejected types used to be dropped from the list entirely, which is what made a freshly created
 * Cue Type look like it had simply failed to exist. They are listed now, disabled, carrying the
 * reason they cannot be placed.
 */
struct FPaper2DPlusFrameCueTypePickerItem
{
	FPaper2DPlusFrameCueTypeDescriptor Type;
	FString SearchText;
	/** False for every rejected row. A false row can never produce a placement. */
	bool bPlaceable = false;
	/** True when clicking the row can open something the designer can actually fix. */
	bool bRepairable = false;
	/** Designer-facing reason a rejected row cannot be placed. Empty on placeable rows. */
	FText Reason;
};

namespace Paper2DPlusFrameCueTypePicker
{
	/** The most severe diagnostic on a rejected type, or a readable fallback for its availability. */
	FText ResolveRejectionReason(const FPaper2DPlusFrameCueTypeDescriptor& Type);

	/**
	 * True when a rejected type names an authoring problem worth showing a designer.
	 *
	 * Discovery rejects two very different things. One is an asset in a fixable state — uncompiled,
	 * unsaved, quarantined, or unloadable — and hiding those is what made a brand-new Cue Type look
	 * like it had failed to exist. The other is structural: the abstract Moment/Range bases
	 * themselves, and the skeleton/reinstancing/trash classes every loaded Cue Type Blueprint drags
	 * along. Those are not assets, cannot be repaired, and would bury the rows that matter.
	 */
	bool IsSurfacedRejection(const FPaper2DPlusFrameCueTypeDescriptor& Type);

	/** The Blueprint a repair click opens, or null when the row names nothing repairable. */
	UBlueprint* ResolveRepairTarget(const FPaper2DPlusFrameCueTypeDescriptor& Type);

	/** Placeable rows first (discovery order), then rejected repair rows. Pure. */
	TArray<TSharedPtr<FPaper2DPlusFrameCueTypePickerItem>> BuildItems(
		const FPaper2DPlusFrameCueTypeDiscoveryResult& Discovery);

	/**
	 * Opens a rejected Cue Type in the restricted Frame Cue Type editor — the repair path the
	 * diagnostic row promises. Returns true only when an editor was actually opened.
	 */
	bool OpenForRepair(const FPaper2DPlusFrameCueTypeDescriptor& Type);
}

/**
 * The + Add Cue Cue-Type picker.
 *
 * Lives in its own file rather than inside the Frame Cues tab: it owns live re-discovery, filtering,
 * keyboard commit, and now a second row family (rejected types with a repair click-through), and the
 * tab file is already far past the repo's split threshold.
 */
class SPaper2DPlusFrameCueTypePicker final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SPaper2DPlusFrameCueTypePicker) {}
		SLATE_ARGUMENT(TFunction<void(UClass*)>, OnPickType)
		SLATE_ARGUMENT(TFunction<void()>, OnCreateType)
		SLATE_ARGUMENT(TFunction<void(const FPaper2DPlusFrameCueTypeDescriptor&)>, OnRepairType)
		SLATE_ARGUMENT(TWeakPtr<SWidget>, FocusReturnTarget)
	SLATE_END_ARGS()

	virtual ~SPaper2DPlusFrameCueTypePicker() override;

	void Construct(const FArguments& InArgs);

	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& KeyEvent) override;

	/** Rows currently visible, in list order. Placeable rows first, then repair rows. */
	const TArray<TSharedPtr<FPaper2DPlusFrameCueTypePickerItem>>& GetFilteredItemsForTests() const
	{
		return FilteredItems;
	}
	/** Give the virtualized list a real viewport so automation can inspect generated row widgets. */
	int32 GenerateRowsForViewportForTests(const FVector2D& ViewportSize);
	FText GetStatusTextForTests() const { return GetStatusText(); }
	/**
	 * True only while the picker has no row to speak for itself. The diagnostic rows are the
	 * discovery-friction surface; the message is an empty state, not a standing status line.
	 */
	bool IsEmptyStateVisibleForTests() const { return GetEmptyStateVisibility() == EVisibility::Visible; }
	void SetSearchTextForTests(const FText& Text) { SetSearchText(Text); }
	FReply ActivateRowForTests(TSharedPtr<FPaper2DPlusFrameCueTypePickerItem> Item)
	{
		return HandleRowActivated(Item);
	}

private:
	void RefreshDiscovery(bool bForce);
	void ApplyFilter();
	void SetSearchText(const FText& Text);
	TSharedRef<ITableRow> GenerateRow(
		TSharedPtr<FPaper2DPlusFrameCueTypePickerItem> Item,
		const TSharedRef<STableViewBase>& OwnerTable);
	FReply HandleRowActivated(TSharedPtr<FPaper2DPlusFrameCueTypePickerItem> Item);
	FReply PickType(TSharedPtr<FPaper2DPlusFrameCueTypePickerItem> Item);
	FReply RepairType(TSharedPtr<FPaper2DPlusFrameCueTypePickerItem> Item);
	FReply CreateType();
	FReply HandlePickerKeyDown(const FGeometry& Geometry, const FKeyEvent& KeyEvent);
	FReply RetryDiscovery();
	EVisibility GetEmptyStateVisibility() const;
	FText GetStatusText() const;
	EActiveTimerReturnType RefreshWhileOpen(double CurrentTime, float DeltaTime);
	int32 CountFiltered(bool bPlaceable) const;

	TFunction<void(UClass*)> OnPickType;
	TFunction<void()> OnCreateType;
	TFunction<void(const FPaper2DPlusFrameCueTypeDescriptor&)> OnRepairType;
	TWeakPtr<SWidget> FocusReturnTarget;
	TArray<TSharedPtr<FPaper2DPlusFrameCueTypePickerItem>> AllItems;
	TArray<TSharedPtr<FPaper2DPlusFrameCueTypePickerItem>> FilteredItems;
	TSharedPtr<SSearchBox> SearchBox;
	TSharedPtr<SListView<TSharedPtr<FPaper2DPlusFrameCueTypePickerItem>>> ListView;
	TWeakPtr<FActiveTimerHandle> DiscoveryRefreshTimer;
	FString SearchText;
	FString DiscoverySignature;
	int32 ReadyTypeCount = 0;
	int32 RejectedTypeCount = 0;
	int32 DiscoveryErrorCount = 0;
};
