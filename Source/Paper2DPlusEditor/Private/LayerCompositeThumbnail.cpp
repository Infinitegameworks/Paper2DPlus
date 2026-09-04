// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "LayerCompositeThumbnail.h"
#include "CharacterProfileEditorModel.h"
#include "EditorCanvasUtils.h"
#include "Paper2DPlusCharacterLayerAsset.h"
#include "Paper2DPlusSpriteSourceUtils.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Paper2DPlusLayerDraw.h" // U4: THE shared offset-math home
#include "PaperSprite.h"

void SLayerCompositeThumbnail::Construct(const FArguments& InArgs)
{
	LayerAsset = InArgs._LayerAsset;
	Model = InArgs._Model;
	FrameIndex = InArgs._FrameIndex;
	bDrawCheckerboard = InArgs._DrawCheckerboard;
	bEnableLayerPicking = InArgs._EnableLayerPicking;
	OnLayerPicked = InArgs._OnLayerPicked;
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
	// The hit cache is rebuilt by the next paint; the alpha cache is keyed by sprite and survives,
	// but entries for sprites that are no longer painted must not pin them.
	HitRegions.Reset();
	AlphaCache.Reset();
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
		Item.LayerId = Layer->LayerId;
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

	HitRegions.Reset();
	HitRegions.Reserve(CachedPaintItems.Num());

	for (const FCachedPaintItem& Item : CachedPaintItems)
	{
		UPaperSprite* Sprite = Item.Sprite.Get();
		if (!Sprite) continue;
		// U4 (D2): the per-item TOTAL offset shifts the draw through THIS cell's paint transform (center +
		// OffsetPx * Scale) — AnimData stays null so DrawFlipbookSprite can't double-apply the SpriteOffset.
		const bool bHovered = bEnableLayerPicking && Item.LayerId.IsValid() && Item.LayerId == HoveredLayerId;
		// Named per-ITEM: OnPaint already has a DrawSize for the fitted composite, and shadowing it
		// is both a C4456 error and a real readability trap.
		FVector2D ItemDrawPos = FVector2D::ZeroVector;
		FVector2D ItemDrawSize = FVector2D::ZeroVector;
		FEditorCanvasUtils::DrawFlipbookSprite(
			OutDrawElements, LayerId, AllottedGeometry,
			Sprite, nullptr, FrameIndex,
			Center + Item.OffsetPx * Scale, Scale, FLinearColor::White,
			&ItemDrawPos, &ItemDrawSize);
		LayerId++;

		// Hover highlight = a SECOND translucent pass of the same sprite, masked by that sprite's own
		// alpha. Two reasons it has to be a second pass rather than a brighter tint on the first:
		//   * Slate packs an element's tint through PackVertexColor into an FColor, so any channel
		//     above 1.0 clamps and a "brighten" tint draws identically to white - invisible.
		//   * Every layer here is full-frame, so a bounding box would outline the whole character no
		//     matter which layer is under the cursor. Tinting the art is the only honest highlight.
		if (bHovered)
		{
			FEditorCanvasUtils::DrawFlipbookSprite(
				OutDrawElements, LayerId, AllottedGeometry,
				Sprite, nullptr, FrameIndex,
				Center + Item.OffsetPx * Scale, Scale,
				FLinearColor(0.35f, 0.72f, 1.0f, 0.45f));
			LayerId++;
		}

		if (bEnableLayerPicking && Item.LayerId.IsValid() && ItemDrawSize.X > 0.0f && ItemDrawSize.Y > 0.0f)
		{
			FLayerHitRegion& Region = HitRegions.AddDefaulted_GetRef();
			Region.DrawPos = ItemDrawPos;
			Region.DrawSize = ItemDrawSize;
			Region.Sprite = Item.Sprite;
			Region.LayerId = Item.LayerId;
		}
	}

	return LayerId;
}

// ============================================================================
// Layer picking — hover to highlight, click to select (restores the affordance the
// schema-v5 overhaul removed along with the Part/Variant model it used to drive)
// ============================================================================
//
// TWO rules decide whether this feels right or maddening:
//   * ALPHA, not bounds. Character layers are full-frame, so every layer's rect covers the whole
//     composite. A bounds test would always resolve to the topmost layer.
//   * A point transparent in EVERY visible layer resolves to NO layer. The earlier version of this
//     feature fell back to the topmost rect, which made every click on empty space open the
//     topmost layer's menu.

bool SLayerCompositeThumbnail::IsSpriteOpaqueAtLocal(
	const FLayerHitRegion& Region, const FVector2D& LocalPos) const
{
	UPaperSprite* Sprite = Region.Sprite.Get();
	if (!Sprite || Region.DrawSize.X <= 0.0f || Region.DrawSize.Y <= 0.0f)
	{
		return false;
	}
	// Outside the drawn rect is trivially not this layer.
	const FVector2D Normalized = (LocalPos - Region.DrawPos) / Region.DrawSize;
	if (Normalized.X < 0.0f || Normalized.X >= 1.0f || Normalized.Y < 0.0f || Normalized.Y >= 1.0f)
	{
		return false;
	}

	FSpriteAlphaMip& Mip = AlphaCache.FindOrAdd(Region.Sprite);
	if (!Mip.bValid && Mip.Alpha.IsEmpty())
	{
		TArray<FColor> Pixels;
		int32 Width = 0;
		int32 Height = 0;
		// FAILS CLOSED: an unreadable source (non-BGRA8, no editor source data) leaves bValid false
		// and this layer simply never wins a hit, rather than swallowing the whole canvas.
		if (Paper2DPlusSpriteSourceUtils::ReadSourceRegion(Sprite, Pixels, Width, Height)
			&& Width > 0 && Height > 0 && Pixels.Num() >= Width * Height)
		{
			Mip.Width = Width;
			Mip.Height = Height;
			Mip.Alpha.SetNumUninitialized(Width * Height);
			for (int32 Index = 0; Index < Width * Height; ++Index)
			{
				Mip.Alpha[Index] = Pixels[Index].A;
			}
			Mip.bValid = true;
		}
		else
		{
			// Mark the failure so we do not retry the decode on every mouse move.
			Mip.Alpha.SetNum(1);
			Mip.bValid = false;
		}
	}
	if (!Mip.bValid)
	{
		return false;
	}

	const int32 TexelX = FMath::Clamp(static_cast<int32>(Normalized.X * Mip.Width), 0, Mip.Width - 1);
	const int32 TexelY = FMath::Clamp(static_cast<int32>(Normalized.Y * Mip.Height), 0, Mip.Height - 1);
	// A low alpha threshold rather than > 0: anti-aliased edges should not make a layer pickable a
	// pixel or two outside its silhouette.
	return Mip.Alpha[TexelY * Mip.Width + TexelX] > 16;
}

FGuid SLayerCompositeThumbnail::LayerUnderLocalPoint(const FVector2D& LocalPos) const
{
	// Topmost first: HitRegions is in paint order (bottom to top), so walk it backwards.
	for (int32 Index = HitRegions.Num() - 1; Index >= 0; --Index)
	{
		if (IsSpriteOpaqueAtLocal(HitRegions[Index], LocalPos))
		{
			return HitRegions[Index].LayerId;
		}
	}
	return FGuid();
}

FReply SLayerCompositeThumbnail::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (!bEnableLayerPicking)
	{
		return FReply::Unhandled();
	}
	const FVector2D Local = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
	const FGuid NewHovered = LayerUnderLocalPoint(Local);
	if (NewHovered != HoveredLayerId)
	{
		HoveredLayerId = NewHovered;
		Invalidate(EInvalidateWidgetReason::Paint);
	}
	// Unhandled on purpose: hovering must not eat input from anything hosting this canvas.
	return FReply::Unhandled();
}

void SLayerCompositeThumbnail::OnMouseLeave(const FPointerEvent& MouseEvent)
{
	SLeafWidget::OnMouseLeave(MouseEvent);
	if (HoveredLayerId.IsValid())
	{
		HoveredLayerId.Invalidate();
		Invalidate(EInvalidateWidgetReason::Paint);
	}
}

FReply SLayerCompositeThumbnail::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (!bEnableLayerPicking || MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return FReply::Unhandled();
	}
	const FVector2D Local = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
	// Only claim the press when it actually lands on a layer's art, so a click on empty canvas still
	// reaches whatever is hosting this widget.
	if (!LayerUnderLocalPoint(Local).IsValid())
	{
		return FReply::Unhandled();
	}
	bPickPressed = true;
	return FReply::Handled().CaptureMouse(SharedThis(this));
}

FReply SLayerCompositeThumbnail::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (!bPickPressed || MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return FReply::Unhandled();
	}
	bPickPressed = false;
	// Resolve again on release: a press-and-drag that ends over a different layer should select the
	// one the user let go over, and one that ends off the art should select nothing.
	const FVector2D Local = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
	const FGuid Picked = LayerUnderLocalPoint(Local);
	if (Picked.IsValid())
	{
		OnLayerPicked.ExecuteIfBound(Picked);
	}
	return FReply::Handled().ReleaseMouseCapture();
}

FCursorReply SLayerCompositeThumbnail::OnCursorQuery(
	const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const
{
	return (bEnableLayerPicking && HoveredLayerId.IsValid())
		? FCursorReply::Cursor(EMouseCursor::Hand)
		: FCursorReply::Unhandled();
}
