// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/SLeafWidget.h"

class UPaper2DPlusCharacterLayerAsset;
class FCharacterProfileEditorModel;
class UPaperSprite;

/**
 * Small leaf widget that paints the composite of all currently-visible character layers for ONE fixed
 * key frame of the current animation, fitted into its allotted geometry.
 *
 * Used by the Character Layer editor's main Art canvas and frame strips (including Hitboxes), so every
 * surface shows the live visible-Layer composite in global paint order. The current animation comes from
 * the shared profile model; FrameIndex is fixed per strip cell and retargeted on the main canvas.
 *
 * Draws each visible Layer through FEditorCanvasUtils::DrawFlipbookSprite with a shared center + fit scale.
 */
class SLayerCompositeThumbnail : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SLayerCompositeThumbnail)
		: _FrameIndex(0)
		, _DrawCheckerboard(false)
	{}
		SLATE_ARGUMENT(TWeakObjectPtr<UPaper2DPlusCharacterLayerAsset>, LayerAsset)
		SLATE_ARGUMENT(TSharedPtr<FCharacterProfileEditorModel>, Model)
		SLATE_ARGUMENT(int32, FrameIndex)
		/** Large preview canvases opt in; compact frame cells retain their standard strip background. */
		SLATE_ARGUMENT(bool, DrawCheckerboard)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SLayerCompositeThumbnail() override;

	virtual FVector2D ComputeDesiredSize(float) const override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

	/** Re-target this live composite without replacing the Slate widget. */
	void SetFrameIndex(int32 InFrameIndex);
	void RefreshFromModel();
	bool HasPaintItems() const { return !CachedPaintItems.IsEmpty(); }

	int32 GetCachedPaintItemCountForTests() const { return CachedPaintItems.Num(); }
	int32 GetRetainedSpriteCountForTests() const { return RetainedSprites.Num(); }

private:
	struct FCachedPaintItem
	{
		TWeakObjectPtr<UPaperSprite> Sprite;
		FVector2D OffsetPx = FVector2D::ZeroVector;
	};
	void RefreshCache();

	TWeakObjectPtr<UPaper2DPlusCharacterLayerAsset> LayerAsset;
	TSharedPtr<FCharacterProfileEditorModel> Model;
	int32 FrameIndex = 0;
	bool bDrawCheckerboard = false;
	TArray<FCachedPaintItem> CachedPaintItems;
	/** Editor previews synchronously resolve soft sprites and retain them across GC while painted. */
	TArray<TStrongObjectPtr<UPaperSprite>> RetainedSprites;
	FVector2D CachedMaxSpriteDims = FVector2D(1.0f, 1.0f);
	FDelegateHandle FlipbookSelectionHandle;
	FDelegateHandle LayerVisibilityHandle;
	FDelegateHandle AssetDataChangedHandle;
	FDelegateHandle AssetExternallyModifiedHandle;
};
