// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "ScopedTransaction.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Containers/Ticker.h"
#include "AnimationTimeline.h"
#include "SEditorViewport.h"
#include "EditorViewportClient.h"
#include "PreviewScene.h"
#include "Editor/EditorEngine.h"
#include "DragAndDrop/DecoratedDragDropOp.h"

class UPaperSprite;
class UPaperFlipbook;
class UPaperSpriteComponent;
class SVerticalBox;
class SHorizontalBox;
class SWidgetSwitcher;
class SSearchBox;
class FCharacterProfileAssetEditorToolkit;
class SSpriteAlignmentCanvas;
class SFrameTimingEditor;

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

/** Drag-drop operation for flipbook cards (shared across Overview groups and Tag Mappings). */
class FFlipbookGroupDragDropOp : public FDragDropOperation
{
public:
	DRAG_DROP_OPERATOR_TYPE(FFlipbookGroupDragDropOp, FDragDropOperation)

	TArray<int32> FlipbookIndices;

	static TSharedRef<FFlipbookGroupDragDropOp> NewFromCardDrag(const TArray<int32>& InFlipbookIndices, FName FromGroup);

	virtual TSharedPtr<SWidget> GetDefaultDecorator() const override;

private:
	FText DefaultHoverText;
	friend class SCharacterProfileAssetEditor;
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
		SLATE_ATTRIBUTE(int32, SelectedFlipbookIndex)
		SLATE_ATTRIBUTE(int32, SelectedFrameIndex)
		SLATE_ATTRIBUTE(EHitboxEditorTool, CurrentTool)
		SLATE_ATTRIBUTE(bool, ShowGrid)
		SLATE_ATTRIBUTE(float, Zoom)
		SLATE_ATTRIBUTE(uint8, VisibilityMask)
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

	// Nudge selection by delta
	void NudgeSelection(int32 DeltaX, int32 DeltaY);

	// Delete current selection
	void DeleteSelection();

	// Sprite dimensions for external access
	FVector2D GetSpriteDimensions() const;

private:
	TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset> Asset;
	TAttribute<int32> SelectedFlipbookIndex;
	TAttribute<int32> SelectedFrameIndex;
	TAttribute<EHitboxEditorTool> CurrentTool;
	TAttribute<bool> ShowGrid;
	TAttribute<float> Zoom;
	TAttribute<uint8> VisibilityMask;

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

	// For creating new hitbox
	FIntRect CreatingRect;

	// Helpers
	FLinearColor GetHitboxColor(EHitboxType Type) const;
	const FFrameHitboxData* GetCurrentFrame() const;
	FFrameHitboxData* GetCurrentFrameMutable() const;
	const FFlipbookHitboxData* GetCurrentFlipbookData() const;
	bool GetCurrentSpriteInfo(UPaperSprite*& OutSprite, FVector2D& OutDimensions) const;
	FVector2D GetLargestSpriteDims() const;

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
	void DrawGrid(const FGeometry& Geom, FSlateWindowElementList& OutDrawElements, int32 LayerId) const;
	void DrawHitbox(const FGeometry& Geom, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FHitboxData& HB, bool bSelected) const;
	void DrawSocket(const FGeometry& Geom, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FSocketData& Sock, bool bSelected) const;
	void DrawResizeHandles(const FGeometry& Geom, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FHitboxData& HB) const;
	void DrawCreatingRect(const FGeometry& Geom, FSlateWindowElementList& OutDrawElements, int32 LayerId) const;

	// Snap to grid
	int32 SnapToGrid(int32 Value) const;
	static constexpr int32 GridSize = 16;
	static constexpr float HandleSize = 8.0f;
	static constexpr float SocketHitRadius = 10.0f;
};

/**
 * Canvas widget for sprite alignment editing.
 * Displays sprite with offset controls, onion skinning, and reticle.
 */
class SSpriteAlignmentCanvas : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SSpriteAlignmentCanvas) {}
		SLATE_ARGUMENT(TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset>, Asset)
		SLATE_ATTRIBUTE(int32, SelectedFlipbookIndex)
		SLATE_ATTRIBUTE(int32, SelectedFrameIndex)
		SLATE_ATTRIBUTE(bool, ShowGrid)
		SLATE_ATTRIBUTE(float, Zoom)
		SLATE_ATTRIBUTE(bool, ShowOnionSkin)
		SLATE_ATTRIBUTE(int32, OnionSkinFrames)
		SLATE_ATTRIBUTE(float, OnionSkinOpacity)
		SLATE_ATTRIBUTE(int32, PreviousFlipbookIndex)
		SLATE_ATTRIBUTE(bool, ShowForwardOnionSkin)
		SLATE_ATTRIBUTE(int32, NextFlipbookIndex)
		SLATE_ATTRIBUTE(bool, ShowReticle)
		SLATE_ATTRIBUTE(ESpriteAnchor, ReticleAnchor)
		SLATE_ATTRIBUTE(bool, FlipX)
		SLATE_ATTRIBUTE(bool, FlipY)
		SLATE_ATTRIBUTE(bool, ShowReferenceSprite)
		SLATE_ATTRIBUTE(TWeakObjectPtr<UPaperSprite>, ReferenceSprite)
		SLATE_ATTRIBUTE(FIntPoint, ReferenceSpriteOffset)
		SLATE_ATTRIBUTE(float, ReferenceSpriteOpacity)
		SLATE_ATTRIBUTE(FIntPoint, QueueLargestDims)
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
	DECLARE_DELEGATE(FOnAlignmentDragStarted);
	DECLARE_DELEGATE(FOnAlignmentDragEnded);
	FOnAlignmentDragStarted OnDragStarted;
	FOnAlignmentDragEnded OnDragEnded;

private:
	TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset> Asset;
	TAttribute<int32> SelectedFlipbookIndex;
	TAttribute<int32> SelectedFrameIndex;
	TAttribute<bool> ShowGrid;
	TAttribute<float> Zoom;
	TAttribute<bool> ShowOnionSkin;
	TAttribute<int32> OnionSkinFrames;
	TAttribute<float> OnionSkinOpacity;
	TAttribute<int32> PreviousFlipbookIndex;
	TAttribute<bool> ShowForwardOnionSkin;
	TAttribute<int32> NextFlipbookIndex;
	TAttribute<bool> ShowReticle;
	TAttribute<ESpriteAnchor> ReticleAnchor;
	TAttribute<bool> FlipX;
	TAttribute<bool> FlipY;
	TAttribute<bool> ShowReferenceSprite;
	TAttribute<TWeakObjectPtr<UPaperSprite>> ReferenceSprite;
	TAttribute<FIntPoint> ReferenceSpriteOffset;
	TAttribute<float> ReferenceSpriteOpacity;
	TAttribute<FIntPoint> QueueLargestDims;

	// Drag state
	bool bIsDragging = false;
	FVector2D DragStart;
	FIntPoint OffsetAtDragStart;

	// Pan state
	bool bIsPanning = false;
	FVector2D PanOffset = FVector2D::ZeroVector;
	FVector2D PanStart;

	// Helpers
	const FFlipbookHitboxData* GetCurrentFlipbookData() const;
	const FSpriteExtractionInfo* GetCurrentExtractionInfo() const;
	FSpriteExtractionInfo* GetCurrentExtractionInfoMutable() const;
	UPaperSprite* GetSpriteAtFrame(int32 FrameIndex) const;
	FIntPoint GetOffsetAtFrame(int32 FrameIndex) const;
	FVector2D GetPivotShift(UPaperSprite* Sprite) const;
	FIntPoint GetLargestSpriteDims() const;

	// Cache for GetLargestSpriteDims
	mutable FIntPoint CachedLargestDims = FIntPoint(128, 128);
	mutable int32 CachedLargestDimsFlipbookIndex = -1;
	mutable TWeakObjectPtr<UPaperFlipbook> CachedLargestDimsFlipbook;

	// Coordinate conversion
	FVector2D ScreenToCanvas(const FGeometry& Geom, const FVector2D& ScreenPos) const;
	FVector2D CanvasToScreen(const FGeometry& Geom, const FVector2D& CanvasPos) const;
	FVector2D GetCanvasCenter(const FGeometry& Geom) const;
	float GetEffectiveZoom() const;
	FVector2D GetReticlePosition(const FGeometry& Geom) const;

	// Drawing helpers
	void DrawCheckerboard(const FGeometry& Geom, FSlateWindowElementList& OutDrawElements, int32 LayerId) const;
	void DrawGrid(const FGeometry& Geom, FSlateWindowElementList& OutDrawElements, int32 LayerId) const;
	void DrawSprite(const FGeometry& Geom, FSlateWindowElementList& OutDrawElements, int32 LayerId, UPaperSprite* Sprite, FIntPoint Offset, bool bInFlipX, bool bInFlipY, FLinearColor Tint = FLinearColor::White) const;
	void DrawSpriteBounds(const FGeometry& Geom, FSlateWindowElementList& OutDrawElements, int32 LayerId, UPaperSprite* Sprite, FIntPoint Offset) const;
	void DrawOnionSkin(const FGeometry& Geom, FSlateWindowElementList& OutDrawElements, int32 LayerId) const;
	void DrawForwardOnionSkin(const FGeometry& Geom, FSlateWindowElementList& OutDrawElements, int32 LayerId) const;
	void DrawReferenceSprite(const FGeometry& Geom, FSlateWindowElementList& OutDrawElements, int32 LayerId) const;
	void DrawReticle(const FGeometry& Geom, FSlateWindowElementList& OutDrawElements, int32 LayerId) const;
	void DrawOffsetIndicator(const FGeometry& Geom, FSlateWindowElementList& OutDrawElements, int32 LayerId) const;

	static constexpr int32 GridSize = 16;
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
	virtual bool InputKey(const FInputKeyEventArgs& EventArgs) override;

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
	/** Process a click on a frame index with Ctrl/Shift modifier support. */
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

/**
 * Main editor widget for Paper2DPlusCharacterProfileAsset.
 * Contains toolbar, canvas, animation/frame lists, and properties panel.
 */
class SCharacterProfileAssetEditor : public SCompoundWidget, public FEditorUndoClient
{
public:
	SLATE_BEGIN_ARGS(SCharacterProfileAssetEditor) {}
		SLATE_ARGUMENT(UPaper2DPlusCharacterProfileAsset*, Asset)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SCharacterProfileAssetEditor();

	// Keyboard handling — OnPreviewKeyDown intercepts arrow keys before children
	virtual FReply OnPreviewKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual bool SupportsKeyboardFocus() const override { return true; }

	/** Navigate to a flipbook's alignment tab (public for toolkit unapplied offset dialog). */
	void NavigateToFlipbookAlignment(int32 FlipbookIndex);

private:
	TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset> Asset;

	// Tab state
	int32 ActiveTabIndex = 0;
	TSharedPtr<SWidgetSwitcher> TabSwitcher;

	// Selection state
	int32 SelectedFlipbookIndex = 0;
	int32 SelectedFrameIndex = 0;

	// Frame multi-select state (transient — not serialized, not in undo)
	TSet<int32> SelectedFrames;
	int32 FrameSelectionAnchorIndex = INDEX_NONE;

	// Highlight colors for frame multi-select
	static const FLinearColor SelectedFrameHighlightColor;
	static const FLinearColor ActiveFrameColor;
	FString OverviewFlipbookSearchText;
	int32 BatchDamageValue = 10;
	int32 BatchKnockbackValue = 0;
	bool bSpriteFlipX = false;
	bool bSpriteFlipY = false;
	EHitboxEditorTool CurrentTool = EHitboxEditorTool::Edit;
	bool bShowGrid = false;
	float ZoomLevel = 1.0f;
	uint8 HitboxVisibilityMask = 0x07; // All types visible: Attack=0x01, Hurtbox=0x02, Collision=0x04

	// 3D view state
	bool bShow3DView = false;

	// Active transaction for undo support
	TUniquePtr<FScopedTransaction> ActiveTransaction;

	// Widget references
	TSharedPtr<SVerticalBox> FlipbookListBox;
	TSharedPtr<SHorizontalBox> FrameListBox;
	TSharedPtr<SVerticalBox> HitboxListBox;
	TSharedPtr<SVerticalBox> PropertiesBox;
	TSharedPtr<SVerticalBox> HitboxSidebarSectionsBox;
	TSharedPtr<SVerticalBox> OverviewFlipbookListBox;
	TSharedPtr<SVerticalBox> AlignmentLeftSectionsBox;
	TSharedPtr<SVerticalBox> AlignmentFlipbookListBox;
	TSharedPtr<SHorizontalBox> AlignmentFrameListBox;
	TSharedPtr<SCharacterProfileEditorCanvas> EditorCanvas;
	TSharedPtr<SHitbox3DViewport> Viewport3D;
	TSharedPtr<SWidgetSwitcher> CanvasViewSwitcher;
	TSharedPtr<SSpriteAlignmentCanvas> AlignmentCanvas;
	TSharedPtr<SFrameTimingEditor> FrameTimingEditor;

	// Alignment editor state
	bool bShowAlignmentGrid = false;
	float AlignmentZoomLevel = 1.0f;
	ESpriteAnchor AlignmentReticleAnchor = ESpriteAnchor::BottomCenter;  // Reticle position in alignment editor
	bool bAlignmentDragActive = false;

	// Playback state
	bool bIsPlaying = false;
	FTSTicker::FDelegateHandle PlaybackTickerHandle;
	float PlaybackPosition = 0.0f;                  // Time position within current animation (seconds)
	FFlipbookTimingData CachedPlaybackTiming;        // Cached to avoid per-tick allocation

	// Playback queue (transient — not saved to asset)
	TArray<int32> PlaybackQueue;                     // Flipbook indices
	int32 PlaybackQueueIndex = 0;                    // Current position in queue during playback
	TSharedPtr<SVerticalBox> PlaybackQueueListBox;

	// Onion skin state
	bool bShowOnionSkin = false;
	bool bShowForwardOnionSkin = false;
	int32 OnionSkinFrames = 1;
	float OnionSkinOpacity = 0.4f;

	// Ping-pong playback
	bool bPingPongPlayback = false;
	bool bPlaybackReversed = false;

	// Reference sprite state
	bool bShowReferenceSprite = false;
	int32 ReferenceFlipbookIndex = INDEX_NONE;
	int32 ReferenceFrameIndex = INDEX_NONE;
	float ReferenceSpriteOpacity = 0.4f;

	// Offset clipboard
	FIntPoint CopiedOffset = FIntPoint::ZeroValue;
	bool bHasCopiedOffset = false;

	// Flipbook rename/context menu state
	int32 PendingRenameFlipbookIndex = INDEX_NONE;
	TMap<int32, TSharedPtr<SInlineEditableTextBlock>> OverviewFlipbookNameTexts;
	TMap<int32, TSharedPtr<SInlineEditableTextBlock>> SidebarFlipbookNameTexts;
	TMap<int32, TSharedPtr<SInlineEditableTextBlock>> AlignmentFlipbookNameTexts;

	// Alignment search filter
	FString AlignmentFlipbookSearchFilter;
	TSharedPtr<SSearchBox> AlignmentFlipbookSearchBox;
	TWeakPtr<FActiveTimerHandle> AlignmentFlipbookSearchDebounceTimer;

	// Flipbook Groups State (transient — not serialized, not in undo)
	TSet<FName> CollapsedFlipbookGroups;                   // collapsed group names
	bool bFlipbookGroupGridView = true;                     // grid vs list toggle
	TSet<int32> SelectedFlipbookCards;                      // multi-select animation indices
	TSet<FName> PreSearchCollapsedState;                    // snapshot before search filtering
	int32 SelectionAnchorIndex = INDEX_NONE;                // anchor for Shift+click range select
	FName PendingRenameFlipbookGroup;                       // deferred EnterEditingMode() after rebuild
	TMap<FName, TSharedPtr<SInlineEditableTextBlock>> FlipbookGroupNameTexts;  // for programmatic rename entry
	TMap<int32, TSharedPtr<SInlineEditableTextBlock>> FlipbookGroupFlipbookNameTexts; // inline rename handles (cards/rows)
	TWeakPtr<FActiveTimerHandle> FlipbookGroupSearchDebounceTimer;  // search debounce (150ms)
	FString FlipbookGroupSearchText;                        // current search filter
	TSharedPtr<SVerticalBox> FlipbookGroupsListBox;         // main groups container

	// Persistent editor layout state
	TArray<FName> HitboxSidebarSectionOrder;
	TArray<FName> AlignmentLeftSectionOrder;
	float OverviewSplitterLeftRatio = 0.7f;
	float OverviewSplitterRightRatio = 0.3f;
	float HitboxSplitterLeftRatio = 0.2f;
	float HitboxSplitterCenterRatio = 0.55f;
	float HitboxSplitterRightRatio = 0.25f;
	float AlignmentSplitterLeftRatio = 0.2f;
	float AlignmentSplitterCenterRatio = 0.6f;
	float AlignmentSplitterRightRatio = 0.2f;

	// Active panel highlight state
	FName ActivePanelSectionId = NAME_None;
	static const FLinearColor ActivePanelHighlightColor;
	static const FLinearColor InactivePanelColor;

	// Flipbook rename/reorder methods
	void RenameFlipbook(int32 FlipbookIndex, const FString& NewName);
	void DuplicateFlipbook(int32 FlipbookIndex);
	void MoveFlipbookUp(int32 FlipbookIndex);
	void MoveFlipbookDown(int32 FlipbookIndex);
	UPaperFlipbook* GetFlipbookAssetForIndex(int32 FlipbookIndex) const;
	void OpenFlipbookAssetEditor(int32 FlipbookIndex);
	void BrowseToFlipbookAssetInContentBrowser(int32 FlipbookIndex);
	void OpenSpriteAssetEditor(UPaperSprite* Sprite);
	void BrowseToSpriteAssetInContentBrowser(UPaperSprite* Sprite);
	void ShowSpriteContextMenu(UPaperSprite* Sprite, const FVector2D& ScreenSpacePosition, int32 InReferenceFlipbookIndex = INDEX_NONE, int32 InReferenceFrameIndex = INDEX_NONE);
	void ShowFlipbookContextMenu(int32 FlipbookIndex);
	void ShowFlipbookPropertiesWindow(int32 FlipbookIndex);
	void TriggerFlipbookRename(int32 FlipbookIndex);

	// Frame reorder (alignment editor drag-drop)
	void ReorderFrame(int32 FromIndex, int32 ToIndex);

	// UI Builders - Main structure
	TSharedRef<SWidget> BuildTabBar();
	TSharedRef<SWidget> BuildOverviewTab();
	TSharedRef<SWidget> BuildHitboxEditorTab();
	TSharedRef<SWidget> BuildAlignmentEditorTab();
	TSharedRef<SWidget> BuildFrameTimingTab();
	TSharedRef<SWidget> BuildFlipbookGrid();
	TSharedRef<SWidget> BuildReorderableSectionCard(
		FName SectionId,
		const FText& SectionTitle,
		const FText& SectionTooltip,
		TSharedRef<SWidget> ContentWidget,
		bool bStretchContent = false);
	TSharedRef<SWidget> BuildTagMappingsPanel();
	void RefreshTagMappingsPanel();
	void EnsureRequiredTagMappingsExist();
	TSharedPtr<SVerticalBox> TagMappingsListBox;
	TArray<TSharedPtr<FString>> TagMappingFlipbookNameOptions;

	// Section layout helpers
	void InitializeSectionLayouts();
	void LoadSectionOrder(const FString& ConfigKey, const TArray<FName>& DefaultOrder, TArray<FName>& InOutOrder) const;
	void SaveSectionOrder(const FString& ConfigKey, const TArray<FName>& Order) const;
	bool CanMoveSection(const TArray<FName>& SectionOrder, FName SectionId, int32 Direction) const;
	void MoveSectionInOrder(TArray<FName>& SectionOrder, FName SectionId, int32 Direction, const FString& ConfigKey);
	void MoveHitboxSidebarSection(FName SectionId, int32 Direction);
	void MoveAlignmentLeftSection(FName SectionId, int32 Direction);
	void LoadFloatLayoutValue(const FString& ConfigKey, float DefaultValue, float& OutValue) const;
	void SaveFloatLayoutValue(const FString& ConfigKey, float Value) const;
	void RebuildHitboxSidebarSections();
	void RebuildAlignmentLeftSections();
	void SetActivePanelSection(FName SectionId);
	bool IsActivePanelSection(FName SectionId) const;
	/** Wrap content with active-panel highlight border. */
	TSharedRef<SWidget> WrapWithActivePanelHighlight(FName SectionId, float InnerPadding, TSharedRef<SWidget> Content);
	void TriggerPendingRenameIfNeeded(TMap<int32, TSharedPtr<SInlineEditableTextBlock>>& NameTexts);
	void ShowShortcutReferenceDialog() const;

	// UI Builders - Flipbook Groups
	TSharedRef<SWidget> BuildFlipbookGroupsPanel();
	void RefreshFlipbookGroupsPanel();
	TSharedRef<SWidget> BuildGroupSection(const FFlipbookGroupInfo* GroupInfo, FName GroupName, int32 NestLevel,
		const TMap<FName, TArray<const FFlipbookGroupInfo*>>& Tree,
		const TMap<FName, TArray<int32>>& AnimsByGroup,
		const TMap<FString, int32>& FlipbookNameUsageCounts);
	TSharedRef<SWidget> BuildFlipbookCard(int32 FlipbookIndex, const TMap<FString, int32>& FlipbookNameUsageCounts);
	void OnFlipbookGroupCardClicked(int32 FlipbookIndex, const FPointerEvent& MouseEvent);
	bool PassesFlipbookGroupSearch(const FFlipbookHitboxData& Animation) const;

	// Flipbook group management
	void CreateFlipbookGroup(FName ParentGroup = NAME_None);
	void DeleteFlipbookGroup(FName GroupName);
	void ShowFlipbookGroupContextMenu(FName GroupName, const FVector2D& CursorPos);
	void TriggerFlipbookGroupRename(FName GroupName);
	bool OnVerifyFlipbookGroupNameChanged(const FText& InText, FText& OutErrorMessage, FName CurrentGroupName);
	void OnFlipbookGroupNameCommitted(const FText& InText, ETextCommit::Type CommitType, FName OriginalGroupName);
	void OnOpenFlipbookGroupColorPicker(FName GroupName, FLinearColor CurrentColor);
	void OnFlipbookGroupColorCommitted(FLinearColor NewColor, FName GroupName);
	void AutoGroupByPrefix();
	void OnFlipbookGroupFlipbooksDrop(const TArray<int32>& FlipbookIndices, FName TargetGroup);

	// FEditorUndoClient interface
	virtual void PostUndo(bool bSuccess) override;
	virtual void PostRedo(bool bSuccess) override;

	// UI Builders - Alignment editor components
	TSharedRef<SWidget> BuildAlignmentToolbar();
	TSharedRef<SWidget> BuildAlignmentFlipbookList();
	TSharedRef<SWidget> BuildAlignmentFrameList();
	TSharedRef<SWidget> BuildAlignmentCanvasArea();
	TSharedRef<SWidget> BuildOffsetControlsPanel();
	TSharedRef<SWidget> BuildPlaybackQueuePanel();
	void RefreshPlaybackQueueList();

	// UI Builders - Hitbox editor components
	TSharedRef<SWidget> BuildToolbar();
	TSharedRef<SWidget> BuildToolPanel();
	TSharedRef<SWidget> BuildFlipbookList();
	TSharedRef<SWidget> BuildFrameList();
	TSharedRef<SWidget> BuildCanvasArea();
	TSharedRef<SWidget> BuildHitboxList();
	TSharedRef<SWidget> BuildPropertiesPanel();
	TSharedRef<SWidget> BuildCopyOperationsPanel();

	// Refresh functions
	void RefreshFlipbookList();
	void RefreshFrameList();
	void RefreshHitboxList();
	void RefreshPropertiesPanel();
	void RefreshOverviewFlipbookList();
	bool PassesOverviewFlipbookSearch(const FFlipbookHitboxData& Animation) const;
	void RefreshAlignmentFlipbookList();
	void RefreshAlignmentFrameList();
	void RefreshAfterNavigation();  // Tab-aware refresh after arrow key navigation
	void RefreshAll();
	void OnAssetExternallyModified(UObject* Object);
	FDelegateHandle OnObjectModifiedHandle;
	// Tab switching
	void SwitchToTab(int32 TabIndex);
	void OnEditHitboxesClicked(int32 FlipbookIndex);

	// Event handlers
	void OnFlipbookSelected(int32 Index);
	void OnFrameSelected(int32 Index);
	void OnToolSelected(EHitboxEditorTool Tool);
	void OnSelectionChanged(EHitboxSelectionType Type, int32 Index);
	void OnHitboxDataModified();
	void OnZoomChanged(float NewZoom);

	// Frame navigation
	void OnPrevFrameClicked();
	void OnNextFrameClicked();

	// Copy operations
	void OnCopyFromPrevious();
	void OnPropagateAllToGroup();
	void OnPropagateSelectedToGroup();
	void OnCopyToNextFrames();
	void OnMirrorAllFrames();
	void OnClampCurrentFlipbookHitboxesToBounds();
	void OnClampAllFlipbookHitboxesToBounds();
	void OnClearCurrentFrame();

	// Undo support
	void BeginTransaction(const FText& Description);
	void EndTransaction();

	// Add new hitbox/socket
	void AddNewHitbox();
	void AddNewSocket();
	void DeleteSelected();

	// Flipbook management
	void AddNewFlipbook();
	void RemoveSelectedFlipbook();
	void OpenFlipbookPicker(int32 FlipbookIndex);

	// Frame management
	void AddNewFrame();
	void RemoveSelectedFrame();

	// Alignment operations
	void NudgeOffset(int32 DeltaX, int32 DeltaY);
	void OnOffsetXChanged(int32 NewValue);
	void OnOffsetYChanged(int32 NewValue);
	void OnCopyOffset();
	void OnPasteOffset();
	void OnApplyOffsetToAll();
	void OnApplyOffsetToRemaining();
	void OnResetOffset();
	void OnAlignmentOffsetChanged(int32 DeltaX, int32 DeltaY);
	void OnApplyFlipToCurrentFrame();
	void OnApplyFlipToCurrentFlipbook();
	void OnApplyFlipToAllFlipbooks();
	void OnExcludeCurrentAlignmentFrame();
	void OnRestoreExcludedAlignmentFrame(int32 ExcludedFrameIndex);
	void OnRestoreAllExcludedAlignmentFrames();
	TSharedRef<SWidget> BuildAlignmentRestoreExcludedMenu();
	bool CanExcludeCurrentAlignmentFrame() const;
	void RefreshCurrentFrameFlipState();
	void RefreshAfterFrameExclusion(bool bDismissMenus = false);

	// Playback controls
	void StartPlayback();
	void StopPlayback();
	void TogglePlayback();
	bool OnPlaybackTick(float DeltaTime);

	// Playback queue
	void AddToPlaybackQueue(int32 FlipbookIndex);
	void RemoveFromPlaybackQueue(int32 QueueIndex);
	void ClearPlaybackQueue();
	void ReorderQueueEntry(int32 FromIndex, int32 ToIndex);
	bool OnQueuePlaybackTick(float DeltaTime);
	void SyncSelectionToQueueEntry(int32 QueueIndex);
	int32 FrameIndexFromPlaybackPosition(const FFlipbookTimingData& Timing, float Position) const;

	// Cross-flipbook navigation
	int32 GetAdjacentFlipbookIndex(int32 Direction) const; // -1 = previous, +1 = next

	// Reference sprite
	void SetReferenceSprite(int32 FlipbookIndex, int32 FrameIndex);
	void ClearReferenceSprite();

	// Edit alignment button handler
	void OnEditAlignmentClicked(int32 FlipbookIndex);

	// Edit timing button handler
	void OnEditTimingClicked(int32 FlipbookIndex);

	// Visibility filtering
	bool IsHitboxTypeVisible(EHitboxType Type) const;

	// Frame multi-select helpers
	void ForEachSelectedFrame(TFunctionRef<void(int32)> Op);
	void ClearFrameSelection();

	// Helpers
	const FFrameHitboxData* GetCurrentFrame() const;
	FFrameHitboxData* GetCurrentFrameMutable();
	const FFlipbookHitboxData* GetCurrentFlipbookData() const;
	FFlipbookHitboxData* GetCurrentFlipbookDataMutable();
	int32 GetCurrentFrameCount() const;
	TArray<int32> GetSortedFlipbookIndices() const;

	/** Build a grouped flipbook list into the given vertical box. ItemBuilder returns the widget for each flipbook index. Filter returns false to skip items. */
	void BuildGroupedFlipbookList(TSharedPtr<SVerticalBox> ListBox, TFunction<TSharedRef<SWidget>(int32)> ItemBuilder, TFunction<bool(int32)> Filter = nullptr);
	UPaperSprite* GetCurrentSprite() const;
};

/**
 * Asset Editor Toolkit for Paper2DPlusCharacterProfileAsset.
 * Provides a dockable, tabbed editor within the Unreal Editor.
 */
class FCharacterProfileAssetEditorToolkit : public FAssetEditorToolkit
{
public:
	virtual ~FCharacterProfileAssetEditorToolkit();

	void InitEditor(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost, UPaper2DPlusCharacterProfileAsset* InAsset);

	// FAssetEditorToolkit interface
	virtual FName GetToolkitFName() const override;
	virtual FText GetBaseToolkitName() const override;
	virtual FString GetWorldCentricTabPrefix() const override;
	virtual FLinearColor GetWorldCentricTabColorScale() const override;
	virtual void RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager) override;
	virtual void UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager) override;
	virtual bool OnRequestClose(EAssetEditorCloseReason InCloseReason) override;

	static void OpenEditor(UPaper2DPlusCharacterProfileAsset* Asset);

private:
	UPaper2DPlusCharacterProfileAsset* EditedAsset = nullptr;
	TSharedPtr<SCharacterProfileAssetEditor> EditorWidget;
	bool bCloseDialogShown = false;
	static const FName CharacterProfileEditorTabId;
	TSharedRef<SDockTab> SpawnEditorTab(const FSpawnTabArgs& Args);
};
