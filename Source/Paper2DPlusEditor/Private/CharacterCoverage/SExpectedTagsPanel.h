// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CharacterCoverage/CharacterCoverageResolver.h"
#include "CharacterCoverage/ExpectedTagAssignment.h"
#include "CoreMinimal.h"
#include "EditorUndoClient.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/SCompoundWidget.h"

class FActiveTimerHandle;
class FCharacterProfileEditorModel;
class SComboButton;
class SVerticalBox;
class UPaper2DPlusCharacterCatalogAsset;
class UPaper2DPlusCharacterProfileAsset;

/** Catalog authority outcome used by production resolution and the headless injection seam. */
enum class EExpectedTagsCatalogResolutionStatus : uint8
{
	NotConfigured,
	ConfiguredButUnavailable,
	Loaded
};

/** One explicit Catalog resolution. A Loaded result is valid only when Catalog is non-null. */
struct FExpectedTagsCatalogResolution
{
	EExpectedTagsCatalogResolutionStatus Status =
		EExpectedTagsCatalogResolutionStatus::NotConfigured;
	FSoftObjectPath ConfiguredPath;
	TWeakObjectPtr<UPaper2DPlusCharacterCatalogAsset> Catalog;

	static FExpectedTagsCatalogResolution NotConfigured();
	static FExpectedTagsCatalogResolution ConfiguredButUnavailable(
		const FSoftObjectPath& InConfiguredPath);
	static FExpectedTagsCatalogResolution Loaded(
		UPaper2DPlusCharacterCatalogAsset* InCatalog);
};

using FExpectedTagsCatalogProvider =
	TFunction<FExpectedTagsCatalogResolution()>;

/** Informational body state. Ready means CoverageRows contains one row per expected tag. */
enum class EExpectedTagsPanelState : uint8
{
	Ready,
	NoAuthoritativeCatalog,
	ConfiguredCatalogUnavailable,
	ProfileNotInCatalog,
	CatalogHasNoExpectations,
	ProfileUnavailable
};

/** Presentation-only refinement of the shared resolver's three coverage states. */
enum class EExpectedTagsRowPresentation : uint8
{
	Missing,
	NearMiss,
	Covered,
	Qualified
};

/**
 * Permanent profile-wide expected-animation coverage surface.
 *
 * Catalog membership/expectations come through the Catalog accessor exactly once per loaded refresh;
 * only a refresh that actually has expectations then calls the shared coverage resolver, exactly
 * once — the non-member and no-expectation states are decided from the Catalog lookup alone. This
 * panel never derives coverage or mutates authored data during construction, refresh, selection
 * changes, or Catalog resolution. Explicit picker/drag gestures delegate all writes to
 * FExpectedTagAssignment.
 */
class SExpectedTagsPanel final
	: public SCompoundWidget
	, public FEditorUndoClient
{
public:
	SLATE_BEGIN_ARGS(SExpectedTagsPanel) {}
		SLATE_ARGUMENT(TSharedPtr<FCharacterProfileEditorModel>, Model)
		/** Optional deterministic seam. Unbound production panels resolve DefaultCharacterCatalog. */
		SLATE_ARGUMENT(FExpectedTagsCatalogProvider, CatalogProvider)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SExpectedTagsPanel() override;

	/** Re-resolve current truth. Strictly read-only. */
	void Refresh();
	/** Idempotent early disconnect used by the toolkit before releasing the shared model. */
	void Shutdown();

	// FEditorUndoClient
	virtual void PostUndo(bool bSuccess) override;
	virtual void PostRedo(bool bSuccess) override;

	// Panel-owned drag arming: survives row reconstruction and validates current truth before launch.
	virtual FReply OnPreviewMouseButtonDown(
		const FGeometry& MyGeometry,
		const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseMove(
		const FGeometry& MyGeometry,
		const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(
		const FGeometry& MyGeometry,
		const FPointerEvent& MouseEvent) override;
	virtual void OnMouseLeave(const FPointerEvent& MouseEvent) override;

#if WITH_DEV_AUTOMATION_TESTS
	/** Focused, Slate-free observation/gesture seams for this panel's automation tests. */
	EExpectedTagsPanelState GetPanelStateForTests() const { return PanelState; }
	const TArray<FCharacterCoverageRow>& GetCoverageRowsForTests() const
	{
		return CoverageRows;
	}
	FText GetEmptyStateTextForTests() const;
	uint32 GetRefreshSerialForTests() const { return RefreshSerial; }
	int32 GetCatalogAccessorCallsInLastRefreshForTests() const
	{
		return CatalogAccessorCallsInLastRefresh;
	}
	int32 GetResolverCallsInLastRefreshForTests() const
	{
		return ResolverCallsInLastRefresh;
	}
	int32 GetBodyRebuildCountForTests() const { return BodyRebuildCount; }
	bool OwnsCatalogWatchForTests() const { return bOwnsCatalogWatch; }
	UObject* GetRetainedCatalogForTests() const
	{
		return CatalogLifetimeGuard.Get();
	}
	void ArmExpectedTagDragForTests(const FGameplayTag& ExpectedTag)
	{
		ArmExpectedTagDrag(ExpectedTag, FVector2D::ZeroVector);
	}
	bool IsExpectedTagDragArmedForTests() const
	{
		return bExpectedTagDragArmed;
	}
	EExpectedTagAssignmentResult AssignTagToAnimationForTests(
		const FGameplayTag& ExpectedTag,
		int32 FlipbookIndex);
	static EExpectedTagsRowPresentation GetRowPresentationForTests(
		const FCharacterCoverageRow& Row);
	static FText GetRowStatusTextForTests(const FCharacterCoverageRow& Row);
#endif

private:
	friend class SExpectedTagsPanelDragSource;

	struct FPendingPickerAssignment
	{
		FGameplayTag ExpectedTag;
		FExpectedTagAnimationTarget Target;
	};

	static FExpectedTagsCatalogResolution ResolveDefaultCatalog();
	static EExpectedTagsRowPresentation GetRowPresentation(
		const FCharacterCoverageRow& Row);
	static FText GetRowStatusText(const FCharacterCoverageRow& Row);
	static FText GetEmptyStateText(EExpectedTagsPanelState State);

	void RebuildBody();
	TSharedRef<SWidget> BuildCoverageRow(const FCharacterCoverageRow& Row);
	TSharedRef<SWidget> BuildExpectedTagChip(const FCharacterCoverageRow& Row) const;
	TSharedRef<SWidget> BuildAnimationPickerMenu(FGameplayTag ExpectedTag);
	FText GetPickerButtonText(const FCharacterCoverageRow& Row) const;

	void HandleModelRefresh();
	void HandleCatalogSettingsChanged();
	void HandleTagColorsChanged();

	/**
	 * Track the Catalog this panel's coverage is derived from, and refresh THIS PANEL when it changes.
	 *
	 * Deliberately a panel-local subscription rather than the shared model's secondary watched-object
	 * slot. That slot is the Layer workspace's dual-asset channel and it drives
	 * OnAssetExternallyModified, which every panel in the Character Profile editor rebuilds from — so
	 * installing the Catalog there made an unrelated Catalog edit tear down in-progress inline renames
	 * and reset list scroll positions in a workspace the designer was not even looking at. This panel
	 * needs a self-refresh, so its blast radius is itself.
	 */
	void UpdateCatalogWatch(UPaper2DPlusCharacterCatalogAsset* LoadedCatalog);
	void ReleaseOwnedCatalogWatch();
	void HandleObjectModified(UObject* ModifiedObject);
	EActiveTimerReturnType FlushCatalogRefresh(double CurrentTime, float DeltaTime);

	void ArmExpectedTagDrag(
		const FGameplayTag& ExpectedTag,
		const FVector2D& ScreenSpacePosition);
	void DisarmExpectedTagDrag();
	bool IsArmedDragStillValid() const;

	void HandleAnimationPicked(
		FGameplayTag ExpectedTag,
		FExpectedTagAnimationTarget Target);
	void QueuePickerAssignment(
		const FGameplayTag& ExpectedTag,
		const FExpectedTagAnimationTarget& Target);
	EActiveTimerReturnType FlushPickerAssignment(double CurrentTime, float DeltaTime);
	void CancelPendingPickerAssignment();

	TSharedPtr<FCharacterProfileEditorModel> Model;
	FExpectedTagsCatalogProvider CatalogProvider;
	/** Soft settings and the model watch are weak; the open panel explicitly keeps its authority live. */
	TStrongObjectPtr<UObject> CatalogLifetimeGuard;
	TWeakObjectPtr<UPaper2DPlusCharacterCatalogAsset> CatalogWatchInstalledByPanel;
	bool bOwnsCatalogWatch = false;
	FDelegateHandle CatalogObjectModifiedHandle;
	bool bCatalogRefreshPending = false;

	TSharedPtr<SVerticalBox> RowsHost;
	TMap<FName, TWeakPtr<SComboButton>> RowPickerCombos;
	TArray<FCharacterCoverageRow> CoverageRows;
	EExpectedTagsPanelState PanelState =
		EExpectedTagsPanelState::NoAuthoritativeCatalog;

	FDelegateHandle AssetDataChangedHandle;
	FDelegateHandle ExternalModifiedHandle;
	FDelegateHandle CatalogSettingsChangedHandle;
	FDelegateHandle TagColorsChangedHandle;
	bool bShutdown = false;

	bool bExpectedTagDragArmed = false;
	FGameplayTag ArmedExpectedTag;
	FVector2D ArmedDragStartPosition = FVector2D::ZeroVector;
	TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset> ArmedSourceAsset;

	TOptional<FPendingPickerAssignment> PendingPickerAssignment;
	TWeakPtr<FActiveTimerHandle> PickerCommitTimerHandle;

	uint32 RefreshSerial = 0;
	int32 CatalogAccessorCallsInLastRefresh = 0;
	int32 ResolverCallsInLastRefresh = 0;
	int32 BodyRebuildCount = 0;
};
