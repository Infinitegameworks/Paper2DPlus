// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "AsepriteImporter.h"
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
#include "Engine/Texture2D.h"
#include "PaperSprite.h"
#include "PaperFlipbook.h"
#include "ObjectTools.h"
#include "PackageTools.h"
#include "DesktopPlatformModule.h"
#include "EditorDirectories.h"
#include "Misc/FileHelper.h"
#include "Misc/ScopedSlowTask.h"
#include "Misc/Compression.h"
#include "Styling/AppStyle.h"

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

// Cel types
static constexpr uint16 ASE_CEL_RAW = 0;
static constexpr uint16 ASE_CEL_LINKED = 1;
static constexpr uint16 ASE_CEL_COMPRESSED = 2;

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
		return (Pos + Bytes) <= Size;
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
// Cel data stored during parsing for later compositing
// ============================================
struct FAsepriteCelData
{
	int32 LayerIndex = 0;
	int32 X = 0;
	int32 Y = 0;
	uint8 Opacity = 255;
	uint16 CelType = 0;
	int32 CelWidth = 0;
	int32 CelHeight = 0;
	TArray<FColor> Pixels;
	TArray<uint8> IndexedPixels;
	TArray<FColor> PaletteSnapshot;
	uint16 LinkedFrame = 0; // For linked cels
};

// ============================================
// ParseFile
// ============================================

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
				Reader.Skip(2); // child level
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

					int32 PixelCount = Cel.CelWidth * Cel.CelHeight;
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

					int32 PixelCount = Cel.CelWidth * Cel.CelHeight;
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

				if (OutData.Palette.Num() < static_cast<int32>(PaletteSize))
				{
					OutData.Palette.SetNum(PaletteSize);
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

					if (ColorIdx < PaletteSize)
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

			// ---- Color profile chunk (skip) ----
			case ASE_CHUNK_COLOR_PROFILE:
			default:
				break;
			}

			// Advance to end of chunk regardless of what was read
			Reader.SetPos(ChunkStartPos + ChunkSize);
		}

		// Advance to end of frame regardless of what was read
		Reader.SetPos(FrameStartPos + FrameSize);
	}

	// Convert indexed cels after parsing all chunks, using the palette snapshot captured when each cel was read.
	if (OutData.ColorDepth == 8)
	{
		for (TArray<FAsepriteCelData>& FrameCels : AllFrameCels)
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

	// ---- Composite frames ----
	// Now flatten all cels for each frame onto frame buffers
	for (int32 FrameIdx = 0; FrameIdx < OutData.Frames.Num(); FrameIdx++)
	{
		FAsepriteFrame& Frame = OutData.Frames[FrameIdx];
		int32 PixelCount = Frame.Width * Frame.Height;
		Frame.Pixels.SetNumZeroed(PixelCount); // Start with transparent black

		const TArray<FAsepriteCelData>& Cels = AllFrameCels[FrameIdx];

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

	// Layout frames in a grid: prefer horizontal strip for small counts, grid for many frames
	int32 Columns, Rows;
	if (FrameCount <= 16)
	{
		Columns = FrameCount;
		Rows = 1;
	}
	else
	{
		Columns = FMath::CeilToInt(FMath::Sqrt(static_cast<float>(FrameCount)));
		Rows = FMath::CeilToInt(static_cast<float>(FrameCount) / Columns);
	}

	int32 SheetWidth = Columns * FrameW;
	int32 SheetHeight = Rows * FrameH;

	// Create package
	FString PackageName = OutputPath / AssetName;
	UPackage* Package = CreatePackage(*PackageName);
	if (!Package) return nullptr;

	// Create texture
	UTexture2D* Texture = NewObject<UTexture2D>(Package, *AssetName, RF_Public | RF_Standalone);
	if (!Texture) return nullptr;

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

	// Allocate and fill pixel data
	int32 TotalPixels = SheetWidth * SheetHeight;
	Mip->BulkData.Lock(LOCK_READ_WRITE);
	uint8* DestData = static_cast<uint8*>(Mip->BulkData.Realloc(TotalPixels * 4));

	// Clear to transparent black
	FMemory::Memzero(DestData, TotalPixels * 4);

	// Copy each frame into the sprite sheet
	for (int32 FrameIdx = 0; FrameIdx < FrameCount; FrameIdx++)
	{
		const FAsepriteFrame& Frame = Data.Frames[FrameIdx];

		int32 Col = FrameIdx % Columns;
		int32 Row = FrameIdx / Columns;
		int32 OffsetX = Col * FrameW;
		int32 OffsetY = Row * FrameH;

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

	// Register asset
	Package->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(Texture);

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

	int32 Columns;
	if (FrameCount <= 16)
	{
		Columns = FrameCount;
	}
	else
	{
		Columns = FMath::CeilToInt(FMath::Sqrt(static_cast<float>(FrameCount)));
	}

	for (int32 FrameIdx = 0; FrameIdx < FrameCount; FrameIdx++)
	{
		FString SpriteName = FString::Printf(TEXT("%s_%02d"), *AssetPrefix, FrameIdx);
		FString PackageName = OutputPath / SpriteName;

		UPackage* Package = CreatePackage(*PackageName);
		if (!Package) continue;

		UPaperSprite* Sprite = NewObject<UPaperSprite>(Package, *SpriteName, RF_Public | RF_Standalone);
		if (!Sprite) continue;

		int32 Col = FrameIdx % Columns;
		int32 Row = FrameIdx / Columns;

		FSpriteAssetInitParameters InitParams;
		InitParams.Texture = SpriteSheet;
		InitParams.Offset = FIntPoint(Col * FrameW, Row * FrameH);
		InitParams.Dimension = FIntPoint(FrameW, FrameH);
		InitParams.SetPixelsPerUnrealUnit(1.0f);
		Sprite->InitializeSprite(InitParams);

		Package->MarkPackageDirty();
		FAssetRegistryModule::AssetCreated(Sprite);

		Sprites.Add(Sprite);
	}

	UE_LOG(LogTemp, Log, TEXT("AsepriteImporter: Created %d sprites"), Sprites.Num());
	return Sprites;
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

		UPaperFlipbook* Flipbook = NewObject<UPaperFlipbook>(Package, *FlipbookName, RF_Public | RF_Standalone);
		if (!Flipbook) return nullptr;

		// Calculate frame rate from frame durations
		// Average the durations of the frames in this range
		float TotalDurationMs = 0.0f;
		for (int32 FrameIndex : FrameIndices)
		{
			if (FrameIndex < 0 || FrameIndex >= Sprites.Num())
			{
				return nullptr;
			}

			if (FrameIndex < Data.Frames.Num())
			{
				TotalDurationMs += Data.Frames[FrameIndex].Duration;
			}
			else
			{
				TotalDurationMs += 100.0f; // Default 100ms
			}
		}
		float AverageDurationMs = TotalDurationMs / FrameIndices.Num();
		float FramesPerSecond = (AverageDurationMs > 0) ? (1000.0f / AverageDurationMs) : DefaultFPS;

		{
			FScopedFlipbookMutator Mutator(Flipbook);
			Mutator.FramesPerSecond = FramesPerSecond;
			Mutator.KeyFrames.Empty();

			for (int32 FrameIndex : FrameIndices)
			{
				FPaperFlipbookKeyFrame KeyFrame;
				KeyFrame.Sprite = Sprites[FrameIndex];

				// Use FrameRun to handle per-frame timing differences
				// Each key frame gets FrameRun=1 (the duration is handled by the FPS)
				KeyFrame.FrameRun = 1;
				Mutator.KeyFrames.Add(KeyFrame);
			}
		}

		Package->MarkPackageDirty();
		FAssetRegistryModule::AssetCreated(Flipbook);

		return Flipbook;
	};

	auto BuildTagFrameSequence = [](int32 FromFrame, int32 ToFrame, uint8 LoopDirection) -> TArray<int32>
	{
		TArray<int32> Sequence;

		if (FromFrame > ToFrame)
		{
			Swap(FromFrame, ToFrame);
		}

		// Forward: 0, Reverse: 1, PingPong: 2
		switch (LoopDirection)
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
	};

	if (Data.Tags.Num() > 0)
	{
		// Create one flipbook per tag
		for (const FAsepriteTag& Tag : Data.Tags)
		{
			FString FlipbookName = FString::Printf(TEXT("%s_%s"), *AssetPrefix, *Tag.Name);
			// Sanitize the name
			FlipbookName = FlipbookName.Replace(TEXT(" "), TEXT("_"));

			int32 FromFrame = FMath::Clamp(Tag.FromFrame, 0, Sprites.Num() - 1);
			int32 ToFrame = FMath::Clamp(Tag.ToFrame, 0, Sprites.Num() - 1);
			TArray<int32> FrameSequence = BuildTagFrameSequence(FromFrame, ToFrame, Tag.LoopDirection);

			UPaperFlipbook* Flipbook = CreateSingleFlipbook(FlipbookName, FrameSequence, 10.0f);
			if (Flipbook)
			{
				Flipbooks.Add(Flipbook);
				UE_LOG(LogTemp, Log, TEXT("AsepriteImporter: Created flipbook '%s' (tag: %s, loopDirection: %d, source frames %d-%d, emitted %d frames)"),
					*FlipbookName, *Tag.Name, Tag.LoopDirection, FromFrame, ToFrame, FrameSequence.Num());
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

	Result.bSuccess = true;

	UE_LOG(LogTemp, Log, TEXT("AsepriteImporter: Import complete - %d sprites, %d flipbooks from '%s'"),
		Result.Sprites.Num(), Result.Flipbooks.Num(), *FilePath);

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
// RegisterMenus / UnregisterMenus
// ============================================

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
				FSlateIcon(FAppStyle::GetAppStyleSetName(), "ClassIcon.PaperFlipbook"),
				FUIAction(FExecuteAction::CreateStatic(&FAsepriteImporter::ShowImportDialog))
			);
		}

		// Add to Content Browser context menu (right-click > Import)
		UToolMenu* ContentBrowserMenu = UToolMenus::Get()->ExtendMenu("ContentBrowser.AddNewContextMenu");
		if (ContentBrowserMenu)
		{
			FToolMenuSection& Section = ContentBrowserMenu->FindOrAddSection("ContentBrowserImportBasic");
			Section.AddMenuEntry(
				"ImportAsepriteFile",
				LOCTEXT("ImportAsepriteContentBrowser", "Import Aseprite File (Paper2D+)"),
				LOCTEXT("ImportAsepriteContentBrowserTooltip", "Import an Aseprite file and create Paper2D sprites/flipbooks"),
				FSlateIcon(FAppStyle::GetAppStyleSetName(), "ClassIcon.PaperFlipbook"),
				FUIAction(FExecuteAction::CreateStatic(&FAsepriteImporter::ShowImportDialog))
			);
		}
	}));
}

void FAsepriteImporter::UnregisterMenus()
{
	// Menus are automatically cleaned up when the module shuts down
}

#undef LOCTEXT_NAMESPACE
