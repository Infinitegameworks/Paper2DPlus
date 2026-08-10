// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FPaper2DPlusFrameCuePreviewHost;
class UPaper2DPlusCueBase;
class UPaper2DPlusFrameCuePreviewContext;
class USoundBase;
struct FPaper2DPlusFrameCueContext;

/**
 * Cue behavior execution for the Frame Cues tab preview.
 *
 * Preview runs the same behavior events game dispatch runs, through the same shared execution helper
 * and an isolated editor-preview actor, Profile Component, flipbook, and world. Is Editor Preview
 * remains set so game-specific branches can opt into a lightweight stand-in, while ordinary
 * spawn/effect/trace/debug/sound behavior works without adapter registration.
 *
 * A behavior that fails in preview is contained rather than quarantined: the first failure of a
 * placement is written to the log once for the editor session and badges that placement in the
 * timeline, and every later scrub, play, and teardown keeps dispatching normally.
 */
namespace Paper2DPlusFrameCuePreviewBehavior
{
	/** Timeline presentation for a placement whose preview behavior failed. */
	struct FPlacementBadge
	{
		bool bHasError = false;
		FText ToolTip;
	};

	/**
	 * The one preview notification sink: the placement's behavior runs, then the preview adapters.
	 *
	 * Behavior first mirrors the game order (the cue acts, then the world reacts), so an adapter and a
	 * behavior implementation of the same cue always observe the same sequence.
	 */
	void NotifyPreview(
		UPaper2DPlusCueBase& Cue,
		const FPaper2DPlusFrameCueContext& Context,
		FPaper2DPlusFrameCuePreviewHost* PreviewHost);

	/**
	 * Binds the runtime's context-aware sound helper to the editor preview-audio path, so a designer
	 * authored sound cue is audible while scrubbing and remains owned by deterministic preview
	 * teardown. The runtime module owns the seam and never depends on editor code; this is the editor
	 * half.
	 */
	void RegisterPreviewSoundHandler();
	void UnregisterPreviewSoundHandler();

	/** Badge state the Frame Cues timeline paints for one placement. */
	FPlacementBadge GetPlacementBadge(const UPaper2DPlusCueBase* Cue);

	/**
	 * Seeds the real placement-badge state for the release-gated visual-tour fixture without
	 * manufacturing a Blueprint exception or emitting an error log.
	 *
	 * This editor-private seam exists only so the screenshot can prove the badged presentation.
	 * Every seed must be paired with ClearVisualTourPlacementErrorBadge during fixture cleanup.
	 */
	void SeedVisualTourPlacementErrorBadge(
		const UPaper2DPlusCueBase& Cue,
		const FText& Message);
	void ClearVisualTourPlacementErrorBadge(const UPaper2DPlusCueBase& Cue);

#if WITH_DEV_AUTOMATION_TESTS
	/** Last routed preview-sound request, so a test can pin the helper's preview path. */
	struct FPreviewSoundRequestRecord
	{
		const UPaper2DPlusCueBase* Cue = nullptr;
		const USoundBase* Sound = nullptr;
		float VolumeMultiplier = 0.0f;
		float PitchMultiplier = 0.0f;
		bool bRoutedToPreviewLedger = false;
	};

	void ResetSessionForTests();
	int32 GetPreviewSoundRequestCountForTests();
	FPreviewSoundRequestRecord GetLastPreviewSoundRequestForTests();
	int32 GetBadgedPlacementCountForTests();
#endif
}
