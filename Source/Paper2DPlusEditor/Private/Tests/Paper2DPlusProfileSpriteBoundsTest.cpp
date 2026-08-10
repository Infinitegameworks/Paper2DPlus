// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#if WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "ProfileSpriteBoundsService.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Engine/Texture2D.h"
#include "PaperFlipbook.h"
#include "PaperSprite.h"

namespace Paper2DPlusProfileSpriteBoundsTest
{
	static UTexture2D* ProfileBounds_MakeTexture()
	{
		constexpr int32 Width = 192;
		constexpr int32 Height = 64;
		UTexture2D* Texture = NewObject<UTexture2D>(GetTransientPackage(), NAME_None, RF_Transient);
		Texture->Source.Init(Width, Height, 1, 1, TSF_BGRA8);
		TArray<FColor> Pixels;
		Pixels.SetNumZeroed(Width * Height);

		// Width is captured explicitly: UE 5.1/5.2 build at a language level where reading a
		// constexpr local inside a lambda still counts as a capture (C3493), even though 5.4+
		// accept it uncaptured.
		auto Fill = [&Pixels, Width](const FIntRect& Bounds)
		{
			for (int32 Y = Bounds.Min.Y; Y < Bounds.Max.Y; ++Y)
			{
				for (int32 X = Bounds.Min.X; X < Bounds.Max.X; ++X)
				{
					Pixels[Y * Width + X] = FColor::White;
				}
			}
		};
		Fill(FIntRect(20, 16, 44, 56));       // Anchor (32,32): L/R 12, T16, B24.
		Fill(FIntRect(76, 12, 116, 60));      // Anchor (96,32): L/R 20, T20, B28.
		// Cell three is deliberately blank and must not inflate the maxima.

		uint8* Dest = Texture->Source.LockMip(0);
		FMemory::Memcpy(Dest, Pixels.GetData(), Pixels.Num() * sizeof(FColor));
		Texture->Source.UnlockMip(0);
		Texture->CompressionSettings = TC_EditorIcon;
		Texture->Filter = TF_Nearest;
		Texture->MipGenSettings = TMGS_NoMipmaps;
		Texture->LODGroup = TEXTUREGROUP_Pixels2D;
		Texture->NeverStream = true;
		Texture->SRGB = false;
		Texture->UpdateResource();
		return Texture;
	}

	/** A texture of the given size with exactly the requested content rects filled opaque white.
	 *  Everything else stays fully transparent, so a cell with no rect reads as an empty frame. */
	static UTexture2D* ProfileBounds_MakeTextureWithContent(
		int32 Width,
		int32 Height,
		const TArray<FIntRect>& ContentRects)
	{
		UTexture2D* Texture = NewObject<UTexture2D>(GetTransientPackage(), NAME_None, RF_Transient);
		Texture->Source.Init(Width, Height, 1, 1, TSF_BGRA8);
		TArray<FColor> Pixels;
		Pixels.SetNumZeroed(Width * Height);
		for (const FIntRect& Rect : ContentRects)
		{
			for (int32 Y = Rect.Min.Y; Y < Rect.Max.Y; ++Y)
			{
				for (int32 X = Rect.Min.X; X < Rect.Max.X; ++X)
				{
					Pixels[Y * Width + X] = FColor::White;
				}
			}
		}

		uint8* Dest = Texture->Source.LockMip(0);
		FMemory::Memcpy(Dest, Pixels.GetData(), Pixels.Num() * sizeof(FColor));
		Texture->Source.UnlockMip(0);
		Texture->CompressionSettings = TC_EditorIcon;
		Texture->Filter = TF_Nearest;
		Texture->MipGenSettings = TMGS_NoMipmaps;
		Texture->LODGroup = TEXTUREGROUP_Pixels2D;
		Texture->NeverStream = true;
		Texture->SRGB = false;
		Texture->UpdateResource();
		return Texture;
	}

	static UPaperSprite* ProfileBounds_MakeSprite(
		UObject* Outer,
		UTexture2D* Texture,
		const TCHAR* Name,
		const FIntRect& Bounds)
	{
		UPaperSprite* Sprite = NewObject<UPaperSprite>(Outer, Name, RF_Transient);
		FSpriteAssetInitParameters Init;
		Init.Texture = Texture;
		Init.Offset = Bounds.Min;
		Init.Dimension = FIntPoint(Bounds.Width(), Bounds.Height());
		Init.SetPixelsPerUnrealUnit(2.5f);
		Sprite->InitializeSprite(Init);
		Sprite->SetPivotMode(ESpritePivotMode::Center_Center, FVector2D::ZeroVector, true);
		return Sprite;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusProfileSpriteBoundsAuditRepair,
	"Paper2DPlus.Profile.SpriteBounds.AuditRepairIdempotence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusProfileSpriteBoundsAuditRepair::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusProfileSpriteBoundsTest;

	UPaper2DPlusCharacterProfileAsset* Profile =
		NewObject<UPaper2DPlusCharacterProfileAsset>(GetTransientPackage());
	UTexture2D* Texture = ProfileBounds_MakeTexture();
	UPaperFlipbook* Flipbook = NewObject<UPaperFlipbook>(Profile);
	UPaperSprite* Sprites[] =
	{
		ProfileBounds_MakeSprite(Profile, Texture, TEXT("Frame0"), FIntRect(0, 0, 64, 64)),
		ProfileBounds_MakeSprite(Profile, Texture, TEXT("Frame1"), FIntRect(64, 0, 128, 64)),
		ProfileBounds_MakeSprite(Profile, Texture, TEXT("Frame2"), FIntRect(128, 0, 192, 64)),
	};
	{
		FScopedFlipbookMutator Mutator(Flipbook);
		for (UPaperSprite* Sprite : Sprites)
		{
			FPaperFlipbookKeyFrame Frame;
			Frame.Sprite = Sprite;
			Frame.FrameRun = 1;
			Mutator.KeyFrames.Add(Frame);
		}
	}

	FFlipbookProfileEntry Entry;
	Entry.Identity.FlipbookName = TEXT("Attack");
	Entry.Identity.Flipbook = Flipbook;
	Entry.SourceTexture = Texture;
	Entry.AlignmentData.GridDims = FIntPoint(3, 1);
	Entry.AlignmentData.OriginalCellSize = FIntPoint(64, 64);
	Entry.AlignmentData.UniformCellSize = FIntPoint(64, 64);
	Entry.AlignmentData.AlphaThreshold = 1;
	Entry.CombatData.Frames.SetNum(3);
	Entry.CombatData.FrameExtractionInfo.SetNum(3);
	Entry.CombatData.FrameExtractionInfo[0].SpriteOffset = FIntPoint(3, -2);
	FHitboxData Hitbox;
	Hitbox.X = 20;
	Hitbox.Y = 30;
	Hitbox.Width = 8;
	Hitbox.Height = 6;
	Entry.CombatData.Frames[0].Hitboxes.Add(Hitbox);
	FSocketData Socket;
	Socket.X = 24;
	Socket.Y = 28;
	Entry.CombatData.Frames[0].Sockets.Add(Socket);
	Profile->Flipbooks.Add(MoveTemp(Entry));

	const FProfileSpriteBoundsReport Before = FProfileSpriteBoundsService::AnalyzeProfile(Profile);
	TestEqual(TEXT("One flipbook checked"), Before.FlipbooksChecked, 1);
	TestEqual(TEXT("All three source regions need normalization"), Before.RepairableFrames, 3);
	TestEqual(TEXT("Blank frame is reported separately"), Before.EmptyFrames, 1);
	TestEqual(TEXT("No unsupported inputs"), Before.UnsupportedFrames, 0);
	TestEqual(TEXT("Per-flipbook maximum directional extent produces 40x48"),
		Before.Flipbooks[0].TargetSize, FIntPoint(40, 48));
	TestEqual(TEXT("Frame zero target"),
		Before.Flipbooks[0].Frames[0].TargetBounds, FIntRect(12, 12, 52, 60));
	TestEqual(TEXT("Frame one target"),
		Before.Flipbooks[0].Frames[1].TargetBounds, FIntRect(76, 12, 116, 60));
	TestEqual(TEXT("Blank frame receives the same target without affecting maxima"),
		Before.Flipbooks[0].Frames[2].TargetBounds, FIntRect(140, 12, 180, 60));
	TestEqual(TEXT("Check Profile does not mutate sprite bounds"),
		FMath::RoundToInt32(Sprites[0]->GetSourceSize().X), 64);
	TestEqual(TEXT("Check Profile does not write trim metadata"),
		Profile->Flipbooks[0].CombatData.FrameExtractionInfo[0].TrimOffset,
		FIntPoint::ZeroValue);

	FProfileSpriteBoundsReport After;
	const FProfileSpriteBoundsRepairStats Stats =
		FProfileSpriteBoundsService::RepairProfile(Profile, After, /*bCreateTransaction=*/false);
	TestEqual(TEXT("Three frames repaired"), Stats.FramesRepaired, 3);
	TestEqual(TEXT("Three distinct sprites modified"), Stats.SpritesModified, 3);
	TestEqual(TEXT("Authored hitbox remapped once"), Stats.HitboxesRemapped, 1);
	TestEqual(TEXT("Authored socket remapped once"), Stats.SocketsRemapped, 1);
	TestEqual(TEXT("Post-repair scan is idempotently clean"), After.RepairableFrames, 0);
	TestEqual(TEXT("Blank frame remains visible in diagnostics"), After.EmptyFrames, 1);

	for (int32 FrameIndex = 0; FrameIndex < 3; ++FrameIndex)
	{
		TestEqual(*FString::Printf(TEXT("Frame %d normalized width"), FrameIndex),
			FMath::RoundToInt32(Sprites[FrameIndex]->GetSourceSize().X), 40);
		TestEqual(*FString::Printf(TEXT("Frame %d normalized height"), FrameIndex),
			FMath::RoundToInt32(Sprites[FrameIndex]->GetSourceSize().Y), 48);
		TestTrue(*FString::Printf(TEXT("Frame %d preserves pixels-per-unit"), FrameIndex),
			FMath::IsNearlyEqual(Sprites[FrameIndex]->GetPixelsPerUnrealUnit(), 2.5f));
		FVector2D CustomPivot;
		TestEqual(*FString::Printf(TEXT("Frame %d bakes the anchor as a custom pivot"), FrameIndex),
			Sprites[FrameIndex]->GetPivotMode(CustomPivot), ESpritePivotMode::Custom);
		const FVector2D Pivot = Sprites[FrameIndex]->GetPivotPosition();
		TestEqual(*FString::Printf(TEXT("Frame %d pivot sits on the shared cell anchor"), FrameIndex),
			FIntPoint(FMath::FloorToInt(Pivot.X), FMath::FloorToInt(Pivot.Y)),
			FIntPoint(32 + 64 * FrameIndex, 32));
		TestEqual(*FString::Printf(TEXT("Frame %d clears Profile TrimOffset — alignment is baked"), FrameIndex),
			Profile->Flipbooks[0].CombatData.FrameExtractionInfo[FrameIndex].TrimOffset,
			FIntPoint::ZeroValue);
	}
	TestEqual(TEXT("Content frame stores texture-local tight source offset"),
		Profile->Flipbooks[0].CombatData.FrameExtractionInfo[0].SourceOffset,
		FIntPoint(20, 16));
	TestEqual(TEXT("Authored SpriteOffset survives Repair All"),
		Profile->Flipbooks[0].CombatData.FrameExtractionInfo[0].SpriteOffset,
		FIntPoint(3, -2));
	TestEqual(TEXT("Cached local pivot follows the baked anchor"),
		Profile->Flipbooks[0].CombatData.FrameExtractionInfo[0].CachedPivotLocal,
		FVector2D(20.0f, 20.0f));
	TestEqual(TEXT("Blank frame stores its normalized texture-local region"),
		Profile->Flipbooks[0].CombatData.FrameExtractionInfo[2].SourceOffset,
		FIntPoint(140, 12));
	TestEqual(TEXT("Hitbox remains on the same art after top-left moves"),
		Profile->Flipbooks[0].CombatData.Frames[0].Hitboxes[0].X, 8);
	TestEqual(TEXT("Socket remains on the same art after top-left moves"),
		Profile->Flipbooks[0].CombatData.Frames[0].Sockets[0].X, 12);

	FProfileSpriteBoundsReport SecondPass;
	const FProfileSpriteBoundsRepairStats SecondStats =
		FProfileSpriteBoundsService::RepairProfile(Profile, SecondPass, /*bCreateTransaction=*/false);
	TestEqual(TEXT("Second repair performs no writes"), SecondStats.FramesRepaired, 0);
	TestEqual(TEXT("Second repair modifies no sprites"), SecondStats.SpritesModified, 0);

	// Attention aggregate (TASK-156 U2): the honest "how many frames need the designer" number.
	TestEqual(TEXT("Pre-repair, every repairable frame needs attention"), Before.AttentionFrames, 3);
	TestTrue(TEXT("Pre-repair the profile is not reported clean"), !Before.IsClean());
	TestEqual(TEXT("Post-repair, nothing needs attention"), After.AttentionFrames, 0);
	TestTrue(TEXT("Post-repair the profile is clean"), After.IsClean());
	TestEqual(TEXT("A healthy profile still counts its content frames healthy"),
		After.HealthyFrames, 3);
	return true;
}

// =============================================================================
// U2 DEFECT 1 — an animation whose bounds cannot be established at all.
//
// Every frame is blank, so no non-empty frame can establish uniform bounds. The
// pre-fix code left Kind at Content/Empty with bNeedsRepair false, which Recount
// reads as HEALTHY: the profile reported itself clean while the check had in fact
// failed outright, and the explanation never reached a surface listing problems.
// =============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusProfileSpriteBoundsInvalidExtentIsNotHealthy,
	"Paper2DPlus.Profile.SpriteBounds.InvalidExtentIsNotHealthy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusProfileSpriteBoundsInvalidExtentIsNotHealthy::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusProfileSpriteBoundsTest;

	UPaper2DPlusCharacterProfileAsset* Profile =
		NewObject<UPaper2DPlusCharacterProfileAsset>(GetTransientPackage());
	// No content rects at all: every cell is fully transparent.
	UTexture2D* Texture = ProfileBounds_MakeTextureWithContent(128, 64, {});
	UPaperFlipbook* Flipbook = NewObject<UPaperFlipbook>(Profile);
	UPaperSprite* Sprites[] =
	{
		ProfileBounds_MakeSprite(Profile, Texture, TEXT("BlankA"), FIntRect(0, 0, 64, 64)),
		ProfileBounds_MakeSprite(Profile, Texture, TEXT("BlankB"), FIntRect(64, 0, 128, 64)),
	};
	{
		FScopedFlipbookMutator Mutator(Flipbook);
		for (UPaperSprite* Sprite : Sprites)
		{
			FPaperFlipbookKeyFrame Frame;
			Frame.Sprite = Sprite;
			Frame.FrameRun = 1;
			Mutator.KeyFrames.Add(Frame);
		}
	}

	FFlipbookProfileEntry Entry;
	Entry.Identity.FlipbookName = TEXT("AllBlank");
	Entry.Identity.Flipbook = Flipbook;
	Entry.SourceTexture = Texture;
	Entry.AlignmentData.GridDims = FIntPoint(2, 1);
	Entry.AlignmentData.OriginalCellSize = FIntPoint(64, 64);
	Entry.AlignmentData.UniformCellSize = FIntPoint(64, 64);
	Entry.AlignmentData.AlphaThreshold = 1;
	Entry.CombatData.Frames.SetNum(2);
	Entry.CombatData.FrameExtractionInfo.SetNum(2);
	Profile->Flipbooks.Add(MoveTemp(Entry));

	const FProfileSpriteBoundsReport Report =
		FProfileSpriteBoundsService::AnalyzeProfile(Profile);

	TestEqual(TEXT("No frame is counted healthy when bounds cannot be established"),
		Report.HealthyFrames, 0);
	TestTrue(TEXT("The profile is NOT reported clean"), !Report.IsClean());
	TestTrue(TEXT("The failure is surfaced as needing attention"), Report.HasAttention());
	TestEqual(TEXT("Both frames are classified unsupported"), Report.UnsupportedFrames, 2);

	for (const FProfileSpriteBoundsFrameReport& Frame : Report.Flipbooks[0].Frames)
	{
		TestEqual(TEXT("Frame is unsupported, not content"),
			(int32)Frame.Kind, (int32)EProfileSpriteBoundsFrameKind::Unsupported);
		TestFalse(TEXT("Every unsupported frame carries its explanation"),
			Frame.Diagnostic.IsEmpty());
	}
	return true;
}

// =============================================================================
// U2 DEFECT 2 — the uniform target cannot fit inside a frame's source region.
//
// This frame is genuinely NOT repairable in place (repair cannot grow a source
// region), so bNeedsRepair correctly stays false. What was wrong is that it was
// then invisible to any surface asking "is there anything to do?" via HasRepairs(),
// while its own diagnostic told the user to go re-extract. It must reach the
// aggregate as a real problem.
// =============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusProfileSpriteBoundsTargetDoesNotFitNeedsAttention,
	"Paper2DPlus.Profile.SpriteBounds.TargetDoesNotFitNeedsAttention",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusProfileSpriteBoundsTargetDoesNotFitNeedsAttention::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusProfileSpriteBoundsTest;

	UPaper2DPlusCharacterProfileAsset* Profile =
		NewObject<UPaper2DPlusCharacterProfileAsset>(GetTransientPackage());

	// Cell A is a full 64x64 with wide content, so it sets a large uniform target.
	// Cell B is a cramped 24x24 region whose own content is small — the target derived from A
	// cannot possibly fit inside B's source region.
	UTexture2D* Texture = ProfileBounds_MakeTextureWithContent(
		128, 64,
		{
			FIntRect(20, 12, 44, 52),   // Cell A: anchor (32,32) -> L12 R12 T20 B20.
			FIntRect(70, 6, 82, 18),    // Cell B: anchor (76,12) -> L6 R6 T6 B6.
		});

	UPaperFlipbook* Flipbook = NewObject<UPaperFlipbook>(Profile);
	UPaperSprite* Sprites[] =
	{
		ProfileBounds_MakeSprite(Profile, Texture, TEXT("Roomy"), FIntRect(0, 0, 64, 64)),
		ProfileBounds_MakeSprite(Profile, Texture, TEXT("Cramped"), FIntRect(64, 0, 88, 24)),
	};
	{
		FScopedFlipbookMutator Mutator(Flipbook);
		for (UPaperSprite* Sprite : Sprites)
		{
			FPaperFlipbookKeyFrame Frame;
			Frame.Sprite = Sprite;
			Frame.FrameRun = 1;
			Mutator.KeyFrames.Add(Frame);
		}
	}

	FFlipbookProfileEntry Entry;
	Entry.Identity.FlipbookName = TEXT("Mixed");
	Entry.Identity.Flipbook = Flipbook;
	Entry.SourceTexture = Texture;
	Entry.AlignmentData.AlphaThreshold = 1;
	Entry.CombatData.Frames.SetNum(2);
	Entry.CombatData.FrameExtractionInfo.SetNum(2);
	Profile->Flipbooks.Add(MoveTemp(Entry));

	const FProfileSpriteBoundsReport Report =
		FProfileSpriteBoundsService::AnalyzeProfile(Profile);

	const FProfileSpriteBoundsFrameReport& Cramped = Report.Flipbooks[0].Frames[1];
	TestEqual(TEXT("The cramped frame is classified unsupported"),
		(int32)Cramped.Kind, (int32)EProfileSpriteBoundsFrameKind::Unsupported);
	TestFalse(TEXT("It carries an explanation"), Cramped.Diagnostic.IsEmpty());
	TestFalse(TEXT("It is honestly NOT flagged as in-place repairable"), Cramped.bNeedsRepair);

	TestTrue(TEXT("But it DOES reach the aggregate as needing attention"), Report.HasAttention());
	TestTrue(TEXT("So the profile is not reported clean"), !Report.IsClean());
	TestEqual(TEXT("It is not counted healthy"), Report.Flipbooks[0].HealthyFrames, 0);
	TestTrue(TEXT("Attention covers unsupported frames even with nothing repairable"),
		Report.AttentionFrames >= 1);
	return true;
}

#endif // WITH_EDITOR
