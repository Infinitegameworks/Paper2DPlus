// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "FrameCues/Paper2DPlusSpawnFlipbookCue.h"
#include "Paper2DPlusNetTypes.h"

namespace Paper2DPlusSpawnFlipbookCueTest
{
	bool SpawnFx_TransformsNearlyEqual(
		const FTransform& Actual,
		const FVector& ExpectedLocation,
		const FRotator& ExpectedRotator,
		const FVector& ExpectedScale)
	{
		return Actual.GetLocation().Equals(ExpectedLocation, KINDA_SMALL_NUMBER)
			&& Actual.GetRotation().Rotator().Equals(ExpectedRotator, KINDA_SMALL_NUMBER)
			&& Actual.GetScale3D().Equals(ExpectedScale, KINDA_SMALL_NUMBER);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusSpawnFlipbookCueTransformTest,
	"Paper2DPlus.FrameCues.SpawnFlipbookCue.EffectWorldTransform",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusSpawnFlipbookCueTransformTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusSpawnFlipbookCueTest;
	const FVector AnchorLocation(100.0f, 5.0f, 50.0f);

	// Facing right: the offset lands in front and above, scaled by the character's size.
	{
		const FTransform Result = UPaper2DPlusSpawnFlipbookCue::ComputeEffectWorldTransform(
			AnchorLocation,
			/*AbsAnchorScaleX=*/2.0f,
			/*AbsAnchorScaleZ=*/3.0f,
			/*bFacingLeft=*/false,
			FVector2D(10.0f, 4.0f),
			/*RotationDegrees=*/15.0f,
			FVector2D(1.5f, 0.5f),
			/*bFlipWithCharacter=*/true);
		TestTrue(TEXT("facing right places, rotates, and scales without mirroring"),
			SpawnFx_TransformsNearlyEqual(
				Result,
				FVector(100.0f + 10.0f * 2.0f, 5.0f, 50.0f + 4.0f * 3.0f),
				FRotator(15.0f, 0.0f, 0.0f),
				FVector(1.5f * 2.0f, 1.0f, 0.5f * 3.0f)));
	}

	// Facing left with flip: offset X, the angle, and the art (negative X scale) mirror together.
	{
		const FTransform Result = UPaper2DPlusSpawnFlipbookCue::ComputeEffectWorldTransform(
			AnchorLocation,
			2.0f,
			3.0f,
			/*bFacingLeft=*/true,
			FVector2D(10.0f, 4.0f),
			15.0f,
			FVector2D(1.5f, 0.5f),
			/*bFlipWithCharacter=*/true);
		TestTrue(TEXT("facing left mirrors offset X, angle, and art together"),
			SpawnFx_TransformsNearlyEqual(
				Result,
				FVector(100.0f - 10.0f * 2.0f, 5.0f, 50.0f + 4.0f * 3.0f),
				FRotator(-15.0f, 0.0f, 0.0f),
				FVector(-1.5f * 2.0f, 1.0f, 0.5f * 3.0f)));
	}

	// Facing left without flip: the effect ignores facing entirely.
	{
		const FTransform Result = UPaper2DPlusSpawnFlipbookCue::ComputeEffectWorldTransform(
			AnchorLocation,
			2.0f,
			3.0f,
			/*bFacingLeft=*/true,
			FVector2D(10.0f, 4.0f),
			15.0f,
			FVector2D(1.5f, 0.5f),
			/*bFlipWithCharacter=*/false);
		TestTrue(TEXT("flip-with-character off ignores facing"),
			SpawnFx_TransformsNearlyEqual(
				Result,
				FVector(100.0f + 10.0f * 2.0f, 5.0f, 50.0f + 4.0f * 3.0f),
				FRotator(15.0f, 0.0f, 0.0f),
				FVector(1.5f * 2.0f, 1.0f, 0.5f * 3.0f)));
	}

	// Degenerate anchor scale clamps instead of collapsing the transform.
	{
		const FTransform Result = UPaper2DPlusSpawnFlipbookCue::ComputeEffectWorldTransform(
			FVector::ZeroVector,
			0.0f,
			0.0f,
			false,
			FVector2D(1.0f, 1.0f),
			0.0f,
			FVector2D(1.0f, 1.0f),
			true);
		TestTrue(TEXT("zero anchor scale is clamped to a finite transform"),
			!Result.ContainsNaN()
			&& Result.GetScale3D().X > 0.0f
			&& Result.GetScale3D().Z > 0.0f);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusSpawnFlipbookCueDefaultsTest,
	"Paper2DPlus.FrameCues.SpawnFlipbookCue.ClassDefaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusSpawnFlipbookCueDefaultsTest::RunTest(const FString& Parameters)
{
	const UPaper2DPlusSpawnFlipbookCue* Defaults =
		GetDefault<UPaper2DPlusSpawnFlipbookCue>();
	if (!TestNotNull(TEXT("spawn flipbook cue CDO"), Defaults))
	{
		return false;
	}

	// The frozen networked-correct class default: a world-visible cosmetic. Changing a NetPolicy
	// class default silently rebases every saved placement that accepted it (delta serialization),
	// so this pin failing means a versioned migration is owed, not that the test is stale.
	TestEqual(TEXT("spawn flipbook cue ships CosmeticOnly"),
		Defaults->NetPolicy, EPaper2DPlusFrameCueNetPolicy::CosmeticOnly);

	TestFalse(TEXT("the cue class is concrete and placeable"),
		Defaults->GetClass()->HasAnyClassFlags(
			CLASS_Abstract | CLASS_Hidden | CLASS_HideDropDown | CLASS_Deprecated));

	// The soft art field must stay a TOP-LEVEL SCALAR soft Paper Flipbook property: the base
	// CollectWarmableEffectArt scan does not recurse into structs or containers, and this is the
	// only warm path a native cue gets.
	TArray<TSoftObjectPtr<UPaperFlipbook>> Warmable;
	UPaper2DPlusSpawnFlipbookCue* Instance = NewObject<UPaper2DPlusSpawnFlipbookCue>();
	Instance->EffectFlipbook = TSoftObjectPtr<UPaperFlipbook>(
		FSoftObjectPath(TEXT("/Game/__Missing/FX_Probe.FX_Probe")));
	Instance->CollectWarmableEffectArt(Warmable);
	TestEqual(TEXT("the structural warm scan collects the soft effect flipbook"),
		Warmable.Num(), 1);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
