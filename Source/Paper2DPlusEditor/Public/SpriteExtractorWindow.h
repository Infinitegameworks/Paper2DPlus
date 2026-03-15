// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/Input/SComboBox.h"
#include "UObject/SoftObjectPath.h"

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
 * Represents a detected sprite region in the source texture
 */
struct FDetectedSprite
{
	FIntRect Bounds;			// Bounding rectangle in texture space
	FIntRect OriginalBounds;	// Original tight-fit bounds before uniform sizing
	bool bSelected = true;		// Is this sprite selected for extraction?
	int32 Index = 0;			// Detection order index

	FIntPoint GetSize() const { return FIntPoint(Bounds.Width(), Bounds.Height()); }
	FIntPoint GetOriginalSize() const { return FIntPoint(OriginalBounds.Width(), OriginalBounds.Height()); }
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
class PAPER2DPLUSEDITOR_API SSpriteExtractorCanvas : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SSpriteExtractorCanvas) {}
		SLATE_ARGUMENT(UTexture2D*, Texture)
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

private:
	UTexture2D* CurrentTexture = nullptr;
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

	// Draw new box
	bool bIsDrawingNewBox = false;
	FVector2D DrawBoxStart;
	FIntRect DrawBoxPreview;

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
	int32 AlphaThreshold = 1;  // Lowered default for better edge detection
	int32 MinSpriteSize = 4;
	bool bUse8DirectionalFloodFill = true;  // Include diagonal neighbors
	int32 IslandMergeDistance = 2;  // Merge islands within this distance

	// Grid mode settings
	int32 GridColumns = 4;
	int32 GridRows = 4;

	// Output settings
	FString OutputPath;
	bool bCreateSubfolder = true;
	bool bCreateFlipbook = true;
	float FlipbookFrameRate = 12.0f;

	// Naming system
	FString NamePrefix;
	FString NameBase;
	FString NameSeparator = TEXT("_");
	int32 SplitIndex = -1;
	TArray<int32> SeparatorPositions;
	TArray<TSharedPtr<int32>> SplitOptions;

	// Character asset integration
	bool bAddToCharacterAsset = false;
	UPaper2DPlusCharacterProfileAsset* TargetCharacterAsset = nullptr;
	FString AnimationName = TEXT("NewAnimation");

	// Detected sprites
	TArray<FDetectedSprite> DetectedSprites;

	// Undo/redo
	TArray<FExtractorStateSnapshot> UndoStack;
	TArray<FExtractorStateSnapshot> RedoStack;
	static constexpr int32 MaxUndoHistory = 50;

	// Auto-update detection
	bool bAutoUpdateDetection = false;
	TWeakPtr<FActiveTimerHandle> ActiveDebounceTimerHandle;
	static constexpr float AutoDetectDebounceSeconds = 0.3f;

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
	void DetectIslands();
	void DetectGrid();

	// Extraction
	int32 ExtractSprites();
	UPaperFlipbook* CreateFlipbook(const TArray<UPaperSprite*>& Sprites);

	// UI refresh
	void RefreshSpriteList();
	void RefreshCanvas();

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

	// Helpers
	bool IsPixelOpaque(const TArray<FColor>& Pixels, int32 Width, int32 X, int32 Y) const;
	void FloodFillMark(TArray<bool>& Visited, const TArray<FColor>& Pixels, int32 Width, int32 Height, int32 StartX, int32 StartY, FIntRect& OutBounds) const;
	void MergeNearbyIslands();
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

private:
	static void OnTextureContextMenuExtension(class FMenuBuilder& MenuBuilder, TArray<TWeakObjectPtr<UTexture2D>> Textures);
};
