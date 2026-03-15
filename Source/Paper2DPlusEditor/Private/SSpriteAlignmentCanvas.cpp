// Copyright 2026 Infinite Gameworks. All Rights Reserved.

// SSpriteAlignmentCanvas.cpp - Sprite alignment canvas implementation
// Split from CharacterProfileAssetEditor.cpp for maintainability

#include "CharacterProfileAssetEditor.h"
#include "EditorCanvasUtils.h"
#include "PaperSprite.h"
#include "PaperFlipbook.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Engine/Texture2D.h"

#define LOCTEXT_NAMESPACE "CharacterProfileAssetEditor"

// ==========================================
// SSpriteAlignmentCanvas Implementation
// ==========================================

void SSpriteAlignmentCanvas::Construct(const FArguments& InArgs)
{
	Asset = InArgs._Asset;
	SelectedFlipbookIndex = InArgs._SelectedFlipbookIndex;
	SelectedFrameIndex = InArgs._SelectedFrameIndex;
	ShowGrid = InArgs._ShowGrid;
	Zoom = InArgs._Zoom;
	ShowOnionSkin = InArgs._ShowOnionSkin;
	OnionSkinFrames = InArgs._OnionSkinFrames;
	OnionSkinOpacity = InArgs._OnionSkinOpacity;
	PreviousFlipbookIndex = InArgs._PreviousFlipbookIndex;
	ShowForwardOnionSkin = InArgs._ShowForwardOnionSkin;
	NextFlipbookIndex = InArgs._NextFlipbookIndex;
	ShowReticle = InArgs._ShowReticle;
	ReticleAnchor = InArgs._ReticleAnchor;
	FlipX = InArgs._FlipX;
	FlipY = InArgs._FlipY;
	ShowReferenceSprite = InArgs._ShowReferenceSprite;
	ReferenceSprite = InArgs._ReferenceSprite;
	ReferenceSpriteOffset = InArgs._ReferenceSpriteOffset;
	ReferenceSpriteOpacity = InArgs._ReferenceSpriteOpacity;
	QueueLargestDims = InArgs._QueueLargestDims;

	// Enable clipping so sprites don't render outside canvas bounds
	SetClipping(EWidgetClipping::ClipToBounds);
}

FVector2D SSpriteAlignmentCanvas::ComputeDesiredSize(float) const
{
	// Return a minimum size - the canvas will expand to fill available space
	// Drawing code uses Geom.GetLocalSize() which adapts to actual allocated size
	return FVector2D(100, 100);
}

const FFlipbookHitboxData* SSpriteAlignmentCanvas::GetCurrentFlipbookData() const
{
	if (!Asset.IsValid()) return nullptr;
	int32 FlipbookIndex = SelectedFlipbookIndex.Get();
	if (!Asset->Flipbooks.IsValidIndex(FlipbookIndex)) return nullptr;
	return &Asset->Flipbooks[FlipbookIndex];
}

const FSpriteExtractionInfo* SSpriteAlignmentCanvas::GetCurrentExtractionInfo() const
{
	const FFlipbookHitboxData* Anim = GetCurrentFlipbookData();
	if (!Anim) return nullptr;
	int32 FrameIndex = SelectedFrameIndex.Get();
	if (!Anim->FrameExtractionInfo.IsValidIndex(FrameIndex)) return nullptr;
	return &Anim->FrameExtractionInfo[FrameIndex];
}

FSpriteExtractionInfo* SSpriteAlignmentCanvas::GetCurrentExtractionInfoMutable() const
{
	if (!Asset.IsValid()) return nullptr;
	int32 FlipbookIndex = SelectedFlipbookIndex.Get();
	if (!Asset->Flipbooks.IsValidIndex(FlipbookIndex)) return nullptr;

	FFlipbookHitboxData& Anim = Asset->Flipbooks[FlipbookIndex];
	int32 FrameIndex = SelectedFrameIndex.Get();
	if (!Anim.FrameExtractionInfo.IsValidIndex(FrameIndex)) return nullptr;
	return &Anim.FrameExtractionInfo[FrameIndex];
}

UPaperSprite* SSpriteAlignmentCanvas::GetSpriteAtFrame(int32 FrameIndex) const
{
	const FFlipbookHitboxData* Anim = GetCurrentFlipbookData();
	if (!Anim || !Anim->Flipbook.IsValid()) return nullptr;

	UPaperFlipbook* Flipbook = Anim->Flipbook.LoadSynchronous();
	if (!Flipbook) return nullptr;

	if (FrameIndex < 0 || FrameIndex >= Flipbook->GetNumKeyFrames()) return nullptr;

	const FPaperFlipbookKeyFrame& KeyFrame = Flipbook->GetKeyFrameChecked(FrameIndex);
	return KeyFrame.Sprite;
}

FIntPoint SSpriteAlignmentCanvas::GetOffsetAtFrame(int32 FrameIndex) const
{
	const FFlipbookHitboxData* Anim = GetCurrentFlipbookData();
	if (!Anim || !Anim->FrameExtractionInfo.IsValidIndex(FrameIndex))
	{
		return FIntPoint::ZeroValue;
	}
	return Anim->FrameExtractionInfo[FrameIndex].SpriteOffset;
}

FVector2D SSpriteAlignmentCanvas::GetPivotShift(UPaperSprite* Sprite) const
{
	if (!Sprite) return FVector2D::ZeroVector;

	// Compute how much the sprite's custom pivot shifts it from its default center.
	// When the pivot is moved UP, the sprite renders LOWER (and vice versa).
	// This matches the sign convention used by Apply Offsets to Flipbook.
	FVector2D SourceCenter = Sprite->GetSourceUV() + Sprite->GetSourceSize() * 0.5f;
	FVector2D PivotPos = Sprite->GetPivotPosition();
	return SourceCenter - PivotPos;
}

FIntPoint SSpriteAlignmentCanvas::GetLargestSpriteDims() const
{
	int32 FlipbookIdx = SelectedFlipbookIndex.Get(-1);

	const FFlipbookHitboxData* Anim = GetCurrentFlipbookData();
	UPaperFlipbook* FB = (Anim && Anim->Flipbook.IsValid()) ? Anim->Flipbook.Get() : nullptr;

	if (CachedLargestDimsFlipbookIndex == FlipbookIdx && CachedLargestDimsFlipbook.Get() == FB && CachedLargestDims.X > 0)
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

FVector2D SSpriteAlignmentCanvas::GetCanvasCenter(const FGeometry& Geom) const
{
	return Geom.GetLocalSize() * 0.5f + PanOffset;
}

float SSpriteAlignmentCanvas::GetEffectiveZoom() const
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

FVector2D SSpriteAlignmentCanvas::ScreenToCanvas(const FGeometry& Geom, const FVector2D& ScreenPos) const
{
	FVector2D LocalPos = Geom.AbsoluteToLocal(ScreenPos);
	FVector2D Center = GetCanvasCenter(Geom);
	float EffectiveZoom = GetEffectiveZoom();
	return (LocalPos - Center) / EffectiveZoom;
}

FVector2D SSpriteAlignmentCanvas::CanvasToScreen(const FGeometry& Geom, const FVector2D& CanvasPos) const
{
	FVector2D Center = GetCanvasCenter(Geom);
	float EffectiveZoom = GetEffectiveZoom();
	return Center + CanvasPos * EffectiveZoom;
}

FVector2D SSpriteAlignmentCanvas::GetReticlePosition(const FGeometry& Geom) const
{
	FVector2D Center = GetCanvasCenter(Geom);
	float EffectiveZoom = GetEffectiveZoom();
	FIntPoint Dims = GetLargestSpriteDims();
	FVector2D BoundsSize = FVector2D(Dims.X, Dims.Y) * EffectiveZoom;
	FVector2D BoundsTopLeft = Center - BoundsSize * 0.5f;

	// Account for current sprite's pivot shift + offset so reticle tracks actual render position
	int32 CurrentFrame = SelectedFrameIndex.Get();
	UPaperSprite* CurrentSprite = GetSpriteAtFrame(CurrentFrame);
	FIntPoint CurrentOffset = GetOffsetAtFrame(CurrentFrame);
	FVector2D ShiftPx = FVector2D::ZeroVector;
	if (CurrentSprite)
	{
		FVector2D PivotShift = GetPivotShift(CurrentSprite);
		ShiftPx = FVector2D(CurrentOffset.X + PivotShift.X, CurrentOffset.Y + PivotShift.Y) * EffectiveZoom;
	}

	ESpriteAnchor Anchor = ReticleAnchor.Get();

	FVector2D AnchorPos;
	switch (Anchor)
	{
		case ESpriteAnchor::TopLeft:      AnchorPos = BoundsTopLeft; break;
		case ESpriteAnchor::TopCenter:    AnchorPos = FVector2D(Center.X, BoundsTopLeft.Y); break;
		case ESpriteAnchor::TopRight:     AnchorPos = FVector2D(BoundsTopLeft.X + BoundsSize.X, BoundsTopLeft.Y); break;
		case ESpriteAnchor::CenterLeft:   AnchorPos = FVector2D(BoundsTopLeft.X, Center.Y); break;
		case ESpriteAnchor::Center:       AnchorPos = Center; break;
		case ESpriteAnchor::CenterRight:  AnchorPos = FVector2D(BoundsTopLeft.X + BoundsSize.X, Center.Y); break;
		case ESpriteAnchor::BottomLeft:   AnchorPos = FVector2D(BoundsTopLeft.X, BoundsTopLeft.Y + BoundsSize.Y); break;
		case ESpriteAnchor::BottomCenter: AnchorPos = FVector2D(Center.X, BoundsTopLeft.Y + BoundsSize.Y); break;
		case ESpriteAnchor::BottomRight:  AnchorPos = BoundsTopLeft + BoundsSize; break;
		case ESpriteAnchor::None:         AnchorPos = Center; break;
		default:                          AnchorPos = Center; break;
	}
	return AnchorPos + ShiftPx;
}

int32 SSpriteAlignmentCanvas::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId,
	const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	// Draw checkerboard background
	DrawCheckerboard(AllottedGeometry, OutDrawElements, LayerId);
	LayerId++;

	// Draw grid if enabled
	if (ShowGrid.Get())
	{
		DrawGrid(AllottedGeometry, OutDrawElements, LayerId);
		LayerId++;
	}

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

	// Draw current sprite
	int32 CurrentFrame = SelectedFrameIndex.Get();
	UPaperSprite* CurrentSprite = GetSpriteAtFrame(CurrentFrame);
	FIntPoint CurrentOffset = GetOffsetAtFrame(CurrentFrame);
	if (CurrentSprite)
	{
		DrawSprite(AllottedGeometry, OutDrawElements, LayerId, CurrentSprite, CurrentOffset, FlipX.Get(), FlipY.Get(), FLinearColor::White);
		LayerId++;

		// Draw sprite bounds outline
		DrawSpriteBounds(AllottedGeometry, OutDrawElements, LayerId, CurrentSprite, CurrentOffset);
		LayerId++;
	}

	// Draw reticle (if enabled)
	if (ShowReticle.Get())
	{
		DrawReticle(AllottedGeometry, OutDrawElements, LayerId);
		LayerId++;
	}

	// Draw offset indicator
	DrawOffsetIndicator(AllottedGeometry, OutDrawElements, LayerId);
	LayerId++;

	return LayerId;
}

void SSpriteAlignmentCanvas::DrawCheckerboard(const FGeometry& Geom, FSlateWindowElementList& OutDrawElements, int32 LayerId) const
{
	FEditorCanvasUtils::DrawCheckerboard(OutDrawElements, LayerId, Geom, CheckerSize);
}

void SSpriteAlignmentCanvas::DrawGrid(const FGeometry& Geom, FSlateWindowElementList& OutDrawElements, int32 LayerId) const
{
	const FVector2D LocalSize = Geom.GetLocalSize();
	float EffectiveZoom = GetEffectiveZoom();
	float GridStep = GridSize * EffectiveZoom;

	// Anchor grid origin to reticle position so lines pass through it
	FVector2D Origin = GetReticlePosition(Geom);

	FLinearColor GridColor(0.5f, 0.5f, 0.5f, 0.4f);

	// Draw vertical lines aligned to reticle
	for (float X = FMath::Fmod(Origin.X, GridStep); X < LocalSize.X; X += GridStep)
	{
		if (X < 0) continue;
		TArray<FVector2D> Points;
		Points.Add(FVector2D(X, 0));
		Points.Add(FVector2D(X, LocalSize.Y));
		FSlateDrawElement::MakeLines(
			OutDrawElements,
			LayerId,
			Geom.ToPaintGeometry(),
			Points,
			ESlateDrawEffect::None,
			GridColor,
			true,
			1.0f
		);
	}

	// Draw horizontal lines aligned to reticle
	for (float Y = FMath::Fmod(Origin.Y, GridStep); Y < LocalSize.Y; Y += GridStep)
	{
		if (Y < 0) continue;
		TArray<FVector2D> Points;
		Points.Add(FVector2D(0, Y));
		Points.Add(FVector2D(LocalSize.X, Y));
		FSlateDrawElement::MakeLines(
			OutDrawElements,
			LayerId,
			Geom.ToPaintGeometry(),
			Points,
			ESlateDrawEffect::None,
			GridColor,
			true,
			1.0f
		);
	}
}

void SSpriteAlignmentCanvas::DrawSpriteBounds(const FGeometry& Geom, FSlateWindowElementList& OutDrawElements, int32 LayerId,
	UPaperSprite* Sprite, FIntPoint Offset) const
{
	if (!Sprite) return;

	float EffectiveZoom = GetEffectiveZoom();
	FVector2D Center = GetCanvasCenter(Geom);

	FVector2D SpriteDims = Sprite->GetSourceSize();
	FVector2D PivotShift = GetPivotShift(Sprite);
	FVector2D BoxSize(SpriteDims.X * EffectiveZoom, SpriteDims.Y * EffectiveZoom);
	FVector2D BoxTopLeft = Center - BoxSize * 0.5f;
	BoxTopLeft.X += (Offset.X + PivotShift.X) * EffectiveZoom;
	BoxTopLeft.Y += (Offset.Y + PivotShift.Y) * EffectiveZoom;

	// Draw 4-edge pixel-perfect outline (cyan, semi-transparent)
	const FLinearColor OutlineColor(0.0f, 0.8f, 1.0f, 0.6f);
	const float EdgeThickness = 1.0f;
	const FSlateBrush* WhiteBrush = FAppStyle::GetBrush("WhiteBrush");

	// Top edge
	FSlateDrawElement::MakeBox(OutDrawElements, LayerId,
		Geom.ToPaintGeometry(FVector2f(BoxSize.X, EdgeThickness), FSlateLayoutTransform(FVector2f(BoxTopLeft))),
		WhiteBrush, ESlateDrawEffect::None, OutlineColor);
	// Bottom edge
	FSlateDrawElement::MakeBox(OutDrawElements, LayerId,
		Geom.ToPaintGeometry(FVector2f(BoxSize.X, EdgeThickness), FSlateLayoutTransform(FVector2f(BoxTopLeft.X, BoxTopLeft.Y + BoxSize.Y - EdgeThickness))),
		WhiteBrush, ESlateDrawEffect::None, OutlineColor);
	// Left edge
	FSlateDrawElement::MakeBox(OutDrawElements, LayerId,
		Geom.ToPaintGeometry(FVector2f(EdgeThickness, BoxSize.Y), FSlateLayoutTransform(FVector2f(BoxTopLeft))),
		WhiteBrush, ESlateDrawEffect::None, OutlineColor);
	// Right edge
	FSlateDrawElement::MakeBox(OutDrawElements, LayerId,
		Geom.ToPaintGeometry(FVector2f(EdgeThickness, BoxSize.Y), FSlateLayoutTransform(FVector2f(BoxTopLeft.X + BoxSize.X - EdgeThickness, BoxTopLeft.Y))),
		WhiteBrush, ESlateDrawEffect::None, OutlineColor);
}

void SSpriteAlignmentCanvas::DrawSprite(const FGeometry& Geom, FSlateWindowElementList& OutDrawElements, int32 LayerId,
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

	// Calculate position with offset + baked pivot shift
	FVector2D PivotShift = GetPivotShift(Sprite);
	FVector2D DrawPos = Center - DrawSize * 0.5f;
	DrawPos.X += (Offset.X + PivotShift.X) * EffectiveZoom;
	DrawPos.Y += (Offset.Y + PivotShift.Y) * EffectiveZoom;

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
		Geom.ToPaintGeometry(FVector2f(DrawSize), FSlateLayoutTransform(1.0f, FVector2D(RenderOffset)), FSlateRenderTransform(FScale2f(RenderScale.X, RenderScale.Y)), FVector2f(0.0f, 0.0f)),
		&SpriteBrush,
		ESlateDrawEffect::None,
		Tint
	);
}

void SSpriteAlignmentCanvas::DrawOnionSkin(const FGeometry& Geom, FSlateWindowElementList& OutDrawElements, int32 LayerId) const
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
		const FFlipbookHitboxData& PrevFlipbookData = Asset->Flipbooks[PrevFlipbookIdx];
		UPaperFlipbook* PrevFB = nullptr;
		if (!PrevFlipbookData.Flipbook.IsNull())
		{
			PrevFB = PrevFlipbookData.Flipbook.LoadSynchronous();
		}

		if (PrevFB && PrevFB->GetNumKeyFrames() > 0)
		{
			int32 PrevFlipbookFrameCount = PrevFB->GetNumKeyFrames();

			for (int32 i = 0; i < RemainingOnionFrames; i++)
			{
				int32 FrameIdx = PrevFlipbookFrameCount - 1 - i;
				if (FrameIdx < 0) break;

				const FPaperFlipbookKeyFrame& KeyFrame = PrevFB->GetKeyFrameChecked(FrameIdx);
				UPaperSprite* PrevSprite = KeyFrame.Sprite;
				if (!PrevSprite) continue;

				FIntPoint PrevOffset = FIntPoint::ZeroValue;
				if (PrevFlipbookData.FrameExtractionInfo.IsValidIndex(FrameIdx))
				{
					PrevOffset = PrevFlipbookData.FrameExtractionInfo[FrameIdx].SpriteOffset;
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

void SSpriteAlignmentCanvas::DrawForwardOnionSkin(const FGeometry& Geom, FSlateWindowElementList& OutDrawElements, int32 LayerId) const
{
	int32 CurrentFrame = SelectedFrameIndex.Get();
	int32 NumOnionFrames = OnionSkinFrames.Get();
	float BaseOpacity = OnionSkinOpacity.Get();

	const FFlipbookHitboxData* Anim = GetCurrentFlipbookData();
	if (!Anim || Anim->Flipbook.IsNull()) return;

	UPaperFlipbook* Flipbook = Anim->Flipbook.LoadSynchronous();
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
		const FFlipbookHitboxData& NextFlipbookData = Asset->Flipbooks[NextFlipbookIdx];
		UPaperFlipbook* NextFB = nullptr;
		if (!NextFlipbookData.Flipbook.IsNull())
		{
			NextFB = NextFlipbookData.Flipbook.LoadSynchronous();
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
				if (NextFlipbookData.FrameExtractionInfo.IsValidIndex(FrameIdx))
				{
					NextOffset = NextFlipbookData.FrameExtractionInfo[FrameIdx].SpriteOffset;
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

void SSpriteAlignmentCanvas::DrawReferenceSprite(const FGeometry& Geom, FSlateWindowElementList& OutDrawElements, int32 LayerId) const
{
	UPaperSprite* RefSprite = ReferenceSprite.Get().Get();
	if (!RefSprite) return;

	FIntPoint RefOffset = ReferenceSpriteOffset.Get();
	float Opacity = ReferenceSpriteOpacity.Get();

	// Green tint to distinguish from blue onion skin
	FLinearColor RefColor(0.5f, 1.0f, 0.5f, Opacity);

	DrawSprite(Geom, OutDrawElements, LayerId, RefSprite, RefOffset, FlipX.Get(), FlipY.Get(), RefColor);
}

void SSpriteAlignmentCanvas::DrawReticle(const FGeometry& Geom, FSlateWindowElementList& OutDrawElements, int32 LayerId) const
{
	float EffectiveZoom = GetEffectiveZoom();
	FVector2D ReticlePos = GetReticlePosition(Geom);

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

void SSpriteAlignmentCanvas::DrawOffsetIndicator(const FGeometry& Geom, FSlateWindowElementList& OutDrawElements, int32 LayerId) const
{
	const FSpriteExtractionInfo* ExtractInfo = GetCurrentExtractionInfo();
	if (!ExtractInfo) return;

	FIntPoint Offset = ExtractInfo->SpriteOffset;
	if (Offset.X == 0 && Offset.Y == 0) return;

	float EffectiveZoom = GetEffectiveZoom();
	FVector2D Center = GetCanvasCenter(Geom);

	// Get the baked pivot shift so the line goes from baked position to current position
	int32 CurrentFrame = SelectedFrameIndex.Get();
	UPaperSprite* CurrentSprite = GetSpriteAtFrame(CurrentFrame);
	FVector2D PivotShift = GetPivotShift(CurrentSprite);
	FVector2D BakedPos = Center + FVector2D(PivotShift.X * EffectiveZoom, PivotShift.Y * EffectiveZoom);

	// Draw line from baked position to current offset position
	FVector2D OffsetPos = BakedPos + FVector2D(Offset.X * EffectiveZoom, Offset.Y * EffectiveZoom);

	TArray<FVector2D> OffsetLine;
	OffsetLine.Add(BakedPos);
	OffsetLine.Add(OffsetPos);

	FSlateDrawElement::MakeLines(
		OutDrawElements,
		LayerId,
		Geom.ToPaintGeometry(),
		OffsetLine,
		ESlateDrawEffect::None,
		FLinearColor(0.0f, 1.0f, 0.0f, 0.8f),
		true,
		1.5f
	);

	// Draw small circle at offset position
	const int32 NumSegments = 8;
	float Radius = 5.0f;
	TArray<FVector2D> Circle;
	for (int32 i = 0; i <= NumSegments; i++)
	{
		float Angle = (float)i / (float)NumSegments * 2.0f * PI;
		Circle.Add(FVector2D(
			OffsetPos.X + FMath::Cos(Angle) * Radius,
			OffsetPos.Y + FMath::Sin(Angle) * Radius
		));
	}
	FSlateDrawElement::MakeLines(
		OutDrawElements,
		LayerId,
		Geom.ToPaintGeometry(),
		Circle,
		ESlateDrawEffect::None,
		FLinearColor(0.0f, 1.0f, 0.0f, 1.0f),
		true,
		1.5f
	);
}

FReply SSpriteAlignmentCanvas::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
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

FReply SSpriteAlignmentCanvas::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
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

FReply SSpriteAlignmentCanvas::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
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
		PanStart = MouseEvent.GetScreenSpacePosition();
		return FReply::Handled();
	}

	return FReply::Unhandled();
}

void SSpriteAlignmentCanvas::OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent)
{
	bool bWasDragging = bIsDragging;
	bIsDragging = false;
	bIsPanning = false;

	if (bWasDragging)
	{
		OnDragEnded.ExecuteIfBound();
	}

	Invalidate(EInvalidateWidgetReason::Paint);
}

FReply SSpriteAlignmentCanvas::OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	float ZoomDelta = MouseEvent.GetWheelDelta() * 0.1f;
	float NewZoom = FMath::Clamp(Zoom.Get() + ZoomDelta, 0.5f, 4.0f);

	OnZoomChanged.ExecuteIfBound(NewZoom);

	return FReply::Handled();
}

FReply SSpriteAlignmentCanvas::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
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
