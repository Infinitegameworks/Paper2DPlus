// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"

class UPaperFlipbook;
class UPaper2DPlusCharacterProfileAsset;
struct FEditorEffectPreview;

/**
 * Preview canvas widget for frame events -- renders the current flipbook sprite
 * plus any active/selected effect previews. Supports drag-to-reposition for
 * spawn effect offsets.
 */
class SFrameEventPreviewCanvas : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SFrameEventPreviewCanvas) {}
		SLATE_ATTRIBUTE(UPaperFlipbook*, Flipbook)
		SLATE_ATTRIBUTE(int32, FrameIndex)
		SLATE_ARGUMENT(TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset>, Asset)
		SLATE_ATTRIBUTE(int32, FlipbookIndex)
		SLATE_ARGUMENT(TSharedPtr<TArray<FEditorEffectPreview>>, ActiveEditorEffects)
		SLATE_ATTRIBUTE(double, PlaybackTime)
		SLATE_ATTRIBUTE(bool, IsPlaying)
		SLATE_ATTRIBUTE(int32, SelectedEventIndex)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual FVector2D ComputeDesiredSize(float) const override;

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

	// Delegates for effect drag positioning
	DECLARE_DELEGATE(FOnEffectDragStarted);
	DECLARE_DELEGATE(FOnEffectDragEnded);
	DECLARE_DELEGATE_OneParam(FOnEffectOffsetChanged, FVector2D);

	FOnEffectDragStarted OnEffectDragStarted;
	FOnEffectDragEnded OnEffectDragEnded;
	FOnEffectOffsetChanged OnEffectOffsetChanged;

private:
	TAttribute<UPaperFlipbook*> Flipbook;
	TAttribute<int32> FrameIndex;
	TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset> Asset;
	TAttribute<int32> FlipbookIndex;
	TSharedPtr<TArray<FEditorEffectPreview>> ActiveEditorEffects;
	TAttribute<double> PlaybackTime;
	TAttribute<bool> IsPlaying;
	TAttribute<int32> SelectedEventIndex;

	// Drag state for effect positioning
	bool bDraggingEffect = false;
	FVector2D DragStartScreenPos;
	FVector2D DragStartOffset;

	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent) override;

	float GetEffectiveZoom(const FVector2D& WidgetSize, UPaperFlipbook* FB) const;
};