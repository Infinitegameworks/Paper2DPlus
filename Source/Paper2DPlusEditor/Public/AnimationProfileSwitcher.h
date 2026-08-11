// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ProfileItemPicker.h"
#include "Widgets/SCompoundWidget.h"

class SBorder;
class SBox;

/**
 * Builds the animation-only visual used by popup, Navigator, drawer, and the current row.
 * It resolves only already-loaded flipbooks; unloaded or invalid paths remain an
 * explicit placeholder and never turn virtualized search into a synchronous load.
 */
PAPER2DPLUSEDITOR_API TSharedPtr<SWidget> BuildAnimationProfileItemPreview(
	const FProfilePickerItem& Item);

/**
 * Compact current-animation search with local adjacent keyboard navigation.
 *
 * Adjacency is re-derived from stable item identity and CanonicalOrder every time
 * the source changes. Up/Down are intentionally local Slate commands: they only
 * arrive while this control (or one of its non-input descendants) owns focus.
 */
class PAPER2DPLUSEDITOR_API SAnimationProfileSwitcher : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SAnimationProfileSwitcher) {}
		SLATE_ARGUMENT(TSharedPtr<IProfileItemPickerSource>, Source)
		SLATE_ATTRIBUTE(FText, EmptySelectionText)
		/** Caption above the current row. Defaults to "Current animation"; Combat passes "Current attack". */
		SLATE_ATTRIBUTE(FText, CaptionText)
		/** Singular noun for list-boundary and accessibility text. Defaults to "animation". */
		SLATE_ATTRIBUTE(FText, ItemNounText)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SAnimationProfileSwitcher() override;

	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual void OnFocusChanging(
		const FWeakWidgetPath& PreviousFocusPath,
		const FWidgetPath& NewWidgetPath,
		const FFocusEvent& InFocusEvent) override;
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

#if WITH_DEV_AUTOMATION_TESTS
	FProfileItemIdentity GetPreviousIdentityForTests() const;
	FProfileItemIdentity GetCurrentIdentityForTests() const;
	FProfileItemIdentity GetNextIdentityForTests() const;
	FText GetAccessibleSummaryForTests() const;
	bool HasKeyboardFocusIndicatorForTests() const { return KeyboardFocusBorder.IsValid(); }
	bool IsKeyboardFocusIndicatorVisibleForTests() const;
	int32 GetFocusChangeRevisionForTests() const { return FocusChangeRevision; }
	int32 GetPersistentRowCountForTests() const;
#endif

private:
	void RefreshFromSource();
	bool NavigateAdjacent(int32 Delta);
	const FProfilePickerItem* GetRelativeItem(int32 Delta) const;
	TSharedRef<SWidget> BuildCurrentRow();
	void RefreshCurrentPreview();
	FText GetRelativeLabel(int32 Delta) const;
	FText GetAccessibleSummary() const;
	FSlateColor GetFocusBorderColor() const;
	FReply HandleSwitcherMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event);

	TSharedPtr<IProfileItemPickerSource> Source;
	TAttribute<FText> EmptySelectionText;
	TAttribute<FText> CaptionText;
	TAttribute<FText> ItemNounText;
	TArray<FProfilePickerItem> OrderedItems;
	int32 CurrentItemIndex = INDEX_NONE;
	FDelegateHandle SourceChangedHandle;
	TSharedPtr<SBorder> KeyboardFocusBorder;
	TSharedPtr<SBox> CurrentPreviewHost;
#if WITH_DEV_AUTOMATION_TESTS
	int32 FocusChangeRevision = 0;
#endif
};
