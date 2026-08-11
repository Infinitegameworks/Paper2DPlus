// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "FlipbookDrawCanvas.h"
#include "FlipbookDrawModel.h"
#include "EditorCanvasUtils.h"
#include "PaperSprite.h"
#include "Engine/Texture2D.h"
#include "Rendering/DrawElements.h"
// UE 5.0 compat: FAppStyle/AppStyle.h doesn't exist, use FEditorStyle
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
#include "EditorStyleSet.h"
#ifndef FAppStyle
#define FAppStyle FEditorStyle
#define GetAppStyleSetName GetStyleSetName
#endif
#else
#include "Styling/AppStyle.h"
#endif

namespace
{
	constexpr float FlipbookDrawRenderMinZoom = 0.25f;
	constexpr float FlipbookDrawRenderMaxZoom = 64.0f;
}

FVector2D SFlipbookDrawCanvas::GetFrameSize() const
{
	if (bDrawable && WorkW > 0 && WorkH > 0)
	{
		return FVector2D(WorkW, WorkH);
	}
	if (Model.IsValid())
	{
		if (UPaperSprite* Sprite = Model->GetCurrentSprite())
		{
			return FVector2D(Sprite->GetSourceSize());
		}
	}
	return FVector2D::ZeroVector;
}

FVector2D SFlipbookDrawCanvas::GetDrawSize(const FGeometry& Geom) const
{
	return GetFrameSize() * ZoomLevel;
}

FVector2D SFlipbookDrawCanvas::GetDrawPos(const FGeometry& Geom) const
{
	const FVector2D DrawSize = GetDrawSize(Geom);
	return PanOffset + (Geom.GetLocalSize() - DrawSize) * 0.5f;
}

FVector2D SFlipbookDrawCanvas::ScreenToFramePixel(const FGeometry& Geom, const FVector2D& LocalPos) const
{
	const FVector2D DrawPos = GetDrawPos(Geom);
	return (LocalPos - DrawPos) / FMath::Max(ZoomLevel, KINDA_SMALL_NUMBER);
}

FIntPoint SFlipbookDrawCanvas::ScreenToFramePixelInt(const FGeometry& Geom, const FVector2D& LocalPos) const
{
	const FVector2D FP = ScreenToFramePixel(Geom, LocalPos);
	return FIntPoint(FMath::FloorToInt(FP.X), FMath::FloorToInt(FP.Y));
}

void SFlipbookDrawCanvas::EnsureInitialFit(const FGeometry& Geom) const
{
	if (!bNeedsInitialFit)
	{
		return;
	}

	const FVector2D FrameSize = GetFrameSize();
	const FVector2D WidgetSize = Geom.GetLocalSize();
	if (FrameSize.X <= 0.0f || FrameSize.Y <= 0.0f || WidgetSize.X <= 1.0f || WidgetSize.Y <= 1.0f)
	{
		return; // wait until both the frame and a real geometry are available
	}

	const float FitZoom = FMath::Min(WidgetSize.X / FrameSize.X, WidgetSize.Y / FrameSize.Y) * 0.85f;
	ZoomLevel = FMath::Clamp(FitZoom, FlipbookDrawRenderMinZoom, FlipbookDrawRenderMaxZoom);
	PanOffset = FVector2D::ZeroVector;
	bNeedsInitialFit = false;
}

FVector2D SFlipbookDrawCanvas::ComputeDesiredSize(float) const
{
	return FVector2D(480, 480);
}

int32 SFlipbookDrawCanvas::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	// Solid background
	FSlateDrawElement::MakeBox(
		OutDrawElements, LayerId,
		AllottedGeometry.ToPaintGeometry(),
		FAppStyle::Get().GetBrush("Graph.Panel.SolidBackground"),
		ESlateDrawEffect::None,
		FLinearColor(0.08f, 0.08f, 0.10f));
	LayerId++;

	EnsureInitialFit(AllottedGeometry);

	UPaperSprite* Sprite = Model.IsValid() ? Model->GetCurrentSprite() : nullptr;
	UTexture2D* Texture = nullptr;
	if (Sprite)
	{
		Texture = Sprite->GetBakedTexture();
		if (!Texture)
		{
			Texture = Cast<UTexture2D>(Sprite->GetSourceTexture());
		}
	}

	if (!Sprite || !Texture)
	{
		return LayerId;
	}

	const FVector2D FrameSize = GetFrameSize();
	const FVector2D DrawSize = GetDrawSize(AllottedGeometry);
	const FVector2D DrawPos = GetDrawPos(AllottedGeometry);

	// Checkerboard behind the frame for transparency visualization (cell scales with zoom so it reads
	// as a fixed pixel grid against the art).
	FEditorCanvasUtils::DrawCheckerboard(
		OutDrawElements, LayerId, AllottedGeometry,
		DrawPos, DrawSize, FMath::Max(2.0f, 8.0f * ZoomLevel));
	LayerId++;

	// Onion skin: ghost the previous/next key frames UNDER the current one (seen through its transparency).
	if (Model.IsValid() && Model->IsOnionSkinEnabled() && Model->GetNumFrames() >= 2)
	{
		const int32 Num = Model->GetNumFrames();
		const int32 Cur = Model->GetCurrentFrame();
		auto DrawGhost = [&](int32 FrameIdx, FLinearColor Tint)
		{
			UPaperSprite* GS = Model->GetSpriteAt(FrameIdx);
			if (!GS) { return; }
			UTexture2D* GT = GS->GetBakedTexture();
			if (!GT) { GT = Cast<UTexture2D>(GS->GetSourceTexture()); }
			if (!GT) { return; }
			FSlateBrush GB;
			GB.SetResourceObject(GT);
			const FVector2D ISz(GT->GetSizeX(), GT->GetSizeY());
			GB.ImageSize = ISz;
			if (ISz.X > 0.0f && ISz.Y > 0.0f)
			{
				const FVector2D UV = FVector2D(GS->GetSourceUV());
				const FVector2D SZ = FVector2D(GS->GetSourceSize());
				GB.SetUVRegion(FBox2D(UV / ISz, (UV + SZ) / ISz));
			}
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId,
				MakePaintGeometry(AllottedGeometry, DrawSize, FSlateLayoutTransform(DrawPos)),
				&GB, ESlateDrawEffect::None, Tint);
		};
		DrawGhost((Cur - 1 + Num) % Num, FLinearColor(0.45f, 0.55f, 1.0f, 0.35f)); // prev — blue
		DrawGhost((Cur + 1) % Num,       FLinearColor(1.0f, 0.55f, 0.45f, 0.35f)); // next — orange
		LayerId++;
	}

	// Prefer the transient WORKING texture (the live, editable mirror of this frame). It is the full
	// frame, so it draws with the default 0..1 UV. Fall back to the sprite's source sub-region for
	// non-drawable frames (e.g. non-BGRA8 source) so they still display read-only.
	FSlateBrush FrameBrush;
	if (bDrawable && WorkingTexture)
	{
		FrameBrush.SetResourceObject(WorkingTexture);
		FrameBrush.ImageSize = FVector2D(WorkW, WorkH);
	}
	else
	{
		FrameBrush.SetResourceObject(Texture);
		const FVector2D TexImageSize(Texture->GetSizeX(), Texture->GetSizeY());
		FrameBrush.ImageSize = TexImageSize;
		if (TexImageSize.X > 0.0f && TexImageSize.Y > 0.0f)
		{
			const FVector2D UV = FVector2D(Sprite->GetSourceUV());
			FrameBrush.SetUVRegion(FBox2D(UV / TexImageSize, (UV + FrameSize) / TexImageSize));
		}
	}

	FSlateDrawElement::MakeBox(
		OutDrawElements, LayerId,
		MakePaintGeometry(AllottedGeometry, DrawSize, FSlateLayoutTransform(DrawPos)),
		&FrameBrush, ESlateDrawEffect::None, FLinearColor::White);
	LayerId++;

	// Pixel-grid overlay at high zoom (1px MakeBox lines at integer pixel boundaries).
	if (Model.IsValid() && Model->IsPixelGridEnabled() && ZoomLevel >= 6.0f)
	{
		const FSlateBrush* GridBrush = FAppStyle::Get().GetBrush("WhiteBrush");
		const FLinearColor GridColor(0.0f, 0.0f, 0.0f, 0.25f);
		const int32 FW = FMath::RoundToInt(FrameSize.X);
		const int32 FH = FMath::RoundToInt(FrameSize.Y);
		for (int32 i = 0; i <= FW; ++i)
		{
			const float X = DrawPos.X + i * ZoomLevel;
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId,
				MakePaintGeometry(AllottedGeometry, FVector2D(1.0f, DrawSize.Y), FSlateLayoutTransform(FVector2D(X, DrawPos.Y))),
				GridBrush, ESlateDrawEffect::None, GridColor);
		}
		for (int32 j = 0; j <= FH; ++j)
		{
			const float Y = DrawPos.Y + j * ZoomLevel;
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId,
				MakePaintGeometry(AllottedGeometry, FVector2D(DrawSize.X, 1.0f), FSlateLayoutTransform(FVector2D(DrawPos.X, Y))),
				GridBrush, ESlateDrawEffect::None, GridColor);
		}
		LayerId++;
	}

	// 1px frame-bounds outline (4 edges), so the editable region is obvious against the checkerboard.
	{
		const FSlateBrush* WhiteBrush = FAppStyle::Get().GetBrush("WhiteBrush");
		const FLinearColor OutlineColor(0.9f, 0.9f, 0.2f, 0.6f);
		const float T = 1.0f;
		const FVector2D P = DrawPos;
		const FVector2D S = DrawSize;
		auto Edge = [&](FVector2D Pos, FVector2D Sz)
		{
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId,
				MakePaintGeometry(AllottedGeometry, Sz, FSlateLayoutTransform(Pos)),
				WhiteBrush, ESlateDrawEffect::None, OutlineColor);
		};
		Edge(FVector2D(P.X, P.Y), FVector2D(S.X, T));                 // top
		Edge(FVector2D(P.X, P.Y + S.Y - T), FVector2D(S.X, T));       // bottom
		Edge(FVector2D(P.X, P.Y), FVector2D(T, S.Y));                 // left
		Edge(FVector2D(P.X + S.X - T, P.Y), FVector2D(T, S.Y));       // right
		LayerId++;
	}

	// Fail-closed UX: a frame whose source isn't BGRA8 can't be edited — say so instead of silently
	// no-op'ing. (Suppressed during playback, when the working texture is intentionally not built.)
	if (!bDrawable && !(Model.IsValid() && Model->IsPlaying()))
	{
		FSlateDrawElement::MakeText(
			OutDrawElements, LayerId,
			MakePaintGeometry(AllottedGeometry, FVector2D(AllottedGeometry.GetLocalSize().X - 12.0f, 18.0f), FSlateLayoutTransform(FVector2D(6.0f, 6.0f))),
			FText::FromString(TEXT("This frame's source texture isn't BGRA8 — read-only (not editable).")),
			FCoreStyle::GetDefaultFontStyle("Regular", 9),
			ESlateDrawEffect::None,
			FLinearColor(1.0f, 0.8f, 0.3f));
		LayerId++;
	}

	return LayerId;
}
