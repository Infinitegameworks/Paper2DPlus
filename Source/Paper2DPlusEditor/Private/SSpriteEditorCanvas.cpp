// Copyright 2026 Infinite Gameworks. All Rights Reserved.

// SSpriteEditorCanvas.cpp - Sprite editor canvas implementation
// Split from CharacterProfileAssetEditor.cpp for maintainability

#include "CharacterProfileAssetEditor.h"
#include "EditorCanvasUtils.h"
#include "SlateShortcutUtils.h"
#include "PaperSprite.h"
#include "PaperFlipbook.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Engine/Texture2D.h"

/** SSpriteEditorCanvas — Canvas widget for the Sprite Editor tab: frame preview, offset visualization, and sprite rendering. */

#define LOCTEXT_NAMESPACE "CharacterProfileAssetEditor"

// ==========================================
// SSpriteEditorCanvas Implementation
// ==========================================

void SSpriteEditorCanvas::Construct(const FArguments& InArgs)
{
	Asset = InArgs._Asset;
	SelectedFlipbookIndex = InArgs._SelectedFlipbookIndex;
	SelectedFrameIndex = InArgs._SelectedFrameIndex;
	Zoom = InArgs._Zoom;
	ShowOnionSkin = InArgs._ShowOnionSkin;
	OnionSkinFrames = InArgs._OnionSkinFrames;
	ForwardOnionSkinFrames = InArgs._ForwardOnionSkinFrames;
	OnionSkinOpacity = InArgs._OnionSkinOpacity;
	PreviousFlipbookIndex = InArgs._PreviousFlipbookIndex;
	ShowForwardOnionSkin = InArgs._ShowForwardOnionSkin;
	NextFlipbookIndex = InArgs._NextFlipbookIndex;
	ShowReticle = InArgs._ShowReticle;
	ReticlePosition = InArgs._ReticlePosition;
	FlipX = InArgs._FlipX;
	FlipY = InArgs._FlipY;
	ShowReferenceSprite = InArgs._ShowReferenceSprite;
	ReferenceSprite = InArgs._ReferenceSprite;
	ReferenceSpriteOffset = InArgs._ReferenceSpriteOffset;
	ReferenceSpriteOpacity = InArgs._ReferenceSpriteOpacity;
	QueueLargestDims = InArgs._QueueLargestDims;
	ExcludedPreviewSprite = InArgs._ExcludedPreviewSprite;
	ExcludedPreviewOffset = InArgs._ExcludedPreviewOffset;

	// Enable clipping so sprites don't render outside canvas bounds
	SetClipping(EWidgetClipping::ClipToBounds);
}

FVector2D SSpriteEditorCanvas::ComputeDesiredSize(float) const
{
	// Return a minimum size - the canvas will expand to fill available space
	// Drawing code uses Geom.GetLocalSize() which adapts to actual allocated size
	return FVector2D(100, 100);
}

const FFlipbookProfileEntry* SSpriteEditorCanvas::GetCurrentFlipbookData() const
{
	if (!Asset.IsValid()) return nullptr;
	int32 FlipbookIndex = SelectedFlipbookIndex.Get();
	if (!Asset->Flipbooks.IsValidIndex(FlipbookIndex)) return nullptr;
	return &Asset->Flipbooks[FlipbookIndex];
}

const FSpriteExtractionInfo* SSpriteEditorCanvas::GetCurrentExtractionInfo() const
{
	const FFlipbookProfileEntry* Anim = GetCurrentFlipbookData();
	if (!Anim) return nullptr;
	int32 FrameIndex = SelectedFrameIndex.Get();
	if (!Anim->CombatData.FrameExtractionInfo.IsValidIndex(FrameIndex)) return nullptr;
	return &Anim->CombatData.FrameExtractionInfo[FrameIndex];
}

UPaperSprite* SSpriteEditorCanvas::GetSpriteAtFrame(int32 FrameIndex) const
{
	const FFlipbookProfileEntry* Anim = GetCurrentFlipbookData();
	if (!Anim || !Anim->Identity.Flipbook.IsValid()) return nullptr;

	UPaperFlipbook* Flipbook = Anim->Identity.Flipbook.LoadSynchronous();
	if (!Flipbook) return nullptr;

	if (FrameIndex < 0 || FrameIndex >= Flipbook->GetNumKeyFrames()) return nullptr;

	const FPaperFlipbookKeyFrame& KeyFrame = Flipbook->GetKeyFrameChecked(FrameIndex);
	return KeyFrame.Sprite;
}

FIntPoint SSpriteEditorCanvas::GetOffsetAtFrame(int32 FrameIndex) const
{
	const FFlipbookProfileEntry* Anim = GetCurrentFlipbookData();
	if (!Anim || !Anim->CombatData.FrameExtractionInfo.IsValidIndex(FrameIndex))
	{
		return FIntPoint::ZeroValue;
	}
	return Anim->CombatData.FrameExtractionInfo[FrameIndex].SpriteOffset;
}

FVector2D SSpriteEditorCanvas::GetPivotShift(UPaperSprite* Sprite) const
{
	if (!Sprite) return FVector2D::ZeroVector;

	// Compute how much the sprite's custom pivot shifts it from its default center.
	// When the pivot is moved UP, the sprite renders LOWER (and vice versa).
	// This matches the Profile-owned SpriteOffset/pivot draw convention used by current previews.
	FVector2D SourceCenter = Sprite->GetSourceUV() + Sprite->GetSourceSize() * 0.5f;
	FVector2D PivotPos = Sprite->GetPivotPosition();
	return SourceCenter - PivotPos;
}

FIntPoint SSpriteEditorCanvas::GetLargestSpriteDims() const
{
	int32 FlipbookIdx = SelectedFlipbookIndex.Get(-1);

	const FFlipbookProfileEntry* Anim = GetCurrentFlipbookData();
	UPaperFlipbook* FB = (Anim && Anim->Identity.Flipbook.IsValid()) ? Anim->Identity.Flipbook.Get() : nullptr;

	if (CachedLargestDimsFlipbookIndex == FlipbookIdx && CachedLargestDimsFlipbook.IsValid() && CachedLargestDimsFlipbook.Get() == FB && CachedLargestDims.X > 0)
	{
		return CachedLargestDims;
	}

	if (!FB)
	{
		CachedLargestDims = FIntPoint(128, 128);
		CachedLargestDimsFlipbookIndex = FlipbookIdx;
		CachedLargestDimsFlipbook = nullptr;
		return CachedLargestDims;
	}

	FIntPoint Largest(1, 1);
	for (int32 i = 0; i < FB->GetNumKeyFrames(); ++i)
	{
		if (UPaperSprite* S = FB->GetKeyFrameChecked(i).Sprite)
		{
			FVector2D Sz = S->GetSourceSize();
			Largest.X = FMath::Max(Largest.X, FMath::RoundToInt(Sz.X));
			Largest.Y = FMath::Max(Largest.Y, FMath::RoundToInt(Sz.Y));
		}
	}

	CachedLargestDims = Largest;
	CachedLargestDimsFlipbookIndex = FlipbookIdx;
	CachedLargestDimsFlipbook = FB;
	return CachedLargestDims;
}

FVector2D SSpriteEditorCanvas::GetCanvasCenter(const FGeometry& Geom) const
{
	FVector2D LiveCenter = Geom.GetLocalSize() * 0.5f + PanOffset;

	FIntPoint QDims = QueueLargestDims.Get(FIntPoint::ZeroValue);
	if (QDims.X > 0 && QDims.Y > 0)
	{
		if (!bCenterLocked)
		{
			LockedCenter = LiveCenter;
			bCenterLocked = true;
		}
		return LockedCenter;
	}

	bCenterLocked = false;
	return LiveCenter;
}

float SSpriteEditorCanvas::GetEffectiveZoom() const
{
	// When a playback queue is active, use stable dims across all queued flipbooks
	FIntPoint QDims = QueueLargestDims.Get(FIntPoint::ZeroValue);
	FIntPoint Dims = (QDims.X > 0 && QDims.Y > 0) ? QDims : GetLargestSpriteDims();
	FVector2D WidgetSize = GetCachedGeometry().GetLocalSize();

	// Before first layout, widget size is 0 — fall back to raw zoom
	if (WidgetSize.X <= 0 || WidgetSize.Y <= 0 || Dims.X <= 0 || Dims.Y <= 0)
	{
		return Zoom.Get();
	}

	// Auto-fit: scale so largest sprite fills 80% of the widget, then apply user zoom
	float BaseScale = FMath::Min(
		WidgetSize.X / (float)Dims.X,
		WidgetSize.Y / (float)Dims.Y
	) * 0.8f;

	return BaseScale * Zoom.Get();
}

FVector2D SSpriteEditorCanvas::ScreenToCanvas(const FGeometry& Geom, const FVector2D& ScreenPos) const
{
	FVector2D LocalPos = Geom.AbsoluteToLocal(ScreenPos);
	FVector2D Center = GetCanvasCenter(Geom);
	float EffectiveZoom = GetEffectiveZoom();
	return (LocalPos - Center) / EffectiveZoom;
}

FVector2D SSpriteEditorCanvas::CanvasToScreen(const FGeometry& Geom, const FVector2D& CanvasPos) const
{
	FVector2D Center = GetCanvasCenter(Geom);
	float EffectiveZoom = GetEffectiveZoom();
	return Center + CanvasPos * EffectiveZoom;
}

FVector2D SSpriteEditorCanvas::GetReticleScreenPosition(const FGeometry& Geom) const
{
	// Reticle position is in canvas space (pixels from origin). Convert to screen space.
	return CanvasToScreen(Geom, ReticlePosition.Get());
}

int32 SSpriteEditorCanvas::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId,
	const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	// Draw checkerboard background
	DrawCheckerboard(AllottedGeometry, OutDrawElements, LayerId);
	LayerId++;

	// Draw onion skin if enabled (backward — blue/purple)
	if (ShowOnionSkin.Get())
	{
		DrawOnionSkin(AllottedGeometry, OutDrawElements, LayerId);
		LayerId++;
	}

	// Draw forward onion skin if enabled (green)
	if (ShowForwardOnionSkin.Get())
	{
		DrawForwardOnionSkin(AllottedGeometry, OutDrawElements, LayerId);
		LayerId++;
	}

	// Draw reference sprite if enabled
	if (ShowReferenceSprite.Get())
	{
		DrawReferenceSprite(AllottedGeometry, OutDrawElements, LayerId);
		LayerId++;
	}

	// Draw current sprite — or excluded preview if one is selected
	TWeakObjectPtr<UPaperSprite> ExclSprite = ExcludedPreviewSprite.Get();
	if (ExclSprite.IsValid())
	{
		// Show excluded frame greyed out
		FIntPoint ExclOffset = ExcludedPreviewOffset.Get();
		DrawSprite(AllottedGeometry, OutDrawElements, LayerId, ExclSprite.Get(), ExclOffset, FlipX.Get(), FlipY.Get(), FLinearColor(0.4f, 0.4f, 0.4f, 0.6f));
		LayerId++;
		DrawSpriteBounds(AllottedGeometry, OutDrawElements, LayerId, ExclSprite.Get(), ExclOffset);
		LayerId++;
	}
	else
	{
		int32 CurrentFrame = SelectedFrameIndex.Get();
		UPaperSprite* CurrentSprite = GetSpriteAtFrame(CurrentFrame);
		FIntPoint CurrentOffset = GetOffsetAtFrame(CurrentFrame);
		if (CurrentSprite)
		{
			DrawSprite(AllottedGeometry, OutDrawElements, LayerId, CurrentSprite, CurrentOffset, FlipX.Get(), FlipY.Get(), FLinearColor::White);
			LayerId++;
			DrawSpriteBounds(AllottedGeometry, OutDrawElements, LayerId, CurrentSprite, CurrentOffset);
			LayerId++;
		}
	}

	// Draw reticle (if enabled)
	if (ShowReticle.Get())
	{
		DrawReticle(AllottedGeometry, OutDrawElements, LayerId);
		LayerId++;
	}

	return LayerId;
}

void SSpriteEditorCanvas::DrawCheckerboard(const FGeometry& Geom, FSlateWindowElementList& OutDrawElements, int32 LayerId) const
{
	FEditorCanvasUtils::DrawCheckerboard(OutDrawElements, LayerId, Geom, CheckerSize);
}

void SSpriteEditorCanvas::DrawSpriteBounds(const FGeometry& Geom, FSlateWindowElementList& OutDrawElements, int32 LayerId,
	UPaperSprite* Sprite, FIntPoint Offset) const
{
	if (!Sprite) return;

	float EffectiveZoom = GetEffectiveZoom();
	FVector2D Center = GetCanvasCenter(Geom);

	FVector2D SpriteDims = Sprite->GetSourceSize();
	FVector2D BoxSize(SpriteDims.X * EffectiveZoom, SpriteDims.Y * EffectiveZoom);

	FVector2D PivotInSprite(Sprite->GetPivotPosition() - Sprite->GetSourceUV());
	FIntPoint QDims = QueueLargestDims.Get(FIntPoint::ZeroValue);
	FVector2D BoxTopLeft;
	if (QDims.X > 0 && QDims.Y > 0)
	{
		FVector2D RefSize(QDims.X * EffectiveZoom, QDims.Y * EffectiveZoom);
		FVector2D RefTopLeft = Center - RefSize * 0.5f;
		FVector2D RefPivot(RefSize.X * 0.5f, RefSize.Y);
		BoxTopLeft.X = FMath::RoundToFloat(RefTopLeft.X + RefPivot.X - PivotInSprite.X * EffectiveZoom + Offset.X * EffectiveZoom);
		BoxTopLeft.Y = FMath::RoundToFloat(RefTopLeft.Y + RefPivot.Y - PivotInSprite.Y * EffectiveZoom + Offset.Y * EffectiveZoom);
	}
	else
	{
		BoxTopLeft.X = FMath::RoundToFloat(Center.X + (Offset.X - PivotInSprite.X) * EffectiveZoom);
		BoxTopLeft.Y = FMath::RoundToFloat(Center.Y + (Offset.Y - PivotInSprite.Y) * EffectiveZoom);
	}

	// Draw 4-edge pixel-perfect outline (cyan, semi-transparent)
	const FLinearColor OutlineColor(0.0f, 0.8f, 1.0f, 0.6f);
	const float EdgeThickness = 1.0f;
	const FSlateBrush* WhiteBrush = FAppStyle::Get().GetBrush("WhiteBrush");

	// Top edge
	FSlateDrawElement::MakeBox(OutDrawElements, LayerId,
		MakePaintGeometry(Geom, FVector2D(BoxSize.X, EdgeThickness), FSlateLayoutTransform(FVector2D(BoxTopLeft))),
		WhiteBrush, ESlateDrawEffect::None, OutlineColor);
	// Bottom edge
	FSlateDrawElement::MakeBox(OutDrawElements, LayerId,
		MakePaintGeometry(Geom, FVector2D(BoxSize.X, EdgeThickness), FSlateLayoutTransform(FVector2D(BoxTopLeft.X, BoxTopLeft.Y + BoxSize.Y - EdgeThickness))),
		WhiteBrush, ESlateDrawEffect::None, OutlineColor);
	// Left edge
	FSlateDrawElement::MakeBox(OutDrawElements, LayerId,
		MakePaintGeometry(Geom, FVector2D(EdgeThickness, BoxSize.Y), FSlateLayoutTransform(FVector2D(BoxTopLeft))),
		WhiteBrush, ESlateDrawEffect::None, OutlineColor);
	// Right edge
	FSlateDrawElement::MakeBox(OutDrawElements, LayerId,
		MakePaintGeometry(Geom, FVector2D(EdgeThickness, BoxSize.Y), FSlateLayoutTransform(FVector2D(BoxTopLeft.X + BoxSize.X - EdgeThickness, BoxTopLeft.Y))),
		WhiteBrush, ESlateDrawEffect::None, OutlineColor);
}

void SSpriteEditorCanvas::DrawSprite(const FGeometry& Geom, FSlateWindowElementList& OutDrawElements, int32 LayerId,
	UPaperSprite* Sprite, FIntPoint Offset, bool bInFlipX, bool bInFlipY, FLinearColor Tint) const
{
	if (!Sprite) return;

	// Get sprite texture
	UTexture2D* SpriteTexture = Sprite->GetBakedTexture();
	if (!SpriteTexture)
	{
		SpriteTexture = Cast<UTexture2D>(Sprite->GetSourceTexture());
	}
	if (!SpriteTexture) return;

	float EffectiveZoom = GetEffectiveZoom();
	FVector2D Center = GetCanvasCenter(Geom);

	// Get sprite dimensions
	FVector2D SpriteDims = Sprite->GetSourceSize();
	FVector2D DrawSize(SpriteDims.X * EffectiveZoom, SpriteDims.Y * EffectiveZoom);

	// Position the sprite so its pivot maps to a stable reference point.
	// During queue playback, use QueueLargestDims as the reference frame:
	// all sprites are placed within a virtual box of that size, with pivots
	// aligned to its bottom-center. This makes positioning independent of
	// individual sprite dimensions and immune to canvas geometry changes.
	FVector2D PivotInSprite(Sprite->GetPivotPosition() - Sprite->GetSourceUV());
	FIntPoint QDims = QueueLargestDims.Get(FIntPoint::ZeroValue);
	FVector2D DrawPos;
	if (QDims.X > 0 && QDims.Y > 0)
	{
		// Queue active: anchor from center of the fixed reference frame.
		// The reference frame is centered on the canvas, sized to QueueLargestDims.
		FVector2D RefSize(QDims.X * EffectiveZoom, QDims.Y * EffectiveZoom);
		FVector2D RefTopLeft = Center - RefSize * 0.5f;
		// Place sprite so its pivot aligns with reference bottom-center
		FVector2D RefPivot(RefSize.X * 0.5f, RefSize.Y);
		DrawPos.X = FMath::RoundToFloat(RefTopLeft.X + RefPivot.X - PivotInSprite.X * EffectiveZoom + Offset.X * EffectiveZoom);
		DrawPos.Y = FMath::RoundToFloat(RefTopLeft.Y + RefPivot.Y - PivotInSprite.Y * EffectiveZoom + Offset.Y * EffectiveZoom);
	}
	else
	{
		DrawPos.X = FMath::RoundToFloat(Center.X + (Offset.X - PivotInSprite.X) * EffectiveZoom);
		DrawPos.Y = FMath::RoundToFloat(Center.Y + (Offset.Y - PivotInSprite.Y) * EffectiveZoom);
	}

	// Create brush
	FSlateBrush SpriteBrush;
	SpriteBrush.SetResourceObject(SpriteTexture);
	SpriteBrush.ImageSize = FVector2D(SpriteTexture->GetSizeX(), SpriteTexture->GetSizeY());
	SpriteBrush.DrawAs = ESlateBrushDrawType::Image;
	SpriteBrush.Tiling = ESlateBrushTileType::NoTile;

	// Set UV region to display only this sprite's portion of the texture
	FVector2D SourceUV = Sprite->GetSourceUV();
	FVector2D SourceSize = Sprite->GetSourceSize();
	FVector2D TextureSize(SpriteTexture->GetSizeX(), SpriteTexture->GetSizeY());
	if (TextureSize.X > 0 && TextureSize.Y > 0)
	{
		FBox2D UVRegion(
			FVector2D(SourceUV.X / TextureSize.X, SourceUV.Y / TextureSize.Y),
			FVector2D((SourceUV.X + SourceSize.X) / TextureSize.X, (SourceUV.Y + SourceSize.Y) / TextureSize.Y)
		);
		SpriteBrush.SetUVRegion(UVRegion);
	}

	const FVector2D RenderScale(bInFlipX ? -1.0f : 1.0f, bInFlipY ? -1.0f : 1.0f);
	const FVector2D RenderOffset(
		bInFlipX ? (DrawPos.X + DrawSize.X) : DrawPos.X,
		bInFlipY ? (DrawPos.Y + DrawSize.Y) : DrawPos.Y);

	FSlateDrawElement::MakeBox(
		OutDrawElements,
		LayerId,
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 2
		Geom.ToPaintGeometry(FVector2D(DrawSize), FSlateLayoutTransform(1.0f, FVector2D(RenderOffset)), FSlateRenderTransform(FScale2D(RenderScale.X, RenderScale.Y)), FVector2D(0.0f, 0.0f)),
#else
		Geom.ToPaintGeometry(FSlateVector2(DrawSize), FSlateLayoutTransform(1.0f, FVector2D(RenderOffset)), FSlateRenderTransform(FScale2f(RenderScale.X, RenderScale.Y)), FSlateVector2(0.0f, 0.0f)),
#endif
		&SpriteBrush,
		ESlateDrawEffect::None,
		Tint
	);
}

void SSpriteEditorCanvas::DrawOnionSkin(const FGeometry& Geom, FSlateWindowElementList& OutDrawElements, int32 LayerId) const
{
	int32 CurrentFrame = SelectedFrameIndex.Get();
	int32 NumOnionFrames = OnionSkinFrames.Get();
	float BaseOpacity = OnionSkinOpacity.Get();

	int32 FramesDrawn = 0;

	// Draw onion skins from current flipbook
	for (int32 i = 1; i <= NumOnionFrames; i++)
	{
		int32 PrevFrame = CurrentFrame - i;
		if (PrevFrame < 0) break;

		UPaperSprite* PrevSprite = GetSpriteAtFrame(PrevFrame);
		if (!PrevSprite) continue;

		FIntPoint PrevOffset = GetOffsetAtFrame(PrevFrame);

		float Opacity = BaseOpacity * (1.0f - (float)(FramesDrawn) / (float)NumOnionFrames);
		FLinearColor OnionColor(0.5f, 0.5f, 1.0f, Opacity);

		DrawSprite(Geom, OutDrawElements, LayerId, PrevSprite, PrevOffset, FlipX.Get(), FlipY.Get(), OnionColor);
		FramesDrawn++;
	}

	// Cross-flipbook onion skin: if we ran out of frames (current frame near 0),
	// continue into previous flipbook's trailing frames
	int32 RemainingOnionFrames = NumOnionFrames - FramesDrawn;
	int32 PrevFlipbookIdx = PreviousFlipbookIndex.Get();

	if (RemainingOnionFrames > 0 && PrevFlipbookIdx != INDEX_NONE && Asset.IsValid()
		&& Asset->Flipbooks.IsValidIndex(PrevFlipbookIdx))
	{
		const FFlipbookProfileEntry& PrevFlipbookData = Asset->Flipbooks[PrevFlipbookIdx];
		UPaperFlipbook* PrevFB = nullptr;
		if (!PrevFlipbookData.Identity.Flipbook.IsNull())
		{
			PrevFB = PrevFlipbookData.Identity.Flipbook.LoadSynchronous();
		}

		if (PrevFB && PrevFB->GetNumKeyFrames() > 0)
		{
			int32 PrevFlipbookFrameCount = PrevFB->GetNumKeyFrames();

			for (int32 i = 0; i < RemainingOnionFrames; i++)
			{
				int32 FrameIdx = PrevFlipbookFrameCount - 1 - i;
				if (FrameIdx < 0 || FrameIdx >= PrevFB->GetNumKeyFrames()) break;

				const FPaperFlipbookKeyFrame& KeyFrame = PrevFB->GetKeyFrameChecked(FrameIdx);
				UPaperSprite* PrevSprite = KeyFrame.Sprite;
				if (!PrevSprite) continue;

				FIntPoint PrevOffset = FIntPoint::ZeroValue;
				if (PrevFlipbookData.CombatData.FrameExtractionInfo.IsValidIndex(FrameIdx))
				{
					PrevOffset = PrevFlipbookData.CombatData.FrameExtractionInfo[FrameIdx].SpriteOffset;
				}

				float Opacity = BaseOpacity * (1.0f - (float)(FramesDrawn) / (float)NumOnionFrames);
				// Purple tint for cross-flipbook frames (distinct from blue)
				FLinearColor OnionColor(0.7f, 0.4f, 1.0f, Opacity);

				DrawSprite(Geom, OutDrawElements, LayerId, PrevSprite, PrevOffset, FlipX.Get(), FlipY.Get(), OnionColor);
				FramesDrawn++;
			}
		}
	}
}

void SSpriteEditorCanvas::DrawForwardOnionSkin(const FGeometry& Geom, FSlateWindowElementList& OutDrawElements, int32 LayerId) const
{
	int32 CurrentFrame = SelectedFrameIndex.Get();
	int32 NumOnionFrames = ForwardOnionSkinFrames.Get();
	float BaseOpacity = OnionSkinOpacity.Get();

	const FFlipbookProfileEntry* Anim = GetCurrentFlipbookData();
	if (!Anim || Anim->Identity.Flipbook.IsNull()) return;

	UPaperFlipbook* Flipbook = Anim->Identity.Flipbook.LoadSynchronous();
	if (!Flipbook) return;

	int32 TotalFrames = Flipbook->GetNumKeyFrames();
	int32 FramesDrawn = 0;

	// Draw forward onion skins from current flipbook
	for (int32 i = 1; i <= NumOnionFrames; i++)
	{
		int32 NextFrame = CurrentFrame + i;
		if (NextFrame >= TotalFrames) break;

		UPaperSprite* NextSprite = GetSpriteAtFrame(NextFrame);
		if (!NextSprite) continue;

		FIntPoint NextOffset = GetOffsetAtFrame(NextFrame);

		float Opacity = BaseOpacity * (1.0f - (float)(FramesDrawn) / (float)NumOnionFrames);
		// Green tint for forward onion skin
		FLinearColor OnionColor(0.5f, 1.0f, 0.5f, Opacity);

		DrawSprite(Geom, OutDrawElements, LayerId, NextSprite, NextOffset, FlipX.Get(), FlipY.Get(), OnionColor);
		FramesDrawn++;
	}

	// Cross-flipbook forward onion skin: if we ran out of frames (current frame near end),
	// continue into next flipbook's leading frames
	int32 RemainingOnionFrames = NumOnionFrames - FramesDrawn;
	int32 NextFlipbookIdx = NextFlipbookIndex.Get();

	if (RemainingOnionFrames > 0 && NextFlipbookIdx != INDEX_NONE && Asset.IsValid()
		&& Asset->Flipbooks.IsValidIndex(NextFlipbookIdx))
	{
		const FFlipbookProfileEntry& NextFlipbookData = Asset->Flipbooks[NextFlipbookIdx];
		UPaperFlipbook* NextFB = nullptr;
		if (!NextFlipbookData.Identity.Flipbook.IsNull())
		{
			NextFB = NextFlipbookData.Identity.Flipbook.LoadSynchronous();
		}

		if (NextFB && NextFB->GetNumKeyFrames() > 0)
		{
			for (int32 i = 0; i < RemainingOnionFrames; i++)
			{
				int32 FrameIdx = i;
				if (FrameIdx >= NextFB->GetNumKeyFrames()) break;

				const FPaperFlipbookKeyFrame& KeyFrame = NextFB->GetKeyFrameChecked(FrameIdx);
				UPaperSprite* NextSprite = KeyFrame.Sprite;
				if (!NextSprite) continue;

				FIntPoint NextOffset = FIntPoint::ZeroValue;
				if (NextFlipbookData.CombatData.FrameExtractionInfo.IsValidIndex(FrameIdx))
				{
					NextOffset = NextFlipbookData.CombatData.FrameExtractionInfo[FrameIdx].SpriteOffset;
				}

				float Opacity = BaseOpacity * (1.0f - (float)(FramesDrawn) / (float)NumOnionFrames);
				// Lighter green tint for cross-flipbook forward frames
				FLinearColor OnionColor(0.4f, 1.0f, 0.7f, Opacity);

				DrawSprite(Geom, OutDrawElements, LayerId, NextSprite, NextOffset, FlipX.Get(), FlipY.Get(), OnionColor);
				FramesDrawn++;
			}
		}
	}
}

void SSpriteEditorCanvas::DrawReferenceSprite(const FGeometry& Geom, FSlateWindowElementList& OutDrawElements, int32 LayerId) const
{
	UPaperSprite* RefSprite = ReferenceSprite.Get().Get();
	if (!RefSprite) return;

	FIntPoint RefOffset = ReferenceSpriteOffset.Get();
	float Opacity = ReferenceSpriteOpacity.Get();

	// Green tint to distinguish from blue onion skin
	FLinearColor RefColor(0.5f, 1.0f, 0.5f, Opacity);

	DrawSprite(Geom, OutDrawElements, LayerId, RefSprite, RefOffset, FlipX.Get(), FlipY.Get(), RefColor);
}

void SSpriteEditorCanvas::DrawReticle(const FGeometry& Geom, FSlateWindowElementList& OutDrawElements, int32 LayerId) const
{
	float EffectiveZoom = GetEffectiveZoom();
	FVector2D ReticlePos = GetReticleScreenPosition(Geom);

	// Draw crosshair at reticle position (based on selected anchor)
	float LineLength = 20.0f;
	FLinearColor ReticleColor(1.0f, 0.5f, 0.0f, 1.0f); // Orange

	// Horizontal line
	TArray<FVector2D> HLine;
	HLine.Add(FVector2D(ReticlePos.X - LineLength, ReticlePos.Y));
	HLine.Add(FVector2D(ReticlePos.X + LineLength, ReticlePos.Y));
	FSlateDrawElement::MakeLines(
		OutDrawElements,
		LayerId,
		Geom.ToPaintGeometry(),
		HLine,
		ESlateDrawEffect::None,
		ReticleColor,
		true,
		2.0f
	);

	// Vertical line
	TArray<FVector2D> VLine;
	VLine.Add(FVector2D(ReticlePos.X, ReticlePos.Y - LineLength));
	VLine.Add(FVector2D(ReticlePos.X, ReticlePos.Y + LineLength));
	FSlateDrawElement::MakeLines(
		OutDrawElements,
		LayerId,
		Geom.ToPaintGeometry(),
		VLine,
		ESlateDrawEffect::None,
		ReticleColor,
		true,
		2.0f
	);

	// Draw circle around reticle position
	const int32 NumSegments = 16;
	float Radius = 10.0f;
	TArray<FVector2D> Circle;
	for (int32 i = 0; i <= NumSegments; i++)
	{
		float Angle = (float)i / (float)NumSegments * 2.0f * PI;
		Circle.Add(FVector2D(
			ReticlePos.X + FMath::Cos(Angle) * Radius,
			ReticlePos.Y + FMath::Sin(Angle) * Radius
		));
	}
	FSlateDrawElement::MakeLines(
		OutDrawElements,
		LayerId,
		Geom.ToPaintGeometry(),
		Circle,
		ESlateDrawEffect::None,
		ReticleColor,
		true,
		1.5f
	);
}

FReply SSpriteEditorCanvas::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		// Alt+left-click: drag to reposition the reticle
		if (MouseEvent.IsAltDown() && ShowReticle.Get())
		{
			bIsDraggingReticle = true;
			FVector2D CanvasPos = ScreenToCanvas(MyGeometry, MouseEvent.GetScreenSpacePosition());
			OnReticlePositionChanged.ExecuteIfBound(CanvasPos);
			return FReply::Handled().CaptureMouse(SharedThis(this));
		}

		// Start dragging to adjust offset
		bIsDragging = true;
		DragStart = MouseEvent.GetScreenSpacePosition();

		const FSpriteExtractionInfo* ExtractInfo = GetCurrentExtractionInfo();
		OffsetAtDragStart = ExtractInfo ? ExtractInfo->SpriteOffset : FIntPoint::ZeroValue;

		OnDragStarted.ExecuteIfBound();

		return FReply::Handled().CaptureMouse(SharedThis(this));
	}
	else if (MouseEvent.GetEffectingButton() == EKeys::MiddleMouseButton ||
			 MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		// Start panning
		bIsPanning = true;
		PanStart = MouseEvent.GetScreenSpacePosition();
		return FReply::Handled().CaptureMouse(SharedThis(this));
	}

	return FReply::Unhandled();
}

FReply SSpriteEditorCanvas::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (bIsDraggingReticle && MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		bIsDraggingReticle = false;
		return FReply::Handled().ReleaseMouseCapture();
	}
	if (bIsDragging && MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		bIsDragging = false;
		OnDragEnded.ExecuteIfBound();
		return FReply::Handled().ReleaseMouseCapture();
	}
	else if (bIsPanning && (MouseEvent.GetEffectingButton() == EKeys::MiddleMouseButton ||
							MouseEvent.GetEffectingButton() == EKeys::RightMouseButton))
	{
		bIsPanning = false;
		return FReply::Handled().ReleaseMouseCapture();
	}

	return FReply::Unhandled();
}

FReply SSpriteEditorCanvas::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (!HasMouseCapture())
	{
		return FReply::Unhandled();
	}
	if (bIsDraggingReticle)
	{
		FVector2D CanvasPos = ScreenToCanvas(MyGeometry, MouseEvent.GetScreenSpacePosition());
		OnReticlePositionChanged.ExecuteIfBound(CanvasPos);
		return FReply::Handled();
	}
	if (bIsDragging)
	{
		float EffectiveZoom = GetEffectiveZoom();
		FVector2D Delta = (MouseEvent.GetScreenSpacePosition() - DragStart) / EffectiveZoom;

		int32 DeltaX = FMath::RoundToInt(Delta.X);
		int32 DeltaY = FMath::RoundToInt(Delta.Y);

		// Calculate new offset from drag start
		int32 NewOffsetX = OffsetAtDragStart.X + DeltaX;
		int32 NewOffsetY = OffsetAtDragStart.Y + DeltaY;

		// Get current offset to compute actual delta (may be null for non-extracted anims)
		const FSpriteExtractionInfo* ExtractInfo = GetCurrentExtractionInfo();
		FIntPoint CurrentOffset = ExtractInfo ? ExtractInfo->SpriteOffset : OffsetAtDragStart;

		int32 ActualDeltaX = NewOffsetX - CurrentOffset.X;
		int32 ActualDeltaY = NewOffsetY - CurrentOffset.Y;

		if (ActualDeltaX != 0 || ActualDeltaY != 0)
		{
			OnOffsetChanged.ExecuteIfBound(ActualDeltaX, ActualDeltaY);
		}

		return FReply::Handled();
	}
	else if (bIsPanning)
	{
		FVector2D Delta = MouseEvent.GetScreenSpacePosition() - PanStart;
		PanOffset += Delta;
		if (bCenterLocked) LockedCenter += Delta;
		PanStart = MouseEvent.GetScreenSpacePosition();
		return FReply::Handled();
	}

	return FReply::Unhandled();
}

void SSpriteEditorCanvas::OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent)
{
	bool bWasDragging = bIsDragging;
	bIsDragging = false;
	bIsDraggingReticle = false;
	bIsPanning = false;
	DragStart = FVector2D::ZeroVector;
	OffsetAtDragStart = FIntPoint::ZeroValue;
	PanStart = FVector2D::ZeroVector;

	if (bWasDragging)
	{
		OnDragEnded.ExecuteIfBound();
	}

	Invalidate(EInvalidateWidgetReason::Paint);
}

FReply SSpriteEditorCanvas::OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	float ZoomDelta = MouseEvent.GetWheelDelta() * 0.1f;
	float NewZoom = FMath::Clamp(Zoom.Get() + ZoomDelta, 0.5f, 4.0f);

	OnZoomChanged.ExecuteIfBound(NewZoom);

	return FReply::Handled();
}

FReply SSpriteEditorCanvas::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (Paper2DPlusEditor::SlateShortcutUtils::ShouldIgnoreShortcutForFocusedWidget())
	{
		return FReply::Unhandled();
	}

	// Skip WASD nudging when Ctrl is held (allow Ctrl+S save, Ctrl+Shift+S save-all, etc.)
	if (InKeyEvent.IsControlDown())
	{
		return FReply::Unhandled();
	}

	// Shift modifier for 10px nudge, otherwise 1px
	int32 NudgeAmount = InKeyEvent.IsShiftDown() ? 10 : 1;

	// WASD for offset nudging (arrow keys handled by parent for navigation)
	if (InKeyEvent.GetKey() == EKeys::W)
	{
		OnOffsetChanged.ExecuteIfBound(0, -NudgeAmount);
		return FReply::Handled();
	}
	else if (InKeyEvent.GetKey() == EKeys::A)
	{
		OnOffsetChanged.ExecuteIfBound(-NudgeAmount, 0);
		return FReply::Handled();
	}
	else if (InKeyEvent.GetKey() == EKeys::S)
	{
		OnOffsetChanged.ExecuteIfBound(0, NudgeAmount);
		return FReply::Handled();
	}
	else if (InKeyEvent.GetKey() == EKeys::D)
	{
		OnOffsetChanged.ExecuteIfBound(NudgeAmount, 0);
		return FReply::Handled();
	}

	return FReply::Unhandled();
}

#undef LOCTEXT_NAMESPACE
