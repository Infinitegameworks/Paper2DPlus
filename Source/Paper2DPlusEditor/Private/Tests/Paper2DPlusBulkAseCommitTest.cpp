// Copyright 2026 Infinite Gameworks. All Rights Reserved.

// End-to-end coverage for the Bulk Sprite Extractor's `.ase` batch commit (TASK-189 U6).
//
// The row-SELECTION rule is pinned purely in Paper2DPlusBulkAseRowTest.cpp. What could not be
// pinned there is the loop BODY, because its load-bearing rule is about what the SECOND file does
// to the FIRST file's asset: the batch adopts the Layer Profile created by the first row INSIDE the
// loop, so every later row merges into it. Do that after the loop instead and every file mints its
// own `<prefix>_Layers` — the exact failure the whole batch model exists to fix, and one that no
// single-file test can see.
//
// So this drives the real window over real `.ase` files on disk and asserts the merged outcome.
//
// A deliberately self-contained `.ase` writer rather than sharing the incremental suite's larger
// one: that namespace lives in another file under active development, and a new file with its own
// minimal fixture has zero merge surface. File-unique prefix `BulkCommit_` — unity builds group
// these translation units, so a generic helper name would collide with a sibling test file.

#if WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "BulkSpriteExtractorWindow.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Paper2DPlusCharacterLayerAsset.h"
#include "Paper2DPlusCharacterProfileAsset.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace Paper2DPlusBulkAseCommitTest
{
	/** Little-endian byte writer for crafting minimal .ase buffers. */
	struct FBulkCommitWriter
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

	static TArray<uint8> BulkCommit_Chunk(uint16 ChunkType, const TArray<uint8>& Payload)
	{
		FBulkCommitWriter W;
		W.U32(static_cast<uint32>(Payload.Num() + 6));
		W.U16(ChunkType);
		W.Append(Payload);
		return W.Bytes;
	}

	static TArray<uint8> BulkCommit_LayerChunk(const FString& Name)
	{
		FBulkCommitWriter P;
		P.U16(1);   // visible
		P.U16(0);   // normal layer
		P.U16(0);   // child level
		P.U16(0); P.U16(0);
		P.U16(0);   // blend mode
		P.U8(255);  // opacity
		P.Zeros(3);
		P.Str(Name);
		return BulkCommit_Chunk(0x2004, P.Bytes);
	}

	static TArray<uint8> BulkCommit_RawCelChunk(uint16 LayerIndex, uint16 W, uint16 H, const FColor& Fill)
	{
		FBulkCommitWriter P;
		P.U16(LayerIndex);
		P.S16(0); P.S16(0);
		P.U8(255);
		P.U16(0);   // ASE_CEL_RAW
		P.Zeros(7);
		P.U16(W); P.U16(H);
		for (int32 i = 0; i < W * H; ++i)
		{
			P.U8(Fill.R); P.U8(Fill.G); P.U8(Fill.B); P.U8(Fill.A);
		}
		return BulkCommit_Chunk(0x2005, P.Bytes);
	}

	static TArray<uint8> BulkCommit_TagChunk(const FString& TagName, uint16 From, uint16 To)
	{
		FBulkCommitWriter P;
		P.U16(1);        // one tag
		P.Zeros(8);
		P.U16(From); P.U16(To);
		P.U8(0);         // forward
		P.Zeros(8);
		P.U8(0); P.U8(0); P.U8(0); P.U8(0);
		P.Str(TagName);
		return BulkCommit_Chunk(0x2018, P.Bytes);
	}

	static TArray<uint8> BulkCommit_Frame(const TArray<TArray<uint8>>& Chunks)
	{
		int32 ChunkBytes = 0;
		for (const TArray<uint8>& C : Chunks) { ChunkBytes += C.Num(); }

		FBulkCommitWriter W;
		W.U32(static_cast<uint32>(16 + ChunkBytes));
		W.U16(0xF1FA);
		W.U16(static_cast<uint16>(Chunks.Num()));
		W.U16(100);   // duration ms
		W.Zeros(2);
		W.U32(static_cast<uint32>(Chunks.Num()));
		for (const TArray<uint8>& C : Chunks) { W.Append(C); }
		return W.Bytes;
	}

	/** One 4x4 frame, one visual layer with the given NAME, one tag with the given NAME. The layer
	 *  name is what makes two files distinguishable inside one merged Layer Profile. */
	static TArray<uint8> BulkCommit_File(const FString& LayerName, const FString& TagName)
	{
		TArray<TArray<uint8>> Chunks;
		Chunks.Add(BulkCommit_LayerChunk(LayerName));
		Chunks.Add(BulkCommit_TagChunk(TagName, 0, 0));
		Chunks.Add(BulkCommit_RawCelChunk(0, 4, 4, FColor(200, 80, 40, 255)));

		FBulkCommitWriter W;
		const TArray<uint8> Frame = BulkCommit_Frame(Chunks);
		W.U32(static_cast<uint32>(128 + Frame.Num()));
		W.U16(0xA5E0);
		W.U16(1);    // frame count
		W.U16(4); W.U16(4);
		W.U16(32);   // color depth
		W.U32(0);
		W.U16(100);
		W.Zeros(128 - W.Bytes.Num());
		W.Append(Frame);
		return W.Bytes;
	}

	/** Writes a crafted `.ase` next to the automation outputs and returns its absolute path. */
	static FString BulkCommit_WriteAse(
		FAutomationTestBase& Test, const FString& FileStem, const FString& LayerName, const FString& TagName)
	{
		const FString Dir = FPaths::ConvertRelativePathToFull(
			FPaths::ProjectSavedDir() / TEXT("Automation"));
		IFileManager::Get().MakeDirectory(*Dir, /*Tree*/ true);
		const FString File = Dir / (FileStem + TEXT(".ase"));
		if (!Test.TestTrue(FString::Printf(TEXT("the crafted .ase reaches disk (%s)"), *FileStem),
			FFileHelper::SaveArrayToFile(BulkCommit_File(LayerName, TagName), *File)))
		{
			return FString();
		}
		return File;
	}

	static bool BulkCommit_HasLayerNamed(const UPaper2DPlusCharacterLayerAsset& Asset, const FString& LayerName)
	{
		return Asset.Layers.ContainsByPredicate([&LayerName](const FCharacterLayer& Layer)
		{
			return Layer.LayerName.Contains(LayerName);
		});
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusBulkAseCommitMergesBatchTest,
	"Paper2DPlus.BulkExtractor.AseCommit.EveryRowMergesIntoOneAdoptedLayerProfile",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusBulkAseCommitMergesBatchTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusBulkAseCommitTest;

	const FString Token = FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(8);
	const FString FileA = BulkCommit_WriteAse(*this, TEXT("BulkCommitA_") + Token, TEXT("Torso"), TEXT("Idle"));
	const FString FileB = BulkCommit_WriteAse(*this, TEXT("BulkCommitB_") + Token, TEXT("Cape"), TEXT("Walk"));
	if (FileA.IsEmpty() || FileB.IsEmpty())
	{
		return false;
	}
	ON_SCOPE_EXIT
	{
		IFileManager::Get().Delete(*FileA, /*RequireExists*/ false);
		IFileManager::Get().Delete(*FileB, /*RequireExists*/ false);
	};

	TSharedRef<SBulkSpriteExtractorWindow> Window = SNew(SBulkSpriteExtractorWindow);
	Window->SetOutputPathOverrideForTests(FString::Printf(TEXT("/Temp/BulkAseCommit_%s"), *Token));
	// The source copy is a side effect this test has no opinion about, and it would write into the
	// project's SourceArt on every run.
	Window->SetKeepSourceInProjectForTests(false);

	Window->AddAseSourcesForTests({ FileA, FileB });
	if (!TestEqual(TEXT("both files became .ase rows"), Window->GetAseRowCountForTests(), 2))
	{
		return false;
	}
	TestTrue(TEXT("row A parsed and is awaiting import"),
		SBulkSpriteExtractorWindow::StatusAwaitsAseImport(Window->GetAseRowStatusForTests(0)));
	TestTrue(TEXT("row B parsed and is awaiting import"),
		SBulkSpriteExtractorWindow::StatusAwaitsAseImport(Window->GetAseRowStatusForTests(1)));

	TestTrue(TEXT("the batch commit reports every attempted row succeeded"),
		Window->CommitAseSourcesForTests());

	TestEqual(TEXT("row A is stamped imported"),
		Window->GetAseRowStatusForTests(0), EBulkExtractorTextureStatus::AseImported);
	TestEqual(TEXT("row B is stamped imported"),
		Window->GetAseRowStatusForTests(1), EBulkExtractorTextureStatus::AseImported);

	// THE point of the batch model. The second row must have merged into the asset the FIRST row
	// created, which is only true because the commit adopts it INSIDE the loop.
	UPaper2DPlusCharacterLayerAsset* Merged = Window->GetBatchLayerProfileForTests().LoadSynchronous();
	if (!TestNotNull(TEXT("the batch adopted a single Layer Profile"), Merged))
	{
		return false;
	}
	TestTrue(TEXT("the first file's layer is present"), BulkCommit_HasLayerNamed(*Merged, TEXT("Torso")));
	TestTrue(TEXT("the SECOND file merged into the SAME asset, rather than minting its own"),
		BulkCommit_HasLayerNamed(*Merged, TEXT("Cape")));

	// And the merge is additive per animation: each file contributed its own tag, so the shared
	// Character Profile holds both animations rather than the second replacing the first.
	if (UPaper2DPlusCharacterProfileAsset* Profile = Merged->BaseProfile.LoadSynchronous())
	{
		const auto HasAnimation = [Profile](const TCHAR* Name)
		{
			return Profile->Flipbooks.ContainsByPredicate([Name](const FFlipbookProfileEntry& Entry)
			{
				return Entry.Identity.FlipbookName.Contains(Name);
			});
		};
		TestTrue(TEXT("the first file's animation survives the second import"), HasAnimation(TEXT("Idle")));
		TestTrue(TEXT("the second file's animation was appended, not substituted"), HasAnimation(TEXT("Walk")));
	}

	// A re-run must leave the imported rows alone — the retry rule, observed through the real loop
	// rather than through the predicate alone.
	TestTrue(TEXT("re-running the commit with nothing awaiting import succeeds trivially"),
		Window->CommitAseSourcesForTests());
	TestEqual(TEXT("row A was not re-imported"),
		Window->GetAseRowStatusForTests(0), EBulkExtractorTextureStatus::AseImported);
	TestEqual(TEXT("row B was not re-imported"),
		Window->GetAseRowStatusForTests(1), EBulkExtractorTextureStatus::AseImported);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusBulkAseCommitBrokenRowTest,
	"Paper2DPlus.BulkExtractor.AseCommit.ABrokenRowFailsAloneAndLeavesTheBatchImportable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusBulkAseCommitBrokenRowTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusBulkAseCommitTest;

	const FString Token = FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(8);
	const FString Good = BulkCommit_WriteAse(*this, TEXT("BulkCommitGood_") + Token, TEXT("Torso"), TEXT("Idle"));
	if (Good.IsEmpty())
	{
		return false;
	}

	// A file that is NOT a .ase at all. The intake parses a summary up front, so this row never
	// reaches the import loop — it is refused at the door and reported, which is the honest
	// outcome: a broken file must not ride along silently inside a green batch.
	const FString BadDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Automation"));
	const FString Bad = BadDir / (TEXT("BulkCommitBad_") + Token + TEXT(".ase"));
	FFileHelper::SaveStringToFile(TEXT("this is not an aseprite file"), *Bad);
	ON_SCOPE_EXIT
	{
		IFileManager::Get().Delete(*Good, /*RequireExists*/ false);
		IFileManager::Get().Delete(*Bad, /*RequireExists*/ false);
	};

	TSharedRef<SBulkSpriteExtractorWindow> Window = SNew(SBulkSpriteExtractorWindow);
	Window->SetOutputPathOverrideForTests(FString::Printf(TEXT("/Temp/BulkAseBroken_%s"), *Token));
	Window->SetKeepSourceInProjectForTests(false);
	Window->AddAseSourcesForTests({ Bad, Good });

	if (!TestEqual(TEXT("both files became rows"), Window->GetAseRowCountForTests(), 2))
	{
		return false;
	}
	TestEqual(TEXT("the unreadable file is stamped a parse error"),
		Window->GetAseRowStatusForTests(0), EBulkExtractorTextureStatus::AseParseError);
	TestFalse(TEXT("a parse error is never attempted by the commit"),
		SBulkSpriteExtractorWindow::StatusAwaitsAseImport(Window->GetAseRowStatusForTests(0)));

	// The broken row must not prevent the good one from importing: a batch that refused to run
	// because one file is bad would make a twenty-file drop unusable.
	Window->CommitAseSourcesForTests();
	TestEqual(TEXT("the good row still imported"),
		Window->GetAseRowStatusForTests(1), EBulkExtractorTextureStatus::AseImported);
	TestEqual(TEXT("the broken row keeps its parse error for the designer to see"),
		Window->GetAseRowStatusForTests(0), EBulkExtractorTextureStatus::AseParseError);
	TestNotNull(TEXT("the batch still adopted a Layer Profile from the row that worked"),
		Window->GetBatchLayerProfileForTests().LoadSynchronous());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
#endif // WITH_EDITOR
