// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/EngineVersionComparison.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SLeafWidget.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "SEditorViewport.h"
#include "EditorViewportClient.h"
#include "PreviewScene.h"
#include "DragAndDrop/DecoratedDragDropOp.h"
#include "PaperSprite.h"
#include "Styling/AppStyle.h"

class UPaperSprite;
class UPaperFlipbook;
class UPaperSpriteComponent;
class FCharacterProfileEditorModel;
class FExtender;
class FMenuBuilder;
class UPaper2DPlusCharacterLayerAsset;
class FHitboxFrameDataProvider;
class IProfileItemPickerSource;
class IProfileToolPanelProvider;
class SCharacterCompletionPanel;
class SExpectedTagsPanel;
class SPlaybackQueuePanel;
class SProfileToolPanelHost;
class SSpriteEditorCanvas;
class SDockTab;
class SWindow;
struct FPaper2DPlusValidationIssue;

/** Tool modes for the hitbox editor */
enum class EHitboxEditorTool : uint8
{
	Draw,		// Draw new hitboxes
	Edit,		// Select, move, resize hitboxes
	Socket		// Place sockets
};

/** Selection type */
enum class EHitboxSelectionType : uint8
{
	None,
	Hitbox,
	Socket
};

/** Resize handle positions */
enum class EResizeHandle : uint8
{
	None,
	TopLeft, Top, TopRight,
	Left, Right,
	BottomLeft, Bottom, BottomRight
};

/** Drag mode for mouse operations */
enum class EHitboxDragMode : uint8
{
	None,
	Creating,		// Drawing a new hitbox
	Moving,			// Moving selected hitbox/socket
	Resizing		// Resizing selected hitbox
};

/** Hitbox visibility flags for the Hitbox Editor. Stored as a uint8 bitmask on
 *  the editor so the canvas and sidebar can filter by type. Backing bits match
 *  EHitboxType ordinals: Attack=0, Hurtbox=1. */
enum class EHitboxVisibility : uint8
{
	None    = 0,
	Attack  = 1 << 0,
	Hurtbox = 1 << 1,
	All     = Attack | Hurtbox
};
ENUM_CLASS_FLAGS(EHitboxVisibility);

/**
 * SFrameStripHitboxOverlay — paints translucent hitbox silhouettes on top of
 * a frame strip sprite thumbnail. Shared by the parent editor and HitboxEditorPanel.
 */
class SFrameStripHitboxOverlay : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SFrameStripHitboxOverlay) {}
		SLATE_ARGUMENT(TWeakObjectPtr<UPaperSprite>, Sprite)
		SLATE_ATTRIBUTE(EHitboxVisibility, VisibilityMask)
		// Independent hitbox/socket channels so the strip can show either overlay alone.
		// Unset defaults preserve the original contract: hitboxes on, sockets off.
		SLATE_ATTRIBUTE(bool, ShowHitboxes)
		SLATE_ATTRIBUTE(bool, ShowSockets)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		Sprite = InArgs._Sprite;
		VisibilityMask = InArgs._VisibilityMask;
		ShowHitboxes = InArgs._ShowHitboxes;
		ShowSockets = InArgs._ShowSockets;
		SetCanTick(false);
	}

	void SetHitboxes(const TArray<FHitboxData>& InHitboxes)
	{
		Hitboxes = InHitboxes;
	}

	void SetSockets(const TArray<FSocketData>& InSockets)
	{
		Sockets = InSockets;
	}

	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(48, 48); }

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override
	{
		UPaperSprite* SpritePtr = Sprite.Get();
		const bool bDrawHitboxes = ShowHitboxes.Get(true) && Hitboxes.Num() > 0;
		const bool bDrawSockets = ShowSockets.Get(false) && Sockets.Num() > 0;
		if (!SpritePtr || (!bDrawHitboxes && !bDrawSockets)) return LayerId;

		const FVector2D SpriteSize = SpritePtr->GetSourceSize();
		if (SpriteSize.X <= 0.0f || SpriteSize.Y <= 0.0f) return LayerId;

		const FVector2D CellSize = AllottedGeometry.GetLocalSize();
		const FVector2D Scale(CellSize.X / SpriteSize.X, CellSize.Y / SpriteSize.Y);

		const EHitboxVisibility Mask = VisibilityMask.Get(EHitboxVisibility::All);
		const FSlateBrush* WhiteBrush = FAppStyle::Get().GetBrush("WhiteBrush"); // ::Get().GetBrush works on all versions; static GetBrush is 5.1+

		if (bDrawHitboxes)
		{
			for (const FHitboxData& HB : Hitboxes)
			{
				const EHitboxVisibility TypeBit = (HB.Type == EHitboxType::Attack) ? EHitboxVisibility::Attack
					: (HB.Type == EHitboxType::Hurtbox) ? EHitboxVisibility::Hurtbox : EHitboxVisibility::None;
				if (TypeBit == EHitboxVisibility::None || !EnumHasAnyFlags(Mask, TypeBit)) continue;
				if (HB.Width <= 0 || HB.Height <= 0) continue;

				FLinearColor Color;
				switch (HB.Type)
				{
					case EHitboxType::Attack:  Color = FLinearColor(1.0f, 0.25f, 0.25f, 0.55f); break;
					case EHitboxType::Hurtbox: Color = FLinearColor(0.25f, 1.0f, 0.25f, 0.55f); break;
					default:                   Color = FLinearColor(1.0f, 1.0f, 1.0f, 0.40f); break;
				}

				const FVector2D Pos(HB.X * Scale.X, HB.Y * Scale.Y);
				const FVector2D Size(HB.Width * Scale.X, HB.Height * Scale.Y);

				FSlateDrawElement::MakeBox(
					OutDrawElements,
					LayerId,
					AllottedGeometry.ToPaintGeometry(Size, FSlateLayoutTransform(Pos)),
					WhiteBrush,
					ESlateDrawEffect::None,
					Color);
			}
		}

		if (bDrawSockets)
		{
			// Sockets are points, so markers keep a fixed on-screen size instead of scaling with the sprite.
			for (const FSocketData& Sock : Sockets)
			{
				const FVector2D Pos(Sock.X * Scale.X, Sock.Y * Scale.Y);

				FSlateDrawElement::MakeBox(
					OutDrawElements,
					LayerId + 1,
					AllottedGeometry.ToPaintGeometry(FVector2D(7.0, 7.0), FSlateLayoutTransform(Pos - FVector2D(3.5, 3.5))),
					WhiteBrush,
					ESlateDrawEffect::None,
					FLinearColor(0.0f, 0.0f, 0.0f, 0.60f));
				FSlateDrawElement::MakeBox(
					OutDrawElements,
					LayerId + 1,
					AllottedGeometry.ToPaintGeometry(FVector2D(5.0, 5.0), FSlateLayoutTransform(Pos - FVector2D(2.5, 2.5))),
					WhiteBrush,
					ESlateDrawEffect::None,
					FLinearColor(0.95f, 0.85f, 0.30f, 0.90f));
			}
		}
		return LayerId + 2;
	}

private:
	TWeakObjectPtr<UPaperSprite> Sprite;
	TAttribute<EHitboxVisibility> VisibilityMask;
	TAttribute<bool> ShowHitboxes;
	TAttribute<bool> ShowSockets;
	TArray<FHitboxData> Hitboxes;
	TArray<FSocketData> Sockets;
};

/** Character profile editor tab indices — eliminates raw integer comparisons. */
enum class ECharacterProfileTab : uint8
{
	Overview = 0,
	Hitboxes,
	SpriteEditor,
	FrameTiming,
	FrameEvents,    // formerly Effects; Phases tab deleted
	RootMotion,
	NumTabs
};
constexpr int32 kNumCharacterProfileTabs = static_cast<int32>(ECharacterProfileTab::NumTabs);

/** Drag-drop operation for flipbook cards (shared across Grid/List group reparenting and the Animation Map). */
class FFlipbookGroupDragDropOp : public FDragDropOperation
{
public:
	DRAG_DROP_OPERATOR_TYPE(FFlipbookGroupDragDropOp, FDragDropOperation)

	TArray<int32> FlipbookIndices;

	/** The asset whose Flipbooks array FlipbookIndices index (combo-graph plan U5, additive payload —
	 *  existing accept sites are untouched). Accept sites that resolve the indices against THEIR OWN
	 *  asset (the Animation Map drop target) must reject ops whose SourceAsset differs — bare indices
	 *  into a different Flipbooks array would mint wrong nodes AND durable position entries. Asset
	 *  identity rather than model identity, so the deferred layer-editor hosting (plan A4) stays
	 *  compatible. Null (a creation site that failed to resolve its asset) fails CLOSED at such sites. */
	TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset> SourceAsset;

	/** InSourceAsset is REQUIRED (no default) so any future creation site is forced to thread its asset
	 *  at compile time (the 'X-wins policies must hold across ALL acquisition paths' rule). */
	static TSharedRef<FFlipbookGroupDragDropOp> NewFromCardDrag(const TArray<int32>& InFlipbookIndices, FName FromGroup,
		UPaper2DPlusCharacterProfileAsset* InSourceAsset);

	virtual TSharedPtr<SWidget> GetDefaultDecorator() const override;

private:
	FText DefaultHoverText;
};

/** Drag-drop operation for dragging entire groups (reparenting). */
class FGroupDragDropOp : public FDragDropOperation
{
public:
	DRAG_DROP_OPERATOR_TYPE(FGroupDragDropOp, FDragDropOperation)

	FName SourceGroupName;

	static TSharedRef<FGroupDragDropOp> New(FName GroupName);

	virtual TSharedPtr<SWidget> GetDefaultDecorator() const override;

private:
	FText DefaultHoverText;
};

/**
 * Interactive canvas widget for drawing and editing hitboxes.
 * Handles mouse input for drawing, selecting, moving, and resizing.
 */
class SCharacterProfileEditorCanvas : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SCharacterProfileEditorCanvas) {}
		SLATE_ARGUMENT(TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset>, Asset)
		/** Optional canonical Layer source used to paint the live composite behind source-local geometry. */
		SLATE_ARGUMENT(TWeakObjectPtr<UPaper2DPlusCharacterLayerAsset>, LayerAsset)
		SLATE_ARGUMENT(TSharedPtr<FCharacterProfileEditorModel>, Model)
		// Optional data-provider seam (see HitboxDataProvider.h). When set, ALL frame-data reads/writes
		// (paint, hit-test, drag-create/move/resize) route through it instead of walking the asset
		// directly, so a Layer-scoped canvas edits the selected Layer's authored data. Null → the
		// canvas keeps its direct Asset->Flipbooks[..].CombatData.Frames walk (byte-identical).
		SLATE_ARGUMENT(TSharedPtr<FHitboxFrameDataProvider>, FrameDataProvider)
		SLATE_ATTRIBUTE(int32, SelectedFlipbookIndex)
		SLATE_ATTRIBUTE(int32, SelectedFrameIndex)
		SLATE_ATTRIBUTE(EHitboxEditorTool, CurrentTool)
		SLATE_ATTRIBUTE(float, Zoom)
		SLATE_ATTRIBUTE(EHitboxVisibility, VisibilityMask)
		SLATE_ATTRIBUTE(EHitboxType, ActiveDrawType)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	// SWidget interface
	virtual FVector2D ComputeDesiredSize(float) const override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonDoubleClick(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual void OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent) override;

	// Selection
	DECLARE_DELEGATE_TwoParams(FOnSelectionChanged, EHitboxSelectionType, int32);
	FOnSelectionChanged OnSelectionChanged;

	DECLARE_DELEGATE(FOnHitboxDataModified);
	FOnHitboxDataModified OnHitboxDataModified;

	DECLARE_DELEGATE(FOnRequestUndo);
	FOnRequestUndo OnRequestUndo;

	DECLARE_DELEGATE(FOnEndTransaction);
	FOnEndTransaction OnEndTransaction;

	// Tool change request delegate (e.g., double-click to edit)
	DECLARE_DELEGATE_OneParam(FOnToolChangeRequested, EHitboxEditorTool);
	FOnToolChangeRequested OnToolChangeRequested;

	// Zoom change delegate
	DECLARE_DELEGATE_OneParam(FOnZoomChanged, float);
	FOnZoomChanged OnZoomChanged;

	// External selection control
	void SetSelection(EHitboxSelectionType Type, int32 Index);
	void AddToSelection(int32 Index);
	void RemoveFromSelection(int32 Index);
	void ToggleSelection(int32 Index);
	void ClearSelection();
	EHitboxSelectionType GetSelectionType() const { return SelectionType; }
	bool IsSelected(int32 Index) const;
	TArray<int32> GetSelectedIndices() const { return SelectedIndices; }
	int32 GetPrimarySelectedIndex() const;
	/** Worldless Slate lifecycle seams: exercise the same exact-once no-capture settlement as a real drag. */
	void ArmDragForTests(EHitboxDragMode Mode, bool bTransactionOpen)
	{
		DragMode = Mode;
		bDragTransactionOpen = bTransactionOpen;
	}
	void SettleDragForTests() { SettleAndResetDragState(); }
	bool HasArmedDragForTests() const { return DragMode != EHitboxDragMode::None; }

	// Nudge selection by delta
	void NudgeSelection(int32 DeltaX, int32 DeltaY);

	// Delete current selection
	void DeleteSelection();

	// Sprite dimensions for external access
	FVector2D GetSpriteDimensions() const;

	/** Re-point the canvas at a new profile asset — the panel calls this from RefreshAll when the model is
	 *  re-initialized (the Character Layer editor's Base Profile picker swaps the profile), so the sprite
	 *  render + zoom (which resolve from this Asset, not the provider) don't keep drawing the OLD profile's
	 *  sprites while frame writes go to the new scope. Clears the largest-dims cache so zoom re-derives. */
	void SetAsset(TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset> InAsset)
	{
		Asset = InAsset;
		ResetCachedGeometry();
	}
	/** Layer visibility, sprite mapping, placement, profile replacement, and undo can all change the
	 *  composite bounds without changing the selected flipbook object. Invalidate that paint-hot cache
	 *  explicitly at each owning model boundary. */
	void ResetCachedGeometry()
	{
		CachedLargestDims = FVector2D(128, 128);
		CachedLargestDimsFlipbookIndex = INDEX_NONE;
		CachedLargestDimsFlipbook.Reset();
	}

private:
	TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset> Asset;
	TWeakObjectPtr<UPaper2DPlusCharacterLayerAsset> LayerAsset;
	TWeakPtr<FCharacterProfileEditorModel> ModelWeak;
	// Optional frame-data seam (see Construct arg). When null the canvas uses its direct asset walk.
	TSharedPtr<FHitboxFrameDataProvider> Provider;
	TAttribute<int32> SelectedFlipbookIndex;
	TAttribute<int32> SelectedFrameIndex;
	TAttribute<EHitboxEditorTool> CurrentTool;
	TAttribute<float> Zoom;
	TAttribute<EHitboxVisibility> VisibilityMask;
	TAttribute<EHitboxType> ActiveDrawType;

	// Selection state
	EHitboxSelectionType SelectionType = EHitboxSelectionType::None;
	TArray<int32> SelectedIndices;

	// Multi-move state: stores initial positions of all selected hitboxes during group move
	TMap<int32, FVector2D> DragStartPositions;

	// Drag state
	EHitboxDragMode DragMode = EHitboxDragMode::None;
	FVector2D DragStart;
	FVector2D DragCurrent;
	EResizeHandle ActiveHandle = EResizeHandle::None;

	// Dead-zone: true once a drag has opened an undo transaction (set lazily on the first real
	// mutation in OnMouseMove). A plain select-click leaves it false, so no transaction opens and
	// the asset stays clean (F9). Mirrors SRootMotionCanvas's pending-drag dead-zone.
	bool bDragTransactionOpen = false;

	// For creating new hitbox
	FIntRect CreatingRect;

	// Helpers
	FLinearColor GetHitboxColor(EHitboxType Type) const;
	const FFrameHitboxData* GetCurrentFrame() const;
	FFrameHitboxData* GetCurrentFrameMutable() const;
	// Create-on-first-edit resolve for the layer provider (find-only in the profile path). Call ONLY
	// inside a live undo transaction at a genuine first-write site (draw-create, socket-create).
	FFrameHitboxData* EnsureCurrentFrameMutable() const;
	/** Dry-run first-write validation. This must pass before requesting the parent transaction. */
	bool CanEnsureCurrentFrame() const;
	void SettleAndResetDragState();
	const FFlipbookProfileEntry* GetCurrentFlipbookData() const;
	bool GetCurrentSpriteInfo(UPaperSprite*& OutSprite, FVector2D& OutDimensions) const;
	FVector2D GetLargestSpriteDims() const;
	// Draw the base-profile boxes read-only underneath the authored boxes (layer scope only).
	void DrawGhostBox(const FGeometry& Geom, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FHitboxData& HB, bool bFinalProjection = false) const;

	// Cache for GetLargestSpriteDims
	mutable FVector2D CachedLargestDims = FVector2D(128, 128);
	mutable int32 CachedLargestDimsFlipbookIndex = -1;
	mutable TWeakObjectPtr<UPaperFlipbook> CachedLargestDimsFlipbook;

	// Coordinate conversion
	FVector2D ScreenToCanvas(const FGeometry& Geom, const FVector2D& ScreenPos) const;
	FVector2D CanvasToScreen(const FGeometry& Geom, const FVector2D& CanvasPos) const;
	FVector2D GetCanvasOffset(const FGeometry& Geom) const;
	float GetEffectiveZoom(const FGeometry& Geom) const;

	// Hit testing
	int32 HitTestHitbox(const FVector2D& CanvasPos) const;
	int32 HitTestSocket(const FVector2D& CanvasPos) const;
	EResizeHandle HitTestHandle(const FVector2D& CanvasPos, const FHitboxData& Hitbox) const;

	// Drawing helpers
	void DrawHitbox(const FGeometry& Geom, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FHitboxData& HB, bool bSelected) const;
	void DrawSocket(const FGeometry& Geom, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FSocketData& Sock, bool bSelected) const;
	void DrawResizeHandles(const FGeometry& Geom, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FHitboxData& HB) const;
	void DrawCreatingRect(const FGeometry& Geom, FSlateWindowElementList& OutDrawElements, int32 LayerId) const;
	static constexpr float HandleSize = 8.0f;
	static constexpr float SocketHitRadius = 10.0f;
};

/**
 * Canvas widget for sprite editor.
 * Displays sprite with offset controls, onion skinning, and reticle.
 */
class SSpriteEditorCanvas : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SSpriteEditorCanvas) {}
		SLATE_ARGUMENT(TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset>, Asset)
		SLATE_ATTRIBUTE(int32, SelectedFlipbookIndex)
		SLATE_ATTRIBUTE(int32, SelectedFrameIndex)
		SLATE_ATTRIBUTE(float, Zoom)
		SLATE_ATTRIBUTE(bool, ShowOnionSkin)
		SLATE_ATTRIBUTE(int32, OnionSkinFrames)
		SLATE_ATTRIBUTE(int32, ForwardOnionSkinFrames)
		SLATE_ATTRIBUTE(float, OnionSkinOpacity)
		SLATE_ATTRIBUTE(int32, PreviousFlipbookIndex)
		SLATE_ATTRIBUTE(bool, ShowForwardOnionSkin)
		SLATE_ATTRIBUTE(int32, NextFlipbookIndex)
		SLATE_ATTRIBUTE(bool, ShowReticle)
		SLATE_ATTRIBUTE(FVector2D, ReticlePosition)
		SLATE_ATTRIBUTE(bool, FlipX)
		SLATE_ATTRIBUTE(bool, FlipY)
		SLATE_ATTRIBUTE(bool, ShowReferenceSprite)
		SLATE_ATTRIBUTE(TWeakObjectPtr<UPaperSprite>, ReferenceSprite)
		SLATE_ATTRIBUTE(FIntPoint, ReferenceSpriteOffset)
		SLATE_ATTRIBUTE(float, ReferenceSpriteOpacity)
		SLATE_ATTRIBUTE(FIntPoint, QueueLargestDims)
		SLATE_ATTRIBUTE(TWeakObjectPtr<UPaperSprite>, ExcludedPreviewSprite)
		SLATE_ATTRIBUTE(FIntPoint, ExcludedPreviewOffset)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	// SWidget interface
	virtual FVector2D ComputeDesiredSize(float) const override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual void OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent) override;

	// Delegate for when offset changes via mouse drag
	DECLARE_DELEGATE_TwoParams(FOnOffsetChanged, int32 /*DeltaX*/, int32 /*DeltaY*/);
	FOnOffsetChanged OnOffsetChanged;

	/** Invalidate cached sprite dimensions — call after baking offsets */
	void InvalidateCachedDims() { CachedLargestDimsFlipbookIndex = -1; }

	// Delegate for zoom changes
	DECLARE_DELEGATE_OneParam(FOnZoomChanged, float);
	FOnZoomChanged OnZoomChanged;

	// Delegates for drag gesture lifecycle (parent manages transaction)
	DECLARE_DELEGATE(FOnSpriteEditorDragStarted);
	DECLARE_DELEGATE(FOnSpriteEditorDragEnded);
	FOnSpriteEditorDragStarted OnDragStarted;
	FOnSpriteEditorDragEnded OnDragEnded;

	// Read-only U23 acceptance seams: prove reconstructed controls still drive the central canvas.
	bool IsShowingOnionSkinForTests() const { return ShowOnionSkin.Get(); }
	bool IsShowingReferenceSpriteForTests() const { return ShowReferenceSprite.Get(); }

	// Delegate for reticle repositioning (Alt+left-click drag)
	DECLARE_DELEGATE_OneParam(FOnReticlePositionChanged, FVector2D);
	FOnReticlePositionChanged OnReticlePositionChanged;

private:
	TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset> Asset;
	TAttribute<int32> SelectedFlipbookIndex;
	TAttribute<int32> SelectedFrameIndex;
	TAttribute<float> Zoom;
	TAttribute<bool> ShowOnionSkin;
	TAttribute<int32> OnionSkinFrames;
	TAttribute<int32> ForwardOnionSkinFrames;
	TAttribute<float> OnionSkinOpacity;
	TAttribute<int32> PreviousFlipbookIndex;
	TAttribute<bool> ShowForwardOnionSkin;
	TAttribute<int32> NextFlipbookIndex;
	TAttribute<bool> ShowReticle;
	TAttribute<FVector2D> ReticlePosition;
	TAttribute<bool> FlipX;
	TAttribute<bool> FlipY;
	TAttribute<bool> ShowReferenceSprite;
	TAttribute<TWeakObjectPtr<UPaperSprite>> ReferenceSprite;
	TAttribute<FIntPoint> ReferenceSpriteOffset;
	TAttribute<float> ReferenceSpriteOpacity;
	TAttribute<FIntPoint> QueueLargestDims;
	TAttribute<TWeakObjectPtr<UPaperSprite>> ExcludedPreviewSprite;
	TAttribute<FIntPoint> ExcludedPreviewOffset;

	// Drag state
	bool bIsDragging = false;
	bool bIsDraggingReticle = false;
	FVector2D DragStart;
	FIntPoint OffsetAtDragStart;

	// Pan state
	bool bIsPanning = false;
	FVector2D PanOffset = FVector2D::ZeroVector;
	FVector2D PanStart;

	// Helpers
	const FFlipbookProfileEntry* GetCurrentFlipbookData() const;
	const FSpriteExtractionInfo* GetCurrentExtractionInfo() const;
	UPaperSprite* GetSpriteAtFrame(int32 FrameIndex) const;
	FIntPoint GetOffsetAtFrame(int32 FrameIndex) const;
	FVector2D GetPivotShift(UPaperSprite* Sprite) const;
	FIntPoint GetLargestSpriteDims() const;

	// Cache for GetLargestSpriteDims
	mutable FIntPoint CachedLargestDims = FIntPoint(128, 128);
	mutable int32 CachedLargestDimsFlipbookIndex = -1;
	mutable TWeakObjectPtr<UPaperFlipbook> CachedLargestDimsFlipbook;

	// Queue playback locks the canvas center to prevent layout-driven shifts
	mutable FVector2D LockedCenter = FVector2D::ZeroVector;
	mutable bool bCenterLocked = false;

	// Coordinate conversion
	FVector2D ScreenToCanvas(const FGeometry& Geom, const FVector2D& ScreenPos) const;
	FVector2D CanvasToScreen(const FGeometry& Geom, const FVector2D& CanvasPos) const;
	FVector2D GetCanvasCenter(const FGeometry& Geom) const;
	float GetEffectiveZoom() const;
	FVector2D GetReticleScreenPosition(const FGeometry& Geom) const;

	// Drawing helpers
	void DrawCheckerboard(const FGeometry& Geom, FSlateWindowElementList& OutDrawElements, int32 LayerId) const;
	void DrawSprite(const FGeometry& Geom, FSlateWindowElementList& OutDrawElements, int32 LayerId, UPaperSprite* Sprite, FIntPoint Offset, bool bInFlipX, bool bInFlipY, FLinearColor Tint = FLinearColor::White) const;
	void DrawSpriteBounds(const FGeometry& Geom, FSlateWindowElementList& OutDrawElements, int32 LayerId, UPaperSprite* Sprite, FIntPoint Offset) const;
	void DrawOnionSkin(const FGeometry& Geom, FSlateWindowElementList& OutDrawElements, int32 LayerId) const;
	void DrawForwardOnionSkin(const FGeometry& Geom, FSlateWindowElementList& OutDrawElements, int32 LayerId) const;
	void DrawReferenceSprite(const FGeometry& Geom, FSlateWindowElementList& OutDrawElements, int32 LayerId) const;
	void DrawReticle(const FGeometry& Geom, FSlateWindowElementList& OutDrawElements, int32 LayerId) const;
	static constexpr float CheckerSize = 16.0f;
};

// Forward declarations for 3D viewport
class SHitbox3DViewport;
class FHitbox3DViewportClient;

/**
 * Viewport client for the 3D hitbox visualization.
 * Handles camera control and debug drawing of hitboxes.
 */
class FHitbox3DViewportClient : public FEditorViewportClient
{
public:
	FHitbox3DViewportClient(FPreviewScene* InPreviewScene, const TSharedRef<SHitbox3DViewport>& InViewport);
	virtual ~FHitbox3DViewportClient() override;

	// FEditorViewportClient interface
	virtual void Draw(const FSceneView* View, FPrimitiveDrawInterface* PDI) override;
	virtual void DrawCanvas(FViewport& InViewport, FSceneView& View, FCanvas& Canvas) override;
	virtual void Tick(float DeltaSeconds) override;
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
	virtual bool InputKey(FViewport* InViewport, int32 ControllerId, FKey Key, EInputEvent Event, float AmountDepressed = 1.f, bool bGamepad = false) override;
#else
	virtual bool InputKey(const FInputKeyEventArgs& EventArgs) override;
#endif

	// Set hitbox data to visualize
	void SetHitboxData(const FFrameHitboxData* InFrameData);
	void SetSelectedHitbox(int32 Index) { SelectedHitboxIndex = Index; }
	void SetSelectedSocket(int32 Index) { SelectedSocketIndex = Index; }

	// Focus camera on hitboxes
	void FocusOnHitboxes();

private:
	TOptional<FFrameHitboxData> FrameDataCopy;
	int32 SelectedHitboxIndex = -1;
	int32 SelectedSocketIndex = -1;

	FLinearColor GetHitboxColor(EHitboxType Type) const;
};

/**
 * 3D viewport widget for visualizing hitbox depth.
 * Uses Unreal's built-in viewport system for proper 3D rendering.
 */
class SHitbox3DViewport : public SEditorViewport
{
public:
	SLATE_BEGIN_ARGS(SHitbox3DViewport) {}
		SLATE_ARGUMENT(TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset>, Asset)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SHitbox3DViewport();

	// Update the displayed hitbox data
	void SetFrameData(const FFrameHitboxData* InFrameData);
	void SetSelectedHitbox(int32 Index);
	void SetSelectedSocket(int32 Index);

	// Set the sprite to display in the viewport
	void SetSprite(UPaperSprite* InSprite);

	// Re-point at a new profile asset on a Base Profile swap (kept in sync with the 2D canvas). The panel
	// re-pushes SetFrameData/SetSprite right after, so this just keeps the held asset consistent.
	void SetAsset(TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset> InAsset) { Asset = InAsset; }

	// Get the viewport client
	TSharedPtr<FHitbox3DViewportClient> GetHitbox3DClient() const { return ViewportClient; }

	// Refresh the viewport
	void RefreshViewport();

protected:
	// SEditorViewport interface
	virtual TSharedRef<FEditorViewportClient> MakeEditorViewportClient() override;

private:
	TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset> Asset;
	TSharedPtr<FPreviewScene> PreviewScene;
	TSharedPtr<FHitbox3DViewportClient> ViewportClient;
	UPaperSpriteComponent* SpriteComponent = nullptr;
};

/** Shared multi-select click handler for frame strips across tabs */
namespace FrameSelectionUtils
{
	inline void HandleFrameClick(
		TSet<int32>& SelectedFrames,
		int32& AnchorIndex,
		int32 ClickedIndex,
		const FPointerEvent& MouseEvent,
		int32 TotalFrameCount)
	{
		if (MouseEvent.IsControlDown())
		{
			if (SelectedFrames.Contains(ClickedIndex))
			{
				SelectedFrames.Remove(ClickedIndex);
			}
			else
			{
				SelectedFrames.Add(ClickedIndex);
			}
			AnchorIndex = ClickedIndex;
		}
		else if (MouseEvent.IsShiftDown() && AnchorIndex != INDEX_NONE)
		{
			int32 Start = FMath::Min(AnchorIndex, ClickedIndex);
			int32 End = FMath::Max(AnchorIndex, ClickedIndex);
			for (int32 i = Start; i <= End; ++i)
			{
				if (i >= 0 && i < TotalFrameCount)
				{
					SelectedFrames.Add(i);
				}
			}
		}
		else
		{
			SelectedFrames.Empty();
			AnchorIndex = ClickedIndex;
		}
	}
}

namespace HitboxCanvasUtils
{
	/** Clamp a hitbox top-left to sprite bounds with a NON-NEGATIVE upper bound (U9).
	 *  A hitbox wider/taller than the sprite (W > MaxX / H > MaxY) must stay draggable and pin at 0
	 *  rather than inverting the clamp into a negative pin. Pure integer math — unit-testable without
	 *  constructing the canvas widget. */
	inline FIntPoint ClampHitboxPositionToBounds(int32 X, int32 Y, int32 W, int32 H, int32 MaxX, int32 MaxY)
	{
		return FIntPoint(
			FMath::Clamp(X, 0, FMath::Max(0, MaxX - W)),
			FMath::Clamp(Y, 0, FMath::Max(0, MaxY - H)));
	}
}

class PAPER2DPLUSEDITOR_API FCharacterProfileAssetEditorToolkit : public FAssetEditorToolkit
{
public:
	virtual ~FCharacterProfileAssetEditorToolkit();

	void InitEditor(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost, UPaper2DPlusCharacterProfileAsset* InAsset);

	virtual FName GetToolkitFName() const override;
	virtual FText GetBaseToolkitName() const override;
	virtual FString GetWorldCentricTabPrefix() const override;
	virtual FLinearColor GetWorldCentricTabColorScale() const override;
	virtual void RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager) override;
	virtual void UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager) override;
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 3)
	virtual bool OnRequestClose(EAssetEditorCloseReason InCloseReason) override;
#else
	virtual bool OnRequestClose() override;
#endif

	// TASK-96: the former Overview + Animation Map tabs are merged into one "Animations" tab (a
	// List/Map view switcher). One tab id replaces the two; the old ids are gone (no external refs).
	static const FName AnimationsTabId;
	static const FName FlipbookListTabId;
	static const FName HitboxEditorTabId;
	static const FName SpriteEditorTabId;
	static const FName FrameTimingTabId;
	static const FName FrameEventsTabId;
	static const FName RootMotionTabId;
	static const FName ContextHostTabId;
	/** Profile-wide Catalog expectation checklist, permanently docked beside contextual Details. */
	static const FName ExpectedTagsTabId;
	static const FName CompletionTabId;
	static const FName RelatedProfilesTabId;
	/** Playback Queue — a docked sibling of Completion/Related Profiles in the lower-right stack. */
	static const FName PlaybackQueueTabId;

	/** Canonical Character workspace. A new name rejects stale layouts, including the retired Validation tab. */
	static TSharedRef<FTabManager::FLayout> CreateDefaultWorkspaceLayout();

	/** U5+ tool rehosts register their contextual provider through this one coordinator seam. */
	void RegisterToolPanelProvider(FName ToolId, TSharedPtr<IProfileToolPanelProvider> Provider);
	void UnregisterToolPanelProvider(FName ToolId, const TSharedPtr<IProfileToolPanelProvider>& Provider);

	/** Read-only probe used by Paper2DPlus.WorkspaceProbe and headless acceptance tests. */
	FString BuildWorkspaceProbeString();
	static TWeakPtr<FCharacterProfileAssetEditorToolkit> GActiveCharacterProfileToolkit;

private:
	UPaper2DPlusCharacterProfileAsset* EditedAsset = nullptr;
	TSharedPtr<FCharacterProfileEditorModel> EditorModel;
	TSharedPtr<IProfileItemPickerSource> AnimationPickerSource;
	TMap<FName, TSharedPtr<IProfileToolPanelProvider>> ToolPanelProviders;
	TWeakPtr<SProfileToolPanelHost> ContextPanelHost;
	TWeakPtr<SExpectedTagsPanel> ExpectedTagsPanel;
	TWeakPtr<SCharacterCompletionPanel> CompletionPanel;
	TWeakPtr<SPlaybackQueuePanel> PlaybackQueuePanel;
	/** Retained so the Asset-menu section survives every RegenerateMenusAndToolbars for the editor's life. */
	TSharedPtr<FExtender> AssetMenuExtender;
	/** False for world-centric hosts, which have no standalone Asset menu for FExtender to reach. Those
	 *  hosts get the same three commands through the compact shared-header Profile Actions fallback. */
	bool bStandaloneAssetMenuAvailable = false;
	FName ActiveToolId;
	TSharedRef<SDockTab> SpawnTab_Animations(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_FlipbookList(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_HitboxEditor(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_SpriteEditor(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_FrameTiming(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_FrameEvents(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_RootMotion(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_ContextHost(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_ExpectedTags(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_Completion(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_RelatedProfiles(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_PlaybackQueue(const FSpawnTabArgs& Args);
	/** HeaderActions is an OPTIONAL tool-specific control placed in the shared Current Animation header.
	 *  Only Animations supplies one (its compact View combo); every other tool passes nothing and gets the
	 *  header unchanged — no empty spacer. The caller must hand over a freshly built widget: header widgets
	 *  are parented to this one tab host and must never be cached or reused across tab spawns. */
	TSharedRef<SDockTab> MakeMainToolTab(
		const FText& Label,
		FName ToolId,
		TSharedRef<SWidget> ToolContent,
		TSharedPtr<SWidget> HeaderActions = nullptr);
	TSharedRef<SWidget> WrapMainToolContent(
		TSharedRef<SWidget> ToolContent,
		TSharedPtr<SWidget> HeaderActions = nullptr);
	/** Add the Character Profile section (Validate / Import JSON / Export JSON) to the standard Asset menu. */
	void RegisterAssetMenuExtender();
	/** The ONE builder for those three commands — shared by the Asset-menu extender and the world-centric
	 *  Profile Actions fallback, so the two hosting modes cannot expose different command sets. */
	void FillCharacterProfileMenuSection(FMenuBuilder& MenuBuilder);
	TSharedRef<SWidget> BuildProfileActionsMenu();
	void ExportProfileJson();
	void ImportProfileJson();
	bool HasEditedProfileAsset() const;
	void OpenValidation();
	void OpenProfileTools();
	void HandleMainToolActivated(TSharedRef<SDockTab> ActivatedTab, ETabActivationCause Cause, FName ToolId);
	void HandleAnimationsContextPanelRequested(FName PanelId);
	TSharedPtr<IProfileToolPanelProvider> FindToolPanelProvider(FName ToolId) const;

public:
	/** Brings the tool tab an issue points at to the front. Public because Character Profile
	 *  validation now lives in the Profile Tools window: the shared panel is hosted there, so issue
	 *  activation has to reach back into this editor to navigate. Without it, activating an issue
	 *  would silently do nothing. */
	void HandleValidationIssueActivated(const FPaper2DPlusValidationIssue& Issue);
};
