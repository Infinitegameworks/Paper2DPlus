// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ProfileThumbnailBudget.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/SoftObjectPtr.h"
#include "Widgets/SCompoundWidget.h"

class FCombatProfileEditorSession;
class ITableRow;
class STableViewBase;
class UPaperFlipbook;
class UPaper2DPlusCombatProfileAsset;
struct FPaper2DPlusCombatAttackDerivedData;
struct FStreamableHandle;
template <typename ItemType> class STileView;

/**
 * The ONE selectable attack surface of the Combat Profile editor: a searchable, virtualized
 * card grid over the session's derived attack catalog. Cards show the move's art, name,
 * attack-tag leaf, and reach; a dot marks moves customized on this profile. Tile generation
 * is the virtualization boundary — a visible unloaded preview streams in through a bounded
 * true-LRU handle cache, and refreshing the catalog never synchronously loads art.
 */
class SCombatAttackBrowserPanel final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SCombatAttackBrowserPanel) {}
		SLATE_ARGUMENT(TSharedPtr<FCombatProfileEditorSession>, Session)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SCombatAttackBrowserPanel() override;

	static constexpr int32 MaxRetainedThumbnails = 48;

	/** Core soft-ref lookup for a move's flipbook preview; never loads. Static so tests can pin the no-load contract. */
	static TSoftObjectPtr<UPaperFlipbook> FindAttackFlipbookSoftRef(
		const UPaper2DPlusCombatProfileAsset* Asset,
		FName MoveName);

#if WITH_DEV_AUTOMATION_TESTS
	int32 GetRowCountForTests() const { return AllRows.Num(); }
	int32 GetVisibleRowCountForTests() const { return FilteredRows.Num(); }
	void SetSearchTextForTests(const FString& InText);
	/** Ticks the virtualized tile view against a constrained viewport; returns generated (visible) card count. */
	int32 GenerateCardsForViewportForTests(const FVector2D& ViewportSize);
	bool RequestThumbnailForTests(const FSoftObjectPath& FlipbookPath);
	bool WasThumbnailRequestedForTests(const FSoftObjectPath& FlipbookPath) const
	{
		return ThumbnailBudget.Contains(FlipbookPath);
	}
	int32 GetRetainedThumbnailCountForTests() const { return ThumbnailBudget.Num(); }
	bool IsThumbnailRetainedForTests(const FSoftObjectPath& FlipbookPath) const
	{
		return ThumbnailBudget.Contains(FlipbookPath);
	}
	void ResetThumbnailsForTests() { ResetThumbnailLoads(); }
#endif

private:
	using FRowPtr = TSharedPtr<FPaper2DPlusCombatAttackDerivedData>;

	void RefreshFromSession();
	void RebuildFilteredRows();
	void HandleSearchChanged(const FText& NewText);
	void SynchronizeSelection();
	FName GetSelectedMoveName() const;

	TSharedRef<ITableRow> GenerateCardTile(FRowPtr Item, const TSharedRef<STableViewBase>& OwnerTable);
	TSharedRef<SWidget> BuildAttackCard(const FPaper2DPlusCombatAttackDerivedData& Row);

	void RequestThumbnailAsync(const FSoftObjectPath& FlipbookPath);
	void TouchThumbnail(const FSoftObjectPath& FlipbookPath);
	void HandleThumbnailLoaded(FSoftObjectPath FlipbookPath);
	void ResetThumbnailLoads();

	TSharedPtr<FCombatProfileEditorSession> Session;
	TArray<FRowPtr> AllRows;
	TArray<FRowPtr> FilteredRows;
	FString SearchText;
	TSharedPtr<STileView<FRowPtr>> CardTileView;

	/** Retention for visible card art. The shared budget owns the LRU; this panel owns the load. Its
	 *  release hook is installed in Construct, where FStreamableHandle is a complete type — this
	 *  header only forward-declares it. */
	TProfileThumbnailBudget<TSharedPtr<FStreamableHandle>> ThumbnailBudget{MaxRetainedThumbnails};

	FDelegateHandle DataChangedHandle;
	FDelegateHandle SelectionChangedHandle;
	bool bSynchronizingSelection = false;
};
