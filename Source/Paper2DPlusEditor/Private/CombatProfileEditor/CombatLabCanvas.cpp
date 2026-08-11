// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "CombatProfileEditor/CombatLabCanvas.h"

#include "CombatProfileEditor/CombatLabPanel.h"
#include "CombatProfileEditor/CombatProfileEditorSession.h"
#include "EditorCanvasUtils.h"
#include "InputCoreTypes.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "PaperFlipbook.h"
#include "PaperSprite.h"
#include "Rendering/DrawElements.h"
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
#include "EditorStyleSet.h"
#ifndef FAppStyle
#define FAppStyle FEditorStyle
#endif
#else
#include "Styling/AppStyle.h"
#endif

void SCombatLabCanvas::Construct(const FArguments& InArgs)
{
	Model = InArgs._Model;
	SetClipping(EWidgetClipping::ClipToBounds);
	SetToolTipText(FText::FromString(
		TEXT("Click a participant, then drag or use the arrow keys to position it. Shift = 1 unit; Ctrl = 50 units.")));
}

FVector2D SCombatLabCanvas::ComputeDesiredSize(float) const
{
	return FVector2D(640.0f, 360.0f);
}

FVector2D SCombatLabCanvas::WorldToCanvas(const FVector2D& World, const FVector2D& Size) const
{
	return FVector2D(Size.X * 0.5f + World.X * Zoom, Size.Y * 0.70f - World.Y * Zoom);
}

void SCombatLabCanvas::DrawParticipant(
	int32 Index,
	const FGeometry& Geometry,
	FSlateWindowElementList& OutDrawElements,
	int32& LayerId) const
{
	if (!Model.IsValid()) return;
	const FCombatLabParticipantState& Participant = Model->GetParticipant(Index);
	const FFlipbookProfileEntry* Entry = Model->GetEntry(Index);
	UPaperFlipbook* Flipbook = Model->GetFlipbook(Index);
	const FVector2D PreviewPosition = Model->GetParticipantPreviewPosition(Index);
	const FVector2D CanvasOrigin = WorldToCanvas(PreviewPosition, Geometry.GetLocalSize());
	if (Entry && Flipbook && Flipbook->GetNumKeyFrames() > 0)
	{
		const int32 Frame = FMath::Clamp(Participant.FrameIndex, 0, Flipbook->GetNumKeyFrames() - 1);
		UPaperSprite* Sprite = Flipbook->GetKeyFrameChecked(Frame).Sprite;
		if (Sprite)
		{
			FEditorCanvasUtils::DrawFlipbookSprite(
				OutDrawElements, LayerId++, Geometry, Sprite, Entry, Frame, CanvasOrigin, Zoom,
				Index == 0 ? FLinearColor::White : FLinearColor(0.85f, 0.9f, 1.0f),
				nullptr, nullptr, Participant.bFacingLeft);
		}
	}

	const TArray<FWorldHitbox>& Boxes = Index == 0
		? Model->GetEvaluation().AttackerBoxes
		: Model->GetEvaluation().DefenderBoxes;
	for (const FWorldHitbox& Box : Boxes)
	{
		const FVector2D Center = WorldToCanvas(FVector2D(Box.Center.X, Box.Center.Z), Geometry.GetLocalSize());
		const FVector2D Extents(Box.Extents.X * Zoom, Box.Extents.Z * Zoom);
		const FVector2D Position = Center - Extents;
		const FVector2D Size = Extents * 2.0f;
		const FLinearColor Color = Box.Type == EHitboxType::Attack
			? FLinearColor(1.0f, 0.12f, 0.08f, 0.32f)
			: FLinearColor(0.12f, 0.9f, 0.25f, 0.24f);
		FSlateDrawElement::MakeBox(
			OutDrawElements, LayerId++, MakePaintGeometry(Geometry, Size, FSlateLayoutTransform(Position)),
			FAppStyle::Get().GetBrush(TEXT("WhiteBrush")), ESlateDrawEffect::None, Color);
	}

	FSlateDrawElement::MakeText(
		OutDrawElements, LayerId++,
		MakePaintGeometry(Geometry, FVector2D(140.0f, 20.0f),
			FSlateLayoutTransform(CanvasOrigin + FVector2D(-70.0f, 12.0f))),
		Participant.MoveName.IsNone() ? FString(TEXT("No move")) : Participant.MoveName.ToString(),
		FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 9), ESlateDrawEffect::None,
		Index == 0 ? FLinearColor(1.0f, 0.75f, 0.25f) : FLinearColor(0.45f, 0.75f, 1.0f));
}

int32 SCombatLabCanvas::OnPaint(
	const FPaintArgs& Args,
	const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements,
	int32 LayerId,
	const FWidgetStyle& InWidgetStyle,
	bool bParentEnabled) const
{
	FEditorCanvasUtils::DrawCheckerboard(OutDrawElements, LayerId++, AllottedGeometry);
	if (!Model.IsValid()) return LayerId;
	const FVector2D Size = AllottedGeometry.GetLocalSize();
	const FSlateBrush* WhiteBrush = FAppStyle::Get().GetBrush(TEXT("WhiteBrush"));

	// Preferred range is an advisory band from the selected attacker position.
	const FPaper2DPlusCombatAttackDerivedData* Attack = Model->GetSession().IsValid()
		? Model->GetSession()->GetSelectedCatalogRow()
		: nullptr;
	if (Attack)
	{
		const FCombatLabParticipantState& Attacker = Model->GetParticipant(0);
		const float Direction = Attacker.bFacingLeft ? -1.0f : 1.0f;
		const float Near = Attack->PreferredRangeLocal.X * Direction;
		const float Far = Attack->PreferredRangeLocal.Y * Direction;
		const float AttackerX = Model->GetParticipantPreviewPosition(0).X;
		const float MinX = FMath::Min(Near, Far) + AttackerX;
		const float MaxX = FMath::Max(Near, Far) + AttackerX;
		const FVector2D P0 = WorldToCanvas(FVector2D(MinX, 0.0f), Size);
		const FVector2D P1 = WorldToCanvas(FVector2D(MaxX, 0.0f), Size);
		FSlateDrawElement::MakeBox(
			OutDrawElements, LayerId++,
			MakePaintGeometry(AllottedGeometry, FVector2D(FMath::Max(1.0f, P1.X - P0.X), 42.0f),
				FSlateLayoutTransform(FVector2D(P0.X, Size.Y * 0.70f - 42.0f))),
			WhiteBrush, ESlateDrawEffect::None, FLinearColor(0.2f, 0.45f, 1.0f, 0.12f));
	}

	FSlateDrawElement::MakeBox(
		OutDrawElements, LayerId++,
		MakePaintGeometry(AllottedGeometry, FVector2D(Size.X, 1.0f),
			FSlateLayoutTransform(FVector2D(0.0f, Size.Y * 0.70f))),
		WhiteBrush, ESlateDrawEffect::None, FLinearColor(0.55f, 0.55f, 0.55f, 0.6f));

	DrawParticipant(0, AllottedGeometry, OutDrawElements, LayerId);
	DrawParticipant(1, AllottedGeometry, OutDrawElements, LayerId);
	if (Model->GetEvaluation().bAnyCollision)
	{
		FSlateDrawElement::MakeBox(
			OutDrawElements, LayerId++, AllottedGeometry.ToPaintGeometry(), WhiteBrush,
			ESlateDrawEffect::None, FLinearColor(1.0f, 0.65f, 0.0f, 0.08f));
	}
	return LayerId;
}

int32 SCombatLabCanvas::PickParticipantForTests(
	const FVector2D& LocalPosition,
	const FVector2D& CanvasSize) const
{
	if (!Model.IsValid()) return INDEX_NONE;
	int32 Best = INDEX_NONE;
	float BestDistance = 64.0f;
	for (int32 Index = 0; Index < 2; ++Index)
	{
		const float Distance = FVector2D::Distance(
			LocalPosition, WorldToCanvas(Model->GetParticipantPreviewPosition(Index), CanvasSize));
		if (Distance < BestDistance)
		{
			BestDistance = Distance;
			Best = Index;
		}
	}
	return Best;
}

FReply SCombatLabCanvas::OnKeyDown(
	const FGeometry& MyGeometry,
	const FKeyEvent& InKeyEvent)
{
	if (!Model.IsValid())
	{
		return SLeafWidget::OnKeyDown(MyGeometry, InKeyEvent);
	}

	FVector2D Direction = FVector2D::ZeroVector;
	const FKey Key = InKeyEvent.GetKey();
	if (Key == EKeys::Left)
	{
		Direction.X = -1.0f;
	}
	else if (Key == EKeys::Right)
	{
		Direction.X = 1.0f;
	}
	else if (Key == EKeys::Up)
	{
		Direction.Y = 1.0f;
	}
	else if (Key == EKeys::Down)
	{
		Direction.Y = -1.0f;
	}
	else
	{
		return SLeafWidget::OnKeyDown(MyGeometry, InKeyEvent);
	}

	const float Step = InKeyEvent.IsControlDown()
		? 50.0f
		: (InKeyEvent.IsShiftDown() ? 1.0f : 10.0f);
	const int32 ParticipantIndex = Model->GetSelectedParticipant();
	const FVector2D Position = Model->GetParticipant(ParticipantIndex).Position;
	Model->SetPosition(ParticipantIndex, Position + Direction * Step);
	return FReply::Handled();
}

FReply SCombatLabCanvas::OnMouseButtonDown(
	const FGeometry& MyGeometry,
	const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton) return FReply::Unhandled();
	const FVector2D Local = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
	DraggedParticipant = PickParticipantForTests(Local, MyGeometry.GetLocalSize());
	if (DraggedParticipant == INDEX_NONE) return FReply::Unhandled();
	Model->SetSelectedParticipant(DraggedParticipant);
	LastDragLocal = Local;
	return FReply::Handled()
		.CaptureMouse(SharedThis(this))
		.SetUserFocus(SharedThis(this), EFocusCause::Mouse);
}

FReply SCombatLabCanvas::OnMouseMove(
	const FGeometry& MyGeometry,
	const FPointerEvent& MouseEvent)
{
	if (DraggedParticipant == INDEX_NONE || !HasMouseCapture() || !Model.IsValid()) return FReply::Unhandled();
	const FVector2D Local = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
	const FVector2D Delta = Local - LastDragLocal;
	LastDragLocal = Local;
	const FVector2D Old = Model->GetParticipant(DraggedParticipant).Position;
	Model->SetPosition(DraggedParticipant, Old + FVector2D(Delta.X / Zoom, -Delta.Y / Zoom));
	return FReply::Handled();
}

FReply SCombatLabCanvas::OnMouseButtonUp(
	const FGeometry& MyGeometry,
	const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton || DraggedParticipant == INDEX_NONE)
	{
		return FReply::Unhandled();
	}
	DraggedParticipant = INDEX_NONE;
	return FReply::Handled().ReleaseMouseCapture();
}

void SCombatLabCanvas::OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent)
{
	DraggedParticipant = INDEX_NONE;
	SLeafWidget::OnMouseCaptureLost(CaptureLostEvent);
}
