// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UTexture2D;
class UPaperSprite;

/**
 * Shared utilities for sprite extraction operations.
 * Used by SSpriteExtractorWindow for initial sprite extraction.
 */
class PAPER2DPLUSEDITOR_API FSpriteExtractionUtils
{
public:
	/** Check if texture needs Paper2D pixel-art settings (nearest filter, no compression, no mips) */
	static bool NeedsPaper2DSettings(const UTexture2D* Texture);

	/** Apply Paper2D pixel-art settings to texture. Calls Texture->Modify() for editor undo support. */
	static void ApplyPaper2DSettings(UTexture2D* Texture);

	/** Force CPU access on texture by rebuilding platform data. Returns false if source data is missing. */
	static bool ForceCPUAccess(UTexture2D* Texture);

	/** Load texture pixel data. Supports PF_B8G8R8A8 and PF_R8G8B8A8. */
	static bool LoadTextureData(
		UTexture2D* Texture,
		TArray<FColor>& OutPixels,
		int32& OutWidth,
		int32& OutHeight);

	/** Create a PaperSprite asset from texture bounds. Returns nullptr on failure. */
	static UPaperSprite* CreateSpriteFromBounds(
		UTexture2D* SourceTexture,
		const FIntRect& Bounds,
		const FString& SpriteName,
		const FString& OutputPath);
};
