// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "SpriteExtractorWindow.h"
#include "SpriteExtractionUtils.h"
#include "EditorCanvasUtils.h"
#include "Paper2DPlusCharacterDataAsset.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "ToolMenus.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Engine/Texture2D.h"
#include "PaperSprite.h"
#include "PaperFlipbook.h"
#include "ObjectTools.h"
#include "PackageTools.h"
#include "ScopedTransaction.h"
#include "DesktopPlatformModule.h"
#include "EditorDirectories.h"
#include "Misc/FileHelper.h"
#include "Misc/ScopedSlowTask.h"
#include "PropertyCustomizationHelpers.h"
#include "Styling/AppStyle.h"
#include "Misc/MessageDialog.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"

#define LOCTEXT_NAMESPACE "SpriteExtractor"

// ============================================
// Static Member Initialization
// ============================================

TArray<FSoftObjectPath> SSpriteExtractorWindow::RecentTextures;

// ============================================
// SSpriteExtractorCanvas Implementation
// ============================================

void SSpriteExtractorCanvas::Construct(const FArguments& InArgs)
{
	CurrentTexture = InArgs._Texture;
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

	// Draw texture if available
	if (CurrentTexture)
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
			AllottedGeometry.ToPaintGeometry(TextureSize, FSlateLayoutTransform(DrawPos)),
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
					AllottedGeometry.ToPaintGeometry(Size, FSlateLayoutTransform(TopLeft)),
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
				AllottedGeometry.ToPaintGeometry(FVector2D(Size.X, OutlineWidth), FSlateLayoutTransform(TopLeft)),
				WhiteBrush,
				ESlateDrawEffect::None,
				OutlineColor
			);
			// Bottom edge
			FSlateDrawElement::MakeBox(
				OutDrawElements,
				LayerId + 1,
				AllottedGeometry.ToPaintGeometry(FVector2D(Size.X, OutlineWidth), FSlateLayoutTransform(FVector2D(TopLeft.X, BottomRight.Y - OutlineWidth))),
				WhiteBrush,
				ESlateDrawEffect::None,
				OutlineColor
			);
			// Left edge
			FSlateDrawElement::MakeBox(
				OutDrawElements,
				LayerId + 1,
				AllottedGeometry.ToPaintGeometry(FVector2D(OutlineWidth, Size.Y), FSlateLayoutTransform(TopLeft)),
				WhiteBrush,
				ESlateDrawEffect::None,
				OutlineColor
			);
			// Right edge
			FSlateDrawElement::MakeBox(
				OutDrawElements,
				LayerId + 1,
				AllottedGeometry.ToPaintGeometry(FVector2D(OutlineWidth, Size.Y), FSlateLayoutTransform(FVector2D(BottomRight.X - OutlineWidth, TopLeft.Y))),
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
				AllottedGeometry.ToPaintGeometry(FVector2D(50, 20), FSlateLayoutTransform(TopLeft + FVector2D(4, 2))),
				IndexText,
				FontInfo,
				ESlateDrawEffect::None,
				FLinearColor::White
			);

			// Draw dimension text below sprite box
			FSlateDrawElement::MakeText(
				OutDrawElements,
				LayerId + 2,
				AllottedGeometry.ToPaintGeometry(FVector2D(100, 16), FSlateLayoutTransform(FVector2D(TopLeft.X + 4, BottomRight.Y + 2))),
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
					AllottedGeometry.ToPaintGeometry(Size, FSlateLayoutTransform(TopLeft)),
					WhiteBrush,
					ESlateDrawEffect::None,
					MergeColor
				);

				// Blue outline (4 edges)
				FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 2,
					AllottedGeometry.ToPaintGeometry(FVector2D(Size.X, MergeOutlineWidth), FSlateLayoutTransform(TopLeft)),
					WhiteBrush, ESlateDrawEffect::None, MergeOutline);
				FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 2,
					AllottedGeometry.ToPaintGeometry(FVector2D(Size.X, MergeOutlineWidth), FSlateLayoutTransform(FVector2D(TopLeft.X, BottomRight.Y - MergeOutlineWidth))),
					WhiteBrush, ESlateDrawEffect::None, MergeOutline);
				FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 2,
					AllottedGeometry.ToPaintGeometry(FVector2D(MergeOutlineWidth, Size.Y), FSlateLayoutTransform(TopLeft)),
					WhiteBrush, ESlateDrawEffect::None, MergeOutline);
				FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 2,
					AllottedGeometry.ToPaintGeometry(FVector2D(MergeOutlineWidth, Size.Y), FSlateLayoutTransform(FVector2D(BottomRight.X - MergeOutlineWidth, TopLeft.Y))),
					WhiteBrush, ESlateDrawEffect::None, MergeOutline);
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
					AllottedGeometry.ToPaintGeometry(FVector2D(HandleSize, HandleSize),
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
				AllottedGeometry.ToPaintGeometry(BoxSize, FSlateLayoutTransform(BoxTL)),
				WhiteBrush, ESlateDrawEffect::None, CyanFill);

			// Outline (4 edges)
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 1,
				AllottedGeometry.ToPaintGeometry(FVector2D(BoxSize.X, CyanWidth), FSlateLayoutTransform(BoxTL)),
				WhiteBrush, ESlateDrawEffect::None, CyanOutline);
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 1,
				AllottedGeometry.ToPaintGeometry(FVector2D(BoxSize.X, CyanWidth), FSlateLayoutTransform(FVector2D(BoxTL.X, BoxBR.Y - CyanWidth))),
				WhiteBrush, ESlateDrawEffect::None, CyanOutline);
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 1,
				AllottedGeometry.ToPaintGeometry(FVector2D(CyanWidth, BoxSize.Y), FSlateLayoutTransform(BoxTL)),
				WhiteBrush, ESlateDrawEffect::None, CyanOutline);
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 1,
				AllottedGeometry.ToPaintGeometry(FVector2D(CyanWidth, BoxSize.Y), FSlateLayoutTransform(FVector2D(BoxBR.X - CyanWidth, BoxTL.Y))),
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
			AllottedGeometry.ToPaintGeometry(FVector2D(200, 30), FSlateLayoutTransform(AllottedGeometry.GetLocalSize() * 0.5f - FVector2D(100, 15))),
			LOCTEXT("NoTexture", "No texture selected"),
			FontInfo,
			ESlateDrawEffect::None,
			FLinearColor(0.5f, 0.5f, 0.5f)
		);
		LayerId++;
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

// ============================================
// SSpriteExtractorWindow Implementation
// ============================================

void SSpriteExtractorWindow::Construct(const FArguments& InArgs)
{
	LoadRecentTextures();

	ChildSlot
	[
		SNew(SVerticalBox)

		// Top toolbar with primary actions
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			BuildMainToolbar()
		]

		// Main content area with splitter
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SNew(SSplitter)
			.Orientation(Orient_Horizontal)

			// Left panel - Settings
			+ SSplitter::Slot()
			.Value(0.25f)
			[
				SNew(SVerticalBox)

				// Texture settings warning banner (initially hidden)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SAssignNew(TextureSettingsBanner, SBorder)
					.BorderBackgroundColor(FLinearColor(0.8f, 0.6f, 0.0f, 1.0f))
					.Visibility(EVisibility::Collapsed)
					.Padding(FMargin(8, 6))
					[
						SNew(SHorizontalBox)

						+ SHorizontalBox::Slot()
						.FillWidth(1.0f)
						.VAlign(VAlign_Center)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("NeedsPaper2DSettings",
								"This texture needs Paper2D settings (nearest filter, no compression, no mips)."))
							.AutoWrapText(true)
						]

						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(4, 0)
						[
							SNew(SButton)
							.Text(LOCTEXT("ApplyNow", "Apply Now"))
							.OnClicked(this, &SSpriteExtractorWindow::OnApplyTextureSettingsClicked)
						]

						+ SHorizontalBox::Slot()
						.AutoWidth()
						[
							SNew(SButton)
							.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
							.Text(LOCTEXT("DismissBanner", "X"))
							.OnClicked_Lambda([this]()
							{
								TextureSettingsBanner->SetVisibility(EVisibility::Collapsed);
								return FReply::Handled();
							})
						]
					]
				]

				// Scrollable settings area
				+ SVerticalBox::Slot()
				.FillHeight(1.0f)
				[
					SNew(SScrollBox)
					+ SScrollBox::Slot()
					[
						SNew(SVerticalBox)

						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(4)
						[
							BuildTextureSection()
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(4)
						[
							BuildRecentTexturesSection()
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(4)
						[
							BuildDetectionSection()
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(4)
						[
							BuildOutputSection()
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(4)
						[
							BuildCharacterAssetSection()
						]
					]
				]
			]

			// Center - Canvas (wrapped in clipping box)
			+ SSplitter::Slot()
			.Value(0.55f)
			[
				SNew(SBox)
				.Clipping(EWidgetClipping::ClipToBounds)
				[
					SAssignNew(Canvas, SSpriteExtractorCanvas)
				]
			]

			// Right panel - Sprite list
			+ SSplitter::Slot()
			.Value(0.2f)
			[
				SNew(SVerticalBox)

				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					BuildSpriteListHeader()
				]

				+ SVerticalBox::Slot()
				.FillHeight(1.0f)
				.Padding(4)
				[
					SAssignNew(SpriteListScrollBox, SScrollBox)
					+ SScrollBox::Slot()
					[
						SAssignNew(SpriteListBox, SVerticalBox)
					]
				]
			]
		]
	];

	// Bind canvas delegates
	if (Canvas.IsValid())
	{
		Canvas->OnZoomChanged.BindLambda([this]() { UpdateStatusTexts(); });
		Canvas->OnSpriteSelectionToggled.BindRaw(this, &SSpriteExtractorWindow::OnCanvasSpriteSelectionToggled);

		Canvas->OnNewBoxDrawn.BindLambda([this](const FIntRect& NewBounds)
		{
			PushUndoState();

			TArray<FDetectedSprite>& Sprites = Canvas->GetDetectedSprites();

			FDetectedSprite NewSprite;
			NewSprite.OriginalBounds = NewBounds;
			NewSprite.Bounds = NewBounds;
			NewSprite.bSelected = true;
			Sprites.Add(NewSprite);

			// Iterative absorb — new sprite is last, absorb may shift indices
			FIntRect FinalBounds = AbsorbContainedSprites(NewBounds, Sprites, Sprites.Num() - 1);
			Sprites.Last().Bounds = FinalBounds;
			Sprites.Last().OriginalBounds = FinalBounds;

			// Re-index
			for (int32 i = 0; i < Sprites.Num(); i++) Sprites[i].Index = i;

			RefreshSpriteList();
			UpdateStatusTexts();
			Canvas->EnterEditMode(Sprites.Num() - 1);
		});

		Canvas->OnSpriteEdited.BindLambda([this](int32 SpriteIndex, const FIntRect& NewBounds)
		{
			PushUndoState();
			if (Canvas.IsValid() && Canvas->GetDetectedSprites().IsValidIndex(SpriteIndex))
			{
				FDetectedSprite& Sprite = Canvas->GetDetectedSprites()[SpriteIndex];
				Sprite.OriginalBounds = NewBounds;
				Sprite.Bounds = NewBounds;

				RefreshSpriteList();
				UpdateStatusTexts();
			}
		});

		}
}

// ============================================
// Keyboard Handler
// ============================================

FReply SSpriteExtractorWindow::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	// Don't intercept shortcuts when a text input widget has focus
	TSharedPtr<SWidget> FocusedWidget = FSlateApplication::Get().GetKeyboardFocusedWidget();
	if (FocusedWidget.IsValid())
	{
		FName WidgetType = FocusedWidget->GetType();
		if (WidgetType == TEXT("SEditableText") || WidgetType == TEXT("SMultiLineEditableText"))
		{
			return FReply::Unhandled();
		}
	}

	FKey Key = InKeyEvent.GetKey();

	// Ctrl+Shift+Z - Redo
	if (Key == EKeys::Z && InKeyEvent.IsControlDown() && InKeyEvent.IsShiftDown())
	{
		Redo();
		return FReply::Handled();
	}

	// Ctrl+Z - Undo
	if (Key == EKeys::Z && InKeyEvent.IsControlDown() && !InKeyEvent.IsShiftDown())
	{
		Undo();
		return FReply::Handled();
	}

	// Ctrl+Y - Redo
	if (Key == EKeys::Y && InKeyEvent.IsControlDown())
	{
		Redo();
		return FReply::Handled();
	}

	// Escape - Exit edit mode (cancel)
	if (Key == EKeys::Escape)
	{
		if (Canvas.IsValid() && Canvas->IsInEditMode())
		{
			Canvas->ExitEditMode(false);
			return FReply::Handled();
		}
	}

	// M - Merge selected sprites
	if (Key == EKeys::M)
	{
		if (Canvas.IsValid())
		{
			// Try merge selection (shift+click) first
			if (Canvas->GetMergeSelectedCount() >= 2)
			{
				TArray<int32> Indices = Canvas->GetMergeSelected().Array();
				MergeSelectedSprites(Indices);
				return FReply::Handled();
			}
			// Fall back to checkbox-selected sprites
			TArray<int32> SelectedIndices;
			TArray<FDetectedSprite>& Sprites = Canvas->GetDetectedSprites();
			for (int32 i = 0; i < Sprites.Num(); i++)
			{
				if (Sprites[i].bSelected) SelectedIndices.Add(i);
			}
			if (SelectedIndices.Num() >= 2)
			{
				MergeSelectedSprites(SelectedIndices);
				return FReply::Handled();
			}
		}
	}

	// Space - Run detection
	if (Key == EKeys::SpaceBar)
	{
		OnDetectSpritesClicked();
		return FReply::Handled();
	}

	// Enter - Extract selected sprites
	if (Key == EKeys::Enter)
	{
		OnExtractSpritesClicked();
		return FReply::Handled();
	}

	// A - Select all
	if (Key == EKeys::A && !InKeyEvent.IsControlDown())
	{
		OnSelectAllClicked();
		return FReply::Handled();
	}

	// D - Deselect all
	if (Key == EKeys::D)
	{
		OnDeselectAllClicked();
		return FReply::Handled();
	}

	// R - Reset view
	if (Key == EKeys::R)
	{
		if (Canvas.IsValid())
		{
			Canvas->ResetView();
			UpdateStatusTexts();
		}
		return FReply::Handled();
	}

	// + or = - Zoom in
	if (Key == EKeys::Add || Key == EKeys::Equals)
	{
		if (Canvas.IsValid())
		{
			Canvas->SetZoom(Canvas->GetZoom() + 0.25f);
			UpdateStatusTexts();
		}
		return FReply::Handled();
	}

	// - - Zoom out
	if (Key == EKeys::Subtract || Key == EKeys::Hyphen)
	{
		if (Canvas.IsValid())
		{
			Canvas->SetZoom(Canvas->GetZoom() - 0.25f);
			UpdateStatusTexts();
		}
		return FReply::Handled();
	}

	// 1-9 - Toggle selection of sprite by index
	int32 NumKeyIndex = -1;
	if (Key == EKeys::One) NumKeyIndex = 0;
	else if (Key == EKeys::Two) NumKeyIndex = 1;
	else if (Key == EKeys::Three) NumKeyIndex = 2;
	else if (Key == EKeys::Four) NumKeyIndex = 3;
	else if (Key == EKeys::Five) NumKeyIndex = 4;
	else if (Key == EKeys::Six) NumKeyIndex = 5;
	else if (Key == EKeys::Seven) NumKeyIndex = 6;
	else if (Key == EKeys::Eight) NumKeyIndex = 7;
	else if (Key == EKeys::Nine) NumKeyIndex = 8;

	if (NumKeyIndex >= 0)
	{
		if (Canvas.IsValid())
		{
			Canvas->ToggleSpriteSelection(NumKeyIndex);
			RefreshSpriteList();
			UpdateStatusTexts();
		}
		return FReply::Handled();
	}

	return FReply::Unhandled();
}

// ============================================
// Main Toolbar
// ============================================

TSharedRef<SWidget> SSpriteExtractorWindow::BuildMainToolbar()
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(FMargin(4))
		[
			SNew(SHorizontalBox)

			// Select Texture button
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(2)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
				.Text(LOCTEXT("SelectTexture", "Select Texture..."))
				.ToolTipText(LOCTEXT("SelectTextureTooltip", "Choose a texture to extract sprites from"))
				.OnClicked(this, &SSpriteExtractorWindow::OnSelectTextureClicked)
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(4, 0)
			[
				SNew(SSeparator)
				.Orientation(Orient_Vertical)
			]

			// Detect Sprites button
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(2)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "FlatButton.Primary")
				.Text(LOCTEXT("DetectSprites", "Detect Sprites"))
				.ToolTipText(LOCTEXT("DetectSpritesTooltip", "Run sprite detection on the texture (Space)"))
				.OnClicked(this, &SSpriteExtractorWindow::OnDetectSpritesClicked)
				.IsEnabled_Lambda([this]() { return SourceTexture != nullptr; })
			]

			// Extract button
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(2)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "FlatButton.Success")
				.Text(LOCTEXT("ExtractSelected", "Extract Selected"))
				.ToolTipText(LOCTEXT("ExtractSelectedTooltip", "Extract selected sprites and create assets (Enter)"))
				.OnClicked(this, &SSpriteExtractorWindow::OnExtractSpritesClicked)
				.IsEnabled_Lambda([this]()
				{
					if (!Canvas.IsValid()) return false;
					return GetSelectedSpriteCount() > 0;
				})
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(4, 0)
			[
				SNew(SSeparator)
				.Orientation(Orient_Vertical)
			]

			// Zoom controls
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(2)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("ZoomLabel", "Zoom:"))
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(2)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
				.Text(LOCTEXT("ZoomOut", "-"))
				.ToolTipText(LOCTEXT("ZoomOutTooltip", "Zoom out (-)"))
				.OnClicked_Lambda([this]()
				{
					if (Canvas.IsValid())
					{
						Canvas->SetZoom(Canvas->GetZoom() - 0.25f);
						UpdateStatusTexts();
					}
					return FReply::Handled();
				})
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(4, 0)
			[
				SAssignNew(ZoomText, STextBlock)
				.Text(LOCTEXT("ZoomDefault", "100%"))
				.MinDesiredWidth(50)
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(2)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
				.Text(LOCTEXT("ZoomIn", "+"))
				.ToolTipText(LOCTEXT("ZoomInTooltip", "Zoom in (+)"))
				.OnClicked_Lambda([this]()
				{
					if (Canvas.IsValid())
					{
						Canvas->SetZoom(Canvas->GetZoom() + 0.25f);
						UpdateStatusTexts();
					}
					return FReply::Handled();
				})
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(2)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
				.Text(LOCTEXT("ResetView", "Reset"))
				.ToolTipText(LOCTEXT("ResetViewTooltip", "Reset view to default zoom and position (R)"))
				.OnClicked_Lambda([this]()
				{
					if (Canvas.IsValid())
					{
						Canvas->ResetView();
						UpdateStatusTexts();
					}
					return FReply::Handled();
				})
			]

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SNullWidget::NullWidget
			]

			// Selection count (right side)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(4, 0)
			[
				SAssignNew(SelectionCountText, STextBlock)
				.Text(LOCTEXT("NoSelection", "No sprites detected"))
			]
		];
}

TSharedRef<SWidget> SSpriteExtractorWindow::BuildTextureSection()
{
	return SNew(SExpandableArea)
		.AreaTitle(LOCTEXT("SourceTextureTitle", "SOURCE TEXTURE"))
		.InitiallyCollapsed(false)
		.BodyContent()
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(4)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
				.Padding(4)
				[
					SNew(STextBlock)
					.Text_Lambda([this]()
					{
						if (!SourceTexture)
						{
							return LOCTEXT("NoTextureSelected", "No texture selected - use toolbar to select");
						}
						return FText::Format(
							LOCTEXT("TextureInfo", "{0}\n{1} x {2}"),
							FText::FromString(SourceTexture->GetName()),
							FText::AsNumber(SourceTexture->GetSizeX()),
							FText::AsNumber(SourceTexture->GetSizeY())
						);
					})
					.AutoWrapText(true)
				]
			]
		];
}

// ============================================
// Recent Textures
// ============================================

void SSpriteExtractorWindow::AddToRecentTextures(UTexture2D* Texture)
{
	if (!Texture) return;

	FSoftObjectPath TexturePath(Texture);

	// Remove if already in list (to move to front)
	RecentTextures.RemoveAll([&TexturePath](const FSoftObjectPath& Path)
	{
		return Path == TexturePath;
	});

	// Insert at front
	RecentTextures.Insert(TexturePath, 0);

	// Trim to max
	if (RecentTextures.Num() > MaxRecentTextures)
	{
		RecentTextures.SetNum(MaxRecentTextures);
	}

	SaveRecentTextures();
}

void SSpriteExtractorWindow::LoadRecentTextures()
{
	RecentTextures.Empty();
	FString ConfigSection = TEXT("Paper2DPlus.SpriteExtractor");
	int32 Count = 0;
	GConfig->GetInt(*ConfigSection, TEXT("RecentTextureCount"), Count, GEditorPerProjectIni);
	for (int32 i = 0; i < Count && i < MaxRecentTextures; i++)
	{
		FString Path;
		FString Key = FString::Printf(TEXT("RecentTexture_%d"), i);
		if (GConfig->GetString(*ConfigSection, *Key, Path, GEditorPerProjectIni))
		{
			RecentTextures.Add(FSoftObjectPath(Path));
		}
	}
}

void SSpriteExtractorWindow::SaveRecentTextures()
{
	FString ConfigSection = TEXT("Paper2DPlus.SpriteExtractor");
	GConfig->SetInt(*ConfigSection, TEXT("RecentTextureCount"), RecentTextures.Num(), GEditorPerProjectIni);
	for (int32 i = 0; i < RecentTextures.Num(); i++)
	{
		FString Key = FString::Printf(TEXT("RecentTexture_%d"), i);
		GConfig->SetString(*ConfigSection, *Key, *RecentTextures[i].ToString(), GEditorPerProjectIni);
	}
	GConfig->Flush(false, GEditorPerProjectIni);
}

TSharedRef<SWidget> SSpriteExtractorWindow::BuildRecentTexturesSection()
{
	// Show placeholder if no recent textures
	if (RecentTextures.Num() == 0)
	{
		return SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(4, 2)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("RecentTextures", "Recent Textures"))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(8, 2)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("NoRecentTextures", "No recent textures"))
				.Font(FCoreStyle::GetDefaultFontStyle("Italic", 9))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)))
			];
	}

	TSharedRef<SVerticalBox> RecentBox = SNew(SVerticalBox);

	RecentBox->AddSlot()
	.AutoHeight()
	.Padding(4, 2)
	[
		SNew(STextBlock)
		.Text(LOCTEXT("RecentTextures", "Recent Textures"))
		.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
	];

	for (int32 i = 0; i < RecentTextures.Num(); i++)
	{
		FSoftObjectPath Path = RecentTextures[i];
		FString DisplayName = FPaths::GetBaseFilename(Path.GetAssetName());

		RecentBox->AddSlot()
		.AutoHeight()
		.Padding(8, 1)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.OnClicked_Lambda([this, Path]() -> FReply
			{
				UTexture2D* Texture = Cast<UTexture2D>(Path.TryLoad());
				if (Texture)
				{
					SetInitialTexture(Texture);
					// Also update internal state
					SourceTexture = Texture;
					SourceTexturePath = Path.ToString();
					if (Canvas.IsValid())
					{
						Canvas->SetTexture(Texture);
						Canvas->ResetView();
					}
					RefreshCanvas();
					AddToRecentTextures(Texture);
				}
				return FReply::Handled();
			})
			.ToolTipText(FText::FromString(Path.ToString()))
			[
				SNew(STextBlock)
				.Text(FText::FromString(DisplayName))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
			]
		];
	}

	return RecentBox;
}

TSharedRef<SWidget> SSpriteExtractorWindow::BuildDetectionSection()
{
	return SNew(SExpandableArea)
		.AreaTitle(LOCTEXT("DetectionTitle", "DETECTION SETTINGS"))
		.InitiallyCollapsed(false)
		.BodyContent()
		[
			SNew(SVerticalBox)

			// Mode selection with radio buttons
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(4)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("ModeLabel", "Detection Mode"))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(4, 2)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0, 0, 16, 0)
				[
					SNew(SCheckBox)
					.Style(FAppStyle::Get(), "RadioButton")
					.ToolTipText(LOCTEXT("IslandModeTooltip", "Automatically detect sprites by finding connected regions of opaque pixels using flood fill. Best for sprite sheets with irregular spacing or varying sprite sizes."))
					.IsChecked_Lambda([this]() { return DetectionMode == ESpriteDetectionMode::Island ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
					.OnCheckStateChanged_Lambda([this](ECheckBoxState State) { if (State == ECheckBoxState::Checked) { DetectionMode = ESpriteDetectionMode::Island; ScheduleAutoDetect(); } })
					[
						SNew(STextBlock).Text(LOCTEXT("IslandMode", "Island Detection"))
					]
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SCheckBox)
					.Style(FAppStyle::Get(), "RadioButton")
					.ToolTipText(LOCTEXT("GridModeTooltip", "Split the texture into a uniform grid of equal-sized cells. Best for sprite sheets with consistent spacing and fixed cell sizes."))
					.IsChecked_Lambda([this]() { return DetectionMode == ESpriteDetectionMode::Grid ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
					.OnCheckStateChanged_Lambda([this](ECheckBoxState State) { if (State == ECheckBoxState::Checked) { DetectionMode = ESpriteDetectionMode::Grid; ScheduleAutoDetect(); } })
					[
						SNew(STextBlock).Text(LOCTEXT("GridMode", "Grid Split"))
					]
				]
			]

			// Common settings
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(4, 8, 4, 2)
			[
				SNew(SHorizontalBox)
				.ToolTipText(LOCTEXT("AlphaThresholdTooltip", "Minimum alpha value (0-255) for a pixel to be considered opaque. Pixels with alpha below this value are treated as transparent. Lower values detect more semi-transparent pixels, higher values are more strict. Default: 1"))

				+ SHorizontalBox::Slot()
				.FillWidth(0.5f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("AlphaThreshold", "Alpha Threshold:"))
				]

				+ SHorizontalBox::Slot()
				.FillWidth(0.5f)
				[
					SNew(SNumericEntryBox<int32>)
					.Value_Lambda([this]() { return AlphaThreshold; })
					.OnValueCommitted_Lambda([this](int32 Value, ETextCommit::Type) { AlphaThreshold = FMath::Clamp(Value, 0, 255); ScheduleAutoDetect(); })
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(4, 2)
			[
				SNew(SHorizontalBox)
				.ToolTipText(LOCTEXT("MinSizeTooltip", "Minimum width and height in pixels for a detected region to be considered a sprite. Regions smaller than this are ignored. Useful for filtering out noise or small artifacts. Default: 4"))

				+ SHorizontalBox::Slot()
				.FillWidth(0.5f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("MinSize", "Min Sprite Size:"))
				]

				+ SHorizontalBox::Slot()
				.FillWidth(0.5f)
				[
					SNew(SNumericEntryBox<int32>)
					.Value_Lambda([this]() { return MinSpriteSize; })
					.OnValueCommitted_Lambda([this](int32 Value, ETextCommit::Type) { MinSpriteSize = FMath::Max(1, Value); ScheduleAutoDetect(); })
				]
			]

			// Island detection specific settings
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(4, 8, 4, 2)
			[
				SNew(STextBlock)
				.Visibility_Lambda([this]() { return DetectionMode == ESpriteDetectionMode::Island ? EVisibility::Visible : EVisibility::Collapsed; })
				.Text(LOCTEXT("IslandOptions", "Island Options"))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(4, 2)
			[
				SNew(SCheckBox)
				.Visibility_Lambda([this]() { return DetectionMode == ESpriteDetectionMode::Island ? EVisibility::Visible : EVisibility::Collapsed; })
				.ToolTipText(LOCTEXT("8DirTooltip", "When enabled, flood fill checks 8 neighboring pixels (including diagonals). When disabled, only checks 4 neighbors (up/down/left/right). Enable this to properly detect sprites with diagonally-connected parts like angled swords or limbs."))
				.IsChecked_Lambda([this]() { return bUse8DirectionalFloodFill ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this](ECheckBoxState State) { bUse8DirectionalFloodFill = (State == ECheckBoxState::Checked); ScheduleAutoDetect(); })
				[
					SNew(STextBlock).Text(LOCTEXT("8Dir", "8-directional (catches diagonals)"))
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(4, 2)
			[
				SNew(SHorizontalBox)
				.Visibility_Lambda([this]() { return DetectionMode == ESpriteDetectionMode::Island ? EVisibility::Visible : EVisibility::Collapsed; })
				.ToolTipText(LOCTEXT("MergeDistTooltip", "Maximum pixel distance between separate islands to merge them into a single sprite. Useful for sprites with disconnected parts like floating accessories, shadows, or effects. Set to 0 to disable merging. Default: 2"))

				+ SHorizontalBox::Slot()
				.FillWidth(0.5f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("MergeDist", "Merge Distance:"))
				]

				+ SHorizontalBox::Slot()
				.FillWidth(0.5f)
				[
					SNew(SNumericEntryBox<int32>)
					.Value_Lambda([this]() { return IslandMergeDistance; })
					.OnValueCommitted_Lambda([this](int32 Value, ETextCommit::Type) { IslandMergeDistance = FMath::Clamp(Value, 0, 50); ScheduleAutoDetect(); })
				]
			]

			// Grid-specific settings
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(4, 8, 4, 2)
			[
				SNew(STextBlock)
				.Visibility_Lambda([this]() { return DetectionMode == ESpriteDetectionMode::Grid ? EVisibility::Visible : EVisibility::Collapsed; })
				.Text(LOCTEXT("GridOptions", "Grid Options"))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(4, 2)
			[
				SNew(SHorizontalBox)
				.Visibility_Lambda([this]() { return DetectionMode == ESpriteDetectionMode::Grid ? EVisibility::Visible : EVisibility::Collapsed; })
				.ToolTipText(LOCTEXT("GridColumnsTooltip", "Number of columns to divide the texture into. The texture width will be split evenly into this many columns. Each cell becomes one sprite."))

				+ SHorizontalBox::Slot()
				.FillWidth(0.5f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("GridColumns", "Columns:"))
				]

				+ SHorizontalBox::Slot()
				.FillWidth(0.5f)
				[
					SNew(SNumericEntryBox<int32>)
					.Value_Lambda([this]() { return GridColumns; })
					.OnValueCommitted_Lambda([this](int32 Value, ETextCommit::Type) { GridColumns = FMath::Max(1, Value); ScheduleAutoDetect(); })
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(4, 2)
			[
				SNew(SHorizontalBox)
				.Visibility_Lambda([this]() { return DetectionMode == ESpriteDetectionMode::Grid ? EVisibility::Visible : EVisibility::Collapsed; })
				.ToolTipText(LOCTEXT("GridRowsTooltip", "Number of rows to divide the texture into. The texture height will be split evenly into this many rows. Total sprites = Columns x Rows."))

				+ SHorizontalBox::Slot()
				.FillWidth(0.5f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("GridRows", "Rows:"))
				]

				+ SHorizontalBox::Slot()
				.FillWidth(0.5f)
				[
					SNew(SNumericEntryBox<int32>)
					.Value_Lambda([this]() { return GridRows; })
					.OnValueCommitted_Lambda([this](int32 Value, ETextCommit::Type) { GridRows = FMath::Max(1, Value); ScheduleAutoDetect(); })
				]
			]

			// Auto-update checkbox
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(4, 8, 4, 2)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SCheckBox)
					.ToolTipText(LOCTEXT("AutoUpdateTooltip", "Automatically re-run sprite detection when detection parameters change. Uses 300ms debounce to batch rapid changes."))
					.IsChecked_Lambda([this]() { return bAutoUpdateDetection ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
					.OnCheckStateChanged_Lambda([this](ECheckBoxState State)
					{
						bAutoUpdateDetection = (State == ECheckBoxState::Checked);
						if (bAutoUpdateDetection && SourceTexture)
						{
							OnDetectSpritesClicked();
						}
					})
					[
						SNew(STextBlock).Text(LOCTEXT("AutoUpdate", "Auto-Update"))
					]
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(8, 0, 0, 0)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("AutoUpdatePaused", "(paused - editing)"))
					.ColorAndOpacity(FSlateColor(FLinearColor(0.8f, 0.6f, 0.2f)))
					.Visibility_Lambda([this]()
					{
						return (bAutoUpdateDetection && Canvas.IsValid() && Canvas->IsInEditMode())
							? EVisibility::Visible : EVisibility::Collapsed;
					})
				]
			]
		];
}

TSharedRef<SWidget> SSpriteExtractorWindow::BuildOutputSection()
{
	return SNew(SExpandableArea)
		.AreaTitle(LOCTEXT("OutputTitle", "OUTPUT SETTINGS"))
		.InitiallyCollapsed(false)
		.BodyContent()
		[
			SNew(SVerticalBox)

			// Naming section header
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(4)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("NamingLabel", "Naming"))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
			]

			// Prefix field
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(4, 2)
			[
				SNew(SHorizontalBox)
				.ToolTipText(LOCTEXT("PrefixTooltip", "Character/entity name prefix. Auto-detected from texture name at the selected split point."))

				+ SHorizontalBox::Slot()
				.FillWidth(0.3f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("Prefix", "Prefix:"))
				]

				+ SHorizontalBox::Slot()
				.FillWidth(0.7f)
				[
					SNew(SEditableTextBox)
					.Text_Lambda([this]() { return FText::FromString(NamePrefix); })
					.OnTextCommitted_Lambda([this](const FText& Text, ETextCommit::Type) { NamePrefix = Text.ToString(); UpdateOutputPath(); })
				]
			]

			// Split point dropdown (only shown if separators found)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(4, 2)
			[
				SNew(SHorizontalBox)
				.Visibility_Lambda([this]() { return SeparatorPositions.Num() > 0 ? EVisibility::Visible : EVisibility::Collapsed; })
				.ToolTipText(LOCTEXT("SplitPointTooltip", "Choose where to split the texture name into prefix and base name"))

				+ SHorizontalBox::Slot()
				.FillWidth(0.3f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("SplitAt", "Split at:"))
				]

				+ SHorizontalBox::Slot()
				.FillWidth(0.7f)
				[
					SAssignNew(SplitComboBox, SComboBox<TSharedPtr<int32>>)
					.OptionsSource(&SplitOptions)
					.OnSelectionChanged_Lambda([this](TSharedPtr<int32> Selection, ESelectInfo::Type)
					{
						if (!Selection.IsValid()) return;
						SplitIndex = *Selection;
						if (SourceTexture && SeparatorPositions.IsValidIndex(SplitIndex))
						{
							FString TexName = SourceTexture->GetName();
							int32 CharIdx = SeparatorPositions[SplitIndex];
							NamePrefix = TexName.Left(CharIdx);
							NameSeparator = TexName.Mid(CharIdx, 1);
							NameBase = TexName.Mid(CharIdx + 1);
							UpdateOutputPath();
						}
					})
					.OnGenerateWidget_Lambda([this](TSharedPtr<int32> Item) -> TSharedRef<SWidget>
					{
						if (!Item.IsValid() || !SourceTexture) return SNew(STextBlock).Text(LOCTEXT("Invalid", "?"));
						FString TexName = SourceTexture->GetName();
						int32 Idx = *Item;
						if (!SeparatorPositions.IsValidIndex(Idx)) return SNew(STextBlock).Text(LOCTEXT("Invalid", "?"));
						int32 CharIdx = SeparatorPositions[Idx];
						FString Preview = FString::Printf(TEXT("%s | %s"),
							*TexName.Left(CharIdx),
							*TexName.Mid(CharIdx));
						return SNew(STextBlock).Text(FText::FromString(Preview));
					})
					[
						SNew(STextBlock)
						.Text_Lambda([this]()
						{
							if (!SourceTexture || !SeparatorPositions.IsValidIndex(SplitIndex))
								return LOCTEXT("NoSplit", "No split");
							FString TexName = SourceTexture->GetName();
							int32 CharIdx = SeparatorPositions[SplitIndex];
							return FText::FromString(FString::Printf(TEXT("%s | %s"),
								*TexName.Left(CharIdx), *TexName.Mid(CharIdx)));
						})
					]
				]
			]

			// Name field
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(4, 2)
			[
				SNew(SHorizontalBox)
				.ToolTipText(LOCTEXT("NameTooltip", "Animation/action name. Sprites get this + index suffix. Flipbook gets just this name (with prefix)."))

				+ SHorizontalBox::Slot()
				.FillWidth(0.3f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("Name", "Name:"))
				]

				+ SHorizontalBox::Slot()
				.FillWidth(0.7f)
				[
					SNew(SEditableTextBox)
					.Text_Lambda([this]() { return FText::FromString(NameBase); })
					.OnTextCommitted_Lambda([this](const FText& Text, ETextCommit::Type) { NameBase = Text.ToString(); UpdateOutputPath(); })
				]
			]

			// Name preview
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(4, 4)
			[
				SNew(STextBlock)
				.Text_Lambda([this]()
				{
					return FText::Format(LOCTEXT("NamePreview", "Sprites: {0}  |  Flipbook: {1}  |  Folder: {2}/"),
						FText::FromString(GetSpriteName(0)),
						FText::FromString(GetFlipbookName()),
						FText::FromString(GetOutputFolderName()));
				})
				.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.7f, 0.5f)))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
			]

			// Output path
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(4, 4, 4, 2)
			[
				SNew(SHorizontalBox)
				.ToolTipText(LOCTEXT("OutputPathTooltip", "Content browser path where extracted sprites will be saved."))

				+ SHorizontalBox::Slot()
				.FillWidth(0.3f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("OutputPath", "Output Path:"))
				]

				+ SHorizontalBox::Slot()
				.FillWidth(0.7f)
				[
					SNew(SEditableTextBox)
					.Text_Lambda([this]() { return FText::FromString(OutputPath.IsEmpty() ? TEXT("/Game/Sprites") : OutputPath); })
					.OnTextCommitted_Lambda([this](const FText& Text, ETextCommit::Type) { OutputPath = Text.ToString(); })
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(24, 0, 4, 0)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("InvalidPathWarning", "Path should start with /Game/"))
				.ColorAndOpacity(FLinearColor(1.0f, 0.3f, 0.3f))
				.Visibility_Lambda([this]()
				{
					FString Path = OutputPath.IsEmpty() ? TEXT("/Game/Sprites") : OutputPath;
					return Path.StartsWith(TEXT("/Game/")) ? EVisibility::Collapsed : EVisibility::Visible;
				})
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(4, 8, 4, 2)
			[
				SNew(SCheckBox)
				.ToolTipText(LOCTEXT("CreateFlipbookTooltip", "Automatically create a PaperFlipbook asset from the extracted sprites."))
				.IsChecked_Lambda([this]() { return bCreateFlipbook ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this](ECheckBoxState State) { bCreateFlipbook = (State == ECheckBoxState::Checked); })
				[
					SNew(STextBlock).Text(LOCTEXT("CreateFlipbook", "Create Flipbook"))
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(4, 2)
			[
				SNew(SHorizontalBox)
				.Visibility_Lambda([this]() { return bCreateFlipbook ? EVisibility::Visible : EVisibility::Collapsed; })
				.ToolTipText(LOCTEXT("FrameRateTooltip", "Playback speed in frames per second. 8-12 for walk, 12-24 for combat."))

				+ SHorizontalBox::Slot()
				.FillWidth(0.3f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("FrameRate", "Frame Rate:"))
				]

				+ SHorizontalBox::Slot()
				.FillWidth(0.7f)
				[
					SNew(SNumericEntryBox<float>)
					.Value_Lambda([this]() { return FlipbookFrameRate; })
					.OnValueCommitted_Lambda([this](float Value, ETextCommit::Type) { FlipbookFrameRate = FMath::Max(0.1f, Value); })
				]
			]

			// Flipbook duration preview
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(4, 2)
			[
				SNew(STextBlock)
				.Visibility_Lambda([this]() { return bCreateFlipbook ? EVisibility::Visible : EVisibility::Collapsed; })
				.Text_Lambda([this]()
				{
					int32 FrameCount = GetSelectedSpriteCount();
					if (FrameCount > 0 && FlipbookFrameRate > 0)
					{
						float Duration = FrameCount / FlipbookFrameRate;
						return FText::Format(
							LOCTEXT("FlipbookDuration", "Duration: {0} frames @ {1} FPS = {2}s"),
							FText::AsNumber(FrameCount),
							FText::AsNumber(FlipbookFrameRate),
							FText::AsNumber(Duration)
						);
					}
					return LOCTEXT("NoDuration", "No sprites selected");
				})
				.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)))
			]
		];
}

TSharedRef<SWidget> SSpriteExtractorWindow::BuildCharacterAssetSection()
{
	return SNew(SExpandableArea)
		.AreaTitle(LOCTEXT("CharacterAssetTitle", "CHARACTER DATA ASSET"))
		.InitiallyCollapsed(true)
		.BodyContent()
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(4)
			[
				SNew(SCheckBox)
				.ToolTipText(LOCTEXT("AddToAssetTooltip", "When enabled, the created flipbook will be automatically added to a Paper2DPlus Character Data Asset as a new animation. Requires a target asset and animation name."))
				.IsChecked_Lambda([this]() { return bAddToCharacterAsset ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this](ECheckBoxState State) { bAddToCharacterAsset = (State == ECheckBoxState::Checked); })
				[
					SNew(STextBlock).Text(LOCTEXT("AddToAsset", "Add to Character Data Asset"))
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(4, 2)
			[
				SNew(SHorizontalBox)
				.Visibility_Lambda([this]() { return bAddToCharacterAsset ? EVisibility::Visible : EVisibility::Collapsed; })
				.ToolTipText(LOCTEXT("TargetAssetTooltip", "The Paper2DPlus Character Data Asset to add the animation to. This asset stores all animations for a character, including flipbooks, hitboxes, and frame events."))

				+ SHorizontalBox::Slot()
				.FillWidth(0.3f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("TargetAsset", "Target Asset:"))
				]

				+ SHorizontalBox::Slot()
				.FillWidth(0.7f)
				[
					SNew(SObjectPropertyEntryBox)
					.AllowedClass(UPaper2DPlusCharacterDataAsset::StaticClass())
					.ObjectPath_Lambda([this]() { return TargetCharacterAsset ? TargetCharacterAsset->GetPathName() : FString(); })
					.OnObjectChanged_Lambda([this](const FAssetData& AssetData) {
						TargetCharacterAsset = Cast<UPaper2DPlusCharacterDataAsset>(AssetData.GetAsset());
					})
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(4, 2)
			[
				SNew(SHorizontalBox)
				.Visibility_Lambda([this]() { return bAddToCharacterAsset ? EVisibility::Visible : EVisibility::Collapsed; })
				.ToolTipText(LOCTEXT("AnimationNameTooltip", "Name for this animation in the Character Data Asset. Use descriptive names like 'Idle', 'Walk', 'Attack1', 'Jump_Start'. This name is used to look up animations at runtime."))

				+ SHorizontalBox::Slot()
				.FillWidth(0.3f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("AnimationName", "Animation Name:"))
				]

				+ SHorizontalBox::Slot()
				.FillWidth(0.7f)
				[
					SNew(SEditableTextBox)
					.Text_Lambda([this]() { return FText::FromString(AnimationName); })
					.OnTextCommitted_Lambda([this](const FText& Text, ETextCommit::Type) { AnimationName = Text.ToString(); })
				]
			]
		];
}

TSharedRef<SWidget> SSpriteExtractorWindow::BuildSpriteListHeader()
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(4)
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(2)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("DetectedSpritesTitle", "DETECTED SPRITES"))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(2)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(2)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
					.Text(LOCTEXT("All", "All"))
					.ToolTipText(LOCTEXT("SelectAllTooltip", "Select all sprites (A)"))
					.OnClicked(this, &SSpriteExtractorWindow::OnSelectAllClicked)
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(2)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
					.Text(LOCTEXT("None", "None"))
					.ToolTipText(LOCTEXT("DeselectAllTooltip", "Deselect all sprites (D)"))
					.OnClicked(this, &SSpriteExtractorWindow::OnDeselectAllClicked)
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(2)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
					.Text(LOCTEXT("Invert", "Invert"))
					.ToolTipText(LOCTEXT("InvertTooltip", "Invert selection"))
					.OnClicked(this, &SSpriteExtractorWindow::OnInvertSelectionClicked)
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(8, 2, 2, 2)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "FlatButton.Primary")
					.Text(LOCTEXT("Merge", "Merge"))
					.ToolTipText(LOCTEXT("MergeTooltip", "Merge selected sprites into one bounding box (M). Shift+click on canvas to select merge targets."))
					.IsEnabled_Lambda([this]()
					{
						if (!Canvas.IsValid()) return false;
						if (Canvas->GetMergeSelectedCount() >= 2) return true;
						return GetSelectedSpriteCount() >= 2;
					})
					.OnClicked_Lambda([this]() -> FReply
					{
						if (!Canvas.IsValid()) return FReply::Handled();
						// Prefer merge selection
						if (Canvas->GetMergeSelectedCount() >= 2)
						{
							TArray<int32> Indices = Canvas->GetMergeSelected().Array();
							MergeSelectedSprites(Indices);
						}
						else
						{
							TArray<int32> SelectedIndices;
							TArray<FDetectedSprite>& Sprites = Canvas->GetDetectedSprites();
							for (int32 i = 0; i < Sprites.Num(); i++)
							{
								if (Sprites[i].bSelected) SelectedIndices.Add(i);
							}
							if (SelectedIndices.Num() >= 2)
							{
								MergeSelectedSprites(SelectedIndices);
							}
						}
						return FReply::Handled();
					})
				]
			]
		];
}

// ============================================
// Helper Functions
// ============================================

void SSpriteExtractorWindow::CheckTextureSettings()
{
	if (!TextureSettingsBanner.IsValid()) return;

	if (SourceTexture && FSpriteExtractionUtils::NeedsPaper2DSettings(SourceTexture))
	{
		TextureSettingsBanner->SetVisibility(EVisibility::Visible);
	}
	else
	{
		TextureSettingsBanner->SetVisibility(EVisibility::Collapsed);
	}
}

FReply SSpriteExtractorWindow::OnApplyTextureSettingsClicked()
{
	if (SourceTexture)
	{
		FSpriteExtractionUtils::ApplyPaper2DSettings(SourceTexture);
		CheckTextureSettings();

		// Refresh canvas to show texture with new settings
		if (Canvas.IsValid())
		{
			Canvas->SetTexture(SourceTexture);
		}
	}
	return FReply::Handled();
}

void SSpriteExtractorWindow::UpdateStatusTexts()
{
	if (ZoomText.IsValid() && Canvas.IsValid())
	{
		int32 ZoomPercent = FMath::RoundToInt(Canvas->GetZoom() * 100.0f);
		ZoomText->SetText(FText::Format(LOCTEXT("ZoomPercent", "{0}%"), FText::AsNumber(ZoomPercent)));
	}

	if (SelectionCountText.IsValid() && Canvas.IsValid())
	{
		TArray<FDetectedSprite>& Sprites = Canvas->GetDetectedSprites();
		int32 TotalCount = Sprites.Num();
		int32 SelectedCount = GetSelectedSpriteCount();

		if (TotalCount == 0)
		{
			SelectionCountText->SetText(LOCTEXT("NoSpritesDetected", "No sprites detected"));
		}
		else
		{
			SelectionCountText->SetText(FText::Format(
				LOCTEXT("SelectionCount", "Selected: {0} of {1}"),
				FText::AsNumber(SelectedCount),
				FText::AsNumber(TotalCount)
			));
		}
	}
}

int32 SSpriteExtractorWindow::GetSelectedSpriteCount() const
{
	if (!Canvas.IsValid()) return 0;

	int32 Count = 0;
	TArray<FDetectedSprite>& Sprites = const_cast<SSpriteExtractorCanvas*>(Canvas.Get())->GetDetectedSprites();
	for (const FDetectedSprite& S : Sprites)
	{
		if (S.bSelected) Count++;
	}
	return Count;
}

FReply SSpriteExtractorWindow::OnInvertSelectionClicked()
{
	if (Canvas.IsValid())
	{
		TArray<FDetectedSprite>& Sprites = Canvas->GetDetectedSprites();
		for (FDetectedSprite& S : Sprites)
		{
			S.bSelected = !S.bSelected;
		}
		RefreshSpriteList();
		UpdateStatusTexts();
	}
	return FReply::Handled();
}

void SSpriteExtractorWindow::SetInitialTexture(UTexture2D* Texture)
{
	if (Texture)
	{
		SourceTexture = Texture;
		SourceTexturePath = Texture->GetPathName();

		if (Canvas.IsValid())
		{
			Canvas->SetTexture(SourceTexture);
		}

		DetectedSprites.Empty();
		RefreshSpriteList();

		// Auto-detect name parts from texture name
		AutoDetectNameParts(SourceTexture->GetName());
		UpdateOutputPath();

		CheckTextureSettings();
	}
}

FReply SSpriteExtractorWindow::OnSelectTextureClicked()
{
	FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");

	FOpenAssetDialogConfig Config;
	Config.DialogTitleOverride = LOCTEXT("SelectTexture", "Select Texture");
	Config.AssetClassNames.Add(UTexture2D::StaticClass()->GetClassPathName());
	Config.bAllowMultipleSelection = false;

	TArray<FAssetData> SelectedAssets = ContentBrowserModule.Get().CreateModalOpenAssetDialog(Config);
	if (SelectedAssets.Num() > 0)
	{
		SourceTexture = Cast<UTexture2D>(SelectedAssets[0].GetAsset());
		if (SourceTexture)
		{
			SourceTexturePath = SelectedAssets[0].GetObjectPathString();
			Canvas->SetTexture(SourceTexture);
			DetectedSprites.Empty();
			RefreshSpriteList();

			// Auto-detect name parts from texture name
			AutoDetectNameParts(SourceTexture->GetName());
			UpdateOutputPath();

			CheckTextureSettings();

			// Add to recent textures list
			AddToRecentTextures(SourceTexture);
		}
	}

	return FReply::Handled();
}

FReply SSpriteExtractorWindow::OnSelectAllClicked()
{
	if (Canvas.IsValid())
	{
		Canvas->SelectAll(true);
		RefreshSpriteList();
		UpdateStatusTexts();
	}
	return FReply::Handled();
}

FReply SSpriteExtractorWindow::OnDeselectAllClicked()
{
	if (Canvas.IsValid())
	{
		Canvas->SelectAll(false);
		RefreshSpriteList();
		UpdateStatusTexts();
	}
	return FReply::Handled();
}

FReply SSpriteExtractorWindow::OnDetectSpritesClicked()
{
	if (!SourceTexture) return FReply::Handled();

	// Pre-flight: verify texture data is accessible (Island mode loads pixels)
	if (DetectionMode == ESpriteDetectionMode::Island)
	{
		TArray<FColor> TestPixels;
		int32 TestW, TestH;
		if (!FSpriteExtractionUtils::LoadTextureData(SourceTexture, TestPixels, TestW, TestH))
		{
			if (!FSpriteExtractionUtils::NeedsPaper2DSettings(SourceTexture))
			{
				// Settings look correct — likely a CPU access issue
				EAppReturnType::Type Result = FMessageDialog::Open(
					EAppMsgType::YesNo,
					LOCTEXT("ForceCPUAccess",
						"Texture data not accessible. This may be a CPU access issue.\n\n"
						"Force CPU access rebuild? This will modify the texture asset."));

				if (Result == EAppReturnType::Yes)
				{
					if (FSpriteExtractionUtils::ForceCPUAccess(SourceTexture))
					{
						if (Canvas.IsValid())
						{
							Canvas->SetTexture(SourceTexture);
						}
						// Retry load
						if (!FSpriteExtractionUtils::LoadTextureData(SourceTexture, TestPixels, TestW, TestH))
						{
							FNotificationInfo Info(LOCTEXT("LoadFailed",
								"Failed to load texture data. Try re-importing the texture from source."));
							Info.ExpireDuration = 5.0f;
							FSlateNotificationManager::Get().AddNotification(Info);
							return FReply::Handled();
						}
						// Success — fall through to detection
					}
					else
					{
						FNotificationInfo Info(LOCTEXT("ForceCPUFailed",
							"Could not force CPU access. The texture may need to be reimported from source."));
						Info.ExpireDuration = 5.0f;
						FSlateNotificationManager::Get().AddNotification(Info);
						return FReply::Handled();
					}
				}
				else
				{
					return FReply::Handled();
				}
			}
			else
			{
				// Settings are wrong — point user to banner
				FNotificationInfo Info(LOCTEXT("ApplySettingsFirst",
					"Apply Paper2D texture settings first (see banner above)."));
				Info.ExpireDuration = 5.0f;
				FSlateNotificationManager::Get().AddNotification(Info);
				return FReply::Handled();
			}
		}
	}

	DetectedSprites.Empty();
	if (Canvas.IsValid())
	{
		Canvas->ClearMergeSelection();
		Canvas->ExitEditMode(false);
	}

	if (DetectionMode == ESpriteDetectionMode::Island)
	{
		DetectIslands();
	}
	else
	{
		DetectGrid();
	}

	// Copy sprites to canvas
	Canvas->SetDetectedSprites(DetectedSprites);

	RefreshSpriteList();
	UpdateStatusTexts();

	UE_LOG(LogTemp, Log, TEXT("SpriteExtractor: Detected %d sprites"), DetectedSprites.Num());

	return FReply::Handled();
}

FReply SSpriteExtractorWindow::OnExtractSpritesClicked()
{
	// Returns positive count on full success, negative on cancellation, 0 on nothing
	int32 Result = ExtractSprites();
	int32 Count = FMath::Abs(Result);
	bool bFullSuccess = Result > 0;

	if (Count > 0)
	{
		FNotificationInfo Info(FText::Format(
			LOCTEXT("ExtractionSuccess", "Successfully extracted {0} sprites to {1}"),
			FText::AsNumber(Count), FText::FromString(GetOutputFolderName())));
		Info.bFireAndForget = true;
		Info.ExpireDuration = 5.0f;
		Info.bUseSuccessFailIcons = true;
		TSharedPtr<SNotificationItem> Notification = FSlateNotificationManager::Get().AddNotification(Info);
		if (Notification.IsValid())
		{
			Notification->SetCompletionState(SNotificationItem::CS_Success);
		}

		// Only close the window on full (non-cancelled) extraction
		if (bFullSuccess)
		{
			TSharedPtr<SWindow> ParentWindow = FSlateApplication::Get().FindWidgetWindow(AsShared());
			if (ParentWindow.IsValid())
			{
				ParentWindow->RequestDestroyWindow();
			}
		}
	}
	return FReply::Handled();
}

void SSpriteExtractorWindow::DetectIslands()
{
	TArray<FColor> Pixels;
	int32 Width, Height;
	if (!FSpriteExtractionUtils::LoadTextureData(SourceTexture, Pixels, Width, Height))
	{
		FNotificationInfo Info(LOCTEXT("TextureLoadFailed",
			"Failed to load texture data. Ensure texture has CPU access enabled (Compression Settings)."));
		Info.ExpireDuration = 8.0f;
		Info.bFireAndForget = true;
		Info.bUseSuccessFailIcons = true;
		TSharedPtr<SNotificationItem> Notification = FSlateNotificationManager::Get().AddNotification(Info);
		if (Notification.IsValid())
		{
			Notification->SetCompletionState(SNotificationItem::CS_Fail);
		}
		return;
	}

	TArray<bool> Visited;
	Visited.SetNumZeroed(Width * Height);

	int32 Index = 0;
	for (int32 Y = 0; Y < Height; Y++)
	{
		for (int32 X = 0; X < Width; X++)
		{
			if (!Visited[Y * Width + X] && IsPixelOpaque(Pixels, Width, X, Y))
			{
				FIntRect Bounds(X, Y, X, Y);
				FloodFillMark(Visited, Pixels, Width, Height, X, Y, Bounds);

				// Check minimum size
				if (Bounds.Width() >= MinSpriteSize && Bounds.Height() >= MinSpriteSize)
				{
					FDetectedSprite Sprite;
					Sprite.Bounds = Bounds;
					Sprite.OriginalBounds = Bounds;
					Sprite.bSelected = true;
					Sprite.Index = Index++;
					DetectedSprites.Add(Sprite);
				}
			}
		}
	}

	// Merge nearby islands
	MergeNearbyIslands();

	// Sort strictly left-to-right so sprite indices match horizontal order.
	// This avoids row grouping based on sprite height/top-edge variance.
	DetectedSprites.Sort([](const FDetectedSprite& A, const FDetectedSprite& B)
	{
		if (A.Bounds.Min.X != B.Bounds.Min.X)
		{
			return A.Bounds.Min.X < B.Bounds.Min.X;
		}

		return A.Bounds.Min.Y < B.Bounds.Min.Y;
	});

	// Re-index after sorting
	for (int32 i = 0; i < DetectedSprites.Num(); i++)
	{
		DetectedSprites[i].Index = i;
	}
}

void SSpriteExtractorWindow::DetectGrid()
{
	if (!SourceTexture || GridColumns < 1 || GridRows < 1) return;

	int32 Width = SourceTexture->GetSizeX();
	int32 Height = SourceTexture->GetSizeY();
	int32 CellWidth = Width / GridColumns;
	int32 CellHeight = Height / GridRows;

	int32 Index = 0;
	for (int32 Row = 0; Row < GridRows; Row++)
	{
		for (int32 Col = 0; Col < GridColumns; Col++)
		{
			FDetectedSprite Sprite;
			Sprite.Bounds = FIntRect(
				Col * CellWidth,
				Row * CellHeight,
				(Col + 1) * CellWidth,
				(Row + 1) * CellHeight
			);
			Sprite.OriginalBounds = Sprite.Bounds;
			Sprite.bSelected = true;
			Sprite.Index = Index++;
			DetectedSprites.Add(Sprite);
		}
	}
}

int32 SSpriteExtractorWindow::ExtractSprites()
{
	if (!SourceTexture || !Canvas.IsValid()) return 0;

	// Collect selected sprites from canvas (which has the current selection state)
	TArray<FDetectedSprite> SelectedSprites;
	TArray<FDetectedSprite>& CanvasSprites = Canvas->GetDetectedSprites();
	for (const FDetectedSprite& Sprite : CanvasSprites)
	{
		if (Sprite.bSelected)
		{
			SelectedSprites.Add(Sprite);
		}
	}

	if (SelectedSprites.Num() == 0)
	{
		FNotificationInfo Info(LOCTEXT("NoSpritesSelected", "No sprites selected for extraction."));
		Info.ExpireDuration = 4.0f;
		Info.bFireAndForget = true;
		FSlateNotificationManager::Get().AddNotification(Info);
		return 0;
	}

	// Resolve output path using naming system
	FString ResolvedOutputPath = OutputPath / GetOutputFolderName();

	// Move source texture into the output folder with _Texture suffix to avoid name collisions
	FString TextureName = SourceTexture->GetName();
	if (!TextureName.EndsWith(TEXT("_Texture")))
	{
		TextureName += TEXT("_Texture");
	}
	FString DestPackageName = ResolvedOutputPath / TextureName;
	if (!FPackageName::DoesPackageExist(DestPackageName))
	{
		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
		TArray<FAssetRenameData> RenameData;
		RenameData.Emplace(SourceTexture, ResolvedOutputPath, TextureName);
		AssetTools.RenameAssets(RenameData);
	}

	FScopedSlowTask Progress(SelectedSprites.Num(), LOCTEXT("ExtractingSprites", "Extracting sprites..."));
	Progress.MakeDialog(true);

	TArray<UPaperSprite*> CreatedSprites;
	bool bCancelled = false;

	for (int32 i = 0; i < SelectedSprites.Num(); i++)
	{
		if (Progress.ShouldCancel())
		{
			bCancelled = true;
			break;
		}

		Progress.EnterProgressFrame(1, FText::Format(
			LOCTEXT("CreatingSprite", "Creating sprite {0}/{1}"),
			FText::AsNumber(i + 1), FText::AsNumber(SelectedSprites.Num())));

		const FDetectedSprite& Sprite = SelectedSprites[i];
		FString SpriteName = GetSpriteName(i);
		UPaperSprite* NewSprite = FSpriteExtractionUtils::CreateSpriteFromBounds(SourceTexture, Sprite.Bounds, SpriteName, ResolvedOutputPath);
		if (NewSprite)
		{
			CreatedSprites.Add(NewSprite);
		}
	}

	// Create flipbook if requested
	UPaperFlipbook* Flipbook = nullptr;
	if (bCreateFlipbook && CreatedSprites.Num() > 0)
	{
		Flipbook = CreateFlipbook(CreatedSprites);
	}

	// Add to character asset if requested
	if (bAddToCharacterAsset && TargetCharacterAsset && Flipbook)
	{
		FScopedTransaction Transaction(LOCTEXT("AddAnimation", "Add Animation to Character Asset"));
		TargetCharacterAsset->Modify();

		FAnimationHitboxData NewAnimation;
		NewAnimation.AnimationName = AnimationName;
		NewAnimation.Flipbook = Flipbook;
		NewAnimation.SourceTexture = SourceTexture;
		NewAnimation.SpritesOutputPath = ResolvedOutputPath;

		// Create frame data for each sprite
		for (int32 i = 0; i < SelectedSprites.Num(); i++)
		{
			FFrameHitboxData FrameData;
			FrameData.FrameName = FString::Printf(TEXT("%s_%02d"), *AnimationName, i);
			NewAnimation.Frames.Add(FrameData);

			// Store extraction info
			FSpriteExtractionInfo ExtractionInfo;
			ExtractionInfo.SourceOffset = SelectedSprites[i].OriginalBounds.Min;
			ExtractionInfo.AlphaThreshold = AlphaThreshold;
			ExtractionInfo.ExtractionTime = FDateTime::Now();
			NewAnimation.FrameExtractionInfo.Add(ExtractionInfo);
		}

		TargetCharacterAsset->Animations.Add(NewAnimation);
	}

	// Show completion notification
	if (CreatedSprites.Num() > 0)
	{
		FString NotifText = bCancelled
			? FString::Printf(TEXT("Extraction cancelled. %d/%d sprites created."), CreatedSprites.Num(), SelectedSprites.Num())
			: FString::Printf(TEXT("Extracted %d sprites to %s"), CreatedSprites.Num(), *ResolvedOutputPath);

		FNotificationInfo Info(FText::FromString(NotifText));
		Info.bFireAndForget = true;
		Info.ExpireDuration = 8.0f;
		FString CapturedOutputPath = ResolvedOutputPath;
		Info.Hyperlink = FSimpleDelegate::CreateLambda([CapturedOutputPath]()
		{
			FString DiskPath;
			FPackageName::TryConvertLongPackageNameToFilename(CapturedOutputPath, DiskPath);
			FPlatformProcess::ExploreFolder(*DiskPath);
		});
		Info.HyperlinkText = LOCTEXT("OpenOutputFolder", "Open Output Folder");
		FSlateNotificationManager::Get().AddNotification(Info);
	}

	return bCancelled ? -CreatedSprites.Num() : CreatedSprites.Num();
}

UPaperFlipbook* SSpriteExtractorWindow::CreateFlipbook(const TArray<UPaperSprite*>& Sprites)
{
	if (Sprites.Num() == 0) return nullptr;

	FString ResolvedOutputPath = OutputPath / GetOutputFolderName();
	FString ResolvedFlipbookName = GetFlipbookName();
	FString PackageName = ResolvedOutputPath / ResolvedFlipbookName;

	UPackage* Package = CreatePackage(*PackageName);
	if (!Package) return nullptr;

	// Check for existing object to avoid fatal crash on name collision
	UPaperFlipbook* Flipbook = FindObject<UPaperFlipbook>(Package, *ResolvedFlipbookName);
	if (!Flipbook)
	{
		UObject* Existing = StaticFindObject(UObject::StaticClass(), Package, *ResolvedFlipbookName);
		if (Existing)
		{
			Existing->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_ForceNoResetLoaders);
		}
		Flipbook = NewObject<UPaperFlipbook>(Package, *ResolvedFlipbookName, RF_Public | RF_Standalone);
	}
	if (!Flipbook) return nullptr;

	// Use FScopedFlipbookMutator to modify the flipbook's protected members
	{
		FScopedFlipbookMutator Mutator(Flipbook);
		Mutator.FramesPerSecond = FlipbookFrameRate;
		Mutator.KeyFrames.Empty();

		for (UPaperSprite* Sprite : Sprites)
		{
			FPaperFlipbookKeyFrame KeyFrame;
			KeyFrame.Sprite = Sprite;
			KeyFrame.FrameRun = 1;
			Mutator.KeyFrames.Add(KeyFrame);
		}
	}

	// Mark package dirty and register
	Package->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(Flipbook);

	UE_LOG(LogTemp, Log, TEXT("SpriteExtractor: Created flipbook '%s' with %d frames at %.1f FPS"),
		*ResolvedFlipbookName, Sprites.Num(), FlipbookFrameRate);

	return Flipbook;
}

void SSpriteExtractorWindow::RefreshSpriteList()
{
	if (!SpriteListBox.IsValid() || !Canvas.IsValid()) return;

	SpriteListBox->ClearChildren();
	SpriteListRows.Empty();

	TArray<FDetectedSprite>& Sprites = Canvas->GetDetectedSprites();
	for (int32 i = 0; i < Sprites.Num(); i++)
	{
		const FDetectedSprite& Sprite = Sprites[i];

		TSharedPtr<SBorder> RowWidget;

		SpriteListBox->AddSlot()
		.AutoHeight()
		.Padding(2)
		[
			SAssignNew(RowWidget, SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
			.Padding(4)
			.BorderBackgroundColor(Sprite.bSelected ? FLinearColor(0.1f, 0.3f, 0.1f, 1.0f) : FLinearColor(0.15f, 0.15f, 0.15f, 1.0f))
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(2)
				[
					SNew(SCheckBox)
					.IsChecked(Sprite.bSelected ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
					.OnCheckStateChanged_Lambda([this, i](ECheckBoxState State)
					{
						Canvas->ToggleSpriteSelection(i);
						RefreshSpriteList();
						UpdateStatusTexts();
					})
				]

				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				.Padding(4, 0)
				[
					SNew(SVerticalBox)

					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(STextBlock)
						.Text(FText::Format(LOCTEXT("SpriteIndex", "#{0}  {1}x{2}"),
							FText::AsNumber(Sprite.Index),
							FText::AsNumber(Sprite.Bounds.Width()),
							FText::AsNumber(Sprite.Bounds.Height())))
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(STextBlock)
						.Text(FText::Format(LOCTEXT("SpritePos", "@ ({0}, {1})"),
							FText::AsNumber(Sprite.Bounds.Min.X),
							FText::AsNumber(Sprite.Bounds.Min.Y)))
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
						.ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f)))
					]
				]
			]
		];

		SpriteListRows.Add(RowWidget);
	}
}

void SSpriteExtractorWindow::OnCanvasSpriteSelectionToggled(int32 SpriteIndex)
{
	RefreshSpriteList();
	UpdateStatusTexts();

	// Scroll the toggled sprite's row into view (deferred via active timer to let layout settle)
	const int32 TargetIndex = SpriteIndex;
	RegisterActiveTimer(0.0f, FWidgetActiveTimerDelegate::CreateLambda(
		[this, TargetIndex](double, float) -> EActiveTimerReturnType
		{
			if (SpriteListScrollBox.IsValid() && SpriteListRows.IsValidIndex(TargetIndex))
			{
				SpriteListScrollBox->ScrollDescendantIntoView(SpriteListRows[TargetIndex]);
			}
			return EActiveTimerReturnType::Stop;
		}
	));
}

void SSpriteExtractorWindow::RefreshCanvas()
{
	if (Canvas.IsValid())
	{
		Canvas->SetDetectedSprites(DetectedSprites);
	}
}

bool SSpriteExtractorWindow::IsPixelOpaque(const TArray<FColor>& Pixels, int32 Width, int32 X, int32 Y) const
{
	int32 Index = Y * Width + X;
	if (Index < 0 || Index >= Pixels.Num()) return false;
	return Pixels[Index].A >= AlphaThreshold;  // Use >= for better anti-aliased edge detection
}

void SSpriteExtractorWindow::FloodFillMark(TArray<bool>& Visited, const TArray<FColor>& Pixels, int32 Width, int32 Height, int32 StartX, int32 StartY, FIntRect& OutBounds) const
{
	TArray<FIntPoint> Stack;
	Stack.Push(FIntPoint(StartX, StartY));

	// Direction offsets: 4-directional (orthogonal) + 4 diagonal = 8-directional
	static const int32 DX4[] = { -1, 1, 0, 0 };
	static const int32 DY4[] = { 0, 0, -1, 1 };
	static const int32 DX8[] = { -1, 1, 0, 0, -1, -1, 1, 1 };
	static const int32 DY8[] = { 0, 0, -1, 1, -1, 1, -1, 1 };

	const int32* DX = bUse8DirectionalFloodFill ? DX8 : DX4;
	const int32* DY = bUse8DirectionalFloodFill ? DY8 : DY4;
	const int32 NumDirections = bUse8DirectionalFloodFill ? 8 : 4;

	while (Stack.Num() > 0)
	{
		FIntPoint Pos = Stack.Pop();
		int32 X = Pos.X;
		int32 Y = Pos.Y;

		if (X < 0 || X >= Width || Y < 0 || Y >= Height) continue;

		int32 Index = Y * Width + X;
		if (Visited[Index]) continue;
		if (!IsPixelOpaque(Pixels, Width, X, Y)) continue;

		Visited[Index] = true;

		// Expand bounds
		OutBounds.Min.X = FMath::Min(OutBounds.Min.X, X);
		OutBounds.Min.Y = FMath::Min(OutBounds.Min.Y, Y);
		OutBounds.Max.X = FMath::Max(OutBounds.Max.X, X + 1);
		OutBounds.Max.Y = FMath::Max(OutBounds.Max.Y, Y + 1);

		// Add neighbors (4 or 8 directions based on setting)
		for (int32 i = 0; i < NumDirections; i++)
		{
			Stack.Push(FIntPoint(X + DX[i], Y + DY[i]));
		}
	}
}

void SSpriteExtractorWindow::MergeNearbyIslands()
{
	if (IslandMergeDistance <= 0) return;

	// Merge islands whose expanded bounds overlap
	bool bMerged = true;
	while (bMerged)
	{
		bMerged = false;
		for (int32 i = 0; i < DetectedSprites.Num(); i++)
		{
			FIntRect ExpandedI = DetectedSprites[i].OriginalBounds;
			ExpandedI.Min.X -= IslandMergeDistance;
			ExpandedI.Min.Y -= IslandMergeDistance;
			ExpandedI.Max.X += IslandMergeDistance;
			ExpandedI.Max.Y += IslandMergeDistance;

			for (int32 j = i + 1; j < DetectedSprites.Num(); j++)
			{
				const FIntRect& BoundsJ = DetectedSprites[j].OriginalBounds;

				// Check if expanded bounds of i intersect with j
				bool bIntersects = !(ExpandedI.Max.X <= BoundsJ.Min.X ||
									 ExpandedI.Min.X >= BoundsJ.Max.X ||
									 ExpandedI.Max.Y <= BoundsJ.Min.Y ||
									 ExpandedI.Min.Y >= BoundsJ.Max.Y);

				if (bIntersects)
				{
					// Merge j into i
					DetectedSprites[i].OriginalBounds.Min.X = FMath::Min(DetectedSprites[i].OriginalBounds.Min.X, BoundsJ.Min.X);
					DetectedSprites[i].OriginalBounds.Min.Y = FMath::Min(DetectedSprites[i].OriginalBounds.Min.Y, BoundsJ.Min.Y);
					DetectedSprites[i].OriginalBounds.Max.X = FMath::Max(DetectedSprites[i].OriginalBounds.Max.X, BoundsJ.Max.X);
					DetectedSprites[i].OriginalBounds.Max.Y = FMath::Max(DetectedSprites[i].OriginalBounds.Max.Y, BoundsJ.Max.Y);
					DetectedSprites[i].Bounds = DetectedSprites[i].OriginalBounds;

					DetectedSprites.RemoveAt(j);
					bMerged = true;
					break;
				}
			}
			if (bMerged) break;
		}
	}

	// Re-index after merging
	for (int32 i = 0; i < DetectedSprites.Num(); i++)
	{
		DetectedSprites[i].Index = i;
	}
}

// ============================================
// Undo/Redo
// ============================================

void SSpriteExtractorWindow::PushUndoState()
{
	FExtractorStateSnapshot Snapshot;
	if (Canvas.IsValid())
	{
		Snapshot.Sprites = Canvas->GetDetectedSprites();
	}

	UndoStack.Add(Snapshot);
	if (UndoStack.Num() > MaxUndoHistory)
	{
		UndoStack.RemoveAt(0);
	}

	// Clear redo stack on new action
	RedoStack.Empty();
}

void SSpriteExtractorWindow::Undo()
{
	if (UndoStack.Num() == 0) return;

	// Save current state to redo
	FExtractorStateSnapshot Current;
	if (Canvas.IsValid())
	{
		Current.Sprites = Canvas->GetDetectedSprites();
	}
	RedoStack.Add(Current);

	// Restore previous state
	FExtractorStateSnapshot Previous = UndoStack.Pop();
	RestoreState(Previous);
}

void SSpriteExtractorWindow::Redo()
{
	if (RedoStack.Num() == 0) return;

	// Save current state to undo
	FExtractorStateSnapshot Current;
	if (Canvas.IsValid())
	{
		Current.Sprites = Canvas->GetDetectedSprites();
	}
	UndoStack.Add(Current);

	// Restore next state
	FExtractorStateSnapshot Next = RedoStack.Pop();
	RestoreState(Next);
}

void SSpriteExtractorWindow::RestoreState(const FExtractorStateSnapshot& State)
{
	if (Canvas.IsValid())
	{
		Canvas->ExitEditMode(false);
		Canvas->ClearMergeSelection();
		Canvas->SetDetectedSprites(State.Sprites);
	}

	DetectedSprites = State.Sprites;
	RefreshSpriteList();
	UpdateStatusTexts();
}

// ============================================
// Merge & Absorb
// ============================================

void SSpriteExtractorWindow::MergeSelectedSprites(const TArray<int32>& IndicesToMerge)
{
	if (IndicesToMerge.Num() < 2 || !Canvas.IsValid()) return;

	PushUndoState();

	TArray<FDetectedSprite>& Sprites = Canvas->GetDetectedSprites();

	// Compute merged bounds from all selected indices
	FIntRect MergedBounds = Sprites[IndicesToMerge[0]].OriginalBounds;
	for (int32 i = 1; i < IndicesToMerge.Num(); i++)
	{
		const FIntRect& Other = Sprites[IndicesToMerge[i]].OriginalBounds;
		MergedBounds.Min.X = FMath::Min(MergedBounds.Min.X, Other.Min.X);
		MergedBounds.Min.Y = FMath::Min(MergedBounds.Min.Y, Other.Min.Y);
		MergedBounds.Max.X = FMath::Max(MergedBounds.Max.X, Other.Max.X);
		MergedBounds.Max.Y = FMath::Max(MergedBounds.Max.Y, Other.Max.Y);
	}

	// Remove all merged sprites (reverse order for safe removal)
	TArray<int32> SortedIndices = IndicesToMerge;
	SortedIndices.Sort([](const int32& A, const int32& B) { return A > B; });
	for (int32 Index : SortedIndices)
	{
		if (Sprites.IsValidIndex(Index))
		{
			Sprites.RemoveAt(Index);
		}
	}

	// Create new merged sprite
	FDetectedSprite MergedSprite;
	MergedSprite.OriginalBounds = MergedBounds;
	MergedSprite.Bounds = MergedBounds;
	MergedSprite.bSelected = true;
	Sprites.Add(MergedSprite);

	// Absorb any sprites fully contained in the merged bounds
	MergedSprite.Bounds = AbsorbContainedSprites(MergedBounds, Sprites, Sprites.Num() - 1);
	Sprites.Last().Bounds = MergedSprite.Bounds;
	Sprites.Last().OriginalBounds = MergedSprite.Bounds;

	// Re-index and sort — preserve row-major ordering for grid mode, left-to-right for island mode.
	if (DetectionMode == ESpriteDetectionMode::Grid)
	{
		Sprites.Sort([](const FDetectedSprite& A, const FDetectedSprite& B)
		{
			if (A.Bounds.Min.Y != B.Bounds.Min.Y)
			{
				return A.Bounds.Min.Y < B.Bounds.Min.Y;
			}
			return A.Bounds.Min.X < B.Bounds.Min.X;
		});
	}
	else
	{
		Sprites.Sort([](const FDetectedSprite& A, const FDetectedSprite& B)
		{
			if (A.Bounds.Min.X != B.Bounds.Min.X)
			{
				return A.Bounds.Min.X < B.Bounds.Min.X;
			}
			return A.Bounds.Min.Y < B.Bounds.Min.Y;
		});
	}
	for (int32 i = 0; i < Sprites.Num(); i++)
	{
		Sprites[i].Index = i;
	}

	Canvas->ClearMergeSelection();
	DetectedSprites = Sprites;
	RefreshSpriteList();
	UpdateStatusTexts();
}

FIntRect SSpriteExtractorWindow::AbsorbContainedSprites(FIntRect Bounds, TArray<FDetectedSprite>& Sprites, int32 SkipIndex)
{
	constexpr int32 MaxIterations = 100;
	for (int32 Iteration = 0; Iteration < MaxIterations; Iteration++)
	{
		bool bAbsorbed = false;
		for (int32 i = Sprites.Num() - 1; i >= 0; i--)
		{
			if (i == SkipIndex) continue;

			const FIntRect& Other = Sprites[i].OriginalBounds;
			// Check if Other is fully contained in Bounds
			if (Other.Min.X >= Bounds.Min.X && Other.Min.Y >= Bounds.Min.Y &&
				Other.Max.X <= Bounds.Max.X && Other.Max.Y <= Bounds.Max.Y)
			{
				Sprites.RemoveAt(i);
				if (i < SkipIndex)
				{
					SkipIndex--;
				}
				bAbsorbed = true;
			}
		}

		if (!bAbsorbed) break;
	}
	return Bounds;
}

// ============================================
// Auto-Update Detection
// ============================================

void SSpriteExtractorWindow::ScheduleAutoDetect()
{
	if (!bAutoUpdateDetection) return;

	// Don't auto-detect during edit mode
	if (Canvas.IsValid() && Canvas->IsInEditMode()) return;

	// Cancel existing timer if active
	TSharedPtr<FActiveTimerHandle> ExistingTimer = ActiveDebounceTimerHandle.Pin();
	if (ExistingTimer.IsValid())
	{
		UnRegisterActiveTimer(ExistingTimer.ToSharedRef());
	}

	// Schedule new debounced detection
	ActiveDebounceTimerHandle = RegisterActiveTimer(AutoDetectDebounceSeconds,
		FWidgetActiveTimerDelegate::CreateSP(this, &SSpriteExtractorWindow::OnAutoDetectTimer));
}

EActiveTimerReturnType SSpriteExtractorWindow::OnAutoDetectTimer(double InCurrentTime, float InDeltaTime)
{
	if (SourceTexture)
	{
		OnDetectSpritesClicked();
	}
	return EActiveTimerReturnType::Stop;
}

// ============================================
// Naming System
// ============================================

void SSpriteExtractorWindow::AutoDetectNameParts(const FString& TextureName)
{
	// Find all separator positions (underscore and hyphen)
	SeparatorPositions.Empty();
	for (int32 i = 0; i < TextureName.Len(); i++)
	{
		TCHAR Ch = TextureName[i];
		if (Ch == TEXT('_') || Ch == TEXT('-'))
		{
			SeparatorPositions.Add(i);
		}
	}

	// Build split options for SComboBox
	SplitOptions.Empty();
	SplitOptions.Add(MakeShared<int32>(-1)); // "No split" option
	for (int32 i = 0; i < SeparatorPositions.Num(); i++)
	{
		SplitOptions.Add(MakeShared<int32>(i));
	}

	if (SeparatorPositions.Num() > 0)
	{
		// Default to last separator (most common: "CharName_AnimName")
		SplitIndex = SeparatorPositions.Num() - 1;

		int32 SepPos = SeparatorPositions[SplitIndex];
		NamePrefix = TextureName.Left(SepPos);
		NameSeparator = TextureName.Mid(SepPos, 1);
		NameBase = TextureName.Mid(SepPos + 1);
	}
	else
	{
		// No separators found — entire name is the base
		SplitIndex = -1;
		NamePrefix.Empty();
		NameSeparator = TEXT("_");
		NameBase = TextureName;
	}

	// Auto-populate animation name with the base name (without prefix)
	AnimationName = NameBase;

	// Refresh the split combo box if it exists
	if (SplitComboBox.IsValid())
	{
		SplitComboBox->RefreshOptions();

		// Restore selection by value — RefreshOptions invalidates old pointers
		for (const auto& Option : SplitOptions)
		{
			if (*Option == SplitIndex)
			{
				SplitComboBox->SetSelectedItem(Option);
				break;
			}
		}
	}
}

FString SSpriteExtractorWindow::GetSpriteName(int32 Index) const
{
	FString FullName;
	if (!NamePrefix.IsEmpty())
	{
		FullName = NamePrefix + NameSeparator;
	}
	FullName += NameBase + FString::Printf(TEXT("_%02d"), Index);
	return FullName;
}

FString SSpriteExtractorWindow::GetFlipbookName() const
{
	FString FullName;
	if (!NamePrefix.IsEmpty())
	{
		FullName = NamePrefix + NameSeparator;
	}
	FullName += NameBase;
	return FullName;
}

FString SSpriteExtractorWindow::GetOutputFolderName() const
{
	FString FullName;
	if (!NamePrefix.IsEmpty())
	{
		FullName = NamePrefix + NameSeparator;
	}
	FullName += NameBase;
	return FullName;
}

void SSpriteExtractorWindow::UpdateOutputPath()
{
	if (!SourceTexture) return;
	FString TexturePath = FPackageName::GetLongPackagePath(SourceTexturePath);
	OutputPath = TexturePath;
}

// ============================================
// FSpriteExtractorActions Implementation
// ============================================

void FSpriteExtractorActions::RegisterMenus()
{
	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateLambda([]()
	{
		// Extend texture context menu
		UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("ContentBrowser.AssetContextMenu.Texture2D");
		if (Menu)
		{
			FToolMenuSection& Section = Menu->FindOrAddSection("GetAssetActions");
			Section.AddMenuEntry(
				"ExtractSprites",
				LOCTEXT("ExtractSprites", "Extract Sprites (Paper2D+)"),
				LOCTEXT("ExtractSpritesTooltip", "Open sprite extractor for this texture"),
				FSlateIcon(FAppStyle::GetAppStyleSetName(), "ClassIcon.PaperSprite"),
				FUIAction(FExecuteAction::CreateLambda([]()
				{
					// Get selected textures from content browser
					FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
					TArray<FAssetData> SelectedAssets;
					ContentBrowserModule.Get().GetSelectedAssets(SelectedAssets);

					for (const FAssetData& Asset : SelectedAssets)
					{
						if (UTexture2D* Texture = Cast<UTexture2D>(Asset.GetAsset()))
						{
							OpenSpriteExtractorForTexture(Texture);
							break;
						}
					}
				}))
			);
		}

		// Add to Tools menu
		UToolMenu* ToolsMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Tools");
		if (ToolsMenu)
		{
			FToolMenuSection& Section = ToolsMenu->FindOrAddSection("Paper2DPlus");
			Section.AddMenuEntry(
				"OpenSpriteExtractor",
				LOCTEXT("OpenSpriteExtractor", "Sprite Extractor"),
				LOCTEXT("OpenSpriteExtractorTooltip", "Open the Paper2D+ Sprite Extractor"),
				FSlateIcon(FAppStyle::GetAppStyleSetName(), "ClassIcon.PaperSprite"),
				FUIAction(FExecuteAction::CreateStatic(&FSpriteExtractorActions::OpenSpriteExtractor))
			);
		}
	}));
}

void FSpriteExtractorActions::UnregisterMenus()
{
	// Menus are automatically cleaned up when the module shuts down
}

void FSpriteExtractorActions::OpenSpriteExtractor()
{
	TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(LOCTEXT("SpriteExtractorTitle", "Paper2D+ Sprite Extractor"))
		.ClientSize(FVector2D(1400, 800))
		.SupportsMinimize(true)
		.SupportsMaximize(true);

	Window->SetContent(
		SNew(SSpriteExtractorWindow)
	);

	FSlateApplication::Get().AddWindow(Window);
}

void FSpriteExtractorActions::OpenSpriteExtractorForTexture(UTexture2D* Texture)
{
	TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(LOCTEXT("SpriteExtractorTitle", "Paper2D+ Sprite Extractor"))
		.ClientSize(FVector2D(1400, 800))
		.SupportsMinimize(true)
		.SupportsMaximize(true);

	TSharedRef<SSpriteExtractorWindow> ExtractorWidget = SNew(SSpriteExtractorWindow);

	// Set the initial texture
	if (Texture)
	{
		ExtractorWidget->SetInitialTexture(Texture);
	}

	Window->SetContent(ExtractorWidget);

	FSlateApplication::Get().AddWindow(Window);
}

#undef LOCTEXT_NAMESPACE
