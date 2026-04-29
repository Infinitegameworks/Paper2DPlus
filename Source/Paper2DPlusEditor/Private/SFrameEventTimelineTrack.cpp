// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "SFrameEventTimelineTrack.h"
#include "EditorCanvasUtils.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "FrameEvents/Paper2DPlusFrameEventBase.h"
#include "FrameEvents/Paper2DPlusFrameEvent.h"
#include "FrameEvents/Paper2DPlusFrameEventState.h"
#include "FrameEvents/Paper2DPlusSpawnEffectFrameEvent.h"
#include "PaperFlipbook.h"
#include "PaperSprite.h"
#include "Styling/AppStyle.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"

#define LOCTEXT_NAMESPACE "FrameEventTimelineTrack"

void SFrameEventTimelineTrack::Construct(const FArguments& InArgs)
{
	Asset = InArgs._Asset;
	SelectedFlipbookIndex = InArgs._SelectedFlipbookIndex;
	SelectedEventIndex = InArgs._SelectedEventIndex;
	SelectedFrameIndex = InArgs._SelectedFrameIndex;
	SetClipping(EWidgetClipping::ClipToBounds);
}

FVector2D SFrameEventTimelineTrack::ComputeDesiredSize(float LayoutScaleMultiplier) const
{
	int32 EventCount = 0;
	int32 FrameCount = 0;
	if (Asset.IsValid())
	{
		int32 FBIdx = SelectedFlipbookIndex.Get(INDEX_NONE);
		if (Asset->Flipbooks.IsValidIndex(FBIdx))
		{
			EventCount = Asset->Flipbooks[FBIdx].FrameEventData.FrameEvents.Num();
			UPaperFlipbook* FB = !Asset->Flipbooks[FBIdx].Identity.Flipbook.IsNull()
				? Asset->Flipbooks[FBIdx].Identity.Flipbook.LoadSynchronous() : nullptr;
			FrameCount = FB ? FB->GetNumKeyFrames() : 0;
		}
	}

	float TotalWidth = LabelWidth + GetTotalColumns() * ColumnWidth;
	if (EventCount == 0) return FVector2D(TotalWidth, 0.0f);
	int32 Idx = SelectedFlipbookIndex.Get(INDEX_NONE);
	float TotalHeight = Asset.IsValid() && Asset->Flipbooks.IsValidIndex(Idx)
		? GetTotalEventsHeight(Asset->Flipbooks[Idx].FrameEventData.FrameEvents) : 0.0f;
	return FVector2D(TotalWidth, FMath::Max(TotalHeight, 1.0f));
}

int32 SFrameEventTimelineTrack::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
	int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const FSlateBrush* WhiteBrush = FAppStyle::GetBrush("WhiteBrush");
	if (!Asset.IsValid()) return LayerId;

	int32 FBIdx = SelectedFlipbookIndex.Get(INDEX_NONE);
	if (!Asset->Flipbooks.IsValidIndex(FBIdx)) return LayerId;

	const FFlipbookProfileEntry& Anim = Asset->Flipbooks[FBIdx];
	const int32 CharFrames = GetCharacterFrameCount();
	const int32 TotalCols = GetTotalColumns();
	const int32 SelFrame = SelectedFrameIndex.Get(0);
	const int32 SelEvent = SelectedEventIndex.Get(INDEX_NONE);
	const TArray<TObjectPtr<UPaper2DPlusFrameEventBase>>& Events = Anim.FrameEventData.FrameEvents;

	// -- Background for overflow area (past character end) --
	if (TotalCols > CharFrames)
	{
		float OverflowX = LabelWidth + CharFrames * ColumnWidth;
		float OverflowW = (TotalCols - CharFrames) * ColumnWidth;
		float TotalH = AllottedGeometry.GetLocalSize().Y;
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId,
			MakePaintGeometry(AllottedGeometry, FVector2D(OverflowW, TotalH), FSlateLayoutTransform(FVector2D(OverflowX, 0))),
			WhiteBrush, ESlateDrawEffect::None, FLinearColor(0.08f, 0.06f, 0.06f, 0.5f));
	}

	// -- Column grid lines --
	for (int32 Col = 0; Col <= TotalCols; Col++)
	{
		float X = LabelWidth + Col * ColumnWidth;
		FLinearColor LineColor = (Col == CharFrames)
			? FLinearColor(0.5f, 0.2f, 0.2f, 0.6f)
			: FLinearColor(0.2f, 0.2f, 0.2f, 0.3f);
		float Thickness = (Col == CharFrames) ? 2.0f : 1.0f;
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId,
			MakePaintGeometry(AllottedGeometry, FVector2D(Thickness, AllottedGeometry.GetLocalSize().Y), FSlateLayoutTransform(FVector2D(X, 0))),
			WhiteBrush, ESlateDrawEffect::None, LineColor);
	}

	// -- Event tracks with inline label rows --
	for (int32 EvIdx = 0; EvIdx < Events.Num(); EvIdx++)
	{
		const UPaper2DPlusFrameEventBase* Event = Events[EvIdx];
		if (!Event) continue;

		float RowY = GetEventTrackY(Events, EvIdx);
		float ThisTrackHeight = GetEventHeight(Event);
		float TrackY = RowY + LabelRowHeight;
		bool bIsSelectedEvent = (EvIdx == SelEvent);
		bool bIsCurrentlyHovered = (EvIdx == HoveredEventIndex);

		// Label row background + text (above the bar)
		FString LabelStr = Event->DebugName.IsNone()
			? Event->GetClass()->GetDisplayNameText().ToString()
			: Event->DebugName.ToString();

		// Measure text width for snug background
		const FSlateFontInfo LabelFont = FCoreStyle::GetDefaultFontStyle("Regular", 7);
		const TSharedRef<FSlateFontMeasure> FontMeasure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
		FVector2D TextSize = FontMeasure->Measure(LabelStr, LabelFont);
		float BgWidth = TextSize.X + 8.0f; // 4px padding each side

		FLinearColor LabelBgColor(0.12f, 0.12f, 0.12f, 0.9f);
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId,
			MakePaintGeometry(AllottedGeometry, FVector2D(BgWidth, LabelRowHeight), FSlateLayoutTransform(FVector2D(0, RowY))),
			WhiteBrush, ESlateDrawEffect::None, LabelBgColor);

		FLinearColor LabelColor = bIsSelectedEvent ? FLinearColor(0.9f, 0.9f, 0.9f) : FLinearColor(0.5f, 0.5f, 0.5f);
		FSlateDrawElement::MakeText(OutDrawElements, LayerId + 1,
			MakePaintGeometry(AllottedGeometry, FVector2D(TextSize.X, LabelRowHeight), FSlateLayoutTransform(FVector2D(4, RowY + 1))),
			LabelStr, LabelFont,
			ESlateDrawEffect::None, LabelColor);

		// Determine bar position and width
		int32 StartFrame = 0;
		int32 BarFrameCount = 1;
		GetEventFrameRange(Event, StartFrame, BarFrameCount);

		float BarX = StartFrame * ColumnWidth;
		float BarW = BarFrameCount * ColumnWidth;
		if (BarW < 4.0f) BarW = 4.0f;

		// Bar color from event's Color property
		FLinearColor BarColor = Event->Color;
		BarColor.A = 0.8f;
		if (bIsSelectedEvent) BarColor = BarColor * 1.3f;
		if (!bIsCurrentlyHovered && !bIsSelectedEvent) BarColor.A = 0.6f;

		// Bar background
		FVector2D BarPos(BarX, TrackY + 1);
		FVector2D BarSize(BarW, ThisTrackHeight - 2);
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 1,
			MakePaintGeometry(AllottedGeometry, BarSize, FSlateLayoutTransform(BarPos)),
			WhiteBrush, ESlateDrawEffect::None, BarColor);

		// Right-edge resize handle indicator for ranged events
		if (BarFrameCount > 1 && (bIsSelectedEvent || bIsCurrentlyHovered))
		{
			float HandleX = BarPos.X + BarSize.X - ResizeHandleWidth;
			FLinearColor HandleColor(1.0f, 1.0f, 1.0f, 0.3f);
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 1,
				MakePaintGeometry(AllottedGeometry, FVector2D(ResizeHandleWidth, BarSize.Y), FSlateLayoutTransform(FVector2D(HandleX, BarPos.Y))),
				WhiteBrush, ESlateDrawEffect::None, HandleColor);
		}

		// Draw effect flipbook sprites inside the bar
		if (const UPaper2DPlusSpawnEffectFrameEvent* SpawnEffect = Cast<UPaper2DPlusSpawnEffectFrameEvent>(Event))
		{
			if (SpawnEffect->EffectFlipbook)
			{
				int32 EffectFrameCount = SpawnEffect->EffectFlipbook->GetNumKeyFrames();
				float SpriteDim = FMath::Min(ThisTrackHeight - 6.0f, ColumnWidth - 4.0f);

				for (int32 F = 0; F < EffectFrameCount; F++)
				{
					UPaperSprite* Sprite = SpawnEffect->EffectFlipbook->GetKeyFrameChecked(F).Sprite;
					if (!Sprite) continue;

					UTexture2D* Tex = Sprite->GetBakedTexture();
					if (!Tex) Tex = Cast<UTexture2D>(Sprite->GetSourceTexture());
					if (!Tex) continue;

					float SpriteX = BarX + F * ColumnWidth + (ColumnWidth - SpriteDim) * 0.5f;
					float SpriteY = TrackY + (ThisTrackHeight - SpriteDim) * 0.5f;

					FSlateBrush SpriteBrush;
					SpriteBrush.SetResourceObject(Tex);
					SpriteBrush.ImageSize = FVector2D(Tex->GetSizeX(), Tex->GetSizeY());
					SpriteBrush.DrawAs = ESlateBrushDrawType::Image;
					SpriteBrush.Tiling = ESlateBrushTileType::NoTile;

					FVector2D SourceUV = Sprite->GetSourceUV();
					FVector2D SourceSize = Sprite->GetSourceSize();
					FVector2D TexSize(Tex->GetSizeX(), Tex->GetSizeY());
					if (TexSize.X > 0 && TexSize.Y > 0)
					{
						SpriteBrush.SetUVRegion(FBox2D(
							FVector2D(SourceUV.X / TexSize.X, SourceUV.Y / TexSize.Y),
							FVector2D((SourceUV.X + SourceSize.X) / TexSize.X, (SourceUV.Y + SourceSize.Y) / TexSize.Y)));
					}

					FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 2,
						MakePaintGeometry(AllottedGeometry,
							FVector2D(SpriteDim, SpriteDim),
							FSlateLayoutTransform(FVector2D(SpriteX, SpriteY))),
						&SpriteBrush, ESlateDrawEffect::None, FLinearColor::White);
				}
			}
		}

		// Selection outline (4 edge boxes)
		if (bIsSelectedEvent)
		{
			FLinearColor OutlineColor(1.0f, 1.0f, 1.0f, 0.8f);
			float T = 1.5f;
			// Top
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 2, MakePaintGeometry(AllottedGeometry, FVector2D(BarSize.X, T), FSlateLayoutTransform(BarPos)), WhiteBrush, ESlateDrawEffect::None, OutlineColor);
			// Bottom
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 2, MakePaintGeometry(AllottedGeometry, FVector2D(BarSize.X, T), FSlateLayoutTransform(FVector2D(BarPos.X, BarPos.Y + BarSize.Y - T))), WhiteBrush, ESlateDrawEffect::None, OutlineColor);
			// Left
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 2, MakePaintGeometry(AllottedGeometry, FVector2D(T, BarSize.Y), FSlateLayoutTransform(BarPos)), WhiteBrush, ESlateDrawEffect::None, OutlineColor);
			// Right
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 2, MakePaintGeometry(AllottedGeometry, FVector2D(T, BarSize.Y), FSlateLayoutTransform(FVector2D(BarPos.X + BarSize.X - T, BarPos.Y))), WhiteBrush, ESlateDrawEffect::None, OutlineColor);
		}
	}

	// -- Playhead (selected frame) --
	{
		float PlayheadX = LabelWidth + SelFrame * ColumnWidth + ColumnWidth * 0.5f;
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 3,
			MakePaintGeometry(AllottedGeometry, FVector2D(2, AllottedGeometry.GetLocalSize().Y), FSlateLayoutTransform(FVector2D(PlayheadX - 1, 0))),
			WhiteBrush, ESlateDrawEffect::None, FLinearColor(0.9f, 0.9f, 0.9f, 0.6f));
	}

	return LayerId + 4;
}

FReply SFrameEventTimelineTrack::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	FVector2D LocalPos = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());

	if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		int32 HitEvent = HitTestEventBar(MyGeometry, LocalPos);
		if (HitEvent != INDEX_NONE)
		{
			OnEventSelected.ExecuteIfBound(HitEvent);
			ShowEventContextMenu(MyGeometry, MouseEvent, HitEvent);
			return FReply::Handled();
		}
		return FReply::Unhandled();
	}

	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton) return FReply::Unhandled();

	// Check if clicking on an event bar
	int32 HitEvent = HitTestEventBar(MyGeometry, LocalPos);
	if (HitEvent != INDEX_NONE)
	{
		OnEventSelected.ExecuteIfBound(HitEvent);

		// Check if clicking on right-edge resize handle
		bool bOnResizeHandle = HitTestResizeHandle(MyGeometry, LocalPos, HitEvent);

		if (bOnResizeHandle)
		{
			// Start resize drag
			bResizingEvent = true;
			DragEventIndex = HitEvent;
			DragStartX = LocalPos.X;

			int32 FBIdx = SelectedFlipbookIndex.Get(INDEX_NONE);
			if (Asset.IsValid() && Asset->Flipbooks.IsValidIndex(FBIdx))
			{
				const TArray<TObjectPtr<UPaper2DPlusFrameEventBase>>& Events = Asset->Flipbooks[FBIdx].FrameEventData.FrameEvents;
				if (Events.IsValidIndex(HitEvent) && Events[HitEvent])
				{
					int32 Start = 0;
					int32 Count = 1;
					GetEventFrameRange(Events[HitEvent], Start, Count);
					DragStartFrameCount = Count;
				}
			}

			OnEventDragStarted.ExecuteIfBound();
		}
		else
		{
			// Start move drag
			bDraggingEvent = true;
			DragEventIndex = HitEvent;
			DragStartX = LocalPos.X;

			int32 FBIdx = SelectedFlipbookIndex.Get(INDEX_NONE);
			if (Asset.IsValid() && Asset->Flipbooks.IsValidIndex(FBIdx))
			{
				const TArray<TObjectPtr<UPaper2DPlusFrameEventBase>>& Events = Asset->Flipbooks[FBIdx].FrameEventData.FrameEvents;
				if (Events.IsValidIndex(HitEvent) && Events[HitEvent])
				{
					int32 Start = 0;
					int32 Count = 1;
					GetEventFrameRange(Events[HitEvent], Start, Count);
					DragStartFrame = Start;
				}
			}

			OnEventDragStarted.ExecuteIfBound();
		}

		return FReply::Handled().CaptureMouse(SharedThis(this));
	}

	return FReply::Unhandled();
}

FReply SFrameEventTimelineTrack::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if ((bDraggingEvent || bResizingEvent) && MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		bDraggingEvent = false;
		bResizingEvent = false;
		DragEventIndex = INDEX_NONE;
		OnEventDragEnded.ExecuteIfBound();
		return FReply::Handled().ReleaseMouseCapture();
	}
	return FReply::Unhandled();
}

FReply SFrameEventTimelineTrack::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	FVector2D LocalPos = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());

	if (bResizingEvent && DragEventIndex != INDEX_NONE)
	{
		float DeltaX = LocalPos.X - DragStartX;
		int32 FrameDelta = FMath::RoundToInt32(DeltaX / ColumnWidth);
		int32 NewCount = FMath::Max(1, DragStartFrameCount + FrameDelta);

		OnEventDurationChanged.ExecuteIfBound(DragEventIndex, NewCount);
		Invalidate(EInvalidateWidgetReason::Paint);
		return FReply::Handled();
	}

	if (bDraggingEvent && DragEventIndex != INDEX_NONE)
	{
		float DeltaX = LocalPos.X - DragStartX;
		int32 FrameDelta = FMath::RoundToInt32(DeltaX / ColumnWidth);
		int32 NewFrame = FMath::Max(0, DragStartFrame + FrameDelta);

		// Clamp to character frame count
		int32 MaxFrame = FMath::Max(0, GetCharacterFrameCount() - 1);
		NewFrame = FMath::Min(NewFrame, MaxFrame);

		OnEventFrameChanged.ExecuteIfBound(DragEventIndex, NewFrame);
		Invalidate(EInvalidateWidgetReason::Paint);
		return FReply::Handled();
	}

	// Hover tracking
	int32 NewHovered = HitTestEventBar(MyGeometry, LocalPos);
	bool bNewHoverResize = false;
	if (NewHovered != INDEX_NONE)
	{
		bNewHoverResize = HitTestResizeHandle(MyGeometry, LocalPos, NewHovered);
	}
	if (NewHovered != HoveredEventIndex || bNewHoverResize != bHoveringResizeHandle)
	{
		HoveredEventIndex = NewHovered;
		bHoveringResizeHandle = bNewHoverResize;
		Invalidate(EInvalidateWidgetReason::Paint);
	}

	return FReply::Unhandled();
}

void SFrameEventTimelineTrack::OnMouseLeave(const FPointerEvent& MouseEvent)
{
	if (HoveredEventIndex != INDEX_NONE)
	{
		HoveredEventIndex = INDEX_NONE;
		Invalidate(EInvalidateWidgetReason::Paint);
	}
}

void SFrameEventTimelineTrack::OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent)
{
	if (bDraggingEvent || bResizingEvent)
	{
		bDraggingEvent = false;
		bResizingEvent = false;
		DragEventIndex = INDEX_NONE;
		OnEventDragEnded.ExecuteIfBound();
	}
}

FCursorReply SFrameEventTimelineTrack::OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const
{
	if (bResizingEvent) return FCursorReply::Cursor(EMouseCursor::ResizeLeftRight);
	if (bDraggingEvent) return FCursorReply::Cursor(EMouseCursor::GrabHandClosed);
	if (HoveredEventIndex != INDEX_NONE)
	{
		if (bHoveringResizeHandle) return FCursorReply::Cursor(EMouseCursor::ResizeLeftRight);
		return FCursorReply::Cursor(EMouseCursor::GrabHand);
	}
	return FCursorReply::Unhandled();
}

// --- Helpers ----------------------------------------------------------------

int32 SFrameEventTimelineTrack::GetCharacterFrameCount() const
{
	if (!Asset.IsValid()) return 0;
	int32 FBIdx = SelectedFlipbookIndex.Get(INDEX_NONE);
	if (!Asset->Flipbooks.IsValidIndex(FBIdx)) return 0;
	UPaperFlipbook* FB = !Asset->Flipbooks[FBIdx].Identity.Flipbook.IsNull()
		? Asset->Flipbooks[FBIdx].Identity.Flipbook.LoadSynchronous() : nullptr;
	return FB ? FB->GetNumKeyFrames() : 0;
}

int32 SFrameEventTimelineTrack::GetTotalColumns() const
{
	int32 CharFrames = GetCharacterFrameCount();
	int32 MaxEnd = CharFrames;

	// Only extend past character frames when a spawn effect event has an
	// EffectFlipbook whose frames (starting from TriggerFrame) overflow
	if (Asset.IsValid())
	{
		int32 FBIdx = SelectedFlipbookIndex.Get(INDEX_NONE);
		if (Asset->Flipbooks.IsValidIndex(FBIdx))
		{
			for (const TObjectPtr<UPaper2DPlusFrameEventBase>& Event : Asset->Flipbooks[FBIdx].FrameEventData.FrameEvents)
			{
				if (!Event) continue;

				if (const UPaper2DPlusSpawnEffectFrameEvent* SpawnEffect = Cast<UPaper2DPlusSpawnEffectFrameEvent>(Event))
				{
					if (SpawnEffect->EffectFlipbook)
					{
						int32 EffectFrames = SpawnEffect->EffectFlipbook->GetNumKeyFrames();
						MaxEnd = FMath::Max(MaxEnd, SpawnEffect->TriggerFrame + EffectFrames);
					}
				}
			}
		}
	}

	return MaxEnd;
}

bool SFrameEventTimelineTrack::EventHasFlipbook(const UPaper2DPlusFrameEventBase* Event)
{
	if (const UPaper2DPlusSpawnEffectFrameEvent* SpawnEffect = Cast<UPaper2DPlusSpawnEffectFrameEvent>(Event))
	{
		return SpawnEffect->EffectFlipbook != nullptr;
	}
	return false;
}

float SFrameEventTimelineTrack::GetEventHeight(const UPaper2DPlusFrameEventBase* Event)
{
	return EventHasFlipbook(Event) ? EventTrackHeightWithSprites : EventTrackHeight;
}

float SFrameEventTimelineTrack::GetEventTrackY(const TArray<TObjectPtr<UPaper2DPlusFrameEventBase>>& Events, int32 EventIdx) const
{
	float Y = 0.0f;
	for (int32 i = 0; i < EventIdx && i < Events.Num(); i++)
	{
		float H = Events[i] ? GetEventHeight(Events[i]) : EventTrackHeight;
		Y += LabelRowHeight + H + TrackPadding;
	}
	return Y;
}

float SFrameEventTimelineTrack::GetTotalEventsHeight(const TArray<TObjectPtr<UPaper2DPlusFrameEventBase>>& Events) const
{
	float H = 0.0f;
	for (const auto& Event : Events)
	{
		float TrackH = Event ? GetEventHeight(Event) : EventTrackHeight;
		H += LabelRowHeight + TrackH + TrackPadding;
	}
	return H;
}

int32 SFrameEventTimelineTrack::FrameFromX(const FGeometry& Geom, float LocalX) const
{
	float ContentX = LocalX - LabelWidth;
	if (ContentX < 0) return INDEX_NONE;
	return FMath::FloorToInt32(ContentX / ColumnWidth);
}

void SFrameEventTimelineTrack::GetEventFrameRange(const UPaper2DPlusFrameEventBase* Event, int32& OutStart, int32& OutCount)
{
	if (const UPaper2DPlusFrameEventState* Ranged = Cast<UPaper2DPlusFrameEventState>(Event))
	{
		OutStart = Ranged->StartFrame;
		OutCount = FMath::Max(1, Ranged->FrameCount);
	}
	else if (const UPaper2DPlusFrameEvent* OneShot = Cast<UPaper2DPlusFrameEvent>(Event))
	{
		OutStart = OneShot->TriggerFrame;
		OutCount = 1;
	}
	else
	{
		OutStart = 0;
		OutCount = 1;
	}
}

int32 SFrameEventTimelineTrack::HitTestEventBar(const FGeometry& Geom, const FVector2D& LocalPos) const
{
	if (!Asset.IsValid()) return INDEX_NONE;
	int32 FBIdx = SelectedFlipbookIndex.Get(INDEX_NONE);
	if (!Asset->Flipbooks.IsValidIndex(FBIdx)) return INDEX_NONE;

	const TArray<TObjectPtr<UPaper2DPlusFrameEventBase>>& Events = Asset->Flipbooks[FBIdx].FrameEventData.FrameEvents;

	// Reverse iterate for front-to-back (last event drawn on top)
	for (int32 EvIdx = Events.Num() - 1; EvIdx >= 0; EvIdx--)
	{
		if (!Events[EvIdx]) continue;

		float RowY = GetEventTrackY(Events, EvIdx);
		float TrackY = RowY + LabelRowHeight;
		float TrackH = GetEventHeight(Events[EvIdx]);

		int32 Start = 0;
		int32 Count = 1;
		GetEventFrameRange(Events[EvIdx], Start, Count);
		float BarX = Start * ColumnWidth;
		float BarW = FMath::Max(4.0f, (float)Count * ColumnWidth);

		if (LocalPos.X >= BarX && LocalPos.X <= BarX + BarW &&
			LocalPos.Y >= TrackY && LocalPos.Y <= TrackY + TrackH)
		{
			return EvIdx;
		}
	}

	return INDEX_NONE;
}

bool SFrameEventTimelineTrack::HitTestResizeHandle(const FGeometry& Geom, const FVector2D& LocalPos, int32 EventIdx) const
{
	if (!Asset.IsValid()) return false;
	int32 FBIdx = SelectedFlipbookIndex.Get(INDEX_NONE);
	if (!Asset->Flipbooks.IsValidIndex(FBIdx)) return false;

	const TArray<TObjectPtr<UPaper2DPlusFrameEventBase>>& Events = Asset->Flipbooks[FBIdx].FrameEventData.FrameEvents;
	if (!Events.IsValidIndex(EventIdx) || !Events[EventIdx]) return false;

	// Only ranged events (FrameCount > 1) have resize handles
	int32 Start = 0;
	int32 Count = 1;
	GetEventFrameRange(Events[EventIdx], Start, Count);
	if (Count <= 1) return false;

	float BarX = Start * ColumnWidth;
	float BarW = (float)Count * ColumnWidth;
	float RightEdge = BarX + BarW;

	// Hit zone: ResizeHandleWidth pixels from the right edge
	return LocalPos.X >= (RightEdge - ResizeHandleWidth - 2.0f) && LocalPos.X <= RightEdge + 2.0f;
}

void SFrameEventTimelineTrack::ShowEventContextMenu(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent, int32 EventIdx)
{
	if (!Asset.IsValid()) return;
	int32 FBIdx = SelectedFlipbookIndex.Get(INDEX_NONE);
	if (!Asset->Flipbooks.IsValidIndex(FBIdx)) return;

	const TArray<TObjectPtr<UPaper2DPlusFrameEventBase>>& Events = Asset->Flipbooks[FBIdx].FrameEventData.FrameEvents;
	if (!Events.IsValidIndex(EventIdx) || !Events[EventIdx]) return;

	const UPaper2DPlusFrameEventBase* Event = Events[EventIdx];
	int32 Start = 0;
	int32 Count = 1;
	GetEventFrameRange(Event, Start, Count);

	FMenuBuilder MenuBuilder(true, nullptr);
	MenuBuilder.BeginSection("EventActions", LOCTEXT("EventActionsSection", "Event"));

	// Show event info
	FString EventName = Event->DebugName.IsNone()
		? Event->GetClass()->GetDisplayNameText().ToString()
		: Event->DebugName.ToString();
	MenuBuilder.AddMenuEntry(
		FText::FromString(EventName),
		FText::GetEmpty(),
		FSlateIcon(),
		FUIAction(),
		NAME_None,
		EUserInterfaceActionType::None);

	MenuBuilder.AddMenuSeparator();

	// Set Frame
	MenuBuilder.AddMenuEntry(
		FText::Format(LOCTEXT("SetFrame", "Frame: {0}"), FText::AsNumber(Start)),
		LOCTEXT("SetFrameTooltip", "Frame index at which this event fires during playback. Drag the event on the timeline to reposition."),
		FSlateIcon(),
		FUIAction(),
		NAME_None,
		EUserInterfaceActionType::None);

	// Set Duration (only for ranged events)
	if (Count > 1)
	{
		MenuBuilder.AddMenuEntry(
			FText::Format(LOCTEXT("SetDuration", "Duration: {0} frames"), FText::AsNumber(Count)),
			LOCTEXT("SetDurationTooltip", "Duration in frames for this ranged state event. The event fires Begin on the first frame, Tick each frame, and End after the last frame."),
			FSlateIcon(),
			FUIAction(),
			NAME_None,
			EUserInterfaceActionType::None);
	}

	MenuBuilder.AddMenuSeparator();

	// Delete
	MenuBuilder.AddMenuEntry(
		LOCTEXT("DeleteEvent", "Delete"),
		LOCTEXT("DeleteEventTooltip", "Remove this event"),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([this, EventIdx]()
		{
			OnEventRemoved.ExecuteIfBound(EventIdx);
		})));

	MenuBuilder.EndSection();

	FWidgetPath WidgetPath;
	FSlateApplication::Get().GeneratePathToWidgetUnchecked(SharedThis(this), WidgetPath);
	FSlateApplication::Get().PushMenu(
		SharedThis(this), WidgetPath, MenuBuilder.MakeWidget(),
		MouseEvent.GetScreenSpacePosition(), FPopupTransitionEffect::ContextMenu);
}

#undef LOCTEXT_NAMESPACE