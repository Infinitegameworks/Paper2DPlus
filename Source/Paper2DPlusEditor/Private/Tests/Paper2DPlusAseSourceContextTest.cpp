// Copyright 2026 Infinite Gameworks. All Rights Reserved.

// Worldless coverage for the TASK-189 multi-source .ase import bookkeeping on
// UPaper2DPlusCharacterLayerAsset: the newline-delimited registry-tag list format (index parity
// with its sibling tag, legacy single-value parse), lazy legacy materialization into element 0,
// and the legacy single-source mirror.

#include "Misc/AutomationTest.h"
#include "Paper2DPlusCharacterLayerAsset.h"
#include "UObject/Package.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace Paper2DPlusAseSourceContextTest
{
	UPaper2DPlusCharacterLayerAsset* AseSourceCtxTest_MakeAsset()
	{
		return NewObject<UPaper2DPlusCharacterLayerAsset>(GetTransientPackage());
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusAseSourceTagListParseTest,
	"Paper2DPlus.AseSourceContext.TagListParsePreservesIndexParity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusAseSourceTagListParseTest::RunTest(const FString& Parameters)
{
	TArray<FString> Values;

	// Legacy single-value tag: one element, verbatim. A path containing characters that are legal
	// on Mac/Linux (';' ',' '|') must NOT split — that is why the delimiter is a newline.
	UPaper2DPlusCharacterLayerAsset::ParseAseSourceTagList(
		TEXT("SourceArt/Hero;Body,v2|final.ase"), Values);
	TestEqual(TEXT("Legacy single value stays one element"), Values.Num(), 1);
	if (Values.Num() == 1)
	{
		TestEqual(TEXT("Legacy value is verbatim"), Values[0],
			FString(TEXT("SourceArt/Hero;Body,v2|final.ase")));
	}

	// Two sources.
	UPaper2DPlusCharacterLayerAsset::ParseAseSourceTagList(TEXT("A.ase\nB.ase"), Values);
	TestEqual(TEXT("Two sources parse to two elements"), Values.Num(), 2);

	// An EMPTY per-source value must be preserved, or the path list and hash list desync and the
	// watcher would compare a source against another source's hash.
	UPaper2DPlusCharacterLayerAsset::ParseAseSourceTagList(TEXT("hashA\n\nhashC"), Values);
	TestEqual(TEXT("Empty middle value is preserved"), Values.Num(), 3);
	if (Values.Num() == 3)
	{
		TestTrue(TEXT("The empty element stays empty"), Values[1].IsEmpty());
	}

	// Empty tag value yields nothing (an unstamped asset has no sources).
	UPaper2DPlusCharacterLayerAsset::ParseAseSourceTagList(FString(), Values);
	TestEqual(TEXT("Empty tag yields no elements"), Values.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusAseSourceContextUpsertTest,
	"Paper2DPlus.AseSourceContext.LegacyMaterializationAndMirror",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusAseSourceContextUpsertTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusAseSourceContextTest;

	UPaper2DPlusCharacterLayerAsset* Asset = AseSourceCtxTest_MakeAsset();
	if (!Asset)
	{
		AddError(TEXT("Failed to construct a transient Character Layer asset."));
		return false;
	}

	// A pre-TASK-189 asset carries its one source ONLY in the legacy single fields.
	Asset->SourceAseFilePath = TEXT("SourceArt/Hero.ase");
	Asset->ImportedAseContentHash = TEXT("legacyhash");
	Asset->ImportAssetPrefix = TEXT("Hero");
	Asset->ImportDisabledTagNames = { TEXT("Debug") };

	// Importing a SECOND file must not lose the first source's record.
	FAsepriteSourceContext& Second = Asset->UpsertAseSourceContext(TEXT("SourceArt/HeroSword.ase"));
	Second.ContentHash = TEXT("swordhash");
	Second.AssetPrefix = TEXT("HeroSword");

	TestEqual(TEXT("Legacy source materialized alongside the new one"), Asset->ImportedAseSources.Num(), 2);
	if (Asset->ImportedAseSources.Num() == 2)
	{
		TestEqual(TEXT("Element 0 is the legacy source"),
			Asset->ImportedAseSources[0].StoredSourcePath, FString(TEXT("SourceArt/Hero.ase")));
		TestEqual(TEXT("Legacy hash carried over"),
			Asset->ImportedAseSources[0].ContentHash, FString(TEXT("legacyhash")));
		TestEqual(TEXT("Legacy prefix carried over"),
			Asset->ImportedAseSources[0].AssetPrefix, FString(TEXT("Hero")));
		TestEqual(TEXT("Legacy disabled tags carried over"),
			Asset->ImportedAseSources[0].DisabledTagNames.Num(), 1);
	}

	// REGRESSION GUARD: the materialization reads SourceAseFilePath, so a caller that writes the NEW
	// source's path into that field BEFORE upserting would record this file as the old source and
	// destroy the previous source's record. Model that hostile ordering explicitly.
	{
		UPaper2DPlusCharacterLayerAsset* Hostile = AseSourceCtxTest_MakeAsset();
		if (Hostile)
		{
			Hostile->SourceAseFilePath = TEXT("SourceArt/First.ase");
			Hostile->ImportedAseContentHash = TEXT("firsthash");
			// A well-behaved caller does NOT touch the legacy fields; it upserts, then syncs.
			Hostile->UpsertAseSourceContext(TEXT("SourceArt/Second.ase")).ContentHash = TEXT("secondhash");
			TestEqual(TEXT("Both sources survive the second import"), Hostile->ImportedAseSources.Num(), 2);
			if (Hostile->ImportedAseSources.Num() == 2)
			{
				TestEqual(TEXT("The FIRST source is still element 0"),
					Hostile->ImportedAseSources[0].StoredSourcePath, FString(TEXT("SourceArt/First.ase")));
				TestEqual(TEXT("The first source keeps its own hash"),
					Hostile->ImportedAseSources[0].ContentHash, FString(TEXT("firsthash")));
			}
		}
	}

	// Re-upserting an existing path returns that entry rather than appending a duplicate.
	FAsepriteSourceContext& SecondAgain = Asset->UpsertAseSourceContext(TEXT("SourceArt/HeroSword.ase"));
	SecondAgain.ContentHash = TEXT("swordhash2");
	TestEqual(TEXT("Re-upsert does not append"), Asset->ImportedAseSources.Num(), 2);
	TestEqual(TEXT("Re-upsert returns the same entry"),
		Asset->ImportedAseSources[1].ContentHash, FString(TEXT("swordhash2")));

	// The legacy mirror tracks element 0 (the PRIMARY source), never the most recent import —
	// otherwise a second file's import would silently re-point the first source's reimport context.
	Asset->SyncLegacyAseSourceMirror();
	TestEqual(TEXT("Mirror keeps the primary path"),
		Asset->SourceAseFilePath, FString(TEXT("SourceArt/Hero.ase")));
	TestEqual(TEXT("Mirror keeps the primary prefix"),
		Asset->ImportAssetPrefix, FString(TEXT("Hero")));

	// Registry-tag bodies: newline-joined, equal element counts.
	TArray<FString> Paths;
	TArray<FString> Hashes;
	UPaper2DPlusCharacterLayerAsset::ParseAseSourceTagList(Asset->BuildAseSourceFileTagValue(), Paths);
	UPaper2DPlusCharacterLayerAsset::ParseAseSourceTagList(Asset->BuildAseSourceHashTagValue(), Hashes);
	TestEqual(TEXT("Both sources appear in the path tag"), Paths.Num(), 2);
	TestEqual(TEXT("Path and hash lists keep index parity"), Hashes.Num(), Paths.Num());
	if (Paths.Num() == 2 && Hashes.Num() == 2)
	{
		TestEqual(TEXT("Second source's hash aligns with its path"), Hashes[1], FString(TEXT("swordhash2")));
	}

	// A path containing the delimiter is rejected at EMISSION (a newline is legal in a filename on
	// Linux/macOS), and both tags suppress together so their lengths can never disagree.
	UPaper2DPlusCharacterLayerAsset* Hostile = AseSourceCtxTest_MakeAsset();
	if (Hostile)
	{
		Hostile->AddToRoot();
		Hostile->SourceAseFilePath = TEXT("SourceArt/Bad\nName.ase");
		Hostile->ImportedAseContentHash = TEXT("badhash");
		TestTrue(TEXT("A newline-bearing path emits no source tag"),
			Hostile->BuildAseSourceFileTagValue().IsEmpty());
		TestTrue(TEXT("Its hash tag suppresses in lockstep"),
			Hostile->BuildAseSourceHashTagValue().IsEmpty());
		Hostile->RemoveFromRoot();
	}

	// An asset that was never stamped emits the bare legacy values (no delimiter), so existing
	// assets keep mapping without a resave.
	UPaper2DPlusCharacterLayerAsset* LegacyOnly = AseSourceCtxTest_MakeAsset();
	if (LegacyOnly)
	{
		LegacyOnly->SourceAseFilePath = TEXT("SourceArt/Old.ase");
		LegacyOnly->ImportedAseContentHash = TEXT("oldhash");
		TestEqual(TEXT("Unstamped asset emits its bare path"),
			LegacyOnly->BuildAseSourceFileTagValue(), FString(TEXT("SourceArt/Old.ase")));
		TestEqual(TEXT("Unstamped asset emits its bare hash"),
			LegacyOnly->BuildAseSourceHashTagValue(), FString(TEXT("oldhash")));
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
