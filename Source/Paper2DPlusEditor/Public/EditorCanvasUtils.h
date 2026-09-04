// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/EngineVersionComparison.h"
#include "Rendering/DrawElements.h"
// UE 5.0 compat: FAppStyle doesn't exist, use FEditorStyle
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
#include "EditorStyleSet.h"
#define FAppStyle FEditorStyle
#define GetAppStyleSetName GetStyleSetName
#else
#include "Styling/AppStyle.h"
#endif
#include "Styling/CoreStyle.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Colors/SColorBlock.h"
#include "Widgets/Text/STextBlock.h"
#include "PaperSprite.h"
#include "PaperFlipbook.h"
#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "Paper2DPlusCharacterProfileAsset.h"

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 2
using FSlateVector2 = FVector2D;
#else
using FSlateVector2 = FVector2f;
#endif

// ToPaintGeometry takes FVector2D in 5.0-5.1, FVector2f (aka UE::Slate::FDeprecateVector2DParameter) in 5.2+.
// FSlateVector2 alias matches the right type per-version so one call works everywhere.
FORCEINLINE FPaintGeometry MakePaintGeometry(const FGeometry& Geom, const FVector2D& Size, const FSlateLayoutTransform& Transform)
{
	return Geom.ToPaintGeometry(FSlateVector2(Size), Transform);
}

/** Resolved UPaperSprite region for Slate. UVs always address the texture Slate will actually draw. */
struct FSpriteSlateRegion
{
	UTexture* Texture = nullptr;
	FVector2D UVMin = FVector2D::ZeroVector;
	FVector2D UVMax = FVector2D::ZeroVector;
	FVector2D PixelSize = FVector2D::ZeroVector;

	bool IsValid() const
	{
		return Texture != nullptr && PixelSize.X > 0.0f && PixelSize.Y > 0.0f;
	}
};

/**
 * The single sprite-to-Slate seam used by preview canvases and thumbnails.
 * UPaperSprite's atlas data pairs baked-atlas UVs with the baked texture and source-sheet UVs with
 * the source texture. Mixing GetBakedTexture() with GetSourceUV() displays the wrong atlas cell.
 */
struct FSpriteSlateAtlasUtils
{
	static bool ResolveAtlasData(const FSlateAtlasData& AtlasData, FSpriteSlateRegion& OutRegion)
	{
		OutRegion = FSpriteSlateRegion();
		if (!AtlasData.AtlasTexture
			|| AtlasData.StartUV.ContainsNaN()
			|| AtlasData.SizeUV.ContainsNaN()
			|| AtlasData.SizeUV.X <= 0.0f
			|| AtlasData.SizeUV.Y <= 0.0f)
		{
			return false;
		}

		const FVector2D UVMax = AtlasData.StartUV + AtlasData.SizeUV;
		if (UVMax.ContainsNaN())
		{
			return false;
		}

		const FVector2D PixelSize = AtlasData.GetSourceDimensions();
		if (PixelSize.ContainsNaN() || PixelSize.X <= 0.0f || PixelSize.Y <= 0.0f)
		{
			return false;
		}

		OutRegion.Texture = AtlasData.AtlasTexture;
		OutRegion.UVMin = AtlasData.StartUV;
		OutRegion.UVMax = UVMax;
		OutRegion.PixelSize = PixelSize;
		return true;
	}

	static bool ResolveSprite(UPaperSprite* Sprite, FSpriteSlateRegion& OutRegion)
	{
		OutRegion = FSpriteSlateRegion();
		if (!Sprite)
		{
			return false;
		}

		// Prefer Paper2D's baked-atlas tuple whenever it is ready. A freshly async-loaded editor
		// texture can be resident before its derived platform dimensions are available, though,
		// which temporarily makes GetSlateAtlasData invalid. In that window use the matching source
		// texture + source UV tuple; never combine source UVs with the baked atlas texture.
		if (ResolveAtlasData(Sprite->GetSlateAtlasData(), OutRegion))
		{
			return true;
		}

#if WITH_EDITOR
		UTexture2D* SourceTexture = Sprite->GetSourceTexture();
		if (!SourceTexture)
		{
			return false;
		}

		FIntPoint ImportedSize = SourceTexture->GetImportedSize();
		if (ImportedSize.X <= 0 || ImportedSize.Y <= 0)
		{
			ImportedSize = FIntPoint(SourceTexture->Source.GetSizeX(), SourceTexture->Source.GetSizeY());
		}
		const FVector2D TextureSize(ImportedSize.X, ImportedSize.Y);
		const FVector2D SourceUV(Sprite->GetSourceUV());
		const FVector2D SourceSize(Sprite->GetSourceSize());
		if (TextureSize.X <= 0.0f || TextureSize.Y <= 0.0f
			|| SourceUV.ContainsNaN() || SourceSize.ContainsNaN()
			|| SourceSize.X <= 0.0f || SourceSize.Y <= 0.0f)
		{
			return false;
		}

		const FVector2D UVMin = SourceUV / TextureSize;
		const FVector2D UVMax = (SourceUV + SourceSize) / TextureSize;
		if (UVMin.ContainsNaN() || UVMax.ContainsNaN())
		{
			return false;
		}

		OutRegion.Texture = SourceTexture;
		OutRegion.UVMin = UVMin;
		OutRegion.UVMax = UVMax;
		OutRegion.PixelSize = SourceSize;
		return true;
#else
		return false;
#endif
	}

	/** Always clears OutBrush first so an invalid/reimported sprite cannot retain a stale texture resource. */
	static bool ConfigureBrush(UPaperSprite* Sprite, FSlateBrush& OutBrush, FVector2D* OutPixelSize = nullptr)
	{
		OutBrush = FSlateBrush();
		if (OutPixelSize)
		{
			*OutPixelSize = FVector2D::ZeroVector;
		}

		FSpriteSlateRegion Region;
		if (!ResolveSprite(Sprite, Region))
		{
			return false;
		}

		OutBrush.SetResourceObject(Region.Texture);
		OutBrush.ImageSize = Region.PixelSize;
		OutBrush.DrawAs = ESlateBrushDrawType::Image;
		OutBrush.Tiling = ESlateBrushTileType::NoTile;
		OutBrush.SetUVRegion(FBox2D(Region.UVMin, Region.UVMax));
		if (OutPixelSize)
		{
			*OutPixelSize = Region.PixelSize;
		}
		return true;
	}
};

/**
 * Shared utilities for editor canvas rendering.
 * Provides consistent visual elements across all canvas tabs.
 */
struct FEditorCanvasUtils
{
	/**
	 * Draw a checkerboard background pattern.
	 * Used by Hitbox, Sprite Editor, and Frame Timing canvases for visual consistency.
	 */
	static void DrawCheckerboard(
		FSlateWindowElementList& OutDrawElements,
		int32 LayerId,
		const FGeometry& Geom,
		float CheckSize = 16.0f,
		FLinearColor DarkColor = FLinearColor(0.1f, 0.1f, 0.1f, 1.0f),
		FLinearColor LightColor = FLinearColor(0.15f, 0.15f, 0.15f, 1.0f))
	{
		const FVector2D LocalSize = Geom.GetLocalSize();

		// Draw dark background
		FSlateDrawElement::MakeBox(
			OutDrawElements,
			LayerId,
			MakePaintGeometry(Geom, FVector2D(LocalSize), FSlateLayoutTransform()),
			FAppStyle::Get().GetBrush("WhiteBrush"),
			ESlateDrawEffect::None,
			DarkColor
		);

		// Draw checkerboard pattern
		for (float Y = 0; Y < LocalSize.Y; Y += CheckSize)
		{
			for (float X = 0; X < LocalSize.X; X += CheckSize)
			{
				int32 CheckX = FMath::FloorToInt(X / CheckSize);
				int32 CheckY = FMath::FloorToInt(Y / CheckSize);
				bool bDark = ((CheckX + CheckY) % 2) == 0;

				if (bDark)
				{
					FSlateDrawElement::MakeBox(
						OutDrawElements,
						LayerId,
						MakePaintGeometry(Geom, FVector2D(CheckSize, CheckSize), FSlateLayoutTransform(FVector2D(X, Y))),
						FAppStyle::Get().GetBrush("WhiteBrush"),
						ESlateDrawEffect::None,
						LightColor
					);
				}
			}
		}
	}

	/**
	 * Draw a checkerboard in a sub-region of the widget (e.g., behind a zoomed/panned texture).
	 * Cells are clipped to the DrawSize boundary.
	 */
	static void DrawCheckerboard(
		FSlateWindowElementList& OutDrawElements,
		int32 LayerId,
		const FGeometry& Geom,
		FVector2D DrawOffset,
		FVector2D DrawSize,
		float CheckSize = 16.0f,
		FLinearColor DarkColor = FLinearColor(0.2f, 0.2f, 0.2f, 1.0f),
		FLinearColor LightColor = FLinearColor(0.4f, 0.4f, 0.4f, 1.0f))
	{
		if (CheckSize <= 0.0f) return;

		int32 NumChecksX = FMath::CeilToInt(DrawSize.X / CheckSize);
		int32 NumChecksY = FMath::CeilToInt(DrawSize.Y / CheckSize);

		for (int32 Y = 0; Y < NumChecksY; Y++)
		{
			for (int32 X = 0; X < NumChecksX; X++)
			{
				FLinearColor Color = ((X + Y) % 2 == 0) ? LightColor : DarkColor;
				FVector2D CellPos = DrawOffset + FVector2D(X * CheckSize, Y * CheckSize);
				FVector2D CellSize(
					FMath::Min(CheckSize, DrawSize.X - X * CheckSize),
					FMath::Min(CheckSize, DrawSize.Y - Y * CheckSize)
				);

				FSlateDrawElement::MakeBox(
					OutDrawElements,
					LayerId,
					MakePaintGeometry(Geom, CellSize, FSlateLayoutTransform(CellPos)),
					FAppStyle::Get().GetBrush("WhiteBrush"),
					ESlateDrawEffect::None,
					Color
				);
			}
		}
	}
	/**
	 * Draw a flipbook sprite at a given center with FrameExtractionInfo offset + pivot shift applied.
	 * ALL canvas widgets that render flipbook sprites MUST use this function to prevent sprite bouncing.
	 *
	 * @param OutDrawElements  Slate draw element list
	 * @param LayerId          Current layer
	 * @param Geom             Widget geometry
	 * @param Sprite           The sprite to draw
	 * @param AnimData         The flipbook hitbox data (for FrameExtractionInfo). May be null.
	 * @param FrameIndex       Key frame index (for offset lookup)
	 * @param Center           Center position in widget space (e.g., GetCanvasCenter())
	 * @param Zoom             Effective zoom factor
	 * @param Tint             Color tint (default white)
	 * @param OutDrawPos       (Optional) Receives the final draw position for overlays/outlines
	 * @param OutDrawSize      (Optional) Receives the final draw size
	 * @param bFlipX           Mirror both art and authored horizontal offsets around Center
	 * @return true if the sprite was drawn
	 */
	static bool DrawFlipbookSprite(
		FSlateWindowElementList& OutDrawElements,
		int32 LayerId,
		const FGeometry& Geom,
		UPaperSprite* Sprite,
		const FFlipbookProfileEntry* AnimData,
		int32 FrameIndex,
		FVector2D Center,
		float Zoom,
		FLinearColor Tint = FLinearColor::White,
		FVector2D* OutDrawPos = nullptr,
		FVector2D* OutDrawSize = nullptr,
		bool bFlipX = false)
	{
		if (!Sprite) return false;

		const FVector2D SpriteSourceSize = FVector2D(Sprite->GetSourceSize());
		FSlateBrush SpriteBrush;
		if (!FSpriteSlateAtlasUtils::ConfigureBrush(Sprite, SpriteBrush)) return false;

		FVector2D DrawSize = SpriteSourceSize * Zoom;
		FVector2D DrawPos = Center - DrawSize * 0.5f;

		// Apply the same Profile-owned alignment pair used at runtime. TrimOffset replaces extraction-
		// generated custom sprite pivots; omitting it here makes repaired/bulk-trimmed art disagree with
		// gameplay previews even though the runtime component is correct.
		if (AnimData && AnimData->CombatData.FrameExtractionInfo.IsValidIndex(FrameIndex))
		{
			const FSpriteExtractionInfo& Info = AnimData->CombatData.FrameExtractionInfo[FrameIndex];
			const FVector2D ProfileOffset = FVector2D(Info.SpriteOffset + Info.TrimOffset);
			DrawPos.X += (bFlipX ? -ProfileOffset.X : ProfileOffset.X) * Zoom;
			DrawPos.Y += ProfileOffset.Y * Zoom;
		}

		// Apply pivot shift (accounts for auto-applied offsets baked into sprite pivot)
		FVector2D SourceCenter = Sprite->GetSourceUV() + Sprite->GetSourceSize() * 0.5f;
		FVector2D PivotPos = Sprite->GetPivotPosition();
		FVector2D PivotShift = FVector2D(SourceCenter) - FVector2D(PivotPos);
		DrawPos.X += (bFlipX ? -PivotShift.X : PivotShift.X) * Zoom;
		DrawPos.Y += PivotShift.Y * Zoom;

		SpriteBrush.Mirroring = bFlipX
			? ESlateBrushMirrorType::Horizontal
			: ESlateBrushMirrorType::NoMirror;

		FSlateDrawElement::MakeBox(OutDrawElements, LayerId,
			MakePaintGeometry(Geom, DrawSize, FSlateLayoutTransform(DrawPos)),
			&SpriteBrush, ESlateDrawEffect::None, Tint);

		if (OutDrawPos) *OutDrawPos = DrawPos;
		if (OutDrawSize) *OutDrawSize = DrawSize;
		return true;
	}
};

/**
 * Renders a UPaperSprite directly from its authoritative Slate atlas region.
 * Bypasses FAssetThumbnailPool/SViewport pipeline for immediate rendering.
 * Use inside SBox with WidthOverride/HeightOverride to control display size.
 */
class SSpriteThumbnail : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SSpriteThumbnail) {}
		// Accepts either a static sprite (.Sprite(Ptr)) or a bound one (.Sprite_Lambda(...)). When bound, the
		// thumbnail re-resolves the sprite each paint and rebuilds its brush only when the sprite changes, so an
		// icon can follow playback / frame changes without rebuilding the widget (callers just Invalidate it).
		SLATE_ATTRIBUTE(UPaperSprite*, Sprite)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		SetClipping(EWidgetClipping::ClipToBounds);
		SpriteAttr = InArgs._Sprite;
		UpdateBrushForSprite(SpriteAttr.Get());
	}

	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(32, 32); }

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override
	{
		// Re-resolve the (possibly bound) sprite; only rebuild the brush when it actually changed.
		UPaperSprite* Current = SpriteAttr.Get();
		if (Current != CachedSprite.Get())
		{
			UpdateBrushForSprite(Current);
		}

		// Checkerboard behind sprite for transparency visualization
		FEditorCanvasUtils::DrawCheckerboard(OutDrawElements, LayerId, AllottedGeometry, 8.0f);

		if (bHasTexture)
		{
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 1,
				AllottedGeometry.ToPaintGeometry(),
				&Brush, ESlateDrawEffect::None, FLinearColor::White);
		}
		return LayerId + 1;
	}

private:
	void UpdateBrushForSprite(UPaperSprite* Sprite) const
	{
		CachedSprite = Sprite;
		bHasTexture = FSpriteSlateAtlasUtils::ConfigureBrush(Sprite, Brush);
	}

	TAttribute<UPaperSprite*> SpriteAttr;
	mutable FSlateBrush Brush;
	mutable bool bHasTexture = false;
	mutable TWeakObjectPtr<UPaperSprite> CachedSprite;
};

// ==========================================
// FRAME STRIP CELL — STANDARD LOOK
// ==========================================

/** Arguments for building a standard frame strip cell. */
struct FFrameStripCellArgs
{
	/** The sprite to display in the cell. May be null (shows empty placeholder). */
	UPaperSprite* Sprite = nullptr;

	/** Optional: a custom widget that fills the sprite thumbnail box INSTEAD of the single `Sprite`
	 *  (e.g. a layered-composite thumbnail). When set, it takes precedence over `Sprite`. `SpriteOverlay`
	 *  is still drawn on top of it, so hitbox silhouettes / onion-skin tints keep working over a composite. */
	TSharedPtr<SWidget> SpriteContentOverride;

	/** The frame index — used as the visible label below the sprite. */
	int32 FrameIndex = 0;

	/** Sprite thumbnail size in pixels (default 48x48). Cell width = SpriteSize + 4. */
	float SpriteSize = 48.0f;

	/** Lambda that returns true if this cell should show the primary selection highlight. */
	TFunction<bool()> IsSelected;

	/** Optional: Lambda that returns true if this cell is part of a multi-selection. */
	TFunction<bool()> IsMultiSelected;

	/** Mouse button down handler. Receives the pointer event. */
	TFunction<FReply(const FPointerEvent&)> OnMouseButtonDown;

	/** Optional 4px tall colored bar below the sprite (e.g., phase color, motion indicator). */
	TAttribute<FLinearColor> BottomBarColor;
	bool bShowBottomBar = false;

	/** Optional overlay widget rendered on top of the cell (e.g., exclusion badge, FX marker).
	 *  Spans the ENTIRE cell (sprite + label + badges + border). Use for corner-anchored
	 *  elements or full-cell dims — NOT for content whose geometry must match the sprite
	 *  (use SpriteOverlay for that). */
	TSharedPtr<SWidget> Overlay;

	/** Optional overlay widget rendered inside the sprite thumbnail box (SpriteSize x SpriteSize).
	 *  Its AllottedGeometry is exactly the sprite area, so widgets that need to paint in
	 *  sprite pixel space (e.g., hitbox silhouettes, onion-skin tints) can compute a correct
	 *  scale factor. Independent of `Overlay` — both can be set. */
	TSharedPtr<SWidget> SpriteOverlay;

	/** Optional content rendered inside the cell BELOW the frame number label
	 *  (e.g., hitbox count badges, status indicators). Lives inside the cell
	 *  border, so it's always visible — unlike Overlay which can be clipped. */
	TSharedPtr<SWidget> BelowLabelContent;

	/** Selection highlight colors (defaults: phase tool green for primary, blue for multi). */
	FLinearColor SelectedBorderColor = FLinearColor(0.20f, 0.60f, 0.30f, 1.0f);
	FLinearColor MultiSelectedBorderColor = FLinearColor(0.15f, 0.45f, 0.75f, 1.0f);
	FLinearColor UnselectedBorderColor = FLinearColor(0.08f, 0.08f, 0.08f, 1.0f);

	/** Frame label color override (default: dim grey). */
	FLinearColor LabelColor = FLinearColor(0.5f, 0.5f, 0.5f);
};

/**
 * Standard frame strip cell utility — matches the visual look of the Phase tool's frame strip.
 *
 * Use this for any horizontal frame thumbnail strip across editor tools to ensure consistent
 * visuals (52px wide cell, 48x48 sprite, 4px optional color bar, frame number, modest border).
 *
 * Each tool customizes click handlers, selection state, and decoration via FFrameStripCellArgs
 * but the visual structure stays consistent across all tools.
 */
struct FFrameStripCellUtils
{
	static TSharedRef<SWidget> Build(const FFrameStripCellArgs& Args)
	{
		TSharedRef<SVerticalBox> CellContent = SNew(SVerticalBox);

		const float SpriteDim = Args.SpriteSize;

		// Sprite thumbnail (or composite override, or empty placeholder), optionally wrapped in an SOverlay
		// so Args.SpriteOverlay's AllottedGeometry is exactly the sprite area.
		TSharedRef<SWidget> SpriteWidget = Args.SpriteContentOverride.IsValid()
			? Args.SpriteContentOverride.ToSharedRef()
			: (Args.Sprite
				? StaticCastSharedRef<SWidget>(SNew(SSpriteThumbnail).Sprite(Args.Sprite))
				: StaticCastSharedRef<SWidget>(SNew(SBorder)
					.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.DarkGroupBorder"))
					.HAlign(HAlign_Center).VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(NSLOCTEXT("FrameStrip", "Empty", "-"))
						.ColorAndOpacity(FSlateColor(FLinearColor(0.4f, 0.4f, 0.4f)))
					]));

		TSharedRef<SWidget> SpriteLayer = Args.SpriteOverlay.IsValid()
			? StaticCastSharedRef<SWidget>(SNew(SOverlay)
				+ SOverlay::Slot() [ SpriteWidget ]
				+ SOverlay::Slot() [ Args.SpriteOverlay.ToSharedRef() ])
			: SpriteWidget;

		CellContent->AddSlot()
		.AutoHeight()
		[
			SNew(SBox)
			.WidthOverride(SpriteDim)
			.HeightOverride(SpriteDim)
			[
				SpriteLayer
			]
		];

		// Optional 4px color bar
		if (Args.bShowBottomBar)
		{
			TAttribute<FLinearColor> BarColor = Args.BottomBarColor;
			CellContent->AddSlot()
			.AutoHeight()
			[
				SNew(SBox)
				.HeightOverride(4.0f)
				[
					SNew(SColorBlock)
					.Color_Lambda([BarColor]() { return BarColor.Get(); })
				]
			];
		}

		// Frame number label
		CellContent->AddSlot()
		.AutoHeight()
		.HAlign(HAlign_Center)
		[
			SNew(STextBlock)
			.Text(FText::AsNumber(Args.FrameIndex))
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 7))
			.ColorAndOpacity(FSlateColor(Args.LabelColor))
		];

		// Optional below-label content (hitbox badges, status indicators, etc.)
		if (Args.BelowLabelContent.IsValid())
		{
			CellContent->AddSlot()
			.AutoHeight()
			.HAlign(HAlign_Center)
			.Padding(0, 1, 0, 1)
			[
				Args.BelowLabelContent.ToSharedRef()
			];
		}

		// Build the bordered cell with selection lambda (supports primary + multi-select)
		TFunction<bool()> IsSelectedLambda = Args.IsSelected;
		TFunction<bool()> IsMultiSelectedLambda = Args.IsMultiSelected;
		FLinearColor SelColor = Args.SelectedBorderColor;
		FLinearColor MultiColor = Args.MultiSelectedBorderColor;
		FLinearColor UnselColor = Args.UnselectedBorderColor;

		TSharedRef<SBorder> Cell = SNew(SBorder)
			.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.DarkGroupBorder"))
			.BorderBackgroundColor_Lambda([IsSelectedLambda, IsMultiSelectedLambda, SelColor, MultiColor, UnselColor]() -> FSlateColor
			{
				if (IsSelectedLambda && IsSelectedLambda()) return FSlateColor(SelColor);
				if (IsMultiSelectedLambda && IsMultiSelectedLambda()) return FSlateColor(MultiColor);
				return FSlateColor(UnselColor);
			})
			.Padding(1)
			.OnMouseButtonDown_Lambda([Handler = Args.OnMouseButtonDown](const FGeometry&, const FPointerEvent& Event) -> FReply
			{
				if (Handler) return Handler(Event);
				return FReply::Unhandled();
			})
			[
				CellContent
			];

		// Wrap in AutoHeight vertical box so cell hugs its content and doesn't
		// stretch to fill parent slot height (keeps frame strip tight).
		TSharedRef<SWidget> CellRoot = Args.Overlay.IsValid()
			? StaticCastSharedRef<SWidget>(SNew(SOverlay)
				+ SOverlay::Slot() [ Cell ]
				+ SOverlay::Slot() [ Args.Overlay.ToSharedRef() ])
			: StaticCastSharedRef<SWidget>(Cell);

		return SNew(SBox)
		.WidthOverride(SpriteDim + 4.0f)
		.VAlign(VAlign_Top)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				CellRoot
			]
		];
	}
};

/**
 * Renders a UPaperFlipbook by showing its first frame statically,
 * then animating through all frames on mouse hover.
 * Uses direct texture rendering like SSpriteThumbnail.
 */
class SFlipbookThumbnail : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SFlipbookThumbnail) {}
		SLATE_ARGUMENT(UPaperFlipbook*, Flipbook)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		SetClipping(EWidgetClipping::ClipToBounds);
		StatusText = FText::FromString(TEXT("No FB"));
		SetFlipbook(InArgs._Flipbook);
	}

	/** Replace the visual source without rebuilding the surrounding Slate tree. */
	void SetFlipbook(UPaperFlipbook* InFlipbook)
	{
		if (Flipbook.Get() == InFlipbook)
		{
			return;
		}

		if (AnimTimerHandle.IsValid())
		{
			UnRegisterActiveTimer(AnimTimerHandle.Pin().ToSharedRef());
		}
		AnimTimerHandle.Reset();
		if (InitialResolveTimerHandle.IsValid())
		{
			UnRegisterActiveTimer(InitialResolveTimerHandle.Pin().ToSharedRef());
		}
		InitialResolveTimerHandle.Reset();

		Flipbook.Reset(InFlipbook);
		Brush = FSlateBrush();
		bHasTexture = false;
		InitialFrameIndex = INDEX_NONE;
		InitialResolveAttempts = 0;
		CurrentFrame = 0;
		TickAccumulator = 0.0;
		FrameRunAccumulator = 0;
		StatusText = FText::FromString(TEXT("No FB"));
		if (InFlipbook)
		{
			StatusText = FText::FromString(TEXT("No Frames"));
		}
		if (Flipbook.IsValid() && Flipbook->GetNumKeyFrames() > 0)
		{
			InitialFrameIndex = FindFirstRenderableFrameIndex(Flipbook->GetNumKeyFrames());
			CurrentFrame = (InitialFrameIndex != INDEX_NONE) ? InitialFrameIndex : 0;
			StatusText = FText::FromString(TEXT("Loading"));
			bHasTexture = SetBrushFromFrame(CurrentFrame);

			// Some flipbooks do not have a renderable first frame; fall back to any valid frame.
			if (!bHasTexture)
			{
				bHasTexture = SetBrushFromBestAvailableFrame();
			}

			// Some sprite textures resolve a frame or two after widget construction.
			// Keep probing briefly so cards are populated without requiring hover.
			InitialResolveAttempts = 0;
			InitialResolveTimerHandle = RegisterActiveTimer(0.1f, FWidgetActiveTimerDelegate::CreateSP(
				this, &SFlipbookThumbnail::OnInitialResolveTick));
		}
		Invalidate(EInvalidateWidgetReason::LayoutAndVolatility);
	}

	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(64, 64); }

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override
	{
		if (!FSlateRect::DoRectanglesIntersect(AllottedGeometry.GetLayoutBoundingRect(), MyCullingRect))
		{
			return LayerId;
		}

		FEditorCanvasUtils::DrawCheckerboard(OutDrawElements, LayerId, AllottedGeometry, 8.0f);

		const bool bRenderable = bHasTexture && HasRenderableResource();
		int32 HighestPaintedLayer = LayerId;
		if (bRenderable)
		{
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 1,
				AllottedGeometry.ToPaintGeometry(),
				&Brush, ESlateDrawEffect::None, FLinearColor::White);
			HighestPaintedLayer = LayerId + 1;
		}
		else if (!StatusText.IsEmpty())
		{
			FSlateDrawElement::MakeText(
				OutDrawElements,
				LayerId + 2,
				MakePaintGeometry(AllottedGeometry, FVector2D(60.0f, 14.0f), FSlateLayoutTransform(FVector2D(2.0f, 24.0f))),
				StatusText,
				FCoreStyle::GetDefaultFontStyle("Regular", 8),
				ESlateDrawEffect::None,
				FLinearColor(0.78f, 0.78f, 0.78f, 0.95f));
			HighestPaintedLayer = LayerId + 2;
		}
		return HighestPaintedLayer;
	}

	virtual void OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		SLeafWidget::OnMouseEnter(MyGeometry, MouseEvent);
		const int32 LiveNumFrames = ReconcileFrameTopology();
		if (LiveNumFrames > 1)
		{
			TickAccumulator = 0.0;
			FrameRunAccumulator = 0;
			CurrentFrame = (InitialFrameIndex != INDEX_NONE) ? InitialFrameIndex : 0;
			if (!SetBrushFromFrame(CurrentFrame))
			{
				SetBrushFromBestAvailableFrame();
			}
			Invalidate(EInvalidateWidgetReason::LayoutAndVolatility);

			if (AnimTimerHandle.IsValid())
			{
				UnRegisterActiveTimer(AnimTimerHandle.Pin().ToSharedRef());
			}
			AnimTimerHandle = RegisterActiveTimer(0.016f, FWidgetActiveTimerDelegate::CreateSP(
				this, &SFlipbookThumbnail::OnAnimTick));
		}
	}

	virtual void OnMouseLeave(const FPointerEvent& MouseEvent) override
	{
		SLeafWidget::OnMouseLeave(MouseEvent);
		if (AnimTimerHandle.IsValid())
		{
			UnRegisterActiveTimer(AnimTimerHandle.Pin().ToSharedRef());
		}
		AnimTimerHandle.Reset();
		const int32 LiveNumFrames = ReconcileFrameTopology();
		CurrentFrame = (InitialFrameIndex != INDEX_NONE && InitialFrameIndex < LiveNumFrames)
			? InitialFrameIndex
			: 0;
		TickAccumulator = 0.0;
		FrameRunAccumulator = 0;
		if (LiveNumFrames > 0 && !SetBrushFromFrame(CurrentFrame))
		{
			SetBrushFromBestAvailableFrame();
		}
		Invalidate(EInvalidateWidgetReason::Paint);
	}

#if WITH_DEV_AUTOMATION_TESTS
	int32 GetLiveFrameCountForTests() const
	{
		return Flipbook.IsValid() ? Flipbook->GetNumKeyFrames() : 0;
	}

	int32 GetCurrentFrameIndexForTests() const { return CurrentFrame; }
	int32 GetInitialFrameIndexForTests() const { return InitialFrameIndex; }
	bool HasTextureForTests() const { return bHasTexture; }
	const FSlateBrush& GetBrushForTests() const { return Brush; }

	void SetCurrentFrameIndexForTests(int32 FrameIndex)
	{
		CurrentFrame = FrameIndex;
	}

	bool RefreshFromLiveFlipbookForTests()
	{
		const int32 LiveNumFrames = ReconcileFrameTopology();
		if (LiveNumFrames <= 0)
		{
			return false;
		}
		return SetBrushFromFrame(CurrentFrame) || SetBrushFromBestAvailableFrame();
	}

	bool AdvanceAnimationForTests(float DeltaTime)
	{
		return OnAnimTick(0.0, DeltaTime) == EActiveTimerReturnType::Continue;
	}
#endif

private:
	int32 ReconcileFrameTopology()
	{
		const int32 LiveNumFrames = Flipbook.IsValid() ? Flipbook->GetNumKeyFrames() : 0;
		if (LiveNumFrames <= 0)
		{
			InitialFrameIndex = INDEX_NONE;
			CurrentFrame = 0;
			bHasTexture = false;
			Brush = FSlateBrush();
			StatusText = Flipbook.IsValid()
				? FText::FromString(TEXT("No Frames"))
				: FText::FromString(TEXT("No FB"));
			return LiveNumFrames;
		}

		if (InitialFrameIndex < 0 || InitialFrameIndex >= LiveNumFrames)
		{
			InitialFrameIndex = FindFirstRenderableFrameIndex(LiveNumFrames);
		}
		if (CurrentFrame < 0 || CurrentFrame >= LiveNumFrames)
		{
			CurrentFrame = (InitialFrameIndex != INDEX_NONE) ? InitialFrameIndex : 0;
			FrameRunAccumulator = 0;
		}
		return LiveNumFrames;
	}

	int32 FindFirstRenderableFrameIndex(int32 LiveNumFrames) const
	{
		if (!Flipbook.IsValid() || LiveNumFrames <= 0)
		{
			return INDEX_NONE;
		}

		for (int32 FrameIndex = 0; FrameIndex < LiveNumFrames; ++FrameIndex)
		{
			const FPaperFlipbookKeyFrame& KeyFrame = Flipbook->GetKeyFrameChecked(FrameIndex);
			FSpriteSlateRegion Region;
			if (FSpriteSlateAtlasUtils::ResolveSprite(KeyFrame.Sprite, Region))
			{
				return FrameIndex;
			}
		}

		return INDEX_NONE;
	}

	bool SetBrushFromFrame(int32 FrameIndex)
	{
		bHasTexture = false;
		const int32 LiveNumFrames = ReconcileFrameTopology();
		if (!Flipbook.IsValid() || LiveNumFrames <= 0)
		{
			return false;
		}
		if (FrameIndex < 0 || FrameIndex >= LiveNumFrames)
		{
			StatusText = FText::FromString(TEXT("No Frame"));
			return false;
		}

		UPaperSprite* Sprite = Flipbook->GetKeyFrameChecked(FrameIndex).Sprite;
		if (!FSpriteSlateAtlasUtils::ConfigureBrush(Sprite, Brush))
		{
			StatusText = FText::FromString(TEXT("No Sprite"));
			return false;
		}
		bHasTexture = true;
		StatusText = FText::GetEmpty();
		return true;
	}

	bool SetBrushFromBestAvailableFrame()
	{
		const int32 LiveNumFrames = ReconcileFrameTopology();
		if (!Flipbook.IsValid() || LiveNumFrames <= 0)
		{
			return false;
		}

		for (int32 FrameIndex = 0; FrameIndex < LiveNumFrames; ++FrameIndex)
		{
			if (SetBrushFromFrame(FrameIndex))
			{
				InitialFrameIndex = FrameIndex;
				CurrentFrame = FrameIndex;
				return true;
			}
		}

		StatusText = FText::FromString(TEXT("No Sprite"));
		return false;
	}

	bool HasRenderableResource() const
	{
		const UTexture* Texture = Cast<UTexture>(Brush.GetResourceObject());
		return Texture && Texture->GetResource() != nullptr;
	}

	EActiveTimerReturnType OnInitialResolveTick(double CurrentTime, float DeltaTime)
	{
		if (!Flipbook.IsValid())
		{
			InitialResolveTimerHandle.Reset();
			return EActiveTimerReturnType::Stop;
		}
		if (ReconcileFrameTopology() <= 0)
		{
			InitialResolveTimerHandle.Reset();
			return EActiveTimerReturnType::Stop;
		}

		++InitialResolveAttempts;
		bool bResolvedBrush = SetBrushFromFrame((InitialFrameIndex != INDEX_NONE) ? InitialFrameIndex : 0);
		if (!bResolvedBrush)
		{
			bResolvedBrush = SetBrushFromBestAvailableFrame();
		}
		bHasTexture = bResolvedBrush;

		// Force a prepass/paint so the card updates even before any hover event.
		Invalidate(EInvalidateWidgetReason::Paint);

		const bool bResourceReady = bHasTexture && HasRenderableResource();
		if (bResourceReady && InitialResolveAttempts >= 2)
		{
			StatusText = FText::GetEmpty();
			InitialResolveTimerHandle.Reset();
			return EActiveTimerReturnType::Stop;
		}

		StatusText = bHasTexture ? FText::FromString(TEXT("Loading")) : FText::FromString(TEXT("No Sprite"));

		if (InitialResolveAttempts >= 5)
		{
			InitialResolveTimerHandle.Reset();
			return EActiveTimerReturnType::Stop;
		}

		return EActiveTimerReturnType::Continue;
	}

	EActiveTimerReturnType OnAnimTick(double CurrentTime, float DeltaTime)
	{
		const int32 LiveNumFrames = ReconcileFrameTopology();
		if (!Flipbook.IsValid() || LiveNumFrames <= 1)
		{
			AnimTimerHandle.Reset();
			if (LiveNumFrames == 1)
			{
				SetBrushFromFrame(0);
				Invalidate(EInvalidateWidgetReason::Paint);
			}
			return EActiveTimerReturnType::Stop;
		}

		float FPS = Flipbook->GetFramesPerSecond();
		if (FPS <= 0.0f) FPS = 15.0f;

		TickAccumulator += DeltaTime;
		float SecondsPerTick = 1.0f / FPS;

		while (TickAccumulator >= SecondsPerTick)
		{
			TickAccumulator -= SecondsPerTick;
			FrameRunAccumulator++;

			int32 FrameRun = FMath::Max(Flipbook->GetKeyFrameChecked(CurrentFrame).FrameRun, 1);
			if (FrameRunAccumulator >= FrameRun)
			{
				FrameRunAccumulator = 0;
				CurrentFrame = (CurrentFrame + 1) % LiveNumFrames;
				if (!SetBrushFromFrame(CurrentFrame))
				{
					// Keep thumbnail visible even if a specific frame has no texture.
					SetBrushFromBestAvailableFrame();
				}
				Invalidate(EInvalidateWidgetReason::Paint);
			}
		}

		return EActiveTimerReturnType::Continue;
	}

	TStrongObjectPtr<UPaperFlipbook> Flipbook;
	FSlateBrush Brush;
	bool bHasTexture = false;
	int32 InitialFrameIndex = INDEX_NONE;
	int32 InitialResolveAttempts = 0;
	int32 CurrentFrame = 0;
	double TickAccumulator = 0.0;
	int32 FrameRunAccumulator = 0;
	TWeakPtr<FActiveTimerHandle> AnimTimerHandle;
	TWeakPtr<FActiveTimerHandle> InitialResolveTimerHandle;
	FText StatusText;
};
