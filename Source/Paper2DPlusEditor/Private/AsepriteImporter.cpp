// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "AsepriteImporter.h"
#include "AsepriteStructuralDiff.h" // TASK-189: per-animation merge + structural-diff content hashes
#include "HitboxConflictDialog.h"
#include "Paper2DPlusCharacterLayerAsset.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Paper2DPlusSettings.h" // TASK-72: normal-map pairing convention + sprite-lit material slot
#include "SpriteExtractionUtils.h"
#include "Paper2DPlusEditorCompat.h" // PAPER2DPLUS_RENAME_TO_TRANSIENT_FLAGS (UE5.8 REN_ForceNoResetLoaders deprecation)
#include "SpriteEditorOnlyTypes.h" // TASK-192 U5: FSpriteGeometryCollection for the tight-bounds gate
#include "UObject/UnrealType.h" // TASK-192 U5: reflection read of the protected sprite geometry
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
#include "IAssetTools.h"       // TASK-189 U4: FAssetRenameData / RenameAssets for on-disk renames
#include "Logging/MessageLog.h" // TASK-189 U4 (R13): non-modal, auditable diff reporting
#include "MessageLogModule.h"
#include "Misc/PackageName.h" // FPackageName::IsTempPackage (WS3-3D FIX 4)
#include "BulkSpriteExtractorWindow.h" // TASK-189: .ase files load as bulk sources
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
#include "Misc/SecureHash.h" // TASK-183: FMD5Hash content stamp for the watcher's startup reconcile
#include "HAL/FileManager.h" // TASK-183: copy-source-into-project
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
	// Hitbox prefixes come from project settings (Aseprite Import → Hitbox Layer Name Prefixes) so artist
	// conventions like "HitBox" classify as data without renaming source files. An empty/blank settings
	// list falls back to the compiled defaults — an empty list must never silently bake data layers into
	// art. The "socket_" convention is fixed and checked first so a configured prefix cannot shadow it.
	TArray<FPaper2DPlusHitboxLayerPrefix> HitboxPrefixes;
	if (const UPaper2DPlusSettings* ClassifySettings = UPaper2DPlusSettings::Get())
	{
		for (const FPaper2DPlusHitboxLayerPrefix& Row : ClassifySettings->HitboxLayerNamePrefixes)
		{
			if (!Row.Prefix.TrimStartAndEnd().IsEmpty())
			{
				HitboxPrefixes.Add(Row);
			}
		}
	}
	if (HitboxPrefixes.Num() == 0)
	{
		HitboxPrefixes.Add(FPaper2DPlusHitboxLayerPrefix(TEXT("attack"), EHitboxType::Attack));
		HitboxPrefixes.Add(FPaper2DPlusHitboxLayerPrefix(TEXT("hurtbox"), EHitboxType::Hurtbox));
		HitboxPrefixes.Add(FPaper2DPlusHitboxLayerPrefix(TEXT("hitbox"), EHitboxType::Attack));
	}

	for (int32 i = 0; i < OutData.Layers.Num(); i++)
	{
		const FAsepriteLayer& Layer = OutData.Layers[i];
		FString LowerName = Layer.Name.ToLower();

		if (LowerName.StartsWith(TEXT("socket_")))
		{
			FAsepriteHitboxLayer HL;
			HL.LayerName = Layer.Name;
			HL.bIsSocket = true;
			HL.SocketName = Layer.Name.Mid(7); // Strip "socket_" prefix
			HL.LayerIndex = i;
			OutData.HitboxLayers.Add(HL);
			continue;
		}

		for (const FPaper2DPlusHitboxLayerPrefix& Row : HitboxPrefixes)
		{
			if (LowerName.StartsWith(Row.Prefix.ToLower()))
			{
				FAsepriteHitboxLayer HL;
				HL.LayerName = Layer.Name;
				HL.HitboxType = Row.Type;
				HL.LayerIndex = i;
				OutData.HitboxLayers.Add(HL);
				break;
			}
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
					// MUST read OutData.AllFrameCels here: the local AllFrameCels was MoveTemp'd into
					// OutData above, so the local is empty and every lookup through it silently fails —
					// which imported all linked ("hold") frames as blank.
					int32 LinkedFrameIdx = Cel.LinkedFrame;
					if (LinkedFrameIdx >= 0 && LinkedFrameIdx < OutData.AllFrameCels.Num())
					{
						// Find the source cel in the linked frame with the same layer index.
						// Linked cels reuse source pixel content, but placement must come from
						// the current frame cel's X/Y (Cel.X/Cel.Y), not the source cel's X/Y.
						for (const FAsepriteCelData& LinkedCel : OutData.AllFrameCels[LinkedFrameIdx])
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
// Import cost instrumentation (TASK-192 U2)
// ============================================

namespace
{
	/** The one active report. Imports are game-thread synchronous, so a bare pointer suffices. */
	FAsepriteImportCostReport* GActiveAseImportCostReport = nullptr;
}

FString FAsepriteImportCostReport::ToSummaryString() const
{
	return FString::Printf(
		TEXT("sheets %d written/%d skipped, sprites %d/%d, flipbooks %d/%d, profile entries %d/%d, packages dirtied %d | parse %.3fs, composite %.3fs, texture build %.3fs, sprites %.3fs, flipbooks %.3fs, profile %.3fs, total %.3fs"),
		SheetsWritten, SheetsSkipped,
		SpritesWritten, SpritesSkipped,
		FlipbooksWritten, FlipbooksSkipped,
		ProfileEntriesWritten, ProfileEntriesSkipped,
		PackagesDirtied,
		GetPhaseSeconds(EAsepriteImportCostPhase::Parse),
		GetPhaseSeconds(EAsepriteImportCostPhase::Composite),
		GetPhaseSeconds(EAsepriteImportCostPhase::TextureBuild),
		GetPhaseSeconds(EAsepriteImportCostPhase::Sprites),
		GetPhaseSeconds(EAsepriteImportCostPhase::Flipbooks),
		GetPhaseSeconds(EAsepriteImportCostPhase::Profile),
		TotalSeconds);
}

FAsepriteImportCostScope::FAsepriteImportCostScope()
{
	check(IsInGameThread());
	if (GActiveAseImportCostReport == nullptr)
	{
		bOwner = true;
		GActiveAseImportCostReport = &Report;
		StartSeconds = FPlatformTime::Seconds();
		// Count every package the import dirties, at the engine chokepoint rather than per call site:
		// nested helpers, the diff apply, and the registry all mark packages, and the whole point of
		// this number is that nothing about it is estimated. The event fires on every mark (even
		// already-dirty), so the unique set is complete.
		PackageMarkedDirtyHandle = UPackage::PackageMarkedDirtyEvent.AddLambda(
			[this](UPackage* Package, bool /*bWasDirty*/)
		{
			if (Package)
			{
				DirtiedPackages.Add(Package->GetFName());
				// Stamped eagerly so GetReport() is accurate while the scope is still open — the
				// tests (and any mid-import diagnostics) read it before the destructor runs.
				Report.PackagesDirtied = DirtiedPackages.Num();
			}
		});
	}
}

FAsepriteImportCostScope::~FAsepriteImportCostScope()
{
	if (!bOwner)
	{
		return;
	}
	UPackage::PackageMarkedDirtyEvent.Remove(PackageMarkedDirtyHandle);
	Report.PackagesDirtied = DirtiedPackages.Num();
	Report.TotalSeconds = FPlatformTime::Seconds() - StartSeconds;
	GActiveAseImportCostReport = nullptr;
	UE_LOG(LogTemp, Log, TEXT("Aseprite import cost (TASK-192): %s"), *Report.ToSummaryString());
}

const FAsepriteImportCostReport& FAsepriteImportCostScope::GetReport() const
{
	if (!bOwner && GActiveAseImportCostReport)
	{
		return *GActiveAseImportCostReport;
	}
	return Report;
}

FAsepriteImportCostReport* FAsepriteImportCostScope::GetActive()
{
	return GActiveAseImportCostReport;
}

FAsepriteImportCostPhaseTimer::FAsepriteImportCostPhaseTimer(const EAsepriteImportCostPhase InPhase)
	: Phase(InPhase)
	, StartSeconds(FPlatformTime::Seconds())
{
}

FAsepriteImportCostPhaseTimer::~FAsepriteImportCostPhaseTimer()
{
	if (GActiveAseImportCostReport)
	{
		GActiveAseImportCostReport->PhaseSeconds[static_cast<int32>(Phase)]
			+= FPlatformTime::Seconds() - StartSeconds;
	}
}

namespace AsepriteImportCost
{
	void AddSheets(const int32 Count, const bool bWritten)
	{
		if (FAsepriteImportCostReport* Active = GActiveAseImportCostReport)
		{
			(bWritten ? Active->SheetsWritten : Active->SheetsSkipped) += Count;
		}
	}

	void AddSprites(const int32 Count, const bool bWritten)
	{
		if (FAsepriteImportCostReport* Active = GActiveAseImportCostReport)
		{
			(bWritten ? Active->SpritesWritten : Active->SpritesSkipped) += Count;
		}
	}

	void AddFlipbooks(const int32 Count, const bool bWritten)
	{
		if (FAsepriteImportCostReport* Active = GActiveAseImportCostReport)
		{
			(bWritten ? Active->FlipbooksWritten : Active->FlipbooksSkipped) += Count;
		}
	}

	void AddProfileEntries(const int32 Count, const bool bWritten)
	{
		if (FAsepriteImportCostReport* Active = GActiveAseImportCostReport)
		{
			(bWritten ? Active->ProfileEntriesWritten : Active->ProfileEntriesSkipped) += Count;
		}
	}
}

// ============================================
// Incremental sprite gating helpers (TASK-192 U5)
// ============================================

namespace
{
	FSpriteGeometryCollection* AseIncrSprite_GetGeometry(UPaperSprite& Sprite, const TCHAR* Name)
	{
		FStructProperty* Property = FindFProperty<FStructProperty>(UPaperSprite::StaticClass(), FName(Name));
		return Property ? Property->ContainerPtrToValuePtr<FSpriteGeometryCollection>(&Sprite) : nullptr;
	}

	/**
	 * True when the sprite's pixel-derived geometry provably does not move for the new cell buffer.
	 * SourceBoundingBox / FullyCustom never read pixels, so they pass; TightBoundingBox compares the
	 * stored box against one derived from the buffer in hand (KTD6); ShrinkWrapped / Diced — or any
	 * unexpected shape inventory — cannot be proven cheaply and reads as "moved", because a wrong
	 * skip is worse than a slow reimport.
	 */
	bool AseIncrSprite_PixelGeometryUnchanged(
		UPaperSprite& Sprite, const TArray<FColor>& CellPixels, const int32 CellW, const int32 CellH,
		const FIntPoint& CellMin, const FString& PackageName)
	{
		FSpriteGeometryCollection* Render = AseIncrSprite_GetGeometry(Sprite, TEXT("RenderGeometry"));
		FSpriteGeometryCollection* Collision = AseIncrSprite_GetGeometry(Sprite, TEXT("CollisionGeometry"));
		if (!Render || !Collision)
		{
			return false;
		}

		// 0 = pixels never read; 1 = comparable tight box (outputs filled); -1 = unprovable.
		const auto ClassifyGeometry = [&](FSpriteGeometryCollection& Geometry,
			FVector2D& OutStoredCenter, FVector2D& OutStoredSize,
			FVector2D& OutDerivedCenter, FVector2D& OutDerivedSize) -> int32
		{
			switch (Geometry.GeometryType)
			{
			case ESpritePolygonMode::SourceBoundingBox:
			case ESpritePolygonMode::FullyCustom:
				return 0;
			case ESpritePolygonMode::TightBoundingBox:
				if (Geometry.Shapes.Num() == 1 && Geometry.Shapes[0].ShapeType == ESpriteShapeType::Box)
				{
					OutStoredCenter = Geometry.Shapes[0].BoxPosition;
					OutStoredSize = Geometry.Shapes[0].BoxSize;
					FAsepriteIncrementalWrite::DeriveTightBoxForCell(
						CellPixels, CellW, CellH, CellMin, Geometry.AlphaThreshold,
						OutDerivedCenter, OutDerivedSize);
					return 1;
				}
				return -1;
			default:
				return -1;
			}
		};

		FVector2D RenderStoredC = FVector2D::ZeroVector, RenderStoredS = FVector2D::ZeroVector;
		FVector2D RenderDerivedC = FVector2D::ZeroVector, RenderDerivedS = FVector2D::ZeroVector;
		FVector2D CollisionStoredC = FVector2D::ZeroVector, CollisionStoredS = FVector2D::ZeroVector;
		FVector2D CollisionDerivedC = FVector2D::ZeroVector, CollisionDerivedS = FVector2D::ZeroVector;
		const int32 RenderKind = ClassifyGeometry(
			*Render, RenderStoredC, RenderStoredS, RenderDerivedC, RenderDerivedS);
		const int32 CollisionKind = ClassifyGeometry(
			*Collision, CollisionStoredC, CollisionStoredS, CollisionDerivedC, CollisionDerivedS);
		if (RenderKind < 0 || CollisionKind < 0)
		{
			return false;
		}
		if (RenderKind == 0 && CollisionKind == 0)
		{
			return true;
		}
		return !FAsepriteIncrementalWrite::ShouldWriteSpriteForDerivedBounds(
			PackageName,
			RenderStoredC, RenderStoredS, RenderDerivedC, RenderDerivedS,
			CollisionStoredC, CollisionStoredS, CollisionDerivedC, CollisionDerivedS).ShouldWrite();
	}
}

// ============================================
// CreatePerLayerSprites
// ============================================

namespace
{
	/**
	 * The ONE gated per-frame sprite reconcile (TASK-192 U5/U6), shared by the per-layer and the
	 * flat creators so their rules cannot drift. Fills Outcome with one ref per frame (identity
	 * always lands, written or skipped), the set of fully re-initialized frames, and the counters.
	 */
	void AseIncr_ReconcileSpriteSet(
		UTexture2D* SpriteSheet,
		const FAsepriteParsedData& Data,
		const FString& OutputPath,
		const FString& AssetPrefix,
		const FSpriteSheetGrid& Grid,
		const FAseSpriteWritePlan* WritePlan,
		FAseSpriteWriteOutcome& Outcome)
	{
		const int32 FrameCount = Data.Frames.Num();
		const int32 FrameW = Data.Width;
		const int32 FrameH = Data.Height;

		Outcome.SpriteRefs.Reset();
		Outcome.SpriteRefs.SetNum(FrameCount);
		Outcome.FullyInitializedFrames.Reset();
		Outcome.SpritesWritten = 0;
		Outcome.SpritesSkipped = 0;

		// The owning sheet resolves lazily: a skipped layer's sheet is not resident, and loading it
		// for nothing would defeat the gate. Only a sprite that genuinely needs (re)initializing
		// pays it.
		UTexture2D* ResolvedSheet = SpriteSheet;
		const auto ResolveSheet = [&]() -> UTexture2D*
		{
			if (!ResolvedSheet && WritePlan && !WritePlan->SheetObjectPath.IsEmpty())
			{
				ResolvedSheet = LoadObject<UTexture2D>(nullptr, *WritePlan->SheetObjectPath);
			}
			return ResolvedSheet;
		};

		for (int32 FrameIdx = 0; FrameIdx < FrameCount; FrameIdx++)
		{
			const FString SpriteName = FString::Printf(TEXT("%s_%02d"), *AssetPrefix, FrameIdx);
			const FString PackageName = OutputPath / SpriteName;
			const FString ObjectPath = PackageName + TEXT(".") + SpriteName;

			FAseWriteDecision Decision;
			if (WritePlan)
			{
				Decision = FAsepriteIncrementalWrite::ShouldWriteSpritePayload(
					PackageName, WritePlan->SheetDecision, WritePlan->bSheetObjectRecreated,
					WritePlan->StampedGrid, WritePlan->CurrentGrid, WritePlan->bForceFullReimport);
			}
			else
			{
				Decision.Verdict = EAseWriteVerdict::Create;
				Decision.Reason = TEXT("full rebuild (no incremental plan)");
			}

			// Mapping identity always lands, written or skipped: the fresh-import branch replaces
			// `Layers` wholesale, so a mapping built only for written sprites would strand the rest.
			Outcome.SpriteRefs[FrameIdx] = TSoftObjectPtr<UPaperSprite>(FSoftObjectPath(ObjectPath));

			if (Decision.Verdict == EAseWriteVerdict::SkipPayload)
			{
				Outcome.SpritesSkipped++;
				AsepriteImportCost::AddSprites(1, /*bWritten*/ false);
				continue;
			}

			// Candidate refinement (pixel-changed sheet, same grid): load the sprite and prove its
			// bounds unmoved before paying any write. Everything else takes the full initialize.
			if (WritePlan && Decision.Verdict == EAseWriteVerdict::WriteInputsChanged)
			{
				UPaperSprite* Existing = LoadObject<UPaperSprite>(nullptr, *ObjectPath);
				if (Existing)
				{
					// R8 self-resolves through the load: a cold sprite import-resolves onto whatever
					// object now inhabits the sheet path, so only a genuinely divergent pointer
					// needs the full re-point.
					const bool bNeedsRepoint =
						ResolveSheet() != nullptr && Existing->GetSourceTexture() != ResolvedSheet;
					if (!bNeedsRepoint)
					{
						const bool bProvenUnchanged =
							WritePlan->FrameBuffers && WritePlan->FrameBuffers->IsValidIndex(FrameIdx)
							&& AseIncrSprite_PixelGeometryUnchanged(
								*Existing, (*WritePlan->FrameBuffers)[FrameIdx], FrameW, FrameH,
								Grid.GetCellRect(FrameIdx).Min, PackageName);
						if (bProvenUnchanged)
						{
							Outcome.SpriteRefs[FrameIdx] = Existing;
							Outcome.SpritesSkipped++;
							AsepriteImportCost::AddSprites(1, /*bWritten*/ false);
							continue;
						}

						// Bounds moved (or could not be proven): the narrow write — RebuildData on
						// the write path only (KTD6); geometry types, collision domain, pivot, and
						// PPU are read by the rebuild, never assigned (R7).
						Existing->RebuildData();
						if (UPackage* Package = Existing->GetOutermost();
							Package && !FPackageName::IsTempPackage(Package->GetName()))
						{
							Package->MarkPackageDirty();
						}
						Outcome.SpriteRefs[FrameIdx] = Existing;
						Outcome.SpritesWritten++;
						AsepriteImportCost::AddSprites(1, /*bWritten*/ true);
						continue;
					}
					// A divergent texture pointer falls through to the full initialize below.
				}
				// A null load (registry row without a loadable package) also falls through: the
				// full initialize is the conservative repair.
			}

			// FULL initialize: create / grid change / forced / re-point / unresolvable candidate.
			UPackage* Package = CreatePackage(*PackageName);
			if (!Package)
			{
				Outcome.SpriteRefs[FrameIdx].Reset();
				continue;
			}
			bool bCreatedSprite = false;
			UPaperSprite* Sprite = FindOrCreateAssetInPackage<UPaperSprite>(Package, SpriteName, bCreatedSprite);
			if (!Sprite)
			{
				Outcome.SpriteRefs[FrameIdx].Reset();
				continue;
			}
			UTexture2D* SheetForInit = ResolveSheet();
			if (!SheetForInit)
			{
				UE_LOG(LogTemp, Warning, TEXT("AsepriteImporter: no sheet texture available to initialize sprite '%s'"), *SpriteName);
				Outcome.SpriteRefs[FrameIdx].Reset();
				continue;
			}

			FSpriteAssetInitParameters InitParams;
			InitParams.Texture = SheetForInit;
			InitParams.Offset = Grid.GetCellRect(FrameIdx).Min;
			InitParams.Dimension = FIntPoint(FrameW, FrameH);
			if (bCreatedSprite)
			{
				// R7: PPU is assigned only at birth; a reimport reads and preserves whatever the
				// designer set since.
				InitParams.SetPixelsPerUnrealUnit(1.0f);
			}
			Sprite->InitializeSprite(InitParams);

			// WS3-3D FIX 4: skip MarkPackageDirty + AssetCreated for /Temp packages (never saved or
			// submitted; the SCC async git-status against a /Temp path fatally errors).
			if (!FPackageName::IsTempPackage(Package->GetName()))
			{
				Package->MarkPackageDirty();
				if (bCreatedSprite)
				{
					FAssetRegistryModule::AssetCreated(Sprite);
				}
			}

			Outcome.SpriteRefs[FrameIdx] = Sprite;
			Outcome.FullyInitializedFrames.Add(FrameIdx);
			Outcome.SpritesWritten++;
			AsepriteImportCost::AddSprites(1, /*bWritten*/ true);
		}
	}
}

// The packed-sheet writer is THE one sheet writer (TASK-192 U9); declared ahead of the composited
// entry point that now routes through it. Definition below, beside its historical home.
enum class EPaper2DPlusPackedSheetUsage : uint8
{
	Color,
	TangentNormal
};

static UTexture2D* Paper2DPlus_CreatePackedSpriteSheetTexture(
	const TArray<TArray<FColor>>& FrameBuffers, int32 FrameWidth, int32 FrameHeight,
	const FString& OutputPath, const FString& AssetName, EPaper2DPlusPackedSheetUsage Usage,
	bool* bOutCreatedTexture = nullptr);

// ============================================
// CreateSpriteSheetTexture
// ============================================

UTexture2D* FAsepriteImporter::CreateSpriteSheetTexture(
	const FAsepriteParsedData& Data,
	const FString& OutputPath,
	const FString& AssetName,
	bool* bOutCreatedTexture)
{
	if (bOutCreatedTexture)
	{
		*bOutCreatedTexture = false;
	}
	if (Data.Frames.Num() == 0) return nullptr;

	// TASK-192 U9: route through the ONE packed-sheet writer so every gate, guard, and setting the
	// per-layer sheets get covers the composited sheet identically — including the /Temp package
	// guard this path previously lacked (it advertised /Temp packages to the registry/SCC, which is
	// why the composited path was recorded as not headless-safe). The buffer copy is deliberate: the
	// packed writer's shape is the shared currency, and a few MB of memcpy is noise beside the sprite
	// pass this pipeline actually pays for (measured — TASK-192 U2).
	TArray<TArray<FColor>> FrameBuffers;
	FrameBuffers.Reserve(Data.Frames.Num());
	for (const FAsepriteFrame& Frame : Data.Frames)
	{
		FrameBuffers.Add(Frame.Pixels);
	}
	return Paper2DPlus_CreatePackedSpriteSheetTexture(
		FrameBuffers, Data.Width, Data.Height, OutputPath, AssetName,
		EPaper2DPlusPackedSheetUsage::Color, bOutCreatedTexture);
}

// ============================================
// CreateSprites
// ============================================

TArray<UPaperSprite*> FAsepriteImporter::CreateSprites(
	UTexture2D* SpriteSheet,
	const FAsepriteParsedData& Data,
	const FString& OutputPath,
	const FString& AssetPrefix,
	const FAseSpriteWritePlan* WritePlan,
	FAseSpriteWriteOutcome* OutOutcome)
{
	TArray<UPaperSprite*> Sprites;
	if ((!SpriteSheet && !WritePlan) || Data.Frames.Num() == 0) return Sprites;

	FAsepriteImportCostPhaseTimer CostPhase(EAsepriteImportCostPhase::Sprites);

	const int32 FrameCount = Data.Frames.Num();
	const int32 FrameW = Data.Width;
	const int32 FrameH = Data.Height;

	// Must match CreateSpriteSheetTexture's grid exactly so each sprite's source region lines up
	// with the generated sheet.
	const FSpriteSheetGrid Grid = FSpriteSheetGrid::Compute(FrameCount, FrameW, FrameH);
	if (!Grid.bValid)
	{
		UE_LOG(LogTemp, Error, TEXT("AsepriteImporter: CreateSprites grid for %d frames of %dx%d exceeds the maximum texture dimension; aborting."),
			FrameCount, FrameW, FrameH);
		return Sprites;
	}

	// TASK-192 U6: the flat sprites ride the SAME gated reconcile as the per-layer ones. The object
	// array keeps its historical 1:1-with-frames shape; a skipped, non-resident sprite is a null
	// slot the callers already tolerate.
	FAseSpriteWriteOutcome LocalOutcome;
	FAseSpriteWriteOutcome& Outcome = OutOutcome ? *OutOutcome : LocalOutcome;
	AseIncr_ReconcileSpriteSet(SpriteSheet, Data, OutputPath, AssetPrefix, Grid, WritePlan, Outcome);

	Sprites.Reserve(FrameCount);
	for (const TSoftObjectPtr<UPaperSprite>& Ref : Outcome.SpriteRefs)
	{
		Sprites.Add(Ref.Get());
	}

	UE_LOG(LogTemp, Log, TEXT("AsepriteImporter: flat sprites for '%s' - %d written, %d skipped"),
		*AssetPrefix, Outcome.SpritesWritten, Outcome.SpritesSkipped);
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
	const TArray<TSoftObjectPtr<UPaperSprite>>& SpriteRefs,
	const FAsepriteParsedData& Data,
	const FString& OutputPath,
	const FString& AssetPrefix,
	FAsepriteIncrementalImportContext* IncrementalContext,
	TArray<FAseTagFlipbookOutcome>* OutOutcomes)
{
	TArray<UPaperFlipbook*> Flipbooks;
	if (SpriteRefs.Num() == 0) return Flipbooks;

	FAsepriteImportCostPhaseTimer CostPhase(EAsepriteImportCostPhase::Flipbooks);

	// TASK-192 U7: the flipbook gate. A flipbook is (sprite list, per-key FrameRun, FPS) — it
	// depends on the tag's authored range, the per-frame durations, and sprite identity, never on
	// pixels, so it gates on the STRUCTURE hash over the EMITTED sequence (which also captures a
	// LoopDirection change). An unresolvable sequence hashes empty and fails closed to write.
	const auto DecideFlipbook = [&](const FString& TagKey, const FString& FlipbookPackagePath,
		int32 FromFrame, int32 ToFrame, const TArray<int32>& FrameSequence) -> FAseWriteDecision
	{
		TArray<int32> EmittedDurations;
		TArray<FString> EmittedSpriteNames;
		bool bSequenceResolvable = true;
		for (int32 FrameIndex : FrameSequence)
		{
			if (!SpriteRefs.IsValidIndex(FrameIndex) || SpriteRefs[FrameIndex].IsNull())
			{
				bSequenceResolvable = false;
				break;
			}
			EmittedDurations.Add(FrameIndex < Data.Frames.Num() ? Data.Frames[FrameIndex].Duration : 100);
			// The sprite NAME is derivable from the shared naming convention without the object, so
			// a skipped-but-existing sprite never has to load just to be hashed (TASK-192 U6).
			EmittedSpriteNames.Add(FString::Printf(TEXT("%s_%02d"), *AssetPrefix, FrameIndex));
		}

		const FString NewStructureHash = bSequenceResolvable
			? FAsepriteIncrementalWrite::ComputeTagStructureHash(
				FromFrame, ToFrame, EmittedDurations, EmittedSpriteNames)
			: FString();

		FAseWriteDecision Decision;
		if (IncrementalContext && !NewStructureHash.IsEmpty())
		{
			const FString StoredStructureHash = IncrementalContext->SourceContext
				? IncrementalContext->SourceContext->TagStructureHashes.FindRef(TagKey)
				: FString();
			Decision = FAsepriteIncrementalWrite::ShouldWriteFlipbook(
				FlipbookPackagePath, StoredStructureHash, NewStructureHash,
				IncrementalContext->bForceFullReimport);
			IncrementalContext->NewTagStructureHashes.Add(TagKey, NewStructureHash);
		}
		else
		{
			Decision.Verdict = EAseWriteVerdict::Create;
			Decision.Reason = TEXT("full rebuild (no incremental context)");
		}
		return Decision;
	};

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

		// Per-key-frame timing from each frame's .ase duration (SpriteRefs[i] is 1:1 with Data.Frames[i]).
		// GcdExact reproduces the artist's per-frame durations exactly; FixedFps is the round-FPS opt-in.
		// Keyframe sprites resolve through the refs on this WRITE path only, so a skipped-but-existing
		// sprite loads exactly when a flipbook that plays it is being rewritten (TASK-192 U6/U7).
		TArray<int32> DurationsMs;
		TArray<UPaperSprite*> ResolvedSprites;
		DurationsMs.Reserve(FrameIndices.Num());
		ResolvedSprites.Reserve(FrameIndices.Num());
		for (int32 FrameIndex : FrameIndices)
		{
			if (FrameIndex < 0 || FrameIndex >= SpriteRefs.Num())
			{
				return nullptr;
			}
			UPaperSprite* Resolved = SpriteRefs[FrameIndex].LoadSynchronous();
			if (!Resolved)
			{
				return nullptr;
			}
			ResolvedSprites.Add(Resolved);
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
				KeyFrame.Sprite = ResolvedSprites[i];
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

		AsepriteImportCost::AddFlipbooks(1, /*bWritten*/ true);
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
			int32 FromFrame = FMath::Clamp(Tag.FromFrame, 0, SpriteRefs.Num() - 1);
			int32 ToFrame = FMath::Clamp(Tag.ToFrame, 0, SpriteRefs.Num() - 1);
			FAsepriteTag ClampedTag = Tag;
			ClampedTag.FromFrame = FromFrame;
			ClampedTag.ToFrame = ToFrame;
			TArray<int32> FrameSequence = BuildTagFrameSequence(ClampedTag);

			const FString FlipbookPackagePath = OutputPath / FlipbookName;
			FAseTagFlipbookOutcome Outcome;
			Outcome.TagName = Tag.Name;
			Outcome.FlipbookAssetName = FlipbookName;
			Outcome.FlipbookPackagePath = FlipbookPackagePath;

			const FAseWriteDecision FlipbookDecision = DecideFlipbook(
				Tag.Name, FlipbookPackagePath, FromFrame, ToFrame, FrameSequence);
			if (!FlipbookDecision.ShouldWrite())
			{
				Outcome.bSkipped = true;
				AsepriteImportCost::AddFlipbooks(1, /*bWritten*/ false);
				if (OutOutcomes) { OutOutcomes->Add(MoveTemp(Outcome)); }
				continue;
			}

			UPaperFlipbook* Flipbook = CreateSingleFlipbook(FlipbookName, FrameSequence, 10.0f);
			Outcome.Flipbook = Flipbook;
			if (OutOutcomes) { OutOutcomes->Add(MoveTemp(Outcome)); }
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
		AllFrames.Reserve(SpriteRefs.Num());
		for (int32 i = 0; i < SpriteRefs.Num(); i++)
		{
			AllFrames.Add(i);
		}

		const FString FlipbookPackagePath = OutputPath / FlipbookName;
		FAseTagFlipbookOutcome Outcome;
		Outcome.TagName = TEXT("__AllFrames__");
		Outcome.FlipbookAssetName = FlipbookName;
		Outcome.FlipbookPackagePath = FlipbookPackagePath;

		const FAseWriteDecision FlipbookDecision = DecideFlipbook(
			TEXT("__AllFrames__"), FlipbookPackagePath, 0, SpriteRefs.Num() - 1, AllFrames);
		if (!FlipbookDecision.ShouldWrite())
		{
			Outcome.bSkipped = true;
			AsepriteImportCost::AddFlipbooks(1, /*bWritten*/ false);
			if (OutOutcomes) { OutOutcomes->Add(MoveTemp(Outcome)); }
			return Flipbooks;
		}

		UPaperFlipbook* Flipbook = CreateSingleFlipbook(FlipbookName, AllFrames, 10.0f);
		Outcome.Flipbook = Flipbook;
		if (OutOutcomes) { OutOutcomes->Add(MoveTemp(Outcome)); }
		if (Flipbook)
		{
			Flipbooks.Add(Flipbook);
			UE_LOG(LogTemp, Log, TEXT("AsepriteImporter: Created flipbook '%s' with all %d frames"),
				*FlipbookName, SpriteRefs.Num());
		}
	}

	return Flipbooks;
}

// ============================================
// FilterTagsByDisabledIndices
// ============================================

void FAsepriteImporter::InitDefaultSelection(const FAsepriteParsedData& InParsedData, FAsepriteLayerImportSettings& OutSettings)
{
	int32 LayerOrderCounter = 0;
	for (int32 i = 0; i < InParsedData.Layers.Num(); i++)
	{
		const FAsepriteLayer& Layer = InParsedData.Layers[i];

		// Skip group layers
		if (Layer.LayerType == 1)
		{
			continue;
		}

		// Skip hitbox/socket layers
		bool bIsHitbox = false;
		for (const FAsepriteHitboxLayer& HL : InParsedData.HitboxLayers)
		{
			if (HL.LayerIndex == i)
			{
				bIsHitbox = true;
				break;
			}
		}
		if (bIsHitbox) continue;

		// Visual layer — enable by default
		OutSettings.LayerImportEnabled.Add(i, true);
		OutSettings.LayerOrder.Add(i, LayerOrderCounter++);
	}

	// Every animation tag enabled by default
	for (int32 TagIdx = 0; TagIdx < InParsedData.Tags.Num(); ++TagIdx)
	{
		OutSettings.TagImportEnabled.Add(TagIdx, true);
	}
}

void FAsepriteImporter::FilterTagsByDisabledIndices(TArray<FAsepriteTag>& Tags, const TSet<int32>& DisabledTagIndices)
{
	if (DisabledTagIndices.Num() == 0)
	{
		return;
	}

	TArray<FAsepriteTag> EnabledTags;
	EnabledTags.Reserve(Tags.Num());
	for (int32 TagIdx = 0; TagIdx < Tags.Num(); ++TagIdx)
	{
		if (!DisabledTagIndices.Contains(TagIdx))
		{
			EnabledTags.Add(Tags[TagIdx]);
		}
	}
	Tags = MoveTemp(EnabledTags);
}

// ============================================
// Source-.ase path/hash helpers (TASK-183)
// ============================================

FString FAsepriteImporter::MakeStoredAsePath(const FString& AbsoluteFilePath)
{
	if (AbsoluteFilePath.IsEmpty())
	{
		return FString();
	}

	FString Full = FPaths::ConvertRelativePathToFull(AbsoluteFilePath);
	FPaths::NormalizeFilename(Full);

	FString ProjectDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
	FPaths::NormalizeDirectoryName(ProjectDir);

	// Under the project → store project-relative so every synced machine resolves it
	if (Full.StartsWith(ProjectDir + TEXT("/"), ESearchCase::IgnoreCase))
	{
		return Full.Mid(ProjectDir.Len() + 1);
	}
	return Full;
}

FString FAsepriteImporter::ResolveStoredAsePath(const FString& StoredPath)
{
	if (StoredPath.IsEmpty())
	{
		return FString();
	}

	FString Resolved = StoredPath;
	if (FPaths::IsRelative(Resolved))
	{
		// Relative stored paths are PROJECT-relative by contract (MakeStoredAsePath). Resolving them
		// with a bare ConvertRelativePathToFull would anchor them to the process working directory
		// (the engine binaries dir) — always prefix the project dir first.
		Resolved = FPaths::ProjectDir() / Resolved;
	}
	Resolved = FPaths::ConvertRelativePathToFull(Resolved);
	FPaths::NormalizeFilename(Resolved);
	return Resolved;
}

FString FAsepriteImporter::HashAseFileContent(const FString& AbsoluteFilePath)
{
	const FMD5Hash Hash = FMD5Hash::HashFile(*AbsoluteFilePath);
	return Hash.IsValid() ? LexToString(Hash) : FString();
}

FString FAsepriteImporter::CopySourceAseIntoProject(const FString& InAbsoluteFilePath, const FString& OutputPath)
{
	FString SourceFull = FPaths::ConvertRelativePathToFull(InAbsoluteFilePath);
	FPaths::NormalizeFilename(SourceFull);

	// Already inside the project → nothing to copy
	FString ProjectDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
	FPaths::NormalizeDirectoryName(ProjectDir);
	if (SourceFull.StartsWith(ProjectDir + TEXT("/"), ESearchCase::IgnoreCase))
	{
		return SourceFull;
	}

	// Destination: <Project>/SourceArt/ FLAT (user call 2026-08-20 — no mirrored content nesting), and
	// deliberately OUTSIDE Content/: a .ase copied INTO Content trips Unreal's own content-directory
	// auto-import monitor, which immediately prompts "new source file detected, import?" over the
	// import that just ran. SourceArt/ still commits with the project and the plugin's watcher
	// registers it as an external watch directory, so live-edit and offline reconcile both cover it.
	// OutputPath is unused here by design; distinct source files are expected to have distinct names.
	(void)OutputPath;
	FString DestDir = ProjectDir / TEXT("SourceArt");
	DestDir = FPaths::ConvertRelativePathToFull(DestDir);
	FPaths::NormalizeDirectoryName(DestDir);

	const FString DestFile = DestDir / FPaths::GetCleanFilename(SourceFull);

	IFileManager& FileManager = IFileManager::Get();
	if (!FileManager.MakeDirectory(*DestDir, /*Tree*/ true) ||
		FileManager.Copy(*DestFile, *SourceFull, /*Replace*/ true) != COPY_OK)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("AsepriteImporter: 'Keep source in project' failed to copy '%s' to '%s'; the import keeps referencing the original."),
			*SourceFull, *DestFile);
		return SourceFull;
	}

	UE_LOG(LogTemp, Log, TEXT("AsepriteImporter: Copied source .ase into project: %s"), *DestFile);
	FString DestNormalized = DestFile;
	FPaths::NormalizeFilename(DestNormalized);
	return DestNormalized;
}

// ============================================
// ImportFile
// ============================================

namespace
{
	/** The composited-frames hash in ComputeLayerBuffersHash's exact byte layout, computed over
	 *  the parse's flat frames without copying them (TASK-192 U6 — the whole-composite stamp). */
	FString AseIncr_ComputeCompositeHash(const FAsepriteParsedData& Data)
	{
		FMD5 Md5;
		const int32 FrameCount = Data.Frames.Num();
		const int32 FrameWidth = Data.Width;
		const int32 FrameHeight = Data.Height;
		Md5.Update(reinterpret_cast<const uint8*>(&FrameCount), sizeof(FrameCount));
		Md5.Update(reinterpret_cast<const uint8*>(&FrameWidth), sizeof(FrameWidth));
		Md5.Update(reinterpret_cast<const uint8*>(&FrameHeight), sizeof(FrameHeight));
		for (const FAsepriteFrame& Frame : Data.Frames)
		{
			const int32 PixelCount = Frame.Pixels.Num();
			Md5.Update(reinterpret_cast<const uint8*>(&PixelCount), sizeof(PixelCount));
			if (PixelCount > 0)
			{
				Md5.Update(reinterpret_cast<const uint8*>(Frame.Pixels.GetData()), PixelCount * sizeof(FColor));
			}
		}
		FMD5Hash Hash;
		Hash.Set(Md5);
		return LexToString(Hash);
	}
}

FAsepriteImportResult FAsepriteImporter::ImportFile(
	const FString& FilePath,
	const FString& OutputPath,
	const FString& AssetPrefix,
	const TSet<int32>* DisabledTagIndices,
	bool bOrganizeSubfolders,
	FAsepriteIncrementalImportContext* IncrementalContext)
{
	FAsepriteImportResult Result;

	// Organized layout: hundreds of per-frame sprites drown everything else when they land flat in
	// one folder. Sheets/Sprites/Flipbooks each get their own subfolder; asset names are unchanged,
	// so a re-import with the same settings reuses the same packages either way.
	const FString SheetOutputPath = bOrganizeSubfolders ? OutputPath / TEXT("Sheets") : OutputPath;
	const FString SpriteOutputPath = bOrganizeSubfolders ? OutputPath / TEXT("Sprites") / AssetPrefix : OutputPath;
	const FString FlipbookOutputPath = bOrganizeSubfolders ? OutputPath / TEXT("Flipbooks") : OutputPath;

	// TASK-192 U2: an outer scope (the watcher's) absorbs this; a standalone ImportFile owns its own.
	FAsepriteImportCostScope CostScope;

	// Parse the Aseprite file
	FAsepriteParsedData ParsedData;
	{
		FAsepriteImportCostPhaseTimer ParsePhase(EAsepriteImportCostPhase::Parse);
		if (!ParseFile(FilePath, ParsedData, Result.ErrorMessage))
		{
			Result.bSuccess = false;
			return Result;
		}
	}

	// Drop de-selected tags right after parse (indices are the file's authored tag order — the same
	// order the import dialog displayed). Sprites still cover every frame; only flipbook emission and
	// hitbox→keyframe alignment see the filtered set.
	if (DisabledTagIndices && DisabledTagIndices->Num() > 0)
	{
		FilterTagsByDisabledIndices(ParsedData.Tags, *DisabledTagIndices);
	}

	FScopedSlowTask Progress(3, LOCTEXT("ImportingAseprite", "Importing Aseprite file..."));
	Progress.MakeDialog();

	// Create sprite sheet texture (TASK-192 U6: gated on the whole-composite stamp when a context
	// is given — its payoff is tag-only edits, which used to cost a full sheet rebuild plus every
	// flat sprite for zero pixel change)
	Progress.EnterProgressFrame(1, LOCTEXT("CreatingSpriteSheet", "Creating sprite sheet texture..."));
	FString TextureName = AssetPrefix + TEXT("_Sheet");
	const FString SheetPackageName = SheetOutputPath / TextureName;
	const FString SheetObjectPath = SheetPackageName + TEXT(".") + TextureName;

	FAseWriteDecision CompositeDecision;
	if (IncrementalContext)
	{
		const FString CompositeHash = AseIncr_ComputeCompositeHash(ParsedData);
		const FString StoredCompositeHash = IncrementalContext->SourceContext
			? IncrementalContext->SourceContext->CompositeContentHash
			: FString();
		CompositeDecision = FAsepriteIncrementalWrite::ShouldWriteSheet(
			SheetPackageName, StoredCompositeHash, CompositeHash,
			IncrementalContext->StampedGrid, IncrementalContext->CurrentGrid,
			IncrementalContext->bForceFullReimport);
		IncrementalContext->NewCompositeContentHash = CompositeHash;
	}
	else
	{
		CompositeDecision.Verdict = EAseWriteVerdict::Create;
		CompositeDecision.Reason = TEXT("full rebuild (no incremental context)");
	}

	bool bFlatSheetCreatedObject = false;
	if (CompositeDecision.ShouldWrite())
	{
		Result.SpriteSheet = CreateSpriteSheetTexture(
			ParsedData, SheetOutputPath, TextureName, &bFlatSheetCreatedObject);
		if (!Result.SpriteSheet)
		{
			Result.ErrorMessage = TEXT("Failed to create sprite sheet texture.");
			Result.bSuccess = false;
			return Result;
		}
	}
	else
	{
		AsepriteImportCost::AddSheets(1, /*bWritten*/ false);
		// KTD9: a RESIDENT skipped sheet still gets its cheap settings reconciled.
		if (UTexture2D* ResidentSheet = FindObject<UTexture2D>(nullptr, *SheetObjectPath))
		{
			if (FAsepriteIncrementalWrite::ReconcileSheetSettings(ResidentSheet, /*bIsNormalMap*/ false))
			{
				ResidentSheet->UpdateResource();
				if (UPackage* SheetPackage = ResidentSheet->GetOutermost();
					SheetPackage && !FPackageName::IsTempPackage(SheetPackage->GetName()))
				{
					SheetPackage->MarkPackageDirty();
				}
				UE_LOG(LogTemp, Log, TEXT("Aseprite incremental: repaired drifted settings on skipped sheet '%s'"), *TextureName);
			}
			Result.SpriteSheet = ResidentSheet;
		}
	}

	// Create sprites (TASK-192 U6: the flat sprites follow U5's rules against the composited
	// sheet's verdict — this IS the primary render path's crop-bug protection, because the flat
	// sprites are what the Character Profile's flipbooks play in game)
	Progress.EnterProgressFrame(1, LOCTEXT("CreatingSprites", "Creating sprites..."));
	FAseSpriteWritePlan FlatSpritePlan;
	FAseSpriteWritePlan* FlatPlanPtr = nullptr;
	FAseSpriteWriteOutcome FlatOutcome;
	TArray<TArray<FColor>> FlatBuffers;
	if (IncrementalContext)
	{
		FlatSpritePlan.SheetDecision = CompositeDecision;
		FlatSpritePlan.bSheetObjectRecreated =
			bFlatSheetCreatedObject && IncrementalContext->SourceContext != nullptr;
		FlatSpritePlan.StampedGrid = IncrementalContext->StampedGrid;
		FlatSpritePlan.CurrentGrid = IncrementalContext->CurrentGrid;
		FlatSpritePlan.bForceFullReimport = IncrementalContext->bForceFullReimport;
		if (CompositeDecision.ShouldWrite())
		{
			FlatBuffers.Reserve(ParsedData.Frames.Num());
			for (const FAsepriteFrame& Frame : ParsedData.Frames)
			{
				FlatBuffers.Add(Frame.Pixels);
			}
			FlatSpritePlan.FrameBuffers = &FlatBuffers;
		}
		FlatSpritePlan.SheetObjectPath = SheetObjectPath;
		FlatPlanPtr = &FlatSpritePlan;
	}
	Result.Sprites = CreateSprites(
		Result.SpriteSheet, ParsedData, SpriteOutputPath, AssetPrefix, FlatPlanPtr, &FlatOutcome);
	if (Result.Sprites.Num() == 0)
	{
		Result.ErrorMessage = TEXT("Failed to create any sprites.");
		Result.bSuccess = false;
		return Result;
	}

	// Create flipbooks (TASK-192 U7: gated per tag on the structure stamp when a context is given;
	// keyframe sprites resolve through the refs, so a written flipbook can pull a skipped-but-
	// existing sprite without the sprite pass having loaded it)
	Progress.EnterProgressFrame(1, LOCTEXT("CreatingFlipbooks", "Creating flipbooks..."));
	TArray<TSoftObjectPtr<UPaperSprite>> FlatSpriteRefs;
	if (FlatPlanPtr)
	{
		FlatSpriteRefs = FlatOutcome.SpriteRefs;
	}
	else
	{
		FlatSpriteRefs.Reserve(Result.Sprites.Num());
		for (UPaperSprite* Sprite : Result.Sprites)
		{
			FlatSpriteRefs.Add(Sprite);
		}
	}
	Result.Flipbooks = CreateFlipbooks(
		FlatSpriteRefs, ParsedData, FlipbookOutputPath, AssetPrefix,
		IncrementalContext, &Result.TagFlipbookOutcomes);

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
	// TASK-189: this is now the MULTI-FILE door into the Bulk Sprite Extractor, and it is the one
	// that matters: a Content Browser drop can only deliver a single file (AssetTools stops its
	// per-file loop the moment a factory reports a cancel), so a whole set comes in here.
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (!DesktopPlatform)
	{
		return;
	}

	TArray<FString> OutFiles;
	const bool bOpened = DesktopPlatform->OpenFileDialog(
		FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
		TEXT("Select Aseprite Files"),
		FEditorDirectories::Get().GetLastDirectory(ELastDirectory::GENERIC_IMPORT),
		TEXT(""),
		TEXT("Aseprite Files (*.ase;*.aseprite)|*.ase;*.aseprite"),
		EFileDialogFlags::Multiple,
		OutFiles);

	if (!bOpened || OutFiles.Num() == 0)
	{
		return;
	}

	FEditorDirectories::Get().SetLastDirectory(ELastDirectory::GENERIC_IMPORT, FPaths::GetPath(OutFiles[0]));
	SBulkSpriteExtractorWindow::OpenBulkExtractorForAseFiles(OutFiles);
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

static UTexture2D* Paper2DPlus_CreatePackedSpriteSheetTexture(
	const TArray<TArray<FColor>>& FrameBuffers,
	int32 FrameWidth, int32 FrameHeight,
	const FString& OutputPath,
	const FString& AssetName,
	EPaper2DPlusPackedSheetUsage Usage,
	bool* bOutCreatedTexture)
{
	if (bOutCreatedTexture)
	{
		*bOutCreatedTexture = false;
	}
	if (FrameBuffers.Num() == 0 || FrameWidth <= 0 || FrameHeight <= 0)
	{
		return nullptr;
	}

	FAsepriteImportCostPhaseTimer CostPhase(EAsepriteImportCostPhase::TextureBuild);

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
	if (bOutCreatedTexture)
	{
		*bOutCreatedTexture = bCreatedTexture;
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

	AsepriteImportCost::AddSheets(1, /*bWritten*/ true);
	return Texture;
}

// ============================================
// CreatePerLayerSpriteSheetTexture
// ============================================

UTexture2D* FAsepriteImporter::CreatePerLayerSpriteSheetTexture(
	const TArray<TArray<FColor>>& FrameBuffers,
	int32 FrameWidth, int32 FrameHeight,
	const FString& OutputPath,
	const FString& AssetName,
	bool* bOutCreatedTexture)
{
	return Paper2DPlus_CreatePackedSpriteSheetTexture(
		FrameBuffers,
		FrameWidth,
		FrameHeight,
		OutputPath,
		AssetName,
		EPaper2DPlusPackedSheetUsage::Color,
		bOutCreatedTexture);
}

TArray<FCharacterLayerAnimationMapping> FAsepriteImporter::CreatePerLayerSprites(
	UTexture2D* SpriteSheet,
	const FAsepriteParsedData& Data,
	const FString& OutputPath,
	const FString& AssetPrefix,
	const FAseSpriteWritePlan* WritePlan,
	FAseSpriteWriteOutcome* OutOutcome)
{
	TArray<FCharacterLayerAnimationMapping> Mappings;
	if ((!SpriteSheet && !WritePlan) || Data.Frames.Num() == 0)
	{
		return Mappings;
	}

	FAsepriteImportCostPhaseTimer CostPhase(EAsepriteImportCostPhase::Sprites);

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

	FAseSpriteWriteOutcome LocalOutcome;
	FAseSpriteWriteOutcome& Outcome = OutOutcome ? *OutOutcome : LocalOutcome;
	AseIncr_ReconcileSpriteSet(SpriteSheet, Data, OutputPath, AssetPrefix, Grid, WritePlan, Outcome);

	// Build animation mappings from tags (or single animation if no tags). Identity comes from the
	// per-frame refs, so skipped-but-existing sprites keep their place in every mapping.
	if (Data.Tags.Num() > 0)
	{
		for (const FAsepriteTag& Tag : Data.Tags)
		{
			FCharacterLayerAnimationMapping Mapping;
			Mapping.AnimationName = Tag.Name;

			const int32 FromFrame = FMath::Clamp(Tag.FromFrame, 0, FrameCount - 1);
			const int32 ToFrame = FMath::Clamp(Tag.ToFrame, 0, FrameCount - 1);

			for (int32 FrameIdx = FromFrame; FrameIdx <= ToFrame; FrameIdx++)
			{
				if (Outcome.SpriteRefs.IsValidIndex(FrameIdx) && !Outcome.SpriteRefs[FrameIdx].IsNull())
				{
					Mapping.Sprites.Add(Outcome.SpriteRefs[FrameIdx]);
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
		FCharacterLayerAnimationMapping Mapping;
		Mapping.AnimationName = AssetPrefix;

		for (int32 FrameIdx = 0; FrameIdx < FrameCount; FrameIdx++)
		{
			if (!Outcome.SpriteRefs[FrameIdx].IsNull())
			{
				Mapping.Sprites.Add(Outcome.SpriteRefs[FrameIdx]);
			}
		}

		if (Mapping.Sprites.Num() > 0)
		{
			Mappings.Add(MoveTemp(Mapping));
		}
	}

	UE_LOG(LogTemp, Log, TEXT("AsepriteImporter: sprites for '%s' - %d written, %d skipped, %d animation mappings"),
		*AssetPrefix, Outcome.SpritesWritten, Outcome.SpritesSkipped, Mappings.Num());

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
	const FString& AssetName,
	bool* bOutCreatedTexture)
{
	// Use the same cell packer as color sheets, but establish linear normal-map settings before Source.Init and the
	// first/only resource build. This keeps the normal grid aligned without ever launching a stale sRGB build.
	return Paper2DPlus_CreatePackedSpriteSheetTexture(
		FrameBuffers,
		FrameWidth,
		FrameHeight,
		OutputPath,
		AssetName,
		EPaper2DPlusPackedSheetUsage::TangentNormal,
		bOutCreatedTexture);
}

void FAsepriteImporter::AttachNormalMapToSprites(
	const TArray<UPaperSprite*>& Sprites,
	UTexture2D* NormalTexture,
	UMaterialInterface* LitMaterial,
	int32& OutSpritesTouched,
	const bool bSkipAlreadyAttached)
{
	OutSpritesTouched = 0;
	if (!NormalTexture)
	{
		return;
	}

	FAsepriteImportCostPhaseTimer CostPhase(EAsepriteImportCostPhase::Sprites);

	for (UPaperSprite* Sprite : Sprites)
	{
		if (!Sprite)
		{
			continue;
		}

		// TASK-192 U4: the attach used to re-initialise (and dirty) every base sprite of a paired
		// layer unconditionally — silently failing R1 for exactly the projects that use normal maps.
		// When the caller opts in, a sprite already carrying this normal texture (and, when a lit
		// material is requested, this material) is a no-op.
		if (bSkipAlreadyAttached)
		{
			FAdditionalSpriteTextureArray BakedList;
			Sprite->GetBakedAdditionalSourceTextures(BakedList);
			const bool bTextureAttached = BakedList.Num() >= 1 && BakedList[0] == NormalTexture;
			const bool bMaterialMatches = !LitMaterial || Sprite->GetDefaultMaterial() == LitMaterial;
			if (bTextureAttached && bMaterialMatches)
			{
				AsepriteImportCost::AddSprites(1, /*bWritten*/ false);
				continue;
			}
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
		AsepriteImportCost::AddSprites(1, /*bWritten*/ true);
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
// AppendProfileEntriesFromImportResult
// ============================================

void FAsepriteImporter::AppendProfileEntriesFromImportResult(
	UPaper2DPlusCharacterProfileAsset* Profile,
	const FAsepriteImportResult& ProfileResult,
	const FString& AssetPrefix,
	int32* OutEntriesAdded,
	int32* OutEntriesRefreshed,
	int32* OutEntriesSkipped)
{
	if (OutEntriesAdded) { *OutEntriesAdded = 0; }
	if (OutEntriesRefreshed) { *OutEntriesRefreshed = 0; }
	if (OutEntriesSkipped) { *OutEntriesSkipped = 0; }
	if (!Profile)
	{
		return;
	}

	// TASK-192 U7: Modify() only when a row actually changes — it used to run before any per-entry
	// comparison, so an all-skip pass still dirtied the Character Profile package. The rows are in
	// memory (the profile is loaded), so this is the compare-then-write half of KTD1.
	bool bModified = false;
	const auto EnsureModified = [&]()
	{
		if (!bModified)
		{
			// Modify() BEFORE mutation so OnObjectModified fires and open editor models reconcile.
			Profile->Modify();
			bModified = true;
		}
	};

	// Same prefix-strip as RegenerateProfileFromImportResult so the two population paths agree.
	const auto StripPrefix = [&AssetPrefix](FString Name)
	{
		if (Name.StartsWith(AssetPrefix + TEXT("_")))
		{
			Name.RightChopInline(AssetPrefix.Len() + 1);
		}
		return Name;
	};

	// Walk the per-tag outcomes when the incremental import produced them — a SKIPPED flipbook still
	// reconciles its row by soft path without ever loading — else the legacy written-objects walk.
	struct FAseAppendRow
	{
		FString AnimName;
		FSoftObjectPath FlipbookPath;
	};
	TArray<FAseAppendRow> Rows;
	if (ProfileResult.TagFlipbookOutcomes.Num() > 0)
	{
		for (const FAseTagFlipbookOutcome& Outcome : ProfileResult.TagFlipbookOutcomes)
		{
			FAseAppendRow Row;
			Row.AnimName = StripPrefix(Outcome.FlipbookAssetName);
			Row.FlipbookPath = Outcome.Flipbook
				? FSoftObjectPath(Outcome.Flipbook)
				: FSoftObjectPath(Outcome.FlipbookPackagePath + TEXT(".") + Outcome.FlipbookAssetName);
			Rows.Add(MoveTemp(Row));
		}
	}
	else
	{
		for (UPaperFlipbook* Flipbook : ProfileResult.Flipbooks)
		{
			if (!Flipbook) continue;
			FAseAppendRow Row;
			Row.AnimName = StripPrefix(Flipbook->GetName());
			Row.FlipbookPath = FSoftObjectPath(Flipbook);
			Rows.Add(MoveTemp(Row));
		}
	}

	for (const FAseAppendRow& Row : Rows)
	{
		FFlipbookProfileEntry* Existing = Profile->Flipbooks.FindByPredicate(
			[&Row](const FFlipbookProfileEntry& Entry)
			{
				return Entry.Identity.FlipbookName.Equals(Row.AnimName, ESearchCase::IgnoreCase);
			});

		if (Existing)
		{
			// Refresh only the art references that actually differ — authored combat/timing/tag
			// data stays untouched either way.
			bool bChanged = false;
			if (Existing->Identity.Flipbook.ToSoftObjectPath() != Row.FlipbookPath)
			{
				EnsureModified();
				Existing->Identity.Flipbook = TSoftObjectPtr<UPaperFlipbook>(Row.FlipbookPath);
				bChanged = true;
			}
			if (ProfileResult.SpriteSheet
				&& Existing->SourceTexture.ToSoftObjectPath() != FSoftObjectPath(ProfileResult.SpriteSheet))
			{
				EnsureModified();
				Existing->SourceTexture = ProfileResult.SpriteSheet;
				bChanged = true;
			}
			if (bChanged)
			{
				if (OutEntriesRefreshed) { ++(*OutEntriesRefreshed); }
			}
			else if (OutEntriesSkipped)
			{
				++(*OutEntriesSkipped);
			}
		}
		else
		{
			EnsureModified();
			FFlipbookProfileEntry Entry;
			Entry.Identity.FlipbookName = Row.AnimName;
			Entry.Identity.Flipbook = TSoftObjectPtr<UPaperFlipbook>(Row.FlipbookPath);
			if (ProfileResult.SpriteSheet)
			{
				Entry.SourceTexture = ProfileResult.SpriteSheet;
			}
			Profile->Flipbooks.Add(MoveTemp(Entry));
			if (OutEntriesAdded) { ++(*OutEntriesAdded); }
		}
	}
}

// ============================================
// TASK-189 U4 — structural-diff APPLY
// ============================================
//
// The pure decisions live in FAsepriteStructuralDiff. This layer feeds them from asset state and
// applies what they decide, against real packages. Three rules shape every function below:
//
//  * ORDER. Renames are applied BEFORE the pipeline writes anything, because the pipeline is
//    FindOrCreate-in-place: rename first and every downstream creator refreshes the SAME package
//    the previous import wrote (references survive through the redirector). Rename afterwards and
//    the new assets already exist under the new name, the old ones are stranded, and the layer is
//    silently duplicated. That forces two phases, at the two points where their inputs first
//    exist: tags before the profile is populated, layers before the per-layer materialisation.
//  * DATA FIRST, DISK SECOND. In-place data edits are ordinary property writes; IAssetTools
//    renames are not undoable. A failed disk rename logs, degrades to keep-both, and never blocks.
//  * NOTHING IS EVER DELETED. Removed generated data is dropped from the asset; the backing
//    packages stay on disk and are REPORTED as orphans (R11). No modal, ever — the watcher runs
//    this path unattended (R13).
namespace AsepriteDiffApply
{
	/** Three name spaces meet on a tag and they are not interchangeable: the layer asset keys its
	 *  mappings by the RAW tag name, the flipbook package is the sanitized "<prefix>_<Tag>", and the
	 *  profile entry is that with the "<prefix>_" stripped. Derive each one, never assume. */
	static FString FlipbookAssetNameForTag(const FString& AssetPrefix, const FString& TagName)
	{
		FString Name = FString::Printf(TEXT("%s_%s"), *AssetPrefix, *TagName);
		FSpriteExtractionUtils::SanitizeAssetName(Name);
		return Name;
	}

	static FString ProfileAnimNameForTag(const FString& AssetPrefix, const FString& TagName)
	{
		FString Name = FlipbookAssetNameForTag(AssetPrefix, TagName);
		const FString Lead = AssetPrefix + TEXT("_");
		if (Name.StartsWith(Lead))
		{
			Name.RightChopInline(Lead.Len());
		}
		return Name;
	}

	/**
	 * THE one derivation of a layer's identity and output folders. Both the per-layer creation loop
	 * and the rename phase call this: if they ever disagreed, a rename would move an asset to a path
	 * the loop then does not find, and the layer would silently duplicate.
	 * OutLayerPath is the FULL hierarchy path (the diff/FCharacterLayer::LayerName key);
	 * OutSanitizedLeafName is the LEAF name as it appears inside generated asset names.
	 */
	static void DeriveLayerOutputNames(
		const FAsepriteParsedData& ParsedData,
		int32 LayerIdx,
		const FAsepriteLayerImportSettings& Settings,
		FString& OutLayerPath,
		FString& OutSanitizedLeafName,
		FString& OutSheetSubPath,
		FString& OutSpriteSubPath)
	{
		const FAsepriteLayer& Layer = ParsedData.Layers[LayerIdx];

		OutLayerPath.Reset();
		if (ParsedData.LayerHierarchy.IsValidIndex(LayerIdx))
		{
			OutLayerPath = ParsedData.LayerHierarchy[LayerIdx].FullPath;
		}
		if (OutLayerPath.IsEmpty())
		{
			OutLayerPath = Layer.Name;
		}

		// Strict, not space-only: a layer name carrying any character the engine forbids in a package
		// name (& ! ~ @ # . , quotes, brackets ...) produces a package that cannot be created, so the
		// sprites and sheet are never written while the Layer Asset still records references to the
		// intended paths. The result is a layer that silently renders nothing, with no error anywhere.
		OutSanitizedLeafName = Layer.Name;
		FSpriteExtractionUtils::SanitizeAssetNameStrict(OutSanitizedLeafName);

		// Surface the substitution: a generated name that does not match the artist's layer name is
		// exactly the kind of thing that has to be visible when hunting missing art later.
		{
			FString SpaceOnly = Layer.Name;
			SpaceOnly.ReplaceInline(TEXT(" "), TEXT("_"));
			SpaceOnly.ReplaceInline(TEXT("/"), TEXT("_"));
			if (OutSanitizedLeafName != SpaceOnly)
			{
				UE_LOG(LogTemp, Warning,
					TEXT("AsepriteImporter: layer '%s' contains characters the engine cannot use in an asset name; its generated assets are named '%s'. Rename the layer in the .ase to control the result."),
					*Layer.Name, *OutSanitizedLeafName);
			}
		}

		// The layer's group path from the .ase group hierarchy (empty for ungrouped layers)
		FString GroupPath;
		if (ParsedData.LayerHierarchy.IsValidIndex(LayerIdx))
		{
			const FAsepriteLayerNode& Node = ParsedData.LayerHierarchy[LayerIdx];
			int32 CurrentParent = Node.ParentIndex;
			while (CurrentParent >= 0 && ParsedData.LayerHierarchy.IsValidIndex(CurrentParent))
			{
				const FAsepriteLayerNode& ParentNode = ParsedData.LayerHierarchy[CurrentParent];
				FString ParentName;
				if (ParsedData.Layers.IsValidIndex(ParentNode.LayerIndex))
				{
					// Group names become FOLDER names, which are long-package segments and reject the
					// same character set. Sanitized per segment, then joined with '/' below.
					ParentName = ParsedData.Layers[ParentNode.LayerIndex].Name;
					FSpriteExtractionUtils::SanitizeAssetNameStrict(ParentName);
				}
				if (!ParentName.IsEmpty())
				{
					GroupPath = GroupPath.IsEmpty() ? ParentName : (ParentName / GroupPath);
				}
				CurrentParent = ParentNode.ParentIndex;
			}
		}

		// Organized = Sheets/ for the layer's sheet textures, Sprites/<prefix>[/Group]/ for its
		// per-frame sprites; legacy = the old OutputPath/AssetPrefix[/Group] nesting for both.
		if (Settings.bOrganizeIntoSubfolders)
		{
			OutSheetSubPath = Settings.OutputPath / TEXT("Sheets");
			OutSpriteSubPath = Settings.OutputPath / TEXT("Sprites") / Settings.AssetPrefix;
			if (!GroupPath.IsEmpty())
			{
				OutSpriteSubPath = OutSpriteSubPath / GroupPath;
			}
		}
		else
		{
			FString LegacySubPath = Settings.OutputPath / Settings.AssetPrefix;
			if (!GroupPath.IsEmpty())
			{
				LegacySubPath = LegacySubPath / GroupPath;
			}
			OutSheetSubPath = LegacySubPath;
			OutSpriteSubPath = LegacySubPath;
		}
	}

	/** Renaming assets needs a registry that has finished its initial scan; before that the rename
	 *  manager cannot see the references it has to fix up. Guard rather than assume — the watcher
	 *  path is post-OnFilesLoaded in practice, a commandlet may not be. */
	static bool IsAssetRegistryReadyForRename()
	{
		FAssetRegistryModule& RegistryModule =
			FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		return !RegistryModule.Get().IsLoadingAssets();
	}

	/** Rename ONE generated asset, leaving the standard redirector so existing references resolve.
	 *  Returns the ACTUAL post-rename object path — never the intended one, because a degrade that
	 *  gets stamped as the intended name is reported as an orphan on the next diff cycle. */
	static bool RenameGeneratedAsset(
		const FSoftObjectPath& OldObjectPath,
		const FString& NewPackagePath,
		const FString& NewAssetName,
		FString& OutActualObjectPath,
		FString& OutFailureReason)
	{
		OutActualObjectPath.Reset();
		OutFailureReason.Reset();
		if (OldObjectPath.IsNull())
		{
			OutFailureReason = TEXT("no generated asset was recorded");
			return false;
		}

		UObject* Asset = OldObjectPath.TryLoad();
		if (!Asset)
		{
			OutFailureReason = FString::Printf(TEXT("'%s' no longer exists"), *OldObjectPath.ToString());
			return false;
		}

		const FString CurrentPackagePath = FPackageName::GetLongPackagePath(Asset->GetOutermost()->GetName());
		if (CurrentPackagePath.Equals(NewPackagePath, ESearchCase::IgnoreCase)
			&& Asset->GetName().Equals(NewAssetName, ESearchCase::CaseSensitive))
		{
			OutActualObjectPath = FSoftObjectPath(Asset).ToString();
			return true; // already where the new name says it should be
		}

		const FString TargetPackageName = NewPackagePath / NewAssetName;
		if (FPackageName::DoesPackageExist(TargetPackageName)
			|| FindPackage(nullptr, *TargetPackageName) != nullptr)
		{
			OutFailureReason = FString::Printf(TEXT("'%s' already exists"), *TargetPackageName);
			return false;
		}

		TArray<FAssetRenameData> Renames;
		Renames.Emplace(Asset, NewPackagePath, NewAssetName);

		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
		if (!AssetTools.RenameAssets(Renames))
		{
			OutFailureReason = FString::Printf(TEXT("the engine refused to rename '%s'"), *OldObjectPath.ToString());
			return false;
		}

		OutActualObjectPath = FSoftObjectPath(Asset).ToString();
		return true;
	}

	/** Names contributed by every source of this Layer Profile EXCEPT the one being reimported.
	 *  Feeds both R9 (a shared name can never pair as a rename) and R10 (a shared name can never be
	 *  cleanly removed — the sibling still owns the data). */
	static TSet<FString> CollectSiblingContributedNames(
		const UPaper2DPlusCharacterLayerAsset& LayerAsset,
		const FString& StoredSourcePath,
		bool bTags)
	{
		TSet<FString> Names;
#if WITH_EDITORONLY_DATA
		for (const FAsepriteSourceContext& Sibling : LayerAsset.ImportedAseSources)
		{
			if (Sibling.StoredSourcePath.Equals(StoredSourcePath, ESearchCase::IgnoreCase))
			{
				continue;
			}
			const TMap<FString, FString>& Hashes = bTags ? Sibling.TagContentHashes : Sibling.LayerContentHashes;
			for (const TPair<FString, FString>& Pair : Hashes)
			{
				Names.Add(Pair.Key);
			}
		}
#endif
		return Names;
	}

	/** Resolve the Layer Profile this import will land in WITHOUT creating anything. The diff has to
	 *  read the previous import's record long before the pipeline creates or adopts the asset, and a
	 *  first import must stay a first import — creating here would make every fresh import look like
	 *  a reimport against an empty record. */
	static UPaper2DPlusCharacterLayerAsset* ResolveExistingLayerAsset(const FAsepriteLayerImportSettings& Settings)
	{
		if (!Settings.ExistingLayerAsset.IsNull())
		{
			return Settings.ExistingLayerAsset.LoadSynchronous();
		}
		const FString LayerAssetName = Settings.AssetPrefix + TEXT("_Layers");
		const FString PackageName = Settings.OutputPath / LayerAssetName;
		if (!FPackageName::DoesPackageExist(PackageName))
		{
			return nullptr;
		}
		return Cast<UPaper2DPlusCharacterLayerAsset>(
			FSoftObjectPath(PackageName + TEXT(".") + LayerAssetName).TryLoad());
	}

	/** The recorded context for THIS source, or null on a first import / a legacy asset that has
	 *  never been stamped. Read it BEFORE UpsertAseSourceContext overwrites it with the new hashes. */
	static const FAsepriteSourceContext* FindRecordedSource(
		const UPaper2DPlusCharacterLayerAsset& LayerAsset,
		const FString& StoredSourcePath)
	{
#if WITH_EDITORONLY_DATA
		return LayerAsset.FindAseSourceContext(StoredSourcePath);
#else
		return nullptr;
#endif
	}

	/** Mark the Layer Profile modified before the first structural write. The legacy reimporter's
	 *  precedent binds here too: no FScopedTransaction, because package creation and bulk-data
	 *  rebuilds in this pipeline are not undoable — but Modify() still fires OnObjectModified so an
	 *  open Layer editor reconciles instead of showing stale rows. */
	static void MarkLayerAssetModified(UPaper2DPlusCharacterLayerAsset& LayerAsset, bool& bInOutModified)
	{
		if (bInOutModified)
		{
			return;
		}
		LayerAsset.SetFlags(RF_Transactional);
		LayerAsset.Modify();
		bInOutModified = true;
	}

	/** Does this profile entry carry designer work a removal must never destroy? Hitbox/excluded
	 *  frames, root motion, frame events, frame cues, curves, transitions, or membership in any tag
	 *  mapping. Generated art references (Flipbook / SourceTexture) are NOT authored data. */
	static bool ProfileEntryHasAuthoredData(
		const UPaper2DPlusCharacterProfileAsset* Profile,
		const FString& AnimName)
	{
		if (!Profile || AnimName.IsEmpty())
		{
			return false;
		}
		const FFlipbookProfileEntry* Entry = Profile->Flipbooks.FindByPredicate(
			[&AnimName](const FFlipbookProfileEntry& Candidate)
			{
				return Candidate.Identity.FlipbookName.Equals(AnimName, ESearchCase::IgnoreCase);
			});
		if (!Entry)
		{
			return false;
		}
		if (Entry->CombatData.Frames.Num() > 0
			|| Entry->CombatData.ExcludedFrames.Num() > 0
			|| Entry->MotionData.RootMotion.Num() > 0
			|| Entry->FrameEventData.FrameEvents.Num() > 0
			|| Entry->FrameEventData.FrameCues.Num() > 0
			|| Entry->CurveData.Curves.Num() > 0
			|| Entry->TransitionData.Transitions.Num() > 0)
		{
			return true;
		}
		// A move a designer has placed in the tag taxonomy is authored, even with no per-frame data.
		for (const TPair<FGameplayTag, FFlipbookTagMapping>& Mapping : Profile->TagMappings)
		{
			for (const FFlipbookTagMappingEntry& TagEntry : Mapping.Value.Entries)
			{
				if (TagEntry.FlipbookName.Equals(AnimName, ESearchCase::IgnoreCase))
				{
					return true;
				}
			}
		}
		return false;
	}

	/**
	 * Surface the outcome (R13): a Message Log listing the user can walk, plus one editor
	 * notification carrying the counts. NEVER a modal — the watcher drives this path unattended, so
	 * a prompt here would hang the editor on a background reimport. Headless runs get the log only.
	 */
	static void PublishDiffReport(
		const FString& AssetDisplayName,
		const FAseDiffApplyReport& Report,
		const TArray<FString>& DetailLines)
	{
		if (FApp::IsUnattended() || IsRunningCommandlet() || GIsAutomationTesting
			|| !FSlateApplication::IsInitialized())
		{
			return; // the UE_LOG lines the caller already emitted are the whole record here
		}

		static const FName ListingName(TEXT("Paper2DPlusAsepriteImport"));
		FMessageLogModule& MessageLogModule =
			FModuleManager::LoadModuleChecked<FMessageLogModule>(TEXT("MessageLog"));
		if (!MessageLogModule.IsRegisteredLogListing(ListingName))
		{
			FMessageLogInitializationOptions Options;
			Options.bShowFilters = true;
			Options.bShowPages = true;
			Options.bAllowClear = true;
			MessageLogModule.RegisterLogListing(
				ListingName, LOCTEXT("AsepriteImportLogLabel", "Aseprite Import"), Options);
		}

		FMessageLog ImportLog(ListingName);
		ImportLog.NewPage(FText::Format(
			LOCTEXT("AsepriteDiffPage", "{0} — reimport"), FText::FromString(AssetDisplayName)));
		for (const FString& Line : DetailLines)
		{
			// Anything the user must act on (a refused rename, a kept-but-orphaned asset) is a
			// Warning; applied renames and clean removals are Info.
			ImportLog.Message(EMessageSeverity::Info, FText::FromString(Line));
		}

		const FString Summary = Report.ToSummaryText();
		if (Summary.IsEmpty())
		{
			return;
		}
		FNotificationInfo Info(FText::Format(
			LOCTEXT("AsepriteDiffNotification", "{0}: {1}"),
			FText::FromString(AssetDisplayName), FText::FromString(Summary)));
		Info.ExpireDuration = 8.0f;
		Info.bFireAndForget = true;
		Info.Hyperlink = FSimpleDelegate::CreateLambda([]()
		{
			FMessageLog(ListingName).Open(EMessageSeverity::Info, /*bOpenEvenIfEmpty=*/true);
		});
		Info.HyperlinkText = LOCTEXT("AsepriteDiffOpenLog", "Show details");
		FSlateNotificationManager::Get().AddNotification(Info);
	}

	/**
	 * PHASE A — TAGS. Runs BEFORE the profile is populated. AppendProfileEntriesFromImportResult
	 * matches by NAME, so a tag renamed in Aseprite would otherwise mint a SECOND profile entry
	 * beside the authored one and strand every combat/timing/cue edit on the dead name.
	 */
	static void ApplyTagDiff(
		UPaper2DPlusCharacterLayerAsset& LayerAsset,
		UPaper2DPlusCharacterProfileAsset* Profile,
		const FAsepriteParsedData& ParsedData,
		const FAsepriteLayerImportSettings& Settings,
		const FString& StoredSourcePath,
		bool& bInOutLayerAssetModified,
		FAseDiffApplyReport& Report)
	{
#if WITH_EDITORONLY_DATA
		const FAsepriteSourceContext* Recorded = FindRecordedSource(LayerAsset, StoredSourcePath);
		if (!Recorded || Recorded->TagContentHashes.Num() == 0)
		{
			return; // first import of this source, or a legacy asset that was never stamped
		}
		// The OLD generated assets were written under the PREVIOUS import's prefix; using this
		// import's prefix to find them would miss every one of them.
		const FString OldAssetPrefix = Recorded->AssetPrefix.IsEmpty() ? Settings.AssetPrefix : Recorded->AssetPrefix;
		const TSet<FString> SiblingTags =
			CollectSiblingContributedNames(LayerAsset, StoredSourcePath, /*bTags=*/true);

		TArray<FAseDiffOldItem> OldItems;
		OldItems.Reserve(Recorded->TagContentHashes.Num());
		for (const TPair<FString, FString>& Pair : Recorded->TagContentHashes)
		{
			FAseDiffOldItem Item;
			Item.Name = Pair.Key;
			Item.ContentHash = Pair.Value;
			Item.bSharedWithOtherSources = SiblingTags.Contains(Pair.Key);
			Item.bHasAuthoredData =
				FAsepriteStructuralDiff::HasAuthoredAnimationData(LayerAsset.Layers, Pair.Key)
				|| ProfileEntryHasAuthoredData(Profile, ProfileAnimNameForTag(OldAssetPrefix, Pair.Key));
			OldItems.Add(MoveTemp(Item));
		}

		TArray<FAseDiffNewItem> NewItems;
		NewItems.Reserve(ParsedData.Tags.Num());
		for (const FAsepriteTag& Tag : ParsedData.Tags)
		{
			FAseDiffNewItem Item;
			Item.Name = Tag.Name;
			Item.ContentHash = FAsepriteStructuralDiff::ComputeTagFramesHash(ParsedData, Tag);
			Item.bCollidesWithExistingItem = SiblingTags.Contains(Tag.Name);
			NewItems.Add(MoveTemp(Item));
		}

		const FAseDiffResult Diff = FAsepriteStructuralDiff::DiffItems(OldItems, NewItems);
		if (Diff.Renames.Num() == 0 && Diff.Removals.Num() == 0)
		{
			return; // pure refresh — leave the package undirtied (R13)
		}

		const FString FlipbookFolder = Settings.bOrganizeIntoSubfolders
			? Settings.OutputPath / TEXT("Flipbooks")
			: Settings.OutputPath;
		const bool bCanRenameOnDisk = IsAssetRegistryReadyForRename();

		// ---- Renames: data first (profile funnel, then the layer-asset side it excludes), disk second.
		for (const FAseDiffRename& Rename : Diff.Renames)
		{
			const FString OldProfileName = ProfileAnimNameForTag(OldAssetPrefix, Rename.OldName);
			const FString NewProfileName = ProfileAnimNameForTag(Settings.AssetPrefix, Rename.NewName);

			MarkLayerAssetModified(LayerAsset, bInOutLayerAssetModified);

			// The layer-asset side FIRST, because its refusal is the one that can still be undone
			// cheaply: a per-layer collision means two rows would claim one animation.
			TArray<FString> CollidedLayers;
			FAsepriteStructuralDiff::RenameAnimationOnLayers(
				LayerAsset.Layers, Rename.OldName, Rename.NewName, &CollidedLayers);
			if (CollidedLayers.Num() > 0)
			{
				Report.DegradedRenames.Add(FString::Printf(
					TEXT("Animation '%s' was NOT renamed to '%s': layer(s) %s already own a row under the new name. Treated as a delete plus an add; nothing was destroyed."),
					*Rename.OldName, *Rename.NewName, *FString::Join(CollidedLayers, TEXT(", "))));
				continue;
			}

			// The profile's full propagation funnel: tag mappings, chain flags, transitions,
			// thumbnail, Animation Map node positions and the lookup caches all follow the name.
			bool bProfileRenamed = true;
			if (Profile)
			{
				const int32 EntryIndex = Profile->Flipbooks.IndexOfByPredicate(
					[&OldProfileName](const FFlipbookProfileEntry& Candidate)
					{
						return Candidate.Identity.FlipbookName.Equals(OldProfileName, ESearchCase::IgnoreCase);
					});
				if (EntryIndex != INDEX_NONE)
				{
					Profile->SetFlags(RF_Transactional);
					Profile->Modify();
					bProfileRenamed = Profile->RenameFlipbookAndPropagate(EntryIndex, NewProfileName);
					if (!bProfileRenamed)
					{
						// The funnel refuses on a case-insensitive collision with another entry.
						// Put the layer-asset rows back and degrade — never leave the two halves
						// of one animation disagreeing about its name.
						FAsepriteStructuralDiff::RenameAnimationOnLayers(
							LayerAsset.Layers, Rename.NewName, Rename.OldName, nullptr);
						Report.DegradedRenames.Add(FString::Printf(
							TEXT("Animation '%s' was NOT renamed to '%s': the Character Profile already has an entry named '%s'. Treated as a delete plus an add; nothing was destroyed."),
							*Rename.OldName, *Rename.NewName, *NewProfileName));
						continue;
					}
					Profile->MarkPackageDirty();
				}
			}

			// Disk last. A failure here degrades to keep-both and is reported; the data edits above
			// stand, and the pipeline recreates the flipbook under the new name below.
			if (bCanRenameOnDisk)
			{
				const FString OldFlipbookName = FlipbookAssetNameForTag(OldAssetPrefix, Rename.OldName);
				const FString NewFlipbookName = FlipbookAssetNameForTag(Settings.AssetPrefix, Rename.NewName);
				const FString OldFlipbookPackage = FlipbookFolder / OldFlipbookName;
				FString ActualPath;
				FString FailureReason;
				if (!RenameGeneratedAsset(
					FSoftObjectPath(OldFlipbookPackage + TEXT(".") + OldFlipbookName),
					FlipbookFolder, NewFlipbookName, ActualPath, FailureReason))
				{
					Report.DegradedRenames.Add(FString::Printf(
						TEXT("Animation '%s' was renamed to '%s', but its flipbook asset stayed put (%s). The new flipbook is created alongside it and the old one is reported as an orphan."),
						*Rename.OldName, *Rename.NewName, *FailureReason));
					Report.OrphanedAssets.AddUnique(OldFlipbookPackage);
				}
			}
			else
			{
				Report.DegradedRenames.Add(FString::Printf(
					TEXT("Animation '%s' was renamed to '%s' in data only — the asset registry is still scanning, so its flipbook could not be moved on disk."),
					*Rename.OldName, *Rename.NewName));
			}

			Report.TagRenames.Add(Rename);
		}

		// ---- Removals: keep authored or shared work, drop only pristine generated rows.
		for (const FAseDiffRemoval& Removal : Diff.Removals)
		{
			const FString OldFlipbookName = FlipbookAssetNameForTag(OldAssetPrefix, Removal.Name);
			const FString OldFlipbookPackage = FlipbookFolder / OldFlipbookName;

			switch (Removal.Disposition)
			{
			case EAseDiffRemovalDisposition::KeepSharedWithOtherSource:
				Report.KeptItems.Add(FString::Printf(
					TEXT("Animation '%s' is gone from this .ase but another source file of this Layer Profile still contributes it — kept. Reimport that file to refresh its art."),
					*Removal.Name));
				break;

			case EAseDiffRemovalDisposition::KeepAuthoredData:
				Report.KeptItems.Add(FString::Printf(
					TEXT("Animation '%s' is gone from this .ase but carries authored data (gameplay, timing, placement or tag membership) — kept, and its flipbook is left on disk."),
					*Removal.Name));
				break;

			case EAseDiffRemovalDisposition::RemoveClean:
			default:
				MarkLayerAssetModified(LayerAsset, bInOutLayerAssetModified);
				FAsepriteStructuralDiff::RemoveAnimationFromLayers(LayerAsset.Layers, Removal.Name);
				if (Profile)
				{
					const FString OldProfileName = ProfileAnimNameForTag(OldAssetPrefix, Removal.Name);
					const int32 EntryIndex = Profile->Flipbooks.IndexOfByPredicate(
						[&OldProfileName](const FFlipbookProfileEntry& Candidate)
						{
							return Candidate.Identity.FlipbookName.Equals(OldProfileName, ESearchCase::IgnoreCase);
						});
					if (EntryIndex != INDEX_NONE)
					{
						Profile->SetFlags(RF_Transactional);
						Profile->Modify();
						Profile->Flipbooks.RemoveAt(EntryIndex);
						Profile->MarkPackageDirty();
					}
				}
				Report.RemovedItems.Add(FString::Printf(
					TEXT("Animation '%s' is gone from this .ase and carried no authored data — its generated rows were dropped."),
					*Removal.Name));
				Report.OrphanedAssets.AddUnique(OldFlipbookPackage);
				break;
			}
		}
#endif // WITH_EDITORONLY_DATA
	}

	/**
	 * PHASE B — LAYERS. Runs after the normal-map pairing is known and BEFORE the per-layer
	 * materialisation, so a renamed layer's sheet and sprites move first and every FindOrCreate
	 * below refreshes those same packages. Run it after materialisation instead and the new assets
	 * already exist under the new name, the old ones are stranded, and the layer is duplicated on
	 * the asset — today's behaviour, and the reason this phase exists.
	 */
	static void ApplyLayerDiff(
		UPaper2DPlusCharacterLayerAsset& LayerAsset,
		const FAsepriteParsedData& ParsedData,
		const TMap<int32, TArray<TArray<FColor>>>& PerLayerBuffers,
		const TArray<int32>& SortedImportLayerIndices,
		const TSet<int32>& PairedNormalIndices,
		const FAsepriteLayerImportSettings& Settings,
		const FString& StoredSourcePath,
		bool& bInOutLayerAssetModified,
		FAseDiffApplyReport& Report)
	{
#if WITH_EDITORONLY_DATA
		const FAsepriteSourceContext* Recorded = FindRecordedSource(LayerAsset, StoredSourcePath);
		if (!Recorded || Recorded->LayerContentHashes.Num() == 0)
		{
			return; // first import of this source, or a legacy asset that was never stamped
		}
		const TSet<FString> SiblingLayers =
			CollectSiblingContributedNames(LayerAsset, StoredSourcePath, /*bTags=*/false);

		TArray<FAseDiffOldItem> OldItems;
		OldItems.Reserve(Recorded->LayerContentHashes.Num());
		for (const TPair<FString, FString>& Pair : Recorded->LayerContentHashes)
		{
			FAseDiffOldItem Item;
			Item.Name = Pair.Key;
			Item.ContentHash = Pair.Value;
			Item.bSharedWithOtherSources = SiblingLayers.Contains(Pair.Key);
			if (const FCharacterLayer* Existing = LayerAsset.Layers.FindByPredicate(
				[&Pair](const FCharacterLayer& Layer)
				{
					return Layer.LayerName.Equals(Pair.Key, ESearchCase::IgnoreCase);
				}))
			{
				Item.bHasAuthoredData =
					FAsepriteStructuralDiff::HasAuthoredLayerData(*Existing, LayerAsset.AppearancePresets);
			}
			OldItems.Add(MoveTemp(Item));
		}

		// New side: exactly the layers the materialisation loop will actually create.
		TArray<FAseDiffNewItem> NewItems;
		TMap<FString, int32> NewNameToLayerIndex;
		for (const int32 LayerIdx : SortedImportLayerIndices)
		{
			const bool* bEnabled = Settings.LayerImportEnabled.Find(LayerIdx);
			if (!bEnabled || !*bEnabled || PairedNormalIndices.Contains(LayerIdx)
				|| !ParsedData.Layers.IsValidIndex(LayerIdx))
			{
				continue;
			}
			const TArray<TArray<FColor>>* Buffers = PerLayerBuffers.Find(LayerIdx);
			if (!Buffers || Buffers->Num() == 0)
			{
				continue;
			}
			FString LayerPath, SanitizedLeaf, SheetSubPath, SpriteSubPath;
			DeriveLayerOutputNames(ParsedData, LayerIdx, Settings, LayerPath, SanitizedLeaf, SheetSubPath, SpriteSubPath);

			FAseDiffNewItem Item;
			Item.Name = LayerPath;
			Item.ContentHash = FAsepriteStructuralDiff::ComputeLayerBuffersHash(
				*Buffers, ParsedData.Width, ParsedData.Height);
			Item.bCollidesWithExistingItem = SiblingLayers.Contains(LayerPath);
			NewNameToLayerIndex.Add(LayerPath, LayerIdx);
			NewItems.Add(MoveTemp(Item));
		}

		const FAseDiffResult Diff = FAsepriteStructuralDiff::DiffItems(OldItems, NewItems);
		if (Diff.Renames.Num() == 0 && Diff.Removals.Num() == 0)
		{
			return; // pure refresh — leave the package undirtied (R13)
		}

		const bool bCanRenameOnDisk = IsAssetRegistryReadyForRename();

		for (const FAseDiffRename& Rename : Diff.Renames)
		{
			// The DECISION is a pure seam (testable without an import); the WRITE stays down below,
			// because there is still work between deciding and writing and an early return in that
			// stretch must not leave a half-renamed layer behind.
			int32 ExistingIndex = INDEX_NONE;
			const EAseLayerRenamePlan RenamePlan = FAsepriteStructuralDiff::PlanLayerRename(
				LayerAsset.Layers, Rename.OldName, Rename.NewName, &ExistingIndex);
			if (RenamePlan == EAseLayerRenamePlan::OldNameNotFound)
			{
				// The stamp named a layer the asset no longer holds; the ordinary add path handles it.
				continue;
			}
			if (RenamePlan == EAseLayerRenamePlan::NewNameAlreadyOwned)
			{
				Report.DegradedRenames.Add(FString::Printf(
					TEXT("Layer '%s' was NOT renamed to '%s': this Layer Profile already has a layer under the new name. Treated as a delete plus an add; nothing was destroyed."),
					*Rename.OldName, *Rename.NewName));
				continue;
			}
			FCharacterLayer* Existing = &LayerAsset.Layers[ExistingIndex];

			const int32* NewLayerIdx = NewNameToLayerIndex.Find(Rename.NewName);
			if (!NewLayerIdx)
			{
				continue;
			}
			FString NewLayerPath, NewSanitizedLeaf, NewSheetSubPath, NewSpriteSubPath;
			DeriveLayerOutputNames(ParsedData, *NewLayerIdx, Settings,
				NewLayerPath, NewSanitizedLeaf, NewSheetSubPath, NewSpriteSubPath);

			// ---- Data first: rebind in place. The stable LayerId never moves, so preset membership,
			// exclusive-group binding, placement, and layer-local gameplay all follow by construction.
			MarkLayerAssetModified(LayerAsset, bInOutLayerAssetModified);
			Existing->LayerName = Rename.NewName;

			// ---- Disk second, from the RECORDED asset paths (authoritative — no re-derivation of
			// where the previous import happened to put them).
			if (bCanRenameOnDisk)
			{
				const FString NewSheetName = Settings.AssetPrefix + TEXT("_") + NewSanitizedLeaf + TEXT("_Sheet");
				const FSoftObjectPath OldSheetPath = Existing->SourceTexture.ToSoftObjectPath();
				FString ActualSheetPath, SheetFailure;
				if (!OldSheetPath.IsNull()
					&& !RenameGeneratedAsset(OldSheetPath, NewSheetSubPath, NewSheetName, ActualSheetPath, SheetFailure))
				{
					Report.DegradedRenames.Add(FString::Printf(
						TEXT("Layer '%s' was renamed to '%s', but its sheet texture stayed put (%s) and is reported as an orphan."),
						*Rename.OldName, *Rename.NewName, *SheetFailure));
					Report.OrphanedAssets.AddUnique(OldSheetPath.GetLongPackageName());
				}
				else if (!OldSheetPath.IsNull())
				{
					// TASK-72's paired normal sheet is "<sheet>_N" beside it; move it too or it is
					// stranded under the old layer's name.
					const FString OldNormalPackage = OldSheetPath.GetLongPackageName() + TEXT("_N");
					const FString OldNormalAsset = FPackageName::GetShortName(OldNormalPackage);
					if (FPackageName::DoesPackageExist(OldNormalPackage))
					{
						FString ActualNormalPath, NormalFailure;
						RenameGeneratedAsset(
							FSoftObjectPath(OldNormalPackage + TEXT(".") + OldNormalAsset),
							NewSheetSubPath, NewSheetName + TEXT("_N"), ActualNormalPath, NormalFailure);
					}
				}

				// Per-frame sprites are shared across this layer's animation mappings — dedupe by
				// path, and carry each one's own "_NN" suffix across to the new prefix.
				const FString NewSpritePrefix = Settings.AssetPrefix + TEXT("_") + NewSanitizedLeaf;
				TSet<FString> SeenSpritePaths;
				for (const FCharacterLayerAnimationMapping& Mapping : Existing->AnimationSprites)
				{
					for (const TSoftObjectPtr<UPaperSprite>& SpriteRef : Mapping.Sprites)
					{
						const FSoftObjectPath SpritePath = SpriteRef.ToSoftObjectPath();
						if (SpritePath.IsNull() || SeenSpritePaths.Contains(SpritePath.ToString()))
						{
							continue;
						}
						SeenSpritePaths.Add(SpritePath.ToString());

						const FString OldSpriteName = SpritePath.GetAssetName();
						int32 SuffixStart = INDEX_NONE;
						if (!OldSpriteName.FindLastChar(TEXT('_'), SuffixStart))
						{
							continue; // not one of ours; leave it alone
						}
						const FString NewSpriteName = NewSpritePrefix + OldSpriteName.RightChop(SuffixStart);
						FString ActualSpritePath, SpriteFailure;
						if (!RenameGeneratedAsset(SpritePath, NewSpriteSubPath, NewSpriteName, ActualSpritePath, SpriteFailure))
						{
							Report.OrphanedAssets.AddUnique(SpritePath.GetLongPackageName());
						}
					}
				}
			}
			else
			{
				Report.DegradedRenames.Add(FString::Printf(
					TEXT("Layer '%s' was renamed to '%s' in data only — the asset registry is still scanning, so its generated assets could not be moved on disk."),
					*Rename.OldName, *Rename.NewName));
			}

			Report.LayerRenames.Add(Rename);
		}

		// ---- Removals.
		for (const FAseDiffRemoval& Removal : Diff.Removals)
		{
			switch (Removal.Disposition)
			{
			case EAseDiffRemovalDisposition::KeepSharedWithOtherSource:
				Report.KeptItems.Add(FString::Printf(
					TEXT("Layer '%s' is gone from this .ase but another source file of this Layer Profile still contributes it — kept. Reimport that file to refresh its art."),
					*Removal.Name));
				break;

			case EAseDiffRemovalDisposition::KeepAuthoredData:
				Report.KeptItems.Add(FString::Printf(
					TEXT("Layer '%s' is gone from this .ase but carries authored data (group, authored animations, placement, exclusive group or preset membership) — kept, and its generated assets are left on disk."),
					*Removal.Name));
				break;

			case EAseDiffRemovalDisposition::RemoveClean:
			default:
			{
				// Collect the backing packages BEFORE dropping the entry — they are reported as
				// orphans, never deleted (R11), and the entry is the only record of where they are.
				if (const FCharacterLayer* Doomed = LayerAsset.Layers.FindByPredicate(
					[&Removal](const FCharacterLayer& Layer)
					{
						return Layer.LayerName.Equals(Removal.Name, ESearchCase::IgnoreCase);
					}))
				{
					const FSoftObjectPath SheetPath = Doomed->SourceTexture.ToSoftObjectPath();
					if (!SheetPath.IsNull())
					{
						Report.OrphanedAssets.AddUnique(SheetPath.GetLongPackageName());
					}
					for (const FCharacterLayerAnimationMapping& Mapping : Doomed->AnimationSprites)
					{
						for (const TSoftObjectPtr<UPaperSprite>& SpriteRef : Mapping.Sprites)
						{
							const FSoftObjectPath SpritePath = SpriteRef.ToSoftObjectPath();
							if (!SpritePath.IsNull())
							{
								Report.OrphanedAssets.AddUnique(SpritePath.GetLongPackageName());
							}
						}
					}
				}
				MarkLayerAssetModified(LayerAsset, bInOutLayerAssetModified);
				if (FAsepriteStructuralDiff::RemoveLayerByName(
					LayerAsset.Layers, LayerAsset.AppearancePresets, Removal.Name))
				{
					Report.RemovedItems.Add(FString::Printf(
						TEXT("Layer '%s' is gone from this .ase and carried no authored data — its generated rows were dropped."),
						*Removal.Name));
				}
				break;
			}
			}
		}
#endif // WITH_EDITORONLY_DATA
	}
}

// ============================================
// ImportAsLayeredAsset
// ============================================

UObject* FAsepriteImporter::ImportAsLayeredAsset(
	FAsepriteParsedData& ParsedData,
	const TMap<int32, TArray<TArray<FColor>>>& PerLayerBuffers,
	const FAsepriteLayerImportSettings& Settings)
{
	// TASK-192 U2: an outer scope (the watcher's) absorbs this; a direct call owns its own report.
	FAsepriteImportCostScope CostScope;

	// Per-tag import selection (dialog checkboxes; TagImportEnabled keys are indices into the file's
	// AUTHORED tag order). Build the disabled set FIRST — the new-profile branch's ImportFile re-parses
	// the file and applies the set against that same authored order — then filter THIS parse's Tags in
	// place so every downstream consumer (per-layer animation mappings, separate-mode flipbooks, the
	// hitbox conflict count and delivery) sees only the enabled tags. Empty/all-true map = no-op.
	TSet<int32> DisabledTagIndices;
	for (const TPair<int32, bool>& TagPair : Settings.TagImportEnabled)
	{
		if (!TagPair.Value)
		{
			DisabledTagIndices.Add(TagPair.Key);
		}
	}

	// Capture the de-selected tag NAMES before the filter destroys the indexing — they persist on the
	// Layer Profile so the watcher's full auto-reimport can re-apply the same selection by name.
	TArray<FString> DisabledTagNames;
	for (int32 TagIdx = 0; TagIdx < ParsedData.Tags.Num(); ++TagIdx)
	{
		if (DisabledTagIndices.Contains(TagIdx))
		{
			DisabledTagNames.Add(ParsedData.Tags[TagIdx].Name);
		}
	}

	if (DisabledTagIndices.Num() > 0)
	{
		FilterTagsByDisabledIndices(ParsedData.Tags, DisabledTagIndices);
	}

	// TASK-183 "keep source in project": copy the .ase beside its generated assets so the artist can
	// commit it with the project and every synced machine resolves the same source. Runs in every
	// import mode (the copy is the shareable artifact); only Layer-asset modes also TRACK it below.
	// /Temp output targets (headless/automation) never copy.
	FString EffectiveSourceFilePath = Settings.SourceFilePath;
	if (Settings.bKeepSourceInProject && !Settings.SourceFilePath.IsEmpty()
		&& !Settings.OutputPath.StartsWith(TEXT("/Temp")))
	{
		EffectiveSourceFilePath = CopySourceAseIntoProject(Settings.SourceFilePath, Settings.OutputPath);
	}

	// --- TASK-189 U4: structural diff, PHASE A (tags) ---
	// This has to happen before ANY pipeline write. The profile population below matches entries by
	// name, so a tag the artist renamed in Aseprite would mint a second entry beside the authored
	// one and strand every combat/timing/cue edit on the dead name. Resolving the Layer Profile here
	// never creates it: a first import must stay a first import.
	FAseDiffApplyReport DiffReport;
	bool bDiffModifiedLayerAsset = false;
	UPaper2DPlusCharacterLayerAsset* DiffLayerAsset = nullptr;
	const FString DiffStoredSourcePath = MakeStoredAsePath(EffectiveSourceFilePath);
	if (Settings.ImportMode != EAsepriteImportMode::SeparateAssetsPerLayer && !DiffStoredSourcePath.IsEmpty())
	{
		DiffLayerAsset = AsepriteDiffApply::ResolveExistingLayerAsset(Settings);
		if (DiffLayerAsset)
		{
			UPaper2DPlusCharacterProfileAsset* DiffProfile = Settings.ExistingProfile.LoadSynchronous();
			if (!DiffProfile)
			{
				DiffProfile = DiffLayerAsset->BaseProfile.LoadSynchronous();
			}
			AsepriteDiffApply::ApplyTagDiff(
				*DiffLayerAsset, DiffProfile, ParsedData, Settings, DiffStoredSourcePath,
				bDiffModifiedLayerAsset, DiffReport);
		}
	}

	// --- TASK-192 U4/U5/U7: the incremental gate context for this source ---
	// Stamps live on the loaded Layer Profile's per-source context (KTD1); a first import has none
	// and every gate fails closed to "write". The current grid is this parse's frames + canvas; any
	// disagreement with the stamp short-circuits every content comparison. Hoisted above the
	// profile branch because ImportFile's flipbook gates read it too.
	const FAsepriteSourceContext* GateSourceContext =
		DiffLayerAsset ? DiffLayerAsset->FindAseSourceContext(DiffStoredSourcePath) : nullptr;
	FAseStampedGrid StampedGrid;
	if (GateSourceContext)
	{
		StampedGrid.FrameCount = GateSourceContext->StampedFrameCount;
		StampedGrid.CanvasWidth = GateSourceContext->StampedCanvasWidth;
		StampedGrid.CanvasHeight = GateSourceContext->StampedCanvasHeight;
	}
	FAseStampedGrid CurrentGrid;
	CurrentGrid.FrameCount = ParsedData.Frames.Num();
	CurrentGrid.CanvasWidth = ParsedData.Width;
	CurrentGrid.CanvasHeight = ParsedData.Height;
	const bool bForceFullReimport = Settings.bForceFullReimport;
	FAsepriteIncrementalImportContext IncrementalContext;
	IncrementalContext.SourceContext = GateSourceContext;
	IncrementalContext.bForceFullReimport = bForceFullReimport;
	IncrementalContext.StampedGrid = StampedGrid;
	IncrementalContext.CurrentGrid = CurrentGrid;

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

	// UNIFIED profile pipeline (TASK-184): the standard import (sheet + sprites + per-tag flipbooks)
	// runs for BOTH profile modes — picking an existing profile used to silently skip flipbook
	// creation, which read as "the import made nothing". What the picked profile's CONTENT decides
	// is only how entries land on it:
	//   - created or EMPTY profile → full regenerate (wipe+repopulate, Overwrite hitboxes) — a
	//     just-created "MainCharProfile" the user picked behaves exactly like a new one;
	//   - POPULATED profile → ADDITIVE: new animations append, same-name entries refresh their art,
	//     everything else (other files' animations, authored combat data) is untouched, and hitbox
	//     delivery keeps the least-destructive Apply default with the 3-way conflict prompt.
	// The multi-file character workflow follows: import five files against one profile and it
	// accumulates all their animations.
	if (Settings.ImportMode != EAsepriteImportMode::SeparateAssetsPerLayer
		&& !Settings.SourceFilePath.IsEmpty())
	{
		// (double-parse accepted — the re-parse keeps DisabledTagIndices addressing the file's
		// authored tag order; it is load-bearing, not redundant)
		FAsepriteImportResult ProfileResult = FAsepriteImporter::ImportFile(
			Settings.SourceFilePath, Settings.OutputPath, Settings.AssetPrefix,
			DisabledTagIndices.Num() > 0 ? &DisabledTagIndices : nullptr,
			Settings.bOrganizeIntoSubfolders,
			&IncrementalContext);

		if (ProfileResult.bSuccess)
		{
			bool bCreatedProfile = false;
			// TASK-192 U7: only a pass that actually changed profile rows (or delivered hitboxes)
			// may dirty the Character Profile package — an all-skip reimport leaves it clean.
			bool bProfileRowsMutated = false;
			UPackage* ProfilePackage = nullptr;

			if (Settings.ImportMode == EAsepriteImportMode::LayerAssetExistingProfile)
			{
				Profile = Settings.ExistingProfile.LoadSynchronous();
				if (Profile)
				{
					ProfilePackage = Profile->GetOutermost();
				}
			}

			if (!Profile)
			{
				// Create (or reuse) "<prefix>_Profile" at the output root
				FString ProfileAssetName = Settings.AssetPrefix + TEXT("_Profile");
				FString ProfilePackageName = Settings.OutputPath / ProfileAssetName;
				ProfilePackage = CreatePackage(*ProfilePackageName);
				if (ProfilePackage)
				{
					Profile = FindOrCreateAssetInPackage<UPaper2DPlusCharacterProfileAsset>(
						ProfilePackage, ProfileAssetName, bCreatedProfile);
				}
			}

			if (Profile)
			{
				FAsepriteImportCostPhaseTimer ProfilePhase(EAsepriteImportCostPhase::Profile);
				if (bCreatedProfile || Profile->Flipbooks.IsEmpty())
				{
					// Modify() → wipe → repopulate → hitbox transfer (Overwrite — nothing to lose).
					// Extracted seam (headless-tested); the Modify-before-wipe inside is the U1
					// reconcile-signal fix.
					FAsepriteImporter::RegenerateProfileFromImportResult(
						Profile, ProfileResult, ParsedData.Tags, Settings.AssetPrefix, bCreatedProfile);
					AsepriteImportCost::AddProfileEntries(Profile->Flipbooks.Num(), /*bWritten*/ true);
					bProfileRowsMutated = true;
				}
				else
				{
					int32 EntriesAdded = 0;
					int32 EntriesRefreshed = 0;
					int32 EntriesSkipped = 0;
					FAsepriteImporter::AppendProfileEntriesFromImportResult(
						Profile, ProfileResult, Settings.AssetPrefix, &EntriesAdded, &EntriesRefreshed, &EntriesSkipped);
					AsepriteImportCost::AddProfileEntries(EntriesAdded + EntriesRefreshed, /*bWritten*/ true);
					AsepriteImportCost::AddProfileEntries(EntriesSkipped, /*bWritten*/ false);
					bProfileRowsMutated = bProfileRowsMutated || (EntriesAdded + EntriesRefreshed) > 0;
					UE_LOG(LogTemp, Log, TEXT("ImportAsLayeredAsset: additively populated profile '%s' (%d animations added, %d refreshed, %d unchanged)."),
						*Profile->GetName(), EntriesAdded, EntriesRefreshed, EntriesSkipped);

					// Hitbox delivery onto a populated profile: least-destructive Apply default;
					// surface the 3-way Merge/Overwrite/Apply prompt only when occupied frames
					// would actually be overwritten and we're attended.
					if (ParsedData.ExtractedFrameData.Num() > 0)
					{
						EHitboxApplyPolicy ChosenPolicy = EHitboxApplyPolicy::Apply;

						// Treat in-editor automation / ECABridge-driven imports as headless too — never block on the modal.
						const bool bHeadless = FApp::IsUnattended() || IsRunningCommandlet() || GIsAutomationTesting;

						const int32 ConflictCount = FAsepriteImporter::CountHitboxConflicts(
							Profile, ParsedData.ExtractedFrameData, ParsedData.Tags, Settings.AssetPrefix,
							/*bAllowSoleEntryFallback*/ false);

						if (!bHeadless && ConflictCount > 0)
						{
							ChosenPolicy = SHitboxConflictDialog::ShowDialog(ConflictCount, EHitboxApplyPolicy::Apply);
						}

						FAsepriteImporter::TransferHitboxDataToProfile(
							Profile, ParsedData.ExtractedFrameData, ParsedData.Tags, Settings.AssetPrefix, ChosenPolicy,
							/*OutUndeliveredTags*/ nullptr, /*bAllowSoleEntryFallback*/ false);
						bProfileRowsMutated = true;
					}
				}

				if (ProfilePackage && (bCreatedProfile || bProfileRowsMutated)
					&& !FPackageName::IsTempPackage(ProfilePackage->GetName()))
				{
					ProfilePackage->MarkPackageDirty();
					if (bCreatedProfile)
					{
						FAssetRegistryModule::AssetCreated(Profile);
					}
				}
			}
		}
	}
	// SeparateAssetsPerLayer mode does not create or reference a profile

	if (Progress.ShouldCancel())
	{
		return Profile; // Return whatever we created so far
	}

	// --- Step 2: Per-layer asset creation ---
	TArray<FCharacterLayer> CreatedLayers;
	// TASK-189: layer FULL path -> MD5 of that layer's OWN composited pixels. Stamped into this
	// source's FAsepriteSourceContext below and consumed by the structural-diff rename pairing —
	// and, since TASK-192 U4, doubling as each layer sheet's incremental write gate.
	TMap<FString, FString> LayerContentHashes;
	// TASK-192 U4: BASE layer path -> the paired NORMAL layer's own pixel hash (the `_Sheet_N` gate;
	// the per-layer loop skips paired normals before LayerContentHashes can cover them).
	TMap<FString, FString> NewNormalLayerHashes;
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

	// --- TASK-189 U4: structural diff, PHASE B (layers) ---
	// Renames must land BEFORE materialisation: every creator below is FindOrCreate-in-place, so a
	// renamed layer whose assets moved first is refreshed in the same packages (references survive
	// through the redirector). Run this after the loop instead and the new assets already exist
	// under the new name, the old ones are stranded, and the layer duplicates on the asset.
	if (DiffLayerAsset)
	{
		AsepriteDiffApply::ApplyLayerDiff(
			*DiffLayerAsset, ParsedData, PerLayerBuffers, SortedImportLayerIndices, PairedNormalIndices,
			Settings, DiffStoredSourcePath, bDiffModifiedLayerAsset, DiffReport);
	}

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

		// Identity + output folders come from the ONE shared derivation, so the rename phase above
		// cannot put an asset anywhere this loop then fails to find.
		FString LayerPath;
		FString SanitizedLayerName;
		FString SheetSubPath;
		FString SpriteSubPath;
		AsepriteDiffApply::DeriveLayerOutputNames(
			ParsedData, LayerIdx, Settings, LayerPath, SanitizedLayerName, SheetSubPath, SpriteSubPath);

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

		// TASK-192 U4: the layer sheet's own gate. The layer hash doubles as the stamp written below,
		// so the gate and the stamp can never disagree about what they cover.
		const FString LayerHash = FAsepriteStructuralDiff::ComputeLayerBuffersHash(
			*LayerBuffers, ParsedData.Width, ParsedData.Height);
		const FString StoredLayerHash = GateSourceContext
			? GateSourceContext->LayerContentHashes.FindRef(LayerPath)
			: FString();
		const FString SheetPackageName = SheetSubPath / TextureName;
		const FAseWriteDecision SheetDecision = FAsepriteIncrementalWrite::ShouldWriteSheet(
			SheetPackageName, StoredLayerHash, LayerHash, StampedGrid, CurrentGrid, bForceFullReimport);

		UTexture2D* LayerTexture = nullptr;
		bool bSheetCreatedObject = false;
		if (SheetDecision.ShouldWrite())
		{
			LayerTexture = CreatePerLayerSpriteSheetTexture(
				*LayerBuffers, ParsedData.Width, ParsedData.Height, SheetSubPath, TextureName,
				&bSheetCreatedObject);
			if (!LayerTexture)
			{
				UE_LOG(LogTemp, Warning, TEXT("ImportAsLayeredAsset: Failed to create texture for layer '%s'"), *Layer.Name);
				continue;
			}
			TexturesCreated++;
		}
		else
		{
			AsepriteImportCost::AddSheets(1, /*bWritten*/ false);
			// KTD9: a RESIDENT skipped sheet still gets its cheap settings reconciled — drifted
			// settings change the DDC key, so a repair pays one UpdateResource and one dirty. A
			// non-resident skipped sheet has nothing in memory to drift.
			if (UTexture2D* ResidentSheet = FindObject<UTexture2D>(
				nullptr, *(SheetPackageName + TEXT(".") + TextureName)))
			{
				if (FAsepriteIncrementalWrite::ReconcileSheetSettings(ResidentSheet, /*bIsNormalMap*/ false))
				{
					ResidentSheet->UpdateResource();
					if (UPackage* SheetPackage = ResidentSheet->GetOutermost();
						SheetPackage && !FPackageName::IsTempPackage(SheetPackage->GetName()))
					{
						SheetPackage->MarkPackageDirty();
					}
					UE_LOG(LogTemp, Log, TEXT("Aseprite incremental: repaired drifted settings on skipped sheet '%s'"), *TextureName);
				}
				LayerTexture = ResidentSheet; // resident — keep downstream pointers hot
			}
		}

		// Create (or reconcile) per-layer sprites against the sheet verdict (TASK-192 U5).
		FAseSpriteWritePlan SpritePlan;
		SpritePlan.SheetDecision = SheetDecision;
		SpritePlan.bSheetObjectRecreated = bSheetCreatedObject && GateSourceContext != nullptr;
		SpritePlan.StampedGrid = StampedGrid;
		SpritePlan.CurrentGrid = CurrentGrid;
		SpritePlan.bForceFullReimport = bForceFullReimport;
		SpritePlan.FrameBuffers = LayerBuffers;
		SpritePlan.SheetObjectPath = SheetPackageName + TEXT(".") + TextureName;

		FAseSpriteWriteOutcome SpriteOutcome;
		TArray<FCharacterLayerAnimationMapping> Mappings = CreatePerLayerSprites(
			LayerTexture, ParsedData, SpriteSubPath, SpritePrefix, &SpritePlan, &SpriteOutcome);
		SpritesCreated += SpriteOutcome.SpritesWritten;

		// --- TASK-72: pair a normal-map layer onto this base layer's sprites ---
		// If this base layer has a paired normal layer, composite the NORMAL layer's pixels into a normal sheet (same
		// grid/frame dims as the base, retagged TC_Normalmap / SRGB-off / WorldNormalMap) and attach it to each of this
		// layer's generated base sprites' secondary texture slot (AdditionalTexture0), with the null-safe lit material.
		if (const int32* NormalLayerIdxPtr = BaseToNormal.Find(LayerIdx))
		{
			const TArray<TArray<FColor>>* NormalBuffers = PerLayerBuffers.Find(*NormalLayerIdxPtr);
			if (NormalBuffers && NormalBuffers->Num() > 0)
			{
				// TASK-192 U4: the paired normal sheet gets its own stamp — the per-layer loop
				// `continue`s past paired normals before LayerContentHashes can cover them, so
				// without this a `_Sheet_N` would either always rebuild or silently ship stale.
				const FString NormalTextureName = TextureName + TEXT("_N");
				const FString NormalHash = FAsepriteStructuralDiff::ComputeLayerBuffersHash(
					*NormalBuffers, ParsedData.Width, ParsedData.Height);
				const FString StoredNormalHash = GateSourceContext
					? GateSourceContext->NormalLayerContentHashes.FindRef(LayerPath)
					: FString();
				const FAseWriteDecision NormalDecision = FAsepriteIncrementalWrite::ShouldWriteSheet(
					SheetSubPath / NormalTextureName, StoredNormalHash, NormalHash,
					StampedGrid, CurrentGrid, bForceFullReimport);

				UTexture2D* NormalTexture = nullptr;
				if (NormalDecision.ShouldWrite())
				{
					NormalTexture = CreateNormalMapSheetTexture(
						*NormalBuffers, ParsedData.Width, ParsedData.Height, SheetSubPath, NormalTextureName);
				}
				else
				{
					AsepriteImportCost::AddSheets(1, /*bWritten*/ false);
				}

				// The attach can matter only when the normal sheet was (re)written, or when a base
				// sprite took a FULL InitializeSprite this pass (which reset its additional-texture
				// list). The attach itself skips sprites already attached identically, so a fully
				// covered reimport dirties zero sprite packages here.
				const bool bAttachNeeded =
					(NormalTexture != nullptr) || SpriteOutcome.FullyInitializedFrames.Num() > 0;
				if (bAttachNeeded)
				{
					if (!NormalTexture)
					{
						NormalTexture = LoadObject<UTexture2D>(nullptr,
							*(SheetSubPath / NormalTextureName + TEXT(".") + NormalTextureName));
					}
					if (NormalTexture)
					{
						// Gather the unique base sprites from the mappings (CreatePerLayerSprites shares one
						// sprite per frame across animation mappings, so dedupe).
						TArray<UPaperSprite*> BaseSprites;
						TSet<UPaperSprite*> SeenSprites;
						for (const FCharacterLayerAnimationMapping& NormalSpriteMapping : Mappings)
						{
							for (const TSoftObjectPtr<UPaperSprite>& SpriteRef : NormalSpriteMapping.Sprites)
							{
								UPaperSprite* Sprite = SpriteRef.LoadSynchronous();
								if (Sprite && !SeenSprites.Contains(Sprite))
								{
									SeenSprites.Add(Sprite);
									BaseSprites.Add(Sprite);
								}
							}
						}

						int32 SpritesTouched = 0;
						AttachNormalMapToSprites(BaseSprites, NormalTexture, ResolvedLitMaterial, SpritesTouched,
							/*bSkipAlreadyAttached*/ true);
						if (NormalDecision.ShouldWrite())
						{
							NormalSheetsCreated++;
						}
						UE_LOG(LogTemp, Log, TEXT("AsepriteImporter (TASK-72): paired normal layer '%s' onto base layer '%s' (%d sprite(s) re-attached, lit material: %s)."),
							ParsedData.Layers.IsValidIndex(*NormalLayerIdxPtr) ? *ParsedData.Layers[*NormalLayerIdxPtr].Name : TEXT("?"),
							*Layer.Name, SpritesTouched, ResolvedLitMaterial ? TEXT("set") : TEXT("none"));
					}
				}
				NewNormalLayerHashes.Add(LayerPath, NormalHash);
			}
		}

		// Build the FCharacterLayer
		FCharacterLayer CharLayer;
		CharLayer.LayerName = LayerPath;
		if (LayerTexture)
		{
			CharLayer.SourceTexture = LayerTexture;
		}
		else
		{
			// Skipped, non-resident sheet: the soft path is identity enough — nothing downstream
			// needs the object, and loading it would defeat the gate (TASK-192 U4).
			CharLayer.SourceTexture = TSoftObjectPtr<UTexture2D>(FSoftObjectPath(SpritePlan.SheetObjectPath));
		}
		CharLayer.AnimationSprites = MoveTemp(Mappings);

		CreatedLayers.Add(MoveTemp(CharLayer));

		// TASK-189: stamp this layer's OWN pixel identity (its cels only, across every frame), so a
		// later reimport can pair a RENAMED layer by content instead of guessing from names. The
		// same value fed this layer's write gate above (TASK-192 U4).
		LayerContentHashes.Add(LayerPath, LayerHash);

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
				FString FlipbookPackagePath = Settings.bOrganizeIntoSubfolders
					? Settings.OutputPath / TEXT("Flipbooks") / FlipbookName
					: Settings.OutputPath / Settings.AssetPrefix / FlipbookName;
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
					AsepriteImportCost::AddFlipbooks(1, /*bWritten*/ true);
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
			UPackage* LayerAssetPackage = nullptr;
			bool bCreatedLayerAsset = false;

			// Dialog Layer-Asset picker: import into the PICKED existing asset instead of deriving
			// "<prefix>_Layers" — the additive-reimport branch below handles it like any existing asset.
			if (!Settings.ExistingLayerAsset.IsNull())
			{
				LayerAsset = Settings.ExistingLayerAsset.LoadSynchronous();
				if (LayerAsset)
				{
					LayerAssetPackage = LayerAsset->GetOutermost();
					LayerAssetPackageName = LayerAssetPackage->GetName();
				}
				else
				{
					UE_LOG(LogTemp, Warning,
						TEXT("ImportAsLayeredAsset: picked Layer asset '%s' failed to load; falling back to '%s'."),
						*Settings.ExistingLayerAsset.ToSoftObjectPath().ToString(), *LayerAssetPackageName);
				}
			}

			if (!LayerAsset)
			{
				LayerAssetPackage = CreatePackage(*LayerAssetPackageName);
				if (LayerAssetPackage)
				{
					LayerAsset = FindOrCreateAssetInPackage<UPaper2DPlusCharacterLayerAsset>(
						LayerAssetPackage, LayerAssetName, bCreatedLayerAsset);
				}
			}

			{
					if (LayerAsset)
					{
					if (bCreatedLayerAsset || LayerAsset->DisplayName.IsEmpty())
					{
						LayerAsset->DisplayName = Settings.AssetPrefix;
					}
					// Track the EFFECTIVE source (the in-project copy when "keep source in project" ran),
					// stored project-relative so the asset resolves on every synced machine, plus the
					// content hash the watcher's startup reconcile compares against the file on disk.
					const FString StoredSourcePath = MakeStoredAsePath(EffectiveSourceFilePath);
					const FString SourceContentHash = HashAseFileContent(EffectiveSourceFilePath);
					// TASK-192 (KTD8): the source-context restamp is a REAL write. A byte-changed
					// .ase that produced no asset change must still advance this stamp and dirty
					// the Layer Profile, or ReconcileOfflineAseChanges re-queues the file on every
					// editor start forever.
					bool bSourceStampChanged = false;

					// TASK-189: record THIS source's own context so several .ase files can feed one Layer
					// Profile and each still reimports with its own prefix and tag selection. The two source
					// registry tags emit NEWLINE-joined lists, so a path containing a newline would mis-split
					// them against the sibling list — refuse to stamp it AT ALL rather than corrupt the
					// mapping (the check must precede every write, including the legacy mirror fields).
					if (StoredSourcePath.Contains(TEXT("\n")))
					{
						UE_LOG(LogTemp, Error,
							TEXT("ImportAsLayeredAsset (TASK-189): source path contains a newline and cannot be tracked for live reimport: '%s'"),
							*StoredSourcePath);
					}
					else
					{
						TMap<FString, FString> TagContentHashes;
						for (const FAsepriteTag& Tag : ParsedData.Tags)
						{
							TagContentHashes.Add(Tag.Name, FAsepriteStructuralDiff::ComputeTagFramesHash(ParsedData, Tag));
						}

						FAsepriteSourceContext& SourceContext = LayerAsset->UpsertAseSourceContext(StoredSourcePath);
						bSourceStampChanged = !SourceContext.ContentHash.Equals(SourceContentHash, ESearchCase::IgnoreCase);
						SourceContext.ContentHash = SourceContentHash;
						SourceContext.AssetPrefix = Settings.AssetPrefix;
						SourceContext.DisabledTagNames = DisabledTagNames;
						SourceContext.LayerContentHashes = LayerContentHashes;
						SourceContext.TagContentHashes = MoveTemp(TagContentHashes);
						// TASK-192 U4/U7: the incremental stamps. Absent values on assets imported
						// before the gate read as "write", never as "skip".
						SourceContext.NormalLayerContentHashes = NewNormalLayerHashes;
						SourceContext.TagStructureHashes = MoveTemp(IncrementalContext.NewTagStructureHashes);
						SourceContext.CompositeContentHash = MoveTemp(IncrementalContext.NewCompositeContentHash);
						SourceContext.StampedFrameCount = ParsedData.Frames.Num();
						SourceContext.StampedCanvasWidth = ParsedData.Width;
						SourceContext.StampedCanvasHeight = ParsedData.Height;
						// Element 0 remains the PRIMARY source mirrored into the legacy single-source fields.
						// NEVER assign SourceAseFilePath/ImportedAseContentHash directly here: Upsert reads
						// them to materialize a pre-TASK-189 source into element 0, so writing them first
						// would record THIS file as the old source and destroy the earlier source's record.
						LayerAsset->SyncLegacyAseSourceMirror();
					}

					// Full-reimport context: lets the watcher re-run this exact import when the .ase
					// changes, so edits reflect in the sheet/sprites/flipbooks/profile — not just here.
					LayerAsset->ImportOutputPath = Settings.OutputPath;
					LayerAsset->bImportOrganizeIntoSubfolders = Settings.bOrganizeIntoSubfolders;
					// ImportAssetPrefix / ImportDisabledTagNames are PER SOURCE since TASK-189: the
					// asset-level fields mirror element 0 of ImportedAseSources (written by
					// SyncLegacyAseSourceMirror above), so a second .ase importing into this Layer
					// Profile can no longer clobber the first source's reimport context.

					// Existing curated assets keep their relationship. Reimport may refresh art and the external
					// source path, but it never silently re-points the canonical Profile/bake ownership graph.
						if (Profile && (bCreatedLayerAsset || LayerAsset->BaseProfile.IsNull()))
						{
							LayerAsset->BaseProfile = Profile;
						}

						bool bLayerDataChanged = false;
					// TASK-189: layers this import ADDS (reimport branch) — they join the Default
					// Appearance preset below, fail-closed against Exclusive Groups.
					TArray<FString> NewLayerNames;
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
									if (Existing->SourceTexture != Incoming.SourceTexture)
									{
										Existing->SourceTexture = Incoming.SourceTexture;
										bLayerDataChanged = true;
									}
									int32 MappingsRefreshed = 0;
									int32 MappingsAdded = 0;
									// TASK-189: per-animation UNION instead of a wholesale replace. A second .ase
									// feeding this Layer Profile must refresh only the animations it contributes —
									// the old assignment discarded every animation the other sources had added.
									if (FAsepriteStructuralDiff::MergeAnimationSprites(
										Existing->AnimationSprites, Incoming.AnimationSprites, &MappingsRefreshed, &MappingsAdded))
									{
										bLayerDataChanged = true;
									}
									DiffReport.MappingsRefreshed += MappingsRefreshed;
									DiffReport.MappingsAdded += MappingsAdded;
								}
								else
								{
									NewLayerNames.Add(Incoming.LayerName);
									LayerAsset->Layers.Add(MoveTemp(Incoming));
									bLayerDataChanged = true;
								}
							}
							LayerAsset->EnsureLayerAuthoringIdentity();
							// TASK-189 R12: a brand-new layer joins the Default Appearance preset so it is visible by
							// default. FAIL CLOSED on Exclusive Groups — a preset holding two members of one group
							// is invalid, so a new layer whose group already has a member in the default preset is
							// still appended to Layers but NOT joined, and the refusal is reported.
							if (NewLayerNames.Num() > 0)
							{
								if (FCharacterLayerAppearancePreset* DefaultPreset = LayerAsset->AppearancePresets.FindByPredicate(
									[&LayerAsset](const FCharacterLayerAppearancePreset& Preset)
									{
										return Preset.PresetId == LayerAsset->DefaultAppearancePresetId;
									}))
								{
									TArray<FString> RefusedLayerNames;
									if (FAsepriteStructuralDiff::JoinNewLayersIntoPreset(
										LayerAsset->Layers, NewLayerNames, *DefaultPreset, &RefusedLayerNames) > 0)
									{
										bLayerDataChanged = true;
									}
									for (const FString& RefusedName : RefusedLayerNames)
									{
										DiffReport.PresetJoinRefusals.Add(FString::Printf(
											TEXT("Layer '%s' was NOT added to the Default Appearance preset — its Exclusive Group already has an active member there."),
											*RefusedName));
									}
								}
							}

							ReimportSummary = FString::Printf(TEXT("Reimported %d generic layer(s)"), CreatedLayers.Num());
						}
						// A structural-diff apply (rename / removal) is a real change even when the
						// merge above found nothing to refresh — it must dirty the package too. So
						// is a changed source-context stamp (KTD8): the restamp is what stops the
						// startup reconcile from re-queueing a byte-changed no-op forever.
						if ((bLayerDataChanged || bDiffModifiedLayerAsset || bSourceStampChanged)
							&& !FPackageName::IsTempPackage(LayerAssetPackageName))
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

		// --- TASK-189 U4 (R13): report the structural-diff outcome LOUDLY and non-modally. ---
		// The watcher runs this whole path unattended, so a prompt here would be a hang; and a diff
		// that quietly renamed or dropped something is exactly what a user must be able to audit
		// afterwards. Every line also goes to the log, which is where first-contact reports get
		// diagnosed from.
		if (!DiffReport.IsEmpty())
		{
			TArray<FString> DetailLines;
			DiffReport.AppendDetailLines(DetailLines);
			for (const FString& Line : DetailLines)
			{
				UE_LOG(LogTemp, Log, TEXT("Aseprite reimport (TASK-189): %s"), *Line);
			}
			AsepriteDiffApply::PublishDiffReport(
				LayerAsset ? LayerAsset->GetName() : Settings.AssetPrefix, DiffReport, DetailLines);
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

		// Post-import assertion (2026-09-04): the import must never report success while the asset it just
		// wrote records sprite references nothing created. Two shipped bugs had exactly that shape (an
		// engine-invalid character in a layer name; a per-source reimport of a row two sources share) and
		// their only symptom was a character missing a garment. Registry-backed, no loads.
		TArray<FCharacterLayerValidationIssue> DanglingSpriteIssues;
		if (LayerAsset)
		{
			DanglingSpriteIssues = LayerAsset->ValidateSpriteReferences();
			for (const FCharacterLayerValidationIssue& Issue : DanglingSpriteIssues)
			{
				UE_LOG(LogTemp, Error, TEXT("ImportAsLayeredAsset: %s"), *Issue.Message);
			}
		}

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
			if (DanglingSpriteIssues.Num() > 0)
			{
				Summary += FString::Printf(
					TEXT(" - ERROR: %d layer(s) reference sprites that were not created (see the Output Log); re-import every source together through the Bulk Sprite Extractor"),
					DanglingSpriteIssues.Num());
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
		Info.ExpireDuration = DanglingSpriteIssues.Num() > 0 ? 12.0f : 5.0f;
		Info.bUseSuccessFailIcons = true;
		if (TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info))
		{
			Item->SetCompletionState(DanglingSpriteIssues.Num() > 0 ? SNotificationItem::CS_Fail : SNotificationItem::CS_Success);
		}
		UE_LOG(LogTemp, Log, TEXT("ImportAsLayeredAsset: %s"), *Summary);
	}

	// --- Step 4: Finalize ---
	Progress.EnterProgressFrame(1.0f, LOCTEXT("FinalizingImport", "Finalizing import..."));

	return PrimaryOutput;
}

bool FAsepriteImporter::ForceReimportLayerAssetSource(
	UPaper2DPlusCharacterLayerAsset& LayerAsset,
	const FAsepriteSourceContext& SourceContext,
	const bool bForceFullReimport,
	FAsepriteImportCostReport* OutReport)
{
	const FString ResolvedPath = ResolveStoredAsePath(SourceContext.StoredSourcePath);
	if (ResolvedPath.IsEmpty() || !FPaths::FileExists(ResolvedPath))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("ForceReimportLayerAssetSource: source '%s' of '%s' does not resolve to a file on disk."),
			*SourceContext.StoredSourcePath, *LayerAsset.GetName());
		return false;
	}
	if (SourceContext.AssetPrefix.IsEmpty() || LayerAsset.ImportOutputPath.IsEmpty())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("ForceReimportLayerAssetSource: '%s' carries no import context for '%s' — a legacy asset must be reimported through the Bulk Extractor once."),
			*LayerAsset.GetName(), *SourceContext.StoredSourcePath);
		return false;
	}

	// Fail closed on rows shared with another source (2026-09-04). A layer name present in two sources is
	// ONE row whose sheet holds every contributor's frames; replaying one source rebuilds that sheet from
	// that source alone and leaves the other source's frames sampling a layout built for a different frame
	// count (wrong frames, wrong offsets) while their old sprites dangle. The whole-batch path - every source
	// of the asset through the Bulk Sprite Extractor - is the fresh-import behaviour that composes shared rows
	// from all contributors, so that is where a shared-row asset is sent.
	{
		TArray<FString> OtherSourcePaths;
		const TArray<FString> SharedLayers = CollectLayersSharedWithOtherSources(LayerAsset, SourceContext, &OtherSourcePaths);
		if (SharedLayers.Num() > 0)
		{
			const FString Reason = FString::Printf(
				TEXT("Force Full Reimport refused for '%s' of '%s': %d layer(s) are shared with %s (%s). Replaying one source would rebuild the shared sheet from that source alone. Re-import every source of this Layer Profile together through the Bulk Sprite Extractor (Paper2D+ Actions > Import Aseprite Files...), which composes shared rows from all contributors."),
				*SourceContext.StoredSourcePath, *LayerAsset.GetName(), SharedLayers.Num(),
				*FString::Join(OtherSourcePaths, TEXT(", ")), *FString::Join(SharedLayers, TEXT(", ")));
			UE_LOG(LogTemp, Warning, TEXT("ForceReimportLayerAssetSource: %s"), *Reason);
			if (OutReport)
			{
				OutReport->DecisionLines.Add(TEXT("refused - ") + Reason);
			}
			if (!FApp::IsUnattended() && !IsRunningCommandlet() && !GIsAutomationTesting && FSlateApplication::IsInitialized())
			{
				FNotificationInfo Info(FText::FromString(Reason));
				Info.ExpireDuration = 12.0f;
				Info.bUseSuccessFailIcons = true;
				if (TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info))
				{
					Item->SetCompletionState(SNotificationItem::CS_Fail);
				}
			}
			return false;
		}
	}

	FAsepriteImportCostScope CostScope;

	FAsepriteParsedData Parsed;
	FString ParseError;
	{
		FAsepriteImportCostPhaseTimer ParsePhase(EAsepriteImportCostPhase::Parse);
		if (!ParseFile(ResolvedPath, Parsed, ParseError))
		{
			UE_LOG(LogTemp, Warning, TEXT("ForceReimportLayerAssetSource: parse failed for %s: %s"),
				*ResolvedPath, *ParseError);
			return false;
		}
	}
	FPerLayerBufferMap PerLayerBuffers;
	{
		FAsepriteImportCostPhaseTimer CompositePhase(EAsepriteImportCostPhase::Composite);
		PerLayerBuffers = CompositePerLayer(Parsed);
	}

	FAsepriteLayerImportSettings ReimportSettings;
	InitDefaultSelection(Parsed, ReimportSettings);
	// Re-apply THIS SOURCE's import-time tag de-selection BY NAME (stable across reordering).
	for (int32 TagIdx = 0; TagIdx < Parsed.Tags.Num(); ++TagIdx)
	{
		if (SourceContext.DisabledTagNames.Contains(Parsed.Tags[TagIdx].Name))
		{
			ReimportSettings.TagImportEnabled.Add(TagIdx, false);
		}
	}
	ReimportSettings.ImportMode = LayerAsset.BaseProfile.IsNull()
		? EAsepriteImportMode::LayerAssetNewProfile
		: EAsepriteImportMode::LayerAssetExistingProfile;
	ReimportSettings.ExistingProfile = LayerAsset.BaseProfile;
	ReimportSettings.ExistingLayerAsset = &LayerAsset;
	ReimportSettings.OutputPath = LayerAsset.ImportOutputPath;
	ReimportSettings.AssetPrefix = SourceContext.AssetPrefix;
	ReimportSettings.bOrganizeIntoSubfolders = LayerAsset.bImportOrganizeIntoSubfolders;
	ReimportSettings.bKeepSourceInProject = false; // the tracked file IS the source
	ReimportSettings.bUserConfirmed = true;
	ReimportSettings.SourceFilePath = ResolvedPath;
	ReimportSettings.bForceFullReimport = bForceFullReimport;

	bool bSucceeded =
		ImportAsLayeredAsset(Parsed, PerLayerBuffers, ReimportSettings) != nullptr;
	// The replay is not a success if the asset it just rewrote records sprites nothing created.
	TArray<FString> DanglingLines;
	if (bSucceeded)
	{
		for (const FCharacterLayerValidationIssue& Issue : LayerAsset.ValidateSpriteReferences())
		{
			DanglingLines.Add(TEXT("dangling - ") + Issue.Message);
		}
		bSucceeded = DanglingLines.Num() == 0;
	}
	if (OutReport)
	{
		*OutReport = CostScope.GetReport();
		OutReport->DecisionLines.Append(DanglingLines);
	}
	return bSucceeded;
}

TArray<FString> FAsepriteImporter::CollectLayersSharedWithOtherSources(
	const UPaper2DPlusCharacterLayerAsset& LayerAsset,
	const FAsepriteSourceContext& SourceContext,
	TArray<FString>* OutOtherSourcePaths)
{
	TArray<FString> Shared;
#if WITH_EDITORONLY_DATA
	for (const FAsepriteSourceContext& Other : LayerAsset.ImportedAseSources)
	{
		if (Other.StoredSourcePath.Equals(SourceContext.StoredSourcePath, ESearchCase::IgnoreCase))
		{
			continue;
		}
		bool bAnyShared = false;
		for (const TPair<FString, FString>& Mine : SourceContext.LayerContentHashes)
		{
			if (Other.LayerContentHashes.Contains(Mine.Key))
			{
				Shared.AddUnique(Mine.Key);
				bAnyShared = true;
			}
		}
		if (bAnyShared && OutOtherSourcePaths)
		{
			OutOtherSourcePaths->AddUnique(Other.StoredSourcePath);
		}
	}
#endif
	Shared.Sort();
	return Shared;
}

void FAsepriteImporter::PublishIncrementalAuditPage(
	const FString& AssetDisplayName,
	const FAsepriteImportCostReport& Report)
{
	if (FApp::IsUnattended() || IsRunningCommandlet() || GIsAutomationTesting
		|| !FSlateApplication::IsInitialized())
	{
		return; // the cost summary + per-skip UE_LOG lines are the whole record here
	}

	static const FName ListingName(TEXT("Paper2DPlusAsepriteImport"));
	FMessageLogModule& MessageLogModule =
		FModuleManager::LoadModuleChecked<FMessageLogModule>(TEXT("MessageLog"));
	if (!MessageLogModule.IsRegisteredLogListing(ListingName))
	{
		FMessageLogInitializationOptions Options;
		Options.bShowFilters = true;
		Options.bShowPages = true;
		Options.bAllowClear = true;
		MessageLogModule.RegisterLogListing(
			ListingName, NSLOCTEXT("AsepriteImporter", "AsepriteImportLogLabel", "Aseprite Import"), Options);
	}

	FMessageLog ImportLog(ListingName);
	ImportLog.NewPage(FText::Format(
		NSLOCTEXT("AsepriteImporter", "AseIncrementalAuditPage", "{0} — incremental reimport audit"),
		FText::FromString(AssetDisplayName)));
	ImportLog.Info(FText::FromString(Report.ToSummaryString()));
	if (Report.DecisionLines.Num() == 0)
	{
		// An all-skipped reimport still produces a report rather than silence (R10).
		ImportLog.Info(NSLOCTEXT("AsepriteImporter", "AseIncrementalAuditEmpty",
			"No per-asset decisions were recorded for this reimport."));
	}
	for (const FString& Line : Report.DecisionLines)
	{
		ImportLog.Info(FText::FromString(Line));
	}
	// Deliberately no Open() and no modal: the watcher drives this path unattended.
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
				LOCTEXT("ImportAseprite", "Import Aseprite Files..."),
				LOCTEXT("ImportAsepriteTooltip", "Load Aseprite (.ase/.aseprite) files into the Bulk Sprite Extractor, where the batch's Character Profile and Layer Profile are chosen and one Extract All imports them together"),
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
