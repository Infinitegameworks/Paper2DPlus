// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class FCharacterProfileEditorModel;
class UPaper2DPlusCharacterProfileAsset;
class UPaper2DPlusCharacterLayerAsset;
class SVerticalBox;
class SScrollBox;
class SExpandableArea;
class SProfileItemPicker;
class SProfileNavigatorPanel;
class FAnimationProfilePickerSource;

class SFlipbookListPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SFlipbookListPanel) {}
		SLATE_ARGUMENT(TSharedPtr<FCharacterProfileEditorModel>, Model)
		// Set only by the Character Layer editor so navigator preferences remain toolkit-specific.
		SLATE_ARGUMENT(TWeakObjectPtr<UPaper2DPlusCharacterLayerAsset>, LayerAsset)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SFlipbookListPanel();

	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

	void RefreshFlipbookList();

#if WITH_DEV_AUTOMATION_TESTS
	int32 GenerateNavigatorRowsForTests(const FVector2D& ViewportSize);
	int32 GetNavigatorPreviewCountForTests() const;
#endif

private:
	TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset> Asset;
	// Set only by the Character Layer editor — see the Construct arg.
	TWeakObjectPtr<UPaper2DPlusCharacterLayerAsset> LayerAsset;
	TSharedPtr<FCharacterProfileEditorModel> Model;

	// Flipbook list
	TSharedPtr<SVerticalBox> FlipbookListBox;
	TSharedPtr<FAnimationProfilePickerSource> AnimationPickerSource;
	TSharedPtr<SProfileItemPicker> CompactAnimationPicker;
	TSharedPtr<SProfileNavigatorPanel> AnimationNavigator;
	TSharedPtr<SExpandableArea> NavigatorExpandArea;

	// Model delegate handles
	FDelegateHandle ModelFlipbookSelectionHandle;
	FDelegateHandle ModelGroupCollapseHandle;
	FDelegateHandle ModelSearchTextHandle;
	FDelegateHandle ModelAssetDataChangedHandle;
	FDelegateHandle ModelAssetExternallyModifiedHandle;

	TWeakPtr<FActiveTimerHandle> SearchDebounceTimer;

	void OnSharedSearchTextChanged(const FString& NewText);

	/**
	 * Append to the shared playback queue and reveal the docked Playback Queue tab. The queue UI and
	 * its ticker live in SPlaybackQueuePanel; this panel only produces entries.
	 */
	void AddToQueueAndReveal(int32 FlipbookIndex);

	/**
	 * Per-flipbook action menu, shared by the row's ▾ dropdown button and its right-click. Contents:
	 * Preview and Add to Queue. Returns a ready-to-show menu widget (FMenuBuilder::MakeWidget). The actions re-resolve the
	 * flipbook by NAME at click time (a popup can outlive a list rebuild from undo/delete/external-modify, so
	 * the row's build-time index goes stale) — a no-op if the animation no longer exists.
	 */
	TSharedRef<class SWidget> BuildFlipbookRowMenu(int32 FlipbookIndex);

	TSharedRef<SWidget> BuildNavigatorItemExtension(const struct FProfileItemIdentity& Identity);
	void HandleNavigatorItemDoubleClicked(const struct FProfileItemIdentity& Identity);
	bool LoadNavigatorExpanded() const;
	void SaveNavigatorExpanded(bool bExpanded) const;
};
