// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#if WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "Paper2DPlusCharacterProfileComponent.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "PaperFlipbook.h"
#include "PaperFlipbookComponent.h"
#include "PaperSprite.h"
#include "Components/SceneComponent.h"
#include "Engine/Texture2D.h"
#include "GameFramework/Actor.h"

/**
 * Worldless coverage for THE shared facing rule (audit F1). The built-in projectile/effect spawn frame
 * events used to flip on negative X scale ONLY, while hitboxes / root motion / replicated anim state
 * flip on |Yaw| in (90,270) OR negative scale. They now all route through
 * UPaper2DPlusCharacterProfileComponent::ResolveFacingLeft, so a YAW-only left-facing flips them too.
 * This pins the rule (the regression-prone part); the frame events call it directly.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFacingRuleTest,
	"Paper2DPlus.Facing.ResolveFacingLeft",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFacingRuleTest::RunTest(const FString& Parameters)
{
	using FacingComp = UPaper2DPlusCharacterProfileComponent;

	// Right-facing baseline: positive scale, zero yaw.
	TestFalse(TEXT("Positive scale, yaw 0 -> not left"), FacingComp::ResolveFacingLeft(FVector(1, 1, 1), 0.0f));
	TestFalse(TEXT("Positive scale, yaw 45 -> not left"), FacingComp::ResolveFacingLeft(FVector(1, 1, 1), 45.0f));

	// THE F1 case: facing left by YAW with POSITIVE scale (the old code missed this).
	TestTrue(TEXT("Yaw 180, positive scale -> left"), FacingComp::ResolveFacingLeft(FVector(1, 1, 1), 180.0f));
	TestTrue(TEXT("Yaw -180 (abs) -> left"), FacingComp::ResolveFacingLeft(FVector(1, 1, 1), -180.0f));
	TestTrue(TEXT("Yaw 170 -> left"), FacingComp::ResolveFacingLeft(FVector(1, 1, 1), 170.0f));

	// Scale-flip (the only case the old code handled): negative X, zero yaw.
	TestTrue(TEXT("Negative X scale, yaw 0 -> left"), FacingComp::ResolveFacingLeft(FVector(-1, 1, 1), 0.0f));
	// Both yaw AND scale -> still left.
	TestTrue(TEXT("Yaw 180 + negative scale -> left"), FacingComp::ResolveFacingLeft(FVector(-1, 1, 1), 180.0f));

	// Open-interval boundaries: exactly 90 / 270 do NOT flip.
	TestFalse(TEXT("Yaw 90 (boundary) -> not left"), FacingComp::ResolveFacingLeft(FVector(1, 1, 1), 90.0f));
	TestFalse(TEXT("Yaw 270 (boundary) -> not left"), FacingComp::ResolveFacingLeft(FVector(1, 1, 1), 270.0f));
	TestTrue(TEXT("Yaw 91 -> left"), FacingComp::ResolveFacingLeft(FVector(1, 1, 1), 91.0f));

	return true;
}

// ==========================================
// SPRITE-OFFSET FACING-FLIP DRIFT TESTS
//
// Regression coverage for the wallslide drift bug (2026-08-05): the per-frame sprite offset was
// tracked as a WORLD-space record and applied via AddWorldOffset deltas. A facing flip that rotates
// the actor/capsule (controller yaw — the standard side-scroller setup) mirrors the already-applied
// offset in world space all by itself, so the record went stale on every flip and the next key-frame
// commit double-applied the correction: the sprite walked 2x the authored offset further from the
// capsule on every left/right flick, and RetireAppliedSpriteOffset left a permanent residue. The
// record is now the PARENT-SPACE delta actually added to the component's relative location, which a
// parent-basis flip cannot invalidate.
//
// Worldless, matching the rest of the plugin's component tests: handlers are invoked directly.
// Unregistered parents do not auto-propagate transforms to children (AttachChildren links form at
// registration), so the rig re-syncs the child after each root mutation — the same refresh a live
// world performs implicitly.
// ==========================================

namespace Paper2DPlusFacingDriftTestUtils
{
	// Authored side offset in pixels; PPU 1 and scale 1 keep world units == pixels.
	constexpr int32 OffsetPx = 10;

	struct FDriftRig
	{
		UPaperSprite* Sprite = nullptr;
		UPaperFlipbook* WallSlideFB = nullptr;
		UPaper2DPlusCharacterProfileAsset* Asset = nullptr;
		AActor* Actor = nullptr;
		USceneComponent* Root = nullptr;
		UPaperFlipbookComponent* FBComp = nullptr;
		UPaper2DPlusCharacterProfileComponent* DataComp = nullptr;
	};

	static UPaperSprite* MakeDriftSprite()
	{
		UTexture2D* Texture = UTexture2D::CreateTransient(32, 32, PF_B8G8R8A8);
		if (!Texture) return nullptr;

		UPaperSprite* Sprite = NewObject<UPaperSprite>();
		FSpriteAssetInitParameters InitParams;
		InitParams.Texture = Texture;
		InitParams.Offset = FIntPoint::ZeroValue;
		InitParams.Dimension = FIntPoint(32, 32);
		InitParams.SetPixelsPerUnrealUnit(1.0f);
		Sprite->InitializeSprite(InitParams);
		return Sprite;
	}

	static UPaperFlipbook* MakeDriftFlipbook(UPaperSprite* Sprite, int32 NumFrames)
	{
		UPaperFlipbook* FB = NewObject<UPaperFlipbook>();
		FScopedFlipbookMutator Mutator(FB);
		Mutator.FramesPerSecond = 10.0f;
		Mutator.KeyFrames.Empty();
		for (int32 i = 0; i < NumFrames; ++i)
		{
			FPaperFlipbookKeyFrame KF;
			KF.Sprite = Sprite;
			KF.FrameRun = 1;
			Mutator.KeyFrames.Add(KF);
		}
		return FB;
	}

	// Capsule stand-in root + child flipbook component + profile whose two frames both author a
	// +OffsetPx side offset (the wallslide shape: sprite pushed toward the wall).
	static FDriftRig BuildDriftRig()
	{
		FDriftRig Rig;
		Rig.Sprite = MakeDriftSprite();
		Rig.WallSlideFB = MakeDriftFlipbook(Rig.Sprite, 2);

		Rig.Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
		FFlipbookProfileEntry Anim;
		Anim.Identity.FlipbookName = TEXT("WallSlide");
		Anim.Identity.Flipbook = Rig.WallSlideFB;
		for (int32 i = 0; i < 2; ++i)
		{
			FSpriteExtractionInfo Info;
			Info.SpriteOffset = FIntPoint(OffsetPx, 0);
			Info.TrimOffset = FIntPoint::ZeroValue;
			Anim.CombatData.FrameExtractionInfo.Add(Info);
		}
		Rig.Asset->Flipbooks.Add(Anim);

		Rig.Actor = NewObject<AActor>();
		Rig.Root = NewObject<USceneComponent>(Rig.Actor);
		Rig.Actor->AddOwnedComponent(Rig.Root);
		Rig.Actor->SetRootComponent(Rig.Root);

		Rig.FBComp = NewObject<UPaperFlipbookComponent>(Rig.Actor);
		Rig.Actor->AddOwnedComponent(Rig.FBComp);
		Rig.FBComp->SetupAttachment(Rig.Root);
		Rig.FBComp->SetFlipbook(Rig.WallSlideFB);

		Rig.DataComp = NewObject<UPaper2DPlusCharacterProfileComponent>(Rig.Actor);
		Rig.Actor->AddOwnedComponent(Rig.DataComp);
		Rig.DataComp->CharacterProfile = Rig.Asset;
		Rig.DataComp->FlipbookComponent = Rig.FBComp;
		Rig.DataComp->bAutoApplyRootMotion = false;
		return Rig;
	}

	// Flip facing the way a stock side-scroller does: rotate the ACTOR (capsule) yaw, sprite rides
	// along as a child. Re-sync the child transform, standing in for live-world propagation.
	static void SetRootYaw(FDriftRig& Rig, float Yaw)
	{
		Rig.Root->SetRelativeRotation(FRotator(0.0f, Yaw, 0.0f));
		Rig.FBComp->UpdateComponentToWorld();
	}
}

// --- Actor-yaw facing flips: the applied side offset must stay pinned to the capsule ---
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFacingSpriteOffsetYawFlipNoDriftTest,
	"Paper2DPlus.Facing.SpriteOffset.YawFlipNoDrift",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFacingSpriteOffsetYawFlipNoDriftTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusFacingDriftTestUtils;

	FDriftRig Rig = BuildDriftRig();
	if (!TestNotNull(TEXT("Rig should build"), Rig.DataComp)) return false;

	// Frame-0 seed through the unified handler: facing right, offset lands at +OffsetPx.
	Rig.DataComp->HandleFlipbookChanged(Rig.WallSlideFB);
	TestTrue(TEXT("Seed: relative X == +OffsetPx"),
		FMath::IsNearlyEqual(Rig.FBComp->GetRelativeLocation().X, (float)OffsetPx, 0.01f));
	TestTrue(TEXT("Seed: world X == +OffsetPx"),
		FMath::IsNearlyEqual(Rig.FBComp->GetComponentLocation().X, (float)OffsetPx, 0.01f));

	// Flick facing left/right repeatedly while the animation keeps stepping frames — the wallslide
	// repro. Pre-fix each cycle pushed the sprite 2x the offset further out (10, -30, +50, ...).
	for (int32 Flick = 1; Flick <= 4; ++Flick)
	{
		const bool bLeft = (Flick % 2) == 1;
		SetRootYaw(Rig, bLeft ? 180.0f : 0.0f);

		// The parent flip alone already mirrors the applied offset — sprite is in the right place
		// BEFORE any commit runs.
		const float ExpectedWorldX = bLeft ? -(float)OffsetPx : (float)OffsetPx;
		TestTrue(FString::Printf(TEXT("Flick %d: world X correct immediately after flip"), Flick),
			FMath::IsNearlyEqual(Rig.FBComp->GetComponentLocation().X, ExpectedWorldX, 0.01f));

		// Animation advances a key frame with the new facing — must be a no-op, not a re-apply.
		Rig.DataComp->HandleFrameChanged(Flick % 2);
		TestTrue(FString::Printf(TEXT("Flick %d: world X still %.0f after frame commit (no double-apply)"),
				Flick, ExpectedWorldX),
			FMath::IsNearlyEqual(Rig.FBComp->GetComponentLocation().X, ExpectedWorldX, 0.01f));
		TestTrue(FString::Printf(TEXT("Flick %d: sprite stays exactly OffsetPx from the capsule"), Flick),
			FMath::IsNearlyEqual(
				FVector::Dist(Rig.FBComp->GetComponentLocation(), Rig.Root->GetComponentLocation()),
				(float)OffsetPx, 0.01f));
	}

	// The parent-space record never needed to touch the relative location across yaw flips.
	TestTrue(TEXT("Relative X unchanged across yaw flips"),
		FMath::IsNearlyEqual(Rig.FBComp->GetRelativeLocation().X, (float)OffsetPx, 0.01f));

	return true;
}

// --- Sprite-self scale flips: the offset must mirror through the relative location, no drift ---
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFacingSpriteOffsetSelfScaleFlipTest,
	"Paper2DPlus.Facing.SpriteOffset.SelfScaleFlipMirrors",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFacingSpriteOffsetSelfScaleFlipTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusFacingDriftTestUtils;

	FDriftRig Rig = BuildDriftRig();
	if (!TestNotNull(TEXT("Rig should build"), Rig.DataComp)) return false;

	Rig.DataComp->HandleFlipbookChanged(Rig.WallSlideFB);
	TestTrue(TEXT("Seed: world X == +OffsetPx"),
		FMath::IsNearlyEqual(Rig.FBComp->GetComponentLocation().X, (float)OffsetPx, 0.01f));

	// A self-scale flip does NOT move the sprite by itself; here the commit must do the mirroring.
	for (int32 Flick = 1; Flick <= 4; ++Flick)
	{
		const bool bLeft = (Flick % 2) == 1;
		Rig.FBComp->SetRelativeScale3D(FVector(bLeft ? -1.0f : 1.0f, 1.0f, 1.0f));
		Rig.DataComp->HandleFrameChanged(Flick % 2);

		const float ExpectedWorldX = bLeft ? -(float)OffsetPx : (float)OffsetPx;
		TestTrue(FString::Printf(TEXT("Flick %d: world X mirrored to %.0f, magnitude constant"),
				Flick, ExpectedWorldX),
			FMath::IsNearlyEqual(Rig.FBComp->GetComponentLocation().X, ExpectedWorldX, 0.01f));
	}

	return true;
}

// --- Retiring the offset after a yaw flip must leave the sprite exactly at its base pose ---
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFacingSpriteOffsetRetireAfterFlipTest,
	"Paper2DPlus.Facing.SpriteOffset.RetireAfterFlipLeavesNoResidue",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFacingSpriteOffsetRetireAfterFlipTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusFacingDriftTestUtils;

	FDriftRig Rig = BuildDriftRig();
	if (!TestNotNull(TEXT("Rig should build"), Rig.DataComp)) return false;

	Rig.DataComp->HandleFlipbookChanged(Rig.WallSlideFB);

	// Flip facing, then switch to an animation with no profile entry: the flipbook-change path
	// retires the applied offset and never re-seeds. Pre-fix the world-space undo converted through
	// the rotated parent with the wrong sign and left a 2x-offset residue on the relative location.
	SetRootYaw(Rig, 180.0f);
	UPaperFlipbook* UnmappedFB = MakeDriftFlipbook(Rig.Sprite, 1);
	Rig.FBComp->SetFlipbook(UnmappedFB);
	Rig.DataComp->HandleFlipbookChanged(UnmappedFB);

	TestTrue(TEXT("Relative location back to zero after retire"),
		Rig.FBComp->GetRelativeLocation().IsNearlyZero(0.01f));
	TestTrue(TEXT("Sprite sits exactly on the capsule after retire"),
		FVector::Dist(Rig.FBComp->GetComponentLocation(), Rig.Root->GetComponentLocation()) < 0.01f);

	return true;
}

#endif // WITH_EDITOR
