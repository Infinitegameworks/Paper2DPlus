// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "Containers/Ticker.h"
#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class FCharacterProfileEditorModel;
class UPaper2DPlusCharacterLayerAsset;
class UPaperFlipbook;
class SBorder;
class SHorizontalBox;
class SLayerCompositeThumbnail;
class SScrollBox;

/** Layer-first art preview and frame navigation. Appearance selection is authored by SLayerAppearancePanel. */
class SLayerOverviewPanel : public SCompoundWidget, public FEditorUndoClient
{
public:
	SLATE_BEGIN_ARGS(SLayerOverviewPanel)
		: _LayerFirstPresentation(true)
	{}
		SLATE_ARGUMENT(TSharedPtr<FCharacterProfileEditorModel>, Model)
		SLATE_ARGUMENT(UPaper2DPlusCharacterLayerAsset*, LayerAsset)
		SLATE_ARGUMENT(bool, LayerFirstPresentation)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SLayerOverviewPanel() override;
	virtual void PostUndo(bool bSuccess) override;
	virtual void PostRedo(bool bSuccess) override;
	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual void OnFocusChanging(
		const FWeakWidgetPath& PreviousFocusPath,
		const FWidgetPath& NewWidgetPath,
		const FFocusEvent& InFocusEvent) override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	void HandleAuthoringModeDeactivated();

	int32 GetPreviewPaintItemCountForTests() const;
	int32 GetFrameCellCountForTests() const;
	bool HasKeyboardFocusIndicatorForTests() const { return KeyboardFocusBorder.IsValid(); }
	bool IsPlayingForTests() const { return bIsPlaying; }
	bool AdvancePlaybackForTests(float DeltaTime) { return OnPlaybackTick(DeltaTime); }

private:
	void RefreshPreview();
	void RefreshFrameStrip();
	void ScrollSelectedFrameIntoView();
	TSharedRef<SWidget> BuildFrameStrip();
	FString GetCurrentAnimationName() const;
	UPaperFlipbook* GetCurrentFlipbook() const;
	int32 GetCurrentFrameCount() const;
	void SeekFrame(int32 FrameIndex, bool bStopPlayback = true);
	void StartPlayback();
	void StopPlayback();
	void TogglePlayback();
	bool OnPlaybackTick(float DeltaTime);
	FSlateColor GetKeyboardFocusIndicatorColor() const;

	TSharedPtr<FCharacterProfileEditorModel> Model;
	TWeakObjectPtr<UPaper2DPlusCharacterLayerAsset> LayerAsset;
	TSharedPtr<SLayerCompositeThumbnail> PreviewCanvas;
	TSharedPtr<SBorder> KeyboardFocusBorder;
	TSharedPtr<SScrollBox> FrameStripScrollBox;
	TSharedPtr<SHorizontalBox> FrameStripBox;
	TArray<TSharedPtr<SWidget>> FrameCellWidgets;
	bool bLayerFirstPresentation = true;
	bool bIsPlaying = false;
	float PlaybackPosition = 0.0f;
	FTSTicker::FDelegateHandle PlaybackTickerHandle;
	FDelegateHandle FlipbookSelectionHandle;
	FDelegateHandle FrameSelectionHandle;
	FDelegateHandle QueuePlaybackStateHandle;
};
