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
	/** Raised when the user clicks visible art belonging to a layer. Never raised for a click that
	 *  lands where every visible layer is transparent. */
	DECLARE_DELEGATE_OneParam(FOnLayerPicked, const FGuid& /*LayerId*/);

	SLATE_BEGIN_ARGS(SLayerCompositeThumbnail)
		: _FrameIndex(0)
		, _DrawCheckerboard(false)
		, _EnableLayerPicking(false)
	{}
		SLATE_ARGUMENT(TWeakObjectPtr<UPaper2DPlusCharacterLayerAsset>, LayerAsset)
		SLATE_ARGUMENT(TSharedPtr<FCharacterProfileEditorModel>, Model)
		SLATE_ARGUMENT(int32, FrameIndex)
		/** Large preview canvases opt in; compact frame cells retain their standard strip background. */
		SLATE_ARGUMENT(bool, DrawCheckerboard)
		/** Opt-in hover-highlight + click-to-select-layer. OFF by default so frame-strip cells keep
		 *  passing their clicks through to the strip's own scrub handler. */
		SLATE_ARGUMENT(bool, EnableLayerPicking)
		SLATE_EVENT(FOnLayerPicked, OnLayerPicked)
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

	//~ Begin SWidget interface — layer picking (no-ops unless bEnableLayerPicking)
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void OnMouseLeave(const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FCursorReply OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const override;
	//~ End SWidget interface

	/** Resolve the layer whose visible art covers a widget-local point, topmost first. Returns an
	 *  invalid Guid when the point is transparent in EVERY visible layer. */
	FGuid LayerUnderLocalPoint(const FVector2D& LocalPos) const;

private:
	struct FCachedPaintItem
	{
		TWeakObjectPtr<UPaperSprite> Sprite;
		FVector2D OffsetPx = FVector2D::ZeroVector;
		/** Stable identity of the layer this item came from, so a hit reports a LayerId rather than
		 *  a paint index that shifts whenever visibility changes. */
		FGuid LayerId;
	};

	/** One painted sprite's screen footprint, captured during OnPaint from the SAME transform that
	 *  drew it — deriving it separately is how a hit-test silently drifts from what is on screen. */
	struct FLayerHitRegion
	{
		FVector2D DrawPos = FVector2D::ZeroVector;
		FVector2D DrawSize = FVector2D::ZeroVector;
		TWeakObjectPtr<UPaperSprite> Sprite;
		FGuid LayerId;
	};

	/** Decoded alpha for one sprite's source rect. */
	struct FSpriteAlphaMip
	{
		TArray<uint8> Alpha;
		int32 Width = 0;
		int32 Height = 0;
		bool bValid = false;
	};

	/** True only when the sprite has a genuinely opaque texel under the point. Fails CLOSED: a
	 *  sprite whose source cannot be decoded reports not-opaque, so an unreadable format can never
	 *  claim the whole canvas. */
	bool IsSpriteOpaqueAtLocal(const FLayerHitRegion& Region, const FVector2D& LocalPos) const;

	void RefreshCache();

	TWeakObjectPtr<UPaper2DPlusCharacterLayerAsset> LayerAsset;
	TSharedPtr<FCharacterProfileEditorModel> Model;
	int32 FrameIndex = 0;
	bool bDrawCheckerboard = false;
	bool bEnableLayerPicking = false;
	FOnLayerPicked OnLayerPicked;
	/** Rebuilt every paint; mutable because OnPaint is const and the hit cache must come from it. */
	mutable TArray<FLayerHitRegion> HitRegions;
	mutable TMap<TWeakObjectPtr<UPaperSprite>, FSpriteAlphaMip> AlphaCache;
	FGuid HoveredLayerId;
	bool bPickPressed = false;
	TArray<FCachedPaintItem> CachedPaintItems;
	/** Editor previews synchronously resolve soft sprites and retain them across GC while painted. */
	TArray<TStrongObjectPtr<UPaperSprite>> RetainedSprites;
	FVector2D CachedMaxSpriteDims = FVector2D(1.0f, 1.0f);
	FDelegateHandle FlipbookSelectionHandle;
	FDelegateHandle LayerVisibilityHandle;
	FDelegateHandle AssetDataChangedHandle;
	FDelegateHandle AssetExternallyModifiedHandle;
};
