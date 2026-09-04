// Copyright 2026 Infinite Gameworks. All Rights Reserved.

// Coverage for the Bulk Sprite Extractor's center-pane `.ase` preview.
//
// The bug this pins: CenterCanvas draws a TEXTURE, and an `.ase` row does not have one. Selecting a
// dropped `.ase` file therefore left the whole middle of the window reading "No texture selected"
// over black, and the file's art only appeared once the per-row Edit… window was opened — so the
// batch surface gave no way to tell one dropped file from another, or a good file from a broken one.
//
// What a compiler cannot catch here is that the pane is FILLED. Every part of this can compile and
// still show nothing: the pane can be active with no decode, the decode can exist with an empty
// frame array, and the frames can exist and be fully transparent. So the assertions go all the way
// down to composited pixels, and they check the failure case too — an unreadable file has to say why
// rather than present an empty frame, which reads as a file with no art in it.
//
// The decode is also the one thing the row set deliberately does NOT keep (rows store summaries so a
// 20-file batch cannot pin hundreds of megabytes of frames), so leaving the row must release it.
//
// A deliberately self-contained `.ase` writer, per the sibling `.ase` suites: a new file with its own
// minimal fixture has zero merge surface. File-unique helper prefix `BulkAsePrev_` — unity builds
// group these translation units, so a generic helper name would collide with a sibling test file.

#if WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "AsepriteImporter.h"
#include "BulkSpriteExtractorWindow.h"
#include "Engine/Texture2D.h"
#include "HAL/FileManager.h"
#include "LayerImportPreviewCanvas.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Paper2DPlusCharacterLayerAsset.h"
#include "PaperSprite.h"
#include "UObject/Package.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace Paper2DPlusBulkAsePreviewTest
{
	/** Little-endian byte writer for crafting minimal .ase buffers. */
	struct FBulkAsePrevWriter
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

	/** The crafted canvas. Small on purpose — every pixel is asserted. */
	static const int32 BulkAsePrev_Canvas = 4;

	static TArray<uint8> BulkAsePrev_Chunk(uint16 ChunkType, const TArray<uint8>& Payload)
	{
		FBulkAsePrevWriter W;
		W.U32(static_cast<uint32>(Payload.Num() + 6));
		W.U16(ChunkType);
		W.Append(Payload);
		return W.Bytes;
	}

	static TArray<uint8> BulkAsePrev_LayerChunk(const FString& Name)
	{
		FBulkAsePrevWriter P;
		P.U16(1);   // visible
		P.U16(0);   // normal layer
		P.U16(0);   // child level
		P.U16(0); P.U16(0);
		P.U16(0);   // blend mode
		P.U8(255);  // opacity
		P.Zeros(3);
		P.Str(Name);
		return BulkAsePrev_Chunk(0x2004, P.Bytes);
	}

	static TArray<uint8> BulkAsePrev_RawCelChunk(uint16 LayerIndex, const FColor& Fill)
	{
		FBulkAsePrevWriter P;
		P.U16(LayerIndex);
		P.S16(0); P.S16(0);
		P.U8(255);
		P.U16(0);   // ASE_CEL_RAW
		P.Zeros(7);
		P.U16(static_cast<uint16>(BulkAsePrev_Canvas));
		P.U16(static_cast<uint16>(BulkAsePrev_Canvas));
		for (int32 i = 0; i < BulkAsePrev_Canvas * BulkAsePrev_Canvas; ++i)
		{
			P.U8(Fill.R); P.U8(Fill.G); P.U8(Fill.B); P.U8(Fill.A);
		}
		return BulkAsePrev_Chunk(0x2005, P.Bytes);
	}

	static TArray<uint8> BulkAsePrev_TagChunk(const FString& TagName, uint16 From, uint16 To)
	{
		FBulkAsePrevWriter P;
		P.U16(1);        // one tag
		P.Zeros(8);
		P.U16(From); P.U16(To);
		P.U8(0);         // forward
		P.Zeros(8);
		P.U8(0); P.U8(0); P.U8(0); P.U8(0);
		P.Str(TagName);
		return BulkAsePrev_Chunk(0x2018, P.Bytes);
	}

	static TArray<uint8> BulkAsePrev_Frame(const TArray<TArray<uint8>>& Chunks)
	{
		int32 ChunkBytes = 0;
		for (const TArray<uint8>& C : Chunks) { ChunkBytes += C.Num(); }

		FBulkAsePrevWriter W;
		W.U32(static_cast<uint32>(16 + ChunkBytes));
		W.U16(0xF1FA);
		W.U16(static_cast<uint16>(Chunks.Num()));
		W.U16(100);   // duration ms
		W.Zeros(2);
		W.U32(static_cast<uint32>(Chunks.Num()));
		for (const TArray<uint8>& C : Chunks) { W.Append(C); }
		return W.Bytes;
	}

	/** One visual layer and one tag spanning the whole file, with FrameFills.Num() frames — each a
	 *  different solid colour, which is what lets the scrubber assertions tell frames apart. */
	static TArray<uint8> BulkAsePrev_File(
		const FString& LayerName, const FString& TagName, const TArray<FColor>& FrameFills)
	{
		TArray<TArray<uint8>> FrameBlocks;
		for (int32 FrameIdx = 0; FrameIdx < FrameFills.Num(); ++FrameIdx)
		{
			TArray<TArray<uint8>> Chunks;
			if (FrameIdx == 0)
			{
				// Aseprite declares layers and tags once, in the first frame.
				Chunks.Add(BulkAsePrev_LayerChunk(LayerName));
				Chunks.Add(BulkAsePrev_TagChunk(TagName, 0, static_cast<uint16>(FrameFills.Num() - 1)));
			}
			Chunks.Add(BulkAsePrev_RawCelChunk(0, FrameFills[FrameIdx]));
			FrameBlocks.Add(BulkAsePrev_Frame(Chunks));
		}

		int32 FrameBytes = 0;
		for (const TArray<uint8>& F : FrameBlocks) { FrameBytes += F.Num(); }

		FBulkAsePrevWriter W;
		W.U32(static_cast<uint32>(128 + FrameBytes));
		W.U16(0xA5E0);
		W.U16(static_cast<uint16>(FrameFills.Num()));
		W.U16(static_cast<uint16>(BulkAsePrev_Canvas));
		W.U16(static_cast<uint16>(BulkAsePrev_Canvas));
		W.U16(32);   // color depth
		W.U32(0);
		W.U16(100);
		W.Zeros(128 - W.Bytes.Num());
		for (const TArray<uint8>& F : FrameBlocks) { W.Append(F); }
		return W.Bytes;
	}

	static FString BulkAsePrev_AutomationDir()
	{
		const FString Dir = FPaths::ConvertRelativePathToFull(
			FPaths::ProjectSavedDir() / TEXT("Automation"));
		IFileManager::Get().MakeDirectory(*Dir, /*Tree*/ true);
		return Dir;
	}

	/** Every pixel of the composited frame is Expected. Reports the first mismatch only — a 4x4
	 *  canvas that is wrong is wrong everywhere, and sixteen identical failures read as noise. */
	static bool BulkAsePrev_FrameIsSolid(
		FAutomationTestBase& Test, const TArray<FColor>& Pixels, const FColor& Expected, const TCHAR* What)
	{
		if (!Test.TestEqual(
				FString::Printf(TEXT("%s covers the whole canvas"), What),
				Pixels.Num(), BulkAsePrev_Canvas * BulkAsePrev_Canvas))
		{
			return false;
		}
		for (int32 i = 0; i < Pixels.Num(); ++i)
		{
			if (Pixels[i] != Expected)
			{
				Test.AddError(FString::Printf(
					TEXT("%s: pixel %d is (%d,%d,%d,%d), expected (%d,%d,%d,%d)"),
					What, i,
					Pixels[i].R, Pixels[i].G, Pixels[i].B, Pixels[i].A,
					Expected.R, Expected.G, Expected.B, Expected.A));
				return false;
			}
		}
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusBulkAsePreviewShowsArtTest,
	"Paper2DPlus.BulkExtractor.AseRows.SelectingAnAseRowFillsTheCenterPaneWithItsArt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusBulkAsePreviewShowsArtTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusBulkAsePreviewTest;

	// Three frames, three colours. Distinct fills are the only way to tell "the scrubber moved" from
	// "the scrubber says a different number while showing the same picture".
	const TArray<FColor> Fills = {
		FColor(200, 80, 40, 255),
		FColor(40, 200, 80, 255),
		FColor(80, 40, 200, 255)
	};

	const FString Token = FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(8);
	const FString File = BulkAsePrev_AutomationDir() / (TEXT("BulkAsePrev_") + Token + TEXT(".ase"));
	if (!TestTrue(TEXT("the crafted .ase reaches disk"),
			FFileHelper::SaveArrayToFile(BulkAsePrev_File(TEXT("Body"), TEXT("Walk"), Fills), *File)))
	{
		return false;
	}
	ON_SCOPE_EXIT{ IFileManager::Get().Delete(*File, /*RequireExists*/ false); };

	TSharedRef<SBulkSpriteExtractorWindow> Window = SNew(SBulkSpriteExtractorWindow);
	// The SourceArt copy is a side effect this test has no opinion about, and it would write into
	// the project on every run.
	Window->SetKeepSourceInProjectForTests(false);
	Window->AddAseSourcesForTests({ File });
	if (!TestEqual(TEXT("the file became one .ase row"), Window->GetAseRowCountForTests(), 1))
	{
		return false;
	}

	// 0. Intake selects the first row ITSELF. A dropped batch used to sit at "No texture selected"
	//    over black until the user clicked a row: the deferred first-row select only ran for a
	//    window created WITH textures, and the .ase path creates it empty and appends afterwards.
	TestTrue(TEXT("intake selected the first .ase row without a click"), Window->HasSelectionForTests());
	TestTrue(TEXT("and the center pane already holds its preview"), Window->HasAsePreviewForTests());
	// The batch's words follow its composition: a batch of files is not a list of textures.
	TestEqual(TEXT("the list header names Aseprite files"),
		Window->GetSourceListHeaderForTests(), FString(TEXT("ASEPRITE FILES")));
	TestEqual(TEXT("the commit button says Import"),
		Window->GetCommitButtonLabelForTests(), FString(TEXT("Import All")));

	Window->SelectRowForTests(0);

	// 1. The pane swaps at all. Without this the texture canvas keeps the slot and everything below
	//    is decoded into a widget nobody can see. Both slots are asserted, not just the predicate:
	//    they share one slot in the layout, so "both collapsed" is the original blank pane and
	//    "both visible" stacks the preview over the texture canvas.
	TestTrue(TEXT("selecting an .ase row hands the center pane to the .ase preview"),
		Window->IsAsePreviewPaneActiveForTests());
	TestTrue(TEXT("the .ase preview slot is the visible one"),
		Window->IsAsePreviewPaneVisibleForTests());
	TestFalse(TEXT("and the texture canvas slot is collapsed out of the way"),
		Window->IsCenterCanvasVisibleForTests());

	// The right pane's GRID / DETECTION / OPTIONS drive the texture pipeline and nothing else — an
	// `.ase` row never runs detection and CommitAseSources reads no trim flag. Leaving them up is
	// the same lie the empty center pane told: controls that look available and change nothing.
	TestFalse(TEXT("the texture-only right-pane sections are hidden for an .ase row"),
		Window->AreTextureOnlySectionsVisibleForTests());

	// 2. The title stops lying. "No texture selected" over a row the user just dropped in reads as a
	//    failure when nothing has failed.
	const FString Title = Window->GetCenterPaneTitleForTests();
	TestNotEqual(TEXT("the title no longer reads 'No texture selected'"),
		Title, FString(TEXT("No texture selected")));
	TestTrue(TEXT("the title names the file"), Title.Contains(TEXT("BulkAsePrev_")));

	// 3. There is a decode, and it matches the file.
	if (!TestTrue(TEXT("the selected row is decoded for preview"), Window->HasAsePreviewForTests()))
	{
		return false;
	}
	TestEqual(TEXT("every authored frame is previewable"),
		Window->GetAsePreviewFrameCountForTests(), Fills.Num());
	TestEqual(TEXT("the preview canvas is the file's canvas"),
		Window->GetAsePreviewSizeForTests(), FIntPoint(BulkAsePrev_Canvas, BulkAsePrev_Canvas));

	// 4. THE POINT: the frame carries the art. A decode whose frames are transparent would satisfy
	//    every assertion above and still show the user a black rectangle.
	TArray<FColor> Pixels;
	if (!TestTrue(TEXT("the previewed frame's pixels are readable"),
			Window->GetAsePreviewFramePixelsForTests(Pixels)))
	{
		return false;
	}
	BulkAsePrev_FrameIsSolid(*this, Pixels, Fills[0], TEXT("the first previewed frame"));

	// 5. The scrubber moves the picture, not just the counter.
	Window->StepAsePreviewFrameForTests(1);
	TestEqual(TEXT("stepping forward advances the frame"), Window->GetAsePreviewFrameForTests(), 1);
	Window->GetAsePreviewFramePixelsForTests(Pixels);
	BulkAsePrev_FrameIsSolid(*this, Pixels, Fills[1], TEXT("the second previewed frame"));

	// Wraps rather than sticking on an end, so a two-frame idle can be scrubbed in one direction.
	Window->StepAsePreviewFrameForTests(-1);
	Window->StepAsePreviewFrameForTests(-1);
	TestEqual(TEXT("stepping back past the start wraps to the last frame"),
		Window->GetAsePreviewFrameForTests(), Fills.Num() - 1);
	Window->GetAsePreviewFramePixelsForTests(Pixels);
	BulkAsePrev_FrameIsSolid(*this, Pixels, Fills.Last(), TEXT("the wrapped previewed frame"));

	// 6. Leaving the row releases the decode. The row set stores summaries precisely so a batch
	//    cannot pin every dropped file's frames; a preview that never let go would reintroduce that.
	Window->ClearSelectionForTests();
	TestFalse(TEXT("clearing the selection releases the decoded frames"),
		Window->HasAsePreviewForTests());
	TestFalse(TEXT("and the pane hands the slot back to the texture canvas"),
		Window->IsAsePreviewPaneActiveForTests());
	TestTrue(TEXT("the texture canvas slot is visible again"),
		Window->IsCenterCanvasVisibleForTests());
	TestFalse(TEXT("and the .ase preview slot is collapsed"),
		Window->IsAsePreviewPaneVisibleForTests());
	// With nothing selected the sections follow the BATCH. This one holds no texture at all, so
	// grid and detection stay hidden — reappearing here would put Cols/Rows/Accept Grid back on
	// screen for a batch that can never use them (a mixed batch gets them back the moment a
	// texture row is selected; that is the selection-scoped half of the rule).
	TestFalse(TEXT("an .ase-only batch keeps the texture-only sections hidden with nothing selected"),
		Window->AreTextureOnlySectionsVisibleForTests());
	TestEqual(TEXT("with nothing selected the title is the honest one for a batch of files"),
		Window->GetCenterPaneTitleForTests(), FString(TEXT("No file selected")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusAseImportSavesArtBeforeProfilesTest,
	"Paper2DPlus.BulkExtractor.AseRows.TheImportSavesGeneratedArtBeforeTheProfilesThatReferenceIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusAseImportSavesArtBeforeProfilesTest::RunTest(const FString& Parameters)
{
	// The import used to create every asset in memory and save nothing, so saving a Layer Profile
	// afterwards tripped UE's reference validator once per unsaved reference — and a profile saved
	// without its sprites has permanently dangling soft references.
	//
	// ORDER is the load-bearing half, and it is invisible to any "did it save?" assertion: the
	// validator asks the asset registry whether each referenced package is ON DISK, so a profile
	// written before its art still trips it. This drives the save through a RECORDING backend so
	// the order is observable without writing anything into the project.
	const FString Token = FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(8);
	const FString Root = FString::Printf(TEXT("/Game/P2DPSaveOrder_%s"), *Token);

	UPackage* SheetPkg = CreatePackage(*(Root / TEXT("Sheet")));
	UPackage* SpritePkg = CreatePackage(*(Root / TEXT("Sprite_00")));
	UPackage* LayerPkg = CreatePackage(*(Root / TEXT("Base_Layers")));
	if (!TestTrue(TEXT("the fixture packages exist"), SheetPkg && SpritePkg && LayerPkg))
	{
		return false;
	}

	// What lives in a package is what classifies it, so each one needs its real asset type.
	UTexture2D* Sheet = NewObject<UTexture2D>(SheetPkg, TEXT("Sheet"), RF_Public | RF_Standalone);
	UPaperSprite* Sprite = NewObject<UPaperSprite>(SpritePkg, TEXT("Sprite_00"), RF_Public | RF_Standalone);
	UPaper2DPlusCharacterLayerAsset* Layers =
		NewObject<UPaper2DPlusCharacterLayerAsset>(LayerPkg, TEXT("Base_Layers"), RF_Public | RF_Standalone);

	TArray<UPackage*> Fixtures = { SheetPkg, SpritePkg, LayerPkg };
	for (UPackage* Package : Fixtures)
	{
		Package->MarkPackageDirty();
	}
	ON_SCOPE_EXIT
	{
		// Never leave /Game packages dirty behind a test — a later Save All would try to write them.
		for (UPackage* Package : Fixtures)
		{
			if (Package)
			{
				Package->SetDirtyFlag(false);
				Package->ClearFlags(RF_Standalone);
			}
		}
	};

	TSet<TWeakObjectPtr<UPackage>> Dirtied;
	for (UPackage* Package : Fixtures)
	{
		Dirtied.Add(Package);
	}

	TSharedRef<SBulkSpriteExtractorWindow> Window = SNew(SBulkSpriteExtractorWindow);

	TArray<TArray<FString>> SaveCalls;
	Window->SetAseSaveBackendForTests(
		[&SaveCalls](const TArray<UPackage*>& Packages, TArray<UPackage*>& /*OutFailed*/)
		{
			TArray<FString> Names;
			for (UPackage* Package : Packages)
			{
				Names.Add(Package ? Package->GetName() : FString());
			}
			SaveCalls.Add(MoveTemp(Names));
			return true;
		});

	TArray<FString> FailedNames;
	const int32 Saved = Window->SaveGeneratedAsePackagesForTests(Dirtied, FailedNames);

	TestEqual(TEXT("every generated package is saved"), Saved, 3);
	TestEqual(TEXT("nothing failed"), FailedNames.Num(), 0);

	if (!TestEqual(TEXT("the save runs as two ordered passes"), SaveCalls.Num(), 2))
	{
		return false;
	}

	// PASS ONE is the art. If a profile were in here the validator would still fire, because the
	// sprites it points at would not be on disk at the moment the profile is written.
	TestEqual(TEXT("the first pass carries both art packages"), SaveCalls[0].Num(), 2);
	TestTrue(TEXT("the sheet is in the first pass"), SaveCalls[0].Contains(SheetPkg->GetName()));
	TestTrue(TEXT("the sprite is in the first pass"), SaveCalls[0].Contains(SpritePkg->GetName()));
	TestFalse(TEXT("the Layer Profile is NOT in the first pass"),
		SaveCalls[0].Contains(LayerPkg->GetName()));

	// PASS TWO is what references it.
	TestEqual(TEXT("the second pass carries only the profile"), SaveCalls[1].Num(), 1);
	TestTrue(TEXT("the Layer Profile lands after its art"),
		SaveCalls[1].Contains(LayerPkg->GetName()));

	// A save that fails must be reported, not silently counted as written — that is the state where
	// the designer still has to act before closing the editor.
	SaveCalls.Reset();
	Window->SetAseSaveBackendForTests(
		[SpritePkg](const TArray<UPackage*>& Packages, TArray<UPackage*>& OutFailed)
		{
			for (UPackage* Package : Packages)
			{
				if (Package == SpritePkg) { OutFailed.Add(Package); }
			}
			return OutFailed.Num() == 0;
		});
	for (UPackage* Package : Fixtures) { Package->MarkPackageDirty(); }

	FailedNames.Reset();
	const int32 SavedWithFailure = Window->SaveGeneratedAsePackagesForTests(Dirtied, FailedNames);
	TestEqual(TEXT("the failed package is named"), FailedNames.Num(), 1);
	TestTrue(TEXT("and it is the one the backend rejected"),
		FailedNames.Contains(SpritePkg->GetName()));
	TestEqual(TEXT("the saved count excludes it"), SavedWithFailure, 2);

	// Suppress the unused-variable warnings under -WarningsAsErrors; the objects exist to classify
	// their packages, which is all this test needs from them.
	TestTrue(TEXT("the fixture assets were created"), Sheet && Sprite && Layers);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusAsePreviewCanvasFlatSourceTest,
	"Paper2DPlus.BulkExtractor.AseRows.ThePreviewCanvasCompositesAFlatSourceWithNoPerLayerBuffers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusAsePreviewCanvasFlatSourceTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusBulkAsePreviewTest;

	// The window test above proves the DECODE carries art. It cannot prove the widget that renders
	// it composites anything, because it reads the decode directly — so restoring the canvas's old
	// "no per-layer buffers, give up" early-out would leave that test green and the pane blank,
	// which is indistinguishable from the bug. This pins the widget's own flat path instead.
	const TArray<FColor> Fills = {
		FColor(200, 80, 40, 255),
		FColor(40, 200, 80, 255),
		FColor(80, 40, 200, 255)
	};

	const FString Token = FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(8);
	const FString File = BulkAsePrev_AutomationDir() / (TEXT("BulkAsePrevCanvas_") + Token + TEXT(".ase"));
	if (!TestTrue(TEXT("the crafted .ase reaches disk"),
			FFileHelper::SaveArrayToFile(BulkAsePrev_File(TEXT("Body"), TEXT("Walk"), Fills), *File)))
	{
		return false;
	}
	ON_SCOPE_EXIT{ IFileManager::Get().Delete(*File, /*RequireExists*/ false); };

	// Declared BEFORE the canvas on purpose: the canvas holds a RAW pointer into this, so it has to
	// be destroyed first — which reverse declaration order guarantees.
	FAsepriteParsedData Data;
	FString ParseError;
	if (!TestTrue(TEXT("the crafted .ase parses"),
			FAsepriteImporter::ParseFile(File, Data, ParseError)))
	{
		return false;
	}

	// NO PerLayerBuffers — this is exactly how the bulk extractor's center pane builds it.
	TSharedRef<SLayerImportPreviewCanvas> Canvas = SNew(SLayerImportPreviewCanvas).ParsedData(&Data);

	TestEqual(TEXT("the canvas reports every authored frame"), Canvas->GetFrameCount(), Fills.Num());

	TArray<FColor> Composited;
	if (!TestTrue(TEXT("the canvas composites something for the first frame"),
			Canvas->GetCompositedFrameForTests(Composited)))
	{
		return false;
	}
	BulkAsePrev_FrameIsSolid(*this, Composited, Fills[0], TEXT("the canvas's first frame"));

	// And the frame index actually reaches the composite, rather than the canvas serving frame 0
	// forever while the host's counter moves.
	Canvas->SetFrameIndex(2);
	Canvas->GetCompositedFrameForTests(Composited);
	BulkAsePrev_FrameIsSolid(*this, Composited, Fills[2], TEXT("the canvas's third frame"));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusBulkAsePreviewExplainsUnreadableFileTest,
	"Paper2DPlus.BulkExtractor.AseRows.AnUnreadableAseRowExplainsItselfInsteadOfShowingNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusBulkAsePreviewExplainsUnreadableFileTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusBulkAsePreviewTest;

	const FString Token = FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(8);
	const FString File = BulkAsePrev_AutomationDir() / (TEXT("BulkAsePrevBad_") + Token + TEXT(".ase"));
	if (!TestTrue(TEXT("the unreadable file reaches disk"),
			FFileHelper::SaveStringToFile(TEXT("this is not an aseprite file"), *File)))
	{
		return false;
	}
	ON_SCOPE_EXIT{ IFileManager::Get().Delete(*File, /*RequireExists*/ false); };

	TSharedRef<SBulkSpriteExtractorWindow> Window = SNew(SBulkSpriteExtractorWindow);
	Window->SetKeepSourceInProjectForTests(false);
	Window->AddAseSourcesForTests({ File });
	if (!TestEqual(TEXT("the file became one .ase row"), Window->GetAseRowCountForTests(), 1))
	{
		return false;
	}
	TestEqual(TEXT("intake stamped the row a parse error"),
		Window->GetAseRowStatusForTests(0), EBulkExtractorTextureStatus::AseParseError);

	Window->SelectRowForTests(0);

	// A broken row still owns the pane — handing the slot back to the texture canvas would put the
	// user right back at "No texture selected" over black, which is what this whole change removes.
	TestTrue(TEXT("a broken .ase row still owns the center pane"),
		Window->IsAsePreviewPaneActiveForTests());
	TestFalse(TEXT("there is no decode to show"), Window->HasAsePreviewForTests());
	TestFalse(TEXT("but there IS a reason to show"), Window->GetAsePreviewErrorForTests().IsEmpty());
	TestNotEqual(TEXT("and the title names the file rather than claiming no texture is selected"),
		Window->GetCenterPaneTitleForTests(), FString(TEXT("No texture selected")));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
#endif // WITH_EDITOR
