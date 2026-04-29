// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Input/DragAndDrop.h"
#include "Framework/Application/SlateApplication.h"

/**
 * SDragClickWrapper — Reusable click + drag-threshold wrapper widget.
 *
 * Handles the common pattern of: left-click → detect drag threshold (5px) → begin
 * drag-drop operation OR fire click callback. Right-click fires a separate callback.
 * Double-click optionally handled.
 *
 * Replaces 5 duplicate wrapper classes across the character profile editor tabs.
 * Each call site customizes behavior via TFunction members set after SNew().
 */
class PAPER2DPLUSEDITOR_API SDragClickWrapper : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SDragClickWrapper) {}
		SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_END_ARGS()

	/** Called on left-click (mouse down + up without exceeding drag threshold). */
	TFunction<void(const FGeometry&, const FPointerEvent&)> OnClickedFunc;

	/** Called on right-click. */
	TFunction<void(const FGeometry&, const FPointerEvent&)> OnRightClickedFunc;

	/** Called on double-click. */
	TFunction<void()> OnDoubleClickedFunc;

	/** Called when drag threshold is exceeded. Must return a valid FDragDropOperation. */
	TFunction<TSharedPtr<FDragDropOperation>()> OnDragDetectedFunc;

	void Construct(const FArguments& InArgs)
	{
		ChildSlot[ InArgs._Content.Widget ];
	}

	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
		{
			bPotentialDrag = true;
			DragStartPos = MouseEvent.GetScreenSpacePosition();
			CachedGeometry = MyGeometry;
			return FReply::Handled().CaptureMouse(SharedThis(this));
		}
		if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
		{
			if (OnRightClickedFunc) { OnRightClickedFunc(MyGeometry, MouseEvent); }
			return FReply::Handled();
		}
		return FReply::Unhandled();
	}

	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		if (bPotentialDrag && MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
		{
			bPotentialDrag = false;
			if (OnClickedFunc) { OnClickedFunc(MyGeometry, MouseEvent); }
			return FReply::Handled().ReleaseMouseCapture();
		}
		return FReply::Unhandled();
	}

	virtual FReply OnMouseButtonDoubleClick(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && OnDoubleClickedFunc)
		{
			OnDoubleClickedFunc();
			return FReply::Handled();
		}
		return FReply::Unhandled();
	}

	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		if (bPotentialDrag)
		{
			const float Distance = FVector2D::Distance(MouseEvent.GetScreenSpacePosition(), DragStartPos);
			if (Distance > 5.0f)
			{
				bPotentialDrag = false;
				if (OnDragDetectedFunc)
				{
					TSharedPtr<FDragDropOperation> Op = OnDragDetectedFunc();
					if (Op.IsValid())
					{
						return FReply::Handled().ReleaseMouseCapture().BeginDragDrop(Op.ToSharedRef());
					}
				}
				return FReply::Handled().ReleaseMouseCapture();
			}
		}
		return FReply::Unhandled();
	}

	virtual void OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent) override
	{
		bPotentialDrag = false;
	}

private:
	bool bPotentialDrag = false;
	FVector2D DragStartPos;
	FGeometry CachedGeometry;
};
