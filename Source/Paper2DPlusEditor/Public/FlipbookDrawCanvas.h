// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"
#include "UObject/GCObject.h"

class FFlipbookDrawModel;
class UPaperSprite;
class UTexture2D;
enum class EFlipbookDrawTool : uint8;
enum class EFlipbookPixelTransform : uint8;

/** One undoable pixel edit: the dirty sub-rect of a frame, plus the before/after pixels for that rect. */
struct FFlipbookPixelEditEntry
{
	int32 Frame = 0;
	FIntRect Rect;
	TArray<FColor> Before;
	TArray<FColor> After;
};

/**
 * SFlipbookDrawCanvas — the paintable canvas for the Flipbook Draw editor.
 *
 * Renders the current key-frame's source sub-region 1:1 (no pivot/offset shift — this is pixel editing,
 * not animation preview), over a checkerboard with free zoom/pan and a one-time fit-on-open. It owns
 * live Pencil/Eraser/Line/Rectangle previews, one-shot Fill/Dropper actions, frame transforms, and the
 * snapshot undo/redo stack used for source-texture byte edits.
 *
 * Coordinate model (single frame drawn at native pixel size): DrawSize = FrameSize * Zoom;
 * DrawPos = PanOffset + (WidgetSize - DrawSize)*0.5; FramePixel = (Local - DrawPos)/Zoom. The brush
 * inverts exactly this, so a stroke can never drift from what's painted.
 *
 * FGCObject because the transient working UTexture2D must survive GC mid-edit.
 */
class SFlipbookDrawCanvas : public SLeafWidget, public FGCObject
{
public:
	SLATE_BEGIN_ARGS(SFlipbookDrawCanvas) {}
		SLATE_ARGUMENT(TSharedPtr<FFlipbookDrawModel>, Model)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SFlipbookDrawCanvas();

	// SWidget
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FVector2D ComputeDesiredSize(float) const override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent) override;
	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

	// FGCObject
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override { return TEXT("SFlipbookDrawCanvas"); }

	/** Reset zoom/pan to a fit-on-next-paint. */
	void RequestFitView();
	/** True while the current frame can accept an immediate whole-frame transform. */
	bool CanApplyFrameTransform() const;
	/** Apply one undoable, dimension-preserving operation to the current frame. */
	void ApplyFrameTransform(EFlipbookPixelTransform Transform);

private:
	void HandleFrameChanged();

	/** Frame (sprite source) pixel dimensions; (0,0) when no current sprite. */
	FVector2D GetFrameSize() const;
	FVector2D GetDrawSize(const FGeometry& Geom) const;
	FVector2D GetDrawPos(const FGeometry& Geom) const;
	/** Local widget pos -> float frame-pixel coords in [0..FrameSize]. */
	FVector2D ScreenToFramePixel(const FGeometry& Geom, const FVector2D& LocalPos) const;
	/** Local widget pos -> integer frame-pixel (floored). */
	FIntPoint ScreenToFramePixelInt(const FGeometry& Geom, const FVector2D& LocalPos) const;
	void EnsureInitialFit(const FGeometry& Geom) const;

	// ---- Working buffer / live preview ----
	/** Load the current frame's pixels into WorkingBuffer + WorkingTexture (rebuilds only on a frame switch). */
	void EnsureWorkingFrame();
	/** (Re)create the transient WorkingTexture from WorkingBuffer. */
	void RebuildWorkingTexture();
	/** Push a dirtied sub-region of WorkingBuffer to the GPU (UpdateTextureRegions) for live feedback. */
	void PushDirtyToWorkingTexture(const FIntRect& Rect);

	// ---- Brush stroke ----
	FColor GetBrushColor(EFlipbookDrawTool Tool) const;   // primary color, or transparent for the eraser
	void BeginStroke(const FIntPoint& FramePixel);
	void ContinueStroke(const FIntPoint& FramePixel);
	void UpdateShapePreview(const FIntPoint& FramePixel);
	void EndStroke();
	void SampleEyedropper(const FIntPoint& FramePixel);   // Eyedropper: working pixel -> primary color
	void DoFloodFill(const FIntPoint& FramePixel);        // Fill: one-shot flood fill committed as one undo entry

	// ---- Playback ----
	void HandlePlaybackStateChanged();
	EActiveTimerReturnType OnPlaybackTick(double InCurrentTime, float InDeltaTime);

	// ---- Undo / redo (our own — texture bytes aren't transaction-captured) ----
	bool CommitWorkingEdit(const TArray<FColor>& Before, const FIntRect& DirtyRect);
	bool ApplyEdit(const FFlipbookPixelEditEntry& Entry, bool bUseAfter);
	void Undo();
	void Redo();

	TSharedPtr<FFlipbookDrawModel> Model;
	FDelegateHandle FrameChangedHandle;
	FDelegateHandle PlaybackStateHandle;
	TWeakPtr<FActiveTimerHandle> PlaybackTimerHandle;
	double PlaybackAccum = 0.0;
	int32 PlaybackFrameRunAccum = 0;

	mutable float ZoomLevel = 4.0f;
	mutable FVector2D PanOffset = FVector2D::ZeroVector;
	mutable bool bNeedsInitialFit = true;

	bool bIsPanning = false;
	FVector2D LastPanLocalPos = FVector2D::ZeroVector;

	// CPU working copy of the current frame + a transient GPU mirror the canvas draws.
	TArray<FColor> WorkingBuffer;
	int32 WorkW = 0;
	int32 WorkH = 0;
	int32 WorkingFrame = INDEX_NONE;
	bool bDrawable = false;
	TObjectPtr<UTexture2D> WorkingTexture = nullptr;

	// Active stroke state.
	bool bStroking = false;
	EFlipbookDrawTool StrokeTool = static_cast<EFlipbookDrawTool>(0);
	FIntPoint ShapeStartPixel = FIntPoint::ZeroValue;
	FIntPoint LastStampPixel = FIntPoint::ZeroValue;
	TArray<FColor> StrokeBefore;
	FIntRect StrokeDirty;

	TArray<FFlipbookPixelEditEntry> UndoStack;
	TArray<FFlipbookPixelEditEntry> RedoStack;
};
