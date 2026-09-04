// Copyright 2026 Infinite Gameworks. All Rights Reserved.

/**
 * Worldless tests for the Aseprite import-selection work (2026-08-19):
 *   (1) linked ("hold") cels composite from their source frame — regression for the MoveTemp'd-local
 *       lookup that imported every linked cel as blank,
 *   (2) hitbox-layer classification is settings-driven (Aseprite Import → Hitbox Layer Name Prefixes),
 *       covers the generic "hitbox" prefix by default, falls back to compiled defaults when the
 *       settings list is emptied, and never shadows the fixed "socket_" convention,
 *   (3) FilterTagsByDisabledIndices — the pure seam behind the dialog's per-tag checkboxes,
 *   (4) FAsepriteImporter::InitDefaultSelection — the shared bulk-intake/watcher default
 *       (every visual layer + every tag enabled; groups and hitbox layers excluded).
 *
 * All tests build minimal in-memory .ase buffers and drive FAsepriteImporter::ParseBuffer directly —
 * no packages are created, so the suite is headless-safe.
 */

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

#include "AsepriteImporter.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Paper2DPlusSettings.h"
#include "PaperFlipbook.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace Paper2DPlusAsepriteImportSelectionTest
{
	/** Little-endian byte writer for crafting minimal .ase buffers (file-unique: AseImport_). */
	struct FAseImportBufWriter
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

	/** Wrap a chunk payload with its 6-byte header (u32 size incl. header, u16 type). */
	static TArray<uint8> AseImport_Chunk(uint16 ChunkType, const TArray<uint8>& Payload)
	{
		FAseImportBufWriter W;
		W.U32(static_cast<uint32>(Payload.Num() + 6));
		W.U16(ChunkType);
		W.Append(Payload);
		return W.Bytes;
	}

	/** Normal (non-group) visible layer chunk. */
	static TArray<uint8> AseImport_LayerChunk(const FString& Name, uint16 LayerType = 0, uint16 ChildLevel = 0, bool bVisible = true)
	{
		FAseImportBufWriter P;
		P.U16(bVisible ? 1 : 0); // flags (bit 0 = visible)
		P.U16(LayerType);        // 0 = normal, 1 = group
		P.U16(ChildLevel);
		P.U16(0); P.U16(0);      // default width/height (ignored)
		P.U16(0);                // blend mode
		P.U8(255);               // opacity
		P.Zeros(3);              // reserved
		P.Str(Name);
		return AseImport_Chunk(0x2004, P.Bytes);
	}

	/** Raw (uncompressed) RGBA cel chunk. */
	static TArray<uint8> AseImport_RawCelChunk(uint16 LayerIndex, int16 X, int16 Y, uint16 W, uint16 H, const FColor& Fill)
	{
		FAseImportBufWriter P;
		P.U16(LayerIndex);
		P.S16(X); P.S16(Y);
		P.U8(255);   // cel opacity
		P.U16(0);    // ASE_CEL_RAW
		P.Zeros(7);  // z-index + reserved
		P.U16(W); P.U16(H);
		for (int32 i = 0; i < W * H; ++i)
		{
			P.U8(Fill.R); P.U8(Fill.G); P.U8(Fill.B); P.U8(Fill.A);
		}
		return AseImport_Chunk(0x2005, P.Bytes);
	}

	/** Linked cel chunk (reuses another frame's pixels on the same layer). */
	static TArray<uint8> AseImport_LinkedCelChunk(uint16 LayerIndex, int16 X, int16 Y, uint16 LinkedFrame)
	{
		FAseImportBufWriter P;
		P.U16(LayerIndex);
		P.S16(X); P.S16(Y);
		P.U8(255);
		P.U16(1);    // ASE_CEL_LINKED
		P.Zeros(7);
		P.U16(LinkedFrame);
		return AseImport_Chunk(0x2005, P.Bytes);
	}

	/** Tags chunk from (name, from, to) triples, forward loop direction. */
	static TArray<uint8> AseImport_TagsChunk(const TArray<TTuple<FString, uint16, uint16>>& Tags)
	{
		FAseImportBufWriter P;
		P.U16(static_cast<uint16>(Tags.Num()));
		P.Zeros(8);
		for (const auto& Tag : Tags)
		{
			P.U16(Tag.Get<1>()); // from
			P.U16(Tag.Get<2>()); // to
			P.U8(0);             // loop direction: forward
			P.Zeros(8);          // repeat + reserved
			P.Zeros(3);          // deprecated color
			P.Zeros(1);          // extra
			P.Str(Tag.Get<0>());
		}
		return AseImport_Chunk(0x2018, P.Bytes);
	}

	/** One frame from pre-built chunks (u32 size incl. the 16-byte frame header). */
	static TArray<uint8> AseImport_Frame(const TArray<TArray<uint8>>& Chunks)
	{
		int32 ChunkBytes = 0;
		for (const TArray<uint8>& C : Chunks) { ChunkBytes += C.Num(); }

		FAseImportBufWriter W;
		W.U32(static_cast<uint32>(16 + ChunkBytes));
		W.U16(0xF1FA);                                  // frame magic
		W.U16(static_cast<uint16>(Chunks.Num()));       // old chunk count
		W.U16(100);                                     // duration ms
		W.Zeros(2);                                     // reserved
		W.U32(static_cast<uint32>(Chunks.Num()));       // new chunk count
		for (const TArray<uint8>& C : Chunks) { W.Append(C); }
		return W.Bytes;
	}

	/** Whole file: 128-byte header + frames. 32-bit RGBA canvas. */
	static TArray<uint8> AseImport_File(uint16 CanvasW, uint16 CanvasH, const TArray<TArray<uint8>>& Frames)
	{
		FAseImportBufWriter W;
		int32 FrameBytes = 0;
		for (const TArray<uint8>& F : Frames) { FrameBytes += F.Num(); }

		W.U32(static_cast<uint32>(128 + FrameBytes)); // file size
		W.U16(0xA5E0);                                // file magic
		W.U16(static_cast<uint16>(Frames.Num()));
		W.U16(CanvasW);
		W.U16(CanvasH);
		W.U16(32);                                    // color depth
		W.U32(0);                                     // flags
		W.U16(100);                                   // deprecated speed
		W.Zeros(128 - W.Bytes.Num());                 // pad header to 128
		for (const TArray<uint8>& F : Frames) { W.Append(F); }
		return W.Bytes;
	}
}

// ============================================================
// (1) Linked cels composite from their source frame
// ============================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusAseImportLinkedCelTest,
	"Paper2DPlus.AsepriteImport.LinkedCelCompositesFromSourceFrame",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusAseImportLinkedCelTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusAsepriteImportSelectionTest;

	// 4x4 canvas, one layer. Frame 0 paints a 2x2 red cel at (1,1); frame 1 LINKS to frame 0
	// (the artist's "hold this frame"). Pre-fix, frame 1 composited blank because the linked-cel
	// lookup read the MoveTemp'd-from local cel array.
	const FColor Red(255, 0, 0, 255);
	TArray<TArray<uint8>> Frame0Chunks;
	Frame0Chunks.Add(AseImport_LayerChunk(TEXT("Body")));
	Frame0Chunks.Add(AseImport_RawCelChunk(0, 1, 1, 2, 2, Red));
	TArray<TArray<uint8>> Frame1Chunks;
	Frame1Chunks.Add(AseImport_LinkedCelChunk(0, 1, 1, 0));

	const TArray<uint8> Buffer = AseImport_File(4, 4,
		{ AseImport_Frame(Frame0Chunks), AseImport_Frame(Frame1Chunks) });

	FAsepriteParsedData Parsed;
	FString Error;
	if (!TestTrue(FString::Printf(TEXT("ParseBuffer succeeds (%s)"), *Error),
		FAsepriteImporter::ParseBuffer(Buffer, Parsed, Error)))
	{
		return false;
	}

	TestEqual(TEXT("Two frames parsed"), Parsed.Frames.Num(), 2);
	if (Parsed.Frames.Num() < 2) { return false; }

	const int32 CelPixelIndex = 1 * 4 + 1; // (x=1, y=1) on the 4x4 canvas
	TestTrue(TEXT("Frame 0 pixel index in range"), Parsed.Frames[0].Pixels.IsValidIndex(CelPixelIndex));
	TestTrue(TEXT("Frame 1 pixel index in range"), Parsed.Frames[1].Pixels.IsValidIndex(CelPixelIndex));
	if (!Parsed.Frames[0].Pixels.IsValidIndex(CelPixelIndex) ||
		!Parsed.Frames[1].Pixels.IsValidIndex(CelPixelIndex))
	{
		return false;
	}

	// Sanity: the raw source frame carries the red cel
	TestEqual(TEXT("Frame 0 composited the raw cel (R)"), Parsed.Frames[0].Pixels[CelPixelIndex].R, static_cast<uint8>(255));
	TestEqual(TEXT("Frame 0 composited the raw cel (A)"), Parsed.Frames[0].Pixels[CelPixelIndex].A, static_cast<uint8>(255));

	// Regression: the LINKED frame must carry the same content, not blank
	TestEqual(TEXT("Frame 1 composited the LINKED cel (R)"), Parsed.Frames[1].Pixels[CelPixelIndex].R, static_cast<uint8>(255));
	TestEqual(TEXT("Frame 1 composited the LINKED cel (A)"), Parsed.Frames[1].Pixels[CelPixelIndex].A, static_cast<uint8>(255));

	return !HasAnyErrors();
}

// ============================================================
// (2) Settings-driven hitbox-layer prefix classification
// ============================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusAseImportHitboxPrefixTest,
	"Paper2DPlus.AsepriteImport.HitboxLayerPrefixClassification",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusAseImportHitboxPrefixTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusAsepriteImportSelectionTest;

	TArray<TArray<uint8>> FrameChunks;
	FrameChunks.Add(AseImport_LayerChunk(TEXT("attackA")));
	FrameChunks.Add(AseImport_LayerChunk(TEXT("HurtBox Zone")));
	FrameChunks.Add(AseImport_LayerChunk(TEXT("HITBOX")));
	FrameChunks.Add(AseImport_LayerChunk(TEXT("socket_Hand")));
	FrameChunks.Add(AseImport_LayerChunk(TEXT("Body")));
	const TArray<uint8> Buffer = AseImport_File(4, 4, { AseImport_Frame(FrameChunks) });

	auto Classify = [&](FAsepriteParsedData& OutParsed) -> bool
	{
		FString Error;
		return FAsepriteImporter::ParseBuffer(Buffer, OutParsed, Error);
	};

	auto FindHitbox = [](const FAsepriteParsedData& Parsed, const FString& LayerName) -> const FAsepriteHitboxLayer*
	{
		for (const FAsepriteHitboxLayer& HL : Parsed.HitboxLayers)
		{
			if (HL.LayerName == LayerName) { return &HL; }
		}
		return nullptr;
	};

	// --- Default settings: attack / hurtbox / hitbox prefixes + fixed socket_ ---
	{
		FAsepriteParsedData Parsed;
		if (!Classify(Parsed))
		{
			AddError(TEXT("Parse with default settings failed"));
			return false;
		}

		TestEqual(TEXT("Four data layers classified"), Parsed.HitboxLayers.Num(), 4);

		const FAsepriteHitboxLayer* Attack = FindHitbox(Parsed, TEXT("attackA"));
		TestTrue(TEXT("attackA classified"), Attack != nullptr);
		if (Attack) { TestEqual(TEXT("attackA is Attack"), Attack->HitboxType, EHitboxType::Attack); }

		const FAsepriteHitboxLayer* Hurt = FindHitbox(Parsed, TEXT("HurtBox Zone"));
		TestTrue(TEXT("HurtBox Zone classified (case-insensitive)"), Hurt != nullptr);
		if (Hurt) { TestEqual(TEXT("HurtBox Zone is Hurtbox"), Hurt->HitboxType, EHitboxType::Hurtbox); }

		const FAsepriteHitboxLayer* Generic = FindHitbox(Parsed, TEXT("HITBOX"));
		TestTrue(TEXT("HITBOX classified via the default 'hitbox' prefix"), Generic != nullptr);
		if (Generic) { TestEqual(TEXT("HITBOX imports as Attack by default"), Generic->HitboxType, EHitboxType::Attack); }

		const FAsepriteHitboxLayer* Socket = FindHitbox(Parsed, TEXT("socket_Hand"));
		TestTrue(TEXT("socket_Hand classified"), Socket != nullptr);
		if (Socket)
		{
			TestTrue(TEXT("socket_Hand is a socket"), Socket->bIsSocket);
			TestEqual(TEXT("Socket name stripped"), Socket->SocketName, FString(TEXT("Hand")));
		}

		TestTrue(TEXT("Body stays a visual layer"), FindHitbox(Parsed, TEXT("Body")) == nullptr);
	}

	// --- Custom settings replace the defaults; empty settings fall back to them ---
	UPaper2DPlusSettings* Settings = GetMutableDefault<UPaper2DPlusSettings>();
	const TArray<FPaper2DPlusHitboxLayerPrefix> SavedPrefixes = Settings->HitboxLayerNamePrefixes;

	{
		// Custom list: only "hurtbox" recognized → attackA/HITBOX stay visual
		Settings->HitboxLayerNamePrefixes = { FPaper2DPlusHitboxLayerPrefix(TEXT("hurtbox"), EHitboxType::Hurtbox) };
		FAsepriteParsedData Parsed;
		if (Classify(Parsed))
		{
			TestTrue(TEXT("Custom list: attackA NOT classified"), FindHitbox(Parsed, TEXT("attackA")) == nullptr);
			TestTrue(TEXT("Custom list: HITBOX NOT classified"), FindHitbox(Parsed, TEXT("HITBOX")) == nullptr);
			TestTrue(TEXT("Custom list: HurtBox Zone classified"), FindHitbox(Parsed, TEXT("HurtBox Zone")) != nullptr);
			TestTrue(TEXT("Custom list: socket_ convention still active"), FindHitbox(Parsed, TEXT("socket_Hand")) != nullptr);
		}
		else
		{
			AddError(TEXT("Parse with custom settings failed"));
		}
	}

	{
		// Emptied list: falls back to the compiled defaults — data layers can never silently become art
		Settings->HitboxLayerNamePrefixes.Empty();
		FAsepriteParsedData Parsed;
		if (Classify(Parsed))
		{
			TestEqual(TEXT("Empty settings fall back to defaults (4 data layers)"), Parsed.HitboxLayers.Num(), 4);
			TestTrue(TEXT("Empty settings: HITBOX classified via fallback"), FindHitbox(Parsed, TEXT("HITBOX")) != nullptr);
		}
		else
		{
			AddError(TEXT("Parse with emptied settings failed"));
		}
	}

	Settings->HitboxLayerNamePrefixes = SavedPrefixes;
	return !HasAnyErrors();
}

// ============================================================
// (3) FilterTagsByDisabledIndices — the pure per-tag checkbox seam
// ============================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusAseImportTagFilterTest,
	"Paper2DPlus.AsepriteImport.FilterTagsByDisabledIndices",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusAseImportTagFilterTest::RunTest(const FString& Parameters)
{
	auto MakeTags = []() -> TArray<FAsepriteTag>
	{
		TArray<FAsepriteTag> Tags;
		for (const TCHAR* Name : { TEXT("Idle"), TEXT("Walk"), TEXT("Run") })
		{
			FAsepriteTag Tag;
			Tag.Name = Name;
			Tags.Add(Tag);
		}
		return Tags;
	};

	// Disable first and last (original indices) → only the middle survives
	{
		TArray<FAsepriteTag> Tags = MakeTags();
		FAsepriteImporter::FilterTagsByDisabledIndices(Tags, { 0, 2 });
		TestEqual(TEXT("One tag survives"), Tags.Num(), 1);
		if (Tags.Num() == 1) { TestEqual(TEXT("Surviving tag is Walk"), Tags[0].Name, FString(TEXT("Walk"))); }
	}

	// Empty set = no-op
	{
		TArray<FAsepriteTag> Tags = MakeTags();
		FAsepriteImporter::FilterTagsByDisabledIndices(Tags, {});
		TestEqual(TEXT("Empty set keeps all tags"), Tags.Num(), 3);
	}

	// All disabled = empty result (the caller falls into the no-tags "_All" path)
	{
		TArray<FAsepriteTag> Tags = MakeTags();
		FAsepriteImporter::FilterTagsByDisabledIndices(Tags, { 0, 1, 2 });
		TestEqual(TEXT("All disabled empties the array"), Tags.Num(), 0);
	}

	// Out-of-range indices are ignored
	{
		TArray<FAsepriteTag> Tags = MakeTags();
		FAsepriteImporter::FilterTagsByDisabledIndices(Tags, { 5, -1 });
		TestEqual(TEXT("Out-of-range indices ignored"), Tags.Num(), 3);
	}

	return !HasAnyErrors();
}

// ============================================================
// (4) InitDefaultSelection — the shared dialog/factory-batch default
// ============================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusAseImportDefaultSelectionTest,
	"Paper2DPlus.AsepriteImport.DialogDefaultSelection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusAseImportDefaultSelectionTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusAsepriteImportSelectionTest;

	// Layers: group "G" (0), visual "Body" under it (1), data layer "HITBOX" (2). Two tags.
	TArray<TArray<uint8>> FrameChunks;
	FrameChunks.Add(AseImport_LayerChunk(TEXT("G"), /*LayerType*/ 1));
	FrameChunks.Add(AseImport_LayerChunk(TEXT("Body"), /*LayerType*/ 0, /*ChildLevel*/ 1));
	FrameChunks.Add(AseImport_LayerChunk(TEXT("HITBOX")));
	FrameChunks.Add(AseImport_TagsChunk({ MakeTuple(FString(TEXT("Idle")), (uint16)0, (uint16)0),
	                                      MakeTuple(FString(TEXT("Walk")), (uint16)0, (uint16)0) }));
	const TArray<uint8> Buffer = AseImport_File(4, 4, { AseImport_Frame(FrameChunks) });

	FAsepriteParsedData Parsed;
	FString Error;
	if (!TestTrue(FString::Printf(TEXT("ParseBuffer succeeds (%s)"), *Error),
		FAsepriteImporter::ParseBuffer(Buffer, Parsed, Error)))
	{
		return false;
	}

	FAsepriteLayerImportSettings Defaults;
	FAsepriteImporter::InitDefaultSelection(Parsed, Defaults);

	// Only the visual layer is enabled; the group and the data layer are excluded entirely
	TestEqual(TEXT("One visual layer enabled"), Defaults.LayerImportEnabled.Num(), 1);
	const bool* BodyEnabled = Defaults.LayerImportEnabled.Find(1);
	TestTrue(TEXT("Body (index 1) enabled"), BodyEnabled && *BodyEnabled);
	TestTrue(TEXT("Group not in selection"), Defaults.LayerImportEnabled.Find(0) == nullptr);
	TestTrue(TEXT("HITBOX not in selection"), Defaults.LayerImportEnabled.Find(2) == nullptr);

	// Every tag enabled
	TestEqual(TEXT("Two tags seeded"), Defaults.TagImportEnabled.Num(), 2);
	for (int32 TagIdx = 0; TagIdx < 2; ++TagIdx)
	{
		const bool* bEnabled = Defaults.TagImportEnabled.Find(TagIdx);
		TestTrue(FString::Printf(TEXT("Tag %d enabled by default"), TagIdx), bEnabled && *bEnabled);
	}

	// Batch flag defaults off
	TestFalse(TEXT("Apply-to-remaining defaults off"), Defaults.bApplyToRemainingFiles);

	// Keep-source-in-project defaults ON (the supported team workflow)
	TestTrue(TEXT("Keep source in project defaults on"), Defaults.bKeepSourceInProject);

	// Organized subfolder layout defaults ON (Flipbooks/Sheets/Sprites, not one flat folder)
	TestTrue(TEXT("Organize into subfolders defaults on"), Defaults.bOrganizeIntoSubfolders);

	return !HasAnyErrors();
}

// ============================================================
// (6) AppendProfileEntriesFromImportResult — additive population (TASK-184)
// ============================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusAseImportAppendEntriesTest,
	"Paper2DPlus.AsepriteImport.AppendProfileEntriesAdditive",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusAseImportAppendEntriesTest::RunTest(const FString& Parameters)
{
	// Unique transient package per run so exact flipbook names never collide across invocations
	static int32 AseAppend_RunCounter = 0;
	const FString PkgName = FString::Printf(TEXT("/Temp/AseAppendEntriesTest_%d"), AseAppend_RunCounter++);
	UPackage* Pkg = CreatePackage(*PkgName);
	if (!TestNotNull(TEXT("Transient test package"), Pkg)) { return false; }

	// Existing profile: one authored entry "Idle" carrying combat data that must survive the refresh
	UPaper2DPlusCharacterProfileAsset* Profile =
		NewObject<UPaper2DPlusCharacterProfileAsset>(Pkg, TEXT("AseAppend_Profile"), RF_Transient);
	{
		FFlipbookProfileEntry Existing;
		Existing.Identity.FlipbookName = TEXT("Idle");
		Existing.CombatData.Frames.SetNum(3); // marker: authored per-frame data
		Profile->Flipbooks.Add(MoveTemp(Existing));
	}

	// Import result: "Hero_Idle" (matches the existing entry after prefix-strip) + "Hero_Walk" (new)
	UPaperFlipbook* IdleFlipbook = NewObject<UPaperFlipbook>(Pkg, TEXT("Hero_Idle"), RF_Transient);
	UPaperFlipbook* WalkFlipbook = NewObject<UPaperFlipbook>(Pkg, TEXT("Hero_Walk"), RF_Transient);
	FAsepriteImportResult ImportResult;
	ImportResult.bSuccess = true;
	ImportResult.Flipbooks = { IdleFlipbook, WalkFlipbook };

	int32 Added = 0;
	int32 Refreshed = 0;
	FAsepriteImporter::AppendProfileEntriesFromImportResult(Profile, ImportResult, TEXT("Hero"), &Added, &Refreshed);

	TestEqual(TEXT("One entry appended"), Added, 1);
	TestEqual(TEXT("One entry refreshed"), Refreshed, 1);
	TestEqual(TEXT("Profile has two entries"), Profile->Flipbooks.Num(), 2);

	const FFlipbookProfileEntry* IdleEntry = Profile->Flipbooks.FindByPredicate(
		[](const FFlipbookProfileEntry& E) { return E.Identity.FlipbookName == TEXT("Idle"); });
	const FFlipbookProfileEntry* WalkEntry = Profile->Flipbooks.FindByPredicate(
		[](const FFlipbookProfileEntry& E) { return E.Identity.FlipbookName == TEXT("Walk"); });

	TestNotNull(TEXT("Idle entry present"), IdleEntry);
	TestNotNull(TEXT("Walk entry appended"), WalkEntry);
	if (IdleEntry)
	{
		TestTrue(TEXT("Idle entry re-pointed at the imported flipbook"), IdleEntry->Identity.Flipbook.Get() == IdleFlipbook);
		TestEqual(TEXT("Idle entry's authored combat frames survive the refresh"), IdleEntry->CombatData.Frames.Num(), 3);
	}
	if (WalkEntry)
	{
		TestTrue(TEXT("Walk entry points at the imported flipbook"), WalkEntry->Identity.Flipbook.Get() == WalkFlipbook);
	}

	// Re-running the same delivery is idempotent — and under the incremental append (TASK-192 U7)
	// idempotent means UNTOUCHED: rows that already match are skipped rather than falsely counted
	// as refreshed, which is what lets an all-skip reimport leave the profile package clean.
	int32 Skipped = 0;
	FAsepriteImporter::AppendProfileEntriesFromImportResult(Profile, ImportResult, TEXT("Hero"), &Added, &Refreshed, &Skipped);
	TestEqual(TEXT("Second pass appends nothing"), Added, 0);
	TestEqual(TEXT("Second pass refreshes nothing — the rows already match"), Refreshed, 0);
	TestEqual(TEXT("Second pass skips both untouched rows"), Skipped, 2);
	TestEqual(TEXT("Still two entries"), Profile->Flipbooks.Num(), 2);

	return !HasAnyErrors();
}

// ============================================================
// (5) Stored source-path round trip + content hash (TASK-183)
// ============================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusAseImportStoredPathTest,
	"Paper2DPlus.AsepriteImport.StoredSourcePathRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusAseImportStoredPathTest::RunTest(const FString& Parameters)
{
	// In-project file → stored PROJECT-RELATIVE, resolves back to the same absolute path
	{
		FString InProject = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Content/MainChar/Hero.aseprite"));
		FPaths::NormalizeFilename(InProject);

		const FString Stored = FAsepriteImporter::MakeStoredAsePath(InProject);
		TestTrue(TEXT("In-project path stores relative"), FPaths::IsRelative(Stored));
		TestFalse(TEXT("Stored form drops the project prefix"), Stored.Contains(TEXT(":")));

		const FString Resolved = FAsepriteImporter::ResolveStoredAsePath(Stored);
		TestEqual(TEXT("Round trip resolves to the original absolute path"), Resolved, InProject);
	}

	// Out-of-project file → stored absolute, resolves unchanged
	{
		FString External = TEXT("C:/SomewhereElse/Art/Hero.aseprite");
		FPaths::NormalizeFilename(External);

		const FString Stored = FAsepriteImporter::MakeStoredAsePath(External);
		TestFalse(TEXT("External path stores absolute"), FPaths::IsRelative(Stored));
		TestEqual(TEXT("External round trip is identity"), FAsepriteImporter::ResolveStoredAsePath(Stored), External);
	}

	// Legacy absolute stored value (pre-TASK-183 assets) resolves as-is
	{
		FString Legacy = TEXT("D:/OldMachine/Hero.ase");
		FPaths::NormalizeFilename(Legacy);
		TestEqual(TEXT("Legacy absolute stored path resolves as-is"), FAsepriteImporter::ResolveStoredAsePath(Legacy), Legacy);
	}

	// Empty in → empty out (never resolves to the cwd)
	TestTrue(TEXT("Empty stored path stays empty"), FAsepriteImporter::MakeStoredAsePath(FString()).IsEmpty());
	TestTrue(TEXT("Empty resolve stays empty"), FAsepriteImporter::ResolveStoredAsePath(FString()).IsEmpty());

	// Content hash: stable for same content, different for different content, empty for a missing file
	{
		const FString Dir = FPaths::ProjectIntermediateDir() / TEXT("AseImportHashTest");
		IFileManager::Get().MakeDirectory(*Dir, /*Tree*/ true);
		const FString FileA = Dir / TEXT("a.bin");
		const FString FileB = Dir / TEXT("b.bin");
		FFileHelper::SaveStringToFile(TEXT("alpha-content"), *FileA);
		FFileHelper::SaveStringToFile(TEXT("alpha-content"), *FileB);

		const FString HashA1 = FAsepriteImporter::HashAseFileContent(FileA);
		const FString HashA2 = FAsepriteImporter::HashAseFileContent(FileA);
		const FString HashB = FAsepriteImporter::HashAseFileContent(FileB);
		TestFalse(TEXT("Hash is non-empty for a readable file"), HashA1.IsEmpty());
		TestEqual(TEXT("Hash is deterministic"), HashA1, HashA2);
		TestEqual(TEXT("Identical content hashes identically"), HashA1, HashB);

		FFileHelper::SaveStringToFile(TEXT("beta-content"), *FileB);
		TestNotEqual(TEXT("Changed content changes the hash"), HashA1, FAsepriteImporter::HashAseFileContent(FileB));

		TestTrue(TEXT("Missing file hashes to empty"),
			FAsepriteImporter::HashAseFileContent(Dir / TEXT("missing.bin")).IsEmpty());

		IFileManager::Get().DeleteDirectory(*Dir, /*RequireExists*/ false, /*Tree*/ true);
	}

	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
