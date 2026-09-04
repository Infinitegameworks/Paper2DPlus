// Copyright 2026 Infinite Gameworks. All Rights Reserved.

// Coverage for the `.ase` row's contract inside the Bulk Sprite Extractor (TASK-189 U5-U7).
//
// A NEW file rather than an extension of Paper2DPlusBulkFolderOrganizationTest.cpp: that file is
// about the folder organizer, and a new file has zero merge surface against a concurrent session.
//
// What is worth pinning here is exactly what a compiler cannot catch. `.ase` rows share one state
// array with texture rows, and the ONLY thing keeping them out of the texture-only phases is a pair
// of hand-maintained status predicates. Adding a fifth `.ase` status and forgetting one line of
// StatusExcludedFromExtract does not fail to build — it makes the texture pipeline call
// LoadSynchronous() on a null texture and report an Aseprite file as a grid failure.
//
// File-unique helper prefix `BulkAse_` — unity builds group these translation units, so a generic
// helper name would collide with a sibling test file.

#if WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "BulkFolderOrganizationUtils.h"
#include "BulkSpriteExtractorWindow.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace Paper2DPlusBulkAseRowTest
{
	/** Every status an `.ase` row can hold. Hard-coded rather than derived from the enum, so adding
	 *  a status forces a deliberate decision here instead of silently widening a loop. */
	static const TArray<EBulkExtractorTextureStatus> BulkAse_AllAseStatuses = {
		EBulkExtractorTextureStatus::AseParsed,
		EBulkExtractorTextureStatus::AseParseError,
		EBulkExtractorTextureStatus::AseImported,
		EBulkExtractorTextureStatus::AseImportFailed
	};

	/** The texture statuses, for the mirror assertion that none of them was caught by the .ase rule. */
	static const TArray<EBulkExtractorTextureStatus> BulkAse_AllTextureStatuses = {
		EBulkExtractorTextureStatus::Pending,
		EBulkExtractorTextureStatus::Inferred,
		EBulkExtractorTextureStatus::Overridden,
		EBulkExtractorTextureStatus::Confirmed,
		EBulkExtractorTextureStatus::SkippedNoPad,
		EBulkExtractorTextureStatus::Padded,
		EBulkExtractorTextureStatus::DebakeSource,
		EBulkExtractorTextureStatus::Error
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusBulkAseStatusExclusionTest,
	"Paper2DPlus.BulkExtractor.AseRows.EveryAseStatusIsExcludedFromTextureOnlyPhases",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusBulkAseStatusExclusionTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusBulkAseRowTest;

	// EVERY `.ase` status, without exception. A row that escapes this predicate reaches the
	// per-texture cell-size map, the auto-pad count, the texture commit loop and the trim
	// delete-source prompt — all of which dereference a texture an `.ase` row does not have.
	for (const EBulkExtractorTextureStatus Status : BulkAse_AllAseStatuses)
	{
		TestTrue(
			*FString::Printf(TEXT(".ase status %d is excluded from the texture-only phases"), (int32)Status),
			SBulkSpriteExtractorWindow::StatusExcludedFromExtract(Status));
	}

	// The mirror: the rule must not have grown wide enough to swallow ordinary texture rows. Only
	// DebakeSource is deliberately excluded among them, because its de-baked outputs carry the grid.
	for (const EBulkExtractorTextureStatus Status : BulkAse_AllTextureStatuses)
	{
		const bool bExpectedExcluded = (Status == EBulkExtractorTextureStatus::DebakeSource);
		TestEqual(
			*FString::Printf(TEXT("texture status %d exclusion is unchanged"), (int32)Status),
			SBulkSpriteExtractorWindow::StatusExcludedFromExtract(Status), bExpectedExcluded);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusBulkAseStatusGateTest,
	"Paper2DPlus.BulkExtractor.AseRows.BrokenAseRowsBlockExtractWhileReadyOnesDoNot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusBulkAseStatusGateTest::RunTest(const FString& Parameters)
{
	// The second predicate answers a different question — "may Extract All proceed?" — and the two
	// deliberately DISAGREE for the failure statuses. An `.ase` row is excluded from the texture
	// phases whether or not it parsed, but a row that FAILED must still block the batch, or a broken
	// file rides along inside a green run and the designer never learns it was dropped.
	TestTrue(TEXT("a parsed .ase row needs no confirmed grid"),
		SBulkSpriteExtractorWindow::StatusCountsAsConfirmed(EBulkExtractorTextureStatus::AseParsed));
	TestTrue(TEXT("an already-imported .ase row needs no confirmed grid"),
		SBulkSpriteExtractorWindow::StatusCountsAsConfirmed(EBulkExtractorTextureStatus::AseImported));

	TestFalse(TEXT("a parse failure BLOCKS Extract All"),
		SBulkSpriteExtractorWindow::StatusCountsAsConfirmed(EBulkExtractorTextureStatus::AseParseError));
	TestFalse(TEXT("an import failure BLOCKS Extract All"),
		SBulkSpriteExtractorWindow::StatusCountsAsConfirmed(EBulkExtractorTextureStatus::AseImportFailed));

	// And both failure statuses are STILL excluded from the texture phases — being a blocker is not
	// the same as being a texture, and conflating them is how a broken .ase became a "grid failure".
	TestTrue(TEXT("a parse failure is still not a texture"),
		SBulkSpriteExtractorWindow::StatusExcludedFromExtract(EBulkExtractorTextureStatus::AseParseError));
	TestTrue(TEXT("an import failure is still not a texture"),
		SBulkSpriteExtractorWindow::StatusExcludedFromExtract(EBulkExtractorTextureStatus::AseImportFailed));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusBulkAseRowIdentityTest,
	"Paper2DPlus.BulkExtractor.AseRows.OrganizationIdentityKeySurvivesARename",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusBulkAseRowIdentityTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlus::BulkFolderOrganization;

	// Export/Import Organization matches rows by DISPLAY NAME first and falls back to this key, which
	// is the only thing that can re-find a row the designer renamed in session. A texture row keys
	// off its source asset; an `.ase` row off its stored source path — the same identity intake
	// dedupes on. An `.ase` row used to produce an EMPTY key, so a rename made it unmatchable.
	TestEqual(TEXT("a texture row keys off its source asset path"),
		MakeRowIdentityKey(TEXT("/Game/Art/Hero.Hero"), FString()), FString(TEXT("/Game/Art/Hero.Hero")));

	TestEqual(TEXT("an .ase row keys off its stored source path"),
		MakeRowIdentityKey(FString(), TEXT("SourceArt/Hero.aseprite")), FString(TEXT("SourceArt/Hero.aseprite")));

	// A row cannot be both, but if a caller ever passes both the ASSET wins: that is the identity the
	// texture half of the pipeline already round-trips.
	TestEqual(TEXT("an asset path outranks a stored .ase path"),
		MakeRowIdentityKey(TEXT("/Game/Art/Hero.Hero"), TEXT("SourceArt/Hero.aseprite")),
		FString(TEXT("/Game/Art/Hero.Hero")));

	// Empty means "match by name only" — an honest answer, not a key that would collide with every
	// other identity-less row in the payload.
	TestTrue(TEXT("a row with neither identity produces no key"),
		MakeRowIdentityKey(FString(), FString()).IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusBulkAseRetryOnlyFailedTest,
	"Paper2DPlus.BulkExtractor.AseRows.ExtractAllRetriesOnlyRowsThatStillNeedImporting",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusBulkAseRetryOnlyFailedTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusBulkAseRowTest;

	// The batch commit's row-selection contract (R20). Re-running Extract All after fixing ONE broken
	// file must not re-import the files that already succeeded — that would duplicate work and, worse,
	// re-run the pipeline over assets the designer has since edited.
	TestTrue(TEXT("a never-imported row is attempted"),
		SBulkSpriteExtractorWindow::StatusAwaitsAseImport(EBulkExtractorTextureStatus::AseParsed));
	TestTrue(TEXT("a row whose import FAILED is retried"),
		SBulkSpriteExtractorWindow::StatusAwaitsAseImport(EBulkExtractorTextureStatus::AseImportFailed));

	TestFalse(TEXT("an already-imported row is left alone"),
		SBulkSpriteExtractorWindow::StatusAwaitsAseImport(EBulkExtractorTextureStatus::AseImported));
	TestFalse(TEXT("a parse failure is never attempted \x2014 there is nothing parsed to import"),
		SBulkSpriteExtractorWindow::StatusAwaitsAseImport(EBulkExtractorTextureStatus::AseParseError));

	// And no TEXTURE status may ever select into the `.ase` commit loop: those rows go through
	// CommitBulkExtract, and importing one as an Aseprite source would parse a `.uasset` as a `.ase`.
	for (const EBulkExtractorTextureStatus Status : BulkAse_AllTextureStatuses)
	{
		TestFalse(
			*FString::Printf(TEXT("texture status %d never enters the .ase commit loop"), (int32)Status),
			SBulkSpriteExtractorWindow::StatusAwaitsAseImport(Status));
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
#endif // WITH_EDITOR
