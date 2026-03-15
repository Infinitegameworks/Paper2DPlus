// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UTexture2D;
class UPaperSprite;
class UPaperFlipbook;
class UPaper2DPlusCharacterProfileAsset;

/** Parsed frame data from Aseprite file */
struct FAsepriteFrame
{
	int32 Duration = 100; // ms
	TArray<FColor> Pixels; // RGBA
	int32 Width = 0;
	int32 Height = 0;
};

/** Parsed tag (animation group) from Aseprite file */
struct FAsepriteTag
{
	FString Name;
	int32 FromFrame = 0;
	int32 ToFrame = 0;
	uint8 LoopDirection = 0; // 0=Forward, 1=Reverse, 2=PingPong
};

/** Parsed layer info */
struct FAsepriteLayer
{
	FString Name;
	bool bVisible = true;
	float Opacity = 1.0f;
	int32 LayerType = 0; // 0=Normal, 1=Group
};

/** Complete parsed Aseprite file data */
struct FAsepriteParsedData
{
	int32 Width = 0;
	int32 Height = 0;
	int32 ColorDepth = 32;
	TArray<FAsepriteFrame> Frames;
	TArray<FAsepriteTag> Tags;
	TArray<FAsepriteLayer> Layers;
	TArray<FColor> Palette;
	bool bIsValid = false;
};

/** Result of an Aseprite import operation */
struct FAsepriteImportResult
{
	bool bSuccess = false;
	FString ErrorMessage;
	UTexture2D* SpriteSheet = nullptr;
	TArray<UPaperSprite*> Sprites;
	TArray<UPaperFlipbook*> Flipbooks;
};

/**
 * Handles importing Aseprite (.ase/.aseprite) files.
 * Parses the binary format and creates Paper2D assets.
 */
class PAPER2DPLUSEDITOR_API FAsepriteImporter
{
public:
	/**
	 * Parse an Aseprite file from disk.
	 * @param FilePath Path to the .ase/.aseprite file
	 * @param OutData Parsed file data
	 * @param OutError Error message if parsing fails
	 * @return True if parsing succeeded
	 */
	static bool ParseFile(const FString& FilePath, FAsepriteParsedData& OutData, FString& OutError);

	/**
	 * Parse Aseprite data from a byte buffer.
	 */
	static bool ParseBuffer(const TArray<uint8>& Buffer, FAsepriteParsedData& OutData, FString& OutError);

	/**
	 * Import an Aseprite file, creating sprites and flipbooks.
	 * @param FilePath Path to the .ase/.aseprite file
	 * @param OutputPath Content browser output path
	 * @param AssetPrefix Prefix for created asset names
	 * @return Import result with created assets
	 */
	static FAsepriteImportResult ImportFile(
		const FString& FilePath,
		const FString& OutputPath,
		const FString& AssetPrefix
	);

	/**
	 * Show an import dialog for Aseprite files.
	 */
	static void ShowImportDialog();

	/**
	 * Register context menu extensions for Aseprite files.
	 */
	static void RegisterMenus();
	static void UnregisterMenus();

private:
	/** Create a texture from flattened frame pixel data */
	static UTexture2D* CreateSpriteSheetTexture(
		const FAsepriteParsedData& Data,
		const FString& OutputPath,
		const FString& AssetName
	);

	/** Create sprites from the sprite sheet */
	static TArray<UPaperSprite*> CreateSprites(
		UTexture2D* SpriteSheet,
		const FAsepriteParsedData& Data,
		const FString& OutputPath,
		const FString& AssetPrefix
	);

	/** Create flipbooks from tags */
	static TArray<UPaperFlipbook*> CreateFlipbooks(
		const TArray<UPaperSprite*>& Sprites,
		const FAsepriteParsedData& Data,
		const FString& OutputPath,
		const FString& AssetPrefix
	);

	/** Flatten a cel onto a frame buffer with opacity */
	static void FlattenCel(
		TArray<FColor>& FrameBuffer,
		int32 FrameWidth, int32 FrameHeight,
		const TArray<FColor>& CelPixels,
		int32 CelWidth, int32 CelHeight,
		int32 CelX, int32 CelY,
		float Opacity
	);

	/** Decompress zlib data. ExpectedSize is the known uncompressed size (0 to auto-estimate). */
	static bool DecompressZlib(const uint8* CompressedData, int32 CompressedSize, TArray<uint8>& OutData, int32 ExpectedSize = 0);

	/** Convert indexed color pixels to RGBA using palette */
	static TArray<FColor> ConvertIndexedToRGBA(const uint8* IndexData, int32 PixelCount, const TArray<FColor>& Palette);
};
