// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class FFlipbookDrawModel;
enum class EFlipbookDrawTool : uint8;
enum class EFlipbookPixelTransform : uint8;
class SWrapBox;

DECLARE_DELEGATE_OneParam(FOnFlipbookFrameTransform, EFlipbookPixelTransform);

/**
 * SFlipbookDrawToolsPanel — the left-hand tool/color/playback panel for the Flipbook Draw editor.
 *
 * Real buttons (per house style: FlatButton.Default / SimpleButton): Pencil/Eraser/Eyedropper/Fill/
 * Line/Rectangle tool toggles, playback, brush size, color/swatches, clearly labeled drawing-symmetry
 * toggles, and one-shot whole-frame transforms supplied by the owning canvas. Persistent tool state
 * lives on FFlipbookDrawModel; pixel mutations remain in SFlipbookDrawCanvas's undo-aware path.
 */
class SFlipbookDrawToolsPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SFlipbookDrawToolsPanel) {}
		SLATE_ARGUMENT(TSharedPtr<FFlipbookDrawModel>, Model)
		SLATE_EVENT(FOnFlipbookFrameTransform, OnFrameTransform)
		SLATE_ATTRIBUTE(bool, CanFrameTransform)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SFlipbookDrawToolsPanel();

private:
	TSharedRef<SWidget> BuildToolButton(EFlipbookDrawTool Tool, const FText& Label, const FText& Tip);
	TSharedRef<SWidget> BuildTransformButton(EFlipbookPixelTransform Transform, const FText& Label, const FText& Tip);
	void RebuildSwatches();
	FReply OnOpenColorPicker();
	FReply OnAddSwatch();
	void HandleToolStateChanged();

	TSharedPtr<FFlipbookDrawModel> Model;
	FOnFlipbookFrameTransform OnFrameTransform;
	TAttribute<bool> CanFrameTransform;
	FDelegateHandle ToolStateHandle;
	TSharedPtr<SWrapBox> SwatchBox;
};
