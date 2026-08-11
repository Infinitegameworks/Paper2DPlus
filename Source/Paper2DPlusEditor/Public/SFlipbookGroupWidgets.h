// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SLeafWidget.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/AppStyle.h"
#include "CharacterProfileAssetEditor.h"
#include "EditorCanvasUtils.h"

/**
 * Small helper widgets used by the Flipbook Groups tab of the Character Profile Asset Editor.
 * Both are header-only — extracted from SCharacterProfileAssetEditor_FlipbookGroups.cpp.
 * (SPhaseSlotDropTarget went with the phase-groups feature, legacy-cleanup 2026-07.)
 */

// ==========================================
// GROUP DRAG HANDLE
// ==========================================

/** Drag handle for flipbook groups — 3x2 grip dots + color indicator, initiates FGroupDragDropOp. */
class SGroupDragHandle : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SGroupDragHandle) {}
		SLATE_ARGUMENT(FLinearColor, GroupColor)
	SLATE_END_ARGS()

	FName GroupName;

	void Construct(const FArguments& InArgs)
	{
		GroupColor = InArgs._GroupColor;
	}

	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override
	{
		return FVector2D(24.0f, 20.0f);
	}

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override
	{
		const FSlateBrush* WhiteBrush = FAppStyle::Get().GetBrush("WhiteBrush");
		const FLinearColor DotColor(0.45f, 0.45f, 0.45f, bIsHovered ? 0.9f : 0.55f);
		const float DotSize = 2.5f;
		const float ColSpacing = 5.0f;
		const float RowSpacing = 4.0f;

		// Draw 3 rows x 2 cols of grip dots (left side)
		const float GripStartX = 2.0f;
		const float GripStartY = (AllottedGeometry.GetLocalSize().Y - (2 * RowSpacing + DotSize)) * 0.5f;

		for (int32 Row = 0; Row < 3; ++Row)
		{
			for (int32 Col = 0; Col < 2; ++Col)
			{
				FVector2D Pos(GripStartX + Col * ColSpacing, GripStartY + Row * RowSpacing);
				FSlateDrawElement::MakeBox(OutDrawElements, LayerId,
					MakePaintGeometry(AllottedGeometry, FVector2D(DotSize, DotSize), FSlateLayoutTransform(Pos)),
					WhiteBrush, ESlateDrawEffect::None, DotColor);
			}
		}

		// Draw color dot (right side)
		const float ColorDotSize = 10.0f;
		const float ColorDotX = 14.0f;
		const float ColorDotY = (AllottedGeometry.GetLocalSize().Y - ColorDotSize) * 0.5f;
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId,
			MakePaintGeometry(AllottedGeometry, FVector2D(ColorDotSize, ColorDotSize), FSlateLayoutTransform(FVector2D(ColorDotX, ColorDotY))),
			WhiteBrush, ESlateDrawEffect::None, GroupColor);

		return LayerId;
	}

	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
		{
			bPotentialDrag = true;
			DragStartPos = MouseEvent.GetScreenSpacePosition();
			return FReply::Handled().CaptureMouse(SharedThis(this));
		}
		return FReply::Unhandled();
	}

	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		if (bPotentialDrag && MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
		{
			bPotentialDrag = false;
			return FReply::Handled().ReleaseMouseCapture();
		}
		return FReply::Unhandled();
	}

	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		if (bPotentialDrag)
		{
			float Distance = FVector2D::Distance(MouseEvent.GetScreenSpacePosition(), DragStartPos);
			if (Distance > 5.0f)
			{
				bPotentialDrag = false;
				return FReply::Handled()
					.ReleaseMouseCapture()
					.BeginDragDrop(FGroupDragDropOp::New(GroupName));
			}
		}
		return FReply::Unhandled();
	}

	virtual void OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		bIsHovered = true;
		Invalidate(EInvalidateWidgetReason::Paint);
	}

	virtual void OnMouseLeave(const FPointerEvent& MouseEvent) override
	{
		bIsHovered = false;
		Invalidate(EInvalidateWidgetReason::Paint);
	}

	virtual void OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent) override
	{
		bPotentialDrag = false;
	}

	virtual FCursorReply OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const override
	{
		return FCursorReply::Cursor(EMouseCursor::GrabHand);
	}

private:
	FLinearColor GroupColor;
	bool bPotentialDrag = false;
	bool bIsHovered = false;
	FVector2D DragStartPos;
};

// ==========================================
// FLIPBOOK GROUP DROP TARGET
// ==========================================

/** Drop target wrapper for flipbook groups — accepts both FFlipbookGroupDragDropOp and FGroupDragDropOp. */
class SFlipbookGroupDropTarget : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SFlipbookGroupDropTarget) {}
		SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_END_ARGS()

	FName TargetGroup;
	TFunction<void(const TArray<int32>&, FName)> OnDropFunc;  // (FlipbookIndices, TargetGroup)
	TFunction<void(FName, FName)> OnGroupDropFunc;  // (SourceGroupName, TargetParentGroup)

	void Construct(const FArguments& InArgs)
	{
		ChildSlot[ InArgs._Content.Widget ];
	}

	virtual void OnDragEnter(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override
	{
		if (DragDropEvent.GetOperationAs<FFlipbookGroupDragDropOp>().IsValid()
			|| DragDropEvent.GetOperationAs<FGroupDragDropOp>().IsValid())
		{
			bDragOver = true;
			Invalidate(EInvalidateWidgetReason::Paint);
		}
	}

	virtual void OnDragLeave(const FDragDropEvent& DragDropEvent) override
	{
		bDragOver = false;
		Invalidate(EInvalidateWidgetReason::Paint);
	}

	virtual FReply OnDragOver(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override
	{
		if (DragDropEvent.GetOperationAs<FFlipbookGroupDragDropOp>().IsValid()
			|| DragDropEvent.GetOperationAs<FGroupDragDropOp>().IsValid())
		{
			return FReply::Handled();
		}
		return FReply::Unhandled();
	}

	virtual FReply OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override
	{
		bDragOver = false;
		Invalidate(EInvalidateWidgetReason::Paint);

		// Flipbook card drop
		TSharedPtr<FFlipbookGroupDragDropOp> CardOp = DragDropEvent.GetOperationAs<FFlipbookGroupDragDropOp>();
		if (CardOp.IsValid() && OnDropFunc)
		{
			OnDropFunc(CardOp->FlipbookIndices, TargetGroup);
			return FReply::Handled();
		}

		// Group reparent drop
		TSharedPtr<FGroupDragDropOp> GroupOp = DragDropEvent.GetOperationAs<FGroupDragDropOp>();
		if (GroupOp.IsValid() && OnGroupDropFunc)
		{
			OnGroupDropFunc(GroupOp->SourceGroupName, TargetGroup);
			return FReply::Handled();
		}

		return FReply::Unhandled();
	}

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override
	{
		if (bDragOver)
		{
			// Draw a highlight border when hovering
			const FVector2D Size = AllottedGeometry.GetLocalSize();
			const float Thickness = 2.0f;
			const FLinearColor HighlightColor(0.3f, 0.5f, 0.8f, 0.6f);
			const FSlateBrush* WhiteBrush = FAppStyle::Get().GetBrush("WhiteBrush");

			// Top
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 1, MakePaintGeometry(AllottedGeometry, FVector2D(Size.X, Thickness), FSlateLayoutTransform()),
				WhiteBrush, ESlateDrawEffect::None, HighlightColor);
			// Bottom
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 1, MakePaintGeometry(AllottedGeometry, FVector2D(Size.X, Thickness), FSlateLayoutTransform(FVector2D(0, Size.Y - Thickness))),
				WhiteBrush, ESlateDrawEffect::None, HighlightColor);
			// Left
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 1, MakePaintGeometry(AllottedGeometry, FVector2D(Thickness, Size.Y), FSlateLayoutTransform()),
				WhiteBrush, ESlateDrawEffect::None, HighlightColor);
			// Right
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 1, MakePaintGeometry(AllottedGeometry, FVector2D(Thickness, Size.Y), FSlateLayoutTransform(FVector2D(Size.X - Thickness, 0))),
				WhiteBrush, ESlateDrawEffect::None, HighlightColor);
		}

		return SCompoundWidget::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	}

private:
	bool bDragOver = false;
};
