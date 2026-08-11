// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#if WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Paper2DPlusCharacterProfileComponent.h"
#include "Paper2DPlusTypes.h"
#include "PaperFlipbook.h"
#include "PaperFlipbookComponent.h"
#include "PaperSprite.h"
#include "GameFramework/Actor.h"

/*
 * TASK-157 U1 — opt-in runtime apply of the Character Profile's Relative Transform.
 *
 * The contract under test, in one line each:
 *   - default OFF, so every project already reading GetRelativeTransform() in its own spawn code is
 *     byte-identical after this change;
 *   - the apply is an ABSOLUTE assignment, so re-running it — or running it beside a game that
 *     applies the same values itself — lands on the same transform instead of stacking (KTD7). This
 *     is the whole reason it is not a retained delta;
 *   - turning the option off restores the flipbook component's OWN authored transform, captured
 *     before the first apply;
 *   - and the KTD11 case: the per-frame sprite-offset record is stored in WORLD units computed
 *     against the component's scale, so changing scale without retiring and re-seeding that record
 *     leaves the sprite displaced. It self-corrects on the next frame change — which never comes on
 *     a single-key-frame idle, and the Fit assist exists specifically to author non-unit scale, so
 *     this is the normal case rather than an edge case.
 *
 * Worldless: every assertion here is about transform composition and bookkeeping, none of which
 * needs a registered component or BeginPlay. The funnel is driven directly, which is exactly what
 * BeginPlay / SetCharacterProfile / ApplyFrameCuePlaybackSource each call.
 *
 * Helpers are RelXf_-prefixed per the unity-build file-unique-name rule.
 */

namespace
{
	/** A flipbook whose frames carry real sprites, so the offset path can read a pixels-per-unit. */
	UPaperFlipbook* RelXf_MakeFlipbook(UObject* Owner, int32 NumFrames)
	{
		UPaperFlipbook* FB = NewObject<UPaperFlipbook>(Owner);
		FScopedFlipbookMutator Mutator(FB);
		Mutator.FramesPerSecond = 10.0f;
		Mutator.KeyFrames.Empty();
		for (int32 i = 0; i < NumFrames; ++i)
		{
			FPaperFlipbookKeyFrame KF;
			KF.FrameRun = 1;
			KF.Sprite = NewObject<UPaperSprite>(FB);
			Mutator.KeyFrames.Add(KF);
		}
		return FB;
	}

	/** Add one animation entry; returns its live flipbook. */
	UPaperFlipbook* RelXf_AddMove(
		UPaper2DPlusCharacterProfileAsset* Asset,
		const FString& MoveName,
		int32 NumFrames)
	{
		FFlipbookProfileEntry Anim;
		Anim.Identity.FlipbookName = MoveName;
		UPaperFlipbook* FB = RelXf_MakeFlipbook(Asset, NumFrames);
		Anim.Identity.Flipbook = FB;
		Anim.CombatData.Frames.SetNum(NumFrames);
		Anim.CombatData.FrameExtractionInfo.SetNum(NumFrames);
		Asset->Flipbooks.Add(Anim);
		return FB;
	}

	struct FRelXf_Rig
	{
		AActor* Owner = nullptr;
		UPaperFlipbookComponent* FBComp = nullptr;
		UPaper2DPlusCharacterProfileComponent* DataComp = nullptr;
	};

	/** Replication OFF — the resolved context is Standalone, so the placement gates all pass. */
	FRelXf_Rig RelXf_MakeRig()
	{
		FRelXf_Rig Rig;
		Rig.Owner = NewObject<AActor>();
		Rig.FBComp = NewObject<UPaperFlipbookComponent>(Rig.Owner);
		Rig.DataComp = NewObject<UPaper2DPlusCharacterProfileComponent>(Rig.Owner);
		Rig.Owner->AddOwnedComponent(Rig.FBComp);
		Rig.Owner->AddOwnedComponent(Rig.DataComp);
		Rig.DataComp->FlipbookComponent = Rig.FBComp;
		return Rig;
	}

	UPaper2DPlusCharacterProfileAsset* RelXf_MakeProfile(
		const FVector& Location,
		const FRotator& Rotation,
		const FVector& Scale)
	{
		UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
		Asset->RelativeLocation = Location;
		Asset->RelativeRotation = Rotation;
		Asset->RelativeScale3D = Scale;
		return Asset;
	}
}

// =============================================================================
// Default off: nothing is touched, even with a non-identity authored transform.
// =============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusRelativeTransformDefaultOff,
	"Paper2DPlus.Placement.RelativeTransformDefaultOff",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusRelativeTransformDefaultOff::RunTest(const FString& Parameters)
{
	FRelXf_Rig Rig = RelXf_MakeRig();
	UPaper2DPlusCharacterProfileAsset* Asset =
		RelXf_MakeProfile(FVector(10, 0, 20), FRotator::ZeroRotator, FVector(2, 2, 2));
	UPaperFlipbook* Flipbook = RelXf_AddMove(Asset, TEXT("Idle"), 1);
	Asset->Flipbooks[0].CombatData.FrameExtractionInfo[0].SpriteOffset = FIntPoint(8, 4);
	Rig.DataComp->CharacterProfile = Asset;

	const FTransform Authored(FRotator::ZeroRotator, FVector(1, 2, 3), FVector(1, 1, 1));
	Rig.FBComp->SetRelativeTransform(Authored);

	TestFalse(TEXT("the option ships off"), Rig.DataComp->bApplyProfileRelativeTransform);

	Rig.DataComp->ApplyProfileRelativeTransform();

	TestTrue(TEXT("default off leaves the authored transform exactly as the designer set it"),
		Rig.FBComp->GetRelativeTransform().Equals(Authored, 1e-4f));

	// NO-CHURN: with a per-frame sprite offset already seeded, the funnel must not disturb it. This
	// is what catches a retire-without-re-seed asymmetry on the path that every existing project
	// takes — the funnel runs on three lifecycle paths whether or not anyone opted in.
	Rig.FBComp->SetFlipbook(Flipbook);
	Rig.DataComp->HandleFlipbookChanged(Flipbook);
	const FVector SeededLocation = Rig.FBComp->GetComponentLocation();
	TestFalse(TEXT("the sprite offset really was seeded (guards a vacuous comparison)"),
		SeededLocation.Equals(Authored.GetLocation(), 1e-4f));

	Rig.DataComp->ApplyProfileRelativeTransform();
	Rig.DataComp->ApplyProfileRelativeTransform();
	TestTrue(TEXT("default off does not disturb an already-seeded sprite offset"),
		Rig.FBComp->GetComponentLocation().Equals(SeededLocation, 1e-4f));
	TestTrue(TEXT("and still leaves the authored transform alone"),
		Rig.FBComp->GetRelativeTransform().GetScale3D().Equals(FVector(1, 1, 1), 1e-4f));
	return true;
}

// =============================================================================
// Enabled: the profile's values land verbatim, and re-running is idempotent —
// the absolute-assignment property that makes a game-side apply harmless.
// =============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusRelativeTransformAppliesOnceAndIsIdempotent,
	"Paper2DPlus.Placement.RelativeTransformAppliesOnceAndIsIdempotent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusRelativeTransformAppliesOnceAndIsIdempotent::RunTest(const FString& Parameters)
{
	FRelXf_Rig Rig = RelXf_MakeRig();
	UPaper2DPlusCharacterProfileAsset* Asset =
		RelXf_MakeProfile(FVector(10, 0, 20), FRotator(0, 0, 0), FVector(2, 2, 2));
	Rig.DataComp->CharacterProfile = Asset;
	Rig.DataComp->bApplyProfileRelativeTransform = true;

	Rig.DataComp->ApplyProfileRelativeTransform();
	const FTransform Expected = Asset->GetRelativeTransform();
	TestTrue(TEXT("the profile's transform is applied verbatim"),
		Rig.FBComp->GetRelativeTransform().Equals(Expected, 1e-4f));

	// Re-entry must not stack. A retained-delta apply would double the location here.
	Rig.DataComp->ApplyProfileRelativeTransform();
	Rig.DataComp->ApplyProfileRelativeTransform();
	TestTrue(TEXT("re-running the funnel is idempotent — the offset never doubles"),
		Rig.FBComp->GetRelativeTransform().Equals(Expected, 1e-4f));

	// The named double-apply hazard: a game that also transcribes GetRelativeTransform() into the
	// component. Absolute assignment means the two agree instead of compounding.
	Rig.FBComp->SetRelativeTransform(Asset->GetRelativeTransform());
	Rig.DataComp->ApplyProfileRelativeTransform();
	TestTrue(TEXT("an external apply of the same values is not compounded by ours"),
		Rig.FBComp->GetRelativeTransform().Equals(Expected, 1e-4f));
	return true;
}

// =============================================================================
// Reversible: turning the option off restores the captured authored transform.
// =============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusRelativeTransformIsReversible,
	"Paper2DPlus.Placement.RelativeTransformIsReversible",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusRelativeTransformIsReversible::RunTest(const FString& Parameters)
{
	FRelXf_Rig Rig = RelXf_MakeRig();
	Rig.DataComp->CharacterProfile =
		RelXf_MakeProfile(FVector(10, 0, 20), FRotator::ZeroRotator, FVector(2, 2, 2));

	const FTransform Authored(FRotator::ZeroRotator, FVector(1, 2, 3), FVector(0.5f, 0.5f, 0.5f));
	Rig.FBComp->SetRelativeTransform(Authored);

	Rig.DataComp->SetApplyProfileRelativeTransform(true);
	TestFalse(TEXT("enabling actually changed the transform (guards a vacuous restore)"),
		Rig.FBComp->GetRelativeTransform().Equals(Authored, 1e-4f));

	Rig.DataComp->SetApplyProfileRelativeTransform(false);
	TestTrue(TEXT("disabling restores the component's own authored transform"),
		Rig.FBComp->GetRelativeTransform().Equals(Authored, 1e-4f));

	// And the restore is itself idempotent.
	Rig.DataComp->ApplyProfileRelativeTransform();
	TestTrue(TEXT("re-running while off leaves the restored authored transform put"),
		Rig.FBComp->GetRelativeTransform().Equals(Authored, 1e-4f));
	return true;
}

// =============================================================================
// Null resolution: no flipbook component is a quiet no-op, not a crash or an error.
// =============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusRelativeTransformNullTargetIsSafe,
	"Paper2DPlus.Placement.RelativeTransformNullTargetIsSafe",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusRelativeTransformNullTargetIsSafe::RunTest(const FString& Parameters)
{
	// No owner and no flipbook component: GetResolvedFlipbookComponent() returns null on both routes.
	UPaper2DPlusCharacterProfileComponent* Comp =
		NewObject<UPaper2DPlusCharacterProfileComponent>();
	Comp->CharacterProfile =
		RelXf_MakeProfile(FVector(10, 0, 20), FRotator::ZeroRotator, FVector(2, 2, 2));
	Comp->bApplyProfileRelativeTransform = true;

	Comp->ApplyProfileRelativeTransform();
	Comp->SetApplyProfileRelativeTransform(false);

	TestTrue(TEXT("an unresolvable target neither crashes nor logs an error"), true);
	return true;
}

// =============================================================================
// Late profile assignment reaches the same funnel.
// =============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusRelativeTransformLateProfileAssignment,
	"Paper2DPlus.Placement.RelativeTransformLateProfileAssignment",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusRelativeTransformLateProfileAssignment::RunTest(const FString& Parameters)
{
	FRelXf_Rig Rig = RelXf_MakeRig();
	Rig.DataComp->bApplyProfileRelativeTransform = true;

	// Option on, but no profile yet — nothing to place.
	Rig.DataComp->ApplyProfileRelativeTransform();
	TestTrue(TEXT("no profile means no placement"),
		Rig.FBComp->GetRelativeTransform().Equals(FTransform::Identity, 1e-4f));

	UPaper2DPlusCharacterProfileAsset* Asset =
		RelXf_MakeProfile(FVector(7, 0, 11), FRotator::ZeroRotator, FVector(3, 3, 3));
	Rig.DataComp->SetCharacterProfile(Asset);

	TestTrue(TEXT("a profile assigned after the fact is placed through the shared funnel"),
		Rig.FBComp->GetRelativeTransform().Equals(Asset->GetRelativeTransform(), 1e-4f));
	return true;
}

// =============================================================================
// KTD11 — the per-frame sprite-offset record is re-seeded when scale changes.
//
// The assertion is order-independence rather than a hand-computed number: a
// profile whose transform applied BEFORE the animation was warmed must place the
// sprite exactly where one applied AFTER does. Without the retire/re-seed, the
// second ordering loses the offset entirely (SetRelativeTransform overwrites the
// location the offset was added to, while the record still claims it is applied),
// so the two orderings disagree and the stale record corrupts the next delta.
// =============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusRelativeTransformReseedsSpriteOffset,
	"Paper2DPlus.Placement.RelativeTransformReseedsSpriteOffset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusRelativeTransformReseedsSpriteOffset::RunTest(const FString& Parameters)
{
	const FVector ProfileLocation(10, 0, 20);
	const FVector ProfileScale(2, 2, 2);

	// ── Ordering A: place first, then warm the animation (BeginPlay with a flipbook already set).
	FRelXf_Rig RigA = RelXf_MakeRig();
	UPaper2DPlusCharacterProfileAsset* AssetA =
		RelXf_MakeProfile(ProfileLocation, FRotator::ZeroRotator, ProfileScale);
	UPaperFlipbook* FlipbookA = RelXf_AddMove(AssetA, TEXT("Idle"), 1);
	AssetA->Flipbooks[0].CombatData.FrameExtractionInfo[0].SpriteOffset = FIntPoint(8, 4);
	RigA.DataComp->CharacterProfile = AssetA;
	RigA.DataComp->bApplyProfileRelativeTransform = true;
	RigA.FBComp->SetFlipbook(FlipbookA);

	RigA.DataComp->ApplyProfileRelativeTransform();
	RigA.DataComp->HandleFlipbookChanged(FlipbookA);
	const FVector LocationA = RigA.FBComp->GetComponentLocation();

	// ── Ordering B: warm the animation first, then place (late assignment). The offset was seeded
	// against the PRE-apply scale here, which is precisely what must be retired and re-seeded.
	FRelXf_Rig RigB = RelXf_MakeRig();
	UPaper2DPlusCharacterProfileAsset* AssetB =
		RelXf_MakeProfile(ProfileLocation, FRotator::ZeroRotator, ProfileScale);
	UPaperFlipbook* FlipbookB = RelXf_AddMove(AssetB, TEXT("Idle"), 1);
	AssetB->Flipbooks[0].CombatData.FrameExtractionInfo[0].SpriteOffset = FIntPoint(8, 4);
	RigB.DataComp->CharacterProfile = AssetB;
	RigB.FBComp->SetFlipbook(FlipbookB);

	RigB.DataComp->HandleFlipbookChanged(FlipbookB);
	const FVector SeededBeforePlacement = RigB.FBComp->GetComponentLocation();
	RigB.DataComp->SetApplyProfileRelativeTransform(true);
	const FVector LocationB = RigB.FBComp->GetComponentLocation();

	// Non-vacuous guards: the offset really is non-zero, and it really does depend on scale.
	TestFalse(TEXT("the authored sprite offset actually moved the sprite before placement"),
		SeededBeforePlacement.IsNearlyZero());
	TestFalse(TEXT("the placed sprite is not merely at the profile's location — the offset survived"),
		LocationB.Equals(ProfileLocation, 1e-4f));

	TestTrue(
		FString::Printf(
			TEXT("placement order does not change where the sprite lands (A=%s B=%s)"),
			*LocationA.ToCompactString(), *LocationB.ToCompactString()),
		LocationA.Equals(LocationB, 1e-3f));

	// The record must also be consistent going forward: a frame change on a single-key-frame
	// animation re-computes the same offset and therefore must not move the sprite again.
	RigB.DataComp->HandleFrameChanged(0);
	TestTrue(TEXT("a re-fired frame change leaves the placed sprite put (record and reality agree)"),
		RigB.FBComp->GetComponentLocation().Equals(LocationB, 1e-3f));
	return true;
}

// =============================================================================
// A different flipbook component re-captures: restoring must not write the
// PREVIOUS component's authored pose onto the new one.
// =============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusRelativeTransformRecapturesOnTargetChange,
	"Paper2DPlus.Placement.RelativeTransformRecapturesOnTargetChange",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusRelativeTransformRecapturesOnTargetChange::RunTest(const FString& Parameters)
{
	FRelXf_Rig Rig = RelXf_MakeRig();
	Rig.DataComp->CharacterProfile =
		RelXf_MakeProfile(FVector(10, 0, 20), FRotator::ZeroRotator, FVector(2, 2, 2));

	const FTransform FirstAuthored(FRotator::ZeroRotator, FVector(1, 2, 3), FVector(1, 1, 1));
	Rig.FBComp->SetRelativeTransform(FirstAuthored);
	Rig.DataComp->SetApplyProfileRelativeTransform(true);

	// Swap in a second component carrying its OWN authored transform.
	UPaperFlipbookComponent* SecondFBComp = NewObject<UPaperFlipbookComponent>(Rig.Owner);
	Rig.Owner->AddOwnedComponent(SecondFBComp);
	const FTransform SecondAuthored(FRotator::ZeroRotator, FVector(-4, 0, 9), FVector(1, 1, 1));
	SecondFBComp->SetRelativeTransform(SecondAuthored);
	Rig.DataComp->FlipbookComponent = SecondFBComp;

	Rig.DataComp->ApplyProfileRelativeTransform();
	TestTrue(TEXT("the new target receives the profile's placement"),
		SecondFBComp->GetRelativeTransform().Equals(
			Rig.DataComp->CharacterProfile->GetRelativeTransform(), 1e-4f));

	Rig.DataComp->SetApplyProfileRelativeTransform(false);
	TestTrue(TEXT("restoring writes the NEW component's authored pose, not the previous one's"),
		SecondFBComp->GetRelativeTransform().Equals(SecondAuthored, 1e-4f));
	return true;
}

#endif // WITH_EDITOR
