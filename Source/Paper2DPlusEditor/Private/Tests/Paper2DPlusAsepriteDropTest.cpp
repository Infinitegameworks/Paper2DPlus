// Copyright 2026 Infinite Gameworks. All Rights Reserved.

// The Content Browser drop of Aseprite files.
//
// The bug this pins: a five-file drop delivered ONE file. UAsepriteFactory must report a cancel (the
// file is a batch source, not an asset), and the engine's per-file import loop stops at the first
// cancel. The drop extender claims the drop before that loop and hands the window the whole list;
// the one pure decision in it — which files are ours and which keep going down the ordinary import
// road — is what a headless test can hold still.

#if WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "AsepriteContentBrowserDrop.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusAsepriteDropSplitsAWholeDropTest,
	"Paper2DPlus.BulkExtractor.AseRows.AContentBrowserDropKeepsEveryAsepriteFileAndPassesTheRestOn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusAsepriteDropSplitsAWholeDropTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusEditor::AsepriteContentBrowserDrop;

	// Both spellings, either case — Aseprite writes ".aseprite" by default and ".ase" on request,
	// and Windows preserves whatever case the artist typed.
	TestTrue(TEXT(".ase is an Aseprite file"), IsAsepriteFile(TEXT("C:/Art/Hero.ase")));
	TestTrue(TEXT(".aseprite is an Aseprite file"), IsAsepriteFile(TEXT("C:/Art/Hero.aseprite")));
	TestTrue(TEXT("extension case does not matter"), IsAsepriteFile(TEXT("C:/Art/HERO.ASEPRITE")));
	TestFalse(TEXT("a PNG is not"), IsAsepriteFile(TEXT("C:/Art/Hero.png")));
	TestFalse(TEXT("a name that merely contains the word is not"), IsAsepriteFile(TEXT("C:/Art/aseprite_notes.txt")));

	// THE POINT: every Aseprite file in the drop survives, in drop order, and the rest are handed
	// on rather than dropped on the floor.
	const TArray<FString> Dropped = {
		TEXT("C:/Art/Hero_Body.aseprite"),
		TEXT("C:/Art/Hero_Reference.png"),
		TEXT("C:/Art/Hero_Suit.ase"),
		TEXT("C:/Art/Hero_Head.aseprite"),
		TEXT("C:/Art/notes.txt"),
	};
	TArray<FString> AseFiles;
	TArray<FString> OtherFiles;
	SplitDroppedFiles(Dropped, AseFiles, OtherFiles);

	if (TestEqual(TEXT("all three Aseprite files are kept"), AseFiles.Num(), 3))
	{
		TestEqual(TEXT("first, in drop order"), AseFiles[0], Dropped[0]);
		TestEqual(TEXT("second, in drop order"), AseFiles[1], Dropped[2]);
		TestEqual(TEXT("third, in drop order"), AseFiles[2], Dropped[3]);
	}
	if (TestEqual(TEXT("both non-Aseprite files are passed on"), OtherFiles.Num(), 2))
	{
		TestEqual(TEXT("the PNG is passed on"), OtherFiles[0], Dropped[1]);
		TestEqual(TEXT("the text file is passed on"), OtherFiles[1], Dropped[4]);
	}

	// The split resets its outputs, so a reused pair of arrays cannot accumulate a previous drop.
	SplitDroppedFiles({ TEXT("C:/Art/Only.png") }, AseFiles, OtherFiles);
	TestEqual(TEXT("a drop with no Aseprite file yields none"), AseFiles.Num(), 0);
	TestEqual(TEXT("and passes its one file on"), OtherFiles.Num(), 1);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
#endif // WITH_EDITOR
