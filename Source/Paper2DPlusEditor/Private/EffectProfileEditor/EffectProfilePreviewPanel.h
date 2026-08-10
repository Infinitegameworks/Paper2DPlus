// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EffectProfileEditorModel.h"
#include "Widgets/SCompoundWidget.h"

class SEffectProfileFramePreview;
class SBorder;
class SHorizontalBox;
class SScrollBox;
struct FPropertyChangedEvent;

/** Selected-flipbook preview using the same clickable frame-strip grammar as Character Profile tools. */
class SEffectProfilePreviewPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SEffectProfilePreviewPanel) {}
		SLATE_ARGUMENT(TSharedPtr<FEffectProfileEditorModel>, Model)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SEffectProfilePreviewPanel() override;
	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual void OnFocusChanging(
		const FWeakWidgetPath& PreviousFocusPath,
		const FWidgetPath& NewWidgetPath,
		const FFocusEvent& InFocusEvent) override;
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply OnMouseButtonDown(
		const FGeometry& MyGeometry,
		const FPointerEvent& MouseEvent) override;

	bool HasSelectionForTests() const { return Playback.GetFlipbook() != nullptr; }
	FEffectProfilePlaybackState& GetPlaybackStateForTests() { return Playback; }
	const FEffectProfilePlaybackState& GetPlaybackStateForTests() const { return Playback; }
	void AdvancePlaybackForTests(float DeltaSeconds);
	void TogglePlaybackForTests() { TogglePlayback(); }
	void ForceRefreshForTests() { ForceRefreshSource(); }
	int32 GetPreviewRefreshRevisionForTests() const { return PreviewRefreshRevision; }
	int32 GetFrameStripRefreshRevisionForTests() const { return FrameStripRefreshRevision; }
	bool HasPlaybackTimerForTests() const { return PlaybackTimerHandle.IsValid(); }
	UObject* GetPreviewBrushResourceForTests() const;
	int32 GetFrameStripFrameCountForTests() const;
	int32 CountDescendantWidgetsForTests(FName WidgetType) const;
	bool HasKeyboardFocusIndicatorForTests() const;
	bool IsKeyboardFocusIndicatorVisibleForTests() const;
	int32 GetFocusChangeRevisionForTests() const { return FocusChangeRevision; }
	int32 GetAccessibleAnnouncementRevisionForTests() const { return AccessibleAnnouncementRevision; }
	float GetFrameStripScrollOffsetForTests() const;
	FText GetAccessibleSummaryForTests() const;

private:
	void HandleSelectedEffectChanged(UPaperFlipbook* Flipbook);
	void HandleModelSourceChanged();
	void HandleSourceObjectChanged(UObject* Object, FPropertyChangedEvent& Event);
	void HandleAssetReimported(UObject* Object);
	FReply HandleOpenSourceClicked();
	FReply HandleFrameClicked(int32 FrameIndex, const FPointerEvent& MouseEvent);
	bool HandleFrameNavigationKey(const FKey& Key);
	bool SeekToFrame(int32 FrameIndex);
	TSharedRef<SWidget> BuildFrameStrip();
	void RefreshFrameStrip();
	void ScrollSelectedFrameIntoView();
	void RefreshPreviewFrame(bool bForceRefresh = false);
	void ForceRefreshSource();
	void RefreshSourceDependencies();
	bool IsSourceDependency(const UObject* Object) const;
	void TogglePlayback();
	void NotifyAccessibleStateChanged();
	void EnsurePlaybackTimer();
	void StopPlaybackTimer();
	EActiveTimerReturnType HandlePlaybackTimer(double CurrentTime, float DeltaSeconds);
	FText GetSelectionTitle() const;
	FText GetPlaybackStatusText() const;
	FText GetPreviewAccessibleText() const;
	FSlateColor GetKeyboardFocusIndicatorColor() const;

	TSharedPtr<FEffectProfileEditorModel> Model;
	TSharedPtr<SEffectProfileFramePreview> PreviewCanvas;
	TSharedPtr<SBorder> KeyboardFocusBorder;
	TSharedPtr<SScrollBox> FrameStripScrollBox;
	TSharedPtr<SHorizontalBox> FrameStripBox;
	TArray<TSharedPtr<SWidget>> FrameCellWidgets;
	FEffectProfilePlaybackState Playback;
	TSet<TWeakObjectPtr<UObject>> SourceDependencies;
	TWeakPtr<FActiveTimerHandle> PlaybackTimerHandle;
	FDelegateHandle SelectionChangedHandle;
	FDelegateHandle SourceChangedHandle;
	FDelegateHandle ObjectPropertyChangedHandle;
	FDelegateHandle AssetReimportHandle;
	int32 PreviewRefreshRevision = 0;
	int32 FrameStripRefreshRevision = 0;
	int32 FocusChangeRevision = 0;

	// How focus last arrived. Clicking the preview focuses it (so keys reach the frame strip), but
	// a persistent focus rectangle around the whole preview after every click reads as a stuck
	// highlight — so the indicator is drawn only for non-mouse focus, the same "focus-visible"
	// rule the platform uses. Keyboard/programmatic focus still shows it.
	EFocusCause LastFocusCause = EFocusCause::SetDirectly;
	int32 AccessibleAnnouncementRevision = 0;
};
