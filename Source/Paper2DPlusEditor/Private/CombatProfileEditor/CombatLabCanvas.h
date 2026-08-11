// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"

class FCombatLabModel;

/** Lightweight Slate renderer for the two worldless Combat Lab participants. */
class SCombatLabCanvas final : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SCombatLabCanvas) {}
		SLATE_ARGUMENT(TSharedPtr<FCombatLabModel>, Model)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent) override;

	int32 PickParticipantForTests(const FVector2D& LocalPosition, const FVector2D& CanvasSize) const;

private:
	FVector2D WorldToCanvas(const FVector2D& World, const FVector2D& Size) const;
	void DrawParticipant(int32 Index, const FGeometry& Geometry,
		FSlateWindowElementList& OutDrawElements, int32& LayerId) const;

	TSharedPtr<FCombatLabModel> Model;
	int32 DraggedParticipant = INDEX_NONE;
	FVector2D LastDragLocal = FVector2D::ZeroVector;
	float Zoom = 1.5f;
};
