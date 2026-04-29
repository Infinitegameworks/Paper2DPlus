// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "SFrameEventPreviewCanvas.h"
#include "FrameEventEditor.h"
#include "EditorCanvasUtils.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "FrameEvents/Paper2DPlusSpawnEffectFrameEvent.h"
#include "PaperFlipbook.h"
#include "PaperSprite.h"

#define LOCTEXT_NAMESPACE "FrameEventPreviewCanvas"

void SFrameEventPreviewCanvas::Construct(const FArguments& InArgs)
{
	Flipbook = InArgs._Flipbook;
	FrameIndex = InArgs._FrameIndex;
	Asset = InArgs._Asset;
	FlipbookIndex = InArgs._FlipbookIndex;
	ActiveEditorEffects = InArgs._ActiveEditorEffects;
	PlaybackTime = InArgs._PlaybackTime;
	IsPlaying = InArgs._IsPlaying;
	SelectedEventIndex = InArgs._SelectedEventIndex;
	SetClipping(EWidgetClipping::ClipToBounds);
}

FVector2D SFrameEventPreviewCanvas::ComputeDesiredSize(float) const
{
	return FVector2D(256, 256);
}

int32 SFrameEventPreviewCanvas::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
	int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const FVector2D GeomSize = AllottedGeometry.GetLocalSize();

	// Checkerboard background
	FEditorCanvasUtils::DrawCheckerboard(OutDrawElements, LayerId, AllottedGeometry);
	LayerId++;

	// Draw sprite via shared utility (handles offset + pivot shift)
	UPaperFlipbook* FB = Flipbook.Get(nullptr);
	int32 Frame = FrameIndex.Get(0);
	if (!FB || Frame < 0 || Frame >= FB->GetNumKeyFrames())
	{
		return LayerId;
	}

	UPaperSprite* Sprite = FB->GetKeyFrameChecked(Frame).Sprite;
	if (!Sprite) return LayerId;

	float Zoom = GetEffectiveZoom(GeomSize, FB);
	FVector2D Center = GeomSize * 0.5f;

	// Get AnimData for offsets
	const FFlipbookProfileEntry* AnimData = nullptr;
	if (Asset.IsValid())
	{
		int32 FBIdx = FlipbookIndex.Get(INDEX_NONE);
		if (Asset->Flipbooks.IsValidIndex(FBIdx))
		{
			AnimData = &Asset->Flipbooks[FBIdx];
		}
	}

	FEditorCanvasUtils::DrawFlipbookSprite(
		OutDrawElements, LayerId, AllottedGeometry,
		Sprite, AnimData, Frame, Center, Zoom);
	LayerId++;

	// Static effect preview when not playing and a spawn effect event is selected
	if (!IsPlaying.Get(false))
	{
		int32 SelEvIdx = SelectedEventIndex.Get(INDEX_NONE);
		if (Asset.IsValid() && SelEvIdx != INDEX_NONE)
		{
			int32 FBIdx = FlipbookIndex.Get(INDEX_NONE);
			if (Asset->Flipbooks.IsValidIndex(FBIdx))
			{
				const auto& Events = Asset->Flipbooks[FBIdx].FrameEventData.FrameEvents;
				if (Events.IsValidIndex(SelEvIdx))
				{
					if (const UPaper2DPlusSpawnEffectFrameEvent* SpawnEvent = Cast<UPaper2DPlusSpawnEffectFrameEvent>(Events[SelEvIdx]))
					{
						if (SpawnEvent->EffectFlipbook && SpawnEvent->EffectFlipbook->GetNumKeyFrames() > 0)
						{
							// Show first frame of the effect at its authored position
							UPaperSprite* EffectSprite = SpawnEvent->EffectFlipbook->GetKeyFrameChecked(0).Sprite;
							if (EffectSprite)
							{
								UTexture2D* EffectTex = EffectSprite->GetBakedTexture();
								if (!EffectTex) EffectTex = Cast<UTexture2D>(EffectSprite->GetSourceTexture());
								if (EffectTex)
								{
									const FVector2D SrcUV = EffectSprite->GetSourceUV();
									const FVector2D SrcSize = EffectSprite->GetSourceSize();

									FVector2D EffectSize = SrcSize * FVector2D(
										FMath::Abs(SpawnEvent->Scale.X),
										FMath::Abs(SpawnEvent->Scale.Y)) * Zoom;
									FVector2D EffectCenter = Center + FVector2D(SpawnEvent->Offset.X, -SpawnEvent->Offset.Y) * Zoom;
									FVector2D EffectPos = EffectCenter - EffectSize * 0.5f;

									FSlateBrush EffectBrush;
									EffectBrush.SetResourceObject(EffectTex);
									EffectBrush.ImageSize = FVector2D(EffectTex->GetSizeX(), EffectTex->GetSizeY());
									EffectBrush.SetUVRegion(FBox2D(
										SrcUV / EffectBrush.ImageSize,
										(SrcUV + SrcSize) / EffectBrush.ImageSize));

									FLinearColor DrawColor = SpawnEvent->Tint;
									DrawColor.A *= 0.7f; // Slightly transparent for preview

									if (FMath::IsNearlyZero(SpawnEvent->Rotation))
									{
										FSlateDrawElement::MakeBox(OutDrawElements, LayerId,
											MakePaintGeometry(AllottedGeometry, EffectSize, FSlateLayoutTransform(EffectPos)),
											&EffectBrush, ESlateDrawEffect::None, DrawColor);
									}
									else
									{
										FSlateDrawElement::MakeRotatedBox(OutDrawElements, LayerId,
											MakePaintGeometry(AllottedGeometry, EffectSize, FSlateLayoutTransform(EffectPos)),
											&EffectBrush, ESlateDrawEffect::None,
											FMath::DegreesToRadians(SpawnEvent->Rotation),
											EffectSize * 0.5f, FSlateDrawElement::RelativeToElement, DrawColor);
									}
									LayerId++;
								}
							}
						}
					}
				}
			}
		}
	}

	// Draw active effect previews (during playback)
	if (ActiveEditorEffects.IsValid())
	{
		const double CurrentPlaybackTime = PlaybackTime.Get(0.0);

		for (const FEditorEffectPreview& Preview : *ActiveEditorEffects)
		{
			UPaper2DPlusSpawnEffectFrameEvent* SpawnEvent = Preview.SourceEvent.Get();
			if (!SpawnEvent || !SpawnEvent->EffectFlipbook) continue;

			UPaperFlipbook* EffectFB = SpawnEvent->EffectFlipbook;
			if (EffectFB->GetNumKeyFrames() == 0) continue;

			// Compute effect playback frame from elapsed time
			float Elapsed = static_cast<float>(CurrentPlaybackTime - Preview.StartTime);
			if (Elapsed < 0.0f) continue;

			int32 EffectFrame = EffectFB->GetKeyFrameIndexAtTime(Elapsed);
			if (EffectFrame < 0 || EffectFrame >= EffectFB->GetNumKeyFrames()) continue;

			UPaperSprite* EffectSprite = EffectFB->GetKeyFrameChecked(EffectFrame).Sprite;
			if (!EffectSprite) continue;

			UTexture2D* EffectTexture = EffectSprite->GetBakedTexture();
			if (!EffectTexture) EffectTexture = Cast<UTexture2D>(EffectSprite->GetSourceTexture());
			if (!EffectTexture) continue;

			const FVector2D SpriteSourcePos = FVector2D(EffectSprite->GetSourceUV());
			const FVector2D SpriteSourceSize = FVector2D(EffectSprite->GetSourceSize());

			// Effect size accounts for authored scale
			FVector2D EffectSize = SpriteSourceSize * FVector2D(
				FMath::Abs(SpawnEvent->Scale.X),
				FMath::Abs(SpawnEvent->Scale.Y)) * Zoom;

			// Position: character center + authored offset (Y inverted for screen coords)
			FVector2D EffectCenter = Center + FVector2D(SpawnEvent->Offset.X, -SpawnEvent->Offset.Y) * Zoom;
			FVector2D EffectPos = EffectCenter - EffectSize * 0.5f;

			// Build brush with UV sub-region
			FSlateBrush EffectBrush;
			EffectBrush.SetResourceObject(EffectTexture);
			EffectBrush.ImageSize = FVector2D(EffectTexture->GetSizeX(), EffectTexture->GetSizeY());
			EffectBrush.SetUVRegion(FBox2D(
				SpriteSourcePos / EffectBrush.ImageSize,
				(SpriteSourcePos + SpriteSourceSize) / EffectBrush.ImageSize));

			FLinearColor DrawColor = SpawnEvent->Tint;

			// Draw with or without rotation
			if (FMath::IsNearlyZero(SpawnEvent->Rotation))
			{
				FSlateDrawElement::MakeBox(OutDrawElements, LayerId,
					MakePaintGeometry(AllottedGeometry, EffectSize, FSlateLayoutTransform(EffectPos)),
					&EffectBrush, ESlateDrawEffect::None, DrawColor);
			}
			else
			{
				FSlateDrawElement::MakeRotatedBox(OutDrawElements, LayerId,
					MakePaintGeometry(AllottedGeometry, EffectSize, FSlateLayoutTransform(EffectPos)),
					&EffectBrush,
					ESlateDrawEffect::None,
					FMath::DegreesToRadians(SpawnEvent->Rotation),
					EffectSize * 0.5f,
					FSlateDrawElement::RelativeToElement,
					DrawColor);
			}

			LayerId++;
		}
	}

	return LayerId;
}

FReply SFrameEventPreviewCanvas::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton) return FReply::Unhandled();
	if (IsPlaying.Get(false)) return FReply::Unhandled();

	int32 SelEvIdx = SelectedEventIndex.Get(INDEX_NONE);
	if (SelEvIdx == INDEX_NONE || !Asset.IsValid()) return FReply::Unhandled();

	int32 FBIdx = FlipbookIndex.Get(INDEX_NONE);
	if (!Asset->Flipbooks.IsValidIndex(FBIdx)) return FReply::Unhandled();

	const auto& Events = Asset->Flipbooks[FBIdx].FrameEventData.FrameEvents;
	if (!Events.IsValidIndex(SelEvIdx)) return FReply::Unhandled();

	const UPaper2DPlusSpawnEffectFrameEvent* SpawnEvent = Cast<UPaper2DPlusSpawnEffectFrameEvent>(Events[SelEvIdx]);
	if (!SpawnEvent || !SpawnEvent->EffectFlipbook) return FReply::Unhandled();

	bDraggingEffect = true;
	DragStartScreenPos = MouseEvent.GetScreenSpacePosition();
	DragStartOffset = SpawnEvent->Offset;
	OnEffectDragStarted.ExecuteIfBound();
	return FReply::Handled().CaptureMouse(SharedThis(this));
}

FReply SFrameEventPreviewCanvas::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (!bDraggingEffect) return FReply::Unhandled();

	UPaperFlipbook* FB = Flipbook.Get(nullptr);
	if (!FB) return FReply::Handled();

	float Zoom = GetEffectiveZoom(MyGeometry.GetLocalSize(), FB);
	if (Zoom <= 0.0f) return FReply::Handled();

	FVector2D ScreenDelta = MouseEvent.GetScreenSpacePosition() - DragStartScreenPos;
	// X maps directly, Y is inverted (screen down = negative offset Y)
	FVector2D NewOffset = DragStartOffset + FVector2D(ScreenDelta.X / Zoom, -ScreenDelta.Y / Zoom);
	NewOffset.X = FMath::RoundToFloat(NewOffset.X);
	NewOffset.Y = FMath::RoundToFloat(NewOffset.Y);

	OnEffectOffsetChanged.ExecuteIfBound(NewOffset);
	Invalidate(EInvalidateWidgetReason::Paint);
	return FReply::Handled();
}

FReply SFrameEventPreviewCanvas::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (bDraggingEffect && MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		bDraggingEffect = false;
		OnEffectDragEnded.ExecuteIfBound();
		return FReply::Handled().ReleaseMouseCapture();
	}
	return FReply::Unhandled();
}

void SFrameEventPreviewCanvas::OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent)
{
	bDraggingEffect = false;
}

float SFrameEventPreviewCanvas::GetEffectiveZoom(const FVector2D& WidgetSize, UPaperFlipbook* FB) const
{
	if (!FB) return 1.0f;

	FIntPoint Largest(1, 1);
	for (int32 i = 0; i < FB->GetNumKeyFrames(); ++i)
	{
		if (UPaperSprite* S = FB->GetKeyFrameChecked(i).Sprite)
		{
			FVector2D Sz = S->GetSourceSize();
			Largest.X = FMath::Max(Largest.X, FMath::RoundToInt(Sz.X));
			Largest.Y = FMath::Max(Largest.Y, FMath::RoundToInt(Sz.Y));
		}
	}

	float ScaleX = WidgetSize.X / Largest.X;
	float ScaleY = WidgetSize.Y / Largest.Y;
	return FMath::Min(ScaleX, ScaleY) * 0.8f;
}

#undef LOCTEXT_NAMESPACE