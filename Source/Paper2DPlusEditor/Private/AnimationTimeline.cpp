// Copyright 2026 Infinite Gameworks. All Rights Reserved.

// AnimationTimeline.cpp - Interactive visual timeline for frame durations

#include "AnimationTimeline.h"
#include "PaperFlipbook.h"
#include "PaperSprite.h"
#include "Rendering/DrawElements.h"

#define LOCTEXT_NAMESPACE "AnimationTimeline"

// ==========================================
// FFlipbookTimingData Implementation
// ==========================================

FFlipbookTimingData FFlipbookTimingData::ReadFromFlipbook(UPaperFlipbook* Flipbook)
{
	FFlipbookTimingData Data;
	if (!Flipbook)
	{
		return Data;
	}

	Data.FPS = Flipbook->GetFramesPerSecond();
	Data.TotalFrames = Flipbook->GetNumKeyFrames();
	Data.FrameDurations.SetNum(Data.TotalFrames);

	for (int32 i = 0; i < Data.TotalFrames; i++)
	{
		const FPaperFlipbookKeyFrame& KeyFrame = Flipbook->GetKeyFrameChecked(i);
		Data.FrameDurations[i] = FMath::Max(KeyFrame.FrameRun, 1);
	}

	Data.Recalculate();
	return Data;
}

void FFlipbookTimingData::Recalculate()
{
	int32 TotalTicks = 0;
	for (int32 Dur : FrameDurations)
	{
		TotalTicks += Dur;
	}
	TotalDurationSeconds = (FPS > 0.0f) ? (TotalTicks / FPS) : 0.0f;
}

float FFlipbookTimingData::GetFrameStartTime(int32 FrameIndex) const
{
	if (FPS <= 0.0f) return 0.0f;

	int32 TicksBefore = 0;
	int32 ClampedIndex = FMath::Clamp(FrameIndex, 0, FrameDurations.Num());
	for (int32 i = 0; i < ClampedIndex; i++)
	{
		TicksBefore += FrameDurations[i];
	}
	return TicksBefore / FPS;
}

float FFlipbookTimingData::GetFrameDurationSeconds(int32 FrameIndex) const
{
	if (!FrameDurations.IsValidIndex(FrameIndex) || FPS <= 0.0f) return 0.0f;
	return FrameDurations[FrameIndex] / FPS;
}

float FFlipbookTimingData::FramesToMilliseconds(int32 Frames) const
{
	return (FPS > 0.0f) ? (Frames / FPS) * 1000.0f : 0.0f;
}

int32 FFlipbookTimingData::MillisecondsToFrames(float Milliseconds) const
{
	return (FPS > 0.0f) ? FMath::RoundToInt((Milliseconds / 1000.0f) * FPS) : 0;
}

// ==========================================
// SAnimationTimeline Implementation
// ==========================================

void SAnimationTimeline::Construct(const FArguments& InArgs)
{
	Flipbook = InArgs._Flipbook;
	SelectedFrameIndex = InArgs._SelectedFrameIndex;
	PlaybackPosition = InArgs._PlaybackPosition;
	IsPlaying = InArgs._IsPlaying;
	DisplayUnit = InArgs._DisplayUnit;

	RefreshTimingData();
}

void SAnimationTimeline::SetFlipbook(UPaperFlipbook* InFlipbook)
{
	Flipbook = InFlipbook;
	ScrollOffset = 0.0f;
	RefreshTimingData();
}

void SAnimationTimeline::RefreshTimingData()
{
	CachedTiming = FFlipbookTimingData::ReadFromFlipbook(Flipbook.Get());
}

FVector2D SAnimationTimeline::ComputeDesiredSize(float) const
{
	return FVector2D(400.0f, RulerHeight + FrameBlockHeight + 10.0f);
}

// ==========================================
// Color Coding
// ==========================================

FLinearColor SAnimationTimeline::GetFrameColor(int32 Duration)
{
	if (Duration <= 1) return FLinearColor(0.2f, 0.7f, 0.3f, 1.0f);       // Green - standard
	if (Duration == 2) return FLinearColor(0.85f, 0.75f, 0.1f, 1.0f);     // Yellow - slight hold
	if (Duration <= 4) return FLinearColor(0.9f, 0.5f, 0.1f, 1.0f);       // Orange - medium hold
	return FLinearColor(0.85f, 0.2f, 0.2f, 1.0f);                         // Red - long hold
}

// ==========================================
// Coordinate Helpers
// ==========================================

float SAnimationTimeline::GetFrameXPosition(int32 FrameIndex) const
{
	float X = 0.0f;
	int32 ClampedIndex = FMath::Clamp(FrameIndex, 0, CachedTiming.FrameDurations.Num());
	for (int32 i = 0; i < ClampedIndex; i++)
	{
		X += CachedTiming.FrameDurations[i] * BasePixelsPerFrame * ZoomFactor;
	}
	return X - ScrollOffset;
}

float SAnimationTimeline::GetFrameWidth(int32 FrameIndex) const
{
	if (!CachedTiming.FrameDurations.IsValidIndex(FrameIndex)) return 0.0f;
	return CachedTiming.FrameDurations[FrameIndex] * BasePixelsPerFrame * ZoomFactor;
}

float SAnimationTimeline::GetTotalTimelineWidth() const
{
	float Total = 0.0f;
	for (int32 Dur : CachedTiming.FrameDurations)
	{
		Total += Dur * BasePixelsPerFrame * ZoomFactor;
	}
	return Total;
}

int32 SAnimationTimeline::HitTestFrame(const FGeometry& Geom, const FVector2D& LocalPos) const
{
	if (LocalPos.Y < RulerHeight || LocalPos.Y > RulerHeight + FrameBlockHeight)
	{
		return -1;
	}

	float X = ScrollOffset + LocalPos.X;
	float Accumulated = 0.0f;
	for (int32 i = 0; i < CachedTiming.FrameDurations.Num(); i++)
	{
		float Width = CachedTiming.FrameDurations[i] * BasePixelsPerFrame * ZoomFactor;
		if (X >= Accumulated && X < Accumulated + Width)
		{
			return i;
		}
		Accumulated += Width;
	}
	return -1;
}

int32 SAnimationTimeline::HitTestDragHandle(const FGeometry& Geom, const FVector2D& LocalPos) const
{
	if (LocalPos.Y < RulerHeight || LocalPos.Y > RulerHeight + FrameBlockHeight)
	{
		return -1;
	}

	for (int32 i = 0; i < CachedTiming.FrameDurations.Num() - 1; i++)
	{
		float HandleX = GetFrameXPosition(i + 1);
		if (FMath::Abs(LocalPos.X - HandleX) <= DragHandleWidth)
		{
			return i; // Return the frame whose right edge this handle is on
		}
	}
	return -1;
}

// ==========================================
// Drawing
// ==========================================

int32 SAnimationTimeline::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	// Background
	FSlateDrawElement::MakeBox(
		OutDrawElements,
		LayerId,
		AllottedGeometry.ToPaintGeometry(),
		FAppStyle::GetBrush("WhiteBrush"),
		ESlateDrawEffect::None,
		FLinearColor(0.02f, 0.02f, 0.02f, 1.0f)
	);

	LayerId++;
	DrawRuler(AllottedGeometry, OutDrawElements, LayerId);
	LayerId++;
	DrawFrameBlocks(AllottedGeometry, OutDrawElements, LayerId);
	LayerId++;
	DrawDragHandles(AllottedGeometry, OutDrawElements, LayerId);
	LayerId++;
	DrawPlaybackCursor(AllottedGeometry, OutDrawElements, LayerId);

	return LayerId;
}

void SAnimationTimeline::DrawRuler(const FGeometry& Geom, FSlateWindowElementList& OutDrawElements, int32 LayerId) const
{
	const float GeomWidth = Geom.GetLocalSize().X;

	// Ruler background
	FSlateDrawElement::MakeBox(
		OutDrawElements,
		LayerId,
		Geom.ToPaintGeometry(FVector2D(GeomWidth, RulerHeight), FSlateLayoutTransform()),
		FAppStyle::GetBrush("WhiteBrush"),
		ESlateDrawEffect::None,
		FLinearColor(0.08f, 0.08f, 0.1f, 1.0f)
	);

	if (CachedTiming.FPS <= 0.0f) return;

	// Determine tick interval based on zoom
	float TickDuration = CachedTiming.GetTickDuration();
	float PixelsPerTick = BasePixelsPerFrame * ZoomFactor;

	// Draw time markers - every N ticks depending on zoom
	int32 TickStep = 1;
	if (PixelsPerTick < 15.0f) TickStep = 5;
	if (PixelsPerTick < 8.0f) TickStep = 10;

	int32 TotalTicks = 0;
	for (int32 Dur : CachedTiming.FrameDurations)
	{
		TotalTicks += Dur;
	}

	const FSlateFontInfo SmallFont = FCoreStyle::GetDefaultFontStyle("Regular", 7);

	for (int32 Tick = 0; Tick <= TotalTicks; Tick += TickStep)
	{
		float X = (Tick * PixelsPerTick) - ScrollOffset;
		if (X < -20.0f || X > GeomWidth + 20.0f) continue;

		// Tick line
		TArray<FVector2D> LinePoints;
		LinePoints.Add(FVector2D(X, RulerHeight - 6.0f));
		LinePoints.Add(FVector2D(X, RulerHeight));
		FSlateDrawElement::MakeLines(
			OutDrawElements,
			LayerId + 1,
			Geom.ToPaintGeometry(),
			LinePoints,
			ESlateDrawEffect::None,
			FLinearColor(0.4f, 0.4f, 0.4f, 1.0f),
			true,
			1.0f
		);

		// Time label
		float TimeSeconds = Tick * TickDuration;
		FString Label = FString::Printf(TEXT("%.2fs"), TimeSeconds);
		FSlateDrawElement::MakeText(
			OutDrawElements,
			LayerId + 2,
			Geom.ToPaintGeometry(FVector2D(50.0f, RulerHeight), FSlateLayoutTransform(FVector2D(X + 2.0f, 1.0f))),
			Label,
			SmallFont,
			ESlateDrawEffect::None,
			FLinearColor(0.6f, 0.6f, 0.6f, 1.0f)
		);
	}
}

void SAnimationTimeline::DrawFrameBlocks(const FGeometry& Geom, FSlateWindowElementList& OutDrawElements, int32 LayerId) const
{
	const float GeomWidth = Geom.GetLocalSize().X;
	const FSlateFontInfo LabelFont = FCoreStyle::GetDefaultFontStyle("Bold", 8);
	const FSlateFontInfo SmallFont = FCoreStyle::GetDefaultFontStyle("Regular", 7);
	const int32 SelIdx = SelectedFrameIndex.Get(-1);
	const ETimingDisplayUnit Unit = DisplayUnit.Get(ETimingDisplayUnit::Frames);

	for (int32 i = 0; i < CachedTiming.FrameDurations.Num(); i++)
	{
		float X = GetFrameXPosition(i);
		float Width = GetFrameWidth(i);

		// Skip if off-screen
		if (X + Width < 0.0f || X > GeomWidth) continue;

		bool bSelected = (i == SelIdx);
		int32 Duration = CachedTiming.FrameDurations[i];
		FLinearColor BlockColor = GetFrameColor(Duration);

		// Darken slightly for even frames for visual separation
		if (i % 2 == 0)
		{
			BlockColor *= 0.85f;
			BlockColor.A = 1.0f;
		}

		// Selected highlight
		if (bSelected)
		{
			BlockColor = FLinearColor::LerpUsingHSV(BlockColor, FLinearColor::White, 0.3f);
		}

		// Frame block
		FSlateDrawElement::MakeBox(
			OutDrawElements,
			LayerId,
			Geom.ToPaintGeometry(FVector2D(FMath::Max(Width - 1.0f, 1.0f), FrameBlockHeight), FSlateLayoutTransform(FVector2D(X, RulerHeight))),
			FAppStyle::GetBrush("WhiteBrush"),
			ESlateDrawEffect::None,
			BlockColor
		);

		// Selection border
		if (bSelected)
		{
			// Top
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 1,
				Geom.ToPaintGeometry(FVector2D(Width, 2.0f), FSlateLayoutTransform(FVector2D(X, RulerHeight))),
				FAppStyle::GetBrush("WhiteBrush"), ESlateDrawEffect::None, FLinearColor::White);
			// Bottom
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 1,
				Geom.ToPaintGeometry(FVector2D(Width, 2.0f), FSlateLayoutTransform(FVector2D(X, RulerHeight + FrameBlockHeight - 2.0f))),
				FAppStyle::GetBrush("WhiteBrush"), ESlateDrawEffect::None, FLinearColor::White);
			// Left
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 1,
				Geom.ToPaintGeometry(FVector2D(2.0f, FrameBlockHeight), FSlateLayoutTransform(FVector2D(X, RulerHeight))),
				FAppStyle::GetBrush("WhiteBrush"), ESlateDrawEffect::None, FLinearColor::White);
			// Right
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 1,
				Geom.ToPaintGeometry(FVector2D(2.0f, FrameBlockHeight), FSlateLayoutTransform(FVector2D(X + Width - 2.0f, RulerHeight))),
				FAppStyle::GetBrush("WhiteBrush"), ESlateDrawEffect::None, FLinearColor::White);
		}

		// Frame label (only if wide enough)
		if (Width > 20.0f)
		{
			// Frame number
			FString FrameLabel = FString::Printf(TEXT("F%d"), i + 1);
			FSlateDrawElement::MakeText(
				OutDrawElements,
				LayerId + 2,
				Geom.ToPaintGeometry(FVector2D(Width - 4.0f, 14.0f), FSlateLayoutTransform(FVector2D(X + 3.0f, RulerHeight + 3.0f))),
				FrameLabel,
				LabelFont,
				ESlateDrawEffect::None,
				FLinearColor(1.0f, 1.0f, 1.0f, 0.9f)
			);

			// Duration value
			FString DurationLabel;
			if (Unit == ETimingDisplayUnit::Milliseconds)
			{
				float Ms = CachedTiming.FramesToMilliseconds(Duration);
				DurationLabel = FString::Printf(TEXT("%.0fms"), Ms);
			}
			else
			{
				DurationLabel = FString::Printf(TEXT("%d"), Duration);
			}

			FSlateDrawElement::MakeText(
				OutDrawElements,
				LayerId + 2,
				Geom.ToPaintGeometry(FVector2D(Width - 4.0f, 14.0f), FSlateLayoutTransform(FVector2D(X + 3.0f, RulerHeight + 18.0f))),
				DurationLabel,
				SmallFont,
				ESlateDrawEffect::None,
				FLinearColor(1.0f, 1.0f, 1.0f, 0.7f)
			);
		}

		// Percentage bar at bottom of block
		if (Width > 10.0f && CachedTiming.TotalDurationSeconds > 0.0f)
		{
			float Percentage = CachedTiming.GetFrameDurationSeconds(i) / CachedTiming.TotalDurationSeconds;
			float BarHeight = 4.0f;
			float BarY = RulerHeight + FrameBlockHeight - BarHeight - 2.0f;

			FSlateDrawElement::MakeBox(
				OutDrawElements,
				LayerId + 2,
				Geom.ToPaintGeometry(FVector2D((Width - 6.0f) * Percentage, BarHeight), FSlateLayoutTransform(FVector2D(X + 3.0f, BarY))),
				FAppStyle::GetBrush("WhiteBrush"),
				ESlateDrawEffect::None,
				FLinearColor(1.0f, 1.0f, 1.0f, 0.3f)
			);
		}
	}
}

void SAnimationTimeline::DrawDragHandles(const FGeometry& Geom, FSlateWindowElementList& OutDrawElements, int32 LayerId) const
{
	for (int32 i = 0; i < CachedTiming.FrameDurations.Num() - 1; i++)
	{
		float HandleX = GetFrameXPosition(i + 1);
		float GeomWidth = Geom.GetLocalSize().X;
		if (HandleX < 0.0f || HandleX > GeomWidth) continue;

		bool bIsActiveHandle = (bIsDraggingHandle && DragHandleFrameIndex == i);
		FLinearColor HandleColor = bIsActiveHandle
			? FLinearColor(1.0f, 0.8f, 0.2f, 0.9f)
			: FLinearColor(0.6f, 0.6f, 0.6f, 0.5f);

		FSlateDrawElement::MakeBox(
			OutDrawElements,
			LayerId,
			Geom.ToPaintGeometry(
				FVector2D(2.0f, FrameBlockHeight),
				FSlateLayoutTransform(FVector2D(HandleX - 1.0f, RulerHeight))
			),
			FAppStyle::GetBrush("WhiteBrush"),
			ESlateDrawEffect::None,
			HandleColor
		);
	}
}

void SAnimationTimeline::DrawPlaybackCursor(const FGeometry& Geom, FSlateWindowElementList& OutDrawElements, int32 LayerId) const
{
	float Position = PlaybackPosition.Get(0.0f);
	if (CachedTiming.TotalDurationSeconds <= 0.0f) return;

	// Convert time to pixel position
	float PixelsPerTick = BasePixelsPerFrame * ZoomFactor;
	float TimeToTicks = Position * CachedTiming.FPS;
	float CursorX = (TimeToTicks * PixelsPerTick) - ScrollOffset;

	float GeomWidth = Geom.GetLocalSize().X;
	if (CursorX < 0.0f || CursorX > GeomWidth) return;

	// Cursor line
	TArray<FVector2D> CursorLine;
	CursorLine.Add(FVector2D(CursorX, 0.0f));
	CursorLine.Add(FVector2D(CursorX, RulerHeight + FrameBlockHeight));
	FSlateDrawElement::MakeLines(
		OutDrawElements,
		LayerId,
		Geom.ToPaintGeometry(),
		CursorLine,
		ESlateDrawEffect::None,
		FLinearColor(1.0f, 0.3f, 0.3f, 0.9f),
		true,
		2.0f
	);

	// Cursor head (triangle at top)
	float TriSize = 6.0f;
	FSlateDrawElement::MakeBox(
		OutDrawElements,
		LayerId,
		Geom.ToPaintGeometry(FVector2D(TriSize * 2, TriSize), FSlateLayoutTransform(FVector2D(CursorX - TriSize, 0.0f))),
		FAppStyle::GetBrush("WhiteBrush"),
		ESlateDrawEffect::None,
		FLinearColor(1.0f, 0.3f, 0.3f, 0.9f)
	);
}

// ==========================================
// Input Handling
// ==========================================

FReply SAnimationTimeline::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	FVector2D LocalPos = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());

	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		// Check for drag handle first
		int32 HandleIdx = HitTestDragHandle(MyGeometry, LocalPos);
		if (HandleIdx >= 0)
		{
			bIsDraggingHandle = true;
			DragHandleFrameIndex = HandleIdx;
			DragStartX = LocalPos.X;
			DragStartDuration = CachedTiming.FrameDurations[HandleIdx];
			return FReply::Handled().CaptureMouse(SharedThis(this));
		}

		// Check for frame click
		int32 FrameIdx = HitTestFrame(MyGeometry, LocalPos);
		if (FrameIdx >= 0)
		{
			OnFrameSelected.ExecuteIfBound(FrameIdx);
			return FReply::Handled();
		}
	}
	else if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		// Start panning
		bIsPanning = true;
		PanStartX = LocalPos.X;
		PanStartScrollOffset = ScrollOffset;
		return FReply::Handled().CaptureMouse(SharedThis(this));
	}

	return FReply::Unhandled();
}

FReply SAnimationTimeline::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (bIsDraggingHandle)
	{
		bIsDraggingHandle = false;
		DragHandleFrameIndex = -1;
		return FReply::Handled().ReleaseMouseCapture();
	}

	if (bIsPanning)
	{
		bIsPanning = false;
		return FReply::Handled().ReleaseMouseCapture();
	}

	return FReply::Unhandled();
}

FReply SAnimationTimeline::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	FVector2D LocalPos = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());

	if (bIsDraggingHandle && DragHandleFrameIndex >= 0)
	{
		float DeltaX = LocalPos.X - DragStartX;
		float PixelsPerFrame = BasePixelsPerFrame * ZoomFactor;
		int32 DeltaFrames = FMath::RoundToInt(DeltaX / PixelsPerFrame);
		int32 NewDuration = FMath::Clamp(DragStartDuration + DeltaFrames, 1, 999);

		if (NewDuration != CachedTiming.FrameDurations[DragHandleFrameIndex])
		{
			OnFrameDurationChanged.ExecuteIfBound(DragHandleFrameIndex, NewDuration);
		}
		return FReply::Handled();
	}

	if (bIsPanning)
	{
		float DeltaX = PanStartX - LocalPos.X;
		ScrollOffset = FMath::Max(0.0f, PanStartScrollOffset + DeltaX);
		return FReply::Handled();
	}

	return FReply::Unhandled();
}

FReply SAnimationTimeline::OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	float Delta = MouseEvent.GetWheelDelta();

	if (MouseEvent.IsControlDown())
	{
		// Zoom
		float OldZoom = ZoomFactor;
		float NewZoom = FMath::Clamp(ZoomFactor * (Delta > 0 ? 1.15f : 0.87f), MinZoom, MaxZoom);

		// Zoom around mouse cursor position
		FVector2D LocalPos = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
		float WorldX = ScrollOffset + LocalPos.X;
		float Ratio = NewZoom / OldZoom;
		ScrollOffset = FMath::Max(0.0f, WorldX * Ratio - LocalPos.X);

		ZoomFactor = NewZoom;
		OnZoomChanged.ExecuteIfBound(ZoomFactor);
		return FReply::Handled();
	}
	else
	{
		// Scroll horizontally
		ScrollOffset = FMath::Max(0.0f, ScrollOffset - Delta * 30.0f);
		return FReply::Handled();
	}
}

FReply SAnimationTimeline::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	FKey Key = InKeyEvent.GetKey();
	int32 SelIdx = SelectedFrameIndex.Get(-1);

	if (Key == EKeys::Left && SelIdx > 0)
	{
		OnFrameSelected.ExecuteIfBound(SelIdx - 1);
		return FReply::Handled();
	}
	if (Key == EKeys::Right && SelIdx < CachedTiming.FrameDurations.Num() - 1)
	{
		OnFrameSelected.ExecuteIfBound(SelIdx + 1);
		return FReply::Handled();
	}

	// Increase/decrease duration with bracket keys or +/-
	if (SelIdx >= 0 && SelIdx < CachedTiming.FrameDurations.Num())
	{
		if (Key == EKeys::RightBracket || Key == EKeys::Equals)
		{
			int32 NewDur = FMath::Min(CachedTiming.FrameDurations[SelIdx] + 1, 999);
			OnFrameDurationChanged.ExecuteIfBound(SelIdx, NewDur);
			return FReply::Handled();
		}
		if (Key == EKeys::LeftBracket || Key == EKeys::Hyphen)
		{
			int32 NewDur = FMath::Max(CachedTiming.FrameDurations[SelIdx] - 1, 1);
			OnFrameDurationChanged.ExecuteIfBound(SelIdx, NewDur);
			return FReply::Handled();
		}
	}

	if (Key == EKeys::Home)
	{
		OnFrameSelected.ExecuteIfBound(0);
		return FReply::Handled();
	}
	if (Key == EKeys::End && CachedTiming.FrameDurations.Num() > 0)
	{
		OnFrameSelected.ExecuteIfBound(CachedTiming.FrameDurations.Num() - 1);
		return FReply::Handled();
	}

	return FReply::Unhandled();
}

void SAnimationTimeline::OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent)
{
	bIsDraggingHandle = false;
	DragHandleFrameIndex = -1;
	bIsPanning = false;

	Invalidate(EInvalidateWidgetReason::Paint);
}

FCursorReply SAnimationTimeline::OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const
{
	FVector2D LocalPos = MyGeometry.AbsoluteToLocal(CursorEvent.GetScreenSpacePosition());
	int32 HandleIdx = HitTestDragHandle(MyGeometry, LocalPos);
	if (HandleIdx >= 0)
	{
		return FCursorReply::Cursor(EMouseCursor::ResizeLeftRight);
	}
	return FCursorReply::Unhandled();
}

#undef LOCTEXT_NAMESPACE
