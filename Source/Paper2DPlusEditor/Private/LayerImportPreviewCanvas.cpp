// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "LayerImportPreviewCanvas.h"

#include "AsepriteImporter.h"
#include "EditorCanvasUtils.h"
#include "Engine/Texture2D.h"
#include "TextureResource.h"

void SLayerImportPreviewCanvas::Construct(const FArguments& InArgs)
{
	ParsedData = InArgs._ParsedData;
	PerLayerBuffers = InArgs._PerLayerBuffers;
	SetClipping(EWidgetClipping::ClipToBounds);

	// Initialize all visual layers as visible
	if (PerLayerBuffers)
	{
		for (const auto& Pair : *PerLayerBuffers)
		{
			LayerVisibility.Add(Pair.Key, true);
		}
	}

	// Create persistent preview texture
	if (ParsedData && ParsedData->bIsValid && ParsedData->Width > 0 && ParsedData->Height > 0)
	{
		UTexture2D* Tex = UTexture2D::CreateTransient(ParsedData->Width, ParsedData->Height, PF_B8G8R8A8);
		if (Tex)
		{
			Tex->SetFlags(RF_Transient);
			Tex->MipGenSettings = TMGS_NoMipmaps;
			Tex->Filter = TF_Nearest;
			Tex->NeverStream = true;
			Tex->SRGB = true;
			PreviewTexture.Reset(Tex);

			PreviewBrush.SetResourceObject(Tex);
			PreviewBrush.ImageSize = FVector2D(ParsedData->Width, ParsedData->Height);
			PreviewBrush.DrawAs = ESlateBrushDrawType::Image;
			PreviewBrush.Tiling = ESlateBrushTileType::NoTile;
		}
	}

	bNeedsRecomposite = true;
}

SLayerImportPreviewCanvas::~SLayerImportPreviewCanvas()
{
	PreviewTexture.Reset();
}

FVector2D SLayerImportPreviewCanvas::ComputeDesiredSize(float) const
{
	return FVector2D(256, 256);
}

int32 SLayerImportPreviewCanvas::GetFrameCount() const
{
	if (!ParsedData) return 0;
	return ParsedData->Frames.Num();
}

void SLayerImportPreviewCanvas::SetFrameIndex(int32 NewIndex)
{
	if (NewIndex != FrameIndex)
	{
		FrameIndex = NewIndex;
		bNeedsRecomposite = true;
		Invalidate(EInvalidateWidgetReason::Paint);
	}
}

void SLayerImportPreviewCanvas::SetLayerVisibility(int32 LayerIndex, bool bVisible)
{
	if (bool* Existing = LayerVisibility.Find(LayerIndex))
	{
		if (*Existing != bVisible)
		{
			*Existing = bVisible;
			bNeedsRecomposite = true;
			Invalidate(EInvalidateWidgetReason::Paint);
		}
	}
}

bool SLayerImportPreviewCanvas::IsLayerVisible(int32 LayerIndex) const
{
	if (const bool* Existing = LayerVisibility.Find(LayerIndex))
	{
		return *Existing;
	}
	return false;
}

void SLayerImportPreviewCanvas::BuildCompositeForFrame(int32 InFrameIndex, TArray<FColor>& OutComposite) const
{
	OutComposite.Reset();
	if (!ParsedData)
	{
		return;
	}

	const int32 W = ParsedData->Width;
	const int32 H = ParsedData->Height;
	const int32 PixelCount = W * H;
	if (PixelCount <= 0)
	{
		return;
	}
	const int32 ClampedFrame = FMath::Clamp(InFrameIndex, 0, FMath::Max(0, ParsedData->Frames.Num() - 1));

	// Alias so the per-layer blend below reads exactly as it did before this was split out.
	TArray<FColor>& Composite = OutComposite;
	Composite.SetNumZeroed(PixelCount);

	if (!PerLayerBuffers)
	{
		// FLAT SOURCE (see the class comment): no per-layer buffers were supplied, so there is
		// nothing to toggle and the parse's already-composited frame IS the picture. It honours the
		// file's own layer visibility and omits hitbox/socket layers, which is exactly what a
		// look-only preview should show.
		if (ParsedData->Frames.IsValidIndex(ClampedFrame)
			&& ParsedData->Frames[ClampedFrame].Pixels.Num() == PixelCount)
		{
			Composite = ParsedData->Frames[ClampedFrame].Pixels;
		}
		return;
	}

	// Build the visible Layer list in global back-to-front order.
	TArray<TPair<int32, int32>> SortedLayers; // (LayerIndex, global order)
	for (const auto& Pair : LayerVisibility)
	{
		if (!Pair.Value) continue; // not visible
		if (!PerLayerBuffers->Contains(Pair.Key)) continue;

		// Use Aseprite's layer order (index = bottom-to-top).
		SortedLayers.Add(TPair<int32, int32>(Pair.Key, Pair.Key));
	}
	SortedLayers.Sort([](const TPair<int32, int32>& A, const TPair<int32, int32>& B)
	{
		return A.Value < B.Value;
	});

	// Composite visible layers into a single buffer
	for (const auto& Entry : SortedLayers)
	{
		const int32 LayerIdx = Entry.Key;
		const TArray<TArray<FColor>>& LayerFrames = (*PerLayerBuffers)[LayerIdx];
		if (!LayerFrames.IsValidIndex(ClampedFrame)) continue;

		const TArray<FColor>& SrcBuffer = LayerFrames[ClampedFrame];
		if (SrcBuffer.Num() != PixelCount) continue;

		// Alpha composite: src over dst
		for (int32 i = 0; i < PixelCount; i++)
		{
			const FColor& Src = SrcBuffer[i];
			if (Src.A == 0) continue;

			if (Src.A == 255 || Composite[i].A == 0)
			{
				Composite[i] = Src;
			}
			else
			{
				float SrcA = Src.A / 255.0f;
				float DstA = Composite[i].A / 255.0f;
				float OutA = SrcA + DstA * (1.0f - SrcA);
				if (OutA > 0.0f)
				{
					Composite[i].R = (uint8)FMath::Clamp((Src.R * SrcA + Composite[i].R * DstA * (1.0f - SrcA)) / OutA, 0.0f, 255.0f);
					Composite[i].G = (uint8)FMath::Clamp((Src.G * SrcA + Composite[i].G * DstA * (1.0f - SrcA)) / OutA, 0.0f, 255.0f);
					Composite[i].B = (uint8)FMath::Clamp((Src.B * SrcA + Composite[i].B * DstA * (1.0f - SrcA)) / OutA, 0.0f, 255.0f);
					Composite[i].A = (uint8)FMath::Clamp(OutA * 255.0f, 0.0f, 255.0f);
				}
			}
		}
	}

}

void SLayerImportPreviewCanvas::RecompositeFrame() const
{
	bNeedsRecomposite = false;
	if (!PreviewTexture.IsValid() || !ParsedData)
	{
		return;
	}

	TArray<FColor> Composite;
	BuildCompositeForFrame(FrameIndex, Composite);
	WriteCompositeToTexture(Composite);
}

#if WITH_DEV_AUTOMATION_TESTS
bool SLayerImportPreviewCanvas::GetCompositedFrameForTests(TArray<FColor>& OutPixels) const
{
	// The SAME call the paint path uploads, so a test that reads this is reading what the widget
	// draws — not a parallel reimplementation of it.
	BuildCompositeForFrame(FrameIndex, OutPixels);
	return OutPixels.Num() > 0;
}
#endif

void SLayerImportPreviewCanvas::WriteCompositeToTexture(const TArray<FColor>& Composite) const
{
	UTexture2D* Tex = PreviewTexture.Get();
	if (!Tex || Composite.Num() <= 0)
	{
		return;
	}

	// UE pixel format is BGRA — FColor is already BGRA-ordered
	void* MipData = Tex->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
	if (MipData)
	{
		FMemory::Memcpy(MipData, Composite.GetData(), Composite.Num() * sizeof(FColor));
		Tex->GetPlatformData()->Mips[0].BulkData.Unlock();
		Tex->UpdateResource();
	}
}

int32 SLayerImportPreviewCanvas::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
	int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	// Background
	FEditorCanvasUtils::DrawCheckerboard(OutDrawElements, LayerId, AllottedGeometry);
	LayerId++;

	if (!PreviewTexture.IsValid() || !ParsedData)
	{
		return LayerId;
	}

	// Recomposite if dirty
	if (bNeedsRecomposite)
	{
		RecompositeFrame();
	}

	// Scale sprite to fit canvas (80% fit factor, centered)
	const FVector2D CanvasSize = AllottedGeometry.GetLocalSize();
	const FVector2D SpriteSize(ParsedData->Width, ParsedData->Height);

	if (SpriteSize.X <= 0 || SpriteSize.Y <= 0) return LayerId;

	float Scale = FMath::Min(CanvasSize.X / SpriteSize.X, CanvasSize.Y / SpriteSize.Y) * 0.8f;
	FVector2D DrawSize = SpriteSize * Scale;
	FVector2D DrawPos = (CanvasSize - DrawSize) * 0.5f;

	FSlateDrawElement::MakeBox(
		OutDrawElements,
		LayerId,
		MakePaintGeometry(AllottedGeometry, DrawSize, FSlateLayoutTransform(DrawPos)),
		&PreviewBrush,
		ESlateDrawEffect::None,
		FLinearColor::White);

	return LayerId + 1;
}
