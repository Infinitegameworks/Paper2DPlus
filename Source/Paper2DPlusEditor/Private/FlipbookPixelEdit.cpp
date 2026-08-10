// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "FlipbookPixelEdit.h"
#include "Paper2DPlusSpriteSourceUtils.h"
#include "PaperSprite.h"
#include "Engine/Texture2D.h"
#include "Engine/Texture.h"

bool FFlipbookPixelEdit::ReadFrame(UPaperSprite* Sprite, TArray<FColor>& OutPixels, int32& OutW, int32& OutH)
{
	// Reuse the proven source-region reader (BGRA8, fail-closed) rather than re-implement the mip math.
	return Paper2DPlusSpriteSourceUtils::ReadSourceRegion(Sprite, OutPixels, OutW, OutH);
}

bool FFlipbookPixelEdit::WriteFrame(UPaperSprite* Sprite, const TArray<FColor>& Pixels, int32 W, int32 H)
{
	if (!Sprite || W <= 0 || H <= 0 || Pixels.Num() != W * H)
	{
		return false;
	}

	UTexture2D* Texture = Cast<UTexture2D>(Sprite->GetSourceTexture());
	if (!Texture)
	{
		return false;
	}

	FTextureSource& Source = Texture->Source;
	if (Source.GetFormat() != TSF_BGRA8)
	{
		return false; // Phase 5 adds a convert-to-BGRA8 path; for now fail closed (never corrupt).
	}

	const int32 TexW = Source.GetSizeX();
	const int32 TexH = Source.GetSizeY();
	const FVector2D UVf = Sprite->GetSourceUV();
	const int32 UVx = FMath::RoundToInt(UVf.X);
	const int32 UVy = FMath::RoundToInt(UVf.Y);

	// Bounds-check the whole window before touching the mip (never write outside this frame's cell).
	if (UVx < 0 || UVy < 0 || UVx + W > TexW || UVy + H > TexH)
	{
		return false;
	}

	uint8* Dest = Source.LockMip(0);
	if (!Dest)
	{
		return false;
	}

	// FColor bytes are B,G,R,A == TSF_BGRA8 byte order, so each row copies straight across.
	const uint8* SrcBytes = reinterpret_cast<const uint8*>(Pixels.GetData());
	for (int32 Y = 0; Y < H; ++Y)
	{
		uint8* DestRow = Dest + ((int64)(UVy + Y) * TexW + UVx) * 4;
		const uint8* SrcRow = SrcBytes + (int64)Y * W * 4;
		FMemory::Memcpy(DestRow, SrcRow, (int64)W * 4);
	}

	Source.UnlockMip(0);
	Texture->UpdateResource();
	Texture->MarkPackageDirty();

	// Sprite geometry can be DERIVED from these pixels — TightBoundingBox (the engine's default for
	// every sprite's CollisionGeometry), ShrinkWrapped and Diced all re-scan the source alpha — and the
	// result is SERIALIZED on the sprite. Nothing invalidates it when a texture's pixels change:
	// UPaperSprite::PostLoad only rebuilds on asset VERSION upgrades. Skip this and the sprite keeps
	// drawing and colliding with the box that fitted the art BEFORE the edit — cropped and offset — until
	// someone happens to touch a sprite property and unknowingly fires PostEditChangeProperty ->
	// RebuildData. Runs once per stroke / transform / undo step (not per pixel), and has nothing to
	// recompute for SourceBoundingBox or FullyCustom geometry, which never read pixels.
	Sprite->RebuildData();

	// The rebuilt geometry lives on the SPRITE package — usually a different asset from the texture
	// dirtied above — so without this it is correct in memory and gone again on the next load.
	Sprite->MarkPackageDirty();
	return true;
}

FIntRect FFlipbookPixelEdit::EmptyDirtyRect()
{
	return FIntRect(MAX_int32, MAX_int32, MIN_int32, MIN_int32);
}

bool FFlipbookPixelEdit::IsDirtyValid(const FIntRect& R)
{
	return R.Min.X < R.Max.X && R.Min.Y < R.Max.Y;
}

void FFlipbookPixelEdit::UnionDirty(FIntRect& A, const FIntRect& B)
{
	A.Min.X = FMath::Min(A.Min.X, B.Min.X);
	A.Min.Y = FMath::Min(A.Min.Y, B.Min.Y);
	A.Max.X = FMath::Max(A.Max.X, B.Max.X);
	A.Max.Y = FMath::Max(A.Max.Y, B.Max.Y);
}

void FFlipbookPixelEdit::StampDab(TArray<FColor>& Buf, int32 W, int32 H, int32 CX, int32 CY, int32 BrushSize,
	const FColor& Color, FIntRect& InOutDirty)
{
	if (W <= 0 || H <= 0 || Buf.Num() != W * H || BrushSize <= 0)
	{
		return;
	}

	// Square brush centered on (CX,CY). half = BrushSize/2 → BrushSize 1 paints exactly one pixel.
	const int32 Half = BrushSize / 2;
	const int32 X0 = CX - Half;
	const int32 Y0 = CY - Half;

	for (int32 OY = 0; OY < BrushSize; ++OY)
	{
		const int32 PY = Y0 + OY;
		if (PY < 0 || PY >= H)
		{
			continue;
		}
		for (int32 OX = 0; OX < BrushSize; ++OX)
		{
			const int32 PX = X0 + OX;
			if (PX < 0 || PX >= W)
			{
				continue;
			}
			Buf[PY * W + PX] = Color;
			InOutDirty.Min.X = FMath::Min(InOutDirty.Min.X, PX);
			InOutDirty.Min.Y = FMath::Min(InOutDirty.Min.Y, PY);
			InOutDirty.Max.X = FMath::Max(InOutDirty.Max.X, PX + 1);
			InOutDirty.Max.Y = FMath::Max(InOutDirty.Max.Y, PY + 1);
		}
	}
}

void FFlipbookPixelEdit::StampLine(TArray<FColor>& Buf, int32 W, int32 H, int32 X0, int32 Y0, int32 X1, int32 Y1,
	int32 BrushSize, const FColor& Color, FIntRect& InOutDirty)
{
	// Standard integer Bresenham; stamp a dab at every step so a fast drag leaves no gaps.
	int32 DX = FMath::Abs(X1 - X0);
	int32 DY = -FMath::Abs(Y1 - Y0);
	int32 SX = (X0 < X1) ? 1 : -1;
	int32 SY = (Y0 < Y1) ? 1 : -1;
	int32 Err = DX + DY;

	int32 X = X0;
	int32 Y = Y0;
	for (;;)
	{
		StampDab(Buf, W, H, X, Y, BrushSize, Color, InOutDirty);
		if (X == X1 && Y == Y1)
		{
			break;
		}
		const int32 E2 = 2 * Err;
		if (E2 >= DY)
		{
			Err += DY;
			X += SX;
		}
		if (E2 <= DX)
		{
			Err += DX;
			Y += SY;
		}
	}
}

void FFlipbookPixelEdit::StampDabMirrored(TArray<FColor>& Buf, int32 W, int32 H, int32 CX, int32 CY, int32 BrushSize,
	const FColor& Color, bool bMirrorX, bool bMirrorY, FIntRect& InOutDirty)
{
	StampDab(Buf, W, H, CX, CY, BrushSize, Color, InOutDirty);
	const int32 MX = W - 1 - CX;
	const int32 MY = H - 1 - CY;
	if (bMirrorX)           { StampDab(Buf, W, H, MX, CY, BrushSize, Color, InOutDirty); }
	if (bMirrorY)           { StampDab(Buf, W, H, CX, MY, BrushSize, Color, InOutDirty); }
	if (bMirrorX && bMirrorY) { StampDab(Buf, W, H, MX, MY, BrushSize, Color, InOutDirty); }
}

void FFlipbookPixelEdit::StampLineMirrored(TArray<FColor>& Buf, int32 W, int32 H, int32 X0, int32 Y0, int32 X1, int32 Y1,
	int32 BrushSize, const FColor& Color, bool bMirrorX, bool bMirrorY, FIntRect& InOutDirty)
{
	StampLine(Buf, W, H, X0, Y0, X1, Y1, BrushSize, Color, InOutDirty);
	const int32 MX0 = W - 1 - X0, MX1 = W - 1 - X1;
	const int32 MY0 = H - 1 - Y0, MY1 = H - 1 - Y1;
	if (bMirrorX)           { StampLine(Buf, W, H, MX0, Y0, MX1, Y1, BrushSize, Color, InOutDirty); }
	if (bMirrorY)           { StampLine(Buf, W, H, X0, MY0, X1, MY1, BrushSize, Color, InOutDirty); }
	if (bMirrorX && bMirrorY) { StampLine(Buf, W, H, MX0, MY0, MX1, MY1, BrushSize, Color, InOutDirty); }
}

void FFlipbookPixelEdit::StampRectangle(TArray<FColor>& Buf, int32 W, int32 H,
	const FIntPoint& Start, const FIntPoint& End, int32 BrushSize, const FColor& Color, FIntRect& InOutDirty)
{
	const int32 MinX = FMath::Min(Start.X, End.X);
	const int32 MaxX = FMath::Max(Start.X, End.X);
	const int32 MinY = FMath::Min(Start.Y, End.Y);
	const int32 MaxY = FMath::Max(Start.Y, End.Y);
	if (MinX == MaxX)
	{
		StampLine(Buf, W, H, MinX, MinY, MinX, MaxY, BrushSize, Color, InOutDirty);
		return;
	}
	if (MinY == MaxY)
	{
		StampLine(Buf, W, H, MinX, MinY, MaxX, MinY, BrushSize, Color, InOutDirty);
		return;
	}

	StampLine(Buf, W, H, MinX, MinY, MaxX, MinY, BrushSize, Color, InOutDirty);
	StampLine(Buf, W, H, MinX, MaxY, MaxX, MaxY, BrushSize, Color, InOutDirty);
	StampLine(Buf, W, H, MinX, MinY, MinX, MaxY, BrushSize, Color, InOutDirty);
	StampLine(Buf, W, H, MaxX, MinY, MaxX, MaxY, BrushSize, Color, InOutDirty);
}

void FFlipbookPixelEdit::StampRectangleMirrored(TArray<FColor>& Buf, int32 W, int32 H,
	const FIntPoint& Start, const FIntPoint& End, int32 BrushSize, const FColor& Color,
	bool bMirrorX, bool bMirrorY, FIntRect& InOutDirty)
{
	StampRectangle(Buf, W, H, Start, End, BrushSize, Color, InOutDirty);
	const FIntPoint MirrorXStart(W - 1 - Start.X, Start.Y);
	const FIntPoint MirrorXEnd(W - 1 - End.X, End.Y);
	const FIntPoint MirrorYStart(Start.X, H - 1 - Start.Y);
	const FIntPoint MirrorYEnd(End.X, H - 1 - End.Y);
	if (bMirrorX)
	{
		StampRectangle(Buf, W, H, MirrorXStart, MirrorXEnd, BrushSize, Color, InOutDirty);
	}
	if (bMirrorY)
	{
		StampRectangle(Buf, W, H, MirrorYStart, MirrorYEnd, BrushSize, Color, InOutDirty);
	}
	if (bMirrorX && bMirrorY)
	{
		StampRectangle(Buf, W, H,
			FIntPoint(MirrorXStart.X, MirrorYStart.Y), FIntPoint(MirrorXEnd.X, MirrorYEnd.Y),
			BrushSize, Color, InOutDirty);
	}
}

void FFlipbookPixelEdit::FloodFill(TArray<FColor>& Buf, int32 W, int32 H, int32 X, int32 Y, const FColor& NewColor,
	FIntRect& InOutDirty)
{
	if (W <= 0 || H <= 0 || Buf.Num() != W * H || X < 0 || X >= W || Y < 0 || Y >= H)
	{
		return;
	}
	const FColor Target = Buf[Y * W + X];
	if (Target == NewColor)
	{
		return; // nothing to do
	}

	TArray<FIntPoint> Stack;
	Stack.Push(FIntPoint(X, Y));
	while (Stack.Num() > 0)
	{
		const FIntPoint P = Stack.Pop();
		if (P.X < 0 || P.X >= W || P.Y < 0 || P.Y >= H)
		{
			continue;
		}
		const int32 Idx = P.Y * W + P.X;
		if (Buf[Idx] != Target)
		{
			continue; // already filled or a boundary color
		}
		Buf[Idx] = NewColor;
		InOutDirty.Min.X = FMath::Min(InOutDirty.Min.X, P.X);
		InOutDirty.Min.Y = FMath::Min(InOutDirty.Min.Y, P.Y);
		InOutDirty.Max.X = FMath::Max(InOutDirty.Max.X, P.X + 1);
		InOutDirty.Max.Y = FMath::Max(InOutDirty.Max.Y, P.Y + 1);
		Stack.Push(FIntPoint(P.X + 1, P.Y));
		Stack.Push(FIntPoint(P.X - 1, P.Y));
		Stack.Push(FIntPoint(P.X, P.Y + 1));
		Stack.Push(FIntPoint(P.X, P.Y - 1));
	}
}

bool FFlipbookPixelEdit::TransformFrame(TArray<FColor>& Buf, int32 W, int32 H, EFlipbookPixelTransform Transform)
{
	if (W <= 0 || H <= 0 || Buf.Num() != W * H)
	{
		return false;
	}

	const FColor Transparent(0, 0, 0, 0);
	TArray<FColor> Transformed;
	Transformed.Init(Transparent, W * H);

	int32 ShiftX = 0;
	int32 ShiftY = 0;
	switch (Transform)
	{
	case EFlipbookPixelTransform::FlipHorizontal:
		for (int32 Y = 0; Y < H; ++Y)
		{
			for (int32 X = 0; X < W; ++X)
			{
				Transformed[Y * W + X] = Buf[Y * W + (W - 1 - X)];
			}
		}
		break;
	case EFlipbookPixelTransform::FlipVertical:
		for (int32 Y = 0; Y < H; ++Y)
		{
			for (int32 X = 0; X < W; ++X)
			{
				Transformed[Y * W + X] = Buf[(H - 1 - Y) * W + X];
			}
		}
		break;
	case EFlipbookPixelTransform::Rotate180:
		for (int32 Index = 0; Index < Buf.Num(); ++Index)
		{
			Transformed[Index] = Buf[Buf.Num() - 1 - Index];
		}
		break;
	case EFlipbookPixelTransform::ShiftLeft:  ShiftX = -1; break;
	case EFlipbookPixelTransform::ShiftRight: ShiftX = 1;  break;
	case EFlipbookPixelTransform::ShiftUp:    ShiftY = -1; break;
	case EFlipbookPixelTransform::ShiftDown:  ShiftY = 1;  break;
	case EFlipbookPixelTransform::Clear:
		break;
	default:
		return false;
	}

	if (ShiftX != 0 || ShiftY != 0)
	{
		for (int32 Y = 0; Y < H; ++Y)
		{
			for (int32 X = 0; X < W; ++X)
			{
				const int32 DestX = X + ShiftX;
				const int32 DestY = Y + ShiftY;
				if (DestX >= 0 && DestX < W && DestY >= 0 && DestY < H)
				{
					Transformed[DestY * W + DestX] = Buf[Y * W + X];
				}
			}
		}
	}

	if (Transformed == Buf)
	{
		return false;
	}
	Buf = MoveTemp(Transformed);
	return true;
}
