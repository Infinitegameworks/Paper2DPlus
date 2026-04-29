// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FrameEvents/Paper2DPlusFrameEvent.h"
#include "Paper2DPlusScreenFlashFrameEvent.generated.h"

/**
 * One-shot frame event that broadcasts a screen-flash request over the
 * owning component's `OnScreenFlashRequested` delegate. Use for hit-stop
 * flashes, parry feedback, taking-damage visuals.
 *
 * Paper2DPlus does NOT own screen rendering — it broadcasts the request and
 * lets the game bind to render the flash via an HUD widget, post-process
 * material parameter, or full-screen quad. This keeps the plugin free of
 * UMG/SlateCore rendering dependencies while still offering a typed
 * authoring class.
 *
 * If the character profile component has no OnScreenFlashRequested binding
 * the event is a silent no-op.
 */
UCLASS(Blueprintable, DisplayName = "Screen Flash Frame Event")
class PAPER2DPLUS_API UPaper2DPlusScreenFlashFrameEvent : public UPaper2DPlusFrameEvent
{
	GENERATED_BODY()

public:
	/** Color of the flash (alpha is typically used as intensity by handlers).
	 *  Named `FlashColor` rather than `Color` to avoid shadowing the base class
	 *  timeline-display `Color` UPROPERTY. */
	UPROPERTY(EditAnywhere, Category = "Screen Flash")
	FLinearColor FlashColor = FLinearColor(1.0f, 1.0f, 1.0f, 0.5f);

	/** Duration in seconds. Handler controls fade-out curve. */
	UPROPERTY(EditAnywhere, Category = "Screen Flash", meta = (ClampMin = "0.0"))
	float Duration = 0.1f;

	virtual void OnReceiveFrameEvent_Implementation(const FPaper2DPlusFrameEventContext& Context) override;
};
