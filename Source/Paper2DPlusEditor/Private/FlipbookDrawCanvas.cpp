// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "FlipbookDrawCanvas.h"
#include "BulkDebakeUtils.h"
#include "FlipbookDrawModel.h"
#include "FlipbookPixelEdit.h"
#include "SlateShortcutUtils.h"
#include "PaperSprite.h"
#include "PaperFlipbook.h"
#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "PixelFormat.h"
#include "Framework/Application/SlateApplication.h"
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 2)
#include "RHITypes.h"
#else
#include "RHIDefinitions.h" // UE 5.0/5.1: RHITypes.h does not exist; the RHI types/enums live in RHIDefinitions.h
#endif

namespace
{
	constexpr float FlipbookDrawInputMinZoom = 0.25f;
	constexpr float FlipbookDrawInputMaxZoom = 64.0f;

	bool FlipbookDraw_IsShapeTool(EFlipbookDrawTool Tool)
	{
		return Tool == EFlipbookDrawTool::Line || Tool == EFlipbookDrawTool::Rectangle;
	}
}

void SFlipbookDrawCanvas::Construct(const FArguments& InArgs)
{
	Model = InArgs._Model;
	SetClipping(EWidgetClipping::ClipToBounds);

	if (Model.IsValid())
	{
		FrameChangedHandle = Model->OnFrameChanged.AddSP(this, &SFlipbookDrawCanvas::HandleFrameChanged);
		PlaybackStateHandle = Model->OnPlaybackStateChanged.AddSP(this, &SFlipbookDrawCanvas::HandlePlaybackStateChanged);
	}

	EnsureWorkingFrame();
}

SFlipbookDrawCanvas::~SFlipbookDrawCanvas()
{
	if (Model.IsValid())
	{
		if (FrameChangedHandle.IsValid())   { Model->OnFrameChanged.Remove(FrameChangedHandle); }
		if (PlaybackStateHandle.IsValid())  { Model->OnPlaybackStateChanged.Remove(PlaybackStateHandle); }
	}
}

void SFlipbookDrawCanvas::AddReferencedObjects(FReferenceCollector& Collector)
{
	// The flipbook itself is GC-rooted by the toolkit (passed to InitAssetEditor); root it here too,
	// defensively, since the canvas dereferences its sprites/textures from a const OnPaint.
	// (Phase 2 adds the transient working UTexture2D, which MUST be rooted here.)
	if (Model.IsValid())
	{
		// TObjectPtr overload — the raw-UObject* AddReferencedObject is deprecated (C4996) and would
		// fail the forced-unity -WarningsAsErrors gate.
		if (TObjectPtr<UPaperFlipbook> FB = Model->GetFlipbook())
		{
			Collector.AddReferencedObject(FB);
		}
	}

	// The transient working texture is otherwise unreferenced — it MUST be rooted or GC can reclaim it mid-edit.
	if (WorkingTexture)
	{
		Collector.AddReferencedObject(WorkingTexture);
	}
}

void SFlipbookDrawCanvas::RequestFitView()
{
	bNeedsInitialFit = true;
	Invalidate(EInvalidateWidgetReason::Paint);
}

bool SFlipbookDrawCanvas::CanApplyFrameTransform() const
{
	return Model.IsValid() && Model->GetCurrentSprite() && !bStroking && !bIsPanning
		&& (bDrawable || Model->IsPlaying());
}

void SFlipbookDrawCanvas::ApplyFrameTransform(EFlipbookPixelTransform Transform)
{
	if (!CanApplyFrameTransform())
	{
		return;
	}
	// Transform buttons live in a sibling panel. Return focus to the canvas so its pixel-specific
	// Ctrl+Z/Ctrl+Y shortcuts remain immediately available after the one-shot action.
	FSlateApplication::Get().SetKeyboardFocus(SharedThis(this), EFocusCause::SetDirectly);
	if (Model->IsPlaying())
	{
		Model->SetPlaying(false);
	}

	EnsureWorkingFrame();
	if (!bDrawable)
	{
		return;
	}
	const TArray<FColor> Before = WorkingBuffer;
	if (!FFlipbookPixelEdit::TransformFrame(WorkingBuffer, WorkW, WorkH, Transform))
	{
		return;
	}

	const FIntRect FullFrame(0, 0, WorkW, WorkH);
	PushDirtyToWorkingTexture(FullFrame);
	CommitWorkingEdit(Before, FullFrame);
	Invalidate(EInvalidateWidgetReason::Paint);
}

void SFlipbookDrawCanvas::HandleFrameChanged()
{
	// Defense-in-depth: if a stroke is somehow still active when the frame changes, ABANDON it (don't
	// commit) rather than let EnsureWorkingFrame swap the buffer to a different frame mid-stroke — that
	// would paint the wrong frame and pair a new-frame commit with old-frame undo pixels. (The OnKeyDown
	// guard already blocks the keyboard vector; this covers any future frame-change path.)
	const bool bHadCapturedInteraction = bStroking || bIsPanning;
	if (bStroking)
	{
		bStroking = false;
		StrokeBefore.Reset();
		StrokeDirty = FFlipbookPixelEdit::EmptyDirtyRect();
	}
	bIsPanning = false;
	if (bHadCapturedInteraction && HasMouseCapture())
	{
		FSlateApplication::Get().ReleaseAllPointerCapture();
	}
	EnsureWorkingFrame();
	Invalidate(EInvalidateWidgetReason::Paint);
}

FReply SFlipbookDrawCanvas::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	const FKey Button = MouseEvent.GetEffectingButton();

	// Middle/right drag = pan.
	if (Button == EKeys::MiddleMouseButton || Button == EKeys::RightMouseButton)
	{
		bIsPanning = true;
		LastPanLocalPos = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
		return FReply::Handled().CaptureMouse(SharedThis(this));
	}

	// Left click: paint (if this frame is drawable and a stamp tool is active), and always take keyboard
	// focus so ←/→ frame nav + Ctrl+Z work.
	if (Button == EKeys::LeftMouseButton)
	{
		FReply Reply = FReply::Handled().SetUserFocus(SharedThis(this), EFocusCause::Mouse);

		// Drawing always stops playback first.
		if (Model.IsValid() && Model->IsPlaying())
		{
			Model->SetPlaying(false);
		}

		EnsureWorkingFrame();
		if (bDrawable && Model.IsValid())
		{
			const FIntPoint Pixel = ScreenToFramePixelInt(MyGeometry, MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition()));
			switch (Model->GetActiveTool())
			{
			case EFlipbookDrawTool::Eyedropper:
				SampleEyedropper(Pixel);
				break;
			case EFlipbookDrawTool::Fill:
				DoFloodFill(Pixel);
				break;
			case EFlipbookDrawTool::Pencil:
			case EFlipbookDrawTool::Eraser:
			case EFlipbookDrawTool::Line:
			case EFlipbookDrawTool::Rectangle:
			default:
				BeginStroke(Pixel);
				Reply.CaptureMouse(SharedThis(this));
				break;
			}
		}
		return Reply;
	}

	return FReply::Unhandled();
}

FReply SFlipbookDrawCanvas::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (bIsPanning && HasMouseCapture())
	{
		const FVector2D LocalPos = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
		PanOffset += (LocalPos - LastPanLocalPos);
		LastPanLocalPos = LocalPos;
		Invalidate(EInvalidateWidgetReason::Paint);
		return FReply::Handled();
	}

	if (bStroking && HasMouseCapture())
	{
		const FIntPoint Pixel = ScreenToFramePixelInt(MyGeometry, MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition()));
		ContinueStroke(Pixel);
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

FReply SFlipbookDrawCanvas::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	const FKey Button = MouseEvent.GetEffectingButton();
	if (bIsPanning && (Button == EKeys::MiddleMouseButton || Button == EKeys::RightMouseButton))
	{
		bIsPanning = false;
		return FReply::Handled().ReleaseMouseCapture();
	}

	if (bStroking && Button == EKeys::LeftMouseButton)
	{
		EndStroke();
		return FReply::Handled().ReleaseMouseCapture();
	}
	return FReply::Unhandled();
}

FReply SFlipbookDrawCanvas::OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	const float Delta = MouseEvent.GetWheelDelta();
	if (Delta == 0.0f)
	{
		return FReply::Unhandled();
	}

	const float OldZoom = ZoomLevel;
	const float Factor = (Delta > 0.0f) ? 1.1f : (1.0f / 1.1f);
	ZoomLevel = FMath::Clamp(ZoomLevel * Factor, FlipbookDrawInputMinZoom, FlipbookDrawInputMaxZoom);

	// Keep the pixel under the cursor stationary while zooming.
	const FVector2D MouseLocal = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
	const FVector2D Center = MyGeometry.GetLocalSize() * 0.5f;
	const FVector2D ToMouse = MouseLocal - Center - PanOffset;
	PanOffset += ToMouse * (1.0f - ZoomLevel / OldZoom);

	Invalidate(EInvalidateWidgetReason::Paint);
	return FReply::Handled();
}

void SFlipbookDrawCanvas::OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent)
{
	bIsPanning = false;
	if (bStroking)
	{
		// Commit whatever was painted so a lost capture doesn't drop the stroke.
		EndStroke();
	}
}

FReply SFlipbookDrawCanvas::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (Paper2DPlusEditor::SlateShortcutUtils::ShouldIgnoreShortcutForFocusedWidget())
	{
		return FReply::Unhandled();
	}

	if (!Model.IsValid())
	{
		return FReply::Unhandled();
	}

	// While a stroke is in flight the mouse is captured but Slate still delivers keys to the focused
	// canvas. Swallow ALL key gestures (frame nav, playback, tool switch, undo) until mouse-up — a
	// mid-stroke frame switch would reload the working buffer out from under the stroke and corrupt
	// both the painted frame and the undo entry.
	if (bStroking)
	{
		return FReply::Handled();
	}

	const FKey Key = InKeyEvent.GetKey();

	// Undo / redo (Ctrl+Z, Ctrl+Y, Ctrl+Shift+Z) — our own pixel-edit stacks.
	if (InKeyEvent.IsControlDown() && Key == EKeys::Z)
	{
		if (InKeyEvent.IsShiftDown()) { Redo(); } else { Undo(); }
		return FReply::Handled();
	}
	if (InKeyEvent.IsControlDown() && Key == EKeys::Y)
	{
		Redo();
		return FReply::Handled();
	}

	// Space toggles playback.
	if (Key == EKeys::SpaceBar)
	{
		Model->TogglePlaying();
		return FReply::Handled();
	}

	// Tool hotkeys mirror the Drawing Tools buttons.
	if (Key == EKeys::B) { Model->SetActiveTool(EFlipbookDrawTool::Pencil);     return FReply::Handled(); }
	if (Key == EKeys::E) { Model->SetActiveTool(EFlipbookDrawTool::Eraser);     return FReply::Handled(); }
	if (Key == EKeys::I) { Model->SetActiveTool(EFlipbookDrawTool::Eyedropper); return FReply::Handled(); }
	if (Key == EKeys::G) { Model->SetActiveTool(EFlipbookDrawTool::Fill);       return FReply::Handled(); }
	if (Key == EKeys::L) { Model->SetActiveTool(EFlipbookDrawTool::Line);       return FReply::Handled(); }
	if (Key == EKeys::R) { Model->SetActiveTool(EFlipbookDrawTool::Rectangle);  return FReply::Handled(); }

	if (Key == EKeys::Left)
	{
		Model->SetPlaying(false);
		Model->StepFrame(-1);
		return FReply::Handled();
	}
	if (Key == EKeys::Right)
	{
		Model->SetPlaying(false);
		Model->StepFrame(1);
		return FReply::Handled();
	}
	if (Key == EKeys::F)
	{
		RequestFitView();
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

// ============================================================================
// Working buffer / live preview
// ============================================================================

void SFlipbookDrawCanvas::EnsureWorkingFrame()
{
	const int32 Frame = Model.IsValid() ? Model->GetCurrentFrame() : INDEX_NONE;
	if (Frame == WorkingFrame && WorkingTexture)
	{
		return; // already loaded for this frame
	}

	WorkingBuffer.Reset();
	WorkW = 0;
	WorkH = 0;
	bDrawable = false;
	WorkingTexture = nullptr;
	WorkingFrame = Frame;

	// During playback the canvas draws the sprite's own texture (read-only) — skip the expensive
	// per-frame transient-texture build (CreateTransient + UpdateResource). The editable working
	// texture is (re)built when playback stops (HandlePlaybackStateChanged) or on the next manual edit.
	if (Model.IsValid() && Model->IsPlaying())
	{
		return;
	}

	UPaperSprite* Sprite = Model.IsValid() ? Model->GetCurrentSprite() : nullptr;
	if (!Sprite)
	{
		return;
	}

	if (FFlipbookPixelEdit::ReadFrame(Sprite, WorkingBuffer, WorkW, WorkH) && WorkW > 0 && WorkH > 0)
	{
		bDrawable = true;
		RebuildWorkingTexture();
	}
}

void SFlipbookDrawCanvas::RebuildWorkingTexture()
{
	// ONE shared implementation of the version-gated CreateTransient recipe (also the bulk
	// extractor's de-bake preview) — GC rooting stays here (this canvas's FGCObject roots
	// WorkingTexture); the helper never roots.
	WorkingTexture = FBulkDebakeUtils::CreateTransientPreviewTexture(WorkW, WorkH, WorkingBuffer);
}

void SFlipbookDrawCanvas::PushDirtyToWorkingTexture(const FIntRect& InRect)
{
	if (!WorkingTexture || !bDrawable)
	{
		return;
	}

	FIntRect R = InRect;
	R.Min.X = FMath::Max(R.Min.X, 0);
	R.Min.Y = FMath::Max(R.Min.Y, 0);
	R.Max.X = FMath::Min(R.Max.X, WorkW);
	R.Max.Y = FMath::Min(R.Max.Y, WorkH);
	const int32 RW = R.Max.X - R.Min.X;
	const int32 RH = R.Max.Y - R.Min.Y;
	if (RW <= 0 || RH <= 0)
	{
		return;
	}

	// Copy the dirtied sub-rect into a tightly-packed heap buffer (freed by the render-thread cleanup).
	uint8* Temp = static_cast<uint8*>(FMemory::Malloc((SIZE_T)RW * RH * 4));
	const uint8* SrcBytes = reinterpret_cast<const uint8*>(WorkingBuffer.GetData());
	for (int32 Y = 0; Y < RH; ++Y)
	{
		FMemory::Memcpy(
			Temp + (SIZE_T)Y * RW * 4,
			SrcBytes + (((SIZE_T)(R.Min.Y + Y) * WorkW + R.Min.X) * 4),
			(SIZE_T)RW * 4);
	}

	FUpdateTextureRegion2D* Region = new FUpdateTextureRegion2D((uint32)R.Min.X, (uint32)R.Min.Y, 0, 0, (uint32)RW, (uint32)RH);
	WorkingTexture->UpdateTextureRegions(0, 1, Region, (uint32)(RW * 4), 4, Temp,
		[](uint8* Data, const FUpdateTextureRegion2D* Regions)
		{
			FMemory::Free(Data);
			delete Regions;
		});
}

// ============================================================================
// Brush stroke
// ============================================================================

FColor SFlipbookDrawCanvas::GetBrushColor(EFlipbookDrawTool Tool) const
{
	if (Tool == EFlipbookDrawTool::Eraser)
	{
		return FColor(0, 0, 0, 0); // erase to transparent
	}
	const FLinearColor Linear = Model.IsValid() ? Model->GetPrimaryColor() : FLinearColor::White;
	return Linear.ToFColor(/*bSRGB*/ true);
}

void SFlipbookDrawCanvas::BeginStroke(const FIntPoint& FramePixel)
{
	if (!bDrawable)
	{
		return;
	}
	bStroking = true;
	StrokeTool = Model->GetActiveTool();
	ShapeStartPixel = FramePixel;
	StrokeBefore = WorkingBuffer; // snapshot for undo
	StrokeDirty = FFlipbookPixelEdit::EmptyDirtyRect();
	LastStampPixel = FramePixel;

	if (FlipbookDraw_IsShapeTool(StrokeTool))
	{
		UpdateShapePreview(FramePixel);
		return;
	}

	const EFlipbookMirror Mirror = Model->GetMirror();
	const bool bMX = EnumHasAnyFlags(Mirror, EFlipbookMirror::Horizontal);
	const bool bMY = EnumHasAnyFlags(Mirror, EFlipbookMirror::Vertical);

	FIntRect StepDirty = FFlipbookPixelEdit::EmptyDirtyRect();
	FFlipbookPixelEdit::StampDabMirrored(WorkingBuffer, WorkW, WorkH, FramePixel.X, FramePixel.Y,
		Model->GetBrushSize(), GetBrushColor(StrokeTool), bMX, bMY, StepDirty);
	if (FFlipbookPixelEdit::IsDirtyValid(StepDirty))
	{
		FFlipbookPixelEdit::UnionDirty(StrokeDirty, StepDirty);
		PushDirtyToWorkingTexture(StepDirty);
	}
	Invalidate(EInvalidateWidgetReason::Paint);
}

void SFlipbookDrawCanvas::ContinueStroke(const FIntPoint& FramePixel)
{
	if (!bStroking || !bDrawable)
	{
		return;
	}
	if (FlipbookDraw_IsShapeTool(StrokeTool))
	{
		UpdateShapePreview(FramePixel);
		return;
	}
	const EFlipbookMirror Mirror = Model->GetMirror();
	const bool bMX = EnumHasAnyFlags(Mirror, EFlipbookMirror::Horizontal);
	const bool bMY = EnumHasAnyFlags(Mirror, EFlipbookMirror::Vertical);

	FIntRect StepDirty = FFlipbookPixelEdit::EmptyDirtyRect();
	FFlipbookPixelEdit::StampLineMirrored(WorkingBuffer, WorkW, WorkH,
		LastStampPixel.X, LastStampPixel.Y, FramePixel.X, FramePixel.Y,
		Model->GetBrushSize(), GetBrushColor(StrokeTool), bMX, bMY, StepDirty);
	if (FFlipbookPixelEdit::IsDirtyValid(StepDirty))
	{
		FFlipbookPixelEdit::UnionDirty(StrokeDirty, StepDirty);
		PushDirtyToWorkingTexture(StepDirty);
	}
	LastStampPixel = FramePixel;
	Invalidate(EInvalidateWidgetReason::Paint);
}

void SFlipbookDrawCanvas::UpdateShapePreview(const FIntPoint& FramePixel)
{
	if (!bStroking || !bDrawable || !Model.IsValid() || !FlipbookDraw_IsShapeTool(StrokeTool))
	{
		return;
	}

	const FIntRect PreviousDirty = StrokeDirty;
	if (FFlipbookPixelEdit::IsDirtyValid(PreviousDirty))
	{
		FIntRect RestoreRect = PreviousDirty;
		RestoreRect.Min.X = FMath::Max(RestoreRect.Min.X, 0);
		RestoreRect.Min.Y = FMath::Max(RestoreRect.Min.Y, 0);
		RestoreRect.Max.X = FMath::Min(RestoreRect.Max.X, WorkW);
		RestoreRect.Max.Y = FMath::Min(RestoreRect.Max.Y, WorkH);
		const int32 RestoreWidth = RestoreRect.Max.X - RestoreRect.Min.X;
		if (RestoreWidth > 0)
		{
			for (int32 Y = RestoreRect.Min.Y; Y < RestoreRect.Max.Y; ++Y)
			{
				FMemory::Memcpy(
					WorkingBuffer.GetData() + Y * WorkW + RestoreRect.Min.X,
					StrokeBefore.GetData() + Y * WorkW + RestoreRect.Min.X,
					(SIZE_T)RestoreWidth * sizeof(FColor));
			}
		}
	}
	FIntRect NewDirty = FFlipbookPixelEdit::EmptyDirtyRect();

	const EFlipbookMirror Mirror = Model->GetMirror();
	const bool bMX = EnumHasAnyFlags(Mirror, EFlipbookMirror::Horizontal);
	const bool bMY = EnumHasAnyFlags(Mirror, EFlipbookMirror::Vertical);
	if (StrokeTool == EFlipbookDrawTool::Line)
	{
		FFlipbookPixelEdit::StampLineMirrored(
			WorkingBuffer, WorkW, WorkH,
			ShapeStartPixel.X, ShapeStartPixel.Y, FramePixel.X, FramePixel.Y,
			Model->GetBrushSize(), GetBrushColor(StrokeTool), bMX, bMY, NewDirty);
	}
	else
	{
		FFlipbookPixelEdit::StampRectangleMirrored(
			WorkingBuffer, WorkW, WorkH, ShapeStartPixel, FramePixel,
			Model->GetBrushSize(), GetBrushColor(StrokeTool), bMX, bMY, NewDirty);
	}

	FIntRect PreviewDirty = NewDirty;
	if (FFlipbookPixelEdit::IsDirtyValid(PreviousDirty))
	{
		if (FFlipbookPixelEdit::IsDirtyValid(PreviewDirty))
		{
			FFlipbookPixelEdit::UnionDirty(PreviewDirty, PreviousDirty);
		}
		else
		{
			PreviewDirty = PreviousDirty;
		}
	}
	StrokeDirty = NewDirty;
	if (FFlipbookPixelEdit::IsDirtyValid(PreviewDirty))
	{
		PushDirtyToWorkingTexture(PreviewDirty);
	}
	LastStampPixel = FramePixel;
	Invalidate(EInvalidateWidgetReason::Paint);
}

void SFlipbookDrawCanvas::EndStroke()
{
	if (!bStroking)
	{
		return;
	}
	bStroking = false;

	if (bDrawable && FFlipbookPixelEdit::IsDirtyValid(StrokeDirty))
	{
		CommitWorkingEdit(StrokeBefore, StrokeDirty);
	}

	StrokeBefore.Reset();
	StrokeDirty = FFlipbookPixelEdit::EmptyDirtyRect();
}

// ============================================================================
// Undo / redo
// ============================================================================

bool SFlipbookDrawCanvas::CommitWorkingEdit(const TArray<FColor>& Before, const FIntRect& DirtyRect)
{
	if (!bDrawable || !Model.IsValid() || Before.Num() != WorkingBuffer.Num())
	{
		return false;
	}

	FIntRect Rect = DirtyRect;
	Rect.Min.X = FMath::Max(Rect.Min.X, 0);
	Rect.Min.Y = FMath::Max(Rect.Min.Y, 0);
	Rect.Max.X = FMath::Min(Rect.Max.X, WorkW);
	Rect.Max.Y = FMath::Min(Rect.Max.Y, WorkH);
	if (Rect.Min.X >= Rect.Max.X || Rect.Min.Y >= Rect.Max.Y)
	{
		return false;
	}

	if (!FFlipbookPixelEdit::WriteFrame(Model->GetCurrentSprite(), WorkingBuffer, WorkW, WorkH))
	{
		WorkingBuffer = Before;
		PushDirtyToWorkingTexture(Rect);
		return false;
	}

	const int32 RW = Rect.Max.X - Rect.Min.X;
	const int32 RH = Rect.Max.Y - Rect.Min.Y;
	FFlipbookPixelEditEntry Entry;
	Entry.Frame = Model->GetCurrentFrame();
	Entry.Rect = Rect;
	Entry.Before.SetNumUninitialized(RW * RH);
	Entry.After.SetNumUninitialized(RW * RH);
	for (int32 Y = 0; Y < RH; ++Y)
	{
		for (int32 X = 0; X < RW; ++X)
		{
			const int32 Src = (Rect.Min.Y + Y) * WorkW + (Rect.Min.X + X);
			const int32 Dst = Y * RW + X;
			Entry.Before[Dst] = Before[Src];
			Entry.After[Dst] = WorkingBuffer[Src];
		}
	}
	UndoStack.Push(MoveTemp(Entry));
	RedoStack.Reset();
	return true;
}

bool SFlipbookDrawCanvas::ApplyEdit(const FFlipbookPixelEditEntry& Entry, bool bUseAfter)
{
	if (Model.IsValid())
	{
		// Switching frames reloads the working buffer from the (committed) source via HandleFrameChanged.
		Model->SetCurrentFrame(Entry.Frame);
	}
	EnsureWorkingFrame();
	if (!bDrawable)
	{
		return false;
	}

	const TArray<FColor>& Px = bUseAfter ? Entry.After : Entry.Before;
	const int32 RW = Entry.Rect.Max.X - Entry.Rect.Min.X;
	const int32 RH = Entry.Rect.Max.Y - Entry.Rect.Min.Y;
	if (RW <= 0 || RH <= 0 || Px.Num() != RW * RH)
	{
		return false;
	}

	const TArray<FColor> BeforeApply = WorkingBuffer;
	for (int32 Y = 0; Y < RH; ++Y)
	{
		for (int32 X = 0; X < RW; ++X)
		{
			const int32 BX = Entry.Rect.Min.X + X;
			const int32 BY = Entry.Rect.Min.Y + Y;
			if (BX >= 0 && BX < WorkW && BY >= 0 && BY < WorkH)
			{
				WorkingBuffer[BY * WorkW + BX] = Px[Y * RW + X];
			}
		}
	}

	if (!FFlipbookPixelEdit::WriteFrame(Model->GetCurrentSprite(), WorkingBuffer, WorkW, WorkH))
	{
		WorkingBuffer = BeforeApply;
		return false;
	}

	PushDirtyToWorkingTexture(Entry.Rect);
	Invalidate(EInvalidateWidgetReason::Paint);
	return true;
}

void SFlipbookDrawCanvas::Undo()
{
	if (UndoStack.Num() == 0)
	{
		return;
	}
	FFlipbookPixelEditEntry Entry = UndoStack.Pop();
	if (ApplyEdit(Entry, /*bUseAfter*/ false))
	{
		RedoStack.Push(MoveTemp(Entry));
	}
	else
	{
		UndoStack.Push(MoveTemp(Entry));
	}
}

void SFlipbookDrawCanvas::Redo()
{
	if (RedoStack.Num() == 0)
	{
		return;
	}
	FFlipbookPixelEditEntry Entry = RedoStack.Pop();
	if (ApplyEdit(Entry, /*bUseAfter*/ true))
	{
		UndoStack.Push(MoveTemp(Entry));
	}
	else
	{
		RedoStack.Push(MoveTemp(Entry));
	}
}

void SFlipbookDrawCanvas::SampleEyedropper(const FIntPoint& FramePixel)
{
	if (!bDrawable || !Model.IsValid())
	{
		return;
	}
	if (FramePixel.X < 0 || FramePixel.X >= WorkW || FramePixel.Y < 0 || FramePixel.Y >= WorkH)
	{
		return;
	}
	const FColor Sampled = WorkingBuffer[FramePixel.Y * WorkW + FramePixel.X];
	// FColor (sRGB bytes) -> FLinearColor; GetBrushColor reverses via ToFColor(true), so the dropper round-trips.
	Model->SetPrimaryColor(FLinearColor(Sampled));
}

void SFlipbookDrawCanvas::DoFloodFill(const FIntPoint& FramePixel)
{
	if (!bDrawable || !Model.IsValid())
	{
		return;
	}
	if (FramePixel.X < 0 || FramePixel.X >= WorkW || FramePixel.Y < 0 || FramePixel.Y >= WorkH)
	{
		return;
	}

	// One-shot action committed as a single undo entry (reuses the stroke commit path).
	bStroking = true;
	StrokeBefore = WorkingBuffer;
	StrokeDirty = FFlipbookPixelEdit::EmptyDirtyRect();

	FFlipbookPixelEdit::FloodFill(WorkingBuffer, WorkW, WorkH, FramePixel.X, FramePixel.Y,
		GetBrushColor(EFlipbookDrawTool::Fill), StrokeDirty);
	if (FFlipbookPixelEdit::IsDirtyValid(StrokeDirty))
	{
		PushDirtyToWorkingTexture(StrokeDirty);
	}
	EndStroke(); // commit + record undo
}

void SFlipbookDrawCanvas::HandlePlaybackStateChanged()
{
	const bool bWantPlaying = Model.IsValid() && Model->IsPlaying();
	const bool bTicking = PlaybackTimerHandle.IsValid();

	if (bWantPlaying && !bTicking)
	{
		PlaybackAccum = 0.0;
		PlaybackFrameRunAccum = 0;
		PlaybackTimerHandle = RegisterActiveTimer(0.0f,
			FWidgetActiveTimerDelegate::CreateSP(this, &SFlipbookDrawCanvas::OnPlaybackTick));
	}
	else if (!bWantPlaying && bTicking)
	{
		UnRegisterActiveTimer(PlaybackTimerHandle.Pin().ToSharedRef());
		PlaybackTimerHandle.Reset();
	}

	// On STOP, rebuild the editable working texture for the now-current frame (it was skipped during
	// playback). Forcing WorkingFrame invalid makes EnsureWorkingFrame do a full (re)build.
	if (!bWantPlaying)
	{
		WorkingFrame = INDEX_NONE;
		EnsureWorkingFrame();
		Invalidate(EInvalidateWidgetReason::Paint);
	}
}

EActiveTimerReturnType SFlipbookDrawCanvas::OnPlaybackTick(double InCurrentTime, float InDeltaTime)
{
	UPaperFlipbook* FB = Model.IsValid() ? Model->GetFlipbook() : nullptr;
	if (!FB || !Model->IsPlaying() || FB->GetNumKeyFrames() < 2)
	{
		PlaybackTimerHandle.Reset();
		return EActiveTimerReturnType::Stop;
	}

	float FPS = FB->GetFramesPerSecond();
	if (FPS <= 0.0f) { FPS = 15.0f; }
	const double SecondsPerTimelineFrame = 1.0 / FPS;

	PlaybackAccum += InDeltaTime;
	while (PlaybackAccum >= SecondsPerTimelineFrame)
	{
		PlaybackAccum -= SecondsPerTimelineFrame;
		++PlaybackFrameRunAccum;

		const int32 Cur = Model->GetCurrentFrame();
		const int32 FrameRun = FMath::Max(FB->GetKeyFrameChecked(Cur).FrameRun, 1);
		if (PlaybackFrameRunAccum >= FrameRun)
		{
			PlaybackFrameRunAccum = 0;
			Model->SetCurrentFrame((Cur + 1) % FB->GetNumKeyFrames());
		}
	}
	return EActiveTimerReturnType::Continue;
}
