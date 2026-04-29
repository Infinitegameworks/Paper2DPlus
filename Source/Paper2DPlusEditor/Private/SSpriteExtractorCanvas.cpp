// Copyright 2026 Infinite Gameworks. All Rights Reserved.

/** SSpriteExtractorCanvas — Canvas widget for sprite detection: texture rendering, island/grid overlay,
 *  sprite selection, edit mode (resize handles), merge selection, draw-new-box, zoom/pan. */

#include "SpriteExtractorWindow.h"
#include "EditorCanvasUtils.h"
#include "Engine/Texture2D.h"
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

#define LOCTEXT_NAMESPACE "SpriteExtractorWindow"

// ============================================
// SSpriteExtractorCanvas Implementation
// ============================================

void SSpriteExtractorCanvas::Construct(const FArguments& InArgs)
{
	CurrentTexture = InArgs._Texture;
	bShowGridOverlayAttr = InArgs._bShowGridOverlay;
	GridDimsAttr = InArgs._GridDims;
	GridStateAttr = InArgs._GridState;
}

void SSpriteExtractorCanvas::AddReferencedObjects(FReferenceCollector& Collector)
{
	if (CurrentTexture)
	{
		Collector.AddReferencedObject(CurrentTexture);
	}
}

FVector2D SSpriteExtractorCanvas::ComputeDesiredSize(float) const
{
	return FVector2D(800, 600);
}

int32 SSpriteExtractorCanvas::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	// Draw background
	FSlateDrawElement::MakeBox(
		OutDrawElements,
		LayerId,
		AllottedGeometry.ToPaintGeometry(),
		FAppStyle::GetBrush("Graph.Panel.SolidBackground"),
		ESlateDrawEffect::None,
		FLinearColor(0.1f, 0.1f, 0.12f)
	);
	LayerId++;

	// Draw texture if available. IsValid() rejects pending-kill / garbage-collected objects —
	// the TStrongObjectPtr root should prevent this, but belt-and-suspenders the access.
	if (IsValid(CurrentTexture))
	{
		FSlateBrush TextureBrush;
		TextureBrush.SetResourceObject(CurrentTexture);
		TextureBrush.ImageSize = FVector2D(CurrentTexture->GetSizeX(), CurrentTexture->GetSizeY());

		FVector2D TextureSize = TextureBrush.ImageSize * ZoomLevel;
		FVector2D DrawPos = PanOffset + (AllottedGeometry.GetLocalSize() - TextureSize) * 0.5f;

		// Draw checkered background for transparency (like Unreal's texture viewer)
		FEditorCanvasUtils::DrawCheckerboard(
			OutDrawElements, LayerId, AllottedGeometry,
			DrawPos, TextureSize, 16.0f * ZoomLevel);
		LayerId++;

		// Draw the texture on top
		FSlateDrawElement::MakeBox(
			OutDrawElements,
			LayerId,
			MakePaintGeometry(AllottedGeometry, TextureSize, FSlateLayoutTransform(DrawPos)),
			&TextureBrush,
			ESlateDrawEffect::None,
			FLinearColor::White
		);
		LayerId++;

		// Draw detected sprites with outline-based visuals (color-blind friendly)
		const FSlateBrush* WhiteBrush = FAppStyle::GetBrush("WhiteBrush");

		for (int32 i = 0; i < DetectedSprites.Num(); i++)
		{
			const FDetectedSprite& Sprite = DetectedSprites[i];
			const bool bHovered = (i == HoveredSpriteIndex);

			FVector2D TopLeft = TextureToScreen(AllottedGeometry, FVector2D(Sprite.Bounds.Min.X, Sprite.Bounds.Min.Y));
			FVector2D BottomRight = TextureToScreen(AllottedGeometry, FVector2D(Sprite.Bounds.Max.X, Sprite.Bounds.Max.Y));
			FVector2D Size = BottomRight - TopLeft;

			// Determine fill, outline color, and outline width by state
			FLinearColor FillColor;
			FLinearColor OutlineColor;
			float OutlineWidth;

			if (Sprite.bSelected && bHovered)
			{
				FillColor = FLinearColor(1.0f, 1.0f, 1.0f, 0.1f);
				OutlineColor = FLinearColor(0.5f, 0.8f, 1.0f, 1.0f);
				OutlineWidth = 3.0f;
			}
			else if (Sprite.bSelected)
			{
				FillColor = FLinearColor(1.0f, 1.0f, 1.0f, 0.08f);
				OutlineColor = FLinearColor(1.0f, 1.0f, 1.0f, 0.9f);
				OutlineWidth = 3.0f;
			}
			else if (bHovered)
			{
				FillColor = FLinearColor(1.0f, 1.0f, 1.0f, 0.05f);
				OutlineColor = FLinearColor(0.5f, 0.8f, 1.0f, 0.8f);
				OutlineWidth = 2.0f;
			}
			else
			{
				FillColor = FLinearColor(0.0f, 0.0f, 0.0f, 0.0f);
				OutlineColor = FLinearColor(0.5f, 0.5f, 0.5f, 0.5f);
				OutlineWidth = 1.0f;
			}

			// Fill (skip if fully transparent)
			if (FillColor.A > 0.0f)
			{
				FSlateDrawElement::MakeBox(
					OutDrawElements,
					LayerId,
					MakePaintGeometry(AllottedGeometry, Size, FSlateLayoutTransform(TopLeft)),
					WhiteBrush,
					ESlateDrawEffect::None,
					FillColor
				);
			}

			// Outline — 4 edges drawn as thin boxes (top, bottom, left, right)
			// Top edge
			FSlateDrawElement::MakeBox(
				OutDrawElements,
				LayerId + 1,
				MakePaintGeometry(AllottedGeometry, FVector2D(Size.X, OutlineWidth), FSlateLayoutTransform(TopLeft)),
				WhiteBrush,
				ESlateDrawEffect::None,
				OutlineColor
			);
			// Bottom edge
			FSlateDrawElement::MakeBox(
				OutDrawElements,
				LayerId + 1,
				MakePaintGeometry(AllottedGeometry, FVector2D(Size.X, OutlineWidth), FSlateLayoutTransform(FVector2D(TopLeft.X, BottomRight.Y - OutlineWidth))),
				WhiteBrush,
				ESlateDrawEffect::None,
				OutlineColor
			);
			// Left edge
			FSlateDrawElement::MakeBox(
				OutDrawElements,
				LayerId + 1,
				MakePaintGeometry(AllottedGeometry, FVector2D(OutlineWidth, Size.Y), FSlateLayoutTransform(TopLeft)),
				WhiteBrush,
				ESlateDrawEffect::None,
				OutlineColor
			);
			// Right edge
			FSlateDrawElement::MakeBox(
				OutDrawElements,
				LayerId + 1,
				MakePaintGeometry(AllottedGeometry, FVector2D(OutlineWidth, Size.Y), FSlateLayoutTransform(FVector2D(BottomRight.X - OutlineWidth, TopLeft.Y))),
				WhiteBrush,
				ESlateDrawEffect::None,
				OutlineColor
			);

			// Index label with dimensions
			FString IndexText = FString::Printf(TEXT("%d"), Sprite.Index);
			FString DimText = FString::Printf(TEXT("%dx%d"), Sprite.Bounds.Width(), Sprite.Bounds.Height());
			FSlateFontInfo FontInfo = FCoreStyle::GetDefaultFontStyle("Bold", 10);
			FSlateFontInfo SmallFontInfo = FCoreStyle::GetDefaultFontStyle("Regular", 9);

			// Draw index number
			FSlateDrawElement::MakeText(
				OutDrawElements,
				LayerId + 2,
				MakePaintGeometry(AllottedGeometry, FVector2D(50, 20), FSlateLayoutTransform(TopLeft + FVector2D(4, 2))),
				IndexText,
				FontInfo,
				ESlateDrawEffect::None,
				FLinearColor::White
			);

			// Draw dimension text below sprite box
			FSlateDrawElement::MakeText(
				OutDrawElements,
				LayerId + 2,
				MakePaintGeometry(AllottedGeometry, FVector2D(100, 16), FSlateLayoutTransform(FVector2D(TopLeft.X + 4, BottomRight.Y + 2))),
				DimText,
				SmallFontInfo,
				ESlateDrawEffect::None,
				FLinearColor(0.8f, 0.8f, 0.8f)
			);

				// Merge selection blue overlay
			if (MergeSelectedIndices.Contains(i))
			{
				FLinearColor MergeColor(0.2f, 0.4f, 1.0f, 0.3f);
				FLinearColor MergeOutline(0.3f, 0.5f, 1.0f, 0.9f);
				float MergeOutlineWidth = 2.0f;

				// Blue fill
				FSlateDrawElement::MakeBox(
					OutDrawElements,
					LayerId + 2,
					MakePaintGeometry(AllottedGeometry, Size, FSlateLayoutTransform(TopLeft)),
					WhiteBrush,
					ESlateDrawEffect::None,
					MergeColor
				);

				// Blue outline (4 edges)
				FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 2,
					MakePaintGeometry(AllottedGeometry, FVector2D(Size.X, MergeOutlineWidth), FSlateLayoutTransform(TopLeft)),
					WhiteBrush, ESlateDrawEffect::None, MergeOutline);
				FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 2,
					MakePaintGeometry(AllottedGeometry, FVector2D(Size.X, MergeOutlineWidth), FSlateLayoutTransform(FVector2D(TopLeft.X, BottomRight.Y - MergeOutlineWidth))),
					WhiteBrush, ESlateDrawEffect::None, MergeOutline);
				FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 2,
					MakePaintGeometry(AllottedGeometry, FVector2D(MergeOutlineWidth, Size.Y), FSlateLayoutTransform(TopLeft)),
					WhiteBrush, ESlateDrawEffect::None, MergeOutline);
				FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 2,
					MakePaintGeometry(AllottedGeometry, FVector2D(MergeOutlineWidth, Size.Y), FSlateLayoutTransform(FVector2D(BottomRight.X - MergeOutlineWidth, TopLeft.Y))),
					WhiteBrush, ESlateDrawEffect::None, MergeOutline);
			}
		}

		// Original bounds overlay: thin green inner outline when Bounds != OriginalBounds
		if (bShowOriginalBounds)
		{
			for (int32 i = 0; i < DetectedSprites.Num(); i++)
			{
				const FDetectedSprite& Sprite = DetectedSprites[i];
				if (Sprite.OriginalBounds == Sprite.Bounds) continue;

				FVector2D OTL = TextureToScreen(AllottedGeometry, FVector2D(Sprite.OriginalBounds.Min.X, Sprite.OriginalBounds.Min.Y));
				FVector2D OBR = TextureToScreen(AllottedGeometry, FVector2D(Sprite.OriginalBounds.Max.X, Sprite.OriginalBounds.Max.Y));
				FVector2D OSize = OBR - OTL;

				const FLinearColor OrigColor(0.2f, 0.9f, 0.3f, 0.7f);
				constexpr float OrigWidth = 1.5f;

				FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 2,
					MakePaintGeometry(AllottedGeometry, FVector2D(OSize.X, OrigWidth), FSlateLayoutTransform(OTL)),
					WhiteBrush, ESlateDrawEffect::None, OrigColor);
				FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 2,
					MakePaintGeometry(AllottedGeometry, FVector2D(OSize.X, OrigWidth), FSlateLayoutTransform(FVector2D(OTL.X, OBR.Y - OrigWidth))),
					WhiteBrush, ESlateDrawEffect::None, OrigColor);
				FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 2,
					MakePaintGeometry(AllottedGeometry, FVector2D(OrigWidth, OSize.Y), FSlateLayoutTransform(OTL)),
					WhiteBrush, ESlateDrawEffect::None, OrigColor);
				FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 2,
					MakePaintGeometry(AllottedGeometry, FVector2D(OrigWidth, OSize.Y), FSlateLayoutTransform(FVector2D(OBR.X - OrigWidth, OTL.Y))),
					WhiteBrush, ESlateDrawEffect::None, OrigColor);
			}
		}

		LayerId += 3;

		// Edit mode handles for the editing sprite
		if (EditingSpriteIndex >= 0 && DetectedSprites.IsValidIndex(EditingSpriteIndex))
		{
			const FDetectedSprite& EditSprite = DetectedSprites[EditingSpriteIndex];
			FVector2D ETL = TextureToScreen(AllottedGeometry, FVector2D(EditSprite.Bounds.Min.X, EditSprite.Bounds.Min.Y));
			FVector2D EBR = TextureToScreen(AllottedGeometry, FVector2D(EditSprite.Bounds.Max.X, EditSprite.Bounds.Max.Y));
			FVector2D EMid = (ETL + EBR) * 0.5f;
			const float HandleSize = 6.0f;
			FLinearColor HandleColor(1.0f, 0.8f, 0.0f, 1.0f);

			// Draw 8 handles (corners and midpoints)
			auto DrawHandle = [&](FVector2D Pos)
			{
				FSlateDrawElement::MakeBox(
					OutDrawElements,
					LayerId,
					MakePaintGeometry(AllottedGeometry, FVector2D(HandleSize, HandleSize),
						FSlateLayoutTransform(Pos - FVector2D(HandleSize * 0.5f, HandleSize * 0.5f))),
					WhiteBrush,
					ESlateDrawEffect::None,
					HandleColor
				);
			};

			DrawHandle(ETL);                                           // TopLeft
			DrawHandle(FVector2D(EMid.X, ETL.Y));                     // Top
			DrawHandle(FVector2D(EBR.X, ETL.Y));                      // TopRight
			DrawHandle(FVector2D(ETL.X, EMid.Y));                     // Left
			DrawHandle(FVector2D(EBR.X, EMid.Y));                     // Right
			DrawHandle(FVector2D(ETL.X, EBR.Y));                      // BottomLeft
			DrawHandle(FVector2D(EMid.X, EBR.Y));                     // Bottom
			DrawHandle(EBR);                                           // BottomRight
			LayerId++;
		}

		// Draw box preview (Ctrl+drag new box)
		if (bIsDrawingNewBox && DrawBoxPreview.Width() > 0 && DrawBoxPreview.Height() > 0)
		{
			FVector2D BoxTL = TextureToScreen(AllottedGeometry, FVector2D(DrawBoxPreview.Min.X, DrawBoxPreview.Min.Y));
			FVector2D BoxBR = TextureToScreen(AllottedGeometry, FVector2D(DrawBoxPreview.Max.X, DrawBoxPreview.Max.Y));
			FVector2D BoxSize = BoxBR - BoxTL;
			FLinearColor CyanFill(0.0f, 1.0f, 1.0f, 0.15f);
			FLinearColor CyanOutline(0.0f, 1.0f, 1.0f, 0.9f);
			float CyanWidth = 2.0f;

			// Fill
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId,
				MakePaintGeometry(AllottedGeometry, BoxSize, FSlateLayoutTransform(BoxTL)),
				WhiteBrush, ESlateDrawEffect::None, CyanFill);

			// Outline (4 edges)
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 1,
				MakePaintGeometry(AllottedGeometry, FVector2D(BoxSize.X, CyanWidth), FSlateLayoutTransform(BoxTL)),
				WhiteBrush, ESlateDrawEffect::None, CyanOutline);
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 1,
				MakePaintGeometry(AllottedGeometry, FVector2D(BoxSize.X, CyanWidth), FSlateLayoutTransform(FVector2D(BoxTL.X, BoxBR.Y - CyanWidth))),
				WhiteBrush, ESlateDrawEffect::None, CyanOutline);
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 1,
				MakePaintGeometry(AllottedGeometry, FVector2D(CyanWidth, BoxSize.Y), FSlateLayoutTransform(BoxTL)),
				WhiteBrush, ESlateDrawEffect::None, CyanOutline);
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 1,
				MakePaintGeometry(AllottedGeometry, FVector2D(CyanWidth, BoxSize.Y), FSlateLayoutTransform(FVector2D(BoxBR.X - CyanWidth, BoxTL.Y))),
				WhiteBrush, ESlateDrawEffect::None, CyanOutline);
			LayerId += 2;
		}
	}
	else
	{
		// Draw "No texture" message
		FSlateFontInfo FontInfo = FCoreStyle::GetDefaultFontStyle("Regular", 14);
		FSlateDrawElement::MakeText(
			OutDrawElements,
			LayerId,
			MakePaintGeometry(AllottedGeometry, FVector2D(200, 30), FSlateLayoutTransform(AllottedGeometry.GetLocalSize() * 0.5f - FVector2D(100, 15))),
			LOCTEXT("NoTexture", "No texture selected"),
			FontInfo,
			ESlateDrawEffect::None,
			FLinearColor(0.5f, 0.5f, 0.5f)
		);
		LayerId++;
	}

	// Grid overlay — bulk extractor's cell-grid preview. Drawn above detection outlines.
	if (CurrentTexture && bShowGridOverlayAttr.Get(false))
	{
		const FIntPoint GridDims = GridDimsAttr.Get(FIntPoint::ZeroValue);
		if (GridDims.X > 0 && GridDims.Y > 0)
		{
			FLinearColor GridColor;
			switch (GridStateAttr.Get(ESpriteCanvasGridState::Inferred))
			{
			case ESpriteCanvasGridState::Overridden: GridColor = FLinearColor(0.31f, 0.71f, 1.0f, 0.80f); break;
			case ESpriteCanvasGridState::Confirmed:  GridColor = FLinearColor(0.39f, 0.86f, 0.39f, 0.65f); break;
			default:                                  GridColor = FLinearColor(1.0f, 0.90f, 0.31f, 0.55f); break; // Inferred
			}
			const FSlateBrush* GridBrush = FAppStyle::GetBrush("WhiteBrush");
			const float CellWFloat = float(CurrentTexture->Source.GetSizeX()) / float(GridDims.X);
			const float CellHFloat = float(CurrentTexture->Source.GetSizeY()) / float(GridDims.Y);
			const float OutlineWidth = 1.0f;

			for (int32 Row = 0; Row < GridDims.Y; Row++)
			{
				for (int32 Col = 0; Col < GridDims.X; Col++)
				{
					const FVector2D TL = TextureToScreen(AllottedGeometry,
						FVector2D(Col * CellWFloat, Row * CellHFloat));
					const FVector2D BR = TextureToScreen(AllottedGeometry,
						FVector2D((Col + 1) * CellWFloat, (Row + 1) * CellHFloat));
					const FVector2D Size = BR - TL;

					// Top edge
					FSlateDrawElement::MakeBox(
						OutDrawElements, LayerId,
						MakePaintGeometry(AllottedGeometry, FVector2D(Size.X, OutlineWidth), FSlateLayoutTransform(TL)),
						GridBrush, ESlateDrawEffect::None, GridColor);
					// Bottom edge
					FSlateDrawElement::MakeBox(
						OutDrawElements, LayerId,
						MakePaintGeometry(AllottedGeometry, FVector2D(Size.X, OutlineWidth), FSlateLayoutTransform(FVector2D(TL.X, BR.Y - OutlineWidth))),
						GridBrush, ESlateDrawEffect::None, GridColor);
					// Left edge
					FSlateDrawElement::MakeBox(
						OutDrawElements, LayerId,
						MakePaintGeometry(AllottedGeometry, FVector2D(OutlineWidth, Size.Y), FSlateLayoutTransform(TL)),
						GridBrush, ESlateDrawEffect::None, GridColor);
					// Right edge
					FSlateDrawElement::MakeBox(
						OutDrawElements, LayerId,
						MakePaintGeometry(AllottedGeometry, FVector2D(OutlineWidth, Size.Y), FSlateLayoutTransform(FVector2D(BR.X - OutlineWidth, TL.Y))),
						GridBrush, ESlateDrawEffect::None, GridColor);
				}
			}
			LayerId++;
		}
	}

	return LayerId;
}

FReply SSpriteExtractorCanvas::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		FVector2D LocalPos = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());

		// Edit mode: check handle hit first
		if (IsInEditMode())
		{
			EHandleType Handle = HitTestHandle(MyGeometry, LocalPos);
			if (Handle != EHandleType::None)
			{
				DraggingHandle = Handle;
				DragStartTexturePos = ScreenToTexture(MyGeometry, LocalPos);
				PreDragBounds = DetectedSprites[EditingSpriteIndex].Bounds;
				return FReply::Handled().CaptureMouse(SharedThis(this));
			}

			// Click outside editing sprite — commit and exit
			int32 HitIndex = HitTestSprite(MyGeometry, MouseEvent.GetScreenSpacePosition());
			if (HitIndex != EditingSpriteIndex)
			{
				ExitEditMode(true);
			}
		}

		// Ctrl+drag: start drawing new box
		if (MouseEvent.IsControlDown() && !IsInEditMode())
		{
			FVector2D TexPos = ScreenToTexture(MyGeometry, LocalPos);
			bIsDrawingNewBox = true;
			DrawBoxStart = TexPos;
			DrawBoxPreview = FIntRect(FIntPoint((int32)TexPos.X, (int32)TexPos.Y), FIntPoint((int32)TexPos.X, (int32)TexPos.Y));
			return FReply::Handled().CaptureMouse(SharedThis(this));
		}

		// Shift+click: toggle merge selection
		if (MouseEvent.IsShiftDown())
		{
			int32 HitIndex = HitTestSprite(MyGeometry, MouseEvent.GetScreenSpacePosition());
			if (HitIndex >= 0)
			{
				ToggleMergeSelection(HitIndex);
				Invalidate(EInvalidateWidgetReason::Paint);
				return FReply::Handled();
			}
		}

		// Normal click: toggle extraction selection
		int32 HitIndex = HitTestSprite(MyGeometry, MouseEvent.GetScreenSpacePosition());
		if (HitIndex >= 0)
		{
			ToggleSpriteSelection(HitIndex);
			OnSpriteSelectionToggled.ExecuteIfBound(HitIndex);
			return FReply::Handled();
		}
	}
	else if (MouseEvent.GetEffectingButton() == EKeys::MiddleMouseButton || MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		bIsPanning = true;
		LastMousePos = MouseEvent.GetScreenSpacePosition();
		return FReply::Handled().CaptureMouse(SharedThis(this));
	}

	return FReply::Unhandled();
}

FReply SSpriteExtractorCanvas::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		// Edit mode: commit handle drag
		if (DraggingHandle != EHandleType::None)
		{
			DraggingHandle = EHandleType::None;
			if (DetectedSprites.IsValidIndex(EditingSpriteIndex))
			{
				OnSpriteEdited.ExecuteIfBound(EditingSpriteIndex, DetectedSprites[EditingSpriteIndex].Bounds);
			}
			return FReply::Handled().ReleaseMouseCapture();
		}

		// Draw new box: commit
		if (bIsDrawingNewBox)
		{
			bIsDrawingNewBox = false;
			if (DrawBoxPreview.Width() >= 1 && DrawBoxPreview.Height() >= 1)
			{
				OnNewBoxDrawn.ExecuteIfBound(DrawBoxPreview);
			}
			Invalidate(EInvalidateWidgetReason::Paint);
			return FReply::Handled().ReleaseMouseCapture();
		}
	}

	if (bIsPanning && (MouseEvent.GetEffectingButton() == EKeys::MiddleMouseButton || MouseEvent.GetEffectingButton() == EKeys::RightMouseButton))
	{
		bIsPanning = false;
		return FReply::Handled().ReleaseMouseCapture();
	}

	return FReply::Unhandled();
}

FReply SSpriteExtractorCanvas::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (bIsPanning)
	{
		FVector2D Delta = MouseEvent.GetScreenSpacePosition() - LastMousePos;
		PanOffset += Delta;
		LastMousePos = MouseEvent.GetScreenSpacePosition();
		return FReply::Handled();
	}

	FVector2D LocalPos = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());

	// Handle drag during edit mode
	if (DraggingHandle != EHandleType::None && HasMouseCapture() && DetectedSprites.IsValidIndex(EditingSpriteIndex))
	{
		FVector2D TexPos = ScreenToTexture(MyGeometry, LocalPos);
		FIntPoint Delta(
			FMath::RoundToInt(TexPos.X - DragStartTexturePos.X),
			FMath::RoundToInt(TexPos.Y - DragStartTexturePos.Y));

		FIntRect NewBounds = PreDragBounds;

		switch (DraggingHandle)
		{
		case EHandleType::TopLeft:     NewBounds.Min.X += Delta.X; NewBounds.Min.Y += Delta.Y; break;
		case EHandleType::Top:         NewBounds.Min.Y += Delta.Y; break;
		case EHandleType::TopRight:    NewBounds.Max.X += Delta.X; NewBounds.Min.Y += Delta.Y; break;
		case EHandleType::Left:        NewBounds.Min.X += Delta.X; break;
		case EHandleType::Right:       NewBounds.Max.X += Delta.X; break;
		case EHandleType::BottomLeft:  NewBounds.Min.X += Delta.X; NewBounds.Max.Y += Delta.Y; break;
		case EHandleType::Bottom:      NewBounds.Max.Y += Delta.Y; break;
		case EHandleType::BottomRight: NewBounds.Max.X += Delta.X; NewBounds.Max.Y += Delta.Y; break;
		default: break;
		}

		// Enforce minimum size
		if (NewBounds.Width() < 1) NewBounds.Max.X = NewBounds.Min.X + 1;
		if (NewBounds.Height() < 1) NewBounds.Max.Y = NewBounds.Min.Y + 1;

		// Clamp to texture bounds
		NewBounds = ClampToTextureBounds(NewBounds);

		DetectedSprites[EditingSpriteIndex].Bounds = NewBounds;

		Invalidate(EInvalidateWidgetReason::Paint);
		return FReply::Handled();
	}

	// Draw box drag
	if (bIsDrawingNewBox && HasMouseCapture())
	{
		FVector2D TexPos = ScreenToTexture(MyGeometry, LocalPos);
		DrawBoxPreview.Min.X = FMath::Min((int32)DrawBoxStart.X, (int32)TexPos.X);
		DrawBoxPreview.Min.Y = FMath::Min((int32)DrawBoxStart.Y, (int32)TexPos.Y);
		DrawBoxPreview.Max.X = FMath::Max((int32)DrawBoxStart.X, (int32)TexPos.X);
		DrawBoxPreview.Max.Y = FMath::Max((int32)DrawBoxStart.Y, (int32)TexPos.Y);
		DrawBoxPreview = ClampToTextureBounds(DrawBoxPreview);
		Invalidate(EInvalidateWidgetReason::Paint);
		return FReply::Handled();
	}

	// Track hovered sprite for highlight (skip during panning/dragging)
	if (!bIsPanning && DraggingHandle == EHandleType::None && !bIsDrawingNewBox)
	{
		int32 NewHoveredIndex = HitTestSprite(MyGeometry, MouseEvent.GetScreenSpacePosition());
		if (NewHoveredIndex != HoveredSpriteIndex)
		{
			HoveredSpriteIndex = NewHoveredIndex;
			Invalidate(EInvalidateWidgetReason::Paint);
		}
	}

	return FReply::Unhandled();
}

FReply SSpriteExtractorCanvas::OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	float Delta = MouseEvent.GetWheelDelta();
	float OldZoom = ZoomLevel;
	ZoomLevel = FMath::Clamp(ZoomLevel + Delta * 0.1f, 0.1f, 10.0f);

	// Zoom toward mouse position
	FVector2D MousePos = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
	FVector2D Center = MyGeometry.GetLocalSize() * 0.5f;
	FVector2D ToMouse = MousePos - Center - PanOffset;
	PanOffset += ToMouse * (1.0f - ZoomLevel / OldZoom);

	OnZoomChanged.ExecuteIfBound();

	return FReply::Handled();
}

FReply SSpriteExtractorCanvas::OnMouseButtonDoubleClick(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		int32 HitIndex = HitTestSprite(MyGeometry, MouseEvent.GetScreenSpacePosition());
		if (HitIndex >= 0)
		{
			// Force selected on double-click
			if (!DetectedSprites[HitIndex].bSelected)
			{
				DetectedSprites[HitIndex].bSelected = true;
				OnSpriteSelectionToggled.ExecuteIfBound(HitIndex);
			}
			EnterEditMode(HitIndex);
			return FReply::Handled();
		}
	}
	return FReply::Unhandled();
}

void SSpriteExtractorCanvas::OnMouseLeave(const FPointerEvent& MouseEvent)
{
	SLeafWidget::OnMouseLeave(MouseEvent);
	if (HoveredSpriteIndex != -1)
	{
		HoveredSpriteIndex = -1;
		Invalidate(EInvalidateWidgetReason::Paint);
	}
}

void SSpriteExtractorCanvas::OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent)
{
	bIsPanning = false;
	DraggingHandle = EHandleType::None;
	bIsDrawingNewBox = false;
}

FCursorReply SSpriteExtractorCanvas::OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const
{
	if (IsInEditMode())
	{
		FVector2D LocalPos = MyGeometry.AbsoluteToLocal(CursorEvent.GetScreenSpacePosition());
		EHandleType Handle = HitTestHandle(MyGeometry, LocalPos);
		switch (Handle)
		{
		case EHandleType::Top:
		case EHandleType::Bottom:
			return FCursorReply::Cursor(EMouseCursor::ResizeUpDown);
		case EHandleType::Left:
		case EHandleType::Right:
			return FCursorReply::Cursor(EMouseCursor::ResizeLeftRight);
		case EHandleType::TopLeft:
		case EHandleType::BottomRight:
			return FCursorReply::Cursor(EMouseCursor::ResizeSouthEast);
		case EHandleType::TopRight:
		case EHandleType::BottomLeft:
			return FCursorReply::Cursor(EMouseCursor::ResizeSouthWest);
		default: break;
		}
	}
	return SLeafWidget::OnCursorQuery(MyGeometry, CursorEvent);
}

void SSpriteExtractorCanvas::SetTexture(UTexture2D* NewTexture)
{
	CurrentTexture = NewTexture;
	// FGCObject::AddReferencedObjects will root CurrentTexture on the next GC pass, so
	// mid-paint collection (triggered by bulk-extract asset creation) can't reclaim it.
	DetectedSprites.Empty();
	MergeSelectedIndices.Empty();
	ExitEditMode(false);
	ResetView();
}

void SSpriteExtractorCanvas::SetDetectedSprites(const TArray<FDetectedSprite>& InSprites)
{
	DetectedSprites = InSprites;
}

void SSpriteExtractorCanvas::ToggleSpriteSelection(int32 Index)
{
	if (DetectedSprites.IsValidIndex(Index))
	{
		DetectedSprites[Index].bSelected = !DetectedSprites[Index].bSelected;
	}
}

void SSpriteExtractorCanvas::SelectAll(bool bSelect)
{
	for (FDetectedSprite& Sprite : DetectedSprites)
	{
		Sprite.bSelected = bSelect;
	}
}

void SSpriteExtractorCanvas::SetZoom(float NewZoom)
{
	ZoomLevel = FMath::Clamp(NewZoom, 0.1f, 10.0f);
}

void SSpriteExtractorCanvas::ResetView()
{
	ZoomLevel = 1.0f;
	PanOffset = FVector2D::ZeroVector;
}

int32 SSpriteExtractorCanvas::HitTestSprite(const FGeometry& Geom, const FVector2D& ScreenPos) const
{
	FVector2D LocalPos = Geom.AbsoluteToLocal(ScreenPos);
	FVector2D TexturePos = ScreenToTexture(Geom, LocalPos);

	for (int32 i = DetectedSprites.Num() - 1; i >= 0; i--)
	{
		const FDetectedSprite& Sprite = DetectedSprites[i];
		if (TexturePos.X >= Sprite.Bounds.Min.X && TexturePos.X < Sprite.Bounds.Max.X &&
			TexturePos.Y >= Sprite.Bounds.Min.Y && TexturePos.Y < Sprite.Bounds.Max.Y)
		{
			return i;
		}
	}

	return -1;
}

FVector2D SSpriteExtractorCanvas::ScreenToTexture(const FGeometry& Geom, const FVector2D& ScreenPos) const
{
	if (!CurrentTexture) return FVector2D::ZeroVector;

	FVector2D TextureSize(CurrentTexture->GetSizeX() * ZoomLevel, CurrentTexture->GetSizeY() * ZoomLevel);
	FVector2D DrawOffset = PanOffset + (Geom.GetLocalSize() - TextureSize) * 0.5f;
	return (ScreenPos - DrawOffset) / ZoomLevel;
}

FVector2D SSpriteExtractorCanvas::TextureToScreen(const FGeometry& Geom, const FVector2D& TexturePos) const
{
	if (!CurrentTexture) return FVector2D::ZeroVector;

	FVector2D TextureSize(CurrentTexture->GetSizeX() * ZoomLevel, CurrentTexture->GetSizeY() * ZoomLevel);
	FVector2D DrawOffset = PanOffset + (Geom.GetLocalSize() - TextureSize) * 0.5f;
	return TexturePos * ZoomLevel + DrawOffset;
}

FIntRect SSpriteExtractorCanvas::ClampToTextureBounds(const FIntRect& Rect) const
{
	if (!CurrentTexture) return Rect;
	FIntRect Clamped = Rect;
	Clamped.Min.X = FMath::Max(0, Clamped.Min.X);
	Clamped.Min.Y = FMath::Max(0, Clamped.Min.Y);
	Clamped.Max.X = FMath::Min(CurrentTexture->GetSizeX(), Clamped.Max.X);
	Clamped.Max.Y = FMath::Min(CurrentTexture->GetSizeY(), Clamped.Max.Y);
	return Clamped;
}

void SSpriteExtractorCanvas::EnterEditMode(int32 SpriteIndex)
{
	if (!DetectedSprites.IsValidIndex(SpriteIndex)) return;
	EditingSpriteIndex = SpriteIndex;
	DraggingHandle = EHandleType::None;
	PreDragBounds = DetectedSprites[SpriteIndex].Bounds;
	Invalidate(EInvalidateWidgetReason::Paint);
}

void SSpriteExtractorCanvas::ExitEditMode(bool bCommit)
{
	if (EditingSpriteIndex < 0) return;

	if (!bCommit && DetectedSprites.IsValidIndex(EditingSpriteIndex))
	{
		// Revert to pre-drag bounds
		DetectedSprites[EditingSpriteIndex].Bounds = PreDragBounds;
	}

	int32 PrevIndex = EditingSpriteIndex;
	EditingSpriteIndex = -1;
	DraggingHandle = EHandleType::None;
	Invalidate(EInvalidateWidgetReason::Paint);
}

void SSpriteExtractorCanvas::ToggleMergeSelection(int32 Index)
{
	if (!DetectedSprites.IsValidIndex(Index)) return;
	if (MergeSelectedIndices.Contains(Index))
	{
		MergeSelectedIndices.Remove(Index);
	}
	else
	{
		MergeSelectedIndices.Add(Index);
	}
}

void SSpriteExtractorCanvas::ClearMergeSelection()
{
	MergeSelectedIndices.Empty();
	Invalidate(EInvalidateWidgetReason::Paint);
}

EHandleType SSpriteExtractorCanvas::HitTestHandle(const FGeometry& Geom, const FVector2D& ScreenPos) const
{
	if (!IsInEditMode() || !DetectedSprites.IsValidIndex(EditingSpriteIndex)) return EHandleType::None;

	const FDetectedSprite& Sprite = DetectedSprites[EditingSpriteIndex];
	FVector2D TL = TextureToScreen(Geom, FVector2D(Sprite.Bounds.Min.X, Sprite.Bounds.Min.Y));
	FVector2D BR = TextureToScreen(Geom, FVector2D(Sprite.Bounds.Max.X, Sprite.Bounds.Max.Y));
	FVector2D Mid = (TL + BR) * 0.5f;

	const float HitRadius = 8.0f;

	struct FHandlePos { EHandleType Type; FVector2D Pos; };
	TArray<FHandlePos> Handles = {
		{ EHandleType::TopLeft,     TL },
		{ EHandleType::Top,         FVector2D(Mid.X, TL.Y) },
		{ EHandleType::TopRight,    FVector2D(BR.X, TL.Y) },
		{ EHandleType::Left,        FVector2D(TL.X, Mid.Y) },
		{ EHandleType::Right,       FVector2D(BR.X, Mid.Y) },
		{ EHandleType::BottomLeft,  FVector2D(TL.X, BR.Y) },
		{ EHandleType::Bottom,      FVector2D(Mid.X, BR.Y) },
		{ EHandleType::BottomRight, BR },
	};

	for (const FHandlePos& H : Handles)
	{
		if (FVector2D::Distance(ScreenPos, H.Pos) <= HitRadius)
		{
			return H.Type;
		}
	}

	return EHandleType::None;
}

#undef LOCTEXT_NAMESPACE
