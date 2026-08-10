// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#if WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"
#include "FlipbookPixelEdit.h"
#include "AsepriteImporter.h"
#include "PaperSprite.h"
#include "SpriteEditorOnlyTypes.h"
#include "UObject/UnrealType.h"
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 1)
#include "ContentBrowserMenuContexts.h"
#include "Paper2DPlusEditorIcons.h"
#include "ToolMenu.h"
#include "ToolMenuEntry.h"
#include "ToolMenus.h"
#endif

/**
 * Worldless tests for the Flipbook Draw pixel core (FFlipbookPixelEdit). They build real UPaperSprites
 * from known FColor buffers (the same materialise pipeline the importer uses), then exercise the
 * windowed write-back, the brush stamping, and — most importantly — that editing one frame on a SHARED
 * sprite sheet never disturbs a sibling frame's pixels.
 *
 * Helpers carry a FILE-UNIQUE PREFIX (FlipbookDraw_*) because unity builds concatenate test .cpp files
 * into one TU — generic anon-namespace helper names collide (see CLAUDE.md / PR #158).
 */
namespace Paper2DPlusFlipbookDrawTestUtils
{
	FString FlipbookDraw_TempDir()
	{
		return FString::Printf(TEXT("/Temp/P2DPFlipbookDrawTest_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
	}

	// Materialise a single WxH FColor buffer into a real UPaperSprite (its own 1-cell sheet).
	UPaperSprite* FlipbookDraw_MakeSprite(const TArray<FColor>& Pixels, int32 W, int32 H, const FString& Dir, const FString& Name)
	{
		TArray<TArray<FColor>> Frames;
		Frames.Add(Pixels);
		UTexture2D* Tex = FAsepriteImporter::CreatePerLayerSpriteSheetTexture(Frames, W, H, Dir, Name + TEXT("_Sheet"));
		if (!Tex) { return nullptr; }
		TArray<UPaperSprite*> Sprites = FAsepriteImporter::CreateSpritesFromSheet(Tex, W, H, 1, Dir, Name);
		return Sprites.Num() > 0 ? Sprites[0] : nullptr;
	}

	// Materialise two WxH buffers packed into ONE shared sheet → two sprites at different sub-rects.
	bool FlipbookDraw_MakeTwoSpritesOnSheet(const TArray<FColor>& F0, const TArray<FColor>& F1, int32 W, int32 H,
		const FString& Dir, const FString& Name, UPaperSprite*& OutS0, UPaperSprite*& OutS1)
	{
		TArray<TArray<FColor>> Frames;
		Frames.Add(F0);
		Frames.Add(F1);
		UTexture2D* Tex = FAsepriteImporter::CreatePerLayerSpriteSheetTexture(Frames, W, H, Dir, Name + TEXT("_Sheet"));
		if (!Tex) { return false; }
		TArray<UPaperSprite*> Sprites = FAsepriteImporter::CreateSpritesFromSheet(Tex, W, H, 2, Dir, Name);
		if (Sprites.Num() != 2 || !Sprites[0] || !Sprites[1]) { return false; }
		OutS0 = Sprites[0];
		OutS1 = Sprites[1];
		return true;
	}

	TArray<FColor> FlipbookDraw_Solid(int32 W, int32 H, FColor C)
	{
		TArray<FColor> P;
		P.Init(C, W * H);
		return P;
	}

	// RenderGeometry / CollisionGeometry are protected on UPaperSprite, but they are UPROPERTYs — reach
	// them reflectively so the test can opt render geometry into the pixel-derived mode and then read
	// back what RebuildData actually derived.
	FSpriteGeometryCollection* FlipbookDraw_Geometry(UPaperSprite* Sprite, const TCHAR* FieldName)
	{
		FStructProperty* Prop = CastField<FStructProperty>(UPaperSprite::StaticClass()->FindPropertyByName(FName(FieldName)));
		return (Sprite && Prop) ? Prop->ContainerPtrToValuePtr<FSpriteGeometryCollection>(Sprite) : nullptr;
	}

	// Whole-pixel accessors so the assertions stay int32-vs-int32. FVector2D is double under LWC and
	// FMath::RoundToInt(double) returns int64, so an unconverted result makes TestEqual ambiguous between
	// its int32 and int64 overloads (C2666) — every pixel value goes through this one narrowing cast.
	int32 FlipbookDraw_Px(double Value)
	{
		return static_cast<int32>(FMath::RoundToInt(Value));
	}

	int32 FlipbookDraw_BoxWidthPx(const FSpriteGeometryCollection& Geom)
	{
		return Geom.Shapes.Num() > 0 ? FlipbookDraw_Px(Geom.Shapes[0].BoxSize.X) : -1;
	}

	int32 FlipbookDraw_BoxLeftPx(const FSpriteGeometryCollection& Geom)
	{
		return Geom.Shapes.Num() > 0
			? FlipbookDraw_Px(Geom.Shapes[0].BoxPosition.X - Geom.Shapes[0].BoxSize.X * 0.5)
			: -1;
	}
}
namespace Paper2DPlusFlipbookDrawTestUtils
{

// ─── WriteFrame → ReadFrame round-trips a known buffer (pins FColor↔BGRA byte order) ───

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFlipbookDrawWriteReadRoundTrip,
	"Paper2DPlus.FlipbookDraw.WriteReadRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFlipbookDrawWriteReadRoundTrip::RunTest(const FString& Parameters)
{
	const int32 W = 5, H = 4;
	const FString Dir = FlipbookDraw_TempDir();
	UPaperSprite* Sprite = FlipbookDraw_MakeSprite(FlipbookDraw_Solid(W, H, FColor(10, 20, 30, 255)), W, H, Dir, TEXT("rt"));
	if (!TestNotNull(TEXT("Sprite materialised"), Sprite)) { return false; }

	// A distinctive per-pixel pattern (different R/G/B/A so a channel swap would be caught).
	TArray<FColor> Edited; Edited.SetNumUninitialized(W * H);
	for (int32 i = 0; i < W * H; ++i)
	{
		Edited[i] = FColor((uint8)(i * 9 + 1), (uint8)(i * 5 + 2), (uint8)(i * 3 + 3), (uint8)(i % 2 ? 200 : 255));
	}

	if (!TestTrue(TEXT("WriteFrame succeeds"), FFlipbookPixelEdit::WriteFrame(Sprite, Edited, W, H))) { return false; }

	TArray<FColor> Back; int32 bw = 0, bh = 0;
	if (!TestTrue(TEXT("ReadFrame succeeds"), FFlipbookPixelEdit::ReadFrame(Sprite, Back, bw, bh))) { return false; }
	TestEqual(TEXT("width"), bw, W);
	TestEqual(TEXT("height"), bh, H);

	bool bAllMatch = (Back.Num() == W * H);
	for (int32 i = 0; bAllMatch && i < W * H; ++i)
	{
		if (Back[i] != Edited[i]) { bAllMatch = false; }
	}
	TestTrue(TEXT("All written pixels read back identically (RGBA preserved)"), bAllMatch);
	return true;
}

// ─── Editing one frame never disturbs a SIBLING frame packed on the same sheet ────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFlipbookDrawSiblingIsolation,
	"Paper2DPlus.FlipbookDraw.SiblingFrameIsolation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFlipbookDrawSiblingIsolation::RunTest(const FString& Parameters)
{
	const int32 W = 6, H = 6;
	const FString Dir = FlipbookDraw_TempDir();

	const FColor Frame0Color(0, 0, 0, 0);          // transparent
	const FColor Frame1Color(33, 77, 122, 255);    // a known opaque color we expect to survive
	const TArray<FColor> Frame1Original = FlipbookDraw_Solid(W, H, Frame1Color);

	UPaperSprite* S0 = nullptr;
	UPaperSprite* S1 = nullptr;
	if (!TestTrue(TEXT("Two sprites materialised on one sheet"),
		FlipbookDraw_MakeTwoSpritesOnSheet(FlipbookDraw_Solid(W, H, Frame0Color), Frame1Original, W, H, Dir, TEXT("sib"), S0, S1)))
	{
		return false;
	}

	// They must share ONE texture (otherwise the test proves nothing).
	TestTrue(TEXT("Both sprites share the same source texture"),
		S0->GetSourceTexture() == S1->GetSourceTexture());

	// Paint frame 0 fully red, committing to the shared sheet's cell 0.
	if (!TestTrue(TEXT("WriteFrame(S0) succeeds"),
		FFlipbookPixelEdit::WriteFrame(S0, FlipbookDraw_Solid(W, H, FColor(255, 0, 0, 255)), W, H)))
	{
		return false;
	}

	// Frame 1's pixels must be byte-for-byte unchanged.
	TArray<FColor> Back1; int32 bw = 0, bh = 0;
	if (!TestTrue(TEXT("ReadFrame(S1) succeeds"), FFlipbookPixelEdit::ReadFrame(S1, Back1, bw, bh))) { return false; }

	bool bSiblingIntact = (Back1.Num() == W * H);
	for (int32 i = 0; bSiblingIntact && i < W * H; ++i)
	{
		if (Back1[i] != Frame1Color) { bSiblingIntact = false; }
	}
	TestTrue(TEXT("Sibling frame untouched by the windowed write"), bSiblingIntact);

	// And frame 0 actually took the paint.
	TArray<FColor> Back0; int32 w0 = 0, h0 = 0;
	if (TestTrue(TEXT("ReadFrame(S0) succeeds"), FFlipbookPixelEdit::ReadFrame(S0, Back0, w0, h0)))
	{
		bool bFrame0Red = (Back0.Num() == W * H);
		for (int32 i = 0; bFrame0Red && i < W * H; ++i)
		{
			if (Back0[i] != FColor(255, 0, 0, 255)) { bFrame0Red = false; }
		}
		TestTrue(TEXT("Painted frame took the red"), bFrame0Red);
	}
	return true;
}

// ─── StampDab clamps to bounds and reports the correct dirty rect ─────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFlipbookDrawStampDabClamp,
	"Paper2DPlus.FlipbookDraw.StampDabClampsAndDirty",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFlipbookDrawStampDabClamp::RunTest(const FString& Parameters)
{
	const int32 W = 8, H = 8;
	TArray<FColor> Buf = FlipbookDraw_Solid(W, H, FColor(0, 0, 0, 0));
	FIntRect Dirty = FFlipbookPixelEdit::EmptyDirtyRect();

	// Brush size 3 centered at the top-left corner → half=1 → covers x,y ∈ {-1,0,1}, clamped to {0,1}.
	FFlipbookPixelEdit::StampDab(Buf, W, H, 0, 0, 3, FColor(255, 255, 255, 255), Dirty);

	TestTrue(TEXT("Dirty rect valid"), FFlipbookPixelEdit::IsDirtyValid(Dirty));
	TestEqual(TEXT("Dirty Min.X"), Dirty.Min.X, 0);
	TestEqual(TEXT("Dirty Min.Y"), Dirty.Min.Y, 0);
	TestEqual(TEXT("Dirty Max.X (exclusive)"), Dirty.Max.X, 2);
	TestEqual(TEXT("Dirty Max.Y (exclusive)"), Dirty.Max.Y, 2);

	auto IsSet = [&](int32 X, int32 Y) { return Buf[Y * W + X] == FColor(255, 255, 255, 255); };
	TestTrue(TEXT("(0,0) set"), IsSet(0, 0));
	TestTrue(TEXT("(1,0) set"), IsSet(1, 0));
	TestTrue(TEXT("(0,1) set"), IsSet(0, 1));
	TestTrue(TEXT("(1,1) set"), IsSet(1, 1));
	TestFalse(TEXT("(2,0) NOT set"), IsSet(2, 0));
	TestFalse(TEXT("(2,2) NOT set"), IsSet(2, 2));
	return true;
}

// ─── StampLine leaves no gaps between distant endpoints ───────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFlipbookDrawStampLineNoGaps,
	"Paper2DPlus.FlipbookDraw.StampLineFillsGaps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFlipbookDrawStampLineNoGaps::RunTest(const FString& Parameters)
{
	const int32 W = 16, H = 16;
	TArray<FColor> Buf = FlipbookDraw_Solid(W, H, FColor(0, 0, 0, 0));
	FIntRect Dirty = FFlipbookPixelEdit::EmptyDirtyRect();

	FFlipbookPixelEdit::StampLine(Buf, W, H, 1, 1, 14, 7, /*BrushSize*/ 1, FColor(255, 255, 255, 255), Dirty);

	const FColor White(255, 255, 255, 255);
	TestTrue(TEXT("start set"), Buf[1 * W + 1] == White);
	TestTrue(TEXT("end set"), Buf[7 * W + 14] == White);

	// Every set pixel must have a set 8-neighbour toward the end — i.e. no isolated gaps along the run.
	int32 SetCount = 0;
	for (int32 i = 0; i < W * H; ++i) { if (Buf[i] == White) { ++SetCount; } }
	// A line from x=1..14 must touch at least 14 distinct columns → ≥14 pixels with brush size 1.
	TestTrue(TEXT("line spans the full horizontal extent (no gaps)"), SetCount >= 14);
	return true;
}

// ─── WriteFrame rejects a buffer whose size doesn't match the frame ───────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFlipbookDrawWriteRejectsBadSize,
	"Paper2DPlus.FlipbookDraw.WriteFrameRejectsBadSize",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFlipbookDrawWriteRejectsBadSize::RunTest(const FString& Parameters)
{
	const int32 W = 4, H = 4;
	const FString Dir = FlipbookDraw_TempDir();
	UPaperSprite* Sprite = FlipbookDraw_MakeSprite(FlipbookDraw_Solid(W, H, FColor(1, 2, 3, 255)), W, H, Dir, TEXT("bad"));
	if (!TestNotNull(TEXT("Sprite materialised"), Sprite)) { return false; }

	TArray<FColor> WrongSize = FlipbookDraw_Solid(W, H + 1, FColor(9, 9, 9, 255)); // mismatched count
	TestFalse(TEXT("WriteFrame rejects mismatched buffer size"), FFlipbookPixelEdit::WriteFrame(Sprite, WrongSize, W, H));

	// And the original pixels are intact after the rejected write.
	TArray<FColor> Back; int32 bw = 0, bh = 0;
	if (TestTrue(TEXT("ReadFrame succeeds"), FFlipbookPixelEdit::ReadFrame(Sprite, Back, bw, bh)))
	{
		bool bIntact = (Back.Num() == W * H);
		for (int32 i = 0; bIntact && i < W * H; ++i) { if (Back[i] != FColor(1, 2, 3, 255)) { bIntact = false; } }
		TestTrue(TEXT("Pixels intact after rejected write"), bIntact);
	}
	return true;
}

// ─── Flood fill stops at a color boundary and doesn't cross it ────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFlipbookDrawFloodFillContiguous,
	"Paper2DPlus.FlipbookDraw.FloodFillContiguous",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFlipbookDrawFloodFillContiguous::RunTest(const FString& Parameters)
{
	const FColor A(10, 10, 10, 255), B(99, 99, 99, 255), C(255, 0, 0, 255);
	const int32 W = 5, H = 1;
	TArray<FColor> Buf = { A, A, B, A, A }; // a barrier at index 2 splits the row

	FIntRect Dirty = FFlipbookPixelEdit::EmptyDirtyRect();
	FFlipbookPixelEdit::FloodFill(Buf, W, H, 0, 0, C, Dirty);

	TestTrue(TEXT("(0) filled"), Buf[0] == C);
	TestTrue(TEXT("(1) filled"), Buf[1] == C);
	TestTrue(TEXT("(2) barrier untouched"), Buf[2] == B);
	TestTrue(TEXT("(3) beyond barrier untouched"), Buf[3] == A);
	TestTrue(TEXT("(4) beyond barrier untouched"), Buf[4] == A);
	TestEqual(TEXT("dirty min x"), Dirty.Min.X, 0);
	TestEqual(TEXT("dirty max x (exclusive)"), Dirty.Max.X, 2);
	return true;
}

// ─── Flood fill of a uniform buffer fills everything ──────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFlipbookDrawFloodFillUniform,
	"Paper2DPlus.FlipbookDraw.FloodFillUniform",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFlipbookDrawFloodFillUniform::RunTest(const FString& Parameters)
{
	const int32 W = 4, H = 4;
	const FColor A(0, 0, 0, 0), C(12, 34, 56, 255);
	TArray<FColor> Buf; Buf.Init(A, W * H);

	FIntRect Dirty = FFlipbookPixelEdit::EmptyDirtyRect();
	FFlipbookPixelEdit::FloodFill(Buf, W, H, 1, 1, C, Dirty);

	bool bAll = true;
	for (int32 i = 0; i < W * H; ++i) { if (Buf[i] != C) { bAll = false; break; } }
	TestTrue(TEXT("entire uniform buffer filled"), bAll);
	TestEqual(TEXT("dirty covers full width"), Dirty.Max.X - Dirty.Min.X, W);
	TestEqual(TEXT("dirty covers full height"), Dirty.Max.Y - Dirty.Min.Y, H);
	return true;
}

// ─── Mirror-X stamping mirrors the dab across the vertical axis ───────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFlipbookDrawMirrorStampX,
	"Paper2DPlus.FlipbookDraw.MirrorStampX",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFlipbookDrawMirrorStampX::RunTest(const FString& Parameters)
{
	const int32 W = 8, H = 8;
	const FColor White(255, 255, 255, 255);
	TArray<FColor> Buf; Buf.Init(FColor(0, 0, 0, 0), W * H);
	FIntRect Dirty = FFlipbookPixelEdit::EmptyDirtyRect();

	// Brush size 1 at (1,2), mirror X only → also stamps (W-1-1=6, 2). No Y mirror.
	FFlipbookPixelEdit::StampDabMirrored(Buf, W, H, 1, 2, 1, White, /*bMirrorX*/ true, /*bMirrorY*/ false, Dirty);

	auto Set = [&](int32 X, int32 Y) { return Buf[Y * W + X] == White; };
	TestTrue(TEXT("origin (1,2) set"), Set(1, 2));
	TestTrue(TEXT("mirror (6,2) set"), Set(6, 2));
	TestFalse(TEXT("no Y mirror (1,5)"), Set(1, 5));
	TestFalse(TEXT("no Y mirror (6,5)"), Set(6, 5));
	return true;
}

// ─── Shape tools keep their outlines gap-free and honor drawing symmetry ───────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFlipbookDrawRectangleStamp,
	"Paper2DPlus.FlipbookDraw.RectangleStampMirrorsOutline",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFlipbookDrawRectangleStamp::RunTest(const FString& Parameters)
{
	const int32 W = 10, H = 6;
	const FColor White(255, 255, 255, 255);
	const FColor Clear(0, 0, 0, 0);
	TArray<FColor> Buf; Buf.Init(Clear, W * H);
	FIntRect Dirty = FFlipbookPixelEdit::EmptyDirtyRect();

	// Outline (1,1)-(3,4), mirrored left/right into (6,1)-(8,4).
	FFlipbookPixelEdit::StampRectangleMirrored(
		Buf, W, H, FIntPoint(1, 1), FIntPoint(3, 4), 1, White,
		/*bMirrorX*/ true, /*bMirrorY*/ false, Dirty);

	auto Pixel = [&](int32 X, int32 Y) { return Buf[Y * W + X]; };
	for (int32 Y = 1; Y <= 4; ++Y)
	{
		TestEqual(FString::Printf(TEXT("left outline at y=%d"), Y), Pixel(1, Y), White);
		TestEqual(FString::Printf(TEXT("left outline edge at y=%d"), Y), Pixel(3, Y), White);
		TestEqual(FString::Printf(TEXT("mirrored outline at y=%d"), Y), Pixel(6, Y), White);
		TestEqual(FString::Printf(TEXT("mirrored outline edge at y=%d"), Y), Pixel(8, Y), White);
	}
	for (int32 X = 1; X <= 3; ++X)
	{
		TestEqual(FString::Printf(TEXT("left top edge at x=%d"), X), Pixel(X, 1), White);
		TestEqual(FString::Printf(TEXT("left bottom edge at x=%d"), X), Pixel(X, 4), White);
	}
	for (int32 X = 6; X <= 8; ++X)
	{
		TestEqual(FString::Printf(TEXT("mirrored top edge at x=%d"), X), Pixel(X, 1), White);
		TestEqual(FString::Printf(TEXT("mirrored bottom edge at x=%d"), X), Pixel(X, 4), White);
	}
	TestEqual(TEXT("left rectangle interior stays clear"), Pixel(2, 2), Clear);
	TestEqual(TEXT("mirrored rectangle interior stays clear"), Pixel(7, 2), Clear);
	TestEqual(TEXT("space between rectangles stays clear"), Pixel(5, 2), Clear);
	TestEqual(TEXT("rectangle dirty minimum X"), Dirty.Min.X, 1);
	TestEqual(TEXT("rectangle dirty minimum Y"), Dirty.Min.Y, 1);
	TestEqual(TEXT("rectangle dirty maximum X is half-open"), Dirty.Max.X, 9);
	TestEqual(TEXT("rectangle dirty maximum Y is half-open"), Dirty.Max.Y, 5);

	TArray<FColor> Degenerate; Degenerate.Init(Clear, W * H);
	FIntRect DegenerateDirty = FFlipbookPixelEdit::EmptyDirtyRect();
	FFlipbookPixelEdit::StampRectangle(
		Degenerate, W, H, FIntPoint(3, 1), FIntPoint(3, 4), 1, White, DegenerateDirty);
	for (int32 Y = 1; Y <= 4; ++Y)
	{
		TestEqual(FString::Printf(TEXT("one-pixel-wide rectangle stays a vertical line at y=%d"), Y),
			Degenerate[Y * W + 3], White);
	}
	return true;
}

// ─── Whole-frame transforms preserve dimensions and map every pixel exactly ─────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFlipbookDrawFrameTransforms,
	"Paper2DPlus.FlipbookDraw.FrameTransformsPreserveDimensions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFlipbookDrawFrameTransforms::RunTest(const FString& Parameters)
{
	const int32 W = 3, H = 2;
	const FColor A(1, 0, 0, 255), B(2, 0, 0, 255), C(3, 0, 0, 255);
	const FColor D(4, 0, 0, 255), E(5, 0, 0, 255), F(6, 0, 0, 255);
	const TArray<FColor> Original = { A, B, C, D, E, F };

	auto Verify = [&](const TCHAR* Label, EFlipbookPixelTransform Transform, const TArray<FColor>& Expected)
	{
		TArray<FColor> Actual = Original;
		TestTrue(FString::Printf(TEXT("%s reports a change"), Label),
			FFlipbookPixelEdit::TransformFrame(Actual, W, H, Transform));
		TestEqual(FString::Printf(TEXT("%s keeps the pixel count"), Label), Actual.Num(), W * H);
		for (int32 Index = 0; Index < Expected.Num(); ++Index)
		{
			TestEqual(FString::Printf(TEXT("%s pixel %d"), Label, Index), Actual[Index], Expected[Index]);
		}
	};

	Verify(TEXT("flip horizontal"), EFlipbookPixelTransform::FlipHorizontal, TArray<FColor>{ C, B, A, F, E, D });
	Verify(TEXT("flip vertical"), EFlipbookPixelTransform::FlipVertical, TArray<FColor>{ D, E, F, A, B, C });
	Verify(TEXT("rotate 180"), EFlipbookPixelTransform::Rotate180, TArray<FColor>{ F, E, D, C, B, A });

	TArray<FColor> Symmetric = { A, B, A, C, D, C };
	const TArray<FColor> SymmetricBefore = Symmetric;
	TestFalse(TEXT("a symmetric horizontal flip reports no pixel change"),
		FFlipbookPixelEdit::TransformFrame(Symmetric, W, H, EFlipbookPixelTransform::FlipHorizontal));
	TestTrue(TEXT("a no-op transform leaves the buffer untouched"), Symmetric == SymmetricBefore);
	return true;
}

// ─── One-pixel nudges expose transparent edges; clear/no-op history is detectable ───

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFlipbookDrawNudgeAndClear,
	"Paper2DPlus.FlipbookDraw.NudgeAndClearUseTransparentEdges",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFlipbookDrawNudgeAndClear::RunTest(const FString& Parameters)
{
	const int32 W = 3, H = 3;
	const FColor White(255, 255, 255, 255);
	const FColor Clear(0, 0, 0, 0);
	TArray<FColor> Original; Original.Init(Clear, W * H);
	Original[1 * W + 1] = White;

	auto VerifyNudge = [&](const TCHAR* Label, EFlipbookPixelTransform Transform, int32 ExpectedX, int32 ExpectedY)
	{
		TArray<FColor> Actual = Original;
		TestTrue(FString::Printf(TEXT("%s reports a change"), Label),
			FFlipbookPixelEdit::TransformFrame(Actual, W, H, Transform));
		for (int32 Y = 0; Y < H; ++Y)
		{
			for (int32 X = 0; X < W; ++X)
			{
				const FColor Expected = (X == ExpectedX && Y == ExpectedY) ? White : Clear;
				TestEqual(FString::Printf(TEXT("%s pixel (%d,%d)"), Label, X, Y), Actual[Y * W + X], Expected);
			}
		}
	};

	VerifyNudge(TEXT("nudge left"), EFlipbookPixelTransform::ShiftLeft, 0, 1);
	VerifyNudge(TEXT("nudge right"), EFlipbookPixelTransform::ShiftRight, 2, 1);
	VerifyNudge(TEXT("nudge up"), EFlipbookPixelTransform::ShiftUp, 1, 0);
	VerifyNudge(TEXT("nudge down"), EFlipbookPixelTransform::ShiftDown, 1, 2);

	TArray<FColor> Cleared = Original;
	TestTrue(TEXT("clear reports the first change"),
		FFlipbookPixelEdit::TransformFrame(Cleared, W, H, EFlipbookPixelTransform::Clear));
	TestFalse(TEXT("clearing an already-clear frame is a no-op"),
		FFlipbookPixelEdit::TransformFrame(Cleared, W, H, EFlipbookPixelTransform::Clear));

	TArray<FColor> Invalid = Original;
	Invalid.Pop();
	const TArray<FColor> InvalidBefore = Invalid;
	TestFalse(TEXT("invalid pixel count fails closed"),
		FFlipbookPixelEdit::TransformFrame(Invalid, W, H, EFlipbookPixelTransform::FlipHorizontal));
	TestTrue(TEXT("invalid transform leaves pixels untouched"), Invalid == InvalidBefore);
	return true;
}

#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 1)

// The engine owns UPaperFlipbook's primary asset actions, so Paper2D+ contributes one discoverable
// Common-section submenu. Pin both its placement and its custom icon contract here.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFlipbookDrawCommonMenuTest,
	"Paper2DPlus.FlipbookDraw.CommonMenuUsesDistinctPluginIcons",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFlipbookDrawCommonMenuTest::RunTest(const FString& Parameters)
{
	UToolMenu* FlipbookMenu = UToolMenus::Get()->FindMenu(TEXT("ContentBrowser.AssetContextMenu.PaperFlipbook"));
	if (!TestNotNull(TEXT("Paper Flipbook context menu is registered"), FlipbookMenu))
	{
		return false;
	}

	FToolMenuSection* CommonSection = FlipbookMenu->FindSection(TEXT("CommonAssetActions"));
	if (!TestNotNull(TEXT("Paper2D+ actions live in the Common section"), CommonSection))
	{
		return false;
	}

	const FToolMenuEntry* Paper2DPlusSubMenu = CommonSection->FindEntry(TEXT("Paper2DPlusActions"));
	if (!TestNotNull(TEXT("Common contains one Paper2D+ Actions submenu"), Paper2DPlusSubMenu))
	{
		return false;
	}
	TestTrue(TEXT("Paper2D+ Actions entry is a submenu"), Paper2DPlusSubMenu->SubMenuData.bIsSubMenu);

	UToolMenu* GeneratedSubMenu = NewObject<UToolMenu>();
	UContentBrowserAssetContextMenuContext* Context = NewObject<UContentBrowserAssetContextMenuContext>(GeneratedSubMenu);
	GeneratedSubMenu->Context.AddObject(Context);
	if (!TestTrue(TEXT("Paper2D+ submenu has a ToolMenus population delegate"),
		Paper2DPlusSubMenu->SubMenuData.ConstructMenu.NewToolMenu.IsBound()))
	{
		return false;
	}
	Paper2DPlusSubMenu->SubMenuData.ConstructMenu.NewToolMenu.Execute(GeneratedSubMenu);

	FToolMenuSection* DefaultSection = GeneratedSubMenu->FindSection(TEXT("Default"));
	if (!TestNotNull(TEXT("Paper2D+ submenu creates its default command section"), DefaultSection))
	{
		return false;
	}

	const FToolMenuEntry* AddToProfile = DefaultSection->FindEntry(TEXT("AddToCharacterProfileAsset"));
	const FToolMenuEntry* DrawFrames = DefaultSection->FindEntry(TEXT("EditFramesPaper2DDraw"));
	if (!TestNotNull(TEXT("Add to Character Profile command is grouped under Paper2D+"), AddToProfile)
		|| !TestNotNull(TEXT("Flipbook Draw command is grouped under Paper2D+"), DrawFrames))
	{
		return false;
	}

	const FSlateIcon SubMenuIcon = Paper2DPlusSubMenu->Icon.Get();
	const FSlateIcon AddIcon = AddToProfile->Icon.Get();
	const FSlateIcon DrawIcon = DrawFrames->Icon.Get();
	const FName PluginStyleSet(Paper2DPlusEditorIcons::StyleSet);
	TestEqual(TEXT("Paper2D+ submenu uses the plugin style"), SubMenuIcon.GetStyleSetName(), PluginStyleSet);
	TestEqual(TEXT("Add-to-profile uses the plugin style"), AddIcon.GetStyleSetName(), PluginStyleSet);
	TestEqual(TEXT("Flipbook Draw uses the plugin style"), DrawIcon.GetStyleSetName(), PluginStyleSet);
	TestNotEqual(TEXT("Submenu and add-to-profile icons are distinct"), SubMenuIcon.GetStyleName(), AddIcon.GetStyleName());
	TestNotEqual(TEXT("Submenu and draw icons are distinct"), SubMenuIcon.GetStyleName(), DrawIcon.GetStyleName());
	TestNotEqual(TEXT("Each flipbook command has its own icon"), AddIcon.GetStyleName(), DrawIcon.GetStyleName());

	TestNotNull(TEXT("Paper2D+ submenu brush resolves"), SubMenuIcon.GetOptionalIcon());
	TestNotNull(TEXT("Add-to-profile brush resolves"), AddIcon.GetOptionalIcon());
	TestNotNull(TEXT("Flipbook Draw brush resolves"), DrawIcon.GetOptionalIcon());

	return true;
}

#endif // UE 5.1+ Content Browser ToolMenus context

// ─── A write that moves the art re-derives the sprite's pixel-derived geometry ───
//
// Tight / shrink-wrapped / diced geometry is computed FROM the source alpha and then SERIALIZED on the
// sprite, and the engine never invalidates it when a texture's pixels change (UPaperSprite::PostLoad
// only rebuilds on asset VERSION upgrades). Before the fix, a draw-tool edit that moved the art left
// every frame rendering against its PRE-EDIT box — visibly cropped and offset — until someone happened
// to nudge a sprite property and unknowingly fired PostEditChangeProperty -> RebuildData. Collision is
// not asserted here (it needs a collision domain, hence BodySetup + physics meshes in a headless run)
// but shares the identical CreatePolygonFromBoundingBox path, and defaults to TightBoundingBox.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFlipbookDrawWriteRebuildsDerivedGeometry,
	"Paper2DPlus.FlipbookDraw.WriteRebuildsDerivedGeometry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFlipbookDrawWriteRebuildsDerivedGeometry::RunTest(const FString& Parameters)
{
	const int32 W = 8, H = 6;
	const FString Dir = FlipbookDraw_TempDir();
	const FColor Clear(0, 0, 0, 0);

	// Art confined to columns 2..3, so the tight box is strictly narrower than the cell.
	TArray<FColor> Before = FlipbookDraw_Solid(W, H, Clear);
	for (int32 Y = 0; Y < H; ++Y)
	{
		Before[Y * W + 2] = FColor(255, 0, 0, 255);
		Before[Y * W + 3] = FColor(255, 0, 0, 255);
	}

	UPaperSprite* Sprite = FlipbookDraw_MakeSprite(Before, W, H, Dir, TEXT("geom"));
	if (!TestNotNull(TEXT("Sprite materialised"), Sprite)) { return false; }

	FSpriteGeometryCollection* Render = FlipbookDraw_Geometry(Sprite, TEXT("RenderGeometry"));
	if (!TestNotNull(TEXT("RenderGeometry is reachable"), Render)) { return false; }

	// Opt render geometry into the pixel-derived mode and establish the pre-edit baseline.
	Render->GeometryType = ESpritePolygonMode::TightBoundingBox;
	Sprite->RebuildData();
	if (!TestEqual(TEXT("Baseline box hugs the 2-column art"), FlipbookDraw_BoxWidthPx(*Render), 2)) { return false; }
	const int32 BaselineLeft = FlipbookDraw_BoxLeftPx(*Render);

	// Move AND widen the art: now columns 5..7. Both the extent and the position change.
	TArray<FColor> After = FlipbookDraw_Solid(W, H, Clear);
	for (int32 Y = 0; Y < H; ++Y)
	{
		After[Y * W + 5] = FColor(0, 255, 0, 255);
		After[Y * W + 6] = FColor(0, 255, 0, 255);
		After[Y * W + 7] = FColor(0, 255, 0, 255);
	}
	if (!TestTrue(TEXT("WriteFrame succeeds"), FFlipbookPixelEdit::WriteFrame(Sprite, After, W, H))) { return false; }

	// Ground truth read straight back off the texture that WriteFrame just committed.
	FVector2D TruthPos(ForceInit), TruthSize(ForceInit);
	Sprite->FindTextureBoundingBox(Render->AlphaThreshold, TruthPos, TruthSize);
	if (!TestEqual(TEXT("Oracle sees the moved 3-column art"), FlipbookDraw_Px(TruthSize.X), 3)) { return false; }

	// THE REGRESSION: without the rebuild inside WriteFrame these still describe the old 2-column art.
	TestEqual(TEXT("Geometry width followed the pixels"), FlipbookDraw_BoxWidthPx(*Render), FlipbookDraw_Px(TruthSize.X));
	TestEqual(TEXT("Geometry re-seated on the moved art"), FlipbookDraw_BoxLeftPx(*Render), FlipbookDraw_Px(TruthPos.X));
	TestNotEqual(TEXT("The box actually moved (guards a vacuous pass)"), FlipbookDraw_BoxLeftPx(*Render), BaselineLeft);

	return true;
}

} // namespace Paper2DPlusFlipbookDrawTestUtils

#endif // WITH_EDITOR
