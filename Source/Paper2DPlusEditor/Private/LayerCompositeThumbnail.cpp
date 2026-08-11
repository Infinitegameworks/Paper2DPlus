// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "LayerCompositeThumbnail.h"
#include "CharacterProfileEditorModel.h"
#include "EditorCanvasUtils.h"
#include "Paper2DPlusCharacterLayerAsset.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Paper2DPlusLayerDraw.h" // U4: THE shared offset-math home
#include "PaperSprite.h"

void SLayerCompositeThumbnail::Construct(const FArguments& InArgs)
{
	LayerAsset = InArgs._LayerAsset;
	Model = InArgs._Model;
	FrameIndex = InArgs._FrameIndex;
	bDrawCheckerboard = InArgs._DrawCheckerboard;
	if (Model.IsValid())
	{
		FlipbookSelectionHandle = Model->OnFlipbookSelectionChanged.AddLambda(
			[this](int32) { RefreshCache(); });
		LayerVisibilityHandle = Model->OnLayerVisibilityChanged.AddLambda(
			[this]() { RefreshCache(); });
		AssetDataChangedHandle = Model->OnAssetDataChanged.AddLambda(
			[this]() { RefreshCache(); });
		AssetExternallyModifiedHandle = Model->OnAssetExternallyModified.AddLambda(
			[this]() { RefreshCache(); });
	}
	RefreshCache();

	// Keep the composite inside the (small) cell bounds — full-frame layers can otherwise bleed past it.
	SetClipping(EWidgetClipping::ClipToBounds);
}

SLayerCompositeThumbnail::~SLayerCompositeThumbnail()
{
	if (Model.IsValid())
	{
		Model->OnFlipbookSelectionChanged.Remove(FlipbookSelectionHandle);
		Model->OnLayerVisibilityChanged.Remove(LayerVisibilityHandle);
		Model->OnAssetDataChanged.Remove(AssetDataChangedHandle);
		Model->OnAssetExternallyModified.Remove(AssetExternallyModifiedHandle);
	}
}

void SLayerCompositeThumbnail::SetFrameIndex(int32 InFrameIndex)
{
	if (FrameIndex == InFrameIndex)
	{
		return;
	}
	FrameIndex = InFrameIndex;
	RefreshCache();
}

void SLayerCompositeThumbnail::RefreshFromModel()
{
	RefreshCache();
}

void SLayerCompositeThumbnail::RefreshCache()
{
	CachedPaintItems.Reset();
	RetainedSprites.Reset();
	CachedMaxSpriteDims = FVector2D(1.0f, 1.0f);
	Invalidate(EInvalidateWidgetReason::Paint);
	UPaper2DPlusCharacterLayerAsset* Asset = LayerAsset.Get();
	UPaper2DPlusCharacterProfileAsset* Profile = Model.IsValid() ? Model->GetAsset() : nullptr;
	if (!Asset || !Profile) return;
	const int32 FlipbookIdx = Model->GetSelectedFlipbookIndex();
	if (!Profile->Flipbooks.IsValidIndex(FlipbookIdx)) return;
	const FFlipbookProfileEntry& Entry = Profile->Flipbooks[FlipbookIdx];
	TArray<FString> VisibleNames;
	for (const FCharacterLayer& Layer : Asset->Layers)
	{
		if (Model->IsLayerVisible(Layer.LayerName)) VisibleNames.Add(Layer.LayerName);
	}
	Asset->SortVisibleLayersForEffectivePaintOrder(VisibleNames);
	for (const FString& LayerName : VisibleNames)
	{
		const FCharacterLayer* Layer = Asset->GetLayerByName(LayerName);
		UPaperSprite* Sprite = Layer
			? Layer->GetSpriteForFrame(Entry.Identity.FlipbookName, FrameIndex, true)
			: nullptr;
		if (!Sprite) continue;
		// This is an editor refresh boundary, not the runtime per-frame path. Resolve the soft reference here
		// and keep a hard reference for as long as this cell/canvas paints it; otherwise an unloaded asset or
		// a GC between frames silently turns the Art view into an empty dark rectangle.
		RetainedSprites.Emplace(Sprite);
		FCachedPaintItem& Item = CachedPaintItems.AddDefaulted_GetRef();
		Item.Sprite = Sprite;
		Item.OffsetPx = Paper2DPlusLayerDraw::ResolveTotalOffsetPx(
			&Entry, FrameIndex, Layer, Entry.Identity.FlipbookName);
		const FVector2D Dims = Sprite->GetSourceSize();
		CachedMaxSpriteDims.X = FMath::Max(
			CachedMaxSpriteDims.X, Dims.X + 2.0f * FMath::Abs(Item.OffsetPx.X));
		CachedMaxSpriteDims.Y = FMath::Max(
			CachedMaxSpriteDims.Y, Dims.Y + 2.0f * FMath::Abs(Item.OffsetPx.Y));
	}
}

FVector2D SLayerCompositeThumbnail::ComputeDesiredSize(float) const
{
	return FVector2D(48.0f, 48.0f);
}

int32 SLayerCompositeThumbnail::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
	int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const FVector2D CanvasSize = AllottedGeometry.GetLocalSize();
	if (bDrawCheckerboard)
	{
		FEditorCanvasUtils::DrawCheckerboard(
			OutDrawElements,
			LayerId,
			AllottedGeometry,
			16.0f,
			FLinearColor(0.20f, 0.20f, 0.20f, 1.0f),
			FLinearColor(0.28f, 0.28f, 0.28f, 1.0f));
		++LayerId;
	}
	if (CachedPaintItems.IsEmpty() || CanvasSize.X <= 0.0f || CanvasSize.Y <= 0.0f) return LayerId;

	// Aspect-correct fit with a hair of margin so the silhouette doesn't touch the cell border.
	const float Scale = FMath::Min(
		CanvasSize.X / CachedMaxSpriteDims.X, CanvasSize.Y / CachedMaxSpriteDims.Y) * 0.95f;
	const FVector2D DrawSize = CachedMaxSpriteDims * Scale;
	const FVector2D Center = (CanvasSize - DrawSize) * 0.5f + DrawSize * 0.5f;

	for (const FCachedPaintItem& Item : CachedPaintItems)
	{
		UPaperSprite* Sprite = Item.Sprite.Get();
		if (!Sprite) continue;
		// U4 (D2): the per-item TOTAL offset shifts the draw through THIS cell's paint transform (center +
		// OffsetPx * Scale) — AnimData stays null so DrawFlipbookSprite can't double-apply the SpriteOffset.
		FEditorCanvasUtils::DrawFlipbookSprite(
			OutDrawElements, LayerId, AllottedGeometry,
			Sprite, nullptr, FrameIndex,
			Center + Item.OffsetPx * Scale, Scale, FLinearColor::White);
		LayerId++;
	}

	return LayerId;
}
