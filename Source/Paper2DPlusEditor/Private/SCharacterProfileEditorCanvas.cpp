// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "CharacterProfileAssetEditor.h"
#include "CharacterProfileEditorModel.h"
#include "EditorCanvasUtils.h"
#include "HitboxDataProvider.h"
#include "Paper2DPlusCharacterLayerAsset.h"
#include "Paper2DPlusAppearanceResolver.h"
#include "Paper2DPlusLayerDraw.h"
#include "SlateShortcutUtils.h"
#include "PaperFlipbook.h"
#include "PaperSprite.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Engine/Texture2D.h"

/** SCharacterProfileEditorCanvas — Canvas utilities for hitbox and sprite editor views: drawing primitives, hit-testing, coordinate transforms. */

#define LOCTEXT_NAMESPACE "CharacterProfileAssetEditor"

// ==========================================
// SCharacterProfileEditorCanvas Implementation
// ==========================================

void SCharacterProfileEditorCanvas::Construct(const FArguments& InArgs)
{
	Asset = InArgs._Asset;
	LayerAsset = InArgs._LayerAsset;
	ModelWeak = InArgs._Model;
	Provider = InArgs._FrameDataProvider;
	SelectedFlipbookIndex = InArgs._SelectedFlipbookIndex;
	SelectedFrameIndex = InArgs._SelectedFrameIndex;
	CurrentTool = InArgs._CurrentTool;
	Zoom = InArgs._Zoom;
	VisibilityMask = InArgs._VisibilityMask;
	ActiveDrawType = InArgs._ActiveDrawType;

	// Clip all drawing to canvas bounds so hitboxes don't bleed over UI chrome
	SetClipping(EWidgetClipping::ClipToBounds);
}

FVector2D SCharacterProfileEditorCanvas::ComputeDesiredSize(float) const
{
	return FVector2D(600, 500);
}

FVector2D SCharacterProfileEditorCanvas::GetSpriteDimensions() const
{
	// Geometry is authored in the canonical Profile frame coordinate system. A larger Hair/Weapon layer
	// may widen the preview zoom, but it must never change the local clamp/origin used by boxes and sockets.
	UPaperSprite* Sprite = nullptr;
	FVector2D Dimensions(128.0f, 128.0f);
	GetCurrentSpriteInfo(Sprite, Dimensions);
	return Dimensions;
}

FVector2D SCharacterProfileEditorCanvas::GetLargestSpriteDims() const
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
		CachedLargestDims = FVector2D(128, 128);
		CachedLargestDimsFlipbookIndex = FlipbookIdx;
		CachedLargestDimsFlipbook = nullptr;
		return CachedLargestDims;
	}

	FVector2D Largest(1, 1);
	for (int32 i = 0; i < FB->GetNumKeyFrames(); i++)
	{
		UPaperSprite* Sprite = FB->GetKeyFrameChecked(i).Sprite;
		if (Sprite)
		{
			FVector2D Dims = Sprite->GetSourceSize();
			Largest.X = FMath::Max(Largest.X, Dims.X);
			Largest.Y = FMath::Max(Largest.Y, Dims.Y);
		}
	}

	// Expand the zoom bounds for every visible, already-loaded layer sprite. ResolveTotalOffsetPx and
	// the sprite pivot shift are the exact transforms used by the composite paint path; including both
	// prevents large/offset equipment from being clipped without changing the Profile-local edit origin.
	if (const UPaper2DPlusCharacterLayerAsset* Layers = LayerAsset.Get(); Layers && Anim)
	{
		const TSharedPtr<FCharacterProfileEditorModel> Model = ModelWeak.Pin();
		FPaper2DPlusAppearanceDescriptor DefaultAppearance;
		Paper2DPlusAppearanceResolver::BuildDefaultDescriptor(Layers, DefaultAppearance);
		for (const FCharacterLayer& Layer : Layers->Layers)
		{
			const bool bVisible = Model.IsValid()
				? Model->IsLayerVisible(Layer.LayerName)
				: DefaultAppearance.ActiveLayerIds.Contains(Layer.LayerId);
			if (!bVisible) continue;
			for (int32 FrameIndex = 0; FrameIndex < FB->GetNumKeyFrames(); ++FrameIndex)
			{
				UPaperSprite* LayerSprite = Layer.GetSpriteForFrame(
					Anim->Identity.FlipbookName, FrameIndex, false);
				if (!LayerSprite) continue;
				const FVector2D Size = LayerSprite->GetSourceSize();
				const FVector2D SourceCenter = LayerSprite->GetSourceUV() + Size * 0.5f;
				const FVector2D PivotShift = SourceCenter - LayerSprite->GetPivotPosition();
				const FVector2D Shift = Paper2DPlusLayerDraw::ResolveTotalOffsetPx(
					Anim, FrameIndex, &Layer, Anim->Identity.FlipbookName) + PivotShift;
				Largest.X = FMath::Max(Largest.X, Size.X + 2.0f * FMath::Abs(Shift.X));
				Largest.Y = FMath::Max(Largest.Y, Size.Y + 2.0f * FMath::Abs(Shift.Y));
			}
		}
	}

	CachedLargestDims = Largest;
	CachedLargestDimsFlipbookIndex = FlipbookIdx;
	CachedLargestDimsFlipbook = FB;
	return CachedLargestDims;
}

FVector2D SCharacterProfileEditorCanvas::GetCanvasOffset(const FGeometry& Geom) const
{
	// Use current frame dims for positioning (hitboxes are relative to current sprite)
	FVector2D SpriteDims = GetSpriteDimensions();
	FVector2D WidgetSize = Geom.GetLocalSize();
	float EffectiveZoom = GetEffectiveZoom(Geom);

	return FVector2D(
		(WidgetSize.X - SpriteDims.X * EffectiveZoom) * 0.5f,
		(WidgetSize.Y - SpriteDims.Y * EffectiveZoom) * 0.5f
	);
}

float SCharacterProfileEditorCanvas::GetEffectiveZoom(const FGeometry& Geom) const
{
	// Use largest sprite dims for zoom so scale stays consistent across frames
	FVector2D LargestDims = GetLargestSpriteDims();
	FVector2D WidgetSize = Geom.GetLocalSize();

	float BaseScale = FMath::Min(
		WidgetSize.X / LargestDims.X,
		WidgetSize.Y / LargestDims.Y
	) * 0.9f;

	return BaseScale * Zoom.Get();
}

FVector2D SCharacterProfileEditorCanvas::ScreenToCanvas(const FGeometry& Geom, const FVector2D& ScreenPos) const
{
	FVector2D LocalPos = Geom.AbsoluteToLocal(ScreenPos);
	FVector2D Offset = GetCanvasOffset(Geom);
	float EffectiveZoom = GetEffectiveZoom(Geom);
	const FVector2D DisplayOffset = Provider.IsValid()
		? Provider->GetAuthoringDisplayOffsetPx(
			SelectedFlipbookIndex.Get(-1), SelectedFrameIndex.Get())
		: FVector2D::ZeroVector;
	return ((LocalPos - Offset) / EffectiveZoom) - DisplayOffset;
}

FVector2D SCharacterProfileEditorCanvas::CanvasToScreen(const FGeometry& Geom, const FVector2D& CanvasPos) const
{
	FVector2D Offset = GetCanvasOffset(Geom);
	float EffectiveZoom = GetEffectiveZoom(Geom);
	const FVector2D DisplayOffset = Provider.IsValid()
		? Provider->GetAuthoringDisplayOffsetPx(
			SelectedFlipbookIndex.Get(-1), SelectedFrameIndex.Get())
		: FVector2D::ZeroVector;
	return Offset + (CanvasPos + DisplayOffset) * EffectiveZoom;
}

int32 SCharacterProfileEditorCanvas::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId,
	const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	// Draw checkerboard background matching Sprite Editor
	FEditorCanvasUtils::DrawCheckerboard(OutDrawElements, LayerId, AllottedGeometry);

	const FFrameHitboxData* Frame = GetCurrentFrame();

	UPaperSprite* Sprite = nullptr;
	FVector2D SpriteDimensions(128.0f, 128.0f);
	bool bHasSprite = GetCurrentSpriteInfo(Sprite, SpriteDimensions);

	float EffectiveZoom = GetEffectiveZoom(AllottedGeometry);
	FVector2D Offset = GetCanvasOffset(AllottedGeometry);

	bool bDrewLayerComposite = false;
	if (const UPaper2DPlusCharacterLayerAsset* Layers = LayerAsset.Get())
	{
		const FFlipbookProfileEntry* Animation = GetCurrentFlipbookData();
		const TSharedPtr<FCharacterProfileEditorModel> Model = ModelWeak.Pin();
		if (Animation)
		{
			TArray<const FCharacterLayer*> PaintLayers;
			FPaper2DPlusAppearanceDescriptor DefaultAppearance;
			Paper2DPlusAppearanceResolver::BuildDefaultDescriptor(Layers, DefaultAppearance);
			for (const FCharacterLayer& Layer : Layers->Layers)
			{
				const bool bVisible = Model.IsValid()
					? Model->IsLayerVisible(Layer.LayerName)
					: DefaultAppearance.ActiveLayerIds.Contains(Layer.LayerId);
				if (bVisible) PaintLayers.Add(&Layer);
			}
			const int32 FrameIndex = SelectedFrameIndex.Get();
			const FVector2D CompositeCenter = Offset
				+ (SpriteDimensions * EffectiveZoom) * 0.5f;
			for (const FCharacterLayer* Layer : PaintLayers)
			{
				UPaperSprite* LayerSprite = Layer
					? Layer->GetSpriteForFrame(Animation->Identity.FlipbookName, FrameIndex, false)
					: nullptr;
				if (!LayerSprite) continue;
				const FVector2D Placement = Paper2DPlusLayerDraw::ResolveTotalOffsetPx(
					Animation, FrameIndex, Layer, Animation->Identity.FlipbookName);
				FEditorCanvasUtils::DrawFlipbookSprite(
					OutDrawElements,
					LayerId + 2,
					AllottedGeometry,
					LayerSprite,
					nullptr,
					FrameIndex,
					CompositeCenter + Placement * EffectiveZoom,
					EffectiveZoom,
					FLinearColor::White);
				bDrewLayerComposite = true;
			}
		}
	}

	if (!bDrewLayerComposite && bHasSprite && Sprite)
	{
		UTexture2D* SpriteTexture = Sprite->GetBakedTexture();
		if (!SpriteTexture)
		{
			SpriteTexture = Cast<UTexture2D>(Sprite->GetSourceTexture());
		}

		if (SpriteTexture)
		{
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

			FVector2D SpriteDrawSize = SpriteDimensions * EffectiveZoom;

			FSlateDrawElement::MakeBox(
				OutDrawElements,
				LayerId + 2,
				MakePaintGeometry(AllottedGeometry, FVector2D(SpriteDrawSize), FSlateLayoutTransform(FVector2D(Offset))),
				&SpriteBrush,
				ESlateDrawEffect::None,
				FLinearColor::White
			);
		}
	}

	// Draw dimension boundary outline
	{
		FVector2D BoundarySize = SpriteDimensions * EffectiveZoom;
		// Visible outline of the dimension box
		TArray<FVector2D> BoundaryPoints = {
			Offset,
			FVector2D(Offset.X + BoundarySize.X, Offset.Y),
			Offset + BoundarySize,
			FVector2D(Offset.X, Offset.Y + BoundarySize.Y),
			Offset
		};
		FSlateDrawElement::MakeLines(
			OutDrawElements,
			LayerId + 3,
			AllottedGeometry.ToPaintGeometry(),
			BoundaryPoints, ESlateDrawEffect::None,
			FLinearColor(0.4f, 0.6f, 0.8f, 0.6f), true, 1.5f
		);
	}

	const EHitboxVisibility Mask = VisibilityMask.Get(EHitboxVisibility::All);

	// Ghosted base-profile boxes (layer scope only): drawn read-only and dimmed BENEATH the authored
	// part boxes so the user draws in context of the base silhouette. Non-interactive — hit-testing and
	// editing only ever see the authored boxes from GetCurrentFrame().
	if (Provider.IsValid())
	{
		TArray<FHitboxData> FinalGhostBoxes;
		Provider->GetFinalGhostBoxes(
			SelectedFlipbookIndex.Get(-1), SelectedFrameIndex.Get(), FinalGhostBoxes);
		for (const FHitboxData& GB : FinalGhostBoxes)
		{
			const EHitboxVisibility TypeBit =
				(GB.Type == EHitboxType::Attack)  ? EHitboxVisibility::Attack
			  : (GB.Type == EHitboxType::Hurtbox) ? EHitboxVisibility::Hurtbox
			                                      : EHitboxVisibility::None;
			if (TypeBit == EHitboxVisibility::None || !EnumHasAnyFlags(Mask, TypeBit)) continue;
			DrawGhostBox(AllottedGeometry, OutDrawElements, LayerId + 4, GB, true);
		}
		TArray<FHitboxData> GhostBoxes;
		Provider->GetGhostBoxes(SelectedFlipbookIndex.Get(-1), SelectedFrameIndex.Get(), GhostBoxes);
		for (const FHitboxData& GB : GhostBoxes)
		{
			const EHitboxVisibility TypeBit =
				(GB.Type == EHitboxType::Attack)  ? EHitboxVisibility::Attack
			  : (GB.Type == EHitboxType::Hurtbox) ? EHitboxVisibility::Hurtbox
			                                      : EHitboxVisibility::None;
			if (TypeBit == EHitboxVisibility::None || !EnumHasAnyFlags(Mask, TypeBit)) continue;
			DrawGhostBox(AllottedGeometry, OutDrawElements, LayerId + 4, GB, false);
		}
	}

	if (Frame)
	{
		for (int32 i = 0; i < Frame->Hitboxes.Num(); i++)
		{
			const FHitboxData& HB = Frame->Hitboxes[i];
			const EHitboxVisibility TypeBit =
				(HB.Type == EHitboxType::Attack)  ? EHitboxVisibility::Attack
			  : (HB.Type == EHitboxType::Hurtbox) ? EHitboxVisibility::Hurtbox
			                                      : EHitboxVisibility::None;
			if (TypeBit == EHitboxVisibility::None || !EnumHasAnyFlags(Mask, TypeBit)) continue;
			bool bSelected = (SelectionType == EHitboxSelectionType::Hitbox && IsSelected(i));
			DrawHitbox(AllottedGeometry, OutDrawElements, LayerId + 5, HB, bSelected);
		}

		// Draw resize handles only on the primary selected hitbox (last one clicked)
		int32 PrimaryIndex = GetPrimarySelectedIndex();
		if (SelectionType == EHitboxSelectionType::Hitbox &&
			Frame->Hitboxes.IsValidIndex(PrimaryIndex))
		{
			const EHitboxType PrimaryType = Frame->Hitboxes[PrimaryIndex].Type;
			const EHitboxVisibility PrimaryBit =
				(PrimaryType == EHitboxType::Attack)  ? EHitboxVisibility::Attack
			  : (PrimaryType == EHitboxType::Hurtbox) ? EHitboxVisibility::Hurtbox
			                                          : EHitboxVisibility::None;
			if (PrimaryBit != EHitboxVisibility::None && EnumHasAnyFlags(Mask, PrimaryBit) &&
				(CurrentTool.Get() == EHitboxEditorTool::Edit || CurrentTool.Get() == EHitboxEditorTool::Draw))
			{
				DrawResizeHandles(AllottedGeometry, OutDrawElements, LayerId + 7, Frame->Hitboxes[PrimaryIndex]);
			}
		}

		for (int32 i = 0; i < Frame->Sockets.Num(); i++)
		{
			bool bSelected = (SelectionType == EHitboxSelectionType::Socket && SelectedIndices.Contains(i));
			DrawSocket(AllottedGeometry, OutDrawElements, LayerId + 7, Frame->Sockets[i], bSelected);
		}
	}

	if (DragMode == EHitboxDragMode::Creating)
	{
		DrawCreatingRect(AllottedGeometry, OutDrawElements, LayerId + 8);
	}

	return LayerId + 10;
}

void SCharacterProfileEditorCanvas::DrawHitbox(const FGeometry& Geom, FSlateWindowElementList& OutDrawElements,
	int32 LayerId, const FHitboxData& HB, bool bSelected) const
{
	float EffectiveZoom = GetEffectiveZoom(Geom);
	FLinearColor Color = GetHitboxColor(HB.Type);
	FVector2D Pos = CanvasToScreen(Geom, FVector2D(HB.X, HB.Y));
	FVector2D BoxSize(HB.Width * EffectiveZoom, HB.Height * EffectiveZoom);

	float FillAlpha = bSelected ? 0.5f : 0.3f;
	FSlateDrawElement::MakeBox(
		OutDrawElements, LayerId,
		MakePaintGeometry(Geom, FVector2D(BoxSize), FSlateLayoutTransform(FVector2D(Pos))),
		FAppStyle::Get().GetBrush("WhiteBrush"),
		ESlateDrawEffect::None,
		Color * FLinearColor(1, 1, 1, FillAlpha)
	);

	float BorderThickness = bSelected ? 3.0f : 2.0f;
	TArray<FVector2D> BorderPoints = {
		Pos,
		FVector2D(Pos.X + BoxSize.X, Pos.Y),
		Pos + BoxSize,
		FVector2D(Pos.X, Pos.Y + BoxSize.Y),
		Pos
	};
	FSlateDrawElement::MakeLines(
		OutDrawElements, LayerId + 1, Geom.ToPaintGeometry(),
		BorderPoints, ESlateDrawEffect::None,
		bSelected ? FLinearColor::White : Color, true, BorderThickness
	);
}

void SCharacterProfileEditorCanvas::DrawGhostBox(const FGeometry& Geom, FSlateWindowElementList& OutDrawElements,
	int32 LayerId, const FHitboxData& HB, bool bFinalProjection) const
{
	// Read-only base-profile box: dimmed, desaturated fill + faint border, drawn at a single layer beneath
	// the authored boxes (dim-attack/dim-hurt ghost palette). Never hit-tested — this is context only, so
	// the user draws the part's boxes over the base silhouette.
	const float EffectiveZoom = GetEffectiveZoom(Geom);
	const FVector2D Offset = GetCanvasOffset(Geom);

	const FLinearColor GhostColor = bFinalProjection
		? FLinearColor(0.42f, 0.45f, 0.72f)
		: ((HB.Type == EHitboxType::Attack)
			? FLinearColor(0.55f, 0.28f, 0.28f) : FLinearColor(0.28f, 0.45f, 0.32f));
	const FVector2D Pos = Offset + FVector2D(HB.X, HB.Y) * EffectiveZoom;
	const FVector2D BoxSize(HB.Width * EffectiveZoom, HB.Height * EffectiveZoom);

	FSlateDrawElement::MakeBox(
		OutDrawElements, LayerId,
		MakePaintGeometry(Geom, FVector2D(BoxSize), FSlateLayoutTransform(FVector2D(Pos))),
		FAppStyle::Get().GetBrush("WhiteBrush"),
		ESlateDrawEffect::None,
		GhostColor * FLinearColor(1, 1, 1, bFinalProjection ? 0.07f : 0.14f)
	);

	const TArray<FVector2D> BorderPoints = {
		Pos,
		FVector2D(Pos.X + BoxSize.X, Pos.Y),
		Pos + BoxSize,
		FVector2D(Pos.X, Pos.Y + BoxSize.Y),
		Pos
	};
	FSlateDrawElement::MakeLines(
		OutDrawElements, LayerId, Geom.ToPaintGeometry(),
		BorderPoints, ESlateDrawEffect::None,
		GhostColor * FLinearColor(1, 1, 1, bFinalProjection ? 0.28f : 0.45f), true,
		bFinalProjection ? 0.75f : 1.0f
	);
}

void SCharacterProfileEditorCanvas::DrawSocket(const FGeometry& Geom, FSlateWindowElementList& OutDrawElements,
	int32 LayerId, const FSocketData& Sock, bool bSelected) const
{
	FVector2D Pos = CanvasToScreen(Geom, FVector2D(Sock.X, Sock.Y));
	float CrossSize = bSelected ? 12.0f : 8.0f;
	float Thickness = bSelected ? 3.0f : 2.0f;
	FLinearColor Color = bSelected ? FLinearColor::White : FLinearColor::Yellow;

	TArray<FVector2D> HLine = { FVector2D(Pos.X - CrossSize, Pos.Y), FVector2D(Pos.X + CrossSize, Pos.Y) };
	TArray<FVector2D> VLine = { FVector2D(Pos.X, Pos.Y - CrossSize), FVector2D(Pos.X, Pos.Y + CrossSize) };

	FSlateDrawElement::MakeLines(OutDrawElements, LayerId, Geom.ToPaintGeometry(),
		HLine, ESlateDrawEffect::None, Color, true, Thickness);
	FSlateDrawElement::MakeLines(OutDrawElements, LayerId, Geom.ToPaintGeometry(),
		VLine, ESlateDrawEffect::None, Color, true, Thickness);

	if (bSelected)
	{
		const int32 NumSegments = 16;
		TArray<FVector2D> Circle;
		for (int32 i = 0; i <= NumSegments; i++)
		{
			float Angle = 2.0f * PI * i / NumSegments;
			Circle.Add(Pos + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * CrossSize);
		}
		FSlateDrawElement::MakeLines(OutDrawElements, LayerId, Geom.ToPaintGeometry(),
			Circle, ESlateDrawEffect::None, Color, true, 1.0f);
	}
}

void SCharacterProfileEditorCanvas::DrawResizeHandles(const FGeometry& Geom, FSlateWindowElementList& OutDrawElements,
	int32 LayerId, const FHitboxData& HB) const
{
	FVector2D TopLeft = CanvasToScreen(Geom, FVector2D(HB.X, HB.Y));
	FVector2D BottomRight = CanvasToScreen(Geom, FVector2D(HB.X + HB.Width, HB.Y + HB.Height));
	FVector2D Center = (TopLeft + BottomRight) * 0.5f;

	TArray<FVector2D> HandlePositions = {
		TopLeft,
		FVector2D(Center.X, TopLeft.Y),
		FVector2D(BottomRight.X, TopLeft.Y),
		FVector2D(TopLeft.X, Center.Y),
		FVector2D(BottomRight.X, Center.Y),
		FVector2D(TopLeft.X, BottomRight.Y),
		FVector2D(Center.X, BottomRight.Y),
		BottomRight
	};

	for (const FVector2D& HandlePos : HandlePositions)
	{
		FVector2D HandleTopLeft = HandlePos - FVector2D(HandleSize * 0.5f, HandleSize * 0.5f);
		FSlateDrawElement::MakeBox(
			OutDrawElements, LayerId,
			MakePaintGeometry(Geom, FVector2D(HandleSize, HandleSize), FSlateLayoutTransform(FVector2D(HandleTopLeft))),
			FAppStyle::Get().GetBrush("WhiteBrush"),
			ESlateDrawEffect::None,
			FLinearColor::White
		);
	}
}

void SCharacterProfileEditorCanvas::DrawCreatingRect(const FGeometry& Geom, FSlateWindowElementList& OutDrawElements, int32 LayerId) const
{
	float EffectiveZoom = GetEffectiveZoom(Geom);
	FVector2D Pos = CanvasToScreen(Geom, FVector2D(CreatingRect.Min.X, CreatingRect.Min.Y));
	FVector2D Size = FVector2D(CreatingRect.Width(), CreatingRect.Height()) * EffectiveZoom;

	FLinearColor DrawColor = GetHitboxColor(ActiveDrawType.Get());

	FSlateDrawElement::MakeBox(
		OutDrawElements, LayerId,
		MakePaintGeometry(Geom, FVector2D(Size), FSlateLayoutTransform(FVector2D(Pos))),
		FAppStyle::Get().GetBrush("WhiteBrush"),
		ESlateDrawEffect::None,
		FLinearColor(DrawColor.R, DrawColor.G, DrawColor.B, 0.2f)
	);

	TArray<FVector2D> BorderPoints = {
		Pos,
		FVector2D(Pos.X + Size.X, Pos.Y),
		Pos + Size,
		FVector2D(Pos.X, Pos.Y + Size.Y),
		Pos
	};
	FSlateDrawElement::MakeLines(
		OutDrawElements, LayerId + 1, Geom.ToPaintGeometry(),
		BorderPoints, ESlateDrawEffect::None,
		DrawColor, true, 2.0f
	);
}

FLinearColor SCharacterProfileEditorCanvas::GetHitboxColor(EHitboxType Type) const
{
	switch (Type)
	{
		case EHitboxType::Attack: return FLinearColor::Red;
		case EHitboxType::Hurtbox: return FLinearColor::Green;
		default: return FLinearColor::White;
	}
}

const FFrameHitboxData* SCharacterProfileEditorCanvas::GetCurrentFrame() const
{
	// Route through the provider when one is injected (the layer editor). The profile editor either
	// injects the default profile provider or passes none — both resolve the same profile rows.
	if (Provider.IsValid())
	{
		return Provider->GetFrame(SelectedFlipbookIndex.Get(-1), SelectedFrameIndex.Get());
	}

	const FFlipbookProfileEntry* Anim = GetCurrentFlipbookData();
	if (!Anim) return nullptr;

	int32 FrameIndex = SelectedFrameIndex.Get();
	if (!Anim->CombatData.Frames.IsValidIndex(FrameIndex)) return nullptr;

	return &Anim->CombatData.Frames[FrameIndex];
}

FFrameHitboxData* SCharacterProfileEditorCanvas::GetCurrentFrameMutable() const
{
	// Find-only. Drag-move/resize borrows this pointer, so it must resolve an EXISTING row (a first
	// draw goes through EnsureCurrentFrameMutable). Provider path re-scopes to the layer override.
	if (Provider.IsValid())
	{
		return Provider->GetFrameMutable(SelectedFlipbookIndex.Get(-1), SelectedFrameIndex.Get());
	}

	if (!Asset.IsValid()) return nullptr;

	int32 FlipbookIndex = SelectedFlipbookIndex.Get();
	if (!Asset->Flipbooks.IsValidIndex(FlipbookIndex)) return nullptr;

	int32 FrameIndex = SelectedFrameIndex.Get();
	if (!Asset->Flipbooks[FlipbookIndex].CombatData.Frames.IsValidIndex(FrameIndex)) return nullptr;

	return &Asset->Flipbooks[FlipbookIndex].CombatData.Frames[FrameIndex];
}

FFrameHitboxData* SCharacterProfileEditorCanvas::EnsureCurrentFrameMutable() const
{
	if (Provider.IsValid())
	{
		return Provider->EnsureFrameMutable(SelectedFlipbookIndex.Get(-1), SelectedFrameIndex.Get());
	}
	return GetCurrentFrameMutable();
}

bool SCharacterProfileEditorCanvas::CanEnsureCurrentFrame() const
{
	if (Provider.IsValid())
	{
		return Provider->CanEnsureFrame(
			SelectedFlipbookIndex.Get(-1), SelectedFrameIndex.Get());
	}
	return GetCurrentFrame() != nullptr;
}

const FFlipbookProfileEntry* SCharacterProfileEditorCanvas::GetCurrentFlipbookData() const
{
	if (!Asset.IsValid()) return nullptr;

	int32 FlipbookIndex = SelectedFlipbookIndex.Get();
	if (!Asset->Flipbooks.IsValidIndex(FlipbookIndex)) return nullptr;

	return &Asset->Flipbooks[FlipbookIndex];
}

bool SCharacterProfileEditorCanvas::GetCurrentSpriteInfo(UPaperSprite*& OutSprite, FVector2D& OutDimensions) const
{
	OutSprite = nullptr;
	OutDimensions = FVector2D(128.0f, 128.0f);

	const FFlipbookProfileEntry* Anim = GetCurrentFlipbookData();
	if (!Anim) return false;

	if (Anim->Identity.Flipbook.IsNull()) return false;

	// Paint/input queries are load-free. Selection/import paths own asset loading and broadcast a refresh.
	UPaperFlipbook* Flipbook = Anim->Identity.Flipbook.Get();
	if (!Flipbook) return false;

	int32 FrameIndex = SelectedFrameIndex.Get();
	int32 NumKeyFrames = Flipbook->GetNumKeyFrames();

	if (NumKeyFrames == 0) return false;

	FrameIndex = FMath::Clamp(FrameIndex, 0, NumKeyFrames - 1);

	const FPaperFlipbookKeyFrame& KeyFrame = Flipbook->GetKeyFrameChecked(FrameIndex);
	OutSprite = KeyFrame.Sprite;
	if (!OutSprite) return false;

	OutDimensions = OutSprite->GetSourceSize();

	if (OutDimensions.X <= 0 || OutDimensions.Y <= 0)
	{
		UTexture2D* Texture = Cast<UTexture2D>(OutSprite->GetSourceTexture());
		if (Texture)
		{
			OutDimensions = FVector2D(Texture->GetSizeX(), Texture->GetSizeY());
		}
	}

	return OutDimensions.X > 0 && OutDimensions.Y > 0;
}

int32 SCharacterProfileEditorCanvas::HitTestHitbox(const FVector2D& CanvasPos) const
{
	const FFrameHitboxData* Frame = GetCurrentFrame();
	if (!Frame) return -1;

	const EHitboxVisibility Mask = VisibilityMask.Get(EHitboxVisibility::All);
	for (int32 i = Frame->Hitboxes.Num() - 1; i >= 0; i--)
	{
		const FHitboxData& HB = Frame->Hitboxes[i];
		const EHitboxVisibility TypeBit =
			(HB.Type == EHitboxType::Attack)  ? EHitboxVisibility::Attack
		  : (HB.Type == EHitboxType::Hurtbox) ? EHitboxVisibility::Hurtbox
		                                      : EHitboxVisibility::None;
		if (TypeBit == EHitboxVisibility::None || !EnumHasAnyFlags(Mask, TypeBit)) continue;
		if (CanvasPos.X >= HB.X && CanvasPos.X <= HB.X + HB.Width &&
			CanvasPos.Y >= HB.Y && CanvasPos.Y <= HB.Y + HB.Height)
		{
			return i;
		}
	}
	return -1;
}

int32 SCharacterProfileEditorCanvas::HitTestSocket(const FVector2D& CanvasPos) const
{
	const FFrameHitboxData* Frame = GetCurrentFrame();
	if (!Frame) return -1;

	for (int32 i = Frame->Sockets.Num() - 1; i >= 0; i--)
	{
		const FSocketData& Sock = Frame->Sockets[i];
		float Dist = FVector2D::Distance(CanvasPos, FVector2D(Sock.X, Sock.Y));
		if (Dist <= SocketHitRadius)
		{
			return i;
		}
	}
	return -1;
}

EResizeHandle SCharacterProfileEditorCanvas::HitTestHandle(const FVector2D& CanvasPos, const FHitboxData& Hitbox) const
{
	float HitSize = HandleSize / GetEffectiveZoom(GetCachedGeometry()) * 1.5f;

	FVector2D TopLeft(Hitbox.X, Hitbox.Y);
	FVector2D BottomRight(Hitbox.X + Hitbox.Width, Hitbox.Y + Hitbox.Height);
	FVector2D Center = (TopLeft + BottomRight) * 0.5f;

	auto TestHandle = [&](const FVector2D& HandlePos) -> bool
	{
		return FMath::Abs(CanvasPos.X - HandlePos.X) <= HitSize &&
			   FMath::Abs(CanvasPos.Y - HandlePos.Y) <= HitSize;
	};

	if (TestHandle(TopLeft)) return EResizeHandle::TopLeft;
	if (TestHandle(FVector2D(Center.X, TopLeft.Y))) return EResizeHandle::Top;
	if (TestHandle(FVector2D(BottomRight.X, TopLeft.Y))) return EResizeHandle::TopRight;
	if (TestHandle(FVector2D(TopLeft.X, Center.Y))) return EResizeHandle::Left;
	if (TestHandle(FVector2D(BottomRight.X, Center.Y))) return EResizeHandle::Right;
	if (TestHandle(FVector2D(TopLeft.X, BottomRight.Y))) return EResizeHandle::BottomLeft;
	if (TestHandle(FVector2D(Center.X, BottomRight.Y))) return EResizeHandle::Bottom;
	if (TestHandle(BottomRight)) return EResizeHandle::BottomRight;

	return EResizeHandle::None;
}


FReply SCharacterProfileEditorCanvas::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		FVector2D CanvasPos = ScreenToCanvas(MyGeometry, MouseEvent.GetScreenSpacePosition());
		EHitboxEditorTool Tool = CurrentTool.Get();

		if (Tool == EHitboxEditorTool::Draw || Tool == EHitboxEditorTool::Edit)
		{
			const bool bShiftDown = MouseEvent.IsShiftDown();

			// Check resize handles on primary selected hitbox first
			int32 PrimaryIndex = GetPrimarySelectedIndex();
			if (SelectionType == EHitboxSelectionType::Hitbox)
			{
				const FFrameHitboxData* Frame = GetCurrentFrame();
				if (Frame && Frame->Hitboxes.IsValidIndex(PrimaryIndex))
				{
					EResizeHandle Handle = HitTestHandle(CanvasPos, Frame->Hitboxes[PrimaryIndex]);
					if (Handle != EResizeHandle::None)
					{
						ActiveHandle = Handle;
						DragMode = EHitboxDragMode::Resizing;
						DragStart = CanvasPos;
						// Dead-zone: defer opening the undo transaction until OnMouseMove makes the first
						// real edit; a plain select-click must not open a transaction or dirty the asset.
						bDragTransactionOpen = false;
						return FReply::Handled().CaptureMouse(SharedThis(const_cast<SCharacterProfileEditorCanvas*>(this)));
					}
				}
			}

			int32 HitHitbox = HitTestHitbox(CanvasPos);
			if (HitHitbox >= 0)
			{
				if (bShiftDown)
				{
					// Shift-click: toggle selection
					ToggleSelection(HitHitbox);
				}
				else
				{
					// Normal click: if the hit hitbox is already selected, keep the multi-selection for dragging
					if (!IsSelected(HitHitbox))
					{
						ClearSelection();
						AddToSelection(HitHitbox);
					}
					else
					{
						// Make it the primary (move to end)
						SelectedIndices.Remove(HitHitbox);
						SelectedIndices.Add(HitHitbox);
						OnSelectionChanged.ExecuteIfBound(SelectionType, GetPrimarySelectedIndex());
					}
				}

				// Store initial positions of all selected hitboxes for group move
				DragStartPositions.Empty();
				const FFrameHitboxData* Frame = GetCurrentFrame();
				if (Frame)
				{
					for (int32 Idx : SelectedIndices)
					{
						if (Frame->Hitboxes.IsValidIndex(Idx))
						{
							DragStartPositions.Add(Idx, FVector2D(Frame->Hitboxes[Idx].X, Frame->Hitboxes[Idx].Y));
						}
					}
				}

				DragMode = EHitboxDragMode::Moving;
				DragStart = CanvasPos;
				// Dead-zone: defer the undo transaction to the first real move in OnMouseMove.
				bDragTransactionOpen = false;
				return FReply::Handled().CaptureMouse(SharedThis(const_cast<SCharacterProfileEditorCanvas*>(this)));
			}

			int32 HitSocket = HitTestSocket(CanvasPos);
			if (HitSocket >= 0)
			{
				// Sockets keep single-select behavior
				SetSelection(EHitboxSelectionType::Socket, HitSocket);
				DragMode = EHitboxDragMode::Moving;
				DragStart = CanvasPos;
				// Dead-zone: defer the undo transaction to the first real move in OnMouseMove.
				bDragTransactionOpen = false;
				return FReply::Handled().CaptureMouse(SharedThis(const_cast<SCharacterProfileEditorCanvas*>(this)));
			}

			// Empty space: draw a new hitbox
			if (!bShiftDown)
			{
				ClearSelection();
			}

			int32 SnappedX = FMath::RoundToInt(CanvasPos.X);
			int32 SnappedY = FMath::RoundToInt(CanvasPos.Y);

			CreatingRect = FIntRect(SnappedX, SnappedY, SnappedX, SnappedY);
			DragMode = EHitboxDragMode::Creating;
			DragStart = CanvasPos;

			return FReply::Handled().CaptureMouse(SharedThis(const_cast<SCharacterProfileEditorCanvas*>(this)));
		}
		else if (Tool == EHitboxEditorTool::Socket)
		{
			// Validate before enrolling the owner in undo. A stale/no-layer scope must not create an empty
			// transaction or dirty the Layer asset merely because the designer clicked the canvas.
			if (!CanEnsureCurrentFrame())
			{
				return FReply::Handled().SetUserFocus(
					SharedThis(const_cast<SCharacterProfileEditorCanvas*>(this)), EFocusCause::Mouse);
			}
			// First-write site: open the transaction, then create-on-first-edit (see the draw path).
			OnRequestUndo.ExecuteIfBound();
			FFrameHitboxData* Frame = EnsureCurrentFrameMutable();
			if (Frame)
			{
				// Socket-create mutates immediately, so the transaction is open now. Mark the flag so the
				// deferred OnMouseButtonUp / OnMouseCaptureLost close fires (this path continues as a Moving drag).
				bDragTransactionOpen = true;

				FSocketData NewSocket;
				NewSocket.Name = FString::Printf(TEXT("Socket%d"), Frame->Sockets.Num());
				NewSocket.X = FMath::RoundToInt(CanvasPos.X);
				NewSocket.Y = FMath::RoundToInt(CanvasPos.Y);

				int32 NewIndex = Frame->Sockets.Add(NewSocket);
				SetSelection(EHitboxSelectionType::Socket, NewIndex);
				OnHitboxDataModified.ExecuteIfBound();

				DragMode = EHitboxDragMode::Moving;
				DragStart = CanvasPos;
				return FReply::Handled().CaptureMouse(SharedThis(const_cast<SCharacterProfileEditorCanvas*>(this)));
			}
			// Couldn't resolve/create a row — close the just-opened transaction so it doesn't linger.
			OnEndTransaction.ExecuteIfBound();
		}

		return FReply::Handled().SetUserFocus(SharedThis(const_cast<SCharacterProfileEditorCanvas*>(this)), EFocusCause::Mouse);
	}

	return FReply::Unhandled();
}

FReply SCharacterProfileEditorCanvas::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton
		&& !HasMouseCapture()
		&& DragMode != EHitboxDragMode::None)
	{
		SettleAndResetDragState();
		return FReply::Handled();
	}
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && HasMouseCapture())
	{
		if (DragMode == EHitboxDragMode::Creating)
		{
			if (CreatingRect.Width() > 0 && CreatingRect.Height() > 0
				&& CanEnsureCurrentFrame())
			{
				// First-write site: open the transaction, THEN create-on-first-edit (the layer override
				// entry may not exist yet). OnRequestUndo must precede EnsureCurrentFrameMutable so the
				// created entry is enrolled in the transaction.
				OnRequestUndo.ExecuteIfBound();
				FFrameHitboxData* Frame = EnsureCurrentFrameMutable();
				if (Frame)
				{
					FHitboxData NewHitbox;
					NewHitbox.Type = ActiveDrawType.Get();
					NewHitbox.X = CreatingRect.Min.X;
					NewHitbox.Y = CreatingRect.Min.Y;
					NewHitbox.Width = CreatingRect.Width();
					NewHitbox.Height = CreatingRect.Height();
					NewHitbox.Damage = 0;
					NewHitbox.Knockback = 0;

					int32 NewIndex = Frame->Hitboxes.Add(NewHitbox);
					SetSelection(EHitboxSelectionType::Hitbox, NewIndex);
					OnHitboxDataModified.ExecuteIfBound();
				}
				OnEndTransaction.ExecuteIfBound();
			}
			CreatingRect = FIntRect();
		}
		else if (DragMode == EHitboxDragMode::Moving || DragMode == EHitboxDragMode::Resizing)
		{
			// Only end the transaction if a mutation actually opened one (dead-zone: a plain
			// select-click leaves bDragTransactionOpen false, so nothing is committed or dirtied).
			if (bDragTransactionOpen)
			{
				OnEndTransaction.ExecuteIfBound();
				bDragTransactionOpen = false;
			}
		}

		DragMode = EHitboxDragMode::None;
		ActiveHandle = EResizeHandle::None;

		return FReply::Handled().ReleaseMouseCapture();
	}

	return FReply::Unhandled();
}

FReply SCharacterProfileEditorCanvas::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (!HasMouseCapture())
	{
		if (DragMode != EHitboxDragMode::None)
		{
			SettleAndResetDragState();
		}
		return FReply::Unhandled();
	}

	FVector2D CanvasPos = ScreenToCanvas(MyGeometry, MouseEvent.GetScreenSpacePosition());
	DragCurrent = CanvasPos;

	if (DragMode == EHitboxDragMode::Creating)
	{
		int32 SnappedX = FMath::RoundToInt(CanvasPos.X);
		int32 SnappedY = FMath::RoundToInt(CanvasPos.Y);

		int32 StartX = FMath::RoundToInt(DragStart.X);
		int32 StartY = FMath::RoundToInt(DragStart.Y);

		FVector2D Dims = GetSpriteDimensions();
		int32 MaxX = FMath::RoundToInt(Dims.X);
		int32 MaxY = FMath::RoundToInt(Dims.Y);

		CreatingRect = FIntRect(
			FMath::Clamp(FMath::Min(StartX, SnappedX), 0, MaxX),
			FMath::Clamp(FMath::Min(StartY, SnappedY), 0, MaxY),
			FMath::Clamp(FMath::Max(StartX, SnappedX), 0, MaxX),
			FMath::Clamp(FMath::Max(StartY, SnappedY), 0, MaxY)
		);
	}
	else if (DragMode == EHitboxDragMode::Moving)
	{
		FVector2D Delta = CanvasPos - DragStart;
		int32 DeltaX = FMath::RoundToInt(Delta.X);
		int32 DeltaY = FMath::RoundToInt(Delta.Y);

		if (DeltaX != 0 || DeltaY != 0)
		{
			FFrameHitboxData* Frame = GetCurrentFrameMutable();
			if (Frame)
			{
				// Dead-zone: open the undo transaction lazily on the first real mutation, so a plain
				// select-click (no movement) never opens a transaction or dirties the asset.
				if (!bDragTransactionOpen)
				{
					OnRequestUndo.ExecuteIfBound();
					bDragTransactionOpen = true;
				}

				if (SelectionType == EHitboxSelectionType::Hitbox && SelectedIndices.Num() > 0)
				{
					// Move all selected hitboxes using stored start positions, clamped to bounds
					FVector2D Dims = GetSpriteDimensions();
					int32 MaxX = FMath::RoundToInt(Dims.X);
					int32 MaxY = FMath::RoundToInt(Dims.Y);
					for (const auto& Pair : DragStartPositions)
					{
						int32 Idx = Pair.Key;
						if (Frame->Hitboxes.IsValidIndex(Idx))
						{
							FHitboxData& HB = Frame->Hitboxes[Idx];
							HB.X = FMath::RoundToInt(Pair.Value.X) + DeltaX;
							HB.Y = FMath::RoundToInt(Pair.Value.Y) + DeltaY;
							// Non-negative upper bound: a hitbox wider/taller than the sprite must stay draggable
							// (pin at 0) instead of inverting the clamp into a negative pin (U9).
							const FIntPoint ClampedHB = HitboxCanvasUtils::ClampHitboxPositionToBounds(
								HB.X, HB.Y, HB.Width, HB.Height, MaxX, MaxY);
							HB.X = ClampedHB.X;
							HB.Y = ClampedHB.Y;
						}
					}
					OnHitboxDataModified.ExecuteIfBound();
				}
				else if (SelectionType == EHitboxSelectionType::Socket)
				{
					int32 SocketIndex = GetPrimarySelectedIndex();
					if (Frame->Sockets.IsValidIndex(SocketIndex))
					{
						Frame->Sockets[SocketIndex].X += DeltaX;
						Frame->Sockets[SocketIndex].Y += DeltaY;
						OnHitboxDataModified.ExecuteIfBound();

						DragStart.X += DeltaX;
						DragStart.Y += DeltaY;
					}
				}
			}

			// For hitboxes, we use absolute positioning from DragStartPositions, so don't update DragStart incrementally
			// But we do need to track the snapped offset for DragStartPositions-based approach
			// DragStart stays the same since we use absolute offsets from initial positions
		}
	}
	else if (DragMode == EHitboxDragMode::Resizing)
	{
		FFrameHitboxData* Frame = GetCurrentFrameMutable();
		int32 ResizeIndex = GetPrimarySelectedIndex();
		if (Frame && SelectionType == EHitboxSelectionType::Hitbox && Frame->Hitboxes.IsValidIndex(ResizeIndex))
		{
			FHitboxData& HB = Frame->Hitboxes[ResizeIndex];

			FVector2D Dims = GetSpriteDimensions();
			int32 BoundsW = FMath::RoundToInt(Dims.X);
			int32 BoundsH = FMath::RoundToInt(Dims.Y);
			int32 NewX = FMath::Clamp(FMath::RoundToInt(CanvasPos.X), 0, BoundsW);
			int32 NewY = FMath::Clamp(FMath::RoundToInt(CanvasPos.Y), 0, BoundsH);

			int32 Left = HB.X;
			int32 Top = HB.Y;
			int32 Right = HB.X + HB.Width;
			int32 Bottom = HB.Y + HB.Height;

			switch (ActiveHandle)
			{
				case EResizeHandle::TopLeft:
					Left = NewX; Top = NewY; break;
				case EResizeHandle::Top:
					Top = NewY; break;
				case EResizeHandle::TopRight:
					Right = NewX; Top = NewY; break;
				case EResizeHandle::Left:
					Left = NewX; break;
				case EResizeHandle::Right:
					Right = NewX; break;
				case EResizeHandle::BottomLeft:
					Left = NewX; Bottom = NewY; break;
				case EResizeHandle::Bottom:
					Bottom = NewY; break;
				case EResizeHandle::BottomRight:
					Right = NewX; Bottom = NewY; break;
				default: break;
			}

			const int32 MinSize = 1;
			if (Right - Left >= MinSize && Bottom - Top >= MinSize)
			{
				// Dead-zone: open the undo transaction lazily on the first real resize mutation.
				if (!bDragTransactionOpen)
				{
					OnRequestUndo.ExecuteIfBound();
					bDragTransactionOpen = true;
				}

				HB.X = FMath::Min(Left, Right);
				HB.Y = FMath::Min(Top, Bottom);
				HB.Width = FMath::Abs(Right - Left);
				HB.Height = FMath::Abs(Bottom - Top);
				OnHitboxDataModified.ExecuteIfBound();
			}
		}
	}

	return FReply::Handled();
}

void SCharacterProfileEditorCanvas::OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent)
{
	SettleAndResetDragState();
	SLeafWidget::OnMouseCaptureLost(CaptureLostEvent);
}

void SCharacterProfileEditorCanvas::SettleAndResetDragState()
{
	// Clear the transaction latch before invoking the owner: capture release and host deactivation can
	// synchronously re-enter this path, and the same gesture must settle exactly once.
	const bool bShouldEndTransaction = bDragTransactionOpen;
	bDragTransactionOpen = false;
	if (bShouldEndTransaction)
	{
		OnEndTransaction.ExecuteIfBound();
	}

	// Capture can be revoked during draw-create as well as move/resize. Discard every transient shape
	// so returning to a mode cannot resurrect a ghost drag.
	DragMode = EHitboxDragMode::None;
	ActiveHandle = EResizeHandle::None;
	CreatingRect = FIntRect();
	DragStartPositions.Empty();
	DragStart = FVector2D::ZeroVector;
	DragCurrent = FVector2D::ZeroVector;

	Invalidate(EInvalidateWidgetReason::Paint);
}

FReply SCharacterProfileEditorCanvas::OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	float ZoomDelta = MouseEvent.GetWheelDelta() * 0.1f;
	float CurrentZoom = Zoom.Get();
	float NewZoom = FMath::Clamp(CurrentZoom + ZoomDelta, 0.25f, 4.0f);

	if (NewZoom != CurrentZoom)
	{
		OnZoomChanged.ExecuteIfBound(NewZoom);
	}

	return FReply::Handled();
}

FReply SCharacterProfileEditorCanvas::OnMouseButtonDoubleClick(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		FVector2D CanvasPos = ScreenToCanvas(MyGeometry, MouseEvent.GetScreenSpacePosition());

		int32 HitIndex = HitTestHitbox(CanvasPos);
		if (HitIndex >= 0)
		{
			ClearSelection();
			AddToSelection(HitIndex);
			SelectionType = EHitboxSelectionType::Hitbox;
			OnSelectionChanged.ExecuteIfBound(EHitboxSelectionType::Hitbox, HitIndex);
			return FReply::Handled();
		}

		int32 HitSocket = HitTestSocket(CanvasPos);
		if (HitSocket >= 0)
		{
			ClearSelection();
			AddToSelection(HitSocket);
			SelectionType = EHitboxSelectionType::Socket;
			OnSelectionChanged.ExecuteIfBound(EHitboxSelectionType::Socket, HitSocket);
			return FReply::Handled();
		}
	}
	return FReply::Unhandled();
}

FReply SCharacterProfileEditorCanvas::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (Paper2DPlusEditor::SlateShortcutUtils::ShouldIgnoreShortcutForFocusedWidget())
	{
		return FReply::Unhandled();
	}

	// Ctrl+0: Reset zoom
	if (InKeyEvent.IsControlDown() && InKeyEvent.GetKey() == EKeys::Zero)
	{
		OnZoomChanged.ExecuteIfBound(1.0f);
		return FReply::Handled();
	}

	if (InKeyEvent.GetKey() == EKeys::Delete || InKeyEvent.GetKey() == EKeys::BackSpace)
	{
		DeleteSelection();
		return FReply::Handled();
	}

	// WASD for hitbox/socket nudging
	if (!InKeyEvent.IsControlDown())
	{
		int32 NudgeAmount = InKeyEvent.IsShiftDown() ? 4 : 1;

		if (InKeyEvent.GetKey() == EKeys::A)
		{
			NudgeSelection(-NudgeAmount, 0);
			return FReply::Handled();
		}
		if (InKeyEvent.GetKey() == EKeys::D)
		{
			NudgeSelection(NudgeAmount, 0);
			return FReply::Handled();
		}
		if (InKeyEvent.GetKey() == EKeys::W)
		{
			NudgeSelection(0, -NudgeAmount);
			return FReply::Handled();
		}
		if (InKeyEvent.GetKey() == EKeys::S)
		{
			NudgeSelection(0, NudgeAmount);
			return FReply::Handled();
		}
	}

	return FReply::Unhandled();
}

void SCharacterProfileEditorCanvas::SetSelection(EHitboxSelectionType Type, int32 Index)
{
	SelectionType = Type;
	SelectedIndices.Empty();
	if (Type != EHitboxSelectionType::None && Index >= 0)
	{
		SelectedIndices.Add(Index);
	}
	OnSelectionChanged.ExecuteIfBound(SelectionType, GetPrimarySelectedIndex());
}

void SCharacterProfileEditorCanvas::AddToSelection(int32 Index)
{
	if (Index < 0) return;
	SelectionType = EHitboxSelectionType::Hitbox;
	if (!SelectedIndices.Contains(Index))
	{
		SelectedIndices.Add(Index);
	}
	else
	{
		// Move to end to make it the primary selection
		SelectedIndices.Remove(Index);
		SelectedIndices.Add(Index);
	}
	OnSelectionChanged.ExecuteIfBound(SelectionType, GetPrimarySelectedIndex());
}

void SCharacterProfileEditorCanvas::RemoveFromSelection(int32 Index)
{
	SelectedIndices.Remove(Index);
	if (SelectedIndices.Num() == 0)
	{
		SelectionType = EHitboxSelectionType::None;
	}
	OnSelectionChanged.ExecuteIfBound(SelectionType, GetPrimarySelectedIndex());
}

void SCharacterProfileEditorCanvas::ToggleSelection(int32 Index)
{
	if (SelectedIndices.Contains(Index))
	{
		RemoveFromSelection(Index);
	}
	else
	{
		AddToSelection(Index);
	}
}

void SCharacterProfileEditorCanvas::ClearSelection()
{
	SelectionType = EHitboxSelectionType::None;
	SelectedIndices.Empty();
	OnSelectionChanged.ExecuteIfBound(SelectionType, -1);
}

bool SCharacterProfileEditorCanvas::IsSelected(int32 Index) const
{
	return SelectedIndices.Contains(Index);
}

int32 SCharacterProfileEditorCanvas::GetPrimarySelectedIndex() const
{
	if (SelectedIndices.Num() > 0)
	{
		return SelectedIndices.Last();
	}
	return -1;
}

void SCharacterProfileEditorCanvas::NudgeSelection(int32 DeltaX, int32 DeltaY)
{
	FFrameHitboxData* Frame = GetCurrentFrameMutable();
	if (!Frame) return;

	if (SelectionType == EHitboxSelectionType::Hitbox && SelectedIndices.Num() > 0)
	{
		OnRequestUndo.ExecuteIfBound();
		for (int32 Idx : SelectedIndices)
		{
			if (Frame->Hitboxes.IsValidIndex(Idx))
			{
				Frame->Hitboxes[Idx].X += DeltaX;
				Frame->Hitboxes[Idx].Y += DeltaY;
			}
		}
		OnHitboxDataModified.ExecuteIfBound();
		OnEndTransaction.ExecuteIfBound();
	}
	else if (SelectionType == EHitboxSelectionType::Socket)
	{
		int32 SocketIndex = GetPrimarySelectedIndex();
		if (Frame->Sockets.IsValidIndex(SocketIndex))
		{
			OnRequestUndo.ExecuteIfBound();
			Frame->Sockets[SocketIndex].X += DeltaX;
			Frame->Sockets[SocketIndex].Y += DeltaY;
			OnHitboxDataModified.ExecuteIfBound();
			OnEndTransaction.ExecuteIfBound();
		}
	}
}

void SCharacterProfileEditorCanvas::DeleteSelection()
{
	FFrameHitboxData* Frame = GetCurrentFrameMutable();
	if (!Frame) return;

	if (SelectionType == EHitboxSelectionType::Hitbox && SelectedIndices.Num() > 0)
	{
		OnRequestUndo.ExecuteIfBound();

		// Sort indices in reverse order to delete from end first, preserving earlier indices
		TArray<int32> SortedIndices = SelectedIndices;
		SortedIndices.Sort([](int32 A, int32 B) { return A > B; });

		for (int32 Idx : SortedIndices)
		{
			if (Frame->Hitboxes.IsValidIndex(Idx))
			{
				Frame->Hitboxes.RemoveAt(Idx);
			}
		}

		ClearSelection();
		OnHitboxDataModified.ExecuteIfBound();
		OnEndTransaction.ExecuteIfBound();
	}
	else if (SelectionType == EHitboxSelectionType::Socket)
	{
		int32 SocketIndex = GetPrimarySelectedIndex();
		if (Frame->Sockets.IsValidIndex(SocketIndex))
		{
			OnRequestUndo.ExecuteIfBound();
			Frame->Sockets.RemoveAt(SocketIndex);
			ClearSelection();
			OnHitboxDataModified.ExecuteIfBound();
			OnEndTransaction.ExecuteIfBound();
		}
	}
}

#undef LOCTEXT_NAMESPACE
