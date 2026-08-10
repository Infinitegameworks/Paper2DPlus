// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SLeafWidget.h"

class UPaper2DPlusCharacterProfileAsset;
class UTexture2D;
struct FAsepriteParsedData;
struct FAsepriteLayerNode;

/** Typedef to avoid comma-in-macro issues with SLATE_ARGUMENT. */
using FPerLayerBufferMap = TMap<int32, TArray<TArray<FColor>>>;

enum class EAsepriteImportMode : uint8
{
	LayerAssetNewProfile,
	LayerAssetExistingProfile,
	SeparateAssetsPerLayer,
};

/** All user selections from the Aseprite layer import dialog. */
struct FAsepriteLayerImportSettings
{
	/** Which layers are checked for import (key = layer index in FAsepriteParsedData::Layers). */
	TMap<int32, bool> LayerImportEnabled;

	/** Per-layer global-order overrides (key = layer index). */
	TMap<int32, int32> LayerOrder;

	EAsepriteImportMode ImportMode = EAsepriteImportMode::LayerAssetNewProfile;

	/** Selected profile when using existing. */
	TSoftObjectPtr<UPaper2DPlusCharacterProfileAsset> ExistingProfile;

	/** Content browser output path (e.g., "/Game/Sprites"). */
	FString OutputPath;

	/** Prefix for asset names (e.g., the filename without extension). */
	FString AssetPrefix;

	/** True if Import was clicked, false if Cancel. */
	bool bUserConfirmed = false;

	/** Original .ase/.aseprite file path on disk (populated by the factory). */
	FString SourceFilePath;
};

/**
 * SLeafWidget that composites per-layer pixel buffers into a single preview image.
 * Used inside the Aseprite layer import dialog.
 */
class SLayerImportPreviewCanvas : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SLayerImportPreviewCanvas) {}
		SLATE_ARGUMENT(const FAsepriteParsedData*, ParsedData)
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

	/** Toggle a layer's visibility in the preview (independent of import checkbox). */
	void SetLayerVisibility(int32 LayerIndex, bool bVisible);
	bool IsLayerVisible(int32 LayerIndex) const;

	int32 GetFrameCount() const;

private:
	void RecompositeFrame() const;

	const FAsepriteParsedData* ParsedData = nullptr;
	const FPerLayerBufferMap* PerLayerBuffers = nullptr;

	TMap<int32, bool> LayerVisibility;
	int32 FrameIndex = 0;

	/** Persistent texture for rendering — updated in-place on frame/visibility changes. */
	mutable TStrongObjectPtr<UTexture2D> PreviewTexture;
	mutable FSlateBrush PreviewBrush;
	mutable bool bNeedsRecomposite = true;
};

/**
 * Modal dialog for configuring Aseprite layer import.
 * Displays the parsed layer structure, composited preview,
 * profile link controls, and import configuration.
 */
class SAsepiteLayerImportDialog : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SAsepiteLayerImportDialog) {}
		SLATE_ARGUMENT(const FAsepriteParsedData*, ParsedData)
		SLATE_ARGUMENT(const FPerLayerBufferMap*, PerLayerBuffers)
		SLATE_ARGUMENT(TSharedPtr<SWindow>, ParentWindow)
		SLATE_ARGUMENT(FString, DefaultOutputPath)
		SLATE_ARGUMENT(FString, DefaultAssetPrefix)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Get the user's import settings after the dialog closes. */
	const FAsepriteLayerImportSettings& GetImportSettings() const { return ImportSettings; }

private:
	void CloseDialog(bool bConfirmed);
	void RebuildLayerList();
	void UpdateImportButtonState();

	/** Count how many visual layers are checked for import. */
	int32 CountCheckedVisualLayers() const;

	const FAsepriteParsedData* ParsedData = nullptr;
	const FPerLayerBufferMap* PerLayerBuffers = nullptr;
	TWeakPtr<SWindow> ParentWindow;

	FAsepriteLayerImportSettings ImportSettings;

	/** Profile mode combo items. */
	TArray<TSharedPtr<FString>> ProfileModeOptions;
	TSharedPtr<FString> SelectedProfileMode;

	/** Collapsed group state (key = layer index of group layer). */
	TMap<int32, bool> GroupCollapsed;

	TSharedPtr<SLayerImportPreviewCanvas> PreviewCanvas;
	TSharedPtr<class SVerticalBox> LayerListBox;
	TSharedPtr<class STextBlock> FrameCounterText;
	TSharedPtr<class SButton> ImportButton;
	TSharedPtr<class STextBlock> ImportButtonText;
	TSharedPtr<SWidget> ProfilePickerBox;
};
