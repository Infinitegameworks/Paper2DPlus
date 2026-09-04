// Copyright 2026 Infinite Gameworks. All Rights Reserved.

/**
 * TASK-192 incremental .ase reimport — instrumentation coverage (U2).
 *
 * These tests pin the cost-report mechanics (scope ownership, nesting, unique-package dirty
 * counting) and the PRE-NARROWING baseline: today a reimport of an unchanged file still writes
 * every sheet and sprite, and these tests assert exactly that. When a narrowing unit (U4+) lands,
 * the baseline test here is the one that flips to skipped counts — deliberately.
 *
 * Scope notes:
 * - The layered import runs against /Temp packages with an EMPTY SourceFilePath, which skips the
 *   profile branch (ImportFile → composited sheet + flat sprites + flipbooks). That branch's
 *   composited-sheet writer has no /Temp package guard until U9 unifies the two writers, so it is
 *   not headless-safe here; its counters use the same shared write-site hooks and are exercised by
 *   the /Game-fixture watcher tests and the U4–U7 scenarios.
 * - The user-cancel scenario ("counters survive the early return") holds by construction: counters
 *   accumulate at the real write sites as work happens, so a cancelled import reports exactly the
 *   work it did — there is no end-of-import tally that could go stale. Nothing here can drive
 *   FScopedSlowTask's cancel deterministically in an unattended run, so no test pretends to.
 *
 * All buffers are crafted in-memory .ase files (the Paper2DPlusAsepriteImportSelectionTest
 * technique); helpers use the file-unique AseIncr_ prefix per the unity-build rule.
 */

#include "CoreMinimal.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Texture2D.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "PaperFlipbook.h"
#include "PaperSprite.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

#include "PackageTools.h"
#include "SpriteEditorOnlyTypes.h"
#include "TextureCompiler.h"
#include "UObject/SavePackage.h"
#include "UObject/UnrealType.h"

#include "AsepriteImporter.h"
#include "AsepriteIncrementalWrite.h"
#include "Paper2DPlusCharacterLayerAsset.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Paper2DPlusSettings.h"
#include "TextureWatcherService.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace Paper2DPlusAsepriteIncrementalTest
{
	/** Little-endian byte writer for crafting minimal .ase buffers (file-unique: AseIncr_). */
	struct FAseIncrBufWriter
	{
		TArray<uint8> Bytes;

		void U8(uint8 V) { Bytes.Add(V); }
		void U16(uint16 V) { Bytes.Add(V & 0xFF); Bytes.Add((V >> 8) & 0xFF); }
		void S16(int16 V) { U16(static_cast<uint16>(V)); }
		void U32(uint32 V)
		{
			Bytes.Add(V & 0xFF); Bytes.Add((V >> 8) & 0xFF);
			Bytes.Add((V >> 16) & 0xFF); Bytes.Add((V >> 24) & 0xFF);
		}
		void Zeros(int32 Count) { Bytes.AddZeroed(Count); }
		void Str(const FString& S)
		{
			const FTCHARToUTF8 Utf8(*S);
			U16(static_cast<uint16>(Utf8.Length()));
			Bytes.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
		}
		void Append(const TArray<uint8>& Other) { Bytes.Append(Other); }
	};

	static TArray<uint8> AseIncr_Chunk(uint16 ChunkType, const TArray<uint8>& Payload)
	{
		FAseIncrBufWriter W;
		W.U32(static_cast<uint32>(Payload.Num() + 6));
		W.U16(ChunkType);
		W.Append(Payload);
		return W.Bytes;
	}

	static TArray<uint8> AseIncr_LayerChunk(const FString& Name)
	{
		FAseIncrBufWriter P;
		P.U16(1);        // flags (visible)
		P.U16(0);        // normal layer
		P.U16(0);        // child level
		P.U16(0); P.U16(0);
		P.U16(0);        // blend mode
		P.U8(255);       // opacity
		P.Zeros(3);
		P.Str(Name);
		return AseIncr_Chunk(0x2004, P.Bytes);
	}

	static TArray<uint8> AseIncr_RawCelChunk(uint16 LayerIndex, uint16 W, uint16 H, const FColor& Fill)
	{
		FAseIncrBufWriter P;
		P.U16(LayerIndex);
		P.S16(0); P.S16(0);
		P.U8(255);
		P.U16(0);    // ASE_CEL_RAW
		P.Zeros(7);
		P.U16(W); P.U16(H);
		for (int32 i = 0; i < W * H; ++i)
		{
			P.U8(Fill.R); P.U8(Fill.G); P.U8(Fill.B); P.U8(Fill.A);
		}
		return AseIncr_Chunk(0x2005, P.Bytes);
	}

	/** Raw cel whose pixels vary per position (seeded), so byte-level sheet comparisons are
	 *  meaningful and the alpha carries real holes for tight-bounds realism. */
	static TArray<uint8> AseIncr_PatternCelChunk(uint16 LayerIndex, uint16 W, uint16 H, uint8 Seed)
	{
		FAseIncrBufWriter P;
		P.U16(LayerIndex);
		P.S16(0); P.S16(0);
		P.U8(255);
		P.U16(0);    // ASE_CEL_RAW
		P.Zeros(7);
		P.U16(W); P.U16(H);
		for (int32 Y = 0; Y < H; ++Y)
		{
			for (int32 X = 0; X < W; ++X)
			{
				const bool bHole = ((X + Y + Seed) % 3) == 0;
				P.U8(static_cast<uint8>(X * 7 + Seed));
				P.U8(static_cast<uint8>(Y * 13 + Seed));
				P.U8(static_cast<uint8>((X ^ Y) * 5));
				P.U8(bHole ? 0 : 255);
			}
		}
		return AseIncr_Chunk(0x2005, P.Bytes);
	}

	/** Full-canvas cel whose art occupies only ArtRect (Max exclusive), transparent elsewhere — so
	 *  tight bounds are smaller than the cell and can genuinely move between imports. */
	static TArray<uint8> AseIncr_SubRectCelChunk(
		uint16 LayerIndex, uint16 W, uint16 H, const FIntRect& ArtRect, const FColor& Color)
	{
		FAseIncrBufWriter P;
		P.U16(LayerIndex);
		P.S16(0); P.S16(0);
		P.U8(255);
		P.U16(0);    // ASE_CEL_RAW
		P.Zeros(7);
		P.U16(W); P.U16(H);
		for (int32 Y = 0; Y < H; ++Y)
		{
			for (int32 X = 0; X < W; ++X)
			{
				const bool bArt = X >= ArtRect.Min.X && X < ArtRect.Max.X
					&& Y >= ArtRect.Min.Y && Y < ArtRect.Max.Y;
				P.U8(bArt ? Color.R : 0);
				P.U8(bArt ? Color.G : 0);
				P.U8(bArt ? Color.B : 0);
				P.U8(bArt ? 255 : 0);
			}
		}
		return AseIncr_Chunk(0x2005, P.Bytes);
	}

	/** An unknown chunk the parser skips by size — a byte-level file change with ZERO asset
	 *  effect, which is exactly the KTD8 restamp scenario. */
	static TArray<uint8> AseIncr_JunkChunk(uint8 Seed)
	{
		FAseIncrBufWriter P;
		for (int32 i = 0; i < 8; ++i)
		{
			P.U8(static_cast<uint8>(Seed + i));
		}
		return AseIncr_Chunk(0x1234, P.Bytes);
	}

	/** GUID token that is filesystem/package safe on every supported engine. */
	static FString AseIncr_NewToken()
	{
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
		return FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower();
#else
		return FGuid::NewGuid().ToString(EGuidFormats::DigitsLower);
#endif
	}

	/** Reads a texture's whole source mip 0 as bytes. */
	static bool AseIncr_ReadSourceMip(UTexture2D* Texture, TArray<uint8>& OutBytes)
	{
		if (!Texture || !Texture->Source.IsValid())
		{
			return false;
		}
		const int64 MipBytes = Texture->Source.CalcMipSize(0);
		const uint8* Data = Texture->Source.LockMip(0);
		if (!Data)
		{
			return false;
		}
		OutBytes.Reset();
		OutBytes.Append(Data, MipBytes);
		Texture->Source.UnlockMip(0);
		return true;
	}

	/** Drops an unsaved /Game fixture asset: registry row out, dirty flag cleared. Nothing was ever
	 *  written to disk, so there is no file half to clean. */
	static void AseIncr_DropFixtureAsset(UObject* Asset)
	{
		if (!Asset)
		{
			return;
		}
		if (Asset->IsAsset())
		{
			FAssetRegistryModule::AssetDeleted(Asset);
		}
		if (UPackage* Package = Asset->GetOutermost())
		{
			Package->SetDirtyFlag(false);
		}
	}

	/** One tags chunk from (name, from, to) triples, forward loop direction. */
	static TArray<uint8> AseIncr_TagsChunk(const TArray<TTuple<FString, uint16, uint16>>& Tags)
	{
		FAseIncrBufWriter P;
		P.U16(static_cast<uint16>(Tags.Num()));
		P.Zeros(8);
		for (const auto& Tag : Tags)
		{
			P.U16(Tag.Get<1>()); // from
			P.U16(Tag.Get<2>()); // to
			P.U8(0);             // forward
			P.Zeros(8);
			P.Zeros(3);
			P.Zeros(1);
			P.Str(Tag.Get<0>());
		}
		return AseIncr_Chunk(0x2018, P.Bytes);
	}

	static TArray<uint8> AseIncr_Frame(const TArray<TArray<uint8>>& Chunks, uint16 DurationMs = 100)
	{
		int32 ChunkBytes = 0;
		for (const TArray<uint8>& C : Chunks) { ChunkBytes += C.Num(); }

		FAseIncrBufWriter W;
		W.U32(static_cast<uint32>(16 + ChunkBytes));
		W.U16(0xF1FA);
		W.U16(static_cast<uint16>(Chunks.Num()));
		W.U16(DurationMs);
		W.Zeros(2);
		W.U32(static_cast<uint32>(Chunks.Num()));
		for (const TArray<uint8>& C : Chunks) { W.Append(C); }
		return W.Bytes;
	}

	static TArray<uint8> AseIncr_File(uint16 CanvasW, uint16 CanvasH, const TArray<TArray<uint8>>& Frames)
	{
		FAseIncrBufWriter W;
		int32 FrameBytes = 0;
		for (const TArray<uint8>& F : Frames) { FrameBytes += F.Num(); }

		W.U32(static_cast<uint32>(128 + FrameBytes));
		W.U16(0xA5E0);
		W.U16(static_cast<uint16>(Frames.Num()));
		W.U16(CanvasW);
		W.U16(CanvasH);
		W.U16(32);
		W.U32(0);
		W.U16(100);
		W.Zeros(128 - W.Bytes.Num());
		for (const TArray<uint8>& F : Frames) { W.Append(F); }
		return W.Bytes;
	}

	/** N visible layers x N frames of WxH raw cels plus even tags — the scalable form behind both
	 *  the small correctness fixture and the Paper2DPlusPerf synthetic replica. */
	static TArray<uint8> AseIncr_LayeredFile(
		const int32 LayerCount, const int32 FrameCount, const uint16 CanvasW, const uint16 CanvasH,
		const int32 TagCount)
	{
		TArray<TArray<uint8>> Frames;
		Frames.Reserve(FrameCount);
		const FColor Palette[] = { FColor::Red, FColor::Blue, FColor::Green, FColor::Yellow,
			FColor::Cyan, FColor::Magenta };
		for (int32 FrameIdx = 0; FrameIdx < FrameCount; ++FrameIdx)
		{
			TArray<TArray<uint8>> Chunks;
			if (FrameIdx == 0)
			{
				for (int32 LayerIdx = 0; LayerIdx < LayerCount; ++LayerIdx)
				{
					Chunks.Add(AseIncr_LayerChunk(FString::Printf(TEXT("Layer%d"), LayerIdx)));
				}
				if (TagCount > 0)
				{
					// Even split into ONE tags chunk; the writer only emits well-formed ranges.
					TArray<TTuple<FString, uint16, uint16>> Tags;
					const int32 Span = FMath::Max(1, FrameCount / TagCount);
					for (int32 TagIdx = 0; TagIdx < TagCount; ++TagIdx)
					{
						const int32 From = TagIdx * Span;
						const int32 To = (TagIdx == TagCount - 1) ? (FrameCount - 1)
							: FMath::Min(FrameCount - 1, From + Span - 1);
						if (From > To || From >= FrameCount) { break; }
						Tags.Emplace(FString::Printf(TEXT("Anim%d"), TagIdx),
							static_cast<uint16>(From), static_cast<uint16>(To));
					}
					Chunks.Add(AseIncr_TagsChunk(Tags));
				}
			}
			for (int32 LayerIdx = 0; LayerIdx < LayerCount; ++LayerIdx)
			{
				Chunks.Add(AseIncr_RawCelChunk(
					static_cast<uint16>(LayerIdx), CanvasW, CanvasH,
					Palette[(LayerIdx + FrameIdx) % UE_ARRAY_COUNT(Palette)]));
			}
			Frames.Add(AseIncr_Frame(Chunks));
		}
		return AseIncr_File(CanvasW, CanvasH, Frames);
	}

	/** Two visible layers, two frames, one tag spanning both — the smallest layered import that
	 *  produces more than one of every gated asset class the /Temp pipeline reaches. */
	static TArray<uint8> AseIncr_TwoLayerTwoFrameFile()
	{
		const TArray<uint8> Layer0 = AseIncr_LayerChunk(TEXT("Body"));
		const TArray<uint8> Layer1 = AseIncr_LayerChunk(TEXT("Coat"));
		const TArray<uint8> Tags = AseIncr_TagsChunk(
			{ MakeTuple(FString(TEXT("Idle")), static_cast<uint16>(0), static_cast<uint16>(1)) });
		const TArray<uint8> Frame0 = AseIncr_Frame({
			Layer0, Layer1, Tags,
			AseIncr_RawCelChunk(0, 4, 4, FColor::Red),
			AseIncr_RawCelChunk(1, 4, 4, FColor::Blue) });
		const TArray<uint8> Frame1 = AseIncr_Frame({
			AseIncr_RawCelChunk(0, 4, 4, FColor::Green),
			AseIncr_RawCelChunk(1, 4, 4, FColor::Yellow) });
		return AseIncr_File(4, 4, { Frame0, Frame1 });
	}

	/** Monotonic run id so repeated suite runs in one editor session never collide in /Temp. */
	static int32 AseIncr_RunCounter = 0;

	/** Parse + composite + run the layered import into /Temp under a cost scope; returns the
	 *  outermost report by value plus the created asset. Empty SourceFilePath by design (see the
	 *  file comment); /Temp keeps every write site's dirty/registry guards engaged. */
	static bool AseIncr_RunTempLayeredImport(
		FAutomationTestBase& Test,
		const FString& OutputPath,
		const FString& AssetPrefix,
		FAsepriteImportCostReport& OutReport,
		UPaper2DPlusCharacterLayerAsset*& OutLayerAsset)
	{
		OutLayerAsset = nullptr;

		const TArray<uint8> Buffer = AseIncr_TwoLayerTwoFrameFile();
		FAsepriteParsedData Parsed;
		FString Error;
		if (!Test.TestTrue(FString::Printf(TEXT("ParseBuffer succeeds (%s)"), *Error),
			FAsepriteImporter::ParseBuffer(Buffer, Parsed, Error)))
		{
			return false;
		}

		const FPerLayerBufferMap PerLayerBuffers = FAsepriteImporter::CompositePerLayer(Parsed);
		if (!Test.TestEqual(TEXT("both visual layers composited"), PerLayerBuffers.Num(), 2))
		{
			return false;
		}

		FAsepriteLayerImportSettings Settings;
		FAsepriteImporter::InitDefaultSelection(Parsed, Settings);
		Settings.ImportMode = EAsepriteImportMode::LayerAssetNewProfile;
		Settings.OutputPath = OutputPath;
		Settings.AssetPrefix = AssetPrefix;
		Settings.bOrganizeIntoSubfolders = false;
		Settings.bUserConfirmed = true;

		FAsepriteImportCostScope CostScope;
		UObject* Result = FAsepriteImporter::ImportAsLayeredAsset(Parsed, PerLayerBuffers, Settings);
		OutLayerAsset = Cast<UPaper2DPlusCharacterLayerAsset>(Result);
		OutReport = CostScope.GetReport();

		return Test.TestNotNull(TEXT("the layered import produced a Character Layer asset"), OutLayerAsset);
	}
}

// ============================================================
// (1) Cost scope: ownership, nesting, and unique dirty counting
// ============================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusAseIncrementalCostScopeTest,
	"Paper2DPlus.Editor.AsepriteIncremental.CostScopeCountsAndNests",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusAseIncrementalCostScopeTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusAsepriteIncrementalTest;

	TestNull(TEXT("no cost report is active before any scope opens"),
		FAsepriteImportCostScope::GetActive());

	{
		FAsepriteImportCostScope Outer;
		if (!TestTrue(TEXT("the outermost scope's report is the active report"),
			FAsepriteImportCostScope::GetActive() == &Outer.GetReport()))
		{
			return false;
		}

		AsepriteImportCost::AddSheets(2, /*bWritten*/ true);
		AsepriteImportCost::AddSprites(3, /*bWritten*/ false);

		{
			// A nested scope must attach, not shadow: its additions land on the outer report.
			FAsepriteImportCostScope Nested;
			TestTrue(TEXT("a nested scope leaves the outer report active"),
				FAsepriteImportCostScope::GetActive() == &Outer.GetReport());
			AsepriteImportCost::AddFlipbooks(1, /*bWritten*/ true);
			TestEqual(TEXT("a nested scope reads the outer report"),
				Nested.GetReport().FlipbooksWritten, 1);
		}

		// Unique-package dirty counting: repeat marks on one package dedupe, a second package adds.
		const int32 RunId = AseIncr_RunCounter++;
		UPackage* PackageA = CreatePackage(*FString::Printf(TEXT("/Temp/AseIncrDirtyA_%d"), RunId));
		UPackage* PackageB = CreatePackage(*FString::Printf(TEXT("/Temp/AseIncrDirtyB_%d"), RunId));
		if (!TestNotNull(TEXT("dirty fixture package A"), PackageA)
			|| !TestNotNull(TEXT("dirty fixture package B"), PackageB))
		{
			return false;
		}
		PackageA->MarkPackageDirty();
		PackageA->MarkPackageDirty();
		PackageB->MarkPackageDirty();

		const FAsepriteImportCostReport& Report = Outer.GetReport();
		TestEqual(TEXT("sheet writes accumulated"), Report.SheetsWritten, 2);
		TestEqual(TEXT("sprite skips accumulated on the skipped side"), Report.SpritesSkipped, 3);
		TestEqual(TEXT("no sprite writes were invented"), Report.SpritesWritten, 0);
		TestEqual(TEXT("the nested scope's flipbook write landed on the owner"), Report.FlipbooksWritten, 1);
		TestEqual(TEXT("two unique packages dirtied, repeat marks deduped"), Report.PackagesDirtied, 2);

		PackageA->SetDirtyFlag(false);
		PackageB->SetDirtyFlag(false);
	}

	TestNull(TEXT("destroying the owner scope clears the active report"),
		FAsepriteImportCostScope::GetActive());
	return !HasAnyErrors();
}

// ============================================================
// (2) Fresh layered import: every write counted, nothing skipped
// ============================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusAseIncrementalFreshImportTest,
	"Paper2DPlus.Editor.AsepriteIncremental.FreshLayeredImportCountsEveryWrite",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusAseIncrementalFreshImportTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusAsepriteIncrementalTest;

	const int32 RunId = AseIncr_RunCounter++;
	const FString OutputPath = FString::Printf(TEXT("/Temp/AseIncrImport_%d"), RunId);
	const FString AssetPrefix = FString::Printf(TEXT("AseIncrA%d"), RunId);

	FAsepriteImportCostReport Report;
	UPaper2DPlusCharacterLayerAsset* LayerAsset = nullptr;
	if (!AseIncr_RunTempLayeredImport(*this, OutputPath, AssetPrefix, Report, LayerAsset))
	{
		return false;
	}

	TestEqual(TEXT("both layers imported"), LayerAsset->Layers.Num(), 2);
	TestEqual(TEXT("one sheet written per layer"), Report.SheetsWritten, 2);
	TestEqual(TEXT("one sprite written per layer per frame"), Report.SpritesWritten, 4);
	TestEqual(TEXT("no sheet reported skipped before any narrowing exists"), Report.SheetsSkipped, 0);
	TestEqual(TEXT("no sprite reported skipped before any narrowing exists"), Report.SpritesSkipped, 0);
	TestEqual(TEXT("no flipbooks on the profile-less /Temp path"), Report.FlipbooksWritten, 0);
	TestEqual(TEXT("no profile entries on the profile-less /Temp path"), Report.ProfileEntriesWritten, 0);
	// Every write site is /Temp-guarded, so a headless import must leave zero packages dirty —
	// this pins the guards as much as the counter.
	TestEqual(TEXT("a /Temp import dirties no package"), Report.PackagesDirtied, 0);

	return !HasAnyErrors();
}

// ============================================================
// (3) The pre-narrowing baseline: an unchanged reimport rewrites everything
// ============================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusAseIncrementalNoOpBaselineTest,
	"Paper2DPlus.Editor.AsepriteIncremental.UnchangedReimportStillWritesEverything",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusAseIncrementalNoOpBaselineTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusAsepriteIncrementalTest;

	const int32 RunId = AseIncr_RunCounter++;
	const FString OutputPath = FString::Printf(TEXT("/Temp/AseIncrBase_%d"), RunId);
	const FString AssetPrefix = FString::Printf(TEXT("AseIncrB%d"), RunId);

	FAsepriteImportCostReport FirstReport;
	UPaper2DPlusCharacterLayerAsset* FirstAsset = nullptr;
	if (!AseIncr_RunTempLayeredImport(*this, OutputPath, AssetPrefix, FirstReport, FirstAsset))
	{
		return false;
	}

	// Byte-identical input into the same packages, with NO SourceFilePath — so no Layer Profile
	// stamps exist and every gate fails closed to "write". Now that the narrowing units are live,
	// this pins the fail-closed contract itself: an unstamped (pre-TASK-192) asset must still
	// rewrite everything rather than skip on absent evidence. The stamped path's skips are pinned
	// by LayeredReimportSkipsUnchangedAndNarrowsSprites.
	FAsepriteImportCostReport SecondReport;
	UPaper2DPlusCharacterLayerAsset* SecondAsset = nullptr;
	if (!AseIncr_RunTempLayeredImport(*this, OutputPath, AssetPrefix, SecondReport, SecondAsset))
	{
		return false;
	}

	TestTrue(TEXT("the reimport reused the same Layer Profile asset"), FirstAsset == SecondAsset);
	TestEqual(TEXT("layer count unchanged by the no-op reimport"), SecondAsset->Layers.Num(), 2);
	TestEqual(TEXT("an unchanged reimport still writes every sheet (pre-narrowing baseline)"),
		SecondReport.SheetsWritten, FirstReport.SheetsWritten);
	TestEqual(TEXT("an unchanged reimport still writes every sprite (pre-narrowing baseline)"),
		SecondReport.SpritesWritten, FirstReport.SpritesWritten);
	TestEqual(TEXT("nothing reports skipped before a narrowing unit exists"),
		SecondReport.SheetsSkipped + SecondReport.SpritesSkipped
			+ SecondReport.FlipbooksSkipped + SecondReport.ProfileEntriesSkipped, 0);

	return !HasAnyErrors();
}

// ============================================================
// (U9) One sheet writer: the composited and packed paths agree
// ============================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusAseIncrementalSheetWriterParityTest,
	"Paper2DPlus.Editor.AsepriteIncremental.SheetWritersProduceIdenticalColorOutput",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusAseIncrementalSheetWriterParityTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusAsepriteIncrementalTest;

	// (U9 characterization) Same pixels through the composited-sheet path — via ImportFile, the real
	// call path — and through the packed writer must produce byte-identical source art and identical
	// pixel-art settings, while a TangentNormal sheet keeps its distinct normal-map settings. Pinned
	// BEFORE the two writers were unified so the unification is provably behavior-preserving, and
	// kept green after it.
	const FString Token = AseIncr_NewToken();
	const FString OutputRoot = FString::Printf(TEXT("/Game/__AutomationTemp__/AseIncrU9_%s"), *Token);
	const FString Prefix = TEXT("U9Sheet");

	// Three frames of 5x4 pattern cels on one layer, no tags (ImportFile emits one "_All" flipbook).
	const TArray<uint8> Layer0 = AseIncr_LayerChunk(TEXT("Art"));
	TArray<TArray<uint8>> Frames;
	for (int32 F = 0; F < 3; ++F)
	{
		TArray<TArray<uint8>> Chunks;
		if (F == 0) { Chunks.Add(Layer0); }
		Chunks.Add(AseIncr_PatternCelChunk(0, 5, 4, static_cast<uint8>(11 * (F + 1))));
		Frames.Add(AseIncr_Frame(Chunks));
	}
	const TArray<uint8> Buffer = AseIncr_File(5, 4, Frames);

	const FString AseDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Automation"));
	const FString AseFile = AseDir / FString::Printf(TEXT("AseIncrU9_%s.ase"), *Token);
	if (!TestTrue(TEXT("the crafted .ase reaches disk for ImportFile"),
		FFileHelper::SaveArrayToFile(Buffer, *AseFile)))
	{
		return false;
	}

	TArray<UObject*> FixtureAssets;
	bool bOk = true;

	FAsepriteImportResult ImportResult = FAsepriteImporter::ImportFile(AseFile, OutputRoot, Prefix, nullptr, false);
	bOk &= TestTrue(FString::Printf(TEXT("ImportFile succeeds (%s)"), *ImportResult.ErrorMessage), ImportResult.bSuccess);
	UTexture2D* FlatSheet = ImportResult.SpriteSheet;
	if (FlatSheet) { FixtureAssets.Add(FlatSheet); }
	for (UPaperSprite* Sprite : ImportResult.Sprites) { if (Sprite) { FixtureAssets.Add(Sprite); } }
	for (UPaperFlipbook* Flipbook : ImportResult.Flipbooks) { if (Flipbook) { FixtureAssets.Add(Flipbook); } }

	FAsepriteParsedData Parsed;
	FString Error;
	bOk &= TestTrue(FString::Printf(TEXT("ParseBuffer succeeds (%s)"), *Error),
		FAsepriteImporter::ParseBuffer(Buffer, Parsed, Error));

	if (bOk && FlatSheet)
	{
		TArray<TArray<FColor>> FrameBuffers;
		FrameBuffers.Reserve(Parsed.Frames.Num());
		for (const FAsepriteFrame& Frame : Parsed.Frames)
		{
			FrameBuffers.Add(Frame.Pixels);
		}

		UTexture2D* PackedSheet = FAsepriteImporter::CreatePerLayerSpriteSheetTexture(
			FrameBuffers, Parsed.Width, Parsed.Height, OutputRoot, Prefix + TEXT("_Packed"));
		if (PackedSheet) { FixtureAssets.Add(PackedSheet); }
		bOk &= TestNotNull(TEXT("the packed writer produced a sheet"), PackedSheet);

		if (PackedSheet)
		{
			TArray<uint8> FlatBytes;
			TArray<uint8> PackedBytes;
			bOk &= TestTrue(TEXT("the composited sheet has readable source art"),
				AseIncr_ReadSourceMip(FlatSheet, FlatBytes));
			bOk &= TestTrue(TEXT("the packed sheet has readable source art"),
				AseIncr_ReadSourceMip(PackedSheet, PackedBytes));
			bOk &= TestTrue(TEXT("both sheets have identical source dimensions"),
				FlatSheet->Source.GetSizeX() == PackedSheet->Source.GetSizeX()
				&& FlatSheet->Source.GetSizeY() == PackedSheet->Source.GetSizeY());
			bOk &= TestTrue(TEXT("the two writers produced byte-identical color sheets"),
				FlatBytes.Num() > 0 && FlatBytes == PackedBytes);

			// Absolute pins on BOTH sheets: the exact layered bake fails closed on TF_Nearest +
			// TMGS_NoMipmaps, so these are contracts a shared writer must keep, not defaults.
			for (UTexture2D* Sheet : { FlatSheet, PackedSheet })
			{
				bOk &= TestTrue(TEXT("sheet keeps TMGS_NoMipmaps"), Sheet->MipGenSettings == TMGS_NoMipmaps);
				bOk &= TestTrue(TEXT("sheet keeps TC_EditorIcon"), Sheet->CompressionSettings == TC_EditorIcon);
				bOk &= TestTrue(TEXT("sheet keeps TF_Nearest"), Sheet->Filter == TF_Nearest);
				bOk &= TestTrue(TEXT("sheet keeps NeverStream"), Sheet->NeverStream);
				bOk &= TestTrue(TEXT("sheet keeps SRGB"), Sheet->SRGB);
				bOk &= TestTrue(TEXT("sheet keeps TEXTUREGROUP_Pixels2D"), Sheet->LODGroup == TEXTUREGROUP_Pixels2D);
			}
		}

		// The colour/normal distinction must survive any writer unification.
		UTexture2D* NormalSheet = FAsepriteImporter::CreateNormalMapSheetTexture(
			FrameBuffers, Parsed.Width, Parsed.Height, OutputRoot, Prefix + TEXT("_N"));
		if (NormalSheet) { FixtureAssets.Add(NormalSheet); }
		bOk &= TestNotNull(TEXT("the normal writer produced a sheet"), NormalSheet);
		if (NormalSheet)
		{
			bOk &= TestTrue(TEXT("normal sheet keeps TC_Normalmap"), NormalSheet->CompressionSettings == TC_Normalmap);
			bOk &= TestFalse(TEXT("normal sheet keeps SRGB off"), NormalSheet->SRGB != 0);
			bOk &= TestTrue(TEXT("normal sheet keeps TEXTUREGROUP_WorldNormalMap"), NormalSheet->LODGroup == TEXTUREGROUP_WorldNormalMap);
		}
	}

	for (UObject* Asset : FixtureAssets)
	{
		AseIncr_DropFixtureAsset(Asset);
	}
	IFileManager::Get().Delete(*AseFile);

	return bOk && !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusAseIncrementalCompositedTempGuardTest,
	"Paper2DPlus.Editor.AsepriteIncremental.CompositedSheetInheritsTheTempPackageGuard",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusAseIncrementalCompositedTempGuardTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusAsepriteIncrementalTest;

	// Post-U9 the composited path routes through the packed writer and inherits its /Temp guard: a
	// /Temp import must neither dirty the sheet package nor advertise it to the Asset Registry
	// (advertising a /Temp package can enqueue an SCC status against an unsubmittable path). Only
	// the SHEET is asserted here — the flat sprite/flipbook creators keep their own behavior until
	// their narrowing units restructure them.
	const int32 RunId = AseIncr_RunCounter++;
	const FString Token = AseIncr_NewToken();
	const FString OutputRoot = FString::Printf(TEXT("/Temp/AseIncrU9Guard_%d"), RunId);
	const FString Prefix = FString::Printf(TEXT("U9G%d"), RunId);

	const TArray<uint8> Layer0 = AseIncr_LayerChunk(TEXT("Art"));
	TArray<TArray<uint8>> Frames;
	for (int32 F = 0; F < 2; ++F)
	{
		TArray<TArray<uint8>> Chunks;
		if (F == 0) { Chunks.Add(Layer0); }
		Chunks.Add(AseIncr_PatternCelChunk(0, 4, 4, static_cast<uint8>(7 * (F + 1))));
		Frames.Add(AseIncr_Frame(Chunks));
	}
	const TArray<uint8> Buffer = AseIncr_File(4, 4, Frames);

	const FString AseFile = FPaths::ConvertRelativePathToFull(
		FPaths::ProjectSavedDir() / TEXT("Automation")
			/ FString::Printf(TEXT("AseIncrU9Guard_%s.ase"), *Token));
	if (!TestTrue(TEXT("the crafted .ase reaches disk"), FFileHelper::SaveArrayToFile(Buffer, *AseFile)))
	{
		return false;
	}

	// "Advertised" means the AssetCreated BROADCAST (which is what enqueues SCC work) — a passive
	// registry query still sees any live RF_Standalone object, /Temp or not, so listen to the
	// OnAssetAdded event itself. The still-unguarded flat sprites double as the positive control
	// proving the listener hears this import's broadcasts at all.
	IAssetRegistry& AssetRegistry =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	TArray<FName> AdvertisedPackages;
	FDelegateHandle AddedHandle = AssetRegistry.OnAssetAdded().AddLambda(
		[&AdvertisedPackages](const FAssetData& Data)
	{
		AdvertisedPackages.Add(Data.PackageName);
	});

	FAsepriteImportResult Result = FAsepriteImporter::ImportFile(AseFile, OutputRoot, Prefix, nullptr, false);
	AssetRegistry.OnAssetAdded().Remove(AddedHandle);

	TestTrue(FString::Printf(TEXT("ImportFile succeeds (%s)"), *Result.ErrorMessage), Result.bSuccess);
	if (Result.SpriteSheet)
	{
		UPackage* SheetPackage = Result.SpriteSheet->GetOutermost();
		TestFalse(TEXT("a /Temp composited sheet package is not dirtied"),
			SheetPackage && SheetPackage->IsDirty());
		TestFalse(TEXT("a /Temp composited sheet is never broadcast as created"),
			SheetPackage && AdvertisedPackages.Contains(SheetPackage->GetFName()));
	}
	else
	{
		AddError(TEXT("ImportFile produced no composited sheet"));
	}
	if (Result.Flipbooks.Num() > 0 && Result.Flipbooks[0])
	{
		// The flipbook creator is the one remaining unguarded /Temp advertiser (the flat sprites
		// inherited the guard with the shared reconcile) — it doubles as the positive control
		// proving the listener hears this import's broadcasts at all.
		TestTrue(TEXT("positive control: the (not yet guarded) flipbook was broadcast"),
			AdvertisedPackages.Contains(Result.Flipbooks[0]->GetOutermost()->GetFName()));
	}

	// The flat sprite/flipbook creators are not yet /Temp-guarded; sweep their side effects so the
	// fixture leaves nothing dirty or advertised behind.
	TArray<UObject*> FixtureAssets;
	if (Result.SpriteSheet) { FixtureAssets.Add(Result.SpriteSheet); }
	for (UPaperSprite* Sprite : Result.Sprites) { if (Sprite) { FixtureAssets.Add(Sprite); } }
	for (UPaperFlipbook* Flipbook : Result.Flipbooks) { if (Flipbook) { FixtureAssets.Add(Flipbook); } }
	for (UObject* Asset : FixtureAssets)
	{
		AseIncr_DropFixtureAsset(Asset);
	}
	IFileManager::Get().Delete(*AseFile);

	return !HasAnyErrors();
}

// ============================================================
// (U3) The incremental write gate seam
// ============================================================

namespace Paper2DPlusAsepriteIncrementalTest
{
	/** A saved-then-unloaded package: present in the Asset Registry, resident nowhere — the exact
	 *  state KTD2 exists for. Returns the long package name; caller cleans up with
	 *  AseIncr_DropColdRegistryPackage. */
	static bool AseIncr_CreateColdRegistryPackage(
		FAutomationTestBase& Test, FString& OutPackageName, FString& OutPackageFile)
	{
		const FString Token = AseIncr_NewToken();
		OutPackageName = FString::Printf(TEXT("/Game/__AutomationTemp__/AseIncrU3_%s/Probe"), *Token);
		OutPackageFile = FPackageName::LongPackageNameToFilename(
			OutPackageName, FPackageName::GetAssetPackageExtension());

		UPackage* Package = CreatePackage(*OutPackageName);
		UPaper2DPlusCharacterLayerAsset* Asset = Package
			? NewObject<UPaper2DPlusCharacterLayerAsset>(
				Package, TEXT("Probe"), RF_Public | RF_Standalone)
			: nullptr;
		if (!Test.TestNotNull(TEXT("cold-registry probe asset created"), Asset))
		{
			return false;
		}

		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.SaveFlags = SAVE_NoError;
		if (!Test.TestTrue(TEXT("cold-registry probe package saved to disk"),
			UPackage::SavePackage(Package, Asset, *OutPackageFile, SaveArgs)))
		{
			return false;
		}
		Package->SetDirtyFlag(false);

		TArray<UPackage*> ToUnload{ Package };
		FText UnloadFailure;
		const bool bUnloaded = UPackageTools::UnloadPackages(ToUnload, UnloadFailure, true);
		CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
		if (!Test.TestTrue(
			FString::Printf(TEXT("cold-registry probe unloaded (%s)"), *UnloadFailure.ToString()),
			bUnloaded && FindPackage(nullptr, *OutPackageName) == nullptr))
		{
			return false;
		}

		IAssetRegistry& AssetRegistry =
			FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
		AssetRegistry.ScanFilesSynchronous({ OutPackageFile }, true);
		return Test.TestTrue(TEXT("the saved probe is present in the Asset Registry"),
			FAsepriteIncrementalWrite::RegistryHasPackage(OutPackageName));
	}

	static void AseIncr_DropColdRegistryPackage(const FString& PackageName, const FString& PackageFile)
	{
		IFileManager::Get().Delete(*PackageFile);
		IAssetRegistry& AssetRegistry =
			FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
		AssetRegistry.ScanModifiedAssetFiles({ PackageFile });
		const FString Directory = FPaths::GetPath(PackageFile);
		IFileManager::Get().DeleteDirectory(*Directory, false, true);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusAseIncrementalGateVerdictTest,
	"Paper2DPlus.Editor.AsepriteIncremental.GateVerdictsFollowRegistryGridAndStamps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusAseIncrementalGateVerdictTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusAsepriteIncrementalTest;

	FString ColdPackage;
	FString ColdFile;
	if (!AseIncr_CreateColdRegistryPackage(*this, ColdPackage, ColdFile))
	{
		return false;
	}

	const FAseStampedGrid GridA{ 4, 8, 8 };
	const FAseStampedGrid GridB{ 5, 8, 8 };
	const FAseStampedGrid Unstamped{};
	TArray<FAseWriteDecision> AllDecisions;

	// KTD2's whole point: present in the registry but resident nowhere must NOT read as "create".
	TestNull(TEXT("the probe package is genuinely not loaded"), FindPackage(nullptr, *ColdPackage));
	const FAseWriteDecision ColdSkip = FAsepriteIncrementalWrite::ShouldWriteSheet(
		ColdPackage, TEXT("abc"), TEXT("abc"), GridA, GridA, false);
	AllDecisions.Add(ColdSkip);
	TestTrue(TEXT("registry-present + matching stamps on an UNLOADED package skips the payload"),
		ColdSkip.Verdict == EAseWriteVerdict::SkipPayload);

	const FAseWriteDecision Missing = FAsepriteIncrementalWrite::ShouldWriteSheet(
		ColdPackage + TEXT("_Missing"), TEXT("abc"), TEXT("abc"), GridA, GridA, false);
	AllDecisions.Add(Missing);
	TestTrue(TEXT("a package absent from the registry creates even when the stamp matches"),
		Missing.Verdict == EAseWriteVerdict::Create);

	const FAseWriteDecision GridChanged = FAsepriteIncrementalWrite::ShouldWriteSheet(
		ColdPackage, TEXT("abc"), TEXT("DIFFERENT"), GridA, GridB, false);
	AllDecisions.Add(GridChanged);
	TestTrue(TEXT("a grid mismatch short-circuits BEFORE the stamp comparison"),
		GridChanged.Verdict == EAseWriteVerdict::WriteGridChanged);

	const FAseWriteDecision AbsentStamp = FAsepriteIncrementalWrite::ShouldWriteSheet(
		ColdPackage, FString(), TEXT("abc"), GridA, GridA, false);
	AllDecisions.Add(AbsentStamp);
	TestTrue(TEXT("an absent stamp writes — it never skips"),
		AbsentStamp.Verdict == EAseWriteVerdict::WriteInputsChanged);

	const FAseWriteDecision UnstampedGrid = FAsepriteIncrementalWrite::ShouldWriteSheet(
		ColdPackage, TEXT("abc"), TEXT("abc"), Unstamped, GridA, false);
	AllDecisions.Add(UnstampedGrid);
	TestTrue(TEXT("an unstamped grid writes — it never skips"),
		UnstampedGrid.Verdict == EAseWriteVerdict::WriteGridChanged);

	const FAseWriteDecision Forced = FAsepriteIncrementalWrite::ShouldWriteSheet(
		ColdPackage, TEXT("abc"), TEXT("abc"), GridA, GridA, true);
	AllDecisions.Add(Forced);
	TestTrue(TEXT("force bypasses every content gate"),
		Forced.Verdict == EAseWriteVerdict::WriteForced);

	// Sprite gates: existence never rides the sheet verdict (KTD3), R8 re-points on a re-created
	// sheet, and a skipped sheet skips the sprite payload.
	const FAseWriteDecision SheetSkip = ColdSkip;
	FAseWriteDecision SheetWrite;
	SheetWrite.Verdict = EAseWriteVerdict::WriteInputsChanged;
	SheetWrite.Reason = TEXT("test");

	const FAseWriteDecision SpriteMissing = FAsepriteIncrementalWrite::ShouldWriteSpritePayload(
		ColdPackage + TEXT("_Missing"), SheetSkip, false, GridA, GridA, false);
	AllDecisions.Add(SpriteMissing);
	TestTrue(TEXT("a deleted sprite under an unchanged sheet is re-created"),
		SpriteMissing.Verdict == EAseWriteVerdict::Create);

	const FAseWriteDecision SpriteSkip = FAsepriteIncrementalWrite::ShouldWriteSpritePayload(
		ColdPackage, SheetSkip, false, GridA, GridA, false);
	AllDecisions.Add(SpriteSkip);
	TestTrue(TEXT("an existing sprite under a skipped sheet skips its payload"),
		SpriteSkip.Verdict == EAseWriteVerdict::SkipPayload);

	const FAseWriteDecision SpriteRepoint = FAsepriteIncrementalWrite::ShouldWriteSpritePayload(
		ColdPackage, SheetSkip, /*bSheetObjectRecreated*/ true, GridA, GridA, false);
	AllDecisions.Add(SpriteRepoint);
	TestTrue(TEXT("a re-created sheet object forces the sprite to re-point (R8)"),
		SpriteRepoint.Verdict == EAseWriteVerdict::WriteInputsChanged);

	const FAseWriteDecision SpriteChanged = FAsepriteIncrementalWrite::ShouldWriteSpritePayload(
		ColdPackage, SheetWrite, false, GridA, GridA, false);
	AllDecisions.Add(SpriteChanged);
	TestTrue(TEXT("a written sheet makes its sprites bounds-compare candidates"),
		SpriteChanged.Verdict == EAseWriteVerdict::WriteInputsChanged);

	const FAseWriteDecision SpriteGrid = FAsepriteIncrementalWrite::ShouldWriteSpritePayload(
		ColdPackage, SheetSkip, false, GridA, GridB, false);
	AllDecisions.Add(SpriteGrid);
	TestTrue(TEXT("a grid change re-initializes sprites regardless of the sheet verdict"),
		SpriteGrid.Verdict == EAseWriteVerdict::WriteGridChanged);

	// Flipbook gates.
	const FAseWriteDecision FlipSkip = FAsepriteIncrementalWrite::ShouldWriteFlipbook(
		ColdPackage, TEXT("hash"), TEXT("hash"), false);
	AllDecisions.Add(FlipSkip);
	TestTrue(TEXT("an unchanged flipbook structure skips"),
		FlipSkip.Verdict == EAseWriteVerdict::SkipPayload);

	const FAseWriteDecision FlipChanged = FAsepriteIncrementalWrite::ShouldWriteFlipbook(
		ColdPackage, TEXT("hash"), TEXT("otherhash"), false);
	AllDecisions.Add(FlipChanged);
	TestTrue(TEXT("a changed flipbook structure writes"),
		FlipChanged.Verdict == EAseWriteVerdict::WriteInputsChanged);

	const FAseWriteDecision FlipMissing = FAsepriteIncrementalWrite::ShouldWriteFlipbook(
		ColdPackage + TEXT("_Missing"), TEXT("hash"), TEXT("hash"), false);
	AllDecisions.Add(FlipMissing);
	TestTrue(TEXT("a flipbook deleted from disk is re-created even with a matching stamp"),
		FlipMissing.Verdict == EAseWriteVerdict::Create);

	for (const FAseWriteDecision& Decision : AllDecisions)
	{
		TestFalse(TEXT("every verdict carries a non-empty reason"), Decision.Reason.IsEmpty());
	}

	AseIncr_DropColdRegistryPackage(ColdPackage, ColdFile);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusAseIncrementalTagStructureHashTest,
	"Paper2DPlus.Editor.AsepriteIncremental.TagStructureHashDiscriminates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusAseIncrementalTagStructureHashTest::RunTest(const FString& Parameters)
{
	const TArray<int32> Durations{ 100, 100, 50, 100 };
	const TArray<FString> Names{ TEXT("A_02"), TEXT("A_03"), TEXT("A_04"), TEXT("A_05") };

	const FString Base = FAsepriteIncrementalWrite::ComputeTagStructureHash(2, 5, Durations, Names);
	const FString Same = FAsepriteIncrementalWrite::ComputeTagStructureHash(2, 5, Durations, Names);
	TestEqual(TEXT("identical structure hashes equal"), Same, Base);

	TArray<int32> Retimed = Durations;
	Retimed[2] = 60;
	TestNotEqual(TEXT("a changed duration changes the hash"),
		FAsepriteIncrementalWrite::ComputeTagStructureHash(2, 5, Retimed, Names), Base);

	TestNotEqual(TEXT("a changed range changes the hash"),
		FAsepriteIncrementalWrite::ComputeTagStructureHash(2, 6, Durations, Names), Base);

	TArray<FString> Reordered = Names;
	Reordered.Swap(1, 2);
	TestNotEqual(TEXT("a reordered sprite list changes the hash"),
		FAsepriteIncrementalWrite::ComputeTagStructureHash(2, 5, Durations, Reordered), Base);

	TArray<FString> Renamed = Names;
	Renamed[0] = TEXT("B_02");
	TestNotEqual(TEXT("a renamed sprite changes the hash"),
		FAsepriteIncrementalWrite::ComputeTagStructureHash(2, 5, Durations, Renamed), Base);

	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusAseIncrementalSettingsReconcileTest,
	"Paper2DPlus.Editor.AsepriteIncremental.SettingsReconciliationRepairsDrift",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusAseIncrementalSettingsReconcileTest::RunTest(const FString& Parameters)
{
	UTexture2D* Sheet = NewObject<UTexture2D>(GetTransientPackage());
	Sheet->MipGenSettings = TMGS_NoMipmaps;
	Sheet->CompressionSettings = TC_EditorIcon;
	Sheet->Filter = TF_Nearest;
	Sheet->NeverStream = true;
	Sheet->SRGB = true;
	Sheet->LODGroup = TEXTUREGROUP_Pixels2D;

	TestFalse(TEXT("identical pixel-art settings report no drift"),
		FAsepriteIncrementalWrite::ReconcileSheetSettings(Sheet, /*bIsNormalMap*/ false));

	Sheet->Filter = TF_Bilinear;
	TestTrue(TEXT("a drifted Filter reports 'differed'"),
		FAsepriteIncrementalWrite::ReconcileSheetSettings(Sheet, false));
	TestTrue(TEXT("the drifted Filter is repaired to TF_Nearest"), Sheet->Filter == TF_Nearest);
	TestFalse(TEXT("the repaired sheet reports no further drift"),
		FAsepriteIncrementalWrite::ReconcileSheetSettings(Sheet, false));

	// Normal-map contract: same reconciliation, the normal set of values.
	UTexture2D* Normal = NewObject<UTexture2D>(GetTransientPackage());
	Normal->MipGenSettings = TMGS_NoMipmaps;
	Normal->CompressionSettings = TC_Normalmap;
	Normal->Filter = TF_Nearest;
	Normal->NeverStream = true;
	Normal->SRGB = false;
	Normal->LODGroup = TEXTUREGROUP_WorldNormalMap;
	TestFalse(TEXT("identical normal-map settings report no drift"),
		FAsepriteIncrementalWrite::ReconcileSheetSettings(Normal, /*bIsNormalMap*/ true));
	Normal->SRGB = true;
	TestTrue(TEXT("a drifted normal-map SRGB reports 'differed'"),
		FAsepriteIncrementalWrite::ReconcileSheetSettings(Normal, true));
	TestFalse(TEXT("the repaired normal map keeps SRGB off"), Normal->SRGB != 0);

	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusAseIncrementalDerivedTightBoxTest,
	"Paper2DPlus.Editor.AsepriteIncremental.DerivedTightBoxMatchesEngineSemantics",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusAseIncrementalDerivedTightBoxTest::RunTest(const FString& Parameters)
{
	// A 6x5 cell with an opaque rectangle spanning x 1..4, y 2..3.
	const int32 W = 6;
	const int32 H = 5;
	TArray<FColor> Cell;
	Cell.Init(FColor(0, 0, 0, 0), W * H);
	for (int32 Y = 2; Y <= 3; ++Y)
	{
		for (int32 X = 1; X <= 4; ++X)
		{
			Cell[Y * W + X] = FColor(10, 20, 30, 255);
		}
	}

	FVector2D Center;
	FVector2D Size;
	FAsepriteIncrementalWrite::DeriveTightBoxForCell(Cell, W, H, FIntPoint(0, 0), 0.0f, Center, Size);
	TestTrue(TEXT("tight box size matches the opaque rect"), Size.Equals(FVector2D(4.0f, 2.0f), 0.001f));
	TestTrue(TEXT("tight box center is the recentered engine form"), Center.Equals(FVector2D(3.0f, 3.0f), 0.001f));

	FAsepriteIncrementalWrite::DeriveTightBoxForCell(Cell, W, H, FIntPoint(10, 20), 0.0f, Center, Size);
	TestTrue(TEXT("the cell's sheet offset shifts the center"), Center.Equals(FVector2D(13.0f, 23.0f), 0.001f));

	// The threshold is STRICTLY greater-than, exactly like FBitmap: at threshold 0.5 (int 127),
	// alpha 127 is out and alpha 128 is in.
	TArray<FColor> Faint;
	Faint.Init(FColor(0, 0, 0, 0), W * H);
	Faint[2 * W + 2] = FColor(0, 0, 0, 127);
	FAsepriteIncrementalWrite::DeriveTightBoxForCell(Faint, W, H, FIntPoint(0, 0), 0.5f, Center, Size);
	TestTrue(TEXT("alpha equal to the threshold is NOT occupied (degenerate box)"),
		Size.Equals(FVector2D(1.0f, 1.0f), 0.001f));
	Faint[2 * W + 2] = FColor(0, 0, 0, 128);
	FAsepriteIncrementalWrite::DeriveTightBoxForCell(Faint, W, H, FIntPoint(0, 0), 0.5f, Center, Size);
	TestTrue(TEXT("alpha above the threshold is occupied"),
		Size.Equals(FVector2D(1.0f, 1.0f), 0.001f)
		&& Center.Equals(FVector2D(2.5f, 2.5f), 0.001f));

	// The engine's degenerate all-empty convergence: top and left pull all the way, bottom and
	// right stay — a 1x1 box on the cell's bottom-right texel.
	TArray<FColor> Empty;
	Empty.Init(FColor(0, 0, 0, 0), 4 * 3);
	FAsepriteIncrementalWrite::DeriveTightBoxForCell(Empty, 4, 3, FIntPoint(0, 0), 0.0f, Center, Size);
	TestTrue(TEXT("an all-empty cell converges on its bottom-right texel"),
		Size.Equals(FVector2D(1.0f, 1.0f), 0.001f)
		&& Center.Equals(FVector2D(3.5f, 2.5f), 0.001f));

	// Bounds-compare refinement: both geometries must match to skip.
	const FVector2D C1(3.0f, 3.0f), S1(4.0f, 2.0f), C2(4.0f, 3.0f);
	TestTrue(TEXT("equal boxes skip"),
		FAsepriteIncrementalWrite::ShouldWriteSpriteForDerivedBounds(
			TEXT("/Temp/P"), C1, S1, C1, S1, C1, S1, C1, S1).Verdict == EAseWriteVerdict::SkipPayload);
	TestTrue(TEXT("a moved render box writes"),
		FAsepriteIncrementalWrite::ShouldWriteSpriteForDerivedBounds(
			TEXT("/Temp/P"), C1, S1, C2, S1, C1, S1, C1, S1).Verdict == EAseWriteVerdict::WriteInputsChanged);
	TestTrue(TEXT("a moved collision box alone still writes"),
		FAsepriteIncrementalWrite::ShouldWriteSpriteForDerivedBounds(
			TEXT("/Temp/P"), C1, S1, C1, S1, C1, S1, C2, S1).Verdict == EAseWriteVerdict::WriteInputsChanged);

	return !HasAnyErrors();
}

// ============================================================
// (U4+U5) Gated layered reimport over a /Game fixture
// ============================================================

namespace Paper2DPlusAsepriteIncrementalTest
{
	/** Reads the sprite's serialized RENDER tight box (reflection — the members are protected)
	 *  plus its alpha threshold, so oracle comparisons use the sprite's own settings. */
	static bool AseIncrTest_ReadRenderBox(
		UPaperSprite& Sprite, FVector2D& OutCenter, FVector2D& OutSize, float& OutAlphaThreshold)
	{
		FStructProperty* Property = FindFProperty<FStructProperty>(
			UPaperSprite::StaticClass(), TEXT("RenderGeometry"));
		FSpriteGeometryCollection* Geometry = Property
			? Property->ContainerPtrToValuePtr<FSpriteGeometryCollection>(&Sprite)
			: nullptr;
		if (!Geometry || Geometry->Shapes.Num() != 1)
		{
			return false;
		}
		OutCenter = Geometry->Shapes[0].BoxPosition;
		OutSize = Geometry->Shapes[0].BoxSize;
		OutAlphaThreshold = Geometry->AlphaThreshold;
		return true;
	}

	static bool AseIncr_IsPackageDirty(const UObject* Asset)
	{
		return Asset && Asset->GetOutermost() && Asset->GetOutermost()->IsDirty();
	}

	/** Clears the dirty flag on every fixture package under OutputRoot so the next pass measures
	 *  only its own dirt. */
	static void AseIncr_ClearAllDirtyUnder(const FString& OutputRoot)
	{
		IAssetRegistry& AssetRegistry =
			FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
		TArray<FAssetData> Assets;
		AssetRegistry.GetAssetsByPath(FName(*OutputRoot), Assets, true, false);
		for (const FAssetData& Data : Assets)
		{
			if (UObject* Asset = Data.IsAssetLoaded() ? Data.GetAsset() : nullptr)
			{
				if (UPackage* Package = Asset->GetOutermost())
				{
					Package->SetDirtyFlag(false);
				}
			}
		}
	}

	/** Drops every fixture asset under OutputRoot: registry row out, dirty flag cleared, packages
	 *  unloaded and GC'd (a live RF_Standalone object still answers registry queries, so teardown
	 *  is not done until the objects are gone). Nothing was saved to disk — no file half. */
	static void AseIncr_DropAllUnder(FAutomationTestBase& Test, const FString& OutputRoot)
	{
		IAssetRegistry& AssetRegistry =
			FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
		TArray<FAssetData> Assets;
		AssetRegistry.GetAssetsByPath(FName(*OutputRoot), Assets, true, false);
		TArray<UPackage*> Packages;
		TArray<UTexture*> Textures;
		for (const FAssetData& Data : Assets)
		{
			UObject* Asset = Data.IsAssetLoaded() ? Data.GetAsset() : nullptr;
			if (!Asset)
			{
				continue;
			}
			if (UTexture* Texture = Cast<UTexture>(Asset))
			{
				Textures.Add(Texture);
			}
			if (Asset->IsAsset())
			{
				FAssetRegistryModule::AssetDeleted(Asset);
			}
			if (UPackage* Package = Asset->GetOutermost())
			{
				Package->SetDirtyFlag(false);
				Packages.AddUnique(Package);
			}
		}
		if (Textures.Num() > 0)
		{
			FTextureCompilingManager::Get().FinishCompilation(Textures);
		}
		if (Packages.Num() > 0)
		{
			FText UnloadFailure;
			UPackageTools::UnloadPackages(Packages, UnloadFailure, true);
		}
		CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
	}

	struct FAseIncrGateRun
	{
		FAsepriteImportCostReport Report;
		UPaper2DPlusCharacterLayerAsset* LayerAsset = nullptr;
	};

	/** One full-pipeline layered import (profile branch included) with the stamps live, measured
	 *  under its own cost scope. The watcher must be shut down around these runs — the import tail
	 *  would otherwise register the fixture's source dir and race later passes. */
	static bool AseIncr_RunGateImport(
		FAutomationTestBase& Test, const TCHAR* Label,
		const TArray<uint8>& Buffer, const FString& AseFile, const FString& OutputRoot,
		const FString& AssetPrefix, UPaper2DPlusCharacterLayerAsset* ExistingAsset,
		FAseIncrGateRun& Out, const bool bForceFullReimport = false)
	{
		Out.LayerAsset = nullptr;
		if (!Test.TestTrue(FString::Printf(TEXT("%s: fixture .ase written"), Label),
			FFileHelper::SaveArrayToFile(Buffer, *AseFile)))
		{
			return false;
		}

		FAsepriteParsedData Parsed;
		FString Error;
		if (!Test.TestTrue(FString::Printf(TEXT("%s: ParseFile succeeds (%s)"), Label, *Error),
			FAsepriteImporter::ParseFile(AseFile, Parsed, Error)))
		{
			return false;
		}
		const FPerLayerBufferMap PerLayerBuffers = FAsepriteImporter::CompositePerLayer(Parsed);

		FAsepriteLayerImportSettings Settings;
		FAsepriteImporter::InitDefaultSelection(Parsed, Settings);
		Settings.ImportMode = EAsepriteImportMode::LayerAssetNewProfile;
		Settings.OutputPath = OutputRoot;
		Settings.AssetPrefix = AssetPrefix;
		Settings.bOrganizeIntoSubfolders = false;
		Settings.bUserConfirmed = true;
		Settings.bKeepSourceInProject = false;
		Settings.SourceFilePath = AseFile;
		Settings.bForceFullReimport = bForceFullReimport;
		if (ExistingAsset)
		{
			Settings.ExistingLayerAsset = ExistingAsset;
		}

		FAsepriteImportCostScope CostScope;
		UObject* Result = FAsepriteImporter::ImportAsLayeredAsset(Parsed, PerLayerBuffers, Settings);
		Out.Report = CostScope.GetReport();
		Out.LayerAsset = Cast<UPaper2DPlusCharacterLayerAsset>(Result);
		return Test.TestNotNull(
			FString::Printf(TEXT("%s: layered import produced the Layer Profile"), Label), Out.LayerAsset);
	}

	/** Two-layer fixture: "Body" carries a sub-rect of art per frame (movable tight bounds), "Coat"
	 *  carries a STATIC sub-rect in the top-left — so the flat composite's tight bounds are the
	 *  union of the two rects and genuinely move when Body grows. One tag spans every frame. 8x8. */
	static TArray<uint8> AseIncr_GateFixtureFile(
		const int32 FrameCount, const FIntRect& BodyArtRect, const FColor& BodyColor,
		const bool bAppendJunkChunk = false)
	{
		TArray<TArray<uint8>> Frames;
		for (int32 F = 0; F < FrameCount; ++F)
		{
			TArray<TArray<uint8>> Chunks;
			if (F == 0)
			{
				if (bAppendJunkChunk)
				{
					Chunks.Add(AseIncr_JunkChunk(77));
				}
				Chunks.Add(AseIncr_LayerChunk(TEXT("Body")));
				Chunks.Add(AseIncr_LayerChunk(TEXT("Coat")));
				Chunks.Add(AseIncr_TagsChunk({ MakeTuple(FString(TEXT("Idle")),
					static_cast<uint16>(0), static_cast<uint16>(FrameCount - 1)) }));
			}
			Chunks.Add(AseIncr_SubRectCelChunk(0, 8, 8, BodyArtRect, BodyColor));
			Chunks.Add(AseIncr_SubRectCelChunk(1, 8, 8, FIntRect(0, 0, 3, 3), FColor::Green));
			Frames.Add(AseIncr_Frame(Chunks));
		}
		return AseIncr_File(8, 8, Frames);
	}

	/** RAII: the watcher is shut down for the duration of a direct-import fixture and restored
	 *  after — its import-tail RefreshAssetMapping would otherwise register the fixture's source
	 *  directory and auto-reimport races the later passes. */
	class FAseIncrScopedWatcherShutdown final
	{
	public:
		explicit FAseIncrScopedWatcherShutdown(FAutomationTestBase& InTest)
			: Test(InTest)
		{
			Test.TestTrue(TEXT("the watcher released every callback for the fixture window"),
				FTextureWatcherService::Get().Shutdown());
		}
		~FAseIncrScopedWatcherShutdown()
		{
			FTextureWatcherService::Get().Initialize();
			Test.TestTrue(TEXT("the watcher is restored after the fixture window"),
				FTextureWatcherService::Get().IsInitialized());
		}
	private:
		FAutomationTestBase& Test;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusAseIncrementalLayeredGateTest,
	"Paper2DPlus.Editor.AsepriteIncremental.LayeredReimportSkipsUnchangedAndNarrowsSprites",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusAseIncrementalLayeredGateTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusAsepriteIncrementalTest;

	const FString Token = AseIncr_NewToken();
	const FString OutputRoot = FString::Printf(TEXT("/Game/__AutomationTemp__/AseIncrU45_%s"), *Token);
	const FString Prefix = TEXT("U45Hero");
	const FString AseFile = FPaths::ConvertRelativePathToFull(
		FPaths::ProjectSavedDir() / TEXT("Automation") / FString::Printf(TEXT("AseIncrU45_%s.ase"), *Token));

	FAseIncrScopedWatcherShutdown WatcherWindow(*this);

	// --- Pass 1: fresh import — everything creates. ---
	const FIntRect SmallArt(2, 2, 5, 5);
	FAseIncrGateRun Fresh;
	if (!AseIncr_RunGateImport(*this, TEXT("fresh"),
		AseIncr_GateFixtureFile(3, SmallArt, FColor::Red), AseFile, OutputRoot, Prefix, nullptr, Fresh))
	{
		AseIncr_DropAllUnder(*this, OutputRoot);
		IFileManager::Get().Delete(*AseFile);
		return false;
	}
	TestEqual(TEXT("fresh: composited + two layer sheets written"), Fresh.Report.SheetsWritten, 3);
	TestEqual(TEXT("fresh: flat + per-layer sprites written"), Fresh.Report.SpritesWritten, 9);
	TestEqual(TEXT("fresh: nothing skipped on a first import"),
		Fresh.Report.SheetsSkipped + Fresh.Report.SpritesSkipped, 0);

	UPaper2DPlusCharacterLayerAsset* LayerAsset = Fresh.LayerAsset;
	const FCharacterLayer* BodyLayer = LayerAsset->Layers.FindByPredicate(
		[](const FCharacterLayer& L) { return L.LayerName == TEXT("Body"); });
	const FCharacterLayer* CoatLayer = LayerAsset->Layers.FindByPredicate(
		[](const FCharacterLayer& L) { return L.LayerName == TEXT("Coat"); });
	if (!TestNotNull(TEXT("Body layer imported"), BodyLayer)
		|| !TestNotNull(TEXT("Coat layer imported"), CoatLayer))
	{
		AseIncr_DropAllUnder(*this, OutputRoot);
		IFileManager::Get().Delete(*AseFile);
		return false;
	}
	UTexture2D* BodySheet = BodyLayer->SourceTexture.Get();
	UTexture2D* CoatSheet = CoatLayer->SourceTexture.Get();
	UPaper2DPlusCharacterProfileAsset* Profile = LayerAsset->BaseProfile.Get();
	TestNotNull(TEXT("the base Character Profile is resident"), Profile);
	TArray<UPaperSprite*> BodySprites;
	for (const TSoftObjectPtr<UPaperSprite>& Ref : BodyLayer->AnimationSprites[0].Sprites)
	{
		BodySprites.AddUnique(Ref.Get());
	}
	TestEqual(TEXT("three Body sprites resident"), BodySprites.Num(), 3);

	// --- Pass 2: byte-identical reimport — layer payloads skip, packages stay clean. ---
	AseIncr_ClearAllDirtyUnder(OutputRoot);
	FAseIncrGateRun Unchanged;
	if (AseIncr_RunGateImport(*this, TEXT("unchanged"),
		AseIncr_GateFixtureFile(3, SmallArt, FColor::Red), AseFile, OutputRoot, Prefix, LayerAsset, Unchanged))
	{
		TestEqual(TEXT("unchanged: no sheet writes at all (composited included — U6)"), Unchanged.Report.SheetsWritten, 0);
		TestEqual(TEXT("unchanged: all three sheets skip"), Unchanged.Report.SheetsSkipped, 3);
		TestEqual(TEXT("unchanged: no sprite writes at all (flat included — U6)"), Unchanged.Report.SpritesWritten, 0);
		TestEqual(TEXT("unchanged: every sprite skips (6 per-layer + 3 flat)"), Unchanged.Report.SpritesSkipped, 9);
		TestFalse(TEXT("unchanged: the Body sheet package stays clean"), AseIncr_IsPackageDirty(BodySheet));
		TestFalse(TEXT("unchanged: the Coat sheet package stays clean"), AseIncr_IsPackageDirty(CoatSheet));
		for (UPaperSprite* Sprite : BodySprites)
		{
			TestFalse(TEXT("unchanged: a Body sprite package stays clean"), AseIncr_IsPackageDirty(Sprite));
		}
		TestFalse(TEXT("unchanged: the Layer Profile stays clean (stamp unchanged — KTD8's negative)"),
			AseIncr_IsPackageDirty(LayerAsset));
		TestEqual(TEXT("unchanged: no flipbook is rewritten (U7)"), Unchanged.Report.FlipbooksWritten, 0);
		TestEqual(TEXT("unchanged: the tag's flipbook skips (U7)"), Unchanged.Report.FlipbooksSkipped, 1);
		TestEqual(TEXT("unchanged: no profile entry is rewritten (U7)"), Unchanged.Report.ProfileEntriesWritten, 0);
		TestFalse(TEXT("unchanged: the Character Profile package stays clean (U7)"),
			AseIncr_IsPackageDirty(Profile));
	}

	// --- Pass 3: recolor INSIDE the Body silhouette — its sheet rewrites, its sprites do not. ---
	AseIncr_ClearAllDirtyUnder(OutputRoot);
	FAseIncrGateRun Recolored;
	if (AseIncr_RunGateImport(*this, TEXT("recolor"),
		AseIncr_GateFixtureFile(3, SmallArt, FColor::Blue), AseFile, OutputRoot, Prefix, LayerAsset, Recolored))
	{
		TestEqual(TEXT("recolor: composited + Body sheets write"), Recolored.Report.SheetsWritten, 2);
		TestEqual(TEXT("recolor: the Coat sheet skips"), Recolored.Report.SheetsSkipped, 1);
		TestEqual(TEXT("recolor: NO sprite writes — every bound is proven unmoved, flat included (U6)"),
			Recolored.Report.SpritesWritten, 0);
		TestEqual(TEXT("recolor: all nine sprites skip"), Recolored.Report.SpritesSkipped, 9);
		TestTrue(TEXT("recolor: the Body sheet package is dirtied"), AseIncr_IsPackageDirty(BodySheet));
		TestFalse(TEXT("recolor: the Coat sheet package stays clean"), AseIncr_IsPackageDirty(CoatSheet));
		for (UPaperSprite* Sprite : BodySprites)
		{
			TestFalse(TEXT("recolor: a Body sprite package stays clean"), AseIncr_IsPackageDirty(Sprite));
		}
		TestTrue(TEXT("recolor: the Layer Profile is dirtied by the restamp (KTD8's positive)"),
			AseIncr_IsPackageDirty(LayerAsset));
		TestEqual(TEXT("recolor: a pixel edit rewrites no flipbook (U7/R2)"),
			Recolored.Report.FlipbooksWritten, 0);
		TestFalse(TEXT("recolor: the Character Profile package stays clean (U7)"),
			AseIncr_IsPackageDirty(Profile));
	}

	// --- Pass 4: GROW the Body art beyond its previous bounds — the crop-bug pin (R5). ---
	AseIncr_ClearAllDirtyUnder(OutputRoot);
	const FIntRect GrownArt(2, 2, 7, 7);
	FAseIncrGateRun Grown;
	if (AseIncr_RunGateImport(*this, TEXT("grown"),
		AseIncr_GateFixtureFile(3, GrownArt, FColor::Blue), AseFile, OutputRoot, Prefix, LayerAsset, Grown))
	{
		TestEqual(TEXT("grown: composited + Body sheets write"), Grown.Report.SheetsWritten, 2);
		TestEqual(TEXT("grown: every moved sprite writes — 3 Body + 3 flat (the union grew, R5 on the primary render path)"),
			Grown.Report.SpritesWritten, 6);
		TestEqual(TEXT("grown: only Coat sprites skip"), Grown.Report.SpritesSkipped, 3);
		for (int32 Index = 0; Index < BodySprites.Num(); ++Index)
		{
			UPaperSprite* Sprite = BodySprites[Index];
			TestTrue(TEXT("grown: a moved Body sprite package is dirtied"), AseIncr_IsPackageDirty(Sprite));

			// Stored box == engine oracle == the value expected from the grown rect: art x/y 2..6
			// in an 8x8 cell at grid offset (8*i, 0) — size (5,5), center (8i+4.5, 4.5). The
			// pre-grow box (size 3,3) differing is the not-equal guard against a vacuous pass.
			FVector2D StoredCenter, StoredSize;
			float Threshold = 0.0f;
			if (TestTrue(TEXT("grown: the Body sprite's render box is readable"),
				Sprite && AseIncrTest_ReadRenderBox(*Sprite, StoredCenter, StoredSize, Threshold)))
			{
				const FVector2D ExpectedCenter(8.0f * Index + 4.5f, 4.5f);
				const FVector2D ExpectedSize(5.0f, 5.0f);
				TestTrue(TEXT("grown: stored bounds match the grown art (no stale crop)"),
					StoredCenter.Equals(ExpectedCenter, 0.01f) && StoredSize.Equals(ExpectedSize, 0.01f));
				TestFalse(TEXT("grown: the box genuinely moved (guard against a vacuous oracle)"),
					StoredSize.Equals(FVector2D(3.0f, 3.0f), 0.01f));

				FVector2D OraclePos, OracleSize;
				Sprite->FindTextureBoundingBox(Threshold, OraclePos, OracleSize);
				TestTrue(TEXT("grown: the engine oracle agrees with the stored box"),
					(OraclePos + OracleSize * 0.5f).Equals(StoredCenter, 0.01f)
					&& OracleSize.Equals(StoredSize, 0.01f));
			}
		}
	}

	// --- Pass 5: grid change (a fourth frame) re-initializes everything. ---
	AseIncr_ClearAllDirtyUnder(OutputRoot);
	FAseIncrGateRun Regrided;
	if (AseIncr_RunGateImport(*this, TEXT("regrid"),
		AseIncr_GateFixtureFile(4, GrownArt, FColor::Blue), AseFile, OutputRoot, Prefix, LayerAsset, Regrided))
	{
		TestEqual(TEXT("regrid: every sheet writes"), Regrided.Report.SheetsWritten, 3);
		TestEqual(TEXT("regrid: no sheet skips"), Regrided.Report.SheetsSkipped, 0);
		TestEqual(TEXT("regrid: every sprite re-initializes (4 flat + 8 per-layer)"),
			Regrided.Report.SpritesWritten, 12);
		TestEqual(TEXT("regrid: no sprite skips"), Regrided.Report.SpritesSkipped, 0);
		TestEqual(TEXT("regrid: the range-grown tag's flipbook rewrites (U7)"),
			Regrided.Report.FlipbooksWritten, 1);
	}

	// --- Pass 6: byte-changed no-op (a junk chunk) — KTD8's whole point. Exactly ONE package
	// dirties: the Layer Profile, restamped so the startup reconcile never re-queues this file. ---
	AseIncr_ClearAllDirtyUnder(OutputRoot);
	FAseIncrGateRun JunkPass;
	if (AseIncr_RunGateImport(*this, TEXT("junk"),
		AseIncr_GateFixtureFile(4, GrownArt, FColor::Blue, /*bAppendJunkChunk*/ true),
		AseFile, OutputRoot, Prefix, LayerAsset, JunkPass))
	{
		TestEqual(TEXT("junk: nothing writes — the byte change has no asset effect"),
			JunkPass.Report.SheetsWritten + JunkPass.Report.SpritesWritten
				+ JunkPass.Report.FlipbooksWritten + JunkPass.Report.ProfileEntriesWritten, 0);
		TestEqual(TEXT("junk: exactly ONE package dirties — the Layer Profile (KTD8/R12)"),
			JunkPass.Report.PackagesDirtied, 1);
		TestTrue(TEXT("junk: the dirtied package IS the Layer Profile"), AseIncr_IsPackageDirty(LayerAsset));
		TestTrue(TEXT("junk: the decision audit names the skips (R10)"),
			JunkPass.Report.DecisionLines.Num() > 0);
		if (LayerAsset->ImportedAseSources.Num() > 0)
		{
			TestEqual(TEXT("junk: the restamped hash matches the file on disk — no startup re-queue"),
				LayerAsset->ImportedAseSources[0].ContentHash,
				FAsepriteImporter::HashAseFileContent(AseFile));
		}
	}

	// --- Pass 7: Force Full Reimport bypasses every gate (U8). ---
	AseIncr_ClearAllDirtyUnder(OutputRoot);
	FAseIncrGateRun Forced;
	if (AseIncr_RunGateImport(*this, TEXT("forced"),
		AseIncr_GateFixtureFile(4, GrownArt, FColor::Blue, /*bAppendJunkChunk*/ true),
		AseFile, OutputRoot, Prefix, LayerAsset, Forced, /*bForceFullReimport*/ true))
	{
		TestEqual(TEXT("forced: every sheet writes"), Forced.Report.SheetsWritten, 3);
		TestEqual(TEXT("forced: every sprite writes"), Forced.Report.SpritesWritten, 12);
		TestEqual(TEXT("forced: the flipbook writes"), Forced.Report.FlipbooksWritten, 1);
		TestEqual(TEXT("forced: nothing skips"),
			Forced.Report.SheetsSkipped + Forced.Report.SpritesSkipped + Forced.Report.FlipbooksSkipped, 0);
		TestEqual(TEXT("forced: sheet + sprite + flipbook packages all dirty (profile rows unchanged)"),
			Forced.Report.PackagesDirtied, 16);
	}

	AseIncr_DropAllUnder(*this, OutputRoot);
	IFileManager::Get().Delete(*AseFile);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusAseIncrementalNormalPairGateTest,
	"Paper2DPlus.Editor.AsepriteIncremental.NormalPairedSheetGatesAndAttachSkips",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusAseIncrementalNormalPairGateTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusAsepriteIncrementalTest;

	const FString Token = AseIncr_NewToken();
	const FString OutputRoot = FString::Printf(TEXT("/Game/__AutomationTemp__/AseIncrU4N_%s"), *Token);
	const FString Prefix = TEXT("U4NHero");
	const FString AseFile = FPaths::ConvertRelativePathToFull(
		FPaths::ProjectSavedDir() / TEXT("Automation") / FString::Printf(TEXT("AseIncrU4N_%s.ase"), *Token));

	// Base "Body" + paired "Body_n" (the default normal-suffix convention), two frames, no tags.
	const auto MakeFile = [](const uint8 BodySeed, const uint8 NormalSeed)
	{
		TArray<TArray<uint8>> Frames;
		for (int32 F = 0; F < 2; ++F)
		{
			TArray<TArray<uint8>> Chunks;
			if (F == 0)
			{
				Chunks.Add(AseIncr_LayerChunk(TEXT("Body")));
				Chunks.Add(AseIncr_LayerChunk(TEXT("Body_n")));
			}
			Chunks.Add(AseIncr_PatternCelChunk(0, 6, 6, static_cast<uint8>(BodySeed + F)));
			Chunks.Add(AseIncr_PatternCelChunk(1, 6, 6, static_cast<uint8>(NormalSeed + F)));
			Frames.Add(AseIncr_Frame(Chunks));
		}
		return AseIncr_File(6, 6, Frames);
	};

	FAseIncrScopedWatcherShutdown WatcherWindow(*this);

	FAseIncrGateRun Fresh;
	if (!AseIncr_RunGateImport(*this, TEXT("fresh"),
		MakeFile(20, 90), AseFile, OutputRoot, Prefix, nullptr, Fresh))
	{
		AseIncr_DropAllUnder(*this, OutputRoot);
		IFileManager::Get().Delete(*AseFile);
		return false;
	}
	TestEqual(TEXT("fresh: composited + Body + Body_Sheet_N written"), Fresh.Report.SheetsWritten, 3);
	TestEqual(TEXT("fresh: one visual layer imported (the normal was consumed)"),
		Fresh.LayerAsset->Layers.Num(), 1);

	UPaper2DPlusCharacterLayerAsset* LayerAsset = Fresh.LayerAsset;
	TArray<UPaperSprite*> BodySprites;
	for (const TSoftObjectPtr<UPaperSprite>& Ref : LayerAsset->Layers[0].AnimationSprites[0].Sprites)
	{
		BodySprites.AddUnique(Ref.Get());
	}
	TestEqual(TEXT("two Body sprites resident"), BodySprites.Num(), 2);

	// --- Unchanged reimport: both sheets skip and ZERO sprite packages dirty — the
	// AttachNormalMapToSprites regression this gate exists to prevent. ---
	AseIncr_ClearAllDirtyUnder(OutputRoot);
	FAseIncrGateRun Unchanged;
	if (AseIncr_RunGateImport(*this, TEXT("unchanged"),
		MakeFile(20, 90), AseFile, OutputRoot, Prefix, LayerAsset, Unchanged))
	{
		TestEqual(TEXT("unchanged: base, normal, and composited sheets all skip"), Unchanged.Report.SheetsSkipped, 3);
		for (UPaperSprite* Sprite : BodySprites)
		{
			TestFalse(TEXT("unchanged: a normal-paired sprite package stays clean"),
				AseIncr_IsPackageDirty(Sprite));
		}
	}

	// --- Normal-only edit: `_Sheet_N` rewrites, the base sheet skips, and the attach's
	// compare-first pass still leaves every sprite package clean. ---
	AseIncr_ClearAllDirtyUnder(OutputRoot);
	FAseIncrGateRun NormalEdit;
	if (AseIncr_RunGateImport(*this, TEXT("normal-edit"),
		MakeFile(20, 123), AseFile, OutputRoot, Prefix, LayerAsset, NormalEdit))
	{
		TestEqual(TEXT("normal-edit: composited + normal sheets write"), NormalEdit.Report.SheetsWritten, 2);
		TestEqual(TEXT("normal-edit: the base colour sheet skips"), NormalEdit.Report.SheetsSkipped, 1);
		for (UPaperSprite* Sprite : BodySprites)
		{
			TestFalse(TEXT("normal-edit: a sprite package stays clean (attachment unchanged)"),
				AseIncr_IsPackageDirty(Sprite));
		}
	}

	AseIncr_DropAllUnder(*this, OutputRoot);
	IFileManager::Get().Delete(*AseFile);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusAseIncrementalRetimeTest,
	"Paper2DPlus.Editor.AsepriteIncremental.TagRetimeRewritesOnlyTheAffectedFlipbook",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusAseIncrementalRetimeTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusAsepriteIncrementalTest;

	const FString Token = AseIncr_NewToken();
	const FString OutputRoot = FString::Printf(TEXT("/Game/__AutomationTemp__/AseIncrU7_%s"), *Token);
	const FString Prefix = TEXT("U7Hero");
	const FString AseFile = FPaths::ConvertRelativePathToFull(
		FPaths::ProjectSavedDir() / TEXT("Automation") / FString::Printf(TEXT("AseIncrU7_%s.ase"), *Token));

	// One layer, four frames of STATIC pixels, two tags: Idle(0-1) and Run(2-3). The only thing
	// that changes between passes is frame 3's duration — the exact R2 scenario.
	const auto MakeFile = [](const uint16 Frame3DurationMs)
	{
		TArray<TArray<uint8>> Frames;
		for (int32 F = 0; F < 4; ++F)
		{
			TArray<TArray<uint8>> Chunks;
			if (F == 0)
			{
				Chunks.Add(AseIncr_LayerChunk(TEXT("Art")));
				Chunks.Add(AseIncr_TagsChunk({
					MakeTuple(FString(TEXT("Idle")), static_cast<uint16>(0), static_cast<uint16>(1)),
					MakeTuple(FString(TEXT("Run")), static_cast<uint16>(2), static_cast<uint16>(3)) }));
			}
			Chunks.Add(AseIncr_PatternCelChunk(0, 6, 6, static_cast<uint8>(30 + F)));
			Frames.Add(AseIncr_Frame(Chunks, F == 3 ? Frame3DurationMs : static_cast<uint16>(100)));
		}
		return AseIncr_File(6, 6, Frames);
	};

	FAseIncrScopedWatcherShutdown WatcherWindow(*this);

	FAseIncrGateRun Fresh;
	if (!AseIncr_RunGateImport(*this, TEXT("fresh"),
		MakeFile(100), AseFile, OutputRoot, Prefix, nullptr, Fresh))
	{
		AseIncr_DropAllUnder(*this, OutputRoot);
		IFileManager::Get().Delete(*AseFile);
		return false;
	}
	TestEqual(TEXT("fresh: both tag flipbooks written"), Fresh.Report.FlipbooksWritten, 2);

	UPaper2DPlusCharacterProfileAsset* Profile = Fresh.LayerAsset->BaseProfile.Get();
	UPaperFlipbook* IdleFlipbook = nullptr;
	UPaperFlipbook* RunFlipbook = nullptr;
	if (TestNotNull(TEXT("the base Character Profile is resident"), Profile))
	{
		for (const FFlipbookProfileEntry& Entry : Profile->Flipbooks)
		{
			if (Entry.Identity.FlipbookName.Equals(TEXT("Idle"), ESearchCase::IgnoreCase))
			{
				IdleFlipbook = Entry.Identity.Flipbook.Get();
			}
			else if (Entry.Identity.FlipbookName.Equals(TEXT("Run"), ESearchCase::IgnoreCase))
			{
				RunFlipbook = Entry.Identity.Flipbook.Get();
			}
		}
	}
	if (!TestNotNull(TEXT("Idle flipbook resident"), IdleFlipbook)
		|| !TestNotNull(TEXT("Run flipbook resident"), RunFlipbook))
	{
		AseIncr_DropAllUnder(*this, OutputRoot);
		IFileManager::Get().Delete(*AseFile);
		return false;
	}

	// --- Retime frame 3 (Run's second key) from 100 ms to 200 ms. Pixels are untouched. ---
	AseIncr_ClearAllDirtyUnder(OutputRoot);
	FAseIncrGateRun Retimed;
	if (AseIncr_RunGateImport(*this, TEXT("retime"),
		MakeFile(200), AseFile, OutputRoot, Prefix, Fresh.LayerAsset, Retimed))
	{
		TestEqual(TEXT("retime: exactly the affected flipbook rewrites"), Retimed.Report.FlipbooksWritten, 1);
		TestEqual(TEXT("retime: the untouched tag's flipbook skips"), Retimed.Report.FlipbooksSkipped, 1);
		TestEqual(TEXT("retime: no sheet writes — pixels did not change (composited included, U6)"),
			Retimed.Report.SheetsWritten, 0);
		TestEqual(TEXT("retime: both sheets skip"), Retimed.Report.SheetsSkipped, 2);
		TestEqual(TEXT("retime: zero sprite writes anywhere"), Retimed.Report.SpritesWritten, 0);
		TestEqual(TEXT("retime: every sprite skips (4 per-layer + 4 flat)"), Retimed.Report.SpritesSkipped, 8);
		TestFalse(TEXT("retime: the Idle flipbook package stays clean"), AseIncr_IsPackageDirty(IdleFlipbook));
		TestTrue(TEXT("retime: the Run flipbook package is dirtied"), AseIncr_IsPackageDirty(RunFlipbook));
		TestEqual(TEXT("retime: no profile entry rewrites (same flipbook identity)"),
			Retimed.Report.ProfileEntriesWritten, 0);
		TestFalse(TEXT("retime: the Character Profile package stays clean"), AseIncr_IsPackageDirty(Profile));

		// The rewritten flipbook carries the retime: durations {100, 200} under GcdExact are
		// FPS 10 with FrameRuns {1, 2} — hard-coded expectations, never computed by the code
		// under test.
		TestEqual(TEXT("retime: Run keeps two keyframes"), RunFlipbook->GetNumKeyFrames(), 2);
		if (RunFlipbook->GetNumKeyFrames() == 2)
		{
			TestEqual(TEXT("retime: Run's first key holds one frame run"),
				RunFlipbook->GetKeyFrameChecked(0).FrameRun, 1);
			TestEqual(TEXT("retime: Run's second key holds the doubled frame run"),
				RunFlipbook->GetKeyFrameChecked(1).FrameRun, 2);
		}
		TestTrue(TEXT("retime: Run's FPS reflects the exact-duration base"),
			FMath::IsNearlyEqual(RunFlipbook->GetFramesPerSecond(), 10.0f, 0.01f));
	}

	AseIncr_DropAllUnder(*this, OutputRoot);
	IFileManager::Get().Delete(*AseFile);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusAseIncrementalForceBypassesSettingTest,
	"Paper2DPlus.Editor.AsepriteIncremental.ForceFullReimportRunsWithLiveReimportDisabled",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusAseIncrementalForceBypassesSettingTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusAsepriteIncrementalTest;

	const FString Token = AseIncr_NewToken();
	const FString OutputRoot = FString::Printf(TEXT("/Game/__AutomationTemp__/AseIncrU8_%s"), *Token);
	const FString Prefix = TEXT("U8Hero");
	const FString AseFile = FPaths::ConvertRelativePathToFull(
		FPaths::ProjectSavedDir() / TEXT("Automation") / FString::Printf(TEXT("AseIncrU8_%s.ase"), *Token));

	FAseIncrScopedWatcherShutdown WatcherWindow(*this);

	const FIntRect Art(1, 1, 4, 4);
	FAseIncrGateRun Fresh;
	if (!AseIncr_RunGateImport(*this, TEXT("fresh"),
		AseIncr_GateFixtureFile(2, Art, FColor::Red), AseFile, OutputRoot, Prefix, nullptr, Fresh))
	{
		AseIncr_DropAllUnder(*this, OutputRoot);
		IFileManager::Get().Delete(*AseFile);
		return false;
	}

	// The manual recovery path must run while the automatic reaction is switched OFF: the setting
	// governs the watcher, not explicit user intent. Restored silently (no broadcast) on exit.
	AseIncr_ClearAllDirtyUnder(OutputRoot);
	UPaper2DPlusSettings* MutableSettings = GetMutableDefault<UPaper2DPlusSettings>();
	const bool bOriginalSetting = MutableSettings->bEnableAseLiveReimport;
	MutableSettings->bEnableAseLiveReimport = false;

	FAsepriteImportCostReport ForcedReport;
	const bool bForcedOk = Fresh.LayerAsset->ImportedAseSources.Num() > 0
		&& FAsepriteImporter::ForceReimportLayerAssetSource(
			*Fresh.LayerAsset, Fresh.LayerAsset->ImportedAseSources[0],
			/*bForceFullReimport*/ true, &ForcedReport);

	MutableSettings->bEnableAseLiveReimport = bOriginalSetting;

	TestTrue(TEXT("Force Full Reimport succeeds while Live .ase Auto-Reimport is disabled"), bForcedOk);
	if (bForcedOk)
	{
		TestEqual(TEXT("forced: every sheet writes (composited + two layers)"), ForcedReport.SheetsWritten, 3);
		TestEqual(TEXT("forced: every sprite writes (2 flat + 4 per-layer)"), ForcedReport.SpritesWritten, 6);
		TestEqual(TEXT("forced: nothing skips"),
			ForcedReport.SheetsSkipped + ForcedReport.SpritesSkipped + ForcedReport.FlipbooksSkipped, 0);
	}

	AseIncr_DropAllUnder(*this, OutputRoot);
	IFileManager::Get().Delete(*AseFile);
	return !HasAnyErrors();
}

// ============================================================
// (perf lane) Synthetic replica of the measured six-layer file
// ============================================================
//
// NOT part of the ordinary suite: the Paper2DPlusPerf namespace sits outside the boundary-dot
// StartsWith:Paper2DPlus selector — the same mechanism that keeps Paper2DPlusRender and
// Paper2DPlusProfile200 opt-in. Run it by name when a machine-local number is needed:
//   Automation RunTests Paper2DPlusPerf.AsepriteIncremental
//
// The shape mirrors NCBJ BT01_LM_Base.aseprite: 6 layers x 64 frames x 150x150 -> six 1200x1200
// per-layer sheets (8x8 grid) plus the composited sheet, 384 per-layer + 64 flat sprites (the
// real file's 448), and 12 tag flipbooks. Since the U9 writer unification the whole pipeline is
// /Temp-guarded, so this runs the FULL import — profile branch included — with a real on-disk
// source file, which is what arms the U3 stamps. Leg 1 (fresh) has no stamps and writes
// everything: the pre-TASK-192 cost. Leg 2 (covered reimport, byte-identical file) is the
// shipped contract: every gate skips and the wall collapses to parse+composite. Timings land in
// the log's cost lines and this test's info events; assertions pin the deterministic write/skip
// counts. The skip pins are the perf twin of LayeredReimportSkipsUnchangedAndNarrowsSprites —
// same gate helpers, same counters — whose gate mutations were proven red on 2026-08-26, so a
// separate mutation pass here would re-prove the identical code path.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusAseIncrementalPerfReplicaTest,
	"Paper2DPlusPerf.AsepriteIncremental.SyntheticSixLayerImportAndReimportCost",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusAseIncrementalPerfReplicaTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusAsepriteIncrementalTest;

	const int32 RunId = AseIncr_RunCounter++;
	const FString OutputPath = FString::Printf(TEXT("/Temp/AseIncrPerf_%d"), RunId);
	const FString AssetPrefix = FString::Printf(TEXT("AseIncrP%d"), RunId);
	const FString AseFile = FPaths::ConvertRelativePathToFull(
		FPaths::ProjectSavedDir() / TEXT("Automation") / FString::Printf(TEXT("AseIncrPerf_%d.ase"), RunId));

	// The import tail registers the source file's directory with the watcher; keep the perf run
	// hermetic the same way the gate-fixture tests do.
	FAseIncrScopedWatcherShutdown WatcherWindow(*this);

	const TArray<uint8> Buffer = AseIncr_LayeredFile(
		/*LayerCount*/ 6, /*FrameCount*/ 64, /*CanvasW*/ 150, /*CanvasH*/ 150, /*TagCount*/ 12);
	if (!TestTrue(TEXT("perf fixture .ase written to disk"), FFileHelper::SaveArrayToFile(Buffer, *AseFile)))
	{
		return false;
	}

	auto RunOnce = [&](const TCHAR* Label, UPaper2DPlusCharacterLayerAsset* ExistingAsset,
		FAsepriteImportCostReport& OutReport, double& OutWallSeconds,
		UPaper2DPlusCharacterLayerAsset*& OutAsset) -> bool
	{
		const double WallStart = FPlatformTime::Seconds();
		FAsepriteImportCostScope CostScope;

		FAsepriteParsedData Parsed;
		FString Error;
		bool bParsed = false;
		{
			FAsepriteImportCostPhaseTimer ParsePhase(EAsepriteImportCostPhase::Parse);
			bParsed = FAsepriteImporter::ParseBuffer(Buffer, Parsed, Error);
		}
		if (!TestTrue(FString::Printf(TEXT("%s: ParseBuffer succeeds (%s)"), Label, *Error), bParsed))
		{
			return false;
		}

		FPerLayerBufferMap PerLayerBuffers;
		{
			FAsepriteImportCostPhaseTimer CompositePhase(EAsepriteImportCostPhase::Composite);
			PerLayerBuffers = FAsepriteImporter::CompositePerLayer(Parsed);
		}
		if (!TestEqual(FString::Printf(TEXT("%s: six layers composited"), Label), PerLayerBuffers.Num(), 6))
		{
			return false;
		}

		FAsepriteLayerImportSettings Settings;
		FAsepriteImporter::InitDefaultSelection(Parsed, Settings);
		Settings.ImportMode = EAsepriteImportMode::LayerAssetNewProfile;
		Settings.OutputPath = OutputPath;
		Settings.AssetPrefix = AssetPrefix;
		Settings.bOrganizeIntoSubfolders = false;
		Settings.bUserConfirmed = true;
		Settings.bKeepSourceInProject = false;
		Settings.SourceFilePath = AseFile;
		if (ExistingAsset)
		{
			// The incremental gate context resolves from the reimport target's stamped
			// FAsepriteSourceContext — without it every gate fails closed to write.
			Settings.ExistingLayerAsset = ExistingAsset;
		}

		UObject* Result = FAsepriteImporter::ImportAsLayeredAsset(Parsed, PerLayerBuffers, Settings);
		OutReport = CostScope.GetReport();
		OutWallSeconds = FPlatformTime::Seconds() - WallStart;
		OutAsset = Cast<UPaper2DPlusCharacterLayerAsset>(Result);
		return TestNotNull(FString::Printf(TEXT("%s: the layered import produced an asset"), Label), OutAsset);
	};

	FAsepriteImportCostReport FreshReport;
	double FreshWall = 0.0;
	FAsepriteImportCostReport ReimportReport;
	double ReimportWall = 0.0;
	UPaper2DPlusCharacterLayerAsset* FreshAsset = nullptr;
	UPaper2DPlusCharacterLayerAsset* CoveredAsset = nullptr;
	const bool bLegsRan = RunOnce(TEXT("fresh"), nullptr, FreshReport, FreshWall, FreshAsset)
		&& RunOnce(TEXT("covered"), FreshAsset, ReimportReport, ReimportWall, CoveredAsset);
	IFileManager::Get().Delete(*AseFile);
	if (!bLegsRan)
	{
		return false;
	}

	TestEqual(TEXT("fresh: seven sheets written (six per-layer + composited)"), FreshReport.SheetsWritten, 7);
	TestEqual(TEXT("fresh: 448 sprites written (384 per-layer + 64 flat)"), FreshReport.SpritesWritten, 448);
	TestEqual(TEXT("fresh: twelve tag flipbooks written"), FreshReport.FlipbooksWritten, 12);
	TestEqual(TEXT("covered: no sheet writes"), ReimportReport.SheetsWritten, 0);
	TestEqual(TEXT("covered: all seven sheets skipped"), ReimportReport.SheetsSkipped, 7);
	TestEqual(TEXT("covered: no sprite writes"), ReimportReport.SpritesWritten, 0);
	TestEqual(TEXT("covered: no flipbook writes"), ReimportReport.FlipbooksWritten, 0);

	AddInfo(FString::Printf(TEXT("FRESH:   %s | wall %.3fs"), *FreshReport.ToSummaryString(), FreshWall));
	AddInfo(FString::Printf(TEXT("COVERED: %s | wall %.3fs"), *ReimportReport.ToSummaryString(), ReimportWall));
	if (ReimportWall > 0.0)
	{
		AddInfo(FString::Printf(TEXT("Covered reimport is %.0fx faster than fresh (%.3fs -> %.3fs)"),
			FreshWall / ReimportWall, FreshWall, ReimportWall));
	}

	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
