// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#if WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "CharacterSizingFit.h"

/*
 * TASK-156 U8 -- the pixel-to-world size readout the sprite extractor shows.
 *
 * The readout is PASSIVE: it reads dimensions and changes nothing about extraction. What is pinned
 * here is that it converts correctly and that it always NAMES the pixels-per-unit it used.
 *
 * That last point is the whole reason this is not a one-liner. Extraction hard-codes
 * pixels-per-unit to 1 (FSpriteExtractionUtils' CreateSpritesFromSheet), so for freshly extracted
 * sprites the pixel and world numbers are equal -- which makes it very easy to write a readout that
 * silently assumes 1 and is quietly wrong for any sprite authored elsewhere. The assertions below
 * therefore include a non-default pixels-per-unit, where a readout that assumed 1 would disagree.
 *
 * Helpers are ExtractorReadout_-prefixed per the unity-build file-unique-name rule.
 */

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusExtractorReadout,
	"Paper2DPlus.ExtractorReadout.StatesTheConversionAndTheUnitItUsed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusExtractorReadout::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusCharacterSizing;

	// The extractor's own default: 1 px per unit, so 64x64 px is 64x64 uu.
	{
		const FString Readout = FormatPixelSizeReadout(FIntPoint(64, 64), 1.0f);
		TestTrue(TEXT("reports the pixel dimensions"), Readout.Contains(TEXT("64x64 px")));
		TestTrue(TEXT("reports the world dimensions"), Readout.Contains(TEXT("64x64 uu")));
		TestTrue(TEXT("and names the pixels-per-unit it used"), Readout.Contains(TEXT("1 px/uu")));
	}

	// Non-square, to catch an axis swap.
	{
		const FString Readout = FormatPixelSizeReadout(FIntPoint(32, 80), 1.0f);
		TestTrue(TEXT("width and height are not transposed"), Readout.Contains(TEXT("32x80 px")));
		TestTrue(TEXT("nor in world units"), Readout.Contains(TEXT("32x80 uu")));
	}

	// THE POINT: a non-default pixels-per-unit must change the world size AND be reported. A
	// readout that assumed the extractor's 1 px/uu would print "128x128 uu @ 1 px/uu" here.
	{
		const FString Readout = FormatPixelSizeReadout(FIntPoint(128, 128), 2.0f);
		TestTrue(TEXT("2 px/uu halves the world size"), Readout.Contains(TEXT("64x64 uu")));
		TestTrue(TEXT("and the value used is reported, not assumed"),
			Readout.Contains(TEXT("2 px/uu")));
		TestFalse(TEXT("it does not report the extractor's default when that is not what was used"),
			Readout.Contains(TEXT("@ 1 px/uu")));
	}

	// Fractional pixels-per-unit, the case where a naive integer format would lie.
	{
		const FString Readout = FormatPixelSizeReadout(FIntPoint(32, 32), 0.5f);
		TestTrue(TEXT("half a pixel per unit doubles the world size"),
			Readout.Contains(TEXT("64x64 uu")));
		TestTrue(TEXT("and the fractional unit survives formatting"),
			Readout.Contains(TEXT("0.5 px/uu")));
	}

	// Degenerate input produces nothing rather than "0x0 px" noise or a divide by zero.
	{
		TestTrue(TEXT("zero dimensions produce no readout"),
			FormatPixelSizeReadout(FIntPoint(0, 0), 1.0f).IsEmpty());
		TestTrue(TEXT("negative dimensions produce no readout"),
			FormatPixelSizeReadout(FIntPoint(-4, 10), 1.0f).IsEmpty());
	}

	// Zero pixels-per-unit is clamped exactly like every other conversion in this home, so the
	// readout stays finite instead of printing inf.
	{
		const FString Readout = FormatPixelSizeReadout(FIntPoint(1, 1), 0.0f);
		TestFalse(TEXT("a zero pixels-per-unit still produces a readout"), Readout.IsEmpty());
		TestFalse(TEXT("and it is finite, not inf"), Readout.Contains(TEXT("inf")));
	}
	return true;
}

#endif // WITH_EDITOR
