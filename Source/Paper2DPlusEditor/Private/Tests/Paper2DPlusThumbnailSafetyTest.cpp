// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "EditorCanvasUtils.h"
#include "Engine/Texture2D.h"
#include "PaperFlipbook.h"
#include "PaperSprite.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFlipbookThumbnailReimportSafetyTest,
	"Paper2DPlus.EditorCanvas.FlipbookThumbnail.ReimportShrinkIsSafeAndAtlasCorrect",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFlipbookThumbnailReimportSafetyTest::RunTest(const FString& Parameters)
{
	UTexture2D* Texture = UTexture2D::CreateTransient(64, 32, PF_B8G8R8A8);
	if (!TestNotNull(TEXT("Transient thumbnail texture created"), Texture))
	{
		return false;
	}
	Texture->Source.Init(64, 32, 1, 1, TSF_BGRA8);

	UPaperFlipbook* Flipbook = NewObject<UPaperFlipbook>(GetTransientPackage());
	UPaperSprite* Sprite = NewObject<UPaperSprite>(Flipbook);
	FSpriteAssetInitParameters SpriteInit;
	SpriteInit.Texture = Texture;
	SpriteInit.Offset = FIntPoint(16, 8);
	SpriteInit.Dimension = FIntPoint(16, 8);
	SpriteInit.SetPixelsPerUnrealUnit(1.0f);
	Sprite->InitializeSprite(SpriteInit);

	{
		FScopedFlipbookMutator Mutator(Flipbook);
		Mutator.FramesPerSecond = 12.0f;
		for (int32 FrameIndex = 0; FrameIndex < 3; ++FrameIndex)
		{
			FPaperFlipbookKeyFrame& Frame = Mutator.KeyFrames.AddDefaulted_GetRef();
			Frame.Sprite = Sprite;
			Frame.FrameRun = 1;
		}
	}

	TSharedRef<SFlipbookThumbnail> Thumbnail =
		SNew(SFlipbookThumbnail).Flipbook(Flipbook);
	FSpriteSlateRegion ExpectedRegion;
	if (!TestTrue(TEXT("Sprite resolves through the shared Slate-atlas seam"),
		FSpriteSlateAtlasUtils::ResolveSprite(Sprite, ExpectedRegion)))
	{
		return false;
	}

	TestTrue(TEXT("Initial thumbnail has a renderable sprite"), Thumbnail->HasTextureForTests());
	TestTrue(TEXT("Thumbnail uses the exact atlas resource returned by UPaperSprite"),
		Thumbnail->GetBrushForTests().GetResourceObject() == ExpectedRegion.Texture);
	TestTrue(TEXT("Thumbnail brush is sized to the atlas cell, not the full source texture"),
		Thumbnail->GetBrushForTests().ImageSize.Equals(ExpectedRegion.PixelSize));
	const FBox2D ActualUVRegion = Thumbnail->GetBrushForTests().GetUVRegion();
	TestTrue(TEXT("Thumbnail uses the atlas-cell minimum UV"),
		FVector2D(ActualUVRegion.Min).Equals(ExpectedRegion.UVMin));
	TestTrue(TEXT("Thumbnail uses the atlas-cell maximum UV"),
		FVector2D(ActualUVRegion.Max).Equals(ExpectedRegion.UVMax));

	// Simulate a live reimport replacing a three-frame flipbook with one frame while a
	// hover timer still points at the old final frame. The refresh/timer paths must
	// reconcile against the live topology before any checked key-frame access.
	Thumbnail->SetCurrentFrameIndexForTests(2);
	{
		FScopedFlipbookMutator Mutator(Flipbook);
		Mutator.KeyFrames.SetNum(1);
	}
	TestTrue(TEXT("One-frame reimport refreshes the surviving frame"),
		Thumbnail->RefreshFromLiveFlipbookForTests());
	TestEqual(TEXT("One-frame reimport clamps the stale current index"),
		Thumbnail->GetCurrentFrameIndexForTests(), 0);
	TestEqual(TEXT("Thumbnail reads the new frame count instead of a construction-time cache"),
		Thumbnail->GetLiveFrameCountForTests(), 1);
	TestFalse(TEXT("Hover animation stops when only one frame remains"),
		Thumbnail->AdvanceAnimationForTests(1.0f));

	{
		FScopedFlipbookMutator Mutator(Flipbook);
		Mutator.KeyFrames.Reset();
	}
	TestFalse(TEXT("Zero-frame reimport enters an intentional empty state"),
		Thumbnail->RefreshFromLiveFlipbookForTests());
	TestEqual(TEXT("Zero-frame reimport clears the initial renderable index"),
		Thumbnail->GetInitialFrameIndexForTests(), INDEX_NONE);
	TestEqual(TEXT("Zero-frame reimport leaves a safe current index"),
		Thumbnail->GetCurrentFrameIndexForTests(), 0);
	TestFalse(TEXT("Zero-frame reimport clears the stale brush"), Thumbnail->HasTextureForTests());
	TestNull(TEXT("Zero-frame reimport clears the stale resource"),
		Thumbnail->GetBrushForTests().GetResourceObject());
	TestFalse(TEXT("Hover animation remains stopped after all frames are removed"),
		Thumbnail->AdvanceAnimationForTests(1.0f));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
