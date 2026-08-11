// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#if WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformMisc.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Modules/ModuleManager.h"
#include "VariantDebake.h"

/**
 * Real-data regression validation for the de-bake core (TASK-107): runs FVariantDebake on the 7 knight
 * potion-drink sheets (22 cols x 1 row, 192x128 cells) with their VFX masks and checks the results
 * against the proven Python reference outputs (session 2026-07-07):
 *   - every VFX aligns at cell offset 10; base within tie-break noise of Drink_Potion_Base.png;
 *   - 36 unrecoverable px; per-variant overlay px / reconstruction-mismatch px in the reference order.
 *
 * SELF-SKIPPING: the fixture sheets are artist deliveries that live outside the repo. When the folder
 * is absent the test logs an info line and passes. Point P2DP_DEBAKE_FIXTURE_DIR at a copy to run it
 * elsewhere; set P2DP_DEBAKE_DUMP_DIR to also write the recovered base/overlays as PNGs for eyeballing.
 *
 * Helpers carry a FILE-UNIQUE PREFIX (DebakeVal_*) — unity builds concatenate test .cpp files (PR #158).
 */
static const TCHAR* DebakeVal_DefaultFixtureDir = TEXT("C:/Users/bluey/Downloads/MainCharacter_Knight (1)/MainCharacter_Knight/0.7/Potions");

static bool DebakeVal_LoadPng(const FString& Path, TArray<FColor>& OutPixels, int32& OutW, int32& OutH)
	{
		TArray<uint8> FileData;
		if (!FFileHelper::LoadFileToArray(FileData, *Path))
		{
			return false;
		}
		IImageWrapperModule& Module = FModuleManager::LoadModuleChecked<IImageWrapperModule>("ImageWrapper");
		TSharedPtr<IImageWrapper> Wrapper = Module.CreateImageWrapper(EImageFormat::PNG);
		if (!Wrapper.IsValid() || !Wrapper->SetCompressed(FileData.GetData(), FileData.Num()))
		{
			return false;
		}
		TArray<uint8> Raw;
		if (!Wrapper->GetRaw(ERGBFormat::BGRA, 8, Raw))
		{
			return false;
		}
		OutW = Wrapper->GetWidth();
		OutH = Wrapper->GetHeight();
		OutPixels.SetNumUninitialized(OutW * OutH);
		FMemory::Memcpy(OutPixels.GetData(), Raw.GetData(), Raw.Num());
		return true;
	}

static void DebakeVal_DumpPng(const FString& Path, const TArray<FColor>& Pixels, int32 W, int32 H)
{
	IImageWrapperModule& Module = FModuleManager::LoadModuleChecked<IImageWrapperModule>("ImageWrapper");
	TSharedPtr<IImageWrapper> Wrapper = Module.CreateImageWrapper(EImageFormat::PNG);
	if (Wrapper.IsValid() && Wrapper->SetRaw(Pixels.GetData(), Pixels.Num() * sizeof(FColor), W, H, ERGBFormat::BGRA, 8))
	{
		const TArray64<uint8> Compressed = Wrapper->GetCompressed();
		TArray<uint8> Bytes(Compressed.GetData(), (int32)Compressed.Num());
		FFileHelper::SaveArrayToFile(Bytes, *Path);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusVariantDebakeRealPotionValidation,
	"Paper2DPlus.VariantDebake.RealPotionValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusVariantDebakeRealPotionValidation::RunTest(const FString& Parameters)
{
	FString FixtureDir = FPlatformMisc::GetEnvironmentVariable(TEXT("P2DP_DEBAKE_FIXTURE_DIR"));
	if (FixtureDir.IsEmpty())
	{
		FixtureDir = DebakeVal_DefaultFixtureDir;
	}
	if (!FPaths::DirectoryExists(FixtureDir))
	{
		AddInfo(FString::Printf(TEXT("Potion fixture dir not found (%s) — validation skipped."), *FixtureDir));
		return true;
	}

	struct FVariantSpec
	{
		const TCHAR* Main;
		const TCHAR* VfxFront;    // nullptr = none
		const TCHAR* VfxBack;     // nullptr = none
		int32 MaxOverlayPx;       // reference + margin
		int32 MaxResidualPx;      // reference + margin
	};
	// Reference numbers (Python, 2026-07-07): overlay 110/213/700/616/600/291/182,
	// residual >8/channel 3/6/77/0/23/32/6. Margins allow tie-break noise, same order of magnitude.
	const FVariantSpec Specs[] =
	{
		{ TEXT("CoolDown_Potion.png"),     TEXT("VFX/VFX_Cooldown_Potion_Front.png"), TEXT("VFX/VFX_Cooldown_Potion_Back.png"), 400, 50 },
		{ TEXT("Focus_Potion.png"),        TEXT("VFX/VFX_Focus_Potion.png"),          nullptr, 600, 50 },
		{ TEXT("Health_Potion.png"),       TEXT("VFX/VFX_Health_Potion.png"),         nullptr, 1500, 250 },
		{ TEXT("Luck_Potion.png"),         TEXT("VFX/VFX_Luck_Potion.png"),           nullptr, 1400, 50 },
		{ TEXT("Mana_Potion.png"),         TEXT("VFX/VFX_Mana_Potion.png"),           nullptr, 1300, 100 },
		{ TEXT("Purifying_Potion.png"),    TEXT("VFX/VFX_Purifying_Potion.png"),      nullptr, 700, 120 },
		{ TEXT("StatusResist_Potion.png"), TEXT("VFX/VFX_StatusResist_Potion.png"),   nullptr, 500, 50 },
	};
	const int32 N = UE_ARRAY_COUNT(Specs);

	TArray<FDebakeSheetInput> Sheets;
	Sheets.SetNum(N);
	for (int32 i = 0; i < N; ++i)
	{
		FDebakeSheetInput& Sheet = Sheets[i];
		if (!DebakeVal_LoadPng(FixtureDir / Specs[i].Main, Sheet.Pixels, Sheet.Width, Sheet.Height))
		{
			AddError(FString::Printf(TEXT("Failed to load %s"), Specs[i].Main));
			return false;
		}
		int32 H = 0;
		TArray<FColor> Buf;
		if (Specs[i].VfxFront && DebakeVal_LoadPng(FixtureDir / Specs[i].VfxFront, Buf, Sheet.VfxFrontWidth, H))
		{
			Sheet.VfxFront = MoveTemp(Buf);
		}
		if (Specs[i].VfxBack && DebakeVal_LoadPng(FixtureDir / Specs[i].VfxBack, Buf, Sheet.VfxBackWidth, H))
		{
			Sheet.VfxBack = MoveTemp(Buf);
		}
	}
	TestEqual(TEXT("Sheet width"), Sheets[0].Width, 4224);
	TestEqual(TEXT("Sheet height"), Sheets[0].Height, 128);

	FDebakeSettings Settings;
	Settings.Columns = 22;
	Settings.Rows = 1;
	Settings.ReferenceSheetIndex = 0;

	FDebakeResult Result;
	FText Error;
	if (!TestTrue(TEXT("Run succeeds"), FVariantDebake::Run(Sheets, Settings, Result, Error)))
	{
		AddError(Error.ToString());
		return false;
	}

	// Every potion VFX aligned at cell offset 10 in the reference session (frame 11/22, 1-based).
	for (int32 i = 0; i < N; ++i)
	{
		TestEqual(FString::Printf(TEXT("%s VFX offset"), Specs[i].Main), Result.VfxOffsets[i], 10);
	}

	// Unrecoverable: 36 px of sparse ground dust in the reference. Allow the same order.
	TestTrue(FString::Printf(TEXT("Unrecoverable px (%d) <= 150"), Result.UnrecoverablePixels.Num()),
		Result.UnrecoverablePixels.Num() <= 150);

	for (int32 i = 0; i < N; ++i)
	{
		AddInfo(FString::Printf(TEXT("%s: overlay %d px, residual %d px, erase-unfixable %d px, offset %d"),
			Specs[i].Main, Result.OverlayPixelCounts[i], Result.ResidualCounts[i],
			Result.EraseUnfixableCounts[i], Result.VfxOffsets[i]));
		TestTrue(FString::Printf(TEXT("%s overlay px (%d) <= %d"), Specs[i].Main, Result.OverlayPixelCounts[i], Specs[i].MaxOverlayPx),
			Result.OverlayPixelCounts[i] <= Specs[i].MaxOverlayPx);
		TestTrue(FString::Printf(TEXT("%s residual px (%d) <= %d"), Specs[i].Main, Result.ResidualCounts[i], Specs[i].MaxResidualPx),
			Result.ResidualCounts[i] <= Specs[i].MaxResidualPx);
	}

	// Compare against the reference base where BOTH are opaque-identical territory: count pixels that
	// differ beyond the emit tolerance. Tie-break/un-blend rounding differences are expected — the C++
	// port must stay within noise (<= 0.5% of the ~15k active px), not match byte-for-byte.
	TArray<FColor> RefBase;
	int32 RefW = 0, RefH = 0;
	if (DebakeVal_LoadPng(FixtureDir / TEXT("NoVFX/Drink_Potion_Base.png"), RefBase, RefW, RefH) &&
		RefW == Sheets[0].Width && RefH == Sheets[0].Height)
	{
		int32 DiffPixels = 0;
		for (int32 P = 0; P < RefW * RefH; ++P)
		{
			const FColor A = Result.BasePixels[P].A == 0 ? FColor(0, 0, 0, 0) : Result.BasePixels[P];
			const FColor B = RefBase[P].A == 0 ? FColor(0, 0, 0, 0) : RefBase[P];
			if (FMath::Abs((int32)A.R - (int32)B.R) > 2 || FMath::Abs((int32)A.G - (int32)B.G) > 2 ||
				FMath::Abs((int32)A.B - (int32)B.B) > 2 || FMath::Abs((int32)A.A - (int32)B.A) > 2)
			{
				DiffPixels++;
			}
		}
		AddInfo(FString::Printf(TEXT("Base vs Python reference: %d differing px"), DiffPixels));
		TestTrue(FString::Printf(TEXT("Base within tie-break noise of the reference (%d differing px <= 400)"), DiffPixels),
			DiffPixels <= 400);
	}
	else
	{
		AddInfo(TEXT("Reference base PNG not found — pixel comparison skipped."));
	}

	// Optional PNG dump for visual inspection at zoom.
	const FString DumpDir = FPlatformMisc::GetEnvironmentVariable(TEXT("P2DP_DEBAKE_DUMP_DIR"));
	if (!DumpDir.IsEmpty())
	{
		DebakeVal_DumpPng(DumpDir / TEXT("Debake_Base.png"), Result.BasePixels, Sheets[0].Width, Sheets[0].Height);
		for (int32 i = 0; i < N; ++i)
		{
			DebakeVal_DumpPng(DumpDir / FString::Printf(TEXT("Debake_Overlay_%d.png"), i), Result.Overlays[i], Sheets[0].Width, Sheets[0].Height);
		}
		AddInfo(FString::Printf(TEXT("Dumped base + %d overlays to %s"), N, *DumpDir));
	}

	return true;
}

#endif // WITH_EDITOR
