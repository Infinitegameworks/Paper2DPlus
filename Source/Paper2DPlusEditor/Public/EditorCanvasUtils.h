// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Rendering/DrawElements.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SLeafWidget.h"
#include "PaperSprite.h"
#include "PaperFlipbook.h"

/**
 * Shared utilities for editor canvas rendering.
 * Provides consistent visual elements across all canvas tabs.
 */
struct FEditorCanvasUtils
{
	/**
	 * Draw a checkerboard background pattern.
	 * Used by Hitbox, Alignment, and Frame Timing canvases for visual consistency.
	 */
	static void DrawCheckerboard(
		FSlateWindowElementList& OutDrawElements,
		int32 LayerId,
		const FGeometry& Geom,
		float CheckSize = 16.0f,
		FLinearColor DarkColor = FLinearColor(0.1f, 0.1f, 0.1f, 1.0f),
		FLinearColor LightColor = FLinearColor(0.15f, 0.15f, 0.15f, 1.0f))
	{
		const FVector2D LocalSize = Geom.GetLocalSize();

		// Draw dark background
		FSlateDrawElement::MakeBox(
			OutDrawElements,
			LayerId,
			Geom.ToPaintGeometry(FVector2f(LocalSize), FSlateLayoutTransform()),
			FAppStyle::GetBrush("WhiteBrush"),
			ESlateDrawEffect::None,
			DarkColor
		);

		// Draw checkerboard pattern
		for (float Y = 0; Y < LocalSize.Y; Y += CheckSize)
		{
			for (float X = 0; X < LocalSize.X; X += CheckSize)
			{
				int32 CheckX = FMath::FloorToInt(X / CheckSize);
				int32 CheckY = FMath::FloorToInt(Y / CheckSize);
				bool bDark = ((CheckX + CheckY) % 2) == 0;

				if (bDark)
				{
					FSlateDrawElement::MakeBox(
						OutDrawElements,
						LayerId,
						Geom.ToPaintGeometry(FVector2f(CheckSize), FSlateLayoutTransform(FVector2f(X, Y))),
						FAppStyle::GetBrush("WhiteBrush"),
						ESlateDrawEffect::None,
						LightColor
					);
				}
			}
		}
	}

	/**
	 * Draw a checkerboard in a sub-region of the widget (e.g., behind a zoomed/panned texture).
	 * Cells are clipped to the DrawSize boundary.
	 */
	static void DrawCheckerboard(
		FSlateWindowElementList& OutDrawElements,
		int32 LayerId,
		const FGeometry& Geom,
		FVector2D DrawOffset,
		FVector2D DrawSize,
		float CheckSize = 16.0f,
		FLinearColor DarkColor = FLinearColor(0.2f, 0.2f, 0.2f, 1.0f),
		FLinearColor LightColor = FLinearColor(0.4f, 0.4f, 0.4f, 1.0f))
	{
		if (CheckSize <= 0.0f) return;

		int32 NumChecksX = FMath::CeilToInt(DrawSize.X / CheckSize);
		int32 NumChecksY = FMath::CeilToInt(DrawSize.Y / CheckSize);

		for (int32 Y = 0; Y < NumChecksY; Y++)
		{
			for (int32 X = 0; X < NumChecksX; X++)
			{
				FLinearColor Color = ((X + Y) % 2 == 0) ? LightColor : DarkColor;
				FVector2D CellPos = DrawOffset + FVector2D(X * CheckSize, Y * CheckSize);
				FVector2D CellSize(
					FMath::Min(CheckSize, DrawSize.X - X * CheckSize),
					FMath::Min(CheckSize, DrawSize.Y - Y * CheckSize)
				);

				FSlateDrawElement::MakeBox(
					OutDrawElements,
					LayerId,
					Geom.ToPaintGeometry(CellSize, FSlateLayoutTransform(CellPos)),
					FAppStyle::GetBrush("WhiteBrush"),
					ESlateDrawEffect::None,
					Color
				);
			}
		}
	}
};

/**
 * Renders a UPaperSprite directly from its source texture with UV sub-region.
 * Bypasses FAssetThumbnailPool/SViewport pipeline for immediate rendering.
 * Use inside SBox with WidthOverride/HeightOverride to control display size.
 */
class SSpriteThumbnail : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SSpriteThumbnail) {}
		SLATE_ARGUMENT(UPaperSprite*, Sprite)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		SetClipping(EWidgetClipping::ClipToBounds);
		UPaperSprite* Sprite = InArgs._Sprite;
		if (Sprite)
		{
			UTexture2D* Tex = Sprite->GetBakedTexture();
			if (!Tex) Tex = Cast<UTexture2D>(Sprite->GetSourceTexture());
			if (Tex)
			{
				Brush.SetResourceObject(Tex);
				Brush.ImageSize = FVector2D(Tex->GetSizeX(), Tex->GetSizeY());
				Brush.DrawAs = ESlateBrushDrawType::Image;
				Brush.Tiling = ESlateBrushTileType::NoTile;

				FVector2D UV = Sprite->GetSourceUV();
				FVector2D Sz = Sprite->GetSourceSize();
				FVector2D TexSz(Tex->GetSizeX(), Tex->GetSizeY());
				if (TexSz.X > 0 && TexSz.Y > 0)
				{
					Brush.SetUVRegion(FBox2D(
						FVector2D(UV.X / TexSz.X, UV.Y / TexSz.Y),
						FVector2D((UV.X + Sz.X) / TexSz.X, (UV.Y + Sz.Y) / TexSz.Y)));
				}
				bHasTexture = true;
			}
		}
	}

	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(32, 32); }

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override
	{
		// Checkerboard behind sprite for transparency visualization
		FEditorCanvasUtils::DrawCheckerboard(OutDrawElements, LayerId, AllottedGeometry, 8.0f);

		if (bHasTexture)
		{
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 1,
				AllottedGeometry.ToPaintGeometry(),
				&Brush, ESlateDrawEffect::None, FLinearColor::White);
		}
		return LayerId + 1;
	}

private:
	FSlateBrush Brush;
	bool bHasTexture = false;
};

/**
 * Renders a UPaperFlipbook by showing its first frame statically,
 * then animating through all frames on mouse hover.
 * Uses direct texture rendering like SSpriteThumbnail.
 */
class SFlipbookThumbnail : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SFlipbookThumbnail) {}
		SLATE_ARGUMENT(UPaperFlipbook*, Flipbook)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		SetClipping(EWidgetClipping::ClipToBounds);
		StatusText = FText::FromString(TEXT("No FB"));
		if (InArgs._Flipbook)
		{
			Flipbook.Reset(InArgs._Flipbook);
			StatusText = FText::FromString(TEXT("No Frames"));
		}
		if (Flipbook.IsValid() && Flipbook->GetNumKeyFrames() > 0)
		{
			NumFrames = Flipbook->GetNumKeyFrames();
			InitialFrameIndex = FindFirstRenderableFrameIndex();
			CurrentFrame = (InitialFrameIndex != INDEX_NONE) ? InitialFrameIndex : 0;
			StatusText = FText::FromString(TEXT("Loading"));
			bHasTexture = SetBrushFromFrame(CurrentFrame);

			// Some flipbooks do not have a renderable first frame; fall back to any valid frame.
			if (!bHasTexture)
			{
				bHasTexture = SetBrushFromBestAvailableFrame();
			}

			// Some sprite textures resolve a frame or two after widget construction.
			// Keep probing briefly so cards are populated without requiring hover.
			InitialResolveAttempts = 0;
			InitialResolveTimerHandle = RegisterActiveTimer(0.05f, FWidgetActiveTimerDelegate::CreateSP(
				this, &SFlipbookThumbnail::OnInitialResolveTick));
		}
	}

	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(64, 64); }

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override
	{
		FEditorCanvasUtils::DrawCheckerboard(OutDrawElements, LayerId, AllottedGeometry, 8.0f);

		const bool bRenderable = bHasTexture && HasRenderableResource();
		if (bRenderable)
		{
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 1,
				AllottedGeometry.ToPaintGeometry(),
				&Brush, ESlateDrawEffect::None, FLinearColor::White);
		}
		else if (!StatusText.IsEmpty())
		{
			FSlateDrawElement::MakeText(
				OutDrawElements,
				LayerId + 2,
				AllottedGeometry.ToPaintGeometry(FVector2D(60.0f, 14.0f), FSlateLayoutTransform(FVector2D(2.0f, 24.0f))),
				StatusText,
				FCoreStyle::GetDefaultFontStyle("Regular", 8),
				ESlateDrawEffect::None,
				FLinearColor(0.78f, 0.78f, 0.78f, 0.95f));
		}
		return LayerId + 1;
	}

	virtual void OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		SLeafWidget::OnMouseEnter(MyGeometry, MouseEvent);
		if (NumFrames > 1)
		{
			TickAccumulator = 0.0;
			FrameRunAccumulator = 0;
			CurrentFrame = (InitialFrameIndex != INDEX_NONE) ? InitialFrameIndex : 0;
			if (!SetBrushFromFrame(CurrentFrame))
			{
				SetBrushFromBestAvailableFrame();
			}
			Invalidate(EInvalidateWidgetReason::LayoutAndVolatility);

			if (AnimTimerHandle.IsValid())
			{
				UnRegisterActiveTimer(AnimTimerHandle.Pin().ToSharedRef());
			}
			AnimTimerHandle = RegisterActiveTimer(0.016f, FWidgetActiveTimerDelegate::CreateSP(
				this, &SFlipbookThumbnail::OnAnimTick));
		}
	}

	virtual void OnMouseLeave(const FPointerEvent& MouseEvent) override
	{
		SLeafWidget::OnMouseLeave(MouseEvent);
		if (AnimTimerHandle.IsValid())
		{
			UnRegisterActiveTimer(AnimTimerHandle.Pin().ToSharedRef());
		}
		AnimTimerHandle.Reset();
		CurrentFrame = (InitialFrameIndex != INDEX_NONE) ? InitialFrameIndex : 0;
		TickAccumulator = 0.0;
		FrameRunAccumulator = 0;
		if (!SetBrushFromFrame(CurrentFrame))
		{
			SetBrushFromBestAvailableFrame();
		}
		Invalidate(EInvalidateWidgetReason::Paint);
	}

private:
	int32 FindFirstRenderableFrameIndex() const
	{
		if (!Flipbook.IsValid() || NumFrames <= 0)
		{
			return INDEX_NONE;
		}

		for (int32 FrameIndex = 0; FrameIndex < NumFrames; ++FrameIndex)
		{
			const FPaperFlipbookKeyFrame& KeyFrame = Flipbook->GetKeyFrameChecked(FrameIndex);
			if (UPaperSprite* Sprite = KeyFrame.Sprite)
			{
				if (UTexture2D* Tex = Sprite->GetBakedTexture())
				{
					return FrameIndex;
				}

				if (Cast<UTexture2D>(Sprite->GetSourceTexture()))
				{
					return FrameIndex;
				}
			}
		}

		return INDEX_NONE;
	}

	bool SetBrushFromFrame(int32 FrameIndex)
	{
		bHasTexture = false;
		if (!Flipbook.IsValid() || FrameIndex < 0 || FrameIndex >= NumFrames)
		{
			StatusText = FText::FromString(TEXT("No FB"));
			return false;
		}

		UPaperSprite* Sprite = Flipbook->GetKeyFrameChecked(FrameIndex).Sprite;
		if (!Sprite)
		{
			StatusText = FText::FromString(TEXT("No Sprite"));
			return false;
		}

		UTexture2D* Tex = Sprite->GetBakedTexture();
		if (!Tex) Tex = Cast<UTexture2D>(Sprite->GetSourceTexture());
		if (!Tex)
		{
			StatusText = FText::FromString(TEXT("No Sprite"));
			return false;
		}

		Brush.SetResourceObject(Tex);
		Brush.ImageSize = FVector2D(Tex->GetSizeX(), Tex->GetSizeY());
		Brush.DrawAs = ESlateBrushDrawType::Image;
		Brush.Tiling = ESlateBrushTileType::NoTile;

		FVector2D UV = Sprite->GetSourceUV();
		FVector2D Sz = Sprite->GetSourceSize();
		FVector2D TexSz(Tex->GetSizeX(), Tex->GetSizeY());
		if (TexSz.X > 0 && TexSz.Y > 0)
		{
			Brush.SetUVRegion(FBox2D(
				FVector2D(UV.X / TexSz.X, UV.Y / TexSz.Y),
				FVector2D((UV.X + Sz.X) / TexSz.X, (UV.Y + Sz.Y) / TexSz.Y)));
		}
		bHasTexture = true;
		StatusText = FText::GetEmpty();
		return true;
	}

	bool SetBrushFromBestAvailableFrame()
	{
		if (!Flipbook.IsValid() || NumFrames <= 0)
		{
			return false;
		}

		for (int32 FrameIndex = 0; FrameIndex < NumFrames; ++FrameIndex)
		{
			if (SetBrushFromFrame(FrameIndex))
			{
				InitialFrameIndex = FrameIndex;
				CurrentFrame = FrameIndex;
				return true;
			}
		}

		StatusText = FText::FromString(TEXT("No Sprite"));
		return false;
	}

	bool HasRenderableResource() const
	{
		const UTexture2D* Texture = Cast<UTexture2D>(Brush.GetResourceObject());
		return Texture && Texture->GetResource() != nullptr;
	}

	EActiveTimerReturnType OnInitialResolveTick(double CurrentTime, float DeltaTime)
	{
		if (!Flipbook.IsValid())
		{
			InitialResolveTimerHandle.Reset();
			return EActiveTimerReturnType::Stop;
		}

		++InitialResolveAttempts;
		bool bResolvedBrush = SetBrushFromFrame((InitialFrameIndex != INDEX_NONE) ? InitialFrameIndex : 0);
		if (!bResolvedBrush)
		{
			bResolvedBrush = SetBrushFromBestAvailableFrame();
		}
		bHasTexture = bResolvedBrush;

		// Force a prepass/paint so the card updates even before any hover event.
		Invalidate(EInvalidateWidgetReason::Paint);

		const bool bResourceReady = bHasTexture && HasRenderableResource();
		if (bResourceReady && InitialResolveAttempts >= 2)
		{
			StatusText = FText::GetEmpty();
			InitialResolveTimerHandle.Reset();
			return EActiveTimerReturnType::Stop;
		}

		StatusText = bHasTexture ? FText::FromString(TEXT("Loading")) : FText::FromString(TEXT("No Sprite"));

		if (InitialResolveAttempts >= 10)
		{
			InitialResolveTimerHandle.Reset();
			return EActiveTimerReturnType::Stop;
		}

		return EActiveTimerReturnType::Continue;
	}

	EActiveTimerReturnType OnAnimTick(double CurrentTime, float DeltaTime)
	{
		if (!Flipbook.IsValid() || NumFrames <= 1) return EActiveTimerReturnType::Stop;

		float FPS = Flipbook->GetFramesPerSecond();
		if (FPS <= 0.0f) FPS = 15.0f;

		TickAccumulator += DeltaTime;
		float SecondsPerTick = 1.0f / FPS;

		while (TickAccumulator >= SecondsPerTick)
		{
			TickAccumulator -= SecondsPerTick;
			FrameRunAccumulator++;

			int32 FrameRun = FMath::Max(Flipbook->GetKeyFrameChecked(CurrentFrame).FrameRun, 1);
			if (FrameRunAccumulator >= FrameRun)
			{
				FrameRunAccumulator = 0;
				CurrentFrame = (CurrentFrame + 1) % NumFrames;
				if (!SetBrushFromFrame(CurrentFrame))
				{
					// Keep thumbnail visible even if a specific frame has no texture.
					SetBrushFromBestAvailableFrame();
				}
				Invalidate(EInvalidateWidgetReason::Paint);
			}
		}

		return EActiveTimerReturnType::Continue;
	}

	TStrongObjectPtr<UPaperFlipbook> Flipbook;
	FSlateBrush Brush;
	bool bHasTexture = false;
	int32 NumFrames = 0;
	int32 InitialFrameIndex = INDEX_NONE;
	int32 InitialResolveAttempts = 0;
	int32 CurrentFrame = 0;
	double TickAccumulator = 0.0;
	int32 FrameRunAccumulator = 0;
	TWeakPtr<FActiveTimerHandle> AnimTimerHandle;
	TWeakPtr<FActiveTimerHandle> InitialResolveTimerHandle;
	FText StatusText;
};
