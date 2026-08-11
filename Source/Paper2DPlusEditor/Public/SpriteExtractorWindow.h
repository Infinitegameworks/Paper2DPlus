// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SpriteExtractionUtils.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/GCObject.h"

class UTexture2D;
class UPaperSprite;
class UPaperFlipbook;
class UPaper2DPlusCharacterProfileAsset;

/**
 * Detection mode for finding sprites in a texture
 */
enum class ESpriteDetectionMode : uint8
{
	Island,		// Detect isolated sprite regions by flood fill
	Grid		// Split texture into uniform grid cells
};

/**
 * Handle type for edit mode resize handles
 */
enum class EHandleType : uint8
{
	None,
	TopLeft, Top, TopRight,
	Left, Right,
	BottomLeft, Bottom, BottomRight
};

/**
 * Snapshot of extractor state for undo/redo
 */
struct FExtractorStateSnapshot
{
	TArray<FDetectedSprite> Sprites;
};

/**
 * Canvas widget for displaying texture and detected sprites
 */
/** State of an optional cell-grid overlay on the canvas. Used by the bulk extractor's grid-confirmation UI. */
enum class ESpriteCanvasGridState : uint8
{
	Inferred,    // Muted yellow — detection's best guess
	Overridden,  // Blue — user edited Cols/Rows manually
	Confirmed    // Green — user accepted this grid
};

class PAPER2DPLUSEDITOR_API SSpriteExtractorCanvas : public SLeafWidget, public FGCObject
{
public:
	// FGCObject — roots CurrentTexture so the canvas doesn't dangle when GC runs during asset
	// creation (e.g., CreateSpriteFromBounds in the bulk extractor commit path). Without this,
	// OnPaint's GetSizeX() call would dispatch through a reclaimed vtable and crash.
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override { return TEXT("SSpriteExtractorCanvas"); }

	SLATE_BEGIN_ARGS(SSpriteExtractorCanvas)
		: _Texture(nullptr)
		, _bShowGridOverlay(false)
		, _GridDims(FIntPoint::ZeroValue)
		, _GridState(ESpriteCanvasGridState::Inferred)
	{}
		SLATE_ARGUMENT(UTexture2D*, Texture)
		/** Optional grid overlay — drawn as Cols × Rows cell rects above detection outlines. */
		SLATE_ATTRIBUTE(bool, bShowGridOverlay)
		SLATE_ATTRIBUTE(FIntPoint, GridDims)
		SLATE_ATTRIBUTE(ESpriteCanvasGridState, GridState)
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
	virtual void OnMouseLeave(const FPointerEvent& MouseEvent) override;
	virtual void OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent) override;
	virtual FCursorReply OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const override;

	// Set new texture to display
	void SetTexture(UTexture2D* NewTexture);

	// Sprite data
	void SetDetectedSprites(const TArray<FDetectedSprite>& InSprites);
	TArray<FDetectedSprite>& GetDetectedSprites() { return DetectedSprites; }

	// Selection
	void ToggleSpriteSelection(int32 Index);
	void SelectAll(bool bSelect);

	// View control
	void SetZoom(float NewZoom);
	float GetZoom() const { return ZoomLevel; }
	void ResetView();

	// Edit mode
	void EnterEditMode(int32 SpriteIndex);
	void ExitEditMode(bool bCommit);
	bool IsInEditMode() const { return EditingSpriteIndex >= 0; }
	int32 GetEditingSpriteIndex() const { return EditingSpriteIndex; }

	// Merge selection
	void ToggleMergeSelection(int32 Index);
	void ClearMergeSelection();
	int32 GetMergeSelectedCount() const { return MergeSelectedIndices.Num(); }
	const TSet<int32>& GetMergeSelected() const { return MergeSelectedIndices; }

	// Show green inner outline for OriginalBounds when they differ from Bounds
	void SetShowOriginalBounds(bool bShow) { bShowOriginalBounds = bShow; Invalidate(EInvalidateWidgetReason::Paint); }

private:
	TObjectPtr<UTexture2D> CurrentTexture = nullptr;
	TArray<FDetectedSprite> DetectedSprites;

	float ZoomLevel = 1.0f;
	FVector2D PanOffset = FVector2D::ZeroVector;
	FVector2D LastMousePos;
	bool bIsPanning = false;

	// Hover tracking
	int32 HoveredSpriteIndex = -1;

	// Edit mode
	int32 EditingSpriteIndex = -1;
	EHandleType DraggingHandle = EHandleType::None;
	FIntRect PreDragBounds;
	FVector2D DragStartTexturePos;

	// Merge selection
	TSet<int32> MergeSelectedIndices;

	// Original bounds overlay (green inner outline when Bounds != OriginalBounds)
	bool bShowOriginalBounds = false;

	// Draw new box
	bool bIsDrawingNewBox = false;
	FVector2D DrawBoxStart;
	FIntRect DrawBoxPreview;

	// Grid overlay attributes (bulk extractor grid-confirmation UI)
	TAttribute<bool> bShowGridOverlayAttr;
	TAttribute<FIntPoint> GridDimsAttr;
	TAttribute<ESpriteCanvasGridState> GridStateAttr;

	// Hit testing
	int32 HitTestSprite(const FGeometry& Geom, const FVector2D& ScreenPos) const;
	EHandleType HitTestHandle(const FGeometry& Geom, const FVector2D& ScreenPos) const;
	FVector2D ScreenToTexture(const FGeometry& Geom, const FVector2D& ScreenPos) const;
	FVector2D TextureToScreen(const FGeometry& Geom, const FVector2D& TexturePos) const;
	FIntRect ClampToTextureBounds(const FIntRect& Rect) const;

public:
	// Selection delegate — fired when a sprite is toggled on the canvas
	DECLARE_DELEGATE_OneParam(FOnSpriteSelectionToggled, int32);
	FOnSpriteSelectionToggled OnSpriteSelectionToggled;

	// Draw new box delegate — fired when user finishes Ctrl+drag
	DECLARE_DELEGATE_OneParam(FOnNewBoxDrawn, const FIntRect&);
	FOnNewBoxDrawn OnNewBoxDrawn;

	// Sprite edited delegate — fired when edit mode commits (index, new bounds)
	DECLARE_DELEGATE_TwoParams(FOnSpriteEdited, int32, const FIntRect&);
	FOnSpriteEdited OnSpriteEdited;

	// Edit begin delegate — fired at resize-handle mouse-down, BEFORE any bounds mutation,
	// so the owner can snapshot the pre-edit state for undo (U5/F18).
	DECLARE_DELEGATE(FOnEditBegin);
	FOnEditBegin OnEditBegin;

	// Zoom changed delegate — fired on mouse wheel zoom
	DECLARE_DELEGATE(FOnZoomChanged);
	FOnZoomChanged OnZoomChanged;
};

/**
 * Main sprite extractor window widget
 */
class PAPER2DPLUSEDITOR_API SSpriteExtractorWindow : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SSpriteExtractorWindow) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Set the initial texture to extract from */
	void SetInitialTexture(UTexture2D* Texture);

	/** Enter re-extract mode: updates existing sprites in-place instead of creating new assets.
	 *  The flipbook's keyframe sprites are updated via InitializeSprite when "Re-extract" is clicked. */
	void SetReExtractMode(UPaperFlipbook* Flipbook, int32 FlipbookIndex, UPaper2DPlusCharacterProfileAsset* ProfileAsset);

	// SWidget interface
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual bool SupportsKeyboardFocus() const override { return true; }

	/** Recently used textures (persisted via editor config) */
	static TArray<FSoftObjectPath> RecentTextures;
	static constexpr int32 MaxRecentTextures = 10;

	/** Add a texture to the recent list */
	static void AddToRecentTextures(UTexture2D* Texture);

	/** Load recent textures from editor config */
	static void LoadRecentTextures();

	/** Save recent textures to editor config */
	static void SaveRecentTextures();

private:
	// UI Components
	TSharedPtr<SSpriteExtractorCanvas> Canvas;
	TSharedPtr<SVerticalBox> SpriteListBox;
	TSharedPtr<SScrollBox> SpriteListScrollBox;
	TSharedPtr<STextBlock> ZoomText;
	TSharedPtr<STextBlock> SelectionCountText;
	TSharedPtr<SBorder> TextureSettingsBanner;
	TSharedPtr<SComboBox<TSharedPtr<int32>>> SplitComboBox;

	// Sprite list row widgets for scroll-to-view
	TArray<TSharedPtr<SWidget>> SpriteListRows;

	// Source texture
	UTexture2D* SourceTexture = nullptr;
	FString SourceTexturePath;

	// Detection settings
	ESpriteDetectionMode DetectionMode = ESpriteDetectionMode::Island;
	int32 AlphaThreshold = 1;
	int32 MinSpriteSize = 4;
	bool bUse8DirectionalFloodFill = true;  // Include diagonal neighbors
	int32 IslandMergeDistance = 2;  // Merge islands within this distance

	// Grid mode settings
	int32 GridColumns = 4;
	int32 GridRows = 4;

	/** Fill GridColumns/GridRows from FSpriteExtractionUtils::DetectFrameGrid instead of making the
	 *  user guess. Island detection answers "where is the ART", which shatters on particle VFX (one
	 *  potion sheet yields 65 islands for 11 frames) and merges frames on trimmed art; the gutter
	 *  rule answers "where are the FRAME BOUNDARIES" and needs no tuning. Reports the deciding rule
	 *  and refuses to overwrite the grid on a low-confidence guess. */
	FReply OnAutoDetectGridClicked();

	/** Result of the last auto-detect, shown under the Columns/Rows fields. Empty until run. */
	FText AutoDetectGridSummary;

	// Output settings
	FString OutputPath;
	bool bCreateSubfolder = true;
	bool bCreateFlipbook = true;
	float FlipbookFrameRate = 12.0f;

	// Uniform dimensions: when true, every extracted sprite gets the same
	// (maxWidth, maxHeight) across all selected sprites, with each sprite's
	// tight-fit bounds expanded outward to reach the uniform size.
	// UniformAnchor controls WHERE the original sprite sits inside the
	// expanded rect (e.g. BottomCenter = feet stay at bottom, extra space
	// above head — the standard for character animation sprites).
	bool bUniformDimensions = true;
	ESpriteAnchor UniformAnchor = ESpriteAnchor::BottomCenter;
	TArray<TSharedPtr<ESpriteAnchor>> UniformAnchorOptions;

	// Naming system
	FString NamePrefix;
	FString NameBase;
	FString NameSeparator = TEXT("_");
	int32 SplitIndex = -1;
	TArray<int32> SeparatorPositions;
	TArray<TSharedPtr<int32>> SplitOptions;

	// Repack texture: create a tight packed texture instead of referencing the original
	bool bRepackTexture = true;

	// Character asset integration
	bool bAddToCharacterAsset = false;
	UPaper2DPlusCharacterProfileAsset* TargetCharacterAsset = nullptr;
	FString AnimationName = TEXT("NewAnimation");

	// Re-extract mode: update existing sprites instead of creating new assets
	bool bReExtractMode = false;
	UPaperFlipbook* ReExtractFlipbook = nullptr;
	int32 ReExtractFlipbookIndex = INDEX_NONE;
	UPaper2DPlusCharacterProfileAsset* ReExtractProfileAsset = nullptr;

	// Detected sprites
	TArray<FDetectedSprite> DetectedSprites;

	// Cached uniform-bounds preview size (the "Extraction bounds: W x H" label). Recomputed only
	// when the selection set / SourceTexture changes (RecomputeUniformPreviewSize, called from
	// RefreshSpriteList) — NOT per-paint, because FSpriteExtractionUtils::ComputeUniformBounds
	// emits heavy per-row UE_LOG. The preview Text_Lambda just formats this value.
	FIntPoint CachedUniformPreviewSize = FIntPoint::ZeroValue;

	// Undo/redo
	TArray<FExtractorStateSnapshot> UndoStack;
	TArray<FExtractorStateSnapshot> RedoStack;
	static constexpr int32 MaxUndoHistory = 50;

	// Auto-update detection
	bool bAutoUpdateDetection = true;
	TWeakPtr<FActiveTimerHandle> ActiveDebounceTimerHandle;
	static constexpr float AutoDetectDebounceSeconds = 0.3f;

	// Sprite count jump warning (noise from low MinSpriteSize + low MergeDist)
	int32 PreviousDetectedSpriteCount = 0;
	bool bDismissedSpriteCountWarning = false;

	// UI Builders
	TSharedRef<SWidget> BuildMainToolbar();
	TSharedRef<SWidget> BuildTextureSection();
	TSharedRef<SWidget> BuildRecentTexturesSection();
	TSharedRef<SWidget> BuildDetectionSection();
	TSharedRef<SWidget> BuildOutputSection();
	TSharedRef<SWidget> BuildCharacterAssetSection();
	TSharedRef<SWidget> BuildSpriteListHeader();

	// Helper to update zoom/selection text
	void UpdateStatusTexts();
	int32 GetSelectedSpriteCount() const;

	// Texture settings
	void CheckTextureSettings();
	FReply OnApplyTextureSettingsClicked();

	// Actions
	FReply OnSelectTextureClicked();
	FReply OnDetectSpritesClicked();
	FReply OnExtractSpritesClicked();
	FReply OnSelectAllClicked();
	FReply OnDeselectAllClicked();
	FReply OnInvertSelectionClicked();

	// Detection algorithms
	void DetectIslands(const TArray<FColor>* PreloadedPixels = nullptr, int32 PreloadedWidth = 0, int32 PreloadedHeight = 0);
	void DetectGrid();

	// Extraction
	int32 ExtractSprites();
	int32 ReExtractSprites();
	UPaperFlipbook* CreateFlipbook(const TArray<UPaperSprite*>& Sprites);

	// Uniform dimensions helpers
	/** Max (width, height) across the given sprites' OriginalBounds. Returns (0,0) if empty. */
	FIntPoint ComputeUniformSpriteSize(const TArray<FDetectedSprite>& Sprites) const;
	/** Expand OriginalBounds to UniformSize using Anchor to decide where the
	 *  original sprite sits inside the expanded rect, clamped inside (TexW, TexH). */
	FIntRect ExpandBoundsToUniform(const FIntRect& OriginalBounds, FIntPoint UniformSize, ESpriteAnchor Anchor, int32 TexW, int32 TexH) const;

	/** Apply uniform bounds to Sprite.Bounds for canvas preview (keeps OriginalBounds as tight-fit). */
	void ApplyUniformBoundsPreview();

	// UI refresh
	void RefreshSpriteList();
	void RefreshCanvas();

	/** Recompute CachedUniformPreviewSize from the canvas's currently-selected sprites + SourceTexture.
	 *  Called from RefreshSpriteList (the selection-changed path) — never per-paint. */
	void RecomputeUniformPreviewSize();

	// Canvas selection callback
	void OnCanvasSpriteSelectionToggled(int32 SpriteIndex);

	// Undo/redo
	void PushUndoState();
	void Undo();
	void Redo();
	void RestoreState(const FExtractorStateSnapshot& State);

	// Merge
	void MergeSelectedSprites(const TArray<int32>& IndicesToMerge);
	FIntRect AbsorbContainedSprites(FIntRect Bounds, TArray<FDetectedSprite>& Sprites, int32 SkipIndex = INDEX_NONE);

	// Auto-update
	void ScheduleAutoDetect();
	EActiveTimerReturnType OnAutoDetectTimer(double InCurrentTime, float InDeltaTime);

	// Naming
	void AutoDetectNameParts(const FString& TextureName);
	FString GetSpriteName(int32 Index) const;
	FString GetFlipbookName() const;
	FString GetOutputFolderName() const;
	void UpdateOutputPath();

};

/**
 * Static actions for registering sprite extractor menus
 */
class PAPER2DPLUSEDITOR_API FSpriteExtractorActions
{
public:
	static void RegisterMenus();
	static void UnregisterMenus();

	static void OpenSpriteExtractor();
	static void OpenSpriteExtractorForTexture(UTexture2D* Texture);
	static void OpenSpriteExtractorForReExtract(UTexture2D* Texture, UPaperFlipbook* Flipbook, int32 FlipbookIndex, UPaper2DPlusCharacterProfileAsset* ProfileAsset);
	static void CombineTexturesAndOpen(const TArray<UTexture2D*>& Textures);
	static void RepackSpritesAsNewTexture(const TArray<UPaperSprite*>& Sprites);

};
