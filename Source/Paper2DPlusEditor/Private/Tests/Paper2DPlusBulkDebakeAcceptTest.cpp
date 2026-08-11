// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#if WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "BulkDebakeUtils.h"
#include "BulkSpriteExtractorWindow.h"
#include "VariantDebake.h"

#include "Engine/Texture2D.h"

/**
 * Headless tests for the bulk extractor's de-bake integration seams: the status predicates that
 * gate the commit pipeline (DebakeSource is confirmed-but-excluded), and the accept path's
 * de-bake → materialise → read-back round trip on a synthetic variant set.
 *
 * Helpers carry a FILE-UNIQUE PREFIX (BulkDebakeAccept_*) — unity builds concatenate test .cpp
 * files into one TU (see CLAUDE.md / PR #158).
 */
static TArray<FColor> BulkDebakeAccept_Solid(int32 W, int32 H, FColor C)
{
	TArray<FColor> P;
	P.Init(C, W * H);
	return P;
}

// ─── 1. Status predicates: DebakeSource satisfies the extract gate but is excluded from extraction ───

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusBulkDebakeStatusPredicates,
	"Paper2DPlus.BulkDebake.StatusPredicates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusBulkDebakeStatusPredicates::RunTest(const FString& Parameters)
{
	using EStatus = EBulkExtractorTextureStatus;

	// Confirmed-gate truth table.
	TestFalse(TEXT("Pending does not count as confirmed"), SBulkSpriteExtractorWindow::StatusCountsAsConfirmed(EStatus::Pending));
	TestFalse(TEXT("Inferred does not count as confirmed"), SBulkSpriteExtractorWindow::StatusCountsAsConfirmed(EStatus::Inferred));
	TestFalse(TEXT("Overridden does not count as confirmed"), SBulkSpriteExtractorWindow::StatusCountsAsConfirmed(EStatus::Overridden));
	TestTrue(TEXT("Confirmed counts"), SBulkSpriteExtractorWindow::StatusCountsAsConfirmed(EStatus::Confirmed));
	TestTrue(TEXT("SkippedNoPad counts"), SBulkSpriteExtractorWindow::StatusCountsAsConfirmed(EStatus::SkippedNoPad));
	TestTrue(TEXT("Padded counts"), SBulkSpriteExtractorWindow::StatusCountsAsConfirmed(EStatus::Padded));
	TestTrue(TEXT("DebakeSource counts (consumed sources must not block Extract All)"),
		SBulkSpriteExtractorWindow::StatusCountsAsConfirmed(EStatus::DebakeSource));
	TestFalse(TEXT("Error does not count"), SBulkSpriteExtractorWindow::StatusCountsAsConfirmed(EStatus::Error));

	// Exclusion truth table — ONLY DebakeSource is excluded.
	TestTrue(TEXT("DebakeSource excluded from extraction"), SBulkSpriteExtractorWindow::StatusExcludedFromExtract(EStatus::DebakeSource));
	TestFalse(TEXT("Confirmed not excluded"), SBulkSpriteExtractorWindow::StatusExcludedFromExtract(EStatus::Confirmed));
	TestFalse(TEXT("Padded not excluded"), SBulkSpriteExtractorWindow::StatusExcludedFromExtract(EStatus::Padded));
	TestFalse(TEXT("SkippedNoPad not excluded"), SBulkSpriteExtractorWindow::StatusExcludedFromExtract(EStatus::SkippedNoPad));
	TestFalse(TEXT("Pending not excluded"), SBulkSpriteExtractorWindow::StatusExcludedFromExtract(EStatus::Pending));
	TestFalse(TEXT("Error not excluded"), SBulkSpriteExtractorWindow::StatusExcludedFromExtract(EStatus::Error));
	return true;
}

// ─── 2. Accept-path round trip: de-bake a synthetic variant set, materialise base + overlays,
//        read them back pixel-identical (the FVariantDebake → WriteSheetTexture → LoadPixels chain
//        the accept button drives) ───

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusBulkDebakeMaterialiseRoundTrip,
	"Paper2DPlus.BulkDebake.MaterialiseRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusBulkDebakeMaterialiseRoundTrip::RunTest(const FString& Parameters)
{
	const int32 W = 8, H = 4; // 2 cells of 4x4
	const FColor BaseC(10, 20, 30, 255);
	const FColor Uniques[3] = { FColor(200, 0, 0, 255), FColor(0, 200, 0, 255), FColor(0, 0, 200, 255) };

	TArray<FDebakeSheetInput> Sheets;
	for (int32 i = 0; i < 3; ++i)
	{
		FDebakeSheetInput In;
		In.Pixels = BulkDebakeAccept_Solid(W, H, BaseC);
		In.Pixels[i] = Uniques[i]; // each variant deviates at its own distinct pixel
		In.Width = W;
		In.Height = H;
		Sheets.Add(MoveTemp(In));
	}

	FDebakeSettings Settings;
	Settings.Columns = 2;
	Settings.Rows = 1;

	FDebakeResult Result;
	FText Error;
	if (!TestTrue(TEXT("Run succeeds"), FVariantDebake::Run(Sheets, Settings, Result, Error)))
	{
		return false;
	}

	const FString TempPath = TEXT("/Temp/Paper2DPlusBulkDebakeAcceptTests");
	const TArray<FString> Names = { TEXT("T_Fx_Red"), TEXT("T_Fx_Green"), TEXT("T_Fx_Blue") };
	const FString RawPrefix = FBulkDebakeUtils::RawCommonPrefix(Names);

	// Base.
	UTexture2D* BaseTex = FBulkDebakeUtils::WriteSheetTexture(TempPath, TEXT("T_Fx_Base"), W, H, Result.BasePixels);
	if (!TestNotNull(TEXT("Base texture written"), BaseTex))
	{
		return false;
	}
	{
		TArray<FColor> ReadBack;
		int32 RW = 0, RH = 0;
		if (!TestTrue(TEXT("Base reads back"), FBulkDebakeUtils::LoadPixels(BaseTex, ReadBack, RW, RH)))
		{
			return false;
		}
		for (int32 P = 0; P < W * H; ++P)
		{
			if (ReadBack[P] != Result.BasePixels[P])
			{
				AddError(FString::Printf(TEXT("Base pixel %d did not round-trip"), P));
				return false;
			}
		}
	}

	// Overlays (each variant emitted exactly its one deviating pixel).
	for (int32 i = 0; i < 3; ++i)
	{
		TestEqual(TEXT("One overlay pixel per variant"), Result.OverlayPixelCounts[i], 1);
		const FString Label = FBulkDebakeUtils::VariantLabel(Names[i], RawPrefix);
		FString OverlayName = FString::Printf(TEXT("T_Fx_Overlay_%s"), *Label);
		FSpriteExtractionUtils::SanitizeAssetName(OverlayName);
		UTexture2D* OverlayTex = FBulkDebakeUtils::WriteSheetTexture(TempPath, OverlayName, W, H, Result.Overlays[i]);
		if (!TestNotNull(TEXT("Overlay texture written"), OverlayTex))
		{
			return false;
		}
		TArray<FColor> ReadBack;
		int32 RW = 0, RH = 0;
		if (!TestTrue(TEXT("Overlay reads back"), FBulkDebakeUtils::LoadPixels(OverlayTex, ReadBack, RW, RH)))
		{
			return false;
		}
		TestEqual(TEXT("Overlay carries the variant's own pixel"), ReadBack[i], Uniques[i]);

		// Composite reconstructs the original variant sheet.
		TArray<FColor> Composite;
		if (TestTrue(TEXT("Composite builds"), FBulkDebakeUtils::BuildCompositeBuffer(Result.BasePixels, ReadBack, Composite)))
		{
			for (int32 P = 0; P < W * H; ++P)
			{
				if (Composite[P] != Sheets[i].Pixels[P])
				{
					AddError(FString::Printf(TEXT("Variant %d composite pixel %d does not reconstruct the original"), i, P));
					return false;
				}
			}
		}
	}
	return true;
}

// ─── 3. Group consumption includes member, front-VFX, and back-VFX paths. Revert uses this
//        domain predicate to preserve shared VFX sources until the last accepted group releases. ───

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusBulkDebakeGroupConsumption,
	"Paper2DPlus.BulkDebake.GroupConsumption",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusBulkDebakeGroupConsumption::RunTest(const FString& Parameters)
{
	const FSoftObjectPath MainPath(TEXT("/Game/Debake/T_Main.T_Main"));
	const FSoftObjectPath FrontPath(TEXT("/Game/Debake/VFX_Front.VFX_Front"));
	const FSoftObjectPath BackPath(TEXT("/Game/Debake/VFX_Back.VFX_Back"));

	const TSharedPtr<FBulkExtractorTextureState> State = MakeShared<FBulkExtractorTextureState>();
	State->Texture = TSoftObjectPtr<UTexture2D>(MainPath);

	FDebakeGroupMember Member;
	Member.State = State;
	Member.VfxFront = TSoftObjectPtr<UTexture2D>(FrontPath);
	Member.VfxBack = TSoftObjectPtr<UTexture2D>(BackPath);

	FDebakeGroupState Group;
	Group.Members.Add(Member);

	TestTrue(TEXT("Member main is consumed"), Group.ConsumesTexturePath(MainPath));
	TestTrue(TEXT("Front VFX is consumed"), Group.ConsumesTexturePath(FrontPath));
	TestTrue(TEXT("Back VFX is consumed"), Group.ConsumesTexturePath(BackPath));
	TestFalse(TEXT("Unrelated texture is not consumed"),
		Group.ConsumesTexturePath(FSoftObjectPath(TEXT("/Game/Debake/T_Other.T_Other"))));
	TestFalse(TEXT("Null path is not consumed"), Group.ConsumesTexturePath(FSoftObjectPath()));
	return true;
}

#endif // WITH_EDITOR
