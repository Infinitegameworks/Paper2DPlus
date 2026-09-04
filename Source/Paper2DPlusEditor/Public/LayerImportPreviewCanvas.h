// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AsepriteImporter.h" // FPerLayerBufferMap
#include "Widgets/SLeafWidget.h"

class UTexture2D;
struct FAsepriteParsedData;

/**
 * Composites per-layer `.ase` pixel buffers into one preview image.
 *
 * Rehomed out of the retired `SAsepiteLayerImportDialog` — it is the bulk extractor's per-row import
 * editor that shows it now. The widget does NOT own the parsed data or the buffers: it holds raw
 * pointers, so whoever constructs it must keep both alive for the widget's whole lifetime.
 *
 * TWO SOURCES, chosen by whether PerLayerBuffers is supplied:
 *  - WITH buffers (the per-row import editor): every layer is composited here, so individual layers
 *    can be shown and hidden while the designer decides what to import.
 *  - WITHOUT buffers (the bulk extractor's look-only row preview): the parse's own already-flattened
 *    `FAsepriteParsedData::Frames[].Pixels` IS the picture. There are no visibility checkboxes on
 *    that pane, and the per-layer decode costs layers × frames full-canvas images — so a preview
 *    that cannot toggle anything must not pay for the ability.
 */
class PAPER2DPLUSEDITOR_API SLayerImportPreviewCanvas : public SLeafWidget
{
public:
	// SLATE_ARGUMENT does not value-initialize, so both pointers need an explicit default here —
	// PerLayerBuffers is now optional, and an omitted argument that read as garbage would be
	// indistinguishable from a supplied per-layer map.
	SLATE_BEGIN_ARGS(SLayerImportPreviewCanvas)
		: _ParsedData(nullptr)
		, _PerLayerBuffers(nullptr)
	{}
		SLATE_ARGUMENT(const FAsepriteParsedData*, ParsedData)
		/** Optional. Null selects the flat source described above — the parse's composited frames. */
		SLATE_ARGUMENT(const FPerLayerBufferMap*, PerLayerBuffers)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SLayerImportPreviewCanvas();

	virtual FVector2D ComputeDesiredSize(float) const override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

	/** Set the current frame index for preview. */
	void SetFrameIndex(int32 NewIndex);
	int32 GetFrameIndex() const { return FrameIndex; }

	/** Toggle a layer's visibility in the preview. Deliberately independent of its import checkbox:
	 *  hiding a layer to see what is underneath must not change what the import will produce. */
	void SetLayerVisibility(int32 LayerIndex, bool bVisible);
	bool IsLayerVisible(int32 LayerIndex) const;

	int32 GetFrameCount() const;

#if WITH_DEV_AUTOMATION_TESTS
	/** The pixels this widget would upload for the current frame. Exposed because "the pane is
	 *  showing something" is otherwise untestable: a decode can be perfect while the widget that
	 *  renders it composites nothing, and that failure looks exactly like the bug being fixed. */
	bool GetCompositedFrameForTests(TArray<FColor>& OutPixels) const;
#endif

private:
	void RecompositeFrame() const;
	/** Composite one frame from whichever source this canvas was given. THE single implementation —
	 *  the paint path and the test accessor both go through it. */
	void BuildCompositeForFrame(int32 InFrameIndex, TArray<FColor>& OutComposite) const;
	/** Upload one full-canvas BGRA buffer into PreviewTexture. Shared by both composite sources. */
	void WriteCompositeToTexture(const TArray<FColor>& Composite) const;

	const FAsepriteParsedData* ParsedData = nullptr;
	const FPerLayerBufferMap* PerLayerBuffers = nullptr;

	TMap<int32, bool> LayerVisibility;
	int32 FrameIndex = 0;

	/** Persistent texture for rendering — updated in-place on frame/visibility changes. */
	mutable TStrongObjectPtr<UTexture2D> PreviewTexture;
	mutable FSlateBrush PreviewBrush;
	mutable bool bNeedsRecomposite = true;
};
