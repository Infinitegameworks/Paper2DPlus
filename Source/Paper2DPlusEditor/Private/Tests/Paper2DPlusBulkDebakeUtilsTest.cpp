// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#if WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "BulkDebakeUtils.h"
#include "SpriteExtractionUtils.h"
#include "VariantDebake.h"

#include "Engine/Texture2D.h"

/**
 * Worldless tests for the shared de-bake editor helpers (FBulkDebakeUtils) plus the crash-safety
 * ratchets the bulk-extractor integration depends on: FVariantDebake::Run's overlays-always-W*H
 * invariant (WriteSheetTexture's memcpy contract), WriteSheetTexture's fail-closed size guard, and
 * the VFX bad-cell-size validation that bounds FVfxView::Sample.
 *
 * Helpers carry a FILE-UNIQUE PREFIX (BulkDebake_*) because unity builds concatenate test .cpp files
 * into one TU — generic anon-namespace helper names collide (see CLAUDE.md / PR #158).
 */
static TArray<FColor> BulkDebake_Solid(int32 W, int32 H, FColor C)
{
	TArray<FColor> P;
	P.Init(C, W * H);
	return P;
}

static FDebakeSheetInput BulkDebake_Sheet(TArray<FColor> Pixels, int32 W, int32 H)
{
	FDebakeSheetInput In;
	In.Pixels = MoveTemp(Pixels);
	In.Width = W;
	In.Height = H;
	return In;
}

// ─── 1. Ratchet: Run() always emits FULL-SIZE overlays, even when a variant carries zero overlay
//        pixels. WriteSheetTexture's stale-overwrite path memcpys W*H*4 bytes from Overlays[i] — a
//        future "leave empty overlays unallocated" optimisation must fail THIS test, not corrupt
//        memory. ───

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusBulkDebakeOverlayBufferInvariant,
	"Paper2DPlus.BulkDebake.OverlayBufferInvariant",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusBulkDebakeOverlayBufferInvariant::RunTest(const FString& Parameters)
{
	const int32 W = 8, H = 4;
	const FColor BaseC(10, 20, 30, 255);

	// Two IDENTICAL sheets: the base reconstructs both perfectly, so no overlay pixel is emitted.
	TArray<FDebakeSheetInput> Sheets;
	Sheets.Add(BulkDebake_Sheet(BulkDebake_Solid(W, H, BaseC), W, H));
	Sheets.Add(BulkDebake_Sheet(BulkDebake_Solid(W, H, BaseC), W, H));

	FDebakeSettings Settings;
	Settings.Columns = 2;
	Settings.Rows = 1;

	FDebakeResult Result;
	FText Error;
	if (!TestTrue(TEXT("Run succeeds"), FVariantDebake::Run(Sheets, Settings, Result, Error)))
	{
		return false;
	}

	for (int32 i = 0; i < Sheets.Num(); ++i)
	{
		TestEqual(TEXT("No overlay pixels emitted"), Result.OverlayPixelCounts[i], 0);
		TestEqual(TEXT("Overlay buffer is STILL full sheet size (the WriteSheetTexture memcpy contract)"),
			Result.Overlays[i].Num(), W * H);
	}
	TestEqual(TEXT("Base buffer is full sheet size"), Result.BasePixels.Num(), W * H);
	return true;
}

// ─── 2. WriteSheetTexture fails closed on a size-mismatched buffer, and round-trips a valid one ───

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusBulkDebakeWriteSheetTextureGuard,
	"Paper2DPlus.BulkDebake.WriteSheetTextureGuard",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusBulkDebakeWriteSheetTextureGuard::RunTest(const FString& Parameters)
{
	const FString TempPath = TEXT("/Temp/Paper2DPlusBulkDebakeTests");

	// Undersized buffer → refused (error logged, nullptr) BEFORE any package/texture is touched.
	AddExpectedError(TEXT("refusing to write"), EAutomationExpectedErrorFlags::Contains, 3);
	TArray<FColor> Short;
	Short.Init(FColor::Red, 15); // 4x4 needs 16
	TestNull(TEXT("Undersized buffer refused"), FBulkDebakeUtils::WriteSheetTexture(TempPath, TEXT("T_BulkDebakeGuard"), 4, 4, Short));
	TestNull(TEXT("Zero width refused"), FBulkDebakeUtils::WriteSheetTexture(TempPath, TEXT("T_BulkDebakeGuard"), 0, 4, Short));
	TestNull(TEXT("Empty buffer at full dims refused"), FBulkDebakeUtils::WriteSheetTexture(TempPath, TEXT("T_BulkDebakeGuard"), 4, 4, TArray<FColor>()));
	AddExpectedError(TEXT("not a valid long package path"), EAutomationExpectedErrorFlags::Contains, 1);
	TestNull(TEXT("Invalid output path refused"),
		FBulkDebakeUtils::WriteSheetTexture(TEXT("NotAContentPath"), TEXT("T_BulkDebakeGuard"), 4, 4, BulkDebake_Solid(4, 4, FColor::Red)));
	TestNull(TEXT("Mismatched transient preview refused"),
		FBulkDebakeUtils::CreateTransientPreviewTexture(4, 4, TArray<FColor>()));

	// Valid buffer → written, and reads back pixel-identical.
	TArray<FColor> Pixels;
	Pixels.Reserve(16);
	for (int32 P = 0; P < 16; ++P)
	{
		Pixels.Add(FColor(P * 10, 255 - P * 10, P, 255));
	}
	UTexture2D* Written = FBulkDebakeUtils::WriteSheetTexture(TempPath, TEXT("T_BulkDebakeRoundTrip"), 4, 4, Pixels);
	if (!TestNotNull(TEXT("Valid write succeeds"), Written))
	{
		return false;
	}
	TArray<FColor> ReadBack;
	int32 RW = 0, RH = 0;
	if (!TestTrue(TEXT("Read back"), FBulkDebakeUtils::LoadPixels(Written, ReadBack, RW, RH)))
	{
		return false;
	}
	TestEqual(TEXT("Width round-trips"), RW, 4);
	TestEqual(TEXT("Height round-trips"), RH, 4);
	for (int32 P = 0; P < 16; ++P)
	{
		if (ReadBack[P] != Pixels[P])
		{
			AddError(FString::Printf(TEXT("Pixel %d did not round-trip (got %s, want %s)"),
				P, *ReadBack[P].ToString(), *Pixels[P].ToString()));
			return false;
		}
	}

	// Re-run REUSES the existing texture object (find-reuse recipe — no shadowing duplicate).
	UTexture2D* Rewritten = FBulkDebakeUtils::WriteSheetTexture(TempPath, TEXT("T_BulkDebakeRoundTrip"), 4, 4, Pixels);
	TestEqual(TEXT("Re-run reuses the prior output object"), Rewritten, Written);
	TestNotNull(TEXT("Valid transient preview builds"),
		FBulkDebakeUtils::CreateTransientPreviewTexture(4, 4, Pixels));
	return true;
}

// ─── 3. Positive geometry with an empty compressed payload fails before LockMip. UE 5.1-5.7
//        otherwise log the failed lock as an Error, converting recoverable bad input into a failed
//        automation run. Failed reads also clear caller outputs rather than exposing stale pixels. ───

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusBulkDebakeUnreadableTextureRejected,
	"Paper2DPlus.BulkDebake.UnreadableTextureRejected",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusBulkDebakeUnreadableTextureRejected::RunTest(const FString& Parameters)
{
	UTexture2D* Texture = NewObject<UTexture2D>(GetTransientPackage());
	const TArrayView64<uint8> EmptyCompressedData;
	Texture->Source.InitWithCompressedSourceData(
		4, 4, 1, TSF_BGRA8, EmptyCompressedData, TSCF_PNG);

	TArray<FColor> Pixels = { FColor::Red };
	int32 Width = 99;
	int32 Height = 99;
	AddExpectedError(TEXT("has no readable source payload"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("Empty compressed source payload is rejected without locking"),
		FBulkDebakeUtils::LoadPixels(Texture, Pixels, Width, Height));
	TestTrue(TEXT("Failed read clears stale caller pixels"), Pixels.IsEmpty());
	TestEqual(TEXT("Failed read clears width"), Width, 0);
	TestEqual(TEXT("Failed read clears height"), Height, 0);
	return true;
}

// ─── 4. Non-cell-multiple VFX dims are a hard Run() error (the validation that bounds FVfxView::Sample) ───

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusBulkDebakeVfxBadCellSizeRejected,
	"Paper2DPlus.BulkDebake.VfxBadCellSizeRejected",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusBulkDebakeVfxBadCellSizeRejected::RunTest(const FString& Parameters)
{
	const int32 W = 8, H = 4; // 2 cells of 4x4
	TArray<FDebakeSheetInput> Sheets;
	Sheets.Add(BulkDebake_Sheet(BulkDebake_Solid(W, H, FColor(10, 20, 30, 255)), W, H));
	Sheets.Add(BulkDebake_Sheet(BulkDebake_Solid(W, H, FColor(10, 20, 30, 255)), W, H));

	// VFX sheet 6 wide — not a multiple of the 4px cell width.
	Sheets[1].VfxFront = BulkDebake_Solid(6, 4, FColor(0, 255, 255, 255));
	Sheets[1].VfxFrontWidth = 6;

	FDebakeSettings Settings;
	Settings.Columns = 2;
	Settings.Rows = 1;

	FDebakeResult Result;
	FText Error;
	TestFalse(TEXT("Non-cell-multiple VFX dims rejected"), FVariantDebake::Run(Sheets, Settings, Result, Error));
	TestFalse(TEXT("Error message set"), Error.IsEmpty());
	return true;
}

// ─── 5. Name heuristics: normalize / sandwich strip / common prefix / variant label ───

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusBulkDebakeNameHeuristics,
	"Paper2DPlus.BulkDebake.NameHeuristics",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusBulkDebakeNameHeuristics::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("NormalizeName strips vfx + separators + case"),
		FBulkDebakeUtils::NormalizeName(TEXT("VFX_Cool-Down Back")), FString(TEXT("cooldownback")));
	TestEqual(TEXT("StripSandwichTokens removes back/front"),
		FBulkDebakeUtils::StripSandwichTokens(TEXT("cooldownbackfront")), FString(TEXT("cooldown")));

	const TArray<FString> Names = { TEXT("T_Potion_Red"), TEXT("T_Potion_Blue"), TEXT("T_Potion_Bright_Green") };
	TestEqual(TEXT("RawCommonPrefix is the raw longest shared prefix"),
		FBulkDebakeUtils::RawCommonPrefix(Names), FString(TEXT("T_Potion_")));
	TestEqual(TEXT("CommonPrefix trims separators + T_"),
		FBulkDebakeUtils::CommonPrefix(Names), FString(TEXT("Potion")));
	TestEqual(TEXT("CommonPrefix falls back to Debaked when nothing is shared"),
		FBulkDebakeUtils::CommonPrefix({ TEXT("Alpha"), TEXT("Beta") }), FString(TEXT("Debaked")));

	TestEqual(TEXT("VariantLabel strips the shared prefix + leading separators"),
		FBulkDebakeUtils::VariantLabel(TEXT("T_Potion_Red"), TEXT("T_Potion_")), FString(TEXT("Red")));
	TestEqual(TEXT("VariantLabel falls back to the full name when the strip empties it"),
		FBulkDebakeUtils::VariantLabel(TEXT("T_Potion_"), TEXT("T_Potion_")), FString(TEXT("T_Potion_")));

	TestTrue(TEXT("ValidateGridDims accepts a clean grid"), FBulkDebakeUtils::ValidateGridDims(8, 4, 2, 1));
	TestFalse(TEXT("ValidateGridDims rejects non-divisible width"), FBulkDebakeUtils::ValidateGridDims(9, 4, 2, 1));
	TestFalse(TEXT("ValidateGridDims rejects zero dims"), FBulkDebakeUtils::ValidateGridDims(0, 4, 1, 1));
	return true;
}

// ─── 6. VFX prefill: front/back classification + longest-core-wins ───

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusBulkDebakePrefillVfxAssignments,
	"Paper2DPlus.BulkDebake.PrefillVfxAssignments",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusBulkDebakePrefillVfxAssignments::RunTest(const FString& Parameters)
{
	const TArray<FString> Members = { TEXT("T_CoolDown_Potion"), TEXT("T_Speed_Potion"), TEXT("T_Cool") };

	const auto MakeCandidate = [](const TCHAR* Path, const TCHAR* AssetName) -> FBulkDebakeUtils::FVfxPrefillCandidate
	{
		FBulkDebakeUtils::FVfxPrefillCandidate C;
		C.Path = FSoftObjectPath(Path);
		C.NameCore = FBulkDebakeUtils::StripSandwichTokens(FBulkDebakeUtils::NormalizeName(AssetName));
		C.bBack = FString(AssetName).Contains(TEXT("back"), ESearchCase::IgnoreCase);
		return C;
	};

	TArray<FBulkDebakeUtils::FVfxPrefillCandidate> Candidates;
	Candidates.Add(MakeCandidate(TEXT("/Game/Vfx/VFX_Cool.VFX_Cool"), TEXT("VFX_Cool")));                     // short front core "cool"
	Candidates.Add(MakeCandidate(TEXT("/Game/Vfx/VFX_CoolDown_Front.VFX_CoolDown_Front"), TEXT("VFX_CoolDown_Front"))); // longer front core "cooldown"
	Candidates.Add(MakeCandidate(TEXT("/Game/Vfx/VFX_CoolDown_Back.VFX_CoolDown_Back"), TEXT("VFX_CoolDown_Back")));
	Candidates.Add(MakeCandidate(TEXT("/Game/Vfx/VFX_Speed.VFX_Speed"), TEXT("VFX_Speed")));

	TArray<FSoftObjectPath> Front, Back;
	FBulkDebakeUtils::PrefillVfxAssignments(Members, Candidates, Front, Back);

	TestEqual(TEXT("Front sized to members"), Front.Num(), 3);
	TestEqual(TEXT("Longest matching core wins the front slot"),
		Front[0].ToString(), FString(TEXT("/Game/Vfx/VFX_CoolDown_Front.VFX_CoolDown_Front")));
	TestEqual(TEXT("Back classified by the back token"),
		Back[0].ToString(), FString(TEXT("/Game/Vfx/VFX_CoolDown_Back.VFX_CoolDown_Back")));
	TestEqual(TEXT("Second member matches its own core"),
		Front[1].ToString(), FString(TEXT("/Game/Vfx/VFX_Speed.VFX_Speed")));
	TestTrue(TEXT("No back candidate for the second member"), Back[1].IsNull());
	TestEqual(TEXT("Exact short member is not stolen by a longer candidate that merely contains it"),
		Front[2].ToString(), FString(TEXT("/Game/Vfx/VFX_Cool.VFX_Cool")));
	return true;
}

// ─── 7. Composite buffer: overlay OVER base per pixel ───

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusBulkDebakeCompositeBuffer,
	"Paper2DPlus.BulkDebake.CompositeBuffer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusBulkDebakeCompositeBuffer::RunTest(const FString& Parameters)
{
	const FColor BaseC(0, 0, 255, 255);
	TArray<FColor> Base = BulkDebake_Solid(2, 2, BaseC);
	TArray<FColor> Overlay;
	Overlay.Init(FColor(0, 0, 0, 0), 4);
	Overlay[1] = FColor(255, 0, 0, 255); // opaque overlay pixel
	Overlay[2] = FColor(255, 0, 0, 128); // semi-transparent overlay pixel

	TArray<FColor> Out;
	if (!TestTrue(TEXT("Composite succeeds"), FBulkDebakeUtils::BuildCompositeBuffer(Base, Overlay, Out)))
	{
		return false;
	}
	TestEqual(TEXT("Transparent overlay keeps the base"), Out[0], BaseC);
	TestEqual(TEXT("Opaque overlay replaces the base"), Out[1], FColor(255, 0, 0, 255));
	TestEqual(TEXT("Semi overlay matches AlphaOver"), Out[2], FVariantDebake::AlphaOver(Overlay[2], BaseC));
	TestEqual(TEXT("Semi overlay red channel blends"), (int32)Out[2].R, 128);

	TArray<FColor> Mismatched;
	Mismatched.Init(FColor(0, 0, 0, 0), 3);
	TestFalse(TEXT("Size mismatch refused"), FBulkDebakeUtils::BuildCompositeBuffer(Base, Mismatched, Out));
	return true;
}

#endif // WITH_EDITOR
