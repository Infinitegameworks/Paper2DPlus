// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "AsepriteImporter.h"
#include "AsepriteLayerImportDialog.h"
#include "HitboxConflictDialog.h"
#include "Paper2DPlusCharacterLayerAsset.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Paper2DPlusSettings.h" // TASK-72: normal-map pairing convention + sprite-lit material slot
#include "SpriteExtractionUtils.h"
#include "Paper2DPlusEditorCompat.h" // PAPER2DPLUS_RENAME_TO_TRANSIENT_FLAGS (UE5.8 REN_ForceNoResetLoaders deprecation)
#include "Materials/MaterialInterface.h" // TASK-72: SpriteLitMaterial resolution
#include "CoreGlobals.h" // GIsAutomationTesting
#include "Misc/App.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "ToolMenus.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Misc/PackageName.h" // FPackageName::IsTempPackage (WS3-3D FIX 4)
#include "TextureWatcherService.h" // TASK-71: refresh the live-reimport watcher map after a fresh import
#include "Engine/Texture2D.h"
#include "TextureCompiler.h"
#include "PaperSprite.h"
#include "PaperFlipbook.h"
#include "ObjectTools.h"
#include "PackageTools.h"
#include "DesktopPlatformModule.h"
#include "EditorDirectories.h"
#include "Misc/FileHelper.h"
#include "Misc/ScopedSlowTask.h"
#include "Misc/Compression.h"
// UE 5.0 compat: FAppStyle/AppStyle.h doesn't exist, use FEditorStyle
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
#include "EditorStyleSet.h"
#ifndef FAppStyle
#define FAppStyle FEditorStyle
#define GetAppStyleSetName GetStyleSetName
#endif
#else
#include "Styling/AppStyle.h"
#endif

/** FAsepriteImporter — Aseprite JSON import: parse sprite sheet data, create flipbooks from tags, and generate frame events from layers. */

#define LOCTEXT_NAMESPACE "AsepriteImporter"

// Aseprite magic numbers
static constexpr uint16 ASE_FILE_MAGIC = 0xA5E0;
static constexpr uint16 ASE_FRAME_MAGIC = 0xF1FA;

// Chunk types
static constexpr uint16 ASE_CHUNK_LAYER = 0x2004;
static constexpr uint16 ASE_CHUNK_CEL = 0x2005;
static constexpr uint16 ASE_CHUNK_COLOR_PROFILE = 0x2007;
static constexpr uint16 ASE_CHUNK_TAGS = 0x2018;
static constexpr uint16 ASE_CHUNK_PALETTE = 0x2019;
static constexpr uint16 ASE_CHUNK_USER_DATA = 0x2020;

// Cel types
static constexpr uint16 ASE_CEL_RAW = 0;
static constexpr uint16 ASE_CEL_LINKED = 1;
static constexpr uint16 ASE_CEL_COMPRESSED = 2;

namespace
{
	template <typename TObjectType>
	TObjectType* FindOrCreateAssetInPackage(UPackage* Package, const FString& AssetName, bool& bOutCreated)
	{
		bOutCreated = false;
		if (!Package)
		{
			return nullptr;
		}

		if (TObjectType* ExistingTyped = FindObject<TObjectType>(Package, *AssetName))
		{
			return ExistingTyped;
		}

		if (UObject* Existing = StaticFindObject(UObject::StaticClass(), Package, *AssetName))
		{
			Existing->Rename(nullptr, GetTransientPackage(), PAPER2DPLUS_RENAME_TO_TRANSIENT_FLAGS);
		}

		TObjectType* Created = NewObject<TObjectType>(Package, *AssetName, RF_Public | RF_Standalone);
		bOutCreated = (Created != nullptr);
		return Created;
	}
}

/**
 * Helper class for reading binary data from a byte buffer with bounds checking.
 */
class FAsepriteReader
{
public:
	FAsepriteReader(const uint8* InData, int32 InSize)
		: Data(InData), Size(InSize), Pos(0)
	{
	}

	bool IsValid() const { return Data != nullptr && Pos <= Size; }
	int32 GetPos() const { return Pos; }
	int32 GetRemaining() const { return Size - Pos; }

	void SetPos(int32 NewPos)
	{
		Pos = FMath::Clamp(NewPos, 0, Size);
	}

	void Skip(int32 Bytes)
	{
		Pos = FMath::Min(Pos + Bytes, Size);
	}

	bool CanRead(int32 Bytes) const
	{
		// Reject negative counts and compute in int64 so a crafted/corrupt .ase can't pass this gate via
		// a negative or overflowing byte count (audit F7).
		return Bytes >= 0 && (static_cast<int64>(Pos) + Bytes) <= Size;
	}

	uint8 ReadUInt8()
	{
		if (!CanRead(1)) return 0;
		uint8 Value = Data[Pos];
		Pos += 1;
		return Value;
	}

	int16 ReadInt16()
	{
		if (!CanRead(2)) return 0;
		int16 Value;
		FMemory::Memcpy(&Value, Data + Pos, 2);
		Pos += 2;
		return Value;
	}

	uint16 ReadUInt16()
	{
		if (!CanRead(2)) return 0;
		uint16 Value;
		FMemory::Memcpy(&Value, Data + Pos, 2);
		Pos += 2;
		return Value;
	}

	uint32 ReadUInt32()
	{
		if (!CanRead(4)) return 0;
		uint32 Value;
		FMemory::Memcpy(&Value, Data + Pos, 4);
		Pos += 4;
		return Value;
	}

	int32 ReadInt32()
	{
		if (!CanRead(4)) return 0;
		int32 Value;
		FMemory::Memcpy(&Value, Data + Pos, 4);
		Pos += 4;
		return Value;
	}

	FString ReadString()
	{
		uint16 Length = ReadUInt16();
		if (Length == 0 || !CanRead(Length))
		{
			return FString();
		}

		TArray<uint8> Chars;
		Chars.SetNum(Length + 1);
		FMemory::Memcpy(Chars.GetData(), Data + Pos, Length);
		Chars[Length] = 0;
		Pos += Length;

		return FString(UTF8_TO_TCHAR(reinterpret_cast<const char*>(Chars.GetData())));
	}

	const uint8* GetCurrentPtr() const
	{
		if (Pos < Size) return Data + Pos;
		return nullptr;
	}

	bool ReadBytes(uint8* OutBuffer, int32 Count)
	{
		if (!CanRead(Count)) return false;
		FMemory::Memcpy(OutBuffer, Data + Pos, Count);
		Pos += Count;
		return true;
	}

private:
	const uint8* Data;
	int32 Size;
	int32 Pos;
};

// ============================================
// ParseFile
// ============================================

// Hard cap on imported image / sprite-sheet dimensions. Matches the max 2D texture dimension on all
// modern RHIs (D3D11/12, Vulkan, Metal). Rejects crafted/corrupt .ase files that would otherwise
// allocate absurd buffers (uint16 canvas dims alone reach 65535x65535 ≈ 17 GB at 4 bpp) and overflow
// 32-bit width*height byte math (audit U11/U12). Shares one source of truth with the sprite-sheet
// grid packing so the canvas guard and the sheet guard can never drift apart.
static constexpr int32 MaxImportTextureDimension = FSpriteSheetGrid::DefaultMaxDimension;

bool FAsepriteImporter::ParseFile(const FString& FilePath, FAsepriteParsedData& OutData, FString& OutError)
{
	TArray<uint8> FileData;
	if (!FFileHelper::LoadFileToArray(FileData, *FilePath))
	{
		OutError = FString::Printf(TEXT("Failed to read file: %s"), *FilePath);
		return false;
	}

	return ParseBuffer(FileData, OutData, OutError);
}

// ============================================
// ParseBuffer
// ============================================

bool FAsepriteImporter::ParseBuffer(const TArray<uint8>& Buffer, FAsepriteParsedData& OutData, FString& OutError)
{
	if (Buffer.Num() < 128)
	{
		OutError = TEXT("File too small to contain a valid Aseprite header (< 128 bytes).");
		return false;
	}

	FAsepriteReader Reader(Buffer.GetData(), Buffer.Num());

	// ---- Header (128 bytes) ----
	uint32 FileSize = Reader.ReadUInt32();
	uint16 Magic = Reader.ReadUInt16();

	if (Magic != ASE_FILE_MAGIC)
	{
		OutError = FString::Printf(TEXT("Invalid Aseprite magic number: 0x%04X (expected 0x%04X)."), Magic, ASE_FILE_MAGIC);
		return false;
	}

	uint16 FrameCount = Reader.ReadUInt16();
	OutData.Width = Reader.ReadUInt16();
	OutData.Height = Reader.ReadUInt16();
	OutData.ColorDepth = Reader.ReadUInt16();

	if (OutData.Width <= 0 || OutData.Height <= 0)
	{
		OutError = FString::Printf(TEXT("Invalid image dimensions: %d x %d"), OutData.Width, OutData.Height);
		return false;
	}

	// FIX(audit U11): bound untrusted canvas dimensions before any width*height allocation. uint16 dims
	// alone reach 65535x65535, which overflows the 32-bit per-frame pixel count and would OOM on SetNum.
	if (OutData.Width > MaxImportTextureDimension || OutData.Height > MaxImportTextureDimension)
	{
		OutError = FString::Printf(TEXT("Image dimensions %d x %d exceed the maximum supported %d (possible corrupt/crafted file)."),
			OutData.Width, OutData.Height, MaxImportTextureDimension);
		return false;
	}

	if (OutData.ColorDepth != 32 && OutData.ColorDepth != 16 && OutData.ColorDepth != 8)
	{
		OutError = FString::Printf(TEXT("Unsupported color depth: %d (expected 8, 16, or 32)"), OutData.ColorDepth);
		return false;
	}

	uint32 Flags = Reader.ReadUInt32();
	uint16 DeprecatedSpeed = Reader.ReadUInt16();

	// Skip to byte 128 (past reserved header data)
	Reader.SetPos(128);

	// Storage for per-frame cel data used for compositing
	TArray<TArray<FAsepriteCelData>> AllFrameCels;
	AllFrameCels.SetNum(FrameCount);

	// The layer index a following user-data chunk (0x2020) decorates. Aseprite attaches user data to the last
	// read object, so this is set by each LAYER chunk and cleared by any other (non-user-data) chunk (TASK-62).

	// ---- Frames ----
	for (int32 FrameIdx = 0; FrameIdx < FrameCount; FrameIdx++)
	{
		if (!Reader.CanRead(16))
		{
			OutError = FString::Printf(TEXT("Unexpected end of data at frame %d."), FrameIdx);
			return false;
		}

		int32 FrameStartPos = Reader.GetPos();

		uint32 FrameSize = Reader.ReadUInt32();
		uint16 FrameMagic = Reader.ReadUInt16();

		if (FrameMagic != ASE_FRAME_MAGIC)
		{
			OutError = FString::Printf(TEXT("Invalid frame magic at frame %d: 0x%04X (expected 0x%04X)."), FrameIdx, FrameMagic, ASE_FRAME_MAGIC);
			return false;
		}

		uint16 OldChunkCount = Reader.ReadUInt16();
		uint16 FrameDuration = Reader.ReadUInt16();
		Reader.Skip(2); // reserved
		uint32 NewChunkCount = Reader.ReadUInt32();

		uint32 ChunkCount = (NewChunkCount != 0) ? NewChunkCount : static_cast<uint32>(OldChunkCount);

		// Store frame duration
		FAsepriteFrame Frame;
		Frame.Duration = (FrameDuration > 0) ? FrameDuration : DeprecatedSpeed;
		Frame.Width = OutData.Width;
		Frame.Height = OutData.Height;
		// Pixels will be composited after all chunks are parsed
		OutData.Frames.Add(Frame);

		// ---- Chunks ----
		for (uint32 ChunkIdx = 0; ChunkIdx < ChunkCount; ChunkIdx++)
		{
			if (!Reader.CanRead(6))
			{
				OutError = FString::Printf(TEXT("Unexpected end of data at chunk %d of frame %d."), ChunkIdx, FrameIdx);
				return false;
			}

			int32 ChunkStartPos = Reader.GetPos();
			uint32 ChunkSize = Reader.ReadUInt32();
			uint16 ChunkType = Reader.ReadUInt16();

			// How many bytes of chunk data remain after the 6-byte header (size + type)
			int32 ChunkDataSize = static_cast<int32>(ChunkSize) - 6;
			if (ChunkDataSize < 0)
			{
				OutError = FString::Printf(TEXT("Invalid chunk size %u at chunk %d of frame %d."), ChunkSize, ChunkIdx, FrameIdx);
				return false;
			}

			switch (ChunkType)
			{
			// ---- Layer chunk ----
			case ASE_CHUNK_LAYER:
			{
				uint16 LayerFlags = Reader.ReadUInt16();
				uint16 LayerType = Reader.ReadUInt16();
				uint16 ChildLevel = Reader.ReadUInt16();
				Reader.Skip(2); // default width (ignored)
				Reader.Skip(2); // default height (ignored)
				uint16 BlendMode = Reader.ReadUInt16();
				uint8 LayerOpacity = Reader.ReadUInt8();
				Reader.Skip(3); // reserved

				FString LayerName = Reader.ReadString();

				FAsepriteLayer Layer;
				Layer.Name = LayerName;
				Layer.bVisible = (LayerFlags & 1) != 0;
				Layer.Opacity = LayerOpacity / 255.0f;
				Layer.LayerType = LayerType;
				Layer.ChildLevel = ChildLevel;
				OutData.Layers.Add(Layer);
				break;
			}

			// ---- Cel chunk ----
			case ASE_CHUNK_CEL:
			{
				FAsepriteCelData Cel;
				Cel.LayerIndex = Reader.ReadUInt16();
				Cel.X = Reader.ReadInt16();
				Cel.Y = Reader.ReadInt16();
				Cel.Opacity = Reader.ReadUInt8();
				Cel.CelType = Reader.ReadUInt16();
				// Skip z-index (recent format addition) and reserved bytes
				Reader.Skip(7);

				if (Cel.CelType == ASE_CEL_RAW)
				{
					Cel.CelWidth = Reader.ReadUInt16();
					Cel.CelHeight = Reader.ReadUInt16();

					// Bound untrusted per-cel dims like the canvas (audit F7): uint16 dims reach 65535 each, so
					// CelWidth*CelHeight can overflow int32 and OOM on SetNum. Cap at MaxImportTextureDimension
					// (16384) so PixelCount and PixelCount*4 stay within int32; compute in int64 defensively.
					const int64 PixelCount64 = static_cast<int64>(Cel.CelWidth) * Cel.CelHeight;
					if (Cel.CelWidth > MaxImportTextureDimension || Cel.CelHeight > MaxImportTextureDimension)
					{
						OutError = FString::Printf(TEXT("Cel dimensions %d x %d exceed the maximum supported %d (possible corrupt/crafted file)."),
							Cel.CelWidth, Cel.CelHeight, MaxImportTextureDimension);
						return false;
					}
					int32 PixelCount = static_cast<int32>(PixelCount64);
					if (PixelCount <= 0)
					{
						// Empty cel - skip
						break;
					}

					if (OutData.ColorDepth == 32)
					{
						int32 ByteCount = PixelCount * 4;
						if (!Reader.CanRead(ByteCount))
						{
							OutError = FString::Printf(TEXT("Not enough data for raw cel pixels at frame %d."), FrameIdx);
							return false;
						}
						Cel.Pixels.SetNum(PixelCount);
						for (int32 i = 0; i < PixelCount; i++)
						{
							uint8 R = Reader.ReadUInt8();
							uint8 G = Reader.ReadUInt8();
							uint8 B = Reader.ReadUInt8();
							uint8 A = Reader.ReadUInt8();
							Cel.Pixels[i] = FColor(R, G, B, A);
						}
					}
					else if (OutData.ColorDepth == 16)
					{
						int32 ByteCount = PixelCount * 2;
						if (!Reader.CanRead(ByteCount))
						{
							OutError = FString::Printf(TEXT("Not enough data for grayscale cel pixels at frame %d."), FrameIdx);
							return false;
						}
						Cel.Pixels.SetNum(PixelCount);
						for (int32 i = 0; i < PixelCount; i++)
						{
							uint8 V = Reader.ReadUInt8();
							uint8 A = Reader.ReadUInt8();
							Cel.Pixels[i] = FColor(V, V, V, A);
						}
					}
					else if (OutData.ColorDepth == 8)
					{
						if (!Reader.CanRead(PixelCount))
						{
							OutError = FString::Printf(TEXT("Not enough data for indexed cel pixels at frame %d."), FrameIdx);
							return false;
						}
						Cel.IndexedPixels.SetNum(PixelCount);
						Reader.ReadBytes(Cel.IndexedPixels.GetData(), PixelCount);
						Cel.PaletteSnapshot = OutData.Palette;
					}
				}
				else if (Cel.CelType == ASE_CEL_LINKED)
				{
					Cel.LinkedFrame = Reader.ReadUInt16();
				}
				else if (Cel.CelType == ASE_CEL_COMPRESSED)
				{
					Cel.CelWidth = Reader.ReadUInt16();
					Cel.CelHeight = Reader.ReadUInt16();

					// Bound untrusted per-cel dims like the canvas (audit F7): uint16 dims reach 65535 each, so
					// CelWidth*CelHeight can overflow int32 and OOM on SetNum. Cap at MaxImportTextureDimension
					// (16384) so PixelCount and PixelCount*4 stay within int32; compute in int64 defensively.
					const int64 PixelCount64 = static_cast<int64>(Cel.CelWidth) * Cel.CelHeight;
					if (Cel.CelWidth > MaxImportTextureDimension || Cel.CelHeight > MaxImportTextureDimension)
					{
						OutError = FString::Printf(TEXT("Cel dimensions %d x %d exceed the maximum supported %d (possible corrupt/crafted file)."),
							Cel.CelWidth, Cel.CelHeight, MaxImportTextureDimension);
						return false;
					}
					int32 PixelCount = static_cast<int32>(PixelCount64);
					if (PixelCount <= 0)
					{
						break;
					}

					// Remaining bytes in this chunk are the compressed data
					int32 CompressedSize = ChunkStartPos + ChunkSize - Reader.GetPos();
					if (CompressedSize <= 0 || !Reader.CanRead(CompressedSize))
					{
						OutError = FString::Printf(TEXT("Invalid compressed data size at frame %d."), FrameIdx);
						return false;
					}

					const uint8* CompressedPtr = Reader.GetCurrentPtr();

					// Calculate expected decompressed size based on color depth
					int32 BytesPerPixel = (OutData.ColorDepth == 32) ? 4 : (OutData.ColorDepth == 16) ? 2 : 1;
					int32 ExpectedDecompressedSize = PixelCount * BytesPerPixel;

					TArray<uint8> DecompressedData;
					if (!DecompressZlib(CompressedPtr, CompressedSize, DecompressedData, ExpectedDecompressedSize))
					{
						OutError = FString::Printf(TEXT("Failed to decompress cel data at frame %d."), FrameIdx);
						return false;
					}

					Reader.Skip(CompressedSize);

					if (OutData.ColorDepth == 32)
					{
						int32 ExpectedBytes = PixelCount * 4;
						if (DecompressedData.Num() < ExpectedBytes)
						{
							OutError = FString::Printf(TEXT("Decompressed data too small at frame %d: got %d bytes, expected %d."), FrameIdx, DecompressedData.Num(), ExpectedBytes);
							return false;
						}
						Cel.Pixels.SetNum(PixelCount);
						const uint8* Src = DecompressedData.GetData();
						for (int32 i = 0; i < PixelCount; i++)
						{
							Cel.Pixels[i] = FColor(Src[i * 4], Src[i * 4 + 1], Src[i * 4 + 2], Src[i * 4 + 3]);
						}
					}
					else if (OutData.ColorDepth == 16)
					{
						int32 ExpectedBytes = PixelCount * 2;
						if (DecompressedData.Num() < ExpectedBytes)
						{
							OutError = FString::Printf(TEXT("Decompressed data too small at frame %d."), FrameIdx);
							return false;
						}
						Cel.Pixels.SetNum(PixelCount);
						const uint8* Src = DecompressedData.GetData();
						for (int32 i = 0; i < PixelCount; i++)
						{
							uint8 V = Src[i * 2];
							uint8 A = Src[i * 2 + 1];
							Cel.Pixels[i] = FColor(V, V, V, A);
						}
					}
					else if (OutData.ColorDepth == 8)
					{
						if (DecompressedData.Num() < PixelCount)
						{
							OutError = FString::Printf(TEXT("Decompressed indexed data too small at frame %d."), FrameIdx);
							return false;
						}
						Cel.IndexedPixels = MoveTemp(DecompressedData);
						Cel.IndexedPixels.SetNum(PixelCount);
						Cel.PaletteSnapshot = OutData.Palette;
					}
				}

				AllFrameCels[FrameIdx].Add(MoveTemp(Cel));
				break;
			}

			// ---- Tags chunk ----
			case ASE_CHUNK_TAGS:
			{
				uint16 TagCount = Reader.ReadUInt16();
				Reader.Skip(8); // reserved

				for (int32 TagIdx = 0; TagIdx < TagCount; TagIdx++)
				{
					FAsepriteTag Tag;
					Tag.FromFrame = Reader.ReadUInt16();
					Tag.ToFrame = Reader.ReadUInt16();
					Tag.LoopDirection = Reader.ReadUInt8();
					Reader.Skip(8); // repeat count + reserved
					Reader.Skip(3); // deprecated tag color RGB
					Reader.Skip(1); // extra byte
					Tag.Name = Reader.ReadString();

					OutData.Tags.Add(Tag);
				}
				break;
			}

			// ---- Palette chunk ----
			case ASE_CHUNK_PALETTE:
			{
				uint32 PaletteSize = Reader.ReadUInt32();
				uint32 FirstColor = Reader.ReadUInt32();
				uint32 LastColor = Reader.ReadUInt32();
				Reader.Skip(8); // reserved

				// FIX(audit U10): bound the palette allocation against an untrusted size before SetNum.
				// Aseprite's new palette chunk (0x2019) may legitimately declare more than 256 entries
				// (e.g. swatch palettes on RGB/grayscale sprites), so rejecting the whole file is wrong —
				// indexed pixels only ever reference 0-255, which is all we decode. Store at most 256
				// entries, which both accepts those valid files and caps the allocation so a crafted
				// multi-billion size can't OOM.
				const uint32 MaxPaletteEntries = 256;
				const int32 EntriesToStore = static_cast<int32>(FMath::Min(PaletteSize, MaxPaletteEntries));
				if (OutData.Palette.Num() < EntriesToStore)
				{
					OutData.Palette.SetNum(EntriesToStore);
				}

				if (FirstColor > LastColor)
				{
					break; // Malformed palette chunk
				}

				for (uint32 ColorIdx = FirstColor; ColorIdx <= LastColor; ColorIdx++)
				{
					uint16 HasName = Reader.ReadUInt16();
					uint8 R = Reader.ReadUInt8();
					uint8 G = Reader.ReadUInt8();
					uint8 B = Reader.ReadUInt8();
					uint8 A = Reader.ReadUInt8();

					// Guard the write against the REAL container size, not the untrusted PaletteSize/
					// LastColor (F6): entries beyond the stored 256 are read-and-skipped, never written
					// out of bounds. The chunk-end SetPos below realigns the reader regardless.
					if (ColorIdx < static_cast<uint32>(OutData.Palette.Num()))
					{
						OutData.Palette[ColorIdx] = FColor(R, G, B, A);
					}

					if (HasName)
					{
						Reader.ReadString(); // color name - we discard it
					}
				}
				break;
			}

			// ---- User data chunk: ignored; generic Layer import never infers appearance semantics ----
			case ASE_CHUNK_USER_DATA:
				break;

			// ---- Color profile chunk (skip) ----
			case ASE_CHUNK_COLOR_PROFILE:
			default:
				break;
			}

			// Track the layer a following user-data chunk decorates: a LAYER chunk sets it; any other chunk
			// (except the user-data chunk itself, which consumes it) clears it.

			// Advance to end of chunk regardless of what was read
			Reader.SetPos(ChunkStartPos + ChunkSize);
		}

		// Advance to end of frame regardless of what was read
		Reader.SetPos(FrameStartPos + FrameSize);
	}

	// Persist cel data on the parsed data struct for per-layer compositing and preview
	OutData.AllFrameCels = MoveTemp(AllFrameCels);

	// Convert indexed cels after parsing all chunks, using the palette snapshot captured when each cel was read.
	if (OutData.ColorDepth == 8)
	{
		for (TArray<FAsepriteCelData>& FrameCels : OutData.AllFrameCels)
		{
			for (FAsepriteCelData& Cel : FrameCels)
			{
				if (Cel.CelType != ASE_CEL_LINKED && Cel.IndexedPixels.Num() > 0)
				{
					const TArray<FColor>& PaletteForCel = Cel.PaletteSnapshot.Num() > 0 ? Cel.PaletteSnapshot : OutData.Palette;
					Cel.Pixels = ConvertIndexedToRGBA(Cel.IndexedPixels.GetData(), Cel.IndexedPixels.Num(), PaletteForCel);
					Cel.IndexedPixels.Empty();
					Cel.PaletteSnapshot.Empty();
				}
			}
		}
	}

	// Build layer hierarchy tree from ChildLevel values
	BuildLayerHierarchy(OutData);

	// ---- Classify hitbox/socket layers by name convention ----
	for (int32 i = 0; i < OutData.Layers.Num(); i++)
	{
		const FAsepriteLayer& Layer = OutData.Layers[i];
		FString LowerName = Layer.Name.ToLower();

		if (LowerName.StartsWith(TEXT("attack")))
		{
			FAsepriteHitboxLayer HL;
			HL.LayerName = Layer.Name;
			HL.HitboxType = EHitboxType::Attack;
			HL.LayerIndex = i;
			OutData.HitboxLayers.Add(HL);
		}
		else if (LowerName.StartsWith(TEXT("hurtbox")))
		{
			FAsepriteHitboxLayer HL;
			HL.LayerName = Layer.Name;
			HL.HitboxType = EHitboxType::Hurtbox;
			HL.LayerIndex = i;
			OutData.HitboxLayers.Add(HL);
		}
		else if (LowerName.StartsWith(TEXT("socket_")))
		{
			FAsepriteHitboxLayer HL;
			HL.LayerName = Layer.Name;
			HL.bIsSocket = true;
			HL.SocketName = Layer.Name.Mid(7); // Strip "socket_" prefix
			HL.LayerIndex = i;
			OutData.HitboxLayers.Add(HL);
		}
	}

	// ---- Extract hitbox data from classified layers ----
	if (OutData.HitboxLayers.Num() > 0)
	{
		ExtractHitboxData(OutData, OutData.AllFrameCels);

		UE_LOG(LogTemp, Log, TEXT("AsepriteImporter: Classified %d hitbox/socket layers, extracted data for %d frames"),
			OutData.HitboxLayers.Num(), OutData.ExtractedFrameData.Num());
	}

	// ---- Composite frames ----
	// Now flatten all cels for each frame onto frame buffers
	for (int32 FrameIdx = 0; FrameIdx < OutData.Frames.Num(); FrameIdx++)
	{
		FAsepriteFrame& Frame = OutData.Frames[FrameIdx];
		int32 PixelCount = Frame.Width * Frame.Height;
		Frame.Pixels.SetNumZeroed(PixelCount); // Start with transparent black

		const TArray<FAsepriteCelData>& Cels = OutData.AllFrameCels[FrameIdx];

		for (const FAsepriteCelData& Cel : Cels)
		{
			// Check layer visibility
			if (Cel.LayerIndex >= 0 && Cel.LayerIndex < OutData.Layers.Num())
			{
				const FAsepriteLayer& Layer = OutData.Layers[Cel.LayerIndex];
				if (!Layer.bVisible)
				{
					continue;
				}

				// Skip hitbox/socket layers — they are data-only, not visual
				bool bIsHitboxLayer = false;
				for (const FAsepriteHitboxLayer& HL : OutData.HitboxLayers)
				{
					if (HL.LayerIndex == Cel.LayerIndex) { bIsHitboxLayer = true; break; }
				}
				if (bIsHitboxLayer) continue;

				// Handle linked cels
				if (Cel.CelType == ASE_CEL_LINKED)
				{
					int32 LinkedFrameIdx = Cel.LinkedFrame;
					if (LinkedFrameIdx >= 0 && LinkedFrameIdx < AllFrameCels.Num())
					{
						// Find the source cel in the linked frame with the same layer index.
						// Linked cels reuse source pixel content, but placement must come from
						// the current frame cel's X/Y (Cel.X/Cel.Y), not the source cel's X/Y.
						for (const FAsepriteCelData& LinkedCel : AllFrameCels[LinkedFrameIdx])
						{
							if (LinkedCel.LayerIndex == Cel.LayerIndex && LinkedCel.CelType != ASE_CEL_LINKED)
							{
								float CombinedOpacity = (Cel.Opacity / 255.0f) * Layer.Opacity;
								FlattenCel(Frame.Pixels, Frame.Width, Frame.Height,
									LinkedCel.Pixels, LinkedCel.CelWidth, LinkedCel.CelHeight,
									Cel.X, Cel.Y, CombinedOpacity);
								break;
							}
						}
					}
				}
				else if (Cel.Pixels.Num() > 0)
				{
					float CombinedOpacity = (Cel.Opacity / 255.0f) * Layer.Opacity;
					FlattenCel(Frame.Pixels, Frame.Width, Frame.Height,
						Cel.Pixels, Cel.CelWidth, Cel.CelHeight,
						Cel.X, Cel.Y, CombinedOpacity);
				}
			}
			else if (Cel.CelType != ASE_CEL_LINKED && Cel.Pixels.Num() > 0)
			{
				// Layer data not available yet (can happen if layers are defined after cels in early frames)
				// Flatten with full opacity
				FlattenCel(Frame.Pixels, Frame.Width, Frame.Height,
					Cel.Pixels, Cel.CelWidth, Cel.CelHeight,
					Cel.X, Cel.Y, Cel.Opacity / 255.0f);
			}
		}
	}

	OutData.bIsValid = true;

	UE_LOG(LogTemp, Log, TEXT("AsepriteImporter: Parsed %d frames, %d tags, %d layers (%dx%d, %d-bit)"),
		OutData.Frames.Num(), OutData.Tags.Num(), OutData.Layers.Num(),
		OutData.Width, OutData.Height, OutData.ColorDepth);

	return true;
}

// ============================================
// FlattenCel
// ============================================

void FAsepriteImporter::FlattenCel(
	TArray<FColor>& FrameBuffer,
	int32 FrameWidth, int32 FrameHeight,
	const TArray<FColor>& CelPixels,
	int32 CelWidth, int32 CelHeight,
	int32 CelX, int32 CelY,
	float Opacity)
{
	for (int32 Y = 0; Y < CelHeight; Y++)
	{
		int32 DestY = CelY + Y;
		if (DestY < 0 || DestY >= FrameHeight) continue;

		for (int32 X = 0; X < CelWidth; X++)
		{
			int32 DestX = CelX + X;
			if (DestX < 0 || DestX >= FrameWidth) continue;

			int32 SrcIdx = Y * CelWidth + X;
			int32 DstIdx = DestY * FrameWidth + DestX;

			if (SrcIdx >= CelPixels.Num() || DstIdx >= FrameBuffer.Num()) continue;

			const FColor& Src = CelPixels[SrcIdx];
			FColor& Dst = FrameBuffer[DstIdx];

			// Apply opacity
			float SrcAlpha = (Src.A / 255.0f) * Opacity;

			if (SrcAlpha <= 0.0f) continue;

			if (SrcAlpha >= 1.0f && Dst.A == 0)
			{
				// Fast path: fully opaque source onto transparent dest
				Dst = Src;
				Dst.A = FMath::Clamp(static_cast<int32>(SrcAlpha * 255.0f), 0, 255);
			}
			else
			{
				// Alpha compositing (source over)
				float DstAlpha = Dst.A / 255.0f;
				float OutAlpha = SrcAlpha + DstAlpha * (1.0f - SrcAlpha);

				if (OutAlpha > 0.0f)
				{
					float InvOutAlpha = 1.0f / OutAlpha;
					Dst.R = static_cast<uint8>(FMath::Clamp((Src.R * SrcAlpha + Dst.R * DstAlpha * (1.0f - SrcAlpha)) * InvOutAlpha, 0.0f, 255.0f));
					Dst.G = static_cast<uint8>(FMath::Clamp((Src.G * SrcAlpha + Dst.G * DstAlpha * (1.0f - SrcAlpha)) * InvOutAlpha, 0.0f, 255.0f));
					Dst.B = static_cast<uint8>(FMath::Clamp((Src.B * SrcAlpha + Dst.B * DstAlpha * (1.0f - SrcAlpha)) * InvOutAlpha, 0.0f, 255.0f));
					Dst.A = static_cast<uint8>(FMath::Clamp(OutAlpha * 255.0f, 0.0f, 255.0f));
				}
			}
		}
	}
}

// ============================================
// ResolveCel
// ============================================

const FAsepriteCelData* FAsepriteImporter::ResolveCel(
	const TArray<TArray<FAsepriteCelData>>& AllFrameCels,
	int32 FrameIndex,
	int32 LayerIndex,
	const FAsepriteCelData** OutPositionCel)
{
	if (FrameIndex < 0 || FrameIndex >= AllFrameCels.Num())
	{
		return nullptr;
	}

	const FAsepriteCelData* FoundCel = nullptr;
	for (const FAsepriteCelData& Cel : AllFrameCels[FrameIndex])
	{
		if (Cel.LayerIndex == LayerIndex)
		{
			FoundCel = &Cel;
			break;
		}
	}

	if (!FoundCel)
	{
		return nullptr;
	}

	if (OutPositionCel)
	{
		*OutPositionCel = FoundCel;
	}

	if (FoundCel->CelType == ASE_CEL_LINKED)
	{
		int32 LinkedFrameIdx = FoundCel->LinkedFrame;
		if (LinkedFrameIdx < 0 || LinkedFrameIdx >= AllFrameCels.Num())
		{
			return nullptr;
		}
		for (const FAsepriteCelData& LinkedCel : AllFrameCels[LinkedFrameIdx])
		{
			if (LinkedCel.LayerIndex == LayerIndex && LinkedCel.CelType != ASE_CEL_LINKED)
			{
				return &LinkedCel;
			}
		}
		return nullptr;
	}

	return FoundCel;
}

// ============================================
// BuildLayerHierarchy
// ============================================

void FAsepriteImporter::BuildLayerHierarchy(FAsepriteParsedData& Data)
{
	Data.LayerHierarchy.SetNum(Data.Layers.Num());

	// Stack of (hierarchy index, child level) for open parent groups
	TArray<TPair<int32, uint16>> ParentStack;

	for (int32 i = 0; i < Data.Layers.Num(); i++)
	{
		const FAsepriteLayer& Layer = Data.Layers[i];
		FAsepriteLayerNode& Node = Data.LayerHierarchy[i];
		Node.LayerIndex = i;

		// Pop stack until we find the correct parent for this child level
		while (ParentStack.Num() > 0 && ParentStack.Last().Value >= Layer.ChildLevel)
		{
			ParentStack.Pop();
		}

		if (ParentStack.Num() > 0)
		{
			int32 ParentHierIdx = ParentStack.Last().Key;
			Node.ParentIndex = ParentHierIdx;
			Data.LayerHierarchy[ParentHierIdx].ChildIndices.Add(i);

			// Build full path from parent
			Node.FullPath = Data.LayerHierarchy[ParentHierIdx].FullPath / Layer.Name;
		}
		else
		{
			Node.ParentIndex = -1;
			Node.FullPath = Layer.Name;
		}

		// If this is a group layer, push it as a potential parent
		if (Layer.LayerType == 1)
		{
			ParentStack.Add(TPair<int32, uint16>(i, Layer.ChildLevel));
		}
	}

	// Propagate group visibility: hidden groups hide all descendants
	for (int32 i = 0; i < Data.Layers.Num(); i++)
	{
		const FAsepriteLayerNode& Node = Data.LayerHierarchy[i];
		if (Node.ParentIndex >= 0 && !Data.Layers[Node.ParentIndex].bVisible)
		{
			Data.Layers[i].bVisible = false;
		}
	}
}

// ============================================
// DecompressZlib
// ============================================

bool FAsepriteImporter::DecompressZlib(const uint8* CompressedData, int32 CompressedSize, TArray<uint8>& OutData, int32 ExpectedSize)
{
	if (!CompressedData || CompressedSize <= 0)
	{
		return false;
	}

	if (ExpectedSize > 0)
	{
		// Known output size - decompress directly
		OutData.SetNum(ExpectedSize);
		if (FCompression::UncompressMemory(NAME_Zlib, OutData.GetData(), ExpectedSize, CompressedData, CompressedSize))
		{
			return true;
		}
		OutData.Empty();
		return false;
	}

	// Unknown output size - try with increasing buffer sizes
	int32 EstimatedSize = CompressedSize * 4;
	constexpr int32 MaxDecompressedSize = 64 * 1024 * 1024; // 64 MB cap

	for (int32 Attempt = 0; Attempt < 10; Attempt++)
	{
		if (EstimatedSize > MaxDecompressedSize)
		{
			break;
		}

		OutData.SetNum(EstimatedSize);
		if (FCompression::UncompressMemory(NAME_Zlib, OutData.GetData(), EstimatedSize, CompressedData, CompressedSize))
		{
			return true;
		}

		// Double the buffer and try again
		EstimatedSize *= 2;
	}

	OutData.Empty();
	return false;
}

// ============================================
// ConvertIndexedToRGBA
// ============================================

TArray<FColor> FAsepriteImporter::ConvertIndexedToRGBA(const uint8* IndexData, int32 PixelCount, const TArray<FColor>& Palette)
{
	TArray<FColor> Result;
	Result.SetNum(PixelCount);

	for (int32 i = 0; i < PixelCount; i++)
	{
		uint8 Index = IndexData[i];
		if (Index < Palette.Num())
		{
			Result[i] = Palette[Index];
		}
		else
		{
			Result[i] = FColor(255, 0, 255, 255); // Magenta for missing palette entries
		}
	}

	return Result;
}

// ============================================
// ExtractHitboxData
// ============================================

void FAsepriteImporter::ExtractHitboxData(FAsepriteParsedData& Data, const TArray<TArray<FAsepriteCelData>>& AllFrameCels)
{
	const int32 FrameCount = Data.Frames.Num();
	Data.ExtractedFrameData.SetNum(FrameCount);

	for (int32 FrameIdx = 0; FrameIdx < FrameCount; FrameIdx++)
	{
		FAsepriteExtractedFrameData& FrameData = Data.ExtractedFrameData[FrameIdx];
		const TArray<FAsepriteCelData>& Cels = AllFrameCels[FrameIdx];

		for (const FAsepriteHitboxLayer& HL : Data.HitboxLayers)
		{
			// Find the cel for this layer in this frame
			const FAsepriteCelData* FoundCel = nullptr;
			for (const FAsepriteCelData& Cel : Cels)
			{
				if (Cel.LayerIndex == HL.LayerIndex)
				{
					FoundCel = &Cel;
					break;
				}
			}

			if (!FoundCel)
			{
				continue; // No cel on this layer for this frame — no hitbox data
			}

			// Resolve linked cels to get actual pixel data
			const FAsepriteCelData* PixelCel = FoundCel;
			if (FoundCel->CelType == ASE_CEL_LINKED)
			{
				int32 LinkedFrameIdx = FoundCel->LinkedFrame;
				if (LinkedFrameIdx >= 0 && LinkedFrameIdx < AllFrameCels.Num())
				{
					PixelCel = nullptr;
					for (const FAsepriteCelData& LinkedCel : AllFrameCels[LinkedFrameIdx])
					{
						if (LinkedCel.LayerIndex == HL.LayerIndex && LinkedCel.CelType != ASE_CEL_LINKED)
						{
							PixelCel = &LinkedCel;
							break;
						}
					}
				}
				else
				{
					PixelCel = nullptr;
				}
			}

			if (!PixelCel || PixelCel->Pixels.Num() == 0)
			{
				continue;
			}

			// Render this cel's pixels to a frame-sized buffer for connected component detection.
			// Use the cel's position (from the current frame for linked cels, source for direct).
			const int32 CelX = FoundCel->X;
			const int32 CelY = FoundCel->Y;
			const int32 CelW = PixelCel->CelWidth;
			const int32 CelH = PixelCel->CelHeight;
			const int32 FrameW = Data.Width;
			const int32 FrameH = Data.Height;

			// Create a boolean mask: true = non-transparent pixel present
			TArray<bool> Mask;
			Mask.SetNumZeroed(FrameW * FrameH);

			for (int32 Y = 0; Y < CelH; Y++)
			{
				int32 DestY = CelY + Y;
				if (DestY < 0 || DestY >= FrameH) continue;

				for (int32 X = 0; X < CelW; X++)
				{
					int32 DestX = CelX + X;
					if (DestX < 0 || DestX >= FrameW) continue;

					int32 SrcIdx = Y * CelW + X;
					if (SrcIdx >= PixelCel->Pixels.Num()) continue;

					if (PixelCel->Pixels[SrcIdx].A > 0)
					{
						Mask[DestY * FrameW + DestX] = true;
					}
				}
			}

			// Connected component detection via BFS (4-connectivity)
			TArray<bool> Visited;
			Visited.SetNumZeroed(FrameW * FrameH);

			for (int32 StartY = 0; StartY < FrameH; StartY++)
			{
				for (int32 StartX = 0; StartX < FrameW; StartX++)
				{
					int32 StartIdx = StartY * FrameW + StartX;
					if (!Mask[StartIdx] || Visited[StartIdx])
					{
						continue;
					}

					// Found an unvisited non-transparent pixel — flood fill to find the component
					int32 MinX = StartX, MaxX = StartX;
					int32 MinY = StartY, MaxY = StartY;

					TArray<int32> Queue;
					Queue.Add(StartIdx);
					Visited[StartIdx] = true;

					int32 QueueHead = 0;
					while (QueueHead < Queue.Num())
					{
						int32 Idx = Queue[QueueHead++];
						int32 PxY = Idx / FrameW;
						int32 PxX = Idx % FrameW;

						MinX = FMath::Min(MinX, PxX);
						MaxX = FMath::Max(MaxX, PxX);
						MinY = FMath::Min(MinY, PxY);
						MaxY = FMath::Max(MaxY, PxY);

						// 4-connectivity neighbors: up, down, left, right
						const int32 Neighbors[4] = {
							(PxY > 0)          ? Idx - FrameW : -1,   // Up
							(PxY < FrameH - 1) ? Idx + FrameW : -1,   // Down
							(PxX > 0)          ? Idx - 1      : -1,   // Left
							(PxX < FrameW - 1) ? Idx + 1      : -1    // Right
						};

						for (int32 NIdx : Neighbors)
						{
							if (NIdx >= 0 && Mask[NIdx] && !Visited[NIdx])
							{
								Visited[NIdx] = true;
								Queue.Add(NIdx);
							}
						}
					}

					// Component bounding box found
					int32 BoxW = MaxX - MinX + 1;
					int32 BoxH = MaxY - MinY + 1;

					if (HL.bIsSocket)
					{
						// Socket: extract center of bounding box
						FSocketData Socket;
						Socket.Name = HL.SocketName;
						Socket.X = MinX + BoxW / 2;
						Socket.Y = MinY + BoxH / 2;
						FrameData.Sockets.Add(Socket);
					}
					else
					{
						// Hitbox: extract bounding box
						FHitboxData Hitbox;
						Hitbox.Type = HL.HitboxType;
						Hitbox.X = MinX;
						Hitbox.Y = MinY;
						Hitbox.Width = BoxW;
						Hitbox.Height = BoxH;
						FrameData.Hitboxes.Add(Hitbox);
					}
				}
			}
		}
	}
}

// ============================================
// CreateSpriteSheetTexture
// ============================================

UTexture2D* FAsepriteImporter::CreateSpriteSheetTexture(
	const FAsepriteParsedData& Data,
	const FString& OutputPath,
	const FString& AssetName)
{
	if (Data.Frames.Num() == 0) return nullptr;

	int32 FrameCount = Data.Frames.Num();
	int32 FrameW = Data.Width;
	int32 FrameH = Data.Height;

	// Dimension-aware grid packing via the shared helper (so the sheet, CreateSprites' source regions,
	// and the reimporter's regenerated regions all agree byte-for-byte). FIX(audit U12 + Codex #111):
	// computes the sheet in 64-bit and rejects overflow before allocating, AND packs wide sheets into a
	// 2D grid instead of over-rejecting a single overflowing row.
	const FSpriteSheetGrid Grid = FSpriteSheetGrid::Compute(FrameCount, FrameW, FrameH);
	if (!Grid.bValid)
	{
		UE_LOG(LogTemp, Error, TEXT("AsepriteImporter: %d frames of %dx%d cannot fit a sprite sheet within the maximum texture dimension %d; aborting import."),
			FrameCount, FrameW, FrameH, FSpriteSheetGrid::DefaultMaxDimension);
		return nullptr;
	}
	const int32 Columns = Grid.Columns;
	const int32 Rows = Grid.Rows;
	const int32 SheetWidth = Grid.SheetW;
	const int32 SheetHeight = Grid.SheetH;

	// Create package
	FString PackageName = OutputPath / AssetName;
	UPackage* Package = CreatePackage(*PackageName);
	if (!Package) return nullptr;

	// Create or reuse texture safely (avoid fatal name collisions)
	bool bCreatedTexture = false;
	UTexture2D* Texture = FindOrCreateAssetInPackage<UTexture2D>(Package, AssetName, bCreatedTexture);
	if (!Texture) return nullptr;
	// Reimport can reuse an asset whose preceding UpdateResource build is still registered. UE 5.0
	// and 5.8 both require source/platform mutations to wait until that task has fully retired.
	FTextureCompilingManager::Get().FinishCompilation({ Texture });

	// Initialize the texture platform data
	Texture->SetPlatformData(new FTexturePlatformData());
	Texture->GetPlatformData()->SizeX = SheetWidth;
	Texture->GetPlatformData()->SizeY = SheetHeight;
	Texture->GetPlatformData()->PixelFormat = PF_B8G8R8A8;

	// Create mip 0
	FTexture2DMipMap* Mip = new FTexture2DMipMap();
	Texture->GetPlatformData()->Mips.Add(Mip);
	Mip->SizeX = SheetWidth;
	Mip->SizeY = SheetHeight;

	// Allocate and fill pixel data (64-bit byte math; dimensions already capped above — audit U12)
	const int64 TotalPixels = static_cast<int64>(SheetWidth) * SheetHeight;
	Mip->BulkData.Lock(LOCK_READ_WRITE);
	uint8* DestData = static_cast<uint8*>(Mip->BulkData.Realloc(TotalPixels * 4));

	// Clear to transparent black
	FMemory::Memzero(DestData, TotalPixels * 4);

	// Copy each frame into the sprite sheet
	for (int32 FrameIdx = 0; FrameIdx < FrameCount; FrameIdx++)
	{
		const FAsepriteFrame& Frame = Data.Frames[FrameIdx];

		const FIntRect Cell = Grid.GetCellRect(FrameIdx);
		const int32 OffsetX = Cell.Min.X;
		const int32 OffsetY = Cell.Min.Y;

		for (int32 Y = 0; Y < FrameH; Y++)
		{
			for (int32 X = 0; X < FrameW; X++)
			{
				int32 SrcIdx = Y * FrameW + X;
				if (SrcIdx >= Frame.Pixels.Num()) continue;

				int32 DstIdx = ((OffsetY + Y) * SheetWidth + (OffsetX + X)) * 4;

				const FColor& Pixel = Frame.Pixels[SrcIdx];
				// UE4 textures use B8G8R8A8 format
				DestData[DstIdx + 0] = Pixel.B;
				DestData[DstIdx + 1] = Pixel.G;
				DestData[DstIdx + 2] = Pixel.R;
				DestData[DstIdx + 3] = Pixel.A;
			}
		}
	}

	Mip->BulkData.Unlock();

	// Configure texture settings for pixel art
	Texture->MipGenSettings = TMGS_NoMipmaps;
	Texture->CompressionSettings = TC_EditorIcon; // No compression for pixel art
	Texture->Filter = TF_Nearest;
	Texture->NeverStream = true;
	Texture->SRGB = true;
	Texture->LODGroup = TEXTUREGROUP_Pixels2D;

	// Source art for the texture (allows re-import and editor display)
	Texture->Source.Init(SheetWidth, SheetHeight, 1, 1, TSF_BGRA8);
	{
		uint8* SourceData = Texture->Source.LockMip(0);
		// Re-read from the mip data we just wrote
		const uint8* MipData = static_cast<const uint8*>(Mip->BulkData.Lock(LOCK_READ_ONLY));
		FMemory::Memcpy(SourceData, MipData, TotalPixels * 4);
		Mip->BulkData.Unlock();
		Texture->Source.UnlockMip(0);
	}

	Texture->UpdateResource();

	// Register newly created asset
	Package->MarkPackageDirty();
	if (bCreatedTexture)
	{
		FAssetRegistryModule::AssetCreated(Texture);
	}

	UE_LOG(LogTemp, Log, TEXT("AsepriteImporter: Created sprite sheet '%s' (%dx%d, %d frames in %dx%d grid)"),
		*AssetName, SheetWidth, SheetHeight, FrameCount, Columns, Rows);

	return Texture;
}

// ============================================
// CreateSprites
// ============================================

TArray<UPaperSprite*> FAsepriteImporter::CreateSprites(
	UTexture2D* SpriteSheet,
	const FAsepriteParsedData& Data,
	const FString& OutputPath,
	const FString& AssetPrefix)
{
	TArray<UPaperSprite*> Sprites;
	if (!SpriteSheet || Data.Frames.Num() == 0) return Sprites;

	int32 FrameCount = Data.Frames.Num();
	int32 FrameW = Data.Width;
	int32 FrameH = Data.Height;

	// Must match CreateSpriteSheetTexture's grid exactly so each sprite's source region lines up
	// with the generated sheet.
	const FSpriteSheetGrid Grid = FSpriteSheetGrid::Compute(FrameCount, FrameW, FrameH);
	if (!Grid.bValid)
	{
		UE_LOG(LogTemp, Error, TEXT("AsepriteImporter: CreateSprites grid for %d frames of %dx%d exceeds the maximum texture dimension; aborting."),
			FrameCount, FrameW, FrameH);
		return Sprites;
	}

	for (int32 FrameIdx = 0; FrameIdx < FrameCount; FrameIdx++)
	{
		FString SpriteName = FString::Printf(TEXT("%s_%02d"), *AssetPrefix, FrameIdx);
		FString PackageName = OutputPath / SpriteName;

		UPackage* Package = CreatePackage(*PackageName);
		if (!Package) continue;

		bool bCreatedSprite = false;
		UPaperSprite* Sprite = FindOrCreateAssetInPackage<UPaperSprite>(Package, SpriteName, bCreatedSprite);
		if (!Sprite) continue;

		FSpriteAssetInitParameters InitParams;
		InitParams.Texture = SpriteSheet;
		InitParams.Offset = Grid.GetCellRect(FrameIdx).Min;
		InitParams.Dimension = FIntPoint(FrameW, FrameH);
		InitParams.SetPixelsPerUnrealUnit(1.0f);
		Sprite->InitializeSprite(InitParams);

		Package->MarkPackageDirty();
		if (bCreatedSprite)
		{
			FAssetRegistryModule::AssetCreated(Sprite);
		}

		Sprites.Add(Sprite);
	}

	UE_LOG(LogTemp, Log, TEXT("AsepriteImporter: Created %d sprites"), Sprites.Num());
	return Sprites;
}

// ============================================
// ComputeFrameTiming — single source of .ase duration -> (FPS, per-key-frame FrameRun)
// ============================================

void FAsepriteImporter::ComputeFrameTiming(const TArray<int32>& DurationsMs, EAsepriteTimingBase Base,
                                           float DefaultFps, float& OutFps, TArray<int32>& OutFrameRuns)
{
	OutFrameRuns.Reset();
	if (DurationsMs.Num() == 0)
	{
		OutFps = DefaultFps;
		return;
	}

	// Sanitize: a 0/negative duration becomes the 100ms default (matches prior importer behavior).
	TArray<int32> Durations;
	Durations.Reserve(DurationsMs.Num());
	for (int32 D : DurationsMs)
	{
		Durations.Add(D > 0 ? D : 100);
	}

	if (Base == EAsepriteTimingBase::GcdExact)
	{
		int32 G = Durations[0];
		for (int32 D : Durations)
		{
			G = FMath::GreatestCommonDivisor(G, D);
		}
		if (G <= 0) // all-zero pathological guard (shouldn't happen after sanitize)
		{
			OutFps = DefaultFps;
			for (int32 i = 0; i < Durations.Num(); ++i) { OutFrameRuns.Add(1); }
			return;
		}
		OutFps = 1000.0f / static_cast<float>(G);
		for (int32 D : Durations)
		{
			OutFrameRuns.Add(FMath::Max(1, D / G)); // exact: G divides every duration
		}
	}
	else // FixedFps
	{
		int32 MinDur = Durations[0];
		for (int32 D : Durations) { MinDur = FMath::Min(MinDur, D); }
		MinDur = FMath::Max(1, MinDur);
		OutFps = 1000.0f / static_cast<float>(MinDur);
		for (int32 D : Durations)
		{
			OutFrameRuns.Add(FMath::Max(1, FMath::RoundToInt(static_cast<float>(D) / static_cast<float>(MinDur))));
		}
	}
}

// ============================================
// TransferHitboxDataToProfile / ProfileHasHitboxConflicts
// Deliver name-convention .ase hitboxes/sockets onto a profile's flipbook entries
// by KEYFRAME INDEX, applying a per-frame policy. Pure data (worldless-testable).
// ============================================

namespace
{
	/** Mirror CreateFlipbooks' naming (sanitize + prefix strip) so the computed name equals the
	 *  stored Entry.Identity.FlipbookName — shared by both the new-profile populate loop and the
	 *  by-name transfer below. */
	FString ComputeProfileAnimName(const FString& AssetPrefix, const FString& TagName)
	{
		FString Name = FString::Printf(TEXT("%s_%s"), *AssetPrefix, *TagName);
		FSpriteExtractionUtils::SanitizeAssetName(Name);
		if (Name.StartsWith(AssetPrefix + TEXT("_")))
		{
			Name.RightChopInline(AssetPrefix.Len() + 1);
		}
		return Name;
	}

	/** Two hitboxes are an exact duplicate when type + geometry (and combat scalars) all match. Used to
	 *  dedupe a no-op re-import under EHitboxApplyPolicy::Merge. */
	bool IsSameHitbox(const FHitboxData& A, const FHitboxData& B)
	{
		return A.Type == B.Type
			&& A.X == B.X && A.Y == B.Y && A.Width == B.Width && A.Height == B.Height
			&& A.Z == B.Z && A.Depth == B.Depth
			&& A.Damage == B.Damage && A.Knockback == B.Knockback;
	}

	/** Two sockets are an exact duplicate when name + position match. */
	bool IsSameSocket(const FSocketData& A, const FSocketData& B)
	{
		return A.Name.Equals(B.Name, ESearchCase::IgnoreCase) && A.X == B.X && A.Y == B.Y;
	}

	/** Apply the imported boxes/sockets of one source frame onto one target profile frame under Policy. */
	void ApplyFrameHitboxes(FFrameHitboxData& Target, const FAsepriteExtractedFrameData& Source, EHitboxApplyPolicy Policy)
	{
		switch (Policy)
		{
		case EHitboxApplyPolicy::Overwrite:
			Target.Hitboxes = Source.Hitboxes;
			Target.Sockets = Source.Sockets;
			break;

		case EHitboxApplyPolicy::Apply:
			// Fill-empty: only write the kind that the target currently lacks; never touch existing author data.
			if (Target.Hitboxes.Num() == 0)
			{
				Target.Hitboxes = Source.Hitboxes;
			}
			if (Target.Sockets.Num() == 0)
			{
				Target.Sockets = Source.Sockets;
			}
			break;

		case EHitboxApplyPolicy::Merge:
			// Append, skipping exact-geometry+type duplicates so a no-op re-import stays stable.
			for (const FHitboxData& Box : Source.Hitboxes)
			{
				if (!Target.Hitboxes.ContainsByPredicate([&Box](const FHitboxData& E) { return IsSameHitbox(E, Box); }))
				{
					Target.Hitboxes.Add(Box);
				}
			}
			for (const FSocketData& Sock : Source.Sockets)
			{
				if (!Target.Sockets.ContainsByPredicate([&Sock](const FSocketData& E) { return IsSameSocket(E, Sock); }))
				{
					Target.Sockets.Add(Sock);
				}
			}
			break;
		}
	}

	/** Grow Entry.CombatData.Frames up to the entry flipbook's key-frame count (never shrink). Returns the
	 *  resulting Frames count (0 if there is no resolvable flipbook). */
	int32 GrowEntryFramesToKeyframeCount(FFlipbookProfileEntry& Entry)
	{
		UPaperFlipbook* FB = Entry.Identity.Flipbook.IsNull() ? nullptr : Entry.Identity.Flipbook.LoadSynchronous();
		if (FB)
		{
			const int32 KeyFrames = FB->GetNumKeyFrames();
			if (Entry.CombatData.Frames.Num() < KeyFrames)
			{
				Entry.CombatData.Frames.SetNum(KeyFrames); // GROW only — never truncate existing per-frame data
			}
		}
		return Entry.CombatData.Frames.Num();
	}

	/** True iff Entry is the no-tags all-frames flipbook for AssetPrefix. Prefix sanitization makes the stored
	 *  Entry.Identity.FlipbookName ambiguous: depending on which creation path produced it, the no-tags entry can be
	 *  stored as the prefix-stripped clean "All", or as the un-stripped "<sanitized-prefix>_All" (the strip in the
	 *  populate loop compares the SANITIZED asset name against the RAW prefix, so a prefix with a space — "My Character"
	 *  -> "My_Character_All" — never strips). Accept BOTH forms (and the legacy ComputeProfileAnimName form) so an
	 *  untagged import always finds its entry regardless of which side sanitized first. Case-insensitive to mirror the
	 *  rest of the importer's name matching. Tagged matching is unchanged. */
	bool EntryIsAllFrames(const FFlipbookProfileEntry& Entry, const FString& AssetPrefix)
	{
		const FString& Name = Entry.Identity.FlipbookName;
		// 1) The clean prefix-stripped form: a bare "All".
		if (Name.Equals(TEXT("All"), ESearchCase::IgnoreCase)) { return true; }
		// 2) The legacy ComputeProfileAnimName form (raw-prefix strip — agrees only when the prefix has no space).
		if (Name.Equals(ComputeProfileAnimName(AssetPrefix, TEXT("All")), ESearchCase::IgnoreCase)) { return true; }
		// 3) The un-stripped sanitized form "<sanitized-prefix>_All" (the populate-loop output for a spaced prefix).
		FString SanitizedPrefix = AssetPrefix;
		FSpriteExtractionUtils::SanitizeAssetName(SanitizedPrefix);
		if (!SanitizedPrefix.IsEmpty()
			&& Name.Equals(SanitizedPrefix + TEXT("_All"), ESearchCase::IgnoreCase)) { return true; }
		return false;
	}

	/** Resolve the single all-frames entry for the no-tags path: prefer the "<prefix>_All"/"All" name match (robust to
	 *  prefix sanitization — see EntryIsAllFrames), else — only when bAllowSoleEntryFallback — the sole entry when there
	 *  is exactly one. Returns INDEX_NONE otherwise. The new-profile path passes true (the sole entry IS the freshly-built
	 *  _All flipbook); the existing-profile path passes false (don't write onto an arbitrary sole flipbook that isn't the
	 *  all-frames entry). */
	int32 FindAllFramesEntryIndex(const UPaper2DPlusCharacterProfileAsset* Profile, const FString& AssetPrefix,
		bool bAllowSoleEntryFallback)
	{
		for (int32 i = 0; i < Profile->Flipbooks.Num(); ++i)
		{
			if (EntryIsAllFrames(Profile->Flipbooks[i], AssetPrefix))
			{
				return i;
			}
		}
		return (bAllowSoleEntryFallback && Profile->Flipbooks.Num() == 1) ? 0 : INDEX_NONE;
	}
}

// Display-order source-frame sequence for a tag, honoring LoopDirection. Single source of truth shared by
// CreateFlipbooks (key-frame emission) and TransferHitboxDataToProfile (hitbox→key-frame alignment).
TArray<int32> FAsepriteImporter::BuildTagFrameSequence(const FAsepriteTag& Tag)
{
	int32 FromFrame = Tag.FromFrame;
	int32 ToFrame = Tag.ToFrame;

	TArray<int32> Sequence;

	if (FromFrame > ToFrame)
	{
		Swap(FromFrame, ToFrame);
	}

	// Forward: 0, Reverse: 1, PingPong: 2
	switch (Tag.LoopDirection)
	{
	case 1:
		for (int32 i = ToFrame; i >= FromFrame; i--)
		{
			Sequence.Add(i);
		}
		break;

	case 2:
		for (int32 i = FromFrame; i <= ToFrame; i++)
		{
			Sequence.Add(i);
		}
		for (int32 i = ToFrame - 1; i > FromFrame; i--)
		{
			Sequence.Add(i);
		}
		break;

	case 0:
	default:
		for (int32 i = FromFrame; i <= ToFrame; i++)
		{
			Sequence.Add(i);
		}
		break;
	}

	return Sequence;
}

void FAsepriteImporter::TransferHitboxDataToProfile(
	UPaper2DPlusCharacterProfileAsset* Profile,
	const TArray<FAsepriteExtractedFrameData>& FrameHitboxData,
	const TArray<FAsepriteTag>& Tags,
	const FString& AssetPrefix,
	EHitboxApplyPolicy Policy,
	int32* OutUndeliveredTags,
	bool bAllowSoleEntryFallback)
{
	if (OutUndeliveredTags)
	{
		*OutUndeliveredTags = 0;
	}
	if (!Profile || FrameHitboxData.Num() == 0)
	{
		return;
	}

	// --- No-tags path: a single all-frames flipbook gets FrameHitboxData[0..N] in keyframe order. ---
	if (Tags.Num() == 0)
	{
		const int32 EntryIdx = FindAllFramesEntryIndex(Profile, AssetPrefix, bAllowSoleEntryFallback);
		if (EntryIdx == INDEX_NONE)
		{
			if (OutUndeliveredTags) { *OutUndeliveredTags += 1; }
			return;
		}
		FFlipbookProfileEntry& Entry = Profile->Flipbooks[EntryIdx];
		const int32 FrameCount = GrowEntryFramesToKeyframeCount(Entry);
		const int32 N = FMath::Min(FrameCount, FrameHitboxData.Num());
		for (int32 KeyframeIdx = 0; KeyframeIdx < N; ++KeyframeIdx)
		{
			ApplyFrameHitboxes(Entry.CombatData.Frames[KeyframeIdx], FrameHitboxData[KeyframeIdx], Policy);
		}
		return;
	}

	// --- Tagged path: match each tag to its flipbook by sanitized/prefix-stripped name. ---
	// Consume each matched entry once so two tags sanitizing to the same name map to distinct entries
	// rather than both landing on the first match.
	TSet<int32> ConsumedEntries;
	for (const FAsepriteTag& Tag : Tags)
	{
		const FString AnimName = ComputeProfileAnimName(AssetPrefix, Tag.Name);
		int32 EntryIdx = INDEX_NONE;
		for (int32 i = 0; i < Profile->Flipbooks.Num(); ++i)
		{
			if (!ConsumedEntries.Contains(i) && Profile->Flipbooks[i].Identity.FlipbookName == AnimName)
			{
				EntryIdx = i;
				break;
			}
		}
		if (EntryIdx == INDEX_NONE)
		{
			// Name divergence: no (remaining) flipbook for this tag — report it rather than silently skipping.
			if (OutUndeliveredTags) { *OutUndeliveredTags += 1; }
			continue;
		}
		ConsumedEntries.Add(EntryIdx);

		FFlipbookProfileEntry& Entry = Profile->Flipbooks[EntryIdx];

		// DISPLAY-order sequence: Seq[k] is the source frame that key-frame k displays (Reverse/PingPong correct).
		const TArray<int32> Seq = BuildTagFrameSequence(Tag);

		// Layout-divergence guard: the target flipbook must have exactly Seq.Num() key-frames (it does when it was
		// just built from this same Seq; it may not on an existing, hand-edited/re-authored flipbook). If it
		// diverges, do NOT index-map onto a different layout — skip the tag, count it, and warn.
		UPaperFlipbook* FB = Entry.Identity.Flipbook.IsNull() ? nullptr : Entry.Identity.Flipbook.LoadSynchronous();
		const int32 KeyFrameCount = FB ? FB->GetNumKeyFrames() : 0;
		if (KeyFrameCount != Seq.Num())
		{
			if (OutUndeliveredTags) { *OutUndeliveredTags += 1; }
			UE_LOG(LogTemp, Warning,
				TEXT("AsepriteImporter: skipping hitbox delivery for animation '%s' (tag '%s') — flipbook has %d key-frames but the tag's display sequence is %d frames (layout divergence; hitboxes would land on the wrong frames)."),
				*AnimName, *Tag.Name, KeyFrameCount, Seq.Num());
			continue;
		}

		GrowEntryFramesToKeyframeCount(Entry);

		for (int32 KeyframeIdx = 0; KeyframeIdx < Seq.Num(); ++KeyframeIdx)
		{
			const int32 SourceFrame = Seq[KeyframeIdx];
			if (SourceFrame < 0 || SourceFrame >= FrameHitboxData.Num())
			{
				continue; // source frame out of range
			}
			if (KeyframeIdx >= Entry.CombatData.Frames.Num())
			{
				continue; // defensive: target keyframe out of range
			}
			ApplyFrameHitboxes(Entry.CombatData.Frames[KeyframeIdx], FrameHitboxData[SourceFrame], Policy);
		}
	}
}

int32 FAsepriteImporter::CountHitboxConflicts(
	const UPaper2DPlusCharacterProfileAsset* Profile,
	const TArray<FAsepriteExtractedFrameData>& FrameHitboxData,
	const TArray<FAsepriteTag>& Tags,
	const FString& AssetPrefix,
	bool bAllowSoleEntryFallback)
{
	if (!Profile || FrameHitboxData.Num() == 0)
	{
		return 0;
	}

	auto FrameIsOccupied = [](const FFrameHitboxData& Frame)
	{
		return Frame.Hitboxes.Num() > 0 || Frame.Sockets.Num() > 0;
	};

	int32 ConflictCount = 0;

	// No-tags path: the all-frames flipbook's targeted keyframes.
	if (Tags.Num() == 0)
	{
		const int32 EntryIdx = FindAllFramesEntryIndex(Profile, AssetPrefix, bAllowSoleEntryFallback);
		if (EntryIdx == INDEX_NONE)
		{
			return 0;
		}
		const FFlipbookProfileEntry& Entry = Profile->Flipbooks[EntryIdx];
		const int32 N = FMath::Min(Entry.CombatData.Frames.Num(), FrameHitboxData.Num());
		for (int32 KeyframeIdx = 0; KeyframeIdx < N; ++KeyframeIdx)
		{
			if (FrameIsOccupied(Entry.CombatData.Frames[KeyframeIdx]))
			{
				++ConflictCount;
			}
		}
		return ConflictCount;
	}

	// Tagged path: mirror the transfer's name-match + consume-once + display-order keyframe mapping (read-only).
	TSet<int32> ConsumedEntries;
	for (const FAsepriteTag& Tag : Tags)
	{
		const FString AnimName = ComputeProfileAnimName(AssetPrefix, Tag.Name);
		int32 EntryIdx = INDEX_NONE;
		for (int32 i = 0; i < Profile->Flipbooks.Num(); ++i)
		{
			if (!ConsumedEntries.Contains(i) && Profile->Flipbooks[i].Identity.FlipbookName == AnimName)
			{
				EntryIdx = i;
				break;
			}
		}
		if (EntryIdx == INDEX_NONE)
		{
			continue;
		}
		ConsumedEntries.Add(EntryIdx);

		const FFlipbookProfileEntry& Entry = Profile->Flipbooks[EntryIdx];

		// Same display-order mapping the transfer uses; same layout-divergence guard (a diverged flipbook is
		// skipped by the transfer, so it can't be a conflict here either).
		const TArray<int32> Seq = BuildTagFrameSequence(Tag);
		const UPaperFlipbook* FB = Entry.Identity.Flipbook.IsNull() ? nullptr : Entry.Identity.Flipbook.LoadSynchronous();
		const int32 KeyFrameCount = FB ? FB->GetNumKeyFrames() : 0;
		if (KeyFrameCount != Seq.Num())
		{
			continue;
		}

		for (int32 KeyframeIdx = 0; KeyframeIdx < Seq.Num(); ++KeyframeIdx)
		{
			const int32 SourceFrame = Seq[KeyframeIdx];
			if (SourceFrame < 0 || SourceFrame >= FrameHitboxData.Num())
			{
				continue;
			}
			if (KeyframeIdx >= Entry.CombatData.Frames.Num())
			{
				continue;
			}
			if (FrameIsOccupied(Entry.CombatData.Frames[KeyframeIdx]))
			{
				++ConflictCount;
			}
		}
	}
	return ConflictCount;
}

bool FAsepriteImporter::ProfileHasHitboxConflicts(
	const UPaper2DPlusCharacterProfileAsset* Profile,
	const TArray<FAsepriteExtractedFrameData>& FrameHitboxData,
	const TArray<FAsepriteTag>& Tags,
	const FString& AssetPrefix,
	bool bAllowSoleEntryFallback)
{
	return CountHitboxConflicts(Profile, FrameHitboxData, Tags, AssetPrefix, bAllowSoleEntryFallback) > 0;
}

// ============================================
// CreateFlipbooks
// ============================================

TArray<UPaperFlipbook*> FAsepriteImporter::CreateFlipbooks(
	const TArray<UPaperSprite*>& Sprites,
	const FAsepriteParsedData& Data,
	const FString& OutputPath,
	const FString& AssetPrefix)
{
	TArray<UPaperFlipbook*> Flipbooks;
	if (Sprites.Num() == 0) return Flipbooks;

	// Helper lambda to create a flipbook from an explicit frame sequence
	auto CreateSingleFlipbook = [&](const FString& FlipbookName, const TArray<int32>& FrameIndices, float DefaultFPS) -> UPaperFlipbook*
	{
		if (FrameIndices.Num() == 0)
		{
			return nullptr;
		}

		FString PackageName = OutputPath / FlipbookName;
		UPackage* Package = CreatePackage(*PackageName);
		if (!Package) return nullptr;

		bool bCreatedFlipbook = false;
		UPaperFlipbook* Flipbook = FindOrCreateAssetInPackage<UPaperFlipbook>(Package, FlipbookName, bCreatedFlipbook);
		if (!Flipbook) return nullptr;

		// Per-key-frame timing from each frame's .ase duration (Sprites[i] is 1:1 with Data.Frames[i]).
		// GcdExact reproduces the artist's per-frame durations exactly; FixedFps is the round-FPS opt-in.
		TArray<int32> DurationsMs;
		DurationsMs.Reserve(FrameIndices.Num());
		for (int32 FrameIndex : FrameIndices)
		{
			if (FrameIndex < 0 || FrameIndex >= Sprites.Num())
			{
				return nullptr;
			}
			DurationsMs.Add(FrameIndex < Data.Frames.Num() ? Data.Frames[FrameIndex].Duration : 100);
		}

		float FramesPerSecond = DefaultFPS;
		TArray<int32> FrameRuns;
		ComputeFrameTiming(DurationsMs, EAsepriteTimingBase::GcdExact, DefaultFPS, FramesPerSecond, FrameRuns);

		{
			FScopedFlipbookMutator Mutator(Flipbook);
			Mutator.FramesPerSecond = FramesPerSecond;
			Mutator.KeyFrames.Empty();

			for (int32 i = 0; i < FrameIndices.Num(); ++i)
			{
				FPaperFlipbookKeyFrame KeyFrame;
				KeyFrame.Sprite = Sprites[FrameIndices[i]];
				// Per-frame FrameRun carries the variable duration (FrameRun / FPS == the .ase ms).
				KeyFrame.FrameRun = FrameRuns.IsValidIndex(i) ? FrameRuns[i] : 1;
				Mutator.KeyFrames.Add(KeyFrame);
			}
		}

		Package->MarkPackageDirty();
		if (bCreatedFlipbook)
		{
			FAssetRegistryModule::AssetCreated(Flipbook);
		}

		return Flipbook;
	};

	if (Data.Tags.Num() > 0)
	{
		// Create one flipbook per tag
		for (const FAsepriteTag& Tag : Data.Tags)
		{
			FString FlipbookName = FString::Printf(TEXT("%s_%s"), *AssetPrefix, *Tag.Name);
			// Sanitize the name
			FSpriteExtractionUtils::SanitizeAssetName(FlipbookName);

			// Clamp the source-frame range to valid sprite indices, then build the display-order sequence via the
			// SHARED helper (identical output to the prior inline builder, incl. LoopDirection). The same helper is
			// used by TransferHitboxDataToProfile so imported hitboxes align to the frames emitted here.
			int32 FromFrame = FMath::Clamp(Tag.FromFrame, 0, Sprites.Num() - 1);
			int32 ToFrame = FMath::Clamp(Tag.ToFrame, 0, Sprites.Num() - 1);
			FAsepriteTag ClampedTag = Tag;
			ClampedTag.FromFrame = FromFrame;
			ClampedTag.ToFrame = ToFrame;
			TArray<int32> FrameSequence = BuildTagFrameSequence(ClampedTag);

			UPaperFlipbook* Flipbook = CreateSingleFlipbook(FlipbookName, FrameSequence, 10.0f);
			if (Flipbook)
			{
				Flipbooks.Add(Flipbook);
				UE_LOG(LogTemp, Log, TEXT("AsepriteImporter: Created flipbook '%s' (tag: %s, loopDirection: %d, source frames %d-%d, emitted %d frames)"),
					*FlipbookName, *Tag.Name, Tag.LoopDirection, FromFrame, ToFrame, FrameSequence.Num());
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("AsepriteImporter: Failed to create flipbook '%s' (tag: %s, frames %d-%d)"),
					*FlipbookName, *Tag.Name, FromFrame, ToFrame);
			}
		}
	}
	else
	{
		// No tags - create a single flipbook with all frames
		FString FlipbookName = FString::Printf(TEXT("%s_All"), *AssetPrefix);
		TArray<int32> AllFrames;
		AllFrames.Reserve(Sprites.Num());
		for (int32 i = 0; i < Sprites.Num(); i++)
		{
			AllFrames.Add(i);
		}

		UPaperFlipbook* Flipbook = CreateSingleFlipbook(FlipbookName, AllFrames, 10.0f);
		if (Flipbook)
		{
			Flipbooks.Add(Flipbook);
			UE_LOG(LogTemp, Log, TEXT("AsepriteImporter: Created flipbook '%s' with all %d frames"),
				*FlipbookName, Sprites.Num());
		}
	}

	return Flipbooks;
}

// ============================================
// ImportFile
// ============================================

FAsepriteImportResult FAsepriteImporter::ImportFile(
	const FString& FilePath,
	const FString& OutputPath,
	const FString& AssetPrefix)
{
	FAsepriteImportResult Result;

	// Parse the Aseprite file
	FAsepriteParsedData ParsedData;
	if (!ParseFile(FilePath, ParsedData, Result.ErrorMessage))
	{
		Result.bSuccess = false;
		return Result;
	}

	FScopedSlowTask Progress(3, LOCTEXT("ImportingAseprite", "Importing Aseprite file..."));
	Progress.MakeDialog();

	// Create sprite sheet texture
	Progress.EnterProgressFrame(1, LOCTEXT("CreatingSpriteSheet", "Creating sprite sheet texture..."));
	FString TextureName = AssetPrefix + TEXT("_Sheet");
	Result.SpriteSheet = CreateSpriteSheetTexture(ParsedData, OutputPath, TextureName);
	if (!Result.SpriteSheet)
	{
		Result.ErrorMessage = TEXT("Failed to create sprite sheet texture.");
		Result.bSuccess = false;
		return Result;
	}

	// Create sprites
	Progress.EnterProgressFrame(1, LOCTEXT("CreatingSprites", "Creating sprites..."));
	Result.Sprites = CreateSprites(Result.SpriteSheet, ParsedData, OutputPath, AssetPrefix);
	if (Result.Sprites.Num() == 0)
	{
		Result.ErrorMessage = TEXT("Failed to create any sprites.");
		Result.bSuccess = false;
		return Result;
	}

	// Create flipbooks
	Progress.EnterProgressFrame(1, LOCTEXT("CreatingFlipbooks", "Creating flipbooks..."));
	Result.Flipbooks = CreateFlipbooks(Result.Sprites, ParsedData, OutputPath, AssetPrefix);

	// Transfer extracted hitbox/socket data to the import result
	if (ParsedData.ExtractedFrameData.Num() > 0)
	{
		Result.FrameHitboxData = ParsedData.ExtractedFrameData;
	}

	Result.bSuccess = true;

	UE_LOG(LogTemp, Log, TEXT("AsepriteImporter: Import complete - %d sprites, %d flipbooks, %d hitbox layers from '%s'"),
		Result.Sprites.Num(), Result.Flipbooks.Num(), ParsedData.HitboxLayers.Num(), *FilePath);

	return Result;
}

// ============================================
// ShowImportDialog
// ============================================

void FAsepriteImporter::ShowImportDialog()
{
	// Open file dialog for .ase/.aseprite files
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (!DesktopPlatform) return;

	TArray<FString> OutFiles;
	bool bOpened = DesktopPlatform->OpenFileDialog(
		FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
		TEXT("Select Aseprite File"),
		FEditorDirectories::Get().GetLastDirectory(ELastDirectory::GENERIC_IMPORT),
		TEXT(""),
		TEXT("Aseprite Files (*.ase;*.aseprite)|*.ase;*.aseprite"),
		EFileDialogFlags::None,
		OutFiles
	);

	if (!bOpened || OutFiles.Num() == 0) return;

	FString SelectedFile = OutFiles[0];
	FEditorDirectories::Get().SetLastDirectory(ELastDirectory::GENERIC_IMPORT, FPaths::GetPath(SelectedFile));

	// Extract filename for defaults
	FString FileName = FPaths::GetBaseFilename(SelectedFile);

	// Create settings dialog
	TSharedRef<SWindow> DialogWindow = SNew(SWindow)
		.Title(LOCTEXT("AsepriteImportTitle", "Import Aseprite File"))
		.ClientSize(FVector2D(450, 220))
		.SupportsMinimize(false)
		.SupportsMaximize(false)
		.IsTopmostWindow(true);

	// Shared settings for the dialog
	TSharedPtr<FString> OutputPathPtr = MakeShared<FString>(TEXT("/Game/Sprites"));
	TSharedPtr<FString> AssetPrefixPtr = MakeShared<FString>(FileName);
	TSharedPtr<FString> FilePathPtr = MakeShared<FString>(SelectedFile);
	TSharedPtr<SWindow> DialogWindowPtr = MakeShareable(&DialogWindow.Get(), [](SWindow*) {}); // Non-owning shared ptr

	TWeakPtr<SWindow> WeakDialogWindow = DialogWindow;

	DialogWindow->SetContent(
		SNew(SBox)
		.Padding(16)
		[
			SNew(SVerticalBox)

			// File path display
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 0, 0, 8)
			[
				SNew(STextBlock)
				.Text(FText::Format(LOCTEXT("SelectedFileLabel", "File: {0}"), FText::FromString(FPaths::GetCleanFilename(SelectedFile))))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
			]

			// Output path
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 4)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.FillWidth(0.3f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("OutputPathLabel", "Output Path:"))
				]

				+ SHorizontalBox::Slot()
				.FillWidth(0.7f)
				[
					SNew(SEditableTextBox)
					.Text(FText::FromString(*OutputPathPtr))
					.OnTextCommitted_Lambda([OutputPathPtr](const FText& Text, ETextCommit::Type)
					{
						*OutputPathPtr = Text.ToString();
					})
				]
			]

			// Asset prefix
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 4)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.FillWidth(0.3f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("AssetPrefixLabel", "Asset Prefix:"))
				]

				+ SHorizontalBox::Slot()
				.FillWidth(0.7f)
				[
					SNew(SEditableTextBox)
					.Text(FText::FromString(*AssetPrefixPtr))
					.OnTextCommitted_Lambda([AssetPrefixPtr](const FText& Text, ETextCommit::Type)
					{
						*AssetPrefixPtr = Text.ToString();
					})
				]
			]

			// Spacer
			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			[
				SNullWidget::NullWidget
			]

			// Buttons
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 8, 0, 0)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					SNullWidget::NullWidget
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(4, 0)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
					.Text(LOCTEXT("CancelButton", "Cancel"))
					.OnClicked_Lambda([WeakDialogWindow]()
					{
						if (TSharedPtr<SWindow> Window = WeakDialogWindow.Pin())
						{
							Window->RequestDestroyWindow();
						}
						return FReply::Handled();
					})
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(4, 0)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "FlatButton.Success")
					.Text(LOCTEXT("ImportButton", "Import"))
					.OnClicked_Lambda([FilePathPtr, OutputPathPtr, AssetPrefixPtr, WeakDialogWindow]()
					{
						FAsepriteImportResult Result = ImportFile(*FilePathPtr, *OutputPathPtr, *AssetPrefixPtr);

						if (Result.bSuccess)
						{
							FNotificationInfo Info(FText::Format(
								LOCTEXT("ImportSuccess", "Aseprite import complete: {0} sprites, {1} flipbooks"),
								FText::AsNumber(Result.Sprites.Num()),
								FText::AsNumber(Result.Flipbooks.Num())
							));
							Info.ExpireDuration = 5.0f;
							FSlateNotificationManager::Get().AddNotification(Info);
						}
						else
						{
							FNotificationInfo Info(FText::Format(
								LOCTEXT("ImportFailed", "Aseprite import failed: {0}"),
								FText::FromString(Result.ErrorMessage)
							));
							Info.ExpireDuration = 8.0f;
							FSlateNotificationManager::Get().AddNotification(Info);
						}

						if (TSharedPtr<SWindow> Window = WeakDialogWindow.Pin())
						{
							Window->RequestDestroyWindow();
						}
						return FReply::Handled();
					})
				]
			]
		]
	);

	FSlateApplication::Get().AddWindow(DialogWindow);
}

// ============================================
// DeriveNormalMapPairings (TASK-72) — pure, worldless-testable
// ============================================

void FAsepriteImporter::DeriveNormalMapPairings(
	const TArray<FString>& LayerNames,
	const TArray<FString>& NormalSuffixes,
	TMap<int32, int32>& OutBaseToNormal,
	TSet<int32>& OutPairedNormalIndices)
{
	OutBaseToNormal.Reset();
	OutPairedNormalIndices.Reset();

	if (LayerNames.Num() == 0 || NormalSuffixes.Num() == 0)
	{
		return; // No-op: nothing to pair.
	}

	// Sanitise + dedupe the suffixes, then sort LONGEST-FIRST so "_normal" is tried before "_n" (a name ending in
	// "_normal" also ends in "_n"; the longer suffix is the intended match and strips to the correct base).
	TArray<FString> Suffixes;
	for (const FString& Raw : NormalSuffixes)
	{
		const FString S = Raw.TrimStartAndEnd();
		if (!S.IsEmpty())
		{
			Suffixes.AddUnique(S);
		}
	}
	if (Suffixes.Num() == 0)
	{
		return;
	}
	Suffixes.Sort([](const FString& A, const FString& B) { return A.Len() > B.Len(); });

	// Map lowercased base name -> first (lowest-index) layer that bears it, so suffix-stripped names resolve to a base.
	TMap<FString, int32> LowerNameToIndex;
	LowerNameToIndex.Reserve(LayerNames.Num());
	for (int32 i = 0; i < LayerNames.Num(); ++i)
	{
		const FString Lower = LayerNames[i].ToLower();
		if (!LowerNameToIndex.Contains(Lower))
		{
			LowerNameToIndex.Add(Lower, i);
		}
	}

	// A base is paired to at most one normal; a normal pairs to at most one base. Walk in ascending index so the
	// result is deterministic (first matching normal wins for a given base).
	TSet<int32> BasesClaimed;
	for (int32 i = 0; i < LayerNames.Num(); ++i)
	{
		const FString Lower = LayerNames[i].ToLower();
		for (const FString& Suffix : Suffixes)
		{
			const FString LowerSuffix = Suffix.ToLower();
			if (!Lower.EndsWith(LowerSuffix))
			{
				continue;
			}

			// Strip the suffix to get the candidate base name; an empty base (the layer IS just the suffix) can't pair.
			const FString BaseLower = Lower.LeftChop(LowerSuffix.Len());
			if (BaseLower.IsEmpty())
			{
				break; // matched a suffix but there's no base stem — leave it as a normal visual layer.
			}

			const int32* BaseIdxPtr = LowerNameToIndex.Find(BaseLower);
			if (BaseIdxPtr && *BaseIdxPtr != i && !BasesClaimed.Contains(*BaseIdxPtr)
				&& !OutPairedNormalIndices.Contains(*BaseIdxPtr))
			{
				OutBaseToNormal.Add(*BaseIdxPtr, i);
				OutPairedNormalIndices.Add(i);
				BasesClaimed.Add(*BaseIdxPtr);
			}
			// Whether or not the base existed, stop after the first (longest) suffix this layer matches.
			break;
		}
	}
}

void FAsepriteImporter::PruneNormalPairingsToEnabledBases(
	TMap<int32, int32>& BaseToNormal,
	TSet<int32>& PairedNormalIndices,
	const TSet<int32>& EnabledBaseIndices)
{
	for (auto It = BaseToNormal.CreateIterator(); It; ++It)
	{
		if (!EnabledBaseIndices.Contains(It.Key()))
		{
			// Base isn't being imported -> release its normal back to the visual set (it may itself be enabled) and
			// drop the pairing, so the normal isn't silently lost to a base that never materialises.
			PairedNormalIndices.Remove(It.Value());
			It.RemoveCurrent();
		}
	}
}

// ============================================
// CompositePerLayer
// ============================================

TMap<int32, TArray<TArray<FColor>>> FAsepriteImporter::CompositePerLayer(const FAsepriteParsedData& Data)
{
	TMap<int32, TArray<TArray<FColor>>> Result;

	if (!Data.bIsValid || Data.AllFrameCels.Num() == 0)
	{
		return Result;
	}

	const int32 FrameCount = Data.Frames.Num();
	const int32 FrameW = Data.Width;
	const int32 FrameH = Data.Height;
	const int32 PixelCount = FrameW * FrameH;

	for (int32 LayerIdx = 0; LayerIdx < Data.Layers.Num(); LayerIdx++)
	{
		const FAsepriteLayer& Layer = Data.Layers[LayerIdx];

		// Skip group layers
		if (Layer.LayerType == 1)
		{
			continue;
		}

		// Skip hitbox/socket layers
		bool bIsHitboxLayer = false;
		for (const FAsepriteHitboxLayer& HL : Data.HitboxLayers)
		{
			if (HL.LayerIndex == LayerIdx)
			{
				bIsHitboxLayer = true;
				break;
			}
		}
		if (bIsHitboxLayer)
		{
			continue;
		}

		TArray<TArray<FColor>>& LayerFrames = Result.Add(LayerIdx);
		LayerFrames.SetNum(FrameCount);

		for (int32 FrameIdx = 0; FrameIdx < FrameCount; FrameIdx++)
		{
			TArray<FColor>& FrameBuffer = LayerFrames[FrameIdx];
			FrameBuffer.SetNumZeroed(PixelCount);

			const FAsepriteCelData* PositionCel = nullptr;
			const FAsepriteCelData* PixelCel = ResolveCel(Data.AllFrameCels, FrameIdx, LayerIdx, &PositionCel);

			if (!PixelCel || PixelCel->Pixels.Num() == 0 || !PositionCel)
			{
				// Sparse frame — leave as transparent
				continue;
			}

			float CombinedOpacity = (PositionCel->Opacity / 255.0f) * Layer.Opacity;

			FlattenCel(
				FrameBuffer, FrameW, FrameH,
				PixelCel->Pixels, PixelCel->CelWidth, PixelCel->CelHeight,
				PositionCel->X, PositionCel->Y,
				CombinedOpacity
			);
		}
	}

	UE_LOG(LogTemp, Log, TEXT("AsepriteImporter: Composited %d layers independently (%d frames each)"),
		Result.Num(), FrameCount);

	return Result;
}

enum class EPaper2DPlusPackedSheetUsage : uint8
{
	Color,
	TangentNormal
};

static UTexture2D* Paper2DPlus_CreatePackedSpriteSheetTexture(
	const TArray<TArray<FColor>>& FrameBuffers,
	int32 FrameWidth, int32 FrameHeight,
	const FString& OutputPath,
	const FString& AssetName,
	EPaper2DPlusPackedSheetUsage Usage)
{
	if (FrameBuffers.Num() == 0 || FrameWidth <= 0 || FrameHeight <= 0)
	{
		return nullptr;
	}

	const int32 FrameCount = FrameBuffers.Num();

	// Dimension-aware grid packing via the shared helper — must match CreatePerLayerSprites and the
	// reimporter's region regeneration. FIX(audit U12 + Codex #111): 64-bit overflow guard before
	// allocation, plus 2D packing of wide sheets instead of over-rejecting a single overflowing row.
	const FSpriteSheetGrid Grid = FSpriteSheetGrid::Compute(FrameCount, FrameWidth, FrameHeight);
	if (!Grid.bValid)
	{
		UE_LOG(LogTemp, Error, TEXT("AsepriteImporter: %d frames of %dx%d cannot fit a sprite sheet within the maximum texture dimension %d; aborting import."),
			FrameCount, FrameWidth, FrameHeight, FSpriteSheetGrid::DefaultMaxDimension);
		return nullptr;
	}
	const int32 Columns = Grid.Columns;
	const int32 Rows = Grid.Rows;
	const int32 SheetWidth = Grid.SheetW;
	const int32 SheetHeight = Grid.SheetH;

	// Create package
	FString PackageName = OutputPath / AssetName;
	UPackage* Package = CreatePackage(*PackageName);
	if (!Package)
	{
		return nullptr;
	}

	// Create or reuse texture
	bool bCreatedTexture = false;
	UTexture2D* Texture = FindOrCreateAssetInPackage<UTexture2D>(Package, AssetName, bCreatedTexture);
	if (!Texture)
	{
		return nullptr;
	}
	// The same package/name is intentionally reused by import and reimport. Finish the previous
	// asynchronous build before replacing platform data, settings, or source art.
	FTextureCompilingManager::Get().FinishCompilation({ Texture });

	// Initialize platform data
	Texture->SetPlatformData(new FTexturePlatformData());
	Texture->GetPlatformData()->SizeX = SheetWidth;
	Texture->GetPlatformData()->SizeY = SheetHeight;
	Texture->GetPlatformData()->PixelFormat = PF_B8G8R8A8;

	// Create mip 0
	FTexture2DMipMap* Mip = new FTexture2DMipMap();
	Texture->GetPlatformData()->Mips.Add(Mip);
	Mip->SizeX = SheetWidth;
	Mip->SizeY = SheetHeight;

	// Allocate and fill pixel data (64-bit byte math; dimensions already capped above — audit U12)
	const int64 TotalPixels = static_cast<int64>(SheetWidth) * SheetHeight;
	Mip->BulkData.Lock(LOCK_READ_WRITE);
	uint8* DestData = static_cast<uint8*>(Mip->BulkData.Realloc(TotalPixels * 4));
	FMemory::Memzero(DestData, TotalPixels * 4);

	// Copy each frame into the sprite sheet grid
	for (int32 FrameIdx = 0; FrameIdx < FrameCount; FrameIdx++)
	{
		const TArray<FColor>& FrameBuffer = FrameBuffers[FrameIdx];

		const FIntRect Cell = Grid.GetCellRect(FrameIdx);
		const int32 OffsetX = Cell.Min.X;
		const int32 OffsetY = Cell.Min.Y;

		for (int32 Y = 0; Y < FrameHeight; Y++)
		{
			for (int32 X = 0; X < FrameWidth; X++)
			{
				const int32 SrcIdx = Y * FrameWidth + X;
				if (SrcIdx >= FrameBuffer.Num())
				{
					continue;
				}

				const int32 DstIdx = ((OffsetY + Y) * SheetWidth + (OffsetX + X)) * 4;
				const FColor& Pixel = FrameBuffer[SrcIdx];
				DestData[DstIdx + 0] = Pixel.B;
				DestData[DstIdx + 1] = Pixel.G;
				DestData[DstIdx + 2] = Pixel.R;
				DestData[DstIdx + 3] = Pixel.A;
			}
		}
	}

	Mip->BulkData.Unlock();

	// Apply the final texture usage before Source.Init and the one UpdateResource call below. In UE 5.8 the async
	// texture build snapshots Source gamma. Starting an sRGB build and then retagging that live texture as a linear
	// normal map races the worker and can trip TextureDerivedDataTask's gamma-consistency assertion on a cold DDC.
	const bool bIsNormalMap = Usage == EPaper2DPlusPackedSheetUsage::TangentNormal;
	Texture->MipGenSettings = TMGS_NoMipmaps;
	Texture->CompressionSettings = bIsNormalMap ? TC_Normalmap : TC_EditorIcon;
	Texture->Filter = TF_Nearest;
	Texture->NeverStream = true;
	Texture->SRGB = !bIsNormalMap;
	Texture->LODGroup = bIsNormalMap ? TEXTUREGROUP_WorldNormalMap : TEXTUREGROUP_Pixels2D;

	// Source art for re-import and editor display
	Texture->Source.Init(SheetWidth, SheetHeight, 1, 1, TSF_BGRA8);
	{
		uint8* SourceData = Texture->Source.LockMip(0);
		const uint8* MipData = static_cast<const uint8*>(Mip->BulkData.Lock(LOCK_READ_ONLY));
		FMemory::Memcpy(SourceData, MipData, TotalPixels * 4);
		Mip->BulkData.Unlock();
		Texture->Source.UnlockMip(0);
	}

	Texture->UpdateResource();

	// WS3-3D FIX 4: /Temp packages are never saved or submitted to SCC; advertising one to the asset registry / SCC
	// (which MarkPackageDirty + AssetCreated trigger) enqueues an async git-status against an unsubmittable /Temp path
	// that fatally errors ("Invalid path"). Skip both for /Temp targets. Real imports go to /Game/..., unaffected.
	if (!FPackageName::IsTempPackage(Package->GetName()))
	{
		Package->MarkPackageDirty();
		if (bCreatedTexture)
		{
			FAssetRegistryModule::AssetCreated(Texture);
		}
	}

	UE_LOG(LogTemp, Log, TEXT("AsepriteImporter: Created per-layer sprite sheet '%s' (%dx%d, %d frames in %dx%d grid)"),
		*AssetName, SheetWidth, SheetHeight, FrameCount, Columns, Rows);

	return Texture;
}

// ============================================
// CreatePerLayerSpriteSheetTexture
// ============================================

UTexture2D* FAsepriteImporter::CreatePerLayerSpriteSheetTexture(
	const TArray<TArray<FColor>>& FrameBuffers,
	int32 FrameWidth, int32 FrameHeight,
	const FString& OutputPath,
	const FString& AssetName)
{
	return Paper2DPlus_CreatePackedSpriteSheetTexture(
		FrameBuffers,
		FrameWidth,
		FrameHeight,
		OutputPath,
		AssetName,
		EPaper2DPlusPackedSheetUsage::Color);
}

// ============================================
// CreatePerLayerSprites
// ============================================

TArray<FCharacterLayerAnimationMapping> FAsepriteImporter::CreatePerLayerSprites(
	UTexture2D* SpriteSheet,
	const FAsepriteParsedData& Data,
	const FString& OutputPath,
	const FString& AssetPrefix)
{
	TArray<FCharacterLayerAnimationMapping> Mappings;
	if (!SpriteSheet || Data.Frames.Num() == 0)
	{
		return Mappings;
	}

	const int32 FrameCount = Data.Frames.Num();
	const int32 FrameW = Data.Width;
	const int32 FrameH = Data.Height;

	// Must match CreatePerLayerSpriteSheetTexture's grid exactly (and the reimporter's regeneration)
	// so each sprite's source region lines up with the regenerated sheet.
	const FSpriteSheetGrid Grid = FSpriteSheetGrid::Compute(FrameCount, FrameW, FrameH);
	if (!Grid.bValid)
	{
		UE_LOG(LogTemp, Error, TEXT("AsepriteImporter: CreatePerLayerSprites grid for %d frames of %dx%d exceeds the maximum texture dimension; aborting."),
			FrameCount, FrameW, FrameH);
		return Mappings;
	}

	// Create all sprites (one per frame) — shared across animation mappings
	TArray<UPaperSprite*> AllSprites;
	for (int32 FrameIdx = 0; FrameIdx < FrameCount; FrameIdx++)
	{
		FString SpriteName = FString::Printf(TEXT("%s_%02d"), *AssetPrefix, FrameIdx);
		FString PackageName = OutputPath / SpriteName;

		UPackage* Package = CreatePackage(*PackageName);
		if (!Package)
		{
			AllSprites.Add(nullptr);
			continue;
		}

		bool bCreatedSprite = false;
		UPaperSprite* Sprite = FindOrCreateAssetInPackage<UPaperSprite>(Package, SpriteName, bCreatedSprite);
		if (!Sprite)
		{
			AllSprites.Add(nullptr);
			continue;
		}

		FSpriteAssetInitParameters InitParams;
		InitParams.Texture = SpriteSheet;
		InitParams.Offset = Grid.GetCellRect(FrameIdx).Min;
		InitParams.Dimension = FIntPoint(FrameW, FrameH);
		InitParams.SetPixelsPerUnrealUnit(1.0f);
		Sprite->InitializeSprite(InitParams);

		// WS3-3D FIX 4: skip MarkPackageDirty + AssetCreated for /Temp packages (never saved/submitted; the SCC
		// async git-status against a /Temp path fatally errors). Real /Game imports are unaffected.
		if (!FPackageName::IsTempPackage(Package->GetName()))
		{
			Package->MarkPackageDirty();
			if (bCreatedSprite)
			{
				FAssetRegistryModule::AssetCreated(Sprite);
			}
		}

		AllSprites.Add(Sprite);
	}

	// Build animation mappings from tags (or single animation if no tags)
	if (Data.Tags.Num() > 0)
	{
		for (const FAsepriteTag& Tag : Data.Tags)
		{
			FCharacterLayerAnimationMapping Mapping;
			Mapping.AnimationName = Tag.Name;

			int32 FromFrame = FMath::Clamp(Tag.FromFrame, 0, FrameCount - 1);
			int32 ToFrame = FMath::Clamp(Tag.ToFrame, 0, FrameCount - 1);

			for (int32 FrameIdx = FromFrame; FrameIdx <= ToFrame; FrameIdx++)
			{
				if (FrameIdx < AllSprites.Num() && AllSprites[FrameIdx])
				{
					Mapping.Sprites.Add(AllSprites[FrameIdx]);
				}
			}

			if (Mapping.Sprites.Num() > 0)
			{
				Mappings.Add(MoveTemp(Mapping));
			}
		}
	}
	else
	{
		// No tags — single animation using the asset prefix as the name
		FCharacterLayerAnimationMapping Mapping;
		Mapping.AnimationName = AssetPrefix;

		for (int32 FrameIdx = 0; FrameIdx < AllSprites.Num(); FrameIdx++)
		{
			if (AllSprites[FrameIdx])
			{
				Mapping.Sprites.Add(AllSprites[FrameIdx]);
			}
		}

		if (Mapping.Sprites.Num() > 0)
		{
			Mappings.Add(MoveTemp(Mapping));
		}
	}

	UE_LOG(LogTemp, Log, TEXT("AsepriteImporter: Created %d sprites across %d animation mappings for '%s'"),
		AllSprites.Num(), Mappings.Num(), *AssetPrefix);

	return Mappings;
}

// ============================================
// CreateSpritesFromSheet (tag-free; TASK-65 phase 3)
// ============================================

TArray<UPaperSprite*> FAsepriteImporter::CreateSpritesFromSheet(
	UTexture2D* SpriteSheet, int32 FrameW, int32 FrameH, int32 FrameCount,
	const FString& OutputPath, const FString& AssetPrefix)
{
	TArray<UPaperSprite*> AllSprites;
	if (!SpriteSheet || FrameW <= 0 || FrameH <= 0 || FrameCount <= 0) { return AllSprites; }

	// Must match CreatePerLayerSpriteSheetTexture's grid exactly so each sprite's source region lines up.
	const FSpriteSheetGrid Grid = FSpriteSheetGrid::Compute(FrameCount, FrameW, FrameH);
	if (!Grid.bValid)
	{
		UE_LOG(LogTemp, Error, TEXT("AsepriteImporter: CreateSpritesFromSheet grid for %d frames of %dx%d exceeds the maximum texture dimension; aborting."),
			FrameCount, FrameW, FrameH);
		return AllSprites;
	}

	for (int32 FrameIdx = 0; FrameIdx < FrameCount; FrameIdx++)
	{
		const FString SpriteName = FString::Printf(TEXT("%s_%02d"), *AssetPrefix, FrameIdx);
		const FString PackageName = OutputPath / SpriteName;

		UPackage* Package = CreatePackage(*PackageName);
		if (!Package) { AllSprites.Add(nullptr); continue; }

		bool bCreatedSprite = false;
		UPaperSprite* Sprite = FindOrCreateAssetInPackage<UPaperSprite>(Package, SpriteName, bCreatedSprite);
		if (!Sprite) { AllSprites.Add(nullptr); continue; }

		FSpriteAssetInitParameters InitParams;
		InitParams.Texture = SpriteSheet;
		InitParams.Offset = Grid.GetCellRect(FrameIdx).Min;
		InitParams.Dimension = FIntPoint(FrameW, FrameH);
		InitParams.SetPixelsPerUnrealUnit(1.0f);
		Sprite->InitializeSprite(InitParams);

		// WS3-3D FIX 4: skip dirty/registry for /Temp packages (the SCC async git-status against /Temp fatally errors).
		if (!FPackageName::IsTempPackage(Package->GetName()))
		{
			Package->MarkPackageDirty();
			if (bCreatedSprite) { FAssetRegistryModule::AssetCreated(Sprite); }
		}
		AllSprites.Add(Sprite);
	}
	return AllSprites;
}

// ============================================
// CreateNormalMapSheetTexture / AttachNormalMapToSprites (TASK-72)
// ============================================

UTexture2D* FAsepriteImporter::CreateNormalMapSheetTexture(
	const TArray<TArray<FColor>>& FrameBuffers,
	int32 FrameWidth, int32 FrameHeight,
	const FString& OutputPath,
	const FString& AssetName)
{
	// Use the same cell packer as color sheets, but establish linear normal-map settings before Source.Init and the
	// first/only resource build. This keeps the normal grid aligned without ever launching a stale sRGB build.
	return Paper2DPlus_CreatePackedSpriteSheetTexture(
		FrameBuffers,
		FrameWidth,
		FrameHeight,
		OutputPath,
		AssetName,
		EPaper2DPlusPackedSheetUsage::TangentNormal);
}

void FAsepriteImporter::AttachNormalMapToSprites(
	const TArray<UPaperSprite*>& Sprites,
	UTexture2D* NormalTexture,
	UMaterialInterface* LitMaterial,
	int32& OutSpritesTouched)
{
	OutSpritesTouched = 0;
	if (!NormalTexture)
	{
		return;
	}

	for (UPaperSprite* Sprite : Sprites)
	{
		if (!Sprite)
		{
			continue;
		}

		// UPaperSprite::AdditionalSourceTextures and DefaultMaterial are PROTECTED; the supported way to set them is to
		// re-initialise the sprite through FSpriteAssetInitParameters (the SAME path CreatePerLayerSprites used to create
		// it). Reconstruct the source region from the sprite's own editor source data so the re-init is identical except
		// for the added normal texture (-> AdditionalTexture0) and the optional lit-material override. InitializeSprite
		// REPLACES the additional-texture list (so a reimport re-attach stays a single entry — idempotent) and rebuilds
		// render data, so no explicit RebuildRenderData() is needed.
		UTexture2D* const SourceTex = Sprite->GetSourceTexture();
		if (!SourceTex)
		{
			continue;
		}
		const FVector2D SrcUV = Sprite->GetSourceUV();
		const FVector2D SrcSize = Sprite->GetSourceSize();

		FSpriteAssetInitParameters InitParams;
		InitParams.Texture = SourceTex;
		InitParams.Offset = FIntPoint(FMath::RoundToInt(SrcUV.X), FMath::RoundToInt(SrcUV.Y));
		InitParams.Dimension = FIntPoint(FMath::RoundToInt(SrcSize.X), FMath::RoundToInt(SrcSize.Y));
		InitParams.SetPixelsPerUnrealUnit(1.0f);
		InitParams.AdditionalTextures.Add(NormalTexture);
		if (LitMaterial)
		{
			InitParams.DefaultMaterialOverride = LitMaterial;
		}
		Sprite->InitializeSprite(InitParams);

		// WS3-3D FIX 4 parity: skip MarkPackageDirty for /Temp packages (transient; the SCC async git-status against a
		// /Temp path fatally errors — mirrors CreatePerLayerSprites). Real /Game imports persist the attachment.
		if (UPackage* Package = Sprite->GetOutermost())
		{
			if (!FPackageName::IsTempPackage(Package->GetName()))
			{
				Package->MarkPackageDirty();
			}
		}
		++OutSpritesTouched;
	}
}

// ============================================
// RegenerateProfileFromImportResult
// ============================================

void FAsepriteImporter::RegenerateProfileFromImportResult(
	UPaper2DPlusCharacterProfileAsset* Profile,
	const FAsepriteImportResult& ProfileResult,
	const TArray<FAsepriteTag>& Tags,
	const FString& AssetPrefix,
	bool bCreatedProfile)
{
	if (!Profile)
	{
		return;
	}

	// Modify() BEFORE the wipe so FCoreUObjectDelegates::OnObjectModified fires for the profile and open
	// editor models reconcile (U1/R12) — the caller's MarkPackageDirty() alone does NOT fire it. Importer-
	// side disk mutation, NOT a transaction (the ImportFromJsonString recipe).
	Profile->Modify();

	Profile->DisplayName = AssetPrefix;

	// Reused existing profiles (re-import in this mode) would otherwise
	// accumulate duplicate entries; repopulate from a clean slate so the
	// flipbook list and the by-name hitbox transfer below stay correct (U13).
	if (!bCreatedProfile && Profile->Flipbooks.Num() > 0)
	{
		UE_LOG(LogTemp, Log, TEXT("AsepriteImporter: Regenerating existing profile '%s' from re-import (%d prior flipbook entries replaced)."),
			*Profile->GetName(), Profile->Flipbooks.Num());
	}
	Profile->Flipbooks.Empty();

	// Populate flipbook entries from the import result
	for (UPaperFlipbook* Flipbook : ProfileResult.Flipbooks)
	{
		if (!Flipbook) continue;

		FFlipbookProfileEntry Entry;
		Entry.Identity.FlipbookName = Flipbook->GetName();
		// Strip the asset prefix to get the clean animation name
		FString AnimName = Flipbook->GetName();
		if (AnimName.StartsWith(AssetPrefix + TEXT("_")))
		{
			AnimName.RightChopInline(AssetPrefix.Len() + 1);
		}
		Entry.Identity.FlipbookName = AnimName;
		Entry.Identity.Flipbook = Flipbook;
		Entry.SourceTexture = ProfileResult.SpriteSheet;

		Profile->Flipbooks.Add(MoveTemp(Entry));
	}

	// Transfer name-convention hitbox data onto the freshly populated entries by KEYFRAME
	// index (shared helper). First-create => Overwrite (nothing to lose); the helper matches
	// tags to flipbooks by sanitized/prefix-stripped name, grows each entry's Frames to its
	// keyframe count, and also covers the no-tags single-"<prefix>_All"-flipbook case.
	FAsepriteImporter::TransferHitboxDataToProfile(
		Profile, ProfileResult.FrameHitboxData, Tags,
		AssetPrefix, EHitboxApplyPolicy::Overwrite);
}

// ============================================
// ImportAsLayeredAsset
// ============================================

UObject* FAsepriteImporter::ImportAsLayeredAsset(
	FAsepriteParsedData& ParsedData,
	const TMap<int32, TArray<TArray<FColor>>>& PerLayerBuffers,
	const FAsepriteLayerImportSettings& Settings)
{
	// Count checked visual layers for progress tracking
	int32 CheckedLayerCount = 0;
	for (const auto& Pair : Settings.LayerImportEnabled)
	{
		if (Pair.Value)
		{
			CheckedLayerCount++;
		}
	}

	// Total steps: profile (1) + per-layer assets (CheckedLayerCount) + layer asset creation (1) + notification (1)
	const float TotalSteps = 1.0f + static_cast<float>(CheckedLayerCount) + 1.0f + 1.0f;
	FScopedSlowTask Progress(TotalSteps, LOCTEXT("ImportingLayeredAsset", "Importing layered Aseprite asset..."));
	Progress.MakeDialog(true); // bShowCancelButton

	// --- Step 1: Profile handling ---
	Progress.EnterProgressFrame(1.0f, LOCTEXT("HandlingProfile", "Setting up character profile..."));

	UPaper2DPlusCharacterProfileAsset* Profile = nullptr;

	if (Settings.ImportMode == EAsepriteImportMode::LayerAssetNewProfile)
	{
		// Create a new profile by running the standard import pipeline
		// (double-parse accepted — keeps ImportFile unchanged, .ase files parse in <50ms)
		if (!Settings.SourceFilePath.IsEmpty())
		{
			FAsepriteImportResult ProfileResult = FAsepriteImporter::ImportFile(
				Settings.SourceFilePath, Settings.OutputPath, Settings.AssetPrefix);

			if (ProfileResult.bSuccess)
			{
				// Create the profile asset in the same output path
				FString ProfileAssetName = Settings.AssetPrefix + TEXT("_Profile");
				FString ProfilePackageName = Settings.OutputPath / ProfileAssetName;
				UPackage* ProfilePackage = CreatePackage(*ProfilePackageName);
				if (ProfilePackage)
				{
					bool bCreatedProfile = false;
					Profile = FindOrCreateAssetInPackage<UPaper2DPlusCharacterProfileAsset>(
						ProfilePackage, ProfileAssetName, bCreatedProfile);

					if (Profile)
					{
						// Modify() → wipe → repopulate → hitbox transfer. Extracted seam (headless-tested) —
						// the Modify-before-wipe inside is the U1 reconcile-signal fix.
						FAsepriteImporter::RegenerateProfileFromImportResult(
							Profile, ProfileResult, ParsedData.Tags, Settings.AssetPrefix, bCreatedProfile);

						ProfilePackage->MarkPackageDirty();
						if (bCreatedProfile)
						{
							FAssetRegistryModule::AssetCreated(Profile);
						}
					}
				}
			}
		}
	}
	else if (Settings.ImportMode == EAsepriteImportMode::LayerAssetExistingProfile)
	{
		Profile = Settings.ExistingProfile.LoadSynchronous();

		// Deliver name-convention hitboxes onto the EXISTING profile (the branch previously delivered nothing).
		// This profile likely already has authored combat data, so default to the least-destructive Apply
		// (fill-empty) policy. Only when the import would actually overwrite occupied frames AND we're attended
		// do we surface the 3-way Merge/Overwrite/Apply prompt.
		if (Profile && ParsedData.ExtractedFrameData.Num() > 0)
		{
			EHitboxApplyPolicy ChosenPolicy = EHitboxApplyPolicy::Apply;

			// Treat in-editor automation / ECABridge-driven imports as headless too — never block on the modal.
			const bool bHeadless = FApp::IsUnattended() || IsRunningCommandlet() || GIsAutomationTesting;

			// Existing profile: NO sole-entry fallback — require an exact "<prefix>_All" / "<prefix>_<tag>" match so
			// we never write onto an arbitrary single flipbook the user already authored.
			const int32 ConflictCount = FAsepriteImporter::CountHitboxConflicts(
				Profile, ParsedData.ExtractedFrameData, ParsedData.Tags, Settings.AssetPrefix,
				/*bAllowSoleEntryFallback*/ false);

			if (!bHeadless && ConflictCount > 0)
			{
				ChosenPolicy = SHitboxConflictDialog::ShowDialog(ConflictCount, EHitboxApplyPolicy::Apply);
			}

			// Importer-side disk mutation (NOT a transaction) — Modify() BEFORE the transfer (the house
			// Modify-before-mutate template) + dirty so the change persists on save.
			Profile->Modify();

			FAsepriteImporter::TransferHitboxDataToProfile(
				Profile, ParsedData.ExtractedFrameData, ParsedData.Tags, Settings.AssetPrefix, ChosenPolicy,
				/*OutUndeliveredTags*/ nullptr, /*bAllowSoleEntryFallback*/ false);

			Profile->MarkPackageDirty();
		}
	}
	// SeparateAssetsPerLayer mode does not create or reference a profile

	if (Progress.ShouldCancel())
	{
		return Profile; // Return whatever we created so far
	}

	// --- Step 2: Per-layer asset creation ---
	TArray<FCharacterLayer> CreatedLayers;
	int32 TexturesCreated = 0;
	int32 SpritesCreated = 0;

	// Iterate enabled layers in source order so the authored global Layer order is deterministic.
	TArray<int32> SortedImportLayerIndices;
	Settings.LayerImportEnabled.GetKeys(SortedImportLayerIndices);
	SortedImportLayerIndices.Sort();

	// --- TASK-72: normal-map layer pairing (computed once, BEFORE the loop) ---
	// Detect "<base><suffix>" normal layers and pair them to their base art layer. A paired normal layer is EXCLUDED
	// from the visual set (it is materialised only as its base sprites' secondary texture, not as its own
	// FCharacterLayer). Provably a byte-identical no-op when no normal layers exist: BaseToNormal/PairedNormalIndices
	// are both empty and every branch below is skipped. Settings-gated; the lit material is null-safe + project-assigned.
	const UPaper2DPlusSettings* P2DPSettings = UPaper2DPlusSettings::Get();
	const bool bPairNormals = P2DPSettings && P2DPSettings->bPairNormalMapsOnImport;
	TMap<int32, int32> BaseToNormal;        // base layer index -> paired normal layer index
	TSet<int32> PairedNormalIndices;        // normal layer indices consumed as a secondary texture
	UMaterialInterface* ResolvedLitMaterial = nullptr;
	if (bPairNormals)
	{
		TArray<FString> LayerNamesByIndex;
		LayerNamesByIndex.Reserve(ParsedData.Layers.Num());
		for (const FAsepriteLayer& L : ParsedData.Layers)
		{
			LayerNamesByIndex.Add(L.Name);
		}
		DeriveNormalMapPairings(LayerNamesByIndex, P2DPSettings->NormalLayerSuffixes, BaseToNormal, PairedNormalIndices);

		// Honor the import dialog's per-layer enablement: only keep a pairing whose BASE layer is enabled for import.
		// If the user disabled a base but left its normal layer enabled, release the normal back to the visual set so it
		// imports as an ordinary layer rather than silently vanishing (a pairing over a disabled base would otherwise
		// produce nothing — the base sprite is never created, so no normal is attached, and the normal is excluded too).
		{
			TSet<int32> EnabledBaseIndices;
			for (const TPair<int32, bool>& Kv : Settings.LayerImportEnabled)
			{
				if (Kv.Value) { EnabledBaseIndices.Add(Kv.Key); }
			}
			PruneNormalPairingsToEnabledBases(BaseToNormal, PairedNormalIndices, EnabledBaseIndices);
		}

		if (BaseToNormal.Num() > 0)
		{
			ResolvedLitMaterial = P2DPSettings->SpriteLitMaterial.LoadSynchronous();
			if (!ResolvedLitMaterial)
			{
				// One hint per import (not per sprite): the normal texture is attached regardless, but a project-assigned
				// lit material sampling AdditionalTexture0 as Normal is required to actually see depth lighting.
				UE_LOG(LogTemp, Log, TEXT("AsepriteImporter (TASK-72): paired %d normal-map layer(s) but Project Settings > Plugins > Paper2DPlus > 'Sprite Lit Material' is unset. The normal texture is attached to AdditionalTexture0, but assign a lit material sampling AdditionalTexture0 as Normal to see lighting."),
					BaseToNormal.Num());
			}
		}
	}
	int32 NormalSheetsCreated = 0;

	for (const int32 LayerIdx : SortedImportLayerIndices)
	{
		if (!Settings.LayerImportEnabled[LayerIdx]) continue; // Layer not checked for import

		// TASK-72: a layer consumed as a paired normal map is NOT created as its own visual FCharacterLayer.
		if (PairedNormalIndices.Contains(LayerIdx)) continue;

		if (!ParsedData.Layers.IsValidIndex(LayerIdx))
		{
			continue;
		}

		const FAsepriteLayer& Layer = ParsedData.Layers[LayerIdx];

		// Get the full path from hierarchy for folder organization
		FString LayerPath;
		if (ParsedData.LayerHierarchy.IsValidIndex(LayerIdx))
		{
			LayerPath = ParsedData.LayerHierarchy[LayerIdx].FullPath;
		}
		if (LayerPath.IsEmpty())
		{
			LayerPath = Layer.Name;
		}

		// Sanitize layer name for asset naming
		FString SanitizedLayerName = Layer.Name;
		SanitizedLayerName.ReplaceInline(TEXT(" "), TEXT("_"));
		SanitizedLayerName.ReplaceInline(TEXT("/"), TEXT("_"));

		// Build output subpath: OutputPath/AssetPrefix/GroupPath/ for grouped layers
		FString SubPath;
		if (ParsedData.LayerHierarchy.IsValidIndex(LayerIdx))
		{
			const FAsepriteLayerNode& Node = ParsedData.LayerHierarchy[LayerIdx];
			if (Node.ParentIndex >= 0 && ParsedData.LayerHierarchy.IsValidIndex(Node.ParentIndex))
			{
				// Build the group path from parent hierarchy
				FString GroupPath;
				int32 CurrentParent = Node.ParentIndex;
				while (CurrentParent >= 0 && ParsedData.LayerHierarchy.IsValidIndex(CurrentParent))
				{
					const FAsepriteLayerNode& ParentNode = ParsedData.LayerHierarchy[CurrentParent];
					FString ParentName;
					if (ParsedData.Layers.IsValidIndex(ParentNode.LayerIndex))
					{
						ParentName = ParsedData.Layers[ParentNode.LayerIndex].Name;
						ParentName.ReplaceInline(TEXT(" "), TEXT("_"));
					}
					if (!ParentName.IsEmpty())
					{
						GroupPath = GroupPath.IsEmpty() ? ParentName : (ParentName / GroupPath);
					}
					CurrentParent = ParentNode.ParentIndex;
				}
				SubPath = Settings.OutputPath / Settings.AssetPrefix / GroupPath;
			}
			else
			{
				SubPath = Settings.OutputPath / Settings.AssetPrefix;
			}
		}
		else
		{
			SubPath = Settings.OutputPath / Settings.AssetPrefix;
		}

		FString TextureName = Settings.AssetPrefix + TEXT("_") + SanitizedLayerName + TEXT("_Sheet");
		FString SpritePrefix = Settings.AssetPrefix + TEXT("_") + SanitizedLayerName;

		Progress.EnterProgressFrame(1.0f, FText::Format(
			LOCTEXT("CreatingLayerAssets", "Creating assets for layer: {0}"),
			FText::FromString(Layer.Name)));

		// Create the per-layer sprite sheet texture
		const TArray<TArray<FColor>>* LayerBuffers = PerLayerBuffers.Find(LayerIdx);
		if (!LayerBuffers || LayerBuffers->Num() == 0)
		{
			continue;
		}

		UTexture2D* LayerTexture = CreatePerLayerSpriteSheetTexture(
			*LayerBuffers, ParsedData.Width, ParsedData.Height, SubPath, TextureName);

		if (!LayerTexture)
		{
			UE_LOG(LogTemp, Warning, TEXT("ImportAsLayeredAsset: Failed to create texture for layer '%s'"), *Layer.Name);
			continue;
		}
		TexturesCreated++;

		// Create per-layer sprites
		TArray<FCharacterLayerAnimationMapping> Mappings = CreatePerLayerSprites(
			LayerTexture, ParsedData, SubPath, SpritePrefix);

		for (const auto& Mapping : Mappings)
		{
			SpritesCreated += Mapping.Sprites.Num();
		}

		// --- TASK-72: pair a normal-map layer onto this base layer's sprites ---
		// If this base layer has a paired normal layer, composite the NORMAL layer's pixels into a normal sheet (same
		// grid/frame dims as the base, retagged TC_Normalmap / SRGB-off / WorldNormalMap) and attach it to each of this
		// layer's generated base sprites' secondary texture slot (AdditionalTexture0), with the null-safe lit material.
		if (const int32* NormalLayerIdxPtr = BaseToNormal.Find(LayerIdx))
		{
			const TArray<TArray<FColor>>* NormalBuffers = PerLayerBuffers.Find(*NormalLayerIdxPtr);
			if (NormalBuffers && NormalBuffers->Num() > 0)
			{
				const FString NormalTextureName = TextureName + TEXT("_N");
				UTexture2D* NormalTexture = CreateNormalMapSheetTexture(
					*NormalBuffers, ParsedData.Width, ParsedData.Height, SubPath, NormalTextureName);
				if (NormalTexture)
				{
					// Gather the unique base sprites from the mappings (CreatePerLayerSprites shares one sprite per frame
					// across animation mappings, so dedupe to avoid re-touching the same sprite per tag).
					TArray<UPaperSprite*> BaseSprites;
					TSet<UPaperSprite*> SeenSprites;
					for (const FCharacterLayerAnimationMapping& NormalSpriteMapping : Mappings)
					{
						for (const TSoftObjectPtr<UPaperSprite>& SpriteRef : NormalSpriteMapping.Sprites)
						{
							UPaperSprite* Sprite = SpriteRef.LoadSynchronous(); // freshly created, resolves immediately
							if (Sprite && !SeenSprites.Contains(Sprite))
							{
								SeenSprites.Add(Sprite);
								BaseSprites.Add(Sprite);
							}
						}
					}

					int32 SpritesTouched = 0;
					AttachNormalMapToSprites(BaseSprites, NormalTexture, ResolvedLitMaterial, SpritesTouched);
					NormalSheetsCreated++;
					UE_LOG(LogTemp, Log, TEXT("AsepriteImporter (TASK-72): paired normal layer '%s' onto base layer '%s' (%d sprite(s), lit material: %s)."),
						ParsedData.Layers.IsValidIndex(*NormalLayerIdxPtr) ? *ParsedData.Layers[*NormalLayerIdxPtr].Name : TEXT("?"),
						*Layer.Name, SpritesTouched, ResolvedLitMaterial ? TEXT("set") : TEXT("none"));
				}
			}
		}

		// Build the FCharacterLayer
		FCharacterLayer CharLayer;
		CharLayer.LayerName = LayerPath;
		CharLayer.SourceTexture = LayerTexture;
		CharLayer.AnimationSprites = MoveTemp(Mappings);

		CreatedLayers.Add(MoveTemp(CharLayer));

		if (Progress.ShouldCancel())
		{
			break;
		}
	}

	if (NormalSheetsCreated > 0)
	{
		UE_LOG(LogTemp, Log, TEXT("AsepriteImporter (TASK-72): generated %d paired normal-map sheet(s)."), NormalSheetsCreated);
	}

	// --- Step 3: Create output assets ---
	Progress.EnterProgressFrame(1.0f, LOCTEXT("CreatingOutputAssets", "Creating output assets..."));

	UObject* PrimaryOutput = nullptr;

	if (Settings.ImportMode == EAsepriteImportMode::SeparateAssetsPerLayer)
	{
		// Separate mode: create a flipbook per layer per tag (no CharacterLayerAsset)
		int32 FlipbooksCreated = 0;
		UPaperFlipbook* FirstFlipbook = nullptr;

		for (const FCharacterLayer& CharLayer : CreatedLayers)
		{
			for (const FCharacterLayerAnimationMapping& Mapping : CharLayer.AnimationSprites)
			{
				if (Mapping.Sprites.Num() == 0) continue;

				FString SanitizedLayerName = CharLayer.LayerName;
				SanitizedLayerName.ReplaceInline(TEXT("/"), TEXT("_"));
				SanitizedLayerName.ReplaceInline(TEXT(" "), TEXT("_"));
				FString FlipbookName = Settings.AssetPrefix + TEXT("_") + SanitizedLayerName + TEXT("_") + Mapping.AnimationName;
				FString FlipbookPackagePath = Settings.OutputPath / Settings.AssetPrefix / FlipbookName;
				UPackage* FlipbookPackage = CreatePackage(*FlipbookPackagePath);
				if (!FlipbookPackage) continue;

				bool bCreatedFlipbook = false;
				UPaperFlipbook* Flipbook = FindOrCreateAssetInPackage<UPaperFlipbook>(
					FlipbookPackage, FlipbookName, bCreatedFlipbook);

				if (Flipbook)
				{
					// Per-frame durations come from the matching tag's frame range (the per-layer Sprites
					// array carries no source-frame back-link, so the tag range is authoritative).
					TArray<int32> TagDurationsMs;
					for (const FAsepriteTag& Tag : ParsedData.Tags)
					{
						if (Tag.Name == Mapping.AnimationName)
						{
							const int32 From = FMath::Max(0, Tag.FromFrame);
							const int32 To = FMath::Min(Tag.ToFrame, ParsedData.Frames.Num() - 1);
							for (int32 F = From; F <= To; ++F)
							{
								TagDurationsMs.Add(ParsedData.Frames[F].Duration);
							}
							break;
						}
					}

					float Fps = 15.0f;
					TArray<int32> FrameRuns;
					ComputeFrameTiming(TagDurationsMs, EAsepriteTimingBase::GcdExact, 15.0f, Fps, FrameRuns);

					// Per-frame FrameRun by position requires the duration list (full tag range) to line up
					// 1:1 with Mapping.Sprites. That holds normally and when a sprite merely fails to LOAD
					// at consume time (the entry is still present). It does NOT hold if CreatePerLayerSprites
					// COMPACTED out a frame whose sprite failed to CREATE (rare) — then the lengths differ
					// and per-index runs would shift, so fall back to uniform FrameRun=1.
					const bool bRunsAligned = (FrameRuns.Num() == Mapping.Sprites.Num());
					{
						FScopedFlipbookMutator Mutator(Flipbook);
						Mutator.FramesPerSecond = Fps;
						Mutator.KeyFrames.Empty();
						int32 KeyIdx = 0;
						for (const TSoftObjectPtr<UPaperSprite>& SpriteRef : Mapping.Sprites)
						{
							UPaperSprite* Sprite = SpriteRef.LoadSynchronous();
							if (Sprite)
							{
								FPaperFlipbookKeyFrame KeyFrame;
								KeyFrame.Sprite = Sprite;
								KeyFrame.FrameRun = (bRunsAligned && FrameRuns.IsValidIndex(KeyIdx)) ? FrameRuns[KeyIdx] : 1;
								Mutator.KeyFrames.Add(KeyFrame);
							}
							++KeyIdx;
						}
					}

					FlipbookPackage->MarkPackageDirty();
					if (bCreatedFlipbook)
					{
						FAssetRegistryModule::AssetCreated(Flipbook);
					}
					FlipbooksCreated++;
					if (!FirstFlipbook)
					{
						FirstFlipbook = Flipbook;
					}
				}
			}
		}

		PrimaryOutput = FirstFlipbook;

		FString Summary = FString::Printf(
			TEXT("Separated %d layers into %d flipbooks (%d textures, %d sprites)"),
			CreatedLayers.Num(), FlipbooksCreated, TexturesCreated, SpritesCreated);
		FNotificationInfo Info(FText::FromString(Summary));
		Info.ExpireDuration = 5.0f;
		Info.bUseSuccessFailIcons = true;
		FSlateNotificationManager::Get().AddNotification(Info);
		UE_LOG(LogTemp, Log, TEXT("ImportAsLayeredAsset: %s"), *Summary);
	}
	else
	{
		// Layer Asset mode: create CharacterLayerAsset
		UPaper2DPlusCharacterLayerAsset* LayerAsset = nullptr;
		FString ReimportSummary; // set on the existing-asset reimport path; appended to the user notification below

		if (CreatedLayers.Num() > 0)
		{
			FString LayerAssetName = Settings.AssetPrefix + TEXT("_Layers");
			FString LayerAssetPackageName = Settings.OutputPath / LayerAssetName;
			UPackage* LayerAssetPackage = CreatePackage(*LayerAssetPackageName);

			if (LayerAssetPackage)
			{
				bool bCreatedLayerAsset = false;
				LayerAsset = FindOrCreateAssetInPackage<UPaper2DPlusCharacterLayerAsset>(
					LayerAssetPackage, LayerAssetName, bCreatedLayerAsset);

					if (LayerAsset)
					{
					if (bCreatedLayerAsset || LayerAsset->DisplayName.IsEmpty())
					{
						LayerAsset->DisplayName = Settings.AssetPrefix;
					}
					LayerAsset->SourceAseFilePath = Settings.SourceFilePath;

					// Existing curated assets keep their relationship. Reimport may refresh art and the external
					// source path, but it never silently re-points the canonical Profile/bake ownership graph.
						if (Profile && (bCreatedLayerAsset || LayerAsset->BaseProfile.IsNull()))
						{
							LayerAsset->BaseProfile = Profile;
						}

						bool bLayerDataChanged = false;
						if (bCreatedLayerAsset || LayerAsset->Layers.IsEmpty())
						{
							LayerAsset->Layers = MoveTemp(CreatedLayers);
							LayerAsset->EnsureLayerAuthoringIdentity();
							FCharacterLayerAppearancePreset DefaultAppearance;
							DefaultAppearance.PresetId = FGuid::NewGuid();
							DefaultAppearance.DisplayName = TEXT("Default");
							for (const FCharacterLayer& Layer : LayerAsset->Layers)
							{
								DefaultAppearance.ActiveLayerIds.Add(Layer.LayerId);
							}
							LayerAsset->AppearancePresets = { DefaultAppearance };
							LayerAsset->DefaultAppearancePresetId = DefaultAppearance.PresetId;
							LayerAsset->UsageMode = ECharacterLayerUsageMode::RuntimeCustomizable;
							LayerAsset->LayerSchemaVersion = UPaper2DPlusCharacterLayerAsset::GenericLayerSchemaVersion;
							bLayerDataChanged = true;
						}
						else
						{
							// Reimport reconciles art by authored layer name while preserving stable identity, global order,
							// Exclusive Group membership, preset membership, placement, and layer-local gameplay.
							for (FCharacterLayer& Incoming : CreatedLayers)
							{
								FCharacterLayer* Existing = LayerAsset->Layers.FindByPredicate([&Incoming](const FCharacterLayer& Layer)
								{
									return Layer.LayerName.Equals(Incoming.LayerName, ESearchCase::IgnoreCase);
								});
								if (Existing)
								{
									Existing->SourceTexture = Incoming.SourceTexture;
									Existing->AnimationSprites = MoveTemp(Incoming.AnimationSprites);
								}
								else
								{
									LayerAsset->Layers.Add(MoveTemp(Incoming));
								}
								bLayerDataChanged = true;
							}
							LayerAsset->EnsureLayerAuthoringIdentity();
							ReimportSummary = FString::Printf(TEXT("Reimported %d generic layer(s)"), CreatedLayers.Num());
						}
						if (bLayerDataChanged && !FPackageName::IsTempPackage(LayerAssetPackageName))
						{
							LayerAssetPackage->MarkPackageDirty();
							if (bCreatedLayerAsset)
							{
								FAssetRegistryModule::AssetCreated(LayerAsset);
							}
						}

					}
			}
		}

		// TASK-71 (live-reimport last-mile): a layered import sets LayerAsset->SourceAseFilePath, but the
		// TextureWatcherService only watches the external directories it discovered at startup (or the last
		// RefreshAssetMapping). Without refreshing here, a BRAND-NEW external .ase imported this session is not
		// watched until the next editor restart (in-Content .ase files are already covered by the recursive
		// Content watch). RefreshAssetMapping() rebuilds the source-file->asset maps AND registers the new .ase's
		// parent directory, closing the loop. Skip /Temp targets (transient, nothing on disk to watch -- mirrors
		// the FIX 4 guards above) and skip when the watcher isn't initialized (e.g. headless/automation).
		if (LayerAsset && FTextureWatcherService::Get().IsInitialized()
			&& !FPackageName::IsTempPackage(LayerAsset->GetOutermost()->GetName()))
		{
			FTextureWatcherService::Get().RefreshAssetMapping();
		}

		PrimaryOutput = LayerAsset ? static_cast<UObject*>(LayerAsset) : static_cast<UObject*>(Profile);

		FString Summary;
		if (LayerAsset)
		{
			Summary = FString::Printf(
				TEXT("Imported %d layers (%d textures, %d sprites)"),
				LayerAsset->Layers.Num(), TexturesCreated, SpritesCreated);
			if (NormalSheetsCreated > 0)
			{
				// Surface normal-map consumption so a layer folded into a base as its normal map is never silently
				// removed from the visible set (review finding: "_n" is a common suffix; consumption must be visible).
				Summary += FString::Printf(TEXT(", %d normal map(s) paired"), NormalSheetsCreated);
			}
			if (Profile)
			{
				Summary += FString::Printf(TEXT(", profile: %s"), *Profile->GetName());
			}
			if (!ReimportSummary.IsEmpty())
			{
				Summary = ReimportSummary;
			}
		}
		else if (Profile)
		{
			Summary = FString::Printf(TEXT("Created profile: %s (no visual layers selected)"), *Profile->GetName());
		}
		else
		{
			Summary = TEXT("Aseprite import completed with no assets created.");
		}

		FNotificationInfo Info(FText::FromString(Summary));
		Info.ExpireDuration = 5.0f;
		Info.bUseSuccessFailIcons = true;
		FSlateNotificationManager::Get().AddNotification(Info);
		UE_LOG(LogTemp, Log, TEXT("ImportAsLayeredAsset: %s"), *Summary);
	}

	// --- Step 4: Finalize ---
	Progress.EnterProgressFrame(1.0f, LOCTEXT("FinalizingImport", "Finalizing import..."));

	return PrimaryOutput;
}

void FAsepriteImporter::RegisterMenus()
{
	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateLambda([]()
	{
		// Add to Tools > Paper2DPlus menu
		UToolMenu* ToolsMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Tools");
		if (ToolsMenu)
		{
			FToolMenuSection& Section = ToolsMenu->FindOrAddSection("Paper2DPlus");
			Section.AddMenuEntry(
				"ImportAsepriteFile",
				LOCTEXT("ImportAseprite", "Import Aseprite File..."),
				LOCTEXT("ImportAsepriteTooltip", "Import an Aseprite (.ase/.aseprite) file as Paper2D sprites and flipbooks"),
				FSlateIcon(FAppStyle::Get().GetStyleSetName(), "ClassIcon.PaperFlipbook"),
				FUIAction(FExecuteAction::CreateStatic(&FAsepriteImporter::ShowImportDialog))
			);
		}

		// Aseprite import is registered in the Paper2D+ Actions submenu by FSpriteExtractorActions::RegisterMenus()
	}));
}

void FAsepriteImporter::UnregisterMenus()
{
	// Menus are automatically cleaned up when the module shuts down
}

#undef LOCTEXT_NAMESPACE
