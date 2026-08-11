// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FrameCues/Paper2DPlusFrameCue.h"
#include "UObject/SoftObjectPtr.h"
#include "Paper2DPlusVisualTourFixtureCue.generated.h"

class UPaperFlipbook;

/**
 * WHAT THIS IS: a screenshot fixture for the release-gated visual tour. Nothing else.
 *
 * It exists because the tour builds a transient Character Profile and photographs the Frame Cues tab,
 * and that tab's readiness gate hard-requires at least one placed Cue (see Paper2DPlusVisualTour.cpp,
 * the FrameEventsTabId branch: GetCueCountForTests() must be > 0). An empty timeline would turn a
 * release gate into a FAILED row, so the tour needs concrete Cue and Cue State placements.
 *
 * IT IS NOT A RESTORED BUILT-IN. The plugin ships Frame Cue MACHINERY, not a cue catalogue: after the
 * built-in Cue Types were deleted there are no concrete native cue classes in the runtime module and
 * none that a designer can place. This editor-only class is HideDropdown so the shared Cue Type descriptor
 * (Paper2DPlusFrameCueTypeAuthoring's RejectedClassFlags, which includes CLASS_HideDropDown) reports
 * it as not-Ready. That one gate is what keeps it out of BOTH the "+ Add Cue" picker
 * (DiscoverCueTypes keeps only Ready descriptors) and direct placement
 * (Paper2DPlusFrameCuePlacementAuthoring::IsCueClassCompatible rejects anything not Ready). It is
 * additionally NotBlueprintable, and it lives in the editor module, so it can never reach cooked data.
 * The tour reaches it the only way left: NewObject + FrameCues.Add on its own transient fixture asset.
 *
 * REJECTED ALTERNATIVE: authoring a real Blueprint Cue Type during the tour run. That would put a
 * Blueprint compile inside a headless, release-gating screenshot pass — a compile failure or a
 * reinstance-timing hiccup would take down the gate for a reason that has nothing to do with the UI
 * being photographed. A native fixture has no compile step and no failure mode of its own.
 *
 * Production code must not reference this outside Paper2DPlusVisualTour.cpp. The picker regression
 * test may name StaticClass only to prove the fixture stays undiscoverable. Any other caller that
 * wants this class actually wants a designer-authored Cue Type.
 */
UCLASS(HideDropdown, NotBlueprintable)
class UPaper2DPlusVisualTourFixtureCue : public UPaper2DPlusCue
{
	GENERATED_BODY()

public:
	/**
	 * Demo art the tour points at its own transient Effect library entry, so the Frame Cues screenshot
	 * shows a populated placement instead of an empty row. Soft, so the generic frame-zero warm pass
	 * (UPaper2DPlusCueBase::CollectWarmableEffectArt) picks it up structurally, exactly as it picks up
	 * a soft flipbook field on a Cue Type a designer authored.
	 */
	UPROPERTY(EditAnywhere, Category = "Visual Tour Fixture", meta = (DisplayName = "Demo Effect Flipbook"))
	TSoftObjectPtr<UPaperFlipbook> DemoEffectFlipbook;

	/** Authored placement offset, in the same character-space pixels the tour's other fixtures use. */
	UPROPERTY(EditAnywhere, Category = "Visual Tour Fixture")
	FVector2D Offset = FVector2D::ZeroVector;
};

/** Editor-only Cue State counterpart used to keep the visual tour's timing-form vocabulary honest. */
UCLASS(HideDropdown, NotBlueprintable)
class UPaper2DPlusVisualTourFixtureCueState : public UPaper2DPlusCueState
{
	GENERATED_BODY()
};
