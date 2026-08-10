// Copyright 2026 Infinite Gameworks. All Rights Reserved.
//
// U4 (layered-asset redesign) — per-child layer offsets, runtime side. Worldless:
//   - the pure Paper2DPlusLayerDraw home (ResolveLayerOffsetPx / ResolveTotalOffsetPx tables, the
//     pixel->world conversion at scale 1 and 4.5 / facing L+R / Y sign, base-recipe parity);
//   - the component's per-child DELTA apply + flipbook-change UNDO (AddWorldOffset on a non-root,
//     unattached scene component updates its own relative transform worldlessly — the NetApply
//     SpriteOffsetCosmeticOnNonRootProxy precedent);
//   - the zero-offset byte-identical pin (no AddWorldOffset call is ever made: children never move AND
//     the LastApplied tracker stays EMPTY).
//
// Helpers are LayerOffset_-prefixed (unity-build FILE-UNIQUE test-helper name rule).

#if WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "Paper2DPlusLayerDraw.h"
#include "Paper2DPlusCharacterLayerAsset.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Paper2DPlusCharacterProfileComponent.h"
#include "Paper2DPlusLayerRenderComponent.h"
#include "PaperFlipbook.h"
#include "PaperSpriteComponent.h"

namespace Paper2DPlusLayerOffsetTestUtils
{
	static FCharacterLayer LayerOffset_MakeLayer(const FString& Name, const FVector2D& DefaultPx)
	{
		FCharacterLayer Layer;
		Layer.LayerName = Name;
		Layer.DefaultOffsetPx = DefaultPx;
		return Layer;
	}

	static void LayerOffset_AddAnimOverride(FCharacterLayer& Layer, const FString& Anim, const FVector2D& Px)
	{
		FCharacterLayerAnimationOffset Override;
		Override.AnimationName = Anim;
		Override.OffsetPx = Px;
		Layer.AnimationOffsets.Add(MoveTemp(Override));
	}

	/** A profile entry named "Walk" bound to a (frame-less, pointer-identity) transient flipbook, with
	 *  authored FrameExtractionInfo rows: frame 0 = zero, frame 1 = SpriteOffset(6,-2) + TrimOffset(2,0). */
	struct FLayerOffset_Rig
	{
		UPaper2DPlusCharacterLayerAsset* Asset = nullptr;
		UPaper2DPlusCharacterProfileAsset* Profile = nullptr;
		UPaperFlipbook* WalkFB = nullptr;
		UPaper2DPlusLayerRenderComponent* Comp = nullptr;
		TMap<FString, UPaperSpriteComponent*> Children;
	};

	static FLayerOffset_Rig LayerOffset_MakeRig(TArray<FCharacterLayer> Layers, bool bAuthorFrameOffsets = true)
	{
		FLayerOffset_Rig Rig;
		Rig.Asset = NewObject<UPaper2DPlusCharacterLayerAsset>();
		Rig.Asset->UsageMode = ECharacterLayerUsageMode::RuntimeCustomizable;
		Rig.Asset->Layers = MoveTemp(Layers);

		Rig.Profile = NewObject<UPaper2DPlusCharacterProfileAsset>();
		Rig.WalkFB = NewObject<UPaperFlipbook>();
		FFlipbookProfileEntry Entry;
		Entry.Identity.FlipbookName = TEXT("Walk");
		Entry.Identity.Flipbook = Rig.WalkFB;
		Entry.CombatData.FrameExtractionInfo.SetNum(2);
		if (bAuthorFrameOffsets)
		{
			Entry.CombatData.FrameExtractionInfo[1].SpriteOffset = FIntPoint(6, -2);
			Entry.CombatData.FrameExtractionInfo[1].TrimOffset = FIntPoint(2, 0);
		}
		Rig.Profile->Flipbooks.Add(MoveTemp(Entry));
		Rig.Asset->BaseProfile = Rig.Profile;

		Rig.Comp = NewObject<UPaper2DPlusLayerRenderComponent>();
		Rig.Comp->CharacterLayerAsset = Rig.Asset;
		Rig.Comp->ProfileComponent = NewObject<UPaper2DPlusCharacterProfileComponent>();

		for (const FCharacterLayer& Layer : Rig.Asset->Layers)
		{
			UPaperSpriteComponent* Child = NewObject<UPaperSpriteComponent>();
			Rig.Comp->Test_RegisterLayerComponent(Layer.LayerName, Child, /*bVisible*/ true);
			Rig.Children.Add(Layer.LayerName, Child);
		}
		return Rig;
	}
}

namespace Paper2DPlusLayerOffsetTestUtils
{

// =============================================================================
// Pure resolve tables: per-anim override vs default; null entry / null layer /
// out-of-range frame contribute zero; base SpriteOffset+TrimOffset summed.
// =============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusLayerOffsetResolveTables,
	"Paper2DPlus.LayerOffset.ResolveTables",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusLayerOffsetResolveTables::RunTest(const FString& Parameters)
{
	FCharacterLayer Layer = LayerOffset_MakeLayer(TEXT("cape"), FVector2D(3.0f, 1.0f));
	LayerOffset_AddAnimOverride(Layer, TEXT("Walk"), FVector2D(5.0f, -2.0f));
	LayerOffset_AddAnimOverride(Layer, TEXT("Jump"), FVector2D::ZeroVector); // an authored ZERO override

	// ResolveLayerOffsetPx: override wins (case-insensitive), else default; an authored zero override
	// beats a non-zero default; empty anim name = default.
	TestEqual(TEXT("override wins for its animation"),
		Paper2DPlusLayerDraw::ResolveLayerOffsetPx(Layer, TEXT("Walk")), FVector2D(5.0f, -2.0f));
	TestEqual(TEXT("override match is case-insensitive"),
		Paper2DPlusLayerDraw::ResolveLayerOffsetPx(Layer, TEXT("wAlK")), FVector2D(5.0f, -2.0f));
	TestEqual(TEXT("un-overridden animation falls back to the default"),
		Paper2DPlusLayerDraw::ResolveLayerOffsetPx(Layer, TEXT("Idle")), FVector2D(3.0f, 1.0f));
	TestEqual(TEXT("an authored ZERO override beats the non-zero default"),
		Paper2DPlusLayerDraw::ResolveLayerOffsetPx(Layer, TEXT("Jump")), FVector2D::ZeroVector);
	TestEqual(TEXT("empty animation name reads the default"),
		Paper2DPlusLayerDraw::ResolveLayerOffsetPx(Layer, FString()), FVector2D(3.0f, 1.0f));

	// ResolveTotalOffsetPx: entry base alignment (SpriteOffset+TrimOffset) + layer offset; nullables zero.
	FFlipbookProfileEntry Entry;
	Entry.CombatData.FrameExtractionInfo.SetNum(2);
	Entry.CombatData.FrameExtractionInfo[1].SpriteOffset = FIntPoint(6, -2);
	Entry.CombatData.FrameExtractionInfo[1].TrimOffset = FIntPoint(2, 0);

	TestEqual(TEXT("null entry + null layer = zero"),
		Paper2DPlusLayerDraw::ResolveTotalOffsetPx(nullptr, 0, nullptr, TEXT("Walk")), FVector2D::ZeroVector);
	TestEqual(TEXT("entry only (base pass): SpriteOffset+TrimOffset summed"),
		Paper2DPlusLayerDraw::ResolveTotalOffsetPx(&Entry, 1, nullptr, TEXT("Walk")), FVector2D(8.0f, -2.0f));
	TestEqual(TEXT("entry frame with zero alignment contributes zero"),
		Paper2DPlusLayerDraw::ResolveTotalOffsetPx(&Entry, 0, nullptr, TEXT("Walk")), FVector2D::ZeroVector);
	TestEqual(TEXT("entry + layer: base alignment + per-anim override"),
		Paper2DPlusLayerDraw::ResolveTotalOffsetPx(&Entry, 1, &Layer, TEXT("Walk")), FVector2D(13.0f, -4.0f));
	TestEqual(TEXT("out-of-range frame: layer offset only"),
		Paper2DPlusLayerDraw::ResolveTotalOffsetPx(&Entry, 99, &Layer, TEXT("Walk")), FVector2D(5.0f, -2.0f));
	TestEqual(TEXT("null entry: layer offset only (default path)"),
		Paper2DPlusLayerDraw::ResolveTotalOffsetPx(nullptr, 0, &Layer, TEXT("Idle")), FVector2D(3.0f, 1.0f));

	return true;
}

// =============================================================================
// Pixel->world conversion: scale 1 and 4.5, facing L/R, the Y sign flip, the
// non-abs'd Z scale, the PPU clamp — and BYTE-PARITY with the base component's
// inline recipe (replicated here verbatim as the oracle).
// =============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusLayerOffsetPixelToWorld,
	"Paper2DPlus.LayerOffset.PixelToWorldConversion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusLayerOffsetPixelToWorld::RunTest(const FString& Parameters)
{
	// Scale 1, PPU 1, facing right: X passes through, Y flips sign into Z (screen-down -> world-up).
	TestEqual(TEXT("identity scale, facing right"),
		Paper2DPlusLayerDraw::PixelOffsetToWorld(FVector2D(11.0f, -1.0f), 1.0f, FVector(1, 1, 1), false),
		FVector(11.0f, 0.0f, 1.0f));

	// Scale 4.5 (the offset doc's canonical scaled-character case).
	TestEqual(TEXT("scale 4.5 multiplies both axes"),
		Paper2DPlusLayerDraw::PixelOffsetToWorld(FVector2D(10.0f, 4.0f), 1.0f, FVector(4.5f, 1.0f, 4.5f), false),
		FVector(45.0f, 0.0f, -18.0f));

	// Facing left: X negated, Z untouched.
	TestEqual(TEXT("facing left negates X only"),
		Paper2DPlusLayerDraw::PixelOffsetToWorld(FVector2D(10.0f, 4.0f), 1.0f, FVector(1, 1, 1), true),
		FVector(-10.0f, 0.0f, -4.0f));

	// Negative X scale: magnitude from |scale| — the single sign flip stays the facing flag (audit F4).
	TestEqual(TEXT("negative X scale uses |scale| (no double negation)"),
		Paper2DPlusLayerDraw::PixelOffsetToWorld(FVector2D(10.0f, 0.0f), 1.0f, FVector(-2.0f, 1.0f, 1.0f), true),
		FVector(-20.0f, 0.0f, 0.0f));

	// Negative Z scale is deliberately NOT abs'd (byte-parity with the base recipe).
	TestEqual(TEXT("negative Z scale flips the vertical (base-recipe parity)"),
		Paper2DPlusLayerDraw::PixelOffsetToWorld(FVector2D(0.0f, 4.0f), 1.0f, FVector(1.0f, 1.0f, -1.0f), false),
		FVector(0.0f, 0.0f, 4.0f));

	// PPU divides; PPU is clamped >= 0.001 (division-by-zero guard).
	TestEqual(TEXT("PPU 2 halves the world offset"),
		Paper2DPlusLayerDraw::PixelOffsetToWorld(FVector2D(10.0f, 2.0f), 2.0f, FVector(1, 1, 1), false),
		FVector(5.0f, 0.0f, -1.0f));
	{
		const FVector Clamped = Paper2DPlusLayerDraw::PixelOffsetToWorld(FVector2D(1.0f, 0.0f), 0.0f, FVector(1, 1, 1), false);
		TestTrue(TEXT("PPU 0 clamps to 0.001 (finite result)"), FMath::IsFinite(Clamped.X));
		TestEqual(TEXT("PPU 0 clamps to 0.001 exactly"), (float)Clamped.X, 1000.0f);
	}

	// SHARED-HELPER CONSISTENCY: the base component's inline recipe (Paper2DPlusCharacterProfileComponent's
	// sprite-offset block), replicated verbatim, must agree with the ONE home for a matrix of inputs.
	auto LayerOffset_BaseRecipeOracle = [](const FIntPoint& Combined, float PPUIn, const FVector& CompScale, bool bFacingLeft) -> FVector
	{
		const float PPU = FMath::Max(PPUIn, 0.001f);
		float OffsetX = (Combined.X / PPU) * FMath::Abs(CompScale.X);
		if (bFacingLeft)
		{
			OffsetX = -OffsetX;
		}
		return FVector(OffsetX, 0.0f, (-Combined.Y / PPU) * CompScale.Z);
	};
	const TArray<FIntPoint> Pixels = { FIntPoint(0, 0), FIntPoint(6, -2), FIntPoint(-3, 7), FIntPoint(64, 0) };
	const TArray<float> PPUs = { 1.0f, 0.5f, 2.0f };
	const TArray<FVector> Scales = { FVector(1, 1, 1), FVector(4.5f, 1, 4.5f), FVector(-2, 1, 3), FVector(1, 1, -1) };
	for (const FIntPoint& Px : Pixels)
	{
		for (const float PPU : PPUs)
		{
			for (const FVector& Scale : Scales)
			{
				for (int32 Facing = 0; Facing < 2; ++Facing)
				{
					const bool bLeft = (Facing == 1);
					const FVector Ours = Paper2DPlusLayerDraw::PixelOffsetToWorld(FVector2D(Px), PPU, Scale, bLeft);
					const FVector Oracle = LayerOffset_BaseRecipeOracle(Px, PPU, Scale, bLeft);
					TestTrue(FString::Printf(TEXT("base-recipe parity px=(%d,%d) ppu=%.2f scale=(%s) left=%d"),
						Px.X, Px.Y, PPU, *Scale.ToCompactString(), Facing), Ours.Equals(Oracle, 1e-4f));
				}
			}
		}
	}

	return true;
}

// =============================================================================
// D1 regression: per-child DELTA application on frame/flipbook change; the
// per-anim override rides on top of the base alignment; a flipbook change
// UNDOES the applied offsets (before re-warm) and clears the tracker.
// =============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusLayerOffsetChildDeltaApplyAndUndo,
	"Paper2DPlus.LayerOffset.ChildDeltaApplyAndFlipbookUndo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusLayerOffsetChildDeltaApplyAndUndo::RunTest(const FString& Parameters)
{
	FCharacterLayer Body = LayerOffset_MakeLayer(TEXT("body"), FVector2D(3.0f, 1.0f));
	FCharacterLayer Cape = LayerOffset_MakeLayer(TEXT("cape"), FVector2D(100.0f, 100.0f)); // default must LOSE to the override
	LayerOffset_AddAnimOverride(Cape, TEXT("Walk"), FVector2D(5.0f, -2.0f));
	FLayerOffset_Rig Rig = LayerOffset_MakeRig({ Body, Cape });

	UPaperSpriteComponent* BodyChild = Rig.Children[TEXT("body")];
	UPaperSpriteComponent* CapeChild = Rig.Children[TEXT("cape")];
	TestTrue(TEXT("children start at origin"), BodyChild->GetRelativeLocation().IsNearlyZero());

	// Flipbook change: entry cached + frame-0 offsets applied (frame 0 alignment is zero, so children carry
	// exactly their layer offsets — pixel (x,y) -> world (x, 0, -y) at scale 1 / PPU 1).
	Rig.Comp->Test_HandleFlipbookChanged(Rig.WalkFB);
	TestNotNull(TEXT("resolved profile entry cached on flipbook change (the U4/U5 seam)"),
		Rig.Comp->Test_GetCachedProfileEntry());
	TestTrue(TEXT("frame 0: body child at its default layer offset"),
		BodyChild->GetRelativeLocation().Equals(FVector(3.0f, 0.0f, -1.0f), 1e-4f));
	TestTrue(TEXT("frame 0: cape child at its per-anim OVERRIDE (default lost)"),
		CapeChild->GetRelativeLocation().Equals(FVector(5.0f, 0.0f, 2.0f), 1e-4f));

	// Frame 1: base alignment (6,-2)+(2,0) = (8,-2) rides on top of each layer offset — the D1 parity the
	// base component applies to ITS sprite, now reaching every child (no tearing).
	Rig.Comp->Test_HandleFrameChanged(1);
	TestTrue(TEXT("frame 1: body child = base alignment + default offset"),
		BodyChild->GetRelativeLocation().Equals(FVector(11.0f, 0.0f, 1.0f), 1e-4f));
	TestTrue(TEXT("frame 1: cape child = base alignment + override"),
		CapeChild->GetRelativeLocation().Equals(FVector(13.0f, 0.0f, 4.0f), 1e-4f));
	TestEqual(TEXT("both children tracked"), Rig.Comp->Test_NumTrackedChildOffsets(), 2);

	// Same frame re-fire: delta 0 -> nothing moves (delta-based apply).
	Rig.Comp->Test_HandleFrameChanged(1);
	TestTrue(TEXT("same-frame re-fire leaves the child put (delta 0)"),
		BodyChild->GetRelativeLocation().Equals(FVector(11.0f, 0.0f, 1.0f), 1e-4f));

	// Back to frame 0: the DELTA walks the child back (never SetRelativeLocation).
	Rig.Comp->Test_HandleFrameChanged(0);
	TestTrue(TEXT("frame 0 again: body child back at its layer offset"),
		BodyChild->GetRelativeLocation().Equals(FVector(3.0f, 0.0f, -1.0f), 1e-4f));

	// Flipbook change to a flipbook NOT in the profile: applied offsets UNDONE (children at origin), the
	// tracker cleared, the cached entry dropped.
	Rig.Comp->Test_HandleFrameChanged(1); // put offsets back on first
	UPaperFlipbook* UnknownFB = NewObject<UPaperFlipbook>();
	Rig.Comp->Test_HandleFlipbookChanged(UnknownFB);
	TestTrue(TEXT("flipbook change undoes the body child's applied offset"),
		BodyChild->GetRelativeLocation().IsNearlyZero());
	TestTrue(TEXT("flipbook change undoes the cape child's applied offset"),
		CapeChild->GetRelativeLocation().IsNearlyZero());
	TestEqual(TEXT("tracker cleared by the undo"), Rig.Comp->Test_NumTrackedChildOffsets(), 0);
	TestNull(TEXT("cached entry dropped (unknown flipbook)"), Rig.Comp->Test_GetCachedProfileEntry());

	// Back to the known animation: offsets re-apply from a clean baseline.
	Rig.Comp->Test_HandleFlipbookChanged(Rig.WalkFB);
	TestTrue(TEXT("returning to the animation re-applies frame-0 offsets"),
		BodyChild->GetRelativeLocation().Equals(FVector(3.0f, 0.0f, -1.0f), 1e-4f));

	return true;
}

// =============================================================================
// Zero-offset byte-identical: an asset with NO authored offsets never moves a
// child and never even populates the tracker (no AddWorldOffset call is made).
// =============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusLayerOffsetZeroOffsetByteIdentical,
	"Paper2DPlus.LayerOffset.ZeroOffsetByteIdentical",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusLayerOffsetZeroOffsetByteIdentical::RunTest(const FString& Parameters)
{
	// (a) An entry with NO FrameExtractionInfo at all + zero layer offsets (the null-alignment path).
	{
		FLayerOffset_Rig Rig = LayerOffset_MakeRig(
			{ LayerOffset_MakeLayer(TEXT("body"), FVector2D::ZeroVector) }, /*bAuthorFrameOffsets*/ false);
		Rig.Profile->Flipbooks[0].CombatData.FrameExtractionInfo.Empty();
		Rig.Comp->Test_HandleFlipbookChanged(Rig.WalkFB);
		Rig.Comp->Test_HandleFrameChanged(1);
		Rig.Comp->Test_HandleFrameChanged(0);
		TestTrue(TEXT("zero-offset asset: child never moves"),
			Rig.Children[TEXT("body")]->GetRelativeLocation().IsNearlyZero());
		TestEqual(TEXT("zero-offset asset: tracker stays EMPTY (the apply branch never ran)"),
			Rig.Comp->Test_NumTrackedChildOffsets(), 0);
	}

	// (b) Same with an entry that authors real frame rows but all-zero values.
	{
		FLayerOffset_Rig Rig = LayerOffset_MakeRig(
			{ LayerOffset_MakeLayer(TEXT("body"), FVector2D::ZeroVector) }, /*bAuthorFrameOffsets*/ false);
		// FrameExtractionInfo rows exist (SetNum(2) in the rig) with default-zero offsets.
		Rig.Comp->Test_HandleFlipbookChanged(Rig.WalkFB);
		Rig.Comp->Test_HandleFrameChanged(1);
		TestEqual(TEXT("authored-zero rows: tracker still EMPTY"), Rig.Comp->Test_NumTrackedChildOffsets(), 0);
		TestTrue(TEXT("authored-zero rows: child untouched"),
			Rig.Children[TEXT("body")]->GetRelativeLocation().IsNearlyZero());
	}

	return true;
}

// =============================================================================
// Per-child scale + facing: each child converts with its OWN GetComponentScale()
// and its OWN yaw (the review contradiction flag) — a 4.5x child scales its
// offset, a yaw-180 child mirrors X; both match the shared conversion exactly.
// =============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusLayerOffsetChildOwnScaleAndFacing,
	"Paper2DPlus.LayerOffset.ChildOwnScaleAndFacing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusLayerOffsetChildOwnScaleAndFacing::RunTest(const FString& Parameters)
{
	FCharacterLayer Body = LayerOffset_MakeLayer(TEXT("body"), FVector2D(3.0f, 1.0f));
	FCharacterLayer Cape = LayerOffset_MakeLayer(TEXT("cape"), FVector2D(100.0f, 100.0f));
	LayerOffset_AddAnimOverride(Cape, TEXT("Walk"), FVector2D(5.0f, -2.0f));
	FLayerOffset_Rig Rig = LayerOffset_MakeRig({ Body, Cape });

	UPaperSpriteComponent* BodyChild = Rig.Children[TEXT("body")];
	UPaperSpriteComponent* CapeChild = Rig.Children[TEXT("cape")];

	// The body child renders at 4.5x (the offset doc's scaled-character case); the cape child is yaw-180
	// flipped (facing left) at identity scale. Each child's conversion must read ITS OWN transform.
	BodyChild->SetRelativeScale3D(FVector(4.5f, 1.0f, 4.5f));
	CapeChild->SetRelativeRotation(FRotator(0.0f, 180.0f, 0.0f));

	Rig.Comp->Test_HandleFlipbookChanged(Rig.WalkFB);
	Rig.Comp->Test_HandleFrameChanged(1);

	// body: total (8,-2)+(3,1) = (11,-1) at scale 4.5 facing right -> (49.5, 0, 4.5).
	TestTrue(TEXT("4.5x child scales its world offset by ITS OWN scale"),
		BodyChild->GetRelativeLocation().Equals(FVector(49.5f, 0.0f, 4.5f), 1e-3f));
	// cape: total (8,-2)+(5,-2) = (13,-4) at identity scale, yaw 180 (facing left) -> (-13, 0, 4).
	TestTrue(TEXT("yaw-180 child mirrors X by ITS OWN facing"),
		CapeChild->GetRelativeLocation().Equals(FVector(-13.0f, 0.0f, 4.0f), 1e-3f));

	// Cross-check against the shared conversion (the same call the component makes per child).
	const FVector ExpectedBody = Paper2DPlusLayerDraw::PixelOffsetToWorld(
		FVector2D(11.0f, -1.0f), 1.0f, BodyChild->GetComponentScale(),
		UPaper2DPlusCharacterProfileComponent::ResolveFacingLeft(BodyChild->GetComponentScale(), BodyChild->GetComponentRotation().Yaw));
	TestTrue(TEXT("body child matches the shared conversion verbatim"),
		BodyChild->GetRelativeLocation().Equals(ExpectedBody, 1e-3f));

	// The flipbook-change undo also walks scaled/flipped children back to origin exactly.
	UPaperFlipbook* UnknownFB = NewObject<UPaperFlipbook>();
	Rig.Comp->Test_HandleFlipbookChanged(UnknownFB);
	TestTrue(TEXT("undo returns the scaled child to origin"), BodyChild->GetRelativeLocation().IsNearlyZero());
	TestTrue(TEXT("undo returns the flipped child to origin"), CapeChild->GetRelativeLocation().IsNearlyZero());

	return true;
}

} // namespace Paper2DPlusLayerOffsetTestUtils

#endif // WITH_EDITOR
