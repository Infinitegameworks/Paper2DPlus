// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "CharacterProfileAssetEditor.h"
#include "EditorCanvasUtils.h"
#include "PaperFlipbook.h"
#include "PaperSprite.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Engine/Texture2D.h"

#define LOCTEXT_NAMESPACE "CharacterProfileAssetEditor"

// ==========================================
// SCharacterProfileEditorCanvas Implementation
// ==========================================

void SCharacterProfileEditorCanvas::Construct(const FArguments& InArgs)
{
	Asset = InArgs._Asset;
	SelectedFlipbookIndex = InArgs._SelectedFlipbookIndex;
	SelectedFrameIndex = InArgs._SelectedFrameIndex;
	CurrentTool = InArgs._CurrentTool;
	ShowGrid = InArgs._ShowGrid;
	Zoom = InArgs._Zoom;
	VisibilityMask = InArgs._VisibilityMask;

	// Clip all drawing to canvas bounds so hitboxes don't bleed over UI chrome
	SetClipping(EWidgetClipping::ClipToBounds);
}

FVector2D SCharacterProfileEditorCanvas::ComputeDesiredSize(float) const
{
	return FVector2D(600, 500);
}

FVector2D SCharacterProfileEditorCanvas::GetSpriteDimensions() const
{
	UPaperSprite* Sprite = nullptr;
	FVector2D Dimensions(128.0f, 128.0f);
	GetCurrentSpriteInfo(Sprite, Dimensions);
	return Dimensions;
}

FVector2D SCharacterProfileEditorCanvas::GetLargestSpriteDims() const
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

	return (LocalPos - Offset) / EffectiveZoom;
}

FVector2D SCharacterProfileEditorCanvas::CanvasToScreen(const FGeometry& Geom, const FVector2D& CanvasPos) const
{
	FVector2D Offset = GetCanvasOffset(Geom);
	float EffectiveZoom = GetEffectiveZoom(Geom);

	return Offset + CanvasPos * EffectiveZoom;
}

int32 SCharacterProfileEditorCanvas::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId,
	const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	// Draw checkerboard background matching Alignment editor
	FEditorCanvasUtils::DrawCheckerboard(OutDrawElements, LayerId, AllottedGeometry);

	if (ShowGrid.Get())
	{
		DrawGrid(AllottedGeometry, OutDrawElements, LayerId + 1);
	}

	const FFrameHitboxData* Frame = GetCurrentFrame();

	UPaperSprite* Sprite = nullptr;
	FVector2D SpriteDimensions(128.0f, 128.0f);
	bool bHasSprite = GetCurrentSpriteInfo(Sprite, SpriteDimensions);

	float EffectiveZoom = GetEffectiveZoom(AllottedGeometry);
	FVector2D Offset = GetCanvasOffset(AllottedGeometry);

	if (bHasSprite && Sprite)
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
				AllottedGeometry.ToPaintGeometry(FVector2f(SpriteDrawSize), FSlateLayoutTransform(FVector2f(Offset))),
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

	if (Frame)
	{
		uint8 Mask = VisibilityMask.Get();
		for (int32 i = 0; i < Frame->Hitboxes.Num(); i++)
		{
			const FHitboxData& HB = Frame->Hitboxes[i];
			if (!(Mask & (1 << static_cast<uint8>(HB.Type)))) continue;
			bool bSelected = (SelectionType == EHitboxSelectionType::Hitbox && IsSelected(i));
			DrawHitbox(AllottedGeometry, OutDrawElements, LayerId + 4, HB, bSelected);
		}

		// Draw resize handles only on the primary selected hitbox (last one clicked)
		int32 PrimaryIndex = GetPrimarySelectedIndex();
		if (SelectionType == EHitboxSelectionType::Hitbox &&
			Frame->Hitboxes.IsValidIndex(PrimaryIndex) &&
			(Mask & (1 << static_cast<uint8>(Frame->Hitboxes[PrimaryIndex].Type))) &&
			(CurrentTool.Get() == EHitboxEditorTool::Edit || CurrentTool.Get() == EHitboxEditorTool::Draw))
		{
			DrawResizeHandles(AllottedGeometry, OutDrawElements, LayerId + 6, Frame->Hitboxes[PrimaryIndex]);
		}

		for (int32 i = 0; i < Frame->Sockets.Num(); i++)
		{
			bool bSelected = (SelectionType == EHitboxSelectionType::Socket && SelectedIndices.Contains(i));
			DrawSocket(AllottedGeometry, OutDrawElements, LayerId + 5, Frame->Sockets[i], bSelected);
		}
	}

	if (DragMode == EHitboxDragMode::Creating)
	{
		DrawCreatingRect(AllottedGeometry, OutDrawElements, LayerId + 7);
	}

	return LayerId + 8;
}

void SCharacterProfileEditorCanvas::DrawGrid(const FGeometry& Geom, FSlateWindowElementList& OutDrawElements, int32 LayerId) const
{
	float EffectiveZoom = GetEffectiveZoom(Geom);
	FVector2D Offset = GetCanvasOffset(Geom);
	FVector2D SpriteDims = GetSpriteDimensions();

	for (float X = 0; X <= SpriteDims.X; X += GridSize)
	{
		float ScreenX = Offset.X + X * EffectiveZoom;
		TArray<FVector2D> Line = {
			FVector2D(ScreenX, Offset.Y),
			FVector2D(ScreenX, Offset.Y + SpriteDims.Y * EffectiveZoom)
		};
		FSlateDrawElement::MakeLines(
			OutDrawElements, LayerId, Geom.ToPaintGeometry(),
			Line, ESlateDrawEffect::None,
			FLinearColor(0.2f, 0.2f, 0.2f, 0.5f), true, 1.0f
		);
	}

	for (float Y = 0; Y <= SpriteDims.Y; Y += GridSize)
	{
		float ScreenY = Offset.Y + Y * EffectiveZoom;
		TArray<FVector2D> Line = {
			FVector2D(Offset.X, ScreenY),
			FVector2D(Offset.X + SpriteDims.X * EffectiveZoom, ScreenY)
		};
		FSlateDrawElement::MakeLines(
			OutDrawElements, LayerId, Geom.ToPaintGeometry(),
			Line, ESlateDrawEffect::None,
			FLinearColor(0.2f, 0.2f, 0.2f, 0.5f), true, 1.0f
		);
	}
}

void SCharacterProfileEditorCanvas::DrawHitbox(const FGeometry& Geom, FSlateWindowElementList& OutDrawElements,
	int32 LayerId, const FHitboxData& HB, bool bSelected) const
{
	float EffectiveZoom = GetEffectiveZoom(Geom);
	FVector2D Offset = GetCanvasOffset(Geom);

	FLinearColor Color = GetHitboxColor(HB.Type);
	FVector2D Pos = Offset + FVector2D(HB.X, HB.Y) * EffectiveZoom;
	FVector2D BoxSize(HB.Width * EffectiveZoom, HB.Height * EffectiveZoom);

	float FillAlpha = bSelected ? 0.5f : 0.3f;
	FSlateDrawElement::MakeBox(
		OutDrawElements, LayerId,
		Geom.ToPaintGeometry(FVector2f(BoxSize), FSlateLayoutTransform(FVector2f(Pos))),
		FAppStyle::GetBrush("WhiteBrush"),
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

void SCharacterProfileEditorCanvas::DrawSocket(const FGeometry& Geom, FSlateWindowElementList& OutDrawElements,
	int32 LayerId, const FSocketData& Sock, bool bSelected) const
{
	float EffectiveZoom = GetEffectiveZoom(Geom);
	FVector2D Offset = GetCanvasOffset(Geom);

	FVector2D Pos = Offset + FVector2D(Sock.X, Sock.Y) * EffectiveZoom;
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
	float EffectiveZoom = GetEffectiveZoom(Geom);
	FVector2D Offset = GetCanvasOffset(Geom);

	FVector2D TopLeft = Offset + FVector2D(HB.X, HB.Y) * EffectiveZoom;
	FVector2D BottomRight = Offset + FVector2D(HB.X + HB.Width, HB.Y + HB.Height) * EffectiveZoom;
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
			Geom.ToPaintGeometry(FVector2f(HandleSize, HandleSize), FSlateLayoutTransform(FVector2f(HandleTopLeft))),
			FAppStyle::GetBrush("WhiteBrush"),
			ESlateDrawEffect::None,
			FLinearColor::White
		);
	}
}

void SCharacterProfileEditorCanvas::DrawCreatingRect(const FGeometry& Geom, FSlateWindowElementList& OutDrawElements, int32 LayerId) const
{
	float EffectiveZoom = GetEffectiveZoom(Geom);
	FVector2D Offset = GetCanvasOffset(Geom);

	FVector2D Pos = Offset + FVector2D(CreatingRect.Min.X, CreatingRect.Min.Y) * EffectiveZoom;
	FVector2D Size = FVector2D(CreatingRect.Width(), CreatingRect.Height()) * EffectiveZoom;

	FSlateDrawElement::MakeBox(
		OutDrawElements, LayerId,
		Geom.ToPaintGeometry(FVector2f(Size), FSlateLayoutTransform(FVector2f(Pos))),
		FAppStyle::GetBrush("WhiteBrush"),
		ESlateDrawEffect::None,
		FLinearColor(1.0f, 1.0f, 1.0f, 0.2f)
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
		FLinearColor::White, true, 2.0f
	);
}

FLinearColor SCharacterProfileEditorCanvas::GetHitboxColor(EHitboxType Type) const
{
	switch (Type)
	{
		case EHitboxType::Attack: return FLinearColor::Red;
		case EHitboxType::Hurtbox: return FLinearColor::Green;
		case EHitboxType::Collision: return FLinearColor::Blue;
		default: return FLinearColor::White;
	}
}

const FFrameHitboxData* SCharacterProfileEditorCanvas::GetCurrentFrame() const
{
	const FFlipbookHitboxData* Anim = GetCurrentFlipbookData();
	if (!Anim) return nullptr;

	int32 FrameIndex = SelectedFrameIndex.Get();
	if (!Anim->Frames.IsValidIndex(FrameIndex)) return nullptr;

	return &Anim->Frames[FrameIndex];
}

FFrameHitboxData* SCharacterProfileEditorCanvas::GetCurrentFrameMutable() const
{
	if (!Asset.IsValid()) return nullptr;

	int32 FlipbookIndex = SelectedFlipbookIndex.Get();
	if (!Asset->Flipbooks.IsValidIndex(FlipbookIndex)) return nullptr;

	int32 FrameIndex = SelectedFrameIndex.Get();
	if (!Asset->Flipbooks[FlipbookIndex].Frames.IsValidIndex(FrameIndex)) return nullptr;

	return &Asset->Flipbooks[FlipbookIndex].Frames[FrameIndex];
}

const FFlipbookHitboxData* SCharacterProfileEditorCanvas::GetCurrentFlipbookData() const
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

	const FFlipbookHitboxData* Anim = GetCurrentFlipbookData();
	if (!Anim) return false;

	if (Anim->Flipbook.IsNull()) return false;

	UPaperFlipbook* Flipbook = Anim->Flipbook.LoadSynchronous();
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

	uint8 Mask = VisibilityMask.Get();
	for (int32 i = Frame->Hitboxes.Num() - 1; i >= 0; i--)
	{
		const FHitboxData& HB = Frame->Hitboxes[i];
		if (!(Mask & (1 << static_cast<uint8>(HB.Type)))) continue;
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

int32 SCharacterProfileEditorCanvas::SnapToGrid(int32 Value) const
{
	if (!ShowGrid.Get()) return Value;
	return FMath::RoundToInt((float)Value / GridSize) * GridSize;
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
						OnRequestUndo.ExecuteIfBound();
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
				OnRequestUndo.ExecuteIfBound();
				return FReply::Handled().CaptureMouse(SharedThis(const_cast<SCharacterProfileEditorCanvas*>(this)));
			}

			int32 HitSocket = HitTestSocket(CanvasPos);
			if (HitSocket >= 0)
			{
				// Sockets keep single-select behavior
				SetSelection(EHitboxSelectionType::Socket, HitSocket);
				DragMode = EHitboxDragMode::Moving;
				DragStart = CanvasPos;
				OnRequestUndo.ExecuteIfBound();
				return FReply::Handled().CaptureMouse(SharedThis(const_cast<SCharacterProfileEditorCanvas*>(this)));
			}

			// Empty space: draw a new hitbox
			if (!bShiftDown)
			{
				ClearSelection();
			}

			int32 SnappedX = SnapToGrid(FMath::RoundToInt(CanvasPos.X));
			int32 SnappedY = SnapToGrid(FMath::RoundToInt(CanvasPos.Y));

			CreatingRect = FIntRect(SnappedX, SnappedY, SnappedX, SnappedY);
			DragMode = EHitboxDragMode::Creating;
			DragStart = CanvasPos;

			return FReply::Handled().CaptureMouse(SharedThis(const_cast<SCharacterProfileEditorCanvas*>(this)));
		}
		else if (Tool == EHitboxEditorTool::Socket)
		{
			FFrameHitboxData* Frame = GetCurrentFrameMutable();
			if (Frame)
			{
				OnRequestUndo.ExecuteIfBound();

				FSocketData NewSocket;
				NewSocket.Name = FString::Printf(TEXT("Socket%d"), Frame->Sockets.Num());
				NewSocket.X = SnapToGrid(FMath::RoundToInt(CanvasPos.X));
				NewSocket.Y = SnapToGrid(FMath::RoundToInt(CanvasPos.Y));

				int32 NewIndex = Frame->Sockets.Add(NewSocket);
				SetSelection(EHitboxSelectionType::Socket, NewIndex);
				OnHitboxDataModified.ExecuteIfBound();

				DragMode = EHitboxDragMode::Moving;
				DragStart = CanvasPos;
				return FReply::Handled().CaptureMouse(SharedThis(const_cast<SCharacterProfileEditorCanvas*>(this)));
			}
		}

		return FReply::Handled().SetUserFocus(SharedThis(const_cast<SCharacterProfileEditorCanvas*>(this)), EFocusCause::Mouse);
	}

	return FReply::Unhandled();
}

FReply SCharacterProfileEditorCanvas::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && HasMouseCapture())
	{
		if (DragMode == EHitboxDragMode::Creating)
		{
			if (CreatingRect.Width() > 0 && CreatingRect.Height() > 0)
			{
				FFrameHitboxData* Frame = GetCurrentFrameMutable();
				if (Frame)
				{
					OnRequestUndo.ExecuteIfBound();

					FHitboxData NewHitbox;
					NewHitbox.Type = EHitboxType::Hurtbox;
					NewHitbox.X = CreatingRect.Min.X;
					NewHitbox.Y = CreatingRect.Min.Y;
					NewHitbox.Width = CreatingRect.Width();
					NewHitbox.Height = CreatingRect.Height();
					NewHitbox.Damage = 0;
					NewHitbox.Knockback = 0;

					int32 NewIndex = Frame->Hitboxes.Add(NewHitbox);
					SetSelection(EHitboxSelectionType::Hitbox, NewIndex);
					OnHitboxDataModified.ExecuteIfBound();
					OnEndTransaction.ExecuteIfBound();
				}
			}
			CreatingRect = FIntRect();
		}
		else if (DragMode == EHitboxDragMode::Moving || DragMode == EHitboxDragMode::Resizing)
		{
			OnEndTransaction.ExecuteIfBound();
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
		return FReply::Unhandled();
	}

	FVector2D CanvasPos = ScreenToCanvas(MyGeometry, MouseEvent.GetScreenSpacePosition());
	DragCurrent = CanvasPos;

	if (DragMode == EHitboxDragMode::Creating)
	{
		int32 SnappedX = SnapToGrid(FMath::RoundToInt(CanvasPos.X));
		int32 SnappedY = SnapToGrid(FMath::RoundToInt(CanvasPos.Y));

		int32 StartX = SnapToGrid(FMath::RoundToInt(DragStart.X));
		int32 StartY = SnapToGrid(FMath::RoundToInt(DragStart.Y));

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
		int32 DeltaX = SnapToGrid(FMath::RoundToInt(Delta.X));
		int32 DeltaY = SnapToGrid(FMath::RoundToInt(Delta.Y));

		if (DeltaX != 0 || DeltaY != 0)
		{
			FFrameHitboxData* Frame = GetCurrentFrameMutable();
			if (Frame)
			{
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
							HB.X = FMath::Clamp(HB.X, 0, MaxX - HB.Width);
							HB.Y = FMath::Clamp(HB.Y, 0, MaxY - HB.Height);
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
			int32 NewX = FMath::Clamp(SnapToGrid(FMath::RoundToInt(CanvasPos.X)), 0, BoundsW);
			int32 NewY = FMath::Clamp(SnapToGrid(FMath::RoundToInt(CanvasPos.Y)), 0, BoundsH);

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
	if (DragMode == EHitboxDragMode::Moving || DragMode == EHitboxDragMode::Resizing)
	{
		OnEndTransaction.ExecuteIfBound();
	}

	DragMode = EHitboxDragMode::None;
	ActiveHandle = EResizeHandle::None;

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

	// Arrow keys for hitbox nudging when there's an active selection
	// (parent OnPreviewKeyDown defers to canvas when hitbox selection exists)
	int32 NudgeAmount = ShowGrid.Get() ? GridSize : 1;
	if (InKeyEvent.IsShiftDown()) NudgeAmount *= 4;

	if (InKeyEvent.GetKey() == EKeys::Left)
	{
		NudgeSelection(-NudgeAmount, 0);
		return FReply::Handled();
	}
	if (InKeyEvent.GetKey() == EKeys::Right)
	{
		NudgeSelection(NudgeAmount, 0);
		return FReply::Handled();
	}
	if (InKeyEvent.GetKey() == EKeys::Up)
	{
		NudgeSelection(0, -NudgeAmount);
		return FReply::Handled();
	}
	if (InKeyEvent.GetKey() == EKeys::Down)
	{
		NudgeSelection(0, NudgeAmount);
		return FReply::Handled();
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
